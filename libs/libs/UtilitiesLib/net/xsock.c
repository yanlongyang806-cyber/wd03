#if !_PS3
#include "wininclude.h"

#define DECLARE_FUNCS
#include "xsock.h"



void xsockInit()
{
	if (ptrSocketCreate) // don't set them if someone has already overridden them
		return;
	ptrSocketCreate			= socket;
	fptrSocketClose			= closesocket;
	fptrSocketIOCTLSocket	= ioctlsocket;
	fptrSocketSetSockOpt	= setsockopt;
	fptrSocketGetSockOpt	= getsockopt;
	fptrSocketGetSockName	= getsockname;
	fptrSocketGetPeerName	= getpeername;
	fptrSocketBind			= bind;
	fptrSocketConnect		= connect;
	fptrSocketListen		= listen;
	fptrSocketAccept		= accept;
	fptrSocketSelect		= select;
	fptrSocketRecv			= recv;
	fptrSocketRecvFrom		= recvfrom;
	fptrSocketSend			= send;
	fptrSocketSendTo		= sendto;
	fptrSocketInet_Addr		= inet_addr;	
	fptrWSAGetLastError		= WSAGetLastError;
	fptrWSAStartup			= WSAStartup;
	fptrWSACleanup			= WSACleanup;
}
#endif
