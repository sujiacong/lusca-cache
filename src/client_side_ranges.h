#ifndef	__CLIENT_SIDE_RANGES_H__
#define	__CLIENT_SIDE_RANGES_H__

extern void clientPackTermBound(String boundary, MemBuf * mb);
extern void clientPackRangeHdr(const HttpReply * rep, const HttpHdrRangeSpec * spec, String boundary, MemBuf * mb);
extern void clientPackRange(clientHttpRequest * http, HttpHdrRangeIter * i, const char **buf, size_t * size,
    MemBuf * mb);
extern int clientCanPackMoreRanges(const clientHttpRequest * http, HttpHdrRangeIter * i, size_t size);
extern int clientPackMoreRanges(clientHttpRequest * http, const char *buf, size_t size, MemBuf * mb);
extern void clientBuildRangeHeader(clientHttpRequest * http, HttpReply * rep);
extern int clientCheckRangeForceMiss(StoreEntry * entry, HttpHdrRange * range);

#endif
