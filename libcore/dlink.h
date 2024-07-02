#ifndef	__LIBCORE_DLINK_H__
#define	__LIBCORE_DLINK_H__

/*
 * Derived from squid/src/typedefs.h ; squid/src/structs.h
 */

struct _dlink_node;
struct _dlink_tail;

typedef struct _dlink_node dlink_node;
typedef struct _dlink_list dlink_list;

struct _dlink_node {
    void *data;
    dlink_node *prev;
    dlink_node *next;
};

struct _dlink_list {
    dlink_node *head;
    dlink_node *tail;
};

#define DLINK_ISEMPTY(n)        ( (n).head == NULL )
#define DLINK_HEAD(n)           ( (n).head->data )
#define	DLINK_ISORPHAN(n)	( (n).next == NULL && (n).prev == NULL )

extern void dlinkAdd(void *data, dlink_node *, dlink_list *);
extern void dlinkAddTail(void *data, dlink_node *, dlink_list *);
extern void dlinkDelete(dlink_node * m, dlink_list * list);
extern void dlinkNodeDelete(dlink_node * m);
extern void * dlinkRemoveHead(dlink_list *list);

#endif
