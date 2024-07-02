#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Origin: squid/src/tools.c */

#include "gb.h"

void
gb_flush(gb_t * g)
{
    g->gb += (g->bytes >> 30);
    g->bytes &= (1 << 30) - 1;
}

double
gb_to_double(const gb_t * g)
{
    return ((double) g->gb) * ((double) (1 << 30)) + ((double) g->bytes);
}

const char *
gb_to_str(const gb_t * g)
{
    /*
     * it is often convenient to call gb_to_str several times for _one_ printf
     */
#define max_cc_calls 5
    typedef char GbBuf[32];
    static GbBuf bufs[max_cc_calls];
    static int call_id = 0;
    double value = gb_to_double(g);
    char *buf = bufs[call_id++];
    if (call_id >= max_cc_calls)
        call_id = 0;
    /* select format */
    if (value < 1e9)
        snprintf(buf, sizeof(GbBuf), "%.2f MB", value / 1e6);
    else if (value < 1e12)
        snprintf(buf, sizeof(GbBuf), "%.2f GB", value / 1e9);
    else
        snprintf(buf, sizeof(GbBuf), "%.2f TB", value / 1e12);
    return buf;
}

