#include "TicketEntry.h"
#include "AutoGen/TicketEntry_h_ast.h"

#include "Category.h"
#include "EntityDescriptor.h"
#include "file.h"
#include "GlobalTypeEnum.h"
#include "jira.h"
#include "Message.h"
#include "objTransactions.h"
#include "Search.h"
#include "StringUtil.h"
#include "ticketenums.h"
#include "ticketnet.h"
#include "TicketTrackerConfig.h"
#include "timing.h"
#include "trivia.h"

#include "AutoGen/AppLocale_h_ast.h"
#include "Autogen/jira_h_ast.h"
#include "AutoGen/ticketenums_h_ast.h"
#include "AutoGen/TicketTrackerLib_autotransactions_autogen_wrappers.h"

const char *getPlatformName(Platform ePlatform)
{
	switch(ePlatform)
	{
		xcase PLATFORM_WIN32:   return "Win32";
		xcase PLATFORM_XBOX360: return "Xbox 360";
	};

	return "Unknown";
}
const char *getStatusString(TicketStatus eStatus)
{
	const char * result = StaticDefineIntRevLookup(TicketStatusEnum, eStatus);
	if (result) return result;
	return "Ticket.Status.Unknown";
}
const char *getInternalStatusString(TicketInternalStatus eStatus)
{
	const char * result = StaticDefineIntRevLookup(TicketInternalStatusEnum, eStatus);
	if (result) return result;
	return "Ticket.Status.NoEscalation";
}
const char *getResolutionString(TicketResolution eResolution)
{
	const char * result = StaticDefineIntRevLookup(TicketResolutionEnum, eResolution);
	if (result) return result;
	return "Ticket.Status.Unknown";
}
const char *getVisibilityString(TicketVisibility eVisibility)
{
	const char * result = StaticDefineIntRevLookup(TicketVisibilityEnum, eVisibility);
	if (result) return result;
	return "Ticket.Visibility.Unknown";
}

int TicketEntry_SortPriority(const ContainerID *id1, const ContainerID *id2)
{
	TicketEntry *p1, *p2;
	p1 = findTicketEntryByID(*id1);
	p2 = findTicketEntryByID(*id2);
	if (!p1 && !p2)
		return 0;
	if (!p1) return 1;
	if (!p2) return -1;
	if (p1->fPriority < p2->fPriority) return  1;
	else if (p1->fPriority >p2->fPriority) return -1;
	else return 0;
}

int TicketEntry_SortPriorityAscending(const TicketEntry **p1, const TicketEntry **p2, const void *ign)
{
	if      ((*p1)->fPriority < (*p2)->fPriority) return  -1;
	else if ((*p1)->fPriority > (*p2)->fPriority) return 1;
	else return 0;
}

void TicketEntry_CalculatePriority(TicketEntry *ticket, U32 uCurrentTime)
{
	U32 uTimeDifference = uCurrentTime - ticket->uLastTime;
	ticket->fPriority = ((F32) ticket->uOccurrences) / (uTimeDifference + 1);
}

void TicketEntry_CalculateAndSortByPriority(CONTAINERID_EARRAY *eaiTicketIDs)
{
	U32 uCurrentTime;
	int size, i;
	TicketEntry *pEntry;

	if (*eaiTicketIDs == NULL)
		return;
	uCurrentTime = timeSecondsSince2000();
	size = eaiSize(eaiTicketIDs);

	for (i=0; i<size; i++)
	{
		pEntry = findTicketEntryByID((*eaiTicketIDs)[i]);
		if (pEntry)
			TicketEntry_CalculatePriority(pEntry, uCurrentTime);
	}
	eaiQSort(*eaiTicketIDs, TicketEntry_SortPriority);
}

void TicketEntry_CsvHeaderCB (char **estr, ParseTable *pti, SearchData *pData)
{
	estrConcatf(estr, "Id,Occurrences (Total),Occurrences (Filtered),Category,Label,"
		"Summary,User Description,Accounts (Filtered),"
		"Filed Time,Last Subscribe Time,Last Updated,"
		"Assigned To,Ticket Status,Ticket Visibility,Jira Assignee,Jira Issue,Jira Status,Knowledge Base,Phone Resolution,SetDebugPos String,"
		"Locale,Product,Shard Name,Internal\n");
}

