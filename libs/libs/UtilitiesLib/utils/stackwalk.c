#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#if PLATFORM_CONSOLE

void stackWalkPreloadDLLs(void)
{
}

void stackWalkDumpStackToBuffer(char* buffer, int bufferSize, /* PCONTEXT */ void *stack)
{
}

#else

bool gbDisplayInvalidAddress = true;
AUTO_CMD_INT(gbDisplayInvalidAddress, DisplayInvalidAddress) ACMD_CMDLINE;

// Stackwalk boundary scan testing value
// 0x0001 : pSymGetModuleInfo64() fails
// 0x0002 : pSymFromAddr() fails
// 0x0004 : Always run fallback stackwalk
// 0x0008 : Fallback frame pointer unwinding fails
// 0x0010 : GetModuleHandleEx fails in fallback
// 0x0020 : VirtualQuery fails in fallback
// 0x0040 : Module base fallback fails
// 0x0080 : Print callstacks and callstack reports.
// 0x0100 : Use frame size unwinder (experimental)
// 0x0200 : Always use fallback callstack
unsigned guStackwalkBoundaryScanTest = 0;
AUTO_CMD_INT(guStackwalkBoundaryScanTest, StackwalkBoundaryScanTest) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

#include "earray.h"
#include "callstack.h"
#include "estring.h"
#include "timing.h"
#include "memlog.h"

#pragma warning(disable: 6258 4701)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif

#include <windows.h>
#include <winternl.h>
#include "Stackwalk.h"
// imagehlp.h must be compiled with packing to eight-byte-boundaries,
// but does nothing to enforce that.
#pragma pack( push, before_imagehlp, 8 )
#include <dbghelp.h>
#pragma pack( pop, before_imagehlp )
#include <Psapi.h>
#include <TlHelp32.h>
#include "error.h"
#include "ParsePEHeader.h"
#include "utils.h"
#include "stackwalkfallback.h"

#define lenof(a) (sizeof(a) / sizeof((a)[0]))
#define MAXNAMELEN 1024 // max name length for found symbols

#define TTBUFLEN 65536 // for a temp buffer

#define WAIT_TOO_LONG 120000	// 2 minutes
#define WAIT_FOR_UNFREEZE 10000	// 10 seconds
#define MAX_FRAME_COUNT 50		// Max number of frames in stackwalk to cut off infinite loops
#define GUID_IS_NULL(sig70) \
	(!(sig70.Data1 || sig70.Data2 || sig70.Data3 || \
	sig70.Data4[0] || sig70.Data4[1] || sig70.Data4[2] || sig70.Data4[3] || \
	sig70.Data4[4] || sig70.Data4[5] || sig70.Data4[6] || sig70.Data4[7]))

static void swShutdown(void);

// SymCleanup()
typedef BOOL (__stdcall *tSymCleanup)( IN HANDLE hProcess );
static tSymCleanup pSymCleanup = NULL;

// SymFunctionTableAccess64()
typedef PVOID (__stdcall *tSymFunctionTableAccess64)( HANDLE hProcess, DWORD64 AddrBase );
static tSymFunctionTableAccess64 pSymFunctionTableAccess64 = NULL;

// SymGetLineFromAddr64()
typedef BOOL (__stdcall *tSymGetLineFromAddr64)( IN HANDLE hProcess, IN DWORD64 dwAddr, OUT PDWORD pdwDisplacement, OUT PIMAGEHLP_LINE64 Line );
static tSymGetLineFromAddr64 pSymGetLineFromAddr64 = NULL;

// SymGetModuleBase64()
typedef DWORD64 (__stdcall *tSymGetModuleBase64)( IN HANDLE hProcess, IN DWORD64 dwAddr );
static tSymGetModuleBase64 pSymGetModuleBase64 = NULL;

// SymGetModuleInfo64()
typedef BOOL (__stdcall *tSymGetModuleInfo64)( IN HANDLE hProcess, IN DWORD64 dwAddr, OUT PIMAGEHLP_MODULE64 ModuleInfo );
static tSymGetModuleInfo64 pSymGetModuleInfo64 = NULL;

// SymGetOptions()
typedef DWORD (__stdcall *tSymGetOptions)( VOID );
static tSymGetOptions pSymGetOptions = NULL;

// SymFromAddr()
typedef BOOL (__stdcall *tSymFromAddr)( IN HANDLE hProcess, IN DWORD64 dwAddr, OUT PDWORD64 pdwDisplacement, OUT PSYMBOL_INFO Symbol );
static tSymFromAddr pSymFromAddr = NULL;

// SymInitialize()
typedef BOOL (__stdcall *tSymInitialize)( IN HANDLE hProcess, IN PSTR UserSearchPath, IN BOOL fInvadeProcess );
static tSymInitialize pSymInitialize = NULL;

// SymLoadModule64()
typedef DWORD64 (__stdcall *tSymLoadModule64)( IN HANDLE hProcess, IN HANDLE hFile, IN PSTR ImageName, IN PSTR ModuleName, IN DWORD64 BaseOfDll, IN DWORD SizeOfDll );
static tSymLoadModule64 pSymLoadModule64 = NULL;

// SymSetOptions()
typedef DWORD (__stdcall *tSymSetOptions)( IN DWORD SymOptions );
static tSymSetOptions pSymSetOptions = NULL;

// StackWalk64()
typedef BOOL (__stdcall *tStackWalk64)( 
							  DWORD MachineType, 
							  HANDLE hProcess,
							  HANDLE hThread, 
							  LPSTACKFRAME64 StackFrame, 
							  PVOID ContextRecord,
							  PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
							  PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
							  PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
							  PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress );
static tStackWalk64 pStackWalk64;

// UnDecorateSymbolName()
typedef DWORD (__stdcall WINAPI *tUnDecorateSymbolName)( PCSTR DecoratedName, PSTR UnDecoratedName,	DWORD UndecoratedLength, DWORD Flags );
static tUnDecorateSymbolName pUnDecorateSymbolName = NULL;

typedef struct ModuleEntry
{
	char imageName[MAX_PATH];
	char moduleName[MAX_PATH];
	DWORD64 baseAddress;
	DWORD64 size;
}
ModuleEntry;

#define MAX_MODULES_IN_MODULELIST 256

typedef struct ModuleList
{
	ModuleEntry* apList[MAX_MODULES_IN_MODULELIST];
	int count;
}
ModuleList;

static void enumAndLoadModuleSymbols( HANDLE hProcess );
static bool loadSingleModuleSymbolsUsingPCOffset(const ModuleList *modules, DWORD64 pcOffset);

static bool fillModuleList( ModuleList *pModules, HANDLE hProcess );
static bool fillModuleListTH32( ModuleList *pModules, DWORD pid );
static bool fillModuleListPSAPI( ModuleList *pModules, DWORD pid, HANDLE hProcess );

static HINSTANCE shDebugHelpDll = NULL;	// The dll to get exe debug symbol lookup related functions.
static HANDLE sStackDumpThread = 0;		// The thread that will create the stack dump for all other threads.
static bool sbDumpingStack = false;			// just a global to prevent recursion
static bool sbHaveBaseModulePDB = false;
static ULONG sStartedThreadID = 0; // Flag for whether it's started and holds the specific thread ID
static volatile bool sbStackWalkCompleted = false;

