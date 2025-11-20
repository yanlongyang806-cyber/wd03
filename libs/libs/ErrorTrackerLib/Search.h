#ifndef ERRORTRACKER_SEARCH_H
#define ERRORTRACKER_SEARCH_H

#include "ErrorTracker.h"
#include "StashTable.h"
#include "objContainer.h"

typedef struct ErrorTrackerContext ErrorTrackerContext;
typedef void (*ExactCompleteCallback)(SearchData *pData, ErrorEntry **foundEntries, void *userData);


// Note: The table in Search.c must be updated if this enum is changed
AUTO_ENUM;
typedef enum SortOrder
{
	SORTORDER_NEWEST = 0,
	SORTORDER_COUNT,
	SORTORDER_ID,
	SORTORDER_TWOWEEKCOUNT,

	SORTORDER_MAX
} SortOrder;

const char * sortOrderToString(SortOrder eSortOrder);
const char * sortOrderEntireEnumString(void);

#define SEARCHFLAG_SORT				(1 <<  0)
#define SEARCHFLAG_TYPE				(1 <<  1)
#define SEARCHFLAG_CALLSTACK_FUNC	(1 <<  2)
#define SEARCHFLAG_ERROR_TEXT		(1 <<  3)
#define SEARCHFLAG_DATA_FILE        (1 <<  4)
#define SEARCHFLAG_PRODUCT_NAME     (1 <<  5)
#define SEARCHFLAG_USER_NAME        (1 <<  6)
#define SEARCHFLAG_EXPRESSION       (1 <<  7)
#define SEARCHFLAG_ANY              (1 <<  8)
#define SEARCHFLAG_VERSION          (1 <<  9)
#define SEARCHFLAG_DAYSAGO          (1 << 10)
#define SEARCHFLAG_PLATFORM         (1 << 11)
#define SEARCHFLAG_EXECUTABLE       (1 << 12)
#define SEARCHFLAG_IP               (1 << 13)
#define SEARCHFLAG_CATEGORY			(1 << 14)
#define SEARCHFLAG_SUMMARY          (1 << 15)
#define SEARCHFLAG_BRANCH			(1 << 16)
#define SEARCHFLAG_EXACT_ENABLED    (1 << 17)
#define SEARCHFLAG_EXACT_STARTDATE  (1 << 18)
#define SEARCHFLAG_EXACT_ENDDATE    (1 << 19)
#define SEARCHFLAG_DONT_TIMEOUT     (1 << 20) // Allows a search to block the ET (yuck!)
#define SEARCHFLAG_EXACT_INTERNAL   (1 << 21) // Exact search to be used internally; Don't prune by IP, don't publish to web, auto-prune post-callback
#define SEARCHFLAG_SHARDINFOSTRING  (1 << 22)
#define SEARCHFLAG_LARGESTMEM		(1 << 23)
#define SEARCHFLAG_GLOBALTYPE		(1 << 24)

#define SEARCH_TYPE_TO_FLAG(A) (1 << (int)A)
#define SEARCH_TYPE_ENABLED(FLAGS, TYPE) (FLAGS & SEARCH_TYPE_TO_FLAG(TYPE))
#define SEARCH_ALL_TYPES 0xffffffff

AUTO_STRUCT;
typedef struct SearchExactMatch
{
	U32 uIndexID; AST(KEY)
	char *filePath; AST(ESTRING)
	U32 uID;
	U32 uFileIndex;
	U32 uTime;
} SearchExactMatch;

AUTO_STRUCT;
typedef struct SearchData
{
	// Core flags, determines what filters are active
	U32 uFlags;

	// Search Request hit time limit
	bool bTimedOut;

	// Basic bools
	bool bHideProgrammers; // Don't show errors and crashes that only programmers have seen.
	bool bHideJiraAssigned;
	bool bShowDuplicates;

	// SEARCHFLAG_SORT
	SortOrder eSortOrder;
	bool bSortDescending;
	
	// SEARCHFLAG_TYPE
	U32 uTypeFlags;

	// SEARCHFLAG_CALLSTACK_FUNC
	char *pCallStackFunc;

	// SEARCHFLAG_ERROR_TEXT
	char *pErrorText;

	// SEARCHFLAG_DATA_FILE
	char *pDataFile;

	// SEARCHFLAG_PRODUCT_NAME
	char *pProductName;

	// SEARCHFLAG_USER_NAME
	char *pUserName;

	// SEARCHFLAG_EXPRESSION
	char *pExpression;

	// SEARCHFLAG_CATEGORY
	char *pCategory;

	// SEARCHFLAG_SUMMARY
	char *pSummary;

	// SEARCHFLAG_ANY - Just search for "anything" ... searches multiple fields
	char *pAny;

	// SEARCHFLAG_VERSION
	char *pVersion;

	// SEARCHFLAG_SHARDINFOSTRING
	char *pShardInfoString;

	// SEARCHFLAG_BRANCH
	char *pBranch;

	// SEARCHFLAG_DAYSAGO
	int iDaysAgo;

	// SEARCHFLAG_PLATFORM
	Platform ePlatform;

	// SEARCHFLAG_EXECUTABLE
	char *pExecutable;

	// SEARCHFLAG_IP
	char *pIP;

	// SEARCHFLAG_LARGESTMEM
	char *pMemoryAlloc;

	// SEARCHFLAG_GLOBALTYPE
	char *pGlobalType;

	// JIRA search, no actual flag; 0 = ignore, 1 = has jira, 2 = no jira
	int iJira;

	// For exact-count searches
	StashTable exactData; NO_AST
	U32 uExactStartTime;
	U32 uExactEndTime;
	U32 uLastID; NO_AST
	EARRAY_OF(SearchExactMatch) ppExactModeMatches; // list of files exact matched

	// Callbacks on internal searches
	ExactCompleteCallback exactCompleteCallback; NO_AST

	// Search state
	ErrorEntry **ppSortedEntries; NO_AST
	int iNextIndex;
} SearchData;

typedef struct ErrorEntry ErrorEntry;
void initializeEntries(void);
void destroySortedSearch(void);
void removeEntryFromSortedSearch(ErrorEntry *pEntry);
void addEntryToSortedSearch(ErrorEntry *pEntry, bool isMerge);
bool searchMatches(SearchData *pData, ErrorEntry *pEntry, U32 uExactID);

AUTO_STRUCT;
typedef struct ExactData
{
	int iCount;
	char **ppExecutableNames;
} ExactData;

ExactData * searchGetExactData(SearchData *pSearchData, U32 uID);
int searchGetExactCount(SearchData *pData, U32 uID);

ErrorEntry * searchFirst(ErrorTrackerContext *pContext, SearchData *pData);
ErrorEntry * searchNext (ErrorTrackerContext *pContext, SearchData *pData);
void searchEnd (SearchData *pData);

void errorSearch_BeginExpression(SearchData *pData, NonBlockingQueryCB pCompleteCB, void *userdata);
void errorSearch_Begin(SearchData *pData, NonBlockingQueryCB pCompleteCB, void *userdata);

void sortBySortOrder(ErrorEntry **ppEntries, SortOrder eSortOrder, bool bDescending);

#endif
