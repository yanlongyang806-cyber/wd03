#include "TimedCallback.h"
#include "StatusReporting.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "StructNet.h"
#include "StatusReporting_h_ast.h"
#include "UtilitiesLib.h"
#include "Estring.h"
#include "Alerts_h_Ast.h"
#include "timing.h"
#include "logging.h"
#include "wininclude.h"
#include "sysutil.h"
#include "UtilitiesLib.h"
#include "StringUtil.h"
#include "timedCallback.h"
#include "StatusReporting.h"
#include "NameValuePair.h"
#include "cmdParse.h"
#include "../../libs/patchclientlib/PatchClientLibStatusMonitoring.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define MAX_PENDING_LOGS 100

static bool gbAllowCommandsOverStatusReportingLinks = false;
AUTO_CMD_INT(gbAllowCommandsOverStatusReportingLinks, AllowCommandsOverStatusReportingLinks);

static void StatusReporting_LazyInit(void);

static StatusReporting_Wrapper sStatusWrapper = {0};
static NetLink *spLinkToControllerTracker = NULL;
static char *spControllerTrackerName = NULL;
static U32 siControllerTrackerPort = 0;

static StatusReporting_LogWrapper **sppMainPendingLogs = NULL;

//if specified, then the controller tracker will use the sentry server to restart any critical system which
//dies. The batch file needs to do any killing and restarting, and shouldn't care what folder it is execute
//from
static char *spCommandLineForRestarting = NULL; 

static char *spAllCTNames = NULL;

// Max number of alerts to mirror to Controller Tracker per STATUS_REPORTING_INTERVAL
int giStatusReportingAlertRate = 10000;
AUTO_CMD_INT(giStatusReportingAlertRate, StatusReportingAlertRate);
int giCurStatusReportingAlerts = 0;



typedef struct ExtraStatusReportee
{
	char *pToWho;
	int iPort;
	CommConnectFSM *pFSM;
	NetLink *pLink;
	StatusReporting_LogWrapper **ppPendingLogs;
	StatusReporting_ExtraReportingBehaviorFlags eFlags;
} ExtraStatusReportee;

static ExtraStatusReportee **sppExtraReportees = NULL;


AUTO_COMMAND ACMD_COMMANDLINE;
void DoExtraStatusReporting(char *pToWho)
{
	ExtraStatusReportee *pReportee = calloc(sizeof(ExtraStatusReportee), 1);
	char *pColon;
	StatusReporting_LazyInit();

	eaPush(&sppExtraReportees, pReportee);

	estrConcatf(&spAllCTNames, "%s%s", estrLength(&spAllCTNames) ? ", ": "", pToWho);

	//if there's a colon, then that specifies a port
	if ((pColon = strchr(pToWho, ':')))
	{
		if (!StringToInt_Paranoid(pColon + 1, &pReportee->iPort))
		{
			assertmsgf(0, "Unable to process ExtraStatusReportee: %s", pToWho);
		}

		pReportee->pToWho = strdup_s(pToWho, pColon - pToWho);
	}
	else
	{
		pReportee->pToWho = strdup(pToWho);
		pReportee->iPort = CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT;
	}

	pReportee->eFlags = StatusReporting_GetBehaviorFlagsForExtraStatusReporting(pReportee->pToWho, pReportee->iPort);
}


AUTO_COMMAND;
void RestartCommand(ACMD_SENTENCE fullCommandLine)
{
	estrCopy2(&sStatusWrapper.status.pRestartingCommand, fullCommandLine);
}

int StatusReportingDisconnect(NetLink* link, void *pUserData)
{
	static char *pDisconnectString = NULL;
	linkGetDisconnectReason(link, &pDisconnectString);

	printf("Disconnected as crit system from CT: %s\n", pDisconnectString);
	log_printf(LOG_MISC, "Disconnected as crit system from CT: %s\n", pDisconnectString);
	return 1;
}

U32 sStatusReportingFailAlertTime = 10;
AUTO_CMD_INT(sStatusReportingFailAlertTime, StatusReportingFailAlertTime);

static char *spMostRecentDisconnectReason = NULL;

