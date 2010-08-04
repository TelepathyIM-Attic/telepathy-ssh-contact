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

#ifndef __SSH_CONTACT_TREE_VIEW_H__
#define __SSH_CONTACT_TREE_VIEW_H__

#include <gtk/gtk.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

#define SSH_CONTACT_TYPE_TREE_VIEW             (ssh_contact_tree_view_get_type ())
#define SSH_CONTACT_TREE_VIEW(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), SSH_CONTACT_TYPE_TREE_VIEW, SshContactTreeView))
#define SSH_CONTACT_TREE_VIEW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), SSH_CONTACT_TYPE_TREE_VIEW, SshContactTreeViewClass))
#define SSH_CONTACT_IS_TREE_VIEW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SSH_CONTACT_TYPE_TREE_VIEW))
#define SSH_CONTACT_IS_TREE_VIEW_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), SSH_CONTACT_TYPE_TREE_VIEW))
#define SSH_CONTACT_TREE_VIEW_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), SSH_CONTACT_TYPE_TREE_VIEW, SshContactTreeViewClass))

typedef struct _SshContactTreeViewPrivate SshContactTreeViewPrivate;

typedef struct
{
  GtkTreeView parent_instance;
  SshContactTreeViewPrivate *priv;
} SshContactTreeView;

typedef struct 
{
  GtkTreeViewClass parent_class;
} SshContactTreeViewClass;

GType ssh_contact_tree_view_get_type (void) G_GNUC_CONST;

GtkWidget *ssh_contact_tree_view_new (void);

TpAccount *ssh_contact_tree_view_get_selected_account (SshContactTreeView *self);

TpContact *ssh_contact_tree_view_get_selected_contact (SshContactTreeView *self);


G_END_DECLS

#endif /* __SSH_CONTACT_TREE_VIEW_H__  */
/* vim: set ts=8: */
