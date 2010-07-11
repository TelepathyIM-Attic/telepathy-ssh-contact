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

#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/channel-dispatcher.h>
#include <telepathy-glib/channel-request.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/simple-handler.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include "common.h"

static GMainLoop *loop = NULL;

static void
splice_cb (gpointer user_data)
{
  gchar *path = user_data;
  gchar *p;

  g_unlink (path);
  p = g_strrstr (path, G_DIR_SEPARATOR_S);
  *p = '\0';
  g_rmdir (path);
  g_free (path);

  g_main_loop_quit (loop);
}

static void
channel_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpChannel *channel = TP_CHANNEL (object);
  gchar *dir;
  gchar *path;
  GSocketAddress *socket_address;
  GInetAddress * inet_address;
  GSocket *socket;
  GArray *array;
  GValue address = { 0, };
  GHashTable *parameters;
  guint port;
  GSocketListener *listener;
  GSocketConnection *tube_connection;
  GSocketConnection *ssh_connection;

  g_assert (tp_proxy_prepare_finish (TP_PROXY (channel), res, NULL));

  /* We have to offer a socket, but we are client side... so we create a unix
   * socket and we'll bridge it to a local IPv4 socket where ssh client can
   * connect */
  dir = g_build_filename (g_get_tmp_dir (), "telepathy-ssh-XXXXXX", NULL);
  dir = mkdtemp (dir);
  path = g_build_filename (dir, "unix-socket", NULL);
  socket_address = g_unix_socket_address_new (path);
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, NULL);
  g_socket_bind (socket, socket_address, FALSE, NULL);
  g_object_unref (socket_address);
  g_free (dir);

  /* Offer the socket */
  array = g_array_sized_new (FALSE, FALSE, sizeof (guchar), strlen (path));
  g_array_append_vals (array, path, strlen (path));
  g_value_init (&address, DBUS_TYPE_G_UCHAR_ARRAY);
  g_value_take_boxed (&address, array);
  parameters = g_hash_table_new (NULL, NULL);

  tp_cli_channel_type_stream_tube_call_offer (channel, -1,
      TP_SOCKET_ADDRESS_TYPE_UNIX, &address,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, parameters,
      NULL, NULL, NULL, NULL);

  g_value_reset (&address);
  g_hash_table_unref (parameters);

  /* Wait for the service side to connect on our socket */
  listener = g_socket_listener_new ();
  g_socket_listen (socket, NULL);
  g_socket_listener_add_socket (listener, socket, NULL, NULL);
  g_object_unref (socket);
  tube_connection = g_socket_listener_accept (listener, NULL, NULL, NULL);

  /* Create an IPv4 socket */
  inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  socket_address = g_inet_socket_address_new (inet_address, 0);
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, NULL);
  g_socket_bind (socket, socket_address, FALSE, NULL);
  g_object_unref (inet_address);
  g_object_unref (socket_address);

  /* Get the port on which we got bound */
  socket_address = g_socket_get_local_address (socket, NULL);
  port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (socket_address));  
  g_object_unref (socket_address);

  /* Start ssh client, it will connect on our IPv4 socket */
  if (fork() == 0)
    {
      gchar *port_str;

      port_str = g_strdup_printf ("%d", port);
      execlp ("ssh", "ssh", "127.0.0.1", "-p", port_str, NULL);
    }

  /* Wait for the ssh client to connect on our socket */
  g_socket_listen (socket, NULL);
  g_socket_listener_add_socket (listener, socket, NULL, NULL);
  g_object_unref (socket);
  ssh_connection = g_socket_listener_accept (listener, NULL, NULL, NULL);
  g_object_unref (listener);

  _g_io_stream_splice (G_IO_STREAM (tube_connection),
      G_IO_STREAM (ssh_connection), splice_cb, path);

  g_object_unref (tube_connection);
  g_object_unref (ssh_connection);
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
  GQuark features[] = { TP_CHANNEL_FEATURE_CORE };
  GList *l;

  for (l = channels; l != NULL; l = l->next)
    {
      TpChannel *channel = l->data;

      if (tp_strdiff (tp_channel_get_channel_type (channel),
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
        continue;

      tp_proxy_prepare_async (TP_PROXY (channel), features,
          channel_prepare_cb, NULL);
    }

  tp_handle_channels_context_accept (context);
}

static void
create_channel_cb (TpChannelDispatcher *dispatcher,
    const gchar *request_path,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpChannelRequest *request;

  if (error != NULL)
    {
      g_debug ("Error: %s", error->message);
      g_assert_not_reached ();
    }

  request = tp_channel_request_new (tp_proxy_get_dbus_daemon (TP_PROXY (dispatcher)),
      request_path, NULL, NULL);

  tp_cli_channel_request_call_proceed (request, -1, NULL, NULL, NULL, NULL);
  g_object_unref (request);
}

typedef struct
{
  gchar *account_path;
  gchar *contact_id;
  TpBaseClient *client;
} Context;

static void
account_manager_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (object);
  Context *ctx = user_data;
  TpDBusDaemon *dbus;
  TpChannelDispatcher *dispatcher;
  GList *accounts, *l;
  GHashTable *request;

  g_assert (tp_account_manager_prepare_finish (account_manager, res, NULL));

  dbus = tp_dbus_daemon_dup (NULL);
  dispatcher = tp_channel_dispatcher_new (dbus);
  accounts = tp_account_manager_get_valid_accounts (account_manager);
  
  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        ctx->contact_id,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "ssh",
      NULL);

  for (l = accounts; l != NULL; l = l->next)
    {
      TpAccount *account = l->data;
      TpConnectionStatus status;
      const gchar *account_path;

      status = tp_account_get_connection_status (account, NULL);
      if (status != TP_CONNECTION_STATUS_CONNECTED)
        continue;

      account_path = tp_proxy_get_object_path (TP_PROXY (account));
      if (tp_strdiff (account_path, ctx->account_path))
        continue;

      tp_cli_channel_dispatcher_call_create_channel (dispatcher, -1,
          account_path, request, G_MAXINT64,
          tp_base_client_get_bus_name (ctx->client), create_channel_cb,
          NULL, NULL, NULL);

      break;
    }

  g_free (ctx->account_path);
  g_free (ctx->contact_id);
  g_object_unref (ctx->client);
  g_slice_free (Context, ctx);

  g_object_unref (dbus);
  g_object_unref (dispatcher);
  g_list_free (accounts);
  g_hash_table_unref (request);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus;
  TpBaseClient *client;
  TpAccountManager *account_manager;
  GQuark features[] = { TP_ACCOUNT_MANAGER_FEATURE_CORE };
  Context *ctx;

  g_type_init ();

  if (argc != 3)
    {
      g_print ("Usage: %s <account id> <contact id>\n", argv[0]);
      return EXIT_FAILURE;
    }

  /* Register an handler for the StreamTube channel we'll request */
  dbus = tp_dbus_daemon_dup (NULL);
  client = tp_simple_handler_new (dbus, FALSE, FALSE, "TelepathySSHClient",
      TRUE, got_channel_cb, NULL, NULL);

  tp_base_client_take_handler_filter (client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "ssh",
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN, TRUE,
      NULL));

  tp_base_client_register (client, NULL);

  ctx = g_slice_new0 (Context);
  ctx->account_path = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE, argv[1], NULL);
  ctx->contact_id = g_strdup (argv[2]);
  ctx->client = g_object_ref (client);

  /* Get the account manager to request a tube with a contact */
  account_manager = tp_account_manager_dup ();
  tp_account_manager_prepare_async (account_manager, features,
      account_manager_prepare_cb, ctx);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_object_unref (dbus);
  g_object_unref (client);
  g_object_unref (account_manager);

  return EXIT_SUCCESS;
}

