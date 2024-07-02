#ifndef	__LIBSQDNS__DNS_INTERNAL_H__
#define	__LIBSQDNS__DNS_INTERNAL_H__

#ifndef _PATH_RESCONF
#define _PATH_RESCONF "/etc/resolv.conf"
#endif
#ifndef NS_DEFAULTPORT
#define NS_DEFAULTPORT 53
#endif

#ifndef NS_MAXDNAME
#define NS_MAXDNAME 1025
#endif
 
#ifndef MAXDNSRCH
#define MAXDNSRCH 6
#endif

#ifndef RES_MAXNDOTS
#define RES_MAXNDOTS 15
#endif
  
/* The buffer size required to store the maximum allowed search path */
#ifndef RESOLV_BUFSZ  
#define RESOLV_BUFSZ NS_MAXDNAME * MAXDNSRCH + sizeof("search ") + 1
#endif


#define IDNS_MAX_TRIES 20
#define MAX_RCODE 6
#define MAX_ATTEMPT 3

typedef struct _ns ns;
typedef struct _idns_query idns_query;
typedef struct _sp sp;

struct _idns_query {
    hash_link hash;
    rfc1035_query query;
    char buf[RESOLV_BUFSZ];
    char name[NS_MAXDNAME + 1];
    char orig[NS_MAXDNAME + 1];
    ssize_t sz;
    unsigned short id;
    int nsends;
    struct timeval start_t;
    struct timeval sent_t;
    struct timeval queue_t;
    dlink_node lru;
    IDNSCB *callback;
    void *callback_data;
    int attempt;
    const char *error;
    int rcode;
    idns_query *queue;
    unsigned short domain;
    unsigned short do_searchpath;
    int tcp_socket;
    char *tcp_buffer;
    size_t tcp_buffer_size;
    size_t tcp_buffer_offset;
};

struct _ns {
    sqaddr_t S;
    int nqueries;
    int nreplies;
};

struct _sp {
    char domain[NS_MAXDNAME];
    int queries;
};


typedef struct {
        sqaddr_t udp4_incoming, udp4_outgoing;
        sqaddr_t udp6_incoming, udp6_outgoing;
        int ignore_unknown_nameservers;
        int idns_retransmit;
        int idns_query;
        int res_defnames;
        int ndots;
} DnsConfigStruct;

extern DnsConfigStruct DnsConfig;
extern int RcodeMatrix[MAX_RCODE][MAX_ATTEMPT];
extern ns *nameservers;
extern sp *searchpath;
extern int nns;
extern int npc;
extern dlink_list idns_lru_list;

extern void idnsConfigure(int ignore_unknown_nameservers, int idns_retransmit,
    int idns_query, int res_defnames);
extern void idnsConfigureV4Addresses(sqaddr_t *incoming_addr, sqaddr_t *outgoing_addr);
extern void idnsConfigureV6Addresses(sqaddr_t *incoming_addr, sqaddr_t *outgoing_addr);

extern void idnsAddNameserver(const char *buf);
extern void idnsAddPathComponent(const char *buf);
extern void idnsFreeNameservers(void);
extern void idnsFreeSearchpath(void);
extern int idnsGetNdots(void);

extern void idnsInit(void);
extern void idnsShutdown(void);
extern void idnsALookup(const char *, IDNSCB *, void *);
extern void idnsPTRLookup(const struct in_addr, IDNSCB *, void *);

#endif
