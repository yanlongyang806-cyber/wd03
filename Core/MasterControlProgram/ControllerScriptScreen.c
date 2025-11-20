#include "mastercontrolprogram.h"
#include "utils.h"
#include "fileutil.h"

#include "estring.h"
#include "timing.h"
#include "../controller/pub/ControllerPub.h"
#include "winutil.h"
#include "ControllerLink.h"

static MultiControlScaleManager *pScriptScreenMCSM = NULL;


#define MAX_CONTROLLER_SCRIPTS 43
char sControllerScriptNames[MAX_CONTROLLER_SCRIPTS][128];
int iControllerScriptCount=0;

AUTO_COMMAND;
void ExtraControllerScript(char *pName)
{
	strcpy(sControllerScriptNames[iControllerScriptCount], pName);
	iControllerScriptCount++;
}




//set by the controller
enumControllerScriptingState gControllerScriptState = CONTROLLERSCRIPTING_NOTRUNNING;
bool bControllerScriptStateChanged = true;

//estrings
char *pControllerScriptingSubHeadString = NULL;
char *pControllerScriptingMainString = NULL;
__int64 iCurScriptStartTime = 0;
int iMostRecentScriptRan = -1;


AUTO_RUN;
void initScriptingTime(void)
{
	iCurScriptStartTime = timeMsecsSince2000();
}


bool gbLaunchInExistingController = false;

U32 iControllerScriptButtonIDs[MAX_CONTROLLER_SCRIPTS] =
{
	IDC_BUTTON1,
	IDC_BUTTON2,
	IDC_BUTTON3,
	IDC_BUTTON4,
	IDC_BUTTON5,
	IDC_BUTTON6,
	IDC_BUTTON7,
	IDC_BUTTON8,
	IDC_BUTTON9,
	IDC_BUTTON10,
	IDC_BUTTON11,
	IDC_BUTTON12,
	IDC_BUTTON13,
	IDC_BUTTON14,
	IDC_BUTTON15,
	IDC_BUTTON16,
	IDC_BUTTON17,
	IDC_BUTTON18,
	IDC_BUTTON19,
	IDC_BUTTON20,
	IDC_BUTTON21,
	IDC_BUTTON22,
	IDC_BUTTON23,
	IDC_BUTTON24,
	IDC_BUTTON25,
	IDC_BUTTON26,
	IDC_BUTTON27,
	IDC_BUTTON28,
	IDC_BUTTON29,
	IDC_BUTTON30,
	IDC_BUTTON31,
	IDC_BUTTON32,
	IDC_BUTTON33,
	IDC_BUTTON34,
	IDC_BUTTON35,
	IDC_BUTTON36,
	IDC_BUTTON37,
	IDC_BUTTON38,
	IDC_BUTTON39,
	IDC_BUTTON40,
	IDC_BUTTON41,
	IDC_BUTTON42,
	IDC_BUTTON43,

};


void HandleScriptState(Packet *pak)
{
	gControllerScriptState = pktGetBits(pak, 32);
	bControllerScriptStateChanged = true;
}


void AddControllerScriptingMainText(char *pString)
{
	int msecElapsed = (int)(timeMsecsSince2000() - iCurScriptStartTime);
	int minutesElapsed = msecElapsed / 1000 / 60;
	int secondsElapsed = (msecElapsed / 1000) % 60;

	char tempString[1024];
	char *pTemp;
	
	sprintf(tempString, "%s (%d:%02d.%03d)", pString, minutesElapsed, secondsElapsed, msecElapsed % 1000);
	
	//remove linefeeds and character returns
	pTemp = tempString;

	while (*pTemp)
	{
		if (*pTemp == '\n' || *pTemp == '\r')
		{
			*pTemp = ' ';
		}

		pTemp++;
	}


	
	estrConcatf(&pControllerScriptingMainString, "%s\r\n", tempString);
}

void SetControllerScriptingSubHead(char *pString)
{
	estrPrintf(&pControllerScriptingSubHeadString, "%s", pString);
}

