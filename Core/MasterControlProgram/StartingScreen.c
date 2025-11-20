#include "mastercontrolprogram.h"
#include "fileutil.h"
#include "estring.h"
#include "timing.h"
#include "mcpUtilities.h"
#include "GlobalComm.h"
#include "sysutil.h"
#include "accountnet.h"
#include "GlobalTypes.h"
#include "winutil.h"
#include "accountinfo.h"
#include "patchclient.h"
#include "accountnet_h_ast.h"

bool sbRefreshBegan = false;

#define AUTO_REFRESH_SERVER_LIST_DELAY 600

ControllerTrackerConnectionStatusStruct sControllerTrackerConnectionStatus = {0};

bool MCPStartTickFunc(SimpleWindow *pWindow)
{
	static int iCounter = 0;
	int iCurTime = timeSecondsSince2000();

	char *pControllerTrackerStatusString = NULL;

	AccountFSM_Update();
	if (sbRefreshBegan && sAccountState == ACCOUNTSERVERSTATE_SUCCEEDED)
	{
		sbRefreshBegan = false;

		if (gpLinkToNewControllerTracker)
		{
			Packet *pak;

			pak = pktCreate(gpLinkToNewControllerTracker, FROM_MCP_TO_NEWCONTROLLERTRACKER_REQUEST_SHARD_LIST);
			pktSendString(pak, GetProductName());
			if (gpTicket)
				pktSendString(pak, gpTicket);
			else
			{
				pktSendString(pak, ACCOUNT_FASTLOGIN_LABEL);
				pktSendU32(pak, guAccountID);
				pktSendU32(pak, guTicketID);
			}
			pktSend(&pak);
		}
	}

	if (UpdateControllerTrackerConnection(&sControllerTrackerConnectionStatus, &pControllerTrackerStatusString))
	{
	
	}
	else
	{
		EnableWindow(GetDlgItem(pWindow->hWnd, IDC_STATIC_CHOOSE_EXISTING), false);
		EnableWindow(GetDlgItem(pWindow->hWnd, IDC_CHOOSESERVER), false);
		EnableWindow(GetDlgItem(pWindow->hWnd, IDC_REFRESH_SERVER_LIST), false);

		ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MESSAGE), SW_SHOW);
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_MESSAGE), pControllerTrackerStatusString);
		estrDestroy(&pControllerTrackerStatusString);
		return true;
	}



	if (gbAvailableShardsChanged)
	{
		if (gAvailableShardList.pMessage && gAvailableShardList.pMessage[0])
		{
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MESSAGE), SW_SHOW);
			SetTextFast(GetDlgItem(pWindow->hWnd, IDC_MESSAGE), gAvailableShardList.pMessage);
		}
		else
		{	
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MESSAGE), SW_HIDE);
		}

		gbAvailableShardsChanged = false;

		if (eaSize(&gAvailableShardList.ppShards))
		{
			LRESULT lResult;

			lResult = SendMessage(GetDlgItem(pWindow->hWnd, IDC_CHOOSESERVER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			if (lResult != CB_ERR) 
			{
				lResult = SendMessage(GetDlgItem(pWindow->hWnd, IDC_CHOOSESERVER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					ShardInfo_Basic *pSelectedShard = gAvailableShardList.ppShards[lResult];

					if (pSelectedShard->pPatchCommandLine && pSelectedShard->pPatchCommandLine[0])
					{
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHPC), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHPC_COMMANDLINE), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHXBOX), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHXBOX_COMMANDLINE), SW_SHOW);
#if !STANDALONE
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHCLIENT), SW_HIDE);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHCLIENT_COMMANDLINE), SW_HIDE);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MONITOR_LOCALLY), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MONITOR_EXISTING_MCP), pSelectedShard->bHasLocalMontiringMCP ? SW_SHOW : SW_HIDE);
#endif
					}
					else
					{

						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHPC), SW_HIDE);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHPC_COMMANDLINE), SW_HIDE);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHXBOX), SW_HIDE);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHXBOX_COMMANDLINE), SW_HIDE);
