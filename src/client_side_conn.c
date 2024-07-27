#include "squid.h"
#include "client_db.h"

#include "client_side_conn.h"

static int clientside_num_conns = 0;

CBDATA_TYPE(ConnStateData);

/* This is a handler normally called by comm_close() */
static void
connStateFree(int fd, void *data)
{
    ConnStateData *connState = data;
    dlink_node *n;
    clientHttpRequest *http;
    debugs(33, 3, "connStateFree: FD %d", fd);
    assert(connState != NULL);
    clientdbEstablished(connState->peer.sin_addr, -1);	/* decrement */
    n = connState->reqs.head;
    while (n != NULL) {
	http = n->data;
	n = n->next;
	assert(http->conn == connState);
	httpRequestFree(http);
    }
    if (connState->auth_user_request)
	authenticateAuthUserRequestUnlock(connState->auth_user_request);
    connState->auth_user_request = NULL;
    authenticateOnCloseConnection(connState);
    memFreeBuf(connState->in.size, connState->in.buf);
    pconnHistCount(0, connState->nrequests);
    if (connState->pinning.fd >= 0)
	comm_close(connState->pinning.fd);
    cbdataUnlock(connState->port);
    cbdataFree(connState);
    clientside_num_conns--;
#ifdef _SQUID_LINUX_
    /* prevent those nasty RST packets */
    {
	char buf[SQUID_TCP_SO_RCVBUF];
	while (FD_READ_METHOD(fd, buf, SQUID_TCP_SO_RCVBUF) > 0);
    }
#endif
}

ConnStateData *
connStateCreate(int fd, sqaddr_t *peer, sqaddr_t *me)
{
        ConnStateData *connState = NULL;

        CBDATA_INIT_TYPE(ConnStateData);
        connState = cbdataAlloc(ConnStateData);
        clientside_num_conns++;
        sqinet_get_v4_sockaddr_ptr(peer, &connState->peer, SQADDR_ASSERT_IS_V4);
        connState->log_addr = connState->peer.sin_addr;
        connState->log_addr.s_addr &= Config.Addrs.client_netmask.s_addr;
        sqinet_get_v4_sockaddr_ptr(me, &connState->me, SQADDR_ASSERT_IS_V4);
        connState->fd = fd;
        connState->pinning.fd = -1;
        connState->in.buf = memAllocBuf(CLIENT_REQ_BUF_SZ, &connState->in.size);
        comm_add_close_handler(fd, connStateFree, connState);

        return connState;
}

int
connStateGetCount(void)
{
        return clientside_num_conns;
}
