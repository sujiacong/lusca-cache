#ifndef	__LIBSQNAME_IPCACHE_H__
#define	__LIBSQNAME_IPCACHE_H__

#define IP_LOOKUP_IF_MISS       0x01

struct _ipcache_addrs {
    struct in_addr *in_addrs;
    unsigned char *bad_mask;
    unsigned char count;
    unsigned char cur;
    unsigned char badcount;
};
typedef struct _ipcache_addrs ipcache_addrs;

typedef void IPH(const ipcache_addrs *, void *);

typedef struct _ipcache_entry ipcache_entry;
struct _ipcache_entry {
    hash_link hash;             /* must be first */
    time_t lastref;
    time_t expires;
    ipcache_addrs addrs;
    IPH *handler;
    void *handlerData;
    char *error_message;
    struct timeval request_time;
    dlink_node lru;
    unsigned short locks;
    struct {
        unsigned int negcached:1;
        unsigned int fromhosts:1;
    } flags;
};

struct _IpcacheStatStruct {
    int requests;
    int replies;
    int hits;
    int misses;
    int negative_hits;
    int numeric_hits;
    int invalid;
};
typedef struct _IpcacheStatStruct IpcacheStatStruct;

extern dlink_list ipcache_lru_list;
extern MemPool * pool_ipcache;
extern IpcacheStatStruct IpcacheStats;
extern hash_table *ip_table;


extern void ipcache_nbgethostbyname(const char *name,
    IPH * handler,
    void *handlerData);
extern EVH ipcache_purgelru;
extern const ipcache_addrs *ipcache_gethostbyname(const char *, int flags);
extern void ipcacheInvalidate(const char *);
extern void ipcacheInvalidateNegative(const char *);
extern void ipcache_init(wordlist *testhosts);
extern void ipcacheCycleAddr(const char *name, ipcache_addrs *);
extern void ipcacheMarkBadAddr(const char *name, struct in_addr);
extern void ipcacheMarkGoodAddr(const char *name, struct in_addr);
extern void ipcacheFreeMemory(void);
extern ipcache_addrs *ipcacheCheckNumeric(const char *name);
extern void ipcache_restart(void);
extern int ipcacheAddEntryFromHosts(const char *name, const char *ipaddr);
extern int ipcacheFlushAll(void);


#endif

