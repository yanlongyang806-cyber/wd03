#include "sock.h"
#include "timing.h"
#include <string.h>
#include <stdio.h>


#include "Estring.h"
#include "earray.h"

#include "StringCache.h"
#include "stashTable.h"

// Number of seconds to cache the local IP addresses
#define HOST_IP_LIST_CACHE_SECS 1

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

#if _PS3

#define NUM_CONTEXTS 4

#else

#if _XBOX
	#pragma comment(lib, "Xonline.lib")
#endif

//hooks used by Xlive code if present
int  (*sockStart_hook)(void) = NULL;
void (*sockStop_hook)(void) = NULL;
void (*sockSetAddr_hook)(struct sockaddr_in *addr,unsigned int ip,int port) = NULL;

#endif

// Set by sockDontConvertLocalhost()
bool sock_dont_convert_localhost = false;

int sockGetError(void);

void	sockSetAddr(struct sockaddr_in *addr,unsigned int ip,int port)
{
#if _PS3
    addr->sin_len = sizeof(struct sockaddr_in);
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = ip;
	addr->sin_port = port;
#else
	//XLive hook
	if ( sockSetAddr_hook != NULL )
	{
		sockSetAddr_hook(addr, ip, port);
	}
	else
	{
		memset(addr,0,sizeof(struct sockaddr_in));
		addr->sin_family=AF_INET;
		addr->sin_addr.s_addr=ip;
		addr->sin_port = htons((U16)port);
	}
#endif
}

int	sockBind(int sock,const struct sockaddr_in *name)
{
	if (bind (sock,(struct sockaddr *) name, sizeof(struct sockaddr_in)) >= 0)
        return 1;

	return 0;
}

void	sockSetBlocking(int fd,int block)
{
    int		noblock;
    noblock = !block;
#if _PS3
    {
        int ret;
        ret = setsockopt(fd, SOL_SOCKET, SO_NBIO, &noblock, sizeof(noblock));
        assert(!ret);
    }
#else
	ioctlsocket (fd, FIONBIO, &noblock);
#endif
}

void sockSetDelay(int fd, int delay)
{
	int noDelay;
	noDelay = !delay;
#if _PS3
    {
        int ret;
	    ret = setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(void *)&noDelay,sizeof(noDelay));
        assert(!ret);
    }
#else
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(void *)&noDelay,sizeof(noDelay));
#endif
}

void	sockStart()
{
#if _PS3
    static bool bAlreadyCalled = 0;
    int ret;

    if ( bAlreadyCalled )
	    return;

    {
	    sys_net_initialize_parameter_t t;
	    static uint8_t __ALIGN(128) memory[NUM_CONTEXTS * 16*1024];

	    t.memory = memory;
	    t.memory_size = sizeof(memory);
	    t.flags = 0;
	    ret = sys_net_initialize_network_ex(&t);

        if (ret < 0) {
	        printf("sys_net_initialize_network() failed\n");
	        exit(1);
        }
    }

    bAlreadyCalled = true;

#else
    static bool bAlreadyCalled = 0;

    WORD wVersionRequested;  
    WSADATA wsaData; 
    int err; 

    if ( bAlreadyCalled )
	    return;

    //XLive hook
    if ( sockStart_hook != NULL )
    {
	    if ( !sockStart_hook() )
	    {
		    bAlreadyCalled = true;
	    }
	    else
	    {
		    fprintf(stderr,"XLive sock error..");
	    }
	    return;
    }

    wVersionRequested = MAKEWORD(2, 2); 
     
    #ifndef _NOXLSP
	    xsockInit();
    #endif
    #if _XBOX 
	    {
		    XNetStartupParams xnsp;
		    memset(&xnsp, 0, sizeof(xnsp));
		    xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
		    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
		    err = XNetStartup(&xnsp);
	    }	
    #endif
	    err = WSAStartup(wVersionRequested, &wsaData); 
     
	    if (err)
		    fprintf(stderr,"Winsock error..");
	    else
		    bAlreadyCalled = true;
#endif
}


void sockStop()
{
#if _PS3

    sys_net_finalize_network();

#else
	//XLive Hook
	if ( sockStop_hook != NULL )
	{
		sockStop_hook();
		return;
	}

	WSACleanup();
#if _XBOX 
	XNetCleanup(NULL);
#endif
#endif
}

