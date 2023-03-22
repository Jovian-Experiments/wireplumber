/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/keys.h>

#include <wp/wp.h>

struct _WpFiltersApi
{
  WpPlugin parent;

  WpObjectManager *metadata_om;
  WpObjectManager *nodes_om;
  WpObjectManager *filter_nodes_om;
  GList *filters[2];
  GHashTable *groups_target;
};

enum {
  ACTION_IS_FILTER_ENABLED,
  ACTION_GET_FILTER_TARGET,
  ACTION_GET_FILTER_FROM_TARGET,
  ACTION_GET_DEFAULT_FILTER,
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpFiltersApi, wp_filters_api, WP, FILTERS_API, WpPlugin)
G_DEFINE_TYPE (WpFiltersApi, wp_filters_api, WP_TYPE_PLUGIN)

struct _Filter {
  gchar *link_group;
  WpDirection direction;
  WpNode *node;
  WpNode *stream;
  gchar *group;
  gboolean enabled;
  gint priority;
};
typedef struct _Filter Filter;

static guint
get_filter_priority (const gchar *link_group)
{
  if (strstr (link_group, "loopback"))
    return 300;
  if (strstr (link_group, "filter-chain"))
    return 200;
  /* By default echo-cancel is the lowest priority to properly cancel audio */
  if (strstr (link_group, "echo-cancel"))
    return 0;
  return 100;
}

static Filter *
filter_new (const gchar *link_group, WpDirection dir, gboolean is_stream,
    WpNode *node)
{
  Filter *f = g_slice_new0 (Filter);
  f->link_group = g_strdup (link_group);
  f->direction = dir;
  f->node = is_stream ? NULL : g_object_ref (node);
  f->stream = is_stream ? g_object_ref (node) : NULL;
  f->group = NULL;
  f->enabled = TRUE;
  f->priority = get_filter_priority (link_group);
  return f;
}

static void
filter_free (Filter *f)
{
  g_clear_pointer (&f->link_group, g_free);
  g_clear_pointer (&f->group, g_free);
  g_clear_object (&f->node);
  g_clear_object (&f->stream);
  g_slice_free (Filter, f);
}

static gint
filter_equal_func (const Filter *f, const gchar *link_group)
{
  return g_str_equal (f->link_group, link_group) ? 0 : 1;
}

static gint
filter_compare_func (const Filter *a, const Filter *b)
{
  gint diff = a->priority - b->priority;
  if (diff != 0)
    return diff;
  return g_strcmp0 (a->link_group, b->link_group);
}

static void
wp_filters_api_init (WpFiltersApi * self)
{
}

static gboolean
wp_filters_api_is_filter_enabled (WpFiltersApi * self, const gchar *direction,
    const gchar *link_group)
{
  WpDirection dir = WP_DIRECTION_INPUT;
  GList *filters;
  Filter *found = NULL;

  g_return_val_if_fail (direction, FALSE);
  g_return_val_if_fail (link_group, FALSE);

  /* Get the filters for the given direction */
  if (g_str_equal (direction, "output") || g_str_equal (direction, "Output"))
    dir = WP_DIRECTION_OUTPUT;
  filters = self->filters[dir];

  /* Find the filter in the filters list */
  filters = g_list_find_custom (filters, link_group,
      (GCompareFunc) filter_equal_func);
  if (!filters)
    return FALSE;

  found = filters->data;
  return found->enabled;
}

static gint
wp_filters_api_get_filter_target (WpFiltersApi * self, const gchar *direction,
    const gchar *link_group)
{
  WpDirection dir = WP_DIRECTION_INPUT;
  GList *filters;
  Filter *found;

  g_return_val_if_fail (direction, -1);
  g_return_val_if_fail (link_group, -1);

  /* Get the filters for the given direction */
  if (g_str_equal (direction, "output") || g_str_equal (direction, "Output"))
    dir = WP_DIRECTION_OUTPUT;
  filters = self->filters[dir];

  /* Find the filter in the filters list */
  filters = g_list_find_custom (filters, link_group,
      (GCompareFunc) filter_equal_func);
  if (!filters)
    return -1;
  found = filters->data;
  if (!found->enabled)
    return -1;

  /* Return the previous filter with matching target that is enabled */
  while ((filters = g_list_previous (filters))) {
    Filter *prev = (Filter *) filters->data;
    if ((prev->group == found->group ||
        (prev->group && found->group && g_str_equal (prev->group, found->group))) &&
        prev->enabled)
      return wp_proxy_get_bound_id (WP_PROXY (prev->node));
  }

  /* Find the target */
  if (found->group) {
    WpNode *node = g_hash_table_lookup (self->groups_target, found->group);
    if (node)
      return wp_proxy_get_bound_id (WP_PROXY (node));
  }

  return -1;
}

