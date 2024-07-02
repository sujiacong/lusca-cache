
#include "squid.h"

void
hierarchyLogEntryCopy(HierarchyLogEntry *dst, HierarchyLogEntry *src)
{
	memcpy(dst, src, sizeof(HierarchyLogEntry));
}

void
hierarchyNote(HierarchyLogEntry * hl,
    hier_code code,
    const char *cache_peer)
{
    assert(hl != NULL);
    hl->code = code;
    xstrncpy(hl->host, cache_peer, SQUIDHOSTNAMELEN);
}
