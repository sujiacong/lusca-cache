#ifndef	__LUSCA_FTPUTIL_H__
#define	__LUSCA_FTPUTIL_H__

extern char * escapeIAC(const char *buf);
extern char * decodeTelnet(char *buf);
extern int is_month(const char *buf);

#endif
