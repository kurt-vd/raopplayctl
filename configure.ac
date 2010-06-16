AC_PREREQ(2.61)
AC_INIT([mpd2airtunes],[0.1],[kurt.van.dijck@skynet.be])
#AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_CONFIG_HEADERS([config.h])
AC_CANONICAL_HOST
AC_USE_SYSTEM_EXTENSIONS

# don't let autoconf fill some defaults
# but allow externally given defaults
CPPFLAGS="$CPPFLAGS -I."
CFLAGS="$CFLAGS -Wall"
#CXXFLAGS="$CXXFLAGS -Wall"
LDFLAGS="$LDFLAGS"

dnl test compilers, tools
AC_PROG_CC_C99
#AC_PROG_CXX
AC_CHECK_TOOL(OBJCOPY, objcopy)
AC_PROG_INSTALL

#AC_HEADER_STDC
#AC_CHECK_HEADERS([stddef.h])

AC_ARG_ENABLE(strip, AC_HELP_STRING(--disable-strip, do not strip output))
STRIPOPTS="-S"
if test x$enable_strip = xno; then
	STRIPOPTS=
fi
AC_SUBST(STRIPOPTS)
AC_MSG_NOTICE(strip options: '$STRIPOPTS')

AC_CHECK_HEADERS([sys/signalfd.h])
AC_REPLACE_FUNCS(signalfd)

AC_SEARCH_LIBS(lround, m, , AC_MSG_ERROR(math library not found))
AC_SEARCH_LIBS(clock_gettime, rt posix-time)
AC_CHECK_FUNCS([clock_gettime gettimeofday])
AC_SEARCH_LIBS(llist_init, llist, , AC_MSG_ERROR(libllist not found))
AC_SEARCH_LIBS(url_listen, url, , AC_MSG_ERROR(liburl not found))

# replace CPPFLAGS to include config.h
# do this only now, as config.h may not be present yet
# and some test rely on config.h thus being ready.
CPPFLAGS="$CPPFLAGS -include config.h"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
