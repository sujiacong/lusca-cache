
/*
 * $Id: disk.c 12731 2008-05-06 15:15:30Z adrian.chadd $
 *
 * DEBUG: section 6     Disk I/O Routines
 * AUTHOR: Harvest Derived
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

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/util.h"
#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "../libcb/cbdata.h"

#include "../libsqinet/sqinet.h"

/*
 * XXX this stuff should be properly decoupled from the comm code; but
 * XXX unfortunately right now a whole lot of stuff still lives
 * XXX in the comm routines rather than the disk routines.
 */
#include "iapp_ssl.h"
#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"
#include "disk.h"

static PF diskHandleRead;
static PF diskHandleWrite;

static MemPool * pool_dread_ctrl;
static MemPool * pool_dwrite_q;

struct _fde_disk *fde_disk = NULL;

#if defined(_SQUID_WIN32_) || defined(_SQUID_OS2_)
static int
diskWriteIsComplete(int fd)
{
    return fde_disk[fd].write_q ? 0 : 1;
}
#endif

void
disk_init_mem(void)
{
    pool_dread_ctrl = memPoolCreate("dread_ctrl", sizeof(dread_ctrl));
    pool_dwrite_q = memPoolCreate("dwrite_q", sizeof(dwrite_q));
}

void
disk_init(void)
{
	fde_disk = xcalloc(Squid_MaxFD, sizeof(struct _fde_disk));
}

/*!
 * @function
 *	file_open
 *
 * @abstract
 *	Open the given file for file-based IO.
 *
 * @discussion
 *	These calls were initially non-blocking but at some point in the
 *	past they were converted to "blocking" only with no completion
 *	callback available.
 *
 *	Any file opened in this manner will be closed on a fork/exec pair.
 *
 * @param	path		path to file toopen
 * @param	mode		O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, etc
 * @return	-1 on error, file descriptor of new file on success.
 */
int
file_open(const char *path, int mode)
{
    int fd;
    if (FILE_MODE(mode) == O_WRONLY)
	mode |= O_APPEND;
    errno = 0;
    fd = open(path, mode, 0644);
    CommStats.syscalls.disk.opens++;
    if (fd < 0) {
	debug(50, 3) ("file_open: error opening file %s: %s\n", path,
	    xstrerror());
	fd = DISK_ERROR;
    } else {
	debug(6, 5) ("file_open: FD %d\n", fd);
	commSetCloseOnExec(fd);
	fd_open(fd, FD_FILE, path);
    }
    return fd;
}


/* close a disk file. */
/*!
 * @function
 *	file_close
 *
 * @abstract
 *	Flush queued data and close the file filedescriptor
 *
 * @discussion
 *	If there is a comm read handler registered on this file
 *	descriptor, the callback is called with FD set to -1.
 *
 *	TODO: Does any code check the FD == -1 and treat errors?
 *	    It should be investigated, documented, and resolved..
 *
 * @param	fd	Filedescriptor to close; must be open.
 */
void
file_close(int fd)
{
    fde *F = &fd_table[fd];
    struct _fde_disk *fdd = &fde_disk[fd];
    PF *read_callback;
    assert(fd >= 0);
    assert(F->flags.open);
    if ((read_callback = F->read_handler)) {
	F->read_handler = NULL;
	read_callback(-1, F->read_data);
    }
    if (fdd->flags.write_daemon) {
#if defined(_SQUID_WIN32_) || defined(_SQUID_OS2_)
	/*
	 * on some operating systems, you can not delete or rename
	 * open files, so we won't allow delayed close.
	 */
	while (!diskWriteIsComplete(fd))
	    diskHandleWrite(fd, NULL);
#else
	F->flags.close_request = 1;
	debug(6, 2) ("file_close: FD %d, delaying close\n", fd);
	return;
#endif
    }
    /*
     * Assert there is no write callback.  Otherwise we might be
     * leaking write state data by closing the descriptor
     */
    assert(F->write_handler == NULL);
    F->flags.closing = 1;
#if CALL_FSYNC_BEFORE_CLOSE
    fsync(fd);
#endif
    debug(6, F->flags.close_request ? 2 : 5)
	("file_close: FD %d, really closing\n", fd);
    fd_close(fd);
    close(fd);
    CommStats.syscalls.disk.closes++;
}

