# expac - an alpm data dump tool
VERSION = $(shell git describe --always)

# paths
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

# compiler flags
CPPFLAGS := -DVERSION=\"$(VERSION)\" -D_GNU_SOURCE $(CPPFLAGS)
CFLAGS   := -std=c11 -g -pedantic -Wall -Wextra -Wno-missing-field-initializers $(CFLAGS)
LDLIBS    = -lalpm

DISTFILES = expac.c README.pod

all: expac doc

doc: expac.1
expac.1: README.pod
	pod2man --section=1 --center="expac manual" --name="EXPAC" --release="expac $(VERSION)" $< $@

install: expac
	install -D -m755 expac $(DESTDIR)$(PREFIX)/bin/expac
	install -D -m644 expac.1 $(DESTDIR)$(MANPREFIX)/man1/expac.1

dist: clean
	mkdir expac-$(VERSION)
	cp $(DISTFILES) expac-$(VERSION)
	sed "s/^VERSION = .*/VERSION = $(VERSION)/" Makefile > expac-$(VERSION)/Makefile
	tar cf - expac-$(VERSION) | gzip -9 > expac-$(VERSION).tar.gz
	rm -rf expac-$(VERSION)

clean:
	$(RM) expac.o expac expac.1

.PHONY: all clean dist doc install doc
