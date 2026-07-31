/*
  mbdesktop_watch - notice when the set of installed applications changes.

  Copyright 2026 the piko project

  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _HAVE_MBDESKTOP_WATCH_H
#define _HAVE_MBDESKTOP_WATCH_H

#include <X11/Xlib.h>

/*
 * Watches for two things, and asks for a menu reload when either happens:
 *
 *   - an application directory gaining or losing a .desktop file, which
 *     is what an `opkg install` or `opkg remove` looks like from here;
 *   - the mount table changing, which is what inserting or removing the
 *     SD card looks like.
 *
 * The card matters because applications can be installed onto it rather
 * than into the ~68 MiB NAND root (see /usr/sbin/pkgadd). Its
 * applications directory only exists while a card is mounted, so it
 * cannot simply be watched once at startup -- the watch has to be
 * dropped and re-taken as cards come and go.
 */

/* Open the watch descriptors. Safe to call when none of the directories
 * exist; watching is best-effort and the desktop works without it.
 * Returns 0 on success, -1 if nothing could be watched at all. */
int mbdesktop_watch_init(void);

/* Block until there is an X event to process or something changed.
 *
 * Returns True if the caller should reload its menus before going on.
 * Returns False when it woke for an X event (or a harmless interruption),
 * in which case the caller should just proceed to XNextEvent().
 *
 * Returns immediately if X events are already queued, so it can be
 * dropped in front of XNextEvent() without changing event handling. */
Bool mbdesktop_watch_wait(Display *dpy);

#endif
