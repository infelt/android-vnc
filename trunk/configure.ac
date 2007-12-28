###############################################################################
##
## $Id$
##
## Copyright (C) 2007 Josh Guilfoyle <jasta@devtcg.org>
##
## This program is free software; you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2, or (at your option) any
## later version.
##
## This program is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
##
###############################################################################

AC_INIT(fbvncserver, 0.01, http://android-vnc.googlecode.com)
AM_INIT_AUTOMAKE(fbvncserver, 0.01)
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_LIBTOOL

AC_ARG_WITH(libvncserver,
  AC_HELP_STRING([--with-libvncserver=dir],
    [set libvncserver installation path]),,)

if test "x$with_libvncserver" != "x"; then
	LIBVNCSERVER_CFLAGS="-I$with_libvncserver/include"
	LIBVNCSERVER_LDFLAGS="-L$with_libvncserver/lib"
	LIBVNCSERVER_LIBS="-lvncserver"
fi

ac_save_CFLAGS="$CFLAGS"
ac_save_LDFLAGS="$LDFLAGS"
CFLAGS="$CFLAGS $LIBVNCSERVER_CFLAGS"
LDFLAGS="$LDFLAGS $LIBVNCSERVER_LDFLAGS"

AC_CHECK_LIB(vncserver, rfbGetScreen,,
  AC_MSG_ERROR([
*** Couldn't find libvncserver library.  Please re-run configure with
*** --with-libvncserver=DIR on the command-line.
]))

CFLAGS="$ac_save_CFLAGS"
LDFLAGS="$ac_save_LDFLAGS"

AC_SUBST(LIBVNCSERVER_LIBS)
AC_SUBST(LIBVNCSERVER_CFLAGS)
AC_SUBST(LIBVNCSERVER_LDFLAGS)

AC_CONFIG_FILES([Makefile
	src/Makefile])

AC_OUTPUT
