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

bin_PROGRAMS = coss_newfs

coss_newfs_SOURCES = coss_newfs.c

LDADD = -L../../lib -L../../libsqdebug -L../../libcore -L../../libmem -L../../libsqstore -lsqdebug -lcore -lm -lsqstore -lmiscutil @XTRA_LIBS@

$(OBJS): $(top_srcdir)/include/version.h ../include/autoconf.h

DISTCLEANFILES = 

