#include <stdio.h>

#include "../libstat/StatHist.h"
#include "globals.h"

int shutting_down = 0;
int reconfiguring = 0;
int opt_reuseaddr = 1;
int iapp_tcpRcvBufSz = 0;
int iapp_incomingRate;
const char * iapp_useAcceptFilter = NULL;
int NHttpSockets = 0;
int HttpSockets[MAXHTTPPORTS];
int theInIcpConnection = -1;
int theOutIcpConnection = -1;
StatHist select_fds_hist;
int need_linux_tproxy = 0;
