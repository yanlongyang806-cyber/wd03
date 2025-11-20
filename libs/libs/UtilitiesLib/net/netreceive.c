#include "sock.h"
#include "net.h"
#include "netprivate.h"
#include "logging.h"
#include "earray.h"
#include "mathutil.h"
#include "utils.h"
#include "crypt.h"
#include "netpacket.h"
#include "nethandshake.h"
#include "timing.h"
#include "netlink.h"
#include "netiocp.h"
#include "netbsd.h"
#include "netsendthread.h"
#include "netreceive.h"
#include "globalComm.h"
#include "textParser.h"
#include "NetPrivate_h_Ast.h"
#include "stringCache.h"
#include "StashTable.h"
#include "resourceInfo.h"
#include "autogen/GlobalComm_h_ast.h"
#include "timedCallback.h"

//if either of these hit, someone mucked with the shared and internal packet commands in an invalid fashion
STATIC_ASSERT(SHAREDCMD_MAX == FIRST_INTERNAL_PACKET_CMD);
STATIC_ASSERT(SHAREDCMD_MAX == 240);


AUTO_RUN_ANON(memBudgetAddMapping("LinkReceiveStats", BUDGET_Networking););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

void logPacket(int id, void *data, int size, int header, char *ext);

static int findEndOfPacket(NetLink *link, char *data,int start,int end)
{
	if(link->flags & LINK_SENT_SOCKS_REQ)
	{
		// Waiting for a SOCKS4 reply, we need at least 8 bytes
		if(end - start >= 8)
			return start + 8;
	}
	else if((link->flags & (LINK_RAW|LINK_HTTP)) == LINK_RAW)
	{
		// If we're specifically a raw link that has nothing to do with HTTP, avoid
		// all of the crazy newline and content length finding code and just say
		// that the packet consists of the entire amount of received data
		return end;
	}
	else
	{
		// Perform the original findDoubleCr() code

		int		i;

		start-=3;
		if (start < 0)
			start=0;
		end-=3;
		for(i=start;i<end;i++)
		{
			if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n')
				return i+4;
		}
	}
	return 0;
}

#define CONTENT_LENGTH_TEXT "Content-Length: "
#define CONNECTION_CLOSE "Connection: close"
int linkGetRawContentLength(char *pHTTPHeader)
{
	char *p;

	p = strstri(pHTTPHeader, CONTENT_LENGTH_TEXT);

	if (!p)
	{
		p = strstri(pHTTPHeader, CONNECTION_CLOSE);
		if(p)
		{
			// They want to send a bunch of data, and then close the connection. Let's just 
			// consider all remaining data to be "raw_data_left"
			return PERPETUALLY_RAW_DATA;
		}

		return 0;
	}

	p += strlen(CONTENT_LENGTH_TEXT);
	return atoi(p);
}


void linkDoDataCorruption(NetLink *link, Packet *pak)
{
	static int iBitsBeforeNextCorrupt = -1;
	int iCurWriteHead;
	int iPakBitSize = pak->size * 8;

	if (iBitsBeforeNextCorrupt == -1)
	{
		iBitsBeforeNextCorrupt = randInt(link->deliberate_data_corruption_freq);
	}

	if (iBitsBeforeNextCorrupt >= iPakBitSize)
	{
		iBitsBeforeNextCorrupt -= iPakBitSize;
		return;
	}

	iCurWriteHead = iBitsBeforeNextCorrupt;
	while (iCurWriteHead < iPakBitSize)
	{
		int iByteNum = iCurWriteHead / 8;
		int iBitNum = iCurWriteHead % 8;

		pak->data[iByteNum] ^= (1 << iBitNum);

		iCurWriteHead += 1 + randInt(link->deliberate_data_corruption_freq);
	}

	iBitsBeforeNextCorrupt = iCurWriteHead - iPakBitSize;
}

bool packetLooksLikeXMLRPC(Packet *pak)
{
	return (pak->size > 5 && !memcmp(pak->data, "POST ", 5)); // I realize this is crazy.
}

LATELINK;
void netreceive_repurposeLinkForXMLRPC(NetLink *link, Packet *pak);
void DEFAULT_LATELINK_netreceive_repurposeLinkForXMLRPC(NetLink *link, Packet *pak)
{
	assertmsg(0, "Link flagged as LINK_REPURPOSED_XMLRPC in an executable not linked against HttpLib!");
}

LATELINK;
void netreceive_processXMLRPC(NetLink *link, Packet *pak);
void DEFAULT_LATELINK_netreceive_processXMLRPC(NetLink *link, Packet *pak)
{
	assertmsg(0, "Link flagged as LINK_REPURPOSED_XMLRPC in an executable not linked against HttpLib!");
}

LATELINK;
void * netreceive_createXMLRPCUserData(void);
void * DEFAULT_LATELINK_netreceive_createXMLRPCUserData(void)
{
	assertmsg(0, "Link flagged as LINK_REPURPOSED_XMLRPC in an executable not linked against HttpLib!");
	return NULL;
}

LATELINK;
void netreceive_socksRecieveError(NetLink *link, U8 code);
void DEFAULT_LATELINK_netreceive_socksRecieveError(NetLink *link, U8 code)
{
	char host[32];
	assertmsgf(code==0x5A, "SOCKS request rejected from %s: %X", linkGetIpStr(link, SAFESTR(host)), code);
}

