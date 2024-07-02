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

libexec_PROGRAMS = ufs_rebuild

ufs_rebuild_SOURCES = ufs_rebuild.c ufs_build_log.c ufs_build_dir.c

LDADD = -L../../lib -L../../libsqdebug -L../../libcore -L../../libmem -L../../libsqstore -L../../libsqtlv  -lsqdebug -lcore -lm -lsqstore -lsqtlv -lmiscutil @XTRA_LIBS@

$(OBJS): $(top_srcdir)/include/version.h ../include/autoconf.h

DISTCLEANFILES = 

