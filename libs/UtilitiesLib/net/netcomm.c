#include "../../3rdparty/zlib/zlib.h"
#include "sock.h"
#include "net.h"
#include "netprivate.h"
#include "netiocp.h"
#include "earray.h"
#include "timing.h"
#include "EventTimingLog.h"
#include "netreceive.h"
#include "netsendthread.h"
#include "nethandshake.h"
#include "endian.h"
#include "WorkerThread.h"
#include "sysutil.h"
#include "netlink.h"
#include "file.h"
#include "CrypticPorts.h"

#define DEV_MODE_DEFAULT_TIMEOUT 1 // 100 // TODO: REDUCE_DEV_MODE_CPU

bool gbFailAllCommConnects = false;
AUTO_CMD_INT(gbFailAllCommConnects, FailAllCommConnects) ACMD_COMMANDLINE;

// Set to a port number to capture on once.
static U32 sNetRequestPeerCaptureOnPort = 0;
AUTO_CMD_INT(sNetRequestPeerCaptureOnPort, NetRequestPeerCaptureOnPort) ACMD_CATEGORY(Debug);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

static int g_comm_default_flags =	LINK_COMPRESS |
									LINK_PACKET_VERIFY
									;
static int g_net_lag_set,g_net_lag_delay,g_net_lag_vary;

static int g_net_xlsp;

int g_force_sockbsd;

//a list of all comms, used for flushing all comms at shutdown time
static NetComm **gppAllComms = NULL;

// Mutex for the above.
static CRITICAL_SECTION gppAllCommsMutex;

// This is set to true by commFlushAndCloseAllComms()
static bool sbCommFlushAndCloseAllCommsCalled = false;

AUTO_COMMAND ACMD_CMDLINE;
void commDefaultTimeout(U32 milliseconds){
	commSetMinReceiveTimeoutMS(commDefault(), milliseconds);
}

AUTO_COMMAND;
void commDefaultVerify(S32 enabled){
	if(enabled){
		g_comm_default_flags |= LINK_PACKET_VERIFY;
	}else{
		g_comm_default_flags &= ~LINK_PACKET_VERIFY;
	}
}

NetComm *commDefault(void)
{
	static NetComm	*comm;

	ATOMIC_INIT_BEGIN;
	U32 msTimeout = isDevelopmentMode() ? DEV_MODE_DEFAULT_TIMEOUT : 1;
	comm = commCreate(msTimeout,1);
	ATOMIC_INIT_END;

	return comm;
}

// Initialize gppAllCommsMutex if needed.
static void commInitAllCommsMutex()
{
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&gppAllCommsMutex);
	ATOMIC_INIT_END;
}

NetComm *commCreate(int timeout_msecs,int num_send_threads)
{
	NetComm	*comm;

	PERFINFO_AUTO_START_FUNC();

	// Create new comm.
	comm = callocStruct(NetComm);
	comm->createdInThreadID = GetCurrentThreadId();

	// Add comm to comm list.
	commInitAllCommsMutex();
	EnterCriticalSection(&gppAllCommsMutex);
	eaPush(&gppAllComms, comm);
	LeaveCriticalSection(&gppAllCommsMutex);

	if (isProductionMode())
		g_comm_default_flags &= ~LINK_PACKET_VERIFY;
	if (!num_send_threads)
	{
		comm->no_send_thread = 1;
		num_send_threads = 1;
	}
	sockStart();
#ifdef _IOCP
	if (!g_force_sockbsd)
		comm->completionPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 1);
#endif
	comm->timeout_msecs = timeout_msecs;
	comm->send_timeout = DEFAULT_SEND_TIMEOUT;
	commInitSendThread(comm,num_send_threads);
	
	PERFINFO_AUTO_STOP();

	return comm;
}

