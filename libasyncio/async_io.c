
/*
 * $Id: async_io.c 14573 2010-04-11 01:41:22Z adrian.chadd $
 *
 * DEBUG: section 32    Asynchronous Disk I/O
 * AUTHOR: Pete Bentley <pete@demon.net>
 * AUTHOR: Stewart Forster <slf@connect.com.au>
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#ifndef _REENTRANT
#error "_REENTRANT MUST be defined to build squid async io support."
#endif

#include "../include/config.h"

#ifndef _SQUID_WIN32_
#include        <pthread.h>
#endif
#include        <stdio.h>
#include        <sys/types.h>
#include        <sys/stat.h>
#ifndef _SQUID_WIN32_
#include        <sys/uio.h>
#endif
#include        <unistd.h>
#include        <fcntl.h>
#include        <errno.h>
#include        <dirent.h>
#include        <signal.h>
#if HAVE_SCHED_H
#include        <sched.h>
#endif
#include        <string.h>   

#include "../include/util.h"
#include "../include/Array.h"
#include "../include/Stack.h"
 
#include "../libcore/varargs.h"
#include "../libcore/tools.h"
#include "../libcore/gb.h"
#include "../libcore/kb.h"
#include "../libcore/dlink.h"

#include "../libsqdebug/debug.h"
 
#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libcb/cbdata.h"

#include "aiops.h"
#include "async_io.h"

typedef enum {
	_AIO_OPEN = 0,
	_AIO_READ = 1,
	_AIO_WRITE = 2,
	_AIO_CLOSE = 3,
	_AIO_UNLINK = 4,
	_AIO_TRUNCATE = 5,
	_AIO_OPENDIR = 6,
	_AIO_STAT = 7
} squidaio_op_t;

typedef struct squidaio_ctrl_t {
    struct squidaio_ctrl_t *next;
    int fd;
    squidaio_op_t operation;
    AIOCB *done_handler;
    void *done_handler_data;
    squidaio_result_t result;
    int len;
    char *bufp;
    FREE *free_func;
    dlink_node node;
} squidaio_ctrl_t;

struct squidaio_stat squidaio_counts;

typedef struct squidaio_unlinkq_t {
    char *path;
    struct squidaio_unlinkq_t *next;
} squidaio_unlinkq_t;

static dlink_list used_list;
static int initialised = 0;
static int usage_count = 0;
#if 0
static OBJH aioStats;
#endif
static MemPool *squidaio_ctrl_pool;

void
aioInit(void)
{
    usage_count++;
    if (initialised)
	return;
    squidaio_ctrl_pool = memPoolCreate("aio_ctrl", sizeof(squidaio_ctrl_t));
    initialised = 1;
}

void
aioDone(void)
{
    if (--usage_count > 0)
	return;
    squidaio_shutdown();
    memPoolDestroy(squidaio_ctrl_pool);
    initialised = 0;
}

/*!
 * @function
 *	aioOpen
 * @abstract
 *	schedule an async filedescriptor open
 * @discussion
 *	The open() will complete unless the operation is cancelled in time.
 *	If the callback_data is invalidated -before- the open completes or
 *	is cancelled then the open filedescriptor will (may?) leak.
 *	This should be kept in mind!
 *
 *	If FD_CLOEXEC is defined during compilation then an aioOpen()'ed
 *	filedescriptor will be marked as close-on-exec().
 *
 * @param
 * 	path		File to open
 *	mode		mode to pass to open()
 *	callback	callback to call on completion
 *	cbdata		callback data
 */
