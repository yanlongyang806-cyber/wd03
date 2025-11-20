#include "strings_opt.h"
#include "StringUtil.h"
#include "memlog.h"
#include "file.h"
#include "EventTimingLog.h"
#include "MemoryPool.h"
#include "WTCmdPacket.h"
#include "TimedCallback.h"
#include "ThreadManager.h"
#include "trivia.h"
#include "MemoryMonitor.h"
#include "MemReport.h"
#include "utils.h"
#include "LinearAllocator.h"
#include "timing_profiler_interface.h"
#include "sysutil.h"
#include "utf8.h"

#include "RdrSurface.h"
#include "RdrDeviceTrace.h"

#if !PLATFORM_CONSOLE
#include <mmsystem.h>
#include <winuser.h>
#endif

#include "rt_xdevice.h"
#include "rt_xsurface.h"
#include "rt_xtextures.h"
#include "rt_xcursor.h"
#include "rt_xshader.h"
#include "rt_xsprite.h"
#include "rt_xFMV.h"
#include "rt_xdrawmode.h"
#include "rt_xprimitive.h"
#include "../rt_winutil.h"
#include "RdrState.h"
#include "systemspecs.h"
#include "osdependent.h"
#include "winutil.h"
#include "sysutil.h"

//callback used for Xlive integration
#include "../XRenderLibCallbacks.h"

#include "StructDefines.h"
#include "nvapi_wrapper.h"

GfxPerfCounts rdrGfxPerfCounts_Last;
GfxPerfCounts rdrGfxPerfCounts_Current;

// TODO DJR disable query tracing after looking at Alex's query softlock hang
#define ENABLE_TRACE_QUERIES 1
#if ENABLE_TRACE_QUERIES
#define TRACE_QUERY_EX(device, bForce, format, ...)		\
	if (rdr_state.traceQueries || (bForce)) TRACE_DEVICE((device), format, __VA_ARGS__)
#define TRACE_QUERY(device, format, ...)		\
	if (rdr_state.traceQueries) TRACE_DEVICE((device), format, __VA_ARGS__)
#else
#define TRACE_QUERY_EX(device, bForce, format, ...)
#define TRACE_QUERY(device, format, ...)
#endif

// Note, during Present itself (in_present) is not considered part of the frame generation.
__forceinline bool rxbxDeviceGeneratingFrame(const RdrDeviceDX *xdevice)
{
	return xdevice && (xdevice->in_scene || xdevice->after_scene_before_present);
}

// Use this for debugging
#define DEBUG_GUARD_DEVICE_DURING_FRAME 1
#if DEBUG_GUARD_DEVICE_DURING_FRAME
#define ASSERT_NOT_INFRAME(xdevice) assertmsg(!rxbxDeviceGeneratingFrame(xdevice), "Sending/processing messages, or altering/resetting the device during frame generation.")
#else
#define ASSERT_NOT_INFRAME(xdevice)
#endif

static const char * getSizeModeString(LPARAM lParam)
{
	if (lParam == SIZE_MAXHIDE)
		return "MAXHIDE: Hidden by other window maximizing";
	else
	if (lParam == SIZE_MAXIMIZED)
		return "Maximized";
	else
	if (lParam == SIZE_MAXSHOW)
		return "MAXSHOW: Becoming visible by other window restoring";
	else
	if (lParam == SIZE_MINIMIZED)
		return "Minimized";
	else
	if (lParam == SIZE_RESTORED)
		return "Restored";
	return "Unknown sizing mode";
}

static const char * showWindowStringTable[SW_MAX + 2] =
{
	"SW_HIDE",
	"SW_SHOWNORMAL, SW_NORMAL",
	"SW_SHOWMINIMIZED",
	"SW_SHOWMAXIMIZED, SW_MAXIMIZE",
	"SW_SHOWNOACTIVATE",
	"SW_SHOW",
	"SW_MINIMIZE",
	"SW_SHOWMINNOACTIVE",
	"SW_SHOWNA",
	"SW_RESTORE",
	"SW_SHOWDEFAULT",
	"SW_FORCEMINIMIZE",
	"SW_MAX"
};

static const char * getShowWindowString(int nCmdShow)
{
	return nCmdShow >= SW_HIDE && nCmdShow <= SW_MAX ? 
		showWindowStringTable[nCmdShow] : "SW_ Unknown";
}

#define SWP_MAX 15
static const char * setWindowPosFlagStringTable[SWP_MAX] =
{
	"SWP_NOSIZE",
	"SWP_NOMOVE",
	"SWP_NOZORDER",
	"SWP_NOREDRAW",

	"SWP_NOACTIVATE",
	"SWP_FRAMECHANGED, SWP_DRAWFRAME",
	"SWP_SHOWWINDOW",
	"SWP_HIDEWINDOW",

	"SWP_NOCOPYBITS",
	"SWP_NOOWNERZORDER, SWP_NOREPOSITION",
	"SWP_NOSENDCHANGING",
	"SWP_UNUSED1",

	"SWP_UNUSED2",
	"SWP_DEFERERASE",
	"SWP_ASYNCWINDOWPOS"
};

static void getSetWindowPosFlagsStrings(int nSWPosFlags, const char ** outputStrings )
{
	int flag_num, flag_bit;
	for (flag_num = 0, flag_bit = 1; flag_num < ARRAY_SIZE(setWindowPosFlagStringTable); ++flag_num, flag_bit <<= 1)
	{
		outputStrings[flag_num] = "";
		if (nSWPosFlags & flag_bit)
			outputStrings[flag_num] = setWindowPosFlagStringTable[flag_num];
	}
}


#if _XBOX
#undef D3DGETDATA_FLUSH
#define D3DGETDATA_FLUSH 0
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("D3D", BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("D3DDevice", BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:ThreadedWindowFunc", BUDGET_Renderer););

static void rxbxCreateQueries(RdrDeviceDX *device);
static void rxbxDestroyQueries(RdrDeviceDX *device);
static void allocWindowMessageTimers(void);
void rxbxDeviceNotifyMainThreadSettingsChanged(RdrDeviceDX * device);

static void rxbxClipCursorToRect(RdrDeviceDX * xdevice, const RECT * clipRect)
{
	ClipCursor(clipRect);
}

static void rxbxUnClipCursor(RdrDeviceDX * xdevice)
{
	ClipCursor(NULL);
}

static void rxbxClipCursorToFullscreen(RdrDeviceDX * xdevice)
{
	MONITORINFOEX moninfo;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	multiMonGetMonitorInfo(device_infos[xdevice->device_info_index]->monitor_index, &moninfo);
	ClipCursor(&moninfo.rcMonitor);
}

void rxbxNeedGammaResetDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	device->current_gamma = -100;
#endif
}

void rxbxPreserveGammaRampDirect(RdrDeviceDX *device)
{
#if !PLATFORM_CONSOLE
	if (!device->hDCforGamma)
	{
		device->hDCforGamma = GetDC(rdrGetWindowHandle(&device->device_base));
		//ReleaseDC(rdrGetWindowHandle(&device->device_base), device->hDCforGamma);
	}
	if (device->hDCforGamma)
	{
		ANALYSIS_ASSUME(device->hDCforGamma != NULL);
		if (GetDeviceGammaRamp(device->hDCforGamma, device->preserved_ramp))
			device->gamma_ramp_has_been_preserved = 1;
	}
#endif
}

void rxbxRestoreGammaRampDirect(RdrDeviceDX *device)
{
#if !PLATFORM_CONSOLE
	if (device->hDCforGamma && device->gamma_ramp_has_been_preserved && device->everTouchedGamma)
		SetDeviceGammaRamp(device->hDCforGamma, device->preserved_ramp);
#endif
}

void rxbxSetGammaDirect(RdrDeviceDX *device, F32 *gamma_ptr, WTCmdPacket *packet) 
{
#if !PLATFORM_CONSOLE
	if (device->hDCforGamma)
	{
		F32 gamma = *gamma_ptr;
		if (gamma > 0.1 && device->current_gamma != gamma && (!nearSameF32Tol(gamma, 1.0, 0.01) || device->everTouchedGamma))
		{
			WORD ramp[256*3];
			int i;
			F32 adjustedGamma;
			F32 rampVal, rampIdx;//, scale;
			F32 scaledGamma = 0;
			static int lastTweak=0;

			adjustedGamma =  MINMAX( ( gamma ), 0.3, 3.0 ); 

			for (i = 0 ; i < 256 ; i++)      
			{	
				//Convert ramp idx to 0.0 to 1.0; 
				rampIdx = (i+1) / 256.0; 

				//Do crazy scaling to get something that looks nice
				//scale = adjustedGamma - 1.0;    
				//scale = scale * (1.0 - rampIdx*rampIdx); 
				//scaledGamma = scale + 1.0;     

				scaledGamma = adjustedGamma;  

				//Apply Gamma Scale   
				rampVal = pow( rampIdx, scaledGamma ); 

				//Scale to fit WORD
				rampVal = rampVal * 65535 + 0.5;
				rampVal = MINMAX( rampVal, 0, 65535 );

				//Apply to R, G, and B ramp
				ramp[i+0] = ramp[i+256] = ramp[i+512] = (WORD)rampVal;
			}

			device->current_gamma = gamma;

			// Tweak gamma to get around buggy ATI statemanager
			lastTweak^=1;
			ramp[1]+=lastTweak;

			device->everTouchedGamma = true;
			SetDeviceGammaRamp(device->hDCforGamma, ramp);
		}
	}
#endif
}

static void rxbxOperatingIntervalSet(RdrDeviceOperatingInterval *interval,
	U32 frameIndex, RdrDeviceLossState newState)
{
	interval->beginFrameIndex = frameIndex;
	interval->newDeviceState = newState;
}

static void rxbxOperatingHistoryUpdate(RdrDeviceOperatingHistory * operatingHistory, 
	U32 frameIndex, RdrDeviceLossState newState)
{
	int deviceOperatingHistIndex = operatingHistory->currentIndex;
	rxbxOperatingIntervalSet(&operatingHistory->history[operatingHistory->currentIndex], 
		frameIndex, newState);
	operatingHistory->currentIndex = (operatingHistory->currentIndex + 1) % RDR_DEVICE_OPER_STATE_HIST_COUNT;
}

__forceinline void rxbxSetDeviceLostDbg(RdrDeviceDX * device, RdrDeviceLossState isLost, const char * strCallerFn, int line)
{
	TRACE_DEVICE(&device->device_base, "DeviceLost change %d (%s) -> %d (%s); %s:%d\n", device->isLost, 
		device->isLost == RDR_OPERATING ? "Opr" : "Lost", isLost, isLost == RDR_OPERATING ? "Opr" : "Lost", strCallerFn, line);
	device->isLost = isLost;
	rxbxOperatingHistoryUpdate(&device->operatingHistory, device->device_base.frame_count, isLost);
	if (!device->notify_settings_frame_count)
		// notify settings for next 3 frames on device loss state change
		device->notify_settings_frame_count = 3;
	rxbxDeviceNotifyMainThreadSettingsChanged(device);
#if !PLATFORM_CONSOLE
	triviaPrintf("DeviceLost", "%d", isLost);
#endif
}
#define rxbxSetDeviceLost(device, isLost) rxbxSetDeviceLostDbg((device), (isLost), __FUNCTION__, __LINE__)

#if !PLATFORM_CONSOLE
static wchar_t windowNameW[100] = L"Cryptic";
static char windowName[100] = "Cryptic";

static StashTable hwnd_hash = 0;
static HWND the_hwnd = 0;
static RdrDeviceDX *the_device = 0;

static RdrDeviceDX *getDeviceForHwnd(HWND hwnd)
{
	RdrDeviceDX *device = 0;
	if (the_hwnd && the_hwnd == hwnd)
	{
		return the_device;
	}
	else if (hwnd_hash && stashAddressFindPointer(hwnd_hash, hwnd, &device))
	{
		return device;
	}
	return 0;
}

static void hashDeviceHwnd(RdrDeviceDX *device)
{
	if (!hwnd_hash && !the_hwnd)
	{
		the_hwnd = device->hWindow;
		the_device = device;
	}
	else
	{
		if (!hwnd_hash)
			hwnd_hash = stashTableCreateAddress(64);
		stashAddressAddPointer(hwnd_hash, device->hWindow, device, true);
		if (the_hwnd)
		{
			stashAddressAddPointer(hwnd_hash, the_hwnd, the_device, true);
			the_hwnd = 0;
			the_device = 0;
		}
	}
}

static void removeHwnd(HWND hwnd)
{
	if (the_hwnd)
	{
		assert(the_hwnd == hwnd);
		the_hwnd = 0;
		the_device = 0;
	}
	else if (hwnd_hash)
	{
		stashAddressRemovePointer(hwnd_hash, hwnd, 0);
	}
}



void rxbxDeviceNotifyMainThreadSettingsChanged(RdrDeviceDX * device)
{
	// send complete new display settings back to main thread
	DisplayParams displayUpdate = device->display_thread;
	displayUpdate.deviceOperating = device->isLost == RDR_OPERATING;
	wtQueueMsg(device->device_base.worker_thread, RDRMSG_DISPLAYPARAMS, &displayUpdate, sizeof(displayUpdate));
}

void rxbxDeviceGetDisplaySettingsFromDevice(RdrDeviceDX * device)
{
	// under DX11 test if we are now fullscreen
	if (device->d3d11_device)
	{
		DXGI_SWAP_CHAIN_DESC swap_desc = { 0 };
		IDXGISwapChain_GetDesc(device->d3d11_swapchain, &swap_desc);
		TRACE_DEVICE(&device->device_base, "Engine FS %s -> Dev FS %s\n", device->display_thread.fullscreen ? "FS" : "Win", swap_desc.Windowed ? "Win" : "FS");
		if (device->display_thread.fullscreen ^ (swap_desc.Windowed ? 0 : 1))
		{
			TRACE_DEVICE(&device->device_base, "Notifying main thread of settings change\n");
			device->display_thread.fullscreen = swap_desc.Windowed ? 0 : 1;
			if (!swap_desc.Windowed)
				rxbxClipCursorToFullscreen(device);
		}
	}
	rxbxDeviceNotifyMainThreadSettingsChanged(device);
}

//////////////////////////////////////////////////////////////////////////
// window

static void rxbxDoWindowedFullscreenDirect(HWND hwnd, RdrDeviceDX *device)
{
	// Switch to windowed_fullscreen
	HMONITOR hmon = MonitorFromWindow(device->hWindow, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEX moninfo;
	moninfo.cbSize = sizeof(moninfo);
	GetMonitorInfo(hmon, (LPMONITORINFO)&moninfo);

	device->display_thread.windowed_fullscreen = 1;
	TRACE_WINDOW("rxbxDoWindowedFSD SetWindowLongPtr GWL_STYLE WS_POPUP|WS_VISIBLE|WS_MINIMIZEBOX\n");
	SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP|WS_VISIBLE|WS_MINIMIZEBOX);
	TRACE_WINDOW("SetWindowPos ( %d, %d ) %d x %d SWP_NOZORDER|SWP_FRAMECHANGED\n", 
		moninfo.rcMonitor.left, moninfo.rcMonitor.top, moninfo.rcMonitor.right - moninfo.rcMonitor.left, moninfo.rcMonitor.bottom - moninfo.rcMonitor.top);
	SetWindowPos(hwnd, NULL,
		moninfo.rcMonitor.left, moninfo.rcMonitor.top, moninfo.rcMonitor.right - moninfo.rcMonitor.left, moninfo.rcMonitor.bottom - moninfo.rcMonitor.top,
		SWP_NOZORDER|SWP_FRAMECHANGED);
	TRACE_WINDOW("UpdateWindow hwnd\n");
	UpdateWindow(hwnd);
	TRACE_WINDOW("ShowWindow SW_SHOW\n");
	ShowWindow(hwnd, SW_SHOW);
}


void rxbxShowDirect(RdrDeviceDX *device, int *pnCmdShow, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	HWND hwnd = rdrGetWindowHandle(&device->device_base);
	int nCmdShow = *pnCmdShow;
	if (nCmdShow == SW_RESTORE)
	{
		if (device->display_thread.windowed_fullscreen)
		{
			// switch to regular windowed
			int restored_size[2] = {
				device->screen_width_restored?device->screen_width_restored:(device->primary_surface.width_thread*3/4),
				device->screen_height_restored?device->screen_height_restored:(device->primary_surface.height_thread*3/4)
			};
			device->display_thread.windowed_fullscreen = 0;
			TRACE_WINDOW("rxbxShowDirect SetWindowLongPtr SW_RESTORE\n");
			SetWindowLongPtr(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|WS_VISIBLE);
			TRACE_WINDOW("SetWindowPos SW_RESTORE\n");
			SetWindowPos(hwnd, NULL, device->screen_x_restored, device->screen_y_restored, restored_size[0], restored_size[1], SWP_NOZORDER);
			TRACE_WINDOW("UpdateWindow SW_RESTORE\n");
			UpdateWindow(hwnd);
			TRACE_WINDOW("ShowWindow SW_SHOWNORMAL\n");
			ShowWindow(hwnd, SW_SHOWNORMAL);
		} else if (device->display_thread.maximize) {
			TRACE_WINDOW("rxbxShowDirect ShowWindow SW_RESTORE\n");
			// restore normally
			ShowWindow(hwnd, SW_RESTORE);
		} else {
			// regular window, switch to maximized or windowed_fullscreen
			if (!rdr_state.disable_windowed_fullscreen && device->allow_windowed_fullscreen)
				rxbxDoWindowedFullscreenDirect(hwnd, device);
			else
			{
				TRACE_WINDOW("rxbxShowDirect ShowWindow SW_MAXIMIZE\n");
				ShowWindow(hwnd, SW_MAXIMIZE);
			}
		}
	} else {
		TRACE_WINDOW("ShowWindow %x %s\n", nCmdShow, getShowWindowString(nCmdShow));
		ShowWindow(hwnd, nCmdShow);
	}
	if (*pnCmdShow == SW_SHOWDEFAULT || *pnCmdShow == SW_SHOWMAXIMIZED)
	{
		TRACE_WINDOW("UpdateWindow NULL\n");

#pragma warning(suppress:6309) // Argument '1' is null: this does not adhere to function specification of 'UpdateWindow'
		UpdateWindow(NULL); // what does passing NULL to UpdateWindow do??
		TRACE_WINDOW("UpdateWindow hwnd\n");
		UpdateWindow(hwnd);
		TRACE_WINDOW("SetFocus hwnd\n");
		SetFocus(hwnd);
		TRACE_WINDOW("SetForegroundWindow hwnd\n");
		SetForegroundWindow(hwnd);
	}
#endif
}



static LRESULT WINAPI DefWindowProc_timed( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	LRESULT ret;
	PERFINFO_AUTO_START("DefWindowProc", 1);
	ret = !rdr_state.unicodeRendererWindow ?
		DefWindowProcA(hWnd, uMsg, wParam, lParam) :
		DefWindowProcW(hWnd, uMsg, wParam, lParam);
	PERFINFO_AUTO_STOP();
	return ret;
}

static int inittimers = 0;
static int windowMessageTimerCount;
#define TIMER_COUNT 1000

static struct {
	PerfInfoStaticData*		piStatic;
	char*					name;
} timers[ TIMER_COUNT ];

static void startWindowMessageTimer(UINT msg)
{
	if(msg < TIMER_COUNT)
	{
		PERFINFO_AUTO_START_STATIC(timers[msg].name, &timers[msg].piStatic, 1);
		
		windowMessageTimerCount++;
	}
}

static void stopWindowMessageTimer()
{
	if(windowMessageTimerCount)
	{
		PERFINFO_AUTO_STOP();
		
		windowMessageTimerCount--;
	}
}


static void rxbxHandleDeviceEnterSizeMoveMsg(RdrDeviceDX * device, HWND hWnd, LPARAM lParam, WPARAM wParam, bool bWindowThread)
{
	// Halt frame movement while the app is sizing or moving
	device->interactive_resizing = true;
	device->interactive_resizing_got_size = false;
}

static void rxbxHandleDeviceSizeMsg(RdrDeviceDX * device, HWND hWnd, LPARAM lParam, WPARAM wParam, bool bWindowThread)
{
	if (device->interactive_resizing)
	{
		device->last_size_lParam = lParam;
		device->last_size_wParam = wParam;
		device->interactive_resizing_got_size = true;
		TRACE_WINDOW("WM_SIZE: Interactive Resize %d x %d (wParam %d %s) (lParam %d)\n", LOWORD(lParam), HIWORD(lParam), wParam, getSizeModeString(wParam), lParam);
	}
	else
	{
		int sizeChanged = 0;
		int newClientWidth;
		int newClientHeight;
		int size[2];
		DisplayParams display_settings = device->display_thread;

		RECT clientRect, wndRect;
			
		// Store the new client height and width in game_state.
		//	Don't store them if they are 0.  May cause divide by 0 errors.
		newClientWidth	= LOWORD(lParam);
		newClientHeight = HIWORD(lParam);

		// DJR validating WM_SIZE vs GetClientRect (checking for possible invalid window style)
		GetClientRect(hWnd, &clientRect);
		GetWindowRect(hWnd, &wndRect);
		TRACE_WINDOW("WM_SIZE GetClientRect: %d x %d GWR: %d x %d\n", clientRect.right, clientRect.bottom, wndRect.right, wndRect.bottom);
		TRACE_WINDOW("WM_SIZE: %d x %d (wParam %d %s) (lParam %d)\n", newClientWidth, newClientHeight, wParam, getSizeModeString(wParam), lParam);
		ClientToScreen(hWnd, (POINT*)&clientRect.left);
		ClientToScreen(hWnd, (POINT*)&clientRect.right);
		TRACE_WINDOW("WM_SIZE Screen Rect of CR (%d, %d), (%d, %d)\n", clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
			
		if (!device->display_thread.fullscreen)
		{
			if (newClientWidth && device->device_width != newClientWidth )
			{
				sizeChanged = 1;
			}
			if (newClientHeight && device->device_height != newClientHeight )
			{
				sizeChanged = 1;
			}
		}
			
		switch(wParam)
		{
			xcase SIZE_MAXIMIZED:
				display_settings.maximize = 1;
				display_settings.minimize = 0;
			xcase SIZE_MINIMIZED:
				display_settings.maximize = 0;
				display_settings.minimize = 1;
			xcase SIZE_RESTORED:
				display_settings.maximize = 0;
				display_settings.minimize = 0;
		}

		size[0] = newClientWidth;
		size[1] = newClientHeight;
		wtQueueMsg(device->device_base.worker_thread, RDRMSG_SIZE, size, sizeof(int)*2);

		if ( newClientWidth && newClientHeight && sizeChanged )
		{
			TRACE_WINDOW("WM_SIZE resize device\n");
			display_settings.width = newClientWidth;
			display_settings.height = newClientHeight;
			rxbxResizeDeviceDirect( &device->device_base, &display_settings, RDRRESIZE_DEFAULT);
		}
		else
		{
			device->display_thread = display_settings;
			rxbxDeviceNotifyMainThreadSettingsChanged(device);
		}
	}
}

static void rxbxHandleDeviceExitSizeMoveMsg(RdrDeviceDX * device, HWND hWnd, LPARAM lParam, WPARAM wParam, bool bWindowThread)
{
	device->interactive_resizing = false;
	if (!device->interactive_resizing_got_size)
		return;
	rxbxHandleDeviceSizeMsg(device, hWnd, device->last_size_lParam, device->last_size_wParam, bWindowThread);
}

//callback used for Xlive integration
MainWndProc_Callback pMainWndProc_Callback = NULL;

void Set_MainWndProc_Callback( MainWndProc_Callback pCallbackRtn )
{
	pMainWndProc_Callback = pCallbackRtn;
}

static volatile long wnd_proc_recursion_count;
static int early_out_wnd_proc;

//////////////////////////////////////////////////////////////////////////
// Unused if the window thread is enabled.
// NOTE: Any changes here should be mirrored in ThreadWndProc.
//////////////////////////////////////////////////////////////////////////
static LRESULT WINAPI MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT lRet = 1;
	RdrDeviceDX *device;
	WinMsg wmsg={0};

	if (early_out_wnd_proc)
	{
		return !rdr_state.unicodeRendererWindow ?
			DefWindowProcA(hWnd, uMsg, wParam, lParam) :
			DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}

	PERFINFO_AUTO_START_FUNC();

	device = getDeviceForHwnd(hWnd);

#if TRACE_DEVICE_LEVEL
	// log messages during create (when there is no device yet associated with the HWND)
	// or during frame generation - the interval from BeginScene to Present, including
	// during Present (which may be perfectly normal, but of interest)
	if (!device || rxbxDeviceGeneratingFrame(device) || device->in_present)
	{
		char *pTempName = NULL;
		if (uMsg < 0xc000 || uMsg > 0xffff || !GetClipboardFormatName_UTF8(uMsg, &pTempName))
			estrCopy2(&pTempName, "");
		if (uMsg == WM_CREATE)
		{
			const CREATESTRUCT *createStr = (const CREATESTRUCT *)lParam;
			TRACE_WINDOW("RT WM_CREATE: (%d, %d) (%d x %d) Style 0x%x ExStyle 0x%x", createStr->x, createStr->y,
				createStr->cx, createStr->cy, createStr->style, createStr->dwExStyle);
		}
		else
		{
			TRACE_WINDOW("RT Wnd %s %s %s %p 0x%04x '%s' %x %x\n", device && device->after_scene_before_present ? "ASBP" : "N", 
				device && device->in_scene ? "IS" : "OS", device && device->in_present ? "IP" : "OP", 
				hWnd, uMsg, pTempName, wParam, lParam);
			// The Errorf won't fire for messages during Present. This is because we have some expected messages here,
			// like WM_NCPAINT for FRAPS, and WM_WINDOWPOSCHANGING/WM_WINDOWPOSCHANGED after DX11 device 
			// SetFullscreenState/ResizeTarget/ResizeBuffers calls for toggling fullscreen.
			if (rxbxDeviceGeneratingFrame(device))
			{
				static int maxMessageWarnings = 10;
				if (maxMessageWarnings > 0)
				{
					--maxMessageWarnings;
					ErrorDeferredf("Processing window message during frame: %s %s %s 0x%04x '%s' %x %x\n", device && device->after_scene_before_present ? "ASBP" : "N", 
						device && device->in_scene ? "IS" : "OS", device && device->in_present ? "IP" : "OP", 
						uMsg, pTempName, wParam, lParam);
				}
			}
		}

		estrDestroy(&pTempName);
	}
#endif

	InterlockedIncrement(&wnd_proc_recursion_count);

	assertmsg(wnd_proc_recursion_count < 1000, "Infinite recursion in MainWndProc");

	if ( device && device->debug_in_reset )
	{
		TRACE_WINDOW("MainWndProc received a message during D3D::Reset: 0x%x 0x%x 0x%x.\n", uMsg, wParam, lParam);
	}


	if (isCrashed() || !device)
	{
		lRet = !rdr_state.unicodeRendererWindow ?
			DefWindowProcA(hWnd, uMsg, wParam, lParam) :
			DefWindowProcW(hWnd, uMsg, wParam, lParam);
		InterlockedDecrement(&wnd_proc_recursion_count);
		PERFINFO_AUTO_STOP();
		return lRet;
	}


	//callback used for Xlive integration
	if ( pMainWndProc_Callback != NULL )
	{
		LRESULT tmpRet = 0;
		if ( (lRet = pMainWndProc_Callback( device, hWnd, uMsg, wParam, lParam )) != IGNORE_CALLBACK_RETURN )
		{
			InterlockedDecrement(&wnd_proc_recursion_count);
			PERFINFO_AUTO_STOP();
			return tmpRet;
		}
	}
	
	startWindowMessageTimer(uMsg);
	
	switch (uMsg) 
	{
		xcase WM_SYSCOMMAND:
			switch (wParam)
			{
				xcase SC_SCREENSAVE:
				acase SC_MONITORPOWER:
					lRet = 0;
				xcase SC_MAXIMIZE:
					if (!rdr_state.disable_windowed_fullscreen && device->allow_windowed_fullscreen)
					{
						rxbxDoWindowedFullscreenDirect(hWnd, device);
						lRet = 0; // Don't let OS maximize our window
					} else {
						lRet = DefWindowProc_timed (hWnd, uMsg, wParam, lParam);
					}

				xdefault:
					lRet = DefWindowProc_timed (hWnd, uMsg, wParam, lParam);
			}

		xcase WM_NCLBUTTONDBLCLK:
			if (!rdr_state.disable_windowed_fullscreen && device->allow_windowed_fullscreen)
			{
#if !PLATFORM_CONSOLE
				rxbxDoWindowedFullscreenDirect(hWnd, device);
// 				int width=device->primary_surface.width_thread;
// 				int height=device->primary_surface.height_thread;
// 				device->windowed_fullscreen = 1;
// 
// 				rwinSetWindowProperties(&device->device_base, hWnd,
// 					device->screen_x_pos, device->screen_y_pos,
// 					&width, &height, &device->refresh_rate, 0, device->device_base.primary_monitor,
// 					0, 1, 0);
#endif
				lRet = 0; // Don't let the OS maximize the window
			} else {
				lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			}

		xcase WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				device->can_set_cursor = 1;
				device->last_set_cursor = GetCursor();
				rxbxApplyCursorDirect(device);
			}
			else
			{
				device->can_set_cursor = 0;
				lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			}

        xcase WM_WINDOWPOSCHANGING:
        {
#if !_XBOX
            if( device->allow_any_size )
            {
                lRet = 0;
            }
            else
            {
                lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
            }
#endif
        }

		xcase WM_DISPLAYCHANGE:
			TRACE_WINDOW("WM_DISPLAYCHANGE %d x %d, %d bpp\n", LOWORD(lParam), HIWORD(lParam), wParam);
			device->notify_settings_frame_count += 2;
			break;

		xcase WM_GETMINMAXINFO:
		{
			MINMAXINFO *windowLimits = (MINMAXINFO*)lParam;
			windowLimits->ptMinTrackSize.x = 256;
			windowLimits->ptMinTrackSize.y = 256;
			lRet = 0;
		}

		xcase WM_WINDOWPOSCHANGED:
		{
			// Detect when the game window is being maximized/minimized/restored.
			WINDOWPOS* pos = (WINDOWPOS*)lParam;
			const char * swpFlagStrs[ SWP_MAX ];
			
			// DJR validating WM_SIZE vs GetClientRect (checking for possible invalid window style)
			RECT clientRect, wndRect;
			GetClientRect(hWnd, &clientRect);
			GetWindowRect(hWnd, &wndRect);
			TRACE_WINDOW("WM_WINDOWPOSCHANGED GetClientRect: %d x %d GWR: %d x %d\n", clientRect.right, clientRect.bottom, wndRect.right - wndRect.left, wndRect.bottom - wndRect.top);
			getSetWindowPosFlagsStrings(pos->flags, swpFlagStrs);
			TRACE_WINDOW("WM_WINDOWPOSCHANGED: (%d, %d) %d x %d 0x%x %s|%s|%s|%s| %s|%s|%s|%s| %s|%s|%s|%s| %s|%s|%s\n",
				pos->x, pos->y, pos->cx, pos->cy, pos->flags,
				swpFlagStrs[ 0], swpFlagStrs[ 1], swpFlagStrs[ 2], swpFlagStrs[ 3], 
				swpFlagStrs[ 4], swpFlagStrs[ 5], swpFlagStrs[ 6], swpFlagStrs[ 7], 
				swpFlagStrs[ 8], swpFlagStrs[ 9], swpFlagStrs[10], swpFlagStrs[11], 
				swpFlagStrs[12], swpFlagStrs[13], swpFlagStrs[14]
			);
			// Call DefWindowProc so that WM_SIZE gets sent.
			DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			
			device->display_thread.xpos = pos->x;
			device->display_thread.ypos = pos->y;

			MIN1(device->notify_settings_frame_count,3);

			if (!device->display_thread.windowed_fullscreen && !device->display_thread.fullscreen && !device->display_thread.maximize && !device->display_thread.minimize)
			{
				TRACE_WINDOW("WM_WINDOWPOSCHANGED updating restored screen pos\n");
				device->screen_x_restored = pos->x;
				device->screen_y_restored = pos->y;
			}
			if (!device->display_thread.windowed_fullscreen && !device->display_thread.fullscreen && !device->display_thread.maximize && !device->display_thread.minimize)
			{
				TRACE_WINDOW("WM_WINDOWPOSCHANGED updating restored screen size\n");
				device->screen_width_restored = pos->cx;
				device->screen_height_restored = pos->cy;
			}
			if (!device->display_thread.fullscreen && !device->display_thread.maximize && !device->display_thread.minimize)
			{
				device->screen_x_pos = pos->x;
				device->screen_y_pos = pos->y;
			}

			if (device->screen_x_pos < -4096) 
				device->screen_x_pos = 0;
			if (device->screen_y_pos < -4096) 
				device->screen_y_pos = 0;
			
			lRet = 0;
		}
		
		xcase WM_ENTERSIZEMOVE:
			rxbxHandleDeviceEnterSizeMoveMsg(device, hWnd, lParam, wParam, false);

        xcase WM_EXITSIZEMOVE:
			rxbxHandleDeviceExitSizeMoveMsg(device, hWnd, lParam, wParam, false);

		xcase WM_SIZE:
			rxbxHandleDeviceSizeMsg(device, hWnd, lParam, wParam, false);

		// Detect when the game window doesn't have focus.
		// Don't draw stuff on the "screen" if the game is inactive.
		xcase WM_ACTIVATEAPP:
			TRACE_WINDOW(__FUNCTION__":WM_ACTIVATEAPP(%d %x)", wParam, lParam);
			switch(wParam)
			{
				xcase 0:
					if (device->after_scene_before_present || device->in_scene)
						TRACE_WINDOW("Lost app focus during scene!\n");

					// The app is being deactivated.
					// Don't minimize/restore DX11 devices because they retain fullscreen 
					// ownership even when app does not have focus.
					if (!device->d3d11_device && device->display_thread.fullscreen && !device->inactive_display && rdr_state.bD3D9ClientManagesWindow)
					{
						TRACE_WINDOW("ShowWindow SW_MINIMIZE\n");
						// Minimize
						ShowWindow(hWnd, SW_MINIMIZE);
					}
					device->inactive_display = 1;
					if (!rdr_state.bProcessMessagesOnlyBetweenFrames)
					{
						if (device->device_base.is_locked_thread && device->d3d_device)
							rxbxIsInactiveDirect(device, NULL, NULL);
					}
					else
					if (device->d3d_device && device->display_thread.fullscreen)
					{
						// avoid submitting another frame while the device is lost
						rxbxSetDeviceLost(device, RDR_LOST_FOCUS);
					}

				xcase 1:
					// Don't minimize/restore DX11 devices because they retain fullscreen 
					// ownership even when app does not have focus.
					if (!device->d3d11_device && device->display_thread.fullscreen && device->inactive_display)
					{
						RECT clientRect;
						GetClientRect(hWnd, &clientRect);
						ClientToScreen(hWnd, (POINT*)&clientRect.left);
						ClientToScreen(hWnd, (POINT*)&clientRect.right);
						TRACE_WINDOW("WM_ACTIVATEAPP Screen Rect of CR (%d, %d), (%d, %d)\n", clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);

						if (rdr_state.bD3D9ClientManagesWindow)
						{
							TRACE_WINDOW("ShowWindow SW_RESTORE\n");
							// Restore
							ShowWindow(hWnd, SW_RESTORE);
						}
					}
					// The app is being activated. Note this may happed during device creation
					// and we don't want to run the reactivation in that case
					device->inactive_display = 0;
					device->inactive_app = 0;

					// In DX11, becoming inactive does not lose fullscreen focus, so we can restore the cursor clip state here.
					// In DX9, reactivating a lost fullscreen device will trigger the Reset codepath, which restore the cursor clip state.
					if (device->d3d11_device && device->display_thread.fullscreen)
						rxbxClipCursorToFullscreen(device);
					//if ( device->d3d_device && rxbxIsInactiveDirect( &device->device_base ) )
					//	rxbxReactivateDirect( &device->device_base );
			}
			
			// force gamma set
			if (wParam)
				rxbxNeedGammaResetDirect(device, NULL, NULL);
			else
				rxbxRestoreGammaRampDirect(device);
			
			lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);

		xcase WM_ACTIVATE:
			{
				U32 activateCode = LOWORD(wParam);
				TRACE_WINDOW("WM_ACTIVATE %u %u %s\n", wParam, lParam, activateCode == WA_INACTIVE ? "WA_INACTIVE" : (activateCode == WA_CLICKACTIVE ? "WA_CLICKACTIVE" : "WA_ACTIVE"));
				device->inactive_display = wParam == WA_INACTIVE;
				lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			}

		xcase WM_SETFOCUS:
			TRACE_WINDOW("WM_SETFOCUS 0x%x\n", wParam);
			rxbxNeedGammaResetDirect(device, NULL, NULL);
			lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			
		xcase WM_CLOSE:
			// do not let DefWindowProc destroy the window, we want to handle this ourselves
			lRet = 0;

		xcase WM_DESTROY:
			rxbxRestoreGammaRampDirect(device);

		xcase WM_QUIT:
			// do not do DefWindowProc, just pass the message back to the handler
			lRet = 0;

		xcase WM_KEYDOWN:
		acase WM_SYSKEYDOWN:{
			if(globMovementLogIsEnabled){
				U32 msMsgTime = GetMessageTime();
				U32 msCurTime = timeGetTime();
			
				globMovementLog("[input] gfx WM_%sKEYDOWN[%u, %u] %dms late, %ums, real %ums",
								uMsg == WM_SYSKEYDOWN ? "SYS" : "",
								wParam,
								lParam,
								msCurTime - msMsgTime,
								msMsgTime,
								msCurTime);
			}

			lRet = 0;
		}

		xcase WM_KEYUP:
		acase WM_SYSKEYUP:{
			if(globMovementLogIsEnabled){
				U32 msMsgTime = GetMessageTime();
				U32 msCurTime = timeGetTime();
			
				globMovementLog("[input] gfx WM_%sKEYUP[%u, %u] %dms late, %ums, real %ums",
								uMsg == WM_SYSKEYUP ? "SYS" : "",
								wParam,
								lParam,
								msCurTime - msMsgTime,
								msMsgTime,
								msCurTime);
			}
			
			lRet = 0;
		}

		xcase WM_CHAR:
		case WM_DEADCHAR:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
		case WM_KEYLAST:
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			lRet = 0;

		xcase WM_DEVICECHANGE:
			lRet = 0;

		xdefault:
			lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
    }

	wmsg.timeStamp = GetMessageTime();
	wmsg.uMsg = uMsg;
	wmsg.wParam = wParam;
	wmsg.lParam = lParam;
	//printf("Got message: %d 0x%04x %d %d\n", wmsg.timeStamp, wmsg.uMsg, wmsg.wParam, wmsg.lParam);
	wtQueueMsg(device->device_base.worker_thread, RDRMSG_WINMSG, &wmsg, sizeof(wmsg));

	stopWindowMessageTimer();
	InterlockedDecrement(&wnd_proc_recursion_count);
	PERFINFO_AUTO_STOP();
   return lRet;
}

