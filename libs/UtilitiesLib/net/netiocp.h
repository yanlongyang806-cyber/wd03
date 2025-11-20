#ifndef _NETIOCP_H
#define _NETIOCP_H

#if _PS3

#define netListenStartIocpAccept(x) __STATIC_ASSERT(0)
#define iocpProcessPackets(x,y,z)
#define safeWSARecv(x,y,z,a)  0//__STATIC_ASSERT(0)
#define addListenAcceptSock(x) __STATIC_ASSERT(0)

#else

#ifdef _IOCP
void netListenStartIocpAccept(NetListen *nl);
void iocpProcessPackets(NetComm *comm, S32 timeoutOverrideMilliseconds, U32 maxMilliseconds);
int safeWSARecv(SOCKET sock, int len, Packet *pak, const char* debugLocName);
void addListenAcceptSock(NetListen *nl);
#else
#define netListenStartIocpAccept(x)
#define iocpProcessPackets(x,y,z)
#define safeWSARecv(x,y,z,a) 0
#define addListenAcceptSock(x)
#endif

#endif

#endif
