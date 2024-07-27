#include <arpa/inet.h>
#include <errno.h>
#include <sys/un.h>
#include "ipcsupport.h"
#include "../src/squid.h"


static const char *FdNotes[fdnEnd] = {
        "None", // fdnNone
        "HTTP Socket", // fdnHttpSocket
        "HTTPS Socket", // fdnHttpsSocket
        "FTP Socket", // fdnFtpSocket
#if SQUID_SNMP
        "Incoming SNMP Socket", // fdnInSnmpSocket
        "Outgoing SNMP Socket", // fdnOutSnmpSocket
#endif
        "Incoming ICP Socket", // fdnInIcpSocket
        "Incoming HTCP Socket" // fdnInHtcpSocket
};

/// Strand location details
typedef struct _StrandCoord
{
	void* msgid; ///< map to sender TypedMsgHdr struct
    int kidId; 	 ///< internal Squid process number
    pid_t pid; 	 ///< OS process or thread identifier
    char* tag;   ///< optional unique well-known key (e.g., cache_dir path)
}StrandCoord;

typedef struct _IpcMgrRequest
{
	void* msgid; 								///< map to sender TypedMsgHdr struct
    int requestorId; 							///< kidId of the requestor; used for response destination
    unsigned int requestId; 					///< unique for sender; matches request w/ response				
	int conn_fd;
    StrandCoord strands[MAX_KID_SUPPORT];	 	///< all registered strands;
    int strandnumer; 
	int strandindex;
	dlink_node node;
	ActionParams* 		 params;
	action_table* 		 action;
	void* 		 		 actiondata;
	StoreEntry*			entry;
	store_client*		sc;
	int					writeOffset;
}IpcMgrRequest;


typedef struct _IpcSnmpRequest
{
	void* msgid;				///< map to sender TypedMsgHdr struct
	int requestorId;
	unsigned int requestId; 	///< unique for sender; matches request w/ response
	int conn_fd;
    StrandCoord strands[MAX_KID_SUPPORT];	 	///< all registered strands;
    int strandnumer; 
	int strandindex;
	dlink_node node;
	struct sockaddr_in* address;
	struct snmp_pdu* pdu;
	struct snmp_session* session;
	struct snmp_pdu* aggrpdu;
	int	aggrCount;
}IpcSnmpRequest;

typedef struct _IpcSnmpResponse
{
	void* msgid;				///< map to sender TypedMsgHdr struct
	unsigned int requestId; 	///< unique for sender; matches request w/ response
	int	havepdu;
	struct snmp_pdu* pdu;
}IpcSnmpResponse;


typedef struct _OpenListenerParams
{
    // bits to re-create the fde entry
    int sock_type;
    int proto;
    int fdNote; 		///< index into fd_note() comment strings
   
    sqaddr_t addr; 		///< will be memset and memcopied
    comm_flags_t flags;
}OpenListenerParams;

typedef struct _SharedListenRequest
{
	void* msgid;				///< map to sender TypedMsgHdr struct
	int requestorId; 			///< kidId of the requestor
	void* mapId; 				///< to map future response to the requestor's callback
	OpenListenerParams params;	///< socket open params
	dlink_node node;
	void* data;
	LISTEN* callback;
}SharedListenRequest;

typedef struct _SharedListenResponse
{
	void* msgid;				///< map to sender TypedMsgHdr struct
	unsigned int requestorId;  	///< kidId of the requestor
    int fd; 					///< opened listening socket or -1
    int errNo; 					///< errno value from comm_open_sharedListen() call
    void* mapId; 				///< to map future response to the requestor's callback
}SharedListenResponse;

typedef struct _IpcMgrResponse
{
	void* msgid;				///< map to sender TypedMsgHdr struct
	unsigned int requestId; 	///< ID of request we are responding to
	char* actionName; 			///< action name (and credentials realm)
	void* actiondata;
	int   actiondatalen;
}IpcMgrResponse;

typedef struct _ListenList
{
	dlink_node link;
	int fd;
	sqaddr_t  addr;
}ListenList;

typedef struct _Coordinator
{
	struct sockaddr_un address; 				///< UDS address from path; treat as read-only
	int options; 								///< UDS options
	int conn_fd; 								///< UDS descriptor
	int kidsfd[MAX_KID_SUPPORT];				///< UDS for kid fd
	dlink_list  ipcmessages[MAX_KID_SUPPORT];	///< all registered strands messages;
	TypedMsgHdr msgbuf; 						///< msghdr struct filled by Comm 
	dlink_list listeners;						///< cached comm_open_listener() results	
	dlink_list TheRequestsList;
    unsigned int LastRequestId; 				///< last requestId used
    StrandCoord strands[MAX_KID_SUPPORT];	 	///< all registered strands;
    int strandnumer;    
}Coordinator;

typedef struct _Strand
{
	int options; 							///< UDS options
	int conn_fd; 							///< UDS descriptor
	int coordinatorfd;						///< UDS descriptor for coordinator
	struct sockaddr_un address; 			///< UDS address from path; treat as read-only
	TypedMsgHdr msgbuf; 					///< msghdr struct filled by Comm
	int 	isRegistered; 					///< whether Coordinator ACKed registration (unused)
	int concurrencyLimit;					///< concurrency limit
	
	dlink_list TheRequestsList;
	int 	TheRequestsListSize;
	int 	lastRequestId;
	dlink_list delayrequests;
	dlink_list ipcmessages;
	
    int kidId; ///< internal Squid process number
    pid_t pid; ///< OS process or thread identifier
    char* tag; ///< optional unique well-known key (e.g., cache_dir path)
}Strand;


typedef struct _InquirerResponse
{
	StoreEntry* entry;
	store_client *sc;
	squid_off_t offset;
	squid_off_t size;
}InquirerResponse;


#define PACK_SIMPLE(msg,a) \
	assert(sizeof(a) <= sizeof(msg->data.raw) - msg->data.size);\
	memcpy(msg->data.raw + msg->data.size, &a, sizeof(a));\
	msg->data.size += sizeof(a);

#define PACK_STR(msg,a) \
	{\
	unsigned int _length;\
	assert(sizeof(unsigned int) <= sizeof(msg->data.raw) - msg->data.size);\
	if(a && a[0]){\
	_length = strlen(a);\
	} else {_length = 0;}\
	memcpy(msg->data.raw + msg->data.size, &_length, sizeof(unsigned int));\
	msg->data.size += sizeof(unsigned int);\
	if(_length > 0)\
	{\
	assert(_length <= sizeof(msg->data.raw) - msg->data.size);\
	memcpy(msg->data.raw + msg->data.size, a, _length);\
	msg->data.size += _length;\
	}\
	}

#define PACK_STRUCT(msg,a) \
	{\
	assert(sizeof(unsigned int) <= sizeof(msg->data.raw) - msg->data.size);\
	unsigned int _length = sizeof(a);\
	memcpy(msg->data.raw + msg->data.size, &_length, sizeof(unsigned int));\
	msg->data.size += sizeof(unsigned int);\
	assert(_length <= sizeof(msg->data.raw) - msg->data.size);\
	memcpy(msg->data.raw + msg->data.size, &a, _length);\
	msg->data.size += _length;\
	}

#define PACK_PSTRUCT(msg,a) \
	{\
	assert(sizeof(unsigned int) <= sizeof(msg->data.raw) - msg->data.size);\
	unsigned int _length = sizeof(*a);\
	memcpy(msg->data.raw + msg->data.size, &_length, sizeof(unsigned int));\
	msg->data.size += sizeof(unsigned int);\
	assert(_length <= sizeof(msg->data.raw) - msg->data.size);\
	memcpy(msg->data.raw + msg->data.size, a, _length);\
	msg->data.size += _length;\
	}

#define PACK_FIXED(msg,a,l) \
	{\
	assert(l <= sizeof(msg->data.raw) - msg->data.size);\
	memcpy(msg->data.raw + msg->data.size, a, l);\
	msg->data.size += l;\
	}


#define UNPACK_SIMPLE(msg,a) \
	assert(sizeof(a) <= msg->data.size - msg->offset);\
	memcpy(&a, msg->data.raw + msg->offset, sizeof(a));\
	msg->offset += sizeof(a);

#define UNPACK_STR(msg,a) \
	{\
	assert(sizeof(unsigned int) <= msg->data.size - msg->offset);\
	unsigned int _length = 0;\
	char** pstr = a;\
	memcpy(&_length, msg->data.raw + msg->offset, sizeof(int));\
	msg->offset += sizeof(int);\
	if(_length > 0)\
		{\
	*pstr = xcalloc(1, _length + 1);\
	assert(_length <= msg->data.size - msg->offset);\
	memcpy(*pstr, msg->data.raw + msg->offset, _length);\
	msg->offset += _length;\
		} else {\
		*pstr = NULL;}\
	}

#define UNPACK_STRUCT(msg,a) \
	{\
	assert(sizeof(unsigned int) <= msg->data.size - msg->offset);\
	unsigned int _length = 0;\
	memcpy(&_length, msg->data.raw + msg->offset, sizeof(int));\
	msg->offset += sizeof(unsigned int);\
	assert(_length == sizeof(a));\
	assert(_length <= msg->data.size - msg->offset);\
	memcpy(&a, msg->data.raw + msg->offset, sizeof(a));\
	msg->offset += sizeof(a);\
	}

#define UNPACK_PSTRUCT(msg,a) \
	{\
	assert(sizeof(unsigned int) <= msg->data.size - msg->offset);\
	unsigned int _length = 0;\
	memcpy(&_length, msg->data.raw + msg->offset, sizeof(int));\
	msg->offset += sizeof(unsigned int);\
	assert(_length == sizeof(**a));\
	assert(_length <= msg->data.size - msg->offset);\
	*a = xcalloc(1, sizeof(**a));\
	memcpy(*a, msg->data.raw + msg->offset, sizeof(**a));\
	msg->offset += sizeof(**a);\
	}

#define UNPACK_FIXED(msg,a,l) \
	assert(l <= msg->data.size - msg->offset);\
	memcpy(a, msg->data.raw + msg->offset, l);\
	msg->offset += l;


Coordinator* TheCoordinatorInstance = NULL;
Strand* TheStrandInstance = NULL;

const char coordinatorAddr[] = DEFAULT_STATEDIR "/coordinator.ipc";
const char strandAddrPfx[] = DEFAULT_STATEDIR "/kid";
static const char channelPathPfx[] = DEFAULT_STATEDIR "/";
static const char coordinatorAddrLabel[] = "-coordinator";
const char strandAddrLabel[] =  "-kid";

extern action_table *ActionTable;

CBDATA_TYPE(IpcMgrRequest);
CBDATA_TYPE(TypedMsgHdr);
CBDATA_TYPE(IpcSnmpRequest);

void StrandSendListenRequest(SharedListenRequest* request);


const char* IpcFdNote(int fdNoteId)
{
    if (fdnNone < fdNoteId && fdNoteId < fdnEnd)
        return FdNotes[fdNoteId];

    debugs(54, DBG_IMPORTANT,"salvaged bug: wrong fd_note ID: %d", fdNoteId);
    return FdNotes[fdnNone];
}

void IpcRegisterTimeout(void* data)
{
	  fatalf("kid%d registration timed out", KidIdentifier);
}

void IpcListenFdTimeout(void* data)
{
	  fatalf("kid%d Listen Fd timed out", KidIdentifier);
}

struct sockaddr_un PathToAddress(const char* pathAddr)
{
    struct sockaddr_un unixAddr;
    memset(&unixAddr, 0, sizeof(unixAddr));
    unixAddr.sun_family = AF_LOCAL;
    xstrncpy(unixAddr.sun_path, pathAddr, sizeof(unixAddr.sun_path));
    return unixAddr;
}

const char* GetStrandAddr(int id)
{
	static int init = 0;
	static char* strandAddr[MAX_KID_SUPPORT];
	if(!init)
	{
	   memset(strandAddr, 0, sizeof(strandAddr));
	   init = 1;
	}
	assert(id > 0 && id < MAX_KID_SUPPORT);
	if (!strandAddr[id]) 
	{
		 strandAddr[id] = xcalloc(1, 1024);
		 
		 strcat(strandAddr[id], channelPathPfx);
		 strcat(strandAddr[id], APP_SHORTNAME);
		 strcat(strandAddr[id], strandAddrLabel);  
		 strcat(strandAddr[id], xitoa(id));
		 strcat(strandAddr[id], ".ipc");
	}
	
    return strandAddr[id];
}

void PrintSnmpOid(struct snmp_pdu* PDU)
{
	variable_list * Var = PDU->variables;

	debugs(54, 6, "cmd:%d",PDU->command);

	while(Var!=NULL)
	{
		snmpDebugOid(5, Var->name, Var->name_length);
		
		Var = Var->next_variable;
	}
}

static action_table *findAction(const char *action)
{
    action_table *a;
    for (a = ActionTable; a != NULL; a = a->next) {
	if (0 == strcmp(a->action, action))
	    return a;
    }
    return NULL;
}

void IpcFreeMessage(void* data)
{
	debugs(54, 4, "free %p",data);
}

void IpcFreeIpcMgrRequest(void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*) data;

	debugs(54, 4, "free %p",data);

	if(request->params)
	{
		xfree(request->params->url);
		xfree(request->params->origin);
		xfree(request->params->action);
		xfree(request->params->user_name);
		xfree(request->params->passwd);
		xfree(request->params->params.param);
		request->params = NULL;
	}

	if(request->actiondata)
	{
		xfree(request->actiondata);
		request->actiondata = NULL;
	}		
}

void IpcFreeSnmpRequest(void* data)
{
	IpcSnmpRequest* request = (IpcSnmpRequest*) data;

	debugs(54,9,"free %p", request);

	if(request->pdu)
	{
		snmp_pdu_free(request->pdu);
		request->pdu = NULL;
	}
	
	if(request->aggrpdu)
	{
		snmp_pdu_free(request->aggrpdu);
		request->aggrpdu = NULL;
		request->aggrCount = 0;
	}

	if(request->session)
	{
		xfree(request->session->community);
		xfree(request->session->peername);
		xfree(request->session);
		request->session = NULL;
	}
}

static int initSendMsgHdr(TypedMsgHdr* msg, const char* unixAddr)
{
	memset(msg, 0, sizeof(TypedMsgHdr));
	msg->name.sun_family = PF_UNIX;
	strcpy(msg->name.sun_path,unixAddr);
	
	msg->msg_name = &msg->name;
	msg->msg_namelen = sizeof(struct sockaddr_un);
	  
    msg->msg_iovlen = 1;
    msg->msg_iov = msg->ios;
    msg->ios[0].iov_base = &msg->data;
    msg->ios[0].iov_len = sizeof(msg->data);
    msg->data.type = 0;
    msg->data.size = 0;
	return 0;
}