static gint
wp_filters_api_get_filter_from_target (WpFiltersApi * self,
    const gchar *direction, gint target_id)
{
  WpDirection dir = WP_DIRECTION_INPUT;
  GList *filters;
  gboolean found = FALSE;
  const gchar *group = NULL;
  gint res = target_id;

  g_return_val_if_fail (direction, res);

  /* Get the filters for the given direction */
  if (g_str_equal (direction, "output") || g_str_equal (direction, "Output"))
    dir = WP_DIRECTION_OUTPUT;
  filters = self->filters[dir];

  /* Find the group matching target_id */
  while (filters) {
    Filter *f = (Filter *) filters->data;
    gint f_target_id = wp_filters_api_get_filter_target (self, direction,
        f->link_group);
    if (f_target_id == target_id && f->enabled) {
      group = f->group;
      found = TRUE;
      break;
    }

    /* Advance */
    filters = g_list_next (filters);
  }

  /* Just return if group was not found */
  if (!found)
    return res;

  /* Get the last filter node ID of the group found */
  filters = self->filters[dir];
  while (filters) {
    Filter *f = (Filter *) filters->data;
    if ((f->group == group ||
        (f->group && group && g_str_equal (f->group, group))) &&
        f->enabled)
      res = wp_proxy_get_bound_id (WP_PROXY (f->node));

    /* Advance */
    filters = g_list_next (filters);
  }

  return res;
}

static gint
wp_filters_api_get_default_filter (WpFiltersApi * self, const gchar *direction)
{
  WpDirection dir = WP_DIRECTION_INPUT;
  GList *filters;

  g_return_val_if_fail (direction, -1);

  /* Get the filters for the given direction */
  if (g_str_equal (direction, "output") || g_str_equal (direction, "Output"))
    dir = WP_DIRECTION_OUTPUT;
  filters = self->filters[dir];

  /* The default filter is the highest priority filter without group, this is
   * the first filer that is enabled because the list is sorted by priority */
  while (filters) {
    Filter *f = (Filter *) filters->data;
    if (f->enabled && !f->group)
      return wp_proxy_get_bound_id (WP_PROXY (f->node));

    /* Advance */
    filters = g_list_next (filters);
  }

  return -1;
}

