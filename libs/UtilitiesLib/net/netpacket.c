#include "../../3rdparty/zlib/zlib.h"
#include "file.h"

#include "sock.h"
#include "net.h"
#include "netprivate.h"
#include "netpacket.h"
#include "timing.h"
#include "structNet.h"
#include "NetPrivate_h_ast.h"
#include "StashTable.h"
#include "resourceInfo.h"
#include "NetPacket_c_ast.h"
#include "MathUtil.h"
#include "MemAlloc.h"
#include "sysutil.h"
#include "TextParser.h"
#include "logging.h"
#include "ThreadSafeMemoryPool.h"
#include "ScratchStack.h"
#include "textparserJSON.h"

AUTO_RUN_ANON(memBudgetAddMapping("PacketTracker", BUDGET_Networking););


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

typedef enum
{
	MSGTYPE_BITS=1,
	MSGTYPE_F32,
	MSGTYPE_BYTES,
	MSGTYPE_STRING,
	MSGTYPE_BITS_64,
	MSGTYPE_F64,
} PacketType;

TSMP_DEFINE(Packet);

// These functions are intended to wrap only the allocator used for the Packet structure itself
// If you want to change how we allocate Packets, change both pktAlloc_internal and pktFree_internal
// Never directly allocate a Packet yourself
static __forceinline Packet *pktAlloc_internal(MEM_DBG_PARMS_VOID)
{
	ATOMIC_INIT_BEGIN;
	TSMP_CREATE(Packet, 1024);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(Packet);
}

static __forceinline void *pktAllocData_internal(S64 size MEM_DBG_PARMS)
{
	int heap = 0;
	if (IsThisObjectDBOrMerger())
		heap = CRYPTIC_PACKET_HEAP;
	return _malloc_dbg(size, heap MEM_DBG_PARMS_CALL);
}

static __forceinline void *pktReallocData_internal(void *data, S64 size MEM_DBG_PARMS)
{
	int heap = 0;
	if (IsThisObjectDBOrMerger())
		heap = CRYPTIC_PACKET_HEAP;
	return _realloc_dbg(data, size, heap MEM_DBG_PARMS_CALL);
}

void *pktReallocData(void *data, S64 size MEM_DBG_PARMS)
{
	return pktReallocData_internal(data, size MEM_DBG_PARMS_CALL);
}

static __forceinline void pktFree_internal(Packet **pkt)
{
	TSMP_FREE(Packet, *pkt);
}

static __forceinline void pktFreeData_internal(void *data)
{
	free(data);
}

static Packet* pktHeapAlloc(U32 bufferSize MEM_DBG_PARMS)
{
	Packet* pkt = pktAlloc_internal(MEM_DBG_PARMS_CALL_VOID);
	pkt->max_size_mutable = bufferSize;
	pkt->data = pktAllocData_internal(pkt->max_size MEM_DBG_PARMS_CALL);
	MEM_DBG_STRUCT_PARMS_INIT(pkt);
	return pkt;
}

Packet *pktCreateRawSize_dbg(NetLink *link, U32 bufferSize MEM_DBG_PARMS)
{
	Packet	*pkt;

	PERFINFO_AUTO_START_FUNC();

	if (!bufferSize)
	{
		if (link)
		{
			if (!link->default_pktsize)
				link->default_pktsize = 512;
			bufferSize = link->default_pktsize;
		}
		else
		{
			bufferSize = 512;
		}
	}
			
	pkt = pktHeapAlloc(bufferSize MEM_DBG_PARMS_CALL);
	pkt->has_verify_data = link ? !!(link->flags & LINK_PACKET_VERIFY) : !!isDevelopmentMode();
	pkt->link = link;
#if GAMECLIENT
    pkt->assert_on_error = 1;
#endif
    if(isDevelopmentMode()) 
        pkt->assert_on_error = 1;

	PERFINFO_AUTO_STOP();
	return pkt;
}

Packet *pktCreateRaw_dbg(NetLink *link MEM_DBG_PARMS)
{
	int		size = 512;

	return pktCreateRawSize_dbg(link, 0 MEM_DBG_PARMS_CALL);
}

Packet *pktCreateRawForReceiving_dbg(NetLink *link MEM_DBG_PARMS)
{
	int		size = 512;
	static PacketTracker *pTracker;
	Packet *pPak;
	ONCE(pTracker = PacketTrackerFind("_Receiving", 0, NULL));

	pPak = pktCreateRawSize_dbg(link, 0 MEM_DBG_PARMS_CALL);
	pPak->pTracker = pTracker;
	TrackerReportPacketCreate(pPak);
	return pPak;
}


Packet *pktCreateTemp_dbg(NetLink *link MEM_DBG_PARMS)
{
	Packet	*pkt;

	PERFINFO_AUTO_START_FUNC();
	pkt = pktHeapAlloc(1024 MEM_DBG_PARMS_CALL);
	if (link)
	{
		pkt->has_verify_data = !!(link->flags & LINK_PACKET_VERIFY);
	}
	PERFINFO_AUTO_STOP();
	return pkt;
}

