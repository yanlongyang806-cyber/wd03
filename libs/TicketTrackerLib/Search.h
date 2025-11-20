#ifndef TICKETTRACKER_SEARCH_H
#define TICKETTRACKER_SEARCH_H

#include "TicketTracker.h"

typedef struct TicketTrackerContext TicketTrackerContext;

typedef enum SearchToggle
{
	SearchToggle_None = 0,
	SearchToggle_No,
	SearchToggle_Yes
} SearchToggle;

// Note: The table in Search.c must be updated if this enum is changed
AUTO_ENUM;
typedef enum SortOrder
{
	SORTORDER_NEWEST = 0,
	SORTORDER_ID,
	SORTORDER_GROUPID, 
	SORTORDER_REPNAME,
	SORTORDER_PRIORITY,
	SORTORDER_COUNT,

	SORTORDER_MAX,		EIGNORE
} SortOrder;

// These values are for filtering based on user subscribe times
AUTO_ENUM;
typedef enum DateSearchType
{
	DATESEARCH_EVERYTHING = 0,
	DATESEARCH_FILED,
	DATESEARCH_LAST_UPDATED, // This filters based on when ticket was LAST subscribed to
	DATESEARCH_STATUS_UPDATED, // This filters based on any time when the ticket was subscribed to

	DATESARCH_MAX,    EIGNORE
} DateSearchType;

AUTO_ENUM;
typedef enum TicketSearchInternal
{
	TICKETSEARCH_UNDEFINED = 0,
	TICKETSEARCH_INTERNAL,
	TICKETSEARCH_EXTERNAL,

	TICKETSEARCH_MAX, EIGNORE
} TicketSearchInternal;

AUTO_ENUM;
typedef enum SearchLevel
{
	SEARCH_DEFAULT = 0, // minimal permissions
	SEARCH_USER, // user searching for [own] tickets; can view private tickets
	SEARCH_ADMIN, // admin searching for tickets; can view merged and hidden tickets
	SEARCH_ADMIN_GAME,
} SearchLevel;

const char * sortOrderToString(SortOrder eSortOrder);
const char * sortOrderEntireEnumString(void);

#define SEARCHFLAG_SORT				(1 <<  0)
#define SEARCHFLAG_PRODUCT_NAME         (1 <<  5)
#define SEARCHFLAG_SUMMARY_DESCRIPTION	(1 <<  6)
#define SEARCHFLAG_GROUP			(1 << 7)
#define SEARCHFLAG_STATUS           (1 << 8)
#define SEARCHFLAG_VERSION          (1 <<  9)
#define SEARCHFLAG_PLATFORM         (1 << 11)
#define SEARCHFLAG_ASSIGNED         (1 << 12)
#define SEARCHFLAG_CHARACTER_NAME   (1 << 13)

#define SEARCHFLAG_CATEGORY			(1 << 14)
#define SEARCHFLAG_EXTRA			(1 << 15)

#define SEARCHFLAG_TIME_RANGE		(1 <<  1)
#define SEARCHFLAG_TIME_BEFORE		(1 <<  2)
#define SEARCHFLAG_ACCOUNT_NAME		(1 <<  3)
#define SEARCHFLAG_TRIVIA			(1 <<  4)

#define SEARCHFLAG_SHARD            (1 << 16)
#define SEARCHFLAG_LABEL            (1 << 17)
#define SEARCHFLAG_OCCURRENCES      (1 << 18)

#define SEARCHFLAG_DATESTART        (1 << 19)
#define SEARCHFLAG_DATEEND          (1 << 20)
#define SEARCHFLAG_LANGUAGE         (1 << 21)
#define SEARCHFLAG_VISIBILITY       (1 << 22)
#define SEARCHFLAG_CLOSEDTIME       (1 << 23)
#define SEARCHFLAG_STATUSUPDATE     (1 << 24)
#define SEARCHFLAG_MODIFIED         (1 << 25)

#define SEARCHFLAG_ANY              (1 << 10)

#define SEARCH_TYPE_TO_FLAG(A) (1 << (int)A)
#define SEARCH_TYPE_ENABLED(FLAGS, TYPE) (FLAGS & SEARCH_TYPE_TO_FLAG(TYPE))
#define SEARCH_ALL_TYPES 0xffffffff

#define SEARCH_SUMMARY_DESCRIPTION   0x0
#define SEARCH_SUMMARY               0x1
#define SEARCH_DESCRIPTION           0x2

#define SEARCH_TRIVIA_BOTH   0x0
#define SEARCH_TRIVIA_KEY    0x1
#define SEARCH_TRIVIA_VALUE  0x2

typedef bool (*ExternalTicketSearchFunc) (TicketEntry *pEntry, void * userData);

typedef enum Platform Platform;
typedef enum TicketStatus TicketStatus;

AUTO_STRUCT;
typedef struct SearchCategory
{
	int iMainCategory;
	int iCategory;
	char *pMainCategoryString;
	char *pCategoryString;
} SearchCategory;

