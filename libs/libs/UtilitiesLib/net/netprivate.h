#ifndef _NETPRIVATE_H
#define _NETPRIVATE_H
#pragma once
GCC_SYSTEM

#include "net.h"
#include "sock.h"
#include "globalComm.h"

#ifdef _IOCP
    #error
#endif

#if !PLATFORM_CONSOLE
    #define _IOCP 1
#endif

#define LINK_PROTOCOL_VER 2

#define LINK_COMPRESS_LEVEL 4

// NOTE: Bruce will cut off the fingers of anyone that changes this. This means you. <NPK 2009-04-16>
#define DEFAULT_SEND_TIMEOUT 0.0f

#define NET_VERBOSE_PRINTING 0

#define NET_DEBUG_RING_BUFFER_SIZE (1024*1024)

// Defines for the sizes of header data in individual packets
// All packets have a header of at least 4 bytes, containing the total size of that packet (inclusive of all header bytes)
// Packets on links with LINK_CRC have an additional "CRC header" of 8 additional bytes for the CRC and sequence ID
#define NET_PACKET_SIZE_BYTES 4
#define NET_PACKET_CRC_BYTES 4
#define NET_PACKET_SEQ_ID_BYTES 4
#define NET_PACKET_CRC_HEADER_SIZE (NET_PACKET_CRC_BYTES + NET_PACKET_SEQ_ID_BYTES)
#define NET_LINK_PACKET_HEADER_SIZE(link) (NET_PACKET_SIZE_BYTES + ((link)->flags & LINK_CRC ? NET_PACKET_CRC_HEADER_SIZE : 0))

#define	COMPRESS_ALLOC_THRESHOLD 8000
#define UNCOMPRESS_ALLOC_THRESHOLD 16384

typedef struct NetComm NetComm;
typedef struct NetListen NetListen;
typedef struct NetLink NetLink;
typedef struct Packet Packet;
typedef struct rc4_key rc4_key;
typedef struct z_stream_s z_stream;
typedef struct WorkerThread WorkerThread;
typedef struct LinkToMultiplexer LinkToMultiplexer;
typedef struct QueueImp QueueImp;
typedef QueueImp *Queue;
typedef struct RingStreamImpl *RingStream;

typedef struct LinkFileSendingMode_SendManager LinkFileSendingMode_SendManager;
typedef struct LinkFileSendingMode_ReceiveManager LinkFileSendingMode_ReceiveManager;

typedef struct LinkReceiveStats LinkReceiveStats;


typedef struct ReceivedTPICacheEntry
{
	ParseTable *pLocalTPI;
	ParseTable **ppReceivedTPIs;
} ReceivedTPICacheEntry;


typedef struct NetCommDebug
{
	U32			test_disconnect : 1;
	int			timer;
	F32			disconnect_seconds;
	int			disconnect_random;
} NetCommDebug;

typedef struct NetCommSendThread {
	WorkerThread*	wt;
	U8*				send_buffer;
	U32				send_buffer_used;
	U32				send_buffer_size;
} NetCommSendThread;

typedef struct NetComm
{
	U32					createdInThreadID;
	#ifdef _IOCP
		HANDLE			completionPort;
	#endif
	NetListen**			listen_list;
	NetLink**			remove_list;
	NetLink**			flushed_disconnects;

	int					timeout_msecs;
	NetCommSendThread**	send_threads;
	int					send_thread_idx;
	U32					msPeriodicStartTime;
	U32					monitoring : 1;		// commMonitor is not reentrant
	const char*			monitoring_file;
	int					monitoring_line;
	U32					monitoring_threadid;
	U32					no_send_thread;
	NetCommDebug		debug;
	F32					send_timeout; // timeout value (seconds) of a single send (closes socket and fails)
	U32					packet_receive_msecs; //max msecs that will be spent in packet_receive (0 = default, which is 100)
	U32					proxy_host;
	U16					proxy_port;
	U32					last_check_ms;
	U32					last_force_frame_ms;
} NetComm;

