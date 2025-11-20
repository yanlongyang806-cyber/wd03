#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

#include "wininclude.h"
#if !PLATFORM_CONSOLE
	//#include <DbgHelp.h>
	//#include "errorrep.h"
	//#include "CrashRpt.h"
	#include <tlhelp32.h>
#endif
#include "tchar.h"

#include "error.h"
#include "errornet.h"
#include "stringcache.h"
#include "timing.h"
#include "file.h"
#include "logging.h"
#include "GlobalTypes.h"
#include "gimmeDLLWrapper.h"
#include "HashFunctions.h"
#include "EString.h"
#include "FolderCache.h"
#include "sysutil.h"
#include "errornet.h"
#include "utilitiesLib.h"
#include "ScratchStack.h"
#include "stackwalk.h"
#include "Message.h"
#include "stringutil.h"
#include "NameValuePair.h"
#include "RegistryReader.h"
#include "alerts.h"
#include "trivia.h"
#include "trivia_h_ast.h"
#include "ContinuousBuilderSupport.h"
#include "MemReport.h"
#include "StatusReporting.h"
#include "memlog.h"
#include "workerthread.h"
#include "AutoGen/error_h_ast.c"
#include "error_c_ast.h"

//including things in serverlib from utilitieslib is a no-no. I don't see a good way around it right now though

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define ASSERT_BUF_SIZE 1024 * 10	// Be prepared for a large stackdump.

extern int sEnableErrorThreading;

char* g_lastAuthorIntro = "Last Author/Status:";
bool g_disableLastAuthor= true; // R Disabled

bool gbDontDoAlerts = true; // R Disabled
bool gbDontReportErrorsToErrorTracker = true; // R Disabled
// Disables sending errors to the error tracker
AUTO_CMD_INT(gbDontReportErrorsToErrorTracker, dontReportErrorsToErrorTracker) ACMD_CMDLINE;

bool gbIgnoreAllErrors = true; // R Disabled
AUTO_CMD_INT(gbIgnoreAllErrors, IgnoreAllErrors) ACMD_CMDLINE;

// Don't error on missing textures
bool gbIgnoreTextureErrors = false;
AUTO_CMD_INT(gbIgnoreTextureErrors, IgnoreTextureErrors) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

static bool sbAllErrorFilenamesNonBlocking = false;
AUTO_CMD_INT(sbAllErrorFilenamesNonBlocking, AllErrorFilenamesNonBlocking) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

ErrorMessage **g_deferred_errors;

bool gbDontLogErrors=true; // R Disabled
// Disables logging of errors to ERRORS.log
AUTO_COMMAND ACMD_CMDLINE;
void dontLogErrors(bool bDontLogErrors)
{
	gbDontLogErrors = bDontLogErrors;
}

static CRITICAL_SECTION reportErrorCS;
static CRITICAL_SECTION deferredErrorAlertCS;
static CRITICAL_SECTION errorLookupCS;

void UpdateDeferredErrorOrAlerts(void);


static bool sbAlwaysReportDespiteDuplication = false;
void ErrorSetAlwaysReportDespiteDuplication(bool bSet)
{
	sbAlwaysReportDespiteDuplication = bSet;
}

AUTO_RUN_EARLY;
void AutoInitErrorCriticalSection(void)
{
	InitializeCriticalSection(&reportErrorCS);
	InitializeCriticalSection(&deferredErrorAlertCS);
	InitializeCriticalSection(&errorLookupCS);
}

// This value is passed to the error tracker during a crash/assert/errorf
// Should be updated regularly with the actual number of clients that would
// be affected by this particular application going away. 
// Note: It should probably just be set to 1, once at startup, for game clients.
int g_errorClientCount = 0;

// This estring is also passed to the error tracker during a crash/assert/errorf
// as the "trivia" string. Please use trivia.h's trivia*() functions to populate
// this string indirectly.
extern char *g_errorTriviaString;
extern char* assertGetAccountName(void);

bool gbNoCallStacksOnErrors = true;
AUTO_CMD_INT(gbNoCallStacksOnErrors, NoCallStacksOnErrors) ACMD_CMDLINE;

// Utility functions for building error messages

typedef struct PerThreadErrorDetails
{
	char *details; // estring
} PerThreadErrorDetails;

static PerThreadErrorDetails *getPerThreadErrorDetails(void)
{
	PerThreadErrorDetails *p;
	STATIC_THREAD_ALLOC(p);
	return p;
}

static ErrorMessage *BuildErrorMessage(ErrorMessageType type, const char *file, int line, const char *filename, const char *group, int days, int curmon, int curday, int curyear, FORMAT_STR char const *fmt, va_list ap)
{
	ErrorMessage *deferr = NULL;
	PerThreadErrorDetails *pPerThreadErrorDetails = NULL;

	PERFINFO_AUTO_START_FUNC();

	deferr = callocStruct(ErrorMessage);
	if(!deferr)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	deferr->errorType = type;
	pPerThreadErrorDetails = getPerThreadErrorDetails();
	if (estrLength(&pPerThreadErrorDetails->details) > 0)
	{
		// Copy and clear the TLS trivia string storage to avoid future false positives
		estrCopy(&deferr->estrDetails, &pPerThreadErrorDetails->details);
		estrCopy2(&pPerThreadErrorDetails->details, "");
	}
	if (type == ERROR_FATAL)
	{
		deferr->bForceShow = 0; // R Disabled
	}
	deferr->file = file;
	deferr->line = line;
	deferr->filename = allocAddFilename(filename);
	deferr->days = days;
	deferr->group = allocAddString(group);
	estrConcatfv(&deferr->estrMsg, fmt, ap);
	deferr->curyear = curyear;
	deferr->curmon = curmon;
	deferr->curday = curday;

	// By default errors are relevant and reported
	if (type == ERROR_DEPRECATED)
	{
		deferr->bRelevant = false;
	}
	else
	{	
		deferr->bRelevant = false; // R Disabled
	}
	deferr->bReport = false; // R Disabled

	PERFINFO_AUTO_STOP();
	return deferr;	
}

static void DestroyErrorMessage(ErrorMessage *errorMessage)
{
	if (errorMessage)
	{	
		PERFINFO_AUTO_START_FUNC();
		estrDestroy(&errorMessage->estrMsg);
		estrDestroy(&errorMessage->estrFormattedMsg);
		estrDestroy(&errorMessage->estrDetails);
		SAFE_FREE(errorMessage->author);
		free(errorMessage);
		PERFINFO_AUTO_STOP();
	}
}


#define GIMME_TIME 30
static bool errorIsInGimmeGet(void)
{
	bool errorFromGimmeGet = false;
    #if !PLATFORM_CONSOLE
	U32 gimmeTime = 0;
	RegReader reader;
	if (isProductionMode()) // Production mode doesn't care about Gimme files
		return false;

	reader = createRegReader();
	initRegReader(reader, "HKEY_LOCAL_MACHINE\\Software\\RaGEZONE\\Gimme");
	if (rrReadInt(reader, "GlvTimestamp", &gimmeTime)) {
		U32 time = timeSecondsSince2000();

		if (gimmeTime + GIMME_TIME > time)
		{
			errorFromGimmeGet = true;
		}
	}

	destroyRegReader(reader);
	#endif
	return errorFromGimmeGet;
}

#if !PLATFORM_CONSOLE
// Copied from SuperAssert.c
static DWORD err_safe_threads[10];
static volatile long err_safe_threads_count=0;


static bool errorCanFreezeThisThread(DWORD threadID)
{
	int i;
	for (i=0; i<err_safe_threads_count; i++) 
		if (err_safe_threads[i]==threadID)
			return false;
	return true;
}

void errorDoNotFreezeThisThread(DWORD threadID)
{
	if (errorCanFreezeThisThread(threadID)) { // Not already in list
		long index = InterlockedIncrement(&err_safe_threads_count);
		err_safe_threads[index-1] = threadID;
	}
}

void errorFreezeAllOtherThreads(int resume)
{

	DWORD			dwOwnerPID	= GetCurrentProcessId();
	DWORD			dwOwnerTID	= GetCurrentThreadId();
    HANDLE			hThreadSnap	= NULL; 
    BOOL			bRet		= FALSE; 
    THREADENTRY32	te32		= {0}; 
	typedef HANDLE (WINAPI *tOpenThread)(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId);
	tOpenThread		pOpenThread;
	static HMODULE	hKernel32Dll = 0;

	if (!hKernel32Dll)
	{
		hKernel32Dll = LoadLibrary(_T("kernel32.dll"));
	}

	if (!hKernel32Dll)
		return;
	pOpenThread = (tOpenThread)GetProcAddress(hKernel32Dll, "OpenThread");
	if (!pOpenThread)
	{
		CloseHandle(hKernel32Dll);
		return;
	}

	errorDoNotFreezeThisThread(dwOwnerTID);

    // Take a snapshot of all threads currently in the system. 
    hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); 
    if (hThreadSnap == INVALID_HANDLE_VALUE) 
        return;
 
    te32.dwSize = sizeof(THREADENTRY32); 
 
    if (Thread32First(hThreadSnap, &te32))
	{
        do {
            if (te32.th32OwnerProcessID == dwOwnerPID)
            { 
				if (errorCanFreezeThisThread(te32.th32ThreadID))
				{
					HANDLE hThread = pOpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
					if (hThread) {
						if (resume) {
							// Resmue it!
							ResumeThread(hThread);
						} else {
							// Suspend it!
							SuspendThread(hThread);
						}
						CloseHandle(hThread);
					}
				}
            } 
        } 
        while (Thread32Next(hThreadSnap, &te32)); 
        bRet = TRUE; 
    } 
    else 
        bRet = FALSE;          // could not walk the list of threads 
 
    CloseHandle (hThreadSnap); 
 
    return; 
}
#endif

static bool gbDontReportErrorsToErrorTracker_stack[3];
static int gbDontReportErrorsToErrorTracker_stack_size=0;
void pushDontReportErrorsToErrorTracker(bool newValue)
{
	assert(gbDontReportErrorsToErrorTracker_stack_size>=0);
	assert(gbDontReportErrorsToErrorTracker_stack_size < ARRAY_SIZE(gbDontReportErrorsToErrorTracker_stack));
	gbDontReportErrorsToErrorTracker_stack[gbDontReportErrorsToErrorTracker_stack_size++] = gbDontReportErrorsToErrorTracker;
	gbDontReportErrorsToErrorTracker = newValue;
}