static LRESULT WINAPI MainWndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// This makes it possible to compile-and-continue MainWndProc.

	return MainWndProc(hWnd, uMsg, wParam, lParam);
}

//////////////////////////////////////////////////////////////////////////

static int registerWindowClass(RdrDeviceDX *device, int icon, WNDPROC wndproc, bool bUnicodeWindow)
{
	static int classnum=0;

	if (device->windowClass.cbSize || device->windowClassW.cbSize)
		return 1;

	++classnum;
	wsprintfW(device->windowClassNameW, L"CrypticWindowClassDX%d", classnum);
	sprintf(device->windowClassName, "CrypticWindowClassDX%d", classnum);

	if (!bUnicodeWindow)
	{
		device->windowClass.cbSize			= sizeof(device->windowClass);
		device->windowClass.style			= CS_OWNDC | CS_DBLCLKS;
		device->windowClass.lpfnWndProc		= wndproc ? wndproc : MainWndProcCallback;
		device->windowClass.cbClsExtra		= 0;
		device->windowClass.cbWndExtra		= 0;
		device->windowClass.hInstance		= GetModuleHandleA( NULL );
		device->windowClass.hCursor			= 0;
		device->windowClass.hbrBackground	= CreateSolidBrush(RGB(0,0,0));
		device->windowClass.lpszMenuName	= windowName;
		device->windowClass.lpszClassName	= device->windowClassName;
		if (icon)
		{
			device->windowClass.hIcon			= LoadIconA(device->windowClass.hInstance, MAKEINTRESOURCEA(icon));
			device->windowClass.hIconSm			= (HICON)LoadImageA(device->windowClass.hInstance, MAKEINTRESOURCEA(icon), IMAGE_ICON, 16, 16, 0);
		}

		TRACE_WINDOW("RegisterClassExA( Size %u, style %u, WndProc 0x%p, ClassExtra %u, WndExtra %u, "
			"HINST 0x%p, Cursor 0x%p, Brush 0x%p, Menu %s, Class %s, Icon 0x%p IconSm 0x%p)", 
			device->windowClass.cbSize,
			device->windowClass.style,
			device->windowClass.lpfnWndProc,
			device->windowClass.cbClsExtra,
			device->windowClass.cbWndExtra,
			device->windowClass.hInstance,
			device->windowClass.hCursor,
			device->windowClass.hbrBackground,
			windowName,
			device->windowClassName,
			device->windowClass.hIcon, 
			device->windowClass.hIconSm);
		if (!RegisterClassExA(&device->windowClass))
		{
			// DX11 TODO translation support
			TRACE_WINDOW("RegisterClassExA failed; GetLastError() = %d", GetLastError());
			MessageBoxA(0, "Couldn't Register Window Class", device->windowClassName, MB_ICONERROR);
			ZeroStruct(&device->windowClass);
			return 0;
		}
	}
	else
	{
		device->windowClassW.cbSize			= sizeof(device->windowClassW);
		device->windowClassW.style			= CS_OWNDC | CS_DBLCLKS;
		device->windowClassW.lpfnWndProc	= wndproc ? wndproc : MainWndProcCallback;
		device->windowClassW.cbClsExtra		= 0;
		device->windowClassW.cbWndExtra		= 0;
		device->windowClassW.hInstance		= GetModuleHandleW( NULL );
		device->windowClassW.hCursor		= 0;
		device->windowClassW.hbrBackground	= CreateSolidBrush(RGB(0,0,0));
		device->windowClassW.lpszMenuName	= windowNameW;
		device->windowClassW.lpszClassName	= device->windowClassNameW;
		if (icon)
		{
			device->windowClassW.hIcon			= LoadIconW(device->windowClassW.hInstance, MAKEINTRESOURCEW(icon));
			device->windowClassW.hIconSm		= (HICON)LoadImageW(device->windowClassW.hInstance, MAKEINTRESOURCEW(icon), IMAGE_ICON, 16, 16, 0);
		}


		TRACE_WINDOW("RegisterClassExW( Size %u, style %u, WndProc 0x%p, ClassExtra %u, WndExtra %u, "
			"HINST 0x%p, Cursor 0x%p, Brush 0x%p, Menu %s, Class %s, Icon 0x%p IconSm 0x%p)", 
			device->windowClassW.cbSize,
			device->windowClassW.style,
			device->windowClassW.lpfnWndProc,
			device->windowClassW.cbClsExtra,
			device->windowClassW.cbWndExtra,
			device->windowClassW.hInstance,
			device->windowClassW.hCursor,
			device->windowClassW.hbrBackground,
			windowName,
			device->windowClassName,
			device->windowClassW.hIcon, 
			device->windowClassW.hIconSm);
		if (!RegisterClassExW(&device->windowClassW))
		{
			// DX11 TODO translation support
			TRACE_WINDOW("RegisterClassExW failed; GetLastError() = %d", GetLastError());
			MessageBoxW(0, L"Couldn't Register Window Class", device->windowClassNameW, MB_ICONERROR);
			ZeroStruct(&device->windowClassW);
			return 0;
		}
	}

	return 1;
}

static void rxbxResizeDirect(RdrDeviceDX *device,int width,int height,int refreshRate,int x0,int y0,int fullscreen,int maximize, int windowed_fullscreen, bool hide)
{
	rxbxProcessWindowsMessagesDirect(device, NULL, NULL);

	// JE: As part of rearranging things for DX11, this was moved to before the call to rwinSetWindowProperties(), and changing any of this makes me frightened
	device->display_thread.fullscreen = fullscreen;

	rwinSetWindowProperties(&device->device_base, device->hWindow, x0, y0, &width, &height, &refreshRate, fullscreen, device->device_base.primary_monitor, maximize, windowed_fullscreen, hide, false);
}

// Creates the window for rendering.  Basically just a copy of the existing window
//  creation functions, but provides a different WindowProc.
static void CreateRenderWindow(RdrDeviceDX *device, WindowCreateParams *create_params)
{
#if !_XBOX
	if (!device->hWindow)
	{
		RECT rcWindowPos;
		int windowWidth, windowHeight;
		HWND devWindow;
		DisplayParams *params = &create_params->display;
		wchar_t windowTitleW[100];

		int windowFlags = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

		if (!params->fullscreen && params->windowed_fullscreen)
			windowFlags |= WS_POPUP | WS_MINIMIZEBOX;
		else if (!params->fullscreen)
			windowFlags |= WS_OVERLAPPEDWINDOW;
		else
			windowFlags |= WS_POPUP;

		if (!params->hide)
			windowFlags |= WS_VISIBLE;

		rcWindowPos.left = 0;
		rcWindowPos.top = 0;
		rcWindowPos.right = params->width;
		rcWindowPos.bottom = params->height;
		AdjustWindowRect(&rcWindowPos, windowFlags, FALSE);

		windowWidth = rcWindowPos.right - rcWindowPos.left;
		windowHeight = rcWindowPos.bottom - rcWindowPos.top;

		// register window class
		if (!registerWindowClass(device, create_params->icon,
			MainWndProc,
			rdr_state.unicodeRendererWindow))
			return;

		// DJR For sequencing WM_SIZE
		TRACE_WINDOW("CreateWindow%c(WC %s Title %s Flags 0x%x, X %d, Y %d, W %d, H %d /* CW %d CH %d */, NULL, NULL, HINST 0x%p, HMENU NULL)\n", 
			rdr_state.unicodeRendererWindow ? 'W' : 'C',
			device->windowClassName, create_params->window_title?create_params->window_title:windowName,
			windowFlags, 
			params->xpos, params->ypos, 
			windowWidth, windowHeight, params->width, params->height,
			device->windowClass.hInstance);
		if (!rdr_state.unicodeRendererWindow)
		{
			devWindow = CreateWindowA(device->windowClassName,
				create_params->window_title?create_params->window_title:windowName,
				windowFlags,
				params->xpos, params->ypos,
				windowWidth,
				windowHeight,
				NULL,
				NULL,
				device->windowClass.hInstance,
				NULL);
		}
		else
		{
			if (create_params->window_title)
			{
				errno_t errorVal = mbstowcs_s(NULL, windowTitleW, ARRAY_SIZE(windowTitleW), create_params->window_title, strlen(create_params->window_title) );
				if (errorVal != 0)
					wcscpy_s(windowTitleW, ARRAY_SIZE(windowTitleW), windowNameW);
			}
			devWindow = CreateWindowW(device->windowClassNameW,
				create_params->window_title?windowTitleW:windowNameW,
				windowFlags,
				params->xpos, params->ypos,
				windowWidth,
				windowHeight,
				NULL,
				NULL,
				device->windowClassW.hInstance,
				NULL);
		}
		device->hWindow = devWindow;

		if (!device->hWindow)
		{
			TRACE_WINDOW("CreateWindow%c failed; GetLastError() = %d", rdr_state.unicodeRendererWindow ? 'W' : 'C', GetLastError());
			rdrAlertMsg(&device->device_base, "Couldn't create window.");
			return;
		}
		hashDeviceHwnd(device);
	}
#endif
}

//////////////////////////////////////////////////////////////////////////

// Called only in the thread that owns the window
static void APIENTRY HideWindowAPC(ULONG_PTR dwParam)
{
	HWND hwnd = (HWND)dwParam;
	// Ignore our message handler so this goes through immediately and doesn't possibly crash
	early_out_wnd_proc = 1;
	ShowWindow(hwnd, SW_HIDE);
	early_out_wnd_proc = 0;
}

// May be called from any thread
void rxbxAppCrashed(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	if (!device)
		return;
	if (!device->worker_thread)
		return;
	if (!xdevice->hWindow)
		return;
	if (wtIsThreaded(device->worker_thread))
	{
		// If in the thread, do it now, otherwise queue APC
		if (GetCurrentThreadId() == xdevice->thread_id)
		{
			// Ignore our message handler so this goes through immediately and doesn't possibly crash
			early_out_wnd_proc = 1;
			ShowWindow(xdevice->hWindow, SW_HIDE);
			early_out_wnd_proc = 0;
		} else {
			QueueUserAPC(HideWindowAPC, wtGetThreadHandle(device->worker_thread), (ULONG_PTR)xdevice->hWindow);
		}
	} else {
		// WT is not threaded, if we're not in the main thread, no way to get a
		// message there
		if (GetCurrentThreadId() == xdevice->thread_id)
		{
			// Ignore our message handler so this goes through immediately and doesn't possibly crash
			early_out_wnd_proc = 1;
			ShowWindow(xdevice->hWindow, SW_HIDE);
			early_out_wnd_proc = 0;
		} else {
			// /me cries
			// this probably won't do anything, but can't hurt!
			HANDLE hThread;
			early_out_wnd_proc = 1;
			ShowWindowAsync(xdevice->hWindow, SW_HIDE);
			hThread = OpenThread(THREAD_SET_CONTEXT, FALSE, xdevice->thread_id);
			if (hThread)
			{
				QueueUserAPC(HideWindowAPC, hThread, (ULONG_PTR)xdevice->hWindow);
				CloseHandle(hThread);
			}
		}
	}
}


// Called only in-between frame rendering
static void rxbxProcessWindowsMessages2Direct(RdrDeviceDX *device)
{
	MSG msg;
	PERFINFO_AUTO_START_FUNC();
	ASSERT_NOT_INFRAME(device);
	if (!rdr_state.unicodeRendererWindow)
	{
		while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (device->device_base.nSendTextInputEnableCount)
				TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}
	else
	{
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (device->device_base.nSendTextInputEnableCount)
				TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	PERFINFO_AUTO_STOP();
}

void rxbxProcessWindowsMessagesDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	PERFINFO_AUTO_START_FUNC();
	if (!rdr_state.bProcessMessagesOnlyBetweenFrames)
		rxbxProcessWindowsMessages2Direct(device);
	PERFINFO_AUTO_STOP();
}
#endif

// returns an SRGB format based on the given format
static DXGI_FORMAT srgbFormat11(DXGI_FORMAT format)
{
	switch (format){
	case DXGI_FORMAT_B8G8R8X8_UNORM:
		return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case DXGI_FORMAT_BC1_UNORM:
		return DXGI_FORMAT_BC1_UNORM_SRGB;
	case DXGI_FORMAT_BC2_UNORM:
		return DXGI_FORMAT_BC2_UNORM_SRGB;
	case DXGI_FORMAT_BC3_UNORM:
		return DXGI_FORMAT_BC3_UNORM_SRGB;
	}
	return format;
}

RdrSurfaceBufferType rxbxGetSurfaceBufferTypeForDXGIFormat(DXGI_FORMAT Format)
{
	RdrSurfaceBufferType sbt = SBT_RGBA;
	switch (Format)
	{
		xcase DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			sbt = SBT_RGBA;

		xcase DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			sbt = SBT_RGBA | SBT_SRGB;

		xcase DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
			sbt = SBT_RGBA10;

		xcase DXGI_FORMAT_R16G16B16A16_FLOAT:
			sbt = SBT_RGBA_FLOAT;

		xcase DXGI_FORMAT_R32G32B32A32_FLOAT:
			sbt = SBT_RGBA_FLOAT32;

		xdefault:
			assert(0);
	}
	return sbt;
}

static bool rxbxGetPrimarySurfaces( RdrDeviceDX * device )
{
	device->primary_surface.params_thread.name = "Primary";

	rxbxSurfaceCleanupDirect(device, &device->primary_surface, NULL);
	if (device->d3d11_device)
	{
		ID3D11Texture2D * pBackBuffer;
		D3D11_TEXTURE2D_DESC backBufferSurfaceDesc;

		// Get a pointer to the back buffer
		CHECKX(IDXGISwapChain_GetBuffer(device->d3d11_swapchain, 0, &IID_ID3D11Texture2D, 
			(LPVOID*)&pBackBuffer ));

		// Get characteristics
		ID3D11Texture2D_GetDesc(pBackBuffer, &backBufferSurfaceDesc);
		device->primary_surface.buffer_types[0] = rxbxGetSurfaceBufferTypeForDXGIFormat(backBufferSurfaceDesc.Format);

		// Create a render-target view
		CHECKX(ID3D11Device_CreateRenderTargetView(device->d3d11_device, (ID3D11Resource*)pBackBuffer, NULL,
			&device->primary_surface.rendertarget[SBUF_0].d3d_surface.render_target_view11));

		rxbxSurfaceGrowSnapshotTextures(
			device,
			&device->primary_surface,
			SBUF_0, 0,
			"SBUF_0");

		device->primary_surface.rendertarget[SBUF_0].textures[0].d3d_buffer.resource_d3d11 = (ID3D11Resource*)pBackBuffer;

		{
			RdrTextureDataDX *surface_texture;
			RdrSurfaceBindFlags buffer_bind_flags = RSBF_DEPTHSTENCIL;
			RdrTexFormatObj tex_format = {RTEX_D24S8};

			// DX11TODO: do we always want to create this depth surface?  Only when needed?  Create after important/high
			// speed surfaces?

			D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;

			rxbxSurfaceGrowSnapshotTextures(
				device,
				&device->primary_surface,
				SBUF_DEPTH, 0,
				"Depth Stencil");

			surface_texture = rxbxMakeTextureForSurface(
				device,
				&device->primary_surface.rendertarget[SBUF_DEPTH].textures[0].tex_handle,
				tex_format,
				backBufferSurfaceDesc.Width,
				backBufferSurfaceDesc.Height,
				false,
				buffer_bind_flags,
				false,
				0,
				1);

			device->primary_surface.rendertarget[SBUF_DEPTH].textures[0].d3d_texture = surface_texture->texture;
			device->primary_surface.rendertarget[SBUF_DEPTH].textures[0].d3d_buffer = surface_texture->d3d11_data;

			// Create the depth stencil view
			descDSV.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			descDSV.Flags = 0;
			descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			descDSV.Texture2D.MipSlice = 0;

			CHECKX(ID3D11Device_CreateDepthStencilView(
					   device->d3d11_device,
					   surface_texture->d3d11_data.resource_d3d11,
					   &descDSV,
					   &device->primary_surface.rendertarget[SBUF_DEPTH].d3d_surface.depth_stencil_view11 ));

		}

		device->primary_surface_mem_usage[0] = rxbxCalcSurfaceMemUsage(device->primary_surface.width_thread, device->primary_surface.height_thread,
			device->d3d11_swap_desc->SampleDesc.Count, MakeRdrTexFormatObj(RTEX_BGRA_U8));
		device->primary_surface_mem_usage[1] =	rxbxCalcSurfaceMemUsage(device->primary_surface.width_thread, device->primary_surface.height_thread,
			device->d3d11_swap_desc->SampleDesc.Count, MakeRdrTexFormatObj(RTEX_D24S8));
	} else {

		CHECKX(IDirect3DDevice9_GetBackBuffer(device->d3d_device, 0, 0, 0, &device->primary_surface.rendertarget[SBUF_0].d3d_surface.surface9));
		CHECKX(IDirect3DDevice9_GetDepthStencilSurface(device->d3d_device, &device->primary_surface.rendertarget[SBUF_DEPTH].d3d_surface.surface9));

		device->primary_surface_mem_usage[0] = rxbxCalcSurfaceMemUsage(device->primary_surface.width_thread, device->primary_surface.height_thread, 
			device->d3d_present_params->MultiSampleType, MakeRdrTexFormatObj(RTEX_BGRA_U8));
		device->primary_surface_mem_usage[1] = rxbxCalcSurfaceMemUsage(device->primary_surface.width_thread, device->primary_surface.height_thread, 
			device->d3d_present_params->MultiSampleType, MakeRdrTexFormatObj(RTEX_D24S8));
	}
	rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:Backbuffer", 1, device->primary_surface_mem_usage[0]);
	rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:Depthbuffer", 1, device->primary_surface_mem_usage[1]);

	rxbxLogCreateSurface(device, device->primary_surface.rendertarget[SBUF_0].d3d_surface);
	rxbxLogCreateSurface(device, device->primary_surface.rendertarget[SBUF_DEPTH].d3d_surface);

	device->primary_surface.type = SURF_PRIMARY;
	device->primary_surface.supports_post_pixel_ops = 
		device->surface_types_post_pixel_supported[device->primary_surface.buffer_types[0] & SBT_TYPE_MASK];

	rxbxCreateQueries(device);

	return device->primary_surface.rendertarget[SBUF_0].d3d_surface.typeless_surface != NULL;
}

