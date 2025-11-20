/* File assert.c
 *	Ripped and modified version of assert.c found in the MS CRT lib.
 *	
 */
#include "logging.h"
#include "GlobalTypes.h"
#include "gimmeDLLWrapper.h"

#include "file.h"
#include "earray.h"
#include "trivia.h"
#include "trivia_h_ast.h"
#include "ContinuousBuilderSupport.h"
#include "mathutil.h"
#include "VirtualMemory.h"
#include "Organization.h"
#include "statusReporting.h"
#include "TimedCallback.h"
#include "estring.h"
#include "UTF8.h"
#include "stringUtil.h"
#include "shlwapi.h"

static const char *assertGetLogFilename(void);
static bool assertInitialized = 0;

static struct {
	S32			val;
	const char* fileName;
	U32			fileLine;
	U32			threadID;
} g_programIsShuttingDown;

static char sAccountName[MAX_NAME_LEN] = "";
static char szErrorTracker[128] = ORGANIZATION_DOMAIN;
static char sCrashBeganEvent[128] = "";
static char sCrashCompletedEvent[128] = "";
static int iNumCrashes = 0;

// Freeze all threads, including the faulting thread, on exceptions and asserts.  If the faulting thread is unfrozen, the process will proceed to crash normally.
static bool sbFreezeOnCrash = false;
AUTO_CMD_INT(sbFreezeOnCrash,freezeOnCrash) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

#define MAX_PAGEFILE_SIZE_FOR_HEAPVALIDATE (4LL * 1024 * 1024 * 1024) // 4 GiB

int programIsShuttingDown(void) {
    return g_programIsShuttingDown.val;
}

static int sForceServerMode = 0;
AUTO_CMD_INT(sForceServerMode, forceServerMode) ACMD_CMDLINE;
void assertForceServerMode(int bForce)
{
	sForceServerMode = bForce;
}

bool DEFAULT_LATELINK_assertForceDumps(void)
{
	return false;
}


//If true, then when something crashes, just sit there forever
//so it can be debugged. The controller has special rules for propagating this to
//other servers in the shard
bool gbLeaveCrashesUpForever = false;
AUTO_CMD_INT(gbLeaveCrashesUpForever, LeaveCrashesUpForever) ACMD_CMDLINE;

//If true, will ignore programmer mode for this process even if the registry key is set
bool gbIgnoreProgrammerMode = false;
AUTO_CMD_INT(gbIgnoreProgrammerMode, IgnoreProgrammerMode) ACMD_CMDLINE;

//Print the callstack report on crash, if available.
bool gbPrintCallstackReport = false;
AUTO_CMD_INT(gbPrintCallstackReport, PrintCallstackReport) ACMD_CMDLINE;

#if defined(_DEBUG) || PROFILE

#include <signal.h>
#include <stdio.h>
#include <process.h>
#include "signal.h"
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>

#include "wininclude.h"
#include "sock.h"
#include <time.h>
#if !PLATFORM_CONSOLE
	#include <DbgHelp.h>
	#include "errorrep.h"
	#include "CrashRpt.h"
	#include <tlhelp32.h>
#endif
#include "sysutil.h"
#include "callstack.h"
#include "tchar.h"
#include "timing_profiler_interface.h"
#include "memlog.h"
#include "fileutil.h"
#include "winutil.h"
#include "strings_opt.h"
#include "SharedHeap.h"
#include "stdtypes.h"
#include "utilitieslib.h"
#include "errornet.h"
#include "stackwalk.h"
#include "memalloc.h"
#include "GlobalComm.h"
#include "cmdParse.h"
#include "timing_profiler_interface.h"
#include "crypticerror.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define ASSERT_SHOW_INTERNAL (!isProductionMode() || !(g_assertmode & ASSERTMODE_ISEXTERNALAPP) || (errorTrackerGetUniqueID() != 0 && !(errorTrackerGetDumpFlags() & DUMPFLAGS_EXTERNAL)))

//Added this NULLPTR to fix ASSERT
int *g_NULLPTR = 0;

#if !PLATFORM_CONSOLE
int __cdecl __crtMessageBoxA(LPCSTR, LPCSTR, UINT);
#endif

// From error.c
extern int g_errorClientCount;
extern char *g_errorTriviaString;

// from memalloc.c
extern char *memory_dump_string;

static const char *pMiniDumpFilenameOverride = NULL;
static const char *pFullDumpFilenameOverride = NULL;

// If non-null, use this instead of our module-local state.
static crashStateFunc overrideCrashState = NULL;

// Set and get crash state.  Don't set if set is -1.
int crashState(int set)
{
	static int g_isAsserting = 0;
	int old;

	// If we've been overridden, forward to that.
	if (overrideCrashState)
		return overrideCrashState(set);

	// Otherwise get and set, if requested.
	old = g_isAsserting;
	if (set != -1)
		g_isAsserting = set;

	return old;
}

// Override our crashState() with one from another module.
void setCrashState(crashStateFunc func)
{
	overrideCrashState = func;
}

// Return true if we've crashed.
int isCrashed()
{
	return crashState(-1);
}

static assertSndCB g_assertSndCB;
void assertSetSndCB(assertSndCB cb)
{
	g_assertSndCB = cb;
}


static AuxAssertCB spAuxAssertCB = NULL;

