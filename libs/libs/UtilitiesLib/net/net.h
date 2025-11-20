#ifndef _NET_H
#define _NET_H
#pragma once
GCC_SYSTEM

#define COMM_MAX_CMD 0
#define MAX_IP_STR 16

#if _PS3
	#define SOCKET_ERROR (-1)
#endif

#define LOOPBACK_IP ((U32)0x0100007F)

/*each netlink has a "type" for each direction. Effectively, each netSend has type. These types
control what the netsend does when its send buffer overflows. This means one of two things:
(1) the sender is sending data faster than the receiver can handle it.
(2) a single packet (or group of packets) was sent which was larger than the buffer (often, for instance,
if a bunch of information is sent at startup, or something like that).

There are three basic ways to deal with this situation:
(1) resize the send buffer so that it's larger
(2) wait until the send buffer empties out
(3) close the link.

Each of the different types of links do different sets of those three things, and there are different "sizes"
of types, also. And bear in mind that each "side" of a net link can have a different type.

The link enum is made up of several different flags that can be OR'd together, and a bunch of predefined
useful types
*/

#define LINKTYPE_SIZE_SHIFT 8
#define LINK_RESIZES_WITH_WARNING_AFTER_MAX 3

AUTO_ENUM;
typedef enum LinkType
{
	//if this flag is true, the link will disconnect upon full send buffer (after, potentially,
	//doing all resizing). 
	LINKTYPE_FLAG_DISCONNECT_ON_FULL = (1 << 0),
	
	//If this is true, then the link will sleep when the 
	//send buffer is full, potentially blocking one or more entire cores	
	LINKTYPE_FLAG_SLEEP_ON_FULL = (1 << 1),

	//NOTE: each legal link type MUST have one and only one of the above two choices (yes
	//this is redundant, as the absence of one could imply the other, but I want to be super-sure
	//that people are consciously choosing which type of link to use.


	//if this flag is true, the link will resize up to LINK_RESIZES_WITH_WARNING_AFTER_MAX times after being "full" the first time,
	//and send out alerts and errors when it does so. Most inside-shard links should have this
	LINKTYPE_FLAG_RESIZE_AND_WARN = (1 << 3),

	//if this flag is true, then don't send alerts when the link buffer is full and sleeping would occur
	LINKTYPE_FLAG_NO_ALERTS_ON_FULL_SEND_BUFFER = (1 << 4),

	//if this flag is true, then the final resize warning, and the actual disconnect, generate critical alerts
	LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT = (1 << 5),

	//the starting "max size" of the send buffer
	LINKTYPE_SIZE_500K = (1 << LINKTYPE_SIZE_SHIFT),
	LINKTYPE_SIZE_1MEG = (2 << LINKTYPE_SIZE_SHIFT),
	LINKTYPE_SIZE_2MEG = (3 << LINKTYPE_SIZE_SHIFT),
	LINKTYPE_SIZE_5MEG = (4 << LINKTYPE_SIZE_SHIFT),
	LINKTYPE_SIZE_10MEG = (5 << LINKTYPE_SIZE_SHIFT),
	LINKTYPE_SIZE_20MEG = (6 << LINKTYPE_SIZE_SHIFT),
	LINKTYPE_SIZE_100MEG = (7 << LINKTYPE_SIZE_SHIFT),

//generally, use these types:

//a reasonable default
	LINKTYPE_DEFAULT = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_1MEG,

//for very high priority links that can not go down no matter what, sleep if you must. Ie, 
//trans server to object DB, etc. Also for links where the receiver is much more important than
//the sender, ie, links TO the transaction server. This must always be used with a max size
	LINKTYPE_SHARD_CRITICAL_500K = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_500K,
	LINKTYPE_SHARD_CRITICAL_1MEG = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_1MEG,
	LINKTYPE_SHARD_CRITICAL_2MEG = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_2MEG,
	LINKTYPE_SHARD_CRITICAL_5MEG = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_5MEG,
	LINKTYPE_SHARD_CRITICAL_10MEG = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_10MEG,
	LINKTYPE_SHARD_CRITICAL_20MEG = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_20MEG,
	LINKTYPE_SHARD_CRITICAL_100MEG = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_100MEG,

//for links which are not critical, because if the receiver can't handle the data, it's probably
//stalled or otherwise dead, but we still want to see the resizing happen. 
//For instance, links from the trans server to non-critical servers, 
	LINKTYPE_SHARD_NONCRITICAL_500K = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_500K,
	LINKTYPE_SHARD_NONCRITICAL_1MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_1MEG,
	LINKTYPE_SHARD_NONCRITICAL_2MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_2MEG,
	LINKTYPE_SHARD_NONCRITICAL_5MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_5MEG,
	LINKTYPE_SHARD_NONCRITICAL_10MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_10MEG,
	LINKTYPE_SHARD_NONCRITICAL_20MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_20MEG,
	LINKTYPE_SHARD_NONCRITICAL_100MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_100MEG,

//same as SHARD_NONCRITICAL, except that on disconnect, and on last-resize-before-disconnect, generates a 
//critical alert
	LINKTYPE_SHARD_NONCRITICAL_ALERTS_500K = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_500K,
	LINKTYPE_SHARD_NONCRITICAL_ALERTS_1MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_1MEG,
	LINKTYPE_SHARD_NONCRITICAL_ALERTS_2MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_2MEG,
	LINKTYPE_SHARD_NONCRITICA_ALERTSL_5MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_5MEG,
	LINKTYPE_SHARD_NONCRITICAL_ALERTS_10MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_10MEG,
	LINKTYPE_SHARD_NONCRITICAL_ALERTS_20MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_20MEG,
	LINKTYPE_SHARD_NONCRITICAL_ALERTS_100MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT | LINKTYPE_SIZE_100MEG,



//for connections TO the client from the server or patch server, where you want to specify a reasonably sized
//buffer, then if it is full, just assume that the client is fubared, and we don't want to waste effort
//resizing and/or sleeping. Also for connections from the client to the game server
	LINKTYPE_TOUNTRUSTED_500K = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_500K,
	LINKTYPE_TOUNTRUSTED_1MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_1MEG,
	LINKTYPE_TOUNTRUSTED_2MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_2MEG,
	LINKTYPE_TOUNTRUSTED_5MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_5MEG,
	LINKTYPE_TOUNTRUSTED_10MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_10MEG,
	LINKTYPE_TOUNTRUSTED_20MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_20MEG,
	LINKTYPE_TOUNTRUSTED_100MEG = LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_SIZE_100MEG,

// for links with large file sends, we use the fullness of the send buffer for flow control, so we 
// need a link that will not drop when the buffer is full, and not warn when it immediately resizes
// to its max size. 
	LINKTYPE_USES_FULL_SENDBUFFER = LINKTYPE_FLAG_SLEEP_ON_FULL | LINKTYPE_SIZE_1MEG | LINKTYPE_FLAG_NO_ALERTS_ON_FULL_SEND_BUFFER,

// HTTP server links do large file sends, using the send buffer for flow control.
	LINKTYPE_HTTP_SERVER = LINKTYPE_USES_FULL_SENDBUFFER,


} LinkType;