void StatusReporting_ConnectionFailCB(void *pUserData, char *pErrorText)
{
	THROTTLE(300, TriggerAlertDeferred("CRIT_SYS_CONNECT_FAIL", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 
		"Unable to connect to %s, error message \"%s\" (will retry, this may cause performances issues particularly if its a DNS failure, change using BeginStatusReporting, ie BeginStatusReporting %s %s %u)", 
		spControllerTrackerName, pErrorText, sStatusWrapper.pMyName, spControllerTrackerName, sStatusWrapper.iMyMainMonitoringPort));
}

void StatusReporting_DisconnectionCB(void *pUserData, char *pErrorText)
{
	estrCopy2(&spMostRecentDisconnectReason, pErrorText);
}

static void LazyLastMinuteInit(void)
{
	if (!sStatusWrapper.pMyProduct)
	{
		sStatusWrapper.pMyProduct = GetProductName();
		sStatusWrapper.pMyShortProduct = GetShortProductName();
		sStatusWrapper.pMyMachineName = getHostName();
		sStatusWrapper.pMyShardName = GetShardNameFromShardInfoString();
		sStatusWrapper.pMyClusterName = ShardCommon_GetClusterName();
		sStatusWrapper.pMyOverlord = UtilitiesLib_GetOverlordName();

		sStatusWrapper.eMyType = GetAppGlobalType();
		sStatusWrapper.iMyID = GetAppGlobalID();
		sStatusWrapper.pVersion = GetUsefulVersionString();
		sStatusWrapper.iMyPid = getpid();
		sStatusWrapper.pMyExeName = getExecutableName();


		if (!sStatusWrapper.pMyName)
		{
			if (sStatusWrapper.pMyShardName)
			{
				sStatusWrapper.pMyName = strdupf("%s_%s_%u", sStatusWrapper.pMyShardName, GlobalTypeToName(sStatusWrapper.eMyType), sStatusWrapper.iMyID);
			}
			else
			{
				sStatusWrapper.pMyName = strdupf("%s_%u", GlobalTypeToName(sStatusWrapper.eMyType), sStatusWrapper.iMyID);
			}
		}
	}
}

static void SendPendingLogs(StatusReporting_LogWrapper ***pppLogs, NetLink *pLink)
{
	FOR_EACH_IN_EARRAY_FORWARDS((*pppLogs), StatusReporting_LogWrapper, pLog)
	{
		Packet *pPack = pktCreate(pLink, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_LOG);
		ParserSendStructSafe(parse_StatusReporting_LogWrapper, pPack, pLog);
		pktSend(&pPack);
	}
	FOR_EACH_END;

	eaDestroyStruct(pppLogs, parse_StatusReporting_LogWrapper);
}

static void SendStatus(void)
{
	Packet *pPack;
	static S64 iLastTimeUtilitiesLibTicks = 0;
	int i;

	static S64 iLastTime = 0;
	S64 iCurTime = timeGetTime();
	S64 iDeltaTime = iLastTime ? iCurTime - iLastTime : 0;
	S64 iDeltaTicks = gUtilitiesLibTicks - iLastTimeUtilitiesLibTicks;
	PCLStatusMonitoringUpdate **sppStatusUpdates = NULL;

	iLastTimeUtilitiesLibTicks = gUtilitiesLibTicks;
	iLastTime = iCurTime;

	if (iDeltaTime)
	{
		sStatusWrapper.status.fFPS = ((float)iDeltaTicks ) * 1000 / iDeltaTime;
	}

	for (i=0; i < ALERTLEVEL_COUNT; i++)
	{
		sStatusWrapper.status.iNumAlerts[i] = Alerts_GetCountByLevel(i);
	}
	
	sStatusWrapper.status.eState = StatusReporting_GetSelfReportedState();
	sStatusWrapper.status.pNameValuePairs = StatusReporting_GetSelfReportedNamedValuePairs();

	LazyLastMinuteInit();


	if (spLinkToControllerTracker)
	{
		pPack = pktCreate(spLinkToControllerTracker, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_GENERIC_STATUS);
		ParserSendStructSafe(parse_StatusReporting_Wrapper, pPack, &sStatusWrapper);
		pktSend(&pPack);

		SendPendingLogs(&sppMainPendingLogs, spLinkToControllerTracker);
	}

	FOR_EACH_IN_EARRAY_FORWARDS(sppExtraReportees, ExtraStatusReportee, pReportee)
	{
		if (pReportee->pLink && linkConnected(pReportee->pLink))
		{
			if (pReportee->eFlags & EXTRAREPORTING_PATCHING_STATUSES)
			{
				if (!sppStatusUpdates)
				{
					PCLStatusMonitoring_GetAllStatuses(&sppStatusUpdates);
				}

				sStatusWrapper.status.ppPCLStatuses = sppStatusUpdates;
			}

			pPack = pktCreate(pReportee->pLink, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_GENERIC_STATUS);
			ParserSendStructSafe(parse_StatusReporting_Wrapper, pPack, &sStatusWrapper);
			pktSend(&pPack);

			SendPendingLogs(&pReportee->ppPendingLogs, pReportee->pLink);

			sStatusWrapper.status.ppPCLStatuses = NULL;


		}
	}
	FOR_EACH_END;

	eaDestroy(&sppStatusUpdates);
}


