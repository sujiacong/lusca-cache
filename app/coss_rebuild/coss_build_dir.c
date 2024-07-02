#include "include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>

#include "include/squid_md5.h"
#include "include/util.h"
#include "libcore/varargs.h"
#include "libcore/tools.h"
#include "libcore/kb.h"
#include "libsqdebug/debug.h"
#include "libsqtlv/tlv.h"
#include "libsqstore/store_mgr.h"
#include "libsqstore/store_log.h"
#include "libsqstore/store_meta.h"
#include "libsqstore/rebuild_entry.h"

/* XXX macosx specific hack - need to generic-ify this! */
#if !defined(O_BINARY)
#define O_BINARY                0x0
#endif

/*
 * Rebuilding from the COSS filesystem itself is currently very, very
 * resource intensive. Since there is no on-disk directory, the
 * whole store must first be read from start to finish and objects
 * must be pulled out of the stripes.
 *
 * There's also no easy logic to determine where the -head- pointer of
 * the filesystem was.
 *
 * All in all this is quite a horrible method for rebuilding..
 */

static int
parse_stripe(int stripeid, char *buf, int len, int blocksize, size_t stripesize)
{   
	int j = 0;
	int tmp;
	rebuild_entry_t re;

	while (j < len) {
		rebuild_entry_init(&re);
		if (! parse_header(&buf[j], len - j, &re)) {
			rebuild_entry_done(&re);
			debug(85, 5) ("parse_stripe: id %d: no more data or invalid header\n", stripeid);
			return 0;
		}

		debug(85, 5) ("  Object: (filen %ld)\n", (long int) (j / blocksize + (stripeid * stripesize / blocksize)));
		debug(85, 5) ("  URL: %s\n", re.url);
		debug(85, 5) ("  hdr_size: %d\n", (int) re.hdr_size);
		debug(85, 5) ("  file_size: %d\n", (int) re.file_size);

		/*
		 * We require at least the size to continue. If we don't get a valid size to read the next
		 * object for, we can't generate a swaplog entry. Leave checking consistency up to the
		 * caller.
		 */
		if (re.hdr_size == -1 || re.file_size == -1) {
			rebuild_entry_done(&re);
			debug(85, 5) ("parse_stripe: id %d: not enough information in this object; end of stripe?\n", stripeid);
			return 0;
		}

		re.swap_filen = (off_t) j / (off_t) blocksize + (off_t) ((off_t) stripeid * (off_t) stripesize / (off_t) blocksize);

		if (! write_swaplog_entry(stdout, &re)) {
			rebuild_entry_done(&re);
			return -1;
		}

		j = j + re.file_size + re.hdr_size;
		/* And now, the blocksize! */
		tmp = j / blocksize;
		tmp = (tmp + 1) * blocksize;
		j = tmp;
		rebuild_entry_done(&re);
	}
	return 1;
}

int
coss_rebuild_dir(const char *file, size_t stripesize, int blocksize, int numstripes)
{
	int fd;
	char *buf;
	ssize_t len;
	int blksize_bits;
	int cur_stripe;
	int report_interval;
	off_t rl, o;
	pid_t pid;

	pid = getpid();
	report_interval = numstripes / 20;	/* every 5% */

	buf = malloc(stripesize);
	if (! buf) {
		debug(85, 1) ("%s: couldn't allocated %ld bytes for rebuild buffer: (%d) %s\n", file, (long int) stripesize, errno, xstrerror());
		return 0;
	}

	fd = open(file, O_RDONLY | O_BINARY);
	if (fd < 0) {
		perror("open");
		return 0;
	}

	storeSwapLogPrintHeader(stdout);	

	for(blksize_bits = 0;((blocksize >> blksize_bits) > 0);blksize_bits++) {
		if( ((blocksize >> blksize_bits) > 0) &&
		  (((blocksize >> blksize_bits) << blksize_bits) != blocksize)) {
			debug(85, 1) ("COSS[%d]: %s: Blocksize bits (%d) must be a power of 2\n", pid, file, blksize_bits);
			safe_free(buf);
			return(0);
		}
	}

	for (cur_stripe = 0; cur_stripe < numstripes; cur_stripe++) {
		getCurrentTime();
		debug(85, 5) ("COSS: %s: STRIPE: %d\n", file, cur_stripe);
		storeSwapLogPrintProgress(stdout, cur_stripe, numstripes);
		if (cur_stripe % report_interval == 0) {
			debug(85, 1) ("COSS[%d]: %s: Rebuilding %.2f%% complete (%d out of %d stripes)\n",
			    pid, file, (float) cur_stripe / (float) numstripes * 100.0, cur_stripe, numstripes);
		}

		o = ((off_t) cur_stripe) * ((off_t) stripesize);
		rl = lseek(fd, o, SEEK_SET);
		if (rl < 0)
			continue;
		
		len = read(fd, buf, stripesize);
		if (len < 0)
			continue;

		if (parse_stripe(cur_stripe, buf, len, blocksize, stripesize) < 0)
			break;
	}
	close(fd);
	storeSwapLogPrintCompleted(stdout);	
	fflush(stdout);

	safe_free(buf);
	return 1;
}
