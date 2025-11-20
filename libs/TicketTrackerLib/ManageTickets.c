#include "ManageTickets.h"

#include "Category.h"
#include "error.h"
#include "GlobalTypeEnum.h"
#include "jira.h"
#include "Message.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "Search.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "TicketEntry.h"
#include "ticketnet.h"
#include "TicketTracker.h"
#include "TicketTrackerConfig.h"
#include "timing.h"

#include "AutoGen/ManageTickets_h_ast.h"
#include "AutoGen/TicketEntry_h_ast.h"
#include "AutoGen/TicketTracker_h_ast.h"
#include "AutoGen/jira_h_ast.h"
#include "AutoGen/TicketTrackerLib_autotransactions_autogen_wrappers.h"

// ------------------------------------
// Jira Status and Resolution Mappings
static int siStatusMap[] = {
	1, TICKETSTATUS_IN_PROGRESS, // Open
	3, TICKETSTATUS_IN_PROGRESS, // In Progress
	4, TICKETSTATUS_IN_PROGRESS, // Reopened
	// 5, TICKETSTATUS_RESOLVED, // Resolved does not change
	6, TICKETSTATUS_RESOLVED, // Closed
	0,
};

static int siResolutionMap[] = 
{
	1, TICKETRESOLUTION_FIXED, 
	2, TICKETRESOLUTION_AS_DESIGNED, 
	3, TICKETRESOLUTION_DUPLICATE,
	5, TICKETRESOLUTION_CANNOT_REPRO,
	6, TICKETRESOLUTION_EXTERNAL,
	7, TICKETRESOLUTION_WONT_FIX,
	8, TICKETRESOLUTION_POSTPONED,
	0,
};

static StashTable stTicketJiraStatusTable;
static StashTable stTicketJiraResolutionTable;
static StashTable stTicketJiraCountTable;
static StashTable stTicketSolutionCountTable;

// ------------------------------------
// Jira Solutions - Deprecated

static bool ticketChangeJiraKeyCount (StashTable table, TicketEntry *ticket, const char *key, int iChange)
{
	TicketJiraSummary *jiraSummary;
	if (stashFindPointer(table, key, &jiraSummary))
	{
		if (iChange > 0)
		{
			int idx = eaiFind(&jiraSummary->eaiTicketIDs, ticket->uID);
			if (idx < 0)
			{
				eaiPush(&jiraSummary->eaiTicketIDs, ticket->uID);
				jiraSummary->uCount++;
			}
		}
		else
		{
			if (eaiFindAndRemove(&jiraSummary->eaiTicketIDs, ticket->uID) > 0)
				jiraSummary->uCount--;
			if (jiraSummary->uCount == 0)
			{
				stashRemovePointer(table, key, NULL);
				StructDestroy(parse_TicketJiraSummary, jiraSummary);
			}
		}
		return true;
	}
	else if (iChange > 0)
	{
		jiraSummary = StructCreate(parse_TicketJiraSummary);
		estrCopy2(&jiraSummary->jiraKey, key);
		eaiPush(&jiraSummary->eaiTicketIDs, ticket->uID);
		jiraSummary->uCount = 1;
		assert(stashAddPointer(table, jiraSummary->jiraKey, jiraSummary, false));
	}
	return false;
}

bool ticketChangeSolutionCount(TicketEntry *ticket, const char *key, int iChange)
{
	if (!ticket || !key)
		return true;
	return ticketChangeJiraKeyCount(stTicketSolutionCountTable, ticket, key, iChange);
}

bool ticketChangeJiraCount(TicketEntry *ticket, const char *key, int iChange)
{
	if (!ticket || !key)
		return true;
	return ticketChangeJiraKeyCount(stTicketJiraCountTable, ticket, key, iChange);
}

