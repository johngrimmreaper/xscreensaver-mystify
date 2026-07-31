/*
 * Minimal standalone host for an unmodified XScreenSaver display hack.
 *
 * Copyright (c) 2026 Reaper <JohnGrimmReaper@disroot.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation. No representations are made about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "screenhack.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_RESOURCES 64

char *progname;

struct resource_entry {
  char *name;
  char *value;
};

struct resource_table {
  struct resource_entry entries[MAX_RESOURCES];
  size_t count;
};

struct runner_options {
  const char *display_name;
  Window requested_window;
  bool have_requested_window;
  bool root_p;
};

static struct resource_table resources;
static volatile sig_atomic_t stop_requested;

static void
on_signal(int signal_number)
{
  (void) signal_number;
  stop_requested = 1;
}

static const char *
program_basename(const char *path)
{
  const char *slash = strrchr(path ? path : "", '/');
  return slash ? slash + 1 : path;
}

static char *
trim_left(char *text)
{
  while (*text && isspace((unsigned char) *text))
    text++;
  return text;
}

static void
trim_right(char *text)
{
  size_t length = strlen(text);

  while (length > 0 && isspace((unsigned char) text[length - 1]))
    text[--length] = '\0';
}

static const char *
resource_name(const char *specifier)
{
  while (*specifier == '.' || *specifier == '*')
    specifier++;
  return specifier;
}

static void
resource_set(const char *name, const char *value)
{
  size_t i;
  char *new_value;

  if (!name || !*name || !value)
    return;

  new_value = strdup(value);
  if (!new_value) {
    perror("strdup");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < resources.count; i++) {
    if (strcmp(resources.entries[i].name, name) == 0) {
      free(resources.entries[i].value);
      resources.entries[i].value = new_value;
      return;
    }
  }

  if (resources.count == ARRAY_LEN(resources.entries)) {
    fprintf(stderr, "%s: too many resources\n", progname);
    free(new_value);
    exit(EXIT_FAILURE);
  }

  resources.entries[resources.count].name = strdup(name);
  if (!resources.entries[resources.count].name) {
    perror("strdup");
    free(new_value);
    exit(EXIT_FAILURE);
  }

  resources.entries[resources.count].value = new_value;
  resources.count++;
}

static const char *
resource_get(const char *name)
{
  size_t i;

  for (i = 0; i < resources.count; i++)
    if (strcmp(resources.entries[i].name, name) == 0)
      return resources.entries[i].value;

  return NULL;
}

static void
resources_free(void)
{
  size_t i;

  for (i = 0; i < resources.count; i++) {
    free(resources.entries[i].name);
    free(resources.entries[i].value);
  }

  memset(&resources, 0, sizeof(resources));
}

static void
load_defaults(const char *const *defaults)
{
  size_t i;

  if (!defaults)
    return;

  for (i = 0; defaults[i]; i++) {
    char *copy = strdup(defaults[i]);
    char *separator;
    char *name;
    char *value;

    if (!copy) {
      perror("strdup");
      exit(EXIT_FAILURE);
    }

    separator = strchr(copy, ':');
    if (!separator) {
      free(copy);
      continue;
    }

    *separator = '\0';
    name = trim_left(copy);
    trim_right(name);
    value = trim_left(separator + 1);
    trim_right(value);

    resource_set(resource_name(name), value);
    free(copy);
  }
}

int
get_integer_resource(Display *dpy, const char *name,
                     const char *resource_class)
{
  const char *value = resource_get(name);
  char *end = NULL;
  long parsed;

  (void) dpy;
  (void) resource_class;

  if (!value)
    return 0;

  errno = 0;
  parsed = strtol(value, &end, 0);
  if (errno || end == value || *end != '\0' ||
      parsed < INT_MIN || parsed > INT_MAX) {
    fprintf(stderr, "%s: invalid integer resource %s: %s\n",
            progname, name, value);
    exit(EXIT_FAILURE);
  }

  return (int) parsed;
}


unsigned int
get_pixel_resource(Display *dpy, Colormap colormap,
                   const char *name, const char *resource_class)
{
  const char *value = resource_get(name);
  XColor color;

  (void) resource_class;

  if (!value)
    value = "black";

  if (!XParseColor(dpy, colormap, value, &color) ||
      !XAllocColor(dpy, colormap, &color)) {
    fprintf(stderr, "%s: unable to allocate color resource %s: %s\n",
            progname, name, value);
    exit(EXIT_FAILURE);
  }

  if (color.pixel > UINT_MAX) {
    fprintf(stderr, "%s: color pixel is outside the supported range\n",
            progname);
    exit(EXIT_FAILURE);
  }

  return (unsigned int) color.pixel;
}


static unsigned long
parse_unsigned_long(const char *text, const char *description)
{
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 0);
  if (errno || end == text || *end != '\0') {
    fprintf(stderr, "%s: invalid %s: %s\n",
            progname, description, text);
    exit(EXIT_FAILURE);
  }

  return value;
}

static const char *
need_argument(int argc, char **argv, int *index)
{
  if (*index + 1 >= argc) {
    fprintf(stderr, "%s: option %s requires an argument\n",
            progname, argv[*index]);
    exit(EXIT_FAILURE);
  }

  (*index)++;
  return argv[*index];
}

static bool
option_equal(const char *argument, const char *option)
{
  if (strcmp(argument, option) == 0)
    return true;

  return strncmp(argument, "--", 2) == 0 && option[0] == '-' &&
         strcmp(argument + 1, option) == 0;
}

static bool
parse_module_option(int argc, char **argv, int *index,
                    const XrmOptionDescRec *module_options)
{
  size_t i;
  const char *argument = argv[*index];

  if (!module_options)
    return false;

  for (i = 0; module_options[i].option; i++) {
    const XrmOptionDescRec *option = &module_options[i];

    if (!option_equal(argument, option->option))
      continue;

    switch (option->argKind) {
    case XrmoptionSepArg:
      resource_set(resource_name(option->specifier),
                   need_argument(argc, argv, index));
      return true;

    case XrmoptionNoArg:
      resource_set(resource_name(option->specifier),
                   option->value ? (const char *) option->value : "true");
      return true;

    default:
      fprintf(stderr, "%s: unsupported option kind for %s\n",
              progname, argument);
      exit(EXIT_FAILURE);
    }
  }

  return false;
}

static void
print_usage(FILE *stream, const XrmOptionDescRec *module_options)
{
  size_t i;

  fprintf(stream,
          "Usage: %s [options]\n"
          "  -display DISPLAY       X display\n"
          "  -root                  draw on the root window\n"
          "  -window                create a test window (default)\n"
          "  -window-id ID          draw into an existing window\n"
          "  -background COLOR      erase/background color\n"
          "  -foreground COLOR      foreground resource\n"
          "  -fps                   accepted for compatibility\n"
          "  -version               print version information\n"
          "  -help                  show this help\n",
          progname);

  if (!module_options)
    return;

  fputs("\nHack options:\n", stream);
  for (i = 0; module_options[i].option; i++)
    fprintf(stream, "  %-22s%s\n",
            module_options[i].option,
            module_options[i].argKind == XrmoptionSepArg ? " VALUE" : "");
}

static struct runner_options
parse_options(int argc, char **argv,
              const XrmOptionDescRec *module_options)
{
  struct runner_options options;
  int i;

  memset(&options, 0, sizeof(options));

  for (i = 1; i < argc; i++) {
    const char *argument = argv[i];

    if (option_equal(argument, "-display")) {
      options.display_name = need_argument(argc, argv, &i);
    } else if (option_equal(argument, "-root")) {
      options.root_p = true;
    } else if (option_equal(argument, "-window")) {
      options.root_p = false;
    } else if (option_equal(argument, "-window-id") ||
               strcmp(argument, "-wid") == 0) {
      options.requested_window = (Window)
        parse_unsigned_long(need_argument(argc, argv, &i), "window id");
      options.have_requested_window = true;
    } else if (option_equal(argument, "-background") ||
               strcmp(argument, "-bg") == 0) {
      resource_set("background", need_argument(argc, argv, &i));
    } else if (option_equal(argument, "-foreground") ||
               strcmp(argument, "-fg") == 0) {
      resource_set("foreground", need_argument(argc, argv, &i));
    } else if (option_equal(argument, "-fps")) {
      resource_set("fps", "true");
    } else if (option_equal(argument, "-no-fps")) {
      resource_set("fps", "false");
    } else if (option_equal(argument, "-visual")) {
      (void) need_argument(argc, argv, &i);
    } else if (option_equal(argument, "-no-db") ||
               option_equal(argument, "-db")) {
      /* Accepted compatibility switches. */
    } else if (option_equal(argument, "-version")) {
      printf("%s standalone\n", progname);
      exit(EXIT_SUCCESS);
    } else if (option_equal(argument, "-help") ||
               strcmp(argument, "-h") == 0) {
      print_usage(stdout, module_options);
      exit(EXIT_SUCCESS);
    } else if (!parse_module_option(argc, argv, &i, module_options)) {
      fprintf(stderr, "%s: unknown option: %s\n", progname, argument);
      print_usage(stderr, module_options);
      exit(EXIT_FAILURE);
    }
  }

  return options;
}

