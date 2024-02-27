/* WirePlumber
 *
 * Copyright © 2024 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_BASE_DIRS_H__
#define __WIREPLUMBER_BASE_DIRS_H__

#include "defs.h"
#include "iterator.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to specify lookup directories
 * \ingroup wpbasedirs
 */
typedef enum { /*< flags >*/
  WP_BASE_DIRS_ENV_CONFIG = (1 << 0),       /*!< $WIREPLUMBER_CONFIG_DIR */
  WP_BASE_DIRS_ENV_DATA = (1 << 1),         /*!< $WIREPLUMBER_DATA_DIR */

  WP_BASE_DIRS_XDG_CONFIG_HOME = (1 << 8),  /*!< XDG_CONFIG_HOME/wireplumber */
  WP_BASE_DIRS_XDG_DATA_HOME = (1 << 9),    /*!< XDG_DATA_HOME/wireplumber */

  WP_BASE_DIRS_XDG_CONFIG_DIRS = (1 << 10), /*!< XDG_CONFIG_DIRS/wireplumber */
  WP_BASE_DIRS_XDG_DATA_DIRS = (1 << 11),   /*!< XDG_DATA_DIRS/wireplumber */

  WP_BASE_DIRS_ETC = (1 << 16),             /*!< ($prefix)/etc/wireplumber */
  WP_BASE_DIRS_PREFIX_SHARE = (1 << 17),    /*!< $prefix/share/wireplumber */

  WP_BASE_DIRS_CONFIGURATION =
      WP_BASE_DIRS_ENV_CONFIG |
      WP_BASE_DIRS_XDG_CONFIG_HOME |
      WP_BASE_DIRS_XDG_CONFIG_DIRS |
      WP_BASE_DIRS_ETC |
      WP_BASE_DIRS_XDG_DATA_DIRS |
      WP_BASE_DIRS_PREFIX_SHARE,

  WP_BASE_DIRS_DATA =
      WP_BASE_DIRS_ENV_DATA |
      WP_BASE_DIRS_XDG_DATA_HOME |
      WP_BASE_DIRS_XDG_DATA_DIRS |
      WP_BASE_DIRS_PREFIX_SHARE,
} WpBaseDirsFlags;

WP_API
gchar * wp_base_dirs_find_file (WpBaseDirsFlags flags,
    const gchar * subdir, const gchar * filename);

WP_API
WpIterator * wp_base_dirs_new_files_iterator (WpBaseDirsFlags flags,
    const gchar * subdir, const gchar * suffix);

G_END_DECLS

#endif