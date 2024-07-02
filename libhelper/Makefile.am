## Process this file with automake to produce Makefile.in

if ENABLE_MINGW32SPECIFIC
MINGWEXLIB = -lmingwex
IPC_SOURCE = ipc_win32.c
else
MINGWEXLIB =
IPC_SOURCE = ipc.c
endif

libhelper_a_SOURCES = \
	$(IPC_SOURCE) \
	helper.c

noinst_LIBRARIES = \
	libhelper.a