void ticketInitializeJiraMappings(void)
{
	int i;

	stTicketJiraStatusTable = stashTableCreateInt(ARRAY_SIZE_CHECKED(siStatusMap) / 2 + 1);
	for (i=0; siStatusMap[i]; i+=2)
	{
		stashIntAddInt(stTicketJiraStatusTable, siStatusMap[i], siStatusMap[i+1], true);
	}

	stTicketJiraResolutionTable = stashTableCreateInt(ARRAY_SIZE_CHECKED(siResolutionMap) / 2 + 1);
	for (i=0; siResolutionMap[i]; i+=2)
	{
		stashIntAddInt(stTicketJiraResolutionTable, siResolutionMap[i], siResolutionMap[i+1], true);
	}
	stTicketJiraCountTable = stashTableCreateWithStringKeys(1000, StashDeepCopyKeys_NeverRelease);
	stTicketSolutionCountTable = stashTableCreateWithStringKeys(1000, StashDeepCopyKeys_NeverRelease);
}

TicketStatus ticketStatusFromJira (int iJiraStatus)
{
	int iStatus;
	if (iJiraStatus && stashIntFindInt(stTicketJiraStatusTable, iJiraStatus, &iStatus))
		return (TicketStatus) iStatus;
	return TICKETSTATUS_UNKNOWN;
}

TicketResolution ticketResolutionFromJira (int iJiraResolution)
{
	int iResolution;
	if (iJiraResolution && stashIntFindInt(stTicketJiraResolutionTable, iJiraResolution, &iResolution))
		return (TicketResolution) iResolution;
	return TICKETRESOLUTION_UNDEFINED;
}

typedef struct TicketJiraChangeData
{
	U32 uTicketID;
	char *prevJira;
} TicketJiraChangeData;

void trJiraSolutionChange_CB(TransactionReturnVal *returnVal, TicketJiraChangeData *data)
{
	
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		TicketEntry *pEntry = findTicketEntryByID(data->uTicketID);
		if (data->prevJira)
			ticketChangeSolutionCount(pEntry, data->prevJira, -1);
		ticketChangeSolutionCount(pEntry, pEntry->pSolutionKey, 1);
	}
	if (data->prevJira)
	{
		ANALYSIS_ASSUME(data->prevJira);
		free(data->prevJira);
	}
	free(data);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppLog, .pSolutionKey");