Packet *pktCreateSize_dbg(NetLink *link,U32 bufferSize,int cmd, PacketTracker *pTracker MEM_DBG_PARMS)
{
	Packet	*pkt;
	
	PERFINFO_AUTO_START_FUNC();

	assertmsgf(cmd != FIRST_SHARED_CMD_ID - 1, "Creating packet with cmd %d. This is only one less than %d, which is the first special ID. Sounds like you're running out of packet IDs.",
		cmd, FIRST_SHARED_CMD_ID);

	if (!pTracker)
	{
		pTracker = PacketTrackerFind(caller_fname, line, NULL);
	}
	
	pkt = pktCreateRawSize_dbg(link, bufferSize MEM_DBG_PARMS_CALL);

	pkt->size = 4;
	pktSendBits(pkt,8,cmd);
	pkt->sendable = 1;
	pkt->pTracker = pTracker;
	

	TrackerReportPacketCreate(pkt);

	PERFINFO_AUTO_STOP();
	
	return pkt;
}

Packet *pktCreate_dbg(NetLink *link,int cmd, PacketTracker *pTracker MEM_DBG_PARMS)
{
	return pktCreateSize_dbg(link, 0, cmd, pTracker MEM_DBG_PARMS_CALL);
}

void pktFree(Packet **pakptr)
{
	Packet	*pkt = *pakptr;
	if (!pkt)
		return;
	PERFINFO_AUTO_START_FUNC();

	TrackerReportPacketFree(pkt);

	if(pkt->link){
		assert(pkt != pkt->link->recv_pak);
		pkt->link = NULL;
	}
	if (!pkt->created_with_set_payload)
		pktFreeData_internal(pkt->data);
	pktFree_internal(pakptr);
	*pakptr = 0;
	PERFINFO_AUTO_STOP();
}

Packet *pktDup(Packet *orig,NetLink *new_link)
{
	Packet	*pak;
	
	PERFINFO_AUTO_START_FUNC();
	pak = pktCreateRawSize(new_link, orig->size);
	pktGrow(pak,orig->size);
	pak->size = orig->size;
	pak->idx = orig->idx;
	memcpy(pak->data,orig->data,pak->size);
	PERFINFO_AUTO_STOP();
	return pak;
}

int pktEnd(Packet *pak)
{
	return (pak->idx >= pak->size);
}

#if 0
	#define PKT_HAS_VERIFY(pkt)							\
		(	pkt->link &&								\
			pkt->link->flags & LINK_PACKET_VERIFY		\
			||											\
			!pkt->link &&								\
			pkt->has_verify_data)
#else
	#define PKT_HAS_VERIFY(pkt) ((pkt)->has_verify_data)
#endif

static __forceinline void pktTrackType(Packet *pkt,PacketType type,int len)
{
	if(!PKT_HAS_VERIFY(pkt))
		return;
	pktGrow(pkt,1);
	pkt->data[pkt->size++] = type;
	if (len)
	{
		int		i;

		pktGrow(pkt,4);
		for(i=0;i<4;i++)
			pkt->data[pkt->size++] = (len >> 8 * i) & 255;
	}
}

static __forceinline void pktVerifyType(SA_PARAM_NN_VALID Packet *pkt,PacketType expected_type,int expected_len)
{
	PacketType	actual_type;

	if(!PKT_HAS_VERIFY(pkt))
		return;
	assert(pkt->idx + 1 < pkt->size);
	actual_type = pkt->data[pkt->idx++];
	assertmsgf(expected_type == actual_type, "expected_type(%d) != actual_type(%d)", expected_type, actual_type);
	if (expected_len)
	{
		int		i,actual_len=0;

		assert(pkt->idx + 4 < pkt->size);
		for(i=0;i<4;i++)
			actual_len |= pkt->data[pkt->idx++] << (i * 8);
		assertmsgf(expected_len == actual_len, "expected_len(%d) != actual_len(%d)", expected_len, actual_len);
	}
}

void pktReset(Packet *pak)
{
	assert(!pak->link);  // Only temp packets are allowed to do this
	pak->idx = 0;
}

static __forceinline int pktSpaceLeft(Packet *pkt,int num_bytes)
{
//	char	buf[40];

	if (pkt->idx + num_bytes <= pkt->size)
		return 1;
	if (PKT_HAS_VERIFY(pkt))
		assertmsg(0,"pktSpaceLeft failed");
	//log_printf("netpacket","%s pktSpaceLeft failed",linkGetIpStr(pkt->link,buf,sizeof(buf)));
	return 0;
}

void pktSendBytes(Packet *pkt,int count,void *bytes)
{
	pktTrackType(pkt,MSGTYPE_BYTES,count);
	pktGrow(pkt,count);
	memcpy(pkt->data+pkt->size,bytes,count);
	pkt->size += count;
}

void pktGetBytes(Packet *pkt,int count,void *bytes)
{
	pktVerifyType(pkt,MSGTYPE_BYTES,count);
	if (!pktSpaceLeft(pkt,count))
	{
		pktSetErrorOccurred(pkt, NULL);
		pkt->idx = pkt->size;
		return;
	}
	if (bytes)
	{
		memcpy(bytes,pkt->data+pkt->idx,count);
	}
	pkt->idx += count;
}

