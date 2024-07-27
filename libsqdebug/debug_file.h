#ifndef	__LIBSQDEBUG_DEBUG_FILE_H__
#define	__LIBSQDEBUG_DEBUG_FILE_H__

extern FILE *debug_log;         /* NULL */
extern int opt_debug_rotate_count;
extern int opt_debug_buffered_logs;
extern char * opt_debug_log;

extern void _db_print_file(const char *format,...);
extern void _db_rotate_log(void);
extern void debugOpenLog(const char *logfile);
extern void logsFlush(void);
extern int debug_log_flush(void);

#endif
