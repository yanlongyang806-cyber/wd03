#include "multiMon.h"
#include "monitorDetectEDID.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static CRITICAL_SECTION multimonCritSec;
static MONITORINFOEX **multimon_infos;
#define DEFAULT_PRIMARY_MONITOR_INDEX 0
static int multimon_primaryIndex = DEFAULT_PRIMARY_MONITOR_INDEX;

AUTO_RUN;
void multiMonInitCritSec(void)
{
	InitializeCriticalSection(&multimonCritSec);
}

static BOOL CALLBACK multiMonEnum(
  HMONITOR hMonitor,  // handle to display monitor
  HDC hdcMonitor,     // handle to monitor DC
  LPRECT lprcMonitor, // monitor intersection rectangle
  LPARAM dwData       // data
)
{
	MONITORINFOEX2 *moninfo = callocStruct(MONITORINFOEX2);
	moninfo->base.cbSize = sizeof(moninfo->base);
	GetMonitorInfo(hMonitor, (LPMONITORINFO)&moninfo->base);
	moninfo->monitor_handle = hMonitor;
	// DX11 TODO DJR add matching EDID information up the HMONITOR so we can show monitor name & model
	//monitorDetectEDIDInfo(moninfo);
	eaPush(&multimon_infos, &moninfo->base);
	return TRUE;
}

static void multiMonInit(void) // Must be called within critical section
{
	bool bWasInit=false;
	int i;
	if (!multimon_infos) {
		bWasInit = true;
		EnumDisplayMonitors(NULL, NULL, multiMonEnum, 0);

		// Find the primary
		multimon_primaryIndex = DEFAULT_PRIMARY_MONITOR_INDEX;
		for (i=0; i<eaSize(&multimon_infos); i++)
		{
			if (multimon_infos[i]->dwFlags & MONITORINFOF_PRIMARY)
			{
				multimon_primaryIndex = i;
				break;
			}
		}
	}
	for (i=0; i<eaSize(&multimon_infos); i++)
	{
		assert(multimon_infos[i]);
	}
}

void multiMonResetInfo(void)
{
	EnterCriticalSection(&multimonCritSec);
	eaDestroyEx(&multimon_infos, NULL);
	multimon_primaryIndex = DEFAULT_PRIMARY_MONITOR_INDEX;
	LeaveCriticalSection(&multimonCritSec);
}

int multiMonGetNumMonitors(void)
{
	int ret;
	EnterCriticalSection(&multimonCritSec);
	multiMonInit();
	ret = eaSize(&multimon_infos);
	LeaveCriticalSection(&multimonCritSec);
	return ret;
}

void multiMonGetMonitorInfo(int monIndex, MONITORINFOEX *moninfo)
{
	int numMonitors;
	EnterCriticalSection(&multimonCritSec);
	numMonitors = multiMonGetNumMonitors();
	MIN1(monIndex, numMonitors-1);
	MAX1(monIndex, 0);
	*moninfo = *multimon_infos[monIndex];
	LeaveCriticalSection(&multimonCritSec);
}

int multimonFindMonitorHMonitor(HMONITOR monitor_handle)
{
	int numMonitors, foundMonitorIndex;
	EnterCriticalSection(&multimonCritSec);
	numMonitors = multiMonGetNumMonitors();
	for (foundMonitorIndex = 0; foundMonitorIndex < numMonitors; ++foundMonitorIndex)
	{
		if (((MONITORINFOEX2*)multimon_infos[foundMonitorIndex])->monitor_handle == monitor_handle)
			break;
	}
	LeaveCriticalSection(&multimonCritSec);
	if (foundMonitorIndex == numMonitors)
		foundMonitorIndex = -1;
	return foundMonitorIndex;
}

int multimonGetPrimaryMonitor()
{
	int primaryIndex;
	EnterCriticalSection(&multimonCritSec);
	assert(multimon_primaryIndex != -1);
	multiMonInit();
	primaryIndex = multimon_primaryIndex;
	LeaveCriticalSection(&multimonCritSec);
	return primaryIndex;
}

typedef struct GetIndicesData
{
	int *indices;
	int indices_size;
	int count;
} GetIndicesData;

static BOOL CALLBACK multiMonGetIndicesEnumFunc(
	HMONITOR hMonitor,  // handle to display monitor
	HDC hdcMonitor,     // handle to monitor DC
	LPRECT lprcMonitor, // monitor intersection rectangle
	LPARAM dwData       // data
	)
{
	GetIndicesData *data = (GetIndicesData *)dwData;
	MONITORINFOEX moninfo;
	int i;
	int monindex=-1;
	moninfo.cbSize = sizeof(moninfo);
	GetMonitorInfo(hMonitor, (LPMONITORINFO)&moninfo);
	// Find the index
	for (i=0; i<eaSize(&multimon_infos); i++) {
		if (multimon_infos[i]->rcMonitor.left == moninfo.rcMonitor.left &&
			multimon_infos[i]->rcMonitor.top == moninfo.rcMonitor.top)
		{
			monindex = i;
			break;
		}
	}
	if (monindex != -1)
	{
		if (data->count < data->indices_size)
		{
			data->indices[data->count] = monindex;
			data->count++;
		}
	}
	return TRUE;
}

int multiMonGetMonitorIndices(HWND hwnd, int *indices, int indices_size)
{
	HDC hDC = GetDC(hwnd);
	GetIndicesData data = {0};

	EnterCriticalSection(&multimonCritSec);
	multiMonInit();
	data.indices = indices;
	data.indices_size = indices_size;
	EnumDisplayMonitors(hDC, NULL, multiMonGetIndicesEnumFunc, (LPARAM)&data);
	LeaveCriticalSection(&multimonCritSec);

	ReleaseDC(hwnd, hDC);
	return data.count;
}


