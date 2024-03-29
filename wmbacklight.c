/* apm/acpi dockapp - phear it 1.34
 * Copyright (C) 2000, 2001, 2002 timecop@japan.co.jp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#define _WMBACKLIGHT_SOURCE

#include <libdockapp/dockapp.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/xpm.h>

/* main pixmap */
#include "XPM/wmbacklight.xpm"

#define VERSION "0.0"
#define SLEEP 150000

// bar types
#define BAR_T0 0
#define BAR_T1 2

#define BAR_0 0
#define BAR_1 1

#define PANEL_0 0
#define PANEL_1 1

struct dockapp {
  int x_fd;         /* X11 fd */
  Display *display; /* display */
  Window win;       /* main window */
  Pixmap pixmap;    /* main pixmap */
  Pixmap mask;      /* mask pixmap */

  unsigned short width;  /* width of pixmap */
  unsigned short height; /* height of pixmap */
  int screen;            /* current screen */
  int redraw;            /* need to redraw? */
  int updated;           /* magnitudes were updated */

  int brightness;
  int brightness_max;
  int kb_backlight;
  int kb_backlight_max;

  char *path_kbd_backlight;
  char *path_kbd_backlight_max;
  char *path_scr_brightness;
  char *path_scr_brightness_max;
};

/* globals */
struct dockapp *dockapp;
/* global_t *globals; */

/* copy a chunk of pixmap around the app */
static void copy_xpm_area(int x, int y, int w, int h, int dx, int dy) {
  XCopyArea(DADisplay, dockapp->pixmap, dockapp->pixmap, DAGC, x, y, w, h, dx,
            dy);
  dockapp->redraw = 1;
}

/* get x coordinate for digit d from pixmap */
static void draw_number(int number, int panel) {
  int panel_offset_y = 29;
  int digit;
  // int digit_off_x = 0;
  int digit_off_y = 92;
  // char size, defined by pixmap
  int char_width = 7;
  // int char_height = 13;
  // panel location
  int panel_x = 10;
  int panel_y = 16 + panel * panel_offset_y;
  // destination
  int dx, dy;
  // digit 1
  digit = (number / 100) % 10;
  dx = panel_x;
  dy = panel_y;
  copy_xpm_area(digit * char_width, digit_off_y, 7, 13, dx, dy);
  // digit 2
  digit = (number / 10) % 10;
  dx = panel_x + char_width;
  dy = panel_y;
  copy_xpm_area(digit * char_width, digit_off_y, 7, 13, dx, dy);
  // digit 3
  digit = number % 10;
  dx = panel_x + 2 * char_width;
  dy = panel_y;
  copy_xpm_area(digit * char_width, digit_off_y, 7, 13, dx, dy);
}

/* */
static void draw_bar(int percent, int bar_dest, int bar_type) {
  int bar_height = 7;
  int bar_max_width = 54;

  // 29 is our magic distance between bars on the pixmap
  int bar_offset = 29;
  // source
  int bar_src_x = 0;
  int bar_src_y = 64;
  // destination
  int bar_dst_x = 5;
  int bar_dst_y = 5;
  // source coordinates
  int sx = bar_src_x;
  int sy = bar_src_y + bar_type * bar_height;
  // destination coordinates
  int dx = bar_dst_x;
  int dy = bar_dst_y + bar_dest * bar_offset;
  // size
  int bar_w = (bar_max_width / 100.0) * percent;
  int bar_h = bar_height;

  copy_xpm_area(sx, sy, bar_w, bar_h, dx, dy);

  // disabled background
  sx = bar_src_x + bar_w;
  // bar inmediately below is the disabled counterpart for bar_type
  sy = bar_src_y + (bar_type + 1) * bar_height;
  dx = bar_dst_x + bar_w;
  bar_w = bar_max_width - bar_w;
  copy_xpm_area(sx, sy, bar_w, bar_h, dx, dy);
}

