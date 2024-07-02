#include "squid.h"

/*
 * Grow if needed + read into a buffer.
 * "read_size" is how much to read in in total
 * and if this isn't available in the buffer, it'll
 * grow to accomodate it.
 *
 * The "grow size" for now is fixed at 8k each time;
 * that logic needs to change later on to optimise
 * network throughput.
 *
 * This is somewhat hideously bad at the moment but is
 * being done so the two intended users of this code can
 * be migrated to use it instead of their own hand
 * crafted magic. This does mean that -too many reallocations-
 * will occur!
 *
 * The "read_size" is important because src/http.c actually
 * checks the read size versus read_size to determine whether
 * the socket buffer was drained or not (at that point in time.)
 * I don't agree with the logic as its not completely race-proof
 * _BUT_ I don't want to interfere (much) with it atm.
 */
int
buf_read(buf_t *b, int fd, int read_size)
{
	int ret;

	/* extend buffer to have enough space */
	debug(1, 3) ("buf_read: len %d, capacity %d, read size %d\n",
	    buf_len(b), buf_capacity(b), read_size);
	buf_grow_to_min_free(b, read_size);

	/* read into empty space */
	ret = FD_READ_METHOD(fd, buf_buf(b) + buf_len(b), buf_capacity(b) - buf_len(b));

	/* error/eof? return */
	if (ret <= 0)
		return ret;

	/* update counters */
	/* XXX do them! */

	/* update buf_t */
	b->len += ret;

	/* return size */
	return ret;
}

int
memBufFill(MemBuf *mb, int fd, int grow_size)
{
	int ret;

	/* We only grow the memBuf if its almost full */
	if (mb->capacity - mb->size < 1024)
		memBufGrow(mb, mb->capacity + grow_size);

	ret = FD_READ_METHOD(fd,  mb->buf + mb->size, mb->capacity - mb->size - 1);
	if (ret <= 0)
		return ret;
	mb->size += ret;
	assert(mb->size <= mb->capacity);
	mb->buf[mb->size] = '\0';
	return ret;
}
