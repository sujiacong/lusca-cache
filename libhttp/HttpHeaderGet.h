#ifndef	__LIBHTTP_HTTP_HEADER_GET_H__
#define	__LIBHTTP_HTTP_HEADER_GET_H__

extern int httpHeaderGetInt(const HttpHeader * hdr, http_hdr_type id);
extern squid_off_t httpHeaderGetSize(const HttpHeader * hdr, http_hdr_type id);
extern time_t httpHeaderGetTime(const HttpHeader * hdr, http_hdr_type id);
extern const char * httpHeaderGetStr(const HttpHeader * hdr, http_hdr_type id);
extern const char * httpHeaderGetLastStr(const HttpHeader * hdr, http_hdr_type id);
extern TimeOrTag httpHeaderGetTimeOrTag(const HttpHeader * hdr, http_hdr_type id);
extern const char *httpHeaderGetAuth(const HttpHeader * hdr, http_hdr_type id, const char *auth_scheme);

#endif
