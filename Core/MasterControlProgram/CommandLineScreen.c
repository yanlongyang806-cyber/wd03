#include "mastercontrolprogram.h"
#include "winutil.h"
#include "StashTable.h"
#include "GlobalTypes.h"
#include "file.h"

StashTable sCommandLineWindowMCSMTable = NULL;
//static MultiControlScaleManager *pCommandLineWindowMCSM = NULL;


char *GetContainerTypeCommandLine(int iIndex)
{
	if (iIndex == GLOBALTYPE_NONE)
	{
		return gGlobalDynamicSettings.globalCommandLine;
	}

	if (iIndex == FAKE_GLOBALTYPE_XBOXCLIENT)
	{
		return gGlobalDynamicSettings.xboxClientCommandLine;
	}

	if (iIndex == FAKE_GLOBALTYPE_PATCHED_CLIENT)
	{
		return gGlobalDynamicSettings.patchedPCClientCommandLine;
	}

	if (iIndex == FAKE_GLOBALTYPE_PATCHED_XBOX_CLIENT)
	{
		return gGlobalDynamicSettings.patchedXboxClientCommandLine;
	}

	if (iIndex == FAKE_GLOBALTYPE_LAUNCH_CLIENT)
	{
		return gGlobalDynamicSettings.launchClientDirectlyCommandLine;
	}

	if (iIndex > COMMAND_LINE_WINDOW_SHARED_OFFSET)
	{
		return GetContainerDynamicInfoFromType(iIndex-COMMAND_LINE_WINDOW_SHARED_OFFSET)->sharedCommandLine;
	}

	return GetContainerDynamicInfoFromType(iIndex)->commandLine;
}

char *GetContainerTypeOverrideLaunchDir(int iIndex)
{
	if (iIndex == GLOBALTYPE_NONE)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_XBOXCLIENT)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_PATCHED_CLIENT)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_PATCHED_XBOX_CLIENT)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_LAUNCH_CLIENT)
	{
		return NULL;
	}

	if (iIndex > COMMAND_LINE_WINDOW_SHARED_OFFSET)
	{
		return NULL;
	}

	return GetContainerDynamicInfoFromType(iIndex)->overrideLaunchDir;
}


char *GetContainerTypeOverrideExeName(int iIndex)
{
	if (iIndex == GLOBALTYPE_NONE)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_XBOXCLIENT)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_PATCHED_CLIENT)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_PATCHED_XBOX_CLIENT)
	{
		return NULL;
	}

	if (iIndex == FAKE_GLOBALTYPE_LAUNCH_CLIENT)
	{
		return NULL;
	}

	if (iIndex > COMMAND_LINE_WINDOW_SHARED_OFFSET)
	{
		return NULL;
	}

	return GetContainerDynamicInfoFromType(iIndex)->overrideExeName;
}