void popDontReportErrorsToErrorTracker(void)
{
	assert(gbDontReportErrorsToErrorTracker_stack_size <= ARRAY_SIZE(gbDontReportErrorsToErrorTracker_stack));
	assert(gbDontReportErrorsToErrorTracker_stack_size);
	gbDontReportErrorsToErrorTracker = gbDontReportErrorsToErrorTracker_stack[--gbDontReportErrorsToErrorTracker_stack_size];
}


AUTO_COMMAND ACMD_CMDLINE;
void disableLastAuthor(bool disable)
{
	disable = true; // R Disabled
	g_disableLastAuthor = disable;
}


static bool gbDisableLastAuthor_stack[3];
static int gbDisableLastAuthor_stack_size=0;
void pushDisableLastAuthor(bool newValue)
{
	assert(gbDisableLastAuthor_stack_size>=0);
	assert(gbDisableLastAuthor_stack_size < ARRAY_SIZE(gbDisableLastAuthor_stack));
	gbDisableLastAuthor_stack[gbDisableLastAuthor_stack_size++] = g_disableLastAuthor;
	g_disableLastAuthor = newValue;
}

void popDisableLastAuthor(void)
{
	assert(gbDisableLastAuthor_stack_size <= ARRAY_SIZE(gbDisableLastAuthor_stack));
	assert(gbDisableLastAuthor_stack_size);
	g_disableLastAuthor = gbDisableLastAuthor_stack[--gbDisableLastAuthor_stack_size];
}

// Default Callbacks

void defaultErrorCallback(ErrorMessage *errMsg, void *userdata)
{
	PERFINFO_AUTO_START_FUNC();
	printf("%s\n", errorFormatErrorMessage(errMsg));
	PERFINFO_AUTO_STOP();
}

void defaultFatalErrorCallback(ErrorMessage *errMsg, void *userdata)
{
	char *errString = errorFormatErrorMessage(errMsg);
	printf("\n\n%s\n",errString);
	if (!gbDontLogErrors)
		filelog_printf("errors.log", "Shutting down from fatal error \"%s\"", errString);
	logWaitForQueueToEmpty();
	fatalerrorAssertmsg(0, errString);
}

typedef struct ErrorCallbackData {
	ErrorCallback cb;
	void *userdata;
} ErrorCallbackData;
//ErrorCallback fatalErrorfCallback = defaultFatalErrorCallback;
//ErrorCallback errorfCallback = defaultErrorCallback;
static ErrorCallbackData fatalErrorfCallback = {defaultFatalErrorCallback, NULL};
static int errorfCallbackStack_size=0;
static ErrorCallbackData errorfCallbackStack[8];

void FatalErrorfSetCallback(ErrorCallback func, void *userdata)
{
	fatalErrorfCallback.cb = func;
	fatalErrorfCallback.userdata = userdata;
}

void ErrorfSetCallback(ErrorCallback func, void *userdata)
{
	errorfCallbackStack_size = 0;
	ErrorfPushCallback(func, userdata);
}

void ErrorfPushCallback(ErrorCallback func, void *userdata)
{
	assert(errorfCallbackStack_size < ARRAY_SIZE(errorfCallbackStack));
	errorfCallbackStack[errorfCallbackStack_size].cb = func;
	errorfCallbackStack[errorfCallbackStack_size].userdata = userdata;
	errorfCallbackStack_size++;
}

void ErrorfPopCallback(void)
{
	assert(errorfCallbackStack_size);
	errorfCallbackStack_size--;
}

char *getWinErrString(char **pestrString, int iError)
{
	static char *last_error = NULL;

	// For console systems, just format the error code as an integer.
#if PLATFORM_CONSOLE
	estrPrintf(pestrString, "System error code %d", iError);
	return *pestrString;
#else

	char *formatBuffer;
	DWORD result;

	// Allow allocating from a saved buffer.
	if (!pestrString)
		pestrString = &last_error;

	// Attempt to format a message.
	result = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		iError,
		MAKELANGID(PRIMARYLANGID(locGetWindowsLocale(getCurrentLocale())), SUBLANG_DEFAULT),
		(LPSTR)&formatBuffer,
		0,
		NULL);

	// Fall back to simple formatting if FormatMessage() failed.
	if (!result)
	{
		int err2 = GetLastError();
		estrPrintf(pestrString, "Windows error code %d (While attempting to format, received error code %d.)", iError, err2);
		return *pestrString;
	}

	// Copy to the estring.
	estrCopy2(pestrString, formatBuffer);
	LocalFree(formatBuffer);
	return *pestrString;
#endif  // PLATFORM_CONSOLE
}

void printWinErr(const char* functionName, const char* filename, int lineno, int err)
{
	char *buffer = NULL;
	estrStackCreate(&buffer);
	getWinErrString(&buffer, err);
	printf("%s, %s, %i: %s", functionName, filename, lineno, buffer);
	estrDestroy(&buffer);
}

char *lastWinErr(void)
{
	static char *buf = NULL;

	// On PS3, just return an empty string.
#ifdef _PS3
	return "";
#else

	// Format error to a static but dynamically-allocated buffer.
	getWinErrString(&buf, GetLastError());
	return buf;
#endif  // _PS3
}

void WinErrorfInternal(bool bReport, const char *file, int line, int code, char const *fmt, ...)
{
	va_list ap;
	char *buffer = NULL;
	char *newFormat = NULL;
	unsigned i;

	// Format the Windows error code.
	estrStackCreate(&buffer);
	getWinErrString(&buffer, code);

	// Remove potential spurious formatters.
	for (i = 0; i != estrLength(&buffer); ++i)
	{
		if (buffer[i] == '%' && buffer[i+1] == '%')
		{
			++i;
			continue;
		}
		if (buffer[i] == '%')
			buffer[i] = '?';
	}

	// Create new format string.
	estrStackCreate(&newFormat);
	estrCopy2(&newFormat, fmt);
	estrAppend2(&newFormat, ": ");
	estrAppend(&newFormat, &buffer);
	estrDestroy(&buffer);

	// Process error normally.
	va_start(ap, fmt);
	ErrorvInternal(bReport, file, line, newFormat, ap);
	va_end(ap);
	estrDestroy(&newFormat);
}

//----------------------------------------------------------------
// Errorf helpers
//----------------------------------------------------------------

// True if error was sent (or at least attempted to), False if it declined to try to send it
bool ReportToErrorTracker(ErrorMessage *errMsg)
{
	const char *file = errMsg->file;
	int line = errMsg->line;
	char stackdump[ASSERT_BUF_SIZE];
	char pCallstackReport[ASSERT_BUF_SIZE];

	stackdump[0] = 0;
	pCallstackReport[0] = 0;

	// If errMsg shouldn't be sent (alert or has already been seen)
	if (!errMsg->bReport)
		return false;

	// jdrago 2010/01/28 - Ensures we only send a single instance of an error once
	if(errMsg->bReported)
		return false;
	errMsg->bReported = true;

	if (gbDontReportErrorsToErrorTracker || errorIsInGimmeGet())
	{
		return false;
	}
	else
	{
		U32 iTime;
		char *pExecutableName;
		U32 iDataFileModificationTime = 0;
		char *pOutputParseText = NULL;

		if (g_disableLastAuthor)
		{
			return false;
		}

		EnterCriticalSection(&reportErrorCS);

		pExecutableName = getExecutableName();
		iTime = timeSecondsSince2000();

#if !_PS3
		//check if we are running an executable that (a) is more than 24 hours old, and (b) isn't the latest gimme version. If
		//both are true, send no errors to the controller
		{
			U32 iExecutableModifiedTime = fileLastChangedSS2000(pExecutableName);
			const char *pExecutableGimmeString = gimmeDLLQueryLastAuthor(pExecutableName);

			if ((!pExecutableGimmeString || strstri(pExecutableGimmeString, "do not have")) && (iTime - iExecutableModifiedTime) > 24 * 60 * 60)
			{
				LeaveCriticalSection(&reportErrorCS);
				return false;
			}
		}
#endif

		//if error string starts with a filename, get its modification time
		if (errMsg->filename)
		{
			iDataFileModificationTime = fileLastChangedSS2000(errMsg->filename);
		}

#if !PLATFORM_CONSOLE
		if (!(getAssertMode() & ASSERTMODE_NOERRORSTACKWALK) && errMsg->bForceSendStack)
		{
			stackWalkDumpStackToBuffer(SAFESTR(stackdump), NULL, NULL, NULL, pCallstackReport);
		}
#endif

		// ----------------------------------------------------------------------
		// Start fresh!
		{
			ErrorData errorData = {0};
			NOCONST(TriviaList) tList = {0};
			bool usefulStack = stackdumpIsUseful(stackdump);

			// Core error type
			errorData.eType           = ERRORDATATYPE_ERROR;

			// About the executable itself
			errorData.pPlatformName   = PLATFORM_NAME;
			errorData.pExecutableName = pExecutableName;
			errorData.pProductName    = GetProductName();
			errorData.pVersionString  = (char *)GetUsefulVersionString();
			errorData.pSVNBranch = gBuildBranch;
			errorData.pAppGlobalType = GlobalTypeToName(GetAppGlobalType());

			errorData.pShardInfoString = GetShardInfoString();
			// Basic error info
			errorData.pErrorString    = errMsg->estrMsg;
			if (assertGetAccountName()[0])
				errorData.pUserWhoGotIt = STACK_SPRINTF("A: %s", assertGetAccountName());
			else
			{
#if _PS3
				errorData.pUserWhoGotIt = STACK_SPRINTF("P:%s", (char*)getUserName());

#elif _XBOX
				errorData.pUserWhoGotIt = STACK_SPRINTF("X:%s", (char*)getUserName());
#else
				errorData.pUserWhoGotIt = STACK_SPRINTF("W:%s", (char*)getUserName());
#endif
			}
			errorData.pSourceFile     = (char*)file;
			errorData.iSourceFileLine = line;

			if (!(getAssertMode() & ASSERTMODE_NOERRORSTACKWALK))
			{
				if ((getAssertMode() & ASSERTMODE_SENDCALLSTACK) || !usefulStack)
				{
					errorData.pStackData = pCallstackReport;
				}
				else
				{
					errorData.pStackData = stackdump;
				}
			}

			if(errMsg->filename && errMsg->filename[0])
			{
				errorData.pDataFile = (char *)errMsg->filename;
				errorData.iDataFileModificationTime = iDataFileModificationTime;
				if(errMsg->author && errMsg->author[0])
					errorData.pAuthor = errMsg->author;
			}

			// Lesser information (more for bugfix prioritizing than for error info)
			errorData.iClientCount    = g_errorClientCount;

			triviaPrintf("details", "%s", (errMsg->estrDetails) ? errMsg->estrDetails : "");

			tList.triviaDatas = (NOCONST(TriviaData)**) triviaGlobalGet();
			errorData.pTriviaList = (TriviaList*) &tList;

			// --- Added 06/06/07 ---
			errorData.iProductionMode = isProductionMode();

			// ----------------------------------------------------------------------
			errorTrackerSendError(&errorData);
			triviaGlobalRelease();
		}
		LeaveCriticalSection(&reportErrorCS);
	}
	errMsg->bSent = true;
	return true;
}

