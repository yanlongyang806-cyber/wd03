#pragma once
GCC_SYSTEM

#include "trivia.h"

#define ASSERT_EXTERNAL_BUF_SIZE 2048
#define ASSERT_DESCRIPTION_MAX_LEN 2048

AUTO_ENUM;
typedef enum ErrorDataType
{
	ERRORDATATYPE_UNKNOWN = 0,
	ERRORDATATYPE_ERROR,
	ERRORDATATYPE_ASSERT,
	ERRORDATATYPE_CRASH,
	ERRORDATATYPE_COMPILE, // For the continuous builder
	ERRORDATATYPE_GAMEBUG, // Co-opted for Manual Dumps
	ERRORDATATYPE_FATALERROR,
	ERRORDATATYPE_XPERF,

	ERRORDATATYPE_COUNT
} ErrorDataType;

AUTO_ENUM; // For TO_ERRORTRACKER_GENERICFILE_[START/CONTINUE/END]
typedef enum ErrorFileType
{
	ERRORFILETYPE_None = 0,
	ERRORFILETYPE_Xperf,
	ERRORFILETYPE_Max
} ErrorFileType;

extern StaticDefineInt ErrorDataTypeEnum[];

#define ERRORDATATYPE_IS_A_CRASH(eType) ((eType == ERRORDATATYPE_ASSERT) || (eType == ERRORDATATYPE_CRASH) || (eType == ERRORDATATYPE_FATALERROR) || (eType == ERRORDATATYPE_GAMEBUG))
#define ERRORDATATYPE_IS_A_ERROR(eType) ((eType == ERRORDATATYPE_ERROR) || (eType == ERRORDATATYPE_XPERF))

// -------------------------------------------------------------------
// Contents of an ERRORDATA type error packet. 

AUTO_STRUCT;
typedef struct ErrorData
{
	// Core error type
	ErrorDataType eType;

	// About the executable itself
	char *pPlatformName;
	char *pExecutableName;
	char *pProductName;
	char *pVersionString;

	// Basic error info
	//char *pErrorSummary;
	char *pErrorString;
	char *pUserWhoGotIt;
	char *pSourceFile;
	int iSourceFileLine;

	// Data file involved (if applicable)
	char *pDataFile;
	char *pAuthor;
	U32 iDataFileModificationTime; AST(FORMAT_DATESS2000)

	// Lesser information (more for bugfix prioritizing than for error info)
	int iClientCount;
	char *pTrivia;
	TriviaList *pTriviaList;

	// --- Added 06/06/07 ---
	int iProductionMode;

	// --- Added 06/07/07 ---
	char *pStackData;
	char *pExpression; // Assert expression as string

	// --- Added 10/17/07 ---
	char *pEntityPTIStr;
	char *pEntityStr;

	// --- Added 10/30/07 ---
	char *pErrorSummary;

	// --- Added 12/03/07 ---
	char *pSVNBranch;
	char *pProductionBuildName;

	// --- Added 12/17/07 - this feature has been removed
	U32 uImageSize;
	char *imageBuffer; NO_AST

	// -- Added 12/21/07
	char *pUserDataStr;
	char *pUserDataPTIStr;

	// -- Added 09/09/09
	char *pShardInfoString;

	// -- Added 1/11/11
	char *pAppGlobalType;

	// -- Added 6/25/12
	char *lastMemItem;

	// -- Added 9/27/12
	U32 uCEPId;

	// Used in ErrorTrackerLib for IP tracking
	U32 uIP; NO_AST

} ErrorData;

// -------------------------------------------------------------------

typedef struct Packet Packet;

ErrorData * getErrorDataFromPacket(Packet *pkt);
void putErrorDataIntoPacket(Packet *pkt, ErrorData *pErrorData);

typedef void (*errorThrottleResetCallback)(size_t uTotalBytesToSend);
typedef void (*errorThrottleProgressCallback)(size_t uTotalSentBytes);
void errorTrackerSetThrottleCallbacks(errorThrottleResetCallback resetCB, errorThrottleProgressCallback progressCB);

bool errorTrackerSendError(ErrorData *pErrorData);
bool errorTrackerSendErrorPlusScreenshot(ErrorData *pErrorData, void *data);
U32  errorTrackerGetDumpFlags(void);
void errorTrackerEnableFullDumpFlag(void);
void errorTrackerDisableFullDumpFlag(void);
U32  errorTrackerGetUniqueID(void);
U32  errorTrackerGetErrorResponse(void);
char *errorTrackerGetErrorMessage(void);