/*
 * This function has the purpose of combining multiple writes.  This is
 * to facilitate the ASYNC_IO option since it can only guarantee 1
 * write to a file per trip around the comm.c select() loop. That's bad
 * because more than 1 write can be made to the access.log file per
 * trip, and so this code is purely designed to help batch multiple
 * sequential writes to the access.log file.  Squid will never issue
 * multiple writes for any other file type during 1 trip around the
 * select() loop.       --SLF
 */
static void
diskCombineWrites(struct _fde_disk *fdd)
{
    int len = 0;
    dwrite_q *q = NULL;
    dwrite_q *wq = NULL;
    /*
     * We need to combine multiple write requests on an FD's write
     * queue But only if we don't need to seek() in between them, ugh!
     * XXX This currently ignores any seeks (file_offset)
     */
    if (fdd->write_q != NULL && fdd->write_q->next != NULL) {
	len = 0;
	for (q = fdd->write_q; q != NULL; q = q->next)
	    len += q->len - q->buf_offset;
	wq = memPoolAlloc(pool_dwrite_q);
	wq->buf = xmalloc(len);
	wq->len = 0;
	wq->buf_offset = 0;
	wq->next = NULL;
	wq->free_func = xfree;
	do {
	    q = fdd->write_q;
	    len = q->len - q->buf_offset;
	    xmemcpy(wq->buf + wq->len, q->buf + q->buf_offset, len);
	    wq->len += len;
	    fdd->write_q = q->next;
	    if (q->free_func)
		(q->free_func) (q->buf);
	    if (q) {
		memPoolFree(pool_dwrite_q, q);
		q = NULL;
	    }
	} while (fdd->write_q != NULL);
	fdd->write_q_tail = wq;
	fdd->write_q = wq;
    }
}

/* write handler */
static void
diskHandleWrite(int fd, void *notused)
{
    int len = 0;
    fde *F = &fd_table[fd];
    struct _fde_disk *fdd = &fde_disk[fd];
    dwrite_q *q = fdd->write_q;
    int status = DISK_OK;
    int do_callback;
    int do_close;
    if (NULL == q)
	return;
    debug(6, 3) ("diskHandleWrite: FD %d\n", fd);
    fdd->flags.write_daemon = 0;
    assert(fdd->write_q != NULL);
    assert(fdd->write_q->len > fdd->write_q->buf_offset);
    debug(6, 3) ("diskHandleWrite: FD %d writing %d bytes\n",
	fd, (int) (fdd->write_q->len - fdd->write_q->buf_offset));
    errno = 0;
    if (fdd->write_q->file_offset != -1)
	lseek(fd, fdd->write_q->file_offset, SEEK_SET);
    len = FD_WRITE_METHOD(fd,
	fdd->write_q->buf + fdd->write_q->buf_offset,
	fdd->write_q->len - fdd->write_q->buf_offset);
    debug(6, 3) ("diskHandleWrite: FD %d len = %d\n", fd, len);
    CommStats.syscalls.disk.writes++;
    fd_bytes(fd, len, FD_WRITE);
    if (len < 0) {
	if (!ignoreErrno(errno)) {
	    status = errno == ENOSPC ? DISK_NO_SPACE_LEFT : DISK_ERROR;
	    debug(50, 1) ("diskHandleWrite: FD %d: disk write error: %s\n",
		fd, xstrerror());
	    /*
	     * If there is no write callback, then this file is
	     * most likely something important like a log file, or
	     * an interprocess pipe.  Its not a swapfile.  We feel
	     * that a write failure on a log file is rather important,
	     * and Squid doesn't otherwise deal with this condition.
	     * So to get the administrators attention, we exit with
	     * a fatal message.
	     */
	    /* XXX move fatal() outside of src/! -adrian */
	    if (fdd->wrt_handle == NULL) {
		debug(1, 1) ("write failure!\n");
		assert(1 == 0);
	    }
#if 0
		fatal("Write failure -- check your disk space and cache.log");
#endif
	    /*
	     * If there is a write failure, then we notify the
	     * upper layer via the callback, at the end of this
	     * function.  Meanwhile, flush all pending buffers
	     * here.  Let the upper layer decide how to handle the
	     * failure.  This will prevent experiencing multiple,
	     * repeated write failures for the same FD because of
	     * the queued data.
	     */
	    do {
		fdd->write_q = q->next;
		if (q->free_func)
		    (q->free_func) (q->buf);
		if (q) {
		    memPoolFree(pool_dwrite_q, q);
		    q = NULL;
		}
	    } while ((q = fdd->write_q));
	}
	len = 0;
    }
    if (q != NULL) {
	/* q might become NULL from write failure above */
	q->buf_offset += len;
	if (q->buf_offset > q->len)
	    debug(50, 1) ("diskHandleWrite: q->buf_offset > q->len (%p,%d, %d, %d FD %d)\n",
		q, (int) q->buf_offset, (int) q->len, len, fd);
	assert(q->buf_offset <= q->len);
	if (q->buf_offset == q->len) {
	    /* complete write */
	    fdd->write_q = q->next;
	    if (q->free_func)
		(q->free_func) (q->buf);
	    if (q) {
		memPoolFree(pool_dwrite_q, q);
		q = NULL;
	    }
	}
    }
    if (fdd->write_q == NULL) {
	/* no more data */
	fdd->write_q_tail = NULL;
    } else {
	/* another block is queued */
	diskCombineWrites(fdd);
	cbdataLock(fdd->wrt_handle_data);
	commSetSelect(fd, COMM_SELECT_WRITE, diskHandleWrite, NULL, 0);
	fdd->flags.write_daemon = 1;
    }
    do_close = F->flags.close_request;
    if (fdd->wrt_handle) {
	if (fdd->wrt_handle_data == NULL)
	    do_callback = 1;
	else if (cbdataValid(fdd->wrt_handle_data))
	    do_callback = 1;
	else
	    do_callback = 0;
	if (fdd->wrt_handle_data != NULL)
	    cbdataUnlock(fdd->wrt_handle_data);
	if (do_callback) {
	    fdd->wrt_handle(fd, status, len, fdd->wrt_handle_data);
	    /*
	     * NOTE, this callback can close the FD, so we must
	     * not touch 'F', 'fdd', etc. after this.
	     */
	    return;
	}
    }
    if (do_close)
	file_close(fd);
}


