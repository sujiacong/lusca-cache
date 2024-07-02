## Process this file with automake to produce Makefile.in

LDADD = -L../lib -lmiscutil

if ENABLE_MINGW32SPECIFIC
AIOPS_SOURCE = aiops_win32.c
else
AIOPS_SOURCE = aiops.c
endif

libasyncio_a_SOURCES = \
	$(AIOPS_SOURCE) \
	async_io.c

noinst_LIBRARIES = \
	libasyncio.a
