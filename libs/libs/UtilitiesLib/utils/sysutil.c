#include "sysutil.h"
#include <stdio.h>
#include <process.h>

#include "fileutil.h"
#include "error.h"
#include "mathutil.h"
#include "sock.h"
#include "StringUtil.h"
#include "Stackwalk.h"
#include "earray.h"
#include "osdependent.h"
#include "ThreadManager.h"
#include "GlobalTypes.h"
#include "utilitiesLib.h"
#include "timing.h"
#include "alerts.h"
#include "AppRegCache.h"
#include "textparser.h"
#include "TimedCallback.h"
#include "systemspecs.h"
#include "UTF8.h"

#if _PS3
#include <sys/memory.h>
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

//don't begin execution until a debugger is attached
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_HIDE;
void WaitForDebugger(int v)
{
	// Dummy command so that -WaitForDebugger doesn't cause command line parsing errors
}

//don't begin execution until a debugger is attached
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_HIDE;
void WaitForDebugger_Break(int v)
{
	// Dummy command so that -WaitForDebugger doesn't cause command line parsing errors
}

bool gWaitedForDebugger;

// Wait for debugging before proceeding.
void waitForDebugger(bool wait_for_debugger_break)
{

	// Set gWaitedForDebugger, for debugging purposes.
	gWaitedForDebugger = true;

	// Wait for a debugger to attach.
	while (!IsDebuggerPresent())
	{
		printf("WAITING FOR DEBUGGER!\r");
		Sleep(1);
	}
	Sleep(1);

	// Generate a debug breakpoint, if requested.
	if (wait_for_debugger_break)
		_DbgBreak();
}

// Check if -WaitForDebugger or -WaitForDebugger_Break has been requested, and if so, wait.
void checkWaitForDebugger(const char *lpCmdLine, int argc, char *const *argv)
{
	bool wait_for_debugger = false;				// -WaitForDebugger
	bool wait_for_debugger_break = false;		// -WaitForDebugger_Break
	RegWaitForDebugger registry_wait_for_debugger;

	// Check the registry for a request to wait for debugger.
	registry_wait_for_debugger = regGetWaitForDebugger();
	if (registry_wait_for_debugger != REGWAITFORDEBUGGER_NONE)
	{
		wait_for_debugger = true;
		if (registry_wait_for_debugger == REGWAITFORDEBUGGER_WAITFORDEBUGGER_BREAK)
			wait_for_debugger_break = true;
	}

	// Search for "-WaitForDebugger" and "-WaitForDebugger_Break" on the command line, depending on which command line style was passed in.
	if (lpCmdLine)
	{
		assert(!argc && !argv);
		if (strstri(lpCmdLine, "WaitForDebugger"))
		{
			wait_for_debugger = true;
			if (strstri(lpCmdLine, "WaitForDebugger_Break"))
				wait_for_debugger_break = true;
		}
	}
	else
	{
		int wfd_i;

		for (wfd_i = 1; wfd_i < argc; wfd_i++)
		{
			if (strstri(argv[wfd_i], "WaitForDebugger"))
				wait_for_debugger = true;
			if (wait_for_debugger && strstri(argv[wfd_i], "WaitForDebugger_Break"))
			{
				wait_for_debugger_break = true;
				break;
			}
		}
	}

	// If WaitForDebugger was requested, wait for a debugger to attach before proceeding.
	if (wait_for_debugger)
		waitForDebugger(wait_for_debugger_break);

	// FIXME: Raoul put this here, but it does not belong here.  It should be moved somewhere more appropriate.
	sharedMemoryProcessCommandLine();
}

#if !PLATFORM_CONSOLE
#include "psapi.h"

#pragma comment(lib, "Version.lib")

char* getComputerName(){
	static char buffer[1024];
	static WCHAR bufferW[1024];
	int bufferSize = 1024;

	if(!buffer[0])
	{
#ifdef UNICODE
		GetComputerName(bufferW, &bufferSize);
#else
		GetComputerName(buffer, &bufferSize);
		buffer[bufferSize] = '\0';
		MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, buffer, -1, SAFESTR(bufferW));
#endif	
		WideCharToMultiByte (CP_UTF8, 0, bufferW, -1, SAFESTR(buffer), NULL, NULL);
	}
	
	return buffer;
}

char* getExecutableName(){
	static char *pModuleFilename = NULL;
	if (!pModuleFilename)
	{
		int result;
		char long_path_name[CRYPTIC_MAX_PATH];

		result = GetModuleFileName_UTF8(NULL, &pModuleFilename);
		assert(result);		// Getting the executable filename should not fail.

		//The above can return an 8.3 path, if so, convert it
		makeLongPathName(pModuleFilename, long_path_name);
		estrCopy2(&pModuleFilename, long_path_name);
	}

	return pModuleFilename;
}

char *getExecutableDir(char *buf)
{
	char *pTempBuf = NULL;
	GetModuleFileName_UTF8(NULL, &pTempBuf);
	makeLongPathName_safe(pTempBuf, buf, MAX_PATH);
	forwardSlashes(buf);
	estrDestroy(&pTempBuf);
	return getDirectoryName(buf);
}


char *getExecutableTopDir(void)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{
		char fullDir[CRYPTIC_MAX_PATH];
		char *pFirstSlash;
			
		getExecutableDir(fullDir);

		backSlashes(fullDir);

		pFirstSlash = strchr(fullDir, '\\');
		if (pFirstSlash)
		{
			pFirstSlash = strchr(pFirstSlash + 1, '\\');
			
			if (pFirstSlash)
			{
				*(pFirstSlash + 1) = 0;
			}
		}

		spRetVal = strdup(fullDir);
	}

	return spRetVal;
}

char *getExecutableTopDirCropped(void)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{
		char *pSlash;

		estrCopy2(&spRetVal, getExecutableTopDir());
		forwardSlashes(spRetVal);
		while (spRetVal[estrLength(&spRetVal) - 1] == '/')
		{
			estrRemove(&spRetVal, estrLength(&spRetVal) - 1, 1);
		}

		pSlash = strrchr(spRetVal, '/');
		if (pSlash)
		{
			estrRemove(&spRetVal, 0, pSlash - spRetVal + 1);
		}
	}

	return spRetVal;
}



char* getExecutableVersion(int dots){
	return getExecutableVersionEx(getExecutableName(), dots);
}

