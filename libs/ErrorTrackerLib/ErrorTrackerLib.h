#ifndef ERRORTRACKERLIB_H
#define ERRORTRACKERLIB_H

// --------------------------------------------------------
// Context

typedef struct ErrorTrackerContext ErrorTrackerContext;
typedef struct ErrorEntry ErrorEntry;
typedef struct ErrorTrackerEntryUserData ErrorTrackerEntryUserData;
typedef struct ErrorTrackerEntryList ErrorTrackerEntryList;
typedef struct DumpData DumpData;
typedef struct ErrorData ErrorData;
typedef struct SearchData SearchData;
typedef struct NOCONST(ErrorEntry) NOCONST(ErrorEntry);
typedef struct ErrorTrackerSettings ErrorTrackerSettings;
typedef enum CBErrorStatus CBErrorStatus;
typedef enum GlobalType GlobalType;

void errorTrackerLibDestroyContext(ErrorTrackerContext *pContext);

// ErrorTrackerEntryList is in ErrorTracker.h
ErrorTrackerEntryList * errorTrackerLibGetEntries(ErrorTrackerContext *pContext); // Just get the full list of entries

// ErrorEntry is in ErrorTracker.h, SearchData is in Search.h
ErrorEntry * errorTrackerLibSearchFirst(ErrorTrackerContext *pContext, SearchData *pSearchData);
ErrorEntry * errorTrackerLibSearchNext (ErrorTrackerContext *pContext, SearchData *pSearchData);

// Processing calls for CB
// ErrorData is in errornet.h
void errorTrackerLibProcessNewErrorData(ErrorTrackerContext *pContext, ErrorData *pErrorData);
void errorTrackerLibSetCBErrorStatus (GlobalType eErrorType, ErrorEntry *pEntry, CBErrorStatus eStatus);

void errorTrackerLibStallUntilTransactionsComplete(void);

typedef void (*NewEntryCallback)(ErrorTrackerContext *pContext, ErrorEntry *pNewEntry, ErrorEntry *pMergedEntry);
void errorTrackerLibSetNewEntryCallback(NewEntryCallback pCallback);

// Used internally upon getting new errors from anywhere
void errorTrackerLibCallNewEntryCallback(ErrorTrackerContext *pContext, NOCONST(ErrorEntry) *pNewEntry, ErrorEntry *pMergedEntry);


// --------------------------------------------------------
// Core functionality

typedef struct FolderCache FolderCache;
typedef struct FolderNode FolderNode;
// Optional config reload callback
void errorTrackerLibReloadConfig(FolderCache * pFolderCache, FolderNode * pFolderNode, 
	int iVirtualLocation, const char * szRelPath, int iWhen, void * pUserData);

// The real error tracker passes in zero for uOptions
bool errorTrackerLibInit(U32 uOptions,                      // ERRORTRACKER_OPTION_*
						 U32 uWebOptions,                   // DUMPENTRY_FLAG_*
						 ErrorTrackerSettings *pSettings);

void errorTrackerLibShutdown(void);
void errorTrackerLibOncePerFrame(void);
void errorTrackerLibOncePerFrame_MainThread(void); // These two functions are for the CB
void errorTrackerLibOncePerFrame_SubThread(void); // These two functions are for the CB
void errorTrackerLibUpdateBlameCache(void);  // Starts up any queued SVN Blame queries in their own threads, 
                                             // use errorTrackerLibFlush() to wait for completion
void errorTrackerLibFlush(void);

void errorTrackerLibWaitForDumpConnection(F32 timeout); // For use in the continuous builder, when it thinks / knows another dump is coming

bool hashMatchesU32(const U32 *h1, const U32 *h2);
bool hashMatches(ErrorEntry *p1, ErrorEntry *p2);

void errorTrackerLibStartSummaryTable(char **estr);
void errorTrackerLibDumpEntryToSummaryTable(char **estr, ErrorEntry *pEntry);
void errorTrackerLibEndSummaryTable(char **estr);

void errorTrackerLibGetDumpFilename(char *dumpFilename, int iBufferSize, ErrorEntry *pEntry, DumpData *pDumpData, bool bGetMinidump);

ErrorEntry *errorTrackerLibLookupHashString(ErrorTrackerContext *pContext, const char *pHashString);

// --------------------------------------------------------
// HTTP related

typedef struct NetLink NetLink;

typedef bool (*requestHandlerFunc)(NetLink *link, char **args, char **values, int count);
void errorTrackerLibSetRequestHandlerFunc(requestHandlerFunc pFunc);

void errorTrackerLibSetWebRoot(const char *pRootPath); // "c:\\core\\data\\server\\...\\"
void errorTrackerLibSetFQDN(const char *pFQDN);        // "errortracker.example.com"
void errorTrackerLibSetDefaultPage(const char *pPage); // "/search"

// --------------------------------------------------------
// Utility Functions

bool errorTrackerLibGenerateErrorTrackerHandlerFile(ErrorEntry *pEntry, DumpData *pDumpData, const char *pOutputFile);



#endif
