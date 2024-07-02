#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include "../include/squid_md5.h"

#include "../libcore/varargs.h"
#include "../libcore/kb.h"

#include "store_mgr.h"
#include "store_log.h"

const char * swap_log_op_str[] = {
    "SWAP_LOG_NOP",
    "SWAP_LOG_ADD",
    "SWAP_LOG_DEL",
    "SWAP_LOG_VERSION",
    "SWAP_LOG_PROGRESS",
    "SWAP_LOG_COMPLETED",
    "SWAP_LOG_ERROR",
    "SWAP_LOG_MAX"
};

int
storeSwapLogUpgradeEntry(storeSwapLogData *dst, storeSwapLogDataOld *src)
{
    dst->op = src->op;
    dst->swap_filen = src->swap_filen;
    dst->timestamp = src->timestamp;
    dst->lastref = src->lastref;
    dst->expires = src->expires;
    dst->lastmod = src->lastmod;
    dst->swap_file_sz = src->swap_file_sz;			/* This is the entry whose size has changed */
    dst->refcount = src->refcount;
    dst->flags = src->flags;
    memcpy(dst->key, src->key, SQUID_MD5_DIGEST_LENGTH);

    return 1;
}

int
storeSwapLogPrintHeader(FILE *fp)
{
    char buf[sizeof(storeSwapLogData)];
    storeSwapLogHeader *sh = (storeSwapLogHeader *) buf;

    memset(buf, 0, sizeof(buf));
    sh->op = SWAP_LOG_VERSION;
    sh->version = 1;
    sh->record_size = sizeof(storeSwapLogData);
    if (fwrite(buf, sizeof(storeSwapLogData), 1, fp) < 1)
        return 0;
    return 1;
}

int
storeSwapLogPrintCompleted(FILE *fp)
{
    char buf[sizeof(storeSwapLogData)];
    storeSwapLogCompleted *sh = (storeSwapLogCompleted *) buf;

    memset(buf, 0, sizeof(buf));
    sh->op = SWAP_LOG_COMPLETED;
    if (fwrite(buf, sizeof(storeSwapLogData), 1, fp) < 1)
        return 0;
    return 1;
}

int
storeSwapLogPrintProgress(FILE *fp, u_int32_t progress, u_int32_t total)
{
        char buf[128];
        storeSwapLogProgress *sp = (storeSwapLogProgress *) buf;

        memset(buf, 0, sizeof(buf));
        sp->op = SWAP_LOG_PROGRESS;
        sp->total = total;
        sp->progress = progress;

        /* storeSwapLogData is the record size */
        if (fwrite(buf, sizeof(storeSwapLogData), 1, fp) < 1)
                return 0;
        return 1;
}

