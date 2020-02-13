/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_H__
#define __WIREPLUMBER_PRIVATE_H__

#include "core.h"
#include "object-manager.h"
#include "proxy.h"

#include <stdint.h>
#include <pipewire/pipewire.h>

G_BEGIN_DECLS

/* registry */

typedef struct _WpRegistry WpRegistry;
struct _WpRegistry
{
  struct pw_registry *pw_registry;
  struct spa_hook listener;

  GPtrArray *globals; // elementy-type: WpGlobal*
  GPtrArray *objects; // element-type: GObject*
  GPtrArray *object_managers; // element-type: WpObjectManager*
};

void wp_registry_init (WpRegistry *self);
void wp_registry_clear (WpRegistry *self);
void wp_registry_attach (WpRegistry *self, struct pw_core *pw_core);
void wp_registry_detach (WpRegistry *self);

/* core */

struct _WpCore
{
  GObject parent;

  /* main loop integration */
  GMainContext *context;

  /* extra properties */
  WpProperties *properties;

  /* pipewire main objects */
  struct pw_context *pw_context;
  struct pw_core *pw_core;

  /* pipewire main listeners */
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;

  WpRegistry registry;
  GHashTable *async_tasks; // <int seq, GTask*>
};

gpointer wp_core_find_object (WpCore * self, GEqualFunc func,
    gconstpointer data);
void wp_core_register_object (WpCore * self, gpointer obj);
void wp_core_remove_object (WpCore * self, gpointer obj);

/* global */

typedef enum {
  WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY = 0x1,
  WP_GLOBAL_FLAG_OWNED_BY_PROXY = 0x2,
} WpGlobalFlags;

typedef struct _WpGlobal WpGlobal;
struct _WpGlobal
{
  guint32 flags;
  guint32 id;
  GType type;
  guint32 permissions;
  WpProperties *properties;
  WpProxy *proxy;
  WpRegistry *registry;
};

#define WP_TYPE_GLOBAL (wp_global_get_type ())
GType wp_global_get_type (void);

static inline void
wp_global_clear (WpGlobal * self)
{
  g_clear_pointer (&self->properties, wp_properties_unref);
}

static inline WpGlobal *
wp_global_ref (WpGlobal * self)
{
  return g_rc_box_acquire (self);
}

static inline void
wp_global_unref (WpGlobal * self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_global_clear);
}

WpGlobal * wp_global_new (WpRegistry * reg, guint32 id, guint32 permissions,
    GType type, WpProperties * properties, WpProxy * proxy, guint32 flags);
void wp_global_rm_flag (WpGlobal *global, guint rm_flag);

struct pw_proxy * wp_global_bind (WpGlobal * global);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpGlobal, wp_global_unref)

/* proxy */

void wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy);

void wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature);
void wp_proxy_augment_error (WpProxy * self, GError * error);

void wp_proxy_handle_event_param (void * proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param);

/* spa props */

struct spa_pod;
struct spa_pod_builder;

typedef struct _WpSpaProps WpSpaProps;
struct _WpSpaProps
{
  GList *entries;
};

void wp_spa_props_clear (WpSpaProps * self);

void wp_spa_props_register_pod (WpSpaProps * self,
    guint32 id, const gchar *name, const struct spa_pod *type);
gint wp_spa_props_register_from_prop_info (WpSpaProps * self,
    const struct spa_pod * prop_info);

const struct spa_pod * wp_spa_props_get_stored (WpSpaProps * self, guint32 id);

gint wp_spa_props_store_pod (WpSpaProps * self, guint32 id,
    const struct spa_pod * value);
gint wp_spa_props_store_from_props (WpSpaProps * self,
    const struct spa_pod * props, GArray * changed_ids);

GPtrArray * wp_spa_props_build_all_pods (WpSpaProps * self,
    struct spa_pod_builder * b);
struct spa_pod * wp_spa_props_build_update (WpSpaProps * self, guint32 id,
    const struct spa_pod * value, struct spa_pod_builder * b);

const struct spa_pod * wp_spa_props_build_pod_valist (gchar * buffer,
    gsize size, va_list args);

static inline const struct spa_pod *
wp_spa_props_build_pod (gchar * buffer, gsize size, ...)
{
  const struct spa_pod *ret;
  va_list args;
  va_start (args, size);
  ret = wp_spa_props_build_pod_valist (buffer, size, args);
  va_end (args);
  return ret;
}

#define wp_spa_props_register(self, id, name, ...) \
({ \
  gchar b[512]; \
  wp_spa_props_register_pod (self, id, name, \
      wp_spa_props_build_pod (b, sizeof (b), ##__VA_ARGS__, NULL)); \
})

#define wp_spa_props_store(self, id, ...) \
({ \
  gchar b[512]; \
  wp_spa_props_store_pod (self, id, \
      wp_spa_props_build_pod (b, sizeof (b), ##__VA_ARGS__, NULL)); \
})

G_END_DECLS

#endif
