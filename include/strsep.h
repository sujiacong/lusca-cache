#ifndef _LUSCA_STRSEP_H_
#define _LUSCA_STRSEP_H_ 

#if HAVE_STRSEP

/*
 * Get strsep() declaration.
 */
#include <string.h>

#else

/* strsep() definition from the FreeBSD libc strsep */
extern char *strsep (char **, const char *);

#endif

#endif /* _LUSCA_STRSEP_H_ */
