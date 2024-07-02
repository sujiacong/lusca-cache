#ifndef	__LIBMEM_STRLIST_H__
#define	__LIBMEM_STRLIST_H__

extern void strListAdd(String * str, const char *item, char del);
void strListAddStr(String * str, const char *item, int len, char del);
extern void strListAddUnique(String * str, const char *item, char del);
extern int strListIsMember(const String * str, const char *item, char del);
extern int strIsSubstr(const String * list, const char *s);
extern int strListGetItem(const String * str, char del, const char **item, int *ilen, const char **pos);

#endif
