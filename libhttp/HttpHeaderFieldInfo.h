#ifndef	__LIBHTTP_HTTPHEADERFIELDINFO_H__
#define	__LIBHTTP_HTTPHEADERFIELDINFO_H__

/* compiled version of HttpHeaderFieldAttrs plus stats */
struct _HttpHeaderFieldInfo {
    http_hdr_type id;
    String name;
    field_type type;
    HttpHeaderFieldStat stat;
};
typedef struct _HttpHeaderFieldInfo HttpHeaderFieldInfo;

extern HttpHeaderFieldInfo * httpHeaderBuildFieldsInfo(const HttpHeaderFieldAttrs * attrs, int count);
extern void httpHeaderDestroyFieldsInfo(HttpHeaderFieldInfo * table, int count);

#endif
