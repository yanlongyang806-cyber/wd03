#include "mastercontrolprogram.h"
#include "estring.h"
#include "fileutil.h"
#include "genericdialog.h"
#include "earray.h"
#include "fileutil2.h"
#include "sysutil.h"
#include "GlobalTypes.h"
#include "error.h"

bool gbStartArtToolConsolesVisible = true;
AUTO_CMD_INT(gbStartArtToolConsolesVisible, StartArtToolConsolesVisible) ACMD_CMDLINE;


int artistToolPids[MAX_ARTIST_TOOLS] = { 0 };
HWND artistToolHWNDs[MAX_ARTIST_TOOLS] = {0};

char *gPrintfBuffers[MAX_PRINTF_BUFFERS] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
bool gbPrintfBuffersChanged[MAX_PRINTF_BUFFERS] = { true, true, true, true, true, true, true, true };
//bits of this byte specify whether each individual art tool needs attention
char gArtToolsNeedAttention = 0;

MCPArtistToolButtonIDs gArtistToolButtonIDs[MAX_ARTIST_TOOLS] =
{
	{ 
		IDC_LAUNCHGETANIM,
		IDC_GETANIM_EXP,
		IDC_GETANIM_ARROW,
		IDC_GETANIMBGB,
		0,
	},
	{

		IDC_LAUNCHGETTEXTURES,
		IDC_GETTEX_EXP,
		IDC_GETTEX_ARROW,
		IDC_GETTEXTURESBGB,
		0,
	},
	{ 

		IDC_LAUNCHGETOBJECTLIBRARY,
		IDC_GETOBJLIB_EXP,
		IDC_GETOBJLIB_ARROW,
		IDC_GETOBJECTLIBRARYBGB,
		IDC_GETOBJECTLIBRARY_NOLODS,
	},
	{ 

		IDC_LAUNCHGETPLAYERGEOM,
		IDC_GETPLAYERGEOM_EXP,
		IDC_GETPLAYERGEOM_ARROW,
		IDC_GETPLAYERGEOMBGB,
		IDC_GETPLAYERGEOM_NOLOD,
	},
};





int giActiveArtToolPrintBuffer = 0;

int GetNumArtistTools(void)
{
	return eaSize(&gGlobalStaticSettings.ppArtistToolStaticInfo);
}

ArtistToolStaticInfo *GetArtistToolStaticInfo(int iSlotNum)
{
	assert(iSlotNum >= 0 && iSlotNum < eaSize(&gGlobalStaticSettings.ppArtistToolStaticInfo));

	return gGlobalStaticSettings.ppArtistToolStaticInfo[iSlotNum];
}

ArtistToolDynamicInfo *GetArtistToolDynamicInfo(int iSlotNum)
{
	assert(iSlotNum >= 0 && iSlotNum < eaSize(&gGlobalDynamicSettings.ppArtistToolDynamicInfo));

	return gGlobalDynamicSettings.ppArtistToolDynamicInfo[iSlotNum];
}


void SetActiveArtToolPrintfBuffer(int iActiveBuffer)
{
	giActiveArtToolPrintBuffer = iActiveBuffer;
	gArtToolsNeedAttention &= ~(1 << iActiveBuffer);
}

void ResetArtToolIcons(void)
{
	int i;
	SimpleWindow *pWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_ARTSETUP);

	if (!pWindow)
	{
		return;
	}

	for (i=0; i < GetNumArtistTools(); i++)
	{
		ShowWindow(GetDlgItem(pWindow->hWnd, gArtistToolButtonIDs[i].iExclamationPointID),
			(gArtToolsNeedAttention & ( 1 << i)) ? SW_SHOW : SW_HIDE);

		ShowWindow(GetDlgItem(pWindow->hWnd, gArtistToolButtonIDs[i].iArrowID),
			(i == giActiveArtToolPrintBuffer) ? SW_SHOW : SW_HIDE);
	}
}
#define RTFHEADER "{\\rtf1\\ansi\\ansicpg1252\\deff0\\deflang1033{\\fonttbl{\\f0\\fswiss\\fcharset0 Courier New;}} {\\colortbl ;\\red0\\green0\\blue0;\\red0\\green0\\blue128;\\red0\\green128\\blue0;\\red0\\green128\\blue128;\\red128\\green0\\blue0;\\red128\\green0\\blue128;\\red128\\green128\\blue0;\\red128\\green128\\blue128;\\red255\\green255\\blue255;\\red0\\green0\\blue255;\\red0\\green255\\blue0;\\red0\\green255\\blue255;\\red255\\green0\\blue0;\\red255\\green0\\blue255;\\red255\\green255\\blue0;\\red255\\green255\\blue255;} "

