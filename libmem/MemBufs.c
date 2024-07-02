#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include "../include/util.h"
#include "../include/Stack.h"
#include "../libcore/valgrind.h"
#include "../libcore/gb.h"
#include "../libcore/varargs.h" /* required for tools.h */
#include "../libcore/tools.h"
#include "../libcore/debug.h"

#include "MemPool.h"
#include "MemBufs.h"

/* module globals */

static MemPool *MemPools[MEM_MAX];

/* string pools */

MemMeter HugeBufCountMeter;
MemMeter HugeBufVolumeMeter;

/* local routines */

/*
 * public routines
 */

/*
 * we have a limit on _total_ amount of idle memory so we ignore
 * max_pages for now
 */
void
memDataInit(mem_type type, const char *name, size_t size, int max_pages_notused)
{
    assert(name && size);
    MemPools[type] = memPoolCreate(name, size);
}

void
memDataNonZero(mem_type type)
{
    memPoolNonZero(MemPools[type]);
}


/* find appropriate pool and use it (pools always init buffer with 0s) */
void *
memAllocate(mem_type type)
{
    return memPoolAlloc(MemPools[type]);
}

/* give memory back to the pool */
void
memFree(void *p, int type)
{
    memPoolFree(MemPools[type], p);
}

/* Find the best fit MEM_X_BUF type */
static mem_type
memFindBufSizeType(size_t net_size, size_t * gross_size)
{
    mem_type type;
    size_t size;
    if (net_size <= 2 * 1024) {
	type = MEM_2K_BUF;
	size = 2 * 1024;
    } else if (net_size <= 4 * 1024) {
	type = MEM_4K_BUF;
	size = 4 * 1024;
    } else if (net_size <= 8 * 1024) {
	type = MEM_8K_BUF;
	size = 8 * 1024;
    } else if (net_size <= 16 * 1024) {
	type = MEM_16K_BUF;
	size = 16 * 1024;
    } else if (net_size <= 32 * 1024) {
	type = MEM_32K_BUF;
	size = 32 * 1024;
    } else if (net_size <= 64 * 1024) {
	type = MEM_64K_BUF;
	size = 64 * 1024;
    } else {
	type = MEM_NONE;
	size = net_size;
    }
    if (gross_size)
	*gross_size = size;
    return type;
}

/* allocate a variable size buffer using best-fit pool */
void *
memAllocBuf(size_t net_size, size_t * gross_size)
{
    mem_type type = memFindBufSizeType(net_size, gross_size);
    if (type != MEM_NONE)
	return memAllocate(type);
    else {
	memMeterInc(HugeBufCountMeter);
	memMeterAdd(HugeBufVolumeMeter, *gross_size);
	return xcalloc(1, net_size);
    }
}

/* resize a variable sized buffer using best-fit pool */
void *
memReallocBuf(void *oldbuf, size_t net_size, size_t * gross_size)
{
    /* XXX This can be optimized on very large buffers to use realloc() */
    size_t new_gross_size;
    void *newbuf = memAllocBuf(net_size, &new_gross_size);
    if (oldbuf) {
	int data_size = *gross_size;
	if (data_size > net_size)
	    data_size = net_size;
	memcpy(newbuf, oldbuf, data_size);
	memFreeBuf(*gross_size, oldbuf);
    }
    *gross_size = new_gross_size;
    return newbuf;
}

/* free buffer allocated with memAllocBuf() */
void
memFreeBuf(size_t size, void *buf)
{
    mem_type type = memFindBufSizeType(size, NULL);
    if (type != MEM_NONE)
	memFree(buf, type);
    else {
	xfree(buf);
	memMeterDec(HugeBufCountMeter);
	memMeterDel(HugeBufVolumeMeter, size);
    }
}
void
memBuffersInit()
{
    memDataInit(MEM_2K_BUF, "2K Buffer", 2048, 10);
    memDataNonZero(MEM_2K_BUF);
    memDataInit(MEM_4K_BUF, "4K Buffer", 4096, 10);
    memDataNonZero(MEM_4K_BUF);
    memDataInit(MEM_8K_BUF, "8K Buffer", 8192, 10);
    memDataNonZero(MEM_8K_BUF);
    memDataInit(MEM_16K_BUF, "16K Buffer", 16384, 10);
    memDataNonZero(MEM_16K_BUF);
    memDataInit(MEM_32K_BUF, "32K Buffer", 32768, 10);
    memDataNonZero(MEM_32K_BUF);
    memDataInit(MEM_64K_BUF, "64K Buffer", 65536, 10);
    memDataNonZero(MEM_64K_BUF);
}


/*
 * Test that all entries are initialized
 */
void
memCheckInit(void)
{
    mem_type t;
    for (t = MEM_NONE, t++; t < MEM_MAX; t++) {
	/*
	 * If you hit this assertion, then you forgot to add a
	 * memDataInit() line for type 't'.
	 */
	assert(MemPools[t]);
    }
}

void
memBuffersClean(void)
{
}

int
memInUse(mem_type type)
{
    return memPoolInUseCount(MemPools[type]);
}

/* ick */

static void
memFree2K(void *p)
{
    memFree(p, MEM_2K_BUF);
}

void
memFree4K(void *p)
{
    memFree(p, MEM_4K_BUF);
}

void
memFree8K(void *p)
{
    memFree(p, MEM_8K_BUF);
}

static void
memFree16K(void *p)
{
    memFree(p, MEM_16K_BUF);
}

static void
memFree32K(void *p)
{
    memFree(p, MEM_32K_BUF);
}

static void
memFree64K(void *p)
{
    memFree(p, MEM_64K_BUF);
}

FREE *
memFreeBufFunc(size_t size)
{
    switch (size) {
    case 2 * 1024:
	return memFree2K;
    case 4 * 1024:
	return memFree4K;
    case 8 * 1024:
	return memFree8K;
    case 16 * 1024:
	return memFree16K;
    case 32 * 1024:
	return memFree32K;
    case 64 * 1024:
	return memFree64K;
    default:
	memMeterDec(HugeBufCountMeter);
	memMeterDel(HugeBufVolumeMeter, size);
	return xfree;
    }
}