void *pktGetBytesTemp(Packet *pkt, int count)
{
	void *pRetVal;
	pktVerifyType(pkt,MSGTYPE_BYTES,count);
	pRetVal = pkt->data + pkt->idx;
	if (!pktSpaceLeft(pkt,count))
	{
		pktSetErrorOccurred(pkt, NULL);
		pkt->idx = pkt->size;
		return NULL;
	}

	pkt->idx += count;
	return pRetVal;
}


void pktSendString(Packet *pkt,const char *str)
{
	int		count;

	if (!str)
		str = "";
	count = (int)strlen(str)+1;
	pktTrackType(pkt,MSGTYPE_STRING,0);
	pktGrow(pkt,count);
	memcpy(pkt->data+pkt->size,str,count);
	pkt->size += count;
}

void pktSendStringf(Packet *pkt,const char *fmt, ...)
{
	char *pTempString = NULL;
	estrStackCreate(&pTempString);
	estrGetVarArgs(&pTempString, fmt);
	pktSendString(pkt, pTempString);
	estrDestroy(&pTempString);
}

char *pktGetStringTemp(Packet *pkt)
{
	char	*s;
	int		i,ok=0;

	pktVerifyType(pkt,MSGTYPE_STRING,0);
	for(i = pkt->idx; i < pkt->size; i++)
	{
		if(pkt->data[i] == '\0')
		{
			ok = 1;
			break;
		}
	}
	if (ok)
	{
		s = pkt->data+pkt->idx;
		pkt->idx = i+1;
	}
	else
	{
		//char buf[40];

		if (PKT_HAS_VERIFY(pkt))
			assertmsg(0,"pktGetStringTemp string not null terminated");
		//log_printf("netpacket","%s pktGetStringTemp failed",linkGetIpStr(pkt->link,buf,sizeof(buf)));
		s = "";
		pkt->idx = pkt->size;
		pktSetErrorOccurred(pkt, NULL);

	}
	return s;
}


char *pktGetStringTempAndGetLen(Packet *pkt, int *pLen)
{
	char	*s;
	int		i,ok=0;

	pktVerifyType(pkt,MSGTYPE_STRING,0);
	for(i = pkt->idx; i < pkt->size; i++)
	{
		if(pkt->data[i] == '\0')
		{
			ok = 1;
			*pLen = i - pkt->idx;
			break;
		}
	}
	if (ok)
	{
		s = pkt->data+pkt->idx;
		pkt->idx = i+1;
	}
	else
	{
		//char buf[40];

		if (PKT_HAS_VERIFY(pkt))
			assertmsg(0,"pktGetStringTemp string not null terminated");
		//log_printf("netpacket","%s pktGetStringTemp failed",linkGetIpStr(pkt->link,buf,sizeof(buf)));
		s = "";
		*pLen = 0;
		pkt->idx = pkt->size;
		pktSetErrorOccurred(pkt, NULL);

	}
	return s;
}



char *pktGetString(Packet *pkt,char *buf,int buf_size)
{
	int		count;
	char	*s;

	s = pktGetStringTemp(pkt);
	count = (int)strlen(s) + 1;
	if (count > buf_size)
	{
		if (PKT_HAS_VERIFY(pkt))
			assertmsg(0,"pktGetString: count <= buf_size");
		count = buf_size-1;
		buf[count] = 0;
	}
	memcpy(buf,s,count);
	return buf;
}

char *pktMallocString(Packet *pkt)
{
	int iLen;
	char *pStr = pktGetStringTempAndGetLen(pkt, &iLen);
	char *pOutBuf;
	if (!pStr)
	{
		return NULL;
	}

	pOutBuf = malloc(iLen + 1);
	memcpy(pOutBuf, pStr, iLen + 1);
	return pOutBuf;
}

char *pktMallocStringAndGetLen(Packet *pkt, int *pLen)
{
	char *pStr = pktGetStringTempAndGetLen(pkt, pLen);
	char *pOutBuf;

	if (!pStr)
	{
		return NULL;
	}
	pOutBuf = malloc(*pLen + 1);
	memcpy(pOutBuf, pStr, *pLen + 1);
	return pOutBuf;
}

int pktGetStringLen(Packet *pkt)
{
	int		idx = pkt->idx, i, ok = 0, ret;

	pktVerifyType(pkt,MSGTYPE_STRING,0);
	for(i = pkt->idx; i < pkt->size; ++i)
	{
		if(pkt->data[i] == '\0')
		{
			ok = 1;
			break;
		}
	}
	ret = (ok ? (i - pkt->idx) : 0);
	pkt->idx = idx;

	return ret;
}

static __forceinline void pktSendBitsInternal(Packet *pkt,int numbits,U32 val)
{
	int		i,numbytes;

	pktTrackType(pkt,MSGTYPE_BITS,numbits);
	if (numbits != 32)
		val &= (1 << numbits)-1;
	numbytes = (numbits + 7) >> 3;
	pktGrow(pkt,numbytes);
	for(i=0;i<numbytes;i++)
		pkt->data[pkt->size++] = (val >> (i * 8)) & 255;
}

void pktSendBits(Packet *pkt,int numbits,U32 val)
{
	pktSendBitsInternal(pkt,numbits,val);
}

