// Note: these files are also included in various other projects (GetTex)

#if !PLATFORM_CONSOLE

#include "process_util.h"
#include <windows.h>
#include <stdio.h>
#include <Tlhelp32.h>
#include <psapi.h>

#include "timing.h"
#include "earray.h"
#include "StringUtil.h"
#include "estring.h"
#include "utils.h"
#include "fileUtil.h"
#include "UTF8.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // Should be 0 bytes, all tracked to callers

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "psapi.lib")

static char *spKillName1 = NULL;
static char *spKillName2 = NULL;
static char *spRestrictToThisDir = NULL;

int kill(DWORD pid) {
	HANDLE h;
	int ret;
	h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	if (!h) {
		printf("Access denied when opening process (Error code: %d)\n", GetLastError());
		return 1;
	}
	ret = TerminateProcess(h, 0);
	CloseHandle(h);
	return ret;
}


void CheckProcessNameAndID( DWORD processID, bool bDoFrankenBuildAwareComparisons)
{
	char *pProcessName = NULL;
	char *pFileName = NULL;

	// Get a handle to the process.

	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ,
		FALSE, processID );

	// Get the process name.

	if (NULL != hProcess )
	{
		HMODULE hMod;
		DWORD cbNeeded;

		if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod), 
			&cbNeeded) )
		{
			estrStackCreate(&pProcessName);
			estrStackCreate(&pFileName);
			GetModuleBaseName_UTF8( hProcess, hMod, &pProcessName);
			GetModuleFileNameEx_UTF8( hProcess, hMod, &pFileName);
		}
		else 
		{
			CloseHandle( hProcess );
			return;
		}
	}
	else return;

	if (!pProcessName || !pFileName)
	{
		estrDestroy(&pProcessName);
		estrDestroy(&pFileName);
		CloseHandle( hProcess );
		return;
	}


	if (spRestrictToThisDir && !strStartsWith(pFileName, spRestrictToThisDir))
	{
		estrDestroy(&pProcessName);
		estrDestroy(&pFileName);
		CloseHandle( hProcess );
		return;
	}

	if (bDoFrankenBuildAwareComparisons)
	{
		if (FilenamesMatchFrankenbuildAware(pProcessName, spKillName1))
		{
			printf( "Killing PID %6u: %s\n", processID, pProcessName );
			kill(processID);
		}
	}
	else if (stricmp_safe(pProcessName, spKillName1)==0 ||
		stricmp_safe(pProcessName, spKillName2)==0)
	{
		printf( "Killing PID %6u: %s\n", processID, pProcessName );
		kill(processID);
	}

	CloseHandle( hProcess );
	estrDestroy(&pProcessName);
	estrDestroy(&pFileName);
}


void killall(const char * module)
{
	KillAllEx(module, false, NULL, false, false, NULL);
}

//takes in "foo.exe" and returns "fooFD.exe"
void GetFDExeName(char **ppOutName, char *pInName)
{
	estrCopy2(ppOutName, pInName);
	estrInsert(ppOutName, estrLength(ppOutName) - 4, "FD", 2);
}

void KillAllEx(const char * module, bool bSkipSelf, U32 *piPIDsToIgnore, bool bDoFDNameFixup, bool bDoFrankenBuildAwareComparisons, char *pRestrictToThisDir)
{
	// Get the list of process identifiers.
	DWORD aProcesses[1024], cbNeeded, cProcesses, currentPID;
	unsigned int i;

	estrCopy2(&spKillName1, module);
	if (!strEndsWith(spKillName1, ".exe"))
	{
		estrConcatf(&spKillName1, ".exe");
	}

	if (bDoFDNameFixup)
	{
		GetFDExeName(&spKillName2, spKillName1);
	}
	else
	{
		estrClear(&spKillName2);
	}

	if (pRestrictToThisDir)
	{
		estrCopy2(&spRestrictToThisDir, pRestrictToThisDir);
	}
	else
	{
		estrDestroy(&spRestrictToThisDir);
	}


	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return;

	// Calculate how many process identifiers were returned.

	cProcesses = cbNeeded / sizeof(DWORD);

	// Print the name and process identifier for each process.

	currentPID = GetCurrentProcessId();
	for ( i = 0; i < cProcesses; i++ )
	{
		if(!(bSkipSelf && (aProcesses[i] == currentPID) || ea32Find(&piPIDsToIgnore, aProcesses[i]) != -1))
			CheckProcessNameAndID( aProcesses[i], bDoFrankenBuildAwareComparisons );
	}
}





