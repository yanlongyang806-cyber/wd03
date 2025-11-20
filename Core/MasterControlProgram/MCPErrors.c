#include "simpleWindowManager.h"
#include "MCPErrors.h"
#include "ListView.h"
#include "resource_MasterControlProgram.h"
#include "MCPErrors_c_ast.h"
#include "winUtil.h"
#include "stashTable.h"
#include "estring.h"
#include "globalTypes.h"
#include "sysUtil.h"
#include "masterControlProgram.h"
#include "genericDialog.h"
#include "gimmeDllWrapper.h"
#include "file.h"
#include "stringutil.h"

bool gbNewErrorsInErrorScreen = false;

static ListView *pMCPErrorLV = NULL;

static MultiControlScaleManager *pErrorScreenMCSM = NULL;

/*
File: Defs/Powers/Items/Consumables/Alchemical/Item_Alchemical_Bloodstingerpoison.Powers
Last Author/Status:jschomer
Items/Consumables/Alchemical.Item_Alchemical_Bloodstingerpoison: 0: GrantPower requires a valid PowerDef (currently Boon_Deadlypoison_Apply) (reported by GameServer)
*/

static int siNextErrorIDX = 1;

AUTO_STRUCT;
typedef struct MCPError
{
	int iIdx;
	int iCount;
	char *pBlamee; AST(ESTRING)
	char *pStr; AST(ESTRING)
	char *pFileName; AST(ESTRING)
	bool bMine; NO_AST
	char *pReportedBy; AST(ESTRING)
} MCPError;

//keep them in both a stashtable for easy lookup by string, and an earray so the order is maintained
static StashTable sMCPErrorsByString = NULL;
static MCPError **sppMCPErrors = NULL;
static bool sbAtLeastOneOfMine = false;

extern char *g_lastAuthorIntro;


