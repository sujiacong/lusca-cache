#ifndef	__CLIENT_SIDE_REQUEST_H__
#define	__CLIENT_SIDE_REQUEST_H__

/* This begins the request-side processing chain */
extern void clientCheckFollowXForwardedFor(void *data);
/* This ends the request-side rewriting and continues the request-side processing chain */
extern void clientFinishRewriteStuff(clientHttpRequest * http);

#endif