static Window
window_from_environment(void)
{
  const char *value = getenv("XSCREENSAVER_WINDOW");

  if (!value || !*value)
    return 0;

  return (Window) parse_unsigned_long(value, "XSCREENSAVER_WINDOW");
}

static Window
create_test_window(Display *dpy, int screen,
                   const char *title, Atom *wm_delete)
{
  Window window = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                                      0, 0, 800, 600, 0,
                                      BlackPixel(dpy, screen),
                                      BlackPixel(dpy, screen));

  if (!window) {
    fprintf(stderr, "%s: unable to create test window\n", progname);
    exit(EXIT_FAILURE);
  }

  XStoreName(dpy, window, title);
  XSelectInput(dpy, window,
               ExposureMask | StructureNotifyMask |
               KeyPressMask | ButtonPressMask);

  *wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dpy, window, wm_delete, 1);
  XMapRaised(dpy, window);
  XSync(dpy, False);

  return window;
}

static void
sleep_microseconds(unsigned long microseconds)
{
  struct timespec requested;
  struct timespec remaining;

  requested.tv_sec = (time_t) (microseconds / 1000000UL);
  requested.tv_nsec = (long) (microseconds % 1000000UL) * 1000L;

  while (!stop_requested &&
         nanosleep(&requested, &remaining) != 0 && errno == EINTR)
    requested = remaining;
}

