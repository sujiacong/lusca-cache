#ifndef	__LIBCORE_FIFO_H__
#define	__LIBCORE_FIFO_H__

typedef struct _fifo_list fifo_list;
typedef struct _fifo_node fifo_node;

struct _fifo_node {
	fifo_node *next;
	void *p;
};

struct _fifo_list {
	fifo_node *head, *tail;
};

extern void fifo_init(fifo_list *l);
extern void fifo_queue(fifo_list *l, void *p);
extern void * fifo_dequeue(fifo_list *l);


#endif
