/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "TestClient.h"

#include "accountnet.h"
#include "Alerts.h"
#include "AutoStartupSupport.h"
#include "ClientControllerLib.h"
#include "DirMonitor.h"
#include "file.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "luaScriptLib.h"
#include "mathutil.h"
#include "MemReport.h"
#include "process_util.h"
#include "ResourceManager.h"
#include "serverlib.h"
#include "StringCache.h"
#include "sysutil.h"
#include "TestClientCommon.h"
#include "TestClientLua.h"
#include "TestClientScripting.h"
#include "TestServerIntegration.h"
#include "timing_profiler_interface.h"
#include "UtilitiesLib.h"
#include "winutil.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"

TestClientGlobalState gTestClientGlobalState = {
	0,
	NULL,
	2,
	0,
	1,
	"Default",
	NULL,
	NULL,
	0,
	0,
	NULL
};

AUTO_CMD_INT(gTestClientGlobalState.bNoShardMode, NoShardMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gTestClientGlobalState.iFPS, ClientFPS) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gTestClientGlobalState.iID, TestClientID) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gTestClientGlobalState.iOwnerID, OwnerID) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

static bool bHaveSentTestServerIntro = false;

AUTO_COMMAND ACMD_NAME(server) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void TestClient_AddLoginServer(const char *pcServer)
{
	ClientController_AppendClientCommandLine("server", pcServer);
}

AUTO_RUN_SECOND;
void TestClient_InitStringCache(void)
{
	stringCacheInitializeShared("TestClientSharedStringCache", 1024*1024*80, 1024*1024*8/10, true);
}

AUTO_STARTUP(TestClient, 1) ASTRT_DEPS(AS_Messages);
void TestClient_AutoStartup(void)
{
	// do nothing
}

static void TestClient_SendTestServerIntro(void)
{
	Packet *pkt;
	
	if(bHaveSentTestServerIntro)
	{
		return;
	}

	bHaveSentTestServerIntro = true;

	pkt = GetPacketToSendToTestServer(TO_TESTSERVER_I_AM_A_TESTCLIENT);
	pktSend(&pkt);

	pkt = GetPacketToSendToTestServer(TO_TESTSERVER_TESTCLIENT_ID);
	pktSendU32(pkt, gTestClientGlobalState.iID);
	pktSend(&pkt);
}

static void TestClient_DoTestServerCommand(const char *cmd)
{
	if(cmd[0] == '#')
	{
		++cmd;

		if(cmd[0] == '!')
		{
			++cmd;
			printf("Executing script command: %s\n", cmd);
			TestClient_ScriptExecute(cmd);
		}
		else
		{
			printf("Executing client script command: %s\n", cmd);
			TestClient_ClientScriptExecute(cmd);
		}
	}
	else if(cmd[0] == '!')
	{
		++cmd;
		printf("Executing local auto command: %s\n", cmd);
		globCmdParse(cmd);
	}
	else
	{		
		printf("Executing client auto command: %s\n", cmd);
		ClientController_SendCommandToClient(cmd);
	}
}

static void TestClient_TestServerMessageCB(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	switch(cmd)
	{
	case FROM_TESTSERVER_TESTCLIENT_ID:
		{
			ContainerID iID = pktGetU32(pkt);
			gTestClientGlobalState.iID = iID;
		}
	xcase FROM_TESTSERVER_TESTCLIENT_COMMAND:
		{
			const char *pcCmd = pktGetStringTemp(pkt);
			TestClient_DoTestServerCommand(pcCmd);
		}
	xcase FROM_TESTSERVER_TESTCLIENT_DIE:
		gTestClientGlobalState.bRunning = false;
	xdefault:
		break;
	}
}

static void TestClient_ServerTick(void)
{
	static ContainerID iLastID = 0;
	bool bConnected = CheckTestServerConnection();

	if(!bConnected)
	{
		return;
	}

	TestClient_SendTestServerIntro();

	if(gTestClientGlobalState.pLatestState->iID != iLastID)
	{
		iLastID = gTestClientGlobalState.pLatestState->iID;

		if(iLastID)
		{
			Packet *pkt = GetPacketToSendToTestServer(TO_TESTSERVER_TESTCLIENT_ENTITY_ID);
			pktSendU32(pkt, iLastID);
			pktSend(&pkt);
		}
	}
}

static void TestClient_TestServerDisconnectCB(NetLink *link, void *user_data)
{
	bHaveSentTestServerIntro = false;
}

AUTO_COMMAND ACMD_NAME(Exit) ACMD_ACCESSLEVEL(9);
void TestClient_Exit(void)
{
	gTestClientGlobalState.bRunning = false;
}

int main(int argc, char **argv)
{
	int frametimer = 0;

	EXCEPTION_HANDLER_BEGIN;
	WAIT_FOR_DEBUGGER;

	gimmeDLLDisable(1);
	RegisterGenericGlobalTypes();
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_TESTCLIENT);
	freeEmergencyMemory();

	DO_AUTO_RUNS;

	dirMonSetBufferSize(NULL, 2*1024);
	FolderCacheChooseMode();
	FolderCacheEnableCallbacks(0);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'C', 0x808080);
	serverLibStartup(argc, argv);
	ClientController_InitLib();
	ClientController_AppendClientCommandLine("setaccountserver", getAccountServer());

	if(!gTestClientGlobalState.bNoShardMode)
	{
		loadstart_printf("Connecting ClientController to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

		while (!InitObjectTransactionManager(GetAppGlobalType(), gServerLibState.containerID, gServerLibState.transactionServerHost, gServerLibState.transactionServerPort, gServerLibState.bUseMultiplexerForTransactions, NULL))
		{
			Sleep(1000);
		}
		if (!objLocalManager())
		{
			loadend_printf("failed.");
			return 0;
		}

		loadend_printf("connected.");
	}

	loadstart_printf("Running auto startup...");
	DoAutoStartup();
	loadend_printf("...done.");

	resFinishLoading();
	stringCacheFinalizeShared();

	loadstart_printf("Initiating Test Server connection...");
	SetTestServerMessageHandler(TestClient_TestServerMessageCB);
	SetTestServerDisconnectHandler(TestClient_TestServerDisconnectCB);
	if(CheckTestServerConnection())
	{
		TestClient_SendTestServerIntro();

		while(gTestClientGlobalState.iID == 0)
		{
			commMonitor(commDefault());
			CheckTestServerConnection();
		}

		loadend_printf("...success.");
	}
	else
	{
		loadend_printf("...failed. (NOT FATAL)");
	}

	luaEnableDebug(1);
	TestClient_InitScripting();

	frametimer = timerAlloc();
	timerStart(frametimer);

	while(gTestClientGlobalState.bRunning)
	{
		static F32 frame = 0.0f;
		F32 elapsed = 0.0f;

		if(!frame)
		{
			frame = 1.0 / gTestClientGlobalState.iFPS;
		}

		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsed(frametimer), 1.0f);
		timerStart(frametimer);
		serverLibOncePerFrame();
		commMonitor(commDefault());

		TestClient_ScriptingTick();
		TestClient_ServerTick();

		elapsed = timerElapsed(frametimer);

		if(elapsed < frame)
		{
			Sleep((frame - elapsed)*1000);
		}

		autoTimerThreadFrameEnd();
	}

	ClientController_KillClient();

	if(!gTestClientGlobalState.bNoShardMode)
	{
		RemoteCommand_ServerIsGoingToDie(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientController exiting normally.", false, false);
	}

	commFlushAndCloseAllComms(5.0f);

	EXCEPTION_HANDLER_END;
	return 0;
}