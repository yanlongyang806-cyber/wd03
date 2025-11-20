#include "../../3rdparty/zlib/zlib.h"
#include "sock.h"
#include "net.h"
#include "netprivate.h"
#include "netsendthread.h"
#include "timing.h"
#include "utils.h"
#include "earray.h"
#include "netlink.h"
#include "netreceive.h"
#include "Queue.h"
#include "stashTable.h"
#include "resourceInfo.h"
#include "NetPrivate_h_ast.h"
#include "continuousbuilderSupport.h"
#include "RingStream.h"
#include "CrypticPorts.h"
#include "globalComm.h"
#include "file.h"
#include "scratchstack.h"

extern bool gbFailAllCommConnects;



// Debug capture instructions, with example port 1234
//
// This captures outgoing traffic on a specific NetLink from a server with a listening port.
// Currently, it does not support capturing incoming traffic, or capturing traffic on an outgoing NetLink.
//
// 1. Apply -NetCaptureBufferSize 1024 on the server, where 1024 is an appropriate size.
// 2. Apply -NetAllowCaptureRequestOnPort 1234 to the server that will be doing the capture.
// 3. Apply -NetRequestPeerCaptureOnPort 1234 to the client that will be connecting to the server, on that port.
// 4. Once sufficient data has been captured, run "netStopCapture 1" on the server.
// 5. When ready to write to disk, run "netDumpCapture c:\filename.dat" on the server, where c:\filename.dat is the file to dump to.

// Set to a port number to capture on once.
static U32 sNetAllowCaptureRequestOnPort = 0;
AUTO_CMD_INT(sNetAllowCaptureRequestOnPort, netAllowCaptureRequestOnPort) ACMD_CATEGORY(Debug);

// Set the size of the capture buffer for AllowCaptureRequestOnPort in bytes.
static unsigned sNetCaptureBufferSize = 1024;
AUTO_CMD_INT(sNetCaptureBufferSize, NetCaptureBufferSize) ACMD_CATEGORY(Debug);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

CRITICAL_SECTION linkListSection = {0};
StashTable sLinksByID = NULL;

static int sNextLinkID = 1;
static U32 netLinkCount;

U32 linkGetTotalCount(void){
	return netLinkCount;
}

AUTO_RUN_FIRST;
void linkListInitSystem(void)
{
	InitializeCriticalSection(&linkListSection);
}

void linkAddToGlobalList(NetLink *link)
{
	EnterCriticalSection(&linkListSection);
	link->ID = sNextLinkID++;
	sprintf(link->IDString, "%d", link->ID);

	if (!sLinksByID)
	{
		sLinksByID = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("NetLinks", RESCATEGORY_SYSTEM, 0, sLinksByID, parse_NetLink);
	}

	stashAddPointer(sLinksByID, link->IDString, link, false);

	LeaveCriticalSection(&linkListSection);
}

NetLink *linkFindByID(U32 id)
{
	NetLink *link = NULL;
	EnterCriticalSection(&linkListSection);
	if (sLinksByID)
	{
		char idString[32];
		sprintf(idString, "%d", id);
		stashFindPointer(sLinksByID, idString, &link);
	}
	LeaveCriticalSection(&linkListSection);
	return link;
}

void linkRemoveFromGlobalList(NetLink *link)
{	
	EnterCriticalSection(&linkListSection);

	stashRemovePointer(sLinksByID, link->IDString, NULL);

	LeaveCriticalSection(&linkListSection);
}

const char *linkError(NetLink *link)
{
	return link->error;
}

bool linkErrorNeedsEncryption(NetLink *link)
{
	if (!link->error)
		return false;
	return !!strstri(link->error, "request encrypt");
}

bool linkWasRepurposedForXMLRPC(NetLink *link)
{
	return !!(link->flags & LINK_REPURPOSED_XMLRPC);
}

void linkSetUserData(NetLink* link, void* userData)
{
	if (!link->lost_user_data_ownership)
		SAFE_FREE(link->user_data);
	link->user_data = userData;
	link->lost_user_data_ownership = 1;
}


void linkDestroyTPICaches(NetLink *link)
{
	int i;

	for (i= 0 ; i < eaSize(&link->ppReceivedTPIs); i++)
	{
		if (link->ppReceivedTPIs[i]->ppReceivedTPIs)
		{
			ParseTableFree(&link->ppReceivedTPIs[i]->ppReceivedTPIs);
		}
		free(link->ppReceivedTPIs[i]);
	}
	eaDestroy(&link->ppReceivedTPIs);
	eaDestroy(&link->ppSentTPIs);
}

void linkDestroyLaggedPacketQueue(NetLink* link)
{
	if (link->pkt_lag_queue)
	{
		LaggedPacket* lp;

		if(eaFindAndRemove(&link->listen->linksWithLaggedPackets, link) < 0){
			FatalErrorf("link 0x%p should have been in linksWithLaggedPackets", link);
		}

		if(!eaSize(&link->listen->linksWithLaggedPackets)){
			eaDestroy(&link->listen->linksWithLaggedPackets);
		}

		while(lp = qDequeue(link->pkt_lag_queue))
		{
			pktFree(&lp->pkt);
			SAFE_FREE(lp);
		}
		destroyQueue(link->pkt_lag_queue);
		link->pkt_lag_queue = NULL;
	}
}

