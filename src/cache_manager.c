
/*
 * $Id: cache_manager.c 14600 2010-04-16 04:39:40Z ajcorrea $
 *
 * DEBUG: section 16    Cache Manager Objects
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

#include "../libmutiprocess/ipcsupport.h"

extern void StartMgrForwarder(int fd, ActionParams *params, StoreEntry* entry);

#define MGR_PASSWD_SZ 128

typedef struct {
    StoreEntry *entry;
    char *action;
    char *user_name;
    char *passwd;
} cachemgrStateData;

static action_table *cachemgrFindAction(const char *action);
static cachemgrStateData *cachemgrParseUrl(const char *url, ActionParams* actionparams);
static void cachemgrParseHeaders(cachemgrStateData * mgr, const request_t * request);
static int cachemgrCheckPassword(cachemgrStateData *);
static void cachemgrStateFree(cachemgrStateData * mgr);
static char *cachemgrPasswdGet(cachemgr_passwd *, const char *);
static const char *cachemgrActionProtection(const action_table * at);
static OBJH cachemgrShutdown;
static OBJH cachemgrReconfigure;
static OBJH cachemgrMenu;
static OBJH cachemgrOfflineToggle;

action_table *ActionTable = NULL;

void
cachemgrRegister(const char *action, const char *desc, OBJH * handler, ADD* addfun, COL* collectfun, int pw_req_flag, int atomic, int aggregatable)
{
    action_table *a;
    action_table **A;
    if (cachemgrFindAction(action) != NULL) {
	debugs(16, 3, "cachemgrRegister: Duplicate '%s'", action);
	return;
    }
    a = xcalloc(1, sizeof(action_table));
    a->action = xstrdup(action);
    a->desc = xstrdup(desc);
    a->handler = handler;
	a->add = addfun;
	a->collect = collectfun;
    a->flags.pw_req = pw_req_flag;
    a->flags.atomic = atomic;
	a->flags.aggregatable = aggregatable;
    for (A = &ActionTable; *A; A = &(*A)->next);
    *A = a;
    debugs(16, 3, "cachemgrRegister: registered %s", action);
}

static action_table *
cachemgrFindAction(const char *action)
{
    action_table *a;
    for (a = ActionTable; a != NULL; a = a->next) {
	if (0 == strcmp(a->action, action))
	    return a;
    }
    return NULL;
}

static char* strdup_substr(const char* s, int start, int end)
{
	char *d;

	int len = strlen(s);
	
	if (s == NULL || end < start || start >= len)
		return NULL;

	assert(end < len);
	
	d = xmalloc(end + 1 - start);
	
	memcpy(d, s + start, end - start + 1);
	
	d[end - start + 1] = '\0';
	
	return d;	
}

static QueryParam*
cachemgrParseParam(const char* paramStr)
{
#define MAX_INT_PARAM_SIZE 20
    regmatch_t pmatch[3];
    regex_t intExpr;
    regcomp(&intExpr, "^([a-z][a-z0-9_]*)=([0-9]+((,[0-9]+))*)$", REG_EXTENDED | REG_ICASE);
    regex_t stringExpr;
    regcomp(&stringExpr, "^([a-z][a-z0-9_]*)=([^&= ]+)$", REG_EXTENDED | REG_ICASE);
    if (regexec(&intExpr, paramStr, 3, pmatch, 0) == 0) {

		QueryParam* result = xcalloc(1, sizeof(QueryParam));

		result->type = ptInt;

		result->key = strdup_substr(paramStr, pmatch[1].rm_so, pmatch[1].rm_eo);
		
		char* pos = (char*)paramStr;
		
        result->value.intvalue = xcalloc(MAX_INT_PARAM_SIZE, sizeof(int));

		result->size = MAX_INT_PARAM_SIZE;
		
        int n = pmatch[2].rm_so;
		int j = 0;
		int i;
        for (i = n; i < pmatch[2].rm_eo; ++i) {
            if (pos[i] == ',') {

				char* intvalue = strdup_substr(paramStr,n,i);
				
                result->value.intvalue[j] = atoi(intvalue);

				xfree(intvalue);
				
                n = i + 1;

				++j;

				if(j >= result->size)
				{
					result->value.intvalue = xrealloc(result->value.intvalue, result->size + MAX_INT_PARAM_SIZE);
				}

				result->len = j;
            }
        }
		
        if (n < pmatch[2].rm_eo)
        {
        	++j;
		
			char* intvalue = strdup_substr(paramStr,n,pmatch[2].rm_eo);

			result->value.intvalue[j] = atoi(intvalue);

			xfree(intvalue);

			result->len = j;
        }
		
    	regfree(&stringExpr);
	
    	regfree(&intExpr);
			
        return result;
    } 
	else if (regexec(&stringExpr, paramStr, 3, pmatch, 0) == 0) {
    
		QueryParam* result = xcalloc(1, sizeof(QueryParam));

		result->type = ptString;    
		
        result->key = strdup_substr(paramStr, pmatch[1].rm_so, pmatch[1].rm_eo);

		result->value.strvalue	= strdup_substr(paramStr, pmatch[2].rm_so, pmatch[2].rm_eo);

		result->len = strlen(result->key);

		result->size =  result->len + 1;
		
    	regfree(&stringExpr);
	
    	regfree(&intExpr);
			
        return result;
    }
	
    regfree(&stringExpr);
	
    regfree(&intExpr);
	
    return NULL;
}

int
cachemgrParseQueryParams(char* queryparams, QueryParams* presult)
{	
	 size_t len = strlen(queryparams);

    if (len != 0) {

		char* aParamsStr = queryparams;

        QueryParam* param = NULL;

		QueryParam** params = (QueryParam**) xcalloc(PARAMS_SIZE_MAX, sizeof(QueryParam*));
		
        size_t n = 0;

		int maxsize = PARAMS_SIZE_MAX;
		
		int j = 0;
		
		size_t i;
		
        for (i = n; i < len; ++i) {
			
            if (aParamsStr[i] == '&') {
	
				char* paramstr = strdup_substr(aParamsStr, n, i);

				param = cachemgrParseParam(paramstr);
					
                if (!param)
                {
                	xfree(paramstr);
                    return 0;
                }

				xfree(paramstr);
				
                params[j] = param;
				
                n = i + 1;

				++j;

				if(j > PARAMS_SIZE_MAX)
				{
					params = xrealloc(params,maxsize + PARAMS_SIZE_MAX);

					maxsize += PARAMS_SIZE_MAX;
				}
            }
			
        }
		
        if (n < len) {

			char* paramstr = strdup_substr(aParamsStr, n, len);
			
			param = cachemgrParseParam(paramstr);
			
            if (!param)
            {
            	xfree(paramstr);
                return 0;
            }

			xfree(paramstr);
			
			params[j] = param;

			++j;
        }

		presult->paramsize = j;

		presult->param = params;

		presult->maxsize = maxsize;

		return 1;
    }
	
    return 0;
}

static cachemgrStateData *
cachemgrParseUrl(const char *url, ActionParams* actionparams)
{
    int t;
    LOCAL_ARRAY(char, host, MAX_URL);
    LOCAL_ARRAY(char, request, MAX_URL);
    LOCAL_ARRAY(char, password, MAX_URL);
	LOCAL_ARRAY(char, params, MAX_URL);
    host[0] = 0;
    request[0] = 0;
    password[0] = 0;
    params[0] = 0;	
    action_table *a;
    cachemgrStateData *mgr = NULL;
    const char *prot;
    int pos = -1;
    int len = strlen(url);
	
    assert(len > 0);	
	
    t = sscanf(url, "cache_object://%[^/]/%[^@?]%n@%[^?]?%s", host, request, &pos, password, params);
    if (t < 3) {
        t = sscanf(url, "cache_object://%[^/]/%[^?]%n?%s", host, request, &pos, params);
    }
    if (t < 1) {
        t = sscanf(url, "http://%[^/]/squid-internal-mgr/%[^?]%n?%s", host, request, &pos, params);
    }
    if (t < 1) {
        t = sscanf(url, "https://%[^/]/squid-internal-mgr/%[^?]%n?%s", host, request, &pos, params);
    }	
    if (t < 2) {
        if (strncmp("cache_object://",url,15)==0)
            xstrncpy(request, "menu", MAX_URL);
        else
            xstrncpy(request, "index", MAX_URL);
#ifdef _SQUID_OS2_
	/*
	 * emx's sscanf insists of returning 2 because it sets request
	 * to null
	 */
    } else if (t == 2 && request[0] == '\0') {
    
        if (strncmp("cache_object://",url,15)==0)
            xstrncpy(request, "menu", MAX_URL);
        else
            xstrncpy(request, "index", MAX_URL);	
#endif
    }
    request[strcspn(request, "/")] = '\0';
    if ((a = cachemgrFindAction(request)) == NULL) {
	debugs(16, 1, "cachemgrParseUrl: action '%s' not found", request);
	return NULL;
    } else {
	prot = cachemgrActionProtection(a);
	if (!strcmp(prot, "disabled") || !strcmp(prot, "hidden")) {
	    debugs(16, 1, "cachemgrParseUrl: action '%s' is %s", request, prot);
	    return NULL;
	}
    }
    /* set absent entries to NULL so we can test if they are present later */
    mgr = xcalloc(1, sizeof(cachemgrStateData));
	
	cachemgrParseQueryParams(params, &actionparams->params);
	
    mgr->user_name = NULL;
    mgr->passwd = t == 3 ? xstrdup(password) : NULL;
    mgr->action = xstrdup(request);
	
	actionparams->url = (char*)url;
	
    return mgr;
}

