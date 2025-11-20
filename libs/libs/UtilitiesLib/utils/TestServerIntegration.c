#include "TestServerIntegration.h"

#include "CrypticPorts.h"
#include "GlobalComm.h"
#include "LinkToMultiplexer.h"
#include "net.h"
#include "netpacketutil.h"
#include "StashTable.h"
#include "StringCache.h"
#include "structNet.h"
#include "timing.h"
#include "UtilitiesLib.h"

#include "TestServerIntegration_h_ast.h"

#define TESTSERVER_SECONDS_PER_CONNECT	5

bool gbIsTestServerHostSet = false;
char gTestServerHost[CRYPTIC_MAX_PATH] = "localhost";

AUTO_COMMAND ACMD_NAME(SetTestServer) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void SetTestServerHostname(const char *pcHostname)
{
	strcpy(gTestServerHost, pcHostname);
	gbIsTestServerHostSet = true;
}

int gTestServerPort = DEFAULT_TESTSERVER_PORT;
AUTO_CMD_INT(gTestServerPort, SetTestServerPort) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// Connect to Test Server via Multiplexer
bool gTestServerUsesMultiplex = false;
AUTO_CMD_INT(gTestServerUsesMultiplex, TestServerUsesMultiplex);

// Do not do Test Server interaction, ever
bool gNoTestServer = false;
AUTO_CMD_INT(gNoTestServer, NoTestServer);

NetLink *gpTestServerLink = NULL;
LinkToMultiplexer *gpMultiplexedTestServerLink = NULL;

static StashTable sGlobalCallbacks = NULL;

static const char *GetTestServerGlobalName_internal(const char *pcScope, const char *pcName)
{
	const char *pcAllocScopedName;
	char *pcScopedName = NULL;

	PERFINFO_AUTO_START_FUNC();
	if(!pcName && !pcScope)
	{
		estrPrintf(&pcScopedName, "All");
	}
	else if(!pcName)
	{
		estrPrintf(&pcScopedName, "%s::", pcScope);
	}
	else if(!pcScope)
	{
		estrPrintf(&pcScopedName, "%s", pcName);
	}
	else
	{
		estrPrintf(&pcScopedName, "%s::%s", pcScope, pcName);
	}

	pcAllocScopedName = allocAddString(pcScopedName);
	estrDestroy(&pcScopedName);
	PERFINFO_AUTO_STOP();

	return pcAllocScopedName;
}

void RegisterTestServerGlobalCallback(const char *pcScope, const char *pcName, TestServerGlobalCallback pCallback)
{
	const char *pcKey = GetTestServerGlobalName_internal(pcScope, pcName);

	if(!sGlobalCallbacks)
	{
		sGlobalCallbacks = stashTableCreateWithStringKeys(8, StashDefault);
	}

	stashAddPointer(sGlobalCallbacks, pcKey, pCallback, true);
}

static void ReadTestServerGlobalFromPacket(Packet *pkt)
{
	TestServerGlobal *pGlobal = StructCreate(parse_TestServerGlobal);
	StashElement elem;
	TestServerGlobalCallback pCallback;

	ParserRecv(parse_TestServerGlobal, pkt, pGlobal, 0);

	if(!stashFindElement(sGlobalCallbacks, GetTestServerGlobalName_internal(pGlobal->pcScope, pGlobal->pcName), &elem) &&
		!stashFindElement(sGlobalCallbacks, GetTestServerGlobalName_internal(pGlobal->pcScope, NULL), &elem) &&
		!stashFindElement(sGlobalCallbacks, GetTestServerGlobalName_internal(NULL, NULL), &elem))
	{
		StructDestroy(parse_TestServerGlobal, pGlobal);
		return;
	}

	pCallback = stashElementGetPointer(elem);
	pCallback(pGlobal);
}

static PacketCallback *spTestServerHandler = NULL;
void SetTestServerMessageHandler(PacketCallback *pCallback)
{
	spTestServerHandler = pCallback;
}

static LinkCallback *spTestServerDisconnectHandler = NULL;
void SetTestServerDisconnectHandler(LinkCallback *pCallback)
{
	spTestServerDisconnectHandler = pCallback;
}

static void TestServerMultiplexedMsgHandler(Packet *pkt, int cmd, int index, LinkToMultiplexer *link)
{
	devassertmsg(index == MULTIPLEX_CONST_ID_TEST_SERVER, "Somehow got a multiplexed message not from the Test Server");

	switch(cmd)
	{
	case FROM_TESTSERVER_HERE_IS_GLOBAL:
	case FROM_TESTSERVER_HERE_IS_METRIC:
		ReadTestServerGlobalFromPacket(pkt);
	xdefault:
		if(spTestServerHandler)
		{
			spTestServerHandler(pkt, cmd, link->pLinkToMultiplexingServer, link->pUserData);
		}
		break;
	}
}


static void TestServerMultiplexedDisconnectHandler(int index, LinkToMultiplexer *link)
{
	if(index == MULTIPLEX_CONST_ID_TEST_SERVER)
	{
		if(spTestServerDisconnectHandler)
		{
			spTestServerDisconnectHandler(link->pLinkToMultiplexingServer, link->pUserData);
		}

		DestroyLinkToMultiplexer(link);
		gpMultiplexedTestServerLink = NULL;
	}
}

