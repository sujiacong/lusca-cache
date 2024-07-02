#ifndef	__LUSCA_WIN32_ERROR_H__
#define	__LUSCA_WIN32_ERROR_H__

extern const char * wsastrerror(int err);
extern const char * WIN32_strerror(int err);
extern void WIN32_maperror(unsigned long WIN32_oserrno);

#endif
