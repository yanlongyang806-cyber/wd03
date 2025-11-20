#include "GlobalTypes.h"
#include "sock.h"
#include "mastercontrolprogram.h"
#include "quitmessage.h"
#include "serverlist.h"
#include "utils.h"
#include "winutil.h"
#include "earray.h"
#include "error.h"
#include "FileWatch.h"

#include "MCPHttp.h"
#include "estring.h"

#include "mcpUtilities.h"
#include "ControllerLink.h"
#include "OtherOptions.h"
#include "fileUtil2.h"
#include "genericMessage.h"
#include "mcpConfig.h"
#include "mcpErrors.h"
#include "estring.h"
#include "UtilitiesLib.h"
#include "gimmeDLLWrapper.h"
#include "StringUtil.h"
#include "windowsx.h"
#include "fileutil.h"

#define MIN_BUTTONS_TO_ALLOW_SPACE_FOR 15

#define BGB_BITMAP_SIZE 175

static bool sbFilledInStartingMapNames = false;
static bool sbFilledInExtraMapNames[NUM_EXTRA_GS_NAMES] = {0};

//moved a bunch of startup command line options here from MasterControlProgram.c, as they are not in the standalone version

// Startup command line options

// Sets starting mode to complex
AUTO_CMD_INT(gbComplex,complex) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// Start directly in local mode
AUTO_CMD_INT(gbRunInLocalMode,local) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// Start directly in local public mode
AUTO_CMD_INT(gbRunInLocalPublicMode,publiclocal) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// I'm an artist
AUTO_CMD_INT(gbArtist,artist) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

// I'm a programmer
AUTO_CMD_INT(gbProgrammer,programmer) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

//don't create a controller... instead, connect to one that already exists
AUTO_CMD_INT(gbDontCreateController, dontCreateController) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

//press the big green button immediately upon getting to that screen
AUTO_CMD_INT(gbPressButtonAutomatically, pressButtonAutomatically) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

//minimizes the main screen dialog when you first get to it
AUTO_CMD_INT(gbStartMinimized, startMinimized) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

//opens the controller script window at startup
AUTO_CMD_INT(gbOpenScriptWindow, openscriptwindow) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

//launch controller scripts using existing controller
AUTO_CMD_INT(gbLaunchInExistingController, launchinexisting) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

//used in production mode to allow users to set which windows start out hidden... after the 
//controller creates the MCP, the MCP sends all the "is this hidden" choices
bool gbSendHideWindowsChoicesOnStartup = false;
AUTO_CMD_INT(gbSendHideWindowsChoicesOnStartup, SendHideWindowsChoicesOnStartup);


#define LOCAL_STATUS_REQUEST_FREQUENCY 100
#define STRING_SCROLL_SPEED 5

void CreateLocalClientForNonLocalConnection(bool bHide, bool bStartInDebugger, char *pExtraCommandLine);

//loaded from local data, where it's written out by the map manager
static char **sppMapNames_UnFiltered = NULL;

//sometimes returns sppMapNames_UnFiltered directly, other times creates and caches and returns
//a list based on GSFILTER
char **GetFilteredMapNameList(void);

void UpdateGSFilter(HWND hDlg);

static U32 siServerMonButtonIDs[]=
{
	IDC_SERVERMONSTAT1,
	IDC_SERVERMONSTAT2,
	IDC_SERVERMONSTAT3,
	IDC_SERVERMONSTAT4,
	IDC_SERVERMONSTAT5,
	IDC_SERVERMONSTAT6,
	IDC_SERVERMONSTAT7,
	IDC_SERVERMONSTAT8,
};

static U32 siExtraGameServerMapIDs[NUM_EXTRA_GS_NAMES] =
{
	IDC_EXTRAGAMESERVERMAPS_1,
	IDC_EXTRAGAMESERVERMAPS_2,
	IDC_EXTRAGAMESERVERMAPS_3,
	IDC_EXTRAGAMESERVERMAPS_4
};

static U32 siLaunchExtraGameServerIDs[NUM_EXTRA_GS_NAMES] = 
{
	IDC_LAUNCHEXTRAGAMESERVER_1,
	IDC_LAUNCHEXTRAGAMESERVER_2,
	IDC_LAUNCHEXTRAGAMESERVER_3,
	IDC_LAUNCHEXTRAGAMESERVER_4,
};

static U32 siExtraGameServerNoAutoIDs[NUM_EXTRA_GS_NAMES] = 
{
	IDC_EXTRAGAMESERVER_NOAUTO_1,
	IDC_EXTRAGAMESERVER_NOAUTO_2,
	IDC_EXTRAGAMESERVER_NOAUTO_3,
	IDC_EXTRAGAMESERVER_NOAUTO_4,
};



char *sStateStrings[]
={
	"   ",
	"Running... Running... Running... ",
	"Crashed..... Crashed..... Crashed..... ",
	"Waiting.... Waiting.... Waiting.... ",
};

void SendAllVisibilityChoices(void)
{
	Packet *pak;
	int i;

	if (!GetControllerLink())
	{
		return;
	}

	for (i=0; i < GetNumContainerTypes(); i++)
	{
		pak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_HIDE_SERVER);

		PutContainerTypeIntoPacket(pak, GetContainerStaticInfoFromSlot(i)->eContainerType);
		pktSendBits(pak, 1, GetContainerDynamicInfoFromSlot(i)->bHideButtonChecked);

		pktSend(&pak);
	}
}

void SendAllOverrideExeNamesAndDirs(void)
{
	Packet *pak;
	int i;

	if (!GetControllerLink())
	{
		return;
	}

	for (i=0; i < GetNumContainerTypes(); i++)
	{
		pak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_SETS_EXE_NAME_AND_DIR);
		PutContainerTypeIntoPacket(pak, GetContainerStaticInfoFromSlot(i)->eContainerType);
		pktSendString(pak, GetContainerDynamicInfoFromSlot(i)->overrideExeName);
		pktSendString(pak, GetContainerDynamicInfoFromSlot(i)->overrideLaunchDir);
		pktSend(&pak);
	}
}

				
static void DoTypeSpecificCommandLineStuff(char **ppCommandLine, GlobalType eType)
{
	switch (eType)
	{
	case GLOBALTYPE_CLIENT:
		if (isDevelopmentMode())
		{
			if (!gbDisableNewErrorScreen)
			{
				char *pSnippet = " -SendAllErrorsToController 2 ";
				estrInsert(ppCommandLine, 0, pSnippet, (int)strlen(pSnippet));
			}
		}
		
		break;
	}
}



