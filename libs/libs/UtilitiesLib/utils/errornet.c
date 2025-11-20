#include "errornet.h"
#include "estring.h"
#include "net/net.h"
#include "net/netlink.h"
#include "timing.h"
#include "sysutil.h"
#include "file.h"
#include "XboxThreads.h"
#include "WorkerThread.h"
#include "errorprogressdlg.h"
#include "../serverlib/pub/serverlib.h"
#include "../../3rdparty/zlib/zlib.h"
#include "errornet_h_ast.h"
#include "ContinuousBuilderSupport.h"
#include "StashTable.h"
#include "windefinclude.h"

AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:errorThread", BUDGET_EngineMisc););

#define ERRORTRACKER_CONNECT_TIMEOUT 5.f

// ------------------------------------------------------------------------------------------------
// Error Threading Declarations

#define ERROR_THREAD_MAX_QUEUED_ERRORS (256)

int sEnableErrorThreading = 1;
AUTO_CMD_INT(sEnableErrorThreading, enableErrorThreading) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

void errorTrackerEnableErrorThreading(bool enabled)
{
	sEnableErrorThreading = enabled;
}

AUTO_STRUCT;
typedef struct ErrorThreadStruct
{
	bool bDumpFlagsReceived;
	bool bErrorFlagsReceived;
} ErrorThreadStruct;

#define ERROR_SEND_THREADED (sEnableErrorThreading && !g_isContinuousBuilder)
static StashTable stErrorQueueResults = NULL;
static WorkerThread *spErrorThread = NULL;
enum
{
	ERRORCMD_SEND = WT_CMD_USER_START,
};
static void lazyInitErrorThread(void);

// ------------------------------------------------------------------------------------------------

#if !_PS3
#pragma warning ( disable : 4996 )
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static DumpResult errorTrackerSendDumpInternal(const char *pCrashInfo, const char *pCrashDescription, const char *pDeferredString, int dump_type);
static bool errorTrackerSendErrorRaw(const char *pRawErrorDataString);

#define DUMP_COMPRESS_LEVEL 1

// zlib apparently uses the value of windowBits for more things than knowing
// the size of window bits. If it is <0, it means "suppress the zlib header",
// and if it is >16, it means "I'd like to use gzip". [shrug]
#define MAX_WBITS_AND_USE_GZIP_PLEASE (MAX_WBITS + 16)

static unsigned char readBuffer[DUMP_READ_BUFFER_SIZE];
static unsigned char  zipBuffer[DUMP_READ_BUFFER_SIZE];

#include "autogen/errornet_h_ast.h"
#include "autogen/errornet_c_ast.h"

CRITICAL_SECTION gErrorAccess;
CRITICAL_SECTION gErrorThreadAccess;
CRITICAL_SECTION gErrorThreadStashAccess;
CRITICAL_SECTION gTriviaAccess;

// Called from utilitiesLibPreAutoRunStuff
void initErrorAccessCriticalSection(void)
{
	InitializeCriticalSection(&gErrorAccess);
	InitializeCriticalSection(&gErrorThreadAccess);
	InitializeCriticalSection(&gTriviaAccess);
	InitializeCriticalSection(&gErrorThreadStashAccess);
}


// eww...
static char szOutputParseText[8192];

static errorThrottleResetCallback sThrottleResetCallback = NULL;
static errorThrottleProgressCallback sThrottleProgressCallback = NULL;

void errorTrackerSetThrottleCallbacks(errorThrottleResetCallback resetCB, errorThrottleProgressCallback progressCB)
{
	sThrottleResetCallback = resetCB;
	sThrottleProgressCallback = progressCB;
}

ErrorData * getErrorDataFromPacket(Packet *pkt)
{
	ErrorData *pErrorData = StructCreate(parse_ErrorData);
	char *pParseString = pktGetStringTemp(pkt);

	if(!ParserReadText(pParseString, parse_ErrorData, pErrorData, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE))
	{
		StructDestroy(parse_ErrorData, pErrorData);
		return NULL;
	}
	
	// Image feature for ET has been removed
	/*if (pErrorData->uImageSize > 0 && pktCheckRemaining(pkt, 1))
	{
		pErrorData->imageBuffer = malloc(pErrorData->uImageSize);
		pktGetBytes(pkt, pErrorData->uImageSize, pErrorData->imageBuffer);
	}
	else*/
	{
		pErrorData->uImageSize = 0;
		pErrorData->imageBuffer = NULL;
	}

	return pErrorData;
}

void putErrorDataIntoPacket(Packet *pkt, ErrorData *pErrorData)
{
	char *pOutputParseText = NULL;

	estrAllocaCreate(&pOutputParseText, 2048);
	ParserWriteText(&pOutputParseText, parse_ErrorData, pErrorData, 0, 0, 0);
	pktSendString(pkt, pOutputParseText);
	estrDestroy(&pOutputParseText);
}

// Wait 45000ms for the server to say what kind of dump we should be sending
#define WAIT_FOR_DUMPFLAGS_TIMEOUT 5000
#define WAIT_FOR_RECEIPT_TIMEOUT 30000

static bool sbReceiptReceived = false;

static int iErrorTrackerDumpFlags = 0;
static int iErrorTrackerDumpID = 0;
static int iErrorTrackerDumpIndex = -1;
static int iUniqueID = 0;
static bool sbDumpFlagsReceived = false;
static int iErrorTrackerErrorResponse = 0;
// Stack trace lines might be put from Error Tracker to here
static char pErrorTrackerErrorMsg[10240] = "";
static bool sbErrorFlagsReceived = false;

static DWORD siLastUpdateTime = 0;
static int siErrorTrackerStatus = 0;

static NetComm *netComm = NULL;

