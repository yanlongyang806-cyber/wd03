#if _XBOX

#include "MemoryMonitor.h"

#include "sock.h"
#include <string.h>
#include <stdio.h>
#include "GraphicsLib.h"

#include "GameClientLib.h"

XNADDR gxnaddr;
XUID gxuid;

bool gbXLive = false;
bool gbNetBSD = false;
bool gbXLSP = false;

bool gLiveUIdisplayed = FALSE;

static DWORD FIGHTCLUB_SERVICEID = 0x49470800;
static IN_ADDR	g_sgaddr;       // SG address
static IN_ADDR	g_inaSecure;    // Secure title server address
static int sXLSP_initialized = 0;


//hooks into sock.c
int  (*sockStart_hook)(void);
void (*sockStop_hook)(void);
void (*sockSetAddr_hook)(struct sockaddr_in *addr,unsigned int ip,int port);

HANDLE          g_hLiveNotify = NULL;

int	XLSP_sockStart(void)
{
	int err; 

	WORD wVersionRequested;  
	WSADATA wsaData; 

	if ( !gbXLSP )
	{
		XNetStartupParams xnsp;
		memset(&xnsp, 0, sizeof(xnsp));
		xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
		xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;

		if ( (err = XNetStartup(&xnsp)) )
		{
			return err;
		}
	}
	else
	{
		if ( (err = XNetStartup(NULL)) )
		{
			return err;
		}
	}

	wVersionRequested = MAKEWORD(2, 2); 

	if ( (err = WSAStartup(wVersionRequested, &wsaData)) )
	{
		XNetCleanup();
		return err;
	}


	if ( (err = XOnlineStartup()) )
	{
		WSACleanup();
		XNetCleanup();
		return err;
	}

	return 0;
}

void XLSP_sockStop(void)
{
	XOnlineCleanup();
	WSACleanup();
	XNetCleanup();
}






//for XLSP all communications go through the SG
//all IP+port pairs are converted into an SG port #
//the new port# on the SG is (low octet of IP)<<8 | ((port#-7000)&0xff)
//this gives us a 16 bit port # that is 8 bits of IP and 8 bits port
//or 256 servers w/ 256 ports each.
//We acually only get 254 server since .0 and .255 are reserved in the IP range
//we can probably do special mapping with these addresses if we need more
void	XLSP_sockSetAddr(struct sockaddr_in *addr,unsigned int ip,int port)
{
	//don't convert if XLSP is not intialized, or IP is 0.0.0.0 or 255.255.255.255
	if ( !sXLSP_initialized || ( ip == 0 ) || ( ip == 0xffffffff ) )
	{
		memset(addr,0,sizeof(struct sockaddr_in));
		addr->sin_family=AF_INET;
		addr->sin_addr.s_addr=ip;
		addr->sin_port = htons((u_short)port);
		return;
	}

	{
		IN_ADDR	tmp_ip;
		unsigned char low_oct;
		unsigned char port_byte;
		int	 new_port;


		tmp_ip.S_un.S_addr = (u_long)ip;
		low_oct = (unsigned char)tmp_ip.S_un.S_un_b.s_b4;
		port_byte = (unsigned char)(port-7000);

		new_port = low_oct;
		new_port <<= 8;
		new_port |= port_byte;

		//overide hack for special ports
		switch ( port )
		{
		case 6980:				//error tracker port maps to SG port 1
			new_port = 1;
			break;
		}

		memset(addr,0,sizeof(struct sockaddr_in));
		addr->sin_family=AF_INET;
		addr->sin_addr.s_addr=g_inaSecure.S_un.S_addr;
		addr->sin_port = htons((u_short)new_port);
	}
}


//hooks for XLive login
void (*XLive_login_init_hook)(void);
void (*XLive_login_loop_hook)(void);
void (*XLive_login_exit_hook)(void);
int (*XLive_login_check_hook)(void);


int XLive_login_check(void)
{
	if ( ( XUserGetSigninState(0) == eXUserSigninState_SignedInToLive ) && !gLiveUIdisplayed )
		return 1;
	else
		return 0;
}


void XLive_login_loop(void)
{
	if ( ( XUserGetSigninState(0) != eXUserSigninState_SignedInToLive ) && !gLiveUIdisplayed )
	{
		XShowSigninUI(1, XSSUI_FLAGS_SHOWONLYONLINEENABLED);
	}
}



#define MAX_SERVERS 20


