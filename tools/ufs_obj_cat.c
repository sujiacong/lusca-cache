#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>

#include "../libcore/varargs.h"
#include "../libcore/tools.h"
#include "../libcore/kb.h"		/* for squid_off_t */
#include "../libcore/debug.h"

#include "../libsqtlv/tlv.h"

#define SQUID_MD5_DIGEST_LENGTH 16

#include "../libsqstore/store_mgr.h"
#include "../libsqstore/store_meta.h"
#include "../libsqstore/store_log.h"

/* XXX temporary! because of linking related stuff! */
int shutting_down = 0;


static const char *
storeKeyText(const unsigned char *key)
{       
        static char buf[64];
        char b2[4];
        
        buf[0] = '\0';
        
        int i; 
        for (i = 0; i < 16; i++) {
                sprintf(b2, "%02X", *(key + i));
                strcat(buf, b2);
        }
        return buf;
}

#define	META_BUFSIZ		4096

int KidIdentifier = 0;

int
main(int argc, const char *argv[])
{
	const char *path = NULL;
	int fd, r;
	char buf[META_BUFSIZ];
	tlv *t, *tl;

	path = argv[1];

	/* open the file */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	/* read in the metadata */
	r = read(fd, buf, META_BUFSIZ);
	if (r <= 0) {
		perror("read");
		exit(1);
	}

	getCurrentTime();

	/* try parsing! */
	tl = storeSwapMetaUnpack(buf, &r);

	close(fd); fd = -1;

	/* is tl null? no metadata */
	if (tl == NULL) {
		printf("%s: ERROR: no valid metadata found\n", path);
		exit(1);
	}

	/* meta data size needs to be <= buffer size */
	if (r > META_BUFSIZ) {
		printf("%s: ERROR: metadata size (%d bytes) bigger than buffer (%d bytes) ?!\n", path, r, META_BUFSIZ);
		exit(1);
	}

	printf("%s: metadata header size: %d\n", path, r);

	for (t = tl ; t; t = t->next) {
       		switch (t->type) {
			case STORE_META_KEY_MD5:
				printf("STORE_META_KEY_MD5: len %d: value %s\n", t->length,
				    storeKeyText(t->value));
				break;
			case STORE_META_URL:
				printf("STORE_META_URL: len %d: value %.*s\n", t->length,
				    t->length, (const char *)t->value);
				break;
			case STORE_META_STOREURL:
				printf("STORE_META_STOREURL: len %d: value %.*s\n", t->length,
				    t->length, (const char *)t->value);
				break;
			case STORE_META_OBJSIZE:
				printf("STORE_META_OBJSIZE: len %d: value %" PRINTF_OFF_T "\n", t->length,
				    * ((squid_off_t *) t->value));
				break;
			case STORE_META_VARY_HEADERS:
				printf("STORE_META_VARY_HEADERS: len %d: value %.*s\n", t->length,
				    t->length, (const char *)t->value);
				break;
			case STORE_META_STD_LFS:
				/* This is for when size_t != squid_file_sz */
				/* XXX why is this size so .. stupidly big for the timestamp? */
				printf("STORE_META_STD_LFS: len %d: value %" PRINTF_OFF_T "\n", t->length,
				    * ((squid_file_sz *) t->value));
				break;
			default:
				printf("  unknown type %d; len %d\n", t->type, t->length);
		}
	}
	
	exit(0);
}