#if !PLATFORM_CONSOLE
static bool rxbxTestForDepthSurfaceSupport(RdrDeviceDX *device)
{
	RdrTexFormatObj tex_format;
	D3DTexture *d3d_texture_2d=NULL;
	DWORD texture_usage = D3DUSAGE_DEPTHSTENCIL;
	bool supports_depth_surface = false;
	if (rxbxGetDepthFormat(device, SF_DEPTH_TEXTURE, &tex_format))
	{
		HRESULT hr = IDirect3DDevice9_CreateTexture(device->d3d_device, 320, 240, 1, 
			texture_usage, rxbxGetGPUFormat9(tex_format),
			D3DPOOL_DEFAULT, &d3d_texture_2d, NULL);
		if (SUCCEEDED(hr))
		{
			IDirect3DTexture9_Release(d3d_texture_2d);
			supports_depth_surface = true;
		}
	}
	return supports_depth_surface;
}
#endif

__forceinline static const char * rxbxGetTextureTypeString( RdrTexType type )
{
	return StaticDefineIntRevLookup(RdrTexTypeEnum, type);
}

const char * rxbxGetTextureFormatString(D3DFORMAT format)
{
	const char * format_str = NULL;
	switch ( format )
	{
	xcase D3DFMT_UNKNOWN:
		format_str = "D3DFMT_UNKNOWN";

	xcase D3DFMT_X8R8G8B8:
		format_str = "D3DFMT_X8R8G8B8";

	xcase D3DFMT_D24S8:
		format_str = "D3DFMT_D24S8";

	xcase D3DFMT_A8R8G8B8:
		format_str = "D3DFMT_A8R8G8B8";

	xcase D3DFMT_R8G8B8:
		format_str = "D3DFMT_R8G8B8";

	xcase D3DFMT_A2B10G10R10:
		format_str = "D3DFMT_A2B10G10R10";

	xcase D3DFMT_A32B32G32R32F:
		format_str = "D3DFMT_A32B32G32R32F";

	xcase D3DFMT_A16B16G16R16F:
		format_str = "D3DFMT_A16B16G16R16F";

    xcase D3DFMT_R32F:
		format_str = "D3DFMT_R32F";

	xcase D3DFMT_G16R16F:
		format_str = "D3DFMT_G16R16F";

	xcase D3DFMT_G16R16:
		format_str = "D3DFMT_G16R16";

	xcase D3DFMT_A16B16G16R16:
		format_str = "D3DFMT_A16B16G16R16";

	xcase D3DFMT_A8L8:
		format_str = "D3DFMT_A8L8";

	xcase D3DFMT_P8:
		format_str = "D3DFMT_P8";

	xcase D3DFMT_R5G6B5:
		format_str = "D3DFMT_R5G6B5";

	xcase D3DFMT_DXT1:
		format_str = "D3DFMT_DXT1";

#if !_XBOX
	xcase D3DFMT_DXT2:
		format_str = "D3DFMT_DXT2";
#endif

	xcase D3DFMT_DXT3:
		format_str = "D3DFMT_DXT3";

#if !_XBOX
	xcase D3DFMT_DXT4:
		format_str = "D3DFMT_DXT4";
#endif

	xcase D3DFMT_DXT5:
		format_str = "D3DFMT_DXT5";

	xcase D3DFMT_A1R5G5B5:
		format_str = "D3DFMT_A1R5G5B5";

	xcase D3DFMT_X1R5G5B5:
		format_str = "D3DFMT_X1R5G5B5";

	xcase D3DFMT_A8:
		format_str = "D3DFMT_A8";

#if !_XBOX
	xcase D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC:
		format_str = "D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC";

	xcase D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC:
		format_str = "D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC";

	xcase D3DFMT_NULL_TEXTURE_FCC:
		format_str = "D3DFMT_NULL_TEXTURE_FCC";

	xcase D3DFMT_ATI_DEPTH_TEXTURE_16_FCC:
		format_str = "D3DFMT_ATI_DEPTH_TEXTURE_16_FCC";

	xcase D3DFMT_ATI_DEPTH_TEXTURE_24_FCC:
		format_str = "D3DFMT_ATI_DEPTH_TEXTURE_24_FCC";
#endif
	xdefault:
		assert( 0 );
	}

	return format_str;
}

void debugOutputHandler(const char * string, char ** estrBuffer)
{
	int appendLen = (int)strlen(string);
	if (estrBuffer[0] && strlen(estrBuffer[0]) + appendLen > 1024)
	{
		OutputDebugString_UTF8(estrBuffer[0]);
		estrDestroy(estrBuffer);
	}

	estrConcat(estrBuffer, string, appendLen);
}

void debugEstrConcatHandler(const char * string, char ** estrBuffer)
{
	int appendLen = (int)strlen(string);
	estrConcat(estrBuffer, string, appendLen);
}

int rxbxCalcRenderTargetTotalSize(const RdrSurfaceDX * surface, const RxbxSurfacePerTargetState * target)
{
	int i;
	int total_mem_size = 0;
	DWORD multisample_quality;
	int multisample_count = rxbxSurfaceGetMultiSampleCount(surface, &multisample_quality);
	RdrTexFormatObj surface_format = rxbxGetTexFormatForSBT(surface->params_thread.buffer_types[0] & SBT_TYPE_MASK);
	if (target->d3d_surface.typeless_surface &&
		!(surface->params_thread.flags & SF_DEPTHONLY && ((RdrDeviceDX*)surface->surface_base.device)->null_supported))
	{
		// memory usage of base surface
		total_mem_size += rxbxCalcSurfaceMemUsage(surface->width_thread, surface->height_thread, multisample_count, surface_format);
		// memory usage of snapshots/auto-resolves/etc?
		multisample_count = 1; // Snapshots are normal res
		for (i=1; i<surface->rendertarget[0].texture_count; i++)
		{
			if (surface->rendertarget[0].textures[i].d3d_texture.typeless)
				total_mem_size += rxbxCalcSurfaceMemUsage(surface->width_thread, surface->height_thread, multisample_count, surface_format);
		}
	}
	return total_mem_size;
}

int rxbxCalcDepthTargetTotalSize(const RdrSurfaceDX * surface, const RxbxSurfacePerTargetState * target)
{
	int i;
	int total_mem_size = 0;
	DWORD multisample_quality;
	int multisample_count = rxbxSurfaceGetMultiSampleCount(surface, &multisample_quality);
	RdrTexFormatObj surface_format = {RTEX_INVALID_FORMAT};
	if (target->d3d_surface.typeless_surface)
	{
		rxbxGetDepthFormat((RdrDeviceDX*)surface->surface_base.device, surface->creation_flags, &surface_format);
		// memory usage of base surface
		total_mem_size += rxbxCalcSurfaceMemUsage(surface->width_thread, surface->height_thread, multisample_count, surface_format);
		// memory usage of snapshots/auto-resolves/etc?
		multisample_count = 1; // Snapshots are normal res
		for (i=1; i<surface->rendertarget[SBUF_DEPTH].texture_count; i++)
		{
			if (surface->rendertarget[SBUF_DEPTH].textures[i].d3d_texture.typeless)
				total_mem_size += rxbxCalcSurfaceMemUsage(surface->width_thread, surface->height_thread, multisample_count, surface_format);
		}
	}
	return total_mem_size;
}

int rxbxCalcSurfaceTotalSize(const RdrSurfaceDX * surface)
{
	int j, total_mem_size = 0;
	for (j = SBUF_0; j < SBUF_DEPTH; ++j)
		total_mem_size += rxbxCalcRenderTargetTotalSize(surface, surface->rendertarget + j);
	total_mem_size += rxbxCalcDepthTargetTotalSize(surface, surface->rendertarget + SBUF_DEPTH);

	return total_mem_size;
}

void rxbxQueryAllSurfacesDirect(RdrDevice *device, RdrSurfaceQueryAllData **data_in, WTCmdPacket *packet)
{
	int i, j, k, tc;
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	RdrSurfaceQueryAllData *data = *data_in;
	PERFINFO_AUTO_START_FUNC();
	data->nsurfaces = eaSize(&xdevice->surfaces);
	MIN1(data->nsurfaces, ARRAY_SIZE(data->details));
	for (i=0; i<data->nsurfaces; i++)
	{
		const RdrSurfaceDX *surface = xdevice->surfaces[i];
		RdrSurfaceQueryData *entry = &data->details[i];
		entry->name = surface->params_thread.name ? surface->params_thread.name : "Unnamed";
		entry->w = surface->width_thread;
		entry->h = surface->height_thread;
		entry->mrt_count = rxbxGetSurfaceMRTCount(surface);
		entry->msaa = surface->params_thread.desired_multisample_level;
		for (j=0; j<SBUF_MAXMRT; j++)
		{
			entry->buffer_types[j] = surface->buffer_types[j];
			for (k=0, tc=0; k<surface->rendertarget[j].texture_count; k++)
			{
				if (surface->rendertarget[j].textures[k].d3d_texture.typeless)
				{
					tc++;
					if (k < ARRAY_SIZE(entry->snapshot_names[j]))
						entry->snapshot_names[j][k] = surface->rendertarget[j].textures[k].name;
				} else {
					if (k < ARRAY_SIZE(entry->snapshot_names[j]))
						entry->snapshot_names[j][k] = "Empty";
				}
			}
			entry->texture_count[j] = tc;;
			entry->texture_max[j] = surface->rendertarget[j].texture_count;;
		}
		for (k=0, tc=0; k<surface->rendertarget[SBUF_DEPTH].texture_count; k++)
		{
			if (surface->rendertarget[SBUF_DEPTH].textures[k].d3d_texture.typeless)
			{
				tc++;
				if (k < ARRAY_SIZE(entry->snapshot_names[SBUF_DEPTH]))
					entry->snapshot_names[SBUF_DEPTH][k] = surface->rendertarget[SBUF_DEPTH].textures[k].name;
			} else {
				if (k < ARRAY_SIZE(entry->snapshot_names[SBUF_DEPTH]))
					entry->snapshot_names[SBUF_DEPTH][k] = "Empty";
			}
		}
		entry->texture_count[SBUF_DEPTH] = tc;;
		entry->texture_max[SBUF_DEPTH] = surface->rendertarget[SBUF_DEPTH].texture_count;;

		entry->total_mem_size = rxbxCalcSurfaceTotalSize(surface);
		if (!surface->surface_base.destroyed_nonthread)
		{
			entry->rdr_surface = &xdevice->surfaces[i]->surface_base;
		} else {
			entry->rdr_surface = NULL;
		}
	}
	PERFINFO_AUTO_STOP();
}

void rxbxDumpDeviceState(const RdrDeviceDX *device, int trivia_log, int dump_objects)
{
	U32 i, m;
	char *dump_text = NULL;
	StashTableIterator stash_iter;
	StashElement stash_elem;
	int line = 1;
	int first_tex_line, first_surface_line;
	OutputHandler handler = debugOutputHandler;
	if (trivia_log > 0)
		handler = debugEstrConcatHandler;

	handlerPrintf(handler, &dump_text, "%9s,%9s,%9s,%9s,%9s,%9s,%9s,%9s,%9s,%9s\n",
		"Surfaces", "Vtx Dcls", "Tex", "Cube Tex", "Vol Tex", "Vtx Buf", "Idx Buf", "Queries", 
		"Vtx Shdrs", "Pxl Shdrs");
	handlerPrintf(handler, &dump_text, "%9d,%9d,%9d,%9d,%9d,%9d,%9d,%9d,%9d,%9d\n",
		device->count_surfaces,
		device->count_vertex_declarations,
		device->count_textures,
		device->count_cubetextures,
		device->count_volumetextures,
		device->count_vertexbuffers,
		device->count_indexbuffers,
		device->count_queries,
		device->count_vertexshaders,
		device->count_pixelshaders
		);
	line += 2;
	if (trivia_log < 0)
		OutputDebugStringf("%s", dump_text);

	if (dump_objects)
	{
		handlerPrintf(handler, &dump_text, "\n\n%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
			"Name", "Width", "Height", "MRT", "MSAA", "Buf 0 Type", "Buf 0 Snaps", "Buf 1 Type", "Buf 1 Snaps", 
			"Buf 2 Type", "Buf 2 Snaps", "Buf 3 Type", "Buf 3 Snaps", "Depth Bufs", "Size");
		line += 3;
		first_surface_line = line;
		for (i = 0, m = eaSize(&device->surfaces); i < m; ++i)
		{
			const RdrSurfaceDX * surface = device->surfaces[i];
			int total_mem_size = rxbxCalcSurfaceTotalSize(surface);

			handlerPrintf(handler, &dump_text, "%s,%d,%d,%d,%d,%s,%d,%s,%d,%s,%d,%s,%d,%d,%d\n",
				surface->params_thread.name ? surface->params_thread.name : "Unnamed", 
				surface->width_thread, surface->height_thread,
				rxbxGetSurfaceMRTCount(surface),
				surface->params_thread.desired_multisample_level,
				rdrGetSurfaceBufferTypeNameString(surface->buffer_types[0]),
				surface->rendertarget[SBUF_0].texture_count,
				rdrGetSurfaceBufferTypeNameString(surface->buffer_types[1]),
				surface->rendertarget[SBUF_1].texture_count,
				rdrGetSurfaceBufferTypeNameString(surface->buffer_types[2]),
				surface->rendertarget[SBUF_2].texture_count,
				rdrGetSurfaceBufferTypeNameString(surface->buffer_types[3]),
				surface->rendertarget[SBUF_3].texture_count,
				surface->rendertarget[SBUF_DEPTH].texture_count,
				total_mem_size
				);
			++line;
		}
		handlerPrintf(handler, &dump_text, ",,,,,,,,,,,,,=sum(N%d:N%d),\n",
			first_surface_line, line-1);

		handlerPrintf(handler, &dump_text, "\n\n%s,%s,%s,%s,%s,%s,%s,%s\n",
			"Name", "Width", "Height", "Format", "Type", "Mips?", "Mem(K)", "Created while dev lost" );
		line += 3;
		first_tex_line = line;
		stashGetIterator(device->texture_data, &stash_iter);
		for (i = 0; stashGetNextElement(&stash_iter, &stash_elem); ++i)
		{
			const RdrTextureDataDX *tex_data;
			tex_data = stashElementGetPointer(stash_elem);

			handlerPrintf(handler, &dump_text, "%s,%d,%d,%s,%s,%d,%d,%d\n",
				tex_data->memmonitor_name ? tex_data->memmonitor_name : "Unnamed", 
				tex_data->width, tex_data->height,
				rdrTexFormatName(tex_data->tex_format),
				rxbxGetTextureTypeString(tex_data->tex_type),
				tex_data->max_levels,
				tex_data->memory_usage / 1024,
				tex_data->created_while_dev_lost
				);
			++line;
		}
		handlerPrintf(handler, &dump_text, ",,,,,,=sum(G%d:G%d),\n",
			first_tex_line, line-1);
		++line;

		if (!trivia_log)
		{
			if (dump_text)
				OutputDebugString_UTF8(dump_text);
		}
		else
		if (dump_text)
		{
			triviaPrintf("Device_state", "%s", dump_text);
		}
	}

	if (dump_text)
	{
		S32 video_mem_estimate = memMonitorGetVideoMemUseEstimate();
		OutputDebugStringf("VideoMemEstimate: %d\n", video_mem_estimate / 1024);
	}

	estrDestroy(&dump_text);
}

void rxbxDumpDeviceStateOnError(const RdrDeviceDX *device, HRESULT hr)
{
	if (hr==D3DERR_DRIVERINTERNALERROR || hr==D3DERR_INVALIDCALL || 
		hr==D3DERR_NOTAVAILABLE || hr==D3DERR_OUTOFVIDEOMEMORY || hr==E_OUTOFMEMORY)
		rxbxDumpDeviceState(device, 1, false);
}

//callback used for Xlive integration
rxbxCreateDirect_Callback prxbxCreateDirect_Callback = NULL;

void Set_rxbxCreateDirect_Callback( rxbxCreateDirect_Callback pCallbackRtn )
{
	prxbxCreateDirect_Callback = pCallbackRtn;
}

#if !PLATFORM_CONSOLE
void rxbxCheckNVAPI(RdrDeviceDX *device, D3DADAPTER_IDENTIFIER9 * adapterId)
{
	if (nv_api_avail)
	{
		DWORD acceptableDriver = MAKELONG(8000,11);
		DWORD acceptableStereoDriver = MAKELONG(2723,13);
		if ( adapterId->DriverVersion.LowPart >= acceptableDriver )
			device->nv_api_support = 1;
		if ( adapterId->DriverVersion.LowPart >= acceptableStereoDriver )
			device->bEnableNVStereoscopicAPI = 1;
	}
}

// Recorded first instance of a driver that was able to successfully use the nvidia stereoscopic api.
#define NV_ACCEPTABLE_STEREO_DRIVER 0x00007fd3

void rxbxCheckNVAPI11(RdrDeviceDX *device)
{
	if (nv_api_avail)
	{
		U32 driverVersion = rxbxNVDriverVersion();
		if (driverVersion >= NV_ACCEPTABLE_STEREO_DRIVER)
			device->bEnableNVStereoscopicAPI = 1;
	}
}

void nvapiRegister(IUnknown *d3d_device)
{
	if (system_specs.videoCardVendorID == VENDOR_NV)
	{
#define CHAMPIONS_ONLIE_APPID 0x8AE0E07E // May use for all CrypticEngine games
		NvAPI_Status status;
		status = NvAPI_D3D_RegisterApp((IUnknown*)d3d_device, CHAMPIONS_ONLIE_APPID);
		switch (status)
		{
		xcase NVAPI_OK:
			verbose_printf("NVIDIA appID successfully registered\n");
		xcase NVAPI_NOT_SUPPORTED:
			verbose_printf("NVIDIA appID not supported (need newer drivers?)\n");
		xcase NVAPI_ERROR:
			printfColor(COLOR_RED, "\nNVIDIA appID registration FAILED\n");
		xcase NVAPI_INVALID_ARGUMENT:
			verbose_printf("NVIDIA appID invalid argument (need newer drivers)\n");
		xdefault:
			printfColor(COLOR_RED, "\nNVIDIA appID registration UNKNOWN RESULT\n");
		}
	}
}
#endif

bool disableMSAAOnATI = false;
AUTO_CMD_INT(disableMSAAOnATI, disableMSAAOnATI) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

void rxbxInitPrimarySurfaceState(RdrDeviceDX *device, int device_width, int device_height)
{
	device->primary_surface.width_thread = device->primary_surface.surface_base.width_nonthread = 
		device->primary_surface.surface_base.vwidth_nonthread = device_width;
	device->primary_surface.height_thread = device->primary_surface.surface_base.height_nonthread = 
		device->primary_surface.surface_base.vheight_nonthread = device_height;

	copyMat44(unitmat44, device->primary_surface.state.projection_mat3d);
	copyMat44(unitmat44, device->primary_surface.state.far_depth_projection_mat3d);
	copyMat44(unitmat44, device->primary_surface.state.sky_projection_mat3d);
	copyMat44(unitmat44, device->primary_surface.state.inv_projection_mat3d);
	copyMat44(unitmat44, device->primary_surface.state.viewmat);
	copyMat4(unitmat, device->primary_surface.state.viewmat4);
	copyMat4(unitmat, device->primary_surface.state.inv_viewmat);
	copyMat4(unitmat, device->primary_surface.state.fog_mat);
	copyMat44(unitmat44, device->primary_surface.state.modelmat);
	setVec4(device->primary_surface.state.depth_range, -50000.0f, 50000.0f, 1.0f, 1.0f);
	setVec4(device->primary_surface.state.viewport, 0, 1, 0, 1);
	device->primary_surface.state.width_2d = device_width;
	device->primary_surface.state.height_2d = device_height;
	device->primary_surface.state.surface_width = device_width;
	device->primary_surface.state.surface_height = device_height;
	setVec4(device->primary_surface.state.inv_screen_params, 
		1.0f / device_width, 1.0f / device_height, 
		0.5f / device_width, 0.5f / device_height);
}

void rxbxLogPresentParams(RdrDeviceDX *device, const char *pszMarker,
	const D3DPRESENT_PARAMETERS * d3d_present_params,
	UINT Adapter, D3DDEVTYPE DeviceType, DWORD BehaviorFlags)
{
	TRACE_WINDOW(
		"%s W %d H %d Fmt %s %x BackBCnt %d MS %d MSQ %d Swap %d HWnd 0x%p WndOrFS %s "
		"AutoDS %s AutoDsFmt %s Flags %x Refresh %d PresentInt %d; Adapter %u, DevType %u, BehaviorFlags 0x%x",
		pszMarker,
		d3d_present_params->BackBufferWidth,
		d3d_present_params->BackBufferHeight,
		rxbxGetTextureFormatString(d3d_present_params->BackBufferFormat),
		d3d_present_params->BackBufferFormat,
		d3d_present_params->BackBufferCount,
		d3d_present_params->MultiSampleType,
		d3d_present_params->MultiSampleQuality,
		d3d_present_params->SwapEffect,
		d3d_present_params->hDeviceWindow,
		d3d_present_params->Windowed ? "Win" : "FS",
		d3d_present_params->EnableAutoDepthStencil ? "Y" : "N",
		rxbxGetTextureFormatString(d3d_present_params->AutoDepthStencilFormat),
		d3d_present_params->Flags,
		d3d_present_params->FullScreen_RefreshRateInHz,
		d3d_present_params->PresentationInterval,
		Adapter,
		DeviceType,
		BehaviorFlags
		);
}

void rxbxLogSwapChainParams(RdrDeviceDX *device, const char *pszMarker,
	const DXGI_SWAP_CHAIN_DESC *swap_chain_desc)
{
	TRACE_WINDOW(
		"%s W %d H %d Fmt %x Refresh %d/%d WinOrFS %s",
		pszMarker,
		swap_chain_desc->BufferDesc.Width,
		swap_chain_desc->BufferDesc.Height,
		swap_chain_desc->BufferDesc.Format,
		swap_chain_desc->BufferDesc.RefreshRate.Numerator,
		swap_chain_desc->BufferDesc.RefreshRate.Denominator,
		swap_chain_desc->Windowed ? "Win" : "FS"
		);
}

void rxbxSafeDestroyWindow(RdrDeviceDX *device)
{
	if (device->hWindow)
	{
		BOOL bDestroySuccess = FALSE;
		TRACE_WINDOW("Destroy device window for 0x%p", device);
		removeHwnd(device->hWindow);
		SetLastError(0);
		bDestroySuccess = DestroyWindow(device->hWindow);
		if (bDestroySuccess)
			TRACE_WINDOW("DestroyWindow succeeded");
		else
		{
			DWORD gle = GetLastError();
			TRACE_WINDOW("DestroyWindow failed, GetLastError() = %u", gle);
		}
		device->hWindow = NULL;
	}
}

void rxbxSafeReleaseDevice(RdrDeviceDX *device, bool bFlushAndClear)
{
	TRACE_WINDOW("Direct3D device shutdown for 0x%p", device);
	if (device->d3d_device)
	{
		ULONG deviceRefs = IDirect3DDevice9_Release(device->d3d_device);
		TRACE_WINDOW("Released Direct3D9 device, 0x%p %u refs", device, deviceRefs);
		device->d3d_device = NULL;
	}
	if (device->d3d_device_ex)
	{
		ULONG deviceRefs = IDirect3DDevice9Ex_Release(device->d3d_device_ex);
		TRACE_WINDOW("Release Direct3D9 Ex device, 0x%p %u refs", device, deviceRefs);
		device->d3d_device_ex = NULL;
	}

	if (device->d3d11_imm_context)
	{
		ULONG contextRefs = 0;
		if (bFlushAndClear)
		{
			TRACE_WINDOW("Clearing Direct3D11 context state");
			ID3D11DeviceContext_ClearState(device->d3d11_imm_context);
			TRACE_WINDOW("Flushing Direct3D11 context");
			ID3D11DeviceContext_Flush(device->d3d11_imm_context);
		}
		contextRefs = ID3D11DeviceContext_Release(device->d3d11_imm_context);
		TRACE_WINDOW("Released Direct3D11 context %p, %u refs", device, contextRefs);
		device->d3d11_imm_context = NULL;
	}
	if (device->d3d11_device)
	{
		ULONG deviceRefs = ID3D11Device_Release(device->d3d11_device);
		TRACE_WINDOW("Released Direct3D11 device, %u refs", deviceRefs);
		device->d3d11_device = NULL;
	}
}

void rxbxSafeReleaseDirect3D(RdrDeviceDX *device)
{
	if (device->d3d9)
	{
		ULONG direct3D9Refs = IDirect3D9_Release(device->d3d9);
		TRACE_WINDOW("Released Direct3D9 system, %u refs", direct3D9Refs);
		device->d3d9 = NULL;
	}
	if (device->d3d9ex)
	{
		ULONG direct3D9ExRefs = IDirect3D9Ex_Release(device->d3d9ex);
		TRACE_WINDOW("Released Direct3D9Ex system, %u refs", direct3D9ExRefs);
		device->d3d9ex = NULL;
	}
	TRACE_WINDOW("Direct3D shutdown for 0x%p", device);
}

void rxbxSafeShutdownEntireDevice(RdrDeviceDX *device, bool bFlushAndClear)
{
	rxbxSafeDestroyWindow(device);
	rxbxSafeReleaseDevice(device, bFlushAndClear);
	rxbxSafeReleaseDirect3D(device);
}

static HRESULT rxbxCreateD3D9ExDevice(RdrDeviceDX *device, const RdrDeviceModeDX *fullscreen_mode,
	const D3DPRESENT_PARAMETERS * d3d_present_params,
	UINT AdapterToUse, D3DDEVTYPE DeviceType, DWORD BehaviorFlags, BOOL bFailureFatal)
{
	HRESULT hrD3DCreate = S_OK;
	bool bTracedError = false;
	int numRetries = 0;
	D3DDISPLAYMODEEX fsDisplayMode = { 0 };

	FP_NO_EXCEPTIONS_BEGIN;
	hrD3DCreate = rxbxCreateD3D9Ex(&device->d3d9ex);
	FP_NO_EXCEPTIONS_END;

	if (FAILED(hrD3DCreate))
	{
		char failureMessage[ 256 ];
		sprintf(failureMessage, "Couldn't initialize Direct3D9Ex. Direct3DCreate9Ex failed with error %s (0x%x)",
			rxbxGetStringForHResult(hrD3DCreate), hrD3DCreate);
		if (bFailureFatal)
			rdrAlertMsg(&device->device_base, failureMessage);
		else
			memlog_printf(NULL, "%s", failureMessage);
		return hrD3DCreate;
	}
	// set both pointers to the Ex interface for compatibility
	IDirect3D9Ex_AddRef(device->d3d9ex);
	device->d3d9 = (IDirect3D9*)device->d3d9ex;

#if !_XBOX
	memMonitorTrackUserMemory("D3D", 1, 3000*1024, 1);
#endif
	device->bIsDuringCreateDevice = 1;

	if (!device->d3d_present_params->Windowed)
	{
		fsDisplayMode.Size = sizeof(fsDisplayMode);
		fsDisplayMode.Width = fullscreen_mode->d3d9_mode.Width;
		fsDisplayMode.Height = fullscreen_mode->d3d9_mode.Height;
		fsDisplayMode.RefreshRate = fullscreen_mode->d3d9_mode.RefreshRate;
		fsDisplayMode.Format = fullscreen_mode->d3d9_mode.Format;
		fsDisplayMode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
	}

	while (FAILED(hrD3DCreate = IDirect3D9Ex_CreateDeviceEx(device->d3d9ex, AdapterToUse, DeviceType, 
		device->hWindow, BehaviorFlags, 
		device->d3d_present_params, device->d3d_present_params->Windowed ? NULL : &fsDisplayMode, &device->d3d_device_ex)))
	{
		char failureMessage[ 256 ];
		sprintf(failureMessage, "Couldn't create Direct3D device. IDirect3D9Ex::CreateDeviceEx failed with hr %s (0x%x)",
			rxbxGetStringForHResult(hrD3DCreate), hrD3DCreate);
		memlog_printf(NULL, "%s", failureMessage);
		rxbxLogPresentParams(device, "Params after failed CreateDeviceEx", device->d3d_present_params, 
			AdapterToUse, DeviceType, BehaviorFlags);
#if !_XBOX
		if (!device->d3d_present_params->Windowed)
		{
			//try windowed mode
			device->d3d_present_params->FullScreen_RefreshRateInHz = 0;
			device->d3d_present_params->SwapEffect = D3DSWAPEFFECT_COPY;
			device->d3d_present_params->Windowed = TRUE;
			device->d3d_present_params->BackBufferFormat = D3DFMT_UNKNOWN;
		}
		else if (BehaviorFlags & D3DCREATE_HARDWARE_VERTEXPROCESSING)
		{
			BehaviorFlags &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
			BehaviorFlags &= ~D3DCREATE_PUREDEVICE;
			BehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
		}
		else
#endif
		{
			break;
		}
	}
	if (device->d3d_device_ex)
	{
		// set both D3D9 device pointers to the Ex device
		IDirect3DDevice9Ex_AddRef(device->d3d_device_ex);
		device->d3d_device = (IDirect3DDevice9*)device->d3d_device_ex;
		rxbxLogPresentParams(device, "CreateDeviceEx done", device->d3d_present_params, 
			AdapterToUse, DeviceType, BehaviorFlags);
	}
	device->bIsDuringCreateDevice = 0;

	return hrD3DCreate;
}

