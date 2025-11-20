#include "SimpleStatusMonitoring.h"
#include "StatusReporting.h"
#include "net.h"
#include "crypticPorts.h"
#include "TimedCallback.h"
#include "globalComm.h"
#include "SimpleStatusMonitoring_h_ast.h"
#include "StatusReporting_h_ast.h"
#include "structNet.h"
#include "stashTable.h"
#include "resourceInfo.h"
#include "SentryServerComm.h"
#include "sock.h"
#include "stringCache.h"
#include "StringUtil.h"
#include "estring.h"
#include "SimpleStatusMonitoring_c_ast.h"
#include "cmdparse.h"


static SimpleStatusMonitoringFlags sMonitoringFlags = 0;

static SimpleMonitoringStatus **sppMonitoreesFromCommandLine = NULL;

StashTable gSimpleMonitoringStatusByName = NULL;



static bool sbQuerySentryServerForProcessExistence = false;
AUTO_CMD_INT(sbQuerySentryServerForProcessExistence, SimpleStatusMonitoring_UseSentryServer);

static char **sppSystemsForSentryServerQuery = NULL;
static char **sppMachinesForQuery = NULL;


static void SimpleStatusMonitoring_HandleCommandResult(Packet *pak);

//all the servers whose status we're monitoring that report to a particular overlord
AUTO_STRUCT;
typedef struct OverlordList
{
	char *pName; AST(POOL_STRING)
	SimpleMonitoringStatus **ppServers; //unowned

	NetLink *pLink; NO_AST
	CommConnectFSM *pFSM; NO_AST
} OverlordList;

AUTO_STRUCT;
typedef struct ServerListForOverlord
{
	const char *pMyName;
	SimpleMonitoringStatus **ppServers;
} ServerListForOverlord;

StashTable sOverlordsByName = NULL;




void AddOverlord(char *pName /*pooled*/, SimpleMonitoringStatus *pStatus)
{
	OverlordList *pOverlord;

	if (!sOverlordsByName)
	{
		sOverlordsByName = stashTableCreateAddress(16);
	}

	if (!stashFindPointer(sOverlordsByName, pName, &pOverlord))
	{
		pOverlord = StructCreate(parse_OverlordList);
		pOverlord->pName = pName;
		stashAddPointer(sOverlordsByName, pName, pOverlord, true);
	}

	//note that eapushing something into an indexed list does nothing if it's already there, so it's
	//more efficient to do this than to first do a find or something
	eaPush(&pOverlord->ppServers, pStatus);
}

static void RemoveSystemFromOverlord(char *pOverlordName, SimpleMonitoringStatus *pStatus)
{
	OverlordList *pOverlord;

	if (stashFindPointer(sOverlordsByName, pOverlordName, &pOverlord))
	{
		int iIndex = eaIndexedFindUsingString(&pOverlord->ppServers, pStatus->pName);
		if (iIndex >= 0)
		{
			eaRemove(&pOverlord->ppServers, iIndex);
		}
	}
}



AUTO_FIXUPFUNC;
TextParserResult SimpleMonitoringStatus_Fixup(SimpleMonitoringStatus *pStatus, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pStatus->pLink)
		{
			linkSetUserData(pStatus->pLink, NULL);
			pStatus->pLink = NULL;
		}

		if (pStatus->pMyOverlord)
		{
			RemoveSystemFromOverlord(pStatus->pMyOverlord, pStatus);
		}
		break;
	}

	return true;
}