AUTO_STRUCT;
typedef struct SearchData
{
	// Core flags, determines what filters are active
	U32 uFlags;

	// Basic bools

	// SEARCHFLAG_SORT
	SortOrder eSortOrder;
	bool bSortDescending;

	//SEARCHFLAG_ACCOUNT_NAME
	char *pAccountName;
	bool bExactAccountName;

	//SEARCHFLAG_CHARACTER_NAME
	char *pCharacterName;
	bool bExactCharacterName;

	//SEARCHFLAG_SUMMARY and/or SEARCHFLAG_DESCRIPTION
	char *pSummaryDescription;
	int iSearchSummaryDescription;

	// SEARCHFLAG_CATEGORY
	// deprecated
	int iMainCategory;				AST(NAME(MainCategoryIndex))
	int iCategory;					AST(NAME(CategoryIndex))
	char *pMainCategory;
	char *pCategory;
	// enddeprecated
	SearchCategory **ppCategories; // Array search

	// SEARCHFLAG_LANGUAGE - actually stores locale ID
	int iLocaleID;

	// SEARCHFLAG_LABEL
	char *pLabel;

	//SEARCHFLAG_TRIVIA
	char *pTrivia;
	int iSearchTriviaKeyValue;

	// SEARCHFLAG_ANY - Just search for "anything" ... searches multiple fields
	char *pAny;

	// SEARCHFLAG_VERSION
	char *pVersion;

	//SEARCHFLAG_TIME_RANGE | SEARCHFLAG_TIME_BEFORE
	char *pTimeString;

	// SEARCHFLAG_PLATFORM
	Platform ePlatform;

	// SEARCHFLAG_PRODUCT_NAME
	STRING_POOLED pProductName; AST(POOL_STRING)
	int iProductIndex;

	// SEARCHFLAG_SHARD
	char *pShardInfoString;
	char *pShardExactName;

	// SEARCHFLAG_IP
	char *pIP;

	// SEARCHFLAG_GROUP
	U32 uGroupID;
	char *pGroupName;

	// SEARCHFLAG_ASSIGNED
	char *pRepAccountName;
	int iIsAssigned; // 1 = No, 2 = Yes

	// SEARCHFLAG_STATUS
	TicketStatus eStatus;     // deprecated
	TicketStatus *peStatuses; // Array search

	// SEARCHFLAG_EXTRA
	char * pExtraSubstring;

	// SEARCHFLAG_JIRA - not an actual flag, since 0 = do not use this
	int iJira; // 1 = issues with Jiras, 2 = issues w/out Jiras

	// SEARCHFLAG_OCCURRENCES  min number
	int iOccurrences;

	// SEARCHFLAG_DATESTART, SEARCHFLAG_DATEEND
	U32 uDateStart;
	U32 uDateEnd;
	char *pDateStartString;
	char *pDateEndString;
	DateSearchType eDateType;

	// SEARCHFLAG_STATUSUPDATE
	U32 uStatusDateStart;
	U32 uStatusDateEnd;

	// SEARCHFLAG_MODIFIED
	U32 uModifiedDateStart;
	U32 uModifiedDateEnd;

	// SEARCHFLAG_CLOSEDTIME
	U32 uClosedStart;
	U32 uClosedEnd;

	// SEARCHFLAG_VISIBILITY
	int iVisible;

	// No matching search flag here - 0 = do not use
	TicketSearchInternal eInternal;

	// Search state
	TicketEntry **ppSortedEntries;						AST( UNOWNED STRUCT(parse_TicketEntryConst) NO_INDEX)
	int iNextIndex;
	int iOffset;
	int iLimit;
	int iNumberOfResults;

	SearchLevel eAdminSearch; // Admin seaches (eg. via web) ignore visibility flag and show all

	// setting this overrides ALL other search parameters (not sorting though)
	ExternalTicketSearchFunc searchFunc;				NO_AST
	void * userData;									NO_AST
} SearchData;

int searchCacheGetCategoryCountForLocale(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus, int iLocaleID);
int searchCacheGetCategoryCount(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus);

int searchCacheGetCategoryCountForProductLocale(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus, int iLocaleID, const char *productName);
int searchCacheGetCategoryCountByProduct(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus, const char *productName);
TicketEntry * searchFirstEx(SearchData *pData, TicketEntry **ppEntries);
TicketEntry * searchFirst(SearchData *pData);
TicketEntry * searchNext (SearchData *pData);
void searchEnd(SearchData *pData);

void initializeEntries(void);
void searchAddTicket(TicketEntry *pEntry);
void searchRemoveTicket(TicketEntry *pEntry);
void searchChangeTicketStatus (TicketEntry *pEntry, TicketStatus ePrevStatus);
void resortPriorities(void);

// Helper functions for filtering
bool UserInfoMatchesFilter(SearchData *pSearchData, TicketEntry *pEntry, TicketClientUserInfo *pUserInfo);
U32 FilterOccurenceCount(SearchData *pSearchData, TicketEntry *pEntry);

// For non-zero defaults
void searchDataInitialize(SA_PARAM_NN_VALID SearchData *psd);

#include "autogen/search_h_ast.h"

#endif