#define LINKTYPE_UNSPEC LINKTYPE_DEFAULT



typedef struct NetComm NetComm;
typedef struct NetListen NetListen;
typedef struct NetLink NetLink;
typedef struct Packet Packet;
typedef struct LinkToMultiplexer LinkToMultiplexer;

//A PacketTracker tracks stats about how many packets from various sources have existed at various times, etc.
typedef struct PacketTracker PacketTracker;

// FIXME: Make these function pointer type, so that that the parameter type to functions like commListenEx() is actually a function pointer.
typedef void LinkCallback(NetLink* link,void *user_data);
// For Beaconizer! - Adam
typedef int LinkCallback2(NetLink* link, S32 index, void *link_user_data, void *func_data);
typedef void PacketCallback(Packet *pkt,int cmd,NetLink* link,void *user_data);

//this requires a critical section, and is called any time you call pktCreate, so you should call it ahead of time,
//cache the result, and cache the result whenever possible
//
//pComment MUST MUST MUST be a literal or pooled string
PacketTracker *PacketTrackerFind(const char *pFileName, int iLineNum, const char *pComment);

AUTO_ENUM;
typedef enum
{
	LINK_RAW				= BIT(0),	// don't use any of the fancy Cryptic packet code, just get the data
	LINK_PACKET_VERIFY		= BIT(1),
	LINK_COMPRESS			= BIT(2),
	LINK_ENCRYPT			= BIT(3),
	LINK_FORCE_FLUSH		= BIT(4),
	LINK_ENCRYPT_OPTIONAL	= BIT(5),	// requires LINK_ENCRYPT; listener-only; commListen should instead just take an optional_flags, but I don't want to touch that much code...
	LINK_FAKE_LINK			= BIT(6),
	LINK_NO_COMPRESS		= BIT(7),
	LINK_CRC				= BIT(8),
	LINK_HTTP				= BIT(9),	// use http-style packet wrapping (forces LINK_RAW to also be on)
	LINK_ALLOW_XMLRPC		= BIT(10),	// If the first packet looks HTTP-ish, reflag/repurpose the link for XMLRPC; listener-only
	LINK_REPURPOSED_XMLRPC	= BIT(11),	// First packed looked HTTP-ish, came from a LINK_ALLOW_XMLRPC listen
	LINK_SMALL_LISTEN		= BIT(12),	// [single expected connection] Make a listen port with a tiny incoming client queue for ports expecting only a single client, avoids ruining non-paged memory
	LINK_MEDIUM_LISTEN		= BIT(13),	// [small queue of connections] Make a listen port with a tiny incoming client queue for ports expecting only a few clients, avoids ruining non-paged memory
	LINK_WAITING_FOR_SOCKS	= BIT(14),	// Waiting for a SOCKS ack
	LINK_SENT_SOCKS_REQ		= BIT(15),	// Sent a SOCKS request
	LINK_CRAZY_DEBUGGING	= BIT(16),	// Additional link debugging checks, possibly expensive
	LINK_FLUSH_PING_ACKS	= BIT(17),	// Ping responses should be flushed; used when we actually need keep-alive when the link goes idle for prolonged periods of time
} LinkFlags;