static int read_value(char *filename) {
  int value;
  FILE *fptr;
  fptr = fopen(filename, "r");
  fscanf(fptr, "%d", &value);
  fclose(fptr);
  return value;
}

static void read_scr_brightness(struct dockapp *d) {
  int raw;
  int bright_old = d->brightness;
  raw = read_value(d->path_scr_brightness);
  d->brightness = (int)((100.0 * raw) / d->brightness_max);
  d->updated += (bright_old != d->brightness);
}

static void read_kbd_backlight(struct dockapp *d) {
  int raw;
  int kb_old = d->kb_backlight;
  raw = read_value(d->path_kbd_backlight);
  d->kb_backlight = (int)((100.0 * raw) / d->kb_backlight_max);
  d->updated += (kb_old != d->kb_backlight);
}

static int check_file(const char *path) {
  struct stat buffer;
  int exist = stat(path, &buffer);
  if (exist == 0)
    return 1;
  else
    return 0;
}

static void redraw_window(void) {
  if (dockapp->redraw) {
    XCopyArea(dockapp->display, dockapp->pixmap, dockapp->win, DAGC, 0, 0, 64,
              64, 0, 0);
    dockapp->redraw = 0;
  }
}

static void new_window(char *display, char *name, int argc, char **argv) {
  XSizeHints *hints;

  /* Initialise the dockapp window and appicon */
  DAOpenDisplay(display, argc, argv);
  DACreateIcon(name, 64, 64, argc, argv);
  dockapp->display = DADisplay;
  dockapp->x_fd = XConnectionNumber(dockapp->display);
  dockapp->win = DAWindow;

  XSelectInput(dockapp->display, dockapp->win,
               ExposureMask | ButtonPressMask | ButtonReleaseMask |
                   StructureNotifyMask);

  /* create the main pixmap . . . */
  DAMakePixmapFromData(wmbacklight_xpm, &dockapp->pixmap, &dockapp->mask,
                       &dockapp->width, &dockapp->height);
  DASetPixmap(dockapp->pixmap);
  DASetShape(dockapp->mask);

  /* force the window to stay this size - otherwise the user could
   * resize us and see our panties^Wmaster pixmap . . . */
  hints = XAllocSizeHints();
  if (hints) {
    hints->flags |= PMinSize | PMaxSize;
    hints->min_width = 64;
    hints->max_width = 64;
    hints->min_height = 64;
    hints->max_height = 64;
    XSetWMNormalHints(dockapp->display, dockapp->win, hints);
    XFree(hints);
  }
  DAShow();
}

