#include "squid.h"

#include "client_side_refresh.h"
#include "client_side_ims.h"
#include "client_side.h"

static void
clientRefreshCheckDone(void *data, int fresh, const char *log)
{
    clientHttpRequest *http = data;
    if (log) {
        safe_free(http->al.ext_refresh);
        http->al.ext_refresh = xstrdup(log);
    }
    if (fresh)
        clientProcessHit(http);
    else
        clientProcessExpired(http);
}

void 
clientRefreshCheck(clientHttpRequest * http)
{
    refreshCheckSubmit(http->entry, clientRefreshCheckDone, http);
}
