/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#define LOGIND_BUS_NAME "org.freedesktop.login1"
#define LOGIND_IFACE_NAME "org.freedesktop.login1.Manager"
#define LOGIND_OBJ_PATH "/org/freedesktop/login1"

enum
{
  ACTION_GET_DBUS,
  SIGNAL_PREPARE_FOR_SLEEP,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _WpLogin1ManagerPlugin
{
  WpPlugin parent;

  WpDbus *dbus;
  guint signal_id;
};

G_DECLARE_FINAL_TYPE (WpLogin1ManagerPlugin, wp_login1_manager_plugin, WP,
    LOGIN1_MANAGER_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpLogin1ManagerPlugin, wp_login1_manager_plugin,
    WP_TYPE_PLUGIN)

static gpointer
wp_login1_manager_plugin_get_dbus (WpLogin1ManagerPlugin *self)
{
  return self->dbus ? g_object_ref (self->dbus) : NULL;
}

static void
wp_login1_manager_plugin_prepare_for_sleep (GDBusConnection *connection,
    const gchar *sender_name, const gchar *object_path,
    const gchar *interface_name, const gchar *signal_name,
    GVariant *parameters, gpointer user_data)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (user_data);
  gboolean start = FALSE;

  g_return_if_fail (parameters);
  g_variant_get (parameters, "(b)", &start);

  g_signal_emit (self, signals[SIGNAL_PREPARE_FOR_SLEEP], 0, start);
}

static void
clear_signal (WpLogin1ManagerPlugin *self)
{
  g_autoptr (GDBusConnection) conn = NULL;

  conn = wp_dbus_get_connection (self->dbus);
  if (conn && self->signal_id > 0) {
    g_dbus_connection_signal_unsubscribe (conn, self->signal_id);
    self->signal_id = 0;
  }
}

static void
on_dbus_state_changed (GObject * obj, GParamSpec * spec,
    WpLogin1ManagerPlugin *self)
{
  WpDBusState state = wp_dbus_get_state (self->dbus);

  switch (state) {
    case WP_DBUS_STATE_CONNECTED: {
      g_autoptr (GDBusConnection) conn = NULL;

      conn = wp_dbus_get_connection (self->dbus);
      g_return_if_fail (conn);

      self->signal_id = g_dbus_connection_signal_subscribe (conn,
          LOGIND_BUS_NAME, LOGIND_IFACE_NAME, "PrepareForSleep",
          LOGIND_OBJ_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
          wp_login1_manager_plugin_prepare_for_sleep, self, NULL);
      break;
    }

    case WP_DBUS_STATE_CONNECTING:
    case WP_DBUS_STATE_CLOSED:
      clear_signal (self);
      break;

    default:
      break;
  }
}

static void
wp_login1_manager_plugin_init (WpLogin1ManagerPlugin * self)
{
}

static void
wp_login1_manager_plugin_constructed (GObject *object)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (object);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->dbus = wp_dbus_get_instance (core, G_BUS_TYPE_SYSTEM);
  g_signal_connect_object (self->dbus, "notify::state",
      G_CALLBACK (on_dbus_state_changed), self, 0);

  G_OBJECT_CLASS (wp_login1_manager_plugin_parent_class)->constructed (object);
}

static void
wp_login1_manager_plugin_finalize (GObject * object)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (object);

  g_clear_object (&self->dbus);

  G_OBJECT_CLASS (wp_login1_manager_plugin_parent_class)->finalize (object);
}

static void
on_dbus_activated (GObject * obj, GAsyncResult * res, gpointer user_data)
{
  WpTransition * transition = WP_TRANSITION (user_data);
  WpLogin1ManagerPlugin * self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (WP_OBJECT (obj), res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_login1_manager_plugin_enable (WpPlugin * plugin,
    WpTransition * transition)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (plugin);

  /* Make sure dbus is always activated */
  g_return_if_fail (self->dbus);
  wp_object_activate (WP_OBJECT (self->dbus), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_dbus_activated, transition);
}

static void
wp_login1_manager_plugin_disable (WpPlugin * plugin)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (plugin);

  clear_signal (self);

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static void
wp_login1_manager_plugin_class_init (
    WpLogin1ManagerPluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->constructed = wp_login1_manager_plugin_constructed;
  object_class->finalize = wp_login1_manager_plugin_finalize;

  plugin_class->enable = wp_login1_manager_plugin_enable;
  plugin_class->disable = wp_login1_manager_plugin_disable;

  /**
   * WpLogin1ManagerPlugin::get-dbus:
   *
   * Returns: (transfer full): the dbus object
   */
  signals[ACTION_GET_DBUS] = g_signal_new_class_handler (
      "get-dbus", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_login1_manager_plugin_get_dbus,
      NULL, NULL, NULL,
      G_TYPE_OBJECT, 0);

  /**
   * WpLogin1ManagerPlugin::changed:
   *
   * @brief
   * @em start: TRUE if going to sleep, FALSE if resuming
   *
   * Signaled when system suspends or resums
   */
  signals[SIGNAL_PREPARE_FOR_SLEEP] = g_signal_new (
      "prepare-for-sleep", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_login1_manager_plugin_get_type(),
      "name", "login1-manager",
      "core", core,
      NULL));
  return TRUE;
}
