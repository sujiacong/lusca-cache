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

#include "../libcore/kb.h"
#include "../libcore/varargs.h"

#include "../libsqdebug/debug.h"

#include "../libsqtlv/tlv.h"

#include "../libsqstore/store_mgr.h"
#include "../libsqstore/store_meta.h"

#define	STRIPESIZE 1048576
#define	BLOCKSIZE 1024
#define BLKBITS 10

/* normally in libiapp .. */
int shutting_down = 0;

static void
parse_stripe(int stripeid, char *buf, int len, int blocksize)
{
    int j = 0;
    int bl = 0;
    tlv *t, *tlv_list;
    int64_t *l;
    int tmp;

    while (j < len) {
	l = NULL;
	bl = 0;
	tlv_list = tlv_unpack(&buf[j], &bl, STORE_META_END + 10);
	if (tlv_list == NULL) {
	    printf("  Object: NULL\n");
	    return;
	}
	printf("  Object: (filen %d) hdr size %d\n", j / blocksize + (stripeid * STRIPESIZE / blocksize), bl);
	for (t = tlv_list; t; t = t->next) {
	    switch (t->type) {
	    case STORE_META_URL:
		/* XXX Is this OK? Is the URL guaranteed to be \0 terminated? */
		printf("    URL: %s\n", (char *) t->value);
		break;
	    case STORE_META_OBJSIZE:
		l = t->value;
		printf("Size: %" PRINTF_OFF_T " (len %d)\n", *l, t->length);
		break;
	    }
	}
	if (l == NULL) {
	    printf("  STRIPE: Completed, got an object with no size\n");
	    return;
	}
	j = j + *l + bl;
	/* And now, the blocksize! */
	tmp = j / blocksize;
	tmp = (tmp + 1) * blocksize;
	j = tmp;

	tlv_free(tlv_list);
    }
}

int
main(int argc, char *argv[])
{
    int fd;
    char buf[STRIPESIZE];
    int i = 0, len;
    unsigned int numstripes = 0;
    int blocksize = BLOCKSIZE;
    int blksize_bits;

    /* Setup the debugging library */
    _db_init("ALL,1");
    _db_set_stderr_debug(1);

    if (argc < 4) {
	printf("Usage: %s <path to COSS datafile> <blocksize> <number of stripes>\n", argv[0]);
	exit(1);
    }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
	perror("open");
	exit(1);
    }

    blocksize = (unsigned int) atoi(argv[2]);
    for(blksize_bits = 0;((blocksize >> blksize_bits) > 0);blksize_bits++) {
	if( ((blocksize >> blksize_bits) > 0) &&
	  (((blocksize >> blksize_bits) << blksize_bits) != blocksize)) {
	    printf("Blocksize bits must be a power of 2\n");
	    exit(1);
	}
    }

    numstripes = (unsigned int) atoi(argv[3]);

    while ((len = read(fd, buf, STRIPESIZE)) > 0) {
	printf("STRIPE: %d (len %d)\n", i, len);
	parse_stripe(i, buf, len, blocksize);
	i++;
	if((numstripes > 0) && (i >= numstripes))
	    break;
    }
    return 0;
}
