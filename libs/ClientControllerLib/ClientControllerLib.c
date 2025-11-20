// ClientControllerLib
// A library to facilitate attaching to, and controlling, a Game Client.
// Provides facilities for:
// 1) Starting up a Game Client with a specified command line.
// 2) Sending commands to said Game Client.
// 3) Receiving commands back from said Game Client.

#include "ClientControllerLib.h"
#include "cmdparse.h"
#include "CrypticPorts.h"
#include "earray.h"
#include "EString.h"
#include "GlobalComm.h"
#include "net.h"
#include "StashTable.h"
#include "testclient_comm.h"
#include "utils.h"
#include "winutil.h"
#include "ServerLib.h"
#include "UTF8.h"

static QueryableProcessHandle *pClientHandle = NULL;
static NetLink *pClientLink = NULL;
static int iTestClientPort = MIN_TESTCLIENT_PORT;
static char **ppcClientCommandLine = NULL;
static char cClientExecDir[CRYPTIC_MAX_PATH];
static bool bClientDebugger = false;
static char *pcCurrentFSMState = NULL;

static StashTable sCommandsSent = NULL;

static HANDLE sCrashBeganEvent = NULL;
static HANDLE sCrashCompletedEvent = NULL;

AUTO_CMD_STRING(cClientExecDir, ExecDirectoryForClient) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(bClientDebugger, StartClientInDebugger) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

const char *ClientController_GetExecDirectoryForClient(void)
{
	return cClientExecDir;
}

void ClientController_SetExecDirectoryForClient(const char *pcDir)
{
	strcpy(cClientExecDir, pcDir);
}

AUTO_COMMAND ACMD_NAME(CmdLineClient) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void ClientController_AppendClientCommandLine(const char *pcCmdLineOption, const ACMD_SENTENCE pcCmdLineSetting)
{
	char *pcCmd = NULL;
	estrPrintf(&pcCmd, "-%s \"%s\"", pcCmdLineOption, pcCmdLineSetting);
	eaPush(&ppcClientCommandLine, pcCmd);
}

bool ClientController_MonitorCrashBeganEvents(const char *pcEventName)
{
	if(sCrashBeganEvent)
	{
		return false;
	}

	sCrashBeganEvent = CreateEvent_UTF8(NULL, FALSE, FALSE, pcEventName);

	if(!sCrashBeganEvent)
	{
		return false;
	}

	ClientController_AppendClientCommandLine("setcrashbeganevent", pcEventName);
	return true;
}

bool ClientController_MonitorCrashCompletedEvents(const char *pcEventName)
{
	if(sCrashCompletedEvent)
	{
		return false;
	}

	sCrashCompletedEvent = CreateEvent_UTF8(NULL, FALSE, FALSE, pcEventName);

	if(!sCrashCompletedEvent)
	{
		return false;
	}

	ClientController_AppendClientCommandLine("setcrashcompletedevent", pcEventName);
	return true;
}

static ClientController_CommandCB *pCommandCB = NULL;
void ClientController_SetCommandCallback(ClientController_CommandCB *pCallback)
{
	pCommandCB = pCallback;
}

static void ClientController_HandleCommandResult(Packet *pkt)
{
	U32 id = pktGetU32(pkt);
	bool bResult = pktGetBits(pkt, 1);
	const char *pcRanCommand = NULL;
	const char *pcResult;
	char *pcCommandString;

	if(bResult)
	{
		pcRanCommand = pktGetStringTemp(pkt);
	}

	pcResult = pktGetStringTemp(pkt);
	stashIntRemovePointer(sCommandsSent, id, &pcCommandString);

	if(bResult)
	{
		if(pCommandCB)
		{
			pCommandCB(pcRanCommand, id, pcResult, pcCommandString);
		}
	}
	else
	{
		printf("Command %d failed: [%s] %s\n", id, pcCommandString, pcResult);
	}

	free(pcCommandString);
}

static void ClientController_HandleCommand(Packet *pkt)
{
	const char *pcCommand = pktGetStringTemp(pkt);
	globCmdParse(pcCommand);
}

static PacketCallback *spClientControllerMessageCB = NULL;
void ClientController_SetMessageCallback(PacketCallback *pCallback)
{
	spClientControllerMessageCB = pCallback;
}

static void ClientController_HandleMsg(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	switch (cmd)
	{
	case TO_TESTCLIENT_CMD_RESULT:
		ClientController_HandleCommandResult(pkt);
		break;
	case TO_TESTCLIENT_CMD_COMMAND:
		ClientController_HandleCommand(pkt);
		break;
	default:
		if(spClientControllerMessageCB)
		{
			spClientControllerMessageCB(pkt, cmd, link, user_data);
		}
		break;
	}
}

static LinkCallback *pClientConnectCallback = NULL;
void ClientController_SetConnectCallback(LinkCallback *pCallback)
{
	pClientConnectCallback = pCallback;
}

static void ClientController_HandleConnect(NetLink *link, void *user_data)
{
	pClientLink = link;
	if(pClientConnectCallback)
	{
		pClientConnectCallback(link, user_data);
	}
}

static LinkCallback *pClientDisconnectCallback = NULL;
void ClientController_SetDisconnectCallback(LinkCallback *pCallback)
{
	pClientDisconnectCallback = pCallback;
}

static void ClientController_HandleDisconnect(NetLink *link, void *user_data)
{
	pClientLink = NULL;
	if(pClientDisconnectCallback)
	{
		pClientDisconnectCallback(link, user_data);
	}
}

