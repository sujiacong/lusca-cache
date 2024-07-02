
#include "HttpGzip.h"
#include <stdlib.h>


int 
httpGzipNeed(request_t *request, HttpReply *reply)
{
    char *type_list = "text/html";

    String s;
    HttpHeader *header;

    header = &request->header;

    /**
     * Checks the request's Accept-Encoding header if the client does understand
     * gzip compression at all.
     */
    s = httpHeaderGetStrOrList(header, HDR_ACCEPT_ENCODING);
    if (strBuf(s) == NULL ||
	    (strStr(s, "gzip") == NULL && strStr(s, "deflate") == NULL) ){
	return 0;
    }

    header = &reply->header;
    /**
     * Checks if the response Cache-Control header allows transformation of the
     * response.
     */
    s = httpHeaderGetStrOrList(header, HDR_CACHE_CONTROL);
    if (strBuf(s) != NULL && strStr(s, "no-transform") != NULL ){
	return 0;
    }

    /**
     * Do not compress if response has a content-range header and status code "206
     * Partial content". 
     */
    if (httpHeaderHas(header, HDR_CONTENT_RANGE)) {
	return 0;
    }

    /**
     * Checks the Content-Type response header.
     * At this time, only responses with "text/html" content-type are allowed to
     * be compressed.
     */
    if (Config.http_gzip.types != NULL) {
	type_list = Config.http_gzip.types;
    }

    s = httpHeaderGetStrOrList(header, HDR_CONTENT_TYPE);
    if (strBuf(s) == NULL || strstr(type_list, strBuf(s)) == NULL ){
	return 0;
    }

    /**
     * Checks the Content-Encoding response header.
     * If this header is present, we must not compress the respone.
     */
    if (httpHeaderHas(header, HDR_CONTENT_ENCODING)) {
	return 0;
    }

    if (reply->content_length != -1 && 
	    reply->content_length < Config.http_gzip.min_length) {
	return 0;
    }

    return 1;
}

void 
httpGzipClearHeaders(HttpReply *reply, int type) 
{
    String s;
    HttpHeader *header;
    header = &reply->header;

    /* delete ContentLength header because we may change the length */
    reply->content_length = -1;
    httpHeaderDelById(header, HDR_CONTENT_LENGTH);

    /* Add "Vary: Accept-Encoding" response header" */
    s = httpHeaderGetStrOrList(header, HDR_VARY);
    if (strBuf(s) == NULL || strStr(s, "Accept-Encoding") == NULL ){
	httpHeaderPutStr(header, HDR_VARY, "Accept-Encoding");
    }

    httpHeaderDelById(header, HDR_CONTENT_LOCATION);

    httpHeaderDelById(header, HDR_ETAG);

    if (type & SQUID_CACHE_GZIP) {
	httpHeaderPutStr(header, HDR_CONTENT_ENCODING, "gzip");
    }
    else if (type & SQUID_CACHE_DEFLATE) {
	httpHeaderPutStr(header, HDR_CONTENT_ENCODING, "deflate");
    }

    httpHeaderPutStr(header, HDR_WARNING, "214 Transformation applied");

    return;
}

void 
httpGzipStart(HttpStateData *httpState)
{
    StoreEntry *entry = httpState->entry;
    request_t *request = httpState->orig_request;
    HttpReply *reply = entry->mem_obj->reply;
    HttpGzipContext *ctx = NULL;

    if (!httpGzipNeed(request, reply)) {
	return;
    }

    ctx = httpGzipContextInitialize(request);

    if (ctx) {
	httpGzipClearHeaders(reply, ctx->compression_type);
	entry->compression_type = ctx->compression_type;
    }

    httpState->context = ctx;

    return;
}

void * 
httpGzipContextInitialize(request_t *request)
{
    String s;
    int gzip, deflate;
    HttpGzipContext *ctx = NULL;
    HttpHeader *header;


    ctx = (HttpGzipContext *) xmalloc(sizeof(HttpGzipContext));

    if (ctx) {
	memset(ctx, 0, sizeof(HttpGzipContext));

	gzip = deflate = 0;
	header = &request->header;
	s = httpHeaderGetStrOrList(header, HDR_ACCEPT_ENCODING);
	if ( strStr(s, "gzip") != NULL) {
	    gzip = 1;
	} 

	if (strStr(s, "deflate") != NULL){
	    deflate = 1;
	}

	if (deflate && Config.http_gzip.prefer_deflate) {
	    ctx->compression_type = SQUID_CACHE_DEFLATE;
	}
	else if (gzip && Config.http_gzip.prefer_gzip) {
	    ctx->compression_type = SQUID_CACHE_GZIP;
	}
	else {
	    /*This could not happen.*/
	    return NULL;
	}

	/*
	 * We preallocate a memory for zlib in one buffer (default:256K), this
	 * decreases a number of malloc() and free() calls and also probably
	 * decreases a number of syscalls (sbrk()/mmap() and so on).
	 * Besides we free the memory as soon as a gzipping will complete
	 * and do not wait while a whole response will be sent to a client.
	 */
	ctx->allocated = Config.http_gzip.prealloc_size;
	ctx->gzipBuffer = (unsigned char *) xmalloc(ctx->allocated);
	if (ctx->gzipBuffer == NULL) {
	    return NULL;
	}

	if (deflateInit2(&ctx->zstream, Config.http_gzip.level, Z_DEFLATED, 
		    -(Config.http_gzip.wbits), Config.http_gzip.memlevel, 
		    Z_DEFAULT_STRATEGY) != Z_OK){
	    return NULL;
	}

	if (!(ctx->compression_type & SQUID_CACHE_DEFLATE)) {
	    ctx->checksum = crc32(0, Z_NULL, 0);

	    ctx->gzipBuffer[0] = (unsigned char)31;         //Magic number #1
	    ctx->gzipBuffer[1] = (unsigned char)139;        //Magic number #2
	    ctx->gzipBuffer[2] = (unsigned char)Z_DEFLATED; //Method
	    ctx->gzipBuffer[3] = (unsigned char)0;          //Flags
	    ctx->gzipBuffer[4] = (unsigned char)0;	    //Mtime #1
	    ctx->gzipBuffer[5] = (unsigned char)0;	    //Mtime #2
	    ctx->gzipBuffer[6] = (unsigned char)0;          //Mtime #3
	    ctx->gzipBuffer[7] = (unsigned char)0;          //Mtime #4
	    ctx->gzipBuffer[8] = (unsigned char)0;          //Extra flags
	    ctx->gzipBuffer[9] = (unsigned char)3;          //Operatin system: UNIX

	    ctx->zstream.total_out = 10;
	}

	ctx->flush = Z_NO_FLUSH;
    }

    return ctx;
}

