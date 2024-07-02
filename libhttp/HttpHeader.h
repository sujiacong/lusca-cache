#ifndef	__LIBHTTP_HTTPHEADER_H__

#define	__LIBHTTP_HTTPHEADER_H__

/* big mask for http headers */
typedef char HttpHeaderMask[(HDR_ENUM_END + 7) / 8];

/*iteration for headers; use HttpHeaderPos as opaque type, do not interpret */
typedef int HttpHeaderPos;

/* use this and only this to initialize HttpHeaderPos */
#define HttpHeaderInitPos (-1)

struct _HttpHeader {
    /* protected, do not use these, use interface functions instead */
    Array entries;              /* parsed entries in raw format */
    HttpHeaderMask mask;        /* bit set <=> entry present */
    http_hdr_owner_type owner;  /* request or reply */
    int len;                    /* length when packed, not counting terminating '\0' */
};
typedef struct _HttpHeader HttpHeader;

/* some fields can hold either time or etag specs (e.g. If-Range) */
struct _TimeOrTag {
    const char *tag;            /* entity tag */
    time_t time;
    int valid;                  /* true if struct is usable */
};  
typedef struct _TimeOrTag TimeOrTag;

extern HttpHeaderFieldInfo *Headers;
extern MemPool * pool_http_header_entry;

/* XXX as mentioned in HttpHeader.c ; these probably shouldn't be here? */
extern HttpHeaderMask ListHeadersMask;
extern HttpHeaderMask ReplyHeadersMask;
extern HttpHeaderMask RequestHeadersMask;

/* XXX as mentioned in HttpHeader.c ; these probably shouldn't be here either?! */
extern MemPool * pool_http_hdr_range_spec;
extern MemPool * pool_http_hdr_range;
extern MemPool * pool_http_hdr_cont_range;

extern void httpHeaderInitLibrary(void);

/* init/clean */
extern void httpHeaderInit(HttpHeader * hdr, http_hdr_owner_type owner);
extern void httpHeaderClean(HttpHeader * hdr);
extern int httpHeaderReset(HttpHeader * hdr);
extern void httpHeaderAddClone(HttpHeader * hdr, const HttpHeaderEntry * e);
extern void httpHeaderAddEntry(HttpHeader * hdr, HttpHeaderEntry * e);
extern void httpHeaderInsertEntry(HttpHeader * hdr, HttpHeaderEntry * e, int pos);
extern void httpHeaderAppend(HttpHeader * dest, const HttpHeader * src);
extern HttpHeaderEntry *httpHeaderGetEntry(const HttpHeader * hdr, HttpHeaderPos * pos);
extern HttpHeaderEntry *httpHeaderFindEntry(const HttpHeader * hdr, http_hdr_type id);
extern HttpHeaderEntry *httpHeaderFindLastEntry(const HttpHeader * hdr, http_hdr_type id);

extern void httpHeaderAddEntryStr(HttpHeader *hdr, http_hdr_type id, const char *attrib, const char *value);
extern int httpHeaderAddEntryStr2(HttpHeader *hdr, http_hdr_type id, const char *attrib, int attrib_len, const char *value, int value_len);
extern void httpHeaderAddEntryString(HttpHeader *hdr, http_hdr_type id, const String *a, const String *v);

extern void httpHeaderInsertEntryStr(HttpHeader *hdr, int pos, http_hdr_type id, const char *attrib, const char *value);

extern int httpHeaderDelByName(HttpHeader * hdr, const char *name);
extern int httpHeaderDelById(HttpHeader * hdr, http_hdr_type id);
extern void httpHeaderDelAt(HttpHeader * hdr, HttpHeaderPos pos);
extern int httpHeaderIdByName(const char *name, int name_len, const HttpHeaderFieldInfo * attrs, int end);
extern String httpHeaderGetByName(const HttpHeader * hdr, const char *name);
extern int httpHeaderIdByNameDef(const char *name, int name_len);
extern const char *httpHeaderNameById(int id);
extern int httpHeaderHas(const HttpHeader * hdr, http_hdr_type id);
extern void httpHeaderRefreshMask(HttpHeader * hdr);
extern void httpHeaderRepack(HttpHeader * hdr);

/* append/update */
extern void httpHeaderUpdate(HttpHeader * old, const HttpHeader * fresh, const HttpHeaderMask * denied_mask);

#endif
