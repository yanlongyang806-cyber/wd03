#include <wininclude.h>
#include <mmsystem.h>
#include <winuser.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <process.h>
#include "stdtypes.h"
#include "error.h"
#include "memcheck.h"
#include "sysutil.h"
#include "utils.h"
#include "timing.h"
#include "StashTable.h"
#include "strings_opt.h"

#include "mathutil.h"
#include "earray.h"
#include "file.h"
#include "StringUtil.h"

#include "ogl.h"
#include "NvPanelApi.h"
#include <gl\gl.h>
#include <gl\glu.h>

#include "../RdrDevicePrivate.h"
#include "rt_device.h"
#include "rt_surface.h"
#include "RenderLib.h"
#include "rt_cursor.h"

#include "MessageStoreUtil.h"
#include "systemspecs.h"

static char windowName[100] = "Cryptic";

#ifdef USE_NVPERFKIT
// To use this, you must compile, and then run NVAppAuth GameClient.exe, then launch it without the debugger
#pragma comment(lib, "../glh/nvperfkit/nv_perfauthMT.lib")
#include "nvperfkit/nv_perfauth.h"
#endif


//////////////////////////////////////////////////////////////////////////
// hwnd hash table

static StashTable hwnd_hash = 0;
static HWND the_hwnd = 0;
static RdrDeviceWinGL *the_device = 0;

