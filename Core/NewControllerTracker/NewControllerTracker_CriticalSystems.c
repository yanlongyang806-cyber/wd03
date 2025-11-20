#include "NewControllerTracker.h"
#include "NEwControllerTracker_CriticalSystems.h"
#include "NEwControllerTracker_CriticalSystems_h_ast.h"
#include "NewControllerTracker_CriticalSystems_c_ast.h"
#include "logging.h"
#include "GlobalComm.h"
#include "StatusReporting.h"
#include "StructNet.h"
#include "StatusReporting_h_ast.h"
#include "ResourceInfo.h"
#include "HttpXpathSupport.h"
#include "sock.h"
#include "file.h"
#include "CmdParse.h"
#include "StringUtil.h"
#include "../../utilities/SentryServer/Sentry_comm.h"
#include "alerts_h_ast.h"
#include "GlobalTypes_h_ast.h"
#include "stringcache.h"
#include "netsmtp.h"
#include "TimedCallback.h"
#include "NewControllerTracker_MailingLists.h"
#include "NewControllerTracker_AlertTrackers.h"
#include "Organization.h"
#include "RegistryReader.h"

//if you suppress an alert, you get an email every n hours reminding you of it
int iAlertSuppressionWarnTimeHours =  24;
AUTO_CMD_INT(iAlertSuppressionWarnTimeHours, AlertSuppressionWarnTimeHours) ACMD_COMMANDLINE;

#define TIME_IN_DOWNTIME_BEFORE_ALERT (60 * 60 * 6)

#define MAX_DOWNTIMES_TO_SAVE 4

#define STATUSLOG_ARCHIVE_TIME (60 * 60 * 24 * 7)

#define CRITICALSYSTEMS_CONFIG_FILENAME "c:/CriticalSystems/CriticalSystemConfig.txt"

#define STAY_ALIVE_FILE "c:/CriticalSystems/StayAlive.txt"

#define ALERT_ISSUE_EXP_TIME (15 * 60)

char gOnlySendTo[256] = "";


static int siEscalationEmailDelay = 300;
AUTO_CMD_INT(siEscalationEmailDelay, EscalationEmailDelay);

bool bOldEmailSystem = false;
bool bNewEmailSystem = true;

AUTO_CMD_INT(bOldEmailSystem, OldEmailSystem);
AUTO_CMD_INT(bNewEmailSystem, NewEmailSystem);


AUTO_CMD_STRING(gOnlySendTo, OnlySendTo);

int *piThrottlingStages = NULL;

char *pCriticalSystemToCreateAtStartup = NULL;
AUTO_CMD_ESTRING(pCriticalSystemToCreateAtStartup, CriticalSystemToCreateAtStartup);



char *CriticalSystems_BeginTrackingSystem(char *pSystemName, int iTimeOut);

AUTO_FIXUPFUNC;
TextParserResult fixupCriticalSystem_Status(CriticalSystem_Status* pSystem, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		if (pSystem->iSecondEmailEscalationTime)
		{
			if (pSystem->iFirstEmailEscalationTime)
			{
				pSystem->eEscStatus = ESCSTATUS_LEVEL1;
			}
			else
			{
				pSystem->eEscStatus = ESCSTATUS_LEVEL2;
			}
		}
		else
		{
			if (pSystem->pConfig->pLevel1EscalationEmail)
			{
				if (pSystem->eStatus == CRITSYSTEMSTATUS_FAILED)
				{
					pSystem->eEscStatus = ESCSTATUS_ACKNOWLEDGED;
				}
				else
				{
					pSystem->eEscStatus = ESCSTATUS_READY;
				}
			}
			else
			{
				pSystem->eEscStatus = ESCSTATUS_NONE;
			}
		}
		break;
	}

	return 1;
}


void SetDefaultThrottlingStages(void)
{
	ea32Destroy(&piThrottlingStages);
	ea32Push(&piThrottlingStages, 60);
	ea32Push(&piThrottlingStages, 300);
	ea32Push(&piThrottlingStages, 600);
}


