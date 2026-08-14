/* mb-wallpaper-picker - a minimal touch-friendly wallpaper picker for
 * the classic Matchbox desktop.
 *
 * Scans a couple of well-known directories for images, shows them as
 * a scrollable grid of thumbnails with a 4-way mode selector, and on
 * tap applies the choice two ways: it writes the persisted spec file
 * ($HOME/.matchbox/wallpaper) that matchbox-desktop reads at startup,
 * and it sets the _MB_WALLPAPER_SPEC property on the root window so a
 * *currently running* desktop updates immediately. The latter exists
 * because this device's busybox has no kill/killall/pkill at all, so
 * there is no way to signal matchbox-desktop to reload -- an X
 * property it already watches (PropertyNotify, same as the existing
 * _MB_THEME_NAME mechanism) is the only avenue.
 *
 * All the actual decode/scale/crop work for the *applied* wallpaper
 * happens inside matchbox-desktop itself (mbdesktop_view_init_bg +
 * mbdesktop_bg_cache_save) the next time it reads the spec -- this
 * picker only ever decodes small thumbnails, never touches the boot
 * path.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <libmb/mb.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define THUMB_SIZE   100
#define CELL_PAD     14
#define CELL         (THUMB_SIZE + CELL_PAD)
#define HEADER_H     56
#define STATUS_H     28
#define SCROLL_BTN   36

typedef struct {
  const char *label;
  const char *prefix;	/* bg spec prefix understood by mbdesktop_bg_parse_spec() */
} WPMode;

static WPMode g_modes[] = {
  { "Mosaic",   "img-mosaic:"    },
  { "Centered", "img-centered:"  },
  { "Stretch",  "img-stretched:" },
  { "Fill",     "img-filled:"    },
};
#define N_MODES ((int)(sizeof(g_modes)/sizeof(g_modes[0])))

typedef struct {
  char          *path;
  char          *label;
  MBPixbufImage *thumb;
  int            thumb_w, thumb_h;
  int            load_failed;
} WPEntry;

static Display  *g_dpy;
static int       g_scr;
static Window    g_root, g_win;
static MBPixbuf *g_pb;
static MBFont   *g_font;
static MBFont   *g_font_small;
static MBColor  *g_col_fg;
static MBColor  *g_col_hl;
static MBDrawable *g_backing;
static GC         g_gc;
static Atom        g_atom_wallpaper;
static Atom        g_atom_utf8;

static WPEntry *g_entries = NULL;
static int      g_n_entries = 0;
static int      g_selected_mode = 3;	/* default to Fill */
static int      g_scroll_row = 0;
static int      g_columns = 1;
static int      g_rows_visible = 1;
static int      g_win_w, g_win_h;
static char     g_status[256] = "Tap an image to apply it";

static int
_has_image_ext(const char *name)
{
  static const char *exts[] = { ".png", ".jpg", ".jpeg", ".bmp", NULL };
  size_t len = strlen(name);
  int i;

  for (i = 0; exts[i]; i++)
    {
      size_t elen = strlen(exts[i]);
      if (len > elen && !strcasecmp(name + len - elen, exts[i]))
	return 1;
    }
  return 0;
}

static int
_cmp_entry(const void *a, const void *b)
{
  const WPEntry *ea = a, *eb = b;
  return strcasecmp(ea->label, eb->label);
}

static void
_scan_dir(const char *dir, WPEntry **entries, int *count, int *cap)
{
  DIR *d = opendir(dir);
  struct dirent *de;

  if (d == NULL) return;

  while ((de = readdir(d)) != NULL)
    {
      char full[1024];

      if (de->d_name[0] == '.') continue;
      if (!_has_image_ext(de->d_name)) continue;

      snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);

      if (*count == *cap)
	{
	  *cap = (*cap) ? (*cap) * 2 : 16;
	  *entries = realloc(*entries, sizeof(WPEntry) * (size_t)(*cap));
	}

      (*entries)[*count].path        = strdup(full);
      (*entries)[*count].label       = strdup(de->d_name);
      (*entries)[*count].thumb       = NULL;
      (*entries)[*count].thumb_w     = 0;
      (*entries)[*count].thumb_h     = 0;
      (*entries)[*count].load_failed = 0;
      (*count)++;
    }

  closedir(d);
}