BOOL ProcessNameMatch( DWORD processID , char * targetName, bool bDoFDNameFixup)
{
	char *pProcessName = NULL;
	static char *spFDName = NULL;


	// Get a handle to the process.

	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ,
		FALSE, processID );

	if (bDoFDNameFixup)
	{
		GetFDExeName(&spFDName, targetName);
	}
	else
	{
		estrClear(&spFDName);
	}

	// Get the process name.

	if (NULL != hProcess )
	{
		HMODULE hMod;
		DWORD cbNeeded;

		if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod), 
			&cbNeeded) )
		{
			estrStackCreate(&pProcessName);
			GetModuleBaseName_UTF8( hProcess, hMod, &pProcessName);
		}
		else {
			// If EnumProcessModules fails with a ERROR_PARTIAL_COPY error, it's
			// likely that you are trying to get the name of a 64-bit process and
			// this process is 32-bit. From the docs on MSDN:
			//
			//    If this function is called from a 32-bit application running on WOW64,
			//    it can only enumerate the modules of a 32-bit process. If the process
			//    is a 64-bit process, this function fails and the last error code is
			//    ERROR_PARTIAL_COPY (299).
			//
			CloseHandle( hProcess );
			return false;
		}
	}
	else return false;

	// Print the process name and identifier.
	CloseHandle( hProcess );


	if (stricmp(pProcessName, targetName)==0 || stricmp_safe(pProcessName, spFDName) == 0)
	{
		estrDestroy(&pProcessName);
		return true;
	}
	else
	{
		estrDestroy(&pProcessName);
		return false;
	}
}

int ProcessCount(char * procName, bool bDoFDNameFixup)
{
	// Get the list of process identifiers.
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;
	int count = 0;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return 0;

	// Calculate how many process identifiers were returned.

	cProcesses = cbNeeded / sizeof(DWORD);

	// Print the name and process identifier for each process.

	for ( i = 0; i < cProcesses; i++ )
	{
		if(ProcessNameMatch( aProcesses[i] , procName, true))
			count++;
	}
	return count;
}

bool processExists(char * procName, int iPid)
{
	return ProcessNameMatch(iPid , procName, false);
}