static void ReceiveMsg(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch(cmd)
	{
	case FROM_ERRORTRACKER_DUMPFLAGS:
		{
			iErrorTrackerDumpFlags = pktGetU32(pkt);
			iErrorTrackerDumpID    = pktGetU32(pkt);
			if(iErrorTrackerDumpFlags & DUMPFLAGS_UNIQUEID)
			{
				iUniqueID = pktGetU32(pkt);
			}
			if(iErrorTrackerDumpFlags & DUMPFLAGS_DUMPINDEX)
			{
				iErrorTrackerDumpIndex = pktGetU32(pkt);
			}
			/*if (getAssertMode() & ASSERTMODE_ISEXTERNALAPP && iErrorTrackerDumpFlags & DUMPFLAGS_FULLDUMP &&
				iErrorTrackerDumpFlags & DUMPFLAGS_EXTERNAL)
			{
				iErrorTrackerDumpFlags &= (~DUMPFLAGS_FULLDUMP);
				iErrorTrackerDumpFlags |= DUMPFLAGS_MINIDUMP;
			}*/
			EnterCriticalSection(&gErrorThreadStashAccess);
			if (ERROR_SEND_THREADED && stErrorQueueResults)
			{
				ErrorThreadStruct *pThread;
				stashIntFindPointer(stErrorQueueResults, linkID(link), &pThread);
				if (pThread)
					pThread->bDumpFlagsReceived = true;
			}
			LeaveCriticalSection(&gErrorThreadStashAccess);
			sbDumpFlagsReceived = true; // always set this; it's just ignored for threaded stuff
		}
		break;

	case FROM_ERRORTRACKER_CANCEL_DUMP:
		{
			errorProgressDlgCancel();
		}
		break;
	case FROM_ERRORTRACKER_ERRRESPONSE:
		{
			iErrorTrackerErrorResponse = pktGetU32(pkt);
			pktGetString(pkt, pErrorTrackerErrorMsg, ARRAY_SIZE_CHECKED(pErrorTrackerErrorMsg));
			EnterCriticalSection(&gErrorThreadStashAccess);
			if (ERROR_SEND_THREADED && stErrorQueueResults)
			{
				ErrorThreadStruct *pThread;
				stashIntFindPointer(stErrorQueueResults, linkID(link), &pThread);
				if (pThread)
					pThread->bErrorFlagsReceived = true;
			}
			LeaveCriticalSection(&gErrorThreadStashAccess);
			sbErrorFlagsReceived = true; // always set this; it's just ignored for threaded stuff
		}
		break;
	case FROM_ERRORTRACKER_STATUS_UPDATE:
		{
			int iUpdate = pktGetU32(pkt);

			// Stackwalking is only state that is allowed to take more than one update timeout cycle
			if (iUpdate > siErrorTrackerStatus || (iUpdate == siErrorTrackerStatus && iUpdate == STATE_ERRORTRACKER_STACKWALK))
			{
				siErrorTrackerStatus = iUpdate;
				siLastUpdateTime = GetTickCount();
			}
		}
		break;
	case FROM_ERRORTRACKER_RECEIVED:
		{
			sbReceiptReceived = true;
		}

	};
}

U32 errorTrackerGetDumpFlags()
{
	return iErrorTrackerDumpFlags;
}
void errorTrackerEnableFullDumpFlag(void)
{
	if (iErrorTrackerDumpFlags & DUMPFLAGS_EXTERNAL)
	{
		iErrorTrackerDumpFlags &= (~DUMPFLAGS_MINIDUMP);
		iErrorTrackerDumpFlags |= DUMPFLAGS_FULLDUMP;
	}
}
void errorTrackerDisableFullDumpFlag(void)
{
	if (iErrorTrackerDumpFlags & DUMPFLAGS_EXTERNAL)
	{
		iErrorTrackerDumpFlags &= (~DUMPFLAGS_FULLDUMP);
		iErrorTrackerDumpFlags |= DUMPFLAGS_MINIDUMP;
	}
}

U32 errorTrackerGetUniqueID()
{
	return iUniqueID;
}

U32 errorTrackerGetErrorResponse(void)
{
	return iErrorTrackerErrorResponse;
}

char *errorTrackerGetErrorMessage(void)
{
	return pErrorTrackerErrorMsg;
}

static char lastDataFile[MAX_PATH] = {0};
static int  lastDataFileCount = 0;

static int packetNum = 0;

// After this many errors from the same data file, just stop sending them. We get the point.
#define SINGLE_DATAFILE_ERROR_THRESHOLD (3)

static void errorNetInitializePerErrorStaticValues(void)
{
	iErrorTrackerDumpFlags = 0;
	iErrorTrackerDumpID    = 0;
	iErrorTrackerDumpIndex = -1;
	iUniqueID              = 0;
	sbDumpFlagsReceived = false;
	iErrorTrackerErrorResponse = 0;
	pErrorTrackerErrorMsg[0] = 0;
	sbErrorFlagsReceived = false;

	siErrorTrackerStatus = STATE_ERRORTRACKER_NONE;
	siLastUpdateTime = 0;
}

ErrorSendingForkFunction spErrorSendingForkFunc = NULL;

void SetErrorSendingForkFunction(ErrorSendingForkFunction pFunc)
{
	spErrorSendingForkFunc = pFunc;
}

