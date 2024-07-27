
/*
 *
 * This is a bit of a hack to evaluate how quickly one could rebuild the UFS bitmap
 * before slowly rebuilding the index.
 *
 * Adrian Chadd <adrian@creative.net.au>
 */

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

#include "libcore/kb.h"
#include "libcore/varargs.h"
#include "libcore/mem.h"
#include "libcore/tools.h"

#include "libsqdebug/debug.h"

#include "libsqtlv/tlv.h"

#define	SQUID_MD5_DIGEST_LENGTH	16

#include "libsqstore/store_mgr.h"
#include "libsqstore/store_meta.h"
#include "libsqstore/store_log.h"
#include "libsqstore/store_file_ufs.h"

#define	BUFSIZE		1024

/* normally in libiapp .. */
int shutting_down = 0;

void
read_dir(store_ufs_dir_t *sd)
{
	DIR *d;
	struct dirent *de;
	char path[SQUID_MAXPATHLEN];
	char dir[SQUID_MAXPATHLEN];
	int fn;
	int i, j;

	getCurrentTime();
	for (i = 0; i < store_ufs_l1(sd); i++) {
		for (j = 0; j < store_ufs_l2(sd); j++) {
			(void) store_ufs_createDir(sd, i, j, dir);
			getCurrentTime();
			debugs(47, 1, "read_dir: opening dir %s", dir);
			d = opendir(dir);
			if (! d) {
				perror("opendir");
				continue;
			}

			while ( (de = readdir(d)) != NULL) {
				if (de->d_name[0] == '.')
					continue;
				getCurrentTime();

				/* Verify that the given filename belongs in the given directory */
				if (sscanf(de->d_name, "%x", &fn) != 1) {
					debugs(47, 1, "read_dir: invalid %s", de->d_name);
						continue;
				}
				if (! store_ufs_filenum_correct_dir(sd, fn, i, j)) {
					debugs(47, 1, "read_dir: %s does not belong in %d/%d", de->d_name, i, j);
						continue;
				}

				snprintf(path, sizeof(path) - 1, "%s/%s", dir, de->d_name);
				printf("%s ", path);
			}
			closedir(d);
			printf("\n");
		}
	}
}

int
main(int argc, char *argv[])
{
    /* Setup the debugging library */
    _db_init("ALL,1");
    _db_set_stderr_debug(1);
    store_ufs_dir_t store_ufs_info;

    if (argc < 5) {
	printf("Usage: %s <store path> <l1> <l2> <path to swapfile>\n", argv[0]);
	exit(1);
    }

    store_ufs_init(&store_ufs_info, argv[1], atoi(argv[2]), atoi(argv[3]), argv[4]);
    read_dir(&store_ufs_info);
    store_ufs_done(&store_ufs_info);

    return 0;
}
