#ifndef	__LIBCORE_GB_H__
#define	__LIBCORE_GB_H__

/* Origin: squid/src/typedefs.h ; squid/src/protos.h ; squid/src/defines.h */

typedef struct {
    size_t count;
    size_t bytes;
    size_t gb;
} gb_t;

#define gb_flush_limit (0x3FFFFFFF)
#define gb_inc(gb, delta) { if ((gb)->bytes > gb_flush_limit || delta > gb_flush_limit) gb_flush(gb); (gb)->bytes += delta; (gb)->count++; }

extern void gb_flush(gb_t * g);
extern double gb_to_double(const gb_t * g);
extern const char * gb_to_str(const gb_t * g);

#endif
