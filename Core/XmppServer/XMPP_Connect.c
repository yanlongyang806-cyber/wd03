#include "XMPP_Connect.h"
#include "XMPP_Gateway.h"

#include "cmdparse.h"
#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "logging.h"
#include "net.h"
#include "sock.h"
#include "timing.h"

#include "ChatServer/chatShard_Shared.h"

static NetLink *spGlobalLink = NULL;
static bool sbVersionMismatch = false;
static bool sbRegisteredXMPP = false;
extern bool gbXMPPVerbose;

NetLink *GetXmppGlobalChatLink(void)
{
	return spGlobalLink;
}

AUTO_COMMAND ACMD_CATEGORY(XmppServer) ACMD_ACCESSLEVEL(9);
void RegisterXmppWithGlobalChatServer(void)
{
	if (!sbRegisteredXMPP && linkConnected(spGlobalLink))
	{
		Packet *pkt;
		pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_REGISTER_SHARD);
		pktSendString(pkt, "XMPP");
		pktSendString(pkt, "XMPP");
		pktSendString(pkt, "XMPP");
		pktSend(&pkt);
		sbRegisteredXMPP = true;
	}
}

extern CmdList gGlobalCmdList;
extern CmdList gRemoteCmdList;
// This is a carbon-copy of defaultCmdParseFunc that only checks the remote command list for the command
int XmppCmdParseFunc(const char *cmdOrig, char **ppReturnString, CmdContext *pContext)
{
	char *pReturnStringInternal = NULL;
	char *cmd, *cmdStart = NULL;
	int retVal = -1;


	if (ppReturnString)
	{
		pContext->output_msg = ppReturnString;
	}
	else
	{
		estrStackCreate(&pReturnStringInternal);
		pContext->output_msg = &pReturnStringInternal;
	}

	pContext->access_level = 10;
	pContext->flags |= CMD_CONTEXT_FLAG_IGNORE_UNKNOWN_FIELDS;

	estrStackCreate(&cmdStart);
	estrCopy2(&cmdStart,cmdOrig);
	cmd = cmdStart;

	if (cmd)
	{
		estrClear(pContext->output_msg);

		pContext->eHowCalled = CMD_CONTEXT_HOWCALLED_COMMANDLINE;
		if (!cmdParseAndExecute(&gRemoteCmdList, cmd, pContext))
		{
			servLog(LOG_XMPP_GENERAL, "XmppCommand", "[Error: Unknown Command] %s", cmd);
			estrDestroy(&pReturnStringInternal);
			estrDestroy(&cmdStart);
			return -1; // did not find command;
		}
		retVal = pContext->return_val.intval;
	}

	estrDestroy(&pReturnStringInternal);
	estrDestroy(&cmdStart);
	return retVal;
}

static void XMPP_GCSConnect(NetLink* link,void *user_data)
{
	struct in_addr ina = {0};
	char *ipString;
	ina.S_un.S_addr = linkGetIp(link);
	ipString = inet_ntoa(ina);

	printf("XMPP: Connected to Global Chat Server at IP [%s].\n", ipString);
}

static void XMPP_GCSDisconnect(NetLink* link,void *user_data)
{
	printf("Local Chat Server: Disconnected from Global Chat Server.\n");
	linkRemove(&spGlobalLink);
	sbRegisteredXMPP = false;
	XMPP_ClearServerData();
}