// this struct is used for listen servers, but also for client-style links.
// you can tell it's a client if listen_sock is zero
typedef struct NetListen
{
	void			*userData;
	LinkCallback	*connect_callback;
	LinkCallback	*disconnect_callback;
	PacketCallback	*message_callback;
	int				port;
	int				user_data_size;
	SOCKET			listen_sock;
	NetComm			*comm;
	NetLink			**links;
	NetLink			**linksWithLaggedPackets;
	LinkFlags		required_flags;
	LinkFlags		unallowed_flags;
	U32				bound_ip;
	LinkType		eLinkType;
	const char		*creationFile;
	int				creationLine;

	LinkReceiveStats *pReceiveStats;
} NetListen;

typedef struct NetCompressDebug {
	z_stream*		sendStart;
	U8*				buffer;
	U32				bufferSize;
	U32				bufferPos;
} NetCompressDebug;

typedef struct NetCompress
{
	z_stream			*send;
	z_stream			*send_verify;
	RingStream			debug_send_ring;
	z_stream			*recv;
	RingStream			debug_recv_ring;
	NetCompressDebug	debug[2];
	U32					curDebug;
	S32					debugIsAllocated;
	bool				lastPacketNoCompress;
} NetCompress;

typedef struct NetEncrypt
{
	U32				private_key[512/32];
	U32				public_key[512/32];
	U32				shared_secret[512/32];
	rc4_key			*encode;
	rc4_key			*decode;
} NetEncrypt;

typedef enum LinkType LinkType;

