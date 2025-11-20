#include "email.h"

#include "crypt.h"
#include "timing.h"
#include "textparser.h"
#include "process_util.h"
#include "earray.h"
#include "errornet.h"
#include "file.h"
#include "fileutil2.h"
#include "utils.h"
#include "estring.h"
#include "stashtable.h"
#include "error.h"
#include "objTransactions.h"

#include "ErrorTracker.h"
#include "jira.h"
#include "ErrorTrackerLib.h"
#include "Search.h"
#include "objContainer.h"
#include "WebReport.h"
#include "Organization.h"
#include "gimmeDLLWrapper.h"
#include "qsortG.h"
#include "StringUtil.h"

#include "ETCommon/ETShared.h"
#include "ETCommon/ETWebCommon.h"
#include "AutoGen/email_c_ast.h"
#include "AutoGen/ErrorTrackerLib_autotransactions_autogen_wrappers.h"
#include "Autogen/jira_h_ast.h"

extern char gFQDN[MAX_PATH];
static int sCounter = 0;

#define NUM_NIGHTLY_ERRORS (5)
#define NUM_NIGHTLY_PRODUCTION_ERRORS (20)
#define NUM_NIGHTLY_BUGS (10)

//delay before sending the same person an email saying they have generated the same error
#define TIME_BETWEEN_EMAILS (60 * 60)

#define CRASH_REPORT_WINDOW_SECONDS (30 * 60 * 60) // 30 hours in seconds

//delay before sending any email to a person, after which all emails ready for that person will be gathered together in
//one big email
#define TIME_TO_COLLECT_ERRORS_BEFORE_EMAILING (5 * 60)

//struct that tracks what "YOU CAUSED AN ERROR" emails we have sent
typedef struct
{
	U32 uTime;
	char firstUserWhoGotIt[256]; //if "", then multiple people have got it, and the email has already been sent
} errorEmailTrackingStruct;

//this stashtable has [ErrorEntry]->uniqueID as its keys, and pointers to errorEmailTrackingStruct as its value
StashTable uniqueIDEmailTrackingTable = NULL;

typedef struct
{
	char whoTo[64];
	char title[128];
	char fileName[64];
	U32 iTimeAdded;
	int iCount;
} QueuedEmailStruct;

QueuedEmailStruct **ppEmailQueue = NULL;

void ImmediatelySendEmail(const char *pWhoTo, const char *pTitle, char *pFileName)
{
	char *pBigSystemString = NULL;

	estrPrintf(&pBigSystemString, 
		"c:\\night\\tools\\bin\\bmail.exe -s universe." ORGANIZATION_DOMAIN " -t %s -f errortracker@" ORGANIZATION_DOMAIN " -a \"%s\" -m %s", 
		pWhoTo, pTitle, pFileName);

	system_detach(pBigSystemString, 0, true);
	estrDestroy(&pBigSystemString);
}

void QueueEmail(char *pWhoTo, char *pTitle, char *pFileName)
{
	int i;
	int iSize = eaSize(&ppEmailQueue);
	QueuedEmailStruct *pEmail;

	for (i=0; i < iSize; i++)
	{
		if (strcmp(ppEmailQueue[i]->whoTo, pWhoTo) == 0)
		{
			char systemString[1024];
			sprintf(systemString, "type %s >> %s", pFileName, ppEmailQueue[i]->fileName);
			system(systemString);

			ppEmailQueue[i]->iCount++;

			return;
		}
	}
	
	pEmail = calloc(sizeof(QueuedEmailStruct), 1);
	strcpy(pEmail->whoTo, pWhoTo);
	strcpy(pEmail->title, pTitle);
	strcpy(pEmail->fileName, pFileName);
	pEmail->iCount = 1;
	pEmail->iTimeAdded = timeSecondsSince2000();

	eaPush(&ppEmailQueue, pEmail);
}

// For the nightly emails
void SendEmailFromStrings(const char *pWhoTo, const char *pTitle, char *pStr1, char *pStr2)
{
	char fileName[MAX_PATH];
	FILE *pFile = NULL;

	while (!pFile)
	{
		sprintf(fileName, "C:\\temp\\ErrorTrackerEmail%d.txt", sCounter++);
		pFile = fopen(fileName, "wt");
	}

	if(pStr1) fprintf(pFile, "%s", pStr1);
	if(pStr2) fprintf(pFile, "%s", pStr2);
	fclose(pFile);
	
	ImmediatelySendEmail(pWhoTo, pTitle, fileName);
}

void ProcessEmailQueue(void)
{
	if(errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS)
		return;

	if (eaSize(&ppEmailQueue))
	{
		if (!ProcessCount("bmail.exe", false))
		{
			if (ppEmailQueue[0]->iTimeAdded < timeSecondsSince2000() - TIME_TO_COLLECT_ERRORS_BEFORE_EMAILING)
			{
				ImmediatelySendEmail(ppEmailQueue[0]->whoTo, ppEmailQueue[0]->title, ppEmailQueue[0]->fileName);
				free(ppEmailQueue[0]);
				eaRemove(&ppEmailQueue, 0);
			}
		}
	}
}