//this function is kinda-not-safe, but can be called this many
//times at once before it fails
#define MAX_IPSTRS_SIMUL 32

char *makeIpStr(U32 ip)
{
//16 is the maximum size needed for an IP... 3+1+3+1+3+1+3+1 (for null terminator)

	static	char	buf[MAX_IPSTRS_SIMUL][16];
	char			*s;
	static int idx = 0;
	s = buf[InterlockedIncrement(&idx) % MAX_IPSTRS_SIMUL];


	//Convert from network order before generating
	ip = ntohl(ip);
	snprintf_s(s, ARRAY_SIZE_CHECKED(buf[0]), "%d.%d.%d.%d", (ip>>24)&255, (ip>>16)&255, (ip>>8)&255, ip&255);
	return s;
}

U32 ipFromNumericString(const char *s)
{
	U32 p1, p2, p3, p4;
	
	char *pNextPeriod;
	p1 = atoi(s);
	pNextPeriod = strchr(s, '.');
	if (!pNextPeriod)
	{
		return 0;
	}
	p2 = atoi(pNextPeriod + 1);
	pNextPeriod = strchr(pNextPeriod+1, '.');
		if (!pNextPeriod)
	{
		return 0;
	}
	p3 = atoi(pNextPeriod + 1);
	pNextPeriod = strchr(pNextPeriod+1, '.');
		if (!pNextPeriod)
	{
		return 0;
	}
	p4 = atoi(pNextPeriod + 1);

	return ntohl((p1 << 24) | (p2 << 16) | (p3 << 8) | p4);
}




char *makeHostNameStr(U32 ip)
#if !PLATFORM_CONSOLE
{
struct hostent	*host_ent;
U32		num_ip;
char	*s;
U32		start=0;
static	int state=0;

	if (!state) {
		start = timeSecondsSince2000();
	}

	s = makeIpStr(ip);
	if (state==-1)
		return s;
	num_ip = inet_addr(s);

	host_ent = gethostbyaddr((char*)&num_ip,4,AF_INET);

	if (!state) {
		if (timeSecondsSince2000() - start > 5) {
			// More than 5 seconds to do a host lookup!  Too slow!
			state = -1;
		} else {
			state = 1;
		}
	}

	if (!host_ent)
		return s;
	return host_ent->h_name;
}
#else
{
	return makeIpStr(ip);
}
#endif

// From RFC 1918
//   The Internet Assigned Numbers Authority (IANA) has reserved the
//   following three blocks of the IP address space for private internets:
//
//     10.0.0.0        -   10.255.255.255  (10/8 prefix)
//     172.16.0.0      -   172.31.255.255  (172.16/12 prefix)
//     192.168.0.0     -   192.168.255.255 (192.168/16 prefix)
//     127.0.0.1                           (localhost)
int isLocalIp(U32 ip)
{
	U8	a,b,c,d;

	a = ip & 255;
	b = (ip >> 8) & 255;
	c = (ip >> 16) & 255;
	d = (ip >> 24) & 255;
	if (a == 10)
		return 1;
	if (a == 172 && (b >= 16 && b <= 31))
		return 1;
	if (a == 192 && b == 168)
		return 1;
	if (a == 127 && b == 0 && c == 0 && d == 1)
		return 1;
	return 0;
}

int numMatchingOctets(U32 ip1, U32 ip2)
{
	U8	a1,b1,c1,d1;
	U8	a2,b2,c2,d2;
	int count = 0;

	a1 = ip1 & 255;
	b1 = (ip1 >> 8) & 255;
	c1 = (ip1 >> 16) & 255;
	d1 = (ip1 >> 24) & 255;

	a2 = ip2 & 255;
	b2 = (ip2 >> 8) & 255;
	c2 = (ip2 >> 16) & 255;
	d2 = (ip2 >> 24) & 255;

	if ( a1 == a2 )
	{
		count++;

		if ( b1 == b2 )
		{
			count++;

			if ( c1 == c2 )
			{
				count++;

				if ( d1 == d2 )
				{
					count++;
				}
			}
		}
	}

	return count;
}


U32 ChooseIP(U32 srcip, U32 ip1, U32 ip2)
{
	int nummatch1, nummatch2;

	nummatch1 = numMatchingOctets(srcip, ip1);
	nummatch2 = numMatchingOctets(srcip, ip2);

	if ( nummatch1 >= nummatch2 )
	{
		return ip1;
	}
	else
	{
		return ip2;
	}
}

