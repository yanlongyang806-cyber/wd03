#pragma once
GCC_SYSTEM

#include "wininclude.h"

//this is a simple set of utilities to make it easy to create multi-windowed Windows apps

typedef struct SimpleWindow SimpleWindow;
typedef bool SimpleWindowManager_TickCallback(SimpleWindow *pWindow);
typedef BOOL SimpleWindowManager_DialogProcCallback(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

typedef struct SWMSavedWindowPos SWMSavedWindowPos;

typedef struct SimpleWindow 
{
	int eWindowType;
	int iUserIndex;
	U32 iDlgResourceID;
	SimpleWindowManager_TickCallback *pTickCB;
	SimpleWindowManager_DialogProcCallback *pDialogCB;
	SimpleWindowManager_DialogProcCallback *pPreDialogCB;
	int iTickCount;
	U32 bCloseRequested : 1;
	U32 bCloseBegun : 1;
	U32 bIsMain : 1;
	U32 bDontAutoShow : 1;

	HWND hWnd;

	SWMSavedWindowPos *pSavedWindowPos;
	SimpleWindow *pNext;
	void *pUserData;

	DWORD iLastMessageTime;

} SimpleWindow;


void SimpleWindowManager_Init(const char *pAppName, bool bUseRegistryForLocalSettings);
void SimpleWindowManager_AddOrActivateWindow(int eWindowType, int iUserIndex, U32 iDlgResourceID,
	bool bIsMainWindow, SimpleWindowManager_DialogProcCallback *pDialogCB, SimpleWindowManager_TickCallback *pTickCB, void *pUserData);
void SimpleWindowManager_Run(SimpleWindowManager_TickCallback *pMainTickCB, void *userdata);
SimpleWindow *SimpleWindowManager_FindWindow(int eWindowType, int iUserIndex);
SimpleWindow *SimpleWindowManager_FindWindowByType(int eWindowType);
void SimpleWindowManager_FindAllWindowsByType(int eWindowType, SimpleWindow ***pppOutWindows);
bool SimpleWindowManager_GetUseDialogs(void);

