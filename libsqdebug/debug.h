#ifndef __LIBSQDEBUG_DEBUG_H__
#define __LIBSQDEBUG_DEBUG_H__

#define MAX_DEBUG_SECTIONS 100

extern int debugLevels[MAX_DEBUG_SECTIONS];
extern int _db_level;
extern char * _debug_options;
extern int opt_debug_stderr;    /* -1 */
extern int KidIdentifier;
extern const char* GSkipBuildPrefix(const char* path);
extern void DumpData(unsigned char *payload, int len);

/* defined debug section limits */
#define MAX_DEBUG_SECTIONS 100

/* defined names for Debug Levels */
#define DBG_CRITICAL    0   /**< critical messages always shown when they occur */
#define DBG_IMPORTANT   1   /**< important messages always shown when their section is being checked */
/* levels 2-8 are still being discussed amongst the developers */
#define DBG_DATA    9   /**< output is a large data dump only necessary for advanced debugging */

#define DBG_PARSE_NOTE(x) (opt_parse_cfg_only?0:(x)) /**< output is always to be displayed on '-k parse' but at level-x normally. */

#define do_debug(SECTION, LEVEL) \
    ((_db_level = (LEVEL)) <= debugLevels[SECTION])
#define debugs(SECTION, LEVEL, args...) \
    !do_debug(SECTION, LEVEL) ? (void) 0 : _db_print(GSkipBuildPrefix(__FILE__),__LINE__,__FUNCTION__,args)

#define dumps(SECTION, LEVEL,payload,len) \
	if(do_debug(SECTION, LEVEL)) {\
    _db_print(GSkipBuildPrefix(__FILE__),__LINE__,__FUNCTION__,"DUMP PAYLOAD %s",#payload);\
	DumpData(payload,len);}
	
	

#if STDC_HEADERS
extern void
_db_print(const char *path, int line, const char* function, const char *format,...);
#else
extern void _db_print();
#endif

#if defined(NODEBUG)
#define assert(EX) ((void)0)
#elif STDC_HEADERS
#define assert(EX)  ((EX)?((void)0):xassert( # EX , __FILE__, __LINE__))
#else
#define assert(EX)  ((EX)?((void)0):xassert("EX", __FILE__, __LINE__))
#endif

extern void xassert(const char *, const char *, int);
extern void _db_init(const char *options);

typedef void DBG_MSG(const char *format,...);

extern void _db_register_handler(DBG_MSG *, int do_timestamp);
extern void _db_unregister_all(void);
extern void _db_set_stderr_debug(int value);
extern int _db_stderr_debug_opt(void);

extern void _db_init_log(const char *logfile);

#endif /* __LIBSQDEBUG_DEBUG_H__ */
