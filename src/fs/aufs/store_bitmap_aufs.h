#ifndef	__STORE_BITMAP_AUFS__
#define	__STORE_BITMAP_AUFS__

extern int storeAufsDirMapBitTest(SwapDir * SD, sfileno filn);
extern void storeAufsDirMapBitSet(SwapDir * SD, sfileno filn);
extern void storeAufsDirMapBitReset(SwapDir * SD, sfileno filn);
extern int storeAufsDirMapBitAllocate(SwapDir * SD);
extern void storeAufsDirInitBitmap(SwapDir * sd);

#endif