void commDestroy(NetComm ** comm)
{
	//TODO: Maybe this should try to destroy the comm
	if(comm)
	{
		if (*comm)
		{
			commFlushDisconnects((*comm));
			FOR_EACH_IN_EARRAY((*comm)->send_threads, NetCommSendThread, st)
			{
				wtDestroy(st->wt);
				st->wt = NULL;
				SAFE_FREE(st->send_buffer);
				SAFE_FREE(st);
			}
			FOR_EACH_END;
			eaDestroy(&(*comm)->send_threads);
		}
		*comm = NULL;
	}
}

static void commCheckTimeouts(NetComm *comm)
{
	int			i,j;
	NetListen	*listen;
	NetLink		*link;

	PERFINFO_AUTO_START_FUNC();

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		listen = comm->listen_list[i];
		for(j=0;j<eaSize(&listen->links);j++)
		{
			link = listen->links[j];
			if (link->connected && link->timeout && linkRecvTimeElapsed(link) > link->timeout)
				linkQueueRemove(link, "commCheckTimeouts timeout exceeded");
		}
	}
	
	PERFINFO_AUTO_STOP();
}

LATELINK;
void netcomm_verifyXMLRPCIsLinkedIn(void);
void DEFAULT_LATELINK_netcomm_verifyXMLRPCIsLinkedIn(void)
{
	assertmsg(0, "Link flagged as LINK_ALLOW_XMLRPC in an executable not linked against HttpLib!");
}

static NetListen *commListenInternalEx(NetComm *comm,LinkType eType, LinkFlags required,int port,PacketCallback packet_cb,LinkCallback connect_cb,
									LinkCallback disconnect_cb,int user_data_size,U32 ip,int socket_num, const char *file, int line)
{
	NetListen *nl;
	
	PERFINFO_AUTO_START_FUNC();
	
	nl = callocStruct(NetListen);

	required |= g_comm_default_flags;
	if (required & LINK_NO_COMPRESS)
	{
		nl->unallowed_flags = LINK_COMPRESS;
		required &= ~LINK_COMPRESS;
	}
	if (required & LINK_HTTP)
		required |= LINK_RAW;
	if (required & LINK_ALLOW_XMLRPC)
		netcomm_verifyXMLRPCIsLinkedIn();
	if (!(required & LINK_PACKET_VERIFY))
		nl->unallowed_flags = LINK_PACKET_VERIFY;
	nl->comm				= comm;
	nl->connect_callback	= connect_cb;
	nl->disconnect_callback = disconnect_cb;
	nl->message_callback	= packet_cb;
	nl->port				= port;
	nl->listen_sock			= socket_num;
	nl->required_flags		= required;
	nl->user_data_size		= user_data_size;
	nl->bound_ip			= ip;
	nl->creationFile		= file;
	nl->creationLine		= line;
	nl->eLinkType			= eType;

	closeSockOnAssert(nl->listen_sock);
	eaPush(&comm->listen_list,nl);
#ifdef _IOCP
	netListenStartIocpAccept(nl);
#endif
	printf("binding listen[%08p] to ip: %s, port %d\n",listen,makeIpStr(ip),port);

	PERFINFO_AUTO_STOP();

	return nl;
}

static int sockListen(NetComm* comm,SOCKET* sock_num,struct sockaddr_in *addr_in,U32 ip,int port, int listen_flags)
{
	int listen_backlog_size = SOMAXCONN;
	PERFINFO_AUTO_START_FUNC();
	*sock_num = socketCreate(AF_INET,SOCK_STREAM,0);
	commVerbosePrintf(comm, "Created listen socket %d", *sock_num);
	sockSetAddr(addr_in,ip,port);
	if (!sockBind(*sock_num,addr_in))
		goto fail;
	sockSetBlocking(*sock_num,0);
	if(listen_flags & LINK_SMALL_LISTEN)
		listen_backlog_size = 2;
	else if(listen_flags & LINK_MEDIUM_LISTEN)
		listen_backlog_size = 20;
	if (listen(*sock_num, listen_backlog_size) != 0)
		goto fail;
	PERFINFO_AUTO_STOP();
	return 1;
fail:
	closesocket(*sock_num);
	PERFINFO_AUTO_STOP();
	return 0;
}

