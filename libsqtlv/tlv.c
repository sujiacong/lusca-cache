#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../include/util.h"

#include "../libcore/varargs.h"
#include "../libsqdebug/debug.h"

#include "tlv.h"

void
tlv_init(void)
{
}

tlv **
tlv_add(int type, const void *ptr, size_t len, tlv ** tail)
{
/*    tlv *t = memPoolAlloc(pool_swap_tlv); */
    tlv *t = xmalloc(sizeof(tlv));
    t->type = (char) type;
    t->length = (int) len;
    t->value = xmalloc(len);
    t->next = NULL;
    xmemcpy(t->value, ptr, len);
    *tail = t;
    return &t->next;            /* return new tail pointer */
}

void
tlv_free(tlv * n)
{
    tlv *t;
    while ((t = n) != NULL) {
        n = t->next;
        xfree(t->value);
        xfree(t);
    }
}

tlv *
tlv_unpack(const char *buf, int *hdr_len, int max_metaid)
{
    tlv *TLV = NULL;            /* we'll return this */
    tlv **T = &TLV;
    char type;
    int length;
    int buflen;
    int j = 0;

    assert(buf != NULL);
    assert(hdr_len != NULL);

#define	STORE_META_OK	0x03
    if (buf[j++] != (char) STORE_META_OK)
        return NULL;
    xmemcpy(&buflen, &buf[j], sizeof(int));
    j += sizeof(int);

    if (buflen > (*hdr_len) - sizeof(char) - sizeof(int)) {
        debug(20, 0) ("tlv_unpack: unable to unpack: passed buffer size %d bytes; TLV length %d bytes; header prefix size %d bytes\n", *hdr_len, buflen, (int) (sizeof(char) + sizeof(int)));
        return NULL;
    }

    /*
     * sanity check on 'buflen' value.  It should be at least big
     * enough to hold one type and one length.
     */
    if (buflen <= (sizeof(char) + sizeof(int)))
            return NULL;
    while (buflen - j >= (sizeof(char) + sizeof(int))) {
        type = buf[j++];
        /* 0 is reserved, but allow some slack for new types.. */
        if (type <= 0 || type > max_metaid) {
            debug(20, 0) ("tlv_unpack: bad type (%d)!\n", type);
            break;
        }
        xmemcpy(&length, &buf[j], sizeof(int));
        if (length < 0 || length > (1 << 16)) {
            debug(20, 0) ("tlv_unpack: insane length (%d)!\n", length);
            break;
        }
        j += sizeof(int);
        if (j + length > buflen) {
            debug(20, 0) ("tlv_unpack: overflow!\n");
            debug(20, 0) ("\ttype=%d, length=%d, buflen=%d, offset=%d\n",
                type, length, buflen, (int) j);
            break;
        }
        T = tlv_add(type, &buf[j], (size_t) length, T);
        j += length;
    }
    *hdr_len = buflen;
    return TLV;
}
