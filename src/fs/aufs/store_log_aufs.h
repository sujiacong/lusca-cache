#ifndef	__STORE_LOG_AUFS_H__
#define	__STORE_LOG_AUFS_H__

extern char *storeAufsDirSwapLogFile(SwapDir *, const char *);

extern int storeAufsDirOpenTmpSwapLog(SwapDir *, int *, int *);
extern void storeAufsDirCloseTmpSwapLog(SwapDir * sd);

extern void storeAufsDirOpenSwapLog(SwapDir * sd);
extern void storeAufsDirCloseSwapLog(SwapDir * sd);

extern void storeAufsDirSwapLog(const SwapDir * sd, const StoreEntry * e, int op);

extern int storeAufsDirWriteCleanStart(SwapDir * sd);
extern const StoreEntry * storeAufsDirCleanLogNextEntry(SwapDir * sd);
extern void storeAufsDirWriteCleanDone(SwapDir * sd);


/* XXX not specifically meant to be here */
extern int storeAufsFilenoBelongsHere(int fn, int F0, int F1, int F2);
extern int storeAufsDirValidFileno(SwapDir *, sfileno, int);
extern STUNREFOBJ storeAufsDirUnrefObj;

#endif
