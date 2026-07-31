#ifndef MYSTIFY_STANDALONE_COLORS_H
#define MYSTIFY_STANDALONE_COLORS_H

#include <X11/Xlib.h>

void free_colors(Screen *screen, Colormap colormap,
                 XColor *colors, int color_count);

void make_color_ramp(Screen *screen, Visual *visual, Colormap colormap,
                     int hue1, double saturation1, double value1,
                     int hue2, double saturation2, double value2,
                     XColor *colors, int *color_count,
                     Bool closed_p, Bool allocate_p,
                     Bool *writable_p);

#endif