// Maximum size of a decimal-formatted IPv4 address string, including null terminator
#define IPV4_ADDR_STR_SIZE 16

// packet commands
Packet *pktCreateRawSize_dbg(NetLink *link, U32 bufferSize MEM_DBG_PARMS);
#define pktCreateRawSize(link, bufferSize) pktCreateRawSize_dbg((link), bufferSize MEM_DBG_PARMS_INIT)
Packet *pktCreateRaw_dbg(NetLink *link MEM_DBG_PARMS);
#define pktCreateRaw(link) pktCreateRaw_dbg((link) MEM_DBG_PARMS_INIT)
Packet *pktCreateRawForReceiving_dbg(NetLink *link MEM_DBG_PARMS);
#define pktCreateRawForReceiving(link) pktCreateRawForReceiving_dbg((link) MEM_DBG_PARMS_INIT)
Packet *pktCreateSize_dbg(NetLink *link,U32 bufferSize,int cmd, PacketTracker *pTracker, const char *file,int line);
#define pktCreateSize(link, bufferSize, cmd) pktCreateSize_dbg((link), bufferSize, (cmd), NULL MEM_DBG_PARMS_INIT)
Packet *pktCreate_dbg(NetLink *link,int cmd, PacketTracker *pTracker, const char *file,int line);
#define pktCreate(link, cmd) pktCreate_dbg((link), (cmd), NULL MEM_DBG_PARMS_INIT)
#define pktCreateWithTracker(link, cmd, pTracker) pktCreate_dbg((link), (cmd), (pTracker) MEM_DBG_PARMS_INIT)
Packet *pktDup(Packet *orig,NetLink *new_link);

#define pktCreateWithCachedTracker(pkt, link, cmd) { static PacketTracker *__pTracker = NULL; if (!__pTracker) { ATOMIC_INIT_BEGIN; __pTracker = PacketTrackerFind(__FILE__, __LINE__, #cmd); ATOMIC_INIT_END; } pkt = pktCreateWithTracker(link, cmd, __pTracker); }

void pktFree(Packet **pakptr);
int pktSend(Packet **pakptr);
int pktSendNoFlush(Packet **pakptr);
int pktSendRaw(Packet **pakptr);
void pktSendBytesRaw(Packet *pkt,const void *bin,int len);

//use this very carefully... things like the verify flag must have been set up properly
int pktSendThroughLink(Packet **pakptr, NetLink *link);

int pktEnd(Packet *pak);
// Sets packet to completely read
void pktSetEnd(Packet *pkt);
void pktSetAssertOnError(Packet* pkt, S32 enabled);
int pktIsDebug(Packet *pkt);
U32 pktLinkID(Packet *pkt);
U32 pktGetReadOrWriteIndex(Packet* pkt);
U32 pktGetSize(Packet *pkt);
void pktSetSendable(Packet* pkt, S32 sendable);
NetLink *pktLink(Packet *pkt);	// returns link this packet is connected to
Packet *pktCreateTemp_dbg(NetLink *link MEM_DBG_PARMS); // for writing to a packet you plan to pktAppend later
#define pktCreateTemp(link) pktCreateTemp_dbg((link) MEM_DBG_PARMS_INIT)
void pktAppend(Packet *dst,Packet *append,int append_idx);
#define pktSendPacket(pak, pktIn) pktAppend((pak), (pktIn), -1)
void pktSetErrorOccurred(Packet *pkt, const char* msg);

U32 pktGetID(Packet* pkt);

//get set the current packet index, ie, "read head" for reading packets
int pktGetIndex(Packet *pkt);
void pktSetIndex(Packet *pkt, int iIndex);

//get set the current packet write index, ie, "read head" for reading packets
int pktGetWriteIndex(Packet *pkt);
void pktSetWriteIndex(Packet *pkt, int iIndex);



void pktSendStringRaw(Packet *pkt,const char *str);
char *pktGetStringRaw(const Packet *pak);
void pktSendBits(Packet *pkt,int numbits,U32 val);
U32 pktGetBits(Packet *pkt,int numbits);
void pktSendBitsAuto(Packet *pkt,U32 val);
U32 pktGetBitsAuto(Packet *pkt);
void pktSendBytes(Packet *pkt,int count,void *bytes);
void pktGetBytes(Packet *pkt,int count,void *bytes);