int cmdParseForStatusReporting(char *pCommand, int iAccessLevel, char **ppRetString, bool *pbReturnIsSlow, int iClientID, int iCommandRequestID, U32 iMCPID, SlowCmdReturnCallbackFunc *pSlowReturnCB, void *pSlowReturnUserData, const char *pAuthNameAndIP)
{
	CmdContext		cmd_context = {0};
	char *msg = NULL;
	int result = 0;
	bool bReturnIsSlow = false;

	cmd_context.flags |= CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL;
	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_STATUSREPORTING;
	cmd_context.slowReturnInfo.iClientID = iClientID;
	cmd_context.slowReturnInfo.iCommandRequestID = iCommandRequestID;
	cmd_context.slowReturnInfo.iMCPID = iMCPID;
	cmd_context.slowReturnInfo.pSlowReturnCB = pSlowReturnCB;
	cmd_context.slowReturnInfo.pUserData = pSlowReturnUserData;
	cmd_context.pAuthNameAndIP = pAuthNameAndIP;

	cmd_context.access_level = iAccessLevel;

	InitCmdOutput(cmd_context,msg);

	result = cmdParseAndExecute(&gGlobalCmdList,pCommand,&cmd_context);

	if (cmd_context.slowReturnInfo.bDoingSlowReturn)
	{
		assertmsg(pbReturnIsSlow, "Someone is trying to do a slow return for a command which should have no return at all");
		*pbReturnIsSlow = true;
	}
	else
	{
		if (ppRetString)
		{
			if (result)
			{
				if (estrLength(&msg))
				{
					estrPrintf(ppRetString, "Command \"%s\" on %s completed successfully. Return string:<br>%s",
						pCommand, GlobalTypeToName(GetAppGlobalType()), msg);
				}
				else
				{
					estrPrintf(ppRetString, "Command \"%s\" on %s completed successfully. (No return string)",
						pCommand, GlobalTypeToName(GetAppGlobalType()));
				}
			}
			else
			{
				estrPrintf(ppRetString, "Command \"%s\" on %s FAILED",
					pCommand, GlobalTypeToName(GetAppGlobalType()));
			}
		}
	}

	CleanupCmdOutput(cmd_context);

	return result;
}

static void StatusReportingSlowReturnCB(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData)
{
	NetLink *pLink = linkFindByID(iClientID);

	if (pLink)
	{
		Packet *pReturnPack = pktCreate(pLink, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_COMMAND_RESULT);
	
		pktSendBits(pReturnPack, 32, iRequestID);
		pktSendBits(pReturnPack, 1, 1);
		pktSendString(pReturnPack, pMessageString);
		pktSend(&pReturnPack);
	}
}



