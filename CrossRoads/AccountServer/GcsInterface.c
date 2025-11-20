#include "AccountManagement.h"
#include "AccountServer.h"
#include "error.h"
#include "file.h"
#include "GcsInterface.h"
#include "GlobalComm.h"
#include "GlobalComm_h_ast.h"
#include "logging.h"
#include "net.h"
#include "netipfilter.h"
#include "sock.h"
#include "timing_profiler.h"

// Packet performance profiling.
typedef struct CmdPerf {
	char*				name;			// Packet type
	PERFINFO_TYPE*		pi;				// Performance information for this type
} CmdPerf;

static NetLink *sChatServerLink = NULL;

// Number of seconds to wait before timing out a GCS connection
static int giGCSTimeout = 1 * SECONDS_PER_HOUR;
AUTO_CMD_INT(giGCSTimeout, GCSTimeout) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static int AccountServerGcsConnectCallback(NetLink* link,AccountLink *accountLink)
{
	char buf[17];
	linkSetTimeout(link, giGCSTimeout);
	printf("Global Chat Server connected from %s.\n", linkGetIpStr(link, buf, sizeof(buf)));
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Global Chat Server connected from %s.", linkGetIpStr(link, buf, sizeof(buf)));
	accountLink->temporarySalt = 0;
	return 1;
}

static int AccountServerGcsDisconnectCallback(NetLink *link,AccountLink *accountLink)
{
	char buf[17];

	if (link == sChatServerLink)
	{
		sChatServerLink = NULL;
		return 1;
	}
	printf("Global Chat Server %s disconnected, from %s.\n",
		accountLink->loginField[0] ? accountLink->loginField : "[]", linkGetIpStr(link, buf, sizeof(buf)));
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Global Chat Server %s disconnected, from %s.",
		accountLink->loginField[0] ? accountLink->loginField : "[]", linkGetIpStr(link, buf, sizeof(buf)));
	memset(accountLink->loginField, 0, MAX_ACCOUNTNAME);
	accountLink->temporarySalt = 0;
	return 1;
}

static void AccountServerGcsHandleMessage(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	static CmdPerf cmdPerf[TO_ACCOUNTSERVER_MAX];
	const char pErrorDisabled[] = "This account has been disabled.";

	// Record packet processing performance.
	if (cmd >= 0 && cmd < ARRAY_SIZE(cmdPerf))
	{
		if(!cmdPerf[cmd].name)
		{
			char buffer[100];
			sprintf(buffer, "GCSCmd:%s (%d)", StaticDefineIntRevLookupNonNull(AccountServerCmdEnum, cmd), cmd);
			cmdPerf[cmd].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[cmd].name, &cmdPerf[cmd].pi, 1);
	}
	else
		PERFINFO_AUTO_START("GCSCmd:Unknown", 1);

	switch(cmd)
	{

		// Global Chat Server connect packet
		xcase TO_ACCOUNTSERVER_CHATSERVER_CONNECT:
			if (sChatServerLink)
			{
				char oldbuf[IPV4_ADDR_STR_SIZE], newbuf[IPV4_ADDR_STR_SIZE];
				AssertOrAlert("ACCOUNTSERVER_MULTIPLE_GCS", "Multiple Global Chat Servers connected to Account Server! (old %s, new %s)",
					linkGetIpStr(sChatServerLink, oldbuf, sizeof(oldbuf)), linkGetIpStr(link, newbuf, sizeof(newbuf)));
			}
			sChatServerLink = link;

		// Request the account ID for a display name.
		xcase TO_ACCOUNTSERVER_DISPLAYNAME_REQUEST:
			{
				U32 uRequestID;
				char *displayName;
				AccountInfo *account;
				Packet *response;

				PKT_CHECK_BYTES(4, pak, packetfailed);
				uRequestID = pktGetU32(pak);
				PKT_CHECK_STR(pak, packetfailed);
				displayName = pktGetStringTemp(pak);

				account = findAccountByDisplayName(displayName);

				response = pktCreate(link, TO_GLOBALCHATSERVER_DISPLAYNAME_TRANS);

				pktSendU32(response, uRequestID);
				if (account)
					pktSendU32(response, account->uID);
				else
					pktSendU32(response, 0);
				pktSend(&response);
			}

		// Request information about an account.
		xcase TO_ACCOUNTSERVER_UNKNOWN_ACCOUNT:
			{
				U32 uRequestID;
				U32 uAccountID;

				PKT_CHECK_BYTES(4, pak, packetfailed);
				uRequestID = pktGetU32(pak);
				PKT_CHECK_BYTES(4, pak, packetfailed);
				uAccountID = pktGetU32(pak);

				AccountServer_SendCreateAccount(link, uRequestID, uAccountID);
			}

		// Unknown packet
		xdefault:
			AssertOrAlert("ACCOUNTSERVER_BOGUS_GCS_PACKET", "Received unknown packet from Global Chat Server");
		break;
	}

	packetfailed:

	PERFINFO_AUTO_STOP();
}

// Send display name update notification to the Global Chat Server.
void AccountServer_SendDisplayNameUpdate(U32 uRequestID, const AccountInfo *pAccount)
{
	NetLink *link = sChatServerLink;

	if (linkConnected(link))
	{
		Packet *response = pktCreate(link, TO_GLOBALCHATSERVER_DISPLAYNAME_UPDATE);

		pktSendU32(response, uRequestID);
		if (pAccount)
		{
			pktSendU32(response, pAccount->uID);
			pktSendString(response, pAccount->displayName);
		}
		else
		{
			pktSendU32(response, 0);
			pktSendString(response, "");
		}
		pktSend(&response);
	}
}

// Send account creation notification to the Global Chat Server.
void AccountServer_SendCreateAccount(NetLink *link, U32 uRequestID, U32 uAccountID)
{
	if (link == NULL)
		link = sChatServerLink;
	if (linkConnected(link))
	{
		AccountInfo *account = findAccountByID(uAccountID);
		Packet *response = pktCreate(link, TO_GLOBALCHATSERVER_CREATEACCOUNT);

		pktSendU32(response, uRequestID);
		pktSendU32(response, uAccountID);
		if (account)
		{
			pktSendString(response, account->displayName);
			pktSendString(response, account->accountName);
		}
		else
		{
			pktSendString(response, "");
			pktSendString(response, "");
		}
		pktSend(&response);
	}
}

// Initialize the Account Server interface to the Global Chat Server.
int GcsInterfaceInit()
{
	NetListen * link = NULL;
	link = commListen(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, DEFAULT_ACCOUNTSERVER_GLOBALCHATSERVER_PORT, AccountServerGcsHandleMessage,
		AccountServerGcsConnectCallback, AccountServerGcsDisconnectCallback, sizeof(AccountLink));
	assertmsg(link, STACK_SPRINTF("Could not start server: Port %d already in use.", DEFAULT_ACCOUNTSERVER_GLOBALCHATSERVER_PORT));
	return 1;
}
