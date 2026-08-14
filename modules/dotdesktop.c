/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "mbdesktop_module.h"

/* Explicit rather than relying on mbdesktop_module.h's own includes:
 * errno/strerror are used below to tell "directory is simply not there"
 * apart from a real failure. */
#include <errno.h>
#include <string.h>

#ifdef USE_LIBSN
#define SN_API_NOT_YET_FROZEN 1
#include <libsn/sn.h>
#endif 

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif 

static void
item_activate_cb(void *data1, void *data2);

/* RootMatchStr / ItemTypeDotDesktop used to exist here to drive the
 * category-to-folder matching. The launcher is flat, so there is nothing
 * left to match against -- see dotdesktop_init(). */

#ifdef USE_LIBSN

static void
item_activate_sn_cb(void *data1, void *data2);

static void
item_activate_si_cb(void *data1, void *data2);

static SnDisplay *SnDpy;

#endif 

/* Insert into the flat top-level list, alphabetically.
 *
 * The desktop used to bucket applications into one vfolder per category
 * and sort within each bucket. Everything now lives in one list -- see
 * dotdesktop_init() for why -- so the sort is over the whole set and the
 * "Back" item that used to head every folder's children is gone. That
 * matters here because the old insertion loop leaned on it: it could
 * assume a first child already existed and only ever insert *after* it.
 * A flat list has no such sentinel, so both the empty-list and the
 * new-first-item cases have to be handled explicitly.
 */
static void
insert_sorted_at_top_level (MBDesktop *mb, MBDesktopItem *item_new)
{
  MBDesktopItem *top = mbdesktop_get_top_level_folder(mb);
  MBDesktopItem *item;

  if (top->item_child == NULL)
    {
      mbdesktop_items_append_to_top_level(mb, item_new);
      return;
    }

  for (item = top->item_child; item != NULL; item = item->item_next_sibling)
    if (strcasecmp(item->name, item_new->name) > 0)
      break;

  if (item == NULL)		/* sorts after everything present */
    {
      mbdesktop_items_append_to_top_level(mb, item_new);
      return;
    }

  item_new->item_parent = top;

  if (item->item_prev_sibling == NULL)
    {
      /* New head of the list. mbdesktop_items_prepend() would do this,
       * but it takes the head by reference and leaves item_parent alone,
       * and the head is what mbdesktop_item_get_parent() reads. */
      item_new->item_next_sibling = item;
      item->item_prev_sibling     = item_new;
      top->item_child             = item_new;
      return;
    }

  mbdesktop_items_insert_after (mb, item->item_prev_sibling, item_new);
}