static void
sync_changed (WpCore * core, GAsyncResult * res, WpFiltersApi * self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static WpNode *
find_group_target_node (WpFiltersApi *self, WpSpaJson *props_json)
{
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (WpObjectInterest) interest = NULL;

  /* Make sure the properties are a JSON object */
  if (!props_json || !wp_spa_json_is_object (props_json)) {
    wp_warning_object (self,
        "Group target properties must be a JSON object");
    return NULL;
  }

  /* Create the object intereset with the group properties */
  interest = wp_object_interest_new (WP_TYPE_NODE, NULL);
  it = wp_spa_json_new_iterator (props_json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *j = g_value_get_boxed (&item);
    g_autofree gchar *key = NULL;
    WpSpaJson *value_json;
    g_autofree gchar *value = NULL;

    key = wp_spa_json_parse_string (j);
    g_value_unset (&item);
    if (!wp_iterator_next (it, &item)) {
      wp_warning_object (self,
        "Could not get valid key-value pairs from groups-target properties");
      break;
    }
    value_json = g_value_get_boxed (&item);
    value = wp_spa_json_parse_string (value_json);
    if (!value) {
      wp_warning_object (self,
        "Could not get '%s' value from groups-target properties", key);
      break;
    }

    wp_object_interest_add_constraint (interest, WP_CONSTRAINT_TYPE_PW_PROPERTY,
        key, WP_CONSTRAINT_VERB_MATCHES, g_variant_new_string (value));
  }

  return wp_object_manager_lookup_full (self->nodes_om,
      wp_object_interest_ref (interest));
}

static void
reevaluate_groups_target (WpFiltersApi *self)
{
  g_autoptr (WpMetadata) m = NULL;
  const gchar *json_str;
  g_autoptr (WpSpaJson) json = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpIterator) it = NULL;

  g_hash_table_remove_all (self->groups_target);

  /* Make sure the metadata exists */
  m = wp_object_manager_lookup (self->metadata_om, WP_TYPE_METADATA, NULL);
  if (!m)
    return;

  /* Don't update anything if the metadata value is not set */
  json_str = wp_metadata_find (m, 0, "filters.configured.groups-target", NULL);
  if (!json_str)
    return;

  /* Make sure the metadata value is an object */
  json = wp_spa_json_new_from_string (json_str);
  if (!json || !wp_spa_json_is_object (json)) {
    wp_warning_object (self,
        "ignoring metadata value as it is not a JSON object: %s", json_str);
    return;
  }

  /* Find the target node for each group, and add it to the hash table */
  it = wp_spa_json_new_iterator (json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *j = g_value_get_boxed (&item);
    g_autofree gchar *key = NULL;
    WpSpaJson *props;
    g_autoptr (WpNode) target = NULL;

    key = wp_spa_json_parse_string (j);
    g_value_unset (&item);
    if (!wp_iterator_next (it, &item)) {
      wp_warning_object (self,
        "Could not get valid key-value pairs from groups-target object");
      break;
    }
    props = g_value_get_boxed (&item);

    /* Find the node and insert it into the table if found */
    target = find_group_target_node (self, props);
    if (target)
      g_hash_table_insert (self->groups_target, g_strdup (key),
          g_steal_pointer (&target));
  }
}

static void
update_values_from_metadata (WpFiltersApi * self, Filter *f)
{
  static const gchar * metadata_keys[2] = {
    [WP_DIRECTION_INPUT] = "filters.configured.audio.sink",
    [WP_DIRECTION_OUTPUT] = "filters.configured.audio.source",
  };
  g_autoptr (WpMetadata) m = NULL;
  const gchar *f_stream_name;
  const gchar *f_node_name;
  const gchar *json_str;
  g_autoptr (WpSpaJson) json = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpIterator) it = NULL;

  /* Make sure the metadata exists */
  m = wp_object_manager_lookup (self->metadata_om, WP_TYPE_METADATA, NULL);
  if (!m)
    return;

  /* Make sure both the stream and node are available */
  if (!f->stream || !f->node)
    return;
  f_stream_name = wp_pipewire_object_get_property (
      WP_PIPEWIRE_OBJECT (f->stream), PW_KEY_NODE_NAME);
  f_node_name = wp_pipewire_object_get_property (
      WP_PIPEWIRE_OBJECT (f->node), PW_KEY_NODE_NAME);

  /* Don't update anything if the metadata value is not set */
  json_str = wp_metadata_find (m, 0, metadata_keys[f->direction], NULL);
  if (!json_str)
    return;

  /* Make sure the metadata value is an array */
  json = wp_spa_json_new_from_string (json_str);
  if (!json || !wp_spa_json_is_array (json)) {
    wp_warning_object (self,
        "ignoring metadata value as it is not a JSON array: %s", json_str);
    return;
  }

  /* Find the filter values in the metadata */
  it = wp_spa_json_new_iterator (json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *j = g_value_get_boxed (&item);
    g_autofree gchar *stream_name = NULL;
    g_autofree gchar *node_name = NULL;
    g_autofree gchar *group = NULL;
    gboolean enabled;
    gint priority;

    if (!j || !wp_spa_json_is_object (j))
      continue;

    /* Parse mandatory fields */
    if (!wp_spa_json_object_get (j, "stream-name", "s", &stream_name,
        "node-name", "s", &node_name, NULL)) {
      g_autofree gchar *str = wp_spa_json_to_string (j);
      wp_warning_object (self,
          "failed to parse stream-name and node-name in metadata filter: %s",
          str);
      continue;
    }

    /* Find first metadata filter matching both stream-name and node-name */
    if (g_str_equal (f_stream_name, stream_name) &&
        g_str_equal (f_node_name, node_name)) {
      /* Update values if they exist */
      if (wp_spa_json_object_get (j, "group", "s", &group, NULL)) {
        g_clear_pointer (&f->group, g_free);
        f->group = g_strdup (group);
      }
      if (wp_spa_json_object_get (j, "enabled", "b", &enabled, NULL))
        f->enabled = enabled;
      if (wp_spa_json_object_get (j, "priority", "i", &priority, NULL))
        f->priority = priority;
      break;
    }
  }
}

