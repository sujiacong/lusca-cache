
/*
 * Derived from squid/src/tools.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dlink.h"

void
dlinkAdd(void *data, dlink_node * m, dlink_list * list)
{
    m->data = data;
    m->prev = NULL;
    m->next = list->head;
    if (list->head)
	list->head->prev = m;
    list->head = m;
    if (list->tail == NULL)
	list->tail = m;
}

void
dlinkAddTail(void *data, dlink_node * m, dlink_list * list)
{
    m->data = data;
    m->next = NULL;
    m->prev = list->tail;
    if (list->tail)
	list->tail->next = m;
    list->tail = m;
    if (list->head == NULL)
	list->head = m;
}

void
dlinkDelete(dlink_node * m, dlink_list * list)
{
    if (m->next)
	m->next->prev = m->prev;
    if (m->prev)
	m->prev->next = m->next;
    if (m == list->head)
	list->head = m->next;
    if (m == list->tail)
	list->tail = m->prev;
    m->next = m->prev = NULL;
}

void *
dlinkRemoveHead(dlink_list *list)
{
	dlink_node *n;

	if (list->head == NULL)
		return NULL;

	n = list->head;
	dlinkDelete(n, list);
	return n->data;
}
