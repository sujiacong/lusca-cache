#include "../include/config.h"

/* XXX this should be uhm, a library function! [ahc] */
static char tohex[] = { '0','1','2','3','4','5','6','7','8','9', 'A', 'B', 'C', 'D', 'E', 'F' };

/*
 * Create a hex string from the given byte array.
 *
 * It is up to the caller to ensure that there is at least 2x "srclen" bytes
 * available in dst.
 */
void
hex_from_byte_array(char *dst, const char *src, int srclen)
{
	int i;
	for (i = 0; i < srclen; i++) {
		*(dst++) = tohex[(*src >> 4) & 0x0f];
		*(dst++) = tohex[*(src++) & 0x0f];
	}
}
