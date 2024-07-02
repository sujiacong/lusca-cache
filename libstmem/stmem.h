#ifndef	__LIBSTMEM_STMEM_H__
#define	__LIBSTMEM_STMEM_H__

/*
 * "4096" is a common constant in Squid, and I'm not quite sure
 * that they're defined and used consistently.
 */

#define	SM_PAGE_SIZE		4096

typedef struct _mem_node mem_node;
struct _mem_node {
    char data[SM_PAGE_SIZE];
    int len;
    int uses;
    mem_node *next;
};

struct _mem_hdr {
    mem_node *head;
    mem_node *tail;
    squid_off_t origin_offset;
};
typedef struct _mem_hdr mem_hdr;

struct _mem_node_ref {
    struct _mem_node *node;
    /*
     * the caller asked for a specific offset into the object;
     * this particular offset is the offset into the above mem_node
     * to get at said data.
     */
    int offset;
};
typedef struct _mem_node_ref mem_node_ref;

extern unsigned long store_mem_size;

extern void stmemInitMem(void);
extern squid_off_t stmemFreeDataUpto(mem_hdr *, squid_off_t);
extern void stmemAppend(mem_hdr *, const char *, int);
extern ssize_t stmemCopy(const mem_hdr *, squid_off_t, char *, size_t);
extern void stmemFree(mem_hdr *);
extern void stmemFreeData(mem_hdr *);
extern void stmemNodeFree(void *);
extern char *stmemNodeGet(mem_node *);
extern int stmemRef(const mem_hdr * mem, squid_off_t offset, mem_node_ref * r);
extern void stmemNodeUnref(mem_node_ref * r);
extern mem_node_ref stmemNodeRef(mem_node_ref * r);
extern void stmemNodeRefCreate(mem_node_ref * r);

#endif
