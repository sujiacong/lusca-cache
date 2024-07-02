#ifndef	__LIBMEM_MEMPOOL_H__
#define	__LIBMEM_MEMPOOL_H__

/* object to track per-action memory usage (e.g. #idle objects) */
struct _MemMeter {
    ssize_t level;              /* current level (count or volume) */
    ssize_t hwater_level;       /* high water mark */
    time_t hwater_stamp;        /* timestamp of last high water mark change */
};
typedef struct _MemMeter MemMeter;
    
/* object to track per-pool memory usage (alloc = inuse+idle) */
struct _MemPoolMeter {
    MemMeter alloc;
    MemMeter inuse;
    gb_t saved;
    gb_t total;
};
typedef struct _MemPoolMeter MemPoolMeter;

extern void memMeterSyncHWater(MemMeter * m);
#define memMeterCheckHWater(m) { if ((m).hwater_level < (m).level) memMeterSyncHWater(&(m)); }
#define memMeterInc(m) { (m).level++; memMeterCheckHWater(m); }
#define memMeterDec(m) { (m).level--; }
#define memMeterAdd(m, sz) { (m).level += (sz); memMeterCheckHWater(m); }
#define memMeterDel(m, sz) { (m).level -= (sz); }

/* MemPool related stuff */

/* a pool is a [growing] space for objects of the same size */
struct _MemPool {
    const char *label;
    size_t obj_size;
#if DEBUG_MEMPOOL
    size_t real_obj_size;       /* with alignment */
#endif
    struct {
        int dozero:1;
    } flags;
    MemPoolMeter meter;
#if DEBUG_MEMPOOL
    MemPoolMeter diff_meter;
#endif
};
typedef struct _MemPool MemPool;

extern void memConfigure(int enable, size_t limit, int dozero);
extern MemPool *memPoolCreate(const char *label, size_t obj_size);
extern void memPoolDestroy(MemPool * pool);
extern void memPoolNonZero(MemPool * p);
extern void *memPoolAlloc(MemPool * pool);
extern void memPoolFree(MemPool * pool, void *obj);
extern int memPoolWasUsed(const MemPool * pool);
extern int memPoolInUseCount(const MemPool * pool);
extern size_t memPoolInUseSize(const MemPool * pool);
extern int memPoolUsedCount(const MemPool * pool);
extern void memPoolInit(void);
extern void memPoolClean(void);

typedef struct {
	int alloc_calls;
	int free_calls;
} MemPoolStatInfo;

extern MemPoolMeter TheMeter;
extern gb_t mem_traffic_volume;
extern Stack Pools;
extern MemPoolStatInfo MemPoolStats;
extern size_t mem_idle_limit;


#endif
