
/*
 * $Id: enums.h 14688 2010-05-23 07:40:23Z adrian.chadd $
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

#ifndef SQUID_ENUMS_H
#define SQUID_ENUMS_H

typedef enum {
    LOG_TAG_NONE,
    LOG_TCP_HIT,
    LOG_TCP_MISS,
    LOG_TCP_REFRESH_HIT,
    LOG_TCP_REFRESH_FAIL_HIT,
    LOG_TCP_REFRESH_MISS,
    LOG_TCP_CLIENT_REFRESH_MISS,
    LOG_TCP_IMS_HIT,
    LOG_TCP_SWAPFAIL_MISS,
    LOG_TCP_NEGATIVE_HIT,
    LOG_TCP_MEM_HIT,
    LOG_TCP_DENIED,
    LOG_TCP_OFFLINE_HIT,
#if LOG_TCP_REDIRECTS
    LOG_TCP_REDIRECT,
#endif
    LOG_TCP_STALE_HIT,
    LOG_TCP_ASYNC_HIT,
    LOG_TCP_ASYNC_MISS,
    LOG_UDP_HIT,
    LOG_UDP_MISS,
    LOG_UDP_DENIED,
    LOG_UDP_INVALID,
    LOG_UDP_MISS_NOFETCH,
    LOG_ICP_QUERY,
    LOG_TYPE_MAX
} log_type;

typedef enum {
    ERR_NONE,
    ERR_READ_TIMEOUT,
    ERR_LIFETIME_EXP,
    ERR_READ_ERROR,
    ERR_WRITE_ERROR,
    ERR_SHUTTING_DOWN,
    ERR_CONNECT_FAIL,
    ERR_INVALID_REQ,
    ERR_UNSUP_REQ,
    ERR_INVALID_URL,
    ERR_SOCKET_FAILURE,
    ERR_DNS_FAIL,
    ERR_CANNOT_FORWARD,
    ERR_FORWARDING_DENIED,
    ERR_NO_RELAY,
    ERR_ZERO_SIZE_OBJECT,
    ERR_FTP_DISABLED,
    ERR_FTP_FAILURE,
    ERR_URN_RESOLVE,
    ERR_ACCESS_DENIED,
    ERR_CACHE_ACCESS_DENIED,
    ERR_CACHE_MGR_ACCESS_DENIED,
    ERR_SQUID_SIGNATURE,	/* not really an error */
    ERR_FTP_PUT_CREATED,	/* !error,a note that the file was created */
    ERR_FTP_PUT_MODIFIED,	/* modified, !created */
    ERR_FTP_PUT_ERROR,
    ERR_FTP_NOT_FOUND,
    ERR_FTP_FORBIDDEN,
    ERR_FTP_UNAVAILABLE,
    ERR_ONLY_IF_CACHED_MISS,	/* failure to satisfy only-if-cached request */
    ERR_TOO_BIG,
    TCP_RESET,
    ERR_INVALID_RESP,
    ERR_MAX
} err_type;

typedef enum {
    ACL_NONE,
    ACL_SRC_IP,
    ACL_DST_IP,
    ACL_MY_IP,
    ACL_SRC_DOMAIN,
    ACL_DST_DOMAIN,
    ACL_SRC_DOM_REGEX,
    ACL_DST_DOM_REGEX,
    ACL_TIME,
    ACL_URLPATH_REGEX,
    ACL_URL_REGEX,
    ACL_URL_PORT,
    ACL_MY_PORT,
    ACL_MY_PORT_NAME,
#if USE_IDENT
    ACL_IDENT,
    ACL_IDENT_REGEX,
#endif
    ACL_TYPE,
    ACL_PROTO,
    ACL_METHOD,
    ACL_BROWSER,
    ACL_REFERER_REGEX,
    ACL_PROXY_AUTH,
    ACL_PROXY_AUTH_REGEX,
    ACL_SRC_ASN,
    ACL_DST_ASN,
#if USE_ARP_ACL
    ACL_SRC_ARP,
#endif
#if SQUID_SNMP
    ACL_SNMP_COMMUNITY,
#endif
#if SRC_RTT_NOT_YET_FINISHED
    ACL_NETDB_SRC_RTT,
#endif
    ACL_MAXCONN,
    ACL_REQ_MIME_TYPE,
    ACL_REP_MIME_TYPE,
    ACL_REP_HEADER,
    ACL_REQ_HEADER,
    ACL_MAX_USER_IP,
    ACL_EXTERNAL,
    ACL_URLLOGIN,
#if USE_SSL
    ACL_USER_CERT,
    ACL_CA_CERT,
#endif
    ACL_URLGROUP,
    ACL_EXTUSER,
    ACL_EXTUSER_REGEX,
    ACL_HIER_CODE,
    ACL_DSTFWD_IP,
    ACL_ANY_OF,
    ACL_ENUM_MAX
} squid_acl;