void linkFree(NetLink *link)
{
	Packet	*recv_pak;
	int		orig,curr;
	
	PERFINFO_AUTO_START_FUNC();
	
	ASSERT_FALSE_AND_SET(link->called_linkFree);
	
	linkRemoveFromGlobalList(link);

	linkUserDisconnect(link);
	
	//printf(	"Freeing link %p (send buf %d/%d, recv buf %d/%d)\n",
	//		link,
	//		link->curr_sendbuf,
	//		link->max_sendbuf,
	//		link->curr_recvbuf,
	//		link->max_recvbuf);
	
	// Make sure the link is really done.
	
	assert(!link->outbox);
	assert(!link->send_queue_head);
	assert(!link->send_queue_tail);
	if(link->recv_pak)
	{
		assert(!link->recv_pak->ol_queued);
	}
	assert(!link->connected);

	orig = eaSize(&link->listen->links);
	eaFindAndRemove(&link->listen->links,link);
	curr = eaSize(&link->listen->links);
	eaFindAndRemove(&link->listen->comm->flushed_disconnects,link);
	eaFindAndRemove(&link->listen->comm->remove_list,link);
	linkStatus(link,"delinked");
	recv_pak = link->recv_pak;
	link->recv_pak = 0;
	pktFree(&recv_pak);

	if (link->debug_recv_ring)
	{
		ringStreamDestroy(link->debug_recv_ring);
		link->debug_recv_ring = 0;
	}
	if (link->debug_early_send_ring)
	{
		ringStreamDestroy(link->debug_early_send_ring);
		link->debug_early_send_ring = 0;
	}
	if (link->debug_late_send_ring)
	{
		ringStreamDestroy(link->debug_late_send_ring);
		link->debug_late_send_ring = 0;
	}

	estrDestroy(&link->estr_send_capture_buf);
	
	SAFE_FREE(link->error);
	if (link->encrypt)
	{
		SAFE_FREE(link->encrypt->encode);
		SAFE_FREE(link->encrypt->decode);
		SAFE_FREE(link->encrypt);
	}
	if (link->compress)
	{
		NetCompress* compress = link->compress;
		
		if(TRUE_THEN_RESET(compress->debugIsAllocated))
		{
			netCompressDebugFree(compress);
		}
		
		deflateEnd(compress->send);
		if (compress->send_verify)
		{
			inflateEnd(compress->send_verify);
			SAFE_FREE(compress->send_verify);
		}
		inflateEnd(compress->recv);
		SAFE_FREE(compress->send);
		SAFE_FREE(compress->recv);
		if (compress->debug_recv_ring)
		{
			ringStreamDestroy(compress->debug_recv_ring);
			compress->debug_recv_ring = 0;
		}
		if (compress->debug_send_ring)
		{
			ringStreamDestroy(compress->debug_send_ring);
			compress->debug_send_ring = 0;
		}
		SAFE_FREE(link->compress);
	}
	linkDestroyLaggedPacketQueue(link);

	linkDestroyTPICaches(link);


	if (link->pLinkFileSendingMode_ReceiveManager)
	{
		linkFileSendingMode_DestroyReceiveManager(link->pLinkFileSendingMode_ReceiveManager);
		link->pLinkFileSendingMode_ReceiveManager = NULL;
	}
	if (link->pLinkFileSendingMode_SendManager)
	{
		linkFileSendingMode_DestroySendManager(link->pLinkFileSendingMode_SendManager);
		link->pLinkFileSendingMode_SendManager = NULL;
	}


	StructDestroySafe(parse_LinkReceiveStats, &link->pReceiveStats);

	if(	link->listen->listen_sock != INVALID_SOCKET ||
		link->cleared_user_link_ptr)
	{
		if(link->listen->listen_sock == INVALID_SOCKET && !eaSize(&link->listen->links))
		{
			eaFindAndRemove(&link->listen->comm->listen_list, link->listen);
			eaDestroy(&link->listen->links);
			free(link->listen);
		}

		// client links have to free themselves for unexpected disconnects
		free(link);
		
		InterlockedDecrement(&netLinkCount);
	}
	else
	{
		link->connected = 0;
		link->disconnected = 1;
	}



	PERFINFO_AUTO_STOP();
}

void linkRemove_wReason(NetLink **link_ptr, const char *disconnect_message)
{
	NetLink	*link = SAFE_DEREF(link_ptr);

	if (!link)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	*link_ptr = NULL;

	linkStatus(link,"LinkRemove");

	if (link->disconnected)
	{
		bool fake = linkIsFake(link);
		if(!fake && link->listen->listen_sock == INVALID_SOCKET && !eaSize(&link->listen->links))
		{
			eaFindAndRemove(&link->listen->comm->listen_list, link->listen);
			eaDestroy(&link->listen->links);
			free(link->listen);
		}

		// client was notified that the link disconnected, everything has already been freed except the link struct
		free(link);
		if (!fake)
			InterlockedDecrement(&netLinkCount);
	}
	else
	{
		link->cleared_user_link_ptr = 1;
		linkQueueRemove(link, disconnect_message);
	}
	
	PERFINFO_AUTO_STOP();
}