AUTO_ENUM;
typedef enum ClientType
{
	CLIENT_TYPE_PC,
	CLIENT_TYPE_XBOX,
	CLIENT_TYPE_PS3,
} ClientType;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "DebugName, Type, outbox, full_count, curr_sendbuf, stats.send.bytes, stats.recv.bytes");
typedef struct NetLink
{
	struct sockaddr_in	addr; NO_AST
	SOCKET			sock; NO_AST
	LinkType		eType;
	LinkType		ePushedType;
	LinkFlags		flags; AST(FLAGS)

	U32				ID;			// unique id
	char			IDString[12]; AST(KEY) //same as ID, sprintfed so it can be the key for a resource table
	U32				pak_id;		// constantly increasing packet id
	U32				protocol : 4;
	U32				connected : 1;
	U32				disconnected : 1; AST(NAME(Disconnected))
	U32				user_disconnect : 1;	// the user code has been or will be notified that the link is disconnected
	U32				listening : 1;		// flag for the first packet ever queued in IOCP mode. only set for that.
	U32				no_compress : 1;
	U32				cleared_user_link_ptr : 1;
	U32				no_timeout : 1;
	U32				auto_ping : 1;
	U32				lost_user_data_ownership : 1;
	U32			    notify_on_disconnect : 1;
	U32				not_trustworthy : 1; // whoever is on the other side of this link may well have bad RAM or even be a hacker or something. Set a flag on all packets gotten from this link.
	U32				called_linkFree : 1;

	U32				trans_server_link_to_multiplerxer : 1; //this link on the trans server is to a multiplexer

	U32				sent_sleep_alert; //the first time a link sleeps, it sends an alert. This flag tracks that.
	U32				sendbuf_full;
	U32				sendbuf_was_full;  // flagged when "goto retry" fires in netSendSafe, manually cleared
	U32			    shuttingDown; // This has to be its own U32, not a bitfield.
	
	int				sentResizeAlert; //starts at zero. When the first resize alert is sent, goes up to 1, etc.

	NetListen		*listen; NO_AST
	Packet			*recv_pak; NO_AST
	Packet			*send_queue_head; NO_AST
	Packet			*send_queue_tail; NO_AST
	U64				send_pak_bytes; NO_AST
	void			*user_data; NO_AST
	void			*non_owned_user_data; NO_AST //a pointer the user can set and clear but which the net system never allocates
	LinkToMultiplexer *link_to_multiplexer; NO_AST
	NetEncrypt		*encrypt; NO_AST
	NetCompress		*compress; NO_AST
	char			*error;
	F32				timeout;
	U32				keep_alive_interval_seconds;
	int				keep_alive_prev_milliseconds;
	U32				ping_recv_count;
	U32				ping_ack_recv_count;
	U32				compressed_bytes_recv_since_ping_ack;
	U32				uncompressed_bytes_recv_since_ping_ack;
	int				max_sendbuf;
	int				max_recvbuf;
	U32				bytesWaitingToRecv;
	int				curr_sendbuf;
	int				curr_recvbuf;
	U32				wsaRecvResult;
	U32				wsaRecvError;
	U32				sendBytesSent;
	U32				sendError;
	U32				noMoreSending; // socket is dead, just let the recv detect it.
	int				full_count;
	volatile int	outbox;
	int				flush_limit;
	NetCommSendThread*	send_thread; NO_AST
	LinkStats		stats; 
	Queue			pkt_lag_queue; NO_AST
	int				lag;			// amount of fake delay (in msecs) to delay sending the packet
	int				lag_vary;		// amount of msecs to vary delaying sending the packet
	int				raw_data_left;  // amount of raw data left to be read from the socket (flagged in LINK_HTTP mode)
	U32				recv_seq_id;	// used by LINK_CRC to verify TCP packets are coming in order
	U32				sent_seq_id;	// used by LINK_CRC to verify TCP packets are coming in order

	U32				disconnect_timer;// time used by flushed_disconnects to determine when to close the link
	const char		*disconnect_reason; //brief string explaining why this link was disconnected... for debugging purposes

	int				deliberate_data_corruption_freq; //if non-zero, then when a packet is received, bits will be corrupted, with the spacing
													   //between corrupted bits being a random number from 0 to this amount
	int				deliberate_packet_disconnect; //if non-zero, disconnect after this many packets have been sent
	int				pktsize_max;	// no packet should be bigger than this

	U32				default_pktsize;	// for auto sizing at pktCreate

	const char		*creationFile;
	int				creationLine;

	U32				received_abort_count;
	
	char			debugName[128];

	// Debugging stream ring buffers
	// There are more ring buffers in compress.
	RingStream		debug_early_send_ring; NO_AST	// At the end of pktCompress()
	RingStream		debug_late_send_ring; NO_AST	// Just after send()
	RingStream		debug_recv_ring; NO_AST			// Right before pktUncompress()

	char			*estr_send_capture_buf; NO_AST	// If set, all outgoing traffic on this link is captured in this EString.

	//will be NULL on most links. Used for ParserSendStructSafe and RecvStructSafe
	ParseTable **ppSentTPIs; NO_AST
	ReceivedTPICacheEntry **ppReceivedTPIs; NO_AST

	U32				proxy_true_host;
	U16				proxy_true_port;

	// The client type for this link
	ClientType		eClientType;

	LinkFileSendingMode_SendManager *pLinkFileSendingMode_SendManager; NO_AST
	LinkFileSendingMode_ReceiveManager *pLinkFileSendingMode_ReceiveManager; NO_AST

	LinkSleepCallBack sleep_cb; NO_AST
	LinkResizeCallBack resize_cb; NO_AST

	LinkReceiveStats *pReceiveStats;
} NetLink;

//internal commands for LINKFILESENDINGMODE
enum
{
	LINKFILESENDINGMODE_CMD_BEGINSEND = 1,
	LINKFILESENDINGMODE_CMD_UPDATESEND = 2,
	LINKFILESENDINGMODE_CMD_CANCELSEND = 3,

	LINKFILESENDINGMODE_CMD_CHECK_FOR_FILE_EXISTENCE = 4,
	LINKFILESENDINGMODE_CMD_FILE_EXISTENCE_REPORT = 5,
};

//private packet commands... note that these use the same command field as the normal packet commands, limiting how many
//"real" commands are available. Note that 220 through 240 are the "shared" commands, for things like multiplexing. See 
//globalComm.h
enum
{
	PACKETCMD_INTERNAL_UNUSED_240 = 240,
	PACKETCMD_INTERNAL_UNUSED_241 = 241,
	PACKETCMD_INTERNAL_UNUSED_242 = 242,
	PACKETCMD_INTERNAL_UNUSED_243 = 243,
	PACKETCMD_INTERNAL_UNUSED_244 = 244,
	PACKETCMD_INTERNAL_UNUSED_245 = 245,
	PACKETCMD_INTERNAL_UNUSED_246 = 246,
	PACKETCMD_INTERNAL_UNUSED_247 = 247,
	PACKETCMD_INTERNAL_UNUSED_248 = 248,
	PACKETCMD_INTERNAL_UNUSED_249 = 249,
	PACKETCMD_INTERNAL_UNUSED_250 = 250,
	PACKETCMD_DISCONNECT = 251,
	PACKETCMD_CRC = 252,
	PACKETCMD_LAG = 253,
	PACKETCMD_PINGACK = 254,
	PACKETCMD_PING = 255, 
};

