#pragma once

typedef struct NetLink NetLink;
typedef struct Packet Packet;
void HandleLauncherRequestsLocalExesForMirroring(NetLink *pLink, Packet *pPacket);
void HandleLauncherGotLocalExesForMirroring(NetLink *pLink, Packet *pPacket);

extern bool gbMirrorLocalExecutables;