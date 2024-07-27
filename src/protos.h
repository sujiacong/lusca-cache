
/*
 * $Id: protos.h 14802 2010-09-08 05:00:25Z adrian.chadd $
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

#ifndef SQUID_PROTOS_H
#define SQUID_PROTOS_H

extern void accessLogLog(AccessLogEntry *, aclCheck_t * checklist);
extern void accessLogRotate(void);
extern void accessLogClose(void);
extern void accessLogInit(void);
extern const char *accessLogTime(time_t);
extern int accessLogParseLogFormat(logformat_token ** fmt, char *def);
extern void accessLogDumpLogFormat(StoreEntry * entry, const char *name, logformat * definitions);
extern void accessLogFreeLogFormat(logformat_token ** fmt);
extern void hierarchyNote(HierarchyLogEntry *, hier_code, const char *);
#if FORW_VIA_DB
extern void fvdbCountVia(const char *key);
extern void fvdbCountForw(const char *key);
#endif
#if HEADERS_LOG
extern void headersLog(int cs, int pq, method_t * m, void *data);
#endif
char *log_quote(const char *header);

/* acl.c */
extern void aclInitMem(void);
extern aclCheck_t *aclChecklistCreate(const struct _acl_access *,
    request_t *,
    const char *ident);
void aclChecklistCacheInit(aclCheck_t * checklist);
extern void aclNBCheck(aclCheck_t *, PF *, void *);
extern int aclCheckFast(const struct _acl_access *A, aclCheck_t *);
int aclCheckFastRequest(const acl_access * A, request_t * request);
extern void aclChecklistFree(aclCheck_t *);
extern int aclMatchAclList(const acl_list * list, aclCheck_t * checklist);
extern void aclDestroyAccessList(struct _acl_access **list);
extern void aclDestroyAcls(acl **);
extern void aclDestroyAclList(acl_list **);
extern void aclParseAccessLine(struct _acl_access **);
extern void aclParseAclList(acl_list **);
extern void aclParseAclLine(acl **);
extern int aclIsProxyAuth(const char *name);
extern err_type aclGetDenyInfoPage(acl_deny_info_list ** head, const char *name, int redirect_allowed);
extern void aclParseDenyInfoLine(struct _acl_deny_info_list **);
extern void aclDestroyDenyInfoList(struct _acl_deny_info_list **);
extern void aclDestroyRegexList(struct _relist *data);
extern int aclMatchRegex(relist * data, const char *word);
extern void aclParseRegexList(void *curlist);
extern const char *aclTypeToStr(squid_acl);
extern wordlist *aclDumpGeneric(const acl *);
extern int aclPurgeMethodInUse(acl_access *);
extern void aclCacheMatchFlush(dlink_list * cache);
extern int aclAuthenticated(aclCheck_t * checklist);

/*
 * cache_cf.c
 */
extern int parseConfigFile(const char *file_name);
extern void configFreeMemory(void);
extern void wordlistCat(const wordlist *, MemBuf * mb);
extern void allocate_new_swapdir(cacheSwap *);
extern void self_destruct(void);
extern int GetInteger(void);
extern int xatoi(const char *);
extern unsigned short xatos(const char *);

/* extra functions from cache_cf.c useful for lib modules */
extern void parse_int(int *var);
extern void parse_onoff(int *var);
extern void parse_eol(char *volatile *var);
extern void parse_wordlist(wordlist ** list);
extern void requirePathnameExists(const char *name, const char *path);
extern void parse_time_t(time_t * var);
extern void parse_cachedir_options(SwapDir * sd, struct cache_dir_option *options, int reconfiguring);
extern void dump_cachedir_options(StoreEntry * e, struct cache_dir_option *options, SwapDir * sd);
extern void parse_sockaddr_in_list_token(sockaddr_in_list **, char *);

/* cbdata.c */
extern void cbdataLocalInit(void);

extern char *clientConstructTraceEcho(clientHttpRequest *);
extern int checkNegativeHit(StoreEntry *);
extern void clientOpenListenSockets(void);
extern void clientHttpConnectionsClose(void);
extern int isTcpHit(log_type);
extern void clientPinConnection(ConnStateData * conn, int fd, const request_t * request, peer * peer, int auth);
extern int clientGetPinnedInfo(const ConnStateData * conn, const request_t * request, peer ** peer);
extern int clientGetPinnedConnection(ConnStateData * conn, const request_t * request, const peer * peer, int *auth);
extern void clientReassignDelaypools(void);

extern void packerToStoreInit(Packer * p, StoreEntry * e);
extern void packerToMemInit(Packer * p, MemBuf * mb);
extern void packerClean(Packer * p);
extern void packerAppend(Packer * p, const char *buf, int size);
#if STDC_HEADERS
extern void
packerPrintf(Packer * p, const char *fmt,...) PRINTF_FORMAT_ARG2;
#else
extern void packerPrintf();
#endif

extern void xassert(const char *, const char *, int);

/* packs, then prints an object using debug() */
extern void debugObj(int section, int level, const char *label, void *obj, ObjPackMethod pm);

/* dns.c */
extern void dnsInternalInit(void);

/* dns_internal.c */
extern void idnsInternalInit(void);

/* event.c */
extern void eventLocalInit(void);

extern void fqdncache_local_params(void);
extern void fqdncache_init_local(void);
extern void fqdnStats(StoreEntry *,void*);

extern void ftpStart(FwdState *);
extern char *ftpUrlWith2f(const request_t *);

extern void gopherStart(FwdState *);
extern int gopherCachable(const request_t *);


extern void whoisStart(FwdState *);

/* http.c */
extern int httpCachable(method_t *);
extern void httpStart(FwdState *);
extern int httpBuildRequestPrefix(request_t * request,
    request_t * orig_request,
    StoreEntry * entry,
    MemBuf * mb,
    http_state_flags);
