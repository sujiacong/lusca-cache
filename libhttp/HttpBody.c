
/*
 * $Id: HttpBody.c 13457 2008-12-27 03:27:42Z adrian.chadd $
 *
 * DEBUG: section 56    HTTP Message Body
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

#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/util.h"
#include "../libcore/valgrind.h"
#include "../libcore/varargs.h"
#include "../libcore/debug.h"
#include "../libcore/kb.h"
#include "../libcore/gb.h"
#include "../libcore/tools.h"

#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"

#include "HttpBody.h"

void
httpBodyInit(HttpBody * body)
{
    body->mb = MemBufNull;
}

void
httpBodyClean(HttpBody * body)
{
    assert(body);
    if (!memBufIsNull(&body->mb))
	memBufClean(&body->mb);
}

/* set body by absorbing mb */
void
httpBodySet(HttpBody * body, MemBuf * mb)
{
    assert(body);
    assert(memBufIsNull(&body->mb));
    body->mb = *mb;		/* absorb */
}

#if UNUSED_CODE
const char *
httpBodyPtr(const HttpBody * body)
{
    return body->mb.buf ? body->mb.buf : "";
}
#endif
