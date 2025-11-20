#pragma once
GCC_SYSTEM

#if !_PS3

C_DECLARATIONS_BEGIN

extern void xsockInit();

#ifndef DECLARE_FUNCS
#define DECL_EXTERN extern
#define DEFAULT_INIT

#define socket(af,type,protocol) ptrSocketCreate(af,type,protocol)
#define closesocket(s) fptrSocketClose(s)
#define ioctlsocket(s,cmd,argp) fptrSocketIOCTLSocket(s,cmd,argp)
#define setsockopt(s,level,optname,optval,optlen) fptrSocketSetSockOpt(s,level,optname,optval,optlen)
#define getsockopt(s,level,optname,optval,optlen) fptrSocketGetSockOpt(s,level,optname,optval,optlen)
#define getsockname(s,name,namelen) fptrSocketGetSockName(s,name,namelen)
#define getpeername(s,name,namelen) fptrSocketGetPeerName(s,name,namelen)
#define bind(s,name,namelen) fptrSocketBind(s,name,namelen)
#define connect(s,name,namelen) fptrSocketConnect(s,name,namelen)
#define listen(s,backlog) fptrSocketListen(s,backlog)
#define accept(s,addr,addrlen) fptrSocketAccept(s,addr,addrlen)
#define select(nfds,readfds,writefds,exceptfds,timeout) fptrSocketSelect(nfds,readfds,writefds,exceptfds,timeout)
#define recv(s,buf,len,flags) fptrSocketRecv(s,buf,len,flags)
#define recvfrom(s,buf,len,flags,from,fromlen) fptrSocketRecvFrom(s,buf,len,flags,from,fromlen)
#define send(s,buf,len,flags) fptrSocketSend(s,buf,len,flags)
#define sendto(s,buf,len,flags,to,tolen) fptrSocketSendTo(s,buf,len,flags,to,tolen)
#define inet_addr(cp) fptrSocketInet_Addr(cp)
#define WSAGetLastError fptrWSAGetLastError
#define WSAStartup(wVersionRequested,lpWSAData) fptrWSAStartup(wVersionRequested,lpWSAData)
#define WSACleanup fptrWSACleanup

#else
#define DECL_EXTERN
#define DEFAULT_INIT = xsockDefaultInit
// Default init function if anyone (e.g. utilities, gimme, etc) calls WSAStartup without calling xsockInit() first
int WSAAPI xsockDefaultInit(__in WORD wVersionRequested, __out LPWSADATA lpWSAData)
{
	xsockInit();
	return WSAStartup(wVersionRequested, lpWSAData);
}
#endif


DECL_EXTERN SOCKET (WSAAPI *ptrSocketCreate)(__in int af,__in int type, __in int protocol);
DECL_EXTERN int (WSAAPI *fptrSocketClose)(__in SOCKET s);
DECL_EXTERN int (WSAAPI *fptrSocketIOCTLSocket)(__in SOCKET s, __in long cmd, __inout u_long FAR * argp);
DECL_EXTERN int (WSAAPI *fptrSocketSetSockOpt)(__in SOCKET s, __in int level, __in int optname, __in_bcount_opt(optlen) const char FAR * optval, __in int optlen);
DECL_EXTERN int (WSAAPI *fptrSocketGetSockOpt)(__in SOCKET s, __in int level, __in int optname, __out_ecount_part(*optlen, *optlen) char FAR * optval, __inout int FAR * optlen);
DECL_EXTERN int (WSAAPI *fptrSocketGetSockName)(__in SOCKET s, __out_bcount(namelen) struct sockaddr FAR * name, __inout int FAR * namelen);
DECL_EXTERN int (WSAAPI *fptrSocketGetPeerName)(__in SOCKET s, __out_bcount(namelen) struct sockaddr FAR * name, __inout int FAR * namelen);
DECL_EXTERN int (WSAAPI *fptrSocketBind)(__in SOCKET s, __in_bcount(namelen) const struct sockaddr FAR * name, __in int namelen);
DECL_EXTERN int (WSAAPI *fptrSocketConnect)(__in SOCKET s, __in_bcount(namelen) const struct sockaddr FAR * name, __in int namelen);
DECL_EXTERN int (WSAAPI *fptrSocketListen)(__in SOCKET s, __in int backlog);
DECL_EXTERN SOCKET (WSAAPI *fptrSocketAccept)(__in SOCKET s, __out_bcount(addrlen) struct sockaddr FAR * addr, __inout int FAR * addrlen);
DECL_EXTERN int (WSAAPI *fptrSocketSelect)(__in int nfds, __inout_opt fd_set FAR * readfds, __inout_opt fd_set FAR * writefds, __inout_opt fd_set FAR * exceptfds, __in_opt const struct timeval FAR * timeout);
DECL_EXTERN int (WSAAPI *fptrSocketRecv)(__in SOCKET s, __out_ecount(len) char FAR * buf, __in int len, __in int flags);
DECL_EXTERN int (WSAAPI *fptrSocketRecvFrom)(__in SOCKET s, __out_ecount(len) char FAR * buf, __in int len, __in int flags, __out_bcount_opt(fromlen) struct sockaddr FAR * from, __inout_opt int FAR * fromlen);
DECL_EXTERN int (WSAAPI *fptrSocketSend)(__in SOCKET s, __in_bcount(len) const char FAR * buf, __in int len, __in int flags );
DECL_EXTERN int (WSAAPI *fptrSocketSendTo)(__in SOCKET s, __in_bcount(len) const char FAR * buf, __in int len, __in int flags, __in_bcount_opt(tolen) const struct sockaddr FAR * to, __in int tolen);
DECL_EXTERN int (WSAAPI *fptrWSAGetLastError)(void);
DECL_EXTERN int (WSAAPI *fptrWSAStartup)(__in WORD wVersionRequested, __out LPWSADATA lpWSAData) DEFAULT_INIT;
DECL_EXTERN int (WSAAPI *fptrWSACleanup)(void);
DECL_EXTERN unsigned long (WSAAPI *fptrSocketInet_Addr)(__in const char FAR * cp);

C_DECLARATIONS_END

#endif