extern void httpAnonInitModule(void);
extern int httpAnonHdrAllowed(http_hdr_type hdr_id);
extern int httpAnonHdrDenied(http_hdr_type hdr_id);
extern void httpBuildRequestHeader(request_t *, request_t *, StoreEntry *, HttpHeader *, http_state_flags);
extern const char *httpMakeVaryMark(request_t * request, HttpReply * reply);
extern int httpGetCount(void);

/* HttpStatusLine.c */
extern void httpStatusLinePackInto(const HttpStatusLine * sline, Packer * p);

/* Http Body */
/* pack */
extern void httpBodyPackInto(const HttpBody * body, Packer * p);

/* Http Cache Control Header Field */
extern void httpHdrCcPackInto(const HttpHdrCc * cc, Packer * p);
extern void httpHdrCcStatDumper(StoreEntry * sentry, int idx, double val, double size, int count);

/* Http Range Header Field */
extern void httpHdrRangePackInto(const HttpHdrRange * range, Packer * p);
/* other */
extern String httpHdrRangeBoundaryStr(clientHttpRequest * http);


/* Http Content Range Header Field */
extern void httpHdrContRangePackInto(const HttpHdrContRange * crange, Packer * p);

/* Http Header Tools */
extern void httpHeaderAddContRange(HttpHeader *, HttpHdrRangeSpec, squid_off_t);

/* Http Header */
extern void httpHeaderInitModule(void);
extern void httpHeaderInitMem(void);
extern void httpHeaderCleanModule(void);
/* parse/pack */
extern void httpHeaderPackInto(const HttpHeader * hdr, Packer * p);
/* field manipulation */
extern void httpHeaderPutCc(HttpHeader * hdr, const HttpHdrCc * cc);
extern void httpHeaderPutContRange(HttpHeader * hdr, const HttpHdrContRange * cr);
extern void httpHeaderPutRange(HttpHeader * hdr, const HttpHdrRange * range);
/* avoid using these low level routines */
extern void httpHeaderEntryPackInto(const HttpHeaderEntry * e, Packer * p);
/* store report about current header usage and other stats */
extern void httpHeaderStoreReport(StoreEntry * e,void* data);
extern void httpHdrMangleList(HttpHeader *, request_t *);

/* Http Reply */
extern void httpReplyInitModule(void);
/* create/destroy */
extern HttpReply *httpReplyCreate(void);
extern void httpReplyDestroy(HttpReply * rep);
/* reset: clean, then init */
extern void httpReplyReset(HttpReply * rep);
/* parse returns -1,0,+1 on error,need-more-data,success */
extern int httpReplyParse(HttpReply * rep, const char *buf, size_t);
extern void httpReplyPackInto(const HttpReply * rep, Packer * p);
/* ez-routines */
/* mem-pack: returns a ready to use mem buffer with a packed reply */
extern MemBuf httpReplyPack(const HttpReply * rep);
/* swap: create swap-based packer, pack, destroy packer and absorbs the reply if not the same as the object reply */
extern void httpReplySwapOut(HttpReply * rep, StoreEntry * e);
/* set commonly used info with one call */
extern void httpReplySetHeaders(HttpReply * rep, http_status status,
    const char *reason, const char *ctype, squid_off_t clen, time_t lmt, time_t expires);
/* do everything in one call: init, set, pack, clean, return MemBuf */
extern MemBuf httpPackedReply(http_version_t ver, http_status status, const char *ctype,
    squid_off_t clen, time_t lmt, time_t expires);
/* construct 304 reply and pack it into MemBuf, return MemBuf */
extern MemBuf httpPacked304Reply(const HttpReply * rep, int http11);
/* update when 304 reply is received for a cached object */
extern void httpReplyUpdateOnNotModified(HttpReply * rep, HttpReply * freshRep);
/* header manipulation */
extern int httpReplyContentLen(const HttpReply * rep);
extern const char *httpReplyContentType(const HttpReply * rep);
extern time_t httpReplyExpires(const HttpReply * rep);
extern int httpReplyHasCc(const HttpReply * rep, http_hdr_cc_type type);
extern void httpRedirectReply(HttpReply *, http_status, const char *);
extern squid_off_t httpReplyBodySize(method_t *, const HttpReply *);
extern squid_off_t httpDelayBodySize(method_t *, const HttpReply *);
extern HttpReply *httpReplyClone(HttpReply * src);

/* Http Request */
extern void requestInitMem(void);
extern request_t *requestCreate(method_t *, protocol_t, const char *urlpath);
extern void requestDestroy(request_t *);
extern request_t *requestLink(request_t *);
extern void requestUnlink(request_t *);
extern void httpRequestSwapOut(const request_t * req, StoreEntry * e);
extern void httpRequestPackDebug(request_t * req, Packer * p);
extern int httpRequestPrefixLen(const request_t * req);
extern int httpRequestHdrAllowed(const HttpHeaderEntry * e, String * strConnection);
extern int httpRequestHdrAllowedByName(http_hdr_type id);
extern void requestReadBody(request_t * request, char *buf, size_t size, CBCB * callback, void *cbdata);
extern void requestAbortBody(request_t * request);

extern void *icpCreateMessage(icp_opcode opcode,
    int flags,
    const char *url,
    int reqnum,
    int pad);
extern int icpUdpSend(int, const struct sockaddr_in *, icp_common_t *, log_type, int);
extern PF icpHandleUdp;
extern PF icpUdpSendQueue;
extern PF httpAccept;

