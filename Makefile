# pluto - a dwm-like window manager for river
# See LICENSE for details.

include config.mk

CFILES = pluto.c layout.c bindings.c input.c ipc.c $(PROTOC)
HFILES = pluto.h config.h ipc.h $(PROTOH)

all: pluto

config.h: config.def.h
	cp config.def.h $@

pluto: $(CFILES) $(HFILES) config.mk
	$(CC) -o $@ $(CFLAGS) $(CFILES) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f pluto $(PROTOC) $(PROTOH)

install: all
	$(MKDIR_P) $(BINDIR)
	$(INSTALL) pluto $(BINDIR)

uninstall:
	rm -f $(BINDIR)/pluto

.PHONY: all clean install uninstall

.SUFFIXES: .xml .c .h

.xml.c:
	wayland-scanner private-code $< $@

.xml.h:
	wayland-scanner client-header $< $@