void PressControllerScriptButton(HWND hDlg, int i)
{
	ShowWindow(GetDlgItem(hDlg, IDC_SCRIPTCOMPLETEWERRORS), SW_HIDE);
	ShowWindow(GetDlgItem(hDlg, IDC_SCRIPTFAILED), SW_HIDE);
	ShowWindow(GetDlgItem(hDlg, IDC_SCRIPTSUCCEEDED),  SW_HIDE);


	estrDestroy(&pControllerScriptingSubHeadString);
	estrDestroy(&pControllerScriptingMainString);
	iCurScriptStartTime = timeMsecsSince2000();

	SetTextFast(hDlg, STACK_SPRINTF("Active Script: %s", sControllerScriptNames[i]));

	AddControllerScriptingMainText(STACK_SPRINTF("Starting %s", sControllerScriptNames[i]));
	iMostRecentScriptRan = i;


	if (gbLaunchInExistingController)
	{
		if (GetControllerLink())
		{
			Packet *pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_RUN_SCRIPT);
	
			if (fileIsAbsolutePath(sControllerScriptNames[i]))
			{
				pktSendString(pPak, sControllerScriptNames[i]);
			}
			else
			{
				pktSendString(pPak, STACK_SPRINTF("server/ControllerScripts/%s.txt", sControllerScriptNames[i]));
			}
			
			pktSend(&pPak);
		}
	}
	else
	{
		if (fileIsAbsolutePath(sControllerScriptNames[i]))
		{
			ForceResetAll(sControllerScriptNames[i]);
		}
		else
		{
			ForceResetAll(STACK_SPRINTF("server/ControllerScripts/%s.txt", sControllerScriptNames[i]));
		}
	}


}

FileScanAction FindControllerScriptsProcessor(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char path[CRYPTIC_MAX_PATH];
	char *s;
	char *pCommonDirString;
	if (iControllerScriptCount >= ARRAY_SIZE(sControllerScriptNames))
		return FSA_STOP;
	if (!strEndsWith(data->name, ".txt"))
		return FSA_EXPLORE_DIRECTORY;
	if (data->name[0] == '_')
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	sprintf(path, "%s/%s", dir, data->name);
	fileRelativePath(path, path);
	assert((pCommonDirString = strstr(path, "server/ControllerScripts/")));

	strcpy(sControllerScriptNames[iControllerScriptCount], pCommonDirString + strlen("server/ControllerScripts/"));
	s = strrchr(sControllerScriptNames[iControllerScriptCount], '.');
	if (s)
		*s = '\0'; // Remove .bat
	iControllerScriptCount++;

	return FSA_EXPLORE_DIRECTORY;
}

void FindControllerScripts(void)
{
	if (iControllerScriptCount == 0)
	{
		memset(sControllerScriptNames, 0, sizeof(sControllerScriptNames));
		iControllerScriptCount = 0;

		fileScanAllDataDirs("server/ControllerScripts", FindControllerScriptsProcessor, NULL);
	}
}
	
bool ControllerScriptTickFunc(SimpleWindow *pWindow)
{


	if (SetTextFast(GetDlgItem(pWindow->hWnd, IDC_CONTROLLERSCRIPT_MAINTEXT), pControllerScriptingMainString ? pControllerScriptingMainString : ""))
	{
		SendMessage(      GetDlgItem(pWindow->hWnd, IDC_CONTROLLERSCRIPT_MAINTEXT), (UINT) EM_SCROLL, SB_PAGEDOWN, 0);  
	}

	SetTextFast(GetDlgItem(pWindow->hWnd, IDC_CONTROLLERSCRIPT_SUBHEAD), pControllerScriptingSubHeadString ? pControllerScriptingSubHeadString : "");

	if (bControllerScriptStateChanged)
	{
		bControllerScriptStateChanged = false;

		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SCRIPTCOMPLETEWERRORS), gControllerScriptState == CONTROLLERSCRIPTING_COMPLETE_W_ERRORS ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SCRIPTFAILED), gControllerScriptState == CONTROLLERSCRIPTING_FAILED ? SW_SHOW : SW_HIDE);
		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_SCRIPTSUCCEEDED), gControllerScriptState == CONTROLLERSCRIPTING_SUCCEEDED ? SW_SHOW : SW_HIDE);
	}
	return true;
}