AUTO_COMMAND;
char *SetThrottlingStages(char *pInts)
{
	static char *pRetString = NULL;
	char **ppInts = NULL;
	int i;

	bool bFailed = false;

	DivideString(pInts, ",", &ppInts, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	ea32Destroy(&piThrottlingStages);

	if (!eaSize(&ppInts))
	{
		bFailed = true;
	}
	else
	{
		for (i=0; i < eaSize(&ppInts); i++)
		{
			int iVal = atoi(ppInts[i]);
			if (!iVal || iVal < 0)
			{
				bFailed = true;
				break;
			}

			ea32Push(&piThrottlingStages, iVal);
		}
	}

	eaDestroyEx(&ppInts, NULL);

	if (bFailed)
	{
		SetDefaultThrottlingStages();
		estrPrintf(&pRetString, "Unable to process \"%s\" as throttling stages", pInts);
		printf("%s", pRetString);
	}
	else
	{
		estrPrintf(&pRetString, "\"%s\" set as new throttling stages", pInts);
	}

	return pRetString;
}



CriticalSystem_GlobalConfig gCriticalSystemsGlobalConfig = { NULL };
StashTable sCriticalSystemsByName = NULL;
CriticalSystem_Status **ppCriticalSystems = NULL;

//pCatName must be pooled
CriticalSystem_CategoryConfig *FindCategoryConfig(const char *pCatName)
{
	int i;

	for (i=0; i < eaSize(&gCriticalSystemsGlobalConfig.ppCategoryConfigs); i++)
	{
		if (gCriticalSystemsGlobalConfig.ppCategoryConfigs[i]->pCategoryName == pCatName)
		{
			return gCriticalSystemsGlobalConfig.ppCategoryConfigs[i];
		}
	}

	return NULL;
}


int GetNextIssueNumber(void)
{
	static int iNextNum = -1;
	int iRetVal;

	RegReader reader = createRegReader();
	initRegReader(reader, "HKEY_LOCAL_MACHINE\\Software\\Cryptic\\ControllerTracker");

	if (!rrReadInt(reader, "IssueNum", &iNextNum)) 
	{
		iNextNum = 10000000;
	}

	iRetVal = iNextNum;
	iNextNum++;
	rrWriteInt(reader, "IssueNum", iNextNum);
	destroyRegReader(reader);

	return iRetVal;
}

char GetIssueNumPrefixChar(void)
{
	char *pName = StatusReporting_GetMyName();

	if (pName)
	{
		return toupper(pName[0]);
	}

	return 'C';
}

void GetNewIssueNumForSystemIfNone(CriticalSystem_Status *pSystem)
{
	if (pSystem->iMainIssue)
	{
		return;
	}

	pSystem->iMainIssue = GetNextIssueNumber();
}



void WriteOutGlobalConfig(void)
{
	char *pTempString = NULL;
	estrStackCreate(&pTempString);
	estrCopy2(&pTempString, CRITICALSYSTEMS_CONFIG_FILENAME);
	mkdirtree(pTempString);
	ParserWriteTextFile(pTempString, parse_CriticalSystem_GlobalConfig, &gCriticalSystemsGlobalConfig, 0, 0);
	estrDestroy(&pTempString);
}

void ReadStatusLog(CriticalSystem_Status *pSystem)
{
	char fileName[CRYPTIC_MAX_PATH];
	StructDeInit(parse_CriticalSystem_EventList, &pSystem->eventList);
	sprintf(fileName, "c:/CriticalSystems/%s_StatusLog.txt", pSystem->pConfig->pName);

	ParserReadTextFile(fileName, parse_CriticalSystem_EventList, &pSystem->eventList, 0);


}

void WriteStatusLogAndMaybeArchive(CriticalSystem_Status *pSystem)
{
	U32 iCurTime = timeSecondsSince2000();
	int iNumLogs = eaSize(&pSystem->eventList.ppEvents);
	char fileName[CRYPTIC_MAX_PATH];

	if (iNumLogs && (iCurTime - pSystem->eventList.ppEvents[iNumLogs - 1]->iTime > 2 * STATUSLOG_ARCHIVE_TIME))
	{
		char *pArchiveFileName1 = NULL;
		char *pArchiveFileName2 = NULL;
		CriticalSystem_EventList archiveList = {NULL};

		int i;

		estrStackCreate(&pArchiveFileName1);
		estrStackCreate(&pArchiveFileName2);

		estrPrintf(&pArchiveFileName1, "%s_StatusArchive_%s", pSystem->pConfig->pName, timeGetDateNoTimeStringFromSecondsSince2000(iCurTime));
		estrMakeAllAlphaNumAndUnderscores(&pArchiveFileName1);
		estrPrintf(&pArchiveFileName2, "c:/CriticalSystems/%s.txt", pArchiveFileName1);

		i = iNumLogs - 1;

		while (iCurTime - pSystem->eventList.ppEvents[i]->iTime > STATUSLOG_ARCHIVE_TIME)
		{
			i--;
		}

		i++;
		while (i < iNumLogs)
		{
			eaPush(&archiveList.ppEvents, pSystem->eventList.ppEvents[i]);
			eaRemove(&pSystem->eventList.ppEvents, i);
			iNumLogs--;
		}


		mkdirtree(pArchiveFileName2);
		ParserWriteTextFile(pArchiveFileName2, parse_CriticalSystem_EventList, &archiveList, 0, 0);
		StructDeInit(parse_CriticalSystem_EventList, &archiveList);

		estrDestroy(&pArchiveFileName1);
		estrDestroy(&pArchiveFileName1);
	}



	sprintf(fileName, "c:/CriticalSystems/%s_StatusLog.txt", pSystem->pConfig->pName);
	mkdirtree(fileName);

	ParserWriteTextFile(fileName, parse_CriticalSystem_EventList, &pSystem->eventList, 0, 0);
}

//pRecipients is a comma-separated list of full email addresses
static void SendEmailToList(char *pRecipients, char *pSubject, char *pBody, char *pSendName)
{
	char *pSubjectToUse = NULL;
	SMTPMessageRequest *pReq;
	char *pResultEstr = NULL;

	static NetComm *pEmailComm = NULL;

	if (!pEmailComm)
	{
		pEmailComm = commCreate(0,0);
	}


	estrCopy2(&pSubjectToUse, pSubject);
	

	if (gOnlySendTo[0])
	{
		pRecipients = gOnlySendTo;
	}


	pReq = StructCreate(parse_SMTPMessageRequest);


	DivideString(pRecipients, ",", &pReq->to, DIVIDESTRING_POSTPROCESS_ESTRINGS);
	
	estrPrintf(&pReq->from, "%s", pSendName);

	estrPrintf(&pReq->subject, "%s", pSubjectToUse);
	estrPrintf(&pReq->body, "%s", pBody ? pBody : "(No body)");

	pReq->comm = pEmailComm;


	if (!smtpMsgRequestSend(pReq, &pResultEstr))
	{
		//do something here... not clear what
	}

	StructDestroy(parse_SMTPMessageRequest, pReq);
	estrDestroy(&pResultEstr);
}

//***pppRecipients is an earray of comma-separated strings. This removes duplicates before sending
static void SendEmailToLists(char ***pppRecipientStrings, char *pSubject, char *pBody, char *pSendName)
{
	char **ppRecipients = NULL;
	char *pFinalString = NULL;

	int i, j;

	for (i=0; i < eaSize(pppRecipientStrings); i++)
	{
		char **ppTempRecipients = NULL;

		ExtractAlphaNumTokensFromStringEx((*pppRecipientStrings)[i], &ppTempRecipients, "@.");

		for (j=0; j < eaSize(&ppTempRecipients); j++)
		{
			if (eaFindString(&ppRecipients, ppTempRecipients[j]) == -1)
			{
				eaPush(&ppRecipients, ppTempRecipients[j]);
				ppTempRecipients[j] = NULL;
			}
		}

		eaDestroyEx(&ppTempRecipients, NULL);
	}

	for (i=0; i < eaSize(&ppRecipients); i++)
	{
		estrConcatf(&pFinalString, "%s%s", i == 0 ? "" : ", ", ppRecipients[i]);
	}
	eaDestroyEx(&ppRecipients, NULL);

	SendEmailToList(pFinalString, pSubject, pBody, pSendName);

	estrDestroy(&pFinalString);
}



void MakeModifySystemCommand(CriticalSystem_Status *pSystem)
{
	estrPrintf(&pSystem->pModify, "ModifySystem %s $STRING(Enter remove to stop tracking %s) $INT(Enter new delay-before-failure (currently %d)) $INT(Enter new delay-before-failure if disconnected (currently %d)) $INT(Enter new pause-after-failure-before-restarting (-1 = never) (currently %d)) $STRING(Enter new email recipients (currently %s)) $STRING(Enter email recipients to remove) $STRING(Enter level 1 escalation emails for customer facing system, NONE to remove (currently %s)) $STRING(Enter level 2 escalation emails for customer facing system (currently %s)) $CONFIRM(Really modify?)",
		pSystem->pConfig->pName, pSystem->pConfig->pName, pSystem->pConfig->iMaxStallTimeBeforeAssumedDeath, pSystem->pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath,
		pSystem->pConfig->iStallTimeAfterDeathBeforeRestarting,
		pSystem->pConfig->pEmailRecipients,
		pSystem->pConfig->pLevel1EscalationEmail,
		pSystem->pConfig->pLevel2EscalationEmail);
}


void GetAllEmailListsForSystem(char ***pppOutLists, CriticalSystem_Status *pSystem, bool bSystemWentDown)
{
	int i;
	CriticalSystem_CategoryConfig *pCatConfig;

	if (gCriticalSystemsGlobalConfig.pGlobalEmailRecipients && gCriticalSystemsGlobalConfig.pGlobalEmailRecipients[0])
	{
		eaPush(pppOutLists, gCriticalSystemsGlobalConfig.pGlobalEmailRecipients);
	}

	if (bSystemWentDown)
	{
		if (gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients && gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients[0])
		{
			eaPush(pppOutLists, gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients);
		}
	}

	if (pSystem->pConfig->pEmailRecipients && pSystem->pConfig->pEmailRecipients[0])
	{
		eaPush(pppOutLists, pSystem->pConfig->pEmailRecipients);
	}

	for (i=0; i < eaSize(&pSystem->pConfig->ppCategories); i++)
	{
		pCatConfig = FindCategoryConfig(pSystem->pConfig->ppCategories[i]);

		if (pCatConfig && pCatConfig->pEmailRecipients && pCatConfig->pEmailRecipients[0])
		{
			eaPush(pppOutLists, pCatConfig->pEmailRecipients);
		}
	}
}

void BeginEscalation(CriticalSystem_Status *pSystem)
{
	pSystem->iEscalationBeganTime = timeSecondsSince2000();
	pSystem->iFirstEmailEscalationTime = timeSecondsSince2000() + siEscalationEmailDelay;
	pSystem->iSecondEmailEscalationTime = timeSecondsSince2000() + siEscalationEmailDelay * 2;

}

AUTO_COMMAND;
void AcknowledgeSystemDown(char *pSystemName, bool bStatusChanged, ACMD_SENTENCE pComment)
{
	CriticalSystem_Status *pSystem;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	pSystem->iFirstEmailEscalationTime = 0;
	pSystem->iSecondEmailEscalationTime = 0;

	if (estrLength(&pSystem->pLastAddressesSentEscalationTo))
	{
		char *pSubject = NULL;
		char *pBody = NULL;

		if (bStatusChanged)
		{
			estrPrintf(&pSubject, "%s now %s", pSystemName, StaticDefineIntRevLookup(enumCriticalSystemStatusEnum, pSystem->eStatus));
			estrPrintf(&pBody, "Critical system %s had failed, and the failure was NOT ACKNOWLEDGED, but it is now %s", 
				pSystemName, StaticDefineIntRevLookup(enumCriticalSystemStatusEnum, pSystem->eStatus));
		}
		else
		{
			estrPrintf(&pSubject, "%s failure ACKNOWLEDGED", pSystemName);
			estrPrintf(&pBody, "Failure of %s has been acknowledged with comment: %s",
				pSystemName, pComment);
		}

		SendEmail_Simple(pSystem->pLastAddressesSentEscalationTo, pSubject, pBody);

		estrDestroy(&pSystem->pLastAddressesSentEscalationTo);
	}
}


void SendEscalationEmail(CriticalSystem_Status *pSystem, char *pRecipient)
{
	char *pDuration = NULL;
	char *pSubject = NULL;
	char *pBody = NULL;

	estrPrintf(&pSubject, "%s is STILL DOWN, NO ACK!!", pSystem->pName_Internal);
	timeSecondsDurationToPrettyEString(timeSecondsSince2000() - pSystem->iEscalationBeganTime, &pDuration);
	estrPrintf(&pBody, "%s has been down for %s, this has not yet been acknowledged. This is very bad.",
		pSystem->pName_Internal, pDuration);

	SendEmail_Simple(pRecipient, pSubject, pBody);

	estrCopy2(&pSystem->pLastAddressesSentEscalationTo, pRecipient);

	estrDestroy(&pDuration);
	estrDestroy(&pSubject);
	estrDestroy(&pBody);
}

void CheckSystemForEscalationEmails(CriticalSystem_Status *pSystem)
{
	if (!pSystem->iSecondEmailEscalationTime)
	{
		return;
	}

	if (pSystem->iFirstEmailEscalationTime && timeSecondsSince2000() > pSystem->iFirstEmailEscalationTime)
	{
		SendEscalationEmail(pSystem, pSystem->pConfig->pLevel1EscalationEmail);
		pSystem->iFirstEmailEscalationTime = 0;
	}
	else if (timeSecondsSince2000() > pSystem->iSecondEmailEscalationTime)
	{
		SendEscalationEmail(pSystem, pSystem->pConfig->pLevel2EscalationEmail);
		pSystem->iSecondEmailEscalationTime += siEscalationEmailDelay;
	}
}

void SetSystemStatus(CriticalSystem_Status *pSystem, enumCriticalSystemStatus eStatus)
{
	CriticalSystem_Event *pEvent;
	enumCriticalSystemStatus eOldStatus = pSystem->eStatus;


	if (eOldStatus == eStatus)
	{
		return;
	}

	if (pSystem->pEndDowntimeCB)
	{
		TimedCallback_Remove(pSystem->pEndDowntimeCB);
		pSystem->pEndDowntimeCB = NULL;
	}

	objLog(LOG_CRITICALSYSTEMS, GLOBALTYPE_CONTROLLERTRACKER, 0, 0, pSystem->pConfig->pName, NULL, NULL, "StateChange", NULL, "NewState %s", StaticDefineIntRevLookup(enumCriticalSystemStatusEnum, eStatus));


	pSystem->eStatus = eStatus;


	if (pSystem->iSecondEmailEscalationTime)
	{
		AcknowledgeSystemDown(pSystem->pName_Internal, true, NULL);
	}

	if (eStatus == CRITSYSTEMSTATUS_DOWNTIME)
	{
		estrClear(&pSystem->pBeginDowntime);
		estrPrintf(&pSystem->pEndDowntime, "EndDownTime %s $CONFIRM(End downtime for %s?) $NORETURN", pSystem->pConfig->pName, pSystem->pConfig->pName);
	}
	else
	{
		estrClear(&pSystem->pEndDowntime);
		estrPrintf(&pSystem->pBeginDowntime, "BeginDownTime %s $INT(How many minutes, or zero for unbounded) $CONFIRM(Begin downtime for %s?) $NORETURN", pSystem->pConfig->pName, pSystem->pConfig->pName);
	}

	if (eStatus == CRITSYSTEMSTATUS_FAILED)
	{
		char *pSubjectString = NULL;
		char *pBodyString = NULL;
		char **ppLists = NULL;

		GetNewIssueNumForSystemIfNone(pSystem);

		estrStackCreate(&pSubjectString);
		estrStackCreate(&pBodyString);

		estrPrintf(&pSubjectString, "%s is down!%s", pSystem->pConfig->pName, pSystem->pConfig->pLevel1EscalationEmail ? " (ACK NEEDED)" : "");
		estrPrintf(&pBodyString, "%s has not responded in %d seconds.\n%s\nUniqueID: %c%d\n", pSystem->pConfig->pName, timeSecondsSince2000() - pSystem->iLastContactTime, 
			pSystem->pConfig->pLevel1EscalationEmail ? "THIS MUST BE ACKNOWLEDGED!!!!!\n" : "",
			GetIssueNumPrefixChar(), pSystem->iMainIssue);

		if (bNewEmailSystem)
		{
			SendEmailWithMailingLists(pSystem, EMAILTYPE_SYSTEMDOWN, 0, 0, NULL, GLOBALTYPE_NONE, GLOBALTYPE_NONE, 0, pSubjectString, pSubjectString, pBodyString, pBodyString, &pSystem->pLastAddressesSentEscalationTo);
		}

		if (bOldEmailSystem)
		{
			GetAllEmailListsForSystem(&ppLists, pSystem, true);
			SendEmailToLists(&ppLists, pSubjectString, pBodyString, "CriticalAlerts@" ORGANIZATION_DOMAIN);
		
			eaDestroy(&ppLists);
		}

		estrDestroy(&pSubjectString);
		estrDestroy(&pBodyString);

		if (pSystem->pConfig->pLevel1EscalationEmail)
		{
			BeginEscalation(pSystem);
		}
	}

	if (eStatus == CRITSYSTEMSTATUS_RUNNING)
	{
		if (eOldStatus == CRITSYSTEMSTATUS_FAILED || eOldStatus == CRITSYSTEMSTATUS_TRYINGTORESTART)
		{
			char *pSubjectString = NULL;
			char *pBodyString = NULL;
			char **ppLists = NULL;

			estrStackCreate(&pSubjectString);
			estrStackCreate(&pBodyString);

			estrPrintf(&pSubjectString, "%s is up!", pSystem->pConfig->pName);
			estrPrintf(&pBodyString, "%s is responding\n\nUniqueID: %c%d\n", pSystem->pConfig->pName, GetIssueNumPrefixChar(), pSystem->iMainIssue);

			if (bOldEmailSystem)
			{
				GetAllEmailListsForSystem(&ppLists, pSystem, false);
				SendEmailToLists(&ppLists, pSubjectString, pBodyString, "CriticalAlerts@" ORGANIZATION_DOMAIN);
				eaDestroy(&ppLists);
			}

			if (bNewEmailSystem)
			{
				SendEmailWithMailingLists(pSystem, EMAILTYPE_SYSTEMUP,
					0, 0, NULL, GLOBALTYPE_NONE, GLOBALTYPE_NONE, 0, pSubjectString, pSubjectString, pBodyString, pBodyString, NULL);
			}


			estrDestroy(&pSubjectString);
			estrDestroy(&pBodyString);

			pSystem->iMainIssue = 0;
		}
	}

	pEvent = StructCreate(parse_CriticalSystem_Event);
	pEvent->eNewStatus = eStatus;
	pEvent->iTime = timeSecondsSince2000();
	eaInsert(&pSystem->eventList.ppEvents, pEvent, 0);


	WriteStatusLogAndMaybeArchive(pSystem);


}

void ExecuteCommandThroughSentryServer(U32 iIP, char *pCommand)
{

	static NetLink *spLinkToSentryServer = NULL;

	if (spLinkToSentryServer && (!linkConnected(spLinkToSentryServer) || linkDisconnected(spLinkToSentryServer)))
	{
		linkRemove(&spLinkToSentryServer);
	}

	if (!spLinkToSentryServer)
	{
		spLinkToSentryServer = commConnectWait(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,"SentryServer",SENTRYSERVERMONITOR_PORT,NULL,NULL,NULL,0, 0.5f);
	}

	if (linkConnected(spLinkToSentryServer))
	{

		Packet *pPak = pktCreate(spLinkToSentryServer, MONITORCLIENT_LAUNCH);
		pktSendString(pPak, makeIpStr(iIP));
		pktSendString(pPak, pCommand);
		pktSend(&pPak);

		printf("Just executed command %s on system %s with sentry server\n", pCommand, makeIpStr(iIP));
	}
	else
	{
		printf("Wanted to execute command %s on system %s with sentry server, but couldn't\n", pCommand, makeIpStr(iIP));
	}
}

void CriticalSystems_PeriodicUpdate(void)
{
	int i, j;
	U32 iCurTime = timeSecondsSince2000();
	int iInactivityTime;
	FILE *pFile;

	for (i=0; i < eaSize(&ppCriticalSystems); i++)
	{
		CriticalSystem_Status *pSystem = ppCriticalSystems[i];

		CheckSystemForEscalationEmails(pSystem);

		switch (pSystem->eStatus)
		{
		case CRITSYSTEMSTATUS_RUNNING:
			iInactivityTime = iCurTime - pSystem->iLastContactTime;
			if (iInactivityTime > STATUS_REPORTING_INTERVAL * 2)
			{
				SetSystemStatus(pSystem, CRITSYSTEMSTATUS_STALLED);
			}
			break;

		case CRITSYSTEMSTATUS_STALLED:
			iInactivityTime = iCurTime - pSystem->iLastContactTime;
			if (iInactivityTime > pSystem->pConfig->iMaxStallTimeBeforeAssumedDeath
				|| pSystem->pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath && (pSystem->iLastDisconnectTime > pSystem->iLastContactTime) && iInactivityTime > pSystem->pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath)
			{
				SetSystemStatus(pSystem, CRITSYSTEMSTATUS_FAILED);
			}
			break;

		case CRITSYSTEMSTATUS_FAILED:
			if (pSystem->pConfig->iStallTimeAfterDeathBeforeRestarting != -1 && pSystem->lastStatus.pRestartingCommand && pSystem->lastStatus.pRestartingCommand[0])
			{
				iInactivityTime = iCurTime - pSystem->iLastContactTime;

				if (iInactivityTime > pSystem->pConfig->iMaxStallTimeBeforeAssumedDeath + pSystem->pConfig->iStallTimeAfterDeathBeforeRestarting)
				{
					ExecuteCommandThroughSentryServer(pSystem->IP, pSystem->lastStatus.pRestartingCommand);
					SetSystemStatus(pSystem, CRITSYSTEMSTATUS_TRYINGTORESTART);
				}

			}
			break;
		case CRITSYSTEMSTATUS_DOWNTIME:
			if (iCurTime > pSystem->iNextDownTimeAlertTime)
			{
				char *pSubjectString = NULL;
				char *pBodyString = NULL;

				char *pDurationString = NULL;
				char **ppLists = NULL;

				estrStackCreate(&pSubjectString);
				estrStackCreate(&pBodyString);

				timeSecondsDurationToPrettyEString(iCurTime - pSystem->iTimeBeganDownTime, &pDurationString);


				estrPrintf(&pSubjectString, "%s still in downtime!", pSystem->pConfig->pName);
				estrPrintf(&pBodyString, "%s has been in downtime for %s\n\nUniqueID: %c%d\n", pSystem->pConfig->pName, pDurationString, GetIssueNumPrefixChar(), pSystem->iMainIssue);

				if (bOldEmailSystem)
				{
					GetAllEmailListsForSystem(&ppLists, pSystem, true);
					SendEmailToLists(&ppLists, pSubjectString, pBodyString, "CriticalAlerts@" ORGANIZATION_DOMAIN);
					eaDestroy(&ppLists);
				}
				
				if (bNewEmailSystem)
				{
					SendEmailWithMailingLists(pSystem, EMAILTYPE_STILLINDOWNTIME,
						0, 0, NULL, GLOBALTYPE_NONE, GLOBALTYPE_NONE, 0, pSubjectString, pSubjectString, pBodyString, pBodyString, NULL);
				}

				estrDestroy(&pSubjectString);
				estrDestroy(&pBodyString);
				estrDestroy(&pDurationString);
				



				pSystem->iNextDownTimeAlertTime = iCurTime + TIME_IN_DOWNTIME_BEFORE_ALERT;
			}
		}

		for (j = 0; j < eaSize(&pSystem->ppAlertSuppressions); j++)
		{
			if (iCurTime > pSystem->ppAlertSuppressions[j]->iWarnTime)
			{
				CriticalSystem_AlertSuppression *pSuppression = pSystem->ppAlertSuppressions[j];
				char *pSubjectString = NULL;
				char *pBodyString = NULL;

				char *pDurationString = NULL;
				char **ppLists = NULL;

				estrStackCreate(&pSubjectString);
				estrStackCreate(&pBodyString);

				timeSecondsDurationToPrettyEString(iCurTime - pSuppression->iStartedTime, &pDurationString);


				estrPrintf(&pSubjectString, "Alert (%s/%s) still being suppressed!", pSuppression->pAlertKey, 
					GlobalTypeToName(pSuppression->eServerType));
				estrPrintf(&pBodyString, "%s has been suppressing alert (%s/%s) for %s\n\nUniqueID: %c%d\n", pSystem->pConfig->pName,
					pSuppression->pAlertKey, GlobalTypeToName(pSuppression->eServerType), pDurationString, GetIssueNumPrefixChar(), pSystem->iMainIssue);

				if (bOldEmailSystem)
				{
					GetAllEmailListsForSystem(&ppLists, pSystem, true);
					SendEmailToLists(&ppLists, pSubjectString, pBodyString, "CriticalAlerts@" ORGANIZATION_DOMAIN);
					eaDestroy(&ppLists);
				}

				if (bNewEmailSystem)
				{
					SendEmailWithMailingLists(pSystem, EMAILTYPE_STILLSUPPRESSINGALERTS,
						0, 0, NULL,  GLOBALTYPE_NONE, GLOBALTYPE_NONE,0, pSubjectString, pSubjectString, pBodyString,
						pBodyString, NULL);
				}

				estrDestroy(&pSubjectString);
				estrDestroy(&pBodyString);
				estrDestroy(&pDurationString);
				



				pSuppression->iWarnTime = iCurTime + iAlertSuppressionWarnTimeHours * (60 * 60);


			}
		}


		if (pSystem->alertIssuesTable)
		{
			StashTableIterator stashIterator;
			StashElement element;

			stashGetIterator(pSystem->alertIssuesTable, &stashIterator);

			while (stashGetNextElement(&stashIterator, &element))
			{
				 CriticalSystem_AlertIssue *pIssue = stashElementGetPointer(element);

				 if (pIssue->iExpTime < iCurTime)
				 {
					 stashRemovePointer(pSystem->alertIssuesTable, &pIssue->key, NULL);
					 StructDestroy(parse_CriticalSystem_AlertIssue, pIssue);
				 }
			}
		}
	}
			

	pFile = fopen(STAY_ALIVE_FILE, "wt");
	if (pFile)
	{
		fprintf(pFile, "%u", iCurTime);
		fclose(pFile);
	}

}

void CreateAndAddCriticalSystemStatusFromConfig(CriticalSystem_Config *pConfig)
{
	CriticalSystem_Status *pSystem = StructCreate(parse_CriticalSystem_Status);

	pSystem->pConfig = pConfig;
	
	pSystem->pName_Internal = strdup(pConfig->pName);
	estrPrintf(&pSystem->pLink, "<a href=\"%s.CriticalSystems.all.systems[%s]\">%s</a>",
		LinkToThisServer(), pConfig->pName, pConfig->pName);


	stashAddPointer(sCriticalSystemsByName, pConfig->pName, pSystem, true);
	eaPush(&ppCriticalSystems, pSystem);

	estrPrintf(&pSystem->pStatusLog, "<a href=\"%s%s%s[%s].Struct.EventList.Events\">Log</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, "CriticalSystems", pConfig->pName);

	MakeModifySystemCommand(pSystem);

	
	ReadStatusLog(pSystem);
	SetSystemStatus(pSystem, CRITSYSTEMSTATUS_STARTUP_NEVER_CONNECTED);

}

StashTable sCriticalSystemWannaBes = NULL;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "LastContactTime, AddCriticalSystem");
typedef struct CriticalSystemWannaBe
{
	char *pName;
	U32 iLastContactTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	AST_COMMAND("AddCriticalSystem", "CriticalSystems_BeginTrackingSystem $FIELD(Name) $INT(Delay time before failure)")
} CriticalSystemWannaBe;

void ContactAttempted(char *pName)
{
	CriticalSystemWannaBe *pWannaBe;

	if (!sCriticalSystemWannaBes)
	{
		sCriticalSystemWannaBes = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("WannaBeCriticalSystems", RESCATEGORY_OTHER, 0, sCriticalSystemWannaBes, parse_CriticalSystemWannaBe);

	}

	if (stashFindPointer(sCriticalSystemWannaBes, pName, &pWannaBe))
	{
		pWannaBe->iLastContactTime = timeSecondsSince2000();
		return;
	}
	
	pWannaBe = StructCreate(parse_CriticalSystemWannaBe);
	pWannaBe->pName = strdup(pName);
	pWannaBe->iLastContactTime = timeSecondsSince2000();
	stashAddPointer(sCriticalSystemWannaBes, pWannaBe->pName, pWannaBe, true);
}

void HandleGenericStatus(Packet *pak, NetLink *pLink)
{

	CriticalSystem_Status *pSystem;
	int i;

	static StatusReporting_Wrapper wrapper = {0};

	StructDeInit(parse_StatusReporting_Wrapper, &wrapper);
	memset(&wrapper, 0, sizeof(StatusReporting_Wrapper));

	ParserRecvStructSafe(parse_StatusReporting_Wrapper, pak, &wrapper);


	if (!stashFindPointer(sCriticalSystemsByName, wrapper.pMyName, &pSystem))
	{
		ContactAttempted(wrapper.pMyName);
		return;
	}

	pSystem->pNetLink = pLink;


	assert(pSystem);

	StructCopyAll(parse_StatusReporting_GenericServerStatus, &wrapper.status, &pSystem->lastStatus);

	pSystem->iLastContactTime = timeSecondsSince2000();
	pSystem->IP = linkGetIp(pLink);

	pSystem->iMyID = wrapper.iMyID;
	pSystem->eMyType = wrapper.eMyType;
	pSystem->iMyGenericMonitoringPort = wrapper.iMyGenericMonitoringPort;
	pSystem->iMyMainMonitoringPort = wrapper.iMyMainMonitoringPort;
	estrCopy2(&pSystem->pVersion, wrapper.pVersion);

	pSystem->pProductName = wrapper.pMyProduct;
	pSystem->pShortProductName = wrapper.pMyShortProduct;

	if (isLocalIp(pSystem->IP))
	{
		pSystem->IP = getHostLocalIp();
	}

	if (pSystem->eStatus != CRITSYSTEMSTATUS_DOWNTIME)
	{
		SetSystemStatus(pSystem, CRITSYSTEMSTATUS_RUNNING);
	}

	estrDestroy(&pSystem->pLink1);
	estrDestroy(&pSystem->pLink2);

	if (wrapper.iMyMainMonitoringPort)
	{
		estrPrintf(&pSystem->pLink1, "<a href=\"http://%s:%d\">Main Monitoring Link</a>",
			makeIpStr(linkGetIp(pLink)),wrapper.iMyMainMonitoringPort);

		if (wrapper.iMyGenericMonitoringPort)
		{
			estrPrintf(&pSystem->pLink2, "<a href=\"http://%s:%d\">Generic Monitoring Link</a>",
				makeIpStr(linkGetIp(pLink)),wrapper.iMyGenericMonitoringPort);
		}
	}
	else if (wrapper.iMyGenericMonitoringPort)
	{
		estrPrintf(&pSystem->pLink1, "<a href=\"http://%s:%d\">Main Monitoring Link</a>",
			makeIpStr(linkGetIp(pLink)),wrapper.iMyGenericMonitoringPort);
	}


	estrPrintf(&pSystem->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", makeIpStr(linkGetIp(pLink)));

	for (i = 0; i < ALERTLEVEL_COUNT; i++)
	{
		char **ppEString = NULL;
		switch (i)
		{
		case ALERTLEVEL_WARNING:
			ppEString = &pSystem->pWarningAlerts;
			break;

		case ALERTLEVEL_CRITICAL:
			ppEString = &pSystem->pCriticalAlerts;
			break;

		}

		if (ppEString)
		{

			if (wrapper.status.iNumAlerts[i])
			{
				estrPrintf(ppEString, "<a href=\"http://%s:%d/viewxpath?xpath=%s[%u].globobj.alerts&svrfilter=me.level%%3D%d\">%d %s Alerts</a>",
					makeIpStr(linkGetIp(pLink)),
					wrapper.iMyGenericMonitoringPort ? wrapper.iMyGenericMonitoringPort : wrapper.iMyMainMonitoringPort,
					GlobalTypeToName(wrapper.eMyType), wrapper.iMyID, i, wrapper.status.iNumAlerts[i], StaticDefineIntRevLookup(enumAlertLevelEnum, i));
			}
			else
			{
				estrCopy2(ppEString, "");
			
			}
		}
	}

	objLogWithStruct(LOG_CRITICALSYSTEMS, wrapper.eMyType, wrapper.iMyID, 0, wrapper.pMyName, NULL, NULL, "StatusReport",
		NULL, &wrapper, parse_StatusReporting_Wrapper);

	if (!linkGetUserData(pLink))
	{
		linkSetUserData(pLink, (void*)allocAddString(wrapper.pMyName));
	}
}

char *DeHtmlIfyLink(char *pLink)
{
	static char *pTempString = NULL;
	estrCopy2(&pTempString, pLink);
	estrReplaceOccurrences(&pTempString, "<a href=\"", "");
	estrTruncateAtFirstOccurrence(&pTempString, '"');
	return pTempString;
}

//alerts are the same "issue" if they come from the same critical system, have the same
//container type and ID, and have the same alert key.



CriticalSystem_AlertIssue *GetAlertIssue(CriticalSystem_Status *pSystem, Alert *pAlert)
{
	AlertIssueKey key;
	CriticalSystem_AlertIssue *pRetVal = NULL;
	
	//this rigmarole is necessary to ensure that there aren't any parts of the struct that are unused due to alignment which end up
	//filled with random data, thus making the struct unusuable as a key
	memset(&key, 0, sizeof(AlertIssueKey));
	key.eType = pAlert->eContainerTypeOfObject;
	key.iID = pAlert->iIDOfObject;
	key.pKey = pAlert->pKey;


	if (!pSystem->alertIssuesTable)
	{
		pSystem->alertIssuesTable = stashTableCreateFixedSize(16, sizeof(AlertIssueKey));
	}
	else
	{
		if (stashFindPointer(pSystem->alertIssuesTable, &key, &pRetVal))
		{
			//do nothing... but will set the exptime and return later on
		}		
	}

	if (!pRetVal)
	{
		pRetVal = StructCreate(parse_CriticalSystem_AlertIssue);
		pRetVal->iIssueNum = GetNextIssueNumber();


		StructCopy(parse_AlertIssueKey, &key, &pRetVal->key, 0, 0, 0);	
		stashAddPointer(pSystem->alertIssuesTable, &pRetVal->key, pRetVal, false);
	}

	pRetVal->iExpTime = timeSecondsSince2000() + ALERT_ISSUE_EXP_TIME;	
	return pRetVal;

}

//alerts now have machine name built in, but didn't used to, so setting this up for backward compatibility
void FindMachineNameFromAlert(Alert *pAlert, char **ppMachineName)
{
	char **ppWordsInString = NULL;
	int iNumWords;

	if (pAlert->pMachineName && pAlert->pMachineName[0])
	{
		estrCopy2(ppMachineName, pAlert->pMachineName);
		return;
	}

	DivideString(pAlert->pString, " ", &ppWordsInString, 0);
	iNumWords = eaSize(&ppWordsInString);


	if (iNumWords > 2 && stricmp(ppWordsInString[iNumWords - 2], "on") == 0)
	{
		estrPrintf(ppMachineName, "%s", ppWordsInString[iNumWords - 1]);
		eaDestroyEx(&ppWordsInString, NULL);
		return;
	}

	estrPrintf(ppMachineName, "unknown");
	eaDestroyEx(&ppWordsInString, NULL);
	return;

}


void AlertThrottler_TimedCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char *pTimedKeyString = (char*)userData;

	char *pBar = strchr(pTimedKeyString, '|');
	CriticalSystem_Status *pSystem;
	CriticalSystem_AlertThrottler *pThrottler;

	char *pNewShortSubject = NULL;
	char *pNewShortBody = NULL;
	char *pNewLongSubject = NULL;
	char *pNewLongBody = NULL;
	char *pDuration = NULL;
	char emailListName[256];
	char **ppLists = NULL;

	assert(piThrottlingStages);

	assert(pBar);
	*pBar = 0;

	if (!stashFindPointer(sCriticalSystemsByName, pTimedKeyString, &pSystem))
	{
		estrDestroy(&pTimedKeyString);
		return;
	}

	if (!pSystem->alertThrottlesTable)
	{
		estrDestroy(&pTimedKeyString);
		return;
	}

	if (!stashFindPointer(pSystem->alertThrottlesTable, pBar + 1, &pThrottler))
	{
		estrDestroy(&pTimedKeyString);
		return;
	}



	if (pThrottler->iCurCount == 0)
	{
		estrDestroy(&pTimedKeyString);
		stashRemovePointer(pSystem->alertThrottlesTable, pThrottler->pKey, NULL);
		StructDestroy(parse_CriticalSystem_AlertThrottler, pThrottler);
		return;
	}

	*pBar = '|';

	estrStackCreate(&pNewShortSubject);
	estrStackCreate(&pNewShortBody);
	estrStackCreate(&pNewLongSubject);
	estrStackCreate(&pNewLongBody);
	estrStackCreate(&pDuration);
	
	estrPrintf(&pNewShortSubject, "%s alerts (%d grouped)!", pSystem->pConfig->pName, pThrottler->iCurCount);
	timeSecondsDurationToPrettyEString(piThrottlingStages[pThrottler->iCurStage], &pDuration);
	estrPrintf(&pNewShortBody, "\"%s\" happened %d times in the last %s",
		pThrottler->shortBody, pThrottler->iCurCount, pDuration);


	if (bOldEmailSystem)
	{
		sprintf(emailListName, "Alerts_%s_%s@" ORGANIZATION_DOMAIN, StaticDefineIntRevLookup(enumAlertCategoryEnum, pThrottler->eCategory),
			StaticDefineIntRevLookup(enumAlertLevelEnum, pThrottler->eLevel));

		SendEmailToList(emailListName, pNewShortSubject, pNewShortBody, "CriticalAlerts@" ORGANIZATION_DOMAIN);
	}

	estrPrintf(&pNewLongSubject, "%s alerts (%d grouped) - %s!", pSystem->pConfig->pName, pThrottler->iCurCount, pThrottler->pKey);

	estrPrintf(&pNewLongBody, "Alert %s happened %d times in the last %s. Full text:\n%s\n",
		pThrottler->pKey, pThrottler->iCurCount, pDuration, pThrottler->pCurGlomString);

	estrDestroy(&pThrottler->pCurGlomString);

	if (bOldEmailSystem)
	{
		sprintf(emailListName, "FullAlerts_%s_%s@" ORGANIZATION_DOMAIN, StaticDefineIntRevLookup(enumAlertCategoryEnum, pThrottler->eCategory),
			StaticDefineIntRevLookup(enumAlertLevelEnum, pThrottler->eLevel));

		GetAllEmailListsForSystem(&ppLists, pSystem, false);
		eaPush(&ppLists, emailListName);

		SendEmailToLists(&ppLists, pNewLongSubject, pNewLongBody, "CriticalAlerts@" ORGANIZATION_DOMAIN);
		eaDestroy(&ppLists);
	}

	if (bNewEmailSystem)
	{
		SendEmailWithMailingLists(pSystem, EMAILTYPE_ALERT, pThrottler->eCategory, pThrottler->eLevel, pThrottler->pAlertKey, pThrottler->eAlertServerType, pThrottler->eAlertObjType, pThrottler->iCurCount, pNewLongSubject, pNewShortSubject,
			pNewLongBody, pNewShortBody, NULL);
	}

	estrDestroy(&pNewShortSubject);
	estrDestroy(&pNewShortBody);
	estrDestroy(&pNewLongSubject);
	estrDestroy(&pNewLongBody);
	estrDestroy(&pDuration);

	pThrottler->iCurCount = 0;
	if (pThrottler->iCurStage < ea32Size(&piThrottlingStages) - 1)
	{
		pThrottler->iCurStage++;
	}

	TimedCallback_Run(AlertThrottler_TimedCallback, pTimedKeyString, piThrottlingStages[pThrottler->iCurStage]);
}


void DoAlertThrottling(CriticalSystem_Status *pSystem, Alert *pAlert, 
	char *pShortSubject, char *pShortBody, char *pLongSubject, char *pLongBody)
{
	char key[256];
	CriticalSystem_AlertThrottler *pThrottler;

	sprintf(key, "%s_%s_%u", pAlert->pKey, GlobalTypeToName(pAlert->eContainerTypeOfServer), pAlert->iErrorID);
	
	if (!piThrottlingStages)
	{
		SetDefaultThrottlingStages();
	}

	assert(piThrottlingStages);

	if (!pSystem->alertThrottlesTable)
	{
		pSystem->alertThrottlesTable = stashTableCreateWithStringKeys(16, StashDefault);
	}

	if (stashFindPointer(pSystem->alertThrottlesTable, key, &pThrottler))
	{
		pThrottler->iCurCount++;
		estrConcatf(&pThrottler->pCurGlomString, "--------------------------------------\n%s\n", pLongBody);
		if (estrLength(&pThrottler->pCurGlomString) > 500000)
		{
			estrSetSize(&pThrottler->pCurGlomString, 500000);
		}	

		strcpy_trunc(pThrottler->shortBody, pShortBody);
	}
	else
	{
		char emailListName[256];
		char **ppLists = NULL;

		char *pTimedCallbackKeyString = NULL;

		if (bOldEmailSystem)
		{
			sprintf(emailListName, "Alerts_%s_%s@" ORGANIZATION_DOMAIN, StaticDefineIntRevLookup(enumAlertCategoryEnum, pAlert->eCategory),
				StaticDefineIntRevLookup(enumAlertLevelEnum, pAlert->eLevel));

			SendEmailToList(emailListName, pShortSubject, pShortBody, "CriticalAlerts@" ORGANIZATION_DOMAIN);

			sprintf(emailListName, "FullAlerts_%s_%s@" ORGANIZATION_DOMAIN, StaticDefineIntRevLookup(enumAlertCategoryEnum, pAlert->eCategory),
			StaticDefineIntRevLookup(enumAlertLevelEnum, pAlert->eLevel));

			GetAllEmailListsForSystem(&ppLists, pSystem, false);
			eaPush(&ppLists, emailListName);
		
			SendEmailToLists(&ppLists, pLongSubject, pLongBody, "CriticalAlerts@" ORGANIZATION_DOMAIN);
			eaDestroy(&ppLists);
		}

		if (bNewEmailSystem)
		{
			SendEmailWithMailingLists(pSystem, EMAILTYPE_ALERT,
				pAlert->eCategory, pAlert->eLevel, pAlert->pKey, pAlert->eContainerTypeOfServer, pAlert->eContainerTypeOfObject, 1, pLongSubject, pShortSubject, pLongBody,
				pShortBody, NULL);
		}


		pThrottler = StructCreate(parse_CriticalSystem_AlertThrottler);
		pThrottler->eCategory = pAlert->eCategory;
		pThrottler->eLevel = pAlert->eLevel;
		pThrottler->pSystemName = strdup(pSystem->pName_Internal);
		pThrottler->pKey = (char*)allocAddString(key);

		//shortbody limited to 250 chars
		strcpy_trunc(pThrottler->shortBody, pShortBody);

		pThrottler->pAlertKey = pAlert->pKey;
		pThrottler->eAlertServerType = pAlert->eContainerTypeOfServer;
		pThrottler->eAlertObjType = pAlert->eContainerTypeOfObject;
		
		stashAddPointer(pSystem->alertThrottlesTable, pThrottler->pKey, pThrottler, false);

		estrPrintf(&pTimedCallbackKeyString, "%s|%s", pSystem->pName_Internal, pThrottler->pKey);
		
		TimedCallback_Run(AlertThrottler_TimedCallback, pTimedCallbackKeyString, piThrottlingStages[0]);
	}
}

bool AllAlertsSuppressed(CriticalSystem_Status *pStatus)
{
	static const char *pAllPooled = NULL;
	int i;
		
	if (!pAllPooled)
	{
		pAllPooled = allocAddString("ALL");
	}

	for (i=0; i < eaSize(&pStatus->ppAlertSuppressions); i++)
	{
		if (pStatus->ppAlertSuppressions[i]->pAlertKey == pAllPooled)
		{
			return true;
		}
	}

	return false;
}

bool AlertIsSuppressed(CriticalSystem_Status *pStatus, Alert *pAlert)
{
	int i;

	if (AllAlertsSuppressed(pStatus))
	{
		return true;
	}

	for (i=0; i < eaSize(&pStatus->ppAlertSuppressions); i++)
	{
		if (pStatus->ppAlertSuppressions[i]->pAlertKey == pAlert->pKey &&
			(pStatus->ppAlertSuppressions[i]->eServerType == pAlert->eContainerTypeOfObject ||
			pStatus->ppAlertSuppressions[i]->eServerType == pAlert->eContainerTypeOfServer))
		{
			return true;
		}
	}

	for (i=eaSize(&pStatus->pConfig->ppVersionTiedAlertSuppresions)-1; i >= 0; i--)
	{
		CriticalSystem_VersionTiedAlertSuppression *pSuppression;
		assert(pStatus->pConfig->ppVersionTiedAlertSuppresions);
		pSuppression = pStatus->pConfig->ppVersionTiedAlertSuppresions[i];
		if (pSuppression->pAlertKey == pAlert->pKey)
		{
			if (stricmp_safe(pSuppression->pVersionName, pStatus->pVersion) == 0)
			{
				return true;
			}
			else
			{
				StructDestroy(parse_CriticalSystem_VersionTiedAlertSuppression, pSuppression);
				eaRemoveFast(&pStatus->pConfig->ppVersionTiedAlertSuppresions, i);
				WriteOutGlobalConfig();
			}
		}
	}


	return false;
}

void HandleAlert(Packet *pak, NetLink *link)
{
	Alert alert = {0};
	CriticalSystem_Status *pSystem;
	char *pShortSubjectString = NULL;
	char *pShortBodyString = NULL;
	char *pLongSubjectString = NULL;
	char *pLongBodyString = NULL;
	char *pFullAlertStructString = NULL;

	char *pName = pktGetStringTemp(pak);

	CriticalSystem_AlertIssue *pIssue;

//	char emailListName[256];

	static char *pMachineName = NULL;

	//must ALWAYS do recvStructSafe even if we throw it out immediately, in case this is the
	//first struct of this type sent over this link
	ParserRecvStructSafe(parse_Alert, pak, &alert);


	if (!stashFindPointer(sCriticalSystemsByName, pName, &pSystem))
	{
		StructDeInit(parse_Alert, &alert);
		return;
	}

	if (AlertIsSuppressed(pSystem, &alert))
	{
		ReportAlertToMailingLists(pSystem, alert.pKey, alert.eLevel, true);	
	
		StructDeInit(parse_Alert, &alert);
		return;
	}

	//we got this from someone other than the critical system itself... therefore it is
	//presumably from sendalert, and we should send it down to the system
	if (pSystem->pNetLink && pSystem->pNetLink != link && !linkGetUserData(link))
	{
		Packet *pOutPack = pktCreate(pSystem->pNetLink ,FROM_CONTROLLERTRACKER_TO_CRITICALSYSTEM_HERE_IS_ALERT_FROM_SOMEONE_ELSE_ABOUT_YOU);
		alert.bWasSentByCriticalSystems = true;
		ParserSendStructSafe(parse_Alert, pOutPack, &alert);
		pktSend(&pOutPack);
	}

	ReportAlertToMailingLists(pSystem, alert.pKey, alert.eLevel, false);	

	FindMachineNameFromAlert(&alert, &pMachineName);

	AlertTrackers_TrackAlert(&alert, pSystem);

	pIssue = GetAlertIssue(pSystem, &alert);

	estrStackCreate(&pShortSubjectString);
	estrStackCreate(&pShortBodyString);

	estrStackCreate(&pLongSubjectString);
	estrStackCreate(&pLongBodyString);

	estrStackCreate(&pFullAlertStructString);

	estrPrintf(&pShortSubjectString, "%s alert!", pSystem->pConfig->pName);
	estrPrintf(&pShortBodyString, "\"%s\": (%s %s)", 
		alert.pString, StaticDefineIntRevLookup(enumAlertLevelEnum, alert.eLevel),
		StaticDefineIntRevLookup(enumAlertCategoryEnum, alert.eCategory));



	estrPrintf(&pLongBodyString, "A %s %s alert has occurred on %s. Alert string: \"%s\".\n\n",
		StaticDefineIntRevLookup(enumAlertLevelEnum, alert.eLevel),
		StaticDefineIntRevLookup(enumAlertCategoryEnum, alert.eCategory),
		pSystem->pConfig->pName,
		alert.pString);


	if (alert.pMapName && alert.pMapName[0])
	{
		estrConcatf(&pLongBodyString, "MapName: %s\n\n", alert.pMapName);
	}


	estrConcatf(&pLongBodyString, "Main monitoring link: %s\n\n", DeHtmlIfyLink(pSystem->pLink1));

	estrConcatf(&pLongBodyString, "Direct link to alert page: http://%s:%d/viewxpath?xpath=%s[%u].globobj.alerts&svrfilter=me.level=%d\n\n",
		makeIpStr(linkGetIp(link)),
		pSystem->iMyGenericMonitoringPort ? pSystem->iMyGenericMonitoringPort : pSystem->iMyMainMonitoringPort,
		GlobalTypeToName(pSystem->eMyType), pSystem->iMyID, alert.eLevel);


	if (alert.pVNC)
	{
		estrConcatf(&pLongBodyString, "VNC: %s\n\n", DeHtmlIfyLink(alert.pVNC));
	}

	if (alert.pErrorLink)
	{
		estrConcatf(&pLongBodyString, "Errortracker: %s\n\n", DeHtmlIfyLink(alert.pErrorLink));
	}

	estrConcatf(&pLongBodyString, "MachineName: %s\n\n", pMachineName);

	estrConcatf(&pLongBodyString, "UniqueID:%c%d\n\n", GetIssueNumPrefixChar(), pIssue->iIssueNum);

	if (pSystem->pProductName && pSystem->pProductName[0])
	{
		estrConcatf(&pLongBodyString, "Product: %s\n", pSystem->pProductName);
	}

	//for shards, put the container type of the server in the subject line, otherwise put the key
	if (pSystem->eMyType == GLOBALTYPE_CONTROLLER)
	{
		char shortAlertKey[27];
		strcpy_trunc(shortAlertKey, alert.pKey);
		estrPrintf(&pLongSubjectString, "%s %s: %s", pShortSubjectString, GlobalTypeToName(alert.eContainerTypeOfServer), shortAlertKey);
	}
	else
	{

		estrPrintf(&pLongSubjectString, "%s %s", pShortSubjectString, alert.pKey);
	}


	//for now trying it out with the long and short subjects being identical
	estrCopy(&pShortSubjectString, &pLongSubjectString);


	ParserWriteText(&pFullAlertStructString, parse_Alert, &alert, 0, 0, 0);

	estrConcatf(&pLongBodyString, "\n\n\nFull Alert Struct:\n%s", pFullAlertStructString);




	if (strStartsWith(alert.pString, "AUTO_EMAIL:"))
	{
		char *pAddress = NULL;
	
		estrCopy2(&pAddress, alert.pString + 11);

		estrTrimLeadingAndTrailingWhitespace(&pAddress);
		estrTruncateAtFirstWhitespace(&pAddress);


		SendEmailToList(pAddress, pLongSubjectString, pLongBodyString, "CriticalAlerts@" ORGANIZATION_DOMAIN);

		estrDestroy(&pAddress);
	}
	else
	{
		DoAlertThrottling(pSystem, &alert, pShortSubjectString, pShortBodyString, pLongSubjectString, pLongBodyString);
	}

	estrDestroy(&pShortSubjectString);
	estrDestroy(&pShortBodyString);
	estrDestroy(&pLongSubjectString);
	estrDestroy(&pLongBodyString);
	estrDestroy(&pFullAlertStructString);

	

	StructDeInit(parse_Alert, &alert);
}




void CriticalSystemHandleMsg(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	switch(cmd)
	{
	case FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_GENERIC_STATUS:
		HandleGenericStatus(pak, link);
		break;

	case FROM_CRITICALSYSTEM_TO_CONTROLLERTRACKER_HERE_IS_ALERT:
		HandleAlert(pak, link);
		break;

	default:
		break;
	}
}

AUTO_STRUCT;
typedef struct CriticalSystems_HtmlStruct
{
	char *pCategories; AST(ESTRING, FORMATSTRING(html=1))
	char *pAlertTrackers; AST(ESTRING, FORMATSTRING(html=1, HTML_NO_HEADER=1))
	CriticalSystem_Status **ppSystems;
	AST_COMMAND("Add New Critical System", "CriticalSystems_BeginTrackingSystem $STRING(Name of Critical System) $INT(Delay time before failure)")
	AST_COMMAND("Reload mailing lists", "ReloadMailingLists")
	AST_COMMAND("Send and reset alert summaries", "SendAlertSummariesNow")
	char *pModifyGlobalEmailRecipients; AST(ESTRING, FORMATSTRING(command=1))
	char *pModifySystemDownEmailRecipients; AST(ESTRING, FORMATSTRING(command=1))
	char *pBeginDowntimeAll; AST(ESTRING, FORMATSTRING(command=1))
	char *pEndDowntimeAll; AST(ESTRING, FORMATSTRING(command=1))
	char *pAddAndRemoveCategoryEmailRecipients; AST(ESTRING, FORMATSTRING(command=1))
	CriticalSystem_SystemDownTime **ppRecentSystemDownTimes;
	char *pFavIcon; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
} CriticalSystems_HtmlStruct;


bool CriticalSystems_CustomXpathHttpFunc(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	static CriticalSystems_HtmlStruct *spHtmlStruct = NULL;
	const char *pCategory = NULL;
	int i, j;
	char *pNextDot;
	static const char *spAll = NULL;
	bool bAll = false;
	const char **ppAllCategoryNames = NULL;
	bool bAtLeastOneFailed = false;
	bool bRetVal;

	PERFINFO_AUTO_START_FUNC();

	if (!spAll)
	{
		spAll = allocAddString("all");
	}

	if (pLocalXPath[0] == 0)
	{
		estrPrintf(&pStructInfo->pRedirectString, "%s.CriticalSystems.all", LinkToThisServer());
	
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (pLocalXPath[0] != '.')
	{
		GetMessageForHttpXpath("Expected .category or .all after .CriticalSystems", pStructInfo, 1);
		PERFINFO_AUTO_STOP();
		return true;
	}

	
	pNextDot = strchr(pLocalXPath + 1, '.');
	if (pNextDot)
	{
		*pNextDot = 0;
		pCategory = allocAddString(pLocalXPath + 1);
		*pNextDot = '.';
		pLocalXPath = pNextDot;
	}
	else
	{
		pCategory = allocAddString(pLocalXPath + 1);
		pLocalXPath = "";
	}

	if (pCategory == spAll)
	{
		bAll = true;
	}

	if (!spHtmlStruct)
	{
		spHtmlStruct = StructCreate(parse_CriticalSystems_HtmlStruct);
	}
	else
	{
		eaDestroy(&spHtmlStruct->ppSystems);
		spHtmlStruct->ppRecentSystemDownTimes = NULL;
		StructReset(parse_CriticalSystems_HtmlStruct, spHtmlStruct);
	}


	for (i=0; i < eaSize(&ppCriticalSystems); i++)
	{
		if (pCategory == spAll || eaFind(&ppCriticalSystems[i]->pConfig->ppCategories, pCategory) != -1)
		{
			eaPush(&spHtmlStruct->ppSystems, ppCriticalSystems[i]);

			if (ppCriticalSystems[i]->eStatus ==  CRITSYSTEMSTATUS_FAILED)
			{
				bAtLeastOneFailed = true;
			}

			if (bAll)
			{
				for (j=0; j < eaSize(&ppCriticalSystems[i]->pConfig->ppCategories); j++)
				{
					eaPushUnique(&ppAllCategoryNames, ppCriticalSystems[i]->pConfig->ppCategories[j]);
				}
			}
		}
	}


	estrPrintf(&spHtmlStruct->pAlertTrackers, "<span><a href=\"%s.globObj.Alerttrackers&svrfilter=me.level%%3D%d\">Critical Alert Trackers</a></span>  ", LinkToThisServer(), ALERTLEVEL_CRITICAL);
	estrConcatf(&spHtmlStruct->pAlertTrackers, "<span><a href=\"%s.globObj.Alerttrackers&svrfilter=me.level%%3D%d\">Warning Alert Trackers</a></span>", LinkToThisServer(), ALERTLEVEL_WARNING);

	if (bAll)
	{
		for (i = 0; i < eaSize(&ppAllCategoryNames); i++)
		{
			estrConcatf(&spHtmlStruct->pCategories, "<span><a href=\"%s.CriticalSystems.%s\">%s</a></span>  ",
				LinkToThisServer(), ppAllCategoryNames[i], ppAllCategoryNames[i]);
		}

		eaDestroy(&ppAllCategoryNames);
	}
	else
	{
		CriticalSystem_CategoryConfig *pConfig = FindCategoryConfig(pCategory);

		estrPrintf(&spHtmlStruct->pBeginDowntimeAll, "BeginCategoryDownTime %s $CONFIRM(Begin downtime for all systems in category %s?) $NORETURN", pCategory, pCategory);
		estrPrintf(&spHtmlStruct->pEndDowntimeAll, "EndCategoryDownTime %s $CONFIRM(End downtime for all systems in category %s?) $NORETURN", pCategory, pCategory);

		estrPrintf(&spHtmlStruct->pAddAndRemoveCategoryEmailRecipients, "AddAndRemoveCategoryEmailRecipients %s $STRING(Who to add) $STRING(Who to remove) $CONFIRM(Add and remove email recipients for %s? Current list: %s)", 
			pCategory, pCategory, (pConfig && pConfig->pEmailRecipients && pConfig->pEmailRecipients[0]) ? pConfig->pEmailRecipients : "empty");
	}

	estrPrintf(&spHtmlStruct->pModifyGlobalEmailRecipients, "AddAndRemoveGlobalEmailRecipients $STRING(Addresses to add, separated by spaces or commas. @" ORGANIZATION_DOMAIN " is default) ");

	if (estrLength(&gCriticalSystemsGlobalConfig.pGlobalEmailRecipients) == 0)
	{
		estrConcatf(&spHtmlStruct->pModifyGlobalEmailRecipients, "\"\" $CONFIRM(There are currently no email recipients.)");
	}
	else
	{
		
		estrConcatf(&spHtmlStruct->pModifyGlobalEmailRecipients, "$STRING(Address to remove) $CONFIRM(Current addresses: %s)",
			gCriticalSystemsGlobalConfig.pGlobalEmailRecipients);
	}


	estrPrintf(&spHtmlStruct->pModifySystemDownEmailRecipients, "AddAndRemoveSystemDownEmailRecipients $STRING(Addresses to add, separated by spaces or commas. @" ORGANIZATION_DOMAIN " is default) ");

	if (estrLength(&gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients) == 0)
	{
		estrConcatf(&spHtmlStruct->pModifySystemDownEmailRecipients, "\"\" $CONFIRM(There are currently no email recipients.)");
	}
	else
	{
		
		estrConcatf(&spHtmlStruct->pModifySystemDownEmailRecipients, "$STRING(Address to remove) $CONFIRM(Current addresses: %s)",
			gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients);
	}

	spHtmlStruct->ppRecentSystemDownTimes = gCriticalSystemsGlobalConfig.ppRecentSystemDownTimes;

	estrPrintf(&spHtmlStruct->pFavIcon, "<link rel=\"shortcut icon\" href=\"/static/%s.ico\" type=\"image/x-icon\" />", 
		bAtLeastOneFailed ? "bad" : "good");

	bRetVal = ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList, spHtmlStruct, parse_CriticalSystems_HtmlStruct, iAccessLevel, 0, pStructInfo, eFlags | GETHTTPFLAG_STATIC_STRUCT_OK_FOR_LOCAL_RETURN);

	PERFINFO_AUTO_STOP();

	return bRetVal;


}

U32 FindLastUpTime(void)
{
	char *pBuffer;
	int iSize;
	U32 iRetVal = 0;

	pBuffer = fileAlloc(STAY_ALIVE_FILE, &iSize);

	if (!pBuffer)
	{
		return 0;
	}

	sscanf(pBuffer, "%u", &iRetVal);

	free(pBuffer);

	return iRetVal;
}

void CriticalSystemDisconnectCB(NetLink* link,char *pCritSysName)
{
	static char *pDisconnectString = NULL;


	CriticalSystem_Status *pSystem;


	linkGetDisconnectReason(link, &pDisconnectString);

	printf("Disconnected from critSystem %s: %s\n", pCritSysName, pDisconnectString);
	log_printf(LOG_MISC, "Disconnected from critSystem %s: %s\n", pCritSysName, pDisconnectString);


	if (stashFindPointer(sCriticalSystemsByName, pCritSysName, &pSystem))
	{
		pSystem->iLastDisconnectTime = timeSecondsSince2000();	
		pSystem->pNetLink = NULL;
	}

}





void CriticalSystems_InitSystem(void)
{
	int i;
	U32 iLastUpTime;


	loadstart_printf("Trying to start listening for Critical Systems...");
	while (!commListen(commDefault(),LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT,
			CriticalSystemHandleMsg,NULL,
			CriticalSystemDisconnectCB,
			0))
	{
		Sleep(1);
	}

	loadend_printf("done");


	sCriticalSystemsByName = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);

	StructInit(parse_CriticalSystem_GlobalConfig, &gCriticalSystemsGlobalConfig);
	ParserReadTextFile(CRITICALSYSTEMS_CONFIG_FILENAME, parse_CriticalSystem_GlobalConfig, &gCriticalSystemsGlobalConfig, 0);

	for (i=0; i < eaSize(&gCriticalSystemsGlobalConfig.ppCriticalSystems); i++)
	{
		CreateAndAddCriticalSystemStatusFromConfig(gCriticalSystemsGlobalConfig.ppCriticalSystems[i]);
	}

	resRegisterDictionaryForStashTable("CriticalSystems", RESCATEGORY_OTHER, 0, sCriticalSystemsByName, parse_CriticalSystem_Status);

	RegisterCustomXPathDomain(".CriticalSystems", CriticalSystems_CustomXpathHttpFunc, NULL);

	iLastUpTime = FindLastUpTime();

	if (iLastUpTime)
	{
		CriticalSystem_SystemDownTime *pDownTime = StructCreate(parse_CriticalSystem_SystemDownTime);
		pDownTime->iTimeWentDown = iLastUpTime;
		pDownTime->iTimeRecovered = timeSecondsSince2000();

		if (eaSize(&gCriticalSystemsGlobalConfig.ppRecentSystemDownTimes) > MAX_DOWNTIMES_TO_SAVE)
		{
			StructDestroy(parse_CriticalSystem_SystemDownTime, eaPop(&gCriticalSystemsGlobalConfig.ppRecentSystemDownTimes));
		}
		
		eaInsert(&gCriticalSystemsGlobalConfig.ppRecentSystemDownTimes, pDownTime, 0);

		WriteOutGlobalConfig();
	}

	if (pCriticalSystemToCreateAtStartup)
	{
		//does nothing if it already exists
		CriticalSystems_BeginTrackingSystem(pCriticalSystemToCreateAtStartup, 30);
	}
}

