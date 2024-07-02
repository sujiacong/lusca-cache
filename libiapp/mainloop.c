#include "../include/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <string.h>

/*
 * The bulk of the following are because the disk code requires
 * the memory code, which requires everything else.
 */
#include "../include/Array.h"
#include "../include/Stack.h"
#include "../include/util.h"
#include "../libcore/gb.h"
#include "../libcore/kb.h"
#include "../libcore/varargs.h"
#include "../libmem/MemPool.h"
#include "../libmem/MemBufs.h"
#include "../libmem/MemBuf.h"
#include "../libmem/MemStr.h"
#include "../libcb/cbdata.h"

#include "../libsqinet/sqinet.h"

#include "event.h"
#include "iapp_ssl.h"
#include "fd_types.h"
#include "comm_types.h"
#include "comm.h"
#include "disk.h"
#include "mainloop.h"

/*
 * Configure the libraries required to bootstrap iapp.
 */
void
iapp_init(void)
{
	memset(&local_addr, '\0', sizeof(struct in_addr));
	safe_inet_addr("127.0.0.1", &local_addr);
	memset(&no_addr, '\0', sizeof(struct in_addr));
	safe_inet_addr("255.255.255.255", &no_addr);

	memPoolInit();
	memBuffersInit();
	memStringInit();
	cbdataInit();
	eventInit();
	comm_init();
	disk_init();
	comm_select_init();
}

int
iapp_runonce(int msec)
{
	int loop_delay;

	eventRun();
	loop_delay = eventNextTime();
	if (loop_delay < 0)
		loop_delay = 0;
	if (loop_delay > msec)
		loop_delay = msec;
	return comm_select(loop_delay);
}
