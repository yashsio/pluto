# pluto version
VERSION = 0.3

# paths
PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin

# compiler
CC         ?= cc
PKG_CONFIG ?= pkg-config
PKGS        = wayland-client xkbcommon

INCS     = -I. -I$(PROTODIR) $(shell $(PKG_CONFIG) --cflags $(PKGS))
CPPFLAGS = -D_DEFAULT_SOURCE -DVERSION=\"$(VERSION)\"

CFLAGS  ?= -g -O2
CFLAGS  += -std=c99 -Wall -Wextra -Wno-unused-parameter $(INCS) $(CPPFLAGS)
LDFLAGS ?=
LDLIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))

# install
INSTALL = install -c -s
MKDIR_P = mkdir -p

# protocols
PROTODIR = ./protocol
PROTOS   = $(PROTODIR)/river-input-management-v1.xml \
           $(PROTODIR)/river-layer-shell-v1.xml \
           $(PROTODIR)/river-libinput-config-v1.xml \
           $(PROTODIR)/river-window-management-v1.xml \
           $(PROTODIR)/river-xkb-bindings-v1.xml
PROTOC   = $(PROTOS:.xml=.c)
PROTOH   = $(PROTOS:.xml=.h)