BOOL ControllerScriptDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			if (!pScriptScreenMCSM)
			{

				pScriptScreenMCSM = BeginMultiControlScaling(hDlg);

				MultiControlScaling_AddChild(pScriptScreenMCSM, IDC_CONTROLLERSCRIPT_MAINTEXT, SCALE_LOCK_BOTH, SCALE_LOCK_BOTH);
				MultiControlScaling_AddChild(pScriptScreenMCSM, IDC_SCRIPTFAILED, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pScriptScreenMCSM, IDC_SCRIPTSUCCEEDED, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pScriptScreenMCSM, IDC_SCRIPTCOMPLETEWERRORS, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pScriptScreenMCSM, IDC_LAUNCHINEXISTING, SCALE_LOCK_RIGHT, SCALE_LOCK_BOTTOM);

				CheckDlgButton(hDlg, IDC_LAUNCHINEXISTING, 
					gbLaunchInExistingController?BST_CHECKED:BST_UNCHECKED);

				for (i=0; i < MAX_CONTROLLER_SCRIPTS; i++)
				{
					MultiControlScaling_AddChild(pScriptScreenMCSM, iControllerScriptButtonIDs[i], 
						SCALE_FULLRESIZE, SCALE_LOCK_BOTTOM);
				}
			}
			else
			{
				ReInitMultiControlScaling(pScriptScreenMCSM, hDlg);
			}


			ShowWindow(GetDlgItem(hDlg, IDC_SCRIPTCOMPLETEWERRORS), gControllerScriptState == CONTROLLERSCRIPTING_COMPLETE_W_ERRORS ? SW_SHOW : SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_SCRIPTFAILED), gControllerScriptState == CONTROLLERSCRIPTING_FAILED ? SW_SHOW : SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_SCRIPTSUCCEEDED), gControllerScriptState == CONTROLLERSCRIPTING_SUCCEEDED ? SW_SHOW : SW_HIDE);
		
			for (i=0; i < MAX_CONTROLLER_SCRIPTS; i++)
			{
				if (sControllerScriptNames[i][0])
				{
					char localName[MAX_PATH];
					ShowWindow(GetDlgItem(hDlg, iControllerScriptButtonIDs[i]),  SW_SHOW);

					getFileNameNoExt(localName, sControllerScriptNames[i]);

					SetWindowText(GetDlgItem(hDlg, iControllerScriptButtonIDs[i]), localName);
				}
				else
				{
					ShowWindow(GetDlgItem(hDlg, iControllerScriptButtonIDs[i]),  SW_HIDE);
				}
			}

			{
			
				static HFONT hf = 0;
				HDC hdc;
				long lfHeight;
		
				if (!hf)
				{
					hdc = GetDC(NULL);
					lfHeight = -MulDiv(18, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					ReleaseDC(NULL, hdc);

					hf = CreateFont(lfHeight, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Times New Roman");
				}

				if (hf)
				{
					SendMessage((HWND)GetDlgItem(hDlg, IDC_CONTROLLERSCRIPT_SUBHEAD), (UINT)WM_SETFONT, (WPARAM)hf, (WPARAM)true);
				}
		
			}

			if (iMostRecentScriptRan != -1)
			{
				SetTextFast(hDlg, STACK_SPRINTF("Active Script: %s", sControllerScriptNames[iMostRecentScriptRan]));
			}
			
			MultiControlScaling_Update(pScriptScreenMCSM);


			return TRUE; 

		}
		break;

	case WM_SIZE:
		MultiControlScaling_Update(pScriptScreenMCSM);
		break;


	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
		{
			pWindow->bCloseRequested = true;
			return FALSE;
		}
			
		for (i=0; i < MAX_CONTROLLER_SCRIPTS; i++)
		{
			if (LOWORD (wParam) == iControllerScriptButtonIDs[i])
			{
				PressControllerScriptButton(hDlg, i);
			}
		}	

		if (LOWORD (wParam) == IDC_LAUNCHINEXISTING)
		{
			gbLaunchInExistingController = IsDlgButtonChecked(hDlg, IDC_LAUNCHINEXISTING);
		}

		break;

	}
	
	return FALSE;
}
