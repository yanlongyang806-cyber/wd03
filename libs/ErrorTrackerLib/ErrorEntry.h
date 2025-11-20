#pragma once

typedef struct NOCONST(ErrorEntry) NOCONST(ErrorEntry);
typedef struct ErrorEntry ErrorEntry;
typedef struct NetLink NetLink;
typedef struct IncomingClientState IncomingClientState;
typedef struct ErrorData ErrorData;
typedef struct NOCONST(TriviaData) NOCONST(TriviaData);

AUTO_ENUM;
typedef enum ErrorTransactionStep
{
	ERRORTRANS_STEP_START = 0,
	ERRORTRANS_STEP_DONE,

	ERRORTRANS_STEP_RESPOND,

	ERRORTRANS_STEP_CREATENEW,
	ERRORTRANS_STEP_PROCESSTRIVIA,

	ERRORTRANS_STEP_MERGE,

	ERRORTRANS_STEP_COUNT, EIGNORE
} ErrorTransactionStep;

AUTO_ENUM;
typedef enum ETRateLimitActivity
{
	ETRLA_Test,
	ETRLA_ErrorReceive,
} ETRateLimitActivity;

AUTO_STRUCT;
typedef struct ErrorTransactionResponseData
{
	ErrorEntry *pEntry; AST(UNOWNED) // This isn't really a CONSTed struct, but AUTO_STRUCT doesn't like NOCONSTs
	U32 uMergeTime;
	int iETIndex;
	U32 linkID;
} ErrorTransactionResponseData;

AUTO_STRUCT;
typedef struct ErrorTransactionMergeQueue
{
	U32 uMergeID; AST(KEY)
	ErrorTransactionStep eStep;
	EARRAY_OF(ErrorTransactionResponseData) ppNewEntries; // 0th one oldest one being process
} ErrorTransactionMergeQueue;

typedef struct ErrorTransactionNewQueue
{
	// Note: iETIndex = 1 for new entries
	NOCONST(ErrorEntry) *pNewEntry;
	NOCONST(TriviaData) **ppTriviaData;

	U32 uNewID;
	U32 linkID;
	ErrorTransactionStep eStep;
} ErrorTransactionNewQueue;

void InitializeTriviaFilter(void);
NOCONST(ErrorEntry) * createErrorEntry_ErrorTracker(NetLink *link, IncomingClientState *pClientState, 
														 ErrorData *pErrorData, int iMergedId);

void SymSrvQueue_FindAndRemoveByLink (NetLink *link);
int SymSrvQueue_OncePerFrame(void); // returns the number of queued lookups still left

void ErrorEntry_AddMergeQueue (U32 uLinkID, SA_PARAM_NN_VALID ErrorEntry *pMerge, SA_PARAM_NN_VALID NOCONST(ErrorEntry) *pNew);
void ErrorEntry_AddNewQueue (U32 uLinkID, SA_PARAM_NN_VALID NOCONST(ErrorEntry) *pNew);
void ErrorEntry_OncePerFrame(void);
bool findUniqueString(CONST_STRING_EARRAY ppStringList, const char *pStr);
void ReprocessDumpData (ErrorEntry *pEntry, int iDumpDataIndex, NetLink *link, bool newEntry);
void ErrorEntry_EditDumpDescription(ErrorEntry *pEntry, int iDumpIndex, const char *description);

bool ErrorEntry_isNullCallstack(ErrorEntry *pEntry);
int ErrorEntry_firstValidStackFrame(ErrorEntry *pEntry);
bool ErrorEntry_AddHashStash(ErrorEntry *pEntry);
void ErrorEntry_RemoveHashStash(ErrorEntry *pEntry);

ErrorEntry ** ErrorEntry_GetGenericCrashes(void);
void ErrorEntry_MergeAndDeleteEntry (ErrorEntry *mergee, ErrorEntry *target, bool leaveStub);
bool ErrorEntry_ForceHashRecalculate (ErrorEntry *pEntry);
bool ErrorEntry_IsGenericCrash(ErrorEntry *pEntry);