int linkConnected(NetLink *link)
{
	return link && link->connected;
}

int linkDisconnected(NetLink *link)
{
	return link && link->disconnected;
}

F32 linkRecvTimeElapsed(NetLink *link)
{
	return SAFE_MEMBER(link, stats.recv.last_time_ms) ? 
				(F32)(link->listen->comm->last_check_ms - link->stats.recv.last_time_ms) / 1000.f:
				0;
}

void linkSetTimeout(NetLink *link,F32 seconds)
{
	if(!link)
	{
		return;
	}
	if (!seconds)
		link->no_timeout = 1;
	else
		 link->no_timeout = 0;
	link->timeout = seconds;
}

void linkSetKeepAliveSeconds(NetLink *link,U32 seconds)
{
	link->keep_alive_interval_seconds = seconds;
	if (seconds)
		link->keep_alive_prev_milliseconds = timeGetTime();
	else
		link->keep_alive_prev_milliseconds = 0;
}

void linkSetKeepAlive(NetLink *link)
{
	linkSetKeepAliveSeconds(link,60);
}

void linkNoWarnOnResize(NetLink *link)
{
	if (link)
		link->sentResizeAlert = LINK_RESIZES_WITH_WARNING_AFTER_MAX;
}

void linkIterate(NetListen *listen,LinkCallback callback)
{
	int		i;

	for(i=eaSize(&listen->links)-1;i>=0;i--)
	{
		if (listen->links[i]->connected && !listen->links[i]->cleared_user_link_ptr)
			callback(listen->links[i],listen->links[i]->user_data);
	}
}

void linkIterate2(NetListen *listen,LinkCallback2 callback, void* user_data)
{
	int		i;

	for(i=eaSize(&listen->links)-1;i>=0;i--)
	{
		if (listen->links[i]->connected && !listen->links[i]->cleared_user_link_ptr)
		{
			if(!callback(listen->links[i], i, listen->links[i]->user_data, user_data))
			{
				break;
			}
		}
	}
}

U32 linkGetSAddr(NetLink* link)
{
	if (!link)
		return 0;
	if(link->proxy_true_host)
		return link->proxy_true_host;
	return link->addr.sin_addr.s_addr;
}

char* linkGetIpStr(NetLink* link, char* buf, int buf_size)
{
	U32 ip = linkGetSAddr(link);
	sprintf_s(SAFESTR2(buf), "%d.%d.%d.%d", ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255);
	return buf;
}

char* linkGetIpPortStr(NetLink* link, char* buf, int buf_size)
{
	U32 ip = linkGetSAddr(link);
	U32 port = linkGetPort(link);
	sprintf_s(SAFESTR2(buf), "%d.%d.%d.%d:%d", ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255, port);
	return buf;
}

char* linkGetIpListenPortStr(NetLink* link, char* buf, int buf_size)
{
	U32 ip = linkGetSAddr(link);
	U32 port = linkGetListenPort(link);
	sprintf_s(SAFESTR2(buf), "%d.%d.%d.%d:%d", ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255, port);
	return buf;
}

void linkCompress(NetLink *link,int on)
{
	int		no_compress;

	// This function does nothing if the link is not compressed.
	if (!(link->flags & LINK_COMPRESS))
		return;

	no_compress = !on;
	if (no_compress != (int)link->no_compress)
		linkFlush(link);
	link->no_compress = !on;
}

const LinkStats* linkStats(const NetLink *link)
{
	return SAFE_MEMBER_ADDR(link, stats);
}

int linkIsServer(NetLink *link)
{
	if (link->listen->listen_sock != INVALID_SOCKET)
		return 1;
	return 0;
}

void linkFlushLimit(NetLink *link,int max_bytes)
{
	link->flush_limit = max_bytes;
}

U32 linkGetSizeFromType(LinkType eType)
{
	switch ((eType >> LINKTYPE_SIZE_SHIFT) << LINKTYPE_SIZE_SHIFT)
	{
	case LINKTYPE_SIZE_500K:
		return 500 * 1024;
	case LINKTYPE_SIZE_1MEG:
		return 1024 * 1024;
	case LINKTYPE_SIZE_2MEG:
		return 2 * 1024 * 1024;
	case LINKTYPE_SIZE_5MEG:
		return 5 * 1024 * 1024;
	case LINKTYPE_SIZE_10MEG:
		return 10 * 1024 * 1024;
	case LINKTYPE_SIZE_20MEG:
		return 20 * 1024 * 1024;
	case LINKTYPE_SIZE_100MEG:
		return 100 * 1024 * 1024;
	default:
		assertmsg(0, "Unknown link type size");
	}
}