char *pEmptyRTFString = RTFHEADER "}";

void UpdateArtToolPrintfBuffer(bool bForce)
{
	SimpleWindow *pWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_ARTSETUP);

	if (!pWindow)
	{
		return;
	}

	if (gbPrintfBuffersChanged[giActiveArtToolPrintBuffer] || bForce)
	{
		ResetArtToolIcons();

		gbPrintfBuffersChanged[giActiveArtToolPrintBuffer] = false;
		if (gPrintfBuffers[giActiveArtToolPrintBuffer])
		{
			const int bufSize = estrLength(&gPrintfBuffers[giActiveArtToolPrintBuffer]);
			const int place = (bufSize - 28000 > 0 ? bufSize - 28000 : 0);	// Ensures that the information displayed will always track what's at the "bottom" of the scroll at the expense of possible cutting off what appears at the top.
			const int tempSize = (bufSize > 28000 ? 29000 : bufSize + 1000);
			char *pTempSmallString = malloc(tempSize);
			snprintf_s(pTempSmallString,tempSize,"%s%s",RTFHEADER,&(gPrintfBuffers[giActiveArtToolPrintBuffer][place]));
			SetWindowText(GetDlgItem(pWindow->hWnd, IDC_TOOLOUTPUT), pTempSmallString);
			free(pTempSmallString);

			SendMessage( GetDlgItem(pWindow->hWnd, IDC_TOOLOUTPUT),      
				(UINT) EM_SCROLL,
				SB_PAGEDOWN, 0 );
		}
		else
		{
			SetWindowText(GetDlgItem(pWindow->hWnd, IDC_TOOLOUTPUT), pEmptyRTFString);
		}
	}
}

void KillArtistTool(int iWhich)
{
	if (artistToolPids[iWhich] && ProcessNameMatch( artistToolPids[iWhich] , GetArtistToolStaticInfo(iWhich)->pExeName, true))
	{
		kill(artistToolPids[iWhich]);
		artistToolPids[iWhich] = 0;
		artistToolHWNDs[iWhich] = 0;

		estrCopy2(&gPrintfBuffers[iWhich], "");
		gbPrintfBuffersChanged[iWhich] = true;
	}
}
void KillAllArtistTools(void)
{
	int i;

	for (i=0; i < MAX_ARTIST_TOOLS; i++)
	{
		KillArtistTool(i);
	}
}


void FindArtistToolConsoleWindow(int iWhichTool)
{

	int iCount = 0;

	if (!artistToolPids[iWhichTool])
	{
		return;
	}

	do
	{

		artistToolHWNDs[iWhichTool] = FindHWNDFromPid(artistToolPids[iWhichTool]);
		

		if (artistToolHWNDs[iWhichTool])
		{
			break;
		}

		Sleep(10);
		iCount++;
	}
	while (iCount < 20);
}

// Overrides the location of Art Tools EXE source location from the MCP project EXE folder (e.g. c:\FightClub\tools\bin)
// to the developer-built source tree c:\src\utilities\bin. These must already be built.
static char runGetVrmlFromSrc[CRYPTIC_MAX_PATH] = "";
AUTO_COMMAND ACMD_NAME(RunGetVrmlFromSrc) ACMD_COMMANDLINE;
void cmdRunGetVrmlFromSrc(int runFromSrc)
{
	strcpy(runGetVrmlFromSrc, runFromSrc ? "c:\\src\\utilities\\bin" : "");
}

// Allows overriding location of Art Tools EXE source location from the MCP project EXE folder (e.g. c:\FightClub\tools\bin)
// to a developer-built source tree. These must already be built.
// Specify the full path to the location of the executables, i.e. c:\srcFightClubFix\utilities\bin.
AUTO_COMMAND ACMD_NAME(RunGetVrmlFromSrcDir) ACMD_COMMANDLINE;
void cmdRunGetVrmlFromSrcDir(const char * srcDir)
{
	strcpy(runGetVrmlFromSrc, srcDir);
}