static void
_scan_wallpapers(void)
{
  int cap = 0;
  char userdir[512];
  const char *home = mb_util_get_homedir();

  _scan_dir("/usr/share/backgrounds", &g_entries, &g_n_entries, &cap);

  if (home != NULL && home[0] != '\0')
    {
      snprintf(userdir, sizeof(userdir), "%s/.matchbox/backgrounds", home);
      _scan_dir(userdir, &g_entries, &g_n_entries, &cap);
    }

  if (g_n_entries > 0)
    qsort(g_entries, (size_t)g_n_entries, sizeof(WPEntry), _cmp_entry);
}

static void
_ensure_thumb(WPEntry *e)
{
  MBPixbufImage *full, *scaled;
  int tw, th;

  if (e->thumb != NULL || e->load_failed) return;

  full = mb_pixbuf_img_new_from_file(g_pb, e->path);
  if (full == NULL)
    {
      e->load_failed = 1;
      return;
    }

  if (full->width >= full->height)
    {
      tw = THUMB_SIZE;
      th = (full->height * THUMB_SIZE) / full->width;
    }
  else
    {
      th = THUMB_SIZE;
      tw = (full->width * THUMB_SIZE) / full->height;
    }
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;

  scaled = mb_pixbuf_img_scale(g_pb, full, tw, th);
  mb_pixbuf_img_free(g_pb, full);

  e->thumb   = scaled;
  e->thumb_w = tw;
  e->thumb_h = th;
}

static void
_mode_btn_rect(int i, int *x, int *y, int *w, int *h)
{
  *w = g_win_w / N_MODES;
  *x = i * (*w);
  *y = 0;
  *h = HEADER_H;
}

static void
_cell_rect(int visible_index, int *x, int *y)
{
  int col = visible_index % g_columns;
  int row = visible_index / g_columns;

  *x = CELL_PAD/2 + col * CELL;
  *y = HEADER_H + STATUS_H + CELL_PAD/2 + row * CELL;
}

static void
_apply_wallpaper(WPEntry *e)
{
  char spec[1024];
  char path[512], tmp_path[544], dirpath[512];
  const char *home = mb_util_get_homedir();
  FILE *fp;

  snprintf(spec, sizeof(spec), "%s%s", g_modes[g_selected_mode].prefix, e->path);

  if (home != NULL && home[0] != '\0')
    {
      snprintf(dirpath, sizeof(dirpath), "%s/.matchbox", home);
      mkdir(dirpath, 0755);

      snprintf(path, sizeof(path), "%s/.matchbox/wallpaper", home);
      snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

      fp = fopen(tmp_path, "w");
      if (fp != NULL)
	{
	  fprintf(fp, "%s\n", spec);
	  fclose(fp);
	  rename(tmp_path, path);	/* atomic -- never leave a torn file */
	}
    }

  XChangeProperty(g_dpy, g_root, g_atom_wallpaper, g_atom_utf8, 8,
		  PropModeReplace, (unsigned char *)spec, (int)strlen(spec));
  XFlush(g_dpy);

  snprintf(g_status, sizeof(g_status), "Applied: %s (%s)",
	   e->label, g_modes[g_selected_mode].label);
}

static int
_max_scroll_row(void)
{
  int total_rows = (g_n_entries + g_columns - 1) / g_columns;
  int max_row = total_rows - g_rows_visible;
  return (max_row > 0) ? max_row : 0;
}