U32 pktGetBits(Packet *pkt,int numbits)
{
	int		i,numbytes;
	U32		val=0;

	pktVerifyType(pkt,MSGTYPE_BITS,numbits);
	numbytes = (numbits + 7) >> 3;
	if (!pktSpaceLeft(pkt,numbytes))
	{
		pkt->idx = pkt->size;
		pktSetErrorOccurred(pkt, NULL);
		return 0;
	}
	for(i=0;i<numbytes;i++)
		val |= pkt->data[pkt->idx++] << (i * 8);
	return val;
}

void pktSendBits64(Packet *pkt,int numbits,U64 val)
{
	int		i,numbytes;

	pktTrackType(pkt,MSGTYPE_BITS_64,numbits);
	if (numbits != 64)
		val &= (1 << numbits)-1;
	numbytes = (numbits + 7) >> 3;
	pktGrow(pkt,numbytes);
	for(i=0;i<numbytes;i++)
		pkt->data[pkt->size++] = (val >> (i * 8)) & 255;
}

U64 pktGetBits64(Packet *pkt,int numbits)
{
	int		i,numbytes;
	U64		val=0;

	pktVerifyType(pkt,MSGTYPE_BITS_64,numbits);
	numbytes = (numbits + 7) >> 3;
	if (!pktSpaceLeft(pkt,numbytes))
	{
		pktSetErrorOccurred(pkt, NULL);

		pkt->idx = pkt->size;
		return 0;
	}
	for(i=0;i<numbytes;i++)
		val |= ((U64)pkt->data[pkt->idx++]) << (U64)(i * 8);
	return val;
}

void pktSendF32(Packet *pkt,F32 f)
{
	U32		val;
	int		i;

	assert(FINITE(f));

	pktTrackType(pkt,MSGTYPE_F32,0);
	val = *((U32 *)&f);
	pktGrow(pkt,4);
	for(i=0;i<4;i++)
		pkt->data[pkt->size++] = (val >> (i * 8)) & 255;
}

F32 pktGetF32(Packet *pkt)
{
	int		i;
	U32		val=0;
	float fVal;

	pktVerifyType(pkt,MSGTYPE_F32,0);
	if (!pktSpaceLeft(pkt,4))
	{
		pktSetErrorOccurred(pkt, NULL);

		pkt->idx = pkt->size;
		return 0;
	}
	for(i=0;i<4;i++)
		val |= pkt->data[pkt->idx++] << (i * 8);
	fVal = *((F32 *)&val);
	if (!FINITE(fVal))
	{
		fVal = 0;
		/*
		if (PKT_HAS_VERIFY(pkt))
		{
			assertmsg(0, "non-finite float received");
		}
		fVal = 0;
		pktSetErrorOccurred(pkt);
		return 0.0f;*/
	}

	return fVal;
}

void pktSendF64(Packet *pkt,F64 f)
{
	U64		val;
	int		i;

	assert(FINITE(f));

	pktTrackType(pkt,MSGTYPE_F64,0);
	val = *((U64 *)&f);
	pktGrow(pkt,8);
	for(i=0;i<8;i++)
		pkt->data[pkt->size++] = (val >> (i * 8)) & 255;
}

F64 pktGetF64(Packet *pkt)
{
	int		i;
	U64		val=0;
	F64 fVal;

	pktVerifyType(pkt,MSGTYPE_F64,0);
	if (!pktSpaceLeft(pkt,8))
	{
		pktSetErrorOccurred(pkt, NULL);
		pkt->idx = pkt->size;
		return 0;
	}
	for(i=0;i<8;i++)
		val |= (U64)pkt->data[pkt->idx++] << (i * 8);

	fVal = *((F64 *)&val);
	if (!FINITE(fVal))
	{
		fVal = 0;
		/*
		if (PKT_HAS_VERIFY(pkt))
		{
			assertmsg(0, "non-finite float received");
		}
		pktSetErrorOccurred(pkt);
		return 0.0f;*/
	}

	return fVal;

}

void pktSendStruct(Packet *pkt,const void *s,ParseTable pti[])
{
	ParserSend(pti, pkt, NULL, s, 0, 0, 0, NULL);
}

void pktSendStructJSON(Packet *pkt, void *s, ParseTable pti[])
{
    char *tmpEStr = NULL;

    ParserWriteJSON(&tmpEStr, pti, s, 0, 0, 0);

    pktSendString(pkt, tmpEStr);

    estrDestroy(&tmpEStr);
}

void *pktGetStruct(Packet *pkt,ParseTable pti[])
{
	void *s = StructCreateVoid(pti);
	ParserRecv(pti, pkt, s, 0);
	return s;
}

void *pktGetStructFromUntrustedSource(Packet *pkt,ParseTable pti[])
{
	void *s = StructCreateVoid(pti);
	ParserRecv(pti, pkt, s, RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE);
	return s;
}

// used for initial handshake, and web-style tcp
// doesn't null terminate, and doesn't do packet type validation
void pktSendBytesRaw(Packet *pkt,const void *bin,int len)
{
	pktGrow(pkt,len);
	memcpy(pkt->data+pkt->size,bin,len);
	pkt->size += len;
}

