/*
  mbdesktop_watch - notice when the set of installed applications changes.

  Copyright 2026 the piko project

  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "mbdesktop_watch.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/inotify.h>

#include <libmb/mb.h>

/*
 * WHY THIS EXISTS
 * ===============
 * matchbox-desktop already knew how to rebuild its menus -- it has done
 * so on SIGHUP since forever. What it could not do was find out on its
 * own that there was anything to rebuild for, and it could not be woken
 * while idle:
 *
 *   - Its main loop blocks in XNextEvent(). A signal arriving while it
 *     is parked there does not get it moving, because Xlib retries its
 *     read internally on EINTR. The flag was set promptly and then acted
 *     on whenever the user next happened to touch the screen.
 *
 * So the desktop is taught to wait on more than one thing at once.
 * mbdesktop_watch_wait() replaces the bare block in XNextEvent() with a
 * select() over the X connection plus the two descriptors below, which
 * means a card appearing shows its applications straight away and an
 * opkg install lands on the desktop without anyone asking it to.
 *
 *
 * WHY THE MOUNT TABLE, AND NOT AN inotify WATCH ON /mnt/card
 * ==========================================================
 * inotify cannot see a mount happen. A watch is attached to an INODE,
 * and mounting a filesystem over /mnt/card does not modify the
 * underlying directory -- it covers it. The watch stays pointed at the
 * now-hidden jffs2 directory underneath and reports nothing, forever.
 *
 * What does work is that /proc/mounts (like /proc/self/mountinfo) is
 * pollable: the kernel raises an exceptional condition on it whenever
 * the mount table changes, which select() reports via exceptfds. That is
 * a real event, not a timer, so there is no polling interval to tune and
 * nothing wakes this process while nothing is happening -- which matters
 * on a battery-powered 400MHz machine.
 *
 * The card's own applications directory is then watched with inotify
 * too, but that watch has to be re-taken every time the mount table
 * changes, because the directory it refers to is a different inode after
 * each remount (and does not exist at all with no card in).
 */

#define CARD_APPS_DIR "/mnt/card/.zaurus/usr/share/applications"

/* The directories dotdesktop.c scans, minus the card (handled
 * separately) and minus $HOME/.applications, which is added at runtime
 * because the home directory is not known at compile time. */
static const char *watch_dirs[] = {
  DATADIR "/applications",
  "/usr/share/applications",
  "/usr/local/share/applications",
  NULL
};

static int inotify_fd = -1;
static int mounts_fd  = -1;
static int card_wd    = -1;
static int debug      = 0;

/* How long to keep absorbing further changes once one has been seen,
 * before telling the caller to reload. One user action is usually
 * several events -- installing a package creates a .desktop file and
 * then closes it (IN_CREATE then IN_CLOSE_WRITE), and pulling a card
 * changes the mount table AND destroys the watched directory -- so
 * without this the desktop rebuilds its whole menu twice for one thing
 * happening. Measured on the device, each rebuild is ~0.4s of CPU on a
 * 400MHz PXA255, so the second one is worth avoiding.
 *
 * Long enough to catch the follow-up events, short enough that nobody
 * perceives it as lag. */
#define COALESCE_MS 250

/* .desktop files are usually written by being created and closed, but
 * opkg unpacks to a temporary name and renames into place, and removing
 * a package unlinks. Watch all of those; DONT_FOLLOW keeps a symlinked
 * directory from redirecting the watch somewhere unexpected. */
#define WATCH_MASK (IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM \
                    | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF)

static void
watch_card_dir (void)
{
  if (card_wd != -1)
    {
      inotify_rm_watch (inotify_fd, card_wd);
      card_wd = -1;
    }

  /* Fails harmlessly when no card is mounted -- that is the normal
   * state, not an error worth reporting. */
  card_wd = inotify_add_watch (inotify_fd, CARD_APPS_DIR, WATCH_MASK);
}

/* Read /proc/mounts to completion. Required before select() will report
 * a *change*: an unread proc file looks perpetually ready, so skipping
 * this turns the select() below into a busy loop. */
static void
drain_mounts (void)
{
  char buf[1024];

  if (mounts_fd == -1)
    return;

  lseek (mounts_fd, 0, SEEK_SET);
  while (read (mounts_fd, buf, sizeof (buf)) > 0)
    ;
}

int
mbdesktop_watch_init (void)
{
  int i;
  char *home_apps = NULL;

  debug = (getenv ("MBDESKTOP_WATCH_DEBUG") != NULL);

  /* inotify_init1() is not in every uClibc build this project is likely
   * to be cross-compiled against, so use the older two-step. */
  inotify_fd = inotify_init ();
  if (inotify_fd != -1)
    {
      fcntl (inotify_fd, F_SETFD, FD_CLOEXEC);
      fcntl (inotify_fd, F_SETFL, O_NONBLOCK);

      for (i = 0; watch_dirs[i] != NULL; i++)
        inotify_add_watch (inotify_fd, watch_dirs[i], WATCH_MASK);

      home_apps = mb_util_get_homedir ();
      if (home_apps != NULL)
        {
          char path[512];
          snprintf (path, sizeof (path), "%s/.applications", home_apps);
          inotify_add_watch (inotify_fd, path, WATCH_MASK);
        }

      watch_card_dir ();
    }

  mounts_fd = open ("/proc/mounts", O_RDONLY);
  if (mounts_fd != -1)
    {
      fcntl (mounts_fd, F_SETFD, FD_CLOEXEC);
      drain_mounts ();
    }

  return (inotify_fd == -1 && mounts_fd == -1) ? -1 : 0;
}

