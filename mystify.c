/* xscreensaver, Copyright (c) 2026 Reaper <JohnGrimmReaper@disroot.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

#include "screenhack.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_POLYGONS  1
#define MAX_POLYGONS 16
#define MIN_POINTS    3
#define MAX_POINTS   32
#define MIN_TRAILS    0
#define MAX_TRAILS   64
#define MIN_SPEED     1
#define MAX_SPEED   128
#define MIN_THICKNESS 1
#define MAX_THICKNESS 16
#define MIN_COLORS    8
#define MAX_COLORS  256


typedef struct {
  int x;
  int y;
} mystify_point;

typedef struct {
  int polygons;
  int points;
  int trails;
  int speed;
  int colors;
} mystify_configuration;

typedef struct mystify_state mystify_state;

/* Description of one polygon update.
 *
 * erase_points is the oldest polygon to erase when erase_p is non-zero.
 * draw_points is the newly advanced polygon to draw.
 *
 * Both arrays contain point_count points.  The renderer closes the polygon
 * by connecting the final point back to the first.
 */
typedef struct {
  const mystify_point *erase_points;
  const mystify_point *draw_points;
  int point_count;
  int color_index;
  int erase_p;
} mystify_frame;

static mystify_state *
mystify_state_create (const mystify_configuration *,
                      unsigned int width,
                      unsigned int height,
                      uint32_t seed);

static int
mystify_state_reset (mystify_state *,
                     unsigned int width,
                     unsigned int height);

static void
mystify_state_free (mystify_state *);

static int
mystify_state_polygon_count (const mystify_state *);

static void
mystify_state_step_polygon (mystify_state *,
                            int polygon,
                            mystify_frame *);

static void
mystify_hsv_to_rgb16 (unsigned int hue,
                      unsigned int count,
                      unsigned short *red,
                      unsigned short *green,
                      unsigned short *blue);


typedef struct {
  mystify_point *history;
  mystify_point *velocity;
  mystify_point *erase_points;
  int slots;
  int head;
  int filled;
  int color_index;
  int color_step;
} mystify_wire;

struct mystify_state {
  mystify_configuration configuration;
  unsigned int width;
  unsigned int height;
  uint32_t rng_state;
  mystify_wire *wires;
};


static uint32_t
mystify_rng_next (mystify_state *state)
{
  uint32_t x = state->rng_state;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;

  state->rng_state = (x ? x : 0x6d2b79f5U);
  return state->rng_state;
}


static int
mystify_rng_range (mystify_state *state, int low, int high)
{
  uint32_t span;

  if (high <= low)
    return low;

  span = (uint32_t) (high - low + 1);
  return low + (int) (mystify_rng_next (state) % span);
}


/* A triangular distribution keeps most speeds moderate while still allowing
   the occasional slow or fast corner. */
static int
mystify_random_speed (mystify_state *state, int maximum)
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
  a = mystify_rng_range (state, 0, span);
  b = mystify_rng_range (state, 0, span);

  return minimum + ((a + b) / 2);
}


static int
mystify_random_velocity (mystify_state *state, int maximum)
{
  int magnitude = mystify_random_speed (state, maximum);

  return ((mystify_rng_next (state) & 1U)
          ? magnitude
          : -magnitude);
}


static int
mystify_clamp (int value, int low, int high)
{
  if (value < low)
    return low;
  if (value > high)
    return high;

  return value;
}


static mystify_point *
mystify_wire_slot (const mystify_state *state,
                    mystify_wire *wire,
                    int slot)
{
  return wire->history +
    ((size_t) slot * (size_t) state->configuration.points);
}


static void
mystify_wire_free (mystify_wire *wire)
{
  if (!wire)
    return;

  free (wire->history);
  free (wire->velocity);
  free (wire->erase_points);

  memset (wire, 0, sizeof (*wire));
}