void SetAuxAssertCB(AuxAssertCB pCB)
{
	spAuxAssertCB = pCB;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setErrorTracker(const char *pErrorTracker)
{
	strcpy(szErrorTracker, pErrorTracker);
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setCrashBeganEvent(const char *pEventName)
{
	strcpy(sCrashBeganEvent, pEventName);
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setCrashCompletedEvent(const char *pEventName)
{
	strcpy(sCrashCompletedEvent, pEventName);
}

AUTO_RUN_FIRST;
void superEarlyErrorTrackerSetting(void)
{
	ParseCommandOutOfCommandLine("SetErrorTracker", szErrorTracker);
}



/*
 * assertion format string for use with output to stderr
 */
static char _assertstring[] = "Assertion failed: %s, file %s, line %d\n";

/*      Format of MessageBox for assertions:
*
*       ================= Microsft Visual C++ Debug Library ================
*
*       Assertion Failed!
*
*       Program: c:\test\mytest\foo.exe
*       File: c:\test\mytest\bar.c
*       Line: 69
*
*       Expression: <expression>
*
*		<Stack dump>
*
*       ===================================================================
*/

/*
 * assertion string components for message box
 */
#define BOXINTRO    "Assertion failed"
#define PROGINTRO   "Program: "
#define VERINTRO	"Version: "
#define TIMEINTRO	"Time: "
#define PROCESSID	"Process ID: "
#define FILEINTRO   "File: "
#define LINEINTRO   "Line: "
#define EXTRAINFO	"Extra Info: "
#define EXTRAINFO2	"\nExtra Info:\n"
#define EXPRINTRO   "Expression: "
#define MSGINTRO	"Error Message: "
#define CRASHINTRO	"Unique Error ID: "
#define CRASHINTRO2	"http://errortracker/detail?id="

static char * dotdotdot = "...";
static char * newline = "\n";
static char * dblnewline = "\n\n";

#define DOTDOTDOTSZ 3
#define NEWLINESZ   1
#define DBLNEWLINESZ   2

#define MAXLINELEN 60
#define ASSERT_BUF_SIZE 1024 * 10	// Be prepared for a large stackdump.
#define VERSION_BUF_SIZE 512

static int g_assertmode = ASSERTMODE_MINIDUMP; // default for internal projects
static char* staticExtraInfo = NULL;
static char* staticExtraInfo2 = NULL;
static int g_autoResponse = -1;
static int g_disableIgnore = 0;
int g_ignoredivzero = 0;			// if set, we advance and ignore integer divide-by-zero errors
static int g_ignoreexceptions = 0;	// if set we don't catch an exception.  Used by assert to fall into OS faster
static int generatingErrorRep = 0;

#if !PLATFORM_CONSOLE
// The default set of flags to pass to the minidump writer
MINIDUMP_TYPE minidump_base_flags = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithProcessThreadData | MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory);
// The "safe" set of flags, call these with an Assert so that it doesn't corrupt memory!
MINIDUMP_TYPE minidump_assert_flags = (MINIDUMP_TYPE)(MiniDumpWithProcessThreadData | MiniDumpWithDataSegs);
// The set of flags that work on older versions of dbghelp.dll
MINIDUMP_TYPE minidump_old_flags = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithDataSegs);
// The set to actually use
MINIDUMP_TYPE minidump_flags = 0;


// Crash report related fields.
struct {
	int running;
	void* handle;

	pfnCrashRptInstall				Install;
	pfnCrashRptGenerateErrorReport3	Report;
	pfnCrashRptAbortErrorReport		StopReport;
	pfnCrashRptUninstall			Uninstall;
} cr;
#endif

int assertbuf_filled = 0; // If it's already been filled from the call to superassert
char assertbuf[ASSERT_BUF_SIZE * 2] = "";
char versionbuf[VERSION_BUF_SIZE] = "";
static char sExternalReturnBuffer[ASSERT_EXTERNAL_BUF_SIZE] = "";
AssertCallback assertCallback = NULL;
static int sCrypticErrorDebugMe = 0;

// Returns age of Version String s1, as compared to Version String s2:
//    -1 : s1 is older / lesser
//     0 : s1 is same
//     1 : s1 is newer / greater
//
// Note: This should be updated if GetUsefulVersionString() ever returns non-numeric
//       strings, and since it is used with strings passed across process boundaries,
//       it can only use s1 and s2 to judge the comparison (cannot use gBuildVerison
//       or any other global vars).
int CompareUsefulVersionStrings(const char *s1, const char *s2)
{
	int v1 = atoi(s1);
	int v2 = atoi(s2);

	if(v1 == v2)
	{
		return 0;
	}

	return (v1 < v2) ? -1 : 1;
}

extern void do_special_internal_autoruns(void);

// returns the modification time of the executable if there is no build_version, build_version if there is one
static const char *GetUsefulVersion(void)
{
	static char retString[160] = "";

	if (retString[0])
	{
		return retString;
	}

	if (ProdVersion() && ProdVersion()[0] && stricmp(ProdVersion(), "none")!=0) {
		sprintf(retString, "%s", ProdVersion());
		return retString;
	}

	//makes sure gVersion is set
	do_special_internal_autoruns();

	if (isDevelopmentMode())
	{
		int iGimmeVersion;

		//if this is being called super-earlyd during startup, fileDataDir() will not be legal yet
		if (!fileLoadedDataDirs())
		{
			
			static char *spTempRetString = NULL;
			if (!spTempRetString)
			{
				estrPrintf(&spTempRetString, "%d (%s)", gBuildVersion, GetShortProductName());
			}
			return spTempRetString;
		}

		iGimmeVersion = gimmeDLLQueryBranchNumber(fileDataDir());
		if (iGimmeVersion != -1)
			sprintf(retString, "%d (%s %d)", gBuildVersion, GetShortProductName(), iGimmeVersion);
		else
			sprintf(retString, "%d", gBuildVersion);
	}
	else
		sprintf(retString, "%d", gBuildVersion);
	
	return retString;
}




void setAssertCallback(AssertCallback func)
{
	assertCallback = func;
}

int assertIsExecutableInToolsBin(void)
{
	char path[CRYPTIC_MAX_PATH];
	getExecutableDir(path);
	return !!strstri(path, "/tools/bin");
}

void setProgramIsShuttingDown(int val, char *pFile, int iLine)	
{
	if (GetAppGlobalType()!=GLOBALTYPE_NONE && GetAppGlobalType()!=GLOBALTYPE_CRYPTICLAUNCHER && GetAppGlobalType()!=GLOBALTYPE_CLIENT) // Don't splatter logs from random utilities
	{
		if (loggingActive())
		{
			log_printf(LOG_CRASH, "Shutting down: %s(%d)", pFile, iLine);
			filelog_printf("Shutdown.log", "Shutting down: %s(%d)", pFile, iLine);
		}
	}
	g_programIsShuttingDown.val = val;  
	g_programIsShuttingDown.fileName = pFile;
	g_programIsShuttingDown.fileLine = iLine;
	g_programIsShuttingDown.threadID = GetCurrentThreadId();
	if (val)
	{
		if (loggingActive())
		{
			logFlush();
			logWaitForQueueToEmpty();
		}
		commFlushAndCloseAllComms(0.5f);
		errorShutdown();
	}
}

SOCKET* socksCloseOnAssert = NULL;
static int socksAdded = 0;
static int socksClosed = 0;

#ifdef _WIN64
	#define eaSOCKET(func) eai64##func
#else
	#define eaSOCKET(func) eai##func
#endif


void closeSockOnAssert(SOCKET sock)
{
	socksAdded++;
	eaSOCKET(Push)(&socksCloseOnAssert, sock);
}

static void closeSocketsOnAssert(void)
{
	while(eaSOCKET(Size)(&socksCloseOnAssert))
	{
		SOCKET sock = socksCloseOnAssert[0];
		printf("Closing socket %"FORM_LL"d.\n", (U64)(uintptr_t)sock);
		socksClosed++;
		safeCloseSocket(&sock);
		eaSOCKET(Remove)(&socksCloseOnAssert, 0);
	}
}


void setAssertExtraInfo(const char* info)
{
	if(staticExtraInfo)
	{
		free(staticExtraInfo);
		staticExtraInfo = NULL;
	}
	
	if(info)
	{
		staticExtraInfo = strdup(info);
	}
}

void setAssertExtraInfo2(const char* info)
{
	if(staticExtraInfo2)
	{
		free(staticExtraInfo2);
		staticExtraInfo2 = NULL;
	}

	if(info)
	{
		staticExtraInfo2 = strdup(info);
	}
}

void setAssertResponse(int idc_response)
{
	g_autoResponse = idc_response;
}

void setAssertProgramVersion(const char* versionName)
{
	Strncpyt(versionbuf, versionName);
}

static DWORD safe_threads[10];
static volatile long safe_threads_count=0;
static bool canFreezeThisThread(DWORD threadID)
{
	int i;
	for (i=0; i<safe_threads_count; i++) 
		if (safe_threads[i]==threadID)
			return false;
	return true;
}
void assertDoNotFreezeThisThread(DWORD threadID)
{
	if (canFreezeThisThread(threadID)) { // Not already in list
		long index = InterlockedIncrement(&safe_threads_count);
		safe_threads[index-1] = threadID;
	}
}

#if !PLATFORM_CONSOLE
void assertFreezeAllOtherThreads(int resume)
{

	DWORD			dwOwnerPID	= GetCurrentProcessId();
	DWORD			dwOwnerTID	= GetCurrentThreadId();
    HANDLE			hThreadSnap	= NULL; 
    BOOL			bRet		= FALSE; 
    THREADENTRY32	te32		= {0}; 
	typedef HANDLE (WINAPI *tOpenThread)(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId);
	tOpenThread		pOpenThread;
	HMODULE			hKernel32Dll = LoadLibrary(_T("kernel32.dll"));

	if (!hKernel32Dll)
		return;
	pOpenThread = (tOpenThread)GetProcAddress(hKernel32Dll, "OpenThread");
	if (!pOpenThread)
	{
		CloseHandle(hKernel32Dll);
		return;
	}

	assertDoNotFreezeThisThread(dwOwnerTID);

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
				if (canFreezeThisThread(te32.th32ThreadID))
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

// Freeze all threads, including the main thread; resme up all threads if the main thread is thawed.
static void assertFreezeAllThreadsOnCrash()
{
	DWORD result;

	// Freeze all threads except the current one.
	assertFreezeAllOtherThreads(0);
	
	// Freeze the current thread.
	// This is where the process will stay until woken back up by some external process.
	result = SuspendThread(GetCurrentThread());

	// Wake everything back up, so we can proceed normally.
	assertFreezeAllOtherThreads(1);
}

#endif

// from "res2c errordlg.res"
static unsigned char DialogResource2[] = { // Program Crash (external user dialog)
	0x01, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x0A, 0xC0, 0x10, 
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x01, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 
	0x72, 0x00, 0x6F, 0x00, 0x67, 0x00, 0x72, 0x00, 0x61, 0x00, 0x6D, 0x00, 0x20, 0x00, 0x43, 0x00, 
	0x72, 0x00, 0x61, 0x00, 0x73, 0x00, 0x68, 0x00, 0x00, 0x00, 0x08, 0x00, 0x90, 0x01, 0x00, 0x01, 
	0x4D, 0x00, 0x53, 0x00, 0x20, 0x00, 0x53, 0x00, 0x68, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 
	0x20, 0x00, 0x44, 0x00, 0x6C, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x50, 0xA2, 0x00, 0xAC, 0x00, 0x32, 0x00, 0x0E, 0x00, 
	0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0x53, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x64, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 
	0xDB, 0x00, 0xAC, 0x00, 0x32, 0x00, 0x0E, 0x00, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x00, 
	0x43, 0x00, 0x61, 0x00, 0x6E, 0x00, 0x63, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50, 0x0D, 0x00, 0x17, 0x00, 
	0xE4, 0x00, 0x10, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x82, 0x00, 0x54, 0x00, 0x6F, 0x00, 
	0x20, 0x00, 0x61, 0x00, 0x73, 0x00, 0x73, 0x00, 0x69, 0x00, 0x73, 0x00, 0x74, 0x00, 0x20, 0x00, 
	0x69, 0x00, 0x6E, 0x00, 0x20, 0x00, 0x66, 0x00, 0x69, 0x00, 0x78, 0x00, 0x69, 0x00, 0x6E, 0x00, 
	0x67, 0x00, 0x20, 0x00, 0x74, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x69, 0x00, 0x73, 0x00, 
	0x73, 0x00, 0x75, 0x00, 0x65, 0x00, 0x2C, 0x00, 0x20, 0x00, 0x70, 0x00, 0x6C, 0x00, 0x65, 0x00, 
	0x61, 0x00, 0x73, 0x00, 0x65, 0x00, 0x20, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 
	0x72, 0x00, 0x20, 0x00, 0x61, 0x00, 0x20, 0x00, 0x64, 0x00, 0x65, 0x00, 0x73, 0x00, 0x63, 0x00, 
	0x72, 0x00, 0x69, 0x00, 0x70, 0x00, 0x74, 0x00, 0x69, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x20, 0x00, 
	0x6F, 0x00, 0x66, 0x00, 0x20, 0x00, 0x77, 0x00, 0x68, 0x00, 0x61, 0x00, 0x74, 0x00, 0x20, 0x00, 
	0x79, 0x00, 0x6F, 0x00, 0x75, 0x00, 0x20, 0x00, 0x77, 0x00, 0x65, 0x00, 0x72, 0x00, 0x65, 0x00, 
	0x20, 0x00, 0x64, 0x00, 0x6F, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x67, 0x00, 0x20, 0x00, 0x61, 0x00, 
	0x74, 0x00, 0x20, 0x00, 0x74, 0x00, 0x68, 0x00, 0x65, 0x00, 0x20, 0x00, 0x74, 0x00, 0x69, 0x00, 
	0x6D, 0x00, 0x65, 0x00, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x81, 0x50, 0x0C, 0x00, 0x2A, 0x00, 0x02, 0x01, 0x78, 0x00, 
	0xEF, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50, 0x0E, 0x00, 0x08, 0x00, 0x00, 0x01, 0x0C, 0x00, 
	0xF0, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0x82, 0x00, 0x53, 0x00, 0x74, 0x00, 0x61, 0x00, 0x74, 0x00, 
	0x69, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, };



/////////////////////////////////// from assertdlg, errordlg, and externaldlg (resource.h)
#define IDC_STOP 0
#define IDC_DEBUG 1
#define IDC_IGNORE 3
#define IDC_ERRORTRACKER 13
#define IDC_MAINTEXT 10
#define IDC_RUNDEBUGGER 11
#define IDC_COPYTOCLIPBOARD 12
#define IDC_CRASHTEXT 15
#define IDC_RETURNTOERROR 0
#define IDC_DISMISS 1
#define IDC_SUBTEXT 11

#define IDD_CALLPROG                    101
#define IDD_ASSERTDLG_DIALOG            102
#define IDS_APP_TITLE                   103
#define IDD_ABOUTBOX                    103
#define IDD_ASSERT                      103
#define IDM_ABOUT                       104
#define IDM_EXIT                        105
#define IDI_SMALL                       108
#define IDC_ASSERTDLG                   109
#define IDD_EXTERNAL                    110
#define IDR_MAINFRAME                   128
#define IDD_ERROR                       129
#define IDC_RICHEDIT21                  1001
#define IDC_ERRORTEXT                   1003
#define IDC_ERRORTEXT2                  1004
#define IDC_FAULTTEXT                   1004
#define IDC_BUTTON1                     1005
#define IDC_ADDITIONAL_TEXT             1006
#define IDC_CRASHDESCRIPTION            1007
#define IDC_STATIC_EXTERNALPRODUCT      1008
///////////////////////////////////

#define BASE_LISTEN_PORT 314159 // Why?  Because I like pie.
bool g_listenThreadRunning=false;
int g_listenThreadResponse=0;

void sendError(SOCKET s, char *error) {
	int i = 0;
	send(s, (char*)&i, sizeof(i), 0);
	i = (int)strlen(error);
	send(s, (char*)&i, sizeof(i), 0);
	send(s, error, i, 0);
}

#if !PLATFORM_CONSOLE
void sendPDB(SOCKET s) {
	char buffer[2048] = "";
	char *ss;
	int i, i2;
	int fh;
	struct _stat64 status;
	char *pProgName = NULL;
	char progname_buf[CRYPTIC_MAX_PATH * 2];

	estrBufferCreate(&pProgName, SAFESTR(progname_buf));
	if ( !GetModuleFileName_UTF8( NULL, &pProgName)) {
		sendError(s, "<program name unknown>");
		estrDestroy(&pProgName);
		return;
	}
	// Convert to .pdb
	ss = strrchr(pProgName, '.');
	if (!ss) {
		sendError(s, "<invalid format of program name>");
		estrDestroy(&pProgName);
		return;
	}

	estrSetSize(&pProgName, ss - pProgName);
	estrConcatf(&pProgName, ".pdb");

	_wsopen_s_UTF8(&fh, pProgName, _O_BINARY | _O_RDONLY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
	if (fh==-1) {
		sendError(s, "Cannot open PDB file for reading");
		estrDestroy(&pProgName);
		return;
	}

	// Send size
	if(!_stat64(pProgName, &status)){
		if(!(status.st_mode & _S_IFREG)) {
			sendError(s, "Bad PDB file (it's a directory?");
			estrDestroy(&pProgName);
			return;
		}
		// Send filename
		i = (int)strlen(pProgName);
		send(s, (char*)&i, sizeof(i), 0);
		send(s, pProgName, i, 0);

		i = status.st_size;
		printf("Sending %d bytes\n", i);
		send(s, (char*)&i, sizeof(i), 0);
	}
	do {
		i = _read(fh, buffer, 1400);
		if (i) {
			i2 = send(s, buffer, i, 0);
			if (i!=i2) {
				printf("Error calling send, only sent %d bytes!\n", i2);
				break;
			}
		}
	} while (i==1400);
	_close(fh);
	printf("Send complete\n");
	estrDestroy(&pProgName);
}
#endif

static int ipIsInternal(struct sockaddr_in* ip)
{
#define IP_BYTE(ip, byteID) (ip->sin_addr.S_un.S_un_b.s_b##byteID)
	if(IP_BYTE(ip, 1) == 10)
	{
		return 1;
	}
	else if(IP_BYTE(ip,1) == 172)
	{
		int val = IP_BYTE(ip,2);
		if(val >= 16 && val <= 31)
			return 1;
	}
	else if(IP_BYTE(ip,1) == 192)
	{
		if(IP_BYTE(ip,2) == 168)
			return 1;
	}
	return 0;
}

static void	wsockStart()
{
	WORD wVersionRequested;  
	WSADATA wsaData; 
	int err; 
	wVersionRequested = MAKEWORD(2, 2); 

	err = WSAStartup(wVersionRequested, &wsaData); 
}

void runRemoteDebugger()
{
#if !PLATFORM_CONSOLE
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	int i;
	static WCHAR *debuggerpaths[] = 
	{
		// VS 2005 remote debuggers
		L"\"C:/Night/tools/bin/remote debugger/msvsmon.exe\" /anyuser /noauth /nosecuritywarn /nowowwarn",
		L"\"C:/Program Files/Microsoft Visual Studio 8/Common7/IDE/Remote Debugger/x86/msvsmon.exe\" /anyuser /noauth /nosecuritywarn /nowowwarn",
		L"\"C:/Program Files (x86)/Microsoft Visual Studio 8/Common7/IDE/Remote Debugger/x86/msvsmon.exe\" /anyuser /noauth /nosecuritywarn /nowowwarn",
		L"\"./remote debugger/msvsmon.exe\" /anyuser /noauth /nosecuritywarn /nowowwarn",
	};
	bool success=false;

	for (i=0; i<ARRAY_SIZE(debuggerpaths); i++)
	{
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);
		memset(&pi, 0, sizeof(pi));
		if (CreateProcess(NULL, debuggerpaths[i], NULL, NULL, 0, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) 
		{
			success = true;
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			break;
		}
	}
	if (!success)
	{
		WCHAR buf[1000];

		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, buf, 2000, NULL);
		MessageBox(NULL, buf, L"Error running debugger", MB_SETFOREGROUND);
	}
#endif
}

DWORD WINAPI listenThreadMain(LPVOID lpThreadParameter)
{
	struct sockaddr_in	addr_in;
	int port=BASE_LISTEN_PORT;
	int result;
	SOCKET s;
	SOCKET s2;

	wsockStart();

	s = socketCreate(AF_INET, SOCK_STREAM, 0);
	do {
		sockSetAddr(&addr_in,htonl(INADDR_ANY),port);
		if (sockBind(s,&addr_in))
			break;
		port++;
		if (port > BASE_LISTEN_PORT + 8) {// If these ports are all aready taken, just give up!
			// Intentionally left this running so we don't get a large number of threads spiraling out of control
			//printf("Couldn't find an open port");
			return 0;
		}
	} while (1);
	//printf("Bound to port %d\n", port);
	result = listen(s, 15);
	if(result)
	{
		// Intentionally left this running so we don't get a large number of threads spiraling out of control
		//printf("listen error..\n");
		return 0;
	}
	do {
		char buffer[16];
		int len;
		struct sockaddr_in incomingIP;
		int addrLen = sizeof(incomingIP);
		s2 = accept(s, (SOCKADDR*)&incomingIP, &addrLen);
		if (s2==INVALID_SOCKET)
			continue;

		len = (int)strlen(assertbuf)+1;
		send(s2, (char*)&len, sizeof(len), 0);
		send(s2, assertbuf, len, 0);
		do {
			// Wait for the main thread to grab the value
			while (g_listenThreadResponse!=0) {
				Sleep(10);
			}
			do {
				result = recv(s2, buffer, 1, 0);
				if (result==SOCKET_ERROR) {
					//printf("read error...\n");
					g_listenThreadRunning = false;
					return 0;
				}
			} while (result==0);
			switch (buffer[0]) {
				case 1:
					g_listenThreadResponse = IDC_STOP+1;
					//printf("Remote debuggerer said STOP\n");
					break;
				case 2:
					g_listenThreadResponse = IDC_DEBUG+1;
#ifndef _XBOX
					if(cr.running)
						cr.StopReport(cr.handle);
#endif
					//printf("Remote debuggerer said DEBUG\n");
					break;
				case 3:
					g_listenThreadResponse = IDC_IGNORE+1;
					//printf("Remote debuggerer said IGNORE\n");
					break;
				case 4:
					runRemoteDebugger();
					break;
				case 5:
					// Get PDB
#ifndef _XBOX
					sendPDB(s2);
#else
					assert(0);
#endif
					break;
				case 0:
					//printf("Remote debuggerer said No more commands\n");
					break;
				default:
					sendError(s2, "Unhandled command (old version of the assert dialog?)");
					//printf("Invalid data from remote debugger debugger client...\n");
					break;
			}
		} while (buffer[0]);
		closesocket(s2);
	} while (1);
	//printf("ListenThread exiting...\n");
	g_listenThreadRunning = false;
	return 0;
}

void listenThreadBegin() {
	HANDLE hListenThread;
	DWORD dwListenThreadID;
	g_listenThreadResponse=0;
	if (g_listenThreadRunning) {
		//printf("Thread already running\n");
		return;
	}
	g_listenThreadRunning = true;
	hListenThread = CreateThread(NULL, 0, listenThreadMain, NULL, 0, &dwListenThreadID);
	if (hListenThread > 0 && (hListenThread != INVALID_HANDLE_VALUE)) {
		assertDoNotFreezeThisThread(dwListenThreadID);
		CloseHandle(hListenThread);
	}
}

char* TimeStr()
{
	__time32_t ts;
	struct tm newtime;
	char am_pm[] = "AM";
	static char result[100];

	ts = _time32(NULL);
	_localtime32_s(&newtime, &ts);

	if( newtime.tm_hour >= 12 )
			strcpy( am_pm, "PM" );
	if( newtime.tm_hour > 12 )        
			newtime.tm_hour -= 12;    
	if( newtime.tm_hour == 0 )        
			newtime.tm_hour = 12;

	asctime_s(SAFESTR(result),&newtime);
	strcpy_s(result+20, ARRAY_SIZE(result)-20, am_pm);
	return result; // returns static buffer
}

// ---------------------------------------------------------------------------------

static int dumpsWritten = 0;

static bool writeMiniDump(PEXCEPTION_POINTERS info)
{
	bool writeSuccess = true;
	if(!(dumpsWritten & DUMPFLAGS_MINIDUMP))
	{
#ifndef _XBOX
		// this guarantees that it is fresh
		DeleteFile(assertGetMiniDumpFilename_Wide());

		minidump_flags = minidump_assert_flags;		
		writeSuccess = assertWriteMiniDumpSimple(info);
#endif
		if (writeSuccess)
			dumpsWritten |= DUMPFLAGS_MINIDUMP;
	}
	return writeSuccess;
}

static bool writeFullDump(PEXCEPTION_POINTERS info)
{
	bool writeSuccess = true;
	if(!(dumpsWritten & DUMPFLAGS_FULLDUMP))
	{
#ifndef _XBOX
		// this guarantees that it is fresh
		DeleteFile(assertGetFullDumpFilename_Wide());

		minidump_flags = MiniDumpWithProcessThreadData;
		writeSuccess = assertWriteFullDumpSimple(info);
#endif
		if (writeSuccess)
			dumpsWritten |= DUMPFLAGS_FULLDUMP;
	}
	return writeSuccess;
}

// Write all dumps that getAssertMode() wants to be dumped.
// This happens before any network mayhem occurs, so that
// services that need a guaranteed dump to disk get them.
static void writeAssertRequestedDumps(PEXCEPTION_POINTERS info)
{
	if(g_assertmode & ASSERTMODE_FULLDUMP)
		writeFullDump(info);

	if(g_assertmode & ASSERTMODE_MINIDUMP)
		writeMiniDump(info);
}

// Writes any dumps that need writing, and then sends the error tracker
// what it requested. Can be updated to prompt the user for permission.
static void writeAndSendDumps(const char *pCrashInfo, PEXCEPTION_POINTERS info)
{
#ifdef _XBOX
	// This will do something soon!
#else
	if (errorTrackerGetDumpFlags() & DUMPFLAGS_EXTERNAL && errorTrackerGetDumpFlags() & DUMPFLAGS_FULLDUMP && 
		g_assertmode & ASSERTMODE_ISEXTERNALAPP && !(g_assertmode & ASSERTMODE_FORCEFULLDUMPS) )
	{
		// TODO
		int msgboxID = MessageBox(NULL,
			L"This bug may require an additional few hundred MBs of debugging information to help fix the issue.\nUploading this may take a while. Do you wish to continue?",
			L"Confirm Extra Debugging Information",
			MB_ICONEXCLAMATION | MB_YESNO);
		if (msgboxID == IDNO)
		{
			errorTrackerDisableFullDumpFlag();
		}
	}

	if(errorTrackerGetDumpFlags() & DUMPFLAGS_FULLDUMP)
	{
		if (!writeFullDump(info) && !(errorTrackerGetDumpFlags() & DUMPFLAGS_EXTERNAL))
		{
			MessageBox(NULL, L"No dumps were sent to Error Tracker. Please leave the error dialog up and find a programmer to look at the crash.", 
							 L"Failed to Write Full Dump", 
							 MB_OK | MB_ICONERROR);
		}
		if (!writeMiniDump(info) && !(errorTrackerGetDumpFlags() & DUMPFLAGS_EXTERNAL))
		{
			MessageBox(NULL, L"No dumps were sent to Error Tracker. Please leave the error dialog up and find a programmer to look at the crash.", 
							 L"Failed to Write Mini Dump", 
							 MB_OK | MB_ICONERROR);
		}
	}
	else if(errorTrackerGetDumpFlags() & DUMPFLAGS_MINIDUMP)
	{
		if (!writeMiniDump(info) && !(errorTrackerGetDumpFlags() & DUMPFLAGS_EXTERNAL))
		{
			MessageBox(NULL, L"No dumps were sent to Error Tracker. Please leave the error dialog up and find a programmer to look at the crash.", 
							 L"Failed to Write Mini Dump", 
							 MB_OK | MB_ICONERROR);
		}
	}

	// Now, send the dumps that the ErrorTracker requested if one of them was successfully written
	if (errorTrackerGetDumpFlags() & dumpsWritten)
	{
		if (dumpsWritten & DUMPFLAGS_MINIDUMP)
		{
			errorTrackerSendDump(pCrashInfo, sExternalReturnBuffer, DUMPFLAGS_MINIDUMP);
		}
		if (errorTrackerGetDumpFlags() & DUMPFLAGS_FULLDUMP && dumpsWritten & DUMPFLAGS_FULLDUMP)
		{
			errorTrackerSendDump(pCrashInfo, sExternalReturnBuffer, DUMPFLAGS_FULLDUMP);
		}
	}
	else if (isProductionMode() && errorTrackerGetDumpFlags() & DUMPFLAGS_EXTERNAL && DUMPFLAGS_DUMPWANTED(errorTrackerGetDumpFlags()))
	{
		errorTrackerSendDumpDescriptionOnly(sExternalReturnBuffer);
	}
#endif
}

// ---------------------------------------------------------------------------------

#if !PLATFORM_CONSOLE
static HWND assertDlg;

// Message handler for external assert dialog.
LRESULT CALLBACK ExternalAssert(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		{
			HFONT hf;
			RECT rc;
			LOGFONT lf;
			char titleBuffer[256];
			
			SetTimer(hDlg, 0, 500 , NULL);
			ShowCursor(1);

			memset(&lf, 0, sizeof(LOGFONT));
			lf.lfHeight = 15;
			lf.lfWeight = FW_BOLD;
			hf = CreateFontIndirect(&lf);

			GetWindowRect(hDlg, &rc);
			MoveWindow(hDlg, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, FALSE);

			// set text
			SendDlgItemMessage(hDlg, IDC_STATIC_EXTERNALPRODUCT, WM_SETFONT, (WPARAM)hf, 0);
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_STATIC_EXTERNALPRODUCT), 
				STACK_SPRINTF("%s has encountered an unexpected crash", GetProductDisplayName(getCurrentLocale()) ));

			if (errorTrackerGetUniqueID())
				sprintf(titleBuffer, "%s Crash- ID #%d", GetProductDisplayName(getCurrentLocale()), errorTrackerGetUniqueID());
			else
				sprintf(titleBuffer, "%s Crash", GetProductDisplayName(getCurrentLocale()));
			SetWindowText_UTF8(hDlg, titleBuffer);
			SetFocus(GetDlgItem(hDlg, IDC_CRASHDESCRIPTION));
		}
	case WM_TIMER:
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
		{
			S16 wideExternReturnBuf[ASSERT_EXTERNAL_BUF_SIZE];
			GetDlgItemText(hDlg, IDC_CRASHDESCRIPTION, wideExternReturnBuf, ASSERT_EXTERNAL_BUF_SIZE);
			WideToUTF8StrConvert(wideExternReturnBuf, sExternalReturnBuffer,  ASSERT_EXTERNAL_BUF_SIZE);
			EndDialog(hDlg, LOWORD(wParam));
			ShowCursor(0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

#endif

bool stackdumpIsUseful(const char *pStackdump)
{
	return pStackdump
		&& strstr(pStackdump, "SYSTEM_ERROR") == NULL
		&& strstr(pStackdump, "SymGetSymFromAddr(): gle = 487") == NULL
		&& strstr(pStackdump, "Line:") != NULL
		&& !strstr(pStackdump, "(...)");  // Fallback callstack: Always use callstack report.
}

static void addExtraInfo(char *assertbuf_param, int assertbuf_size)
{
	/*
	* Extra information
	*/

	if(staticExtraInfo)
	{
		strcat_s_trunc( assertbuf_param, assertbuf_size, newline );
		strcat_s_trunc( assertbuf_param, assertbuf_size, staticExtraInfo );
		strcat_s_trunc( assertbuf_param, assertbuf_size, newline );
	}

	/*
	* More Extra information
	*/

	if(staticExtraInfo2)
	{
		strcat_s_trunc( assertbuf_param, assertbuf_size, EXTRAINFO2 );
		strcat_s_trunc( assertbuf_param, assertbuf_size, staticExtraInfo2 );
		strcat_s_trunc( assertbuf_param, assertbuf_size, newline );
	}

	/*
	* Memory corruption detection
	*/
	{
		size_t pageFileUsage = getProcessPageFileUsage();
		if(pageFileUsage > MAX_PAGEFILE_SIZE_FOR_HEAPVALIDATE)
		{
			strcat_s_trunc( assertbuf_param, assertbuf_size, "Heap corruption state unknown; process too big to check\n" );
		}
		else
		{
			int memoryGood = heapValidateAllReturn();
			if (memoryGood) {
				strcat_s_trunc( assertbuf_param, assertbuf_size, "Heap is NOT corrupted\n" );
			} else {
				strcat_s_trunc( assertbuf_param, assertbuf_size, "Heap is CORRUPTED\n" );
			}
		}
	}

}

static char *platformToExecutableSuffix()
{
#ifdef _WIN64
	return "X64";
#endif

	return "";
}

char *sppPossibleCrypticErrorLocations[] = 
{
	"c:\\Night\\tools\\bin\\CrypticError",
	"..\\..\\..\\Night\\tools\\bin\\CrypticError",
	".\\CrypticError",
};

// This runs CrypticError.exe in "deferred mode" when any of our executables startup, which
// checks if it is the only one running like this, then attempts to send any pre-recorded
// crashes cached on disk (in the TEMP dir). It then cleans up the old errors.

// no longer auto-run, deferred system is off-line
void ceSpawnDeferred(void)
{
#if !PLATFORM_CONSOLE
	S16 progname[CRYPTIC_MAX_PATH + 1] = {0};
	char progname_UTF8[CRYPTIC_MAX_PATH + 1] = {0};

	if(GetModuleFileName(GetModuleHandle(NULL), progname, CRYPTIC_MAX_PATH))
	{
		WideToUTF8StrConvert(progname, SAFESTR(progname_UTF8));

		if(!strstri(progname_UTF8, "CrypticError"))
		{
			char CommandLine[1024];
			BOOL ret = FALSE;
			int iLocAttempt = 0;
			PROCESS_INFORMATION pi;
			STARTUPINFO si;

			for(iLocAttempt=0; iLocAttempt<ARRAY_SIZE(sppPossibleCrypticErrorLocations); iLocAttempt++)
			{
				sprintf(CommandLine, "%s%s.exe -deferred", sppPossibleCrypticErrorLocations[iLocAttempt], platformToExecutableSuffix());

				memset(&si, 0, sizeof(si));
				si.cb = sizeof(si);
				si.dwFlags |= STARTF_USESHOWWINDOW;
				si.wShowWindow = SW_HIDE;
				ret = CreateProcess_UTF8(NULL, CommandLine, NULL, NULL, 0, DETACHED_PROCESS, NULL, NULL, &si, &pi);
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);

				if(ret)
					break;
			}
		}
	}
#endif
}

typedef struct CESpawnArgs
{
	int errorType;
	const char *expression;
	const char *errortext;
	const char *stackdump;
	const char *pCallstackReport;
	const char *filename;
	int lineno;
	S32 didDisableAutoTimers;
	PEXCEPTION_POINTERS info;

	bool bXperfMode;
} CESpawnArgs;

static char sXperfFilepath[MAX_PATH];

LATELINK;
bool shouldSendDumps(void);

//By default, all servers should send dumps. Individual servers can override this function if they don't want to send anything.
bool DEFAULT_LATELINK_shouldSendDumps(void)
{
	return true;
}

// Internal function for ceSpawn(), used inside of an exception handler to ensure args->info is populated
/*static*/ int ceSpawnInternal(CESpawnArgs *args)
#if !PLATFORM_CONSOLE
{
	char CommandLine[1024];
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	BOOL ret;
	bool usefulStack = stackdumpIsUseful(args->stackdump);

	ceClear(); // clear any 
	// ------------------------------------------------------------------------------------------
	// Pointers to variables that CrypticError needs

	if(sCrashBeganEvent[0])
		ceAddStringPtr("beganevent", sCrashBeganEvent);

	if(sCrashCompletedEvent[0])
		ceAddStringPtr("completedevent", sCrashCompletedEvent);

	ceAddStringPtr("assertbuf", assertbuf);
	ceAddStringPtr("errortracker", getErrorTracker());
	ceAddInt("assertmode", g_assertmode);
	ceAddInt("errortype", args->errorType);
	ceAddStringPtr("platformname", PLATFORM_NAME);
	ceAddStringPtr("executablename", getExecutableName());
	ceAddStringPtr("productname", GetProductName());
	ceAddStringPtr("versionstring", GetUsefulVersionString());
	ceAddStringPtr("svnbranch", gBuildBranch);
	ceAddStringPtr("expression", args->expression);
	ceAddStringPtr("appglobaltype", GlobalTypeToName(GetAppGlobalType()));

	if (sAccountName[0])
		ceAddStringPtr("userwhogotit", STACK_SPRINTF("Account: %s", sAccountName));
	else
		ceAddStringPtr("userwhogotit", (char*)getUserName());

	ceAddStringPtr("errortext", args->errortext);

	if ((g_assertmode & ASSERTMODE_SENDCALLSTACK) || !usefulStack)
	{
		ceAddStringPtr("stackdata", args->pCallstackReport);
	}
	else
	{
		ceAddStringPtr("stackdata", args->stackdump);
	}

	ceAddStringPtr("sourcefile", args->filename);
	ceAddInt("sourcefileline", args->lineno);
	ceAddInt("clientcount", g_errorClientCount);
	ceAddStringPtr("trivia", g_errorTriviaString);

	ceAddStringPtr("memorydump", memory_dump_string);
	ceAddPtr("debugme", &sCrypticErrorDebugMe);
	ceAddPtr("exceptioninfo", args->info);
	ceAddInt("debugger", IsDebuggerPresent() ? 1 : 0);

	//in continuous builder-run apps, we always tell cryptic error we are a production mode server, so that 
	//dumps will get persisted to c:\dumps
	ceAddInt("servermode", sForceServerMode || g_isContinuousBuilder || assertForceDumps());
	ceAddInt("productionmode", isProductionMode()  || g_isContinuousBuilder || assertForceDumps());

	//data validation error tracking information to relay on to CrypticError
	ceAddInt("isvalidationerror", errorIsDuringDataLoadingGet());
	if (errorIsDuringDataLoadingGet())
		ceAddStringPtr("validationerrorfile", errorIsDuringDataLoadingGetFileName());

	ceAddStringPtr("shardinfostring", GetShardInfoString());

	ceAddInt("leaveCrashesUpForever", gbLeaveCrashesUpForever);

	ceAddInt("IsContinuousBuilder", g_isContinuousBuilder);

	if (StatusReporting_GetAllControllerTrackerNames() && StatusReporting_GetMyName())
	{
		ceAddStringPtr("CriticalSystem_MyName", StatusReporting_GetMyName());
		ceAddStringPtr("CriticalSystem_CTName", StatusReporting_GetAllControllerTrackerNames());
	}

	// ------------------------------------------------------------------------------------------
	// Execute CrypticError.exe

	{
		int iLocAttempt = 0;
		char *pAdditionalArgs = NULL;

		if (args->bXperfMode)
		{
			estrCopy2(&pAdditionalArgs, " -xperfMode");
			ceAddStringPtr("xperffile", sXperfFilepath);
		}
		else
		{
			estrCopy2(&pAdditionalArgs, "");
		}
		if (!shouldSendDumps())
			estrAppend2(&pAdditionalArgs, " -dontsenddumps");
		if (gbIgnoreProgrammerMode)
			estrAppend2(&pAdditionalArgs, " -ignoreprogrammermode 1");
		if (isProductionMode() && (getAssertMode() & ASSERTMODE_PASSPRODUCTIONMODE) != 0)
			estrAppend2(&pAdditionalArgs, " -productionMode");

		ret = FALSE;
		for(iLocAttempt=0; iLocAttempt<ARRAY_SIZE(sppPossibleCrypticErrorLocations); iLocAttempt++)
		{
			sprintf(CommandLine, "%s%s.exe %s%s", sppPossibleCrypticErrorLocations[iLocAttempt], platformToExecutableSuffix(), ceCalcArgs(), pAdditionalArgs);

			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			ret = CreateProcess_UTF8(NULL, CommandLine, NULL, NULL, 0, DETACHED_PROCESS, NULL, NULL, &si, &pi);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);

			if(ret)
				break;
		}
	}

	if (!ret)
		return 0;

	return 1;
}
#else
{ // XBOX code for UseCrypticError() ... should never be called as we cannot spawn CrypticError.exe on a 360
	return 0;
}
#endif

static char sXperfErrorText[1024];
int ceSpawnXperf(const char *errortext, const char *filename)
{
	int ret = 0;
	CESpawnArgs spawnArgs = {0};

	strcpy(sXperfErrorText, errortext);
	spawnArgs.errorType = ERRORDATATYPE_XPERF;
	spawnArgs.errortext = sXperfErrorText;
	spawnArgs.bXperfMode = true;
	strcpy(sXperfFilepath, filename);
	return ceSpawnInternal(&spawnArgs);
}

/*static*/ int ceSpawn(const char *expression, 
				   int errorType, 
				   const char *errortext, 
				   const char *stackdump, 
				   const char *pCallstackReport,
				   const char *filename,
				   int lineno,
				   S32 didDisableAutoTimers,
				   PEXCEPTION_POINTERS info,
				   int *pFinalRet)
#if !PLATFORM_CONSOLE
{
	int ret = 0;
	CESpawnArgs spawnArgs = {0};

	*pFinalRet = EXCEPTION_CONTINUE_EXECUTION;

	spawnArgs.expression           = expression;
	spawnArgs.errorType            = errorType;
	spawnArgs.errortext            = errortext;
	spawnArgs.stackdump            = stackdump;
	spawnArgs.pCallstackReport     = pCallstackReport;
	spawnArgs.filename             = filename;
	spawnArgs.lineno               = lineno;
	spawnArgs.didDisableAutoTimers = didDisableAutoTimers;
	spawnArgs.info                 = info;

	if(!spawnArgs.info)
	{
		__try 
		{
			// Raise dummy exception to get proper exception information.
			//RaiseException(0, 0, 0, 0);
			*((int*)0x00) = 1;
		}
#pragma warning(suppress:6320)
		__except((spawnArgs.info = GetExceptionInformation()),  (ret = ceSpawnInternal(&spawnArgs), EXCEPTION_EXECUTE_HANDLER))
#pragma warning(suppress:6322)		//Empty _except block...
		{
			// Pass through ceSpawn() return value
		}
	}
	else
		ret = ceSpawnInternal(&spawnArgs);

	if(ret)
	{
		// ceSpawnInternal() worked, wait for CE to either kill the process, or Debug/Ignore it

		bool bIgnore = false; // If true, we are going to try to ignore instead of debug

		while(!sCrypticErrorDebugMe && !g_listenThreadResponse)
		{
			// This is where a process that has successfully spawned CrypticError spins for eternity ...

			// He who binds to himself a joy
			// Does the winged life destroy;
			// But he who kisses the joy as it flies
			// Lives in eternity's sun rise. 

			Sleep(100);
		}

		if(sCrypticErrorDebugMe)
		{
			bIgnore = (sCrypticErrorDebugMe == 2);
		}
		else
		{
			if(g_listenThreadResponse == IDC_DEBUG+1)
				bIgnore = false;
			else if(g_listenThreadResponse == IDC_IGNORE+1)
				bIgnore = true;
		}

		sCrypticErrorDebugMe = 0;
		g_listenThreadResponse = 0;

		// ------------------------------------------------------------------
		// If we get here, someone pushed "Debug" in CrypticError

		if(!bIgnore)
		{
			*pFinalRet = EXCEPTION_CONTINUE_SEARCH;
 			g_ignoreexceptions = 1;	// dumb hack to make us fall into OS faster
				// This has a side effect of, if a debugger is attached, and we get an assert,
				// the next regular exception after our assert will skip CrypticError and go right
				// to VS.  This is probably fine (I'd rather the errors always go to VS if attached
				// anyway).
		}

		/* return to user code */

		autoTimerEnableRecursion(didDisableAutoTimers);
		ceClear(); // Undo all CrypticError variables so that we get fresh data if someone hits Ignore, and then crashes
		crashState(0); // Clear crash state
		LeaveEmergencyMallocBufferMode();
	}

	return ret;
}
#else
{
	return 0;
}
#endif

// -------------------------------------------------------------------------------------

int __cdecl superassertf(const char* expr, const char* errormsg_fmt, bool isFatalError, const char* filename, unsigned lineno, ...)
{
	char errormsg[1000] = "";
	va_list ap;

	va_start(ap, lineno);
	if (errormsg_fmt)
	{
		vsprintf(errormsg, errormsg_fmt, ap);
	}
	va_end(ap);

	return superassert(expr, errormsg, isFatalError, filename, lineno);
}


/***
*_assert() - Display a message and abort
*
*Purpose:
*       The assert macro calls this routine if the assert expression is
*       true.  By placing the assert code in a subroutine instead of within
*       the body of the macro, programs that call assert multiple times will
*       save space.
*
*Entry:
*
*Exit:	Returns EXCEPTION_CONTINUE_SEARCH if we want a breakpoint exception to be thrown
*		Returns EXCEPTION_CONTINUE_EXECUTION if we want to coniue
*
*Exceptions:
*
*******************************************************************************/

int __cdecl superassert(const char* expr, const char* errormsg, bool isFatalError, const char* filename, unsigned lineno)
#if !PLATFORM_CONSOLE
{
	/*
	* Build the assertion message, then write it out. The exact form
	* depends on whether it is to be written out via stderr or the
	* MessageBox API.
	*/
	int nCode = IDABORT;
	char * pch;
	char stackdump[ASSERT_BUF_SIZE] = "";
	char progname[CRYPTIC_MAX_PATH + 1] = "";
	char curtime[100] = "";
	char processID[100] = "";
	char expression[512] = "";
	char errortext[1024] = "";
	char cWindowsErrorMessage[1000];
	DWORD dLastError = GetLastError();
	char pCallstackReport[ASSERT_BUF_SIZE] = "";
	S32 didDisableAutoTimers = 0;
	S32 crashNum = InterlockedIncrement(&iNumCrashes);

	if (sbFreezeOnCrash)
		assertFreezeAllThreadsOnCrash();

	//if we're still super-early in AUTO_RUNs, can't do triviaPrintf because it
	//requires a parsetable to have been initialized already
	if (giCurAutoRunStep >= 3)
	{
		triviaPrintf("NumCrashes", "%d", crashNum);
	}

	EnterEmergencyMallocBufferMode();

	crashState(1);  // Set crash state
	assertbuf_filled = 0;
	dumpsWritten = 0;


	printf("%s PROGRAM ASSERT OCCURRED! %s\n", timeGetLocalDateString(), errormsg?errormsg:expr); // Must be before assertFreezeAllOtherThreads
	memlog_printf(0, "Assertion occured: %s", errormsg?errormsg:expr);

	if(g_assertSndCB)
	{
		static S32 noRecurse;
		if(!noRecurse)
		{
			noRecurse = 1;
			g_assertSndCB();  // Kill sounds so crashing doesn't get too annoying.
			noRecurse = 0;
		}
	}

	if ( dLastError )
	{
		S16 wideWindowsErrorMessage[1000];
		if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dLastError, 0, wideWindowsErrorMessage, 1000, NULL))
		{
			WideToUTF8StrConvert(wideWindowsErrorMessage, SAFESTR(cWindowsErrorMessage));
		}
		else
		{
			sprintf_s(SAFESTR(cWindowsErrorMessage), "Error code: %d\n", dLastError);
		}

	}

	autoTimerDisableRecursion(&didDisableAutoTimers);

	closeSocketsOnAssert();

	generateVirtualMemoryLayout();
	if (memory_dump_string)
	{
		virtualMemoryMakeStatsString(&g_vmStats, SAFESTR(errortext));
		estrConcat(&memory_dump_string, errortext, (int)strlen(errortext));
		errortext[0] = '\0';
	}

