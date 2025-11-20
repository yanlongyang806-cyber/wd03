#define LAUNCHERCOMM_PARSE_INFO_DEFS
#include "performance.h"
#include "PerformanceCounter.h"
#include "structNet.h"
#include "wininclude.h"
#include "svrGlobalInfo.h"
#include "Autogen/svrGlobalInfo_h_ast.h"
#include "floatAverager.h"
#include "timing.h"
#include "logging.h"
#include "estring.h"
#include "Alerts.h"
#include "ThreadManager.h"
#include "cpu_count.h"

extern int gNumProcessors;
PerformanceCounter *counterNetwork=NULL;
PerformanceCounter *counterCPU=NULL;
TrackedPerformance gPerfInfo;

CRITICAL_SECTION sPerfInfoCriticalSection = {0};

static AlertOnSlowArgs sCounterNetworkArgs =
{
	1.0f,
	120,
	"CounterNetwork"
};

static AlertOnSlowArgs sCounterCPUArgs =
{
	1.0f,
	120,
	"CounterCPU"
};

//when converting cpu usage number for hyper threaded CPUs to the human-readable number, this is the 
//"actual" reported percentage seen internally on a hyperthreaded machine which is reported to the outside
//world as the "equivalent" number
int siHyperThreadingCPUConversionPoint = 45;
AUTO_CMD_INT(siHyperThreadingCPUConversionPoint, HyperThreadingCPUConversionPoint) ACMD_AUTO_SETTING(Launcher, LAUNCHER);

//when converting cpu usage number for hyper threaded CPUs to the human-readable number, this is what the 
//outside world sees at the key point when the internal performance hits the conversion point
int siHyperThreadingCPUConversionEquivalent = 90;
AUTO_CMD_INT(siHyperThreadingCPUConversionEquivalent, HyperThreadingCPUConversionEquivalent) ACMD_AUTO_SETTING(Launcher, LAUNCHER);




void perfGetList(void) 
{ 
	MEMORYSTATUSEX memoryStatus;
	static int inited=0; 
	static TrackedPerformance sLocalPerfInfo = {0};

	PERFINFO_AUTO_START_FUNC();

	if (!inited) { 
		inited = 1; 
		counterNetwork = performanceCounterCreate("Network Interface"); 
		if (counterNetwork) { 
			performanceCounterAdd(counterNetwork, "Bytes Sent/sec", &sLocalPerfInfo.bytesSent); 
			performanceCounterAdd(counterNetwork, "Bytes Received/sec", &sLocalPerfInfo.bytesRead); 
		} 
		counterCPU = performanceCounterCreate("Processor"); 
		if (counterCPU) { 
			performanceCounterAdd(counterCPU, "% Processor Time", &sLocalPerfInfo.cpuUsage); 
		} 
	} 

	if (counterNetwork) 
		performanceCounterQuery(counterNetwork, &sCounterNetworkArgs); 
	if (counterCPU) 
		performanceCounterQuery(counterCPU, &sCounterCPUArgs); 

	sLocalPerfInfo.cpuUsage_Raw = sLocalPerfInfo.cpuUsage;

	if (HyperThreadingEnabled() && siHyperThreadingCPUConversionPoint && siHyperThreadingCPUConversionEquivalent)
	{
		ConvertCPUUsageForHyperThreading(&sLocalPerfInfo.cpuUsage);

	
	}
	

	// Send total memory usage numbers
	ZeroMemory(&memoryStatus,sizeof(MEMORYSTATUSEX));
	memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);

	GlobalMemoryStatusEx(&memoryStatus);

	sLocalPerfInfo.iNumProcessors = gNumProcessors;
	sLocalPerfInfo.bHyperThreading = HyperThreadingEnabled();
	sLocalPerfInfo.iTotalRAM = memoryStatus.ullTotalPhys;
	sLocalPerfInfo.iFreeRAM = memoryStatus.ullAvailPhys;
	sLocalPerfInfo.TotalPageFile = memoryStatus.ullTotalPageFile;
	sLocalPerfInfo.iAvailPageFile = memoryStatus.ullAvailPageFile;
	sLocalPerfInfo.iAvailVirtual = memoryStatus.ullAvailVirtual;

	EnterCriticalSection(&sPerfInfoCriticalSection);
	StructCopy(parse_TrackedPerformance, &sLocalPerfInfo, &gPerfInfo, 0, 0, 0);
	LeaveCriticalSection(&sPerfInfoCriticalSection);

	PERFINFO_AUTO_STOP();
}

