
#include "mastercontrolprogram.h"
#include "svnutils.h"
#include "Estring.h"
#include "winutil.h"
#include "timing.h"
#include "autogen/SVNUtils_h_ast.h"

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

BOOL svnCheckinsMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			char *pURL = NULL;
			int iCurRev;
			HWND hOutPut;
				
			estrStackCreate(&pURL);

			iCurRev = SVN_GetRevNumOfFolders("c:\\src",NULL, &pURL, 5);

			if (!iCurRev)
			{
				iCurRev = SVN_GetRevNumOfFolders("http://code/svn/dev", NULL, &pURL, 5);
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

			estrDestroy(&pURL);

			hOutPut = GetDlgItem(hDlg, IDC_OUTPUT);
			orig_OutputProc =  (WNDPROC)(LONG_PTR)SetWindowLongPtr(hOutPut,
				GWLP_WNDPROC, (LONG_PTR)MakeCtrlAWorkProc);
		}
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			{
				char fromString[128];
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
						estrPrintf(&pOutString, "ERROR: SVN_GetCheckins failed.\r\n"
							"Things to check:\r\n"
							"  1. Make sure that c:\\cryptic\\tools\\bin is in your default path and is up to date.\r\n"
							"  2. Make sure that you have logged in to Subversion (usually via Tortoise) and saved authorization.\r\n"
							"  3. The URL you provided is correct. (e.g. http://code/svn/dev)");
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

				estrDestroy(&pOutString);
			}
			break;

				


		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		}

	}
	
	return FALSE;
}
