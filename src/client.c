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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

#include "client-helpers.h"

typedef struct
{
  GMainLoop *loop;
  gchar *argv0;

  gchar *account_path;
  gchar *contact_id;
  gchar *login;
  gchar **ssh_opts;

  TpChannel *channel;
  GSocketConnection *tube_connection;
  GSocketConnection *ssh_connection;

  gboolean success:1;
} ClientContext;

static void
channel_invalidated_cb (TpChannel *channel,
    guint domain,
    gint code,
    gchar *message,
    ClientContext *context)
{
  g_main_loop_quit (context->loop);
}

static void
leave (ClientContext *context)
{
  if (context->channel != NULL &&
      tp_proxy_get_invalidated (context->channel) == NULL)
    tp_channel_close_async (context->channel, NULL, NULL);
  else
    g_main_loop_quit (context->loop);
}

static void
throw_error_message (ClientContext *context,
    const gchar *message)
{
  g_print ("Error: %s\n", message);
  context->success = FALSE;
  leave (context);
}

static void
throw_error (ClientContext *context,
    const GError *error)
{
  throw_error_message (context, error ? error->message : "No error message");
}

static void
ssh_client_watch_cb (GPid pid,
    gint status,
    gpointer user_data)
{
  ClientContext *context = user_data;

  leave (context);
  g_spawn_close_pid (pid);
}

static void
splice_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  ClientContext *context = user_data;
  GError *error = NULL;

  if (!g_io_stream_splice_finish (res, &error))
    throw_error (context, error);
  else
    leave (context);

  g_clear_error (&error);
}

static void
ssh_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  ClientContext *context = user_data;
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GError *error = NULL;

  context->ssh_connection = g_socket_listener_accept_finish (listener, res,
      NULL, &error);
  if (error != NULL)
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  /* Splice tube and ssh connections */
  g_io_stream_splice_async (G_IO_STREAM (context->tube_connection),
      G_IO_STREAM (context->ssh_connection), G_IO_STREAM_SPLICE_NONE,
      G_PRIORITY_DEFAULT, NULL, splice_cb, context);
}

static void
create_tube_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  ClientContext *context = user_data;
  GSocketListener *listener;
  GSocket *socket;
  GStrv args = NULL;
  GPid pid;
  GError *error = NULL;

  context->tube_connection = _client_create_tube_finish (res, &context->channel,
      &error);
  if (error != NULL)
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  g_signal_connect (context->channel, "invalidated",
      G_CALLBACK (channel_invalidated_cb), context);

  listener = g_socket_listener_new ();
  socket = _client_create_local_socket (&error);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_listen (socket, &error))
    goto OUT;
  if (!g_socket_listener_add_socket (listener, socket, NULL, &error))
    goto OUT;

  g_socket_listener_accept_async (listener, NULL,
      ssh_socket_connected_cb, context);

  args = _client_create_exec_args (socket, context->contact_id,
      context->login, context->ssh_opts);

  /* spawn ssh client */
  if (g_spawn_async (NULL, args, NULL,
      G_SPAWN_SEARCH_PATH | G_SPAWN_CHILD_INHERITS_STDIN |
      G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error))
    {
      g_child_watch_add (pid, ssh_client_watch_cb, context);
    }

OUT:

  if (error != NULL)
    throw_error (context, error);

  g_clear_error (&error);
  tp_clear_object (&listener);
  tp_clear_object (&socket);
  g_strfreev (args);
}

static void
start_tube (ClientContext *context,
    TpContact *contact)
{
  TpAccount *account;

  account = tp_connection_get_account (tp_contact_get_connection (contact));

  if (context->account_path == NULL || context->contact_id == NULL)
    {
      g_print ("\nTo avoid interactive mode, you can use that command:\n"
          "%s --account %s --contact %s\n", context->argv0,
          tp_proxy_get_object_path (account),
          tp_contact_get_identifier (contact));
    }

  _client_create_tube_async (account, tp_contact_get_identifier (contact),
      create_tube_cb, context);
}

static void
choose_contact (ClientContext *context,
    GList *accounts)
{
  GPtrArray *candidates;
  GList *l;
  GString *text;
  gchar buffer[10];
  gchar *str;

  text = g_string_new (NULL);
  candidates = g_ptr_array_new_with_free_func (g_object_unref);
  for (l = accounts; l != NULL; l = l->next)
    {
      TpAccount *account = l->data;
      TpConnection *connection;
      TpCapabilities *caps;
      GPtrArray *contacts;
      GString *subtext;
      gboolean found = FALSE;
      guint i;

      connection = tp_account_get_connection (account);
      if (connection == NULL)
        continue;

      caps = tp_connection_get_capabilities (connection);
      if (!_capabilities_has_stream_tube (caps))
        continue;

      subtext = g_string_new (NULL);
      contacts = tp_connection_dup_contact_list (connection);
      for (i = 0; i < contacts->len; i++)
        {
          TpContact *contact = g_ptr_array_index (contacts, i);

          caps = tp_contact_get_capabilities (contact);
          if (!_capabilities_has_stream_tube (caps))
            continue;

          if (context->contact_id != NULL &&
              tp_strdiff (context->contact_id,
                  tp_contact_get_identifier (contact)))
            continue;

          found = TRUE;
          g_ptr_array_add (candidates, g_object_ref (contact));

          g_string_append_printf (subtext, "  %d) %s (%s)\n", candidates->len,
              tp_contact_get_alias (contact),
              tp_contact_get_identifier (contact));
        }
      g_ptr_array_unref (contacts);

      if (found)
        {
          g_string_append_printf (text,
              "Account %s (%s):\n%s",
              tp_account_get_display_name (l->data),
              tp_account_get_protocol (l->data),
              subtext->str);
        }
      g_string_free (subtext, TRUE);
    }

  if (candidates->len == 0)
    {
      throw_error_message (context, "No suitable contact");
      goto OUT;
    }

  if (candidates->len == 1 && context->contact_id != NULL)
    {
      start_tube (context, g_ptr_array_index (candidates, 0));
      goto OUT;
    }

  g_print ("%sWhich contact to use? ", text->str);
  str = fgets (buffer, sizeof (buffer), stdin);
  if (str != NULL)
    {
      guint i; 

      str[strlen (str) - 1] = '\0';
      i = atoi (str) - 1;

      if (i < candidates->len)
        {
          start_tube (context, g_ptr_array_index (candidates, i));
          goto OUT;
        }
    }

  throw_error_message (context, "Invalid contact number");

OUT:
  g_ptr_array_unref (candidates);
  g_string_free (text, TRUE);
}

