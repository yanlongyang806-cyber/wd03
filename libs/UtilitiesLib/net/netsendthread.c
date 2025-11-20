#include "../../3rdparty/zlib/zlib.h"
#include "sock.h"
#include "net.h"
#include "netprivate.h"
#include "crypt.h"
#include "netpacket.h"
#include "earray.h"
#include "timing.h"
#include "netlink.h"
#include "XboxThreads.h"
#include "Queue.h"
#include "mathutil.h"
#include "wtcmdpacket.h"
#include "EventTimingLog.h"
#include "file.h"
#include "GlobalTypes.h"
#include "ScratchStack.h"
#include "RingStream.h"
#include "cmdparse.h"

#include "SimpleCpuUsage.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:pktSendThread", BUDGET_Networking););

#define MAX_SEND_BUFFER_BYTES (512*1024)

// The last EString capture from netStopCapture.
static char *spLastCapture = NULL;

static S32 netNoSendBuffer;
AUTO_CMD_INT(netNoSendBuffer, netNoSendBuffer);

// Stop a capture started by AllowCaptureRequestOnPort.
static U32 sbNetStopCapture = false;
AUTO_CMD_INT(sbNetStopCapture, netStopCapture) ACMD_CATEGORY(Debug) ACMD_CALLBACK(netStopCapture);

enum
{
	NETCMD_SEND  = WT_CMD_USER_START,
	NETCMD_SENDANDFLUSH,
	NETCMD_FLUSH,
	NETCMD_CLOSE,
	NETCMD_FORCEFRAME,
};

static void getSendSize(NetLink *link)
{
	int		send_size,send_size_size = sizeof(send_size);
	
	linkVerbosePrintf(	link,
						"send buffer before %d/%d",
						link->curr_sendbuf,
						link->max_sendbuf);

	getsockopt(link->sock,SOL_SOCKET,SO_SNDBUF,(char*)&send_size,&send_size_size);
	link->curr_sendbuf = send_size;

	linkVerbosePrintf(	link,
						"send buffer after  %d/%d",
						link->curr_sendbuf,
						link->max_sendbuf);
}

void logPacket(int id, void *data, int size, int header,char *ext)
{
	int idx;
	FILE *pkt_log = NULL;
	char filename[MAX_PATH];

	if(!IsGameServerSpecificallly_NotRelatedTypes() && !IsClient())
		return;

	sprintf(filename, "c:/fightclub/logs/pkt_%s%s.log", IsServer() ? "server" : "client",ext ? ext : "");

	pkt_log = fileOpen(filename, "a+");

	if(!pkt_log)
		return;

	if (header)
		fprintf(pkt_log, "\n\npak-id %d - size %d\n\n", id, size);
	else
	{
		for(idx=0; idx<size; idx++)
			fwrite((char*)data+idx, 1, 1, pkt_log);

	}
	fileClose(pkt_log);
}

static void resizeSendBuffer(	NetLink* link,
								SOCKET sock,
								int send_size)
{
	PERFINFO_AUTO_START_FUNC();

	linkVerbosePrintf(	link,
						"send buffer before %d/%d",
						link->curr_sendbuf,
						link->max_sendbuf);

	setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&send_size,sizeof(send_size));
	getSendSize(link);

	linkVerbosePrintf(	link,
						"send buffer after  %d/%d",
						link->curr_sendbuf,
						link->max_sendbuf);

	PERFINFO_AUTO_STOP();
}

static void netHandleDisconnectOnFull(	NetLink* link,
										U32 sizeCurrentPacket)
{
	linkSetDisconnectReason(link, "DisconnectOnFull");

	if (!link->not_trustworthy)
	{
		ErrorOrAlertDeferred(	true,
							"LINK_CLOSE",
							"Net link %s has overflowed its maximum size of %uB while sending %uB."
							" Closing it."
							" Last recv %1.2fs, recv %"FORM_LL"d, sent %"FORM_LL"d.",
//							" Last recv %1.2fs, recv %uB, sent %uB.",
							link->debugName,
							link->max_sendbuf,
							sizeCurrentPacket,
							(F32)(timeGetTime() - link->stats.recv.last_time_ms) / 1000.f,
							link->stats.recv.real_bytes,
							link->stats.send.real_bytes);
	}

	safeCloseLinkSocket(link, "overflowed send buffer");
}