static int initReceivedMsgHdr(TypedMsgHdr* msg, const char* unixAddr)
{
	memset(msg, 0, sizeof(TypedMsgHdr));
	msg->name.sun_family = PF_UNIX;
	strcpy(msg->name.sun_path,unixAddr);
	
	msg->msg_name = &msg->name;
	msg->msg_namelen = sizeof(struct sockaddr_un);
	  
    msg->msg_iovlen = 1;
    msg->msg_iov = msg->ios;
    msg->ios[0].iov_base = &msg->data;
    msg->ios[0].iov_len = sizeof(msg->data);
    msg->data.type = 0;
    msg->data.size = 0;

    msg->msg_control = &msg->ctrl;
    msg->msg_controllen = sizeof(msg->ctrl);
	
	return 0;
}



void initIpcStrandInstance()
{
	if(!TheStrandInstance)
	{
		debugs(54, 4, "TheStrandInstance");
		TheStrandInstance = xcalloc(1, sizeof(Coordinator));
		TheStrandInstance->conn_fd = -1;
		TheStrandInstance->concurrencyLimit = 1;
		TheStrandInstance->options = COMM_NONBLOCKING | COMM_DOBIND;
		TheStrandInstance->address.sun_family = PF_UNIX;
		TheStrandInstance->kidId = KidIdentifier;
		TheStrandInstance->pid = getpid();
		TheStrandInstance->conn_fd = -1;
		TheStrandInstance->coordinatorfd = -1;
	}
}

void initIpcCoordinatorInstance()
{
	if(!TheCoordinatorInstance)
	{
		int i;
		
		debugs(54, 4, "TheCoordinatorInstance");
		TheCoordinatorInstance = (Coordinator*)xcalloc(1, sizeof(Coordinator));
		TheCoordinatorInstance->conn_fd = -1;
		TheCoordinatorInstance->options = COMM_NONBLOCKING | COMM_DOBIND;
		TheCoordinatorInstance->address.sun_family = PF_UNIX;

		TheCoordinatorInstance->conn_fd = -1;
		
		for(i = 0;i<MAX_KID_SUPPORT;i++)
		{
			TheCoordinatorInstance->kidsfd[i] = -1;
		}
	}
}

TypedMsgHdr* createTypedMsgHdr(const char* address)
{

	CBDATA_INIT_TYPE_FREECB(TypedMsgHdr,IpcFreeMessage);
	
	TypedMsgHdr* msg = cbdataAlloc(TypedMsgHdr);

	initSendMsgHdr(msg, address);

	msg->retries = MAX_IPC_MESSAGE_RETRY;

	msg->timeout = MAX_IPC_MESSAGE_TIMEOUT;

	msg->restart_intval = MAX_IPC_MESSAGE_RETRY_INT;

	return msg;
}

int HasIpcFd(const TypedMsgHdr* message)
{ 
	if(message->msg_control && message->msg_controllen)
	{
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(message);
    	if(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    	{
    		debugs(54, 9, "typed message have fd");
			return 1;
    	}
	}
	debugs(54, 9, "typed message have no fd");
	return 0;
}

int GetIpcFd(const TypedMsgHdr* message)
{
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(message);

    assert(message->msg_control && message->msg_controllen);
	
    assert(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS);

    const int fdCount = 1;
	
    const int *fdStore = (int*)(CMSG_DATA(cmsg));
	
    int fd = -1;
	
    memcpy(&fd, fdStore, fdCount * sizeof(int));
	
    return fd;
}


int packIpcFd(TypedMsgHdr* message, int fd)
{

    assert(fd >= 0 && !message->msg_control && !message->msg_controllen);
    message->msg_control = &message->ctrl;
    message->msg_controllen = sizeof(message->ctrl);

    const int fdCount = 1;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fdCount);

    int *fdStore = (int*)(CMSG_DATA(cmsg));
    memcpy(fdStore, &fd, fdCount * sizeof(int));
    message->msg_controllen = cmsg->cmsg_len;

	return 0;
}

const char* GetCoordinatorAddr()
{
    static char* coordinatorAddr = NULL;
    if (!coordinatorAddr) {
		coordinatorAddr = xcalloc(1, 1024);
		strcat(coordinatorAddr, channelPathPfx);
		strcat(coordinatorAddr, APP_SHORTNAME);
		strcat(coordinatorAddr, coordinatorAddrLabel);	
		strcat(coordinatorAddr, ".ipc");
    }
    return coordinatorAddr;
}

void CoordinatorRemoveBindSocket(unsigned long addr, unsigned short port)
{
	dlink_node* head = TheCoordinatorInstance->listeners.head;

	ListenList*  node = NULL;
	
	while(head != NULL)
	{
		node = (ListenList*)head->data;
		
		struct in_addr saddr = sqinet_get_v4_inaddr(&node->addr, SQADDR_ASSERT_IS_V4);
		
		unsigned int sport = sqinet_get_port(&node->addr);

		if(saddr.s_addr == addr && sport == port)
			break;
		
		node = NULL;
		
		head = head->next;
	}

	if(node != NULL)
	{
		debugs(54, 6, "remove listening on fd %d", node->fd);
		
		comm_close(node->fd);
		
		dlinkDelete(&node->link, &TheCoordinatorInstance->listeners);
		
		xfree(node);
	}
}

int CoordinatoropenListenSocket(SharedListenRequest* request, int *errNo)
{
    debugs(54, 6, "opening listen FD at %s for kid %d", inet_ntoa(sqinet_get_v4_inaddr(&request->params.addr, SQADDR_ASSERT_IS_V4)), request->requestorId);

	dlink_node* head = TheCoordinatorInstance->listeners.head;

	ListenList*  node = NULL;

	int fd;

	while(head != NULL)
	{
		node = (ListenList*)head->data;
		
		if(memcmp(&node->addr,&request->params.addr,sizeof(sqaddr_t)) == 0)
		{
			break;
		}
		
		node = NULL;
		
		head = head->next;
	}

	*errNo = 0;

	if(node != NULL)
	{
		debugs(54, 6, "port %d is already listening on fd %d", sqinet_get_port(&request->params.addr), node->fd);
		return node->fd;
	}
	else
	{
		debugs(54, 1, "new listening on port %s:%d", inet_ntoa(sqinet_get_v4_inaddr(&request->params.addr, SQADDR_ASSERT_IS_V4)), sqinet_get_port(&request->params.addr));
	}
	
    enter_suid();
	
	unsigned char TOS = 0;
	
    //int fd = comm_open(request->sock_type, request->proto, request->addr, request->port, request->flags | COMM_DOBIND, TOS, request->fdNote)
    assert(request->params.fdNote < fdnEnd);
	
	fd  = comm_open6(request->params.sock_type, request->params.proto, (sqaddr_t*)&request->params.addr, request->params.flags | COMM_DOBIND, TOS, FdNotes[request->params.fdNote]);
	
	*errNo = (fd > 0) ? 0 : errno;

	leave_suid();

	if(*errNo)
	{
    	debugs(54, 6, "tried listening on port %d for kid:%d, error:%s", sqinet_get_port(&request->params.addr), request->requestorId, strerror(*errNo));
	}

	node = (ListenList*)xcalloc(1, sizeof(ListenList));
	node->fd = fd;
	memcpy(&node->addr,&request->params.addr,sizeof(sqaddr_t));
	
	dlinkAdd(node, &node->link, &TheCoordinatorInstance->listeners);
	
    return fd;
}


StrandCoord* CoordinatorFindStrand(int kidId)
{
	int i;
	for(i = 0; i < TheCoordinatorInstance->strandnumer; i++)
	{
		if(TheCoordinatorInstance->strands[i].kidId == kidId)
		{
			return &TheCoordinatorInstance->strands[i];
		}
	}
	return NULL;
}

void CoordinatorRegisterStrand(const StrandCoord* strand)
{
    debugs(54, 3, "registering kid %d",strand->kidId);

	StrandCoord* found = NULL;
    if ((found = CoordinatorFindStrand(strand->kidId)) )
	{
		 debugs(54, 3, "kid %d already registered",strand->kidId);
    } 
	else
    {
		memcpy(&TheCoordinatorInstance->strands[TheCoordinatorInstance->strandnumer],strand,sizeof(StrandCoord));
		
		++TheCoordinatorInstance->strandnumer;
    }
}

IpcMgrRequest* FindMgrRequest(dlink_list* list, int requestId)
{
    dlink_node *n;
	dlink_node *pre = NULL;
    IpcMgrRequest *q;
    for (n = list->tail; n; pre = n,n = n->prev) {
	q = n->data;
	if (q->requestId == requestId)
	    return q;
    }
    return NULL;
}


void CoordinatorBroadcastSignal(int sig)
{
    if (sig > 0) {
		
        if (IamCoordinatorProcess())
        {
			int i = 0;
			for(i = 0; i < TheCoordinatorInstance->strandnumer; i++)
			{
				int pid = TheCoordinatorInstance->strands[i].pid;
				debugs(54, 1, "broadcast signal %d to kid %d, pid %d", sig, TheCoordinatorInstance->strands[i].kidId, pid);
	        	kill(pid, sig);
			}
        }
        sig = -1;
    }
}


#define PACK_UNPACK
TypedMsgHdr* packRegisterRequest(const Strand* strand)
{
	TypedMsgHdr* msg = createTypedMsgHdr(GetCoordinatorAddr());
	
	debugs(54, 2, "start,kidid:%d,pid:%d", strand->kidId, strand->pid);

    msg->data.type = mtRegistration;

	void* msgid = msg;

	PACK_SIMPLE(msg,msgid);

	PACK_SIMPLE(msg,strand->kidId);

	PACK_SIMPLE(msg,strand->pid);

	PACK_STR(msg,strand->tag);

	debugs(54, 2, "finished,msgid:%p,type:%d,size:%d",msgid,msg->data.type, msg->data.size);

	dumps(54, 2, (unsigned char*)msg->data.raw, msg->data.size);
	
	return msg;
}

StrandCoord* unPackRegisterRequest(TypedMsgHdr* message)
{
	StrandCoord* strandhdrMsg = (StrandCoord*)xcalloc(1, sizeof(StrandCoord));

	assert(message->data.type == mtRegistration);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, strandhdrMsg->msgid);

	UNPACK_SIMPLE(message, strandhdrMsg->kidId);
	
	UNPACK_SIMPLE(message, strandhdrMsg->pid);

	UNPACK_STR(message, &strandhdrMsg->tag);

	debugs(54, 9, "finished,msgid:%p,kid:%d,pid:%d",strandhdrMsg->msgid,strandhdrMsg->kidId, strandhdrMsg->pid);
	
	return strandhdrMsg;
}

TypedMsgHdr* packRegisterResponse(StrandCoord* strand)
{
	TypedMsgHdr* msg = createTypedMsgHdr(GetStrandAddr(strand->kidId));

	debugs(54, 2, "start,dest kidid:%d,pid:%d", strand->kidId, strand->pid);
	
    msg->data.type = mtRegistration;

	PACK_SIMPLE(msg,strand->msgid);
	
	PACK_SIMPLE(msg,strand->kidId);

	PACK_SIMPLE(msg,strand->pid);

	PACK_STR(msg,strand->tag);

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d",strand->msgid,msg->data.type, msg->data.size);

	return msg;
}

StrandCoord* unPackRegisterResponse(TypedMsgHdr* message)
{
	StrandCoord* strandhdrMsg = (StrandCoord*)xcalloc(1, sizeof(StrandCoord));

	assert(message->data.type == mtRegistration);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, strandhdrMsg->msgid);

	UNPACK_SIMPLE(message, strandhdrMsg->kidId);
	
	UNPACK_SIMPLE(message, strandhdrMsg->pid);

	UNPACK_STR(message, &strandhdrMsg->tag);

	debugs(54, 9, "finished,msgid:%p,kid:%d,pid:%d",strandhdrMsg->msgid,strandhdrMsg->kidId, strandhdrMsg->pid);
	
	return strandhdrMsg;
}

TypedMsgHdr* packSharedListenRequest(SharedListenRequest* strand)
{
	TypedMsgHdr* msg = createTypedMsgHdr(GetCoordinatorAddr());
	
	debugs(54, 9, "start,kidid:%d",strand->requestorId);

	msg->data.type = mtSharedListenRequest;

	strand->msgid = msg;

	PACK_SIMPLE(msg,strand->msgid);
	
	PACK_SIMPLE(msg,strand->requestorId);
	
	PACK_SIMPLE(msg,strand->mapId);
	
	PACK_STRUCT(msg,strand->params);

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d",strand->msgid,msg->data.type, msg->data.size);
	
	return msg;
}

SharedListenRequest* unPackSharedListenRequest(TypedMsgHdr* message)
{
	SharedListenRequest* request = xcalloc(1, sizeof(SharedListenRequest));

	assert(message->data.type == mtSharedListenRequest);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, request->msgid);

	UNPACK_SIMPLE(message, request->requestorId);
	
	UNPACK_SIMPLE(message, request->mapId);
	
	UNPACK_STRUCT(message,request->params);

	debugs(54, 9, "finished,msgid:%p,kid:%d",request->msgid,request->requestorId);
	
	return request;
}

TypedMsgHdr* packSharedListenResponseMsg(SharedListenResponse* response)
{
	TypedMsgHdr* msg = createTypedMsgHdr(GetStrandAddr(response->requestorId));

	debugs(54, 2, "start,dest kidid:%d", response->requestorId);

	msg->data.type = mtSharedListenResponse;

	PACK_SIMPLE(msg,response->msgid);
		
	PACK_SIMPLE(msg,response->requestorId);

	PACK_SIMPLE(msg,response->fd);
	
	PACK_SIMPLE(msg,response->errNo);
	
	PACK_SIMPLE(msg,response->mapId);

	packIpcFd(msg, response->fd);

	dumps(54, 9, (unsigned char*)msg->ctrl.raw, sizeof(msg->ctrl));

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d",response->msgid,msg->data.type, msg->data.size);

	return msg;
}