static void
cachemgrParseHeaders(cachemgrStateData * mgr, const request_t * request)
{
    const char *basic_cookie;	/* base 64 _decoded_ user:passwd pair */
    const char *passwd_del;
    assert(mgr && request);
	
    basic_cookie = httpHeaderGetAuth(&request->header, HDR_AUTHORIZATION, "Basic");
    if (!basic_cookie)
	return;
    if (!(passwd_del = strchr(basic_cookie, ':'))) {
	debugs(16, 1, "cachemgrParseHeaders: unknown basic_cookie format '%s'", basic_cookie);
	return;
    }
    /* found user:password pair, reset old values */
    safe_free(mgr->user_name);
    safe_free(mgr->passwd);
    mgr->user_name = xstrdup(basic_cookie);
    mgr->user_name[passwd_del - basic_cookie] = '\0';
    mgr->passwd = xstrdup(passwd_del + 1);
    /* warning: this prints decoded password which maybe not what you want to do @?@ @?@ */
    debugs(16, 9, "cachemgrParseHeaders: got user: '%s' passwd: '%s'", mgr->user_name, mgr->passwd);
}

/*
 * return 0 if mgr->password is good
 */
static int
cachemgrCheckPassword(cachemgrStateData * mgr)
{
    char *pwd = cachemgrPasswdGet(Config.passwd_list, mgr->action);
    action_table *a = cachemgrFindAction(mgr->action);
    assert(a != NULL);
    if (pwd == NULL)
	return a->flags.pw_req;
    if (strcmp(pwd, "disable") == 0)
	return 1;
    if (strcmp(pwd, "none") == 0)
	return 0;
    if (!mgr->passwd)
	return 1;
    return strcmp(pwd, mgr->passwd);
}