#if !STANDALONE
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHCLIENT), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHCLIENT_COMMANDLINE), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MONITOR_LOCALLY), SW_SHOW);
						ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MONITOR_EXISTING_MCP), pSelectedShard->bHasLocalMontiringMCP ? SW_SHOW : SW_HIDE);
#endif
					}
				}
			}

			EnableWindow(GetDlgItem(pWindow->hWnd, IDC_STATIC_CHOOSE_EXISTING), true);
			EnableWindow(GetDlgItem(pWindow->hWnd, IDC_CHOOSESERVER), true);
			EnableWindow(GetDlgItem(pWindow->hWnd, IDC_REFRESH_SERVER_LIST), true);

		}
		else
		{
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHPC), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHPC_COMMANDLINE), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHXBOX), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_PATCHXBOX_COMMANDLINE), SW_HIDE);
#if !STANDALONE
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHCLIENT), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_LAUNCHCLIENT_COMMANDLINE), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MONITOR_LOCALLY), SW_HIDE);
			ShowWindow(GetDlgItem(pWindow->hWnd, IDC_MONITOR_EXISTING_MCP), SW_HIDE);
#endif

			EnableWindow(GetDlgItem(pWindow->hWnd, IDC_STATIC_CHOOSE_EXISTING), false);
			EnableWindow(GetDlgItem(pWindow->hWnd, IDC_CHOOSESERVER), false);
			EnableWindow(GetDlgItem(pWindow->hWnd, IDC_REFRESH_SERVER_LIST), true);

		}

	}
	return true;
}

#if !STANDALONE
void PrepareToMonitorServer( int iServerNum)
{
	strcpy(gServerLibState.controllerHost, gAvailableShardList.ppShards[iServerNum]->pShardControllerAddress );

	gbDontCreateController = true;
}



void SpawnClientConnectToServer( int iServerNum )
{
	char *pIPString;
	char fullCommandLine[1024];

	if (gClientPID)
	{
		kill(gClientPID);
		gClientPID = 0;
	}

	pIPString = gAvailableShardList.ppShards[iServerNum]->pShardLoginServerAddress;

	
	sprintf(fullCommandLine, "./GameClient.exe -server %s -SetAccountServer %s - %s", pIPString, pIPString, gGlobalDynamicSettings.launchClientDirectlyCommandLine);
	
	gClientPID = system_detach(fullCommandLine, 1, false);
}
#endif

void PatchPCClientAndLaunch( HWND hParentWindow, int iServerNum )
{
	char *pIPString = gAvailableShardList.ppShards[iServerNum]->pShardLoginServerAddress;
	char *pPatchString = gAvailableShardList.ppShards[iServerNum]->pPatchCommandLine;
	char cwd[CRYPTIC_MAX_PATH];
	static char *spPatchDir = NULL; //ESTRING
	char *pFullCommandLine = NULL;
	char *pEscapedSnippet = NULL;
	char *pUnescapedSnippet = NULL;
	char *pPatchClientExeName = NULL;
	const char *pBuildPatchclient = NULL;

	ShardInfo_Basic *pShard = gAvailableShardList.ppShards[iServerNum];

	fileGetcwd(cwd, 1023);

	if (!pPatchString[0])
	{
		return;
	}

	estrClear(&spPatchDir);
#if STANDALONE
	estrPrintf(&spPatchDir, "%s", getExecutableName());
	estrTruncateAtLastOccurrence(&spPatchDir, '/');
#else
	estrPrintf(&spPatchDir, "c:\\%s_mcppatched", pShard->pProductName);
#endif

	mkdirtree(STACK_SPRINTF("%s\\foo.txt", spPatchDir));
	verify(chdir(spPatchDir) == 0);

	if (ShouldTryToGetLastMinuteFiles(pShard))
	{
		if (TryToGetLastMinuteFiles(pShard->iUniqueID, false))
		{
		}
		else
		{
			Errorf("Couldn't get last minute files from controller tracker");
			return;
		}
	}

	estrStackCreate(&pFullCommandLine);
	estrStackCreate(&pEscapedSnippet);
	estrStackCreate(&pUnescapedSnippet);

	assert(chdir(cwd) == 0);

#if STANDALONE
	//create patchclient.exe
	estrStackCreate(&pPatchClientExeName);
	estrPrintf(&pPatchClientExeName, "%s/patchclient.exe", spPatchDir); 
	if (!fileExists(pPatchClientExeName))
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(PATCHCLIENT_EXE), "EXE");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				FILE *pExeFile;
				void *pExeData = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

				pExeFile = fopen(pPatchClientExeName, "wb");
				if (pExeFile)
				{
					fwrite(pExeData, iExeSize, 1, pExeFile);
					fclose(pExeFile);
				}
			}
		}
	}




