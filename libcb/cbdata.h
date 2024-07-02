#ifndef	__LIBCB_CBDATA_H__
#define	__LIBCB_CBDATA_H__

#if CBDATA_DEBUG
#include "../include/hash.h"
#endif

/*  
 * cbdata types. similar to the MEM_* types above, but managed
 * in cbdata.c. A big difference is that these types are dynamically
 * allocated. This list is only a list of predefined types. Other types
 * are added runtime
 */ 
typedef enum {
    CBDATA_UNKNOWN = 0,
    CBDATA_UNDEF = 0,
    CBDATA_FIRST = 1,
    CBDATA_FIRST_CUSTOM_TYPE = 1000
} cbdata_type;

struct cbdata_index_struct {
    MemPool *pool;
    FREE *free_func;
};

typedef struct _cbdata {
#if HASHED_CBDATA
    hash_link hash;
#endif
    int valid;
    int locks;
    int type;
#if CBDATA_DEBUG
    const char *file;
    int line;
#endif
    void *y;                    /* cookie used while debugging */
#if !HASHED_CBDATA
    union {
        void *pointer;
        double double_float;
        int integer;
    } data;
#endif
} cbdata;

#define CREATE_CBDATA(type) cbdataInitType(CBDATA_##type, #type, sizeof(type), NULL)
#define CREATE_CBDATA_FREE(type, free_func) cbdataInitType(CBDATA_##type, #type, sizeof(type), free_func)
#define CBDATA_COOKIE(p) ((void *)((unsigned long)(p) ^ 0xDEADBEEF))

#if CBDATA_DEBUG 
#define cbdataAlloc(type)        cbdataInternalAllocDbg(CBDATA_##type,__FILE__,__LINE__)
#define cbdataLock(a)           cbdataLockDbg(a,__FILE__,__LINE__)
#define cbdataUnlock(a)         cbdataUnlockDbg(a,__FILE__,__LINE__)
#else
#define cbdataAlloc(type) ((type *)cbdataInternalAlloc(CBDATA_##type))
extern void cbdataLock(const void *p); 
extern void cbdataUnlock(const void *p);
#endif

#define cbdataFree(var) (var = (var != NULL ? cbdataInternalFree(var): NULL))
#define CBDATA_TYPE(type)       static cbdata_type CBDATA_##type = 0
#define CBDATA_GLOBAL_TYPE(type)        cbdata_type CBDATA_##type
#define CBDATA_INIT_TYPE(type)  (CBDATA_##type ? 0 : (CBDATA_##type = cbdataAddType(CBDATA_##type, #type, sizeof(type), NULL)))
#define CBDATA_INIT_TYPE_FREECB(type, free_func)        (CBDATA_##type ? 0 : (CBDATA_##type = cbdataAddType(CBDATA_##type, #type, sizeof(type), free_func)))

extern struct cbdata_index_struct *cbdata_index;

extern void cbdataInit(void);
#if CBDATA_DEBUG
extern void *cbdataInternalAllocDbg(cbdata_type type, const char *, int);
extern void cbdataLockDbg(const void *p, const char *, int);
extern void cbdataUnlockDbg(const void *p, const char *, int);
#else
extern void *cbdataInternalAlloc(cbdata_type type);
#endif
/* Note: Allocations is done using the cbdataAlloc macro */
extern void *cbdataInternalFree(void *p);
extern int cbdataValid(const void *p);
extern void cbdataInitType(cbdata_type type, const char *label, int size, FREE * free_func);
extern cbdata_type cbdataAddType(cbdata_type type, const char *label, int size, FREE * free_func);
extern int cbdataLocked(const void *p);
extern int cbdataInUseCount(cbdata_type type);

extern int cbdataCount;
#if HASHED_CBDATA
extern hash_table *cbdata_htable;
#endif

/* Generic cbdata stuff */

/*
 * use this when you need to pass callback data to a blocking
 * operation, but you don't want to add that pointer to cbdata
 */
struct _generic_cbdata {
    void *data;
};
typedef struct _generic_cbdata generic_cbdata;

CBDATA_GLOBAL_TYPE(generic_cbdata);

#endif
