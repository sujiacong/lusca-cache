#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>

#include "libcore/tools.h"

/*
 * Test > 32 bit storedir size math for uint64_percent()
 */
static void
test1a(void)
{
	uint64_t store_swap_size;
	uint64_t maxSize;

	maxSize = 2030043136;
	store_swap_size = 1826533798;

	/* Make well and truely sure the values are > 32 bit */
	maxSize *= 4;
	store_swap_size *= 4;

	printf("MaxSize: %llu\n", maxSize);
	printf("store_swap_size: %llu\n", store_swap_size);
	printf("Current Capacity       : %d%% used, %d%% free\n",
	    (int) uint64_percent(store_swap_size, maxSize),
	    (int) uint64_percent((maxSize - store_swap_size), maxSize));
}

int
main(int argc, const char *argv[])
{
	printf("%s: initializing\n", argv[0]);
	test1a();
	exit(0);
}

