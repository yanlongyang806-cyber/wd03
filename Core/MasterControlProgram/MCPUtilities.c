
#include "mastercontrolprogram.h"
#include "MCPGimmeCheckins.h"
#include "MCPSVNCheckins.h"
#include "MCPPurgeLogFiles.h"
#include "MCPXboxCP.h"
#include "MCPMemLeakFinder.h"



BOOL utilitiesMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{



	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		case IDSUPERESCAPER:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_SUPERESCAPER, 0, IDD_SUPERESCAPER, false,
				SuperEscaperDlgFunc,  NULL, NULL);
			break;
		case IDTIMINGCONVERSION:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_TIMINGCONVERSION, 0, IDD_TIMECONVERSION, false,
				TimingConversionDlgFunc,  NULL, NULL);
			break;
		case IDGIMMECHECKINS:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_GIMMECHECKINS, 0, IDD_GETGIMMECHECKINS, false,
				gimmeCheckinsMenuDlgProc,  NULL, NULL);
			break;

		case IDSVNCHECKINS:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_SVNCHECKINS, 0, IDD_GETSVNCHECKINS, false,
				svnCheckinsMenuDlgProc,  NULL, NULL);
			break;
		
		case IDPURGELOGS:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_PURGELOGS, 0, IDD_PURGELOGFILES, false,
				purgeLogFilesMenuDlgProc,  NULL, NULL);
			break;

		case IDXBOXCP:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_XBOXCP, 0, IDD_XBOX_CONTROL_PANEL, false,
				xboxCPMenuDlgProc, NULL, NULL);
			break;

		case IDSENTRYSERVERTEST:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_SENTRYSERVERTEST, 0, IDD_SENTRYSERVERTESTER, false,
				sentryServerTestDlgFunc, NULL, NULL);
			break;

		case IDMEMLEAKS:
			LoadLibrary(TEXT("Riched20.dll"));

			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MEMLEAKFINDER, 0, IDD_MEMLEAKFINDER, false,
				memLeakFinderMenuDlgProc, NULL, NULL);
			break;



		}

	}
	
	return FALSE;
}



