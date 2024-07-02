#ifndef	__LUSCA_HIERARCHY_ENTRY_H__
#define	__LUSCA_HIERARCHY_ENTRY_H__

extern void hierarchyLogEntryCopy(HierarchyLogEntry *dst, HierarchyLogEntry *src);
extern void hierarchyNote(HierarchyLogEntry * hl, hier_code code, const char *cache_peer);

#endif
