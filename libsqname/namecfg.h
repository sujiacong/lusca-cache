#ifndef	__LIBSQNAME_NAMECFG_H__
#define	__LIBSQNAME_NAMECFG_H__

extern int namecache_dns_skiptests;
extern int namecache_dns_positive_ttl;
extern int namecache_dns_negative_ttl;
extern int namecache_ipcache_size;
extern int namecache_ipcache_high;
extern int namecache_ipcache_low;

extern int namecache_fqdncache_size;
extern int namecache_fqdncache_logfqdn;

extern const char *dns_error_message;   /* NULL */

#endif
