
/*
 * $Id: HttpHdrCc.c 13783 2009-02-02 18:55:14Z adrian.chadd $
 *
 * DEBUG: section 65    HTTP Cache Control Header
 * AUTHOR: Alex Rousskov
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "squid.h"

void
httpHdrCcPackInto(const HttpHdrCc * cc, Packer * p)
{
    http_hdr_cc_type flag;
    int pcount = 0;
    assert(cc && p);
    for (flag = 0; flag < CC_ENUM_END; flag++) {
	if (EBIT_TEST(cc->mask, flag) && flag != CC_OTHER) {

	    /* print option name */
	    packerPrintf(p, (pcount ? ", %.*s" : "%.*s"), strLen2(CcFieldsInfo[flag].name), strBuf2(CcFieldsInfo[flag].name));

	    /* handle options with values */
	    if (flag == CC_MAX_AGE)
		packerPrintf(p, "=%d", (int) cc->max_age);

	    if (flag == CC_S_MAXAGE)
		packerPrintf(p, "=%d", (int) cc->s_maxage);

	    if (flag == CC_MAX_STALE && cc->max_stale >= 0)
		packerPrintf(p, "=%d", (int) cc->max_stale);

	    if (flag == CC_STALE_WHILE_REVALIDATE)
		packerPrintf(p, "=%d", (int) cc->stale_while_revalidate);

	    pcount++;
	}
    }
    if (strLen(cc->other))
	packerPrintf(p, (pcount ? ", %.*s" : "%.*s"), strLen2(cc->other), strBuf2(cc->other));
}

void
httpHdrCcStatDumper(StoreEntry * sentry, int idx, double val, double size, int count)
{
    extern const HttpHeaderStat *dump_stat;	/* argh! */
    const int id = (int) val;
    const int valid_id = id >= 0 && id < CC_ENUM_END;
    const char *name = valid_id ? strBuf2(CcFieldsInfo[id].name) : "INVALID";
    int name_len = valid_id ? strLen2(CcFieldsInfo[id].name) : 7;
    if (count || valid_id)
	storeAppendPrintf(sentry, "%2d\t %-20.*s\t %5d\t %6.2f\n",
	    id, name_len, name, count, xdiv(count, dump_stat->ccParsedCount));
}
