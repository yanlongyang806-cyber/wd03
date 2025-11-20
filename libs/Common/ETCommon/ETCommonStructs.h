#pragma once

// For ErrorDataType
#include "errornet.h"

extern bool gbETVerbose;

bool isCrypticModule(const char *module);

void ETCommonInit(void);
U32 errorTrackerLibGetOptions(void);
void errorTrackerLibSetOptions(U32 uOptions);

#define ET_HASH_ARRAY_SIZE 4

typedef struct NetLink NetLink;
typedef struct IncomingClientState IncomingClientState;
typedef struct NOCONST(TriviaData) NOCONST(TriviaData);
typedef struct NOCONST(ErrorEntry) NOCONST(ErrorEntry);
typedef struct ErrorEntry ErrorEntry;
typedef struct TriviaOverview TriviaOverview;
typedef struct TriviaListMultiple TriviaListMultiple;
typedef enum GlobalType GlobalType;

AUTO_STRUCT AST_IGNORE(MaxDailyErrorsPerSource);
typedef struct ErrorTrackerSettings
{
	char *pErrorEmailAddress; AST(ESTRING)
	char *pProductionErrorEmailAddress; AST(ESTRING)
	char *pCrashEmailAddress; AST(ESTRING)
	char *pCrashCountEmailAddress; AST(ESTRING)
	int   iWebInterfacePort;

	char *pUnknownIPEmails; AST(ESTRING)
	char *pSymbolFailureEmails; AST(ESTRING)

	char *pDumpDir; AST(ESTRING)
	char *pDumpTempDir; AST(ESTRING)
	char **ppAlternateDumpDir; AST(ESTRING)
	char *pTempPDBDir; AST(ESTRING)
	
	char *pRawDataDir; AST(ESTRING)

	char *pJiraHost; AST(ESTRING DEFAULT("jira"))
	int iJiraPort; AST(DEFAULT(8080))

	int iMaxInfoEntries; AST(DEFAULT(20))
	U32 uTriviaAlertLimit;

	// Product Name to Short Product Name Mappings in pairs, short and then full
	// Eg. "TT, TicketTracker, ET, ErrorTracker"
	STRING_EARRAY eaSVNProductMappings;

	// These do not reload
	char *pSVNUsername; AST(ESTRING)
	char *pSVNPassword; AST(ESTRING)
	char *pSVNRoot; AST(ESTRING)
} ErrorTrackerSettings;

// --------------------------------------------------------
// Text generation

//flags to allow custom disabling of parts of text dumps, ORed together
#define DUMPENTRY_FLAG_NO_TYPE (1 << 0)
#define DUMPENTRY_FLAG_NO_UID (1 << 1)
#define DUMPENTRY_FLAG_NO_HTTP (1 << 2)
#define DUMPENTRY_FLAG_NO_TOTALCOUNT (1 << 3)
#define DUMPENTRY_FLAG_NO_USERSCOUNT (1 << 4)
#define DUMPENTRY_FLAG_NO_DAYS_AGO (1 << 5)
#define DUMPENTRY_FLAG_NO_BLAMEE (1 << 6)
#define DUMPENTRY_FLAG_NO_DATAFILE (1 << 7)
#define DUMPENTRY_FLAG_NO_EXPRESSION (1 << 8)
#define DUMPENTRY_FLAG_NO_ERRORSTRING (1 << 9)
#define DUMPENTRY_FLAG_NO_CALLSTACK (1 << 10)
#define DUMPENTRY_FLAG_NO_EXECUTABLES (1 << 11)
#define DUMPENTRY_FLAG_NO_USERS (1 << 12)
#define DUMPENTRY_FLAG_NO_DUMPINFO (1 << 13)
#define DUMPENTRY_FLAG_NO_JIRA (1 << 14)
#define DUMPENTRY_FLAG_NO_FIRSTSEEN (1 << 15)
#define DUMPENTRY_FLAG_NO_VERSIONS (1 << 16)
#define DUMPENTRY_FLAG_NO_SOURCEFILE (1 << 17)
#define DUMPENTRY_FLAG_NO_PLATFORMS (1 << 18)
#define DUMPENTRY_FLAG_NO_PRODUCTS (1 << 19)
#define DUMPENTRY_FLAG_NO_IPS (1 << 20)
#define DUMPENTRY_FLAG_NO_PROGRAMMER_REQUEST (1 << 21)
#define DUMPENTRY_FLAG_FORCE_TRIVIASTRINGS (1 << 22)
#define DUMPENTRY_FLAG_NO_DUMP_TOGGLES (1 << 23)
#define DUMPENTRY_FLAG_NO_ERRORDETAILS (1 << 24)
#define DUMPENTRY_FLAG_ALLOW_NEWLINES_IN_ERRORSTRING (1 << 25)
#define DUMPENTRY_FLAG_NO_COMMENTS (1 << 26)