SharedListenResponse* unPackSharedListenResponse(TypedMsgHdr* message)
{
	SharedListenResponse* response = xcalloc(1, sizeof(SharedListenResponse));

	assert(message->data.type == mtSharedListenResponse);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, response->msgid);

	UNPACK_SIMPLE(message, response->requestorId);

	UNPACK_SIMPLE(message, response->fd);
	
	UNPACK_SIMPLE(message, response->errNo);

	UNPACK_SIMPLE(message, response->mapId);

	if(HasIpcFd(message))
	{
		dumps(54, 9, (unsigned char*)message->ctrl.raw, sizeof(message->ctrl));
	
		response->fd = GetIpcFd(message);
	}
	else
	{
		response->fd = -1;
	}

	debugs(54, 9, "finished,msgid:%p,kid:%d",response->msgid,response->requestorId);
	
	return response;
}

TypedMsgHdr* packIpcMgrRequest(int dest_kidId, IpcMgrRequest* request)
{
	TypedMsgHdr* msg = NULL;

	if(dest_kidId == 0)
	{
		msg = createTypedMsgHdr(GetCoordinatorAddr());
	}
	else
	{
		msg = createTypedMsgHdr(GetStrandAddr(dest_kidId));
	}

	debugs(54, 2, "start,dest kidid:%d", dest_kidId);

	msg->data.type = mtCacheMgrRequest;

	request->msgid = msg;

	PACK_SIMPLE(msg, request->msgid);
	
	PACK_SIMPLE(msg, request->requestorId);

	PACK_SIMPLE(msg, request->requestId);

	PACK_SIMPLE(msg, request->params->method);

	PACK_SIMPLE(msg, request->params->flags);

	PACK_STR(msg, request->params->url);

	PACK_STR(msg, request->params->origin);

	PACK_STR(msg, request->params->action);

	PACK_STR(msg, request->params->user_name);

	PACK_STR(msg, request->params->passwd);

	PACK_SIMPLE(msg, request->params->params.paramsize);
	
	int i = 0;
	
	for(i = 0;i < request->params->params.paramsize; i++)
	{
		QueryParam* param = request->params->params.param[i];

		PACK_STR(msg, param->key);

		switch(param->type)
		{
			PACK_SIMPLE(msg, param->type);
			
			case ptString:
				{
					memcpy(msg->data.raw + msg->offset, &param->len, sizeof(int));
					msg->offset += sizeof(int);

					memcpy(msg->data.raw + msg->offset, param->value.strvalue, param->len);
					msg->offset += param->len;
					break;
				}

			case ptInt:
				{
					int  j;
					memcpy(msg->data.raw + msg->offset, &param->len, sizeof(int));
					msg->offset += sizeof(int);
					
					for(j = 0;j < param->len; j++)
					{
						memcpy(msg->data.raw + msg->offset, &param->value.intvalue[j], sizeof(int));
						msg->offset += sizeof(int);					
					}
					break;
				}
			default:
					break;
		}
	}

	if(!request->action || (request->action && !request->action->add) || dest_kidId == 0)
	{
		packIpcFd(msg, request->conn_fd);
		
		debugs(54, 9, "pack fd,control len", msg->msg_controllen);
		
		dumps(54, 9, (unsigned char*)msg->ctrl.raw, msg->msg_controllen);
	}

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d",request->msgid,msg->data.type, msg->data.size);

	return msg;
}

IpcMgrRequest* unPackIpcMgrRequest(TypedMsgHdr* message)
{

	CBDATA_INIT_TYPE_FREECB(IpcMgrRequest,IpcFreeIpcMgrRequest);

	IpcMgrRequest* request = cbdataAlloc(IpcMgrRequest);

	memset(request, 0, sizeof(IpcMgrRequest));

	assert(message->data.type == mtCacheMgrRequest);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, request->msgid);

	UNPACK_SIMPLE(message, request->requestorId);

	UNPACK_SIMPLE(message, request->requestId);

	request->params = xcalloc(1, sizeof(ActionParams));

	UNPACK_SIMPLE(message, request->params->method);

	UNPACK_SIMPLE(message, request->params->flags);

	UNPACK_STR(message, &request->params->url);

	UNPACK_STR(message, &request->params->origin);

	UNPACK_STR(message, &request->params->action);

	assert(request->params->action != NULL);

	request->action = findAction(request->params->action);

	UNPACK_STR(message, &request->params->user_name);

	UNPACK_STR(message, &request->params->passwd);

	UNPACK_SIMPLE(message, request->params->params.paramsize);
	
	if(request->params->params.paramsize > 0)
	{
		int i;
		int len;

		QueryParams* params = xcalloc(1, sizeof(QueryParams));

		params->maxsize = request->params->params.paramsize;
		
		params->param = (QueryParam**)xcalloc(params->maxsize,sizeof(QueryParam*));
		
		for(i = 0;i<request->params->params.paramsize;i++)
		{
			len = 0;
			
			params->param[i] = (QueryParam*)xcalloc(1,sizeof(QueryParam));

			UNPACK_STR(message, &params->param[i]->key);

			UNPACK_SIMPLE(message, params->param[i]->type);
			
			switch(params->param[i]->type)
			{
				case ptInt:
				{
					int k;
					memcpy(&len, message->data.raw + message->offset, sizeof(int));
					message->offset += sizeof(int);
					params->param[i]->len = len;
					params->param[i]->size = len;
					params->param[i]->value.intvalue = xcalloc(len,sizeof(int*));
					for(k=0;k<len;k++)
					{
						memcpy(&len, message->data.raw + message->offset, sizeof(int));
						message->offset += sizeof(int);

						params->param[i]->value.intvalue[k] = len;
					}
					break;
				}
				case ptString:
				{
					memcpy(&len, message->data.raw + message->offset, sizeof(int));	
					message->offset += sizeof(int);
					params->param[i]->len = len;
					params->param[i]->size = len + 1;
					params->param[i]->value.strvalue = xcalloc(len, len + 1);
					
					memcpy(&params->param[i]->value.strvalue, message->data.raw + message->offset, len);
					message->offset += len;
					
					break;
				}
				
				default:
					break;
			}
		}
	}

	if(HasIpcFd(message))
	{
		request->conn_fd = GetIpcFd(message);
	}
	else
	{
		request->conn_fd = -1;
	}
	
	debugs(54, 9, "finished,msgid:%p,kid:%d",request->msgid,request->requestorId);
	
	return request;
}

TypedMsgHdr* packIpcMgrResponse(int dest_kidId, IpcMgrResponse* response)
{
	TypedMsgHdr* msg = NULL;
	
	if(dest_kidId == 0)
	{
		msg = createTypedMsgHdr(GetCoordinatorAddr());
	}
	else
	{
		msg = createTypedMsgHdr(GetStrandAddr(dest_kidId));
	}

	debugs(54, 2, "start,dest kidid:%d", dest_kidId);
	
	msg->data.type = mtCacheMgrResponse;

	PACK_SIMPLE(msg, response->msgid);

	PACK_SIMPLE(msg, response->requestId);
	
	PACK_STR(msg, response->actionName);

	if(response->actiondata)
	{
		PACK_SIMPLE(msg, response->actiondatalen);
		PACK_FIXED(msg,response->actiondata,response->actiondatalen);
	}
	else
	{
		response->actiondatalen = 0;
		PACK_SIMPLE(msg, response->actiondatalen);
	}

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d,action:%s",response->msgid,msg->data.type, msg->data.size,response->actionName);

	return msg;
}

IpcMgrResponse*  unPackIpcMgrResponse(TypedMsgHdr* message)
{
	IpcMgrResponse* response = xcalloc(1, sizeof(IpcMgrResponse));

	assert(message->data.type == mtCacheMgrResponse);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, response->msgid);
	
	UNPACK_SIMPLE(message, response->requestId);

	UNPACK_STR(message, &response->actionName);

	assert(response->actionName);

	UNPACK_SIMPLE(message, response->actiondatalen);

	if(response->actiondatalen > 0)
	{
		response->actiondata = xcalloc(1, response->actiondatalen);
		UNPACK_FIXED(message, response->actiondata, response->actiondatalen);
	}
	
	debugs(54, 9, "finished,msgid:%p",response->msgid);
	
	return response;
}

static void
packSnmpVar(TypedMsgHdr* msg, struct variable_list* var)
{
	PACK_SIMPLE(msg,var->name_length);
    if (var->name_length > 0) {
        assert(var->name != NULL);
        PACK_FIXED(msg,var->name, var->name_length * sizeof(oid));
    }
    PACK_SIMPLE(msg,var->type);
    PACK_SIMPLE(msg,var->val_len);
    if (var->val_len > 0) {
        assert(var->val.string != NULL);
        PACK_FIXED(msg,var->val.string, var->val_len);
    }
}

static struct variable_list* unPackSnmpVar(TypedMsgHdr* msg)
{
	struct variable_list* var = xcalloc(1, sizeof(variable_list));

	UNPACK_SIMPLE(msg,var->name_length);
	assert(var->name_length > 0);
	if(var->name_length)
	{
        var->name = xmalloc(var->name_length * sizeof(oid));
        UNPACK_FIXED(msg, var->name, var->name_length * sizeof(oid));
	}
	UNPACK_SIMPLE(msg,var->type);
	
	UNPACK_SIMPLE(msg,var->val_len);

	if(var->val_len > 0)
	{
		var->val.string = xmalloc(var->val_len);
		UNPACK_FIXED(msg,var->val.string, var->val_len);
	}

	return var;
}

void packSnmpPdu(TypedMsgHdr* msg, struct snmp_pdu* pdu)
{
	int count = 0;
	variable_list* var = NULL;
	PACK_SIMPLE(msg,pdu->command);

	PACK_STRUCT(msg,pdu->address);
  
    PACK_SIMPLE(msg,pdu->reqid);
    PACK_SIMPLE(msg,pdu->errstat);
    PACK_SIMPLE(msg,pdu->errindex);
    PACK_SIMPLE(msg,pdu->non_repeaters);
    PACK_SIMPLE(msg,pdu->max_repetitions);
    PACK_SIMPLE(msg,pdu->enterprise_length);
	
    if (pdu->enterprise_length > 0) {
        assert(pdu->enterprise != NULL);
        PACK_FIXED(msg,pdu->enterprise, pdu->enterprise_length * sizeof(oid));
    }
    PACK_STRUCT(msg,pdu->agent_addr);
    PACK_SIMPLE(msg,pdu->trap_type);
    PACK_SIMPLE(msg,pdu->specific_type);
    PACK_SIMPLE(msg,pdu->time);
	
    for (var = pdu->variables; var != NULL; var = var->next_variable)
        ++count;	
    PACK_SIMPLE(msg,count);
	
    for (var = pdu->variables; var != NULL; var = var->next_variable)
        packSnmpVar(msg,var);
}

struct snmp_pdu*
unPackSnmpPdu(TypedMsgHdr* msg)
{
	int count;
	variable_list** p_var = NULL;
	struct snmp_pdu* pdu = xcalloc(1, sizeof(struct snmp_pdu));
    UNPACK_SIMPLE(msg,pdu->command);
    UNPACK_STRUCT(msg,pdu->address);
	UNPACK_SIMPLE(msg,pdu->reqid);
	UNPACK_SIMPLE(msg,pdu->errstat);
	UNPACK_SIMPLE(msg,pdu->errindex);
	UNPACK_SIMPLE(msg,pdu->non_repeaters);
	UNPACK_SIMPLE(msg,pdu->max_repetitions);
	UNPACK_SIMPLE(msg,pdu->enterprise_length);


    if (pdu->enterprise_length > 0) {
        pdu->enterprise = xmalloc(pdu->enterprise_length * sizeof(oid));
        UNPACK_FIXED(msg,pdu->enterprise, pdu->enterprise_length * sizeof(oid));
    }
    UNPACK_STRUCT(msg,pdu->agent_addr);
    UNPACK_SIMPLE(msg,pdu->trap_type);
    UNPACK_SIMPLE(msg,pdu->specific_type);
    UNPACK_SIMPLE(msg,pdu->time);
    
	UNPACK_SIMPLE(msg,count);
	
    for (p_var = &pdu->variables; count > 0; p_var = &(*p_var)->next_variable, --count) 
	{
        struct variable_list* var = unPackSnmpVar(msg);	
        *p_var = var;
    }
	return pdu;
}

void packSnmpSession(TypedMsgHdr* msg, struct snmp_session* session)
{
	int len = 0;
	
	PACK_SIMPLE(msg, session->Version);
	PACK_SIMPLE(msg, session->community_len);

	if(session->community_len > 0)
	{
	    assert(session->community != NULL);
		PACK_FIXED(msg,session->community,session->community_len);
	}
	PACK_SIMPLE(msg,session->retries);
	PACK_SIMPLE(msg,session->timeout);

	if(session->peername != NULL)
	{
	    len = strlen(session->peername);
	}
	PACK_SIMPLE(msg,len);
	if(len > 0)
	{
		PACK_FIXED(msg,session->peername,len);
	}

	PACK_SIMPLE(msg,session->remote_port);
	PACK_SIMPLE(msg,session->local_port);
}


struct snmp_session* unPackSnmpSession(TypedMsgHdr* msg)
{
	int len = 0;
    struct snmp_session* session = (struct snmp_session*)xcalloc(1, sizeof(struct snmp_session));
	UNPACK_SIMPLE(msg,session->Version);
	UNPACK_SIMPLE(msg,session->community_len);

    if (session->community_len > 0) {
        session->community = xmalloc(session->community_len + 1);
        assert(session->community != NULL);
        UNPACK_FIXED(msg, session->community, session->community_len);
        session->community[session->community_len] = 0;
    }
	
	UNPACK_SIMPLE(msg,session->retries);
	UNPACK_SIMPLE(msg,session->timeout);
	UNPACK_SIMPLE(msg,len);
    if (len > 0) {
        session->peername = xmalloc(len + 1);
        assert(session->peername != NULL);
        UNPACK_FIXED(msg, session->peername, len);
        session->peername[len] = 0;
    }

	UNPACK_SIMPLE(msg,session->remote_port);
	
	UNPACK_SIMPLE(msg,session->local_port);

	return session;
}