AUTO_COMMAND;
char *CriticalSystems_BeginTrackingSystem(char *pSystemName, int iTimeOut)
{
	static char sRetString[256];
	CriticalSystem_Config *pConfig;
	CriticalSystemWannaBe *pWannaBe;

	if (stashFindPointer(sCriticalSystemsByName, pSystemName, NULL))
	{
		sprintf(sRetString, "ERROR: A critical systen named %s already exists", pSystemName);
		return sRetString;
	}

	if (stashRemovePointer(sCriticalSystemWannaBes, pSystemName, &pWannaBe))
	{
		StructDestroy(parse_CriticalSystemWannaBe, pWannaBe);
	}

	pConfig = StructCreate(parse_CriticalSystem_Config);
	pConfig->pName = strdup(pSystemName);
	pConfig->iMaxStallTimeBeforeAssumedDeath = iTimeOut;
	pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath = MIN(iTimeOut / 4, 15);

	eaPush(&gCriticalSystemsGlobalConfig.ppCriticalSystems, pConfig);
	CreateAndAddCriticalSystemStatusFromConfig(pConfig);

	WriteOutGlobalConfig();

	sprintf(sRetString, "Critical system %s added", pSystemName);
	return sRetString;
}

AUTO_COMMAND;
void EndDownTime(char *pSystemName)
{
	CriticalSystem_Status *pSystem;
	U32 iCurTime = timeSecondsSince2000();
	int iInactivityTime;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	iInactivityTime = iCurTime - pSystem->iLastContactTime;

	if (pSystem->iLastContactTime == 0)
	{
		SetSystemStatus(pSystem, CRITSYSTEMSTATUS_STARTUP_NEVER_CONNECTED);
		return;
	}

	if (iInactivityTime > pSystem->pConfig->iMaxStallTimeBeforeAssumedDeath)
	{
		SetSystemStatus(pSystem, CRITSYSTEMSTATUS_FAILED);
		return;
	}

	if (pSystem->pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath && (pSystem->iLastDisconnectTime > pSystem->iLastContactTime) && iInactivityTime > pSystem->pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath)
	{
		SetSystemStatus(pSystem, CRITSYSTEMSTATUS_FAILED);
		return;
	}


	if (iInactivityTime > STATUS_REPORTING_INTERVAL * 2)
	{
		SetSystemStatus(pSystem, CRITSYSTEMSTATUS_STALLED);
		return;
	}

	SetSystemStatus(pSystem, CRITSYSTEMSTATUS_RUNNING);
}

void EndDownTimeCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	CriticalSystem_Status *pSystem;


	if (!stashFindPointer(sCriticalSystemsByName, (char*)userData, &pSystem))
	{
		return;
	}
	
	pSystem->pEndDowntimeCB = NULL;

	EndDownTime((char*)userData);
}


AUTO_COMMAND;
void BeginDownTime(char *pSystemName, int iNumMinutes)
{
	CriticalSystem_Status *pSystem;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	SetSystemStatus(pSystem, CRITSYSTEMSTATUS_DOWNTIME);
	pSystem->iTimeBeganDownTime = timeSecondsSince2000();
	pSystem->iNextDownTimeAlertTime = pSystem->iTimeBeganDownTime + TIME_IN_DOWNTIME_BEFORE_ALERT;

	if (iNumMinutes)
	{
		pSystem->pEndDowntimeCB = TimedCallback_Run(EndDownTimeCB, (void*)allocAddString(pSystemName), iNumMinutes * 60.0f);
	}
}



AUTO_COMMAND;
void BeginCategoryDowntime(char *pCatName)
{
	int i;
	const char *pAllocedCatName = allocAddString(pCatName);

	for (i=0; i < eaSize(&ppCriticalSystems); i++)
	{
		if (eaFind(&ppCriticalSystems[i]->pConfig->ppCategories, pAllocedCatName) != -1)
		{
			BeginDownTime(ppCriticalSystems[i]->pConfig->pName, 0);
		}
	}
}