#define FIRST_INTERNAL_PACKET_CMD PACKETCMD_INTERNAL_UNUSED_240



#define NUM_TRACKER_BUCKETS 16

AUTO_STRUCT;
typedef struct PacketTrackerBucket
{
	int iCurCount;
	int iMaxCount;
	S64 iTotalSent;
} PacketTrackerBucket;





AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "TotalCreated, TotalBytesSent");
typedef struct PacketTracker
{
	char *pDescriptiveName; AST(ESTRING KEY)
	S64 iTotalCreated;
	S64 iTotalSent;
	S64 iTotalFreed;
	S64 iTotalBytesSent; AST(FORMATSTRING(HTML_BYTES=1))

	S64 iLargestPacket; AST(FORMATSTRING(HTML_BYTES=1))
	U32 iLargestPacketTime; AST(FORMATSTRING(HTML_SECS_AGO=1))

	int iCurCount;
	int iMaxCount;

	PacketTrackerBucket buckets[NUM_TRACKER_BUCKETS]; AST(INDEX(0, le512bytes) INDEX(1, le1K) INDEX(2, le2K) INDEX(3, le4K) INDEX(4, le8K) INDEX(5, le16K) INDEX(6, le32K) INDEX(7, le64K) INDEX(8, le128K) INDEX(9, le256K) INDEX(10, le512K), INDEX(11, le1M) INDEX(12, le2M) INDEX(13, le4M) INDEX(14, le8M) INDEX(15, gt8M))
} PacketTracker;

typedef struct Packet
{
	U32			id;
	U8			*data;
	int			idx;
	int			size;
	union
	{
		const int			max_size;
		int max_size_mutable;
	};
	MEM_DBG_STRUCT_PARMS
	const char* ol_queued_loc_name;
	U32			msTimeWhenPktSendWasCalled;
	U32			has_verify_data : 1;
	U32			ol_queued : 1;
	U32			receiving : 1;
	U32			sendable : 1;			 // created using pktCreate
	U32			no_compress : 1;		// copied from link, for thread safety
	U32			is_static : 1;
	U32			is_in_pkt_heap : 1;
	U32			assert_on_error : 1;
	U32			error_occurred : 1; //an error of some sort occurred on this packet. Usually an overrun, also possibly a bad float
	U32			created_with_set_payload : 1; //if true, data is not owned by the packet system
	Packet*		nextInSendThread;
	NetLink		*link;
	PacketTracker *pTracker;
	OVERLAPPED	ol;
} Packet;

typedef struct
{
	U32		time;
	Packet*	pkt;
	S32		flush;
} LaggedPacket;

extern int g_force_sockbsd;

int netSafeSend(NetLink* link, SOCKET sock, const U8* data, U32 size);
void linkSendLaggedPackets(NetLink *link,int force_flush);
void linkUserConnect(NetLink* link);
void linkUserDisconnect(NetLink* link);
void linkQueueRemove(NetLink *link, const char *disconnect_reason);

void linkSetDisconnectReason(NetLink* link, const char* reason);

// If LINK_CRAZY_DEBUGGING, push to debugging ring buffer.
void pktPushRing(Packet *pkt, RingStream *ring, const char *data, size_t size);
	
#if NET_VERBOSE_PRINTING
	void linkVerbosePrintf(NetLink* link, FORMAT_STR const char* format, ...);
	void commVerbosePrintf(NetComm* comm, FORMAT_STR const char* format, ...);
#else
	#define linkVerbosePrintf(...)
	#define commVerbosePrintf(...)
#endif


