/*!
 * @header Vector - vector data structure
 *
 * This is an implementation of a Vector style memory array of fixed-size,
 * arbitrary count structures.
 *
 * @copyright Adrian Chadd <adrian@creative.net.au>
 */
#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../include/util.h"
#include "Vector.h"

static int
vector_grow(vector_t *v, int new_count)
{
	void *t;

	if (new_count < v->alloc_count)
		return 0;

	t = xrealloc(v->data, (v->obj_size * new_count));
	if (t == NULL)
		return 0;

	v->data = t;
	v->alloc_count = new_count;
	return 1;
}

/*!
 * @function
 * 	vector_init
 * @abstract
 *	Setup a vector for use.
 * @discussion
 *
 * @param	v		pointer to allocated vector_t to initialise
 * @param	obj_size	size of each struct being stored
 * @param	obj_count	number of objects to initially allocate for
 */
void
vector_init(vector_t *v, int obj_size, int obj_count)
{
	v->obj_size = obj_size;
	v->alloc_count = 0;
	v->used_count = 0;
	v->data = NULL;
	(void) vector_grow(v, obj_count);
}

/*!
 * @function
 * 	vector_done
 * @abstract
 * 	Free memory associated with a vector_t
 * @discussion
 *
 * @param	v	pointer to vector_t to clean up
 */
void
vector_done(vector_t *v)
{
	v->used_count = 0;
	v->alloc_count = 0;
	if (v->data)
		xfree(v->data);
	v->data = NULL;
}

void *
vector_get_real(const vector_t *v, int offset)
{
	if (offset >= v->used_count)
		return NULL;
	if (v->data == NULL)
		return NULL;

	return ((char *) v->data + (v->obj_size * offset));
}

void *
vector_append(vector_t *v)
{
	int offset;
	if (v->used_count == v->alloc_count)
		(void) vector_grow(v, v->alloc_count + 16);

	if (v->used_count == v->alloc_count)
		return NULL;
	offset = v->used_count;
	v->used_count++;
	return ((char *) v->data + (v->obj_size * offset));
}

void *
vector_insert(vector_t *v, int offset)
{
	int position = offset;

	if (position >= v->alloc_count)
		(void) vector_grow(v, v->alloc_count + 1);

	/* If we're asked to insert past the end of the list, just turn this into an effective append */
	if (position > v->used_count)
		position = v->used_count;

	/* Relocate the rest */
	if (position < v->alloc_count)
		memmove((char *) v->data + (position + 1) * v->obj_size,
		    (char *) v->data + position * v->obj_size, (v->used_count - position) * v->obj_size);
	v->used_count++;
	return ((char *) v->data + (v->obj_size * position));
}

int
vector_copy_item(vector_t *v, int dst, int src)
{
	if (dst >= v->used_count)
		return -1;
	if (src >= v->used_count)
		return -1;
	memcpy( (char *) v->data + (dst) * v->obj_size, 
		(char *) v->data + (src) * v->obj_size, 
		v->obj_size );
	return 1;
}

/*!
 * @function
 *	vector_shrink
 * @abstract
 *	Shrink the given vector to the given size.
 * @discussion
 *	Any references to the currently-stored data that will be "deleted"
 *	after shrinking should be removed before calling vector_shrink().
 *
 *	The operation is ignored if a larger size than the currently allocated
 *	count is given.
 *
 * @param	v	pointer vector to shrink
 * @param	new_size	the new size; must be smaller or equal to the current size
 */
void
vector_shrink(vector_t *v, int new_size)
{
	/* XXX should we have a real debugging assert() in here? */
	if (new_size < v->used_count)
		v->used_count = new_size;
}