char * GetIpStr(U32 ip, char * buf, int buf_size)
{
	sprintf_s(SAFESTR2(buf), "%d.%d.%d.%d", ip&255, (ip>>8)&255, (ip>>16)&255, (ip>>24)&255);
	return buf;
}





#if !_XBOX
void setIpList(struct hostent *host_ent,U32 *ip_list)
{
    U32 ip_local,ip_remote,*addr,t;

    if(host_ent->h_length < 2) {
        ip_local = ip_remote = *(U32 *)&host_ent->h_addr;
    } else {

	    ip_local = *((U32 *)host_ent->h_addr_list[0]);
	    addr = (U32*)host_ent->h_addr_list[1];
	    if (addr)
		    ip_remote = *addr;
	    else
		    ip_remote = ip_local;

	    if (isLocalIp(ip_remote) && !isLocalIp(ip_local))
	    {
		    t = ip_local;
		    ip_local = ip_remote;
		    ip_remote = t;
	    }
    }
	ip_list[0] = ip_local;
	ip_list[1] = ip_remote;
}

static struct hostent* timedGetHostByName(const char* name)
{
	struct hostent* host_ent;
	
	PERFINFO_AUTO_START_BLOCKING("gethostbyname", 1);
	host_ent = gethostbyname(name);
	PERFINFO_AUTO_STOP();
	
	return host_ent;
}

int setHostIpList(U32 ip_list[2])
{
	static U32 saved_ip_list[2];
	static char	buf[256];
	static int timer;
	static CRITICAL_SECTION cs;
	struct hostent *host_ent;
	int result;

	// Get the local hostname; assume that it never changes.
	ATOMIC_INIT_BEGIN;
	result = gethostname(buf,sizeof(buf));
	assertmsg(!result, "gethostbyname()");
	timer = timerAlloc();
	timerAdd(timer, HOST_IP_LIST_CACHE_SECS*2);		// Make sure we always do an actual lookup the first time.
	InitializeCriticalSection(&cs);
	ATOMIC_INIT_END;

	// If we've recently asked for our own IP, use the cached version.
	EnterCriticalSection(&cs);
	if (timerElapsed(timer) < HOST_IP_LIST_CACHE_SECS)
	{
		memcpy(ip_list, saved_ip_list, sizeof(saved_ip_list));
		LeaveCriticalSection(&cs);
		return 1;
	}
	
	// Our cache is expired: Do another host lookup.
	// We observed that this can be rather slow, for unknown reasons.  Perhaps it is checking the 'hosts' file and similar.
	host_ent = timedGetHostByName(buf);

	// This seems lame, but we're preserving previous behavior.
	if (!host_ent)
	{
		LeaveCriticalSection(&cs);
		return 0;
	}

	setIpList(host_ent,saved_ip_list);
	memcpy(ip_list, saved_ip_list, sizeof(saved_ip_list));
	timerStart(timer);
	LeaveCriticalSection(&cs);

	return 1;
}

U32 getHostLocalIp()
{
	U32	ip_list[2];

	if (!setHostIpList(ip_list))
		return 0;
	return ip_list[0];
}

U32 getHostPublicIp()
{
	U32	ip_list[2];

	if (!setHostIpList(ip_list))
		return 0;
	return ip_list[1];
}

#else

U32 getHostPublicIp()
{
	return 0;
}

U32 getHostLocalIp()
{
	return 0;
}

#endif

int isIp(const char *s)
{
	for(;*s;s++)
	{
		if (!isdigit(*s) && *s != '.')
			return 0;
	}
	return 1;
}

U32 ipFromString(const char *s)
{
	int dummy;
	return ipFromStringWithError(s, &dummy);
}


static CRITICAL_SECTION sFailedIPLookup;
static StashTable sFailedIPs;

AUTO_RUN_EARLY;
void InitFailedIPLookup(void)
{
	InitializeCriticalSection(&sFailedIPLookup);
	sFailedIPs = stashTableCreateWithStringKeys(4, StashDeepCopyKeys_NeverRelease);
}


//if an IP lookup of a string fails, don't try again for this many seconds
static int siDelayBeforeRetryingFailedIPLookup_fromCmd = -1;
AUTO_CMD_INT(siDelayBeforeRetryingFailedIPLookup_fromCmd, DelayBeforeRetryingFailedIPLookup);

