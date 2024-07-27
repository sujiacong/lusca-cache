#include "squid.h"
#include "client_side_body.h"
#include "client_side.h"

static void
clientEatRequestBodyHandler(char *buf, ssize_t size, void *data)
{
    clientHttpRequest *http = data;
    ConnStateData *conn = http->conn;
    if (buf && size < 0) {
	return;			/* Aborted, don't care */
    }
    if (conn->in.offset > 0 && conn->body.size_left > 0) {
	conn->body.callback = clientEatRequestBodyHandler;
	conn->body.cbdata = http;
	cbdataLock(conn->body.cbdata);
	conn->body.buf = NULL;
	conn->body.bufsize = SQUID_TCP_SO_RCVBUF;
	clientProcessBody(conn);
        return;
    }

    if (conn->in.offset == 0 && conn->body.size_left != 0) {
        debugs(1, 1, "clientEatRequestBodyHandler: FD %d: no more data left in socket; but request header says there should be; aborting for now", conn->fd);
        return;
    }
    if (http->request->flags.proxy_keepalive) {
        debugs(33, 5, "clientEatRequestBodyHandler: FD %d Keeping Alive", conn->fd);
        clientKeepaliveNextRequest(http);
    } else {
        comm_close(conn->fd);
    }
}

/*!
 * @function
 * 	clientEatRequestBody
 *
 * @abstract
 * 	Consume and discard request body data from the given
 * 	client connection.
 * 
 * @discussion
 * 	This is called to eat all the request body data from a
 * 	client connection. This is used to begin aborting a
 * 	http request w/ a request body.
 *
 * 	The eat body request handler will recursively call
 * 	itself until all incoming data is consumed. Subsequent
 * 	data which arrives on the client connection will be fed to
 * 	clientProcessBody() which will continue to consume
 * 	data until the end of the request.
 *
 * @param	http	client HTTP request to eat the request body of
 */
void
clientEatRequestBody(clientHttpRequest * http)
{
    ConnStateData *conn = http->conn;
    cbdataLock(conn);
    if (conn->body.request)
	requestAbortBody(conn->body.request);
    if (cbdataValid(conn))
	clientEatRequestBodyHandler(NULL, -1, http);
    cbdataUnlock(conn);
}

/* Called by clientReadRequest to process body content */
/*!
 * @function
 * 	clientProcessBody
 *
 * @abstract
 * 	Handle incoming request body data; call the request body
 * 	callback previously configured
 *
 * @discussion
 *
 *	clientProcessBody() assumes that conn->body.cbdata has been
 *	set and has been cbdataRef()'ed. It will derefence the pointer
 *	once the copying has completed and call the relevant callback
 *	if needed.
 *
 * 	clientProcessBody() assumes there is -some- data available.
 * 	It simply ignores processing if called with conn->in.offset == 0.
 * 	This means that conn->body.cbdata won't be unlocked. Calling
 * 	this function with conn.in.offset == 0 and not subsequently
 * 	deref'ing the cbdata will thus cause conn->body.cbdata
 * 	(likely a clientHttpRequest) to leak.
 *
 * @param	conn	The connection to handle request body data for
 */
