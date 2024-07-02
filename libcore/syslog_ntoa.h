#ifndef	__LIBCORE_SYSLOG_NTOA_H__
#define	__LIBCORE_SYSLOG_NTOA_H__

#define PRIORITY_MASK (LOG_ERR | LOG_WARNING | LOG_NOTICE | LOG_INFO | LOG_DEBUG)

/* Define LOG_AUTHPRIV as LOG_AUTH on systems still using the old deprecated LOG_AUTH */
#if !defined(LOG_AUTHPRIV) && defined(LOG_AUTH)
#define LOG_AUTHPRIV LOG_AUTH
#endif

extern int	syslog_ntoa(const char *s);

#endif
