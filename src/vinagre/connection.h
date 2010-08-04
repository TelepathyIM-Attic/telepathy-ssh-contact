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

#ifndef __SSH_CONTACT_CONNECTION_H__
#define __SSH_CONTACT_CONNECTION_H__

#include <vinagre/vinagre-connection.h>

G_BEGIN_DECLS

#define SSH_CONTACT_TYPE_CONNECTION             (ssh_contact_connection_get_type ())
#define SSH_CONTACT_CONNECTION(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), SSH_CONTACT_TYPE_CONNECTION, SshContactConnection))
#define SSH_CONTACT_CONNECTION_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), SSH_CONTACT_TYPE_CONNECTION, SshContactConnectionClass))
#define SSH_CONTACT_IS_CONNECTION(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SSH_CONTACT_TYPE_CONNECTION))
#define SSH_CONTACT_IS_CONNECTION_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), SSH_CONTACT_TYPE_CONNECTION))
#define SSH_CONTACT_CONNECTION_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), SSH_CONTACT_TYPE_CONNECTION, SshContactConnectionClass))

typedef struct _SshContactConnectionPrivate SshContactConnectionPrivate;

typedef struct
{
  VinagreConnection parent_instance;
  SshContactConnectionPrivate *priv;
} SshContactConnection;

typedef struct 
{
  VinagreConnectionClass parent_class;
} SshContactConnectionClass;

GType ssh_contact_connection_get_type (void) G_GNUC_CONST;

VinagreConnection *ssh_contact_connection_new (void);

const gchar *ssh_contact_connection_get_account_path (SshContactConnection *self);

void ssh_contact_connection_set_account_path (SshContactConnection *self,
    const gchar *account_path);

const gchar *ssh_contact_connection_get_contact_id (SshContactConnection *self);

void ssh_contact_connection_set_contact_id (SshContactConnection *self,
    const gchar *contact_id);

G_END_DECLS

#endif /* __SSH_CONTACT_CONNECTION_H__  */
/* vim: set ts=8: */
