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

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

static GMainLoop *loop = NULL;
static GList *channel_list = NULL;

static void
channel_invalidated_cb (TpChannel *channel,
    guint domain,
    gint code,
    gchar *message,
    gpointer user_data)
{
  channel_list = g_list_remove (channel_list, channel);
  g_object_unref (channel);

  if (channel_list == NULL)
    g_main_loop_quit (loop);
}

static void
session_complete (TpChannel *channel, const GError *error)
{
  if (error != NULL)
    {
      g_debug ("Error for channel %p: %s", channel,
          error ? error->message : "No error message");
    }

  tp_channel_close_async (channel, NULL, NULL);
}

static void
splice_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer channel)
{
  GError *error = NULL;

  g_io_stream_splice_finish (res, &error);
  session_complete (channel, error);
  g_clear_error (&error);
}

static void
accept_tube_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpChannel *channel = TP_CHANNEL (object);
  TpStreamTubeConnection *stc;
  GInetAddress *inet_address = NULL;
  GSocketAddress *socket_address = NULL;
  GSocket *socket = NULL;
  GSocketConnection *tube_connection = NULL;
  GSocketConnection *sshd_connection = NULL;
  GError *error = NULL;

  stc = tp_stream_tube_channel_accept_finish (TP_STREAM_TUBE_CHANNEL (channel),
      res, &error);
  if (stc == NULL)
    goto OUT;

  tube_connection = tp_stream_tube_connection_get_socket_connection (stc);

  /* Connect to the sshd */
  inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  socket_address = g_inet_socket_address_new (inet_address, 22);
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &error);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_connect (socket, socket_address, NULL, &error))
    goto OUT;
  sshd_connection = g_socket_connection_factory_create_connection (socket);

  /* Splice tube and ssh connections */
  g_io_stream_splice_async (G_IO_STREAM (tube_connection),
      G_IO_STREAM (sshd_connection), G_IO_STREAM_SPLICE_NONE,
      G_PRIORITY_DEFAULT, NULL, splice_cb, channel);

OUT:

  if (error != NULL)
    session_complete (channel, error);

  tp_clear_object (&stc);
  tp_clear_object (&inet_address);
  tp_clear_object (&socket_address);
  tp_clear_object (&socket);
  tp_clear_object (&sshd_connection);
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
      if (TP_IS_STREAM_TUBE_CHANNEL (l->data))
        {
          TpStreamTubeChannel *channel = l->data;

          channel_list = g_list_prepend (channel_list, g_object_ref (channel));
          g_signal_connect (channel, "invalidated",
              G_CALLBACK (channel_invalidated_cb), NULL);

          tp_stream_tube_channel_accept_async (channel, accept_tube_cb, NULL);
        }
    }

  tp_handle_channels_context_accept (context);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus = NULL;
  TpSimpleClientFactory *factory = NULL;
  TpBaseClient *client = NULL;
  gboolean success = TRUE;
  GError *error = NULL;

  g_type_init ();

  tp_debug_set_flags (g_getenv ("SSH_CONTACT_DEBUG"));

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    goto OUT;

  factory = (TpSimpleClientFactory *) tp_automatic_client_factory_new (dbus);
  client = tp_simple_handler_new_with_factory (factory, FALSE, FALSE,
      "SSHContact", FALSE, got_channel_cb, NULL, NULL);

  tp_base_client_take_handler_filter (client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        TUBE_SERVICE,
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN,
        FALSE,
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
  tp_clear_object (&factory);
  tp_clear_object (&client);
  g_clear_error (&error);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