char *errorFormatErrorMessage(ErrorMessage *errMsg)
{
	bool bSentError = false;

	PERFINFO_AUTO_START_FUNC();

	ReportToErrorTracker(errMsg);
	if (errMsg->estrFormattedMsg)
		return errMsg->estrFormattedMsg;

	estrClear(&errMsg->estrFormattedMsg);

	if (errMsg->filename)
	{
		estrConcatf(&errMsg->estrFormattedMsg, "File: %s\nLast Author/Status:%s\n", errMsg->filename, errMsg->author);
	}

	estrAppend(&errMsg->estrFormattedMsg, &errMsg->estrMsg);

	if (!gbDontLogErrors && !stringCacheReadOnly())
	{
		log_printf(LOG_ERRORS, "%s\n", errMsg->estrFormattedMsg);
	}

	// Don't append error details to a fatalerror string, because it will use the returned
	// text in a call to superassert. At this point, however, any future usages of the 
	// returned string are informational, so it is okay to append info here.
	if((errMsg->errorType != ERROR_FATAL) && (estrLength(&errMsg->estrDetails) > 0))
	{
		estrConcatf(&errMsg->estrFormattedMsg, "\n\nDetails: %s", errMsg->estrDetails);
	}

	if(!sEnableErrorThreading && errMsg->bSent)
	{
		// jdrago 2010/01/28 - This data is always going to be wrong if we're threading errors, so I disabled it if so

		if(bSentError && errorTrackerGetUniqueID() > 0)
		{
			estrConcatf(&errMsg->estrFormattedMsg, "\nUnique Error ID: %d", errorTrackerGetUniqueID());
			if (errMsg->errorType != ERROR_FATAL)
				estrConcatf(&errMsg->estrFormattedMsg, "\nhttp://errortracker/detail?id=%d", errorTrackerGetUniqueID());
		}
		else
		{
			char *errorMsg = errorTrackerGetErrorMessage();
			if(*errorMsg)
			{
				estrConcatf(&errMsg->estrFormattedMsg, "\nETResponse: %s", errorMsg);
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return errMsg->estrFormattedMsg;
}


bool DelayExpired( const char *filename, int days )
{
	__time32_t changedTime, systemTime, daysToSeconds;

	if (days == -1)
		return false;

	// this returns the number of seconds elapsed since midnight of january 1, 1970,
	// the single most useful measure of time in the history of the universe...
	changedTime = fileLastChanged( filename );
	systemTime = _time32(NULL);

	systemTime -= changedTime;

	// the number of seconds in the delay
	daysToSeconds = (__time32_t)(days * 86400);

	if ( systemTime >= daysToSeconds )
		return true;

	return false;
}


bool DelayMDYExpired( int curMonth, int curDay, int curYear, int days )
{	
	SYSTEMTIME specifiedTime = {0};
	U32 curTime, specTime;

	if (days == -1)
		return false;

	specifiedTime.wYear = ( curYear < 100 ? curYear + 2000 : curYear);
	specifiedTime.wMonth = curMonth;
	specifiedTime.wDay = curDay;

	curTime = timeSecondsSince2000();
	specTime = timerSecondsSince2000FromSystemTime(&specifiedTime) + ((U32)days) * 86400;

	return curTime >= specTime;
}

//A little bit of dementia to allow Errorf from different parts of the code
//to get their own number of displays 
StashTable dialogsDone;
static StashTable spReportedErrorsTable = NULL;

void ErrorfResetCounts(void)
{
	stashTableClear(dialogsDone);
}

bool errorIsFromQuickloadMessages(const char *msg)
{
	if (quickLoadMessages &&
		(strstri(msg, "display name") ||
		strstri(msg, "displayName") ||
		(strstri(msg, "exist") || strstri(msg, "invalid") || strstri(msg, "unknown") || strstri(msg, "found")) && strstri(msg, "message")
		))
		return true;
	return false;
}

void DisplayErrorMessage( ErrorMessage *errMsg )
{
	int hash;
	StashElement element;
	char **str;
	STATIC_THREAD_ALLOC(str);

	PERFINFO_AUTO_START_FUNC();

	if (gbIgnoreAllErrors || errorIsInGimmeGet() || errorIsFromQuickloadMessages(errMsg->estrMsg))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!gbFolderCacheModeChosen)
	{
		assertmsgf(0, "Premature Errorf: Someone (%s: %d) tried to generate Errorf \"%s\" before the file system is ready, mostly like in an AUTO_RUN",
			errMsg->file, errMsg->line, errMsg->estrMsg);
	}

	if (!errMsg->bReport)
	{
		errMsg->bForceShow = true; // Always show Alertf
	}

	if (errMsg->filename && errMsg->filename[0])
	{
		errorFindAuthor(errMsg);
	}

	EnterCriticalSection(&errorLookupCS);
	if (errMsg->bReport)
	{
		char *pCombinedString = NULL;
		if (!spReportedErrorsTable)
			spReportedErrorsTable = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);

		estrStackCreate(&pCombinedString);
		estrPrintf(&pCombinedString, "%s%s", errMsg->estrMsg ? errMsg->estrMsg : "", errMsg->filename ? errMsg->filename : "");


		if (stashFindPointer(spReportedErrorsTable, pCombinedString, NULL))
			errMsg->bReport = false;
		else
			stashAddPointer(spReportedErrorsTable, pCombinedString, NULL, false);

		estrDestroy(&pCombinedString);
	}

	if (!errMsg->file || (isDevelopmentMode() && errMsg->bForceShow))
	{
		// Self errors in dev mode are always shown
		errMsg->errorCount = 0;
	}
	else
	{
		// This is used by FolderCache's ErrorfCallback
		if (!dialogsDone)
		{
			dialogsDone = stashTableCreateInt(10);
		}

		hash = hashString(errMsg->file, false) + errMsg->line;

		if (stashIntFindElement(dialogsDone, hash, &element))
		{
			errMsg->errorCount = stashElementGetInt(element) + 1;
			stashElementSetInt(element,errMsg->errorCount);
		}
		else
		{
			errMsg->errorCount = 1;
			stashIntAddInt(dialogsDone, hash, 1, true);
		}
	}
	LeaveCriticalSection(&errorLookupCS);

	// Only display each error (and send to ET) once
	if (errMsg->bReport || errMsg->bForceShow || sbAlwaysReportDespiteDuplication)
		ErrorfCallCallback(errMsg);

	PERFINFO_AUTO_STOP();
}

//static strings, extensions only, 0 and 1 are a pair, 2 and 3, etc.
//
//each pair of extensions means "if you're trying to blame a file, and it doesn't exist, and has one of these
//extensions, blame the other file instead
static char **sppInterchangeableExtensionsForErrorReporting = NULL;

void ErrorAddInterchangeableFilenameExtensionPair(char *pStr1, char *pStr2)
{
	eaPush(&sppInterchangeableExtensionsForErrorReporting, pStr1);
	eaPush(&sppInterchangeableExtensionsForErrorReporting, pStr2);
}

static void FixupFileNameForInterchangeableExtensionPairs(ErrorMessage *errMsg)
{
	char tempName[CRYPTIC_MAX_PATH];
	int i;

	if (!sppInterchangeableExtensionsForErrorReporting)
	{
		return;
	}

	if (!errMsg->filename || !errMsg->filename[0])
	{
		return;
	}


	if (fileExists(errMsg->filename))
	{
		return;
	}

	for (i = 0; i < eaSize(&sppInterchangeableExtensionsForErrorReporting); i++)
	{
		if (strEndsWith(errMsg->filename, sppInterchangeableExtensionsForErrorReporting[i]))
		{
			char *pOtherExtension = sppInterchangeableExtensionsForErrorReporting[i ^ 1];

			changeFileExt(errMsg->filename, pOtherExtension, tempName);

			if (fileExists(tempName))
			{
				errMsg->filename = allocAddFilename(tempName);

				estrInsertf(&errMsg->estrMsg, 0, "(This error was reported for a %s file, but may actually come from a %s file) ",
					sppInterchangeableExtensionsForErrorReporting[i], pOtherExtension);

				return;
			}
		}
	}
}

void errorFindAuthor(ErrorMessage *errMsg)
{
	bool mine = false;
	const char *user;

	PERFINFO_AUTO_START_FUNC();

	//filenames beginning with src/data or src\data are actualy resources that are compiled into EXEs, and then
	//parsed in RAM
	if (strStartsWith(errMsg->filename, "src/data") || strStartsWith(errMsg->filename, "src\\data"))
	{
		errMsg->author = strdup("Internal resource");
		return;
	}

	//For complicated reasons, some times objects are assigned filenames that don't actually exist. For instance,
	//lots of stuff in world libs loads an object out of a .modelnames file and then tells it that it was actually
	//loaded from a .rootmods file. Then when an error occurs, we want to blame it on the .modelnames file rather
	//than the .rootmods file, IF the .rootmods file doesn't actually exist, so as to not
	//get "not in database" when we really should be able to blame whoever modifed the .modelnames file
	FixupFileNameForInterchangeableExtensionPairs(errMsg);

	user = g_disableLastAuthor?"UNKNOWN":gimmeDLLQueryLastAuthor(errMsg->filename);

	if(strstri(user, "do not have"))
	{
		errMsg->bNotLatestData = true;

		//CBs report not latest datas, just to avoid the mysterious "no errors found" condition
		if (!g_isContinuousBuilder)
		{
			errMsg->bReport = false;
		}
	}

	if(strstri(user, "you have this file checked out") || strstri(user, "not in database"))
	{
		errMsg->bCheckedOut = true;

		//Either of these types of errors on a CB is in fact something worrisome and weird
		if (!g_isContinuousBuilder)
		{
			errMsg->bReport = false;
		}
	}

	errorLogFileHasError(errMsg->filename);
	if (gimmeDLLQueryIsFileMine(errMsg->filename))
	{
		mine = true;
		errMsg->bForceShow = true;
	}

	errMsg->author = strdup(user);

	if (errMsg->curday || errMsg->curmon || errMsg->curyear)
	{
		//retroactive errors when there is no gimme depend solely on date, ignore all author stuff entirely
		if (!gimmeDLLQueryExists())
		{
			errMsg->bReport = errMsg->bForceShow = errMsg->bRelevant = DelayMDYExpired(errMsg->curmon, errMsg->curday, errMsg->curyear, errMsg->days);
		}
		else
		{
			if (g_isContinuousBuilder || gbMakeBinsAndExit)
			{
				errMsg->bRelevant = DelayMDYExpired(errMsg->curmon, errMsg->curday, errMsg->curyear, errMsg->days);
				if (!errMsg->bRelevant)
				{
					errMsg->bReport = false;
				}
			}
			else
			{
				errMsg->bRelevant = UserIsInGroup(errMsg->group) || DelayMDYExpired(errMsg->curmon, errMsg->curday, errMsg->curyear, errMsg->days) || mine;
				if (!errMsg->bRelevant)
					errMsg->bReport = false;
			}
		}
	}
	else if (errMsg->group )
	{
		errMsg->bRelevant = UserIsInGroup(errMsg->group) || DelayExpired(errMsg->filename, errMsg->days) || mine;
		if (!errMsg->bRelevant && !(g_isContinuousBuilder || gbMakeBinsAndExit))
			errMsg->bReport = false;
	}

	PERFINFO_AUTO_STOP();
}

void ErrorfCallCallback(ErrorMessage *errMsg)
{
	if (errMsg->errorType == ERROR_FATAL)
	{	
		errMsg->bForceShow = true;
		//if(fatalErrorfCallback) {
		//	fatalErrorfCallback(errMsg);
		//} else {
		//	defaultFatalErrorCallback(errMsg);
		//}
		if(fatalErrorfCallback.cb)
			fatalErrorfCallback.cb(errMsg, fatalErrorfCallback.userdata);
	}

	if(errorfCallbackStack_size) {
		PERFINFO_AUTO_START("callback", 1);
		assert(errorfCallbackStack_size > 0 && errorfCallbackStack_size <= ARRAY_SIZE(errorfCallbackStack));
		if(errorfCallbackStack[errorfCallbackStack_size-1].cb)
			errorfCallbackStack[errorfCallbackStack_size-1].cb(errMsg, errorfCallbackStack[errorfCallbackStack_size-1].userdata);
		PERFINFO_AUTO_STOP();
	} else {
		defaultErrorCallback(errMsg, NULL);
	}

}

//----------------------------------------------------------------
// FatalErrorf
//----------------------------------------------------------------
#undef FatalErrorf
void FatalErrorf(char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
#if _PS3
    vprintf(fmt, ap);
#endif
	errMsg =  BuildErrorMessage(ERROR_FATAL, NULL, 0, NULL, NULL, 0, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

#undef FatalErrorFilenamef
void FatalErrorFilenamef(const char *filename, char const *fmt, ...) 
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_FATAL, NULL, 0, filename, NULL, 0, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

void FatalErrorv(char const *fmt, va_list ap)
{
	ErrorMessage *errMsg;

#if _PS3
    vprintf(fmt, ap);
#endif
	errMsg =  BuildErrorMessage(ERROR_FATAL, NULL, 0, NULL, NULL, 0, 0, 0, 0, fmt, ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

//----------------------------------------------------------------
// Errorf
//----------------------------------------------------------------

static CRITICAL_SECTION deferredErrorfCS;

static void enterDeferredErrorfCriticalSection()
{
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&deferredErrorfCS);
	ATOMIC_INIT_END;
	EnterCriticalSection(&deferredErrorfCS);
}

static void leaveDeferredErrorfCriticalSection()
{
	LeaveCriticalSection(&deferredErrorfCS);
}

void InvalidDataErrorfInternal(const char *file, int line, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_INVALID, file, line, NULL, NULL, 0, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

void DeprecatedErrorfInternal(const char *file, int line, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_DEPRECATED, file, line, NULL, NULL, 0, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void forceStackWalk(void)
{
	ErrorfForceCallstack("forceStackWalk: Test callstack - not an error");
}

void ErrorfCallstackInternal(bool bReport, const char *file, int line, bool bDoNotSendCallstack, char const *fmt, ...)
{
	va_list ap;

	PERFINFO_AUTO_START_FUNC();
	va_start(ap, fmt);
	ErrorvCallstackInternal(bReport, file, line, bDoNotSendCallstack, fmt, ap);
	va_end(ap);
	PERFINFO_AUTO_STOP();
}

void ErrorfInternal(bool bReport, const char *file, int line, char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ErrorvInternal(bReport, file, line, fmt, ap);
	va_end(ap);
}

void ErrorDeferredfInternal(const char *file, int line, FORMAT_STR char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);	
	enterDeferredErrorfCriticalSection();
	eaPush(&g_deferred_errors, BuildErrorMessage(ERROR_NORMAL, file, line, NULL, NULL, 0, 0, 0, 0, fmt, ap));
	leaveDeferredErrorfCriticalSection();
	va_end(ap);
}


void ErrorvCallstackInternal(bool bReport, const char *file, int line, bool bDoNotSendCallstack, char const *fmt, va_list ap)
{
	ErrorMessage *errMsg = BuildErrorMessage(ERROR_NORMAL, file, line, NULL, NULL, 0, 0, 0, 0, fmt, ap);

	errMsg->bReport = bReport;
	errMsg->bForceSendStack = !bDoNotSendCallstack;
	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

void ErrorvInternal(bool bReport, const char *file, int line, char const *fmt, va_list ap)
{
	if (gbCavemanMode)
	{
	    vprintf(fmt, ap);
		return;
	}


#if _PS3
    printf("\n");
    vprintf(fmt, ap);
#endif
	PERFINFO_AUTO_START_FUNC();
	ErrorvCallstackInternal(bReport, file, line, gbNoCallStacksOnErrors, fmt, ap);
	PERFINFO_AUTO_STOP();
}





void ErrorFilenamevInternal_Blocking(const char *file, int line, const char *filename, char const *fmt, va_list ap) 
{
	ErrorMessage *errMsg;
	PERFINFO_AUTO_START_FUNC();
	errMsg = BuildErrorMessage(ERROR_NORMAL, file, line, filename, NULL, 0, 0, 0, 0, fmt, ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
	PERFINFO_AUTO_STOP();
}

void ErrorFilenamefInternal_Blocking(const char *file, int line, const char *filename, char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ErrorFilenamevInternal_Blocking(file, line, filename, fmt, ap);
	va_end(ap);
}

void ErrorFilenamevInternal(const char *file, int line, const char *filename, char const *fmt, va_list ap) 
{
	if (sbAllErrorFilenamesNonBlocking && !g_isContinuousBuilder)
	{
		ErrorFilenamev_NonBlockingInternal(file, line, filename, fmt, ap);
	}
	else
	{
		ErrorFilenamevInternal_Blocking(file, line, filename, fmt, ap);
	}
}


void ErrorFilenamefInternal(const char *file, int line, const char *filename, char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (sbAllErrorFilenamesNonBlocking && !g_isContinuousBuilder)
	{
		ErrorFilenamev_NonBlockingInternal(file, line, filename, fmt, ap);
	}
	else
	{
		ErrorFilenamevInternal_Blocking(file, line, filename, fmt, ap);
	}
	va_end(ap);
}



void InvalidDataErrorFilenamefInternal(const char *file, int line, const char *filename, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_INVALID, file, line, filename, NULL, 0, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);	
}

void DeprecatedErrorFilenamefInternal(const char *file, int line, const char *filename, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_DEPRECATED, file, line, filename, NULL, 0, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);	
}

void ErrorFilenameTwofInternal(const char *file, int line, const char *filename1, const char *filename2, char const *fmt, ...)
{
	va_list ap;
	char **str;

	const char *newest_file;

	char user1[128];
	char user2[128];

	STATIC_THREAD_ALLOC(str);

	if (gbIgnoreAllErrors || errorIsInGimmeGet())
	{
		return;
	}

	strcpy(user1, g_disableLastAuthor||!filename1?"UNKNOWN":gimmeDLLQueryLastAuthor(filename1));
	strcpy(user2, g_disableLastAuthor||!filename2?"UNKNOWN":gimmeDLLQueryLastAuthor(filename2));

	if (!filename2 || fileNewer(filename2, filename1))
		newest_file = filename1;
	else {
		if (fileNewer(filename1, filename2)) {
			newest_file = filename2;
		} else {
			// Identical timestamps, use alphabetical just to be consistent
			if (stricmp(filename1, filename2)>0) {
				newest_file = filename1;
			} else {
				newest_file = filename2;
			}
		}
	}
	// Blame files which are not up to date
	if (strstri(user1, "do not have"))
		newest_file = filename1;
	if (strstri(user2, "do not have"))
		newest_file = filename2;
	// Always give blame to locally modified files
	if (filename1 && (!g_disableLastAuthor && gimmeDLLQueryIsFileLockedByMeOrNew(filename1)))
		newest_file = filename1;
	if (filename2 && (!g_disableLastAuthor && gimmeDLLQueryIsFileLockedByMeOrNew(filename2)))
		newest_file = filename2; // Always give blame to locally modified files

	if (filename1) errorLogFileHasError(filename1);
	if (filename2) errorLogFileHasError(filename2);

	if (!newest_file) newest_file = "UNKNOWN FILE";

	va_start(ap, fmt);
	estrClear(str);
	estrConcatfv(str, fmt, ap);
	ErrorFilenamefInternal(file, line, newest_file, "%s found in: \n%s (%s) and \n%s (%s)", *str, filename1, user1, filename2, user2);
	va_end(ap);
}

void ErrorFilenameDupInternal(const char *file, int line, const char *filename1, const char *filename2, const char *key, const char *type)
{
	ErrorFilenameTwofInternal(file, line, filename1, filename2, "Duplicate %s %s", type, key);
}


void ErrorFilenameDeferredfInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);	
	enterDeferredErrorfCriticalSection();
	eaPush(&g_deferred_errors, BuildErrorMessage(ERROR_NORMAL, file, line, filename, NULL, 0, 0, 0, 0, fmt, ap));
	leaveDeferredErrorfCriticalSection();
	va_end(ap);
}

void ErrorFilenameGroupDeferredfInternal(const char *file, int line, const char *filename, const char *group, FORMAT_STR char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);	
	enterDeferredErrorfCriticalSection();
	eaPush(&g_deferred_errors, BuildErrorMessage(ERROR_NORMAL, file, line, filename, group, 0, 0, 0, 0, fmt, ap));
	leaveDeferredErrorfCriticalSection();
	va_end(ap);
}

void ErrorFilenameGroupRetroactiveDeferredfInternal(const char *file, int line, const char *filename, const char *group, int days, int curmon, int curday, int curyear, FORMAT_STR char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	enterDeferredErrorfCriticalSection();
	eaPush(&g_deferred_errors, BuildErrorMessage(ERROR_NORMAL, file, line, filename, group, days, curmon, curday, curyear, fmt, ap));
	leaveDeferredErrorfCriticalSection();
	va_end(ap);
}

void ErrorOncePerFrame(void)
{
	int i;
	int size; 
	ErrorMessage **deferred_error_copy;
	PERFINFO_AUTO_START_FUNC();

	UpdateDeferredErrorOrAlerts();
	enterDeferredErrorfCriticalSection();
	deferred_error_copy = g_deferred_errors;
	g_deferred_errors = NULL;
	leaveDeferredErrorfCriticalSection();

	size = eaSize(&deferred_error_copy);

	if(!size)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	
	

	for(i=0; i<size; i++)
	{
		ErrorMessage *deferr = deferred_error_copy[i];
		if (!deferr)
		{
			continue;
		}

		DisplayErrorMessage(deferr);
	}

	eaClearEx(&deferred_error_copy, DestroyErrorMessage);

	PERFINFO_AUTO_STOP();
}

#if !PLATFORM_CONSOLE

// Fill the given string array with all of the users
void FillAllUsers(char ***users)
{
	int i;
	const char *const *all_users = NULL;
	if (!gimmeDLLQueryExists())
		return;
	else
		all_users = gimmeDLLQueryFullUserList();

	for(i = 0; i < eaSize(&all_users); i++)
		eaPush(users, strdup(all_users[i]));
}

// Fill the given string array with all of the unique groups
void FillAllGroups(char ***groups)
{
	int i;
	const char *const *all_groups = NULL;
	if (!gimmeDLLQueryExists())
		return;
	else
		all_groups = gimmeDLLQueryFullGroupList();

	for(i = 0; i < eaSize(&all_groups); i++)
		eaPush(groups, strdup(all_groups[i]));
}

// is the current user in the specified group(s)?
int UserIsInGroup(const char *group)
{
	return UserIsInGroupEx(group, false);
}

int UserIsInGroupEx(const char *group, bool disableSpecialGroups)
{
	static const char *const *current_user_groups = NULL;

	int i = 0, j = 0, num_groups = 0, len = (int)strlen(group);
	char groups[256][128];

	if (g_disableLastAuthor)
		return 1;

	// no groups
	if ( !len )
		return false;

	// take the group string and split it up into the groups array
	while ( i < len )
	{
		if ( group[i] == ' ' || group[i] == '|' || group[i] == ',' )
		{
			groups[num_groups][j] = 0;
			++num_groups;
			while( group[i] != ' ' && group[i] != '|' && group[i] != ',' && i++ < len );
		}
		else
			groups[num_groups][j] = group[i];

		++j, ++i;
	}

	groups[num_groups++][j] = 0;

	if (!disableSpecialGroups)
		strcpy( groups[num_groups++], "All" );

	if(!current_user_groups)
	{
		if(!isDevelopmentMode())
		{
			char **ret = NULL;
			eaCreate(&ret);
			current_user_groups = ret;
		}
		else if(!gimmeDLLQueryExists())
		{
			char **ret = NULL;
			eaCreate(&ret);
			eaPush(&ret, strdup("All"));
			current_user_groups = ret;
		}
		else
			current_user_groups = gimmeDLLQueryGroupList();
	}

	// see if the groups for the error match the groups of the user
	for ( i = 0; i < eaSize(&current_user_groups); ++i )
	{
		for ( j = 0; j < num_groups; ++j )
		{
			if ( stricmp(current_user_groups[i], groups[j]) == 0 )
			{
				return true;
			}
		}
	}

	return false;
}

int IsGroupName(const char *group)
{
	static const char *const *all_groups = NULL;

	int i = 0;

	if(!group || !group[0])
		return false;

	if(!all_groups)
	{
		if(!isDevelopmentMode())
		{
			char **ret = NULL;
			eaCreate(&ret);
			all_groups = ret;
		}
		else if(!gimmeDLLQueryExists())
		{
			char **ret = NULL;
			eaCreate(&ret);
			eaPush(&ret, strdup("All"));
			all_groups = ret;
		}
		else
			all_groups = gimmeDLLQueryFullGroupList();
	}

	for(i=0; i<eaSize(&all_groups); i++)
		if(!stricmp(all_groups[i], group))
			return true;

	return false;
}

#else 

// This needs to be implemented eventually

int IsGroupName(const char *group)
{
	return true;
}

int UserIsInGroup( const char *group )
{
	return true;
}

int UserIsInGroupEx(const char *group, bool disableSpecialGroups)
{
	return true;
}

#endif

void ErrorFilenameGroupfInternal(const char *file, int line, const char *filename, const char *group, int days, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;
	
	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_NORMAL, file, line, filename, group, days, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

void ErrorFilenameGroupRetroactivefInternal(const char *file, int line, const char *filename, const char *group, int days, 
	int curMonth, int curDay, int curYear, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, fmt);
	errMsg =  BuildErrorMessage(ERROR_NORMAL, file, line, filename, group, days, curMonth, curDay, curYear, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

void ErrorFilenameGroupInvalidfInternal(const char *file, int line, const char *filename, const char *group, int days, char const *fmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;
	
	va_start(ap, fmt);
	errMsg = BuildErrorMessage(ERROR_INVALID, file, line, filename, group, days, 0, 0, 0, fmt, ap);
	va_end(ap);

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}

// Verbose printing

static int verbose_level,temp_verbose_level;


typedef struct LoadPerf {
	const char*		name;
	PERFINFO_TYPE*	pi;
} LoadPerf;

#define MAX_DEPTH	10
typedef struct LoadStartEndData
{
	int g_loadstart_depth;
	int depth;
	struct {
		int load_timer;
		size_t scratch_stack_size;
	} load_timer_data[MAX_DEPTH];
	int untimed_load_timer;
	int untimed_last_was_start;
	bool untimed_recursive;
	bool untimed_should_report;
	F32 untimed_elapsed;
	bool suppress_printing;
	StashTable stLoadPerf;
	voidVoidFunc loadend_callback;
	int load_line_pos;
	COORD expectedCursorPos;
} LoadStartEndData;

static LoadStartEndData *getThreadLoadStartEndData(void)
{
	LoadStartEndData *ret;
	STATIC_THREAD_ALLOC(ret);
	return ret;
}

static void loadLineCheckStart(void)
{
	extern HANDLE getConsoleHandle(void);
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
	if (cbi.dwCursorPosition.X != lsed->expectedCursorPos.X ||
		cbi.dwCursorPosition.Y != lsed->expectedCursorPos.Y)
	{
		lsed->load_line_pos = 0;
		if (cbi.dwCursorPosition.X != 0)
		{
			printf("\n");
		}
	}
}

static void loadLineCheckEnd(void)
{
	extern HANDLE getConsoleHandle(void);
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	CONSOLE_SCREEN_BUFFER_INFO cbi;
	GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
	lsed->expectedCursorPos = cbi.dwCursorPosition;
}

static void loadLineClear(void)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	if (lsed->load_line_pos)
	{
		lsed->load_line_pos = 0;
		printf("\n");
	}
}

#undef verbose_printf
void verbose_printf(char const *fmt, ...)
{
	char str[1000];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	if (errorGetVerboseLevel())
	{
		LoadStartEndData *lsed = getThreadLoadStartEndData();
		printfEnterCS();
		loadLineCheckStart();
		loadLineClear();
		printfColor(COLOR_BRIGHT, "%*s%s", lsed->g_loadstart_depth*2, "", str);
		loadLineCheckEnd();
		printfLeaveCS();
	} else {
		memlog_printf(NULL, "%s", str);
	}
}

AUTO_CMD_INT(verbose_level, verbose) ACMD_CATEGORY(debug) ACMD_ACCESSLEVEL(9) ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0) ACMD_CMDLINE;

void errorSetVerboseLevel(int v)
{
	verbose_level = v;
}

void errorSetTempVerboseLevel(int v)
{
	temp_verbose_level = v;
}

int errorGetVerboseLevel(void)
{
	return MAX(verbose_level,temp_verbose_level);
}









//stuff for XML timing. Up here so it can be inlined by loadstart stuff
static char *spXMLTiming_FileName = NULL;
static char *spXMLTiming_CurString = NULL;
static int siXMLTimingDepth = 0;
U64 iLastEndTime = 0; //we track when the last task ended. If it was more than a threshold ago, we add an "unknown" block



typedef struct XMLTimingElement
{
	U64 iStartTime; //timerCpuTicks64
	int iStringOffset; //the offset in the string where we will come back and insert our time string once the block completes
} XMLTimingElement;

static XMLTimingElement sXMLElements[MAX_DEPTH];
static char sXMLTimingIndentString[MAX_DEPTH + 1] = "";

static FILE *spOutFile = NULL;

static S64 sCPUSpeed = 0;


void MaybeAddUnnamedBlock(U64 iTime)
{
}

void XMLTiming_BeginBlock(char *pBlockName)
{
	char *pEscapedString = NULL;
	XMLTimingElement *pElement;
	U64 iCurTime;

	if (siXMLTimingDepth == 1 && stricmp(pBlockName, "UNACCOUNTED FOR") == 0)
	{
		int iBrk = 0;
	}

	if (!spXMLTiming_FileName)
	{
		return;
	}

	if (siXMLTimingDepth >= MAX_DEPTH)
	{
		siXMLTimingDepth++;
		return;
	}

	iCurTime = timerCpuTicks64();

	MaybeAddUnnamedBlock(iCurTime);

	sXMLTimingIndentString[siXMLTimingDepth] = ' ';
	pElement = &sXMLElements[siXMLTimingDepth++];
	

	estrStackCreate(&pEscapedString);

	estrCopyWithHTMLEscapingSafe(&pEscapedString, pBlockName, false);

	estrReplaceMultipleChars(&pEscapedString, "\n\r", ' ');

	estrConcatf(&spXMLTiming_CurString, "%s<task name=\"%s\" >\n", sXMLTimingIndentString, pEscapedString);
	
	pElement->iStartTime = timerCpuTicks64();
	pElement->iStringOffset = estrLength(&spXMLTiming_CurString)-2; //skip backwards over the newline and the >

	estrDestroy(&pEscapedString);
}

float GetSecsBetweenCpuTicksTimes(U64 iStartTime, U64 iEndTime)
{
	//binary wrap around here is fine
	return (float)((double)(iEndTime - iStartTime) / (double)sCPUSpeed);
}	


void XMLTiming_EndBlock(void)
{
	U64 iCurTime;
	char *pTimeString = NULL;
	float fSecsDuration;

	XMLTimingElement *pElement;
	
	if (!spXMLTiming_FileName)
	{
		return;
	}


	if (siXMLTimingDepth < 1)
	{
		return;
	}

	siXMLTimingDepth--;
	if (siXMLTimingDepth >= MAX_DEPTH)
	{
		return;
	}

	iCurTime = timerCpuTicks64();
	MaybeAddUnnamedBlock(iCurTime);

	estrStackCreate(&pTimeString);

	pElement = &sXMLElements[siXMLTimingDepth];

	fSecsDuration = GetSecsBetweenCpuTicksTimes(pElement->iStartTime, iCurTime);
	
	if (fSecsDuration < 1)
	{
		estrPrintf(&pTimeString, "time = \"%f\" prettytime = \"&lt; 1 second\"", fSecsDuration);
	}
	else
	{
		char *pPrettyTimeString = NULL;
		estrStackCreate(&pPrettyTimeString);
		timeSecondsDurationToPrettyEString((int)fSecsDuration, &pPrettyTimeString);
		estrPrintf(&pTimeString, "time = \"%f\" prettytime = \"%s\"", fSecsDuration, pPrettyTimeString);
		estrDestroy(&pPrettyTimeString);
	}

	estrInsert(&spXMLTiming_CurString, pElement->iStringOffset, pTimeString, estrLength(&pTimeString));

	estrConcatf(&spXMLTiming_CurString, "%s</task>\n", sXMLTimingIndentString);

	sXMLTimingIndentString[siXMLTimingDepth] = 0;

	estrDestroy(&pTimeString);
}



AUTO_COMMAND ACMD_HIDE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void XMLTiming(char *pFileName)
{
	spXMLTiming_FileName = strdup(pFileName);
	sCPUSpeed = timerCpuSpeed64();

	XMLTiming_BeginBlock("Root");
}

typedef struct
{
	char *pFileName;
	int iSeconds;
} XMLFilteringRequest;

static XMLFilteringRequest **sppXMLFilteringRequests = NULL;

//write out a second file with only times > a certain number of seconds
AUTO_COMMAND ACMD_HIDE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void XMLTimingFiltered(char *pFileName, int iSeconds)
{
	XMLFilteringRequest *pRequest = calloc(sizeof(XMLFilteringRequest), 1);
	pRequest->pFileName = strdup(pFileName);
	pRequest->iSeconds = iSeconds;

	eaPush(&sppXMLFilteringRequests, pRequest);
}

#define ADVANCE_TO_EOL					\
while (*pReadHead && *pReadHead != '\n')\
			{							\
				pReadHead++;			\
			}							\
			if (*pReadHead == '\n')		\
			{							\
				pReadHead++;			\
			}							\


//oh, the hackiness of this XML parsing. Please don't tell anyone
void WriteFilteredFileFromXMLString(char *pXMLString, char *pOutFileName, int iSeconds)
{
	char *pReadHead;
	char *pName;
	char *pTime;
	int iSpaceCount;
	char *pTemp;
	float fTime;

	FILE *pOutFile = fopen(pOutFileName, "wt");
	if (!pOutFile)
	{
		return;
	}

	pReadHead = pXMLString;

	while (*pReadHead)
	{
		NameValuePair **ppPairs = NULL;

		iSpaceCount = 0;
		while (*pReadHead == ' ')
		{
			pReadHead++;
			iSpaceCount++;
		}

		if (*pReadHead != '<')
		{
			break;
		}

		pReadHead++;

		if (*pReadHead == '/')
		{
			ADVANCE_TO_EOL;
			continue;
		}

		pReadHead += 5;
		pTemp = strchr(pReadHead, '>');

		if (!pTemp)
		{
			break;
		}

		*pTemp = 0;

		GetNameValuePairsFromString(pReadHead, &ppPairs, "=");

		*pTemp = '>';

		ADVANCE_TO_EOL;

		pName = GetValueFromNameValuePairs(&ppPairs, "name");
		pTime = GetValueFromNameValuePairs(&ppPairs, "time");


		if (pName && pTime && ((fTime = atof(pTime)) > iSeconds))
		{
			int i;
			int iTime = (int)fTime;

			fprintf(pOutFile, "%3d:%02d", iTime / 60, iTime % 60);

			for (i=0; i < iSpaceCount + 1; i++)
			{
				fprintf(pOutFile, " ");
			}

			fprintf(pOutFile, "%s\n", pName);

		}

		eaDestroyStruct(&ppPairs, parse_NameValuePair);
	}

	fclose(pOutFile);
}

void errorShutdown(void)
{
	while (siXMLTimingDepth)
	{
		XMLTiming_EndBlock();
	}

	if (spXMLTiming_FileName)
	{
		FILE *pOutFile = fopen(spXMLTiming_FileName, "wt" );
		int i;
		
		for (i=0; i < eaSize(&sppXMLFilteringRequests); i++)
		{
			WriteFilteredFileFromXMLString(spXMLTiming_CurString, sppXMLFilteringRequests[i]->pFileName, sppXMLFilteringRequests[i]->iSeconds);
		}

		if (pOutFile)
		{
			fprintf(pOutFile, "%s", spXMLTiming_CurString);
			estrClear(&spXMLTiming_CurString);
			fclose(pOutFile);
		}
	}

#if _PS3
    printf("errorShutdown\n");
#endif
}












void loadstart_report_unaccounted(bool shouldReport)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	lsed->untimed_should_report = shouldReport;
}

void loadstart_suppress_printing(bool shouldSuppress)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	lsed->suppress_printing = shouldSuppress;
}

