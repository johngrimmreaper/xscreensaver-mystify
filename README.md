# Mystify for XScreenSaver

This repository packages the Mystify display hack independently from the full
XScreenSaver source package.

It is intended for users who want Mystify on an existing XScreenSaver
installation without replacing their distribution's complete XScreenSaver
package or waiting for the next upstream release.

## Source synchronization

`mystify.c`, `config/mystify.xml`, and `mystify.6x` are copied from the
authoritative XScreenSaver implementation.

Current synchronized source commit:

```text
5b739903e0011143bba1da127216dbfb29fddf42
mystify: fold private simulation core into hack
```

The standalone-only files `screenhack.h` and `screenhack-standalone.c` provide
the small part of the XScreenSaver module lifecycle needed to compile the
unmodified hack as an independent executable.

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
