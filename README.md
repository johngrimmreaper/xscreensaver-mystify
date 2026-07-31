# Mystify for XScreenSaver

This repository packages the Mystify display hack independently from the full
XScreenSaver source package.

It is intended for users who want Mystify on an existing XScreenSaver
installation without replacing their distribution's complete XScreenSaver
package or waiting for the next upstream release.

Current standalone release: **0.2.0**

## Source synchronization

`mystify.c`, `config/mystify.xml`, and `mystify.6x` are copied from the
authoritative XScreenSaver implementation.

Current synchronized source commit:

```text
f1a60a3ab34d70e76749e24ff48fadd4598d03bd
mystify: allocate colors through XScreenSaver utilities
```

The standalone-only files `screenhack.h`, `screenhack-standalone.c`,
`colors.h`, and `colors-standalone.c` provide the small parts of the
XScreenSaver module lifecycle, resource handling, and color allocation needed
to compile the unmodified hack as an independent executable.

The display-hack source should remain byte-for-byte identical to
`hacks/mystify.c` at the recorded XScreenSaver commit.

## Building

```sh
make
make check
./mystify -window
```

Build requirements:

- a C17 compiler;
- `pkg-config`;
- Xlib development headers and libraries.

On Debian or Ubuntu:

```sh
sudo apt install build-essential pkgconf libx11-dev
```

## Installation

```sh
sudo make install
```

The Debian package additionally installs the manual page and screensaver
desktop entry.

## License

Mystify and its standalone compatibility host are distributed under the X11
License. See `LICENSE`.