/* Absorb the rest of a burst of changes, so one user action costs one
 * menu rebuild rather than several. Deliberately does NOT look at the X
 * descriptor: X events are left queued for the caller to handle after
 * the reload, exactly as they would have been anyway. */
static void
settle (void)
{
  struct timeval tv;
  fd_set rfds, xfds;
  int maxfd;

  for (;;)
    {
      FD_ZERO (&rfds);
      FD_ZERO (&xfds);
      maxfd = -1;

      if (inotify_fd != -1)
        {
          FD_SET (inotify_fd, &rfds);
          maxfd = inotify_fd;
        }
      if (mounts_fd != -1)
        {
          FD_SET (mounts_fd, &xfds);
          if (mounts_fd > maxfd)
            maxfd = mounts_fd;
        }
      if (maxfd < 0)
        return;

      tv.tv_sec  = 0;
      tv.tv_usec = COALESCE_MS * 1000;

      if (select (maxfd + 1, &rfds, NULL, &xfds, &tv) <= 0)
        return;                 /* quiet for COALESCE_MS, or interrupted */

      if (mounts_fd != -1 && FD_ISSET (mounts_fd, &xfds))
        {
          drain_mounts ();
          watch_card_dir ();
        }

      if (inotify_fd != -1 && FD_ISSET (inotify_fd, &rfds))
        {
          char buf[4096]
            __attribute__ ((aligned (__alignof__ (struct inotify_event))));
          while (read (inotify_fd, buf, sizeof (buf)) > 0)
            ;
        }
    }
}

Bool
mbdesktop_watch_wait (Display *dpy)
{
  fd_set rfds, xfds;
  int    xfd, maxfd;
  int    r;
  Bool   reload = False;

  /* Never sit in select() with events already queued in Xlib's buffer:
   * those have been read off the socket, so the descriptor is not
   * readable and this would block with work outstanding.
   *
   * QueuedAlready, NOT XPending(). They look interchangeable and are
   * not. XPending() is XEventsQueued(dpy, QueuedAfterFlush), which
   * flushes the output buffer and then tries to read the socket if the
   * local queue is empty. Doing that on every pass through the event
   * loop kept the connection permanently busy: measured on the device,
   * the version using XPending() woke ~6 times a second forever with
   * the X descriptor always reporting readable (ino=0 mounts=0, so
   * neither watch was involved), burning ~23 CPU ticks per 30 idle
   * seconds against 0 for the unmodified desktop.
   *
   * QueuedAlready only inspects the queue Xlib has already built. It
   * performs no I/O, so an idle desktop stays idle -- which on a
   * battery-powered 400MHz machine is the whole point. XNextEvent()
   * below still flushes before it blocks, so nothing goes unsent. */
  if (XEventsQueued (dpy, QueuedAlready) > 0)
    return False;

  xfd = ConnectionNumber (dpy);

  FD_ZERO (&rfds);
  FD_ZERO (&xfds);

  FD_SET (xfd, &rfds);
  maxfd = xfd;

  if (inotify_fd != -1)
    {
      FD_SET (inotify_fd, &rfds);
      if (inotify_fd > maxfd)
        maxfd = inotify_fd;
    }

  /* The mount table signals change as an EXCEPTION, not as readability.
   * Putting it in rfds instead would never fire. */
  if (mounts_fd != -1)
    {
      FD_SET (mounts_fd, &xfds);
      if (mounts_fd > maxfd)
        maxfd = mounts_fd;
    }

  r = select (maxfd + 1, &rfds, NULL, &xfds, NULL);

  if (debug)
    fprintf (stderr, "watch: select=%d x=%d ino=%d mounts=%d\n", r,
             FD_ISSET (xfd, &rfds),
             (inotify_fd != -1) ? FD_ISSET (inotify_fd, &rfds) : -1,
             (mounts_fd != -1) ? FD_ISSET (mounts_fd, &xfds) : -1);

  if (r < 0)
    {
      /* A signal (SIGHUP from a manual refresh, SIGCHLD from a launched
       * application exiting) landed. Not an error: go round again and
       * let the caller check its own reload flag. */
      return False;
    }

  if (mounts_fd != -1 && FD_ISSET (mounts_fd, &xfds))
    {
      /* A card went in or came out -- or something else was mounted, in
       * which case re-taking the watch and rebuilding the menu is
       * cheap and harmless. */
      drain_mounts ();
      watch_card_dir ();
      reload = True;
    }

  if (inotify_fd != -1 && FD_ISSET (inotify_fd, &rfds))
    {
      char buf[4096]
        __attribute__ ((aligned (__alignof__ (struct inotify_event))));

      /* Drain every queued event. The exact events do not matter -- any
       * of them means the menu is stale -- but they MUST be read, or the
       * descriptor stays readable and the next select() returns at once,
       * forever. */
      while (read (inotify_fd, buf, sizeof (buf)) > 0)
        ;

      reload = True;
    }

  if (reload)
    settle ();

  return reload;
}
