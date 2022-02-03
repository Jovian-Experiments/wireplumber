/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_LOGIN1_MANAGER_PLUGIN_H__
#define __WIREPLUMBER_LOGIN1_MANAGER_PLUGIN_H__

#include <wp/wp.h>

G_BEGIN_DECLS

typedef enum {
  WP_DBUS_PLUGIN_CONNECTION_STATUS_CLOSED = 0,
  WP_DBUS_PLUGIN_CONNECTION_STATUS_CONNECTING,
  WP_DBUS_PLUGIN_CONNECTION_STATUS_CONNECTED,
} WpDBusPluginConnectionStatus;

G_DECLARE_FINAL_TYPE (WpLogin1ManagerPlugin, wp_login1_manager_plugin, WP,
    LOGIN1_MANAGER_PLUGIN, WpPlugin)

struct _WpLogin1ManagerPlugin
{
  WpPlugin parent;

  WpDBusPluginConnectionStatus state;
  guint signal_id;

  GCancellable *cancellable;
  GDBusConnection *connection;
};

G_END_DECLS

#endif
