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

#include "wordlist.h"

static MemPool * pool_wordlist = NULL;

/*!
 * @function
 * 	wordlistInitMem
 * @abstract
 *	allocate resources required for the wordlist code.
 */
static inline void
wordlistInitMem(void)
{
	if (! pool_wordlist)
		pool_wordlist = memPoolCreate("wordlist", sizeof(wordlist));
}

/*
 * @function
 *	wordlistDestroy
 * @abstract
 *	Destroy and deallocate the given wordlist.
 * @param	wordlist		pointer to the pointer of the first wordlist item
 *
 * @abstract
 *	The wordlist is walked and freed; the wordlist pointer is then set to NULL.
 */
void
wordlistDestroy(wordlist ** list)
{   
    wordlist *w = NULL;
    wordlistInitMem();
    while ((w = *list) != NULL) {
        *list = w->next;
        safe_free(w->key);
        memPoolFree(pool_wordlist, w);
    }
    *list = NULL;
}


/*!
 * @function
 *	wordlistAddBuf
 * @abstract
 *	Add a (buf,len) string to the wordlist.
 * @param	list	wordlist to append the item to
 * @param	buf	pointer to the beginning of the string in memory
 * @param	len	length of the string
 * @return	the newly-allocated C string key
 *
 * @discussion
 *	The list pointer is not a pointer to the list, its a pointer to the
 *	first item in the list.
 *
 *	The wordlist is given a private copy of the key; the caller does not
 *	need to keep it around after this call.
 *
 *	The list is a single-linked list, so the performance of this append
 *	operator is unfortunately O(n).
 */
char *
wordlistAddBuf(wordlist ** list, const char *buf, int len)
{
    while (*list)
        list = &(*list)->next;
    wordlistInitMem();
    *list = memPoolAlloc(pool_wordlist);
    (*list)->key = xstrndup(buf, len);
    (*list)->next = NULL;
    return (*list)->key;
}

/*!
 * @function
 *	wordlistAdd
 * @abstract
 *	Add a C string to the wordlist.
 * @param	list	wordlist to append the item to
 * @param	key	the C string to add to the list
 * @return	the newly-allocated C string key
 *
 * @discussion
 *	The list pointer is not a pointer to the list, its a pointer to the
 *	first item in the list.
 *
 *	The wordlist is given a private copy of the key; the caller does not
 *	need to keep it around after this call.
 *
 *	The list is a single-linked list, so the performance of this append
 *	operator is unfortunately O(n).
 */
char *
wordlistAdd(wordlist ** list, const char *key)
{   
    while (*list)
        list = &(*list)->next;
    wordlistInitMem();
    *list = memPoolAlloc(pool_wordlist);
    (*list)->key = xstrdup(key);
    (*list)->next = NULL;
    return (*list)->key;
}


/*!
 * @function
 *	wordlistJoin
 * @abstract
 *	join together two wordlists
 * @param	list		target list
 * @param	wl		wordlist to join to 'list'
 *
 * @discussion
 *	'wl' (the second list) is joined to the first and then set to NULL;
 *	thus the list 'wl' is unavailable after this call.
 */
void
wordlistJoin(wordlist ** list, wordlist ** wl)
{   
    while (*list)
        list = &(*list)->next;
    *list = *wl;
    *wl = NULL;
}

void
wordlistAddWl(wordlist ** list, wordlist * wl)
{   
    wordlistInitMem();
    while (*list)
        list = &(*list)->next;
    for (; wl; wl = wl->next, list = &(*list)->next) {
        *list = memPoolAlloc(pool_wordlist);
        (*list)->key = xstrdup(wl->key);
        (*list)->next = NULL;
    }
}

#if 0
void
wordlistCat(const wordlist * w, MemBuf * mb)
{   
    while (NULL != w) {
        memBufPrintf(mb, "%s\n", w->key);
        w = w->next;
    }
}
#endif

/*!
 * @function
 *	wordlistDup
 * @abstract
 *	Duplicate a wordlist
 * @param	w	the wordlist to duplicate
 * @return	the duplicated list
 */
wordlist *
wordlistDup(const wordlist * w)
{   
    wordlist *D = NULL;
    while (NULL != w) {
        wordlistAdd(&D, w->key);
        w = w->next;
    }
    return D;
}

/*!
 * @function
 *	wordlistPopHead
 * @abstract
 *	Remove and return the top-most item on the word list
 * @param	head	pointer to the head item on the list
 * @return	the key value (which must be free()'ed once used), or NULL if no entry
 *		was available.
 *
 * @discussion
 *	The list itself is modified on POP - the top-most item is removed
 *	and the head pointer is updated to point to the new item head, or NULL
 *	if the list is now empty.
 */
char *
wordlistPopHead(wordlist **head)
{
	wordlist *e;
	char *k;

	if (*head == NULL)
		return NULL;
	
	k = (*head)->key;
	e = *head;
	*head = (*head)->next;
	wordlistInitMem();
	memPoolFree(pool_wordlist, e);
	return k;
}

