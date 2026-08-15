/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "mbdesktop_module.h"

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

#ifdef USE_LIBSN

static void
item_activate_sn_cb(void *data1, void *data2);

static void
item_activate_si_cb(void *data1, void *data2);

static SnDisplay *SnDpy;

#endif

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

  if (item == NULL)
    {
      mbdesktop_items_append_to_top_level(mb, item_new);
      return;
    }

  item_new->item_parent = top;

  if (item->item_prev_sibling == NULL)
    {

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
  unsigned char  *heavy = NULL;
  unsigned char  *nodesktop = NULL;

  category = mb_dotdesktop_get(dd, "Categories");
  if (category && strstr(category, "Action"))
    return;

  nodesktop = mb_dotdesktop_get(dd, "X-Piko-NoDesktop");
  if (nodesktop && (!strcasecmp((char *)nodesktop, "true")
		    || !strcmp((char *)nodesktop, "1")))
    return;

  exec_str = mb_dotdesktop_get_exec(dd);

  heavy = mb_dotdesktop_get(dd, "X-Piko-Heavy");
  {
    char *video_key = (char *)mb_dotdesktop_get(dd, "X-Piko-Video");
    int   is_heavy  = heavy && (!strcasecmp((char *)heavy, "true")
				|| !strcmp((char *)heavy, "1"));

    if (is_heavy || video_key)
    {
      char *nm      = (char *)mb_dotdesktop_get(dd, "Name");
      char *reason  = (char *)mb_dotdesktop_get(dd, "X-Piko-Heavy-Reason");
      char *drivers = (char *)mb_dotdesktop_get(dd, "X-Piko-Drivers");
      char *video   = video_key;
      char  reason_opt[512]  = "";
      char  drivers_opt[128] = "";
      char  video_opt[64]    = "";
      char *wrapped;
      int   len;

      if (nm == NULL) nm = "This application";

      if (reason)
	snprintf(reason_opt, sizeof(reason_opt), "-r '%s' ", reason);
      if (drivers)
	snprintf(drivers_opt, sizeof(drivers_opt), "--drivers='%s' ", drivers);
      if (video)
	snprintf(video_opt, sizeof(video_opt), "--video='%s' ", video);

      len = strlen(exec_str) + strlen(nm) + strlen(reason_opt)
            + strlen(drivers_opt) + strlen(video_opt) + 64;

      wrapped = malloc(len);
      if (wrapped)
	{
	  snprintf(wrapped, len, "matchbox-apprun -n '%s' %s%s%s%s-- %s",
		   nm, is_heavy ? "" : "-y ", reason_opt, drivers_opt,
		   video_opt, exec_str);
	  free(exec_str);
	  exec_str = wrapped;
	}
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

  snprintf( vfolder_path_root, 512, "%s/.matchbox/vfolders/Root.directory",
	    mb_util_get_homedir());

  if (stat(vfolder_path_root, &stat_info))
    snprintf(vfolder_path_root, 512, PKGDATADIR "/vfolders/Root.directory");

  dd = mb_dotdesktop_new_from_file(vfolder_path_root);

  if (!dd)
    {
      fprintf( stderr, "mb-desktop-dotdesktop: cant open %s\n",
	       vfolder_path_root );
      return -1;
    }

  if (!mb->user_overide_title)
    mbdesktop_item_set_name (mb, mb->top_head_item,
			     mb_dotdesktop_get(dd, "Name"));

  mb_dotdesktop_free(dd);

  if (arg_str)
    {
      desktops_dirs_n = 1;
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

      if (i > 0 && !strcmp(app_paths[0], app_paths[i]))
	continue;

      if ((dp = opendir(app_paths[i])) == NULL)
	{

	  if (errno != ENOENT)
	    fprintf(stderr, "mb-desktop-dotdesktop: failed to open %s: %s\n",
		    app_paths[i], strerror(errno));
	  continue;
	}

#ifdef USE_DNOTIFY
      fd = open(app_paths[i], O_RDONLY);
      fcntl(fd, F_SETSIG, SIGRTMIN);
      fcntl(fd, F_NOTIFY, DN_RENAME|DN_MODIFY|DN_CREATE|DN_DELETE|DN_MULTISHOT);
#endif

      chdir(app_paths[i]);

      n = scandir(".", &namelist, 0, alphasort);

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

      fprintf (stderr, "Failed to exec %s \n", (char *)item->data);
      _exit (1);
      break;
    }

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
