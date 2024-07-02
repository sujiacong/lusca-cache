#ifndef	__LUSCA_LIBSQURL_PROTO_H__
#define	__LUSCA_LIBSQURL_PROTO_H__

typedef enum {
    PROTO_NONE,
    PROTO_HTTP,
    PROTO_FTP,
    PROTO_GOPHER,
    PROTO_WAIS,
    PROTO_CACHEOBJ,
    PROTO_ICP, 
    PROTO_HTCP, 
    PROTO_URN,
    PROTO_WHOIS, 
    PROTO_INTERNAL,
    PROTO_HTTPS, 
    PROTO_MAX
} protocol_t;

extern const char *ProtocolStr[];

extern protocol_t urlParseProtocol(const char *s);
extern int urlDefaultPort(protocol_t p);

#endif
