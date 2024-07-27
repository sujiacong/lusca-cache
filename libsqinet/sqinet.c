/*!
 * @header sqinet - IPv4/IPv6 management functions
 *
 * These functions provide an IPv4/IPv6 aware end-point identifier type.
 *
 * @copyright Adrian Chadd <adrian@squid-cache.org>
 */

#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#include <assert.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#if HAVE_NETDB_H
#include <netdb.h>
#endif
#include <sys/un.h>

#include "../include/util.h"		/* for memrcmp(); perhaps that should be broken out? */
#include "../include/hash.h"		/* for hash4() */
#include "sqinet.h"

/* bit opearations on a char[] IP mask of unlimited length - reverse bit order */
/* Based off CBIT_*, the important difference is the IBIT_BIT bit selector */

/* ie, bit position iteration happens from high bit to low bit in each byte,
 * versus CBIT_ which occurs low bit to high bit in each byte.
 */

#define IBIT_BIT(bit)           (1<<( 7 - ((bit)%8)))
#define IBIT_BIN(mask, bit)     (mask)[(bit)>>3]
#define IBIT_SET(mask, bit)     ((void)(IBIT_BIN(mask, bit) |= IBIT_BIT(bit)))
#define IBIT_CLR(mask, bit)     ((void)(IBIT_BIN(mask, bit) &= ~IBIT_BIT(bit)))
#define IBIT_TEST(mask, bit)    ((IBIT_BIN(mask, bit) & IBIT_BIT(bit)) != 0)


/*!
 * @function
 *	sqinet_init
 * @abstract
 *	Initialise the given sqaddr_t for use.
 * @discussion
 * 	This for now zero's the sqaddr_t and will eventually set an init'ed flag to 1
 *	for subsequent verification and assert or debug that the given sqaddr_t is
 *	uninitialised.
 *
 * @param	s	pointer sqaddr_t to initialise.
 */
void
sqinet_init(sqaddr_t *s)
{
	memset(s, 0, sizeof(*s));
	s->init = 1;
}

/*!
 * @function
 *	sqinet_done
 * @abstract
 *	Finish using the given sqaddr_t. It should be called in situations where
 *	the sqaddr_t is finished being used.
 * @discussion
 *	It is currently a no-op; no debugging is being done to ensure that sqaddr_t's
 *	are properly init'ed and done'd.
 *
 * @param	s	pointer to sqaddr_t to finish using.
 */
void
sqinet_done(sqaddr_t *s)
{
	/* XXX we can't yet enforce that this is only deinit'ed once */
	s->init = 0;
}

void
sqinet_set_family(sqaddr_t *s, int af_family)
{
	assert(s->init);
	assert(s->st.ss_family == 0);
	s->st.ss_family = af_family;
}

/*!
 * @function
 *	sqinet_set_mask_addr
 * @abstract
 *	Set an address value containing the CIDR "mask". The address will be
 *	partially overwritten by '1' bits; callers should ensure the address
 *	is all 0's (ie, any addr) by either calling sqinet_init() and sqinet_set_family()
 *	or, if the sqaddr is already init'ed, setting the value to anyaddr.
 *
 * @discussion
 *	The sqaddr type is slightly overloaded as both an IP address and netmask
 *	"container". Its a bit unfortunate but it works out well enough in practice.
 *	This function sets a CIDR mask value by setting '1' to the mask bits.
 *	It -probably- should set 0 bits for the rest but I'm assuming for now that
 *	the callee will only do this to a freshly setup sqaddr.
 *
 * @param	dst 	pointer to sqaddr_t to modify
 * @param	masklen	number of mask bits to set in the sqaddr_t address field
 * @return		1 if successful, 0 if error (for example, >32 mask bits requested
 *			for an AF_INET address.)
 */
int
sqinet_set_mask_addr(sqaddr_t *dst, int masklen)
{
	int i;
	unsigned char * p = NULL;
	int plen = 0;

	assert(dst->init);

	switch (dst->st.ss_family) {
		case AF_INET:
			p = (unsigned char *) &(((struct sockaddr_in *) &dst->st)->sin_addr);
			plen = 32;
			break;
		case AF_INET6:
			p = (unsigned char *) &(((struct sockaddr_in6 *) &dst->st)->sin6_addr);
			plen = 128;
			break;
		default:
			assert(1==0);
	}

	for (i = 0; i <= plen && masklen > 0; i++, masklen--) {
		IBIT_SET(p, i);
	}

	return 1;
}

