#ifndef	__LUSCA_PCONN_H__
#define	__LUSCA_PCONN_H__

extern void pconnPush(int, const char *host, u_short port, const char *domain, struct in_addr *client_address, u_short client_port);
extern int pconnPop(const char *host, u_short port, const char *domain, struct in_addr *client_address, u_short client_port, int *idle);
extern void pconnInit(void); 

#endif
