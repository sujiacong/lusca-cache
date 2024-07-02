/*
 * async_io.h
 *
 * Internal declarations for the aufs routines
 */

#ifndef __LIBASYNCIO_ASYNC_IO_H__
#define __LIBASYNCIO_ASYNC_IO_H__

extern int n_asyncufs_dirs;
extern int squidaio_nthreads;
extern int squidaio_magic1;
extern int squidaio_magic2;

struct squidaio_stat {
    int open;
    int close;
    int cancel;
    int write;
    int read;
    int stat;
    int unlink;
    int check_callback;
};

extern struct squidaio_stat squidaio_counts;

/* Base number of threads if not specified to configure.
 * Weighted by number of directories (see aiops.c) */
#define THREAD_FACTOR 16

/* Queue limit where swapouts are deferred (load calculation) */
#define MAGIC1_FACTOR 10
#define MAGIC1 squidaio_magic1
/* Queue limit where swapins are deferred (open/create fails) */
#define MAGIC2_FACTOR 20
#define MAGIC2 squidaio_magic2

typedef void AIOCB(int fd, void *cbdata, const char *buf, int aio_return, int aio_errno);

void aioInit(void);
void aioDone(void);
void aioCancel(int);
void aioOpen(const char *, int, mode_t, AIOCB *, void *);
void aioClose(int);
void aioWrite(int, off_t offset, char *, int size, AIOCB *, void *, FREE *);
void aioRead(int, off_t offset, int size, AIOCB *, void *);
void aioStat(char *, struct stat *, AIOCB *, void *);
void aioUnlink(const char *, AIOCB *, void *);
void aioTruncate(const char *, off_t length, AIOCB *, void *);
int aioCheckCallbacks(void);
void aioSync(void);
int aioQueueSize(void);

#endif