bool errorTrackerSendError(ErrorData *pErrorData)
{
	int iCmdLength = 0;
	char *pTempCmd = NULL;
	char *pCurrCmd = NULL;
	char *pTokenContext = NULL; // Yay, strtok_s()
	char *pOutputParseText = NULL;
	bool ret = true;

	static int bInSendError = 0;
	
	#if MAKE_ERRORS_LESS_ANNOYING
		return false;
	#endif

	EnterCriticalSection(&gErrorAccess);

	if(bInSendError)
	{
		if (bInSendError == 1)
		{
			bInSendError++;
			printf("ERROR: Recursing into errorTrackerSendError!\n");
		} 
		else bInSendError++;
		LeaveCriticalSection(&gErrorAccess);
		return false;
	}
	bInSendError = 1;

	// ---------------------------------------------------------------------------
	// Minimize our number of errors on a particularly broken file
	if(pErrorData->pDataFile)
	{
		if(!stricmp(lastDataFile, pErrorData->pDataFile))
		{
			lastDataFileCount++;

			if(lastDataFileCount > SINGLE_DATAFILE_ERROR_THRESHOLD)
			{
				// Don't bother to send another, they get the point.
				bInSendError = 0;
				LeaveCriticalSection(&gErrorAccess);
				return true;
			}
		}
		else
		{
			strcpy(lastDataFile, pErrorData->pDataFile);
			lastDataFileCount = 1;
		}
	}
	else
	{
		lastDataFile[0] = 0;
		lastDataFileCount = 0;
	}
	// ---------------------------------------------------------------------------

	estrAllocaCreate(&pOutputParseText, 2048);
	ParserWriteText(&pOutputParseText, parse_ErrorData, pErrorData, 0, 0, 0);

#if PLATFORM_CONSOLE
	if (ERRORDATATYPE_IS_A_CRASH(pErrorData->eType))
		sEnableErrorThreading = false; // Force this to false so that XBox and PS3 send crash info through main thread
#endif

	if (ERROR_SEND_THREADED)
	{
		char *pParseCopy = strdup(pOutputParseText);
		lazyInitErrorThread();
		if (!wtQueueCmd(spErrorThread, ERRORCMD_SEND, &pParseCopy, sizeof(&pParseCopy)))
		{
			free(pParseCopy);
		}
	}
	else
	{
		ret = errorTrackerSendErrorRaw(pOutputParseText);
	}


	estrDestroy(&pOutputParseText);
	bInSendError = 0;

	if (spErrorSendingForkFunc)
	{
		spErrorSendingForkFunc(pErrorData);
	}
	LeaveCriticalSection(&gErrorAccess);

	return ret;
}

static void errorTrackerDisconnect(NetLink *link, void *data)
{
	if (ERROR_SEND_THREADED)
	{
		ErrorThreadStruct *pThreadStruct;
		EnterCriticalSection(&gErrorThreadStashAccess);
		if (stashIntRemovePointer(stErrorQueueResults, linkID(link), &pThreadStruct))
		{
			StructDestroy(parse_ErrorThreadStruct, pThreadStruct);
		}
		LeaveCriticalSection(&gErrorThreadStashAccess);
	}
}

// Calls to this function should initialize their own critical sections as necessary
static bool errorTrackerSendErrorRaw(const char *pRawErrorDataString)
{
	NetLink *pErrorTrackerLink;
	U32 iTime;
	char *pExecutableName;
	Packet *pPak;
	bool bReceivedResponse = false;
	U32 uLinkID;
	U32 pNum = InterlockedIncrement(&packetNum);

	errorNetInitializePerErrorStaticValues();

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	commSetSendTimeout(netComm, 10.0f);

	iTime = timeSecondsSince2000();
	pExecutableName = getExecutableName();

	pErrorTrackerLink = commConnect(netComm, LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,getErrorTracker(),
		DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,errorTrackerDisconnect,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		return false;
	}

	uLinkID = linkID(pErrorTrackerLink);
	if (ERROR_SEND_THREADED)
	{
		ErrorThreadStruct *pThreadStruct = StructCreate(parse_ErrorThreadStruct);
		EnterCriticalSection(&gErrorThreadStashAccess);
		assert(stashIntAddPointer(stErrorQueueResults, uLinkID, pThreadStruct, false));
		LeaveCriticalSection(&gErrorThreadStashAccess);
	}

	// ---------------------------------------------------------------------------------
	// Send Error Data!

	pktCreateWithCachedTracker(pPak, pErrorTrackerLink, TO_ERRORTRACKER_ERROR);
	pktSendString(pPak, pRawErrorDataString);
	pktSendU32(pPak, pNum);
	pktSend(&pPak);
	commMonitor(netComm);

	// ---------------------------------------------------------------------------------
	// Wait for a few seconds to see if the server wants a dump from us, and to get error flags

	siLastUpdateTime = GetTickCount();
	
	while(!bReceivedResponse)
	{
		DWORD tick = GetTickCount();
		if(tick > (siLastUpdateTime + WAIT_FOR_DUMPFLAGS_TIMEOUT))
		{
			break;
		}

		Sleep(1);
		commMonitor(netComm);
		if (ERROR_SEND_THREADED)
		{
			ErrorThreadStruct *pThreadStruct = NULL;
			EnterCriticalSection(&gErrorThreadStashAccess);
			stashIntFindPointer(stErrorQueueResults, uLinkID, &pThreadStruct);
			LeaveCriticalSection(&gErrorThreadStashAccess);
			// Just exit out of this wait if pThreadStruct is NULL
			bReceivedResponse = pThreadStruct ? (pThreadStruct->bDumpFlagsReceived && pThreadStruct->bErrorFlagsReceived) : true;
		}
		else
			bReceivedResponse = (sbDumpFlagsReceived || sbErrorFlagsReceived);
	}

	if (ERROR_SEND_THREADED)
	{
		ErrorThreadStruct *pThreadStruct = NULL;
		EnterCriticalSection(&gErrorThreadStashAccess);
		stashIntRemovePointer(stErrorQueueResults, uLinkID, &pThreadStruct);
		LeaveCriticalSection(&gErrorThreadStashAccess);
		if (pThreadStruct)
			StructDestroy(parse_ErrorThreadStruct, pThreadStruct);
	}

	commMonitor(netComm);
	linkShutdown(&pErrorTrackerLink);
	sbDumpFlagsReceived = sbErrorFlagsReceived = false; // always unset this; these are ignored for threaded
	return true;
}

