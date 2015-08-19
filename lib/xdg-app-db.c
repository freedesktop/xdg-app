/* xdg-app-db.c
 *
 * Copyright (C) 2015 Red Hat, Inc
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

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/statfs.h>

#include "xdg-app-db.h"
#include "gvdb/gvdb-reader.h"

struct XdgAppDb {
  GObject parent;

  char *path;
  GvdbTable *gvdb;

  /* Map id => GVariant (data, array[(appid, perms)]) */
  GvdbTable *main_table;
  GHashTable *main_updates;

  /* (reverse) Map app id => [ id ]*/
  GvdbTable *app_table;
  GHashTable *app_updates;
};

typedef struct {
  GObjectClass parent_class;
} XdgAppDbClass;

static void initable_iface_init      (GInitableIface         *initable_iface);

G_DEFINE_TYPE_WITH_CODE (XdgAppDb, xdg_app_db, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));

enum {
  PROP_0,
  PROP_PATH,
  LAST_PROP
};

XdgAppDb *
xdg_app_db_new (const char *path, GError **error)
{
  return g_initable_new (XDG_APP_TYPE_DB,
			 NULL,
			 error,
			 "path", path,
			 NULL);
}

static void
xdg_app_db_finalize (GObject *object)
{
  XdgAppDb *self = (XdgAppDb *)object;

  g_clear_pointer (&self->path, g_free);
  g_clear_pointer (&self->gvdb, gvdb_table_free);
  g_clear_pointer (&self->main_table, gvdb_table_free);
  g_clear_pointer (&self->app_table, gvdb_table_free);
  g_clear_pointer (&self->main_updates, g_hash_table_unref);
  g_clear_pointer (&self->app_updates, g_hash_table_unref);

  G_OBJECT_CLASS (xdg_app_db_parent_class)->finalize (object);
}

static void
xdg_app_db_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  XdgAppDb *self = XDG_APP_DB(object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, self->path);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
xdg_app_db_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  XdgAppDb *self = XDG_APP_DB (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_clear_pointer (&self->path, g_free);
      self->path = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
xdg_app_db_class_init (XdgAppDbClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = xdg_app_db_finalize;
  object_class->get_property = xdg_app_db_get_property;
  object_class->set_property = xdg_app_db_set_property;

  g_object_class_install_property (object_class,
                                   PROP_PATH,
                                   g_param_spec_object ("path",
                                                        "",
                                                        "",
                                                        G_TYPE_FILE,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
xdg_app_db_init (XdgAppDb *self)
{
}

static gboolean
is_on_nfs (const char *path)
{
  struct statfs statfs_buffer;
  int statfs_result;
  g_autofree char *dirname = NULL;

  dirname = g_path_get_dirname (path);

  statfs_result = statfs (dirname, &statfs_buffer);
  if (statfs_result != 0)
    return FALSE;

  return statfs_buffer.f_type == 0x6969;
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  XdgAppDb *self = (XdgAppDb *)initable;

  if (is_on_nfs (self->path))
    {
      char *contents;
      gsize length;
      GBytes *bytes;

      g_autoptr (GFile) file = g_file_new_for_path (self->path);

      /* We avoid using mmap on NFS, because its prone to give us SIGBUS at semi-random
         times (nfs down, file removed, etc). Instead we just load the file */
      if (!g_file_load_contents (file, cancellable, &contents, &length, NULL, error))
        return FALSE;

      bytes = g_bytes_new_take (contents, length);
      self->gvdb = gvdb_table_new_from_bytes (bytes, TRUE, error);
      g_bytes_unref (bytes);
    }
  else
    {
      self->gvdb = gvdb_table_new (self->path, TRUE, error);
    }

  if (self->gvdb == NULL)
    return FALSE;

  self->main_table = gvdb_table_get_table (self->gvdb, "main");
  if (self->main_table == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "No main table in db");
      return FALSE;
    }

  self->app_table = gvdb_table_get_table (self->gvdb, "apps");
  if (self->app_table == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "No app table in db");
      return FALSE;
    }

  self->main_updates =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, (GDestroyNotify)g_variant_unref);
  self->app_updates =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, (GDestroyNotify)g_variant_unref);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

/* Transfer: full */
char **
xdg_app_db_list_ids (XdgAppDb *self)
{
  g_autofree char **main_ids = NULL;
  GPtrArray *res;
  GHashTableIter iter;
  gpointer key, value;
  int i;

  res = g_ptr_array_new ();

  g_hash_table_iter_init (&iter, self->main_updates);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (value != NULL)
        g_ptr_array_add (res, g_strdup (key));
    }

  // TODO: can we use gvdb_table_list here???
  main_ids = gvdb_table_get_names (self->main_table, NULL);
  for (i = 0; main_ids[i] != NULL; i++)
    {
      char *id = main_ids[i];

      if (g_hash_table_lookup_extended (self->main_updates, id, NULL, NULL))
        g_free (id);
      else
        g_ptr_array_add (res, id);
    }

  g_ptr_array_add (res, NULL);
  return (char **)g_ptr_array_free (res, FALSE);
}

