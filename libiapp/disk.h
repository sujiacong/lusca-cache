#ifndef	__LIBIAPP_DISK_H__
#define	__LIBIAPP_DISK_H__

#define DISK_OK                   (0)
#define DISK_ERROR               (-1)
#define DISK_EOF                 (-2)
#define DISK_NO_SPACE_LEFT       (-6)

/*
 * Macro to find file access mode
 */
#ifdef O_ACCMODE
#define FILE_MODE(x) ((x)&O_ACCMODE)
#else
#define FILE_MODE(x) ((x)&(O_RDONLY|O_WRONLY|O_RDWR))
#endif

typedef struct _dread_ctrl dread_ctrl;
typedef struct _dwrite_q dwrite_q;

/* disk.c / diskd.c callback typedefs */
typedef void DRCB(int, const char *buf, int size, int errflag, void *data);
                                                        /* Disk read CB */
typedef void DWCB(int, int, size_t, void *);    /* disk write CB */
typedef void DOCB(int, int errflag, void *data);        /* disk open CB */
typedef void DCCB(int, int errflag, void *data);        /* disk close CB */
typedef void DUCB(int errflag, void *data);     /* disk unlink CB */
typedef void DTCB(int errflag, void *data);     /* disk trunc CB */

struct _dread_ctrl {
    int fd;
    off_t file_offset;
    size_t req_len;
    char *buf;
    int end_of_file;
    DRCB *handler;
    void *client_data;
};
 
struct _dwrite_q {
    off_t file_offset;
    char *buf;
    size_t len;
    size_t buf_offset;
    dwrite_q *next;
    FREE *free_func;
};

struct _fde_disk {
	DWCB *wrt_handle;
	void *wrt_handle_data;
	dwrite_q *write_q;
	dwrite_q *write_q_tail;
	off_t offset;
	struct {
		int write_daemon:1;
	} flags;
};

extern int file_open(const char *path, int mode);
extern void file_close(int fd);
extern void file_write(int, off_t, void *, size_t len, DWCB *, void *, FREE *);
extern void file_write_mbuf(int fd, off_t, MemBuf mb, DWCB * handler, void *handler_data);
extern void file_read(int, char *, size_t, off_t, DRCB *, void *);
extern void disk_init(void);
extern void disk_init_mem(void); 

#endif
