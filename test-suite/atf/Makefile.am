
ATF_LIBDIR=$(ATF_PREFIX)/lib
ATF_BINDIR=$(ATF_PREFIX)/bin
ATF_INCLUDEDIR=$(ATF_PREFIX)/include

INCLUDES=-I$(top_builddir) -I$(ATF_INCLUDEDIR)
LDADD=-L$(top_builddir)/libcore \
	-L$(top_builddir)/lib \
	-L$(top_builddir)/libcore \
	-L$(top_builddir)/libsqdebug \
	-L$(top_builddir)/libmem \
	-L$(top_builddir)/libcb \
	-L$(top_builddir)/libmime \
	-L$(top_builddir)/libhelper \
	-L$(top_builddir)/libstmem \
	-L$(top_builddir)/libiapp \
	-L$(top_builddir)/libhttp \
	-L$(top_builddir)/libsqurl \
	-L$(top_builddir)/libstat \
	-L$(top_builddir)/libsqdns \
	-L$(top_builddir)/libsqident \
	-L$(top_builddir)/libsqinet \
	-L$(top_builddir)/libsqname \
	-L$(top_builddir)/libasyncio \
	-L$(top_builddir)/libsqtlv \
	-L$(top_builddir)/libsqstore \
	-L$(ATF_LIBDIR) \
	-lsqstore \
	-lsqtlv \
	-lasyncio \
	-lstmem \
	-lsqdns \
	-lsqident \
	-lsqinet \
	-lsqdebug \
	-lsqname \
	-lcb \
	-lhelper \
	-lmime \
	-liapp \
	-lhttp \
	-lsqurl \
	-lmem \
	-lcore \
	-lstat \
	-lmiscutil \
	-lpthread -lm \
	-latf-c

check_PROGRAMS=	libhttp lib_Vector libsqurl url_benchmark

libhttp_SOURCES= libhttp.c core.c
lib_Vector_SOURCES= lib_Vector.c core.c
libsqurl_SOURCES= libsqurl.c core.c
url_benchmark_SOURCES= url_benchmark.c core.c

check-local: check-atf

check-atf:
	env PATH=$$PATH:$(ATF_BINDIR) $(ATF_BINDIR)/atf-run | $(ATF_BINDIR)/atf-report

#check: libhttp lib_Vector libsqurl
#	@env PATH=$$PATH:$(ATF_BINDIR) $(ATF_BINDIR)/atf-run | $(ATF_BINDIR)/atf-report

