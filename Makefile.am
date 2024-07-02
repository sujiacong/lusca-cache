## Process this file with automake to produce Makefile.in
#
# $Id: Makefile.am 14766 2010-08-29 07:41:59Z adrian.chadd $
#

AUTOMAKE_OPTIONS = dist-bzip2 subdir-objects 1.5
DIST_SUBDIRS	= lib libcore libmem libsqname libcb libiapp libstat libsqftp libsqurl libhttp libsqdns libsqident libsqinet libhelper libmime libsqdebug libstmem libasyncio libsqtlv libsqstore snmplib scripts src icons errors contrib doc helpers tools app test-suite
SUBDIRS		= lib libcore libmem libsqname libcb libiapp libstat libsqftp libsqurl libhttp libsqdns libsqident libsqinet libhelper libmime libsqdebug libstmem libasyncio libsqtlv libsqstore @makesnmplib@ scripts src icons errors doc helpers tools app test-suite

DISTCLEANFILES = include/stamp-h include/stamp-h[0-9]*
DEFAULT_PINGER		= $(libexecdir)/`echo pinger | sed '$(transform);s/$$/$(EXEEXT)/'`

dist-hook:
	@ for subdir in include; do \
	  if test "$$subdir" = .; then :; else \
	    test -d $(distdir)/$$subdir \
	    || mkdir $(distdir)/$$subdir \
	    || exit 1; \
	    cp -p $(srcdir)/$$subdir/*.h  $(distdir)/$$subdir \
	      || exit 1; \
	    rm -f $(distdir)/$$subdir/autoconf.h; \
	  fi; \
	done
	@if [ -f $(top_srcdir)/RELEASENOTES.html ]; then \
	  cp -p $(top_srcdir)/RELEASENOTES.html $(distdir)/RELEASENOTES.html; \
	else \
	  cp $(top_srcdir)/doc/release-notes/release.html $(distdir)/RELEASENOTES.html; \
	fi

EXTRA_DIST = \
	ChangeLog \
	CONTRIBUTORS \
	COPYING \
	COPYRIGHT \
	CREDITS \
	INSTALL \
	QUICKSTART \
	README \
	SPONSORS \
	bootstrap.sh

install-pinger:
	chown root $(DEFAULT_PINGER) 
	chmod 4711 $(DEFAULT_PINGER)

distuninstallcheck_listfiles = \
	find . -name squid.conf -prune -o -name cachemgr.conf -prune -type f -print