typedef enum DumpResult
{
	DUMPRESULT_SUCCESS = 0,
	DUMPRESULT_FAILURE_RECURSIVE, // Calling send dump from inside of send dump
	DUMPRESULT_FAILURE_CONNECT_TIMEOUT,
	DUMPRESULT_FAILURE_NOTHING_TO_SEND,
	DUMPRESULT_FAILURE_DEFLATE_INIT,
	DUMPRESULT_FAILURE_NO_DUMP_FILE,
	DUMPRESULT_FAILURE_CANT_OPEN_FILE,
	DUMPRESULT_FAILURE_SEND_CANCELLED,
	DUMPRESULT_FAILURE_LINK_DROPPED,
	DUMPRESULT_FAILURE_DEFLATE_ERROR_WHILE_SENDING,
	DUMPRESULT_FAILURE_NO_RECEIPT,
	DUMPRESULT_FAILURE_SEND_EXPLICIT_CANCELLED,

	DUMPRESULT_COUNT
} DumpResult;

const char * DumpResultToString(DumpResult dumpResult);

DumpResult errorTrackerSendDump(const char *pCrashInfo, const char *pDescription, int dump_type);
void errorTrackerSendDumpDescriptionOnly(const char *pDescription);
void errorTrackerSendDumpDescriptionUpdate(const char *pDescription);
void errorTrackerSendMemoryDump (U32 errorID, void *pData, U32 size);
void errorTrackerSendGenericFile(U32 uErrorID, ErrorFileType eFileType, const char *filepath, void *pExtraData, ParseTable *pti);

// -------------------------------------------------------------------
// Deferred crash dump sending for the 360. 

// Called during a crash, writes out relevant crash info to disk
void errorTrackerWriteDumpCache(ErrorData *pErrorData);

// Called on startup, detects crash info on disk and sends it
void errorTrackerCheckDumpCache(void);

void errorTrackerEnableErrorThreading(bool enabled); // default: on; tweaks the same thing as -enableErrorThreading

// -------------------------------------------------------------------

#define DUMP_READ_BUFFER_SIZE (16 * 1024)

// -------------------------------------------------------------------

AUTO_STRUCT;
typedef struct ErrorDataCache
{
	ErrorData *pErrorData;
	int iUniqueID;
	int iDumpFlags;
	char *assertbuf;
	char *description;
} ErrorDataCache;

// -------------------------------------------------------------------

#define DUMPFLAGS_MINIDUMP   0x01
#define DUMPFLAGS_FULLDUMP   0x02
#define DUMPFLAGS_UNIQUEID   0x04 // Packet contains unique crash ID
#define DUMPFLAGS_COMPRESSED 0x08 // Dump being sent is pre-compressed
#define DUMPFLAGS_DEFERRED   0x10 // Crash was already reported; attaching new dump data to it
#define DUMPFLAGS_EXTERNAL   0x20 // Crashes from external IPs - default mini, can send full if wanted, different dialog
#define DUMPFLAGS_CLIENTDUMP 0x40  // Dump is from a game client
#define DUMPFLAGS_DUMPINDEX  0x80  // Actual Dump Index for deferred description updates
#define DUMPFLAGS_AUTOCLOSE  0x100 // Tell CrypticError to just kill the app and close when complete

#define ERRORRESPONSE_NULLCALLSTACK 0x01
#define ERRORRESPONSE_OLDVERSION    0x02
#define ERRORRESPONSE_PROGRAMMERREQUEST 0x03
#define ERRORRESPONSE_ERRORCREATING 0x04

#define DUMPFLAGS_DUMPWANTED(dumpflags) (dumpflags & (DUMPFLAGS_FULLDUMP | DUMPFLAGS_MINIDUMP))

// -------------------------------------------------------------------

AUTO_STRUCT;
typedef struct ErrorXperfData
{
	char *pFilename; // File name to be saved for xperf
} ErrorXperfData;


//you can set up a "forking" function, which takes any error that's going to be sent to ErrorTracker and
//forks it to your callback. This is very different from all the normal error redirection and so forth because
//it happens at the very last minute after all the normal "should I or should I not report this" code, 
//and only if reporting to errortracker (including local ET in CB.exe) is going to happen, not if a 
//dialog is going to pop up, or it's going to be sent to the controller, etc.
typedef void (*ErrorSendingForkFunction)(ErrorData *pErrorData);

void SetErrorSendingForkFunction(ErrorSendingForkFunction pFunc);