typedef enum {
    ACL_LOOKUP_NONE,
    ACL_LOOKUP_NEEDED,
    ACL_LOOKUP_PENDING,
    ACL_LOOKUP_DONE,
    ACL_PROXY_AUTH_NEEDED
} acl_lookup_state;

typedef enum {
    PEER_NONE,
    PEER_SIBLING,
    PEER_PARENT,
    PEER_MULTICAST
} peer_t;

typedef enum {
    LOOKUP_NONE,
    LOOKUP_HIT,
    LOOKUP_MISS
} lookup_t;

typedef enum {
    HIER_NONE,
    HIER_DIRECT,
    SIBLING_HIT,
    PARENT_HIT,
    DEFAULT_PARENT,
    SINGLE_PARENT,
    FIRSTUP_PARENT,
    FIRST_PARENT_MISS,
    CLOSEST_PARENT_MISS,
    CLOSEST_PARENT,
    CLOSEST_DIRECT,
    NO_DIRECT_FAIL,
    SOURCE_FASTEST,
    ROUNDROBIN_PARENT,
#if USE_CACHE_DIGESTS
    CD_PARENT_HIT,
    CD_SIBLING_HIT,
#endif
    CARP,
    ANY_OLD_PARENT,
    USERHASH_PARENT,
    SOURCEHASH_PARENT,
    PINNED,
    HIER_MAX
} hier_code;

typedef enum {
    ICP_INVALID,
    ICP_QUERY,
    ICP_HIT,
    ICP_MISS,
    ICP_ERR,
    ICP_SEND,
    ICP_SENDA,
    ICP_DATABEG,
    ICP_DATA,
    ICP_DATAEND,
    ICP_SECHO,
    ICP_DECHO,
    ICP_NOTIFY,
    ICP_INVALIDATE,
    ICP_DELETE,
    ICP_UNUSED15,
    ICP_UNUSED16,
    ICP_UNUSED17,
    ICP_UNUSED18,
    ICP_UNUSED19,
    ICP_UNUSED20,
    ICP_MISS_NOFETCH,
    ICP_DENIED,
    ICP_HIT_OBJ,
    ICP_END
} icp_opcode;

enum {
    NOT_IN_MEMORY,
    IN_MEMORY
};

enum {
    PING_NONE,
    PING_WAITING,
    PING_DONE
};

enum {
    STORE_OK,
    STORE_PENDING
};

enum {
    SWAPOUT_NONE,
    SWAPOUT_WRITING,
    SWAPOUT_DONE
};

typedef enum {
    STORE_NON_CLIENT,
    STORE_MEM_CLIENT,
    STORE_DISK_CLIENT
} store_client_t;

/*
 * These are for StoreEntry->flag, which is defined as a SHORT
 *
 * NOTE: These flags are written to swap.state, so think very carefully
 * about deleting or re-assigning!
 */
enum {
    ENTRY_SPECIAL,
    ENTRY_REVALIDATE,
    DELAY_SENDING,
    RELEASE_REQUEST,
    REFRESH_FAILURE,
    ENTRY_CACHABLE,
    ENTRY_DISPATCHED,
    KEY_PRIVATE,
    ENTRY_FWD_HDR_WAIT,
    ENTRY_NEGCACHED,
    ENTRY_VALIDATED,
    ENTRY_BAD_LENGTH,
    ENTRY_ABORTED,
    ENTRY_DEFER_READ,
    KEY_EARLY_PUBLIC
};

