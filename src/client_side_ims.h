#ifndef	__CLIENT_SIDE_IMS_H__
#define	__CLIENT_SIDE_IMS_H__

extern void clientProcessExpired(clientHttpRequest *);
/* XXX this is likely not meant to be here */
extern int modifiedSince(StoreEntry * entry, request_t * request);

#endif
