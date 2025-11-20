
#include "mastercontrolprogram.h"
#include "sTringutil.h"
#include "fileUtil2.h"
#include "sysutil.h"
#include "winutil.h"
#include "XboxHostIO.h"

int siPrintfCounter = -2;

MultiControlScaleManager *pXboxMCSM = NULL;

BOOL xboxCPMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
			xboxBeginStatusQueryThread();
			SetTimer(hDlg, 0, 1, NULL);

			if (!pXboxMCSM)
			{
				pXboxMCSM = BeginMultiControlScaling(hDlg);

				MultiControlScaling_AddChild(pXboxMCSM, IDC_XBOX_TEXT_CAPTURE, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pXboxMCSM, IDC_XBOXOUTPUT, SCALE_LOCK_BOTH, SCALE_LOCK_BOTH);
				MultiControlScaling_AddChild(pXboxMCSM, IDC_REBOOT, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pXboxMCSM, IDC_COLD_REBOOT, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pXboxMCSM, IDC_XBOX_CLEAR, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
			}
			else
			{
				ReInitMultiControlScaling(pXboxMCSM, hDlg);
			}

			MultiControlScaling_Update(pXboxMCSM);
			break;

	case WM_SIZE:
		MultiControlScaling_Update(pXboxMCSM);
		break;


	case WM_TIMER:
		{
			bool bReady;
			char *pXboxName = NULL;
			char *pExeName = NULL;

			xboxQueryStatusFromThread(&bReady, &pXboxName, &pExeName);

			SetTextFast(GetDlgItem(hDlg, IDC_XBOXACTIVE), bReady ? "READY" : "NOT READY");
			SetTextFast(GetDlgItem(hDlg, IDC_XBOXNAME), pXboxName);
			SetTextFast(GetDlgItem(hDlg, IDC_XBOXRUNNING), pExeName);

			estrDestroy(&pXboxName);
			estrDestroy(&pExeName);

			if (siPrintfCounter != -2)
			{
				if (siPrintfCounter != xboxGetPrintfCounter())
				{
					char *pStr;
					int iLen;
					char *pInternalString = NULL;

					xboxAccessCapturedPrintfs(&pStr, &iLen, &siPrintfCounter);
					estrCopy2(&pInternalString, pStr);
					xboxFinishedAccessingPrintfs();

					estrFixupNewLinesForWindows(&pInternalString);

					SetWindowText(GetDlgItem(hDlg, IDC_XBOXOUTPUT), pInternalString);
					estrDestroy(&pInternalString);

					SendMessage(GetDlgItem(hDlg, IDC_XBOXOUTPUT), EM_LINESCROLL, 0, 
						SendMessage(GetDlgItem(hDlg, IDC_XBOXOUTPUT), EM_GETLINECOUNT, 0 ,0));
				}
			}


		}
	
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_XBOX_TEXT_CAPTURE:
			siPrintfCounter = -1;
			xboxBeginCapturingPrintfs();

			break;

		case IDC_REBOOT:
			xboxReboot(false);
			break;
		case IDC_COLD_REBOOT:
			xboxReboot(true);
			break;

		case IDC_XBOX_CLEAR:
			xboxResetPrintfCapturing();
			break;

		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		}

	}
	
	return FALSE;
}
