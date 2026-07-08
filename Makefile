# pluto - dynamic window manager for river
# See LICENSE for details.

include config.mk

CFILES  = pluto.c layout.c bindings.c $(PROTOC)
HFILES  = pluto.h config.h $(PROTOH)

all: pluto

config.h: config.def.h
	cp config.def.h $@

pluto: $(CFILES) $(HFILES) config.mk
	$(CC) -o $@ $(CFLAGS) $(CFILES)

clean:
	rm -f pluto $(PROTOC) $(PROTOH)

install: all
	$(MKDIR_P) $(BINDIR)
	$(INSTALL) pluto $(BINDIR)
	$(MKDIR_P) $(MANDIR)/man1
	cp doc/pluto.1 $(MANDIR)/man1
	chmod 644 $(MANDIR)/man1/pluto.1

uninstall:
	rm -f $(BINDIR)/pluto
	rm -f $(MANDIR)/man1/pluto.1

.PHONY: all clean install uninstall

.SUFFIXES: .xml .c .h

.xml.c:
	wayland-scanner private-code $< $@

.xml.h:
	wayland-scanner client-header $< $@
