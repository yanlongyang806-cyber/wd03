#pragma once
GCC_SYSTEM

#if _PS3

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <netex/errno.h>
#include <netex/net.h>
#include <netdb.h>
#include <arpa/inet.h>

typedef int SOCKET;

#define INVALID_SOCKET  (SOCKET)(~0)

#define closesocket(x) socketclose(x)


#define SOMAXCONN 2

#define WSAGetLastError() sys_net_errno

#define WSAEWOULDBLOCK  SYS_NET_EWOULDBLOCK
#define WSAEISCONN      SYS_NET_ERROR_EISCONN

#define SD_RECEIVE SHUT_RD	
#define SD_SEND    SHUT_WR	 
#define SD_BOTH    SHUT_RDWR

#else

#ifdef _WIN32
#include "wininclude.h"
#ifndef _NOXLSP
#include "../net/xsock.h"
#endif


#define s_addr S_un.S_addr
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#define closesocket(x) close(x)
#define ioctlsocket(x,y,z) ioctl(x,y,(U32)z)
#define Sleep(x) usleep(x*1000)
#endif

#endif

#include "stdtypes.h"

C_DECLARATIONS_BEGIN

void sockSetAddr(struct sockaddr_in *addr,unsigned int ip,int port);
int sockBind(int sock,const struct sockaddr_in *name);
void sockSetBlocking(int fd, int block);
void sockSetDelay(int fd, int delay);
void sockStart(void);
void sockStop(void);
char *makeIpStr(U32 ip);
char *makeHostNameStr(U32 ip);
int isLocalIp(U32 ip);
int numMatchingOctets(U32 ip1, U32 ip2);
U32 ChooseIP(U32 srcip, U32 ip1, U32 ip2);
char * GetIpStr(U32 ip, char * buf, int buf_size);
void setIpList(struct hostent *host_ent,U32 *ip_list);
int setHostIpList(U32 ip_list[2]);
U32 getHostLocalIp(void);
U32 getHostPublicIp(void);
U32 ipFromString(const char *s);
U32 ipFromStringWithError(const char *s, int *error);

// Normally, we map localhost to our actual IP.  This can be disabled for performance, at the risk of causing terrible breakage.
bool sockDontConvertLocalhost(bool disable);

//can be run before networking stuff is initialized. String must be "n.n.n.n"
U32 ipFromNumericString(const char *s);

//note that these two functions will NOT work correctly (necessarily) if you pass in an IP that's alreayd in string
//form... so if you pass in "1.2.3.4" they will just return that IP, regardless of whether it's public or local
//
//for hostnames, they should work correctly
U32 ipLocalFromString(const char *s);
U32 ipPublicFromString(const char *s);
const char* sockGetReadableError(int errVal);
int isDisconnectError(int errVal);
char* stringFromAddr(struct sockaddr_in *addr);

//generally should call safeCloseLinkSocket instead
void safeCloseSocket(SOCKET *sock_ptr);


int isIp(const char *s);


//given "foo.bar.com", finds all non-zero IPs of "foo.bar.com", "foo0.bar.com", "foo1.bar.com", etc.
//up to "foo9.bar.com", and pushes all unique ones (pooled) into pppOutIPs (as "1.2.3.4" strings)
//
//(used for redundant controller trackers, etc.)
void GetAllUniqueIPs(char *pURL, const char ***pppOutIPs);

SOCKET socketCreate(S32 af, S32 type, S32 protocol);

//when an IP lookup fails, it's quite likely that it's due to something like a DNS stall that causes blocking. Therefore, for
//some applications, we don't want to retry repeatedly (for instance, a controller can't talk to a controller tracker). For other
//applications, we want to try instantly. Set this to a non-zero # of seconds to control the delay before retrying
//
//Note that this can be overridden via the command -DelayBeforeRetryingFailedIPLookup
extern int giDelayBeforeRetryingFailedIPLookup_Default;


// response from inet_addr("127.0.0.1")
#define LOCALHOST_ADDR 0x0100007f
C_DECLARATIONS_END

