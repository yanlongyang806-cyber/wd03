/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "aslJobManager.h"
#include "objSchema.h"
#include "error.h"
#include "serverlib.h"
#include "autostartupsupport.h"
#include "resourceManager.h"
#include "objTransactions.h"
#include "winInclude.h"
#include "globalTypes.h"

#include "aslJobManagerPub.h"
#include "aslJobManagerPub_h_ast.h"
#include "RemoteCommandGroup.h"
#include "aslJobManager_h_ast.h"
#include "logging.h"
#include "stringUtil.h"
#include "RemoteCommandGroup_h_ast.h"
#include "../../core/controller/pub/controllerPub.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "TimedCallback.h"
#include "stringCache.h"
#include "sock.h"
#include "alerts.h"
#include "ContinuousBuilderSupport.h"
#include "NameValuePair.h"
#include "EventCountingHeatMap.h"
#include "aslPatchCompletionQuerying.h"
#include "ugcProjectUtils.h"
#include "Autogen/appServerLib_autogen_SlowFuncs.h"
#include "file.h"
#include "NameValuePair.h"
#include "aslJobManagerJobQueues.h"

StashTable sActiveAndQueuedSummariesByName = NULL;
StashTable sCompletedSummariesByName = NULL;
StashTable sGroupQueuesByName = NULL;

char gJobManagerConfigFileName[CRYPTIC_MAX_PATH] = "server/JobManagerConfig.txt";


JobManagerConfig *gpJobManagerConfig = NULL;

static bool sbDoPatchCompletionQuerying = false;
AUTO_CMD_INT(sbDoPatchCompletionQuerying, DoPatchCompletionQuerying);

int giMaxInGroupQueue = 100;
AUTO_CMD_INT(giMaxInGroupQueue, MaxInGroupQueue);

static bool sbJobManagerIsPaused = false;

bool JobManagerIsPaused(void)
{
	return sbJobManagerIsPaused;
}

AUTO_COMMAND;
void PauseJobManager(int iPause)
{
	sbJobManagerIsPaused = !!iPause;
}

void CheckQueues(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);


void ReplaceRCGMacros(JobSummary *pSummary, RemoteCommandGroup *pGroup)
{
	char minutesString[32];
	char queuedMinutesString[32] = "0";
	int i;

	sprintf(minutesString, "%d", (pSummary->iCompletionTime - pSummary->iStartTime + 59)/60);
	if (pSummary->iQueuedTime)
	{
		sprintf(queuedMinutesString, "%d", (pSummary->iStartTime - pSummary->iQueuedTime + 59)/60);
	}

	for (i=0; i < eaSize(&pGroup->ppCommands); i++)
	{
		estrReplaceOccurrences(&pGroup->ppCommands[i]->pCommandString, "__JOB_DURATION_MINS__", minutesString);
		estrReplaceOccurrences(&pGroup->ppCommands[i]->pCommandString, "__JOB_QUEUED_MINS__", queuedMinutesString);
	}
}


JobSummary *GetSummaryByName(char *pName)
{
	JobSummary *pRetVal = NULL;
	stashFindPointer(sActiveAndQueuedSummariesByName, pName, &pRetVal);
	return pRetVal;
}

JobSummary *GetCompletedSummaryByName(char *pName)
{
	JobSummary *pRetVal = NULL;
	stashFindPointer(sCompletedSummariesByName, pName, &pRetVal);
	return pRetVal;
}

void AddSummaryToMainStashTable(JobSummary *pSummary)
{
	if (!sActiveAndQueuedSummariesByName)
	{
		sActiveAndQueuedSummariesByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("Jobs", RESCATEGORY_SYSTEM, 0, sActiveAndQueuedSummariesByName, parse_JobSummary);
	}

	stashAddPointer(sActiveAndQueuedSummariesByName, pSummary->pName, pSummary, false);
}




void JobLog(JobManagerJobGroupState *pGroupState, FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;
	U32 iCurTime = timeSecondsSince2000();


	estrGetVarArgs(&pFullString, pFmt);

	if (g_isContinuousBuilder)
	{
		SendStringToCB(CBSTRING_COMMENT, "Job: %s", pFullString);
	}


	if (iCurTime > pGroupState->pSummary->iStartTime)
	{
		char *pDurationString = NULL;
		timeSecondsDurationToPrettyEString(iCurTime - pGroupState->pSummary->iStartTime, &pDurationString);
		estrConcatf(&pGroupState->pSummary->pLogString, "%s: ", pDurationString);
		estrDestroy(&pDurationString);
	}

	estrConcatf(&pGroupState->pSummary->pLogString, "%s\n", pFullString);
	estrDestroy(&pFullString);

	if (pGroupState->pDef->eServerTypeForLogUpdates)
	{
		RemoteCommand_UpdateJobLogging(pGroupState->pDef->eServerTypeForLogUpdates,
			pGroupState->pDef->iServerIDForLogUpdates,
			pGroupState->pDef->iUserDataForLogUpdates,
			pGroupState->pDef->pNameForLogUpdates ? pGroupState->pDef->pNameForLogUpdates : "",
			pGroupState->pSummary->pLogString);
	}


}


void CheckJobGroupsForTimeout(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	StashTableIterator stashIterator;
	StashElement stashElement;
	U32 iCurTime = timeSecondsSince2000();

	if (!sActiveAndQueuedSummariesByName)
	{
		return;
	}

	stashGetIterator(sActiveAndQueuedSummariesByName, &stashIterator);

	while (stashGetNextElement(&stashIterator, &stashElement))
	{
		JobSummary *pSummary = stashElementGetPointer(stashElement);
		if (!pSummary->pQueue)
		{
			int i;
			
			JobManagerJobGroupState *pGroup = pSummary->pGroupState;

			for (i=0; i < eaSize(&pGroup->ppJobStates); i++)
			{
				if (pGroup->ppJobStates[i]->eResult == JMR_ONGOING && pGroup->ppJobStates[i]->pDef->iTimeout)
				{

					if (pGroup->ppJobStates[i]->iStartTime + pGroup->ppJobStates[i]->pDef->iTimeout < iCurTime)
					{
						if (pGroup->ppJobStates[i]->pDef->eType == JOB_WAIT)
						{
							SucceedJob(pGroup, pGroup->ppJobStates[i], "Waited for %d seconds, done waiting", pGroup->ppJobStates[i]->pDef->iTimeout);
							break;
						}
						else
						{
							FailJob(pGroup, pGroup->ppJobStates[i], true, "Job was running for %d seconds... timed out", pGroup->ppJobStates[i]->pDef->iTimeout);
							break;
						}
					}
				}
			}
		}
	}
}



