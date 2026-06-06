# Mystify for XScreenSaver

A small Xlib screensaver inspired by Microsoft Windows 95/98 **Mystify Your
Mind**. It is designed for old Unix workstations and low-end X11 computers.

## Design goals

- Two independently deforming four-corner wire polygons by default.
- Five retained historical wires per polygon.
- Integer-only motion in the animation loop.
- Independent corner velocities which change magnitude after an edge bounce.
- Old-wire erasure with black lines instead of clearing the frame.
- No OpenGL, XRender, GTK, image buffers, alpha blending, or floating-point
  work in the frame loop.
- A backing pixmap is maintained only for exposure repair; animation is drawn
  incrementally to both the pixmap and the window.

The erase-oldest/draw-newest strategy deliberately preserves the tiny gaps
and black cuts produced when an old wire crosses a newer one. This is part of
the visual character of the original 1990s implementation and also avoids a
full-window clear or copy every frame.

## Implementation

This project is an original standalone Xlib implementation of the classic
Mystify visual effect. Its animation, rendering, configuration, and Unix
integration were written specifically for this project.

## Building

```sh
make
./mystify -window
```

Install through the Debian package for XScreenSaver integration.

## Useful options

```text
-root                 draw on the root window
-window               create a test window
-window-id ID         draw into an existing X window
-delay USEC           frame delay (default 30000)
-polys N              moving polygons (default 2)
-points N             corners per polygon (default 4)
-trails N             retained older wires (default 5)
-speed N              maximum corner speed (default 17)
-thickness N          line width (default 1)
-colors N             allocated hue steps (default 64)
-seed N               reproducible random seed
-frames N             stop after N frames (testing)
-self-test            run non-graphical internal checks
```

XScreenSaver may launch it through `XSCREENSAVER_WINDOW`; this is supported.

## License

This project is distributed under the X11 License. See `LICENSE`.
