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

## Building and installing without a package

This generic build path can be used on any Unix-like distribution with the
required development tools and X11 libraries.

Build requirements:

- a C17 compiler;
- `make`;
- `pkg-config`;
- Xlib development headers and libraries;
- an existing XScreenSaver installation if Mystify will be used as a display
  hack.

On Debian or Ubuntu:

```sh
sudo apt update
sudo apt install build-essential pkgconf libx11-dev xscreensaver
```

On Fedora, RHEL, Rocky Linux, or AlmaLinux with EPEL enabled:

```sh
sudo dnf install gcc make pkgconf-pkg-config libX11-devel xscreensaver-base
```

On another distribution, install the equivalent compiler, `make`,
`pkg-config`, Xlib development package, and XScreenSaver runtime package.

Compile and test Mystify from the top-level source directory:

```sh
make
make check
```

Run the compiled program directly without installing it:

```sh
./mystify -window
```

Install the executable and XML configuration directly, without creating a
distribution package:

```sh
sudo make install
```

The default manual installation paths are:

```text
/usr/libexec/xscreensaver/mystify
/usr/share/xscreensaver/config/mystify.xml
```

Distributions with a different XScreenSaver filesystem layout can override
`PREFIX`, `LIBEXECDIR`, and `SYSCONFDIR` when running `make install`.

## Building distribution packages

### Debian or Ubuntu

Install the Debian packaging dependencies:

```sh
sudo apt update
sudo apt install build-essential debhelper pkgconf libx11-dev
```

Build an unsigned binary package from the top-level source directory:

```sh
dpkg-buildpackage -b -us -uc
```

The generated `.deb` package and related build artifacts are written to the
parent directory.

### Fedora, RHEL, Rocky Linux, or AlmaLinux

On Enterprise Linux, enable EPEL first so the resulting package can use the
EPEL `xscreensaver-base` runtime dependency.

Install the RPM build tools and build dependencies:

```sh
sudo dnf install gcc make pkgconf-pkg-config libX11-devel \
  desktop-file-utils rpm-build rpmdevtools xscreensaver-base
```

Create the standard RPM build tree, download the tagged source archive declared
by the spec, and build both the source and binary RPMs:

```sh
rpmdev-setuptree
spectool -g -R xscreensaver-mystify.spec
rpmbuild -ba xscreensaver-mystify.spec
```

The generated packages are written beneath:

```text
~/rpmbuild/SRPMS/
~/rpmbuild/RPMS/<architecture>/
```

The RPM spec follows Fedora and EPEL's non-OpenGL XScreenSaver hack layout.
It installs a modular `hacks.conf.d` fragment and refreshes XScreenSaver's
generated hack list when the package is installed or removed. The additional
`xscreensaver-mystify.desktop` entry follows Fedora's GNOME screensaver
compatibility convention and is restricted with `OnlyShowIn=GNOME;`.

## Installation

```sh
sudo make install
```

The Debian package additionally installs the manual page and screensaver
desktop entry.

## License

Mystify and its standalone compatibility host are distributed under the X11
License. See `LICENSE`.
