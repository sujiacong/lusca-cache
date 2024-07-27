#ifndef	__LIBSQDEBUG_DEBUG_SYSLOG_H__
#define	__LIBSQDEBUG_DEBUG_SYSLOG_H__

extern int opt_syslog_enable;
extern int syslog_facility;     /* LOG_LOCAL4 */
extern void _db_print_syslog(const char *format,...);
extern void _db_set_syslog(const char *facility);

#endif