static void handleCommandThroughStatusReporting(Packet *pPkt, NetLink *pLink)
{
	int iCommandID = pktGetBits(pPkt, 32);
	int iAccessLevel = pktGetBits(pPkt, 32);
	char *pAuthNameAndIP = pktGetStringTemp(pPkt);
	char *pCommandString = pktGetStringTemp(pPkt);
	int iLinkID = linkID(pLink);

	char *pRetString = NULL;
	bool bReturnIsSlow = false;

	if (!cmdParseForStatusReporting(pCommandString, iAccessLevel, &pRetString, &bReturnIsSlow, iLinkID, iCommandID, 0, 
		StatusReportingSlowReturnCB, NULL, pAuthNameAndIP))
	{
		Packet *pReturnPack = pktCreate(pLink, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_COMMAND_RESULT);
		pktSendBits(pReturnPack, 32, iCommandID);
		pktSendBits(pReturnPack, 1, 0);
		pktSend(&pReturnPack);
		return;
	}

	if (!bReturnIsSlow)
	{
		Packet *pReturnPack = pktCreate(pLink, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_COMMAND_RESULT);
	
		pktSendBits(pReturnPack, 32, iCommandID);
		pktSendBits(pReturnPack, 1, 1);
		pktSendString(pReturnPack, pRetString);
		pktSend(&pReturnPack);
	}

	estrDestroy(&pRetString);



}

void StatusReportingPacket(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_HERE_IS_ALERT_FROM_SOMEONE_ELSE_ABOUT_YOU:
		{
			Alert *pAlert = StructCreate(parse_Alert);
			ParserRecvStructSafe(parse_Alert, pkt, pAlert);
			pAlert->bWasSentByCriticalSystems = true;

			TriggerAlertByStruct(pAlert);
		}
		break;

	xcase FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_SAFE_SHUTDOWN:
		StatusReporting_ShutdownSafely();
		break;

	xcase FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_COMMAND:
		if (gbAllowCommandsOverStatusReportingLinks)
		{
			handleCommandThroughStatusReporting(pkt, link);
		}
		break;

	}
}

bool StatusReporting_AttemptToConnect(void)
{
	static CommConnectFSM *pFSM = NULL;
	static U32 iDisconnectBeginTime = 0;
	static bool sbAlertedThisDisconnect = false;
	static bool sbPreviouslyConnected = false;

	if (!spControllerTrackerName)
	{
		return false;
	}

	if (commConnectFSMForTickFunctionWithRetrying(&pFSM, &spLinkToControllerTracker, 
		"Status reporting link", 2.0f, commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,spControllerTrackerName,siControllerTrackerPort,
			StatusReportingPacket,0,StatusReportingDisconnect,0, StatusReporting_ConnectionFailCB, NULL, StatusReporting_DisconnectionCB, NULL))
	{
		if (!sbPreviouslyConnected)
		{
			//we weren't connected, now are... always send our status update immediately upon connection, no harm in sending it twice sometimes
			//but we want it sent before any alerts
			sbPreviouslyConnected = true;
			SendStatus();

		}

		iDisconnectBeginTime = 0;
		sbAlertedThisDisconnect = false;

		return true;
	}
	else
	{
		sbPreviouslyConnected = false;

		if (!iDisconnectBeginTime)
		{
			iDisconnectBeginTime = timeSecondsSince2000();
			return false;
		}

		if (!sbAlertedThisDisconnect && timeSecondsSince2000() - sStatusReportingFailAlertTime > iDisconnectBeginTime)
		{
			sbAlertedThisDisconnect = true;
			TriggerAlertDeferred("CRIT_SYS_CONNECT_FAIL", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "Unable to connect to %s for %d consecutive seconds, most recent disconnect due to: %s (will retry, this may cause performances issues particularly if its a DNS failure, change using BeginStatusReporting, ie BeginStatusReporting %s %s %u)", 
				spControllerTrackerName, sStatusReportingFailAlertTime, 
				spMostRecentDisconnectReason ? spMostRecentDisconnectReason : "(Never connected)",
				sStatusWrapper.pMyName, spControllerTrackerName, sStatusWrapper.iMyMainMonitoringPort);
		}
		return false;
	}
}

static void ExtraStatusReportingPacket(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_SAFE_SHUTDOWN:
		StatusReporting_ShutdownSafely();
		break;
	xcase FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_COMMAND:
		if (gbAllowCommandsOverStatusReportingLinks)
		{
			handleCommandThroughStatusReporting(pkt, link);
		}
		break;

	}
}

