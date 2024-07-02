#ifndef	__LIBSQSTORE_STORE_META_H__
#define	__LIBSQSTORE_STORE_META_H__

/*
 * NOTE!  We must preserve the order of this list!
 */
typedef enum {
    STORE_META_VOID,            /* should not come up */
    STORE_META_KEY_URL,         /* key w/ keytype */
    STORE_META_KEY_SHA,
    STORE_META_KEY_MD5,
    STORE_META_URL,             /* the url , if not in the header */
    STORE_META_STD,             /* standard metadata */
    STORE_META_HITMETERING,     /* reserved for hit metering */
    STORE_META_VALID,
    STORE_META_VARY_HEADERS,    /* Stores Vary request headers */
    STORE_META_STD_LFS,         /* standard metadata in lfs format */
    STORE_META_OBJSIZE,         /* object size, if its known */
    STORE_META_STOREURL,        /* the store url, if different to the normal URL */
    STORE_META_END
} store_meta_types;

/*
 * For now these aren't used in the application itself; they're
 * designed to be used by other bits of code which are manipulating
 * store swap entries.
 */

struct _storeMetaIndexOld {
	time_t timestamp;
	time_t lastref;
	time_t expires;
	time_t lastmod;
	size_t swap_file_sz;
	u_short refcount;
	u_short flags;
};
typedef struct _storeMetaIndexOld storeMetaIndexOld;

struct _storeMetaIndexNew {
	time_t timestamp;
	time_t lastref;
	time_t expires;
	time_t lastmod;
	squid_file_sz swap_file_sz;
	u_short refcount;
	u_short flags;
};
typedef struct _storeMetaIndexNew storeMetaIndexNew;

extern tlv * storeSwapMetaUnpack(const char *buf, int *hdr_len);
extern char * storeSwapMetaPack(tlv * tlv_list, int *length);

#endif