// --------------------------------------------------------
// Options (and associated queries)

#define ERRORTRACKER_OPTION_DISABLE_EMAILS       0x01
#define ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE    0x02
#define ERRORTRACKER_OPTION_DISABLE_AUTO_BLAME   0x04
#define ERRORTRACKER_OPTION_FORCE_NO_DUMP		 0x08
#define ERRORTRACKER_OPTION_DISABLE_AUTO_CLEAR	 0x10
#define ERRORTRACKER_OPTION_DISABLE_DISCARD_NOT_LATEST 0x20
#define ERRORTRACKER_OPTION_WRITE_TRIVIA_DISK 0x40
#define ERRORTRACKER_OPTION_REQUEST_AUTOCLOSE_ON_ERROR 0x80
#define ERRORTRACKER_OPTION_RECORD_ALL_ERRORDATA_TO_DISK 0x100
#define ERRORTRACKER_OPTION_FORCE_RUN_SYMSERV_FROM_CORE_TOOLS_BIN 0x200
#define ERRORTRACKER_OPTION_LOG_TRIVIA 0x400
#define ERRORTRACKER_OPTION_DISABLE_ERROR_LIMITING 0x800

// Force the create new entry transactions to stall the ET until complete - Used by the CB
// Also forces the HTTP stuff to use commDefault() instead of errorTrackerCommDefault()
#define ERRORTRACKER_OPTION_FORCE_SYNTRANS 0x800 

AUTO_STRUCT AST_IGNORE(EntityDescriptors);
typedef struct ErrorTrackerEntryList
{
	ErrorEntry **ppEntries; // mostly obsolete, used only during text read/write
	U32 uNextID;
	bool bSomethingHasChanged;
	GlobalType eContainerType; AST( SUBTABLE(GlobalTypeEnum)) 
} ErrorTrackerEntryList;

AUTO_STRUCT;
typedef struct ErrorTrackerContext
{
	ErrorTrackerEntryList entryList;
	bool bCreatedContext;  NO_AST     // If false, this is the default context, and shouldn't be deleted
} ErrorTrackerContext;

// --------------------------------------------------------

#define DUMPDATAFLAGS_FULLDUMP BIT(0)
#define DUMPDATAFLAGS_MOVED BIT(1)
#define DUMPDATAFLAGS_DELETED BIT(2)
#define DUMPDATAFLAGS_REJECTED BIT(3)

#define FULLDUMP_MAX_THRESHOLD 1
#define MINIDUMP_MAX_THRESHOLD 5
#define TRIVIA_MAX_THRESHOLD 20

#define MAX_USERS_AND_IPS (25)
#define MAX_ERRORDETAILS (25)
#define MAX_DAYS_AGO 32
#define MAX_ERROR_TRIVIA_COUNT 3
#define ERRORTRACKER_TRIVIA_AGE_CUTOFF 14*24*60*60 // 14 days in seconds
#define ERRORTRACKER_DUMP_AGE_CUTOFF 14*24*60*60
// Blame information is old if it hasn't been updated in a day (and it reoccurs)
#define MAX_BLAME_AGE (1 * 24 * 60 * 60)

