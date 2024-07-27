#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "include/util.h"

#include "libcore/varargs.h"
#include "libcore/tools.h"
#include "libsqdebug/debug.h"

/* XXX macosx specific hack - need to generic-ify this! */
#if !defined(O_BINARY)
#define O_BINARY                0x0
#endif

int shutting_down = 0;	/* needed for debug routines for now */

/*
 * Args: /path/to/cossdir <number of stripes> <stripesize>
 */
int KidIdentifier = 0;

int
main(int argc, const char *argv[])
{
	size_t sz, stripe_sz;
	const char *path;
	int i;
	int fd;
	char *buf;
	off_t r;
	char *t = NULL;
	char *debug_args = "ALL,1";

        /* Setup the debugging library */
        if ((t = getenv("SQUID_DEBUG")))
                debug_args = xstrdup(t);
        _db_init(debug_args);
        _db_set_stderr_debug(99);
        getCurrentTime();

	if (argc < 3) {
		printf("Usage: %s <path> <stripe count> <stripe size>\n", argv[0]);
		exit(1);
	}

	path = argv[1];
	sz = atoi(argv[2]);
	stripe_sz = atoi(argv[3]);

	/*
	 * For now, just write 256 bytes of NUL's into the beginning of
	 * each stripe. COSS doesn't really have an on-disk format
	 * that leads itself to anything newfs-y quite yet. The NULs
	 * -should- be enough to trick the rebuild process into treating
	 * the rest of that stripe as empty.
	 */
	fd = open(path, O_WRONLY | O_CREAT | O_BINARY, 0644);
	if (fd < 0) {
		perror("open");
		exit(127);
	}
	buf = xcalloc(stripe_sz, sizeof(char));
	debugs(85, 1, "coss_newfs: %s: initialising stripe", path);

	for (i = 0; i < sz; i += 1) {
		getCurrentTime();
		debugs(85, 5, "seeking to stripe %d", i);
		r = lseek(fd, (off_t) i * (off_t) stripe_sz, SEEK_SET);
		if (r < 0) {
			perror("lseek");
			exit(127);
		}
		r = write(fd, buf, stripe_sz);
		if (r < 0) {
			perror("write");
			exit(127);
		}
	}
	safe_free(buf);
	debugs(85, 1, "coss_newfs: %s: finished", path);
	close(fd);
	exit(0);
}
