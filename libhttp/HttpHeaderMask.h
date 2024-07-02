#ifndef	__LIBHTTP_HTTPHEADERMASK_H__
#define	__LIBHTTP_HTTPHEADERMASK_H__

/*
 * XXX the HttpHeaderMask type is defined in HttpHeader.h ; shifting
 * XXX it should be shifted here but that'll require rejiggling the
 * XXX header include dependency order. Do it later!
 */

extern void httpHeaderMaskInit(HttpHeaderMask * mask, int value);
extern void httpHeaderCalcMask(HttpHeaderMask * mask, const http_hdr_type * enums, int count);


#endif
