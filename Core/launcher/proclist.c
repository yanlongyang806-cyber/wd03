#include <stdio.h>
#include "wininclude.h"
#include <tlhelp32.h>
#include <psapi.h>
#include "proclist.h"

#include "logging.h"
#include "StashTable.h"
#include "serverlib.h"
#include "process_util.h"
#include "globaltypes.h"
#include "structnet.h"
#include "svrGlobalInfo.h"
#include "autogen/svrGlobalInfo_h_ast.h"
#include "ControllerLink.h"
#include "timing.h"
#include "estring.h"
#include "alerts.h"
#include "performance.h"
#include "proclist_h_ast.h"
#include "ResourceInfo.h"
#include "Launcher.h"
#include "utilitiesLib.h"
#include "WinUtil.h"
#include "TimedCallback.h"

static void CancelIgnoring(void);

int gNumProcessors = 0;
ProcessList			process_list = {0};
static U32			total_mem;

extern StashTable htWindowsToHide;
extern StashTable htWindowsToShow;
extern StashTable htProcessesToAttach;

int giAlertAfterFailedKillTimeout = 10;
AUTO_CMD_INT(giAlertAfterFailedKillTimeout, AlertAfterFailedKillTimeout);

extern U32 *geaiCrypticErrorPIDs;

static int giPidOfProcessBeingIgnored = 0;
U32 giTimeBeganIgnoringProcess = 0;

//key = pid  data = *ProcessInfo
StashTable htProcessListByPid;

#define ERRORID_UNHANDLED -3	// -2 (0xfffffffe) is not allowed by stash table
#define ERRORID_UNKNOWN -1

AUTO_RUN;
void procListInit(void)
{
	htProcessListByPid = stashTableCreateInt(64);
	resRegisterDictionaryForStashTable("Processes", RESCATEGORY_SYSTEM, 0, htProcessListByPid, parse_ProcessInfo);
}

static void addFileTime(FILETIME *a,FILETIME *b,FILETIME *c)
{
	LARGE_INTEGER lia, lib, lic;
	lia.LowPart = a->dwLowDateTime;
	lia.HighPart = a->dwHighDateTime;
	lib.LowPart = b->dwLowDateTime;
	lib.HighPart = b->dwHighDateTime;
	lic.QuadPart = lia.QuadPart + lib.QuadPart;
	c->dwHighDateTime = lic.HighPart;
	c->dwLowDateTime = lic.LowPart;
}

static U32 milliSecondsRunning(FILETIME kt,FILETIME ut)
{
FILETIME	tt;
double		ftime,seconds;
U32			t,ts;
#define SEC_SCALE 0.0000001

	addFileTime(&kt,&ut,&tt);
	seconds = tt.dwLowDateTime * SEC_SCALE;
	ftime = tt.dwHighDateTime * SEC_SCALE * 4294967296.0;
	t = ftime * 1000;
	ts = seconds * 1000;
	return t + ts;
}

static void getProcessName(HANDLE p,char *str, int strSize)
{
	DWORD			dwSize2;
	HMODULE			hMod[1000] ;
	char			szExeFilePath[1000],*exe_name = "total", *s;

	if(!EnumProcessModules(p, hMod, sizeof( hMod ), &dwSize2 ) ) {
		strcpy_s(str,strSize, "UNKNOWN");
		return;
	}

	szExeFilePath[0] = 0;
	if (0==GetModuleFileNameEx( p, hMod[0],szExeFilePath, sizeof( szExeFilePath ) )) {
		szExeFilePath[0] = 0;
	}
	exe_name = strrchr(szExeFilePath,'\\');
	if (!exe_name++)
		exe_name = szExeFilePath;
	if (s = strrchr(exe_name, '.'))
		*s = 0;
	strcpy_s(str,strSize,exe_name);
}

