#ifndef	__LUSCA_FTPPARTS_H__
#define	__LUSCA_FTPPARTS_H__

typedef struct {
    char type;
    squid_off_t size;
    char *date;
    char *name;
    char *showname;
    char *link;
} ftpListParts;

extern void ftpListPartsFree(ftpListParts ** parts);
extern ftpListParts * ftpListParseParts(const char *buf, struct _ftp_flags flags);

#endif
