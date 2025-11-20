#if !_PS3
int larva_colony_size = 20;
AUTO_CMD_INT(larva_colony_size, LarvaColonySize) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

#include "net.h"
#include "sock.h"
#include "netprivate.h"
#ifdef _IOCP
#include "netlink.h"

#include "earray.h"
#include "timing.h"
#include "EventTimingLog.h"
#include "netreceive.h"
#include "netprivate.h"
#include "RingStream.h"
#include "stringCache.h"

#include <winsock2.h>
#include <mswsock.h>

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

int safeWSARecv(SOCKET sock, int len, Packet *pak,const char* debugLocName)
{
	NetLink*	link = pak->link;
	ULONG		ulFlags = MSG_PARTIAL;
	WSABUF		bufferArray;
	int         received = 0;

	if (g_force_sockbsd || linkIsFake(link))
		return 1;
	if (sock==INVALID_SOCKET)
	{
		linkSetDisconnectReason(link, "sock==INVALID_SOCKET");
		return -1;
	}
	if (pak->ol_queued)
		return 0;
	pak->ol_queued_loc_name = debugLocName;
	pak->ol_queued = 1;
	pak->receiving = 1;
	ZeroStruct(&pak->ol);
	bufferArray.len = len;
	bufferArray.buf = pak->data + pak->size;

	// COR-14625
	// For the fourth parameter below, lpNumberOfBytesRecvd, MSDN 8/18/2011 claims the following:
	// "Use NULL for this parameter if the lpOverlapped parameter is not NULL to avoid potentially
	// erroneous results. This parameter can be NULL only if the lpOverlapped parameter is not NULL."
	// It's not clear if they mean that the value of lpNumberOfBytesRecvd might be misleading, or that
	// passing a non-null pointer could somehow break something in some otherwise undocumented way.
	// However, we observed that on Vista x64 Home Premium, using wpclsp.dll 1.00.0.1 with
	// timestamps 11/2/2006 8:03 AM (and 1/19/2008 12:32 AM on some customer machines), if you pass a
	// null pointer for that parameter, it will cause the process to crash if Parental Controls is
	// enabled on "Medium" or higher.
	// So we pass the parameter to work around the crash, but don't use the value for anything.
	// Since this was how things were for several years before it was recently fixed to pass a null
	// pointer, we have good reason to believe this is probably OK, despite the scare tactics in the
	// documentation.

	link->wsaRecvResult = WSARecv(sock,&bufferArray,1,&received,&ulFlags,&pak->ol,NULL);

	if(link->wsaRecvResult != SOCKET_ERROR)
	{
		link->wsaRecvError = 0;

		linkVerbosePrintf(	link,
							"recv %d %d",
							link->wsaRecvResult,
							link->wsaRecvError);
	}
	else
	{
		link->wsaRecvError = WSAGetLastError();

		linkVerbosePrintf(	link,
							"recv %d %d",
							link->wsaRecvResult,
							link->wsaRecvError);

		if(link->wsaRecvError != WSA_IO_PENDING)
		{
			pak->ol_queued = 0;
			if (isDisconnectError(link->wsaRecvError))
			{
				char tempErrorString[64];
				sprintf(tempErrorString, "disconnect error: %d", link->wsaRecvError);
				pak->receiving = 0;
				linkSetDisconnectReason(link, allocAddString(tempErrorString));
				return -1;
			}
			if(link->wsaRecvError != WSAENOBUFS) 
				printWinErr("WSARecv", __FILE__, __LINE__, link->wsaRecvError);
		}
	}
	return 0;
}

static void netAcceptEx(SOCKET listen_sock,SOCKET empty_sock,void *info_buf,OVERLAPPED *ol)
{
	static	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	static	LPFN_ACCEPTEX lpfnAcceptEx;
	DWORD	dwBytes;

	if (!lpfnAcceptEx)
	{
		WSAIoctl(	listen_sock, 
					SIO_GET_EXTENSION_FUNCTION_POINTER, 
					&GuidAcceptEx, 
					sizeof(GuidAcceptEx),
					&lpfnAcceptEx, 
					sizeof(lpfnAcceptEx), 
					&dwBytes, 
					NULL, 
					NULL);
	}

	lpfnAcceptEx(	listen_sock, 
					empty_sock,
					info_buf, 
					0,//outBufLen - ((sizeof(struct sockaddr_in) + 16) * 2),
					sizeof(struct sockaddr_in) + 16, 
					sizeof(struct sockaddr_in) + 16, 
					&dwBytes, 
					ol);
}

