#pragma once

typedef U32 ContainerID;
typedef struct Packet Packet;
typedef struct NetLink NetLink;

bool TestServer_IsSharded(void);
void TestServer_InitSharded(void);
void TestServer_ShardedReady(void);
void TestServer_ShardedTick(F32 fTotalElapsed, F32 fElapsed);

void TestServer_Client_MessageCB(Packet *pkt, int cmd, NetLink *link, int index, void *pClient);
void TestServer_Client_DisconnectCB(NetLink *link, int index, void *pClient);