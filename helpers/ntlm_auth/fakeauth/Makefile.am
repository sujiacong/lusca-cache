#
#  Makefile for the Squid Object Cache server
#
#  $Id: Makefile.am 8333 2001-08-31 11:19:36Z robertc $
#
#  Uncomment and customize the following to suit your needs:
#

libexec_PROGRAMS = fakeauth_auth
fakeauth_auth_SOURCES	= fakeauth_auth.c ntlm.h
INCLUDES      = -I. -I$(top_builddir)/include -I$(top_srcdir)/include \
		-I$(top_srcdir)/src/
LDADD	= -L$(top_builddir)/lib -lmiscutil $(CRYPTLIB) $(XTRA_LIBS)
