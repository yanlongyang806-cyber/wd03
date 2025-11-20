#pragma once

typedef struct RdrDevice RdrDevice;
typedef struct RdrShellExecuteCommands RdrShellExecuteCommands;
typedef struct WTCmdPacket WTCmdPacket;
typedef struct HWND__ *HWND;
typedef struct HMONITOR__ *HMONITOR;
typedef struct DisplayParams DisplayParams;

void rwinSetTitleDirect(RdrDevice *device, const char *title, WTCmdPacket *packet);
void rwinSetSizeDirect(RdrDevice *device, DisplayParams *dimensions, WTCmdPacket *packet);
void rwinSetPosAndSizeDirect(RdrDevice *device, DisplayParams *dimensions, WTCmdPacket *packet);
void rwinSetIconDirect(RdrDevice *device, int *resource_id_ptr, WTCmdPacket *packet);
void rwinShowDirect(RdrDevice *device, int *pnCmdShow, WTCmdPacket *packet);
void rwinShellExecuteDirect(RdrDevice* device, RdrShellExecuteCommands* pCommands, WTCmdPacket* packet);
void rwinDisplayParamsSetToCoverMonitor(RdrDevice *device, HMONITOR hmon, DisplayParams *dimensions);
void rwinDisplayParamsSetToCoverMonitorForDeviceWindow(RdrDevice *device, HWND hwnd, DisplayParams *dimensions);
void rwinDisplayParamsSetToCoverMonitorForSavedWindow(RdrDevice *device, DisplayParams *dimensions);
void rwinSetWindowProperties(RdrDevice *device, HWND hwnd, int x0, int y0, int *width_inout, int *height_inout, 
	int *refreshRate_inout, int fullscreen, int monitor_index, int maximized, int windowed_fullscreen, int hide,
	bool removeTopmost);