char* getExecutableVersionEx(char* executableName, int dots){
	int result;
	void* fileVersionInfo;
	char* moduleFilename = executableName;
	int fileVersionInfoSize;
	VS_FIXEDFILEINFO* fileInfo;
	int fileInfoSize;
	static char versionStr[128];

	fileVersionInfoSize = GetFileVersionInfoSize_UTF8(moduleFilename, 0);

	// If the file doesn't have any version information...
	if(!fileVersionInfoSize)
		return NULL;

	// Allocate some buffer space and retrieve version information.
	fileVersionInfo = calloc(1, fileVersionInfoSize);
	result = GetFileVersionInfo_UTF8(moduleFilename, 0, fileVersionInfoSize, fileVersionInfo);
	assert(result);

	result = VerQueryValue(fileVersionInfo, L"\\", &fileInfo, &fileInfoSize);

	#define HIBITS(x) x >> 16
	#define LOWBITS(x) x & ((1 << 16) - 1)
	switch(dots){
	default:
	case 4:
		sprintf_s(SAFESTR(versionStr), "%i.%i.%i.%i", HIBITS(fileInfo->dwFileVersionMS), LOWBITS(fileInfo->dwFileVersionMS), HIBITS(fileInfo->dwFileVersionLS), LOWBITS(fileInfo->dwFileVersionLS));
		break;
	case 3:
		sprintf_s(SAFESTR(versionStr), "%i.%i.%i", HIBITS(fileInfo->dwFileVersionMS), LOWBITS(fileInfo->dwFileVersionMS), HIBITS(fileInfo->dwFileVersionLS));
		break;
	case 2:
		sprintf_s(SAFESTR(versionStr), "%i.%i", HIBITS(fileInfo->dwFileVersionMS), LOWBITS(fileInfo->dwFileVersionMS));
		break;
	case 1:
		sprintf_s(SAFESTR(versionStr), "%i", HIBITS(fileInfo->dwFileVersionMS));
		break;
	}
	
	free(fileVersionInfo);
	return versionStr;
}

/* Function versionCompare()
 *	Determines which of the given versions is newer.
 *	Note that this function will destructively modify the given strings.
 *
 *	It is assumed that the given version numbers are in the xx.xx... format.
 *	There can be as many sub-version number as the filename length will
 *	allow.
 *
 *	FIXME!!! This is copied right out of PatchClient\PatchDlg.c.  It's probably
 *	a bad idea to keep two copies of this thing.
 *	
 *	Returns:
 *		
 *		-1 - Version 2 is newer.
 *		 0 - The two versions are equal.
 *		 1 - Version 1 is newer.
 */
int versionCompare(char* version1, char* version2){
	int v1Num;
	int v2Num;
	char* v1Token;
	char* v2Token;

	char v1Buffer[512];
	char v2Buffer[512];

	strcpy(v1Buffer, version1);
	strcpy(v2Buffer, version2);

	version1 = v1Buffer;
	version2 = v2Buffer;

	while(1){
		// Grab the next version number.
		v1Token = strsep(&version1, ".");
		v2Token = strsep(&version2, ".");

		// The loop has not ended.  It means that a definite answer has
		// not been produced yet.  Therefore, the two versions are currently
		// equal.

		// If version 1 ended first, then version 2 is definitely newer.
		if(!v1Token && v2Token){
			return -1;
		}

		// If version 2 ended first, then version 1 is defintely newer.
		if(v1Token && !v2Token){
			return 1;
		}

		// If both version ended at the same time, no further comparison can be done.
		// Due to our assumption that the two versions have been "equal" so far, we
		// come to the conclusion that they must be equal.
		if(!v1Token && !v2Token){
			return 0;
		}

		ANALYSIS_ASSUME(v1Token);
		ANALYSIS_ASSUME(v2Token);
		// Both versions still have some sub-version numbers for comparison.
		v1Num = atoi(v1Token);
		v2Num = atoi(v2Token);

		if(v1Num > v2Num)
			return 1;
		if(v1Num < v2Num)
			return -1;
	}
}
#endif

unsigned long getPhysicalMemoryEx(unsigned long *max, unsigned long *avail, unsigned long *availVirtual) {
#if _PS3
    int e;
    sys_memory_info_t mem_info;

    e = sys_memory_get_user_memory_size(&mem_info);
    assert(!e);

    if (max) 
		*max = mem_info.total_user_memory;
	if (avail)
		*avail = mem_info.available_user_memory;
	if (availVirtual)
		*availVirtual = 0;

	return mem_info.total_user_memory;
#else
	MEMORYSTATUS memoryStatus;
	ZeroStruct(&memoryStatus);
	memoryStatus.dwLength = sizeof(memoryStatus);

	GlobalMemoryStatus (&memoryStatus);
	if (max) 
		*max = memoryStatus.dwTotalPhys;
	if (avail)
		*avail = memoryStatus.dwAvailPhys;
	if (availVirtual)
		*availVirtual = memoryStatus.dwAvailVirtual;

	return memoryStatus.dwTotalPhys;
#endif
}

U64 getPhysicalMemory64Ex(U64 *max, U64 *avail, U64 *availVirtual)
{
#if PLATFORM_CONSOLE
	unsigned long m, a, av;
	getPhysicalMemory(&m, &a, &av);
	if (max)
		*max = m;
	if (avail)
		*avail = a;
	if (availVirtual)
		*availVirtual = av;
	return m;
#else
	MEMORYSTATUSEX memoryStatus;
	ZeroStruct(&memoryStatus);
	memoryStatus.dwLength = sizeof(memoryStatus);

	GlobalMemoryStatusEx(&memoryStatus);
	if (max) 
		*max = memoryStatus.ullTotalPhys;
	if (avail)
		*avail = memoryStatus.ullAvailPhys;
	if (availVirtual)
		*availVirtual = memoryStatus.ullAvailVirtual;

	return memoryStatus.ullTotalPhys;
#endif
}

unsigned long getPhysicalMemory(unsigned long *max, unsigned long *avail)
{
	return getPhysicalMemoryEx(max, avail, NULL);
}

U64 getPhysicalMemory64(U64 *max, U64 *avail)
{
	return getPhysicalMemory64Ex(max, avail, NULL);
}

U64 getVirtualAddressSize(void)
{
#if _PS3
    return 0;
#else
	char* lastAddress = (char*)0;
	char* curAddress = (char*)0x10016;  // Everything below this value is off limits.

	U64 total = (size_t)curAddress;

	while(1)
	{
		MEMORY_BASIC_INFORMATION mbi;

		VirtualQuery(curAddress, &mbi, sizeof(mbi));

		if(mbi.BaseAddress == lastAddress)
		{
			break;
		}

		lastAddress = (char*)mbi.BaseAddress;
		total += mbi.RegionSize;
		curAddress += mbi.RegionSize;
	}
	return total;
#endif
}

// give correct CR/LF pairs
void expandCRLF(char* target, const char* source)
{
	while (*source)
	{
		if (*source == '\n')
		{
			*target++ = '\r';
			*target++ = '\n';
		}
		else
			*target++ = *source;
		source++;
	}
	*target = 0;
}

// NOTE: This function should be able to have target == source.
void unexpandCRLF(char *target, const char *source)
{
	while (*source)
	{
		if (*source != '\r')
			*target++ = *source;
		source++;
	}
	*target = 0;
}

#if _PS3

char* getExecutableName()
{
    extern char *cl_exec_name;
    return cl_exec_name;
}

char *getExecutableDir(char *buf)
{
	strcpy_unsafe(buf, "/app_home/");
	return buf;
}



void disableRtlHeapChecking(void *heap)
{

}

char* getComputerName()
{
	static char workString[] = "FAKEPS3";
    return workString;
}

#elif !_XBOX
void winCopyToClipboard(const char* s)
{
	HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, strlen(s)*2 + 1); // some extra space to handle CR/LF

	if(handle)
	{
		char* handleCopy;
		handleCopy = GlobalLock(handle);

		if(handleCopy)
		{
			int error;

			// need to switch to correct eoln's
			expandCRLF(handleCopy, s);

			GlobalUnlock(handle);

			if (OpenClipboard(NULL))
			{
				EmptyClipboard();

				handle = SetClipboardData(CF_TEXT, handle);

				if(!handle)
				{			
					error = GetLastError();
				}

				CloseClipboard();
			}
		}
	}
}

