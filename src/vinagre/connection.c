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

#include <vinagre/vinagre-utils.h>
#include <vinagre/vinagre-cache-prefs.h>

#include <telepathy-glib/telepathy-glib.h>

#include "connection.h"
#include "tree-view.h"

struct _SshContactConnectionPrivate
{
  gchar *account_path;
  gchar *contact_id;
};

G_DEFINE_TYPE (SshContactConnection, ssh_contact_connection,
    VINAGRE_TYPE_CONNECTION);

static void
impl_fill_writer (VinagreConnection *conn, xmlTextWriter *writer)
{
  SshContactConnection *self = SSH_CONTACT_CONNECTION (conn);

  xmlTextWriterWriteElement (writer, (const xmlChar *) "account",
      (const xmlChar *) (self->priv->account_path ? self->priv->account_path : ""));
  xmlTextWriterWriteElement (writer, (const xmlChar *) "contact",
      (const xmlChar *) (self->priv->contact_id ? self->priv->contact_id : ""));

  VINAGRE_CONNECTION_CLASS (ssh_contact_connection_parent_class)->impl_fill_writer (conn, writer);
}

static void
impl_parse_item (VinagreConnection *conn, xmlNode *root)
{
  SshContactConnection *self = SSH_CONTACT_CONNECTION (conn);
  xmlNode *curr;
  xmlChar *s_value;

  for (curr = root->children; curr; curr = curr->next)
    {
      s_value = xmlNodeGetContent (curr);

      if (!xmlStrcmp(curr->name, (const xmlChar *)"account"))
	ssh_contact_connection_set_account_path (self, (gchar *) s_value);
      else if (!xmlStrcmp(curr->name, (const xmlChar *)"contact"))
	ssh_contact_connection_set_contact_id (self, (gchar *) s_value);

      xmlFree (s_value);
    }

  VINAGRE_CONNECTION_CLASS (ssh_contact_connection_parent_class)->impl_parse_item (conn, root);
}

static void
impl_parse_options_widget (VinagreConnection *conn,
    GtkWidget *widget)
{
  GtkWidget *entry;
  const gchar *username;

  entry = g_object_get_data (G_OBJECT (widget), "username_entry");
  if (entry == NULL)
    {
      g_warning ("Wrong widget passed to impl_parse_options_widget()");
      return;
    }

  username = gtk_entry_get_text (GTK_ENTRY (entry));
  vinagre_cache_prefs_set_string  ("ssh-contact-connection", "username",
      username);
  vinagre_connection_set_username (conn, username);

  VINAGRE_CONNECTION_CLASS (ssh_contact_connection_parent_class)->impl_parse_options_widget (conn, widget);
}

static void
impl_parse_host_widget (VinagreConnection *conn,
    GtkWidget *widget)
{
  SshContactConnection *self = SSH_CONTACT_CONNECTION (conn);
  SshContactTreeView *view;
  TpAccount *account;
  TpContact *contact;

  /* widget is a GtkScrolledWindow containing an SshContactTreeView */
  view = SSH_CONTACT_TREE_VIEW (gtk_bin_get_child (GTK_BIN (widget)));

  account = ssh_contact_tree_view_get_selected_account (view);
  ssh_contact_connection_set_account_path (self,
      tp_proxy_get_object_path (account));

  contact = ssh_contact_tree_view_get_selected_contact (view);
  ssh_contact_connection_set_contact_id (self,
      tp_contact_get_identifier (contact));

  VINAGRE_CONNECTION_CLASS (ssh_contact_connection_parent_class)->impl_parse_host_widget (conn, widget);
}

static gchar *
impl_get_string_rep (VinagreConnection *conn,
    gboolean has_protocol)
{
  SshContactConnection *self = SSH_CONTACT_CONNECTION (conn);
  GString *uri;

  if (has_protocol)
    {
      uri = g_string_new (vinagre_connection_get_protocol (conn));
      g_string_append (uri, "://");
    }
  else
    {
      uri = g_string_new (NULL);
    }

  g_string_append (uri, self->priv->account_path);
  g_string_append_c (uri, '/');
  g_string_append (uri, self->priv->contact_id);

  return g_string_free (uri, FALSE);
}

static void
finalize (GObject *object)
{
  SshContactConnection *self = SSH_CONTACT_CONNECTION (object);

  g_free (self->priv->contact_id);
  g_free (self->priv->account_path);

  G_OBJECT_CLASS (ssh_contact_connection_parent_class)->finalize (object);
}

static void
ssh_contact_connection_init (SshContactConnection *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SSH_CONTACT_TYPE_CONNECTION,
      SshContactConnectionPrivate);
}

static void
ssh_contact_connection_class_init (SshContactConnectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  VinagreConnectionClass* vinagre_class = VINAGRE_CONNECTION_CLASS (klass);

  object_class->finalize = finalize;

  vinagre_class->impl_fill_writer = impl_fill_writer;
  vinagre_class->impl_parse_item = impl_parse_item;
  vinagre_class->impl_parse_options_widget = impl_parse_options_widget;
  vinagre_class->impl_parse_host_widget = impl_parse_host_widget;
  vinagre_class->impl_get_string_rep = impl_get_string_rep;

  g_type_class_add_private (klass, sizeof (SshContactConnectionPrivate));
}

VinagreConnection *
ssh_contact_connection_new (void)
{
  return g_object_new (SSH_CONTACT_TYPE_CONNECTION,
      "protocol", "ssh-contact",
      NULL);
}

const gchar *
ssh_contact_connection_get_account_path (SshContactConnection *self)
{
  g_return_val_if_fail (SSH_CONTACT_IS_CONNECTION (self), NULL);

  return self->priv->account_path;
}

void
ssh_contact_connection_set_account_path (SshContactConnection *self,
    const gchar *account_path)
{
  g_return_if_fail (SSH_CONTACT_IS_CONNECTION (self));

  g_free (self->priv->account_path);
  self->priv->account_path = g_strdup (account_path);
}

const gchar *
ssh_contact_connection_get_contact_id (SshContactConnection *self)
{
  g_return_val_if_fail (SSH_CONTACT_IS_CONNECTION (self), NULL);

  return self->priv->contact_id;
}

void
ssh_contact_connection_set_contact_id (SshContactConnection *self,
    const gchar *contact_id)
{
  g_return_if_fail (SSH_CONTACT_IS_CONNECTION (self));

  g_free (self->priv->contact_id);
  self->priv->contact_id = g_strdup (contact_id);
}

/* vim: set ts=8: */