void QueueSVNBlameEmail(U32 uID, const char *pExecutableName, StackTraceLine *pLine)
{
	char fileName[MAX_PATH];
	FILE *pFile = NULL;

	// needs all of these fields
	if (!pLine->pBlamedPerson || !pLine->pFilename || !pLine->pFunctionName)
		return;

	if (gbETVerbose) printf("Sending an SVN blame email to %s\n", pLine->pBlamedPerson);
	while (!pFile)
	{
		sprintf(fileName, "C:\\temp\\ErrorTrackerEmail%d.txt", sCounter++);
		pFile = fopen(fileName, "wt");
	}

	fprintf(pFile,  "\n\nYou may be responsible for a crash that occurred in %s.\n"
		"http://%s/detail?id=%d\n"
		"You are being blamed for:\n"
		"Filename  : %s\n"
		"Function  : %s\n"
		"Line      : %d\n"
		"Version   : %d\n"
		"Revised on: %s\n",
		pExecutableName,
		getMachineAddress(), uID,
		pLine->pFilename,
		pLine->pFunctionName,
		pLine->iLineNum,
		pLine->iBlamedRevision, 
		timeGetLocalDateStringFromSecondsSince2000 (pLine->uBlamedRevisionTime));
	fclose(pFile);
	
	QueueEmail( STACK_SPRINTF("%s@" ORGANIZATION_DOMAIN, pLine->pBlamedPerson), 
		"YOU HAVE CAUSED A CRASH!", fileName);
}

void QueueErrorNotificationEmail(U32 uID, const char *pUserToSendTo, 
								 const char *pErrorString, 
								 const char *pDataFile,
								 U32 iDataFileModificationDate, 
								 const char *pWhoGotIt, 
								 const char *pWhoElseGotIt, 
								 const char *pExecutableName,
								 const char *pVersion)
{
	char fileName[MAX_PATH];
	FILE *pFile = NULL;

	if (gbETVerbose) printf("Sending an error email to %s\n", pUserToSendTo);
	while (!pFile)
	{
		sprintf(fileName, "C:\\temp\\ErrorTrackerEmail%d.txt", sCounter++);
		pFile = fopen(fileName, "wt");
	}

	fprintf(pFile, "\n\nYou appear to have caused an error that was experienced by %s and %s. "
				   "Here are the details:\n"
				   "http://%s/detail?id=%d\n"
		           "\n%s\n\n"
				   "Filename              : %s\n"
				   "File modification date: %s\n"
				   "Executable            : %s\n"
				   "Version               : %s\n\n"
				   "After another hour, if the problem persists, "
				   "we will have no choice but to... send you another polite but firm email\n\n\n",

				   pWhoGotIt, 
				   pWhoElseGotIt, 
				   getMachineAddress(), uID,
				   pErrorString,
				   pDataFile,
				   timeGetDateStringFromSecondsSince2000(iDataFileModificationDate), 
				   pExecutableName,
				   pVersion);

	fclose(pFile);
	
	QueueEmail( STACK_SPRINTF("%s@" ORGANIZATION_DOMAIN, pUserToSendTo), "YOU HAVE CAUSED AN ERROR!", fileName);
}


void PurgeErrorEmailTimesTable(void)
{
	U32 uTime = timeSecondsSince2000();

	if (uniqueIDEmailTrackingTable)
	{
		StashTableIterator stashIterator;
		StashElement element;

		stashGetIterator(uniqueIDEmailTrackingTable, &stashIterator);
	
		while (stashGetNextElement(&stashIterator, &element))
		{
			errorEmailTrackingStruct *pStruct = stashElementGetPointer(element);

			if (uTime - pStruct->uTime > TIME_BETWEEN_EMAILS)
			{
				free(pStruct);
				stashIntRemovePointer(uniqueIDEmailTrackingTable, stashElementGetIntKey(element), NULL);
			}
		}
	}
}

// -----------------------------------------------------------------------------------------------

void sendEmailsOnNewBlame(ErrorEntry *pBlameEntry, int indexFlags)
{
	int i;
	if (!indexFlags)
		return;
	for (i=0; i<MAX_SVN_BLAME_DEPTH; i++)
	{
		if (indexFlags & (1 << i))
		{
			//Queue email
			QueueSVNBlameEmail(pBlameEntry->uID, pBlameEntry->ppExecutableNames[0], 
				pBlameEntry->ppStackTraceLines[i]);
		}
	}
}

void sendEmailsOnNewError(ErrorEntry *pNewEntry, ErrorEntry *pMergedEntry)
{
	if (!pNewEntry)
		pNewEntry = pMergedEntry;
	if(errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS)
		return;

	// Only interested in ERRORs
	if(pNewEntry->eType != ERRORDATATYPE_ERROR)
	{
		return;
	}

	// Username who got the crash is required
	if(eaSize(&pNewEntry->ppUserInfo) == 0)
	{
		return;
	}
	if(pNewEntry->ppUserInfo[0]->pUserName == 0)
	{
		return;
	}

	// Data file is required
	if(!pNewEntry->pDataFile || !pNewEntry->pDataFile[0])
	{
		return;
	}

	// Ignore errors in which the blamed user is the user who got it. They know about it already.
	if(!stricmp(pNewEntry->pLastBlamedPerson, pNewEntry->ppUserInfo[0]->pUserName))
	{
		return;
	}

	// Update our tracking table / send emails
	{
		U32 uTime = timeSecondsSince2000();
		errorEmailTrackingStruct *pTrackingStruct;
		U32 uID = pNewEntry->uID;
		const char *pUserName = pNewEntry->ppUserInfo[0]->pUserName;
		const char *pExecutableName = "";

		if(eaSize(&pNewEntry->ppExecutableNames) > 0)
		{
			pExecutableName = pNewEntry->ppExecutableNames[0];
		}

		// Lazily create the table
		if (!uniqueIDEmailTrackingTable)
		{
			uniqueIDEmailTrackingTable = stashTableCreateInt(64);
		}

		if (!stashIntFindPointer(uniqueIDEmailTrackingTable, uID, &pTrackingStruct))
		{
			pTrackingStruct = malloc(sizeof(errorEmailTrackingStruct));
			pTrackingStruct->uTime = uTime;
			strcpy(pTrackingStruct->firstUserWhoGotIt, pUserName);

			stashIntAddPointer(uniqueIDEmailTrackingTable, uID, pTrackingStruct, false);
		}
		else
		{
			//check if it's been gotten by one other person but no email has been sent yet
			if((pTrackingStruct->firstUserWhoGotIt[0])
			&& (stricmp(pUserName, pTrackingStruct->firstUserWhoGotIt) != 0))
			{
				QueueErrorNotificationEmail(pMergedEntry->uID, pNewEntry->pLastBlamedPerson, 
											(pNewEntry->pErrorString) ? pNewEntry->pErrorString : "", 
											pNewEntry->pDataFile,
											pNewEntry->uDataFileTime, 
											pUserName, 
											pTrackingStruct->firstUserWhoGotIt, 
											pExecutableName,
											eaSize(&pNewEntry->ppVersions) ? pNewEntry->ppVersions[eaSize(&pNewEntry->ppVersions)-1] : "No Version Information");

				pTrackingStruct->firstUserWhoGotIt[0] = 0;
			}
		}
	}
}