int main(int argc, char **argv) {
  char *display = NULL;

  char default_scr_brightness[] = "/sys/class/backlight/intel_backlight";
  char default_kbd_backlight[] = "/sys/class/leds/tpacpi::kbd_backlight";
  char *scr_device = default_scr_brightness;
  char *kbd_device = default_kbd_backlight;

  fd_set fds;

  DAProgramOption options[] = {
      /* {"-n", "--no-blink", "disable blinking", DONone, False, {NULL}}, */
      {"-d",
       "--display",
       "display or remote display",
       DOString,
       False,
       {&display}},
      {"-s",
       "--scr-device",
       "screen backlight device",
       DOString,
       False,
       {&scr_device}},
      {"-k",
       "--kbd-device",
       "keyboard backlight device",
       DOString,
       False,
       {&kbd_device}}};

  dockapp = calloc(1, sizeof(struct dockapp));

  DAParseArguments(
      argc, argv, options, 3,
      "WM dockapp to display screen brightness and keyboard backlight level",
      VERSION);

  int size;
  // screen brightness
  size = strlen(scr_device) + strlen("/brightness");
  dockapp->path_scr_brightness = calloc(size, sizeof(char));
  size = strlen(scr_device) + strlen("/max_brightness");
  dockapp->path_scr_brightness_max = calloc(size, sizeof(char));
  // keyboard backlight
  size = strlen(kbd_device) + strlen("/backlight");
  dockapp->path_kbd_backlight = calloc(size, sizeof(char));
  size = strlen(kbd_device) + strlen("/max_backlight");
  dockapp->path_kbd_backlight_max = calloc(size, sizeof(char));

  strcpy(dockapp->path_scr_brightness, scr_device);
  strcat(dockapp->path_scr_brightness, "/brightness");
  strcpy(dockapp->path_scr_brightness_max, scr_device);
  strcat(dockapp->path_scr_brightness_max, "/max_brightness");

  strcpy(dockapp->path_kbd_backlight, kbd_device);
  strcat(dockapp->path_kbd_backlight, "/brightness");
  strcpy(dockapp->path_kbd_backlight_max, kbd_device);
  strcat(dockapp->path_kbd_backlight_max, "/max_brightness");

  if (!check_file(dockapp->path_scr_brightness)) {
    fprintf(stderr, "Invalid path: %s\n", dockapp->path_scr_brightness);
    exit(1);
  }
  if (!check_file(dockapp->path_scr_brightness_max)) {
    fprintf(stderr, "Invalid path: %s\n", dockapp->path_scr_brightness_max);
    exit(1);
  }
  if (!check_file(dockapp->path_kbd_backlight)) {
    fprintf(stderr, "Invalid path: %s\n", dockapp->path_kbd_backlight);
    exit(1);
  }
  if (!check_file(dockapp->path_kbd_backlight_max)) {
    fprintf(stderr, "Invalid path: %s\n", dockapp->path_kbd_backlight_max);
    exit(1);
  }

  // read max values
  dockapp->brightness_max = read_value(dockapp->path_scr_brightness_max);
  dockapp->kb_backlight_max = read_value(dockapp->path_kbd_backlight_max);

  /* make new dockapp window */
  new_window(display, "wmbacklight", argc, argv);

  /* main loop */
  while (1) {
    Atom atom;
    Atom wmdelwin;
    XEvent event;
    while (XPending(dockapp->display)) {
      XNextEvent(dockapp->display, &event);
      switch (event.type) {
      case Expose:
        /* update */
        dockapp->redraw = 1;
        while (XCheckTypedEvent(dockapp->display, Expose, &event))
          ;
        redraw_window();
        break;
      case DestroyNotify:
        XCloseDisplay(dockapp->display);
        exit(0);
        break;
      case ButtonPress:
        break;
      case ButtonRelease:
        break;
      case ClientMessage:
        /* what /is/ this ****?
         * Turns out that libdockapp adds the WM_DELETE_WINDOW atom to
         * the WM_PROTOCOLS property for the window, which means that
         * rather than get a simple DestroyNotify message, we get a
         * nice little message from the WM saying "hey, can you delete
         * yourself, pretty please?". So, when running as a window
         * rather than an icon, we're impossible to kill in a friendly
         * manner, because we're expecting to die from a DestroyNotify
         * and thus blithely ignoring the WM knocking on our window
         * border . . .
         *
         * This simply checks for that scenario - it may fail oddly if
         * something else comes to us via a WM_PROTOCOLS ClientMessage
         * event, but I suspect it's not going to be an issue. */
        wmdelwin = XInternAtom(dockapp->display, "WM_DELETE_WINDOW", 1);
        atom = event.xclient.data.l[0];
        if (atom == wmdelwin) {
          XCloseDisplay(dockapp->display);
          exit(0);
        }
        break;
      }
    }

    // read
    read_scr_brightness(dockapp);
    read_kbd_backlight(dockapp);

    if(dockapp->updated){
      draw_bar(dockapp->brightness, BAR_0, BAR_T0);
      draw_number(dockapp->brightness, PANEL_0);
      draw_bar(dockapp->kb_backlight, BAR_1, BAR_T1);
      draw_number(dockapp->kb_backlight, PANEL_1);
      redraw_window();
    }

    FD_ZERO(&fds);
    FD_SET(dockapp->x_fd, &fds);
    usleep(SLEEP);
  }
  return 0;
}