/* Transfer: full */
char **
xdg_app_db_list_ids_by_app (XdgAppDb *self,
                            const char *app)
{
  g_autofree char **apps = NULL;
  GPtrArray *res;
  GHashTableIter iter;
  gpointer key, value;
  int i;

  res = g_ptr_array_new ();

  g_hash_table_iter_init (&iter, self->app_updates);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (value != NULL)
        g_ptr_array_add (res, g_strdup (key));
    }

  // TODO: can we use gvdb_table_list here???
  apps = gvdb_table_get_names (self->app_table, NULL);
  for (i = 0; apps[i] != NULL; i++)
    {
      char *app = apps[i];

      if (g_hash_table_lookup_extended (self->app_updates, app, NULL, NULL))
        g_free (app);
      else
        g_ptr_array_add (res, app);
    }

  g_ptr_array_add (res, NULL);
  return (char **)g_ptr_array_free (res, FALSE);
}

/* Transfer: full */
char **
xdg_app_db_list_ids_by_value (XdgAppDb *self,
                              GVariant *data)
{
  g_autofree char **ids = xdg_app_db_list_ids (self);
  int i;
  GPtrArray *res;

  res = g_ptr_array_new ();

  for (i = 0; ids[i] != NULL; i++)
    {
      char *id = ids[i];

      g_autoptr(XdgAppDbEntry) entry = NULL;
      g_autoptr(GVariant) entry_data = NULL;

      entry = xdg_app_db_lookup (self, id);
      if (entry)
        {
          entry_data = xdg_app_db_entry_get_data (entry);
          if (g_variant_equal (data, entry_data))
            {
              g_ptr_array_add (res, id);
              id = NULL; /* Don't free, as we return this */
            }
        }
      g_free (id);
    }

  g_ptr_array_add (res, NULL);
  return (char **)g_ptr_array_free (res, FALSE);
}

XdgAppDbEntry  *
xdg_app_db_entry_ref (XdgAppDbEntry  *entry)
{
  g_variant_ref ((GVariant *)entry);
  return entry;
}

void
xdg_app_db_entry_unref (XdgAppDbEntry  *entry)
{
  g_variant_unref ((GVariant *)entry);
}

/* Transfer: full */
XdgAppDbEntry *
xdg_app_db_lookup (XdgAppDb *self,
                   const char *id)
{
  GVariant *res;
  gpointer value;

  if (g_hash_table_lookup_extended (self->main_updates, id, NULL, &value))
    {
      if (value == NULL)
        return NULL;
      res = g_variant_ref ((GVariant *)value);
    }
  else
    {
      res = gvdb_table_get_value (self->main_table, id);
      if (res == NULL)
        return NULL;
    }

  return (XdgAppDbEntry *)res;
}

/* Transfer: full */
GVariant *
xdg_app_db_entry_get_data (XdgAppDbEntry  *entry)
{
  GVariant *v = (GVariant *)entry;

  return g_variant_get_child_value (v, 0);
}

/* Transfer: container */
const char **
xdg_app_db_entry_list_apps (XdgAppDbEntry  *entry)
{
  GVariant *v = (GVariant *)entry;
  g_autoptr(GVariant) app_array = NULL;
  GVariantIter iter;
  GVariant *child;
  GPtrArray *res;

  res = g_ptr_array_new ();

  app_array = g_variant_get_child_value (v, 1);

  g_variant_iter_init (&iter, app_array);
  while ((child = g_variant_iter_next_value (&iter)))
    {
      const char *child_app_id;

      g_variant_get_child (child, 0, "&s", &child_app_id);

      g_ptr_array_add (res, (char *)child_app_id);

      g_variant_unref (child);
    }

  g_ptr_array_add (res, NULL);
  return (const char **)g_ptr_array_free (res, FALSE);
}

static GVariant *
xdg_app_db_entry_get_permissions_variant (XdgAppDbEntry  *entry,
                                          const char *app_id)
{
  GVariant *v = (GVariant *)entry;
  g_autoptr(GVariant) app_array = NULL;
  GVariantIter iter;
  GVariant *child;
  GVariant *res = NULL;

  app_array = g_variant_get_child_value (v, 1);

  g_variant_iter_init (&iter, app_array);
  while (res == NULL &&
         (child = g_variant_iter_next_value (&iter)))
    {
      const char *child_app_id;

      g_variant_get_child (child, 0, "&s", &child_app_id);

      if (strcmp (app_id, child_app_id) == 0)
        res = g_variant_get_child_value (child, 1);

      g_variant_unref (child);
    }

  return res;
}


/* Transfer: container */
const char **
xdg_app_db_entry_list_permissions (XdgAppDbEntry *entry,
                                   const char *app)
{
  g_autoptr(GVariant) permissions = NULL;

  permissions = xdg_app_db_entry_get_permissions_variant (entry, app);
  if (permissions)
    return g_variant_get_strv (permissions, NULL);
  else
    return g_new0 (const char *, 1);
}

gboolean
xdg_app_db_entry_has_permission (XdgAppDbEntry  *entry,
                                 const char     *app,
                                 const char     *permission)
{
  g_autofree const char **app_permissions = NULL;

  app_permissions = xdg_app_db_entry_list_permissions (entry, app);

  return g_strv_contains (app_permissions, permission);
}

gboolean
xdg_app_db_entry_has_permissions (XdgAppDbEntry  *entry,
                                  const char     *app,
                                  const char    **permissions)
{
  g_autofree const char **app_permissions = NULL;
  int i;

  app_permissions = xdg_app_db_entry_list_permissions (entry, app);

  for (i = 0; permissions[i] != NULL; i++)
    {
      if (!g_strv_contains (app_permissions, permissions[i]))
        return FALSE;
    }

  return TRUE;
}
