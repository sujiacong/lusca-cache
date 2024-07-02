#
#  Makefile for the Squid Object Cache server
#
#  $Id: Makefile.am 10514 2005-05-17 16:56:44Z hno $
#
#

libexec_PROGRAMS = yp_auth
yp_auth_SOURCES	= yp_auth.c nis_support.h nis_support.c
LDADD	= -L$(top_builddir)/lib -lmiscutil $(CRYPTLIB) $(XTRA_LIBS)
INCLUDES	= -I$(top_srcdir)/include