/***********************************************************************************************
* Worker thread stuff
*/
/* Function WorkerThreadProc()
*	This is the main function of the Image Extraction thread.  It is a worker thread
*	whose main purpose is to create missing images for a mirror patch server.  The
*	function itself does not do anything useful.  It simply asks the thread to goto
*	sleep until a new task has been given to it through QueueUserAPC().
*/
DWORD WINAPI StackDumpWorkerThreadProc(LPVOID useless)
{
	//need to do some CRT stuff immediately so any CRT-oriented allocation stuff will have already happened
	int x = errno;
	x++;

	while(1){
		SleepEx(INFINITE, 1);
	}
}

/* Function WorkerThreadExitProc()
*	Ask the worker thread to kill itself.
*/
void CALLBACK WorkerThreadExitProc(LPVOID useless){
	ExitThread(0);
}

/*
* Worker thread stuff
***********************************************************************************************/

// Return true if we're in an external environment, and should not attempt more aggressive symbol lookup.
static bool swIsExternal(void)
{
	return getAssertMode() & ASSERTMODE_ISEXTERNALAPP;
}

static void swSymInitialize(void)
{
	DWORD symOptions;						// symbol handler settings
	char symSearchPath[2048];
	char* p;

	// NOTE: normally, the exe directory and the current directory should be taken
	// from the target process. The current dir would be gotten through injection
	// of a remote thread; the exe fir through either ToolHelp32 or PSAPI.

	char *tt = NULL; // this is a _sample_. you can do the error checking yourself.

	// build symbol search path from:
	symSearchPath[0] = 0;
	// current directory
	if ( GetCurrentDirectory_UTF8( &tt ) )
	{
		strcat(symSearchPath, tt);
		strcat(symSearchPath, ";");
	}
	// dir with executable
	if ( GetModuleFileName_UTF8( 0, &tt ) )
	{
		for ( p = tt + strlen( tt ) - 1; p >= tt; -- p )
		{
			// locate the rightmost path separator
			if ( *p == '\\' || *p == '/' || *p == ':' )
				break;
		}
		// if we found one, p is pointing at it; if not, tt only contains
		// an exe name (no path), and p points before its first byte
		if ( p != tt ) // path sep found?
		{
			if ( *p == ':' ) // we leave colons in place
				++ p;
			*p = '\0'; // eliminate the exe name and last path sep

			strcat(symSearchPath, tt);
			strcat(symSearchPath, ";");
		}
	}
	// environment variable _NT_SYMBOL_PATH
	if ( GetEnvironmentVariable_UTF8( "_NT_SYMBOL_PATH", &tt ) )
	{
		strcat(symSearchPath, tt);
		strcat(symSearchPath, ";");
	}
	// environment variable _NT_ALTERNATE_SYMBOL_PATH
	if ( GetEnvironmentVariable_UTF8( "_NT_ALTERNATE_SYMBOL_PATH", &tt ) )
	{
		strcat(symSearchPath, tt);
		strcat(symSearchPath, ";");
	}
	// environment variable SYSTEMROOT
	if ( GetEnvironmentVariable_UTF8( "SYSTEMROOT", &tt ) )
	{
		strcat(symSearchPath, tt);
		strcat(symSearchPath, ";");
	}
	// Internal Cryptic tools directory
	if ( !swIsExternal() )
	{
		strcat(symSearchPath, "c:\\night\\tools\\bin;");
	}


	if ( strlen(symSearchPath) > 0 ) // if we added anything, we have a trailing semicolon
	{
		symSearchPath[strlen(symSearchPath)-1] = 0;
	}

	//sdcWrite(pContext,  "symbols path: %s\n", symSearchPath.c_str() );

	// SymInitialize() wants a writeable string
	estrCopy2( &tt, symSearchPath);

	// init symbol handler stuff
	if (!pSymInitialize(GetCurrentProcess(), tt, false))
	{
		goto Cleanup;
	}

	symOptions = pSymGetOptions();
	symOptions |= SYMOPT_LOAD_LINES;		// Load line information
	symOptions &= ~SYMOPT_UNDNAME;			// Do not show function names in undecorated form.  Irrelevant in c either way.
	symOptions |= SYMOPT_DEFERRED_LOADS;	// Do not load symbol files until a symbol is referenced.
	pSymSetOptions(symOptions);

Cleanup:
	estrDestroy(&tt);
}

static ULONG swStartup(void)
{
	if (!sStartedThreadID)
	{
		ULONG threadID;
		// Original documentation:
		// We load imagehlp.dll dynamically because the NT4-version does not
		// offer all the functions that are in the NT5 lib.
		// JS: We also don't want to require debug dll's to be present for the executable to run.
		// JS: If the dll's are not present, trying to dump the stack would just do nothing.

		// Try to load the debug help dll first.
		//	dbghelp.dll is the new and approved way to get at debug info.  Its functionality also appears to
		//	be the superset of imagehlp.dll.
		shDebugHelpDll = LoadLibrary( L"dbghelp.dll" );

		// If that doesn't work, then try imagehlp.dll.
		if (shDebugHelpDll == NULL)
		{
			shDebugHelpDll = LoadLibrary( L"imagehlp.dll" );
			return 0;
		}

		// Grab the addresses of functions we are going to be using.
		// All of these functions are present in both dll's.
		pSymCleanup =				(tSymCleanup)				GetProcAddress(shDebugHelpDll, "SymCleanup");
		pSymFunctionTableAccess64 =	(tSymFunctionTableAccess64)	GetProcAddress(shDebugHelpDll, "SymFunctionTableAccess64");
		pSymGetLineFromAddr64 =		(tSymGetLineFromAddr64)		GetProcAddress(shDebugHelpDll, "SymGetLineFromAddr64");
		pSymGetModuleBase64 =		(tSymGetModuleBase64)		GetProcAddress(shDebugHelpDll, "SymGetModuleBase64");
		pSymGetModuleInfo64 =		(tSymGetModuleInfo64)		GetProcAddress(shDebugHelpDll, "SymGetModuleInfo64");
		pSymGetOptions =			(tSymGetOptions)			GetProcAddress(shDebugHelpDll, "SymGetOptions");
		pSymFromAddr =				(tSymFromAddr)				GetProcAddress(shDebugHelpDll, "SymFromAddr");
		pSymInitialize =			(tSymInitialize)			GetProcAddress(shDebugHelpDll, "SymInitialize");
		pSymLoadModule64 =			(tSymLoadModule64)			GetProcAddress(shDebugHelpDll, "SymLoadModule64");
		pSymSetOptions =			(tSymSetOptions)			GetProcAddress(shDebugHelpDll, "SymSetOptions");
		pStackWalk64 =				(tStackWalk64)				GetProcAddress(shDebugHelpDll, "StackWalk64");
		pUnDecorateSymbolName =		(tUnDecorateSymbolName)		GetProcAddress(shDebugHelpDll, "UnDecorateSymbolName");

		// Did we get all the functions we needed?
		if (!(
			pSymCleanup &&
			pSymFunctionTableAccess64 &&
//			pSymGetLineFromAddr64 && (optional)
			pSymGetModuleBase64 &&
//			pSymGetModuleInfo64 && (optional)
			pSymGetOptions &&
			pSymFromAddr &&
			pSymInitialize &&
			pSymLoadModule64 &&
			pSymSetOptions &&
			pStackWalk64 &&
			pUnDecorateSymbolName))
		{
			goto Cleanup;
		}

		// Create a worker thread that will be used for all other threads to dump their stacks on request.
		sStackDumpThread = CreateThread(0, 0, StackDumpWorkerThreadProc, NULL, 0, &threadID);
		sStartedThreadID = threadID;
		assertDoNotFreezeThisThread(sStartedThreadID);
		errorDoNotFreezeThisThread(sStartedThreadID);

		swSymInitialize();

		enumAndLoadModuleSymbols(GetCurrentProcess());
	}

	return sStartedThreadID;

Cleanup:
	// If not, then it'll be impossible to do a stackdump.
	swShutdown();
	return 0;
}