void SendCommandLineSettingsToController(void)
{
	if (GetControllerLink())
	{
		int i;
		Packet *pPak = pktCreate(GetControllerLink(), TO_CONTROLLER_HERE_ARE_SERVERTYPE_COMMANDLINES);
		char *pFullCommandLine = NULL;

		//first, send globalCommandLine by itself
		PutContainerTypeIntoPacket(pPak, GLOBALTYPE_ALL);
		estrPrintf(&pFullCommandLine, " %s ", GetGlobalCommandLine());
		estrTruncateAtFirstOccurrence(&pFullCommandLine, '\n');
		estrTruncateAtFirstOccurrence(&pFullCommandLine, '\r');

		pktSendString(pPak, pFullCommandLine);

		for (i=0; i < GetNumContainerTypes(); i++)
		{
			ContainerDynamicInfo *pDynInfo = GetContainerDynamicInfoFromSlot(i);
			if (pDynInfo && pDynInfo->eContainerType != GLOBALTYPE_ALL)
			{

					
				estrPrintf(&pFullCommandLine, " %s ", pDynInfo->sharedCommandLine);

				estrTruncateAtFirstOccurrence(&pFullCommandLine, '\n');
				estrTruncateAtFirstOccurrence(&pFullCommandLine, '\r');

				//if we are not auto-launching a server of this type, might as well send along the specific command line
				//settings and treat them as shared. This is usually the desired "natural" behavior
				if (pDynInfo->bAutoLaunchButtonChecked)
				{
					estrConcatf(&pFullCommandLine, " - %s ", pDynInfo->commandLine);
					estrTruncateAtFirstOccurrence(&pFullCommandLine, '\n');
					estrTruncateAtFirstOccurrence(&pFullCommandLine, '\r');
				}

				PutContainerTypeIntoPacket(pPak, pDynInfo->eContainerType);

				DoTypeSpecificCommandLineStuff(&pFullCommandLine, pDynInfo->eContainerType);

				pktSendString(pPak, pFullCommandLine);
				//should the controller start future launches of this in the debugger
				pktSendBits(pPak, 1, (pDynInfo->bAutoLaunchButtonChecked && pDynInfo->bDebugButtonChecked) ? 1 : 0);
			}

		}

		estrDestroy(&pFullCommandLine);
		PutContainerTypeIntoPacket(pPak, GLOBALTYPE_NONE);
		pktSend(&pPak);
	}
}
			

bool OkayToAskControllerAbout(int iServerSlotNum)
{
	switch(GetContainerStaticInfoFromSlot(iServerSlotNum)->eControllerCreationType)
	{
	case ASKCONTROLLERTOCREATE_NEVER:
		return false;

	case ASKCONTROLLERTOCREATE_ALWAYS:
		return true;

	default:
		return stricmp(gServerLibState.controllerHost, "localhost") == 0;
	}
}

void MakeSchemasIfNecessary(void)
{
	char *pGSFileName = NULL;
	U32 iLastChanged;



	if (stricmp(gServerLibState.controllerHost, "localhost") != 0)
	{
		return;
	}

	if (isProductionMode())
	{
		return;
	}

	if (!UserIsInGroup("software"))
	{
		return;
	}

	estrPrintf(&pGSFileName, "%s/gameserver.exe", fileExecutableDir());
	fixupExeNameForFullDebugFixup(&pGSFileName);

	if (fileExists(pGSFileName))
	{
		iLastChanged = fileLastChanged(pGSFileName);
	
		if (iLastChanged != gGlobalDynamicSettings.iGameServerModificationTimeLastTimeMadeSchemas)
		{
			gGlobalDynamicSettings.iGameServerModificationTimeLastTimeMadeSchemas = iLastChanged;

			PressOtherOptionButtonByName("Make Schema Files", true);

			WriteSettingsToFile();
		}
	}


}



void SendSingleButtonMessage(HWND hDlg, GlobalType eContainerType, bool bUseSpecialGameServerIndex, int iSpecialGameServerIndex )
{
	Packet *pak;
	int i;

	if (!GetControllerLink())
	{
		return;
	}


	for (i=0; i < GetNumContainerTypes(); i++)
	{
		if (OkayToAskControllerAbout(i) && GetContainerStaticInfoFromSlot(i)->eContainerType == eContainerType)
		{
			char *pTempCommandLine = NULL;
			char typeSpecificCommandLine[1024];

			typeSpecificCommandLine[0] = 0;

			pak = pktCreate(GetControllerLink(), TO_CONTROLLER_BIGGREENBUTTON);
			pktSendBitsPack(pak, 1, 1);


			PutContainerTypeIntoPacket(pak, GetContainerStaticInfoFromSlot(i)->eContainerType);
			pktSendBits(pak, 1, GetContainerDynamicInfoFromSlot(i)->bHideButtonChecked);
			pktSendBits(pak, 1, GetContainerDynamicInfoFromSlot(i)->bDebugButtonChecked);

			if (GetContainerStaticInfoFromSlot(i)->eContainerType == GLOBALTYPE_GAMESERVER)
			{
				char *pMapName;
				if (bUseSpecialGameServerIndex)
				{
					pMapName = GetNthExtraGameServerNameFromDynamicSettings(iSpecialGameServerIndex);
				}
				else
				{
					pMapName = gGlobalDynamicSettings.mapNameForGameServer;
				}

				if (pMapName[0])
				{
					sprintf(typeSpecificCommandLine, "-mapName %s", pMapName);
				}
			}

			estrPrintf(&pTempCommandLine, "%s - %s - %s", typeSpecificCommandLine, GetContainerStaticInfoFromSlot(i)->pFixedCommandLine ? GetContainerStaticInfoFromSlot(i)->pFixedCommandLine : "", GetContainerDynamicInfoFromSlot(i)->commandLine);

			estrTruncateAtFirstOccurrence(&pTempCommandLine, '\r');
			estrTruncateAtFirstOccurrence(&pTempCommandLine, '\n');
	
			DoTypeSpecificCommandLineStuff(&pTempCommandLine, GetContainerStaticInfoFromSlot(i)->eContainerType);

			pktSendString(pak, pTempCommandLine);		
			
			estrDestroy(&pTempCommandLine);

			pktSend(&pak);

		}
	}

}




