#ifndef	__STORE_REBUILD_COSS_H__
#define	__STORE_REBUILD_COSS_H__

typedef struct _RebuildState RebuildState;
struct _RebuildState {
    SwapDir *sd;
    int n_read;
    FILE *log;
    struct {
        unsigned int clean:1;
    } flags;
    struct _store_rebuild_data counts;
    struct {
	sfileno swap_filen;
	time_t timestamp;
    } recent;
    struct {
        int new;
        int reloc;
        int fresher;
        int unknown;
    } cosscounts;
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

extern void storeCossDirRebuild(SwapDir * sd);

#endif