void stackWalkPreloadDLLs(void)
{
	swStartup();
}

// Note: This function would ideally be called externally on program shutdown (once), and would
// do a better job of properly stopping threads. At the moment, we don't bother - we simply call it
// if we fail to properly startup the module in an attempt to clean things up nicely (in swStartup())
static void swShutdown(void)
{
	if (pSymCleanup)
	{
		pSymCleanup(GetCurrentProcess());
	}

	if (sStartedThreadID)
	{
		TerminateThread(sStackDumpThread, 0);
		sStackDumpThread = 0;
		FreeLibrary(shDebugHelpDll);
		shDebugHelpDll = NULL;
		sStartedThreadID = 0;
	}
}

/*******************************************************************************
* StackDumpContext
*/
typedef enum{
	SDT_BUFFER,
	SDT_STREAM,
} StackDumpTarget;

typedef struct{
	DWORD dwThreadID;
	HANDLE threadHandle;		// Thread handle
	LPCONTEXT stack;			// if available, caller's execution context
	void *objectInMainFrame;	// Pointer to an object (such as a variable) in the stack frame of the entry function for this thread

	StackDumpTarget dumpTarget;
	union{
		struct{
			char* buffer;
			int bufferSize;

			char* writeCursor;
			int remainingBufferSize;
		} targetBuffer;

		struct{
			FILE* stream;
		} targetStream;
	};

	// Fallback context, if first attempt fails.
	FallbackStackThreadContext fallback_context;
} StackDumpContext;

// TLS slot for stackwalkSetMainFunctionPointer().
static int tls_main_pointer_index = 0;

// This stores a pointer to something in Cryptic code entry point (such as main(), WinMain(), thread start function, etc) frame.
// When we're unwinding, we can use this know when we've unwound everything.
// This does not use STATIC_THREAD_ALLOC because that macro uses the heap, which can cause a circular dependency.
void stackwalkSetMainFunctionPointer(void *ptr)
{
	bool tls_success;

	// Allocate TLS slot, if necessary.
	ATOMIC_INIT_BEGIN;
	tls_main_pointer_index = TlsAlloc();
	ATOMIC_INIT_END;
	assert(tls_main_pointer_index != TLS_OUT_OF_INDEXES);

	// Set pointer.
	tls_success = TlsSetValue(tls_main_pointer_index, ptr);
	assert(tls_success);
}

// Get pointer set by stackwalkSetMainFunctionPointer().
static void *stackwalkGetMainFunctionPointer()
{
	return TlsGetValue(tls_main_pointer_index);
}

static void initStackDumpContext(StackDumpContext* context){
	// Fill out the stack dump context
	//	GetCurrentThread() seems to return a dummy handle that probably means "this/current thread".
	//  To get at the real handle, ask for a copy of the handle.
	context->dwThreadID = GetCurrentThreadId();
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
		GetCurrentProcess(), &context->threadHandle, 0, false, DUPLICATE_SAME_ACCESS);
	context->stack = NULL;
}

static void cleanStackDumpContext(StackDumpContext* context){
	CloseHandle(context->threadHandle);
}

/* Function sdcWrite()
*	This is the function that is called for all outputs from the actual stackdump.  Since there
*	are different kinds of stackdump output targets, the functions frees the stackdumping
*	code from worrying about how to perform the actual outputting.
*/
static void sdcWrite(StackDumpContext* context, char* format, ...){
	va_list args;
	va_start(args, format);

	switch(context->dumpTarget){
		case SDT_BUFFER:
			{
				// If we're supposed to write to a buffer...
				int charactersPrinted;

				// Print the specified string into the buffer if possible.
				charactersPrinted = vsnprintf_s(context->targetBuffer.writeCursor, context->targetBuffer.remainingBufferSize, _TRUNCATE, format, args);

				// If the specified string cannot be printed because the buffer isn't large enough, do nothing.
				if(-1 == charactersPrinted)
					return;

				// Account for the characters printed.
				context->targetBuffer.writeCursor += charactersPrinted;
				context->targetBuffer.remainingBufferSize -= charactersPrinted;
				break;
			}
		case SDT_STREAM:
			// If we're supposed to print to a stream, do it.
			vfprintf(context->targetStream.stream, format, args);
			break;
		default:
			break;
	}

	va_end(args);
}
/*
* StackDumpContext
*******************************************************************************/

// The following code takes a thread handle and returns a thread ID. Handles are 
// not unique, so it can be ambiguous what thread is indicated by a handle. Note 
// that this code uses undocumented NT functions and data structures, and may not 
// be compatible with future versions of Windows. Therefore, this function is 
// only included so we can call it from the immediate window.

// The following definitions use unsupported, internal Windows NT definitions. 
typedef LONG    KPRIORITY;

typedef struct _CLIENT_ID {
	DWORD        UniqueProcess;
	DWORD        UniqueThread;
} CLIENT_ID;

