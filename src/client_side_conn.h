#ifndef	__CLIENT_SIDE_CONN_H__
#define	__CLIENT_SIDE_CONN_H__

extern ConnStateData * connStateCreate(int fd, sqaddr_t *peer, sqaddr_t *me);
extern int connStateGetCount(void);

#endif
