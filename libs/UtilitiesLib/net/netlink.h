#ifndef _LINK_H
#define _LINK_H

#if _PS3
#include "sock.h"
#elif _XBOX
#include "wininclude.h"
#include "winsockx.h"
#else
#include "winsock2.h"
#endif

void linkFree(NetLink *link);
#if 0
#define linkStatus(link, msg) printf("%d %08p.%08p %s\n",link->ID,link->listen,link,msg);
#else
#define linkStatus(...)
#endif
NetLink *linkCreate(SOCKET sock,NetListen *nl, LinkType eType, const char *file, int line);
int linkActivate(NetLink *link,int bytes_xferred);
void linkGrowRecvBuf(NetLink *link,int bytes_xferred);
int linkHasValidSocket(NetLink *link);
int linkGetRawDataRemaining(NetLink *link);
const char *linkError(NetLink *link);
U32 linkGetPingAckReceiveCount(NetLink *link);

void safeCloseLinkSocket(NetLink *link, const char* reason);
void linkDestroyLaggedPacketQueue(NetLink* link);


#endif
