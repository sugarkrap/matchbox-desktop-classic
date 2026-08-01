/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _HAVE_MBDESKTOP_H
#define _HAVE_MBDESKTOP_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_DNOTIFY 		/* Needed for dnotify stuff from fcntl.c */
#define _GNU_SOURCE
#endif 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h> 
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysymdef.h>
#include <X11/keysym.h>
#include <X11/Xmd.h>

#include <zlib.h>

#include "config.h"

#include <libmb/mb.h>


#ifdef USE_XSETTINGS
#include <xsettings-client.h>
#endif 

#define DD_DIR DATADIR "/applications/" 

/* #define DD_DIR "/tmp/" */
#define PIXMAP_PATH PIXMAPSDIR


#ifdef DEBUG
#define DBG(txt, args... ) fprintf(stderr, "DT-DEBUG: " txt , ##args )
#else
#define DBG(txt, args... ) /* nothing */
#endif

#define ITEM_WIDTH  96 /* 48 */
#define ITEM_HEIGHT 108 /* 54 */
#define ICON_SIZE   64 /* 32 */

/* Pager strip at the bottom of the launcher. The arrows are drawn at
 * PAGER_ICON_SIZE but tapped over a third of the strip each, so the size
 * here is a legibility choice, not a hit-target one. PAGER_PAD is the
 * breathing room above and below the taller of arrow and page text. */
#define PAGER_ICON_SIZE 16
#define PAGER_PAD        6

#ifdef MB_HAVE_PNG
#define NO_APP_ICON    "mbnoapp.png"
#define FOLDER_PATH    DATADIR "/pixmaps/mbfolder.png"
#else
#define NO_APP_ICON    "mbnoapp.xpm"
#define FOLDER_PATH    DATADIR "/pixmaps/mbfolder.xpm"
#endif

/* below will fall back to fixed for core X fonts */
#define FONT_DESC       "Sans bold 14px"
#define FONT_TITLE_DESC "Sans bold 14px"


#define FONT_COL "#ffffff"

#define ITEM_TEXT_ROWS 2

#define DEFAULT_DESKTOP_BG_COL "#a1b2e9"

enum {
  VIEW_ICONS,
  VIEW_LIST,
  VIEW_ICONS_ONLY,
  VIEW_TEXT_ONLY,
};

enum {
  DKTP_FONT_COL_WHITE,
  DKTP_FONT_COL_BLACK,
  DKTP_FONT_COL_SHADOW,
  DKTP_FONT_COL_UNKOWN
};

enum {	   /* from .desktop type key should this be moved to dotdesktop.c ? */
  ITEM_TYPE_UNKNOWN = 0,
  ITEM_TYPE_ROOT,

  ITEM_TYPE_DOTDESKTOP_FOLDER,
  ITEM_TYPE_DOTDESKTOP_ITEM,
  ITEM_TYPE_MODULE_ITEM,
  ITEM_TYPE_MODULE_WINDOW,
  ITEM_TYPE_APP,
  ITEM_TYPE_FOLDER,  /* Same as 'official' Directory */
  ITEM_TYPE_LINK,    /* URL  */

  ITEM_TYPE_FSDEVICE, 
  ITEM_TYPE_MIMETYPE, 
  ITEM_TYPE_DIRECTORY, 
  ITEM_TYPE_SERVICE, 
  ITEM_TYPE_SERVICETYPE ,

  ITEM_TYPE_TASK_FOLDER, 		/* Not official */
  ITEM_TYPE_PREVIOUS,
  ITEM_TYPE_CNT, 
};

enum {
  BG_SOLID = 1,
  BG_TILED_PXM,
  BG_STRETCHED_PXM,
  BG_GRADIENT_HORIZ,
  BG_GRADIENT_VERT,
  BG_CENTERED_PXM,
  BG_FILLED_PXM,
};

/* True for the bg types that come from a decoded image file on disk,
 * as opposed to solid colors/gradients computed purely in memory --
 * these are the ones eligible for the baked raw-pixel boot cache. */
#define BG_IS_IMAGE_TYPE(t) ( (t) == BG_TILED_PXM     || \
			       (t) == BG_STRETCHED_PXM || \
			       (t) == BG_CENTERED_PXM  || \
			       (t) == BG_FILLED_PXM )

enum {
  HIGHLIGHT_OUTLINE,
  HIGHLIGHT_OUTLINE_CLEAR,
  HIGHLIGHT_FILL,
  HIGHLIGHT_FILL_CLEAR,
};