//	ShowWindow(compatibleGetConsoleWindow(), SW_SHOW);

	/*
	* Line 1: box intro line
	*/
	strcpy_trunc( assertbuf, BOXINTRO );
	strcat_trunc( assertbuf, dblnewline );

	/*
	* Line 2: program line
	*/
	strcat_trunc( assertbuf, PROGINTRO );

	progname[CRYPTIC_MAX_PATH] = '\0';

	{
		S16 wideprogname[CRYPTIC_MAX_PATH + 1] = {0};
		if ( GetModuleFileName( NULL, wideprogname, CRYPTIC_MAX_PATH ))
		{
			WideToUTF8StrConvert(wideprogname, SAFESTR(progname));
		}
		else
		{
			strcpy( progname, "<program name unknown>");
		}

	}

	pch = (char *)progname;

	/* sizeof(PROGINTRO) includes the NULL terminator */
	if ( sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ > MAXLINELEN )
	{
		size_t offset = (sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ) - MAXLINELEN;
		size_t bufsize = ARRAY_SIZE_CHECKED(progname) - offset;
		pch += offset;
		strncpy_s( pch, bufsize, dotdotdot, DOTDOTDOTSZ );
	}
	
	strcat_trunc( assertbuf, pch );
	strcat_trunc( assertbuf, newline );

	if(strlen(versionbuf))
	{
		strcat_trunc( assertbuf, VERINTRO );
		strcat_trunc( assertbuf, versionbuf );
		strcat_trunc( assertbuf, newline );
	}

	/*
	* Line 2.5: svn revision
	*/
	strcat_trunc( assertbuf, "SVN Revision: " );
	strcat_trunc( assertbuf, GetUsefulVersionString() );
	strcat_trunc( assertbuf, newline );

	/*
	* Line 3: time
	*/
	
	sprintf_s(SAFESTR(curtime), "%s%s\n", TIMEINTRO, TimeStr() );
	strcat_trunc( assertbuf, curtime );


	/*
	* Line 5: process ID
	*/

	sprintf_s(SAFESTR(processID), "%s%d\n", PROCESSID, _getpid() );
	strcat_trunc( assertbuf, processID );

	/*
	* Line 6: file line
	*/
	strcat_trunc( assertbuf, FILEINTRO );

	/* sizeof(FILEINTRO) includes the NULL terminator */
	if ( sizeof(FILEINTRO) + strlen(filename) + NEWLINESZ > MAXLINELEN )
	{
		size_t p, len, ffn;

		pch = (char *) filename;
		ffn = MAXLINELEN - sizeof(FILEINTRO) - NEWLINESZ;

		for ( len = strlen(filename), p = 1;
			pch[len - p] != '\\' && pch[len - p] != '/' && p < len;
			p++ );

		/* keeping pathname almost 2/3rd of full filename and rest
		* is filename
		*/
		if ( (ffn - ffn/3) < (len - p) && ffn/3 > p )
		{
			/* too long. using first part of path and the
			filename string */
			strncat( assertbuf, pch, ffn - DOTDOTDOTSZ - p );
			strcat_trunc( assertbuf, dotdotdot );
			strcat_trunc( assertbuf, pch + len - p );
		}
		else if ( ffn - ffn/3 > len - p )
		{
			/* pathname is smaller. keeping full pathname and putting
			* dotdotdot in the middle of filename
			*/
			p = p/2;
			strncat( assertbuf, pch, ffn - DOTDOTDOTSZ - p );
			strcat_trunc( assertbuf, dotdotdot );
			strcat_trunc( assertbuf, pch + len - p );
		}
		else
		{
			/* both are long. using first part of path. using first and
			* last part of filename.
			*/
			strncat( assertbuf, pch, ffn - ffn/3 - DOTDOTDOTSZ );
			strcat_trunc( assertbuf, dotdotdot );
			strncat( assertbuf, pch + len - p, ffn/6 - 1 );
			strcat_trunc( assertbuf, dotdotdot );
			strcat_trunc( assertbuf, pch + len - (ffn/3 - ffn/6 - 2) );
		}

	}
	else
		/* plenty of room on the line, just append the filename */
		strcat_trunc( assertbuf, filename );

	strcat_trunc( assertbuf, newline );

	/*
	* Line 7: line line
	*/
	strcat_trunc( assertbuf, LINEINTRO );
	_itoa_s( lineno, assertbuf + strlen(assertbuf), ARRAY_SIZE_CHECKED(assertbuf) - strlen(assertbuf), 10 );
	strcat_trunc( assertbuf, newline );


	/*
	* Line 8: message line
	*/
	strcat_trunc( assertbuf, EXPRINTRO );

	strcat_trunc( assertbuf, expr );
	strcpy_trunc(expression, expr);

	strcat_trunc( assertbuf, dblnewline );

	
	if(errormsg){
		strcat_trunc(assertbuf, MSGINTRO);
		strcat_trunc(assertbuf, errormsg);
		strcat_trunc(assertbuf, newline);

		strcat_trunc(errortext, errormsg);
	}

	/*
	 * Print the stack
	 */

	assertFreezeAllOtherThreads(0); // Freeze other threads before walking the stack to avoid starving the stackwalk thread
	stackWalkDumpStackToBuffer(SAFESTR(stackdump), NULL, NULL, NULL, pCallstackReport);
	assertFreezeAllOtherThreads(1); // ... and resume them afterwards. We might need other threads for a bit (such as a WSA background thread in a call to ip2str)

	// Print the callstack report, if requested.
	if (gbPrintCallstackReport)
		printf("%s", pCallstackReport);

	if (assertCallback)
	{
		assertCallback(assertbuf);
	}

	//if we're asserting during AUTO_RUNs or something, we want to communicate with errortracker and stuff
	if (!assertInitialized)
	{
		setDefaultAssertMode();
	}

	if(getAssertMode() & ASSERTMODE_USECRYPTICERROR)
	{
		int ret;
		int finalRet;

		// Fill in the rest of the assertbuf so that the listenThread can get at it and show up in CrypticError
		strcat_trunc(assertbuf, newline);
		strcat_trunc(assertbuf, stackdump);
		strcat_trunc(assertbuf, newline );
		addExtraInfo(assertbuf, ARRAY_SIZE(assertbuf));
		if ( dLastError )
		{
			strcat_trunc(assertbuf, "Last windows SYSTEM error: ");
			strcat_trunc(assertbuf, cWindowsErrorMessage);
		}
		else
		{
			strcat_trunc(assertbuf, "No windows SYSTEM error code found.\n");
		}

		if(isDevelopmentMode())
			listenThreadBegin();
		ret = ceSpawn(expression, isFatalError ? ERRORDATATYPE_FATALERROR : ERRORDATATYPE_ASSERT, errortext, stackdump, pCallstackReport, filename, lineno, didDisableAutoTimers, NULL, &finalRet);
		if(ret)
		{
			return finalRet;
		}
	}

	writeAssertRequestedDumps(NULL);

	if (assertInitialized)
	{
		ErrorData errorData = {0};
		NOCONST(TriviaList) tList = {0};
		bool usefulStack = stackdumpIsUseful(stackdump);

		errorData.eType = isFatalError ? ERRORDATATYPE_FATALERROR : ERRORDATATYPE_ASSERT;

		// About the executable itself
		errorData.pPlatformName = PLATFORM_NAME;
		errorData.pExecutableName = getExecutableName();
		errorData.pProductName = GetProductName();
		errorData.pVersionString = (char*)GetUsefulVersionString();
		errorData.pSVNBranch = gBuildBranch;
		errorData.pAppGlobalType = GlobalTypeToName(GetAppGlobalType());

		// Basic error info
		errorData.pExpression = expression;
		if (sAccountName[0])
			errorData.pUserWhoGotIt = STACK_SPRINTF("A:%s", sAccountName);
		else
			errorData.pUserWhoGotIt = STACK_SPRINTF("W:%s", (char*)getUserName());
		errorData.pErrorString = errortext;
		if ((g_assertmode & ASSERTMODE_SENDCALLSTACK) || !usefulStack)
		{
			errorData.pStackData = pCallstackReport;
		}
		else
		{
			errorData.pStackData = stackdump;
		}


		errorData.pSourceFile = (char*)filename;
		errorData.iSourceFileLine = lineno;

		// Data file involved (if applicable)
		//errorData.pDataFile;
		//errorData.iDataFileModificationTime;

		// Lesser information (more for bugfix prioritizing than for error info)
		errorData.iClientCount = g_errorClientCount;
		
		tList.triviaDatas = (NOCONST(TriviaData)**) triviaGlobalGet();
		errorData.pTriviaList = (TriviaList*) &tList;

		errorData.iProductionMode = isProductionMode();

		errorTrackerSendError(&errorData);
		triviaGlobalRelease();

		if(errorTrackerGetUniqueID() != 0)
		{
			char idbuffer[32];
			sprintf_s(SAFESTR(idbuffer), "%d", errorTrackerGetUniqueID());
			strcat_trunc( assertbuf, CRASHINTRO );
			strcat_trunc( assertbuf, idbuffer );
			strcat_trunc( assertbuf, newline );
			strcat_trunc( assertbuf, CRASHINTRO2 );
			strcat_trunc( assertbuf, idbuffer );
			strcat_trunc( assertbuf, newline );

			if (!usefulStack && !errorTrackerGetErrorResponse() && errorTrackerGetErrorMessage() && strlen(errorTrackerGetErrorMessage()))
			{
				strcpy(stackdump, errorTrackerGetErrorMessage());
			}

			if (memory_dump_string)
			{
				errorTrackerSendMemoryDump(errorTrackerGetUniqueID(), memory_dump_string, estrLength(&memory_dump_string));
			}
		}
	}
	
	if (ASSERT_SHOW_INTERNAL)
	{
		// Append stackdump after sending error, to the end of the string
		//if (!(g_assertmode & ASSERTMODE_NODEBUGBUTTONS))
		//{
			// do not show callstack if ASSERTMODE_NODEBUGBUTTONS
		//}
		strcat_trunc(assertbuf, newline);
		strcat_trunc(assertbuf, stackdump);
		strcat_trunc(assertbuf, newline );

		// Extra info
		addExtraInfo(assertbuf, ARRAY_SIZE(assertbuf));

		/*
		* Line 4: windows error?
		*/
		if ( dLastError )
		{
			strcat_trunc(assertbuf, "Last windows SYSTEM error: ");
			strcat_trunc(assertbuf, cWindowsErrorMessage);
		}
		else
		{
			strcat_trunc(assertbuf, "No windows SYSTEM error code found.\n");
		}
	}
	else
	{
		// TODO move this after the initial error sending, send extra data if flagged as external connection by Error Tracker
		nCode = DialogBoxIndirect(winGetHInstance(), (LPDLGTEMPLATE)DialogResource2, NULL, ExternalAssert); // TODO
		if (nCode == IDOK)
		{
		}
		else if (nCode == IDCANCEL)
		{
			sExternalReturnBuffer[0] = 0;
		}
		nCode = IDABORT;
	}

	listenThreadBegin(); // Needs to be before assertCallback

	sharedHeapEmergencyMutexRelease();

	// Write dumps *after* starting the listenThread, so that the DbServer is notified immediately upon MapServer crash

	writeAndSendDumps(assertbuf, NULL);

	if (spAuxAssertCB)
	{
		spAuxAssertCB(expr, errormsg, filename, lineno);
	}

	if (isProductionMode() && (g_assertmode & ASSERTMODE_ISEXTERNALAPP) == 0)
	{
		FILE *file = fopen(assertGetLogFilename(), "w");
		if (file)
		{
			fprintf(file, "%s", assertbuf);
			fclose(file);
		}
	}

	/* Abort: abort the program */
	logWaitForQueueToEmpty();
	/* raise abort signal */
	raise(SIGABRT);
	/* We usually won't get here, but it's possible that
	SIGABRT was ignored.  So exit the program anyway. */
	_exit(3);
	return 0;
}
#else
{
	ErrorData errorData = {0};

	EnterEmergencyMallocBufferMode();




	generateVirtualMemoryLayout();

	errorData.eType = isFatalError ? ERRORDATATYPE_FATALERROR : ERRORDATATYPE_ASSERT;

	// About the executable itself
	errorData.pPlatformName = PLATFORM_NAME;
	errorData.pExecutableName = getExecutableName();
	errorData.pProductName = GetProductName();
	errorData.pVersionString = (char*)GetUsefulVersionString();

	// Basic error info
	errorData.pErrorString = (char*)errormsg;
	if (sAccountName[0])
		errorData.pUserWhoGotIt = STACK_SPRINTF("Account: %s", sAccountName);
	else {
#if _PS3
		errorData.pUserWhoGotIt = STACK_SPRINTF("P:%s", (char*)getUserName());
#else
		errorData.pUserWhoGotIt = STACK_SPRINTF("X:%s", (char*)getUserName());
#endif
	}
	errorData.pSourceFile = (char*)filename;
	errorData.iSourceFileLine = lineno;

	// Data file involved (if applicable)
	//errorData.pDataFile;
	//errorData.iDataFileModificationTime;

	// Lesser information (more for bugfix prioritizing than for error info)
	errorData.iClientCount = g_errorClientCount;
	errorData.pTrivia = g_errorTriviaString;

	errorData.iProductionMode = isProductionMode();
	errorData.pExpression     = (char*)expr;

	callstackWriteTextReport(SAFESTR(assertbuf));
	errorData.pStackData      = assertbuf;

	errorTrackerSendError(&errorData);

	if (errorTrackerGetUniqueID() != 0 && memory_dump_string)
		errorTrackerSendMemoryDump(errorTrackerGetUniqueID(), memory_dump_string, estrLength(&memory_dump_string));

	// TODO: Somehow display errorTrackerGetUniqueID() to the user, if non-zero

	errorTrackerWriteDumpCache(&errorData);

	if (spAuxAssertCB)
	{
		spAuxAssertCB(expr, errormsg, filename, lineno);
	}

	LeaveEmergencyMallocBufferMode();

#if _XBOX
	if (g_isContinuousBuilder)
	{
		DmCrashDump(false);
		exit(-1);
		return 0;
	}
#endif

	DebugBreak();
	return 0;
}
#endif

