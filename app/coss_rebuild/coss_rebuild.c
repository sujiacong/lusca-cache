#include "include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "../libcore/varargs.h"
#include "libcore/tools.h"
#include "libsqdebug/debug.h"

#include "coss_build_dir.h"

int shutting_down = 0;	/* needed for libiapp */

#define	WRITE_BUFFER_LEN	65536

typedef enum {
        REBUILD_NONE,
        REBUILD_DISK,
        REBUILD_LOG
} rebuild_type_t;

static void
usage(const char *cmd)
{
	printf("Usage: %s <command> <path> <block size> <stripe size> <number of stripes>\n", cmd);
	printf("  where the block and stripe sizes are in bytes\n");
	printf("  and <command> is, for now, 'rebuild'\n");
}

int
main(int argc, const char *argv[])
{
	const char *cmd;
	const char *path;
	int block_size;
	size_t stripe_size;
	int num_stripes;
	char * wbuf = NULL;
	rebuild_type_t r;


	if (argc < 5) {
		usage(argv[0]);
		exit(127);
	}
	cmd = argv[1];

	if (strcmp(cmd, "rebuild") == 0)
		r = REBUILD_DISK;
	else {
		usage(argv[0]);
		exit(127);
	}

	wbuf = malloc(WRITE_BUFFER_LEN);
	if (wbuf) {  
		setbuffer(stdout, wbuf, WRITE_BUFFER_LEN);
	}

	/* Setup the debugging library */
	_db_init("ALL,1");
	_db_set_stderr_debug(1);

	path = argv[2];
	block_size = atoi(argv[3]);
	stripe_size = atoi(argv[4]);
	num_stripes = atoi(argv[5]);

	(void) coss_rebuild_dir(path, stripe_size, block_size, num_stripes);

	exit(0);
}