void procInfoPutIntoGlobalInfo(ProcessInfo *pi, LauncherGlobalInfo *pGlobalInfo)
{
	F32		cpu_elapsed,clock_elapsed,cpu_usage,cpu_usage60;
	U32		msecs;
	int		end;
	LauncherProcessInfo *pProcessInfo = StructCreate(parse_LauncherProcessInfo);


	msecs = pi->time_tables[0];
	cpu_elapsed = pi->time_tables[0] - pi->time_tables[1];
	clock_elapsed = process_list.timestamp_tables[0] - process_list.timestamp_tables[1];
	if (process_list.timestamp_tables[1]) {
		cpu_usage = cpu_elapsed / clock_elapsed;
	} else {
		// initial case
		cpu_usage = 0;
	}

	for (end=1; end < NUM_TICKS-1 && process_list.timestamp_tables[end] && pi->time_tables[end]; end++);
	end--;
	cpu_elapsed = pi->time_tables[0] - pi->time_tables[end];
	clock_elapsed = process_list.timestamp_tables[0] - process_list.timestamp_tables[end];
	if (clock_elapsed) {
		cpu_usage60 = cpu_elapsed / clock_elapsed;
	} else {
		cpu_usage60 = 0;
	}

	pProcessInfo->eType = pi->container_type;
	pProcessInfo->ID = pi->container_id;
	pProcessInfo->PID = pi->process_id;
	pProcessInfo->iLowLevelControllerIndex = pi->lowLevelControllerIndex;
	if (pi->bStateStringChanged)
	{
		strcpy_trunc(pProcessInfo->stateString, pi->stateString);
		pi->bStateStringChanged = false;
	}


	pProcessInfo->perfInfo.fFPS = pi->fFPS;
	pProcessInfo->perfInfo.iLongestFrameMsecs = pi->iLongestFrameMsecs;

	//when sending this to controller, express it as seconds in the past to avoid
	//issues of non-synched clocks
	if (pi->iLastContactTime)
	{
		pProcessInfo->perfInfo.iLastContactTime = timeSecondsSince2000() - pi->iLastContactTime;
		if (pProcessInfo->perfInfo.iLastContactTime == 0)
		{
			pProcessInfo->perfInfo.iLastContactTime = 1;
		}
	}


	pi->fCPUUsage = cpu_usage/gNumProcessors;
	pi->fCPUUsageLastMinute = cpu_usage60/gNumProcessors;

	//not sure why these sometimes are bad floats. In any case, zero them out in that case so the controller doesn't think the
	//packet is corrupt
	if (!FINITE(pi->fCPUUsage))
	{
		pi->fCPUUsage = 0;
	}

	if (!FINITE(pi->fCPUUsageLastMinute))
	{
		pi->fCPUUsageLastMinute = 0;
	}

	pProcessInfo->perfInfo.fCPUUsage_raw = 100.0f * pi->fCPUUsage;
	pProcessInfo->perfInfo.fCPUUsageLastMinute_raw = 100.0f * pi->fCPUUsageLastMinute;
	pProcessInfo->perfInfo.fCPUUsage = pProcessInfo->perfInfo.fCPUUsage_raw;
	pProcessInfo->perfInfo.fCPUUsageLastMinute = pProcessInfo->perfInfo.fCPUUsageLastMinute_raw;

	ConvertCPUUsageForHyperThreading_float(&pProcessInfo->perfInfo.fCPUUsage);
	ConvertCPUUsageForHyperThreading_float(&pProcessInfo->perfInfo.fCPUUsageLastMinute);



	if (!FINITE(pProcessInfo->perfInfo.fFPS))
	{
		pProcessInfo->perfInfo.fFPS = 0;
	}

	pProcessInfo->perfInfo.physicalMemUsed = ((U64)pi->mem_used_phys) * 1024;
	pProcessInfo->perfInfo.physicalMemUsedMax = ((U64)pi->mem_used_phys_max) * 1024;
//	pProcessInfo->perfInfo.virtualMemUsed = ((U64)pi->mem_used_virt) * 1024;

	eaPush(&pGlobalInfo->ppProcesses, pProcessInfo);

}