#else
	pBuildPatchclient = patchclientCmdLineEx(false, cwd, NULL);

	if (!pBuildPatchclient)
	{
		estrPrintf(&pFullCommandLine, "copy %s\\patchclient.exe %s", fileCrypticToolsBinDir(), spPatchDir);
	
		estrReplaceOccurrences(&pFullCommandLine, "/", "\\");
		system(pFullCommandLine);
	}
#endif

	verify(chdir(spPatchDir) == 0);


	if (pBuildPatchclient)
		estrPrintf(&pFullCommandLine, "%s ", pBuildPatchclient);
	else
		estrPrintf(&pFullCommandLine, "./patchclient.exe ");



	estrPrintf(&pUnescapedSnippet, "./GameClient.exe -server %s -PatchLogin -PatchServerAndProject {PatchServer} {Project} - %s - %s",
		pIPString, 
		gAvailableShardList.ppShards[iServerNum]->pAutoClientCommandLine ? gAvailableShardList.ppShards[iServerNum]->pAutoClientCommandLine : "", 
		gGlobalDynamicSettings.patchedPCClientCommandLine);
	
	estrTruncateAtFirstOccurrence(&pUnescapedSnippet, '\n');
	estrTruncateAtFirstOccurrence(&pUnescapedSnippet, '\r');

	estrSuperEscapeString(&pEscapedSnippet, pUnescapedSnippet);



	estrConcatf(&pFullCommandLine, " %s -executableDir \"%s\\%sClient\" -patchall -superesc executable %s",
		pPatchString, spPatchDir, pShard->pProductName, pEscapedSnippet);

	estrReplaceOccurrences_CaseInsensitive(&pFullCommandLine, STACK_SPRINTF("%sServer", pShard->pProductName), STACK_SPRINTF("%sClient", pShard->pProductName));

	system_detach(pFullCommandLine, 1, false);



	estrDestroy(&pFullCommandLine);
	estrDestroy(&pEscapedSnippet);
	estrDestroy(&pUnescapedSnippet);

	verify(chdir(cwd) == 0);

}
//remove -server and -port arguments
void FixupPatchStringForXbox(char **ppPatchString)
{
	estrRemoveCmdLineStyleArgIfPresent(ppPatchString, "-server");
	estrRemoveCmdLineStyleArgIfPresent(ppPatchString, "-port");
}

