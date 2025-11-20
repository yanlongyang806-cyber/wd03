#include "rt_winutil.h"
#include "RdrDevice.h"
#include "wininclude.h"
#include "RenderLib.h"
#include "earray.h"
#include "RdrDevicePrivate.h"
#include "Message.h"
#include "winutil.h"
#include "memlog.h"
#include "osdependent.h"
#include "RdrState.h"
#include "RdrDeviceTrace.h"
#include "UTF8.h"

//////////////////////////////////////////////////////////////////////////
// Title

void rwinSetTitleDirect(RdrDevice *device, const char *title, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	if (!title || !title[0])
		return;

	device->title_need_update = 1;
	strcpy(device->current_title, title);
#endif
}

//////////////////////////////////////////////////////////////////////////
// Size

void rwinManualChangeDisplayMode(RdrDevice *device, HWND hwnd, int x0, int y0, int *width_inout, int *height_inout, int *refreshRate_inout, int fullscreen, int monitor_index, int maximized, int windowed_fullscreen, int hide)
{
	static S32 isFullScreenNow;
	GfxResolution **supported_resolutions=0;
	GfxResolution *desktop_res=0;
	DEVMODE dm;
	int useRefreshRate = 1;
	int width = *width_inout;
	int height = *height_inout;
	int refreshRate = *refreshRate_inout;

	// DX11 TODO DJR we need different mode selection code here - DX9 has different mode constraints than DX11,
	// we may need to hang the allowed modes off the RdrDevice
	assert(0);
	supported_resolutions = rdrGetSupportedResolutions(&desktop_res, monitor_index, NULL);

	if (refreshRate < 60)
		useRefreshRate = 0;

	if (fullscreen)
	{		
		rdrGetClosestResolution(&width, &height, &refreshRate, fullscreen, monitor_index);
		*width_inout = width;
		*height_inout = height;
		*refreshRate_inout = refreshRate;
	}

	if (fullscreen)
	{
		int fullScreenResult, i;
		
		// Find the Device Name for the currently selected monitor
		MONITORINFOEX moninfo;
		const S16 *deviceName;
		multiMonGetMonitorInfo(monitor_index, &moninfo);
		deviceName = moninfo.szDevice;

		memset(&dm,0,sizeof(dm));
		dm.dmSize       = sizeof(dm);
		dm.dmPelsWidth  = width;
		dm.dmPelsHeight = height;
		dm.dmDisplayFrequency = refreshRate;
		dm.dmBitsPerPel = 32;//game_state.screendepth;
		dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | ((useRefreshRate&&refreshRate)?DM_DISPLAYFREQUENCY:0);

		/* DJR This is only for debugging: tracing if the display change actually sends any messages. 
		// I wouldn't typically want it enabled in a production build since other code may use WM_USER
		SendMessage(hwnd, WM_USER, 0x2badbeef, 0xacacacac);
		*/
		//// JE: It appears this call might not even be needed, at least on DX9 on XP.  Still changes resolution when we call Reset() in DirectX - would have to change the ClipCursor code to be smarter/after that though
		TRACE_DEVICE(device, "ChangeDisplaySettingsEx (%d x %d)\n", width, height);
		fullScreenResult = ChangeDisplaySettingsEx(deviceName, &dm, NULL, CDS_FULLSCREEN, NULL);

		if (fullScreenResult != DISP_CHANGE_SUCCESSFUL)
		{
			char * errormsg;
			typedef struct Errors { int errorCode; char msg[200]; } Errors;
			Errors errors[] = { /*weird that dualview isn't defined*/
				{ -6 /*DISP_CHANGE_BADDUALVIEW*/,  "DualViewCapable"  },
				{ DISP_CHANGE_BADFLAGS,     "BadFlags"  },
				{ DISP_CHANGE_BADMODE,      "UnsupportedGraphics"  },
				{ DISP_CHANGE_BADPARAM,     "BadFlagParam" }, 
				{ DISP_CHANGE_FAILED,       "DriverFailed"  },
				{ DISP_CHANGE_NOTUPDATED,   "RegistryFailed"  },
				{ DISP_CHANGE_RESTART,      "RestartComputer"  },
				{ 0, 0 },
			};

			errormsg = "UnknownFailure";
			for (i = 0; errors[i].errorCode != 0; i++)
			{
				if (fullScreenResult == errors[i].errorCode) 
					errormsg = errors[i].msg;
			}

			rdrAlertMsg(device, TranslateMessageKeyDefault("FullscreenFailure", "FullscreenFailure"));

			fullscreen = 0;
			width = 800;
			height = 600;
			refreshRate = 0;
		}
		else
		{
			// successfully changed resolution
			// Get the appropriate coordinates for the full-screen window
			//RECT screenRect = {x0, y0, x0+width, y0+height};
			multiMonResetInfo(); // Reset after resolution change
			multiMonGetMonitorInfo(monitor_index, &moninfo);
			x0 = moninfo.rcMonitor.left;
			y0 = moninfo.rcMonitor.top;
			refreshRate = dm.dmDisplayFrequency;
		}
	} else {
		if(isFullScreenNow){
			TRACE_DEVICE(device, "ChangeDisplaySettingsEx reset to desktop settings\n");
			ChangeDisplaySettingsEx(NULL, NULL, NULL, CDS_FULLSCREEN, NULL);
		}
	}
	isFullScreenNow = !!fullscreen;

	multiMonResetInfo(); // Reset after possible resolution change
	device->thread_cursor_needs_refresh = true;
}