void procSendTrackedInfo(Packet *pak)
{
	int		i;
	LauncherGlobalInfo globalInfo = {0};


	globalInfo.iLastTimeReceivedFromController = giLastControllerTime;
	globalInfo.iPIDOfIgnoredServer = PidOfProcessBeingIgnored();

	if (gNumProcessors==0) {
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		gNumProcessors = sysinfo.dwNumberOfProcessors;
	}


//	procInfoSend(&process_list.total,pak,num_processors); // Scale launcher CPU usage by number of CPUs*/


	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (process_list.ppProcessInfos[i]->container_id >= 0)
		{
			procInfoPutIntoGlobalInfo(process_list.ppProcessInfos[i],&globalInfo); // Send MapServer process info as % of single CPU (only used by ServerMonitor)
		}
	}

	ParserSend(parse_LauncherGlobalInfo, pak, NULL, &globalInfo, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);

	StructDeInit(parse_LauncherGlobalInfo, &globalInfo);


}

static void calcTimers(ProcessInfo *pi,U32 msecs)
{
int		j,d;

	if (!pi->count)
	{
		for(j=0;j<NUM_TICKS;j++)
			pi->time_tables[j] = msecs;
	} else {
		memmove(&pi->time_tables[1],&pi->time_tables[0],(NUM_TICKS-1) * sizeof(U32));
		d = pi->time_tables[0] - msecs;
		if (d > 0)
		{
			// this will (probably) help when 60 days worth of CPU time (2^32 millis) has passed
			for(j=1;j<NUM_TICKS;j++)
				pi->time_tables[j] -= d;
		}
		pi->time_tables[0] = msecs;
	}
}

void notifyProcessClosed(U32 process_id, int container_id, int container_type)
{	
	int dummyInt;

	Packet *pak = pktCreate(GetControllerLink(), LAUNCHERANSWER_PROCESS_CLOSED);
	PutContainerTypeIntoPacket(pak, container_type);
	PutContainerIDIntoPacket(pak, container_id);

	pktSendBitsPack(pak, 1, process_id);
	pktSend(&pak);

	stashIntRemoveInt(htWindowsToHide, process_id, &dummyInt);
	stashIntRemoveInt(htWindowsToShow, process_id, &dummyInt);
	stashIntRemoveInt(htProcessesToAttach, process_id, &dummyInt);

//	printf("Sending process closed message\n");
	printf("Sending process closed message (type %s, ID %d)\n", 
		GlobalTypeToName(container_type), container_id);

}

void notifyProcessCrashed(U32 process_id, int container_id, int container_type, int iErrorID)
{
	int dummyInt;

	Packet *pak;
	
	if (!GetControllerLink())
		return;

	pak = pktCreate(GetControllerLink(), LAUNCHERANSWER_PROCESS_CRASHED);


	//NOTE NOTE NOTE this must be identical to the bits sent for LAUNCHER_ANSWER_CRYPTIC_ERROR_IS_FINISHED_WITH_SERVER
	PutContainerTypeIntoPacket(pak, container_type);
	PutContainerIDIntoPacket(pak, container_id);
	pktSendBits(pak, 32, process_id);
	pktSendBits(pak, 32, iErrorID);
	pktSend(&pak);
	//NOTE NOTE NOTE this must be identical to the bits sent for LAUNCHER_ANSWER_CRYPTIC_ERROR_IS_FINISHED_WITH_SERVER


	stashIntRemoveInt(htWindowsToHide, process_id, &dummyInt);
	stashIntRemoveInt(htWindowsToShow, process_id, &dummyInt);
	stashIntRemoveInt(htProcessesToAttach, process_id, &dummyInt);

	printf("Sending process crashed message\n");

}