int giDelayBeforeRetryingFailedIPLookup_Default = 0;

int DelayBeforeRetryingFailedIPLookup(void)
{
	if (siDelayBeforeRetryingFailedIPLookup_fromCmd != -1)
	{
		return siDelayBeforeRetryingFailedIPLookup_fromCmd;
	}

	return giDelayBeforeRetryingFailedIPLookup_Default;
}


U32 ipFromStringWithError(const char *s, int *error)
{
	U32 ret;
	char	ip_str[100];

	*error = 0;

	if (!s)
		return 0;
		
	PERFINFO_AUTO_START_FUNC();

	strcpy(ip_str,s);
	if (stricmp(s,"localhost")==0 || stricmp(s,"127.0.0.1")==0)
	{
		if (sock_dont_convert_localhost)
		{
			PERFINFO_AUTO_STOP();
			return LOCALHOST_ADDR;
		}
#if !_XBOX
		strcpy(ip_str,makeIpStr(getHostLocalIp()));
#else
		assert(0);
#endif
	}	
	else if (!isIp(s))
	{
#if !_XBOX
		U32 iFailureWearoffTime;
		struct hostent *hostent;

		EnterCriticalSection(&sFailedIPLookup);
		if (DelayBeforeRetryingFailedIPLookup() && stashFindInt(sFailedIPs, s, &iFailureWearoffTime))
		{
			
			if (iFailureWearoffTime < timeSecondsSince2000())
			{
				stashRemoveInt(sFailedIPs, s, NULL);
			}
			else
			{
				*error = -1;
				LeaveCriticalSection(&sFailedIPLookup);
				PERFINFO_AUTO_STOP();
				return 0;
			}
		}

		hostent=timedGetHostByName(s);
		
		if (hostent)
		{
			sprintf(ip_str,"%d.%d.%d.%d",(U8)hostent->h_addr_list[0][0],(U8)hostent->h_addr_list[0][1],
										(U8)hostent->h_addr_list[0][2],(U8)hostent->h_addr_list[0][3]);
		}
		else
		{
			if (DelayBeforeRetryingFailedIPLookup())
			{

				stashAddInt(sFailedIPs, s, timeSecondsSince2000() + DelayBeforeRetryingFailedIPLookup(), true);
			}

			*error = sockGetError();
			LeaveCriticalSection(&sFailedIPLookup);

			PERFINFO_AUTO_STOP();
			return 0;
		}
		LeaveCriticalSection(&sFailedIPLookup);

#else
		WSAEVENT hEvent = WSACreateEvent();
		XNDNS *xdns = 0;
		if (XNetDnsLookup(s, hEvent, &xdns) == 0)
		{
			WaitForSingleObject(hEvent, INFINITE);
			if (xdns->iStatus == 0)
			{
				assert(xdns->cina > 0);
				sprintf(ip_str,"%d.%d.%d.%d",xdns->aina[0].S_un.S_un_b.s_b1,xdns->aina[0].S_un.S_un_b.s_b2,
					xdns->aina[0].S_un.S_un_b.s_b3,xdns->aina[0].S_un.S_un_b.s_b4);
			}
			else if (xdns->iStatus == WSAHOST_NOT_FOUND)
			{
				verbose_printf("\nDNS error: host %s not found.\n", s);
			}
			else if (xdns->iStatus == WSAETIMEDOUT)
			{
				verbose_printf("\nDNS error: timed out looking for %s.\n", s);
			}
		}
		XNetDnsRelease(xdns);
		WSACloseEvent(hEvent);
#endif
	}
	ret = inet_addr(ip_str);
	if (ret == INADDR_NONE)
		ret = 0;
	PERFINFO_AUTO_STOP();
	return ret;
}

// Normally, we map localhost to our actual IP.  This can be disabled for performance, at the risk of causing terrible breakage.
bool sockDontConvertLocalhost(bool disable)
{
	bool old = sock_dont_convert_localhost;
	sock_dont_convert_localhost = disable;
	return old;
}