static bool StatusReporting_AttemptExtraConnections(void)
{
	bool bAtLeastOneConnected = false;

	FOR_EACH_IN_EARRAY_FORWARDS(sppExtraReportees, ExtraStatusReportee, pReportee)
	{
		if (commConnectFSMForTickFunctionWithRetrying(&pReportee->pFSM, &pReportee->pLink, 
			"Extra Status reporting link", 2.0f, commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,pReportee->pToWho,pReportee->iPort,
			ExtraStatusReportingPacket,0,0,0, NULL, NULL, NULL, NULL))
		{
			bAtLeastOneConnected = true;
		}
	}
	FOR_EACH_END;

	return bAtLeastOneConnected;
}

void StatusReporting_Tick(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	bool bMainConnection;
	bool bExtraConnections;
	//printf("status reporting tick\n");
		
	PERFINFO_AUTO_START_FUNC();

	bMainConnection = StatusReporting_AttemptToConnect();
	bExtraConnections = StatusReporting_AttemptExtraConnections();



	if (bMainConnection || bExtraConnections)
	{
		if (callback)
		{
			ONCE(TimedCallback_Remove(callback);TimedCallback_Add(StatusReporting_Tick, NULL, STATUS_REPORTING_INTERVAL_NORMAL));
		}

		giCurStatusReportingAlerts = 0;
		SendStatus();
	}

	PERFINFO_AUTO_STOP();
}

void StatusReporting_ForceUpdate(void)
{
	StatusReporting_Tick(NULL, 0, NULL);
}

bool sbStatusReportingGeneratesMessagesBoxesForAlertsWhenNotConnected = false;
bool sbStatusReportingUsesSendAlertExeWhenNotConnected = false;

static void MaybeUseSendAlert(Alert *pAlert)
{
	char *pCmdLine = NULL;
	char *pFullAlertString = NULL;
	char *pFullAlertStringSuperSec = NULL;

	if (!sbStatusReportingUsesSendAlertExeWhenNotConnected)
	{
		return;
	}


	estrStackCreate(&pCmdLine);
	estrStackCreate(&pFullAlertString);
	estrStackCreate(&pFullAlertStringSuperSec);

	ParserWriteText(&pFullAlertString, parse_Alert, pAlert, 0, 0, 0);
	estrSuperEscapeString(&pFullAlertStringSuperSec, pFullAlertString);

	estrPrintf(&pCmdLine, "sendAlert -controllerTrackerName %s -criticalSystemName %s -SuperEscapedFullAlert %s",
		StatusReporting_GetControllerTrackerName(), StatusReporting_GetMyName(), pFullAlertStringSuperSec);

	system_detach(pCmdLine, true, true);

	estrDestroy(&pCmdLine);
	estrDestroy(&pFullAlertString);
	estrDestroy(&pFullAlertStringSuperSec);
}

static void MaybeGenerateMessageBox(Alert *pAlert)
{
	static SimpleEventCounter *pMessageBoxEventCounter = NULL; 
	static bool bSuppressAll = false;

	if (!sbStatusReportingGeneratesMessagesBoxesForAlertsWhenNotConnected)
	{
		return;
	}

	if (!pMessageBoxEventCounter)
	{
		pMessageBoxEventCounter = SimpleEventCounter_Create(10, 60, 600);
	}

	if (SimpleEventCounter_ItHappened(pMessageBoxEventCounter, timeSecondsSince2000()))
	{
		if (!bSuppressAll)
		{
			bSuppressAll = true;
			system_detach("messagebox -title ALERT -message \"Alerts are coming in faster than we feel comfortable creating message boxes. You will not see this warning or any messagebox alerts again. Look at the servermonitor to see all alerts\" -icon error", true, true);
		}
	}

	if (!bSuppressAll)
	{
		char *pTempString1 = NULL;
		char *pTempString2 = NULL;

		estrCopy2(&pTempString1, pAlert->pString);
		estrReplaceOccurrences(&pTempString1, "\"", "\\q");

		estrPrintf(&pTempString2, "messagebox -title ALERT -message \"%s alert: %s\" -icon error",
			GlobalTypeToName(GetAppGlobalType()), pTempString1);

		system_detach(pTempString2, true, true);

		estrDestroy(&pTempString1);
		estrDestroy(&pTempString2);
	}
}

