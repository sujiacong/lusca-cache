#ifndef	__LUSCA_FILEMAP_H__
#define	__LUSCA_FILEMAP_H__

/* swap_filen is 25 bits, signed */
#define FILEMAP_MAX_SIZE (1<<24)
#define FILEMAP_MAX (FILEMAP_MAX_SIZE - 65536)

struct _fileMap {
    int max_n_files;
    int n_files_in_map;
    int toggle;
    int nwords;
    unsigned long *file_map; 
};  
typedef struct _fileMap fileMap;

extern fileMap *file_map_create(void);
extern int file_map_allocate(fileMap *, int);
extern int file_map_bit_set(fileMap *, int);
extern int file_map_bit_test(fileMap *, int);
extern void file_map_bit_reset(fileMap *, int);
extern void filemapFreeMemory(fileMap *);

#endif
