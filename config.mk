# version
VERSION = $(shell git describe --always)

# compiler flags
CC = c99
CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS += -g -pedantic -Wall -Wextra ${CPPFLAGS}
LDFLAGS += -lalpm
