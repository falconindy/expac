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

expac: \
	expac.c \
	conf.c conf.h \
	util.h

doc: expac.1
expac.1: README.pod
	pod2man --section=1 --center="expac manual" --name="EXPAC" --release="expac $(VERSION)" $< $@

install: expac
	install -Dm755 expac $(DESTDIR)$(PREFIX)/bin/expac
	install -Dm644 expac.1 $(DESTDIR)$(MANPREFIX)/man1/expac.1

expac-$(VERSION).tar.gz: dist
dist: clean
	mkdir expac-$(VERSION)
	cp $(DISTFILES) expac-$(VERSION)
	sed "s/^VERSION = .*/VERSION = $(VERSION)/" Makefile > expac-$(VERSION)/Makefile
	tar cf - expac-$(VERSION) | gzip -9 > expac-$(VERSION).tar.gz
	rm -rf expac-$(VERSION)

upload: expac-$(VERSION).tar.gz
	gpg --detach-sign expac-$(VERSION).tar.gz
	scp expac-$(VERSION).tar.gz expac-$(VERSION).tar.gz.sig code.falconindy.com:archive/expac/

clean:
	$(RM) *.o expac expac.1

.PHONY: all clean dist doc install doc
