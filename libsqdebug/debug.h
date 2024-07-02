#ifndef __LIBSQDEBUG_DEBUG_H__
#define __LIBSQDEBUG_DEBUG_H__

#define MAX_DEBUG_SECTIONS 100

extern int debugLevels[MAX_DEBUG_SECTIONS];
extern int _db_level;
extern char * _debug_options;
extern int opt_debug_stderr;    /* -1 */

#define do_debug(SECTION, LEVEL) \
    ((_db_level = (LEVEL)) <= debugLevels[SECTION])
#define debug(SECTION, LEVEL) \
    !do_debug(SECTION, LEVEL) ? (void) 0 : _db_print

#if STDC_HEADERS
extern void
_db_print(const char *,...) PRINTF_FORMAT_ARG1;
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

typedef void DBG_MSG(const char *format, va_list args);

extern void _db_register_handler(DBG_MSG *, int do_timestamp);
extern void _db_unregister_all(void);
extern void _db_set_stderr_debug(int value);
extern int _db_stderr_debug_opt(void);

extern void _db_init_log(const char *logfile);

#endif /* __LIBSQDEBUG_DEBUG_H__ */
