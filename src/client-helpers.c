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
#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>

#include "client-helpers.h"
#include "common.h"

typedef struct
{
  GSimpleAsyncResult *result;
  TpBaseClient *client;
  gchar *unix_path;
} CreateTubeData;

static void
create_tube_complete (CreateTubeData *data,
    const GError *error)
{
  if (error != NULL)
    g_simple_async_result_set_from_error (data->result, error);

  g_simple_async_result_complete_in_idle (data->result);

  tp_clear_object (&data->result);
  tp_clear_object (&data->client);

  g_slice_free (CreateTubeData, data);
}

static void
unix_path_destroy (gchar *unix_path)
{
  if (unix_path != NULL)
    {
      gchar *p;

      g_unlink (unix_path);
      p = g_strrstr (unix_path, G_DIR_SEPARATOR_S);
      *p = '\0';
      g_rmdir (unix_path);
      g_free (unix_path);
    }
}

static void
create_tube_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  CreateTubeData *data = user_data;
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GSocketConnection *connection;
  GError *error = NULL;

  connection = g_socket_listener_accept_finish (listener, res, NULL, &error);

  if (connection != NULL)
    {
      /* Transfer ownership of connection */
      g_simple_async_result_set_op_res_gpointer (data->result, connection,
          g_object_unref);

      /* Transfer ownership of unix path */
      g_object_set_data_full (G_OBJECT (connection), "unix-path",
          data->unix_path, (GDestroyNotify) unix_path_destroy);
      data->unix_path = NULL;
    }

  create_tube_complete (data, error);

  g_clear_error (&error);
}

static void
create_tube_offer_cb (TpChannel *channel,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  CreateTubeData *data = user_data;

  if (error != NULL)
    create_tube_complete (data, error);
}

static void
create_tube_channel_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    CreateTubeData *data)
{
  create_tube_complete (data, tp_proxy_get_invalidated (proxy));
}

static void
create_tube_handle_channel (CreateTubeData *data,
    TpChannel *channel)
{
  GSocketListener *listener = NULL;
  gchar *dir;
  GSocket *socket = NULL;
  GSocketAddress *socket_address = NULL;
  GValue *address;
  GHashTable *parameters;
  GError *error = NULL;

  g_signal_connect (channel, "invalidated",
      G_CALLBACK (create_tube_channel_invalidated_cb), data);

  /* We are client side, but we have to offer a socket... So we offer an unix
   * socket on which the service side can connect. We also create an IPv4 socket
   * on which the ssh client can connect. When both sockets are connected,
   * we can forward all communications between them. */

  listener = g_socket_listener_new ();

  /* Create temporary file for our unix socket */
  dir = g_build_filename (g_get_tmp_dir (), "telepathy-ssh-XXXXXX", NULL);
  dir = mkdtemp (dir);
  data->unix_path = g_build_filename (dir, "unix-socket", NULL);
  g_free (dir);

  /* Create the unix socket, and listen for connection on it */
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &error);
  if (socket == NULL)
    goto OUT;
  socket_address = g_unix_socket_address_new (data->unix_path);
  if (!g_socket_bind (socket, socket_address, FALSE, &error))
    goto OUT; 
  if (!g_socket_listen (socket, &error))
    goto OUT;
  if (!g_socket_listener_add_socket (listener, socket, NULL, &error))
    goto OUT;

  g_socket_listener_accept_async (listener, NULL,
    create_tube_socket_connected_cb, data);

  /* Offer the socket */
  address = tp_address_variant_from_g_socket_address (socket_address,
      TP_SOCKET_ADDRESS_TYPE_UNIX, &error);
  if (address == NULL)
    goto OUT;
  parameters = g_hash_table_new (NULL, NULL);
  tp_cli_channel_type_stream_tube_call_offer (channel, -1,
      TP_SOCKET_ADDRESS_TYPE_UNIX, address,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, parameters,
      create_tube_offer_cb, data, NULL, NULL);
  tp_g_value_slice_free (address);
  g_hash_table_unref (parameters);

OUT:

  if (error != NULL)
    create_tube_complete (data, error);

  tp_clear_object (&listener);
  tp_clear_object (&socket);
  tp_clear_object (&socket_address);
  g_clear_error (&error);
}

static void
create_tube_got_channel_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  CreateTubeData *data = user_data;
  GList *l;

  for (l = channels; l != NULL; l = l->next)
    {
      TpChannel *channel = l->data;

      if (!tp_strdiff (tp_channel_get_channel_type (channel),
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
        {
          create_tube_handle_channel (data, channel);
          break;
        }
    }

  tp_handle_channels_context_accept (context);
}

static void
create_tube_channel_request_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    CreateTubeData *data)
{
  const GError *error;

  error = tp_proxy_get_invalidated (proxy);
  if (!g_error_matches (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED))
    create_tube_complete (data, error);

  g_object_unref (proxy);
}

static void
create_tube_request_proceed_cb (TpChannelRequest *request,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  CreateTubeData *data = user_data;

  if (error != NULL)
    {
      create_tube_complete (data, error);
      return;
    }
}

