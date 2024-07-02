#include "../../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/util.h"


void
test_strpbrk_n(const char *foo)
{
	const char *w_space = " \t\n\r";
	if (strpbrk_n(foo, strlen(foo), w_space))
		printf("whitespace FOUND in '%s'\n", foo);
	else
		printf("whitespace NOT found in '%s'\n", foo);
}

int
main(int argc, const char *argv[])
{
	test_strpbrk_n("abcdef");
	test_strpbrk_n("abcdef ghi");
	exit(0);
}
