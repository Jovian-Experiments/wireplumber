/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpState
 *
 * The #WpState class saves and loads properties from a file
 */

#define G_LOG_DOMAIN "wp-state"

#define WP_STATE_DIR_NAME "wireplumber"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "debug.h"
#include "state.h"

enum {
  PROP_0,
  PROP_NAME,
};

struct _WpState
{
  GObject parent;

  /* Props */
  gchar *name;

  gchar *location;
};

G_DEFINE_TYPE (WpState, wp_state, G_TYPE_OBJECT)

static gboolean
path_exists (const char *path)
{
  struct stat info;
  return stat (path, &info) == 0;
}

static char *
get_new_location (const char *name)
{
  g_autofree gchar *path = NULL;

  /* Get the config path */
  path = g_build_filename (g_get_user_config_dir (), WP_STATE_DIR_NAME, NULL);
  g_return_val_if_fail (path, NULL);

  /* Create the directory if it doesn't exist */
  if (!path_exists (path))
    g_mkdir_with_parents (path, 0700);

  return g_build_filename (path, name, NULL);
}

static void
wp_state_ensure_location (WpState *self)
{
  if (!self->location)
    self->location = get_new_location (self->name);
  g_return_if_fail (self->location);
}

static void
wp_state_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpState *self = WP_STATE (object);

  switch (property_id) {
  case PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_state_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpState *self = WP_STATE (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_state_finalize (GObject * object)
{
  WpState * self = WP_STATE (object);

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->location, g_free);

  G_OBJECT_CLASS (wp_state_parent_class)->finalize (object);
}

static void
wp_state_init (WpState * self)
{
}

static void
wp_state_class_init (WpStateClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_state_finalize;
  object_class->set_property = wp_state_set_property;
  object_class->get_property = wp_state_get_property;

  /**
   * WpState:name:
   * The file name where the state will be stored.
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name",
          "The file name where the state will be stored", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_state_new:
 * @name: the state name
 *
 * Returns: (transfer full): the new #WpState
 */
WpState *
wp_state_new (const gchar *name)
{
  g_return_val_if_fail (name, NULL);
  return g_object_new (wp_state_get_type (),
      "name", name,
      NULL);
}

/**
 * wp_state_get_name:
 * @self: the state
 *
 * Returns: the name of this state
 */
const gchar *
wp_state_get_name (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);

  return self->name;
}

/**
 * wp_state_get_location:
 * @self: the state
 *
 * Returns: the location of this state
 */
const gchar *
wp_state_get_location (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);
  wp_state_ensure_location (self);

  return self->location;
}

/**
 * wp_state_clear:
 * @self: the state
 *
 * Clears the state removing its file
 */
void
wp_state_clear (WpState *self)
{
  g_return_if_fail (WP_IS_STATE (self));
  wp_state_ensure_location (self);

  if (path_exists (self->location))
    remove (self->location);
}

/**
 * wp_state_save:
 * @self: the state
 * @props: (transfer none): the properties to save
 *
 * Saves new properties in the state, overwriting all previous data.
 *
 * Returns: TRUE if the properties could be saved, FALSE otherwise
 */
gboolean
wp_state_save (WpState *self, WpProperties *props)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autofree gchar *tmp_name = NULL, *tmp_location = NULL;
  gulong tmp_size;
  int fd;
  FILE *f;

  g_return_val_if_fail (WP_IS_STATE (self), FALSE);
  wp_state_ensure_location (self);

  wp_info_object (self, "saving state into %s", self->location);

  /* Get the temporary name and location */
  tmp_size = strlen (self->name) + 5;
  tmp_name = g_malloc (tmp_size);
  g_snprintf (tmp_name, tmp_size, "%s.tmp", self->name);
  tmp_location = get_new_location (tmp_name);
  g_return_val_if_fail (tmp_location, FALSE);

  /* Open */
  fd = open (tmp_location, O_CLOEXEC | O_CREAT | O_WRONLY | O_TRUNC, 0700);
  if (fd == -1) {
    wp_critical_object (self, "can't open %s", tmp_location);
    return FALSE;
  }

  /* Write */
  f = fdopen(fd, "w");
  for (it = wp_properties_iterate (props);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    const gchar *p = wp_properties_iterator_item_get_key (&item);
    while (*p) {
      if (*p == ' ' || *p == '\\')
        fputc('\\', f);
      fprintf(f, "%c", *p++);
    }
    fprintf(f, " %s\n", wp_properties_iterator_item_get_value (&item));
  }
  fclose(f);

  /* Rename temporary file */
  if (rename(tmp_location, self->location) < 0) {
    wp_critical_object("can't rename temporary file '%s' to '%s'", tmp_name,
        self->name);
    return FALSE;
  }

  return TRUE;
}

/**
 * wp_state_load:
 * @self: the state
 *
 * Loads the state data into new properties.
 *
 * Returns (transfer full): the new properties with the state data
 */
WpProperties *
wp_state_load (WpState *self)
{
  g_autoptr (WpProperties) props = wp_properties_new_empty ();
  int fd;
  FILE *f;
  char line[1024];

  g_return_val_if_fail (WP_IS_STATE (self), NULL);
  wp_state_ensure_location (self);

  /* Open */
  wp_info_object (self, "loading state from %s", self->location);
  fd = open (self->location, O_CLOEXEC | O_RDONLY);
  if (fd == -1) {
    /* We consider empty state if fill does not exist */
    if (errno == ENOENT)
      return g_steal_pointer (&props);
    wp_critical_object (self, "can't open %s", self->location);
    return NULL;
  }

  /* Read */
  f = fdopen(fd, "r");
  while (fgets (line, sizeof(line)-1, f)) {
    char *val, *key, *k, *p;
    val = strrchr(line, '\n');
    if (val)
      *val = '\0';

    key = k = p = line;
    while (*p) {
      if (*p == ' ')
	break;
      if (*p == '\\')
	p++;
      *k++ = *p++;
    }
    *k = '\0';
    val = ++p;
    wp_properties_set (props, key, val);
  }
  fclose(f);

  return g_steal_pointer (&props);
}