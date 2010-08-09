/*
 * Copyright (C) 2009 Jonh Wendell <wendell@bani.com.br>
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

#include <glib/gi18n.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>

#include <vinagre/vinagre-utils.h>
#include <vinagre/vinagre-prefs.h>

#include "tab.h"
#include "connection.h"
#include "../client-helpers.h"
#include "../common.h"

struct _SshContactTabPrivate
{
  GtkWidget *vte;
  GSocketConnection *tube_connection;
  GSocketConnection *ssh_connection;
};

G_DEFINE_TYPE (SshContactTab, ssh_contact_tab, VINAGRE_TYPE_TAB)

static void
ssh_disconnected_cb (VteTerminal *ssh,
    SshContactTab *self)
{
  g_signal_emit_by_name (self, "tab-disconnected");
}

static void
get_connection_info (SshContactTab *self,
    const gchar **account_path,
    const gchar **contact_id,
    const gchar **username)
{
  VinagreConnection *conn;

  conn = vinagre_tab_get_conn (VINAGRE_TAB (self));
  if (account_path != NULL)
    *account_path = ssh_contact_connection_get_account_path (SSH_CONTACT_CONNECTION (conn));
  if (contact_id != NULL)
    *contact_id = ssh_contact_connection_get_contact_id (SSH_CONTACT_CONNECTION (conn));
  if (username != NULL)
    *username = vinagre_connection_get_username (conn);
}

static gchar *
impl_get_tooltip (VinagreTab *tab)
{
  SshContactTab *self = SSH_CONTACT_TAB (tab);
  const gchar *account_path;
  const gchar *contact_id;

  get_connection_info (self, &account_path, &contact_id, NULL);

  return  g_markup_printf_escaped (
      "<b>%s</b> %s\n"
      "<b>%s</b> %s",
      _("Account:"), account_path + strlen (TP_ACCOUNT_OBJECT_PATH_BASE),
      _("Contact:"), contact_id);
}

static GdkPixbuf *
impl_get_screenshot (VinagreTab *tab)
{
  SshContactTab *self = SSH_CONTACT_TAB (tab);
  GdkPixbuf *pixbuf;
  GdkPixmap *pixmap;

  pixmap = gtk_widget_get_snapshot (self->priv->vte, NULL);
  pixbuf = gdk_pixbuf_get_from_drawable (NULL, GDK_DRAWABLE (pixmap),
      gdk_colormap_get_system (), 0, 0, 0, 0, -1, -1);

  g_object_unref (pixmap);

  return pixbuf;
}

static void
throw_error (SshContactTab *self,
    const GError *error)
{
  g_debug ("ERROR: %s", error->message);
  g_signal_emit_by_name (self, "tab-auth-failed", error->message);
}

static void
splice_cb (GIOStream *stream1,
    GIOStream *stream2,
    const GError *error,
    gpointer user_data)
{
  SshContactTab *self = user_data;

  if (error != NULL)
    throw_error (self, error);
  else
      g_signal_emit_by_name (self, "tab-disconnected");
}

static void
maybe_start_splice (SshContactTab *self)
{
  if (self->priv->tube_connection != NULL && self->priv->ssh_connection != NULL)
    {
      g_signal_emit_by_name (self, "tab-connected");

      /* Splice tube and ssh connections */
      _g_io_stream_splice (G_IO_STREAM (self->priv->tube_connection),
          G_IO_STREAM (self->priv->ssh_connection), splice_cb, self);
    }
}

static void
ssh_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  SshContactTab *self = user_data;
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GError *error = NULL;

  self->priv->ssh_connection = g_socket_listener_accept_finish (listener, res,
      NULL, &error);
  if (error != NULL)
    {
      throw_error (self, error);
      g_clear_error (&error);
      return;
    }

  maybe_start_splice (self);
}

