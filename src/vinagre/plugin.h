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

#ifndef __SSH_CONTACT_PLUGIN_H__
#define __SSH_CONTACT_PLUGIN_H__

#include <glib.h>
#include <glib-object.h>
#include <vinagre/vinagre-plugin.h>

G_BEGIN_DECLS

#define SSH_CONTACT_TYPE_PLUGIN                 (ssh_contact_plugin_get_type ())
#define SSH_CONTACT_PLUGIN(o)                   (G_TYPE_CHECK_INSTANCE_CAST ((o), SSH_CONTACT_TYPE_PLUGIN, SshContactPlugin))
#define SSH_CONTACT_PLUGIN_CLASS(k)             (G_TYPE_CHECK_CLASS_CAST((k), SSH_CONTACT_TYPE_PLUGIN, SshContactPluginClass))
#define SSH_CONTACT_IS_PLUGIN(o)                (G_TYPE_CHECK_INSTANCE_TYPE ((o), SSH_CONTACT_TYPE_PLUGIN))
#define SSH_CONTACT_IS_PLUGIN_CLASS(k)          (G_TYPE_CHECK_CLASS_TYPE ((k), SSH_CONTACT_TYPE_PLUGIN))
#define SSH_CONTACT_PLUGIN_GET_CLASS(o)         (G_TYPE_INSTANCE_GET_CLASS ((o), SSH_CONTACT_TYPE_PLUGIN, SshContactPluginClass))

typedef struct _SshContactPluginPrivate	SshContactPluginPrivate;

typedef struct
{
  VinagrePlugin parent_instance;
  SshContactPluginPrivate *priv;
} SshContactPlugin;

typedef struct
{
  VinagrePluginClass parent_class;
} SshContactPluginClass;

GType ssh_contact_plugin_get_type (void) G_GNUC_CONST;

/* All the plugins must implement this function */
G_MODULE_EXPORT GType register_vinagre_plugin (GTypeModule *module);

G_END_DECLS

#endif /* __SSH_CONTACT_PLUGIN_H__ */
/* vim: set ts=8: */