U32 ipPublicFromString(const char *s)
{
	char	ip_str[100];
	U32 ret;

	if (!s)
		return 0;

	strcpy(ip_str,s);
	if (stricmp(s,"localhost")==0 || stricmp(s,"127.0.0.1")==0)
#if !_XBOX
		strcpy(ip_str,makeIpStr(getHostPublicIp()));
#else
		assert(0);
#endif
	else if (!isIp(s))
	{
#if !_XBOX
		struct hostent *hostent=timedGetHostByName(s);
		
		if (hostent)
		{
			if ((U32*)(hostent->h_addr_list[1])) 
			{
				char str1[100];
				char str2[100];
				U32 ip1;
				U32 ip2;
				sprintf(str1,"%d.%d.%d.%d",(U8)hostent->h_addr_list[0][0],(U8)hostent->h_addr_list[0][1],
										(U8)hostent->h_addr_list[0][2],(U8)hostent->h_addr_list[0][3]);
				sprintf(str2,"%d.%d.%d.%d",(U8)hostent->h_addr_list[1][0],(U8)hostent->h_addr_list[1][1],
										(U8)hostent->h_addr_list[1][2],(U8)hostent->h_addr_list[1][3]);


				ip1 = inet_addr(str1);
				ip2 = inet_addr(str2);

				if (isLocalIp(ip1))
				{
					return ip2;
				}
				else
				{
					return ip1;
				}
			} 
			else 
			{
				sprintf(ip_str,"%d.%d.%d.%d",(U8)hostent->h_addr_list[0][0],(U8)hostent->h_addr_list[0][1],
										(U8)hostent->h_addr_list[0][2],(U8)hostent->h_addr_list[0][3]);
			}
		}
		else
			return 0;
#else
		WSAEVENT hEvent = WSACreateEvent();
		XNDNS *xdns = 0;
		if (XNetDnsLookup(s, hEvent, &xdns) == 0)
		{
			WaitForSingleObject(hEvent, INFINITE);
			if (xdns->iStatus == 0)
			{
				assert(xdns->cina > 0);
				sprintf(ip_str,"%d.%d.%d.%d",xdns->aina[0].S_un.S_un_b.s_b1,xdns->aina[0].S_un.S_un_b.s_b2,
					xdns->aina[0].S_un.S_un_b.s_b3,xdns->aina[0].S_un.S_un_b.s_b4);
			}
			else if (xdns->iStatus == WSAHOST_NOT_FOUND)
			{
				verbose_printf("\nDNS error: host %s not found.\n", s);
			}
			else if (xdns->iStatus == WSAETIMEDOUT)
			{
				verbose_printf("\nDNS error: timed out looking for %s.\n", s);
			}
		}
		XNetDnsRelease(xdns);
		WSACloseEvent(hEvent);
#endif
	}
	ret = inet_addr(ip_str);
	if (ret == INADDR_NONE)
		ret = 0;
	return ret;
}


U32 ipLocalFromString(const char *s)
{
	U32 ret;
	char	ip_str[100];

	if (!s)
		return 0;

	strcpy(ip_str,s);
	if (stricmp(s,"localhost")==0 || stricmp(s,"127.0.0.1")==0)
#if !_XBOX
		strcpy(ip_str,makeIpStr(getHostPublicIp()));
#else
		assert(0);
#endif
	else if (!isIp(s))
	{
#if !_XBOX
		struct hostent *hostent=timedGetHostByName(s);
		
		if (hostent)
		{
			if ((U32*)(hostent->h_addr_list[1])) 
			{
				char str1[100];
				char str2[100];
				U32 ip1;
				U32 ip2;
				sprintf(str1,"%d.%d.%d.%d",(U8)hostent->h_addr_list[0][0],(U8)hostent->h_addr_list[0][1],
										(U8)hostent->h_addr_list[0][2],(U8)hostent->h_addr_list[0][3]);
				sprintf(str2,"%d.%d.%d.%d",(U8)hostent->h_addr_list[1][0],(U8)hostent->h_addr_list[1][1],
										(U8)hostent->h_addr_list[1][2],(U8)hostent->h_addr_list[1][3]);


				ip1 = inet_addr(str1);
				ip2 = inet_addr(str2);

				if (isLocalIp(ip2) && !isLocalIp(ip1))
				{
					return ip2;
				}
				else
				{
					return ip1;
				}			
			} 
			else 
			{
				sprintf(ip_str,"%d.%d.%d.%d",(U8)hostent->h_addr_list[0][0],(U8)hostent->h_addr_list[0][1],
										(U8)hostent->h_addr_list[0][2],(U8)hostent->h_addr_list[0][3]);
			}
		}
		else
			return 0;
#else
		WSAEVENT hEvent = WSACreateEvent();
		XNDNS *xdns = 0;
		if (XNetDnsLookup(s, hEvent, &xdns) == 0)
		{
			WaitForSingleObject(hEvent, INFINITE);
			if (xdns->iStatus == 0)
			{
				assert(xdns->cina > 0);
				sprintf(ip_str,"%d.%d.%d.%d",xdns->aina[0].S_un.S_un_b.s_b1,xdns->aina[0].S_un.S_un_b.s_b2,
					xdns->aina[0].S_un.S_un_b.s_b3,xdns->aina[0].S_un.S_un_b.s_b4);
			}
			else if (xdns->iStatus == WSAHOST_NOT_FOUND)
			{
				verbose_printf("\nDNS error: host %s not found.\n", s);
			}
			else if (xdns->iStatus == WSAETIMEDOUT)
			{
				verbose_printf("\nDNS error: timed out looking for %s.\n", s);
			}
		}
		XNetDnsRelease(xdns);
		WSACloseEvent(hEvent);
#endif
	}
	ret = inet_addr(ip_str);
	if (ret == INADDR_NONE)
		ret = 0;
	return ret;
}

