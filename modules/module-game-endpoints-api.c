/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/keys.h>
#include <wp/wp.h>

struct _WpGameEndpointsApi
{
  WpPlugin parent;

  WpObjectManager *stream_nodes_om;
  GHashTable *cgroups;
  GHashTable *game_endpoints[2];
};

enum {
  ACTION_GET_GAME_ENDPOINT,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpGameEndpointsApi, wp_game_endpoints_api, WP,
    GAME_ENDPOINTS_API, WpPlugin)
G_DEFINE_TYPE (WpGameEndpointsApi, wp_game_endpoints_api, WP_TYPE_PLUGIN)

static void
wp_game_endpoints_api_init (WpGameEndpointsApi * self)
{
}

static WpSessionItem *
wp_game_endpoints_api_get_game_endpoint (WpGameEndpointsApi * self, int pid,
    const char *direction)
{
  WpDirection dir = WP_DIRECTION_INPUT;
  const gchar *cgroup;
  WpSessionItem *ep;

  cgroup = g_hash_table_lookup (self->cgroups, GINT_TO_POINTER (pid));
  if (!cgroup)
    return NULL;

  if (g_str_equal (direction, "output") || g_str_equal (direction, "Output"))
    dir = WP_DIRECTION_OUTPUT;

  ep = g_hash_table_lookup (self->game_endpoints[dir], cgroup);
  return ep ? g_object_ref (ep) : NULL;
}

static gboolean
is_steam_game (const gchar *cgroup)
{
  /* All steam games must have .scope suffix in their cgroup */
  if (!g_str_has_suffix (cgroup, ".scope"))
    return FALSE;

  /* Make sure this is not a non-Steam game */
  if (strstr (cgroup, "app-steam-unknown-") != NULL ||
      strstr (cgroup, "app-steam-app0-"))
    return FALSE;

  return strstr (cgroup, "app-steam-app") != NULL;
}

static void
on_steam_game_endpoint_activated (WpObject * si, GAsyncResult * res,
    WpSessionItem *self)
{
  g_autoptr (GError) error = NULL;
  const gchar *str;

  str = wp_session_item_get_property (WP_SESSION_ITEM (si), "name");

  /* Finish */
  if (!wp_object_activate_finish (si, res, &error)) {
    wp_warning_object (self,
        "Failed to activate Steam Game Endpoint for '%s': %s",
        str ? str : "unknown", error->message);
    return;
  }

  /* Register */
  wp_info_object (self,
      "Activated Steam Game Endpoint for '%s'", str ? str : "unknown");
  wp_session_item_register (WP_SESSION_ITEM (g_object_ref (si)));
}

static WpSessionItem *
create_stream_game_endpoint (WpGameEndpointsApi *self, const gchar *cgroup,
    WpDirection dir, const char *app_name)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpSessionItem) ep = NULL;
  g_autofree char *name = NULL;
  g_autofree char *role = NULL;
  const gchar *str;

  ep = wp_session_item_make (core, "si-audio-endpoint");
  if (!ep)
    return NULL;

  /* Build the node name based on cgroup and application name */
  str = strstr (cgroup, "app-steam-");
  if (!str)
    str = cgroup;
  name = g_strdup_printf ("%s (%s)", app_name, str);
  role = g_strdup_printf ("Endpoint for %s (%s)", app_name, str);

  /* Configure endpoint */
  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_set (props, "name", name);
    wp_properties_set (props, "role", role);
    wp_properties_set (props, "node.name", name);
    wp_properties_set (props, "node.description", role);
    wp_properties_set (props, "media.class",
        dir == WP_DIRECTION_INPUT ? "Audio/Source" : "Audio/Sink");
    wp_properties_set (props, "media.type", "Audio");
    wp_properties_set (props, "item.node.type", "device");
    wp_properties_set (props, "item.node.direction",
        dir == WP_DIRECTION_INPUT ? "output" : "input");
    wp_properties_setf (props, "node.autoconnect", "%u", TRUE);
    if (!wp_session_item_configure (ep, props))
      return FALSE;
  }

  return g_object_ref (ep);
}