/*!
 * @function
 *	sqinet_copy_v4_inaddr
 * @abstract
 *	Copy the given sqaddr_t IPv4 address to the given in_addr pointer after checking
 *	the sqaddr_t is an IPv4 address.
 *
 * @param	src	pointer to the sqaddr_t to copy the IPv4 address from.
 * @param	dst	pointer to the in_addr to set with the IPv4 address.
 * @param	flags	ORed flags from sqaddr_flags enum; control the behaviour
 * 			in face of errors.
 * @return		1 if successful, 0 if failure and SQADDR_ASSERT_IS_V4 isn't set. 
 */
int
sqinet_copy_v4_inaddr(const sqaddr_t *src, struct in_addr *dst, sqaddr_flags flags)
{
	struct sockaddr_in *s;

	assert(src->init);
	/* Must be a v4 address */
	if (flags & SQADDR_ASSERT_IS_V4)
		assert(src->st.ss_family == AF_INET);
	if (src->st.ss_family != AF_INET)
		return 0;

	s = (struct sockaddr_in *) &src->st;
	*dst = s->sin_addr;
	return 1;
}

/*!
 * @function
 *	sqinet_get_unix_addr
 * @abstract
 *	Return the unix domain address of the given sqaddr_t.
 * @discussion
 *	This routine will assert() if the passed-in sqaddr_t is not initialised.
 *
 * @param	s	pointer to sqaddr_t to return the domain path for.
 * @return		the unix domain string.
 */
const char*
sqinet_get_unix_addr(const sqaddr_t *s)
{
	struct sockaddr_un *unixaddr = (struct sockaddr_un *) &s->st;
	assert(s->init);
	switch (s->st.ss_family) {
		case PF_UNIX:
			 return unixaddr->sun_path;
		default:
			assert(0);
	}
	return 0;
}

/*!
 * @function
 *	sqinet_set_unix_addr
 * @abstract
 *	Set the given sqaddr_t to the given unix domain address.
 * @discussion
 *	The sqaddr_t should be init'ed but not assigned any particular address.
 *	The code does not currently check that the sqaddr_t is init'ed or previously assigned
 *	and will silently overwrite the existing address.
 *
 * @param	s	pointer to the sqaddr_t to set the unix domain address of.
 * @param	addr	pointer to the sockaddr_un domain address.
 * @return		1 is succesful, 0 if failure (eg not initialised, address not set.)
 */
int sqinet_set_unix_addr(sqaddr_t *s, struct sockaddr_un* addr)
{
	struct sockaddr_un *unixaddr; 
	assert(s->init);
	unixaddr = (struct sockaddr_un *) &s->st;
	unixaddr->sun_family = PF_UNIX;
	memcpy(unixaddr->sun_path,addr->sun_path,sizeof(addr->sun_path));
	return 1;
}

/*!
 * @function
 *	sqinet_set_v4_inaddr
 * @abstract
 *	Set the given sqaddr_t to the given IPv4 address. The IPv4 port is set to 0.
 * @discussion
 *	The sqaddr_t should be init'ed but not assigned any particular address.
 *	The code does not currently check that the sqaddr_t is init'ed or previously assigned
 *	and will silently overwrite the existing address.
 *
 * @param	s	pointer to the sqaddr_t to set the IPv4 address of.
 * @param	v4addr	pointer to the in_addr to set the IPv4 address from.
 * @return		1 is succesful, 0 if failure (eg not initialised, address not set.)
 */
int
sqinet_set_v4_inaddr(sqaddr_t *s, struct in_addr *v4addr)
{
	struct sockaddr_in *v4; 

	assert(s->init);
	s->st.ss_family = AF_INET;

	v4 = (struct sockaddr_in *) &s->st;
	v4->sin_family = AF_INET;		/* XXX is this needed? Its a union after all.. */
	v4->sin_addr = *v4addr;
	v4->sin_port = 0;
	return 1;
}