#ifdef SQUID_SNMP
typedef enum {atNone = 0, atSum, atAverage, atMax, atMin} AggrType;
extern PF snmpHandleUdp;
extern void snmpInit(void);
extern void snmpConnectionOpen(void);
extern void snmpConnectionShutdown(void);
extern void snmpConnectionClose(void);
extern void snmpDebugAnswer(variable_list* Answer);
extern void snmpDebugOid(int lvl, oid * Name, snint Len);
extern AggrType snmpAggrType(oid* Current, snint CurrentLen);
extern struct snmp_pdu * snmpAgentResponse(struct snmp_pdu *PDU);
extern void addr2oid(struct in_addr addr, oid * Dest);
extern struct in_addr *oid2addr(oid * id);
extern struct in_addr *client_entry(struct in_addr *current);
extern variable_list *snmp_basicFn(variable_list *, snint *);
extern variable_list *snmp_confFn(variable_list *, snint *);
extern variable_list *snmp_sysFn(variable_list *, snint *);
extern variable_list *snmp_prfSysFn(variable_list *, snint *);
extern variable_list *snmp_prfProtoFn(variable_list *, snint *);
extern variable_list *snmp_prfPeerFn(variable_list *, snint *);
extern variable_list *snmp_netIpFn(variable_list *, snint *);
extern variable_list *snmp_netFqdnFn(variable_list *, snint *);
extern variable_list *snmp_netIdnsFn(variable_list *, snint *);
extern variable_list *snmp_meshPtblFn(variable_list *, snint *);
extern variable_list *snmp_meshCtblFn(variable_list *, snint *);
#endif /* SQUID_SNMP */

#if USE_WCCP
extern void wccpInit(void);
extern void wccpConnectionOpen(void);
extern void wccpConnectionClose(void);
#endif /* USE_WCCP */

#if USE_WCCPv2
extern void wccp2Init(void);
extern void wccp2ConnectionOpen(void);
extern void wccp2ConnectionClose(void);
#endif /* USE_WCCPv2 */

extern void icpHandleIcpV3(int, struct sockaddr_in, char *, int);
extern int icpCheckUdpHit(StoreEntry *, request_t * request);
extern void icpConnectionsOpen(void);
extern void icpConnectionShutdown(void);
extern void icpConnectionClose(void);
extern int icpSetCacheKey(const cache_key * key);
extern const cache_key *icpGetCacheKey(const char *url, int reqnum);

extern void ipcache_local_params(void);
extern void ipcache_init_local(void);
extern void stat_ipcache_get(StoreEntry *,void* data);

extern char *mime_get_header(const char *mime, const char *header);
extern char *mime_get_header_field(const char *mime, const char *name, const char *prefix);
extern const char *mime_get_auth(const char *hdr, const char *auth_scheme, const char **auth_field);

extern void mimeInit(char *filename);
extern void mimeFreeMemory(void);
extern char *mimeGetContentEncoding(const char *fn);
extern char *mimeGetContentType(const char *fn);
extern char *mimeGetIcon(const char *fn);
extern const char *mimeGetIconURL(const char *fn);
extern char mimeGetTransferMode(const char *fn);
extern int mimeGetDownloadOption(const char *fn);
extern int mimeGetViewOption(const char *fn);

extern int mcastSetTtl(int, int);
extern IPH mcastJoinGroups;

/* Labels for hierachical log file */
/* put them all here for easier reference when writing a logfile analyzer */


extern peer *getFirstPeer(void);
extern peer *getFirstUpParent(request_t *);
extern peer *getNextPeer(peer *);
extern peer *getSingleParent(request_t *);
extern int neighborsCount(request_t *);
extern int neighborsUdpPing(request_t *,
    StoreEntry *,
    IRCB * callback,
    void *data,
    int *exprep,
    int *timeout);
extern void neighborAddAcl(const char *, const char *);
extern void neighborsUdpAck(const cache_key *, icp_common_t *, const struct sockaddr_in *);
extern void neighborAdd(const char *, const char *, int, int, int, int, int);
extern void neighbors_init(void);
#if USE_HTCP
extern void neighborsHtcpClear(StoreEntry *, const char *, request_t *, method_t *, htcp_clr_reason);
#endif
extern peer *peerFindByName(const char *);
extern peer *peerFindByNameAndPort(const char *, unsigned short);
extern peer *getDefaultParent(request_t * request);
extern peer *getRoundRobinParent(request_t * request);
EVH peerClearRRLoop;
extern void peerClearRR(void);
extern peer *getAnyParent(request_t * request);
extern lookup_t peerDigestLookup(peer * p, request_t * request);
extern peer *neighborsDigestSelect(request_t * request);
extern void peerNoteDigestLookup(request_t * request, peer * p, lookup_t lookup);
extern void peerNoteDigestGone(peer * p);
extern int neighborUp(const peer * e);
extern CBDUNL peerDestroy;
extern const char *neighborTypeStr(const peer * e);
extern peer_t neighborType(const peer *, const request_t *);
extern void peerConnectFailed(peer *);
extern void peerConnectSucceded(peer *);
extern void dump_peer_options(StoreEntry *, peer *);
extern int peerHTTPOkay(const peer *, request_t *);
extern peer *whichPeer(const struct sockaddr_in *from);
#if USE_HTCP
extern void neighborsHtcpReply(const cache_key *, htcpReplyData *, const struct sockaddr_in *);
#endif
extern void peerAddFwdServer(FwdServer ** FS, peer * p, hier_code code);
extern int peerAllowedToUse(const peer *, request_t *);

extern void netdbInitMem(void);
extern void netdbInit(void);
extern void netdbHandlePingReply(const struct sockaddr_in *from, int hops, int rtt);
extern void netdbPingSite(const char *hostname);
extern int netdbHops(struct in_addr);
extern void netdbFreeMemory(void);
extern int netdbHostHops(const char *host);
extern int netdbHostRtt(const char *host);
extern void netdbUpdatePeer(request_t *, peer * e, int rtt, int hops);
extern void netdbDeleteAddrNetwork(struct in_addr addr);
extern void netdbBinaryExchange(StoreEntry *);
extern EVH netdbExchangeStart;
extern void netdbExchangeUpdatePeer(struct in_addr, peer *, double, double);
extern peer *netdbClosestParent(request_t *);
extern void netdbHostData(const char *host, int *samp, int *rtt, int *hops);

