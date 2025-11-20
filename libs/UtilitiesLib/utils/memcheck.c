#include <stdio.h>
#include <stdlib.h>
#include "memcheck.h"
#include "MemoryMonitor.h"
#include "fileutil.h"
#include "stringUtil.h"
#include "utils.h"
#include "timing.h"
#include "MemAlloc.h"
#include "MemTrack.h"
#include "UnitSpec.h"
#include "MemReport.h"
#include "systemspecs.h"
#include "VirtualMemory.h"

#if _XBOX
#include <xtl.h>
#endif

#if _XBOX && defined(PROFILE)
    #define HAVE_HEAP_FUNCTIONS 0
#else
    #define HAVE_HEAP_FUNCTIONS 1
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#if 1 || WIN32

#include "wininclude.h"

#ifdef _DEBUG
#define  SET_CRT_DEBUG_FIELD(a)   _CrtSetDbgFlag((a) | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG))
#define  CLEAR_CRT_DEBUG_FIELD(a) _CrtSetDbgFlag(~(a) & _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG))
#else
#define  SET_CRT_DEBUG_FIELD(a)   ((void) 0)
#define  CLEAR_CRT_DEBUG_FIELD(a) ((void) 0)
#endif

void myInvalidParameterHandler( const wchar_t * expression,
                                const wchar_t * function, 
                                const wchar_t * file, 
                                unsigned int    line, 
                                uintptr_t   pReserved )

{
	char expr_utf[2048];
	char file_utf[MAX_PATH];
	char function_utf[1024];

	expr_utf[0] = 0;
	file_utf[0] = 0;
	function_utf[0] = 0;

	if (expression)
		WideToUTF8StrConvert(expression, SAFESTR(expr_utf));
	if (function)
		WideToUTF8StrConvert(function, SAFESTR(function_utf));
	if (file)
		WideToUTF8StrConvert(file, SAFESTR(file_utf));
#ifdef _XBOX
	if ( expression && function && file )
	{
	// For some reason the Xbox sets timestamps to 2106, and then asserts on them -_-
	// specifically: ( ( ( yr - 1900 ) >= _BASE_YEAR ) && ( ( yr - 1900 ) <= _MAX_YEAR ) )
		if (strstri(expr_utf, "_MAX_YEAR"))
		{
			return;
		}
	}
#endif

	ignorableAssertmsgf(0, "CRT Invalid Paramater in function %s  (%s %d): %s", 
	(char*)function_utf, (char*)file_utf, line, (char*)expr_utf);
	

}

 

#if !_PS3

int CrtReportingFunction( int reportType, char *userMessage, int *retVal )
{
	if (reportType == _CRT_WARN) {
		return FALSE;
	}
#if defined(_XBOX)
	// For some reason the Xbox sets timestamps to 2106, and then asserts on them -_-
	// specifically: ( ( ( yr - 1900 ) >= _BASE_YEAR ) && ( ( yr - 1900 ) <= _MAX_YEAR ) )
	//
	//Note that this will then go on to trigger the invalid parameter function
	if (strstri(userMessage, "_MAX_YEAR"))
	{
#if !defined(PROFILE)
		//hack to fix XBOX CRT bug
		if (_crtAssertBusy == -2)
		{
			_crtAssertBusy++;
		}

		if (_crtAssertBusy == 0 && reportType == _CRT_ASSERT)
		{
			_crtAssertBusy--;
		}
#endif

		return 1;
	}
#endif
	printf("\nCRT Error: %s\n", userMessage);

	// Disable minidumps because they have to allocate memory to start a thread and we have the heap locked
	if (getAssertMode() & ASSERTMODE_MINIDUMP && !strstri(userMessage, "Run-Time Check")) {
		// Can't do a mini, do a full!
		setAssertMode(getAssertMode() & ~(ASSERTMODE_MINIDUMP|ASSERTMODE_ZIPPED) | ASSERTMODE_FULLDUMP);
	} else {
		setAssertMode(getAssertMode() & ~(ASSERTMODE_ZIPPED));
	}
	assertmsg(!"CRT Error", userMessage);
	// Debug by default if we get here.
	*retVal = 1;
	return 1;
}

#endif