// exception table for looking up our structured exception
#define EXCEPTION_NAME(x) { x, #x },
typedef struct ExceptionName {
	DWORD code;
	char* name;
} ExceptionName;
ExceptionName g_exceptnames[] = {
	EXCEPTION_NAME(EXCEPTION_ACCESS_VIOLATION)
	EXCEPTION_NAME(EXCEPTION_DATATYPE_MISALIGNMENT)
	EXCEPTION_NAME(EXCEPTION_BREAKPOINT)
	EXCEPTION_NAME(EXCEPTION_SINGLE_STEP)
	EXCEPTION_NAME(EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
	EXCEPTION_NAME(EXCEPTION_FLT_DENORMAL_OPERAND)
	EXCEPTION_NAME(EXCEPTION_FLT_DIVIDE_BY_ZERO)
	EXCEPTION_NAME(EXCEPTION_FLT_INEXACT_RESULT)
	EXCEPTION_NAME(EXCEPTION_FLT_INVALID_OPERATION)
	EXCEPTION_NAME(EXCEPTION_FLT_OVERFLOW)
	EXCEPTION_NAME(EXCEPTION_FLT_STACK_CHECK)
	EXCEPTION_NAME(EXCEPTION_FLT_UNDERFLOW)
	EXCEPTION_NAME(STATUS_FLOAT_MULTIPLE_TRAPS)
	EXCEPTION_NAME(STATUS_FLOAT_MULTIPLE_FAULTS)
	EXCEPTION_NAME(EXCEPTION_INT_DIVIDE_BY_ZERO)
	EXCEPTION_NAME(EXCEPTION_INT_OVERFLOW)
	EXCEPTION_NAME(EXCEPTION_PRIV_INSTRUCTION)
	EXCEPTION_NAME(EXCEPTION_IN_PAGE_ERROR)
	EXCEPTION_NAME(EXCEPTION_ILLEGAL_INSTRUCTION)
	EXCEPTION_NAME(EXCEPTION_NONCONTINUABLE_EXCEPTION)
	EXCEPTION_NAME(EXCEPTION_STACK_OVERFLOW)
	EXCEPTION_NAME(EXCEPTION_INVALID_DISPOSITION)
	EXCEPTION_NAME(EXCEPTION_GUARD_PAGE)
	EXCEPTION_NAME(EXCEPTION_INVALID_HANDLE)
};

static S32 isFPException(DWORD code){
	return	code == EXCEPTION_FLT_DENORMAL_OPERAND ||
			code == EXCEPTION_FLT_DIVIDE_BY_ZERO ||
			code == EXCEPTION_FLT_INEXACT_RESULT ||
			code == EXCEPTION_FLT_INVALID_OPERATION ||
			code == EXCEPTION_FLT_OVERFLOW ||
			code == EXCEPTION_FLT_STACK_CHECK ||
			code == EXCEPTION_FLT_UNDERFLOW ||
			code == STATUS_FLOAT_MULTIPLE_TRAPS ||
			code == STATUS_FLOAT_MULTIPLE_FAULTS;
}

char* GetExceptionName(unsigned long code)
{
	int i;
	static char buf[100];
	for (i = 0; i < ARRAY_SIZE(g_exceptnames); i++)
	{
		if (g_exceptnames[i].code == code)
			return g_exceptnames[i].name;
	}
	sprintf_s(SAFESTR(buf), "%x", code);
	return buf;
}

static int AdvancePastDivideInstruction(CONTEXT* co)
{
#pragma warning (push)
#pragma warning (disable: 4312) // warning about 64-bit, but this code is only defined for x86 anyway
#if defined(_X86_)
	LPBYTE ip = (LPBYTE)co->Eip;
	BYTE advance = 2; // initial bytecode + mod R/M byte
	BYTE mod, rm, base;

	// we only recognize DIV/IDIV initial byte:
	if (ip[0] != 0xF6 && ip[0] != 0xF7)
		return 0;
	
	// break down mod R/M & SIB bytes to get length
	mod = (ip[1] & 0xC0) >> 6;
	rm = (ip[1] & 0x07);
	base = (ip[2] & 0x07); 
	switch (mod)
	{
	case 3:	break;	// referring to register
	case 2: 
		advance += 4;	// 32-bit displacement
		if (rm == 4)
			advance += 1; // SIB byte
		break;
	case 1:
		advance += 1;	// 8-bit displacement
		if (rm == 4)
			advance += 1; // SIB byte
		break;
	case 0:
		if (rm == 5)
			advance += 4; // 32-bit displacement
		else if (rm == 4)
		{
			advance += 1; // SIB byte
			if (base == 5)
				advance += 4; // 32-bit displacement
		}
		break;
	default:
		return 0;
	}

	// just going to set the result to INT_MAX in the interest
	// of causing a detectable error
	co->Eax = 0x7fffffff;

	co->Eip += advance;
	return 1;

#else // !_X86_
	return 0;
#endif
#pragma warning (pop)
}

#if !PLATFORM_CONSOLE
static void removeFPExceptions(PEXCEPTION_POINTERS info)
{
#ifndef _WIN64
	// Disable exceptions in the x87 control word.
	
	info->ContextRecord->FloatSave.ControlWord |= _MCW_EM;
	
	// Unflag all the active exceptions in the x87 status word.
	
	info->ContextRecord->FloatSave.StatusWord &= ~_MCW_EM;
	
	// Disable exceptions in the SSE register MXCSR.

	info->ContextRecord->ExtendedRegisters[24] |= BIT(7);
	info->ContextRecord->ExtendedRegisters[25] |= BIT_RANGE(0, 4);

	// Unflag all the active exceptions in the SSE register MXCSR.
	
	info->ContextRecord->ExtendedRegisters[24] &= ~BIT_RANGE(0, 5);
#endif
}
#endif


// handle a structure exception, we do the same thing as
// for an assert, except that the message is built differently
int assertExcept(unsigned int code, PEXCEPTION_POINTERS info, void *tib, void *boundingFramePointer)
#if !PLATFORM_CONSOLE
{
	int nCode = EXCEPTION_EXECUTE_HANDLER;
	char * pch;
	char stackdump[ASSERT_BUF_SIZE] = "";
	char progname[CRYPTIC_MAX_PATH + 1] = "";
	char curtime[100] = "";
	char processID[100] = "";
	char errortext[1024] = "";
	static int handlingAssertion = 0;
	int recursiveCall = 0;	// is this copy a recursive child?
	DWORD dLastError = GetLastError();
	char cWindowsErrorMessage[1000];
	char pCallstackReport[ASSERT_BUF_SIZE] = "";
	S32 didDisableAutoTimers = 0;
	S32 crashNum = InterlockedIncrement(&iNumCrashes);

	if (sbFreezeOnCrash)
		assertFreezeAllThreadsOnCrash();

	//if we're still super-early in AUTO_RUNs, can't do triviaPrintf because it
	//requires a parsetable to have been initialized already
	if (giCurAutoRunStep >= 3)
	{
		triviaPrintf("NumCrashes", "%d", crashNum);
	}
	// Clear the FPU exception so it doesn't recurse.

	_clearfp();
	disableFPExceptions(1);

	EnterEmergencyMallocBufferMode();

	crashState(1);  // Set crash state.
	assertbuf_filled = 0;
	dumpsWritten = 0;

	if ( dLastError )
	{
		S16 wideWindowsErrorMessage[1000];
		if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dLastError, 0, wideWindowsErrorMessage, 1000, NULL))
		{
			WideToUTF8StrConvert(wideWindowsErrorMessage, SAFESTR(cWindowsErrorMessage));
		}
		else
		{
			sprintf_s(SAFESTR(cWindowsErrorMessage), "Error code: %d\n", dLastError);
		}
	}

	autoTimerDisableRecursion(&didDisableAutoTimers);

	printf("%s PROGRAM CRASH OCCURRED!\n", timeGetLocalDateString()); // Must be before assertFreezeAllOtherThreads

	assertFreezeAllOtherThreads(0);

	if(!isFPException(code)){
		closeSocketsOnAssert();
	}

	generateVirtualMemoryLayout();

	// MAK - why do hardware engineers feel that div-zero is so important?

	if (g_ignoredivzero && code == EXCEPTION_INT_DIVIDE_BY_ZERO)
	{
		if (AdvancePastDivideInstruction(info->ContextRecord))
		{
			assertFreezeAllOtherThreads(1);
			autoTimerEnableRecursion(didDisableAutoTimers);
			crashState(0);  // Clear crash state.
			removeFPExceptions(info);
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		// otherwise, we couldn't recognize the instruction.. fall through
	}

	if (programIsShuttingDown())
	{
		assertFreezeAllOtherThreads(1);
		autoTimerEnableRecursion(didDisableAutoTimers);
		crashState(0);  // Clear crash state.
		removeFPExceptions(info);
		return IsDebuggerPresent()?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER;
	}

	// detect recursion
	if (handlingAssertion) recursiveCall = 1;
	handlingAssertion = 1;

	// if we got called by assert, just fall through instead of popping again
	if (g_ignoreexceptions)
	{
		g_ignoreexceptions = 0;
		handlingAssertion = 0;
		assertFreezeAllOtherThreads(1);
		autoTimerEnableRecursion(didDisableAutoTimers);
		crashState(0);  // Clear crash state.
		removeFPExceptions(info);
		return EXCEPTION_CONTINUE_SEARCH; // fall through
	}

	if (!assertbuf_filled) {
		memlog_printf(0, "Exception caught: %s", GetExceptionName(code));
		/*
		* Line 0: exception line
		*/
		sprintf_s(SAFESTR(assertbuf), "Exception caught: %s\n", GetExceptionName(code));
		sprintf_s(SAFESTR(errortext), "Exception caught: %s\n", GetExceptionName(code));

		/*
		* Line 1: program line
		*/
		strcat_trunc( assertbuf, PROGINTRO );

		progname[CRYPTIC_MAX_PATH] = '\0';
		{
			S16 wideprogname[CRYPTIC_MAX_PATH + 1] = {0};
			if ( GetModuleFileName( NULL, wideprogname, CRYPTIC_MAX_PATH ))
			{
				WideToUTF8StrConvert(wideprogname, SAFESTR(progname));
			}
			else
			{
				strcpy( progname, "<program name unknown>");
			}
		}

		pch = (char *)progname;

		/* sizeof(PROGINTRO) includes the NULL terminator */
		if ( sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ > MAXLINELEN )
		{
			size_t offset = (sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ) - MAXLINELEN;
			size_t bufsize = ARRAY_SIZE_CHECKED(progname) - offset;
			pch += offset;
			strncpy_s( pch, bufsize, dotdotdot, DOTDOTDOTSZ );
		}

		strcat_trunc( assertbuf, pch );
		strcat_trunc( assertbuf, newline );

		if(strlen(versionbuf))
		{
			strcat_trunc( assertbuf, VERINTRO );
			strcat_trunc( assertbuf, versionbuf );
			strcat_trunc( assertbuf, newline );
		}

		/*
		* Line 1.5: svn revision
		*/
		strcat_trunc( assertbuf, "SVN Revision: " );
		strcat_trunc( assertbuf, GetUsefulVersionString() );
		strcat_trunc( assertbuf, newline );

		/*
		* Line 2: time
		*/

		sprintf_s(SAFESTR(curtime), "%s%s\n", TIMEINTRO, TimeStr() );
		strcat_trunc( assertbuf, curtime );

		/*
		* Line 3: process ID
		*/

		sprintf_s(SAFESTR(processID), "%s%d\n", PROCESSID, _getpid() );
		strcat_trunc( assertbuf, processID );
		strcat_trunc( assertbuf, newline );

		/*
		* Print the stack
		*/
		stackWalkDumpStackToBuffer(SAFESTR(stackdump), info->ContextRecord, tib, boundingFramePointer, pCallstackReport);

		assertbuf_filled = 1;
	} else {
		assertbuf_filled = 0;
	}

	if (assertCallback)
	{
		assertCallback(assertbuf);
	}

	if (!assertInitialized)
	{
		setDefaultAssertMode();
	}

	// Print the callstack report, if requested.
	if (gbPrintCallstackReport)
		printf("%s", pCallstackReport);

#if !_XBOX
	if(getAssertMode() & ASSERTMODE_USECRYPTICERROR)
	{
		int finalRet;
		int ret;

		// Fill in the rest of the assertbuf so that the listenThread can get at it and show up in CrypticError
		strcat_trunc(assertbuf, newline);
		strcat_trunc(assertbuf, stackdump);
		strcat_trunc(assertbuf, newline );
		addExtraInfo(assertbuf, ARRAY_SIZE(assertbuf));
		if ( dLastError )
		{
			strcat_trunc(assertbuf, "Last windows SYSTEM error: ");
			strcat_trunc(assertbuf, cWindowsErrorMessage);
		}
		else
		{
			strcat_trunc(assertbuf, "No windows SYSTEM error code found.\n");
		}

		if (isDevelopmentMode())
			listenThreadBegin();

		ret = ceSpawn("", ERRORDATATYPE_CRASH, errortext, stackdump, pCallstackReport, "", 0, didDisableAutoTimers, info, &finalRet);
		if(ret)
		{
			assertFreezeAllOtherThreads(1);
			removeFPExceptions(info);
			return finalRet;
		}
	}
#endif

	writeAssertRequestedDumps(info);

	{
		ErrorData errorData = {0};
		NOCONST(TriviaList) tList = {0};
		bool usefulStack = stackdumpIsUseful(stackdump);

		errorData.eType = ERRORDATATYPE_CRASH;

		// About the executable itself
		errorData.pPlatformName = PLATFORM_NAME;
		errorData.pExecutableName = getExecutableName();
		errorData.pProductName = GetProductName();
		errorData.pVersionString = (char*)GetUsefulVersionString();
		errorData.pSVNBranch = gBuildBranch;
		errorData.pAppGlobalType = GlobalTypeToName(GetAppGlobalType());

		// Basic error info
		if (sAccountName[0])
			errorData.pUserWhoGotIt = STACK_SPRINTF("A:%s", sAccountName);
		else
			errorData.pUserWhoGotIt = STACK_SPRINTF("W:%s", (char*)getUserName());
		errorData.pErrorString = errortext;
		if ((g_assertmode & ASSERTMODE_SENDCALLSTACK) || !usefulStack)
		{
			errorData.pStackData = pCallstackReport;
		}
		else
		{
			errorData.pStackData = stackdump;
		}

		//errorData.pSourceFile;
		//errorData.iSourceFileLine;

		// Data file involved (if applicable)
		//errorData.pDataFile;
		//errorData.iDataFileModificationTime;

		// Lesser information (more for bugfix prioritizing than for error info)
		errorData.iClientCount = g_errorClientCount;

		tList.triviaDatas = (NOCONST(TriviaData)**) triviaGlobalGet();
		errorData.pTriviaList = (TriviaList*) &tList;

		errorData.iProductionMode = isProductionMode();

		assertFreezeAllOtherThreads(1);
		errorTrackerSendError(&errorData);
		triviaGlobalRelease();

		if(errorTrackerGetUniqueID() != 0)
		{
			char idbuffer[32];
			sprintf_s(SAFESTR(idbuffer), "%d", errorTrackerGetUniqueID());
			strcat_trunc( assertbuf, CRASHINTRO );
			strcat_trunc( assertbuf, idbuffer );
			strcat_trunc( assertbuf, newline );
			strcat_trunc( assertbuf, CRASHINTRO2 );
			strcat_trunc( assertbuf, idbuffer );
			strcat_trunc( assertbuf, newline );

			if (!usefulStack && !errorTrackerGetErrorResponse() && errorTrackerGetErrorMessage() && strlen(errorTrackerGetErrorMessage()))
			{
				strcpy(stackdump, errorTrackerGetErrorMessage());
			}
		}
	}
	
	if (ASSERT_SHOW_INTERNAL)
	{
		if (assertbuf_filled)
		{
			assertbuf_filled = 0;

			// Append stackdump after sending error, to the end of the string
			strcat_trunc( assertbuf, newline );
			strcat_trunc(assertbuf, stackdump);
			strcat_trunc( assertbuf, newline );

			// Extra info
			addExtraInfo(assertbuf, ARRAY_SIZE(assertbuf));

			/*
			* Line 4: windows error?
			*/
			if ( dLastError )
			{
				strcat_trunc(assertbuf, "Last windows SYSTEM error: ");
				strcat_trunc(assertbuf, cWindowsErrorMessage);
			}
			else
			{
				strcat_trunc(assertbuf, "No windows SYSTEM error code found.\n");
			}
		}
	}
	else
	{
		// TODO move this after the initial error sending, send extra data if flagged as external connection by Error Tracker
		nCode = DialogBoxIndirect(winGetHInstance(), (LPDLGTEMPLATE)DialogResource2, NULL, ExternalAssert); // TODO
		if (nCode == IDOK)
		{
			//printf("%s\n", sExternalReturnBuffer);
		}
		else if (nCode == IDCANCEL)
		{
			sExternalReturnBuffer[0] = 0;
		}
		nCode = EXCEPTION_EXECUTE_HANDLER;
	}

	if(!(g_assertmode & ASSERTMODE_NOERRORTRACKER))
	{
		writeAndSendDumps(assertbuf, info);
	}
	assertFreezeAllOtherThreads(0);

	g_disableIgnore = 1;

	// otherwise, do assert dialog
	listenThreadBegin();	// Start listening for a RemoteDebuggerer so that
							// it will work while waiting in the error report window.

	sharedHeapEmergencyMutexRelease();

	// Freeze all threads at this point!
	// Done above: assertFreezeAllOtherThreads(0);

	if (isProductionMode() && (g_assertmode & ASSERTMODE_ISEXTERNALAPP) == 0)
	{
		FILE *file = fopen(assertGetLogFilename(), "w");
		if (file)
		{
			fprintf(file, "%s", assertbuf);
			fclose(file);
		}
	}
	logWaitForQueueToEmpty();
	raise(SIGABRT);
	_exit(3);
}
#else
{
	ErrorData errorData = {0};

	printf("Xbox assertExcept (%s CB)\n", g_isContinuousBuilder ? "IS" : "IS NOT");

	errorData.eType = ERRORDATATYPE_CRASH;

	// About the executable itself
	errorData.pPlatformName = PLATFORM_NAME;
	errorData.pExecutableName = getExecutableName();
	errorData.pProductName = GetProductName();
	errorData.pVersionString = (char*)GetUsefulVersionString();

	// Basic error info
	//errorData.pErrorString;
	if (sAccountName[0])
		errorData.pUserWhoGotIt = STACK_SPRINTF("A:%s", sAccountName);
	else
		errorData.pUserWhoGotIt = STACK_SPRINTF("X:%s", (char*)getUserName());
	//errorData.pSourceFile;
	//errorData.iSourceFileLine;

	// Data file involved (if applicable)
	//errorData.pDataFile;
	//errorData.iDataFileModificationTime;

	// Lesser information (more for bugfix prioritizing than for error info)
	errorData.iClientCount = g_errorClientCount;
	errorData.pTrivia = g_errorTriviaString;

	errorData.iProductionMode = isProductionMode();
	//errorData.pExpression;

	callstackWriteTextReport(SAFESTR(assertbuf));
	errorData.pStackData      = assertbuf;

	errorTrackerSendError(&errorData);

	// TODO: Somehow display errorTrackerGetUniqueID() to the user, if non-zero

	errorTrackerWriteDumpCache(&errorData);

	//we always want Continuous Builders to exit and crash normally so we can get the dump and stuff
#if _XBOX
	if (g_isContinuousBuilder)
	{
		DmCrashDump(false);
		exit(-1);

		printf("Done with Xbox assertExcept\n");

		return EXCEPTION_CONTINUE_SEARCH;
	}
#endif
	DebugBreak();


	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static const char *assertGetLogFilename(void)
{
#ifdef _XBOX
	return "";
#else
	static char filename[CRYPTIC_MAX_PATH] = ""; // This has to be static, we pass it to another thread!
	char *s,datestr[1000];
	//char modulepath[MAX_PATH];
	char *modulename = NULL;

	if(filename[0] != 0)
		return filename;

	timeMakeDateStringFromSecondsSince2000(datestr,timeSecondsSince2000());
	for(s=datestr;*s;s++)
	{
		if (*s == ':' || *s==' ')
			*s = '_';
	}
	sprintf(filename, "%s_%s.log", getExecutableName(), datestr);
	return filename;
#endif
}

static char sFulldumpFilename[CRYPTIC_MAX_PATH+5] = {0}; 
static S16 sWideFulldumpFilename[CRYPTIC_MAX_PATH + 5] = {0}; 


S16 *assertGetFullDumpFilename_Wide()
{
	char *s,datestr[1000];
	char modulepath[MAX_PATH];
	char *modulename = NULL;
	int len = 0;
	int assertMode = getAssertMode();
	S16 sWideTemp[1000];


	if(sWideFulldumpFilename[0] != 0)
	{
		return sWideFulldumpFilename;
	}

	if(pFullDumpFilenameOverride)
	{
		UTF8ToWideStrConvert(pFullDumpFilenameOverride, SAFESTR(sWideFulldumpFilename));
		strcpy_trunc(sFulldumpFilename, pFullDumpFilenameOverride);
		return sWideFulldumpFilename;
	}


	if (assertMode & ASSERTMODE_TEMPORARYDUMPS)
	{
		//just using sWideFulldumpFilename as temp storage here
		len = GetTempPath(ARRAY_SIZE(sWideFulldumpFilename), sWideFulldumpFilename);
	}

	if (len)
	{
		WideToUTF8StrConvert(sWideFulldumpFilename, SAFESTR(sFulldumpFilename));
		GetModuleFileName(NULL, SAFESTR(sWideTemp));
		WideToUTF8StrConvert(sWideTemp, SAFESTR(modulepath));
		forwardSlashes(modulepath);
		for (modulename = modulepath + strlen(modulepath); modulename > modulepath; modulename--)
		{
			if (*modulename == '/')
			{
				modulename++;
			break;
			}
		}
		strcat_trunc(sFulldumpFilename, modulename);
	}
	else
	{
		GetModuleFileName(NULL, SAFESTR(sWideFulldumpFilename));
		WideToUTF8StrConvert(sWideFulldumpFilename, SAFESTR(sFulldumpFilename));
	}

	if (assertMode & (ASSERTMODE_DATEDMINIDUMPS | ASSERTMODE_TEMPORARYDUMPS)) {
		timeMakeDateStringFromSecondsSince2000(datestr,timeSecondsSince2000());
		for(s=datestr;*s;s++)
		{
			if (*s == ':' || *s==' ')
				*s = '_';
		}
		strcat_trunc(sFulldumpFilename, ".");
		strcat_trunc(sFulldumpFilename, datestr);
	}

	strcat_trunc(sFulldumpFilename, ".dmp");
	UTF8ToWideStrConvert(sFulldumpFilename, SAFESTR(sWideFulldumpFilename));
	return sWideFulldumpFilename;

}

char *assertGetFullDumpFilename(void)
{
	if (sFulldumpFilename[0])
	{
		return sFulldumpFilename;
	}

	assertGetFullDumpFilename_Wide();

	return sFulldumpFilename;
}

void assertOverrideMiniDumpFilename(const char *pFilename)
{
	pMiniDumpFilenameOverride = pFilename;
}

void assertOverrideFullDumpFilename(const char *pFilename)
{
	pFullDumpFilenameOverride = pFilename;
}

static char minidumpFileName_UTF8[CRYPTIC_MAX_PATH+5] = {0};
static S16 minidumpFileName_UTF16[CRYPTIC_MAX_PATH+5] = {0};

S16 *assertGetMiniDumpFilename_Wide()
{
#ifdef _XBOX
	return assertGetFullDumpFilename();
#else
	char *s,datestr[1000];
	char modulepath[MAX_PATH];
	char *modulename = NULL;
	int len = 0;
	int assertMode = getAssertMode();


	if(minidumpFileName_UTF16[0] != 0)
	{
		return minidumpFileName_UTF16;
	}

	if(pMiniDumpFilenameOverride)
	{
		UTF8ToWideStrConvert(pMiniDumpFilenameOverride, SAFESTR(minidumpFileName_UTF16));
		strcpy_trunc(minidumpFileName_UTF8, pMiniDumpFilenameOverride);
		return minidumpFileName_UTF16;
	}


	if (assertMode & ASSERTMODE_TEMPORARYDUMPS)
	{
		len = GetTempPath(ARRAY_SIZE(minidumpFileName_UTF16), minidumpFileName_UTF16);
	}
	if (len)
	{
		S16 sWideTemp[MAX_PATH];
		GetModuleFileName(NULL, SAFESTR(sWideTemp));

		WideToUTF8StrConvert(sWideTemp, SAFESTR(modulepath));
		WideToUTF8StrConvert(minidumpFileName_UTF16, SAFESTR(minidumpFileName_UTF8));

		forwardSlashes(modulepath);
		for (modulename = modulepath + strlen(modulepath); modulename > modulepath; modulename--)
		{
			if (*modulename == '/')
			{
				modulename++;
			break;
			}
		}
		strcat_trunc(minidumpFileName_UTF8, modulename);
	}
	else
	{
		GetModuleFileName(NULL, SAFESTR(minidumpFileName_UTF16));
		WideToUTF8StrConvert(minidumpFileName_UTF16, SAFESTR(minidumpFileName_UTF8));
	}

	if (assertMode & (ASSERTMODE_DATEDMINIDUMPS | ASSERTMODE_TEMPORARYDUMPS)) {
		timeMakeDateStringFromSecondsSince2000(datestr,timeSecondsSince2000());
		for(s=datestr;*s;s++)
		{
			if (*s == ':' || *s==' ')
				*s = '_';
		}
		strcat_trunc(minidumpFileName_UTF8, ".");
		strcat_trunc(minidumpFileName_UTF8, datestr);
	}

	strcat_trunc(minidumpFileName_UTF8, ".mdmp");

	UTF8ToWideStrConvert(minidumpFileName_UTF8, SAFESTR(minidumpFileName_UTF16));
	return minidumpFileName_UTF16;
#endif
}

char *assertGetMiniDumpFilename(void)
{
	if (minidumpFileName_UTF8[0])
	{
		return minidumpFileName_UTF8;
	}
	assertGetMiniDumpFilename_Wide();

	return minidumpFileName_UTF8;
}

#ifndef _XBOX
typedef BOOL (__stdcall *MiniDumpWriter)(
	HANDLE hProcess,
	DWORD ProcessId,
	HANDLE hFile,
	MINIDUMP_TYPE DumpType,
	PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	PMINIDUMP_CALLBACK_INFORMATION CallbackParam
	);
#endif

typedef struct DumpThreadParams
{
	char*					filename;
	DWORD					threadId;
	PEXCEPTION_POINTERS		exceptInfo;
	BOOL					error;
} DumpThreadParams;

#ifndef _XBOX
static MiniDumpWriter pMiniDumpWriteDump = NULL;
unsigned WINAPI MiniDumpThread(void* pData);

bool assertWriteMiniDump(char* filename, PEXCEPTION_POINTERS info)
{
	HMODULE debugDll;
	HANDLE hThread;
	DWORD threadId;
	DumpThreadParams params;

	// Try to load the debug help dll or imagehlp.dll
	debugDll = LoadLibrary(L"dbghelp.dll" );
	if(!debugDll)
	{
		debugDll = LoadLibrary(L"imagehlp.dll" );
		
		if(!debugDll)
		{
			return false;
		}
	}
	pMiniDumpWriteDump = (MiniDumpWriter) GetProcAddress(debugDll, "MiniDumpWriteDump");
	if(!pMiniDumpWriteDump)
	{
		FreeLibrary(debugDll);
		return false;
	}

	// Use the other thread to write with (some version of MiniDumpWriteDump need to pause this thread)
	params.exceptInfo = info;
	params.filename = filename;
	params.threadId = GetCurrentThreadId();
	hThread = (HANDLE)_beginthreadex(NULL, 0, MiniDumpThread, &params, 0, &threadId);
	if (INVALID_HANDLE_VALUE != hThread)
	{
		WaitForSingleObject(hThread, 60000);
		CloseHandle(hThread);
	}

	pMiniDumpWriteDump = NULL;
	FreeLibrary(debugDll);

	return !params.error;
}

unsigned WINAPI MiniDumpThread(void* pData)
{
	HANDLE hFile;
	DumpThreadParams* pParams = (DumpThreadParams*)pData;

	pParams->error = 0;

	// Create the file first.
	hFile = CreateFile_UTF8(pParams->filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE != hFile)
	{
		MINIDUMP_EXCEPTION_INFORMATION mdei;

		mdei.ThreadId = pParams->threadId;
		mdei.ExceptionPointers = pParams->exceptInfo;
		mdei.ClientPointers = TRUE;

		pParams->error = !pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, 
			minidump_flags,
			pParams->exceptInfo? &mdei: NULL, NULL, NULL);
		if (pParams->error) // try again with simpler options
			pParams->error = !pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, 
				minidump_old_flags,
				pParams->exceptInfo? &mdei: NULL, NULL, NULL);

		CloseHandle(hFile);
	}
	else
	{
		// Could not open the file!
		pParams->error = 1;
	}
	return !pParams->error;
}


