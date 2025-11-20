#include "TestClientScripting.h"

#include "ClientControllerLib.h"
#include "cmdparse.h"
#include "luaScriptLib.h"
#include "net.h"
#include "pyLib.h"
#include "structNet.h"
#include "TestClient.h"
#include "testclient_comm.h"
#include "TestClientCommon.h"
#include "TestClientLua.h"
#include "TestClientPython.h"
#include "textparser.h"
#include "timing.h"

#include "TestClientCommon_h_ast.h"

static bool sbPythonMode = false;
int gScriptTimer;

AUTO_CMD_STRING(gTestClientGlobalState.cScriptName, ScriptName) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(sbPythonMode, PythonMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

AUTO_COMMAND ACMD_NAME(TCSet) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void TestClient_InitScriptSet(const char *pcVarName, const char *pcValue)
{
	if(sbPythonMode)
	{
		pyLibInitSet(pcVarName, pcValue);
	}
	else
	{
		luaInitSet(pcVarName, pcValue);
	}
}

AUTO_COMMAND ACMD_NAME(TCAppend) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void TestClient_InitScriptAppend(const char *pcVarName, const char *pcValue)
{
	if(sbPythonMode)
	{
		pyLibInitAppend(pcVarName, pcValue);
	}
	else
	{
		luaInitInsert(pcVarName, pcValue);
	}
}

AUTO_COMMAND ACMD_NAME(TCRun) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void TestClient_InitScriptRun(const char *pcScriptString)
{
	if(sbPythonMode)
	{
		pyLibInitRun(pcScriptString);
	}
	else
	{
		luaInitRun(pcScriptString);
	}
}

static void TestClient_HandleClientMessage(Packet *pkt, int cmd, NetLink *link, void *user_data)
{
	switch(cmd)
	{
	case TO_TESTCLIENT_CMD_UPDATE:
		PERFINFO_AUTO_START("TO_TESTCLIENT_CMD_UPDATE", 1);
		StructReset(parse_TestClientStateUpdate, gTestClientGlobalState.pLatestState);
		ParserRecv(parse_TestClientStateUpdate, pkt, gTestClientGlobalState.pLatestState, 0);
		PERFINFO_AUTO_STOP();
		break;
	}
}

AUTO_COMMAND ACMD_NAME(Status);
void TestClient_ScriptStatus(ACMD_SENTENCE pcStatus)
{
	estrCopy2(&gTestClientGlobalState.pcStatus, pcStatus);
}

AUTO_COMMAND ACMD_NAME(PushChat);
void TestClient_PushChat(char *channel, ContainerID senderID, char *sender, ACMD_SENTENCE message)
{
	if(senderID == gTestClientGlobalState.pLatestState->iID)
	{
		return;
	}

	if(message[0] == '!')
	{
		if(senderID && senderID != gTestClientGlobalState.iOwnerID)
		{
			printf("RESTRICTED! Received auto command \"%s\" from non-owner \"%s\"!\n", message, sender);
			return;
		}

		++message;

		if(message[0] == '!')
		{
			++message;
			printf("Executing local auto command: %s\n", message);
			globCmdParse(message);
		}
		else
		{
			printf("Executing client auto command: %s\n", message);
			ClientController_SendCommandToClient(message);
		}
	}
	else if(message[0] == '#')
	{
		if(senderID && senderID != gTestClientGlobalState.iOwnerID)
		{
			printf("RESTRICTED! Received script command \"%s\" from non-owner \"%s\"!\n", message, sender);
			return;
		}

		++message;
		printf("Executing script command: %s\n", message);
		TestClient_ScriptExecute(message);
	}
	else
	{
		TestClientChatEntry *pEntry = calloc(1, sizeof(TestClientChatEntry));
		estrCopy2(&pEntry->pChannel, channel);
		estrCopy2(&pEntry->pSender, sender);
		estrCopy2(&pEntry->pMessage, message);
		eaPush(&gTestClientGlobalState.ppChat, pEntry);

		if(eaSize(&gTestClientGlobalState.ppChat) > TC_MAX_CHAT_QUEUE)
		{
			pEntry = eaRemove(&gTestClientGlobalState.ppChat, 0);
			estrDestroy(&pEntry->pChannel);
			estrDestroy(&pEntry->pSender);
			estrDestroy(&pEntry->pMessage);
			free(pEntry);
		}
	}
}

AUTO_COMMAND ACMD_NAME(PushNotify);
void TestClient_PushNotify(const char *pchName, const char *pchObject, ACMD_SENTENCE pchString)
{
	TestClientNotifyEntry *pEntry = calloc(1, sizeof(TestClientNotifyEntry));
	estrCopy2(&pEntry->pName, pchName);
	estrCopy2(&pEntry->pObject, pchObject);
	estrCopy2(&pEntry->pString, pchString);
	eaPush(&gTestClientGlobalState.ppNotify, pEntry);

	if(eaSize(&gTestClientGlobalState.ppNotify) > TC_MAX_NOTIFY_QUEUE)
	{
		pEntry = eaRemove(&gTestClientGlobalState.ppNotify, 0);
		estrDestroy(&pEntry->pName);
		estrDestroy(&pEntry->pObject);
		estrDestroy(&pEntry->pString);
		free(pEntry);
	}
}

void TestClient_GetQueuedChat(TestClientChatEntry ***pppOutChat)
{
	eaCopy(pppOutChat, &gTestClientGlobalState.ppChat);
	eaClear(&gTestClientGlobalState.ppChat);
}

void TestClient_GetQueuedNotify(TestClientNotifyEntry ***pppOutNotify)
{
	eaCopy(pppOutNotify, &gTestClientGlobalState.ppNotify);
	eaClear(&gTestClientGlobalState.ppNotify);
}

void TestClient_ScriptExecute(const char *script)
{
	if(sbPythonMode)
	{
		pyLibExecute(script);
	}
	else
	{
		TestClient_LuaExecute(script);
	}
}

void TestClient_ClientScriptExecute(const char *script)
{
	char *pcScriptCmd = NULL;

	estrPrintf(&pcScriptCmd, "gclScript_Run \"%s\"", script);
	ClientController_SendCommandToClient(pcScriptCmd);
	estrDestroy(&pcScriptCmd);
}

void TestClient_InitScripting(void)
{
	// Begin crash monitoring
	ClientController_MonitorCrashBeganEvents(TESTCLIENT_CRASHBEGAN_EVENT);
	ClientController_MonitorCrashCompletedEvents(TESTCLIENT_CRASHCOMPLETED_EVENT);
	ClientController_SetMessageCallback(TestClient_HandleClientMessage);

	// Initialize some global stuff
	gTestClientGlobalState.pLatestState = StructCreate(parse_TestClientStateUpdate);
	gScriptTimer = timerAlloc();
	timerStart(gScriptTimer);

	if(sbPythonMode)
	{
		TestClient_InitPython();
	}
	else
	{
		TestClient_InitLua();
	}
}

void TestClient_ScriptingTick(void)
{
	if(sbPythonMode)
	{
		TestClient_PythonTick();
	}
	else
	{
		TestClient_LuaTick();
	}
}

F32 TestClient_GetScriptTime(void)
{
	return timerElapsed(gScriptTimer);
}