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

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include <vinagre/vinagre-debug.h>
#include <vinagre/vinagre-cache-prefs.h>

#include "plugin.h"
#include "connection.h"
#include "tab.h"
#include "tree-view.h"

VINAGRE_PLUGIN_REGISTER_TYPE (SshContactPlugin, ssh_contact_plugin)

static void
impl_activate (VinagrePlugin *plugin,
    VinagreWindow *window)
{
  vinagre_debug_message (DEBUG_PLUGINS, "SshContactPlugin Activate");
}

static void
impl_deactivate  (VinagrePlugin *plugin,
    VinagreWindow *window)
{
  vinagre_debug_message (DEBUG_PLUGINS, "SshContactPlugin Deactivate");
}

static void
impl_update_ui (VinagrePlugin *plugin,
    VinagreWindow *window)
{
  vinagre_debug_message (DEBUG_PLUGINS, "SshContactPlugin Update UI");
}

static const gchar *
impl_get_protocol (VinagrePlugin *plugin)
{
  return "ssh-contact";
}

static gchar **
impl_get_public_description (VinagrePlugin *plugin)
{
  gchar **result = g_new (gchar *, 3);

  result[0] = g_strdup (_("SSH-Contact"));
  /* Translators: This is a description of the SSH protocol. It appears at
   * Connect dialog. */
  result[1] = g_strdup (_("Access Unix/Linux terminals using a Telepathy Tube"));
  result[2] = NULL;

  return result;
}

static const gchar *
impl_get_mdns_service (VinagrePlugin *plugin)
{
  return NULL;
}

static VinagreConnection *
impl_new_connection (VinagrePlugin *plugin)
{
  return ssh_contact_connection_new ();
}

static VinagreConnection *
impl_new_connection_from_string (VinagrePlugin *plugin,
  const gchar *uri,
  gchar **error_msg,
  gboolean use_bookmarks)
{
  VinagreConnection *conn = NULL;
  const gchar *p1 = uri;
  const gchar *p2;
  gchar *account_path;
  gchar *contact_id;

  /* FIXME: Search in bookmarks */

  /* URI is in the form "ssh-contact://<account path>/<contact id>". */

  /* Skip the scheme part */
  if (!g_str_has_prefix (p1, vinagre_plugin_get_protocol (plugin)))
    goto OUT;
  p1 += strlen (vinagre_plugin_get_protocol (plugin));
  if (!g_str_has_prefix (p1, "://"))
    goto OUT;
  p1 += strlen ("://");

  /* Search delimiter between account path and contact id */
  p2 = g_strrstr (p1, "/");
  if (p2 == NULL)
    goto OUT;

  account_path = g_strndup (p1, p2 - p1);
  contact_id = g_strdup (p2 + 1);

  conn = ssh_contact_connection_new ();
  ssh_contact_connection_set_account_path (SSH_CONTACT_CONNECTION (conn),
      account_path);
  ssh_contact_connection_set_contact_id (SSH_CONTACT_CONNECTION (conn),
      contact_id);

  g_free (account_path);
  g_free (contact_id);

OUT:

  if (conn == NULL)
    *error_msg = g_strdup ("URI format not recognized");

  return conn;
}

static GtkWidget *
impl_new_tab (VinagrePlugin *plugin,
    VinagreConnection *conn,
    VinagreWindow *window)
{
  return ssh_contact_tab_new (conn, window);
}

static gint
impl_get_default_port (VinagrePlugin *plugin)
{
  return 0;
}

static GtkWidget *
impl_get_connect_widget (VinagrePlugin *plugin,
    VinagreConnection *conn)
{
  GtkWidget *box, *label, *u_box, *u_entry;
  gchar *str;

  box = gtk_vbox_new (FALSE, 0);

  str = g_strdup_printf ("<b>%s</b>", _("SSH Options"));
  label = gtk_label_new (str);
  g_free (str);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_misc_set_padding (GTK_MISC (label), 0, 6);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  u_box = gtk_hbox_new (FALSE, 4);
  label = gtk_label_new ("  ");
  gtk_box_pack_start (GTK_BOX (u_box), label, FALSE, FALSE, 0);

  label = gtk_label_new_with_mnemonic (_("_Username:"));
  gtk_box_pack_start (GTK_BOX (u_box), label, FALSE, FALSE, 0);

  u_entry = gtk_entry_new ();
  /* Translators: This is the tooltip for the username field in a SSH
   * connection */
  gtk_widget_set_tooltip_text (u_entry,
      _("Optional. If blank, your username will be used."));
  g_object_set_data (G_OBJECT (box), "username_entry", u_entry);
  gtk_box_pack_start (GTK_BOX (u_box), u_entry, TRUE, TRUE, 5);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), u_entry);
  str = g_strdup (VINAGRE_IS_CONNECTION (conn) ?
		  vinagre_connection_get_username (conn) :
		  vinagre_cache_prefs_get_string  ("ssh-contact-connection", "username", ""));
  gtk_entry_set_text (GTK_ENTRY (u_entry), str);
  gtk_entry_set_activates_default (GTK_ENTRY (u_entry), TRUE);
  g_free (str);

  gtk_box_pack_start (GTK_BOX (box), u_box, TRUE, TRUE, 0);

  return box;
}

static GtkWidget *
impl_get_connect_host_widget (VinagrePlugin *plugin,
    VinagreConnection *conn)
{
  GtkWidget *sw;
  GtkWidget *view;

  view = ssh_contact_tree_view_new ();
  gtk_widget_show (view);

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_container_add (GTK_CONTAINER (sw), view);
  gtk_widget_set_size_request (GTK_WIDGET (sw), -1, 150);

  return sw;
}

static void
impl_parse_mdns_dialog (VinagrePlugin *plugin,
    GtkWidget *connect_widget,
    GtkWidget *dialog)
{
}

static void
ssh_contact_plugin_init (SshContactPlugin *self)
{
}

static void
ssh_contact_plugin_class_init (SshContactPluginClass *klass)
{
  VinagrePluginClass *plugin_class = VINAGRE_PLUGIN_CLASS (klass);

  plugin_class->activate   = impl_activate;
  plugin_class->deactivate = impl_deactivate;
  plugin_class->update_ui  = impl_update_ui;
  plugin_class->get_protocol  = impl_get_protocol;
  plugin_class->get_public_description  = impl_get_public_description;
  plugin_class->new_connection = impl_new_connection;
  plugin_class->new_connection_from_string = impl_new_connection_from_string;
  plugin_class->get_mdns_service  = impl_get_mdns_service;
  plugin_class->new_tab = impl_new_tab;
  plugin_class->get_default_port = impl_get_default_port;
  plugin_class->get_connect_widget = impl_get_connect_widget;
  plugin_class->get_connect_host_widget = impl_get_connect_host_widget;
  plugin_class->parse_mdns_dialog = impl_parse_mdns_dialog;
}
/* vim: set ts=8: */
