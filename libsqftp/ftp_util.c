#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include <assert.h>

#include "../include/util.h"

static const char *Month[] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int
is_month(const char *buf)
{
    int i;
    for (i = 0; i < 12; i++)
        if (!strcasecmp(buf, Month[i]))
            return 1;
    return 0;
}

/* escapes any IAC (0xFF) characters. Returns a new string */
char *
escapeIAC(const char *buf)
{
    int n;
    char *ret;
    unsigned const char *p;
    unsigned char *r;
    for (p = (unsigned const char *) buf, n = 1; *p; n++, p++)
        if (*p == 255) 
            n++;
    ret = xmalloc(n);
    for (p = (unsigned const char *) buf, r = (unsigned char *) ret; *p; p++) {
        *r++ = *p;
        if (*p == 255)
            *r++ = 255;
    }
    *r++ = '\0';
    assert((r - (unsigned char *) ret) == n);
    return ret;
}

/* removes any telnet options. Same string returned */
char *
decodeTelnet(char *buf)
{
    char *p = buf;
    while ((p = strstr(p, "\377\377")) != NULL) {
        p++;
        memmove(p, p + 1, strlen(p + 1) + 1);
    }
    return buf;
}

