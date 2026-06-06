CC ?= cc
PKG_CONFIG ?= pkg-config
CPPFLAGS ?=
CFLAGS ?= -O2 -g
LDFLAGS ?=
WARNINGS = -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion \
           -Wstrict-prototypes -Wmissing-prototypes
X11_CFLAGS := $(shell $(PKG_CONFIG) --cflags x11)
X11_LIBS := $(shell $(PKG_CONFIG) --libs x11)

PREFIX ?= /usr
LIBEXECDIR ?= $(PREFIX)/libexec/xscreensaver
SYSCONFDIR ?= $(PREFIX)/share/xscreensaver/config
MANDIR ?= $(PREFIX)/share/man
DESTDIR ?=

all: mystify

mystify: mystify.c
	$(CC) $(CPPFLAGS) $(X11_CFLAGS) $(CFLAGS) $(WARNINGS) -o $@ $< $(LDFLAGS) $(X11_LIBS)

check: mystify
	./mystify --self-test

install: mystify
	install -D -m 0755 mystify $(DESTDIR)$(LIBEXECDIR)/mystify
	install -D -m 0644 config/mystify.xml $(DESTDIR)$(SYSCONFDIR)/mystify.xml
	install -D -m 0644 mystify.6 $(DESTDIR)$(MANDIR)/man6/mystify.6
	mkdir -p $(DESTDIR)$(PREFIX)/lib/xscreensaver
	ln -sfn ../../libexec/xscreensaver/mystify $(DESTDIR)$(PREFIX)/lib/xscreensaver/mystify

clean:
	rm -f mystify

.PHONY: all check install clean
