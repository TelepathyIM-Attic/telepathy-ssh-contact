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

#include "client-helpers.h"
#include "common.h"

typedef struct
{
  GSocketConnection *connection;
  TpChannel *channel;
} CreateTubeData;

static void
create_tube_data_free (CreateTubeData *data)
{
  tp_clear_object (&data->connection);
  tp_clear_object (&data->channel);

  g_slice_free (CreateTubeData, data);
}

static void create_tube_channel_invalidated_cb (TpProxy *proxy, guint domain,
    gint code, gchar *message, GSimpleAsyncResult *simple);
static void create_tube_incoming_cb (TpStreamTubeChannel *channel,
    TpStreamTubeConnection *tube_connection, GSimpleAsyncResult *simple);

static void
create_tube_complete (GSimpleAsyncResult *simple, const GError *error)
{
  CreateTubeData *data;

  g_object_ref (simple);

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (data->channel != NULL)
    {
      g_signal_handlers_disconnect_by_func (data->channel,
          create_tube_channel_invalidated_cb, simple);
      g_signal_handlers_disconnect_by_func (data->channel,
          create_tube_incoming_cb, simple);
    }

  if (error != NULL)
    g_simple_async_result_set_from_error (simple, error);

  g_simple_async_result_complete (simple);

  g_object_unref (simple);
}

static void
create_tube_channel_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    GSimpleAsyncResult *simple)
{
  create_tube_complete (simple,
      tp_proxy_get_invalidated (proxy));
}

static void
create_tube_incoming_cb (TpStreamTubeChannel *channel,
    TpStreamTubeConnection *tube_connection,
    GSimpleAsyncResult *simple)
{
  CreateTubeData *data;

  data = g_simple_async_result_get_op_res_gpointer (simple);
  data->connection = tp_stream_tube_connection_get_socket_connection (
      tube_connection);
  g_object_ref (data->connection);

  create_tube_complete (simple, NULL);
}

static void
create_tube_offer_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = user_data;
  GError *error = NULL;
  CreateTubeData *data;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (!tp_stream_tube_channel_offer_finish (TP_STREAM_TUBE_CHANNEL (object),
      res, &error))
    create_tube_complete (simple, error);

  g_clear_error (&error);
  g_object_unref (simple);
}

static void
create_channel_cb (GObject *acr,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = user_data;
  CreateTubeData *data;
  GError *error = NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  data->channel = tp_account_channel_request_create_and_handle_channel_finish (
      TP_ACCOUNT_CHANNEL_REQUEST (acr), res, NULL, &error);
  if (!TP_IS_STREAM_TUBE_CHANNEL (data->channel))
    {
      tp_clear_object (&data->channel);

      if (error == NULL)
        error = g_error_new_literal (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
            "Not supported channel type");

      create_tube_complete (simple, error);

      g_clear_error (&error);
      g_object_unref (simple);
      return;
    }

  g_signal_connect (data->channel, "invalidated",
      G_CALLBACK (create_tube_channel_invalidated_cb), simple);

  g_signal_connect_data (data->channel, "incoming",
      G_CALLBACK (create_tube_incoming_cb),
      g_object_ref (simple), (GClosureNotify) g_object_unref, 0);

  tp_stream_tube_channel_offer_async (TP_STREAM_TUBE_CHANNEL (data->channel),
      NULL, create_tube_offer_cb, g_object_ref (simple));

  g_object_unref (simple);
}

void
_client_create_tube_async (const gchar *account_path,
    const gchar *contact_id,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  CreateTubeData *data;
  GHashTable *request;
  TpDBusDaemon *dbus;
  TpAccount *account = NULL;
  TpAccountChannelRequest *acr;
  GError *error = NULL;

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus != NULL)
    account = tp_account_new (dbus, account_path, &error);
  if (account == NULL)
    {
      g_simple_async_report_gerror_in_idle (NULL, callback, user_data, error);
      g_clear_error (&error);
      tp_clear_object (&dbus);
      return;
    }

  simple = g_simple_async_result_new (NULL, callback, user_data,
      _client_create_tube_finish);

  data = g_slice_new0 (CreateTubeData);
  g_simple_async_result_set_op_res_gpointer (simple, data,
      (GDestroyNotify) create_tube_data_free);

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

  acr = tp_account_channel_request_new (account, request, G_MAXINT64);
  tp_account_channel_request_create_and_handle_channel_async (acr,
      NULL, create_channel_cb, simple);

  g_hash_table_unref (request);
  g_object_unref (dbus);
  g_object_unref (account);
  g_object_unref (acr);
}

GSocketConnection  *
_client_create_tube_finish (GAsyncResult *result,
    TpChannel **channel,
    GError **error)
{
  GSimpleAsyncResult *simple;
  CreateTubeData *data;

  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
      _client_create_tube_finish), NULL);

  data = g_simple_async_result_get_op_res_gpointer (
      G_SIMPLE_ASYNC_RESULT (result));

  if (channel != NULL)
    *channel = g_object_ref (data->channel);

  return g_object_ref (data->connection);
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

  if (username != NULL && *username != '\0')
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
