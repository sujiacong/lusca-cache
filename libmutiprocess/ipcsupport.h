#ifndef __IPC_SUPPORT_H__
#define __IPC_SUPPORT_H__

#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

#include "config.h"
#include "../libcore/dlink.h"

#define MAX_KID_SUPPORT 32
#define PARAMS_SIZE_MAX 10
#define APP_SHORTNAME 			"squid"

#define MAX_IPC_MESSAGE_RETRY 		3
#define MAX_IPC_MESSAGE_TIMEOUT 	10
#define MAX_IPC_MESSAGE_RETRY_INT	2

#define SQUID_CMSG_SPACE(len) (sizeof(struct cmsghdr) + (len) + 16)

/// message class identifier
typedef enum { mtNone = 0, mtRegistration,
               mtStrandSearchRequest, mtStrandSearchResponse,
               mtSharedListenRequest, mtSharedListenResponse,
               mtIpcIoNotification,
               mtCollapsedForwardingNotification,
               mtCacheMgrRequest, mtCacheMgrResponse
#if SQUID_SNMP
               ,
               mtSnmpRequest, mtSnmpResponse
#endif		

} MessageType;

/// We cannot send char* FD notes to other processes. Pass int IDs and convert.

/// fd_note() label ID
typedef enum { fdnNone, fdnHttpSocket, fdnHttpsSocket, fdnFtpSocket,
#if SQUID_SNMP
               fdnInSnmpSocket, fdnOutSnmpSocket,
#endif
               fdnInIcpSocket, fdnInHtcpSocket, fdnEnd
} FdNoteId;

typedef void IPCCB(void *data);
typedef void LISTEN(int fd, void *data);

typedef struct _IPCCallback
{
	IPCCB* handle;
	void*  data;
}IPCCallback;

typedef struct _TypedMsgHdr
{
    void *msg_name;             /* Address to send to/receive from.  */
    socklen_t msg_namelen;      /* Length of address data.  */
    struct iovec *msg_iov;      /* Vector of data to send/receive into.  */
    size_t msg_iovlen;          /* Number of elements in the vector.  */
    void *msg_control;          /* Ancillary data (eg BSD filedesc passing). */
    size_t msg_controllen;      /* Ancillary data buffer length.!! The type should be socklen_t but the definition of the kernel is incompatible with this.  */
    int msg_flags;              /* Flags on received message.  */
    struct sockaddr_un name; 	///< same as .msg_name
    struct iovec ios[1]; 		///< same as .msg_iov[]
    struct DataBuffer {
	   int type; 				///< Message kind, uses MessageType values
	   size_t size; 			///< actual raw data size (for sanity checks)
	   char raw[4096]; 			///< buffer with type-specific data
   } data; 						///< same as .msg_iov[0].iov_base
   struct CtrlBuffer {
	   							/// control buffer space for one fd
	   char raw[SQUID_CMSG_SPACE(sizeof(int))];
   } ctrl; 						///< same as .msg_control
					
   unsigned int offset; 		/// data offset for the next get/put*() to start with
   int canceled;
   int* conn_fd;
   int retries;
   int timeout;
   int restart_intval;
   int kidid;
   
   IPCCallback timeout_handle;
   IPCCallback success_handle;
   IPCCallback retry_handle;
   IPCCallback clear_handle;

   dlink_node list;
}TypedMsgHdr;


extern int SendIpcMessage(TypedMsgHdr* message);
extern int StrandSendMessageToCoordinator(TypedMsgHdr* message);
extern int CoordinatorSendMessageToStrand(int kidId,TypedMsgHdr* message);
extern void initIpcStrandInstance();
extern void initIpcCoordinatorInstance();
extern void StartIpcCoordinatorInstance();
extern void StartIpcStrandInstance();
extern void CoordinatorBroadcastSignal(int sig);
extern const char* IpcFdNote(int fdNoteId);
extern void StrandStartListenRequest(int sock_type, int proto, int fdnote, int flags, struct in_addr* addr, short port, void* data, LISTEN* listen_callback);

#endif