void PatchXboxClientAndLaunch( int iServerNum )
{
	char tempFolder[MAX_PATH];
	char programFilesFolder[MAX_PATH];
	char *pIPString = gAvailableShardList.ppShards[iServerNum]->pShardLoginServerAddress;
	char *pPatchString = gAvailableShardList.ppShards[iServerNum]->pPatchCommandLine;
	char *pSystemString = NULL;
	char *pEscapedSnippet = NULL;
	char *pUnescapedSnippet = NULL;

	const char *pToolsBinDirToUse;
	char *pPatchClientXboxExeName = NULL;

	char *pPatchStringToUse = NULL;

	ShardInfo_Basic *pShard = gAvailableShardList.ppShards[iServerNum];


	if (!pPatchString[0])
	{
		return;
	}

	estrStackCreate(&pSystemString);
	estrStackCreate(&pEscapedSnippet);
	estrStackCreate(&pUnescapedSnippet);

	GetEnvironmentVariable("temp", SAFESTR(tempFolder));
	GetEnvironmentVariable("ProgramFiles", SAFESTR(programFilesFolder));

#if STANDALONE
	pToolsBinDirToUse = ".";

	estrPrintf(&pPatchClientXboxExeName, "%s/patchclientxbox.exe", pToolsBinDirToUse);
	if (!fileExists(pPatchClientXboxExeName))
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(PATCHCLIENTXBOX_EXE), "EXE");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				FILE *pExeFile;
				void *pExeData = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

				pExeFile = fopen(pPatchClientXboxExeName, "wb");
				if (pExeFile)
				{
					fwrite(pExeData, iExeSize, 1, pExeFile);
					fclose(pExeFile);
				}
			}
		}
	}




#else
	pToolsBinDirToUse = fileCrypticToolsBinDir();
#endif




	estrPrintf(&pSystemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbcp\" -Y -T %s\\patchclientxbox.exe xe:\\patchclient\\patchclientxbox.exe", programFilesFolder, pToolsBinDirToUse);

	if(system(pSystemString))
	{
		MessageBox(NULL, pSystemString, "This command failed!", MB_OK);
		estrDestroy(&pSystemString);
		estrDestroy(&pEscapedSnippet);
		estrDestroy(&pUnescapedSnippet);	
		estrDestroy(&pPatchClientXboxExeName);
		return;
	}


	if (ShouldTryToGetLastMinuteFiles(pShard))
	{
		if (TryToGetLastMinuteFiles(pShard->iUniqueID, true))
		{
		}
		else
		{
			Errorf("Couldn't get last minute files from controller tracker");
			estrDestroy(&pSystemString);
			estrDestroy(&pEscapedSnippet);
			estrDestroy(&pUnescapedSnippet);
			estrDestroy(&pPatchClientXboxExeName);
			return;
		}
	}

	//add other command line stuff here
	estrPrintf(&pSystemString, "echo -server %s - %s - %s", pIPString, pShard->pAutoClientCommandLine ? pShard->pAutoClientCommandLine : "",
		gGlobalDynamicSettings.patchedXboxClientCommandLine);
	
	estrTruncateAtFirstOccurrence(&pSystemString, '\n');
	estrTruncateAtFirstOccurrence(&pSystemString, '\r');

	estrConcatf(&pSystemString,  " > \"%s\\tempCmdLine.txt\"", tempFolder);
	if(system(pSystemString))
	{
		MessageBox(NULL, pSystemString, "This command failed!", MB_OK);
		estrDestroy(&pSystemString);
		estrDestroy(&pEscapedSnippet);
		estrDestroy(&pUnescapedSnippet);	
		estrDestroy(&pPatchClientXboxExeName);
		return;
	}

	estrPrintf(&pSystemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbcp\" -Y -T %s\\tempCmdLine.txt xe:\\%s\\cmdline.txt", programFilesFolder, tempFolder, pShard->pProductName);
	if(system(pSystemString))
	{
		MessageBox(NULL, pSystemString, "This command failed!", MB_OK);
		estrDestroy(&pSystemString);
		estrDestroy(&pEscapedSnippet);
		estrDestroy(&pUnescapedSnippet);	
		estrDestroy(&pPatchClientXboxExeName);
		return;
	}

