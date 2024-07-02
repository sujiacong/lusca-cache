
/*
 * $Id: client_side_location_rewrite.c 14821 2010-11-05 08:13:15Z adrian.chadd $
 *
 * DEBUG: section 33    Client-side Routines
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

#include "squid.h"

static void clientHttpLocationRewriteCheckDone(int answer, void *data);
static void clientHttpLocationRewrite(clientHttpRequest * http);
static void clientHttpLocationRewriteDone(void *data, char *reply);

void
clientHttpLocationRewriteCheck(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    aclCheck_t *ch;
    if (!Config.Program.location_rewrite.command || !httpHeaderHas(&rep->header, HDR_LOCATION)) {
	clientHttpLocationRewriteDone(http, NULL);
	return;
    }
    if (Config.accessList.location_rewrite) {
	ch = clientAclChecklistCreate(Config.accessList.location_rewrite, http);
	ch->reply = http->reply;
	aclNBCheck(ch, clientHttpLocationRewriteCheckDone, http);
    } else {
	clientHttpLocationRewriteCheckDone(ACCESS_ALLOWED, http);
    }
}

static void
clientHttpLocationRewriteCheckDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    if (answer == ACCESS_ALLOWED) {
	clientHttpLocationRewrite(http);
    } else {
	clientHttpLocationRewriteDone(http, NULL);
    }
}

static void
clientHttpLocationRewrite(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    if (!httpHeaderHas(&rep->header, HDR_LOCATION))
	clientHttpLocationRewriteDone(http, NULL);
    else
	locationRewriteStart(rep, http, clientHttpLocationRewriteDone, http);
}

static void
clientHttpLocationRewriteDone(void *data, char *reply)
{
    clientHttpRequest *http = data;
    HttpReply *rep = http->reply;
    ConnStateData *conn = http->conn;
    if (reply && *reply) {
	httpHeaderDelById(&rep->header, HDR_LOCATION);
	if (*reply == '/') {
	    /* We have to restore the URL as sent by the client */
	    request_t *req = http->orig_request;
	    const char *proto = conn->port->protocol;
	    const char *host = httpHeaderGetStr(&req->header, HDR_HOST);
	    if (!host)
		host = req->host;
	    httpHeaderPutStrf(&rep->header, HDR_LOCATION, "%s://%s%s", proto, host, reply);
	} else {
	    httpHeaderPutStr(&rep->header, HDR_LOCATION, reply);
	}
    }
    clientHttpReplyAccessCheck(http);
}