static int
mystify_wire_initialize (mystify_state *state,
                          mystify_wire *wire,
                          int index)
{
  const mystify_configuration *configuration = &state->configuration;
  size_t history_points;
  mystify_point *initial;
  int i;

  wire->slots = configuration->trails + 1;
  wire->head = 0;
  wire->filled = 1;
  wire->color_index =
    (index * configuration->colors) / configuration->polygons;
  wire->color_step = ((index & 1) ? 1 : -1);

  history_points =
    (size_t) wire->slots * (size_t) configuration->points;

  wire->history = (mystify_point *)
    calloc (history_points, sizeof (*wire->history));
  wire->velocity = (mystify_point *)
    calloc ((size_t) configuration->points, sizeof (*wire->velocity));
  wire->erase_points = (mystify_point *)
    calloc ((size_t) configuration->points, sizeof (*wire->erase_points));

  if (!wire->history || !wire->velocity || !wire->erase_points)
    {
      mystify_wire_free (wire);
      return 0;
    }

  initial = mystify_wire_slot (state, wire, 0);

  for (i = 0; i < configuration->points; i++)
    {
      initial[i].x =
        mystify_rng_range (state, 0, (int) state->width - 1);
      initial[i].y =
        mystify_rng_range (state, 0, (int) state->height - 1);

      wire->velocity[i].x =
        mystify_random_velocity (state, configuration->speed);
      wire->velocity[i].y =
        mystify_random_velocity (state, configuration->speed);
    }

  for (i = 1; i < wire->slots; i++)
    memcpy (mystify_wire_slot (state, wire, i),
            initial,
            (size_t) configuration->points * sizeof (*initial));

  return 1;
}


static int
mystify_advance_coordinate (mystify_state *state,
                             int old_position,
                             int *velocity,
                             int limit)
{
  int next;

  if (limit <= 1)
    {
      *velocity = 0;
      return 0;
    }

  next = old_position + *velocity;

  if (next < 0)
    {
      next = -next;
      *velocity =
        mystify_random_speed (state, state->configuration.speed);
    }
  else if (next >= limit)
    {
      next = ((limit - 1) * 2) - next;
      *velocity =
        -mystify_random_speed (state, state->configuration.speed);
    }

  return mystify_clamp (next, 0, limit - 1);
}


static void
mystify_wires_free (mystify_state *state)
{
  int i;

  if (!state || !state->wires)
    return;

  for (i = 0; i < state->configuration.polygons; i++)
    mystify_wire_free (&state->wires[i]);

  free (state->wires);
  state->wires = 0;
}


static int
mystify_wires_create (mystify_state *state)
{
  int i;

  state->wires = (mystify_wire *)
    calloc ((size_t) state->configuration.polygons,
            sizeof (*state->wires));

  if (!state->wires)
    return 0;

  for (i = 0; i < state->configuration.polygons; i++)
    if (!mystify_wire_initialize (state, &state->wires[i], i))
      {
        mystify_wires_free (state);
        return 0;
      }

  return 1;
}


static mystify_state *
mystify_state_create (const mystify_configuration *configuration,
                       unsigned int width,
                       unsigned int height,
                       uint32_t seed)
{
  mystify_state *state;

  if (!configuration ||
      configuration->polygons < 1 ||
      configuration->points < 3 ||
      configuration->trails < 0 ||
      configuration->trails == INT_MAX ||
      configuration->speed < 1 ||
      configuration->colors < 2 ||
      width == 0 ||
      height == 0 ||
      width > INT_MAX ||
      height > INT_MAX)
    return 0;

  state = (mystify_state *) calloc (1, sizeof (*state));
  if (!state)
    return 0;

  state->configuration = *configuration;
  state->rng_state = (seed ? seed : 1U);

  if (!mystify_state_reset (state, width, height))
    {
      free (state);
      return 0;
    }

  return state;
}


static int
mystify_state_reset (mystify_state *state,
                      unsigned int width,
                      unsigned int height)
{
  if (!state ||
      width == 0 ||
      height == 0 ||
      width > INT_MAX ||
      height > INT_MAX)
    return 0;

  mystify_wires_free (state);

  state->width = width;
  state->height = height;

  return mystify_wires_create (state);
}


static void
mystify_state_free (mystify_state *state)
{
  if (!state)
    return;

  mystify_wires_free (state);
  free (state);
}


static int
mystify_state_polygon_count (const mystify_state *state)
{
  return (state ? state->configuration.polygons : 0);
}