void TrackerReportPacketSend(Packet *pPak);
void TrackerReportPacketFree(Packet *pPak);
void TrackerReportPacketCreate(Packet *pPak);
void TrackerReportPacketResize(Packet *pPak, int iPrevoiusMaxSize);

void linkFileSendingMode_DestroySendManager(LinkFileSendingMode_SendManager *pManager);
void linkFileSendingMode_DestroyReceiveManager(LinkFileSendingMode_ReceiveManager *pManager);
void linkFileSendingMode_Receive_Disconnect(NetLink *pLink);
void linkFileSendingMode_Send_Disconnect(NetLink *pLink);

//linkReceiveStats tracks all packets received on each link, and generates a bunch of useful
//servermonitorable data

extern bool gbTrackLinkReceiveStats;

//make sure to only call this if gbTrackLinkReceiveStats is true, but because we need to do the timing before and after
//the callback, it's hard to make a macro to do it automatically
void linkUpdateReceiveStats(NetLink *pLink, int iCmd, int iSize, S64 iTicks);


//we store counts by power of 2, indexed by the index of the highest set bit
//in the size. So 64 bytes through 127 bytes have index 7
AUTO_STRUCT;
typedef struct LinkReceiveStats_OneSize
{
	const char *pName; AST(POOL_STRING)
	S64 iCount;
	S64 iSize; AST(FORMATSTRING(HTML_BYTES=1))
	S64 iDuration; AST(FORMATSTRING(HTML_TICKS_DURATION = 1))
	S64 iAverageDuration; AST(FORMATSTRING(HTML_TICKS_DURATION = 1))
} LinkReceiveStats_OneSize;

AUTO_STRUCT;
typedef struct LinkReceiveStats_OneDuration
{
	const char *pName; AST(POOL_STRING)
	S64 iCount;
	S64 iTotalDuration; AST(FORMATSTRING(HTML_TICKS_DURATION = 1))
	S64 iTotalSize; AST(FORMATSTRING(HTML_BYTES=1))
	S64 iAverageSize;  AST(FORMATSTRING(HTML_BYTES=1))
} LinkReceiveStats_OneDuration;

AUTO_STRUCT;
typedef struct LinkReceiveStats_SizeCounts
{
	S64 iTotalCount;
	S64 iTotalSize; AST(FORMATSTRING(HTML_BYTES=1))
	S64 iTotalDuration; AST(FORMATSTRING(HTML_TICKS_DURATION = 1))
	S64 iAverageDuration; AST(FORMATSTRING(HTML_TICKS_DURATION = 1))

	S64 iMaxSize; AST(FORMATSTRING(HTML_BYTES=1))
	S64 iMaxDuration;  AST(FORMATSTRING(HTML_TICKS_DURATION = 1))

	LinkReceiveStats_OneSize **ppBySizeGroups; //sparse array indexed by bit index
	LinkReceiveStats_OneDuration **ppByDurationGroups;
} LinkReceiveStats_SizeCounts;

AUTO_STRUCT;
typedef struct LinkReceiveStats_PerCommand
{
	int iCommandNum; AST(KEY)
	char *pCommandName; //if a staticDefine is specified, this is a string, otherwise it's just %d of the int

	LinkReceiveStats_SizeCounts perCommandTotals; AST(EMBEDDED_FLAT)
} LinkReceiveStats_PerCommand;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "overall.TotalCount, overall.TotalSize");
typedef struct LinkReceiveStats
{
	//will contain information about whether this is live data, or a delta, or what. Filled in at serverMonitor fixup time
	char *pComment; AST(ESTRING)

	//the stats for each link in a netListen always report up to the parent as well
	struct LinkReceiveStats *pParent; NO_AST
	struct LinkReceiveStats **ppGroups; NO_AST
	char *pName; AST(KEY)
	StaticDefineInt *pCommandNames; NO_AST
	LinkReceiveStats_PerCommand **ppCommandStats;
	LinkReceiveStats_SizeCounts overall;

	U32 iFirstDataTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	U32 iCurDeltaStartTime; NO_AST

	AST_COMMAND("Begin getting a delta for ALL LinkReceiveStats", "ReceiveStats_CaptureDelta $INT(How many seconds of data, 0 to cancel)")
} LinkReceiveStats;




#endif