//returns a pointer to the bytes inside the packet
void *pktGetBytesTemp(Packet *pkt,int count);
void pktSendString(Packet *pkt,const char *str);
void pktSendStringf(Packet *pkt, const char *fmt, ...);
char *pktGetString(Packet *pkt,char *buf,int buf_size);
char *pktGetStringTemp(SA_PARAM_NN_VALID Packet *pkt);
char *pktGetStringTempAndGetLen(Packet *pkt, int *pLen);
SA_RET_OP_STR char *pktMallocString(Packet *pkt);
SA_RET_OP_STR char *pktMallocStringAndGetLen(Packet *pkt, int *pLen);
int pktGetStringLen(Packet *pkt);
void pktSendF32(Packet *pkt,F32 f);
F32 pktGetF32(Packet *pkt);
void pktSendF64(Packet *pkt,F64 f);
F64 pktGetF64(Packet *pkt);
void pktSendBits64(Packet *pkt, int numbits, U64 val);
U64 pktGetBits64(Packet *pkt, int numbits);
void pktSendBool(Packet *pkt, bool val);
bool pktGetBool(Packet *pkt);
void pktSendStruct(Packet *pkt,const void *s,ParseTable pti[]);
void pktSendStructJSON(Packet *pkt, const void *s, ParseTable pti[]);
void *pktGetStruct(Packet *pkt,ParseTable pti[]);
void *pktGetStructFromUntrustedSource(Packet *pkt,ParseTable pti[]);
U32 pktId(Packet *pkt);

//returns number of bytes copied
int pktCopyRemainingRawBytesToOtherPacket(Packet *destPkt, Packet *srcPkt);

int pktGetNumRemainingRawBytes(Packet *pkt);

// Dangerous.  Probably shouldn't use.
void pktReset(Packet *pak);
void pktSetBuffer(Packet *pak, U8* data, int size_in_bytes);
// END DANGER AREA

#define pktGetBitsPack(pkt, num_bits) pktGetBitsAuto(pkt)
#define pktGetU32(pkt) pktGetBits(pkt,32)
#define pktGetU64(pkt) pktGetBits64(pkt, 64)

#define pktSendU64(pkt, val) pktSendBits64(pkt, 64, val)
#define pktSendU32(pkt, val) pktSendBits(pkt,32,val)
#define pktSendBitsPack(pkt, num_bits, val) pktSendBitsAuto(pkt,val)

bool pktIsNotTrustworthy(Packet *pak);

//DO NOT USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING
void pktSetHasVerify(Packet *pak, bool bVerify);

//wrappers for basically using packets simply as bitstreams
Packet *pktCreateTempWithSetPayload(void *pPayload, int bytes);
void *pktGetEntirePayload(Packet *pak, int *pOutBytes);

//these two should be used together. 
//
//send a packet inside another packet
void pktSendEntireTempPacket(Packet *pOuterPacket, Packet *pTempPacket);
//get it out on the other end temporarily. do NOT call pktFree.
Packet *pktCreateAndGetEntireTempPacket(Packet *pPacket);



// net global commands
void netSetSockBsd(int on); // tell net code to only use bsd socket calls, none of the windows IOCP stuff
void netSetXLSP(int on);			// turn on XLSP security mumbo jumbo

// netcomm commands
NetComm *commDefault(void);											// always returns the same netcomm.
NetComm *commCreate(int timeout_msecs,int num_send_threads);	// two netcomms can work in the same thread, but two threads each need their own netcomm
void commMonitorWithTimeout_dbg(NetComm *comm, S32 timeoutOverrideMilliseconds MEM_DBG_PARMS);
#define commMonitorWithTimeout(comm, timeoutOverrideMilliseconds) commMonitorWithTimeout_dbg(comm, timeoutOverrideMilliseconds MEM_DBG_PARMS_INIT)
void commMonitor_dbg(NetComm *comm MEM_DBG_PARMS);								// process incoming data/connects/disconnects
#define commMonitor(comm) commMonitor_dbg(comm MEM_DBG_PARMS_INIT)
bool commIsMonitoring(NetComm *comm);

NetListen *commListenEx(NetComm *comm, LinkType eType, LinkFlags required,int port,PacketCallback msg_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, const char *file, int line);
NetLink *commConnectEx(NetComm *comm, LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback msg_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, char *error, size_t error_size, const char *file, int line);
NetLink *commConnectIPEx(NetComm *comm, LinkType eType, LinkFlags flags,U32 ip,int port,PacketCallback msg_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, char *error, size_t error_size, const char *file, int line);
NetLink *commConnectWaitEx(NetComm *comm, LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size,F32 timeout, const char *file, int line);
NetListen *commListenIpEx(NetComm *comm, LinkType eType, LinkFlags required,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size,U32 ip, const char *file, int line);
int commListenBothEx(NetComm *comm, LinkType eType, LinkFlags required,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size,NetListen **local_p,NetListen **public_p, const char *file, int line);


