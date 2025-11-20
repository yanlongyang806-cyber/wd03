#pragma once
GCC_SYSTEM

#include "wininclude.h"

typedef struct MONITORINFOEX2
{
	MONITORINFOEX base;
	HMONITOR monitor_handle;
	char description[ 64 ];
} MONITORINFOEX2;

#if !_XBOX

int multiMonGetNumMonitors(void);
// Returns the primary monitor if given an invalid monitor index
// Index 0 will always be the *primary* display, not necessary the internal
//  "first" display
void multiMonGetMonitorInfo(int monIndex, MONITORINFOEX *moninfo);

// Call after device resize
void multiMonResetInfo(void);

// Use the monitor handle to retrieve the index of the monitor in the cache.
int multimonFindMonitorHMonitor(HMONITOR monitor_handle);

// Retrieve the index of the primary monitor in the cache.
int multimonGetPrimaryMonitor();

// Gets the indices of monitors which a given window overlaps
int multiMonGetMonitorIndices(HWND hwnd, int *indices, int indices_size);

#endif