int linkAsyncReadRaw(NetLink *link, int async_bytes_read, PacketCallback *msg_cb)
{
	Packet	*new_pak,*pak = link->recv_pak;
	int		start,end,leftover,amt;
	void	*memory = pak->data;

	PERFINFO_AUTO_START_FUNC();

	start = pak->size;
	pak->size += async_bytes_read;

	// -----------------------------------------------------------------------------------------
	// RAW DATA CODE 
handle_raw_data:
	while((link->raw_data_left > 0) || (link->raw_data_left == PERPETUALLY_RAW_DATA))
	{
		if((pak->size > link->raw_data_left) && (link->raw_data_left != PERPETUALLY_RAW_DATA))
		{
			// The beginning of this packet must be split off as the remainder of the raw data

			leftover = pak->size - link->raw_data_left;
			pak->size = link->raw_data_left;
			link->recv_pak = new_pak = pktCreateRawForReceiving(link);
			pktGrow(new_pak,leftover+PACKET_SIZE);
			memcpy(new_pak->data,pak->data+link->raw_data_left,leftover);

			link->raw_data_left    -= pak->size;
			if (link->deliberate_data_corruption_freq)
			{
				linkDoDataCorruption(link, pak);
			}

			if(link->flags & LINK_REPURPOSED_XMLRPC)
			{
				netreceive_processXMLRPC(link, pak);
			}
			else if (msg_cb && !link->cleared_user_link_ptr)
			{
				START_BIT_COUNT(pak, "msg_cb");
				msg_cb(pak,0,link,link->user_data);
				STOP_BIT_COUNT(pak);
			}

			link->stats.recv.bytes += pak->size;
			link->stats.recv.packets++;
			pktFree(&pak);

			pak = new_pak;
			pak->size = leftover;
			start = 0;
		}
		else
		{
			// The packet is either the exact amount, or not enough.

			if(link->raw_data_left != PERPETUALLY_RAW_DATA)
				link->raw_data_left -= pak->size;

			if (link->deliberate_data_corruption_freq)
			{
				linkDoDataCorruption(link, pak);
			}

			if(link->flags & LINK_REPURPOSED_XMLRPC)
			{
				netreceive_processXMLRPC(link, pak);
			}
			else if (msg_cb && !link->cleared_user_link_ptr)
			{
				START_BIT_COUNT(pak, "msg_cb");
				msg_cb(pak,0,link,link->user_data);
				STOP_BIT_COUNT(pak);
			}

			link->stats.recv.bytes += pak->size;
			link->stats.recv.packets++;

			link->recv_pak = new_pak = pktCreateRawForReceiving(link);
			pktFree(&pak);

			pak = new_pak;
			start = 0;

			pktGrow(pak,PACKET_SIZE);
			linkStatus(link,"read raw");
			
			PERFINFO_AUTO_START("linkAsyncReadRaw:safeWSARecv1", 1);
			amt = safeWSARecv(link->sock,pak->max_size - pak->size,pak, "linkAsyncReadRaw:safeWSARecv1");
			PERFINFO_AUTO_STOP();
			
			PERFINFO_AUTO_STOP();// FUNC.
			return amt >= 0;
		}
	}
	// -----------------------------------------------------------------------------------------

	while(link->raw_data_left == 0 && (end = findEndOfPacket(link, pak->data, start, pak->size)))
	{
		leftover = pak->size - end;
		pak->size = end;
		link->recv_pak = new_pak = pktCreateRawForReceiving(link);
		pktGrow(new_pak,leftover+PACKET_SIZE);
		memcpy(new_pak->data,pak->data+end,leftover);
		pktGrow(pak,1);
		pak->data[pak->size]=0;

		if(link->flags & LINK_HTTP)
		{
			link->raw_data_left = linkGetRawContentLength((char*)pak->data);
		}

		if (!(link->flags & LINK_RAW) && link->stats.recv.packets == 0)
		{
			NetListen	*listen = link->listen;

			linkStatus(link,"got first packet\n");
			if (link->listen->listen_sock != INVALID_SOCKET)
			{
				if((link->flags & LINK_ALLOW_XMLRPC) && packetLooksLikeXMLRPC(pak))
				{
					netreceive_repurposeLinkForXMLRPC(link, pak);
				}

				if(!(link->flags & LINK_REPURPOSED_XMLRPC))
				{
					netFirstServerPacket(link,pak);
				}
			}				
			else if(link->flags & LINK_SENT_SOCKS_REQ)
			{
				char host[32];
				if(pak->data[1]!=0x5A)
				{
					Errorf("SOCKS request rejected from %s: %X", linkGetIpStr(link, SAFESTR(host)), pak->data[1]);
					netreceive_socksRecieveError(link, pak->data[1]);
					link->connected = 1;
					link->notify_on_disconnect = 1;
					linkQueueRemove(link, "SOCKS request rejected");
				}
				else
				{
					printf("SOCKS connection active\n");
				}
				link->flags &= ~(LINK_WAITING_FOR_SOCKS|LINK_SENT_SOCKS_REQ);
				pktFree(&pak);
				pak = new_pak;
				pak->size = leftover;
				start = 0;
				link->stats.recv.real_packets--;
				continue;
			}
			else
			{
				netFirstClientPacket(link,pak);
			}

			if(link->flags & LINK_REPURPOSED_XMLRPC)
			{
				devassert(link->user_data == NULL);
				link->user_data = netreceive_createXMLRPCUserData();

				netreceive_processXMLRPC(link, pak);
			}
			else if(!link->cleared_user_link_ptr)
			{
				linkUserConnect(link);
			}
		}
		else 
		{
			if (link->deliberate_data_corruption_freq)
			{
				linkDoDataCorruption(link, pak);
			}

			if(link->flags & LINK_REPURPOSED_XMLRPC)
			{
				netreceive_processXMLRPC(link, pak);
			}
			else if (msg_cb && !link->cleared_user_link_ptr)
			{
				START_BIT_COUNT(pak, "msg_cb");
				msg_cb(pak,0,link,link->user_data);
				STOP_BIT_COUNT(pak);
			}
		}
		link->stats.recv.bytes+=pak->size; 
		link->stats.recv.packets++;
		pktFree(&pak);
		pak = new_pak;
		pak->size = leftover;
		start = 0;
	}

	// Immediately process any raw data that has already been sent
	if((link->raw_data_left > 0) && (link->recv_pak->size > 0))
	{
		pak = link->recv_pak;
		start = 0;
		goto handle_raw_data;
	}

	pktGrow(pak,PACKET_SIZE);
	linkStatus(link,"read raw");

	PERFINFO_AUTO_START("linkAsyncReadRaw:safeWSARecv2", 1);
	amt = safeWSARecv(link->sock,pak->max_size - pak->size,pak,"linkAsyncReadRaw:safeWSARecv2");
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();// FUNC.
	return amt >= 0;
}