int JobManagerLibOncePerFrame(F32 fElapsed)
{
	
	aslPatchCompletionQuerying_Update();
	return 1;
}



void aslJobManagerSendGlobalInfo(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	JobManagerGlobalInfo jobManagerInfo = {0};
	JobManagerGroupQueue *pQueue = FindGroupQueue(allocAddString("UGCPublish"));
	

	if (pQueue)
	{
		jobManagerInfo.iActiveAndQueuedPublishes = eaSize(&pQueue->ppQueue) + pQueue->iNumActive;
		
		jobManagerInfo.iNumSuccededPublishes = EventCounter_GetTotalTotal(pQueue->pSucceededCounter);

		if (jobManagerInfo.iNumSuccededPublishes)
		{
			jobManagerInfo.iAveragePublishTime = pQueue->iTotalSucceededTime / jobManagerInfo.iNumSuccededPublishes;
			jobManagerInfo.iAverageQueuedTime = pQueue->iTotalQueuedTime / jobManagerInfo.iNumSuccededPublishes;
		}

		jobManagerInfo.iNumFailedPublishes = EventCounter_GetTotalTotal(pQueue->pFailedCounter);


		jobManagerInfo.iNumSuccededPublishesLastHour = EventCounter_GetCount(pQueue->pSucceededCounter, EVENTCOUNT_LASTFULLHOUR, timeSecondsSince2000());
		jobManagerInfo.iNumFailedPublishesLastHour = EventCounter_GetCount(pQueue->pFailedCounter, EVENTCOUNT_LASTFULLHOUR, timeSecondsSince2000());
	}

	pQueue = FindGroupQueue(allocAddString("UGCRePublish"));
	
	if (pQueue)
	{
		jobManagerInfo.iActiveAndQueuedRepublishes = eaSize(&pQueue->ppQueue) + pQueue->iNumActive;
	}

	RemoteCommand_HereIsJobManagerGlobalInfo(GLOBALTYPE_CONTROLLER, 0, objServerType(), objServerID(), &jobManagerInfo);
}


static void loadJobManagerConfig(void)
{
	gpJobManagerConfig = StructCreate(parse_JobManagerConfig);
	if (!fileExists(gJobManagerConfigFileName))
	{
		return;
	}

	if (!ParserReadTextFile(gJobManagerConfigFileName, parse_JobManagerConfig, gpJobManagerConfig, PARSER_OPTIONALFLAG))
	{
		char *pTempString = NULL;
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserReadTextFile(gJobManagerConfigFileName, parse_JobManagerConfig, gpJobManagerConfig, PARSER_OPTIONALFLAG);
		ErrorfPopCallback();

		assertmsgf(0, "Error while reading Job Manager config file: %s\n", pTempString);
	}
}

void WriteOutJobManagerConfig(void)
{
	ParserWriteTextFile(gJobManagerConfigFileName, parse_JobManagerConfig, gpJobManagerConfig, 0, 0);
}


int JobManagerLibInit(void)
{

	loadJobManagerConfig();

	objLoadAllGenericSchemas();
		
	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_JOBMANAGER, "map manager type not set");

	loadstart_printf("Connecting JobManager to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");


	if (sbDoPatchCompletionQuerying && isProductionMode())
	{
		aslPatchCompletionQuerying_Begin(UGC_GetShardSpecificNSPrefix(NULL), 10);
	}

	gAppServer->oncePerFrame = JobManagerLibOncePerFrame;

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");

	TimedCallback_Add(CheckJobGroupsForTimeout, NULL, 10.0f);
	TimedCallback_Add(CheckQueues, NULL, 1.0f);
	TimedCallback_Add(aslJobManagerSendGlobalInfo, NULL, 5.0f);

	InitJobQueues();

	return 1;
}


AUTO_RUN;
int JobManagerRegister(void)
{
	aslRegisterApp(GLOBALTYPE_JOBMANAGER,JobManagerLibInit, 0);
	return 1;
}

bool strHasLeadingOrTrailingWhiteSpace(char *pStr)
{
	char *pTemp = NULL;
	int iTrimmed;
	estrStackCreate(&pTemp);
	estrCopy2(&pTemp, pStr);
	iTrimmed = estrTrimLeadingAndTrailingWhitespace(&pTemp);
	estrDestroy(&pTemp);

	return iTrimmed != 0;
}

bool StringIsLegalForGroupName(char *pName)
{
	if (!pName || !pName[0])
	{
		return false;
	}

	if (strchr(pName, '|') || strchr(pName, '"'))
	{
		return false;
	}

	if (strHasLeadingOrTrailingWhiteSpace(pName))
	{
		return false;
	}

	return true;
}

bool StringIsLegalForJobName(char *pName)
{
	if (!pName || !pName[0])
	{
		return false;
	}

	if (strchr(pName, '|') || strchr(pName, '"'))
	{
		return false;
	}

	if (strHasLeadingOrTrailingWhiteSpace(pName))
	{
		return false;
	}

	return true;
}


bool ValidateJobDef(JobManagerJobDef *pJobDef)
{
	if (!StringIsLegalForJobName(pJobDef->pJobName))
	{
		log_printf(LOG_ERRORS, "Rejecting Job %s because its name is illegal", pJobDef->pJobName);
		return false;
	}

	switch (pJobDef->eType)
	{
	xcase JOB_WAIT:
		if (!pJobDef->iTimeout)
		{
			log_printf(LOG_ERRORS, "Rejecting job %s because it has no timeout for a WAIT job", pJobDef->pJobName);
			return false;
		}
	xcase JOB_SERVER_W_CMD_LINE:
		if (!pJobDef->pServerWCmdLineDef)
		{
			log_printf(LOG_ERRORS, "Rejecting Job %s because it has no pServerWCmdLineDef", pJobDef->pJobName);
			return false;
		}
		if (!pJobDef->pServerWCmdLineDef->eServerTypeToLaunch || !pJobDef->pServerWCmdLineDef->pExtraCmdLine)
		{
			log_printf(LOG_ERRORS, "Rejecting Job %s beacuse it has no valid server type or command line", pJobDef->pJobName);
			return false;
		}
	xcase JOB_REMOTE_CMD:
		if (!pJobDef->pRemoteCmdDef)
		{
			log_printf(LOG_ERRORS, "Rejecting Job %s because it has no pRemoteCmdDef", pJobDef->pJobName);
			return false;
		}
		if (!pJobDef->pRemoteCmdDef->eTypeForCommand || !pJobDef->pRemoteCmdDef->pCommandString)
		{
			log_printf(LOG_ERRORS, "Rejecting Job %s because it has no server type to send the commmand to, or no command string", pJobDef->pJobName);
			return false;
		}
		if (!strstri(pJobDef->pRemoteCmdDef->pCommandString, "$JOBNAME$"))
		{
			log_printf(LOG_ERRORS, "Rejecting Job %s because its command string does not include $JOBNAME$", pJobDef->pJobName);
			return false;
		}

		if (pJobDef->pRemoteCmdDef->pCancelCommandString && !strstri(pJobDef->pRemoteCmdDef->pCancelCommandString, "$JOBNAME$"))
		{
			log_printf(LOG_ERRORS, "Rejecting Job %s because its cancel command does not include $JOBNAME$", pJobDef->pJobName);
			return false;
		}



	}

	return true;
}

