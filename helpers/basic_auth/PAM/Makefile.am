#
#  Makefile for the Squid PAM authentication helper
#
#  $Id: Makefile.am 10514 2005-05-17 16:56:44Z hno $
#
#  Uncomment and customize the following to suit your needs:
#

INCLUDES	= -I$(top_srcdir)/include

man_MANS		= pam_auth.8
EXTRA_DIST		= pam_auth.8
libexec_PROGRAMS	= pam_auth
LDADD			= -L$(top_builddir)/lib -lmiscutil -lpam $(XTRA_LIBS)