bool meetsNightlyReportTimingCriteria(ErrorEntry *p)
{
	int uTime, size;
	uTime = timeSecondsSince2000();


	size = eaSize(&p->ppUserInfo);
	// --------------------------------------------------------
	// If at least two people didn't get it, forget it.
	if(size < 2 && !p->bProductionMode)
	{
		int i;
		bool bHasCrypticdmzUser = false;
		for (i=0; i<size; i++)
		{
			if (p->ppUserInfo[i]->pUserName && strstri(p->ppUserInfo[i]->pUserName, "crypticdmz"))
			{
				bHasCrypticdmzUser = true;
				break;
			}
		}
		if (!bHasCrypticdmzUser)
			return false;
	}

	// --------------------------------------------------------
	// If it hasn't occurred in the last 24 hours, forget it.
	if((uTime - p->uNewestTime) > SECONDS_PER_DAY)
	{
		return false;
	}

	// --------------------------------------------------------
	// If an artist hasn't gotten it, forget it.
	if(!ArtistGotIt(p))
	{
		return false;
	}

	return true;
}

static void destroyStringArray(char *pString)
{
	free(pString);
}


void notifyDumpReceived(ErrorEntry *p)
{
	int i, iNumNotify = eaSize(&p->ppDumpNotifyEmail);
	char *pTo = NULL;
	char *pText = NULL;
	char *pHeader = NULL;

	if(iNumNotify < 1)
	{
		return;
	}

	estrClear(&pTo);
	estrClear(&pText);
	estrClear(&pHeader);

	for(i=0; i<iNumNotify; i++)
	{
		if(i)
			estrConcatf(&pTo, ",");

		estrConcatf(&pTo, "%s", p->ppDumpNotifyEmail[i]);
	}

	printf("Notify:Dump: %s\n", pTo);

	estrPrintf(&pHeader, "ErrorTracker: New Dump Received For ID# %d", p->uID);

	estrPrintf(&pText, "New dump received for Error ID# %d.\n\nPlease visit this page to download it:\n\n"
					   "http://%s/detail?id=%d\n\n- ErrorTracker\n",
					   p->uID,
					   gFQDN,
					   p->uID);

	SendEmailFromStrings(pTo, pHeader, pText, NULL);

	estrDestroy(&pTo);
	estrDestroy(&pText);
	estrDestroy(&pHeader);

	// Clean out the array ... emails have been sent
	AutoTrans_trErrorEntry_RemoveNotifyMails(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, p->uID);
}