TypedMsgHdr* packIpcSnmpRequest(int dest_kidid, IpcSnmpRequest* request)
{
	TypedMsgHdr* msg = NULL;

	if(dest_kidid == 0)
	{
		msg = createTypedMsgHdr(GetCoordinatorAddr());
	}
	else
	{
		msg = createTypedMsgHdr(GetStrandAddr(dest_kidid));
	}

	debugs(54, 2, "start,dest kidid:%d,requestid:%d", dest_kidid,request->requestorId);

	msg->data.type = mtSnmpRequest;

	request->msgid = msg;

	PACK_SIMPLE(msg, request->msgid);
	
	PACK_SIMPLE(msg, request->requestorId);

	PACK_SIMPLE(msg, request->requestId);

	packSnmpPdu(msg, request->pdu);

	if(IamWorkerProcess())
	{
		packSnmpSession(msg, request->session);

		PACK_PSTRUCT(msg,request->address);
		
		packIpcFd(msg, request->conn_fd);
	}

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d",request->msgid,msg->data.type, msg->data.size);

	dumps(54, 9, (unsigned char*)msg->data.raw, msg->data.size);

	return msg;

}
 
IpcSnmpRequest* unPackIpcSnmpRequest(TypedMsgHdr* message)
{
	
	CBDATA_INIT_TYPE_FREECB(IpcSnmpRequest,IpcFreeIpcMgrRequest);

	IpcSnmpRequest* request = cbdataAlloc(IpcSnmpRequest);

	memset(request, 0, sizeof(IpcSnmpRequest));

	assert(message->data.type == mtSnmpRequest);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, request->msgid);

	UNPACK_SIMPLE(message, request->requestorId);

	UNPACK_SIMPLE(message, request->requestId);

	request->pdu = unPackSnmpPdu(message);
	
	if(IamCoordinatorProcess())
	{
		request->session = unPackSnmpSession(message);

		UNPACK_PSTRUCT(message, &request->address);
	}
	
	if(HasIpcFd(message))
	{
		request->conn_fd = GetIpcFd(message);
	}
	else
	{
		request->conn_fd = -1;
	}	
	
	debugs(54, 9, "finished,msgid:%p,request kid:%d,requestid:%d",request->msgid,request->requestorId,request->requestId);

	return request;
}

TypedMsgHdr* packIpcSnmpResponse(int dest_kidId, IpcSnmpResponse* response)
{
	TypedMsgHdr* msg = NULL;

	if(dest_kidId == 0)
	{
		msg = createTypedMsgHdr(GetCoordinatorAddr());
	}
	else
	{
		msg = createTypedMsgHdr(GetStrandAddr(dest_kidId));
	}

	msg->data.type = mtSnmpResponse;

	PACK_SIMPLE(msg, response->msgid);

	PACK_SIMPLE(msg, response->requestId);

	PACK_SIMPLE(msg, response->havepdu);

	debugs(54, 9, "start,msgid:%p,dest kidid:%d,requestid:%d",response->msgid,dest_kidId,response->requestId);
	
	if(response->havepdu)
	{
		debugs(54, 9, "pack snmp pdu,error stat:%d", response->pdu->errstat);
		packSnmpPdu(msg, response->pdu);
	}

	debugs(54, 9, "finished,msgid:%p,type:%d,size:%d",response->msgid,msg->data.type, msg->data.size);

	dumps(54, 9, (unsigned char*)msg->data.raw, msg->data.size);

	return msg;

}

IpcSnmpResponse* unPackIpcSnmpResponse(TypedMsgHdr* message)
{
	IpcSnmpResponse* response = xcalloc(1, sizeof(IpcSnmpResponse));

	assert(message->data.type == mtSnmpResponse);

	debugs(54, 9, "start,my kidid:%d", KidIdentifier);

	UNPACK_SIMPLE(message, response->msgid);

	UNPACK_SIMPLE(message, response->requestId);

	UNPACK_SIMPLE(message, response->havepdu);

	if(response->havepdu)
	{
		response->pdu = unPackSnmpPdu(message);
	}

	debugs(54, 9, "finished,msgid:%p,requestid:%d",response->msgid,response->requestId);
	
	return response;
}
#undef PACK_UNPACK


#define MGR_REQUEST

void CloseForwarder(int fd, void* data);

int ImportMgrForwardFdIntoComm(int fd)
{
	sqaddr_t addr;
	struct sockaddr_in saddr;
	sqinet_init(&addr);
    socklen_t len = sizeof(saddr);

	if(fd < 0) return -1;
	
    if (getsockname(fd, (struct sockaddr*)(&saddr), &len) == 0) {
		
		sqinet_set_v4_sockaddr(&addr, &saddr);

        comm_import_opened(fd, SOCK_STREAM, FdNotes[fdnHttpSocket], &addr, COMM_NOCLOEXEC);
    } 
	else 
    {
        debugs(54, DBG_CRITICAL, "ERROR: ImportMgrForwardFdIntoComm, error:%s", xstrerror());
        comm_close(fd);
		return -1;
    }
	
    return fd;
}

void closeHttpPortIfNeeded(http_port_list* oldport, http_port_list* newport)
{
	http_port_list* s = oldport;
	
	unsigned long saddr = 0;
	unsigned long nsaddr = 0;
	unsigned short port = 0;
	http_port_list* ns = NULL;

	while(s != NULL)
	{
		port = s->s.sin_port;
		
		saddr = s->s.sin_addr.s_addr;

		ns = newport;

		while(ns != NULL)
		{
			if(port == ns->s.sin_port)
			{
				nsaddr = ns->s.sin_addr.s_addr;
				break;
			}
			ns=ns->next;
		}

		if(ns == NULL)
		{
			debugs(54, 6, "remove http port %u:%d", saddr, ntohs(port));
			CoordinatorRemoveBindSocket(saddr, ntohs(port));
		}
		else
		{
			if(nsaddr != saddr)
			{
				debugs(54, 6, "remove http port %u:%d", saddr, ntohs(port));
				CoordinatorRemoveBindSocket(saddr, ntohs(port));
			}
		}
		s=s->next;
	}
}

#if USE_SSL
void closeHttpsPortIfNeeded(http_port_list* oldport, http_port_list* newport)
{
	https_port_list* s = oldport;
	
	unsigned long saddr = 0;
	unsigned long nsaddr = 0;
	unsigned short port = 0;
	https_port_list* ns = NULL;

	while(s != NULL)
	{
		port = s->http.s.sin_port;
		
		saddr = s->http.s.sin_addr.s_addr;

		ns = newport;

		while(ns != NULL)
		{
			if(port == ns->http.s.sin_port)
			{
				nsaddr = ns->http.s.sin_addr.s_addr;
				break;
			}
			ns=ns->next;
		}

		if(ns == NULL)
		{
			debugs(54, 6, "remove http port %u:%d", saddr, ntohs(port));
			CoordinatorRemoveBindSocket(saddr, ntohs(port));
		}
		else
		{
			if(nsaddr != saddr)
			{
				debugs(54, 6, "remove http port %u:%d", saddr, ntohs(port));
				CoordinatorRemoveBindSocket(saddr, ntohs(port));
			}
		}
		s=s->next;
	}
}
#endif

int MgrForwardCondition(int fd, void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*)data;
	
	debugs(54, 6, "FD:%d,arg:%p,argfd:%d", fd, data, request->conn_fd);
	
	if(request->conn_fd == fd)
	{		
		TypedMsgHdr* message = (TypedMsgHdr*)request->msgid;
		message->canceled = 1;
		request->requestId = 0;
		dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);
		debugs(54, 6, "FD:%d,cancel mgr message:%p,msgid:%p", fd, data, message);
		return 1;
	}
	return 0;
}

void IpcForwarderTimeOut(void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*) data;
	
	debugs(54, 6, "FD:%d,kidid:%d,action:%s", request->conn_fd, KidIdentifier, request->params->action);

	if(request->requestId != 0)
	{
		request->requestId = 0;
		comm_remove_close_handler(request->conn_fd, CloseForwarder, request);
		dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);
		storeUnlockObject(request->entry);
		comm_close(request->conn_fd);
		cbdataFree(data);
	}
	else
	{
		cbdataFree(data);
	}
}

void CloseForwarder(int fd, void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*) data;
	
	debugs(54, 6, "FD:%d,kidid:%d,action:%s", request->conn_fd, KidIdentifier, request->params->action);

	request->requestId = 0;

	TypedMsgHdr* message = (TypedMsgHdr*)request->msgid;

	message->canceled = 1;

	comm_condition_remove_close_handler(fd, CloseForwarder, MgrForwardCondition);

	eventTravel(fd, IpcForwarderTimeOut, MgrForwardCondition);
	
	dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);

	cbdataFree(request);
}


IpcMgrRequest* createIpcMgrRequest(int fd, ActionParams *params)
{

	CBDATA_INIT_TYPE_FREECB(IpcMgrRequest,IpcFreeIpcMgrRequest);

	assert(params->action != NULL);

	IpcMgrRequest* req = cbdataAlloc(IpcMgrRequest);

	memset(req, 0, sizeof(IpcMgrRequest));
	
	if(++TheStrandInstance->lastRequestId == 0)
		
	++TheStrandInstance->lastRequestId;
	
	req->requestId = TheStrandInstance->lastRequestId;

	req->conn_fd = fd;

	req->requestorId = KidIdentifier;

	req->params = (ActionParams*) xcalloc(1, sizeof(ActionParams));

	memcpy(&req->params->params, &params->params, sizeof(QueryParams));
	
	req->params->url = params->url?xstrdup(params->url):NULL;
	req->params->origin = params->origin?xstrdup(params->origin):NULL;
	req->params->action = params->action?xstrdup(params->action):NULL;
	req->params->user_name = params->user_name?xstrdup(params->user_name):NULL;
	req->params->passwd = params->passwd?xstrdup(params->passwd):NULL;

	req->action = findAction(params->action);

	assert(req->action != NULL);

	debugs(54, 6, "FD:%d,kidid:%d,action:%s", req->conn_fd, KidIdentifier,req->action->action);

	return req;
}

void StartMgrForwarder(int fd, ActionParams *params, StoreEntry* entry)
{
	IpcMgrRequest* request = createIpcMgrRequest(fd, params);

	EBIT_CLR(entry->flags, ENTRY_FWD_HDR_WAIT);

	request->entry = entry;

	storeLockObject(entry);

	dlinkAdd(request, &request->node, &TheStrandInstance->TheRequestsList);

	comm_add_close_handler(request->conn_fd, CloseForwarder, request);

	TypedMsgHdr* message = packIpcMgrRequest(0, request);

	message->timeout_handle.handle = IpcForwarderTimeOut;
	
	message->timeout_handle.data = request;

	debugs(54, 6, "FD:%d,kidid:%d,action:%s", request->conn_fd, KidIdentifier, params->action);

	StrandSendMessageToCoordinator(message);
}





void RequestClosed(int fd, void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*) data;
	
	debugs(54, 6, "FD:%d,kidid:%d,action:%s", request->conn_fd, request->params->action);

	dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);

	comm_close(request->conn_fd);
}

void
InquirerInit()
{
	;
}

int InquirerCleanup()
{
	return 0;
}


QueryParam *getQueryParams(char* key, QueryParams* params)
{

	return NULL;
}

void InquirerApplyQueryParams(StrandCoord aStrands[], int strandsNum, int* StrandsResultNum, StrandCoord StrandsResult[], QueryParams* aParams)
{
	int num = 0;

	int i = 0;

    QueryParam* processesParam = getQueryParams("processes", aParams);
	
    QueryParam* workersParam = getQueryParams("workers", aParams);

    if (processesParam == NULL || workersParam == NULL) {
        if (processesParam != NULL) {

			if(processesParam->type == ptInt)
			{	
				for(i = 0; i < strandsNum; i++)
				{
					int j = 0;
					
					for(j = 0; j < processesParam->size; j++)
					{
						if(processesParam->value.intvalue[j] == aStrands[i].kidId)
						break;
					}
					
					if(j < processesParam->size)
					{
						StrandsResult[num++].kidId = processesParam->value.intvalue[j];
					}
				}
				*StrandsResultNum = num;
			}
        } else if (workersParam != NULL) {

			if(workersParam->type == ptInt) {
				int i = 0;
				for(i =0; i < strandsNum; i++)
				{
					int j = 0;
					
					for(j = 0; j < workersParam->size; j++)
					{
						if(workersParam->value.intvalue[j] == aStrands[i].kidId)
						break;
					}
					
					if(j < workersParam->size)
					{
						StrandsResult[num++].kidId = workersParam->value.intvalue[j];
					}
				}
				*StrandsResultNum = num;
			}
        } else {
        			for(i = 0; i < strandsNum; i++)
					{
						StrandsResult[i].kidId = aStrands[i].kidId;
					}
					*StrandsResultNum = strandsNum;
        }
    }
	else
	{
		*StrandsResultNum = 0;
	}
	
}

void InquirerRequestTimedOut(void* data)
{
	debugs(54, 5, "timeout");
}

void InquirerSendData(void *data, mem_node_ref ref, ssize_t size);


void
InquirerClose(void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*)data;
	
	comm_remove_close_handler(request->conn_fd, RequestClosed, request);

	dlinkDelete(&request->node, &TheCoordinatorInstance->TheRequestsList);

	if(request->entry)
	{
		storeUnlockObject(request->entry);
	}

	comm_close(request->conn_fd);
	
	cbdataFree(request->msgid);
	
	cbdataFree(data);
}

void
InquirerSendComplete(int fd, char *buf, size_t size, int errflag, void *data)
{
	IpcMgrRequest* request = (IpcMgrRequest*)data;

	if (size < 0) {
		
	}
	else if (size == 0) {
		
	}
	else
	{
		request->writeOffset += size;
	}

	debugs(54, 5, "FD %d receive %d bytes, write offset %d", request->conn_fd, (int)size, request->writeOffset);

	if(request->entry)
	{
		if(request->writeOffset < objectLen(request->entry))
		{
			debugs(54, 5, "FD %d resend start", request->conn_fd);
			
			storeClientRef(request->sc, request->entry, request->writeOffset, request->writeOffset, SM_PAGE_SIZE, InquirerSendData, (void*)request);

			return;
		}
	}
	
	if(IamWorkerProcess() && !request->action->flags.aggregatable)
	{
	    IpcMgrResponse response;
		
		response.msgid = request->msgid;
		
		response.requestId = request->requestId;
		
		response.actionName = request->params->action;

		response.actiondata = NULL;

		response.actiondatalen = 0;

		TypedMsgHdr* message = packIpcMgrResponse(0, &response);
		
		storeUnlockObject(request->entry);
		
		comm_close(request->conn_fd);

		cbdataFree(data);

		StrandSendMessageToCoordinator(message);
	}
	
	if(IamCoordinatorProcess())
	{
		InquirerClose(request);
	}
}