void winCopyUTF8ToClipboard(const char* s)
{
	if (OpenClipboard(NULL))
	{
		HGLOBAL *widestr;
		unsigned short *widestrCopy;
		char *replaced = calloc(strlen(s) * 2 + 1, 1); // for CR/LF replacement
		S32 size;

		expandCRLF(replaced, s);
		size = UTF8ToWideStrConvert(replaced, NULL, 0);

		widestr = GlobalAlloc(GMEM_MOVEABLE, sizeof(unsigned short) * (size + 1));
		assert(widestr);
		widestrCopy = GlobalLock(widestr);
		UTF8ToWideStrConvert(replaced, widestrCopy, size + 1);
		GlobalUnlock(widestr);
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, widestr);
		CloseClipboard();

		free(replaced);
	}
}
const char *winCopyFromClipboard(void) // Returns pointer to static buffer
{
	static char *buf=NULL;
	if(OpenClipboard(NULL))
	{
		HANDLE handle = GetClipboardData(CF_TEXT);

		if(handle){
			char* data = GlobalLock(handle);
			size_t len;
			assert(data);
			len = strlen(data)+1;
			buf = realloc(buf, len);
			strcpy_s(buf, len, data);
			GlobalUnlock(handle);
		} else {
			SAFE_FREE(buf);
		}
		CloseClipboard();
	} else {
		SAFE_FREE(buf);
	}
	return buf;
}

const char *winCopyUTF8FromClipboard(void)
{
	static char *buf = NULL;
	if (OpenClipboard(NULL))
	{
		HANDLE handle = GetClipboardData(CF_UNICODETEXT);

		if(handle){
			unsigned short *data = GlobalLock(handle);
			S32 len;
			assert(data);
			len = WideToUTF8StrConvert(data, NULL, 0) + 1;
			buf = realloc(buf, len);
			WideToUTF8StrConvert(data, buf, len);
			unexpandCRLF(buf, buf);
			GlobalUnlock(handle);
		} else {
			SAFE_FREE(buf);
		}
		CloseClipboard();
	} else {
		SAFE_FREE(buf);
	}
	return buf;
}

static HWND g_console_hwnd=NULL;
HWND compatibleGetConsoleWindow(void)
{
	static bool doneOnce=false;
	if (!g_console_hwnd && !doneOnce) {
		typedef HWND (WINAPI *tGetConsoleWindow)(void);
		tGetConsoleWindow pGetConsoleWindow = NULL;
		HINSTANCE hKernel32Dll = LoadLibrary( L"kernel32.dll" );
		if (hKernel32Dll)
		{
			pGetConsoleWindow = (tGetConsoleWindow) GetProcAddress(hKernel32Dll, "GetConsoleWindow");
			
			// This FreeLibrary is okay, since if kernel32.dll exists, then its dynamically linked refcount is > 1 (I think).
			
			FreeLibrary(hKernel32Dll);
		}
		if (pGetConsoleWindow) {
			g_console_hwnd = pGetConsoleWindow();
		} else { // Try manual way
			char buf[1024];
			S16 buf2[1024];
			int tries=6;
			doneOnce = true;
			sprintf_s(SAFESTR(buf), "TempConsoleTitle: %d", _getpid());
			GetConsoleTitle(buf2, ARRAY_SIZE(buf2)-1);
			SetConsoleTitle_UTF8(buf);
			while (g_console_hwnd==NULL && tries) {
				g_console_hwnd = FindWindow_UTF8(NULL, buf);
				if (!g_console_hwnd && tries == 1) {
					printf("Warning: couldn't find window named %s\n", buf);
					Sleep(100);
				}
				tries--;
			}
			SetConsoleTitle(buf2);
		}
	}
	return g_console_hwnd;
}

BOOL compatibleGetProcessWorkingSetSizeEx(__in HANDLE hProcess,
										  __out  PSIZE_T lpMinimumWorkingSetSize,
										  __out  PSIZE_T lpMaximumWorkingSetSize,
										  __out  PDWORD Flags)
{
	typedef BOOL (WINAPI *tGetProcessWorkingSetSizeEx)(HANDLE, PSIZE_T, PSIZE_T, PDWORD);
	typedef BOOL (WINAPI *tGetProcessWorkingSetSize)(HANDLE, PSIZE_T, PSIZE_T);
	HINSTANCE hKernel32Dll = LoadLibrary(L"kernel32.dll");
	if(hKernel32Dll)
	{
		// Try loading the extended version first.
		tGetProcessWorkingSetSizeEx pGetProcessWorkingSetSizeEx = (tGetProcessWorkingSetSizeEx) GetProcAddress(hKernel32Dll, "GetProcessWorkingSetSizeEx");
		if(pGetProcessWorkingSetSizeEx)
		{
			return pGetProcessWorkingSetSizeEx(hProcess, lpMinimumWorkingSetSize, lpMaximumWorkingSetSize, Flags);
		}
		else
		{
			// Then look for the normal one.
			tGetProcessWorkingSetSize pGetProcessWorkingSetSize = (tGetProcessWorkingSetSize) GetProcAddress(hKernel32Dll, "GetProcessWorkingSetSize");
			if(pGetProcessWorkingSetSize)
			{
				*Flags = 0;
				return pGetProcessWorkingSetSize(hProcess, lpMinimumWorkingSetSize, lpMaximumWorkingSetSize);
			}
		}

		FreeLibrary(hKernel32Dll);
	}
	return FALSE;
}


void printProcessWorkingSetSize(void)
{
	SIZE_T minSize;
	SIZE_T maxSize;
	DWORD flags;

	if(!compatibleGetProcessWorkingSetSizeEx(GetCurrentProcess(), &minSize, &maxSize, &flags)){
		printfColor(COLOR_BRIGHT|COLOR_RED, "Failed to get working set info.\n");
	}else{
		printf(	"Working sets: min %"FORM_LL"d, max %"FORM_LL"d, flags %d.\n",
			(S64)minSize,
			(S64)maxSize,
			(U32)flags);
	}
}

static U32 sWindowPIDToHide = 0;
static U32 sWindowPIDToShow = 0;
static BOOL CALLBACK EnumProcHideAndShowWindowByPID(HWND hwnd, LPARAM lParam)
{
	DWORD processID;

	if(GetWindowThreadProcessId(hwnd, &processID))
	{
		if (processID == sWindowPIDToHide)
		{
			ShowWindow(hwnd, SW_HIDE);
		}

		if (processID == sWindowPIDToShow)
		{
			ShowWindow(hwnd, SW_SHOW);
		}
	}

	return TRUE;
}


void HideConsoleWindowByPID(U32 pid)
{
	sWindowPIDToHide = pid;
	EnumWindows(EnumProcHideAndShowWindowByPID, (LPARAM)NULL);
	sWindowPIDToHide = 0;
}
void ShowConsoleWindowByPID(U32 pid)
{
	sWindowPIDToShow = pid;
	EnumWindows(EnumProcHideAndShowWindowByPID, (LPARAM)NULL);
	sWindowPIDToShow = 0;
}


HWND sFoundHWND;
U32 sWindowPIDToFind;

