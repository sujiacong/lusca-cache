#ifndef	__LIBHTTP_HTTP_HEADER_LIST_H__
#define	__LIBHTTP_HTTP_HEADER_LIST_H__

extern String httpHeaderGetList(const HttpHeader * hdr, http_hdr_type id);
extern String httpHeaderGetStrOrList(const HttpHeader * hdr, http_hdr_type id);
extern String httpHeaderGetByNameListMember(const HttpHeader * hdr, const char *name,
    const char *member, const char separator);
extern String httpHeaderGetListMember(const HttpHeader * hdr, http_hdr_type id,
    const char *member, const char separator);

#endif
