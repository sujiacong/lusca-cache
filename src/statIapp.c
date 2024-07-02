#include "squid.h"

/*
 * Dump out statistics pertaining to the libiapp stuff
 */
void
statIappStats(StoreEntry *sentry)
{
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.accepts = %d\n", CommStats.syscalls.sock.accepts);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.sockets = %d\n", CommStats.syscalls.sock.sockets);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.connects = %d\n", CommStats.syscalls.sock.connects);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.binds = %d\n", CommStats.syscalls.sock.binds);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.closes = %d\n", CommStats.syscalls.sock.closes);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.reads = %d\n", CommStats.syscalls.sock.reads);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.writes = %d\n", CommStats.syscalls.sock.writes);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.recvfroms = %d\n", CommStats.syscalls.sock.recvfroms);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.sock.sendtos = %d\n", CommStats.syscalls.sock.sendtos);

        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.polls = %d\n", CommStats.syscalls.polls);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.selects = %d\n", CommStats.syscalls.selects);

        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.disk.opens = %d\n", CommStats.syscalls.disk.opens);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.disk.closes = %d\n", CommStats.syscalls.disk.closes);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.disk.reads = %d\n", CommStats.syscalls.disk.reads);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.disk.writes = %d\n", CommStats.syscalls.disk.writes);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.disk.seeks = %d\n", CommStats.syscalls.disk.seeks);
        storeAppendPrintf(sentry, "libiapp.commstats.syscalls.disk.unlinks = %d\n", CommStats.syscalls.disk.unlinks);

        storeAppendPrintf(sentry, "libiapp.commstats.select_fds = %d\n", CommStats.select_fds);
        storeAppendPrintf(sentry, "libiapp.commstats.select_loops = %d\n", CommStats.select_loops);
        storeAppendPrintf(sentry, "libiapp.commstats.select_time = %d\n", CommStats.select_time);
}