void addListenAcceptSock(NetListen *nl)
{
	SOCKET		accept_sock;
	NetLink		*link;
	Packet		*listen_msg;
	HANDLE		new_port;

	PERFINFO_AUTO_START_FUNC();
	
	accept_sock = socketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	link = linkCreate(accept_sock,nl, nl->eLinkType, nl->creationFile, nl->creationLine);
	linkVerbosePrintf(link, "Created accept socket %d", accept_sock);
	link->listening = 1;
	listen_msg = pktCreateRaw(link);
	netAcceptEx(nl->listen_sock,link->sock,listen_msg->data,&listen_msg->ol);
	new_port = CreateIoCompletionPort((HANDLE)link->sock, nl->comm->completionPort, 0, 0);
	assert(new_port);
	
	PERFINFO_AUTO_STOP();
}

void netListenStartIocpAccept(NetListen *nl)
{
	int		i;
	HANDLE	valid_cmd;
	NetComm	*comm = nl->comm;

	if (!comm->completionPort)
		return;
	valid_cmd = CreateIoCompletionPort((HANDLE)nl->listen_sock, comm->completionPort, 0, 0);
	assert(valid_cmd == comm->completionPort);
	for(i=0;i<larva_colony_size;i++)
		addListenAcceptSock(nl);
}

static __forceinline int linkIocpAccept(NetLink *link,Packet *pkt,int bytes_xferred)
{
	int			err=0;
	NetListen	*listen = link->listen;
	int			result;

	PERFINFO_AUTO_START_FUNC();

	addListenAcceptSock(listen);
	link->listening = 0;
	err = setsockopt( link->sock,SOL_SOCKET,SO_UPDATE_ACCEPT_CONTEXT,(char *)&listen->listen_sock,sizeof(SOCKET) );
	if (err)
		err = WSAGetLastError();
	pktFree(&pkt);
	if (err)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	result = linkActivate(link,bytes_xferred);
	PERFINFO_AUTO_STOP();
	return result;
}

void linkSetDisconnectReason(NetLink* link, const char* reason)
{
	if(!link->disconnect_reason){
		link->disconnect_reason = reason;
	}

	if (link->flags & LINK_CRAZY_DEBUGGING)
	{
		if (link->debug_early_send_ring)
			ringStreamWriteDebugFile(link->debug_early_send_ring, "CrazyDebuggingSendEarly");
		if (link->debug_late_send_ring)
			ringStreamWriteDebugFile(link->debug_late_send_ring, "CrazyDebuggingSendLate");
		if (link->debug_recv_ring)
			ringStreamWriteDebugFile(link->debug_recv_ring, "CrazyDebuggingRecv");
		if (link->compress)
		{
			if (link->compress->debug_send_ring)
				ringStreamWriteDebugFile(link->compress->debug_send_ring, "CrazyDebuggingSendCompress");
			if (link->compress->debug_recv_ring)
				ringStreamWriteDebugFile(link->compress->debug_recv_ring, "CrazyDebuggingRecvCompress");
		}
	}
}

U32 pktGetWSAErrorAfterIOCompletion(Packet* pak){
	SOCKET	sock = pak->link->sock;
	U32		error = 0;
	
	if(sock != INVALID_SOCKET){
		U32	byteCount;
		U32	flags;

		PERFINFO_AUTO_START_FUNC();
		WSAGetOverlappedResult(sock, &pak->ol, &byteCount, 0, &flags);
		error = WSAGetLastError();
		PERFINFO_AUTO_STOP();
	}
	return error;
}

void linkDisconnectedByIOCP(NetLink* link,
							Packet* pak,
							NetLink*** remove_list,
							const char* reason)
{
	link->wsaRecvError = pktGetWSAErrorAfterIOCompletion(pak);
	if (link->recv_pak)
		link->recv_pak->receiving = 0;
	eaPushUnique(remove_list, link);
	linkSetDisconnectReason(link, reason);
	linkStatus(link, reason);
}

