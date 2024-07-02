#ifndef	__LUSCA_LIBSQURL_URL_H__
#define	__LUSCA_LIBSQURL_URL_H__

extern char * url_convert_hex(char *org_url, int allocate);
extern int urlIsRelative(const char *url);
extern char * urlHostname(const char *url);
extern int urlMakeHttpCanonical(char *urlbuf, protocol_t protocol,
    const char *login, const char *host, int port, const char *urlpath,
    int urlpath_len);
extern int urlMakeHttpCanonical2(char *urlbuf, protocol_t protocol,
    const char *login, const char *host, int port, const char *urlpath,
    int urlpath_len);

#endif