enumTransactionOutcome trTicketSetSolution(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *actor, const char *jiraKey)
{
	char *logString = NULL;
	SAFE_FREE(pEntry->pSolutionKey);
	if (jiraKey)
	{
		estrPrintf(&logString, "Changed Solution Key: %s", jiraKey);
		pEntry->pSolutionKey = StructAllocString(jiraKey);
	}
	else
		estrPrintf(&logString, "Cleared Solution Key");
	trh_AddGenericLog(pEntry, actor, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

bool setTicketSolution(TicketEntry *pEntry, const char *actorName, const char *solutionKey)
{
	TicketJiraChangeData *data;
	if (stricmp_safe(pEntry->pSolutionKey, solutionKey) == 0)
		return true;
	if (solutionKey && *solutionKey)
	{
		NOCONST(JiraIssue) jiraIssue = {0};
		estrCopy2(&jiraIssue.key, solutionKey);
		if (!jiraDefaultLogin())
		{
			Errorf("Could not connect to Jira");
			StructDeInitNoConst(parse_JiraIssue, &jiraIssue);
			return false;
		}
		if (!jiraGetIssue((JiraIssue*) &jiraIssue, NULL))
		{
			Errorf("Could not find Jira Solution Key %s", solutionKey);
			StructDeInitNoConst(parse_JiraIssue, &jiraIssue);
			return false;
		}
		StructDeInitNoConst(parse_JiraIssue, &jiraIssue);
	}
	data = (TicketJiraChangeData*) malloc(sizeof(TicketJiraChangeData));
	data->uTicketID = pEntry->uID;
	data->prevJira = StructAllocString(pEntry->pSolutionKey);
	AutoTrans_trTicketSetSolution(objCreateManagedReturnVal(trJiraSolutionChange_CB, data), objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, actorName, solutionKey);
	return true;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppLog, .pPhoneResKey");
enumTransactionOutcome trTicketSetPhoneResolution(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *actor, const char *jiraKey)
{
	char *logString = NULL;
	SAFE_FREE(pEntry->pPhoneResKey);
	if (jiraKey)
	{
		estrPrintf(&logString, "Changed Phone Resolution: %s", jiraKey);
		pEntry->pPhoneResKey = StructAllocString(jiraKey);
	}
	else
		estrPrintf(&logString, "Cleared Phone Resolution");
	trh_AddGenericLog(pEntry, actor, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

bool setTicketPhoneResolution(TicketEntry *pEntry, const char *actorName, const char *solutionKey)
{
	TicketJiraChangeData *data;
	if (stricmp_safe(pEntry->pPhoneResKey, solutionKey) == 0)
		return true;
	if (solutionKey && *solutionKey)
	{
		NOCONST(JiraIssue) jiraIssue = {0};
		estrCopy2(&jiraIssue.key, solutionKey);
		if (!jiraDefaultLogin())
		{
			Errorf("Could not connect to Jira");
			StructDeInitNoConst(parse_JiraIssue, &jiraIssue);
			return false;
		}
		if (!jiraGetIssue((JiraIssue*) &jiraIssue, NULL))
		{
			Errorf("Could not find Phone Resolution Key %s", solutionKey);
			StructDeInitNoConst(parse_JiraIssue, &jiraIssue);
			return false;
		}
		StructDeInitNoConst(parse_JiraIssue, &jiraIssue);
	}
	data = (TicketJiraChangeData*) malloc(sizeof(TicketJiraChangeData));
	data->uTicketID = pEntry->uID;
	data->prevJira = StructAllocString(pEntry->pSolutionKey);
	AutoTrans_trTicketSetPhoneResolution(objCreateManagedReturnVal(trJiraSolutionChange_CB, data), objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, actorName, solutionKey);
	return true;
}

void trJiraChange_CB(TransactionReturnVal *returnVal, TicketJiraChangeData *data)
{
	
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		TicketEntry *pEntry = findTicketEntryByID(data->uTicketID);
		if (pEntry->eVisibility != TICKETVISIBLE_HIDDEN)
		{
			if (data->prevJira)
				ticketChangeJiraCount(pEntry, data->prevJira, -1);
			ticketChangeJiraCount(pEntry, pEntry->pSolutionKey, 1);
		}
	}
	if (data->prevJira)
	{
		ANALYSIS_ASSUME(data->prevJira);
		free(data->prevJira);
	}
	free(data);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppLog, .pJiraIssue");
enumTransactionOutcome trTicketSetJiraIssue(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *actor, 
	NON_CONTAINER JiraIssue *pJiraIssue)
{
	char *logString = NULL;
	if (pEntry->pJiraIssue)
	{
		StructDestroyNoConst(parse_JiraIssue, pEntry->pJiraIssue);
		pEntry->pJiraIssue = NULL;
	}
	if (pJiraIssue)
	{
		estrPrintf(&logString, "Changed Jira Key: %s", pJiraIssue->key);
		pEntry->pJiraIssue = StructCloneNoConst(parse_JiraIssue, CONTAINER_NOCONST(JiraIssue, pJiraIssue));
	}
	else
		estrPrintf(&logString, "Cleared Jira Key");
	trh_AddGenericLog(pEntry, actor, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

bool setJiraKey(TicketEntry *pEntry, const char *actorName, const char *issueKey)
{
	TicketJiraChangeData *data;
	NOCONST(JiraIssue) *jiraIssue = NULL;
	if (pEntry->pJiraIssue && stricmp_safe(pEntry->pJiraIssue->key, issueKey) == 0)
		return true;
	if (issueKey && *issueKey)
	{
		jiraIssue = StructCreateNoConst(parse_JiraIssue);
		estrCopy2(&jiraIssue->key, issueKey);
		if (!jiraDefaultLogin())
		{
			Errorf("Could not connect to Jira");
			StructDestroyNoConst(parse_JiraIssue, jiraIssue);
			return false;
		}
		if (!jiraGetIssue((JiraIssue*) jiraIssue, NULL))
		{
			Errorf("Could not find Jira Key %s", issueKey);
			StructDestroyNoConst(parse_JiraIssue, jiraIssue);
			return false;
		}
	}
	data = (TicketJiraChangeData*) malloc(sizeof(TicketJiraChangeData));
	data->uTicketID = pEntry->uID;
	data->prevJira = StructAllocString(pEntry->pSolutionKey);
	AutoTrans_trTicketSetJiraIssue(objCreateManagedReturnVal(trJiraChange_CB, data), objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, actorName, CONTAINER_RECONST(JiraIssue, jiraIssue));
	if (jiraIssue)
		StructDestroyNoConst(parse_JiraIssue, jiraIssue);
	return true;
}

int ticketEntryChangeResolution(const char *actorName, TicketEntry *pEntry, TicketResolution eResolution)
{
	char *pLogString = NULL;
	TicketResolution ePrevResolution = pEntry->eResolution;
	if (eResolution >= TICKETRESOLUTION_COUNT || eResolution < 0)
	{
		return -1;
	}
	if (pEntry->eResolution == eResolution)
		return 0;

	pEntry->eResolution = eResolution;
	estrPrintf(&pLogString, "Resolution: %s", getResolutionString(eResolution));
	objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "ChangeResolution", "set .eResolution = %d", eResolution);
	ticketAddGenericLog(pEntry, actorName, pLogString);
	estrDestroy(&pLogString);
	return 0;
}

// ------------------------------------

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".pMainCategory, .pCategory, .ppLog");
enumTransactionOutcome trChangeCategory(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *actor, const char *pMainCategory, const char *pCategory)
{
	char *logString = NULL;
	SAFE_FREE(pEntry->pCategory);
	SAFE_FREE(pEntry->pMainCategory);
	pEntry->pMainCategory = StructAllocString(pMainCategory);
	if (!nullStr(pCategory))
		pEntry->pCategory = StructAllocString(pCategory);

	estrPrintf(&logString, "Changed Category to: %s - %s", pMainCategory, pCategory);
	trh_AddGenericLog(pEntry, actor, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

bool ticketChangeCategory(TicketEntry *pEntry, const char *actorName, const char *pMainCategory, const char *pCategory)
{
	int maincatidx, subcatidx;

	if (!pEntry)
		return false;
	
	maincatidx = categoryGetIndex(pMainCategory);
	if (maincatidx < 0)
		return false;

	subcatidx = subcategoryGetIndex(maincatidx, pCategory);
	if (pCategory && *pCategory && subcatidx < 0)
		return false;

	searchRemoveTicket(pEntry);
	AutoTrans_trChangeCategory(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, actorName, pMainCategory, pCategory);
	searchAddTicket(pEntry);
	return true;
}

AUTO_TRANS_HELPER;
void trh_AddGenericLog(ATH_ARG NOCONST(TicketEntryConst) *pEntry, const char *actor, const char *logString)
{
	NOCONST(TicketLog) *pLog = StructCreateNoConst(parse_TicketLog);
	pLog->uTime = timeSecondsSince2000();
	pLog->actorName = StructAllocString(actor);
	estrCopy2(&pLog->pLogString, logString);
	eaPush(&pEntry->ppLog, pLog);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppLog");
enumTransactionOutcome trAddGenericLog(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *actor, const char *logString)
{
	trh_AddGenericLog(pEntry, actor, logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ticketAddGenericLog (TicketEntry *pEntry, const char *actorName, const char *logfmt, ...)
{
	char *pLogString = NULL;
	va_list	ap;

	va_start(ap, logfmt);	
	estrConcatfv(&pLogString, logfmt, ap);
	va_end(ap);
	AutoTrans_trAddGenericLog(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, actorName, pLogString);
	estrDestroy(&pLogString);
}

AUTO_TRANS_HELPER;
void trh_AddCommunicationLog(ATH_ARG NOCONST(TicketEntryConst) *pEntry, TicketUserType eType, const char *actor, const char *logString)
{
	NOCONST(TicketCommLog) *pCommLog = StructCreateNoConst(parse_TicketCommLog);	
	pCommLog->eType = eType;
	pCommLog->userName = StructAllocString(actor);
	estrCopy2(&pCommLog->pLogString, logString);
	pCommLog->uTime = timeSecondsSince2000();
	eaPush(&pEntry->ppResponseDescriptionLog, pCommLog);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppResponseDescriptionLog");
enumTransactionOutcome trAddCommunicationLog(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, int eType, const char *actor, const char *logString)
{
	trh_AddCommunicationLog(pEntry, (TicketUserType)eType, actor, logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ticketLogCommunication(TicketEntry *pEntry, TicketUserType eType, const char *username, const char *logString)
{
	if (!pEntry || !eType || !logString || !*logString)
		return;;
	AutoTrans_trAddCommunicationLog(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, eType, username, logString);
}

bool ticketUserCanEdit(TicketEntry *pEntry, const char *accountName)
{
	if (!pEntry || !accountName || !*accountName)
		return false;

	if (pEntry->eVisibility != TICKETVISIBLE_PRIVATE)
		return false;
	if (pEntry->uUniqueAccounts != 1)
		return false;
	if (!eaSize(&pEntry->ppUserInfo) || stricmp(pEntry->ppUserInfo[0]->pAccountName, accountName))
		return false;
	return true;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppResponseDescriptionLog, .pSummary, .pUserDescription, .eStatus");
enumTransactionOutcome trEditTicket(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *username, const char *pSummary, const char *pDescription, int bAdminEdit)
{
	char *logString = NULL;
	if (pSummary)
	{
		SAFE_FREE(pEntry->pSummary);
		pEntry->pSummary = StructAllocString(pSummary);
		estrPrintf(&logString, "Summary: %s", pEntry->pSummary);
	}

	if (pDescription)
	{
		SAFE_FREE(pEntry->pUserDescription);
		pEntry->pUserDescription = StructAllocString(pDescription);
		if (logString)
			estrConcatf(&logString, ";\nDescription: %s", pEntry->pUserDescription);
		else
			estrPrintf(&logString, "Description: %s", pEntry->pUserDescription);
	}

	if (!bAdminEdit && pEntry->eStatus == TICKETSTATUS_PENDING)
	{
		pEntry->eStatus = TICKETSTATUS_PLAYEREDITED;
		estrConcatf(&logString, ";\nStatus: %s", getStatusString(pEntry->eStatus));
	}
	trh_AddCommunicationLog(pEntry, TICKETUSER_PLAYER, username, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ticketUserEditInternal(TicketEntry *pEntry, const char *username, const char *pSummary, const char *pDescription, bool bAdminEdit)
{
	if (nullStr(pSummary) && nullStr(pDescription))
		return;
	AutoTrans_trEditTicket(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, username, pSummary, pDescription, bAdminEdit);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppComments");
enumTransactionOutcome trAddComment(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *username, const char *comment)
{
	TicketComment *pComment = StructCreateNoConst(parse_TicketComment);
	pComment->pUser = StructAllocString(username);
	pComment->pComment = StructAllocString(comment);
	eaPush(&pEntry->ppComments, pComment);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ticketAddComment(TicketEntry *pEntry, const char *pUser, const char *pText)
{
	if (nullStr(pUser) || nullStr(pText))
		return;
	AutoTrans_trAddComment(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, pUser, pText);
}

AUTO_TRANS_HELPER;
void trh_ChangeCSRResponse(ATH_ARG NOCONST(TicketEntryConst) *pEntry, const char *username, const char *response, bool isAutoResponse)
{
	char *logString = NULL;
	SAFE_FREE(pEntry->pResponseToUser);
	pEntry->pResponseToUser = StructAllocString(response);
	pEntry->bAutoResponse = isAutoResponse;
	pEntry->uResponseTime = timeSecondsSince2000();
	
	if (isAutoResponse)
		estrPrintf(&logString, "Response (Auto): %s", response);
	else
		estrPrintf(&logString, "Response: %s", response);
	trh_AddCommunicationLog(pEntry, TICKETUSER_CSR, username, logString);
	estrDestroy(&logString);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".pResponseToUser, .bAutoResponse, .uResponseTime, .ppResponseDescriptionLog");
enumTransactionOutcome trChangeCSRResponse(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, const char *username, const char *response)
{
	trh_ChangeCSRResponse(pEntry, username, response, false);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ticketChangeCSRResponse(TicketEntry *pEntry, const char *username, const char *response)
{
	if (!pEntry)
		return;
	AutoTrans_trChangeCSRResponse(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, username, response);
}

void ticketAssignToAccountName(TicketEntry *pEntry, const char *pAccountName)
{
	if (!pEntry)
		return;	
	if (pAccountName && *pAccountName)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "ChangeAssignee", "set .pRepAccountName = \"%s\"", pAccountName);
	else
		objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "ChangeAssignee", "destroy pRepAccountName");
	ticketAddGenericLog(pEntry, pAccountName, "Assigned to User");
}

// ------------------------------------

void changeTicketTrackerEntryVisible (const char *actorName, TicketEntry *pEntry, TicketVisibility eVisibility)
{
	if (pEntry->eVisibility != eVisibility)
	{
		if (pEntry->eVisibility == TICKETVISIBLE_HIDDEN && pEntry->pJiraIssue)
			ticketChangeJiraCount(pEntry, pEntry->pJiraIssue->key, -1);
		objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "ChangeVisibility", "set .eVisibility = %d", eVisibility);
		ticketAddGenericLog(pEntry, actorName, "Changed Visibility: ", getVisibilityString(eVisibility));
	}
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".eStatus, .uEndTime");
enumTransactionOutcome trChangeStatus(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, int iStatus)
{
	pEntry->eStatus = (TicketStatus) iStatus;
	if (TICKET_STATUS_IS_CLOSED(pEntry->eStatus))
		pEntry->uEndTime = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}


int ticketEntryChangeStatus(const char *actorName, TicketEntry *pEntry, TicketStatus eStatus)
{
	TicketLog *pLog = NULL;
	TicketStatus ePrevStatus = pEntry->eStatus;
	if (eStatus >= TICKETSTATUS_COUNT || eStatus < 0)
	{
		return -1;
	}
	if (pEntry->eStatus == eStatus)
		return 0;

	AutoTrans_trChangeStatus(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, eStatus);
	searchChangeTicketStatus(pEntry, ePrevStatus);
	
	ticketAddGenericLog(pEntry, actorName, "Status: %s", getStatusString(eStatus));
	return 0;
}

// ------------------------------------
// Jira Reports

// Sorts in descending count
static int TicketJiraReportCmp(const TicketJiraSummary **pptr1, const TicketJiraSummary **pptr2)
{
	if ((*pptr1)->uCount < (*pptr2)->uCount)
		return 1;
	if ((*pptr1)->uCount > (*pptr2)->uCount)
		return -1;
	return 0;
}

static void TicketTracker_GenerateJiraReportHelper(StashTableIterator *iter, TicketJiraSummary ***eaTopTen, U32 uStartTime, U32 uEndTime)
{
	TicketJiraSummary ** jiraReport = NULL;
	StashElement elem;
	int count = 0, minCount = 0;

	while (stashGetNextElement(iter, &elem))
	{
		TicketJiraSummary *jira = StructCreate(parse_TicketJiraSummary);
		TicketJiraSummary *cur = stashElementGetPointer(elem);
		int i;

		estrCopy2(&jira->jiraKey, cur->jiraKey);
		for (i=0; (U32)i < cur->uCount; i++)
		{
			TicketEntry *ticket = findTicketEntryByID(cur->eaiTicketIDs[i]);
			if (ticket)
			{
				if (uStartTime && ticket->uFiledTime < uStartTime)
					continue;
				if (uEndTime && uEndTime < ticket->uFiledTime)
					continue;
				eaiPush(&jira->eaiTicketIDs, ticket->uID);
				jira->uCount++;
				jira->uNumSubscribers += ticket->uOccurrences;
			}
		}
		if (jira->uCount)
		{
			bool bAdded = false;
			if (count < 10)
				bAdded = true;
			else if (jira->uCount >= (U32) minCount)
				bAdded = true;

			if (bAdded)
			{
				int index = (int) eaBFind(jiraReport, TicketJiraReportCmp, jira);
				if (!minCount || jira->uCount < (U32) minCount)
					minCount = jira->uCount;
				count++;
				eaInsert(&jiraReport, jira, index);
				if (count > 10)
				{
					int lastIndex = eaSize(&jiraReport)-1;
					int removeCount = 1;
					while (lastIndex >= 10 && jiraReport[lastIndex-1]->uCount == minCount)
					{
						lastIndex--;
						removeCount++;
					}
					if (lastIndex >= 10)
					{
						for (i=0; i<removeCount; i++)
							StructDestroy(parse_TicketJiraSummary, jiraReport[lastIndex+i]);
						eaRemoveRange(&jiraReport, lastIndex, removeCount);
						count -= removeCount;
					}
				}
			}
			else StructDestroy(parse_TicketJiraSummary, jira);
		}
		else StructDestroy(parse_TicketJiraSummary, jira);
	}
	*eaTopTen = jiraReport;
}

void TicketTracker_GenerateJiraReport (TicketJiraSummary ***eaTopTen, U32 uStartTime, U32 uEndTime)
{
	StashTableIterator iter = {0};
	stashGetIterator(stTicketJiraCountTable, &iter);
	TicketTracker_GenerateJiraReportHelper(&iter, eaTopTen, uStartTime, uEndTime);
}

void TicketTracker_GenerateSolutionReport (TicketJiraSummary ***eaTopTen, U32 uStartTime, U32 uEndTime)
{
	StashTableIterator iter = {0};
	stashGetIterator(stTicketSolutionCountTable, &iter);
	TicketTracker_GenerateJiraReportHelper(&iter, eaTopTen, uStartTime, uEndTime);
}

// ///////////////////////////////////
// Auto-Response Rules
extern TicketAutoResponseRuleList gTicketAutoResponseRules;

void TicketAutoResponse_ApplyRule(NOCONST(TicketEntryConst) *pEntry)
{
	TicketAutoResponseRule *foundRule = NULL;
	// Applies the first matching rule found
	EARRAY_CONST_FOREACH_BEGIN(gTicketAutoResponseRules.eaRules, i, s);
	{
		TicketAutoResponseRule *rule = gTicketAutoResponseRules.eaRules[i];
		if (rule->eLanguage != LANGUAGE_DEFAULT && rule->eLanguage != pEntry->eLanguage && 
			(rule->eLanguage != LANGUAGE_ENGLISH || pEntry->eLanguage != LANGUAGE_DEFAULT))
			continue;
		if (eaSize(&rule->eaProducts) > 0 && eaFind(&rule->eaProducts, pEntry->pProductName) == -1)
			continue;
		if (!nullStr(rule->pMainCategory))
		{
			if (stricmp_safe(rule->pMainCategory, pEntry->pMainCategory) != 0)
				continue;
			if (!nullStr(rule->pCategory) && stricmp_safe(rule->pCategory, pEntry->pCategory) != 0)
				continue;
		}
		foundRule = rule;
		break;
	}
	EARRAY_FOREACH_END;
	if (foundRule)
	{
		const char *response = NULL;
		if (foundRule->eLanguage == LANGUAGE_DEFAULT)
			response = langTranslateMessageKeyDefault(pEntry->eLanguage, foundRule->pMessage, foundRule->pMessage);
		else
			response = foundRule->pMessage;
		trh_ChangeCSRResponse(pEntry, NULL, response, true);
	}
}

#include "AutoGen/ManageTickets_h_ast.c"