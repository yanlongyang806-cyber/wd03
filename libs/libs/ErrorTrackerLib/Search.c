#include "Search.h"
#include "earray.h"
#include "estring.h"
#include "utils.h"
#include "email.h"
#include "ErrorTracker.h"
#include "ErrorTrackerLib.h"
#include "ErrorTrackerLibPrivate.h"
#include "timing.h"
#include "wininclude.h"
//#include <winsock.h>
#include "objContainer.h"
#include "jira.h"
#include "fileutil2.h"
#include "StringUtil.h"
#include "AutoGen/jira_h_ast.h"
#include "AutoGen/Search_h_ast.h"
#include "ETCommon/ETShared.h"

#define MAX_SEARCH_SECONDS 5

// -------------------------------------------------------------------------------------------
// Exact Mode Frills

ExactData * searchGetExactData(SearchData *pSearchData, U32 uID)
{
	ExactData *pExactData = NULL;
	if(pSearchData->exactData)
	{
		if(stashIntFindPointer(pSearchData->exactData, uID, &pExactData))
		{
			return pExactData;
		}
		else
		{
			pExactData = StructCreate(parse_ExactData);
			stashIntAddPointer(pSearchData->exactData, uID, pExactData, true);

			return pExactData;
		}
	}
	return NULL;
}

static void exactAddExecutable(ExactData *pExactData, const char *pExecutableName)
{
	EARRAY_FOREACH_BEGIN(pExactData->ppExecutableNames, i);
	{
		if(!strcmp(pExactData->ppExecutableNames[i], pExecutableName))
			return; // Already have it recorded
	}
	EARRAY_FOREACH_END;

	eaPush(&pExactData->ppExecutableNames, strdup(pExecutableName));
}

// -------------------------------------------------------------------------------------------
// Sorting comparison function macros. I completely agree with you, this code is very ugly.

#define BASIC_SORT_BY_INT_FUNC(FUNCNAME, VARIABLE) \
int FUNCNAME(const ErrorEntry **pEntry1, const ErrorEntry **pEntry2, const void *ign) { \
	if      ((*pEntry1)->VARIABLE < (*pEntry2)->VARIABLE) return -1; \
	else if ((*pEntry1)->VARIABLE > (*pEntry2)->VARIABLE) return  1; \
	else return 0; }

#define BASIC_SORT_BY_INT_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const ErrorEntry **pEntry1, const ErrorEntry **pEntry2, const void *ign) { \
	if      ((*pEntry1)->VARIABLE < (*pEntry2)->VARIABLE) return  1; \
	else if ((*pEntry1)->VARIABLE > (*pEntry2)->VARIABLE) return -1; \
	else return 0; }

#define BASIC_FIND_BY_INT_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const ErrorEntry **pEntry1, const ErrorEntry **pEntry2) { \
	if      ((*pEntry1)->VARIABLE < (*pEntry2)->VARIABLE) return  1; \
	else if ((*pEntry1)->VARIABLE > (*pEntry2)->VARIABLE) return -1; \
	else return 0; }

#define BASIC_SORT_BY_STR_FUNC(FUNCNAME, VARIABLE) \
int FUNCNAME(const ErrorEntry **pEntry1, const ErrorEntry **pEntry2, const void *ign) { \
	return -1 * strcmp((*pEntry1)->VARIABLE, (*pEntry2)->VARIABLE); }

#define BASIC_SORT_BY_STR_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const ErrorEntry **pEntry1, const ErrorEntry **pEntry2, const void *ign) { \
	return strcmp((*pEntry1)->VARIABLE, (*pEntry2)->VARIABLE); }

