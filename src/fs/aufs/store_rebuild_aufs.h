#ifndef	__STORE_REBUILD_AUFS_H__
#define	__STORE_REBUILD_AUFS_H__

typedef struct _RebuildState RebuildState;
struct _RebuildState {
    SwapDir *sd;
    int n_read;
    int log_fd;
    struct {
        unsigned int clean:1;
        unsigned int init:1;
    } flags;
    struct _store_rebuild_data counts;
    struct {
	int r_fd, w_fd;
	pid_t pid;
    } helper;
    struct {
	char *buf;
	int size;
	int used;
   } rbuf;
};

extern void storeAufsDirRebuild(SwapDir * sd);

#endif
