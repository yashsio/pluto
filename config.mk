# pluto version
VERSION = 0.1

# paths
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin
MANDIR   = $(PREFIX)/man

# compiler
CC       = gcc
CFLAGS   = -g -O2 -Wall -I. -I$(PROTODIR) -flto -lwayland-client -lxkbcommon
LDFLAGS  =

# install
INSTALL  = /usr/bin/install -c -s
MKDIR_P  = /usr/bin/mkdir -p

# protocols
PROTODIR = ./protocol
PROTOS   = $(PROTODIR)/river-layer-shell-v1.xml \
           $(PROTODIR)/river-window-management-v1.xml \
           $(PROTODIR)/river-xkb-bindings-v1.xml
PROTOC   = $(PROTOS:.xml=.c)
PROTOH   = $(PROTOS:.xml=.h)