/*void assertWriteFullDump(char* filename, PEXCEPTION_POINTERS info)
{
	extern char *findUserDump(void);
	char CommandLine[200];
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	BOOL ret;

	sprintf(
		CommandLine,
		"%s %u*%u \"%s\"",
		findUserDump(),
		GetCurrentThreadId(),
		info,
		filename
		);

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	ret = CreateProcess(NULL,CommandLine,
		NULL, NULL, 0, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

	if (!ret) {
		printf("Failed to spawn %s to create full user dump, switching to minidump mode...\n", CommandLine);
		g_assertmode &= ~ASSERTMODE_FULLDUMP;
		g_assertmode |= ASSERTMODE_MINIDUMP;
	} else {
		WaitForSingleObject( pi.hProcess, 60000 );
		CloseHandle( pi.hProcess );
		CloseHandle( pi.hThread );
	}
}*/

bool assertWriteFullDumpSimple(PEXCEPTION_POINTERS info)
{
	char *fulldumpFilename = assertGetFullDumpFilename();
	static int recursive_call=0;
	bool writeSuccess = false;

	if (recursive_call)
		return false;
	recursive_call = 1;

	minidump_flags |= MiniDumpWithFullMemory;

	if(!info)
	{
		__try 
		{
			// Raise dummy exception to get proper exception information.
			//RaiseException(0, 0, 0, 0);
			*((int*)0x00) = 1;
		}
#pragma warning(suppress:6320)
		__except(writeSuccess = assertWriteMiniDump(fulldumpFilename, GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
#pragma warning(suppress:6322)		//Empty _except block...
		{
			// Pass through to the zipper
		}
	}
	else
		writeSuccess = assertWriteMiniDump(fulldumpFilename, info);
	// Zip the dump
	if (writeSuccess && (g_assertmode & ASSERTMODE_ZIPPED)) {
		fileZip(fulldumpFilename);
	}

	recursive_call = 0;

	return writeSuccess;
}



bool assertWriteMiniDumpSimple(PEXCEPTION_POINTERS info)
{
	char *minidumpFilename = assertGetMiniDumpFilename();
	static int recursive_call=0;
	bool writeSuccess = false;

	if (recursive_call)
		return false;
	recursive_call = 1;

	minidump_flags |= MiniDumpNormal;

	if(!info)
	{
		__try 
		{
			// Raise dummy exception to get proper exception information.
			//RaiseException(0, 0, 0, 0);
			*((int*)0x00) = 1;
		}
#pragma warning(suppress:6320)
		__except(writeSuccess = assertWriteMiniDump(minidumpFilename, GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
#pragma warning(suppress:6322)		//Empty _except block...
		{
			// Pass through to the zipper
		}
	}
	else
		writeSuccess = assertWriteMiniDump(minidumpFilename, info);
	// Zip the minidump
	if (writeSuccess && (g_assertmode & ASSERTMODE_ZIPPED)) {
		fileZip(minidumpFilename);
	}

	recursive_call = 0;
	return writeSuccess;
}
#endif

#else
//If this is called, it means you've compiled the utilities library without _DEBUG defined 
//and linking project with _DEBUG defined.
//void __cdecl superassert(const char* expr, const char* errormsg, bool isFatalError, const char* filename, unsigned lineno){}
#endif

void setAssertMode(int assertmode) { 
	assertInitialized = true;
	g_assertmode = assertmode; 
}
int getAssertMode() { return g_assertmode; }

void assertSetAccountName(const char *accountName)
{
	if (accountName && accountName[0])
	{
		strcpy(sAccountName, accountName);
	}
	else
		assertClearAccountName();
}
char* assertGetAccountName(void)
{
	return sAccountName;
}
void assertClearAccountName(void)
{
	sAccountName[0] = 0;
}

int assertIsDevelopmentMode(void)
{
	return isDevelopmentMode();
}

const char *getErrorTracker()
{
	return szErrorTracker;
}

void AssertErrorf(const char * file, int line, char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ErrorvInternal(true, file, line, fmt, ap);
	va_end(ap);
}

void AssertErrorFilenamef(const char * file, int line, const char *filename, char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ErrorFilenamevInternal(file, line, filename, fmt, ap);
	va_end(ap);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void assertNow(ACMD_SENTENCE errorString)
{
	assertmsg(0, errorString);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void crashNow(void)
{
	int x = 5, y;

	y = x - x;
	x = 7 / y;
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void crashDivZero(void)
{
	F32 x = 0.f;
	F32 y = 1.f;
	F32 z = y / x;
	
	printf("%s: Did %f / %f, got %f.\n", __FUNCTION__, y, x, z);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void crashInvalidFloat(void)
{
	F32 z = sqrt(-1.f);
	
	printf("%s: Did sqrt(-1), got %f.\n", __FUNCTION__, z);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void crashBadThread(void)
{
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)0xdeadbeef, NULL, 0, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void infLoopNow(void)
{
	while (1)
	{
	}
}

#pragma warning(push)
#pragma warning(disable:4717)
AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void infRecurseNow(void)
{
	infRecurseNow();
}
#pragma warning(pop)

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void errorfNow(ACMD_SENTENCE errorString)
{
	Errorf("%s", errorString);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void errorDetailsfNow(char *errorString, ACMD_SENTENCE errorDetails)
{
	ErrorDetailsf("%s", errorDetails);
	Errorf("%s", errorString);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void allocNow(int size)
{
	void *leakedMemory = malloc(size);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void alertfNow(ACMD_SENTENCE errorString)
{
	Alertf("%s", errorString);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void allocNowMegs(int size)
{
	int i;

	for (i=0; i < size; i++)
	{
		void *leakedMemory = malloc(1024 * 1024);
		printf("Just malloced a meg\n");
	}
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void stackOverflowNow(int iBytesAtATime)
{
	char *pBuf = alloca(iBytesAtATime);
	int x = 5;
	if (x)
	{
		stackOverflowNow(iBytesAtATime);
	}
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void RAMOverflowNow(int iBytesAtATime)
{
	while (1)
	{
		malloc(iBytesAtATime);
	}
}




// Displays the current build version
AUTO_COMMAND ACMD_NAME("version") ACMD_CATEGORY(Debug) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0);
const char *GetUsefulVersionString(void)
{
    return GetUsefulVersion();
}

void VersionSlowCB(TimedCallback *callback, F32 timeSinceLastCallback, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	DoSlowCmdReturn(1, GetUsefulVersion(), pSlowReturnInfo);
	free(pSlowReturnInfo);
}

//displays the version a second later, useful for debugging things that depend on slow command return
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void VersionSlow(CmdContext *pContext)
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo = calloc(sizeof(CmdSlowReturnForServerMonitorInfo), 1);
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));
	pContext->slowReturnInfo.bDoingSlowReturn = true;
	TimedCallback_Run(VersionSlowCB, pSlowReturnInfo, 1.0f);
}

const char *GetUsefulVersionString_Short(void)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{
		char *pTemp = NULL;
		estrStackCreate(&pTemp);
		estrCopy2(&pTemp, GetUsefulVersionString());
		estrTruncateAtFirstOccurrence(&pTemp, '(');
		estrTrimLeadingAndTrailingWhitespace(&pTemp);
		estrCopy(&spRetVal, &pTemp);
		estrDestroy(&pTemp);
	}

	return spRetVal;
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void fatalErrorfNow(ACMD_SENTENCE errorString)
{
	FatalErrorf("%s", errorString);
}

AUTO_COMMAND ACMD_CATEGORY(debug) ACMD_CMDLINE;
void fatalErrorDetailsfNow(const char * errorString, const char * detailsString)
{
	ErrorDetailsf("%s", detailsString);
	FatalErrorf("%s", errorString);
}

static void **sppRecentBigAllocs = NULL;

AUTO_COMMAND ACMD_CATEGORY(debug);
void bigAlloc(void)
{
	eaPush(&sppRecentBigAllocs, malloc(100000000));
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void bigFree(void)
{
	free(eaPop(&sppRecentBigAllocs));
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void ErrorFilenamefNow(char *pFileName, ACMD_SENTENCE pString)
{
	ErrorFilenamef(pFileName, "%s", pString);
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void ErrorFilenamefNonBlockingNow(char *pFileName, ACMD_SENTENCE pString)
{
	ErrorFilenamef_NonBlocking(pFileName, "%s", pString);
}

void assertTimedCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (userData)
	{
		assertmsg(0, (char*)userData);
	}
	else
	{
		assert(0);
	}
}

#if !_PS3
U32 assertGetCurrentThreadID(void){
	return GetCurrentThreadId();
}
#endif


#if !PLATFORM_CONSOLE

// code for catching illegal pure virtual function calls

static void pureVirtualFuncWasCalled(void)
{
	assertmsg(0, "Pure virtual function called illegally!");
}

AUTO_RUN_EARLY;
void assertOnIllegalPureVirtualFuncCalls(void)
{
	_set_purecall_handler(pureVirtualFuncWasCalled);
}

#endif