AUTO_COMMAND;
void EndCategoryDowntime(char *pCatName)
{
	int i;
	const char *pAllocedCatName = allocAddString(pCatName);

	for (i=0; i < eaSize(&ppCriticalSystems); i++)
	{
		if (eaFind(&ppCriticalSystems[i]->pConfig->ppCategories, pAllocedCatName) != -1)
		{
			EndDownTime(ppCriticalSystems[i]->pConfig->pName);
		}
	}
}

void RemoveSystem(CriticalSystem_Status *pSystem)
{

	char **ppLists = NULL;
	char *pSubjectString = NULL;
	char *pBodyString = NULL;

	GetNewIssueNumForSystemIfNone(pSystem);

	estrPrintf(&pSubjectString, "OMG OMG %s is BEING REMOVED", pSystem->pConfig->pName);
	estrPrintf(&pBodyString, "%s is NO LONGER a critical system. No further messages will be sent. Noah is convined that people won't understand what this means if we do not use SCARY SCARY LANGUAGE. He says, and I quote, that if you did this wrong HE WILL CONSUME YOUR SOUL.\n\nIf this bothers you, please go talk to someone appropriaten\n\nUniqueID: %c%d\n", pSystem->pConfig->pName, GetIssueNumPrefixChar(), pSystem->iMainIssue);

	if (bOldEmailSystem)
	{
		GetAllEmailListsForSystem(&ppLists, pSystem, true);
		SendEmailToLists(&ppLists, pSubjectString, pBodyString, "CriticalAlerts@" ORGANIZATION_DOMAIN);
	}

	if (bNewEmailSystem)
	{
		SendEmailWithMailingLists(pSystem, EMAILTYPE_BEINGREMOVED,
			0, 0, NULL,  GLOBALTYPE_NONE, GLOBALTYPE_NONE, 0, pSubjectString, pSubjectString, pBodyString,
			pBodyString, NULL);
	}

	eaDestroy(&ppLists);
	estrDestroy(&pSubjectString);
	estrDestroy(&pBodyString);

	


	if (pSystem->alertIssuesTable)
	{
		stashTableDestroyStruct(pSystem->alertIssuesTable, NULL, parse_CriticalSystem_AlertIssue);
	}

	if (pSystem->alertThrottlesTable)
	{
		stashTableDestroyStruct(pSystem->alertThrottlesTable, NULL, parse_CriticalSystem_AlertThrottler);
	}


	eaFindAndRemove(&gCriticalSystemsGlobalConfig.ppCriticalSystems, pSystem->pConfig);
	eaFindAndRemove(&ppCriticalSystems, pSystem);
	stashRemovePointer(sCriticalSystemsByName, pSystem->pConfig->pName, NULL);

	StructDestroy(parse_CriticalSystem_Config, pSystem->pConfig);
	StructDestroy(parse_CriticalSystem_Status, pSystem);

	WriteOutGlobalConfig();
}


