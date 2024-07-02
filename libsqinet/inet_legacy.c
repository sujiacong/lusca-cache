#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_STRINGS_H
#include <strings.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_NETDB_H
#include <netdb.h>
#endif

#include "inet_legacy.h"

/*!
 * @header Legacy IPv4 functions
 *
 * These functions provide legacy IPv4-only address/endpoint functions.
 *
 * @copyright Squid Project
 */

/*!
 * @function
 *	xinet_ntoa
 * @abstract 
 *	A wrapper around inet_ntoa() which was intended to be the "fast" replacement
 *	where inet_ntoa() is being called very frequently.
 * @discussion
 *	For the time being this function simply calls inet_ntoa() and returns the
 *	result.
 *
 * @param	addr	IPv4 address to convert.
 * @return		a pointer to a static const char * buffer
 * 			containing the IPv4 address.
 */
const char *
xinet_ntoa(const struct in_addr addr)
{
    return inet_ntoa(addr);
}

/*!
 * @function
 *	IsNoAddr
 * @abstract
 *	Return whether the given IPv4 address is equivalent to INADDR_NONE (255.255.255.255.)
 *
 * @param	s	Pointer to the IPv4 address to check.
 * @return		1 if the IPv4 address is INADDR_NONE, 0 otherwise.
 */
int
IsNoAddr(const struct in_addr *s)
{
	return s->s_addr == INADDR_NONE;
}

/*!
 * @function
 *	IsAnyAddr
 * @abstract
 *	Return whether the given IPv4 address is equivalent to INADDR_ANY (0.0.0.0.)
 * @param	s	Pointer to the IPv4 address to check.
 * @return		1 if the IPv4 address is INADDR_NONE, 0 otherwise.
 */
int
IsAnyAddr(const struct in_addr *s)
{
	return s->s_addr == INADDR_ANY;
}

/*!
 * @function
 *	SetNoAddr
 * @abstract
 *	Set the given IPv4 address to INADDR_NONE (255.255.255.255.)
 *
 * @param	s	Pointer to the IPv4 address to set to INADDR_NONE.
 */
void
SetNoAddr(struct in_addr *s)
{
	s->s_addr = INADDR_NONE;
}

/*!
 * @function
 *	SetAnyAddr
 * @abstract
 *	Set the given IPv4 address to INADDR_ANY (0.0.0.0.)
 *
 * @param	s	Pointer to the IPv4 address to set to INADDR_ANY.
 */
void
SetAnyAddr(struct in_addr *s)
{
	s->s_addr = INADDR_ANY;
}