static bool SystemIsInList(SimpleMonitoringStatus *pStatus, SentryProcess_FromSimpleQuery_List *pList)
{
	FOR_EACH_IN_EARRAY(pList->ppProcesses, SentryProcess_FromSimpleQuery, pProcess)
	{
		if (pProcess->iPID == pStatus->status.iMyPid)
		{
			char *pShort1 = NULL;
			char *pShort2 = NULL;
			bool bMatch;

			estrStackCreate(&pShort1);
			estrStackCreate(&pShort2);

			estrGetDirAndFileName(pProcess->pProcessName, NULL, &pShort1);
			estrGetDirAndFileName(pStatus->status.pMyExeName, NULL, &pShort2);

			estrTruncateAtFirstOccurrence(&pShort1, '.');
			estrTruncateAtFirstOccurrence(&pShort2, '.');

			bMatch = (stricmp_safe(pShort1, pShort2) == 0);

			estrDestroy(&pShort1);
			estrDestroy(&pShort2);

			return bMatch;

		}

	}
	FOR_EACH_END;
	return false;
}

static void ListOfExesCB(SentryProcess_FromSimpleQuery_List *pList, void *pUserData)
{
	int iSystemNum;


	for (iSystemNum = eaSize(&sppSystemsForSentryServerQuery) - 1; iSystemNum >= 0; iSystemNum --)
	{
		SimpleMonitoringStatus *pStatus;
		if (stashFindPointer(gSimpleMonitoringStatusByName, sppSystemsForSentryServerQuery[iSystemNum], &pStatus))
		{
			if (pStatus->eConnectionState == SIMPLEMONITORING_DISCONNECTED || pStatus->eConnectionState == SIMPLEMONITORING_STALLED)
			{
				if (stricmp_safe(pStatus->status.pMyMachineName, pList->pMachineName) == 0)
				{
					if (SystemIsInList(pStatus, pList))
					{
						//do nothing
					}
					else
					{
						pStatus->eConnectionState = SIMPLEMONITORING_GONE;
						pStatus->iLastActivityTime = timeSecondsSince2000();
						eaRemove(&sppSystemsForSentryServerQuery, iSystemNum);
					}
				}
			}
			else
			{
				eaRemove(&sppSystemsForSentryServerQuery, iSystemNum);
			}
		}
		else
		{
			eaRemove(&sppSystemsForSentryServerQuery, iSystemNum);
		}
	}	
	

	eaFindAndRemove(&sppMachinesForQuery, allocAddString(pList->pMachineName));
	if (!eaSize(&sppMachinesForQuery))
	{
		eaDestroy(&sppSystemsForSentryServerQuery);
	}
}


static void SimpleStatusMonitoring_SentryServerCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{

	if (eaSize(&sppMachinesForQuery))
	{
		//still waiting for one or more responses from last time
		return;
	}

	FOR_EACH_IN_STASHTABLE(gSimpleMonitoringStatusByName, SimpleMonitoringStatus, pStatus)
	{
		if (!pStatus->pMachineNameForwardedFrom )
		{
			switch (pStatus->eConnectionState)
			{
			case SIMPLEMONITORING_DISCONNECTED:
			case SIMPLEMONITORING_STALLED:
				if (pStatus->status.pMyMachineName)
				{
					eaPushUnique(&sppSystemsForSentryServerQuery, (void*)allocAddString(pStatus->pName));
					eaPushUnique(&sppMachinesForQuery, (void*)allocAddString(pStatus->status.pMyMachineName));
				}
			}
		}
	}
	FOR_EACH_END;

	if (eaSize(&sppMachinesForQuery))
	{
		FOR_EACH_IN_EARRAY(sppMachinesForQuery, char, pMachineName)
		{
			SentryServerComm_QueryMachineForRunningExes_Simple(pMachineName, ListOfExesCB, NULL);
		}
		FOR_EACH_END;
	}
}

