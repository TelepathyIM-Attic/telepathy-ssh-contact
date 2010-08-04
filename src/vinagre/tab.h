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

#ifndef __SSH_CONTACT_TAB_H__
#define __SSH_CONTACT_TAB_H__

#include <vinagre/vinagre-tab.h>

G_BEGIN_DECLS

#define SSH_CONTACT_TYPE_TAB              (ssh_contact_tab_get_type())
#define SSH_CONTACT_TAB(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), SSH_CONTACT_TYPE_TAB, SshContactTab))
#define SSH_CONTACT_TAB_CONST(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), SSH_CONTACT_TYPE_TAB, SshContactTab const))
#define SSH_CONTACT_TAB_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), SSH_CONTACT_TYPE_TAB, SshContactTabClass))
#define SSH_CONTACT_IS_TAB(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj), SSH_CONTACT_TYPE_TAB))
#define SSH_CONTACT_IS_TAB_CLASS(klass)...(G_TYPE_CHECK_CLASS_TYPE ((klass), SSH_CONTACT_TYPE_TAB))
#define SSH_CONTACT_TAB_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), SSH_CONTACT_TYPE_TAB, SshContactTabClass))

typedef struct _SshContactTabPrivate SshContactTabPrivate;

typedef struct
{
  VinagreTab tab;
  SshContactTabPrivate *priv;
} SshContactTab;

typedef struct
{
  VinagreTabClass parent_class;
} SshContactTabClass;

GType ssh_contact_tab_get_type (void) G_GNUC_CONST;

GtkWidget *ssh_contact_tab_new (VinagreConnection *conn, VinagreWindow *window);

G_END_DECLS

#endif  /* __VINAGRE_SSH_TAB_H__  */
/* vim: set ts=8: */
