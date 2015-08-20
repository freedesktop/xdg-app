/* xdg-app-db.h
 *
 * Copyright © 2015 Red Hat, Inc
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#ifndef XDG_APP_DB_H
#define XDG_APP_DB_H

#include <string.h>

#include "libglnx/libglnx.h"
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct XdgAppDb XdgAppDb;
typedef struct _XdgAppDbEntry XdgAppDbEntry;

#define XDG_APP_TYPE_DB (xdg_app_db_get_type())
#define XDG_APP_DB(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XDG_APP_TYPE_DB, XdgAppDb))
#define XDG_APP_IS_DB(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XDG_APP_TYPE_DB))

GType xdg_app_db_get_type (void);

XdgAppDb *xdg_app_db_new               (const char     *path,
                                              GError        **error);
char **         xdg_app_db_list_ids          (XdgAppDb *self);
char **         xdg_app_db_list_ids_by_app   (XdgAppDb *self,
                                              const char     *app);
char **         xdg_app_db_list_ids_by_value (XdgAppDb *self,
                                              GVariant       *data);
XdgAppDbEntry * xdg_app_db_lookup            (XdgAppDb *self,
                                              const char     *id);

XdgAppDbEntry  *xdg_app_db_entry_ref              (XdgAppDbEntry  *entry);
void            xdg_app_db_entry_unref            (XdgAppDbEntry  *entry);
GVariant *      xdg_app_db_entry_get_data         (XdgAppDbEntry  *entry);
const char **   xdg_app_db_entry_list_apps        (XdgAppDbEntry  *entry);
const char **   xdg_app_db_entry_list_permissions (XdgAppDbEntry  *entry,
                                                   const char     *app);
gboolean        xdg_app_db_entry_has_permission   (XdgAppDbEntry  *entry,
                                                   const char     *app,
                                                   const char     *permission);
gboolean        xdg_app_db_entry_has_permissions  (XdgAppDbEntry  *entry,
                                                   const char     *app,
                                                   const char    **permissions);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(XdgAppDb, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(XdgAppDbEntry, xdg_app_db_entry_unref)

G_END_DECLS

#endif /* XDG_APP_DB_H */