static void SimpleStatusMonitoring_TimedCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	U32 iCurTime = timeSecondsSince2000();

	ONCE(if (sbQuerySentryServerForProcessExistence) TimedCallback_Add(SimpleStatusMonitoring_SentryServerCB, NULL, 10.0f));

	FOR_EACH_IN_STASHTABLE(gSimpleMonitoringStatusByName, SimpleMonitoringStatus, pStatus)
	{
		if (!pStatus->pMachineNameForwardedFrom)
		{

			switch (pStatus->eConnectionState)
			{
			xcase SIMPLEMONITORING_CONNECTED:
				if (pStatus->iLastActivityTime + 30 < iCurTime)
				{
					pStatus->eConnectionState = SIMPLEMONITORING_STALLED;
				}


			xcase SIMPLEMONITORING_DISCONNECTED:
				if (sbQuerySentryServerForProcessExistence && pStatus->status.pMyMachineName)
				{
					//we will learn from sentry server whether this exists or not
					break;
				}

				//fall through
			case SIMPLEMONITORING_GONE:
			case SIMPLEMONITORING_CRASHED:
				if (pStatus->iLastActivityTime + 30 < iCurTime)
				{
					stashRemovePointer(gSimpleMonitoringStatusByName, pStatus->pName, NULL);
					StructDestroy(parse_SimpleMonitoringStatus, pStatus);
				}
			}
		}
			

	}
	FOR_EACH_END;

	if (sMonitoringFlags & SSMFLAG_FORWARD_STATUSES_TO_OVERLORD)
	{
		FOR_EACH_IN_STASHTABLE(sOverlordsByName, OverlordList, pOverlord)
		{
			if (commConnectFSMForTickFunctionWithRetrying(&pOverlord->pFSM, &pOverlord->pLink, 
					"Link to overlord", 2.0f, commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,pOverlord->pName,OVERLORD_LISTEN_FOR_SIMPLE_STATUS_FORWARDING,
						0,0,0,0, NULL, NULL, NULL, NULL))
			{
				ServerListForOverlord sList = {0};
				Packet *pPack;

				sList.pMyName = getHostName();
				sList.ppServers = pOverlord->ppServers;

				pPack = pktCreate(pOverlord->pLink, TO_OVERLORD_HERE_ARE_FORWARDED_SERVER_STATUSES);
				ParserSendStructSafe(parse_ServerListForOverlord, pPack, &sList);
				pktSend(&pPack);
			}
		}
		FOR_EACH_END;
	}
}




void SimpleStatusMonitoring_HandleGenericStatus(Packet *pak, NetLink *link)
{
	StatusReporting_Wrapper *pWrapper = StructCreate(parse_StatusReporting_Wrapper);
	SimpleMonitoringStatus *pStatus;
	
	ParserRecvStructSafe(parse_StatusReporting_Wrapper, pak, pWrapper);

	if (stashFindPointer(gSimpleMonitoringStatusByName, pWrapper->pMyName, &pStatus))
	{
		pStatus->eConnectionState = SIMPLEMONITORING_CONNECTED;
		pStatus->iLastActivityTime = timeSecondsSince2000();
		pStatus->pLink = link;
		pStatus->iIP = linkGetIp(link);
		StructCopy(parse_StatusReporting_Wrapper, pWrapper, &pStatus->status, 0, 0, 0);
	}
	else
	{
		pStatus = StructCreate(parse_SimpleMonitoringStatus);
		pStatus->pName = strdup(pWrapper->pMyName);
		pStatus->eConnectionState = SIMPLEMONITORING_CONNECTED;
		pStatus->iLastActivityTime = timeSecondsSince2000();
		StructCopy(parse_StatusReporting_Wrapper, pWrapper, &pStatus->status, 0, 0, 0);

		stashAddPointer(gSimpleMonitoringStatusByName, pStatus->pName, pStatus, false);
		linkSetUserData(link, pStatus);

		if (pWrapper->pMyOverlord && (sMonitoringFlags & SSMFLAG_FORWARD_STATUSES_TO_OVERLORD))
		{
			AddOverlord(pWrapper->pMyOverlord, pStatus);
			pStatus->pMyOverlord = pWrapper->pMyOverlord;
		}

	}

	StructDestroy(parse_StatusReporting_Wrapper, pWrapper);
}