//returns true if it gets "gameserver" or "gameclient 7"
bool IsContainerTypeWithOrWithoutID(const char *pInStr, GlobalType *pOutGlobalType)
{
	char **ppWords = NULL;
	int iCount;
	bool bRetVal = false;
	GlobalType eType;

	DivideString(pInStr, " ", &ppWords, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	iCount = eaSize(&ppWords);

	if (iCount > 0)
	{
		if ((eType = NameToGlobalType(ppWords[0])) != GLOBALTYPE_NONE)
		{
			switch (iCount)
			{
			xcase 1:
				*pOutGlobalType = eType;
				bRetVal = true;

			xcase 2:
				if (atoi(ppWords[1]))
				{
					*pOutGlobalType = eType;
					bRetVal = true;
				}
			}
		}
	}

	eaDestroyEx(&ppWords, NULL);
	return bRetVal;
}


#define REPORTED_BY_PREFIX "(reported by "
#define PREFIX_LEN 13
MCPError *ParseErrorFromRawErrorString(const char *pInString)
{
	char *pCopy = NULL;
	char *pReportedBy;
	char *pTemp;
	char **ppLines = NULL;
	int iLineNum;
	char *pCurLine;
	GlobalType eType;

	MCPError *pRetVal = StructCreate(parse_MCPError);
	
	estrCopy2(&pCopy, pInString);

	estrTrimLeadingAndTrailingWhitespace(&pCopy);

	//first, trim off "(reported by Gameserver) if it's there"
	pReportedBy = strstri(pCopy, REPORTED_BY_PREFIX);
	if (pReportedBy)
	{
		pTemp = strchr(pReportedBy, ')');
		if (pTemp && pTemp - pCopy == estrLength(&pCopy) - 1)
		{
			char *pWord = NULL;
			estrSetSize(&pWord, pTemp - pReportedBy - PREFIX_LEN);
			memcpy(pWord, pReportedBy + PREFIX_LEN, pTemp - pReportedBy - PREFIX_LEN);
			if (IsContainerTypeWithOrWithoutID(pWord, &eType))
			{
				estrRemove(&pCopy, pReportedBy - pCopy, pTemp - pReportedBy + 1);
				estrTrimLeadingAndTrailingWhitespace(&pCopy);

				estrPrintf(&pRetVal->pReportedBy, "%s", GlobalTypeToName(eType));

			}

			estrDestroy(&pWord);
		}
	}

	DivideString(pCopy, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	for (iLineNum = eaSize(&ppLines)-1; iLineNum >= 0; iLineNum--)
	{
		pCurLine = ppLines[iLineNum];

		if (strStartsWith(pCurLine, "File: "))
		{
			estrCopy2(&pRetVal->pFileName, pCurLine + 6);
			free(pCurLine);
			eaRemove(&ppLines, iLineNum);
		}
		else if (strStartsWith(pCurLine, g_lastAuthorIntro))
		{
			estrCopy2(&pRetVal->pBlamee, pCurLine + strlen(g_lastAuthorIntro));
			free(pCurLine);
			eaRemove(&ppLines, iLineNum);
		}
	}

	for (iLineNum = 0; iLineNum < eaSize(&ppLines); iLineNum++)
	{
		estrConcatf(&pRetVal->pStr, "%s\r\n ", ppLines[iLineNum]);
	}

	estrDestroy(&pCopy);
	eaDestroyEx(&ppLines, NULL);

	return pRetVal;
}

void MergeReportedBys(MCPError *pPreExisting, MCPError *pNew)
{
	if (!pPreExisting->pReportedBy)
	{
		estrCopy(&pPreExisting->pReportedBy, &pNew->pReportedBy);
		return;
	}

	if (!pNew->pReportedBy)
	{
		return;
	}

	if (strstri(pPreExisting->pReportedBy, pNew->pReportedBy))
	{
		return;
	}

	estrConcatf(&pPreExisting->pReportedBy, ", %s", pNew->pReportedBy);
}

void MergeFileNames(MCPError *pPreExisting, MCPError *pNew)
{
	if (!pPreExisting->pFileName)
	{
		estrCopy(&pPreExisting->pFileName, &pNew->pFileName);
		return;
	}

	if (!pNew->pFileName)
	{
		return;
	}

	if (strstri(pPreExisting->pFileName, pNew->pFileName))
	{
		return;
	}

	estrConcatf(&pPreExisting->pFileName, ", %s", pNew->pFileName);
}


void ReportMCPError(const char *pRawString)
{
	MCPError *pError = ParseErrorFromRawErrorString(pRawString);
	MCPError *pPreExisting = NULL;
	SimpleWindow *pErrorWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_ERRORS);
	SimpleWindow *pMainWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_MAIN);

	assert(pError);

	if (!sMCPErrorsByString)
	{
		sMCPErrorsByString = stashTableCreateWithStringKeys( 1000, StashDefault );
	}


	if (stashFindPointer(sMCPErrorsByString, pError->pStr, &pPreExisting))
	{
		pPreExisting->iCount++;
		MergeReportedBys(pPreExisting, pError);
		if (pMCPErrorLV)
		{
			listViewItemChanged(pMCPErrorLV, pPreExisting);
		}
		return;
	}

	gbNewErrorsInErrorScreen = true;

	if (!pErrorWindow)
	{

		SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_ERRORS, 0, IDD_ERRORS, false,
			errorsDlgProc,  NULL, NULL);
		pErrorWindow = SimpleWindowManager_FindWindowByType(MCPWINDOW_ERRORS);
	}


	if (pErrorWindow)
	{
		flashWindow(pErrorWindow->hWnd);
	}
	else if (pMainWindow)
	{
		flashWindow(pMainWindow->hWnd);
	}

	if (!pErrorWindow)
	{


		gbNewErrorsInErrorScreen = true;
		if (pMainWindow)
		{
			ShowWindow(GetDlgItem(pMainWindow->hWnd, IDC_ERROREXP1), SW_SHOW);
			ShowWindow(GetDlgItem(pMainWindow->hWnd, IDC_ERROREXP2), SW_SHOW);
		}	
	}



	if (!sMCPErrorsByString)
	{
		sMCPErrorsByString = stashTableCreateWithStringKeys( 1000, StashDefault );
	}


	pError->iIdx = siNextErrorIDX++;
	pError->iCount = 1;
	stashAddPointer(sMCPErrorsByString, pError->pStr, pError, false);

	if (pError->pFileName)
	{
		pError->bMine = gimmeDLLQueryIsFileMine(pError->pFileName);
		if (pError->bMine && !sbAtLeastOneOfMine)
		{
			sbAtLeastOneOfMine = true;

			if (pErrorWindow)
			{
				InvalidateRect(pErrorWindow->hWnd, NULL, FALSE);
			}
		}
	}

	if (!eaSize(&sppMCPErrors))
	{
		if (pErrorWindow)
		{
			InvalidateRect(pErrorWindow->hWnd, NULL, FALSE);
		}
	}

	eaPush(&sppMCPErrors, pError);

	if (pMCPErrorLV)
	{
		listViewAddItem(pMCPErrorLV, pError);
		if (pError->bMine)
		{
			listViewSetItemColor(pMCPErrorLV, pError, RGB(220, 50, 50), RGB(0,0,0));
		}
	}


	

}

