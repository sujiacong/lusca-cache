#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../libcore/varargs.h"
#include "ctx.h"
#include "debug.h"

int Ctx_Lock = 0;

/*
 * Context-based Debugging
 *
 * Rationale
 * ---------
 * 
 * When you have a long nested processing sequence, it is often impossible
 * for low level routines to know in what larger context they operate. If a
 * routine coredumps, one can restore the context using debugger trace.
 * However, in many case you do not want to coredump, but just want to report
 * a potential problem. A report maybe useless out of problem context.
 * 
 * To solve this potential problem, use the following approach:
 * 
 * int
 * top_level_foo(const char *url)
 * {
 *      // define current context
 *      // note: we stack but do not dup ctx descriptions!
 *      Ctx ctx = ctx_enter(url);
 *      ...
 *      // go down; middle_level_bar will eventually call bottom_level_boo
 *      middle_level_bar(method, protocol);
 *      ...
 *      // exit, clean after yourself
 *      ctx_exit(ctx);
 * }
 * 
 * void
 * bottom_level_boo(int status, void *data)
 * {
 *      // detect exceptional condition, and simply report it, the context
 *      // information will be available somewhere close in the log file
 *      if (status == STRANGE_STATUS)
 *      debug(13, 6) ("DOS attack detected, data: %p\n", data);
 *      ...
 * }
 * 
 * Current implementation is extremely simple but still very handy. It has a
 * negligible overhead (descriptions are not duplicated).
 * 
 * When the _first_ debug message for a given context is printed, it is
 * prepended with the current context description. Context is printed with
 * the same debugging level as the original message.
 * 
 * Note that we do not print context every type you do ctx_enter(). This
 * approach would produce too many useless messages.  For the same reason, a
 * context description is printed at most _once_ even if you have 10
 * debugging messages within one context.
 * 
 * Contexts can be nested, of course. You must use ctx_enter() to enter a
 * context (push it onto stack).  It is probably safe to exit several nested
 * contexts at _once_ by calling ctx_exit() at the top level (this will pop
 * all context till current one). However, as in any stack, you cannot start
 * in the middle.
 * 
 * Analysis: 
 * i)   locate debugging message,
 * ii)  locate current context by going _upstream_ in your log file,
 * iii) hack away.
 *
 *
 * To-Do: 
 * -----
 *
 *       decide if we want to dup() descriptions (adds overhead) but allows to
 *       add printf()-style interface
 *
 * implementation:
 * ---------------
 *
 * descriptions for contexts over CTX_MAX_LEVEL limit are ignored, you probably
 * have a bug if your nesting goes that deep.
 */

#define CTX_MAX_LEVEL 255

/*
 * produce a warning when nesting reaches this level and then double
 * the level
 */
static int Ctx_Warn_Level = 32;
/* all descriptions has been printed up to this level */
static int Ctx_Reported_Level = -1;
/* descriptions are still valid or active up to this level */
static int Ctx_Valid_Level = -1;
/* current level, the number of nested ctx_enter() calls */
static int Ctx_Current_Level = -1;
/* saved descriptions (stack) */
static const char *Ctx_Descrs[CTX_MAX_LEVEL + 1];
/* "safe" get secription */
static const char *ctx_get_descr(Ctx ctx);


Ctx
ctx_enter(const char *descr)
{
    Ctx_Current_Level++;

    if (Ctx_Current_Level <= CTX_MAX_LEVEL)
	Ctx_Descrs[Ctx_Current_Level] = descr;

    if (Ctx_Current_Level == Ctx_Warn_Level) {
	debug(0, 0) ("# ctx: suspiciously deep (%d) nesting:\n", Ctx_Warn_Level);
	Ctx_Warn_Level *= 2;
    }
    return Ctx_Current_Level;
}

void
ctx_exit(Ctx ctx)
{
    assert(ctx >= 0);
    Ctx_Current_Level = (ctx >= 0) ? ctx - 1 : -1;
    if (Ctx_Valid_Level > Ctx_Current_Level)
	Ctx_Valid_Level = Ctx_Current_Level;
}

/*
 * the idea id to print each context description at most once but provide enough
 * info for deducing the current execution stack
 */
void
ctx_print(void)
{
    /* lock so _db_print will not call us recursively */
    Ctx_Lock++;
    /* ok, user saw [0,Ctx_Reported_Level] descriptions */
    /* first inform about entries popped since user saw them */
    if (Ctx_Valid_Level < Ctx_Reported_Level) {
	if (Ctx_Reported_Level != Ctx_Valid_Level + 1)
	    _db_print("ctx: exit levels from %2d down to %2d\n",
		Ctx_Reported_Level, Ctx_Valid_Level + 1);
	else
	    _db_print("ctx: exit level %2d\n", Ctx_Reported_Level);
	Ctx_Reported_Level = Ctx_Valid_Level;
    }
    /* report new contexts that were pushed since last report */
    while (Ctx_Reported_Level < Ctx_Current_Level) {
	Ctx_Reported_Level++;
	Ctx_Valid_Level++;
	_db_print("ctx: enter level %2d: '%s'\n", Ctx_Reported_Level,
	    ctx_get_descr(Ctx_Reported_Level));
    }
    /* unlock */
    Ctx_Lock--;
}

/* checks for nulls and overflows */
static const char *
ctx_get_descr(Ctx ctx)
{
    if (ctx < 0 || ctx > CTX_MAX_LEVEL)
	return "<lost>";
    return Ctx_Descrs[ctx] ? Ctx_Descrs[ctx] : "<null>";
}