/* NOTE NOTE NOTE NOTE this must be in sync with the SendAlert.exe utility*/
void StatusReporting_AlertCB(Alert *pAlert)
{
	Packet *pPack;

	if (pAlert->bWasSentByCriticalSystems)
	{
		return;
	}

	if (!StatusReporting_AttemptToConnect())
	{
		MaybeGenerateMessageBox(pAlert);
		MaybeUseSendAlert(pAlert);
		return;
	}

	if(giStatusReportingAlertRate && giCurStatusReportingAlerts >= giStatusReportingAlertRate)
	{
		return;
	}

	LazyLastMinuteInit();

	++giCurStatusReportingAlerts;
	pPack = pktCreate(spLinkToControllerTracker, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_ALERT);
	pktSendString(pPack, sStatusWrapper.pMyName);
	ParserSendStructSafe(parse_Alert, pPack, pAlert);
	pktSend(&pPack);
}
/* NOTE NOTE NOTE NOTE this must be in sync with the SendAlert.exe utility*/

static void StatusReporting_LazyInit(void)
{



	TimedCallback_Add(StatusReporting_Tick, NULL, STATUS_REPORTING_INTERVAL_BEFORE_CONNECTION);
}

AUTO_COMMAND ACMD_COMMANDLINE;
void BeginStatusReporting(const char *pMyName, const char *pControllerTrackerName, int iMyMainPortNumber)
{
	char *pColon;

	StatusReporting_LazyInit();

	assert(pMyName[0] && pControllerTrackerName[0]);

	estrConcatf(&spAllCTNames, "%s%s", estrLength(&spAllCTNames) ? ", ": "", pControllerTrackerName);


	SAFE_FREE(sStatusWrapper.pMyName);
	sStatusWrapper.pMyName = strdup(pMyName);

	if ((pColon = strchr(pControllerTrackerName, ':')))
	{
		if (!StringToInt_Paranoid(pColon + 1, &siControllerTrackerPort))
		{
			assertmsgf(0, "Unable to process BeginStatusReporting, badly formed CT name: %s", pControllerTrackerName);
		}

		spControllerTrackerName = strdup_s(pControllerTrackerName, pColon - pControllerTrackerName);

	}
	else
	{
		spControllerTrackerName = strdup(pControllerTrackerName);
		siControllerTrackerPort = CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT;
	}
	sStatusWrapper.iMyMainMonitoringPort = iMyMainPortNumber;
	AddFixupAlertCB(StatusReporting_AlertCB);
}

//launches a local controllertracker, reports to it for status reporting, has it send emails to (presumably) me
AUTO_COMMAND ACMD_COMMANDLINE;
void AutoBeginStatusReporting(const char *pWhoToEmail)
{
	char *pCommandLine = NULL;
	char *pExeName;
	char *pShortExeName = NULL;

	sStatusWrapper.pMyName = strdupf("%s_AutoCreated", GlobalTypeToName(GetAppGlobalType()));
	sStatusWrapper.eMyType = GetAppGlobalType();
	sStatusWrapper.iMyID = GetAppGlobalID();
	sStatusWrapper.pVersion = GetUsefulVersionString();

	spControllerTrackerName = strdup("localhost");

	pExeName = getExecutableName();
	estrGetDirAndFileName(pExeName, NULL, &pShortExeName);


	estrPrintf(&pCommandLine, "newControllerTracker.exe -CriticalSystemToCreateAtStartup %s -AutoMailingListRecipient %s -KillMeWhenAnotherProcessDies %s %u",
		sStatusWrapper.pMyName, pWhoToEmail, pShortExeName, getpid());

	system_detach(pCommandLine, false, false);

	estrDestroy(&pCommandLine);
	estrDestroy(&pShortExeName);

	loadstart_printf("Waiting for auto-created controller tracker to start up for status reporting...");
	while (!StatusReporting_AttemptToConnect())
	{
		commMonitor(commDefault());
		Sleep(1);
	}
	loadstart_printf("done\n");

	TimedCallback_Add(StatusReporting_Tick, NULL, STATUS_REPORTING_INTERVAL_BEFORE_CONNECTION);

	AddFixupAlertCB(StatusReporting_AlertCB);


}