static BOOL CALLBACK EnumProcCheckIfCrashed(HWND hwnd, LPARAM lParam)
{
	int *crashed = (int*)lParam;
	char text[500];
	if(GetWindowText(hwnd, text, ARRAY_SIZE(text))) {
		if (0==stricmp(text, "&Send Error Report") ||
			0==stricmp(text, "De&bug") ||
			0==stricmp(text, "Debug") ||
			0==stricmp(text, "&Abort"))
		{
			*crashed=1;
		}
	}
	return !*crashed;
}

//returns true if we care about the window names for this particular process 
bool ProcessWindowNamesAreImportant(DWORD pid)
{
	ProcessInfo *pProcInfo;
	
	if (stashIntFindPointer(htProcessListByPid, pid, &pProcInfo))
	{
		if (pProcInfo->container_type == GLOBALTYPE_CLIENT)
		{
			return true;
		}
	}

	return false;
}

ProcessInfo *FindProcessByPID(int iProcessID)
{
	ProcessInfo *pProcInfo;
	
	if (stashIntFindPointer(htProcessListByPid, iProcessID, &pProcInfo))
	{
		return pProcInfo;
	}

	return NULL;
}



void FoundWindowWithImportantName(DWORD pid, char *pTitle)
{
	ProcessInfo *pProcInfo;
	
	if (stashIntFindPointer(htProcessListByPid, pid, &pProcInfo))
	{
		if (strncmp(pTitle, "GameClient /", 12) == 0)
		{
			char *pStateString = pTitle + 11;

			if (strcmp(pProcInfo->stateString, pStateString) != 0)
			{
				strcpy_trunc(pProcInfo->stateString, pStateString);
				pProcInfo->bStateStringChanged = true;
			}
		}
	}

}




static StashTable htCrashes;

static BOOL CALLBACK EnumProcFindCrashedServers(HWND hwnd, LPARAM lParam)
{
	DWORD processID;

	if(GetWindowThreadProcessId(hwnd, &processID))
	{
		char title[500];
		if(GetWindowText(hwnd, title, ARRAY_SIZE(title)))
		{

			int iErrorID = 0;

			if (ProcessWindowNamesAreImportant(processID))
			{
				FoundWindowWithImportantName(processID, title);
			}

			if(strStartsWith(title, "Crashed Application"))
			{
				iErrorID = atoi(title + 19);
				if (iErrorID == 0)
				{
					iErrorID = ERRORID_UNKNOWN;
				}
			} 
			else if (strEndsWith(title, ".exe") ||
				strStartsWith(title, "Microsoft Visual C"))
			{
				int crashed=0;
				// Might be a crash window
				EnumChildWindows(hwnd, EnumProcCheckIfCrashed, (LPARAM)&crashed);
				if (crashed) {
					iErrorID = ERRORID_UNHANDLED; // unhandled!
				}
			}
			if (iErrorID) 
			{
				stashIntAddInt(htCrashes, processID, iErrorID, false);
			}
		}
	}
	return TRUE;	// signal to continue processing further windows
}


static void findCrashedServers()
{
	if (!htCrashes)
		htCrashes = stashTableCreateInt(16);

	EnumWindows(EnumProcFindCrashedServers, (LPARAM)NULL);
}

void resetCrashedServers(void)
{
	stashTableClear(htCrashes);

}

void MarkCrashedServer(U32 iPid, int iErrorTrackerID)
{
	stashIntAddInt(htCrashes, iPid, iErrorTrackerID ? iErrorTrackerID : ERRORID_UNKNOWN, false);
}



static BOOL CALLBACK EnumProcHideAndShowWindows(HWND hwnd, LPARAM lParam)
{
	DWORD processID;

	if(GetWindowThreadProcessId(hwnd, &processID))
	{
		int dummy;

		if (stashIntFindInt(htWindowsToHide, processID, &dummy))
		{
			stashIntRemoveInt(htWindowsToHide, processID, &dummy);
			ShowWindow(hwnd, SW_HIDE);
		}

		if (stashIntFindInt(htWindowsToShow, processID, &dummy))
		{
			stashIntRemoveInt(htWindowsToShow, processID, &dummy);
			ShowWindow(hwnd, SW_SHOW);
		}
	}

	return TRUE;
}


