/*
 * Copyright © 2015 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include "xdg-app-permission-store.h"

static gboolean
handle_lookup (XdgAppPermissionStore *object,
               GDBusMethodInvocation *invocation,
               const gchar *arg_table,
               const gchar *arg_id)
{
  GVariant *permissions = NULL;
  GVariant *data = NULL;

  xdg_app_permission_store_complete_lookup (object, invocation,
                                            permissions, data);

  return TRUE;
}

void
xdg_app_permission_store_start (GDBusConnection *connection)
{
  XdgAppPermissionStore *store;
  GError *error = NULL;

  store = xdg_app_permission_store_skeleton_new ();

  g_signal_connect (store, "handle-lookup", G_CALLBACK (handle_lookup), NULL);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (store),
                                         connection,
                                         "/org/freedesktop/XdgApp/PermissionStore",
                                         &error))
    {
      g_warning ("error: %s\n", error->message);
      g_error_free (error);
    }
}
