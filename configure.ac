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

AC_SEARCH_LIBS(argv_parse, conf, , AC_MSG_ERROR([libconf not found]))
AC_SEARCH_LIBS(pollh_new, pollcore, , AC_MSG_ERROR([libpollcore not found]))
AC_SEARCH_LIBS(ustl_add_timeout, ustl, , AC_MSG_ERROR([libustl not found]))

# replace CPPFLAGS to include config.h
# do this only now, as config.h may not be present yet
# and some test rely on config.h thus being ready.
CPPFLAGS="$CPPFLAGS -include config.h"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