static RdrDeviceWinGL *getDeviceForHwnd(HWND hwnd)
{
	RdrDeviceWinGL *device = 0;
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

static void hashDeviceHwnd(RdrDeviceWinGL *device)
{
	if (!hwnd_hash && !the_hwnd)
	{
		the_hwnd = device->hwnd;
		the_device = device;
	}
	else
	{
		if (!hwnd_hash)
			hwnd_hash = stashTableCreateAddress(64);
		stashAddressAddPointer(hwnd_hash, device->hwnd, device, true);
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



//////////////////////////////////////////////////////////////////////////
// Gamma

void rwglNeedGammaResetDirect(RdrDeviceWinGL *device)
{
	device->current_gamma = -100;
}

void rwglPreserveGammaRampDirect(RdrDeviceWinGL *device)
{
	if (device->primary_surface.primary.hDC && GetDeviceGammaRamp(device->primary_surface.primary.hDC, device->preserved_ramp))
		device->gamma_ramp_has_been_preserved = 1;
}

void rwglRestoreGammaRampDirect(RdrDeviceWinGL *device)
{
	if (device->primary_surface.primary.hDC && device->gamma_ramp_has_been_preserved)
		SetDeviceGammaRamp(device->primary_surface.primary.hDC, device->preserved_ramp);
}

void rwglSetGammaDirect(RdrDeviceWinGL *device, F32 gamma) 
{
	if (gamma > 0.1 && device->current_gamma != gamma )  
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

		SetDeviceGammaRamp(device->primary_surface.primary.hDC, ramp);
	}
}



//////////////////////////////////////////////////////////////////////////
// Vsync

void rwglSetVsyncDirect(RdrDeviceWinGL *device, int enable)
{
	rwglSetSurfaceActiveDirect(device, &device->primary_surface);
	if (wglSwapIntervalEXT)
		wglSwapIntervalEXT(enable?1:0);
}

//////////////////////////////////////////////////////////////////////////
// window

static LONG WINAPI DefWindowProc_timed( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	LONG ret;
	PERFINFO_AUTO_START("DefWindowProc", 1);
	ret = DefWindowProc(hWnd, uMsg, wParam, lParam);
	PERFINFO_AUTO_STOP();
	return ret;
}

static int windowMessageTimerCount;

static void startWindowMessageTimer(UINT msg)
{
	#define TIMER_COUNT 1000
	
	static struct {
		#if USE_NEW_TIMING_CODE
			PerfInfoStaticData*		piStatic;
		#else
			PerformanceInfo*		piStatic;
		#endif
		char*					name;
	}* timers;
	
	if(!timers)
	{
		int i;
		
		timers = calloc(sizeof(*timers), TIMER_COUNT);
		assert(timers);
		
		#define SET_NAME(x) if(x >= 0 && x < TIMER_COUNT)timers[x].name = #x;
		SET_NAME(WM_SYSCOMMAND);
		SET_NAME(WM_SETCURSOR);
		SET_NAME(WM_WINDOWPOSCHANGED);
		SET_NAME(WM_SIZE);
		SET_NAME(WM_ACTIVATEAPP);
		SET_NAME(WM_SETFOCUS);
		SET_NAME(WM_CLOSE);
		SET_NAME(WM_DESTROY);
		SET_NAME(WM_QUIT);
		SET_NAME(WM_CHAR);
		SET_NAME(WM_KEYDOWN);
		SET_NAME(WM_LBUTTONDBLCLK);
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
	
	if(msg < TIMER_COUNT)
	{
		PERFINFO_AUTO_START_STATIC(timers[msg].name, &timers[msg].piStatic, 1);
		
		windowMessageTimerCount++;
	}
	
	#undef TIMER_COUNT 
}

static void stopWindowMessageTimer()
{
	if(windowMessageTimerCount)
	{
		PERFINFO_AUTO_STOP();
		
		windowMessageTimerCount--;
	}
}

static LONG WINAPI MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LONG lRet = 1;
	RdrDeviceWinGL *device = getDeviceForHwnd(hWnd);
	WinMsg wmsg={0};


	if (isCrashed() || !device)
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	startWindowMessageTimer(uMsg);
	
	switch (uMsg) 
	{
		xcase WM_SYSCOMMAND:
			switch (wParam)
			{
				xcase SC_SCREENSAVE:
				case SC_MONITORPOWER:
					lRet = 0;
				xdefault:
					lRet = DefWindowProc_timed (hWnd, uMsg, wParam, lParam);
			}

		xcase WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT)
			{
				device->can_set_cursor = 1;
				rwglSetCursorDirect(device);
			}
			else
			{
				device->can_set_cursor = 0;
				lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			}

		xcase WM_WINDOWPOSCHANGED:
		{
			// Detect when the game window is being maximized/minimized/restored.
			WINDOWPOS* pos = (WINDOWPOS*)lParam;
			
			// Call DefWindowProc so that WM_SIZE gets sent.
			DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			
			if (!device->fullscreen && !device->maximized && !device->minimized)
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
		
		xcase WM_SIZE:
		{
			int newClientWidth;
			int newClientHeight;
			int size[2];
			
			// Store the new client height and width in game_state.
			//	Don't store them if they are 0.  May cause divide by 0 errors.
			newClientWidth	= LOWORD(lParam);
			newClientHeight = HIWORD(lParam);
			
			if (!device->fullscreen)
			{
				if (newClientWidth)
					device->primary_surface.width = newClientWidth;
				if (newClientHeight)
					device->primary_surface.height = newClientHeight;
			}
			
			switch(wParam)
			{
				xcase SIZE_MAXIMIZED:
					device->maximized = 1;
					device->minimized = 0;
				xcase SIZE_MINIMIZED:
					device->maximized = 0;
					device->minimized = 1;
				xcase SIZE_RESTORED:
					device->maximized = 0;
					device->minimized = 0;
					device->screen_width_restored = device->primary_surface.width;
					device->screen_height_restored = device->primary_surface.height;
			}

			size[0] = device->primary_surface.width;
			size[1] = device->primary_surface.height;
			wtQueueMsg(device->device_base.worker_thread, RDRMSG_SIZE, size, sizeof(int)*2);
		}
	    
		// Detect when the game window doesn't have focus.
		// Don't draw stuff on the "screen" if the game is inactive.
		xcase WM_ACTIVATEAPP:
			switch(wParam)
			{
				xcase 0:
					// The app is being deactivated.
					device->inactive_display = 1;
				xcase 1:
					// The app is being activated.
					device->inactive_display = 0;
			}
			
			// force gamma set
			if (wParam)
				rwglNeedGammaResetDirect(device);
			else
				rwglRestoreGammaRampDirect(device);
			
			lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
			
		xcase WM_SETFOCUS:
			rwglNeedGammaResetDirect(device);
			
		xcase WM_CLOSE:
			// do not let DefWindowProc destroy the window, we want to handle this ourselves
			lRet = 0;

		xcase WM_DESTROY:
			rwglRestoreGammaRampDirect(device);

		xcase WM_QUIT:
			// do not do DefWindowProc, just pass the message back to the handler
			lRet = 0;

		xcase WM_SYSKEYUP:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_KEYDOWN:
		case WM_CHAR:
		case WM_DEADCHAR:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
		case WM_KEYLAST:
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			lRet = 0;

		xdefault:
			lRet = DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
    }

	wmsg.timeStamp = GetMessageTime();
	wmsg.uMsg = uMsg;
	wmsg.wParam = wParam;
	wmsg.lParam = lParam;
	wtQueueMsg(device->device_base.worker_thread, RDRMSG_WINMSG, &wmsg, sizeof(wmsg));

	stopWindowMessageTimer();
    return lRet;
}

static LONG WINAPI MainWndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// This makes it possible to compile-and-continue MainWndProc.

	return MainWndProc(hWnd, uMsg, wParam, lParam);
}

//////////////////////////////////////////////////////////////////////////

static HGLRC setupGL(RdrDeviceWinGL *device, bool reinit)
{
	static PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),  // size of this pfd
			1,                              // version number
			PFD_DRAW_TO_WINDOW              // support window
			|  PFD_SUPPORT_OPENGL           // support OpenGL
			|  PFD_DOUBLEBUFFER ,           // double buffered
			PFD_TYPE_RGBA,                  // RGBA type
			32,                             // 32-bit color depth
			0, 0, 0, 0, 0, 0,               // color bits ignored
			0,                              // no alpha buffer
			0,                              // shift bit ignored
			0,                              // no accumulation buffer
			0, 0, 0, 0,                     // accum bits ignored
			24,                             // 24-bit z-buffer      
			8,                              // 8 bit stencil buffer
			0,                              // no auxiliary buffer
			PFD_MAIN_PLANE,                 // main layer
			0,                              // reserved
			0, 0, 0                         // layer masks ignored
	};


	RdrSurfaceWinGL *surface = &device->primary_surface;
	int  pixelFormat;
	HGLRC rv = 0;
	DWORD last_err;

	CHECKDEVICELOCK(device);

	PERFINFO_AUTO_START("ChoosePixelFormat", 1);
	pixelFormat = ChoosePixelFormat(surface->primary.hDC, &pfd);
	SET_FP_CONTROL_WORD_DEFAULT; // nVidia driver changes the FP consistency on load
	if ( pixelFormat ) {
		PERFINFO_AUTO_STOP_START("SetPixelFormat", 1);
		if ( SetPixelFormat(surface->primary.hDC, pixelFormat, &pfd) ) {
			if (reinit && surface->primary.glRC) {
				rv = surface->primary.glRC;
			} else {
				PERFINFO_AUTO_STOP_START("wglCreateContext", 1);
				rv = wglCreateContext(surface->primary.hDC);
			}
			if ( rv ) {
				PERFINFO_AUTO_STOP_START("wglMakeCurrent", 1);
				if ( !wglMakeCurrent(surface->primary.hDC, rv) ) {
					last_err = GetLastError();
					wglDeleteContext( rv );
					rv = 0;
				} else {
					// Success
					PERFINFO_AUTO_STOP_START("glextInitWgl", 1);
					if (!rwglInitExtensions())
					{
						wglDeleteContext( rv );
						rv = 0;
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return rv;
}

static void nvidiaBeforeSetupGL()
{
	BOOL b=0;
	DWORD dw=0;
	fNvCplGetDataInt NvCplGetDataInt;
	fNvCplIsExternalPowerConnectorAttached NvCplIsExternalPowerConnectorAttached;
	fNvCplSetDataInt NvCplSetDataInt;
	HINSTANCE hLib = LoadLibrary("NVCPL.dll");

	if (!hLib)
		return;

	NvCplGetDataInt = (fNvCplGetDataInt)GetProcAddress(hLib, "NvCplGetDataInt");
	NvCplIsExternalPowerConnectorAttached = (fNvCplIsExternalPowerConnectorAttached)GetProcAddress(hLib, "NvCplIsExternalPowerConnectorAttached");
	NvCplSetDataInt = (fNvCplSetDataInt)GetProcAddress(hLib, "NvCplSetDataInt");

	if (NvCplIsExternalPowerConnectorAttached && NvCplIsExternalPowerConnectorAttached(&b))
	{
		if (!b)
			Errorf("You don't have your power cable attached to your video card, dummy!");
	}

	// This doesn't work:
	//if (NvCplGetDataInt && NvCplSetDataInt && NvCplGetDataInt(NVCPL_API_CURRENT_AA_VALUE, &dw))
	//{
	//	b = NvCplSetDataInt(NVCPL_API_CURRENT_AA_VALUE, NVCPL_API_AA_METHOD_2X);
	//	NvCplGetDataInt(NVCPL_API_CURRENT_AA_VALUE, &dw);
	//}
	FreeLibrary(hLib);
}


static int registerWindowClass(RdrDeviceWinGL *device)
{
	static int classnum=0;

	if (device->windowClass.cbSize)
		return 1;

	sprintf_s(SAFESTR(device->windowClassName), "CrypticWindowClassGL%d", classnum++);

	device->windowClass.cbSize			= sizeof(device->windowClass);
	device->windowClass.style			= CS_OWNDC | CS_DBLCLKS;
	device->windowClass.lpfnWndProc		= (WNDPROC)MainWndProcCallback;
	device->windowClass.cbClsExtra		= 0;
	device->windowClass.cbWndExtra		= 0;
	device->windowClass.hInstance		= device->hInstance;
	device->windowClass.hCursor			= 0;
	device->windowClass.hbrBackground	= CreateSolidBrush(RGB(0,0,0));
	device->windowClass.lpszMenuName	= windowName;
	device->windowClass.lpszClassName	= device->windowClassName;

	if (!RegisterClassEx(&device->windowClass))
	{
		MessageBox(0, "Couldn't Register Window Class", device->windowClassName, MB_ICONERROR);
		ZeroStruct(&device->windowClass);
		return 0;
	}

	return 1;
}

static void rwglResizeDirect(RdrDeviceWinGL *device,int width,int height,int refreshRate,int x0,int y0,int fullscreen,int maximize)
{
	GfxResolution **supported_resolutions=0;
	GfxResolution *desktop_res=0;
	DEVMODE dm;
	DWORD	window_style;
	int useRefreshRate = 1;

	rwglProcessWindowsMessagesDirect(device);

	supported_resolutions = rdrGetSupportedResolutions(&desktop_res);

	if (refreshRate < 60)
		useRefreshRate = 0;

	if (!fullscreen)
	{
		width += GetSystemMetrics(SM_CXSIZEFRAME) * 2;
		height += GetSystemMetrics(SM_CYSIZEFRAME) * 2 + GetSystemMetrics(SM_CYSIZE) + 1;
	}
	else
	{		
		GfxResolution * gr;
		int i, j, okRes;

		okRes = 0;
		for (i = 0; i < eaSize(&supported_resolutions); i++)
		{
			gr = supported_resolutions[i];
			if (gr->width == width && gr->height == height)
			{
				if (!eaiSize(&gr->refreshRates) || !useRefreshRate)
				{
					okRes = 1;
					useRefreshRate = 0;
				}
				else
				{
					for (j = 0; j < eaiSize(&gr->refreshRates); j++)
					{
						if (gr->refreshRates[j] == refreshRate)
							okRes = 1;
					}
				}
			}
		}

		if (!okRes)
		{
			assert(supported_resolutions);
			rdrAlertMsg((RdrDevice *)device, textStd("ResolutionNotSupported", width, height, supported_resolutions[0]->width, supported_resolutions[0]->height));
			width = supported_resolutions[0]->width;
			height = supported_resolutions[0]->height;
			refreshRate = 0;
			useRefreshRate = 0;
		}

		x0 = 0;
		y0 = 0;
	}


	if (fullscreen)
	{
		int fullScreenResult, i;

		memset(&dm,0,sizeof(dm));
		dm.dmSize       = sizeof(dm);
		dm.dmPelsWidth  = width;
		dm.dmPelsHeight = height;
		dm.dmDisplayFrequency = refreshRate;
		dm.dmBitsPerPel = 32;//game_state.screendepth;
		dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | ((useRefreshRate&&refreshRate)?DM_DISPLAYFREQUENCY:0);

		fullScreenResult = ChangeDisplaySettings( &dm, CDS_FULLSCREEN );

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

			rdrAlertMsg((RdrDevice *)device, textStd("FullscreenFailure", errormsg));

			fullscreen = 0;
			width = 800;
			height = 600;
			refreshRate = 0;
			width += GetSystemMetrics(SM_CXSIZEFRAME) * 2;
			height += GetSystemMetrics(SM_CYSIZEFRAME) * 2 + GetSystemMetrics(SM_CYSIZE) + 1;
		}
		else
		{
			// successfully changed resolution
			refreshRate = dm.dmDisplayFrequency;
		}
	}

	if (!fullscreen)
	{
		EnumDisplaySettings(NULL,ENUM_CURRENT_SETTINGS,&dm);
		if (dm.dmBitsPerPel < 32)
			rdrAlertMsg((RdrDevice *)device, textStd("RunIn32BitorDie"));
	}

	window_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	if (!fullscreen)
		window_style |= WS_OVERLAPPEDWINDOW;
	SetWindowLongPtr(device->hwnd, GWL_STYLE, window_style);
	SetWindowPos(device->hwnd, HWND_TOP, x0, y0, width, height, SWP_NOZORDER | SWP_FRAMECHANGED);

	ShowWindow(device->hwnd, !fullscreen && maximize ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT );
	UpdateWindow(device->hwnd);
	SetFocus(device->hwnd);
	SetForegroundWindow(device->hwnd);

	device->refresh_rate = refreshRate;
}

void rwglCreateDirect(RdrDeviceWinGL *device, WindowCreateParams *params)
{
	RdrSurfaceWinGL *surface = &device->primary_surface;

	assert(!device->hwnd);

	// register window class
	if (!registerWindowClass(device))
		return;

	// create window
	device->hwnd = CreateWindow(device->windowClassName,
								windowName,
								WS_CLIPSIBLINGS | 
								WS_CLIPCHILDREN |
								WS_OVERLAPPEDWINDOW, 
								100, 100,
								800, 600,
								NULL,
								NULL,
								device->hInstance,
								NULL);
	if (!device->hwnd)
	{
		rdrAlertMsg((RdrDevice *)device, "Couldn't create window.");
		return;
	}
	hashDeviceHwnd(device);

	// nvidia specific setup
	if (rdr_caps.videoCardVendorID == VENDOR_NV)
		nvidiaBeforeSetupGL();

	// get window device context
	surface->primary.hDC  = GetDC(device->hwnd);
	if (!surface->primary.hDC)
	{
		rdrAlertMsg((RdrDevice *)device, "Failed to get device context.");
		rwglDestroyDirect(device);
		return;
	}

	// setup opengl render context
	surface->primary.glRC = setupGL(device, false);
	if (!surface->primary.glRC)
	{
		rdrAlertMsg((RdrDevice *)device, "Failed to create OpenGL rendering context.");
		rwglDestroyDirect(device);
		return;
	}

	rwglPreserveGammaRampDirect(device);

	rwglResizeDirect(device, params->width, params->height, params->refreshRate, params->xpos, params->ypos, params->fullscreen, params->maximize);

	rwglSetStateActive(&device->primary_surface.state, 1, device->primary_surface.width, device->primary_surface.height);
	device->primary_surface.state_inited = 1;
}

void rwglDestroyDirect(RdrDeviceWinGL *device)
{
	RdrSurfaceWinGL *surface = &device->primary_surface;

	CHECKDEVICELOCK(device);

	rwglRestoreGammaRampDirect(device);

	if (surface->primary.glRC)
	{
		rwglSetSurfaceActiveDirect(device, surface);
		rwglDestroyAllSecondarySurfacesDirect(device);
		rwglUnsetSurfaceActiveDirect(device);

		wglMakeCurrent(0,0);
		wglDeleteContext(surface->primary.glRC);
		surface->primary.glRC = 0;
	}

	if (surface->primary.hDC)
	{
		ReleaseDC(device->hwnd, surface->primary.hDC);
		surface->primary.hDC = 0;
	}

	if (device->hwnd)
	{
		removeHwnd(device->hwnd);
		DestroyWindow(device->hwnd);
		device->hwnd = 0;
	}

	if (device->fullscreen)
		ChangeDisplaySettings(NULL,0);

	ShowCursor(TRUE);

	wtQueueMsg(device->device_base.worker_thread, RDRMSG_DESTROY, 0, 0);
}

int rwglIsInactive(RdrDevice *device)
{
	return 0;
}

void rwglReactivate(RdrDevice *device)
{
}

//////////////////////////////////////////////////////////////////////////

void rwglProcessWindowsMessagesDirect(RdrDeviceWinGL *device)
{
	MSG msg;
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglSwapBufferDirect(RdrDeviceWinGL *device)
{
	CHECKDEVICELOCK(device);

	rwglSetSurfaceActiveDirect(device, &device->primary_surface);

	if (isDevelopmentMode())
	{
		HGLRC curr_glc = wglGetCurrentContext();
		assert(curr_glc == device->primary_surface.primary.glRC);
	}

	SwapBuffers(device->primary_surface.primary.hDC);
	rwglResetState();

	if (wtIsThreaded(device->device_base.worker_thread))
	{
		EnterCriticalSection(&device->device_base.frame_check);
		device->device_base.frames_buffered--;
		LeaveCriticalSection(&device->device_base.frame_check);
		if (device->device_base.frame_done_signal)
			SetEvent(device->device_base.frame_done_signal);
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglDestroyAllSecondarySurfacesDirect(RdrDeviceWinGL *device)
{
	int i;

	CHECKDEVICELOCK(device);

	for (i = 1; i < eaSize(&device->surfaces); i++)
	{
		rwglFreeSurfaceDirect(device, device->surfaces[i]);
	}
}

