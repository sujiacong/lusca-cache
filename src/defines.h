
/*
 * $Id: defines.h 14694 2010-05-23 13:06:44Z adrian.chadd $
 *
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

#ifndef SQUID_DEFINES_H
#define SQUID_DEFINES_H

/* Define load weights for cache_dir types */
#define MAX_LOAD_VALUE 1000

#define COSS_LOAD_BASE 0
#define AUFS_LOAD_BASE 100
#define DISKD_LOAD_BASE 100
#define UFS_LOAD_BASE 500

#define COSS_LOAD_STRIPE_WEIGHT (900 - COSS_LOAD_BASE)
#define COSS_LOAD_QUEUE_WEIGHT (100 - COSS_LOAD_BASE)
#if COSS_LOAD_QUEUE_WEIGHT < 0
#undef COSS_LOAD_QUEUE_WEIGHT
#define COSS_LOAD_QUEUE_WEIGHT 0
#endif

#define AUFS_LOAD_QUEUE_WEIGHT (MAX_LOAD_VALUE - AUFS_LOAD_BASE)

#define DISKD_LOAD_QUEUE_WEIGHT (MAX_LOAD_VALUE - DISKD_LOAD_BASE)

#define ACL_NAME_SZ 32
#define BROWSERNAMELEN 128

#define ACL_SUNDAY	0x01
#define ACL_MONDAY	0x02
#define ACL_TUESDAY	0x04
#define ACL_WEDNESDAY	0x08
#define ACL_THURSDAY	0x10
#define ACL_FRIDAY	0x20
#define ACL_SATURDAY	0x40
#define ACL_ALLWEEK	0x7F
#define ACL_WEEKDAYS	0x3E

#define DNS_INBUF_SZ 4096

#define HTTP_REPLY_FIELD_SZ 128

#define BUF_TYPE_8K 	1
#define BUF_TYPE_MALLOC 2

#define ANONYMIZER_NONE		0
#define ANONYMIZER_STANDARD	1
#define ANONYMIZER_PARANOID	2

#define USER_IDENT_SZ 64
#define IDENT_NONE 0
#define IDENT_PENDING 1
#define IDENT_DONE 2

#define MAX_MIME 4096

/* Mark a neighbor cache as dead if it doesn't answer this many pings */
#define HIER_MAX_DEFICIT  20

#define ICP_FLAG_HIT_OBJ     0x80000000ul
#define ICP_FLAG_SRC_RTT     0x40000000ul

/* Version */
#define ICP_VERSION_2		2
#define ICP_VERSION_3		3
#define ICP_VERSION_CURRENT	ICP_VERSION_2

#define DIRECT_UNKNOWN 0
#define DIRECT_NO    1
#define DIRECT_MAYBE 2
#define DIRECT_YES   3

#define REDIRECT_NONE 0
#define REDIRECT_PENDING 1
#define REDIRECT_DONE 2

#define AUTHENTICATE_AV_FACTOR 1000
/* AUTHENTICATION */

#define NTLM_CHALLENGE_SZ 300

#define  CONNECT_PORT        443

#define current_stacksize(stack) ((stack)->top - (stack)->base)

/* logfile status */
#define LOG_ENABLE  1
#define LOG_DISABLE 0

#define MAX_FILES_PER_DIR (1<<20)

#define PEER_MAX_ADDRESSES 10
#define RTT_AV_FACTOR      50

#define PEER_DEAD 0
#define PEER_ALIVE 1

#define AUTH_MSG_SZ 4096
#define HTTP_REPLY_BUF_SZ 4096
#define CLIENT_REQ_BUF_SZ 4096

#if !defined(ERROR_BUF_SZ) && defined(MAX_URL)
#define ERROR_BUF_SZ (MAX_URL << 2)
#endif

#if SQUID_SNMP
#define VIEWINCLUDED    1
#define VIEWEXCLUDED    2
#endif