const char* sockGetReadableError(int errVal){
#if _PS3
	return NULL;
#else
	switch(errVal)
	{
		case 0:						return "No error";
		case WSAECONNRESET:			return "Connection reset by remote host";
		case WSAECONNABORTED:		return "Connection reset locally";
		case WSAENETRESET:			return "Keep-alive failed";
		case WSAENOTSOCK:			return "Invalid socket used";
		case WSAEDISCON:			return "Socket is shutting down";
		case WSAEFAULT:				return "Bad pointer passed to socket function";
		case WSAEINVAL:				return "Invalid parameter passed to socket function";
		case WSAENETDOWN:			return "Serious network failure somewhere, possibly tcp stack";
		case WSAENOTCONN:			return "Socket is not connected";
		case WSAEOPNOTSUPP:			return "Unsupported operation";
		case WSAESHUTDOWN:			return "Sending after a shutdown";
		case WSAETIMEDOUT:			return "Timeout on connection attempt";
		case WSANOTINITIALISED:		return "WSAStartup not called yet";
		case WSAEINPROGRESS:		return "Blocking socket op outstanding";
		case WSAEINTR:				return "A blocking call was canceled";
		case WSA_OPERATION_ABORTED:	return "Overlapped operation canceled";
		case WSAEMSGSIZE:			return "Message too long";
		default:					return "Unknown socket error";
	}
#endif
}

int isDisconnectError(int errVal){ 
#if _PS3
    return 
        errVal == SYS_NET_ECONNRESET ||
        errVal == SYS_NET_ECONNABORTED ||
        errVal == SYS_NET_ENETRESET ||
        errVal == SYS_NET_EMSGSIZE
    ;
#else
	switch(errVal)
	{
		xcase WSAECONNRESET:		// Lost connection.
		acase WSAECONNABORTED:		// Connection aborted intentionally by something.
		acase WSAENETRESET:			// Low level keep-alive failed somehow.
		{
			#if 0 // this should only run if something like logSetExcessiveVerbosity is set
			#ifndef _XBOX
					log_printf("net_disconnect", "disconnect error %i:%s", errVal, lastWinErr());
			#else
					log_printf("net_disconnect", "disconnect error %i", errVal);
			#endif
			#endif
			return 1;
		}
		xcase WSAENOTSOCK:			// That is not an existing socket handle.
		acase WSAEDISCON:			// Socket is shutting down.
		acase WSAEFAULT:			// We passed a bad buffer pointer param.
		acase WSAEINVAL:			// Bad parameter.
		acase WSAENETDOWN:			// Serious network failure somewhere, possibly tcp stack.
		acase WSAENOTCONN:			// Socket is not connected.
		acase WSAEOPNOTSUPP:		// Unsupported operation.
		acase WSAESHUTDOWN:			// Sending after a shutdown.
		acase WSAETIMEDOUT:			// Timeout on connection attempt.
		acase WSANOTINITIALISED:	// WSAStartup not called yet.
		{
			return 1;
		}
		xcase WSAEINPROGRESS:		// Pretty sure this is impossible to ever happen.
		acase WSAEINTR:				// A blocking call was canceled.
		acase WSA_OPERATION_ABORTED:// Overlapped operation canceled, possibly by OS.
		{
			return 0;
		}
		xcase WSAEMSGSIZE:
		{
			// "A message sent on a datagram socket was larger than the internal message
			// buffer or some other network limit, or the buffer used to receive a
			// datagram into was smaller than the datagram itself."

			// The other end sent us a packet bigger than our network layer accepts, must be spoofed or garbage!
			// Just disconnect the link!
			return 1;
		}
	}
#endif
	return 0;
}