bool errorTrackerSendErrorPlusScreenshot(ErrorData *pErrorData, void *data)
{
	U32 iTime;
	char *pExecutableName;
	NetLink *pErrorTrackerLink;
	int iCmdLength = 0;
	char *pTempCmd = NULL;
	char *pCurrCmd = NULL;
	char *pTokenContext = NULL; // Yay, strtok_s()
	Packet *pPak;
	static int bInSendError = 0;

	if (sEnableErrorThreading) // Unsupported for threaded errors
	{
		printf("ERROR: Errors with screenshots not supported with threading!\n");
		return false;
	}

	assert (pErrorData->uImageSize > 0);
	EnterCriticalSection(&gErrorAccess);
	if(bInSendError)
	{
		if (bInSendError == 1)
		{
			bInSendError++;
			printf("ERROR: Recursing into errorTrackerSendError!\n");
		} 
		else bInSendError++;
		LeaveCriticalSection(&gErrorAccess);
		return false;
	}
	bInSendError = 1;
	
	errorNetInitializePerErrorStaticValues();

	// ---------------------------------------------------------------------------
	// Minimize our number of errors on a particularly broken file
	if(pErrorData->pDataFile)
	{
		if(!strcmp(lastDataFile, pErrorData->pDataFile))
		{
			lastDataFileCount++;
			if(lastDataFileCount > SINGLE_DATAFILE_ERROR_THRESHOLD)
			{
				// Don't bother to send another, they get the point.
				bInSendError = 0;
				LeaveCriticalSection(&gErrorAccess);
				return true;
			}
		}
		else
		{
			strcpy(lastDataFile, pErrorData->pDataFile);
			lastDataFileCount = 1;
		}
	}
	else
	{
		lastDataFile[0] = 0;
		lastDataFileCount = 0;
	}
	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	commSetSendTimeout(netComm, 10.0f);

	iTime = timeSecondsSince2000();
	pExecutableName = getExecutableName();

	pErrorTrackerLink = commConnect(netComm,LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,getErrorTracker(),
		DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		bInSendError = 0;
		LeaveCriticalSection(&gErrorAccess);
		return false;
	}

	pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_ERROR);
	putErrorDataIntoPacket(pPak, pErrorData);
	pktSendBytes(pPak, pErrorData->uImageSize, data);
	pktSend(&pPak);
	commMonitor(netComm);

	siLastUpdateTime = GetTickCount();
	while(!sbDumpFlagsReceived || !sbErrorFlagsReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (siLastUpdateTime + WAIT_FOR_DUMPFLAGS_TIMEOUT))
			break;
		Sleep(1);
		commMonitor(netComm);
	}

	commMonitor(netComm);

	linkShutdown(&pErrorTrackerLink);
	bInSendError = 0;
	sbDumpFlagsReceived = sbErrorFlagsReceived = false;
	LeaveCriticalSection(&gErrorAccess);
	return true;
}

static void* zalloc(void* opaque, U32 items, U32 size)
{
	return malloc(items * size);
}

static void zfree(void* opaque, void* address)
{
	SAFE_FREE(address);
}

static void sendBytes(NetLink *pErrorTrackerLink, int iCmd, char *buffer, U32 len, U32 totalLen)
{
	Packet *pPak = pktCreate(pErrorTrackerLink, iCmd);
	pktSendU32(pPak, len);
	pktSendU32(pPak, totalLen);
	pktSendBytes(pPak, len, buffer);
	pktSend(&pPak);
}

static DumpResult errorTrackerInitFileSend(SA_PARAM_NN_VALID FILE *file, z_stream *z, size_t *piTotalBytes)
{
	int err;
	if (z)
	{
		z->zalloc = zalloc;
		z->zfree  = zfree;

		z->next_in  = readBuffer;
		z->avail_in = 0;

		z->next_out  = zipBuffer;
		z->avail_out = DUMP_READ_BUFFER_SIZE;

		err = deflateInit2(z, DUMP_COMPRESS_LEVEL, Z_DEFLATED, MAX_WBITS_AND_USE_GZIP_PLEASE, 8, Z_DEFAULT_STRATEGY);
		if(err != Z_OK)
			return DUMPRESULT_FAILURE_DEFLATE_INIT;
	}

	// -----------------------------------------------

	if (piTotalBytes)
	{
		fseek(file, 0, SEEK_END);
		*piTotalBytes = ftell(file);
		fseek(file, 0, SEEK_SET);
	}
	return DUMPRESULT_SUCCESS;
}

static DumpResult errorTrackerSendFileUncompressed(NetLink *link, int iCmd, SA_PARAM_NN_VALID FileWrapper *file, size_t iTotalBytes)
{
	size_t iTotalBytesRead = 0;
	size_t iSentBytes = 0;

	if(sThrottleResetCallback)
		sThrottleResetCallback(iTotalBytes);

	while(!errorProgressDlgIsCancelled() && !errorProgressDlgWasExplicitlyCancelled())
	{
		size_t iReadBytes;
		errorProgressDlgPump();
		if(!linkHasValidSocket(link))
			return DUMPRESULT_FAILURE_LINK_DROPPED;

		commMonitor(netComm);

		// Read in a bit more of the chunk
		iReadBytes = fread(readBuffer, 1, DUMP_READ_BUFFER_SIZE, file);
		if (iReadBytes > 0)
		{
			iTotalBytesRead += iReadBytes;

			if(sThrottleProgressCallback)
				sThrottleProgressCallback(iTotalBytesRead);

			// clear our write buffer, if necessary
			iSentBytes += iReadBytes;
			sendBytes(link, iCmd, readBuffer, (U32)iReadBytes, (U32)iSentBytes);
			errorProgressDlgUpdate(iTotalBytesRead, iTotalBytes);
		}
		else // feof()
			return DUMPRESULT_SUCCESS;
	}

	if (errorProgressDlgWasExplicitlyCancelled())
		return DUMPRESULT_FAILURE_SEND_EXPLICIT_CANCELLED;
	else
		return DUMPRESULT_FAILURE_SEND_CANCELLED;

}