void HideAndShowWindows()
{
	PERFINFO_AUTO_START_FUNC();
	EnumWindows(EnumProcHideAndShowWindows, (LPARAM)NULL);
	PERFINFO_AUTO_STOP();
}


//returns 0 if the process is not crashed
int getCrashedProcessErrorID(int process_id)
{
	int iResult;
	if ( stashIntFindInt(htCrashes, process_id, &iResult))
		return iResult;
	return 0;
}


void procGetList()
{
	PROCESSENTRY32 pe;
	BOOL retval;
	HANDLE hSnapshot;
	U32		total_mem_phys=0, total_mem_virt=0;
	ProcessInfo	*pi;
	int			i,launcher_count=0;
	FILETIME	total_time = {0,0},zero_time = {0,0};
	PROCESS_MEMORY_COUNTERS mem_counters;
	FILETIME	current_time;
	SYSTEMTIME	current_system_time;
	U32			current_time_millis;
	int			reset_stats=0;
	
	PERFINFO_AUTO_START_FUNC();

	GetSystemTime(&current_system_time); // gets current time
	SystemTimeToFileTime(&current_system_time, &current_time);  // converts to file time format
	current_time_millis = milliSecondsRunning(current_time, zero_time);

	hSnapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
	if (hSnapshot==INVALID_HANDLE_VALUE)
	{
		printf("cannot create snapshot\n");
		PERFINFO_AUTO_STOP();
		return;
	}

	pe.dwSize=sizeof(PROCESSENTRY32);
	retval=Process32First(hSnapshot,&pe);
	while((retval=Process32Next(hSnapshot,&pe)))
	{
		for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
		{
			//if we have exeNameForTracking, then we need to verify that the exe name matches, otherwise it's actually a different process
			//with the same PID, in which case we just pretend we didn't find it in the list... then the "new" one will be added and the
			//old one will be removed
			if (process_list.ppProcessInfos[i]->process_id == pe.th32ProcessID)
			{
				if (process_list.ppProcessInfos[i]->exeNameForTracking[0])
				{
					if (!strstri(pe.szExeFile,process_list.ppProcessInfos[i]->exeNameForTracking))
					{
						continue;
					}
				}
			
				if (process_list.ppProcessInfos[i]->tag)
					process_list.ppProcessInfos[i]->count++;
				process_list.ppProcessInfos[i]->tag = 0;

				if (process_list.ppProcessInfos[i]->iTriedToKillItThen && !process_list.ppProcessInfos[i]->bAlreadyAlertedNonKilling
					&& process_list.ppProcessInfos[i]->iTriedToKillItThen < timeSecondsSince2000() - giAlertAfterFailedKillTimeout)
				{
					process_list.ppProcessInfos[i]->bAlreadyAlertedNonKilling = true;
					TriggerAlertf("KILL_FAILED", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 
						process_list.ppProcessInfos[i]->container_type, 
						process_list.ppProcessInfos[i]->container_id,
						0, 0, NULL, 0, "Launcher was unable to kill process with pid %d (%s)... it's still there %d seconds later",
						process_list.ppProcessInfos[i]->process_id, GlobalTypeAndIDToString(process_list.ppProcessInfos[i]->container_type, process_list.ppProcessInfos[i]->container_id), giAlertAfterFailedKillTimeout);
				}
				break;
			}
		}
		if (i >= eaSize(&process_list.ppProcessInfos))
		{
			if (pe.th32ProcessID)
			{
				pi = calloc(sizeof(ProcessInfo), 1);
				pi->process_id = pe.th32ProcessID;
				pi->container_id = -1;
				pi->container_type = 0;
				strcpy(pi->exename, ExtractShortExeName(pe.szExeFile));

				eaPush(&process_list.ppProcessInfos, pi);

				stashIntAddPointer(htProcessListByPid, pi->process_id, pi, false);
			}
		}
	}
	CloseHandle(hSnapshot);

	findCrashedServers();

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		pi = process_list.ppProcessInfos[i];
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pi'"
		if (pi->tag)
		{
			// Process has exited
			if (pi->process_id == giPidOfProcessBeingIgnored)
			{
				printf("The process we were ignoring is now closed... we can ignore another one\n");
				CancelIgnoring();
			}

			if (pi->container_id>=0) {
				// We should notify the DbServer
				notifyProcessClosed(pi->process_id, pi->container_id, pi->container_type);
			}



			// Add to process_list.total_offset!
			process_list.total_offset += pi->time_tables[0];
		
			stashIntRemovePointer(htProcessListByPid, pi->process_id, NULL);
			eaRemoveFast(&process_list.ppProcessInfos, i);
			i--;
			free(pi);

			continue;
		}
		else
		{
			HANDLE		p;
			U32			msecs;
			FILETIME	cr,ex,kt,ut;

			pi->tag = 1;
			p = OpenProcess(PROCESS_QUERY_INFORMATION| PROCESS_VM_READ,FALSE,pi->process_id);
			if (!p) {
				// Unable to query the process, reset stats, otherwise they'll be very messed up when this process comes back!
				//Apparently on some systems we get processes we can *never* query, so let's not reset the stats every tick,
				// and just deal with the wacky values when we get them =(
				//This also happens if we get a list of process IDs, and the process is
				// closed before we can query it, treat it as closed now then!  Otherwise
				// the DbServer will assume it's closed because it was not reported upon.
				//reset_stats = 1;

				if (pi->process_id == giPidOfProcessBeingIgnored)
				{
					printf("The process we were ignoring is now closed... we can ignore another one\n");
					CancelIgnoring();
				}

				if (pi->container_id>=0)
				{
					log_printf(LOG_ERRORS, "Error querying process (ct: %d, cid: %d, pid: %d, %s), assuming closed\n", pi->container_type, pi->container_id, pi->process_id, pi->exename);
					// We should notify the DbServer
					notifyProcessClosed(pi->process_id, pi->container_id, pi->container_type);
				}
				// Treat as closed for this tick
				process_list.total_offset += pi->time_tables[0];

				stashIntRemovePointer(htProcessListByPid, pi->process_id, NULL);
				eaRemoveFast(&process_list.ppProcessInfos, i);
				i--;
				free(pi);
				continue;
			}
			GetProcessTimes(p,&cr,&ex,&kt,&ut);
			if (GetProcessMemoryInfo(p,&mem_counters,sizeof(mem_counters)))
			{
				pi->mem_used_phys = mem_counters.PagefileUsage >> 10;
				pi->mem_used_phys_max = mem_counters.PeakPagefileUsage >> 10;
				total_mem_phys += pi->mem_used_phys;

//AWERNER apparently PagefileUsage gives us the "physical memory" we want, and "virtual memory" is pointless				
//				pi->mem_used_virt = mem_counters.PagefileUsage >> 10;
//				total_mem_virt += pi->mem_used_virt;
			}
			CloseHandle(p);
	

			msecs = milliSecondsRunning(kt,ut);
			addFileTime(&kt,&total_time,&total_time);
			addFileTime(&ut,&total_time,&total_time);

			calcTimers(pi,msecs);
			if (!pi->crashErrorID && (pi->crashErrorID = getCrashedProcessErrorID(pi->process_id))) {
				notifyProcessCrashed(pi->process_id, pi->container_id, pi->container_type, pi->crashErrorID);
			}
		}

	}

	if (0) {
		process_list.total.mem_used_phys = total_mem_phys;
		process_list.total.mem_used_virt = total_mem_virt;
	} else {
		MEMORYSTATUSEX memoryStatus;
		ZeroMemory(&memoryStatus,sizeof(MEMORYSTATUSEX));
		memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);

		GlobalMemoryStatusEx(&memoryStatus);
		process_list.total.mem_used_phys = (memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys) >> 10;
		process_list.total.mem_used_virt = (memoryStatus.ullTotalPageFile - memoryStatus.ullAvailPageFile) >> 10;
	}

	// Update timestamp_tables
	if (process_list.total.count==0) {
		memset(process_list.timestamp_tables, 0, sizeof(process_list.timestamp_tables));
	}
	memmove(&process_list.timestamp_tables[1], &process_list.timestamp_tables[0], (NUM_TICKS-1)*sizeof(U32));
	process_list.timestamp_tables[0] = current_time_millis;
	calcTimers(&process_list.total,milliSecondsRunning(total_time,zero_time) + process_list.total_offset);
	process_list.total.count++;

	if (reset_stats) {
		process_list.total.count = 0;
	}
	
	resetCrashedServers();

