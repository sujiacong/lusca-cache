#ifndef	__LUSCA_ICMP_H__
#define	__LUSCA_ICMP_H__

extern void icmpOpen(void);
extern void icmpClose(void);
extern void icmpSourcePing(struct in_addr to, const icp_common_t *, const char *url);
extern void icmpDomainPing(struct in_addr to, const char *domain);

#endif