void ClientController_InitLib(void)
{
	loadstart_printf("Initializing ClientControllerLib...\n");

	while(iTestClientPort <= MAX_TESTCLIENT_PORT)
	{
		if(commListen(commDefault(), LINKTYPE_DEFAULT, LINK_FORCE_FLUSH|LINK_SMALL_LISTEN, iTestClientPort, ClientController_HandleMsg, ClientController_HandleConnect, ClientController_HandleDisconnect, 0))
		{
			break;
		}
		else
		{
			++iTestClientPort;
		}
	}

	if(iTestClientPort > MAX_TESTCLIENT_PORT)
	{
		loadend_printf("...FAILED.");
	}

	sCommandsSent = stashTableCreateInt(1024);

	loadend_printf("...done.");
}

AUTO_COMMAND ACMD_NAME(StartClient);
bool ClientController_StartClient(bool bWorld, bool bGraphics, const ACMD_SENTENCE pcExtraCmdLine)
{
	char *pOrigDir = NULL;
	char *pcCmdLine = NULL;
	int i;
	U32 iPID;

	PERFINFO_AUTO_START_FUNC();
	
	if(pClientHandle)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(cClientExecDir && cClientExecDir[0])
	{
		estrPrintf(&pcCmdLine, "%s\\gameclient.exe", cClientExecDir);
		backSlashes(pcCmdLine);
	}
	else
		estrPrintf(&pcCmdLine, "gameclient.exe");
	
	estrConcatf(&pcCmdLine, " -allow_testclients -allowsharedmemory -quitontestclientdisconnect -console -nopopups2 -testclientport %d %s", iTestClientPort, pcExtraCmdLine);

	for(i = 0; i < eaSize(&ppcClientCommandLine); ++i)
	{
		estrConcatf(&pcCmdLine, " %s", ppcClientCommandLine[i]);
	}

	if(!bGraphics)
	{
		estrConcatf(&pcCmdLine, " -nographics -noaudio -nonewfx -noanimation");

		if(!bWorld)
		{
			estrConcatf(&pcCmdLine, " -noworld");
		}
	}

	if(bClientDebugger)
	{
		estrConcatf(&pcCmdLine, " -waitfordebugger");
	}

	if(cClientExecDir && cClientExecDir[0])
	{
		GetCurrentDirectory_UTF8(&pOrigDir);
		SetCurrentDirectory_UTF8(cClientExecDir);
	}

	pClientHandle = StartQueryableProcessEx(pcCmdLine, NULL, true, true, false, NULL, &iPID);
	ReportOwnedChildProcess("gameclient.exe", iPID);

	if(cClientExecDir && cClientExecDir[0])
		SetCurrentDirectory_UTF8(pOrigDir);

	estrDestroy(&pOrigDir);

	PERFINFO_AUTO_STOP();
	return (pClientHandle != NULL);
}

static bool ClientController_IsClientRunning(void)
{
	PERFINFO_AUTO_START_FUNC();

	if(!pClientHandle)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(QueryableProcessComplete(&pClientHandle, NULL))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}

static bool ClientController_IsClientConnected(void)
{
	PERFINFO_AUTO_START_FUNC();

	if(!ClientController_IsClientRunning())
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(!pClientLink)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(!linkConnected(pClientLink) || linkDisconnected(pClientLink))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}

ClientControllerState ClientController_MonitorState(void)
{
	static ClientControllerState eState = CC_NOT_RUNNING;
	DWORD waitResult = WAIT_TIMEOUT;

	if(sCrashBeganEvent)
	{
		WaitForSingleObjectWithReturn(sCrashBeganEvent, 0, waitResult);
	}

	if(waitResult == WAIT_OBJECT_0)
	{
		eState = CC_CRASHED;
	}
	else if(eState == CC_CRASHED)
	{
		if(sCrashCompletedEvent)
		{
			WaitForSingleObjectWithReturn(sCrashCompletedEvent, 0, waitResult);
		}

		if(waitResult == WAIT_OBJECT_0)
		{
			eState = CC_CRASH_COMPLETE;
		}
	}
	else if(ClientController_IsClientConnected())
	{
		eState = CC_CONNECTED;
	}
	else if(ClientController_IsClientRunning())
	{
		eState = CC_RUNNING;
	}
	else
	{
		eState = CC_NOT_RUNNING;
	}

	return eState;
}

AUTO_COMMAND ACMD_NAME(KillClient);
void ClientController_KillClient(void)
{
	PERFINFO_AUTO_START_FUNC();

	if(!pClientHandle)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(QueryableProcessComplete(&pClientHandle, NULL))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	KillQueryableProcess(&pClientHandle);

	PERFINFO_AUTO_STOP();
}

static U32 siCurCommandID = 0;
AUTO_COMMAND ACMD_NAME(SendCommandToClient);
U32 ClientController_SendCommandToClient(const ACMD_SENTENCE pchCommand)
{
	Packet *pkt = NULL;

	PERFINFO_AUTO_START_FUNC();
	pkt = pktCreate(pClientLink, FROM_TESTCLIENT_CMD_SENDCOMMAND);
	pktSendU32(pkt, ++siCurCommandID);
	pktSendString(pkt, pchCommand);
	pktSend(&pkt);
	PERFINFO_AUTO_STOP();

	stashIntAddPointer(sCommandsSent, siCurCommandID, strdup(pchCommand), false);

	return siCurCommandID;
}

const char *ClientController_GetClientFSMState(void)
{
	return pcCurrentFSMState;
}

AUTO_COMMAND ACMD_NAME(StateUpdate);
void ClientController_StateUpdate(const char *pchState)
{
	estrCopy2(&pcCurrentFSMState, pchState);
}