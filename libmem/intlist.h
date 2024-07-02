#ifndef	__LIBMEM_INTLIST_H__
#define	__LIBMEM_INTLIST_H__

struct _intlist;
typedef struct _intlist intlist;
struct _intlist {
    int i;
    intlist *next;
};

extern void intlistDestroy(intlist **);
extern int intlistFind(intlist * list, int i);
extern intlist * intlistAddTail(intlist *list, int i);

#endif

