#ifndef	__CLIENT_SIDE_BODY_H__
#define	__CLIENT_SIDE_BODY_H__

extern void clientProcessBody(ConnStateData * conn);
extern BODY_HANDLER clientReadBody;
extern void clientEatRequestBody(clientHttpRequest * http);

#endif
