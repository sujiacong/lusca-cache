#ifndef	__LIBSQDEBUG_CTX_H__
#define	__LIBSQDEBUG_CTX_H__

typedef int Ctx;
extern int Ctx_Lock;

extern void	ctx_print(void);
extern Ctx	ctx_enter(const char *descr);
extern void	ctx_exit(Ctx ctx);

#endif