void pktSendStringRaw(Packet *pkt,const char *str)
{
	int		count = (int)strlen(str);

	pktGrow(pkt,count);
	memcpy(pkt->data+pkt->size,str,count);
	pkt->size += count;
}

int pktUncompress(Packet *pkt,int new_packed_bytes)
{
	z_stream	*z = pkt->link->compress->recv;
	U8			*packed;
	int			err,orig_size = pkt->size,unpacked;

	pktPushRing(pkt, &pkt->link->compress->debug_recv_ring, pkt->data + pkt->size, new_packed_bytes);
	
	// !!!: It is unclear as to if we should use something other than alloca here. It is possible that for a very large packet, this could overlfow the stack
	//      We should probably make a wrapper to revert to malloc or ScratchAlloc for larger sizes. See also the _malloca function from MS. <NPK 2008-09-25>
	if(new_packed_bytes < UNCOMPRESS_ALLOC_THRESHOLD)
		packed = _alloca(new_packed_bytes);
	else
		packed = ScratchAllocUninitialized(new_packed_bytes);
	memcpy(packed,pkt->data + pkt->size,new_packed_bytes);
	z->avail_in		= new_packed_bytes;
	z->next_in		= packed;
	ADD_MISC_COUNT(new_packed_bytes, "bytesCompressed");
	while(z->avail_in)
	{
		U32 addSize;
		pktGrow(pkt,PACKET_SIZE);
		z->avail_out	= pkt->max_size - pkt->size;
		z->next_out		= pkt->data + pkt->size;
		err=inflate(z,Z_SYNC_FLUSH);
		if (err)
			return -1;
		addSize = (pkt->max_size - pkt->size) - z->avail_out;
		ADD_MISC_COUNT(addSize, "bytesUncompressed");
		pkt->size += addSize;
	}
	unpacked = pkt->size - orig_size;
	pkt->size = orig_size;
	if(new_packed_bytes >= UNCOMPRESS_ALLOC_THRESHOLD)
		ScratchFree(packed);
	return unpacked;
}

int pktSendRaw(Packet **pakptr)
{
	Packet		*pkt = *pakptr;
	NetLink		*link = pkt->link;
	int			bytes_sent;

	InterlockedIncrement(&link->outbox);
	bytes_sent = netSafeSend(link,link->sock,pkt->data,pkt->size);
	pktFree(pakptr);
	InterlockedDecrement(&link->outbox);
	if (bytes_sent <= 0)
		return 0;
	link->stats.send.packets++;
	link->stats.send.real_packets++;
	*pakptr = 0;
	return 1;
}

void _pktGrow(Packet *pkt,int bytes)
{
	int		max_size = pkt->max_size;
	int		starting_max_size = max_size;

	/*if (pkt->link && pkt->link->pktsize_max && max_size + bytes > pkt->link->pktsize_max && pkt != pkt->link->recv_pak)
	{
		Errorf("send packet too large! %d > %d\n",max_size + bytes,pkt->link->pktsize_max);
	}*/
	while(bytes + pkt->size > max_size)
		max_size *= 2;
	if (max_size != pkt->max_size)
	{
		PERFINFO_AUTO_START("pktGrow", 1);
			pkt->data = pktReallocData_internal(pkt->data, max_size, pkt->caller_fname, pkt->line);
		PERFINFO_AUTO_STOP();
		pkt->max_size_mutable = max_size;
	}

	TrackerReportPacketResize(pkt, starting_max_size);
}

int pktIsDebug(Packet *pkt)
{
	return PKT_HAS_VERIFY(pkt);
}

U32 pktLinkID(Packet *pkt)
{
	return pkt->link->ID;
}

U32 pktGetReadOrWriteIndex(Packet* pkt)
{
	return pkt ? pkt->sendable ? pkt->size : pkt->idx : 0;
}

U32 pktGetSize(Packet *pak)
{
	return pak->size;
}

void pktAppend(Packet *dst, Packet *append, int append_idx)
{
	int		size;

	assertmsg(dst->has_verify_data==append->has_verify_data, "Pkt append requires both packets have same verify data state");
	
	if(!append->size)
	{
		return;
	}
	
	ADD_MISC_COUNT(append->size, "pktAppend bytes");
	
	if (append_idx < 0)
		append_idx = append->idx;
	size = append->size - append_idx;
	pktGrow(dst,size);
	memcpy(dst->data + dst->size,append->data + append_idx,size);
	dst->size += size;
}

U32 pktGetID(Packet* pkt)
{
	return SAFE_MEMBER(pkt, id);
}

int pktGetIndex(Packet *pkt)
{
	return SAFE_MEMBER(pkt, idx);
}

void pktSetIndex(Packet *pkt, int iIndex)
{
	if(pkt)
	{
		pkt->idx = iIndex;
	}
}

void pktSetEnd(Packet *pkt)
{
	pkt->idx = pkt->size;
}

void pktSetAssertOnError(Packet* pkt, S32 enabled)
{
	pkt->assert_on_error = !!enabled;
}

int pktGetWriteIndex(Packet *pkt)
{
	return SAFE_MEMBER(pkt, size);
}
void pktSetWriteIndex(Packet *pkt, int iIndex)
{
	if(pkt)
	{
		pkt->size = iIndex;
	}
}