static void
add_a_dotdesktop_item (MBDesktop     *mb,
		       MBDotDesktop  *dd)
{
  MBDesktopItem  *item_new = NULL;
  char           *exec_str = NULL;
  char           *category = NULL;
  unsigned char  *heavy = NULL;   /* mb_dotdesktop_get returns unsigned char * */
  unsigned char  *nodesktop = NULL;

  /* We dont want 'action' entrys */
  category = mb_dotdesktop_get(dd, "Categories");
  if (category && strstr(category, "Action"))
    return;

  /*
   * X-Piko-NoDesktop: reachable from a menu, but not an icon out here.
   *
   * The desktop is a flat launcher (see dotdesktop_init()) -- every
   * application it knows about is one icon on one list, with no folders to
   * put anything behind. That is the right shape for applications and the
   * wrong one for *settings*: piko-settings groups those into its own
   * categorised window, so listing each of them out here as well would
   * push the actual applications down the list to no benefit.
   *
   * This key removes an entry from THIS view only, and deliberately not
   * from anywhere else -- mb-applet-menu-launcher in matchbox-panel scans
   * the same files and never reads it, so a settings entry stays browsable
   * under its Categories= folder in the panel menu exactly as before.
   *
   * NoDisplay= is the freedesktop key that sounds like this and is not:
   * it means "hide from menus", which would take the panel menu away too.
   * The spec has nothing for "hide from the desktop, keep in the menu" --
   * an icon view is not a concept it has -- and reserves X- for exactly
   * this, so other implementations are required to ignore it. Same shape
   * as X-Piko-Heavy below.
   */
  nodesktop = mb_dotdesktop_get(dd, "X-Piko-NoDesktop");
  if (nodesktop && (!strcasecmp((char *)nodesktop, "true")
		    || !strcmp((char *)nodesktop, "1")))
    return;

  exec_str = mb_dotdesktop_get_exec(dd);

  /*
   * X-Piko-Heavy: applications that need the whole machine.
   *
   * Some things here (Quake, emulators, video players) want every cycle and
   * every megabyte, and want /dev/fb0 to themselves. That cannot be shared
   * with the X server, so they have to be run with the graphical session
   * stopped -- see userspace/src/matchbox-heavyrun.cxx, which asks the user
   * first, closes everything else, and puts the desktop back afterwards.
   *
   * Doing the rewrite HERE rather than in the activate callbacks means all
   * three of them (plain, StartupNotify, SingleInstance) inherit it, since
   * every one of them just execs item->data.
   *
   * The Desktop Entry spec has no key for this and reserves the X- prefix
   * for exactly this sort of extension, so other implementations are
   * required to ignore it. Name is passed through for the dialog text.
   */
  heavy = mb_dotdesktop_get(dd, "X-Piko-Heavy");
  if (heavy && (!strcasecmp((char *)heavy, "true")
		|| !strcmp((char *)heavy, "1")))
    {
      char *nm     = (char *)mb_dotdesktop_get(dd, "Name");
      char *reason = (char *)mb_dotdesktop_get(dd, "X-Piko-Heavy-Reason");
      char *wrapped;
      int   len;

      if (nm == NULL) nm = "This application";

      /* "matchbox-heavyrun -n 'NAME' -r 'REASON' -- EXEC" plus quoting slack. */
      len = strlen(exec_str) + strlen(nm)
            + (reason ? strlen(reason) : 0) + 64;

      wrapped = malloc(len);
      if (wrapped)
	{
	  if (reason)
	    snprintf(wrapped, len, "matchbox-heavyrun -n '%s' -r '%s' -- %s",
		     nm, reason, exec_str);
	  else
	    snprintf(wrapped, len, "matchbox-heavyrun -n '%s' -- %s",
		     nm, exec_str);
	  free(exec_str);
	  exec_str = wrapped;
	}
    }

  item_new = mbdesktop_item_new_with_params( mb,
					     mb_dotdesktop_get(dd, "Name"),
					     mb_dotdesktop_get(dd, "Icon"),
					     (void *)exec_str,
					     ITEM_TYPE_DOTDESKTOP_ITEM
					     );
  if (item_new == NULL ) return;

#ifdef USE_LIBSN
  if (mb_dotdesktop_get(dd, "SingleInstance")
      && !strcasecmp(mb_dotdesktop_get(dd, "SingleInstance"), 
		     "true"))
    {
      mbdesktop_item_set_activate_callback (mb, item_new, 
					    item_activate_si_cb); 
    }
  else if (mb_dotdesktop_get(dd, "StartupNotify")
	   && !strcasecmp(mb_dotdesktop_get(dd, "StartupNotify"), "true"))
    mbdesktop_item_set_activate_callback (mb, item_new, 
					  item_activate_sn_cb); 
  else
#endif
    mbdesktop_item_set_activate_callback (mb, item_new,
					  item_activate_cb);

  insert_sorted_at_top_level (mb, item_new);
}



