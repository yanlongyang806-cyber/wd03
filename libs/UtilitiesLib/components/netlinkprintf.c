#include "net/net.h"
#include "netlinkprintf.h"
#include "sock.h"
#include "utils.h"


//printfs always use their own netlink, so we don't need to worry about command namespaces
enum
{
	NETLINK_PRINTF = 1,
	NETLINK_PRINTF_CONNECT
};

static NetComm	*comm_printf;
NetLink *gpNetLinkForPrintf = NULL;
static int inNetLinkPrintf = 0;

int NetLinkVPrintf(NetLink *pNetLink, const char *format, va_list argptr)
{
	static int iFirst = 1;
	static DWORD threadID;
	int retVal = 0;
	
	if (iFirst)
	{
		iFirst = 0;
		threadID = GetCurrentThreadId();
	}
	else
	{
		if (threadID != GetCurrentThreadId())
		{
			return 0;
		}
	}

	if (inNetLinkPrintf)
		return 0;

	++inNetLinkPrintf;

#if !PLATFORM_CONSOLE

	if (pNetLink && !linkDisconnected(pNetLink))
	{
		char buffer[4096];
		Packet *pak;

		retVal = vsprintf_s(buffer, sizeof(buffer), format, argptr);

		pak = pktCreate(pNetLink, NETLINK_PRINTF);

		pktSendString(pak, buffer);

		pktSendBits(pak, 4, consoleGetColorFG());
		pktSendBits(pak, 4, consoleGetColorBG());

		pktSend(&pak);

		commMonitor(comm_printf);
	}
#endif

	--inNetLinkPrintf;

	return retVal;
}

AUTO_COMMAND ACMD_CMDLINE;
void AttemptToConnectNetLinkForPrintf(char *pServerName, int iPortNum, int iId, char *pName)
{
	Packet *pPacket;

	++inNetLinkPrintf;

	if (!comm_printf)
		comm_printf = commCreate(1,0);

	if (gpNetLinkForPrintf)
		linkRemove(&gpNetLinkForPrintf);

	gpNetLinkForPrintf = commConnect(comm_printf, LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,pServerName,iPortNum,0,0,0,0);
	if (linkConnectWait(&gpNetLinkForPrintf,4.f))
	{
		pPacket = pktCreate(gpNetLinkForPrintf, NETLINK_PRINTF_CONNECT);
		pktSendBitsPack(pPacket, 1, iId);
		pktSendString(pPacket, pName);
		pktSend(&pPacket);
	}

	--inNetLinkPrintf;
}



NetLinkPrintfCB *gpReceivePrintfCB = NULL;

typedef struct
{
	char linkName[32];
	int iLinkID;
} NetLinkPrintfUserData;

int NetLinkPrintfConnect(NetLink* link,NetLinkPrintfUserData *pUserData)
{
	snprintf_s(pUserData->linkName, sizeof(pUserData->linkName), "Unknown");
	pUserData->iLinkID = -1;

	return 1;
}

static void NetLinkPrintfHandleMsg(Packet *pak,int cmd, NetLink *link, NetLinkPrintfUserData *pUserData)
{
	char *pString;
	int iFGColor;
	int iBGColor;

	switch (cmd)
	{
	case NETLINK_PRINTF:
		pString = pktGetStringTemp(pak);
		iFGColor = pktGetBits(pak, 4);
		iBGColor = pktGetBits(pak, 4);

		gpReceivePrintfCB(pUserData->linkName, pUserData->iLinkID, pString, iFGColor, iBGColor);
		break;

	case NETLINK_PRINTF_CONNECT:
		pUserData->iLinkID = pktGetBitsPack(pak, 1);
		pString = pktGetStringTemp(pak);
		snprintf_s(pUserData->linkName, sizeof(pUserData->linkName), "%s", pString);
		break;

	}
}


void RegisterToReceiveNetLinkPrintfs(int iPortNum, int iMaxConnected, NetLinkPrintfCB *pReceivePrintfCB)
{
	assertmsg(gpReceivePrintfCB == NULL, "Can't call RegisterToReceiveNetLinkPrintfs twice.");

	gpReceivePrintfCB = pReceivePrintfCB;

	comm_printf = commCreate(0,0);
	commListen(comm_printf,LINKTYPE_UNSPEC, 0,iPortNum,NetLinkPrintfHandleMsg,NetLinkPrintfConnect,0,sizeof(NetLinkPrintfUserData));
}

void ReceiveNetLinkPrints_Monitor(void)
{
	commMonitor(comm_printf);
}
