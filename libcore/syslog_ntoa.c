
#include "../include/config.h"

#if HAVE_SYSLOG

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

#include "syslog.h"

typedef struct {
    const char *name;
    int value;
} syslog_symbol_t;

int
syslog_ntoa(const char *s)
{
#define syslog_symbol(a) #a, a
    static syslog_symbol_t symbols[] =
    {
#ifdef LOG_AUTHPRIV
	{syslog_symbol(LOG_AUTHPRIV)},
#endif
#ifdef LOG_DAEMON
	{syslog_symbol(LOG_DAEMON)},
#endif
#ifdef LOG_LOCAL0
	{syslog_symbol(LOG_LOCAL0)},
#endif
#ifdef LOG_LOCAL1
	{syslog_symbol(LOG_LOCAL1)},
#endif
#ifdef LOG_LOCAL2
	{syslog_symbol(LOG_LOCAL2)},
#endif
#ifdef LOG_LOCAL3
	{syslog_symbol(LOG_LOCAL3)},
#endif
#ifdef LOG_LOCAL4
	{syslog_symbol(LOG_LOCAL4)},
#endif
#ifdef LOG_LOCAL5
	{syslog_symbol(LOG_LOCAL5)},
#endif
#ifdef LOG_LOCAL6
	{syslog_symbol(LOG_LOCAL6)},
#endif
#ifdef LOG_LOCAL7
	{syslog_symbol(LOG_LOCAL7)},
#endif
#ifdef LOG_USER
	{syslog_symbol(LOG_USER)},
#endif
#ifdef LOG_ERR
	{syslog_symbol(LOG_ERR)},
#endif
#ifdef LOG_WARNING
	{syslog_symbol(LOG_WARNING)},
#endif
#ifdef LOG_NOTICE
	{syslog_symbol(LOG_NOTICE)},
#endif
#ifdef LOG_INFO
	{syslog_symbol(LOG_INFO)},
#endif
#ifdef LOG_DEBUG
	{syslog_symbol(LOG_DEBUG)},
#endif
	{NULL, 0}
    };
    syslog_symbol_t *p;

    for (p = symbols; p->name != NULL; ++p)
	if (!strcmp(s, p->name) || !strcasecmp(s, p->name + 4))
	    return p->value;
    return 0;
}

#endif
