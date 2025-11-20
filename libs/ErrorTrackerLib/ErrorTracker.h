#ifndef ERRORTRACKER_H
#define ERRORTRACKER_H

#include "ErrorTrackerLib.h"
#include "ETCommon/ETCommonStructs.h"
#include "AutoGen/ETCommonStructs_h_ast.h"

#define ETINDEX_START 1

bool ErrorTrackerExitTriggered(void);
void initErrorTracker(void);
void terminateSymServLookup(void);
void shutdownErrorTracker(void);
void updateErrorTracker(void);
void SaveJiraUpdates(void);
const char *getRawDataDir(void);

typedef struct NetComm NetComm;
NetComm *errorTrackerCommDefault();

typedef struct ErrorEntry ErrorEntry;
extern ErrorTrackerSettings gErrorTrackerSettings;

typedef enum GlobalType GlobalType;

typedef struct IncomingData IncomingData;
typedef struct NetLink NetLink;
typedef struct IncomingClientState IncomingClientState;
void errorTrackerSendStatusUpdate(NetLink *link, int update_code);
void ProcessEntry(NetLink *link, IncomingClientState *pClientState, NOCONST(ErrorEntry) *pEntry);
void ProcessEntry_Finish (NetLink *link, SA_PARAM_NN_VALID ErrorEntry *pMergedEntry, NOCONST(ErrorEntry) *pEntry, int iETIndex, NOCONST(TriviaData) **ppTriviaData);

ErrorEntry * getErrorTrackerEntryByIndex(ErrorTrackerContext *pContext, int i);

// Calls all callbacks and stuff, should only be used internally
void errorTrackerOnNewError(ErrorTrackerContext *pContext, NOCONST(ErrorEntry) *pNewEntry, ErrorEntry *pMergedEntry);

void errorTrackerCleanUpDumps(void);
void performMigration(void);

void errorTrackerEntryDeleteEx (SA_PARAM_NN_VALID ErrorEntry *pEntry, bool bDeleteDumps, const char *file, int line);
#define errorTrackerEntryDelete(pEntry, bDeleteDumps) errorTrackerEntryDeleteEx(pEntry, bDeleteDumps, __FILE__, __LINE__)
//void errorTrackerEntryDelete (SA_PARAM_NN_VALID ErrorEntry *con, bool bDeleteDumps);
void errorTrackerEntryCreateStub(ErrorEntry *pEntry, U32 uMergeID);
int RemoveOldEntries(U32 timeCurrent, U32 maxAgeInSeconds, bool bOnlyNonfatals);

bool loadErrorEntry(U32 uID, U32 uIndex, NOCONST(ErrorEntry) *output);
int errorTrackerCleanupNonfatals(void);
int errorTrackerTrimUsers(void);

void LogAllTriviaOverviews();

NetComm *getSymbolServerComm(void);

void recordErrorEntry(U32 uID, U32 uIndex, ErrorEntry *pEntry);

AUTO_STRUCT;
typedef struct ErrorTrackerHashWrapper
{
	U32 uID; AST(KEY)
	char *pGenericLabel; AST(ESTRING) // does not need to be unique
	U32 aiHash[4];
} ErrorTrackerHashWrapper;

AUTO_STRUCT;
typedef struct ErrorTrackerConfig
{
	U32 uLastKey;
	EARRAY_OF(ErrorTrackerHashWrapper) ppGenericHashes;
} ErrorTrackerConfig;

// --------------------------------------------------------

void errorTrackerLoadConfig(void);
void errorTrackerSaveConfig(void);
bool errorTrackerIsGenericHash (ErrorEntry *pEntry, char **estr);

void errorTrackerAddGenericHash(ErrorEntry *pEntry, const char *genericLabel);
void errorTrackerUndoGenericHash(ErrorEntry *pEntry);

#endif
