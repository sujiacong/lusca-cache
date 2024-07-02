
#include "include/config.h"
#include "include/util.h"

#if HAVE_STDIO_H
#include <stdio.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_MEMORY_H
#include <memory.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_ASSERT_H
#include <assert.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_STRINGS_H
#include <strings.h>
#endif

#include "include/rfc1035.h"

#include <sys/socket.h>
#include <sys/time.h>
int
main(int argc, char *argv[])
{
    const char *input;
    const char *qtype;
    char buf[512];
    char rbuf[512];
    size_t sz = 512;
    unsigned short sid;
    int s;
    int rl;
    struct sockaddr_in S;
    struct in_addr a;

    if (5 != argc) {
	fprintf(stderr, "usage: %s ip port <PTR|A|AAAA> <query>\n", argv[0]);
	return 1;
    }
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
	perror("socket");
	return 1;
    }
    memset(&S, '\0', sizeof(S));
    S.sin_family = AF_INET;
    S.sin_port = htons(atoi(argv[2]));
    S.sin_addr.s_addr = inet_addr(argv[1]);

    qtype = argv[3];
    input = argv[4];

    do {
	memset(buf, '\0', 512);
	sz = 512;
	if (strcmp(qtype, "AAAA") == 0) {
	    sz = rfc1035BuildAAAAQuery(input, buf, sz, 1, NULL);
	} else if (strcmp(qtype, "PTR") == 0) {
            inet_aton(input, &a);
            sz = rfc1035BuildPTRQuery(a, buf, sz, 1, NULL);
	} else if (strcmp(qtype, "A") == 0) {
	    sz = rfc1035BuildAQuery(input, buf, sz, 1, NULL);
	} else {
		perror("qtype");
		return 1;
	}
	sendto(s, buf, sz, 0, (struct sockaddr *) &S, sizeof(S));
	do {
	    fd_set R;
	    struct timeval to;
	    FD_ZERO(&R);
	    FD_SET(s, &R);
	    to.tv_sec = 10;
	    to.tv_usec = 0;
	    rl = select(s + 1, &R, NULL, NULL, &to);
	} while (0);
	if (rl < 1) {
	    printf("TIMEOUT\n");
	    continue;
	}
	memset(rbuf, '\0', 512);
	rl = recv(s, rbuf, 512, 0);
	{
	    unsigned short rid = 0;
	    int i;
	    int n;
	    rfc1035_message *answers = NULL;
	    n = rfc1035MessageUnpack(rbuf,
		rl,
		&answers);
	    if (n < 0) {
		printf("ERROR %d\n", rfc1035_errno);
		continue;
	    }
	    printf("SENT ID: %#hx; RECEVIED ID: %#hx\n", sid, rid);

		printf("%d answers\n", n);
		for (i = 0; i < n; i++) {
		    if (answers->answer[i].type == RFC1035_TYPE_A) {
			struct in_addr a;
			memcpy(&a, answers->answer[i].rdata, 4);
			printf("A\t%d\t%s\n", answers->answer[i].ttl, inet_ntoa(a));
		    } else if (answers->answer[i].type == RFC1035_TYPE_PTR) {
			char ptr[128];
			strncpy(ptr, answers->answer[i].rdata, answers->answer[i].rdlength);
			printf("PTR\t%d\t%s\n", answers->answer[i].ttl, ptr);
                   } else if (answers->answer[i].type == RFC1035_TYPE_AAAA) {
                       /* XXX this so should be going through getnameinfo() or something */
                       struct sockaddr_in6 s;
                       int j;
                       bzero(&s, sizeof(s));
                       memcpy(&s.sin6_addr, answers->answer[i].rdata, 16);
                       for (j = 0; j < 16; j += 2) {
                               printf("%.2x%.2x:", (unsigned char) answers->answer[i].rdata[j], (unsigned char) answers->answer[i].rdata[j+1]);
                       }
                       printf("\n");
		    } else {
			fprintf(stderr, "can't print answer type %d\n",
			    (int) answers->answer[i].type);
		    }
	    }
	}
    } while(0);
    return 0;
}