bool ValidateGroupDefRequest(JobManagerJobGroupDef *pDef)
{
	int i, j, k;
	bool bFoundOneWithNoWaiting = false;

	if (!eaSize(&pDef->ppJobs))
	{
		log_printf(LOG_ERRORS, "Rejecting def %s because it has no Jobs", pDef->pJobGroupName);
		return false;
	}

	if (!StringIsLegalForGroupName(pDef->pJobGroupName))
	{
		log_printf(LOG_ERRORS, "Rejecting def %s because its name is not legal", pDef->pJobGroupName);
		return false;
	}

	if (GetSummaryByName(pDef->pJobGroupName) || GetCompletedSummaryByName(pDef->pJobGroupName))
	{
		log_printf(LOG_ERRORS, "Rejecting def %s because its name is already in use", pDef->pJobGroupName);
		return false;
	}

	for (i=0; i < eaSize(&pDef->ppJobs); i++)
	{

		if (!ValidateJobDef(pDef->ppJobs[i]))
		{
			return false;
		}

		if (eaSize(&pDef->ppJobs[i]->ppJobsIDependOn))
		{
			for (j=0 ; j < eaSize(&pDef->ppJobs[i]->ppJobsIDependOn); j++)
			{
				bool bFound = false;
				for (k = 0 ; k < eaSize(&pDef->ppJobs); k++)
				{
					if (k != i)
					{
						if (stricmp(pDef->ppJobs[i]->ppJobsIDependOn[j], pDef->ppJobs[k]->pJobName) == 0)
						{
							bFound = true;
							break;
						}
					}
				}

				if (!bFound)
				{
					log_printf(LOG_ERRORS, "Rejecting def %s because job %s has a dependency on %s which doesn't exist",
						pDef->pJobGroupName, pDef->ppJobs[i]->pJobName, pDef->ppJobs[i]->ppJobsIDependOn[j]);
					return false;
				}
			}
		}
		else
		{
			bFoundOneWithNoWaiting = true;
		}

	


		for (j = i+1; j < eaSize(&pDef->ppJobs); j++)
		{
			if (stricmp_safe(pDef->ppJobs[i]->pJobName, pDef->ppJobs[j]->pJobName) == 0)
			{
				log_printf(LOG_ERRORS, "Rejecting def %s because it has two Jobs named %s", pDef->pJobGroupName, pDef->ppJobs[i]->pJobName);
				return false;
			}
		}
	}

	if (!bFoundOneWithNoWaiting)
	{
		log_printf(LOG_ERRORS, "Rejecting def %s because it every job in it has dependencies", pDef->pJobGroupName);
		return false;
	}


	return true;
}



static void LaunchServerForJob_CB(TransactionReturnVal *returnVal, char *pJobName)
{
	Controller_SingleServerInfo *pSingleServerInfo = NULL;
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	enumTransactionOutcome eOutcome;

	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		eOutcome = RemoteCommandCheck_StartServer(returnVal, &pSingleServerInfo);

		if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && pSingleServerInfo->iIP != 0)
		{
			pJob->pServerWCmdLineState->iServerID = pSingleServerInfo->iGlobalID;
			estrCopy2(&pJob->pStateString, "Server successfully started");
			JobLog(pGroup, "Server successfully launched. Container ID: %u. IP: %s",
				pSingleServerInfo->iGlobalID, makeIpStr(pSingleServerInfo->iIP));
		}
		else
		{
			FailJob(pGroup, pJob, false, "Could not launch server");
		}
	}


	if (pSingleServerInfo)
	{
		StructDestroy(parse_Controller_SingleServerInfo, pSingleServerInfo);
	}

	free(pJobName);
}


static void JobRemoteCommandCB(TransactionReturnVal *returnVal, char *pJobName)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;

	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		if (pJob->eResult == JMR_ONGOING)
		{
			if (returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE || !returnVal->pBaseReturnVals || atoi(returnVal->pBaseReturnVals[0].returnString) != 1)
			{
				FailJob(pGroup, pJob, false, "Remote command failed");				
			}
			else
			{
				pJob->pRemoteCmdState->bCommandReturned = true;
				estrCopy2(&pJob->pStateString, "remote command returned successfully");
				SucceedJob(pGroup, pJob, "%s", "Remote command returned successfully");
			}
		}
	}
	
	free(pJobName);
}

void JobRemoteCommandTimeoutCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pJobName)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;

	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		if (pJob->eResult == JMR_ONGOING && !pJob->pRemoteCmdState->bCommandReturned)
		{
			FailJob(pGroup, pJob, false, "Remote command didn't return after %d seconds", pJob->pDef->pRemoteCmdDef->iInitialCommandTimeout);
		}
	}

	free(pJobName);
}


