#ifndef	__STORE_LOG_COSS_H__
#define	__STORE_LOG_COSS_H__

extern void storeCossDirOpenSwapLog(SwapDir *sd);
extern void storeCossDirCloseSwapLog(SwapDir *sd);
extern void storeCossDirSwapLog(const SwapDir * sd, const StoreEntry * e, int op);


#endif
