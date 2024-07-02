#ifndef	__LIBIAPP_COMM_IPS_H__
#define	__LIBIAPP_COMM_IPS_H__


/* "Local" bind spoofing is for listen sockets wishing to listen on non-local addresses */
extern int comm_ips_bind_lcl(int fd, sqaddr_t *a);

/* "Remote" bind spoofing is for connect sockets wishing to bind to a non-local address */
extern int comm_ips_bind_rem(int fd, sqaddr_t *a);

extern void comm_ips_keepCapabilities(void);
extern void comm_ips_restoreCapabilities(void);

#endif
