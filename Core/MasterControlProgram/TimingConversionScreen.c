#include "mastercontrolprogram.h"
#include "estring.h"
#include "timing.h"
#include "winUtil.h"
#include "StringUtil.h"


void SetAllFromSS2000(HWND hDlg, U32 iSS2000)
{
	char temp[256];

	sprintf(temp, "%u", iSS2000);
	SetTextFast(GetDlgItem(hDlg, IDC_SS2000_EDIT), temp);

	timeMakeDateStringFromSecondsSince2000(temp, iSS2000);
	SetTextFast(GetDlgItem(hDlg, IDC_DATESTRING_EDIT), temp);

	timeMakeLocalDateStringFromSecondsSince2000(temp, iSS2000);
	SetTextFast(GetDlgItem(hDlg, IDC_LOCALDATESTRING_EDIT), temp);

	sprintf(temp, "%u", timeSecondsSince2000ToPatchFileTime(iSS2000));
	SetTextFast(GetDlgItem(hDlg, IDC_UNIXTIME_EDIT), temp);

	sprintf(temp, "%"FORM_LL"u", timerFileTimeFromSecondsSince2000(iSS2000));
	SetTextFast(GetDlgItem(hDlg, IDC_WINDOWSTIME_EDIT), temp);

	SetTextFast(GetDlgItem(hDlg, IDC_GIMMEDATE_EDIT), timeGetGimmeStringFromSecondsSince2000(iSS2000));

	SetTextFast(GetDlgItem(hDlg, IDC_LOCALGIMMEDATE_EDIT), timeGetLocalGimmeStringFromSecondsSince2000(iSS2000));

	SetTextFast(GetDlgItem(hDlg, IDC_LOGTIME_EDIT), timeGetLogDateStringFromSecondsSince2000(iSS2000));

}


BOOL TimingConversionDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	char temp[1024];
	U32 iSS2000 = 0;
	U32 iTemp;

	switch (iMsg)
	{

	case WM_INITDIALOG:
			SetAllFromSS2000(hDlg, timeSecondsSince2000());

			break;




	case WM_COMMAND:
		switch (LOWORD (wParam))
		{


		case IDCANCEL:
			pWindow->bCloseRequested = true;
		break;

		case IDC_NOW:
			SetAllFromSS2000(hDlg, timeSecondsSince2000());
			break;

		case IDC_SS2000_SET:
			
			GetWindowText(GetDlgItem(hDlg, IDC_SS2000_EDIT), SAFESTR(temp));

			if (StringToUint_Paranoid(temp, &iSS2000))
			{
				SetAllFromSS2000(hDlg, iSS2000);
			}
			
		break;

		case IDC_DATESTRING_SET:
			GetWindowText(GetDlgItem(hDlg, IDC_DATESTRING_EDIT), SAFESTR(temp));

			iSS2000 = timeGetSecondsSince2000FromDateString(temp);

			if (iSS2000)
			{
				SetAllFromSS2000(hDlg, iSS2000);
			}
		break;

		case IDC_LOCALDATESTRING_SET:
			GetWindowText(GetDlgItem(hDlg, IDC_LOCALDATESTRING_EDIT), SAFESTR(temp));

			iSS2000 = timeGetSecondsSince2000FromLocalDateString(temp);

			if (iSS2000)
			{
				SetAllFromSS2000(hDlg, iSS2000);
			}
		break;

		case IDC_UNIXTIME_SET:
			GetWindowText(GetDlgItem(hDlg, IDC_UNIXTIME_EDIT), SAFESTR(temp));

			if (StringToUint_Paranoid(temp, &iTemp))
			{
				SetAllFromSS2000(hDlg, timePatchFileTimeToSecondsSince2000(iTemp));
			}
		break;

		case IDC_WINDOWSTIME_SET:
			{
				S64 iTemp64;

				GetWindowText(GetDlgItem(hDlg, IDC_WINDOWSTIME_EDIT), SAFESTR(temp));

				iTemp64 = _atoi64(temp);

				if (iTemp64)
				{
				
					SetAllFromSS2000(hDlg,  timerSecondsSince2000FromFileTime(iTemp64));
				}
			}
					
		break;

		case IDC_GIMMEDATE_SET:
			GetWindowText(GetDlgItem(hDlg, IDC_GIMMEDATE_EDIT), SAFESTR(temp));

			iSS2000 = timeGetSecondsSince2000FromGimmeString(temp);

			if (iSS2000)
			{
				SetAllFromSS2000(hDlg, iSS2000);
			}
		
		break;
		
		case IDC_LOCALGIMMEDATE_SET:
			GetWindowText(GetDlgItem(hDlg, IDC_LOCALGIMMEDATE_EDIT), SAFESTR(temp));

			iSS2000 = timeGetSecondsSince2000FromLocalGimmeString(temp);

			if (iSS2000)
			{
				SetAllFromSS2000(hDlg, iSS2000);
			}
		
		break;

		case IDC_LOGTIME_SET:
			GetWindowText(GetDlgItem(hDlg, IDC_LOGTIME_EDIT), SAFESTR(temp));

			iSS2000 = timeGetSecondsSince2000FromLogDateString(temp);

			if (iSS2000)
			{
				SetAllFromSS2000(hDlg, iSS2000);
			}
		
		break;

		}

	}
	
	return FALSE;
}

