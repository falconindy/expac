# version
VERSION = $(shell git describe --always)

# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

# compiler flags
CC ?= c99
CPPFLAGS += -DVERSION=\"${VERSION}\"
CFLAGS += -std=c99 -g -pedantic -Wall -Wextra ${CPPFLAGS}
LDFLAGS += -lalpm