//this gets executed after client has successfully logged in to Live
void XLive_login_exit(void)
{
	XTITLE_SERVER_INFO Servers[ MAX_SERVERS ];
	DWORD dwResult = 0;
	INT   iResult = 0;
	DWORD dwServerCount = 0;
	DWORD dwBufferSize = 0;
	HANDLE hServerEnum = INVALID_HANDLE_VALUE;
	int err;
	int SGidx;
	char* DesiredSG = "SJC_CS01_PartnerNet";


	//get the XUID of this client
	if ( (dwResult = XUserGetXUID( 0, &gxuid )) != ERROR_SUCCESS )
	{
		char msg[1024];

		sprintf( msg, "XUserGetXUID failed with %d.", dwResult );
		assertmsg(0, msg );
	}

	//set the login name of this client to the XUID
	sprintf_s( SAFESTR(gGCLState.loginName), "%16.16llX", gxuid );

	//get the XNADDR of this client
	while ( (dwResult=XNetGetTitleXnAddr(&gxnaddr)) == XNET_GET_XNADDR_PENDING )
	{
		Sleep(1);
	}
	if ( dwResult & XNET_GET_XNADDR_NONE  )
	{
		char msg[1024];

		sprintf( msg, "XNetGetTitleXnAddr failed with %8.8x.", dwResult );
		assertmsg(0, msg );
	}


	//if not in XLSP mode then exit
	if ( !gbXLSP )
		return;

	//if XLSP already initialized then exit
	if ( sXLSP_initialized )
		return;

	ZeroMemory( Servers, sizeof( Servers ) );

	dwResult = XTitleServerCreateEnumerator( NULL, MAX_SERVERS, &dwBufferSize, &hServerEnum );
	if( ERROR_SUCCESS != dwResult )
	{
		char msg[1024];

		sprintf( msg, "XTitleServerCreateEnumerator failed with %d.", dwResult );
		assertmsg(0, msg );
	}

	dwResult = XEnumerate( hServerEnum, Servers, sizeof( Servers ), &dwServerCount, NULL );
	if( ERROR_SUCCESS != dwResult )
	{
		char msg[1024];

		sprintf( msg, "XEnumerate failed with %d.", dwResult );
		CloseHandle( hServerEnum );
		assertmsg(0, msg );
	}

	CloseHandle( hServerEnum );

	for (SGidx=0; SGidx<(int)dwServerCount ;SGidx++)
	{
		if ( strcmp(Servers[SGidx].szServerInfo, DesiredSG) == 0 )
			break;
	}

	if (SGidx >= (int)dwServerCount)
	{
		//desired SG not found
		char msg[1024];

		sprintf( msg, "XLSP SG %s not found", DesiredSG );
		assertmsg(0, msg );
	}

	g_sgaddr.S_un.S_addr = Servers[SGidx].inaServer.S_un.S_addr;

	if ( (err = XNetServerToInAddr( g_sgaddr, FIGHTCLUB_SERVICEID, &g_inaSecure )) )
	{
		char msg[1024];

		sprintf(msg, "XNetServerToInAddr error %d", err);
		assertmsg(0, msg );
	}
	printf("secure ip: %8.8X\n", g_inaSecure.S_un.S_addr);

	if( ( err = XNetConnect(g_inaSecure) ) )
	{
		char msg[1024];

		sprintf(msg, "XNetConnect error %d", err);
		assertmsg(0, msg );
	}

	printf("XNetConnect\n");

	sXLSP_initialized = 1;
}



void XLive_login_init(void)
{
	//start up the network
	sockStart();

	//on XBOX give the system a few seconds to auto login so that XLSP can intialize early
	if ( gbXLive )
	{
		int ii;

		for (ii=0;ii<5*60;ii++)
		{
			if ( XLive_login_check () )
			{
				XLive_login_exit();
				break;
			}

			Sleep(16);
		}
	}

}


AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void XLive(int val)
{
	gbXLive = val;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void NoXLive(int val)
{
	gbXLive = false;
}


AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void NETBSD(int val)
{
	gbNetBSD = val;
}



AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void XLSP(int val)
{
	gbXLSP = val;
	if (val)
		gbXLive = val;
}

void InitXliveLib(void)
{

	if ( gbNetBSD || gbXLSP )
	{
		//force net code to only use BSD functions
		netSetSockBsd(1);	
	}

	sockStart_hook = XLSP_sockStart;
	sockStop_hook = XLSP_sockStop;

	if ( gbXLive )
	{
		XLive_login_init_hook = XLive_login_init;
		XLive_login_loop_hook = XLive_login_loop;
		XLive_login_exit_hook = XLive_login_exit;
		XLive_login_check_hook = XLive_login_check;
	}


	if ( gbXLSP )
	{
		sockSetAddr_hook = XLSP_sockSetAddr;
	}
	else
	{
		//setup pointer to standard net routines
		xsockInit();
	}
}

int XLSP_getInitialized(void)
{
	return sXLSP_initialized;
}

#endif
