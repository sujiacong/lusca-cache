#ifndef	__STORE_H__
#define	__STORE_H__

#define REBUILD_TIMESTAMP_DELTA_MAX 2
#define STORE_IN_MEM_BUCKETS            (229)

typedef struct lock_ctrl_t {
    SIH *callback;
    void *callback_data;
    StoreEntry *e;
} lock_ctrl_t;

#endif