static void loadstart_untimed(bool isStart)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	if (lsed->untimed_recursive)
		return;
	lsed->untimed_recursive = true;

	if (!lsed->untimed_load_timer)
		lsed->untimed_load_timer = timerAlloc();
	if (lsed->untimed_last_was_start && !isStart) {
		// Nothing bad
	} else {
		lsed->untimed_elapsed = timerElapsed(lsed->untimed_load_timer);
		if (lsed->untimed_elapsed > 0.08 && lsed->untimed_should_report) {
			extern const char *g_lastTaskName;
			printfEnterCS();
			loadLineCheckStart();
			if (g_lastTaskName && lsed->depth==0) {
				loadstart_printf("UNACCOUNTED FOR (last task was %s)", g_lastTaskName);
			} else {
				loadstart_printf("UNACCOUNTED FOR");
			}
			loadend_printf("");
			loadLineCheckEnd();
			printfLeaveCS();
		}
	}
	lsed->untimed_last_was_start = isStart;
	timerStart(lsed->untimed_load_timer);
	lsed->untimed_recursive = false;
}

static S32 use_load_perfinfo_timers;
AUTO_CMD_INT(use_load_perfinfo_timers, use_load_perfinfo_timers) ACMD_ACCESSLEVEL(1) ACMD_CMDLINEORPUBLIC;

