
#ifndef SQUID_HTTP_GZIP_H
#define SQUID_HTTP_GZIP_H

#include "squid.h"
#include <zlib.h>


#define GZIP_ERROR (-1)
#define GZIP_OK    (0)
#define GZIP_SYNC  (1)

typedef struct {
    z_stream        zstream;
    unsigned char  *gzipBuffer;
    unsigned int    checksum;
    unsigned int    allocated;
    unsigned int    originalSize;
    unsigned int    compressedSize;
    unsigned int    sendingOffset;
    unsigned int    lastChunkSize;

    int             compression_type;
    int             flush;  
} HttpGzipContext;

int httpGzipNeed(request_t *request, HttpReply *reply);

void httpGzipClearHeaders(HttpReply *reply, int type);

void httpGzipStart(HttpStateData *httpState);

void * httpGzipContextInitialize();

void httpGzipContextFinalize(HttpStateData *httpState);

int httpGzipCompress(HttpStateData *httpState, const char *buf, ssize_t len);

int httpGzipStreamOutReset(HttpGzipContext *ctx);

int httpGzipDone(HttpStateData *httpState);

#endif /* SQUID_HTTP_GZIP_H */