void InquirerSendData(void *data, mem_node_ref ref, ssize_t size)
{
	IpcMgrRequest* request = (IpcMgrRequest*)data;
	
    const char *buf = NULL;
	
    assert(size + ref.offset <= SM_PAGE_SIZE);
	
    assert(size <= SM_PAGE_SIZE);
	
    debugs(54, 5, "FD %d send %d bytes",  request->conn_fd, (int) size);
	
    if (size < 0) {
		InquirerSendComplete(request->conn_fd, NULL, 0, COMM_OK, request);
		stmemNodeUnref(&ref);
		return;
    } else if (size == 0) {
		InquirerSendComplete(request->conn_fd, NULL, 0, COMM_OK, request);
		stmemNodeUnref(&ref);
		return;
    }
	
    assert(ref.node->data);
	
    buf = ref.node->data + ref.offset;
	
	comm_write(request->conn_fd, buf, size, InquirerSendComplete, data, NULL);
	
    stmemNodeUnref(&ref);
}

int InquirerLast(const IpcMgrRequest* request);
void InquirerSendResponse(IpcMgrRequest* request);
void InquirerNext(IpcMgrRequest* request);

void
InquirerhandleTimeout(void* data)
{
	IpcMgrRequest* request = (IpcMgrRequest*)data;

	cbdataFree(request->msgid);

	if(InquirerLast(request))
	{
		if(request->action && request->action->add)
		{
			InquirerSendResponse(request);
		}
		else
		{
			InquirerSendComplete(request->conn_fd, NULL, 0, COMM_OK, request);
		}
	}
	else
	{
		InquirerNext(request);
	}
}

void InquirerSendResponse(IpcMgrRequest* request)
{
	request->entry = storeCreateEntry(request->params->url, request->params->flags, urlMethodGetKnownByCode(request->params->method));

	request->sc = storeClientRegister(request->entry, (void*)request);

	storeLockObject(request->entry);

	assert(request->action != NULL);

	request->action->handler(request->entry,request->actiondata);

	storeComplete(request->entry);

	debugs(54, 4, "FD:%d, entry object size:%d", request->conn_fd, objectLen(request->entry));
	
	storeClientRef(request->sc, request->entry, 0, 0, SM_PAGE_SIZE, InquirerSendData, (void*)request);	
}

void InquirerNext(IpcMgrRequest* request)
{
	++request->strandindex;
	
	int kidId = request->strands[request->strandindex].kidId;

	debugs(54, 4, "inquire kid: %d", kidId);
	
	TypedMsgHdr* message = packIpcMgrRequest(kidId, request);

	message->timeout_handle.handle = InquirerhandleTimeout;
	
	message->timeout_handle.data = request;

	CoordinatorSendMessageToStrand(kidId, message);
}

int InquirerLast(const IpcMgrRequest* request)
{
	return (request->strandindex + 1) == request->strandnumer;
}

IpcMgrRequest* InquirerGetRequest(int requestid)
{
	IpcMgrRequest* request = FindMgrRequest(&TheCoordinatorInstance->TheRequestsList,requestid);

	return request;
}

void InquirerSendHeaderDone(int fd, char* buf, size_t size, int errflag , void *data)
{
	IpcMgrRequest* request = data;
	
	TypedMsgHdr* message = packIpcMgrRequest(request->strands[request->strandindex].kidId, request);

	message->timeout_handle.handle = InquirerhandleTimeout;
	
	message->timeout_handle.data  = request;

	debugs(54, 4, "inquirer start,send request to kid %d", request->strands[request->strandindex].kidId);

	CoordinatorSendMessageToStrand(request->strands[request->strandindex].kidId, message);
}

void InquirerSendHeader(IpcMgrRequest* request)
{
	MemBuf membuf;
	
	HttpReply* reply;
	
    if (request->strandnumer == 0) {
        LOCAL_ARRAY(char, url, MAX_URL);
        snprintf(url, MAX_URL, "%s", request->params->url);
    	method_t *method_get = urlMethodGetKnownByCode(METHOD_GET);		
		request_t* req = urlParse(method_get, url);
		ErrorState* err = errorCon(ERR_INVALID_URL, HTTP_NOT_FOUND, req);
		reply = errorBuildReply(err);
        membuf = httpReplyPack(reply);
    } else {
		reply = httpReplyCreate();
		httpReplySetHeaders(reply, HTTP_OK, NULL, "text/plain", -1, -1, squid_curtime);
		httpHeaderPutStr(&reply->header, HDR_CONNECTION, "close");
		membuf = httpReplyPack(reply);	
    }

	comm_write(request->conn_fd, membuf.buf, membuf.size, InquirerSendHeaderDone, request, NULL);
}


int
InquirerIpcStart(IpcMgrRequest* request)
{
	InquirerSendHeader(request);
	return 0;
}

void InquireAddResponse(IpcMgrRequest* request, IpcMgrResponse* response)
{
	debugs(54, 4, "action:%s", response->actionName);
    if(request->actiondata == NULL)
	{
	   request->actiondata = response->actiondata;
	}
	else 
	{	
       request->action->add(request->actiondata, response->actiondata);
	   xfree(response->actiondata);
	}
}


void
InquirerhandleRemoteAck(IpcMgrResponse* response)
{
	IpcMgrRequest* request = InquirerGetRequest(response->requestId);

	debugs(54, 4, "action:%s", response->actionName);
	
	if(InquirerLast(request))
	{
		if(request->action && request->action->add)
		{
			InquirerSendResponse(request);
		}
		else
		{
			debugs(54, 4, "inquire action:%s complete", response->actionName);
			
			InquirerSendComplete(request->conn_fd, NULL, 0, COMM_OK, request);
		}
	}
	else 
	{
		if(request->action && request->action->add)
		{
			InquireAddResponse(request, response);
			xfree(response->actionName);
			xfree(response);
		}
		InquirerNext(request);
	}
}

void StrandActionDump(StoreEntry* entry, IpcMgrRequest* request)
{
    if(entry != NULL && (!request->action || !request->action->add))
    {
	    if (UsingSmp())
	        storeAppendPrintf(entry, "by kid%d {\n", KidIdentifier);
		
	    if(request->action && request->action->handler) 
		{
			request->action->handler(entry, NULL);
	    }
		
	    if (request->action->flags.atomic && UsingSmp())
	        storeAppendPrintf(entry, "} by kid%d\n\n", KidIdentifier);
    }
}



#undef MGR_REQUEST


#define SNMP_REQUEST

void SnmpNext(IpcSnmpRequest* request);
void SnmpHandleRequestTimeout(void* data);
void IpcWriteRetry(void* data);

int SnmpIsLastRequest(IpcSnmpRequest* request)
{
	return (request->strandindex + 1) == request->strandnumer;
}

IpcSnmpRequest* FindSnmpRequest(dlink_list* list, int requestId)
{
    dlink_node *n;
    IpcSnmpRequest *q;
    for (n = list->tail; n; n = n->prev) {
	q = n->data;
	if (q->requestId == requestId)
	    return q;
    }
    return NULL;
}

void IpcSnmpForwardTimeOut(void* data)
{
	IpcSnmpRequest* request = (IpcSnmpRequest*) data;

	if(request->requestId != 0)
	{
		request->requestId = 0;
		debugs(54, 6, "FD:%d,kidid:%d", request->conn_fd, KidIdentifier);
		dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);
		comm_close(request->conn_fd);
		cbdataFree(data);
	}
	else
	{
	    cbdataFree(data);
	}
}

int IpcSnmpCondition(int fd, void* arg)
{
	IpcSnmpRequest* request = (IpcSnmpRequest*)arg;
	
	debugs(54, 6, "FD:%d,arg:%p,argfd:%d", fd, arg, request->conn_fd);
	
	if(request->conn_fd == fd)
	{		
		TypedMsgHdr* message = (TypedMsgHdr*)request->msgid;
		message->canceled = 1;
		request->requestId = 0;
		dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);
		debugs(54, 6, "FD:%d,cancel message:%p,msgid:%p", fd, arg, message);
		return 1;
	}
	return 0;
}

void
SnmpForwardClosed(int fd, void* data)
{
	IpcSnmpRequest* request = (IpcSnmpRequest*) data;

	TypedMsgHdr* message = (TypedMsgHdr*)request->msgid;

	message->canceled = 1;

	debugs(54, 6, "FD:%d,request:%p,kidid:%d,msgid:%p", request->conn_fd,request,KidIdentifier,message);

	comm_condition_remove_close_handler(fd, SnmpForwardClosed, IpcSnmpCondition);

	eventTravel(fd, IpcSnmpForwardTimeOut, IpcSnmpCondition);

	dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);
}


void SnmpRequestClosed(int fd, void* data)
{
	IpcSnmpRequest* request = (IpcSnmpRequest*) data;
	
	TypedMsgHdr* message = (TypedMsgHdr*)request->msgid;
	
	debugs(54, 6, "FD:%d,request:%p,kidid:%d,msgid:%p", request->conn_fd,request,request->strands[request->strandindex],message);

	eventDeleteNoAssert(IpcWriteRetry, message);

	comm_condition_remove_close_handler(fd, SnmpForwardClosed, IpcSnmpCondition);

	eventConditionDelete(fd, SnmpHandleRequestTimeout, IpcSnmpCondition);
	
	dlinkDelete(&request->node, &TheCoordinatorInstance->TheRequestsList);
}

void SnmpSendComplete(IpcSnmpRequest* request)
{
	debugs(54, 6, "kidid:%d",KidIdentifier);

	request->requestId = 0;

	comm_remove_close_handler(request->conn_fd, SnmpRequestClosed, request);

	dlinkDelete(&request->node, &TheCoordinatorInstance->TheRequestsList);

	comm_close(request->conn_fd);

	cbdataFree(request);
}


int ImportSnmpForwardFdIntoComm(int fd)
{
	sqaddr_t addr;
	struct sockaddr_in saddr;
	sqinet_init(&addr);
    socklen_t len = sizeof(saddr);

	if(fd < 0) return -1;
	
    if (getsockname(fd, (struct sockaddr*)(&saddr), &len) == 0) {
		
		sqinet_set_v4_sockaddr(&addr, &saddr);

        comm_import_opened(fd, SOCK_DGRAM, FdNotes[fdnInSnmpSocket], &addr, 0);
    } 
	else 
    {
        debugs(54, DBG_CRITICAL, "ERROR: ImportSnmpForwardFdIntoComm, error:%s", xstrerror());
        comm_close(fd);
		return -1;
    }
	
    return fd;
}


void 
SnmpVarOperatorDivide(variable_list* pVar, int num)
{
    assert(num != 0);
	
    switch (pVar->type) {
    case SMI_INTEGER:
	{
		assert(pVar->val.integer != NULL && pVar->val_len == sizeof(int));
		int val = *pVar->val.integer;
		val = val/num;
		pVar->val.string = xmalloc(sizeof(val));
		memcpy(pVar->val.string, &val, sizeof(val));
		pVar->type = SMI_INTEGER;
		pVar->val_len = sizeof(val);
		break;
	}
    case SMI_GAUGE32:
	{
		assert(pVar->val.integer != NULL && pVar->val_len == 4);
		unsigned int val = *pVar->val.integer;
		val = val / num;
		pVar->val.string = (unsigned char*)xmalloc(sizeof(unsigned int));
		memcpy(pVar->val.string, &val, sizeof(unsigned int));
		pVar->type = SMI_GAUGE32;
		pVar->val_len = sizeof(val);
        break;
    }
    case SMI_COUNTER32:
	{
		assert(pVar->val.integer != NULL && pVar->val_len == 4);
		int val = *pVar->val.integer;
		val = val / num;
		pVar->val.string = (unsigned char*)xmalloc(sizeof(int));
		memcpy(pVar->val.string, &val, sizeof(int));
		pVar->type = SMI_COUNTER32;
		pVar->val_len = sizeof(val);
		break;
	}
    case SMI_COUNTER64:
	{
		assert(pVar->val.integer != NULL && pVar->val_len == 8);
		long long int val = *pVar->val.integer;
		val = val / num;
		pVar->val.string = (unsigned char*)xmalloc(sizeof(long long int));
		memcpy(pVar->val.string, &val, sizeof(long long int));
		pVar->type = SMI_COUNTER64;
		pVar->val_len = sizeof(val);
		break;
	}		
    case SMI_TIMETICKS:
	{
		assert(pVar->val.integer != NULL && pVar->val_len == sizeof(unsigned int));
		unsigned int ticks = *pVar->val.integer;
		ticks = ticks / num;
		pVar->val.string = (unsigned char*)xmalloc(sizeof(unsigned int));
		memcpy(pVar->val.string, &ticks, sizeof(unsigned int));
		pVar->type = SMI_TIMETICKS;
		pVar->val_len = sizeof(unsigned int);
		break;
	}
	
    default:
        debugs(49, DBG_CRITICAL, "Unsupported type:%d", pVar->type);
        
        break;
    }
}

void
SnmpPdufixAggregate(IpcSnmpRequest* request)
{
    if (request->aggrCount < 2)
        return;

	variable_list* p_aggr = NULL;
	
    for (p_aggr = request->aggrpdu->variables; p_aggr != NULL; p_aggr = p_aggr->next_variable) {
        if (snmpAggrType(p_aggr->name, p_aggr->name_length) == atAverage) {
			SnmpVarOperatorDivide(p_aggr, request->aggrCount);
        }
    }
    request->aggrCount = 0;
}

void
SnmpVarOperatorAdd(variable_list* pVarA, variable_list* pVarB)
{
	switch (pVarA->type) {
	case SMI_INTEGER:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == sizeof(int));
		int val = *pVarA->val.integer + *pVarB->val.integer;
		if(pVarA->val.string) xfree(pVarA->val.string);
		pVarA->val.string = xmalloc(sizeof(val));
		memcpy(pVarA->val.string, &val, sizeof(val));
		pVarA->type = SMI_INTEGER;
		pVarA->val_len = sizeof(val);
		break;
	}
	case SMI_COUNTER32:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == 4);
		int val = *pVarA->val.integer + *pVarA->val.integer;
		if(pVarA->val.string) xfree(pVarA->val.string);
		pVarA->val.string = (unsigned char*)xmalloc(sizeof(int));
		memcpy(pVarA->val.string, &val, sizeof(int));
		pVarA->type = SMI_COUNTER32;
		pVarA->val_len = sizeof(val);
		break;
	}		
	case SMI_COUNTER64:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == 8);
		long long int val = *pVarA->val.integer + *pVarB->val.integer;
		if(pVarA->val.string) xfree(pVarA->val.string);
		pVarA->val.string = (unsigned char*)xmalloc(sizeof(long long int));
		memcpy(pVarA->val.string, &val, sizeof(long long int));
		pVarA->type = SMI_COUNTER64;
		pVarA->val_len = sizeof(val);
		break;
	}		
	case SMI_TIMETICKS:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == sizeof(unsigned int));
		unsigned int ticks = *pVarA->val.integer + *pVarB->val.integer;
		if(pVarA->val.string) xfree(pVarA->val.string);
		pVarA->val.string = (unsigned char*)xmalloc(sizeof(unsigned int));
		memcpy(pVarA->val.string, &ticks, sizeof(unsigned int));
		pVarA->type = SMI_TIMETICKS;
		pVarA->val_len = sizeof(unsigned int);
		break;
	}		
	default:
	   debugs(49, DBG_CRITICAL, "Unsupported type:%d", pVarA->type);
	   break;
	}
}

