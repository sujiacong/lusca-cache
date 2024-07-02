#  Makefile for storage modules in the Squid Object Cache server
#
#  $Id: Makefile.am 14054 2009-05-21 06:46:21Z adrian.chadd $
#

AUTOMAKE_OPTIONS = subdir-objects

EXTRA_LIBRARIES = libaufs.a libcoss.a libnull.a
noinst_LIBRARIES = @STORE_LIBS@

EXTRA_libaufs_a_SOURCES = aufs/aiops.c aufs/aiops_win32.c

libaufs_a_SOURCES = aufs/store_asyncufs.h aufs/store_dir_aufs.c aufs/store_io_aufs.c aufs/store_bitmap_aufs.h aufs/store_bitmap_aufs.c aufs/store_rebuild_aufs.h aufs/store_rebuild_aufs.c aufs/store_log_aufs.h aufs/store_log_aufs.c
libcoss_a_SOURCES = coss/store_coss.h   coss/store_io_coss.c coss/store_dir_coss.c coss/store_rebuild_coss.h coss/store_rebuild_coss.c coss/store_log_coss.c coss/store_log_coss.h
libnull_a_SOURCES = null/store_null.c

LDADD = $(top_builddir)/lib/libmiscutil.a @XTRA_LIBS@

EXTRA_DIST = \
	coss/coss-notes.txt

INCLUDES      = -I. -I$(top_builddir)/include -I$(top_srcdir)/include \
	-I$(top_srcdir)/src

## targets below to emulate distributed makefiles
aufs/all: libaufs.a
aufs/clean: clean
coss/all: libcoss.a
coss/clean: clean
null/all: libnull.a
null/clean: clean