void SendBigGreenButtonMessage(HWND hDlg)
{
	Packet *pak;
	int i;
	int iNumControlledTypes = 0;


	
	//in dev mode, check if there's > 1 gig of log files > 1 week old, if so, suggest they be purged. On first
	//button press only
	if (!isProductionMode())
	{
		static int bOnce = false;

		if (!bOnce)
		{
			U64 iSize;
			char *pPrettyBytesString = NULL;

			bOnce = true;

			iSize = GetSizeOfOldFiles(fileLogDir(), 7, NULL);

			if (iSize > 1000000000)
			{
				estrMakePrettyBytesString(&pPrettyBytesString, iSize);
		
				sprintf(gGenericMessage, "Your log directory, %s, has %s of log files that are more than a week old. You might wish to purge them using the log file purging command found in the MCP utilities menu", fileLogDir(), pPrettyBytesString);
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_GENERICMESSAGE, 0, IDD_GENERICMESSAGE, false,
						genericMessageMenuDlgProc, NULL, NULL);
				estrDestroy(&pPrettyBytesString);
			}
		}
	}

	

	if (!GetControllerLink())
	{
		return;
	}

	SendCommandLineSettingsToController();

	if (!fileWatcherIsRunning())
	{
		startFileWatcher();
	}

	MakeSchemasIfNecessary();


	//make sure to send all visibility before sending BGB so that non-auto-launched things get the correct visibility
	SendAllVisibilityChoices();

	SendAllOverrideExeNamesAndDirs();

	pak = pktCreate(GetControllerLink(), TO_CONTROLLER_BIGGREENBUTTON);

	for (i=0; i < GetNumContainerTypes(); i++)
	{
		if (OkayToAskControllerAbout(i) && !GetContainerDynamicInfoFromSlot(i)->bAutoLaunchButtonChecked)
		{
			iNumControlledTypes++;		
		}
	}

	pktSendBitsPack(pak, 1, iNumControlledTypes);

	for (i=0; i < GetNumContainerTypes(); i++)
	{
		if (OkayToAskControllerAbout(i) && !GetContainerDynamicInfoFromSlot(i)->bAutoLaunchButtonChecked)
		{
			char tempCommandLine[COMMAND_LINE_SIZE * 3];
			char typeSpecificCommandLine[1024];
			char *pFirstNewline;
			typeSpecificCommandLine[0] = 0;
			PutContainerTypeIntoPacket(pak, GetContainerStaticInfoFromSlot(i)->eContainerType);
//			pktSendBits(pak, 1, IsDlgButtonChecked(hDlg, gGlobalSettings.staticContainerInfo[i].iHideButtonID)==BST_CHECKED);
//			pktSendBits(pak, 1, IsDlgButtonChecked(hDlg, gGlobalSettings.staticContainerInfo[i].iDebugButtonID)==BST_CHECKED);
			pktSendBits(pak, 1, GetContainerDynamicInfoFromSlot(i)->bHideButtonChecked);
			pktSendBits(pak, 1, GetContainerDynamicInfoFromSlot(i)->bDebugButtonChecked);

			if (GetContainerStaticInfoFromSlot(i)->eContainerType == GLOBALTYPE_GAMESERVER)
			{
				if (gGlobalDynamicSettings.mapNameForGameServer[0])
				{
					sprintf(typeSpecificCommandLine, "-mapName %s", gGlobalDynamicSettings.mapNameForGameServer);
				}
			}
			

			sprintf(tempCommandLine, "%s %s %s", typeSpecificCommandLine, GetContainerStaticInfoFromSlot(i)->pFixedCommandLine ? GetContainerStaticInfoFromSlot(i)->pFixedCommandLine : "", GetContainerDynamicInfoFromSlot(i)->commandLine);

			if (pFirstNewline = strchr(tempCommandLine, '\n'))
			{
				*pFirstNewline = 0;
			}
			if (pFirstNewline = strchr(tempCommandLine, '\r'))
			{
				*pFirstNewline = 0;
			}
			pktSendString(pak, tempCommandLine);
		}
	}

	pktSend(&pak);


	for (i=0; i < MAX_ARTIST_TOOLS; i++)
	{
		if (GetArtistToolDynamicInfo(i)->bBGBChecked)
		{
			LaunchArtistTool(i, false);
		}
	}

	for (i=0 ; i < NUM_EXTRA_GS_NAMES; i++)
	{
		if (GetNthExtraGameServerNameFromDynamicSettings(i)[0] && !gGlobalDynamicSettings.extraMapNoAutoLaunch[i])
		{
			SendSingleButtonMessage(hDlg, GLOBALTYPE_GAMESERVER, true, i);
		}
	}

}



bool MainScreenTickFunc(SimpleWindow *pWindow)
{
	static int counter = 0;
	int i;
	U32 **ppiMonitoringPorts = MCPHttpMonitoringGetMonitoredPorts();

	if (!GetControllerLink())
	{
		int seconds = 3;
		Alertf("Controller went away. Resetting all processes in %d seconds.", seconds);
		Sleep(seconds * 1000);
		ForceResetAll(NULL);
	}

	counter++;

	if (counter % LOCAL_STATUS_REQUEST_FREQUENCY == 0)
	{
		if (GetControllerLink())
		{
			Packet *pak;


			pak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_LOCAL_STATUS);

			for (i=0; i < GetNumContainerTypes(); i++)
			{
				if (OkayToAskControllerAbout(i))
				{
					PutContainerTypeIntoPacket(pak, GetContainerStaticInfoFromSlot(i)->eContainerType);
				}
			}

			PutContainerTypeIntoPacket(pak, GLOBALTYPE_NONE);

			pktSend(&pak);
		}
	}

	for (i=0; i < GetNumContainerTypes(); i++)
	{
		if (gContainerButtonIDs[i].iStatusStringID)
		{
			int iLen = (int)strlen(sStateStrings[GetContainerStaticInfoFromSlot(i)->eState]) / 3;
			char stringToUse[64];
			strcpy(stringToUse, sStateStrings[GetContainerStaticInfoFromSlot(i)->eState]/* + (counter / STRING_SCROLL_SPEED) % iLen*/);
			stringToUse[iLen] = 0;
			SetTextFast(GetDlgItem(pWindow->hWnd, gContainerButtonIDs[i].iStatusStringID), stringToUse);
		}

		if (gContainerButtonIDs[i].iStateStringID)
		{
			if (GetContainerStaticInfoFromSlot(i)->stateString[1])
			{
				SetTextFast(GetDlgItem(pWindow->hWnd, gContainerButtonIDs[i].iStateStringID), GetContainerStaticInfoFromSlot(i)->stateString);
			}
			else
			{
				SetTextFast(GetDlgItem(pWindow->hWnd, gContainerButtonIDs[i].iStateStringID), 0);
			}
		}

	}

	if (gbOpenScriptWindow)
	{
		gbOpenScriptWindow = false;
		SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_CONTROLLERSCRIPT, 0, IDD_CONTROLLERSCRIPTS, false,
			ControllerScriptDlgFunc,  ControllerScriptTickFunc, NULL);
	}

	if (gbPressButtonAutomatically)
	{
		static bool bPressedIt = false;

		if (GetControllerLink() && !bPressedIt)
		{
			bPressedIt = true;
			SendBigGreenButtonMessage(pWindow->hWnd);
		}
	}

	if (gbHttpMonitor)
	{
		BeginMCPHttpMonitoring();
	}



	for (i=0; i < ARRAY_SIZE(siServerMonButtonIDs); i++)
	{
		if (i < ea32Size(ppiMonitoringPorts))
		{
			char tempString[12];
			sprintf(tempString, "%u", (*ppiMonitoringPorts)[i]);
			SetTextFast(GetDlgItem(pWindow->hWnd, siServerMonButtonIDs[i]), tempString);
			ShowWindow(GetDlgItem(pWindow->hWnd, siServerMonButtonIDs[i]), SW_SHOW);
		}
		else
		{
			ShowWindow(GetDlgItem(pWindow->hWnd, siServerMonButtonIDs[i]), SW_HIDE);
		}
	}

	return true;

}

