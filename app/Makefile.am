## Process this file with automake to produce Makefile.in
#
# $Id: Makefile.am 12826 2008-06-08 13:51:32Z adrian.chadd $
#

if ENABLE_WIN32SPECIFIC
TEST_SUITE =
else
TEST_SUITE = tcptest
endif

DIST_SUBDIRS	= ufs_rebuild ufs_newfs coss_newfs coss_rebuild $(TEST_SUITE)
SUBDIRS		= ufs_rebuild ufs_newfs coss_newfs coss_rebuild $(TEST_SUITE)