#define DO_SORT(FUNCNAME, DESCENDING) \
	if(DESCENDING) {eaStableSort(ppEntries, NULL, FUNCNAME ## Desc);} \
	else           {eaStableSort(ppEntries, NULL, FUNCNAME);}

// -------------------------------------------------------------------------------------------
// Sorting comparison functions, defined by the above macros. This is prettier.

BASIC_SORT_BY_INT_FUNC     (SortByNewest,     uNewestTime);
BASIC_SORT_BY_INT_FUNC_DESC(SortByNewestDesc, uNewestTime);
BASIC_FIND_BY_INT_FUNC_DESC(FindByNewestDesc, uNewestTime);

BASIC_SORT_BY_INT_FUNC     (SortByCount,      iTotalCount);
BASIC_SORT_BY_INT_FUNC_DESC(SortByCountDesc,  iTotalCount);

BASIC_SORT_BY_INT_FUNC     (SortByTwoWeekCount,      iTwoWeekCount);
BASIC_SORT_BY_INT_FUNC_DESC(SortByTwoWeekCountDesc,  iTwoWeekCount);

BASIC_SORT_BY_INT_FUNC     (SortByID,      uID);
BASIC_SORT_BY_INT_FUNC_DESC(SortByIDDesc,  uID);

// -------------------------------------------------------------------------------------------
// Sorting code

static const char * SortOrderToStringTable[SORTORDER_MAX] = {
	"Newest",     // SORTORDER_NEWEST
	"Count",      // SORTORDER_COUNT
	"ID",         // SORTORDER_ID
	"TwoWeek",    // SORTORDER_TWOWEEKCOUNT
};

const char * sortOrderToString(SortOrder eSortOrder)
{
	return SortOrderToStringTable[eSortOrder];
}

const char * sortOrderEntireEnumString(void)
{
	static char *pEntireEnumString = NULL;
	int i;

	if(pEntireEnumString == NULL)
	{
		// Lazy initialization
		estrPrintf(&pEntireEnumString, "%s", SortOrderToStringTable[0]);
		for(i=1; i<SORTORDER_MAX; i++)
		{
			estrConcatf(&pEntireEnumString, "|%s", SortOrderToStringTable[i]);
		}
	}

	return pEntireEnumString;
}

void sortBySortOrder(ErrorEntry **ppEntries, SortOrder eSortOrder, bool bDescending)
{
	switch(eSortOrder)
	{
		xcase SORTORDER_NEWEST:		DO_SORT(SortByNewest, bDescending);
		xcase SORTORDER_COUNT:		DO_SORT(SortByCount,  bDescending);
		xcase SORTORDER_ID:  		DO_SORT(SortByID,     bDescending);
		xcase SORTORDER_TWOWEEKCOUNT:
			DO_SORT(SortByTwoWeekCount, bDescending);

		xdefault:					printf("sortBySortOrder(): Unknown sort order %d\n", eSortOrder);
	};
}

// -------------------------------------------------------------------------------------------
// Search Code
static ErrorEntry **sppTimeSortedEntries = NULL;
static int entryCount = 0;

void initializeEntries(void)
{
	ContainerIterator iter = {0};
	Container *con;

	entryCount = 0;
	eaClear(&sppTimeSortedEntries);
	objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		ErrorEntry * pEntry = CONTAINER_ENTRY(con);
		eaPush(&sppTimeSortedEntries, pEntry);
		con = objGetNextContainerFromIterator(&iter);
		entryCount++;
	}
	objClearContainerIterator(&iter);
	sortBySortOrder(sppTimeSortedEntries, SORTORDER_NEWEST, true);
	assert(entryCount == eaSize(&sppTimeSortedEntries));
}

void addEntryToSortedSearch(ErrorEntry *pEntry, bool isMerge)
{
	int idx = 0;
	int newcount = eaSize(&sppTimeSortedEntries);
	PERFINFO_AUTO_START_FUNC();
	
	// remove entry if already in there
	if (isMerge)
	{
		eaFindAndRemove(&sppTimeSortedEntries, pEntry);
		newcount = eaSize(&sppTimeSortedEntries);
		assert (entryCount == newcount || entryCount == newcount+1);
	}

	// find place to insert new (or just updated) entry
	for (idx = newcount - 1; idx >= 0; idx--)
	{
		if (sppTimeSortedEntries[idx]->uNewestTime <= pEntry->uNewestTime)
		{
			eaInsert(&sppTimeSortedEntries, pEntry, idx+1);
			break;
		}
	}
	entryCount = newcount+1;
	PERFINFO_AUTO_STOP();
}

void removeEntryFromSortedSearch(ErrorEntry *pEntry)
{
	eaFindAndRemove(&sppTimeSortedEntries, pEntry);
	entryCount = eaSize(&sppTimeSortedEntries);
}

void destroySortedSearch(void)
{
	eaDestroy(&sppTimeSortedEntries);
	entryCount = 0;
}

bool searchMatchesSimple(ErrorEntry *pEntry, SearchData *pData)
{
	return searchMatches(pData, pEntry, 0);
}

bool searchMatches(SearchData *pData, ErrorEntry *pEntry, U32 uExactID)
{
	int uTime;
	ExactData *pExactData = NULL;

	PERFINFO_AUTO_START_FUNC();

	uTime = timeSecondsSince2000();

	if(uExactID)
		pExactData = searchGetExactData(pData, uExactID);

	if(pData->uFlags & SEARCHFLAG_EXACT_ENABLED)
	{
		// All Exact-Only checks go here
		// Note: This code makes the assumption that entries checked in Exact mode
		//       are most likely single entries read from disk. This allows for 
		//       perfect time range checks (amongst other things).

		// Even if we're not in the second pass (where uExactID is non-zero),
		// any early out conditions we make here can save us from potentially
		// thousands of disk hits. 

		// If you add checks in here, be sure to think about if they need to be 
		// different whether or not you're looking at the toplevel ET entry or
		// a single crash's worth (nonzero uExactID == single entry)

		if(pData->uFlags & SEARCHFLAG_EXACT_STARTDATE)
		{
			if(pEntry->uNewestTime < pData->uExactStartTime)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}

		if(pData->uFlags & SEARCHFLAG_EXACT_ENDDATE)
		{
			if(pEntry->uFirstTime > pData->uExactEndTime)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	if(pData->uFlags & SEARCHFLAG_TYPE)
	{
		if(!(pData->uTypeFlags & SEARCH_TYPE_TO_FLAG(pEntry->eType)))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_DAYSAGO)
	{
		int daysAgo = calcElapsedDays(pEntry->uNewestTime, uTime);
		if(daysAgo > pData->iDaysAgo)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (pData->iJira)
	{
		if (pData->iJira == 1 && pEntry->pJiraIssue == NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
		if (pData->iJira == 2 && pEntry->pJiraIssue != NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->bHideProgrammers)
	{
		if(!ArtistGotIt(pEntry))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->bHideJiraAssigned)
	{
		if(pEntry->pJiraKey)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(!pData->bShowDuplicates)
	{
		if(pEntry->uMergeID)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_CALLSTACK_FUNC)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppStackTraceLines, i, s);
		{
			if(strstri(pEntry->ppStackTraceLines[i]->pFunctionName, pData->pCallStackFunc))
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_ERROR_TEXT)
	{
		if(pEntry->pErrorString)
		{
			if(!strstri(pEntry->pErrorString, pData->pErrorText))
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		else
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_DATA_FILE)
	{
		if(pEntry->pDataFile == NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		if(!strstri(pEntry->pDataFile, pData->pDataFile))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_PRODUCT_NAME)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->eaProductOccurrences, i, s);
		{
			if(strstri_safe(pEntry->eaProductOccurrences[i]->key, pData->pProductName))
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_USER_NAME)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppUserInfo, i, s);
		{
			if (pEntry->ppUserInfo[i]->pUserName)
			{
				if(strstri(pEntry->ppUserInfo[i]->pUserName, pData->pUserName))
				{
					bFound = true;
					break;
				}
			}
			else
			{
				Errorf("Null username in ETID %i", pEntry->uID);
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_EXPRESSION)
	{
		if(pEntry->pExpression == NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		if(!strstri(pEntry->pExpression, pData->pExpression))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (pData->uFlags & SEARCHFLAG_CATEGORY)
	{
		if(pEntry->pCategory == NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
		if (!strstri(pEntry->pCategory, pData->pCategory))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (pData->uFlags & SEARCHFLAG_SUMMARY)
	{
		if(pEntry->pErrorSummary == NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
		if (!strstri(pEntry->pErrorSummary, pData->pSummary))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_VERSION)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppVersions, i, s);
		{
			if(pEntry->ppVersions[i] && strstri(pEntry->ppVersions[i], pData->pVersion))
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_SHARDINFOSTRING)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppShardInfoStrings, i, s);
		{
			if(strstri(pEntry->ppShardInfoStrings[i], pData->pShardInfoString))
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_BRANCH)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppBranches, i, s);
		{
			if(pEntry->ppBranches[i] && strstri(pEntry->ppBranches[i], pData->pBranch))
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_PLATFORM)
	{
		bool bFound = false;
		
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppPlatformCounts, i, s);
		{
			if(pEntry->ppPlatformCounts[i]->ePlatform == pData->ePlatform)
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & (SEARCHFLAG_EXECUTABLE|SEARCHFLAG_EXACT_ENABLED))
	{
		bool bCheckExecutables = !!(pData->uFlags & SEARCHFLAG_EXECUTABLE);
		bool bFound = !bCheckExecutables; // Pretend we found a match if we're not searching by EXE names
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppExecutableNames, i, s);
		{
			if( !bCheckExecutables || strstri_safe(pEntry->ppExecutableNames[i], pData->pExecutable))
			{
				bFound = true;
				if(pExactData)
					exactAddExecutable(pExactData, pEntry->ppExecutableNames[i]);
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_IP)
	{
		bool bFound = false;
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppIPCounts, i, s);
		{
			struct in_addr ina = {0};
			ina.S_un.S_addr = pEntry->ppIPCounts[i]->uIP;
			if(strstri(inet_ntoa(ina), pData->pIP))
			{
				bFound = true;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!bFound)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_LARGESTMEM)
	{
		if(pEntry->pLargestMemory == NULL)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		if(!strstri(pEntry->pLargestMemory, pData->pMemoryAlloc))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (pData->uFlags & SEARCHFLAG_GLOBALTYPE)
	{
		EARRAY_CONST_FOREACH_BEGIN(pEntry->ppAppGlobalTypeNames, i, s);
		{
			if (stricmp_safe(pEntry->ppAppGlobalTypeNames[i], pData->pGlobalType) != 0)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		EARRAY_FOREACH_END;
	}

	PERFINFO_AUTO_STOP();
	return true;
}

static int searchMatchesExact(SearchData *pData, ErrorEntry *pEntry)
{
	ExactData *pExactData;
	char eePath[MAX_PATH];
	char ** eeFiles;
	int count = 0;
	
	// Walk all individual recordings of an entry to get a true search match count

	GetErrorEntryDir(getRawDataDir(), pEntry->uID, SAFESTR(eePath));
	backSlashes(eePath);
	eeFiles = fileScanDirNoSubdirRecurse(eePath);
	EARRAY_FOREACH_BEGIN(eeFiles, i);
	{
		ErrorEntry ee = {0};
		backSlashes(eeFiles[i]);

		if(ParserReadTextFile(eeFiles[i], parse_ErrorEntry, &ee, 0))
		{
			if(searchMatches(pData, &ee, pEntry->uID))
				count++;

			StructDeInit(parse_ErrorEntry, &ee);
		}
	}
	EARRAY_FOREACH_END;

	pExactData = searchGetExactData(pData, pEntry->uID);
	pExactData->iCount = count;

	eaDestroy(&eeFiles);
	return count;
}

int searchGetExactCount(SearchData *pData, U32 uID)
{
	ExactData *pExactData = searchGetExactData(pData, uID);
	if(pExactData)
		return pExactData->iCount;

	return 0;
}

ErrorEntry * internalSearchNext(ErrorTrackerContext *pContext, SearchData *pData, int iStartingIndex)
{
	ErrorEntry *pEntry = NULL;
	if (iStartingIndex < 0 || iStartingIndex >= eaSize(&pData->ppSortedEntries))
		return NULL;
	pEntry = pData->ppSortedEntries[iStartingIndex];
	pData->iNextIndex++;
	return pEntry;
}

bool internalSearch(SearchData *pData)
{
	int i;
	bool success = true;
	U32 time;

	PERFINFO_AUTO_START_FUNC();

	time = timerAlloc();

	if(pData->uFlags & SEARCHFLAG_EXACT_ENABLED)
	{
		if(!pData->exactData)
			pData->exactData = stashTableCreateInt(64);
	}

	for (i=0; i<eaSize(&sppTimeSortedEntries); i++)
	{
		bool bMatches = false;
		ErrorEntry * pEntry = sppTimeSortedEntries[i];

		if(!(pData->uFlags & SEARCHFLAG_DONT_TIMEOUT))
		{
			if(timerElapsed(time) > MAX_SEARCH_SECONDS)
			{
				success = false;
				pData->bTimedOut = true;
				break;
			}
		}

		if(searchMatches(pData, pEntry, 0))
		{
			// Don't bother to hit the disk until we know it matches "un-exactly"
			if(pData->uFlags & SEARCHFLAG_EXACT_ENABLED)
			{
				bMatches = (searchMatchesExact(pData, pEntry) > 0) ? 1 : 0;
			}
			else
			{
				bMatches = true;
			}
		}
		
		if(bMatches)
		{
			eaPush(&pData->ppSortedEntries, pEntry);
		}
	}
	pData->iNextIndex = 0;

	timerFree(time);

	PERFINFO_AUTO_STOP_FUNC();

	return success;
}

ErrorEntry * searchFirst(ErrorTrackerContext *pContext, SearchData *pData)
{
	ErrorEntry *pEntry = NULL;

	pData->bTimedOut = false;

	if(internalSearch(pData))
	{
		if(pData->uFlags & SEARCHFLAG_SORT && (pData->eSortOrder != SORTORDER_NEWEST || !pData->bSortDescending))
		{
			sortBySortOrder(pData->ppSortedEntries, pData->eSortOrder, pData->bSortDescending);
		}

		pEntry = internalSearchNext(pContext, pData, 0);
	}
	else
	{
		printf("Search cancelled, exceeded %d seconds in search time!\n", MAX_SEARCH_SECONDS);
	}

	if(pEntry == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}

	return pEntry;
}

ErrorEntry * searchNext(ErrorTrackerContext *pContext, SearchData *pData)
{
	ErrorEntry *pEntry = internalSearchNext(pContext, pData, pData->iNextIndex);

	if(pEntry == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}

	return pEntry;
}

static void destroyExactData(ExactData* pExactData)
{
	StructDestroy(parse_ExactData, pExactData);
}

void searchEnd(SearchData *pData)
{
	if (pData)
	{
		eaDestroy(&pData->ppSortedEntries);
		eaDestroyStruct(&pData->ppExactModeMatches, parse_SearchExactMatch);

		if(pData->exactData)
		{
			stashTableDestroyEx(pData->exactData, NULL, destroyExactData);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// ErrorEntry search functions
//
// In all cases, these functions return false if the entry doesn't exist.
// ---------------------------------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesType");
bool errorSearch_MatchesType(SA_PARAM_NN_VALID ErrorEntry *pEntry, U32 uFlag)
{
	if(!pEntry) return false;
	if(!(uFlag & SEARCH_TYPE_TO_FLAG(pEntry->eType))) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesDaysAgo");
bool errorSearch_MatchesDaysAgo(SA_PARAM_NN_VALID ErrorEntry *pEntry, int days)
{
	int daysAgo;
	if(!pEntry) return false;
	daysAgo = calcElapsedDays(pEntry->uNewestTime, timeSecondsSince2000());
	if(daysAgo > days) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesJira");
bool errorSearch_MatchesJira(SA_PARAM_NN_VALID ErrorEntry *pEntry, int jira)
{
	if(!pEntry) return false;
	if(jira == 1 && pEntry->pJiraIssue == NULL) return false;
	if(jira == 2 && pEntry->pJiraIssue != NULL) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesHideProgrammers");
bool errorSearch_MatchesHideProgrammers(SA_PARAM_NN_VALID ErrorEntry *pEntry)
{
	if(!pEntry) return false;
	if(!ArtistGotIt(pEntry)) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesHideJira");
bool errorSearch_MatchesHideJira(SA_PARAM_NN_VALID ErrorEntry *pEntry)
{
	if(!pEntry) return false;
	if(pEntry->pJiraKey) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesHideDuplicates");
bool errorSearch_MatchesHideDuplicates(SA_PARAM_NN_VALID ErrorEntry *pEntry)
{
	if(!pEntry) return false;
	if(pEntry->uMergeID) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesCallstackFunction");
bool errorSearch_MatchesCallstackFunction(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pFunc)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppStackTraceLines); i++)
	{
		if(strstri_safe(pEntry->ppStackTraceLines[i]->pFunctionName, pFunc))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesErrorText");
bool errorSearch_MatchesErrorText(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pErrorText)
{
	if(!pEntry) return false;
	if(pEntry->pErrorString)
	{
		if(!strstri_safe(pEntry->pErrorString, pErrorText))
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesDataFile");
bool errorSearch_MatchesDataFile(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pDataFile)
{
	if(!pEntry) return false;
	if(!pEntry->pDataFile) return false;
	if(!strstri_safe(pEntry->pDataFile, pDataFile)) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesProductName");
bool errorSearch_MatchesProductName(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pProductName)
{
	bool bFound = false;
	if(!pEntry) return false;
	EARRAY_CONST_FOREACH_BEGIN(pEntry->eaProductOccurrences, i, s);
	{
		if(strstri_safe(pEntry->eaProductOccurrences[i]->key, pProductName))
		{
			bFound = true;
			break;
		}
	}
	EARRAY_FOREACH_END;

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesUserName");
bool errorSearch_MatchesUserName(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pUserName)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppUserInfo); i++)
	{
		if(strstri_safe(pEntry->ppUserInfo[i]->pUserName, pUserName))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesExpression");
bool errorSearch_MatchesExpression(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pExpression)
{
	if(!pEntry) return false;
	if(!pEntry->pExpression) return false;
	if(!strstri_safe(pEntry->pExpression, pExpression)) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesCategory");
bool errorSearch_MatchesCategory(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pCategory)
{
	if(!pEntry) return false;
	if(!pEntry->pCategory) return false;
	if (!strstri_safe(pEntry->pCategory, pCategory)) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesSummary");
bool errorSearch_MatchesSummary(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pSummary)
{
	if(!pEntry) return false;
	if(!pEntry->pErrorSummary) return false;
	if (!strstri_safe(pEntry->pErrorSummary, pSummary)) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesVersion");
bool errorSearch_MatchesVersion(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pVersion)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppVersions); i++)
	{
		if(strstri_safe(pEntry->ppVersions[i], pVersion))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesShardInfo");
bool errorSearch_MatchesShardInfo(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pShardInfo)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppShardInfoStrings); i++)
	{
		if(strstri_safe(pEntry->ppShardInfoStrings[i], pShardInfo))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesBranch");
bool errorSearch_MatchesBranch(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pBranch)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppBranches); i++)
	{
		if(strstri_safe(pEntry->ppBranches[i], pBranch))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesPlatform");
bool errorSearch_MatchesPlatform(SA_PARAM_NN_VALID ErrorEntry *pEntry, int ePlatform)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppPlatformCounts); i++)
	{
		if(pEntry->ppPlatformCounts[i]->ePlatform == ePlatform)
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesExecutable");
bool errorSearch_MatchesExecutable(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pExecutable)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppExecutableNames); i++)
	{
		if(strstri_safe(pEntry->ppExecutableNames[i], pExecutable))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesIP");
bool errorSearch_MatchesIP(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pIP)
{
	int i;
	bool bFound = false;
	if(!pEntry) return false;
	for(i=0; i<eaSize(&pEntry->ppIPCounts); i++)
	{
		struct in_addr ina = {0};
		ina.S_un.S_addr = pEntry->ppIPCounts[i]->uIP;
		if(strstri_safe(inet_ntoa(ina), pIP))
		{
			bFound = true;
			break;
		}
	}

	if(!bFound) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesLargestMem");
bool errorSearch_MatchesLargestMem(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pMem)
{
	if(!pEntry) return false;
	if(!pEntry->pLargestMemory) return false;
	if(!strstri_safe(pEntry->pLargestMemory, pMem)) return false;
	return true;
}

AUTO_EXPR_FUNC(Error) ACMD_NAME("ErrorSearch_MatchesGlobalType");
bool ErrorSearch_MatchesGlobalType(SA_PARAM_NN_VALID ErrorEntry *pEntry, const char *pGlobalType)
{
	if(!pEntry) return false;
	EARRAY_CONST_FOREACH_BEGIN(pEntry->ppAppGlobalTypeNames, i, s);
	{
		if (stricmp_safe(pEntry->ppAppGlobalTypeNames[i], pGlobalType) != 0)
			return false;
	}
	EARRAY_FOREACH_END;
	return true;
}

void errorSearch_BeginExpression(SearchData *pData, NonBlockingQueryCB pCompleteCB, void *userdata)
{
	char *pExpression = NULL;

#define EXPR_CONCAT_AND(expr) if (expr) estrConcatf(&expr, " and ");

	if(pData->uFlags & SEARCHFLAG_TYPE)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesType(me, %u)", pData->uTypeFlags);
	}

	if(pData->uFlags & SEARCHFLAG_DAYSAGO)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesDaysAgo(me, %d)", pData->iDaysAgo);
	}

	if (!pData->bShowDuplicates)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesHideDuplicates(me)");
	}

	if (pData->iJira)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesJira(me, %d)", pData->iJira);
	}

	if(pData->bHideJiraAssigned)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesHideJira(me)");
	}

	if(pData->uFlags & SEARCHFLAG_PLATFORM)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesPlatform(me, %d)", pData->ePlatform);
	}

	if(pData->uFlags & SEARCHFLAG_LARGESTMEM)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesLargestMem(me, \"%s\")", pData->pMemoryAlloc);
	}

	if(pData->uFlags & SEARCHFLAG_EXPRESSION)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesExpression(me, \"%s\")", pData->pExpression);
	}

	if (pData->uFlags & SEARCHFLAG_CATEGORY)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesCategory(me, \"%s\")", pData->pCategory);
	}

	if (pData->uFlags & SEARCHFLAG_SUMMARY)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesSummary(me, \"%s\")", pData->pSummary);
	}

	if(pData->uFlags & SEARCHFLAG_DATA_FILE)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesDataFile(me, \"%s\")", pData->pDataFile);
	}

	if(pData->uFlags & SEARCHFLAG_ERROR_TEXT)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesErrorText(me, \"%s\")", pData->pErrorText);
	}

	if(pData->uFlags & SEARCHFLAG_IP)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesIP(me, \"%s\")", pData->pIP);
	}

	if(pData->uFlags & SEARCHFLAG_USER_NAME)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesUserName(me, \"%s\")", pData->pUserName);
	}

	if(pData->uFlags & SEARCHFLAG_PRODUCT_NAME)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesProductName(me, \"%s\")", pData->pProductName);
	}

	if(pData->uFlags & SEARCHFLAG_EXECUTABLE)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesExecutable(me, \"%s\")", pData->pExecutable);
	}

	if(pData->uFlags & SEARCHFLAG_BRANCH)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesBranch(me, \"%s\")", pData->pBranch);
	}

	if(pData->uFlags & SEARCHFLAG_CALLSTACK_FUNC)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesCallstackFunction(me, \"%s\")", pData->pCallStackFunc);
	}

	if(pData->uFlags & SEARCHFLAG_VERSION)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesVersion(me, \"%s\")", pData->pVersion);
	}

	if(pData->uFlags & SEARCHFLAG_SHARDINFOSTRING)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesShardInfo(me, \"%s\")", pData->pShardInfoString);
	}

	if(pData->bHideProgrammers)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesHideProgrammers(me)");
	}
	
	if(pData->uFlags & SEARCHFLAG_GLOBALTYPE && pData->pGlobalType)
	{
		EXPR_CONCAT_AND(pExpression);
		estrConcatf(&pExpression, "ErrorSearch_MatchesGlobalType(me, \"%s\")", pData->pGlobalType);
	}

#undef EXPR_CONCAT_AND

	NonBlockingContainerQuery(GLOBALTYPE_ERRORTRACKERENTRY, pExpression, "Error", 0, pCompleteCB, userdata);
	estrDestroy(&pExpression);
}

void errorSearch_Begin(SearchData *pData, NonBlockingQueryCB pCompleteCB, void *userdata)
{
	NonBlockingContainerSearch(GLOBALTYPE_ERRORTRACKERENTRY, searchMatchesSimple, NULL, pData, 0, pCompleteCB, userdata);
}

#include "Search_h_ast.c"
