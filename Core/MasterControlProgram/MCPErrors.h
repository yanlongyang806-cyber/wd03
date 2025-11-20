#pragma once
BOOL errorsDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

void ReportMCPError(const char *pRawString);
void ResetMCPErrors(void);