AUTO_ENUM;
typedef enum Platform
{
	PLATFORM_UNKNOWN = 0,

	PLATFORM_WIN32,
	PLATFORM_XBOX360,
	PLATFORM_PS3,
	PLATFORM_WIN64,
	PLATFORM_WINE,

	PLATFORM_COUNT
} Platform;

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct StackTraceLine
{
	CONST_STRING_MODIFIABLE pFunctionName;   AST(NAME(FunctionName),ESTRING)
	CONST_STRING_MODIFIABLE pModuleName;     AST(NAME(ModuleName),ESTRING)
	CONST_STRING_MODIFIABLE pFilename;       AST(NAME(FileName),ESTRING)
	const int   iLineNum;                    AST(NAME(LineNum))

	CONST_STRING_MODIFIABLE pBlamedPerson;   AST(NAME(BlamedPerson),ESTRING)
	const int   iBlamedRevision;             AST(NAME(BlamedRevision))
	const U32   uBlamedRevisionTime; AST(FORMATSTRING(HTML_SECS = 1))
} StackTraceLine;
AST_PREFIX()

AUTO_STRUCT;
typedef struct StackTraceLineList
{
	StackTraceLine **ppStackTraceLines;
} StackTraceLineList;
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct DayCount
{
	const int iDaysSinceFirstTime; AST(NAME(DaysSinceFirstTime))
	const int iCount;              AST(NAME(Count))
} DayCount;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlatformCount
{
	const Platform ePlatform; AST(NAME(Platform))
	const int iCount;         AST(NAME(Count))
} PlatformCount;

AUTO_STRUCT AST_CONTAINER;
typedef struct UserInfo
{
	CONST_STRING_MODIFIABLE pUserName; AST(NAME(UserName))
	const int iCount;                  AST(NAME(Count))
} UserInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct IPCount
{
	const U32 uIP;
	const int iCount; AST(NAME(Count))
} IPCount;

// One dump's worth of information.
AUTO_STRUCT AST_CONTAINER;
typedef struct DumpData
{
	const U32 uFlags; // Uses DUMPDATAFLAGS_*
	CONST_STRING_MODIFIABLE pDumpDescription;
	const int iDumpArrayIndex; // The earray index for the dump in the ErrorEntry - only used on receiving dumps
	const int iDumpIndex;                     AST(NAME(DumpIndex))
	const int iMiniDumpIndex; // used when FULLDUMP was requests, MINIDUMP was received
	const int iETIndex;       // The rawdata index of the crash, unknown if zero
	CONST_OPTIONAL_STRUCT(ErrorEntry) pEntry; AST(NAME(Entry) STRUCT_NORECURSE)
	const bool bWritten;
	const bool bMiniDumpWritten;
	const bool bCancelled;

	const U32 uIP;

	// Reprocessed Dump Info
	const U32 uMovedID; // Error Entry ContainerID dump was moved to
	const U32 uMovedIndex; // Error Entry Dump Index dump was moved to (+1 so that 0 = unset)

	const U32 uPreviousID; // Error Entry ContainerID dump was moved from
} DumpData;

AUTO_STRUCT AST_CONTAINER;
typedef struct MemoryDumpData
{
	const int iDumpIndex;
	const U32 uTimeReceived;
} MemoryDumpData;

AUTO_STRUCT AST_CONTAINER;
typedef struct XperfDumpData
{
	const U32 uTimeReceived;
	CONST_STRING_MODIFIABLE filename;
} XperfDumpData;

AUTO_STRUCT AST_CONTAINER;
typedef struct BranchTimeLog
{
	CONST_STRING_MODIFIABLE branch;
	const U32 uFirstTime; AST(FORMATSTRING(HTML_SECS = 1))
	const U32 uNewestTime; AST(FORMATSTRING(HTML_SECS = 1))
} BranchTimeLog;

AUTO_STRUCT AST_CONTAINER;
typedef struct ErrorCountData
{
	CONST_STRING_MODIFIABLE key; AST(KEY)
	const U32 uCount;
} ErrorCountData;
AST_PREFIX()

// Any new entries to ErrorEntry should potentially be mentioned in these functions:
//   - createErrorTrackerEntryFromErrorData()
//   - recalcUniqueID()
//   - mergeErrorTrackerEntries()