void TicketEntry_CsvHeaderFileCB (FileWrapper *file, ParseTable *pti, SearchData *pData)
{
	char *header = NULL;
	estrCopy2(&header, "");
	TicketEntry_CsvHeaderCB(&header, pti, pData);
	fwrite(header, sizeof(char), estrLength(&header), file);
	estrDestroy(&header);
}

void TicketEntry_CsvCB (char **estr, TicketEntry *ticket, SearchData *pData)
{
	int i,size;
	char *summaryCopy = NULL, *descriptionCopy = NULL, *labelCopy = NULL;
	char datetime[64];
	bool bFirstEntry = true;
	U32 uOccurrences = FilterOccurenceCount(pData, ticket);

	if (ticket->pSummary)
	{
		estrCopyWithHTMLUnescaping(&summaryCopy, ticket->pSummary);
		estrReplaceOccurrences(&summaryCopy, "\"", "\"\""); // double the double-quotes
	}
	else
		estrCopy2(&summaryCopy, "");

	if (ticket->pUserDescription)
	{
		estrCopyWithHTMLUnescaping(&descriptionCopy, ticket->pUserDescription);
		estrReplaceOccurrences(&descriptionCopy, "\"", "\"\""); // double the double-quotes
	}
	else
		estrCopy2(&descriptionCopy, "");

	if (ticket->pLabel)
	{
		estrCopyWithHTMLUnescaping(&labelCopy, ticket->pLabel);
		estrReplaceOccurrences(&labelCopy, "\"", "\"\""); // double the double-quotes
	}
	else
		estrCopy2(&labelCopy, "");

	estrConcatf(estr, "%d,%d,%d,\"%s - %s\",\"%s\",\"%s\",\"%s\",\"",
		ticket->uID, ticket->uOccurrences, uOccurrences, 
		categoryGetTranslation(ticket->pMainCategory), categoryGetTranslation(ticket->pCategory), 
		labelCopy, summaryCopy, descriptionCopy);
	estrDestroy(&labelCopy);
	estrDestroy(&summaryCopy);
	estrDestroy(&descriptionCopy);

	size= eaSize(&ticket->ppUserInfo);
	for (i=0; i<size; i++)
	{
		if(UserInfoMatchesFilter(pData, ticket, (TicketClientUserInfo*)ticket->ppUserInfo[i]))
		{
			// TODO make list of account names unique

			if(bFirstEntry)
			{
				estrConcatf(estr, "%s", ticket->ppUserInfo[i]->pAccountName);
				bFirstEntry = false;
			}
			else
			{
				estrConcatf(estr, ",%s", ticket->ppUserInfo[i]->pAccountName);
			}
		}
	}
	timeMakeLocalDateStringFromSecondsSince2000(datetime, ticket->uFiledTime);
	estrConcatf(estr, "\",\"%s\",", datetime);

	timeMakeLocalDateStringFromSecondsSince2000(datetime, ticket->uLastTime);
	estrConcatf(estr, "\"%s\",", datetime);

	timeMakeLocalDateStringFromSecondsSince2000(datetime, ticket->uLastModifiedTime);
	estrConcatf(estr, "\"%s\",", datetime);

	estrConcatf(estr, "\"%s\",", (ticket->pRepAccountName) ? ticket->pRepAccountName : "");
	estrConcatf(estr, "\"%s\",", TranslateMessageKey(getStatusString(ticket->eStatus)));
	estrConcatf(estr, "\"%s\",", TranslateMessageKey(getVisibilityString(ticket->eVisibility)));
	estrConcatf(estr, "\"%s\",", (ticket->pJiraIssue) ? ticket->pJiraIssue->assignee : "");
	estrConcatf(estr, "\"%s\",", (ticket->pJiraIssue) ? ticket->pJiraIssue->key : "");
	estrConcatf(estr, "\"%s\",", (ticket->pJiraIssue) ? jiraGetStatusString(ticket->pJiraIssue->status) : "");

	if (ticket->pSolutionKey)
		estrConcatf(estr, "%s,", ticket->pSolutionKey);
	else
		estrConcatf(estr, ",");
	if (ticket->pPhoneResKey)
		estrConcatf(estr, "%s,", ticket->pPhoneResKey);
	else
		estrConcatf(estr, ",");

	{
		char *debugPosString = NULL;
		estrStackCreate(&debugPosString);
		ticketConstructDebugPosString(&debugPosString, ticket);
		estrReplaceOccurrences(&debugPosString, "\"", "\"\"");
		estrConcatf(estr, "\"%s\",", debugPosString);
		estrDestroy(&debugPosString);
	}
	estrConcatf(estr, "%s,", locGetName(locGetIDByLanguage(ticket->eLanguage)));
	estrConcatf(estr, "%s,", ticket->pProductName);

	if (eaSize(&ticket->ppUserInfo) > 0)
	{
		char retBuffer[64] = "";
		char *shardInfoString = ticket->ppUserInfo[0]->pShardInfoString;
		if (shardInfoString)
		{
			char *pName = strstr(shardInfoString, "name (");
			if (pName)
			{
				char *pCloseParens = strchr(pName, ')');
				if (pCloseParens)
					strncpy(retBuffer, pName + 6, pCloseParens - pName - 6);
				estrConcatf(estr, "\"%s\",", retBuffer);
			}
			else
				estrConcatf(estr, "Unknown,");
		}
		else
			estrConcatf(estr, "No Shard,");
	}
	else
		estrConcatf(estr, "No Shard,");
	estrConcatf(estr, "%d\n", ticket->bIsInternal);
}