int netSafeSend(NetLink* link,
				SOCKET sock,
				const U8* data,
				U32 size)
{
	U32	num_retry=0;
	U32	time_since_size_changed_ms = timeGetTime();
	const U32 size_orig = size;
	
	PERFINFO_AUTO_START_FUNC();
	
	if (!link->curr_sendbuf)
		getSendSize(link);
	if (link->max_sendbuf && link->curr_sendbuf > link->max_sendbuf)
	{
		resizeSendBuffer(link, sock, link->max_sendbuf);
	}
	ADD_MISC_COUNT(size, "send bytes");
retry:
	num_retry++;
	PERFINFO_AUTO_START_BLOCKING("send", 1);
	link->sendBytesSent = send(sock,data,size,0);
	if(link->sendBytesSent != size){
		link->sendError = WSAGetLastError();
	}else{
		link->sendError = 0;
	}
	linkVerbosePrintf(link, "send %d bytes", size);
	PERFINFO_AUTO_STOP();

	// If requested, push the data we just sent onto a ring buffer.
	if (link->flags & LINK_CRAZY_DEBUGGING && (int)link->sendBytesSent > 0)
	{
		if (!link->debug_late_send_ring)
			link->debug_late_send_ring = ringStreamCreate(NET_DEBUG_RING_BUFFER_SIZE);
		ringStreamPush(link->debug_late_send_ring, data, link->sendBytesSent);
	}

	// If requested, push the data we just sent onto the capture buffer.
	if (link->estr_send_capture_buf && estrGetCapacity(&link->estr_send_capture_buf) && (int)link->sendBytesSent > 0)
	{
		unsigned capture_size = MIN(estrGetCapacity(&link->estr_send_capture_buf) - estrLength(&link->estr_send_capture_buf) - 1, link->sendBytesSent);
		if (capture_size)
			estrConcat(&link->estr_send_capture_buf, data, capture_size);
	}

	linkStatus(pkt->link,"send");

	if (link->sendBytesSent != size)
	{
		if (link->sendError == WSAEWOULDBLOCK || link->sendError == 0)
		{
			int		send_size = link->curr_sendbuf;

			if (link->sendBytesSent &&
				link->sendBytesSent != SOCKET_ERROR)
			{
				data += link->sendBytesSent;
				size -= link->sendBytesSent;
				time_since_size_changed_ms = timeGetTime();
			}
			if (!link->max_sendbuf || send_size < link->max_sendbuf)
			{
				int startingSize = send_size;

				send_size *= 2;
				if (link->max_sendbuf && send_size > link->max_sendbuf)
				{
					send_size = link->max_sendbuf;
				}
				resizeSendBuffer(link, sock, send_size);

				if (link->eType & LINKTYPE_FLAG_RESIZE_AND_WARN)
				{
					while (link->sentResizeAlert < LINK_RESIZES_WITH_WARNING_AFTER_MAX
						&& link->curr_sendbuf > (link->max_sendbuf >> (LINK_RESIZES_WITH_WARNING_AFTER_MAX - link->sentResizeAlert)))
					{
						bool bCritical = false;
						link->sentResizeAlert++;

						if (link->eType & LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT)
						{
							bCritical = true;
						}

						if (link->resize_cb)
						{
							link->resize_cb(link->curr_sendbuf);
						}

						ErrorOrAlertDeferred(bCritical, bCritical ? "LINK_FINAL_RESIZE" : "LINK_RESIZED", "Net link %s has been resized to %u bytes. This is resize #%d over its nominal max of %u bytes",
							link->debugName, link->curr_sendbuf, link->sentResizeAlert, link->max_sendbuf >> LINK_RESIZES_WITH_WARNING_AFTER_MAX);
					}
				}


				goto retry;
			}
			else
			{

				if (link->eType & LINKTYPE_FLAG_DISCONNECT_ON_FULL)
				{
					if (link->eType & LINKTYPE_FLAG_CRITICAL_ALERTS_ON_RESIZE_AND_DISCONNECT)
					{

						ErrorOrAlertDeferred(true, "LINK_FULL_DISCON_CRIT", "Net link %s has overflowed its %u byte send buffer and is disconnecting",
							link->debugName, link->curr_sendbuf);
					}
					else if (link->eType & LINKTYPE_FLAG_RESIZE_AND_WARN)
					{
						ErrorOrAlertDeferred(false, "LINK_FULL_DISCON", "Net link %s has overflowed its %u byte send buffer and is disconnecting",
							link->debugName, link->curr_sendbuf);
					}
		
					netHandleDisconnectOnFull(link, size_orig);
					PERFINFO_AUTO_STOP();
					return 0;
				}
				link->sendbuf_full = 1;
					
				if (link->sleep_cb)
				{
					link->sleep_cb();
				}

				Sleep(1);


				if (!(link->eType & LINKTYPE_FLAG_NO_ALERTS_ON_FULL_SEND_BUFFER) && FALSE_THEN_SET(link->sent_sleep_alert))
				{
	
					ErrorOrAlertDeferred(false, "LINK_SLEEP", "Net link %s is full (send buff size: %u) and is sleeping. This probably means that it is sending data faster than the receiving process can handle it, and may need investigation",
						link->debugName, link->max_sendbuf);
				}

				link->full_count++;
			}
			link->sendbuf_was_full = 1; // manually cleared by link owner with linkClearSendBufWasFull
			if(	(F32)(timeGetTime() - time_since_size_changed_ms) / 1000.f > link->listen->comm->send_timeout &&
				!(link->eType & LINKTYPE_FLAG_SLEEP_ON_FULL)) // effectively making SLEEP_ON_FULL work like no-timeout
			{
				printf(	"netSafeSend(): ERROR: send timed out - link name %s / %s:%d (%d bytes sent, %d remaining, buffer size %d)!\n",
						link->debugName,
						makeIpStr(link->addr.sin_addr.s_addr),
						ntohs(link->addr.sin_port),
						link->sendBytesSent,
						size,
						link->curr_sendbuf);
				safeCloseLinkSocket(link, "send timed out");
				PERFINFO_AUTO_STOP();
				return 0;
			}
			goto retry;
		}
		PERFINFO_AUTO_STOP();
		return 0;
	}
	assert(link->sendBytesSent == size);
	link->stats.send.real_bytes += size_orig;
	if (num_retry == 1 && link->curr_sendbuf <= link->max_sendbuf)
		link->sendbuf_full = 0;
	PERFINFO_AUTO_STOP();
	return size_orig;
}

typedef struct SavedCompressHeader {
	U32 sizeThisChunk;
	U32 sizeTotal;
	S32 isFlush;
} SavedCompressHeader;

static void *zlib_debug_calloc(void *opaque,uInt num,uInt size)
{
	return calloc(num,size);
}

static void zlib_debug_free(void *opaque,void *data)
{
	free(data);
}

static S32 saveCompressState;
AUTO_CMD_INT(saveCompressState, netSaveCompressState) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static U32 saveCompressBufferSize = SQR(1024);
AUTO_CMD_INT(saveCompressBufferSize, netSaveCompressBufferSize) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static void netCompressDebugAlloc(NetCompressDebug* debug)
{
	PERFINFO_AUTO_START("alloc debug buffer", 1);
	debug->bufferSize = saveCompressBufferSize;
	MINMAX1(debug->bufferSize, 1024, 10 * SQR(1024));
	debug->buffer = calloc(debug->bufferSize, 1);
	PERFINFO_AUTO_STOP();
}