typedef void (*MBDesktopCB)( void *data1, void *data2 ) ;

typedef struct _mbdesktop_item {

  int                     type;
  int                     subtype; /* user defined type */
  int                     view;

  char                   *name;
  char                   *name_extended;
  char                   *comment;
  char                   *icon_name;
  MBPixbufImage          *icon;
  void                   *data;

  Window                  win;

  MBDesktopCB             activate_cb;
  MBDesktopCB             populate_cb; 	/* only populates children */

  struct _mbdesktop_item *item_next_sibling; 
  struct _mbdesktop_item *item_prev_sibling; 
  struct _mbdesktop_item *item_child; 
  struct _mbdesktop_item *item_parent; 

  /* Used for folders to remember there scroll pos when backed up to */
  struct _mbdesktop_item *saved_scroll_offset_item; 

  int                     x;
  int                     y;
  int                     width;
  int                     height;

  struct MBDesktopFolderModule *module_handle; /* XXX to go */

} MBDesktopItem;

typedef struct MBDesktopModuleslist
{
  struct MBDesktopFolderModule *module_handle;
  void *dl_handle;
  struct MBDesktopModuleslist *next;

} MBDesktopModuleslist;

typedef struct mbtest {
  char *data1;
  char *data2;
} MBTest;

typedef struct item_sizes {

  int width;
  int height;
  int icon_size;
  int box_yoffset;
  int box_size;
  int txt_yoffset;

} MBDesktopItemDimentions;

typedef struct _mbdesktop_bg {
  int type;

  union {
    char *filename;
    int cols[3];
    int gcols[6];
  } data;

} MBDesktopBG;

typedef struct _mbdesktop {

  Display *dpy;
  int scr;

  MBFont  *font;
  MBFont  *titlefont;

  MBColor *fgcol;
  MBColor *bgcol;

  MBDrawable *backing_cache;

  GC invert_gc;
  GC gc;

  Window root, win_top_level;

  Bool have_focus;
  Bool user_overide_font_col;
  Bool user_overide_font_outline;
  Bool user_overide_title;

  int                     current_view; 		
  MBDesktopItemDimentions itemDim;

  MBDotDesktopFolders     *ddfolders;

  MBDesktopItem           *top_head_item;      /* Very top of item list  */
  MBDesktopItem           *current_head_item;  /* Top of each child list */
  MBDesktopItem           *kbd_focus_item;     /* Kdb focused item       */
  MBDesktopItem           *scroll_offset_item; /* First item of the current
						  page -- derived from
						  current_page, never
						  advanced item by item */
  MBDesktopItem           *last_visible_item;  /* One past the last painted */
  MBDesktopItem           *current_folder_item; /* The current opened folder */

  Bool                     had_kbd_input;

  int                      current_view_columns;
  int                      current_view_rows;

  int desktop_width;
  int desktop_height;
  int workarea_width;
  int workarea_height;
  int workarea_x;
  int workarea_y;

  MBPixbuf               *pixbuf;   /* MBPixbuf ref  */

  Pixmap                  root_pxm;

  /* Pagination.
   *
   * The launcher lays every application out on a fixed grid and flips
   * whole pages, rather than scrolling the list by rows the way the
   * folder-based desktop used to. current_page is the only piece of
   * state that survives a repaint: items_per_page/n_pages and the
   * scroll_offset_item/last_visible_item window are all recomputed from
   * it, so a workarea change (panel appearing, screen rotation) cannot
   * leave the view pointing at half a page.
   *
   * The two arrow images are the theme's 16x16 mbup.png rotated a
   * quarter turn each way -- see mbdesktop_set_pager_buttons(). Using
   * one source for both guarantees they are mirror images of each
   * other, which loading mbup/mbdown separately would not.
   */
  MBPixbufImage *img_page_prev;
  MBPixbufImage *img_page_next;

  int            current_page;      /* 0-based */
  int            n_pages;
  int            items_per_page;
  int            pager_offset;      /* height reserved at the bottom, 0
				       when everything fits on one page */

  /* Tap targets, not the drawn glyphs: each covers a third of the pager
   * strip so a stylus does not have to find a 16x16 arrow. */
  XRectangle     pager_prev_rect;
  XRectangle     pager_next_rect;

  MBPixbufImage *bg_img;
  MBDesktopBG *bg;

  char *theme_name;
  char *bg_def;

  Bool view_type;

  int icon_size;  
  int icon_padding;
  int item_width;
  int item_height;
  int title_offset;

  Bool                    use_title_header;
  Bool                    use_text_outline;

  Window                  win_plugin;
  XRectangle              win_plugin_rect;

#ifdef USE_FAM

  FAMConnection fam_conn;
  FAMRequest *fam_req;

#endif 


#ifdef USE_XSETTINGS
  XSettingsClient *xsettings_client;
  Bool             xsettings_have_bg;
  Bool             xsettings_have_manager;
#endif 

  Atom window_type_atom, window_type_desktop_atom, desktop_manager_atom,
    window_type_dialog_atom, window_state_atom, window_state_modal_atom,
    window_utf8_name_atom, utf8_atom, atom_mb_theme, atom_mb_wallpaper;

  char *top_level_name;

  int    type_register_cnt;

  MBDesktopModuleslist *modules;

  MBColor *hl_col;
  int    font_col_type;

  /* Progress Dialog stuff  */
  Window win_dialog;
  Pixmap win_dialog_backing;
  int    win_dialog_w;
  int    win_dialog_h;


  /* TOGO */
  time_t dd_dir_mtime;
  char *dd_dir;
  char *pixmaps_dir;
  char *folder_img_path;

} MBDesktop;