void TicketEntry_CsvFileCB (FileWrapper *file, TicketEntry *ticket, SearchData *pData)
{
	char *ticketEntry = NULL;
	estrCopy2(&ticketEntry, "");
	TicketEntry_CsvCB(&ticketEntry, ticket, pData);
	fwrite(ticketEntry, sizeof(char), estrLength(&ticketEntry), file);
	estrDestroy(&ticketEntry);
}

void TicketTracker_DumpSearchToCSVString (char **estr, SearchData *sd)
{
	TicketEntry *p;
	int iSearchCount = 0;
	int i, size;

	if (!estr)
	{
		Errorf("NULL Estring passed into CSV creation");
		return;
	}

	p = searchFirst(sd);
	TicketEntry_CsvHeaderCB(estr, parse_TicketEntryConst, sd);
	size = eaSize(&sd->ppSortedEntries);
	for (i=0; i<size; i++)
	{
		TicketEntry_CsvCB(estr, sd->ppSortedEntries[i], sd);
	}
	searchEnd(sd);
}

void GetTicketFileDir(U32 uID, char *dirname, size_t dirname_size)
{
	U32 i, k, m;
	m = uID / 1000000;
	k = (uID % 1000000) / 1000;
	i = uID % 1000;
	sprintf_s(SAFESTR2(dirname), "%s\\%dm\\%dk\\%d\\", GetTicketFileParentDirectory(), m, k, i);
}

// Relative to parent directory exposed for GenericFileServing
const char *GetTicketFileRelativePath(U32 uID, const char *file)
{
	static char filepath[MAX_PATH];
	U32 i, k, m;
	m = uID / 1000000;
	k = (uID % 1000000) / 1000;
	i = uID % 1000;
	sprintf_s(SAFESTR(filepath), "%dm/%dk/%d/%s", m, k, i, file);
	forwardSlashes(filepath);
	return filepath;
}

extern char gTicketTrackerAltDataDir[MAX_PATH];
void TicketEntry_WriteScreenshot(TicketEntry *pEntry, TicketData *data)
{
	char filepath[MAX_PATH];
	FILE *pFile = NULL;
	
	objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "AddScreenshot", 
		"set .pScreenshotFilename = \"%s%d_ss.jpg\"", GetTicketFileRelativePath(pEntry->uID, ""), pEntry->uID);
	if (!devassert(pEntry->pScreenshotFilename))
		return;
	sprintf(filepath, "%s\\%s", GetTicketFileParentDirectory(), pEntry->pScreenshotFilename);
	
	mkdirtree(filepath);
	pFile = fopen(filepath, "wb");
	fwrite(data->imageBuffer, sizeof(char), data->uImageSize, pFile);
	fclose(pFile);
}