static void HandleCrashReport(Packet *pPack)
{
	char *pName = pktGetStringTemp(pPack);
	SimpleMonitoringStatus *pStatus;

	if (stashFindPointer(gSimpleMonitoringStatusByName, pName, &pStatus))
	{
		pStatus->eConnectionState = SIMPLEMONITORING_CRASHED;
		pStatus->iLastActivityTime = timeSecondsSince2000();
	}
}

static void AddLogToStatus(SimpleMonitoringStatus *pStatus, char *pLog)
{
	SimpleMonitoringStatus_Log *pNewLog = StructCreate(parse_SimpleMonitoringStatus_Log);
	pNewLog->iTime = timeSecondsSince2000();
	pNewLog->pStr = strdup(pLog);

	eaInsert(&pStatus->ppLogs, pNewLog, 0);

	if (eaSize(&pStatus->ppLogs) > MAX_LOGS_PER_SYSTEM)
	{
		StructDestroy(parse_SimpleMonitoringStatus_Log, eaPop(&pStatus->ppLogs));
	}
}

static void SimpleStatusMonitoring_HandleLog(Packet *pak)
{
	StatusReporting_LogWrapper *pLog = StructCreate(parse_StatusReporting_LogWrapper);
	SimpleMonitoringStatus *pStatus;
	
	ParserRecvStructSafe(parse_StatusReporting_LogWrapper, pak, pLog);

	if (stashFindPointer(gSimpleMonitoringStatusByName, pLog->pMyName, &pStatus))
	{
		AddLogToStatus(pStatus, pLog->pLog);
	}

	StructDestroy(parse_StatusReporting_LogWrapper, pLog);
}

static void SimpleStatusMonitoringHandleMsg(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	switch(cmd)
	{
	case FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_GENERIC_STATUS:
		SimpleStatusMonitoring_HandleGenericStatus(pak, link);
		break;

	case FROM_ERRORTRACKER_TO_CONTROLLERTRACKER_CRASH_REPORT:
		HandleCrashReport(pak);
		break;

	case FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_LOG:
		SimpleStatusMonitoring_HandleLog(pak);
		break;

	case FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_COMMAND_RESULT:
		SimpleStatusMonitoring_HandleCommandResult(pak);
		break;

	default:
		break;
	}
}

static void SimpleStatusMonitoringDisconnectCB(NetLink *link,void *pUserData)
{
	SimpleMonitoringStatus *pStatus = pUserData;
	if (pStatus)
	{
		if (pStatus->eConnectionState == SIMPLEMONITORING_CONNECTED)
		{
			pStatus->iLastActivityTime = timeSecondsSince2000();
			pStatus->eConnectionState = SIMPLEMONITORING_DISCONNECTED;
			pStatus->pLink = NULL;
		}
	}
}

//For each machine that is forwardign us statuses, we remember all the servers on that machine by name, so that
//every time we get a new packet, we can clear out all their statuses, and every time we get a disconnected
//from a forwarder, we can set all their statuses properly
AUTO_STRUCT;
typedef struct ForwardedServerListFromOneMachine
{
	const char *pMachineName; AST(POOL_STRING)
	char **ppServerNames;
} ForwardedServerListFromOneMachine;

StashTable sForwardedServerListsByMachineName = NULL;