void rwinDisplayParamsSetToCoverMonitor(RdrDevice *device, HMONITOR hmon, DisplayParams *dimensions)
{
	MONITORINFOEX moninfo;
	moninfo.cbSize = sizeof(moninfo);
	if (!GetMonitorInfo(hmon, (LPMONITORINFO)&moninfo))
		memlog_printf(NULL, "GetMonitorInfo failed (0x%x)", GetLastError());
	else
	{
		dimensions->xpos = moninfo.rcMonitor.left;
		dimensions->ypos = moninfo.rcMonitor.top;
		dimensions->width = moninfo.rcMonitor.right - moninfo.rcMonitor.left;
		dimensions->height = moninfo.rcMonitor.bottom - moninfo.rcMonitor.top;
	}
}

void rwinDisplayParamsSetToCoverMonitorForDeviceWindow(RdrDevice *device, HWND hwnd, DisplayParams *dimensions)
{
	// This must match Windows' maximize behavior
	HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	rwinDisplayParamsSetToCoverMonitor(device, hmon, dimensions);
}

void rwinDisplayParamsSetToCoverMonitorForSavedWindow(RdrDevice *device, DisplayParams *dimensions)
{
	// This must match Windows' maximize behavior
	RECT rcWindow = {dimensions->xpos, dimensions->ypos, 
		dimensions->xpos + dimensions->width, dimensions->ypos + dimensions->height};
	HMONITOR hmon = MonitorFromRect(&rcWindow, MONITOR_DEFAULTTONEAREST);
	rwinDisplayParamsSetToCoverMonitor(device, hmon, dimensions);
}

