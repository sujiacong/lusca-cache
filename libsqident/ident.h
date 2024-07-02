#ifndef	__LIBSQIDENT_IDENT_H__
#define	__LIBSQIDENT_IDENT_H__

#if USE_IDENT

typedef void IDCB(const char *ident, void *data);

extern void identStart(sqaddr_t *me, sqaddr_t *my_peer, IDCB * callback, void *cbdata);
extern void identStart4(struct sockaddr_in *me, struct sockaddr_in *my_peer, IDCB * callback, void *cbdata);
extern void identInit(void);
extern void identConfigTimeout(int timeout);

#endif

#endif
