#ifndef	__LIBSQTLV_TLV_H__
#define	__LIBSQTLV_TLV_H__

struct _tlv;
typedef struct _tlv tlv;
struct _tlv {
    char type;
    int length;
    void *value;
    tlv *next;
};

extern void tlv_init(void);
extern tlv ** tlv_add(int type, const void *ptr, size_t len, tlv ** tail);
extern void tlv_free(tlv *);
extern tlv * tlv_unpack(const char *buf, int *hdr_len, int max_metaid);

#endif