void rwinSetWindowProperties(RdrDevice *device, HWND hwnd, int x0, int y0, int *width_inout, int *height_inout,
	int *refreshRate_inout, int fullscreen, int monitor_index, int maximized, int windowed_fullscreen, int hide,
	bool removeTopmost)
{
#if !PLATFORM_CONSOLE
	RECT client_rect, wnd_rect;
	DWORD	window_style;
	DEVMODE dm;
	WINDOWPLACEMENT window_pos = { 0 };
	int width = *width_inout;
	int height = *height_inout;

	if (monitor_index < 0)
		monitor_index = 0;

	if (rdr_state.useManualDisplayModeChange)
	{
		rwinManualChangeDisplayMode(device, hwnd, x0, y0, width_inout, height_inout, refreshRate_inout, fullscreen, monitor_index, maximized, windowed_fullscreen, hide);
	}

	if (!fullscreen)
	{
		EnumDisplaySettings(NULL,ENUM_CURRENT_SETTINGS,&dm);
		if (dm.dmBitsPerPel < 32)
			rdrAlertMsg(device, TranslateMessageKeyDefault("RunIn32BitorDie", "RunIn32BitorDie"));
	}

	window_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	if (!fullscreen && windowed_fullscreen)
	{
		// This calculation must match rwinDisplayParamsSetToCoverMonitorForDeviceWindow,
		HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFOEX moninfo;
		moninfo.cbSize = sizeof(moninfo);
		GetMonitorInfo(hmon, (LPMONITORINFO)&moninfo);
		x0 = moninfo.rcMonitor.left;
		y0 = moninfo.rcMonitor.top;
		width = moninfo.rcMonitor.right - moninfo.rcMonitor.left;
		height = moninfo.rcMonitor.bottom - moninfo.rcMonitor.top;

		window_style |= WS_POPUP|WS_MINIMIZEBOX|WS_MAXIMIZE;
	}
	else if (!fullscreen)
		window_style |= WS_OVERLAPPEDWINDOW;
	else
		window_style |= WS_POPUP;
	if (!hide)
		window_style |= WS_VISIBLE;


	TRACE_DEVICE(device, "rwinSetWindowProperties SetWindowLongPtr GWL_STYLE %x\n", window_style);
	SetWindowLongPtr(hwnd, GWL_STYLE, window_style);

	// To ensure client area is the desired size, resize Window outer dimensions based
	// on the style we set
	{
		RECT a_rcPos = {0, 0, width, height};

		AdjustWindowRect(&a_rcPos, window_style, FALSE);
		width = a_rcPos.right - a_rcPos.left;
		height = a_rcPos.bottom - a_rcPos.top;
	}
	if (1)
	{
		if (removeTopmost || !fullscreen || !rdrSupportsFeature(device, FEATURE_DX11_RENDERER))
		{
			// Have to use SetWindowPos to work in Full screen when the task bar is at the top
			TRACE_DEVICE(device, "SetWindowPos(0x%p, %s, (%d, %d), %d x %d, SWP_FRAMECHANGED%s) %s\n", 
				hwnd, removeTopmost ? "HWND_NOTOPMOST" : "HWND_TOP", x0, y0, width, height,
				removeTopmost ? "" : " | SWP_NOZORDER", fullscreen ? "Fullscreen" : "Windowed");
			SetWindowPos(hwnd, removeTopmost ? HWND_NOTOPMOST : HWND_TOP, x0, y0, width, height, 
				removeTopmost ? SWP_FRAMECHANGED : (SWP_FRAMECHANGED | SWP_NOZORDER));
			if (!hide)
			{
				TRACE_DEVICE(device, "ShowWindow(%s)\n", !fullscreen && maximized ? "Maximized" : "Default");
				ShowWindow(hwnd, (window_style & WS_MAXIMIZE) || (!fullscreen && maximized) ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT );
			}
		}
	} else {
		TRACE_DEVICE(device, "SetWindowPos(0x%p, HWND_TOP, (%d, %d) %d x %d, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE) %s\n",
			hwnd, x0, y0, width, height, fullscreen ? "Fullscreen" : "Windowed");
		// update just the frame style, SetWindowPlacement takes care of the normal/restore pos
		SetWindowPos(hwnd, HWND_TOP, x0, y0, width, height, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE);

		window_pos.length = sizeof(window_pos);
		window_pos.flags = 0;
		window_pos.showCmd = SW_HIDE;
		if (!hide)
			window_pos.showCmd = !fullscreen && maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT;
		window_pos.ptMinPosition.y = window_pos.ptMinPosition.x = 0;
		window_pos.ptMaxPosition.x = window_pos.ptMaxPosition.x = 0;
		window_pos.rcNormalPosition.left = x0;
		window_pos.rcNormalPosition.top = y0;
		window_pos.rcNormalPosition.right = x0 + width;
		window_pos.rcNormalPosition.bottom = y0 + height;
		TRACE_DEVICE(device, "SetWindowPlacement\n");
		SetWindowPlacement(hwnd, &window_pos);
	}

	if (!hide)
	{
		TRACE_DEVICE(device, "UpdateWindow NULL");
#pragma warning(suppress:6309) // UpdateWindow() isn't supposed to accept NULL - what does this do?
#pragma warning(suppress:6387)
		UpdateWindow(NULL);
		TRACE_DEVICE(device, "UpdateWindow hwnd");
		UpdateWindow(hwnd);
		TRACE_DEVICE(device, "SetFocus hwnd");
		SetFocus(hwnd);
		TRACE_DEVICE(device, "SetForegroundWindow hwnd");
		SetForegroundWindow(hwnd);
	}

	// Query the window for it's actual client rect size,
	// in case the window was maximized, for example.
	GetClientRect(hwnd, &client_rect);
	GetClientRect(hwnd, &wnd_rect);
	TRACE_DEVICE(device, "Client %d x %d %s GWR %d x %d\n", client_rect.right, client_rect.bottom, fullscreen ? "Fullscreen" : "Windowed",
		wnd_rect.right - wnd_rect.left, wnd_rect.bottom - wnd_rect.top);
	// When going to fullscreen mode, we never use any part of the window dimensions (window rect, position, client rect) as the resolution.
	// We only use the exact, originally-requested mode information, because the client rect may be inconsistent with the resolution, causing
	// Reset or display mode change failures. Otherwise, we use the client rect as the size, provided it is non-zero.
	if (!fullscreen)
	{
		*width_inout = client_rect.right;
		*height_inout = client_rect.bottom;
	}
#endif
}

