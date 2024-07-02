#ifndef	__LIBSQINET_LEGACY_INET_H__
#define	__LIBSQINET_LEGACY_INET_H__

extern const char *xinet_ntoa(const struct in_addr addr);
extern int IsNoAddr(const struct in_addr *a);
extern int IsAnyAddr(const struct in_addr *a);
extern void SetNoAddr(struct in_addr *a);
extern void SetAnyAddr(struct in_addr *a);

#endif
