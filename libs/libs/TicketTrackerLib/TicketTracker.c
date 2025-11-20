#include "tickettracker.h"
#include "AutoGen\tickettracker_h_ast.h"
#include "Authentication.h"
#include "TicketAPI.h"
#include "TicketAssignment.h"
#include "TicketEntry.h"
#include "TicketLocation.h"
#include "TicketTrackerConfig.h"
#include "ManageTickets.h"
#include "TicketTrackerDB.h"
#include "RightNowIntegration.h"

#include "AutoTransDefs.h"
#include "Category.h"
#include "EntityDescriptor.h"
#include "file.h"
#include "earray.h"
#include "estring.h"
#include "jira.h"
#include "jpeg.h"
#include "JSONRPC.h"
#include "logging.h"
#include "loggingEnums.h"
#include "MemoryMonitor.h"
#include "Message.h"
#include "objContainerIO.h"
#include "objPath.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "Search.h"
#include "ServerLib.h"
#include "sock.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "sysutil.h"
#include "textparser.h"
#include "ticketnet.h"
#include "timing.h"
#include "timing_profiler.h"
#include "timing_profiler_interface.h"
#include "trivia.h"
#include "utilitiesLib.h"
#include "utils.h"
#include "WebInterface.h"
#include "winutil.h"

#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/EntityDescriptor_h_ast.h"
#include "AutoGen/jira_h_ast.h"
#include "AutoGen/ticketenums_h_ast.h"
#include "AutoGen/ticketnet_h_ast.h"
#include "AutoGen/TicketEntry_h_ast.h"
#include "AutoGen/TicketTrackerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/trivia_h_ast.h"

static void SendTicketResponse (NetLink *link, U32 uErrorCode, const char *pErrorMessage);
typedef struct TicketCreateData
{
	U32 linkID;
	U32 mergeID; // for merging tickets
	TicketData *ticketData;
} TicketCreateData;

static void ticketDataDestroy(TicketData *data)
{
	if (data->imageBuffer)
	{
		free(data->imageBuffer);
		data->imageBuffer = NULL;
	}
	StructDestroy(parse_TicketData, data);
}

#define CONTAINER_ENTRY_ID 1
#define STATUS_REQUEST_MAX_AGE 1209600 // 14 * 24 * 60 *60, two weeks
// in seconds; only applies to closed / resolved tickets

#define MIGRATION_MAX_COUNT_PER_FRAME 5000

#define TICKET_AUTORESPONSE_TEXT "Thank you very much for taking the time to submit this bug report.\n\n" \
	"Your issue is in the queue to be processed and entered into our bug database. "\
	"We generally review all bug reports we receive within one day.\n\n"\
	"Unfortunately, due to the number of reports we receive, we are unable to respond to each report individually. "\
	"If you do require individual assistance from our support staff, please submit a GM Help request and we will attempt to contact you in-game as soon as possible."
#define TICKET_AUTOPROCESS_TEXT "Thank you very much for taking the time to submit this bug report.\n\n"\
	"Your issue has been processed and entered into our bug database. Please see the forums and upcoming patch notes for updates on this issue.\n\n"\
	"For issues that require individual support from our in-game staff, please submit a GM Help request and we will attempt to contact you in-game as soon as possible."

static bool sbAutoRespondSTOFixup = false;
AUTO_CMD_INT(sbAutoRespondSTOFixup, STOResponseFixup);

// ------------------------------------
// Source Controlled
char gTicketTrackerDataDir[MAX_PATH] = "server\\TicketTracker\\Data\\";
AUTO_CMD_STRING(gTicketTrackerDataDir, ticketTrackerDataDir);

// Not Source Controlled
char gTicketTrackerAltDataDir[MAX_PATH] = "TicketTracker\\Data\\";
AUTO_CMD_STRING(gTicketTrackerAltDataDir, ticketTrackerAltDataDir);
char gTicketTrackerDBBackupDir[MAX_PATH] = "TicketTracker\\Backup\\";
AUTO_CMD_STRING(gTicketTrackerDBBackupDir, ticketTrackerDBBackupDir);

char gLogFileName[MAX_PATH] = "ticket_delete.log";

bool gbImportEntityDescriptors = false;
AUTO_CMD_INT(gbImportEntityDescriptors, ImportEntityDescriptors);

bool gbAutoResponseEnabled = false;
AUTO_CMD_INT(gbAutoResponseEnabled, AutoResponseEnabled);

bool gbWipeGameTickets = false;
AUTO_CMD_INT(gbWipeGameTickets, WipeGameTickets);

// ------------------------------------

extern ParseTable parse_TriviaData[];
#define TYPE_parse_TriviaData TriviaData
extern ParseTable parse_TicketRequestResponse[];
#define TYPE_parse_TicketRequestResponse TicketRequestResponse
extern ParseTable parse_TicketRequestResponseList[];
#define TYPE_parse_TicketRequestResponseList TicketRequestResponseList
extern ParseTable parse_TicketTrackerUser[];
#define TYPE_parse_TicketTrackerUser TicketTrackerUser
extern ParseTable parse_TicketUserGroup[];
#define TYPE_parse_TicketUserGroup TicketUserGroup

extern TimingHistory *gSearchHistory;
extern TimingHistory *gSendHistory;

AUTO_STARTUP(TicketTracker,1) ASTRT_DEPS(Category);
void ticketTrackerStartup(void)
{
	// Does nothing right now
}

static void ConvertDebugPosString(const char *debugPos, SA_PARAM_NN_VALID TicketClientGameLocation *pLocation)
{
	char *stringStart = "SetDebugPos \"";
	char *debugPosCopy = estrStackCreateFromStr(debugPos);
	char *cur = debugPosCopy, *cur2;
	if (!debugPos)
		return;

	// Custom parsing since sscanf is a piece of shit

	if (!strStartsWith(debugPos, stringStart))
		return;
	cur += strlen(stringStart);

	for (cur2 = cur; *cur2 != '\"' && *cur2 != ' ' && *cur2; cur2++) {}
	if (*cur2 == 0)
		return;
	*cur2 = 0;
	estrCopy2((char**) &pLocation->zoneName, cur);
	cur = cur2+1;
	while (*cur && *cur == ' ')
		cur++;

	sscanf(cur, "%f %f %f %f %f %f", 
		&pLocation->position[0], &pLocation->position[1], &pLocation->position[2],
		&pLocation->rotation[0], &pLocation->rotation[1], &pLocation->rotation[2]);
	estrDestroy(&debugPosCopy);
}

static bool ConvertDebugPosStringFromList (TriviaData **ppData, SA_PARAM_NN_VALID TicketClientGameLocation *pLocation, bool bRemove)
{
	NOCONST(TriviaList) tempList = {0};
	TriviaData *pTrivia = NULL;
	tempList.triviaDatas = (NOCONST(TriviaData)**) ppData;
	if (bRemove)
		pTrivia = triviaListRemoveEntry((TriviaList*) &tempList, "playerPos");
	else
		pTrivia = triviaListFindEntry((TriviaList*) &tempList, "playerPos");
	if (pTrivia)
	{
		ConvertDebugPosString(pTrivia->pVal, pLocation);
		return true;
	}
	return false;
}

void ticketConstructDebugPosString(char **estr, TicketEntry *pTicket)
{
	estrClear(estr);
	
	if (pTicket->pDebugPosString)
		estrCopy2(estr, pTicket->pDebugPosString);
	else if (pTicket->gameLocation.zoneName && pTicket->gameLocation.zoneName[0])
	{
		estrPrintf(estr, "SetDebugPos \"%s\" %f %f %f %f %f %f", 
			pTicket->gameLocation.zoneName, 
			pTicket->gameLocation.position[0], pTicket->gameLocation.position[1], pTicket->gameLocation.position[2], 
			pTicket->gameLocation.rotation[0], pTicket->gameLocation.rotation[1], pTicket->gameLocation.rotation[2]);
	}
}

static void SendTicketResponse (NetLink *link, U32 uErrorCode, const char *pErrorMessage)
{
	Packet *pak;
	if (!link || !linkConnected(link))
		return;
	//printf("   %s - Err(%s)\n", uErrorCode ? "Failure" : "Success", pErrorMessage ? pErrorMessage : "NULL");

	pak = pktCreate(link, FROM_TICKETTRACKER_ERRFLAGS);
	pktSendU32(pak, uErrorCode);
	pktSendString(pak, pErrorMessage);
	pktSend(&pak);
}


TicketEntry * findTicketEntryByID(U32 uID)
{
	Container * con = objGetContainer(GLOBALTYPE_TICKETENTRY, uID);
	if (con)
	{
		return CONTAINER_ENTRY(con);
	}
	return NULL;
}