char* stringFromAddr(struct sockaddr_in *addr){
	static char buffer[128];
#if _PS3
	sprintf(buffer, "%i.%i.%i.%i:%i", 
        addr->sin_addr.s_addr>>24,
        (addr->sin_addr.s_addr>>16)&0xff,
        (addr->sin_addr.s_addr>>8)&0xff,
        addr->sin_addr.s_addr&0xff,
		addr->sin_port);
#else
	sprintf(buffer, "%i.%i.%i.%i:%i", 
		addr->sin_addr.S_un.S_un_b.s_b1,
		addr->sin_addr.S_un.S_un_b.s_b2,
		addr->sin_addr.S_un.S_un_b.s_b3,
		addr->sin_addr.S_un.S_un_b.s_b4,
		ntohs(addr->sin_port));
#endif
	return buffer;
}

int sockGetError(){
	return WSAGetLastError();
}

void safeCloseSocket(SOCKET *sock_ptr)
{
	SOCKET	sock;
	sock = *sock_ptr;
	if (sock==INVALID_SOCKET)
		return;
	PERFINFO_AUTO_START_FUNC();
	*sock_ptr = INVALID_SOCKET;
	closesocket(sock);
	PERFINFO_AUTO_STOP();
}

//given "foo.bar.com", finds all non-zero IPs of "foo.bar.com", "foo0.bar.com", "foo1.bar.com", etc.
//up to "foo9.bar.com", and pushes all unique ones (strduped) into pppOutIPs (as "1.2.3.4" strings)
void GetAllUniqueIPs(char *pURL, const char ***pppOutIPs)
{
	U32 iIP;
	char *pFirstDot = strchr(pURL, '.');
	int iFirstDotIndex;
	int i;
	char *pTempString = NULL, *pTempIP;

	if (stricmp(pURL, "localhost") == 0)
	{
		eaPush(pppOutIPs, strdup("127.0.0.1"));
		return;
	}

	iIP = ipFromString(pURL);

	if (iIP)
	{
		pTempIP = makeIpStr(iIP);
		printf("While finding unique IPs, converted %s to %s\n",
			pURL, pTempIP);
		if(eaFindString(pppOutIPs, pTempIP)==-1)
			eaPush(pppOutIPs, strdup(pTempIP));
	}

	if (isIp(pURL))
	{
		return;
	}

	if (!pFirstDot)
	{
		iFirstDotIndex = (int)strlen(pURL);
	}
	else
	{
		iFirstDotIndex = pFirstDot - pURL;
	}
	
	estrStackCreate(&pTempString);

	estrCopy2(&pTempString, pURL);

	//insert a random character, as it will get overridden repeatedly
	estrInsert(&pTempString, iFirstDotIndex, pURL, 1);


	for (i=0; i <= 9; i++)
	{
		pTempString[iFirstDotIndex] = '0' + i;

		iIP = ipFromString(pTempString);
		if (iIP)
		{
			pTempIP = makeIpStr(iIP);
			printf("While finding unique IPs, converted %s to %s\n",
				pTempString, pTempIP);
			if(eaFindString(pppOutIPs, pTempIP)==-1)
				eaPush(pppOutIPs, strdup(pTempIP));
		}
	}

	estrDestroy(&pTempString);
}

SOCKET socketCreate(S32 af, S32 type, S32 protocol)
{
	SOCKET s;
	PERFINFO_AUTO_START_FUNC();
	s = socket(af, type, protocol);
	#if !PLATFORM_CONSOLE
	if(s != INVALID_SOCKET){
		SetHandleInformation((HANDLE)s, ~0, 0);
	}
	#endif
	PERFINFO_AUTO_STOP();
	return s;
}




	