extern void cachemgrStart(int fd, request_t * request, StoreEntry * entry);
extern void cachemgrRegister(const char *, const char *, OBJH *, ADD*, COL*, int, int, int);

extern void cachemgrInit(void);

extern void peerSelect(request_t *, StoreEntry *, PSC *, void *data);
extern void peerSelectInit(void);

/* peer_digest.c */
extern void peerDigestInitMem(void);
extern PeerDigest *peerDigestCreate(peer * p);
extern void peerDigestNeeded(PeerDigest * pd);
extern void peerDigestNotePeerGone(PeerDigest * pd);
extern void peerDigestStatsReport(const PeerDigest * pd, StoreEntry * e);

/* forward.c */
extern void fwdStart(int, StoreEntry *, request_t *);
extern void fwdStartPeer(peer *, StoreEntry *, request_t *);
extern DEFER fwdCheckDeferRead;
extern void fwdFail(FwdState *, ErrorState *);
extern void fwdUnregister(int fd, FwdState *);
extern void fwdComplete(FwdState * fwdState);
extern void fwdInitMem(void);
extern void fwdInit(void);
extern int fwdReforwardableStatus(http_status s);
extern void fwdServersFree(FwdServer ** FS);
#if WIP_FWD_LOG
extern void fwdUninit(void);
extern void fwdLogRotate(void);
extern void fwdStatus(FwdState *, http_status);
#endif
struct in_addr getOutgoingAddr(request_t * request);
unsigned long getOutgoingTOS(request_t * request);

extern void urnStart(request_t *, StoreEntry *);

extern void redirectStart(clientHttpRequest *, RH *, void *);
extern void redirectInit(void);
extern void redirectShutdown(void);

extern void storeurlStart(clientHttpRequest *, RH *, void *);
extern void storeurlInit(void);
extern void storeurlShutdown(void);

extern void locationRewriteStart(HttpReply *, clientHttpRequest *, RH *, void *);
extern void locationRewriteInit(void);
extern void locationRewriteShutdown(void);

/* auth_modules.c */
extern void authSchemeSetup(void);

/* authenticate.c */
extern void authenticateAuthUserMerge(auth_user_t *, auth_user_t *);
extern auth_user_t *authenticateAuthUserNew(const char *);
extern int authenticateAuthSchemeId(const char *typestr);
extern void authenticateStart(auth_user_request_t *, RH *, void *);
extern void authenticateSchemeInit(void);
extern void authenticateConfigure(authConfig *);
extern void authenticateInitMem(void);
extern void authenticateInit(authConfig *);
extern void authenticateShutdown(void);
extern void authenticateFixHeader(HttpReply *, auth_user_request_t *, request_t *, int, int);
extern void authenticateAddTrailer(HttpReply *, auth_user_request_t *, request_t *, int);
extern auth_acl_t authenticateTryToAuthenticateAndSetAuthUser(auth_user_request_t **, http_hdr_type, request_t *, ConnStateData *, struct in_addr);
extern void authenticateAuthUserUnlock(auth_user_t * auth_user);
extern void authenticateAuthUserLock(auth_user_t * auth_user);
extern void authenticateAuthUserRequestUnlock(auth_user_request_t *);
extern void authenticateAuthUserRequestLock(auth_user_request_t *);
extern char *authenticateAuthUserRequestMessage(auth_user_request_t *);
extern int authenticateAuthUserInuse(auth_user_t * auth_user);
extern void authenticateAuthUserRequestRemoveIp(auth_user_request_t *, struct in_addr);
extern void authenticateAuthUserRequestClearIp(auth_user_request_t *);
extern int authenticateAuthUserRequestIPCount(auth_user_request_t *);
extern int authenticateDirection(auth_user_request_t *);
extern FREE authenticateFreeProxyAuthUser;
extern void authenticateFreeProxyAuthUserACLResults(void *data);
extern void authenticateProxyUserCacheCleanup(void *);
extern void authenticateInitUserCache(void);
extern int authenticateActiveSchemeCount(void);
extern int authenticateSchemeCount(void);
extern void authenticateUserNameCacheAdd(auth_user_t * auth_user);
extern int authenticateCheckAuthUserIP(struct in_addr request_src_addr, auth_user_request_t * auth_user);
extern int authenticateUserAuthenticated(auth_user_request_t *);
extern void authenticateUserCacheRestart(void);
extern char *authenticateUserUsername(auth_user_t *);
extern char *authenticateUserRequestUsername(auth_user_request_t *);
extern int authenticateValidateUser(auth_user_request_t *);
extern void authenticateOnCloseConnection(ConnStateData * conn);
extern void authSchemeAdd(const char *type, AUTHSSETUP * setup);

extern void refreshAddToList(const char *, int, time_t, int, time_t);
extern int refreshIsCachable(const StoreEntry *);
extern int refreshCheckHTTP(const StoreEntry *, request_t *);
extern int refreshCheckHTTPStale(const StoreEntry *, request_t *);
extern int refreshCheckStaleOK(const StoreEntry *, request_t *);
extern int refreshCheckICP(const StoreEntry *, request_t *);
extern int refreshCheckHTCP(const StoreEntry *, request_t *);
extern int refreshCheckDigest(const StoreEntry *, time_t delta);
extern refresh_cc refreshCC(const StoreEntry *, request_t *);
extern time_t getMaxAge(const char *url);
extern void refreshInit(void);
extern const refresh_t *refreshLimits(const char *url);

extern void serverConnectionsClose(void);
extern void shut_down(int);
extern void rotate_logs(int);
extern void reconfigure(int);


extern void start_announce(void *unused);
extern void sslStart(clientHttpRequest *, squid_off_t *, int *);