static D3DFORMAT rxbxGetDisplayFormat(D3DFORMAT BackBufferFormat)
{
	D3DFORMAT displayFormat = BackBufferFormat;
	if (displayFormat == D3DFMT_A8R8G8B8)
		displayFormat = D3DFMT_X8R8G8B8;
	else
	if (displayFormat == D3DFMT_A1R5G5B5)
		displayFormat = D3DFMT_X1R5G5B5;
	return displayFormat;
}

void rxbxCreateDirect(RdrDeviceDX *device, WindowCreateParams *create_params, WTCmdPacket *packet)
{
	HRESULT hr;
	LPDIRECT3D9 d3d = NULL;
	int i;
	UINT AdapterToUse = D3DADAPTER_DEFAULT;
	D3DCAPS9 rdr_caps_d3d9;
	D3DDEVTYPE DeviceType = rdr_state.usingNVPerfHUD ? D3DDEVTYPE_REF : D3DDEVTYPE_HAL;
	HMONITOR preferred_monitor_handle;
	const RdrDeviceInfo * const * device_infos = NULL;
	const RdrDeviceInfo * preferred_device_info = NULL;
	RdrDeviceModeDX * fullscreen_mode = NULL;
	DisplayParams * params = &create_params->display;
	U32 availableMemA,availableMemB;

#if !_XBOX
	D3DADAPTER_IDENTIFIER9 AdapterIdentifier;
	DWORD creationflags = D3DCREATE_HARDWARE_VERTEXPROCESSING | 
#if !VERIFY_STATEMANAGEMENT
		D3DCREATE_PUREDEVICE |
#endif
		(rdr_state.bD3D9ClientManagesWindow ? D3DCREATE_NOWINDOWCHANGES : 0);

	device->thread_id = GetCurrentThreadId();
	device->device_base.bManualWindowManagement = true;

	allocWindowMessageTimers();
#else
	DWORD creationflags = D3DCREATE_BUFFER_2_FRAMES;
#endif

	clearFPExceptionMaskForThisThread(NULL);

	rxbxInitBackgroundShaderCompile();

	assert(!device->d3d_device);

	// Find appropriate adapter and monitor
	device_infos = rdrEnumerateDevices();
	assert(params->preferred_adapter >= 0 && params->preferred_adapter < eaSize(&device_infos));
	preferred_device_info = device_infos[params->preferred_adapter];
	preferred_monitor_handle = preferred_device_info->monitor_handle;
	AdapterToUse = preferred_device_info->adapter_index;
	device->device_info_index = params->preferred_adapter;
	device->device_base.primary_monitor = preferred_device_info->monitor_index;

	triviaPrintf("D3DDeviceType", "%s", preferred_device_info->type);

	if (params->fullscreen)
	{
		// cover the normal Windows desktop rectangle of the target monitor,
		// so we don't show a partially-covering black box.
		rwinDisplayParamsSetToCoverMonitor(&device->device_base, preferred_device_info->monitor_handle, params);
		fullscreen_mode = (RdrDeviceModeDX *)preferred_device_info->display_modes[params->preferred_fullscreen_mode];
		device->device_width = fullscreen_mode->base.width;
		device->device_height = fullscreen_mode->base.height;
		params->refreshRate = preferred_device_info->display_modes[params->preferred_fullscreen_mode]->refresh_rate;
	}
	else
	{
		if (params->maximize || params->windowed_fullscreen)
			// cover the normal Windows desktop rectangle of the target monitor,
			// so we don't show a partially-covering black box.
			rwinDisplayParamsSetToCoverMonitorForSavedWindow(&device->device_base, params);
		device->device_width = params->width;
		device->device_height = params->height;
	}
	device->display_thread = *params;

	rxbxInitPrimarySurfaceState(device, params->width, params->height);

	device->stPixelShaderCache = stashTableCreateInt(512);
	device->d3d_present_params = &device->d3d_present_params_for_debug; // Only DX9 device has this member non-NULL

	getPhysicalMemoryEx(NULL, NULL, &availableMemA);
#if !_XBOX
	//assert(!device->hWindow);
	// create window
	if (!device->hWindow)
	{
		CreateRenderWindow(device, create_params);
	}

	rxbxPreserveGammaRampDirect(device);

	device->allow_windowed_fullscreen = params->allow_windowed_fullscreen;
#endif


	device->primary_surface.width_thread = device->primary_surface.surface_base.width_nonthread = 
		device->primary_surface.surface_base.vwidth_nonthread = device->device_width;
	device->primary_surface.height_thread = device->primary_surface.surface_base.height_nonthread = 
		device->primary_surface.surface_base.vheight_nonthread = device->device_height;

	for (i = 0; i < SBUF_MAXMRT; i ++) {
		device->primary_surface.buffer_types[i] = SBT_RGBA;	// Setting buffer_types explicitly.  From now on, if the surface type is changed, the buffer_types need to be changed with it otherwise an assert will be triggered in rt_xtextures when a debugframe is being grabbed (and perhaps elsewhere).
		if (params->srgbBackBuffer)
			device->primary_surface.buffer_types[i] |= SBT_SRGB;
	}

	device->d3d_present_params->BackBufferWidth = device->device_width;
	device->d3d_present_params->BackBufferHeight = device->device_height;
	device->d3d_present_params->BackBufferCount = 1;
	device->d3d_present_params->BackBufferFormat = params->fullscreen ? fullscreen_mode->d3d9_mode.Format : D3DFMT_UNKNOWN;

	device->d3d_present_params->MultiSampleType = D3DMULTISAMPLE_NONE;
	device->d3d_present_params->EnableAutoDepthStencil = TRUE;
	device->d3d_present_params->Flags = 0;
	device->d3d_present_params->FullScreen_RefreshRateInHz = params->refreshRate;

	device->d3d_present_params->SwapEffect = D3DSWAPEFFECT_DISCARD;
	device->d3d_present_params->Windowed = FALSE;
	device->d3d_present_params->PresentationInterval = params->vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
	device->d3d_present_params->hDeviceWindow = device->hWindow;
#if !_XBOX
	device->d3d_present_params->Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
	if (!params->fullscreen)
	{
		device->d3d_present_params->FullScreen_RefreshRateInHz = 0;
		device->d3d_present_params->SwapEffect = D3DSWAPEFFECT_COPY;
		device->d3d_present_params->Windowed = TRUE;
	}
#endif
	device->d3d_present_params->AutoDepthStencilFormat = D3DFMT_D24S8;

#if _XBOX
	if ( creationflags & D3DCREATE_BUFFER_2_FRAMES )
	{
		// Default size
		device->d3d_present_params->RingBufferParameters.SecondarySize = 32 * 1024;
		// double the secondary ring buffer to allow more commands to queue
		// without stalling CPU or GPU, per recommendation from GameFest
		// D3D/GPU performance update presentation
		device->d3d_present_params->RingBufferParameters.SecondarySize = 8 * 1024 * 1024;
	}
#endif

#if !_XBOX
	memMonitorTrackUserMemory("D3D", 1, 3000*1024, 1);
#endif

	if (!strcmp(preferred_device_info->type, DEVICETYPE_D3D9EX))
	{
		rxbxCreateD3D9ExDevice(device, fullscreen_mode, device->d3d_present_params, 
			AdapterToUse, DeviceType, creationflags, true);
		// make a local copy of the pointer - but note that device solely owns the references
		d3d = device->d3d9;
	}
	else
	{
		HRESULT hrCheckDisplayMode = S_OK;
		HRESULT hrCheckDeviceType = S_OK;
		HRESULT hrGetDeviceCaps = S_OK;
		D3DDISPLAYMODE d3dAdapterModeDesc = { 0 };
		D3DCAPS9 d3dDeviceCaps = { 0 };

		FP_NO_EXCEPTIONS_BEGIN;
			device->d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
		FP_NO_EXCEPTIONS_END;

		// make a local copy of the pointer - but note that device solely owns the references
		d3d = device->d3d9;

		hrCheckDisplayMode = IDirect3D9_GetAdapterDisplayMode(d3d, AdapterToUse, &d3dAdapterModeDesc);
		TRACE_WINDOW("GetAdapterDisplayMode for %d returns %s 0x%x: W %u H %u Fmt %u %s %u", AdapterToUse, 
			rxbxGetStringForHResult(hrCheckDisplayMode), hrCheckDisplayMode, 
			d3dAdapterModeDesc.Width, d3dAdapterModeDesc.Height, 
			d3dAdapterModeDesc.Format, rxbxGetTextureFormatString(d3dAdapterModeDesc.Format),
			d3dAdapterModeDesc.RefreshRate);

		hrGetDeviceCaps = IDirect3D9_GetDeviceCaps(d3d, AdapterToUse, DeviceType, &d3dDeviceCaps);
		TRACE_WINDOW("GetDeviceCaps for %d returns %s 0x%x", AdapterToUse, 
			rxbxGetStringForHResult(hrGetDeviceCaps), hrGetDeviceCaps);
		if (SUCCEEDED(hrGetDeviceCaps))
		{
			// For devices without D3DCREATE_PUREDEVICE support, fall back to software
			if (!(d3dDeviceCaps.DevCaps & D3DDEVCAPS_PUREDEVICE))
			{
				creationflags &= ~(D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE);
				creationflags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
			}
		}

		hrCheckDeviceType = IDirect3D9_CheckDeviceType(d3d, AdapterToUse, DeviceType, 
			device->d3d_present_params->Windowed ? d3dAdapterModeDesc.Format : rxbxGetDisplayFormat(device->d3d_present_params->BackBufferFormat),
			device->d3d_present_params->BackBufferFormat, 
			device->d3d_present_params->Windowed);
		TRACE_WINDOW("CheckDeviceType returns %s 0x%x", rxbxGetStringForHResult(hrCheckDeviceType), hrCheckDeviceType);

		device->bIsDuringCreateDevice = 1;
		rxbxLogPresentParams(device, "Call CreateDevice with ", device->d3d_present_params, 
			AdapterToUse, DeviceType, creationflags);
		while (FAILED(hr = IDirect3D9_CreateDevice(d3d, AdapterToUse, DeviceType, 
			device->hWindow, creationflags, 
			device->d3d_present_params, &device->d3d_device)))
		{
			char failureMessage[ 256 ];
			sprintf(failureMessage, "Couldn't create D3D device. IDirect3D9::CreateDevice failed with hr %s (0x%x)",
				rxbxGetStringForHResult(hr), hr);
			TRACE_WINDOW("%s", failureMessage);
			rxbxLogPresentParams(device, "Params after failed CreateDevice", device->d3d_present_params, 
				AdapterToUse, DeviceType, creationflags);

			ErrorDetailsf("%s", failureMessage);
			ErrorDeferredf("Direct3D9 device startup failure.");
#if !_XBOX
			if (!device->d3d_present_params->Windowed)
			{
				//try windowed mode
				device->d3d_present_params->FullScreen_RefreshRateInHz = 0;
				device->d3d_present_params->SwapEffect = D3DSWAPEFFECT_COPY;
				device->d3d_present_params->Windowed = TRUE;
				device->d3d_present_params->BackBufferFormat = D3DFMT_UNKNOWN;
			}
			else if (creationflags & D3DCREATE_HARDWARE_VERTEXPROCESSING)
			{
				creationflags &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
				creationflags &= ~D3DCREATE_PUREDEVICE;
				creationflags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
			}
			else
#endif
			{
				rxbxSafeShutdownEntireDevice(device, false);
				if (create_params->bNotifyUserOnCreateFail)
					rdrAlertMsg(&device->device_base, failureMessage);
				return;
			}
			rxbxLogPresentParams(device, "Params before next CreateDevice retry", device->d3d_present_params, 
				AdapterToUse, DeviceType, creationflags);
		}
		rxbxLogPresentParams(device, "CreateDevice done", device->d3d_present_params, 
			AdapterToUse, DeviceType, creationflags);
		device->bIsDuringCreateDevice = 0;
	}

	// I don't know how scientific this is, but it seems better than a hard-coded guess.
	getPhysicalMemoryEx(NULL, NULL, &availableMemB);
	rdrTrackUserMemoryDirect(&device->device_base, "D3DDevice", 1, availableMemA-availableMemB);

	// TODO can we get the window tracking to get this data precisely?
	if (params->fullscreen)
	{
		// Device create with fullscreen can change desktop monitor rectangle layout
		// so find the new rectangle
		rwinDisplayParamsSetToCoverMonitorForDeviceWindow(&device->device_base, device->hWindow, params);
		rxbxClipCursorToFullscreen(device);
	}
	device->display_thread = *params;

#if !_XBOX
	nvapiRegister((IUnknown*)device->d3d_device);
#endif

	SET_FP_CONTROL_WORD_DEFAULT;

#if 0
#if _XBOX
	// Command buffer
	rdrTrackUserMemoryDirect(&device->device_base, "rxbx:RingBuffer", 1, device->d3d_present_params->RingBufferParameters.PrimarySize + 
		device->d3d_present_params->RingBufferParameters.SecondarySize);
	// Note: on Xbox, it seems to additionally allocate memory to hold the backbuffer, + 256KB (around 4MB more),
	//   but on the first call to Present, 2-4 MBs are released.  This is tracking the observed result of that:
	//   JE: Now this is tracked as part of XMemAlloc:D3D:Physical
	//rdrTrackUserMemoryDirect(&device->device_base, "rxbx:DeviceOverhead", 1, 1100*1024);
#else
		rdrTrackUserMemoryDirect(&device->device_base, "D3DDevice", 1, 43*1024*1024); // Empirical on Vista GF9800x2
#endif
#endif

	for (i = 0; i < SBT_NUM_TYPES; i++)
	{
		D3DFORMAT format = rxbxGetGPUFormat9(rxbxGetTexFormatForSBT(i));
		// Can we use format as surface supporting post-pixel shader ops such as alpha blend?
		if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_SURFACE, format)))
			device->surface_types_post_pixel_supported[i] = RSBS_AsSurfaceWithPostPixOps;
		// as RTT texture?
		if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, format)))
			device->surface_types_post_pixel_supported[i] |= RSBS_AsRTTexturePostPixOps;
		// Can we use as a surface at all, e.g. for a dummy color buffer?
		if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format)))
			device->surface_types_post_pixel_supported[i] |= RSBS_AsSurface;
	}

#if !_XBOX
	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_VOLUMETEXTURE, D3DFMT_DXT5))) {
		// FIXME: Transgaming tells us it supports DXT volume textures, but it really doesn't.
		if(!getIsTransgaming()) {
			device->dxt_volume_tex_supported = true;
		}
	}

	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE, D3DFMT_R32F)))
		device->vfetch_r32f_supported = true;

	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
													D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC)))
		device->nvidia_rawz_supported = true;
	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
													D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC)))
		device->nvidia_intz_supported = true;
	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
													D3DFMT_NULL_TEXTURE_FCC)))
		device->null_supported = true;
	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
													D3DFMT_ATI_DEPTH_TEXTURE_16_FCC)))
		device->ati_df16_supported = true;
	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
													D3DFMT_ATI_DEPTH_TEXTURE_24_FCC)))
		device->ati_df24_supported = true;

	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
													D3DFMT_ATI_FOURCC_RESZ)))
		device->ati_resolve_msaa_z_supported = true;

	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,D3DRTYPE_SURFACE,
													D3DFMT_NVIDIA_ATOC)))
		device->alpha_to_coverage_supported_nv = true; // This is also for Intel

#ifdef MJF_ENABLE_HACKY_ATI_INSTANCING_SUPPORT
	if (systemspecs.videoCardVendorID == VENDOR_ATI)
	{
		// This is a hacky way to enable hardware instancing on
		// the ATI X### series cards, as described at
		// <URL:http://www.gamedev.net/community/forums/topic.asp?topic_id=332289&whichpage=1&#2156112>
		if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_SURFACE,
														  D3DFMT_ATI_INSTANCING_FCC)))
		{
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_POINTSIZE, D3DFMT_ATI_INSTANCING_FCC));
			device->ati_instancing_supported = true;
		}
	}
#endif
#endif

#if !_XBOX
    device->allow_any_size = params->allow_any_size;

	if (SUCCEEDED(hr = IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &AdapterIdentifier)))
		rxbxCheckNVAPI(device, &AdapterIdentifier);

#if !_XBOX
	if (device->bEnableNVStereoscopicAPI)
		rxbxNVInitStereoHandle(device->d3d_device, device->d3d_device_ex, NULL);
#endif

	// check device caps
	CHECKX(IDirect3DDevice9_GetDeviceCaps(device->d3d_device, &rdr_caps_d3d9));
	device->rdr_caps_d3d9_debug = rdr_caps_d3d9; // Save a copy for debugging later

	device->nvidia_csaa_supported = 0;

	for (i = 0; i < SBT_NUM_TYPES; i++)
	{
		D3DFORMAT format = rxbxGetGPUFormat9(rxbxGetTexFormatForSBT(i));
		#if !PLATFORM_CONSOLE
		device->surface_types_nvidia_csaa_supported[i] = RNVCSAA_NotSupported;
		#endif

		if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, format)))
		{
			if (system_specs.videoCardVendorID == VENDOR_ATI && disableMSAAOnATI)
				device->surface_types_multisample_supported[i] = 1;
			else if (rdr_state.disableNonTrivialSurfaceTypes && i!=SBT_RGBA && i!=SBT_RGB16)
				device->surface_types_multisample_supported[i] = 0;
			else if (rdr_state.disableMSAASurfaceTypes)
				device->surface_types_multisample_supported[i] = 1;
			else if (SUCCEEDED(hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, format, !params->fullscreen, D3DMULTISAMPLE_16_SAMPLES, NULL)))
				device->surface_types_multisample_supported[i] = 16;
			else if (SUCCEEDED(hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, format, !params->fullscreen, D3DMULTISAMPLE_8_SAMPLES, NULL)))
				device->surface_types_multisample_supported[i] = 8;
			else if (SUCCEEDED(hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, format, !params->fullscreen, D3DMULTISAMPLE_4_SAMPLES, NULL)))
				device->surface_types_multisample_supported[i] = 4;
			else if (SUCCEEDED(hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, format, !params->fullscreen, D3DMULTISAMPLE_2_SAMPLES, NULL)))
				device->surface_types_multisample_supported[i] = 2;
			else
				device->surface_types_multisample_supported[i] = 1;

			#if !PLATFORM_CONSOLE
			//LM: this comes from http://developer.nvidia.com/object/coverage-sampled-aa.html
			if (system_specs.videoCardVendorID == VENDOR_NV)
			{
				DWORD quality = 0;
				if (SUCCEEDED(hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, format, !params->fullscreen, D3DMULTISAMPLE_4_SAMPLES, &quality)))
				{
					if (quality > 4) 
						device->surface_types_nvidia_csaa_supported[i] |= RNVCSAA_Standard;
				}

				quality = 0;
				if (SUCCEEDED(hr = IDirect3D9_CheckDeviceMultiSampleType(d3d, rdr_caps_d3d9.AdapterOrdinal, rdr_caps_d3d9.DeviceType, format, !params->fullscreen, D3DMULTISAMPLE_8_SAMPLES, &quality)))
				{
					if (quality > 2) 
						device->surface_types_nvidia_csaa_supported[i] |= RNVCSAA_Quality;
				}
				
				if (device->surface_types_nvidia_csaa_supported[i] != RNVCSAA_NotSupported)
					device->nvidia_csaa_supported |= 1;
			}
			#endif
		}
		else
		{
			device->surface_types_multisample_supported[i] = 0;
		}
	}

	if (!(rdr_caps_d3d9.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER))
	{
		//device->unsupported_tex_flags |= RTF_BLACK_BORDER;
	}

	device->rdr_caps_new.features_supported = 0;
	if (device->d3d_device_ex)
		device->rdr_caps_new.features_supported |= FEATURE_D3D9EX;
	if (!(rdr_caps_d3d9.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY))
		device->rdr_caps_new.features_supported |= FEATURE_NONSQUARETEXTURES;
	if (rdr_caps_d3d9.RasterCaps & D3DPRASTERCAPS_ANISOTROPY)
		device->rdr_caps_new.features_supported |= FEATURE_ANISOTROPY;
	if (!(rdr_caps_d3d9.TextureCaps & D3DPTEXTURECAPS_POW2))
		device->rdr_caps_new.features_supported |= FEATURE_NONPOW2TEXTURES;
	if (rdr_caps_d3d9.NumSimultaneousRTs >= 2)
		device->rdr_caps_new.features_supported |= FEATURE_MRT2;
	if (rdr_caps_d3d9.NumSimultaneousRTs >= 4)
		device->rdr_caps_new.features_supported |= FEATURE_MRT4;
	if (rdr_caps_d3d9.VertexShaderVersion >= D3DVS_VERSION(2, 0) && rdr_caps_d3d9.PixelShaderVersion >= D3DPS_VERSION(2, 0))
		device->rdr_caps_new.features_supported |= FEATURE_SM20;
	if (rdr_caps_d3d9.VertexShaderVersion >= D3DVS_VERSION(2, 0) &&
			rdr_caps_d3d9.PixelShaderVersion >= D3DPS_VERSION(2, 0) &&
			rdr_caps_d3d9.PS20Caps.NumTemps >= 32 &&
			!!(rdr_caps_d3d9.PS20Caps.Caps & D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT))
		device->rdr_caps_new.features_supported |= FEATURE_SM2B;
	if (rdr_caps_d3d9.VertexShaderVersion >= D3DVS_VERSION(3, 0) && rdr_caps_d3d9.PixelShaderVersion >= D3DPS_VERSION(3, 0))
		device->rdr_caps_new.features_supported |= FEATURE_SM30;
	if (rdr_caps_d3d9.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)
		device->rdr_caps_new.features_supported |= FEATURE_DX10_LEVEL_CARD;
	if ((device->rdr_caps_new.features_supported & FEATURE_SM30) || device->ati_instancing_supported)
		device->rdr_caps_new.features_supported |= FEATURE_INSTANCING;
	if ((device->rdr_caps_new.features_supported & FEATURE_SM30) && device->vfetch_r32f_supported)
		device->rdr_caps_new.features_supported |= FEATURE_VFETCH;
	if (rxbxTestForDepthSurfaceSupport(device))
		device->rdr_caps_new.features_supported |= FEATURE_DEPTH_TEXTURE;
	if ((device->rdr_caps_new.features_supported & FEATURE_DEPTH_TEXTURE) &&
		(device->nvidia_rawz_supported || device->nvidia_intz_supported || device->ati_df24_supported))
		device->rdr_caps_new.features_supported |= FEATURE_24BIT_DEPTH_TEXTURE;
	if ((device->rdr_caps_new.features_supported & FEATURE_DEPTH_TEXTURE) &&
		(device->nvidia_rawz_supported || device->nvidia_intz_supported))
		device->rdr_caps_new.features_supported |= FEATURE_STENCIL_DEPTH_TEXTURE;
	if (rdr_caps_d3d9.DeclTypes & D3DDTCAPS_FLOAT16_2)
		device->rdr_caps_new.features_supported |= FEATURE_DECL_F16_2;
	if (device->surface_types_multisample_supported[SBT_FLOAT] && device->surface_types_multisample_supported[SBT_RGBA_FLOAT])
		device->rdr_caps_new.features_supported |= FEATURE_SBUF_FLOAT_FORMATS;
	if (device->dxt_volume_tex_supported)
		device->rdr_caps_new.features_supported |= FEATURE_DXT_VOLUME_TEXTURE;
	if (device->nv_api_support | device->ati_resolve_msaa_z_supported)
		device->rdr_caps_new.features_supported |= FEATURE_DEPTH_TEXTURE_MSAA;
	if (device->nvidia_csaa_supported)
		device->rdr_caps_new.features_supported |= FEATURE_NV_CSAA_SURFACE;
	if (rdr_caps_d3d9.MaxPixelShader30InstructionSlots >= 768)
		device->rdr_caps_new.supports_sm30_plus = 1;
	if (rdr_caps_d3d9.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS)
		device->rdr_caps_new.supports_independent_write_masks = 1;

	if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_SURFACE, D3DFMT_X8R8G8B8)) &&
		SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8)))
		device->rdr_caps_new.features_supported |= FEATURE_SRGB;

	device->rdr_caps_new.max_anisotropy = rdr_caps_d3d9.MaxAnisotropy;

	if (system_specs.videoCardVendorID == VENDOR_ATI && (device->rdr_caps_new.features_supported & FEATURE_SM30))
	{
		device->alpha_to_coverage_supported_ati = 1;
	}
	if (system_specs.videoCardVendorID == VENDOR_INTEL && (device->rdr_caps_new.features_supported & FEATURE_SM30) &&
		device->surface_types_multisample_supported[SBT_RGBA] > 1)
	{
		// The NVIDIA device query for "ATOC" should also work here, they say (when we get a driver to test it on)
		device->alpha_to_coverage_supported_ati = 1;
	}


	device->caps_filled = 1;
	
	if (!rxbxSupportsFeature((RdrDevice*)device, FEATURE_DEPTH_TEXTURE))
		rdr_state.lowResAlphaUnsupported = true;
#endif

	rxbxResizeDirect(device, device->device_width, device->device_height, params->refreshRate, params->xpos, params->ypos, params->fullscreen, params->maximize, params->windowed_fullscreen, params->hide);

	if (!device->display_thread.fullscreen && !device->display_thread.windowed_fullscreen)
	{
		device->screen_x_restored = params->xpos;
		device->screen_y_restored = params->ypos;
		device->screen_width_restored = params->width;
		device->screen_height_restored = params->height;
	}

	rxbxResetDeviceState(device);
	rxbxSetStateActive(device, &device->primary_surface.state, 1, device->primary_surface.width_thread, device->primary_surface.height_thread);
	device->primary_surface.state_inited = 1;

#if !_XBOX
	//callback used for Xlive integration
	if ( prxbxCreateDirect_Callback )
	{
		prxbxCreateDirect_Callback(device);
	}
#endif

	rdrUpdateMaterialFeatures(&device->device_base);

	if (!rxbxCompileMinimalVertexShaders(device))
	{
		// alert message fails to display if the window is used
		rxbxSafeShutdownEntireDevice(device, false);
		rdrAlertMsg(&device->device_base, "A required vertex shader failed to compile or load. This failure may be due to "
			"unsupported hardware on your system, in which case this failure may persist "
			"every time you attempt to start the client.");
		return;
	}

	rxbxInitPrimitiveVertexDecls(device);
	rxbxInitSpriteVertexDecl(device);

	if (!rxbxGetPrimarySurfaces( device ))
	{
		// alert message fails to display if the window is used
		rxbxSafeShutdownEntireDevice(device, false);
		rdrAlertMsg(&device->device_base, "Couldn't create primary surface.");
		return;
	}
	
	device->notify_settings_frame_count = 3;
	rxbxDeviceNotifyMainThreadSettingsChanged(device);
}

static float d3dFeatureLevel;
// Overrides the D3D feature level
AUTO_CMD_FLOAT(d3dFeatureLevel, d3dFeatureLevel) ACMD_CMDLINE;
static int d3d11_device_debug = 0;
AUTO_CMD_INT(d3d11_device_debug, d3d11_device_debug) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;


typedef struct RdrDXGIOutputSearch
{
	IDXGIAdapter * pAdapter;
	IDXGIOutput * pOutput;
	DXGI_ADAPTER_DESC adapterDesc;
	DXGI_OUTPUT_DESC outputDesc;
	UINT requestedMonitorNum;
} RdrDXGIOutputSearch;

DXGIEnumResults rxbxFindMonitorEnumDXGIOutputDevicesHandler(IDXGIAdapter * pAdapter, UINT adapterNum, const DXGI_ADAPTER_DESC * pDesc, IDXGIOutput * pOutput, 
	const DXGI_OUTPUT_DESC * pOutputDesc, UINT outputNum, void * pUserData)
{
	RdrDXGIOutputSearch * pSearch = (RdrDXGIOutputSearch*)pUserData;
	U32 outputMonitorIndex = (U32)multimonFindMonitorHMonitor(pOutputDesc->Monitor);
	if (outputMonitorIndex == pSearch->requestedMonitorNum)
	{
		pSearch->pAdapter = pAdapter;
		IDXGIOutput_AddRef(pOutput);
		pSearch->pOutput = pOutput;
		pSearch->adapterDesc = *pDesc;
		pSearch->outputDesc = *pOutputDesc;
		return DXGIEnum_Complete;
	}
	return DXGIEnum_ContinueEnum;
}