void ticketUserClose(NetLink *link, TicketEntry *pEntry, const char *username)
{
	if (!pEntry)
	{
		SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketUnknown");
		return;
	}
	if (!ticketUserCanEdit(pEntry, username))
	{
		SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketCannotEdit");
		return;
	}
	ticketEntryChangeStatus(username, pEntry, TICKETSTATUS_CLOSED);
	SendTicketResponse(link, TICKETFLAGS_SUCCESS, STACK_SPRINTF("%d", pEntry->uID));
}

void ticketUserEdit (NetLink *link, TicketEntry *pEntry, const char *username, 
					 const char *pSummary, const char *pDescription, bool bAdminEdit)
{
	if (!pEntry)
	{
		SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketUnknown");
		return;
	}
	if (!bAdminEdit && !ticketUserCanEdit(pEntry, username))
	{
		SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketCannotEdit");
		return;
	}
	ticketUserEditInternal(pEntry, username, pSummary, pDescription, bAdminEdit);
	SendTicketResponse(link, TICKETFLAGS_SUCCESS, STACK_SPRINTF("%d", pEntry->uID));
}

void ticketMergeMultiple(const char *actorName, TicketEntry *parent, TicketEntry **aquisitions, const char *pNewSummary, const char *pNewDescription)
{
	int i, size;
	char **eaUniqueAccounts = NULL;

	size = eaSize(&parent->ppUserInfo);
	for (i=0; i<size; i++)
		eaPush(&eaUniqueAccounts, parent->ppUserInfo[i]->pAccountName);

	size = eaSize(&aquisitions);
	for (i=0; i<size; i++)
	{
		ticketMergeExisting(actorName, &eaUniqueAccounts, parent, aquisitions[i], NULL, NULL, false);
	}
	ticketUserEditInternal (parent, actorName, pNewSummary, pNewDescription, true);
	eaDestroy(&eaUniqueAccounts);
}

AUTO_TRANSACTION
ATR_LOCKS(parent, ".ppUserInfo, .uID, .uOccurrences, .uUniqueAccounts, .pSummary, .pUserDescription, .ppLog")
ATR_LOCKS(acquisition, ".ppUserInfo, .uID, .uMergedID, .eStatus, .eVisibility, .ppLog");
enumTransactionOutcome trMergeExistingTickets(ATR_ARGS, NOCONST(TicketEntryConst) *parent, NOCONST(TicketEntryConst) *acquisition, 
	const char *actorName, const char *pNewSummary, const char *pNewDescription, int bEditDescription)
{
	int i, size, j = 0, size2;
	int iAddedUserCount = 0;
	char *logString = NULL;
	size = eaSize(&acquisition->ppUserInfo);
	size2 = eaSize(&parent->ppUserInfo);

	for (i=0; i<size;)
	{
		if (j == size2)
		{
			NOCONST(TicketClientUserInfo) *pInfo = StructCloneNoConst(parse_TicketClientUserInfo, acquisition->ppUserInfo[i]);
			eaPush(&parent->ppUserInfo, pInfo);
			iAddedUserCount++;
			j++;
			size2++;
			i++;
		}
		else if (acquisition->ppUserInfo[i]->uFiledTime < parent->ppUserInfo[j]->uFiledTime)
		{
			NOCONST(TicketClientUserInfo) *pInfo = StructCloneNoConst(parse_TicketClientUserInfo, acquisition->ppUserInfo[i]);
			eaInsert(&parent->ppUserInfo, pInfo, j);
			iAddedUserCount++;
			j++;
			size2++;
			i++;
		}
		else
		{
			j++;
		}
	}

	acquisition->uMergedID = parent->uID;
	parent->uOccurrences += iAddedUserCount;
	parent->uUniqueAccounts += iAddedUserCount;
	if (bEditDescription)
	{
		SAFE_FREE(parent->pSummary);
		SAFE_FREE(parent->pUserDescription);
		parent->pSummary = StructAllocString(pNewSummary);
		parent->pUserDescription = StructAllocString(pNewDescription);
	}	
	acquisition->eStatus = TICKETSTATUS_MERGED;
	acquisition->eVisibility = TICKETVISIBLE_HIDDEN;

	estrPrintf(&logString, "Merged to Ticket #%d", parent->uID);
	trh_AddGenericLog(acquisition, actorName, logString);
	estrPrintf(&logString, "Merged in Ticket #%d", acquisition->uID);
	trh_AddGenericLog(parent, actorName, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ticketMergeExisting (const char *actorName, const char ***eaUniqueAccounts, TicketEntry *parent, TicketEntry *acquisition, 
						  const char *pNewSummary, const char *pNewDescription, bool bEditDescription)
{
	if (!parent || !acquisition || parent == acquisition)
		return;
	AutoTrans_trMergeExistingTickets(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, parent->uID, GLOBALTYPE_TICKETENTRY, acquisition->uID, 
		actorName, pNewSummary, pNewDescription, bEditDescription);
}

// NULL = remove all files
void removeTicketDataFile(TicketEntry *pEntry, const char *pFilename)
{
	int ret = 0;
	char filepath[MAX_PATH];
	GetTicketFileDir(pEntry->uID, SAFESTR(filepath));
	if (dirExists(filepath))	
	{
		if (pFilename)
		{
			strcat_s(SAFESTR(filepath), pFilename);
			if (fileExists(pFilename))
				fileForceRemove(filepath);
		}
		else
		{
			char cmdBuffer[512];
			sprintf(cmdBuffer, "rmdir /s /q %s", filepath);
			ret = system(cmdBuffer);
		}
	}
}

void removeTicketTrackerEntry (TicketEntry *pEntry)
{
	// For cases where they weren't removed already
	if (pEntry->pScreenshotFilename || pEntry->pDxDiagFilename || pEntry->pEntityFileName)
		removeTicketDataFile(pEntry, NULL);

	if (pEntry->pJiraIssue && pEntry->eVisibility != TICKETVISIBLE_HIDDEN)
		ticketChangeJiraCount(pEntry, pEntry->pJiraIssue->key, -1);
	searchRemoveTicket(pEntry);
	removeTicketFromGrid(pEntry);
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID);
}

AUTO_COMMAND ACMD_CATEGORY(Tickettracker_Debug);
void removeTicketTest(U32 uID)
{
	TicketEntry *pEntry = findTicketEntryByID(uID);
	if (pEntry)
		removeTicketTrackerEntry(pEntry);
}

void trNewTicket_CB(TransactionReturnVal *returnVal, TicketCreateData *data)
{
	NetLink *link = linkFindByID(data->linkID);
	bool bSuccess = false;
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		TicketEntry *pEntry;
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);

		pEntry = findTicketEntryByID(uID);
		if (pEntry)
		{
			addTicketToBucket(pEntry); // add to location tables
			searchAddTicket(pEntry); // add to optimized category search tables
			if (!nullStr(data->ticketData->pEntityStr) && pEntry->uEntityDescriptorID)
				TicketEntry_WriteEntity(pEntry, data->ticketData->pEntityStr);
			if (eaSize(&data->ticketData->ppUserDataStr))
			{
				TicketEntry_WriteUserData(pEntry, &data->ticketData->ppUserDataStr);
			}
			else if (data->ticketData->pUserDataStr)
			{
				STRING_EARRAY eaStrings = NULL;
				eaStackCreate(&eaStrings, 1);
				eaStrings[0] = data->ticketData->pUserDataStr;
				TicketEntry_WriteUserData(pEntry, &eaStrings);
				eaDestroy(&eaStrings);
			}
			timingHistoryPush(gSendHistory);

			if (data->ticketData->imageBuffer)
			{
				TicketEntry_WriteScreenshot(pEntry, data->ticketData);
			}
			if (link)
				SendTicketResponse(link, TICKETFLAGS_SUCCESS, STACK_SPRINTF("%d", pEntry->uID));
			if (pEntry->ppUserInfo && pEntry->ppUserInfo[0]->pPWAccountName && stricmp(pEntry->pMainCategory, "CBug.CategoryMain.GM") == 0)
			{
				RightNow_CreateIncident(CONTAINER_RECONST(TicketEntryConst, pEntry));
			}
			bSuccess = true;
		}
		else
			printf("\n%s - Could not find new entry %d.\n", StaticDefineIntRevLookup(GlobalTypeEnum, GLOBALTYPE_TICKETENTRY), uID);
	}
	if (!bSuccess && link)
		SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketFailure");
	if (data->ticketData)
		ticketDataDestroy(data->ticketData);
	free(data);
}