void KillEverything(void)
{
	int i;

	ClearControllerConnection();
		
	KillAllEx("gettex.exe", false, NULL, true, true, NULL);
	KillAllEx("getvrml.exe", false, NULL, true, true, NULL);

	KillAllArtistTools();



	for (i=0; i < GetNumContainerTypes(); i++)
	{
		KillAllEx(GetContainerStaticInfoFromSlot(i)->pCommandName, false, NULL, true, true, NULL);
	}
}

void ForceResetAll(char *pControllerScriptFileName)
{
	if (gbDontCreateController)
	{
		if (GetControllerLink())
		{
			Packet *pak;
			pak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_RESET);
			pktSend(&pak);
		}
	}
	else
	{
		KillEverything();

		Sleep(500);
		CreateAndConnectToController(pControllerScriptFileName);
	}
	
	eaClearEx(&ppQueuedErrorDialogs, NULL);
}

static void LoadMapNames(HWND hDlg)
{
	char fname[CRYPTIC_MAX_PATH];
	char *pBuf;
	

	eaDestroyEx(&sppMapNames_UnFiltered, NULL);
	

	eaPush(&sppMapNames_UnFiltered, strdup(""));

	sprintf(fname, "%s/mapname.txt", fileLocalDataDir());

	pBuf = fileAlloc(fname, NULL);

	if (pBuf)
	{
		DivideString(pBuf, "\r\n", &sppMapNames_UnFiltered, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

		free(pBuf);
	}

	UpdateGSFilter(hDlg);
}


bool ButtonWasActuallyPressed(HWND hDlg)
{


	DWORD posDword = GetMessagePos();
	POINT pos = {GET_X_LPARAM(posDword), GET_Y_LPARAM(posDword)};
	int xDistFromCenter;
	int yDistFromCenter;
	int distFromCenterSquared;

	ScreenToClient(GetDlgItem(hDlg, IDC_BIGGREENBUTTON), &pos);
	xDistFromCenter = ABS(BGB_BITMAP_SIZE / 2 - pos.x);
	yDistFromCenter = ABS(BGB_BITMAP_SIZE / 2 - pos.y);
	distFromCenterSquared = xDistFromCenter * xDistFromCenter + yDistFromCenter * yDistFromCenter;

	if (distFromCenterSquared < 3600)
	{
		return true;
	}

	return false;
}


BOOL MainScreenDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i, j;

	static HBITMAP bgb1 = 0, bgb2 = 0;
	LPDRAWITEMSTRUCT lpdis;
	HDC hdcMem;
	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			U32 **ppiMonitoringPorts = MCPHttpMonitoringGetMonitoredPorts();

			LoadMapNames(hDlg);

			if (gGlobalDynamicSettings.mapNameForGameServer[0] && eaFindString(&sppMapNames_UnFiltered, gGlobalDynamicSettings.mapNameForGameServer) == -1)
			{
				eaPush(&sppMapNames_UnFiltered, strdup(gGlobalDynamicSettings.mapNameForGameServer));
				UpdateGSFilter(hDlg);
			}


			SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_RESETCONTENT, 0, 0);
			SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_SETDROPPEDWIDTH, 300, 0);

			if (gGlobalDynamicSettings.mapNameForGameServer[0])
			{
				LRESULT lResult = SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_ADDSTRING, 0, (LPARAM)gGlobalDynamicSettings.mapNameForGameServer);
				SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_SELECTSTRING, 0, (LPARAM)gGlobalDynamicSettings.mapNameForGameServer);
				sbFilledInStartingMapNames = false;
			}




			for (j=0; j < NUM_EXTRA_GS_NAMES; j++)
			{
				sbFilledInExtraMapNames[j] = false;
				SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[j]), CB_RESETCONTENT, 0, 0);
				SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[j]), CB_SETDROPPEDWIDTH, 300, 0);

				if (GetNthExtraGameServerNameFromDynamicSettings(j)[0])
				{
					LRESULT lResult = SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[j]), CB_ADDSTRING, 0, (LPARAM)GetNthExtraGameServerNameFromDynamicSettings(j));
					SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[j]), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)0);
					SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[j]), CB_SELECTSTRING, 0, (LPARAM)GetNthExtraGameServerNameFromDynamicSettings(j));				
				}

				CheckDlgButton(hDlg, siExtraGameServerNoAutoIDs[j], gGlobalDynamicSettings.extraMapNoAutoLaunch[j] ? BST_CHECKED:BST_UNCHECKED);
			}


			

			{
				char *pTitleString = NULL;
				if (isProductionMode())
				{
					estrPrintf(&pTitleString, "%s - %s", fileExecutableDir(), GetUsefulVersionString());
				}
				else
				{
					const char *pDataDir = fileDataDir();
					const char *pBranchName = gimmeDLLQueryBranchName(pDataDir);

					if (stricmp(pBranchName, "Source Control not available") == 0)
					{
						estrPrintf(&pTitleString, "%s - (No gimme)", fileExecutableDir());
					}
					else
					{
						estrPrintf(&pTitleString, "%s - %s(%d)", fileExecutableDir(), pBranchName, gimmeDLLQueryBranchNumber(pDataDir));
					}
				}
			
				SetTextFast(hDlg, pTitleString);
				estrDestroy(&pTitleString);
			}

			if (!bgb1)
			{
				int iBmpSize;

				char *pBuffer = NULL;

				FILE *pOutFile;
				char *pTempFileName = NULL;
				
				char cwd[CRYPTIC_MAX_PATH];

				bool bFix = false;

				fileGetcwd(SAFESTR(cwd));

				if (strstri(cwd, "fix\\") || strstri(cwd, "fix/"))
				{
					bFix = true;
				}


				if (bFix)
				{
					pBuffer = fileAllocWithRetries("server/MCP/bgbupfix.bmp", &iBmpSize, 5);
				}

				if (!pBuffer)
				{
					pBuffer = fileAllocWithRetries("server/MCP/bgbup.bmp", &iBmpSize, 5);
				}
				assertmsg(pBuffer, "Couldn't load bgbup.bmp");


				pOutFile = GetTempBMPFile(&pTempFileName);

				fwrite(pBuffer, iBmpSize, 1, pOutFile);
				fclose(pOutFile);
				free(pBuffer);
		
				bgb1 = LoadImage(0, pTempFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

				assertmsgf(bgb1, "Couldn't load %s", pTempFileName);
				DeleteFile(pTempFileName);

				pBuffer = NULL;

				if (bFix)
				{
					pBuffer = fileAllocWithRetries("server/MCP/bgbdownfix.bmp", &iBmpSize, 5);
				}

				if (!pBuffer)
				{					
					pBuffer = fileAllocWithRetries("server/MCP/bgbdown.bmp", &iBmpSize, 5);
				}

				assertmsg(pBuffer, "Couldn't load bgbdown.bmp");

				pOutFile = GetTempBMPFile(&pTempFileName);

				fwrite(pBuffer, iBmpSize, 1, pOutFile);
				fclose(pOutFile);
				free(pBuffer);

				bgb2 = LoadImage(0, pTempFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

				assertmsgf(bgb2, "Couldn't load %s", pTempFileName);

				DeleteFile(pTempFileName);
				estrDestroy(&pTempFileName);

			}
			
			for (i=0; i < GetNumContainerTypes(); i++)
			{
				CheckDlgButton(hDlg, gContainerButtonIDs[i].iHideButtonID, 
					GetContainerDynamicInfoFromSlot(i)->bHideButtonChecked?BST_CHECKED:BST_UNCHECKED);
				CheckDlgButton(hDlg, gContainerButtonIDs[i].iDebugButtonID, 
					GetContainerDynamicInfoFromSlot(i)->bDebugButtonChecked?BST_CHECKED:BST_UNCHECKED);
				CheckDlgButton(hDlg, gContainerButtonIDs[i].iDontAutoLaunchButtonID, 
					GetContainerDynamicInfoFromSlot(i)->bAutoLaunchButtonChecked?BST_CHECKED:BST_UNCHECKED);

				if (gContainerButtonIDs[i].iGoButtonID)
				{
					SetWindowText(GetDlgItem(hDlg, gContainerButtonIDs[i].iGoButtonID), GlobalTypeToName(GetContainerStaticInfoFromSlot(i)->eContainerType) );
				}

				SetWindowText(GetDlgItem(hDlg, gContainerButtonIDs[i].iEditCommandLineButtonID), IsCommandLineNonEmpty(GetContainerDynamicInfoFromSlot(i)->commandLine)
					|| !StringIsAllWhiteSpace(GetContainerDynamicInfoFromSlot(i)->overrideExeName)
					|| !StringIsAllWhiteSpace(GetContainerDynamicInfoFromSlot(i)->overrideLaunchDir) ? "!!!" : "...");
				SetWindowText(GetDlgItem(hDlg, gContainerButtonIDs[i].iEditSharedCommandLineButtonID), IsCommandLineNonEmpty(GetContainerDynamicInfoFromSlot(i)->sharedCommandLine) ? "!!!" : "...");
			}

			SetWindowText(GetDlgItem(hDlg, IDC_GLOBAL_EDITCOMMANDLINE), IsCommandLineNonEmpty(gGlobalDynamicSettings.globalCommandLine) ? "!!!" : "...");

			for (i=GetNumContainerTypes(); i < MAX_CONTAINER_TYPES; i++)
			{
				if (gContainerButtonIDs[i].iHideButtonID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iHideButtonID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iDebugButtonID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iDebugButtonID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iDontAutoLaunchButtonID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iDontAutoLaunchButtonID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iGoButtonID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iGoButtonID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iStatusStringID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iStatusStringID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iStateStringID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iStateStringID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iEditCommandLineButtonID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iEditCommandLineButtonID), SW_HIDE);
				}
				if (gContainerButtonIDs[i].iEditSharedCommandLineButtonID)
				{
					ShowWindow(GetDlgItem(hDlg, gContainerButtonIDs[i].iEditSharedCommandLineButtonID), SW_HIDE);
				}
			}


			ShowWindow(GetDlgItem(hDlg, IDC_ARTEXP1), gArtToolsNeedAttention ? SW_SHOW : SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_ARTEXP2), gArtToolsNeedAttention ? SW_SHOW : SW_HIDE);
	
			ShowWindow(GetDlgItem(hDlg, IDC_ERROREXP1), gbNewErrorsInErrorScreen ? SW_SHOW : SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_ERROREXP2), gbNewErrorsInErrorScreen ? SW_SHOW : SW_HIDE);



			//if this is the complex screen, resize it
			if (GetDlgItem(hDlg, gContainerButtonIDs[MAX_CONTAINER_TYPES-1].iGoButtonID))
			{
				RECT topButtonRect;
				RECT bottomButtonRect;
				RECT windowRect;
				
				int iYDelta;

				int iNumButtonsToUse;
				int iYDeltaBetweenButtons;
	
				//first, move buttons 31+, which all just pile up in a heap currently
				GetWindowRect(GetDlgItem(hDlg, gContainerButtonIDs[FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP-1].iGoButtonID), &bottomButtonRect);
				GetWindowRect(GetDlgItem(hDlg, gContainerButtonIDs[FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP-2].iGoButtonID), &topButtonRect);
				
				iYDeltaBetweenButtons =  bottomButtonRect.bottom - topButtonRect.bottom;

				for (i = FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP; i < MAX_CONTAINER_TYPES; i++)
				{
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iDebugButtonID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iDontAutoLaunchButtonID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iEditCommandLineButtonID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iEditSharedCommandLineButtonID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iGoButtonID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iHideButtonID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iStateStringID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
					OffsetWindow(hDlg, GetDlgItem(hDlg, gContainerButtonIDs[i].iStatusStringID), 0, iYDeltaBetweenButtons * (i - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP + 1));
				}

				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_SIMPLEMODE), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_XBOX_CLIENT), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_XBOXCLIENT_EDITCOMMANDLINE), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_ARTISTSETUP), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_ARTEXP1), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_ARTEXP2), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_GOTO_OTHEROPTIONS), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_MONITOR), 0, iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));

				iNumButtonsToUse = MAX(GetNumContainerTypes(), MIN_BUTTONS_TO_ALLOW_SPACE_FOR);


				assertmsgf(GetNumContainerTypes() <= MAX_CONTAINER_TYPES, "Too many MCP container types");


				GetWindowRect(GetDlgItem(hDlg, gContainerButtonIDs[MAX_CONTAINER_TYPES-1].iGoButtonID), &bottomButtonRect);
				GetWindowRect(GetDlgItem(hDlg, gContainerButtonIDs[iNumButtonsToUse-1].iGoButtonID), &topButtonRect);
				GetWindowRect(hDlg, &windowRect);

				//force window size to what it would be on a high-res monitor, so scrolling works on 
				//small-res monitors
				windowRect.bottom = windowRect.top + 1036;

				iYDelta = bottomButtonRect.bottom - topButtonRect.bottom;

				SetWindowPos(hDlg, HWND_NOTOPMOST, windowRect.left, windowRect.top, windowRect.right - windowRect.left, 
					windowRect.bottom - windowRect.top - iYDelta + iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP), 0);

				SD_OnInitDialog_ForceSize(hDlg, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top - iYDelta + iYDeltaBetweenButtons * (MAX_CONTAINER_TYPES - FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP));

				resizeControl(hDlg, GetDlgItem(hDlg, IDC_SIMPLEMODE), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_XBOX_CLIENT), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_XBOXCLIENT_EDITCOMMANDLINE), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_ARTISTSETUP), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_ARTEXP1), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_ARTEXP2), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_GOTO_OTHEROPTIONS), 0, -iYDelta, 0, 0);
				resizeControl(hDlg, GetDlgItem(hDlg, IDC_MONITOR), 0, -iYDelta, 0, 0);
			
				for (i=0; i < ARRAY_SIZE(siServerMonButtonIDs); i++)
				{
					resizeControl(hDlg, GetDlgItem(hDlg, siServerMonButtonIDs[i]), 0, -iYDelta, 0, 0);
				}

			}

			if (gbStartMinimized)
			{
				static bool bOnce = false;

				if (!bOnce)
				{
					bOnce = true;
					ShowWindow(hDlg, SW_MINIMIZE);
				}
			}

			if (gbSendHideWindowsChoicesOnStartup)
			{
				SendAllVisibilityChoices();
			}

	
			return TRUE; 

		}
		break;

        case WM_DRAWITEM: 
            lpdis = (LPDRAWITEMSTRUCT) lParam; 
            hdcMem = CreateCompatibleDC(lpdis->hDC); 
 
			ANALYSIS_ASSUME(bgb1);
			ANALYSIS_ASSUME(bgb2);
            if ((lpdis->itemState & ODS_SELECTED) && ButtonWasActuallyPressed(hDlg))  // if selected 
                SelectObject(hdcMem, bgb2); 
            else 
                SelectObject(hdcMem, bgb1); 
 
            // Destination 
            StretchBlt( 
                lpdis->hDC,         // destination DC 
                lpdis->rcItem.left, // x upper left 
                lpdis->rcItem.top,  // y upper left 
 
                // The next two lines specify the width and 
                // height. 
                lpdis->rcItem.right - lpdis->rcItem.left, 
                lpdis->rcItem.bottom - lpdis->rcItem.top, 
                hdcMem,    // source device context 
                0, 0,      // x and y upper left 
                BGB_BITMAP_SIZE,        // source bitmap width PLUS ONE (not sure why)
                BGB_BITMAP_SIZE,        // source bitmap height PLUS ONE (not sure why)
                SRCCOPY);  // raster operation 
 
            DeleteDC(hdcMem); 
            return TRUE; 



	case WM_CLOSE:
		pWindow->bCloseRequested = true;
		break;

	case WM_SIZE:
		SD_OnSize(hDlg, wParam, LOWORD(lParam), HIWORD(lParam));
		break;

	case WM_HSCROLL:
		SD_OnHScroll(hDlg, LOWORD(wParam));
		break;

	case WM_VSCROLL:
		SD_OnVScroll(hDlg, LOWORD(wParam));
		break;

	case WM_COMMAND:
		if (gbCommandLinesMightHaveChanged)
		{
			gbCommandLinesMightHaveChanged = false;

			for (i=0; i < GetNumContainerTypes(); i++)
			{
			
				SetWindowText(GetDlgItem(hDlg, gContainerButtonIDs[i].iEditCommandLineButtonID), IsCommandLineNonEmpty(GetContainerDynamicInfoFromSlot(i)->commandLine)
					|| !StringIsAllWhiteSpace(GetContainerDynamicInfoFromSlot(i)->overrideExeName)
					|| !StringIsAllWhiteSpace(GetContainerDynamicInfoFromSlot(i)->overrideLaunchDir) ? "!!!" : "...");
				SetWindowText(GetDlgItem(hDlg, gContainerButtonIDs[i].iEditSharedCommandLineButtonID), IsCommandLineNonEmpty(GetContainerDynamicInfoFromSlot(i)->sharedCommandLine) ? "!!!" : "...");
			}
		
			SetWindowText(GetDlgItem(hDlg, IDC_GLOBAL_EDITCOMMANDLINE), IsCommandLineNonEmpty(gGlobalDynamicSettings.globalCommandLine) ? "!!!" : "...");
		}


		switch (LOWORD (wParam))
		{
			case IDC_GSFILTER:
				{
					UpdateGSFilter(hDlg);
				}
				break;

			case IDC_BIGGREENBUTTON:
				{
					if (!ButtonWasActuallyPressed(hDlg))
					{
						return FALSE;
					}


					if (!gbProgrammer && fileLastChanged(gExecutableFilename) != giExecutableTimestamp)
					{
						sprintf(gQuitMessage, "The MCP executable has changed. Please restart it.");
						ForceResetAll(NULL);
						pWindow->bCloseRequested = true;
						SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_QUITMESSAGE, 0, IDD_QUITMESSAGE,
							true, &QuitMessageDlgFunc, NULL, NULL);
					}
					else
					{
						SendBigGreenButtonMessage(hDlg);
					}
				}
			break;
