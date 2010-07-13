/*
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
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
  /* FIXME: If the offer failed, do we have to cancel the pending
   * g_socket_listener_accept_async() ? */
  if (error != NULL)
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

  /* We are client side, but we have to offer a socket... So we offer an unix
   * socket on which the service side can connect. We also create an IPv4 socket
   * on which the ssh client can connect. When both sockets are connected,
   * we can forward all communications between them. */

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
  TpChannelRequest *request;
  GError *err = NULL;

  if (error != NULL)
    {
      throw_error (error);
      return;
    }

  request = tp_channel_request_new (tp_proxy_get_dbus_daemon (TP_PROXY (dispatcher)),
      request_path, NULL, &err);
  if (request == NULL)
    {
      throw_error (err);
      g_clear_error (&err);
      return;
    }

  tp_cli_channel_request_call_proceed (request, -1, request_proceed_cb,
      NULL, NULL, NULL);
  g_object_unref (request);
}

static void
request_channel (TpBaseClient *client,
    const gchar *account_path,
    const gchar *contact_id)
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
        contact_id,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "ssh",
      NULL);

  tp_cli_channel_dispatcher_call_create_channel (dispatcher, -1,
      account_path, request, G_MAXINT64,
      tp_base_client_get_bus_name (client), create_channel_cb,
      NULL, NULL, NULL);

  g_object_unref (dbus);
  g_object_unref (dispatcher);
  g_hash_table_unref (request);
}

typedef struct
{
  TpBaseClient *client;
  gchar *contact_id;
} PrepareAccountManagerData;

static void
account_manager_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (object);
  PrepareAccountManagerData *data = user_data;
  GList *accounts, *l, *next;
  guint count = 0;
  gchar buffer[50];
  gchar *str;

  accounts = tp_account_manager_get_valid_accounts (manager);
  for (l = accounts; l != NULL; l = next)
    {
      TpAccount *account = l->data;
      TpConnectionStatus status;

      next = l->next;

      status = tp_account_get_connection_status (account, NULL);
      if (status != TP_CONNECTION_STATUS_CONNECTED)
        {
          accounts = g_list_delete_link (accounts, l);
          continue;
        }

      /* FIXME: Add a check if that account supports Tubes */

      g_print ("%d) %s (%s)\n", ++count,
          tp_account_get_display_name (account),
          tp_account_get_protocol (account));
    }

  do
    {
      g_print ("Which account to use? ");
      str = fgets (buffer, sizeof (buffer), stdin);
      if (str == NULL)
        {
          g_print ("Error reading stdin\n");
          continue;
        }

      str[strlen (str) - 1] = '\0';
      l = g_list_nth (accounts, atoi (str) - 1);
      if (l == NULL)
        g_print ("Invalid account number\n");
    }
  while (l == NULL);

  request_channel (data->client, tp_proxy_get_object_path (l->data),
      data->contact_id);

  g_list_free (accounts);
  g_object_unref (data->client);
  g_free (data->contact_id);
  g_slice_free (PrepareAccountManagerData, data);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus = NULL;
  TpBaseClient *client = NULL;
  gchar *account_path = NULL;
  GError *error = NULL;

  g_type_init ();

  if (argc < 2 || argc > 3)
    {
      g_print ("Usage: %s [<account id>] <contact id>\n", argv[0]);
      success = FALSE;
      goto OUT;
    }

  /* Register an handler for the StreamTube channel we'll request */
  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    goto OUT;

  client = tp_simple_handler_new (dbus, FALSE, FALSE, "TelepathySSHClient",
      TRUE, got_channel_cb, NULL, NULL);

  tp_base_client_take_handler_filter (client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "ssh",
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN, TRUE,
      NULL));

  if (!tp_base_client_register (client, &error))
    goto OUT;

  if (argc == 3)
    {
      gchar *account_path;

      account_path = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE, argv[1], NULL);
      request_channel (client, account_path, argv[2]);
      g_free (account_path);
    }
  else
    {
      PrepareAccountManagerData *data;
      TpAccountManager *manager;

      data = g_slice_new0 (PrepareAccountManagerData);
      data->client = g_object_ref (client);
      data->contact_id = g_strdup (argv[1]);

      manager = tp_account_manager_new (dbus);
      tp_proxy_prepare_async (TP_PROXY (manager), NULL,
          account_manager_prepare_cb, data);
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
  tp_clear_object (&client);
  g_free (account_path);
  g_clear_error (&error);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

