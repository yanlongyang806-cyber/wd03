
#include "mastercontrolprogram.h"
#include "gimmeutils.h"
#include "timing.h"
#include "Estring.h"
#include "Earray.h"
#include "Svnutils.h"
#include "GlobalTypes.h"
#include "stringUtil.h"
#include "utils.h"
#include "winutil.h"



static WNDPROC orig_OutputProc = NULL;


// Superclass the checkin window edit box
static LRESULT CALLBACK MakeCtrlAWorkProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch( iMsg )
	{

	// Ctrl-A key combinations
	case WM_KEYDOWN:
		if (GetAsyncKeyState(VK_CONTROL))
		{
			switch (wParam)
			{
				case 'A':
					SendMessage(hWnd, EM_SETSEL, 0, -1);
					return true;
			}
		}
	}


	return CallWindowProc(orig_OutputProc, hWnd, iMsg, wParam, lParam)	;
}

BOOL gimmeCheckinsMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	char tempStr[16];

	switch (iMsg)
	{
	case WM_KEYDOWN:
		printf("key pressed\n");
		break;

	case WM_INITDIALOG:
		{
			char gimmeFolder[CRYPTIC_MAX_PATH];

			U32 iCurTime = timeSecondsSince2000_ForceRecalc();
			int iCurBranch;
			int iCurCoreBranch;
			HWND hOutPut;


			sprintf(gimmeFolder, "c:\\%s", GetProductName());
			iCurBranch = Gimme_GetBranchNum(gimmeFolder);

			iCurCoreBranch = Gimme_GetBranchNum("c:\\core");


			SetTextFast(GetDlgItem(hDlg, IDC_BRANCH), iCurBranch != -1 ? STACK_SPRINTF("%d", iCurBranch) : "!ERROR!");
			SetTextFast(GetDlgItem(hDlg, IDC_BRANCH_CORE), iCurCoreBranch != -1 ? STACK_SPRINTF("%d", iCurCoreBranch) : "!ERROR!");


			SetTextFast(GetDlgItem(hDlg, IDC_FROMSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iCurTime - 60 * 60));
			SetTextFast(GetDlgItem(hDlg, IDC_TOSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iCurTime));

			sprintf(tempStr, "%u", iCurTime - 60 * 60);
			SetTextFast(GetDlgItem(hDlg, IDC_FROMSECS), tempStr);


			sprintf(tempStr, "%u", iCurTime);
			SetTextFast(GetDlgItem(hDlg, IDC_TOSECS), tempStr);

			hOutPut = GetDlgItem(hDlg, IDC_OUTPUT);
			orig_OutputProc =  (WNDPROC)(LONG_PTR)SetWindowLongPtr(hOutPut,
				GWLP_WNDPROC, (LONG_PTR)MakeCtrlAWorkProc);

			
		}
		break;






	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_GETFROMSTRINGS:
		case ID_GETFROMSECS2:
			{
				char fromString[128];
				char toString[128];
				char branchString[128];
				char coreBranchString[128];
				U32 iFromTime, iToTime;
				char *pOutString = NULL;
				int iBranch, iCoreBranch;
				U32 eFlags = GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS;
				
				bool bPatchNotes = IsDlgButtonChecked(hDlg, IDC_PATCHNOTES);
				bool bShowBlanks = IsDlgButtonChecked(hDlg, IDC_SHOWBLANKS);
				bool bShowBuilders = IsDlgButtonChecked(hDlg, IDC_SHOWBUILDERS);
				bool bOutputCSV = IsDlgButtonChecked(hDlg, IDC_CSV);
				bool bRevNums = IsDlgButtonChecked(hDlg, IDC_REVNUMS);

				if(bShowBlanks)
				{
					eFlags &= ~GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS;
				}
				if(bShowBuilders)
				{
					eFlags &= ~GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS;
				}

				estrStackCreate(&pOutString);

				if (LOWORD(wParam) == ID_GETFROMSTRINGS)
				{
				
					GetWindowText(GetDlgItem(hDlg, IDC_FROMSTRING), SAFESTR(fromString));
					GetWindowText(GetDlgItem(hDlg, IDC_TOSTRING), SAFESTR(toString));
					GetWindowText(GetDlgItem(hDlg, IDC_BRANCH), SAFESTR(branchString));
					GetWindowText(GetDlgItem(hDlg, IDC_BRANCH_CORE), SAFESTR(coreBranchString));

					iBranch = atoi(branchString);
					iCoreBranch = atoi(coreBranchString);

					iFromTime = timeGetSecondsSince2000FromLocalGimmeString(fromString);
					iToTime = timeGetSecondsSince2000FromLocalGimmeString(toString);

					if (iBranch < 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be a non-negative int", branchString);
					}
					if (iCoreBranch < 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be a non-negative int", coreBranchString);
					}


					if (iFromTime == 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Should be in format MMDDYYHH{:MM{:SS}}\n", fromString);
					}

					if (iToTime == 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Should be in format MMDDYYHH{:MM{:SS}}\n", toString);
					}

				}
				else
				{
					GetWindowText(GetDlgItem(hDlg, IDC_FROMSECS), SAFESTR(fromString));
					GetWindowText(GetDlgItem(hDlg, IDC_TOSECS), SAFESTR(toString));
					GetWindowText(GetDlgItem(hDlg, IDC_BRANCH), SAFESTR(branchString));
					GetWindowText(GetDlgItem(hDlg, IDC_BRANCH_CORE), SAFESTR(coreBranchString));

					iBranch = atoi(branchString);
					iCoreBranch = atoi(coreBranchString);

					if (!StringToUint(fromString, &iFromTime))
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be secs since 2000", fromString);
						iFromTime = 0;
					}

					if (!StringToUint(toString, &iToTime))
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be secs since 2000", toString);
						iToTime = 0;
					}
				}

				if (iFromTime > iToTime)
				{
					U32 temp = iFromTime;
					iFromTime = iToTime;
					iToTime = temp;
				}


				if (iFromTime && iToTime && iBranch >= 0)
				{
					CheckinInfo **ppCheckins = NULL;
					char *pFolderString = NULL;
					bool bResult;

					sprintf(tempStr, "%u", iFromTime);
					SetTextFast(GetDlgItem(hDlg, IDC_FROMSECS), tempStr);

					sprintf(tempStr, "%u", iToTime);
					SetTextFast(GetDlgItem(hDlg, IDC_TOSECS), tempStr);

					SetTextFast(GetDlgItem(hDlg, IDC_FROMSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iFromTime));
					SetTextFast(GetDlgItem(hDlg, IDC_TOSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iToTime));



					estrStackCreate(&pFolderString);
					
					if (stricmp(GetProductName(), "core") == 0)
					{
						estrPrintf(&pFolderString, "c:/core");
					}
					else
					{
						estrPrintf(&pFolderString, "c:/%s;c:/core", GetProductName());
					}

					
					bResult =  Gimme_GetCheckinsBetweenTimes_ForceBranch(iFromTime, iToTime, pFolderString, NULL, eFlags, &ppCheckins, 120, iBranch, iCoreBranch);

					if (!bResult)
					{
						estrPrintf(&pOutString, "ERROR: Gimme_GetCheckins failed.\r\n"
							"Please check:\r\n"
							"  1. Make sure that c:\\cryptic\\tools\\bin is in your default path and is up to date.");
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
								if (!bPatchNotes || strstri_safe(ppCheckins[i]->checkinComment, "PATCHNOTES:"))
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
										if (bRevNums)
										{
											estrConcatf(&pOutString, "%s\tRev:%d(%s)\t%s\r\n",
												ppCheckins[i]->userName, ppCheckins[i]->iRevNum, timeGetLocalGimmeStringFromSecondsSince2000(ppCheckins[i]->iCheckinTimeSS2000), ppCheckins[i]->checkinComment);				
										}
										else
										{
											estrConcatf(&pOutString, "%s\t%s\t%s\r\n",
												ppCheckins[i]->userName, timeGetLocalGimmeStringFromSecondsSince2000(ppCheckins[i]->iCheckinTimeSS2000), ppCheckins[i]->checkinComment);
										}
									}
								}
							}
						}
					}


					estrDestroy(&pFolderString);
					eaDestroyEx(&ppCheckins, NULL);
				}

				SetTextFast(GetDlgItem(hDlg, IDC_OUTPUT), pOutString);

				estrDestroy(&pOutString);
			}
			break;

	
		}
		break;

		case WM_CLOSE:
			pWindow->bCloseRequested = true;

			return FALSE;

	}
	
	return FALSE;
}