extern void statInit(void);
extern void statFreeMemory(void);
extern double median_svc_get(int, int);
extern int stat5minClientRequests(void);
extern double stat5minCPUUsage(void);
extern const char *storeEntryFlags(const StoreEntry *);
extern double statRequestHitRatio(int minutes);
extern double statRequestHitMemoryRatio(int minutes);
extern double statRequestHitDiskRatio(int minutes);
extern double statByteHitRatio(int minutes);
extern int storeEntryLocked(const StoreEntry *);


/* StatHist */
extern void statHistDump(const StatHist * H, StoreEntry * sentry, StatHistBinDumper * bd);
extern StatHistBinDumper statHistEnumDumper;
extern StatHistBinDumper statHistIntDumper;

/* MemMeter */

/* mem */
extern void memInit(void);
extern void memInitModule(void);
extern void memClean(void);
extern void memCleanModule(void);

/* MemPool */

/* Mem */
extern void memReport(StoreEntry * e);

/* ----------------------------------------------------------------- */

/*
 * store.c
 */
extern StoreEntry *new_StoreEntry(int, const char *);
extern void storeEntrySetStoreUrl(StoreEntry * e, const char *store_url);
extern StoreEntry *storeGet(const cache_key *);
extern StoreEntry *storeGetPublic(const char *uri, const method_t * method);
extern StoreEntry *storeGetPublicByCode(const char *uri, const method_code_t code);
extern StoreEntry *storeGetPublicByRequest(request_t * request);
extern StoreEntry *storeGetPublicByRequestMethod(request_t * request, const method_t * method);
extern StoreEntry *storeGetPublicByRequestMethodCode(request_t * request, const method_code_t code);
extern StoreEntry *storeCreateEntry(const char *, request_flags, method_t *);
extern void storeSetPublicKey(StoreEntry *);
extern void storeComplete(StoreEntry *);
extern void storeInitMem(void);
extern void storeRequestFailed(StoreEntry *, ErrorState * err);

extern void storeInit(void);
extern void storeAbort(StoreEntry *);
extern void storeAppend(StoreEntry *, const char *, int);
extern void storeLockObjectDebug(StoreEntry *, const char *file, const int line);
extern void storeRelease(StoreEntry *);
extern void storePurgeEntriesByUrl(request_t * req, const char *url);
extern int storeUnlockObjectDebug(StoreEntry *, const char *file, const int line);
extern const char *storeLookupUrl(const StoreEntry * e);
#define	storeLockObject(a) storeLockObjectDebug(a, __FILE__, __LINE__);
#define	storeUnlockObject(a) storeUnlockObjectDebug(a, __FILE__, __LINE__);
extern EVH storeMaintainSwapSpace;
extern void storeExpireNow(StoreEntry *);
extern void storeReleaseRequest(StoreEntry *);
extern void storeConfigure(void);
extern void storeNegativeCache(StoreEntry *);
extern void storeFreeMemory(void);
extern int expiresMoreThan(time_t, time_t);
extern int storeEntryValidToSend(StoreEntry *);
extern void storeTimestampsSet(StoreEntry *);
extern void storeRegisterAbort(StoreEntry * e, STABH * cb, void *);
extern void storeUnregisterAbort(StoreEntry * e);
extern void storeMemObjectDump(MemObject * mem);
extern void storeEntryDump(const StoreEntry * e, int debug_lvl);
extern const char *storeUrl(const StoreEntry *);
extern void storeCreateMemObject(StoreEntry *, const char *);
extern void storeCopyNotModifiedReplyHeaders(MemObject * O, MemObject * N);
extern void storeBuffer(StoreEntry *);
extern void storeBufferFlush(StoreEntry *);
extern void storeHashInsert(StoreEntry * e, const cache_key *);
extern void storeSetMemStatus(StoreEntry * e, int);
#if STDC_HEADERS
extern void
storeAppendPrintf(StoreEntry *, const char *,...) PRINTF_FORMAT_ARG2;
#else
extern void storeAppendPrintf();
#endif
extern void storeAppendVPrintf(StoreEntry *, const char *, va_list ap);
extern int storeCheckCachable(StoreEntry * e);
extern void storeSetPrivateKey(StoreEntry *);
extern squid_off_t objectLen(const StoreEntry * e);
extern squid_off_t contentLen(const StoreEntry * e);
extern HttpReply *storeEntryReply(StoreEntry *);
extern int storeTooManyDiskFilesOpen(void);
extern void storeEntryReset(StoreEntry *);
extern void storeHeapPositionUpdate(StoreEntry *, SwapDir *);
extern void storeSwapFileNumberSet(StoreEntry * e, sfileno filn);
extern void storeFsInit(void);
extern void storeFsDone(void);
extern void storeFsAdd(const char *, STSETUP *);
extern void storeReplAdd(const char *, REMOVALPOLICYCREATE *);
void storeDeferRead(StoreEntry *, int fd);
void storeResumeRead(StoreEntry *);
void storeResetDefer(StoreEntry *);
extern int memHaveHeaders(const MemObject * mem);
extern void storeUpdate(StoreEntry * e, request_t *);


/* store_modules.c */
extern void storeFsSetup(void);

/* repl_modules.c */
extern void storeReplSetup(void);

/* store_io.c */
extern storeIOState * storeIOAllocate(FREE *state_free);
extern storeIOState *storeCreate(StoreEntry *, STFNCB *, STIOCB *, void *);
extern storeIOState *storeOpen(StoreEntry *, STFNCB *, STIOCB *, void *);
extern void storeClose(storeIOState *);
extern void storeRead(storeIOState *, char *, size_t, squid_off_t, STRCB *, void *);
extern void storeWrite(storeIOState *, char *, size_t, FREE *);
extern void storeUnlink(StoreEntry *);
extern void storeRecycle(StoreEntry *);
extern squid_off_t storeOffset(storeIOState *);

/*
 * store_log.c
 */
