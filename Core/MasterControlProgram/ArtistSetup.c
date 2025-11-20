#include "mastercontrolprogram.h"
#include "richedit.h"
#include "winutil.h"
	
static MultiControlScaleManager *pArtistSetupMCSM = NULL;

bool ArtistSetupTickFunc(SimpleWindow *pWindow)
{
	static int iCounter = 0;

	UpdateArtToolPrintfBuffer(false);

	iCounter++;

	if (iCounter % 30 == 0)
	{
		int i;

		for (i=0; i < GetNumArtistTools(); i++)
		{
			ResetArtToolButtonText(pWindow->hWnd, i);
		}
	}
	return true;
}



BOOL ArtistSetupDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{

			if (!pArtistSetupMCSM)
			{
				pArtistSetupMCSM = BeginMultiControlScaling(hDlg);

				MultiControlScaling_AddChild(pArtistSetupMCSM, IDC_SHOWARTTOOL, SCALE_LOCK_LEFT, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pArtistSetupMCSM, IDC_HIDEARTTOOL, SCALE_LOCK_LEFT, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pArtistSetupMCSM, IDC_KILLARTTOOL, SCALE_LOCK_RIGHT, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pArtistSetupMCSM, IDC_TOOLOUTPUT, SCALE_LOCK_BOTH, SCALE_LOCK_BOTH);


				for (i=0; i < MAX_ARTIST_TOOLS; i++)
				{
					MultiControlScaling_AddChild(pArtistSetupMCSM, gArtistToolButtonIDs[i].iArrowID, SCALE_MOVE_NORESIZE, SCALE_LOCK_TOP);
					MultiControlScaling_AddChild(pArtistSetupMCSM, gArtistToolButtonIDs[i].iBGBCheckID, SCALE_FULLRESIZE, SCALE_LOCK_TOP);
					MultiControlScaling_AddChild(pArtistSetupMCSM, gArtistToolButtonIDs[i].iExclamationPointID, SCALE_MOVE_NORESIZE, SCALE_LOCK_TOP);
					MultiControlScaling_AddChild(pArtistSetupMCSM, gArtistToolButtonIDs[i].iExtraCheckID, SCALE_FULLRESIZE, SCALE_LOCK_TOP);
					MultiControlScaling_AddChild(pArtistSetupMCSM, gArtistToolButtonIDs[i].iLaunchButtonID, SCALE_FULLRESIZE, SCALE_LOCK_TOP);
				}
			}
			else
			{
				ReInitMultiControlScaling(pArtistSetupMCSM, hDlg);
			}

			gArtToolsNeedAttention &= ~(1 << giActiveArtToolPrintBuffer);

			UpdateArtToolPrintfBuffer(true);


			for (i=0; i < GetNumArtistTools(); i++)
			{
				CheckDlgButton(hDlg, gArtistToolButtonIDs[i].iExtraCheckID, 
					GetArtistToolDynamicInfo(i)->bExtraChecked?BST_CHECKED:BST_UNCHECKED);
				CheckDlgButton(hDlg, gArtistToolButtonIDs[i].iBGBCheckID, 
					GetArtistToolDynamicInfo(i)->bBGBChecked?BST_CHECKED:BST_UNCHECKED);

				ResetArtToolButtonText(pWindow->hWnd, i);


			}

			SendMessage(   
				GetDlgItem(hDlg, IDC_TOOLOUTPUT),
				(UINT) EM_SETBKGNDCOLOR,     
				(WPARAM) 0,     
				(LPARAM) RGB(0,0,0)); 


			MultiControlScaling_Update(pArtistSetupMCSM);

	
			return TRUE; 

		}
		break;


	case WM_SIZE:
		MultiControlScaling_Update(pArtistSetupMCSM);
		break;

	case WM_COMMAND:
		for (i=0; i < GetNumArtistTools(); i++)
		{
			if (LOWORD (wParam) == gArtistToolButtonIDs[i].iLaunchButtonID)
			{
				LaunchArtistTool(i, false);
				ResetArtToolButtonText(pWindow->hWnd, i);

			}
			else if (LOWORD (wParam) == gArtistToolButtonIDs[i].iBGBCheckID)
			{
				GetArtistToolDynamicInfo(i)->bBGBChecked = IsDlgButtonChecked(hDlg, gArtistToolButtonIDs[i].iBGBCheckID);
				WriteSettingsToFile();

			}
			else if (LOWORD (wParam) == gArtistToolButtonIDs[i].iExtraCheckID)
			{
				GetArtistToolDynamicInfo(i)->bExtraChecked = IsDlgButtonChecked(hDlg, gArtistToolButtonIDs[i].iExtraCheckID);
				WriteSettingsToFile();

			}
		}
		
		switch (LOWORD(wParam))
		{
		case IDC_KILLARTTOOL:
			KillArtistTool(giActiveArtToolPrintBuffer);
			ResetArtToolButtonText(pWindow->hWnd, giActiveArtToolPrintBuffer);
			break;

		case IDC_SHOWARTTOOL:
			if (!artistToolPids[giActiveArtToolPrintBuffer])
			{
				FindArtistToolConsoleWindow(giActiveArtToolPrintBuffer);
			}

			if (artistToolPids[giActiveArtToolPrintBuffer] && ProcessNameMatch( artistToolPids[giActiveArtToolPrintBuffer] , GetArtistToolStaticInfo(giActiveArtToolPrintBuffer)->pExeName, true))
			{
				ShowWindow(artistToolHWNDs[giActiveArtToolPrintBuffer], SW_SHOW);
			}
			break;
		case IDC_HIDEARTTOOL:
			if (!artistToolPids[giActiveArtToolPrintBuffer])
			{
				FindArtistToolConsoleWindow(giActiveArtToolPrintBuffer);
			}

			if (artistToolPids[giActiveArtToolPrintBuffer] && ProcessNameMatch( artistToolPids[giActiveArtToolPrintBuffer] , GetArtistToolStaticInfo(giActiveArtToolPrintBuffer)->pExeName, true))
			{
				ShowWindow(artistToolHWNDs[giActiveArtToolPrintBuffer], SW_HIDE);
			}
			break;


		case IDCANCEL:
			pWindow->bCloseRequested = true;
			break;
		}
	}
	
	return FALSE;
}