MultiControlScaleManager *FindCommandLineMCSMManager(int iIndex, HWND hDlg)
{
	MultiControlScaleManager *pManager;

	if (!sCommandLineWindowMCSMTable)
	{
		sCommandLineWindowMCSMTable = stashTableCreateInt(16);
	}

	if (stashIntFindPointer(sCommandLineWindowMCSMTable, iIndex == 0 ? -1 : 2, &pManager))
	{
		ReInitMultiControlScaling(pManager, hDlg);

		return pManager;
	}

	pManager = BeginMultiControlScaling(hDlg);
	MultiControlScaling_AddChild(pManager, IDOK, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
	MultiControlScaling_AddChild(pManager, IDC_EDITCOMMANDLINE, SCALE_LOCK_BOTH, SCALE_LOCK_BOTH);

	MultiControlScaling_AddChild(pManager, IDC_OVERRIDEEXE, SCALE_LOCK_BOTH, SCALE_LOCK_BOTTOM);
	MultiControlScaling_AddChild(pManager, IDC_OVERRIDEEXETITLE, SCALE_LOCK_BOTH, SCALE_LOCK_BOTTOM);
	MultiControlScaling_AddChild(pManager, IDC_OVERRIDELAUNCHDIR, SCALE_LOCK_BOTH, SCALE_LOCK_BOTTOM);
	MultiControlScaling_AddChild(pManager, IDC_OVERRIDELAUNCHDIRTITLE, SCALE_LOCK_BOTH, SCALE_LOCK_BOTTOM);

	stashIntAddPointer(sCommandLineWindowMCSMTable, iIndex == 0 ? -1 : 2, pManager, false);

	return pManager;
}



BOOL EditCommandLineDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	MultiControlScaleManager *pMCSMManager;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			char captionString[1024];
			
			
			pMCSMManager = FindCommandLineMCSMManager(pWindow->iUserIndex, hDlg);
			
			SetTextFast(GetDlgItem(pWindow->hWnd, IDC_EDITCOMMANDLINE), GetContainerTypeCommandLine(pWindow->iUserIndex));

			SetTextFast(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), GetContainerTypeOverrideExeName(pWindow->iUserIndex));
			SetTextFast(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), GetContainerTypeOverrideLaunchDir(pWindow->iUserIndex));

			if (pWindow->iUserIndex == GLOBALTYPE_NONE)
			{

				sprintf(captionString, "Specify globally shared command line options for all servers.");
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit GLOBALLY SHARED command line");
				SetWindowText(hDlg, captionString);

				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_HIDE);



			}
			else if (pWindow->iUserIndex == FAKE_GLOBALTYPE_XBOXCLIENT)
			{

				sprintf(captionString, "Specify command line parameters for XBox Client. Only the first line of text is used, so you can store alternatives in other lines");
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit command line - XBox Client");
				SetWindowText(hDlg, captionString);


				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_HIDE);

			}
			else if (pWindow->iUserIndex == FAKE_GLOBALTYPE_PATCHED_CLIENT)
			{

				sprintf(captionString, "Specify command line parameters for Patched PC Client. Only the first line of text is used, so you can store alternatives in other lines");
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit command line - Patched PC Client");
				SetWindowText(hDlg, captionString);


				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_HIDE);

			}
			else if (pWindow->iUserIndex == FAKE_GLOBALTYPE_PATCHED_XBOX_CLIENT)
			{

				sprintf(captionString, "Specify command line parameters for Patched XBOX Client. Only the first line of text is used, so you can store alternatives in other lines");
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit command line - Patched Xbox Client");
				SetWindowText(hDlg, captionString);


				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_HIDE);

			}
			else if (pWindow->iUserIndex == FAKE_GLOBALTYPE_LAUNCH_CLIENT)
			{
				sprintf(captionString, "Specify command line parameters for PC Client. Only the first line of text is used, so you can store alternatives in other lines");
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit command line - PC Client");
				SetWindowText(hDlg, captionString);
	
			
		
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_HIDE);
	
			}
			else if (pWindow->iUserIndex > COMMAND_LINE_WINDOW_SHARED_OFFSET)
			{
				sprintf(captionString, "Specify SHARED command line parameters for all servers of type %s, including ones launched later by the controller during gameplay.",
					GlobalTypeToName(pWindow->iUserIndex - COMMAND_LINE_WINDOW_SHARED_OFFSET));
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit SHARED command line - %s", GlobalTypeToName(pWindow->iUserIndex - COMMAND_LINE_WINDOW_SHARED_OFFSET));
				SetWindowText(hDlg, captionString);


				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_HIDE);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_HIDE);

			}
			else
			{
				sprintf(captionString, "Specify command line parameters for %s. Only the first line of text is used, so you can store alternatives in other lines",
					GlobalTypeToName(pWindow->iUserIndex));
				SetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE_CAPTION), captionString);
				sprintf(captionString, "Edit command line - %s", GlobalTypeToName(pWindow->iUserIndex));
				SetWindowText(hDlg, captionString);
	
		
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXE), SW_SHOW);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDEEXETITLE), SW_SHOW);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIR), SW_SHOW);
				ShowWindow(GetDlgItem(pWindow->hWnd, IDC_OVERRIDELAUNCHDIRTITLE), SW_SHOW);
	
			}

			MultiControlScaling_Update(pMCSMManager);


			return TRUE; 

		}
		break;




	case WM_SIZE:
			pMCSMManager = FindCommandLineMCSMManager(pWindow->iUserIndex, hDlg);
			MultiControlScaling_Update(pMCSMManager);
		break;


	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
			
			GetWindowText(GetDlgItem(hDlg, IDC_EDITCOMMANDLINE), GetContainerTypeCommandLine(pWindow->iUserIndex), COMMAND_LINE_SIZE);				
		
			GetWindowText(GetDlgItem(hDlg, IDC_OVERRIDEEXE), GetContainerTypeOverrideExeName(pWindow->iUserIndex), CRYPTIC_MAX_PATH);				
			GetWindowText(GetDlgItem(hDlg, IDC_OVERRIDELAUNCHDIR), GetContainerTypeOverrideLaunchDir(pWindow->iUserIndex), CRYPTIC_MAX_PATH);				

			pWindow->bCloseRequested = true;
			if (pWindow->iUserIndex > COMMAND_LINE_WINDOW_SHARED_OFFSET || pWindow->iUserIndex == GLOBALTYPE_NONE || isProductionMode())
			{
#if !STANDALONE
				SendCommandLineSettingsToController();
#endif
			}
			gbCommandLinesMightHaveChanged = true;
			WriteSettingsToFile();

			SendAllOverrideExeNamesAndDirs();

			
		break;


		case IDCANCEL:
			pWindow->bCloseRequested = true;
		break;

		}

	}
	
	return FALSE;
}




bool IsCommandLineNonEmpty(char *pString)
{
	int i = 0;

	do
	{
		if (pString[i] == 0)
		{
			return false;
		}

		if (pString[i] == '\n')
		{
			return false;
		}

		if (pString[i] == '\r')
		{
			return false;
		}

		if (pString[i] != ' ')
		{
			return true;
		}

		i++;
	}
	while (1);
}

