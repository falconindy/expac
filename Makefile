# expac - an alpm data dump tool
VERSION = 8

# paths
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

# compiler flags
CPPFLAGS := -DVERSION=\"$(VERSION)\" -D_GNU_SOURCE $(CPPFLAGS)
CFLAGS   := -std=c11 -g -pedantic -Wall -Wextra -Wno-missing-field-initializers $(CFLAGS)
LDLIBS    = -lalpm

all: expac doc

expac: \
	expac.c \
	conf.c conf.h \
	util.h

doc: \
	expac.1

expac.1: README.pod
	pod2man --section=1 --center="expac manual" --name="EXPAC" --release="expac $(VERSION)" $< $@

install: expac
	install -Dm755 expac $(DESTDIR)$(PREFIX)/bin/expac
	install -Dm644 expac.1 $(DESTDIR)$(MANPREFIX)/man1/expac.1

dist:
	git archive --format=tar --prefix=expac-$(VERSION)/ HEAD | gzip -9 > expac-$(VERSION).tar.gz

upload: expac-$(VERSION).tar.gz
	gpg --detach-sign expac-$(VERSION).tar.gz
	scp expac-$(VERSION).tar.gz expac-$(VERSION).tar.gz.sig pkgbuild.com:public_html/sources/expac/

clean:
	$(RM) *.o expac expac.1

.PHONY: all clean dist doc install doc