int
SnmpVarOperatorCompare(variable_list* pVarA, variable_list* pVarB)
{
    switch (pVarA->type) {
    case SMI_INTEGER:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == sizeof(int));
		int val = *pVarA->val.integer - *pVarB->val.integer;
		return val;
	}
    case SMI_GAUGE32:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == 4);
		unsigned int val = *pVarA->val.integer + *pVarB->val.integer;
		return val;
    }		
    case SMI_COUNTER32:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == 4);
		int val = *pVarA->val.integer + *pVarA->val.integer;
		return val;
	}		
    case SMI_COUNTER64:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == 8);
		long long int val = *pVarA->val.integer + *pVarB->val.integer;
		return val;
	}		
    case SMI_TIMETICKS:
	{
		assert(pVarA->val.integer != NULL && pVarA->val_len == sizeof(unsigned int));
		unsigned int ticks = *pVarA->val.integer + *pVarB->val.integer;
		return ticks;
	}		
    default:
       debugs(49, DBG_CRITICAL, "Unsupported type:%d", pVarA->type);
	   return 0;
    }
}

int SnmpPduaggregate(IpcSnmpRequest* request, struct snmp_pdu** responsepdu)
{
	struct snmp_pdu* pduA = request->aggrpdu;
	
	struct snmp_pdu* pduB = *responsepdu;
	
	variable_list* var = NULL;
    int countA = 0;
	int countB = 0;

	if(pduB->errstat != SNMP_ERR_NOERROR)
	{
		snmp_pdu_free(request->aggrpdu);
		request->aggrpdu = pduB;
		request->aggrCount = 1;
		*responsepdu = NULL;
		return 0;
	}
	 
    for (var = pduA->variables; var != NULL; var = var->next_variable)
        ++countA;
	
	for (var = pduB->variables; var != NULL; var = var->next_variable)
        ++countB;
	
    assert(countA == countB);

	++ request->aggrCount;
	
	variable_list* p_aggr = NULL;
	variable_list* p_var = NULL;
	
 	for (p_aggr = pduA->variables, p_var = pduB->variables; p_var != NULL;
            p_aggr = p_aggr->next_variable, p_var = p_var->next_variable) {
		assert(p_aggr != NULL);
		if(p_aggr->type == SMI_NULLOBJ)
		{	
			debugs(54,9,"aggrpdu SMI_NULLOBJ,use response name");	
			size_t len = p_var->name_length * sizeof(oid);
			if(p_aggr->name) xfree(p_aggr->name);
			p_aggr->name = (oid*)xmalloc(len);
			memcpy((void*)p_aggr->name, (void*)p_var->name, len);
			p_aggr->name_length = p_var->name_length;

			p_aggr->val.string = xcalloc(1, p_var->val_len);
			memcpy(p_aggr->val.string, p_var->val.string, p_var->val_len);
			p_aggr->val_len = p_var->val_len;
			p_aggr->type =  p_var->type;				
		}
		else
		{
			AggrType aggrtype;
			
			switch((aggrtype = snmpAggrType(p_aggr->name, p_aggr->name_length)))
			{
				debugs(54,9,"aggrpdu snmpAggrType %d", aggrtype);
	       		case atSum:
	            case atAverage:
	                // The mean-average division is done later
	                // when the Snmp::Pdu::fixAggregate() called
					SnmpVarOperatorAdd(p_aggr,p_var);
	                break;
	            case atMax:
	                if (SnmpVarOperatorCompare(p_var,p_aggr) > 0)
	                {
						if(p_aggr->val.string) xfree(p_aggr->val.string);
						p_aggr->val.string = xcalloc(1, p_var->val_len);
						memcpy(p_aggr->val.string, p_var->val.string, p_var->val_len);
						p_aggr->val_len =  p_var->val_len;
						p_aggr->type =  p_var->type;
	                }
	                break;
	            case atMin:
	                if (SnmpVarOperatorCompare(p_var,p_aggr) < 0)
	                {
						if(p_aggr->val.string) xfree(p_aggr->val.string);
						p_aggr->val.string = xcalloc(1, p_var->val_len);
						memcpy(p_aggr->val.string, p_var->val.string, p_var->val_len);						
						p_aggr->val_len = p_var->val_len;
						p_aggr->type =  p_var->type;
	                }
	                break;
	            default:
	                break;			
			}
		}
	}
			
	return 1;
}

IpcSnmpRequest* CreateSnmpRequest(int fd, struct snmp_pdu* PDU, struct snmp_session* session, struct sockaddr_in* from)
{
	CBDATA_INIT_TYPE_FREECB(IpcSnmpRequest,IpcFreeSnmpRequest);

	IpcSnmpRequest* request = cbdataAlloc(IpcSnmpRequest);

	memset(request, 0, sizeof(IpcSnmpRequest));
	
	if(++TheStrandInstance->lastRequestId == 0)
	++TheStrandInstance->lastRequestId;
	
	request->requestId = TheStrandInstance->lastRequestId;

	request->conn_fd = fd;

	request->requestorId = KidIdentifier;

	request->pdu = PDU;

	request->session = session;

	request->address = from;
	
	debugs(54, 6, "FD:%d,kidid:%d", request->conn_fd, KidIdentifier);

	return request;
}

void StartSnmpForwarder(struct snmp_pdu* PDU, struct snmp_session* session, int sock, struct sockaddr_in* from)
{
	IpcSnmpRequest* request = CreateSnmpRequest(sock, PDU, session, from);
	
	dlinkAdd(request, &request->node, &TheStrandInstance->TheRequestsList);

	debugs(54, 6, "FD:%d,kidid:%d,add close handle %p:%p", request->conn_fd, KidIdentifier, SnmpForwardClosed, request);

	PrintSnmpOid(PDU);
	
	comm_add_close_handler(request->conn_fd, SnmpForwardClosed, request);
	
	TypedMsgHdr* message = packIpcSnmpRequest(0, request);
	
	message->timeout_handle.handle = IpcSnmpForwardTimeOut;
	
	message->timeout_handle.data = request;

	StrandSendMessageToCoordinator(message);
}

void SnmpSendResponse(IpcSnmpRequest* request)
{
    u_char buffer[4096];
	
    int len = sizeof(buffer);

	SnmpPdufixAggregate(request);

	assert(request->aggrpdu);

	assert(request->conn_fd > 0);

	request->aggrpdu->command = SNMP_PDU_RESPONSE;

	PrintSnmpOid(request->aggrpdu);

	snmp_build(request->session, request->aggrpdu, buffer, &len);

	comm_udp_sendto(request->conn_fd, request->address, sizeof(struct sockaddr_in), buffer, len);

	debugs(54, 6, "FD:%d, udp send bufflen:%d",request->conn_fd,len);

	SnmpSendComplete(request);
}

void SnmpNext(IpcSnmpRequest* request)
{
	++request->strandindex;
	
	int kidId = request->strands[request->strandindex].kidId;
	
	debugs(54, 4, "next kid:%d", kidId);
	
	TypedMsgHdr* message = packIpcSnmpRequest(kidId, request);

	message->timeout_handle.handle = SnmpHandleRequestTimeout;
	
	message->timeout_handle.data = request;

	CoordinatorSendMessageToStrand(kidId, message);
}

void SnmpCopyPdu(struct snmp_pdu* dest, struct snmp_pdu* src)
{
	int count = 0;
	variable_list** p_var = NULL;
	variable_list* vars = src->variables;
    dest->errstat = SNMP_DEFAULT_ERRSTAT;
    dest->errindex = SNMP_DEFAULT_ERRINDEX;	
    dest->command = src->command;
    dest->address.sin_addr.s_addr = src->address.sin_addr.s_addr;
    dest->reqid = src->reqid;
    dest->errstat = src->errstat;
    dest->errindex = src->errindex;
    dest->non_repeaters = src->non_repeaters;
    dest->max_repetitions = src->max_repetitions;
    dest->agent_addr.sin_addr.s_addr = src->agent_addr.sin_addr.s_addr;
    dest->trap_type = src->trap_type;
    dest->specific_type = src->specific_type;
    dest->time = src->time;

	if(src->enterprise_length > 0)
	{
		dest->enterprise = xcalloc(1, src->enterprise_length * sizeof(oid));
		memcpy(dest->enterprise, src->enterprise, src->enterprise_length * sizeof(oid));
	}

	for (p_var = &dest->variables; vars != NULL;
			vars = vars->next_variable, p_var = &(*p_var)->next_variable, ++count) {
				
		*p_var = (variable_list*)xcalloc(1, sizeof(variable_list));

		debugs(54, 4, "name:%p, name length:%d", vars->name, vars->name_length);
		
		if(vars->name && vars->name_length > 0)
		{
			(*p_var)->name = xcalloc(1, sizeof(oid) * vars->name_length);
			memcpy((*p_var)->name, vars->name, sizeof(oid) * vars->name_length);
			(*p_var)->name_length = vars->name_length;
		}

		debugs(54, 4, "str:%p, vars len:%d", vars->val.string, vars->val_len);
		
		if(vars->val.string && vars->val_len > 0)
		{
			(*p_var)->val.string = xcalloc(1, vars->val_len);
			memcpy((*p_var)->val.string, vars->val.string, vars->val_len);
		}
		(*p_var)->type = vars->type;
		(*p_var)->val_len = vars->val_len;
	}

	debugs(54, 4, "pdu cmd:%d,stat:%d,var count:%d", dest->command, dest->errstat,count);
}


void
SnmpHandleRequestTimeout(void* data)
{
	IpcSnmpRequest* request = (IpcSnmpRequest*)data;
	
	if(SnmpIsLastRequest(request))
	{
		debugs(54, 4, "remote kid:%d response snmp,send response", request->strands[request->strandindex].kidId);
		SnmpSendResponse(request);
	}
	else
	{
		debugs(54, 4, "remote kid:%d response snmp,send next", request->strands[request->strandindex].kidId);
		SnmpNext(request);
	}	
}

void
SnmpHandleRemoteAck(IpcSnmpResponse* response)
{
	IpcSnmpRequest* request = FindSnmpRequest(&TheCoordinatorInstance->TheRequestsList,response->requestId);

	assert(request != NULL);

	debugs(54, 4, "remote kid:%d, remote request id:%d", request->strands[request->strandindex].kidId, response->requestId);

	if(SnmpIsLastRequest(request))
	{
		if(SnmpPduaggregate(request, &response->pdu))
		{
			debugs(54, 4, "remote kid:%d response snmp,add complete,send response", request->strands[request->strandindex].kidId);
		}	
		SnmpSendResponse(request);
	}
	else 
	{
		if(SnmpPduaggregate(request, &response->pdu))
		{
			debugs(54, 4, "remote kid:%d response snmp,add complete,SnmpNext", request->strands[request->strandindex].kidId);
			SnmpNext(request);
		}
		else
		{
			debugs(54, 4, "remote kid:%d response snmp error:%d", request->strands[request->strandindex].kidId,request->aggrpdu->errstat);
			SnmpSendResponse(request);
		}
	}
	xfree(response);
}

void SnmpIpcStart(IpcSnmpRequest* request)
{
	TypedMsgHdr* message = packIpcSnmpRequest(request->strands[request->strandindex].kidId, request);

	message->timeout_handle.handle = SnmpHandleRequestTimeout;
	
	message->timeout_handle.data  = request;

	CoordinatorSendMessageToStrand(request->strands[request->strandindex].kidId, message);
}


#undef SNMP_REQUEST

#define IPC_HANDER

void CollapsedForwardingHandleNotification(const TypedMsgHdr* message)
{

}

void StrandHandleSnmpRequest(IpcSnmpRequest* request)
{
    IpcSnmpResponse response;
	
	debugs(54, 4, "kidid:%d,create snmp response", KidIdentifier);
	
	response.msgid = request->msgid;
	
	response.requestId = request->requestId;

	response.havepdu = 1;

	response.pdu = snmpAgentResponse(request->pdu);

	PrintSnmpOid(response.pdu);

	TypedMsgHdr* message = packIpcSnmpResponse(0, &response);
	
    StrandSendMessageToCoordinator(message);
}

void StrandHandleSnmpResponse(IpcSnmpResponse* response)
{
	IpcSnmpRequest* request = FindSnmpRequest(&TheStrandInstance->TheRequestsList, response->requestId);

	if(request != NULL)
	{
		debugs(54, 4, "kidid:%d,snmp response", KidIdentifier);

		debugs(54, 6, "eventDelete %p:%p", IpcSnmpForwardTimeOut, request);

		eventDelete(IpcSnmpForwardTimeOut, request);

		comm_remove_close_handler(request->conn_fd, SnmpForwardClosed, request);

		dlinkDelete(&request->node, &TheStrandInstance->TheRequestsList);

		//comm_close(request->conn_fd);
	}
	else
	{
		debugs(54, 1, "kidid:%d,received not exists snmp response request id %d", KidIdentifier, response->requestId);
	}

	xfree(response);
}

void StrandHandleRegistrationResponse(StrandCoord* response)
{
	// handle registration response from the coordinator; it could be stale
	if (response->kidId == KidIdentifier && response->pid == getpid()) {

		if(cbdataValid(response->msgid)) 
		{
			debugs(54, 6, "eventDelete %p:%p", IpcRegisterTimeout, response->msgid);
			eventDelete(IpcRegisterTimeout, response->msgid);
			cbdataFree(response->msgid);
		}
		
		//eventDelete(IpcWriteTimeout, message);
		debugs(54, 6, "kid %d registered",response->kidId);
	} else {
		// could be an ACK to the registration message of our dead predecessor
		debugs(54, 1, "kid %d is not yet registered", response->kidId);
	}

	xfree(response);
}