void CopyErrorsToClipBoard(void)
{
	char *pFullString = NULL;
	int i;

	for (i=0; i < eaSize(&sppMCPErrors); i++)
	{
		MCPError *pError = sppMCPErrors[i];
		estrConcatf(&pFullString, "%s%s\t%s\t%s",
			estrLength(&pFullString) == 0 ? "" : "\n", 
			pError->pBlamee, pError->pStr, pError->pFileName);
	}

	estrFixupTabsIntoSpacesAcrossMultipleLines(&pFullString, 4);

	winCopyToClipboard(pFullString ? pFullString : "");
	estrDestroy(&pFullString);
}

void ResetMCPErrors(void)
{

	siNextErrorIDX = 1;

	if (pMCPErrorLV)
	{
		listViewDelAllItems(pMCPErrorLV, NULL);
		stashTableDestroyStruct(sMCPErrorsByString, NULL, parse_MCPError);
		eaDestroy(&sppMCPErrors);
		sMCPErrorsByString = NULL;
		sbAtLeastOneOfMine = false;

	}
}

//example code from UIModalDialog.c on how to open file/open folder
/*
		xcase UIEditFile:
		{
			// Extract file name from message, if found, edit it w/ windows assigned editor
			if (dialog_message && strlen(dialog_message) > 8) {
				char filename[MAX_PATH];
				char *s;
				strncpy(filename, dialog_message+strlen("File: "), ARRAY_SIZE(filename)-1);
				s = strchr(filename, '\n');
				if (s) {
					*s = '\0';
					fileLocateWrite(filename, filename);
					fileOpenWithEditor(filename);
				}
			}
			return_button = -1;
		}
		xcase UIOpenFolder:
		{
			// Extract file name from message, if found, edit it w/ windows assigned editor
			if (dialog_message && strlen(dialog_message) > 8) {
				char filename[MAX_PATH];
				char *s;
				strncpy(filename, dialog_message+strlen("File: "), ARRAY_SIZE(filename)-1);
				s = strchr(filename, '\n');
				if (s) {
					char cmd[1024];
					*s = '\0';
					fileLocateWrite(filename, filename);
					backSlashes(filename);
					sprintf(cmd, "explorer.exe /select,\"%s\"", filename);
					system_detach(cmd, 0, 0);
				}
			}
			return_button = -1;
		}*/



