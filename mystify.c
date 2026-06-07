/*
 * SPDX-License-Identifier: X11
 * Copyright (C) 2026 Reaper <JohnGrimmReaper@disroot.org>
 */

/*
 * Mystify for XScreenSaver
 *
 * Copyright (c) 2026 Reaper <JohnGrimmReaper@disroot.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME "mystify"
#define PROGRAM_VERSION "0.1.0"

#define DEFAULT_DELAY_US 30000U
#define DEFAULT_POLYS 2
#define DEFAULT_POINTS 4
#define DEFAULT_TRAILS 5
#define DEFAULT_SPEED 14
#define DEFAULT_THICKNESS 1
#define DEFAULT_COLORS 64

#define MIN_POLYS 1
#define MAX_POLYS 16
#define MIN_POINTS 3
#define MAX_POINTS 32
#define MIN_TRAILS 0
#define MAX_TRAILS 64
#define MIN_SPEED 1
#define MAX_SPEED 128
#define MIN_THICKNESS 1
#define MAX_THICKNESS 16
#define MIN_COLORS 8
#define MAX_COLORS 256

struct options {
    const char *display_name;
    Window requested_window;
    bool have_requested_window;
    bool root;
    bool create_window;
    bool self_test;
    unsigned int delay_us;
    int polygons;
    int points;
    int trails;
    int speed;
    int thickness;
    int colors;
    unsigned int seed;
    bool seed_set;
    unsigned long frame_limit;
};

struct wire {
    XPoint *history;
    XPoint *velocity;
    int slots;
    int head;
    int filled;
    int color_index;
    int color_step;
};

struct app {
    Display *dpy;
    int screen;
    Window window;
    bool owns_window;
    Atom wm_delete;
    Visual *visual;
    Colormap colormap;
    int depth;
    unsigned int width;
    unsigned int height;
    Pixmap backing;
    GC draw_gc;
    GC erase_gc;
    unsigned long black;
    unsigned long *palette;
    int palette_count;
    struct wire *wires;
    struct options opt;
};

static volatile sig_atomic_t stop_requested = 0;
static uint32_t rng_state = 1U;

static void on_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static uint32_t rng_next(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x ? x : 0x6d2b79f5U;
    return rng_state;
}

static int rng_range(int low, int high)
{
    uint32_t span;

    if (high <= low)
        return low;
    span = (uint32_t)(high - low + 1);
    return low + (int)(rng_next() % span);
}

/* A triangular distribution keeps most speeds moderate while still allowing
 * the occasional slow or fast corner. This captures the irregular, organic
 * bounce changes seen in the legacy saver without floating-point arithmetic. */
static int random_speed(int maximum)
{
    int minimum = maximum / 6;
    int span;
    int a;
    int b;

    if (minimum < 1)
        minimum = 1;
    if (maximum < minimum)
        maximum = minimum;

    span = maximum - minimum;
    a = rng_range(0, span);
    b = rng_range(0, span);
    return minimum + ((a + b) / 2);
}

static int random_velocity(int maximum)
{
    int magnitude = random_speed(maximum);
    return (rng_next() & 1U) ? magnitude : -magnitude;
}

static int clamp_int(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static unsigned long parse_ulong(const char *text, const char *what)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "%s: invalid %s: %s\n", PROGRAM_NAME, what, text);
        exit(EXIT_FAILURE);
    }
    return value;
}

static int parse_int_option(const char *text, const char *what, int low, int high)
{
    unsigned long value = parse_ulong(text, what);

    if (value > (unsigned long)INT_MAX || (int)value < low || (int)value > high) {
        fprintf(stderr, "%s: %s must be between %d and %d\n",
                PROGRAM_NAME, what, low, high);
        exit(EXIT_FAILURE);
    }
    return (int)value;
}

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage: %s [options]\n"
        "  -display DISPLAY       X display\n"
        "  -root                  draw on the root window\n"
        "  -window                create a normal test window\n"
        "  -window-id ID          draw into an existing window\n"
        "  -delay USEC            frame delay (default %u)\n"
        "  -polys N               polygons (default %d)\n"
        "  -points N              corners per polygon (default %d)\n"
        "  -trails N              retained old wires (default %d)\n"
        "  -speed N               maximum speed (default %d)\n"
        "  -thickness N           line width (default %d)\n"
        "  -colors N              hue palette size (default %d)\n"
        "  -seed N                deterministic random seed\n"
        "  -frames N              stop after N frames\n"
        "  -self-test             run internal tests without X11\n"
        "  -version               print version\n"
        "  -help                  show this help\n",
        PROGRAM_NAME, DEFAULT_DELAY_US, DEFAULT_POLYS, DEFAULT_POINTS,
        DEFAULT_TRAILS, DEFAULT_SPEED, DEFAULT_THICKNESS, DEFAULT_COLORS);
}

