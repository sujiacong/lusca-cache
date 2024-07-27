
#include "config.h"

#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#if HAVE_STDIO_H
#include <stdio.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <sys/stat.h>
#include <dirent.h>

#include "include/util.h"
#include "include/squid_md5.h"

#include "libcore/kb.h"
#include "libcore/varargs.h"
#include "libcore/mem.h"
#include "libcore/tools.h"

#include "libsqdebug/debug.h"

#include "libsqtlv/tlv.h"

#include "libsqstore/store_mgr.h"
#include "libsqstore/store_meta.h"
#include "libsqstore/store_log.h"
#include "libsqstore/store_file_ufs.h"

#include "rebuild_entry.h"

void
rebuild_entry_done(rebuild_entry_t *re)
{
	safe_free(re->md5_key);
	safe_free(re->url);
	safe_free(re->storeurl);
}

void
rebuild_entry_init(rebuild_entry_t *re)
{
	memset(re, 0, sizeof(*re));
	re->hdr_size = -1;
	re->file_size = -1;
	re->swap_filen = -1;
}

int
parse_header(char *buf, int len, rebuild_entry_t *re)
{
	tlv *t, *tlv_list;
	int bl = len;
	int parsed = 0;

	tlv_list = tlv_unpack(buf, &bl, STORE_META_END + 10);
	if (tlv_list == NULL) {
		return -1;
	}

	re->hdr_size = bl;

	for (t = tlv_list; t; t = t->next) {
	    switch (t->type) {
	    case STORE_META_URL:
		debugs(47, 5, "  STORE_META_URL");
		/* XXX Is this OK? Is the URL guaranteed to be \0 terminated? */
		re->url = xstrdup( (char *) t->value );
		parsed++;
		break;
	    case STORE_META_KEY_MD5:
		debugs(47, 5, "  STORE_META_KEY_MD5");
		/* XXX should double-check key length? */
		re->md5_key = xmalloc(SQUID_MD5_DIGEST_LENGTH);
		memcpy(re->md5_key, t->value, SQUID_MD5_DIGEST_LENGTH);
		parsed++;
		break;
	    case STORE_META_STD_LFS:
		debugs(47, 5, "  STORE_META_STD_LFS");
		/* XXX should double-check lengths match? */
		memcpy(&re->mi, t->value, sizeof(re->mi));
		parsed++;
		break;

	    /* Undocumented mess! */
	    /* STORE_META_OBJSIZE is the objectLen(). It includes the reply headers but not the swap metadata */
	    /* swap_file_sz in the rebuild entry data is the objectLen() + swap_hdr_size */
	    case STORE_META_OBJSIZE:
		debugs(47, 5, "  STORE_META_OBJSIZE");
		/* XXX is this typecast'ed to the right "size" on all platforms ? */
		re->file_size = *((squid_off_t *) t->value);
		parsed++;
		break;
	    default:
		break;
	    }
	}
	assert(tlv_list != NULL);
	tlv_free(tlv_list);
	return (parsed > 1);
}

int
write_swaplog_entry(FILE *fp, rebuild_entry_t *re)
{
	storeSwapLogData sd;

	sd.op = SWAP_LOG_ADD;
	sd.swap_filen = re->swap_filen;
	sd.timestamp = re->mi.timestamp;
	sd.lastref = re->mi.lastref;
	sd.expires = re->mi.expires;
	sd.lastmod = re->mi.lastmod;
	/*
	 * If we get here - either file_size must be set by the parser above
	 * or by some other method (eg UFS dir rebuild will use stat()
	 * and then substract the swap header length.
	 */
	if (re->file_size < 0 || re->hdr_size < 0)
		return -1;

	sd.swap_file_sz = re->hdr_size + re->file_size;
	sd.refcount = re->mi.refcount;
	sd.flags = re->mi.flags;

	memcpy(&sd.key, re->md5_key, sizeof(sd.key));
	if (fwrite(&sd, sizeof(sd), 1, fp) < 1)
		return 0;
	return 1;
}