static void HandleForwardedServerList(Packet *pak, NetLink *link, void *pUserData)
{
	const char *pMachineName = (char*)pUserData;
	ServerListForOverlord *pReceivedList = StructCreate(parse_ServerListForOverlord);
	ForwardedServerListFromOneMachine *pListLastTime = NULL;

	ParserRecvStructSafe(parse_ServerListForOverlord, pak, pReceivedList);

	if (!pMachineName)
	{
		pMachineName = allocAddString(pReceivedList->pMyName);
		linkSetUserData(link, (char*)pMachineName);
	}

	//we now have a list of the names we got last time, plus the list we got this time... so anything no longer
	//on the list must be actually gone, so we destroy it
	if (stashFindPointer(sForwardedServerListsByMachineName, pMachineName, &pListLastTime))
	{
		FOR_EACH_IN_EARRAY(pListLastTime->ppServerNames, char, pName)
		{
			SimpleMonitoringStatus *pStatus;
			
			if (stashRemovePointer(gSimpleMonitoringStatusByName, pName, &pStatus))
			{
				StructDestroy(parse_SimpleMonitoringStatus, pStatus);
			}
		}
		FOR_EACH_END;
	} 
	

	FOR_EACH_IN_EARRAY(pReceivedList->ppServers, SimpleMonitoringStatus, pStatus)
	{
		SimpleMonitoringStatus *pPreExistingStatus;
		if (stashFindPointer(gSimpleMonitoringStatusByName, pStatus->pName, &pPreExistingStatus))
		{
			StructCopy(parse_SimpleMonitoringStatus, pStatus, pPreExistingStatus, 0, 0, 0);
			pPreExistingStatus->pMachineNameForwardedFrom = pMachineName;
		}
		else
		{
			pPreExistingStatus = StructCreate(parse_SimpleMonitoringStatus);
			StructCopy(parse_SimpleMonitoringStatus, pStatus, pPreExistingStatus, 0, 0, 0);
			stashAddPointer(gSimpleMonitoringStatusByName, pPreExistingStatus->pName, pPreExistingStatus, false);
			pPreExistingStatus->pMachineNameForwardedFrom = pMachineName;
		}
	}
	FOR_EACH_END;

	if (!pListLastTime)
	{
		pListLastTime = StructCreate(parse_ForwardedServerListFromOneMachine);
		pListLastTime->pMachineName = pMachineName;
		if (!sForwardedServerListsByMachineName)
		{
			sForwardedServerListsByMachineName = stashTableCreateAddress(16);
		}

		stashAddPointer(sForwardedServerListsByMachineName, pMachineName, pListLastTime, true);
	}

	eaDestroyEx(&pListLastTime->ppServerNames, NULL);
	FOR_EACH_IN_EARRAY(pReceivedList->ppServers, SimpleMonitoringStatus, pStatus)
	{
		eaPush(&pListLastTime->ppServerNames, strdup(pStatus->pName));
	}
	FOR_EACH_END;

	StructDestroy(parse_ServerListForOverlord, pReceivedList);
}

static void SimpleStatusMonitoringHandleForwardingMsg(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	switch(cmd)
	{
	xcase TO_OVERLORD_HERE_ARE_FORWARDED_SERVER_STATUSES:
		HandleForwardedServerList(pak, link, pUserData);


	default:
		break;
	}
}

static void SimpleStatusMonitoringHandleForwardingDisconnect(NetLink *link,void *pUserData)
{
	const char *pMachineName = (char*)pUserData;
	ForwardedServerListFromOneMachine *pListLastTime = NULL;

	if (pMachineName && stashFindPointer(sForwardedServerListsByMachineName, pMachineName, &pListLastTime))
	{
		FOR_EACH_IN_EARRAY(pListLastTime->ppServerNames, char, pName)
		{
			SimpleMonitoringStatus *pStatus;
			
			if (stashFindPointer(gSimpleMonitoringStatusByName, pName, &pStatus))
			{
				pStatus->eConnectionState = SIMPLEMONITORING_FORWARDING_LOST;
				pStatus->iLastActivityTime = timeSecondsSince2000();
			}
		}
		FOR_EACH_END;
	}
}