void 
httpGzipContextFinalize(HttpStateData *httpState)
{
    HttpGzipContext *ctx = (HttpGzipContext *)httpState->context;

    if (ctx != NULL) {
	if (ctx->gzipBuffer != NULL) {
	    xfree(ctx->gzipBuffer);
	}

	xfree(httpState->context);
    }

    httpState->context = NULL;
}

static int 
httpGzipIncreaseBuffer(HttpGzipContext *ctx)
{
    ctx->allocated <<= 1;
    ctx->gzipBuffer = (unsigned char *) xrealloc(ctx->gzipBuffer, ctx->allocated);
    if (ctx->gzipBuffer == NULL) {
	return GZIP_ERROR;
    }

    return 0;
}

int
httpGzipStreamOutReset(HttpGzipContext *ctx) 
{
    ctx->compressedSize += ctx->zstream.total_out;
    ctx->zstream.total_out = 0;

    return 0;
}

int
httpGzipCompress(HttpStateData *httpState, const char *buf, ssize_t len)
{
    HttpGzipContext *ctx = httpState->context;

    ctx->originalSize += len;
    ctx->lastChunkSize = len;

    if (!(ctx->compression_type & SQUID_CACHE_DEFLATE)) {
	ctx->checksum = crc32(ctx->checksum, (unsigned char *)buf, len);
    }

    ctx->zstream.next_in    = (unsigned char *)buf;
    ctx->zstream.avail_in   = len;

    while (1) {
	ctx->zstream.next_out   = &ctx->gzipBuffer[ctx->zstream.total_out];
	ctx->zstream.avail_out  = ctx->allocated - ctx->zstream.total_out;

	if (deflate(&ctx->zstream, ctx->flush) != Z_OK) {
	    return GZIP_ERROR;
	}

	if (ctx->zstream.avail_out == 0) {
	    if (httpGzipIncreaseBuffer(ctx) < 0) {
		return GZIP_ERROR;
	    }

	    continue;
	}

	if (ctx->flush == Z_SYNC_FLUSH) {
	    ctx->flush = Z_NO_FLUSH;
	    return GZIP_SYNC;
	}

	/*flush the buffer when total_out > 3/4 * allocated's buffer.*/
	if (ctx->zstream.total_out > ((ctx->allocated * 3) >> 2 )) {
	    ctx->flush = Z_SYNC_FLUSH;
	    continue;
	}

	break;
    }

    return GZIP_OK;
}

int
httpGzipDone(HttpStateData *httpState)
{
    int rc;
    StoreEntry *entry = httpState->entry;
    HttpReply *reply = entry->mem_obj->reply;
    HttpGzipContext *ctx = httpState->context;

    while (1) {
	ctx->zstream.next_out   = &ctx->gzipBuffer[ctx->zstream.total_out];
	ctx->zstream.avail_out  = ctx->allocated - ctx->zstream.total_out;

	rc = deflate(&ctx->zstream, Z_FINISH);

	if (rc == Z_STREAM_END) {
	    break;
	}
	else if (rc == Z_OK ){
	    if (ctx->zstream.avail_out == 0) {
		if (httpGzipIncreaseBuffer(ctx) < 0) {
		    return GZIP_ERROR;
		}

	    }

	    continue;
	}
	else {
	    return GZIP_ERROR;
	}
    }

    if (deflateEnd(&ctx->zstream) != Z_OK) {
	return GZIP_ERROR;
    }

    /*GZIP Footer*/
    if (!(ctx->compression_type & SQUID_CACHE_DEFLATE)) {
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) ctx->checksum & 0xff;
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) (ctx->checksum >> 8) & 0xff;
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) (ctx->checksum >> 16)  & 0xff;
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) (ctx->checksum >> 24)  & 0xff;

	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) ctx->originalSize & 0xff;
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) (ctx->originalSize >> 8) & 0xff;
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) (ctx->originalSize >> 16) & 0xff;
	ctx->gzipBuffer[ctx->zstream.total_out++] = (unsigned char) (ctx->originalSize >> 24) & 0xff;
    }

    ctx->compressedSize += ctx->zstream.total_out;

    reply->content_length = ctx->compressedSize;
    httpHeaderPutSize(&reply->header, HDR_CONTENT_LENGTH, reply->content_length);

    return GZIP_OK;
}