static void
create_tube_channel_cb (TpChannelDispatcher *dispatcher,
    const gchar *request_path,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  CreateTubeData *data = user_data;
  TpDBusDaemon *dbus;
  TpChannelRequest *request;
  GError *err = NULL;

  if (error != NULL)
    {
      create_tube_complete (data, error);
      return;
    }

  dbus = tp_proxy_get_dbus_daemon (TP_PROXY (dispatcher));
  request = tp_channel_request_new (dbus, request_path, NULL, &err);
  if (request == NULL)
    {
      create_tube_complete (data, err);
      g_clear_error (&err);
      return;
    }

  g_signal_connect (request, "invalidated",
      G_CALLBACK (create_tube_channel_request_invalidated_cb), data);

  tp_cli_channel_request_call_proceed (request, -1,
      create_tube_request_proceed_cb, data, NULL, NULL);
}

void
_client_create_tube_async (const gchar *account_path,
    const gchar *contact_id,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  CreateTubeData *data;
  TpDBusDaemon *dbus = NULL;
  TpChannelDispatcher *dispatcher = NULL;
  GHashTable *request = NULL;
  GError *error = NULL;

  dbus = tp_dbus_daemon_dup (NULL);

  data = g_slice_new0 (CreateTubeData);
  data->result = g_simple_async_result_new (NULL, callback, user_data,
      _client_create_tube_finish);
  data->client = tp_simple_handler_new (dbus, FALSE, FALSE,
      "SSHContactClient", TRUE, create_tube_got_channel_cb, data, NULL);

  tp_base_client_take_handler_filter (data->client, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        TUBE_SERVICE,
      TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN,
        TRUE,
      NULL));

  if (!tp_base_client_register (data->client, &error))
    {
      create_tube_complete (data, error);
      g_clear_error (&error);
      g_object_unref (dbus);
      return;
    }

  dispatcher = tp_channel_dispatcher_new (dbus);
  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        contact_id,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        TUBE_SERVICE,
      NULL);

  tp_cli_channel_dispatcher_call_create_channel (dispatcher, -1,
      account_path, request, G_MAXINT64,
      tp_base_client_get_bus_name (data->client),
      create_tube_channel_cb, data, NULL, NULL);

  g_object_unref (dispatcher);
  g_hash_table_unref (request);
}

GSocketConnection  *
_client_create_tube_finish (GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
      _client_create_tube_finish), NULL);

  return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
}

GSocket *
_client_create_local_socket (GError **error)
{
  GSocket *socket = NULL;
  GInetAddress * inet_address = NULL;
  GSocketAddress *socket_address = NULL;

  /* Create the IPv4 socket, and listen for connection on it */
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, error);
  if (socket != NULL)
    {
      inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
      socket_address = g_inet_socket_address_new (inet_address, 0);
      g_socket_bind (socket, socket_address, FALSE, error);
    }

  tp_clear_object (&inet_address);
  tp_clear_object (&socket_address);

  return socket;
}

GStrv
_client_create_exec_args (GSocket *socket,
    const gchar *contact_id,
    const gchar *username)
{
  GPtrArray *args;
  GSocketAddress *socket_address;
  GInetAddress *inet_address;
  guint16 port;
  gchar *host;
  gchar *str;

  /* Get the local host and port on which sshd is running */
  socket_address = g_socket_get_local_address (socket, NULL);
  inet_address = g_inet_socket_address_get_address (
      G_INET_SOCKET_ADDRESS (socket_address));
  port = g_inet_socket_address_get_port (
      G_INET_SOCKET_ADDRESS (socket_address));
  host = g_inet_address_to_string (inet_address);

  /* Create ssh client args */
  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("ssh"));
  g_ptr_array_add (args, host);

  g_ptr_array_add (args, g_strdup ("-p"));
  str = g_strdup_printf ("%d", port);
  g_ptr_array_add (args, str);

  if (contact_id != NULL)
    {
      str = g_strdup_printf ("-oHostKeyAlias=%s", contact_id);
      g_ptr_array_add (args, str);
    }

  if (username != NULL)
    {
      g_ptr_array_add (args, g_strdup ("-l"));
      g_ptr_array_add (args, g_strdup (username));
    }

  g_ptr_array_add (args, NULL);

  return (gchar **) g_ptr_array_free (args, FALSE);
}

gboolean
_capabilities_has_stream_tube (TpCapabilities *caps)
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
      TpHandleType handle_type;

      fixed = g_value_get_boxed (g_value_array_get_nth (arr, 0));
      chan_type = tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE);
      service = tp_asv_get_string (fixed,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);
      handle_type = tp_asv_get_uint32 (fixed,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

      if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
          handle_type == TP_HANDLE_TYPE_CONTACT &&
          (!tp_capabilities_is_specific_to_contact (caps) ||
           !tp_strdiff (service, TUBE_SERVICE)))
        return TRUE;
    }

  return FALSE;
}