static bool firstTime = true;
void perfSendReset()
{
	firstTime = true;
}

static IntAverager *spCPUAverager = NULL;
static IntAverager *spRawCPUAverager = NULL;


void perfSendTrackedInfo(Packet *pak)
{


/*	static TrackedPerformance lastInfo;
	if (firstTime) {
		ParserSend(TrackedPerformanceInfo, pak, NULL, &gPerfInfo, true, false, 0, 0, NULL);
		firstTime = false;
	} else {
		ParserSend(TrackedPerformanceInfo, pak, &lastInfo, &gPerfInfo, false, true, 0, 0, NULL);
	}
	StructCopyAll(TrackedPerformanceInfo, &gPerfInfo, &lastInfo);
*/
	if (!spCPUAverager)
	{
		spCPUAverager = IntAverager_Create(AVERAGE_MINUTE);
		spRawCPUAverager = IntAverager_Create(AVERAGE_MINUTE);
	}

	EnterCriticalSection(&sPerfInfoCriticalSection);
	IntAverager_AddDatapoint(spCPUAverager, gPerfInfo.cpuUsage);
	gPerfInfo.cpuLast60 = IntAverager_Query(spCPUAverager, AVERAGE_MINUTE);
	
	IntAverager_AddDatapoint(spRawCPUAverager, gPerfInfo.cpuUsage_Raw);
	gPerfInfo.cpuLast60_Raw = IntAverager_Query(spRawCPUAverager, AVERAGE_MINUTE);

	ParserSend(parse_TrackedPerformance, pak, NULL, &gPerfInfo, 0, 0, 0, NULL);
	LeaveCriticalSection(&sPerfInfoCriticalSection);
}


void perfLog(void)
{
	char *pFullStr = NULL;

	EnterCriticalSection(&sPerfInfoCriticalSection);
	if (spCPUAverager)
	{
		IntAverager_AddDatapoint(spCPUAverager, gPerfInfo.cpuUsage);
		gPerfInfo.cpuLast60 = IntAverager_Query(spCPUAverager, AVERAGE_MINUTE);
	}
	else
	{
		gPerfInfo.cpuLast60 = gPerfInfo.cpuUsage;
	}


	ParserWriteText(&pFullStr, parse_TrackedPerformance, &gPerfInfo, 0, 0, 0);
	LeaveCriticalSection(&sPerfInfoCriticalSection);

	log_printf(LOG_CRASH, "Perf info: %s", pFullStr);

	estrDestroy(&pFullStr);

}

AUTO_RUN_EARLY;
void InitPerfCritSec(void)
{
	InitializeCriticalSection(&sPerfInfoCriticalSection);
}


static DWORD WINAPI GetPerfList_Thread(LPVOID lpParam)
{
	int iSleepLength = (INT_PTR)lpParam;

	while (1)
	{
		Sleep(iSleepLength);
		perfGetList();
	}

	return 0;
}

//starts doing perfGetList once ever fInterval seconds in a background thread, properly criticalsectioned
void perfBeginBackgroundPerfGetListThread(float fInterval)
{
	static ManagedThread *pPerfThread = NULL;
	if (pPerfThread)
	{
		return;
	}

	pPerfThread = tmCreateThread(GetPerfList_Thread, (void*)((INT_PTR)((int)(fInterval * 1000.0f))));
}



