#ifndef	__LIBHELPER_HELPER_H__
#define	__LIBHELPER_HELPER_H__

#define HELPER_MAX_ARGS 64
#define REDIRECT_AV_FACTOR 1000

typedef void HLPCB(void *, char *buf);
typedef void HLPSCB(void *, void *lastserver, char *buf);
typedef int HLPSAVAIL(void *);
typedef void HLPSRESET(void *);

typedef struct _helper helper;
typedef struct _helper_stateful statefulhelper;
typedef struct _helper_server helper_server;
typedef struct _helper_stateful_server helper_stateful_server;
typedef struct _helper_request helper_request;
typedef struct _helper_stateful_request helper_stateful_request;

struct _helper_request {
    char *buf;
    HLPCB *callback;
    void *data;
    struct timeval dispatch_time;
    dlink_node n;
};

struct _helper_stateful_request {
    char *buf;
    HLPSCB *callback;
    void *data;
    struct timeval dispatch_time;
    dlink_node n;
};

struct _helper {
    wordlist *cmdline;
    dlink_list servers;
    dlink_list queue;
    const char *id_name;
    int n_to_start;
    int n_running;
    int n_active;
    int ipc_type;
    int concurrency;
    time_t last_queue_warn;
    struct {
        int requests;
        int replies;
        int queue_size;
        int max_queue_size;
        int avg_svc_time;
    } stats;
    time_t last_restart;
};


struct _helper_stateful {
    wordlist *cmdline;
    dlink_list servers;
    dlink_list queue;
    const char *id_name;
    int n_to_start;
    int n_running;
    int n_active;
    int ipc_type;
    int concurrency;
    MemPool *datapool;
    HLPSAVAIL *IsAvailable;
    HLPSRESET *Reset;
    time_t last_queue_warn;
    struct {
        int requests;
        int replies;
        int queue_size;
        int max_queue_size;
        int avg_svc_time;
    } stats;
    time_t last_restart;
};

struct _helper_server {
    int index;
    int pid;
    int rfd;
    int wfd;
    MemBuf wqueue;
    char *rbuf;
    size_t rbuf_sz;
    int roffset;
    dlink_node link;
    helper *parent;
    helper_request **requests;
    struct _helper_flags {
        unsigned int writing:1;
        unsigned int closing:1;
        unsigned int shutdown:1;
    } flags;
    struct {
        int uses;
        unsigned int pending;
    } stats;
    void *hIpc;
};

struct _helper_stateful_server {
    int index;
    int pid;
    int rfd;
    int wfd;
    char *buf;
    size_t buf_sz;
    int offset;
    struct timeval dispatch_time;
    struct timeval answer_time;
    dlink_node link;
    statefulhelper *parent;
    helper_stateful_request *request;
    struct _helper_stateful_flags {
        unsigned int alive:1;
        unsigned int busy:1;
        unsigned int closing:1;
        unsigned int shutdown:1;
        unsigned int reserved:1;
    } flags;
    struct {
        int uses;
        int submits;
        int releases;
    } stats;
    void *data;                 /* State data used by the calling routines */
    void *hIpc;
};

extern void helperInitMem(void);
extern void helperOpenServers(helper * hlp);
extern void helperStatefulOpenServers(statefulhelper * hlp);
extern void helperSubmit(helper * hlp, const char *buf, HLPCB * callback, void *data);
extern void helperStatefulSubmit(statefulhelper * hlp, const char *buf, HLPSCB * callback, void *data, helper_stateful_server * lastserver);
extern void helperShutdown(helper * hlp);
extern void helperStatefulShutdown(statefulhelper * hlp);
extern helper *helperCreate(const char *);
extern statefulhelper *helperStatefulCreate(const char *);
extern void helperFree(helper *);
extern void helperStatefulFree(statefulhelper *);
extern void helperStatefulReset(helper_stateful_server * srv);
extern void helperStatefulReleaseServer(helper_stateful_server * srv);
extern void *helperStatefulServerGetData(helper_stateful_server * srv);
extern helper_stateful_server *helperStatefulGetServer(statefulhelper *);


#endif
