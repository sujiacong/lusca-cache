#ifndef	__LIBSQSTORE_REBUILD_ENTRY_H__
#define	__LIBSQSTORE_REBUILD_ENTRY_H__


struct _rebuild_entry {
        storeMetaIndexNew mi;
        char *md5_key;
        char *url;
        char *storeurl;
        squid_file_sz file_size;                /* swap file size - object size + metadata */
        int hdr_size;                           /* metadata size */
        int swap_filen;
};
typedef struct _rebuild_entry rebuild_entry_t;

extern void rebuild_entry_done(rebuild_entry_t *re);
extern void rebuild_entry_init(rebuild_entry_t *re);
extern int parse_header(char *buf, int len, rebuild_entry_t *re);
extern int write_swaplog_entry(FILE *fp, rebuild_entry_t *re);

#endif
