#ifndef	__LIBSQSTORE_STORE_FILE_UFS_H__
#define	__LIBSQSTORE_STORE_FILE_UFS_H__

struct _store_ufs_dir {
	char *path;
	char *swaplog_path;
	int l1;
	int l2;
};
typedef struct _store_ufs_dir store_ufs_dir_t;

extern void store_ufs_init(store_ufs_dir_t *sd, const char *path, int l1, int l2, const char *swaplog_path);
extern void store_ufs_done(store_ufs_dir_t *sd);

extern int store_ufs_createPath(store_ufs_dir_t *sd, int swap_filen, char *buf);
extern int store_ufs_createDir(store_ufs_dir_t *sd, int d1, int d2, char *buf);
extern int store_ufs_filenum_correct_dir(store_ufs_dir_t *sd, int fn, int F1, int F2);
extern int store_ufs_has_valid_rebuild_log(store_ufs_dir_t *sd);

static inline int store_ufs_l1(store_ufs_dir_t *sd) { return sd->l1; }
static inline int store_ufs_l2(store_ufs_dir_t *sd) { return sd->l2; }
static inline const char * store_ufs_path(store_ufs_dir_t *sd) { return sd->path; }


#endif