static BOOL CALLBACK EnumProcFindHWND(HWND hwnd, LPARAM lParam)
{
	DWORD processID;

	if(GetWindowThreadProcessId(hwnd, &processID))
	{
		if (processID == sWindowPIDToFind)
		{
			sFoundHWND = hwnd;
		}
	}

	return TRUE;
}


HWND FindHWNDFromPid(U32 pid)
{
	sFoundHWND = 0;
	sWindowPIDToFind = pid;
	EnumWindows(EnumProcFindHWND, (LPARAM)NULL);
	sWindowPIDToFind = 0;
	return sFoundHWND;
}


void hideConsoleWindow(void) {
	compatibleGetConsoleWindow();
	if (g_console_hwnd!=NULL) {
		ShowWindow(g_console_hwnd, SW_HIDE);
	}
}

void showConsoleWindow(void) {
	compatibleGetConsoleWindow();
	if (g_console_hwnd!=NULL) {
		ShowWindow(g_console_hwnd, SW_SHOW);
	}
}

void showConsoleWindow_NoTaskbar(void) {
	compatibleGetConsoleWindow();
	if (g_console_hwnd!=NULL) {
		ShowWindow(g_console_hwnd, SW_NORMAL);
	}
}

bool IsConsoleWindowVisible(void)
{
	WINDOWINFO wInfo;

	compatibleGetConsoleWindow();
	wInfo.cbSize = sizeof(WINDOWINFO);

	if (g_console_hwnd)
	{
		GetWindowInfo(g_console_hwnd, &wInfo);

		if (wInfo.dwStyle & WS_VISIBLE)
		{
			return true;
		}
	}

	return false;
}


int WasLaunchedInNTDebugger(void) {
	long *data;
	int ret=0;
	// When some debug flags are set on the heap, NT clears the allocated
	// memory with 0xbaadf00d, so we check this to see if we were launched
	// in a debugger
	data = HeapAlloc(GetProcessHeap(), 0, 8);
	if (*data == 0xbaadf00d) {
		ret=1;
	}
	HeapFree(GetProcessHeap(), 0, data);
	return ret;

}

void addNoDebugHeapEnvVariable(void)
{
	if (!IsUsingVista()) return; // setx doesn't exist/doesn't work the same pre-Vista
	if (IsUsingWin8()) return; // Windows 8 UAC (which can't be turned off) prevents this from working properly

	system("setx /M _NO_DEBUG_HEAP 1");
}

void disableRtlHeapChecking(HANDLE heap) {
	extern HANDLE _crtheap;
#ifdef _WIN64
	int heapFlagOffset = 6;
#else
	int heapFlagOffset = 4;
	if (IsUsingVista())
		heapFlagOffset = 16;
#endif

	// Note: on Vista, it appears this causes corruption if you call this after
	//  any allocations have occurred, so it should only be called on a freshly created heap

	// Better solution: run gflags.exe (Included in Microsft Debbuging Tools for Windows)
	//   Enter in the application name, choose Image File Options, hit Apply,
	//   this will turn it off for all instances of the program and also fix the
	//   problem with Frees being slow that this hack doesn't fix.
	// Better better solution: set the environment variable _NO_DEBUG_HEAP=1

	// This is dependent on the current implementation of the XP (and 2K and NT?) heap
	// which stores a number of flags 16 bytes into the heap, and checks the bitmask
	// 0x7D030F60 when deciding whether or not to clear the memory with 0xbaadf00d,
	// so, we're clearing all of the bits that may cause the clearing to happen
	if (heap == NULL)
		heap = _crtheap;
	if (WasLaunchedInNTDebugger())
	{
		addNoDebugHeapEnvVariable();

		// This assert is basically because these 2 values are the only values I saw,
		// if there are any values for these flags, they might mean something special,
		// and we should take a closer look to see if we're clearing anything
		// important. 
		if (*((long*)heap + heapFlagOffset) == 0x40001062 || // Vista, no idea what that extra flag is
			*((long*)heap + heapFlagOffset) == 0x40000062 || // Vista
			*((long*)heap + heapFlagOffset) == 0x40000061 ||
			*((long*)heap + heapFlagOffset) == 0x40000060)  // <-- this one indicates "Heap Free Checking" "Heap Tail Checking" "Heap Parameter Checking"
		{
			// Clear the bad bits!
			*((long*)heap + heapFlagOffset) &= ~0x7D030F60;
			if (IsUsingVista() && *((long*)heap + heapFlagOffset+1) == 0x40000060) {
				*((long*)heap + heapFlagOffset+1) &= ~0x7D030F60;
			}
		}
	}
}

void preloadDLLs(int silent)
{
	if (!silent)
	{
		loadstart_printf("Preloading DLLs...");
	}

	// These are dlls that we try to load no matter what

	LoadLibrary(L"C:\\Program Files\\Google\\Google Desktop Search\\GoogleDesktopNetwork1.dll"); // just in case our other trick didn't work

	LoadLibrary(L"nvtt.dll");
	
	// Force this so it shows up in the right memory space
	LoadLibrary(L"hnetcfg.dll");
	LoadLibrary(L"odbc32.dll"); 
	LoadLibrary(L"odbcbcp.dll");

	stackWalkPreloadDLLs();

	// these are just so that the debugger doesn't get confused when you load a dll while debugging
#if 0 // MSVS2005 doesn't have the bug that requires this.
	if (IsDebuggerPresent()) {
		LoadLibrary("uxtheme.dll");
		LoadLibrary("version.dll");
		LoadLibrary("msctfime.ime");
		LoadLibrary("ole32.dll");
		LoadLibrary("comctl32.dll");
		LoadLibrary("winmm.dll");
		LoadLibrary("comdlg32.dll");
		LoadLibrary("shlwapi.dll");
		LoadLibrary("shell32.dll");
		LoadLibrary("Syncor11.dll");
		LoadLibrary("DbgHelp.dll");
		LoadLibrary("CrypticCrashRpt.dll");
		LoadLibrary("hid.dll");
		// check to see if nVidia fixed their DLL yet
		//{ unsigned int fp; SAVE_FP_CONTROL_WORD(fp); assert((fp & 0xffff)==0x007f); }
		//LoadLibrary("nvoglnt.dll");
		//{ unsigned int fp; SAVE_FP_CONTROL_WORD(fp); assert((fp & 0xffff)==0x007f); }
		//SET_FP_CONTROL_WORD_DEFAULT
		LoadLibrary("mcd32.dll");
		LoadLibrary("setupapi.dll");
		LoadLibrary("wintrust.dll");
		LoadLibrary("crypt32.dll");
		LoadLibrary("msasn1.dll");
		LoadLibrary("imagehlp.dll");
		LoadLibrary("ntmarta.dll");
		LoadLibrary("wldap32.dll");
		LoadLibrary("samlib.dll");
		LoadLibrary("mscms.dll");
		LoadLibrary("winspool.drv");
		LoadLibrary("icm32.dll");
		LoadLibrary("opengl32.dll");
		LoadLibrary("msvcrt.dll");
		LoadLibrary("advapi32.dll");
		LoadLibrary("rpcrt4.dll");
		LoadLibrary("gdi32.dll");
		LoadLibrary("user32.dll");
		LoadLibrary("glu32.dll");
		LoadLibrary("ddraw.dll");
		LoadLibrary("dciman32.dll");
		LoadLibrary("dsound.dll");
		LoadLibrary("dinput8.dll");
		LoadLibrary("ws2_32.dll");
		LoadLibrary("ws2help.dll");
		LoadLibrary("imm32.dll");
		LoadLibrary("shimeng.dll");
		LoadLibrary("acgenral.dll");
		LoadLibrary("oleaut32.dll");
		LoadLibrary("msacm32.dll");
		LoadLibrary("userenv.dll");
		LoadLibrary("lpk.dll");
		LoadLibrary("usp10.dll");
		LoadLibrary("psapi.dll");
		LoadLibrary("wsock32.dll");
		LoadLibrary("wdmaud.drv");
		LoadLibrary("msacm32.drv");
		LoadLibrary("midimap.dll");
		LoadLibrary("ksuser.dll");
		LoadLibrary("mswsock.dll");
		LoadLibrary("dnsapi.dll");
		LoadLibrary("winrnr.dll");
		LoadLibrary("rasadhlp.dll");
		LoadLibrary("secur32.dll");
		LoadLibrary("hnetcfg.dll");
		LoadLibrary("wshtcpip.dll");
		LoadLibrary("apphelp.dll");
		LoadLibrary("clbcatq.dll");
		LoadLibrary("comres.dll");
		LoadLibrary("cscui.dll");
		LoadLibrary("cscdll.dll");
		LoadLibrary("browseui.dll");
		LoadLibrary("ntshrui.dll");
		LoadLibrary("atl.dll");
		LoadLibrary("netapi32.dll");
		LoadLibrary("shdocvw.dll");
		LoadLibrary("cryptui.dll");
		LoadLibrary("wininet.dll");
		LoadLibrary("riched20.dll");
		LoadLibrary("mpr.dll");
		LoadLibrary("drprov.dll");
		LoadLibrary("ntlanman.dll");
		LoadLibrary("netui0.dll");
		LoadLibrary("netui1.dll");
		LoadLibrary("netrap.dll");
		LoadLibrary("davclnt.dll");
		LoadLibrary("endpnp.dll");
		LoadLibrary("EBUtil2.dll");
		LoadLibrary("C:\\Program Files\\Logitech\\iTouch\\itchhk.dll");
		LoadLibrary("C:\\Program Files\\Common Files\\Logitech\\Scrolling\\LGMSGHK.DLL");
		LoadLibrary("console.dll");
		LoadLibrary("msctf.dll");
		LoadLibrary("imjp81.ime");
		LoadLibrary("imjp81k.dll");
		LoadLibrary("C:\\WINDOWS\\ime\\imjp8_1\\DICTS\\imjpcd.dic");
	}
#endif

	SET_FP_CONTROL_WORD_DEFAULT;

	if (!silent)
	{
		loadend_printf("done.");
	}
}