#undef loadstart_printf
void loadstart_printf(const char* fmt, ...)
{
	va_list args;
	char buf[1024];
	LoadStartEndData *lsed = getThreadLoadStartEndData();

	if(lsed->suppress_printing)
		return;

	loadstart_untimed(true);

	devassert(lsed->g_loadstart_depth < 15);

	if (!lsed->load_timer_data[lsed->depth].load_timer)
		lsed->load_timer_data[lsed->depth].load_timer = timerAlloc();
	lsed->load_timer_data[lsed->depth].scratch_stack_size = ScratchPerThreadOutstandingAllocSize();
	timerStart(lsed->load_timer_data[lsed->depth].load_timer);
	printfEnterCS();
	loadLineCheckStart();
	loadLineClear();
	lsed->load_line_pos += lsed->depth*2;

	va_start(args, fmt);
	lsed->load_line_pos += vsprintf(buf, fmt, args);
	va_end(args);

	if(	use_load_perfinfo_timers &&
		PERFINFO_RUN_CONDITIONS)
	{
		LoadPerf* p;
		
		if(!lsed->stLoadPerf){
			lsed->stLoadPerf = stashTableCreateWithStringKeys(100, StashDefault);
		}

		if(!stashFindPointer(lsed->stLoadPerf, buf, &p)){
			const char* prefix = "Load: ";
			p = callocStruct(LoadPerf);
			p->name = strdupf("%s%s", prefix, buf);
			stashAddPointer(lsed->stLoadPerf, p->name + strlen(prefix), p, 0);
		}
		
		PERFINFO_AUTO_START_STATIC(p->name, &p->pi, 1);
	}

	printf("%*s", lsed->load_line_pos, buf);
	fflush(fileGetStdout());
	loadLineCheckEnd();
	printfLeaveCS();
	lsed->g_loadstart_depth++;
	lsed->depth++;
	if (lsed->depth==MAX_DEPTH)
		lsed->depth--;

	XMLTiming_BeginBlock(buf);
}