AUTO_COMMAND;
void SimpleStatusMonitoring_Begin(int iPort, SimpleStatusMonitoringFlags eFlags)
{
	sMonitoringFlags = eFlags;

	if (!iPort)
	{
		iPort = CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT;
	}
	gSimpleMonitoringStatusByName = stashTableCreateWithStringKeys(5, StashDefault);
	resRegisterDictionaryForStashTable("SimpleMonitoringStatuses", RESCATEGORY_OTHER, 0, gSimpleMonitoringStatusByName, parse_SimpleMonitoringStatus);

	FOR_EACH_IN_EARRAY(sppMonitoreesFromCommandLine, SimpleMonitoringStatus, pStatus)
	{
		stashAddPointer(gSimpleMonitoringStatusByName, pStatus->pName, pStatus, true);
	}
	FOR_EACH_END;

	if (!commListen(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,iPort,
			SimpleStatusMonitoringHandleMsg,NULL,
			SimpleStatusMonitoringDisconnectCB,
			0))
	{
		assertmsgf(0, "Unable to do simpleStatusMonitoring on port %u... is a controllerTracker running on this machine?",
			CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT);
	}

	TimedCallback_Add(SimpleStatusMonitoring_TimedCB, NULL, 1.0f);

	if (eFlags & SSMFLAG_LISTEN_FOR_FORWARDED_STATUSES_AS_OVERLORD)
	{
		if (!commListen(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, OVERLORD_LISTEN_FOR_SIMPLE_STATUS_FORWARDING,
			SimpleStatusMonitoringHandleForwardingMsg, NULL,
			SimpleStatusMonitoringHandleForwardingDisconnect, 0))
		{
			assertmsgf(0, "Unable to do forward-to-overlord monitoring on port %u..",
				OVERLORD_LISTEN_FOR_SIMPLE_STATUS_FORWARDING);
		}
	}
}

SimpleMonitoringStatus *SimpleStatusMonitoring_FindStatusByName(char *pName)
{
	SimpleMonitoringStatus *pRetVal = NULL;
	stashFindPointer(gSimpleMonitoringStatusByName, pName, &pRetVal);
	return pRetVal;
}

SimpleMonitoringStatus *SimpleStatusMonitoring_FindConnectedStatusByName(char *pName)
{
	SimpleMonitoringStatus *pRetVal = NULL;
	stashFindPointer(gSimpleMonitoringStatusByName, pName, &pRetVal);
	if (pRetVal && pRetVal->eConnectionState == SIMPLEMONITORING_CONNECTED)
	{
		return pRetVal;
	}

	return NULL;
}

AUTO_COMMAND;
char *SimpleStatusMonitoring_TellSystemToShutDown(char *pSystemName)
{
	SimpleMonitoringStatus *pStatus;
	Packet *pPkt;

	if (!stashFindPointer(gSimpleMonitoringStatusByName, pSystemName, &pStatus))
	{
		return "Unkown system";
	}

	if (!pStatus->pLink)
	{
		return "System disconnected";
	}

	pPkt = pktCreate(pStatus->pLink, FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_SAFE_SHUTDOWN);
	pktSend(&pPkt);

	return "Shutdown packet sent";
}

AUTO_COMMAND;
char *SimpleStatusMonitoring_RestartSystem(char *pName)
{
	SimpleMonitoringStatus *pStatus;
	if (!stashFindPointer(gSimpleMonitoringStatusByName, pName, &pStatus))
	{
		return "Unkown system";
	}

	if (pStatus->eConnectionState == SIMPLEMONITORING_CONNECTED)
	{
		return "Can't restart... it's already running";
	}

	if (!estrLength(&pStatus->status.status.pRestartingCommand))
	{
		return "No restarting command... don't know how";
	}

	if (pStatus->status.pMyMachineName)
	{
		SentryServerComm_RunCommand_1Machine(pStatus->status.pMyMachineName, pStatus->status.status.pRestartingCommand);


	}
	else if (pStatus->iIP)
	{		
		SentryServerComm_RunCommand_1Machine(makeIpStr(pStatus->iIP), pStatus->status.status.pRestartingCommand);
	}
	else if (pStatus->pAssumedMachineName)
	{
		SentryServerComm_RunCommand_1Machine(pStatus->pAssumedMachineName, pStatus->status.status.pRestartingCommand);
	
	}
	else
	{
		return "No IP or machine name for that system";
	}

	return "Restarting command sent";
}

