/*
 * Minimal color helpers for the standalone Mystify package.
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

#include "colors.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static double
clamp_unit(double value)
{
  if (value < 0.0)
    return 0.0;
  if (value > 1.0)
    return 1.0;
  return value;
}

static void
hsv_to_rgb(double hue, double saturation, double value,
           unsigned short *red, unsigned short *green,
           unsigned short *blue)
{
  double chroma;
  double hue_sector;
  double intermediate;
  double match;
  double red_component = 0.0;
  double green_component = 0.0;
  double blue_component = 0.0;
  int sector;

  saturation = clamp_unit(saturation);
  value = clamp_unit(value);

  hue = fmod(hue, 360.0);
  if (hue < 0.0)
    hue += 360.0;

  chroma = value * saturation;
  hue_sector = hue / 60.0;
  intermediate = chroma *
    (1.0 - fabs(fmod(hue_sector, 2.0) - 1.0));
  sector = (int) floor(hue_sector);

  switch (sector) {
  case 0:
    red_component = chroma;
    green_component = intermediate;
    break;
  case 1:
    red_component = intermediate;
    green_component = chroma;
    break;
  case 2:
    green_component = chroma;
    blue_component = intermediate;
    break;
  case 3:
    green_component = intermediate;
    blue_component = chroma;
    break;
  case 4:
    red_component = intermediate;
    blue_component = chroma;
    break;
  default:
    red_component = chroma;
    blue_component = intermediate;
    break;
  }

  match = value - chroma;

  *red = (unsigned short)
    lround((red_component + match) * 65535.0);
  *green = (unsigned short)
    lround((green_component + match) * 65535.0);
  *blue = (unsigned short)
    lround((blue_component + match) * 65535.0);
}

static void
interpolate_color(int index, int count, Bool closed_p,
                  int hue1, double saturation1, double value1,
                  int hue2, double saturation2, double value2,
                  XColor *color)
{
  double fraction;

  if (count <= 1) {
    fraction = 0.0;
  } else if (closed_p) {
    double position = (double) index / (double) (count - 1);
    fraction = position <= 0.5
      ? position * 2.0
      : (1.0 - position) * 2.0;
  } else {
    fraction = (double) index / (double) (count - 1);
  }

  hsv_to_rgb(
    (double) hue1 + ((double) hue2 - (double) hue1) * fraction,
    saturation1 + (saturation2 - saturation1) * fraction,
    value1 + (value2 - value1) * fraction,
    &color->red,
    &color->green,
    &color->blue
  );

  color->flags = DoRed | DoGreen | DoBlue;
  color->pixel = 0;
}

void
free_colors(Screen *screen, Colormap colormap,
            XColor *colors, int color_count)
{
  Display *display;
  unsigned long *pixels;
  int index;

  if (!screen || !colors || color_count <= 0)
    return;

  display = DisplayOfScreen(screen);
  pixels = (unsigned long *)
    calloc((size_t) color_count, sizeof(*pixels));

  if (!pixels)
    return;

  for (index = 0; index < color_count; index++)
    pixels[index] = colors[index].pixel;

  XFreeColors(display, colormap, pixels, color_count, 0);
  free(pixels);
}

void
make_color_ramp(Screen *screen, Visual *visual, Colormap colormap,
                int hue1, double saturation1, double value1,
                int hue2, double saturation2, double value2,
                XColor *colors, int *color_count,
                Bool closed_p, Bool allocate_p,
                Bool *writable_p)
{
  Display *display = screen ? DisplayOfScreen(screen) : NULL;
  int requested;
  int attempt;
  int allocated;
  int index;

  (void) visual;

  if (!colors || !color_count || *color_count <= 0) {
    if (color_count)
      *color_count = 0;
    return;
  }

  if (writable_p)
    *writable_p = False;

  requested = *color_count;

  if (!allocate_p) {
    for (index = 0; index < requested; index++)
      interpolate_color(
        index, requested, closed_p,
        hue1, saturation1, value1,
        hue2, saturation2, value2,
        &colors[index]
      );

    *color_count = requested;
    return;
  }

  if (!display) {
    *color_count = 0;
    return;
  }

  attempt = requested;

  while (attempt >= 2) {
    allocated = 0;

    for (index = 0; index < attempt; index++) {
      interpolate_color(
        index, attempt, closed_p,
        hue1, saturation1, value1,
        hue2, saturation2, value2,
        &colors[index]
      );

      if (!XAllocColor(display, colormap, &colors[index]))
        break;

      allocated++;
    }

    if (allocated == attempt) {
      *color_count = allocated;
      return;
    }

    if (allocated > 0)
      free_colors(screen, colormap, colors, allocated);

    attempt /= 2;
  }

  *color_count = 0;
}
