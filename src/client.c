/*
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>
#include <telepathy-glib/telepathy-glib.h>

#include "common.h"

static gboolean success = TRUE;
static GMainLoop *loop = NULL;
static gchar *unix_path = NULL;

static void
remove_unix_path (void)
{
  gchar *p;

  g_unlink (unix_path);
  p = g_strrstr (unix_path, G_DIR_SEPARATOR_S);
  *p = '\0';
  g_rmdir (unix_path);
  g_free (unix_path);
}

static void
throw_error (const GError *error)
{
  g_debug ("Error: %s", error ? error->message : "No error message");
  success = FALSE;
  g_main_loop_quit (loop);
}

static void
splice_cb (GIOStream *stream1,
    GIOStream *stream2,
    const GError *error,
    gpointer user_data)
{
  if (error != NULL)
    throw_error (error);
  else
    g_main_loop_quit (loop);
}

static void
ssh_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GSocketConnection *tube_connection = user_data;
  GSocketConnection *ssh_connection;
  GError *error = NULL;

  ssh_connection = g_socket_listener_accept_finish (listener, res, NULL, &error);
  if (ssh_connection == NULL)
    {
      throw_error (error);
      g_object_unref (tube_connection);
      return;
    }

  /* Splice tube and ssh connections */
  _g_io_stream_splice (G_IO_STREAM (tube_connection),
      G_IO_STREAM (ssh_connection), splice_cb, NULL);
}

static void
tube_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GSocketConnection *tube_connection;
  GSocket *socket = NULL;
  GInetAddress * inet_address = NULL;
  GSocketAddress *socket_address = NULL;
  guint port;
  GError *error = NULL;

  tube_connection = g_socket_listener_accept_finish (listener, res, NULL, &error);
  if (tube_connection == NULL)
    goto OUT;

  /* Create the IPv4 socket, and listen for connection on it */
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &error);
  if (socket == NULL)
    goto OUT;
  inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  socket_address = g_inet_socket_address_new (inet_address, 0);
  if (!g_socket_bind (socket, socket_address, FALSE, &error))
    goto OUT;
  if (!g_socket_listen (socket, &error))
    goto OUT;
  if (!g_socket_listener_add_socket (listener, socket, NULL, &error))
    goto OUT;

  /* Get the port on which we got bound */
  tp_clear_object (&socket_address);
  socket_address = g_socket_get_local_address (socket, &error);
  if (socket_address == NULL)
    goto OUT;
  port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (socket_address));

  g_socket_listener_accept_async (listener, NULL,
    ssh_socket_connected_cb, g_object_ref (tube_connection));

  /* Start ssh client, it will connect on our IPv4 socket */
  if (fork() == 0)
    {
      gchar *port_str;

      port_str = g_strdup_printf ("%d", port);
      execlp ("ssh", "ssh", "127.0.0.1", "-p", port_str, NULL);
    }

OUT:

  tp_clear_object (&tube_connection);
  tp_clear_object (&socket);
  tp_clear_object (&inet_address);
  tp_clear_object (&socket_address);
  g_clear_error (&error);
}

static void
offer_tube_cb (TpChannel *channel,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  if (error != NULL)
    throw_error (error);
}

static void
channel_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    gpointer user_data)
{
  const GError *error;

  error = tp_proxy_get_invalidated (proxy);
  throw_error (error);
}

static void
handle_channel (TpChannel *channel)
{
  gchar *dir;
  GSocketListener *listener = NULL;
  GSocket *socket = NULL;
  GSocketAddress *socket_address = NULL;
  GValue *address;
  GHashTable *parameters;
  GError *error = NULL;

  g_signal_connect (channel, "invalidated",
      G_CALLBACK (channel_invalidated_cb), NULL);

  /* We are client side, but we have to offer a socket... So we offer an unix
   * socket on which the service side can connect. We also create an IPv4 socket
   * on which the ssh client can connect. When both sockets are connected,
   * we can forward all communications between them. */

  /* FIXME: I don't think we close socket connections, or cancel
   * g_socket_listener_accept_async in all error cases... */

  listener = g_socket_listener_new ();

  /* Create temporary file for our unix socket */
  dir = g_build_filename (g_get_tmp_dir (), "telepathy-ssh-XXXXXX", NULL);
  dir = mkdtemp (dir);
  unix_path = g_build_filename (dir, "unix-socket", NULL);
  g_atexit (remove_unix_path);
  g_free (dir);

  /* Create the unix socket, and listen for connection on it */
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &error);
  if (socket == NULL)
    goto OUT;
  socket_address = g_unix_socket_address_new (unix_path);
  if (!g_socket_bind (socket, socket_address, FALSE, &error))
    goto OUT;
  if (!g_socket_listen (socket, &error))
    goto OUT;
  if (!g_socket_listener_add_socket (listener, socket, NULL, &error))
    goto OUT;

  g_socket_listener_accept_async (listener, NULL,
    tube_socket_connected_cb, NULL);

  /* Offer the socket */
  address = tp_address_variant_from_g_socket_address (socket_address,
      TP_SOCKET_ADDRESS_TYPE_UNIX, &error);
  if (address == NULL)
    goto OUT;
  parameters = g_hash_table_new (NULL, NULL);
  tp_cli_channel_type_stream_tube_call_offer (channel, -1,
      TP_SOCKET_ADDRESS_TYPE_UNIX, address,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, parameters,
      offer_tube_cb, NULL, NULL, NULL);
  tp_g_value_slice_free (address);
  g_hash_table_unref (parameters);

