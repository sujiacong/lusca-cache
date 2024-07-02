#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "pconn_hist.h"

int client_pconn_hist[PCONN_HIST_SZ];
int server_pconn_hist[PCONN_HIST_SZ];


void
pconnHistCount(int what, int i)
{
    if (i >= PCONN_HIST_SZ)
        i = PCONN_HIST_SZ - 1;
    /* what == 0 for client, 1 for server */
    if (what == 0)
        client_pconn_hist[i]++;
    else if (what == 1)
        server_pconn_hist[i]++;
    else
        assert(0);
}