void pktSetBuffer(Packet *pak, U8* data, int size_in_bytes)
{
	assert(!pak->link);  // Only temp packets are allowed to do this
	pktFreeData_internal(pak->data);

	pak->data = data;
	pak->size = size_in_bytes;
	pak->created_with_set_payload = true;

	//pretty sure this case is impossible, but might as well be careful
	if (pak->pTracker)
	{
		int iPrevMaxSize = pak->max_size;
		pak->max_size_mutable = size_in_bytes;
		TrackerReportPacketResize(pak, iPrevMaxSize);
	}
	else
	{
		pak->max_size_mutable = size_in_bytes;
	}
}

U32 pktId(Packet *pak)
{
	return pak->id;
}

NetLink *pktLink(Packet *pak)
{
	return pak->link;
}

bool pktCheckNullTerm(const Packet *pak)
{
	int idx = pak->idx;

	if(PKT_HAS_VERIFY(pak))
		++idx;

	for(; idx < pak->size; ++idx)
	{
		if(pak->data[idx] == '\0')
			return true;
	}

	return false;
}

bool pktCheckRemaining(const Packet *pak, int bytes)
{
	int idx = pak->idx;

	if(PKT_HAS_VERIFY(pak))
	{
		if(idx >= pak->size)
			return false;
		switch(pak->data[idx])
		{
		case MSGTYPE_BYTES:
		case MSGTYPE_BITS:
		case MSGTYPE_BITS_64:
			idx += 4;
		}
		++idx;
	}

	return (idx + bytes) <= pak->size;
}

char *pktGetStringRaw(const Packet *pak)
{
	return pak->data;
}

#define AUTO_FIRST_BYTE_DATA_BITS	6
#define AUTO_FIRST_BYTE_FLAG_BITS	(BITS_PER_BYTE - AUTO_FIRST_BYTE_DATA_BITS)
static const U32 num_extra_bits[] = { 0,8,16,26 };
static const U32 auto_masks[] = {~(BIT(6)-1),~(BIT(14)-1),~(BIT(22)-1),0};
STATIC_ASSERT(ARRAY_SIZE(num_extra_bits) == ARRAY_SIZE(auto_masks));

AUTO_RUN_LATE;
void pktVerifyAutoBits(void)
{
	ARRAY_FOREACH_BEGIN(auto_masks, i);
		U32 bitCount = num_extra_bits[i] + AUTO_FIRST_BYTE_DATA_BITS;
		if(bitCount == 32){
			assert(auto_masks[i] == 0);
		}else{
			assert(auto_masks[i] == ~(BIT(bitCount) - 1));
		}
	ARRAY_FOREACH_END;
}

void pktSendBitsAuto(Packet *pak, U32 val)
{
	int		i;
	NetLink	*link = pak->link;

	if (link && link->protocol == 1)
	{
		pktSendBits(pak,32,val);
		return;
	}
	for(i=0;i<ARRAY_SIZE(auto_masks) - 1;i++)
	{
		if (!(val & auto_masks[i]))
			break;
	}
	pktSendBits(pak,8,((val & (BIT(AUTO_FIRST_BYTE_DATA_BITS) - 1)) << AUTO_FIRST_BYTE_FLAG_BITS) | i);
	if (i)
		pktSendBits(pak,num_extra_bits[i],val >> AUTO_FIRST_BYTE_DATA_BITS);
}

U32 pktGetBitsAuto(Packet *pak)
{
	U32		val,idx;
	NetLink	*link = pak->link;

	if (link && link->protocol == 1)
		return pktGetBits(pak,32);

	val = pktGetBits(pak, 8);
	idx = val & 3;
	val >>= 2;
	if (idx)
		val |= pktGetBits(pak,num_extra_bits[idx & 3]) << 6;
	return val;
}

void pktSendBool(Packet *pkt, bool val)
{
	pktSendBits(pkt, 1, val ? 1 : 0);
}

bool pktGetBool(Packet *pkt)
{
	return pktGetBits(pkt, 1);
}

void pktGetRemainingRawBytes(Packet *pkt, void **ppOutBuf, int *pOutBufSize)
{
	*pOutBufSize = pkt->size - pkt->idx;
	*ppOutBuf = pkt->data + pkt->idx;
}

int pktGetNumRemainingRawBytes(Packet *pkt)
{
	return pkt->size - pkt->idx;
}


int pktCopyRemainingRawBytesToOtherPacket(Packet *destPkt, Packet *srcPkt)
{
	void *pSrcBuf;
	int iSrcBufSize;

	pktGetRemainingRawBytes(srcPkt, &pSrcBuf, &iSrcBufSize);
	if (iSrcBufSize)
	{
		pktSendBytesRaw(destPkt, pSrcBuf, iSrcBufSize);
		return iSrcBufSize;
	}

	return 0;
}

bool pktIsNotTrustworthy(Packet *pak)
{
	if (pak->link)
	{
		return pak->link->not_trustworthy;
	}

	return false;
}

void pktSetHasVerify(Packet *pak, bool bVerify)
{
	pak->has_verify_data = !!bVerify;
}