/*!
 * @function
 *	sqinet_set_v4_port
 * @abstract
 *	Set the IPv4 port of the given sqaddr_t.
 * @discussion
 *	It may be more sensible to write an sqinet_set_port() function which
 *	does the "correct" thing after checking the type.
 *
 *	It also may be more sensible to only allow the port to be set once
 * 	and then enforce the user to call a "clear" function before setting
 *	a new port.
 *
 * @param	s	pointer to the sqaddr_t to set the IPv4 port of.
 * @param	port	IPv4 port to set in host byte order.
 * @param	flags	Determine behaviour of function if error.
 * @return		1 if port value set successfully, 0 if error.
 */
int
sqinet_set_v4_port(sqaddr_t *s, short port, sqaddr_flags flags)
{
	struct sockaddr_in *v4;

	assert(s->init);
	/* Must be a v4 address */
	if (flags & SQADDR_ASSERT_IS_V4)
		assert(s->st.ss_family == AF_INET);
	if (s->st.ss_family != AF_INET)
		return 0;
	v4 = (struct sockaddr_in *) &s->st;
	v4->sin_port = htons(port);
	return 1;
}

/*!
 * @function
 *	sqinet_set_v4_sockaddr
 * @abstract
 *	Set the sqaddr_t to the given IPv4 address/port.
 * @discussion
 *	This should be called on a freshly init'ed sqaddr_t before it
 *	has had another address set.
 *
 * @param	s	pointer to the sqaddr_t to set the IPv4 address/port of.
 * @param	v4addr	pointer to the sockaddr_in containing the IPv4 address/port.
 * @return		1 if value set successfully, 0 if error.
 */
int
sqinet_set_v4_sockaddr(sqaddr_t *s, const struct sockaddr_in *v4addr)
{
	struct sockaddr_in *v4;

	assert(s->init);
	v4 = (struct sockaddr_in *) &s->st;
	*v4 = *v4addr;
	s->st.ss_family = AF_INET;
	return 1;
}

/*!
 * @function
 *	sqinet_get_v4_inaddr
 * @abstract
 *	return a struct in_addr containing the IPv4 address, or INADDR_NONE
 *	for uninitialised or IPv6 address.
 * @discussion
 *	The method for returning "invalid" addresses is a bit silly..
 *
 * @param	s	pointer to sqaddr_t to return the IPv4 address of.
 * @param	flags	control behaviour on error.
 * @return		IPv4 address via in_addr, or INADDR_NONE on error/IPv6 address.
 */
struct in_addr
sqinet_get_v4_inaddr(const sqaddr_t *s, sqaddr_flags flags)
{
	struct sockaddr_in *v4;
	struct in_addr none_addr;

	none_addr.s_addr = INADDR_NONE;
	assert(s->init);
	if (flags & SQADDR_ASSERT_IS_V4) {
		assert(s->st.ss_family == AF_INET);
	}
	if (s->st.ss_family != AF_INET)
		return none_addr;

	v4 = (struct sockaddr_in *) &s->st;
	return v4->sin_addr;
}

/*!
 * @function
 *	sqinet_get_v4_sockaddr_ptr
 * @abstract
 *	populate a sockaddr_in containing the IPv4 address/port.
 *
 * @param	s	pointer to sqaddr_t to return the IPv4 details of.
 * @param	v4	pointer to destination sockaddr_in.
 * @param	flags	control behaviour on error.
 * @return		1 on successful assignment of the IPv4 details, 0 on error.
 */
int
sqinet_get_v4_sockaddr_ptr(const sqaddr_t *s, struct sockaddr_in *v4, sqaddr_flags flags)
{
	assert(s->init);
	if (flags & SQADDR_ASSERT_IS_V4)
		assert(s->st.ss_family == AF_INET);
	if (flags & SQADDR_ASSERT_IS_V6)
		assert(s->st.ss_family == AF_INET6);
	if(s->st.ss_family != AF_INET)
		return 0;

	*v4 = *(struct sockaddr_in *) &s->st;
	return 1;
}