enumStatusReportingState StatusReporting_GetState(void)
{
	if (spControllerTrackerName == NULL)
	{
		return STATUSREPORTING_OFF;
	}

	if (spLinkToControllerTracker && linkConnected(spLinkToControllerTracker) && !linkDisconnected(spLinkToControllerTracker))
	{
		return STATUSREPORTING_CONNECTED;
	}

	return STATUSREPORTING_NOT_CONNECTED;
}

void StatusReporting_SetGenericPortNum(int iPortNum)
{
	sStatusWrapper.iMyGenericMonitoringPort = iPortNum;
}


char *StatusReporting_GetMyName(void)
{
	LazyLastMinuteInit();

	return sStatusWrapper.pMyName;
}

char *StatusReporting_GetControllerTrackerName(void)
{
	return spControllerTrackerName;
}

char *StatusReporting_GetAllControllerTrackerNames(void)
{
	return spAllCTNames;
}
	


void DEFAULT_LATELINK_StatusReporting_ShutdownSafely(void)
{}


void StatusReporting_Log(FORMAT_STR const char* format, ...)
{
	Packet *pPack;
	StatusReporting_LogWrapper *pLog = StructCreate(parse_StatusReporting_LogWrapper);
	estrGetVarArgs(&pLog->pLog, format);

	LazyLastMinuteInit();

	pLog->pMyName = strdup(sStatusWrapper.pMyName);
	pLog->iTime = timeSecondsSince2000();

	if (spLinkToControllerTracker)
	{
		pPack = pktCreate(spLinkToControllerTracker, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_LOG);
		ParserSendStructSafe(parse_StatusReporting_LogWrapper, pPack, pLog);
		pktSend(&pPack);
	}
	else
	{
		eaPush(&sppMainPendingLogs, StructClone(parse_StatusReporting_LogWrapper, pLog));
		if (eaSize(&sppMainPendingLogs) > MAX_PENDING_LOGS)
		{
			StructDestroy(parse_StatusReporting_LogWrapper, eaRemove(&sppMainPendingLogs, 0));
		}
	}

	FOR_EACH_IN_EARRAY_FORWARDS(sppExtraReportees, ExtraStatusReportee, pReportee)
	{
		if (pReportee->pLink && linkConnected(pReportee->pLink))
		{
			pPack = pktCreate(pReportee->pLink, FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_LOG);
			ParserSendStructSafe(parse_StatusReporting_LogWrapper, pPack, pLog);
			pktSend(&pPack);
		}
		else
		{
			eaPush(&pReportee->ppPendingLogs, StructClone(parse_StatusReporting_LogWrapper, pLog));
			if (eaSize(&pReportee->ppPendingLogs) > MAX_PENDING_LOGS)
			{
				StructDestroy(parse_StatusReporting_LogWrapper, eaRemove(&pReportee->ppPendingLogs, 0));
			}
		}
	}
	FOR_EACH_END;

	StructDestroy(parse_StatusReporting_LogWrapper, pLog);
}


void StatusReporting_LogFromBGThreadCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pStr)
{
	StatusReporting_Log("%s", pStr);
	estrDestroy(&pStr);
}

void StatusReporting_LogFromBGThread(FORMAT_STR const char* format, ...)
{
	char *pActualStr = NULL;
	estrGetVarArgs(&pActualStr, format);

	TimedCallback_Run(StatusReporting_LogFromBGThreadCB, pActualStr, 0.01f);
}

 int DEFAULT_LATELINK_StatusReporting_GetSelfReportedState(void)
 {
	 return STATUS_UNSPECIFIED;
 }


NameValuePairList *DEFAULT_LATELINK_StatusReporting_GetSelfReportedNamedValuePairs(void)
{
	return NULL;
}

int DEFAULT_LATELINK_StatusReporting_GetBehaviorFlagsForExtraStatusReporting(char *pName, int iPort)
{
	return 0;
}

#include "StatusReporting_h_ast.c"