void ActuallyStartJob(JobManagerJobGroupState *pGroupState, JobManagerJobState *pJobState)
{
	pJobState->iStartTime = timeSecondsSince2000();
	pJobState->eResult = JMR_ONGOING;

	switch(pJobState->pDef->eType)
	{
	xcase JOB_WAIT:
		JobLog(pGroupState, "Starting job %s. Waiting for %d seconds",
			pJobState->pDef->pJobName, pJobState->pDef->iTimeout);


	xcase JOB_SERVER_W_CMD_LINE:
		{
			RemoteCommandGroup *pServerCrashGroup = CreateEmptyRemoteCommandGroup();
			RemoteCommandGroup *pServerCloseGroup = CreateEmptyRemoteCommandGroup();

			//this will be freed when the remote command returns
			char *pJobFullName = strdup(GetFullJobName(pGroupState, pJobState));
		
			char *pFullCommandLine = NULL;

			char *pOwnerString = NULL;
			char *pSuperEscOwnerString = NULL;

			ParserWriteText(&pOwnerString, parse_JobGroupOwner, &pGroupState->pDef->owner, 0, 0, 0);
			estrSuperEscapeString(&pSuperEscOwnerString, pOwnerString);

			estrPrintf(&pFullCommandLine, "%s - -NameForJobManager \"%s\" -SetJobOwner %s",
				pJobState->pDef->pServerWCmdLineDef->pExtraCmdLine ? pJobState->pDef->pServerWCmdLineDef->pExtraCmdLine : "",
				pJobFullName, pSuperEscOwnerString);


			AddCommandToRemoteCommandGroup(pServerCrashGroup, GLOBALTYPE_JOBMANAGER, 0, false,
				"ServerForStepCrashed \"%s\"", pJobFullName);

			AddCommandToRemoteCommandGroup(pServerCloseGroup, GLOBALTYPE_JOBMANAGER, 0, false,
				"ServerForStepClosed \"%s\"", pJobFullName);

			pJobState->pServerWCmdLineState = StructCreate(parse_JobManagerServerWCmdLineState);

			pJobState->pServerWCmdLineState->iCrashRemoteCommandGroupID = pServerCrashGroup->iUidFromSubmitter;
			pJobState->pServerWCmdLineState->iCloseRemoteCommandGroupID = pServerCloseGroup->iUidFromSubmitter;
			
/*
void RemoteCommand_StartServer( TransactionReturnVal *pReturnValStruct, GlobalType gServerType, 
 ContainerID gServerID, GlobalType eGlobalType, U32 iContainerID, const char* pCategory, 
 const char* commandLine, const char* pReason, const AdditionalServerLaunchInfo* pAdditionalInfo, 
 const ServerLaunchDebugNotificationInfo* pDebugInfo, const RemoteCommandGroup* pWhatToDoOnCrash, 
 const RemoteCommandGroup* pWhatToDoOnAnyClose)
*/
			log_printf(LOG_JOBS, "Jobgroup %s job %s requesting launch of %s with full command line %s\n", 
				pGroupState->pDef->pJobGroupName, pJobState->pDef->pJobName, GlobalTypeToName(pJobState->pDef->pServerWCmdLineDef->eServerTypeToLaunch),
				pFullCommandLine);

			RemoteCommand_StartServer(
				objCreateManagedReturnVal(LaunchServerForJob_CB, pJobFullName),
				GLOBALTYPE_CONTROLLER, 0, pJobState->pDef->pServerWCmdLineDef->eServerTypeToLaunch, 0, NULL,
				pFullCommandLine, STACK_SPRINTF("Requested for JobGroup %s Job %s", pGroupState->pDef->pJobGroupName, pJobState->pDef->pJobName),
				NULL, NULL, pServerCrashGroup, pServerCloseGroup);

			JobLog(pGroupState, "Starting job %s. Requested launch of server of type %s",
				pJobState->pDef->pJobName, GlobalTypeToName(pJobState->pDef->pServerWCmdLineDef->eServerTypeToLaunch));


			estrDestroy(&pFullCommandLine);
			DestroyRemoteCommandGroup(pServerCrashGroup);
			DestroyRemoteCommandGroup(pServerCloseGroup);

			estrCopy2(&pJobState->pStateString, "Requested server creation");


		}
	xcase JOB_REMOTE_CMD:
		{
			RemoteCommandGroup *pServerCrashGroup = CreateEmptyRemoteCommandGroup();
			RemoteCommandGroup *pServerCloseGroup = CreateEmptyRemoteCommandGroup();
			char *pCommandString = NULL;
			char *pCommandDebugNameString = NULL;
			BaseTransaction baseTransaction;
			BaseTransaction **ppBaseTransactions = NULL;
			//this will be freed when the remote command returns
			char *pJobFullName = strdup(GetFullJobName(pGroupState, pJobState));

			AddCommandToRemoteCommandGroup(pServerCrashGroup, GLOBALTYPE_JOBMANAGER, 0, false,
				"ServerForStepCrashed \"%s\"", pJobFullName);

			AddCommandToRemoteCommandGroup(pServerCloseGroup, GLOBALTYPE_JOBMANAGER, 0, false,
				"ServerForStepClosed \"%s\"", pJobFullName);

			pJobState->pRemoteCmdState = StructCreate(parse_JobManagerRemoteCmdState);

			pJobState->pRemoteCmdState->iCrashRemoteCommandGroupID = pServerCrashGroup->iUidFromSubmitter;
			pJobState->pRemoteCmdState->iCloseRemoteCommandGroupID = pServerCloseGroup->iUidFromSubmitter;

			RemoteCommand_AddThingToDoOnServerCrash(GLOBALTYPE_CONTROLLER, 0, pJobState->pDef->pRemoteCmdDef->eTypeForCommand, 
				pJobState->pDef->pRemoteCmdDef->iIDForCommand, pServerCrashGroup);
			RemoteCommand_AddThingToDoOnServerClose(GLOBALTYPE_CONTROLLER, 0, pJobState->pDef->pRemoteCmdDef->eTypeForCommand, 
				pJobState->pDef->pRemoteCmdDef->iIDForCommand, pServerCloseGroup);
			DestroyRemoteCommandGroup(pServerCrashGroup);
			DestroyRemoteCommandGroup(pServerCloseGroup);

			estrStackCreateSize(&pCommandString, 4096);
			estrStackCreateSize(&pCommandDebugNameString, 4096);

			estrCopy2(&pCommandDebugNameString, pJobState->pDef->pRemoteCmdDef->pCommandString);
			estrTruncateAtFirstOccurrence(&pCommandDebugNameString, ' ');

			estrConcatf(&pCommandString, "%sremotecommand %s", pJobState->pDef->pRemoteCmdDef->bSlow ? "slow" : "", pJobState->pDef->pRemoteCmdDef->pCommandString);
			estrReplaceOccurrences(&pCommandString, "$JOBNAME$", pJobFullName);

			baseTransaction.pData = pCommandString;
			baseTransaction.recipient.containerID = pJobState->pDef->pRemoteCmdDef->iIDForCommand;
			baseTransaction.recipient.containerType = pJobState->pDef->pRemoteCmdDef->eTypeForCommand;
			baseTransaction.pRequestedTransVariableNames = NULL;
			eaPush(&ppBaseTransactions, &baseTransaction);
			
			RequestNewTransaction( objLocalManager(), pCommandDebugNameString, ppBaseTransactions, TRANS_TYPE_SEQUENTIAL_ATOMIC, objCreateManagedReturnVal(JobRemoteCommandCB, pJobFullName), 0 );

			JobLog(pGroupState, "Starting job %s. Requesting remote command \"%s\" on %s[%u]",
				pJobState->pDef->pJobName, pCommandString, GlobalTypeToName(pJobState->pDef->pRemoteCmdDef->eTypeForCommand), pJobState->pDef->pRemoteCmdDef->iIDForCommand);


			eaDestroy(&ppBaseTransactions);
			estrDestroy(&pCommandDebugNameString);
			estrDestroy(&pCommandString);

			estrCopy2(&pJobState->pStateString, "Requested remote command");
			
			if (pJobState->pDef->pRemoteCmdDef->iInitialCommandTimeout)
			{
				TimedCallback_Run(JobRemoteCommandTimeoutCB, strdup(pJobFullName), pJobState->pDef->pRemoteCmdDef->iInitialCommandTimeout);
			}
		}

	}
}

