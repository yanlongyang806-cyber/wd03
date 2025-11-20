#ifndef _SENDPROGRESSDLG_H_
#define _SENDPROGRESSDLG_H_

#include <atlbase.h>
#include <atlwin.h>

#include "resource.h"

// stolen from commctrl.h
#define PBM_SETRANGE            (WM_USER+1)
#define PBM_SETPOS              (WM_USER+2)

class CSendProgressDlg : public CDialogImpl<CSendProgressDlg>
{
public:
	enum { IDD = IDD_PROGRESSDLG };

	BEGIN_MSG_MAP(CSendProgressDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

	int isDone;

	CSendProgressDlg() 
	{
		isDone = 0;
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CenterWindow();
		SetWindowText(_T("Sending Dump, please wait..."));

		GetDlgItem(IDC_PROGRESSBAR).SendMessage(PBM_SETRANGE, 0, MAKELPARAM(0, 100));
		GetDlgItem(IDC_PROGRESSBAR).SendMessage(PBM_SETPOS,   0, 0);

		GetDlgItem(IDC_OUTPUTTEXT).SetWindowTextA("Initializing ...");

		return TRUE;
	}

	void UpdateStatus(int iCurrentBytes, int iTotalBytes)
	{
		static char szStatus[256];
		static int  iEverMovingProgressBar = 0;
		int iPercentage;

		sprintf_s(szStatus, 256, "%d / %d bytes", iCurrentBytes, iTotalBytes);
		GetDlgItem(IDC_OUTPUTTEXT).SetWindowTextA(szStatus);

		if(iTotalBytes > 0)
		{
			double dPercentage = (double)(iCurrentBytes / (double)iTotalBytes) * 100;
			iPercentage = (int)dPercentage;
		}
		else
		{
			iPercentage = iEverMovingProgressBar;
			iEverMovingProgressBar = (iEverMovingProgressBar+1) % 100;
		}

		GetDlgItem(IDC_PROGRESSBAR).SendMessage(PBM_SETPOS, iPercentage, 0);
	}

	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		isDone = 1;
		return 0;
	}

};

#endif