/*!
 * @function
 *	sqinet_get_v4_sockaddr_ptr
 * @abstract
 *	return a sockaddr_in containing the IPv4 address/port.
 * @discussion
 *	The routine assumes the sqaddr_t family is IPv4 and will
 *	blindly return a typecast'ed sockaddr_in regardless.
 *	This should really be addressed..
 *
 * @param	s	pointer to sqaddr_t to return the IPv4 details of.
 * @param	flags	control behaviour on error.
 * @return		sockaddr_in containing the IPv4 address/port details.
 */
struct sockaddr_in
sqinet_get_v4_sockaddr(const sqaddr_t *s, sqaddr_flags flags)
{
	assert(s->init);
	if (flags & SQADDR_ASSERT_IS_V4)
		assert(s->st.ss_family == AF_INET);
	if (flags & SQADDR_ASSERT_IS_V6)
		assert(s->st.ss_family == AF_INET6);

	return * (struct sockaddr_in *) &s->st;
}

/*!
 * @function
 *	sqinet_set_v6_sockaddr
 * @abstract
 *	Set the sqaddr_t to the given IPv6 address/port.
 * @discussion
 *	This should be called on a freshly init'ed sqaddr_t before it
 *	has had another address set.
 *
 *	Some study of the IPv6 sockaddr_in6 fields will be prudent before
 *	calling this function. Pay attention to what the default values
 *	of the "new" IPv6 fields besides just the address/port fields.
 *
 * @param	s	pointer to the sqaddr_t to set the IPv6 details of.
 * @param	v4addr	pointer to the sockaddr_in containing the IPv6 details.
 * @return		1 if value set successfully, 0 if error.
 */
int
sqinet_set_v6_sockaddr(sqaddr_t *s, const struct sockaddr_in6 *v6addr)
{
	struct sockaddr_in6 *v6;

	assert(s->init);
	v6 = (struct sockaddr_in6 *) &s->st;
	*v6 = *v6addr;
	s->st.ss_family = AF_INET6;
	return 1;
}

void
sqinet_set_anyaddr(sqaddr_t *s)
{
	struct sockaddr_in *v4;
	struct sockaddr_in6 *v6;

	assert(s->init);
	switch(s->st.ss_family) {
		case AF_INET:
			v4 = (struct sockaddr_in *) &s->st;
			v4->sin_addr.s_addr = INADDR_ANY;
			break;
		case AF_INET6:
			v6 = (struct sockaddr_in6 *) &s->st;
			v6->sin6_addr = in6addr_any;
			break;
		default:
			assert(0);
	}
	return;
}

/*!
 * @function
 *	sqinet_is_anyaddr
 * @abstract
 *	Return whether the given sqaddr_t is a v4 or v6 ANY_ADDR
 * @discussion
 *	"ANY_ADDR" is defined as an all-zero's address.
 *
 *	This function will assert() if the sqaddr_t has no family.
 *
 * @param	s	pointer to sqaddr_t to check.
 * @return		1 if sqaddr_t is an ANY_ADDR (all zero's), 0 otherwise.
 */
int
sqinet_is_anyaddr(const sqaddr_t *s)
{
	struct sockaddr_in *v4;
	struct sockaddr_in6 *v6;

	assert(s->init);
	switch(s->st.ss_family) {
		case AF_INET:
			v4 = (struct sockaddr_in *) &s->st;
			return (v4->sin_addr.s_addr == INADDR_ANY);
			break;
		case AF_INET6:
			v6 = (struct sockaddr_in6 *) &s->st;
			return (memcmp(&v6->sin6_addr, &in6addr_any, sizeof(in6addr_any)) == 0);
			break;
		default:
			assert(0);
	}
	return 0;
}

void
sqinet_set_noaddr(sqaddr_t *s)
{
	struct sockaddr_in *v4;
	struct sockaddr_in6 *v6;
	struct in6_addr no6addr = {{{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }}};

	assert(s->init);
	switch(s->st.ss_family) {
		case AF_INET:
			v4 = (struct sockaddr_in *) &s->st;
			v4->sin_addr.s_addr = INADDR_NONE;
			break;
		case AF_INET6:
			v6 = (struct sockaddr_in6 *) &s->st;
			v6->sin6_addr = no6addr;
			break;
		default:
			assert(0);
	}
	return;
}