static void
mystify_state_step_polygon (mystify_state *state,
                             int polygon,
                             mystify_frame *frame)
{
  mystify_wire *wire;
  mystify_point *previous;
  mystify_point *next;
  int next_slot;
  int i;

  if (!frame)
    return;

  memset (frame, 0, sizeof (*frame));

  if (!state ||
      polygon < 0 ||
      polygon >= state->configuration.polygons)
    return;

  wire = &state->wires[polygon];
  next_slot = (wire->head + 1) % wire->slots;
  previous = mystify_wire_slot (state, wire, wire->head);
  next = mystify_wire_slot (state, wire, next_slot);

  if (wire->filled == wire->slots)
    {
      memcpy (wire->erase_points,
              next,
              (size_t) state->configuration.points * sizeof (*next));

      frame->erase_points = wire->erase_points;
      frame->erase_p = 1;
    }

  for (i = 0; i < state->configuration.points; i++)
    {
      next[i].x =
        mystify_advance_coordinate (state,
                                    previous[i].x,
                                    &wire->velocity[i].x,
                                    (int) state->width);

      next[i].y =
        mystify_advance_coordinate (state,
                                    previous[i].y,
                                    &wire->velocity[i].y,
                                    (int) state->height);
    }

  wire->head = next_slot;

  if (wire->filled < wire->slots)
    wire->filled++;

  wire->color_index += wire->color_step;

  if (wire->color_index < 0)
    wire->color_index = state->configuration.colors - 1;
  else if (wire->color_index >= state->configuration.colors)
    wire->color_index = 0;

  frame->draw_points = next;
  frame->point_count = state->configuration.points;
  frame->color_index = wire->color_index;
}


static void
mystify_hsv_to_rgb16 (unsigned int hue,
                       unsigned int count,
                       unsigned short *red,
                       unsigned short *green,
                       unsigned short *blue)
{
  unsigned int h6;
  unsigned int sector;
  unsigned int fraction;
  unsigned int rising;
  unsigned int falling;

  if (!red || !green || !blue)
    return;

  if (count == 0)
    {
      *red = 0;
      *green = 0;
      *blue = 0;
      return;
    }

  h6 = (hue % count) * 6U * 65536U / count;
  sector = h6 >> 16;
  fraction = h6 & 0xffffU;
  rising = fraction;
  falling = 65535U - fraction;

  switch (sector % 6U)
    {
    case 0:
      *red = 65535;
      *green = (unsigned short) rising;
      *blue = 0;
      break;

    case 1:
      *red = (unsigned short) falling;
      *green = 65535;
      *blue = 0;
      break;

    case 2:
      *red = 0;
      *green = 65535;
      *blue = (unsigned short) rising;
      break;

    case 3:
      *red = 0;
      *green = (unsigned short) falling;
      *blue = 65535;
      break;

    case 4:
      *red = (unsigned short) rising;
      *green = 0;
      *blue = 65535;
      break;

    default:
      *red = 65535;
      *green = 0;
      *blue = (unsigned short) falling;
      break;
    }
}


struct mystify {
  Display *dpy;
  Window window;

  unsigned int width;
  unsigned int height;
  unsigned int depth;

  int delay;
  int thickness;

  Colormap colormap;
  Visual *visual;
  unsigned long black;

  Pixmap backing;
  GC draw_gc;
  GC erase_gc;

  unsigned long *palette;
  int palette_count;
  Bool allocated_colors_p;

  XPoint *xpoints;

  mystify_configuration configuration;
  mystify_state *simulation;
};


static int
clamp_integer (int value, int minimum, int maximum)
{
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;

  return value;
}


static unsigned long
component_mask_to_pixel (unsigned short component, unsigned long mask)
{
  unsigned int shift = 0;
  unsigned long normalized;
  unsigned long maximum;

  if (!mask)
    return 0;

  while (((mask >> shift) & 1UL) == 0)
    shift++;

  normalized = mask >> shift;
  maximum = normalized;

  return (((unsigned long) component * maximum + 32767UL) / 65535UL)
    << shift;
}


static unsigned long
direct_color_pixel (const struct mystify *state,
                    unsigned short red,
                    unsigned short green,
                    unsigned short blue)
{
  return (component_mask_to_pixel (red,   state->visual->red_mask) |
          component_mask_to_pixel (green, state->visual->green_mask) |
          component_mask_to_pixel (blue,  state->visual->blue_mask));
}


static void
allocate_palette (struct mystify *state, int requested)
{
  int allocated = 0;
  int i;

  state->palette = (unsigned long *)
    calloc ((size_t) requested, sizeof (*state->palette));

  if (!state->palette)
    abort ();

  state->allocated_colors_p =
    (state->visual->class != TrueColor &&
     state->visual->class != DirectColor);

  for (i = 0; i < requested; i++)
    {
      unsigned short red;
      unsigned short green;
      unsigned short blue;

      mystify_hsv_to_rgb16 ((unsigned int) i,
                            (unsigned int) requested,
                            &red, &green, &blue);

      if (!state->allocated_colors_p)
        {
          state->palette[allocated++] =
            direct_color_pixel (state, red, green, blue);
        }
      else
        {
          XColor color;

          color.red = red;
          color.green = green;
          color.blue = blue;
          color.flags = DoRed | DoGreen | DoBlue;

          if (XAllocColor (state->dpy, state->colormap, &color))
            state->palette[allocated++] = color.pixel;
        }
    }

  if (allocated < 2)
    {
      fprintf (stderr, "%s: unable to allocate a usable color palette\n",
               progname);
      abort ();
    }

  state->palette_count = allocated;
  state->configuration.colors = allocated;
}