//for each string in the list, if it doesn't have an @, prepend @ORGANIZATION_DOMAIN
void MakeIntoEmailAddresses(char ***pppList)
{
	int i;

	for (i=0; i < eaSize(pppList); i++)
	{
		if (!strchr((*pppList)[i], '@'))
		{
			char *pTempString = NULL;
			estrStackCreate(&pTempString);
			estrPrintf(&pTempString, "%s@" ORGANIZATION_DOMAIN, (*pppList)[i]);
			free((*pppList)[i]);
			(*pppList)[i] = strdup(pTempString);
			estrDestroy(&pTempString);
		}
	}
}

AUTO_COMMAND;
void AddCategory(char *pSystemName, char *pCategory)
{
	const char *pPooledCatName;
	CriticalSystem_Status *pSystem;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	pPooledCatName = allocAddString(pCategory);

	if (eaFind(&pSystem->pConfig->ppCategories, pPooledCatName) != -1)
	{
		return;
	}

	eaPush(&pSystem->pConfig->ppCategories, pPooledCatName);
	WriteOutGlobalConfig();
}

AUTO_COMMAND;
void RemoveCategory(char *pSystemName, char *pCategory)
{
	const char *pPooledCatName;
	CriticalSystem_Status *pSystem;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	pPooledCatName = allocAddString(pCategory);

	eaFindAndRemove(&pSystem->pConfig->ppCategories, pPooledCatName);

	WriteOutGlobalConfig();
}