static const char *need_argument(int argc, char **argv, int *index)
{
    if (*index + 1 >= argc) {
        fprintf(stderr, "%s: option %s requires an argument\n",
                PROGRAM_NAME, argv[*index]);
        exit(EXIT_FAILURE);
    }
    ++(*index);
    return argv[*index];
}

static struct options parse_options(int argc, char **argv)
{
    struct options opt = {
        .display_name = NULL,
        .requested_window = 0,
        .have_requested_window = false,
        .root = false,
        .create_window = false,
        .self_test = false,
        .delay_us = DEFAULT_DELAY_US,
        .polygons = DEFAULT_POLYS,
        .points = DEFAULT_POINTS,
        .trails = DEFAULT_TRAILS,
        .speed = DEFAULT_SPEED,
        .thickness = DEFAULT_THICKNESS,
        .colors = DEFAULT_COLORS,
        .seed = 0,
        .seed_set = false,
        .frame_limit = 0
    };
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!strcmp(arg, "-display") || !strcmp(arg, "--display")) {
            opt.display_name = need_argument(argc, argv, &i);
        } else if (!strcmp(arg, "-root") || !strcmp(arg, "--root")) {
            opt.root = true;
        } else if (!strcmp(arg, "-window") || !strcmp(arg, "--window")) {
            opt.create_window = true;
        } else if (!strcmp(arg, "-window-id") || !strcmp(arg, "--window-id") ||
                   !strcmp(arg, "-wid")) {
            opt.requested_window = (Window)parse_ulong(
                need_argument(argc, argv, &i), "window id");
            opt.have_requested_window = true;
        } else if (!strcmp(arg, "-delay") || !strcmp(arg, "--delay")) {
            unsigned long value = parse_ulong(need_argument(argc, argv, &i), "delay");
            if (value < 1000UL || value > 10000000UL) {
                fprintf(stderr, "%s: delay must be between 1000 and 10000000 usec\n",
                        PROGRAM_NAME);
                exit(EXIT_FAILURE);
            }
            opt.delay_us = (unsigned int)value;
        } else if (!strcmp(arg, "-polys") || !strcmp(arg, "--polys")) {
            opt.polygons = parse_int_option(need_argument(argc, argv, &i),
                                            "polys", MIN_POLYS, MAX_POLYS);
        } else if (!strcmp(arg, "-points") || !strcmp(arg, "--points")) {
            opt.points = parse_int_option(need_argument(argc, argv, &i),
                                          "points", MIN_POINTS, MAX_POINTS);
        } else if (!strcmp(arg, "-trails") || !strcmp(arg, "--trails")) {
            opt.trails = parse_int_option(need_argument(argc, argv, &i),
                                          "trails", MIN_TRAILS, MAX_TRAILS);
        } else if (!strcmp(arg, "-speed") || !strcmp(arg, "--speed")) {
            opt.speed = parse_int_option(need_argument(argc, argv, &i),
                                         "speed", MIN_SPEED, MAX_SPEED);
        } else if (!strcmp(arg, "-thickness") || !strcmp(arg, "--thickness")) {
            opt.thickness = parse_int_option(need_argument(argc, argv, &i),
                                             "thickness", MIN_THICKNESS,
                                             MAX_THICKNESS);
        } else if (!strcmp(arg, "-colors") || !strcmp(arg, "--colors")) {
            opt.colors = parse_int_option(need_argument(argc, argv, &i),
                                          "colors", MIN_COLORS, MAX_COLORS);
        } else if (!strcmp(arg, "-seed") || !strcmp(arg, "--seed")) {
            opt.seed = (unsigned int)parse_ulong(need_argument(argc, argv, &i), "seed");
            opt.seed_set = true;
        } else if (!strcmp(arg, "-frames") || !strcmp(arg, "--frames")) {
            opt.frame_limit = parse_ulong(need_argument(argc, argv, &i), "frames");
        } else if (!strcmp(arg, "-self-test") || !strcmp(arg, "--self-test")) {
            opt.self_test = true;
        } else if (!strcmp(arg, "-version") || !strcmp(arg, "--version")) {
            printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
            exit(EXIT_SUCCESS);
        } else if (!strcmp(arg, "-help") || !strcmp(arg, "--help") ||
                   !strcmp(arg, "-h")) {
            usage(stdout);
            exit(EXIT_SUCCESS);
        } else if (!strcmp(arg, "-no-db") || !strcmp(arg, "-db") ||
                   !strcmp(arg, "-fps") || !strcmp(arg, "-no-fps") ||
                   !strcmp(arg, "-visual")) {
            /* Compatibility options accepted by many XScreenSaver launchers.
             * -visual consumes one argument; the others are harmless here. */
            if (!strcmp(arg, "-visual"))
                (void)need_argument(argc, argv, &i);
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", PROGRAM_NAME, arg);
            usage(stderr);
            exit(EXIT_FAILURE);
        }
    }

    return opt;
}

