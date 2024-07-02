#ifndef	__LIBHTTP_HTTPVERSION_H__
#define	__LIBHTTP_HTTPVERSION_H__

struct _http_version_t {
    unsigned int major;
    unsigned int minor;
};

typedef struct _http_version_t http_version_t;

extern void httpBuildVersion(http_version_t * version, unsigned int major, unsigned int minor);

#endif