void CreateNewJobFromDef(JobManagerJobGroupState *pGroupState, JobManagerJobDef *pJobDef)
{
	JobManagerJobState *pJobState = StructCreate(parse_JobManagerJobState);
	pJobState->pDef = StructClone(parse_JobManagerJobDef, pJobDef);
	assert(pJobState->pDef);
	eaPush(&pGroupState->ppJobStates, pJobState);
	
	if (eaSize(&pJobDef->ppJobsIDependOn))
	{
		pJobState->eResult = JMR_WAITING;
	}
	else
	{
		JobQueues_AddOrStartJob(pGroupState, pJobState);
	}
}

void StartNewJobFromSummary(JobSummary *pSummary)
{
	int i;
	static U32 iNextRcgId = 1;
	JobManagerGroupQueue *pQueue;

	JobManagerJobGroupState *pGroupState = pSummary->pGroupState = StructCreate(parse_JobManagerJobGroupState);
	JobManagerJobGroupDef *pGroupDef = pGroupState->pDef = pSummary->pGroupDef;
	assert(pGroupState->pDef);
	pSummary->iStartTime = timeSecondsSince2000();
	pGroupState->pSummary = pSummary;

	estrClear(&pSummary->pLogString);
	estrClear(&pSummary->pJumpQueue);


	JobLog(pGroupState, "Activating new job group %s at %s, comment: %s", pGroupState->pDef->pJobGroupName, timeGetLocalDateStringFromSecondsSince2000(pSummary->iStartTime), 
		pGroupState->pDef->pComment ? pGroupState->pDef->pComment : "(NONE)");

	for (i=0; i < eaSize(&pGroupDef->ppJobs); i++)
	{
		CreateNewJobFromDef(pGroupState, pGroupDef->ppJobs[i]);
	}

	if (pGroupDef->pWhatToDoOnJobManagerCrash)
	{
		pGroupState->iCrashGroupID = iNextRcgId++;
		pGroupState->iCloseGroupID = iNextRcgId++;

		pGroupDef->pWhatToDoOnJobManagerCrash->eSubmitterServerType = GetAppGlobalType();
		pGroupDef->pWhatToDoOnJobManagerCrash->iSubmitterContainerID = GetAppGlobalID();
		pGroupDef->pWhatToDoOnJobManagerCrash->iUidFromSubmitter = pGroupState->iCrashGroupID;

		RemoteCommand_AddThingToDoOnServerCrash(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), pGroupDef->pWhatToDoOnJobManagerCrash);

		pGroupDef->pWhatToDoOnJobManagerCrash->iUidFromSubmitter = pGroupState->iCloseGroupID;

		RemoteCommand_AddThingToDoOnServerClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), pGroupDef->pWhatToDoOnJobManagerCrash);
	}


	pQueue = FindGroupQueue(pGroupDef->pGroupTypeName);
	pQueue->iNumActive++;
}



char *GetFullJobName(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob)
{
	static char *pRetVal = NULL;
	estrPrintf(&pRetVal, "%s | %s", pGroup->pDef->pJobGroupName, pJob->pDef->pJobName);

	return pRetVal;
}
 
bool FindGroupAndJobFromFullJobName(char *pFullName, JobManagerJobGroupState **ppGroup, JobManagerJobState **ppJob)
{
	static char **ppStrs = NULL;
	
	JobSummary *pSummary;
	int i;

	eaDestroyEx(&ppStrs, NULL);

	DivideString(pFullName, "|", &ppStrs, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	if (eaSize(&ppStrs) != 2)
	{
		return false;
	}

	pSummary = GetSummaryByName(ppStrs[0]);
	if (!pSummary)
	{
		return false;
	}

	if (!pSummary->pGroupState)
	{
		return false;
	}

	for (i=0; i < eaSize(&pSummary->pGroupState->ppJobStates); i++)
	{
		if (stricmp(pSummary->pGroupState->ppJobStates[i]->pDef->pJobName, ppStrs[1]) == 0)
		{
			if (ppGroup)
			{
				*ppGroup = pSummary->pGroupState;
			}
			if (ppJob)
			{
				*ppJob = pSummary->pGroupState->ppJobStates[i];
			}

			return true;
		}
	}

	return false;
}

#undef FailJob
void FailJob(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob, bool bFailedForExternalReasons, const char *pReason, ...)
{
	char *pFullReason = NULL;

	if (pJob->eResult != JMR_ONGOING)
	{
		return;
	}

	if (pReason)
	{
		estrGetVarArgs(&pFullReason, pReason);
	}
	else
	{
		estrCopy2(&pFullReason, "(No Reason given)");
	}

	JobLog(pGroup, "Job %s failing because: %s", pJob->pDef->pJobName, pFullReason);

	log_printf(LOG_ERRORS, "Failing group %s step %s because: %s",
		pGroup->pDef->pJobGroupName, pJob->pDef->pJobName, pFullReason);

	pJob->iCompleteTime = timeSecondsSince2000();
	pJob->eResult = JMR_FAILED;
	estrCopy2(&pJob->pStateString, pFullReason);

	switch (pJob->pDef->eType)
	{
	xcase JOB_SERVER_W_CMD_LINE:
		if (pJob->pServerWCmdLineState->iServerID)
		{
			RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 0, pJob->pDef->pServerWCmdLineDef->eServerTypeToLaunch, pJob->pServerWCmdLineState->iServerID, pJob->pServerWCmdLineState->iCrashRemoteCommandGroupID);
			RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 1, pJob->pDef->pServerWCmdLineDef->eServerTypeToLaunch, pJob->pServerWCmdLineState->iServerID, pJob->pServerWCmdLineState->iCloseRemoteCommandGroupID);
			RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, pJob->pDef->pServerWCmdLineDef->eServerTypeToLaunch,
				pJob->pServerWCmdLineState->iServerID,
				STACK_SPRINTF("Job %s (group %s) failed because %s", pJob->pDef->pJobName, pGroup->pDef->pJobGroupName,
				pFullReason));
		}
	xcase JOB_REMOTE_CMD:
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 0, pJob->pDef->pRemoteCmdDef->eTypeForCommand, pJob->pDef->pRemoteCmdDef->iIDForCommand, pJob->pRemoteCmdState->iCrashRemoteCommandGroupID);
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 1, pJob->pDef->pRemoteCmdDef->eTypeForCommand, pJob->pDef->pRemoteCmdDef->iIDForCommand, pJob->pRemoteCmdState->iCloseRemoteCommandGroupID);
		if (bFailedForExternalReasons && pJob->pDef->pRemoteCmdDef->pCancelCommandString)
		{
			char *pCmdString;
			estrStackCreate(&pCmdString);
			estrPrintf(&pCmdString, "remotecommand %s", pJob->pDef->pRemoteCmdDef->pCancelCommandString);
			estrReplaceOccurrences(&pCmdString, "$JOBNAME$", GetFullJobName(pGroup, pJob));
			objRequestTransactionSimple(NULL, pJob->pDef->pRemoteCmdDef->eTypeForCommand, pJob->pDef->pRemoteCmdDef->iIDForCommand, NULL, pCmdString);
			estrDestroy(&pCmdString);
		}
	}

	FailJobGroup(pGroup, false, "Job %s failed because %s", pJob->pDef->pJobName, pFullReason);
	estrDestroy(&pFullReason);
}

