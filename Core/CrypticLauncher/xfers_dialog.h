// CrypicLauncher transfers dialog functions

#pragma once
#include "windefinclude.h"

// Structure forward defines
typedef struct SimpleWindow SimpleWindow;

BOOL XfersPreDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
BOOL XfersDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool XfersTickFunc(SimpleWindow *pWindow);