__forceinline void rxbxOpenTXAA(RdrDeviceDX *device)
{
	TxaaU4 openResult = TxaaOpenDX(&device->txaa_context,
		device->d3d11_device, device->d3d11_imm_context);
	device->txaa_supported = openResult == TXAA_RETURN_OK;
	memlog_printf(NULL, "Nvidia TXAA open %s", device->txaa_supported ? "succeeded" : "failed");
}

__forceinline void rxbxCloseTXAA(RdrDeviceDX *device)
{
	if (device->txaa_supported)
	{
		TxaaCloseDX(&device->txaa_context);
		device->txaa_supported = 0;
	}
}

static bool rxbxDoesSurfaceTypeSupportMultisampleLevel(ID3D11Device * d3d11_device, 
	DXGI_FORMAT format, UINT multisample_count, UINT *pQuality)
{
	HRESULT hr;
	UINT quality = 0;

	// Per MSDN docs on quality levels and sample patterns (see here:
	// http://msdn.microsoft.com/en-us/library/windows/desktop/ff476218(v=vs.85).aspx)
	// and a reproducible fatal error attempting to create 16x render targets, a success code 
	// with a quality level of zero indicates the format does NOT support multisampling.
	hr = ID3D11Device_CheckMultisampleQualityLevels(d3d11_device, format, multisample_count, &quality);
	*pQuality = quality;

	return SUCCEEDED(hr) && quality;
}

void rxbxCreateDirect11(RdrDeviceDX *device, WindowCreateParams *create_params, WTCmdPacket *packet)
{
	HRESULT hr;
	int i;
	IDXGIAdapter * pAdapterToUse=NULL;
	UINT format_support; // D3D11_FORMAT_SUPPORT enum bitfield
	// Only allow a 10_0 feature level device for now
	F32 default_feature_level = d3dFeatureLevel;
	D3D_FEATURE_LEVEL feature_levels[] =
	{
//		D3D_FEATURE_LEVEL_11_0,
//		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
//		D3D_FEATURE_LEVEL_9_3,
//		D3D_FEATURE_LEVEL_9_2,
//		D3D_FEATURE_LEVEL_9_1,
	};
	int feature_levels_size = ARRAY_SIZE(feature_levels);
	int device_create_flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
	RdrDXGIOutputSearch monitorSearch = { 0 };
	const RdrDeviceInfo * const * device_infos;
	const RdrDeviceInfo * preferred_device_info;
	RdrDeviceModeDX * fullscreen_mode = NULL;
	DisplayParams * params = &create_params->display;
	DXGI_FORMAT buffer_format;

#if !_XBOX
	device->thread_id = GetCurrentThreadId();

	if (system_specs.supportedDXVersion >= 10.0f && !d3dFeatureLevel)
		default_feature_level = system_specs.supportedDXVersion;
	if (default_feature_level)
	{
		feature_levels_size = 1;
		if (nearSameF32(default_feature_level, 9.1))
			feature_levels[0] = D3D_FEATURE_LEVEL_9_1;
		else if (nearSameF32(default_feature_level, 9.2))
			feature_levels[0] = D3D_FEATURE_LEVEL_9_2;
		else if (nearSameF32(default_feature_level, 9.3))
			feature_levels[0] = D3D_FEATURE_LEVEL_9_3;
		else if (nearSameF32(default_feature_level, 10.0))
			feature_levels[0] = D3D_FEATURE_LEVEL_10_0;
		else if (nearSameF32(default_feature_level, 10.1))
			feature_levels[0] = D3D_FEATURE_LEVEL_10_1;
		else if (nearSameF32(default_feature_level, 11.0))
			feature_levels[0] = D3D_FEATURE_LEVEL_11_0;
		else
			FatalErrorf("Invalid -d3dFeatureLevel specified: %1.1f", default_feature_level);
	}

	allocWindowMessageTimers();
#endif

	rxbxInitBackgroundShaderCompile();

	assert(!device->d3d_device);

	// Find appropriate adapter and monitor
	device_infos = rdrEnumerateDevices();
	assert(params->preferred_adapter >= 0 && params->preferred_adapter < eaSize(&device_infos));
	preferred_device_info = device_infos[params->preferred_adapter];
	monitorSearch.requestedMonitorNum = preferred_device_info->monitor_index;
	device->device_info_index = params->preferred_adapter;
	device->device_base.primary_monitor = preferred_device_info->monitor_index;

	triviaPrintf("D3DDeviceType", "%s %.1f", preferred_device_info->type, default_feature_level);

	rxbxEnumDXGIOutputs(rxbxFindMonitorEnumDXGIOutputDevicesHandler, &monitorSearch);
	if (monitorSearch.pOutput)
	{
		IDXGIOutput_Release(monitorSearch.pOutput);
		monitorSearch.pOutput = NULL;
	}

	if (params->fullscreen)
	{
		// cover the normal Windows desktop rectangle of the target monitor,
		// so we don't show a partially-covering black box.
		rwinDisplayParamsSetToCoverMonitor(&device->device_base, preferred_device_info->monitor_handle, params);
		assert(params->preferred_fullscreen_mode >= 0 && params->preferred_fullscreen_mode < eaSize(&preferred_device_info->display_modes));
		fullscreen_mode = (RdrDeviceModeDX *)preferred_device_info->display_modes[params->preferred_fullscreen_mode];

		// Cover the desktop of the target monitor, to prevent partial window draws,
		// flashing, etc.
		params->xpos = monitorSearch.outputDesc.DesktopCoordinates.left;
		params->ypos = monitorSearch.outputDesc.DesktopCoordinates.top;
		params->width = monitorSearch.outputDesc.DesktopCoordinates.right - monitorSearch.outputDesc.DesktopCoordinates.left;
		params->height = monitorSearch.outputDesc.DesktopCoordinates.bottom - monitorSearch.outputDesc.DesktopCoordinates.top;
		device->device_width = fullscreen_mode->base.width;
		device->device_height = fullscreen_mode->base.height;
	}
	else
	{
		if (params->maximize || params->windowed_fullscreen)
			// cover the normal Windows desktop rectangle of the target monitor,
			// so we don't show a partially-covering black box.
			rwinDisplayParamsSetToCoverMonitorForSavedWindow(&device->device_base, params);
		device->device_width = params->width;
		device->device_height = params->height;
	}

	rxbxInitPrimarySurfaceState(device, params->width, params->height);

	device->stPixelShaderCache = stashTableCreateInt(512);
	device->d3d11_state_keys = linAllocCreate(16*1024 - 128, false);
	device->d3d11_rasterizer_states = stashTableCreateFixedSize(16, sizeof(RdrRasterizerState));
	device->d3d11_blend_states = stashTableCreateInt(16);
	device->d3d11_depth_stencil_states = stashTableCreateFixedSize(16, sizeof(RdrDepthStencilState));
	device->d3d11_sampler_states = stashTableCreateInt(16);
	device->d3d11_swap_desc = &device->d3d11_swap_desc_for_debug;


	//assert(!device->hWindow);
	// create window
	if (!device->hWindow)
	{
		// Make a window using the requested parameters
		CreateRenderWindow(device, create_params);
	}

	rxbxPreserveGammaRampDirect(device);

	device->allow_windowed_fullscreen = params->allow_windowed_fullscreen;

	device->primary_surface.width_thread = device->primary_surface.surface_base.width_nonthread = 
		device->primary_surface.surface_base.vwidth_nonthread = device->device_width;
	device->primary_surface.height_thread = device->primary_surface.surface_base.height_nonthread = 
		device->primary_surface.surface_base.vheight_nonthread = device->device_height;
	for (i = 0; i < SBUF_MAXMRT; i ++) {
		device->primary_surface.buffer_types[i] = SBT_RGBA;	// Setting buffer_types explicitly.  From now on, if the surface type is changed, the buffer_types need to be changed with it otherwise an assert will be triggered in rt_xtextures when a debugframe is being grabbed (and perhaps elsewhere).
		if (params->srgbBackBuffer) {
			device->primary_surface.buffer_types[i] |= SBT_SRGB;
		}
	}

	if (fullscreen_mode) {
		buffer_format = fullscreen_mode->dxgi_mode.Format;
	} else {
		buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;
	}

	if (params->srgbBackBuffer) {
		buffer_format = srgbFormat11(buffer_format);
	}

	device->d3d11_swap_desc->BufferDesc.Width = device->device_width;
	device->d3d11_swap_desc->BufferDesc.Height = device->device_height;
	device->d3d11_swap_desc->BufferDesc.Format = buffer_format;
	device->d3d11_swap_desc->BufferDesc.RefreshRate.Numerator = 0;
	device->d3d11_swap_desc->BufferDesc.RefreshRate.Denominator = 0;
	device->d3d11_swap_desc->BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
	device->d3d11_swap_desc->SampleDesc.Count = 1;
	device->d3d11_swap_desc->SampleDesc.Quality = 0;
	device->d3d11_swap_desc->BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	device->d3d11_swap_desc->BufferCount = 1;
	device->d3d11_swap_desc->OutputWindow = device->hWindow;
	device->d3d11_swap_desc->Windowed = TRUE;
	device->d3d11_swap_desc->SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	device->d3d11_swap_desc->Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	device->d3d11_swap_interval = params->vsync?1:0;

	if (params->fullscreen)
	{
		device->d3d11_swap_desc->BufferDesc = fullscreen_mode->dxgi_mode;
		device->d3d11_swap_desc->BufferDesc.Format = buffer_format;
		device->d3d11_swap_desc->SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		device->d3d11_swap_desc->Windowed = FALSE;
	}

	if (d3d11_device_debug)
		device_create_flags |= D3D11_CREATE_DEVICE_DEBUG;

	{
		IDXGIFactory * pFactory = NULL;
		HRESULT hrCreateDXGIFactory = S_OK, hrEnumAdapter = S_OK;

		hrCreateDXGIFactory = CreateDXGIFactory(&IID_IDXGIFactory ,(void**)&pFactory);
		if(FAILED(hrCreateDXGIFactory))
		{
			memlog_printf(NULL, "CreateDXGIFactory failed %s (0x%x)", 
				rxbxGetStringForHResult(hrCreateDXGIFactory), hrCreateDXGIFactory);
			rdrAlertMsg(&device->device_base, "Couldn't access DXGI Factory.");
			return;
		}

		hrEnumAdapter = IDXGIFactory_EnumAdapters(pFactory, preferred_device_info->adapter_index, &pAdapterToUse);
		IDXGIFactory_Release(pFactory);
		pFactory = NULL;

		if(FAILED(hrEnumAdapter))
		{
			memlog_printf(NULL, "EnumAdapters failed %s (0x%x)", 
				rxbxGetStringForHResult(hrEnumAdapter), hrEnumAdapter);
			rdrAlertMsg(&device->device_base, "Couldn't access DXGI adapter.");
			return;
		}
	}

	device->bIsDuringCreateDevice = 1;
	rxbxLogSwapChainParams(device, "Call CreateDeviceAndSwapChain with ", device->d3d11_swap_desc);

	FP_NO_EXCEPTIONS_BEGIN;
		hr = D3D11CreateDeviceAndSwapChain(
			pAdapterToUse,
			pAdapterToUse?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,
			NULL,
			device_create_flags,
			feature_levels,
			feature_levels_size,
			D3D11_SDK_VERSION,
			device->d3d11_swap_desc,
			&device->d3d11_swapchain,
			&device->d3d11_device,
			&device->d3d11_feature_level,
			&device->d3d11_imm_context
			);
	FP_NO_EXCEPTIONS_END;

	device->bIsDuringCreateDevice = 0;
	if (pAdapterToUse)
		IDXGIAdapter_Release(pAdapterToUse);
	if (FAILED(hr))
	{
		char failureMessage[ 256 ];
		sprintf(failureMessage, "Couldn't create D3D device. D3D11CreateDeviceAndSwapChain failed %s (0x%x)", 
			rxbxGetStringForHResult(hr), hr);
		memlog_printf(NULL, "%s", failureMessage);
		DestroyWindow(device->hWindow); // alert message fails to display if the window is used
		device->hWindow = NULL;
		device->d3d11_device = NULL;
		rdrAlertMsg(&device->device_base, failureMessage);
		return;
	}
	rxbxLogSwapChainParams(device, "CreateDeviceAndSwapChain done ", device->d3d11_swap_desc);
	rxbxInitConstantBuffers(device);

	// TODO can we get the window tracking to get this data precisely?
	if (params->fullscreen)
	{
		// Device create with fullscreen can change desktop monitor rectangle layout
		// so find the new rectangle
		rwinDisplayParamsSetToCoverMonitorForDeviceWindow(&device->device_base, device->hWindow, params);
		rxbxClipCursorToFullscreen(device);
	}
	device->display_thread = *params;

	memMonitorTrackUserMemory("D3D", 1, 3000*1024, 1);

	// This doesn't work on D3D11, and doesn't really do anything for 9 anyway
	//nvapiRegister((IUnknown*)device->d3d11_device);
#if RDR_NVIDIA_TXAA_SUPPORT
	if (device->d3d11_feature_level >= D3D_FEATURE_LEVEL_11_0) {
		rxbxOpenTXAA(device);
	}
#endif

	SET_FP_CONTROL_WORD_DEFAULT;

	if (IsUsingVista())
		rdrTrackUserMemoryDirect(&device->device_base, "D3DDevice", 1, 43*1024*1024); // Empirical on Vista GF9800x2

	if (SUCCEEDED(hr = ID3D11Device_CheckFormatSupport(device->d3d11_device, DXGI_FORMAT_BC3_UNORM, &format_support)) &&
		(format_support & D3D11_FORMAT_SUPPORT_TEXTURE3D))
		device->dxt_volume_tex_supported = true;

	// Unknown how to query about vertex texture fetch
	//if (SUCCEEDED(hr = IDirect3D9_CheckDeviceFormat(d3d, AdapterToUse, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE, D3DFMT_R32F)))
	//	device->vfetch_r32f_supported = true;

	device->nvidia_rawz_supported = false;
	device->nvidia_intz_supported = false;
	device->null_supported = false;
	device->ati_df16_supported = false;
	device->ati_df24_supported = false;
	device->ati_resolve_msaa_z_supported = false;

	device->allow_any_size = params->allow_any_size;

	// check device caps
	device->nvidia_csaa_supported = 0;

	for (i = 0; i < SBT_NUM_TYPES; i++)
	{
		DXGI_FORMAT format = rxbxGetGPUFormat11(rxbxGetTexFormatForSBT(i), false);
		device->surface_types_nvidia_csaa_supported[i] = RNVCSAA_NotSupported;
		device->surface_types_post_pixel_supported[i] = 0;

		if (SUCCEEDED(hr = ID3D11Device_CheckFormatSupport(device->d3d11_device, format, &format_support)))
		{
			if (format_support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
			{
				UINT quality;

				// Can we use format as surface supporting post-pixel shader ops such as alpha blend?
				if (format_support & D3D11_FORMAT_SUPPORT_BLENDABLE)
				{
					device->surface_types_post_pixel_supported[i] |= RSBS_AsSurfaceWithPostPixOps;
					// as RTT texture?
					if (format_support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)
						device->surface_types_post_pixel_supported[i] |= RSBS_AsRTTexturePostPixOps;
				}
				// Can we use as a surface at all, e.g. for a dummy color buffer? - checked with render_target above? or is there some other flag?
				device->surface_types_post_pixel_supported[i] |= RSBS_AsSurface;

				if (system_specs.videoCardVendorID == VENDOR_ATI && disableMSAAOnATI)
					device->surface_types_multisample_supported[i] = 1;
				else if (rdr_state.disableNonTrivialSurfaceTypes && i!=SBT_RGBA && i!=SBT_RGB16)
					device->surface_types_multisample_supported[i] = 0;
				else if (rdr_state.disableMSAASurfaceTypes || !(format_support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET))
					device->surface_types_multisample_supported[i] = 1;
				else if (rxbxDoesSurfaceTypeSupportMultisampleLevel(device->d3d11_device, format, 16, &quality))
					device->surface_types_multisample_supported[i] = 16;
				else if (rxbxDoesSurfaceTypeSupportMultisampleLevel(device->d3d11_device, format, 8, &quality))
					device->surface_types_multisample_supported[i] = 8;
				else if (rxbxDoesSurfaceTypeSupportMultisampleLevel(device->d3d11_device, format, 4, &quality))
					device->surface_types_multisample_supported[i] = 4;
				else if (rxbxDoesSurfaceTypeSupportMultisampleLevel(device->d3d11_device, format, 2, &quality))
					device->surface_types_multisample_supported[i] = 2;
				else
					device->surface_types_multisample_supported[i] = 1;

				if (device->surface_types_multisample_supported[i] > 1)
				{
					//LM: this comes from http://developer.nvidia.com/object/coverage-sampled-aa.html
					if (system_specs.videoCardVendorID == VENDOR_NV)
					{
						if (rxbxDoesSurfaceTypeSupportMultisampleLevel(device->d3d11_device, format, 4, &quality))
						{
							if (quality > 8) 
								device->surface_types_nvidia_csaa_supported[i] |= RNVCSAA_Standard;
						}

						quality = 0;
						if (rxbxDoesSurfaceTypeSupportMultisampleLevel(device->d3d11_device, format, 8, &quality))
						{
							if (quality > 16) 
								device->surface_types_nvidia_csaa_supported[i] |= RNVCSAA_Quality;
						}

						if (device->surface_types_nvidia_csaa_supported[i] != RNVCSAA_NotSupported)
							device->nvidia_csaa_supported |= 1;
					}
				}
			}
		}
		else
		{
			device->surface_types_multisample_supported[i] = 0;
		}
	}

	// Fill in caps
	device->rdr_caps_new.features_supported = 0;

	if (SUCCEEDED(hr = ID3D11Device_CheckFormatSupport(device->d3d11_device, DXGI_FORMAT_D16_UNORM, &format_support)))
	{
		if ((format_support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) && (format_support & D3D11_FORMAT_SUPPORT_TEXTURE2D))
		{
			device->rdr_caps_new.features_supported |= FEATURE_DEPTH_TEXTURE;
		}
	}

	if (SUCCEEDED(hr = ID3D11Device_CheckFormatSupport(device->d3d11_device, DXGI_FORMAT_D24_UNORM_S8_UINT, &format_support)))
	{
		if ((format_support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) && (format_support & D3D11_FORMAT_SUPPORT_TEXTURE2D))
		{
			device->rdr_caps_new.features_supported |= FEATURE_DEPTH_TEXTURE|FEATURE_24BIT_DEPTH_TEXTURE|FEATURE_STENCIL_DEPTH_TEXTURE;
		}
	}

	device->rdr_caps_new.features_supported |= FEATURE_NONSQUARETEXTURES;
	device->rdr_caps_new.features_supported |= FEATURE_ANISOTROPY;
	device->rdr_caps_new.features_supported |= FEATURE_NONPOW2TEXTURES; // Might no longer be supported on DX9 hardware through DX11?
	device->rdr_caps_new.features_supported |= FEATURE_DX11_RENDERER;
	device->rdr_caps_new.features_supported |= FEATURE_SM20;
	device->rdr_caps_new.features_supported |= FEATURE_SRGB;

	if (device->d3d11_feature_level >= D3D_FEATURE_LEVEL_9_3)
		device->rdr_caps_new.features_supported |= FEATURE_MRT2|FEATURE_MRT4;
	if (device->txaa_supported)
		device->rdr_caps_new.features_supported |= FEATURE_TXAA;

	// For features that are d3d level dependent and are still compatible with every level that came after it because all cases are expected to fall through.
	switch(device->d3d11_feature_level){
	case D3D_FEATURE_LEVEL_11_0:
		device->rdr_caps_new.features_supported |= FEATURE_TESSELLATION;
	case D3D_FEATURE_LEVEL_10_1:
		device->rdr_caps_new.features_supported |= FEATURE_DEPTH_TEXTURE_MSAA;
	case D3D_FEATURE_LEVEL_10_0:
		device->rdr_caps_new.features_supported |= FEATURE_DX10_LEVEL_CARD;
	case D3D_FEATURE_LEVEL_9_3:
		device->rdr_caps_new.features_supported |= FEATURE_SM2B|FEATURE_SM30|FEATURE_INSTANCING|FEATURE_DECL_F16_2;
		device->rdr_caps_new.supports_independent_write_masks = 1;
	}

	//if (device->d3d11_feature_level >= D3D_FEATURE_LEVEL_9_2) // Not sure this is right
	//	device->rdr_caps_new.features_supported |= FEATURE_SM2A;
	if (device->surface_types_multisample_supported[SBT_FLOAT] && device->surface_types_multisample_supported[SBT_RGBA_FLOAT])
		device->rdr_caps_new.features_supported |= FEATURE_SBUF_FLOAT_FORMATS;
	if (device->dxt_volume_tex_supported)
		device->rdr_caps_new.features_supported |= FEATURE_DXT_VOLUME_TEXTURE;
	if (device->nvidia_csaa_supported)
		device->rdr_caps_new.features_supported |= FEATURE_NV_CSAA_SURFACE;


	assert(device->d3d11_feature_level >= D3D_FEATURE_LEVEL_10_0);
	// The things below here are supported on some D3D9 cards, and we really need them if we are going to run correctly on those cards, so they should just use the D3D9 renderer.

	if (device->d3d11_feature_level >= D3D_FEATURE_LEVEL_10_0)
	{
		device->rdr_caps_new.supports_sm30_plus = 1;
		device->rdr_caps_new.features_supported |= FEATURE_VFETCH;
	}

	device->rdr_caps_new.max_anisotropy = (device->d3d11_feature_level > D3D_FEATURE_LEVEL_9_1)?16:2;

	device->caps_filled = 1;

	if (!rxbxCompileMinimalVertexShaders(device))
	{
		DestroyWindow(device->hWindow); // alert message fails to display if the window is used
		device->hWindow = NULL;
		ID3D11Device_Release(device->d3d11_device);
		device->d3d11_device = NULL;
		rdrAlertMsg(&device->device_base, "A required vertex shader failed to compile or load. This failure may be due to "
			"unsupported hardware on your system, in which case this failure may persist "
			"every time you attempt to start the client.");
		return;
	}

	rxbxInitPrimitiveVertexDecls(device);
	rxbxInitSpriteVertexDecl(device);

	if (!rxbxGetPrimarySurfaces( device ))
	{
		rdrAlertMsg(&device->device_base, "Couldn't create primary surface.");
		if (device->d3d11_device)
		{
			ID3D11Device_Release(device->d3d11_device);
			device->d3d11_device = NULL;
		}
		return;
	}

	if (!rxbxSupportsFeature((RdrDevice*)device, FEATURE_DEPTH_TEXTURE))
		rdr_state.lowResAlphaUnsupported = true;

	rxbxResizeDirect(device, device->device_width, device->device_height, params->refreshRate, params->xpos, params->ypos, params->fullscreen, params->maximize, params->windowed_fullscreen, params->hide);

	if (!device->display_thread.fullscreen && !device->display_thread.windowed_fullscreen)
	{
		device->screen_x_restored = params->xpos;
		device->screen_y_restored = params->ypos;
		device->screen_width_restored = params->width;
		device->screen_height_restored = params->height;
	}

	rxbxResetDeviceState(device);
	rxbxSetStateActive(device, &device->primary_surface.state, 1, device->primary_surface.width_thread, device->primary_surface.height_thread);
	device->primary_surface.state_inited = 1;

	rxbxCheckNVAPI11(device);
	if (device->bEnableNVStereoscopicAPI)
		rxbxNVInitStereoHandle(NULL, NULL, device->d3d11_device);

#if !_XBOX
	//callback used for Xlive integration
	if ( prxbxCreateDirect_Callback )
	{
		prxbxCreateDirect_Callback(device);
	}
#endif

	rdrUpdateMaterialFeatures(&device->device_base);

	device->notify_settings_frame_count = 3;
	rxbxDeviceNotifyMainThreadSettingsChanged(device);
}

void rxbxFlushGPUDirectEx(RdrDeviceDX *device, bool allowSleep)
{
	BOOL bResult;
	if (device->flush_query.typeless)
	{
		HRESULT hrQueryGetData;
		rxbxQueryEnd(device, device->flush_query);

		while (S_FALSE == (hrQueryGetData = rxbxQueryGetData(device, device->flush_query, &bResult, sizeof(bResult), true))) {
			TRACE_QUERY(&device->device_base, "GPU flush GetData == 0x%08x %p %d\n", hrQueryGetData, device->flush_query.typeless, bResult);
			if (allowSleep && !rdr_state.noSleepWhileWaitingForGPU)
				Sleep(1);
		}
		TRACE_QUERY(&device->device_base, "GPU flush GetData == 0x%08x %p %d\n", hrQueryGetData, device->flush_query.typeless, bResult);
	}
}

void rxbxFlushGPUDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	rxbxFlushGPUDirectEx(device, true);
}



// rxbxFreeAllTexturesDirect calls this after deleting all device textures,
// to clear the surface texture pointers. Otherwise the device has dangling
// pointers.
void rxbxDeviceDetachDeadSurfaceTextures(RdrDeviceDX *device)
{
	int surfaceIndex, countSurfaces = eaSize( &device->surfaces ), i;

	for ( surfaceIndex = 0; surfaceIndex < countSurfaces; ++surfaceIndex )
	{
		int bufferIndex;
		RdrSurfaceDX * surface = device->surfaces[ surfaceIndex ];
		for ( bufferIndex = 0; bufferIndex < ARRAY_SIZE(surface->rendertarget); ++bufferIndex )
		{
			for (i=surface->rendertarget[bufferIndex].texture_count-1; i>=0; i--) 
			{
				ZeroStructForce(&surface->rendertarget[ bufferIndex ].textures[i].tex_handle);
				surface->rendertarget[ bufferIndex ].textures[i].d3d_texture.typeless = NULL;
				surface->rendertarget[ bufferIndex ].textures[i].d3d_buffer.typeless = NULL;
			}
		}
	}
}

static void freeVBOChunk(RdrVBOMemoryDX *vbo_chunk)
{
	if (!vbo_chunk)
		return;

	PERFINFO_AUTO_START("Freeing VBO chunk", 1);
	rxbxNotifyVertexBufferFreed(vbo_chunk->device, vbo_chunk->vbo);
#if _XBOX
	IDirect3DVertexBuffer9_BlockUntilNotBusy(vbo_chunk->vbo);
#endif
	rxbxReleaseVertexBuffer(vbo_chunk->device, vbo_chunk->vbo);
	rdrTrackUserMemoryDirect(&vbo_chunk->device->device_base, "VideoMemory:TempVBO", 1, -(S32)vbo_chunk->size_bytes);
	free(vbo_chunk);
	PERFINFO_AUTO_STOP();
}

static void freeIBOChunk(RdrIBOMemoryDX *ibo_chunk)
{
	if (!ibo_chunk)
		return;

	PERFINFO_AUTO_START("Freeing IBO chunk", 1);
	rxbxNotifyIndexBufferFreed(ibo_chunk->device, ibo_chunk->ibo);
#if _XBOX
	IDirect3DIndexBuffer9_BlockUntilNotBusy(ibo_chunk->ibo);
#endif
	rxbxReleaseIndexBuffer(ibo_chunk->device, ibo_chunk->ibo);
	rdrTrackUserMemoryDirect(&ibo_chunk->device->device_base, "VideoMemory:TempIBO", 1, -(S32)ibo_chunk->size_bytes);
	free(ibo_chunk);
	PERFINFO_AUTO_STOP();
}

static void rxbxReleaseQuadIndexBufferDirect(RdrDeviceDX *device)
{
	if (device->quad_index_list_ibo.typeless_index_buffer)
	{
		rxbxReleaseIndexBuffer(device, device->quad_index_list_ibo);
		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:QuadIBO", 1, -(S32)device->quad_index_list_count*6*sizeof(U16));
		device->quad_index_list_ibo.typeless_index_buffer = NULL;
		device->quad_index_list_count = 0;
	}
}