static void XMPP_HandleGCSMessage(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase FROM_GLOBALCHATSERVER_COMMAND:
		{
			U32 retVal = pktGetU32(pkt); // ignored by XMPP
			char *pCommandString = pktGetStringTemp(pkt);
			CmdContext context = {0};
			XmppCmdParseFunc(pCommandString, NULL, &context);
			if (gbXMPPVerbose)
				printf ("Cmd from GCS: %s\n", pCommandString);
		}
	xcase FROM_GLOBALCHATSERVER_MESSAGE:
		// unhandled
	xcase FROM_GLOBALCHATSERVER_ALERT:
		// also unhandled
	xcase FROM_GLOBALCHATSERVER_ACCOUNT_LIST: 
	case FROM_GLOBALCHATSERVER_CHANNEL_LIST:
		// also unhandled
	xcase FROM_GLOBALCHATSERVER_SHARDINFO_REQUEST:
		{
			RegisterXmppWithGlobalChatServer();
		}
	xcase FROM_GLOBALCHATSERVER_VERSIONINFO:
		// unhandled
		/*{
		U32 uMajorRevision = pktGetU32(pkt);
		U32 uMinorRevision = pktGetU32(pkt);
		if (uMajorRevision != CHATSERVER_MAJOR_REVISION)
		{
		AssertOrAlert("CHAT_MAJOR_VERSION_MISMATCH", "Chat Major Version Mismatch - GCS v.%d, Shard Chat v.%d", 
		uMajorRevision, CHATSERVER_MAJOR_REVISION);
		sbVersionMismatch = true;
		linkRemove_wReason(&link, "Major version mismatch");
		}
		else if (uMinorRevision < CHATSERVER_MINOR_REVISION)
		{
		AssertOrAlert("CHAT_MINOR_VERSION_MISMATCH", "Chat Minor Version Mismatch - GCS v.%d, Shard Chat v.%d", 
		uMinorRevision, CHATSERVER_MINOR_REVISION);
		sbVersionMismatch = true;
		linkRemove_wReason(&link, "Minor version mismatch");
		}
		else
		sbVersionMismatch = false;
		printf ("Version Received: Major %d, Minor %d\n", uMajorRevision, uMinorRevision);
		}*/
	xdefault:
		//log_printf(LOG_CHATSERVER, "[Error: Unknown packet received]");
		break;
	}
}

AUTO_COMMAND ACMD_CATEGORY(XMPPServer) ACMD_ACCESSLEVEL(9);
void RegisterXMPPWithGlobalChat(void)
{
	if (!sbRegisteredXMPP && linkConnected(spGlobalLink))
	{
		Packet *pkt;
		pkt = pktCreate(spGlobalLink, TO_GLOBALCHATSERVER_REGISTER_SHARD);
		pktSendString(pkt, "XMPP");
		pktSendString(pkt, "XMPP");
		pktSendString(pkt, "XMPP");
		pktSend(&pkt);
		sbRegisteredXMPP = true;
	}
}

AUTO_COMMAND ACMD_CATEGORY(XMPPServer) ACMD_ACCESSLEVEL(9);
void ReconnectToGlobalChat(void)
{
	if (!linkConnected(spGlobalLink))
	{
		printf ("Reconnecting to Global Chat Server...\n\n");
		if (!spGlobalLink)
			linkRemove(&spGlobalLink); // clear the link first so we don't have a phantom link
		spGlobalLink = commConnect(getCommToGlobalChat(), LINKTYPE_UNSPEC, LINK_COMPRESS|LINK_FORCE_FLUSH, 
			getGlobalChatServer(), DEFAULT_GLOBAL_CHATSERVER_PORT,
			XMPP_HandleGCSMessage, XMPP_GCSConnect, XMPP_GCSDisconnect, 0);
		sbRegisteredXMPP = false;
	}
}

void XMPPServer_Tick(F32 elapsed)
{
	static U32 suLastConnectTryTime = 0;
	U32 uTime = timeSecondsSince2000();
	if (!spGlobalLink)
	{
		U32 uReconnectTime = suLastConnectTryTime + 
			(sbVersionMismatch ? CHATSERVER_VERSION_RECONNECT_TIME : CHATSERVER_RECONNECT_TIME);
		if (!suLastConnectTryTime || uReconnectTime < uTime)
			ReconnectToGlobalChat();
		else
			return;
		suLastConnectTryTime = uTime;
	}

	XMPP_Tick(elapsed);
	commMonitor(getCommToGlobalChat());
	if (linkConnected(spGlobalLink))
	{
		if (!sbRegisteredXMPP)
			RegisterXMPPWithGlobalChat();
	}
	else if (spGlobalLink && suLastConnectTryTime + CHATSERVER_CONNECT_TIMEOUT < uTime)
	{
		linkRemove(&spGlobalLink);
	}
}
