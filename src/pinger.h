#ifndef	__LUSCA_PINGER_H__
#define	__LUSCA_PINGER_H__

#define PINGER_PAYLOAD_SZ 8192 

struct _pingerEchoData {
    struct in_addr to;
    unsigned char opcode;
    int psize;
    char payload[PINGER_PAYLOAD_SZ];
};

struct _pingerReplyData {
    struct in_addr from;
    unsigned char opcode;
    int rtt;
    int hops;
    int psize;
    char payload[PINGER_PAYLOAD_SZ];
};

typedef struct _pingerEchoData pingerEchoData;
typedef struct _pingerReplyData pingerReplyData;


#endif
