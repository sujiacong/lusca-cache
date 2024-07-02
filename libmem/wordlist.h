#ifndef	__LIBMEM_WORDLIST_H__
#define	__LIBMEM_WORDLIST_H__

typedef struct _wordlist wordlist;
struct _wordlist {
    char *key;
    wordlist *next;
};

extern char *wordlistAdd(wordlist **, const char *);
extern char * wordlistAddBuf(wordlist ** list, const char *buf, int len);
extern char * wordlistPopHead(wordlist **);
extern void wordlistAddWl(wordlist **, wordlist *);
extern void wordlistJoin(wordlist **, wordlist **);
extern wordlist *wordlistDup(const wordlist *);
extern void wordlistDestroy(wordlist **);


#endif