void LaunchArtistTool(int iWhichTool, bool bReLaunchIfRunning)
{
	char cwd[1024];
	char *pCommandLine = NULL;
	char fixedSrcDir[1024];
	char fixedCommandString[1024];

	if (artistToolPids[iWhichTool] && ProcessNameMatch( artistToolPids[iWhichTool] , GetArtistToolStaticInfo(iWhichTool)->pExeName, true))
	{
		if (!bReLaunchIfRunning)
		{
			SetActiveArtToolPrintfBuffer(iWhichTool);
			UpdateArtToolPrintfBuffer(true);
			return;
		}
		else
		{
			kill(artistToolPids[iWhichTool]);
		}
	}


	fileGetcwd(cwd, 1023);


	strcpy(fixedSrcDir, GetArtistToolStaticInfo(iWhichTool)->pDirectory);
	strcpy(fixedCommandString, GetArtistToolStaticInfo(iWhichTool)->pCommandString);

	if (runGetVrmlFromSrc[0])
	{
		ReplaceStrings(fixedCommandString, 1024, "$TOOLSBIN$", runGetVrmlFromSrc, true);
	}

	ApplyDirectoryMacros(fixedSrcDir, true);
	ApplyDirectoryMacros(fixedCommandString, true);

	backSlashes(fixedSrcDir);
	backSlashes(fixedCommandString);
	assert(chdir(fixedSrcDir) ==0);
	
	estrPrintf(&pCommandLine, FORMAT_OK(fixedCommandString),
		DEFAULT_MCP_PRINTF_PORT, iWhichTool, GetArtistToolDynamicInfo(iWhichTool)->bExtraChecked ? GetArtistToolStaticInfo(iWhichTool)->pExtraString : "");
	estrReplaceOccurrences(&pCommandLine, "$PROJNAME$", GetProductName());
	estrReplaceOccurrences(&pCommandLine, "$SHORTPROJNAME$", GetShortProductName());

	if (gbStartArtToolConsolesVisible)
	{
		estrReplaceOccurrences(&pCommandLine, "-hide", "");
	}

	estrConcatf(&pCommandLine, " -ConnectToControllerAndSendErrors ");

	artistToolPids[iWhichTool] = system_detach_with_fulldebug_fixup(pCommandLine, gbStartArtToolConsolesVisible ? false : true, gbStartArtToolConsolesVisible ? false : true);

	estrDestroy(&pCommandLine);

	FindArtistToolConsoleWindow(iWhichTool);

	assert(chdir(cwd) == 0);

	if (gPrintfBuffers[iWhichTool])
	{
		estrCopy2(&gPrintfBuffers[iWhichTool], "");
	}

	SetActiveArtToolPrintfBuffer(iWhichTool);
	UpdateArtToolPrintfBuffer(true);



}

void ResetArtToolButtonText(HWND hWnd, int iButtonNum)
{
	char tempString[128];

	sprintf(tempString, "%s%s", GetArtistToolStaticInfo(iButtonNum)->pName, 
		(artistToolPids[iButtonNum] && ProcessNameMatch( artistToolPids[iButtonNum] , GetArtistToolStaticInfo(iButtonNum)->pExeName, true)) ? " (running)" : "");

	SetWindowText(GetDlgItem(hWnd, gArtistToolButtonIDs[iButtonNum].iLaunchButtonID), tempString);
}


void SetArtToolNeedsAttention(int iArtToolNum)
{
	SimpleWindow *pArtWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_ARTSETUP);
	SimpleWindow *pMainWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_MAIN);

	if (pArtWindow)
	{
		flashWindow(pArtWindow->hWnd);
	}
	else if (pMainWindow)
	{
		flashWindow(pMainWindow->hWnd);
	}

	if (pArtWindow && iArtToolNum == giActiveArtToolPrintBuffer)
	{
		return;
	}


	
	gArtToolsNeedAttention |= ( 1 << iArtToolNum);

	//if we are in the artist setup screen but looking at a different buffer
	if (pArtWindow)
	{
		ResetArtToolIcons();
	}


	if (pMainWindow)
	{
		ShowWindow(GetDlgItem(pMainWindow->hWnd, IDC_ARTEXP1), SW_SHOW);
		ShowWindow(GetDlgItem(pMainWindow->hWnd, IDC_ARTEXP2), SW_SHOW);
	}
}

