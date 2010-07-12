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

#include <gio/gunixsocketaddress.h>
#include <telepathy-glib/telepathy-glib.h>

#include "common.h"

static GMainLoop *loop = NULL;
static guint n_sessions = 0;

static void
throw_error (const GError *error)
{
  g_debug ("Error: %s", error ? error->message : "No error message");

  if (--n_sessions == 0)
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
  else if (--n_sessions == 0)
    g_main_loop_quit (loop);
}

static void
accept_tube_cb (TpChannel *channel,
    const GValue *address,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSocketAddress *socket_address = NULL;
  GInetAddress *inet_address = NULL;
  GSocket *socket = NULL;
  GSocketConnection *tube_connection = NULL;
  GSocketConnection *sshd_connection = NULL;
  GError *err = NULL;

  if (error != NULL)
    {
      throw_error (error);
      return;
    }

  /* Connect to the unix socket we received */
  socket_address = tp_g_socket_address_from_variant (
      TP_SOCKET_ADDRESS_TYPE_UNIX, address, &err);
  if (socket_address == NULL)
    goto OUT;
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_connect (socket, socket_address, NULL, &err))
    goto OUT;
  tube_connection = g_socket_connection_factory_create_connection (socket);
  tp_clear_object (&socket_address);
  tp_clear_object (&socket);

  /* Connect to the sshd */
  inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  socket_address = g_inet_socket_address_new (inet_address, 22);
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_connect (socket, socket_address, NULL, &err))
    goto OUT;
  sshd_connection = g_socket_connection_factory_create_connection (socket);

  /* Splice tube and ssh connections */
  _g_io_stream_splice (G_IO_STREAM (tube_connection),
      G_IO_STREAM (sshd_connection), splice_cb, NULL);

OUT:

  if (err != NULL)
    throw_error (err);

  tp_clear_object (&inet_address);
  tp_clear_object (&socket_address);
  tp_clear_object (&socket);
  tp_clear_object (&tube_connection);
  tp_clear_object (&sshd_connection);
  g_clear_error (&err);
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
  GValue value = { 0, };
  GList *l;

  /* FIXME: Dummy value because passing NULL makes tp-glib crash */
  g_value_init (&value, G_TYPE_STRING);

  for (l = channels; l != NULL; l = l->next)
    {
      TpChannel *channel = l->data;

      if (tp_strdiff (tp_channel_get_channel_type (channel),
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
        continue;

      n_sessions++;

      tp_cli_channel_type_stream_tube_call_accept (channel, -1,
          TP_SOCKET_ADDRESS_TYPE_UNIX,
          TP_SOCKET_ACCESS_CONTROL_LOCALHOST, &value,
          accept_tube_cb, NULL, NULL, NULL);

    }
  tp_handle_channels_context_accept (context);

  g_value_reset (&value);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus = NULL;
  TpBaseClient *client = NULL;
  gboolean success = TRUE;
  GError *error = NULL;

  g_type_init ();

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    goto OUT;

  client = tp_simple_handler_new (dbus, FALSE, FALSE, "TelepathySSH",
      FALSE, got_channel_cb, NULL, NULL);

  tp_base_client_take_handler_filter (client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "ssh",
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN, FALSE,
      NULL));

  if (!tp_base_client_register (client, &error))
    goto OUT;

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

OUT:

  if (error != NULL)
    {
      g_debug ("Error: %s", error->message);
      success = FALSE;
    }

  tp_clear_pointer (&loop, g_main_loop_unref);
  tp_clear_object (&dbus);
  tp_clear_object (&client);
  g_clear_error (&error);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