int
dotdesktop_init (MBDesktop             *mb, 
		 MBDesktopFolderModule *folder_module, 
		 char                  *arg_str)
{
/* Five, not four: the fifth is the SD card. Applications can be
 * installed onto a card instead of into the NAND root, which on this
 * hardware is only ~68 MiB (see /usr/sbin/pkgadd, which drives opkg's
 * second destination). Without this entry a card application installs
 * correctly, is on $PATH, and simply never appears on the desktop.
 *
 * A path that is not there costs one failed opendir() per reload and is
 * skipped -- which is the normal case, since most of the time there is
 * no card in the slot. mbdesktop_watch.c is what notices a card arriving
 * and asks for the reload that makes this directory get scanned. */
#define APP_PATHS_N 5

  DIR *dp;
  struct stat    stat_info;

  char vfolder_path_root[512];
  char orig_wd[256];

  int   desktops_dirs_n  = APP_PATHS_N;

  int   i = 0;

  MBDotDesktop            *dd;

  char                     app_paths[APP_PATHS_N][256];
  struct dirent          **namelist;

#ifdef USE_LIBSN
  SnDpy = sn_display_new (mb->dpy, NULL, NULL);
#endif

  /* Root.directory is still read -- for the launcher's title, and for
   * nothing else now.
   *
   * The rest of the vfolders are deliberately ignored. This module used to
   * turn each one into an on-screen folder and file every application into
   * whichever folder's Match= matched its Categories= line, which meant
   * finding an application required knowing (or guessing) which category
   * somebody had filed it under, and then opening folders one at a time
   * until it turned up. On a device with a couple of dozen applications
   * that is strictly more work than showing all of them.
   *
   * Categories are not gone from the system, only from the desktop:
   * mb-applet-menu-launcher in matchbox-panel reads exactly the same
   * vfolder .directory files, independently of this module, and still
   * presents its menu as a category tree. Browsing by category lives
   * there; the desktop is now a flat, paginated launcher.
   */
  snprintf( vfolder_path_root, 512, "%s/.matchbox/vfolders/Root.directory",
	    mb_util_get_homedir());

  if (stat(vfolder_path_root, &stat_info))
    snprintf(vfolder_path_root, 512, PKGDATADIR "/vfolders/Root.directory");

  dd = mb_dotdesktop_new_from_file(vfolder_path_root);

  if (!dd) 			/* XXX improve */
    {
      fprintf( stderr, "mb-desktop-dotdesktop: cant open %s\n",
	       vfolder_path_root );
      return -1;
    }

  /* --title on the command line wins, so a session can name the launcher
   * without patching matchbox-common's data files. */
  if (!mb->user_overide_title)
    mbdesktop_item_set_name (mb, mb->top_head_item,
			     mb_dotdesktop_get(dd, "Name"));

  mb_dotdesktop_free(dd);

  /* The $HOME/.matchbox/desktop/dd-folder-overides file, which pinned an
   * individual .desktop file into a named folder, is not read any more:
   * there are no folders left for it to name. */

  /* Now grep all the .desktop files */

  if (arg_str)
    { 				/* hack to allow just one dir to be searched */
      desktops_dirs_n = 1;	/* Need to figure better way */
      strncpy(app_paths[0], arg_str, 256);
    }
  else
    {
      snprintf(app_paths[0], 256, "%s/applications", DATADIR);
      snprintf(app_paths[1], 256, "/usr/share/applications");
      snprintf(app_paths[2], 256, "/usr/local/share/applications");
      snprintf(app_paths[3], 256, "%s/.applications", mb_util_get_homedir());
      snprintf(app_paths[4], 256,
	       "/mnt/card/.zaurus/usr/share/applications");

    }

  if (getcwd(orig_wd, 255) == (char *)NULL)
    {
      fprintf(stderr, "Cant get current directory\n");
      return -1;
    }

  for (i = 0; i < desktops_dirs_n; i++)
    {
#ifdef USE_DNOTIFY
      int fd;
#endif
      
      int   n = 0, j = 0;

      /* Dont reread default */
      if (i > 0 && !strcmp(app_paths[0], app_paths[i]))
	continue;

      if ((dp = opendir(app_paths[i])) == NULL)
	{
	  /* "Not there" is the ordinary case for two of these paths -- the
	   * SD card is usually absent, and $HOME/.applications often does
	   * not exist -- so it is not worth a line of output. Anything
	   * else (permissions, I/O error) still gets reported.
	   *
	   * This is not just tidiness: the session's stderr goes to
	   * /tmp/matchbox-session.log, /tmp here is on the jffs2 root and
	   * not a tmpfs, and the menu is now reloaded every time a card is
	   * inserted or a package installed. Logging a line per missing
	   * directory per reload would write to flash for no reason. */
	  if (errno != ENOENT)
	    fprintf(stderr, "mb-desktop-dotdesktop: failed to open %s: %s\n",
		    app_paths[i], strerror(errno));
	  continue;
	}

#ifdef USE_DNOTIFY
      fd = open(app_paths[i], O_RDONLY);
      fcntl(fd, F_SETSIG, SIGRTMIN);
      fcntl(fd, F_NOTIFY, DN_RENAME|DN_MODIFY|DN_CREATE|DN_DELETE|DN_MULTISHOT);
#endif // USE_DNOTIFY

  
      chdir(app_paths[i]);

      n = scandir(".", &namelist, 0, alphasort);
      /*      while((dir_entry = readdir(dp)) != NULL) */
      while (j < n && n > 0)
	{

	  if (namelist[j]->d_name[0] ==  '.')
	    goto end;

	  if (strcmp(namelist[j]->d_name+strlen(namelist[j]->d_name)-8,".desktop"))
	    goto end;

	  lstat(namelist[j]->d_name, &stat_info);
	  if (!(S_ISDIR(stat_info.st_mode)))
	    {
	      MBDotDesktop *dd;
	      dd = mb_dotdesktop_new_from_file(namelist[j]->d_name);
	      if (dd)
		{
		  if (mb_dotdesktop_get(dd, "Type") 
		      && !strcmp(mb_dotdesktop_get(dd, "Type"), "Application")
		      && mb_dotdesktop_get(dd, "Name")
		      && mb_dotdesktop_get(dd, "Exec"))
		    add_a_dotdesktop_item (mb, dd);
		  mb_dotdesktop_free(dd);
		}
	    }
	end:
	  free(namelist[j]);
	  ++j;

	}
      
      closedir(dp);
      free(namelist);
    }
  chdir(orig_wd);

  return 1;
}