#define STORE_META_KEY STORE_META_KEY_MD5

#define STORE_META_TLD_START sizeof(int)+sizeof(char)
#define STORE_META_TLD_SIZE STORE_META_TLD_START
#define SwapMetaType(x) (char)x[0]
#define SwapMetaSize(x) &x[sizeof(char)]
#define SwapMetaData(x) &x[STORE_META_TLD_START]
#define STORE_HDR_METASIZE (4*sizeof(time_t)+2*sizeof(u_short)+sizeof(squid_file_sz))
#define STORE_HDR_METASIZE_OLD (4*sizeof(time_t)+2*sizeof(u_short)+sizeof(size_t))

#define STORE_ENTRY_WITH_MEMOBJ		1
#define STORE_ENTRY_WITHOUT_MEMOBJ	0

#define COUNT_INTERVAL 60
/*
 * keep 60 minutes' worth of per-minute readings (+ current reading)
 */
#define N_COUNT_HIST (3600 / COUNT_INTERVAL) + 1
/*
 * keep 3 days' (72 hours) worth of hourly readings
 */
#define N_COUNT_HOUR_HIST (86400 * 3) / (60 * COUNT_INTERVAL)

/* were to look for errors if config path fails */
#ifndef DEFAULT_SQUID_ERROR_DIR
#define DEFAULT_SQUID_ERROR_DIR "/usr/local/squid/etc/errors"
#endif

/*
 * Max number of ICP messages to receive per call to icpHandleUdp
 */
#define INCOMING_ICP_MAX 15
/*
 * Max number of HTTP connections to accept per call to httpAccept
 * and PER HTTP PORT
 */
#define INCOMING_HTTP_MAX 10
#define INCOMING_TOTAL_MAX (INCOMING_ICP_MAX+INCOMING_HTTP_MAX)

/*
 * This many TCP connections must FAIL before we mark the
 * peer as DEAD
 */
#define PEER_TCP_MAGIC_COUNT 10

#define STORE_CLIENT_BUF_SZ 4096

#define URI_WHITESPACE_STRIP 0
#define URI_WHITESPACE_ALLOW 1
#define URI_WHITESPACE_ENCODE 2
#define URI_WHITESPACE_CHOP 3
#define URI_WHITESPACE_DENY 4

#ifndef _PATH_DEVNULL
#ifdef _SQUID_MSWIN_
#define _PATH_DEVNULL "NUL"
#else
#define _PATH_DEVNULL "/dev/null"
#endif
#endif

#ifndef O_TEXT
#define O_TEXT 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_NOATIME
#define O_NOATIME 0
#endif

/* Windows Port */
#ifdef _SQUID_WIN32_
#define _WIN_SQUID_SERVICE_CONTROL_STOP SERVICE_CONTROL_STOP
#define _WIN_SQUID_SERVICE_CONTROL_SHUTDOWN SERVICE_CONTROL_SHUTDOWN
#define _WIN_SQUID_SERVICE_CONTROL_INTERROGATE SERVICE_CONTROL_INTERROGATE
#define _WIN_SQUID_SERVICE_CONTROL_ROTATE	128
#define _WIN_SQUID_SERVICE_CONTROL_RECONFIGURE	129
#define _WIN_SQUID_SERVICE_CONTROL_DEBUG	130
#define _WIN_SQUID_SERVICE_CONTROL_INTERRUPT 	131
#define _WIN_SQUID_DEFAULT_SERVICE_NAME		"Squid"
#define _WIN_SQUID_SERVICE_OPTION		"--ntservice"
#define _WIN_SQUID_RUN_MODE_INTERACTIVE		0
#define _WIN_SQUID_RUN_MODE_SERVICE		1
#endif

#define	LOGFILE_SEQNO(n)	( (n)->sequence_number )

#define	storeSwapTLVFree	tlv_free

#endif /* SQUID_DEFINES_H */