NetListen *commListenIpEx(NetComm *comm, LinkType eType, LinkFlags required,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size,U32 ip, const char *file, int line)
{
	struct sockaddr_in	addr_in;
	SOCKET				socket_num;

	if (!sockListen(comm,&socket_num,&addr_in,ip,port,required))
		return 0;
	
	return commListenInternalEx(comm,eType, required,port,packet_cb,connect_cb,disconnect_cb,user_data_size,ip,socket_num, file, line);
}

NetListen *commListenEx(NetComm *comm,LinkType eType, LinkFlags required,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, const char *file, int line)
{
	return commListenIpEx(comm, eType, required,port,packet_cb,connect_cb,disconnect_cb,user_data_size,INADDR_ANY, file, line);
}

int commListenBothEx(NetComm *comm,LinkType eType, LinkFlags required,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size,NetListen **local_p,NetListen **public_p, const char *file, int line)
{
	NetListen			*local_listen,*public_listen=0;
	struct sockaddr_in	local_addr,public_addr;
	SOCKET				local_socknum,public_socknum;

	if (!sockListen(comm,&local_socknum,&local_addr,getHostLocalIp(),port,required))
		return 0;
	if (getHostLocalIp() != getHostPublicIp())
	{
		if (!sockListen(comm,&public_socknum,&public_addr,getHostPublicIp(),port,required))
		{
			closesocket(local_socknum);
			return 0;
		}
		public_listen = commListenInternalEx(comm, eType, required,port,packet_cb,connect_cb,disconnect_cb,user_data_size,getHostPublicIp(),public_socknum, file, line);

	}
	local_listen = commListenInternalEx(comm,eType, required,port,packet_cb,connect_cb,disconnect_cb,user_data_size,getHostLocalIp(),local_socknum, file, line);
	if (local_p)
		*local_p = local_listen;
	if (public_p)
		*public_p = public_listen;
	return 1;
}

NetLink *commConnectEx(NetComm *comm,LinkType eType, LinkFlags flags,const char *ip_str,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, char *error, size_t error_size, const char *file, int line)
{
	U32 ip;
	NetLink* link;
	int error_code;
	
	PERFINFO_AUTO_START_FUNC();
	
	ip = ipFromStringWithError(ip_str, &error_code);
	if (!ip)
	{
		if (error && error_size)
			sprintf_s(SAFESTR2(error), "Name error, code %d", error_code);
		return NULL;
	}

    link = commConnectIPEx(comm, eType, flags, ip, port,packet_cb,connect_cb,disconnect_cb,user_data_size, error, error_size, file, line);
    
    PERFINFO_AUTO_STOP();
    
    return link;
}

