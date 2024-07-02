#
#  Makefile for the Squid Object Cache server
#
#  $Id: Makefile.am 11393 2006-09-22 09:08:46Z hno $
#
#  Uncomment and customize the following to suit your needs:
#


AUTOMAKE_OPTIONS = subdir-objects

INCLUDES        = -I. -I$(top_builddir)

TESTS=$(check_PROGRAMS)
check_PROGRAMS=

SUBDIRS		= 

noinst_PROGRAMS = tcptest

tcptest_SOURCES = tcptest.c tunnel.c

LDADD = -L../../lib -L../../libsqdebug -L../../libcore -L../../libmem -L../../libcb -L../../libiapp -L../../libsqinet -L../../libhttp -L../../libstat -liapp -lmem -lcb -lsqdebug -lcore -lstat -lm -lsqinet -lmiscutil @SSLLIB@

$(OBJS): $(top_srcdir)/include/version.h ../include/autoconf.h

DISTCLEANFILES = 

