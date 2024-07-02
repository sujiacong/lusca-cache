#ifndef	__TCPTEST_TUNNEL_H__
#define	__TCPTEST_TUNNEL_H__

typedef struct {
    struct {
        int fd;
        int len;
        char *buf;
    } client, server;
    int connected;
    sqaddr_t peer;
} SslStateData;

extern void sslStart(int fd, sqaddr_t *peer);


#endif
