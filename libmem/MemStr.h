#ifndef	__LIBMEM_MEMSTR_H__
#define	__LIBMEM_MEMSTR_H__

#define	MEM_STR_POOL_COUNT	3

typedef struct {
	MemPool *pool;
} StrPoolsStruct;

extern MemMeter StrCountMeter;
extern MemMeter StrVolumeMeter;
extern StrPoolsStruct StrPools[];

extern	void * memAllocString(size_t net_size, size_t * gross_size);
extern	void memFreeString(size_t size, void *buf);
extern	void memStringInit(void);

#endif