static void
_render(void)
{
  int i, visible_start, visible_end;
  Pixmap pxm = mb_drawable_pixmap(g_backing);

  /* Background */
  XSetForeground(g_dpy, g_gc, WhitePixel(g_dpy, g_scr));
  XFillRectangle(g_dpy, pxm, g_gc, 0, 0, g_win_w, g_win_h);

  /* Mode selector row */
  for (i = 0; i < N_MODES; i++)
    {
      int x, y, w, h;
      _mode_btn_rect(i, &x, &y, &w, &h);

      XSetForeground(g_dpy, g_gc, (i == g_selected_mode)
		     ? mb_col_xpixel(g_col_hl) : mb_col_xpixel(g_col_fg));
      XFillRectangle(g_dpy, pxm, g_gc, x+2, y+2, w-4, h-4);

      mb_font_set_color(g_font, (i == g_selected_mode) ? g_col_fg : g_col_hl);
      mb_font_render_simple(g_font, g_backing, x, y + (h/2) - 8, w,
			    (unsigned char *)g_modes[i].label,
			    MB_ENCODING_LATIN,
			    MB_FONT_RENDER_ALIGN_CENTER);
    }
  mb_font_set_color(g_font, g_col_fg);

  /* Status line */
  XSetForeground(g_dpy, g_gc, mb_col_xpixel(g_col_fg));
  mb_font_set_color(g_font_small, g_col_fg);
  mb_font_render_simple(g_font_small, g_backing, 8, HEADER_H + 4,
			g_win_w - 16, (unsigned char *)g_status,
			MB_ENCODING_LATIN, MB_FONT_RENDER_OPTS_CLIP_TRAIL);

  if (g_n_entries == 0)
    {
      mb_font_render_simple(g_font_small, g_backing, 8,
			    HEADER_H + STATUS_H + 24, g_win_w - 16,
			    (unsigned char *)
			    "No images in /usr/share/backgrounds or "
			    "~/.matchbox/backgrounds",
			    MB_ENCODING_LATIN, MB_FONT_RENDER_OPTS_CLIP_TRAIL);
    }

  /* Thumbnail grid -- only decode what is actually visible */
  visible_start = g_scroll_row * g_columns;
  visible_end   = visible_start + (g_rows_visible * g_columns);
  if (visible_end > g_n_entries) visible_end = g_n_entries;

  for (i = visible_start; i < visible_end; i++)
    {
      WPEntry *e = &g_entries[i];
      int cx, cy;

      _cell_rect(i - visible_start, &cx, &cy);

      XSetForeground(g_dpy, g_gc, mb_col_xpixel(g_col_fg));
      XDrawRectangle(g_dpy, pxm, g_gc, cx, cy, THUMB_SIZE, THUMB_SIZE);

      _ensure_thumb(e);

      if (e->thumb != NULL)
	{
	  int ox = cx + (THUMB_SIZE - e->thumb_w) / 2;
	  int oy = cy + (THUMB_SIZE - e->thumb_h) / 2;
	  mb_pixbuf_img_render_to_drawable(g_pb, e->thumb, pxm, ox, oy);
	}
      else if (e->load_failed)
	{
	  mb_font_render_simple(g_font_small, g_backing, cx + 4, cy + THUMB_SIZE/2 - 8,
				THUMB_SIZE - 8, (unsigned char *) "(failed)",
				MB_ENCODING_LATIN, MB_FONT_RENDER_OPTS_CLIP_TRAIL);
	}

      mb_font_render_simple(g_font_small, g_backing, cx, cy + THUMB_SIZE + 2,
			    THUMB_SIZE, (unsigned char *) e->label,
			    MB_ENCODING_LATIN, MB_FONT_RENDER_OPTS_CLIP_TRAIL
			    | MB_FONT_RENDER_ALIGN_CENTER);
    }

  /* Scroll indicators, only drawn when they would do something */
  if (g_scroll_row > 0 || _max_scroll_row() > 0)
    {
      int bx = g_win_w - SCROLL_BTN - 4;
      int uy = g_win_h - (2*SCROLL_BTN) - 8;
      int dy = g_win_h - SCROLL_BTN - 4;
      XPoint up[3], down[3];

      up[0].x = bx+SCROLL_BTN/2; up[0].y = uy;
      up[1].x = bx;              up[1].y = uy+SCROLL_BTN;
      up[2].x = bx+SCROLL_BTN;   up[2].y = uy+SCROLL_BTN;

      down[0].x = bx;              down[0].y = dy;
      down[1].x = bx+SCROLL_BTN;   down[1].y = dy;
      down[2].x = bx+SCROLL_BTN/2; down[2].y = dy+SCROLL_BTN;

      XSetForeground(g_dpy, g_gc, mb_col_xpixel(g_scroll_row > 0 ? g_col_hl : g_col_fg));
      XFillPolygon(g_dpy, pxm, g_gc, up, 3, Convex, CoordModeOrigin);

      XSetForeground(g_dpy, g_gc,
		     mb_col_xpixel(_max_scroll_row() > g_scroll_row ? g_col_hl : g_col_fg));
      XFillPolygon(g_dpy, pxm, g_gc, down, 3, Convex, CoordModeOrigin);
    }

  XSetWindowBackgroundPixmap(g_dpy, g_win, pxm);
  XClearWindow(g_dpy, g_win);
  XFlush(g_dpy);
}