void iocpProcessPackets(NetComm *comm,
						S32 timeoutOverrideMilliseconds,
						U32 maxMilliseconds)
{
	int				i,result,bytes_xferred;
	Packet			*pkt;
	OVERLAPPED		*ol_ptr;
	NetLink			*link;
	NetLink			**remove_list = 0;
	int				timeout = timeoutOverrideMilliseconds >= 0 ?
									timeoutOverrideMilliseconds :
									comm->timeout_msecs;
	U32				startTime = timeGetTime();
	U32				count = 0;

	if (!comm->completionPort)
		return;

	PERFINFO_AUTO_START_FUNC();

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	comm->last_check_ms = timeGetTime();

	while(	!count ||
			maxMilliseconds == INFINITE ||
			timeGetTime() - startTime < maxMilliseconds)
	{
		int	error = 0;
		int aborted = 0;
		ULONG_PTR completion_key;  // Currently unused, set to zero.

		if(!count++){
			PERFINFO_AUTO_START_BLOCKING("GetQueuedCompletionStatus", 1);
		}else{
			PERFINFO_AUTO_START_BLOCKING("GetQueuedCompletionStatus(not-first)", 1);
		}
		result = GetQueuedCompletionStatus(comm->completionPort, &bytes_xferred, &completion_key, &ol_ptr, timeout);
		if (!result)
		{
			error = GetLastError();
		}
		PERFINFO_AUTO_STOP();
		timeout = 0;

		pkt = (Packet *)((char*)ol_ptr - offsetof(Packet,ol));
		
		if (!result)
		{
			if (error == WAIT_TIMEOUT || error == ERROR_TIMEOUT)
			{
				break;
			}
			else if (error == ERROR_OPERATION_ABORTED)
			{
				pkt->ol_queued = 0;
				bytes_xferred = 0;
				aborted = 1;
			}
			else if (error == ERROR_MORE_DATA)
			{
				bytes_xferred = 0;
			}
		}

		if(bytes_xferred)
		{
			ADD_MISC_COUNT(bytes_xferred, "bytes_xferred");
		}
		
		pkt->ol_queued = 0;
		link = pkt->link;

		linkVerbosePrintf(	link,
							"received %d bytes",
							bytes_xferred);

		linkGrowRecvBuf(link,bytes_xferred);
		
		if(aborted)
		{
			link->received_abort_count++;
		}
		
		if(	!aborted &&
			!bytes_xferred &&
			!link->listening)
		{
			linkDisconnectedByIOCP(link, pkt, &remove_list, "socket was shutdown");
		}
		else if (link->sock==INVALID_SOCKET)
		{
			linkDisconnectedByIOCP(link, pkt, &remove_list, "socket is invalid");
		}
		else if (pkt->receiving)
		{
			pkt->receiving = 0;
			if (bytes_xferred)
			{
				link->stats.recv.real_packets++;
				link->stats.recv.real_bytes+=bytes_xferred;
			}
			if (!linkReceiveCore(link,bytes_xferred))
			{
				eaPushUnique(&remove_list,link);
				linkSetDisconnectReason(link, "read failed");
				linkStatus(link,"read failed");
			}
		}
		else if (link->listen->listen_sock!=INVALID_SOCKET)
		{
			// New connection on a listening socket.
			if (!linkIocpAccept(link,pkt,bytes_xferred))
			{
				eaPushUnique(&remove_list,link);
				linkSetDisconnectReason(link, "accept failed");
				linkStatus(link,"accept failed");
			}
			else
			{
				linkStatus(link,"accept");

				// Flag all raw links as connected upon successful accept
				if(link->flags & LINK_RAW)
				{
					link->connected = 1;

					linkUserConnect(link);
				}
			}
		}
		else assert(0);
	}
	if (remove_list)
	{
		for(i=0;i<eaSize(&remove_list);i++)
		{
			linkStatus(remove_list[i],"netio call linkQueueRemove");
			linkQueueRemove(remove_list[i], NULL);
		}
		eaDestroy(&remove_list);
	}

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP_FUNC();
}


#endif
#endif