#define commListen(comm, eType, required, port, msg_cb, connect_cb, disconnect_cb, user_data_size) commListenEx(comm, eType, required, port, msg_cb, connect_cb, disconnect_cb, user_data_size, __FILE__, __LINE__)
#define commConnect(comm, eType, flags,ip, port, msg_cb, connect_cb, disconnect_cb, user_data_size) commConnectEx(comm, eType, flags,ip, port, msg_cb, connect_cb, disconnect_cb, user_data_size, NULL, 0, __FILE__, __LINE__)
#define commConnectIP(comm, eType, flags, ip, port, msg_cb, connect_cb, disconnect_cb, user_data_size) commConnectIPEx(comm, eType, flags, ip, port, msg_cb, connect_cb, disconnect_cb, user_data_size, NULL, 0, __FILE__, __LINE__)
#define commConnectWait(comm, eType, flags,ip, port, packet_cb, connect_cb, disconnect_cb, user_data_size, timeout) commConnectWaitEx(comm, eType, flags,ip, port, packet_cb, connect_cb, disconnect_cb, user_data_size, timeout, __FILE__, __LINE__)
#define commListenIp(comm, eType, required, port, packet_cb, connect_cb, disconnect_cb, user_data_size, ip) commListenIpEx(comm, eType, required, port, packet_cb, connect_cb, disconnect_cb, user_data_size, ip, __FILE__, __LINE__)
#define commListenBoth(comm, eType, required, port, packet_cb, connect_cb, disconnect_cb, user_data_size, local_p,public_p) commListenBothEx(comm, eType, required, port, packet_cb, connect_cb, disconnect_cb, user_data_size, local_p,public_p, __FILE__, __LINE__)


//use a little FSM to keep reattempting connection and letting you query the status without blocking
typedef struct CommConnectFSM CommConnectFSM;
typedef enum CommConnectFSMType
{
	COMMFSMTYPE_TRY_ONCE, //try to connect, waiting fWaitTime.
	COMMFSMTYPE_RETRY_FOREVER, //try to connect, waiting fWaitTime. Then just start over again, repeatedly
} CommConnectFSMType;

AUTO_ENUM;
typedef enum CommConnectFSMStatus
{
	COMMFSMSTATUS_SUCCEEDED,
	COMMFSMSTATUS_STILL_TRYING,
	COMMFSMSTATUS_FAILED,
	
	
	//already returned SUCCEEDED or FAILED once, so should rarely be returned since you should usually call commConnectFSMDestroy
	//after getting back SUCCEEDED or FAILED
	COMMFSMSTATUS_DONE, 
} CommConnectFSMStatus;
extern StaticDefineInt CommConnectFSMStatusEnum[];

typedef void (*NetErrorReportingCB)(void *pUserData, char *pErrorString);

CommConnectFSM *commConnectFSMEx(CommConnectFSMType eFSMType, float fWaitTime, 
	NetComm *comm, LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback packet_cb,LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, NetErrorReportingCB pConnectErrorCB, void *pConnectErrorUserData, const char *file, int line);
#define commConnectFSM(eFSMType, fWaitTime, comm, eType, flags, ip, port, packet_cb, connect_cb, disconnect_db, user_data_size, pConnectErrorCB, pConnectErrorUserData) commConnectFSMEx(eFSMType, fWaitTime, comm, eType, flags, ip, port, packet_cb, connect_cb, disconnect_db, user_data_size, pConnectErrorCB, pConnectErrorUserData, __FILE__, __LINE__)
//if status is succeeded, will set the NetLink pointer
CommConnectFSMStatus commConnectFSMUpdate(CommConnectFSM *pFSM, NetLink **ppSuccessfulOutLink);
void commConnectFSMDestroy(CommConnectFSM **ppFSM);

//An easy way to encapsulate all the FSM stuff is to basically say "here's an FSM and a netlink, and I have a tick function, and in that
//tick function, if I am connected, I want to do stuff, otherwise, I want to not do stuff but keep trying to reconnect. To do that, 
//just call this function... if it returns true, then you are connected, if not you are not
bool commConnectFSMForTickFunctionWithRetryingEx(CommConnectFSM **ppFSM, NetLink **ppLink, char *pDebugNameToSet, float fWaitTime, 
	NetComm *comm, LinkType eType, LinkFlags flags,const char *ip,int port,PacketCallback packet_cb,
	LinkCallback connect_cb,LinkCallback disconnect_cb,int user_data_size, NetErrorReportingCB pErrorCB, void *pErrorCBUserData, 
	NetErrorReportingCB pDisconnectionCB, void *pDisconnectionCBUserData, const char *file, int line);

#define commConnectFSMForTickFunctionWithRetrying(ppFSM, ppLink, pDebugNameToSet, fWaitTime, comm, eType, flags, ip, port, packet_cb, connect_cb, disconnect_cb, user_data_size, pErrorCB, pErrorCBUserData, pDisonnectionCB, pDisconnectionCBUserData) \
	commConnectFSMForTickFunctionWithRetryingEx(ppFSM, ppLink, pDebugNameToSet, fWaitTime, comm, eType, flags, ip, port, packet_cb, connect_cb, disconnect_cb, user_data_size, pErrorCB, pErrorCBUserData, pDisonnectionCB, pDisconnectionCBUserData, __FILE__, __LINE__)

void commDestroy(NetComm ** comm); // The do-nothing, pretends it's destroying function

int commCountOpenSocks(NetComm *comm);


//waits until all comms are flushed and closed, if possible
void commFlushAndCloseAllComms(float fTimeout);

// Return true if commFlushAndCloseAllComms() has been called.
bool commFlushAndCloseAllCommsCalled(void);