static void rxbxReleaseDeviceSecondaryDataDirect(RdrDeviceDX *device)
{
	int i;
	rxbxResetDeviceState( device );

	// Set a flag causing the cursor cache to be cleared (in the main thread) before next use
	// Can't clear hashtable here, we're in the wrong thread
	device->reset_all_cursors = 1;
	device->last_set_cursor = 0;

	rxbxFreeAllNonManagedTexturesDirect(device);
	rxbxFMVReleaseAllForResetDirect(device);

	//LDM: this isn't required as far as I can tell and it causes problems if commands which depend on current_state not being null
	//rxbxSetSurfaceActiveDirect( device, NULL ); 

	if (device->primary_surface_mem_usage[0])
	{
		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:Backbuffer", 1, -(S32)device->primary_surface_mem_usage[0]);
		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:Depthbuffer", 1, -(S32)device->primary_surface_mem_usage[1]);
		device->primary_surface_mem_usage[0] = 0;
		device->primary_surface_mem_usage[1] = 0;
	}
	rxbxSurfaceCleanupDirect(device, &device->primary_surface, NULL);
	rxbxDestroyAllSecondarySurfacesDirect( device );

	rxbxDestroyQueries(device);

	eaDestroyEx(&device->vbo_memory, freeVBOChunk);
	eaDestroyEx(&device->ibo_memory, freeIBOChunk);
	eaDestroyEx(&device->ibo32_memory, freeIBOChunk);

	for (i = 0; i < ARRAY_SIZE(device->sprite_index_buffer_info); ++i)
		rxbxFreeSpriteIndexBuffer(device, i);

	rxbxReleaseQuadIndexBufferDirect(device);
}

void rxbxHandleLostDeviceDirect(RdrDeviceDX *device)
{
	// We lost the device. We must release all 
	// rendering surfaces. We also reset texture state
	// to put the surfaces back into a known state to allow
	// us to safely restore when we get the device back
	int was_device_invalidated = device->isLost == RDR_LOST_DRIVER_INTERNAL_ERROR;

	assert(device->d3d_device); // Only on DX9?

	memlog_printf(0, "D3D Device lost Device:0x%08p ActiveSurface:0x%08p", device, device->active_surface);

	rxbxSetDeviceLost(device, RDR_LOST_FOCUS);

#if LOG_RESOURCE_USAGE
	rxbxDumpMemInfo("before release device data");
#endif

	rxbxReleaseDeviceSecondaryDataDirect(device);

#if LOG_RESOURCE_USAGE
	rxbxDumpMemInfo("after release device data");
	rxbxDumpDeviceState(device, -1, false);
#endif

#if !PLATFORM_CONSOLE
	// commit all state-managed-states
	if (device->caps_filled)
		rxbxApplyStatePreDraw(device);
#endif

#if !_XBOX && 0
	// DJR I disabled this code because we currently can't 
	// release & recreate all pixel shaders & textures, so
	// we can't tear down and reconstruct the device.
	if (was_device_invalidated)
	{
		WindowCreateParams params = { 0 };
		int device_ref_count = 0;
		params.width = device->d3d_present_params->BackBufferWidth;
		params.height = device->d3d_present_params->BackBufferHeight;

		rxbxFreeAllTexturesDirect(device, NULL, NULL);

		memlog_printf(NULL, "%s", "rxbxReactivate():DeviceDriverInternalError");
		// we must recreate the device
		device_ref_count = IDirect3DDevice9_Release(device->d3d_device);
		memlog_printf(NULL, "rxbxReactivate(): device ref count after Release %d\n", device_ref_count);
		device->d3d_device = NULL;

		rxbxCreateDirect(device, &params, NULL);
	}
#endif

	if( device->device_base.gfx_lib_device_lost_cb )
		TimedCallback_Run( (TimedCallbackFunc)device->device_base.gfx_lib_device_lost_cb, NULL, 0 );
}

//callback used for Xlive integration
rxbxPresentDirect_Callback prxbxPresentDirect_Callback = NULL;

void Set_rxbxPresentDirect_Callback( rxbxPresentDirect_Callback pCallbackRtn )
{
	prxbxPresentDirect_Callback = pCallbackRtn;
}

static void checkFrameRateStabilizer(RdrDeviceDX *device, int location)
{
#if !PLATFORM_CONSOLE
	HDC hdc;
	int state_location = rdr_state.frameRateStabilizer % 10;
	int state_value = (rdr_state.frameRateStabilizer / 10) % 10;
	int state_flag = rdr_state.frameRateStabilizer / 100;
	static int value_checker=0;
	if (!rdr_state.frameRateStabilizer)
		return;
	if (state_location != location)
		return;
	if (!state_value)
		state_value = 1;
	value_checker++;
	if (value_checker >= state_value) {
		value_checker = 0;
	} else {
		return; // Skip this frame
	}

	PERFINFO_AUTO_START_FUNC();

	if (state_flag == 0) {
		PERFINFO_AUTO_START("Flush GPU", 1);
		rxbxFlushGPUDirect(device, NULL, NULL);
		PERFINFO_AUTO_STOP();
	} else {
		PERFINFO_AUTO_START("GetPixel", 1);
		// Grab pixel from desktop
		hdc = GetDC(NULL); // Get the DrawContext of the desktop
		if (hdc) {
			COLORREF cr;
			POINT pCursor;
			pCursor.x = device->primary_surface.width_thread / 2;
			pCursor.y = device->primary_surface.height_thread / 2;
			if (ClientToScreen(device->hWindow, &pCursor)) {
				// Note: it's perfectly valid to have negative coordinates here!
				cr = GetPixel(hdc, pCursor.x, pCursor.y);
			}
			ReleaseDC(NULL, hdc);
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
#endif
}

__forceinline static bool rxbxIsFatalGetDataResult(HRESULT hrGetData)
{
	return FAILED(hrGetData) && hrGetData != D3DERR_DEVICELOST && hrGetData != S_FALSE && hrGetData != E_FAIL;
}

// analysis is nuts
#pragma warning(push)
#pragma warning(disable:6386)
static void rxbxCheckForQueriesCompleted(RdrDeviceDX *device, int flushIndex)
{
	int i;
	{
		const int iBufferSize=ARRAY_SIZE(device->sync_query);
		for (i=0; i<iBufferSize; i++)
		{
			const int iRingBufferPos = (device->sync_query_index+i)%iBufferSize;
			if (device->sync_query[iRingBufferPos].bIssued) {
				HRESULT hrGetData;
				BOOL bResult = false;
				hrGetData = rxbxQueryGetData(device, device->sync_query[iRingBufferPos].query, &bResult, sizeof(bResult),
					(iRingBufferPos==flushIndex));
				TRACE_QUERY_EX(&device->device_base, 
					device->sync_query[iRingBufferPos].bSyncStuck || (FAILED(hrGetData) && S_FALSE != hrGetData), 
					"Sync query %d 0x%p GetData == 0x%08x %d %s %u\n", iRingBufferPos, device->sync_query[iRingBufferPos].query.typeless, hrGetData, bResult, (iRingBufferPos==flushIndex) ? "F" : "NF",
					device->sync_query[iRingBufferPos].frameStuck);
				if (rxbxIsFatalGetDataResult(hrGetData))
				{
					rxbxFatalHResultErrorf(device, hrGetData, "checking sync query", 
						"Sync query %d 0x%p GetData == 0x%08x %d %s %u\n", iRingBufferPos, 
						device->sync_query[iRingBufferPos].query.typeless, hrGetData, bResult, 
						(iRingBufferPos==flushIndex) ? "F" : "NF",
						device->sync_query[iRingBufferPos].frameStuck);
				}
				// completion (S_OK) or non-fatal failure codes clear the issued state; the failure codes
				// cause us to abandon the query
				if (S_FALSE != hrGetData)
				{
					device->sync_query[iRingBufferPos].bIssued = false;
					device->sync_query[iRingBufferPos].bSyncStuck = false;
					device->sync_query[iRingBufferPos].frameStuck = 0;
				}
			}
		}
	}

	if (device->bProfilingQueries && device->iNumWaitingQueries)
	{
		// check my disjoint marker
		HRESULT hrGetData;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data;
		F32 fMsPerCycle;
		const int iBufferSize=ARRAY_SIZE(device->profiling_query);
		if (device->d3d11_device)
		{
			hrGetData = rxbxQueryGetData(device, device->disjoint_query[device->iFirstWaitingDisjointQuery], &data, sizeof(data), false);
		}
		else
		{
			data.Frequency = 100000000;
			hrGetData = rxbxQueryGetData(device, device->disjoint_query[device->iFirstWaitingDisjointQuery], &data.Disjoint, sizeof(data.Disjoint), false);
		}
		if (S_FALSE != hrGetData)
		{
			/*static int iFrames = 0;
			bool bDoIt = false;
			iFrames++;
			if (iFrames == 300)
			{
				iFrames = 0;
				bDoIt = true;
			}*/
 			//devassert(hrGetData == S_OK);
			device->iFirstWaitingDisjointQuery = (device->iFirstWaitingDisjointQuery+1)%ARRAY_SIZE(device->disjoint_query);
			device->iNumWaitingQueries--;
			fMsPerCycle = 1000.f/data.Frequency;
			for (i=0; i<iBufferSize; i++)
			{
				const int iRingBufferPos = (device->profiling_query_index+i)%iBufferSize;
				const int iPreviousPos = (iRingBufferPos+iBufferSize-1)%iBufferSize;
				RxbxProfilingQuery * pLastQuery = &device->profiling_query[iPreviousPos];
				RxbxProfilingQuery * pQuery = &device->profiling_query[iRingBufferPos];
				if (pQuery->bIssued) {
					hrGetData = rxbxQueryGetData(device, pQuery->query, &pQuery->uTimeStamp, sizeof(pQuery->uTimeStamp), false);
					TRACE_QUERY(&device->device_base, "Profiling query %d 0x%p GetData == 0x%08x %"FORM_LL"u\n", iRingBufferPos, pQuery->query.typeless, hrGetData, pQuery->uTimeStamp);
					if (S_FALSE != hrGetData)
					{
						pQuery->bIssued = false;
					}
					else
					{
						// try again later
						break;
					}

					if (!data.Disjoint && pLastQuery->iTimerIdx != -1)
					{
						UINT64 uDiff = pQuery->uTimeStamp-pLastQuery->uTimeStamp;
						rdrGfxPerfCounts_Current.afTimers[pLastQuery->iTimerIdx] += uDiff*fMsPerCycle;
						//if (bDoIt)
							//printf("Timer %d: %f\n",pLastQuery->iTimerIdx,uDiff*fMsPerCycle);
					}
					else
					{
						rdrGfxPerfCounts_Last = rdrGfxPerfCounts_Current;
						memset(&rdrGfxPerfCounts_Current,0,sizeof(rdrGfxPerfCounts_Current));
					}
				}
			}
		}
	}
}
#pragma warning(pop)

static int bDebugDumpDeviceStateAfterPresent = 0;
AUTO_CMD_INT(bDebugDumpDeviceStateAfterPresent,bDebugDumpDeviceStateAfterPresent);

static void rxbxFatalDriverInternalError(void)
{
	triviaPrintf("CrashWasVideoRelated", "1");
	if (system_specs.isRunningNortonAV && system_specs.videoCardVendorID == VENDOR_NV)
	{
		FatalErrorf( "The graphics device or driver encountered an unrecoverable internal error and must exit. "
			"Please restart the application. If the error persists restart the computer. "
			"This issue can be caused by an incompatibility between your currently installed anti-virus"
			" software and the NVIDIA graphics drivers, and may be fixed by using a"
			" different anti-virus solution.");
	} else {
		FatalErrorf( "The graphics device or driver encountered an unrecoverable internal error and must exit. "
			"Please restart the application. If the error persists restart the computer." );
	}
}

static void rxbxClearAllBuffers(RdrDeviceDX *device)
{
	RdrClearParams params;
	RdrSurfaceDX *oldsurface = device->active_surface;
	params.bits = CLEAR_ALL;
	setVec4(params.clear_color, 1, 0, 1, 1);
	params.clear_depth = 0.5;

	FOR_EACH_IN_EARRAY(device->surfaces, RdrSurfaceDX, surface)
	{
		RdrSurfaceActivateParams active_params = { &surface->surface_base, 0 };
		Vec4 saved_viewport;
		active_params.write_mask = ~0;
		copyVec4(surface->state.viewport, saved_viewport);
		setVec4(surface->state.viewport, 0, 1, 0, 1);
		rxbxSetSurfaceActiveDirect(device, &active_params);
		rxbxClearActiveSurfaceDirect(device, &params, NULL);
		copyVec4(saved_viewport, surface->state.viewport);
		rxbxResetViewport(device, surface->width_thread, surface->height_thread);
	}
	FOR_EACH_END;
	rxbxSetSurfaceActiveDirectSimple(device, oldsurface);
}

int rxbxIssueProfilingQuery(RdrDeviceDX *device, int iTimerIdx)
{
	int index = device->profiling_query_index;

	if (device->bProfilingQueries) { // Old cards will not have any queries; also feature must be enabled
		RxbxProfilingQuery * pQuery = &device->profiling_query[index];
		if (pQuery->bIssued)
		{
			// the buffer is full for some reason.  Just going to bail until I figure out why this would happen.
			return -1;
		}
		assert(!pQuery->bIssued);

		// The block that is starting here (has to be one or the other, just a convention)
		pQuery->iTimerIdx = iTimerIdx;

		TRACE_QUERY(&device->device_base, "Profiling timestamp %p %d\n", pQuery->query.typeless, index);

		// Issue an event so we know when this has made it through the GPU pipeline (timestamps don't use Begin, so we just end)
		rxbxQueryEnd(device, pQuery->query);

		pQuery->bIssued = true;

		device->profiling_query_index++;
		device->profiling_query_index%=ARRAY_SIZE(device->profiling_query);
	}

	return index;
}

int rxbxIssueSyncQuery(RdrDeviceDX *device)
{
	int index = device->sync_query_index;

	if (device->sync_query[index].query.typeless) { // Old cards will not have any queries
		if(device->sync_query[index].bIssued)
			// We've run out of frame sync queries in the ring buffer, so perhaps we have the main
			// thread running too far ahead of the render thread, say due to some frames that are
			// taking a long time on the GPU. Since the Present code will have stopped waiting for 
			// the frame, we should abandon the query and move on.
			TRACE_QUERY_EX(&device->device_base, true, "Sync query abandoned; reissuing %p %d %d %d\n", 
				device->sync_query[index].query.typeless, index, 
				device->sync_query[index].bSyncStuck, device->sync_query[index].frameStuck);

		TRACE_QUERY(&device->device_base, "Sync query end %p %d\n", device->sync_query[index].query.typeless, index);
		// Issue an event so we know when this has made it through the GPU pipeline
		rxbxQueryEnd(device, device->sync_query[index].query);

		device->sync_query[index].bIssued = true;

		device->sync_query_index++;
		device->sync_query_index%=ARRAY_SIZE(device->sync_query);
	}

	return index;
}

static int sync_wait_iteration_count;
static int rdr_debug_max_sync_waits = 180;
AUTO_CMD_INT(rdr_debug_max_sync_waits,rdr_debug_max_sync_waits) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(DEBUG);
static int SYNC_ITERATIONS_BEFORE_ERRORF = 160;

static void rxbxApplyDeviceSettingChanges(RdrDeviceDX *device)
{
	if (device->device_base.title_need_update)
	{
		wchar_t wbuffer[256];
		device->device_base.title_need_update = 0;
		UTF8ToWideStrConvert(device->device_base.current_title, wbuffer, ARRAY_SIZE(wbuffer));
		SetWindowTextW(device->hWindow, wbuffer);
	}

	if (device->device_base.thread_cursor_needs_refresh)
	{
		device->device_base.thread_cursor_needs_refresh = 0;
		rxbxRefreshCursorStateDirect( device );
	}
}

static void rxbxPresentDirect(RdrDeviceDX *device)
{
	HRESULT nPresentCode;

	if (!device)
		return;

	PERFINFO_AUTO_START_FUNC();

	etlAddEvent(device->device_base.event_timer, "Buffer swap", ELT_CODE, ELTT_BEGIN);

	CHECKDEVICELOCK(device);

	if (device->d3d_device || device->d3d11_device)
	{
#if !PLATFORM_CONSOLE
		assert(!device->in_scene);
		device->after_scene_before_present = 1;
		// Don't process messages during the frame: starting at BeginScene, until after Present
		if (!rdr_state.bProcessMessagesOnlyBetweenFrames)
			rxbxProcessWindowsMessages2Direct(device);
#endif

		PERFINFO_AUTO_START("top", 1);{
		PERFINFO_AUTO_START("checkFrameRateStabilizer", 1);
		checkFrameRateStabilizer(device, 1);
		PERFINFO_AUTO_STOP();
		
		PERFINFO_AUTO_START("rxbxSetDefaultGPRAllocation", 1);
		rxbxSetDefaultGPRAllocation(device);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("rxbxDisownManagedParams", 1);
		rxbxDisownManagedParams(device);
		PERFINFO_AUTO_STOP();

		if (rdr_state.perFrameSleep)
		{
			int amount = rdr_state.perFrameSleep-1;
			PERFINFO_AUTO_START_BLOCKING("perFrameSleep", 1);
			amount = CLAMP(amount, 0, 500);
			Sleep(amount);
			PERFINFO_AUTO_STOP();
		}

		}PERFINFO_AUTO_STOP(); // "top"

		PERFINFO_AUTO_START("rxbxCheckForQueriesCompleted", 1);
		// Wait for previous frames to finish on the GPU
		rxbxCheckForQueriesCompleted(device, -1);
		PERFINFO_AUTO_STOP();

		if (rdr_state.max_gpu_frames_ahead)
		{
			int frame_index, wait_index;
			U32 sprite_timer=0;
			F32 sprite_time;
			bool sprite_waited = false;

			PERFINFO_AUTO_START("Wait For GPU", 1);

			MIN1(rdr_state.max_gpu_frames_ahead, ARRAY_SIZE(device->frame_sync_query_frame_indices));

			frame_index = (device->cur_frame_index - rdr_state.max_gpu_frames_ahead + ARRAY_SIZE(device->frame_sync_query_frame_indices)) % ARRAY_SIZE(device->frame_sync_query_frame_indices);

			// wait for previous frame sprite draw query to finish
			wait_index = device->sprite_draw_sync_query_frame_indices[frame_index] - 1;
			if (wait_index >= 0)
			{
				sync_wait_iteration_count = 0;
				while (device->sync_query[wait_index].bIssued)
				{
					++sync_wait_iteration_count;
					sprite_waited = true;
					PERFINFO_AUTO_START_L3("Queries", 1);
					rxbxCheckForQueriesCompleted(device, wait_index);
					PERFINFO_AUTO_STOP_L3();
					if (device->sync_query[wait_index].bIssued && !rdr_state.noSleepWhileWaitingForGPU)
						Sleep(1);
					if (sync_wait_iteration_count == SYNC_ITERATIONS_BEFORE_ERRORF)
					{
						ErrorDeferredf("Sprite sync query may be stuck; ending wait early.");
					}
					if (rdr_debug_max_sync_waits && sync_wait_iteration_count >= rdr_debug_max_sync_waits)
					{
						// Break out, or this loop can hang, possibly because we're not filling the command
						// buffer with further work. The query documentation (see here:
						// http://msdn.microsoft.com/en-us/library/windows/desktop/bb147308(v=vs.85).aspx) 
						// seems to indicate the loop's structure should not be subject to infinite loops,
						// because we're flushing the GetData call on the current query, but we've got 
						// crash dumps from hung clients.
						device->sync_query[wait_index].bSyncStuck = true;
						device->sync_query[wait_index].frameStuck = device->device_base.frame_count;
						TRACE_DEVICE(&device->device_base, "Skipped sprite sync query %d after %d checks, frame_index = %d, max frames ahead = %d\n", wait_index, sync_wait_iteration_count, frame_index, rdr_state.max_gpu_frames_ahead);
						break;
					}
				}
			}

			if (sprite_waited)
			{
				// start timer (assumes sprite drawing query has not already been passed; if it has then the time will be underestimated)
				sprite_timer = timerAlloc();
				timerStart(sprite_timer);
#if !PLATFORM_CONSOLE
				// Don't process messages during the frame: starting at BeginScene, until after Present
				if (!rdr_state.bProcessMessagesOnlyBetweenFrames)
					rxbxProcessWindowsMessages2Direct(device);
#endif
			}

			// wait for previous frame sync query to finish
			wait_index = device->frame_sync_query_frame_indices[frame_index] - 1;
			if (wait_index >= 0)
			{
				sync_wait_iteration_count = 0;
				while (device->sync_query[wait_index].bIssued)
				{
					++sync_wait_iteration_count;
					rxbxCheckForQueriesCompleted(device, wait_index);
					if (device->sync_query[wait_index].bIssued && !rdr_state.noSleepWhileWaitingForGPU)
						Sleep(1);
					if (sync_wait_iteration_count == SYNC_ITERATIONS_BEFORE_ERRORF)
					{
						ErrorDeferredf("Frame sync query may be stuck; ending wait early.");
					}
					if (rdr_debug_max_sync_waits && sync_wait_iteration_count >= rdr_debug_max_sync_waits)
					{
						// Break out, or this loop can hang. See explanation in the sprite sync loop.
						device->sync_query[wait_index].bSyncStuck = true;
						device->sync_query[wait_index].frameStuck = device->device_base.frame_count;
						TRACE_DEVICE(&device->device_base, "Skipped frame sync query %d after %d checks, frame_index = %d, max frames ahead = %d\n", wait_index, sync_wait_iteration_count, frame_index, rdr_state.max_gpu_frames_ahead);
						break;
					}
				}
			}

			if (sprite_waited)
			{
				// stop timer (assumes sprite drawing is the final operation in the queue)
				sprite_time = timerElapsed(sprite_timer);
				timerFree(sprite_timer);
			}
			else
			{
				sprite_time = -1;
			}

			INC_OPERATION(sprite_draw_time, sprite_time);

			PERFINFO_AUTO_STOP();
		}

		// Present is called here to maximize time between the EndScene call and the Present call
		PERFINFO_AUTO_START("Direct3D Present", 1);
		if (device->skip_next_present)
		{
			TRACE_FRAME(&device->device_base, "Present Skipped due to request");
			device->after_scene_before_present = 0;
			device->skip_next_present = 0;
			nPresentCode = 0;
		} else {
			FP_NO_EXCEPTIONS_BEGIN;
			device->in_present = 1;
			device->after_scene_before_present = 0;
			if (device->d3d11_device)
				nPresentCode = IDXGISwapChain_Present(device->d3d11_swapchain, device->d3d11_swap_interval, 0);
			else
				nPresentCode = IDirect3DDevice9_Present(device->d3d_device, NULL, NULL, NULL, NULL);
			TRACE_FRAME(&device->device_base, "Present\n");
			FP_NO_EXCEPTIONS_END;
			device->in_present = 0;
#if !PLATFORM_CONSOLE
			if (rdr_state.bProcessMessagesOnlyBetweenFrames)
				rxbxProcessWindowsMessages2Direct(device);
#endif
#if LOG_RESOURCE_USAGE
			rxbxDumpMemInfo("after Present()");
#endif
		}
		PERFINFO_AUTO_STOP();

#if _XBOX
		{
			static bool doneOnce=false;
			// At least on DevKits it seems the OS releases this chunk of memory
			// back to us on the first Present, I'm not sure if it grabs it back
			// later or not though, but this lets memMonitorPhysicalUntracked()
			// give us better values here.
			if (!doneOnce) {
				doneOnce=true;
				memMonitorTrackUserMemory("SystemReserved", true, -3680*1024, -1);
			}
		}
#endif
		if (rdr_state.max_gpu_frames_ahead)
		{
			assert(device->frame_sync_query_frame_indices[device->cur_frame_index] == 0);
			device->frame_sync_query_frame_indices[device->cur_frame_index] = 1+rxbxIssueSyncQuery(device);
		}

		device->cur_frame_index++;
		device->cur_frame_index = device->cur_frame_index % ARRAY_SIZE(device->frame_sync_query_frame_indices);
		device->frame_sync_query_frame_indices[device->cur_frame_index] = 0;
		device->sprite_draw_sync_query_frame_indices[device->cur_frame_index] = 0;

#if !PLATFORM_CONSOLE
		PERFINFO_AUTO_START("handle present errors", 1);{
		// TODO - DJR just testing the state dump code
		if (bDebugDumpDeviceStateAfterPresent)
		{
			rxbxDumpDeviceState(device, 0, bDebugDumpDeviceStateAfterPresent >= 2);
			bDebugDumpDeviceStateAfterPresent = 0;
		}
		else
        {
		    rxbxDumpDeviceStateOnError(device, nPresentCode);
        }

		if (nPresentCode == D3DERR_DEVICELOST) {
			rxbxSetDeviceLost(device, RDR_LOST_FOCUS);
			TRACE_WINDOW("Present Failed with D3DERR_DEVICELOST Device:0x%08p ActiveSurface:0x%08p", device, device->active_surface);
		} else if (nPresentCode == D3DERR_DRIVERINTERNALERROR) {
			// some other error occurred in a call where D3D could not report it, but we
			// need to recreate the device when we attempt to reactivate it
			rxbxSetDeviceLost(device, RDR_LOST_DRIVER_INTERNAL_ERROR);
			TRACE_WINDOW("Present Failed with D3DERR_DRIVERINTERNALERROR Device:0x%08p ActiveSurface:0x%08p", device, device->active_surface);

			rxbxDumpDeviceStateOnError(device, nPresentCode);
			// We terminate the application here because we can't reset the device.
			rxbxFatalDriverInternalError();
		}
		}PERFINFO_AUTO_STOP();
#endif

		PERFINFO_AUTO_START("checkFrameRateStabilizer", 1);
		checkFrameRateStabilizer(device, 2);
		PERFINFO_AUTO_STOP();
	}

	rxbxApplyDeviceSettingChanges(device);

	device->device_base.perf_values_last_frame = device->device_base.perf_values;
	ZeroStruct(&device->device_base.perf_values);
	device->device_base.perf_values.vs_constant_changes.min_seq = INT_MAX;
	device->device_base.perf_values.vs_constant_changes.max_seq = 0;
	if (device->device_base.perf_values_last_frame.vs_constant_changes.num_seq)
	{
		device->device_base.perf_values_last_frame.vs_constant_changes.avg_seq /= 
			device->device_base.perf_values_last_frame.vs_constant_changes.num_seq;
	}

	etlAddEvent(device->device_base.event_timer, "Buffer swap", ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP_FUNC();

	if (wtIsThreaded(device->device_base.worker_thread))
	{
		EnterCriticalSection(&device->device_base.frame_check);
		device->device_base.frames_buffered--;
		LeaveCriticalSection(&device->device_base.frame_check);

		// These must NOT be inside a perfinfo block!
		autoTimerThreadFrameEnd();
		autoTimerThreadFrameBegin(wtGetName(device->device_base.worker_thread));

		if (device->device_base.frame_done_signal)
			SetEvent(device->device_base.frame_done_signal);
	}

	if (device->notify_settings_frame_count)
	{
		--device->notify_settings_frame_count;
		rxbxDeviceGetDisplaySettingsFromDevice(device);
	}
}

static int rdrStackOverflowNow;
// Triggers a stack overflow in the render thread
AUTO_CMD_INT(rdrStackOverflowNow, rdrStackOverflowNow) ACMD_CATEGORY(Debug);

void rxbxBeginSceneDirect(RdrDeviceDX *device, RdrDeviceBeginSceneParams *params, WTCmdPacket *packet)
{
	int i;
	HRESULT hResult;

	PERFINFO_AUTO_START(__FUNCTION__ " Pre-Present", 1);

	CHECKDEVICELOCK(device);

	device->device_base.frame_count = params->frame_count_update;
	if (device->isLost != RDR_OPERATING || !params->do_begin_scene)
		TRACE_FRAME(&device->device_base, "BeginScene %s/%s\n", device->isLost ? "lost" : "oper", 
			params->do_begin_scene ? "requested" : "skipped");

	if (rdrStackOverflowNow)
	{
		void stackOverflowNow(int iBytesAtATime);
		stackOverflowNow(rdrStackOverflowNow);
	}

	if (params->do_begin_scene)
	{
		bool bBeganScene = false;
		rxbxDealWithAllCompiledResultsDirect(device);
		PERFINFO_AUTO_START_L3("Process Queries", 1);
		rxbxProcessQueriesDirect(device, false); // get data from any occlusion queries that have finished
		PERFINFO_AUTO_STOP_L3();

		if (device->needs_present) {
			device->needs_present = 0;
			PERFINFO_AUTO_STOP();
			rxbxPresentDirect(device); // this function cannot be wrapped in a perfinfo block!
			PERFINFO_AUTO_START(__FUNCTION__ " Post-Present", 1);
		}
#if !PLATFORM_CONSOLE
		if (!device->in_scene)
		{
			device->in_scene = 1;
			if (device->d3d_device)
			{
				PERFINFO_AUTO_START_L3("D3D BeginScene", 1);
				CHECKX(IDirect3DDevice9_BeginScene(device->d3d_device));
				PERFINFO_AUTO_STOP_L3();
				TRACE_FRAME(&device->device_base, "BeginScene\n");
			}


#if D3D_RESOURCE_LEAK_TEST_ENABLE
			// DJR leak some textures or virtual memory space, for testing D3D allocation failure handling
			rxbxResourceLeakTest(device);
#endif

			bBeganScene = true;
			checkFrameRateStabilizer(device, 3);

			if (rdr_state.clearAllBuffersEachFrame)
			{
				rxbxClearAllBuffers(device);
			}

		}
#endif

		PERFINFO_AUTO_START_L3("Check VBOs", 1);
		for (i = eaSize(&device->vbo_memory)-1; i >= 0; --i)
		{
			RdrVBOMemoryDX *vbo_chunk = device->vbo_memory[i];
			vbo_chunk->used_bytes = 0;

			// if a chunk is unused for 10 frames, free it
			if (vbo_chunk->last_frame_used - device->frame_count_xdevice > 10)
			{
#if _XBOX
				// must unbind the VBO before checking if it is busy
				if (IDirect3DVertexBuffer9_IsSet(vbo_chunk->vbo, device->d3d_device))
					rxbxNotifyVertexBufferFreed(device, vbo_chunk->vbo);

				if (IDirect3DVertexBuffer9_IsBusy(vbo_chunk->vbo))
					continue;
#endif
				freeVBOChunk(vbo_chunk);
				eaRemoveFast(&device->vbo_memory, i);
			}
		}
		PERFINFO_AUTO_STOP_L3();

		PERFINFO_AUTO_START_L3("Check IBOs", 1);
		for (i = eaSize(&device->ibo_memory)-1; i >= 0; --i)
		{
			RdrIBOMemoryDX *ibo_chunk = device->ibo_memory[i];
			ibo_chunk->used_bytes = 0;

			// if a chunk is unused for 10 frames, free it
			if (ibo_chunk->last_frame_used - device->frame_count_xdevice > 10)
			{
				freeIBOChunk(ibo_chunk);
				eaRemoveFast(&device->ibo_memory, i);
			}
		}

		for (i = eaSize(&device->ibo32_memory)-1; i >= 0; --i)
		{
			RdrIBOMemoryDX *ibo_chunk = device->ibo32_memory[i];
			ibo_chunk->used_bytes = 0;

			// if a chunk is unused for 10 frames, free it
			if (ibo_chunk->last_frame_used - device->frame_count_xdevice > 10)
			{
				freeIBOChunk(ibo_chunk);
				eaRemoveFast(&device->ibo32_memory, i);
			}
		}
		PERFINFO_AUTO_STOP_L3();

		device->frame_count_xdevice++;

#if DEBUG_DRAW_CALLS
		device->drawCallNumber = 0;
#endif

		// if no one is reading the profiling queries, refrain from issuing more.
		if (bBeganScene && device->bProfilingQueries && (device->iNumWaitingQueries != ARRAY_SIZE(device->disjoint_query)))
		{
			//issue the disjoint query for GPU profiling
			if (device->d3d11_device)
			{
				ID3D11DeviceContext_Begin(device->d3d11_imm_context, device->disjoint_query[device->iNextFreeDisjointQuery].asynch_d3d11);
			} else {
				hResult = IDirect3DQuery9_Issue(device->disjoint_query[device->iNextFreeDisjointQuery].query_d3d9, D3DISSUE_BEGIN);
				devassert(hResult == S_OK);
			}

			rxbxIssueProfilingQuery(device,-1);
		}

		device->display_thread.stereoscopicActive = rxbxNVStereoIsActive();
		if (device->display_thread.stereoscopicActive)
			rxbxStereoscopicTexUpdate(device);
	}

	PERFINFO_AUTO_STOP();
}

void rxbxDrawAllFMVs(RdrDeviceDX *device)
{
	int i;
	// TODO: Need to adjust blend state?  What if last sprite was additive or something?
	for (i=0; i<eaSize(&device->active_fmvs); i++)
	{
		rxbxFMVGoDirect(device, device->active_fmvs[i]);
	}
	if (eaSize(&device->active_fmvs))
	{
		// Bink doesn't play well with state management
		if (!device->d3d11_device)
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_ZENABLE, TRUE));
		rxbxResetDeviceState(device);
		rxbxResetStateDirect(device, NULL, NULL);
	}
}

void rxbxEndSceneDirect(RdrDeviceDX *device, RdrDeviceEndSceneParams *params, WTCmdPacket *packet)
{
	PERFINFO_AUTO_START(__FUNCTION__ " Pre-Present", 1);

	CHECKDEVICELOCK(device);

	if (params->do_buffer_swap && params->do_end_scene)
	{
		rxbxDrawAllFMVs(device);
	}

	device->device_base.frame_count = params->frame_count_update;
	if (device->isLost != RDR_OPERATING || !params->do_end_scene)
		TRACE_FRAME(&device->device_base, "EndScene %s/%s\n", device->isLost ? "lost" : "oper", 
			params->do_end_scene ? "requested" : "skipped");


#if !PLATFORM_CONSOLE
	//callback used for Xlive integration
	if (params->do_xlive_callback && prxbxPresentDirect_Callback && 0)
	{
		// FIXME: if we want to use this, this needs to not mess up our ZFUNC/we need to restore
		//  it or apply state afterwards or something
		etlAddEvent(device->device_base.event_timer, "Windows Live callback", ELT_CODE, ELTT_BEGIN);
		if (device->d3d_device)
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_ZFUNC, D3DCMP_LESSEQUAL));
		prxbxPresentDirect_Callback(device);
		etlAddEvent(device->device_base.event_timer, "Windows Live callback", ELT_CODE, ELTT_END);
	}

	if (params->do_end_scene)
	{
		if (device->in_scene)
		{
			// If we are full on queries, refrain from issuing more.
			if (device->bProfilingQueries && (device->iNumWaitingQueries != ARRAY_SIZE(device->disjoint_query)))
			{
				//finish the disjoint query for GPU profiling
				HRESULT hResult;
				if (device->d3d11_device)
				{
					ID3D11DeviceContext_End(device->d3d11_imm_context, device->disjoint_query[device->iNextFreeDisjointQuery].asynch_d3d11);
				} else {
					hResult = IDirect3DQuery9_Issue(device->disjoint_query[device->iNextFreeDisjointQuery].query_d3d9, D3DISSUE_END);
					devassert(hResult == S_OK);
				}

				device->iNextFreeDisjointQuery = (device->iNextFreeDisjointQuery+1)%ARRAY_SIZE(device->disjoint_query);
				device->iNumWaitingQueries++;
			}

			checkFrameRateStabilizer(device, 4);
			if (device->d3d_device)
			{
				CHECKX(IDirect3DDevice9_EndScene(device->d3d_device));
				TRACE_FRAME(&device->device_base, "EndScene\n");
			}

			device->after_scene_before_present = 1;
			device->in_scene = 0;
		}
	}
