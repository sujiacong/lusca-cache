#ifndef	__LIBHELPER_IPC_H__
#define	__LIBHELPER_IPC_H__

#define IPC_NONE 0
#define IPC_TCP_SOCKET 1
#define IPC_UDP_SOCKET 2
#define IPC_FIFO 3
#define IPC_UNIX_STREAM 4
#define IPC_UNIX_DGRAM 5

#if HAVE_SOCKETPAIR && defined (AF_UNIX)
#define IPC_STREAM IPC_UNIX_STREAM
#else
#define IPC_STREAM IPC_TCP_SOCKET
#endif

/*
 * Do NOT use IPC_UNIX_DGRAM here because you can't
 * send() more than 4096 bytes on a socketpair() socket
 * at least on FreeBSD
 */
#if HAVE_SOCKETPAIR && defined (AF_UNIX) && SUPPORTS_LARGE_AF_UNIX_DGRAM
#define IPC_DGRAM IPC_UNIX_DGRAM
#else
#define IPC_DGRAM IPC_UDP_SOCKET
#endif

extern pid_t ipcCreate(int type, const char *prog, const char *const args[], const char *name,
    int sleep_after_fork, int *rfd, int *wfd, void **hIpc);
extern void ipcClose(pid_t pid, int rfd, int wfd);

#endif