static void
account_prepared_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccount *account = TP_ACCOUNT (object);
  ClientContext *context = user_data;
  GList *accounts;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (account), res, &error))
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  accounts = g_list_prepend (NULL, account);
  choose_contact (context, accounts);
  g_list_free (accounts);
}

static void
account_manager_prepared_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (object);
  ClientContext *context = user_data;
  GList *accounts;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (manager), res, &error))
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (manager);
  choose_contact (context, accounts);
  g_list_free (accounts);
}

static void
client_context_clear (ClientContext *context)
{
  tp_clear_pointer (&context->loop, g_main_loop_unref);
  g_free (context->argv0);
  g_free (context->account_path);
  g_free (context->contact_id);
  g_free (context->login);
  g_strfreev (context->ssh_opts);

  tp_clear_object (&context->channel);
  tp_clear_object (&context->tube_connection);
  tp_clear_object (&context->ssh_connection);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus;
  TpSimpleClientFactory *factory;
  GError *error = NULL;
  ClientContext context = { 0, };
  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "account", 'a',
        0, G_OPTION_ARG_STRING, &context.account_path,
        "The account ID",
        NULL },
      { "contact", 'c',
        0, G_OPTION_ARG_STRING, &context.contact_id,
        "The contact ID",
        NULL },
      { "login", 'l',
        0, G_OPTION_ARG_STRING, &context.login,
        "Specifies the user to log in as on the remote machine",
        NULL },
      { G_OPTION_REMAINING, 0,
        0, G_OPTION_ARG_STRING_ARRAY, &context.ssh_opts,
        NULL,
        NULL },
      { NULL }
  };

  g_type_init ();

  optcontext = g_option_context_new ("-- [OPTIONS FOR SSH CLIENT]");
  g_option_context_add_main_entries (optcontext, options, NULL);
  if (!g_option_context_parse (optcontext, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command "
          "line options.\n", error->message, argv[0]);
      return EXIT_FAILURE;
    }
  g_option_context_free (optcontext);

  context.argv0 = g_strdup (argv[0]);
  g_set_application_name (PACKAGE_NAME);
  tp_debug_set_flags (g_getenv ("SSH_CONTACT_DEBUG"));

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    goto OUT;

  /* Create a factory and define the features we need */
  factory = (TpSimpleClientFactory *) tp_automatic_client_factory_new (dbus);
  tp_simple_client_factory_add_account_features_varargs (factory,
      TP_ACCOUNT_FEATURE_CONNECTION,
      0);
  tp_simple_client_factory_add_connection_features_varargs (factory,
      TP_CONNECTION_FEATURE_CONTACT_LIST,
      TP_CONNECTION_FEATURE_CAPABILITIES,
      0);
  tp_simple_client_factory_add_contact_features_varargs (factory,
      TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_CAPABILITIES,
      TP_CONTACT_FEATURE_INVALID);
  g_object_unref (dbus);

  /* If user gave an account path, prepare only that account, otherwise prepare
   * the whole account manager. */
  if (context.account_path != NULL)
    {
      TpAccount *account;
      GArray *features;

      /* Fixup account path if needed */
      if (!g_str_has_prefix (context.account_path, TP_ACCOUNT_OBJECT_PATH_BASE))
        {
          gchar *account_id = context.account_path;

          context.account_path = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE,
            account_id, NULL);

           g_free (account_id);
        }

      account = tp_simple_client_factory_ensure_account (factory,
          context.account_path, NULL, &error);
      if (account == NULL)
        goto OUT;

      features = tp_simple_client_factory_dup_account_features (factory,
          account);

      tp_proxy_prepare_async (account, (GQuark *) features->data,
          account_prepared_cb, &context);

      g_object_unref (account);
      g_array_unref (features);
    }
  else
    {
      TpAccountManager *manager;

      manager = tp_account_manager_new_with_factory (factory);

      tp_proxy_prepare_async (manager, NULL,
          account_manager_prepared_cb, &context);
      g_object_unref (manager);
    }
  g_object_unref (factory);

  context.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (context.loop);

OUT:

  if (error != NULL)
    {
      context.success = FALSE;
      g_debug ("Error: %s", error->message);
    }

  g_clear_error (&error);
  client_context_clear (&context);

  return context.success ? EXIT_SUCCESS : EXIT_FAILURE;
}