void TicketEntry_WriteEntity(TicketEntry *pEntry, const char *pEntityStr)
{
	FILE *pFile = NULL;
	char filepath[MAX_PATH];
	
	if (nullStr(pEntityStr) || pEntry->uEntityDescriptorID == 0)
		return;
	objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "AddEntity", 
		"set .pEntityFileName = \"%s%d_entity.txt\"", GetTicketFileRelativePath(pEntry->uID, ""), pEntry->uID);
	if (!devassert(pEntry->pEntityFileName))
		return;
	sprintf(filepath, "%s\\%s", GetTicketFileParentDirectory(), pEntry->pEntityFileName);
	
	mkdirtree(filepath);
	pFile = fopen(filepath, "w");
	fwrite(pEntityStr, sizeof(char), strlen(pEntityStr), pFile);
	fclose(pFile);
}

void TicketEntry_WriteDxDiag(TicketEntry *pEntry, const char *pDxDiag)
{
	FILE *pFile = NULL;
	char filepath[MAX_PATH];
	
	objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "AddDxDiag", 
		"set .pDxDiagFilename = \"%s%d_dxdiag.txt\"", GetTicketFileRelativePath(pEntry->uID, ""), pEntry->uID);
	if (!devassert(pEntry->pDxDiagFilename))
		return;
	sprintf(filepath, "%s\\%s", GetTicketFileParentDirectory(), pEntry->pDxDiagFilename);

	mkdirtree(filepath);
	pFile = fopen(filepath, "wt");
	fwrite(pDxDiag, sizeof(char), strlen(pDxDiag), pFile);
	fclose(pFile);
}

void TicketEntry_WriteUserData(TicketEntry *pEntry, STRING_EARRAY *eaUserDataStrings)
{
	FILE *pFile = NULL;
	char filepath[MAX_PATH];
	TicketUserData userdata = {0};
	
	objRequestTransactionSimplef(NULL, GLOBALTYPE_TICKETENTRY, pEntry->uID, "AddUserData", 
		"set .pUserDataFilename = \"%s%d_userdata.txt\"", GetTicketFileRelativePath(pEntry->uID, ""), pEntry->uID);
	if (!devassert(pEntry->pUserDataFilename))
		return;
	sprintf(filepath, "%s\\%s", GetTicketFileParentDirectory(), pEntry->pUserDataFilename);

	mkdirtree(filepath);
	userdata.eaUserDataStrings = *eaUserDataStrings;
	ParserWriteTextFile(filepath, parse_TicketUserData, &userdata, 0, 0);
}

// Writes the Entity Parse Table out as either XML or HTML
void appendEntityParseTable(char **estr, U32 uTicketID, U32 uDescriptorID, const char *pParseFileName, bool bXML)
{
	ParseTable **ppParseTables = NULL;
	void *pEntity = NULL;
	char *ptiName;
	bool loaded = false;

	char *pParseString = NULL;
	FILE *file;
	char fullParsePath[MAX_PATH];
	
	sprintf(fullParsePath, "%s\\%s", GetTicketFileParentDirectory(), pParseFileName);
	file = fileOpen(fullParsePath, "r");
	if (file)
	{
		U32 iSize = fileGetSize(file);
		size_t iRead = 0;
		int error = 0;
		pParseString = malloc(iSize+1);
		iRead = fread(pParseString, sizeof(char), iSize, file);
		if (iRead != iSize)
		{
			error = ferror (file);
		}
		fclose(file);
		if (error)
		{
			free(pParseString);
			return; // error reading file.
		}
		else
			pParseString[iRead] = 0; // NULL terminate it
	}
	else
	{
		Errorf("Could not find Entity file for character.");
		return;
	}
	
	loaded = loadParseTableAndStruct (&ppParseTables, &pEntity, &ptiName, uDescriptorID, pParseString);
	if (loaded)
	{
		char *tempString = NULL;
		estrStackCreate(&tempString);

		if (bXML)
		{
			ParserWriteXML(&tempString, ppParseTables[0], pEntity);
			estrCopyWithHTMLEscaping(estr, tempString, false);
		}
		else
		{
			WriteHTMLContext htmlContext = {0};
			estrConcatf(estr, "\n<div class=\"heading\">%s</div>\n", ptiName ? ptiName : "Struct Information");
			ParserWriteHTML(&tempString, ppParseTables[0], pEntity, &htmlContext);
			estrAppend2(estr, tempString);
		}

		destroyParseTableAndStruct(&ppParseTables, &pEntity);
		estrDestroy(&tempString);
	}
	else
		Errorf("Failed to load parse table for Entity.");
	if (pParseString)
		free(pParseString);
}

#include "AutoGen/TicketEntry_h_ast.c"