#undef loadupdate_printf
void loadupdate_printf(const char* fmt, ...)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	char buf[1024];
	va_list args;

	if(lsed->suppress_printing)
		return;

	va_start(args, fmt);
	lsed->load_line_pos += vsprintf(buf, fmt, args);
	va_end(args);
	printfEnterCS();
	loadLineCheckStart();
	printf("%s", buf);
	fflush(fileGetStdout());
	loadLineCheckEnd();
	printfLeaveCS();
}

// Feel free to twiddle with these numbers to get a rainbow of timing colors =)
static struct {
	F32 elapsed;
	int color;
} scale[] = {
	{1.5, COLOR_GREEN | COLOR_BRIGHT},
//	{2.5, COLOR_GREEN},
//	{5.0, COLOR_GREEN | COLOR_RED},
	{10.0, COLOR_GREEN | COLOR_RED | COLOR_BRIGHT},
//	{7.0, COLOR_RED},
//	{0, COLOR_RED | COLOR_BRIGHT},
};

static char endfmt[16]="(%0.2f)\n";
void setLoadTimingPrecistion(int digits)
{
	sprintf(endfmt, "(%%0.%df)\n", digits);
}

void loadend_setCallback(voidVoidFunc callback)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	lsed->loadend_callback = callback;
}