typedef struct _THREAD_BASIC_INFORMATION {
	NTSTATUS ExitStatus; 
	PVOID TebBaseAddress; 
	CLIENT_ID ClientId; 
	KAFFINITY AffinityMask; 
	KPRIORITY Priority; 
	KPRIORITY BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef enum _THREAD_INFORMATION_CLASS {

	ThreadBasicInformation, 
	ThreadTimes, 
	ThreadPriority, 
	ThreadBasePriority, 
	ThreadAffinityMask, 
	ThreadImpersonationToken, 
	ThreadDescriptorTableEntry, 
	ThreadEnableAlignmentFaultFixup, 
	ThreadEventPair, 
	ThreadQuerySetWin32StartAddress, 
	ThreadZeroTlsCell, 
	ThreadPerformanceCount, 
	ThreadAmILastThread, 
	ThreadIdealProcessor, 
	ThreadPriorityBoost, 
	ThreadSetTlsArrayAddress, 
	//ThreadIsIoPending, // This is already defined in the NT headers
	ThreadHideFromDebugger = 17

} THREAD_INFORMATION_CLASS, *PTHREAD_INFORMATION_CLASS;

// Please call this function from the immediate window only.
DWORD WINAPI GetThreadIdNT(HANDLE hThread)
{
	NTSTATUS                 Status;
	THREAD_BASIC_INFORMATION tbi = { 0 };
	typedef NTSTATUS
		(*NtQueryInformationThreadFN) (
		IN HANDLE ThreadHandle,
		IN THREADINFOCLASS ThreadInformationClass,
		OUT PVOID ThreadInformation,
		IN ULONG ThreadInformationLength,
		OUT PULONG ReturnLength OPTIONAL
		);


	HANDLE hNtDLL = LoadLibrary( L"Ntdll.dll" );
	NtQueryInformationThreadFN pfnNtQueryInformationThread = NULL;
	if (hNtDLL)
	{
		pfnNtQueryInformationThread = 
			(NtQueryInformationThreadFN)GetProcAddress( hNtDLL, "NtQueryInformationThread" );
		if ( pfnNtQueryInformationThread )
		{
			Status = pfnNtQueryInformationThread(hThread,
				ThreadBasicInformation,
				&tbi,
				sizeof(tbi),
				NULL);
		}
		CloseHandle(hNtDLL);
	}

	// Return TID
	return tbi.ClientId.UniqueThread;
}

/********************************************************/

typedef struct ContextCallstackWrapper {
	StackDumpContext *pContext;
	char *pCallstackReport;
	int iCountId;
} ContextCallstackWrapper;

static int ShowStackEx(StackDumpContext *pContext,
	char *pCallstackReport,
	int id,
	bool *report_probably_complete_stack,
	bool *report_module_info_ok);

static void __stdcall WorkerThreadDumpStackProc(ULONG_PTR dwData)
{
	ContextCallstackWrapper *pContextCallstack = (ContextCallstackWrapper*) dwData;
	bool report_probably_complete_stack;
	bool report_module_info_ok;
	int frames;

	// Dump the stack.
	frames = ShowStackEx(pContextCallstack->pContext, pContextCallstack->pCallstackReport, pContextCallstack->iCountId, &report_probably_complete_stack, &report_module_info_ok);

	if (guStackwalkBoundaryScanTest & 0x0080)
		printf("---stackwalk callstack:\n%s\n---report:\n%s\n------\n", pContextCallstack->pContext->targetBuffer.buffer, pContextCallstack->pCallstackReport);

	// If we didn't get a complete stack, use the fallback unwinder instead.
	// Assume that both buffers have the same size.
	if (!frames || !report_probably_complete_stack || !report_module_info_ok || (guStackwalkBoundaryScanTest & 0x4))
		frames = fallback_stackWalkDumpStackToBuffer(pContextCallstack->pContext->targetBuffer.buffer, pContextCallstack->pContext->targetBuffer.bufferSize,
			&pContextCallstack->pContext->fallback_context, pContextCallstack->pCallstackReport, pContextCallstack->pContext->targetBuffer.bufferSize,
			pContextCallstack->pContext->objectInMainFrame, frames);

	sbStackWalkCompleted = true;
	return;
}

/* Function dumpStack()
*	This function can be call by any thread at any time to produce a dump of the calling threads stack.
*
*	This function sends the stack dump worker thread the data it requires to produce a stack dump for
*	the calling thread, then goes into a waiting state for the worker thread to finish dumping the stack.
*	When the worker thread is done, the calling thread returns to execution as normal.
*
*/

static int siStackwalkCountId = 0;
static volatile int siStackwalkFailedId;

// can be called with a NULL stack, in which case it will look at the current stack
void stackWalkDumpStackToBuffer(char* buffer, int buffer_size, void *vstack, void *tib, void *boundingFramePointer, char *pCallstackReport)
{
	bool bUnFroze=false;
	StackDumpContext context = {0};
	int i;
	
	PCONTEXT stack = (PCONTEXT)vstack;

	ContextCallstackWrapper contextCallstack = {0};

	// Make sure the stackwalk module has been started.
	// If not, start it.
	// If it cannot be started, do not proceed.
	if (!swStartup())
	{
		return;
	}

	// prevent recursive calls
	if (sbDumpingStack)
	{
		sprintf_s(SAFESTR2(buffer), "Couldn't generate stack - stackWalkDumpStackToBuffer was called recursively");
		return;
	}
	sbDumpingStack = true;
	sbStackWalkCompleted = false;
	contextCallstack.pContext = &context;
	contextCallstack.pCallstackReport = pCallstackReport;
	contextCallstack.iCountId = ++siStackwalkCountId;

	initStackDumpContext(&context);
	context.dumpTarget = SDT_BUFFER;
	context.targetBuffer.writeCursor = context.targetBuffer.buffer = buffer;
	context.targetBuffer.remainingBufferSize = context.targetBuffer.bufferSize = buffer_size;
	context.stack = stack;
	context.objectInMainFrame = stackwalkGetMainFunctionPointer();

	// Capture the current context for the fallback unwinder in case the primary unwinder fails.
	if (context.stack)
#ifdef _M_AMD64
		fallback_captureCurrentContext(&context.fallback_context, tib, boundingFramePointer, true, (void *)context.stack->Rip, (void *)context.stack->Rbp);
#else
		fallback_captureCurrentContext(&context.fallback_context, tib, boundingFramePointer, true, (void *)context.stack->Eip, (void *)context.stack->Ebp);
#endif
	else
		fallback_captureCurrentContext(&context.fallback_context, tib, boundingFramePointer, false, NULL, NULL);

	// Ask the worker thread to dump this thread's stack.
	QueueUserAPC(WorkerThreadDumpStackProc, sStackDumpThread, (ULONG_PTR) &contextCallstack);

	for (i=0; i < WAIT_TOO_LONG / 100; i++)
	{
		Sleep(100);

		if (sbStackWalkCompleted)
		{
			break;
		}

		if (i>WAIT_FOR_UNFREEZE / 100 && !bUnFroze)
		{
			// Freezing threads might have locked up a thread StackWalk requires (e.g. WinSock thread if the MS symserv is in the symbol path)
			extern void assertFreezeAllOtherThreads(int resume);
			bUnFroze = true;
			assertFreezeAllOtherThreads(1);
		}
	}


	if (!sbStackWalkCompleted)
	{
		siStackwalkFailedId = siStackwalkCountId;
		sprintf_s(SAFESTR2(buffer), "ERROR: swDumpStackToBuffer - timeout waiting for dump");
	}

	cleanStackDumpContext(&context);
	sbDumpingStack = false;
}

static void stackwalkWriteCallstackTextReport(
	HANDLE hProcess,
	STACKFRAME64 *sframe,
	char **ppModules,
	char **ppEntryPoints,
	const IMAGEHLP_MODULE64 *pModuleInfo,
	BOOL hasModuleInfo,
	bool *report_module_info_ok)
{
	IMAGEHLP_MODULE64 moduleInfo = *pModuleInfo;
	int err = 0;
	char *image = NULL;
	char *pdb = NULL;

	if (!hasModuleInfo || GUID_IS_NULL(moduleInfo.PdbSig70))
	{
		// parse PE executable header as default fall-back if pSGMI fails
		if (!moduleInfo.LoadedImageName[0])
		{
			char *pTemp = NULL;
			GetModuleFileName_UTF8(0, &pTemp);
			strcpy_trunc(moduleInfo.LoadedImageName, pTemp);
			estrDestroy(&pTemp);
		}
		if (moduleInfo.LoadedImageName[0])
		{
			const char *failureReason = NULL;
			int failureErrorCode;
			GetDebugInfo(moduleInfo.LoadedImageName, NULL, &moduleInfo, &failureReason, &failureErrorCode);
			if (failureReason)
				stackWalkWarning((void *)moduleInfo.BaseOfImage, failureReason, failureErrorCode);
			if (!GUID_IS_NULL(moduleInfo.PdbSig70))
				*report_module_info_ok = true;
		}
		else
		{
			err = GetLastError();
			return;
		}
	}

	if (moduleInfo.ImageName && moduleInfo.ImageName[0])
	{
		image = strrchr(moduleInfo.ImageName, '\\');
		if (!image)
			image = moduleInfo.ImageName;
		else
			image = image + 1;
	} 
	else
	{
		image = strrchr(moduleInfo.LoadedImageName, '\\');
		if (!image)
			image = moduleInfo.LoadedImageName;
		else
			image = image + 1;
	}

	if (moduleInfo.LoadedPdbName && moduleInfo.LoadedPdbName[0])
	{
		pdb = strstri(moduleInfo.LoadedPdbName, ".pdb");
		if (pdb)
		{
			pdb = pdb + 4;
			*pdb = '\0';
		}
	}

	{
		estrConcatf( ppModules, 
			"%s%s\n" // Module Name
			"%s%s\n", // PDB Name
			LineContentHeaders[LINECONTENTS_MODULE_NAME], image,
			LineContentHeaders[LINECONTENTS_MODULE_PDB], moduleInfo.LoadedPdbName );
		estrConcatf( ppModules,
			"%s%"FORM_LL"x\n" // Module Base Address
			"%s%08x\n" // Module Size
			"%s%d\n", // Module Time (in seconds since 2000)
			LineContentHeaders[LINECONTENTS_MODULE_BASE_ADDRESS], moduleInfo.BaseOfImage,
			LineContentHeaders[LINECONTENTS_MODULE_SIZE], moduleInfo.ImageSize,
			LineContentHeaders[LINECONTENTS_MODULE_TIME], 
			timeGetSecondsSince2000FromWindowsTime32(moduleInfo.TimeDateStamp));
		estrConcatf( ppModules, "%s{%08X-%04X-%04X-%02X %02X-%02X %02X %02X %02X %02X %02X}\n", 
			LineContentHeaders[LINECONTENTS_MODULE_GUID],
			moduleInfo.PdbSig70.Data1,
			moduleInfo.PdbSig70.Data2,
			moduleInfo.PdbSig70.Data3,
			moduleInfo.PdbSig70.Data4[0], moduleInfo.PdbSig70.Data4[1],
			moduleInfo.PdbSig70.Data4[2], moduleInfo.PdbSig70.Data4[3], 
			moduleInfo.PdbSig70.Data4[4], moduleInfo.PdbSig70.Data4[5], 
			moduleInfo.PdbSig70.Data4[6], moduleInfo.PdbSig70.Data4[7]);
		estrConcatf( ppModules, "%s%d\n\n", 
			LineContentHeaders[LINECONTENTS_MODULE_AGE],
			moduleInfo.PdbAge);

		estrConcatf( ppEntryPoints, "%s%08"FORM_LL"x\n", LineContentHeaders[LINECONTENTS_CALLSTACK_ADDRESS], sframe->AddrPC.Offset);
	}
}

// FIXME: The following is very unsafe due to unsynchronized access to siStackwalkFailedId.
#define CheckSdcWrite(pContext, fmt, ...) \
	if (id != siStackwalkFailedId) \
	{ \
		sdcWrite(pContext, fmt, __VA_ARGS__); \
	} \
	else \
	{ \
		siStackwalkFailedId = 0; \
		frames = 0; \
		stackWalkWarning(0, "StackwalkFailed", siStackwalkFailedId); \
		goto exitShowStack; \
	}

static int ShowStackEx(StackDumpContext *pContext, char *pCallstackReport, int id, bool *report_probably_complete_stack, bool *report_module_info_ok)
{
	HANDLE hThread;
	CONTEXT threadContext = {0};
	int passed_context = 0;

	DWORD imageType = IMAGE_FILE_MACHINE_I386;
	HANDLE hProcess = GetCurrentProcess();	// hProcess normally comes from outside
	int frameNum;							// counts walked frames
	DWORD64 offsetFromSymbol;				// tells us how far from the symbol we were
	char undName[MAXNAMELEN];				// undecorated name
	IMAGEHLP_MODULE64 Module;
	IMAGEHLP_LINE64 Line;
	char *pModules = NULL;
	char *pEntryPoints = NULL;
	BOOL hasModuleInfo;
	IMAGEHLP_MODULE64 moduleInfo = {0};
	DWORD64 moduleOffset = 0;
	const char * pModuleName = NULL;
	DWORD gleValue;

	ULONG64 buffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME*sizeof(TCHAR) + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO) buffer; // SYMBOL_INFO + SymName string
	STACKFRAME64 s; // in/out stackframe

	int frames = 0;
	ModuleList modules = {0};
	bool bModulesValid = false;

	memset( &s, '\0', sizeof(s) );

	if (pCallstackReport) 
	{
		estrAllocaCreate(&pModules, MAX_STACK_ESTR);
		if (!pModules)
			stackWalkWarning(0, "allocModules", 0);
		estrAllocaCreate(&pEntryPoints, 2048);
		if (!pEntryPoints)
			stackWalkWarning(0, "allocEntryPoints", 0);
		if (!pModules || !pEntryPoints)
			return 0;
		*report_probably_complete_stack = false;
		*report_module_info_ok = false;
	}

	// Get the thread execution context so we can do a stack dump.
	if (pContext->stack)
	{
		memcpy(&threadContext, pContext->stack, sizeof(CONTEXT));
		passed_context = 1;
	}
	else
	{
		threadContext.ContextFlags = CONTEXT_FULL;
		if(!GetThreadContext(pContext->threadHandle, &threadContext))
		{
			gleValue = GetLastError();
			sdcWrite(pContext,  "GetThreadContext(): gle = %lu\n", gleValue );
			stackWalkWarning(NULL, "GetThreadContext", gleValue );
			return 0;
		}
	}

	hThread = pContext->threadHandle;

	// init STACKFRAME for first call
#ifdef _M_IX86
	// normally, call ImageNtHeader() and use machine info from PE header
	imageType = IMAGE_FILE_MACHINE_I386;
	s.AddrPC.Offset = threadContext.Eip;
	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Offset = threadContext.Ebp;
	s.AddrFrame.Mode = AddrModeFlat;
	//s.AddrStack.Offset = threadContext.Esp;
	//s.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
	imageType = IMAGE_FILE_MACHINE_AMD64;
	s.AddrPC.Offset = threadContext.Rip;
	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Offset = threadContext.Rsp;
	s.AddrFrame.Mode = AddrModeFlat;
	s.AddrStack.Offset = threadContext.Rsp;
	s.AddrStack.Mode = AddrModeFlat;
#elif _M_IA64
	imageType = IMAGE_FILE_MACHINE_IA64;
	s.AddrPC.Offset = threadContext.StIIP;
	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Offset = threadContext.IntSp;
	s.AddrFrame.Mode = AddrModeFlat;
	s.AddrBStore.Offset = threadContext.RsBSP;
	s.AddrBStore.Mode = AddrModeFlat;
	s.AddrStack.Offset = threadContext.IntSp;
	s.AddrStack.Mode = AddrModeFlat;
#else
#error "Platform not supported!"
#endif	/**/
	
	pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSymbol->MaxNameLen = MAX_SYM_NAME;

	memset( &Line, '\0', sizeof Line );
	Line.SizeOfStruct = sizeof Line;

	memset( &Module, '\0', sizeof Module );
	Module.SizeOfStruct = sizeof Module;

	offsetFromSymbol = 0;

//	swSymInitialize();

	for ( frameNum = 0; frameNum < MAX_FRAME_COUNT; ++ frameNum )
	{
		bool bWroteSymbolDetails = false;

		// get next stack frame (StackWalk(), SymFunctionTableAccess(), SymGetModuleBase())
		// if this returns ERROR_INVALID_ADDRESS (487) or ERROR_NOACCESS (998), you can
		// assume that either you are done, or that the stack is so hosed that the next
		// deeper frame could not be found.
		if ( ! pStackWalk64( imageType, hProcess, hThread, &s, &threadContext, NULL,
			pSymFunctionTableAccess64, pSymGetModuleBase64, NULL ) )
			break;

		//The first frame will always be SymGetSymFromAddr().
		//The 2nd frame will always be WaitForSingleObject(), because the thread will be waiting for the stackdump to complete.
		//.. if we were passed a stack, this assumption won't be true
		if(!passed_context && frameNum < 2) 
			continue;

		CheckSdcWrite(pContext, "%3i ", passed_context? frameNum : frameNum - 2);

		// Check if we've gotten past the main frame yet.
		// This assumes the main frame isn't bigger than 2 MiB, which is pretty reasonable, given that that's the default size of the entire main thread stack.
		if (s.AddrStack.Offset < (DWORD64)pContext->objectInMainFrame && (DWORD64)pContext->objectInMainFrame - s.AddrStack.Offset < 2*1024*1024)
			*report_probably_complete_stack = true;

		// look up module info once
		moduleOffset = s.AddrPC.Offset;
		pModuleName = NULL;
		moduleInfo.SizeOfStruct = sizeof (IMAGEHLP_MODULE64);
		if (guStackwalkBoundaryScanTest & 0x1)
			hasModuleInfo = 0;
		else
			hasModuleInfo = pSymGetModuleInfo64 && pSymGetModuleInfo64( hProcess, s.AddrPC.Offset, &moduleInfo);

		// if we have base module symbols, and we failed to get module info already, try to load symbols on the fly
		if (sbHaveBaseModulePDB && !hasModuleInfo)
		{
			printf("base module PDB detected - ");
			if (!bModulesValid)
			{
				// fill in module list - done once per crash
				fillModuleList(&modules, hProcess);
				bModulesValid = true;
			}

			loadSingleModuleSymbolsUsingPCOffset(&modules, s.AddrPC.Offset);
			hasModuleInfo = pSymGetModuleInfo64 && pSymGetModuleInfo64( hProcess, s.AddrPC.Offset, &moduleInfo);
		}

		if (hasModuleInfo)
		{
			if (!GUID_IS_NULL(moduleInfo.PdbSig70))
				*report_module_info_ok = true;
			moduleOffset -= moduleInfo.BaseOfImage;
			pModuleName = strrchr(moduleInfo.ImageName, '\\');
			if(pModuleName){
				pModuleName++; // Advance past the slash
			}else{
				pModuleName = moduleInfo.ImageName;
			}
			if (GUID_IS_NULL(moduleInfo.PdbSig70))
				memlog_printf(NULL, "address %p module %p pSGMI succeeded with a null GUID", (void *)s.AddrPC.Offset, (void *)moduleInfo.BaseOfImage);
			++frames;
		}
		else
		{
			int err = GetLastError();
			memlog_printf(NULL, "address %p: pSGMI failed with error code %d", (void *)s.AddrPC.Offset, err);
			moduleInfo.SizeOfStruct = 0;
		}

		if (s.AddrPC.Offset == 0 )
		{
			CheckSdcWrite(pContext, "%s", "-nosymbols- PC == 0\n" );
		}
		else
		{ // we seem to have a valid PC
			// show procedure info

			// dbghelp.dll has the useful feature of returning export symbols as well as PDB symbols.
			// this is great, except in some cases.

			// ErrorTracker has the behavior that it will "do no work" on any stacks that have any symbols.
			// For "in the field" builds, we want to ensure that stacks go to error tracker with no symbols so that it will do its work properly.
			// For builds that we control (developers, running in Boston, etc) we will have PDB's, and we WILL want export symbols to be listed

			// if pSymFromAddr fails -OR- it succeeds and returns an export symbol and we don't have debugging symbols for the base module
			//  output basic stack frame entry for ErrorTracker to decipher if it can
			// else
			//  output symbols for stack frame

			if ((guStackwalkBoundaryScanTest & 0x2) || !pSymFromAddr( hProcess, s.AddrPC.Offset, &offsetFromSymbol, pSymbol) || ((pSymbol->Flags & SYMFLAG_EXPORT) && (!sbHaveBaseModulePDB)))
			{
				gleValue = GetLastError();
				if (gbDisplayInvalidAddress || gleValue != ERROR_INVALID_ADDRESS ) {
					if (hasModuleInfo)
					{
						CheckSdcWrite(pContext, "%s!%08x (%x,%x,%x,%x)", pModuleName, moduleOffset, s.Params[0], s.Params[1], s.Params[2], s.Params[3]);
					}
					else
					{
						CheckSdcWrite(pContext, "0x%08x (%x,%x,%x,%x)", s.AddrPC.Offset, s.Params[0], s.Params[1], s.Params[2], s.Params[3]);
					}
					CheckSdcWrite(pContext,  "SymGetSymFromAddr(): gle = %lu", gleValue );
				}
				else
				{
					if (hasModuleInfo)
					{
						CheckSdcWrite(pContext, "SYSTEM_ERROR 487 at %s!%08x (%x,%x,%x,%x)", pModuleName, moduleOffset, s.Params[0], s.Params[1], s.Params[2], s.Params[3]);
					}
					else
					{
						CheckSdcWrite(pContext, "SYSTEM_ERROR 487 at 0x%08X (%x,%x,%x,%x)", s.AddrPC.Offset, s.Params[0], s.Params[1], s.Params[2], s.Params[3]);
					}
				}
			}
			else
			{
				char buf[2000];
				pUnDecorateSymbolName( pSymbol->Name, undName, MAXNAMELEN, UNDNAME_NAME_ONLY );
				sprintf(buf, "%s (%"FORM_LL"x,%"FORM_LL"x,%"FORM_LL"x,%"FORM_LL"x)", undName, s.Params[0], s.Params[1], s.Params[2], s.Params[3]);
				CheckSdcWrite(pContext, buf);
				//CheckSdcWrite(pContext,  "%-25s", undName );
			}
			
			// show line number info, NT5.0-method
			if ( pSymGetLineFromAddr64 != NULL )
			{	
				DWORD displacement = 0;
				// yes, we have SymGetLineFromAddr()
				if ( ! pSymGetLineFromAddr64( hProcess, s.AddrPC.Offset, &displacement, &Line ) )
				{
					if ( GetLastError() != ERROR_INVALID_ADDRESS ) {
						//CheckSdcWrite(pContext,  "\nSymGetLineFromAddr(): gle = %lu", gle );
					}
				}
				else
				{
					CheckSdcWrite(pContext,  "\n\t\tLine: %s(%lu)", Line.FileName, Line.LineNumber);

					CheckSdcWrite(pContext,  "\n\t\tModule: %s", (pModuleName) ? pModuleName : "???");
					bWroteSymbolDetails = true;
				}
			}  // yes, we have SymGetLineFromAddr()

			if(!bWroteSymbolDetails)
				sdcWrite(pContext, "\n\t\tLine: --- (0)\n\t\tModule: %s", pModuleName ? pModuleName : "???");

			CheckSdcWrite(pContext, "\n");

			if (pCallstackReport)
			{
				stackwalkWriteCallstackTextReport(hProcess, &s, &pModules, &pEntryPoints, &moduleInfo, hasModuleInfo, report_module_info_ok);
			}
		} // we seem to have a valid PC

		// no return address means no deeper stackframe
		if ( s.AddrReturn.Offset == 0 )
		{
			// avoid misunderstandings in the CheckSdcWrite(pContext, ) following the loop
			SetLastError( 0 );
			break;
		}
	} // for ( frameNum )

	if (pCallstackReport && (pModules || pEntryPoints))
	{
		static int CALLSTACK_BUFFER_SIZE = 10240;  // FIXME: This seems totally bogus and complete lies.
		pCallstackReport[0] = 0;

		strcatf_s(pCallstackReport, CALLSTACK_BUFFER_SIZE, "%s\n\n", LineContentHeaders[LINECONTENTS_MODULES_START]);
		strcatf_s(pCallstackReport, CALLSTACK_BUFFER_SIZE, "%s", pModules);
		strcatf_s(pCallstackReport, CALLSTACK_BUFFER_SIZE, "%s\n\n", LineContentHeaders[LINECONTENTS_MODULES_END]);
		strcatf_s(pCallstackReport, CALLSTACK_BUFFER_SIZE, "%s\n\n", LineContentHeaders[LINECONTENTS_CALLSTACK_START]);
		strcatf_s(pCallstackReport, CALLSTACK_BUFFER_SIZE, "%s", pEntryPoints);
		strcatf_s(pCallstackReport, CALLSTACK_BUFFER_SIZE, "%s\n\n", LineContentHeaders[LINECONTENTS_CALLSTACK_END]);

		estrDestroy(&pModules);
		estrDestroy(&pEntryPoints);
	}

	if (frameNum == MAX_FRAME_COUNT)
		CheckSdcWrite(pContext, "\nStackwalk reached max frame count and terminated early.\n");
	gleValue = GetLastError();
	if ( gleValue != ERROR_SUCCESS )
		CheckSdcWrite(pContext,  "\nStackWalk(): gle = %lu\n", gleValue );

	// Don't do this!
	// flushall flushes even files that are meant for reading.  This would definitely screw something up if the
	// thread being stack dumped is reading a file!
	// If you want tio flush stdout immediately, do it some other way!
	//flushall();

exitShowStack:
	// de-init symbol handler etc.
//	pSymCleanup( hProcess );

	return frames;
}