void commFlushAllLinks(NetComm *comm);							// call linkFlush on every link
void commFlushAndCloseAllLinks(NetComm *comm);							// call linkFlush on every link
// netcomm debug commands
void commTimedDisconnect(NetComm *comm,F32 seconds);
void commRandomDisconnect(NetComm *comm, int random);

void commSetSendTimeout(NetComm *comm, F32 seconds);
void commSetMinReceiveTimeoutMS(NetComm* comm, U32 ms);
void commSetPacketReceiveMsecs(NetComm* comm, U32 ms);

// Disable packet verify data forcibly
void commDefaultVerify(S32 enabled);

// Use a SOCKS proxy for links made on this comm
void commSetProxy(NetComm* comm, const char *host, U16 port);
bool commIsProxyEnabled(NetComm* comm);

// netlisten commands
int listenCount(NetListen *listen);
void *listenGetUserData(NetListen *listen);
void listenSetUserData(NetListen *listen, void *userData);
void listenSetRequiredFlags(NetListen *listen, LinkFlags flags);  // Set the required flags for future NetLinks created by this NetListen.

void listenInitReceiveStats(NetListen *listen, char *pDebugName, StaticDefineInt *pCommandNames);

void commEnableForcedSendThreadFrames(NetComm* comm, S32 enabled);

// NetLink commands
// information on link status

AUTO_STRUCT;
typedef struct LinkDir
{
	U64		bytes;				// uncompressed byte count
	U64		real_bytes;			// actual number sent, after compression
	U32		packets;			// virtual packets
	U32		real_packets;		// actual number put on the wire
	U32		last_time_ms;		// last time a packet was processed
} LinkDir;


typedef struct PacketHistory
{
	U32		real_id;
	U32		time;
	F32		elapsed;
	U32		sent_bytes;
	U32		curr_recv;
	U32		curr_real_recv;
	U32		curr_sent;
	U32		curr_real_sent;
} PacketHistory;

AUTO_STRUCT;
typedef struct LinkStats
{
	LinkDir			send;
	LinkDir			recv;
	PacketHistory	history[64]; NO_AST
	int				history_idx;
	int				last_recv_idx;
} LinkStats;

const LinkStats* linkStats(const NetLink* link);

// Only use linkRemove for times when you don't want to send data before removing
#define linkRemove(link) linkRemove_wReason(link, __FUNCTION__)
void linkRemove_wReason(NetLink **link, const char *disconnect_reason); // This will not flush packets			

void linkFlushAndClose(NetLink **linkInOut, const char *disconnect_reason); // flush all outgoing traffic, then close then link
#define linkShutdown(link) linkFlushAndClose(link, __FUNCTION__)

int linkConnected(NetLink *link);
int linkDisconnected(NetLink *link);
F32 linkRecvTimeElapsed(NetLink *link);			// how long in seconds since we've received a packet
void linkSetTimeout(NetLink *link,F32 seconds);	// link will disconnect if no packet received in the time given
void linkSetKeepAlive(NetLink *link); // Send a keep alive ping every so often to avoid timeouts.
void linkSetKeepAliveSeconds(NetLink *link,U32 seconds); // Send a keep alive ping every seconds seconds often to avoid timeouts.
void linkNoWarnOnResize(NetLink *link);
void linkIterate(NetListen *listen,LinkCallback callback); // iterates through all the links in the listen block
void linkIterate2(NetListen *listen, LinkCallback2 callback, void* func_data); // For when you need more data
void linkFlush(NetLink *link);						// flush all packets in send buffer
void linkSendKeepAlive(NetLink *link);				// send a ping packet to the peer.

// turn compression on or off (link must start with compression on)
// WARNING: Using this feature can cause spurious crashes in zlib due to what is believed to be a zlib bug.
// I recommend against using this feature until someone has analyzed the situation with zlib and discovered
// a workaround or fix.
void linkCompress(NetLink *link,int on);

int linkIsServer(NetLink *link);					// link was connected via listen/accept
void linkFlushLimit(NetLink *link,int max_bytes);	// flush when more than this much packet data is buffered to send
int linkSendBufFull(NetLink *link);				// reports if sendbuf is full
int linkSendBufWasFull(NetLink *link);				// returns "if sendbuf was ever full" flag (manually cleared with linkClearSendBufWasFull)
void linkClearSendBufWasFull(NetLink *link);		// clears "if sendbuf was ever full" flag
void linkCloseOnOverflow(NetLink *link);			// forces link to close if sendbuf is full
int linkConnectWaitNoRemove(NetLink **link, F32 timeout); // busy waits for connect
int linkConnectWait(NetLink **link, F32 timeout);	// busy waits for connect, NULLs link if fail
int linkWaitForPacket(NetLink *link,PacketCallback *msgCb,F32 timeout);	// returns true if a packet is received before timeout
U32 linkCurrentPacketBytesWaitingToRecv(NetLink *link);  // for progress measurement of large packets