extern void storeLog(int tag, const StoreEntry * e);
extern void storeLogRotate(void);
extern void storeLogClose(void);
extern void storeLogOpen(void);


/*
 * store_key_*.c
 */
extern cache_key *storeKeyDup(const cache_key *);
extern cache_key *storeKeyCopy(cache_key *, const cache_key *);
extern void storeKeyFree(const cache_key *);
extern const cache_key *storeKeyScan(const char *);
extern const char *storeKeyText(const cache_key *);
extern const cache_key *storeKeyPublic(const char *, const method_t *);
extern const cache_key *storeKeyPublicByRequest(request_t *);
extern const cache_key *storeKeyPublicByRequestMethod(request_t *, const method_t *);
extern const cache_key *storeKeyPrivate(const char *, method_t *, int);
extern int storeKeyHashBuckets(int);
extern int storeKeyNull(const cache_key *);
extern void storeKeyInit(void);
extern HASHHASH storeKeyHashHash;
extern HASHCMP storeKeyHashCmp;

/*
 * store_digest.c
 */
extern void storeDigestInit(void);
extern void storeDigestNoteStoreReady(void);
extern void storeDigestScheduleRebuild(void);
extern void storeDigestDel(const StoreEntry * entry);
extern void storeDigestReport(StoreEntry *, void *);

/*
 * store_dir.c
 */
extern OBJH storeDirStats;
extern char *storeDirSwapLogFile(int, const char *);
extern char *storeSwapDir(int);
extern char *storeSwapFullPath(int, char *);
extern char *storeSwapSubSubDir(int, char *);
extern const char *storeSwapPath(int);
extern int storeDirWriteCleanLogs(int reopen);
extern STDIRSELECT *storeDirSelectSwapDir;
extern void storeCreateSwapDirectories(void);
extern void storeDirCloseSwapLogs(void);
extern void storeDirCloseTmpSwapLog(int dirn);
extern void storeDirConfigure(void);
extern void storeDirDiskFull(sdirno);
extern void storeDirInit(void);
extern void storeDirOpenSwapLogs(void);
extern void storeDirSwapLog(const StoreEntry *, int op);
extern void storeDirUpdateSwapSize(SwapDir *, squid_off_t size, int sign);
extern void storeDirSync(void);
extern void storeDirCallback(void);
extern void storeDirLRUDelete(StoreEntry *);
extern void storeDirLRUAdd(StoreEntry *);
extern int storeDirGetBlkSize(const char *path, int *blksize);

#ifdef HAVE_STATVFS
extern int storeDirGetUFSStats(const char *, fsblkcnt_t *, fsblkcnt_t *, fsfilcnt_t *, fsfilcnt_t *);
#else
extern int storeDirGetUFSStats(const char *, int *, int *, int *, int *);
#endif


/*
 * store_swapmeta.c
 */
extern tlv *storeSwapMetaBuild(StoreEntry * e);
extern char * storeSwapMetaAssemble(StoreEntry *e, int *length);

/*
 * store_rebuild.c
 */
extern void storeRebuildStart(void);
extern void storeRebuildComplete(struct _store_rebuild_data *);
extern void storeRebuildProgress(int sd_index, int total, int sofar);

/*
 * store_swapin.c
 */
extern void storeSwapInStart(store_client *);

/*
 * store_swapout.c
 */
extern void storeSwapOut(StoreEntry * e);
extern void storeSwapOutFileClose(StoreEntry * e);
extern int /* swapout_able */ storeSwapOutMaintainMemObject(StoreEntry * e);
extern squid_off_t storeSwapOutObjectBytesOnDisk(const MemObject * mem);

/*
 * store_client.c
 */
extern store_client *storeClientRegister(StoreEntry * e, void *data);
extern void storeClientRef(store_client *, StoreEntry *, squid_off_t, squid_off_t, size_t, STNCB *, void *);
extern void storeClientCopyHeaders(store_client *, StoreEntry *, STHCB *, void *);
extern int storeClientCopyPending(store_client *, StoreEntry * e, void *data);
extern int storeClientUnregister(store_client * sc, StoreEntry * e, void *data);
extern squid_off_t storeLowestMemReaderOffset(const StoreEntry * entry);
extern void InvokeHandlers(StoreEntry * e);
extern int storePendingNClients(const StoreEntry * e);


extern const char *getMyHostname(void);
extern const char *uniqueHostname(void);
extern void safeunlink(const char *path, int quiet);
extern void death(int sig);
extern void fatal(const char *message);
#if STDC_HEADERS
extern void
fatalf(const char *fmt,...) PRINTF_FORMAT_ARG1;
#else
extern void fatalf();
#endif
extern void fatalvf(const char *fmt, va_list args); 
extern void fatal_dump(const char *message);
extern void sigusr2_handle(int sig);
extern void sig_child(int sig);
extern void enableCoredumps(void);
extern void leave_suid(void);
extern void enter_suid(void);
extern void no_suid(void);
extern void writePidFile(void);
extern void setSocketShutdownLifetimes(int);
extern void setMaxFD(void);
extern void setSystemLimits(void);
extern pid_t readPidFile(void);
extern struct in_addr inaddrFromHostent(const struct hostent *hp);
extern void debug_trap(const char *);
extern const char *checkNullString(const char *p);
extern void squid_getrusage(struct rusage *r);
extern double rusage_cputime(struct rusage *r);
extern int rusage_maxrss(struct rusage *r);
extern int rusage_pagefaults(struct rusage *r);
extern void releaseServerSockets(void);
extern void PrintRusage(void);
extern void dumpMallocStats(void);

#if USE_UNLINKD
extern void unlinkdInit(void);
extern void unlinkdClose(void);
extern void unlinkdUnlink(const char *);
#endif