void
aioOpen(const char *path, int oflag, mode_t mode, AIOCB * callback, void *callback_data)
{
    squidaio_ctrl_t *ctrlp;

    assert(initialised);
    squidaio_counts.open++;
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = -2;
    ctrlp->done_handler = callback;
    ctrlp->done_handler_data = callback_data;
    ctrlp->operation = _AIO_OPEN;
    cbdataLock(callback_data);
    ctrlp->result.data = ctrlp;
    squidaio_open(path, oflag, mode, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
    return;
}

/*!
 * @function
 *	aioClose
 * @abstract
 *	Schedule an async close() of the filedescriptor
 * @discussion
 *	The close will happen some time in the future with no notification
 *	of completion.
 *
 * @param	fd	Filedescriptor to close
 */
void
aioClose(int fd)
{
    squidaio_ctrl_t *ctrlp;

    assert(initialised);
    squidaio_counts.close++;
    aioCancel(fd);
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = fd;
    ctrlp->done_handler = NULL;
    ctrlp->done_handler_data = NULL;
    ctrlp->operation = _AIO_CLOSE;
    ctrlp->result.data = ctrlp;
    squidaio_close(fd, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
    return;
}

/*!
 * @function
 *	aioCancel
 * @abstract
 *	Attempt to cancel all pending operations on the given filedescriptor
 * @discussion
 *	If there is one, the callback completion handler for a given queued
 *	request is called with an error / errno of -2.
 *
 *	TODO: Evaluate whether both requests and queued replies are cancelled!
 *
 *	TODO #2: it's possible that events may slip by and not be cancelled?
 *
 * @param	fd	File descriptor
 */
void
aioCancel(int fd)
{
    squidaio_ctrl_t *ctrlp;
    AIOCB *done_handler;
    void *their_data;
    dlink_node *m, *next;

    assert(initialised);
    squidaio_counts.cancel++;
    for (m = used_list.head; m; m = next) {
	next = m->next;
	ctrlp = m->data;
	if (ctrlp->fd != fd)
	    continue;

	squidaio_cancel(&ctrlp->result);

	if ((done_handler = ctrlp->done_handler)) {
	    their_data = ctrlp->done_handler_data;
	    ctrlp->done_handler = NULL;
	    ctrlp->done_handler_data = NULL;
	    debugs(32, 0, "this be aioCancel. Danger ahead!");
	    if (cbdataValid(their_data))
		done_handler(fd, their_data, NULL, -2, -2);
	    cbdataUnlock(their_data);
	    /* free data if requested to aioWrite() */
	    if (ctrlp->free_func)
		ctrlp->free_func(ctrlp->bufp);
	    /* free temporary read buffer */
	    if (ctrlp->operation == _AIO_READ)
		xfree(ctrlp->bufp);
	}
	dlinkDelete(m, &used_list);
	memPoolFree(squidaio_ctrl_pool, ctrlp);
    }
}

/*!
 * @function
 *	aioWrite
 * @abstract
 *	Schedule an async write to occur.
 *
 * @discussion
 *	The write is scheduled and will run regardless of whether the
 *	callback_data is valid or not. The validity of callback_data
 *	only effects whether the callback is made. This means that
 *	the buffer being written must stay valid for the lifespan of
 *	the async write.
 *
 *	The free_func is called once the operation is complete, regardless
 *	of whether the callback data is valid or not.
 *
 * @param	fd		Filedescriptor to write to
 * @param	offset		Offset to write to, or -1 to append at the end of the file
 * @param	bufp		Buffer to write from
 * @param	len		Length of bufp
 * @param	callback	Completion callback
 * @param	callback_data	Completion callback data
 * @param	free_func	If not NULL, this will be called to free the given buffer
 */
void
aioWrite(int fd, off_t offset, char *bufp, int len, AIOCB * callback, void *callback_data, FREE * free_func)
{
    squidaio_ctrl_t *ctrlp;

    assert(initialised);
    squidaio_counts.write++;
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = fd;
    ctrlp->done_handler = callback;
    ctrlp->done_handler_data = callback_data;
    ctrlp->operation = _AIO_WRITE;
    ctrlp->bufp = bufp;
    ctrlp->free_func = free_func;
    assert(offset >= 0);
    cbdataLock(callback_data);
    ctrlp->result.data = ctrlp;
    squidaio_write(fd, bufp, len, offset, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
}				/* aioWrite */


void
aioRead(int fd, off_t offset, int len, AIOCB * callback, void *callback_data)
{
    squidaio_ctrl_t *ctrlp;

    assert(initialised);
    squidaio_counts.read++;
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = fd;
    ctrlp->done_handler = callback;
    ctrlp->done_handler_data = callback_data;
    ctrlp->operation = _AIO_READ;
    ctrlp->len = len;
    ctrlp->bufp = xmalloc(len);
    assert(offset >= 0);
    cbdataLock(callback_data);
    ctrlp->result.data = ctrlp;
    squidaio_read(fd, ctrlp->bufp, len, offset, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
    return;
}				/* aioRead */

/*!
 * @function
 *	aioStat
 * @abstract
 *	Schedule a sync() call on the given path; write to the given
 *	stat.
 * @discussion
 *	sb is not written in to via the stat() syscall; instead it is
 *	copied in via squidaio_cleanup_request(), via squidaio_pool().
 *
 * 	TODO: check whether an invalid callback_data means stat() isn't
 *	    called, or whether it is still written into!
 *
 *	Currently, nothing in COSS/AUFS uses this call so it is possible
 *	the broken-ness is not being exposed..
 *
 * @param	path		File to stat()
 * @param	sb		stat struct to write result to
 * @param	callback	Completion callback
 * @param	callback_data	Completion callback data
 */
void
aioStat(char *path, struct stat *sb, AIOCB * callback, void *callback_data)
{
    squidaio_ctrl_t *ctrlp;

    assert(initialised);
    squidaio_counts.stat++;
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = -2;
    ctrlp->done_handler = callback;
    ctrlp->done_handler_data = callback_data;
    ctrlp->operation = _AIO_STAT;
    cbdataLock(callback_data);
    ctrlp->result.data = ctrlp;
    squidaio_stat(path, sb, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
    return;
}				/* aioStat */

/*!
 * @function
 *	aioUnlink
 * @abstract
 *	Schedule an unlink() on the given path.
 * @discussion
 *	The callback is called, if still valid, with the results of the unlink().
 *
 * @param	path		File to unlink()
 * @param	callback	Completion callback
 * @param	callback_data	Completion callback data
 */
void
aioUnlink(const char *path, AIOCB * callback, void *callback_data)
{
    squidaio_ctrl_t *ctrlp;
    assert(initialised);
    squidaio_counts.unlink++;
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = -2;
    ctrlp->done_handler = callback;
    ctrlp->done_handler_data = callback_data;
    ctrlp->operation = _AIO_UNLINK;
    cbdataLock(callback_data);
    ctrlp->result.data = ctrlp;
    squidaio_unlink(path, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
}				/* aioUnlink */

/*!
 * @function
 *	aioTruncate
 * @abstract
 *	Schedule a truncate() on the given path.
 * @discussion
 *	The callback is called, if still valid, with the results of the truncate().
 *
 * @param	path		File to unlink()
 * @param	callback	Completion callback
 * @param	callback_data	Completion callback data
 */
void
aioTruncate(const char *path, off_t length, AIOCB * callback, void *callback_data)
{
    squidaio_ctrl_t *ctrlp;
    assert(initialised);
    squidaio_counts.unlink++;
    ctrlp = memPoolAlloc(squidaio_ctrl_pool);
    ctrlp->fd = -2;
    ctrlp->done_handler = callback;
    ctrlp->done_handler_data = callback_data;
    ctrlp->operation = _AIO_TRUNCATE;
    cbdataLock(callback_data);
    ctrlp->result.data = ctrlp;
    squidaio_truncate(path, length, &ctrlp->result);
    dlinkAdd(ctrlp, &ctrlp->node, &used_list);
}				/* aioTruncate */

int
aioCheckCallbacks(void)
{
    squidaio_result_t *resultp;
    squidaio_ctrl_t *ctrlp;
    AIOCB *done_handler;
    void *their_data;
    int retval = 0;

    assert(initialised);
    squidaio_counts.check_callback++;
    for (;;) {
	if ((resultp = squidaio_poll_done()) == NULL)
	    break;
	ctrlp = (squidaio_ctrl_t *) resultp->data;
	if (ctrlp == NULL)
	    continue;		/* XXX Should not happen */
	dlinkDelete(&ctrlp->node, &used_list);
	if ((done_handler = ctrlp->done_handler)) {
	    their_data = ctrlp->done_handler_data;
	    ctrlp->done_handler = NULL;
	    ctrlp->done_handler_data = NULL;
	    if (cbdataValid(their_data)) {
		retval = 1;	/* Return that we've actually done some work */
		done_handler(ctrlp->fd, their_data, ctrlp->bufp,
		    ctrlp->result.aio_return, ctrlp->result.aio_errno);
	    } else {
		if (ctrlp->operation == _AIO_OPEN) {
		    /* The open operation was aborted.. */
		    int fd = ctrlp->result.aio_return;
		    if (fd >= 0)
			aioClose(fd);
		}
	    }
	    cbdataUnlock(their_data);
	}
	/* free data if requested to aioWrite() */
	if (ctrlp->free_func)
	    ctrlp->free_func(ctrlp->bufp);
	/* free temporary read buffer */
	if (ctrlp->operation == _AIO_READ)
	    xfree(ctrlp->bufp);
	memPoolFree(squidaio_ctrl_pool, ctrlp);
    }
    return retval;
}

/*!
 * @function
 *	aioSync
 * @abstract
 *	Sync all pending IO operations
 * @discussion
 *	This will sync -and- call all pending callbacks; which may be quite
 *	dangerous as it may cause re-entry into a module (eg, aufs calls
 *	aioSync() which calls aufs completion callbacks, which calls a variety
 *	of callbacks which schedule even further aufs IO operations..)
 */
void
aioSync(void)
{
    if (!initialised)
	return;			/* nothing to do then */
    /* Flush all pending operations */
    debugs(32, 1, "aioSync: flushing pending I/O operations");
    do {
	aioCheckCallbacks();
    } while (squidaio_sync());
    debugs(32, 1, "aioSync: done");
}

int
aioQueueSize(void)
{
    return memPoolInUseCount(squidaio_ctrl_pool);
}
