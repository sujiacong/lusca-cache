#ifndef	__LIBIAPP__PCONN_HIST_H__
#define	__LIBIAPP__PCONN_HIST_H__

#define PCONN_HIST_SZ (1<<16)

extern int client_pconn_hist[PCONN_HIST_SZ];
extern int server_pconn_hist[PCONN_HIST_SZ];

extern void pconnHistCount(int what, int i);


#endif