static void
draw_polygon (struct mystify *state,
              Drawable drawable,
              GC gc,
              const mystify_point *points,
              int point_count)
{
  int i;

  for (i = 0; i < point_count; i++)
    {
      state->xpoints[i].x = (short) points[i].x;
      state->xpoints[i].y = (short) points[i].y;
    }

  state->xpoints[point_count] = state->xpoints[0];

  XDrawLines (state->dpy, drawable, gc,
              state->xpoints, point_count + 1, CoordModeOrigin);
}


static void
draw_to_canvas (struct mystify *state,
                GC gc,
                const mystify_point *points,
                int point_count)
{
  draw_polygon (state, state->backing, gc, points, point_count);
  draw_polygon (state, state->window,  gc, points, point_count);
}


static void
clear_canvas (struct mystify *state)
{
  XFillRectangle (state->dpy, state->backing, state->erase_gc,
                  0, 0, state->width, state->height);

  XFillRectangle (state->dpy, state->window, state->erase_gc,
                  0, 0, state->width, state->height);
}


static void
create_backing (struct mystify *state,
                unsigned int width,
                unsigned int height)
{
  if (state->backing)
    XFreePixmap (state->dpy, state->backing);

  state->width = width;
  state->height = height;

  state->backing =
    XCreatePixmap (state->dpy, state->window,
                   width, height, state->depth);

  if (!state->backing)
    {
      fprintf (stderr, "%s: unable to create backing pixmap\n", progname);
      abort ();
    }

  clear_canvas (state);
}


static void
render_frame (struct mystify *state, const mystify_frame *frame)
{
  if (frame->erase_p)
    draw_to_canvas (state, state->erase_gc,
                    frame->erase_points, frame->point_count);

  XSetForeground (state->dpy, state->draw_gc,
                  state->palette[frame->color_index]);

  draw_to_canvas (state, state->draw_gc,
                  frame->draw_points, frame->point_count);
}


static void *
mystify_init (Display *dpy, Window window)
{
  struct mystify *state =
    (struct mystify *) calloc (1, sizeof (*state));

  XWindowAttributes attributes;
  XGCValues values;
  int requested_colors;
  int seed_resource;
  uint32_t seed;

  if (!state)
    abort ();

  state->dpy = dpy;
  state->window = window;

  XGetWindowAttributes (dpy, window, &attributes);

  state->width = (unsigned int) attributes.width;
  state->height = (unsigned int) attributes.height;
  state->depth = (unsigned int) attributes.depth;
  state->visual = attributes.visual;
  state->colormap = attributes.colormap;
  state->black = BlackPixelOfScreen (attributes.screen);

  state->delay =
    clamp_integer (get_integer_resource (dpy, "delay", "Integer"),
                   0, INT_MAX);

  state->configuration.polygons =
    clamp_integer (get_integer_resource (dpy, "polygons", "Integer"),
                   MIN_POLYGONS, MAX_POLYGONS);

  state->configuration.points =
    clamp_integer (get_integer_resource (dpy, "points", "Integer"),
                   MIN_POINTS, MAX_POINTS);

  state->configuration.trails =
    clamp_integer (get_integer_resource (dpy, "trails", "Integer"),
                   MIN_TRAILS, MAX_TRAILS);

  state->configuration.speed =
    clamp_integer (get_integer_resource (dpy, "speed", "Integer"),
                   MIN_SPEED, MAX_SPEED);

  state->thickness =
    clamp_integer (get_integer_resource (dpy, "thickness", "Integer"),
                   MIN_THICKNESS, MAX_THICKNESS);

  requested_colors =
    clamp_integer (get_integer_resource (dpy, "colors", "Integer"),
                   MIN_COLORS, MAX_COLORS);

  seed_resource = get_integer_resource (dpy, "seed", "Integer");
  seed = (seed_resource > 0
          ? (uint32_t) seed_resource
          : (uint32_t) random ());

  if (!seed)
    seed = 1;

  allocate_palette (state, requested_colors);

  values.foreground = state->black;
  values.background = state->black;
  values.line_width = state->thickness;
  values.line_style = LineSolid;
  values.cap_style = CapButt;
  values.join_style = JoinBevel;

  state->erase_gc =
    XCreateGC (dpy, window,
               GCForeground | GCBackground | GCLineWidth |
               GCLineStyle | GCCapStyle | GCJoinStyle,
               &values);

  values.foreground = state->palette[0];

  state->draw_gc =
    XCreateGC (dpy, window,
               GCForeground | GCBackground | GCLineWidth |
               GCLineStyle | GCCapStyle | GCJoinStyle,
               &values);

  if (!state->erase_gc || !state->draw_gc)
    abort ();

  state->xpoints = (XPoint *)
    calloc ((size_t) state->configuration.points + 1,
            sizeof (*state->xpoints));

  if (!state->xpoints)
    abort ();

  state->simulation =
    mystify_state_create (&state->configuration,
                          state->width, state->height, seed);

  if (!state->simulation)
    abort ();

  create_backing (state, state->width, state->height);

  return state;
}


