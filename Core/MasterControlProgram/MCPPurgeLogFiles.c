
#include "mastercontrolprogram.h"
#include "sTringutil.h"
#include "fileUtil2.h"
#include "sysutil.h"
#include "winutil.h"

BOOL purgeLogFilesMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
			
			SetTextFast(GetDlgItem(hDlg, IDC_DIRNAME), fileLogDir());
			SetTextFast(GetDlgItem(hDlg, IDC_DAYSOLD), "7");
			SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), "Ready to begin purging");
	
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			{
				int iDaysOld;
				char daysOldString[256];
				char dirString[256];
				int iFilesPurged;


				GetWindowText(GetDlgItem(hDlg, IDC_DIRNAME), SAFESTR(dirString));
				GetWindowText(GetDlgItem(hDlg, IDC_DAYSOLD), SAFESTR(daysOldString));

				if (!StringToInt(daysOldString, &iDaysOld) || iDaysOld < 1)
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), "Must specify a positive integer for \"days old\"");
					break;
				}

				if (!dirExists(dirString))
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), STACK_SPRINTF("Directory %s does not exist", dirString));
					break;
				}

				if (strlen(dirString) < 4)
				{
					SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), STACK_SPRINTF("I'm sorry dave, I'm afraid I can't do that"));
					break;
				}

				newConsoleWindow();
				showConsoleWindow();
				iFilesPurged = PurgeDirectoryOfOldFiles(dirString, iDaysOld, NULL);
				SetTextFast(GetDlgItem(hDlg, IDC_PURGERESULT), STACK_SPRINTF("Purged %d files", iFilesPurged));
			}
			break;

		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		}

	}
	
	return FALSE;
}
