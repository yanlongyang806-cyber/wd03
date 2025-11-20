#include "SimpleWindowManager.h"
#include "winUtil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "file.h"
#include "autogen/SimpleWindowManager_c_ast.h"
#include "estring.h"
#include "AppLocale.h"
#include "utilitiesLib.h"
#include "multiMon.h"
#include "sysutil.h"
#include "UTF8.h"

#define MAX_WINDOW_TYPES 32
#define SWM_POSITION_REG_VALUE_NAME "WindowPos"


AUTO_STRUCT;
typedef struct SWMSavedWindowPos
{
	int eWindowType;
	int iUserIndex;
	U32 iDlgResourceID;

	int x;
	int y;
	int width;
	int height;
} SWMSavedWindowPos;


AUTO_STRUCT;
typedef struct SWMSavedWindowPosList
{
	SWMSavedWindowPos **ppList;
} SWMSavedWindowPosList;

#if !PLATFORM_CONSOLE
SimpleWindow *spWindowsByType[MAX_WINDOW_TYPES] = { 0 };


static SWMSavedWindowPosList sWindowPosList = {0};
static SWMSavedWindowPosList sWindowPosList_previous = {0};
static char sSWMAppName[64];
static int sSWMNumMainWindows = 0;
static SimpleWindow *spCurrentlyCreatingWindow = NULL;
static bool sSWMUseRegistry = false;
static char *spSWMRegistryKey = NULL; //estring

void SimpleWindowManager_SaveWindowPosIntoStruct(SimpleWindow *pWindow)
{
	RECT rect;
	WINDOWINFO wInfo;

	if (!pWindow->pSavedWindowPos)
	{
		return;
	}

	wInfo.cbSize = sizeof(WINDOWINFO);

	GetWindowInfo(pWindow->hWnd, &wInfo);
	
	if (!(wInfo.dwStyle & WS_MINIMIZE))
	{
		GetWindowRect(pWindow->hWnd, &rect);

		pWindow->pSavedWindowPos->x = rect.left;
		pWindow->pSavedWindowPos->y = rect.top;
		pWindow->pSavedWindowPos->width = rect.right - rect.left;
		pWindow->pSavedWindowPos->height = rect.bottom - rect.top;
	}
}



void SimpleWindowManager_LoadWindowPositions(void)
{
	if (sSWMUseRegistry)
	{
		ParserReadRegistryStringified(spSWMRegistryKey, parse_SWMSavedWindowPosList, &sWindowPosList, SWM_POSITION_REG_VALUE_NAME);
	}
	else
	{
		char fullPath[CRYPTIC_MAX_PATH];
		sprintf(fullPath, "%s/%s_SWMWindowPosList.txt", fileLocalDataDir(), sSWMAppName);
		
		ParserReadTextFile(fullPath, parse_SWMSavedWindowPosList, &sWindowPosList, 0);
	}

	StructCopyAll(parse_SWMSavedWindowPosList, &sWindowPosList, &sWindowPosList_previous);
	
}

void SimpleWindowManager_SaveWindowPositions(void)
{
	int i;
	char fullPath[CRYPTIC_MAX_PATH];
	

	for (i=0; i < MAX_WINDOW_TYPES; i++)
	{
		SimpleWindow *pWindow = spWindowsByType[i];

		while (pWindow)
		{
			SimpleWindowManager_SaveWindowPosIntoStruct(pWindow);

			pWindow = pWindow->pNext;
		}
	}

	if (StructCompare(parse_SWMSavedWindowPosList, &sWindowPosList, &sWindowPosList_previous, 0, 0, 0)!=0) 
	{
		if (sSWMUseRegistry)
		{
			ParserWriteRegistryStringified(spSWMRegistryKey, parse_SWMSavedWindowPosList, &sWindowPosList, 0, 0, SWM_POSITION_REG_VALUE_NAME);
		}
		else
		{
			sprintf(fullPath, "%s/%s_SWMWindowPosList.txt", fileLocalDataDir(), sSWMAppName);
			ParserWriteTextFile(fullPath, parse_SWMSavedWindowPosList, &sWindowPosList, 0, 0);
		}
		StructCopyAll(parse_SWMSavedWindowPosList, &sWindowPosList, &sWindowPosList_previous);
	}
}