static DumpResult errorTrackerSendFileCompressed(NetLink *link, int iCmd, SA_PARAM_NN_VALID FileWrapper *file, SA_PARAM_NN_VALID z_stream *zipStream, 
	size_t iTotalBytes, SA_PARAM_NN_VALID size_t *piSentBytes)
{
	size_t iTotalBytesRead = 0;
	int zflags = 0;
	int err;

	*piSentBytes = 0;
	if(sThrottleResetCallback)
		sThrottleResetCallback(iTotalBytes);

	while (!errorProgressDlgIsCancelled() && !errorProgressDlgWasExplicitlyCancelled())
	{
		errorProgressDlgPump();
		if(!linkHasValidSocket(link))
			return DUMPRESULT_FAILURE_LINK_DROPPED;

		commMonitor(netComm);

		// Fill our read buffer, if necessary
		if (zipStream && zipStream->avail_in == 0)
		{
			// Read in a bit more of the chunk
			size_t iReadBytes = fread(readBuffer, 1, DUMP_READ_BUFFER_SIZE, file);
			if (iReadBytes == 0) // feof()
				zflags |= Z_FINISH;

			iTotalBytesRead += iReadBytes;

			if(sThrottleProgressCallback)
				sThrottleProgressCallback(iTotalBytesRead);

			zipStream->avail_in = (unsigned int)iReadBytes;
			zipStream->next_in  = readBuffer;
		}

		// clear our write buffer, if necessary
		if(zipStream && zipStream->avail_out == 0)
		{
			*piSentBytes += DUMP_READ_BUFFER_SIZE;
			sendBytes(link, iCmd, zipBuffer, DUMP_READ_BUFFER_SIZE, (U32) *piSentBytes);
			errorProgressDlgUpdate(iTotalBytesRead, iTotalBytes);

			// Reset the zipBuffer ptr
			zipStream->avail_out = DUMP_READ_BUFFER_SIZE;
			zipStream->next_out  = zipBuffer;
		}

		// Call deflate
		err = deflate(zipStream, zflags);

		if(err == Z_STREAM_END)
		{
			// Send whatever is leftover
			int leftover = DUMP_READ_BUFFER_SIZE - zipStream->avail_out;
			if(leftover > 0)
			{
				*piSentBytes += leftover;
				sendBytes(link, iCmd, zipBuffer, (U32)leftover, (U32) *piSentBytes);
			}
			return DUMPRESULT_SUCCESS;
		}
		else if (err != Z_OK)
		{
			return DUMPRESULT_FAILURE_DEFLATE_ERROR_WHILE_SENDING;
		}
	}

	if (errorProgressDlgWasExplicitlyCancelled())
		return DUMPRESULT_FAILURE_SEND_EXPLICIT_CANCELLED;
	else
		return DUMPRESULT_FAILURE_SEND_CANCELLED;
}

const char * DumpResultToString(DumpResult dumpResult)
{
	switch(dumpResult)
	{
	case DUMPRESULT_SUCCESS:                             return "Success";
	case DUMPRESULT_FAILURE_RECURSIVE:                   return "Recursive failure";
	case DUMPRESULT_FAILURE_CONNECT_TIMEOUT:             return "Connect timeout";
	case DUMPRESULT_FAILURE_NOTHING_TO_SEND:             return "Nothing to send";
	case DUMPRESULT_FAILURE_DEFLATE_INIT:                return "Deflate init";
	case DUMPRESULT_FAILURE_NO_DUMP_FILE:                return "No dump file on disk";
	case DUMPRESULT_FAILURE_CANT_OPEN_FILE:              return "Cannot open dump file";
	case DUMPRESULT_FAILURE_SEND_CANCELLED:              return "Send cancelled";
	case DUMPRESULT_FAILURE_LINK_DROPPED:                return "Link dropped";
	case DUMPRESULT_FAILURE_DEFLATE_ERROR_WHILE_SENDING: return "Deflate error during send";
	case DUMPRESULT_FAILURE_NO_RECEIPT:                  return "No receipt";
	}

	return "Unknown failure";
}

DumpResult errorTrackerSendDump(const char *pCrashInfo, const char *pDescription, int dump_type)
{
#if _PS3
#elif _XBOX
	// Do nothing with this external call ... the 360 sends everything via
	// errorTrackerWriteDumpCache(), located below
	return DUMPRESULT_SUCCESS;
#else
	return errorTrackerSendDumpInternal(pCrashInfo, pDescription, NULL, dump_type);
#endif
}

void errorTrackerSendDumpDescriptionOnly(const char *pCrashDescription)
{
	static bool sbInsideErrorTrackerSendDump = false;
	DWORD start_tick;
	NetLink *pErrorTrackerLink;
	Packet *pPak;

	EnterCriticalSection(&gErrorAccess);
	if(sbInsideErrorTrackerSendDump)
	{
		printf("ERROR: errorTrackerSendDump(): Already inside this function! (recursive)\n");
		LeaveCriticalSection(&gErrorAccess);
		return;
	}
	sbInsideErrorTrackerSendDump = true;

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	commSetSendTimeout(netComm, 10.0f);

	pErrorTrackerLink = commConnect(netComm,LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS,getErrorTracker(),DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_DESCRIPTION_ONLY);
	pktSendU32(pPak, iErrorTrackerDumpFlags);
	pktSendU32(pPak, iErrorTrackerDumpID);
	pktSendString(pPak, pCrashDescription);
	pktSend(&pPak);

	// ---------------------------------------------------------------------------------
	
	linkFlush(pErrorTrackerLink);
	commMonitor(netComm);

	start_tick = GetTickCount();
	while(!sbReceiptReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_RECEIPT_TIMEOUT))
		{
			break;
		}

		Sleep(1);
		commMonitor(netComm);
	}
	commMonitor(netComm);
	linkShutdown(&pErrorTrackerLink);
	sbReceiptReceived = false;
	sbInsideErrorTrackerSendDump = false;
	LeaveCriticalSection(&gErrorAccess);
}

void errorTrackerSendDumpDescriptionUpdate(const char *pDescription)
{
	static bool sbInsideErrorTrackerSendDump = false;
	DWORD start_tick;
	NetLink *pErrorTrackerLink;
	Packet *pPak;

	if (iUniqueID == 0) //We have no error ID, so we can't send any useful information
		return;
	if(!(*pDescription)) //no user input, nothing worthwhile to send.
		return; 


	EnterCriticalSection(&gErrorAccess);
	if(sbInsideErrorTrackerSendDump)
	{
		printf("ERROR: errorTrackerSendDump(): Already inside this function! (recursive)\n");
		LeaveCriticalSection(&gErrorAccess);
		return;
	}
	sbInsideErrorTrackerSendDump = true;

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	commSetSendTimeout(netComm, 10.0f);

	pErrorTrackerLink = commConnect(netComm,LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS,getErrorTracker(),DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	sbReceiptReceived = false;
	pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_DESCRIPTION_UPDATE);
	pktSendU32(pPak, iUniqueID);
	pktSendU32(pPak, iErrorTrackerDumpIndex); //If there is no index (ErrorTracker didn't want a dump) it will try to use -1 here, which will become MAX_INT.
	pktSendString(pPak, pDescription);
	pktSend(&pPak);

	// ---------------------------------------------------------------------------------
	
	linkFlush(pErrorTrackerLink);
	commMonitor(netComm);

	start_tick = GetTickCount();
	while(!sbReceiptReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_RECEIPT_TIMEOUT))
		{
			break;
		}

		Sleep(1);
		commMonitor(netComm);
	}
	commMonitor(netComm);
	linkShutdown(&pErrorTrackerLink);
	sbReceiptReceived = false;
	sbInsideErrorTrackerSendDump = false;
	LeaveCriticalSection(&gErrorAccess);
}

