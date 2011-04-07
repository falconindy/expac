# version
VERSION = $(shell git describe --always)

# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

# compiler flags
CC       ?= gcc
CPPFLAGS += -DVERSION=\"${VERSION}\"
CFLAGS   += -std=c99 -g -pedantic -Wall -Wextra -Werror ${CPPFLAGS}
LDFLAGS  += -lalpm