extern StashTable errorSourceFileLineTable;
typedef struct ErrorSourceCountStruct
{
	char *pKey;
	int iCount;
} ErrorSourceCountStruct;
static int Error_SortCount(const ErrorSourceCountStruct **p1, const ErrorSourceCountStruct **p2, const void *ign)
{
	if      ((*p1)->iCount < (*p2)->iCount) return  1;
	else if ((*p1)->iCount > (*p2)->iCount) return -1;
	else return 0;
}
#define ERROR_COUNT_TOPLIMIT 10
void SendNightlyErrorReport(bool bProductionErrorsOnly)
{
	ErrorEntry *p = NULL;
	int iCount = 0;
	int iSuppressCount = 0;
	SearchData sd = {0};
	const char *pEmailSubject    = "Nightly Error Report";
	const char *pEmailRecipients = gErrorTrackerSettings.pErrorEmailAddress;
	char *header = NULL;
	char *str = NULL;
	char *suppressStr = NULL;
	estrCreate(&header);
	estrCreate(&str);
	estrCreate(&suppressStr);

	estrCopy2(&header, "Nightly Report of Errors:\nBlamed on: (");

	sd.uFlags          = SEARCHFLAG_DONT_TIMEOUT|SEARCHFLAG_SORT|SEARCHFLAG_TYPE;
	sd.uTypeFlags      = SEARCH_TYPE_TO_FLAG(ERRORDATATYPE_ERROR);
	sd.eSortOrder      = SORTORDER_TWOWEEKCOUNT;
	sd.bSortDescending = true;

	if(bProductionErrorsOnly)
	{
		sd.uFlags |= SEARCHFLAG_EXECUTABLE;
		sd.pExecutable = "Production";

		pEmailSubject    = "Nightly ProductionMode-Only Errors";
		pEmailRecipients = gErrorTrackerSettings.pProductionErrorEmailAddress;
		estrCopy2(&header, "Nightly Report of ProductionMode-Only Errors:\nBlamed on: (");
	}
	if (nullStr(pEmailRecipients))
		return;

	p = searchFirst(errorTrackerLibGetCurrentContext(), &sd);
	while(p != NULL)
	{
		if(meetsNightlyReportTimingCriteria(p))
		{
			if(iCount > 0)
			{
				estrConcatf(&header, ", ");
			}
			estrConcatf(&header, "%s", (p->pLastBlamedPerson) ? p->pLastBlamedPerson : "[unknown]");

			if ( findErrorTrackerByID(p->uID)->bSuppressErrorInfo)
			{
				ET_DumpEntryToString(&suppressStr, p, DUMPENTRY_FLAG_NO_ERRORDETAILS, false);
				iSuppressCount++;
			}
			else 
			{
				ET_DumpEntryToString(&str, p, DUMPENTRY_FLAG_NO_ERRORDETAILS, true);

				iCount++;
			}
			
			if(!bProductionErrorsOnly && iCount == NUM_NIGHTLY_ERRORS || bProductionErrorsOnly && iCount == NUM_NIGHTLY_PRODUCTION_ERRORS)
			{
				break;
			}
		}
		p = searchNext(errorTrackerLibGetCurrentContext(), &sd);
	}
	estrConcatf(&header, ")\n\n");

	if(iCount == 0)
	{
		estrConcatf(&str, "\nNo Errors Found\n");
	}

	if (!bProductionErrorsOnly)
	{
		StashTableIterator iter = {0};
		StashElement elem = NULL;
		ErrorSourceCountStruct **ppCounts = NULL;
		int i, size;
		stashGetIterator(errorSourceFileLineTable, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			ErrorSourceCountStruct *errCount = malloc(sizeof(ErrorSourceCountStruct));
			errCount->pKey = stashElementGetStringKey(elem);
			errCount->iCount = stashElementGetInt(elem);

			eaPush(&ppCounts, errCount);
		}
		eaStableSort(ppCounts, NULL, Error_SortCount);
		size = eaSize(&ppCounts);
		if (size)
			estrConcatf(&str, "\n--------------\n\nLargest Daily Counts from Errorf source:\n");
		for (i=0; i<size; i++)
		{
			if (i<ERROR_COUNT_TOPLIMIT)
			{
				estrConcatf(&str, "%s - %d\n", ppCounts[i]->pKey, ppCounts[i]->iCount);
			}
			free(ppCounts[i]);
		}
		eaDestroy(&ppCounts);
	}

	if(iSuppressCount > 0) 
	{
		estrConcatf(&str, "\n--------------\n\nSuppressed Errors:\n");
		estrAppend(&str, &suppressStr);
	} 

	SendEmailFromStrings(pEmailRecipients, pEmailSubject, header, str);

	estrDestroy(&str);
	estrDestroy(&suppressStr);
	estrDestroy(&header);
}

#include "email_c_ast.h"

AUTO_STRUCT;
typedef struct BugMailData
{
	char *subject; AST(ESTRING)
	char *header;  AST(ESTRING)
} BugMailData;

void BugReportComplete(SearchData *pData, ErrorEntry **foundEntries, BugMailData *bmd)
{
	ErrorEntry *p = NULL;
	int iCount = 0;
	int iSuppressCount = 0;
	char *str = NULL;
	char *suppressStr = NULL;

	estrCreate(&str);
	estrCreate(&suppressStr);

	EARRAY_FOREACH_BEGIN(foundEntries, i);
	{
		p = foundEntries[i];

		if(p->eType == ERRORDATATYPE_ERROR)
			continue;

		if (findErrorTrackerByID(p->uID)->bSuppressErrorInfo) 
		{
			ET_DumpEntryToString(&suppressStr, p, DUMPENTRY_FLAG_NO_ERRORDETAILS, false);
			iSuppressCount++;
		}
		else
		{
			ET_DumpEntryToString(&str, p, DUMPENTRY_FLAG_NO_ERRORDETAILS, true);

			iCount++;
		}
		
		if(iCount == NUM_NIGHTLY_BUGS)
		{
			break;
		}
	}
	EARRAY_FOREACH_END;

	if (iSuppressCount > 0) 
	{
		estrConcatf(&str, "\n--------------\n\nSuppressed Errors:\n");
		estrAppend(&str, &suppressStr);
	}

	if(iCount > 0 || iSuppressCount > 0)
	{
		SendEmailFromStrings(gErrorTrackerSettings.pCrashEmailAddress, bmd->subject, bmd->header, str);
	}

	estrDestroy(&str);
	estrDestroy(&suppressStr);
	StructDestroy(parse_BugMailData, bmd);
}

