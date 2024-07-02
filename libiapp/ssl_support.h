
/*
 * $Id: ssl_support.h 14563 2010-04-10 23:20:36Z radiant@aol.jp $
 *
 * AUTHOR: Benno Rice
 *
 * SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by the
 *  National Science Foundation.  Squid is Copyrighted (C) 1998 by
 *  Duane Wessels and the University of California San Diego.  Please
 *  see the COPYRIGHT file for full details.  Squid incorporates
 *  software developed and/or copyrighted by other sources.  Please see
 *  the CREDITS file for full details.
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

#ifndef __LIBIAPP_SSL_SUPPORT_H__
#define __LIBIAPP_SSL_SUPPORT_H__

#include "config.h"
#if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#if HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#if HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif

#if	USE_SSL

SSL_CTX *sslCreateServerContext(const char *certfile, const char *keyfile, int version, const char *cipher, const char *options, const char *flags, const char *clientCA, const char *CAfile, const char *CApath, const char *CRLfile, const char *dhpath, const char *context);
SSL_CTX *sslCreateClientContext(const char *certfile, const char *keyfile, int version, const char *cipher, const char *options, const char *flags, const char *CAfile, const char *CApath, const char *CRLfile);
int ssl_read_method(int, char *, int);
int ssl_write_method(int, const char *, int);
int ssl_shutdown_method(int);
int ssl_verify_domain(const char *host, SSL *);

const char *sslGetUserEmail(SSL * ssl);
const char *sslGetUserAttribute(SSL * ssl, const char *attribute);
const char *sslGetCAAttribute(SSL * ssl, const char *attribute);
const char *sslGetUserCertificatePEM(SSL * ssl);
const char *sslGetUserCertificateChainPEM(SSL * ssl);

#ifdef _SQUID_MSWIN_

#define SSL_set_fd(s,f) (SSL_set_fd(s,fd_table[fd].win32.handle))

#endif /* _SQUID_MSWIN_ */

extern const char * ssl_password;
extern const char * ssl_engine;
extern int ssl_unclean_shutdown;

#endif

#endif /* __LIBIAPP_SSL_SUPPORT_H__ */