static void
_handle_click(int x, int y)
{
  int i;

  if (y < HEADER_H)
    {
      int idx = x / (g_win_w / N_MODES);
      if (idx >= 0 && idx < N_MODES && idx != g_selected_mode)
	{
	  g_selected_mode = idx;
	  _render();
	}
      return;
    }

  /* Scroll buttons */
  if (x >= g_win_w - SCROLL_BTN - 4 && x <= g_win_w - 4)
    {
      int uy = g_win_h - (2*SCROLL_BTN) - 8;
      int dy = g_win_h - SCROLL_BTN - 4;

      if (y >= uy && y < uy + SCROLL_BTN && g_scroll_row > 0)
	{
	  g_scroll_row--;
	  _render();
	  return;
	}
      if (y >= dy && y < dy + SCROLL_BTN && g_scroll_row < _max_scroll_row())
	{
	  g_scroll_row++;
	  _render();
	  return;
	}
    }

  /* Thumbnail grid */
  {
    int visible_start = g_scroll_row * g_columns;
    int visible_end = visible_start + (g_rows_visible * g_columns);
    if (visible_end > g_n_entries) visible_end = g_n_entries;

    for (i = visible_start; i < visible_end; i++)
      {
	int cx, cy;
	_cell_rect(i - visible_start, &cx, &cy);

	if (x >= cx && x < cx + THUMB_SIZE
	    && y >= cy && y < cy + THUMB_SIZE + 16)
	  {
	    _apply_wallpaper(&g_entries[i]);
	    _render();
	    return;
	  }
      }
  }
}

int
main(int argc, char **argv)
{
  XEvent ev;
  Atom window_utf8_name_atom;
  int i;
  const char *display_name = getenv("DISPLAY");

  for (i = 1; i < argc; i++)
    {
      if ((!strcmp(argv[i], "-display") || !strcmp(argv[i], "-d"))
	  && i+1 < argc)
	{
	  display_name = argv[++i];
	  continue;
	}
    }

  if ((g_dpy = XOpenDisplay(display_name)) == NULL)
    {
      fprintf(stderr, "mb-wallpaper-picker: unable to open display!\n");
      exit(1);
    }

  g_scr  = DefaultScreen(g_dpy);
  g_root = RootWindow(g_dpy, g_scr);
  g_pb   = mb_pixbuf_new(g_dpy, g_scr);

  g_atom_wallpaper = XInternAtom(g_dpy, "_MB_WALLPAPER_SPEC", False);
  g_atom_utf8      = XInternAtom(g_dpy, "UTF8_STRING", False);
  window_utf8_name_atom = XInternAtom(g_dpy, "_NET_WM_NAME", False);

  _scan_wallpapers();

  g_win_w = DisplayWidth(g_dpy, g_scr);
  g_win_h = DisplayHeight(g_dpy, g_scr);

  g_columns = (g_win_w - CELL_PAD) / CELL;
  if (g_columns < 1) g_columns = 1;
  g_rows_visible = (g_win_h - HEADER_H - STATUS_H - CELL_PAD) / CELL;
  if (g_rows_visible < 1) g_rows_visible = 1;

  g_win = XCreateWindow(g_dpy, g_root, 0, 0, g_win_w, g_win_h, 0,
			CopyFromParent, CopyFromParent, g_pb->vis,
			0, NULL);

  XStoreName(g_dpy, g_win, "Set Wallpaper");
  XChangeProperty(g_dpy, g_win, window_utf8_name_atom, g_atom_utf8, 8,
		  PropModeReplace, (unsigned char *) "Set Wallpaper", 13);

  XSelectInput(g_dpy, g_win, ExposureMask | ButtonPressMask |
	      ButtonReleaseMask | StructureNotifyMask);

  g_font       = mb_font_new_from_string(g_dpy, "Sans bold 14px");
  g_font_small = mb_font_new_from_string(g_dpy, "Sans 11px");
  g_col_fg     = mb_col_new_from_spec(g_pb, "#202020");
  g_col_hl     = mb_col_new_from_spec(g_pb, "#3465a4");
  mb_font_set_color(g_font, g_col_fg);
  mb_font_set_color(g_font_small, g_col_fg);

  g_backing = mb_drawable_new(g_pb, g_win_w, g_win_h);

  {
    XGCValues gv;
    gv.function = GXcopy;
    gv.graphics_exposures = 0;
    g_gc = XCreateGC(g_dpy, g_root, GCFunction|GCGraphicsExposures, &gv);
  }

  XMapWindow(g_dpy, g_win);

  _render();

  for (;;)
    {
      XNextEvent(g_dpy, &ev);

      switch (ev.type)
	{
	case Expose:
	  if (ev.xexpose.count == 0)
	    _render();
	  break;
	case ButtonRelease:
	  _handle_click(ev.xbutton.x, ev.xbutton.y);
	  break;
	case ClientMessage:
	  /* WM_DELETE_WINDOW, if the WM sends it */
	  exit(0);
	}
    }

  return 0;
}