static void
reevaluate_filters (WpFiltersApi *self, WpDirection direction)
{
  /* Update filter values */
  GList *filters = self->filters[direction];
  while (filters) {
    Filter *f = (Filter *) filters->data;
    update_values_from_metadata (self, f);
    filters = g_list_next (filters);
  }

  /* Sort filters */
  self->filters[direction] = g_list_sort (self->filters[direction],
      (GCompareFunc) filter_compare_func);
}

static void
schedule_changed (WpFiltersApi * self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Reevaluate */
  reevaluate_groups_target (self);
  for (guint i = 0; i < 2; i++)
    reevaluate_filters (self, i);

  wp_core_sync_closure (core, NULL, g_cclosure_new_object (
      G_CALLBACK (sync_changed), G_OBJECT (self)));
}

static void
on_node_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpFiltersApi * self = WP_FILTERS_API (d);
  const gchar *key;
  WpDirection dir;
  gboolean is_stream;
  GList *found;

  /* Get direction */
  key = wp_pipewire_object_get_property (proxy, PW_KEY_MEDIA_CLASS);
  if (!key)
    return;

  if (g_str_equal (key, "Audio/Sink") ||
      g_str_equal (key, "Stream/Output/Audio")) {
    dir = WP_DIRECTION_INPUT;
  } else if (g_str_equal (key, "Audio/Source") ||
      g_str_equal (key, "Stream/Input/Audio")) {
    dir = WP_DIRECTION_OUTPUT;
  } else {
    wp_debug_object (self, "ignoring node with media class: %s", key);
    return;
  }

  /* Check whether the proxy is a stream or not */
  is_stream = FALSE;
  if (g_str_equal (key, "Stream/Output/Audio") ||
      g_str_equal (key, "Stream/Input/Audio"))
    is_stream = TRUE;

  /* We use the link group as filter name */
  key = wp_pipewire_object_get_property (proxy, PW_KEY_NODE_LINK_GROUP);
  if (!key) {
    wp_debug_object (self, "ignoring node without link group");
    return;
  }

  /* Check if the filter already exists, and add it if it does not exist */
  found = g_list_find_custom (self->filters[dir], key,
      (GCompareFunc) filter_equal_func);
  if (!found) {
    Filter *f = filter_new (key, dir, is_stream, WP_NODE (proxy));
    update_values_from_metadata (self, f);
    self->filters[dir] = g_list_insert_sorted (self->filters[dir],
        f, (GCompareFunc) filter_compare_func);
  } else {
    Filter *f = found->data;
    if (is_stream) {
      g_clear_object (&f->stream);
      f->stream = g_object_ref (WP_NODE (proxy));
    } else {
      g_clear_object (&f->node);
      f->node = g_object_ref (WP_NODE (proxy));
    }
    update_values_from_metadata (self, f);
    self->filters[dir] = g_list_sort (self->filters[dir],
        (GCompareFunc) filter_compare_func);
  }
}