/* write block to a file */
/* write back queue. Only one writer at a time. */
/* call a handle when writing is complete. */

/*!
 * @function
 *	file_write
 * @abstract
 *	Queue a block to write to the given file descriptor and attempt
 *	to write as required.
 *
 * @discussion
 *	The original comment said "only one writer at a time". This isn't
 *	strictly true anymore. There's code above to coalesce writes
 *	into a single buffer before writing them out (rather than using
 *	writev()...) specifically to support writing out logfile entries.
 *
 *	Like file_read(), this function only locks the callback data
 *	and not the write buffer. This puts the requirement on the caller
 *	to allocate a temporary write buffer, copy data into it and pass
 *	it in with free_func set. If the callback data becomes invalid
 *	here, file_write() will simply call free_func(ptr_to_buf) and no harm
 *	is done. If one wishes to avoid the copy, then the referenced buffer
 *	is most likely going to be free()'ed when its owner is free()d (ie,
 *	by whatever calls cdataFree() on the callback_data object) and so
 *	garbage may be written out by the time this call is scheduled.
 *
 *	Again, like file_read(), this code is synchronous and so the above
 *	race condition won't happen. It does mean that it can't be naively
 *	converted into an asynchronous API because said race condition may
 *	occur.
 *
 *	The "correct" method for resolving this is most likely to change
 *	the calling semantics to assert the cbdata check and force the caller
 *	to stay around until the call is completed or cancelled.
 *
 *	The "most likely" method for resolving this given the Squid codebase
 *	is to turn the buffer+len+freefunc into a buffer container object
 *	so it can be refcounted and manipulated as required, separately from
 *	the callback data.
 *
 * @param	fd		file descriptor to write to
 * @param	file_offset	file offset to write at (-1 means "append"?)
 * @param	ptr_to_buf	buffer to write out
 * @param	len		length of buffer to write out
 * @param	handle		completion callback
 * @param	handle_data	completion callback handler
 * @param	free_func	If not NULL, function to free ptr_to_buf
 */
void
file_write(int fd,
    off_t file_offset,
    void *ptr_to_buf,
    size_t len,
    DWCB * handle,
    void *handle_data,
    FREE * free_func)
{
    dwrite_q *wq = NULL;
    fde *F = &fd_table[fd];
    struct _fde_disk *fdd = &fde_disk[fd];
    assert(fd >= 0);
    assert(F->flags.open);
    /* if we got here. Caller is eligible to write. */
    wq = memPoolAlloc(pool_dwrite_q);
    wq->file_offset = file_offset;
    wq->buf = ptr_to_buf;
    wq->len = len;
    wq->buf_offset = 0;
    wq->next = NULL;
    wq->free_func = free_func;
    fdd->wrt_handle = handle;
    fdd->wrt_handle_data = handle_data;
    /* add to queue */
    if (fdd->write_q == NULL) {
	/* empty queue */
	fdd->write_q = fdd->write_q_tail = wq;
    } else {
	fdd->write_q_tail->next = wq;
	fdd->write_q_tail = wq;
    }
    if (!fdd->flags.write_daemon) {
	cbdataLock(fdd->wrt_handle_data);
	diskHandleWrite(fd, NULL);
    }
}