HANDLE CreateFileMappingSafe( DWORD lpProtect, int size, const char* handleName, int silent)
{
	HANDLE hMapFile = NULL;
	int iNumTriesLeft = 5; // try 5 times

	while ( !hMapFile && iNumTriesLeft > 0)
	{
		hMapFile = CreateFileMapping_UTF8(INVALID_HANDLE_VALUE, NULL, lpProtect, 0, size, handleName);
		if ( !hMapFile )
		{
			iNumTriesLeft--;
			// wait a second or so, plus some noise
			if (!silent)
				printf("Failed to map file %s, trying again in 1 second. Tries Left = %d\n", handleName, iNumTriesLeft);
			Sleep(1000 + (qrand() % 200));
		}
	}

	if (!hMapFile && !silent) // tried for 5 seconds, no go, so report error
	{
		char *pCBuf = NULL;
		char cFullErrorMessage[1000];
		FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pCBuf, NULL);
		strcpy(cFullErrorMessage, "Failed to map file %s. Windows system error message: ");
		strcat(cFullErrorMessage, pCBuf);
		Errorf(FORMAT_OK(cFullErrorMessage), handleName);
		estrDestroy(&pCBuf);
	}

	return hMapFile;
}

static void showFileMappingError(const char* handleName)
{
	char *pCBuf = NULL;
	FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pCBuf, NULL);
	Errorf("Failed to map file %s. Windows system error message: %s", handleName, pCBuf);
	estrDestroy(&pCBuf);
}

HANDLE OpenFileMappingSafe(DWORD dwDesiredAccess, bool bInheritHandle, const char* handleName, int silent )
{
	HANDLE hMapFile = NULL;
	int iNumTriesLeft = 5; // try 5 times

	while ( !hMapFile && iNumTriesLeft > 0)
	{
		hMapFile = OpenFileMapping_UTF8(dwDesiredAccess, bInheritHandle, handleName);
		if ( !hMapFile )
		{
			iNumTriesLeft--;
			// wait a second or so, plus some noise
			if (!silent)
				printf("Failed to open map of file %s, trying again in 1 second. Tries Left = %d\n", handleName, iNumTriesLeft);
			Sleep(1000 + (qrand() % 200));
		}
	}

	if (!hMapFile && !silent) // tried for 5 seconds, no go, so report error
	{
		showFileMappingError(handleName);
	}

	return hMapFile;
}


LPVOID MapViewOfFileExSafe(HANDLE handle, const char* handleName, void* desiredAddress, int silent )
{
	LPVOID lpMapAddress = NULL;
	int iNumTriesLeft = 5; // try 5 times


	while ( !lpMapAddress && iNumTriesLeft > 0 )
	{
		lpMapAddress = MapViewOfFileEx(handle, // handle to mapping object 
			FILE_MAP_ALL_ACCESS,               // read/write permission 
			0,                                 // Start at 0
			0,                                 // Start at 0
			0,                                 // map entire file
			desiredAddress);				// Base address to map to
		if ( !lpMapAddress )
		{
			iNumTriesLeft--;
			// wait a second or so, plus some noise
			if (!silent)
				printf("Failed to map view of file %s, trying again in 1 second. Tries Left = %d\n", handleName, iNumTriesLeft);
			Sleep(1000 + (qrand() % 200));
		}
	}

	if (!lpMapAddress && !silent) // tried for 5 seconds, no go, so report error
	{
		showFileMappingError(handleName);
	}

	return lpMapAddress;
}

void PreloadSharedAddress(int silent)
{
	HANDLE hMapFile = NULL;
	void* startingAddress = (void*)0x30000000;
	int size = 0x30000000; // 0x28... to 0x58... 768 MB
	SOCKET dummySock;
	LPVOID lpMapAddress;

	// First, map a large swath of virtual memory, so that dlls (like googledesktop) don't insert themselves
	// where we want to map shared memory

	hMapFile = CreateFileMappingSafe(PAGE_READWRITE, size, "MemMapTrickGoogle", silent);
	if ( !hMapFile )
		return;

	lpMapAddress = MapViewOfFileExSafe(hMapFile, "MemMapTrickGoogle", startingAddress, silent);

	if ( !lpMapAddress )
	{
		CloseHandle(hMapFile);
		return;
	}



	// Now, make winsock load (which brings googledesktop, and possibly other unsavory elements)
	sockStart();
	dummySock = socketCreate(AF_INET,SOCK_DGRAM,0);


	// since the address space above is mapped, the dlls must go elsewhere or perish, so we have effectively
	// reserverd that address space for our shared memory sytems


	// Just in case, let's preload the other dlls here too
	preloadDLLs(silent);

	// clean up
	closesocket(dummySock);
	UnmapViewOfFile(lpMapAddress);
	CloseHandle(hMapFile);
}