static unsigned long component_mask_to_pixel(unsigned short component,
                                              unsigned long mask)
{
    unsigned int shift = 0;
    unsigned long normalized;
    unsigned long max_value;

    if (mask == 0)
        return 0;
    while (((mask >> shift) & 1UL) == 0UL)
        ++shift;
    normalized = mask >> shift;
    max_value = normalized;
    return (((unsigned long)component * max_value + 32767UL) / 65535UL) << shift;
}

static void hsv_to_rgb16(unsigned int hue, unsigned int count,
                         unsigned short *red, unsigned short *green,
                         unsigned short *blue)
{
    /* Integer HSV with saturation and value fixed at 100%. */
    unsigned int h6 = (hue % count) * 6U * 65536U / count;
    unsigned int sector = h6 >> 16;
    unsigned int fraction = h6 & 0xffffU;
    unsigned int rising = fraction;
    unsigned int falling = 65535U - fraction;

    switch (sector % 6U) {
    case 0:
        *red = 65535; *green = (unsigned short)rising; *blue = 0; break;
    case 1:
        *red = (unsigned short)falling; *green = 65535; *blue = 0; break;
    case 2:
        *red = 0; *green = 65535; *blue = (unsigned short)rising; break;
    case 3:
        *red = 0; *green = (unsigned short)falling; *blue = 65535; break;
    case 4:
        *red = (unsigned short)rising; *green = 0; *blue = 65535; break;
    default:
        *red = 65535; *green = 0; *blue = (unsigned short)falling; break;
    }
}

static unsigned long direct_color_pixel(const struct app *app,
                                        unsigned short red,
                                        unsigned short green,
                                        unsigned short blue)
{
    return component_mask_to_pixel(red, app->visual->red_mask) |
           component_mask_to_pixel(green, app->visual->green_mask) |
           component_mask_to_pixel(blue, app->visual->blue_mask);
}

