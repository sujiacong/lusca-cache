#ifndef	__LIBASYNCIO_AIOPS_H__
#define	__LIBASYNCIO_AIOPS_H__


enum _squidaio_thread_status {
    _THREAD_STARTING = 0,
    _THREAD_WAITING,
    _THREAD_BUSY,
    _THREAD_FAILED,
    _THREAD_DONE
};
typedef enum _squidaio_thread_status squidaio_thread_status; 

enum _squidaio_request_type { 
    _AIO_OP_NONE = 0,
    _AIO_OP_OPEN,
    _AIO_OP_READ,
    _AIO_OP_WRITE,
    _AIO_OP_CLOSE,
    _AIO_OP_UNLINK,
    _AIO_OP_TRUNCATE,
    _AIO_OP_OPENDIR,
    _AIO_OP_STAT
};
typedef enum _squidaio_request_type squidaio_request_type;

struct _squidaio_result_t {
    int aio_return;
    int aio_errno;
    void *_data;                /* Internal housekeeping */
    void *data;                 /* Available to the caller */
};
typedef struct _squidaio_result_t squidaio_result_t;

typedef struct squidaio_request_t {
    struct squidaio_request_t *next;
    squidaio_request_type request_type;
    int cancelled;
    char *path;
    int oflag;
    mode_t mode;
    int fd;
    char *bufferp;
    int buflen;
    off_t offset;
    int ret;
    int err;
    struct stat *tmpstatp;
    struct stat *statp;
    squidaio_result_t *resultp;
} squidaio_request_t;

#ifndef _SQUID_MSWIN_
typedef struct squidaio_request_queue_t {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    squidaio_request_t *volatile head;
    squidaio_request_t *volatile *volatile tailp;
    unsigned long requests;
    unsigned long blocked;      /* main failed to lock the queue */
} squidaio_request_queue_t;   
    
typedef struct squidaio_thread_t squidaio_thread_t;
struct squidaio_thread_t {
    squidaio_thread_t *next;
    pthread_t thread;
    squidaio_thread_status status;
    struct squidaio_request_t *current_req;
    unsigned long requests;
};
#else
typedef struct squidaio_request_queue_t {
    HANDLE mutex;
    HANDLE cond;
    squidaio_request_t *volatile head;
    squidaio_request_t *volatile *volatile tailp;
    unsigned long requests;
    unsigned long blocked;      /* main failed to lock the queue */
} squidaio_request_queue_t;   
    
typedef struct squidaio_thread_t squidaio_thread_t;
struct squidaio_thread_t {
    squidaio_thread_t *next;
    HANDLE thread;
    DWORD dwThreadId;
    squidaio_thread_status status;
    struct squidaio_request_t *current_req;
    unsigned long requests;
    int volatile exit;
};
#endif

extern int aiops_default_ndirs;

void squidaio_init(void);
void squidaio_shutdown(void);
int squidaio_cancel(squidaio_result_t *);
int squidaio_open(const char *, int, mode_t, squidaio_result_t *);
int squidaio_read(int, char *, int, off_t, squidaio_result_t *);
int squidaio_write(int, char *, int, off_t, squidaio_result_t *);
int squidaio_close(int, squidaio_result_t *);
int squidaio_stat(const char *, struct stat *, squidaio_result_t *);
int squidaio_unlink(const char *, squidaio_result_t *);
int squidaio_truncate(const char *, off_t length, squidaio_result_t *);
int squidaio_opendir(const char *, squidaio_result_t *);
squidaio_result_t *squidaio_poll_done(void);
int squidaio_operations_pending(void);
int squidaio_sync(void);
int squidaio_get_queue_len(void);
squidaio_thread_t * squidaio_get_thread_head(void);


#endif