//won't do anything after simpleStatusReporting is already going
AUTO_COMMAND ACMD_COMMANDLINE;
void AddSimpleMonitoree(char *pName, char *pMachineName, char *pRestartingCommand)
{
	SimpleMonitoringStatus *pStatus = StructCreate(parse_SimpleMonitoringStatus);
	pStatus->pName = strdup(pName);
	pStatus->pAssumedMachineName = strdup(pMachineName);
	pStatus->status.status.pRestartingCommand = strdup(pRestartingCommand);

	eaPush(&sppMonitoreesFromCommandLine, pStatus);
}

AUTO_STRUCT;
typedef struct SimpleStatusMonitoringCommand
{
	int iCommandID; 
	CmdSlowReturnForServerMonitorInfo slowReturnInfo; NO_AST
} SimpleStatusMonitoringCommand;

StashTable sSimpleStatusMonitoringCommandsByID = NULL;



//note that the access level is checked on the other side as well, so someone with AL 1 can access this command
//and try to send an AL 4 command, but it will fail over there
AUTO_COMMAND ACMD_ACCESSLEVEL(1);
char *SendCommandToSimpleStatusMonitoree(CmdContext *pContext, char *pSystemName, ACMD_SENTENCE pCommandString)
{
	SimpleMonitoringStatus *pStatus;
	Packet *pPkt;
	static int iNextCmdID = 1;
	SimpleStatusMonitoringCommand *pCommand;

	if (!stashFindPointer(gSimpleMonitoringStatusByName, pSystemName, &pStatus) || !pStatus->pLink)
	{
		return "FAIL: Unknown or disconnected system";
	}

	pPkt = pktCreate(pStatus->pLink, FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_COMMAND);

	pktSendBits(pPkt, 32, iNextCmdID);
	pktSendBits(pPkt, 32, pContext->access_level);
	pktSendString(pPkt, pContext->pAuthNameAndIP ? pContext->pAuthNameAndIP : "");
	pktSendString(pPkt, pCommandString);
	pktSend(&pPkt);

	if (!sSimpleStatusMonitoringCommandsByID)
	{
		sSimpleStatusMonitoringCommandsByID = stashTableCreateInt(16);
	}

	pCommand = StructCreate(parse_SimpleStatusMonitoringCommand);
	pCommand->iCommandID = iNextCmdID;
	memcpy(&pCommand->slowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));
	pContext->slowReturnInfo.bDoingSlowReturn = true;

	stashIntAddPointer(sSimpleStatusMonitoringCommandsByID, pCommand->iCommandID, pCommand, true);

	do
	{
		iNextCmdID++;
	}
	while (iNextCmdID == 0);

	return "You should never see this";

}

static void SimpleStatusMonitoring_HandleCommandResult(Packet *pak)
{
	int iCmdId = pktGetBits(pak, 32);
	bool bSucceeded = pktGetBits(pak, 1);
	char *pResultString = bSucceeded ? pktGetStringTemp(pak) : "";
	SimpleStatusMonitoringCommand *pCommand;

	if (stashIntFindPointer(sSimpleStatusMonitoringCommandsByID, iCmdId, &pCommand))
	{
		DoSlowCmdReturn(bSucceeded, pResultString, &pCommand->slowReturnInfo);
		StructDestroy(parse_SimpleStatusMonitoringCommand, pCommand);
	}
}


const char *SimpleMonitoringStatus_GetMachineName(SimpleMonitoringStatus *pStatus)
{
	if (pStatus->status.pMyMachineName)
	{
		return pStatus->status.pMyMachineName;
	}
	else if (pStatus->iIP)
	{	
		return makeIpStr(pStatus->iIP);
	}
	else if (pStatus->pAssumedMachineName)
	{
		return pStatus->pAssumedMachineName;	
	}

	return NULL;
}

#include "SimpleStatusMonitoring_h_ast.c"
#include "SimpleStatusMonitoring_c_ast.c"