void StrandHandleSharedListenJoined(SharedListenResponse* response)
{
	SharedListenRequest *request;
	
	dlink_node *link, *tmplink;
	
	link = TheStrandInstance->delayrequests.head;
	
	while (link) {
		request = link->data;
		tmplink = link;
		link = link->next;
		if(request->mapId == response->mapId)
		{
			debugs(54, 4, "eventDelete %p:%p", IpcListenFdTimeout, response->msgid);

			if(cbdataValid(response->msgid)) 
			{
				eventDelete(IpcListenFdTimeout, response->msgid);

				cbdataFree(response->msgid);
			}
			
			comm_import_opened(response->fd, request->params.sock_type, FdNotes[request->params.fdNote], &request->params.addr, request->params.flags);

			request->callback(response->fd, request->data);		

			debugs(54, 6, "StrandHandleSharedListenJoined,remove request %p", request);
			
			--TheStrandInstance->TheRequestsListSize;
			
			dlinkDelete(&request->node, &TheStrandInstance->delayrequests);
			
			xfree(request);
			
			break;
		}
	}

	link = TheStrandInstance->delayrequests.head;

	if (link) {
		request = (SharedListenRequest*)link->data;
		if(request)
		{
			TypedMsgHdr* message = packSharedListenRequest(request);

			message->timeout_handle.handle = IpcListenFdTimeout;
			message->timeout_handle.data = message;
			
			debugs(54, 1, "send bind request %p", request);
			
			StrandSendMessageToCoordinator(message);
		}
	}
	xfree(response);
}

void CoordinatorSendSnmpForwardResponse(IpcSnmpRequest* request)
{
	IpcSnmpResponse resp;
	
	resp.msgid = request->msgid;
	
	resp.requestId = request->requestId;
	
	resp.pdu = NULL;
	
	TypedMsgHdr* message = packIpcSnmpResponse(request->requestorId, &resp);
		
	CoordinatorSendMessageToStrand(request->requestorId, message);
}

void CoordinatorSendForwardResponse(IpcMgrRequest* request)
{
	IpcMgrResponse resp;
	
	resp.requestId = request->requestId;
	
	resp.actionName = request->params->action;
	
	resp.actiondata = 0;

	TypedMsgHdr* message = packIpcMgrResponse(request->requestorId, &resp);
		
	CoordinatorSendMessageToStrand(request->requestorId, message);
}

void StrandHandleCacheMgrRequest(IpcMgrRequest* request)
{
	if(request->action->flags.aggregatable && request->action->collect && request->action->add)
	{
	    IpcMgrResponse response;
		
		response.msgid = request->msgid;
		
		response.requestId = request->requestId;
		
		response.actionName = request->params->action;

		response.actiondata = request->action->collect();
		
		response.actiondatalen = request->action->add(response.actiondata, NULL);

		debugs(54,6,"action:%s",response.actionName);
		
		TypedMsgHdr* message = packIpcMgrResponse(0, &response);
		
		StrandSendMessageToCoordinator(message);

		cbdataFree(request);
	}
	else
	{
		assert(request->conn_fd >= 0);
		
		request->conn_fd = ImportMgrForwardFdIntoComm(request->conn_fd);
		
		request->entry = storeCreateEntry(request->params->url, request->params->flags, urlMethodGetKnownByCode(request->params->method));
		
		request->sc = storeClientRegister(request->entry, (void*)request);
		
		storeLockObject(request->entry);

		StrandActionDump(request->entry, request);

		storeComplete(request->entry);
		
		debugs(54, 4, "FD:%d, entry object size:%d", request->conn_fd, objectLen(request->entry));
		
		storeClientRef(request->sc, request->entry, 0, 0, SM_PAGE_SIZE, InquirerSendData, (void*)request);			
	}
}

void StrandHandleCacheMgrResponse(IpcMgrResponse* resp)
{
	IpcMgrRequest* request = FindMgrRequest(&TheStrandInstance->TheRequestsList, resp->requestId);

	debugs(54, 4, "FD:%d,kidid:%d,action:%s", request->conn_fd, KidIdentifier, request->params->action);

	debugs(54, 6, "eventDelete %p:%p", IpcForwarderTimeOut, request);
	
	eventDelete(IpcForwarderTimeOut, request);	

	comm_close(request->conn_fd);
	
	xfree(resp);
}

void
CoordinatorHandleSharedListenRequest(SharedListenRequest* request)
{
    debugs(54, 4, "kid %d needs shared listen FD for %p",request->requestorId, &request->params.addr);

    int errNo = 0;
	
    int fd = CoordinatoropenListenSocket(request, &errNo);
	
	assert(errNo == 0);
	
	debugs(54, 3, "sending shared listen %d for %s to kid %d, mapId=%u",\
		sqinet_get_port(&request->params.addr),\
		inet_ntoa(sqinet_get_v4_inaddr(&request->params.addr, SQADDR_ASSERT_IS_V4)), request->requestorId, request->mapId);
		
    SharedListenResponse response;
	response.msgid = request->msgid;
	response.requestorId = request->requestorId;
	response.errNo = errNo;
	response.fd = fd;
	response.mapId = request->mapId;
	
    TypedMsgHdr* message = packSharedListenResponseMsg(&response);

    CoordinatorSendMessageToStrand(request->requestorId, message);

	xfree(request);
}

void CoordinatorHandleRegistrationRequest(StrandCoord* strand)
{
	debugs(54, 4, "kid:%d", strand->kidId);

    CoordinatorRegisterStrand(strand);

	TypedMsgHdr* message = packRegisterResponse(strand);

    CoordinatorSendMessageToStrand(strand->kidId, message);

	xfree(strand);
}


void CoordinatorHandleIpcMgrRequest(IpcMgrRequest* request)
{
	if (++TheCoordinatorInstance->LastRequestId == 0) 
	  ++TheCoordinatorInstance->LastRequestId;

	assert(request->conn_fd > 0);

	request->conn_fd = ImportMgrForwardFdIntoComm(request->conn_fd);

	request->requestId = TheCoordinatorInstance->LastRequestId;

	InquirerApplyQueryParams(TheCoordinatorInstance->strands, TheCoordinatorInstance->strandnumer, &request->strandnumer, request->strands, &request->params->params);
	
	assert(request->strandnumer > 0);
	
	const int kidId = request->strands[request->strandindex].kidId;

	debugs(54, 4, "inquire kid: %d", kidId);
	
	dlinkAdd(request, &request->node, &TheCoordinatorInstance->TheRequestsList);

	comm_add_close_handler(request->conn_fd, RequestClosed, request);
	
	InquirerIpcStart(request);
}

void CoordinatorHandleIpcMgrResponse(IpcMgrResponse* rsp)
{
	IpcMgrRequest* request = FindMgrRequest(&TheCoordinatorInstance->TheRequestsList, rsp->requestId);

	debugs(54, 4, "eventDelete %p:%p", InquirerhandleTimeout, request);

	eventDelete(InquirerhandleTimeout, request);

	InquirerhandleRemoteAck(rsp);
}

void CoordinatorHandleSnmpRequest(IpcSnmpRequest* request)
{
	if (++TheCoordinatorInstance->LastRequestId == 0) 
	++TheCoordinatorInstance->LastRequestId;

	assert(request->conn_fd > 0);
	
	request->aggrpdu = xcalloc(1, sizeof(struct snmp_pdu));

	SnmpCopyPdu(request->aggrpdu, request->pdu);

	request->aggrCount = 0;

	request->conn_fd = ImportSnmpForwardFdIntoComm(request->conn_fd);

	request->requestId = TheCoordinatorInstance->LastRequestId;

	memcpy(&request->strands, &TheCoordinatorInstance->strands, TheCoordinatorInstance->strandnumer * sizeof(StrandCoord));

	request->strandnumer = TheCoordinatorInstance->strandnumer;
	
	assert(request->strandnumer > 0);

	const int kidId = request->strands[request->strandindex].kidId;

	debugs(54, 4, "snmp kid: %d", kidId);

	dlinkAdd(request, &request->node, &TheCoordinatorInstance->TheRequestsList);

	comm_add_close_handler(request->conn_fd, SnmpRequestClosed, request);

	SnmpIpcStart(request);
}

void CoordinatorHandleSnmpResponse(IpcSnmpResponse* response)
{
	IpcSnmpRequest* request = FindSnmpRequest(&TheCoordinatorInstance->TheRequestsList, response->requestId);

	if(request)
	{
		debugs(54, 6, "eventDelete %p:%p", SnmpHandleRequestTimeout, request);

		eventDelete(SnmpHandleRequestTimeout, request);

		SnmpHandleRemoteAck(response);
	}
	else
	{
		debugs(54, 1, "coordinator received not exists snmp response requestid %d", response->requestId);
	}
}

void StrandRegisterSelf(Strand* self)
{
    assert(!self->isRegistered);

	debugs(54, 2, "kid:%d pid:%d, registerself", self->kidId,self->pid);
	
    TypedMsgHdr* message = packRegisterRequest(self);

	message->timeout_handle.handle = IpcRegisterTimeout;
	message->timeout_handle.data = message;
	
    StrandSendMessageToCoordinator(message);
}

void StrandSendListenRequest(SharedListenRequest* request)
{
	assert(TheStrandInstance);
	 
	dlinkAdd(request, &request->node, &TheStrandInstance->delayrequests);

	++TheStrandInstance->TheRequestsListSize;
	 
    if(TheStrandInstance->TheRequestsListSize > TheStrandInstance->concurrencyLimit)
	{
       debugs(54, 1, "listen request size %d, delay send requests %p", TheStrandInstance->TheRequestsListSize, request);
    }
	else
    {
		request->mapId = (void*)request;

		debugs(54, 1, "send bind request %p", request);
		
		TypedMsgHdr* message = packSharedListenRequest(request);

		message->timeout_handle.handle = IpcListenFdTimeout;
		message->timeout_handle.data = message;
			
		StrandSendMessageToCoordinator(message);
    }
}

void StrandStartListenRequest(int sock_type, int proto, int fdnote, int flags, struct in_addr* addr, short port, void* data, LISTEN* listen_callback)
{
	SharedListenRequest* request = xcalloc(1, sizeof(SharedListenRequest));
	request->requestorId = KidIdentifier;
	request->params.sock_type = sock_type;
	request->params.proto = proto;
	request->params.fdNote = fdnote;
	request->params.flags = flags;
	request->mapId = request;
	request->data = data;
	request->callback = listen_callback;

	sqinet_init(&request->params.addr);

	if(sock_type && proto == IPPROTO_TCP)
	{
		if(fdnote == fdnHttpSocket)
		{
			http_port_list* s = (http_port_list*)data;
			sqinet_set_v4_sockaddr(&request->params.addr, &s->s);
		}
#if USE_SSL			
		else if(fdnote == fdnHttpSocket)
		{
			https_port_list* https = (https_port_list*)port;
			http_port_list* s = &https->http;	
			sqinet_set_v4_sockaddr(&request->params.addr, &s->s);
		}
#endif	
		else
		{
			return;
		}
	}
	else if (proto == IPPROTO_UDP)
	{
		sqinet_set_v4_inaddr(&request->params.addr,addr);
		sqinet_set_v4_port(&request->params.addr,port,SQADDR_ASSERT_IS_V4);
	}
	else
	{
		return;
	}
	
	StrandSendListenRequest(request);
}

static void IpcSendNext(void* data, int cleanup)
{
	TypedMsgHdr* message = (TypedMsgHdr*)data;
	
	if(IamCoordinatorProcess())
	{
		dlinkDelete(&message->list, &TheCoordinatorInstance->ipcmessages[message->kidid]);
		
		TypedMsgHdr* nextmessage = (TypedMsgHdr*)dlinkRemoveHead(&TheCoordinatorInstance->ipcmessages[message->kidid]);
		
		if(cleanup) 
			cbdataFree(data);
		
		if(nextmessage)
		{
			debugs(54, 4, "send message %p", nextmessage);
			dlinkDelete(&nextmessage->list, &TheCoordinatorInstance->ipcmessages[nextmessage->kidid]);
			CoordinatorSendMessageToStrand(nextmessage->kidid, nextmessage);
		}		
	}
	else
	{
		dlinkDelete(&message->list, &TheStrandInstance->ipcmessages);
		
		TypedMsgHdr* nextmessage = (TypedMsgHdr*)dlinkRemoveHead(&TheStrandInstance->ipcmessages);
		
		if(cleanup) 
			cbdataFree(data);
		
		if(nextmessage)
		{
			debugs(54, 4, "send message %p", nextmessage);
			dlinkDelete(&nextmessage->list, &TheStrandInstance->ipcmessages);
			StrandSendMessageToCoordinator(nextmessage);
		}
	}

}

void IpcWriteData(int fd, char* buf, size_t size, int errflag , void *data)
{
	TypedMsgHdr* message = (TypedMsgHdr*)data;

	if (errflag != 0) 
	{
		assert(fd == *message->conn_fd);
		
		*message->conn_fd = -1;

		comm_close(fd);
		
		if(message->retries-- > 0)
		{
			debugs(54, 5, "fd %d flag %d retrie %d,add event retry %p:%p",fd,errflag, MAX_IPC_MESSAGE_RETRY - message->retries, message->retry_handle.handle, message->retry_handle.data);

			eventAdd("IpcWriteRetry", message->retry_handle.handle, message->retry_handle.data, message->restart_intval, 0);
		}
		else
		{
			debugs(54, 5, "fd %d flag %d,retries rich max %d",fd,errflag,MAX_IPC_MESSAGE_RETRY);

			IpcSendNext(message, 0);
		}
	}
	else
	{	
		message->success_handle.handle(message->success_handle.data);
	}	
}


int SendIpcMessage(TypedMsgHdr* message)
{
	debugs(54, 2, "FD is %d,message:%p, path:%s",*message->conn_fd,message,message->name.sun_path);

	if(*message->conn_fd < 0)
	{
		if(IamCoordinatorProcess())
		{
			*message->conn_fd = comm_open_uds(SOCK_DGRAM, 0, &message->name, COMM_NONBLOCKING);
		}
		else
		{
			*message->conn_fd = comm_open_uds(SOCK_DGRAM, 0, &message->name, COMM_NONBLOCKING);
		}
		debugs(54, 2, "reconnect,new fd %d, message:%p,path:%s",*message->conn_fd,message,message->name.sun_path);
	}
	
	if (!eventFind(message->timeout_handle.handle, message->timeout_handle.data))
	{
		debugs(54,2,"message %p add event timeout %p:%p", message, message->timeout_handle.handle, message->timeout_handle.data);
		eventAdd("SendIpcMessageTimeout", message->timeout_handle.handle, message->timeout_handle.data, message->timeout, 0);
	}

	dumps(54, 9, (unsigned char*)message->data.raw, message->data.size);
	
	comm_write(*message->conn_fd, (char*)message, sizeof(TypedMsgHdr), IpcWriteData, message, NULL);

	return 0;
}