/*
 * a wrapper around file_write to allow for MemBuf to be file_written
 * in a snap
 */
void
file_write_mbuf(int fd, off_t file_offset, MemBuf mb, DWCB * handler, void *handler_data)
{
    file_write(fd, file_offset, mb.buf, mb.size, handler, handler_data, memBufFreeFunc(&mb));
}

/* Read from FD */
static void
diskHandleRead(int fd, void *data)
{
    dread_ctrl *ctrl_dat = data;
    struct _fde_disk *fdd = &fde_disk[fd];
    int len;
    int rc = DISK_OK;
    /*
     * FD < 0 indicates premature close; we just have to free
     * the state data.
     */
    if (fd < 0) {
	memPoolFree(pool_dread_ctrl, ctrl_dat);
	return;
    }
    if (fdd->offset != ctrl_dat->file_offset) {
	debug(6, 3) ("diskHandleRead: FD %d seeking to offset %d\n",
	    fd, (int) ctrl_dat->file_offset);
	lseek(fd, ctrl_dat->file_offset, SEEK_SET);	/* XXX ignore return? */
	CommStats.syscalls.disk.seeks++;
	fdd->offset = ctrl_dat->file_offset;
    }
    errno = 0;
    len = FD_READ_METHOD(fd, ctrl_dat->buf, ctrl_dat->req_len);
    if (len > 0)
	fdd->offset += len;
    CommStats.syscalls.disk.reads++;
    fd_bytes(fd, len, FD_READ);
    if (len < 0) {
	if (ignoreErrno(errno)) {
	    commSetSelect(fd, COMM_SELECT_READ, diskHandleRead, ctrl_dat, 0);
	    return;
	}
	debug(50, 1) ("diskHandleRead: FD %d: %s\n", fd, xstrerror());
	len = 0;
	rc = DISK_ERROR;
    } else if (len == 0) {
	rc = DISK_EOF;
    }
    if (cbdataValid(ctrl_dat->client_data))
	ctrl_dat->handler(fd, ctrl_dat->buf, len, rc, ctrl_dat->client_data);
    cbdataUnlock(ctrl_dat->client_data);
    memPoolFree(pool_dread_ctrl, ctrl_dat);
}

/*!
 * @function
 *	file_read
 * @abstract
 *	Begin a file read operation into {buf,req_len}. Call {handler,client_data} when complete.
 * @discussion
 *	There is no locking performed on the supplied buffer/req_len; only the client_data buffer
 *	is actually locked. This means that if the caller cbdata becomes invalid for some reason
 *	(ie, is cbdataFree()'ed, but the references haven't yet gone away) then the underlying
 *	read buffer may have also been prematurely free()ed as well.
 *
 *	This needs to be kept in mind when using this API. It doesn't matter because of the
 *	way the underlying code is written (ie, the read() is done straight away after the
 *	read scheduling is done) so there is no chance of the underlying buffer being free()d.
 *	This also unfortunately means this code, as it stands, can't be made "pluggable" with
 *	an async disk IO implementation as said race conditions may occur.
 *
 * @param	fd		file descriptor to read from
 * @param	buf		buffer to read into
 * @param	req_len		size of buffer; maximum size to read
 * @param	file_offset	-1 for "use system filepos; >-1 to explicitly set filepos first
 * @param	handler		callback for completion
 * @param	client_data	callback data for completion
 */
void
file_read(int fd, char *buf, size_t req_len, off_t file_offset, DRCB * handler, void *client_data)
{
    dread_ctrl *ctrl_dat;
    assert(fd >= 0);
    ctrl_dat = memPoolAlloc(pool_dread_ctrl);
    ctrl_dat->fd = fd;
    ctrl_dat->file_offset = file_offset;
    ctrl_dat->req_len = req_len;
    ctrl_dat->buf = buf;
    ctrl_dat->end_of_file = 0;
    ctrl_dat->handler = handler;
    ctrl_dat->client_data = client_data;
    cbdataLock(client_data);
    diskHandleRead(fd, ctrl_dat);
}
