#ifndef	__LIBMEM_MEMBUFS_H__
#define	__LIBMEM_MEMBUFS_H__

typedef enum {
    MEM_NONE,
    MEM_2K_BUF,
    MEM_4K_BUF,
    MEM_8K_BUF,
    MEM_16K_BUF,
    MEM_32K_BUF,
    MEM_64K_BUF,
    MEM_MAX
} mem_type;

typedef void FREE(void *);

extern MemMeter HugeBufCountMeter;
extern MemMeter HugeBufVolumeMeter;


extern void memBuffersInit(void);
extern void memBuffersClean(void);
extern void *memAllocate(mem_type);
extern void *memAllocBuf(size_t net_size, size_t * gross_size);
extern void *memReallocBuf(void *buf, size_t net_size, size_t * gross_size);
extern void memFree(void *, int type);
extern void memFree4K(void *);
extern void memFree8K(void *);
extern void memFreeBuf(size_t size, void *);
extern FREE *memFreeBufFunc(size_t size);
extern int memInUse(mem_type);
extern size_t memTotalAllocated(void);
extern void memDataInit(mem_type, const char *, size_t, int);
extern void memDataNonZero(mem_type);
extern void memCheckInit(void);

#endif