NetLink *commConnectIPEx(NetComm *comm,LinkType eType, LinkFlags flags,U32 ip,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, char *error, size_t error_size, const char *file, int line)
{
	NetLink	*link;
	SOCKET		sock;
	int			ret;
	NetListen	*nl=0;
	U32			peer_capture_port;

	if (!ip)
	{
		return 0;
	}


	
	PERFINFO_AUTO_START_FUNC();

	sock = socketCreate(AF_INET,SOCK_STREAM,0);

	commVerbosePrintf(comm, "Created connect socket %d", sock);

#if _PS3
	if((int)sock < 0)
	{
		printf("socket failed\n");
		PERFINFO_AUTO_STOP();
		return 0;
	}
#else
	if(sock == INVALID_SOCKET)
	{
		U32 error_code = WSAGetLastError();
		printf("socket failed: error %d.\n", error_code);
		if (error && error_size)
			sprintf_s(SAFESTR2(error), "socket failed: error %d.\n", error_code);
		PERFINFO_AUTO_STOP();
		return 0;
	}
#endif

    flags |= g_comm_default_flags | LINK_CRC;
	if (flags & LINK_NO_COMPRESS)
		flags &= ~LINK_COMPRESS;
	if (flags & LINK_HTTP)
		flags |= LINK_RAW;
	nl = callocStruct(NetListen);
	eaPush(&comm->listen_list,nl);
	nl->creationFile		= file;
	nl->creationLine		= line;
	nl->eLinkType			= eType;
	nl->connect_callback	= connect_cb;
	nl->disconnect_callback = disconnect_cb;
	nl->message_callback	= packet_cb;
	nl->port				= port;
	nl->comm				= comm;
	nl->user_data_size		= user_data_size;
	nl->listen_sock			= INVALID_SOCKET;

	link = linkCreate(sock, nl, eType, file, line);
	linkStatus(link,"socket create");
	link->flags			= flags;
	link->protocol		= LINK_PROTOCOL_VER;
	if(comm->proxy_host && ip != LOOPBACK_IP)
	{
		sockSetAddr(&link->addr,comm->proxy_host,comm->proxy_port);
		link->proxy_true_host = ip;
		link->proxy_true_port = port;
		link->flags |= LINK_WAITING_FOR_SOCKS;
	}
	else
		sockSetAddr(&link->addr,ip,port);
	sockSetBlocking(sock,0);
	sockSetDelay(sock,0);

	// If configured as such, request that the peer capture this connection.
	if (sNetRequestPeerCaptureOnPort == (U16)port)
	{
		peer_capture_port = InterlockedExchange(&sNetRequestPeerCaptureOnPort, 0);
		if (peer_capture_port == (U16)port)
		{
			struct sockaddr_in local_addr;
			sockSetAddr(&local_addr, htonl(INADDR_ANY), MAGIC_CAPTURE_CONNECT_PORT);
			if (!sockBind(sock, &local_addr))
			{
				U32 error_code = WSAGetLastError();
				printf("bind failed: error %d.\n", error_code);
			}
		}
	}

#ifdef _IOCP
	if (!g_force_sockbsd)
	{
		HANDLE		command_valid;

		command_valid = CreateIoCompletionPort((HANDLE)link->sock, comm->completionPort, (ULONG_PTR)0, 1);
		assert(command_valid == link->listen->comm->completionPort);
	}
#endif


	if (gbFailAllCommConnects)
	{
		PERFINFO_AUTO_STOP();
		return link;	
	}

	ret = connect(sock,(struct sockaddr *)&link->addr,sizeof(link->addr));
	
	#if _PS3
		if(ret < 0 && ret != SYS_NET_ERROR_EINPROGRESS)
		{
			linkRemove_wReason(&link, "connect failed");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	#else
		if(ret == SOCKET_ERROR)
		{
			U32 error_code = WSAGetLastError();
			
			if(isDisconnectError(error_code)){
				char buffer[100];
				printf(	"connect to ip %s got error %d, cancelling.\n",
						GetIpStr(ip, SAFESTR(buffer)),
						error_code);
				if (error && error_size)
					sprintf_s(SAFESTR2(error), "connect to ip %s got error %d, cancelling.\n",
							GetIpStr(ip, SAFESTR(buffer)),
							error_code);
				linkFree(link);
				PERFINFO_AUTO_STOP();
				return 0;
			}
		}
	#endif

	eaPush(&nl->links,link);
	PERFINFO_AUTO_STOP();
	return link;
}

NetLink *commConnectWaitEx(NetComm *comm,LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size,F32 timeout, const char *file, int line)
{
	NetLink	*link;

	link = commConnectEx(comm, eType, flags,ip,port,packet_cb,connect_cb,disconnect_cb,user_data_size, NULL, 0, file, line);
	linkConnectWait(&link, timeout);
	return link;
}

// Basically what this function does is spam the socket with send() and connect() until it says it is connected.
//
// ... In case you weren't sure, no, this is not the right way to do this.
//
// The biggest practical problem with this is that we don't have any way to tell if the connection attempt fails; even if the connection attempt fails
// for some reasonable reason, such as host unreachable or connection refused, it doesn't tell us, and we just go on spamming it until the end of time,
// or at least until the user closes and uninstalls the software and demands a refund from CS.
//
// TODO: The technical part of fixing it is easy.  The hard part is the minor code restructure required, although I suspect it's not as bad as it seems.
// Here's what needs to be done:
// In IOCP mode: ConnectEx() should replace connect(), and we could get a completion like a normal person.  This is actually probably pretty easy.
// In BSD mode: This is harder.  Ideally, we'd use select().  The problem is that the BSD code doesn't use select() and relies entirely on polling.
//   So I think polling recv() might be the way to go in that case.
static void checkLinkStatus(NetLink *link)
{
	int isRaw = link->flags & LINK_RAW;
	bool connected = false;

	if (link->error)
	{
		printf("%s\n",link->error);
	}
	if (link->stats.send.real_packets)
		return;
	if (!isRaw && !(link->flags & LINK_WAITING_FOR_SOCKS) && netSendConnectPkt(link))
	{
		connected = true;
	}
	else if(link->sock == INVALID_SOCKET)
	{
		linkQueueRemove(link, "socket became invalid sending connection packet");
	}
	else
	{
		int		ret,error;

		ret = connect(link->sock,(struct sockaddr *)&link->addr,sizeof(link->addr));
		if (ret)
		{
			error = WSAGetLastError();

			if(isRaw && error == WSAEISCONN)
			{
				link->connected = 1;
				connected = true;

				if( !(link->flags & LINK_HTTP) &&
					!link->cleared_user_link_ptr)
				{
					linkUserConnect(link);
				}
			}

			if(link->flags & LINK_WAITING_FOR_SOCKS && !(link->flags & LINK_SENT_SOCKS_REQ) && error == WSAEISCONN)
			{
				Packet *socks_req = pktCreateRawSize(link, 9);
				char buf[9];
				U16 port = endianSwapIfNotBig(U16, link->proxy_true_port);
				U32 ip = endianSwapIfNotBig(U32, link->proxy_true_host);
				buf[0] = 0x04; // SOCKS version: v4
				buf[1] = 0x01; // command code: stream connection
				buf[2] = port & 0x00FF; // Port low byte
				buf[3] = port >> 8; // Port high byte
				buf[4] = ip >> 24; // IP first byte
				buf[5] = (ip & 0x00FF0000) >> 16; // IP second byte
				buf[6] = (ip & 0x0000FF00) >> 8; // IP third byte
				buf[7] = ip & 0x000000FF; // IP forth byte
				buf[8] = 0; // Null terminated name
				pktSendBytesRaw(socks_req, buf, 9);
				if(pktSendRaw(&socks_req))
				{
					link->stats.send.packets--;
					link->stats.send.real_packets--;
					link->flags |= LINK_SENT_SOCKS_REQ;
				}
				printf("Requesting SOCKS4 connection\n");
			}
		}
	}
	if (connected || link->flags & LINK_SENT_SOCKS_REQ)
	{
		link->recv_pak = pktCreateRaw(link);
		if (!linkAsyncReadRaw(link,0,link->listen->message_callback))
		{
			linkQueueRemove(link, "linkAsyncRaw failed in checkLinkStatus"); // goes straight from not connected to disconnected
		}
	}
}

static void commConnectMonitor(NetComm *comm)
{
	int			i,j;
	NetListen	*listen;
	
	PERFINFO_AUTO_START_FUNC();

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		listen = comm->listen_list[i];

		if (listen->listen_sock == INVALID_SOCKET)
		{
			for(j=0;j<eaSize(&listen->links);j++)
			{
				if (!listen->links[j]->connected)
					checkLinkStatus(listen->links[j]);
			}
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void commSendLaggedPackets(NetComm *comm)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(comm->listen_list, i, isize);
	{
		NetListen* nl = comm->listen_list[i];

		if(g_net_lag_set){
			EARRAY_CONST_FOREACH_BEGIN(nl->links, j, jsize);
			{
				NetLink	*	link = nl->links[j];
				Packet*		pak;

				if(	link->lag == g_net_lag_delay &&
					link->lag_vary == g_net_lag_vary
					||
					!linkConnected(link))
				{
					continue;
				}

				pak = pktCreate(link,PACKETCMD_LAG);

				link->lag = g_net_lag_delay;
				link->lag_vary = g_net_lag_vary;
				pktSendU32(pak,link->lag);
				pktSendU32(pak,link->lag_vary);
				pktSend(&pak);
			}
			EARRAY_FOREACH_END;
		}

		EARRAY_FOREACH_REVERSE_BEGIN(nl->linksWithLaggedPackets, j);
		{
			NetLink* link = nl->linksWithLaggedPackets[j];

			linkSendLaggedPackets(link, !link->lag);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

void commSendKeepAlivePackets(NetComm *comm)
{
	int			i,j;

	PERFINFO_AUTO_START_FUNC();

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		NetListen* nl = comm->listen_list[i];
		for(j=0;j<eaSize(&nl->links);j++)
		{
			NetLink	*link = nl->links[j];
			if(	link->keep_alive_interval_seconds &&
				link->connected &&
				!link->disconnected &&
				(U32)(timeGetTime() - link->keep_alive_prev_milliseconds) / 1000 >= link->keep_alive_interval_seconds)
			{					
				linkSendKeepAlive(link);
				linkFlush(link);
				link->keep_alive_prev_milliseconds = timeGetTime();
			}
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void commMonitorWithTimeout_dbg(NetComm *comm, S32 timeoutOverrideMilliseconds MEM_DBG_PARMS)
{
	PerfInfoGuard* piGuard;
	U32 curTime;
	
	if(!comm){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC_PIX_GUARD(&piGuard);
	curTime = timeGetTime();
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);
	assertmsgf(!comm->monitoring_file, "Already monitoring from %s:%d (%u), now monitoring from %s:%d(%u)", comm->monitoring_file, comm->monitoring_line, comm->monitoring_threadid, caller_fname, line, GetCurrentThreadId());
	comm->monitoring_file = caller_fname;
	comm->monitoring_line = line;
	comm->monitoring_threadid = GetCurrentThreadId();
	commSendLaggedPackets(comm);
	commConnectMonitor(comm);
	commProcessPackets(comm, timeoutOverrideMilliseconds);
	if(curTime - comm->msPeriodicStartTime >= 1000)
	{
		comm->msPeriodicStartTime = curTime;
		commCheckTimeouts(comm);
		commSendKeepAlivePackets(comm);
	}
	comm->monitoring_file = NULL;
	if(	comm->last_force_frame_ms &&
		curTime - comm->last_force_frame_ms >= 1000)
	{
		commForceThreadFrame(comm);
		comm->last_force_frame_ms = FIRST_IF_SET(curTime, -1);
	}
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP_PIX_GUARD(&piGuard);
}

void commMonitor_dbg(NetComm *comm MEM_DBG_PARMS)
{
	commMonitorWithTimeout_dbg(comm, -1 MEM_DBG_PARMS_CALL);
}

bool commIsMonitoring(NetComm *comm)
{
	return !!comm->monitoring_file;
}

void commFlushAllLinks(NetComm *comm)
{
	int			i,j;
	NetListen	*listen;

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		listen = comm->listen_list[i];

		for(j=0;j<eaSize(&listen->links);j++)
			linkFlush(listen->links[j]);
	}
}

void commFlushAndCloseAllLinks(NetComm *comm)
{
	int			i,j;
	NetListen	*listen;

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		listen = comm->listen_list[i];

		for(j=0;j<eaSize(&listen->links);j++)
		{
			if(listen->links[j] && !listen->links[j]->cleared_user_link_ptr)
			{
				// This avoids NULLing out all entries in a NetComm's earray.
				// Without this temp variable, you would never be able to 
				// call commMonitor() again on any NetComm that was affected
				// by the function. 
				NetLink *pTemp = listen->links[j];

				linkFlushAndClose(&pTemp, "Closing all Links");
			}
		}
	}
}

int commCountOpenSocks(NetComm *comm)
{
	int iCount = 0;
	int			i,j;
	NetListen	*listen;

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		listen = comm->listen_list[i];

		for(j=0;j<eaSize(&listen->links);j++)
		{
			if (listen->links[j] && linkHasValidSocket(listen->links[j]))
			{
				iCount++;
			}
		}
	}

	return iCount;
}



void commFlushAndCloseAllComms(float fTimeout)
{
	int i;
	U32 startingTime;
	int iSockCount;

	PERFINFO_AUTO_START_FUNC();

	// Note that this function has been called.
	sbCommFlushAndCloseAllCommsCalled = true;

	commInitAllCommsMutex();
	EnterCriticalSection(&gppAllCommsMutex);
	for (i=0; i < eaSize(&gppAllComms); i++)
	{
		if(gppAllComms[i]->createdInThreadID == GetCurrentThreadId())
		{
			commFlushAndCloseAllLinks(gppAllComms[i]);
		}
	}
	LeaveCriticalSection(&gppAllCommsMutex);

	startingTime = timeGetTime();

	do
	{
		iSockCount = 0;

		Sleep(1);

		EnterCriticalSection(&gppAllCommsMutex);
		for (i=0; i < eaSize(&gppAllComms); i++)
		{
			if(gppAllComms[i]->createdInThreadID == GetCurrentThreadId())
			{
				if (commCountOpenSocks(gppAllComms[i]))
				{
					// FIXME: if monitoring_file, this won't work, and we'll be stuck in this function until fTimeout
					// The workaround is to not initiate shutdown from inside a packet handler.
					if (!gppAllComms[i]->monitoring_file)
						commMonitorWithTimeout(gppAllComms[i], 0);
					iSockCount += commCountOpenSocks(gppAllComms[i]);
				}
			}
		}
		LeaveCriticalSection(&gppAllCommsMutex);
	}
	while (iSockCount > 0 && (timeGetTime() - startingTime) < (U32)(fTimeout*1000));

	PERFINFO_AUTO_STOP();
}

// Return true if commFlushAndCloseAllComms() has been called.
bool commFlushAndCloseAllCommsCalled()
{
	return sbCommFlushAndCloseAllCommsCalled;
}

int listenCount(NetListen *listen)
{
	return eaSize(&listen->links);
}

void *listenGetUserData(NetListen *listen)
{
	return listen->userData;
}

void listenSetUserData(NetListen *listen, void *userData)
{
	listen->userData = userData;
}

// Set the required flags for future NetLinks created by this NetListen.
void listenSetRequiredFlags(NetListen *listen, LinkFlags flags)
{
	listen->required_flags = flags;
}

AUTO_COMMAND ACMD_NAME(netNoVerify) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void commSetNoVerify(int no_packet_verify)
{
	if (no_packet_verify)
		g_comm_default_flags &= ~LINK_PACKET_VERIFY;
	else
		g_comm_default_flags |= LINK_PACKET_VERIFY;
}

AUTO_COMMAND ACMD_NAME(netNoCompress) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void commSetNoCompress(int no_compress)
{
	if (no_compress)
		g_comm_default_flags |= LINK_NO_COMPRESS;
	else
		g_comm_default_flags &= ~LINK_NO_COMPRESS;
}

AUTO_COMMAND ACMD_NAME(NetLag) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void commSetLag(int msecs)
{
	g_net_lag_delay = msecs;
	g_net_lag_set = 1;
}

AUTO_CMD_INT(g_net_lag_vary, NetLagVary) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

AUTO_COMMAND ACMD_NAME(NetXLSP) ACMD_CMDLINE;
void netSetXLSP(int on)
{
	g_net_xlsp = g_force_sockbsd = on;
}

bool sockbsd_set_on_command_line=false;
AUTO_COMMAND ACMD_NAME(NetSockBsd) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void netSetSockBsd(int on)
{
	g_force_sockbsd = on;
	sockbsd_set_on_command_line = true;
}

AUTO_RUN_EARLY;
void initWineSockets(void)
{
	if (!sockbsd_set_on_command_line && getWineVersion())
	{
		g_force_sockbsd = true;
	}
}

void commTimedDisconnect(NetComm *comm,F32 seconds)
{
	NetCommDebug	*debug = &comm->debug;

	debug->test_disconnect = 1;
	debug->disconnect_seconds = seconds;
}

void commRandomDisconnect(NetComm *comm, int random)
{
	NetCommDebug	*debug = &comm->debug;

	debug->test_disconnect = 1;
	debug->disconnect_random = random;
}

void commSetSendTimeout(NetComm *comm, F32 seconds)
{
	if(comm)
	{
		comm->send_timeout = seconds;
	}
}

void commSetMinReceiveTimeoutMS(NetComm* comm, U32 ms)
{
	if(comm)
	{
		comm->timeout_msecs = ms;
	}
}

void commSetPacketReceiveMsecs(NetComm* comm, U32 ms)
{
	if(comm)
	{
		comm->packet_receive_msecs = ms;
	}
}


void commFlushAllComms(void)
{
	int i;

	commInitAllCommsMutex();
	EnterCriticalSection(&gppAllCommsMutex);
	for (i=0; i < eaSize(&gppAllComms); i++)
	{
		commFlushAllLinks(gppAllComms[i]);
	}
	LeaveCriticalSection(&gppAllCommsMutex);
}

void commSetProxy(NetComm* comm, const char *host, U16 port)
{
	comm->proxy_host = ipFromString(host);
	comm->proxy_port = port;
}

bool commIsProxyEnabled(NetComm* comm)
{
	return !!comm->proxy_host;
}

void commEnableForcedSendThreadFrames(NetComm* comm, S32 enabled)
{
	if(comm)
	{
		if(enabled)
		{
			comm->last_force_frame_ms = timeGetTime() - 1000;
			if(!comm->last_force_frame_ms){
				comm->last_force_frame_ms = 1;
			}
		}
		else
		{
			comm->last_force_frame_ms = 0;
		}
	}
}

#if NET_VERBOSE_PRINTING
void linkVerbosePrintf(NetLink* link, const char* format, ...)
{
	char buffer[1000];
	
	sprintf(buffer,
			"%d.%p.%p.%d: ",
			GetCurrentThreadId(),
			SAFE_MEMBER(link, listen->comm),
			link,
			SAFE_MEMBER(link, sock));

	VA_START(va, format);
		vsprintf_s(	buffer + strlen(buffer),
					sizeof(buffer) - strlen(buffer),
					format,
					va);
	VA_END();
	
	printf("%s\n", buffer);
}

void commVerbosePrintf(NetComm* comm, const char* format, ...)
{
	char buffer[1000];
	
	sprintf(buffer, "%d.%p: ", GetCurrentThreadId(), comm);

	VA_START(va, format);
		vsprintf_s(	buffer + strlen(buffer),
					sizeof(buffer) - strlen(buffer),
					format,
					va);
	VA_END();
	
	printf("%s\n", buffer);
}
#endif