static void allocate_palette(struct app *app)
{
    int requested = app->opt.colors;
    int allocated = 0;
    int i;

    app->palette = calloc((size_t)requested, sizeof(*app->palette));
    if (!app->palette) {
        perror("calloc palette");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < requested; ++i) {
        unsigned short red;
        unsigned short green;
        unsigned short blue;
        XColor color;

        hsv_to_rgb16((unsigned int)i, (unsigned int)requested, &red, &green, &blue);
        color.red = red;
        color.green = green;
        color.blue = blue;
        color.flags = DoRed | DoGreen | DoBlue;

        if (app->visual->class == TrueColor || app->visual->class == DirectColor) {
            app->palette[allocated++] = direct_color_pixel(app, red, green, blue);
        } else if (XAllocColor(app->dpy, app->colormap, &color)) {
            app->palette[allocated++] = color.pixel;
        }
    }

    if (allocated < 2) {
        fprintf(stderr, "%s: unable to allocate a usable color palette\n", PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }
    app->palette_count = allocated;
}

static XPoint *wire_slot(const struct app *app, struct wire *wire, int slot)
{
    size_t stride = (size_t)app->opt.points + 1U;
    return wire->history + ((size_t)slot * stride);
}

static void close_polygon(const struct app *app, XPoint *points)
{
    points[app->opt.points] = points[0];
}

static void initialize_wire(struct app *app, struct wire *wire, int index)
{
    size_t point_count = (size_t)app->opt.points + 1U;
    size_t history_points;
    XPoint *initial;
    int i;

    wire->slots = app->opt.trails + 1;
    wire->head = 0;
    wire->filled = 1;
    wire->color_index = (index * app->palette_count) / app->opt.polygons;
    wire->color_step = (index & 1) ? 1 : -1;

    history_points = (size_t)wire->slots * point_count;
    wire->history = calloc(history_points, sizeof(*wire->history));
    wire->velocity = calloc((size_t)app->opt.points, sizeof(*wire->velocity));
    if (!wire->history || !wire->velocity) {
        perror("calloc wire");
        exit(EXIT_FAILURE);
    }

    initial = wire_slot(app, wire, 0);
    for (i = 0; i < app->opt.points; ++i) {
        initial[i].x = (short)rng_range(0, (int)app->width - 1);
        initial[i].y = (short)rng_range(0, (int)app->height - 1);
        wire->velocity[i].x = (short)random_velocity(app->opt.speed);
        wire->velocity[i].y = (short)random_velocity(app->opt.speed);
    }
    close_polygon(app, initial);

    for (i = 1; i < wire->slots; ++i)
        memcpy(wire_slot(app, wire, i), initial, point_count * sizeof(*initial));
}

static void free_wires(struct app *app)
{
    int i;

    if (!app->wires)
        return;
    for (i = 0; i < app->opt.polygons; ++i) {
        free(app->wires[i].history);
        free(app->wires[i].velocity);
    }
    free(app->wires);
    app->wires = NULL;
}

static void create_wires(struct app *app)
{
    int i;

    app->wires = calloc((size_t)app->opt.polygons, sizeof(*app->wires));
    if (!app->wires) {
        perror("calloc wires");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < app->opt.polygons; ++i)
        initialize_wire(app, &app->wires[i], i);
}

static int advance_coordinate(int old_position, short *velocity,
                              int limit, int maximum_speed)
{
    int next;

    if (limit <= 1) {
        *velocity = 0;
        return 0;
    }

    next = old_position + *velocity;
    if (next < 0) {
        next = -next;
        *velocity = (short)random_speed(maximum_speed);
    } else if (next >= limit) {
        next = ((limit - 1) * 2) - next;
        *velocity = (short)-random_speed(maximum_speed);
    }

    return clamp_int(next, 0, limit - 1);
}

static void draw_polygon(struct app *app, Drawable target, GC gc,
                         const XPoint *points)
{
    XDrawLines(app->dpy, target, gc, (XPoint *)points,
               app->opt.points + 1, CoordModeOrigin);
}

static void draw_to_both(struct app *app, GC gc, const XPoint *points)
{
    draw_polygon(app, app->backing, gc, points);
    draw_polygon(app, app->window, gc, points);
}

static void advance_wire(struct app *app, struct wire *wire)
{
    int next_slot = (wire->head + 1) % wire->slots;
    XPoint *previous = wire_slot(app, wire, wire->head);
    XPoint *next = wire_slot(app, wire, next_slot);
    int i;

    if (wire->filled == wire->slots)
        draw_to_both(app, app->erase_gc, next);

    for (i = 0; i < app->opt.points; ++i) {
        next[i].x = (short)advance_coordinate(previous[i].x,
                                               &wire->velocity[i].x,
                                               (int)app->width,
                                               app->opt.speed);
        next[i].y = (short)advance_coordinate(previous[i].y,
                                               &wire->velocity[i].y,
                                               (int)app->height,
                                               app->opt.speed);
    }
    close_polygon(app, next);

    wire->head = next_slot;
    if (wire->filled < wire->slots)
        ++wire->filled;

    wire->color_index += wire->color_step;
    if (wire->color_index < 0)
        wire->color_index = app->palette_count - 1;
    else if (wire->color_index >= app->palette_count)
        wire->color_index = 0;

    XSetForeground(app->dpy, app->draw_gc, app->palette[wire->color_index]);
    draw_to_both(app, app->draw_gc, next);
}

static void clear_canvas(struct app *app)
{
    XFillRectangle(app->dpy, app->backing, app->erase_gc,
                   0, 0, app->width, app->height);
    XFillRectangle(app->dpy, app->window, app->erase_gc,
                   0, 0, app->width, app->height);
}

static void recreate_backing(struct app *app, unsigned int width,
                             unsigned int height)
{
    if (width == 0 || height == 0)
        return;

    if (app->backing)
        XFreePixmap(app->dpy, app->backing);
    app->width = width;
    app->height = height;
    app->backing = XCreatePixmap(app->dpy, app->window, width, height,
                                 (unsigned int)app->depth);
    if (!app->backing) {
        fprintf(stderr, "%s: unable to create backing pixmap\n", PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    free_wires(app);
    create_wires(app);
    clear_canvas(app);
}

static Window window_from_environment(void)
{
    const char *value = getenv("XSCREENSAVER_WINDOW");

    if (!value || !*value)
        return 0;
    return (Window)parse_ulong(value, "XSCREENSAVER_WINDOW");
}

static Window create_test_window(struct app *app)
{
    XSetWindowAttributes attrs;
    unsigned long mask = CWBackPixel | CWEventMask;
    Window window;

    attrs.background_pixel = app->black;
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                       ButtonPressMask;
    window = XCreateWindow(app->dpy, RootWindow(app->dpy, app->screen),
                           0, 0, 800, 600, 0, app->depth, InputOutput,
                           app->visual, mask, &attrs);
    if (!window) {
        fprintf(stderr, "%s: unable to create test window\n", PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    XStoreName(app->dpy, window, "Mystify for XScreenSaver");
    app->wm_delete = XInternAtom(app->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(app->dpy, window, &app->wm_delete, 1);
    XMapRaised(app->dpy, window);
    app->owns_window = true;
    return window;
}

static void setup_x(struct app *app)
{
    XWindowAttributes attrs;
    XGCValues values;
    Window env_window;

    app->dpy = XOpenDisplay(app->opt.display_name);
    if (!app->dpy) {
        fprintf(stderr, "%s: unable to open display %s\n", PROGRAM_NAME,
                app->opt.display_name ? app->opt.display_name : "(default)");
        exit(EXIT_FAILURE);
    }

    app->screen = DefaultScreen(app->dpy);
    app->visual = DefaultVisual(app->dpy, app->screen);
    app->colormap = DefaultColormap(app->dpy, app->screen);
    app->depth = DefaultDepth(app->dpy, app->screen);
    app->black = BlackPixel(app->dpy, app->screen);

    env_window = window_from_environment();
    if (app->opt.have_requested_window) {
        app->window = app->opt.requested_window;
    } else if (env_window) {
        app->window = env_window;
    } else if (app->opt.root) {
        app->window = RootWindow(app->dpy, app->screen);
    } else {
        app->window = create_test_window(app);
    }

    if (!XGetWindowAttributes(app->dpy, app->window, &attrs)) {
        fprintf(stderr, "%s: cannot query target window 0x%lx\n",
                PROGRAM_NAME, (unsigned long)app->window);
        exit(EXIT_FAILURE);
    }

    app->visual = attrs.visual;
    app->colormap = attrs.colormap;
    app->depth = attrs.depth;
    app->width = (unsigned int)attrs.width;
    app->height = (unsigned int)attrs.height;

    if (!app->opt.root) {
        XSelectInput(app->dpy, app->window,
                     ExposureMask | StructureNotifyMask | KeyPressMask |
                     ButtonPressMask);
    }

    allocate_palette(app);

    values.foreground = app->black;
    values.background = app->black;
    values.line_width = app->opt.thickness;
    values.line_style = LineSolid;
    values.cap_style = CapButt;
    values.join_style = JoinBevel;
    app->erase_gc = XCreateGC(app->dpy, app->window,
                              GCForeground | GCBackground | GCLineWidth |
                              GCLineStyle | GCCapStyle | GCJoinStyle, &values);

    values.foreground = app->palette[0];
    app->draw_gc = XCreateGC(app->dpy, app->window,
                             GCForeground | GCBackground | GCLineWidth |
                             GCLineStyle | GCCapStyle | GCJoinStyle, &values);
    if (!app->erase_gc || !app->draw_gc) {
        fprintf(stderr, "%s: unable to create X graphics contexts\n", PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    app->backing = 0;
    app->wires = NULL;
    recreate_backing(app, app->width, app->height);
    XFlush(app->dpy);
}

static void copy_exposed_area(struct app *app, const XExposeEvent *expose)
{
    XCopyArea(app->dpy, app->backing, app->window, app->draw_gc,
              expose->x, expose->y,
              (unsigned int)expose->width, (unsigned int)expose->height,
              expose->x, expose->y);
}

static void process_events(struct app *app)
{
    while (XPending(app->dpy)) {
        XEvent event;
        XNextEvent(app->dpy, &event);

        switch (event.type) {
        case Expose:
            copy_exposed_area(app, &event.xexpose);
            break;
        case ConfigureNotify:
            if ((unsigned int)event.xconfigure.width != app->width ||
                (unsigned int)event.xconfigure.height != app->height) {
                recreate_backing(app,
                                 (unsigned int)event.xconfigure.width,
                                 (unsigned int)event.xconfigure.height);
            }
            break;
        case ClientMessage:
            if (app->owns_window &&
                (Atom)event.xclient.data.l[0] == app->wm_delete)
                stop_requested = 1;
            break;
        case DestroyNotify:
            stop_requested = 1;
            break;
        case KeyPress:
        case ButtonPress:
            if (app->owns_window)
                stop_requested = 1;
            break;
        default:
            break;
        }
    }
}

static void sleep_delay(unsigned int microseconds)
{
    struct timespec requested;
    struct timespec remaining;

    requested.tv_sec = (time_t)(microseconds / 1000000U);
    requested.tv_nsec = (long)(microseconds % 1000000U) * 1000L;
    while (nanosleep(&requested, &remaining) != 0 && errno == EINTR) {
        if (stop_requested)
            return;
        requested = remaining;
    }
}

static void run_animation(struct app *app)
{
    unsigned long frame = 0;
    int i;

    while (!stop_requested &&
           (app->opt.frame_limit == 0 || frame < app->opt.frame_limit)) {
        process_events(app);
        for (i = 0; i < app->opt.polygons; ++i)
            advance_wire(app, &app->wires[i]);
        XFlush(app->dpy);
        ++frame;
        sleep_delay(app->opt.delay_us);
    }
}

static void cleanup(struct app *app)
{
    free_wires(app);
    free(app->palette);
    if (app->backing)
        XFreePixmap(app->dpy, app->backing);
    if (app->draw_gc)
        XFreeGC(app->dpy, app->draw_gc);
    if (app->erase_gc)
        XFreeGC(app->dpy, app->erase_gc);
    if (app->owns_window && app->window)
        XDestroyWindow(app->dpy, app->window);
    if (app->dpy)
        XCloseDisplay(app->dpy);
}

static int self_test(void)
{
    short velocity;
    int position;
    int i;

    rng_state = 0x12345678U;
    for (i = 0; i < 10000; ++i) {
        int speed = random_speed(17);
        if (speed < 2 || speed > 17) {
            fprintf(stderr, "self-test: random speed out of range: %d\n", speed);
            return EXIT_FAILURE;
        }
    }

    velocity = -7;
    position = advance_coordinate(2, &velocity, 100, 17);
    if (position != 5 || velocity <= 0) {
        fprintf(stderr, "self-test: left reflection failed\n");
        return EXIT_FAILURE;
    }

    velocity = 7;
    position = advance_coordinate(97, &velocity, 100, 17);
    if (position != 94 || velocity >= 0) {
        fprintf(stderr, "self-test: right reflection failed\n");
        return EXIT_FAILURE;
    }

    puts("mystify self-test: PASS");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    struct app app;
    struct sigaction action;

    memset(&app, 0, sizeof(app));
    app.opt = parse_options(argc, argv);
    if (app.opt.self_test)
        return self_test();

    if (app.opt.seed_set) {
        rng_state = app.opt.seed ? app.opt.seed : 1U;
    } else {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        rng_state = (uint32_t)now.tv_nsec ^ (uint32_t)now.tv_sec ^
                    (uint32_t)getpid();
        if (!rng_state)
            rng_state = 1U;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);

    setup_x(&app);
    run_animation(&app);
    cleanup(&app);
    return EXIT_SUCCESS;
}