/*!
 * @function
 *	sqinet_is_noaddr
 * @abstract
 *	Return whether the given sqaddr_t is set to "no address".
 * @discussion
 *	"no address" is all-ones (ie, ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff and 255.255.255.255)
 *
 *	This routine will assert() if the passed-in sqaddr_t is not initialised.
 *
 * @param	s	pointer to sqaddr_t to check.
 * @return		1 if sqaddr_t is "no address", 0 otherwise.
 */
int
sqinet_is_noaddr(const sqaddr_t *s)
{
	struct sockaddr_in *v4;
	struct sockaddr_in6 *v6;
	struct in6_addr no6addr = {{{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }}};

	assert(s->init);
	switch(s->st.ss_family) {
		case AF_INET:
			v4 = (struct sockaddr_in *) &s->st;
			return (v4->sin_addr.s_addr == INADDR_NONE);
			break;
		case AF_INET6:
			v6 = (struct sockaddr_in6 *) &s->st;
			return (memcmp(&v6->sin6_addr, &no6addr, sizeof(no6addr)) == 0);
			break;
		default:
			assert(0);
	}
	return 0;
}

/*!
 * @function
 *	sqinet_get_port
 * @abstract
 *	Return the IPv4 or IPv6 port of the given sqaddr_t.
 * @discussion
 *	This routine will assert() if the passed-in sqaddr_t is not initialised.
 *
 * @param	s	pointer to sqaddr_t to return the port for.
 * @return		the IPv4 or IPv6 port in host-byte order.
 */
int
sqinet_get_port(const sqaddr_t *s)
{
	assert(s->init);
	switch (s->st.ss_family) {
		case AF_INET:
			return ntohs(((struct sockaddr_in *) &s->st)->sin_port);
			break;
		case AF_INET6:
			return ntohs(((struct sockaddr_in6 *) &s->st)->sin6_port);
			break;
		case PF_UNIX:
			break;
			
		default:
			assert(0);
	}
	return 0;
}

/*!
 * @function
 *	sqinet_set_port
 * @abstract
 *	Set the IPv4 or IPv6 port of the given sqaddr_t.
 * @discussion
 *	The port should only be set after the sqinet_t is init'ed and has
 *	an IPv4 or IPv6 family assigned.
 *	It also may be more sensible to only allow the port to be set once
 * 	and then enforce the user to call a "clear" function before setting
 *	a new port.
 *
 * @param	s	pointer to the sqaddr_t to set the port of.
 * @param	port	port to set in host byte order.
 * @param	flags	Determine behaviour of function if error.
 * @return		1 if port value set successfully, 0 if error.
 */
void
sqinet_set_port(const sqaddr_t *s, short port, sqaddr_flags flags)
{
	assert(s->init);
	if (flags & SQADDR_ASSERT_IS_V4)
		assert(s->st.ss_family == AF_INET);
	if (flags & SQADDR_ASSERT_IS_V6)
		assert(s->st.ss_family == AF_INET6);

	switch (s->st.ss_family) {
		case AF_INET:
			((struct sockaddr_in *) &s->st)->sin_port = htons(port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *) &s->st)->sin6_port = htons(port);
			break;
		default:
			assert(0);
	}
}

/*!
 * @function
 *	sqinet_ntoa
 * @abstract
 *	Convert an IPv4 or IPv6 address to a string.
 * @discussion
 *	This function doesn't verify that the sqaddr has been setup properly
 *	and currently just passes everything to getnameinfo().
 *
 *	This function calls getnameinfo() which may not be thread-safe!
 *
 * @param	s	sqaddr_t to convert to an IPv4/IPv6 string.
 * @param	hoststr	destination buffer pointer
 * @param	hostlen	length of destination buffer
 * @param	flags	enforce the address type
 * @return		1 on success, 0 on failure.
 */