static void TestServerMsgHandler(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	switch(cmd)
	{
	case FROM_TESTSERVER_HERE_IS_GLOBAL:
	case FROM_TESTSERVER_HERE_IS_METRIC:
		ReadTestServerGlobalFromPacket(pkt);
	xdefault:
		if(spTestServerHandler)
		{
			spTestServerHandler(pkt, cmd, link, user_data);
		}
		break;
	}
}

static void TestServerDisconnectHandler(NetLink *link, void *user_data)
{
	if(spTestServerDisconnectHandler)
	{
		spTestServerDisconnectHandler(link, user_data);
	}

	gpTestServerLink = NULL;
}

static bool AttemptToConnectToTestServer(void)
{
	if(gTestServerUsesMultiplex)
	{
		int iRequiredServers[] = { MULTIPLEX_CONST_ID_TEST_SERVER, -1 };
		gpMultiplexedTestServerLink = GetAndAttachLinkToMultiplexer("localhost", GetMultiplexerListenPort(), LINKTYPE_DEFAULT, TestServerMultiplexedMsgHandler, 
			NULL, TestServerMultiplexedDisconnectHandler, NULL, iRequiredServers, MULTIPLEX_CONST_ID_TEST_SERVER, NULL, "Multiplexed link to test server", NULL);

		if(!gpMultiplexedTestServerLink)
		{
			printf("Failed to connect to Test Server through multiplexer!\n");
			return false;
		}
	}
	else
	{
		gpTestServerLink = commConnect(commDefault(), LINKTYPE_SHARD_NONCRITICAL_1MEG, LINK_FORCE_FLUSH, gTestServerHost, gTestServerPort, TestServerMsgHandler, NULL, TestServerDisconnectHandler, 0);

		if(!linkConnectWait(&gpTestServerLink, 1.0f))
		{
			printf("Failed to connect to Test Server directly!\n");
			return false;
		}
	}

	return true;
}

bool CheckTestServerConnection(void)
{
	static int iLastAttempt = 0;

	if(gNoTestServer)
	{
		return false;
	}

	if(gTestServerUsesMultiplex)
	{
		UpdateLinkToMultiplexer(gpMultiplexedTestServerLink);
		
		if(gpMultiplexedTestServerLink)
		{
			return true;
		}
	}
	else if(gpTestServerLink)
	{
		// Nothing to monitor here since we connected on commDefault
		return true;
	}

	if(timeSecondsSince2000() - iLastAttempt <= TESTSERVER_SECONDS_PER_CONNECT)
	{
		return false;
	}

	iLastAttempt = timeSecondsSince2000();
	return AttemptToConnectToTestServer();
}

Packet *GetPacketToSendToTestServer(int cmd)
{
	Packet *pkt = NULL;

	if(gTestServerUsesMultiplex)
	{
		if(gpMultiplexedTestServerLink)
		{
			pkt = CreateLinkToMultiplexerPacket(gpMultiplexedTestServerLink, MULTIPLEX_CONST_ID_TEST_SERVER, cmd, NULL);
		}
	}
	else if(gpTestServerLink)
	{
		pkt = pktCreate(gpTestServerLink, cmd);
	}

	return pkt;
}

void RequestMetricFromTestServer(const char *pcScope, const char *pcName)
{
	Packet *pkt;

	if(!CheckTestServerConnection())
	{
		return;
	}

	pkt = GetPacketToSendToTestServer(TO_TESTSERVER_GET_METRIC);
	pktSendString(pkt, pcScope);
	pktSendString(pkt, pcName);
	pktSend(&pkt);
}

void RequestGlobalFromTestServer(const char *pcScope, const char *pcName)
{
	Packet *pkt;
	
	if(!CheckTestServerConnection())
	{
		return;
	}

	pkt = GetPacketToSendToTestServer(TO_TESTSERVER_GET_GLOBAL);
	pktSendString(pkt, pcScope);
	pktSendString(pkt, pcName);
	pktSend(&pkt);
}

void PushMetricToTestServer(const char *pcScope, const char *pcName, float val, bool bPersist)
{
	Packet *pkt;

	if(!CheckTestServerConnection())
	{
		return;
	}

	pkt = GetPacketToSendToTestServer(TO_TESTSERVER_PUSH_METRIC);
	pktSendString(pkt, pcScope);
	pktSendString(pkt, pcName);
	pktSendFloat(pkt, val);
	pktSendBool(pkt, bPersist);
	pktSend(&pkt);
}

void ClearGlobalOnTestServer(const char *pcScope, const char *pcName)
{
	Packet *pkt;

	if(!CheckTestServerConnection())
	{
		return;
	}

	pkt = GetPacketToSendToTestServer(TO_TESTSERVER_CLEAR_GLOBAL);
	pktSendString(pkt, pcScope);
	pktSendString(pkt, pcName);
	pktSend(&pkt);
}

#include "TestServerIntegration_h_ast.c"