void linkSetType(NetLink *pLink, LinkType eType)
{
	pLink->eType = eType;
	pLink->sent_sleep_alert = false;
	pLink->max_sendbuf = linkGetSizeFromType(eType);
	pLink->sentResizeAlert = 0;

	if (eType & LINKTYPE_FLAG_RESIZE_AND_WARN)
	{
		pLink->max_sendbuf *= (1 << LINK_RESIZES_WITH_WARNING_AFTER_MAX);
	}

	assertmsg(!(eType & LINKTYPE_FLAG_DISCONNECT_ON_FULL) ^ !(eType & LINKTYPE_FLAG_SLEEP_ON_FULL), "A valid link type must have one and only one of DISCONNECT_ON_FULL or SLEEP_ON_FULL");
}

void linkPushType(NetLink *pLink)
{
	pLink->ePushedType = pLink->eType;
}

void linkPopType(NetLink *pLink)
{
	linkSetType(pLink, pLink->ePushedType);
}

NetLink *linkCreate(SOCKET sock,NetListen *nl, LinkType eType, const char *file, int line)
{
	static U32 ID;
	NetLink	*link;
	NetComm	*comm = nl->comm;
	char temp[128];
	
	InterlockedIncrement(&netLinkCount);

	link = callocStruct(NetLink);
	
	link->sock = sock;
	link->listen = nl;
	link->stats.recv.last_time_ms = nl->comm->last_check_ms;
	link->max_recvbuf = 16384;
	link->max_sendbuf = 1000000;
	link->send_thread = comm->send_threads[(++comm->send_thread_idx) % eaSize(&comm->send_threads)];

	link->creationFile = file;
	link->creationLine = line;

	getFileNameNoDir(temp, file);
	sprintf(link->debugName, "%s(%d)", temp, line);

	linkAddToGlobalList(link);

	linkSetType(link, eType);


	return link;
}

int linkActivate(NetLink *link,int bytes_xferred)
{
	int err,len = sizeof(link->addr);
	int result;

	PERFINFO_AUTO_START_FUNC();
	err = getpeername(link->sock,(struct sockaddr *)&link->addr,&len);
	if (err)
		err = WSAGetLastError();
	if (sNetAllowCaptureRequestOnPort == (U16)link->listen->port && htons(link->addr.sin_port) == MAGIC_CAPTURE_CONNECT_PORT)
	{
		U32 allow_capture_port;
		allow_capture_port = InterlockedExchange(&sNetAllowCaptureRequestOnPort, 0);
		if (allow_capture_port == (U16)link->listen->port)
		{
			estrHeapCreate(&link->estr_send_capture_buf, sNetCaptureBufferSize, 0);
			printf("Capturing up to %u bytes on link %s (port %u)\n", sNetCaptureBufferSize, link->debugName, link->listen->port);
		}
	}
	link->recv_pak = pktCreateRawForReceiving(link);
	eaPush(&link->listen->links,link);
	link->flags = link->listen->required_flags;
	if(link->recv_pak){
		link->recv_pak->has_verify_data = !!(link->flags & LINK_PACKET_VERIFY);
	}
	result = linkAsyncReadRaw(link,bytes_xferred,link->listen->message_callback);
	PERFINFO_AUTO_STOP();
	return result;
}

LATELINK;
void netlink_cleanupXMLRPCUserData(void *data);
void DEFAULT_LATELINK_netlink_cleanupXMLRPCUserData(void *data)
{
	assertmsg(0, "Link flagged as LINK_REPURPOSED_XMLRPC in an executable not linked against HttpLib!");
}

void linkQueueRemove(NetLink *link, const char *disconnect_reason)
{
	NetComm	*comm = link->listen->comm;

	PERFINFO_AUTO_START_FUNC();
	
	if(!link->disconnect_reason){
		link->disconnect_reason = disconnect_reason;
	}
	linkStatus(link, __FUNCTION__);
	safeCloseLinkSocket(link, __FUNCTION__);
	if(TRUE_THEN_RESET(link->connected))
	{
		linkUserDisconnect(link);
	}
	eaPushUnique(&comm->remove_list,link);
	linkFlush(link);

	PERFINFO_AUTO_STOP();
}

int linkSendBufFull(NetLink *link)
{
	return link->sendbuf_full;
}

int linkSendBufWasFull(NetLink *link)
{
	return link->sendbuf_was_full;
}

void linkClearSendBufWasFull(NetLink *link)
{
	link->sendbuf_was_full = 0;
}

