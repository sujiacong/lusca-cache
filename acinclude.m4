dnl AC_CHECK_SIZEOF_SYSTYPE is as the standard AC_CHECK_SIZEOF macro
dnl but also capable of checking the size of system defined types, not
dnl only compiler defined types.
dnl
dnl AC_CHECK_SYSTYPE is the same thing but replacing AC_CHECK_TYPE
dnl However AC_CHECK_TYPE is not by far as limited as AC_CHECK_SIZEOF
dnl (it at least makes use of <sys/types.h>, <stddef.h> and <stdlib.h>)

dnl AC_CHECK_SIZEOF_SYSTYPE(TYPE [, CROSS-SIZE])
AC_DEFUN([AC_CHECK_SIZEOF_SYSTYPE],
[
AC_REQUIRE([AC_HEADER_STDC])dnl
AC_CHECK_SIZEOF($1, ,
[
#include <stdio.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_BITYPES_H
#include <sys/bitypes.h>
#endif
])
])dnl

dnl aborts with an error specified as the second argument if the first argument doesn't
dnl contain either "yes" or "no"
AC_DEFUN([SQUID_YESNO],[
if test "$1" != "yes" -a "$1" != "no" ; then
  AC_MSG_ERROR([$2])
fi
])

# check if the compiler accepts a supplied flag
# first argument is the variable containing the result
# (will be set to "yes" or "no")
# second argument is the flag to be tested, verbatim
#
AC_DEFUN([SQUID_CC_CHECK_ARGUMENT],[
  AC_CACHE_CHECK([whether compiler accepts $2],[$1],
  [{
    AC_REQUIRE([AC_PROG_CC])
    SAVED_FLAGS="$CFLAGS"
    SAVED_CXXFLAGS="$CXXFLAGS"
    CFLAGS="$CFLAGS $2"
    CXXFLAGS="$CXXFLAGS $2"
    AC_TRY_LINK([],[int foo; ],
      [$1=yes],[$1=no])
    CFLAGS="$SAVED_CFLAGS"
    CXXFLAGS="$SAVED_CXXFLAGS"
  }])
])

dnl like AC_DEFINE, but it defines the value to 0 or 1 using well-known textual
dnl conventions:
dnl 1: "yes", "true", 1
dnl 0: "no" , "false", 0, ""
dnl aborts with an error for unknown values
AC_DEFUN([SQUID_DEFINE_BOOL],[
squid_tmp_define=""
case "$2" in 
  yes|true|1) squid_tmp_define="1" ;;
  no|false|0|"") squid_tmp_define="0" ;;
  *) AC_MSG_ERROR([SQUID_DEFINE[]_BOOL: unrecognized value for $1: '$2']) ;;
esac
ifelse([$#],3,
  [AC_DEFINE_UNQUOTED([$1], [$squid_tmp_define],[$3])],
  [AC_DEFINE_UNQUOTED([$1], [$squid_tmp_define])]
)
unset squid_tmp_define
])

dnl AC_CHECK_SYSTYPE(TYPE, DEFAULT)
AC_DEFUN([AC_CHECK_SYSTYPE],
[AC_REQUIRE([AC_HEADER_STDC])dnl
AC_CHECK_TYPE($1, ,
[AC_DEFINE_UNQUOTED($1, $2, [Define to '$2' if not defined])], 
[
/* What a mess.. many systems have added the (now standard) bit types
 * in their own ways, so we need to scan a wide variety of headers to
 * find them..
 */
#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_BITYPES_H
#include <sys/bitypes.h>
#endif
])
])dnl