void IpcWriteRetry(void* data)
{
	if(shutting_down) return;
	
	TypedMsgHdr* message = (TypedMsgHdr*)data;

	debugs(54, 6, "IpcWriteRetry,send message %p",message);
	
	if(!message->canceled)
	{
		SendIpcMessage(message);
	}
	else
	{
		if(message->kidid)
		{
			dlinkDelete(&message->list, &TheCoordinatorInstance->ipcmessages[message->kidid]);
		}
		else
		{
			dlinkDelete(&message->list, &TheStrandInstance->ipcmessages);
		}
	
		debugs(54, 6, "IpcWriteRetry,message %p canceled", message);
		
		IpcSendNext(message, 1);
	}
}

void IpcWriteComplete(void* data)
{
	TypedMsgHdr* message = (TypedMsgHdr*)data;
	
	int cleanup = 1;
	
	debugs(54, 4, "IpcWriteComplete message:%p, type:%d", message, message->data.type);
	
	switch(message->data.type)
	{
		case mtRegistration:
		{
			if(IamCoordinatorProcess())
			{
				debugs(54, 4, "eventDelete %p:%p", message->timeout_handle.handle, message->timeout_handle.data);
				eventDelete(message->timeout_handle.handle, message->timeout_handle.data);
				break;
			}
			else
			{
				cleanup = 0;
				break;
			}
		}

		case mtSharedListenRequest:
		{
			cleanup = 0;
			break;
		}

		case mtCacheMgrRequest:
		{
			cleanup = 0;
			break;
		}
			
		case mtCacheMgrResponse:
		{
			debugs(54, 4, "eventDelete %p:%p", message->timeout_handle.handle, message->timeout_handle.data);
			eventDelete(message->timeout_handle.handle, message->timeout_handle.data);			
			break;
		}
#if SQUID_SNMP		
		case mtSnmpRequest:
		{
			cleanup = 0;
			break;
		}
			
		case mtSnmpResponse:
		{
			debugs(54, 4, "eventDelete %p:%p", message->timeout_handle.handle, message->timeout_handle.data);
			eventDelete(message->timeout_handle.handle, message->timeout_handle.data);			
			break;
		}
#endif		
		default:
		{
				debugs(54, 4, "eventDelete %p:%p", message->timeout_handle.handle, message->timeout_handle.data);
				eventDelete(message->timeout_handle.handle, message->timeout_handle.data);
				break;
		}
	}
	
	IpcSendNext(data,cleanup);
}

void IpcWriteTimeout(void* data)
{
	if(shutting_down) return;
	
	TypedMsgHdr* message = (TypedMsgHdr*)data;

	debugs(54, 4, "IpcWriteTimeout");
	
	if(message->kidid)
	{
		dlinkDelete(&message->list, &TheCoordinatorInstance->ipcmessages[message->kidid]);
	}
	else
	{
		dlinkDelete(&message->list, &TheStrandInstance->ipcmessages);
	}
	
	cbdataFree(data);
}

void IpcClearMessage(void* data)
{
	debugs(54, 4, "IpcClearMessage");
}


int CoordinatorSendMessageToStrand(int kidId, TypedMsgHdr* message)
{
	assert(strcmp(message->name.sun_path, GetStrandAddr(kidId)) == 0);
	
	if (!message->clear_handle.handle)
	{
		message->clear_handle.handle = IpcClearMessage;
		message->success_handle.data = message;
	}
	
	if(!message->success_handle.handle) 
	{
		message->success_handle.handle = IpcWriteComplete;
		message->success_handle.data = message;
	}
	
	if(!message->retry_handle.handle) 
	{
		message->retry_handle.handle = IpcWriteRetry;
		message->retry_handle.data = message;
	}
	
	if(!message->timeout_handle.handle)  
	{
		message->timeout_handle.handle = IpcWriteTimeout;
		message->timeout_handle.data = message;
	}

	message->kidid = kidId;

	message->conn_fd = &TheCoordinatorInstance->kidsfd[kidId];

	if(DLINK_ISEMPTY(TheCoordinatorInstance->ipcmessages[kidId]))
	{
		dlinkAddTail(message, &message->list, &TheCoordinatorInstance->ipcmessages[kidId]);

		debugs(54, 4, "FD:%d,list empty,CoordinatorSendMessageToStrand add message to list and send", *message->conn_fd);
		
		SendIpcMessage(message);
	}
	else
	{

		debugs(54, 4, "FD:%d,list not empty,CoordinatorSendMessageToStrand add message", *message->conn_fd);
		
		dlinkAddTail(message, &message->list, &TheCoordinatorInstance->ipcmessages[kidId]);
	}
	
	return 0;
}

int StrandSendMessageToCoordinator(TypedMsgHdr* message)
{
	assert(strcmp(message->name.sun_path, GetCoordinatorAddr()) == 0);
	
	if (!message->clear_handle.handle)
	{
		message->clear_handle.handle = IpcClearMessage;
		message->success_handle.data = message;
	}
	
	if(!message->success_handle.handle) 
	{
		message->success_handle.handle = IpcWriteComplete;
		message->success_handle.data = message;
	}
	
	if(!message->retry_handle.handle) 
	{
		message->retry_handle.handle = IpcWriteRetry;
		message->retry_handle.data = message;
	}
	
	if(!message->timeout_handle.handle)  
	{
		message->timeout_handle.handle = IpcWriteTimeout;
		message->timeout_handle.data = message;
	}

	message->kidid = 0;

	message->conn_fd = &TheStrandInstance->coordinatorfd;

	if(DLINK_ISEMPTY(TheStrandInstance->ipcmessages))
	{
		dlinkAddTail(message, &message->list, &TheStrandInstance->ipcmessages);
		
		debugs(54, 2, "FD:%d,list empty,StrandSendMessageToCoordinator add message to list and send",*message->conn_fd);
		
		SendIpcMessage(message);
	}
	else
	{
		dlinkAddTail(message, &message->list, &TheStrandInstance->ipcmessages);

		debugs(54, 2, "FD:%d,list not empty,StrandSendMessageToCoordinator add message to list",*message->conn_fd);
	}

	return 0;
}



void CoordinatorProcessReceive(TypedMsgHdr* message)
{
	debugs(54, 2, "type:%d, size:%d", message->data.type, message->data.size);

	dumps(54, 9, (unsigned char*)message->data.raw, message->data.size);

	debugs(54, 2, "current msg request list size %d", dlinkSize(&TheCoordinatorInstance->TheRequestsList));

    switch (message->data.type) 
	{
	    case mtRegistration:
		{
			StrandCoord* regmessage = unPackRegisterRequest(message);
	        CoordinatorHandleRegistrationRequest(regmessage);
	        break;
	    }
	    case mtSharedListenRequest:
		{
			SharedListenRequest* request = unPackSharedListenRequest(message);
	        CoordinatorHandleSharedListenRequest(request);
	        break;
	    }	

	    case mtCacheMgrRequest: {
	        IpcMgrRequest* req = unPackIpcMgrRequest(message);
			CoordinatorSendForwardResponse(req);
	        CoordinatorHandleIpcMgrRequest(req);
	    }
	    break;

	    case mtCacheMgrResponse: {
	        IpcMgrResponse* resp = unPackIpcMgrResponse(message);
	        CoordinatorHandleIpcMgrResponse(resp);
	    }
	    break;

#if SQUID_SNMP
	    case mtSnmpRequest: {
	        IpcSnmpRequest* req = unPackIpcSnmpRequest(message);
			CoordinatorSendSnmpForwardResponse(req);		
	        CoordinatorHandleSnmpRequest(req);
	    }
	    break;

	    case mtSnmpResponse: {
	        IpcSnmpResponse* resp = unPackIpcSnmpResponse(message);
	        CoordinatorHandleSnmpResponse(resp);
	    }
	    break;
#endif

	    default:
	        debugs(54, 0, "Unhandled message type: %d", message->data.type);
	        break;
    }
}


void StrandProcessReceive(TypedMsgHdr* message)
{
	debugs(54, 2, "recive type:%d, size:%d", message->data.type, message->data.size);

	dumps(54, 9, (unsigned char*)message->data.raw, message->data.size);

	debugs(54, 2, "current msg request list size %d", dlinkSize(&TheStrandInstance->TheRequestsList));

    switch (message->data.type) 
	{
    case mtRegistration:
		{
	 		StrandCoord* response = unPackRegisterResponse(message);
       		StrandHandleRegistrationResponse(response);
        	break;
    	}
    case mtSharedListenResponse:
		{
			SharedListenResponse* rep = unPackSharedListenResponse(message);
        	StrandHandleSharedListenJoined(rep);
        	break;
    	}
    case mtCacheMgrRequest: {
        IpcMgrRequest* req = unPackIpcMgrRequest(message);
		assert(req->action);
        StrandHandleCacheMgrRequest(req);
    }
    break;

    case mtCacheMgrResponse: {
        IpcMgrResponse* resp = unPackIpcMgrResponse(message);
        StrandHandleCacheMgrResponse(resp);
		break;
    }
    case mtCollapsedForwardingNotification:
        CollapsedForwardingHandleNotification(message);
        break;

#if SQUID_SNMP
    case mtSnmpRequest: {
         IpcSnmpRequest* req = unPackIpcSnmpRequest(message);
         StrandHandleSnmpRequest(req);
    }
    break;

    case mtSnmpResponse: {
         IpcSnmpResponse* resp = unPackIpcSnmpResponse(message);
         StrandHandleSnmpResponse(resp);
    }
    break;
#endif

    default:
        debugs(54, 0, "Unhandled message type: %d" , message->data.type);
        break;
    }
}


void CoordinatorReadData(int fd, void* data)
{
    int size;

    debugs(54, 4, "CoordinatorReadData: FD %d: reading request...", fd);
	
    size = FD_READ_METHOD(fd, (char*)&TheCoordinatorInstance->msgbuf, sizeof(TypedMsgHdr));
	
    debugs(54, 4, "CoordinatorReadData: FD %d: read %d bytes", fd, size);

    if (size == 0){
		
    } 
	else if (size < 0)
	{
		if (!ignoreErrno(errno)) 
		{
		    debugs(54, 0, "clientReadRequest: FD %d: %s", fd, xstrerror());
		    comm_close(fd);
		    return;
		} else {
		    debugs(54, 2, "CoordinatorReadData: FD %d: no data to process (%s)", fd, xstrerror());
		}
	}
	else
	{
		CoordinatorProcessReceive(&TheCoordinatorInstance->msgbuf);
	}

	memset(&TheCoordinatorInstance->msgbuf, 0, sizeof(TypedMsgHdr));
	
	initReceivedMsgHdr(&TheCoordinatorInstance->msgbuf, GetCoordinatorAddr());

	commSetSelect(fd, COMM_SELECT_READ, CoordinatorReadData, NULL, 0);
}

void StrandReadData(int fd, void* data)
{
	 int size;

	 debugs(54, 4, "StrandReadData: FD %d: reading request...", fd);
	 
	 size = FD_READ_METHOD(fd, (char*)&TheStrandInstance->msgbuf, sizeof(TypedMsgHdr));
	 
	 debugs(54, 4, "StrandReadData: FD %d: read %d bytes", fd, size);
	
	 if (size == 0){
			 
	
	 } 
	 else if (size < 0) 
	 {
		 if (!ignoreErrno(errno)) 
		 {
			 debugs(54, 0, "StrandReadData: FD %d: %s", fd, xstrerror());
			 comm_close(fd);
			 return;
		 } else {
			 debugs(54, 2, "StrandReadData: FD %d: no data to process (%s)", fd, xstrerror());
		 }
	 }
	 else
	 {
		 StrandProcessReceive(&TheStrandInstance->msgbuf);
	 }

	 initReceivedMsgHdr(&TheStrandInstance->msgbuf,GetStrandAddr(KidIdentifier));
	
	 commSetSelect(fd, COMM_SELECT_READ, StrandReadData, NULL, 0);
}

void StartIpcStrandInstance()
{
	if(!TheStrandInstance)
	{
		initIpcStrandInstance();
	}
	
	strcpy(TheStrandInstance->address.sun_path,GetStrandAddr(KidIdentifier));

	initReceivedMsgHdr(&TheStrandInstance->msgbuf,GetStrandAddr(KidIdentifier));

	TheStrandInstance->conn_fd = comm_open_uds(SOCK_DGRAM, 0, &TheStrandInstance->address, TheStrandInstance->options);

	if(TheStrandInstance->conn_fd < 0)
	{
		debugs(54, 0, "open address:%s failed", TheStrandInstance->address.sun_path);
		exit(1);
	}
	else
	{
		debugs(54, 2, "open address:%s success", TheStrandInstance->address.sun_path);

		commSetSelect(TheStrandInstance->conn_fd, COMM_SELECT_READ, StrandReadData, NULL, 0);	

		StrandRegisterSelf(TheStrandInstance);
	}
}

void StartIpcCoordinatorInstance()
{
	initIpcCoordinatorInstance();
	
	strcpy(TheCoordinatorInstance->address.sun_path,GetCoordinatorAddr());
	
	initReceivedMsgHdr(&TheCoordinatorInstance->msgbuf, GetCoordinatorAddr());

	TheCoordinatorInstance->conn_fd = comm_open_uds(SOCK_DGRAM, 0, &TheCoordinatorInstance->address, TheCoordinatorInstance->options);

	if(TheCoordinatorInstance->conn_fd < 0)
	{
		debugs(54, 0, "StartIpcCoordinatorInstance open address:%s failed", TheCoordinatorInstance->address.sun_path);
		exit(1);
	}
	else
	{
		debugs(54, 2, "StartIpcCoordinatorInstance open address:%s success", TheCoordinatorInstance->address.sun_path);
		
		commSetSelect(TheCoordinatorInstance->conn_fd, COMM_SELECT_READ, CoordinatorReadData, NULL, 0);
	}	
}


#undef IPC_HANDER


