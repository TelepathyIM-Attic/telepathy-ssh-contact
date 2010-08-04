/*
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
#include <gtk/gtk.h>

#include <telepathy-glib/telepathy-glib.h>

#include "tree-view.h"
#include "../client-helpers.h"

enum
{
  COL_ACCOUNT,
  COL_CONTACT,
  COL_TEXT,
  N_COL
};

struct _SshContactTreeViewPrivate
{
  GtkListStore *store;
  TpAccountManager *account_manager;
};

G_DEFINE_TYPE (SshContactTreeView, ssh_contact_tree_view,
    GTK_TYPE_TREE_VIEW);

static TpAccount *
find_account_for_contact (SshContactTreeView *self,
    TpContact *contact)
{
  TpAccount *account = NULL;
  TpConnection *connection;
  GList *list;

  connection = tp_contact_get_connection (contact);
  list = tp_account_manager_get_valid_accounts (self->priv->account_manager);
  while (list)
    {
      if (tp_account_get_connection (list->data) == connection)
        {
          account = list->data;
          break;
        }
        
      list = g_list_delete_link (list, list);
    }
  g_list_free (list);

  return account;
}

static void
got_contacts_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    guint n_failed,
    const TpHandle *failed,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  SshContactTreeView *self = user_data;
  guint i;

  if (error != NULL)
    {
      g_debug ("%s", error->message);
      return;
    }

  for (i = 0; i < n_contacts; i++)
    {
      TpAccount *account;

      if (!_capabilities_has_stream_tube (tp_contact_get_capabilities (contacts[i])))
        continue;

      account = find_account_for_contact (self, contacts[i]);
      gtk_list_store_insert_with_values (self->priv->store, NULL, -1,
          COL_ACCOUNT, account,
          COL_CONTACT, contacts[i],
          COL_TEXT, tp_contact_get_alias (contacts[i]),
          -1);
    }
}

static void
stored_channel_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer self)
{
  TpChannel *channel = TP_CHANNEL (object);
  TpConnection *connection;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_CAPABILITIES };
  const TpIntSet *set;
  GArray *handles;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (channel, res, &error))
    {
      g_debug ("%s", error->message);
      g_clear_error (&error);
      return;
    }

  connection = tp_channel_borrow_connection (channel);
  set = tp_channel_group_get_members (channel);
  handles = tp_intset_to_array (set);

  tp_connection_get_contacts_by_handle (connection, handles->len,
      (TpHandle *) handles->data, G_N_ELEMENTS (features), features,
      got_contacts_cb, self, NULL, NULL);

  g_array_unref (handles);
}

static void
ensure_stored_channel_cb (TpConnection *connection,
    gboolean yours,
    const gchar *channel_path,
    GHashTable *properties,
    const GError *error,
    gpointer self,
    GObject *weak_object)
{
  TpChannel *channel;
  GQuark features[] = { TP_CHANNEL_FEATURE_GROUP, 0 };
  GError *err = NULL;

  if (error != NULL)
    {
      g_debug ("%s", error->message);
      return;
    }

  channel = tp_channel_new_from_properties (connection, channel_path,
      properties, &err);
  if (channel == NULL)
    {
      g_debug ("%s", err->message);
      g_clear_error (&err);
      return;
    }

  tp_proxy_prepare_async (TP_PROXY (channel), features,
      stored_channel_prepare_cb, self);

  g_object_unref (channel);
}

static void
connection_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer self)
{
  TpConnection *connection = TP_CONNECTION (object);
  GError *error = NULL;
  GHashTable *request;

  if (!tp_proxy_prepare_finish (TP_PROXY (connection), res, &error))
    {
      g_debug ("%s", error->message);
      g_clear_error (&error);
      return;
    }

  if (!_capabilities_has_stream_tube (tp_connection_get_capabilities (connection)))
    return;

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_LIST,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        "stored",
      NULL);

  tp_cli_connection_interface_requests_call_ensure_channel (connection, -1,
      request, ensure_stored_channel_cb, self, NULL, NULL);

  g_hash_table_unref (request);
}

static void
account_manager_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer self)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (object);
  GList *accounts, *l;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (manager), res, &error))
    {
      g_debug ("%s", error->message);
      g_clear_error (&error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (manager);
  for (l = accounts; l != NULL; l = l->next)
    {
      GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
      TpAccount *account = l->data;
      TpConnection *connection;
    
      connection = tp_account_get_connection (account);
      if (connection != NULL)
        tp_proxy_prepare_async (TP_PROXY (connection), features,
            connection_prepare_cb, self);
    }
  g_list_free (accounts);
}

static void
dispose (GObject *object)
{
  SshContactTreeView *self = SSH_CONTACT_TREE_VIEW (object);

  tp_clear_object (&self->priv->store);
  tp_clear_object (&self->priv->account_manager);

  G_OBJECT_CLASS (ssh_contact_tree_view_parent_class)->dispose (object);
}

static void
ssh_contact_tree_view_init (SshContactTreeView *self)
{
  GtkTreeView *view = GTK_TREE_VIEW (self);
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SSH_CONTACT_TYPE_TREE_VIEW,
      SshContactTreeViewPrivate);

  self->priv->store = gtk_list_store_new (N_COL,
      G_TYPE_OBJECT,  /* ACCOUNT */
      G_TYPE_OBJECT,  /* CONTACT */
      G_TYPE_STRING); /* TEXT */

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Contacts",
      renderer, "text", COL_TEXT, NULL);

  gtk_tree_view_append_column (view, column);
  gtk_tree_view_set_headers_visible (view, FALSE);
  gtk_tree_view_set_model (view, GTK_TREE_MODEL (self->priv->store));

  self->priv->account_manager = tp_account_manager_dup ();
  tp_proxy_prepare_async (TP_PROXY (self->priv->account_manager), NULL,
      account_manager_prepare_cb, self);
}

static void
ssh_contact_tree_view_class_init (SshContactTreeViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = dispose;

  g_type_class_add_private (klass, sizeof (SshContactTreeViewPrivate));
}

GtkWidget *
ssh_contact_tree_view_new (void)
{
  return g_object_new (SSH_CONTACT_TYPE_TREE_VIEW, NULL);
}

TpAccount *
ssh_contact_tree_view_get_selected_account (SshContactTreeView *self)
{
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  TpAccount *account = NULL;

  g_return_val_if_fail (SSH_CONTACT_IS_TREE_VIEW (self), NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter, COL_ACCOUNT, &account, -1);
  g_object_unref (account);

  return account;
}

TpContact *
ssh_contact_tree_view_get_selected_contact (SshContactTreeView *self)
{
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  TpContact *contact;

  g_return_val_if_fail (SSH_CONTACT_IS_TREE_VIEW (self), NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter, COL_CONTACT, &contact, -1);
  g_object_unref (contact);

  return contact;
}

/* vim: set ts=8: */