static void
on_stream_node_added (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpGameEndpointsApi * self = WP_GAME_ENDPOINTS_API (d);
  const gchar* str;
  gint pid;
  WpDirection dir = WP_DIRECTION_INPUT;
  g_autofree gchar *path = NULL;
  g_autofree gchar *cgroup = NULL;

  /* Get node's PID */
  str = wp_pipewire_object_get_property (proxy, PW_KEY_APP_PROCESS_ID);
  if (!str)
    return;
  pid = atoi (str);
  if (pid < 0)
    return;

  /* Get the cgroup for this PID */
  path = g_strdup_printf ("/proc/%s/cgroup", str);
  if (!g_file_get_contents (path, &cgroup, NULL, NULL) || !cgroup)
    return;
  cgroup[strlen (cgroup) - 1] = '\0';  /* remove end of file character */

  /* Get node's direction */
  str = wp_pipewire_object_get_property (proxy, PW_KEY_MEDIA_CLASS);
  if (!str)
    return;
  if (g_str_equal (str, "Stream/Input/Audio"))
    dir = WP_DIRECTION_INPUT;
  else if (g_str_equal (str, "Stream/Output/Audio"))
    dir = WP_DIRECTION_OUTPUT;
  else {
    wp_warning_object (self, "Invalid media class %s", str);
    return;
  }

  /* Make sure the cgroup is a steam game */
  if (!is_steam_game (cgroup))
    return;

  /* Add cgroup for this PID */
  g_hash_table_insert (self->cgroups, GINT_TO_POINTER (pid), g_strdup (cgroup));

  /* Check if we need to create an endpoint for this cgroup */
  if (!g_hash_table_contains (self->game_endpoints[dir], cgroup)) {
    g_autoptr (WpSessionItem) ep = NULL;
    const char *app_name;

    /* Get application name */
    app_name = wp_pipewire_object_get_property (proxy, "application.name");
    if (!app_name)
      app_name = "Unknown";

    /* Create endpoint */
    ep = create_stream_game_endpoint (self, cgroup, dir, app_name);
    if (!ep) {
      wp_warning_object (self, "Failed to create Steam Game Endpoint for '%s'",
         cgroup);
      return;
    }

    /* Add endpoint */
    g_hash_table_insert (self->game_endpoints[dir], g_strdup (cgroup),
        g_object_ref (ep));
    wp_info_object (self, "Created Steam Game Endpoint for '%s'", cgroup);

    /* Activate endpoint and register it */
    wp_object_activate (WP_OBJECT (ep), WP_SESSION_ITEM_FEATURE_ACTIVE,
        NULL,  (GAsyncReadyCallback) on_steam_game_endpoint_activated, self);
  }
}

static gboolean
find_cgroup_func (gpointer key, gpointer value, gpointer data)
{
  const gchar *v = value;
  const gchar *cgroup = data;
  return v && cgroup && g_str_equal (v, cgroup);
}

static void
on_stream_node_removed (WpObjectManager *om, WpPipewireObject *proxy, gpointer d)
{
  WpGameEndpointsApi * self = WP_GAME_ENDPOINTS_API (d);

  const gchar* str;
  gint pid;
  WpDirection dir = WP_DIRECTION_INPUT;
  g_autofree gchar *cgroup = NULL;

  /* Get node's PID */
  str = wp_pipewire_object_get_property (proxy,
      PW_KEY_APP_PROCESS_ID);
  if (!str)
    return;
  pid = atoi (str);
  if (pid < 0)
    return;

  /* Get node's direction */
  str = wp_pipewire_object_get_property (proxy, PW_KEY_MEDIA_CLASS);
  if (!str)
    return;
  if (g_str_equal (str, "Stream/Input/Audio"))
    dir = WP_DIRECTION_INPUT;
  else if (g_str_equal (str, "Stream/Output/Audio"))
    dir = WP_DIRECTION_OUTPUT;
  else {
    wp_warning_object (self, "Invalid media class %s", str);
    return;
  }

  /* Get cgroup */
  str = g_hash_table_lookup (self->cgroups, GINT_TO_POINTER (pid));
  if (!str)
    return;
  cgroup = g_strdup (str);

  /* Remove cgroup */
  g_hash_table_remove (self->cgroups, GINT_TO_POINTER (pid));

  /* Remove the associated endpoint if this cgroup was the last one */
  if (g_hash_table_find (self->cgroups, find_cgroup_func, cgroup) == NULL) {
    g_hash_table_remove (self->game_endpoints[dir], cgroup);
    wp_info_object (self, "Removed Steam Game Endpoint for '%s'", cgroup);
  }
}

static void
game_endpoint_free (gpointer p)
{
  WpSessionItem *ep = p;
  wp_session_item_remove (ep);
  g_object_unref (ep);
}

static void
wp_game_endpoints_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpGameEndpointsApi * self = WP_GAME_ENDPOINTS_API (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  /* Init the hash tables */
  self->cgroups = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      g_free);
  for (guint i = 0; i < 2; i++)
    self->game_endpoints[i] = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, game_endpoint_free);

  /* Create the stream nodes object manager */
  self->stream_nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->stream_nodes_om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "#s", "Stream/*/Audio",
      NULL);
  wp_object_manager_request_object_features (self->stream_nodes_om,
      WP_TYPE_NODE, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->stream_nodes_om, "object-added",
      G_CALLBACK (on_stream_node_added), self, 0);
  g_signal_connect_object (self->stream_nodes_om, "object-removed",
      G_CALLBACK (on_stream_node_removed), self, 0);
  wp_core_install_object_manager (core, self->stream_nodes_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_game_endpoints_api_disable (WpPlugin * plugin)
{
  WpGameEndpointsApi * self = WP_GAME_ENDPOINTS_API (plugin);

  /* Clear hash tables */
  g_hash_table_remove_all (self->cgroups);
  for (guint i = 0; i < 2; i++)
    g_hash_table_remove_all (self->game_endpoints[i]);
}

static void
wp_game_endpoints_api_class_init (WpGameEndpointsApiClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_game_endpoints_api_enable;
  plugin_class->disable = wp_game_endpoints_api_disable;

  signals[ACTION_GET_GAME_ENDPOINT] = g_signal_new_class_handler (
      "get-game-endpoint", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_game_endpoints_api_get_game_endpoint,
      NULL, NULL, NULL,
      WP_TYPE_SESSION_ITEM, 2, G_TYPE_INT, G_TYPE_STRING);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_game_endpoints_api_get_type (),
          "name", "game-endpoints-api",
          "core", core,
          NULL));
  return TRUE;
}