int
sqinet_ntoa(const sqaddr_t *s, char *hoststr, int hostlen, sqaddr_flags flags)
{
	assert(s->init);
	int retval;

	if (flags & SQADDR_ASSERT_IS_V4)
		assert(s->st.ss_family == AF_INET);
	if (flags & SQADDR_ASSERT_IS_V6)
		assert(s->st.ss_family == AF_INET6);

	/* Handle the crazy [] wrapped address logic here */
	if (! (flags & SQADDR_NO_BRACKET_V6) && s->st.ss_family == AF_INET6) {
		assert(hostlen > 2);
		*hoststr = '[';
		hoststr++;
		hostlen -= 2;
	}

	if(sqinet_get_family(s) == AF_UNIX)
	{
		const char* path = sqinet_get_unix_addr(s);
		if(!path)
		{
			return 0;
		}
		strncpy(hoststr, path, MAX_IPSTRLEN);
		return 1;
	}

	retval = getnameinfo((struct sockaddr *) (&s->st), sqinet_get_length(s), hoststr, hostlen, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV);
	if (! (flags & SQADDR_NO_BRACKET_V6) && s->st.ss_family == AF_INET6) {
		size_t len = strlen(hoststr);
		assert(len < hostlen - 2);	/* XXX need space for ]\0 */
		hoststr[len] = ']';
		hoststr[len+1] = '\0';
	}
	return (retval == 0);
}

/*!
 * @function
 *	sqinet_aton
 * @abstract
 *	Perform a IP string -> sockaddr translation.
 * @discussion
 *	The sqaddr_t must be init'ed and not already set (not asserted
 *	at the moment.)
 *
 *	The port won't be filled in - the caller can do that with a
 *	sqinet_set_{v4,v6}_port() call.
 *
 *	It may be prudent to set the port to 0 just in case - assuming
 *	the caller has just init'ed the sqaddr_t and thus the port will
 *	be 0.
 *
 * @param	s	pointer to sqaddr_t
 * @param	hoststr	host string to convert to a sockaddr.
 * @param	flags	Control the behaviour:
 *			- SQATON_FAMILY_IPv4: given string is an IPv4 address.
 *			- SQATON_FAMILY_IPv6: given string is an IPv6 address.
 *			- SQATON_PASSIVE: setup return socket address for an incoming connection?
 * @return		1 if the sqaddr_t has been set with the new address; 0 on failure.
 */
int
sqinet_aton(sqaddr_t *s, const char *hoststr, sqaton_flags flags)
{
	struct addrinfo hints, *r = NULL;
	int err;

	assert(s->init);
	memset(&hints, 0, sizeof(hints));
	if (flags & SQATON_FAMILY_IPv4)
		hints.ai_family = AF_INET;
	if (flags & SQATON_FAMILY_IPv6)
		hints.ai_family = AF_INET6;
	if (flags & SQATON_PASSIVE)
		hints.ai_flags |= AI_PASSIVE;

	err = getaddrinfo(hoststr, NULL, &hints, &r);
	if (err != 0) {
		if (r != NULL)
			freeaddrinfo(r);
		return 0;
	}
	if (r == NULL) {
		return 0;
	}

	/* Just set the current sqaddr_t st to the first res pointer */
	/* We need to ensure that the lengths are compatible */
	/*
	 * Its a bit annoying that this API copies the data when most instances
	 * the caller is using this to do some kind of parsing and can use r->ai_addr
	 * direct. We may wish to replace this with a seperate function to -just- do
	 * IP string (+ port) -> sockaddr conversion which bypasses the damned
	 * allocation + copy overhead. Only if it matters..
	 */
	assert(r->ai_addrlen <= sizeof(s->st));
	memcpy(&s->st, r->ai_addr, r->ai_addrlen);
	freeaddrinfo(r);
	return 1;
}

/*!
 * @function
 *	sqinet_assemble_rev
 * @abstract
 *	Assemble a DNS reverse record entry for the given IP address.
 * @discussion
 *	This function assumes the caller passes in a big enough buffer even though "len" is
 *	given; it should be re-engineered to error out if buf isn't long enough.
 *
 * @param	s	pointer to sqaddr_t
 * @param	buf	buffer to write into
 * @param	buflen	length of buffer
 * @return	length if success, 0 on failure
 */