void SimpleWindowManager_Init(const char *pAppName, bool bUseRegistryForLocalSettings)
{
	sprintf(sSWMAppName, "%s", pAppName);
	sSWMUseRegistry = bUseRegistryForLocalSettings;
	if (sSWMUseRegistry)
	{
		estrPrintf(&spSWMRegistryKey, "%s", pAppName);
	}
	SimpleWindowManager_LoadWindowPositions();
}

void SimpleWindowManager_DeInit(void)
{
	int i;
	
	for (i=0; i < MAX_WINDOW_TYPES; i++)
	{
		SimpleWindow *pWindow = spWindowsByType[i];
		spWindowsByType[i] = NULL;

		while (pWindow)
		{
			SimpleWindow *pNextWindow;
			ANALYSIS_ASSUME(pWindow);
#pragma warning(suppress:6001) // /analzye " Using uninitialized memory '*pWindow'"
			pNextWindow = pWindow->pNext;
			if(pWindow->hWnd)
			{
				ShowWindow(pWindow->hWnd, SW_HIDE);
				DestroyWindow(pWindow->hWnd);
			}
			free(pWindow);
			pWindow = pNextWindow;
		}
	}
}

// Return false if the window position is not valid.
// Basically, make sure that the upper-left corner of the window is sufficiently protruding into a visible monitor.
static bool SimpleWindowManager_IsValidWindowPos(const SWMSavedWindowPos *pWindowPos)
{
	int i;
	const long protrusion = 50;	 // Amount by which the window must be protruding into the monitor.

	// Check each monitor to see if the upper-left corner of the window is within it.
	for (i=0; i<multiMonGetNumMonitors(); i++)
	{
		MONITORINFOEX info;
		multiMonGetMonitorInfo(i, &info);
		if (pWindowPos->x >= info.rcMonitor.left
			&& pWindowPos->x < info.rcMonitor.right - protrusion
			&& pWindowPos->y >= info.rcMonitor.top
			&& pWindowPos->y < info.rcMonitor.bottom - protrusion)
			return true;
	}

	return false;
}

void SimpleWindowManager_FindWindowPosForNewWindow(SimpleWindow *pWindow)
{
	int i;

	for (i=0; i < eaSize(&sWindowPosList.ppList); i++)
	{
		if (pWindow->eWindowType == sWindowPosList.ppList[i]->eWindowType
			&& pWindow->iUserIndex == sWindowPosList.ppList[i]->iUserIndex
			&& pWindow->iDlgResourceID == sWindowPosList.ppList[i]->iDlgResourceID)
		{
			WINDOWINFO wInfo;
			bool valid_pos;

			wInfo.cbSize = sizeof(WINDOWINFO);
			GetWindowInfo(pWindow->hWnd, &wInfo);

			pWindow->pSavedWindowPos = sWindowPosList.ppList[i];

			// Make sure the window's position is correct for the current monitor configuration.
			valid_pos = SimpleWindowManager_IsValidWindowPos(pWindow->pSavedWindowPos);
			if (valid_pos)
			{

				//for non-resizeable windows, don't set the width and height
				if (wInfo.dwStyle & WS_THICKFRAME)
				{
					SetWindowPos(pWindow->hWnd, HWND_NOTOPMOST, 
						pWindow->pSavedWindowPos->x,
						pWindow->pSavedWindowPos->y,
						pWindow->pSavedWindowPos->width,
						pWindow->pSavedWindowPos->height, SWP_NOZORDER);
				}
				else
				{
					RECT rect;
					GetWindowRect(pWindow->hWnd, &rect);

					SetWindowPos(pWindow->hWnd, HWND_NOTOPMOST, 
						pWindow->pSavedWindowPos->x,
						pWindow->pSavedWindowPos->y,
						rect.right - rect.left,
						rect.bottom - rect.top, SWP_NOZORDER);					
				}

				return;
			}
		}
	}

	pWindow->pSavedWindowPos = calloc(sizeof(SWMSavedWindowPos), 1);
	pWindow->pSavedWindowPos->eWindowType = pWindow->eWindowType;
	pWindow->pSavedWindowPos->iDlgResourceID = pWindow->iDlgResourceID;
	pWindow->pSavedWindowPos->iUserIndex = pWindow->iUserIndex;
	
	SimpleWindowManager_SaveWindowPosIntoStruct(pWindow);

	eaPush(&sWindowPosList.ppList, pWindow->pSavedWindowPos);
}	


