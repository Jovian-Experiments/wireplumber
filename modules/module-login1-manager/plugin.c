/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "plugin.h"
#include "login1-manager-enums.h"

#define LOGIND_BUS_NAME "org.freedesktop.login1"
#define LOGIND_IFACE_NAME "org.freedesktop.login1.Manager"
#define LOGIND_OBJ_PATH "/org/freedesktop/login1"

static void setup_connection (WpLogin1ManagerPlugin *self);

G_DEFINE_TYPE (WpLogin1ManagerPlugin, wp_login1_manager_plugin, WP_TYPE_PLUGIN)

enum
{
  SIGNAL_PREPARE_FOR_SLEEP,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_STATE,
};

static guint signals[LAST_SIGNAL] = { 0 };

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
wp_login1_manager_plugin_init (WpLogin1ManagerPlugin * self)
{
  self->cancellable = g_cancellable_new ();
}

static void
wp_login1_manager_plugin_finalize (GObject * object)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (object);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (wp_login1_manager_plugin_parent_class)->finalize (object);
}

static void
clear_connection (WpLogin1ManagerPlugin *self)
{
  if (self->connection && self->signal_id > 0)
    g_dbus_connection_signal_unsubscribe (self->connection, self->signal_id);
  g_clear_object (&self->connection);

  if (self->state != WP_DBUS_PLUGIN_CONNECTION_STATUS_CLOSED) {
    self->state = WP_DBUS_PLUGIN_CONNECTION_STATUS_CLOSED;
    g_object_notify (G_OBJECT (self), "state");
  }
}

static gboolean
do_connect (WpLogin1ManagerPlugin *self, GAsyncReadyCallback callback,
    gpointer data, GError **error)
{
  g_autofree gchar *address = NULL;

  address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SYSTEM, NULL, error);
  if (!address) {
    g_prefix_error (error, "Error acquiring session bus address: ");
    return FALSE;
  }

  wp_debug_object (self, "Connecting to bus: %s", address);

  self->state = WP_DBUS_PLUGIN_CONNECTION_STATUS_CONNECTING;
  g_object_notify (G_OBJECT (self), "state");

  g_dbus_connection_new_for_address (address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, self->cancellable, callback, data);
  return TRUE;
}

static void
on_reconnect_got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (data);
  g_autoptr (GError) error = NULL;

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    clear_connection (self);
    wp_info_object (self, "Could not reconnect to session bus: %s",
        error->message);
    return;
  }

  wp_debug_object (self, "Reconnected to bus");
  setup_connection (self);
}

static gboolean
idle_connect (WpLogin1ManagerPlugin * self)
{
  g_autoptr (GError) error = NULL;

  if (!do_connect (self, on_reconnect_got_bus, self, &error))
    wp_info_object (self, "Cannot reconnect: %s", error->message);

  return G_SOURCE_REMOVE;
}

static void
on_connection_closed (GDBusConnection *connection,
    gboolean remote_peer_vanished, GError *error, gpointer data)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (data);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  wp_info_object (self, "D-Bus connection closed: %s", error->message);

  clear_connection (self);

  /* try to reconnect on idle if connection was closed */
  if (core)
    wp_core_idle_add_closure (core, NULL, g_cclosure_new_object (
        G_CALLBACK (idle_connect), G_OBJECT (self)));
}

static void
setup_connection (WpLogin1ManagerPlugin *self)
{
  g_signal_connect_object (self->connection, "closed",
      G_CALLBACK (on_connection_closed), self, 0);
  g_dbus_connection_set_exit_on_close (self->connection, FALSE);

  /* Listen for the PrepareForSleep signal */
  self->signal_id = g_dbus_connection_signal_subscribe (self->connection,
      LOGIND_BUS_NAME,
      LOGIND_IFACE_NAME,
      "PrepareForSleep",
      LOGIND_OBJ_PATH,
      NULL,
      G_DBUS_SIGNAL_FLAGS_NONE,
      wp_login1_manager_plugin_prepare_for_sleep,
      self,
      NULL);

  self->state = WP_DBUS_PLUGIN_CONNECTION_STATUS_CONNECTED;
  g_object_notify (G_OBJECT (self), "state");
}

static void
on_enable_got_bus (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpLogin1ManagerPlugin *self =
      wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  self->connection = g_dbus_connection_new_for_address_finish (res, &error);
  if (!self->connection) {
    clear_connection (self);
    g_prefix_error (&error, "Failed to connect to session bus: ");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_debug_object (self, "Connected to bus");
  setup_connection (self);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_login1_manager_plugin_enable (WpPlugin * plugin,
    WpTransition * transition)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (plugin);
  g_autoptr (GError) error = NULL;

  g_return_if_fail (self->state == WP_DBUS_PLUGIN_CONNECTION_STATUS_CLOSED);

  if (!do_connect (self, on_enable_got_bus, transition, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }
}

static void
wp_login1_manager_plugin_disable (WpPlugin * plugin)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (plugin);

  g_cancellable_cancel (self->cancellable);
  clear_connection (self);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static void
wp_login1_manager_plugin_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpLogin1ManagerPlugin *self = WP_LOGIN1_MANAGER_PLUGIN (object);

  switch (property_id) {
  case PROP_STATE:
    g_value_set_enum (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_login1_manager_plugin_class_init (WpLogin1ManagerPluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_login1_manager_plugin_finalize;
  object_class->get_property = wp_login1_manager_plugin_get_property;

  plugin_class->enable = wp_login1_manager_plugin_enable;
  plugin_class->disable = wp_login1_manager_plugin_disable;

  g_object_class_install_property (object_class, PROP_STATE,
      g_param_spec_enum ("state", "state", "The state",
          WP_TYPE_DBUS_PLUGIN_CONNECTION_STATUS,
          WP_DBUS_PLUGIN_CONNECTION_STATUS_CLOSED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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