void netCompressDebugFree(NetCompress* compress)
{
	PERFINFO_AUTO_START_FUNC();
	
	ARRAY_FOREACH_BEGIN(compress->debug, i);
		NetCompressDebug* debug = compress->debug + i;
		SAFE_FREE(debug->buffer);
		if(debug->sendStart){
			PERFINFO_AUTO_START("deflateEnd", 1);
			deflateEnd(debug->sendStart);
			PERFINFO_AUTO_STOP();

			SAFE_FREE(debug->sendStart);
		}
		ZeroStruct(debug);
	ARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void netCompressDebugStartBuffer(NetCompress* compress,
										NetCompressDebug* debug)
{
	PERFINFO_AUTO_START_FUNC();
	
	debug->bufferPos = 0;
	
	if(	debug->bufferSize &&
		debug->bufferSize != saveCompressBufferSize)
	{
		PERFINFO_AUTO_START("resize debug buffer", 1);
		SAFE_FREE(debug->buffer);
		debug->bufferSize = 0;
		PERFINFO_AUTO_STOP();
	}
	
	if(!debug->buffer){
		compress->debugIsAllocated = 1;
		netCompressDebugAlloc(debug);
	}
	
	// Copy the z_stream.
	
	if(debug->sendStart){
		PERFINFO_AUTO_START("deflateEnd", 1);
		deflateEnd(debug->sendStart);
		ZeroStruct(debug->sendStart);
		PERFINFO_AUTO_STOP();
	}else{
		debug->sendStart = callocStruct(z_stream);
	}
	
	assert(debug->sendStart);
	
	PERFINFO_AUTO_START("deflateCopy", 1);
	{
		debug->sendStart->zalloc = zlib_debug_calloc;
		debug->sendStart->zfree = zlib_debug_free;
		deflateCopy(debug->sendStart, compress->send);
	}
	PERFINFO_AUTO_STOP();
	
	PERFINFO_AUTO_STOP();// FUNC.
}

// If LINK_CRAZY_DEBUGGING, push to debugging ring buffer.
void pktPushRing(Packet *pkt, RingStream *ring, const char *data, size_t size)
{
	NetLink *link;

	link = pkt->link;
	if (link->flags & LINK_CRAZY_DEBUGGING)
	{
		if (!*ring)
			*ring = ringStreamCreate(NET_DEBUG_RING_BUFFER_SIZE);
		ringStreamPush(*ring, data, size);
	}
}

static bool netVerifyPacketContents = false;
AUTO_CMD_INT(netVerifyPacketContents, netVerifyPacketContents) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

static __forceinline void pktVerifyContents(const Packet *pkt, const U8 *data)
{
	U32 size;

	if (!netVerifyPacketContents) return;

	PERFINFO_AUTO_START("VerifyPacketContents", 1);
	size = bytesToU32(data);
	assert(size == (unsigned)pkt->size);

	if (pkt->link->flags & LINK_CRC)
	{
		U32 crc = bytesToU32(data + 4);
		U32 seq_id = bytesToU32(data + 8);
		U32 verify_crc;

		PERFINFO_AUTO_START("cryptAdler32", 1);
		verify_crc = cryptAdler32(data + NET_LINK_PACKET_HEADER_SIZE(pkt->link), pkt->size - NET_LINK_PACKET_HEADER_SIZE(pkt->link));
		PERFINFO_AUTO_STOP();

		assert(crc == verify_crc);
		assert(seq_id == pkt->link->sent_seq_id);
	}
	PERFINFO_AUTO_STOP();
}

static void pktCompress(Packet *pkt,int flush)
{
	NetCompress*	compress = pkt->link->compress;
	z_stream*		z = compress->send;
	U8*				zdata;
	int				zsize,zmaxsize;

	// Toggle compression, if necessary.
	// Warning: Even with the following workaround, zlib still generates spurious crashes when frequently changing
	// the deflate params.  The current advice is to not use this feature until we analyze the underlying zlib
	// problem and figure out how to deal with it.
	// Note: zlib (confirmed on versions 1.2.3 and 1.2.5) has an bug affecting the following case:
	//   -deflateParams called twice in a row
	//   -no new data compressed between calls
	//   -compression parameters are different
	//   -some data has previously been compressed on the stream
	// In this case, a double-flush may be generated internally, which will cause zlib to return a Z_BUF_ERROR,
	// which will probably lead to crashes.  So, we only call this function is we know that we actually have
	// data to write immediately after.
	if (pkt->no_compress && !compress->lastPacketNoCompress)
	{
		deflateParams(z,Z_NO_COMPRESSION,Z_DEFAULT_STRATEGY);
		compress->lastPacketNoCompress = true;
	}
	else if (!pkt->no_compress && compress->lastPacketNoCompress)
	{
		extern S32 netCompressLevel;
		S32 level = netCompressLevel;
		MINMAX1(level, Z_NO_COMPRESSION, Z_BEST_COMPRESSION);
		deflateParams(z,level,Z_DEFAULT_STRATEGY);
		compress->lastPacketNoCompress = false;
	}

	// Allocate memory
	zmaxsize = deflateBound(z, pkt->size) + 100;
	if (zmaxsize <= COMPRESS_ALLOC_THRESHOLD)
	{
		zdata = _alloca(zmaxsize);
	}
	else
	{
		PERFINFO_AUTO_START("zdata malloc", 1);
		zdata = ScratchAllocUninitialized(zmaxsize);
		PERFINFO_AUTO_STOP();
	}
		
	if(saveCompressState){
		NetCompressDebug*	debug = compress->debug + compress->curDebug;
		U8*					dataPos = pkt->data;
		U32					dataSizeRemaining = pkt->size;
		U8*					zdataPos = zdata;
		
		PERFINFO_AUTO_START("save and deflate", 1);
		
		if(!debug->buffer){
			compress->debugIsAllocated = 1;
			netCompressDebugAlloc(debug);
		}
		
		while(dataSizeRemaining){
			U32						bufferRemaining = debug->bufferSize - debug->bufferPos;
			SavedCompressHeader*	header;
			U32						zdataRemaining = (zdata + zmaxsize) - zdataPos;
			
			assert(debug->bufferPos <= debug->bufferSize);

			if(bufferRemaining < sizeof(*header) + 100){
				// Go to the next debug struct.

				compress->curDebug = (compress->curDebug + 1) % ARRAY_SIZE(compress->debug);
				debug = compress->debug + compress->curDebug;
				netCompressDebugStartBuffer(compress, debug);
				continue;
			}
			
			PERFINFO_AUTO_START("copy to debug buffer", 1);
			{
				header = (SavedCompressHeader*)(debug->buffer + debug->bufferPos);
				bufferRemaining -= sizeof(*header);
				
				header->sizeThisChunk = MIN(bufferRemaining, dataSizeRemaining);
				header->sizeTotal = pkt->size;
				header->isFlush = !!flush;
				
				memcpy(header + 1, dataPos, header->sizeThisChunk);
				dataSizeRemaining -= header->sizeThisChunk;
				debug->bufferPos += sizeof(*header) + header->sizeThisChunk;
			}
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_START("deflate", 1);
			{
				int result;
				z->avail_in		= header->sizeThisChunk;
				z->next_in		= dataPos;
				z->avail_out	= zdataRemaining;
				z->next_out		= zdataPos;
				result = deflate(z,flush && !dataSizeRemaining ? Z_SYNC_FLUSH : 0);
				assert(result == Z_OK);
			}
			PERFINFO_AUTO_STOP();

			dataPos += header->sizeThisChunk;
			zdataPos += zdataRemaining - z->avail_out;
		}

		zsize = zdataPos - zdata;
		
		PERFINFO_AUTO_STOP();
	}else{
		if(TRUE_THEN_RESET(compress->debugIsAllocated)){
			netCompressDebugFree(compress);
		}

		if (pkt->no_compress)
		{
			PERFINFO_AUTO_START("deflate (no compress)", 1);
		}
		else
		{
			PERFINFO_AUTO_START("deflate", 1);
		}
		{
			int result;
			z->avail_in		= pkt->size;
			z->next_in		= pkt->data;
			z->avail_out	= zmaxsize;
			z->next_out		= zdata;
			result = deflate(z,flush ? Z_SYNC_FLUSH : 0);
			assert(result == Z_OK);
			zsize = zmaxsize - z->avail_out;
		}
		PERFINFO_AUTO_STOP();
	}

	ADD_MISC_COUNT(pkt->size, "bytesBeforeCompression");
	ADD_MISC_COUNT(zsize, "bytesSentCompressed");

	devassert(zsize < 900*1024*1024);

	// If requested, verify the compressed data.
	// This is intended to track down bugs that result in corruption to the compressor state, such as
	// random memory corruption.  Normally, these kinds of issues would only manifest on the receiving side,
	// and might take a while to cause a problem that would be noticeable enough to be automatically reported.
	// Specifically, we're trying to track down COR-14425, which presents as the receiver reading a very large
	// packet size, and getting stuck in that state.  We'll probably improve the protocol to allow detection of
	// this kind of case, but this provides more timely and local notification of the problem.
	if (compress->send_verify)
	{
		size_t buffer_size;
		char *buffer;
		bool used_scratch = false;
		char *original_data;
		volatile unsigned overflow_count = 0;

		PERFINFO_AUTO_START("pktCompressVerify", 1);

		// Get enough memory to decompress.
		buffer_size = MAX(pkt->size * 2, 256);
		if (buffer_size > 16*1024)
		{
			buffer = ScratchAllocUninitialized(buffer_size);
			used_scratch = true;
		}
		else
			buffer = alloca(buffer_size);

		// Set up the stream.
		compress->send_verify->next_in = zdata;
		compress->send_verify->avail_in = zsize;
		compress->send_verify->next_out = buffer;
		compress->send_verify->avail_out = (unsigned)buffer_size;

		// Continuously inflate() until the data we just compressed comes back.
		while (compress->send_verify->avail_in)
		{
			int result;
			unsigned limit;

			// Try to inflate().
			result = inflate(compress->send_verify, Z_SYNC_FLUSH);
			assert(result == Z_OK);

			// If there was previous data sent on the stream, but not flushed, we'll get it back now.  If there's
			// a lot of it, we might need to remove some to make room.
			limit = (unsigned)buffer_size/2;
			if (compress->send_verify->avail_in && compress->send_verify->avail_out < limit)
			{
				unsigned offset;
				PERFINFO_AUTO_START("VerifyBufferOverflow", 1);
				offset = limit - compress->send_verify->avail_out;
				memmove(buffer, buffer + offset, limit);
				compress->send_verify->next_out -= offset;
				compress->send_verify->avail_out += offset;
				++overflow_count;
				PERFINFO_AUTO_STOP();
			}
		}

		// Verify that the decompressed data matches our original data.
		// Note that this is not possible if we're not flushing, since the stream isn't necessarily synced.
		if (flush)
		{
			assert((buffer_size - compress->send_verify->avail_out) >= (unsigned)pkt->size);
			original_data = (compress->send_verify->next_out - pkt->size);
			assert(memcmp(pkt->data, original_data, pkt->size) == 0);
			assert(compress->send_verify->adler == z->adler);

			// Now verify the contents of the re-uncompressed data
			pktVerifyContents(pkt, original_data);
		}

		// Free memory.
		if (used_scratch)
			ScratchFree(buffer);

		PERFINFO_AUTO_STOP();
	}

	pktVerifyContents(pkt, pkt->data);

	pktGrow(pkt,zsize - pkt->size);
	pkt->size = zsize;
	memcpy(pkt->data,zdata,zsize);
	if (zmaxsize > COMPRESS_ALLOC_THRESHOLD){
		PERFINFO_AUTO_START("zdata free", 1);
		ScratchFree(zdata);
		PERFINFO_AUTO_STOP();
	}

	pktPushRing(pkt, &compress->debug_send_ring, pkt->data, pkt->size);
}

static void linkDecrementOutbox(NetLink* link)
{
	assert(link->outbox);
	InterlockedDecrement(&link->outbox);
}

static U32 pktGetQueueTotalBytes(Packet* pkt){
	U32 size = 0;

	for(; pkt; pkt = pkt->nextInSendThread){
		size += pkt->size;
	}

	return size;
}

// Command data for netDumpCaptureThread()
struct capture_data {
	char *filename;			// Name of file to write capture to
	char *capture;			// EString of capture data
};

// Dump a previously-stopped capture, in a background thread.
// If anything goes wrong, put the buffer back.
static void netDumpCaptureThread(void *userdata)
{
	struct capture_data *data = userdata;
	FILE *outfile;
	unsigned written;
	int result;

	// Try to open file.
	outfile = fopen(data->filename, "wb");
	if (!outfile)
	{
		estrDestroy(&spLastCapture);
		spLastCapture = data->capture;
		printf("unable to open \"%s\"\n", data->filename);
		free(data->filename);
		free(data);
		return;
	}

	// Try to write the capture.
	written = (unsigned)fwrite(data->capture, 1, estrLength(&data->capture), outfile);
	if (written != estrLength(&data->capture))
	{
		estrDestroy(&spLastCapture);
		spLastCapture = data->capture;
		printf("incomplete write to \"%s\", %u/%u\n", data->filename, written, estrLength(&data->capture));
		free(data->filename);
		free(data);
		return;
	}

	// Try to flush and close file.
	result = fclose(outfile);
	if (result)
	{
		estrDestroy(&spLastCapture);
		spLastCapture = data->capture;
		printf("failure to close \"%s\"\n", data->filename);
		free(data->filename);
		free(data);
		return;
	}

	// Clean up.
	estrDestroy(&data->capture);
	printf("capture complete: %u bytes written to \"%s\"\n", written, data->filename);
	free(data->filename);
	free(data);
}

// Dump a previously-stopped capture.
AUTO_COMMAND ACMD_CATEGORY(Debug);
void netDumpCapture(const char *filename)
{
	char *capture = spLastCapture;
	struct capture_data *data;

	// Get capture data.
	if (!capture)
	{
		printf("no capture\n");
		return;
	}
	spLastCapture = NULL;

	// Create information for background thread.
	data = malloc(sizeof(*data));
	data->filename = strdup(filename);
	data->capture = capture;

	// Spawn background thread.
	_beginthread(netDumpCaptureThread, 0, data);
}

// Confirm receipt of capture command.
void netStopCapture(CMDARGS)
{
	printf("waiting for background thread to stop capturing...\n");
}

static void pktSendThreadFlush(NetLink *link)
{
	S32 done = 0;
	NetCommSendThread* st = link->send_thread;

	if(!link->send_queue_head)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	// Stop a running capture session.
	if (link->estr_send_capture_buf && InterlockedExchange(&sbNetStopCapture, 0))
	{
		estrDestroy(&spLastCapture);
		spLastCapture = link->estr_send_capture_buf;
		link->estr_send_capture_buf = NULL;
		printf("background thread has stopped capturing.\n");
	}

	st->send_buffer_used = 0;

	// Can't check link->send_queue_head here because linkDecrementOutbox is the last time
	// the link can be referenced, so we have to pre-check if the loop is done.

	while(!done)
	{
		Packet* pkt = link->send_queue_head;
		SOCKET	sock = link->sock;
		S32		failed =	link->noMoreSending ||
							sock == INVALID_SOCKET;
		
		link->send_queue_head = pkt->nextInSendThread;
		
		if(!link->send_queue_head){
			link->send_queue_tail = NULL;
			done = 1;
		}
		
		if(!failed){
			if(	st->send_buffer_used
				||
				!netNoSendBuffer &&
				link->send_queue_head)
			{
				// Use the send buffer if there is already something in the buffer or if there is
				// more than one packet in the queue and netNoSendBuffer isn't set.

				U32			bytesToSend = pkt->size;
				const U8*	data = pkt->data;

				while(bytesToSend){
					U32 bytesToCopy = MAX_SEND_BUFFER_BYTES - st->send_buffer_used;
					
					MIN1(bytesToCopy, bytesToSend);
					
					if(	st->send_buffer_used + bytesToCopy > st->send_buffer_size &&
						st->send_buffer_size < MAX_SEND_BUFFER_BYTES)
					{
						if(!st->send_buffer_size){
							assert(!st->send_buffer_used);
							st->send_buffer_size = pktGetQueueTotalBytes(pkt);
							
							MAX1(st->send_buffer_size, 1024);
						}else{
							while(st->send_buffer_used + bytesToCopy > st->send_buffer_size){
								st->send_buffer_size *= 2;
							}
						}

						MIN1(st->send_buffer_size, MAX_SEND_BUFFER_BYTES);

						st->send_buffer = realloc(st->send_buffer, st->send_buffer_size);
					}
					
					assert(st->send_buffer_used + bytesToCopy <= st->send_buffer_size);
					
					memcpy(	st->send_buffer + st->send_buffer_used,
							data,
							bytesToCopy);

					st->send_buffer_used += bytesToCopy;
					bytesToSend -= bytesToCopy;
					data += bytesToCopy;
					
					assert(st->send_buffer_used <= MAX_SEND_BUFFER_BYTES);
					
					if(st->send_buffer_used == MAX_SEND_BUFFER_BYTES){
						if(netSafeSend(link, sock, st->send_buffer, st->send_buffer_used) <= 0){
							failed = 1;
							break;
						}
						st->send_buffer_used = 0;
					}
				}
			}
			else if(netSafeSend(link, sock, pkt->data, pkt->size) <= 0){
				failed = 1;
			}
		}

		if(	PERFINFO_RUN_CONDITIONS &&
			pkt->msTimeWhenPktSendWasCalled)
		{
			U32 diff = timeGetTime() - pkt->msTimeWhenPktSendWasCalled;
			
			if(link->lag){
				ADD_MISC_COUNT(diff, "milliseconds since pktSend (test-lagged)");
			}else{
				ADD_MISC_COUNT(diff, "milliseconds since pktSend");
			}
		}
		
		pktFree(&pkt);
		
		// Send anything that's left in the buffer, if this is the last queued packet.

		if(	!failed &&
			st->send_buffer_used &&
			!link->send_queue_head)
		{
			if(netSafeSend(link, sock, st->send_buffer, st->send_buffer_used) <= 0){
				failed = 1;
			}
			st->send_buffer_used = 0;
		}

		if(failed){
			link->noMoreSending = 1;
		}

		// This decrement has to be the last thing done to the link because the owner thread will
		// free the link when the outbox is empty.

		linkDecrementOutbox(link);
	}

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

void u32ToBytes(U8 *b, U32 val)
{
	b[0] = (val & 255);
	b[1] = (val >> 8) & 255;
	b[2] = (val >> 16) & 255;
	b[3] = (val >> 24) & 255;
}

static void pktSendThreadQueue(Packet *pkt)
{
	NetLink	*link = pkt->link;
	static int secret_id;
	SOCKET	sock = link->sock;

	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	if(	link->noMoreSending ||
		sock == INVALID_SOCKET)
	{
		pktFree(&pkt);
		linkDecrementOutbox(link);
	}
	else
	{
		pkt->id = ++secret_id;

		if(link->flags & LINK_CRC)
		{
			U32	crc;
			
			PERFINFO_AUTO_START("pktCRC", 1);
			crc = cryptAdler32(pkt->data + NET_PACKET_SIZE_BYTES, pkt->size - NET_PACKET_SIZE_BYTES);

			pktGrow(pkt, NET_PACKET_CRC_HEADER_SIZE);
			memmove(pkt->data + NET_PACKET_SIZE_BYTES + NET_PACKET_CRC_HEADER_SIZE, pkt->data + NET_PACKET_SIZE_BYTES, pkt->size - NET_PACKET_SIZE_BYTES);
			pkt->size += NET_PACKET_CRC_HEADER_SIZE;

			u32ToBytes(pkt->data + NET_PACKET_SIZE_BYTES, crc);
			u32ToBytes(pkt->data + NET_PACKET_SIZE_BYTES + NET_PACKET_CRC_BYTES, ++link->sent_seq_id);
			PERFINFO_AUTO_STOP();
		}

		u32ToBytes(pkt->data, pkt->size);

		pktPushRing(pkt, &link->debug_early_send_ring, pkt->data, pkt->size);

		if(link->flags & LINK_COMPRESS)
		{
			PERFINFO_AUTO_START("pktCompress", 1);
			pktCompress(pkt, 1);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			ADD_MISC_COUNT(pkt->size, "bytesSentUncompressed");
		}

		if (link->flags & LINK_ENCRYPT)
		{
			PERFINFO_AUTO_START("cryptRc4", 1);
			cryptRc4(link->encrypt->encode,pkt->data,pkt->size);
			PERFINFO_AUTO_STOP();
		}

		PERFINFO_AUTO_START("pktQueue", 1);
		if(link->send_queue_tail)
		{
			link->send_queue_tail->nextInSendThread = pkt;
			link->send_queue_tail = pkt;
		}
		else
		{
			assert(!link->send_queue_head);
			link->send_queue_head = link->send_queue_tail = pkt;
		}
		assert(!pkt->nextInSendThread);
		PERFINFO_AUTO_STOP();
	}

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

// Called by linkFlushAndClose()
static void netCloseDispatch(NetCommSendThread* st, NetLink** linkptr, WTCmdPacket *packet)
{
	NetLink	*link;
	SOCKET sock;

	SIMPLE_CPU_DECLARE_TICKS(ticksStart);
	SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

	SIMPLE_CPU_TICKS(ticksStart);

	PERFINFO_AUTO_START_FUNC();
	link = *linkptr;
	sock = link->sock;
	pktSendThreadFlush(link);
	if(	!link->noMoreSending &&
		sock != INVALID_SOCKET)
	{
		shutdown(sock, SD_SEND);
	}
	link->shuttingDown = 0;
	link->disconnect_timer = timerCpuTicks();
	linkDecrementOutbox(link);
	PERFINFO_AUTO_STOP();

	SIMPLE_CPU_TICKS(ticksEnd);
	SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT, ticksStart, ticksEnd);
}

static void netForceFrameDispatch(NetCommSendThread* st, void* unused, WTCmdPacket *packet)
{
	SIMPLE_CPU_DECLARE_TICKS(ticksStart);
	SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

	SIMPLE_CPU_TICKS(ticksStart);

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_STOP();

	SIMPLE_CPU_TICKS(ticksEnd);
	SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT, ticksStart, ticksEnd);
}

static void netFlushDispatch(NetCommSendThread* st, NetLink **linkptr, WTCmdPacket *packet)
{
	NetLink* link = *linkptr;

	SIMPLE_CPU_DECLARE_TICKS(ticksStart);
	SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

	SIMPLE_CPU_TICKS(ticksStart);

	pktSendThreadFlush(link);
	linkDecrementOutbox(link);

	SIMPLE_CPU_TICKS(ticksEnd);
	SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT, ticksStart, ticksEnd);
}