#endif

	if (params->do_buffer_swap) {
		if (rdr_state.swapBuffersAtEndOfFrame) {
			PERFINFO_AUTO_STOP();
			rxbxPresentDirect(device); // this function cannot be wrapped in a perfinfo block!
			PERFINFO_AUTO_START(__FUNCTION__ " Post-Present", 1);
		} else {
			TRACE_FRAME(&device->device_base, "Present delayed");
			device->needs_present = 1;
		}
	}
	else
	{
		TRACE_FRAME(&device->device_base, "Present Skipped %s/%s", device->isLost ? "lost" : "oper", 
			params->do_buffer_swap ? "requested" : "skipped");
		device->after_scene_before_present = 0;
	}

	etlAddEvent(device->device_base.event_timer, "End scene", ELT_CODE, ELTT_INSTANT);

	PERFINFO_AUTO_STOP();
}

// This function frees D3D objects which will automatically be freed when releasing
//   the device, therefore should not be called in general for performance reasons,
//   but can be called for helping track memory leaks, etc.
// This should *only* release D3D objects bound to a device (and would normally
//   be released automatically), should not free any structures we allocated, we
//   need to free those ourself!
static void rxbxReleaseD3DOjbects(RdrDeviceDX *device)
{
	int refcount;
#define CLEANUP_STATE_TABLE(table, type)				\
		FOR_EACH_IN_STASHTABLE(device->table, type, state)	\
		{													\
			refcount = type##_Release(state);				\
		}													\
		FOR_EACH_END;

	CLEANUP_STATE_TABLE(d3d11_rasterizer_states, ID3D11RasterizerState);
	CLEANUP_STATE_TABLE(d3d11_blend_states, ID3D11BlendState);
	CLEANUP_STATE_TABLE(d3d11_depth_stencil_states, ID3D11DepthStencilState);
	CLEANUP_STATE_TABLE(d3d11_sampler_states, ID3D11SamplerState);
}

void rxbxDestroyDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	RdrSurfaceDX *surface = &device->primary_surface;

	CHECKDEVICELOCK(device);

//	rxbxRestoreGammaRampDirect(device);

	if (device->d3d_device || device->d3d11_device) // These are NULL if it failed to create the device
		rxbxReleaseDeviceSecondaryDataDirect(device);
	rxbxFreeAllTexturesDirect(device, NULL, NULL);

#if _FULLDEBUG
	rxbxReleaseD3DOjbects(device);
#endif

	stashTableDestroy(device->d3d11_rasterizer_states);
	stashTableDestroy(device->d3d11_blend_states);
	stashTableDestroy(device->d3d11_depth_stencil_states);
	stashTableDestroy(device->d3d11_sampler_states);
	stashTableDestroy(device->stPixelShaderCache);
	device->stPixelShaderCache = NULL;

#if RDR_NVIDIA_TXAA_SUPPORT
	rxbxCloseTXAA(device);
#endif

	rxbxSafeShutdownEntireDevice(device, true);

#if !PLATFORM_CONSOLE
	rxbxRestoreGammaRampDirect(device);

	if (rdr_state.useManualDisplayModeChange && device->display_thread.fullscreen)
	{
		ChangeDisplaySettings(NULL,0);
		multiMonResetInfo();
	}

	ShowCursor(TRUE);
#endif

	rdrUntrackAllUserMemoryDirect(&device->device_base); // FIXME: Move this to happen inside the thread, it's not thread-safe

	wtQueueMsg(device->device_base.worker_thread, RDRMSG_DESTROY, 0, 0);
}

int rxbxIsInactiveDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	if (rdr_state.bProcessMessagesOnlyBetweenFrames && device->isLost != RDR_OPERATING)
		rxbxProcessWindowsMessages2Direct(device);
	rxbxReactivateDirect(device);
#endif
	return device->isLost;
}

// REFACTOR
void rxbxTestIfNonManagedTextureActive(RdrDeviceDX *device);


