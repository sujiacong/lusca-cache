#include "../include/config.h"

#include <stdio.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include <ctype.h>

#include "../include/util.h"

/*
 * matchDomainName() compares a hostname with a domainname according
 * to the following rules:
 * 
 *    HOST          DOMAIN        MATCH?
 * ------------- -------------    ------
 *    foo.com       foo.com         YES
 *   .foo.com       foo.com         YES
 *  x.foo.com       foo.com          NO
 *    foo.com      .foo.com         YES
 *   .foo.com      .foo.com         YES
 *  x.foo.com      .foo.com         YES
 *
 *  We strip leading dots on hosts (but not domains!) so that
 *  ".foo.com" is is always the same as "foo.com".
 *
 *  Return values:
 *     0 means the host matches the domain
 *     1 means the host is greater than the domain
 *    -1 means the host is less than the domain
 */

int
matchDomainName(const char *h, const char *d)
{
    int dl;
    int hl;
    while ('.' == *h)
	h++;
    hl = strlen(h);
    dl = strlen(d);
    /*
     * Start at the ends of the two strings and work towards the
     * beginning.
     */
    while (xtolower(h[--hl]) == xtolower(d[--dl])) {
	if (hl == 0 && dl == 0) {
	    /*
	     * We made it all the way to the beginning of both
	     * strings without finding any difference.
	     */
	    return 0;
	}
	if (0 == hl) {
	    /* 
	     * The host string is shorter than the domain string.
	     * There is only one case when this can be a match.
	     * If the domain is just one character longer, and if
	     * that character is a leading '.' then we call it a
	     * match.
	     */
	    if (1 == dl && '.' == d[0])
		return 0;
	    else
		return -1;
	}
	if (0 == dl) {
	    /*
	     * The domain string is shorter than the host string.
	     * This is a match only if the first domain character
	     * is a leading '.'.
	     */
	    if ('.' == d[0])
		return 0;
	    else
		return 1;
	}
    }
    /*
     * We found different characters in the same position (from the end).
     */
    /*
     * If one of those character is '.' then its special.  In order
     * for splay tree sorting to work properly, "x-foo.com" must
     * be greater than ".foo.com" even though '-' is less than '.'.
     */
    if ('.' == d[dl])
	return 1;
    if ('.' == h[hl])
	return -1;
    return (xtolower(h[hl]) - xtolower(d[dl]));
}