/* url.c */
extern char *url_escape(const char *url);
extern void urlInitialize(void);
extern request_t *urlParse(method_t *, char *);
extern const char *urlCanonical(request_t *);
extern char *urlMakeAbsolute(request_t *, const char *);
extern char *urlRInternal(const char *host, u_short port, const char *dir, const char *name);
extern char *urlInternal(const char *dir, const char *name);
extern int urlCheckRequest(const request_t *);
extern char *urlCanonicalClean(const request_t *);

extern void useragentOpenLog(void);
extern void useragentRotateLog(void);
extern void logUserAgent(const char *, const char *);
extern void useragentLogClose(void);
extern void refererOpenLog(void);
extern void refererRotateLog(void);
extern void logReferer(const char *, const char *, const char *);
extern void refererCloseLog(void);
extern peer_t parseNeighborType(const char *s);

extern void errorInitialize(void);
extern void errorClean(void);
extern HttpReply *errorBuildReply(ErrorState * err);
extern void errorSend(int fd, ErrorState *);
extern void errorAppendEntry(StoreEntry *, ErrorState *);
extern void errorStateFree(ErrorState * err);
extern int errorReservePageId(const char *page_name);
extern ErrorState *errorCon(err_type type, http_status, request_t * request);
extern int errorPageId(const char *page_name);

extern int asnMatchIp(void *, sqaddr_t *);
extern int asnMatchIp4(void *, struct in_addr); 
extern void asnInit(void);
extern void asnFreeMemory(void);

/* tools.c */
extern dlink_node *dlinkNodeNew(void);

extern int stringHasCntl(const char *);
extern void linklistPush(link_list **, void *);
extern void *linklistShift(link_list **);
extern int xrename(const char *from, const char *to);
extern int isPowTen(int);
extern void parseEtcHosts(void);
extern int getMyPort(void);
extern int parse_sockaddr(char *s, struct sockaddr_in *addr);

char *strwordtok(char *buf, char **t);
void strwordquote(MemBuf * mb, const char *str);

void setUmask(mode_t mask);
void keepCapabilities(void);

#if USE_HTCP
extern void htcpInit(void);
extern void htcpQuery(StoreEntry * e, request_t * req, peer * p);
extern void htcpClear(StoreEntry * e, const char *uri, request_t * req, method_t *, peer * p, htcp_clr_reason reason);
extern void htcpSocketShutdown(void);
extern void htcpSocketClose(void);
#endif

/* String */

/* CacheDigest */
extern CacheDigest *cacheDigestCreate(int capacity, int bpe);
extern void cacheDigestDestroy(CacheDigest * cd);
extern CacheDigest *cacheDigestClone(const CacheDigest * cd);
extern void cacheDigestClear(CacheDigest * cd);
extern void cacheDigestChangeCap(CacheDigest * cd, int new_cap);
extern int cacheDigestTest(const CacheDigest * cd, const cache_key * key);
extern void cacheDigestAdd(CacheDigest * cd, const cache_key * key);
extern void cacheDigestDel(CacheDigest * cd, const cache_key * key);
extern size_t cacheDigestCalcMaskSize(int cap, int bpe);
extern int cacheDigestBitUtil(const CacheDigest * cd);
extern void cacheDigestGuessStatsUpdate(cd_guess_stats * stats, int real_hit, int guess_hit);
extern void cacheDigestGuessStatsReport(const cd_guess_stats * stats, StoreEntry * sentry, const char *label);
extern void cacheDigestReport(CacheDigest * cd, const char *label, StoreEntry * e);

extern void internalStart(request_t *, StoreEntry *);
extern int internalCheck(const char *urlpath);
extern int internalStaticCheck(const char *urlpath);
extern char *internalLocalUri(const char *dir, const char *name);
extern char *internalStoreUri(const char *dir, const char *name);
extern char *internalRemoteUri(const char *, u_short, const char *, const char *);
extern const char *internalHostname(void);
extern int internalHostnameIs(const char *);

extern void carpInit(void);
extern peer *carpSelectParent(request_t *);

extern void peerUserHashInit(void);
extern peer *peerUserHashSelectParent(request_t *);
extern void peerSourceHashInit(void);
extern peer *peerSourceHashSelectParent(request_t *);

#if DELAY_POOLS
extern void delayPoolsInit(void);
extern void delayInitDelayData(unsigned short pools);
extern void delayFreeDelayData(unsigned short pools);
extern void delayCreateDelayPool(unsigned short pool, u_char class);
extern void delayInitDelayPool(unsigned short pool, u_char class, delaySpecSet * rates);
extern void delayFreeDelayPool(unsigned short pool);
extern void delayPoolsReconfigure(void);
extern void delaySetNoDelay(int fd);
extern void delayClearNoDelay(int fd);
extern int delayIsNoDelay(int fd);
extern delay_id delayClient(clientHttpRequest *);
extern delay_id delayPoolClient(unsigned short pool, in_addr_t client);
extern EVH delayPoolsUpdate;
extern int delayBytesWanted(delay_id d, int min, int max);
extern void delayBytesIn(delay_id, int qty);
extern int delayMostBytesWanted(const MemObject * mem, int max);
extern delay_id delayMostBytesAllowed(const MemObject * mem, size_t * bytes);
extern void delaySetStoreClient(store_client * sc, delay_id delay_id);
extern void delayRegisterDelayIdPtr(delay_id * loc);
extern void delayUnregisterDelayIdPtr(delay_id * loc);
#endif

/* helper.c */
extern void helperStats(StoreEntry * sentry, helper * hlp);
extern void helperStatefulStats(StoreEntry * sentry, statefulhelper * hlp);

#if USE_LEAKFINDER
extern void leakInit(void);
extern void *leakAddFL(void *, const char *, int);
extern void *leakTouchFL(void *, const char *, int);
extern void *leakFreeFL(void *, const char *, int);
#endif