void rxbxReactivateDirect(RdrDeviceDX *xdevice)
{
#if !PLATFORM_CONSOLE
	HRESULT nTestCoopLevelResult, nResetResult;
	RdrSurfaceDX *active_surface = xdevice->active_surface;
	int was_device_invalidated = xdevice->isLost == RDR_LOST_DRIVER_INTERNAL_ERROR;
	RdrDeviceLossState final_device_lost = RDR_OPERATING;

	ASSERT_NOT_INFRAME(xdevice);

	if (xdevice->interactive_resizing || xdevice->inactive_display || xdevice->inactive_app)
	{
		return; // This isn't actually hit, we skip the size messages completely while resizing, but we could process them and skip resetting with this code.
	}

	assert(xdevice->d3d_device); // Only on DX9

	PERFINFO_AUTO_START("rxbxReactivateDirect", 1);

	PERFINFO_AUTO_START("TestCooperativeLevel", 1);
	nTestCoopLevelResult = IDirect3DDevice9_TestCooperativeLevel( xdevice->d3d_device );
	PERFINFO_AUTO_STOP();
	if ( nTestCoopLevelResult != D3D_OK )
	{
		// don't set to LOST_FOCUS if already lost
		if (xdevice->isLost == RDR_OPERATING)
			rxbxSetDeviceLost(xdevice, RDR_LOST_FOCUS);
		if ( nTestCoopLevelResult != D3DERR_DEVICENOTRESET ) {
			TRACE_DEVICE(&xdevice->device_base, __FUNCTION__":returning because TCL = %s 0x%x", 
				rxbxGetStringForHResult(nTestCoopLevelResult), nTestCoopLevelResult);
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	else
	if (xdevice->isLost == RDR_OPERATING)
	{
		rxbxSetStateActive(xdevice, &xdevice->primary_surface.state, 0, xdevice->primary_surface.width_thread, xdevice->primary_surface.height_thread);
		//xdevice->isLost = 0;
		TRACE_DEVICE(&xdevice->device_base, __FUNCTION__":returning because operating");
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("LostDevice", 1);

	TRACE_DEVICE(&xdevice->device_base, "%s (%d x %d)", "rxbxReactivate():LostDevice",
		xdevice->d3d_present_params->BackBufferWidth,
		xdevice->d3d_present_params->BackBufferHeight);

	// reset the xdevice

	rxbxTestIfNonManagedTextureActive(xdevice);

	PERFINFO_AUTO_START("LostDevice", 1);
	rxbxHandleLostDeviceDirect(xdevice); // Must be guaranteed to set isLost != RDR_OPERATING
	PERFINFO_AUTO_STOP();

	if (!was_device_invalidated)
	{
		const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
		// check if any textures are still referenced


		xdevice->debug_in_reset = 1;

		rxbxLogPresentParams(xdevice, "Reset", xdevice->d3d_present_params, 
			device_infos[xdevice->device_info_index]->adapter_index, 
			rdr_state.usingNVPerfHUD ? D3DDEVTYPE_REF : D3DDEVTYPE_HAL, 0);
		PERFINFO_AUTO_START("Reset", 1);
		nResetResult = IDirect3DDevice9_Reset( xdevice->d3d_device, xdevice->d3d_present_params );
		PERFINFO_AUTO_STOP();
		TRACE_DEVICE(&xdevice->device_base, "Reset Complete hr %x", nResetResult);
		rxbxLogPresentParams(xdevice, "Reset complete", xdevice->d3d_present_params,
			device_infos[xdevice->device_info_index]->adapter_index, 
			rdr_state.usingNVPerfHUD ? D3DDEVTYPE_REF : D3DDEVTYPE_HAL, 0);

		// unless changing only vsync, the monitor info is no longer valid 
		multiMonResetInfo();

		xdevice->debug_in_reset = 0;

		SET_FP_CONTROL_WORD_DEFAULT; // Gets changed/reset here on ATI drivers

		if (nResetResult == D3DERR_DEVICELOST) {
			// reset failed since device is still lost, why?
			static int reportResetErrorOnlyOnce = 0;
			HRESULT nRetryTCLResult = IDirect3DDevice9_TestCooperativeLevel( xdevice->d3d_device );
			TRACE_DEVICE(&xdevice->device_base, "Reset returned D3DERR_DEVICELOST. 1st TestCooperativeLevel returned %s. 2nd TCL returned %s",
				rxbxGetStringForHResult(nTestCoopLevelResult), rxbxGetStringForHResult(nRetryTCLResult));
			if (!reportResetErrorOnlyOnce)
			{
				reportResetErrorOnlyOnce = 1;
				ErrorDeferredf("Reset returned D3DERR_DEVICELOST. 1st TestCooperativeLevel returned %s. 2nd TCL returned %s",
					rxbxGetStringForHResult(nTestCoopLevelResult), rxbxGetStringForHResult(nRetryTCLResult));
			}
			final_device_lost = RDR_LOST_FOCUS;

			++xdevice->reset_fail_count;
			if (xdevice->reset_fail_count > 256)
				rxbxFatalHResultErrorf(xdevice, nResetResult, "resetting the device", "");
		}
		else
		if (nResetResult == D3DERR_DRIVERINTERNALERROR) {
			// some other error occurred in a call where D3D could not report it, but we
			// need to recreate the device when we attempt to reactivate it
			rxbxSetDeviceLost(xdevice, RDR_LOST_DRIVER_INTERNAL_ERROR);
			TRACE_DEVICE(&xdevice->device_base, "Reset Failed with D3DERR_DRIVERINTERNALERROR Device:0x%08p ActiveSurface:0x%08p", xdevice, xdevice->active_surface);

			rxbxDumpDeviceStateOnError(xdevice, nResetResult);
			// We terminate the application here because we can't reset the device.
			rxbxFatalDriverInternalError();
		}
		else
		if (FAILED(nResetResult))
			rxbxFatalHResultErrorf(xdevice, nResetResult, "resetting the device", "");
		else
		{
			xdevice->reset_fail_count = 0;
			if (xdevice->d3d_present_params->Windowed)
				rxbxUnClipCursor(xdevice);
			else
			{
				// Device create with fullscreen can change desktop monitor rectangle layout
				// so find the new rectangle
				RECT client_rect, wnd_rect;
				MONITORINFOEX moninfo;
				multiMonGetMonitorInfo(device_infos[xdevice->device_info_index]->monitor_index, &moninfo);

				rxbxClipCursorToRect(xdevice, &moninfo.rcMonitor);

				// cover the fullscreen rect of the target monitor
				xdevice->display_thread.xpos = moninfo.rcMonitor.left;
				xdevice->display_thread.ypos = moninfo.rcMonitor.top;
				xdevice->display_thread.width = moninfo.rcMonitor.right - moninfo.rcMonitor.left;
				xdevice->display_thread.height = moninfo.rcMonitor.bottom - moninfo.rcMonitor.top;

				// Query the window for it's actual client rect size,
				// in case the window was maximized, for example.
				GetClientRect(xdevice->hWindow, &client_rect);
				GetClientRect(xdevice->hWindow, &wnd_rect);

				// Have to use SetWindowPos to work in Full screen when the task bar is at the top
				TRACE_DEVICE(&xdevice->device_base, "Post Reset SetWindowPos %d x %d %s\n", xdevice->display_thread.width, xdevice->display_thread.height, 
					xdevice->display_thread.fullscreen ? "Fullscreen" : "Windowed");
				SetWindowPos(xdevice->hWindow, HWND_TOP, 
					xdevice->display_thread.xpos, xdevice->display_thread.ypos, 
					xdevice->display_thread.width, xdevice->display_thread.height, SWP_FRAMECHANGED | SWP_NOZORDER);

				TRACE_DEVICE(&xdevice->device_base, "Post Reset Client %d x %d %s GWR %d x %d\n", client_rect.right, client_rect.bottom, 
					xdevice->display_thread.fullscreen ? "Fullscreen" : "Windowed",
					wnd_rect.right - wnd_rect.left, wnd_rect.bottom - wnd_rect.top);
			}
		}

		rxbxGetPrimarySurfaces( xdevice );
	}
	rxbxResetDeviceState(xdevice);
	rxbxSetStateActive(xdevice, &xdevice->primary_surface.state, 1, xdevice->primary_surface.width_thread, xdevice->primary_surface.height_thread);
	rxbxSetDeviceLost(xdevice, final_device_lost);

	// Restore state

	if (active_surface)
		rxbxSetSurfaceActiveDirectSimple(xdevice, active_surface);

	if (xdevice->in_scene)
	{
		PERFINFO_AUTO_START("BeginScene", 1);
		CHECKX(IDirect3DDevice9_BeginScene(xdevice->d3d_device));
		PERFINFO_AUTO_STOP();
		TRACE_FRAME(&xdevice->device_base, "BeginScene (Reactivate) %u\n", xdevice->device_base.frame_count);
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
#else
	rxbxSetDeviceLost(xdevice, RDR_OPERATING);
#endif
}

void rxbxSetVsyncDirect(RdrDeviceDX *xdevice, int *enable_ptr, WTCmdPacket *packet)
{
#if !PLATFORM_CONSOLE
	int enable = *enable_ptr;
	if (xdevice->d3d11_device)
	{
		if (!!enable != xdevice->d3d11_swap_interval)
			xdevice->d3d11_swap_interval = !!enable;
	} else {
		DWORD newValue = enable ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
		if (newValue != xdevice->d3d_present_params->PresentationInterval)
		{
			xdevice->d3d_present_params->PresentationInterval = newValue;
			rxbxSetDeviceLost(xdevice, RDR_FORCE_RESET_SETTINGS_CHANGED); // Faking lost so that we can forcibly reset it
			rxbxReactivateDirect(xdevice);
		}
	}
#endif
}

static IDXGIOutput * rxbxGetDXGIOutput(U32 adapter_index, U32 output_index)
{
	IDXGIAdapter * pAdapter = NULL;
	IDXGIFactory* pFactory = NULL; 
	IDXGIOutput* pOutput = NULL; 
	HRESULT enumResult;

	// Create a DXGIFactory object.
	enumResult = CreateDXGIFactory(&IID_IDXGIFactory ,(void**)&pFactory);
	assert(SUCCEEDED(enumResult));

	enumResult = IDXGIFactory_EnumAdapters(pFactory, adapter_index, &pAdapter);
	assert(SUCCEEDED(enumResult));
	enumResult = IDXGIAdapter_EnumOutputs(pAdapter, output_index, &pOutput);
	assert(SUCCEEDED(enumResult));

	if (pAdapter)
		IDXGIAdapter_Release(pAdapter);
	if(pFactory)
		IDXGIFactory_Release(pFactory);

	return pOutput;
}

void rxbxResizeDeviceDirect(RdrDevice * device, DisplayParams *dimensions, RdrResizeFlags flags)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	int newAdapter = 0, newDisplayMode = 0, newFullScreen = dimensions->fullscreen;
	int newWidth = 0, newHeight = 0;
	bool bFullscreenSwitch = !!(dimensions->fullscreen != xdevice->display_thread.fullscreen || (dimensions->fullscreen && dimensions->preferred_fullscreen_mode != xdevice->display_thread.preferred_fullscreen_mode));
	const RdrDeviceInfo * const * device_infos = NULL;
	const RdrDeviceInfo * device_info = NULL;
	const RdrDeviceModeDX * display_mode = NULL;

	ASSERT_NOT_INFRAME(xdevice);

	xdevice->display_thread = *dimensions;
	if (newFullScreen)
	{
		device_infos = rdrEnumerateDevices();

		// Fullscreen. Use the exact display mode and adapter identified in the request, not the other
		// display parameters, which apply only to windowed mode
		newAdapter = dimensions->preferred_adapter;
		newDisplayMode = dimensions->preferred_fullscreen_mode;
		assert(newAdapter >= 0 && newAdapter < eaSize(&device_infos));

		device_info = device_infos[newAdapter];
		assert(newDisplayMode >= 0 && newDisplayMode < eaSize(&device_info->display_modes));
		display_mode = (RdrDeviceModeDX*)device_info->display_modes[newDisplayMode];

		xdevice->device_width = newWidth = display_mode->base.width;
		xdevice->device_height = newHeight = display_mode->base.height;
		xdevice->refresh_rate = display_mode->base.refresh_rate;
	}
	else
	if (dimensions->windowed_fullscreen && dimensions->allow_windowed_fullscreen)
	{
		rwinDisplayParamsSetToCoverMonitorForDeviceWindow(device, xdevice->hWindow, dimensions);

		// Windowed fullscreen: Use the size of the active monitor
		xdevice->device_width = newWidth = dimensions->width;
		xdevice->device_height = newHeight = dimensions->height;
		xdevice->display_thread = *dimensions;
	}
	else
	{
		// Windowed: Use the requested window client rectangle dimensions
		xdevice->device_width = newWidth = dimensions->width;
		xdevice->device_height = newHeight = dimensions->height;
	}

	if (xdevice->d3d11_device)
	{
		HRESULT hrDXGIResult;

		if (flags == RDRRESIZE_PRECOMMIT_START)
		{
			if (bFullscreenSwitch)
			{
				IDXGIOutput * pOutput = NULL;
				DXGI_MODE_DESC mode_desc = xdevice->d3d11_swap_desc->BufferDesc;
				DXGI_OUTPUT_DESC temp_output_desc = { 0 };
				if (newFullScreen)
				{
					int output_monitor_index;
					pOutput = rxbxGetDXGIOutput(device_info->adapter_index, device_info->output_index);
					mode_desc = display_mode->dxgi_mode;
				
					// DX11 TODO DJR this code is to ensure the Output matches up with the selected 
					// mode's description of the monitor, as I rewrite/fix the fullscreen support.
					IDXGIOutput_GetDesc(pOutput, &temp_output_desc);
					output_monitor_index = multimonFindMonitorHMonitor(temp_output_desc.Monitor);
					assert(output_monitor_index == dimensions->preferred_monitor);
				}
				else
				{
					mode_desc.Width = newWidth;
					mode_desc.Height = newHeight;
				}

				if (xdevice->display_thread.srgbBackBuffer) {
					mode_desc.Format = srgbFormat11(mode_desc.Format);
				}
				xdevice->d3d11_swap_desc->BufferDesc = mode_desc;

				TRACE_DEVICE(&xdevice->device_base, "IDXGISwapChain_SetFullscreenState %s %p", newFullScreen ? "FS" : "W", pOutput);
				hrDXGIResult = IDXGISwapChain_SetFullscreenState(xdevice->d3d11_swapchain, newFullScreen ? TRUE : FALSE, pOutput);
				TRACE_DEVICE(&xdevice->device_base, "SetFullscreenState complete\n");
				rxbxFatalHResultErrorf(xdevice, hrDXGIResult, "SetFullscreenState failed changing display mode", "%d x %d", newWidth, newHeight);

				TRACE_DEVICE(&xdevice->device_base, "IDXGISwapChain_ResizeTarget W %u x H %u Ref %u/%u Fmt %u Scale %u Scan %u %s", mode_desc.Width, mode_desc.Height, mode_desc.RefreshRate.Numerator, mode_desc.RefreshRate.Denominator, 
					mode_desc.Format, mode_desc.Scaling, mode_desc.ScanlineOrdering,
					display_mode ? display_mode->base.name : "Windowed");
				hrDXGIResult = IDXGISwapChain_ResizeTarget(xdevice->d3d11_swapchain, &mode_desc);
				TRACE_DEVICE(&xdevice->device_base, "ResizeTarget complete\n");
				rxbxFatalHResultErrorf(xdevice, hrDXGIResult, "ResizeTarget failed changing display mode", "%d x %d", newWidth, newHeight);

				// DX11 TODO DJR: MS docs claim this helps DXGI like our mode switch request.
				// See http://msdn.microsoft.com/en-us/library/windows/desktop/ee417025(v=vs.85).aspx.
				// But it appears to cause another mode switch.
				//if (newFullScreen)
				//{
				//	// let DXGI know we are OK with the exact refresh rate it selected
				//	DXGI_MODE_DESC mode_desc = display_mode->dxgi_mode;
				//	mode_desc.RefreshRate.Numerator = 0;
				//	mode_desc.RefreshRate.Denominator = 0;
				//	IDXGISwapChain_ResizeTarget(xdevice->d3d11_swapchain, &mode_desc);
				//}
				if (pOutput)
					IDXGIOutput_Release(pOutput);
				multiMonResetInfo();
			}
		}
		else
		if (flags == RDRRESIZE_DEFAULT || flags == RDRRESIZE_PRECOMMIT_END)
		{
			TRACE_DEVICE(&xdevice->device_base, "IDXGISwapChain_ResizeBuffers %d x %d", newWidth, newHeight);
			rxbxSurfaceCleanupDirect(xdevice, &xdevice->primary_surface, NULL);
			hrDXGIResult = IDXGISwapChain_ResizeBuffers(xdevice->d3d11_swapchain, 1, newWidth, newHeight, xdevice->d3d11_swap_desc->BufferDesc.Format, xdevice->d3d11_swap_desc->Flags);
			TRACE_DEVICE(&xdevice->device_base, "ResizeBuffers complete\n");
			xdevice->primary_surface.width_thread = newWidth;
			xdevice->primary_surface.surface_base.width_nonthread = newWidth; // not thread safe, but no good way around it
			xdevice->primary_surface.surface_base.vwidth_nonthread = newWidth; // not thread safe, but no good way around it
			xdevice->primary_surface.height_thread = newHeight;
			xdevice->primary_surface.surface_base.height_nonthread = newHeight; // not thread safe, but no good way around it
			xdevice->primary_surface.surface_base.vheight_nonthread = newHeight; // not thread safe, but no good way around it
			rxbxFatalHResultErrorf(xdevice, hrDXGIResult, "ResizeBuffers failed", "%d x %d", newWidth, newHeight);
			rxbxGetPrimarySurfaces(xdevice);
			rxbxResetDeviceState(xdevice);
			rxbxSetSurfaceActiveDirectSimple(xdevice, &xdevice->primary_surface);

			if (newFullScreen)
				rxbxClipCursorToFullscreen(xdevice);
			else
				rxbxUnClipCursor(xdevice);

			// TODO: store new settings in the swapdesc?
			// TODO: handle full screen toggle?  Seems to work already?
		}
	} else {
		xdevice->d3d_present_params->BackBufferWidth = newWidth;
		xdevice->d3d_present_params->BackBufferHeight = newHeight;
		xdevice->primary_surface.width_thread = newWidth;
		xdevice->primary_surface.surface_base.width_nonthread = newWidth; // not thread safe, but no good way around it
		xdevice->primary_surface.surface_base.vwidth_nonthread = newWidth; // not thread safe, but no good way around it
		xdevice->primary_surface.height_thread = newHeight;
		xdevice->primary_surface.surface_base.height_nonthread = newHeight; // not thread safe, but no good way around it
		xdevice->primary_surface.surface_base.vheight_nonthread = newHeight; // not thread safe, but no good way around it

		if (newFullScreen)
		{
			xdevice->d3d_present_params->BackBufferFormat = display_mode->d3d9_mode.Format;
			xdevice->d3d_present_params->FullScreen_RefreshRateInHz = display_mode->base.refresh_rate;
			xdevice->d3d_present_params->SwapEffect = D3DSWAPEFFECT_DISCARD;
			xdevice->d3d_present_params->Windowed = FALSE;
		} else {
			if (xdevice->display_thread.fullscreen)
				xdevice->display_thread.windowed_fullscreen = 0; // Tied to line in rwinSetSizeDirect
			xdevice->d3d_present_params->BackBufferFormat = D3DFMT_UNKNOWN;
			xdevice->d3d_present_params->FullScreen_RefreshRateInHz = 0;
			xdevice->d3d_present_params->SwapEffect = D3DSWAPEFFECT_COPY;
			xdevice->d3d_present_params->Windowed = TRUE;
		}
		if (flags == RDRRESIZE_PRECOMMIT_START)
		{
			xdevice->doing_fullscreen_toggle = 1;
		} else if (flags == RDRRESIZE_PRECOMMIT_END)
		{
			xdevice->doing_fullscreen_toggle = 0;
		}
		if (!xdevice->doing_fullscreen_toggle && flags!=RDRRESIZE_PRECOMMIT_SKIPONE) {
			rxbxSetDeviceLost(xdevice, RDR_FORCE_RESET_SETTINGS_CHANGED);
			rxbxReactivateDirect(xdevice);
		}
	}

	if (flags == RDRRESIZE_PRECOMMIT_END || flags == RDRRESIZE_DEFAULT)
	{
		if (dimensions->fullscreen)
			rwinDisplayParamsSetToCoverMonitorForDeviceWindow(&xdevice->device_base, xdevice->hWindow, dimensions);
		else
		if (dimensions->maximize || dimensions->windowed_fullscreen)
			rwinDisplayParamsSetToCoverMonitorForDeviceWindow(&xdevice->device_base, xdevice->hWindow, dimensions);

		// Windowed fullscreen: Use the size of the active monitor
		xdevice->device_width = newWidth = dimensions->width;
		xdevice->device_height = newHeight = dimensions->height;
		xdevice->display_thread = *dimensions;
		xdevice->display_thread.width = xdevice->device_width;
		xdevice->display_thread.height = xdevice->device_height;
		rxbxDeviceNotifyMainThreadSettingsChanged(xdevice);
	}
}

void rxbxDestroyAllSecondarySurfacesDirect(RdrDeviceDX *device)
{
	int surfaceIndex, countSurfaces = eaSize( &device->surfaces );

	for ( surfaceIndex = 1; surfaceIndex < countSurfaces; ++surfaceIndex )
	{
		rxbxSurfaceCleanupDirect(device, device->surfaces[ surfaceIndex ], NULL);
	}
}

//////////////////////////////////////////////////////////////////////////
// occlusion queries

MP_DEFINE(RxbxOcclusionQuery);

void rxbxProcessQueriesDirect(RdrDeviceDX *device, bool flush)
{
	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	assert(!device->outstanding_queries_tail == !device->outstanding_queries);
	while (device->outstanding_queries) {
		LARGE_INTEGER query_result = { 0 };
		RxbxOcclusionQuery *query = device->outstanding_queries;

		HRESULT hr = rxbxQueryGetOcclusionData(device, query->query, &query_result, flush);
		// DX11TODO
		assert(!query_result.HighPart);

		TRACE_QUERY(&device->device_base, "Query GetData %p %p 0x%08x\n", query->rdr_query, query->query.typeless, hr);

		if (hr == S_OK) {
			if (query->pshader) {

				if (query->pshader->pixel_count_frame != query->frame) {
					query->pshader->pixel_count_last = query->pshader->pixel_count;
					query->pshader->pixel_count = 0;
					query->pshader->pixel_count_frame = query->frame;
				}
				query->pshader->pixel_count+=query_result.LowPart;
			}
			TRACE_QUERY(&device->device_base, "Query ready %p %p %d\n", query->rdr_query, query->query.typeless, query_result.LowPart);

			if (query->rdr_query) {
				query->rdr_query->pixel_count = query_result.LowPart;
				query->rdr_query->data_ready = true;
				query->rdr_query->frame_ready = device->device_base.frame_count;
			}
			// Remove from outstanding queries queue
			if (query->next) {
				device->outstanding_queries = query->next;
			} else {
				assert(device->outstanding_queries_tail == query);
				device->outstanding_queries = device->outstanding_queries_tail = NULL;
			}
			// Add to free queue
			query->next = device->free_queries;
			device->free_queries = query;
		} else if (!flush || hr != S_FALSE) {
			break;
        } else {
        }
	}
}

void rxbxFlushQueriesDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	// stall the GPU to get back occlusion query results immediately
	rxbxProcessQueriesDirect(device, true);
}

void rxbxFreeQueryDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	RxbxOcclusionQuery *query, *next_query, *last_query = NULL;
	RdrOcclusionQueryResult *rdr_query;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	wtCmdRead(packet, &rdr_query, sizeof(RdrOcclusionQueryResult *));

	if (!rdr_query)
		return;

	// Find any outstanding queries referencing this result struct and remove them
	for (query = device->outstanding_queries; query; query = next_query) {
		next_query = query->next;

		if (query->rdr_query == rdr_query) {
			LARGE_INTEGER query_result;

			// Flush the query so when it is reused it doesn't return the wrong result
			rxbxQueryGetOcclusionData(device, query->query, &query_result, true);
			TRACE_QUERY(&device->device_base, "Flushing query %p\n", query->query.typeless);

            // Remove from outstanding queries queue
			if (last_query) {
				last_query->next = next_query;
			} else {
				device->outstanding_queries = next_query;
			}

			// Add to free queue
			query->next = device->free_queries;
			device->free_queries = query;
		} else {
			last_query = query;
		}
	}

	// fixup tail
	device->outstanding_queries_tail = last_query;

	// free data
	free(rdr_query);
}

void rxbxFinishOcclusionQueryDirect(RdrDeviceDX *device, RxbxOcclusionQuery *query)
{
	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (query) {
		rxbxQueryEnd(device, query->query);

		TRACE_QUERY(&device->device_base, "Query finish %p %p\n", query->rdr_query, query->query.typeless);

        // Add to end of list
		query->next = NULL;
		assert(!device->outstanding_queries_tail == !device->outstanding_queries);
		if (device->outstanding_queries_tail)
			device->outstanding_queries_tail->next = query;
		else 
			device->outstanding_queries = query;
		device->outstanding_queries_tail = query;
		if (query == device->last_pshader_query)
			device->last_pshader_query = NULL;
        if (query == device->last_rdr_query)
			device->last_rdr_query = NULL;
	}
}

HRESULT rxbxCreateQuery(RdrDeviceDX *device, D3DQUERYTYPE query_type, RdrQueryObj *query)
{
	if (rdr_state.disableOcclusionQueries)
	{
        if (query)
			query->typeless = NULL;
		return S_FALSE;
	} else {
		HRESULT hr;
		if (device->d3d11_device)
		{
			D3D11_QUERY_DESC desc = {0};
			// evidently DX9 types are also our abstracted types
			switch (query_type)
			{
				xcase D3DQUERYTYPE_EVENT:
					desc.Query = D3D11_QUERY_EVENT;
				xcase D3DQUERYTYPE_OCCLUSION:
					desc.Query = D3D11_QUERY_OCCLUSION;
				xcase D3DQUERYTYPE_TIMESTAMP:
					desc.Query = D3D11_QUERY_TIMESTAMP;
				xcase D3DQUERYTYPE_TIMESTAMPDISJOINT:
					desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
				xdefault:
					assert(0);
			}
			hr = ID3D11Device_CreateQuery(device->d3d11_device, &desc, query ? &query->query_d3d11 : NULL);
		} else {
			hr = IDirect3DDevice9_CreateQuery(device->d3d_device, query_type, query ? &query->query_d3d9 : NULL);
		}
		if (FAILED(hr))
			return hr;

        rxbxLogCreateQuery(device, *query);
		return hr;
	}
}

void rxbxStartOcclusionQueryDirect(RdrDeviceDX *device, RxbxPixelShader *pshader, RdrOcclusionQueryResult *rdr_query)
{
	RxbxOcclusionQuery *query;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	MP_CREATE(RxbxOcclusionQuery, 256);
	if (!device->free_queries) {
		query = MP_ALLOC(RxbxOcclusionQuery);
		if (FAILED(rxbxCreateQuery(device, D3DQUERYTYPE_OCCLUSION, &query->query))) {
			MP_FREE(RxbxOcclusionQuery, query);
			query = NULL;
			if (rdr_query) {
				rdr_query->failed = true;
				rdr_query->data_ready = true;
				rdr_query->frame_ready = device->device_base.frame_count;
			}
		}
	} else {
		query = device->free_queries;
		device->free_queries = query->next;
	}
	if (query) {
		query->pshader = pshader;
		query->rdr_query = rdr_query;
		query->frame = device->frame_count_xdevice;

		TRACE_QUERY(&device->device_base, "Query start %p %p %p\n", query->rdr_query, query->query.typeless, device->device_state.targets_driver[0].typeless_surface);

		if (device->d3d11_device)
		{
			ID3D11DeviceContext_Begin(device->d3d11_imm_context, query->query.asynch_d3d11);
		} else {
			CHECKX(IDirect3DQuery9_Issue(query->query.query_d3d9, D3DISSUE_BEGIN));
		}
		if (pshader)
		{
			assert(!device->last_pshader_query);
			device->last_pshader_query = query;
		}
		if (rdr_query)
		{
			assert(!device->last_rdr_query);
			device->last_rdr_query = query;
			rdr_query->draw_calls = 0;
		}
	}
}

static void rxbxDestroyProfilingQueries(RdrDeviceDX *device);

static void rxbxCreateQueries(RdrDeviceDX *device)
{
	int i;

	if (!device->flush_query.typeless)
		rxbxCreateQuery(device, D3DQUERYTYPE_EVENT, &device->flush_query);

	for (i=0; i<ARRAY_SIZE(device->sync_query); i++)
	{
		if (!device->sync_query[i].query.typeless)
			rxbxCreateQuery(device, D3DQUERYTYPE_EVENT, &device->sync_query[i].query); // This can fail on old cards
	}
	
	// create profiling queries
	device->bProfilingQueries = true;
	for (i=0; i<ARRAY_SIZE(device->disjoint_query); i++)
	{
		if (!device->disjoint_query[i].typeless)
		{
			HRESULT hrCreateQuery = rxbxCreateQuery(device, D3DQUERYTYPE_TIMESTAMPDISJOINT, &device->disjoint_query[i]);
			if (FAILED(hrCreateQuery))
			{
				memlog_printf(NULL, "TIMESTAMPDISJOINT query create %d failed hr = %s", i, rxbxGetStringForHResult(hrCreateQuery));
				device->bProfilingQueries = false;
				break;
			}
		}
	}

	if (device->bProfilingQueries)
	{
		for (i=0; i<ARRAY_SIZE(device->profiling_query); i++)
		{
			if (!device->profiling_query[i].query.typeless)
			{
				HRESULT hrCreateQuery = rxbxCreateQuery(device, D3DQUERYTYPE_TIMESTAMP, &device->profiling_query[i].query);
				if (FAILED(hrCreateQuery))
				{
					memlog_printf(NULL, "TIMESTAMP query create %d failed hr = %s", i, rxbxGetStringForHResult(hrCreateQuery));
					device->bProfilingQueries = false;
					break;
				}
			}
		}
	}

	if (!device->bProfilingQueries)
		rxbxDestroyProfilingQueries(device);
}

static void rxbxDestroyProfilingQueries(RdrDeviceDX *device)
{
	int i;
	device->bProfilingQueries = false;
	for (i=0; i<ARRAY_SIZE(device->disjoint_query); i++)
	{
		if (device->disjoint_query[i].typeless)
		{
			rxbxReleaseQuery(device, device->disjoint_query[i]);
			device->disjoint_query[i].typeless = NULL;
		}
	}

	for (i=0; i<ARRAY_SIZE(device->profiling_query); i++)
	{
		if (device->profiling_query[i].query.typeless)
		{
			rxbxReleaseQuery(device, device->profiling_query[i].query);
			device->profiling_query[i].query.typeless = NULL;
			device->profiling_query[i].bIssued = false;
		}
	}
}

static void rxbxDestroyQueries(RdrDeviceDX *device)
{
	int i;

	if (device->flush_query.typeless)
	{
		rxbxReleaseQuery(device, device->flush_query);
		device->flush_query.typeless = NULL;
	}

	for (i=0; i<ARRAY_SIZE(device->sync_query); i++)
	{
		if (device->sync_query[i].query.typeless)
		{
			rxbxReleaseQuery(device, device->sync_query[i].query);
			device->sync_query[i].query.typeless = NULL;
			device->sync_query[i].bIssued = false;
			device->sync_query[i].bSyncStuck = false;
			device->sync_query[i].frameStuck = 0;
		}
	}

	rxbxDestroyProfilingQueries(device);

	while (device->free_queries)
	{
		RxbxOcclusionQuery *next = device->free_queries->next;
		rxbxReleaseQuery(device, device->free_queries->query);
		MP_FREE(RxbxOcclusionQuery, device->free_queries);
		device->free_queries = next;
	}

	assert(!device->outstanding_queries_tail == !device->outstanding_queries);
	while (device->outstanding_queries)
	{
		RxbxOcclusionQuery *next = device->outstanding_queries->next;
		if (device->outstanding_queries->rdr_query)
			device->outstanding_queries->rdr_query->failed = true;
		rxbxReleaseQuery(device, device->outstanding_queries->query);
		MP_FREE(RxbxOcclusionQuery, device->outstanding_queries);
		device->outstanding_queries = next;
	}
	device->outstanding_queries_tail = NULL;

	assert(!device->last_pshader_query);
	device->last_pshader_query = NULL;
	assert(!device->last_rdr_query);
	device->last_rdr_query = NULL;
}

//////////////////////////////////////////////////////////////////////////

#if _XBOX
#define D3DUSAGE_DYNAMIC 0
#define D3DLOCK_DISCARD 0
#endif

static int temp_vbo_use_frame_delay;
static int disable_temp_vbo_sharing;

// adds a frame delay between when a temp VBO is used and when it can be used again
AUTO_CMD_INT(temp_vbo_use_frame_delay, rdrTempVBOUseFrameDelay) ACMD_CATEGORY(DEBUG);

// disables sharing of temp VBOs by different draw calls
AUTO_CMD_INT(disable_temp_vbo_sharing, rdrDisableTempVBOSharing) ACMD_CATEGORY(DEBUG);

__forceinline HRESULT rxbxLockVBO(RdrDeviceDX *device, RdrVBOMemoryDX *vbo_chunk, void **buffer_mem_ptr, int byte_count)
{
	HRESULT hr;
	if (device->d3d11_device)
	{
		D3D11_MAPPED_SUBRESOURCE map_info;
		D3D11_MAP map_mode = D3D11_MAP_WRITE_NO_OVERWRITE;
		PERFINFO_AUTO_START("ID3D11DeviceContext_Map", 1);
		if (vbo_chunk->used_bytes == 0)
			map_mode = D3D11_MAP_WRITE_DISCARD;
		hr = ID3D11DeviceContext_Map(device->d3d11_imm_context, vbo_chunk->vbo.vertex_buffer_resource_d3d11,
			0, map_mode, 0, &map_info);
		if (FAILED(hr))
			*buffer_mem_ptr = NULL;
		else
			*buffer_mem_ptr = (byte*)map_info.pData + vbo_chunk->used_bytes;
		PERFINFO_AUTO_STOP();
	}
	else
	{
		PERFINFO_AUTO_START("IDirect3DVertexBuffer9_Lock", 1);
		hr = IDirect3DVertexBuffer9_Lock(vbo_chunk->vbo.vertex_buffer_d3d9, vbo_chunk->used_bytes, byte_count, buffer_mem_ptr, 
			(vbo_chunk->used_bytes == 0)?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE);
		PERFINFO_AUTO_STOP();
	}
	return hr;
}

__forceinline void rxbxUnlockVBO(RdrDeviceDX *device, RdrVBOMemoryDX *vbo_chunk)
{
	if (device->d3d11_device)
		ID3D11DeviceContext_Unmap(device->d3d11_imm_context, vbo_chunk->vbo.vertex_buffer_resource_d3d11, 0);
	else
		CHECKX(IDirect3DVertexBuffer9_Unlock(vbo_chunk->vbo.vertex_buffer_d3d9));
}

HRESULT rxbxAppendVBOMemory(RdrDeviceDX *device, RdrVBOMemoryDX *vbo_chunk, const void *data, int byte_count)
{
	HRESULT hr;
	void * buffer_mem = NULL;
	hr = rxbxLockVBO(device, vbo_chunk, &buffer_mem, byte_count);
	if (!FAILED(hr))
	{
		memcpy_writeCombined(buffer_mem, data, byte_count);
		rxbxUnlockVBO(device, vbo_chunk);
	}
	return hr;
}

bool rxbxAllocTempVBOMemory(RdrDeviceDX *device, const void *data, int byte_count, RdrVertexBufferObj *vbo_out, int *vbo_offset_out, bool fatal)
{
	RdrVBOMemoryDX *vbo_chunk = NULL;
	HRESULT hr;
	int i, cur_size_diff = 0, vbo_count = eaSize(&device->vbo_memory);

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < vbo_count; ++i)
	{
		RdrVBOMemoryDX *chunk = device->vbo_memory[i];
		int available_size = chunk->size_bytes - chunk->used_bytes;
		int size_diff = available_size - byte_count;

		if (!chunk->used_bytes && chunk->last_frame_used + temp_vbo_use_frame_delay >= device->frame_count_xdevice)
			continue;

		if (chunk->used_bytes && disable_temp_vbo_sharing)
			continue;

#if _XBOX
		// on the xbox we can't tell the driver to discard the previous data, so we have to wait until the VBO is available
		if (chunk->used_bytes == 0)
		{
			// the VBO can't be bound while we are modifying it or while we are checking if it is busy
			if (IDirect3DVertexBuffer9_IsSet(chunk->vbo, device->d3d_device))
				rxbxNotifyVertexBufferFreed(device, chunk->vbo);

			if (IDirect3DVertexBuffer9_IsBusy(chunk->vbo))
				continue;

			if (size_diff >= 0 && size_diff <= 64)
			{
				// close enough, stop looking
				vbo_chunk = chunk;
				break;
			}
			else if (size_diff > 64 && (!vbo_chunk || size_diff < cur_size_diff))
			{
				// find best fit
				cur_size_diff = size_diff;
				vbo_chunk = chunk;
			}
		}

		// the xbox doesn't let us sub-lock a VBO, so keep looking
#else
		if (size_diff >= 0 && size_diff <= 64)
		{
			vbo_chunk = chunk;
			break;
		}
		else if (size_diff > 64 && (!vbo_chunk || size_diff > cur_size_diff))
		{
			// find worst fit
			cur_size_diff = size_diff;
			vbo_chunk = chunk;
		}
#endif
	}

#if !_XBOX
	// the VBO can't be bound while we are modifying it
	if (vbo_chunk)
		rxbxNotifyVertexBufferFreed(device, vbo_chunk->vbo);
#endif

    if (!vbo_chunk)
	{
		vbo_chunk = calloc(1, sizeof(*vbo_chunk));
		vbo_chunk->size_bytes = pow2(byte_count);
#if !_XBOX
		MAX1(vbo_chunk->size_bytes, 65536); // don't make anything smaller than a 64 kb page; on the xbox we don't care because we can't subupdate
#endif
		vbo_chunk->device = device;

		PERFINFO_AUTO_START("IDirect3DDevice9_CreateVertexBuffer", 1);
		hr = rxbxCreateVertexBuffer(device, vbo_chunk->size_bytes, BUF_DYNAMIC, &vbo_chunk->vbo);
		PERFINFO_AUTO_STOP();
		if (FAILED(hr))
		{
			if (fatal)
				rxbxFatalHResultErrorf(device, hr, "Creating vertex buffer", "");
			SAFE_FREE(vbo_chunk);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:TempVBO", 1, vbo_chunk->size_bytes);

		eaPush(&device->vbo_memory, vbo_chunk);
	}

	*vbo_out = vbo_chunk->vbo;
	*vbo_offset_out = vbo_chunk->used_bytes;

	hr = rxbxAppendVBOMemory(device, vbo_chunk, data, byte_count);
	if (FAILED(hr))
	{
		if (fatal)
			rxbxFatalHResultErrorf(device, hr, "Locking vertex buffer", "");
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	ADD_MISC_COUNT(byte_count, "bytes copied");

	vbo_chunk->used_bytes += byte_count;
	vbo_chunk->last_frame_used = device->frame_count_xdevice;

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

__forceinline HRESULT rxbxLockIBO(RdrDeviceDX *device, RdrIBOMemoryDX *ibo_chunk, void **buffer_mem_ptr, int byte_count)
{
	HRESULT hr;
	PERFINFO_AUTO_START("rxbxIndexBufferLockWrite", 1);
	*buffer_mem_ptr = rxbxIndexBufferLockWrite(device, ibo_chunk->ibo, ibo_chunk->used_bytes==0 ? RDRLOCK_DISCARD : RDRLOCK_NOOVERWRITE, ibo_chunk->used_bytes, byte_count, &hr);
	PERFINFO_AUTO_STOP();
	return hr;
}

HRESULT rxbxAppendIBOMemory(RdrDeviceDX *device, RdrIBOMemoryDX *ibo_chunk, const void *data, int byte_count)
{
	HRESULT hr;
	void * buffer_mem = NULL;
	hr = rxbxLockIBO(device, ibo_chunk, &buffer_mem, byte_count);
	if (!FAILED(hr))
	{
		memcpy_writeCombined(buffer_mem, data, byte_count);
		rxbxIndexBufferUnlock(device, ibo_chunk->ibo);
	}
	return hr;
}

bool rxbxAllocTempIBOMemory(RdrDeviceDX *device, const void *data, int indices, bool b32Bit, RdrIndexBufferObj *ibo_out, int *ibo_offset_out, bool fatal)
{
	RdrIBOMemoryDX *ibo_chunk = NULL;
	HRESULT hr;
	int i, cur_size_diff = 0, ibo_count;
	int byte_count;
	RdrIBOMemoryDX ***chunks;

	PERFINFO_AUTO_START_FUNC();

	if (b32Bit)
	{
		byte_count = indices * sizeof(U32);
		chunks = &device->ibo32_memory;
	}
	else
	{
		byte_count = indices * sizeof(U16);
		chunks = &device->ibo_memory;
	}
	ibo_count = eaSize(chunks);

	for (i = 0; i < ibo_count; ++i)
	{
		RdrIBOMemoryDX *chunk = (*chunks)[i];
		int available_size = chunk->size_bytes - chunk->used_bytes;
		int size_diff = available_size - byte_count;

		if (!chunk->used_bytes && chunk->last_frame_used + temp_vbo_use_frame_delay >= device->frame_count_xdevice)
			continue;

		if (chunk->used_bytes && disable_temp_vbo_sharing)
			continue;

#if _XBOX
		// on the xbox we can't tell the driver to discard the previous data, so we have to wait until the IBO is available
		if (chunk->used_bytes == 0)
		{
			// the VBO can't be bound while we are modifying it or while we are checking if it is busy
			if (IDirect3DIndexBuffer9_IsSet(chunk->ibo, device->d3d_device))
				rxbxNotifyIndexBufferFreed(device, chunk->ibo);

			if (IDirect3DIndexBuffer9_IsBusy(chunk->ibo))
				continue;

			if (size_diff >= 0 && size_diff <= 64)
			{
				// close enough, stop looking
				ibo_chunk = chunk;
				break;
			}
			else if (size_diff > 64 && (!ibo_chunk || size_diff < cur_size_diff))
			{
				// find best fit
				cur_size_diff = size_diff;
				ibo_chunk = chunk;
			}
		}

		// the xbox doesn't let us sub-lock a VBO, so keep looking
#else
		if (size_diff >= 0 && size_diff <= 64)
		{
			ibo_chunk = chunk;
			break;
		}
		else if (size_diff > 64 && (!ibo_chunk || size_diff > cur_size_diff))
		{
			// find worst fit
			cur_size_diff = size_diff;
			ibo_chunk = chunk;
		}
#endif
	}

#if !_XBOX
	// the VBO can't be bound while we are modifying it
	if (ibo_chunk)
		rxbxNotifyIndexBufferFreed(device, ibo_chunk->ibo);
#endif

	if (!ibo_chunk)
	{
		ibo_chunk = calloc(1, sizeof(*ibo_chunk));
		ibo_chunk->size_bytes = pow2(byte_count);
#if !_XBOX
		MAX1(ibo_chunk->size_bytes, 65536); // don't make anything smaller than a 64 kb page; on the xbox we don't care because we can't subupdate
#endif
		ibo_chunk->device = device;

		PERFINFO_AUTO_START("rxbxCreateIndexBuffer", 1);
		// DX11TODO - DJR - figure out if/how we are leaking these
		hr = rxbxCreateIndexBuffer(device, ibo_chunk->size_bytes, b32Bit ? BUF_32BIT_INDEX | BUF_DYNAMIC : BUF_DYNAMIC, &ibo_chunk->ibo);
		PERFINFO_AUTO_STOP();
		if (FAILED(hr))
		{
			if (fatal)
				rxbxFatalHResultErrorf(device, hr, "Creating index buffer", "");
			SAFE_FREE(ibo_chunk);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:TempIBO", 1, ibo_chunk->size_bytes);

		eaPush(chunks, ibo_chunk);
	}

	*ibo_out = ibo_chunk->ibo;
	*ibo_offset_out = ibo_chunk->used_bytes;

	hr = rxbxAppendIBOMemory(device, ibo_chunk, data, byte_count);
	if (FAILED(hr))
	{
		if (fatal)
			rxbxFatalHResultErrorf(device, hr, "Locking index buffer", "");
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	ADD_MISC_COUNT(byte_count, "bytes copied");

	ibo_chunk->used_bytes += byte_count;
	ibo_chunk->last_frame_used = device->frame_count_xdevice;

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

#if !PLATFORM_CONSOLE
// Keep this at the end of the file because the macros screw up Visual Assist
static void allocWindowMessageTimers(void)
{
	if(!inittimers)
	{
		int i;

		inittimers = 1;

		#define SET_NAME(x) if(x >= 0 && x < TIMER_COUNT)timers[x].name = #x;
		SET_NAME(WM_SYSCOMMAND);
		SET_NAME(WM_SETCURSOR);
		SET_NAME(WM_GETMINMAXINFO);
		SET_NAME(WM_WINDOWPOSCHANGED);
		SET_NAME(WM_SIZE);
		SET_NAME(WM_ACTIVATEAPP);
		SET_NAME(WM_ACTIVATE);
		SET_NAME(WM_SETFOCUS);
		SET_NAME(WM_CLOSE);
		SET_NAME(WM_DESTROY);
		SET_NAME(WM_QUIT);
		SET_NAME(WM_CHAR);
		SET_NAME(WM_KEYDOWN);
		SET_NAME(WM_LBUTTONDBLCLK);
		SET_NAME(WM_NCLBUTTONDBLCLK);
		SET_NAME(WM_KEYUP);
		SET_NAME(WM_SYSKEYUP);
		SET_NAME(WM_SYSKEYDOWN);
		SET_NAME(WM_DEADCHAR);
		SET_NAME(WM_SYSCHAR);
		SET_NAME(WM_SYSDEADCHAR);
		SET_NAME(WM_KEYLAST);
		#undef SET_NAME
		
		for(i = 0; i < TIMER_COUNT; i++)
		{
			if(!timers[i].name)
			{				
				char buffer[100];
				
				STR_COMBINE_BEGIN(buffer);
				STR_COMBINE_CAT("WM_(");
				STR_COMBINE_CAT_D(i);
				STR_COMBINE_CAT(")");
				STR_COMBINE_END(buffer);
				
				timers[i].name = strdup(buffer);
			}
		}
	}
}
#endif
