#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/util.h"
#include "../include/Stack.h"
#include "../libcore/varargs.h" /* required for tools.h */
#include "../libcore/tools.h"
#include "../libcore/gb.h"
#include "../libmem/MemPool.h"

#include "intlist.h"

static MemPool * pool_intlist = NULL;

/*!
 * @function
 *	intlistCheckAlloc
 * @abstract
 *	Check and initialise the memory pool if needed
 */
static void
intlistCheckAlloc(void)
{
	if (! pool_intlist)
		pool_intlist = memPoolCreate("intlist", sizeof(intlist));
}

/*!
 * @function
 *	intlistDestroy
 * @abstract
 *	Free the given intlist and its entries
 * @param	list		pointer to intlist to free
 *
 * @discussion
 *	"list" is a pointer to the intlist; it is set to NULL once the list
 *	is freed.
 */
void
intlistDestroy(intlist ** list)
{
    intlist *w = NULL;
    intlist *n = NULL;
    intlistCheckAlloc();
    for (w = *list; w; w = n) {
        n = w->next;
        memPoolFree(pool_intlist, w);
    }
    *list = NULL;
}

/*!
 * @function
 *	intlistFind
 * @abstract
 *	Find the first instance of the given value in the intlist
 * @param	list	intlist to search
 * @param	i	value to find
 * @return	1 whether the value was found, 0 otherwise
 */
int
intlistFind(intlist * list, int i)
{
    intlist *w = NULL;
    for (w = list; w; w = w->next)
        if (w->i == i)
            return 1;
    return 0;
}

/*!
 * @function
 *	intlistAddTail
 * @abstract
 *	Append an integer value to the given intlist
 * @param	list	the intlist to append to, or NULL if there is no list
 * @param	i	the integer value to add
 * @return	the list itself
 *
 * @discussion
 *	The intlist is a single linked list with only a head pointer; thus
 *	the append operation is O(n).
 */
intlist *
intlistAddTail(intlist * list, int i)
{
    intlist *t, *n;

    for (t = list; t && t->next; t = t->next)
        ;

    intlistCheckAlloc();
    n = memPoolAlloc(pool_intlist);
    n->i = i;
    n->next = NULL;
    if (t)
      t->next = n;
    return n;
}
