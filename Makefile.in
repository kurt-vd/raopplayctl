PROGS	= mpd2airtunes
default	: $(PROGS)

PREFIX	= @prefix@
VPATH	= @srcdir@
SVNROOT	= @srcdir@

host	= @host@
CC	= @CC@
OBJCOPY	= @OBJCOPY@
INSTALL	= @INSTALL@

CFLAGS	= @CFLAGS@
CPPFLAGS= @CPPFLAGS@
LDFLAGS	= @LDFLAGS@
LDLIBS	= @LIBS@

STRIPOPTS	= @STRIPOPTS@

include $(SVNROOT)/make/include
-include $(patsubst $(SVNROOT)/%.c, %.d, $(wildcard $(SVNROOT)/*.c))

mpd2airtunes-dbg: main.o

clean:
	rm -rf $(wildcard *.d) $(wildcard *.o) $(patsubst %,%-dbg,$(PROGS))

install: $(PROGS)
	@[ -e $(DESTDIR)$(PREFIX)/bin ] || $(INSTALL) $(INSTOPTS) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(INSTOPTS) -t $(DESTDIR)$(PREFIX)/bin $^