/*
bool moduleIsLoaded(const char *moduleName) // Module name without path, e.g. foo.dll
{
	int i;
	ModuleList modules = {0};

	fillModuleList(&modules, GetCurrentProcess());

	for (i=0; i<modules.count; i++)
	{
		if (stricmp(modules.apList[i]->moduleName, moduleName)==0)
			return true;
	}
	return false;
}
*/

// return true if we loaded symbols this time
// (if we already loaded them successfully in a different call, this will return false!)
static bool loadSymbolsForModuleEntry(ModuleEntry *pEntry)
{
	char img[MAX_PATH];
	char mod[MAX_PATH];

	// pSymLoadModule64() wants writeable strings
	strcpy(img, pEntry->imageName); // has full path
	strcpy(mod, pEntry->moduleName);

	return (pSymLoadModule64(GetCurrentProcess(), 0, img, mod, pEntry->baseAddress, pEntry->size) != 0);
}

static void enumAndLoadModuleSymbols( HANDLE hProcess )
{
	ModuleList modules = {0};
	int i;
	char *pBaseImageName = NULL;

	estrStackCreate(&pBaseImageName);

	// get base module name
	GetModuleFileNameEx_UTF8(GetCurrentProcess(), NULL, &pBaseImageName);

	// fill in module list
	fillModuleList( &modules, hProcess );

	for (i = 0; i < modules.count; i++)
	{
		ModuleEntry *pEntry = modules.apList[i];

		if (loadSymbolsForModuleEntry(pEntry) && stricmp(pEntry->imageName, pBaseImageName) == 0)
		{
			sbHaveBaseModulePDB = true;
		}
	}

	estrDestroy(&pBaseImageName);
}

