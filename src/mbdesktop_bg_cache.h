/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _HAVE_MBDESKTOP_BG_CACHE_H
#define _HAVE_MBDESKTOP_BG_CACHE_H

#include "mbdesktop.h"

/* Attempts to load the previously baked wallpaper straight from the
 * on-disk raw cache, skipping image decode/scale entirely. Returns a
 * new MBPixbufImage sized to the current desktop dimensions, or NULL
 * if there is no valid cache for the currently configured mb->bg
 * spec (missing file, size changed, pixel format changed, or the
 * source image was modified since the cache was written).
 */
MBPixbufImage *
mbdesktop_bg_cache_load(MBDesktop *mb);

/* Bakes the fully composited (desktop-sized) background image to the
 * on-disk raw cache so the next launch can use
 * mbdesktop_bg_cache_load() instead of decoding/scaling the source
 * image again. Best-effort: failures (read-only fs, no $HOME, etc)
 * are silently ignored since this is a boot-speed cache, not a
 * requirement for correctness.
 */
void
mbdesktop_bg_cache_save(MBDesktop *mb, MBPixbufImage *img);

#endif