//	estrPrintf(&pUnescapedSnippet, "xe:\\%s\\GameClientXBox.exe -PatchLogin -PatchServerAndProject {PatchServer} {Project}", pShard->pProductName);
	estrPrintf(&pUnescapedSnippet, "xe:\\%s\\GameClientXBox.exe", pShard->pProductName);
	estrSuperEscapeString_shorter(&pEscapedSnippet, pUnescapedSnippet);

	estrCopy2(&pPatchStringToUse, pPatchString);
	FixupPatchStringForXbox(&pPatchStringToUse);

	estrPrintf(&pSystemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbreboot\" xe:\\patchclient\\patchclientxbox.exe -root devkit:\\%s -verbose 2 %s -superesc executable %s",
		programFilesFolder, pShard->pProductName, pPatchStringToUse, pEscapedSnippet);
	



	estrReplaceOccurrences_CaseInsensitive(&pSystemString, STACK_SPRINTF("%sServer", pShard->pProductName), STACK_SPRINTF("%sXboxClient", pShard->pProductName));

	system_detach(pSystemString, 1, false);

/*	



	estrPrintf(&pFullCommandLine, "%s\\patchclient.exe", fileCoreToolsBinDir());
	
	estrReplaceOccurrences(&pFullCommandLine, "/", "\\");

//	estrConcatf(&pFullCommandLine, " %s ",
//		pPatchString);
	estrConcatf(&pFullCommandLine, " %s -executableDir %s\\%sClient -executable \"GameClient.exe -server %s -PatchLogin -PatchServerAndProject {PatchServer} {Project}\"",
		pPatchString, patchDir, GetProductName(), pIPString);

	estrReplaceOccurrences_CaseInsensitive(&pFullCommandLine, STACK_SPRINTF("%sServer", GetProductName()), STACK_SPRINTF("%sClient", GetProductName()));

	system_detach(pFullCommandLine, 1, false);


*/
	estrDestroy(&pSystemString);
	estrDestroy(&pEscapedSnippet);
	estrDestroy(&pUnescapedSnippet);
	estrDestroy(&pPatchClientXboxExeName);
	estrDestroy(&pPatchStringToUse);


}