U32 linkID(NetLink *link);
NetLink *linkFindByID(U32 id);
int linkCompareIP(NetLink *left, NetLink *right);
void linkChangeCallback(NetLink *link,PacketCallback callback); // avoid this.
void *linkGetListenUserData(NetLink *link);
U32 linkGetListenIp(NetLink *link); // ip the commListen is bound to (0 if not bound to specific one)
U32 linkGetIp(NetLink* link);
U32 linkGetPort(NetLink* link);
U32 linkGetListenPort(NetLink* link);
uintptr_t linkGetSocket(NetLink* link);
void linkAutoPing(NetLink *link,int on);
void linkSetUserData(NetLink* link, void * userData);
void *linkGetUserData(NetLink *link);
const char *linkError(NetLink *link);
bool linkErrorNeedsEncryption(NetLink *link);
bool linkWasRepurposedForXMLRPC(NetLink *link);
void linkSetMaxAllowedPacket(NetLink *link,U32 size);
void linkSetMaxRecvSize(NetLink *link, U32 size);
bool linkIsLocalHost(NetLink *link);

int linkPendingSends(NetLink *link);

void linkGetDisconnectReason(NetLink *link, char **ppOutEString);
U32 linkGetDisconnectErrorCode(NetLink *link);

LinkToMultiplexer *linkGetLinkToMultiplexer(NetLink *link);
void linkSetLinkToMultiplexer(NetLink *link, LinkToMultiplexer *pLinkToMultiplexer);

void linkSetIsNotTrustworthy(NetLink *link, bool bSet);
S32 linkIsNotTrustworthy(NetLink *link);
void linkSetDebugName(NetLink *link, char *pDebugName);
char *linkDebugName(NetLink *link);


// Adam - This is to enable use of pkts as bytestreams.  Not recommended and will be removed later.
NetLink* linkCreateFakeLink(void);		// Creates a fake link.
int linkIsFake(NetLink *link);

//tells the link to deliberately corrupt bits that it receives, to test robustness and stuff
//
//it will corrupt random bits, with the distance between them being a random number from 1 to freq
//
//0 means no corruption
void linkSetCorruptionFrequency(NetLink *link, int freq);

// tells the link to automatically disconnect after sending a certain number of packets
void linkSetPacketDisconnect(NetLink *link, int packets);

// MaxH: My first pollution of the glorious new networking code - pulling some info out of the link
//       Maybe these should go in LinkStats.
U32 linkGetSAddr(NetLink* link);
char* linkGetIpStr(NetLink* link, char* buf, int buf_size);
char* linkGetIpPortStr(NetLink* link, char* buf, int buf_size);
char* linkGetIpListenPortStr(NetLink* link, char* buf, int buf_size);

void linkSetType(NetLink *pLink, LinkType eType);
void linkPushType(NetLink *link);
void linkPopType(NetLink *link);
U32 linkGetTotalCount(void);

//can set callback to be called when a link resizes or sleeps... for debugging purposes only

typedef void (*LinkSleepCallBack)(void);
typedef void (*LinkResizeCallBack)(int iNewSize);

void linkSetSleepCB(NetLink *link, LinkSleepCallBack cb);
void linkSetResizeCB(NetLink *link, LinkResizeCallBack cb);

//used by ParserSendStructSafe and ParserRecvStructSafe to cache which parse tables have already
//been sent/received over a link

typedef enum LinkGetReceivedTableResult
{
	TABLE_NOT_YET_RECEIVED,
	LOCAL_TABLE_IDENTICAL,
	TABLE_RECEIVED_AND_DIFFERENT,
} LinkGetReceivedTableResult;

LinkGetReceivedTableResult LinkGetReceivedParseTable(NetLink *pLink, ParseTable *pLocalTPI, ParseTable ***pppReceivedTPIList);
void LinkSetReceivedParseTable(NetLink *pLink, ParseTable *pLocalTPI, bool bTableWasIdentical, ParseTable ***pppReceivedTPIList);
bool LinkParseTableAlreadySent(NetLink *pLink, ParseTable *pLocalTPI);
void LinkSetParseTableSent(NetLink *pLink, ParseTable *pLocalTPI);

//if you're going to be spending a lot of time looking at linkReceiveStats, you should call this to set up the
//StaticDefine which gives the packet types names
void linkInitReceiveStats(NetLink *pLink, StaticDefineInt *pCommandNames);

//the linkReceiveStats for this link should be grouped together with others into a named group
void linkReceiveStats_AddLinkToNamedGroup(NetLink *pLink, char *pNamedGroup);

// MaxH: Trying to prevent crashes
bool pktCheckNullTerm(const Packet *pak);
bool pktCheckRemaining(const Packet *pak, int bytes);

// Convenience macros for packet checking, mhaider-style.

// Check if it's OK to read a string.
#define PKT_CHECK_STR(PAK, FAIL_LABEL)							\
	do {														\
		if (!pktCheckNullTerm((PAK)))							\
			goto FAIL_LABEL;									\
	} while (0)

// Check if its OK to read N bytes.
#define PKT_CHECK_BYTES(N, PAK, FAIL_LABEL)						\
	do {														\
		if (!pktCheckRemaining((PAK), (N)))						\
			goto FAIL_LABEL;									\
	} while (0)

#endif


//log stats from packet trackers
void LogPacketTrackers(void); 

