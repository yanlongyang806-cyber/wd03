#include "mastercontrolprogram.h"

char gQuitMessage[1024];

	



BOOL QuitMessageDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{

			
			SetWindowText(GetDlgItem(hDlg, IDC_QUITMESSAGE), gQuitMessage);
			
		}
		break;



	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
		{
			pWindow->bCloseRequested = true;
		
			return FALSE;

		}
		break;

	}
	
	return FALSE;
}
