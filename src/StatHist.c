
/*
 * $Id: StatHist.c 12826 2008-06-08 13:51:32Z adrian.chadd $
 *
 * DEBUG: section 62    Generic Histogram
 * AUTHOR: Duane Wessels
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

/*
 * Important restrictions on val_in and val_out functions:
 * 
 *   - val_in:  ascending, defined on [0, oo), val_in(0) == 0;
 *   - val_out: x == val_out(val_in(x)) where val_in(x) is defined
 *
 *  In practice, the requirements are less strict, 
 *  but then it gets hard to define them without math notation.
 *  val_in is applied after offseting the value but before scaling
 *  See log and linear based histograms for examples
 */

#include "squid.h"

/* Local functions */
static StatHistBinDumper statHistBinDumper;

static void
statHistBinDumper(StoreEntry * sentry, int idx, double val, double size, int count)
{
    if (count)
	storeAppendPrintf(sentry, "\t%3d/%f\t%d\t%f\n",
	    idx, val, count, count / size);
}

void
statHistDump(const StatHist * H, StoreEntry * sentry, StatHistBinDumper * bd)
{
    int i;
    double left_border = H->min;
    if (!bd)
	bd = statHistBinDumper;
    for (i = 0; i < H->capacity; i++) {
	const double right_border = statHistVal(H, i + 1);
	assert(right_border - left_border > 0.0);
	bd(sentry, i, left_border, right_border - left_border, H->bins[i]);
	left_border = right_border;
    }
}

void
statHistEnumDumper(StoreEntry * sentry, int idx, double val, double size, int count)
{
    if (count)
	storeAppendPrintf(sentry, "%2d\t %5d\t %5d\n",
	    idx, (int) val, count);
}

void
statHistIntDumper(StoreEntry * sentry, int idx, double val, double size, int count)
{
    if (count)
	storeAppendPrintf(sentry, "%9d\t%9d\n", (int) val, count);
}