/* Activate callbacks */


#ifdef USE_LIBSN
static void
item_activate_sn_cb(void *data1, void *data2)
{
  MBDesktop *mb = (MBDesktop *)data1;
  MBDesktopItem *item = (MBDesktopItem *)data2;

  SnLauncherContext *context;
  pid_t child_pid = 0;

  context = sn_launcher_context_new (SnDpy, mb->scr);

  sn_launcher_context_set_name (context, item->name);
  if (item->comment)
    sn_launcher_context_set_description (context, item->comment);
  sn_launcher_context_set_binary_name (context, (char *)item->data);

  sn_launcher_context_initiate (context, "mbdesktop launch", 
				(char *)item->data, CurrentTime);

  switch ((child_pid = fork ()))
    {
    case -1:
      fprintf (stderr, "Fork failed\n" );
      break;
    case 0:
      sn_launcher_context_setup_child_process (context);
      mb_exec((char *)item->data);
      // execlp(item->exec_str, item->exec_str, NULL);
      fprintf (stderr, "Failed to exec %s \n", (char *)item->data);
      _exit (1);
      break;
    }
  /* No animate_startup: it XGrabServer()s across 20 round trips. See the
   * long note in item_activate_cb() below. */

}
#endif

#ifdef USE_LIBSN
static void
item_activate_si_cb(void *data1, void *data2)
{
  MBDesktop *mb = (MBDesktop *)data1;
  MBDesktopItem *item = (MBDesktopItem *)data2;
  Window win_found;

  if (mb_single_instance_is_starting(mb->dpy, (char *)item->data))
    return;

  win_found = mb_single_instance_get_window(mb->dpy, (char *)item->data);

  if (win_found != None)
    {
      /* No animate_startup: see the note in item_activate_cb() below. */
      mb_util_window_activate(mb->dpy, win_found);
    }
  else item_activate_sn_cb((void *)mb, (void *)item);

}
#endif

static void
item_activate_cb(void *data1, void *data2)
{
  MBDesktop *mb = (MBDesktop *)data1;
  MBDesktopItem *item = (MBDesktopItem *)data2;

  switch (fork())
    {
    case 0:
      mb_exec((char *)item->data);
      fprintf(stderr, "exec failed, cleaning up child\n");
      _exit(1);
    case -1:
      fprintf(stderr, "can't fork\n");
      break;
    }

  /*
   * No mb_util_animate_startup() here, and that is a bug fix rather than a
   * change of taste.
   *
   * It draws the expanding zoom rectangle, and the way it does it is to
   * XGrabServer() and then run 21 XOR XDrawRectangle()s on the ROOT window
   * (subwindow_mode IncludeInferiors, growing to full screen) with 20
   * XSync(dpy, True) round trips in between, before it ungrabs. libmb's own
   * comment on the function opens "Not sure on the evilness of this yet".
   *
   * A server grab means no other client is serviced at all, so for the whole
   * of that the panel cannot repaint, the window manager cannot answer the
   * MapRequest of the application we just forked, and queued input lands only
   * once it is over. That was survivable when a round trip was nearly free.
   *
   * It stopped being nearly free when Xfbdev gained double-buffered page
   * flipping: every one of those XSync()s now drives a block-handler flush
   * that waits on FBIO_WAITFORVSYNC, measured at up to 153 ms per round trip
   * on this panel. Twenty of them, grabbed, is a session-wide lockout of a
   * second or more -- and it is exactly why launching from a desktop icon
   * hung while the same application from matchbox-panel's launcher did not:
   * mb-applet-menu-launcher's fork_exec() just forks and execs, with no
   * animation and no grab.
   *
   * The underlying flip pacing is worth fixing on its own, but nothing should
   * be grabbing the server on an application launch path to begin with.
   */

}

#define MODULE_NAME         "DotDesktop App Launcher"
#define MODULE_DESC         "DotDesktop App Launcher"
#define MODULE_AUTHOR       "Matthew Allum"
#define MODULE_MAJOR_VER    0
#define MODULE_MINOR_VER    0
#define MODULE_MICRO_VER    1
#define MODULE_API_VERSION  0
 
MBDesktopModuleInfo dotdesktop_info = 
  {
    MODULE_NAME         ,
    MODULE_DESC         ,
    MODULE_AUTHOR       ,
    MODULE_MAJOR_VER    ,
    MODULE_MINOR_VER    ,
    MODULE_MICRO_VER    ,
    MODULE_API_VERSION
  };

MBDesktopFolderModule folder_module =
  {
    &dotdesktop_info,
    dotdesktop_init,
    NULL,
    NULL
  };
