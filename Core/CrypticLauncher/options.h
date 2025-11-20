// CrypicLauncher options dialog functions

#pragma once
#include "windefinclude.h"

// Structure forward defines
typedef struct SimpleWindow SimpleWindow;
typedef struct CrypticLauncherWindow CrypticLauncherWindow;

BOOL OptionsPreDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
BOOL OptionsDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool OptionsTickFunc(SimpleWindow *pWindow);

void loadRegistrySettings(CrypticLauncherWindow *launcher, const char *productName);