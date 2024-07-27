#ifndef	__LIBIAPP_COMM_H__
#define	__LIBIAPP_COMM_H__

/*
 * Hey dummy, don't be tempted to move this to lib/config.h.in
 * again.  O_NONBLOCK will not be defined there because you didn't
 * #include <fcntl.h> yet.
 */
#if defined(_SQUID_SUNOS_)
/*
 * We assume O_NONBLOCK is broken, or does not exist, on SunOS.
 */
#define SQUID_NONBLOCK O_NDELAY
#elif defined(O_NONBLOCK)
/*
 * We used to assume O_NONBLOCK was broken on Solaris, but evidence
 * now indicates that its fine on Solaris 8, and in fact required for
 * properly detecting EOF on FIFOs.  So now we assume that if  
 * its defined, it works correctly on all operating systems.
 */
#define SQUID_NONBLOCK O_NONBLOCK
/*
 * O_NDELAY is our fallback.
 */
#else
#define SQUID_NONBLOCK O_NDELAY
#endif

#define FD_READ_METHOD(fd, buf, len) (*fd_table[fd].read_method)(fd, buf, len)
#define FD_WRITE_METHOD(fd, buf, len) (*fd_table[fd].write_method)(fd, buf, len)

typedef struct _close_handler close_handler;

typedef void PF(int, void *);
typedef int  CDT(int, void *);
typedef void CWCB(int fd, char *, size_t size, int flag, void *data);
typedef void CRCB(int fd, int size, int flag, int xerrno, void *data);
typedef void CNCB(int fd, int status, void *);
typedef int DEFER(int fd, void *data);
typedef int READ_HANDLER(int, char *, int);
typedef int WRITE_HANDLER(int, const char *, int);
typedef void CBCB(char *buf, ssize_t size, void *data);

struct _close_handler {
    PF *handler;
    void *data;
    close_handler *next;
};

struct _CommWriteStateData {
    int valid;
    char *buf;
    size_t size;
    size_t offset;
    CWCB *handler;
    void *handler_data;
    FREE *free_func;
    char header[32];
    size_t header_size;
};
typedef struct _CommWriteStateData CommWriteStateData;

struct _fde {
    unsigned int type;
    u_short local_port;
    u_short remote_port;
    sqaddr_t local_address;
    sqaddr_t remote_address;
    unsigned char tos;
    char ipaddrstr[MAX_IPSTRLEN]; /* dotted decimal address of peer - XXX should be MAX_IPSTRLEN */
    const char *desc;
    char descbuf[FD_DESC_SZ];
    struct {
        unsigned int open:1;
        unsigned int close_request:1;
        unsigned int closing:1;
        unsigned int socket_eof:1;
        unsigned int nolinger:1;
        unsigned int nonblocking:1;
        unsigned int ipc:1;
        unsigned int called_connect:1;
        unsigned int nodelay:1;
        unsigned int close_on_exec:1;
        unsigned int backoff:1; /* keep track of whether the fd is backed off */
        unsigned int dnsfailed:1;       /* did the dns lookup fail */
	unsigned int tproxy_lcl:1;		/* should this listen socket have its listen details spoofed via comm_ips_lcl_bind()? */
	unsigned int tproxy_rem:1;		/* should the source address of this FD be spoofed via comm_ips_rem_bind()? */
    } flags;
    comm_pending read_pending;
    comm_pending write_pending;
    squid_off_t bytes_read;
    squid_off_t bytes_written;
    int uses;                   /* ie # req's over persistent conn */
    struct {
    	struct {
		char *buf;
		int size;
		CRCB *cb;
		void *cbdata;
		int active;
    	} read;
	struct {
		CNCB *cb;
		void *cbdata;
		sqaddr_t addr;
		int active;
	} connect;
    } comm;
    PF *read_handler;
    void *read_data;
    PF *write_handler;
    void *write_data;
    PF *timeout_handler;
    time_t timeout;
    void *timeout_data;
    void *lifetime_data;
    close_handler *close_handler;       /* linked list */
    DEFER *defer_check;         /* check if we should defer read */
    void *defer_data;
    struct _CommWriteStateData rwstate;         /* State data for comm_write */
    READ_HANDLER *read_method;
    WRITE_HANDLER *write_method;
#if USE_SSL
    SSL *ssl;
#endif
#ifdef _SQUID_MSWIN_
    struct {
        long handle;
    } win32;
#endif
#if DELAY_POOLS
    int slow_id;
#endif
};

typedef struct _fde fde;

/* .. XXX how the hell will this be threaded? */
struct _CommStatStruct {
    struct {
        struct {
            int opens;
            int closes;
            int reads;
            int writes;
            int seeks;
            int unlinks;
        } disk;
        struct {
            int accepts;
            int sockets;
            int connects;
            int binds;
            int closes;
            int reads;
            int writes;
            int recvfroms;
            int sendtos;
        } sock;
        int polls;
        int selects;
    } syscalls;
    int select_fds;
    int select_loops;
    int select_time;
};

typedef struct _CommStatStruct CommStatStruct;

