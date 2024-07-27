#ifndef	__LUSCA_CLIENT_DB_H__
#define	__LUSCA_CLIENT_DB_H__

extern void clientdbInitMem(void);
extern void clientdbInit(void);
extern void clientdbUpdate(struct in_addr, log_type, protocol_t, squid_off_t);
extern int clientdbCutoffDenied(struct in_addr);
extern void clientdbDump(StoreEntry *,void* data);
extern void clientdbFreeMemory(void);
extern int clientdbEstablished(struct in_addr, int);

#endif