/* logfile.c */
extern Logfile *logfileOpen(const char *path, size_t bufsz, int);
extern void logfileClose(Logfile * lf);
extern void logfileRotate(Logfile * lf);
extern void logfileWrite(Logfile * lf, char *buf, size_t len);
extern void logfileFlush(Logfile * lf);
extern void logfileLineStart(Logfile * lf);
extern void logfileLineEnd(Logfile * lf);
#if STDC_HEADERS
extern void
logfilePrintf(Logfile * lf, const char *fmt,...) PRINTF_FORMAT_ARG2;
#else
extern void logfilePrintf(va_alist);
#endif

/*
 * Removal Policies
 */
extern RemovalPolicy *createRemovalPolicy(RemovalPolicySettings * settings);

/*
 * prototypes for system functions missing from system includes
 */

#ifdef _SQUID_SOLARIS_
extern int getrusage(int, struct rusage *);
extern int getpagesize(void);
extern int gethostname(char *, int);
#endif

#if URL_CHECKSUM_DEBUG
extern unsigned int url_checksum(const char *url);
#endif

/*
 * hack to allow snmp access to the statistics counters
 */
extern StatCounters *snmpStatGet(int);

/* Cygwin & native Windows Port */
/* win32.c */
#ifdef _SQUID_WIN32_
extern int WIN32_Subsystem_Init(int *, char ***);
extern void WIN32_sendSignal(int);
extern void WIN32_Abort(int);
extern void WIN32_Exit(void);
extern void WIN32_SetServiceCommandLine(void);
extern void WIN32_InstallService(void);
extern void WIN32_RemoveService(void);
extern void WIN32_ExceptionHandlerInit(void);
extern int SquidMain(int, char **);
#ifdef _SQUID_MSWIN_
extern DWORD WIN32_IpAddrChangeMonitorInit();
#endif
#endif

/* external_acl.c */
extern void parse_externalAclHelper(external_acl **);
extern void dump_externalAclHelper(StoreEntry * sentry, const char *name, const external_acl *);
extern void free_externalAclHelper(external_acl **);
extern void aclParseExternal(void *curlist, const char *name);
extern void aclDestroyExternal(void **curlust);
extern int aclMatchExternal(void *dataptr, aclCheck_t * ch);
extern wordlist *aclDumpExternal(void *dataptr);
typedef void EAH(void *data, void *result);
extern void externalAclLookup(aclCheck_t * ch, void *acl_data, EAH * handler, void *data);
extern void externalAclConfigure(void);
extern void externalAclInit(void);
extern void externalAclShutdown(void);
extern int externalAclRequiresAuth(void *acl_data);
extern char *strtokFile(void);
const char *externalAclMessage(external_acl_entry * entry);


/* refresh_check.c */
extern void parse_refreshCheckHelper(refresh_check_helper **);
extern void dump_refreshCheckHelper(StoreEntry * sentry, const char *name, const refresh_check_helper *);
extern void free_refreshCheckHelper(refresh_check_helper **);
extern void refreshCheckSubmit(StoreEntry * entry, REFRESHCHECK * callback, void *data);
extern void refreshCheckInit(void);
extern void refreshCheckConfigure(void);
extern void refreshCheckShutdown(void);

#if USE_WCCPv2
extern void parse_wccp2_service(void *v);
extern void free_wccp2_service(void *v);
extern void dump_wccp2_service(StoreEntry * e, const char *label, void *v);
extern int check_null_wccp2_service(void *v);

extern void parse_wccp2_service_info(void *v);
extern void free_wccp2_service_info(void *v);
extern void dump_wccp2_service_info(StoreEntry * e, const char *label, void *v);
#endif

/* peer_monitor.c */
extern void peerMonitorInit(void);
extern void peerMonitorNow(peer *);

/* errormap.c */
extern void errorMapInit(void);
extern int errorMapStart(const errormap * map, request_t * req, HttpReply * reply, const char *aclname, ERRMAPCB * callback, void *data);

rewritetoken *rewriteURLCompile(const char *urlfmt);
char *internalRedirectProcessURL(clientHttpRequest * req, rewritetoken * head);

/* New HTTP message parsing support */
extern void HttpMsgBufInit(HttpMsgBuf * hmsg, const char *buf, size_t size);
extern void httpMsgBufDone(HttpMsgBuf * hmsg);
extern int httpMsgParseRequestLine(HttpMsgBuf * hmsg);
extern int httpMsgParseRequestHeader(request_t * req, HttpMsgBuf * hmsg);
extern int httpMsgFindHeadersEnd(HttpMsgBuf * hmsg);

/* client_side.c */
extern void clientHttpReplyAccessCheck(clientHttpRequest * http);	/* entry back into client_side.c from the location rewrite code */
extern aclCheck_t *clientAclChecklistCreate(const acl_access * acl, const clientHttpRequest * http);
extern void clientInterpretRequestHeaders(clientHttpRequest * http);
extern void clientAccessCheck2(void *data);
extern void clientFinishRewriteStuff(clientHttpRequest * http);
extern int connStateGetCount(void);
extern StoreEntry *clientCreateStoreEntry(clientHttpRequest *, method_t *, request_flags);
extern void clientProcessRequest(clientHttpRequest *);
extern void httpRequestFree(void *data);

/* client_side_nat.c */
extern int clientNatLookup(ConnStateData * conn);

/* client_side_redirect.c */
extern void clientRedirectStart(clientHttpRequest * http);

/* client_side_storeurl_rewrite.c */
extern void clientStoreURLRewriteStart(clientHttpRequest * http);

/* statIapp.c */
extern void statIappStats(StoreEntry *sentry, void* data);

/* comm.c */
extern void commConnectStart(int fd, const char *, u_short, CNCB *, void *, struct in_addr *addr);

/* client_side_location_rewrite.c */
extern void clientHttpLocationRewriteCheck(clientHttpRequest * http);

#endif /* SQUID_PROTOS_H */