AUTO_COMMAND;
void SendNightlyBugReport(void)
{
	SearchData sd = {0};
	U32 t = timeSecondsSince2000();
	char startDate[1024] = {0};
	char endDate[1024] = {0};
	BugMailData *bmd = NULL;

	if (nullStr(gErrorTrackerSettings.pCrashEmailAddress))
		return;

	sd.uFlags          = SEARCHFLAG_DONT_TIMEOUT
		               | SEARCHFLAG_SORT
					   | SEARCHFLAG_EXACT_ENABLED
					   | SEARCHFLAG_EXACT_INTERNAL
					   | SEARCHFLAG_EXACT_STARTDATE
					   | SEARCHFLAG_EXACT_ENDDATE;

	sd.eSortOrder            = SORTORDER_COUNT;
	sd.bSortDescending       = true;
	sd.uExactStartTime       = t - CRASH_REPORT_WINDOW_SECONDS;
	sd.uExactEndTime         = t;
	sd.exactCompleteCallback = BugReportComplete;

	timeMakeLocalDateStringFromSecondsSince2000(startDate, sd.uExactStartTime);
	timeMakeLocalDateStringFromSecondsSince2000(endDate,   sd.uExactEndTime);

	// -------------------------------------------------------------------------
	// Nightly All-Crashes Bug Report
	bmd = StructCreate(parse_BugMailData);
	estrPrintf(&bmd->subject, "Nightly Crash Report (All)");
	estrPrintf(&bmd->header,  "\nNightly Report of Crashes [%s - %s]:\n\n", startDate, endDate);
	startReport(&sd, 1, bmd); // "IP" for each different web report must be different
	// -------------------------------------------------------------------------

	// -------------------------------------------------------------------------
	// Nightly Live-Only Bug Report
	bmd = StructCreate(parse_BugMailData);
	estrPrintf(&bmd->subject, "Nightly Crash Report (LIVE)");
	estrPrintf(&bmd->header,  "\nNightly Report of Crashes [%s - %s]:\n\n", startDate, endDate);
	sd.uFlags |= SEARCHFLAG_SHARDINFOSTRING;
	sd.pShardInfoString = "(Live)";
	startReport(&sd, 2, bmd);
	// -------------------------------------------------------------------------

	// -------------------------------------------------------------------------
	// Nightly Playtest-Only Bug Report
	bmd = StructCreate(parse_BugMailData);
	estrPrintf(&bmd->subject, "Nightly Crash Report (Playtest)");
	estrPrintf(&bmd->header,  "\nNightly Report of Crashes [%s - %s]:\n\n", startDate, endDate);
	sd.uFlags |= SEARCHFLAG_SHARDINFOSTRING;
	sd.pShardInfoString = "(Playtest)";
	startReport(&sd, 3, bmd);
	// -------------------------------------------------------------------------
}

int SortByDailyCountDesc(const ErrorEntry **pEntry1, const ErrorEntry **pEntry2, const void *ign)
{
	if ((*pEntry1)->iDailyCount < (*pEntry2)->iDailyCount)
		return 1;
	else if ((*pEntry1)->iDailyCount > (*pEntry2)->iDailyCount)
		return -1;
	else 
		return 0;
}

bool EntryMeetsReportingCriteria(ErrorEntry *pEntry)
{
	if (eaSize(&pEntry->ppUserInfo) < 2)
	{
		return false;
	}
	return true;
}