static void
create_tube_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  SshContactTab *self = user_data;
  GError *error = NULL;

  self->priv->tube_connection = _client_create_tube_finish (res, &error);
  if (error != NULL)
    {
      throw_error (self, error);
      g_clear_error (&error);
      return;
    }

  maybe_start_splice (self);
}

static gboolean
start_tube (gpointer user_data)
{
  SshContactTab *self = user_data;
  const gchar *username;
  const gchar *account_path;
  const gchar *contact_id;
  GSocketListener *listener;
  GSocket *socket;
  GError *error = NULL;
  GStrv args = NULL;

  g_signal_emit_by_name (self, "tab-initialized");

  get_connection_info (self, &account_path, &contact_id, &username);

  listener = g_socket_listener_new ();
  socket = _client_create_local_socket (&error);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_listen (socket, &error))
    goto OUT;
  if (!g_socket_listener_add_socket (listener, socket, NULL, &error))
    goto OUT;

  g_socket_listener_accept_async (listener, NULL,
      ssh_socket_connected_cb, self);

  args = _client_create_exec_args (socket, contact_id, username);
  vte_terminal_fork_command (VTE_TERMINAL (self->priv->vte), "ssh", args,
      NULL, NULL, FALSE, FALSE, FALSE);

  _client_create_tube_async (account_path, contact_id, create_tube_cb, self);

OUT:

  if (error != NULL)
    throw_error (self, error);

  g_clear_error (&error);
  tp_clear_object (&listener);
  tp_clear_object (&socket);
  g_strfreev (args);

  return FALSE;
}

static void
constructed (GObject *object)
{
  SshContactTab *self = SSH_CONTACT_TAB (object);

  g_idle_add (start_tube, self);

  vinagre_tab_add_recent_used (VINAGRE_TAB (self));
  vinagre_tab_set_state (VINAGRE_TAB (self), VINAGRE_TAB_STATE_CONNECTED);

  gtk_widget_show (GTK_WIDGET (self));

  if (G_OBJECT_CLASS (ssh_contact_tab_parent_class)->constructed)
    G_OBJECT_CLASS (ssh_contact_tab_parent_class)->constructed (object);
}

static void
dispose (GObject *object)
{
  SshContactTab *self = SSH_CONTACT_TAB (object);

  if (self->priv->tube_connection)
    g_io_stream_close (G_IO_STREAM (self->priv->tube_connection), NULL, NULL);
  tp_clear_object (&self->priv->tube_connection);

  if (self->priv->ssh_connection)
    g_io_stream_close (G_IO_STREAM (self->priv->ssh_connection), NULL, NULL);
  tp_clear_object (&self->priv->ssh_connection);

  if (G_OBJECT_CLASS (ssh_contact_tab_parent_class)->dispose)
    G_OBJECT_CLASS (ssh_contact_tab_parent_class)->dispose (object);
}

static void 
ssh_contact_tab_class_init (SshContactTabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  VinagreTabClass* tab_class = VINAGRE_TAB_CLASS (klass);

  object_class->constructed = constructed;
  object_class->dispose = dispose;

  tab_class->impl_get_tooltip = impl_get_tooltip;
  tab_class->impl_get_screenshot = impl_get_screenshot;

  g_type_class_add_private (klass, sizeof (SshContactTabPrivate));

}

static void
ssh_contact_tab_init (SshContactTab *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SSH_CONTACT_TYPE_TAB,
      SshContactTabPrivate);

  /* Create the ssh widget */
  self->priv->vte = vte_terminal_new ();
  vinagre_tab_add_view (VINAGRE_TAB (self), self->priv->vte);
  gtk_widget_show (self->priv->vte);

  g_signal_connect (self->priv->vte, "child-exited",
      G_CALLBACK (ssh_disconnected_cb), self);
}

GtkWidget *
ssh_contact_tab_new (VinagreConnection *conn,
    VinagreWindow *window)
{
  return g_object_new (SSH_CONTACT_TYPE_TAB, "conn", conn, "window", window,
      NULL);
}

/* vim: set ts=8: */