void UpdateAlertSuppressionStatus(CriticalSystem_Status *pSystem)
{
	if (AllAlertsSuppressed(pSystem))
	{
		pSystem->eAlertSuppresseds = CRITSYSTEMALERTS_ALL;
	}
	else if (eaSize(&pSystem->ppAlertSuppressions))
	{
		pSystem->eAlertSuppresseds = CRITSYSTEMALERTS_SOME;
	}
	else
	{
		pSystem->eAlertSuppresseds = CRITSYSTEMALERTS_NONE;
	}
}
AUTO_COMMAND;
void RemoveVersionTiedAlertSuppression(char *pSystemName, char *pKey)
{
	CriticalSystem_Status *pSystem;
	int i;
	bool bRemovedOne = false;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	for (i=eaSize(&pSystem->pConfig->ppVersionTiedAlertSuppresions) - 1; i >= 0; i--)
	{
		if (stricmp(pSystem->pConfig->ppVersionTiedAlertSuppresions[i]->pAlertKey, pKey) == 0)
		{
			StructDestroy(parse_CriticalSystem_VersionTiedAlertSuppression, pSystem->pConfig->ppVersionTiedAlertSuppresions[i]);
			eaRemoveFast(&pSystem->pConfig->ppVersionTiedAlertSuppresions, i);
			bRemovedOne = true;
		}
	}

	WriteOutGlobalConfig();
}

AUTO_COMMAND;
char *AddVersionTiedAlertSuppression(char *pSystemName, char *pKey)
{
	static char *spRetVal = NULL;
	CriticalSystem_Status *pSystem;
	CriticalSystem_VersionTiedAlertSuppression *pSuppression;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return "Unknown critical system";
	}

	if (!(pSystem->pVersion && pSystem->pVersion[0]))
	{
		return "No version set for that system";
	}

	pSuppression = StructCreate(parse_CriticalSystem_VersionTiedAlertSuppression);

	pSuppression->pAlertKey = allocAddString(pKey);
	pSuppression->pSystemName = strdup(pSystemName);
	pSuppression->pVersionName = strdup(pSystem->pVersion);

	eaPush(&pSystem->pConfig->ppVersionTiedAlertSuppresions, pSuppression);

	WriteOutGlobalConfig();

	estrPrintf(&spRetVal, "%s will be suppressed for %s as long as its version is %s",
		pKey, pSystemName, pSystem->pVersion);
	return spRetVal;

}




AUTO_COMMAND;
char *AddAlertSuppression(char *pSystemName, char *pKey, char *pServerTypeName)
{
	static char retVal[256];
	CriticalSystem_Status *pSystem;
	GlobalType eType;
	CriticalSystem_AlertSuppression *pSuppression;

	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return "Unknown critical system";
	}

	eType = NameToGlobalType(pServerTypeName);

	if (!eType)
	{
		sprintf(retVal, "Unknown container type %s", pServerTypeName);
		return retVal;
	}

	pSuppression = StructCreate(parse_CriticalSystem_AlertSuppression);
	pSuppression->pAlertKey = allocAddString(pKey);
	pSuppression->eServerType = eType;
	pSuppression->pSystemName = strdup(pSystemName);
	pSuppression->iWarnTime = timeSecondsSince2000() + iAlertSuppressionWarnTimeHours * (60 * 60);
	pSuppression->iStartedTime = timeSecondsSince2000();

	eaPush(&pSystem->ppAlertSuppressions, pSuppression);

	UpdateAlertSuppressionStatus(pSystem);

	return "SUCCESS";
}

AUTO_COMMAND;
void RemoveAlertSuppression(char *pSystemName, char *pKey, char *pServerType)
{
	GlobalType eType;
	int i;
	CriticalSystem_Status *pSystem;
	const char *pPooledKey = allocAddString(pKey);


	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return;
	}

	eType = NameToGlobalType(pServerType);
	if (!eType)
	{
		return;
	}

	for (i=0; i < eaSize(&pSystem->ppAlertSuppressions); i++)
	{
		if (pSystem->ppAlertSuppressions[i]->pAlertKey == pPooledKey 
			&& pSystem->ppAlertSuppressions[i]->eServerType == eType)
		{
			StructDestroy(parse_CriticalSystem_AlertSuppression, pSystem->ppAlertSuppressions[i]);
			eaRemove(&pSystem->ppAlertSuppressions, i);

			UpdateAlertSuppressionStatus(pSystem);

			return;
		}
	}
}