#undef SucceedJob
void SucceedJob(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob, const char *pReason, ...)
{
	char *pFullReason = NULL;

	if (pJob->eResult != JMR_ONGOING)
	{
		return;
	}

	switch (pJob->pDef->eType)
	{
	case JOB_SERVER_W_CMD_LINE:
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 0, pJob->pDef->pServerWCmdLineDef->eServerTypeToLaunch, pJob->pServerWCmdLineState->iServerID, pJob->pServerWCmdLineState->iCrashRemoteCommandGroupID);
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 1, pJob->pDef->pServerWCmdLineDef->eServerTypeToLaunch, pJob->pServerWCmdLineState->iServerID, pJob->pServerWCmdLineState->iCloseRemoteCommandGroupID);
		break;

	case JOB_REMOTE_CMD:
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 0, pJob->pDef->pRemoteCmdDef->eTypeForCommand, pJob->pDef->pRemoteCmdDef->iIDForCommand, pJob->pRemoteCmdState->iCrashRemoteCommandGroupID);
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 1, pJob->pDef->pRemoteCmdDef->eTypeForCommand, pJob->pDef->pRemoteCmdDef->iIDForCommand, pJob->pRemoteCmdState->iCloseRemoteCommandGroupID);
		break;

	}

	if (pReason)
	{
		estrGetVarArgs(&pFullReason, pReason);
	}
	else
	{
		estrCopy2(&pFullReason, "(No Reason given)");
	}

	JobLog(pGroup, "Job %s succeeding because: %s", pJob->pDef->pJobName, pFullReason);

	pJob->iCompleteTime = timeSecondsSince2000();
	pJob->eResult = JMR_SUCCEEDED;
	estrCopy2(&pJob->pStateString, pFullReason);

	estrDestroy(&pFullReason);

	pGroup->iNumStepsSucceeded++;
	if (pGroup->iNumStepsSucceeded == eaSize(&pGroup->ppJobStates))
	{
		SucceedJobGroup(pGroup);
	}
	else
	{
		int i;

		for (i=0; i < eaSize(&pGroup->ppJobStates); i++)
		{
			if (pGroup->ppJobStates[i]->eResult == JMR_WAITING)
			{
				int iIndex = eaFindString(&pGroup->ppJobStates[i]->pDef->ppJobsIDependOn, pJob->pDef->pJobName);
				if (iIndex >= 0)
				{
					free(pGroup->ppJobStates[i]->pDef->ppJobsIDependOn[iIndex]);
					eaRemoveFast(&pGroup->ppJobStates[i]->pDef->ppJobsIDependOn, iIndex);
					if (eaSize(&pGroup->ppJobStates[i]->pDef->ppJobsIDependOn) == 0)
					{
						JobQueues_AddOrStartJob(pGroup, pGroup->ppJobStates[i]);
					}
				}
			}
		}
	}
	
}


void CleanupJobGroupByName(char *pName)
{
	JobSummary *pSummary = GetSummaryByName(pName);
	JobManagerGroupQueue *pQueue;

	if (!pSummary || !pSummary->pGroupState || !pSummary->pGroupDef)
	{
		return;
	}

	pQueue = FindGroupQueue(pSummary->pGroupDef->pGroupTypeName);

	if (pSummary->pGroupDef->pWhatToDoOnJobManagerCrash)
	{
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 0, GetAppGlobalType(), GetAppGlobalID(), pSummary->pGroupState->iCrashGroupID);
		RemoteCommand_RemoveThingToDoOnServerCrashOrClose(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), 1, GetAppGlobalType(), GetAppGlobalID(), pSummary->pGroupState->iCloseGroupID);
	}

	RegisterCompletedSummary(pSummary);

	StructDestroy(parse_JobManagerJobGroupState, pSummary->pGroupState);
	pSummary->pGroupState = NULL;
	pSummary->pGroupDef = NULL;


	InformQueueOfActiveGroupCompletion(pQueue);

}

