CC ?= cc
VERSION = 0.2.0
PKG_CONFIG ?= pkg-config

CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=

WARNINGS = -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion \
           -Wstrict-prototypes -Wmissing-prototypes
STANDARD = -std=gnu17

VERSION_CPPFLAGS = -DSCREENHACK_STANDALONE_VERSION=\"$(VERSION)\"

X11_CFLAGS := $(shell $(PKG_CONFIG) --cflags x11)
X11_LIBS := $(shell $(PKG_CONFIG) --libs x11)
MATH_LIBS = -lm

PREFIX ?= /usr
LIBEXECDIR ?= $(PREFIX)/libexec/xscreensaver
SYSCONFDIR ?= $(PREFIX)/share/xscreensaver/config
DESTDIR ?=

OBJECTS = mystify.o screenhack-standalone.o colors-standalone.o

all: mystify

mystify: $(OBJECTS)
	$(CC) $(CFLAGS) $(STANDARD) $(WARNINGS) -o $@ $(OBJECTS) \
	      $(LDFLAGS) $(X11_LIBS) $(MATH_LIBS)

mystify.o: mystify.c screenhack.h colors.h
	$(CC) $(CPPFLAGS) $(X11_CFLAGS) $(CFLAGS) $(STANDARD) $(WARNINGS) \
	      -c -o $@ mystify.c

screenhack-standalone.o: screenhack-standalone.c screenhack.h
	$(CC) $(CPPFLAGS) $(VERSION_CPPFLAGS) $(X11_CFLAGS) $(CFLAGS) $(STANDARD) $(WARNINGS) \
	      -c -o $@ screenhack-standalone.c

colors-standalone.o: colors-standalone.c colors.h
	$(CC) $(CPPFLAGS) $(X11_CFLAGS) $(CFLAGS) $(STANDARD) $(WARNINGS) \
	      -c -o $@ colors-standalone.c

check: mystify
	./mystify --version
	./mystify --help >/dev/null

install: mystify
	install -D -m 0755 mystify \
	  $(DESTDIR)$(LIBEXECDIR)/mystify
	install -D -m 0644 config/mystify.xml \
	  $(DESTDIR)$(SYSCONFDIR)/mystify.xml

clean:
	rm -f mystify $(OBJECTS)

.PHONY: all check install clean