void rwinSetSizeDirect(RdrDevice *device, DisplayParams *dimensions, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	int refreshRate;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	DisplayParams initialState;
	bool bInitialFullscreenWindowState;
	bool bFinalFullscreenWindowState;
	bool bRemoveTopmost;
	bool bTransitionFullscreenToWFS;
	bool bProgrammaticWindowReshape;
	device->getSize(device, &initialState);

	refreshRate = 0;
	if (dimensions->fullscreen)
	{
		const RdrDeviceInfo *preferred_device = device_infos[ dimensions->preferred_adapter ];
		if (preferred_device && dimensions->preferred_fullscreen_mode < eaSize(&preferred_device->display_modes))
			refreshRate = preferred_device->display_modes[ dimensions->preferred_fullscreen_mode ]->refresh_rate;
		else
		{
			// no fullscreen support reported during enumeration
			dimensions->fullscreen = 0;
			refreshRate = 0;
		}
	}

	// Fancier code that allows toggling between full-screen

	// Flags description:
	//  Not completely sure what's going on, but on XP, we get D3D debug warnings, and possibly crashes if we call
	//    reset any time in the middle when we might not know our correct width/height/fullscreenness
	//  On Vista, it would generate a crash if we called the first reset, but after fixing this it creates
	//    a D3D warning if we skip the intermediate resets for some reason (the same ones that cause a
	//    warning on XP if they're there), so these flags were set empirically, there's probably some better way.
	TRACE_DEVICE(device, "Resize start\n");

	device->resizeDeviceDirect(device, dimensions, RDRRESIZE_PRECOMMIT_START);

	// Fullscreen and windowed fullscreen are both "fullscreen-like" states with respect to
	// the window style. Transitioning between either fullscreen-like state and windowed
	// requires manual window style and layout management.
	//
	// Toggle titlebar & frame when transitioning between W & FS, or W & WFS.
	// Always manually update window position & size to cover the screen when ending in WFS.
	//
	//     x		
	// W------FS
	//	\	  /
	// x \	 /
	//    \ /
	//    WFS
	bInitialFullscreenWindowState = !!(initialState.fullscreen || initialState.windowed_fullscreen);
	bFinalFullscreenWindowState = !!(dimensions->fullscreen || dimensions->windowed_fullscreen);
	bProgrammaticWindowReshape = dimensions->xpos != initialState.xpos || dimensions->ypos != initialState.ypos ||
		dimensions->width != initialState.width || dimensions->height != initialState.height;

	if (bInitialFullscreenWindowState != bFinalFullscreenWindowState || 
		(bProgrammaticWindowReshape && !device->bManualWindowManagement))
		// before the actual device reset, adjust window position & style to account for final settings, and
		// cover the destination monitor before transition to windowed fullscreen or fullscreen
		rwinSetWindowProperties(device, rdrGetWindowHandle(device), dimensions->xpos, dimensions->ypos, &dimensions->width, &dimensions->height, &refreshRate, dimensions->fullscreen, 
			device->primary_monitor, dimensions->maximize, dimensions->windowed_fullscreen, false, false);
	device->resizeDeviceDirect(device, dimensions, RDRRESIZE_PRECOMMIT_END);

	// bManualWindowManagement is secret flag for D3D9 & 9Ex.
	bRemoveTopmost = device->bManualWindowManagement && !dimensions->fullscreen;
	bTransitionFullscreenToWFS = !dimensions->fullscreen && (dimensions->maximize || dimensions->windowed_fullscreen);
	if (bTransitionFullscreenToWFS || bRemoveTopmost)
	{
		// After the reset, desktop layout may have changed. Make sure the window ends 
		// there to match the updated dimensions struct.
		rwinSetWindowProperties(device, rdrGetWindowHandle(device), dimensions->xpos, dimensions->ypos, &dimensions->width, &dimensions->height, &refreshRate, dimensions->fullscreen, 
			device->primary_monitor, dimensions->maximize, dimensions->windowed_fullscreen, false, bRemoveTopmost);
		device->resizeDeviceDirect(device, dimensions, RDRRESIZE_PRECOMMIT_END);
	}

	TRACE_DEVICE(device, "Resize end\n");
#endif
}