void forEachProcess(ForEachProcessCallback callback,
					void* userPointer)
{
	HANDLE						hSnapshot;
	PROCESSENTRY32				entry = {0};
	S32							notDone;
	ForEachProcessCallbackData	data = {0};
	
	if(!callback){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	data.userPointer = userPointer;

	PERFINFO_AUTO_START("CreateToolhelp32Snapshot", 1);
		hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PERFINFO_AUTO_STOP();

	if(hSnapshot == INVALID_HANDLE_VALUE){
		PERFINFO_AUTO_STOP();
		return;
	}

	entry.dwSize = sizeof(entry);

	PERFINFO_AUTO_START("Process32First", 1);
		notDone = Process32First(hSnapshot, &entry);
	PERFINFO_AUTO_STOP();

	while(notDone)
	{
		UTF16ToEstring(entry.szExeFile, 0, &data.exeFileName);
		data.pid = entry.th32ProcessID;
		data.pidParent = entry.th32ParentProcessID;
		
		PERFINFO_AUTO_START("callback", 1);
			notDone = callback(&data);
		PERFINFO_AUTO_STOP();
		
		if(notDone){
			PERFINFO_AUTO_START("Process32Next", 1);
				notDone = Process32Next(hSnapshot, &entry);
			PERFINFO_AUTO_STOP();
		}
	}

	estrDestroy(&data.exeFileName);

	PERFINFO_AUTO_START("CloseHandle", 1);
		CloseHandle(hSnapshot);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

void forEachThread(	ForEachThreadCallback callback,
					U32 pid,
					void* userPointer)
{
	THREADENTRY32				entry = {0};
	HANDLE						hSnapshot;
	S32							notDone;
	ForEachThreadCallbackData	data = {0};

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("CreateToolhelp32Snapshot", 1);
		hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	PERFINFO_AUTO_STOP();

	if(hSnapshot == INVALID_HANDLE_VALUE){
		PERFINFO_AUTO_STOP();
		return;
	}

	data.userPointer = userPointer;

	entry.dwSize = sizeof(entry);

	PERFINFO_AUTO_START("Thread32First", 1);
		notDone = Thread32First(hSnapshot, &entry);
	PERFINFO_AUTO_STOP();

	while(notDone){
		if(	!pid ||
			entry.th32OwnerProcessID  == pid)
		{
			data.pid = entry.th32OwnerProcessID;
			data.tid = entry.th32ThreadID;
			
			PERFINFO_AUTO_START("callback", 1);
				notDone = callback(&data);
			PERFINFO_AUTO_STOP();
		}

		if(notDone){
			PERFINFO_AUTO_START("Thread32Next", 1);
				notDone = Thread32Next(hSnapshot, &entry);
			PERFINFO_AUTO_STOP();
		}
	}

	PERFINFO_AUTO_START("CloseHandle", 1);
		CloseHandle(hSnapshot);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();// FUNC.
}

void forEachModule(	ForEachModuleCallback callback,
					U32 pid,
					void* userPointer)
{
	ForEachModuleCallbackData	data = {0};
	HMODULE hMods[1024];
	HANDLE hProcess;
	DWORD cbNeeded;
	unsigned int i;
	int notDone;


	PERFINFO_AUTO_START_FUNC();

	data.userPointer = userPointer;
	data.pid = pid;

	
		


	hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                        PROCESS_VM_READ,
                        FALSE, pid );

	if( EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
	{
		for ( i = 0; i < (cbNeeded / sizeof(HMODULE)); i++ )
		{
			TCHAR szModName[MAX_PATH];
			MODULEINFO info;

			// Get the full path to the module's file.

			GetModuleFileNameEx( hProcess, hMods[i], szModName,
										sizeof(szModName) / sizeof(TCHAR));

			UTF16ToEstring(szModName, 0, &data.modulePath);

			GetModuleInformation(hProcess, hMods[i], &info, sizeof(info));
				
			data.baseAddress = info.lpBaseOfDll;
			data.baseSize = info.SizeOfImage;
			
			PERFINFO_AUTO_START("callback", 1);
				notDone = callback(&data);
			PERFINFO_AUTO_STOP();

			if (!notDone)
			{
				break;
			}

		}
	}
	

	estrDestroy(&data.modulePath);

	CloseHandle(hProcess);


	PERFINFO_AUTO_STOP();// FUNC.
}



//cobbled together from this:
//http://social.msdn.microsoft.com/Forums/en/windowssdk/thread/6f79156f-1087-415e-ab79-8a3a9d063527
//and this:
//http://msdn.microsoft.com/en-us/library/aa965225(v=VS.85).aspx



#if _MSC_VER < 1600 // VS10 includes this
typedef union _PSAPI_WORKING_SET_BLOCK {
    ULONG_PTR Flags;
    struct {
        ULONG_PTR Protection  :5;
        ULONG_PTR ShareCount  :3;
        ULONG_PTR Shared  :1;
        ULONG_PTR Reserved  :3;
        ULONG_PTR VirtualPage  :20;
    } ;
} PSAPI_WORKING_SET_BLOCK, 
 *PPSAPI_WORKING_SET_BLOCK;

typedef struct _PSAPI_WORKING_SET_INFORMATION {
    ULONG_PTR NumberOfEntries;
    PSAPI_WORKING_SET_BLOCK WorkingSetInfo[1];
} PSAPI_WORKING_SET_INFORMATION, 
 *PPSAPI_WORKING_SET_INFORMATION;
#endif

U64 GetProcessPrivateWorkingSetRAMUsage(HANDLE hProcHandle)
{
	PSAPI_WORKING_SET_INFORMATION firstWSI = {0};
	static PSAPI_WORKING_SET_INFORMATION *pWSIArray = NULL;
	static size_t iAllocedSize = 0;
	size_t       iSize = 0;
	bool bRet;



	static SYSTEM_INFO *pSysInfo = NULL;

	if (!pSysInfo)
	{
		pSysInfo = calloc(sizeof(SYSTEM_INFO), 1);
		GetSystemInfo(pSysInfo);
	}

	bRet = QueryWorkingSet(
		hProcHandle,&firstWSI,
		sizeof(PSAPI_WORKING_SET_INFORMATION));
	
	if (bRet)
	{
		return pSysInfo->dwPageSize;
	}


	if ((bRet == FALSE) && (GetLastError() == ERROR_BAD_LENGTH))
	{
		U64 iRetVal = 0;

		//make it a bit bigger than needed in case memory grows between our two calls to QueryWorkingSet
		iSize = sizeof(PSAPI_WORKING_SET_INFORMATION) + sizeof(PSAPI_WORKING_SET_BLOCK ) * (firstWSI.NumberOfEntries + 8);

		if (iSize > iAllocedSize)
		{
			if (iAllocedSize == 0)
			{
				pWSIArray = malloc(iSize);
			}
			else
			{
				pWSIArray = realloc(pWSIArray, iSize);
			}
			iAllocedSize = iSize;
		}

		assert(pWSIArray);

		memset(pWSIArray, 0, iSize);
		bRet = QueryWorkingSet(hProcHandle, pWSIArray, (DWORD)iSize);

		if (bRet)
		{
			U32 i;

			for (i = 0; i < pWSIArray->NumberOfEntries; i++)
			{
				if (!pWSIArray->WorkingSetInfo[i].Shared)
				{
					iRetVal += pSysInfo->dwPageSize;
				}
			}

			return iRetVal;
		}


	}




	 return 0;
}





















#endif