//there is currently no "normal" reset button
/*
			case IDC_RESET:
				{
					Packet *pak;

					if (GetControllerLink())
					{
						pak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_RESET);
						pktSend(&pak, GetControllerLink());
					}
				}
			break;
*/
			case IDC_FORCE_RESET_ALL:
				ForceResetAll(NULL);
				break;
	
			case IDC_COMPLEXMODE:
				pWindow->bCloseRequested = true;
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MAIN, MAININDEX_COMPLEX, IDD_BGB_COMPLEX, true,
					MainScreenDlgFunc, MainScreenTickFunc, NULL);
				break;

			case IDC_SIMPLEMODE:
				pWindow->bCloseRequested = true;
				if (gbArtist)
				{
					SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MAIN, MAININDEX_ARTIST, IDD_BGB_ARTISTS, true,
						MainScreenDlgFunc, MainScreenTickFunc, NULL);

				}
				else
				{
					SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_MAIN, MAININDEX_SIMPLE, IDD_BGB_SIMPLE, true,
						MainScreenDlgFunc, MainScreenTickFunc, NULL);
				}				
				break;

			case IDC_ARTISTSETUP:
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_ARTSETUP, 0, IDD_ARTISTSETUP, false,
						ArtistSetupDlgFunc, ArtistSetupTickFunc, NULL);
				break;

			case IDC_GOTO_OTHEROPTIONS:
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_OTHEROPTIONS, 0, IDD_OTHEROPTIONS, false,
					OtherOptionsDlgFunc, NULL, NULL);
				break;

			case IDC_CONTROLLERSCRIPTS:
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_CONTROLLERSCRIPT, 0, IDD_CONTROLLERSCRIPTS, false,
					 ControllerScriptDlgFunc,  ControllerScriptTickFunc, NULL);
				break;

			case IDC_MONITOR:
				//				MCP_CreateGraphicsWindow();
				{
					char URL[1024];
					U32 **ppiMonitoringPorts = MCPHttpMonitoringGetMonitoredPorts();


					BeginMCPHttpMonitoring();

					ppiMonitoringPorts = MCPHttpMonitoringGetMonitoredPorts();

					if (ea32Size(ppiMonitoringPorts))
					{
						if ((*ppiMonitoringPorts)[0] == 80)
						{
							sprintf(URL, "http://localhost/viewxpath");
						}
						else
						{
							sprintf(URL, "http://localhost:%d/viewxpath", (*ppiMonitoringPorts)[0]);
						}
						openURL(URL);
					}	
				}
				break;
			
			case IDC_GLOBAL_EDITCOMMANDLINE:
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, 0, IDD_EDITCOMMANDLINE, false,
					 EditCommandLineDlgFunc,  NULL, NULL);
				break;

			case IDC_XBOXCLIENT_EDITCOMMANDLINE:
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, FAKE_GLOBALTYPE_XBOXCLIENT, IDD_EDITCOMMANDLINE, false,
					 EditCommandLineDlgFunc,  NULL, NULL);
				break;

		case IDC_UTILITIES:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_UTILITIES, 0, IDD_UTILITIES, false,
				utilitiesMenuDlgProc,  NULL, NULL);
			break;

		case IDC_CONFIGURE:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_CONFIGURE, 0, IDD_CONFIG, false,
				configMenuDlgProc,  NULL, NULL);
			break;

		case IDC_ERROR_WINDOW:
			gbNewErrorsInErrorScreen = false;

			ShowWindow(GetDlgItem(hDlg, IDC_ERROREXP1), SW_HIDE);
			ShowWindow(GetDlgItem(hDlg, IDC_ERROREXP2), SW_HIDE);


			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_ERRORS, 0, IDD_ERRORS, false,
				errorsDlgProc,  NULL, NULL);
			break;

			case IDC_XBOX_CLIENT:
				{
					char systemString[1024 + COMMAND_LINE_SIZE * 2];
					char *pFirstNewline;


					sprintf(systemString, "\"%s\\scripts\\xboxsetup\\run_on_xbox.bat\" - %s - %s", fileToolsDir(), GetGlobalCommandLine(), gGlobalDynamicSettings.xboxClientCommandLine);

					if (pFirstNewline = strchr(systemString, '\n'))
					{
						*pFirstNewline = 0;
					}
					system_detach(systemString, false, false);
				}
				break;

			case IDC_BACK:
				KillEverything();
				SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_STARTING, 0, 
					IDD_MCP_START, true,
					MCPStartDlgFunc, MCPStartTickFunc, NULL);
				pWindow->bCloseRequested = true;
				break;

			case IDC_STARTINGGSMAP:
				{
					LRESULT lResult;
					char **ppFilteredNames = GetFilteredMapNameList();

					if (!sbFilledInStartingMapNames)
					{
						sbFilledInStartingMapNames = true;
						SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_RESETCONTENT, 0, 0);
						SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_SETDROPPEDWIDTH, 300, 0);


						for (i=0; i < eaSize(&ppFilteredNames); i++)
						{

							lResult = SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_ADDSTRING, 0, (LPARAM)ppFilteredNames[i]);
							SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
						}

						SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_SELECTSTRING, 0, (LPARAM)gGlobalDynamicSettings.mapNameForGameServer);
					}

					lResult = SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						lResult = SendMessage(GetDlgItem(hDlg, IDC_STARTINGGSMAP), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
						if (lResult != CB_ERR) 
						{			
							if (lResult >= 0 && lResult < eaSize(&ppFilteredNames))
							{
								if (stricmp(gGlobalDynamicSettings.mapNameForGameServer, ppFilteredNames[lResult]) != 0)
								{
									strcpy(gGlobalDynamicSettings.mapNameForGameServer, ppFilteredNames[lResult]);
									WriteSettingsToFile();
								}
							}
						}
					}
				}
				break;



			default:
				for (i=0; i < NUM_EXTRA_GS_NAMES; i++)
				{
					if (LOWORD(wParam) == siLaunchExtraGameServerIDs[i] && GetNthExtraGameServerNameFromDynamicSettings(i)[0])
					{
						SendCommandLineSettingsToController();
						SendSingleButtonMessage(hDlg, GLOBALTYPE_GAMESERVER, true, i);
					}

					if (LOWORD(wParam) == siExtraGameServerMapIDs[i])
					{
						LRESULT lResult;
						char **ppFilteredNames = GetFilteredMapNameList();


						if (!sbFilledInExtraMapNames[i])
						{
							sbFilledInExtraMapNames[i] = true;
							SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_RESETCONTENT, 0, 0);
							SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_SETDROPPEDWIDTH, 300, 0);


							for (j=0; j < eaSize(&ppFilteredNames); j++)
							{

								lResult = SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_ADDSTRING, 0, (LPARAM)ppFilteredNames[j]);
								SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)j);
							}

							if (GetNthExtraGameServerNameFromDynamicSettings(i)[0])
							{
								SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_SELECTSTRING, 0, (LPARAM)GetNthExtraGameServerNameFromDynamicSettings(i));
							}
						}



						lResult = SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
						if (lResult != CB_ERR) 
						{
							lResult = SendMessage(GetDlgItem(hDlg, siExtraGameServerMapIDs[i]), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
							if (lResult != CB_ERR) 
							{			
								if (lResult >= 0 && lResult < eaSize(&ppFilteredNames))
								{
									if (stricmp(GetNthExtraGameServerNameFromDynamicSettings(i), ppFilteredNames[lResult]) != 0)
									{
										SetNthExtraGameServerNameFromDynamicSettings(i, ppFilteredNames[lResult]);
										WriteSettingsToFile();
									}
								}
							}
						}		
					}

					if (LOWORD(wParam) == siExtraGameServerNoAutoIDs[i])
					{
						gGlobalDynamicSettings.extraMapNoAutoLaunch[i] = IsDlgButtonChecked(hDlg, siExtraGameServerNoAutoIDs[i]);
						WriteSettingsToFile();
					}
				}


				for (i=0; i < ARRAY_SIZE(siServerMonButtonIDs); i++)
				{
					if (LOWORD(wParam) == siServerMonButtonIDs[i])
					{
						U32 **ppiMonitoringIDs = MCPHttpMonitoringGetMonitoredPorts();
						if (i <= ea32Size(ppiMonitoringIDs))
						{
							SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_SERVERMONITORCONTROL, (*ppiMonitoringIDs)[i], 
								IDD_SERVERMONCONTROL, false,
								ServerMonitorControlScreenDlgFunc, ServerMonitorControlScreenTickFunc, NULL);
						}
						return FALSE;
					}
				}
				for (i=0 ; i < GetNumContainerTypes(); i++)
				{
					if (LOWORD (wParam) == gContainerButtonIDs[i].iHideButtonID)
					{
						int action = HIWORD(wParam);
						if (action ==  BN_CLICKED && GetControllerLink())
						{
							bool bNowHidden = GetContainerDynamicInfoFromSlot(i)->bHideButtonChecked = IsDlgButtonChecked(hDlg, gContainerButtonIDs[i].iHideButtonID);
							
							Packet *pak;

							pak = pktCreate(GetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_HIDE_SERVER);

							PutContainerTypeIntoPacket(pak, GetContainerStaticInfoFromSlot(i)->eContainerType);
							pktSendBits(pak, 1, bNowHidden);

							pktSend(&pak);

							WriteSettingsToFile();

						}
					}
					else if (LOWORD (wParam) == gContainerButtonIDs[i].iGoButtonID)
					{
						if (GetContainerStaticInfoFromSlot(i)->eContainerType == GLOBALTYPE_CLIENT && stricmp(gServerLibState.controllerHost, "localhost") != 0)
						{
							char tempCommandLine[COMMAND_LINE_SIZE * 2];
							char *pFirstNewline;

	
							sprintf(tempCommandLine, "%s %s", GetContainerStaticInfoFromSlot(i)->pFixedCommandLine ? GetContainerStaticInfoFromSlot(i)->pFixedCommandLine : "", GetContainerDynamicInfoFromSlot(i)->commandLine);

							if (pFirstNewline = strchr(tempCommandLine, '\n'))
							{
								*pFirstNewline = 0;
							}

							CreateLocalClientForNonLocalConnection(
								GetContainerDynamicInfoFromSlot(i)->bHideButtonChecked,
								GetContainerDynamicInfoFromSlot(i)->bDebugButtonChecked,
								tempCommandLine);
						}
						else
						{
							SendCommandLineSettingsToController();
							SendSingleButtonMessage(hDlg, GetContainerStaticInfoFromSlot(i)->eContainerType, false, 0);
						}
					}
					else if (LOWORD (wParam) == gContainerButtonIDs[i].iDebugButtonID)
					{
						GetContainerDynamicInfoFromSlot(i)->bDebugButtonChecked = IsDlgButtonChecked(hDlg, gContainerButtonIDs[i].iDebugButtonID);
						WriteSettingsToFile();
						
						if (GetContainerStaticInfoFromSlot(i)->eContainerType == GLOBALTYPE_GAMESERVER && GetControllerLink())
						{							
							Packet *pak;

							pak = pktCreate(GetControllerLink(), TO_CONTROLLER_SET_GAMESERVERS_START_IN_DEBUGGER);

							pktSendBits(pak, 1, IsDlgButtonChecked(hDlg, gContainerButtonIDs[i].iDebugButtonID));

							pktSend(&pak);
						}

					}
					else if (LOWORD (wParam) == gContainerButtonIDs[i].iDontAutoLaunchButtonID)
					{
						GetContainerDynamicInfoFromSlot(i)->bAutoLaunchButtonChecked = IsDlgButtonChecked(hDlg, gContainerButtonIDs[i].iDontAutoLaunchButtonID);
						WriteSettingsToFile();

					}
					else if (LOWORD(wParam) == gContainerButtonIDs[i].iEditCommandLineButtonID)
					{
						SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, GetContainerStaticInfoFromSlot(i)->eContainerType, IDD_EDITCOMMANDLINE, false,
							 EditCommandLineDlgFunc,  NULL, NULL);
					}
					else if (LOWORD(wParam) == gContainerButtonIDs[i].iEditSharedCommandLineButtonID)
					{
						SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, GetContainerStaticInfoFromSlot(i)->eContainerType + COMMAND_LINE_WINDOW_SHARED_OFFSET, IDD_EDITCOMMANDLINE, false,
							 EditCommandLineDlgFunc,  NULL, NULL);
					}

				}
				break;
		}
		break;
	}
	
	return FALSE;
}