SimpleWindow *SimpleWindowManager_FindWindow(int eWindowType, int iUserIndex)
{
	SimpleWindow *pWindow;

	assert(eWindowType >= 0 && eWindowType < MAX_WINDOW_TYPES);

	pWindow = spWindowsByType[eWindowType];

	while (pWindow)
	{
		if (pWindow->iUserIndex == iUserIndex)
		{
			return pWindow;
		}

		pWindow = pWindow->pNext;
	}

	return NULL;
}
SimpleWindow *SimpleWindowManager_FindWindowByType(int eWindowType)
{
	assert(eWindowType >= 0 && eWindowType < MAX_WINDOW_TYPES);

	return spWindowsByType[eWindowType];
}

void SimpleWindowManager_FindAllWindowsByType(int eWindowType, SimpleWindow ***pppOutWindows)
{
	SimpleWindow *pWindow = spWindowsByType[eWindowType];
	while (pWindow)
	{
		eaPush(pppOutWindows, pWindow);
		pWindow = pWindow->pNext;
	}
}



void SimpleWindowManager_DestroyWindow(SimpleWindow *pWindow)
{
	SimpleWindow **ppTemp = &spWindowsByType[pWindow->eWindowType];

	while (*ppTemp && (*ppTemp) != pWindow)
	{
		ppTemp = &((*ppTemp)->pNext);
	}

	if(*ppTemp)
	{
		*ppTemp = (*ppTemp)->pNext;
		free(pWindow);
	}
}


BOOL CALLBACK SimpleWindowManager_DefaultDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	SimpleWindow *pWindow = (SimpleWindow*)((intptr_t)GetWindowLongPtr(hDlg, GWLP_USERDATA));
	BOOL bRetVal = false;

	if (!pWindow)
	{
		assert(spCurrentlyCreatingWindow);
		pWindow = spCurrentlyCreatingWindow;
		spCurrentlyCreatingWindow = NULL;
		SetWindowLongPtr(hDlg, GWLP_USERDATA, (intptr_t)pWindow);
		pWindow->hWnd = hDlg;
	}

	if (pWindow->pDialogCB)
	{
		bRetVal = pWindow->pDialogCB(hDlg, iMsg, wParam, lParam, pWindow);
	}

	if (pWindow->bCloseRequested && !pWindow->bCloseBegun)
	{
		if (pWindow->bIsMain)
		{
			sSWMNumMainWindows--;
			if (sSWMNumMainWindows == 0)
			{
				PostQuitMessage(0);
			}
		}

		SimpleWindowManager_SaveWindowPosIntoStruct(pWindow);
		pWindow->bCloseBegun = true;
		DestroyWindow(hDlg);


		return false;
	}

	if(!bRetVal && !SimpleWindowManager_GetUseDialogs()) {
		return DefWindowProc(hDlg, iMsg, wParam, lParam);
	}

	return bRetVal;
}