static void
cachemgrStateFree(cachemgrStateData * mgr)
{
    safe_free(mgr->action);
    safe_free(mgr->user_name);
    safe_free(mgr->passwd);
    storeUnlockObject(mgr->entry);
    xfree(mgr);
}

void
cachemgrStart(int fd, request_t * request, StoreEntry * entry)
{
    cachemgrStateData *mgr = NULL;
    ErrorState *err = NULL;
    action_table *a;
	
	ActionParams action_params;
	memset(&action_params, 0, sizeof(ActionParams));
	
    debugs(16, 3, "cachemgrStart: '%s'", storeUrl(entry));
    if ((mgr = cachemgrParseUrl(storeUrl(entry), &action_params)) == NULL) {
	err = errorCon(ERR_INVALID_URL, HTTP_NOT_FOUND, request);
	err->url = xstrdup(storeUrl(entry));
	errorAppendEntry(entry, err);
	entry->expires = squid_curtime;
	return;
    }
    mgr->entry = entry;
    storeLockObject(entry);
    entry->expires = squid_curtime;
    debugs(16, 5, "CACHEMGR: %s requesting '%s'",
	fd_table[fd].ipaddrstr, mgr->action);
    /* get additional info from request headers */
    cachemgrParseHeaders(mgr, request);
    /* Check password */
    if (cachemgrCheckPassword(mgr) != 0) {
	/* build error message */
	ErrorState *err;
	HttpReply *rep;
	err = errorCon(ERR_CACHE_MGR_ACCESS_DENIED, HTTP_UNAUTHORIZED, request);
	/* warn if user specified incorrect password */
	if (mgr->passwd)
	    debugs(16, 1, "CACHEMGR: %s@%s: incorrect password for '%s'",
		mgr->user_name ? mgr->user_name : "<unknown>",
		fd_table[fd].ipaddrstr, mgr->action);
	else
	    debugs(16, 1, "CACHEMGR: %s@%s: password needed for '%s'",
		mgr->user_name ? mgr->user_name : "<unknown>",
		fd_table[fd].ipaddrstr, mgr->action);
	rep = errorBuildReply(err);
	errorStateFree(err);
	/*
	 * add Authenticate header, use 'action' as a realm because
	 * password depends on action
	 */
	httpHeaderPutAuth(&rep->header, "Basic", mgr->action);
	/* store the reply */
	httpReplySwapOut(rep, entry);
	entry->expires = squid_curtime;
	storeComplete(entry);
	cachemgrStateFree(mgr);
	return;
    }

	action_params.origin = (char*)httpHeaderGetStr(&request->header, HDR_ORIGIN);
		
    debugs(16, 1, "CACHEMGR: %s@%s requesting '%s'",
	mgr->user_name ? mgr->user_name : "<unknown>",
	fd_table[fd].ipaddrstr, mgr->action);

    if (UsingSmp() && IamWorkerProcess()) {
		
        // is client the right connection to pass here?
		action_params.action = mgr->action;	
		action_params.passwd = mgr->passwd;	
	    action_params.user_name = mgr->user_name;
	 	action_params.method = request->method->code;
		action_params.flags = request->flags;
		
        StartMgrForwarder(fd, &action_params, entry);
		
		cachemgrStateFree(mgr);
		
        return;
    }
	
    /* retrieve object requested */
    a = cachemgrFindAction(mgr->action);
	
    assert(a != NULL);
    storeBuffer(entry);
    {
	HttpReply *rep = entry->mem_obj->reply;
	/* prove there are no previous reply headers around */
	assert(0 == rep->sline.status);
	httpReplySetHeaders(rep, HTTP_OK, NULL, "text/plain", -1, -1, squid_curtime);
	httpReplySwapOut(rep, entry);
    }
    a->handler(entry,NULL);
    storeBufferFlush(entry);
    if (a->flags.atomic)
	storeComplete(entry);
    cachemgrStateFree(mgr);
}

