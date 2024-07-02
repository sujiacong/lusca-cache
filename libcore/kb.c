#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "kb.h"

void
kb_incr(kb_t * k, squid_off_t v)
{  
    k->bytes += v;
    k->kb += (k->bytes >> 10);
    k->bytes &= 0x3FF;
    if (k->kb < 0) {
        /*
         * If kb overflows and becomes negative then add powers of
         * 2 until it becomes positive again.
         */
        kb_t x;
        x.kb = 1L << 31;
        while (x.kb && ((k->kb + x.kb) < 0)) {
            x.kb <<= 1;
        }
        k->kb += x.kb;
    }
}

