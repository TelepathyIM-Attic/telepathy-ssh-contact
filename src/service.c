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

#include <gio/gunixsocketaddress.h>

#include <telepathy-glib/channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/simple-handler.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include "common.h"

static GMainLoop *loop = NULL;
static guint n_tubes = 0;

static void
splice_cb (gpointer user_data)
{
  if (--n_tubes == 0)
    g_main_loop_quit (loop);
}

static void
accept_tube_cb (TpChannel *channel,
    const GValue *address,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GArray *array;
  gchar *path;
  GSocketAddress *socket_address;
  GInetAddress *inet_address;
  GSocket *socket;
  GSocketConnection *tube_connection;
  GSocketConnection *sshd_connection;

  g_assert_no_error (error);

  /* Connect to the unix socket we received */
  array = g_value_get_boxed (address);
  path = g_strndup (array->data, array->len);
  socket_address = g_unix_socket_address_new (path);
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, NULL);
  g_socket_connect (socket, socket_address, NULL, NULL);
  tube_connection = g_socket_connection_factory_create_connection (socket);
  g_object_unref (socket_address);
  g_object_unref (socket);

  /* Connect to the sshd */
  inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  socket_address = g_inet_socket_address_new (inet_address, 22);
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, NULL);
  g_socket_connect (socket, socket_address, NULL, NULL);
  sshd_connection = g_socket_connection_factory_create_connection (socket);
  g_object_unref (inet_address);
  g_object_unref (socket_address);
  g_object_unref (socket);

  _g_io_stream_splice (G_IO_STREAM (tube_connection),
      G_IO_STREAM (sshd_connection), splice_cb, NULL);

  g_object_unref (tube_connection);
  g_object_unref (sshd_connection);
}

static void
channel_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpChannel *channel = TP_CHANNEL (object);
  GHashTable *parameters;
  GValue value = { 0, };

  g_assert (tp_proxy_prepare_finish (TP_PROXY (channel), res, NULL));

  parameters = g_hash_table_new (NULL, NULL);
  g_value_init (&value, G_TYPE_STRING);

  tp_cli_channel_type_stream_tube_call_accept (channel, -1,
      TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, &value,
      accept_tube_cb, NULL, NULL, NULL);

  g_hash_table_unref (parameters);
  g_value_reset (&value);
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

      n_tubes++;

      tp_proxy_prepare_async (TP_PROXY (channel), features,
          channel_prepare_cb, NULL);
    }

  tp_handle_channels_context_accept (context);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus;
  TpBaseClient *client;

  g_type_init ();

  dbus = tp_dbus_daemon_dup (NULL);
  client = tp_simple_handler_new (dbus, FALSE, FALSE, "TelepathySSHService",
      FALSE, got_channel_cb, NULL, NULL);

  tp_base_client_take_handler_filter (client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "ssh",
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN, FALSE,
      NULL));

  tp_base_client_register (client, NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_object_unref (dbus);
  g_object_unref (client);

  return 0;
}

