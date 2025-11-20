#include "mastercontrolprogram.h"
#include "estring.h"
#include "SuperEscaperScreen_c_ast.h"

AUTO_STRUCT;
typedef struct SimpleStringStruct
{
	char *pStr; AST(ESTRING)
} SimpleStringStruct;

BOOL SuperEscaperDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{

	case WM_INITDIALOG:
			SendMessage(    // returns LRESULT in lResult
			   GetDlgItem(hDlg, IDC_INSTRING),           // (HWND) handle to destination control
			   EM_LIMITTEXT,          // (UINT) message ID
			   0x7FFFFFFE,                // = () wParam; 
			   0);
			SendMessage(    // returns LRESULT in lResult
			   GetDlgItem(hDlg, IDC_OUTSTRING),           // (HWND) handle to destination control
			   EM_LIMITTEXT,          // (UINT) message ID
			   0x7FFFFFFE,                // = () wParam; 
			   0);
			break;




	case WM_COMMAND:
		switch (LOWORD (wParam))
		{


		case IDCANCEL:
			pWindow->bCloseRequested = true;
		break;

		case IDTEXTTOSUPESC:
			{
				char *pUnescapedString = NULL;
				char *pEscapedString = NULL;
				int iLen = GetWindowTextLength(GetDlgItem(pWindow->hWnd, IDC_INSTRING));

				estrStackCreate(&pUnescapedString);
				estrStackCreate(&pEscapedString);

				estrSetSize(&pUnescapedString, iLen);
				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_INSTRING), pUnescapedString, iLen+1);

				estrSuperEscapeString(&pEscapedString, pUnescapedString);

				SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), pEscapedString);

				estrDestroy(&pEscapedString);
				estrDestroy(&pUnescapedString);
			}
			break;
		case IDSUPESCTOTEXT:
			{
				char *pUnescapedString = NULL;
				char *pEscapedString = NULL;
				int iLen = GetWindowTextLength(GetDlgItem(pWindow->hWnd, IDC_INSTRING));

				estrStackCreate(&pUnescapedString);
				estrStackCreate(&pEscapedString);

				estrSetSize(&pUnescapedString, iLen);
				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_INSTRING), pUnescapedString, iLen+1);

				if (estrSuperUnescapeString(&pEscapedString, pUnescapedString))
				{
					SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), pEscapedString);
				}
				else
				{
					SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), "ERROR!!! Invalid SuperEscaped string");
				}

				estrDestroy(&pEscapedString);
				estrDestroy(&pUnescapedString);
			}
			break;	
		case IDTEXTTOTP:
			{
				SimpleStringStruct simpleStruct = {NULL};
				int iLen;
				char *pStringifiedStruct = NULL;
				char *pBeginning;
				char *pEnd;

				iLen = GetWindowTextLength(GetDlgItem(pWindow->hWnd, IDC_INSTRING));

				estrSetSize(&simpleStruct.pStr, iLen);
				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_INSTRING), simpleStruct.pStr, iLen+1);
				ParserWriteText(&pStringifiedStruct, parse_SimpleStringStruct, &simpleStruct, 0, 0, 0);

				pBeginning = strstri(pStringifiedStruct, "str ");
				if (!pBeginning)
				{
					SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), "ERROR!!! textparser failure (this should not be possible)");
				}
				else
				{
					pEnd = strchr(pBeginning, '\r');
					if (!pEnd)
					{
						SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), "ERROR!!! textparser failure (this should not be possible)");
					}
					else
					{

						*pEnd = 0;

						SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), pBeginning + 4);
					}

				}

				estrDestroy(&pStringifiedStruct);
				StructDeInit(parse_SimpleStringStruct, &simpleStruct);
			}
			break;

		case IDTPTOTEXT:
			{
				int iLen = GetWindowTextLength(GetDlgItem(pWindow->hWnd, IDC_INSTRING));
				char *pStructString = NULL;
				char *pInString = NULL;
				SimpleStringStruct simpleStruct = {NULL};

				estrSetSize(&pInString, iLen);
				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_INSTRING), pInString, iLen+1);
	

				estrPrintf(&pStructString, "{\n\tstr %s\n}\n", pInString);

				if (ParserReadText(pStructString,parse_SimpleStringStruct,&simpleStruct, 0))
				{
					SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), simpleStruct.pStr ? simpleStruct.pStr : "");
				}
				else
				{
					SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), "ERROR: Invalid textparser string");
				}

				estrDestroy(&pStructString);
				estrDestroy(&pInString);
				StructDeInit(parse_SimpleStringStruct, &simpleStruct);
			}
			break;
				
		case IDESCTOTEXT:
		case IDTEXTTOESC:
			{
				int iLen = GetWindowTextLength(GetDlgItem(pWindow->hWnd, IDC_INSTRING));
				char *pOutString = NULL;
				char *pInString = NULL;
				SimpleStringStruct simpleStruct = {NULL};

				estrSetSize(&pInString, iLen);
				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_INSTRING), pInString, iLen+1);
	
				if (LOWORD (wParam) == IDESCTOTEXT)
				{
					estrAppendUnescaped(&pOutString, pInString);
				}
				else
				{
					estrAppendEscaped(&pOutString, pInString);
				}


				estrFixupNewLinesForWindows(&pOutString);
			
				SetWindowText(GetDlgItem(pWindow->hWnd, IDC_OUTSTRING), pOutString);
				

				estrDestroy(&pOutString);
				estrDestroy(&pInString);
		
			}
			break;



		}
		

	}
	
	return FALSE;
}



#include "SuperEscaperScreen_c_ast.c"