AUTO_COMMAND;
char *ModifySystem(char *pSystemName, char *pDieString, int iNewDelayTime, int iNewDelayTimeWithDisconnect, int iNewPauseBeforeRestartingTime, char *pAddString, char *pRemoveString, char *pEsc1Email, char *pEsc2Email)
{
	CriticalSystem_Status *pSystem;
	static char *pRetString = NULL;
	bool bFailed = false;
	char **ppCurRecips = NULL;
	char **ppRecipsToAdd = NULL;
	char **ppRecipsToRemove = NULL;
	bool bRemoved = false;
	bool bClearEscEmails = false;

	if (!pEsc1Email[0])
	{
		pEsc1Email = NULL;
	}

	if (!pEsc2Email[0])
	{
		pEsc2Email = NULL;
	}

	if (stricmp_safe(pEsc1Email, "none") == 0)
	{
		bClearEscEmails = true;
	}
	else
	{
		if (!!pEsc1Email != !!pEsc2Email)
		{
			return ("Escalation level 1 and 2 email addresses must both be set or both be clear");
		}
	}

	estrPrintf(&pRetString, "Succeeded");


	if (!stashFindPointer(sCriticalSystemsByName, pSystemName, &pSystem))
	{
		return "Couldn't find system";
	}



	//doing Bruce's fancy while trick so that we can break out

	do
	{

		if (stricmp(pDieString, "remove") == 0)
		{
			RemoveSystem(pSystem);
			bRemoved = true;
			break;
		}

		if (iNewDelayTime < 0)
		{
			estrPrintf(&pRetString, "new delay time can't be negative");
			bFailed = true;
			break;
		}

		if (iNewDelayTimeWithDisconnect < 0)
		{
			estrPrintf(&pRetString, "new delay with disconnect time can't be negative");
			bFailed = true;
			break;
		}

		if (iNewPauseBeforeRestartingTime < -1)
		{
			estrPrintf(&pRetString, "new pause time before resarting can't be negative");
			bFailed = true;
			break;
		}

		ExtractAlphaNumTokensFromStringEx(pSystem->pConfig->pEmailRecipients, &ppCurRecips, "@.");
		ExtractAlphaNumTokensFromStringEx(pAddString, &ppRecipsToAdd, "@.");
		ExtractAlphaNumTokensFromStringEx(pRemoveString, &ppRecipsToRemove, "@.");

		MakeIntoEmailAddresses(&ppRecipsToAdd);
		MakeIntoEmailAddresses(&ppRecipsToRemove);
			
		if (!AddAndRemoveStrings(&ppCurRecips, &ppRecipsToAdd, &ppRecipsToRemove, true, &pRetString))
		{
			bFailed = true;
			break;
		}
	}
	while (0);

	if (!bFailed)
	{
		if (!bRemoved)
		{
			MakeCommaSeparatedString(&ppCurRecips, &pSystem->pConfig->pEmailRecipients);
			
			if (iNewDelayTime)
			{
				pSystem->pConfig->iMaxStallTimeBeforeAssumedDeath = iNewDelayTime;
			}

			if (iNewDelayTimeWithDisconnect)
			{
				pSystem->pConfig->iMaxStallTimeWithDisconnectBeforeAssumedDeath = iNewDelayTimeWithDisconnect;
			}

			if (iNewPauseBeforeRestartingTime)
			{
				pSystem->pConfig->iStallTimeAfterDeathBeforeRestarting = iNewPauseBeforeRestartingTime;
			}
		}

		if (bClearEscEmails)
		{
			SAFE_FREE(pSystem->pConfig->pLevel1EscalationEmail);
			SAFE_FREE(pSystem->pConfig->pLevel2EscalationEmail);
		}
		else if (pEsc1Email)
		{
			SAFE_FREE(pSystem->pConfig->pLevel1EscalationEmail);
			SAFE_FREE(pSystem->pConfig->pLevel2EscalationEmail);
			pSystem->pConfig->pLevel1EscalationEmail = strdup(pEsc1Email);
			pSystem->pConfig->pLevel2EscalationEmail = strdup(pEsc2Email);
		}


	
		WriteOutGlobalConfig();

		if (!bRemoved)
		{
			MakeModifySystemCommand(pSystem);
		}
	}

	eaDestroyEx(&ppCurRecips, NULL);
	eaDestroyEx(&ppRecipsToAdd, NULL);
	eaDestroyEx(&ppRecipsToRemove, NULL);

	return pRetString;
}




AUTO_COMMAND;
char *AddAndRemoveGlobalEmailRecipients(char *pAddString, char *pRemoveString)
{
	static char *pRetString = NULL;
	char **ppCurRecips = NULL;
	char **ppRecipsToAdd = NULL;
	char **ppRecipsToRemove = NULL;
	bool bFailed = false;

	estrPrintf(&pRetString, "Succeeded");

	ExtractAlphaNumTokensFromStringEx(gCriticalSystemsGlobalConfig.pGlobalEmailRecipients, &ppCurRecips, "@.");
	ExtractAlphaNumTokensFromStringEx(pAddString, &ppRecipsToAdd, "@.");
	ExtractAlphaNumTokensFromStringEx(pRemoveString, &ppRecipsToRemove, "@.");

	MakeIntoEmailAddresses(&ppRecipsToAdd);
	MakeIntoEmailAddresses(&ppRecipsToRemove);
		
	if (!AddAndRemoveStrings(&ppCurRecips, &ppRecipsToAdd, &ppRecipsToRemove, true, &pRetString))
	{
		bFailed = true;
	}

	if (!bFailed)
	{
		MakeCommaSeparatedString(&ppCurRecips, &gCriticalSystemsGlobalConfig.pGlobalEmailRecipients);
		WriteOutGlobalConfig();
	}

	eaDestroyEx(&ppCurRecips, NULL);
	eaDestroyEx(&ppRecipsToAdd, NULL);
	eaDestroyEx(&ppRecipsToRemove, NULL);

	return pRetString;
}




AUTO_COMMAND;
char *AddAndRemoveSystemDownEmailRecipients(char *pAddString, char *pRemoveString)
{
	static char *pRetString = NULL;
	char **ppCurRecips = NULL;
	char **ppRecipsToAdd = NULL;
	char **ppRecipsToRemove = NULL;
	bool bFailed = false;

	estrPrintf(&pRetString, "Succeeded");

	ExtractAlphaNumTokensFromStringEx(gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients, &ppCurRecips, "@.");
	ExtractAlphaNumTokensFromStringEx(pAddString, &ppRecipsToAdd, "@.");
	ExtractAlphaNumTokensFromStringEx(pRemoveString, &ppRecipsToRemove, "@.");

	MakeIntoEmailAddresses(&ppRecipsToAdd);
	MakeIntoEmailAddresses(&ppRecipsToRemove);
		
	if (!AddAndRemoveStrings(&ppCurRecips, &ppRecipsToAdd, &ppRecipsToRemove, true, &pRetString))
	{
		bFailed = true;
	}

	if (!bFailed)
	{
		MakeCommaSeparatedString(&ppCurRecips, &gCriticalSystemsGlobalConfig.pSystemDownEmailRecipients);
		WriteOutGlobalConfig();
	}

	eaDestroyEx(&ppCurRecips, NULL);
	eaDestroyEx(&ppRecipsToAdd, NULL);
	eaDestroyEx(&ppRecipsToRemove, NULL);

	return pRetString;
}

		
AUTO_COMMAND;
char *AddAndRemoveCategoryEmailRecipients(char *pCatName_unPooled, char *pWhoToAdd, char *pWhoToRemove)
{
	static char *pRetString = NULL;
	const char *pCatNamePooled = allocAddString(pCatName_unPooled);
	CriticalSystem_CategoryConfig *pConfig = FindCategoryConfig(pCatNamePooled);

	char **ppCurRecips = NULL;
	char **ppRecipsToAdd = NULL;
	char **ppRecipsToRemove = NULL;
	bool bFailed = false;


	if (!pConfig)
	{
		pConfig = StructCreate(parse_CriticalSystem_CategoryConfig);
		pConfig->pCategoryName = pCatNamePooled;

		eaPush(&gCriticalSystemsGlobalConfig.ppCategoryConfigs, pConfig);
	}

	ExtractAlphaNumTokensFromStringEx(pConfig->pEmailRecipients, &ppCurRecips, "@.");
	ExtractAlphaNumTokensFromStringEx(pWhoToAdd, &ppRecipsToAdd, "@.");
	ExtractAlphaNumTokensFromStringEx(pWhoToRemove, &ppRecipsToRemove, "@.");

	MakeIntoEmailAddresses(&ppRecipsToAdd);
	MakeIntoEmailAddresses(&ppRecipsToRemove);
		
	if (!AddAndRemoveStrings(&ppCurRecips, &ppRecipsToAdd, &ppRecipsToRemove, true, &pRetString))
	{
		bFailed = true;
	}

	if (!bFailed)
	{
		MakeCommaSeparatedString(&ppCurRecips, &pConfig->pEmailRecipients);
		WriteOutGlobalConfig();
	}

	eaDestroyEx(&ppCurRecips, NULL);
	eaDestroyEx(&ppRecipsToAdd, NULL);
	eaDestroyEx(&ppRecipsToRemove, NULL);

	return pRetString;
}

bool DoesCategoryExist(char *pCatName)
{
	const char *pPooled = allocAddString(pCatName);
	int i;

	if (FindCategoryConfig(pPooled))
	{
		return true;
	}

	for (i=0; i < eaSize(&ppCriticalSystems); i++)
	{
		if (eaFind(&ppCriticalSystems[i]->pConfig->ppCategories, pPooled) != -1)
		{
			return true;
		}
	}

	return false;
}
	
bool IsCategoryOrSystemName(char *pName)
{
	if (DoesCategoryExist(pName))
	{
		return true;
	}

	if (stashFindPointer(sCriticalSystemsByName, pName, NULL))
	{
		return true;
	}

	return false;
}
		
void ExpandListOfCategoryOrSystemNames(char **ppInNames, char ***pppOutNames)
{
	int i;
	for (i = 0; i < eaSize(&ppInNames); i++)
	{
		char *pCurName = (char*)allocAddString(ppInNames[i]);
		CriticalSystem_Status *pSystem;
		CriticalSystem_CategoryConfig *pCategory;
		
		pCategory = FindCategoryConfig(pCurName);

		if (pCategory)
		{
			eaPush(pppOutNames, pCurName);

			FOR_EACH_IN_STASHTABLE(sCriticalSystemsByName, CriticalSystem_Status, pMaybeStatus)
			{
				if (eaFind(&pMaybeStatus->pConfig->ppCategories, pCurName) != -1)
				{
					eaPush(pppOutNames, (char*)allocAddString(pMaybeStatus->pName_Internal));
				}
			}
			FOR_EACH_END;

		}
		else
		{
			if (stashFindPointer(sCriticalSystemsByName, pCurName, &pSystem))
			{
				int j;
				eaPush(pppOutNames, pCurName);

				for (j = 0; j < eaSize(&pSystem->pConfig->ppCategories); j++)
				{
					eaPush(pppOutNames, (char*)(pSystem->pConfig->ppCategories[j]));
				}
			}
		}
	}
}

#include "NewControllerTracker_CriticalSystems_h_ast.c"
#include "NewControllerTracker_CriticalSystems_c_ast.c"
