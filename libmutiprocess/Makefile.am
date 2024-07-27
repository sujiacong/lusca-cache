## Process this file with automake to produce Makefile.in

libmutiprocess_a_SOURCES = \
        ipcsupport.c \
        multiprocess.c

noinst_LIBRARIES = \
        libmutiprocess.a

DEFS += -DDEFAULT_STATEDIR=\"$(localstatedir)/run/squid\"

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run/squid;