extern void fd_init(void);
extern void fd_close(int fd);
extern void fd_open(int fd, unsigned int type, const char *);
extern void fd_note(int fd, const char *);
extern void fd_note_static(int fd, const char *);
extern void fd_bytes(int fd, int len, unsigned int type);
extern void fdFreeMemory(void);
extern void fdDumpOpen(void);
extern int fdNFree(void);
extern int fdUsageHigh(void);
extern void fdAdjustReserved(void);

extern int commSetNonBlocking(int fd);
extern int commUnsetNonBlocking(int fd);
extern void commSetCloseOnExec(int fd);
extern int commSetTcpBufferSize(int fd, int buffer_size);
extern void commSetTcpKeepalive(int fd, int idle, int interval, int timeout);
extern int commGetSocketTos(int fd);
extern int commSetTos(int fd, int tos);
extern int commSetSocketPriority(int fd, int prio);
extern int commSetIPOption(int fd, uint8_t option, void *value, size_t size);
extern int comm_accept(int fd, sqaddr_t *, sqaddr_t *);
extern void comm_close(int fd);
extern void comm_reset_close(int fd);
#if LINGERING_CLOSE
extern void comm_lingering_close(int fd);
#endif
extern void commSetNoPmtuDiscover(int fd);

extern int comm_connect_addr(int sock, const sqaddr_t *addr);
extern void comm_connect_begin(int fd, const sqaddr_t *addr, CNCB *cb, void *cbdata);
extern void comm_init(void);
extern int comm_listen(int sock);
extern int comm_open(int, int, struct in_addr, u_short, comm_flags_t flags, unsigned char TOS, const char *);
extern int comm_fdopen(int, int, struct in_addr, u_short, comm_flags_t flags, unsigned char, const char *);
extern int comm_open6(int, int, sqaddr_t *addr, comm_flags_t flags, unsigned char TOS, const char *);
extern int comm_fdopen6(int, int, sqaddr_t *addr, comm_flags_t flags, unsigned char, const char *);
extern u_short comm_local_port(int fd);

extern void commDeferFD(int fd);
extern void commResumeFD(int fd);
extern void commSetSelect(int, unsigned int, PF *, void *, time_t);
extern void commRemoveSlow(int fd);
extern void comm_add_close_handler(int fd, PF *, void *);
extern void comm_remove_close_handler(int fd, PF *, void *);
extern void comm_condition_remove_close_handler(int fd, PF *, CDT *);
extern int comm_udp_sendto(int, const struct sockaddr_in *, int, const void *, int);
extern int comm_udp_sendto6(int, const sqaddr_t *, const void *, int);
extern void comm_write(int fd,
    const char *buf,
    int size,
    CWCB * handler,
    void *handler_data,
    FREE *);
extern void comm_write_mbuf(int fd, MemBuf mb, CWCB * handler, void *handler_data);
extern void comm_write_header(int fd,
    const char *buf,
    int size,
    const char *header,
    size_t header_size,
    CWCB * handler,
    void *handler_data,
    FREE *);
extern void comm_write_mbuf_header(int fd, MemBuf mb, const char *header, size_t header_size, CWCB * handler, void *handler_data);
#if 0
/* comm_read / comm_read_cancel two functions are in testing and not to be used! */
extern void comm_read(int fd, char *buf, int size, CRCB *cb, void *data);
extern int comm_read_cancel(int fd);
#endif
extern int comm_open_uds(int sock_type,int proto,struct sockaddr_un* addr,int flags);
extern void comm_import_opened(int fd, int ai_socktype, const char *note, sqaddr_t *addr, int flags);
extern void commCallCloseHandlers(int fd);
extern int commSetTimeout(int fd, int, PF *, void *);
extern void commSetDefer(int fd, DEFER * func, void *);
extern int ignoreErrno(int);
extern void commCloseAllSockets(void);
extern int commBind(int s, sqaddr_t *addr);
extern void commSetTcpNoDelay(int);
extern void commSetTcpRcvbuf(int, int);
extern void commSetReuseAddr(int fd);

extern int comm_create_fifopair(int *prfd, int *pwfd, int *crfd, int *cwfd);
extern int comm_create_unix_stream_pair(int *prfd, int *pwfd, int *crfd, int *cwfd, int buflen);
extern int comm_create_unix_dgram_pair(int *prfd, int *pwfd, int *crfd, int *cwfd);


/*
 * comm_select.c
 */
extern void comm_select_init(void);
extern void comm_select_postinit(void);
extern void comm_select_shutdown(void);
extern int comm_select(int);
extern void commUpdateEvents(int fd);
extern void commSetEvents(int fd, int need_read, int need_write);
extern void commClose(int fd);
extern void commOpen(int fd);
extern void commUpdateReadHandler(int, PF *, void *);
extern void commUpdateWriteHandler(int, PF *, void *);
extern void comm_quick_poll_required(void);
extern const char * comm_select_status(void);

extern fde *fd_table;
extern int Biggest_FD;          /* -1 */
extern int Number_FD;           /* 0 */
extern int Opening_FD;          /* 0 */
extern int Squid_MaxFD;         /* SQUID_MAXFD */
extern int RESERVED_FD;

extern struct in_addr local_addr;
extern struct in_addr no_addr;

extern CommStatStruct CommStats;


#endif
