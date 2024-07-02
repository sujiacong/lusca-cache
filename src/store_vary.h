#ifndef	__STORE_VARY_H__
#define	__STORE_VARY_H__

extern void storeLocateVaryDone(VaryData * data);
extern void storeLocateVary(StoreEntry * e, int offset, const char *vary_data,
    String accept_encoding, STLVCB * callback, void *cbdata);
extern void storeAddVary(const char *store_url, const char *url, method_t * method, const cache_key * key,
    const char *etag, const char *vary, const char *vary_headers,
    const char *accept_encoding);

#endif