static void netSendAndFlushDispatch(NetCommSendThread* st, Packet **pakptr, WTCmdPacket *packet)
{
	Packet *pkt = *pakptr;
	NetLink* link = pkt->link;

	SIMPLE_CPU_DECLARE_TICKS(ticksStart);
	SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

	SIMPLE_CPU_TICKS(ticksStart);

	pktSendThreadQueue(pkt);
	pktSendThreadFlush(link);

	SIMPLE_CPU_TICKS(ticksEnd);
	SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT, ticksStart, ticksEnd);
}

static void netSendDispatch(NetCommSendThread* st,Packet **pakptr, WTCmdPacket *packet)
{
	SIMPLE_CPU_DECLARE_TICKS(ticksStart);
	SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

	SIMPLE_CPU_TICKS(ticksStart);

	pktSendThreadQueue(*pakptr);

	SIMPLE_CPU_TICKS(ticksEnd);
	SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT, ticksStart, ticksEnd);
}

// Verify that the size of LinkStats::history is a power of 2.
STATIC_ASSERT(!(TYPE_ARRAY_SIZE(LinkStats, history) &
				(TYPE_ARRAY_SIZE(LinkStats, history) - 1)));

static Packet* linkGetPingPacket(NetLink *link)
{
	Packet			*pak;
	PacketHistory	*history;

	pktCreateWithCachedTracker(pak, link,PACKETCMD_PING);

	link->stats.history_idx = (++link->stats.history_idx) & (ARRAY_SIZE(link->stats.history)-1);
	history = &link->stats.history[link->stats.history_idx];

	history->real_id = link->stats.send.real_packets;
	history->time = timerCpuTicks();
	history->sent_bytes = link->send_pak_bytes;
	link->send_pak_bytes = 0;
	history->curr_recv = link->stats.recv.bytes;
	history->curr_real_recv = link->stats.recv.real_bytes;
	history->curr_sent = link->stats.send.bytes;
	history->curr_real_sent = link->stats.send.real_bytes;
	pktSendBits(pak, 32, history->real_id);
	return pak;
}