// returns true if we attempted to load module symbols for the provided pcOffset
static bool loadSingleModuleSymbolsUsingPCOffset(const ModuleList *modules, DWORD64 pcOffset)
{
	int i;

	for (i = 0; i < modules->count; i++)
	{
		ModuleEntry *pEntry = modules->apList[i];

		if (pcOffset >= pEntry->baseAddress && pcOffset < (pEntry->baseAddress+pEntry->size))
		{
			loadSymbolsForModuleEntry(pEntry);

			return true;
		}
	}

	return false;
}


static bool fillModuleList( ModuleList *pModules, HANDLE hProcess )
{
	DWORD pid = GetCurrentProcessId();
	// try toolhelp32 first
	if ( fillModuleListTH32( pModules, pid ) )
		return true;
	// nope? try psapi, then
	return fillModuleListPSAPI( pModules, pid, hProcess );
}


/*
// miscellaneous toolhelp32 declarations; we cannot #include the header
// because not all systems may have it
#define MAX_MODULE_NAME32 255
#define TH32CS_SNAPMODULE   0x00000008
#pragma pack( push, 8 )
typedef struct tagMODULEENTRY32
{
	DWORD   dwSize;
	DWORD   th32ModuleID;       // This module
	DWORD   th32ProcessID;      // owning process
	DWORD   GlblcntUsage;       // Global usage count on the module
	DWORD   ProccntUsage;       // Module usage count in th32ProcessID's context
	BYTE  * modBaseAddr;        // Base address of module in th32ProcessID's context
	DWORD   modBaseSize;        // Size in bytes of module starting at modBaseAddr
	HMODULE hModule;            // The hModule of this module in th32ProcessID's context
	char    szModule[MAX_MODULE_NAME32 + 1];
	char    szExePath[MAX_PATH];
} MODULEENTRY32;
typedef MODULEENTRY32 *  PMODULEENTRY32;
typedef MODULEENTRY32 *  LPMODULEENTRY32;
#pragma pack( pop )
*/