int linkConnectWaitNoRemove(NetLink **linkptr, F32 timeout)
{
	int timer;

	if(!SAFE_DEREF(linkptr))
	{
		return false;
	}

	if(gbFailAllCommConnects)
	{
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	timer = timerAlloc();
	while(!timeout || timerElapsed(timer) < timeout)
	{
		PERFINFO_AUTO_START("linkConnectWait loop", 1);
		commMonitorWithTimeout((*linkptr)->listen->comm, 1);
		if(!*linkptr || linkConnected(*linkptr) || linkDisconnected(*linkptr))
		{
			PERFINFO_AUTO_STOP();
			break;
		}
		PERFINFO_AUTO_STOP();
	}
	timerFree(timer);

	PERFINFO_AUTO_STOP();

	return linkConnected(*linkptr);
}

int linkConnectWait(NetLink **linkptr, F32 timeout)
{
	PERFINFO_AUTO_START_FUNC();

	if(linkConnectWaitNoRemove(linkptr, timeout))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	linkRemove(linkptr);
	PERFINFO_AUTO_STOP();
	return false;
}

int linkWaitForPacket(NetLink *link,PacketCallback *msgCb,F32 timeout)
{
	int				ret=0,timer = timerAlloc();
	U32				last_recv_bytes = link->stats.recv.bytes;
	PacketCallback	*origCb = link->listen->message_callback;

	if (msgCb)
		linkChangeCallback(link,msgCb);
	while(timerElapsed(timer) < timeout)
	{
		commMonitor(link->listen->comm);
		if (linkDisconnected(link))
			break;
		if (link->stats.recv.bytes != last_recv_bytes)
		{
			ret = 1;
			break;
		}
		Sleep(1);
	}
	timerFree(timer);
	if (msgCb)
		linkChangeCallback(link,origCb);
	return ret;
}

// for progress measurement of large packets
U32 linkCurrentPacketBytesWaitingToRecv(NetLink *link)
{
	return link->bytesWaitingToRecv;
}

int linkCompareIP(NetLink *left, NetLink *right)
{
#if _PS3
    return left->addr.sin_addr.s_addr == right->addr.sin_addr.s_addr;
#else
	return left->addr.sin_addr.S_un.S_addr == right->addr.sin_addr.S_un.S_addr;
#endif
}

U32 linkID(NetLink *link)
{
	return SAFE_MEMBER(link, ID);
}

void linkChangeCallback(NetLink *link,PacketCallback callback)
{
	NetListen	*listen;
	int			active_links=0,i;
	
	if(!link){
		return;
	}
	
	listen = link->listen;
	assert(!linkIsServer(link));
	for(i=0;i<eaSize(&link->listen->links);i++)
	{
		if (linkConnected(link->listen->links[i]))
			active_links++;
	}
	assert(active_links <= 1);
	listen->message_callback = callback;
}

void linkGrowRecvBuf(NetLink *link,int bytes_xferred)
{
	int		recv_size = link->curr_recvbuf,recv_size_size = sizeof(recv_size);

	if (!recv_size)
	{
		PERFINFO_AUTO_START_FUNC();
		linkVerbosePrintf(	link,
							"receive buffer before %d/%d",
							link->curr_recvbuf,
							link->max_recvbuf);
		getsockopt(link->sock,SOL_SOCKET,SO_RCVBUF,(char*)&recv_size,&recv_size_size);
		link->curr_recvbuf = recv_size;
		linkVerbosePrintf(	link,
							"receive buffer after  %d/%d",
							link->curr_recvbuf,
							link->max_recvbuf);
		PERFINFO_AUTO_STOP();
	}
	if (bytes_xferred > recv_size/2 && link->curr_recvbuf < link->max_recvbuf)
	{
		PERFINFO_AUTO_START_FUNC();
		recv_size *= 2;
		if (link->max_recvbuf && recv_size > link->max_recvbuf)
			recv_size = link->max_recvbuf;
		linkVerbosePrintf(	link,
							"receive buffer before %d/%d",
							link->curr_recvbuf,
							link->max_recvbuf);
		setsockopt(link->sock,SOL_SOCKET,SO_RCVBUF,(char*)&recv_size,sizeof(recv_size));
		getsockopt(link->sock,SOL_SOCKET,SO_RCVBUF,(char*)&recv_size,&recv_size_size);
		link->curr_recvbuf = recv_size;
		linkVerbosePrintf(	link,
							"receive buffer after  %d/%d",
							link->curr_recvbuf,
							link->max_recvbuf);
		//printf("recv buf: %d\n",link->curr_recvbuf);
		PERFINFO_AUTO_STOP();
	}
}

#if !_PS3
NetLink* linkCreateFakeLink(void)
{
	static U32 ID;
	NetLink	*link;

	link = callocStruct(NetLink);
	link->ID = 0xBEACBEAC;
	link->flags = LINK_FAKE_LINK;
	link->sock = INVALID_SOCKET;
	link->listen = NULL;
	link->send_thread = NULL;
	link->disconnected = 1;
	return link;	
}
#endif

int linkIsFake(NetLink* link)
{
	return !!(link->flags & LINK_FAKE_LINK);
}

void *linkGetUserData(NetLink *link)
{
	return SAFE_MEMBER(link, user_data);
}

int linkPendingSends(NetLink *link)
{
	return SAFE_MEMBER(link, outbox);
}

LinkToMultiplexer *linkGetLinkToMultiplexer(NetLink *link)
{
	return SAFE_MEMBER(link, link_to_multiplexer);
}

void linkSetLinkToMultiplexer(NetLink *link, LinkToMultiplexer *pLinkToMultiplexer)
{
	if(link){
		link->link_to_multiplexer = pLinkToMultiplexer;
	}
}

void *linkGetListenUserData(NetLink *link)
{
	return SAFE_MEMBER(link, listen->userData);
}

U32 linkGetListenIp(NetLink *link)
{
	return SAFE_MEMBER(link, listen->bound_ip);
}

U32 linkGetIp(NetLink* link)
{
#if _PS3
    return SAFE_MEMBER(link, addr.sin_addr.s_addr);
#else
	return SAFE_MEMBER(link, addr.sin_addr.S_un.S_addr);
#endif
}

bool linkIsLocalHost(NetLink *link)
{
	U32 uIP = linkGetIp(link);

	//stole this line from netipfilter.c
	if ((uIP == LOCALHOST_ADDR) || uIP == getHostLocalIp()) 
	{
		return true;
	}

	return false;

}

U32 linkGetPort(NetLink* link)
{
	if(!link)
		return 0;
	if(link->proxy_true_host)
		return link->proxy_true_port;
    return ntohs(link->addr.sin_port);
}

U32 linkGetListenPort(NetLink* link)
{
	return SAFE_MEMBER(link, listen->port);
}

uintptr_t linkGetSocket(NetLink* link)
{
	return SAFE_MEMBER(link, sock);
}

void linkAutoPing(NetLink *link,int on)
{
	if (link){
		link->auto_ping = on;
	}
}

int linkHasValidSocket(NetLink *link)
{
	return link && link->sock != INVALID_SOCKET;
}

int linkGetRawDataRemaining(NetLink *link)
{
	return SAFE_MEMBER(link, raw_data_left);
}

U32 linkGetPingAckReceiveCount(NetLink *link)
{
	return SAFE_MEMBER(link, ping_ack_recv_count);
}

void linkGetDisconnectReason(NetLink *link, char **ppOutEstring)
{
	U32 iErrorCode = linkGetDisconnectErrorCode(link);
	estrPrintf(ppOutEstring, "%s(%u:%s)", FIRST_IF_SET(link->disconnect_reason, "unknown"), iErrorCode, sockGetReadableError(iErrorCode));
}

U32 linkGetDisconnectErrorCode(NetLink *link)
{
	return SAFE_MEMBER(link, wsaRecvError);
}

void linkSetCorruptionFrequency(NetLink *link, int freq)
{
	if(link){
		link->deliberate_data_corruption_freq = freq;
	}
}

void linkSetPacketDisconnect(NetLink *link, int packets)
{
	if(link){
		link->deliberate_packet_disconnect = packets;
	}
}

void linkSetMaxAllowedPacket(NetLink *link,U32 size)
{
	if(link){
		link->pktsize_max = size;
	}
}

void linkSetMaxRecvSize(NetLink *link, U32 size)
{
	if(link){
		link->max_recvbuf = size;
	}
}

void linkSetIsNotTrustworthy(NetLink *link, bool bSet)
{
	if(link){
		link->not_trustworthy = bSet;

		// If the link is not trustworthy, turn off crazy debugging.
		if (bSet)
			link->flags &= ~LINK_CRAZY_DEBUGGING;
	}
}

S32 linkIsNotTrustworthy(NetLink *link)
{
	return SAFE_MEMBER(link, not_trustworthy);
}

void safeCloseLinkSocket(NetLink *link, const char* reason)
{
	//useful breakpoint for finding out why some link is dying
	/*
	if (!(link->flags & LINK_HTTP))
	{
		int iBrk = 0;
	}*/
	if (link)
	{
		linkSetDisconnectReason(link, reason);
		linkVerbosePrintf(link, "Closing socket: %s", reason);
		//printf("closing link %p, socket %d: %s\n", link, link->sock, reason);
		safeCloseSocket(&link->sock);
	}
}

void linkSetDebugName(NetLink *link, char *pName)
{
	strcpy_trunc(link->debugName, pName);
}

char* linkDebugName(NetLink *link)
{
	return link->debugName;
}


typedef struct CommConnectFSM
{
	CommConnectFSMType eFSMType;
	CommConnectFSMStatus eStatus;
	float fWaitTime;
	int iTimer;

	NetLink *pLink;

	NetComm *comm;
	LinkType eType;
	LinkFlags flags;
	const char *ip;
	int port;
	PacketCallback *packet_cb;
	LinkCallback *connect_cb;
	LinkCallback *disconnect_cb;
	int user_data_size;
	const char *file;
	int line;
	NetErrorReportingCB pConnectErrorCB;
	void *pConnectErrorUserData;
} CommConnectFSM;

CommConnectFSM *commConnectFSMEx(CommConnectFSMType eFSMType, float fWaitTime, 
	NetComm *comm, LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, NetErrorReportingCB pConnectErrorCB, void *pConnectErrorUserData, const char *file, int line)
{
	CommConnectFSM *pFSM = callocStruct(CommConnectFSM);
	char errorString[256];
	pFSM->eFSMType = eFSMType;
	pFSM->eStatus = COMMFSMSTATUS_STILL_TRYING;
	pFSM->fWaitTime = fWaitTime;
	pFSM->iTimer = timerAlloc();
	pFSM->comm = comm;
	pFSM->eType = eType;
	pFSM->flags = flags;
	pFSM->ip = ip;
	pFSM->port = port;
	pFSM->packet_cb = packet_cb;
	pFSM->connect_cb = connect_cb;
	pFSM->disconnect_cb = disconnect_cb;
	pFSM->user_data_size = user_data_size;
	pFSM->file = file;
	pFSM->line = line;
	pFSM->pConnectErrorCB = pConnectErrorCB;
	pFSM->pConnectErrorUserData = pConnectErrorUserData;

	pFSM->pLink = commConnectEx(comm, eType, flags, ip, port, packet_cb, connect_cb, disconnect_cb, user_data_size, SAFESTR(errorString), file, line);
	if (!pFSM->pLink && pFSM->pConnectErrorCB)
	{
		pFSM->pConnectErrorCB(pFSM->pConnectErrorUserData, errorString);
	}

	return pFSM;
}

CommConnectFSMStatus commConnectFSMUpdate(CommConnectFSM *pFSM, NetLink **ppSuccessfulOutLink)
{
	char errorString[256];

	if (pFSM->eStatus != COMMFSMSTATUS_STILL_TRYING)
	{
		return COMMFSMSTATUS_DONE;
	}

	if (linkConnected(pFSM->pLink) && !linkDisconnected(pFSM->pLink))
	{

		*ppSuccessfulOutLink = pFSM->pLink;
		pFSM->pLink = NULL;
		pFSM->eStatus = COMMFSMSTATUS_SUCCEEDED;
		return COMMFSMSTATUS_SUCCEEDED;
	}

	if (timerElapsed(pFSM->iTimer) < pFSM->fWaitTime && !linkDisconnected(pFSM->pLink))
	{
		return COMMFSMSTATUS_STILL_TRYING;
	}

	if (pFSM->eFSMType == COMMFSMTYPE_TRY_ONCE)
	{
		linkRemove_wReason(&pFSM->pLink, "Timed out or disconnected during commConnectFSMUpdate");
		timerFree(pFSM->iTimer);
		pFSM->iTimer = 0;
		pFSM->eStatus = COMMFSMSTATUS_FAILED;
		return COMMFSMSTATUS_FAILED;
	}


	linkRemove_wReason(&pFSM->pLink, "Timed out or disconnected during commConnectFSMUpdate - going to retry");
	pFSM->pLink = commConnectEx(pFSM->comm, pFSM->eType, pFSM->flags, pFSM->ip, pFSM->port, pFSM->packet_cb, pFSM->connect_cb, pFSM->disconnect_cb, pFSM->user_data_size, SAFESTR(errorString),
		pFSM->file, pFSM->line);
	if (!pFSM->pLink && pFSM->pConnectErrorCB)
	{
		pFSM->pConnectErrorCB(pFSM->pConnectErrorUserData, errorString);
	}
	timerStart(pFSM->iTimer);

	return COMMFSMSTATUS_STILL_TRYING;

}

void commConnectFSMDestroy(CommConnectFSM **ppFSM)
{
	CommConnectFSM *pFSM;

	if (!ppFSM || !(*ppFSM))
	{
		return;
	}
	

	

	pFSM = *ppFSM;

	if (pFSM->pLink)
	{
		linkRemove_wReason(&pFSM->pLink, "commConnectFSMDestroy");
	}

	if (pFSM->iTimer)
	{
		timerFree(pFSM->iTimer);
	}

	free(pFSM);
	*ppFSM = NULL;
}



LinkGetReceivedTableResult LinkGetReceivedParseTable(NetLink *pLink, ParseTable *pLocalTPI, ParseTable ***pppSentTPIList)
{
	int i;

	for (i=0; i < eaSize(&pLink->ppReceivedTPIs); i++)
	{
		if (pLink->ppReceivedTPIs[i]->pLocalTPI == pLocalTPI)
		{
			if (pLink->ppReceivedTPIs[i]->ppReceivedTPIs == NULL)
			{
				return LOCAL_TABLE_IDENTICAL;
			}
			else
			{
				*pppSentTPIList = pLink->ppReceivedTPIs[i]->ppReceivedTPIs;
				return TABLE_RECEIVED_AND_DIFFERENT;
			}
		}
	}

	return TABLE_NOT_YET_RECEIVED;
}

void LinkSetReceivedParseTable(NetLink *pLink, ParseTable *pLocalTPI, bool bTableWasIdentical, ParseTable ***pppSentTPIList)
{
	int i;
	ReceivedTPICacheEntry *pEntry;


	for (i = 0; i < eaSize(&pLink->ppReceivedTPIs); i++)
	{
		if (pLink->ppReceivedTPIs[i]->pLocalTPI == pLocalTPI)
		{
			if (!bTableWasIdentical)
			{
				ParseTableFree(pppSentTPIList);
			}
			return;
		}
	}

	pEntry = callocStruct(ReceivedTPICacheEntry);
	pEntry->pLocalTPI = pLocalTPI;
	if (!bTableWasIdentical)
	{
		pEntry->ppReceivedTPIs = *pppSentTPIList;
	}

	eaPush(&pLink->ppReceivedTPIs, pEntry);

}

bool LinkParseTableAlreadySent(NetLink *pLink, ParseTable *pLocalTPI)
{
	if (eaFind(&pLink->ppSentTPIs, pLocalTPI) != -1)
	{
		return true;
	}
	
	return false;
}

void LinkSetParseTableSent(NetLink *pLink, ParseTable *pLocalTPI)
{
	eaPush(&pLink->ppSentTPIs, pLocalTPI);
}

void linkCancelRead(NetLink* link)
{
	#if _IOCP
	if(SAFE_MEMBER2(link, recv_pak, ol_queued)){
		CancelIo((HANDLE)link->sock);
	}
	#endif
}

void linkUserConnect(NetLink* link)
{
	NetListen *listen = link->listen;

	if (listen->user_data_size && link->user_data == NULL && !link->lost_user_data_ownership)
	{
		link->user_data = calloc(listen->user_data_size,1);
	}
	
	if (listen->connect_callback)
	{
		PERFINFO_AUTO_START("connect_cb", 1);
		listen->connect_callback(link,link->user_data);
		PERFINFO_AUTO_STOP();
	}
	if (!link->user_disconnect)
		link->notify_on_disconnect = 1;
}

void linkUserDisconnect(NetLink* link)
{

	// This is set here because link->disconnected is not set until later, but it's helpful to know that the link is effectively
	// disconnected at this point.  Specifically, the user disconnect callback may try to perform operations on the link by calling
	// back into net code.
	link->user_disconnect = true;

	if (g_isContinuousBuilder)
	{
		char *pDisconnectReason = NULL;
		linkGetDisconnectReason(link, &pDisconnectReason);
		printf("NetLink %s disconnected: %s\n", 
			linkDebugName(link), pDisconnectReason);
		estrDestroy(&pDisconnectReason);
	}


	PERFINFO_AUTO_START("LinkFileSendingMode disconnect", 1);
	if (link->pLinkFileSendingMode_ReceiveManager)
	{
		linkFileSendingMode_Receive_Disconnect(link);
	}

	if (link->pLinkFileSendingMode_SendManager)
	{
		linkFileSendingMode_Send_Disconnect(link);
	}
	PERFINFO_AUTO_STOP();


	if(link->flags & LINK_REPURPOSED_XMLRPC)
	{
		PERFINFO_AUTO_START("netlink_cleanupXMLRPCUserData", 1);
		netlink_cleanupXMLRPCUserData(link->user_data);
		PERFINFO_AUTO_STOP();
	}
	else if(link->listen->disconnect_callback &&
			link->notify_on_disconnect)
	{

		
		PERFINFO_AUTO_START("disconnect_cb", 1);
        link->listen->disconnect_callback(link,link->user_data);
        link->notify_on_disconnect = false;
        PERFINFO_AUTO_STOP();
	}

	if (!link->lost_user_data_ownership){
		SAFE_FREE(link->user_data);
	}
}

bool commConnectFSMForTickFunctionWithRetryingEx(CommConnectFSM **ppFSM, NetLink **ppLink, char *pDebugNameToSet, float fWaitTime, 
	NetComm *comm, LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback packet_cb,
	LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, NetErrorReportingCB pErrorCB, void *pErrorCBUserData, 
	NetErrorReportingCB pDisconnectionCB, void *pDisconnectionCBUserData, const char *file, int line)
{
	if (!(*ppLink) || linkDisconnected(*ppLink))
	{
		if (*ppLink && pDisconnectionCB)
		{
			char *pTempString = NULL;
			linkGetDisconnectReason(*ppLink, &pTempString);
			pDisconnectionCB(pDisconnectionCBUserData, pTempString);
			estrDestroy(&pTempString);
		}

		linkRemove(ppLink);

		if (!(*ppFSM))
		{
			(*ppFSM) = commConnectFSMEx(COMMFSMTYPE_RETRY_FOREVER, fWaitTime, comm,
				eType, flags, ip, port, packet_cb,
				connect_cb, disconnect_cb, user_data_size, pErrorCB, pErrorCBUserData, file, line);
			return false;
		}

		if (commConnectFSMUpdate((*ppFSM), ppLink) == COMMFSMSTATUS_SUCCEEDED)
		{
			commConnectFSMDestroy(ppFSM);
			linkSetDebugName((*ppLink), pDebugNameToSet);

			return true;
		}


		return false;
	}
	else if ((*ppFSM))
	{
		//weird case where we were trying to connect via FSM, but someone else already connected in some other way... no harm, no foul, just 
		//get rid of the FSM
		commConnectFSMDestroy(ppFSM);
	}

	return true;
}

void linkSetSleepCB(NetLink *link, LinkSleepCallBack cb)
{
	link->sleep_cb = cb;
}

void linkSetResizeCB(NetLink *link, LinkResizeCallBack cb)
{
	link->resize_cb = cb;
}

#include "net_h_ast.h"
#include "net_h_ast.c"
#include "NetPrivate_h_ast.c"

