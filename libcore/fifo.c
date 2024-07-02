#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "../include/util.h"

#include "fifo.h"

void
fifo_init(fifo_list *l)
{
	l->head = l->tail = NULL;
}

void
fifo_queue(fifo_list *l, void *p)
{
	fifo_node *n;

	n = xcalloc(1, sizeof(*n));
	n->p = p;
	if (l->head == NULL) {
		l->head = l->tail = n;
	} else {
		l->tail->next = n;
		l->tail = n;
	}
}

void *
fifo_dequeue(fifo_list *l)
{
	void *p;
	fifo_node *n;

	if (l->head == NULL)
		return NULL;

	p = l->head->p;
	n = l->head;
	l->head = l->head->next;
	free(n);
	if (l->head == NULL)
		l->tail = NULL;
	return p;
}
