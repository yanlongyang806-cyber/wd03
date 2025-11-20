#ifndef _NETPACKET_H
#define _NETPACKET_H

int pktUncompress(Packet *pkt,int new_packed_bytes);
void *pktReallocData(void *data, S64 size, MEM_DBG_PARMS_VOID);
#define pktGrow(pkt,bytes) if ((bytes) + pkt->size > pkt->max_size) _pktGrow(pkt,bytes);
void _pktGrow(Packet *pkt,int bytes);

#define PACKET_SIZE	(512)

__forceinline static U32 bytesToU32(const U8 *b)
{
	return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

#endif