OUT:

  if (error != NULL)
    throw_error (error);

  tp_clear_object (&listener);
  tp_clear_object (&socket);
  tp_clear_object (&socket_address);
  g_clear_error (&error);
}

static void
got_channel_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  GList *l;

  for (l = channels; l != NULL; l = l->next)
    {
      TpChannel *channel = l->data;

      if (tp_strdiff (tp_channel_get_channel_type (channel),
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
        continue;

      handle_channel (channel);
    }

  tp_handle_channels_context_accept (context);
}

static void
channel_request_invalidated (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    gpointer user_data)
{
  const GError *error;

  error = tp_proxy_get_invalidated (proxy);
  if (!g_error_matches (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED))
    throw_error (error);

  g_object_unref (proxy);
}

static void
request_proceed_cb (TpChannelRequest *request,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  if (error != NULL)
    throw_error (error);
}

static void
create_channel_cb (TpChannelDispatcher *dispatcher,
    const gchar *request_path,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpDBusDaemon *dbus;
  TpChannelRequest *request;
  GError *err = NULL;

  if (error != NULL)
    {
      throw_error (error);
      return;
    }

  dbus = tp_proxy_get_dbus_daemon (TP_PROXY (dispatcher));
  request = tp_channel_request_new (dbus, request_path, NULL, &err);
  if (request == NULL)
    {
      throw_error (err);
      g_clear_error (&err);
      return;
    }

  g_signal_connect (request, "invalidated",
      G_CALLBACK (channel_request_invalidated), NULL);

  tp_cli_channel_request_call_proceed (request, -1, request_proceed_cb,
      NULL, NULL, NULL);
}

typedef struct
{
  TpBaseClient *client;
  gchar *account_id;
  gchar *contact_id;

  GList *accounts;
  guint n_readying_connections;

  TpAccount *account;
} RequestData;

static void
request_channel (RequestData *data)
{
  TpDBusDaemon *dbus = NULL;
  TpChannelDispatcher *dispatcher = NULL;
  GHashTable *request = NULL;

  dbus = tp_dbus_daemon_dup (NULL);
  dispatcher = tp_channel_dispatcher_new (dbus);
  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        data->contact_id,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        TUBE_SERVICE,
      NULL);

  tp_cli_channel_dispatcher_call_create_channel (dispatcher, -1,
      tp_proxy_get_object_path (TP_PROXY (data->account)), request, G_MAXINT64,
      tp_base_client_get_bus_name (data->client), create_channel_cb,
      NULL, NULL, NULL);

  g_object_unref (dbus);
  g_object_unref (dispatcher);
  g_hash_table_unref (request);
}

static gboolean
has_stream_tube_cap (TpCapabilities *caps)
{
  GPtrArray *classes;
  guint i;

  if (caps == NULL)
    return FALSE;

  classes = tp_capabilities_get_channel_classes (caps);
  for (i = 0; i < classes->len; i++)
    {
      GValueArray *arr = g_ptr_array_index (classes, i);
      GHashTable *fixed;
      const gchar *chan_type;
      const gchar *service;

      fixed = g_value_get_boxed (g_value_array_get_nth (arr, 0));
      chan_type = tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE);
      service = tp_asv_get_string (fixed, TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);

      if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
          (!tp_capabilities_is_specific_to_contact (caps) ||
           !tp_strdiff (service, TUBE_SERVICE)))
        return TRUE;
    }

  return FALSE;
}