void rwinSetPosAndSizeDirect(RdrDevice *device, DisplayParams *dimensions, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	int refreshRate;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();

	refreshRate = 0;
	if (dimensions->fullscreen)
	{
		refreshRate = device_infos[ dimensions->preferred_adapter ]->display_modes[ dimensions->preferred_fullscreen_mode ]->refresh_rate;
	}
	// DX11 TODO DJR with recent major revisions to the fullscreen support, check on all platforms
	// Fancier code that allows toggling between full-screen

	// Flags description:
	//  Not completely sure what's going on, but on XP, we get D3D debug warnings, and possibly crashes if we call
	//    reset any time in the middle when we might not know our correct width/height/fullscreenness
	//  On Vista, it would generate a crash if we called the first reset, but after fixing this it creates
	//    a D3D warning if we skip the intermediate resets for some reason (the same ones that cause a
	//    warning on XP if they're there), so these flags were set empirically, there's probably some better way.
	TRACE_DEVICE(device, "Resize start\n");
	device->resizeDeviceDirect(device, dimensions, RDRRESIZE_PRECOMMIT_START);
	rwinSetWindowProperties(device, rdrGetWindowHandle(device), dimensions->xpos, dimensions->ypos, &dimensions->width, &dimensions->height, &refreshRate, dimensions->fullscreen, 
		device->primary_monitor, dimensions->maximize, dimensions->windowed_fullscreen, false, false);
	device->resizeDeviceDirect(device, dimensions, RDRRESIZE_PRECOMMIT_END);
	TRACE_DEVICE(device, "Resize end\n");
#endif
}


void rwinShowDirect(RdrDevice *device, int *pnCmdShow, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	HWND hwnd = rdrGetWindowHandle(device);
	int nCmdShow = *pnCmdShow;
	if (hwnd)
	{
		ANALYSIS_ASSUME(hwnd);
		ShowWindow(hwnd, nCmdShow);
		if (*pnCmdShow == SW_SHOWDEFAULT || *pnCmdShow == SW_SHOWMAXIMIZED)
		{
#pragma warning(suppress:6309) // UpdateWindow() isn't supposed to accept NULL - what does this do?
#pragma warning(suppress:6387)
			UpdateWindow(NULL);
			UpdateWindow(hwnd);
			SetFocus(hwnd);
			SetForegroundWindow(hwnd);
		}
	}
#endif
}

void rwinShellExecuteDirect(RdrDevice* device, RdrShellExecuteCommands* pCommands, WTCmdPacket* packet)
{
#if !PLATFORM_CONSOLE
	if (pCommands->callback)
		pCommands->callback((int)(intptr_t)ShellExecute_UTF8(pCommands->hwnd, pCommands->lpOperation, pCommands->lpFile, pCommands->lpParameters, pCommands->lpDirectory, pCommands->nShowCmd), pCommands->hwnd, pCommands->lpOperation, pCommands->lpFile, pCommands->lpParameters, pCommands->lpDirectory, pCommands->nShowCmd);
	else
		ShellExecute_UTF8(pCommands->hwnd, pCommands->lpOperation, pCommands->lpFile, pCommands->lpParameters, pCommands->lpDirectory, pCommands->nShowCmd);

#endif
}

//////////////////////////////////////////////////////////////////////////
// Icon

void rwinSetIconDirect(RdrDevice *device, int *resource_id_ptr, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	int resource_id = *resource_id_ptr;
	HANDLE hIcon = LoadImage(rdrGetInstanceHandle(device), MAKEINTRESOURCE(resource_id), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
	HANDLE hIconSm = LoadImage(rdrGetInstanceHandle(device), MAKEINTRESOURCE(resource_id), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	HWND hwnd = rdrGetWindowHandle(device);
	if (hwnd)
	{
		ANALYSIS_ASSUME(hwnd);
		SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)hIcon);
		SetClassLongPtr(hwnd, GCLP_HICONSM, (LONG_PTR)hIconSm);
	}
#endif
}

// This function attempts to set focus to the Start Menu or Taskbar, and expects the active 
// desktop to have such a window with class Shell_TrayWnd, and an empty title. Otherwise, 
// the function fails.
bool rwinSetFocusAwayFromClient()
{
	HWND hwndTaskBar = FindWindow(L"Shell_TrayWnd", L"");
	if (hwndTaskBar)
		SetForegroundWindow(hwndTaskBar);
	return hwndTaskBar != NULL;
}
