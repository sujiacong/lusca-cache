/*
 * $Id: stub_memaccount.c 11379 2006-09-18 22:54:39Z hno $
 */

/* Stub function for programs not implementing statMemoryAccounted */
#include "config.h"
#include "util.h"
size_t
statMemoryAccounted(void)
{
    return -1;
}
