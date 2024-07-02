#ifndef	__LIBSQNAME_FQDNCACHE_H__
#define	__LIBSQNAME_FQDNCACHE_H__

#define FQDN_LOW_WATER       90
#define FQDN_HIGH_WATER      95
#define FQDN_LOOKUP_IF_MISS     0x01
#define FQDN_MAX_NAMES 5

typedef void FQDNH(const char *, void *);

typedef struct _fqdncache_entry fqdncache_entry;
struct _fqdncache_entry {
    hash_link hash;             /* must be first */
    time_t lastref;
    time_t expires;
    unsigned char name_count;
    char *names[FQDN_MAX_NAMES + 1];
    FQDNH *handler;
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

struct _FqdncacheStatStruct {
    int requests;
    int replies;
    int hits;
    int misses;
    int negative_hits;
};
typedef struct _FqdncacheStatStruct FqdncacheStatStruct;


extern MemPool * pool_fqdncache;
extern hash_table *fqdn_table;
extern FqdncacheStatStruct FqdncacheStats;

extern void fqdncache_nbgethostbyaddr(struct in_addr, FQDNH *, void *);
extern const char *fqdncache_gethostbyaddr(struct in_addr, int flags);
extern void fqdncache_init(void);
extern void fqdncacheReleaseInvalid(const char *);
extern const char *fqdnFromAddr(struct in_addr);
extern int fqdncacheQueueDrain(void);
extern void fqdncacheFreeMemory(void);
extern void fqdncache_restart(void);
extern EVH fqdncache_purgelru;
extern void fqdncacheAddEntryFromHosts(char *addr, wordlist * hostnames);
extern int fqdncacheFlushAll(void);

#endif
