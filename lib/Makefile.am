## Process this file with automake to produce Makefile.in
#
#  $Id: Makefile.am 14613 2010-04-18 04:25:59Z adrian.chadd $
#

if ENABLE_WIN32SPECIFIC
WIN32SOURCE = win32_version.c win32_error.c
LIBSSPWIN32=libsspwin32.a
else
WIN32SOURCE =
LIBSSPWIN32=
endif

if ENABLE_MINGW32SPECIFIC
WIN32SRC = win32lib.c
else
WIN32SRC=
endif

EXTRA_LIBRARIES = \
	libdlmalloc.a \
	libregex.a \
	libsspwin32.a
noinst_LIBRARIES = \
	@LIBDLMALLOC@ \
	libmiscutil.a \
	libntlmauth.a \
	@LIBREGEX@ \
	$(LIBSSPWIN32)
#
# dirent.c, ecnrypt.c and getopt.c are needed for native Windows support.
#
EXTRA_libmiscutil_a_SOURCES = \
	dirent.c \
	encrypt.c \
	getopt.c \
	win32lib.c
libmiscutil_a_SOURCES = \
	Array.c \
	base64.c \
	charset.c \
	getfullhostname.c \
	hash.c \
	heap.c \
	hex.c \
	html_quote.c \
	iso3307.c \
	md5.c \
	rfc1035.c \
	rfc1123.c \
	rfc1738.c \
	rfc2617.c \
	safe_inet_addr.c \
	splay.c \
	strsep.c \
	Stack.c \
	stub_memaccount.c \
	util.c \
	uudecode.c \
	Vector.c \
	$(WIN32SRC) \
	$(WIN32SOURCE)
libmiscutil_a_LIBADD = \
	@LIBOBJS@
# $(top_srcdir)/include/version.h should be a dependency
libregex_a_SOURCES = \
	GNUregex.c
libdlmalloc_a_SOURCES = \
	dlmalloc.c
libntlmauth_a_SOURCES = \
	ntlmauth.c
libntlmauth_a_LIBADD = \
	@LIBOBJS@
libsspwin32_a_SOURCES = \
	sspwin32.c

INCLUDES	= -I$(top_builddir)/include -I$(top_srcdir)/include
