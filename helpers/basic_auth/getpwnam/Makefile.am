#
#  Makefile for the Squid Object Cache server
#
#  $Id: Makefile.am 10514 2005-05-17 16:56:44Z hno $
#
#  Uncomment and customize the following to suit your needs:
#


libexec_PROGRAMS = getpwname_auth

getpwname_auth_SOURCES = getpwnam_auth.c 

INCLUDES      = -I$(top_srcdir)/include
LDADD = -L$(top_builddir)/lib -lmiscutil $(CRYPTLIB) $(XTRA_LIBS)