/*FileSendingMode is a special option for net links which you can use to send
huge blocks of data around when putting it all in a single packet would kill the RAM on the sending or receiving server.

It has to be enabled and set up on both the sending and receiving side.

On the sending side:
(1) call linkFileSendingMode_InitSending(link); Note that the link very likely will need LINKTYPE_FLAG_SLEEP_ON_FULL,
  at least if you're trying to cram data into it in a hurry.
(2) to begin sending a file, call linkFileSendingMode_BeginSendingFile(link, iCmd, pFileName, fileSize). iCmd is the 
  packet cmd that will be received on the far side. pFileName is the filename to be written to disk on the far side. This
  will return a "handle" integer
(3) Then repeatedly call linkFileSEndingMode_SendBytes(link, iHandle, pData, dataSize)
(4) You can optionally call linkFileSendingMode_CancelSend(link, iHandle)
(5) If you want to send a file from disk, blocking until its sent, just call linkFileSendingMode_SendFileBlocking(), which is
  a simple wrapper around (2) and (3)
(6) If you are going to use SENDMANAGED_CHECK_FOR_EXISTENCE, then n your receive cmd function on 
  that link, put this: if (linkFileSendingMode_ReceiveHelper(link, iCmd, pak)) return;


On the receiving side:
(1) call linkFileSendingMode_InitReceiving(link);
(2) For each packet cmd that you might be receiving, call linkFileSendingMode_RegisterCallback(link, iCmd, pRootDir, pCallback). This
  sets it up so that when a file is fully received with that iCmd, you will get the appropriate callback, and the file will be put
  into the specified root dir
(3) In your receive cmd function on that link, put this: if (linkFileSendingMode_ReceiveHelper(link, iCmd, pak)) return;

Note that this stuff is, generally speaking, NOT threadsafe. 
*/

void linkFileSendingMode_InitSending(NetLink *pLink);
int linkFileSendingMode_BeginSendingFile(NetLink *pLink, int iCmd, char *pRemoteFileName, S64 iFileSize);

//returns true if everything worked, false otherwise
bool linkFileSendingMode_SendBytes(NetLink *pLink, int iHandle, void *pData, S64 iDataSize);

void linkFileSendingMode_CancelSend(NetLink *pLink, int iHandle);

//wrapper which does one call to BeginSendingFile and repeated calls to SendBytes. Returns false if the file can't
//be opened/read
bool linkFileSendingMode_SendFileBlocking(NetLink *pLink, int iCmd, char *pLocalFileName, char *pRemoteFileName);


typedef void (*linkFileSendingMode_ReceiveCB)(int iCmd, char *pFileName);
typedef void (*linkFileReceivingMode_ErrorCB)(char *pError);

void linkFileSendingMode_InitReceiving(NetLink *pLink, linkFileReceivingMode_ErrorCB pErrorCB);
void linkFileSendingMode_RegisterCallback(NetLink *pLink, int iCmd, const char *pRootDir, linkFileSendingMode_ReceiveCB pCB);

//returns true if it "stole" the packet, so you can exit from the receieve cmd function
bool linkFileSendingMode_ReceiveHelper(NetLink *pLink, int iCmd, Packet *pPak);


//Managed file sending is a layer on top of all this... you specify a file that you want to send, by filename,
//along with a variety of callbacks, and packets to send on success/failure, and then it gets sent bit by bit during a tick function
//
//returns true if file sending has begun
typedef void (*ManagedFileSendCB)(char *pFileName, char *pErrorString, void *pUserData);

AUTO_ENUM;
typedef enum ManagedSendFlags
{
	SENDMANAGED_CHECK_FOR_EXISTENCE = 1 << 0, // do an extra handshake with a crc check to see if the file
		//already exists. If it does, then act as if the send succeeded. Note that you should NOT use this
		//on enormous files if you are concerned about performances.
} ManagedSendFlags;

bool linkFileSendingMode_SendManagedFile(NetLink *pLink, int iCmd, ManagedSendFlags eFlags, char *pLocalFileName, char *pRemoteFileName, U32 iBytesPerTick, 
	ManagedFileSendCB pErrorCB, void *pErrorCBUserData,
	ManagedFileSendCB pSuccessCB, void *pSuccessCBUserData, 
	Packet *pSuccessPacket);

bool linkFileSendingMode_linkHasActiveManagedFileSend(NetLink *pLink);

bool linkFileSendingMode_SendMultipleManagedFiles(NetLink *pLink, int iCmd, ManagedSendFlags eFlags, 

	//must have an even size... [0] is a local filename, [1] the accompanying remote, [2] local, [3] remote, etc.
	char **ppLocalAndRemoteFileNames, 
	
	U32 iBytesPerTick, 
	ManagedFileSendCB pErrorCB, void *pErrorCBUserData,
	ManagedFileSendCB pSuccessCB, void *pSuccessCBUserData, 
	Packet *pSuccessPacket);



//only necessary if you are using SendManagedFile
void linkFileSendingMode_Tick(NetLink *pLink);

//returns a debug string showing all files currently being sent and their progress
char *netGetFileSendingSummaryString(void);