static void
on_node_removed (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpFiltersApi * self = WP_FILTERS_API (d);

  const gchar *key;
  WpDirection dir;
  GList *found;

  /* Get direction */
  key = wp_pipewire_object_get_property (proxy, PW_KEY_MEDIA_CLASS);
  if (!key)
    return;

  if (g_str_equal (key, "Audio/Sink") ||
      g_str_equal (key, "Stream/Output/Audio")) {
    dir = WP_DIRECTION_INPUT;
  } else if (g_str_equal (key, "Audio/Source") ||
      g_str_equal (key, "Stream/Input/Audio")) {
    dir = WP_DIRECTION_OUTPUT;
  } else {
    wp_debug_object (self, "ignoring node with media class: %s", key);
    return;
  }

  /* We use the link group as filter name */
  key = wp_pipewire_object_get_property (proxy, PW_KEY_NODE_LINK_GROUP);
  if (!key) {
    wp_debug_object (self, "ignoring node without link group");
    return;
  }

  /* Find and remove the filter */
  found = g_list_find_custom (self->filters[dir], key,
      (GCompareFunc) filter_equal_func);
  if (found) {
    self->filters[dir] = g_list_remove (self->filters[dir], found->data);
  }
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpFiltersApi * self = WP_FILTERS_API (d);
  schedule_changed (self);
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpFiltersApi * self = WP_FILTERS_API (d);

  /* Handle the changed signal */
  g_signal_connect_object (metadata, "changed",
      G_CALLBACK (on_metadata_changed), self, 0);

  schedule_changed (self);
}

static void
on_metadata_installed (WpObjectManager * om, WpFiltersApi * self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  /* Create the nodes object manager */
  self->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->nodes_om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_LINK_GROUP, "-",
      NULL);
  wp_object_manager_request_object_features (self->nodes_om,
      WP_TYPE_NODE, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->nodes_om, "objects-changed",
      G_CALLBACK (schedule_changed), self, G_CONNECT_SWAPPED);
  wp_core_install_object_manager (core, self->nodes_om);

  /* Create the filter nodes object manager */
  self->filter_nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->filter_nodes_om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_LINK_GROUP, "+",
      NULL);
  wp_object_manager_request_object_features (self->filter_nodes_om,
      WP_TYPE_NODE, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->filter_nodes_om, "object-added",
      G_CALLBACK (on_node_added), self, 0);
  g_signal_connect_object (self->filter_nodes_om, "object-removed",
      G_CALLBACK (on_node_removed), self, 0);
  g_signal_connect_object (self->filter_nodes_om, "objects-changed",
      G_CALLBACK (schedule_changed), self, G_CONNECT_SWAPPED);
  wp_core_install_object_manager (core, self->filter_nodes_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_filters_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpFiltersApi * self = WP_FILTERS_API (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->groups_target = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);

  /* Create the metadata object manager */
  self->metadata_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "filters",
      NULL);
  wp_object_manager_request_object_features (self->metadata_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->metadata_om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  g_signal_connect_object (self->metadata_om, "installed",
      G_CALLBACK (on_metadata_installed), self, 0);
  wp_core_install_object_manager (core, self->metadata_om);
}

static void
wp_filters_api_disable (WpPlugin * plugin)
{
  WpFiltersApi * self = WP_FILTERS_API (plugin);

  for (guint i = 0; i < 2; i++) {
    if (self->filters[i]) {
      g_list_free_full (self->filters[i], (GDestroyNotify) filter_free);
      self->filters[i] = NULL;
    }
  }
  g_clear_pointer (&self->groups_target, g_hash_table_unref);

  g_clear_object (&self->metadata_om);
  g_clear_object (&self->nodes_om);
  g_clear_object (&self->filter_nodes_om);
}

static void
wp_filters_api_class_init (WpFiltersApiClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_filters_api_enable;
  plugin_class->disable = wp_filters_api_disable;

  signals[ACTION_IS_FILTER_ENABLED] = g_signal_new_class_handler (
      "is-filter-enabled", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_filters_api_is_filter_enabled,
      NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[ACTION_GET_FILTER_TARGET] = g_signal_new_class_handler (
      "get-filter-target", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_filters_api_get_filter_target,
      NULL, NULL, NULL,
      G_TYPE_INT, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[ACTION_GET_FILTER_TARGET] = g_signal_new_class_handler (
      "get-filter-from-target", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_filters_api_get_filter_from_target,
      NULL, NULL, NULL,
      G_TYPE_INT, 2, G_TYPE_STRING, G_TYPE_INT);

  signals[ACTION_GET_DEFAULT_FILTER] = g_signal_new_class_handler (
      "get-default-filter", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_filters_api_get_default_filter,
      NULL, NULL, NULL,
      G_TYPE_INT, 1, G_TYPE_STRING);

  signals[SIGNAL_CHANGED] = g_signal_new (
      "changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_filters_api_get_type (),
      "name", "filters-api",
      "core", core,
      NULL));
  return TRUE;
}