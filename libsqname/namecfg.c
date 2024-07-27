#include <stdio.h>

int namecache_dns_skiptests = 1;
int namecache_dns_positive_ttl = 0;
int namecache_dns_negative_ttl = 0;
int namecache_ipcache_size = 0;
int namecache_ipcache_high = 0;
int namecache_ipcache_low = 0;

int namecache_fqdncache_size = 0;
int namecache_fqdncache_logfqdn = 0;

const char *dns_error_message = NULL;