static bool show_physical_untracked; // Warning - not thread-safe
// During each loading step, shows the amount of untracked physical memory and other metrics.
AUTO_CMD_INT(show_physical_untracked, show_physical_untracked) ACMD_CMDLINE;

#undef loadend_printf
void loadend_printf(const char* fmt, ...)
{
	int		i;
	va_list args;
	F32		elapsed;
	char buf[1024];
	bool bMismatched=false;
	LoadStartEndData *lsed = getThreadLoadStartEndData();

	if(lsed->suppress_printing)
		return;

	XMLTiming_EndBlock();

	loadstart_untimed(false);

	lsed->g_loadstart_depth--;
	lsed->depth--;
	if (lsed->depth<0)
	{
		lsed->depth=0;
		bMismatched = true;
	}

	printfEnterCS();
	loadLineCheckStart();
	i = 0;
	if (lsed->load_line_pos == 0) {
		lsed->load_line_pos += lsed->depth*2;
		i = lsed->load_line_pos;
	}

	va_start(args, fmt);
	lsed->load_line_pos += vsprintf(buf, fmt, args);
	va_end(args);

	printf("%*s%s%*s", i, "", buf, MAX(0, 70 - lsed->load_line_pos), "");

	if (lsed->untimed_recursive) {
		elapsed = lsed->untimed_elapsed;
	} else {
		elapsed = timerElapsed(lsed->load_timer_data[lsed->depth].load_timer);
		if(!bMismatched && lsed->load_timer_data[lsed->depth].scratch_stack_size != ScratchPerThreadOutstandingAllocSize())
		{
			// Generate an assert for the ScratchStack alloc that was leaked in the last loadstart/loadend pair.
			ScratchVerifyNoOutstanding();
		}
	}
	for (i=0; i<ARRAY_SIZE(scale)-1 && elapsed > scale[i].elapsed; i++);
	printfColor(scale[i].color, FORMAT_OK(endfmt),elapsed);
	if (show_physical_untracked) // Warning - not thread-safe
	{
		static U32 lastUntracked = 0;
		U32 untracked = memMonitorPhysicalUntracked();
		int delta = untracked - lastUntracked;
		if (ABS(delta) > 100) {
			printfColor(COLOR_RED|COLOR_GREEN, "       %d untracked bytes, delta: %d bytes\n", untracked, delta);
		}
		lastUntracked = untracked;
	}
	loadLineCheckEnd();
	printfLeaveCS();
	lsed->load_line_pos=0;
	if (lsed->depth > 1) {
		timerFree(lsed->load_timer_data[lsed->depth].load_timer);
		lsed->load_timer_data[lsed->depth].load_timer = 0;
	}

	if (lsed->loadend_callback)
		lsed->loadend_callback();

	timerStart(lsed->untimed_load_timer); // Because the printf takes some time.. ?
	
	if(use_load_perfinfo_timers){
		PERFINFO_AUTO_STOP(); // Matches the one in loadstart_printf
	}
}

int loadstart_depth(void)
{
	LoadStartEndData *lsed = getThreadLoadStartEndData();
	return lsed->g_loadstart_depth;
}

static bool errorLogStarted=false;
void errorLogStart(void)
{
	if (!gbDontLogErrors && !isProductionMode()) {
		char fullpath[CRYPTIC_MAX_PATH];
		errorLogStarted = true;
		sprintf(fullpath, "%s/%s.log", logGetDir(), "errorLogLastRun");
		if (fileSize(fullpath) > 200000) {
			// Reset the log every 200K
			fileForceRemove(fullpath);
		}
		filelog_printf("errorLogLastRun.log", "STARTING IGNORE:");
	}
}

void errorLogFileHasError(const char *file)
{
	if (errorLogStarted)
		filelog_printf("errorLogLastRun.log", "FILEERROR: %s", file);
}

void errorLogFileIsBeingReloaded(const char *file)
{
	if (errorLogStarted)
		filelog_printf("errorLogLastRun.log", "FILERELOAD: %s", file);
}


void AssertOrAlertEx(SA_PARAM_NN_STR const char *pKeyString, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;

	va_list ap;

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);
	
	printf("ASSERTORALERT: %s\n", pFullErrorString);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		assertmsg(0, pFullErrorString);
	}
	else
	{
		TriggerAlertEx(allocAddString(pKeyString), pFullErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, pFileName, iLineNum);
	}

	estrDestroy(&pFullErrorString);
}


void AssertOrProgrammerAlertEx(SA_PARAM_NN_STR const char *pKeyString, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;

	va_list ap;

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);
	
	printf("ASSERTORPROGRAMMERALERT: %s\n", pFullErrorString);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		assertmsg(0, pFullErrorString);
	}
	else
	{
		TriggerAlertEx(allocAddString(pKeyString), pFullErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER,
			0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, pFileName, iLineNum);
	}

	estrDestroy(&pFullErrorString);
}

void AssertOrAlertWarningEx(SA_PARAM_NN_STR const char *pKeyString, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;

	va_list ap;

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);
	
	printf("ASSERTORALERT: %s\n", pFullErrorString);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		assertmsg(0, pFullErrorString);
	}
	else
	{
		TriggerAlertEx(allocAddString(pKeyString), pFullErrorString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS,
			0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, pFileName, iLineNum);
	}

	estrDestroy(&pFullErrorString);
}


void AssertOrAlertWithStructEx(const char *pKeyString, ParseTable *pTPI, void *pStruct, const char *pFileName, int iLineNum, const char *pErrorStringFmt, ...)
{
	char *pInString = NULL;
	char *pStructString = NULL;
	char *pFullString = NULL;

	estrStackCreate(&pInString);
	estrStackCreate(&pStructString);
	estrStackCreate(&pFullString);

	estrGetVarArgs(&pInString, pErrorStringFmt);
	ParserWriteText(&pStructString, pTPI, pStruct, 0, 0, 0);

	estrPrintf(&pFullString, "%s\n\nAttached struct of type %s:\n%s", pInString, ParserGetTableName(pTPI), pStructString);

	AssertOrAlertEx(pKeyString, pFileName, iLineNum, "%s", pFullString);

	estrDestroy(&pInString);
	estrDestroy(&pStructString);
	estrDestroy(&pFullString);
}

void AssertOrAlertWarningWithStructEx(const char *pKeyString, ParseTable *pTPI, void *pStruct, const char *pFileName, int iLineNum, const char *pErrorStringFmt, ...)
{
	char *pInString = NULL;
	char *pStructString = NULL;
	char *pFullString = NULL;

	estrStackCreate(&pInString);
	estrStackCreate(&pStructString);
	estrStackCreate(&pFullString);

	estrGetVarArgs(&pInString, pErrorStringFmt);
	ParserWriteText(&pStructString, pTPI, pStruct, 0, 0, 0);

	estrPrintf(&pFullString, "%s\n\nAttached struct of type %s:\n%s", pInString, ParserGetTableName(pTPI), pStructString);

	AssertOrAlertWarningEx(pKeyString, pFileName, iLineNum, "%s", pFullString);

	estrDestroy(&pInString);
	estrDestroy(&pStructString);
	estrDestroy(&pFullString);
}


void ErrorOrAlertEx(SA_PARAM_NN_STR const char *pKeyString, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;
	va_list ap;

	if (gbCavemanMode)
	{
		return;
	}

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		Errorf("%s", pFullErrorString);
	}
	else
	{
		TriggerAlertEx(allocAddString(pKeyString), pFullErrorString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS,
			0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, pFileName, iLineNum);
	}

	estrDestroy(&pFullErrorString);
}

void ErrorOrProgrammerAlertEx(SA_PARAM_NN_STR const char *pKeyString, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;
	va_list ap;

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		Errorf("%s", pFullErrorString);
	}
	else
	{
		TriggerAlertEx(allocAddString(pKeyString), pFullErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER,
			0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, pFileName, iLineNum);
	}

	estrDestroy(&pFullErrorString);
}

