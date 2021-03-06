PROGS	= raopplayctl
default	: $(PROGS)

VERSION	:= $(shell ./getlocalversion .)
PREFIX	= /usr/local
INSTALL	= install

-include config.mk

CPPFLAGS+= -DVERSION="\"$(VERSION)\""

clean:
	rm -rf $(wildcard *.o) $(PROGS)

install: $(PROGS)
	@[ -e $(DESTDIR)$(PREFIX)/bin ] || $(INSTALL) $(INSTOPTS) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(INSTOPTS) -t $(DESTDIR)$(PREFIX)/bin $^