#undef FailJobGroup
void FailJobGroup(JobManagerJobGroupState *pGroup, bool bCancelled, const char *pReason_In, ...)
{
	int i;
	char *pReason = NULL;
	JobManagerGroupQueue *pQueue;

	if (pGroup->bFailing)
	{
		return;
	}

	pQueue = FindGroupQueue(pGroup->pDef->pGroupTypeName);

	if (pQueue)
	{
		EventCounter_ItHappened(pQueue->pFailedCounter, timeSecondsSince2000());
	}


	estrGetVarArgs(&pReason, pReason_In);

	log_printf(LOG_ERRORS, "Job %s failed because: %s", pGroup->pDef->pJobGroupName, pReason);
	JobLog(pGroup, "Job group %s failing because: %s", pGroup->pDef->pJobGroupName, pReason);
	Errorf("Job group %s failing because: %s", pGroup->pDef->pJobGroupName, pReason);

	if (pGroup->pDef->bAlertOnFailure && !bCancelled)
	{
		CRITICAL_NETOPS_ALERT("JOB_FAILED", "A %s job has failed. Log info:\n\n%s", pGroup->pDef->pGroupTypeName, pGroup->pSummary->pLogString ? pGroup->pSummary->pLogString : "(no logging... that is odd)");
	}

	pGroup->bFailing = true;
	pGroup->pSummary->iCompletionTime = timeSecondsSince2000();

	for (i=0; i < eaSize(&pGroup->ppJobStates); i++)
	{
		FailJob(pGroup, pGroup->ppJobStates[i], true, "Parent group failed");
	}

	if (pGroup->pDef->pWhatToDoOnFailure)
	{
		ReplaceRCGMacros(pGroup->pSummary, pGroup->pDef->pWhatToDoOnFailure); 
		ExecuteAndFreeRemoteCommandGroup(pGroup->pDef->pWhatToDoOnFailure, CleanupJobGroupByName, pGroup->pDef->pJobGroupName);
		pGroup->pDef->pWhatToDoOnFailure = NULL;
	}
	else
	{
		CleanupJobGroupByName(pGroup->pDef->pJobGroupName);
	}
}

void CleanupJobOnGroupSuccess(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob)
{

}


void SucceedJobGroup(JobManagerJobGroupState *pGroup)
{
	int i;
	JobManagerGroupQueue *pQueue = FindGroupQueue(pGroup->pDef->pGroupTypeName);

	JobLog(pGroup, "Job group succeeding");

	pGroup->pSummary->iCompletionTime = timeSecondsSince2000();


	if (pQueue)
	{
		EventCounter_ItHappened(pQueue->pSucceededCounter, pGroup->pSummary->iCompletionTime);
		pQueue->iTotalSucceededTime += pGroup->pSummary->iCompletionTime - pGroup->pSummary->iStartTime;
		if (pGroup->pSummary->iQueuedTime)
		{
			pQueue->iTotalQueuedTime += pGroup->pSummary->iStartTime - pGroup->pSummary->iQueuedTime;
		}
	}



	for (i=0; i < eaSize(&pGroup->ppJobStates); i++)
	{
		CleanupJobOnGroupSuccess(pGroup, pGroup->ppJobStates[i]);
	}

	if (pGroup->pDef->pWhatToDoOnSuccess)
	{
		ReplaceRCGMacros(pGroup->pSummary, pGroup->pDef->pWhatToDoOnSuccess); 
		ExecuteAndFreeRemoteCommandGroup(pGroup->pDef->pWhatToDoOnSuccess, CleanupJobGroupByName, pGroup->pDef->pJobGroupName);
		pGroup->pDef->pWhatToDoOnSuccess = NULL;
	}
	else
	{
		CleanupJobGroupByName(pGroup->pDef->pJobGroupName);
	}
}


void RegisterCompletedSummary(JobSummary *pSummary)
{
	if (pSummary->pGroupState)
	{
		pSummary->bFailed = pSummary->pGroupState->bFailing;
	}
	pSummary->iCompletionTime = timeSecondsSince2000();
	pSummary->bComplete = true;

	stashRemovePointer(sActiveAndQueuedSummariesByName, pSummary->pName, NULL);
	if (!sCompletedSummariesByName)
	{
		sCompletedSummariesByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("CompletedJobs", RESCATEGORY_SYSTEM, 0, sCompletedSummariesByName, parse_JobSummary);
	}
	stashAddPointer(sCompletedSummariesByName, pSummary->pName, pSummary, true);

	estrClear(&pSummary->pCancel);
	estrClear(&pSummary->pJumpQueue);
}

JobManagerGroupResultEnum GetCompletedJobGroupByName(char *pName, U32 *pOutStartTime, U32 *pOutEndTime)
{
	JobSummary *pSummary;

	if (!sCompletedSummariesByName)
	{
		return JMR_UNKNOWN;
	}

	if (!stashFindPointer(sCompletedSummariesByName, pName, &pSummary))
	{
		return JMR_UNKNOWN;
	}

	if (pOutStartTime)
	{
		*pOutStartTime = pSummary->iStartTime;
	}

	if (pOutEndTime)
	{
		*pOutEndTime = pSummary->iCompletionTime;
	}

	return pSummary->bFailed ? JMR_FAILED : JMR_SUCCEEDED;

}

int FindMaxActiveForQueue(const char *pGroupTypeName)
{
	char *pVal = GetValueFromNameValuePairs(&gpJobManagerConfig->ppMaxGroupQueueSizes, pGroupTypeName);
	if (pVal)
	{
		int iVal;
		if (StringToInt(pVal, &iVal))
		{
			return iVal;
		}

		assertmsgf(0, "While trying to find max queue size for queue %s, found %s, which doesn't appear to be an integer. Try fixing JobGroupManagerConfig.txt", 
			pGroupTypeName, pVal);
	}
	return giMaxInGroupQueue;
}

JobManagerGroupQueue *FindGroupQueue(const char *pGroupTypeName /*POOL_STRING*/)
{
	JobManagerGroupQueue *pRetVal = NULL;
	if (!sGroupQueuesByName)
	{
		sGroupQueuesByName = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("JobQroupQueues", RESCATEGORY_SYSTEM, 0, sGroupQueuesByName, parse_JobManagerGroupQueue);

	}

	if (!stashFindPointer(sGroupQueuesByName, pGroupTypeName, &pRetVal))
	{
		pRetVal = StructCreate(parse_JobManagerGroupQueue);
		pRetVal->iMaxActive = FindMaxActiveForQueue(pGroupTypeName);
		pRetVal->pGroupTypeName = pGroupTypeName;

		pRetVal->pFailedCounter = EventCounter_Create(timeSecondsSince2000());
		pRetVal->pSucceededCounter = EventCounter_Create(timeSecondsSince2000());

		stashAddPointer(sGroupQueuesByName, pGroupTypeName, pRetVal, false);

	}

	return pRetVal;
}