typedef enum {
    ACCESS_DENIED,
    ACCESS_ALLOWED,
    ACCESS_REQ_PROXY_AUTH
} allow_t;

typedef enum {
    AUTH_ACL_CHALLENGE = -2,
    AUTH_ACL_HELPER = -1,
    AUTH_ACL_CANNOT_AUTHENTICATE = 0,
    AUTH_AUTHENTICATED = 1
} auth_acl_t;

typedef enum {
    AUTH_UNKNOWN,		/* default */
    AUTH_BASIC,
    AUTH_NTLM,
    AUTH_DIGEST,
    AUTH_NEGOTIATE,
    AUTH_BROKEN			/* known type, but broken data */
} auth_type_t;

/* stateful helper reservation info */
typedef enum {
    S_HELPER_FREE,		/* available for requests */
    S_HELPER_RESERVED,		/* in a reserved state - no active request, but state data in the helper shouldn't be disturbed */
    S_HELPER_DEFERRED		/* available for requests, and at least one more will come from a previous caller with the server pointer */
} stateful_helper_reserve_t;


#if SQUID_SNMP
enum {
    SNMP_C_VIEW,
    SNMP_C_USER,
    SNMP_C_COMMUNITY
};

#endif

#if HTTP_GZIP

/*STORE_META_VALID does not use anywhere. so I use it. It's evil.*/
#define STORE_META_GZIP STORE_META_VALID

#endif

enum {
    STORE_LOG_CREATE,
    STORE_LOG_SWAPIN,
    STORE_LOG_SWAPOUT,
    STORE_LOG_RELEASE,
    STORE_LOG_SWAPOUTFAIL
};

enum {
    MEDIAN_HTTP,
    MEDIAN_ICP_QUERY,
    MEDIAN_DNS,
    MEDIAN_HIT,
    MEDIAN_MISS,
    MEDIAN_NM,
    MEDIAN_NH,
    MEDIAN_ICP_REPLY
};

enum {
    SENT,
    RECV
};

/*
 * These are field indicators for raw cache-cache netdb transfers
 */
enum {
    NETDB_EX_NONE,
    NETDB_EX_NETWORK,
    NETDB_EX_RTT,
    NETDB_EX_HOPS
};

/*
 * Return codes from checkVary(request)
 */
enum {
    VARY_NONE,
    VARY_MATCH,
    VARY_OTHER,
    VARY_RESTART,
    VARY_CANCEL
};

typedef enum {
    ST_OP_NONE,
    ST_OP_OPEN,
    ST_OP_CREATE
} store_op_t;

/*
 * Rewrite format tokens
 */
typedef enum {
    RFT_UNKNOWN = 0,
    RFT_STRING,
    RFT_CLIENT_IPADDRESS,
    RFT_LOCAL_IPADDRESS,
    RFT_LOCAL_PORT,
    RFT_EPOCH_SECONDS,
    RFT_TIME_SUBSECONDS,
    RFT_REQUEST_HEADER,
    RFT_USERNAME,
    RFT_USERLOGIN,
    RFT_USERIDENT,
    RFT_USERSSL,
    RFT_EXTERNALACL_USER,
    RFT_METHOD,
    RFT_PROTOCOL,
    RFT_URL,
    RFT_URLPATH,
    RFT_URLHOST,
    RFT_HDRHOST,
    RFT_EXTERNALACL_TAG,
    RFT_EXTERNALACL_LOGSTR
} rewrite_token_type;

typedef enum {
    FORWARDED_FOR_ON,
    FORWARDED_FOR_OFF,
    FORWARDED_FOR_TRANSPARENT,
    FORWARDED_FOR_DELETE,
    FORWARDED_FOR_TRUNCATE
} forwarded_for_mode;

#if USE_HTCP

enum htcp_clr_reason {
    HTCP_CLR_PURGE,
    HTCP_CLR_INVALIDATION,
};

#endif

#endif /* SQUID_ENUMS_H */
