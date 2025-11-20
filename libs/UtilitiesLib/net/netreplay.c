#include "net.h"
#include "nethandshake.h"
#include "netlink.h"
#include "netreceive.h"
#include "netpacket.h"
#include "netprivate.h"
#include "netreplay.h"
#include "StringUtil.h"

// Replay state
typedef struct NetReplay
{
	NetLink *link;
	bool callback_called;
} NetReplay;

// Handle a replayed packet.
static void netReplayPacketCallback(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	NetReplay *replay = user_data;
	replay->callback_called = true;
}

// Initialize a replay object.
const char *netReplayInit(NetReplay **replay, const char *stream, size_t stream_len)
{
	const char end_handshake[] = "\r\n\r\n";
	const char *end;
	char *handshake_data;
	NetLink *link;
	LinkFlags flags = 0;
	U32 protocol = 0;
	U8 *their_key_str = 0;
	char *err = 0;
	int result;

	// Allocate replay object.
	*replay = calloc(1, sizeof(**replay));

	// Find end of handshake.
	end = strnstr(stream, stream_len, end_handshake);
	assertmsg(end, "Unable to find stream start");
	handshake_data = malloc(end - stream);
	memcpy(handshake_data, stream, end - stream);

	// Create fake link.
	link = linkCreateFakeLink();
	(*replay)->link = link;
	link->disconnected = false;
	link->recv_pak = pktCreateRaw(link);
	link->user_data = *replay;

	// Initialize fake link parameters from handshake.
	result = getConnectionInfo(link, handshake_data, &flags, &protocol, &their_key_str, &err);
	assertmsgf(result, "getConnectionInfo() failed: %s", err);
	free(handshake_data);
	link->flags |= flags;
	link->protocol = protocol;
	link->connected = 1;
	if (flags & LINK_ENCRYPT)
		linkInitEncryption(link,their_key_str);
	if (flags & LINK_COMPRESS)
		linkCompressInit(link);
	link->recv_pak->has_verify_data = !!(link->flags & LINK_PACKET_VERIFY);

	// Create fake NetListen.
	link->listen = calloc(1, sizeof(*link->listen));
	link->listen->message_callback = netReplayPacketCallback;

	// Create fake NetComm.
	link->listen->comm = calloc(1, sizeof(*link->listen->comm));

	// Return pointer to just after handshake.
	return end + sizeof(end_handshake) - 1;
}

// Destroy a replay object.
void netReplayDestroy(NetReplay *replay)
{
	NetListen *listen = replay->link->listen;
	replay->link->connected = false;
	linkFree(replay->link);
	free(listen->comm);
	free(listen);
}

// Replay and find the next packet demarcation point.
const char *netReplayNext(NetReplay *replay, const char *stream, size_t stream_len)
{
	const char *pos = stream;

	// Play bytes one-by-one until the callback is called.
	replay->callback_called = false;
	do
	{
		NetLink *link = replay->link;
		Packet *recv_pak = link->recv_pak;
		if (pos == stream + stream_len)
			return NULL;
		recv_pak->data[recv_pak->size] = *pos;
		linkReceiveCore(link, 1);
		++pos;
	} while (!replay->callback_called);

	return pos;
}
