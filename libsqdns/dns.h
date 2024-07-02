#ifndef	__LIBSQDNS_DNS_H__
#define	__LIBSQDNS_DNS_H__

/*
 * Max number of DNS messages to receive per call to DNS read handler
 */
#define INCOMING_DNS_MAX 15

typedef void IDNSCB(void *, rfc1035_rr *, int, const char *);

#endif