static DumpResult errorTrackerSendDumpInternal(const char *pCrashInfo, const char *pCrashDescription, const char *pDeferredString, int dump_type)
{
	static bool sbInsideErrorTrackerSendDump = false;

	NetLink *pErrorTrackerLink;
	Packet *pPak;
	char *pDumpFilename = NULL;
	DWORD start_tick;
	int iDumpFlagsCopy = iErrorTrackerDumpFlags;
	DumpResult dumpResult = DUMPRESULT_SUCCESS;
	FILE *pDumpFile = NULL;
	size_t iTotalBytes = 0;
	size_t iSentBytes = 0;

	EnterCriticalSection(&gErrorAccess);

	if(sbInsideErrorTrackerSendDump)
	{
		printf("ERROR: errorTrackerSendDump(): Already inside this function! (recursive)\n");
		LeaveCriticalSection(&gErrorAccess);
		return DUMPRESULT_FAILURE_RECURSIVE;
	}
	sbInsideErrorTrackerSendDump = true;

	// Figure out what dump to send and open the file
	if (!dump_type)
		dump_type = DUMPFLAGS_FULLDUMP | DUMPFLAGS_MINIDUMP; // default to either
	// Mini or full?
	if (!!(errorTrackerGetDumpFlags() & DUMPFLAGS_FULLDUMP & dump_type))
	{
		pDumpFilename = assertGetFullDumpFilename();
		iDumpFlagsCopy = iErrorTrackerDumpFlags;
	}
	else if ((!!(errorTrackerGetDumpFlags() & DUMPFLAGS_MINIDUMP)) || dump_type == DUMPFLAGS_MINIDUMP)
	{
		pDumpFilename = assertGetMiniDumpFilename();
		iDumpFlagsCopy = (iDumpFlagsCopy & ~(DUMPFLAGS_FULLDUMP)) | DUMPFLAGS_MINIDUMP;
	}
	else
	{
		// Don't want to send anything?
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return DUMPRESULT_FAILURE_NOTHING_TO_SEND;
	}
	// Always sends dumps compressed
	iDumpFlagsCopy |= DUMPFLAGS_COMPRESSED;

	if(!pDumpFilename || !fileExists(pDumpFilename))
	{
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return DUMPRESULT_FAILURE_NO_DUMP_FILE;
	}

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	commSetSendTimeout(netComm, 10.0f);

	pErrorTrackerLink = commConnect(netComm,LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS,getErrorTracker(),DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return DUMPRESULT_FAILURE_CONNECT_TIMEOUT;
	}
	
	pDumpFile = (FILE*)fopen(pDumpFilename, "rb");
	if(pDumpFile == NULL)
	{
		linkRemove(&pErrorTrackerLink);
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return DUMPRESULT_FAILURE_CANT_OPEN_FILE;
	}

	linkFlushLimit(pErrorTrackerLink, 32768);

	{
		z_stream z = {0};
		dumpResult = errorTrackerInitFileSend(pDumpFile, &z, &iTotalBytes);

		if (dumpResult != DUMPRESULT_SUCCESS)
		{
			fclose(pDumpFile);
			linkRemove(&pErrorTrackerLink);
			sbInsideErrorTrackerSendDump = false;
			LeaveCriticalSection(&gErrorAccess);
			return DUMPRESULT_FAILURE_DEFLATE_INIT;
		}

		errorProgressDlgInit(pCrashInfo);

		pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_START);
		pktSendU32(pPak, iDumpFlagsCopy);
		if(iDumpFlagsCopy & DUMPFLAGS_DEFERRED)
			pktSendString(pPak, pDeferredString);
		pktSendU32(pPak, iErrorTrackerDumpID);
		if (!(iDumpFlagsCopy & DUMPFLAGS_DEFERRED) && iDumpFlagsCopy & DUMPFLAGS_EXTERNAL)
			pktSendString(pPak, pCrashDescription);
		pktSend(&pPak);

		dumpResult = errorTrackerSendFileCompressed(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_CONTINUE, pDumpFile, &z, iTotalBytes, &iSentBytes);
		
		if (dumpResult == DUMPRESULT_FAILURE_SEND_EXPLICIT_CANCELLED)
		{
			pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_CANCELLED);
			pktSendU32(pPak, iErrorTrackerDumpID);
			pktSend(&pPak);
		}

		errorProgressDlgShutdown();
		fclose(pDumpFile);
		deflateEnd(&z);
	}

	if (dumpResult != DUMPRESULT_SUCCESS)
	{
		linkRemove(&pErrorTrackerLink);
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return dumpResult;
	}

	pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_FINISH);
	pktSendU32(pPak, (U32)iSentBytes);          // How many bytes should be in the final file?
	pktSendU32(pPak, iErrorTrackerDumpID); // Maps this dump to a particular crash
	pktSendU32(pPak, (U32)iTotalBytes);         // How many bytes in the uncompressed stream?
	pktSend(&pPak);

	// ---------------------------------------------------------------------------------

	linkFlush(pErrorTrackerLink);
	commMonitor(netComm);

	start_tick = GetTickCount();
	while (!sbReceiptReceived && !errorProgressDlgIsCancelled())
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_RECEIPT_TIMEOUT))
		{
			dumpResult = DUMPRESULT_FAILURE_NO_RECEIPT;
			break;
		}

		Sleep(1);
		commMonitor(netComm);
	}
	commMonitor(netComm);
	linkShutdown(&pErrorTrackerLink);
	sbReceiptReceived = false;
	sbInsideErrorTrackerSendDump = false;
	LeaveCriticalSection(&gErrorAccess);
	return dumpResult;
}

