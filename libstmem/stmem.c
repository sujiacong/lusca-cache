
/*
 * $Id: stmem.c 14467 2010-03-20 10:05:52Z adrian.chadd $
 *
 * DEBUG: section 19    Store Memory Primitives
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

/*!
 * @header storage memory routines.
 *
 *	The stmem routines provide a simple memory buffer system for in-flight
 *	data. It has also been (ab)used to store entire objects in memory,
 *	effectively a "memory cache", although this was not its primary design
 *	goal.
 *
 *	Data is appended via stmemAppend(), and accessed either via stmemRef()
 *	or stmemCopy(). stmemFreeDataUpTo() allows the beginning of the
 *	buffer to be freed, allowing the non-transferred part of large objects
 *	to be left in memory whilst the rest is free()'ed (and, presumably, either
 *	not needed anymore, or on disk.)
 */


#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../include/Array.h"
#include "../include/Stack.h"

#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/varargs.h"
#include "../libcore/tools.h"

#include "../libmem/MemPool.h"

#include "../libsqdebug/debug.h"

#include "stmem.h"

unsigned long store_mem_size = 0;

MemPool *pool_mem_node = NULL;

void
stmemNodeFree(void *buf)
{
    mem_node *p = (mem_node *) buf;
    if (!p->uses)
	memPoolFree(pool_mem_node, p);
    else
	p->uses--;
}


/*!
 * @function
 *	stmemNodeGet
 * @abstract
 *	Take a reference to an stmemNode data range, return the data.
 * @param	p	mem_node to return the data for
 * @return	the data page for the given mem_node
 *
 * @discussion
 *	The reference count on the given page is incremented before the page
 *	is returned.
 */
char *
stmemNodeGet(mem_node * p)
{
    p->uses++;
    return p->data;
}

/*!
 * @function
 *	stmemNodeFree
 * @abstract
 *	Free the stmem list.
 * @param	mem	stmem list to free
 *
 * @discussion
 *	This function calls stmemNodeFree() on all pages - any pages which
 *	are currently referenced will not be freed until their refcount
 *	reaches 0.
 */
void
stmemFree(mem_hdr * mem)
{
    mem_node *p;
    while ((p = mem->head)) {
	mem->head = p->next;
	store_mem_size -= SM_PAGE_SIZE;
	stmemNodeFree(p);
    }
    mem->head = mem->tail = NULL;
    mem->origin_offset = 0;
}

squid_off_t
stmemFreeDataUpto(mem_hdr * mem, squid_off_t target_offset)
{
    squid_off_t current_offset = mem->origin_offset;
    mem_node *p = mem->head;
    while (p && ((current_offset + p->len) <= target_offset)) {
	if (p == mem->tail) {
	    /* keep the last one to avoid change to other part of code */
	    mem->head = mem->tail;
	    mem->origin_offset = current_offset;
	    return current_offset;
	} else {
	    mem_node *lastp = p;
	    p = p->next;
	    current_offset += lastp->len;
	    store_mem_size -= SM_PAGE_SIZE;
	    stmemNodeFree(lastp);
	}
    }
    mem->head = p;
    mem->origin_offset = current_offset;
    if (current_offset < target_offset) {
	/* there are still some data left. */
	return current_offset;
    }
    assert(current_offset == target_offset);
    return current_offset;
}

/* Append incoming data. */
/*!
 * @function
 *	stmemAppend
 * @abstract
 *	Append some data to the stmem list.
 * @param	mem	stmem list to append to.
 * @param	data	buffer containing data to append.
 * @param	len	Amount of data to append.
 *
 * @discussion
 *	This routine appends data to the end of the list and can't insert
 *	data into a sparse list (which is good, as stmem doesn't really handle
 *	"sparse" type objects!) and will allocate/extend the stmem list as needed.
 *
 *	Even if the caller supplies the data in a SM_PAGE_SIZE buffer, the
 *	data is appended in SM_PAGE_SIZE chunks. Annoying, but its how it is.
 */
void
stmemAppend(mem_hdr * mem, const char *data, int len)
{
    mem_node *p;
    int avail_len;
    int len_to_copy;
    debugs(19, 6, "stmemAppend: len %d", len);
    /* Does the last block still contain empty space? 
     * If so, fill out the block before dropping into the
     * allocation loop */
    if (mem->head && mem->tail && (mem->tail->len < SM_PAGE_SIZE)) {
	avail_len = SM_PAGE_SIZE - (mem->tail->len);
	len_to_copy = XMIN(avail_len, len);
	xmemcpy((mem->tail->data + mem->tail->len), data, len_to_copy);
	/* Adjust the ptr and len according to what was deposited in the page */
	data += len_to_copy;
	len -= len_to_copy;
	mem->tail->len += len_to_copy;
    }
    while (len > 0) {
	len_to_copy = XMIN(len, SM_PAGE_SIZE);
	p = memPoolAlloc(pool_mem_node);	/* This is a non-zero'ed buffer; make sure you fully initialise it */
	p->next = NULL;
	p->len = len_to_copy;
	p->uses = 0;
	store_mem_size += SM_PAGE_SIZE;
	xmemcpy(p->data, data, len_to_copy);
	if (!mem->head) {
	    /* The chain is empty */
	    mem->head = mem->tail = p;
	} else {
	    /* Append it to existing chain */
	    mem->tail->next = p;
	    mem->tail = p;
	}
	len -= len_to_copy;
	data += len_to_copy;
    }
}

