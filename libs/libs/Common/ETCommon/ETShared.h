#pragma once
#include "ETCommon/ETCommonStructs.h"

typedef struct NOCONST(ErrorEntry) NOCONST(ErrorEntry);
typedef struct NOCONST(StackTraceLine) NOCONST(StackTraceLine);
typedef struct NOCONST(DumpData) NOCONST(DumpData);
typedef struct NetLink NetLink;
typedef struct IncomingClientState IncomingClientState;
typedef enum GlobalType GlobalType;
typedef struct NetComm NetComm;

#define ET_LATEST_HASH_VERSION 1

void ErrorTrackerStashTableInit();

void addEntryToStashTables(ErrorEntry *pEntry);
void removeEntryFromStashTables(NOCONST(ErrorEntry) *pEntry);

const char *errorTrackerGetSourceDataDir(void);
const char *errorTrackerGetDatabaseDir(void);
const char *errorTrackerGetPDBDir(void);
const char * getMachineAddress(void);
void ETShared_SetFQDN(const char *fqdn);

NetComm *errorTrackerCommDefault();
void errorTrackerLibSendWrappedString(NetLink *link, const char *pString);

#define ET_ZERO_HASH_STRING "0_0_0_0"
char *errorTrackerLibStringFromUniqueHash(ErrorEntry *pEntry);
char *errorTrackerLibShortStringFromUniqueHash(ErrorEntry *pEntry);

ErrorTrackerContext * errorTrackerLibCreateContext(void);
ErrorTrackerContext * errorTrackerLibGetCurrentContext(void);
void errorTrackerLibSetCurrentContext(ErrorTrackerContext *pContext);
GlobalType errorTrackerLibGetCurrentType(void);
void errorTrackerLibSetType(ErrorTrackerContext *pContext, GlobalType eType);

//ErrorEntry * findErrorTrackerByID(U32 uID);
ErrorEntry * findErrorTrackerByIDEx(U32 uID, char *file, int line);
#define findErrorTrackerByID(uID) findErrorTrackerByIDEx(uID, __FILE__, __LINE__)
ErrorEntry * findErrorTrackerEntryFromNewEntry(NOCONST(ErrorEntry) *pNewEntry);
bool hasHash(const U32 *hash);
bool hashMatchesU32(const U32 *h1, const U32 *h2);
bool hashMatches(ErrorEntry *p1, ErrorEntry *p2);

const char * ErrorDataTypeToString(ErrorDataType eType);
const char *getPlatformName(Platform ePlatform);
Platform getPlatformFromName(const char *pPlatformName);
const char * getPrintableModuleName(ErrorEntry *p, const char *pModuleName);
const char * findNewestVersion(CONST_STRING_EARRAY * const pppVersions);
bool hasValidBlameInfo(ErrorEntry *pEntry);
int findDayCount(CONST_EARRAY_OF(DayCount) ppDays, int iDaysSinceFirstTime);
int ETShared_ParseSVNRevision (const char *pVersionString);

void errorTrackerSendStatusUpdate(NetLink *link, int update_code);
void SendDumpFlags(NetLink *link, U32 uFlags, U32 uDumpID, U32 uETID, U32 uDumpIndex);
void SendDumpFlagsWithContext(NetLink *link, U32 uFlags, U32 uDumpID, U32 uETID, U32 uDumpIndex, U32 context);
void sendFailureResponse(NetLink *link);

bool RunningFromToolsBin(ErrorEntry *p);
int calcElapsedDays(U32 uStartTime, U32 uEndTime);
void calcReadDumpPath(char *pFilename, int iMaxLength, U32 uID, int iDumpIndex, bool bFullDump);
void calcWriteDumpPath(char *pFilename, int iMaxLength, U32 uID, int iDumpIndex, bool bFullDump);
int calcRequestedDumpFlags(ErrorEntry *pMergedEntry, ErrorEntry *pEntry);
void calcMemoryDumpPath(char *pFilename, int iMaxLength, U32 uID, int iDumpIndex);
void calcTriviaDataPath(char *pFileName, int iMaxLength, U32 uID);
int parseErrorEntryDir(char *dirPath);
void GetErrorEntryDir(const char* srcdir, U32 uID, char *dirname, size_t dirname_size);
void GetErrorEntryDirDashes(const char* srcdir, U32 uID, char *dirname, size_t dirname_size);

bool findUniqueString(CONST_STRING_EARRAY ppStringList, const char *pStr);
#define addUniqueString(pppStringList, pStr) if (!findUniqueString(*pppStringList, pStr)) \
	eaPush(pppStringList, strdup(pStr));

const char * errorExecutableGetName(const char *pExeFullPath);
char *ET_GetExecutableName(char *exeNameOrPath);
void recalcUniqueID(NOCONST(ErrorEntry) *pEntry, U32 uVersion);
void ET_ConstructHashString (SA_PARAM_NN_VALID char **estrHash, SA_PARAM_NN_VALID NOCONST(ErrorEntry) *pEntry, bool newHash);
bool parseErrorType(const char *pStr, NOCONST(ErrorEntry) *pEntry);
void addCountForDay(ATH_ARG NOCONST(ErrorEntry) *pEntry, U32 uTime, int iCount);
void addCountForPlatform(ATH_ARG NOCONST(ErrorEntry) *pEntry, Platform ePlatform, int iCount);
void addCountForUser(ATH_ARG NOCONST(ErrorEntry) *pEntry, const char *pUserName, int iCount);
void addCountForIP(ATH_ARG NOCONST(ErrorEntry) *pEntry, U32 uIP, int iCount);
void CopyStackTraceLine(NOCONST(StackTraceLine) **dst, StackTraceLine *src);
void parseStackTraceLines(NOCONST(ErrorEntry) *pEntry, char *pStr);

NOCONST(ErrorEntry) * createErrorEntryFromErrorData(ErrorData *pErrorData, int iMergedId, bool *pbExternalSymsrv);

void etAddProductCount(ATH_ARG NOCONST(ErrorEntry) *pEntry, const char *productName, U32 uCount);
void mergeErrorEntry_Part1(ATH_ARG NOCONST(ErrorEntry) *pDst, NON_CONTAINER ErrorEntry *pNew, U32 uTime);
void mergeErrorEntry_Part2(ATH_ARG NOCONST(ErrorEntry) *pDst, NON_CONTAINER ErrorEntry *pNew, U32 uTime);

void MoveDumpFiles(SA_PARAM_NN_VALID ErrorEntry *pOldEntry, SA_PARAM_NN_VALID DumpData *oldDump, SA_PARAM_NN_VALID ErrorEntry *pNewEntry, SA_PARAM_NN_VALID NOCONST(DumpData) *newDump);

void etLoadClientExeList(void);
bool etExeIsClient(ErrorEntry *pEntry);

AUTO_STRUCT;
typedef struct ETClientExeList
{
	STRING_EARRAY eaExeNames;
} ETClientExeList;

const char *getVersionPatchProject(SA_PARAM_NN_STR const char *version);
const char *getSVNBranchProject(SA_PARAM_NN_STR const char *branch);
char *getSimplifiedVersionString(SA_PARAM_NN_VALID char **estr, const char *version);