// Note: The above functions attempt to make mention (even if only in a comment) about 
//       every member of ErrorEntry. If you add any new members to the struct,
//       please update these functions (at least), even if they don't apply and are
//       just added as a comment. You'll see.

typedef struct JiraIssue JiraIssue;

//AUTO_ENUM;
//typedef enum CBErrorStatus
//{
//	CBERROR_NEW = 0,
//	CBERROR_OLD,
//	CBERROR_FIXED,
//	CBERROR_COUNT, EIGNORE
//} CBErrorStatus;

AUTO_STRUCT; // This is ONLY used for Continuous Builder
typedef struct ErrorTrackerEntryUserData
{
	char *pStepsWhereItHappened; AST(ESTRING)
	int iEmailNumberWhenItFirstHappened;
	U32 iTimeWhenItFirstHappened;
	char *pDumpFileName;
	char *pMemoryDumpFileName;

	//this is a crash or assert, but was in a NON_FATAL_ASSERT_EXES .exe, so is not fatal
	bool bNonFatal;

	//CBErrorStatus eStatus;
} ErrorTrackerEntryUserData;

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER;
typedef struct ErrorEntry
{
	// -------------------------------------------------------------------------------
	// Identification

	const U32 uID; AST(KEY)       // Ever-incrementing ID, used by web interface, crash dialog
	const U32 uMergeID;				// If this entry has been merged into another, this is the ID of the merged entry
	const U32 aiUniqueHash[ET_HASH_ARRAY_SIZE];    // MD5 hash, generated from portions of the entry
	const U32 aiUniqueHashNew[ET_HASH_ARRAY_SIZE]; // New MD5 hash with better parameters
	const U32 uHashVersion;
	// Non-unique string label for generic crashes (whose hash gets set to 0_0_0_0)
	CONST_STRING_MODIFIABLE pGenericLabel; AST(ESTRING)
	const ErrorDataType eType; AST(NAME(Type))	// Type of error data

	// -------------------------------------------------------------------------------
	// Crash Info
	CONST_EARRAY_OF(StackTraceLine) ppStackTraceLines; AST(NAME(StackTraceLines))
	CONST_STRING_MODIFIABLE pErrorSummary; AST(NAME(Summary))// used for in-game CBugs
	CONST_STRING_MODIFIABLE pErrorString; AST(NAME(ErrorString))
	CONST_STRING_MODIFIABLE pExpression; AST(NAME(Expression))
	CONST_STRING_MODIFIABLE pCategory; AST(NAME(Category))
	CONST_STRING_MODIFIABLE pSourceFile; AST(NAME(SourceFile))
	const int iSourceFileLine; AST(NAME(SourceFileLine))
	CONST_EARRAY_OF(UserInfo) ppUserInfo; AST(NAME(UserInfo))
	CONST_EARRAY_OF(IPCount) ppIPCounts; AST(NAME(IPCounts))
	CONST_STRING_EARRAY ppExecutableNames; AST(PERSIST, NAME(ExecutableNames))
	CONST_STRING_EARRAY ppAppGlobalTypeNames; AST(NAME(AppGlobalTypes))
	
	CONST_STRING_EARRAY ppProductNames; AST(NAME(ProductNames)) // deprecated; use eaProductOccurrences
	CONST_EARRAY_OF(ErrorCountData) eaProductOccurrences;

	const bool bProductionMode; AST(NAME(ProductionMode))       // true if any of the merged crashes were production
	CONST_STRING_MODIFIABLE pLargestMemory; AST(NAME(LargestMemory)) //Largest user of memory at the time of the crash

	const bool bMustFindProgrammer; AST(NAME(MustFindProgrammer))   // User-settable flag for crashes that can't be reproduced and/or debugged
	CONST_STRING_MODIFIABLE pProgrammerName; AST(NAME(ProgrammerName))
	const bool bReplaceCallstack; // Flag for ERRORDATATYPE_ERROR to replace the callstack with a newer one on the next occurrence of the error

	// This is not used for calculating the hash
	CONST_STRING_EARRAY eaErrorDetails; AST(NAME(ErrorDetails))
	// -------------------------------------------------------------------------------
	// Counts
	const int iTotalCount; AST(NAME(TotalCount))
	const int iTwoWeekCount; AST(NAME(TwoWeekCount))// updated daily
	CONST_EARRAY_OF(DayCount) ppDayCounts; AST(NAME(DayCounts))
	CONST_EARRAY_OF(PlatformCount) ppPlatformCounts; AST(NAME(PlatformCounts))
	CONST_INT_EARRAY ppExecutableCounts; AST(NAME(ExecutableCounts))

	// -------------------------------------------------------------------------------
	// Timing
	const U32 uFirstTime; AST(FORMATSTRING(HTML_SECS = 1))
	const U32 uNewestTime; AST(FORMATSTRING(HTML_SECS = 1))
	CONST_EARRAY_OF(BranchTimeLog) branchTimeLogs;

	// -------------------------------------------------------------------------------
	// Versioning
	CONST_STRING_EARRAY ppVersions; AST(NAME(Versions))
	CONST_STRING_EARRAY ppShardInfoStrings; AST(NAME(ShardInfoStrings))
	CONST_STRING_MODIFIABLE pShardStartString; AST(NAME(ShardStart)) // Only shows the first shard start time, for dump info
	CONST_STRING_EARRAY ppBranches; AST(NAME(Branches))
	CONST_STRING_EARRAY ppProductionBuildNames; AST(NAME(ProductionBuildNames))

	// -------------------------------------------------------------------------------
	// Data file info
	CONST_STRING_MODIFIABLE pDataFile; AST(NAME(DataFile))
	const U32   uDataFileTime;
	CONST_STRING_MODIFIABLE pLastBlamedPerson; AST(NAME(LastBlamedPerson))

	// -------------------------------------------------------------------------------
	// Client counts
	const int iMaxClients; AST(NAME(MaxClients))  // Max number of clients affected by a single instance of the error
	const int iTotalClients; AST(NAME(TotalClients)) // Total number of clients ever affected by this (allows dupes)
	// (iTotalClients / iTotalCount) = average client count per crash

	// -------------------------------------------------------------------------------
	// Trivia string (one per crash ... really only stored for the DumpData-owned entries)
	CONST_EARRAY_OF(TriviaData) ppTriviaData; AST(NAME(TriviaData), FORCE_CONTAINER)

	CONST_STRING_MODIFIABLE pTriviaFilename;
	const TriviaListMultiple triviaListMultiple; // deprecated
	const TriviaOverview triviaOverview;
	CONST_EARRAY_OF(CommentEntry) ppCommentList;
	const U32 uTriviaCount; // Only user for Errors
	const U32 uLastSavedTrivia;

	// -------------------------------------------------------------------------------
	// Dump Information
	CONST_EARRAY_OF(DumpData) ppDumpData; AST(NAME(DumpData))
	const int iMiniDumpCount; AST(NAME(MiniDumpCount))    // Just used for quick checks, use ppDumpData for output
	const int iFullDumpCount; AST(NAME(FullDumpCount))    // Just used for quick checks, use ppDumpData for output
	const bool bFullDumpRequested; AST(NAME(FullDumpRequested))
	const bool bBlockDumpRequests; AST(NAME(BlockDumpRequests))
	const bool bSuppressErrorInfo; AST(NAME(SuppressErrorInfo))
	const U32 uLastSavedDump;

	CONST_EARRAY_OF(ErrorEntry) ppRecentErrors; AST(NAME(RecentErrors) NO_INDEX) //List of recent error occurrences. 
	const int iETIndex; AST(NAME(ETIndex)) //index of ee file for individual error entries

	// Cached value
	U32 uOldestDump; NO_AST

	CONST_STRING_EARRAY ppDumpNotifyEmail; AST(ESTRING NAME(DumpNotifyEmail)) // People that want to know if this entry gets a new dump

	// Extra Dump Info (Memory Dumps, Xperf)
	CONST_EARRAY_OF(MemoryDumpData) ppMemoryDumps;
	const int iMemoryDumpCount;
	CONST_EARRAY_OF(XperfDumpData) ppXperfDumps;

	// -------------------------------------------------------------------------------
	// SVN Blame Information
	const int iCurrentBlameVersion; AST(NAME(CurrentBlameVersion))
		// SVN version when the stack trace blame info was queried
		// TODO this is currently unreliable
	const U32 uCurrentBlameTime;      // Time of that SVN blame query
	const int iRequestedBlameVersion; AST(NAME(RequestedBlameVersion)) 
		// If non-zero, requests that the SVN blame info is updated
		// to this SVN version. We must keep track of this as we 
		// don't want to perform a bunch of SVN queries right when
		// receiving a new bug (if applicable).


	// -------------------------------------------------------------------------------
	// Jira Information
	CONST_STRING_MODIFIABLE pJiraKey; AST(NAME(JiraKey))
	CONST_STRING_MODIFIABLE pJiraAssignee; AST(NAME(JiraAssignee))
	CONST_OPTIONAL_STRUCT(JiraIssue) pJiraIssue; AST(FORCE_CONTAINER)

	// -------------------------------------------------------------------------------
	// In-Game Character Information -- this stuff is all deprcated and not used anywhere
	const U32 uEntityDescriptorID;
	CONST_STRING_MODIFIABLE pEntityStr; AST(NAME(EntityStr))
	CONST_STRING_MODIFIABLE pScreenshotFilename; AST(ESTRING)
	const U32 uUserDataDescriptorID;
	// ****

	CONST_STRING_MODIFIABLE pUserDataStr; AST(ESTRING)
	const bool bUnlimitedUsers;

AST_PREFIX()
	ErrorTrackerEntryUserData *pUserData; AST(NAME(UserData)) // This is ONLY used for the CB

	// -------------------------------------------------------------------------------

	char *pStashString; AST(ESTRING) AST_NOT(PERSIST)
	bool bDelayResponse; NO_AST
	bool bBlameDataLocked; NO_AST
	int iDailyCount; NO_AST // resets nightly, or whenever ErrorTracker is restarted
	bool bIsNewEntry; NO_AST // resets nightly, or whenever ErrorTracker is restarted
	int iCurrentDumpReceiveCount; NO_AST

	// Deprecated
	const int iUniqueID;	AST(NAME(UniqueID))		// ** deprecated
	const int iOldestVersion;	AST(NAME(OldestVersion))		// ** deprecated
	const int iNewestVersion;	AST(NAME(NewestVersion))		// ** deprecated
	CONST_STRING_EARRAY ppUserNames; AST(NAME(UserNames)) // ** deprecated
	CONST_STRING_MODIFIABLE pTriviaString; AST(NAME(TriviaString))
	CONST_OPTIONAL_STRUCT(TriviaList) pTriviaList; AST(NAME(TriviaList)) 
	CONST_STRING_MODIFIABLE pEntityPTIStr; AST(NAME(EntityPTIStr))
	// End Deprecated
} ErrorEntry;

#define UNCONST_ENTRY(pConstEntry) (CONTAINER_NOCONST(ErrorEntry, pConstEntry))
#define CONST_ENTRY(pNoconstEntry) ((ErrorEntry*) pNoconstEntry)

#define CREATE_ERRORTRACKER_STRUCT() (StructCreateNoConst(parse_ErrorEntry)
#define CONTAINER_ENTRY(pContainer) ((ErrorEntry*) pContainer->containerData)

typedef enum SymSrvQueueStatus
{
	SYMSTATUS_Connecting = 0,
	SYMSTATUS_AwaitingResponse,
	SYMSTATUS_Done
} SymSrvQueueStatus;

typedef struct QueueNewEntryStruct
{
	int starttime;

	NetLink *link;
	NetLink *symsrvLink;
	IncomingClientState *pClientState;

	char *pCallstackText;
	char *pHashString;
	char *fNameCallstack;
	char *fNameStackTrace;
	int statuscount;

	NOCONST(ErrorEntry) *pEntry;
	ErrorData *pErrorData;
	SymSrvQueueStatus eStatus;
} QueueNewEntryStruct;

const char *ETGetShortProductName (const char *fullName);
const char *ETGetFullProductName (const char *shortName);
void ETReloadSVNProductNameMappings(void);