int
sqinet_assemble_rev(const sqaddr_t *s, char *buf, int len)
{
	int r, i;
	unsigned int ipi;
	struct in_addr a4;
	struct in6_addr a6;
	unsigned const char *s6;

	assert(s->init);
	switch (s->st.ss_family) {
		case AF_INET:
			a4 = (((struct sockaddr_in *) &s->st)->sin_addr);
			ipi = (unsigned int) ntohl(a4.s_addr);
			r = snprintf(buf, len, "%u.%u.%u.%u.in-addr.arpa", ipi & 255, (ipi >> 8) & 255, (ipi >> 16) & 255, (ipi >> 24) & 255);
			return r;
			break;
		case AF_INET6:
			/*
			 * XXX two things:
			 * XXX + is this stuff endian-friendly? Husni had endian logic in his version of this!
			 * XXX + we're not checking the length of the available buffer at all the right places!
			 */
			a6 = (((struct sockaddr_in6 *) &s->st)->sin6_addr);
			s6 = a6.s6_addr;
			r = 0;
			for (i = 0; i < 16; i++) {
				/* Make sure we've got space for this AND the .ip6.arpa at the end */
				if (r + 16 >= len) {
					return 0;
				}
				r += snprintf(buf + r, len - r, "%x.%x.", s6[i] & 0x0f, (s6[i] >> 4) & 0x0f);
			}
			strcat(buf + r, "ip6.arpa");
			return r + 8;
			break;
	}
	return 0;
}

/*!
 * @function
 *	sqinet_compare_port
 * @abstract
 *	Return whether two sqaddr_t entries point to the same address family and ports
 * @param	a	pointer to sqaddr_t
 * @param	b	pointer to sqaddr_t
 * @return		1 whether the address families and ports are equivalent, 0 otherwise
 */
int
sqinet_compare_port(const sqaddr_t *a, const sqaddr_t *b)
{
	assert(a->init);
	assert(b->init);
	if (a->st.ss_family != b->st.ss_family)
		return 0;
	switch (a->st.ss_family) {
		case AF_INET:
			return (((struct sockaddr_in *) &a->st)->sin_port) == (((struct sockaddr_in *) &b->st)->sin_port);
		break;
		case AF_INET6:
			return (((struct sockaddr_in6 *) &a->st)->sin6_port) == (((struct sockaddr_in6 *) &b->st)->sin6_port);
		break;
		default:
			assert(1==0);
	}
	return 0;
}

/*!
 * @function
 *	sqinet_compare_addr
 * @abstract
 *	Return whether two sqaddr_t entries point to the same address family and address
 * @param	a	pointer to sqaddr_t
 * @param	b	pointer to sqaddr_t
 * @return		1 whether the address families and addresses are equivalent, 0 otherwise
 */
int
sqinet_compare_addr(const sqaddr_t *a, const sqaddr_t *b)
{
	assert(a->init);
	assert(b->init);
	if (a->st.ss_family != b->st.ss_family)
		return 0;
	switch (a->st.ss_family) {
		case AF_INET:
			return (((struct sockaddr_in *) &a->st)->sin_addr.s_addr) == (((struct sockaddr_in *) &b->st)->sin_addr.s_addr);
		break;
		case AF_INET6:
			return (memcmp(
				&(((struct sockaddr_in6 *) &a->st)->sin6_addr),
				&(((struct sockaddr_in6 *) &b->st)->sin6_addr),
				sizeof((((struct sockaddr_in6 *) &a->st)->sin6_addr))) == 0);
		break;
		default:
			assert(1==0);
	}
	return 0;
}

void
sqinet_mask_addr(sqaddr_t *dst, const sqaddr_t *mask)
{
	int i;
	assert(dst->init);
	assert(mask->init);
	assert (dst->st.ss_family == mask->st.ss_family);
	switch (dst->st.ss_family) {
		case AF_INET:
			(((struct sockaddr_in *) &dst->st)->sin_addr.s_addr) &= (((struct sockaddr_in *) &mask->st)->sin_addr.s_addr);
		break;
		case AF_INET6:
			for (i = 0; i < 16; i++)
				(((struct sockaddr_in6 *) &dst->st)->sin6_addr.s6_addr[i]) &= (((struct sockaddr_in6 *) &mask->st)->sin6_addr.s6_addr[i]);
		break;
		default:
			assert(1==0);
	}
	return;

}
/*
 * This is likely an un-necessary mostly-duplicate of sqinet_compare_addr();
 * should they eventually be folded into the same routine? Probably!
 *
 * note the memrcmp() call - the data is in network order (big-endian);
 * this comparison function is designed for use by stuff like the splay
 * code; and thus wants memcmp-like semantics. The way to give it that
 * is to compare the IP address from the least significant bits first;
 * that way we can order them great or less than each other.
 */