#else // ifndef _XBOX

#include "sysutil.h"
#include <stdio.h>
#include <process.h>


char* getExecutableName()
{
	//this doesn't get the directory and may not work in commercial release

	char *pCommandLine = GetCommandLine();
	static char tempBuffer[256];
	char *pFirstSpace;
	int iLen;

	if (!pCommandLine)
	{
		return "game:\\GameClientXBOX.exe";
	}

	//skip random leading "
	if (*pCommandLine == '"')
	{
		pCommandLine++;
	}

	//blatantly abusing our safe sprintf technology here
	sprintf(tempBuffer, "game:\\%s", pCommandLine);
	pFirstSpace = strchr(tempBuffer, ' ');
	if (pFirstSpace)
	{
		*pFirstSpace = 0;
	}

	//remove trailing quotes
	iLen = strlen(tempBuffer);

	while (iLen && tempBuffer[iLen - 1] == '"')
	{
		tempBuffer[iLen - 1] = 0;
		iLen--;
	}



	return tempBuffer;
}

char *getExecutableDir(char *buf)
{
	strcpy_unsafe(buf, "game:\\");
	return buf;
}

void disableRtlHeapChecking(void *heap)
{

}

//on the XBOX we steal the computer name out of the file data dir, which contains various extra characters but that's OK
// Or, we used to, and then we noticed there's just a function to get it.  Duh.
char* getComputerName()
{
	static char workString[64] = "";

	char *pTemp;
	const char * const *eaGameDataDirs = fileGetGameDataDirs();

	int i;
	int iLen;
	bool bFoundShare=false;

	if (workString[0])
	{
		return workString;
	}

	{
		DWORD size = ARRAY_SIZE(workString);
		if (DmGetXboxName(workString, &size) == XBDM_NOERR)
			return workString;
	}

	strcpy(workString, "UnknownXbox");
	for (i=0; i<eaSize(&eaGameDataDirs); i++)
	{
		if (strStartsWith(eaGameDataDirs[i], "net:/smb/"))
		{
			strcpy(workString, "Xbox_");
			strncat(workString, eaGameDataDirs[i] + strlen("net:/smb/"), _TRUNCATE);
			pTemp = strchr(workString, '/');
			if (pTemp)
				*pTemp = '\0';
			bFoundShare = true;
		}
	}

	iLen = (int)strlen(workString);

	for (i=0; i < iLen; i++)
	{
		if (!isalnum((unsigned char)workString[i]))
		{
			workString[i] = '_';
		}
	}

	return workString;
}


void winCopyToClipboard(const char* s) {}
const char *winCopyFromClipboard(void) { return ""; }

void winCopyUTF8ToClipboard(const char* s) {}
const char * winCopyUTF8FromClipboard(void) { return ""; }

size_t xboxGetSystemReservedSize(void)
{
	return 32*1024*1024;
}

#endif // ifndef _XBOX else

size_t getProcessPageFileUsage(void)
{
#if PLATFORM_CONSOLE
	unsigned long maxMem, availMem;
	getPhysicalMemory(&maxMem, &availMem);
	return maxMem - availMem;
#else
	PROCESS_MEMORY_COUNTERS pmc={0};
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	return pmc.PagefileUsage;
#endif
}

// This function returns the same values as shown in VMMap, but they do not correlate to getProcessPageFileUsage or anything else useful.
size_t getProcessImageSize()
{
#if PLATFORM_CONSOLE
	return 0;
#else
	MEMORY_BASIC_INFORMATION mbi;
	void *addr=0, *lastaddr=(void*)-1;
	SIZE_T sum_committed=0;
	SIZE_T sum_free=0;
	SIZE_T sum_reserved=0;
	SIZE_T sum_image=0;		// statics show up here
	SIZE_T sum_private=0;	// mallocs show up here
	SIZE_T sum_mapped=0;	// DLLS?
	SIZE_T sum_image0=0;	// statics show up here
	SIZE_T sum_private0=0;	// mallocs show up here
	SIZE_T sum_mapped0=0;	// DLLS?
	SIZE_T sum_image1=0;	// statics show up here
	SIZE_T sum_private1=0;	// mallocs show up here
	SIZE_T sum_mapped1=0;	// DLLS?
	// Private seems to relate to amount malloced
	int count=0;
	while (count < 1024) { // Don't want this to spin out of control!
		VirtualQuery(addr, &mbi, sizeof(mbi));
		if (mbi.BaseAddress == lastaddr)
			break;
		if (mbi.State == MEM_COMMIT) {
			sum_committed += mbi.RegionSize; //TM: 25164/28612K
			if (mbi.Type & MEM_IMAGE) {
				sum_image += mbi.RegionSize;	// 36769792
				sum_image0 += mbi.RegionSize;
			}
			if (mbi.Type & MEM_MAPPED) {
				sum_mapped += mbi.RegionSize;	// 16568320
				sum_mapped0 += mbi.RegionSize;
			}
			if (mbi.Type & MEM_PRIVATE) {
				sum_private += mbi.RegionSize;	// 25788416
				sum_private0 += mbi.RegionSize;
			}
		} else if (mbi.State == MEM_RESERVE) {
			sum_reserved += mbi.RegionSize;
			if (mbi.Type & MEM_IMAGE) {
				sum_image += mbi.RegionSize;
				sum_image1 += mbi.RegionSize;
			}
			if (mbi.Type & MEM_MAPPED) {
				sum_mapped += mbi.RegionSize;
				sum_mapped1 += mbi.RegionSize;
			}
			if (mbi.Type & MEM_PRIVATE) {
				sum_private += mbi.RegionSize;
				sum_private1 += mbi.RegionSize;
			}
		} else if (mbi.State == MEM_FREE) {
			sum_free += mbi.RegionSize;
		}
		lastaddr = addr;
		addr = (char*)addr + mbi.RegionSize;
		count++;
	}
	return sum_image;
#endif
}


AUTO_RUN_EARLY;
void seedExecutableName(void)
{
	// Needed in case we crash
	getExecutableName();
}

static char s_fakeWineVersion[64];
AUTO_CMD_STRING(s_fakeWineVersion, fakeWineVersion) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

static int s_fakeTransgaming;
AUTO_CMD_INT(s_fakeTransgaming, fakeTransgaming) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

const char *getWineVersion(void)
{
#if !PLATFORM_CONSOLE

	if(getIsTransgaming()) {
		// We'll let Transgaming stuff masquerade as WINE 1.5.13, which is the oldest
		// version that supports raw input.
		return "1.5.13";
	}

	if(system_specs.isFilledIn && system_specs.isWine) {

		return system_specs.wineVersion;

	} else if(strlen(s_fakeWineVersion)) {

		return s_fakeWineVersion;

	} else {

		const char * (CDECL *pwine_get_version)(void);
		HMODULE hntdll = GetModuleHandle(L"ntdll.dll");
		if(!hntdll)
			return NULL;

		pwine_get_version = (const char * (CDECL *)(void))GetProcAddress(hntdll, "wine_get_version");
		if(pwine_get_version)
			return pwine_get_version();
	}

#endif
	return NULL;
}