static void
got_contacts_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    guint n_failed,
    const TpHandle *failed,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  RequestData *data = user_data;
  guint i;
  GList *candidates = NULL, *l;
  guint count = 0;
  gchar buffer[10];
  gchar *str;

  if (error != NULL)
    {
      throw_error (error);
      return;
    }

  /* Build a list of all contacts supporting StreamTube */
  for (i = 0; i < n_contacts; i++)
    if (has_stream_tube_cap (tp_contact_get_capabilities (contacts[i])))
      candidates = g_list_prepend (candidates, contacts[i]);

  if (candidates == NULL)
    {
      g_print ("No suitable contact, abort\n");
      g_main_loop_quit (loop);
      return;
    }

  /* Ask the user which candidate to use */
  for (l = candidates; l != NULL; l = l->next)
    {
      TpContact *contact = l->data;

      g_print ("%d) %s (%s)\n", ++count, tp_contact_get_alias (contact),
          tp_contact_get_identifier (contact));
    }

  g_print ("Which contact to use? ");
  str = fgets (buffer, sizeof (buffer), stdin);
  if (str != NULL)
    {
      str[strlen (str) - 1] = '\0';
      l = g_list_nth (candidates, atoi (str) - 1);
    }
  if (l == NULL)
    {
      g_print ("Invalid contact number\n");
      g_main_loop_quit (loop);
      return;
    }

  data->contact_id = g_strdup (tp_contact_get_identifier (l->data));
  request_channel (data);

  g_list_free (candidates);
}

static void
stored_channel_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpChannel *channel = TP_CHANNEL (object);
  TpConnection *connection;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_CAPABILITIES };
  const TpIntSet *set;
  GArray *handles;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (channel, res, &error))
    {
      throw_error (error);
      g_clear_error (&error);
      return;
    }

  connection = tp_channel_borrow_connection (channel);
  set = tp_channel_group_get_members (channel);
  handles = tp_intset_to_array (set);

  tp_connection_get_contacts_by_handle (connection, handles->len,
      (TpHandle *) handles->data, G_N_ELEMENTS (features), features,
      got_contacts_cb, user_data, NULL, NULL);

  g_array_unref (handles);
}

static void
ensure_stored_channel_cb (TpConnection *connection,
    gboolean yours,
    const gchar *channel_path,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpChannel *channel;
  GQuark features[] = { TP_CHANNEL_FEATURE_GROUP, 0 };
  GError *err = NULL;

  if (error != NULL)
    {
      throw_error (error);
      return;
    }

  channel = tp_channel_new_from_properties (connection, channel_path,
      properties, &err);
  if (channel == NULL)
    {
      throw_error (err);
      g_clear_error (&err);
      return;
    }

  tp_proxy_prepare_async (TP_PROXY (channel), features,
      stored_channel_prepare_cb, user_data);

  g_object_unref (channel);
}

static void
chooser_contact (RequestData *data)
{
  TpConnection *connection;
  GHashTable *request;

  /* If a contact ID was passed in the options, use it */
  if (data->contact_id != NULL)
    request_channel (data);

  /* Otherwise, we'll get TpContact objects for all stored contacts on that
   * account. */
  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_LIST,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        "stored",
      NULL);

  connection = tp_account_get_connection (data->account);
  tp_cli_connection_interface_requests_call_ensure_channel (connection, -1,
      request, ensure_stored_channel_cb, data, NULL, NULL);

  g_hash_table_unref (request);
}

static void
chooser_account (RequestData *data)
{
  GList *l;
  guint count = 0;
  gchar buffer[10];
  gchar *str;

  if (data->accounts == NULL)
    {
      g_print ("No suitable account, abort\n");
      g_main_loop_quit (loop);
      return;
    }

  for (l = data->accounts; l != NULL; l = l->next)
    {
      g_print ("%d) %s (%s)\n", ++count,
          tp_account_get_display_name (l->data),
          tp_account_get_protocol (l->data));
    }

  g_print ("Which account to use? ");
  str = fgets (buffer, sizeof (buffer), stdin);
  if (str != NULL)
    {
      str[strlen (str) - 1] = '\0';
      l = g_list_nth (data->accounts, atoi (str) - 1);
    }
  if (l == NULL)
    {
      g_print ("Invalid account number\n");
      g_main_loop_quit (loop);
      return;
    }
  data->account = g_object_ref (l->data);

  /* If the account id was not in the options, print it now. It makes easier to
  * copy/paste later. */
  if (data->account_id == NULL)
    {
      const gchar *account_path;

      account_path = tp_proxy_get_object_path (data->account);
      data->account_id = g_strdup (account_path +
          strlen (TP_ACCOUNT_OBJECT_PATH_BASE));

      g_print ("Going to use account: '%s'\n", data->account_id);
    }

  chooser_contact (data);
}

