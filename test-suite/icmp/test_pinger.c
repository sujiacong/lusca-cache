#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <signal.h>

#include "include/Array.h"
#include "include/Stack.h"
#include "include/util.h"
#include "libcore/valgrind.h"
#include "libcore/varargs.h"
#include "libcore/debug.h"
#include "libcore/kb.h"
#include "libcore/gb.h"
#include "libcore/tools.h"
                        
#include "libmem/MemPool.h"
#include "libmem/MemBufs.h"
#include "libmem/MemBuf.h"

#include "libsqdebug/debug_file.h"
                
#include "libcb/cbdata.h"
                
#include "libsqinet/inet_legacy.h" 
#include "libsqinet/sqinet.h"
        
#include "libiapp/iapp_ssl.h"
#include "libiapp/fd_types.h"
#include "libiapp/comm_types.h"
#include "libiapp/comm.h"
#include "libiapp/pconn_hist.h"   
#include "libiapp/signals.h"
#include "libiapp/mainloop.h"

#include "libhelper/ipc.h"

#define	MAX_PKT_SZ	384
#define PINGER_PAYLOAD_SZ 8192
#define	MAX_PAYLOAD	8000
#define S_ICMP_ECHO      1

struct _pingerEchoData {
    struct in_addr to;
    unsigned char opcode;
    int psize;
    char payload[PINGER_PAYLOAD_SZ];
};
typedef struct _pingerEchoData PingerEchoData;
 
struct _pingerReplyData {
    struct in_addr from;
    unsigned char opcode;
    int rtt;
    int hops;
    int psize;
    char payload[PINGER_PAYLOAD_SZ];
};
typedef struct _pingerReplyData PingerReplyData;

typedef struct {
    struct timeval tv;
    unsigned char opcode;
    char payload[MAX_PAYLOAD];
} icmpEchoData;

void
no_suid(void)
{
}

static void
icmpSend(int icmp_sock, PingerEchoData * pkt, int len)
{
    int x; 
    if (icmp_sock < 0)
        return;
    debug(37, 1) ("icmpSend: to %s, opcode %d, len %d\n",
        inet_ntoa(pkt->to), (int) pkt->opcode, pkt->psize);
    x = send(icmp_sock, (char *) pkt, len, 0);
    debug(37, 1) ("send of %d bytes returned %d\n", len, x);
    if (x < 0) {
        debug(37, 1) ("icmpSend: send: %s\n", xstrerror());
        if (errno == ECONNREFUSED || errno == EPIPE) {
		debug(1, 1)( "icmpSend: bad error\n");
            return;
        }
    } else if (x != len) {
        debug(37, 1) ("icmpSend: Wrote %d of %d bytes\n", x, len);
    }
}   


static void   
icmpSendEcho(int icmp_sock, struct in_addr to, int opcode, const char *payload, int len)
{   
    static PingerEchoData pecho;
    if (payload && len == 0)
        len = strlen(payload);
    assert(len <= PINGER_PAYLOAD_SZ);
    pecho.to = to;
    pecho.opcode = (unsigned char) opcode;
    pecho.psize = len;
    xmemcpy(pecho.payload, payload, len);
    icmpSend(icmp_sock, &pecho, sizeof(PingerEchoData) - PINGER_PAYLOAD_SZ + len);
}

int
main(int argc, const char *argv[])
{
    const char *args[2];
    int rfd;
    int wfd;
    void *hIpc;
    pid_t pid;

        iapp_init();
        squid_signal(SIGPIPE, SIG_IGN, SA_RESTART);
        
        _db_init("ALL,99");
        _db_set_stderr_debug(99);
	debug_log = stderr;

    args[0] = "(pinger)";
    args[1] = NULL;
    pid = ipcCreate(IPC_DGRAM,
        "../../src/pinger",
        args,
        "Pinger Socket",
	0,
        &rfd,
        &wfd,  
        &hIpc);

    if (pid < 0)
	exit (-1);

    assert(rfd == wfd);
    fd_note(rfd, "pinger");
#if 0
    commSetSelect(icmp_sock, COMM_SELECT_READ, icmpRecv, NULL, 0);
    commSetTimeout(icmp_sock, -1, NULL, NULL);
#endif
    debug(37, 1) ("Pinger PID (%d) socket opened on FD %d\n", pid, rfd);

    while (1) {
	struct in_addr to;
	char payload[] = "abcdefgh";
	inet_aton("203.56.168.1", &to);
	icmpSendEcho(rfd, to, S_ICMP_ECHO, payload, 8);
	sleep(1);
    }

    exit(0);
}