static void
cachemgrShutdown(StoreEntry * entryunused, void* data)
{
    debugs(16, 0, "Shutdown by command.");
    shut_down(0);
}

static void
cachemgrReconfigure(StoreEntry * sentry, void* data)
{
    debugs(16, 0, "Reconfigure by command.");
    storeAppendPrintf(sentry, "Reconfiguring Squid Process ....");
    reconfigure(SIGHUP);
}

static void
cachemgrOfflineToggle(StoreEntry * sentry, void* data)
{
    Config.onoff.offline = !Config.onoff.offline;
    debugs(16, 0, "offline_mode now %s.",
	Config.onoff.offline ? "ON" : "OFF");
    storeAppendPrintf(sentry, "offline_mode is now %s\n",
	Config.onoff.offline ? "ON" : "OFF");
}

static const char *
cachemgrActionProtection(const action_table * at)
{
    char *pwd;
    assert(at);
    pwd = cachemgrPasswdGet(Config.passwd_list, at->action);
    if (!pwd)
	return at->flags.pw_req ? "hidden" : "public";
    if (!strcmp(pwd, "disable"))
	return "disabled";
    if (strcmp(pwd, "none") == 0)
	return "public";
    return "protected";
}

static void
cachemgrMenu(StoreEntry * sentry, void* data)
{
    action_table *a;
    for (a = ActionTable; a != NULL; a = a->next) {
	storeAppendPrintf(sentry, " %-22s\t%s\t%s\n",
	    a->action, a->desc, cachemgrActionProtection(a));
    }
}

static char *
cachemgrPasswdGet(cachemgr_passwd * a, const char *action)
{
    wordlist *w;
    while (a != NULL) {
	for (w = a->actions; w != NULL; w = w->next) {
	    if (0 == strcmp(w->key, action))
		return a->passwd;
	    if (0 == strcmp(w->key, "all"))
		return a->passwd;
	}
	a = a->next;
    }
    return NULL;
}
static void
cachemgrFlushIpcache(StoreEntry * sentry, void* data)
{
    debugs(16, 0, "IP cache flushed by cachemgr...");
    int removed;
    removed = ipcacheFlushAll();
    debugs(16, 0, " removed %d entries", removed);
    storeAppendPrintf(sentry, "IP cache flushed ...\n removed %d entries\n", removed);
}
static void
cachemgrFlushFqdn(StoreEntry * sentry, void* data)
{
    debugs(16, 0, "FQDN cache flushed by cachemgr...");
    int removed;
    removed = fqdncacheFlushAll();
    debugs(16, 0, " removed %d entries", removed);
    storeAppendPrintf(sentry, "FQDN cache flushed ...\n removed %d entries\n", removed);
}

void
cachemgrInit(void)
{
    cachemgrRegister("menu",
	"This Cachemanager Menu",
	cachemgrMenu, NULL, NULL, 0, 1, 0);
    cachemgrRegister("shutdown",
	"Shut Down the Squid Process",
	cachemgrShutdown, NULL, NULL, 1, 1, 0);
    cachemgrRegister("reconfigure",
	"Reconfigure the Squid Process",
	cachemgrReconfigure, NULL, NULL,  1, 1, 0);
    cachemgrRegister("offline_toggle",
	"Toggle offline_mode setting",
	cachemgrOfflineToggle, NULL, NULL,  1, 1, 0);
    cachemgrRegister("flushdns",
        "Flush ALL DNS Cache Entries",
        cachemgrFlushIpcache, NULL, NULL,  0, 1, 0);
    cachemgrRegister("flushfqdn",
        "Flush ALL FQDN Cache Entries",
        cachemgrFlushFqdn, NULL, NULL, 0, 1, 0);

}
