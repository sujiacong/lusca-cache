#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "include/squid_md5.h"
#include "include/util.h"

#include "libcore/varargs.h"
#include "libcore/kb.h"
#include "libcore/tools.h"
#include "libsqdebug/debug.h"
#include "libsqstore/store_mgr.h"
#include "libsqstore/store_log.h"
#include "libsqstore/store_file_ufs.h"

#include "ufs_build_dir.h"
#include "ufs_build_log.h"

#define	WRITE_BUFFER_LEN	65536

int shutting_down = 0;

typedef enum {
	REBUILD_NONE,
	REBUILD_DISK,
	REBUILD_LOG
} rebuild_type_t;

static rebuild_type_t
probe_dir(store_ufs_dir_t *u)
{
	if (store_ufs_has_valid_rebuild_log(u))
		return REBUILD_LOG;
	return REBUILD_DISK;
}

static void
usage(const char *cmdname)
{
	printf("Usage: %s <command> <store path> <l1> <l2> <path to swapfile>\n", cmdname);
	printf("  where <command> is one of rebuild-dir, rebuild-log or rebuild.\n");
}

int KidIdentifier = 0;
int
main(int argc, char *argv[])
{
	const char *cmd;
	store_ufs_dir_t store_ufs_info;
	rebuild_type_t rebuild_type;
	char *t;
	char *wbuf = NULL;
	char *debug_args = "ALL,1";

	if (argc < 6) {
		usage(argv[0]);
		exit(1);
	}
	cmd = argv[1];

	wbuf = malloc(WRITE_BUFFER_LEN);
	if (wbuf) {
		setbuffer(stdout, wbuf, WRITE_BUFFER_LEN);
	}

	/* Setup the debugging library */
	if ((t = getenv("SQUID_DEBUG")))
		debug_args = xstrdup(t);
	_db_init(debug_args);
	_db_set_stderr_debug(99);
	getCurrentTime();

	debugs(86, 2, "ufs_rebuild: cmd=%s, dir=%s, l1=%d, l2=%d, swaplog=%s", argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), argv[5]);

	store_ufs_init(&store_ufs_info, argv[2], atoi(argv[3]), atoi(argv[4]), argv[5]);

	if (strcmp(cmd, "rebuild-dir") == 0) {
		rebuild_type = REBUILD_DISK;
	} else if (strcmp(cmd, "rebuild-log") == 0) {
		rebuild_type = REBUILD_LOG;
	} else if (strcmp(cmd, "rebuild") == 0) {
		rebuild_type = probe_dir(&store_ufs_info);
	} else {
		usage(argv[0]);
		exit(1);
	}

	/* Output swap header to stdout */
#ifdef _SQUID_WIN32_
	setmode(fileno(stdout), O_BINARY);
#endif
	(void) storeSwapLogPrintHeader(stdout);

	debugs(86, 1, "ufs_rebuild: %s: rebuild type: %s", store_ufs_info.path, rebuild_type == REBUILD_DISK ? "REBUILD_DISK" : "REBUILD_LOG");

	if (rebuild_type == REBUILD_DISK)
		rebuild_from_dir(&store_ufs_info);
	else
		rebuild_from_log(&store_ufs_info);

	store_ufs_done(&store_ufs_info);
	(void) storeSwapLogPrintCompleted(stdout);
	fflush(stdout);

	return 0;
}