static TicketEntry * createTicketEntryFromTicketData(TicketData *pTicketData)
{
	TicketEntry *pEntry;
	NOCONST(TicketClientUserInfo) *info;
	int i;
	Category *category;
	char *logString = NULL;

	PERFINFO_AUTO_START_FUNC();
	pEntry = StructCreateNoConst(parse_TicketEntry);
	estrTrimLeadingAndTrailingWhitespace(&pTicketData->pMainCategory);
	estrTrimLeadingAndTrailingWhitespace(&pTicketData->pCategory);
	
	if (!CategoryConvert(pTicketData->pMainCategory, pTicketData->pCategory, &pEntry->pMainCategory, &pEntry->pCategory))
	{
		pEntry->pMainCategory = StructAllocString(pTicketData->pMainCategory);
		pEntry->pCategory = StructAllocString(pTicketData->pCategory);
	}

	category = getCategory(pEntry->pMainCategory, pEntry->pCategory);	
	if (category && category->bShowTicketsDefault)
		pEntry->eVisibility = TICKETVISIBLE_PRIVATE;
	else
		pEntry->eVisibility = TICKETVISIBLE_HIDDEN;

	pEntry->pSummary = StructAllocString(pTicketData->pSummary);
	pEntry->pUserDescription = StructAllocString(pTicketData->pUserDescription);

	info = StructCreateNoConst(parse_TicketClientUserInfo);
	pEntry->uLastTime = pEntry->uFiledTime = info->uFiledTime = info->uUpdateTime = timeSecondsSince2000();
	
	info->uAccountID = pTicketData->uAccountID;
	info->uCharacterID = pTicketData->uCharacterID;
	info->pAccountName = StructAllocString(pTicketData->pAccountName);
	info->pPWAccountName = StructAllocString(pTicketData->pPWAccountName);
	info->pDisplayName = StructAllocString(pTicketData->pDisplayName);
	info->pCharacterName = StructAllocString(pTicketData->pCharacterName);
	info->pShardInfoString = StructAllocString(pTicketData->pShardInfoString);
	info->pVersionString = StructAllocString(pTicketData->pVersionString);
	eaPush(&pEntry->ppUserInfo, info);
	
	pEntry->pPlatformName = StructAllocString(pTicketData->pPlatformName);
	pEntry->pProductName = allocAddString(pTicketData->pProductName);
	pEntry->pLabel = StructAllocString(pTicketData->pTicketLabel);

	estrPrintf(&logString, "Summary: %s",  pTicketData->pSummary);
	trh_AddCommunicationLog(pEntry, TICKETUSER_PLAYER, pTicketData->pAccountName, logString);
	estrPrintf(&logString, "Description: %s",  pTicketData->pUserDescription);
	trh_AddCommunicationLog(pEntry, TICKETUSER_PLAYER, pTicketData->pAccountName, logString);
	estrDestroy(&logString);
	TicketAutoResponse_ApplyRule(pEntry);

	pEntry->uFlags = pTicketData->uIsInternal;

	if (pTicketData->pTriviaList && pTicketData->pTriviaList->triviaDatas)
	{
		TriviaData *pTrivia;
		eaCopyStructs(&pTicketData->pTriviaList->triviaDatas, &((TriviaData**)(pEntry->ppTriviaData)), parse_TriviaData);
		pTrivia = triviaListFindEntry((TriviaList*) &pEntry->ppTriviaData, "playerPos");

		if (pTrivia)
			estrCopy2(&pEntry->pDebugPosString, pTrivia->pVal);
		ConvertDebugPosStringFromList((TriviaData**) pEntry->ppTriviaData, (TicketClientGameLocation*) &pEntry->gameLocation, true);
	}
	// -------------------------------------------------------------------------------
	// Character Info
	pEntry->bReadableEntity = false;
	PERFINFO_AUTO_START("Entity", 1);
	if (pTicketData->pEntityPTIStr && pTicketData->pEntityStr)
	{
		ParseTable **pEntityParseTables = NULL;
		const char *ptiName;
		int numTables = 0;
		
		pEntry->uEntityDescriptorID = addEntityDescriptor(pTicketData->pEntityPTIStr);

		ParseTableReadText(pTicketData->pEntityPTIStr, &pEntityParseTables, &numTables, &ptiName, 
			PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS);
		if (numTables)
		{
			// check to see if parse tables can be read
			pEntry->bReadableEntity = true;
		}
		ParseTableFree(&pEntityParseTables);
	}
	PERFINFO_AUTO_STOP();

	if (pTicketData->ppUserDataPTIStr && pTicketData->ppUserDataStr)
	{
		int userDataCount = min(eaSize(&pTicketData->ppUserDataPTIStr), eaSize(&pTicketData->ppUserDataStr));

		for (i=0; i<userDataCount; i++)
		{
			ParseTable **pParseTables = NULL;
			char *ptiName = NULL;
			int numTables = 0;

			ParseTableReadText(pTicketData->ppUserDataPTIStr[i], &pParseTables, &numTables, &ptiName, 
				PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS);
			if (numTables)
			{
				eaiPush(&pEntry->eaiUserDataDescriptorIDs, addEntityDescriptor(pTicketData->ppUserDataPTIStr[i]));
			}
			else
			{
				estrDestroy(&pTicketData->ppUserDataPTIStr[i]);
				eaRemove(&pTicketData->ppUserDataPTIStr, i);
			}
			ParseTableFree(&pParseTables);
		}
	}
	if (pTicketData->pUserDataPTIStr && pTicketData->pUserDataStr)
	{
		ParseTable **pParseTables = NULL;
		char *ptiName = NULL;
		int numTables = 0;

		ParseTableReadText(pTicketData->pUserDataPTIStr, &pParseTables, &numTables, &ptiName, 
			PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS);
		if (numTables)
			eaiPush(&pEntry->eaiUserDataDescriptorIDs, addEntityDescriptor(pTicketData->pUserDataPTIStr));
		else
			estrDestroy(&pTicketData->pUserDataStr);
		ParseTableFree(&pParseTables);
	}
	pEntry->eLanguage = pTicketData->eLanguage;

	// Server Info
	pEntry->iGameServerID = pTicketData->iServerContainerID;

	pEntry->eStatus = TICKETSTATUS_OPEN;
	pEntry->uOccurrences = pEntry->uUniqueAccounts = 1;
	pEntry->bIsInternal = pTicketData->uIsInternal;
	PERFINFO_AUTO_STOP_FUNC();
	return pEntry;
}

// ------------------------------------

static bool ticketUserIsSubscribed(TicketEntry *pEntry, TicketData *ticket)
{
	int i;
	if (!ticket->pAccountName)
		return true;
	for (i=eaSize(&pEntry->ppUserInfo)-1; i>=0; i--)
	{
		if (stricmp(pEntry->ppUserInfo[i]->pAccountName, ticket->pAccountName) == 0)
			return true;
	}
	return false;
}

static int ticketUserFindSubscribed(TicketEntry *pEntry, TicketData *ticket)
{
	int i;
	if (!ticket->pAccountName)
		return -1;
	for (i=eaSize(&pEntry->ppUserInfo)-1; i>=0; i--)
	{
		if (stricmp(pEntry->ppUserInfo[i]->pAccountName, ticket->pAccountName) == 0)
			return i;
	}
	return -1;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntry, ".uLastTime, .ppUserInfo");