void SimpleWindowManager_AddOrActivateWindow(int eWindowType, int iUserIndex, U32 iDlgResourceID, bool bIsMainWindow,
	SimpleWindowManager_DialogProcCallback *pDialogCB, SimpleWindowManager_TickCallback *pTickCB, void *pUserData)
{
	SimpleWindow *pNewWindow;

	assert(eWindowType >= 0 && eWindowType < MAX_WINDOW_TYPES);

	if ((pNewWindow = SimpleWindowManager_FindWindow(eWindowType, iUserIndex)))
	{
		SetActiveWindow(pNewWindow->hWnd);
		ShowWindow(pNewWindow->hWnd, SW_SHOW);
		ShowWindow(pNewWindow->hWnd, SW_RESTORE);
	}
	else
	{
		HGLOBAL h=NULL;
		DLGTEMPLATE *lpTemplate;
		WORD langid;
		HRSRC rsrc;
		
		assert(spCurrentlyCreatingWindow == NULL);

		spCurrentlyCreatingWindow = pNewWindow = calloc(sizeof(SimpleWindow), 1);

		if (bIsMainWindow)
		{
			sSWMNumMainWindows++;
		}

		// Try to load from the current locale
		langid = MAKELANGID(PRIMARYLANGID(locGetWindowsLocale(getCurrentLocale())), SUBLANG_DEFAULT);
		rsrc = FindResourceEx(GetModuleHandle(NULL), RT_DIALOG, MAKEINTRESOURCE(iDlgResourceID), langid);
		if (rsrc)
			h = LoadResource(GetModuleHandle(NULL), rsrc);
		if(!h)
		{
			// then from the default locale
			langid = MAKELANGID(PRIMARYLANGID(locGetWindowsLocale(LANGUAGE_DEFAULT)), SUBLANG_DEFAULT);
			rsrc = FindResourceEx(GetModuleHandle(NULL), RT_DIALOG, MAKEINTRESOURCE(iDlgResourceID), langid);
			if (rsrc)
				h = LoadResource(GetModuleHandle(NULL), rsrc);
			if(!h)
			{
				// then from no locale
				rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(iDlgResourceID), RT_DIALOG);
				if (rsrc)
					h = LoadResource(GetModuleHandle(NULL), rsrc);
			}
		}

		assert(h);

		lpTemplate = (DLGTEMPLATE *)LockResource(h);

		assertmsgf(lpTemplate, "Error code: %d", GetLastError());

		pNewWindow->eWindowType = eWindowType;
		pNewWindow->iUserIndex = iUserIndex;
		pNewWindow->iDlgResourceID = iDlgResourceID;
		pNewWindow->pNext = spWindowsByType[eWindowType];
		pNewWindow->pTickCB = pTickCB;
		pNewWindow->pDialogCB = pDialogCB;
		pNewWindow->bIsMain = bIsMainWindow;
		pNewWindow->pUserData = pUserData;

		spWindowsByType[eWindowType] = pNewWindow;

		if(SimpleWindowManager_GetUseDialogs()) {

			//kludgily, this causes a dlgproc to run which finds spCurrentlyCreatingWindow and
			//sets the pNewWindow->hWnd through that, and also does
			//		SetWindowLongPtr(pNewWindow->hWnd, GWL_USERDATA, (intptr_t)pNewWindow);
			CreateDialogIndirect(winGetHInstance(), lpTemplate, NULL, (DLGPROC)SimpleWindowManager_DefaultDialogProc);

		} else {

			WNDCLASSEX windowClass;
			HWND hWnd = NULL;

			memset(&windowClass, 0, sizeof(windowClass));
			windowClass.cbSize = sizeof(windowClass);
			windowClass.style = CS_HREDRAW | CS_VREDRAW;
			windowClass.hInstance = winGetHInstance();
			windowClass.lpszClassName = L"SimpleWindow";
			windowClass.lpszMenuName = NULL;
			windowClass.lpfnWndProc = (WNDPROC)SimpleWindowManager_DefaultDialogProc;

			RegisterClassEx(&windowClass);

			hWnd = CreateWindow_UTF8(
				"SimpleWindow",
				"SimpleWindow",
				WS_BORDER | WS_CAPTION | WS_SYSMENU,
				0,0,100,100,
				NULL,
				NULL,
				winGetHInstance(),
				NULL);

			// Because we're not using the Windows dialog functions, we have to manually
			// send the WM_INITDIALOG message here to finish the setup.
			pDialogCB(hWnd, WM_INITDIALOG, 0, 0, pNewWindow);
		}

		assert(pNewWindow->hWnd);

		SimpleWindowManager_FindWindowPosForNewWindow(pNewWindow);

		if(!pNewWindow->bDontAutoShow)
			ShowWindow(pNewWindow->hWnd, SW_SHOW);
		UpdateWindow(pNewWindow->hWnd);
	}
}