void ErrorOrAutoGroupingAlert(const char *pKeyString, int iGroupingSeconds, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullError = NULL;
	estrGetVarArgs(&pFullError, pErrorStringFmt);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		Errorf("%s", pFullError);
	}
	else
	{
		TriggerAutoGroupingAlert(pKeyString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, iGroupingSeconds, 
			"%s", pFullError);
	}

	estrDestroy(&pFullError);
}

void ErrorOrCriticalAlertEx(SA_PARAM_NN_STR const char *pKeyString, const char *pFileName, int iLineNum, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;
	va_list ap;

	if (gbCavemanMode)
	{
		return;
	}

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);

	if (isDevelopmentMode() && StatusReporting_GetState() == STATUSREPORTING_OFF)
	{
		Errorf("%s", pFullErrorString);
	}
	else
	{
		TriggerAlertEx(allocAddString(pKeyString), pFullErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
			0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, pFileName, iLineNum);
	}

	estrDestroy(&pFullErrorString);
}


#undef ErrorDetailsf
void ErrorDetailsf(FORMAT_STR const char *pDetailsStringFmt, ...)
{
	PerThreadErrorDetails *pPerThreadErrorDetails = getPerThreadErrorDetails();
	char *pFullDetailsString = NULL;

	va_list ap;

	va_start(ap, pDetailsStringFmt);

	estrConcatfv(&pFullDetailsString, pDetailsStringFmt, ap);

	va_end(ap);

	estrCopy2(&pPerThreadErrorDetails->details, pFullDetailsString);

	estrDestroy(&pFullDetailsString);
}


void EstringErrorCallback(ErrorMessage *errMsg, void *userdata)
{
	char **ppEStr = (char**)userdata;
	estrConcatf(ppEStr, "%s\n", errorFormatErrorMessage(errMsg));
}


static char **sppDeferredErrorAlerts = NULL;

#undef ErrorOrAlertDeferred
void ErrorOrAlertDeferred(bool bCriticalAlert, SA_PARAM_NN_STR const char *pKeyString, FORMAT_STR const char *pErrorStringFmt, ...)
{
	char *pFullErrorString = NULL;

	va_list ap;

	va_start(ap, pErrorStringFmt);

	estrConcatfv(&pFullErrorString, pErrorStringFmt, ap);

	va_end(ap);

	EnterCriticalSection(&deferredErrorAlertCS);

	if (bCriticalAlert)
	{
		eaPush(&sppDeferredErrorAlerts, "1");
	}
	else
	{
		eaPush(&sppDeferredErrorAlerts, "0");
	}
	eaPush(&sppDeferredErrorAlerts, (char*)pKeyString);
	eaPush(&sppDeferredErrorAlerts, strdup(pFullErrorString));

	LeaveCriticalSection(&deferredErrorAlertCS);
	estrDestroy(&pFullErrorString);
}

void UpdateDeferredErrorOrAlerts(void)
{
	if (eaSize(&sppDeferredErrorAlerts))
	{
		EnterCriticalSection(&deferredErrorAlertCS);

		while (eaSize(&sppDeferredErrorAlerts))
		{
			if (sppDeferredErrorAlerts[0][0] == '1')
			{
				TriggerAlert(sppDeferredErrorAlerts[1], sppDeferredErrorAlerts[2], ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
					0, 0, 0, 0, 0, NULL, 0);
			}
			else
			{
				ErrorOrAlert(sppDeferredErrorAlerts[1], "%s", sppDeferredErrorAlerts[2]);
			}
			
			free(sppDeferredErrorAlerts[2]);
			eaRemove(&sppDeferredErrorAlerts, 0);			
			eaRemove(&sppDeferredErrorAlerts, 0);
			eaRemove(&sppDeferredErrorAlerts, 0);
		}



		LeaveCriticalSection(&deferredErrorAlertCS);
	}
}


typedef struct PerThreadDuringDataLoading
{
	int duringDataLoading;
	char duringDataLoadingFilename[MAX_PATH];
} PerThreadDuringDataLoading;

static PerThreadDuringDataLoading last_thread_during_loading; // stack copy for minidumps

static PerThreadDuringDataLoading *getPerThreadDuringDataLoading(void)
{
	PerThreadDuringDataLoading *p;
	STATIC_THREAD_ALLOC(p);
	return p;
}

void errorIsDuringDataLoadingInc(const char *filename)
{
	PerThreadDuringDataLoading *p = getPerThreadDuringDataLoading();
	p->duringDataLoading++;
	if (filename)
		strcpy(p->duringDataLoadingFilename, filename);
	else
		p->duringDataLoadingFilename[0] = '\0';
	memcpy(&last_thread_during_loading, p, sizeof(*p));
}

void errorIsDuringDataLoadingDec(void)
{
	PerThreadDuringDataLoading *p = getPerThreadDuringDataLoading();
	p->duringDataLoading--;
}

bool errorIsDuringDataLoadingGet(void)
{
	PerThreadDuringDataLoading *p = getPerThreadDuringDataLoading();
	return p && p->duringDataLoading>0;
}

const char *errorIsDuringDataLoadingGetFileName(void)
{
	PerThreadDuringDataLoading *p = getPerThreadDuringDataLoading();
	if (p)
	{
		p->duringDataLoadingFilename[ARRAY_SIZE(p->duringDataLoadingFilename)-1] = '\0'; // Make absolutely sure it's terminated
		return p->duringDataLoadingFilename;
	} else {
		return "";
	}
}

#pragma warning(push)
#pragma warning(disable:6011)
AUTO_COMMAND ACMD_CMDLINE ACMD_HIDE;
void errorIsDuringDataLoadingTest(const char *filename)
{
	char *crasher = NULL;
	errorIsDuringDataLoadingInc(filename);
	crasher[0] = '\0'; // NULL pointer exception to force a crash
}
#pragma warning(pop)



void AlertfInternal(char *pFile, int iLine, char *pFmt, ...)
{
	va_list ap;
	ErrorMessage *errMsg;

	va_start(ap, pFmt);
	errMsg = BuildErrorMessage(ERROR_ALERT, pFile, iLine, NULL, NULL, 0, 0, 0, 0, pFmt, ap);
	va_end(ap);

	errMsg->bReport = false;

	DisplayErrorMessage(errMsg);

	DestroyErrorMessage(errMsg);
}


DWORD WINAPI ErrorThreadedTest(char *errormsg)
{
	EXCEPTION_HANDLER_BEGIN
	Errorf("%s", errormsg);
	SAFE_FREE(errormsg);

	EXCEPTION_HANDLER_END
	return 0;
}

void ErrorStartThreadTest(const char *errormsg)
{
	DWORD id;
	char *errorcpy = StructAllocString(errormsg);
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, ErrorThreadedTest, (LPVOID)errorcpy, 0, &id));
}

WorkerThread *gpErrorWorkerThread = NULL;

enum
{
	ERRORCMD_ERRORFILENAMEF = WT_CMD_USER_START,
};

AUTO_STRUCT;
typedef struct ErrorFilenameCache
{
	char *pErrorFilename;
	char *pFullErrorString; AST(ESTRING)
	int iCallerLine; AST(UNOWNED)
	const char *pCallerFilename; AST(UNOWNED)
} ErrorFilenameCache;


static void ErrorThread_ErrorFilenamef(void *user_data, void *data, WTCmdPacket *packet)
{
	ErrorFilenameCache *pCache = *((ErrorFilenameCache**)data);
	ErrorFilenamefInternal_Blocking(pCache->pCallerFilename, pCache->iCallerLine, pCache->pErrorFilename, "%s", pCache->pFullErrorString);
	StructDestroy(parse_ErrorFilenameCache, pCache);
}

void ErrorFilenameWorkerThreadInit(void)
{
	gpErrorWorkerThread = wtCreate(16, 16, NULL, "ErrorThread");
	wtRegisterCmdDispatch(gpErrorWorkerThread, ERRORCMD_ERRORFILENAMEF, ErrorThread_ErrorFilenamef);

	wtSetThreaded(gpErrorWorkerThread, true, 0, false);
	wtStart(gpErrorWorkerThread);
}


void ErrorFilenamef_NonBlockingInternal(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...)
{
	ErrorFilenameCache *pCache = StructCreate(parse_ErrorFilenameCache);

	ONCE(ErrorFilenameWorkerThreadInit(););

	estrGetVarArgs(&pCache->pFullErrorString, fmt);

	consolePushColor();
	consoleSetColor(COLOR_RED | COLOR_HIGHLIGHT, 0);
	printf("%s\n", pCache->pFullErrorString);
	consolePopColor();

	log_printf(LOG_ERRORS, "Dispatching ErrorFilenamef_NonBlocking %s:%s to worker thread", pCache->pFullErrorString, filename);
	
	pCache->pCallerFilename = file;
	pCache->iCallerLine = line;
	pCache->pErrorFilename = strdup(filename);

	wtQueueCmd(gpErrorWorkerThread, ERRORCMD_ERRORFILENAMEF, &pCache, sizeof(void*));
}

void ErrorFilenamev_NonBlockingInternal(const char *file, int line, const char *filename, char const *fmt, va_list ap)
{
	ErrorFilenameCache *pCache = StructCreate(parse_ErrorFilenameCache);

	ONCE(ErrorFilenameWorkerThreadInit(););

	estrConcatfv_dbg(&pCache->pFullErrorString, file, line, fmt, ap);

	consolePushColor();
	consoleSetColor(COLOR_RED | COLOR_HIGHLIGHT, 0);
	printf("%s\n", pCache->pFullErrorString);
	consolePopColor();

	log_printf(LOG_ERRORS, "Dispatching ErrorFilenamef_NonBlocking %s:%s to worker thread", pCache->pFullErrorString, filename);
	pCache->pCallerFilename = file;
	pCache->iCallerLine = line;
	pCache->pErrorFilename = strdup(filename);

	wtQueueCmd(gpErrorWorkerThread, ERRORCMD_ERRORFILENAMEF, &pCache, sizeof(void*));
}


#include "error_c_ast.c"