bool JobGroupQueueIsFull(const char *pGroupTypeName)
{
	JobManagerGroupQueue *pQueue = FindGroupQueue(pGroupTypeName);

	if (pQueue->iNumActive >= pQueue->iMaxActive)
	{
		return true;
	}

	return false;
}

void UpdatedPlacesInQueue(JobManagerGroupQueue *pQueue)
{
	int i;

	for (i=0; i < eaSize(&pQueue->ppQueue); i++)
	{
		estrPrintf(&pQueue->ppQueue[i]->pLogString, "QUEUED: (%d/%d)", i + 1, eaSize(&pQueue->ppQueue));
		pQueue->ppQueue[i]->iPlaceInQueue = i + 1;
	}
}

void PutSummaryIntoGroupQueue(JobSummary *pSummary)
{
	JobManagerGroupQueue *pQueue = FindGroupQueue(pSummary->pGroupDef->pGroupTypeName);
	eaPush(&pQueue->ppQueue, pSummary);
	pSummary->pQueue = pQueue;

	pSummary->iQueuedTime = timeSecondsSince2000();

	UpdatedPlacesInQueue(pQueue);

	estrPrintf(&pSummary->pJumpQueue, "JumpQueueByName %s $CONFIRM(Really jump the queue?) $NORETURN", pSummary->pName);


}


void InformQueueOfActiveGroupCompletion(JobManagerGroupQueue *pQueue)
{

	if (!pQueue->iNumActive)
	{
		AssertOrAlert("JM_QUEUE_CORRUPTION", "Queue %s somehow didn't know about an active group", pQueue->pGroupTypeName);
		return;
	}


	pQueue->iNumActive--;


	MaybeStartGroupFromGroupQueue(pQueue);
}

void MaybeStartGroupFromGroupQueue(JobManagerGroupQueue *pQueue)
{
	JobSummary *pSummary;

	if (pQueue->iNumActive >= pQueue->iMaxActive)
	{
		return;
	}

	if (!eaSize(&pQueue->ppQueue))
	{
		return;
	}

	pSummary = pQueue->ppQueue[0];
	eaRemove(&pQueue->ppQueue, 0);

	UpdatedPlacesInQueue(pQueue);

	if (!pSummary)
	{
		AssertOrAlert("JM_QUEUE_NULL", "Queue %s somehow got a NULL summary in it", pQueue->pGroupTypeName);
		return;
	}

	pSummary->pQueue = NULL;

	StartNewJobFromSummary(pSummary);
}

void CheckQueues(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	FOR_EACH_IN_STASHTABLE(sGroupQueuesByName, JobManagerGroupQueue, pQueue)
	{
		MaybeStartGroupFromGroupQueue(pQueue);
	}
	FOR_EACH_END

	UpdateJobQueues();
}

AUTO_COMMAND;
void SetGroupQueueMaxActive(char *pQueueName, int iMaxActive)
{
	JobManagerGroupQueue *pQueue = FindGroupQueue(allocAddString(pQueueName));
	pQueue->iMaxActive = iMaxActive;
	UpdateOrSetValueInNameValuePairList(&gpJobManagerConfig->ppMaxGroupQueueSizes, pQueueName, STACK_SPRINTF("%d", iMaxActive));

	WriteOutJobManagerConfig();
}

void CancelJobGroupByName(char *pSummaryName, const char *pHowCancelled)
{
	static char *pTempName = NULL;
	JobSummary *pSummary;

	estrCopy2(&pTempName, pSummaryName);
	estrTrimLeadingAndTrailingWhitespace(&pTempName);
	pSummary = GetSummaryByName(pTempName);
	if (!pSummary)
	{
		return;
	}


	if (!pSummary->pQueue)
	{
		FailJobGroup(pSummary->pGroupState, true, "Cancelled via %s", pHowCancelled);
	}
	else
	{
		if (pSummary->pQueue->ppQueue[pSummary->iPlaceInQueue-1] != pSummary)
		{
			AssertOrAlert("QUEUE_CORRUPTION", "Job Queue %s has gotten its places corrupted", pSummary->pQueue->pGroupTypeName);
			return;
		}
		eaRemove(&pSummary->pQueue->ppQueue, pSummary->iPlaceInQueue-1);
		UpdatedPlacesInQueue(pSummary->pQueue);
		estrPrintf(&pSummary->pLogString, "Cancelled via %s", pHowCancelled);
		pSummary->pQueue = NULL;
		pSummary->bFailed = true;
		RegisterCompletedSummary(pSummary);
	}
}


AUTO_COMMAND ACMD_NAME(CancelByName);
void CancelByNameCmd(ACMD_SENTENCE pSummaryName, CmdContext *pContext)
{
	CancelJobGroupByName(pSummaryName, StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, pContext->eHowCalled));
}

AUTO_COMMAND;
void JumpQueueByName(ACMD_SENTENCE pSummaryName)
{
	static char *pTempName = NULL;
	JobSummary *pSummary;
	estrCopy2(&pTempName, pSummaryName);
	estrTrimLeadingAndTrailingWhitespace(&pTempName);
	pSummary = GetSummaryByName(pTempName);
	if (!pSummary)
	{
		return;
	}

	if (!pSummary->pQueue)
	{
		return;
	}

	if (pSummary->iPlaceInQueue == 1)
	{
		return;
	}

	if (pSummary->pQueue->ppQueue[pSummary->iPlaceInQueue-1] != pSummary)
	{
		AssertOrAlert("QUEUE_CORRUPTION", "Job Queue %s has gotten its places corrupted", pSummary->pQueue->pGroupTypeName);
		return;
	}

	eaRemove(&pSummary->pQueue->ppQueue, pSummary->iPlaceInQueue-1);
	eaInsert(&pSummary->pQueue->ppQueue, pSummary, 0);

	UpdatedPlacesInQueue(pSummary->pQueue);
	




}

void QueryPatchCompletion_ForJobCB(void *pUserData)
{
	SlowRemoteCommandReturn_QueryPatchCompletion_ForJob((SlowRemoteCommandID)((intptr_t)pUserData), 1);
}


AUTO_COMMAND_REMOTE_SLOW(int);
void QueryPatchCompletion_ForJob(char *pNameSpaceName, char *pJobName, SlowRemoteCommandID iCmdID)
{
	aslPatchCompletionQuerying_Query(pNameSpaceName, QueryPatchCompletion_ForJobCB, (void*)(intptr_t)iCmdID);
}





#include "aslJobManager_h_ast.c"