void SendNightlyCrashCount(void)
{
	ContainerIterator iter = {0};
	Container *con;
	char *pHeader = NULL;
	char *pNewCrashes = NULL;
	char *pCrashes = NULL;
	char *pBody = NULL;
	int iTotalCount = 0;
	int iUniqueCount = 0;
	int iTotalNewCount = 0;
	int iUniqueNewCount = 0;
	ErrorEntry **ppEntries = NULL;
	int i;

	objInitContainerIteratorFromType(errorTrackerLibGetEntries(errorTrackerLibGetCurrentContext())->eContainerType, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(con);
		if (ERRORDATATYPE_IS_A_CRASH(pEntry->eType) && pEntry->iDailyCount > 0 && EntryMeetsReportingCriteria(pEntry))
		{
			eaPush(&ppEntries, pEntry);
		}
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	eaStableSort(ppEntries, NULL, SortByDailyCountDesc);
	for (i=0; i<eaSize(&ppEntries); i++)
	{
		ErrorEntry *pEntry = ppEntries[i];
		if (pEntry->bIsNewEntry)
		{
			estrConcatf(&pNewCrashes, "http://%s/detail?id=%d (%d occurrences)\n", 
				getMachineAddress(), pEntry->uID, pEntry->iDailyCount);
			iTotalNewCount += pEntry->iDailyCount;
			iUniqueNewCount++;
		}
		else
		{
			estrConcatf(&pCrashes, "http://%s/detail?id=%d (%d occurrences)\n", 
				getMachineAddress(), pEntry->uID, pEntry->iDailyCount);
		}
		iTotalCount += pEntry->iDailyCount;
		iUniqueCount++;
	}

	estrConcatf(&pHeader, "Crash Stats for %s\n", timeGetLocalDateNoTimeString());
	estrConcatf(&pHeader, "%d Total Crashes and Asserts (%d Unique)\n", 
		iTotalCount, iUniqueCount);
	estrConcatf(&pHeader, "%d Total New Crashes and Asserts (%d Unique)\n\n", 
		iTotalNewCount, iUniqueNewCount);

	estrConcatf(&pBody, "New Crashes:\n%s\n\nOld Crashes:\n%s",
		pNewCrashes, pCrashes);

	SendEmailFromStrings(gErrorTrackerSettings.pCrashCountEmailAddress, "Daily Crash Count", pHeader, pBody);
	estrDestroy(&pHeader);
	estrDestroy(&pCrashes);
}

void SendNightlyLackOfDumpsEmail(void)
{
	ContainerIterator iter = {0};
	Container *con;
	char *pHeader = NULL;
	char *pNewCrashes = NULL;
	char *pCrashes = NULL;
	char *pBody = NULL;
	int iTotalCount = 0;
	int iUniqueCount = 0;
	int iTotalNewCount = 0;
	int iUniqueNewCount = 0;
	ErrorEntry **ppEntries = NULL;
	int i;

	objInitContainerIteratorFromType(errorTrackerLibGetEntries(errorTrackerLibGetCurrentContext())->eContainerType, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(con);
		if (ERRORDATATYPE_IS_A_CRASH(pEntry->eType) && pEntry->iDailyCount > 0 && 
			pEntry->bFullDumpRequested && eaSize(&pEntry->ppDumpData) == 0 && EntryMeetsReportingCriteria(pEntry))
		{
			eaPush(&ppEntries, pEntry);
		}
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	eaStableSort(ppEntries, NULL, SortByDailyCountDesc); // Sort by daily count
	for (i=0; i<eaSize(&ppEntries); i++)
	{
		ErrorEntry *pEntry = ppEntries[i];
		if (pEntry->bIsNewEntry)
		{
			estrConcatf(&pNewCrashes, "http://%s/detail?id=%d (%d occurrences)\n", 
				getMachineAddress(), pEntry->uID, pEntry->iDailyCount);
			iTotalNewCount += pEntry->iDailyCount;
			iUniqueNewCount++;
		}
		else
		{
			estrConcatf(&pCrashes, "http://%s/detail?id=%d (%d occurrences)\n", 
				getMachineAddress(), pEntry->uID, pEntry->iDailyCount);
		}
		iTotalCount += pEntry->iDailyCount;
		iUniqueCount++;
	}

	estrConcatf(&pHeader, "Crashes with no Dumps for %s\n", timeGetLocalDateNoTimeString());
	estrConcatf(&pHeader, "%d Total Crashes and Asserts (%d Unique)\n", 
		iTotalCount, iUniqueCount);
	estrConcatf(&pHeader, "%d Total New Crashes and Asserts (%d Unique)\n\n", 
		iTotalNewCount, iUniqueNewCount);

	estrConcatf(&pBody, "New Crashes:\n%s\n\nOld Crashes:\n%s",
		pNewCrashes, pCrashes);

	SendEmailFromStrings("tchao", "Crashes with no Dumps", pHeader, pBody);
	estrDestroy(&pHeader);
	estrDestroy(&pCrashes);
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void SendNightlyEmails(void)
{
	if(errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS)
		return;

	SendNightlyErrorReport(false);
	SendNightlyErrorReport(true);
	SendNightlyBugReport();
	SendNightlyCrashCount();
	SendNightlyLackOfDumpsEmail();
	SendUnknownIPEmail();
	SendSymbolLookupEmail();
}

void ClearEmailFiles(void)
{
	int i, j, size;
	char ** files = fileScanDir("C:\\temp");

	size = eaSize(&files);
	for (i=0; i<size; i++)
	{
		char *fileName = files[i];

		backSlashes(fileName);
		if (strstri(fileName, "ErrorTrackerEmail"))
		{
			bool bFileInQueue = false;
			for (j=eaSize(&ppEmailQueue)-1; j>=0; j--)
			{
				if (stricmp(fileName, ppEmailQueue[j]->fileName) == 0)
				{
					bFileInQueue = true;
					break;
				}
			}
			if (!bFileInQueue)
			{
				fileForceRemove(fileName);
			}
		}
	}
	eaDestroy(&files);
}

//////////////////////////////////////////////
// Unknown IPs for incoming server crashes

AUTO_STRUCT;
typedef struct UnknownIPAggregate
{
	U32 uIP; AST(KEY)
	INT_EARRAY eaiETIDs;
	INT_EARRAY eaiAppServerTypes;
	STRING_EARRAY eaVersions;
	STRING_EARRAY eaExecutables;
	STRING_EARRAY eaUsers;
	U32 uCount;
} UnknownIPAggregate;

AUTO_STRUCT;
typedef struct UnknownIPInstance
{
	char *hashString; AST(KEY ESTRING)
	INT_EARRAY eaiETIDs;
	U32 uCount;

	U32 uIP;
	GlobalType eType;
	char *pUser;
	char *pExecutable;
	char *pVersion;
} UnknownIPInstance;

AUTO_STRUCT;
typedef struct UnknownIPTrackingList
{
	EARRAY_OF(UnknownIPAggregate) eaAggregateList;

	EARRAY_OF(UnknownIPInstance) eaInstanceList;
} UnknownIPTrackingList;

static UnknownIPTrackingList spUnknownIPs = {0};
static bool sUnknownIPsChanged = false;
static char sUnknownIP_Filepath[MAX_PATH];
#define UNKNOWNIP_FILENAME "etUnknownIPs.txt"

AUTO_COMMAND;
void UnknownIPSaveFile(void)
{
	if (sUnknownIPsChanged)
	{
		ParserWriteTextFile(sUnknownIP_Filepath, parse_UnknownIPTrackingList, &spUnknownIPs, 0, 0);
		sUnknownIPsChanged = false;
	}
}

// Init function
void UnknownIPLoadFile(void)
{
	sprintf(sUnknownIP_Filepath, "%s/%s", fileLocalDataDir(), UNKNOWNIP_FILENAME);
	StructReset(parse_UnknownIPTrackingList, &spUnknownIPs);
	if (fileExists(sUnknownIP_Filepath))
		ParserReadTextFile(sUnknownIP_Filepath, parse_UnknownIPTrackingList, &spUnknownIPs, 0);
}

static void UnknownIPDeleteFile(void)
{
	if (fileExists(sUnknownIP_Filepath))
		fileForceRemove(sUnknownIP_Filepath);
}

// Sends daily
AUTO_COMMAND ACMD_CATEGORY(ETDebug);
void SendUnknownIPEmail(void)
{
	const char *ip;
	char *body = NULL;
	struct in_addr ina = {0};

	if (!gErrorTrackerSettings.pUnknownIPEmails || eaSize(&spUnknownIPs.eaInstanceList) == 0)
		return;

	estrConcatf(&body, "\nNumber of Unknown IPs that have reported Errors and/or Crashes: %d", eaSize(&spUnknownIPs.eaAggregateList));
	EARRAY_CONST_FOREACH_BEGIN(spUnknownIPs.eaAggregateList, i, n);
	{
		UnknownIPAggregate *pAggregate = spUnknownIPs.eaAggregateList[i];
		ina.S_un.S_addr = pAggregate->uIP;
		ip = inet_ntoa(ina);
		
		estrConcatf(&body, "\n\nIP: %s\n"
			"Number of reports received: %d", ip, pAggregate->uCount);
		
		EARRAY_INT_CONST_FOREACH_BEGIN(pAggregate->eaiETIDs, j, o);
		{
			if (j == 0)
				estrConcatf(&body, "\nET: http://%s/detail?id=%d", getMachineAddress(), pAggregate->eaiETIDs[j]);
			else
				estrConcatf(&body, "\n    http://%s/detail?id=%d", getMachineAddress(), pAggregate->eaiETIDs[j]);
		}
		EARRAY_FOREACH_END;

		EARRAY_INT_CONST_FOREACH_BEGIN(pAggregate->eaiAppServerTypes, j, o);
		{
			const char *typeName = pAggregate->eaiAppServerTypes[j] ? GlobalTypeToName(pAggregate->eaiAppServerTypes[j]): "Unknown";
			if (j == 0)
				estrConcatf(&body, "\nServer Types: %s",  typeName);
			else
				estrConcatf(&body, ", %s",  typeName);
		}
		EARRAY_FOREACH_END;

		EARRAY_CONST_FOREACH_BEGIN(pAggregate->eaVersions, j, o);
		{
			if (j == 0)
				estrConcatf(&body, "\nVersions: %s",  pAggregate->eaVersions[j]);
			else
				estrConcatf(&body, ", %s",  pAggregate->eaVersions[j]);
		}
		EARRAY_FOREACH_END;

		EARRAY_CONST_FOREACH_BEGIN(pAggregate->eaExecutables, j, o);
		{
			if (j == 0)
				estrConcatf(&body, "\nExecutables: %s",  pAggregate->eaExecutables[j]);
			else
				estrConcatf(&body, ", %s",  pAggregate->eaExecutables[j]);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	estrConcatf(&body, "\n\nUnique Instances (CSV Format)\nIP, Count, User, Executable, Version\n");
	EARRAY_CONST_FOREACH_BEGIN(spUnknownIPs.eaInstanceList, i, s);
	{
		UnknownIPInstance *pInstance= spUnknownIPs.eaInstanceList[i];
		ina.S_un.S_addr = pInstance->uIP;
		ip = inet_ntoa(ina);

		estrConcatf(&body, "%s,%d,%s,%s,%s\n", ip, pInstance->uCount, pInstance->pUser, pInstance->pExecutable, pInstance->pVersion);
	}
	EARRAY_FOREACH_END;

	SendEmailFromStrings(gErrorTrackerSettings.pUnknownIPEmails, "Unknown Server Report", body, NULL);
	eaClearStruct(&spUnknownIPs.eaAggregateList, parse_UnknownIPAggregate);
	eaClearStruct(&spUnknownIPs.eaInstanceList, parse_UnknownIPInstance);
	estrDestroy(&body);
	UnknownIPDeleteFile();
	sUnknownIPsChanged = false;
}

static void AddUnknownAggregate(U32 uEntryID, U32 uIP, GlobalType eType, 
	const char *pVersion, const char *pExecutable, const char *pUser)
{
	UnknownIPAggregate *pAggregate;
	int index;

	if (!spUnknownIPs.eaAggregateList)
		eaIndexedEnable(&spUnknownIPs.eaAggregateList, parse_UnknownIPAggregate);
	pAggregate = eaIndexedGetUsingInt(&spUnknownIPs.eaAggregateList, uIP);
	if (!pAggregate)
	{
		pAggregate = StructCreate(parse_UnknownIPAggregate);
		pAggregate->uIP = uIP;
		pAggregate->uCount = 1;

		eaiPushUnique(&pAggregate->eaiETIDs, uEntryID);
		eaiPushUnique(&pAggregate->eaiAppServerTypes, eType);

		if (pUser)
		{
			index = (int) eaBFind(pAggregate->eaUsers, strCmp, pUser);
			if (index >= eaSize(&pAggregate->eaUsers) || stricmp(pAggregate->eaUsers[index], pUser) != 0)
				eaInsert(&pAggregate->eaUsers, strdup(pUser), index);
		}
		if (pExecutable)
		{
			index = (int) eaBFind(pAggregate->eaExecutables, strCmp, pExecutable);
			if (index >= eaSize(&pAggregate->eaExecutables) || stricmp(pAggregate->eaExecutables[index], pExecutable) != 0)
				eaInsert(&pAggregate->eaExecutables, strdup(pExecutable), index);
		}
		if (pVersion)
		{
			index = (int) eaBFind(pAggregate->eaVersions, strCmp, pVersion);
			if (index >= eaSize(&pAggregate->eaVersions) || stricmp(pAggregate->eaVersions[index], pVersion) != 0)
				eaInsert(&pAggregate->eaVersions, strdup(pVersion), index);
		}
		eaIndexedAdd(&spUnknownIPs.eaAggregateList, pAggregate);
	}
	else
		pAggregate->uCount++;
}

static void AddUnknownInstance(U32 uEntryID, U32 uIP, GlobalType eType, 
	const char *pVersion, const char *pExecutable, const char *pUser)
{
	UnknownIPInstance *pInstance;
	char *pCRCString = NULL;
	char *hashString = NULL;
	U32 hash[ET_HASH_ARRAY_SIZE];

	// Hash info: IP, GlobalType, Exe, Version, User
	estrPrintf(&pCRCString, "%d,%d,%s,%s,%s", uIP, eType, pExecutable, pVersion, pUser);
	strupr(pCRCString);
	cryptMD5(pCRCString, (int)strlen(pCRCString), hash);
	estrPrintf(&hashString, "%u_%u_%u_%u", hash[0], hash[1], hash[2], hash[3]);
	estrDestroy(&pCRCString);

	if (!spUnknownIPs.eaInstanceList)
		eaIndexedEnable(&spUnknownIPs.eaInstanceList, parse_UnknownIPInstance);
	pInstance = eaIndexedGetUsingString(&spUnknownIPs.eaInstanceList, hashString);
	if (!pInstance)
	{
		pInstance = StructCreate(parse_UnknownIPInstance);
		pInstance->uIP = uIP;
		eaiPushUnique(&pInstance->eaiETIDs, uEntryID);
		pInstance->eType = eType;
		pInstance->pExecutable = StructAllocString(pExecutable);
		pInstance->pVersion = StructAllocString(pVersion);
		pInstance->pUser = StructAllocString(pUser);
		estrCopy2(&pInstance->hashString, hashString);
		eaIndexedAdd(&spUnknownIPs.eaInstanceList, pInstance);
	}
	pInstance->uCount++;
	estrDestroy(&hashString);
}

void AddUnknownIP(U32 uIP, ErrorEntry *pEntry, const U32 uEntryID)
{
	// Information to add:
	// IP, ETID, GlobalType, Executable, Version, User
	GlobalType eType;
	char *pVersion = NULL;
	const char *pExecutable;
	const char *pUser;

	if (!pEntry)
		return;

	pExecutable = eaSize(&pEntry->ppExecutableNames) ? pEntry->ppExecutableNames[0] : NULL;
	if (eaSize(&pEntry->ppVersions) > 0)
		getSimplifiedVersionString(&pVersion, pEntry->ppVersions[0]);
	pUser = eaSize(&pEntry->ppUserInfo) ? pEntry->ppUserInfo[0]->pUserName : NULL;
	eType = eaSize(&pEntry->ppAppGlobalTypeNames) ? NameToGlobalType(pEntry->ppAppGlobalTypeNames[0]) : GLOBALTYPE_NONE;

	AddUnknownInstance(uEntryID, uIP, eType, pVersion, pExecutable, pUser);
	AddUnknownAggregate(uEntryID, uIP, eType, pVersion, pExecutable, pUser);
	sUnknownIPsChanged = true;
	estrDestroy(&pVersion);
}

//////////////////////////////////////////////
// Symbol Lookup Failures

AUTO_STRUCT;
typedef struct SymbolFailInfo
{
	char *guid; AST(KEY)
	STRING_EARRAY eaModules;
	U32 uCount;
} SymbolFailInfo;
static EARRAY_OF(SymbolFailInfo) seaSymbolFailures = NULL;

void AddSymbolLookupFailure(const char *module, const char *guid)
{
	SymbolFailInfo *symbolFail;
	if (!seaSymbolFailures)
		eaIndexedEnable(&seaSymbolFailures, parse_SymbolFailInfo);
	symbolFail = eaIndexedGetUsingString(&seaSymbolFailures, guid);
	if (!symbolFail)
	{
		symbolFail = StructCreate(parse_SymbolFailInfo);
		symbolFail->guid = strdup(guid);
		eaIndexedAdd(&seaSymbolFailures, symbolFail);
	}
	symbolFail->uCount++;
	addUniqueString(&symbolFail->eaModules, module);
}

void SendSymbolLookupEmail(void)
{
	char *body = NULL;

	if (!gErrorTrackerSettings.pSymbolFailureEmails || eaSize(&seaSymbolFailures) == 0)
		return;

	estrPrintf(&body, "Failed to load symbols for %d different module GUIDs:\n\n", eaSize(&seaSymbolFailures));
	EARRAY_CONST_FOREACH_BEGIN(seaSymbolFailures, i, n);
	{
		SymbolFailInfo *pSymbolFail = seaSymbolFailures[i];

		estrConcatf(&body, "%s: ", pSymbolFail->guid);
		EARRAY_CONST_FOREACH_BEGIN(pSymbolFail->eaModules, j, o);
		{
			estrConcatf(&body, "%s%s", j ? "," : "", pSymbolFail->eaModules[j]);
		}
		EARRAY_FOREACH_END;
		estrConcatf(&body, "\n");
	}
	EARRAY_FOREACH_END;

	SendEmailFromStrings(gErrorTrackerSettings.pSymbolFailureEmails, "ET Symbol Load Failures", body, NULL);
	eaClearStruct(&seaSymbolFailures, parse_SymbolFailInfo);
	estrDestroy(&body);
}

#include "AutoGen/email_c_ast.c"