static int pktSendInternal(	Packet **pakptr,
							S32 flush)
{
	Packet*		pak = *pakptr;
	NetLink*	link = pak->link;
	U32			threadCmd = NETCMD_SEND;

	PERFINFO_AUTO_START_FUNC();	
	if(!link->connected || link->disconnected)
	{
		pktFree(pakptr);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	InterlockedIncrement(&link->outbox);
	wtEnterCmdCritical(link->send_thread->wt);
	wtQueueCmd(	link->send_thread->wt,
				flush ? NETCMD_SENDANDFLUSH : NETCMD_SEND,
				&pak,
				sizeof(pak));
	wtLeaveCmdCritical(link->send_thread->wt);
	
	if(flush){
		link->send_pak_bytes = 0;
	}
	
	if(	link->deliberate_packet_disconnect > 0 &&
		!--link->deliberate_packet_disconnect)
	{
		linkQueueRemove(link, "deliberate_packet_disconnect");
	}
	
	*pakptr = NULL;

	PERFINFO_AUTO_STOP();
	return 1;
}

// send a ping packet to the peer.
void linkSendKeepAlive(NetLink *link)
{
	Packet* pak = pktCreate(link,PACKETCMD_PING);

	pktSendBits(pak,32,0);
	pktSend(&pak);
}

void linkSendDisconnect(NetLink *link)
{
	Packet			*oob = pktCreate(link,PACKETCMD_DISCONNECT);

	pktSend(&oob);
}

void linkFlush(NetLink *link)
{
	if(!link->send_pak_bytes || linkIsFake(link))
		return;

	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	link->send_pak_bytes = 0;
	InterlockedIncrement(&link->outbox);
	wtQueueCmd(link->send_thread->wt, NETCMD_FLUSH, &link, sizeof(link));

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

void linkFlushAndClose(NetLink **linkInOut, const char *disconnect_reason)
{
	NetLink* link = SAFE_DEREF(linkInOut);
	
	if(!link)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	*linkInOut = NULL;
	ASSERT_FALSE_AND_SET(link->cleared_user_link_ptr);
	if(link->disconnected){
		linkRemove_wReason(&link, "linkFlushAndClose");
	}else{
		assert(eaFind(&link->listen->comm->flushed_disconnects, link) < 0);
		if(!link->disconnect_reason){
			link->disconnect_reason = FIRST_IF_SET(disconnect_reason, "linkFlushAndClose");
		}
		link->shuttingDown = 1;
		eaPush(&link->listen->comm->flushed_disconnects, link);
		linkSendLaggedPackets(link, 1);
		InterlockedIncrement(&link->outbox);
		wtQueueCmd(link->send_thread->wt, NETCMD_CLOSE, &link, sizeof(link));
	}
	
	PERFINFO_AUTO_STOP();
}

void linkSendLaggedPackets(NetLink *link,int force_flush)
{
	LaggedPacket	*lp;
	F32				lag;

	if (!link->pkt_lag_queue)
		return;
	lag = link->lag ? (link->lag + randInt(link->lag_vary)) / 1000.f : 0;
	lag /= 2; // since each end is adding lag
	while(lp = qPeek(link->pkt_lag_queue))
	{
		if(	!force_flush &&
			timerSeconds(timerCpuTicks() - lp->time) < lag)
		{
			break;
		}
		lp = qDequeue(link->pkt_lag_queue);
		pktSendInternal(&lp->pkt, lp->flush);
		free(lp);
	}
	if(force_flush)
	{
		linkDestroyLaggedPacketQueue(link);
	}
}

static U32 nlpo2(U32 x)
{
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	return(x+1);
}

static void pktAddToLagQueue(	NetLink* link,
								Packet** pakptr,
								S32 flush)
{
	LaggedPacket* lp = callocStruct(LaggedPacket);

	if (!link->pkt_lag_queue){
		link->pkt_lag_queue = createQueue();
		assert(eaFind(&link->listen->linksWithLaggedPackets, link) < 0);
		eaPush(&link->listen->linksWithLaggedPackets, link);
	}
	lp->time = timerCpuTicks();
	lp->pkt = *pakptr;
	*pakptr = NULL;
	lp->flush = !!flush;
	qEnqueue(link->pkt_lag_queue,lp);
}

static int pktSendOrLag(NetLink* link,
						Packet** pakptr,
						S32 flush)
{
	if(	link->lag
		||
		link->pkt_lag_queue &&
		qGetSize(link->pkt_lag_queue))
	{
		pktAddToLagQueue(link, pakptr, flush);
		return 1;
	}
	else
	{
		return pktSendInternal(pakptr, flush);
	}
}

static int pktSendHelper(	Packet **pakptr,
							S32 noFlush)
{
	int			result,size;
	Packet*		pak = SAFE_DEREF(pakptr);
	NetLink*	link;
	S32			flush = 0;
	
	if(!pak){
		return 0;
	}
	
	*pakptr = NULL;
	
	if(	PERFINFO_RUN_CONDITIONS &&
		!noFlush)
	{
		pak->msTimeWhenPktSendWasCalled = timeGetTime();
	}

	link = pak->link;

	//should only happen in rare debug cases or cases during shutdown when the transaction manager has lost connection
	//and thus creates fake packets, etc.
	if (!link)
	{
		pktFree(&pak);
		return 0;
	}

	// It's been true for a long time that you must wait for a NetLink to connect before you send data on it.
	// However, this was never enforced, and if you did it, depending on the timing, it would just drop your data,
	// and furthermore, become wedged and never connect.  The following will drop the data and devassert to let
	// the programmer know she did something wrong.
	if (!link->connected && !link->disconnected && !link->user_disconnect)
	{
		ErrorDetailsf("link id %u ptr %p size %d addr %u port %u", link->ID, link, pak->size,
			ntohl(link->addr.sin_addr.s_addr), ntohs(link->addr.sin_port));
		devassertmsg(link->connected, "NetLink must be connected before data can be sent");
		pktFree(&pak);
		return 0;
	}

	TrackerReportPacketSend(pak);
	

	assert(pak->sendable);

	size = pak->size;
	ADD_MISC_COUNT(size, "pktSend bytes");
	if (size > 512 && size < 4*65536)
		link->default_pktsize = nlpo2(size);

	link->stats.send.packets++;
	link->stats.send.real_packets++;
	link->stats.send.bytes += pak->size;

	if(link->flags & LINK_CRC)
	{
		link->stats.send.bytes += 8;
		link->send_pak_bytes += 8;
	}

	link->send_pak_bytes += pak->size;

	if(	!noFlush &&
		(	link->flags & LINK_FORCE_FLUSH
			||
			link->flush_limit &&
			link->send_pak_bytes >= link->flush_limit)
		||
		link->send_pak_bytes >= MAX_SEND_BUFFER_BYTES)
	{
		flush = 1;
	}

	// If the link is not compressed, mark the packet so that it won't be compressed.
	pak->no_compress = link->no_compress;

	result = pktSendOrLag(link, &pak, flush && !link->auto_ping);

	if(	result &&
		link->auto_ping)
	{
		Packet* pakPing = linkGetPingPacket(link);

		result = pktSendOrLag(link, &pakPing, flush);
	}



	return result;
}

int pktSend(Packet **pakptr)
{
	int result;
	PERFINFO_AUTO_START_FUNC();
	result = pktSendHelper(pakptr, 0);
	PERFINFO_AUTO_STOP();
	return result;
}

int pktSendThroughLink(Packet **pakptr, NetLink *link)
{
	if (*pakptr)
	{
		(*pakptr)->link = link;
		return pktSend(pakptr);
	}

	return 0;
}


int pktSendNoFlush(Packet **pakptr)
{
	int result;
	PERFINFO_AUTO_START_FUNC();
	result = pktSendHelper(pakptr, 1);
	PERFINFO_AUTO_STOP();
	return result;
}

int DEFAULT_LATELINK_comm_commandqueue_size(void)
{
	return 4096;
}

LATELINK;
int comm_commandqueue_size(void);

// Set commCmdQueueSize to 0 to force the production mode value, otherwise use >1 to set it.
static S32 commCmdQueueSize = 1;
AUTO_CMD_INT(commCmdQueueSize, commCmdQueueSize);

void commInitSendThread(NetComm *comm,int num_threads)
{
	int		i;

	for(i=0;i<num_threads;i++)
	{
		U32 cmdQueueSize = commCmdQueueSize > 1 ?
								MAX(commCmdQueueSize, 128) :
								isProductionMode() || !commCmdQueueSize ?
									comm_commandqueue_size() :
									4096;
		NetCommSendThread* st = callocStruct(NetCommSendThread);
		
		cmdQueueSize = BIT(highBitIndex(cmdQueueSize - 1) + 1);

		//The command queue size might need to be bigger for things that are sending lots of things (e.g. the ObjectDB)
		st->wt = wtCreate(cmdQueueSize, 2, NULL, "pktSendThread");
		wtRegisterCmdDispatch(st->wt, NETCMD_SEND, netSendDispatch);
		wtRegisterCmdDispatch(st->wt, NETCMD_SENDANDFLUSH, netSendAndFlushDispatch);
		wtRegisterCmdDispatch(st->wt, NETCMD_FLUSH, netFlushDispatch);
		wtRegisterCmdDispatch(st->wt, NETCMD_CLOSE, netCloseDispatch);
		wtRegisterCmdDispatch(st->wt, NETCMD_FORCEFRAME, netForceFrameDispatch);
		wtSetProcessor(st->wt, THREADINDEX_NETSEND);
		wtSetThreaded(st->wt, !comm->no_send_thread, 0, false);
		wtStart(st->wt);
		eaPush(&comm->send_threads, st);
	}
}

void commForceThreadFrame(NetComm* comm)
{
	EARRAY_CONST_FOREACH_BEGIN(comm->send_threads, i, isize);
		wtQueueCmd(comm->send_threads[i]->wt, NETCMD_FORCEFRAME, NULL, 0);
	EARRAY_FOREACH_END;
}
