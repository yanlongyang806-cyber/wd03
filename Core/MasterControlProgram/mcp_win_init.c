//#include <winsock2.h>
#include <wininclude.h>
#include <mmsystem.h>
#include <winuser.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "cmdparse.h"
#include <direct.h>
#include <process.h>
#include "mastercontrolprogram.h"
#include "file.h"


// SetLayeredWindowAttributes
typedef BOOL (__stdcall *tSetLayeredWindowAttributes)( 
	HWND hwnd,
    COLORREF crKey,
    BYTE bAlpha,
    DWORD dwFlags);
tSetLayeredWindowAttributes pSetLayeredWindowAttributes = NULL;
HINSTANCE hLayeredWindowDll = 0;	// The dll to layeredwindow from (USER)


// splash window stuff
static HWND		hlogo;			// splash screen handle
static int g_quitlogo;			// signal to exit the logo screen even if it hasn't been created
static int g_show_splash = 1;

char windowName[100] = "MCP";
char className[100] = "MCP";

////////////////////////////////////////////////////////////////////// splash logo

#define SPLASH_ALPHASTART	10 // the increment we start at
#define SPLASH_ALPHAINC		100 // number of increments of alpha we will go through
#define SPLASH_ALPHATIME	20 // milliseconds for each alpha increment

#ifndef LWA_ALPHA // Not defined in Win9x headers
#	define LWA_ALPHA               0x00000002
#endif
#ifndef WS_EX_LAYERED
#	define WS_EX_LAYERED           0x00080000
#endif

LONG WINAPI DefWindowProc_timed( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	LONG ret;
	PERFINFO_AUTO_START("DefWindowProc", 1);
	ret = DefWindowProc(hWnd, uMsg, wParam, lParam);
	PERFINFO_AUTO_STOP();
	return ret;
}

static LONG WINAPI SplashProc ( HWND    hWnd,
                          UINT    uMsg,
                          WPARAM  wParam,
                          LPARAM  lParam ) 
{
static HBITMAP hbmLogo;
static int width;
static int height;
static int curalpha = 0;

	switch (uMsg)
	{
	case WM_CREATE:
		{
			BITMAP bmpdata;
			int screenwidth;
			int screenheight;
			int opacity;

		
			int iBmpSize;

			char *pBuffer = fileAllocWithRetries("server/MCP/loading.bmp", &iBmpSize, 5);

			FILE *pOutFile;

			char *pTempFileName = NULL;
			
	
			assertmsg(pBuffer, "Couldn't load loading.bmp");

		

			pOutFile = GetTempBMPFile(&pTempFileName);
			fwrite(pBuffer, iBmpSize, 1, pOutFile);
			fclose(pOutFile);
			free(pBuffer);

			hbmLogo = LoadImage(0, pTempFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

			assertmsgf(hbmLogo, "Couldn't load %s", pTempFileName);

			DeleteFile(pTempFileName);
			estrDestroy(&pTempFileName);

			GetObject(hbmLogo, sizeof(BITMAP), &bmpdata);
			width = bmpdata.bmWidth > 0? bmpdata.bmWidth : bmpdata.bmWidth * -1;
			height = bmpdata.bmHeight > 0? bmpdata.bmHeight : bmpdata.bmHeight * -1;

			// resize to fit
			screenwidth = GetSystemMetrics(SM_CXSCREEN);
			screenheight = GetSystemMetrics(SM_CYSCREEN);
			MoveWindow(hWnd, (screenwidth - width) / 2, (screenheight - height) /2, width, height, FALSE);


			// see if we can get SetLayeredWindowAttributes
			hLayeredWindowDll = LoadLibrary( "user32.dll" );
			if (hLayeredWindowDll)
			{
				pSetLayeredWindowAttributes = (tSetLayeredWindowAttributes) GetProcAddress(hLayeredWindowDll, "SetLayeredWindowAttributes");
			}

			// set layered style and timer
			if (pSetLayeredWindowAttributes)
			{
				SetWindowLong(hWnd, GWL_EXSTYLE, 
					GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
				curalpha = SPLASH_ALPHASTART;
				opacity = (curalpha * 255) / SPLASH_ALPHAINC;
				pSetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
				SetTimer(hWnd, 0, SPLASH_ALPHATIME, 0);
			}

			ShowWindow(hWnd, SW_SHOWNORMAL);

			return 1;
		}

	case WM_TIMER:
		{
			if (pSetLayeredWindowAttributes)
			{
				curalpha++;
				if (curalpha < SPLASH_ALPHAINC)
				{
					int opacity = (curalpha * 255) / SPLASH_ALPHAINC;
					pSetLayeredWindowAttributes(hWnd, 0, opacity, LWA_ALPHA);
					SetTimer(hWnd, 0, SPLASH_ALPHATIME, 0);
				}
				else
				{
					pSetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
				}
			}
			return 0;
		}

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc, hdcmem;
			HBITMAP oldbitmap;

			hdc = BeginPaint(hWnd, &ps);
			hdcmem = CreateCompatibleDC(hdc);
			oldbitmap = (HBITMAP)SelectObject(hdcmem, hbmLogo);
			BitBlt(hdc, 0, 0, width, height, hdcmem, 0, 0, SRCCOPY);
			SelectObject(hdcmem, oldbitmap);
			DeleteDC(hdcmem);
			EndPaint(hWnd, &ps);
		}
		return 1;

	case WM_DESTROY:
		DeleteObject(hbmLogo);
		PostQuitMessage(0);
		break;

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
	case WM_SYSKEYDOWN:
	case WM_LBUTTONDBLCLK:
	case WM_DEADCHAR:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_KEYLAST:
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MOUSEMOVE:
		break;

	}
	return DefWindowProc_timed(hWnd, uMsg, wParam, lParam);
}

void RegisterSplashWindow()
{
	WNDCLASS splashwc;

	splashwc.hInstance	   = ghInstance;	
    splashwc.style         = CS_OWNDC;
    splashwc.lpfnWndProc   = (WNDPROC)SplashProc;
    splashwc.cbClsExtra    = 0;
    splashwc.cbWndExtra    = 0;
	
	splashwc.hIcon         = 0;
	
    splashwc.hCursor       = 0;
    splashwc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    splashwc.lpszMenuName  = "MCP";
    splashwc.lpszClassName = "MCP";

    RegisterClass( &splashwc );
}

// run the splash screen during startup
void SplashThread(void* data)
{
	MSG msg;

	// get the window started
	hlogo = CreateWindow("MCP", windowName, WS_POPUP | WS_BORDER, 
		100, 100, 400, 200, NULL, NULL, ghInstance, NULL);

	// run the message pump
	while (!g_quitlogo && GetMessage(&msg, 0, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyWindow(hlogo);
}

 void CreateSplash()
{
	g_quitlogo = 0;
	RegisterSplashWindow();
	_beginthread(SplashThread, 0, 0);
	Sleep(10); // make sure the window starts
}

 void DestroySplash()
{
	SendMessage(hlogo, WM_DESTROY, 0, 0);
	g_quitlogo = 1;
	Sleep(10); // let the window close before we go on
}