static char **sppAllFilesFromCB = NULL;
void OpenFileCB(ListView *lv, MCPError *pError, void *data)
{
	char fixedUpFile[CRYPTIC_MAX_PATH];
	if (pError->pFileName && pError->pFileName[0])
	{
		fileLocateWrite(pError->pFileName, fixedUpFile);
		if (eaFindString(&sppAllFilesFromCB, fixedUpFile) == -1)
		{
			eaPush(&sppAllFilesFromCB, strdup(fixedUpFile));
		}
	}
}

void OpenFolderCB(ListView *lv, MCPError *pError, void *data)
{

	char fixedUpFile[CRYPTIC_MAX_PATH];
	if (pError->pFileName && pError->pFileName[0])
	{
		fileLocateWrite(pError->pFileName, fixedUpFile);
		backSlashes(fixedUpFile);
		if (eaFindString(&sppAllFilesFromCB, fixedUpFile) == -1)
		{
			eaPush(&sppAllFilesFromCB, strdup(fixedUpFile));
		}
	}
}

static char *spAllSelectedString = NULL;
void CopySelectedCB(ListView *lv, MCPError *pError, void *data)
{
	estrConcatf(&spAllSelectedString, "%s%s\t%s\t%s",
		estrLength(&spAllSelectedString) == 0 ? "" : "\n", 
		pError->pBlamee, pError->pStr, pError->pFileName);
}

MCPError *spCurSelectedError = NULL;

void FindCurSelectedCB(ListView *lv, MCPError *pError, void *data)
{
	spCurSelectedError = pError;
}

int SortErrorsByBlameeCB(MCPError *pError1, MCPError *pError2)
{
	if (pError1->bMine && !pError2->bMine)
	{
		return -1;
	}

	if (pError2->bMine && !pError1->bMine)
	{
		return 1;
	}

	return stricmp_safe(pError1->pBlamee, pError2->pBlamee);
}

