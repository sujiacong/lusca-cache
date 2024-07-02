#ifndef	__LIBCORE_VARARGS_H__
#define	__LIBCORE_VARARGS_H__


#include "../include/config.h"

#if defined(HAVE_STDARG_H)
#include <stdarg.h>
#define HAVE_STDARGS            /* let's hope that works everywhere (mj) */
#define VA_LOCAL_DECL va_list ap;
#define VA_START(f) va_start(ap, f)
#define VA_SHIFT(v,t) ;         /* no-op for ANSI */
#define VA_END va_end(ap)
#else
#if defined(HAVE_VARARGS_H)
#include <varargs.h>
#undef HAVE_STDARGS
#define VA_LOCAL_DECL va_list ap;
#define VA_START(f) va_start(ap)        /* f is ignored! */
#define VA_SHIFT(v,t) v = va_arg(ap,t)
#define VA_END va_end(ap)
#else
#error XX **NO VARARGS ** XX
#endif
#endif


#endif	/* __LIBCORE_VARARGS_H__ */