enumTransactionOutcome trTicketResubscribe(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, int userInfoIndex)
{
	pEntry->uLastTime = pEntry->ppUserInfo[userInfoIndex]->uUpdateTime = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void ticketUserUpdateSubscribed(TicketEntry *pEntry, TicketData *ticket, int idx)
{
	AutoTrans_trTicketResubscribe(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, idx);
	// don't bother updating other stuff	
}

TicketEntry * ProcessIncomingTicket(Packet *pkt, NetLink *link, TicketClientState *pClientState)
{
	TicketData *pTicketData;
	TicketEntry *pEntry = NULL;
	//struct in_addr ina = {0};
 
	PERFINFO_AUTO_START_FUNC();
	pTicketData = getTicketDataFromPacket(pkt);
	if (!pTicketData)
	{
		SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketFailure");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	
	/*ina.S_un.S_addr = pTicketData->uIP;
	SERVLOG_PAIRS(LOG_TICKETDATA, "IncomingTicket", 
		("cryptic_account", "%s", pTicketData->pAccountName)
		("pwe_account", "%s", pTicketData->pPWAccountName)
		("mergeid", "%d", pTicketData->iMergeID)
		("shard", "%s", pTicketData->pShardInfoString ? GetShardValueFromInfoStringByKey(pTicketData->pShardInfoString, "name") : "Unknown")
		("lang", "%s", StaticDefineIntRevLookup(LanguageEnum, pTicketData->eLanguage))
		("maincat", "%s", pTicketData->pMainCategory)
		("subcat", "%s", pTicketData->pCategory ? pTicketData->pCategory : "None")
		("ip", "%s", inet_ntoa(ina)));*/
	if (pTicketData->iMergeID)
	{
		// If this fails, just treat it as a regular ticket
		pEntry = findTicketEntryByID(pTicketData->iMergeID);
	}

	if (pTicketData->uImageSize > 0 && pktCheckRemaining(pkt, 1))
	{
		pTicketData->imageBuffer = malloc(pTicketData->uImageSize);
		pktGetBytes(pkt, pTicketData->uImageSize, pTicketData->imageBuffer);
	}
	else
		pTicketData->imageBuffer = NULL;

	if (!pTicketData)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	if (pEntry)
	{
		int idx;
		idx = ticketUserFindSubscribed(pEntry, pTicketData);
		if (idx >= 0)
		{
			U32 uCurTime = timeSecondsSince2000();
			U32 uLastUpdateTime = pEntry->ppUserInfo[idx]->uUpdateTime ? pEntry->ppUserInfo[idx]->uUpdateTime : pEntry->ppUserInfo[idx]->uFiledTime;
			if (uCurTime - uLastUpdateTime < RESUBSCRIBE_MIN_TIME)
				SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketAlreadySubscribed");
			else
			{
				ticketUserUpdateSubscribed(pEntry, pTicketData, idx);
				SendTicketResponse(link, TICKETFLAGS_SUCCESS, "CTicket.TicketResubscribed");
			}
		}
		else
		{
			mergeDataIntoExistingTicket(linkID(link), pEntry, pTicketData);
		}
	}
	else
	{
		bool bMergedStuckKillme = false;
		if (stricmp(pTicketData->pTicketLabel, "stuck_killme_cmd") == 0)
		{
			TicketClientGameLocation location = {0};
			TicketEntry *pClosestTicket;
			ConvertDebugPosStringFromList((TriviaData**) pTicketData->pTriviaList->triviaDatas, &location, false);
			
			if (location.zoneName && location.zoneName[0])
			{
				pClosestTicket = findClosestStuckTicket(&location); // filter by position first
				if (pClosestTicket)
				{
					mergeDataIntoExistingTicket(linkID(link), pClosestTicket, pTicketData);
					bMergedStuckKillme = true;
				}
			} // otherwise it failed to parse the debug pos string
		}

		if (!bMergedStuckKillme)
			createTicket(pTicketData, linkID(link));
	}
	PERFINFO_AUTO_STOP_FUNC();
	return pEntry;
}

ContainerID createTicket(TicketData *pTicketData, U32 linkID)
{
	TicketEntry *pEntry;
	TicketCreateData *data;
	data = calloc(1, sizeof(TicketCreateData));
	data->linkID = linkID;
	data->ticketData = pTicketData;
	// New ticket
	pEntry = createTicketEntryFromTicketData(pTicketData);
	objRequestContainerCreateLocal(objCreateManagedReturnVal(trNewTicket_CB, data),
		GLOBALTYPE_TICKETENTRY, pEntry);
	StructDestroyNoConst(parse_TicketEntry, pEntry);
	return objContainerGetMaxID(GLOBALTYPE_TICKETENTRY);
}

void trMergeTicket_CB(TransactionReturnVal *returnVal, TicketCreateData *data)
{
	NetLink *link = linkFindByID(data->linkID);
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		TicketEntry *pEntry = findTicketEntryByID(data->mergeID);
		if (pEntry)
			SendTicketResponse(link, TICKETFLAGS_SUCCESS, STACK_SPRINTF("%d", pEntry->uID));
	}
	free(data);
}

AUTO_TRANSACTION
ATR_LOCKS(pEntry, ".uLastTime, .uOccurrences, .uUniqueAccounts, .ppUserInfo");
enumTransactionOutcome trMergeNewTicket(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, 
	const char *pAccountName, const char *pDisplayName, const char *pCharacterName, const char *pShardInfoString)
{
	NOCONST(TicketClientUserInfo) *info;
	bool bAccountInTicket = false;
	int i;
	info = StructCreateNoConst(parse_TicketClientUserInfo);

	pEntry->uLastTime = info->uFiledTime = info->uUpdateTime = timeSecondsSince2000();
	info->pAccountName = StructAllocString(pAccountName);
	info->pDisplayName = StructAllocString(pDisplayName);
	info->pCharacterName = StructAllocString(pCharacterName);
	info->pShardInfoString = StructAllocString(pShardInfoString);

	pEntry->uOccurrences++;
	for (i=eaSize(&pEntry->ppUserInfo)-1; i>=0; i--)
	{
		if (stricmp(pEntry->ppUserInfo[i]->pAccountName, pAccountName) == 0)
		{
			bAccountInTicket = true;
			break;
		}
	}
	if (!bAccountInTicket)
		pEntry->uUniqueAccounts++;
	eaPush(&pEntry->ppUserInfo, info);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void mergeDataIntoExistingTicket (U32 uLinkID, TicketEntry *pEntry, TicketData *pTicketData)
{
	TicketCreateData *data;
	if (!pEntry || !pTicketData || !pTicketData->pAccountName)
		return;
	data = malloc(sizeof(TicketCreateData));
	data->linkID = uLinkID;
	data->mergeID = pEntry->uID;
	data->ticketData = NULL;
	AutoTrans_trMergeNewTicket(objCreateManagedReturnVal(trMergeTicket_CB, data), objServerType(), 
		GLOBALTYPE_TICKETENTRY, pEntry->uID, pTicketData->pAccountName, pTicketData->pDisplayName, pTicketData->pCharacterName, pTicketData->pShardInfoString);
}

bool hashMatchesU32(U32 *h1, U32 *h2)
{
	return (
		(h1[0] == h2[0])
	&&  (h1[1] == h2[1])
	&&  (h1[2] == h2[2])
	&&  (h1[3] == h2[3]));
}

// ------------------------------------
// Network Callbacks
NetComm *ticketTrackerCommDefault(void)
{
	static NetComm	*comm;

	if (!comm)
		comm = commCreate(0,1);
	return comm;
}

static int TicketClientConnect(NetLink* link, TicketClientState *pClientState)
{
	return 1;
}

static int TicketClientDisconnect(NetLink* link, TicketClientState *pClientState)
{
	return 1;
}

int TicketResponseSortCountDesc (const TicketRequestResponse **pEntry1, const TicketRequestResponse **pEntry2, const void *ign)
{
	if      ((*pEntry1)->uCount < (*pEntry2)->uCount) return  1;
	else if ((*pEntry1)->uCount > (*pEntry2)->uCount) return -1;
	else return 0;
}

#define TICKET_LABELREQUEST_MAXSIZE 10
static void TicketRequest_ConstructSearchFilter(SearchData *psd, TicketRequestData *pTicketData, bool bSetAccountFilter)
{
	if (bSetAccountFilter)
	{
		if (pTicketData->pAccountName && *pTicketData->pAccountName)
		{
			psd->uFlags = SEARCHFLAG_ACCOUNT_NAME;
			psd->pAccountName = (char*) pTicketData->pAccountName;
			psd->bExactAccountName = true;
		}
		if (pTicketData->pCharacterName && *pTicketData->pCharacterName)
		{
			psd->uFlags |= SEARCHFLAG_CHARACTER_NAME;
			psd->pCharacterName = (char*) pTicketData->pCharacterName;
			psd->bExactCharacterName = true;
		}
	}
	if (pTicketData->pProduct && *pTicketData->pProduct)
	{
		psd->uFlags |= SEARCHFLAG_PRODUCT_NAME;
		psd->pProductName = allocAddString(pTicketData->pProduct);
	}
	if (pTicketData->pCategory && *pTicketData->pCategory)
	{
		if (!pTicketData->pMainCategory)
		{
			char *main = NULL, *sub = NULL;
			if (CategoryConvert(pTicketData->pMainCategory, pTicketData->pCategory, &main, &sub))
			{
				free((char*) pTicketData->pCategory);
				if (pTicketData->pMainCategory)
					free((char*) pTicketData->pMainCategory);
				pTicketData->pMainCategory = strdup(main);
				pTicketData->pCategory = strdup(sub);
				psd->uFlags |= SEARCHFLAG_CATEGORY;
				psd->iMainCategory = categoryGetIndex(main);
				psd->iCategory = subcategoryGetIndex(psd->iMainCategory, sub);
			}
			estrDestroy(&main);
			estrDestroy(&sub);
		}
		else
		{
			psd->uFlags |= SEARCHFLAG_CATEGORY;
			psd->iMainCategory = categoryGetIndex(pTicketData->pMainCategory);
			psd->iCategory = subcategoryGetIndex(psd->iMainCategory, pTicketData->pCategory);
		}
	}
	if (pTicketData->pLabel && *pTicketData->pLabel)
	{
		psd->uFlags |= SEARCHFLAG_LABEL;
		psd->pLabel = (char*) pTicketData->pLabel;
	}
	if (pTicketData->pKeyword && *pTicketData->pKeyword)
	{
		psd->uFlags |= SEARCHFLAG_SUMMARY_DESCRIPTION;
		psd->pSummaryDescription = (char*) pTicketData->pKeyword;
		psd->iSearchSummaryDescription = SEARCH_SUMMARY_DESCRIPTION;
	}
	if (pTicketData->accessLevel == 9)
		psd->eAdminSearch = SEARCH_ADMIN_GAME;
	if (pTicketData->pShardName && *pTicketData->pShardName)
	{
		psd->uFlags |= SEARCHFLAG_SHARD;
		psd->pShardExactName = (char*) pTicketData->pShardName;
	}
}

#define MAX_GAMECLIENT_DESCRIPTION_LEN 500
static void CopyTicketToResponseStruct(TicketEntry *src, TicketRequestResponse *dst, const char *accountName)
{
	if (src && dst)
	{
		dst->uID = src->uID;
		dst->uCount = src->uOccurrences;
		dst->pMainCategory = StructAllocString(src->pMainCategory);
		dst->pCategory = StructAllocString(src->pCategory);
		dst->pSummary = StructAllocString(src->pSummary);
		if (src->pUserDescription && strlen(src->pUserDescription) > MAX_GAMECLIENT_DESCRIPTION_LEN)
		{
			dst->pDescription = malloc(MAX_GAMECLIENT_DESCRIPTION_LEN);
			strncpy_s(dst->pDescription, MAX_GAMECLIENT_DESCRIPTION_LEN, src->pUserDescription, MAX_GAMECLIENT_DESCRIPTION_LEN-4);
			dst->pDescription[MAX_GAMECLIENT_DESCRIPTION_LEN-4] = dst->pDescription[MAX_GAMECLIENT_DESCRIPTION_LEN-3] = dst->pDescription[MAX_GAMECLIENT_DESCRIPTION_LEN-2] = '.';
			dst->pDescription[MAX_GAMECLIENT_DESCRIPTION_LEN-1] = '\0';
		}
		else
			dst->pDescription = StructAllocString(src->pUserDescription);
		dst->pStatus = StructAllocString(getStatusString(src->eStatus));

		dst->uFiledTime = src->ppUserInfo ? src->ppUserInfo[0]->uFiledTime : 0;
		dst->uLastTime = src->uLastTime;
		dst->bVisible = src->eVisibility == TICKETVISIBLE_PUBLIC;
		dst->uSubscribedAccounts = src->uUniqueAccounts;

		dst->pLabel = StructAllocString(src->pLabel);
		dst->pResponse = StructAllocString(src->pResponseToUser);

		if (stricmp(dst->pMainCategory, "CBug.CategoryMain.Stickies") == 0)
			dst->bSticky = true;
					
		if (accountName)
		{
			int i, size = eaSize(&src->ppUserInfo);
			for (i=0; i<size; i++)
			{
				if (src->ppUserInfo[i]->pAccountName && stricmp(accountName, src->ppUserInfo[i]->pAccountName) == 0)
				{
					dst->pCharacter = StructAllocString(src->ppUserInfo[i]->pCharacterName);
					dst->bIsSubscribed = true;
					break;
				}
			}
		}
		else
		{
			if (src->ppUserInfo && src->ppUserInfo[0]->pCharacterName)
				dst->pCharacter = StructAllocString(src->ppUserInfo[0]->pCharacterName);
			else
				dst->pCharacter = StructAllocString("Unknown");
		}
	}
}

int TicketHandleLabelRequest (NetLink *link, TicketClientState *pClientState, TicketRequestData *pTicketData)
{
	SearchData sd = {0};
	TicketEntry *pEntry;
	TicketRequestResponseList ticketResponseList = {0};
	U32 uCurTime = timeSecondsSince2000();

	TicketEntry **ppEntries = NULL;
	TicketClientGameLocation location = {0};
	bool bLocationFilter = false;

	if (pTicketData->pDebugPosString && pTicketData->pDebugPosString[0] && 
		pTicketData->pCategory && strstri(pTicketData->pCategory, "Environment"))
	{
		ConvertDebugPosString(pTicketData->pDebugPosString, &location);
		findNearbyTickets(&ppEntries, &location); // filter by position first
		bLocationFilter = true;
	}

	TicketRequest_ConstructSearchFilter(&sd, pTicketData, false);
	// TODO sorting?
	
	if (bLocationFilter)
		pEntry = searchFirstEx(&sd, ppEntries);
	else
		pEntry = searchFirst(&sd);
	while(pEntry != NULL)
	{
		// Omit if it's closed AND too old (two weeks)
		if (!(TICKET_STATUS_IS_CLOSED(pEntry->eStatus) && pEntry->uEndTime + STATUS_REQUEST_MAX_AGE < uCurTime))
		{
			TicketRequestResponse *pTicketResponse = StructCreate(parse_TicketRequestResponse);
			CopyTicketToResponseStruct(pEntry, pTicketResponse, pTicketData->pAccountName);
			eaPush(&ticketResponseList.ppTickets, pTicketResponse);
		}
		pEntry = searchNext(&sd);
	}
	searchEnd(&sd);
	StructDeInit(parse_SearchData, &sd);

	{
		int size;
		// Sort by count and limit number
		TicketResponse_CalculateAndSortByPriority(ticketResponseList.ppTickets);
		size = eaSize(&ticketResponseList.ppTickets);
		if (size > TICKET_LABELREQUEST_MAXSIZE)
		{
			int i, count = 0;
			const char *pOpenString = getStatusString(TICKETSTATUS_OPEN);
			for (i=0; i<size; i++)
			{
				if (!ticketResponseList.ppTickets[i]->bSticky && stricmp(ticketResponseList.ppTickets[i]->pStatus, pOpenString) == 0)
				{
					count++;
					if (count >= TICKET_LABELREQUEST_MAXSIZE)
					{
						i++;
						break;
					}
				}
			}
			for (; i<size; i++)
			{
				TicketRequestResponse *response = eaPop(&ticketResponseList.ppTickets);
				if (response)
					StructDestroy(parse_TicketRequestResponse, response);
			}
		}
	}

	{
		char *pOutputParse = NULL;
		Packet *pak = link ? pktCreate(link, FROM_TICKETTRACKER_STATUS) : NULL;
		TicketRequestResponseWrapper *pWrapper = StructCreate(parse_TicketRequestResponseWrapper);

		ParserWriteTextSafe(&pWrapper->pListString, &pWrapper->pTPIString, &pWrapper->uCRC, 
			parse_TicketRequestResponseList, &ticketResponseList, 0, 0, 0);
		ParserWriteText(&pOutputParse, parse_TicketRequestResponseWrapper, pWrapper, 0, 0, 0);
		StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		
		if(link)
		{
			pktSendU32(pak, pTicketData->uID);
			pktSendString(pak, pOutputParse);
			pktSend(&pak);
		}
		estrDestroy(&pOutputParse);
	}
	eaDestroyStruct(&ticketResponseList.ppTickets, parse_TicketRequestResponse);
	timingHistoryPush(gSearchHistory);
	return 1;
}

int TicketHandleStatusRequest (NetLink *link, TicketClientState *pClientState, TicketRequestData *pTicketData)
{
	SearchData sd = {0};
	TicketEntry *pEntry;
	TicketRequestResponseList ticketResponseList = {0};
	U32 uCurTime = timeSecondsSince2000();

	TicketRequest_ConstructSearchFilter(&sd, pTicketData, true);
	sd.eAdminSearch = SEARCH_USER; // this will search for all tickets for the user, visible and hidden
	pEntry = searchFirst(&sd);
	while(pEntry != NULL)
	{
		// Omit if it's closed AND too old (two weeks)
		if (!(TICKET_STATUS_IS_CLOSED(pEntry->eStatus) && pEntry->uEndTime + STATUS_REQUEST_MAX_AGE < uCurTime))
		{
			TicketRequestResponse *pTicketResponse = StructCreate(parse_TicketRequestResponse);

			CopyTicketToResponseStruct(pEntry, pTicketResponse, pTicketData->pAccountName);

			eaPush(&ticketResponseList.ppTickets, pTicketResponse);
		}
		pEntry = searchNext(&sd);
	}
	searchEnd(&sd);
	StructDeInit(parse_SearchData, &sd);

	{
		int size;
		// Sort by count and limit number
		TicketResponse_CalculateAndSortByPriority(ticketResponseList.ppTickets);
		size = eaSize(&ticketResponseList.ppTickets);
		if (size > TICKET_LABELREQUEST_MAXSIZE)
		{
			int i, count = 0;
			const char *pOpenString = getStatusString(TICKETSTATUS_OPEN);
			for (i=0; i<size; i++)
			{
				if (stricmp(ticketResponseList.ppTickets[i]->pStatus, pOpenString) == 0)
				{
					count++;
					if (count >= TICKET_LABELREQUEST_MAXSIZE)
					{
						i++;
						break;
					}
				}
			}
			for (; i<size; i++)
			{
				TicketRequestResponse *response = eaPop(&ticketResponseList.ppTickets);
				if (response)
					StructDestroy(parse_TicketRequestResponse, response);
			}
		}
	}

	{
		char *pOutputParse = NULL;
		Packet *pak = pktCreate(link, FROM_TICKETTRACKER_STATUS);
		TicketRequestResponseWrapper *pWrapper = StructCreate(parse_TicketRequestResponseWrapper);

		ParserWriteTextSafe(&pWrapper->pListString, &pWrapper->pTPIString, &pWrapper->uCRC, 
			parse_TicketRequestResponseList, &ticketResponseList, 0, 0, 0);
		ParserWriteText(&pOutputParse, parse_TicketRequestResponseWrapper, pWrapper, 0, 0, 0);
		StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		
		pktSendU32(pak, pTicketData->uID);
		pktSendString(pak, pOutputParse);
		pktSend(&pak);
		estrDestroy(&pOutputParse);
	}
	eaDestroyStruct(&ticketResponseList.ppTickets, parse_TicketRequestResponse);
	return 1;
}

static void TicketHandleMsg(Packet *pak,int cmd, NetLink *link, TicketClientState *pClientState)
{
	switch(cmd)
	{
	xcase (TO_TICKETTRACKER_ERROR):
		PERFINFO_AUTO_START("Ticket Create", 1);
		{
			TicketEntry *pEntry = ProcessIncomingTicket(pak, link, pClientState);
		}
		PERFINFO_AUTO_STOP();
	xcase (TO_TICKETTRACKER_REQUEST_STATUS):
		PERFINFO_AUTO_START("Ticket Request Status", 1);
		{
			TicketRequestData *pTicketData = StructCreate(parse_TicketRequestData);
			char *pParseString = pktGetStringTemp(pak);

			if(!ParserReadText(pParseString, parse_TicketRequestData, pTicketData, 0))
			{
				StructDestroy(parse_TicketRequestData, pTicketData);
				return; // TODO send error
			}
			// TODO(Theo) Use RightNow searching or not?
			/*if (pTicketData->pPWAccountName && *pTicketData->pPWAccountName)
				RightNow_UserQueryIncidents(linkID(link), pTicketData);
			else*/
			TicketHandleStatusRequest(link, pClientState, pTicketData);
		}
		PERFINFO_AUTO_STOP();
	xcase (TO_TICKETTRACKER_REQUEST_LABEL):
		PERFINFO_AUTO_START("Ticket Request Search", 1);
		{
			TicketRequestData *pTicketData = StructCreate(parse_TicketRequestData);
			char *pParseString = pktGetStringTemp(pak);

			if(!ParserReadText(pParseString, parse_TicketRequestData, pTicketData, 0))
			{
				StructDestroy(parse_TicketRequestData, pTicketData);
				return; // TODO send error
			}
			TicketHandleLabelRequest(link, pClientState, pTicketData);
		}
		PERFINFO_AUTO_STOP();
	xcase (TO_TICKETTRACKER_TICKET_EDIT):
	{
		TicketData *pTicketData = getTicketDataFromPacket(pak);
		if (pTicketData)
		{
			TicketEntry *pEntry = findTicketEntryByID(pTicketData->iMergeID);
			ticketUserEdit(link, pEntry, pTicketData->pAccountName, pTicketData->pSummary, pTicketData->pUserDescription, false);
			StructDestroy(parse_TicketData, pTicketData);
		}
		else
		{
			SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketFailure");
		}
	}
	xcase (TO_TICKETTRACKER_TICKET_CLOSE):
	{
		TicketData *pTicketData = getTicketDataFromPacket(pak);
		if (pTicketData)
		{
			TicketEntry *pEntry = findTicketEntryByID(pTicketData->iMergeID);
			ticketUserClose(link, pEntry, pTicketData->pAccountName);
			StructDestroy(parse_TicketData, pTicketData);
		}
		else
		{
			SendTicketResponse(link, TICKETFLAGS_ERROR, "CTicket.TicketFailure");
		}
	}
	xdefault:
		;
		//printf("IncomingData: Unknown command %d\n",cmd);
	}
}

bool initIncomingData(void)
{
	NetListen *pLocal_listen, *pPublic_listen;
	int val;
	for(;;)
	{
		val = commListenBoth(ticketTrackerCommDefault(), LINKTYPE_UNSPEC, LINK_NO_COMPRESS, DEFAULT_TICKET_TRACKER_PORT,
			TicketHandleMsg, TicketClientConnect, TicketClientDisconnect, sizeof(TicketClientState), 
			&pLocal_listen, &pPublic_listen);
		if (val)
			break;
		Sleep(1);
	}

	return true;
}

// ------------------------------------
// Fully Qualified Domain Name
char gFQDN[MAX_PATH] = "";
AUTO_CMD_STRING(gFQDN, FQDN);
const char * getMachineAddress(void)
{
	static char *pMachineAddress = NULL;
	if(pMachineAddress == NULL)
	{
		if(gFQDN[0] != 0)
		{
			estrCopy2(&pMachineAddress, gFQDN);
		}
		else
		{
			estrForceSize(&pMachineAddress, 128);
			gethostname(pMachineAddress, 127);
		}
	}
	return pMachineAddress;
}

static U32 suOptions = 0;

U32 ticketTrackerGetOptions(void)
{
	return suOptions;
}

void ticketTrackerSetOptions(U32 uOptions)
{
	suOptions = uOptions;
}

void ticketTrackerAddOptions(U32 uOptions)
{
	suOptions |= uOptions;
}

static bool sDeleteTicketLogs = false;
AUTO_CMD_INT(sDeleteTicketLogs, DeleteLogs) ACMD_CMDLINE;

AUTO_TRANSACTION ATR_LOCKS(entry, ".ppLog");
enumTransactionOutcome trDestroyLogs(ATR_ARGS, NOCONST(TicketEntryConst) *entry)
{
	eaDestroyStructNoConst(&entry->ppLog, parse_TicketLog);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(entry, ".eaUserDataStrings");
enumTransactionOutcome trDestroyUserData(ATR_ARGS, NOCONST(TicketEntryConst) *entry)
{
	eaDestroyEx(&entry->eaUserDataStrings, StructFreeString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void performMigration()
{
	U32 uTotalTicketsUpdated = 0;

	PERFINFO_AUTO_START_FUNC();

	loadstart_printf("Scanning tickets... ");

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_TICKETENTRY, ticketContainer);
	{
		TicketEntry *ticket = (TicketEntry*) ticketContainer->containerData;
		bool bUpdated = false;

		if (!devassert(ticket))
			continue;

		autoTimerThreadFrameBegin(__FUNCTION__);

		if (ticket->pScreenshotFilename || ticket->pEntityFileName || ticket->pDxDiagFilename)
		{
			if (ticket->pScreenshotFilename && strstri(ticket->pScreenshotFilename, ".txt") != NULL)
				objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, ticket->uID, "ChangeScreenshot", 
					"set .pScreenshotFilename = \"%d_ss.jpg\"", ticket->uID);
			else if (ticket->pEntityFileName && strstri(ticket->pEntityFileName, "m/") == NULL || 
				ticket->pScreenshotFilename && strstri(ticket->pScreenshotFilename, "m/") == NULL || 
				ticket->pDxDiagFilename && strstri(ticket->pDxDiagFilename, "m/") == NULL)
			{
				bUpdated = true;
				if (ticket->pScreenshotFilename)
					objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, ticket->uID, "ChangeScreenshot", 
						"set .pScreenshotFilename = \"%s%d_ss.jpg\"", GetTicketFileRelativePath(ticket->uID, ""), ticket->uID);
				if (ticket->pEntityFileName)
					objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, ticket->uID, "ChangeEntity", 
						"set .pEntityFileName = \"%s%d_entity.txt\"", GetTicketFileRelativePath(ticket->uID, ""), ticket->uID);
				if (ticket->pDxDiagFilename)
					objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, ticket->uID, "ChangeDxDiag", 
						"set .pDxDiagFilename = \"%s%d_dxdiag.txt\"", GetTicketFileRelativePath(ticket->uID, ""), ticket->uID);
			}
		}

		if (eaSize(&ticket->eaUserDataStrings))
		{
			if (!ticket->pUserDataFilename)
				TicketEntry_WriteUserData(ticket, &ticket->eaUserDataStrings);
			if (ticket->pUserDataFilename)
				AutoTrans_trDestroyUserData(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, ticket->uID);
			bUpdated = true;
		}

		if (sDeleteTicketLogs && eaSize(&ticket->ppLog))
		{
			AutoTrans_trDestroyLogs(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, ticket->uID);
			bUpdated = true;
		}

		if (bUpdated)
		{
			uTotalTicketsUpdated++;
			UpdateObjectTransactionManager();
		}

		autoTimerThreadFrameEnd();
	}
	CONTAINER_FOREACH_END;
	printf("Migration: %d tickets updated\n", uTotalTicketsUpdated);
}