BOOL MCPStartDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			char tempString[1024];

			sprintf(tempString, "%s", fileExecutableDir());
			SetWindowText(hDlg, tempString);
			
			SetWindowText(GetDlgItem(hDlg, IDC_PATCHPC_COMMANDLINE), 
				IsCommandLineNonEmpty(gGlobalDynamicSettings.patchedPCClientCommandLine) ? "!!!" : "...");

			SetWindowText(GetDlgItem(hDlg, IDC_PATCHXBOX_COMMANDLINE), 
				IsCommandLineNonEmpty(gGlobalDynamicSettings.patchedXboxClientCommandLine) ? "!!!" : "...");

			
			return TRUE; 

		}
		break;

	case WM_COMMAND:
		if (gbCommandLinesMightHaveChanged)
		{
			gbCommandLinesMightHaveChanged = false;

		
			SetWindowText(GetDlgItem(hDlg, IDC_PATCHPC_COMMANDLINE), 
				IsCommandLineNonEmpty(gGlobalDynamicSettings.patchedPCClientCommandLine) ? "!!!" : "...");

			SetWindowText(GetDlgItem(hDlg, IDC_PATCHXBOX_COMMANDLINE), 
				IsCommandLineNonEmpty(gGlobalDynamicSettings.patchedXboxClientCommandLine) ? "!!!" : "...");

		}


		switch (LOWORD (wParam))
		{
#if !STANDALONE
		case IDC_LAUNCHCLIENT:
#endif
		case IDC_PATCHXBOX:
		case IDC_PATCHPC:
			{
				LRESULT lResult;

				lResult = SendMessage(GetDlgItem(hDlg, IDC_CHOOSESERVER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					lResult = SendMessage(GetDlgItem(hDlg, IDC_CHOOSESERVER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						if (lResult < eaSize(&gAvailableShardList.ppShards))
						{
							switch (LOWORD (wParam))
							{
#if !STANDALONE
							xcase IDC_LAUNCHCLIENT:
								SpawnClientConnectToServer( lResult);
#endif
							xcase IDC_PATCHXBOX:
								PatchXboxClientAndLaunch( lResult);
							xcase IDC_PATCHPC:
								PatchPCClientAndLaunch( hDlg, lResult);
								;
							}
						}
					}
				}
			}
		break;

		case IDC_PATCHPC_COMMANDLINE:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, FAKE_GLOBALTYPE_PATCHED_CLIENT, IDD_EDITCOMMANDLINE, false,
				 EditCommandLineDlgFunc,  NULL, NULL);
			break;
		case IDC_PATCHXBOX_COMMANDLINE:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, FAKE_GLOBALTYPE_PATCHED_XBOX_CLIENT, IDD_EDITCOMMANDLINE, false,
				 EditCommandLineDlgFunc,  NULL, NULL);
			break;
#if !STANDALONE
		case IDC_LAUNCHCLIENT_COMMANDLINE:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_COMMANDLINE, FAKE_GLOBALTYPE_LAUNCH_CLIENT, IDD_EDITCOMMANDLINE, false,
				 EditCommandLineDlgFunc,  NULL, NULL);
			break;
		case IDC_MONITOR_LOCALLY:
			{
				LRESULT lResult;

				lResult = SendMessage(GetDlgItem(hDlg, IDC_CHOOSESERVER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					lResult = SendMessage(GetDlgItem(hDlg, IDC_CHOOSESERVER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						if (lResult < eaSize(&gAvailableShardList.ppShards))
						{
							PrepareToMonitorServer( lResult);
							//will connect to but not create a controller, because gbDontCreateController will
							//be set
							CreateAndConnectToController(NULL);
							StartMainWindow();
							pWindow->bCloseRequested = true;


							if (gClientPID)
							{
								kill(gClientPID);
								gClientPID = 0;
							}				



						}
					}
				}
			}
			break;

		case IDC_MONITOR_EXISTING_MCP:
			{
				LRESULT lResult;

				lResult = SendMessage(GetDlgItem(hDlg, IDC_CHOOSESERVER), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				if (lResult != CB_ERR) 
				{
					lResult = SendMessage(GetDlgItem(hDlg, IDC_CHOOSESERVER), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
					if (lResult != CB_ERR) 
					{
						if (lResult < eaSize(&gAvailableShardList.ppShards))
						{
							char URL[1024];
							sprintf(URL, "http://%s/viewxpath", gAvailableShardList.ppShards[lResult]->pShardControllerAddress);
							openURL(URL);
						}
					}
				}
			}
			break;

		case IDC_RUNLOCAL:
			gbRunPublic = false;
			CreateAndConnectToController(GetStartingScriptFileName());
			StartMainWindow();
			pWindow->bCloseRequested = true;
			if (gClientPID)
			{
				kill(gClientPID);
				gClientPID = 0;
			}
			break;

		case IDC_RUNPUBLICLOCAL:
			gbRunPublic = true;		
			CreateAndConnectToController(GetStartingScriptFileName());
			StartMainWindow();
			pWindow->bCloseRequested = true;
			if (gClientPID)
			{
				kill(gClientPID);
				gClientPID = 0;
			}		
			break;

		case IDC_UTILITIES:
			SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_UTILITIES, 0, IDD_UTILITIES, false,
				utilitiesMenuDlgProc,  NULL, NULL);
			break;
#endif

		case IDC_REFRESH_SERVER_LIST:
			sbRefreshBegan = 1;
			AccountFSM_Reset();
			AccountFSM_BeginContact();
			break;

		case IDCANCEL:
			pWindow->bCloseRequested = true;
		break;

		case IDC_CHOOSESERVER:
			gbAvailableShardsChanged = true;
			break;



		}

	}
	
	return FALSE;
}