void CreateLocalClientForNonLocalConnection(bool bHide, bool bStartInDebugger, char *pExtraCommandLine)
{
	U32 iLoginServerIP = ServerList_GetServerIP(GLOBALTYPE_LOGINSERVER);

	if (iLoginServerIP)
	{
		char commandLine[1024];
		sprintf(commandLine, "./GameClient -server %s %s %s - %s", makeIpStr(iLoginServerIP),
			bHide ? "-hide" : "", bStartInDebugger ? "-WaitForDebugger" : "", pExtraCommandLine);	
		system_detach(commandLine, 1, false);
	}
}

static char *spLastFilterString = NULL;
static char *spCurFilterString = NULL;
static char *spLastStringReturnedFilteredListFor = NULL;

static char **sppFilteredList = NULL;

void UpdateGSFilter(HWND hDlg)
{
	estrGetWindowText(&spCurFilterString, GetDlgItem(hDlg, IDC_GSFILTER));

	if (stricmp_safe(spCurFilterString, spLastFilterString) == 0)
	{
		return;
	}

	sbFilledInStartingMapNames = 0;
	memset(sbFilledInExtraMapNames, 0, sizeof(sbFilledInExtraMapNames));
	estrCopy(&spLastFilterString, &spCurFilterString);
}

char **GetFilteredMapNameList(void)
{
	int i;

	if (estrLength(&spCurFilterString) == 0)
	{
		return sppMapNames_UnFiltered;
	}

	if (stricmp_safe(spLastStringReturnedFilteredListFor, spCurFilterString) == 0)
	{
		return sppFilteredList;
	}

	eaDestroyEx(&sppFilteredList, NULL);

	eaPush(&sppFilteredList, strdup(""));

	for (i = 0; i < eaSize(&sppMapNames_UnFiltered); i++)
	{
		if (strstri(sppMapNames_UnFiltered[i], spCurFilterString))
		{
			eaPush(&sppFilteredList, strdup(sppMapNames_UnFiltered[i]));
		}
	}

	estrCopy(&spLastStringReturnedFilteredListFor, &spCurFilterString);
	return sppFilteredList;
}