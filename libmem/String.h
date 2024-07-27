#ifndef	__LIBMEM_STRING_H__
#define	__LIBMEM_STRING_H__

struct _String {
    /* never reference these directly! */
    unsigned short int size;    /* buffer size; 64K limit */
    unsigned short int len;     /* current length  */
    char *buf;
};

typedef struct _String String;

/* Code using these define's don't treat the buffer as a NUL-terminated C string */
/* XXX note - the -uses- of these calls don't assume C-string; the String code may not yet! */
#define strLen2(s)     ((/* const */ int)(s).len)
#define strBuf2(s)     ((const char*)(s).buf)
#define strCat(s,str)		stringAppend(&(s), (str), strlen(str))
#define	strCatStr(ds, ss)	stringAppend(&(ds), strBuf2(ss), strLen(ss))
static inline char stringGetCh(const String *s, int pos) { return strBuf2(*s)[pos]; }

#define strCmp(s,str)		strcmp(strBuf(s), (str))
#define strNCmp(s,str,n)	strncmp(strBuf(s), (str), (n))
#define strCaseCmp(s,str)	strcasecmp(strBuf(s), (str))
#define strNCaseCmp(s,str,n)	strncasecmp(strBuf(s), (str), (n))

extern int strNCmpNull(const String *s, const char *s2, int n);
extern void stringInit(String * s, const char *str);
extern void stringLimitInit(String * s, const char *str, int len);
extern String stringDup(const String * s);
extern void stringClean(String * s);
extern void stringReset(String * s, const char *str);
extern void stringAppend(String * s, const char *buf, int len);
extern char * stringDupToC(const String *s);
extern char * stringDupToCOffset(const String *s, int offset);
extern char * stringDupSubstrToC(const String *s, int len);
extern char * stringDupToCRange(const String *s, int start, int end);
extern int strChr(String *s, char c);
extern int strRChr(String *s, char c);


/*
 * These is okish, but the use case probably should be replaced with a strStr() later
 * on which maps to a zero-copy region reference.
 */
extern void strCut(String *s, int pos);

/*
 * These two functions return whether the string is set to some value, even if
 * its an empty string. A few Squid functions do if (strBuf(str)) to see if
 * something has set the string to a value; these functions replace them.
 */
#define	strIsNull(s)	( (s).buf == NULL )
#define	strIsNotNull(s)	( (s).buf != NULL )

/* These are legacy routines which may or may not expect NUL-termination or not */
#define strLen(s)     ((/* const */ int)(s).len)
#define strBuf(s)     ((const char*)(s).buf)

#define strStr(s,str) ((const char*)strstr(strBuf(s), (str)))  


/* extern void stringAppendf(String *s, const char *fmt, ...) PRINTF_FORMAT_ARG2; */


extern const String StringNull; /* { 0, 0, NULL } */

#endif