static unsigned long
mystify_draw (Display *dpy, Window window, void *closure)
{
  struct mystify *state = (struct mystify *) closure;
  int polygon;

  (void) dpy;
  (void) window;

  for (polygon = 0;
       polygon < mystify_state_polygon_count (state->simulation);
       polygon++)
    {
      mystify_frame frame;

      mystify_state_step_polygon (state->simulation, polygon, &frame);
      render_frame (state, &frame);
    }

  return (unsigned long) state->delay;
}


static void
mystify_reshape (Display *dpy, Window window, void *closure,
                 unsigned int width, unsigned int height)
{
  struct mystify *state = (struct mystify *) closure;

  (void) dpy;
  (void) window;

  if (!width || !height)
    return;

  if (width == state->width && height == state->height)
    return;

  if (!mystify_state_reset (state->simulation, width, height))
    abort ();

  create_backing (state, width, height);
}


static Bool
mystify_event (Display *dpy, Window window, void *closure, XEvent *event)
{
  struct mystify *state = (struct mystify *) closure;

  (void) dpy;
  (void) window;

  if (event->xany.type == Expose && state->backing)
    {
      const XExposeEvent *expose = &event->xexpose;

      XCopyArea (state->dpy,
                 state->backing,
                 state->window,
                 state->draw_gc,
                 expose->x,
                 expose->y,
                 (unsigned int) expose->width,
                 (unsigned int) expose->height,
                 expose->x,
                 expose->y);
    }

  return False;
}


static void
mystify_free (Display *dpy, Window window, void *closure)
{
  struct mystify *state = (struct mystify *) closure;

  (void) window;

  if (!state)
    return;

  mystify_state_free (state->simulation);

  if (state->backing)
    XFreePixmap (dpy, state->backing);

  if (state->draw_gc)
    XFreeGC (dpy, state->draw_gc);

  if (state->erase_gc)
    XFreeGC (dpy, state->erase_gc);

  if (state->allocated_colors_p && state->palette_count)
    XFreeColors (dpy, state->colormap,
                 state->palette, state->palette_count, 0);

  free (state->palette);
  free (state->xpoints);
  free (state);
}


static const char *mystify_defaults[] = {
  ".background: black",
  ".foreground: white",
  "*fpsSolid: true",
  "*delay: 30000",
  "*polygons: 2",
  "*points: 4",
  "*trails: 5",
  "*speed: 14",
  "*thickness: 1",
  "*colors: 64",
  "*seed: 0",
  0
};


static XrmOptionDescRec mystify_options[] = {
  { "-delay",     ".delay",     XrmoptionSepArg, 0 },
  { "-polys",     ".polygons",  XrmoptionSepArg, 0 },
  { "-points",    ".points",    XrmoptionSepArg, 0 },
  { "-trails",    ".trails",    XrmoptionSepArg, 0 },
  { "-speed",     ".speed",     XrmoptionSepArg, 0 },
  { "-thickness", ".thickness", XrmoptionSepArg, 0 },
  { "-colors",    ".colors",    XrmoptionSepArg, 0 },
  { "-seed",      ".seed",      XrmoptionSepArg, 0 },
  { 0, 0, 0, 0 }
};


XSCREENSAVER_MODULE ("Mystify", mystify)