static void initializeTickets(void)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	initializeTicketLocation(); // initialize Ticket Location stash
	RightNowPush_Init();

	objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
	currCon = objGetNextContainerFromIterator(&iter);

	while (currCon)
	{
		TicketEntry *pEntry = CONTAINER_ENTRY(currCon);

		addTicketToBucket(pEntry);
		if (pEntry->eVisibility != TICKETVISIBLE_HIDDEN && pEntry->pJiraIssue && pEntry->pJiraIssue->key)
		{
			ticketChangeJiraCount(pEntry, pEntry->pJiraIssue->key, 1);
		}
		if (pEntry->pSolutionKey)
			ticketChangeSolutionCount(pEntry, pEntry->pSolutionKey, 1);
		if (pEntry->pPhoneResKey)
			ticketChangeSolutionCount(pEntry, pEntry->pPhoneResKey, 1);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

extern bool gbCreateSnapshotMode;
void ticketTrackerInit()
{
	TicketEntry tentry = {0};
	int toMigrate = 0;
	
	//setDefaultProductionMode(true);
	setEntityDescriptorContainers(true);
	TicketTrackerDBInit();
	if (gbCreateSnapshotMode)
		return;
	
	LoadAllTicketTrackerConfigFiles();
	TTSavedValues_Load();
	ticketInitializeJiraMappings();
	ATR_DoLateInitialization();

	loadstart_printf("Migrating old data...         ");
	performMigration();
	loadend_printf("Done.");

	loadstart_printf("Opening incoming data port... ");
	initIncomingData();
	loadend_printf("Now accepting tickets.");

	loadstart_printf("Opening web interface...      ");
	initWebInterface();
	loadend_printf("Now accepting requests.");

	loadstart_printf("Initializing other Ticket-related things...");
	initializeTickets();
	loadend_printf("Done.");

	printf("Ticket Tracker Web Address: http://%s/\n", getMachineAddress());
}

// Call this on startup if you want to test the Ticket Tracker's throughput capability
void ticketTrackerTimingTest()
{
	TicketRequestData request = {0};
	int i;

	request.pCategory = "Cbug.Category.Mission";
	
	for(i = 0; i < 10000; ++i)
	{
		TicketHandleLabelRequest(NULL, NULL, &request);
	}

	printf("Most searches in 1.0 seconds: %d", timingHistoryMostInInterval(gSearchHistory, 1.0));
}

void ticketTrackerShutdown(void)
{	
	objContainerSaveTick();
	objForceRotateIncrementalHog();
	objFlushContainers();
	objCloseAllWorkingFiles();
	objCloseContainerSource();
	shutdownWebInterface();
}

// ------------------------------------
// Periodic Functions
void EveryFewMinutesCheck(void);
void HourlyCheck(void);
void NightlyCheck(void);

void ticketTrackerOncePerFrame(void)
{
	commMonitor(ticketTrackerCommDefault());
	updateWebInterface();

	UpdateObjectTransactionManager();
	objContainerSaveTick();

	EveryFewMinutesCheck();
	HourlyCheck();
	NightlyCheck();
	resortPriorities();
}

void EveryFewMinutesActivities(void)
{
	// does nothing at the moment
	/*char time[32];
	timeMakeLocalTimeStringFromSecondsSince2000(time, timeSecondsSince2000());
	printf("EveryFewMinutesActivities: %s\n", time);

	printf("Average searches in 60.0 seconds: %0.2f\n", timingHistoryAverageInInterval(gSearchHistory, 60.0));
	printf("Most searches in 60.0 seconds: %d\n", timingHistoryMostInInterval(gSearchHistory, 60.0));
	printf("Average sends in 60.0 seconds: %0.2f\n", timingHistoryAverageInInterval(gSendHistory, 60.0));
	printf("Most sends in 60.0 seconds: %d\n", timingHistoryMostInInterval(gSendHistory, 60.0));*/
}

void HourlyActivities(void)
{
	/*char time[32];
	timeMakeLocalTimeStringFromSecondsSince2000(time, timeSecondsSince2000());
	printf("HourlyActivities: %s\n", time);*/
	UpdateTicketsFromRightNow();
}

static TicketEntry **sppJiraToUpdate = NULL;
static DWORD WINAPI TicketUpdateJiras(LPVOID lpParam)
{
	static NetComm *sJiraUpdateComm = NULL;
	int i, size = eaSize(&sppJiraToUpdate);
	NOCONST(JiraIssue) jiraCopy = {0};

	EXCEPTION_HANDLER_BEGIN
	if (!sJiraUpdateComm)
		sJiraUpdateComm = commCreate(0,0);

	for (i=0; i<size; i++)
	{
		NOCONST(JiraIssue)* jira = sppJiraToUpdate[i]->pJiraIssue;
		estrCopy2(&jiraCopy.key, jira->key);
		if (jira->assignee)
			estrCopy2(&jiraCopy.assignee, jira->assignee);
		else
			estrDestroy(&jiraCopy.assignee);
		jiraCopy.status = jira->status;
		
		sppJiraToUpdate[i]->pJiraIssue->prevStatus = sppJiraToUpdate[i]->pJiraIssue->status;
		sppJiraToUpdate[i]->pJiraIssue->prevResolution = sppJiraToUpdate[i]->pJiraIssue->resolution;
		jiraGetIssue((JiraIssue*) (&jiraCopy), sJiraUpdateComm);
		if (stricmp_safe(jiraCopy.assignee, jira->assignee) != 0)
		{
			if (jiraCopy.assignee)
				estrCopy2(&jira->assignee, jiraCopy.assignee);
			else
				estrDestroy(&jira->assignee);
			sppJiraToUpdate[i]->bJiraIsDirty = true;
		}
		if (jiraCopy.status != jira->status)
		{
			jira->status = jiraCopy.status;
			sppJiraToUpdate[i]->bJiraIsDirty = true;
		}
		if (jiraCopy.resolution != jira->resolution)
		{
			jira->resolution = jiraCopy.resolution;
			sppJiraToUpdate[i]->bJiraIsDirty = true;
		}
	}
	eaDestroy(&sppJiraToUpdate);
	EXCEPTION_HANDLER_END
	return 0;
}

void NightlyJiraActivities(void)
{
	ContainerIterator iter = {0};
	Container *con;
	DWORD dummy;
	
	if (!jiraDefaultLogin())
		return;
	eaClear(&sppJiraToUpdate);
	objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while(con)
	{
		TicketEntry *pEntry = CONTAINER_ENTRY(con);
		if (pEntry->eStatus != TICKETSTATUS_MERGED && pEntry->pJiraIssue)
		{
			eaPush(&sppJiraToUpdate, pEntry);
		}
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	CloseHandle((HANDLE) _beginthreadex(0, 0, TicketUpdateJiras, 0, 0, &dummy));
}

#define TICKET_AGE_CUTOFF (45 * 24 * 60 * 60) // 45 days, in seconds
#define TICKET_RESPONSEWAIT_CUTOFF (72 * 60 * 60) // 72 hours in seconds
#define TICKET_RESOLVED_CUTOFF (14 * 24 * 60 * 60) // 14 days
#define TICKET_AUTORESPONSE_PROCESS_TIME (72 * 60 * 60) // 72 hours
#define TICKET_KEEP_SCREENSHOT_TIME (48 * 60 * 60) // 48 hours

void NightlyActivities(void)
{
	char time[32];
	ContainerIterator iter = {0};
	Container *con;
	int updateCount = 0, removeCount = 0;
	U32 curTime = timeSecondsSince2000();
	TicketEntryList toRemove = {0};
	
	timeMakeLocalTimeStringFromSecondsSince2000(time, curTime);
	printf("NightlyActivities: %s\n", time);

	objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while(con)
	{
		TicketEntry *pEntry = CONTAINER_ENTRY(con);

		if (gbAutoResponseEnabled && 
			((pEntry->eStatus == TICKETSTATUS_OPEN || pEntry->eStatus == TICKETSTATUS_IN_PROGRESS) && 
			 !pEntry->pRepAccountName ))
		{ // Open or In Progress, Not Assigned
			if ((!pEntry->pResponseToUser || !*pEntry->pResponseToUser) &&
				stricmp(pEntry->pMainCategory, "CBug.CategoryMain.GameSupport") == 0)// Add response
			{
				pEntry->pResponseToUser = strdup(langTranslateMessageKeyDefault(pEntry->eLanguage, 
					"NoProduct_DefaultResponse", TICKET_AUTORESPONSE_TEXT));
				pEntry->uResponseTime = timeSecondsSince2000();
				pEntry->bAutoResponse = true;
			}
			else if (pEntry->bAutoResponse && curTime > (pEntry->uResponseTime + TICKET_AUTORESPONSE_PROCESS_TIME))
			{ // Has Auto-response and is 72 Hours after auto-response
				ticketEntryChangeStatus(NULL, pEntry, TICKETSTATUS_PROCESSED);
				if (pEntry->pResponseToUser)
					free(pEntry->pResponseToUser);
				pEntry->pResponseToUser = strdup(langTranslateMessageKeyDefault(pEntry->eLanguage, 
					"Ticket_AutoProcess", TICKET_AUTOPROCESS_TEXT));
			}
		}
		else if (pEntry->eStatus != TICKETSTATUS_MERGED && pEntry->pJiraIssue && pEntry->pJiraIssue->prevStatus)
		{
			if (pEntry->pJiraIssue->status != pEntry->pJiraIssue->prevStatus)
			{
				TicketStatus eStatus = ticketStatusFromJira(pEntry->pJiraIssue->status);
				TicketResolution eResolution = ticketResolutionFromJira(pEntry->pJiraIssue->resolution);

				if (eStatus != TICKETSTATUS_UNKNOWN)
					ticketEntryChangeStatus(0, pEntry, eStatus);
				ticketEntryChangeResolution(0, pEntry, eResolution);
				updateCount++;
			}
			else if (pEntry->pJiraIssue->resolution != pEntry->pJiraIssue->prevResolution)
			{
				TicketResolution eResolution = ticketResolutionFromJira(pEntry->pJiraIssue->resolution);
				ticketEntryChangeResolution(0, pEntry, eResolution);
			}

			pEntry->pJiraIssue->prevResolution = pEntry->pJiraIssue->prevStatus = 0;
		}
		else if (TICKET_STATUS_IS_CLOSED(pEntry->eStatus))
		{
			if (pEntry->eVisibility == TICKETVISIBLE_PUBLIC && pEntry->uEndTime + TICKET_RESOLVED_CUTOFF < curTime)
			{
				// Change to private
				changeTicketTrackerEntryVisible("Automatic Resolve Hide", pEntry, TICKETVISIBLE_PRIVATE);
			}
			if  (pEntry->uEndTime + TICKET_AGE_CUTOFF < curTime)
			{
				if (pEntry->pScreenshotFilename || pEntry->pDxDiagFilename || pEntry->pEntityFileName)
				{
					removeTicketDataFile(pEntry, NULL);
					if (pEntry->pScreenshotFilename)
						objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "RemoveScreenshot", "destroy .pScreenshotFilename");
					if (pEntry->pDxDiagFilename)
						objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "RemoveDxDiag", "destroy .pDxDiagFilename");
					if (pEntry->pEntityFileName)
						objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "RemoveEntity", "destroy .pEntityFileName");
				}
				eaPush(&toRemove.ppEntries, (TicketEntryConst*) pEntry);
				removeCount++;
			}
			else if  (pEntry->uEndTime + TICKET_KEEP_SCREENSHOT_TIME < curTime && pEntry->pScreenshotFilename)
			{
				removeTicketDataFile(pEntry, pEntry->pScreenshotFilename);
				objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "RemoveScreenshot", "destroy .pScreenshotFilename");
				ticketAddGenericLog(pEntry, NULL, "Screenshot purged - Ticket closed for >48 hours.");
			}
		}
		else if (pEntry->eStatus == TICKETSTATUS_PENDING)
		{
			int size = eaSize(&pEntry->ppStatusLog);
			if (size && pEntry->ppStatusLog[size-1]->eStatus == TICKETSTATUS_PENDING &&
				pEntry->ppStatusLog[size-1]->uTime + TICKET_RESPONSEWAIT_CUTOFF < curTime)
			{
				ticketEntryChangeStatus("No Customer Response", pEntry, TICKETSTATUS_RESOLVED);
			}
		}
		if (pEntry->bJiraIsDirty)
			pEntry->bJiraIsDirty = false;
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	printf("Removing %d old tickets... \n", removeCount);
	if (toRemove.ppEntries)
	{
		int i;
		for (i=eaSize(&toRemove.ppEntries)-1; i>=0; i--)
		{
			char datetime[128];
			timeMakeDateStringFromSecondsSince2000(datetime, toRemove.ppEntries[i]->uEndTime);
			filelog_printf(gLogFileName, "Ticket Cleanup - ID #%d, End Time: %s", toRemove.ppEntries[i]->uID, datetime);
			removeTicketTrackerEntry((TicketEntry*) toRemove.ppEntries[i]);
		}
		eaDestroy(&toRemove.ppEntries);
	}
}

