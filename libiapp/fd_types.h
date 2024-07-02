#ifndef	__LIBIAPP_FD_TYPES_H__
#define	__LIBIAPP_FD_TYPES_H__

/* how long the per-fd description is */
#define FD_DESC_SZ              64

enum {
    FD_NONE,
    FD_LOG,
    FD_FILE,
    FD_SOCKET,
    FD_PIPE,
    FD_UNKNOWN
};

enum {
    FD_READ,
    FD_WRITE
};


#endif
