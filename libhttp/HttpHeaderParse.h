#ifndef	__LIBHTTP_HTTPHEADERPARSE_H__
#define	__LIBHTTP_HTTPHEADERPARSE_H__


extern int httpHeaderParse(HttpHeader * hdr, const char *header_start, const char *header_end);
extern int httpHeaderParseInt(const char *start, int *val);
extern int httpHeaderParseSize(const char *start, squid_off_t * sz);
extern void httpHeaderNoteParsedEntry(http_hdr_type id, String context, int error);

extern int HeaderEntryParsedCount;
extern int httpConfig_relaxed_parser;

#endif
