PROGS	= mpd2airport
default	: $(PROGS)

VERSION	:= $(shell ./getlocalversion .)
PREFIX	= /usr/local

-include config.mk

CPPFLAGS+= -DVERSION="\"$(VERSION)\""

mpd2airtunes:

clean:
	rm -rf $(wildcard *.o) $(PROGS)

install: $(PROGS)
	@[ -e $(DESTDIR)$(PREFIX)/bin ] || $(INSTALL) $(INSTOPTS) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(INSTOPTS) -t $(DESTDIR)$(PREFIX)/bin $^