static void linkRecvPing(NetLink *link,Packet *pak_in)
{
	U32				sender_id = pktGetBits(pak_in,32);

	Packet	*pak;
	static PacketTracker *pTracker;

	ONCE(pTracker = PacketTrackerFind("linkRecvPing", 0, NULL));

	pak = pktCreateSize_dbg(link,64,PACKETCMD_PINGACK, pTracker, __FILE__, __LINE__);
	pktSendU32(pak,sender_id);
	if (link->flags & LINK_FLUSH_PING_ACKS)
	{
		pktSend(&pak);
		linkFlush(link);
	}
	else
		pktSendNoFlush(&pak);

	link->ping_recv_count++;
}

static void linkRecvPingAck(NetLink *link,Packet *pak)
{
	int				i,idx;
	U32				real_id = pktGetBits(pak,32);
	PacketHistory	*history;

	link->ping_ack_recv_count++;

	if (!real_id)
	{
		return;
	}

	for(i=0;i<ARRAY_SIZE(link->stats.history);i++)
	{
		idx = (link->stats.history_idx - i) & (ARRAY_SIZE(link->stats.history)-1);
		history = &link->stats.history[idx];
		if (history->real_id == real_id)
		{
			link->stats.last_recv_idx = idx;
			history->elapsed = 	timerSeconds(timerCpuTicks() - history->time);
			history->curr_real_recv = link->compressed_bytes_recv_since_ping_ack;
			history->curr_recv = link->uncompressed_bytes_recv_since_ping_ack;
		}
	}
}

static void linkRecvLag(NetLink *link,Packet *pak)
{
	link->lag = pktGetU32(pak);
	link->lag_vary = pktGetU32(pak);
}

static void linkAddProfilerCountForBytesWaitingToRecv(const NetLink* link)
{
	START_MISC_COUNT(0, "waiting for more data");
	{
		#define ADD(x) else if(link->bytesWaitingToRecv < x){ADD_MISC_COUNT(1, "waiting for <"#x" bytes");}
		if(0){}
		ADD(10)
		ADD(20)
		ADD(30)
		ADD(40)
		ADD(50)
		ADD(60)
		ADD(70)
		ADD(80)
		ADD(90)
		ADD(100)
		ADD(200)
		ADD(300)
		ADD(400)
		ADD(500)
		ADD(600)
		ADD(700)
		ADD(800)
		ADD(900)
		ADD(1000)
		ADD(2000)
		ADD(3000)
		ADD(4000)
		ADD(5000)
		ADD(6000)
		ADD(7000)
		ADD(8000)
		ADD(9000)
		ADD(10000)
		ADD(20000)
		ADD(30000)
		ADD(40000)
		ADD(50000)
		ADD(60000)
		ADD(70000)
		ADD(80000)
		ADD(90000)
		ADD(100000)
		ADD(200000)
		ADD(300000)
		ADD(400000)
		ADD(500000)
		ADD(600000)
		ADD(700000)
		ADD(800000)
		ADD(900000)
		ADD(1000000)
		ADD(2000000)
		ADD(3000000)
		ADD(4000000)
		ADD(5000000)
		ADD(6000000)
		ADD(7000000)
		ADD(8000000)
		ADD(9000000)
		ADD(10000000)
		ADD(20000000)
		ADD(30000000)
		ADD(40000000)
		ADD(50000000)
		ADD(60000000)
		ADD(70000000)
		ADD(80000000)
		ADD(90000000)
		ADD(100000000)
		ADD(200000000)
		ADD(300000000)
		ADD(400000000)
		ADD(500000000)
		ADD(600000000)
		ADD(700000000)
		ADD(800000000)
		ADD(900000000)
		ADD(1000000000)
		else{
			ADD_MISC_COUNT(1, "waiting for >=1,000,000,000 bytes")
		}
		#undef ADD
	}
	STOP_MISC_COUNT(1);
}