// ------------------------------------
// Periodic Checks
#define EVERY_FEW_MINUTES_IN_SECONDS 300 // Check every 5 minutes

void EveryFewMinutesCheck(void)
{
	static int lastSecondChecked = -1;
	int iTime = timeSecondsSince2000();

	// Lazily init lastSecondChecked
	if(lastSecondChecked == -1)
	{
		lastSecondChecked = timeSecondsSince2000();
	}

	if(iTime > (lastSecondChecked + EVERY_FEW_MINUTES_IN_SECONDS))
	{
		EveryFewMinutesActivities();
		lastSecondChecked = iTime;
	}
}

int GetCurrentHour(void)
{
	struct tm timeStruct;
	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &timeStruct);
	return timeStruct.tm_hour;
}
void HourlyCheck(void)
{
	static int lastHourChecked = -1;
	int iCurrentHour = GetCurrentHour();

	// Lazily init lastHourChecked
	if(lastHourChecked == -1)
	{
		lastHourChecked = GetCurrentHour();
	}

	if(iCurrentHour != lastHourChecked)
	{
		HourlyActivities();
		lastHourChecked = iCurrentHour;
	}
}

#define HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR (6)
void NightlyCheck(void)
{
	static int lastDayOfWeekChecked = -1;
	static int lastJiraDayOfWeekChecked = -1;
	int currentDayOfWeek, currentHour;
	struct tm timeStruct;

	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &timeStruct);
	currentDayOfWeek = timeStruct.tm_wday;
	currentHour      = timeStruct.tm_hour;

	if(currentDayOfWeek != lastJiraDayOfWeekChecked)
	{
		if (currentHour == HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR-1)
		{
			NightlyJiraActivities();
			lastJiraDayOfWeekChecked = currentDayOfWeek;
		}
	}
	if(currentDayOfWeek != lastDayOfWeekChecked)
	{
		if(currentHour == HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR)
		{
			NightlyActivities();
			lastDayOfWeekChecked = currentDayOfWeek;
		}
	}
}