/*
 * Fetch a page from the store mem.
 *
 * The page data may not start from the same offset given here; the
 * mem_node reference returned will include a node pointer and offset
 * into the buffer so the client 'knows' where to start.
 *
 * The reference taking is a bit of a hack (and the free'ing is also
 * quite a bit of a hack!) but it'll have to suffice until a better
 * buffer management system replaces this stuff.
 *
 * Please note that the stmem code doesn't at all attempt to make
 * sure the memobject you're copying from actually has data where
 * you've asked for it. the store_client.c and store_swapout.c
 * routines all do magic to keep that in check.
 */
int
stmemRef(const mem_hdr * mem, squid_off_t offset, mem_node_ref * r)
{
    mem_node *p = mem->head;
    squid_off_t t_off = mem->origin_offset;

    debugs(19, 3, "stmemRef: offset %" PRINTF_OFF_T "; initial offset in memory %d", offset, (int) mem->origin_offset);
    if (p == NULL)
	return 0;
    /* Seek our way into store */
    while ((t_off + p->len) <= offset) {
	t_off += p->len;
	if (!p->next) {
	    debugs(19, 1, "stmemRef: p->next == NULL");
	    return 0;
	}
	assert(p->next);
	p = p->next;
    }
    /* XXX this should really be a "reference" function! [ahc] */
    r->node = p;
    p->uses++;

    r->offset = offset - t_off;
    assert(r->offset >= 0);
    assert(r->offset >= 0);
    assert(p->len + t_off - offset > 0);
    debugs(19, 3, "stmemRef: returning node %p, offset %d, %d bytes", p, (int) r->offset, (int) (p->len + t_off - offset));
    return p->len + t_off - offset;
}

/*!
 * @function
 *	stmemNodeRefCreate
 * @abstract
 *	Initialise the given mem_node_ref to a blank, private 4k mem_node.
 * @param	r	mem_node_ref to create an empty page for.
 *
 * @discussion
 *	The stmem node refcount is set to 0; the page is freed the minute this
 *	reference is cleared.
 */
void
stmemNodeRefCreate(mem_node_ref * r)
{
    assert(r->node == NULL);
    r->node = memPoolAlloc(pool_mem_node);
    r->node->uses = 0;
    r->node->next = NULL;
    r->node->len = SM_PAGE_SIZE;
    r->offset = 0;
}

/*!
 * @function
 *	stmemNodeRef
 * @abstract
 *	Create a copy of an existing memnode reference.
 * @param	r	mem_node_ref to create a opy of
 * @return	a new mem_node_ref referencing the same page, offset, length as r.
 */
mem_node_ref
stmemNodeRef(mem_node_ref * r)
{
    mem_node_ref r2;
    r2 = *r;
    r2.node->uses++;
    return r2;
}

/*!
 * @function
 *	stmemNodeUnref
 * @abstract
 *	Delete/clear the given reference; clear the stmem page if needed.
 * @param	r	mem_node_ref to dereference and clear.
 *
 * @discussion
 *	stmemNodeFree() is called on the passed in node; it may be freed.
 */
void
stmemNodeUnref(mem_node_ref * r)
{
    if (!r->node)
	return;
    stmemNodeFree((void *) r->node);
    r->node = NULL;
    r->offset = -1;
}

/*!
 * @function
 *	stmemCopy
 * @abstract
 *	Copy data from an stmem list into the given buffer.
 * @param	mem	stmem list to copy from.
 * @param	offset	offset in the list to begin copying from.
 * @param	buf	buffer to copy into.
 * @param	size	size of 'buf'.
 * @return	0 if no data can be copied; the number of bytes copied if possible.
 *
 * @discussion
 *	This begins searching from the beginning of the list; and thus is quite
 *	slow (O(n)) for large objects in memory. This obviously needs to be
 *	fixed somehow.
 */
ssize_t
stmemCopy(const mem_hdr * mem, squid_off_t offset, char *buf, size_t size)
{
    mem_node *p = mem->head;
    squid_off_t t_off = mem->origin_offset;
    size_t bytes_to_go = size;
    char *ptr_to_buf = NULL;
    int bytes_from_this_packet = 0;
    int bytes_into_this_packet = 0;
    debugs(19, 6, "stmemCopy: offset %" PRINTF_OFF_T ": size %d", offset, (int) size);
    if (p == NULL)
	return 0;
    assert(size > 0);
    /* Seek our way into store */
    while ((t_off + p->len) < offset) {
	t_off += p->len;
	if (!p->next) {
	    debugs(19, 1, "stmemCopy: p->next == NULL");
	    return 0;
	}
	assert(p->next);
	p = p->next;
    }
    /* Start copying begining with this block until
     * we're satiated */
    bytes_into_this_packet = offset - t_off;
    bytes_from_this_packet = XMIN(bytes_to_go, p->len - bytes_into_this_packet);
    xmemcpy(buf, p->data + bytes_into_this_packet, bytes_from_this_packet);
    bytes_to_go -= bytes_from_this_packet;
    ptr_to_buf = buf + bytes_from_this_packet;
    p = p->next;
    while (p && bytes_to_go > 0) {
	if (bytes_to_go > p->len) {
	    xmemcpy(ptr_to_buf, p->data, p->len);
	    ptr_to_buf += p->len;
	    bytes_to_go -= p->len;
	} else {
	    xmemcpy(ptr_to_buf, p->data, bytes_to_go);
	    bytes_to_go -= bytes_to_go;
	}
	p = p->next;
    }
    return size - bytes_to_go;
}

/*!
 * @function
 *	stmemInitMem
 * @abstract
 *	Setup the stmem system.
 */
void
stmemInitMem(void)
{
    pool_mem_node = memPoolCreate("mem_node", sizeof(mem_node));
    memPoolNonZero(pool_mem_node);
}