void SimpleWindowManager_Run(SimpleWindowManager_TickCallback *pMainTickCB, void *userdata)
{

	MSG mssg;
	U32 tickTimer = timerAlloc();
	U32 slowTimer = timerAlloc();


	// prime the message structure
	PeekMessage( &mssg, NULL, 0, 0, PM_NOREMOVE);

	timerStart(tickTimer);
	timerStart(slowTimer);

	// run till completed
	while (mssg.message!=WM_QUIT) 
	{
		autoTimerThreadFrameBegin("main");

		// is there a message to process?
		if (PeekMessage( &mssg, NULL, 0, 0, PM_REMOVE)) 
		{
			S32 bFoundDialog = 0;
			int i;
		
			for (i=0; i < MAX_WINDOW_TYPES && !bFoundDialog; i++)
			{
				SimpleWindow *pWindow = spWindowsByType[i];

				while (pWindow )
				{
					pWindow->iLastMessageTime = mssg.time;

					if(pWindow->bCloseRequested && pWindow->bCloseBegun)
					{
						SimpleWindowManager_DestroyWindow(pWindow);
						break;
					}

					// Check if this message needs special handling.
					if(pWindow->pPreDialogCB)
						if(pWindow->pPreDialogCB(mssg.hwnd, mssg.message, mssg.wParam, mssg.lParam, pWindow))
						{
							bFoundDialog = 1;

							if (pWindow->bCloseRequested && !pWindow->bCloseBegun)
							{
								if (pWindow->bIsMain)
								{
									sSWMNumMainWindows--;
									if (sSWMNumMainWindows == 0)
									{
										PostQuitMessage(0);
									}
								}

								SimpleWindowManager_SaveWindowPosIntoStruct(pWindow);
								pWindow->bCloseBegun = true;
								DestroyWindow(pWindow->hWnd);
							}

							break;
						}
	
					if(SimpleWindowManager_GetUseDialogs()) {
						if(IsDialogMessage(pWindow->hWnd, &mssg))
						{
							bFoundDialog = 1;
							break;
						}
					}

					pWindow = pWindow->pNext;
				}
			}

			if(!bFoundDialog)
			{
			// dispatch the message
				TranslateMessage(&mssg);
				DispatchMessage(&mssg);
			}
		} 
		else 
		{
			MsgWaitForMultipleObjectsEx(0, 0, DEFAULT_SERVER_SLEEP_TIME, QS_ALLEVENTS, 0);
			if (timerElapsed(slowTimer) > 5.0f)
			{
				timerStart(slowTimer);
				SimpleWindowManager_SaveWindowPositions();
			}


			if (timerElapsed(tickTimer) > (1.0f / 60.0f))
			{
				int i;
				bool ret;
	
				timerStart(tickTimer);

				if (pMainTickCB)
				{
					ret = pMainTickCB(userdata);
					if(!ret)
						break;
				}

				for (i=0; i < MAX_WINDOW_TYPES; i++)
				{
					SimpleWindow *pWindow = spWindowsByType[i];

					while (pWindow)
					{
						SimpleWindow *pNextWindow = pWindow->pNext;
						if (pWindow->pTickCB && !pWindow->bCloseRequested)
						{
							pWindow->iTickCount++;
							pWindow->pTickCB(pWindow);
						}

						pWindow = pNextWindow;
					}
				}
			}
		}

		autoTimerThreadFrameEnd();
	}

	SimpleWindowManager_SaveWindowPositions();
	SimpleWindowManager_DeInit();
}

// Transgaming's Cider doesn't support any of the Windows dialog creation functions, so we
// have to use CreateWindow and friends. This forces that functionality on, even under
// Windows.
static int s_windowManagerNoDialogs = 0;
AUTO_CMD_INT(s_windowManagerNoDialogs, windowManagerNoDialogs) ACMD_COMMANDLINE;

bool SimpleWindowManager_GetUseDialogs(void) {
	return !s_windowManagerNoDialogs && !getIsTransgaming();
}


#endif
#include "autogen/SimpleWindowManager_c_ast.c"
