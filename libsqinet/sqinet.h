#ifndef	__LIBSQINET_INET_H__
#define	__LIBSQINET_INET_H__

/* Max size of IPv4/IPv6 expanded addresses _PLUS_ the []'s for v6 addresses */
#define	MAX_IPSTRLEN	77

struct _sqaddr {
	int init;
	struct sockaddr_storage st;
};
typedef struct _sqaddr sqaddr_t;

typedef enum {
	SQADDR_NONE = 0x00,
	SQADDR_ASSERT_IS_V4 = 0x01,
	SQADDR_ASSERT_IS_V6 = 0x02,
	SQADDR_NO_BRACKET_V6 = 0x04,
} sqaddr_flags;

typedef enum {
	SQATON_NONE = 0x0,
	SQATON_FAMILY_IPv4 = 0x2,
	SQATON_FAMILY_IPv6 = 0x4,
	SQATON_PASSIVE = 0x8,
} sqaton_flags;

static inline int sqinet_is_init(sqaddr_t *s) { return (s->init); }
extern void sqinet_init(sqaddr_t *s);
extern void sqinet_done(sqaddr_t *s);
extern void sqinet_set_family(sqaddr_t *s, int af_family);
extern int sqinet_set_mask_addr(sqaddr_t *dst, int masklen);
extern int sqinet_copy_v4_inaddr(const sqaddr_t *s, struct in_addr *dst, sqaddr_flags flags);
extern int sqinet_set_v4_inaddr(sqaddr_t *s, struct in_addr *v4addr);
extern int sqinet_set_v4_port(sqaddr_t *s, short port, sqaddr_flags flags);
extern int sqinet_set_v4_sockaddr(sqaddr_t *s, const struct sockaddr_in *v4addr);
extern int sqinet_set_v6_sockaddr(sqaddr_t *s, const struct sockaddr_in6 *v6addr);
extern int sqinet_get_port(const sqaddr_t *s);
extern void sqinet_set_port(const sqaddr_t *s, short port, sqaddr_flags flags);
extern struct in_addr sqinet_get_v4_inaddr(const sqaddr_t *s, sqaddr_flags flags);
extern int sqinet_get_v4_sockaddr_ptr(const sqaddr_t *s, struct sockaddr_in *v4, sqaddr_flags flags);
extern struct sockaddr_in sqinet_get_v4_sockaddr(const sqaddr_t *s, sqaddr_flags flags);
extern void sqinet_set_anyaddr(sqaddr_t *s);
extern int sqinet_is_anyaddr(const sqaddr_t *s);
extern void sqinet_set_noaddr(sqaddr_t *s);
extern int sqinet_is_noaddr(const sqaddr_t *s);
extern int sqinet_ntoa(const sqaddr_t *s, char *hoststr, int hostlen, sqaddr_flags flags);
extern int sqinet_aton(sqaddr_t *s, const char *hoststr, sqaton_flags flags);
extern int sqinet_assemble_rev(const sqaddr_t *s, char *buf, int len);

static inline struct sockaddr * sqinet_get_entry(sqaddr_t *s) { return (struct sockaddr *) &(s->st); }
static inline const struct sockaddr * sqinet_get_entry_ro(const sqaddr_t *s) { return (struct sockaddr *) &(s->st); }
static inline int sqinet_get_family(const sqaddr_t *s) { return s->st.ss_family; }
static inline int sqinet_get_length(const sqaddr_t *s) { if (s->st.ss_family == AF_INET) return sizeof(struct sockaddr_in); else return sizeof(struct sockaddr_in6); }
static inline int sqinet_get_maxlength(const sqaddr_t *s) { return sizeof(s->st); }

static inline int sqinet_copy(sqaddr_t *dst, const sqaddr_t *src) { *dst = *src; return 1; }
extern int sqinet_compare_port(const sqaddr_t *a, const sqaddr_t *b);
extern int sqinet_compare_addr(const sqaddr_t *a, const sqaddr_t *b);

extern void sqinet_mask_addr(sqaddr_t *dst, const sqaddr_t *mask);
extern int sqinet_host_compare(const sqaddr_t *a, const sqaddr_t *b);
extern int sqinet_range_compare(const sqaddr_t *a, const sqaddr_t *b_start, const sqaddr_t *b_end);
extern int sqinet_host_is_netaddr(const sqaddr_t *a, const sqaddr_t *mask);

extern unsigned int sqinet_hash_host_key(const sqaddr_t *a, unsigned int size);

#endif