int
sqinet_host_compare(const sqaddr_t *a, const sqaddr_t *b)
{
	assert(a->init);
	assert(b->init);
	assert (a->st.ss_family == b->st.ss_family);
	switch (a->st.ss_family) {
		case AF_INET:
			return (memrcmp(
				&(((struct sockaddr_in *) &a->st)->sin_addr),
				&(((struct sockaddr_in *) &b->st)->sin_addr),
				sizeof((((struct sockaddr_in *) &a->st)->sin_addr))));
		break;
		case AF_INET6:
			return (memrcmp(
				&(((struct sockaddr_in6 *) &a->st)->sin6_addr),
				&(((struct sockaddr_in6 *) &b->st)->sin6_addr),
				sizeof((((struct sockaddr_in6 *) &a->st)->sin6_addr))));
		break;
		default:
			assert(1==0);
	}
	return -1;
}

int
sqinet_range_compare(const sqaddr_t *a, const sqaddr_t *b_start, const sqaddr_t *b_end)
{
	return 0;
	struct in_addr in4_a, in4_b_start, in4_b_end;
	struct in6_addr *in6_a, *in6_b_start, *in6_b_end;
	const int in6_size = sizeof(struct in6_addr);

	assert(a->init);
	assert(b_start->init);
	assert(b_end->init);

	assert (a->st.ss_family == b_start->st.ss_family);
	assert (a->st.ss_family == b_end->st.ss_family);

	switch (a->st.ss_family) {
		case AF_INET:
			in4_a = (((struct sockaddr_in *) &a->st)->sin_addr);
			in4_b_start = (((struct sockaddr_in *) &b_start->st)->sin_addr);
			in4_b_end = (((struct sockaddr_in *) &b_end->st)->sin_addr);
			if (ntohl(in4_a.s_addr) > ntohl(in4_b_end.s_addr))
				return 1;
			if (ntohl(in4_a.s_addr) < ntohl(in4_b_start.s_addr))
				return -1;
			return 0;
		break;
		case AF_INET6:
			in6_a = &(((struct sockaddr_in6 *) &a->st)->sin6_addr);
			in6_b_start = &(((struct sockaddr_in6 *) &b_start->st)->sin6_addr);
			in6_b_end = &(((struct sockaddr_in6 *) &b_end->st)->sin6_addr);

			if (memrcmp(in6_a, in6_b_end, in6_size) > 0)
				return 1;
			if (memrcmp(in6_a, in6_b_start, in6_size) < 0)
				return -1;
			return 0;
		break;
		default:
			assert(1==0);
	}
	return -1;
}

int
sqinet_host_is_netaddr(const sqaddr_t *a, const sqaddr_t *mask)
{
	sqaddr_t tmp;
	int r;

	sqinet_init(&tmp);
	sqinet_copy(&tmp, a);
	sqinet_mask_addr(&tmp, mask);
	r = (sqinet_host_compare(a, &tmp) == 0);
	sqinet_done(&tmp);
	return r;
}

unsigned int
sqinet_hash_host_key(const sqaddr_t *addr, unsigned int size)
{
	const struct in_addr *in4;
	const struct in6_addr *in6;

	/* XXX not very fast? */
	switch (addr->st.ss_family) {
		case AF_INET:
			in4 = &(((struct sockaddr_in *) &addr->st)->sin_addr);
			return hash4(in4, sizeof(*in4));
			break;
		case AF_INET6:
			in6 = &(((struct sockaddr_in6 *) &addr->st)->sin6_addr);
			return hash4(in6, sizeof(*in6));
			break;
		default:
			assert(1==0);
	}
	return -1;
}