void pktSetErrorOccurred(Packet *pkt, const char* pchError)
{
	pkt->error_occurred = 1;
	if(pkt->assert_on_error) {
		if (pchError)
			assertmsgf(0, "Packet 0x%p has a read error: %s", pkt, pchError);
		else
			assertmsgf(0, "Packet 0x%p has a read error. There may be an errorf with more information.", pkt);
	}
}

Packet *pktCreateTempWithSetPayload(void *pPayload, int bytes)
{
	Packet *pak = pktAlloc_internal(MEM_DBG_PARMS_INIT_VOID);
	pak->data = pPayload;
	pak->size = pak->max_size_mutable = bytes;
	pak->created_with_set_payload = true;

	return pak;
}


void *pktGetEntirePayload(Packet *pak, int *pOutBytes)
{
	if (pOutBytes)
	{
		*pOutBytes = pak->size;
	}

	return pak->data;
}


void pktSendEntireTempPacket(Packet *pOuterPacket, Packet *pTempPacket)
{
	if (!pTempPacket)
	{
		pktSendBits(pOuterPacket, 32, 0);
		return;
	}

	pktSendBits(pOuterPacket, 32, pTempPacket->size);
	if (pTempPacket->size)
	{
		pktSendBytes(pOuterPacket, pTempPacket->size, pTempPacket->data);
	}
}

//get it out on the other end. You will need to pktFree it
Packet *pktCreateAndGetEntireTempPacket(Packet *pPacket)
{
	int iSize = pktGetBits(pPacket, 32);
	return pktCreateTempWithSetPayload(pktGetBytesTemp(pPacket, iSize), iSize);
}

void pktSetSendable(Packet* pkt, S32 sendable)
{
	if(pkt)
	{
		pkt->sendable = !!sendable;
	}
}

//512bytes, 1K, 2K, 4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1M, 2M, 4M, 8M, 16MPlus



AUTO_STRUCT;
typedef struct PacketTrackerList
{
	PacketTracker **ppTrackers; AST(NO_INDEX)
} PacketTrackerList;

static CRITICAL_SECTION sPacketTrackerCS = {0};
static StashTable sPacketTrackers = NULL;
static PacketTrackerList sTrackerList = {0};
PacketTracker *gpGlobalTracker = NULL;

AUTO_RUN_FIRST;
void PacketTrackerCSInit(void)
{
	InitializeCriticalSection(&sPacketTrackerCS);

}

static int BucketNum(int iSize)
{
	int iBitNum;
	if (iSize <= 0)
	{
		return 0;
	}

	iBitNum = highBitIndex(iSize - 1);
	if (iBitNum <= 8)
	{
		return 0;
	}

	if (iBitNum >= 23)
	{
		return NUM_TRACKER_BUCKETS - 1;
	}

	return iBitNum - 8;
}

//this requires a critical section, and is called any time you call pktCreate, so you should call it ahead of time,
//cache the result, and cache the result whenever possible
PacketTracker *PacketTrackerFind(const char *pFileName, int iLineNum, const char *pComment)
{
	U32 iHash;
	PacketTracker *pRetVal;
	PacketTracker *pOtherThreadOne = NULL;

	PERFINFO_AUTO_START_FUNC();

#ifdef _M_X64
	iHash = ((((intptr_t)pFileName) & 0xffffffff) + (((intptr_t)pFileName) >> 32)) * (iLineNum + 1) + (((intptr_t)pComment) & 0xffffffff) + (((intptr_t)pComment) >> 32);
#else
	iHash = ((intptr_t)pFileName * (iLineNum + 1)) + (intptr_t)pComment;
#endif

	if (!iHash)
	{
		iHash = 1;
	}

	EnterCriticalSection(&sPacketTrackerCS);

	if (!sPacketTrackers)
	{
		sPacketTrackers = stashTableCreateInt(16);
		resRegisterDictionaryForEArray("PacketTrackers", RESCATEGORY_SYSTEM, 0, &sTrackerList.ppTrackers, parse_PacketTracker);
		gpGlobalTracker = StructCreate(parse_PacketTracker);
		estrCopy2(&gpGlobalTracker->pDescriptiveName, "_All_Packets");
		eaPush(&sTrackerList.ppTrackers, gpGlobalTracker);
	}

	if (!stashIntFindPointer(sPacketTrackers, iHash, &pRetVal))
	{
		
		//want to do all our StructCreation and so forth outside the CS because printfs can cause deadlocks
		LeaveCriticalSection(&sPacketTrackerCS);

		pRetVal = StructCreate(parse_PacketTracker);
		if (!pComment && !iLineNum)
		{
			estrPrintf(&pRetVal->pDescriptiveName, "%s", pFileName);
		}
		else if (!pComment)
		{
			estrPrintf(&pRetVal->pDescriptiveName, "%s(%d)", pFileName, iLineNum);
		}
		else if (!iLineNum)
		{
			estrPrintf(&pRetVal->pDescriptiveName, "%s - %s", pFileName, pComment);
		}
		else 
		{
			estrPrintf(&pRetVal->pDescriptiveName, "%s(%d) - %s", pFileName, iLineNum, pComment);
		}

	
		EnterCriticalSection(&sPacketTrackerCS);
		if (stashIntFindPointer(sPacketTrackers, iHash, &pOtherThreadOne))
		{

		}
		else
		{
			stashIntAddPointer(sPacketTrackers, iHash, pRetVal, false);
			eaPush(&sTrackerList.ppTrackers, pRetVal);
		}
	}

	LeaveCriticalSection(&sPacketTrackerCS);
	
	if (pOtherThreadOne)
	{
		StructDestroy(parse_PacketTracker, pRetVal);
		pRetVal = pOtherThreadOne;
	}


	PERFINFO_AUTO_STOP();

	return pRetVal;
}