// This is a way to determine if we're running under Transgaming's wrapper without
// actually having to link the Transgaming API lib.
bool getIsTransgaming(void)
{
#if !PLATFORM_CONSOLE

	static bool isTransgaming = false;
	static bool isChecked = false;

	if(s_fakeTransgaming) return true;

	if(!isChecked) {

		bool (WINAPI *pIsTransGaming)(void);
		HMODULE hntdll = GetModuleHandle(L"ntdll.dll");
		if(!hntdll) {
			isChecked = true;
			isTransgaming = false;
			return false;
		}

		pIsTransGaming = (bool (WINAPI *)(void))GetProcAddress(hntdll, "IsTransgaming");
		if(pIsTransGaming) {
			isTransgaming = pIsTransGaming();
			isChecked = true;
			return true;
		}
	}

	return isTransgaming;
#else
	return false;
#endif
}

const char *getTransgamingInfo(void) {

	static char *transgamingInfo = NULL;

	if(!getIsTransgaming()) {
		return NULL;
	}

	if(!transgamingInfo) {
		char *platformInfoStr = NULL;

		LPCSTR (WINAPI *getOS)(void);
		bool (WINAPI *getVersion)(LPSTR, SIZE_T);
		bool (WINAPI *getMacOSVersion)(DWORD*,DWORD*,DWORD*);
		bool (WINAPI *getBundleIdentifier)(LPSTR, DWORD);

		HMODULE hntdll = GetModuleHandle(L"ntdll.dll");
		if(!hntdll) {
			return NULL;
		}

		// TGGetOS
		getOS = (char* (WINAPI *)(void))GetProcAddress(hntdll, "TGGetOS");
		if(getOS) {
			estrConcatf(
				&platformInfoStr,
				"OS:\"%s\" ", getOS());
		}

		// TGGetVersion (Cider build number)
		getVersion = (bool (WINAPI *)(LPSTR, SIZE_T))GetProcAddress(hntdll, "TGGetVersion");
		if(getVersion) {
			char tmp[1024] = {0};
			if(getVersion(tmp, 1023)) {
				tmp[1023] = 0;
				estrConcatf(
					&platformInfoStr,
					"CiderBuildNumber:\"%s\" ",
					tmp);
			}
		}

		// TGMACOSGetVersion
		getMacOSVersion = (bool (WINAPI *)(DWORD*,DWORD*,DWORD*))GetProcAddress(hntdll, "TGMACOSGetVersion");
		if(getMacOSVersion) {
			DWORD major = 0;
			DWORD minor = 0;
			DWORD extra = 0;
			if(getMacOSVersion(&major, &minor, &extra)) {
				estrConcatf(
					&platformInfoStr,
					"MacOSVersion:%ld.%ld.%ld ",
					major, minor, extra);
			}
		}

		// TGMACOSGetBundleIdentifier
		getBundleIdentifier = (bool (WINAPI *)(LPSTR, DWORD))GetProcAddress(hntdll, "TGMACOSGetBundleIdentifier");
		if(getBundleIdentifier) {
			char tmp[1024] = {0};
			if(getBundleIdentifier(tmp, 1023)) {
				estrConcatf(
					&platformInfoStr,
					"BundleIdentifier:%s ",
					tmp);
			}
		}

		transgamingInfo = strdup(platformInfoStr);
		estrDestroy(&platformInfoStr);
	}

	return transgamingInfo;
}

bool getWineVersionNumbers(int *piMajor, int *piMinor, int *piRevision)
{
	const char *wineVer = getWineVersion();
	*piRevision = *piMinor = *piMajor = 0;

	if(wineVer) {

		int *numParts = NULL;
		char *tmpStr = strdup(wineVer);

		{
			char *curChar = tmpStr;
			while(curChar != NULL && curChar[0]) {

				char *nextChar = strchr(curChar, '.');

				if(nextChar) {
					nextChar[0] = 0;
					nextChar++;
				}

				ea32Push(&numParts, atoi(curChar));

				curChar = nextChar;
			}
		}

		if(ea32Size(&numParts) > 0) *piMajor = numParts[0];
		if(ea32Size(&numParts) > 1) *piMinor = numParts[1];
		if(ea32Size(&numParts) > 2) *piRevision = numParts[2];

		ea32Destroy(&numParts);
		free(tmpStr);

		return true;
	}

	return false;
}

bool getWineVersionOrLater(int iMajor, int iMinor, int iRevision)
{
	int wineMajor, wineMinor, wineRevision;
	getWineVersionNumbers(&wineMajor, &wineMinor, &wineRevision);

	if(wineMajor > iMajor) return true;
	if(wineMajor < iMajor) return false;

	if(wineMinor > iMinor) return true;
	if(wineMinor < iMinor) return false;

	if(wineRevision >= iRevision) return true;

	return false;
}

void xperfEnsureRunning(void)
{
#if !PLATFORM_CONSOLE
	if (!(IsUsingVista() && IsUsingX64())) // Have not set this up to work on 32-bit, have not tested on 64-bit XP
		return;
	if (!fileExists("C:\\Night\\tools\\xperf\\xpcirc-autostart.bat"))
		return;
	system_detach("C:\\Night\\tools\\xperf\\xpcirc-autostart.bat", 1, 1);
#endif
}

static bool sbProgrammerMachine = false;

static bool sbForceDoXperf = false;
AUTO_CMD_INT(sbForceDoXperf, ForceDoXperf);

static bool sbForceNoXperf = false;
AUTO_CMD_INT(sbForceNoXperf, ForceNoXperf);

AUTO_RUN_LATE;
void xperfInit(void)
{
	if (regGetMachineMode() == MACHINEMODE_PROGRAMMER)
	{
		sbProgrammerMachine = true;
	}
}

//(seconds) minimum delay between two xperf dumps on the same machine
#define XPERF_CHECK_TIME (10.)
static int siMinDelayBetweenXperfs = 20;
AUTO_CMD_INT(siMinDelayBetweenXperfs, MinDelayBetweenXperfs) ACMD_AUTO_SETTING(Misc, LAUNCHER);

typedef struct XPerfCBData
{
	char *filename;
	char *reason;
	F32 fTotalTime;
} XPerfCBData;

// 5 minute timeout for slow/stalled machines
#define XPERF_TIMEOUT_SECONDS (300.)
void xperf_SendToErrorTrackerCB(TimedCallback *callback, F32 timeSinceLastCallback, XPerfCBData *data)
{
	if (data)
	{
		char fullPath[MAX_PATH];
		sprintf(fullPath, "%s%s", XPERFDUMP_DIRECTORY_PATH, data->filename);
		if (fileExists(fullPath))
		{
			ceSpawnXperf(data->reason, data->filename);
		}
		else if (data->fTotalTime < XPERF_TIMEOUT_SECONDS)
		{
			data->fTotalTime += timeSinceLastCallback;
			TimedCallback_Run(xperf_SendToErrorTrackerCB, data, XPERF_CHECK_TIME);
			return;
		}
		SAFE_FREE(data->filename);
		SAFE_FREE(data->reason);
		free(data);
	}
}