static bool fillModuleListTH32( ModuleList *pModules, DWORD pid )
{
	// CreateToolhelp32Snapshot()
	typedef HANDLE (__stdcall *tCT32S)( DWORD dwFlags, DWORD th32ProcessID );
	// Module32First()
	typedef BOOL (__stdcall *tM32F)( HANDLE hSnapshot, LPMODULEENTRY32 lpme );
	// Module32Next()
	typedef BOOL (__stdcall *tM32N)( HANDLE hSnapshot, LPMODULEENTRY32 lpme );

	// I think the DLL is called tlhelp32.dll on Win9X, so we try both
	const char *dllname[] = { "kernel32.dll", "tlhelp32.dll" };
	HINSTANCE hToolhelp = 0;
	tCT32S pCT32S;
	tM32F pM32F;
	tM32N pM32N;

	HANDLE hSnap;
	MODULEENTRY32 me = { sizeof me };
	bool keepGoing;
	ModuleEntry *e = NULL;
	int i;

	e = calloc(sizeof(ModuleEntry), 1);

	for ( i = 0; i < lenof( dllname ); ++ i )
	{
		hToolhelp = LoadLibrary_UTF8( dllname[i] );
		if ( hToolhelp == 0 )
			continue;
		pCT32S = (tCT32S) GetProcAddress( hToolhelp, "CreateToolhelp32Snapshot" );
		pM32F = (tM32F) GetProcAddress( hToolhelp, "Module32FirstW" );
		pM32N = (tM32N) GetProcAddress( hToolhelp, "Module32NextW" );
		if ( pCT32S != 0 && pM32F != 0 && pM32N != 0 )
			break; // found the functions!
		FreeLibrary( hToolhelp );
		hToolhelp = 0;
	}

	if ( hToolhelp == 0 ) // nothing found?
		return false;

	hSnap = pCT32S( TH32CS_SNAPMODULE, pid );
	if ( hSnap == (HANDLE) -1 )
		return false;

	keepGoing = !!pM32F( hSnap, &me );
	while ( keepGoing )
	{
		char *pTemp = NULL;
		estrStackCreate(&pTemp);
		// here, we have a filled-in MODULEENTRY32
		// JS: I don't care what modules are loaded.
		//printf( "%08lXh %6lu %-15.15s %s\n", me.modBaseAddr, me.modBaseSize, me.szModule, me.szExePath );

		UTF16ToEstring(me.szExePath, 0, &pTemp);
		strcpy(e->imageName, pTemp);

		UTF16ToEstring(me.szModule, 0, &pTemp);
		strcpy(e->moduleName, pTemp);

		e->baseAddress = (INT_PTR) me.modBaseAddr;
		e->size = me.modBaseSize;
		if(pModules->count < MAX_MODULES_IN_MODULELIST)
		{
			pModules->apList[pModules->count] = e;
			pModules->count++;
			e = calloc(sizeof(ModuleEntry), 1);
		}
		keepGoing = !!pM32N( hSnap, &me );
		estrDestroy(&pTemp);
	}

	CloseHandle( hSnap );

	FreeLibrary( hToolhelp );

	free(e);

	return (pModules->count != 0);
}