static char * getDumpCacheFilename(void)
{
	static char fn[MAX_PATH] = {0};
	
	if(fn[0] == 0)
	{
		char temp[MAX_PATH];
		char *p;
		strcpy(temp, getExecutableName());

		p = strrchr(temp, '\\');
		if(p)
		{
			p++;
		}
		else
		{
			p = temp;
		}

		sprintf(fn, "E:\\dumps\\%s.dumpcache", p);
	}

	return fn;
}

void errorTrackerWriteDumpCache(ErrorData *pErrorData)
{
#if _XBOX
	DM_DUMP_SETTINGS dumpSettings = {0};
	char *pCacheFilename = getDumpCacheFilename();
	ErrorDataCache *pCache;
	DWORD dumpMode;

	// If the error tracker didn't want either dump, don't bother to write the cache.
	if(!(errorTrackerGetDumpFlags() & (DUMPFLAGS_FULLDUMP|DUMPFLAGS_MINIDUMP)))
	{
		printf("avoiding ErrorData cache writing ... ErrorTracker doesn't want a dump.");
		if (iUniqueID)
		{
			// do not write out dump
			DmGetDumpMode(&dumpMode);
			if (dumpMode == DM_DUMPMODE_ENABLED)
			{
				DmSetDumpMode(DM_DUMPMODE_SMART);
			}
		}
		return;
	}

	pCache             = StructCreate(parse_ErrorDataCache);
	pCache->iUniqueID  = iUniqueID;
	pCache->iDumpFlags = iErrorTrackerDumpFlags;
	pCache->pErrorData = pErrorData; // NULL this before destroying pCache!

	makeDirectoriesForFile(pCacheFilename);
	ParserWriteTextFile(pCacheFilename, parse_ErrorDataCache, pCache, 0, 0);

	// Set the dump settings on the xbox to whatever the error tracker requested: full or mini?
	if(iErrorTrackerDumpFlags & DUMPFLAGS_FULLDUMP)
	{
		dumpSettings.ReportingFlags = DUMP_RF_FORMAT_FULL;
	}
	else
	{
		dumpSettings.ReportingFlags = DUMP_RF_FORMAT_PARTIAL;
	}
	// TODO will this work for retail?
	DmGetDumpMode(&dumpMode);
	if (dumpMode != DM_DUMPMODE_ENABLED)
	{
		DmSetDumpMode(DM_DUMPMODE_ENABLED);
	}

	DmSetDumpSettings(&dumpSettings);

	pCache->pErrorData = NULL; // We don't own this
	StructDestroy(parse_ErrorDataCache, pCache);
#endif
}

void errorTrackerCheckDumpCache(void)
{
#if _XBOX
	char *pCacheFilename = getDumpCacheFilename();
	char *pDumpFilename  = assertGetFullDumpFilename();
	bool cacheExists     = fileExists(pCacheFilename);
	bool dumpExists      = fileExists(pDumpFilename);
	ErrorDataCache cache = {0};

	printf("Cache FileName: %s\nDump FileName: %s\n", pCacheFilename, pDumpFilename);
	if(cacheExists && dumpExists)
	{
		char *pDeferredString = NULL;
		printf("Dump found.\n");
		estrStackCreate(&pDeferredString);
		ParserReadTextFile(pCacheFilename, parse_ErrorDataCache, &cache, 0);
		if (cache.pErrorData)
		{
			ParserWriteText(&pDeferredString, parse_ErrorData, cache.pErrorData, 0, 0, 0);
		}
		else
		{
			ErrorData emptyData = {0};
			ParserWriteText(&pDeferredString, parse_ErrorData, &emptyData, 0, 0, 0);
		}

		iErrorTrackerDumpFlags = cache.iDumpFlags|DUMPFLAGS_DEFERRED;
		iErrorTrackerDumpID    = cache.iUniqueID;
		iUniqueID              = cache.iUniqueID;

		errorTrackerSendDumpInternal("", NULL, pDeferredString, DUMPFLAGS_FULLDUMP | DUMPFLAGS_MINIDUMP);

		StructDeInit(parse_ErrorDataCache, &cache);
		estrDestroy(&pDeferredString);
	}

	// Cleanup the files so we aren't constantly sending the same dump over and over,
	// and so that the filenames are accurate on the 360 dump dir (don't get "1" appended, etc)

	if(cacheExists)
		DeleteFile(pCacheFilename);

	if(dumpExists)
		DeleteFile(pDumpFilename);
#endif
}

static void errorTrackerSendData(NetLink *link, int iCmd, void *pData, U32 uSize)
{
	U32 uBytesSent = 0;
	Packet *pkt;
	while (uBytesSent < uSize)
	{
		U32 curIndex = uBytesSent;
		U32 pktSize = min (uSize - uBytesSent, DUMP_READ_BUFFER_SIZE);
		if(!linkHasValidSocket(link))
		{
			printf("errorTrackerSendDump(): Bailing out, socket went away\n");
			return;
		}
		uBytesSent += pktSize;

		pkt = pktCreate(link, iCmd);
		pktSendU32(pkt, pktSize);
		pktSendU32(pkt, uBytesSent);
		pktSendBytes(pkt, pktSize, ((char*) pData) + curIndex);
		pktSend(&pkt);
	}
}

