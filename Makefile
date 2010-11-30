include config.mk

SRC = expac.c
OBJ = ${SRC:.c=.o}

all: expac

35:
	${MAKE} PMCHECK=-D_HAVE_ALPM_FIND_SATISFIER all

.c.o:
	${CC} -c ${CFLAGS} $<

expac: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

install: expac
	install -D -m755 expac ${DESTDIR}${PREFIX}/bin/expac

dist: clean
	mkdir expac-${VERSION}
	cp Makefile expac.c expac-${VERSION}
	sed "s/^VERSION = .*/VERSION = ${VERSION}/" config.mk > expac-${VERSION}/config.mk
	tar cf - expac-${VERSION} | gzip -9 > expac-${VERSION}.tar.gz
	rm -rf expac-${VERSION}

clean:
	$(RM) ${OBJ} expac

