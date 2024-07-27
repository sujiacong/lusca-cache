#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif

#include "../include/config.h"
#include "../include/squid_md5.h"
#include "../include/util.h"

#include "../libcore/varargs.h"
#include "../libcore/kb.h"
#include "../libcore/tools.h"	/* for SQUID_MAXPATHLEN */

#include "../libsqdebug/debug.h"

#include "store_mgr.h"
#include "store_log.h"

#include "store_file_ufs.h"

void
store_ufs_init(store_ufs_dir_t *sd, const char *path, int l1, int l2, const char * swaplog_path)
{
	sd->path = xstrdup(path);
	sd->swaplog_path = xstrdup(swaplog_path);
	sd->l1 = l1;
	sd->l2 = l2;
}

void
store_ufs_done(store_ufs_dir_t *sd)
{
	safe_free(sd->path);
	safe_free(sd->swaplog_path);
}

/*
 * Create a UFS path given the component bits.
 *
 * "buf" must be SQUID_MAXPATHLEN.
 */
int
store_ufs_createPath(store_ufs_dir_t *sd, int filn, char *buf)
{   
    int L1 = store_ufs_l1(sd);
    int L2 = store_ufs_l2(sd);
    buf[0] = '\0';
    snprintf(buf, SQUID_MAXPATHLEN, "%s/%02X/%02X/%08X",
        store_ufs_path(sd),
        ((filn / L2) / L2) % L1,
        (filn / L2) % L2,
        filn);
    return 1;
}   

/*
 * Create a UFS directory path given the component bits.
 */
int
store_ufs_createDir(store_ufs_dir_t *sd, int i, int j, char *buf)
{
    buf[0] = '\0';
    snprintf(buf, SQUID_MAXPATHLEN, "%s/%02X/%02X", store_ufs_path(sd), i, j);
    return 1;
}

/*
 * F1/F2 - current directory numbers which they're in
 * L1/L2 - configured storedir L1/L2
 * fn - file number
 *
 * returns whether "fn" belongs in the directory F1/F2 given the configured L1/L2
 */
int
store_ufs_filenum_correct_dir(store_ufs_dir_t *sd, int fn, int F1, int F2)
{
    int L1 = store_ufs_l1(sd), L2 = store_ufs_l2(sd);
    int D1, D2;
    int filn = fn;

    D1 = ((filn / L2) / L2) % L1;
    if (F1 != D1)
        return 0;
    D2 = (filn / L2) % L2;
    if (F2 != D2)
        return 0;
    return 1;
}

/*
 * Check whether the given UFS storedir has a valid logfile to rebuild from
 *
 * This is mostly ripped from what was inferred from src/fs/aufs/store_dir_aufs.c
 *
 * In summary:
 * + If there is no logfile, return false
 * + Is the logfile 0 bytes long? return false
 *
 * Note that this isn't at all atomic - the file isn't opened here at all.
 * It is possible that the log will change status between this check and
 * a subsequent attempt at using it. Code should thus not assume that
 * "true" from this guarantees the logfile will be correct.
 */
int
store_ufs_has_valid_rebuild_log(store_ufs_dir_t *sd)
{
	struct stat sb;
	int x;

	x = stat(sd->swaplog_path, &sb);
	if (x < 0) {
		debugs(47, 1, "store_ufs_has_valid_rebuild_log: %s: no valid swaplog found: (%d) %s", sd->swaplog_path, errno, xstrerror());
		return 0;
	}

	if (sb.st_size == 0)
		return 0;

	return 1;
}