// sends a dump
void errorTrackerSendMemoryDump (U32 errorID, void *pData, U32 size)
{
	static bool sbInsideErrorTrackerSendDump = false;
	DWORD start_tick;
	NetLink *pErrorTrackerLink;
	Packet *pkt;
	U32 uBytesSent = 0;

	EnterCriticalSection(&gErrorAccess);
	
	if (sbInsideErrorTrackerSendDump)
	{
		printf("ERROR: errorTrackerSendMemoryDump(): Already inside this function! (recursive)\n");
		LeaveCriticalSection(&gErrorAccess);
		return;
	}
	sbInsideErrorTrackerSendDump = true;

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!
	commSetSendTimeout(netComm, 10.0f);

	pErrorTrackerLink = commConnect(netComm,LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,getErrorTracker(),
		DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	sbReceiptReceived = false;
	pkt = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_MEMORY_DUMP_START);
	pktSendU32(pkt, size); // total size
	pktSendU32(pkt, errorID);
	pktSend(&pkt);

	errorTrackerSendData(pErrorTrackerLink, TO_ERRORTRACKER_MEMORY_DUMP_CONTINUE, pData, size);

	pkt = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_MEMORY_DUMP_END);
	pktSendU32(pkt, size); // total size
	pktSendU32(pkt, errorID);
	pktSend(&pkt);

	linkFlush(pErrorTrackerLink);
	commMonitor(netComm);

	start_tick = GetTickCount();
	while(!sbReceiptReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_RECEIPT_TIMEOUT))
		{
			break;
		}

		Sleep(1);
		commMonitor(netComm);
	}
	commMonitor(netComm);
	linkShutdown(&pErrorTrackerLink);
	sbInsideErrorTrackerSendDump = false;
	sbReceiptReceived = false;
	LeaveCriticalSection(&gErrorAccess);
}

void errorTrackerSendGenericFile(U32 uErrorID, ErrorFileType eFileType, const char *filepath, void *pExtraData, ParseTable *pti)
{
	static bool sbInsideErrorTrackerSendDump = false;
	DWORD start_tick;
	NetLink *pErrorTrackerLink;
	Packet *pkt;
	FILE *file;
	size_t iTotalBytes, iSentBytes = 0;
	DumpResult dumpResult;
	z_stream z = {0};

	EnterCriticalSection(&gErrorAccess);

	if (sbInsideErrorTrackerSendDump)
	{
		printf("ERROR: errorTrackerSendGenericFile(): Already inside a dump/file sending function! (recursive)\n");
		LeaveCriticalSection(&gErrorAccess);
		return;
	}
	sbInsideErrorTrackerSendDump = true;

	file = fopen(filepath, "rb");
	if (!file)
	{
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!
	commSetSendTimeout(netComm, 10.0f);

	pErrorTrackerLink = commConnect(netComm,LINKTYPE_USES_FULL_SENDBUFFER, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,getErrorTracker(),
		DEFAULT_ERRORTRACKER_PUBLIC_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pErrorTrackerLink, ERRORTRACKER_CONNECT_TIMEOUT))
	{
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	dumpResult = errorTrackerInitFileSend(file, &z, &iTotalBytes);
	if (dumpResult != DUMPRESULT_SUCCESS)
	{
		fclose(file);
		linkRemove(&pErrorTrackerLink);
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	sbReceiptReceived = false;
	pkt = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_GENERICFILE_START);
	pktSendU32(pkt, eFileType); // File type info
	pktSendU32(pkt, (U32)iTotalBytes); // total file size
	pktSend(&pkt);

	dumpResult = errorTrackerSendFileCompressed(pErrorTrackerLink, TO_ERRORTRACKER_GENERICFILE_CONTINUE, file, &z, iTotalBytes, &iSentBytes);
	if (dumpResult == DUMPRESULT_FAILURE_SEND_EXPLICIT_CANCELLED)
	{
		// TODO(Theo)
		/*pkt = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_CANCELLED);
		pktSendU32(pkt, iErrorTrackerDumpID);
		pktSend(&pkt);*/
	}
	errorProgressDlgShutdown();
	fclose(file);
	if (dumpResult != DUMPRESULT_SUCCESS)
	{
		linkRemove(&pErrorTrackerLink);
		sbInsideErrorTrackerSendDump = false;
		LeaveCriticalSection(&gErrorAccess);
		return;
	}

	pkt = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_GENERICFILE_END);
	pktSendU32(pkt, (U32)iSentBytes); // total bytes sent
	pktSendU32(pkt, uErrorID);
	pktSendU32(pkt, (U32)iTotalBytes); // total size (uncompressed)
	if (pExtraData)
		pktSendStruct(pkt, pExtraData, pti);
	pktSend(&pkt);

	linkFlush(pErrorTrackerLink);
	commMonitor(netComm);

	start_tick = GetTickCount();
	while(!sbReceiptReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_RECEIPT_TIMEOUT))
		{
			break;
		}
		Sleep(1);
		commMonitor(netComm);
	}
	commMonitor(netComm);
	linkShutdown(&pErrorTrackerLink);
	sbReceiptReceived = false;
	sbInsideErrorTrackerSendDump = false;
	LeaveCriticalSection(&gErrorAccess);
}

// -------------------------------------------------------------------------------------
// Error Threading

static void errorSendDispatch(void *user_data, char **pParseString, WTCmdPacket *packet)
{
	EnterCriticalSection(&gErrorThreadAccess);
	errorTrackerSendErrorRaw(*pParseString);
	LeaveCriticalSection(&gErrorThreadAccess);
	free(*pParseString);
}

static void lazyInitErrorThread(void)
{
	ATOMIC_INIT_BEGIN;
	{
		spErrorThread = wtCreate(ERROR_THREAD_MAX_QUEUED_ERRORS, 2, NULL, "errorThread");
		wtRegisterCmdDispatch(spErrorThread, ERRORCMD_SEND, errorSendDispatch);
		wtSetProcessor(spErrorThread, THREADINDEX_NETSEND);
		wtSetThreaded(spErrorThread, true, 0, false);
		wtSetSkipIfFull(spErrorThread, true);
		wtStart(spErrorThread);
		EnterCriticalSection(&gErrorThreadStashAccess);
		stErrorQueueResults = stashTableCreateInt(ERROR_THREAD_MAX_QUEUED_ERRORS);
		LeaveCriticalSection(&gErrorThreadStashAccess);
	}
	ATOMIC_INIT_END;
}


#include "autogen/errornet_h_ast.c"
#include "autogen/errornet_c_ast.c"