/*
// miscellaneous psapi declarations; we cannot #include the header
// because not all systems may have it
typedef struct MODULEINFO {
	LPVOID lpBaseOfDll;
	DWORD SizeOfImage;
	LPVOID EntryPoint;


} MODULEINFO, *LPMODULEINFO;
*/

static void clearModules(ModuleList *pModules)
{
	int i;
	for(i=0; i<pModules->count;i++)
	{
		free(pModules->apList[i]);
	}
	pModules->count = 0;
}

static bool fillModuleListPSAPI( ModuleList *pModules, DWORD pid, HANDLE hProcess )
{
	// EnumProcessModules()
	typedef BOOL (__stdcall *tEPM)( HANDLE hProcess, HMODULE *lphModule, DWORD cb, LPDWORD lpcbNeeded );
	// GetModuleFileNameEx()
	typedef DWORD (__stdcall *tGMFNE)( HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize );
	// GetModuleBaseName() -- redundant, as GMFNE() has the same prototype, but who cares?
	typedef DWORD (__stdcall *tGMBN)( HANDLE hProcess, HMODULE hModule, LPSTR lpFilename, DWORD nSize );
	// GetModuleInformation()
	typedef BOOL (__stdcall *tGMI)( HANDLE hProcess, HMODULE hModule, LPMODULEINFO pmi, DWORD nSize );

	HINSTANCE hPsapi;
	tEPM pEPM;
	tGMFNE pGMFNE;
	tGMBN pGMBN;
	tGMI pGMI;

	unsigned int i;
	ModuleEntry *e = NULL;
	DWORD cbNeeded;
	MODULEINFO mi;
	HMODULE *hMods = 0;
	char *tt = 0;

	e = calloc(sizeof(ModuleEntry), 1);

	hPsapi = LoadLibrary( L"psapi.dll" );
	if ( hPsapi == 0 )
		return false;

	clearModules(pModules);

	pEPM = (tEPM) GetProcAddress( hPsapi, "EnumProcessModules" );
	pGMFNE = (tGMFNE) GetProcAddress( hPsapi, "GetModuleFileNameExA" );
	pGMBN = (tGMFNE) GetProcAddress( hPsapi, "GetModuleBaseNameA" );
	pGMI = (tGMI) GetProcAddress( hPsapi, "GetModuleInformation" );
	if ( pEPM == 0 || pGMFNE == 0 || pGMBN == 0 || pGMI == 0 )
	{
		// yuck. Some API is missing.
		FreeLibrary( hPsapi );
		return false;
	}

	hMods = calloc(sizeof(HMODULE), TTBUFLEN / sizeof(HMODULE));
	tt = malloc(TTBUFLEN);
	// not that this is a sample. Which means I can get away with
	// not checking for errors, but you cannot. :)

	if ( ! pEPM( hProcess, hMods, TTBUFLEN, &cbNeeded ) )
	{
		printf( "EPM failed, gle = %lu\n", GetLastError() );
		goto cleanup;
	}

	if ( cbNeeded > TTBUFLEN )
	{
		printf( "More than %lu module handles. Huh?\n", lenof( hMods ) );
		goto cleanup;
	}

	for ( i = 0; i < cbNeeded / sizeof hMods[0]; ++ i )
	{
		// for each module, get:
		// base address, size
		pGMI( hProcess, hMods[i], &mi, sizeof mi );
		e->baseAddress = (DWORD)(INT_PTR) mi.lpBaseOfDll;
		e->size = mi.SizeOfImage;
		// image file name
		tt[0] = '\0';
		pGMFNE( hProcess, hMods[i], tt, TTBUFLEN );
		strcpy(e->imageName, tt);
		// module name
		tt[0] = '\0';
		pGMBN( hProcess, hMods[i], tt, TTBUFLEN );
		strcpy(e->imageName, tt);
		printf( "%08"FORM_LL"Xh %8"FORM_LL"u %-15.15s %s\n", e->baseAddress,
			e->size, e->moduleName, e->imageName);

		if(pModules->count < MAX_MODULES_IN_MODULELIST)
		{
			pModules->apList[pModules->count] = e;
			pModules->count++;
			e = calloc(sizeof(ModuleEntry), 1);
		}
	}

cleanup:
	if ( hPsapi )
		FreeLibrary( hPsapi );
	free(tt);
	free(hMods);
	free(e);

	return (pModules->count != 0);
}

// Report a problem related to stack walking.
void stackWalkWarning(void *file, const char *string, int code)
{
	static bool stackWalkWarning_failed = false;

	__try
	{
		memlog_printf(NULL, "stackWalkWarning: %p: %s (%d)", file, string, code);
	}
#pragma warning(suppress:6320)
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		stackWalkWarning_failed = true;
	}
}

#endif