void
clientProcessBody(ConnStateData * conn)
{
    int size;
    char *buf = conn->body.buf;
    void *cbdata = conn->body.cbdata;
    CBCB *callback = conn->body.callback;
    request_t *request = conn->body.request;
    /* Note: request is null while eating "aborted" transfers */
    debugs(33, 2, "clientProcessBody: start fd=%d body_size=%lu in.offset=%ld cb=%p req=%p", conn->fd, (unsigned long int) conn->body.size_left, (long int) conn->in.offset, callback, request);
#if 0
    if (conn->in.offset == 0) {
	/* This typically will only occur when some recursive call through the body eating path has occured -adrian */
	/* XXX so no need atm to call the callback handler; the original code didn't! -adrian */
	debugs(33, 1, "clientProcessBody: cbdata %p: would've leaked; conn->in.offset=0 here", cbdata);
	cbdataUnlock(conn->body.cbdata);
	conn->body.cbdata = conn->body.callback = NULL;
	return;
    }
#endif
    if (conn->in.offset) {
	int valid = cbdataValid(conn->body.cbdata);
	if (!valid) {
	    comm_close(conn->fd);
	    return;
	}
	/* Some sanity checks... */
	assert(conn->body.size_left > 0);
	assert(conn->in.offset > 0);
	assert(callback != NULL);
	/* How much do we have to process? */
	size = conn->in.offset;
	if (size > conn->body.size_left)	/* only process the body part */
	    size = conn->body.size_left;
	if (size > conn->body.bufsize)	/* don't copy more than requested */
	    size = conn->body.bufsize;
	if (valid && buf)
	    xmemcpy(buf, conn->in.buf, size);
	conn->body.size_left -= size;
	/* Move any remaining data */
	conn->in.offset -= size;
	/* Resume the fd if necessary */
	if (conn->in.offset < conn->in.size - 1)
	    commResumeFD(conn->fd);
	if (conn->in.offset > 0)
	    xmemmove(conn->in.buf, conn->in.buf + size, conn->in.offset);
	/* Remove request link if this is the last part of the body, as
	 * clientReadRequest automatically continues to process next request */
	if (conn->body.size_left <= 0 && request != NULL) {
	    request->body_reader = NULL;
	    if (request->body_reader_data)
		cbdataUnlock(request->body_reader_data);
	    request->body_reader_data = NULL;
	}
	/* Remove clientReadBody arguments (the call is completed) */
	conn->body.request = NULL;
	conn->body.callback = NULL;
	cbdataUnlock(conn->body.cbdata);
	conn->body.cbdata = NULL;
	conn->body.buf = NULL;
	conn->body.bufsize = 0;
	/* Remember that we have touched the body, not restartable */
	if (request != NULL)
	    request->flags.body_sent = 1;
	/* Invoke callback function */
	if (valid)
	    callback(buf, size, cbdata);
	if (request != NULL)
	    requestUnlink(request);	/* Linked in clientReadBody */
	debugs(33, 2, "clientProcessBody: end fd=%d size=%d body_size=%lu in.offset=%ld cb=%p req=%p", conn->fd, size, (unsigned long int) conn->body.size_left, (long int) conn->in.offset, callback, request);
    }
}

/* Abort a body request */
static void
clientAbortBody(request_t * request)
{
    ConnStateData *conn = request->body_reader_data;
    char *buf;
    CBCB *callback;
    void *cbdata;
    int valid;
    if (!conn || !cbdataValid(conn))
	return;
    if (!conn->body.callback || conn->body.request != request)
	return;
    buf = conn->body.buf;
    callback = conn->body.callback;
    cbdata = conn->body.cbdata;
    valid = cbdataValid(cbdata);
    assert(request == conn->body.request);
    conn->body.buf = NULL;
    conn->body.callback = NULL;
    cbdataUnlock(conn->body.cbdata);
    conn->body.cbdata = NULL;
    conn->body.request = NULL;
    if (valid)
	callback(buf, -1, cbdata);	/* Signal abort to clientReadBody caller to allow them to clean up */
    else
	debugs(33, 1, "NOTICE: A request body was aborted with cancelled callback: %p, possible memory leak", callback);
    requestUnlink(request);	/* Linked in clientReadBody */
}

/* file_read like function, for reading body content */
void
clientReadBody(request_t * request, char *buf, size_t size, CBCB * callback, void *cbdata)
{   
    ConnStateData *conn = request->body_reader_data;
    if (!callback) {
        clientAbortBody(request);
        return;
    }
    if (!conn) {
        debugs(33, 5, "clientReadBody: no body to read, request=%p", request);
        callback(buf, 0, cbdata);       /* Signal end of body */
        return;
    }
    assert(cbdataValid(conn));   
    debugs(33, 2, "clientReadBody: start fd=%d body_size=%lu in.offset=%ld cb=%p req=%p", conn->fd, (unsigned long int) conn->body.size_left, (long int) conn->in.offset, callback, request);
    conn->body.callback = callback;
    conn->body.cbdata = cbdata;
    cbdataLock(conn->body.cbdata);
    conn->body.buf = buf;
    conn->body.bufsize = size;
    conn->body.request = requestLink(request);
    clientProcessBody(conn);
}
