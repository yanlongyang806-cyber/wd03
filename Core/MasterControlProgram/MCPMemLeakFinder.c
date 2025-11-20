
#include "mastercontrolprogram.h"
#include "svnutils.h"
#include "Estring.h"
#include "winutil.h"
#include "timing.h"
#include "utils.h"
#include "stashTable.h"
#include "file.h"
#include "StringUtil.h"
#include "memLeakTracking.h"
#include "Textparser.h"
#include "MCPMemLeakFinder_c_ast.h"
#include "UtilitiesLib.h"

AUTO_STRUCT;
typedef struct MemLeakCategoryList
{
	MemLeakCategory **ppCategories; AST(NAME(Category))
} MemLeakCategoryList;

void LoadMemLeakCategories(MemLeakCategory ***pppCategories)
{
	MemLeakCategoryList *pList = StructCreate(parse_MemLeakCategoryList);
	HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(MEMLEAKCATEGORIES_TXT), "TXT");
	int iCategoryNum;

	if (rsrc)
	{
		HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
		if (gptr)
		{
			void *pTxtFile = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
			size_t iExeSize = SizeofResource(GetModuleHandle(NULL), rsrc);

	
			ParserReadTextForFile(pTxtFile, "src\\data\\MemLeakCategories.txt", parse_MemLeakCategoryList, pList, 0);
		}
	}

	ParserLoadFiles(GetDirForBaseConfigFiles(), "_MemLeakCategories.txt", NULL, PARSER_OPTIONALFLAG, parse_MemLeakCategoryList, pList);

	for (iCategoryNum = eaSize(&pList->ppCategories) - 1; iCategoryNum > 0; iCategoryNum--)
	{
		MemLeakCategory *pFirstCategory = pList->ppCategories[iCategoryNum];
		int iSecondCategoryNum;
		assert(pFirstCategory);

		for (iSecondCategoryNum = iCategoryNum - 1; iSecondCategoryNum >= 0; iSecondCategoryNum--)
		{
			MemLeakCategory *pSecondCategory = pList->ppCategories[iSecondCategoryNum];
			assert(pSecondCategory);

			if (stricmp(pFirstCategory->pCategoryName__, pSecondCategory->pCategoryName__) == 0)
			{
				int i;

				for (i=0; i < eaSize(&pFirstCategory->ppMemberNames__); i++)
				{
					if (eaFindString(&pSecondCategory->ppMemberNames__, pFirstCategory->ppMemberNames__[i]) == -1)
					{
						eaPush(&pSecondCategory->ppMemberNames__, pFirstCategory->ppMemberNames__[i]);
						pFirstCategory->ppMemberNames__[i] = NULL;
					}
				}

				StructDestroy(parse_MemLeakCategory, pFirstCategory);
				eaRemoveFast(&pList->ppCategories, iCategoryNum);
				break;
			}
		}
	}

	*pppCategories = pList->ppCategories;
	pList->ppCategories = NULL;
}



