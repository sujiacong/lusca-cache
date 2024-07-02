#ifndef	__LIBIAPP_SSL_H__
#define	__LIBIAPP_SSL_H__

#include "../include/config.h"

#if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#if HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#if HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif

#endif
