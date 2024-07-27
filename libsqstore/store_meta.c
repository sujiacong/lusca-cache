#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>

#include "../include/util.h"

#include "../libcore/varargs.h"
#include "../libcore/tools.h"
#include "../libcore/kb.h"		/* for squid_off_t */
#include "../libcore/debug.h"

#include "../libsqtlv/tlv.h"

#include "libsqstore/store_mgr.h"
#include "libsqstore/store_meta.h"


char *
storeSwapMetaPack(tlv * tlv_list, int *length)
{
    int buflen = 0;
    tlv *t;
    int j = 0;
    char *buf; 
    assert(length != NULL);
    buflen++;                   /* STORE_META_OK */
    buflen += sizeof(int);      /* size of header to follow */
    for (t = tlv_list; t; t = t->next)
        buflen += sizeof(char) + sizeof(int) + t->length;
    buf = xmalloc(buflen);
    buf[j++] = (char) STORE_META_OK;
    xmemcpy(&buf[j], &buflen, sizeof(int));
    j += sizeof(int);
    for (t = tlv_list; t; t = t->next) {
        buf[j++] = (char) t->type;
        xmemcpy(&buf[j], &t->length, sizeof(int));
        j += sizeof(int);
        xmemcpy(&buf[j], t->value, t->length);
        j += t->length;
    }   
    assert((int) j == buflen);
    *length = buflen;
    return buf;
}       

tlv *
storeSwapMetaUnpack(const char *buf, int *hdr_len)
{
    tlv *TLV = NULL;            /* we'll return this */
    tlv **T = &TLV;
    char type;
    int length;
    int buflen;
    int j = 0;
    assert(buf != NULL);
    assert(hdr_len != NULL);
    if (buf[j++] != (char) STORE_META_OK)
        return NULL;
    xmemcpy(&buflen, &buf[j], sizeof(int));
    j += sizeof(int);
    /*
     * sanity check on 'buflen' value.  It should be at least big
     * enough to hold one type and one length.
     */
    if (buflen <= (sizeof(char) + sizeof(int)))
            return NULL;
    while (buflen - j >= (sizeof(char) + sizeof(int))) {
        type = buf[j++];
        /* VOID is reserved, but allow some slack for new types.. */
        if (type <= STORE_META_VOID || type > STORE_META_END + 10) {
            debugs(20, 0, "storeSwapMetaUnpack: bad type (%d)!", type);
            break;
        }
        xmemcpy(&length, &buf[j], sizeof(int));
        if (length < 0 || length > (1 << 16)) {
            debugs(20, 0, "storeSwapMetaUnpack: insane length (%d)!", length);
            break;
        }
        j += sizeof(int);
        if (j + length > buflen) {
            debugs(20, 0, "storeSwapMetaUnpack: overflow!");
            debugs(20, 0, "\ttype=%d, length=%d, buflen=%d, offset=%d",
                type, length, buflen, (int) j);
            break;
        }
        T = tlv_add(type, &buf[j], (size_t) length, T);
        j += length;
    }
    *hdr_len = buflen;
    return TLV;
}