int asyncRead(NetLink *link, int async_bytes_read, PacketCallback *msg_cb)
{
	Packet	*pak = link->recv_pak;
	S32		cmd,size,amt,start,end,remaining,pak_idx=0,pak_size,actual_read=async_bytes_read;
	S32		resetPingAck = 0;

	PERFINFO_AUTO_START_FUNC();

	linkStatus(link,"start read cooked");

	start	= pak->size;
	end		= pak->size + async_bytes_read;
	if (link->flags & LINK_ENCRYPT)
	{
		PERFINFO_AUTO_START("cryptRc4", 1);
			cryptRc4(link->encrypt->decode,pak->data + start,end - start);
		PERFINFO_AUTO_STOP();
	}
	if (link->flags & LINK_COMPRESS)
	{
		PERFINFO_AUTO_START("pktUncompress", 1);
			async_bytes_read = pktUncompress(pak,end-start);
		PERFINFO_AUTO_STOP();

		if (async_bytes_read < 0)
		{
			linkSetDisconnectReason(link, "async_bytes_read < 0");
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
		end	= pak->size + async_bytes_read;
	}

	pktPushRing(pak, &pak->link->debug_recv_ring, pak->data + pak->size, async_bytes_read);

	link->stats.recv.bytes+=async_bytes_read;
	link->uncompressed_bytes_recv_since_ping_ack += async_bytes_read;
	link->compressed_bytes_recv_since_ping_ack += actual_read;

	pak_size = pak->size + async_bytes_read;
	pak->idx = 0;
	START_BIT_COUNT(pak, "allPaks");
	for(;;)
	{
		remaining = end - pak->idx;
		if (remaining < NET_PACKET_SIZE_BYTES)
			break;

		if (link->flags & LINK_CRC && remaining >= NET_LINK_PACKET_HEADER_SIZE(link))
		{
			U32		seq_id = bytesToU32(pak->data + pak->idx + NET_PACKET_SIZE_BYTES + NET_PACKET_CRC_BYTES);

			if(seq_id != link->recv_seq_id + 1)
			{
				char	ip_buf[100];
				
				log_printf(LOG_NETCRC,"%s bad seq id %d/%d",linkGetIpStr(link,ip_buf,sizeof(ip_buf)),seq_id,link->recv_seq_id + 1);
				START_BIT_COUNT(pak, "badSeqID");
				STOP_BIT_COUNT(pak);
				STOP_BIT_COUNT(pak);
				linkSetDisconnectReason(link, "seq_id != link->recv_seq_id + 1");
				PERFINFO_AUTO_STOP_FUNC();
				return 0;
			}
		}

		size = bytesToU32(&pak->data[pak->idx]);

		if (size < NET_LINK_PACKET_HEADER_SIZE(link))
		{
			char	ip_buf[100];

			log_printf(LOG_NETCRC, "%s bad size: %d - id: %d", linkGetIpStr(link,ip_buf,sizeof(ip_buf)), size, pak->id);
			START_BIT_COUNT(pak, "badSize");
			STOP_BIT_COUNT(pak);
			STOP_BIT_COUNT(pak);
			linkSetDisconnectReason(link, "size < NET_LINK_PACKET_HEADER_SIZE(link)");
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}

		if (size > remaining)
		{
			linkVerbosePrintf(link, "received partial %d/%d", remaining, size);
			link->bytesWaitingToRecv = size - remaining;
			if(PERFINFO_RUN_CONDITIONS){
				linkAddProfilerCountForBytesWaitingToRecv(link);
			}
			break;
		}

		link->bytesWaitingToRecv = 0;
		linkVerbosePrintf(link, "received full %d/%d", remaining, size);
		pak->size = pak->idx + size;
		link->recv_seq_id += 1;
		
		START_BIT_COUNT(pak, "pak size");
			pak->idx += NET_PACKET_SIZE_BYTES;
		STOP_BIT_COUNT(pak);
		
		if (link->flags & LINK_CRC)
		{
			U32		crc,local_crc;

			START_BIT_COUNT(pak, "pak crc and seq id");
				crc = bytesToU32(pak->data + pak->idx);
				pak->idx += NET_PACKET_CRC_HEADER_SIZE;
			STOP_BIT_COUNT(pak);

			PERFINFO_AUTO_START("cryptAdler32", 1);
				local_crc = cryptAdler32(pak->data + pak->idx,pak->size - pak->idx);
			PERFINFO_AUTO_STOP();
			if (crc != local_crc)
			{
				char	ip_buf[100];

				log_printf(LOG_NETCRC,"%s bad crc - size: %d - id: %d",linkGetIpStr(link,ip_buf,sizeof(ip_buf)), pak->size, pak->id);
				START_BIT_COUNT(pak, "badCRC");
				STOP_BIT_COUNT(pak);
				STOP_BIT_COUNT(pak);
				linkSetDisconnectReason(link, "crc != local_crc");
				PERFINFO_AUTO_STOP_FUNC();
				return 0;
			}
		}
		cmd = pktGetBits(pak,8);
		link->stats.recv.packets++;
		pak->id = ++link->pak_id;

		if (cmd >= FIRST_INTERNAL_PACKET_CMD) switch(cmd)
		{
			xcase PACKETCMD_PING:
				START_BIT_COUNT(pak, "linkRecvPing");
					linkRecvPing(link,pak);
				STOP_BIT_COUNT(pak);
			xcase PACKETCMD_PINGACK:
				START_BIT_COUNT(pak, "linkRecvPingAck");
					resetPingAck = 1;
					linkRecvPingAck(link, pak);
				STOP_BIT_COUNT(pak);
			xcase PACKETCMD_LAG:
				START_BIT_COUNT(pak, "linkRecvLag");
					linkRecvLag(link,pak);
				STOP_BIT_COUNT(pak);
			xcase PACKETCMD_DISCONNECT:
				START_BIT_COUNT(pak, "disconnect");
				STOP_BIT_COUNT(pak);
				STOP_BIT_COUNT(pak);
				linkSetDisconnectReason(link, "PACKETCMD_DISCONNECT");
				PERFINFO_AUTO_STOP_FUNC();
				return 0;
		}
		else if (msg_cb)
		{
	
			if (link->deliberate_data_corruption_freq)
			{
				linkDoDataCorruption(link, pak);
			}
			
			if(!link->cleared_user_link_ptr)
			{
				PerfInfoGuard* piGuard;

				
				PERFINFO_AUTO_START_GUARD("msg_cb", 1, &piGuard);
				START_BIT_COUNT(pak, "msg_cb");
				
				if (gbTrackLinkReceiveStats)
				{
					S64 iTicks = timerCpuTicks64();
					U32 iSize = pktGetSize(pak);

					msg_cb(pak,cmd,link,link->user_data);
					linkUpdateReceiveStats(link, cmd, iSize, timerCpuTicks64() - iTicks);
				}
				else
				{
					msg_cb(pak,cmd,link,link->user_data);
				}

				STOP_BIT_COUNT(pak);
				PERFINFO_AUTO_STOP_GUARD(&piGuard);
			}
		}

		START_BIT_COUNT(pak, "pak size clamp");
			pak->idx = pak->size;
		STOP_BIT_COUNT(pak);
	}
	STOP_BIT_COUNT(pak);
	
	if(resetPingAck){
		link->compressed_bytes_recv_since_ping_ack = 0;
		link->uncompressed_bytes_recv_since_ping_ack = 0;
	}

	remaining = pak_size - pak->idx;
	PERFINFO_AUTO_START("memmove", 1);
	if (pak->idx && remaining)
		memmove(pak->data,pak->data + pak->idx,remaining);
	PERFINFO_AUTO_STOP();
	pak->idx = 0;
	pak->size = remaining;

	START_BIT_COUNT(pak, "remaining");
		pak->idx = remaining;
	STOP_BIT_COUNT(pak);
	pak->idx = 0;
	pktGrow(pak,PACKET_SIZE);
	if (!pak->size && link->pktsize_max && pak->max_size > link->pktsize_max)
	{
		int iOldMaxSize = pak->max_size;
		pak->data = pktReallocData(pak->data, link->pktsize_max, pak->caller_fname, pak->line);
		pak->max_size_mutable = link->pktsize_max;
		TrackerReportPacketResize(pak, iOldMaxSize);
	}
	if (pak->max_size < link->curr_recvbuf)
	{
		pktGrow(pak,link->curr_recvbuf - pak->max_size);
	}
	linkStatus(link,"end read cooked");
	PERFINFO_AUTO_START("safeWSARecv", 1);
		amt = safeWSARecv(link->sock,MIN(pak->max_size - pak->size,link->curr_recvbuf),pak,"asyncRead");
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP_FUNC();
	return amt >= 0;
}

static void processDisconnects(NetComm *comm)
{
	int		i;
	NetLink	*link;

	if(!eaSize(&comm->remove_list)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	// delete links that are disconnected, and have freed their send/receive buffers
	for(i=eaSize(&comm->remove_list)-1;i>=0;i--)
	{
		link = comm->remove_list[i];

		if(	!link->outbox &&
			!link->shuttingDown &&
			(	!link->recv_pak ||
				!link->recv_pak->receiving))
		{
			if (link->recv_pak){
				linkStatus(link,"freed + link->recv_pak");
			}else{
				linkStatus(link,"freed");
			}
			linkFree(link);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

static void processFlushedDisconnects(NetComm *comm, F32 timeout)
{
	int		i;
	U32		now;
	NetLink	*link;
	F32		seconds;

	if(!eaSize(&comm->flushed_disconnects)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	now = timerCpuTicks();
	
	for(i=eaSize(&comm->flushed_disconnects)-1;i>=0;i--)
	{
		link = comm->flushed_disconnects[i];

		if (link->disconnect_timer)
		{
			seconds = timerSeconds(now - link->disconnect_timer);
			if (seconds > timeout)
			{
				linkStatus(link,"flushed disconnect");
				eaRemove(&comm->flushed_disconnects,i);
				safeCloseLinkSocket(link, __FUNCTION__);
				
				//ABW Bruce and I copied this here from Netlink.c 413 in an effort to solve a weird
				//bug where the disconnect callback seems to not be called when FlushAndClose is called
				//in certain corner timing cases
				
				linkUserDisconnect(link);

				if(	!link->outbox &&
					!link->connected &&
					!link->recv_pak)
				{
					// This happens if you flush and close a link that was never connected.
					// Without this, the link will forever get calls to netSendConnectPkt from
					// checkLinkStatus from commConnectMonitor.

					linkFree(link);
				} else {
					linkQueueRemove(link, __FUNCTION__);
				}
			}
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void commFlushDisconnects(NetComm *comm)
{
	int		i;
	NetLink	*link;

	while (eaSize(&comm->flushed_disconnects) || eaSize(&comm->remove_list))
	{
		iocpProcessPackets(comm, -1, FIRST_IF_SET(comm->packet_receive_msecs, 100));
		processDisconnects(comm);
		processFlushedDisconnects(comm, 0.001f);
	}

	if (0) // This code doesn't ever free the link, it's packets, etc, the above code should
	{
		for(i=eaSize(&comm->flushed_disconnects)-1;i>=0;i--)
		{
			link = comm->flushed_disconnects[i];

			if (link->disconnect_timer)
			{
				linkStatus(link,"flushed disconnect");
				eaRemove(&comm->flushed_disconnects,i);
				safeCloseLinkSocket(link, __FUNCTION__);
			}
		}
	}
}

static void disconnectAll(NetComm *comm)
{
	int				i,j;
	NetListen		*nl;
	NetLink			*link;

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		nl = comm->listen_list[i];
		for(j=0;j<eaSize(&nl->links);j++)
		{
			link = nl->links[j];
			safeCloseLinkSocket(link, __FUNCTION__);
		}
	}
}

void commTestDisconnect(NetComm *comm)
{
	NetCommDebug	*debug = &comm->debug;

	if (!comm->debug.test_disconnect)
		return;
		
	PERFINFO_AUTO_START_FUNC();
	
	if (debug->disconnect_seconds)
	{
		if (!debug->timer)
			debug->timer = timerAlloc();
		if (timerElapsed(debug->timer) > debug->disconnect_seconds)
		{
			disconnectAll(comm);
			timerStart(debug->timer);
		}
	}
	if (debug->disconnect_random)
	{
		if (randInt(debug->disconnect_random) == 0)
			disconnectAll(comm);
	}
	
	PERFINFO_AUTO_STOP();
}


void commProcessPackets(NetComm *comm, S32 timeoutOverrideMilliseconds)
{
	iocpProcessPackets(	comm,
						timeoutOverrideMilliseconds,
						FIRST_IF_SET(comm->packet_receive_msecs, 100));

	bsdProcessPackets(comm, timeoutOverrideMilliseconds);
	commCheckBsdAccepts(comm);
	processDisconnects(comm);
	processFlushedDisconnects(comm, 5);
	commTestDisconnect(comm);
}

int linkReceiveCore(NetLink *link,int bytes_xferred)
{
	int		ok=0;

	if (link->sendbuf_full)
		link->sendbuf_full = 0;
	link->stats.recv.last_time_ms = link->listen->comm->last_check_ms;
	if (link->connected && !(link->flags & LINK_RAW))
		ok = asyncRead(link,bytes_xferred,link->listen->message_callback);
	else
	{
		ok = linkAsyncReadRaw(link,bytes_xferred,link->listen->message_callback);
		if (link->connected && !(link->flags & LINK_RAW) && link->recv_pak->size)
		{
			bytes_xferred = link->recv_pak->size;
			link->recv_pak->size = 0;
			link->recv_pak->has_verify_data = !!(link->flags & LINK_PACKET_VERIFY);
			ok = asyncRead(link,bytes_xferred,link->listen->message_callback);
		}
	}
	return ok;
}

static const char *GetSizeGroupName(int iIndex)
{
	char *pTemp = NULL;
	char retVal[128];
	estrStackCreate(&pTemp);
	estrMakePrettyBytesString(&pTemp, (((S64)1) << (iIndex)));
	sprintf(retVal, "%s to ", pTemp);
	estrMakePrettyBytesString(&pTemp, (((S64)1) << (iIndex + 1)));
	strcat(retVal, pTemp);
	estrDestroy(&pTemp);

	return allocAddString(retVal);
}

static const char *GetDurationGroupName(int iIndex)
{
	static char *pTemp = NULL;

	if (iIndex == 0)
	{
		return "Short";
	}
	if (iIndex >= 31)
	{
		return "Long";
	}


	timeTicksToPrettyEString((((S64)1) << (iIndex + 1)), &pTemp);
	estrInsertf(&pTemp, 0, "%I64d to ", (((S64)1) << (iIndex)));

	return allocAddString(pTemp);
}

static int GetDurationIndexFromS64(S64 iInt)
{
	if (iInt >= ((S64)1) << 30)
	{
		return 31;
	}

	if (iInt <=0)
	{
		return 0;
	}

	return highBitIndex((U32)iInt);
}


static void linkReceiveStats_AddToSizeCounts(LinkReceiveStats_SizeCounts *pSizeCounts, int iSize, S64 iTicks)
{
	int iSizeIndex = highBitIndex(iSize);
	int iDurationIndex;
	LinkReceiveStats_OneSize *pOneSizeStats = NULL;
	LinkReceiveStats_OneDuration *pOneDurationStats = NULL;

	if (iSize > pSizeCounts->iMaxSize)
	{
		pSizeCounts->iMaxSize = iSize;
	}

	if (iTicks > pSizeCounts->iMaxDuration)
	{
		pSizeCounts->iMaxDuration = iTicks;
	}

	pSizeCounts->iTotalCount++;
	pSizeCounts->iTotalSize += iSize;
	pSizeCounts->iTotalDuration += iTicks;

	if (iSizeIndex < eaSize(&pSizeCounts->ppBySizeGroups))
	{
		pOneSizeStats = pSizeCounts->ppBySizeGroups[iSizeIndex];
	}

	if (!pOneSizeStats)
	{
		pOneSizeStats = StructCreate(parse_LinkReceiveStats_OneSize);
		pOneSizeStats->pName = GetSizeGroupName(iSizeIndex);

		eaSet(&pSizeCounts->ppBySizeGroups, pOneSizeStats, iSizeIndex);
	}

	pOneSizeStats->iCount++;
	pOneSizeStats->iSize += iSize;
	pOneSizeStats->iDuration += iTicks;


	iDurationIndex = GetDurationIndexFromS64(iTicks);

	if (iDurationIndex < eaSize(&pSizeCounts->ppByDurationGroups))
	{
		pOneDurationStats = pSizeCounts->ppByDurationGroups[iDurationIndex];
	}

	if (!pOneDurationStats)
	{
		pOneDurationStats = StructCreate(parse_LinkReceiveStats_OneDuration);
		pOneDurationStats->pName = GetDurationGroupName(iDurationIndex);

		eaSet(&pSizeCounts->ppByDurationGroups, pOneDurationStats, iDurationIndex);
	}

	pOneDurationStats->iCount++;
	pOneDurationStats->iTotalDuration += iTicks;
	pOneDurationStats->iTotalSize += iSize;


}


//if EndTime is set, then we are doing a delta from the first time to the last time.
//if StartTime is set, but endtime is not set, then we were doing a delta before, then started back doing
//live recording at startTime

static U32 siReceiveStatsDeltaStartTime = 0;
static U32 siReceiveStatsDeltaEndTime = 0;

static void ClearStats(LinkReceiveStats *pStats)
{
	pStats->iFirstDataTime = 0;
	StructReset(parse_LinkReceiveStats_SizeCounts, &pStats->overall);
	eaClearStruct(&pStats->ppCommandStats, parse_LinkReceiveStats_PerCommand);
}

static void linkReceiveStats_ReportPacket(LinkReceiveStats *pStats, int iCmd, int iSize, S64 iTicks)
{
	LinkReceiveStats_PerCommand *pPerCommand;

	//special check... don't give stats on multiplex packets, because they're all reported inside the multiplex code with their
	//"real" payload
	if (iCmd == SHAREDCMD_MULTIPLEX)
	{
		return;
	}
	
	if (pStats->iCurDeltaStartTime != siReceiveStatsDeltaStartTime)
	{
		ClearStats(pStats);
		pStats->iCurDeltaStartTime = siReceiveStatsDeltaStartTime;
	}

	if (siReceiveStatsDeltaEndTime && timeSecondsSince2000() >= siReceiveStatsDeltaEndTime)
	{
		return;
	}

	if (!pStats->iFirstDataTime)
	{
		pStats->iFirstDataTime = timeSecondsSince2000();
	}



	if (pStats->pParent)
	{
		linkReceiveStats_ReportPacket(pStats->pParent, iCmd, iSize, iTicks);
	}

	if (pStats->ppGroups)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pStats->ppGroups, LinkReceiveStats, pGroup)
		{
			linkReceiveStats_ReportPacket(pGroup, iCmd, iSize, iTicks);
		}
		FOR_EACH_END;
	}

	linkReceiveStats_AddToSizeCounts(&pStats->overall, iSize, iTicks);

	pPerCommand = eaIndexedGetUsingInt(&pStats->ppCommandStats, iCmd);
	if (!pPerCommand)
	{
		pPerCommand = StructCreate(parse_LinkReceiveStats_PerCommand);
		pPerCommand->iCommandNum = iCmd;

		if (iCmd >= FIRST_SHARED_CMD_ID)
		{
			pPerCommand->pCommandName = strdup(StaticDefineInt_FastIntToString(SharedCmdIDsEnum, iCmd));
		} 
		else if (pStats->pCommandNames)
		{
			pPerCommand->pCommandName = strdup(StaticDefineInt_FastIntToString(pStats->pCommandNames, iCmd));
		}
		else
		{
			pPerCommand->pCommandName = strdupf("%d", iCmd);
		}

		eaPush(&pStats->ppCommandStats, pPerCommand);
	}

	linkReceiveStats_AddToSizeCounts(&pPerCommand->perCommandTotals, iSize, iTicks);
}


void linkUpdateReceiveStats(NetLink *pLink, int iCmd, int iSize, S64 iTicks)
{
	if (!pLink->pReceiveStats)
	{
		linkInitReceiveStats(pLink, NULL);	
	}

	linkReceiveStats_ReportPacket(pLink->pReceiveStats, iCmd, iSize, iTicks);
}

static StashTable sNetListenReceiveStats = NULL;
static CRITICAL_SECTION sReceiveStatsCriticalSection = {0};

static void lazyInitReceieveStatsStashTable(void)
{
	if (!sNetListenReceiveStats)
	{
		sNetListenReceiveStats = stashTableCreateWithStringKeys(10, StashDefault);
		resRegisterDictionaryForStashTable("NetListenReceiveStats", RESCATEGORY_SYSTEM, 0, sNetListenReceiveStats, parse_LinkReceiveStats);
	}
}

void TurnLinkReceiveStatsBackOffCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (siReceiveStatsDeltaEndTime == (U32)(((intptr_t)userData)))
	{
		gbTrackLinkReceiveStats = false;
	}
}


//clear all ReceiveStats, then capture a specified number of seconds of data. 0 to cancel
//and go back to normal operation. If gbTrackLinkReceiveStats is off, this will turn it on, then turn it back off when the delta is done
AUTO_COMMAND;
void ReceiveStats_CaptureDelta(U32 iNumSeconds)
{


	siReceiveStatsDeltaStartTime = timeSecondsSince2000();
	if (iNumSeconds)
	{
		siReceiveStatsDeltaEndTime = siReceiveStatsDeltaStartTime + iNumSeconds;


		if (!gbTrackLinkReceiveStats)
		{
			gbTrackLinkReceiveStats = true;
			TimedCallback_Run(TurnLinkReceiveStatsBackOffCB, (void*)((intptr_t)siReceiveStatsDeltaEndTime), iNumSeconds + 5);
		}

	}
	else
	{
		siReceiveStatsDeltaEndTime = 0;
	}



}


void linkReceiveStats_AddLinkToNamedGroup(NetLink *pLink, char *pNamedGroup)
{
	LinkReceiveStats *pGroup = NULL;

	linkInitReceiveStats(pLink, NULL);

	if (eaIndexedGetUsingString(&pLink->pReceiveStats->ppGroups, pNamedGroup))
	{
		return;
	}

	EnterCriticalSection(&sReceiveStatsCriticalSection);
	lazyInitReceieveStatsStashTable();

	if (!stashFindPointer(sNetListenReceiveStats, pNamedGroup, &pGroup))
	{
		pGroup = StructCreate(parse_LinkReceiveStats);

		pGroup->pName = strdup(pNamedGroup);
		pGroup->pCommandNames = pLink->pReceiveStats->pCommandNames;

		stashAddPointer(sNetListenReceiveStats, pGroup->pName, pGroup, true);
	}


	eaPush(&pLink->pReceiveStats->ppGroups, pGroup);

	LeaveCriticalSection(&sReceiveStatsCriticalSection);
}


void listenInitReceiveStats(NetListen *listen, char *pDebugName, StaticDefineInt *pCommandNames)
{
	if (listen->pReceiveStats)
	{
		return;
	}

	listen->pReceiveStats = StructCreate(parse_LinkReceiveStats);
	listen->pReceiveStats->pName = pDebugName ? strdup(pDebugName) : strdupf("NetListen: created %s(%d)", listen->creationFile, listen->creationLine);
	listen->pReceiveStats->pCommandNames = pCommandNames;

	FOR_EACH_IN_EARRAY(listen->links, NetLink, link)
	{
		linkInitReceiveStats(link, pCommandNames);
	}
	FOR_EACH_END;

	ATOMIC_INIT_BEGIN;
		InitializeCriticalSection(&sReceiveStatsCriticalSection);
	ATOMIC_INIT_END;

	EnterCriticalSection(&sReceiveStatsCriticalSection);
	lazyInitReceieveStatsStashTable();

	stashAddPointer(sNetListenReceiveStats, listen->pReceiveStats->pName, listen->pReceiveStats, true);
	LeaveCriticalSection(&sReceiveStatsCriticalSection);

}

void linkInitReceiveStats(NetLink *pLink, StaticDefineInt *pCommandNames)
{
	NetListen *pListen;

	if (pLink->pReceiveStats)
	{
		return;
	}

	pListen = pLink->listen;

	if (pLink->pReceiveStats)
	{
		return;
	}

	pLink->pReceiveStats = StructCreate(parse_LinkReceiveStats);
	pLink->pReceiveStats->pName = strdupf("Link: %s", linkDebugName(pLink));

	if (pListen && pListen->pReceiveStats)
	{
		pLink->pReceiveStats->pCommandNames = pListen->pReceiveStats->pCommandNames;
		pLink->pReceiveStats->pParent = pListen->pReceiveStats;
	}
	else
	{
		pLink->pReceiveStats->pCommandNames = pCommandNames;
	}
}



AUTO_FIXUPFUNC;
TextParserResult fixupLinkReceiveStats(LinkReceiveStats *pStats, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	xcase FIXUPTYPE_CONSTRUCTOR: 
		//seems redundant, need to do it explicitly on the object DB
		eaIndexedEnable(&pStats->ppCommandStats, parse_LinkReceiveStats_PerCommand);
		eaIndexedEnable(&pStats->ppGroups, parse_LinkReceiveStats);

	xcase FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pStats->ppGroups);

	xcase FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		if (siReceiveStatsDeltaStartTime != pStats->iCurDeltaStartTime)
		{
			ClearStats(pStats);
			pStats->iCurDeltaStartTime = siReceiveStatsDeltaStartTime;
		}

		if (siReceiveStatsDeltaEndTime)
		{
			if (siReceiveStatsDeltaEndTime < timeSecondsSince2000())
			{
				static char *spDuration = NULL;

				timeSecondsDurationToPrettyEString(siReceiveStatsDeltaEndTime - siReceiveStatsDeltaStartTime, &spDuration);
				estrPrintf(&pStats->pComment, "%s of data starting at %s, complete. To make a new delta or cancel, execute ReceiveStats_CaptureDelta",
					spDuration, timeGetLocalDateStringFromSecondsSince2000(siReceiveStatsDeltaStartTime));
			}
			else
			{
				static char *spDuration = NULL;

				timeSecondsDurationToPrettyEString(siReceiveStatsDeltaEndTime - siReceiveStatsDeltaStartTime, &spDuration);
				estrPrintf(&pStats->pComment, "%s of data starting at %s, still in progress. To make a new delta or cancel, execute ReceiveStats_CaptureDelta",
					spDuration, timeGetLocalDateStringFromSecondsSince2000(siReceiveStatsDeltaStartTime));
			}
		}
		else
		{
			estrPrintf(&pStats->pComment, "Live stats started at %s. To do a delta, execute ReceiveStats_CaptureDelta",
				pStats->iFirstDataTime ? timeGetLocalDateStringFromSecondsSince2000(pStats->iFirstDataTime) : "(never)");
		}
		

	}

	return 1;
}

AUTO_FIXUPFUNC;
TextParserResult fixupLinkReceiveStats_SizeCounts(LinkReceiveStats_SizeCounts *pSizeCounts, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	xcase FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		if (pSizeCounts->iTotalCount)
		{
			pSizeCounts->iAverageDuration = pSizeCounts->iTotalDuration / pSizeCounts->iTotalCount;
		}
	}

	return 1;
}

AUTO_FIXUPFUNC;
TextParserResult fixupLinkReceiveStats_OneSize(LinkReceiveStats_OneSize *pOneSize, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	xcase FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		if (pOneSize->iCount)
		{
			pOneSize->iAverageDuration = pOneSize->iDuration / pOneSize->iCount;
		}
	}

	return 1;
}


AUTO_FIXUPFUNC;
TextParserResult fixupLinkReceiveStats_OneDuration(LinkReceiveStats_OneDuration *pOneDuration, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	xcase FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		if (pOneDuration->iCount)
		{
			pOneDuration->iAverageSize = pOneDuration->iTotalSize / pOneDuration->iCount;
		}
	}

	return 1;
}

//this needs to exist so that the FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED can recurse properly from a linkReceiveStats down to a 
//LinkReceiveStats_SizeCounts
AUTO_FIXUPFUNC;
TextParserResult fixupLinkReceiveStats_PerCommand(LinkReceiveStats_PerCommand *pPerCommand, enumTextParserFixupType eType, void *pExtraData)
{
	fixupLinkReceiveStats_SizeCounts(&pPerCommand->perCommandTotals, eType, pExtraData);

	return 1;
}

			

bool gbTrackLinkReceiveStats = false;
AUTO_CMD_INT(gbTrackLinkReceiveStats, TrackLinkReceiveStats) ACMD_COMMANDLINE;