BOOL memLeakFinderMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			SendMessage(    // returns LRESULT in lResult
			   GetDlgItem(hDlg, IDC_MEMLEAKIN),           // (HWND) handle to destination control
			   EM_LIMITTEXT,          // (UINT) message ID
			   0x7FFFFFFE,                // = () wParam; 
			   0);



	/*		char *pURL = NULL;
			int iCurRev;
				
			estrStackCreate(&pURL);

			iCurRev = SVN_GetRevNumOfFolders("c:\\src",NULL, &pURL, 5);

			if (!iCurRev)
			{
				iCurRev = SVN_GetRevNumOfFolders("svn://code/dev", NULL, &pURL, 5);
			}

			SetTextFast(GetDlgItem(hDlg, IDC_URL), pURL ? pURL : "!ERROR!");
	
			if (!iCurRev)
			{
				SetTextFast(GetDlgItem(hDlg, IDC_FROM), "!ERROR!");
				SetTextFast(GetDlgItem(hDlg, IDC_TO), "!ERROR!");
			}
			else
			{
				SetTextFast(GetDlgItem(hDlg, IDC_FROM), STACK_SPRINTF("%d", iCurRev - 20));
				SetTextFast(GetDlgItem(hDlg, IDC_TO), STACK_SPRINTF("%d", iCurRev));
			}

			estrDestroy(&pURL);*/
		}
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_MEMLEAK_CLEAR:
			SetWindowText(GetDlgItem(hDlg, IDC_MEMLEAKIN), "");
			break;

		case IDOK:
			{
				int iInLen;
				char *pInString;
				char *pOutString = NULL;
				MemLeakCategory **ppCategories = NULL;

				//always reload categories in case they're changing on disk
				eaDestroyStruct(&ppCategories, parse_MemLeakCategory);

				LoadMemLeakCategories(&ppCategories);

				iInLen = GetWindowTextLength(GetDlgItem(hDlg, IDC_MEMLEAKIN));
				pInString = malloc(iInLen + 1);
				GetWindowText(GetDlgItem(hDlg, IDC_MEMLEAKIN), pInString, iInLen + 1);
			
				FindMemLeaksFromStringWithMultipleMMDS(pInString, &pOutString, NULL, &ppCategories, 0);

				estrFixupNewLinesForWindows(&pOutString);

				SetTextFast(GetDlgItem(hDlg, IDC_MEMLEAKOUT), pOutString);

				free(pInString);
				estrDestroy(&pOutString);

		/*		char fromString[128];
				char toString[128];
				char URLString[128];
				int iFrom, iTo;
				char *pOutString = NULL;


				bool bPatchNotes = IsDlgButtonChecked(hDlg, IDC_PATCHNOTES);
				bool bShowBlanks = IsDlgButtonChecked(hDlg, IDC_SHOWBLANKS);
				bool bOutputCSV = IsDlgButtonChecked(hDlg, IDC_CSV);

				estrStackCreate(&pOutString);

				GetWindowText(GetDlgItem(hDlg, IDC_FROM), SAFESTR(fromString));
				GetWindowText(GetDlgItem(hDlg, IDC_TO), SAFESTR(toString));
				GetWindowText(GetDlgItem(hDlg, IDC_URL), SAFESTR(URLString));

				iFrom = atoi(fromString);
				iTo = atoi(toString);


				if (iFrom <= 0)
				{
					estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be a positive int", fromString);
				}

				if (iTo <= 0)
				{
					estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be a positive int", toString);
				}

				if (iFrom > iTo)
				{
					int temp = iFrom;
					iFrom = iTo;
					iTo = temp;
				}


				if (iFrom > 0 && iTo > 0)
				{
					CheckinInfo **ppCheckins = NULL;
					bool bResult;

					
					bResult =  SVN_GetCheckins(iFrom, iTo, NULL, NULL, URLString, &ppCheckins, 120, 0);

					if (!bResult)
					{
						estrPrintf(&pOutString, "ERROR: SVN_GetCheckins failed");
					}
					else
					{
						if (eaSize(&ppCheckins) == 0)
						{
							estrPrintf(&pOutString, "(No checkins)");
						}
						else
						{
							int i;

							for (i=0; i < eaSize(&ppCheckins); i++)
							{
								if ((!bPatchNotes || strstri_safe(ppCheckins[i]->checkinComment, "PATCHNOTES:"))
									&& (bShowBlanks || strlen(ppCheckins[i]->checkinComment)>0))
								{
									if(bOutputCSV)
									{
										static char *estr;
										estrCopy2(&estr, ppCheckins[i]->checkinComment);
										estrReplaceOccurrences(&estr, "\"", "\"\"");
										estrTrimLeadingAndTrailingWhitespace(&estr);

										estrConcatf(&pOutString, "%d,%s,%s,\"%s\"\r\n",
											ppCheckins[i]->iRevNum,
											ppCheckins[i]->userName,
											timeGetDateStringFromSecondsSince2000(ppCheckins[i]->iCheckinTimeSS2000),
											estr);
									}
									else
									{
										estrConcatf(&pOutString, "%s\t%d\t%s\n",
											ppCheckins[i]->userName, ppCheckins[i]->iRevNum, ppCheckins[i]->checkinComment);
									}
								}
							}
						}
					}

					eaDestroyStruct(&ppCheckins, parse_CheckinInfo);
				}

				SetTextFast(GetDlgItem(hDlg, IDC_OUTPUT), pOutString);

				estrDestroy(&pOutString);*/
			}
			break;

				


		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		}

	}
	
	return FALSE;
}

#include "MCPMemLeakFinder_c_ast.c"