#if 0
	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
		showProc(&process_list.processes[i]);
	showProc(&process_total);
#endif

	PERFINFO_AUTO_STOP();
}

bool trackProcessByKnownProcessID(int iContainerType, int iContainerID, int iProcessID, int iLowLevelControllerIndex, char *pExeName)
{
	int		i,retry;

	for(retry=0;retry<5;retry++)
	{
		for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
		{
			if (process_list.ppProcessInfos[i]->process_id == iProcessID)
			{
				if (!(strstri(process_list.ppProcessInfos[i]->exename, pExeName)))
				{
					printf("For PID %d, looking for name %s, found %s\n", iProcessID, pExeName, process_list.ppProcessInfos[i]->exename);
					continue;
				}

				process_list.ppProcessInfos[i]->container_id = iContainerID;
				process_list.ppProcessInfos[i]->container_type = iContainerType;
				process_list.ppProcessInfos[i]->lowLevelControllerIndex = iLowLevelControllerIndex;
				strcpy(process_list.ppProcessInfos[i]->exeNameForTracking, pExeName);

				return true;
			}
		}
		procGetList();
	}
	return false;
}

U32 trackProcessByExename(int container_id,int container_type, const char *command_line)
{
	char *exe_name, *s;
	char name[MAX_PATH];
	int		i;

	Strncpyt(name, command_line);

	exe_name = strrchr(name,'\\');
	if (!exe_name++)
		exe_name = name;
	if (s = strrchr(exe_name, '.'))
		*s = 0;

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (stricmp(process_list.ppProcessInfos[i]->exename, exe_name)==0)
		{
			process_list.ppProcessInfos[i]->container_id = container_id;
			process_list.ppProcessInfos[i]->container_type = container_type;
			return process_list.ppProcessInfos[i]->process_id;
		}
	}
	return 0;
}