BOOL errorsDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	static HBITMAP sBlameBanner = 0, sNoBlameBanner = 0, sNoErrorsBanner=0;


	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			wchar_t wideBuf[2048];

			RECT child;

			UTF8ToWideStrConvert("EDIT",wideBuf,ARRAY_SIZE(wideBuf));	

			GetWindowRect(GetDlgItem(hDlg, IDC_SELECTED_TEXT), &child);
			MapWindowPoints(NULL, hDlg, (LPPOINT)&child, 2);

			DestroyWindow(GetDlgItem(hDlg, IDC_SELECTED_TEXT));
			CreateWindowW(wideBuf, NULL, WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
				child.left, child.top, child.right - child.left, child.bottom - child.top,
				hDlg,
				(HMENU)IDC_SELECTED_TEXT, ghInstance, 0);

			ShowWindow(GetDlgItem(hDlg, IDC_SELECTED_TEXT), SW_SHOW);


			pMCPErrorLV = listViewCreate();
			listViewInit(pMCPErrorLV, parse_MCPError, hDlg, GetDlgItem(hDlg, IDC_ERROR_LIST));

			listViewSetColumnCompareFunc(pMCPErrorLV, 2, SortErrorsByBlameeCB);

			SetTimer(hDlg, 0, 1, NULL);

		

			listViewSetColumnWidth(pMCPErrorLV, 0, 20);
			listViewSetColumnWidth(pMCPErrorLV, 1, 20);
			listViewSetColumnWidth(pMCPErrorLV, 2, 75);
			listViewSetColumnWidth(pMCPErrorLV, 3, 750);
			listViewSetColumnWidth(pMCPErrorLV, 4, 500);

			if (sMCPErrorsByString)
			{
				int i;
				for (i=0; i < eaSize(&sppMCPErrors); i++)
				{
					listViewAddItem(pMCPErrorLV, sppMCPErrors[i]);
					if (sppMCPErrors[i]->bMine)
					{
						listViewSetItemColor(pMCPErrorLV, sppMCPErrors[i], RGB(220, 50, 50), RGB(0,0,0));
					}
				}
			}

			if (!pErrorScreenMCSM)
			{

				pErrorScreenMCSM = BeginMultiControlScaling(hDlg);

				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_ERROR_LIST, SCALE_LOCK_BOTH, SCALE_LOCK_BOTH);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDOK, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_RESET_ERRORS, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_COPY_TO_CB, SCALE_LOCK_RIGHT, SCALE_LOCK_TOP);

				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_STATIC_BLAMEE, SCALE_LOCK_LEFT, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_SELECTED_BLAMEE, SCALE_LOCK_LEFT, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_STATIC_FILE, SCALE_LOCK_LEFT, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_SELECTED_FILENAME, SCALE_LOCK_BOTH, SCALE_LOCK_BOTTOM);
				MultiControlScaling_AddChild(pErrorScreenMCSM, IDC_SELECTED_TEXT, SCALE_LOCK_BOTH, SCALE_LOCK_BOTTOM);


			}
			else
			{
				ReInitMultiControlScaling(pErrorScreenMCSM, hDlg);
			}

			MultiControlScaling_Update(pErrorScreenMCSM);


			if (!sBlameBanner)
			{
				int iBmpSize;

				char *pBuffer = fileAllocWithRetries("server/MCP/ERRORS.bmp", &iBmpSize, 5);

				FILE *pOutFile;
				char *pTempFileName = NULL;
				
				assertmsg(pBuffer, "Couldn't load ERRORS.bmp");


				pOutFile = GetTempBMPFile(&pTempFileName);

				fwrite(pBuffer, iBmpSize, 1, pOutFile);
				fclose(pOutFile);
				free(pBuffer);
		
				sNoBlameBanner = LoadImage(0, pTempFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

				assertmsgf(sNoBlameBanner, "Couldn't load %s", pTempFileName);
				DeleteFile(pTempFileName);

				
				pBuffer = fileAllocWithRetries("server/MCP/ERRORS_YOU.bmp", &iBmpSize, 5);

				assertmsg(pBuffer, "Couldn't load ERRORS_YOU.bmp");

				pOutFile = GetTempBMPFile(&pTempFileName);

				fwrite(pBuffer, iBmpSize, 1, pOutFile);
				fclose(pOutFile);
				free(pBuffer);

				sBlameBanner = LoadImage(0, pTempFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

				assertmsgf(sBlameBanner, "Couldn't load %s", pTempFileName);

				DeleteFile(pTempFileName);
				estrDestroy(&pTempFileName);
				
				
				pBuffer = fileAllocWithRetries("server/MCP/NO_ERRORS.bmp", &iBmpSize, 5);

				assertmsg(pBuffer, "Couldn't load NO_ERRORS.bmp");

				pOutFile = GetTempBMPFile(&pTempFileName);

				fwrite(pBuffer, iBmpSize, 1, pOutFile);
				fclose(pOutFile);
				free(pBuffer);

				sNoErrorsBanner = LoadImage(0, pTempFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

				assertmsgf(sNoErrorsBanner, "Couldn't load %s", pTempFileName);

				DeleteFile(pTempFileName);
				estrDestroy(&pTempFileName);

			}



		}
		break;

			case WM_TIMER:
			{
				static char *spLastShown = NULL;

				if (pMCPErrorLV)
				{
					spCurSelectedError = NULL;
					listViewDoOnSelected(pMCPErrorLV, FindCurSelectedCB, NULL);
					if (spCurSelectedError)
					{
						SetTextFast(GetDlgItem(hDlg, IDC_SELECTED_BLAMEE), spCurSelectedError->pBlamee);
						SetTextFast(GetDlgItem(hDlg, IDC_SELECTED_FILENAME), spCurSelectedError->pFileName);
	
						if (stricmp_safe(spCurSelectedError->pStr, spLastShown) != 0)
						{
							wchar_t wideBuf[2048];
						
							UTF8ToWideStrConvert(spCurSelectedError->pStr,wideBuf,ARRAY_SIZE(wideBuf));	
							SetWindowTextW(GetDlgItem(hDlg, IDC_SELECTED_TEXT), wideBuf);
							estrCopy2(&spLastShown, spCurSelectedError->pStr ? spCurSelectedError->pStr : "");
						}
					}
					else
					{
						estrDestroy(&spLastShown);
						SetTextFast(GetDlgItem(hDlg, IDC_SELECTED_BLAMEE), NULL);
						SetTextFast(GetDlgItem(hDlg, IDC_SELECTED_FILENAME), NULL);
						SetTextFast(GetDlgItem(hDlg, IDC_SELECTED_TEXT), NULL);
					}
				}
			}
			break;



		
		case WM_DRAWITEM: 
			{
				LPDRAWITEMSTRUCT lpdis;
				HDC hdcMem;

				lpdis = (LPDRAWITEMSTRUCT) lParam; 
				hdcMem = CreateCompatibleDC(lpdis->hDC); 
	 
				// it would have errored out above
				ANALYSIS_ASSUME(sNoBlameBanner);
				ANALYSIS_ASSUME(sBlameBanner);
				ANALYSIS_ASSUME(sNoErrorsBanner);
				SelectObject(hdcMem, eaSize(&sppMCPErrors) == 0 ? sNoErrorsBanner : (sbAtLeastOneOfMine ? sBlameBanner : sNoBlameBanner)); 

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
					945,        // source bitmap width PLUS ONE (not sure why)
					86,        // source bitmap height PLUS ONE (not sure why)
					SRCCOPY);  // raster operation 
	 
				DeleteDC(hdcMem); 
			}
            return TRUE; 


	case WM_SIZE:
		MultiControlScaling_Update(pErrorScreenMCSM);
		break;

	case WM_NOTIFY:
		//this can happen after someone clicks OK, at which point pMCPErrorLV will be NULL
		if (pMCPErrorLV)
		{
			return listViewOnNotify(pMCPErrorLV, wParam, lParam, NULL);
		}
		else
		{
			return FALSE;
		}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{

		case IDC_RESET_ERRORS:
				ResetMCPErrors();
			break;

		case IDC_COPY_TO_CB:
			CopyErrorsToClipBoard();
			break;
		
		case IDC_COPYSELECTED:
			if (pMCPErrorLV)
			{
				estrClear(&spAllSelectedString);
				listViewDoOnSelected(pMCPErrorLV, CopySelectedCB, NULL);
				if (estrLength(&spAllSelectedString))
				{
					estrFixupTabsIntoSpacesAcrossMultipleLines(&spAllSelectedString, 4);

					winCopyToClipboard(spAllSelectedString);
				}
			}
			break;
		case IDC_OPENFILE:
			if (pMCPErrorLV)
			{
				int i;
				eaDestroyEx(&sppAllFilesFromCB, NULL);
				listViewDoOnSelected(pMCPErrorLV, OpenFileCB, NULL);

				for (i=0; i < eaSize(&sppAllFilesFromCB); i++)
				{
					fileOpenWithEditor(sppAllFilesFromCB[i]);
				}
			}
			break;
		case IDC_OPENFOLDER:
			if (pMCPErrorLV)
			{
				int i;
				char *pSystemString = NULL;
				eaDestroy(&sppAllFilesFromCB);
				listViewDoOnSelected(pMCPErrorLV, OpenFolderCB, NULL);

				for (i=0; i < eaSize(&sppAllFilesFromCB); i++)
				{
					estrPrintf(&pSystemString, "explorer.exe /select,\"%s\"", sppAllFilesFromCB[i]);
					system_detach(pSystemString, 0, 0);
				}

				estrDestroy(&pSystemString);
			}
			break;

	
		case IDCANCEL:
		case IDOK:
			pWindow->bCloseRequested = true;
			if (pMCPErrorLV)
			{
				listViewDestroy(pMCPErrorLV);
				pMCPErrorLV = NULL;
			}

			return FALSE;

		}

	}
	
	return FALSE;
}


#include "MCPErrors_c_ast.c"