//the internal version is called either via function call (xperfDump()) or cmd. If called by a cmd, someone
//obviously actively wants it done, so forceDump is true. If called via a function, then we use some logic
//that checks whether this is a programmer machine, etc., so as not to be generating xperf dumps willy nilly
//that no one will ever want
static void xperfDump_Internal(const char *filename, const char *reason, bool bForceDump)
{
#if !PLATFORM_CONSOLE
	static CRITICAL_SECTION mutex;
	char buf[1024];
	char fullFileName[MAX_PATH];
	char cwd[MAX_PATH];
	U32 now;
	static U32 last_dump = 0;

	if (!bForceDump)
	{
		if (sbForceNoXperf)
		{
			return;
		}

		if (sbProgrammerMachine && !sbForceDoXperf)
		{
			return;
		}
}	

	// Acquire mutex.
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&mutex);
	ATOMIC_INIT_END;
	EnterCriticalSection(&mutex);

	// If it's been siMinDelayBetweenXperfs seconds since the last dump, take another one.
	now = timeSecondsSince2000();
	if (last_dump <= now - siMinDelayBetweenXperfs)
	{
		XPerfCBData *pData;
		last_dump = now;
		sprintf(fullFileName, "%s.%s.%u.%u.%u", 
			filename, GlobalTypeToShortName(GetAppGlobalType()), GetAppGlobalID(), getpid(), timeSecondsSince2000());
		sprintf(buf, "xpcirc.bat %s", fullFileName);
		fileGetcwd(cwd, ARRAY_SIZE(cwd));
		chdir("C:\\night\\tools\\xperf");
		system_detach(buf, 0, 0);
		chdir(cwd);

		sprintf(buf, "%s.etl", fullFileName);
		pData = malloc(sizeof(XPerfCBData));
		pData->filename = strdup(buf);
		pData->reason = StructAllocString(reason);
		pData->fTotalTime = 0.;
		TimedCallback_Run(xperf_SendToErrorTrackerCB, pData, XPERF_CHECK_TIME);
	}

	// Release mutex.
	LeaveCriticalSection(&mutex);

#endif
}

// TODO(Theo) Add a more descriptive reason to XPerf dumps?
// Dumps the xperf circular buffer
AUTO_COMMAND ACMD_NAME(xperfDump) ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0) ACMD_HIDE;
void xperfDump_Cmd(const char *filename)
{
	xperfDump_Internal(filename, filename, true);
}

AUTO_COMMAND ACMD_NAME(xperfDumpNoForce) ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0) ACMD_HIDE;
void xperfDumpNoForce_Cmd(const char *filename)
{
	xperfDump_Internal(filename, filename, false);
}


void xperfDump(const char *filename)
{
	xperfDump_Internal(filename, filename, false);
}

// Views an xperf Trace
AUTO_COMMAND;
void xperfView(const char *filename)
{
#if !PLATFORM_CONSOLE
	char buf[1024];
	char cwd[MAX_PATH];
	sprintf(buf, "view.bat %s", filename);
	fileGetcwd(cwd, ARRAY_SIZE(cwd));
	chdir("C:\\night\\tools\\xperf");
	system(buf);
	chdir(cwd);
#endif
}

void printExecutableVersion(void)
{
	static bool doneonce=false;
	if (doneonce)
		return;
	if (!GetConsoleWindow())
		return;
	doneonce = true;
	printfColor(COLOR_BRIGHT, "[%s]", getExecutableName());

#if _FULLDEBUG
	printfColor(COLOR_GREEN|COLOR_BLUE, " [Full Debug]");
#else
	printfColor(COLOR_RED|COLOR_GREEN, " [Debug]");
#endif
#if _M_X64
	printfColor(COLOR_GREEN, " [x64]");
#else
	printfColor(COLOR_BLUE|COLOR_BRIGHT, " [x86]");
#endif

	printfColor(COLOR_BRIGHT, "\n");
}

AUTO_RUN_EARLY;
void autoPrintExecutableVersion(void)
{
	if (GetAppGlobalType() != GLOBALTYPE_NONE && GetAppGlobalType() != GLOBALTYPE_GIMMEDLL && !gbCavemanMode)
		printExecutableVersion();
}


void RunHandleExeAndAlert(const char *pAlertKey, const char *pBadFile, char *pShortLogFileName, FORMAT_STR const char *pMessage, ...)
{
	char *pFullLogFileName = NULL;
	char *pAlertString = NULL;
	char *pFullCmdString = NULL;

	estrStackCreate(&pFullLogFileName);
	estrStackCreate(&pAlertString);
	estrStackCreate(&pFullCmdString);

	estrPrintf(&pFullLogFileName, "%s\\%s%s", fileLogDir(), pShortLogFileName, strchr(pShortLogFileName, '.') ? "" : ".log");
	backSlashes(pFullLogFileName);
	

	estrGetVarArgs(&pAlertString, pMessage);
	estrConcatf(&pAlertString, "\nAffected file: %s\nHandle.exe output will be found in %s\n", pBadFile, pFullLogFileName);

	estrPrintf(&pFullCmdString, "cmd /c \"echo Handle output for %s >> %s && handle -accepteula %s >> %s", 
		pBadFile, pFullLogFileName,  pBadFile, pFullLogFileName);

	system_detach(pFullCmdString, 1, 1);

	TriggerAlertDeferred(pAlertKey, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, "%s", pAlertString);

	estrDestroy(&pFullLogFileName);
	estrDestroy(&pAlertString);
	estrDestroy(&pFullCmdString);
}

// Return data for a Windows resource.
void *allocResourceById(const char *type, int id, size_t *size)
{
	HGLOBAL h=NULL;
	WORD langid;
	HRSRC rsrc;
	void *ptr = 0;

	// Try to load from the current locale
	langid = MAKELANGID(PRIMARYLANGID(locGetWindowsLocale(getCurrentLocale())), SUBLANG_DEFAULT);
	rsrc = FindResourceEx(GetModuleHandle(NULL), L"HOGG", MAKEINTRESOURCE(id), langid);
	if (rsrc)
		h = LoadResource(GetModuleHandle(NULL), rsrc);
	if(!h)
	{
		// then from the default locale
		langid = MAKELANGID(PRIMARYLANGID(locGetWindowsLocale(LANGUAGE_DEFAULT)), SUBLANG_DEFAULT);
		rsrc = FindResourceEx(GetModuleHandle(NULL), L"HOGG", MAKEINTRESOURCE(id), langid);
		if (rsrc)
			h = LoadResource(GetModuleHandle(NULL), rsrc);
		if(!h)
		{
			// then from no locale
			rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(id), L"HOGG");
			if (rsrc)
				h = LoadResource(GetModuleHandle(NULL), rsrc);
		}
	}

	// Get the data for the resource.
	if (h)
		ptr = LockResource(h);

	// Get the size of the data.
	if (ptr && size)
		*size = SizeofResource(GetModuleHandle(NULL), rsrc);

	return ptr;
}

// Sleep for a certain number of seconds.
void delay(U32 milliseconds)
{
	Sleep(milliseconds);
}

int CrypticTlsAlloc()
{
	return TlsAlloc();
}

void *CrypticTlsGetValue(int dwTlsIndex)
{
	return TlsGetValue(dwTlsIndex);
}

int CrypticTlsSetValue(int dwTlsIndex, void *lpTlsValue)
{
	return TlsSetValue(dwTlsIndex, lpTlsValue);
}

int CrypticGetLastError(void)
{
	return GetLastError();
}