bool processExistsByNameSubString(char *pProcName)
{
	int i;

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (strstri(process_list.ppProcessInfos[i]->exename, pProcName))
		{
			return true;
		}
	}

	return false;
}

void KillProcessFromTypeAndID(int container_id,int container_type)
{
	int i;

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (process_list.ppProcessInfos[i]->container_id == container_id && 
			process_list.ppProcessInfos[i]->container_type == container_type &&
			eaiFind(&geaiCrypticErrorPIDs, process_list.ppProcessInfos[i]->process_id) == -1)
		{
			kill(process_list.ppProcessInfos[i]->process_id);
			process_list.ppProcessInfos[i]->iTriedToKillItThen = timeSecondsSince2000();
			return;
		}
	}
}

void HideProcessFromTypeAndID(int container_id,int container_type, bool bHide)
{
	int i;

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (process_list.ppProcessInfos[i]->container_id == container_id && process_list.ppProcessInfos[i]->container_type == container_type)
		{
			if (bHide)
			{
				stashIntAddInt(htWindowsToHide, process_list.ppProcessInfos[i]->process_id, 0, false);
			}
			else
			{
				stashIntAddInt(htWindowsToShow, process_list.ppProcessInfos[i]->process_id, 0, false);
			}
			return;
		}
	}
}