static void
connection_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *connection = TP_CONNECTION (object);
  RequestData *data = user_data;

  if (!tp_proxy_prepare_finish (TP_PROXY (connection), res, NULL) ||
      !has_stream_tube_cap (tp_connection_get_capabilities (connection)))
    {
      GList *l;

      /* Remove the account that has that connection from the list */
      for (l = data->accounts; l != NULL; l = l->next)
        if (tp_account_get_connection (l->data) == connection)
          {
            g_object_unref (l->data);
            data->accounts = g_list_delete_link (data->accounts, l);
            break;
          }
    }

  if (--data->n_readying_connections == 0)
    chooser_account (data);
}

static void
account_manager_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (object);
  RequestData *data = user_data;
  GList *l, *next;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (manager), res, &error))
    {
      throw_error (error);
      g_clear_error (&error);
      return;
    }

  /* We want to list all accounts which has a connection that have StreamTube
   * support. So first we prepare all connections, and we keep in
   * data->accounts those that are suitable. */

  data->accounts = tp_account_manager_get_valid_accounts (manager);
  g_list_foreach (data->accounts, (GFunc) g_object_ref, NULL);

  for (l = data->accounts; l != NULL; l = next)
    {
      GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
      TpAccount *account = l->data;
      TpConnection *connection;

      next = l->next;

      connection = tp_account_get_connection (account);
      if (connection == NULL)
        {
          g_object_unref (account);
          data->accounts = g_list_delete_link (data->accounts, l);
          continue;
        }

      data->n_readying_connections++;
      tp_proxy_prepare_async (TP_PROXY (connection), features,
          connection_prepare_cb, data);
    }

  if (data->n_readying_connections == 0)
    chooser_account (data);
}

static void
account_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccount *account = TP_ACCOUNT (object);
  RequestData *data = user_data;
  GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
  TpConnection *connection;
  GError *error = NULL;

  /* We are in the case where an account was specified in options, so we have
   * only one candidate, if that accounts has no connection or the connection
   * has no StreamTube support, we'll fail. */

  if (!tp_proxy_prepare_finish (TP_PROXY (account), res, &error))
    {
      throw_error (error);
      return;
    }

  connection = tp_account_get_connection (account);
  if (connection == NULL)
    {
      g_debug ("Account not online");
      g_main_loop_quit (loop);
      return;
    }

  /* Prepare account's connection with caps feature */
  data->accounts = g_list_prepend (NULL, g_object_ref (account));
  data->n_readying_connections = 1;
  tp_proxy_prepare_async (TP_PROXY (connection), features,
      connection_prepare_cb, data);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus = NULL;
  GError *error = NULL;
  RequestData data = { 0, };
  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "account", 'a',
        0, G_OPTION_ARG_STRING, &data.account_id,
        "The account ID",
        NULL },
      { "contact", 'c',
        0, G_OPTION_ARG_STRING, &data.contact_id,
        "The contact ID",
        NULL },
      { NULL }
  };

  g_type_init ();

  optcontext = g_option_context_new ("- ssh-contact");
  g_option_context_add_main_entries (optcontext, options, NULL);
  if (!g_option_context_parse (optcontext, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command "
          "line options.\n", error->message, argv[0]);
      return EXIT_FAILURE;
    }
  g_option_context_free (optcontext);

  g_set_application_name (PACKAGE_NAME);

  /* Register an handler for the StreamTube channel we'll request */
  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    goto OUT;

  data.client = tp_simple_handler_new (dbus, FALSE, FALSE, "SSHContactClient",
      TRUE, got_channel_cb, NULL, NULL);

  tp_base_client_take_handler_filter (data.client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        "ssh",
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN,
        TRUE,
      NULL));

  if (!tp_base_client_register (data.client, &error))
    goto OUT;

  /* If an account id was specified in options, then prepare it, otherwise
   * we get the account manager to get a list of all accounts */
  if (data.account_id != NULL)
    {
      gchar *account_path;

      account_path = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE, data.account_id,
          NULL);
      data.account = tp_account_new (dbus, account_path, &error);
      if (data.account == NULL)
        goto OUT;

      tp_proxy_prepare_async (TP_PROXY (data.account), NULL,
          account_prepare_cb, &data);

      g_free (account_path);
    }
  else
    {
      TpAccountManager *manager;

      manager = tp_account_manager_new (dbus);
      tp_proxy_prepare_async (TP_PROXY (manager), NULL,
          account_manager_prepare_cb, &data);

      g_object_unref (manager);
    }

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

OUT:

  if (error != NULL)
    {
      success = FALSE;
      g_debug ("Error: %s", error->message);
    }

  tp_clear_pointer (&loop, g_main_loop_unref);
  tp_clear_object (&dbus);
  g_clear_error (&error);

  tp_clear_object (&data.client);
  g_free (data.account_id);
  g_free (data.contact_id);
  g_list_foreach (data.accounts, (GFunc) g_object_unref, NULL);
  g_list_free (data.accounts);
  tp_clear_object (&data.account);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