void ticketExtractCharacterName(char **estr, TicketEntry *pEntry)
{
	// TODO This is broken since the Entity string was moved into its own file
	/*ParseTable **ppParseTables = NULL;
	void *pEntity = NULL;
	bool loaded = loadParseTableAndStruct (&ppParseTables, &pEntity, NULL, 
		pEntry->uEntityDescriptorID, pEntry->pEntityStr);
	if (loaded)
	{
		objPathGetEString(".pSaved.savedName",  ppParseTables[0], pEntity, estr);
	}
	destroyParseTableAndStruct (&ppParseTables, &pEntity);*/
}

AUTO_COMMAND ACMD_CATEGORY(TicketTracker_Temp);
void RemoveSTO_GMandBugTickets(void)
{
	int i,iToRemove, removeCount, iDone = 0;
	static TicketEntry ** seaTicketsToRemove = {0};

	if (!seaTicketsToRemove)
	{
		ContainerIterator iter = {0};
		TicketEntry *pEntry = NULL;
		removeCount = 0;

		objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
		while (pEntry = objGetNextObjectFromIterator(&iter))
		{
			if (stricmp(pEntry->pProductName, "StarTrek") == 0 && 
				(stricmp(pEntry->pMainCategory, "CBug.CategoryMain.GameSupport") == 0 ||
				stricmp(pEntry->pMainCategory, "CBug.CategoryMain.GM") == 0))
			{
				removeCount++;
				eaPush(&seaTicketsToRemove, pEntry);
			}
		}
		objClearContainerIterator(&iter);
		printf("Removing %d STO GM/Bug tickets (Total)... \n", removeCount);
	}

	removeCount = eaSize(&seaTicketsToRemove);
	while (removeCount > 0)
	{
		if (removeCount < MIGRATION_MAX_COUNT_PER_FRAME)
			iToRemove = removeCount;
		else
			iToRemove = MIGRATION_MAX_COUNT_PER_FRAME;
		if (iToRemove)
		{
			TicketEntryList toRemove = {0};
			printf("\rRemoving STO GM/Bug tickets #%d-%d...", iDone, iDone + iToRemove);
			iDone += iToRemove;
			for (i=0; i<iToRemove; i++)
			{
				if (seaTicketsToRemove[i])
					eaPush(&toRemove.ppEntries, (TicketEntryConst*) seaTicketsToRemove[i]);
			}

			if (toRemove.ppEntries)
			{
				for (i=eaSize(&toRemove.ppEntries)-1; i>=0; i--)
				{
					char datetime[128];
					timeMakeDateStringFromSecondsSince2000(datetime, toRemove.ppEntries[i]->uEndTime);
					filelog_printf(gLogFileName, "Ticket Cleanup - ID #%d, End Time: %s", toRemove.ppEntries[i]->uID, datetime);
					removeTicketTrackerEntry((TicketEntry*) toRemove.ppEntries[i]);
				}
				eaDestroy(&toRemove.ppEntries);
			}

			eaRemoveRange(&seaTicketsToRemove, 0, iToRemove);
			removeCount = eaSize(&seaTicketsToRemove);
			if (removeCount == 0)
				eaDestroy(&seaTicketsToRemove);
			else
			{
				objContainerSaveTick();
				Sleep(1000);
			}
		}
	}
}

#include "AutoGen\tickettracker_h_ast.c"