static void AddToBucket(PacketTrackerBucket *pBucket)
{
	pBucket->iCurCount++;
	if (pBucket->iCurCount > pBucket->iMaxCount)
	{
		pBucket->iMaxCount = pBucket->iCurCount;
	}
}


static void RemoveFromBucket(PacketTrackerBucket *pBucket)
{
	if (pBucket->iCurCount > 0)
	{
		pBucket->iCurCount--;
	}

}

static void AddToTracker(PacketTracker *pTracker)
{
	pTracker->iCurCount++;
	if (pTracker->iCurCount > pTracker->iMaxCount)
	{
		pTracker->iMaxCount = pTracker->iCurCount;
	}
	pTracker->iTotalCreated++;
}


static void RemoveFromTracker(PacketTracker *pTracker)
{
	pTracker->iTotalFreed++;
	if (pTracker->iCurCount > 0)
	{
		pTracker->iCurCount--;
	}
}

void TrackerReportPacketSend(Packet *pPak)
{
	if (pPak->pTracker)
	{
		int iBucketNum = BucketNum(pPak->max_size);

		pPak->pTracker->iTotalSent++;
		pPak->pTracker->iTotalBytesSent += pPak->size;
		gpGlobalTracker->iTotalSent++;
		gpGlobalTracker->iTotalBytesSent += pPak->size;

		pPak->pTracker->buckets[iBucketNum].iTotalSent++;
		gpGlobalTracker->buckets[iBucketNum].iTotalSent++;

		if (pPak->size > pPak->pTracker->iLargestPacket)
		{
			pPak->pTracker->iLargestPacket = pPak->size;
			pPak->pTracker->iLargestPacketTime = timeSecondsSince2000();
		}		
		
		if (pPak->size > gpGlobalTracker->iLargestPacket)
		{
			gpGlobalTracker->iLargestPacket = pPak->size;
			gpGlobalTracker->iLargestPacketTime = timeSecondsSince2000();
		}


		pPak->pTracker = NULL;
	}
}

void TrackerReportPacketFree(Packet *pPak)
{
	if (pPak->pTracker)
	{
		int iBucketNum = BucketNum(pPak->max_size);
	
		RemoveFromTracker(pPak->pTracker);
		RemoveFromTracker(gpGlobalTracker);

		RemoveFromBucket(&pPak->pTracker->buckets[iBucketNum]);
		RemoveFromBucket(&gpGlobalTracker->buckets[iBucketNum]);
	}
}

void TrackerReportPacketCreate(Packet *pPak)
{
	if (pPak->pTracker)
	{
		int iBucketNum = BucketNum(pPak->max_size);

		AddToTracker(pPak->pTracker);
		AddToTracker(gpGlobalTracker);
		AddToBucket(&pPak->pTracker->buckets[iBucketNum]);
		AddToBucket(&gpGlobalTracker->buckets[iBucketNum]);
	}
}


void TrackerReportPacketResize(Packet *pPak, int iPrevoiusMaxSize)
{
	if (pPak->pTracker)
	{
		int iOldBucketNum  = BucketNum(iPrevoiusMaxSize);
		int iBucketNum = BucketNum(pPak->max_size);

		RemoveFromBucket(&pPak->pTracker->buckets[iOldBucketNum]);
		RemoveFromBucket(&gpGlobalTracker->buckets[iOldBucketNum]);
		AddToBucket(&pPak->pTracker->buckets[iBucketNum]);
		AddToBucket(&gpGlobalTracker->buckets[iBucketNum]);
	}
}

AUTO_COMMAND;
void LogPacketTrackers(void)
{
	char *pOutString = NULL;

	ParserWriteText(&pOutString, parse_PacketTrackerList, &sTrackerList, 0, 0, 0);
	servLog(LOG_PACKET, "PacketTrackers", "%s", pOutString);
	estrDestroy(&pOutString);
}




/*
void DoBucketTest(int i)
{
	printf("%d goes in bucket %d\n", i, BucketNum(i));
}

AUTO_RUN;
void BucketTest(void)
{
	DoBucketTest(0);
	DoBucketTest(1);
	DoBucketTest(2);

	DoBucketTest(511);
	DoBucketTest(512);
	DoBucketTest(513);

	DoBucketTest(1023);
	DoBucketTest(1024);
	DoBucketTest(1025);

	DoBucketTest(8 * 1024 * 1024 - 1);
	DoBucketTest(8 * 1024 * 1024);
	DoBucketTest(8 * 1024 * 1024 + 1);

	DoBucketTest(16 * 1024 * 1024 - 1);
	DoBucketTest(16 * 1024 * 1024);
	DoBucketTest(16 * 1024 * 1024 + 1);
}
*/



#include "NetPacket_c_ast.c"