struct moddentry
{
  struct MBDesktopFolderModule *handle;
  struct moddentry             *next;
};

/* Module structs  should go in mbdesktop_module.h */

typedef struct MBDesktopModuleInfo 
{
  char *name;
  char *desc;
  char *author;
  int   major;
  int   minor;
  int   micro;
  int   api;

} MBDesktopModuleInfo;

typedef struct MBDesktopFolderModule
{

  MBDesktopModuleInfo *mod_info;
  int            (*mod_init)   (MBDesktop                    *mb, 
				struct MBDesktopFolderModule *folder_module,
				char                         *arg_str);
  void           (*mod_xevent) (MBDesktop                    *mb, 
				struct MBDesktopFolderModule *folder_module,
				XEvent                       *xevent);
  void            *user_data;
} MBDesktopFolderModule;


void
mbdesktop_calculate_item_dimentions(MBDesktop *mb);


/* ------------------- */


Display *
mbdesktop_xdisplay (MBDesktop *mb);

Window
mbdesktop_xrootwin (MBDesktop *mb);

MBPixbuf *
mbdesktop_mbpixbuf (MBDesktop *mb);

int
mbdesktop_xscreen (MBDesktop *mb);

int
mbdesktop_icon_size (MBDesktop *mb);

void
mbdesktop_set_font(MBDesktop *mb, char *spec);

void
mbdesktop_set_title_font(MBDesktop *mb, char *spec);

void
mbdesktop_set_font_color(MBDesktop *mb, char *spec);

void
mbdesktop_switch_icon_theme (MBDesktop     *mb, 
			     MBDesktopItem *item );

void
mbdesktop_switch_theme (MBDesktop *mb, char *theme_name );

void
mbdesktop_set_pager_buttons(MBDesktop *mb);

/* Number of items in the currently displayed list. */
int
mbdesktop_page_item_count (MBDesktop *mb);

/* Which page `item` falls on, or -1 if it is not in the current list. */
int
mbdesktop_page_of_item (MBDesktop *mb, MBDesktopItem *item);

/* Show page `page` (clamped), moving the keyboard focus onto it. Repaints
 * only if the page actually changed; returns True if it did. */
Bool
mbdesktop_page_goto (MBDesktop *mb, int page);

int
mbdesktop_current_folder_view ( MBDesktop *mb );

void 
mbdesktop_view_paint(MBDesktop *mb, Bool use_cache);

Bool
mbdesktop_get_workarea(MBDesktop *mb, int *x, int *y, int *w, int *h);

void
mbdesktop_items_free(MBDesktop *mb, MBDesktopItem *item);

Bool
mbdesktop_bg_parse_spec(MBDesktop *mb, char *spec);

void
mbdesktop_progress_dialog_init (MBDesktop   *mb);

void
mbdesktop_progress_dialog_set_percentage (MBDesktop   *mb, 
					  int          percentage);

void
mbdesktop_progress_dialog_open (MBDesktop   *mb);

void
mbdesktop_progress_dialog_close (MBDesktop   *mb);


#endif
