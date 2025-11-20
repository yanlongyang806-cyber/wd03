#ifndef _NETHANDSHAKE_H
#define _NETHANDSHAKE_H

typedef struct NetLink NetLink;

void linkCompressInit(NetLink *link);
void linkInitEncryption(NetLink *link,char *their_key_str);
int getConnectionInfo(NetLink* link,char *data,LinkFlags *flags,U32 *protocol,U8 **their_key_str,char **err);
void netFirstServerPacket(NetLink *link,Packet *pak);
void netFirstClientPacket(NetLink *link,Packet *pak);
int netSendConnectPkt(NetLink *link);

#endif