void HideAllProcesses(bool bHide)
{
	int i;

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (process_list.ppProcessInfos[i]->container_id  && process_list.ppProcessInfos[i]->container_type )
		{
			if (bHide)
			{
				stashIntAddInt(htWindowsToHide, process_list.ppProcessInfos[i]->process_id, 0, false);
			}
			else
			{
				stashIntAddInt(htWindowsToShow, process_list.ppProcessInfos[i]->process_id, 0, false);
			}
		}
	}
}





void KillAllProcessesWithTypeAndID(const char **ppExeNamesNotToKill, U32 *piPIDsNotToKill)
{
	int i;

	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (process_list.ppProcessInfos[i]->container_id >= 0 && process_list.ppProcessInfos[i]->container_type != GLOBALTYPE_MASTERCONTROLPROGRAM)
		{
			if (process_list.ppProcessInfos[i]->process_id != _getpid())
			{
				if (eaFindString(&ppExeNamesNotToKill, process_list.ppProcessInfos[i]->exename) < 0 && ea32Find(&piPIDsNotToKill, process_list.ppProcessInfos[i]->process_id) < 0)
				{
					kill(process_list.ppProcessInfos[i]->process_id);
				}
			}
		}
	}
}



void LogProcInfo(void)
{
	int		i;
	LauncherGlobalInfo globalInfo = {0};
	char *pFullStr = NULL;


	if (gNumProcessors==0) {
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		gNumProcessors = sysinfo.dwNumberOfProcessors;
	}


//	procInfoSend(&process_list.total,pak,num_processors); // Scale launcher CPU usage by number of CPUs*/


	for(i=0;i<eaSize(&process_list.ppProcessInfos);i++)
	{
		if (process_list.ppProcessInfos[i]->container_id >= 0)
		{
			procInfoPutIntoGlobalInfo(process_list.ppProcessInfos[i],&globalInfo); // Send MapServer process info as % of single CPU (only used by ServerMonitor)
		}
	}

	ParserWriteText(&pFullStr, parse_LauncherGlobalInfo, &globalInfo, 0, 0, 0);
	log_printf(LOG_CRASH, "Proc info: %s", pFullStr);
	estrDestroy(&pFullStr);

	StructDeInit(parse_LauncherGlobalInfo, &globalInfo);


}

U32 PidOfProcessBeingIgnored(void)
{
	return giPidOfProcessBeingIgnored;
}


static TimedCallback *pIgnoreCB = NULL;

static ContainerID giContainerIDOfProcessBeingIgnored;
static GlobalType giContainerTypeOfProcessBeingIgnored;

static char *spDuration1 = NULL;
static char *spDuration2 = NULL;


static void IgnoreCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	pIgnoreCB = NULL;
	kill(giPidOfProcessBeingIgnored);
	giPidOfProcessBeingIgnored = 0;

}
void BeginIgnoringProcess(ProcessInfo *pProcess, int iTimeToLeaveIt)
{
	char temp[1024];

	CancelIgnoring();

	sprintf(temp, "%d IGNORED - %s[%u] was wedged, now being ignored by %s", pProcess->process_id,
		GlobalTypeToName(pProcess->container_type), pProcess->container_id, GetShardNameFromShardInfoString());
	SetWindowTitleByPID(pProcess->process_id, temp);

	giPidOfProcessBeingIgnored = pProcess->process_id;
	giContainerIDOfProcessBeingIgnored = pProcess->container_id;
	giContainerTypeOfProcessBeingIgnored = pProcess->container_type;
 
	pIgnoreCB = TimedCallback_Run(IgnoreCB, (void*)(intptr_t)iTimeToLeaveIt, iTimeToLeaveIt); 

	pProcess->container_id = -1;
	pProcess->container_type = 0;
	giTimeBeganIgnoringProcess = timeSecondsSince2000();
}

static void CancelIgnoring(void)
{
	giPidOfProcessBeingIgnored = 0;

	if (pIgnoreCB)
	{
		TimedCallback_Remove(pIgnoreCB);
		pIgnoreCB = NULL;
	}
}



#include "proclist_h_ast.c"