extern void test(void);
void memCheckInit(void)
{
	static bool inited=false;

	if (inited)
		return;
	inited = true;

	memMonitorInit();

#if _PS3
#else
/*	// JE: These would print errors to stdout, but that doesn't exist in the game project, and we re-assign stdout in the newConsoleWindow call anyway!
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
*/
	_CrtSetReportHook(CrtReportingFunction);
	_set_invalid_parameter_handler(myInvalidParameterHandler);
	//_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	//_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);

    {
	    static _CrtMemState state;
	    _CrtMemCheckpoint( &state );
    }
	return;


	SET_CRT_DEBUG_FIELD( _CRTDBG_LEAK_CHECK_DF | _CRTDBG_DELAY_FREE_MEM_DF );

	// Set the debug heap to report memory leaks when the process terminates,
	// and to keep freed blocks in the linked list.
	//SET_CRT_DEBUG_FIELD( _CRTDBG_LEAK_CHECK_DF | _CRTDBG_DELAY_FREE_MEM_DF );

	// Open a log file for the hook functions to use 

	// Install the hook functions
	//_CrtSetDumpClient( MyDumpClientHook );
	//_CrtSetReportHook( MyReportHook );
#endif
}

#endif

void memCheckDumpAllocs_Internal(const char *filename, bool printToConsole)
{
	char	*buffer=NULL;
	FILE	*file;

    if ( printToConsole )
    {
	    memMonitorPerLineStatsInternal(defaultHandler, NULL, NULL, 0, 0);
    }
	memMonitorPerLineStatsInternal(estrConcatHandler, &buffer, NULL, 0, 0);

	file = fopen(filename, "wb");
	if (file)
	{
		fwrite(buffer,strlen(buffer),1,file);
		fclose(file);
	}
	estrDestroy(&buffer);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(MemlogDumpFile) ACMD_CATEGORY(Profile);
void memCheckDumpAllocsFile(const char *filename)
{
    memCheckDumpAllocs_Internal(filename, true);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(MemlogDumpFileOnly) ACMD_CATEGORY(Profile);
void memCheckDumpAllocsFileOnly(const char *filename)
{
    memCheckDumpAllocs_Internal(filename, false);
}

// Print out allocation statistics to c:\memlog.txt, and small allocs to c:\memlogSA.txt
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(MemlogDump) ACMD_CATEGORY(Profile);
void memCheckDumpAllocs(void)
{
#if _XBOX
	memCheckDumpAllocs_Internal("devkit:\\xboxmemlog.txt", true);
#else
	memCheckDumpAllocs_Internal("c:\\memlog.txt", true);
#endif
}

#if !PLATFORM_CONSOLE
static void getLastFreeVMemBlock(char **address, SIZE_T *size, U64 *total_free)
{
	SYSTEM_INFO sysinfo;
	char* lastAddress = (char*)0;
	char* curAddress;
	uintptr_t mask;
	*address = 0;
	*size = 0;
	*total_free=0;
	GetSystemInfo(&sysinfo);
	mask = sysinfo.dwAllocationGranularity - 1;
	curAddress = sysinfo.lpMinimumApplicationAddress;  // Everything below this value is off limits.

	while(1)
	{
		MEMORY_BASIC_INFORMATION mbi;

		VirtualQuery(curAddress, &mbi, sizeof(mbi));

		if(mbi.BaseAddress == lastAddress)
		{
			break;
		}

		lastAddress = mbi.BaseAddress;

		if(mbi.State == MEM_FREE)
		{
			// Test if it's allocatable
			PVOID allocationBase = mbi.BaseAddress;
			intptr_t allocationSize = mbi.RegionSize;
			if ((uintptr_t)allocationBase & mask) {
				allocationBase = (PVOID)((uintptr_t)((char*)allocationBase + sysinfo.dwAllocationGranularity) & ~mask);
				allocationSize -= (char*)allocationBase - (char*)mbi.BaseAddress;
			}
			if (allocationSize>0)
			{
#ifdef TEST_IF_ACCESSIBLE
				PVOID mem = VirtualAlloc(allocationBase, allocationSize, MEM_RESERVE, PAGE_NOACCESS);
				if (mem == NULL) {
					// Failed
					printf("Failed to allocate free memory at %p, size %d\n", allocationBase, allocationSize);
				}
				if (mem) {
					assert(mem == allocationBase);
					VirtualFree(allocationBase, 0, MEM_RELEASE);
#endif
					*total_free += allocationSize;
					*address = allocationBase;
					*size = allocationSize;
#ifdef TEST_IF_ACCESSIBLE
				}
#endif
			}
		}

		curAddress += mbi.RegionSize;
	}
}
#endif

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setFreeVirtualMemory(int mb)
{
#if !PLATFORM_CONSOLE
	char *address;
	SIZE_T size;
	U64 totalFree;
	U64 bytes = (U64)mb*1024LL*1024LL;
	getLastFreeVMemBlock(&address, &size, &totalFree);
	while (totalFree > bytes)
	{
		U64 slack = totalFree - bytes;
		char *result;
		if (slack > size) {
			// Take the whole thing
			result = VirtualAlloc(address, size, MEM_RESERVE, PAGE_NOACCESS);
		} else {
			// Take part
			address += (size - slack);
			result = VirtualAlloc(address, slack, MEM_RESERVE, PAGE_NOACCESS);
		}
		if (result != address) {
			printf("Memory allocation failed!\n");
			break;
		}
		getLastFreeVMemBlock(&address, &size, &totalFree);
	}
#endif
}

size_t heapWalk(HANDLE hHeap, int silent, char *logfilename)
{
#if HAVE_HEAP_FUNCTIONS
	PROCESS_HEAP_ENTRY phe = {0};
#endif
	size_t totaldata=0, totaloverhead=0, count=0, totalregions=0, uncommited=0, busy=0, other=0;
	size_t maxuncommited=0;
	void *maxaddr=NULL;
	FILE *logfile=NULL;

	if (logfilename) {
		logfile = fopen(logfilename, "wt");
		if (logfile)
			fprintf(logfile, "DataSize, OverHead,  Address,   Region,    Flags\n");
	}

#if HAVE_HEAP_FUNCTIONS
	while (HeapWalk(hHeap, &phe)) {
		count++;
		totaldata += phe.cbData;
		totaloverhead += phe.cbOverhead;
		if (!silent)
			printf("Heap entry: %8d bytes, %8d overhead at %8Ix\n", phe.cbData, phe.cbOverhead, (intptr_t)phe.lpData);
		if (logfile) {
			fprintf(logfile, "%8d, %8d, %8Id, %8d, %8x\n", phe.cbData, phe.cbOverhead, (intptr_t)phe.lpData, phe.iRegionIndex, phe.wFlags);
		}
		if (phe.wFlags & PROCESS_HEAP_REGION) {
			totalregions++;
		}
		if (phe.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) {
			uncommited += phe.cbData + phe.cbOverhead;
			maxuncommited = (maxuncommited>(phe.cbData + phe.cbOverhead))?maxuncommited:(phe.cbData + phe.cbOverhead);
		}
		if (phe.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
			busy += phe.cbData + phe.cbOverhead;
		}
		if (phe.wFlags & (PROCESS_HEAP_ENTRY_MOVEABLE | PROCESS_HEAP_ENTRY_DDESHARE)) {
			other++;
		}

		maxaddr = (maxaddr>phe.lpData)?maxaddr:phe.lpData;
	}
#endif
	if (logfile) {
		fclose(logfile);
	}
	printf("Total: %Id allocations, %Id bytes, %Id overhead, max address: %p, max uncommited: %Id\n", count, totaldata, totaloverhead, maxaddr, maxuncommited);
	printf("   Num regions: %Id, total uncommited: %Id, total busy (alloced): %Id, num other: %Id\n", totalregions, uncommited, busy, other);
	return (intptr_t)maxaddr;
}

// data == NULL means validate the entire heap
int heapValidate(HANDLE heap, DWORD flags, void *data)
{
#if HAVE_HEAP_FUNCTIONS
	return HeapValidate(heap, flags, data);
#else
	return 1;
#endif
}

HANDLE getProcessHeap(void)
{
	return GetProcessHeap();
}

static int disable_heap_validate_all = 0;
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_HIDE ACMD_CMDLINE;
void disableHeapValidateAll(int value)
{
	disable_heap_validate_all = value;
}

void assertHeapValidateAll(void)
{
	assert(heapValidateAllReturn());
}

// Validates all process heaps and small alloc memory pools
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME(HeapValidateAll);
int heapValidateAllReturn(void)
{
#if !PLATFORM_CONSOLE
	extern HANDLE _crtheap;
#endif
	static bool in_here = false;
	int ret=1,heap_valid;

	if(disable_heap_validate_all)
		return 1;

	if (in_here)
		return 0; // only time this is re-entrant is during a CRT heap validation error

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	in_here = true;

#define DOIT(func) \
	PERFINFO_AUTO_START(#func, 1);	\
	if (!(func)) {	\
		ret = 0;	\
		OutputDebugStringf("%s", "Heap validation failed on " #func "\r\n"); \
	}	\
	PERFINFO_AUTO_STOP()

#if _PS3
#else
	DOIT(_CrtCheckMemory());
#endif
#if !PLATFORM_CONSOLE
	DOIT(heapValidate(_crtheap, 0, NULL));
#endif
	// DOIT(mpVerifyAllFreelists());

	DOIT((heap_valid = heapValidate(GetProcessHeap(), 0, NULL)));

	if (heap_valid)
	{
		DOIT(memTrackValidateHeap());
	}

	in_here = false;

	PERFINFO_AUTO_STOP();

	return ret;
}

int heapValidateAllPeriodic(int period)
{
	static int count=0;
	count++;
	if (count >= period) {
		int ret;
		ret = heapValidateAllReturn();
		if (ret)
			count = 0; // Only reset the count if it's good, so you can step into this function in the debugger without being confused.
		return ret;
	}
	return 1;
}