static void
install_signal_handlers(void)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);

  (void) sigaction(SIGINT, &action, NULL);
  (void) sigaction(SIGTERM, &action, NULL);
  (void) sigaction(SIGHUP, &action, NULL);
}

int
screenhack_standalone_main(int argc, char **argv,
                           const char *class_name,
                           const char *const *defaults,
                           const XrmOptionDescRec *module_options,
                           screenhack_init_fn init_cb,
                           screenhack_draw_fn draw_cb,
                           screenhack_reshape_fn reshape_cb,
                           screenhack_event_fn event_cb,
                           screenhack_free_fn free_cb)
{
  struct runner_options options;
  Display *dpy;
  Window window;
  Window environment_window;
  Atom wm_delete = None;
  void *closure;
  int screen;
  bool owns_window = false;
  unsigned int width = 0;
  unsigned int height = 0;
  struct timespec now;

  progname = (char *) program_basename(argv[0]);
  load_defaults(defaults);
  options = parse_options(argc, argv, module_options);

  dpy = XOpenDisplay(options.display_name);
  if (!dpy) {
    fprintf(stderr, "%s: unable to open display %s\n", progname,
            options.display_name ? options.display_name : "(default)");
    resources_free();
    return EXIT_FAILURE;
  }

  screen = DefaultScreen(dpy);
  environment_window = window_from_environment();

  if (options.have_requested_window) {
    window = options.requested_window;
  } else if (environment_window) {
    window = environment_window;
  } else if (options.root_p) {
    window = RootWindow(dpy, screen);
  } else {
    window = create_test_window(dpy, screen, class_name, &wm_delete);
    owns_window = true;
  }

  if (!owns_window && window != RootWindow(dpy, screen))
    XSelectInput(dpy, window, ExposureMask | StructureNotifyMask);

  if (clock_gettime(CLOCK_REALTIME, &now) == 0)
    srandom((unsigned int) now.tv_sec ^ (unsigned int) now.tv_nsec ^
            (unsigned int) getpid());
  else
    srandom((unsigned int) getpid());

  install_signal_handlers();
  closure = init_cb(dpy, window);
  if (!closure) {
    fprintf(stderr, "%s: initialization failed\n", progname);
    if (owns_window)
      XDestroyWindow(dpy, window);
    XCloseDisplay(dpy);
    resources_free();
    return EXIT_FAILURE;
  }

  while (!stop_requested) {
    unsigned long delay;

    while (XPending(dpy)) {
      XEvent event;

      XNextEvent(dpy, &event);
      if (event_cb)
        (void) event_cb(dpy, window, closure, &event);

      switch (event.type) {
      case ConfigureNotify:
        if ((unsigned int) event.xconfigure.width != width ||
            (unsigned int) event.xconfigure.height != height) {
          width = (unsigned int) event.xconfigure.width;
          height = (unsigned int) event.xconfigure.height;
          if (reshape_cb)
            reshape_cb(dpy, window, closure, width, height);
        }
        break;

      case ClientMessage:
        if (owns_window && wm_delete != None &&
            (Atom) event.xclient.data.l[0] == wm_delete)
          stop_requested = 1;
        break;

      case DestroyNotify:
        stop_requested = 1;
        break;

      case KeyPress:
      case ButtonPress:
        if (owns_window)
          stop_requested = 1;
        break;

      default:
        break;
      }
    }

    if (stop_requested)
      break;

    delay = draw_cb(dpy, window, closure);
    XFlush(dpy);
    sleep_microseconds(delay);
  }

  free_cb(dpy, window, closure);
  if (owns_window)
    XDestroyWindow(dpy, window);
  XCloseDisplay(dpy);
  resources_free();

  return EXIT_SUCCESS;
}
