#ifndef	__LIBSQSTORE_UFS_UTILS_H__
#define	__LIBSQSTORE_UFS_UTILS_H__

struct _ufsSwapLogEntryList {
	storeSwapLogData *buf;
	int count;
	int size;
};
typedef struct _ufsSwapLogEntryList ufs_swaplog_t;

/* Swaplog */
extern void ufs_swaplog_init(ufs_swaplog_t *el);
extern int ufs_swaplog_append(ufs_swaplog_t *el, StoreEntry *e, int op);
extern void ufs_swaplog_done(ufs_swaplog_t *el);
extern size_t ufs_swaplog_getarray_count(ufs_swaplog_t *el);
extern size_t ufs_swaplog_getarray_buf_totalsize(ufs_swaplog_t *el);
extern storeSwapLogData * ufs_swaplog_take_buffer(ufs_swaplog_t *el);

#endif
