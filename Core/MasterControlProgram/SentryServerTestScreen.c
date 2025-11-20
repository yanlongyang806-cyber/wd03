#include "mastercontrolprogram.h"
#include "estring.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "winUtil.h"

NetLink *gpLinkToSentryServer = NULL;

static HWND sSSDlg = NULL;

void SentryServeTestScreenMessageCB(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{
	xcase MONITORSERVER_QUERY:
		{
			char *pFullString = NULL;

			while(!pktEnd(pak))
			{
				char *pValStr = pktGetStringTemp(pak);
				F64 fVal;
				if (!pValStr[0])
				{
					estrConcatf(&pFullString, "\r\n");
					continue;
				}
				fVal = pktGetF64(pak);

				estrConcatf(&pFullString, "%s %f\r\n", pValStr, (float)fVal);
			}
		

			SetTextFast(GetDlgItem(sSSDlg, IDC_RESPONSE), pFullString);

			estrDestroy(&pFullString);
		}
		break;
		
	}
}



BOOL sentryServerTestDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{




	
	case WM_INITDIALOG:
			sSSDlg = hDlg;
			SetTimer(hDlg, 0, 1, NULL);
			if (!gpLinkToSentryServer)
			{
				gpLinkToSentryServer = commConnect(commDefault(),LINKTYPE_SHARD_CRITICAL_20MEG, LINK_FORCE_FLUSH,"SentryServer",SENTRYSERVERMONITOR_PORT,SentryServeTestScreenMessageCB,NULL,NULL,0);
				SetTextFast(GetDlgItem(hDlg, IDC_SSCONNECTION), "Sentry Server: Connecting");
			}
			break;

	case WM_TIMER:
		if (linkConnected(gpLinkToSentryServer))
		{
			SetTextFast(GetDlgItem(hDlg, IDC_SSCONNECTION), "Sentry Server: Connected");
		}
		break;


	case WM_COMMAND:
		switch (LOWORD (wParam))
		{		
		case IDCANCEL:
			pWindow->bCloseRequested = true;
		break;

		case IDC_SENDTOSENTRYSERVER:
			{
				char query[4096];
				Packet *pPak = pktCreate(gpLinkToSentryServer, MONITORCLIENT_QUERY);
				GetWindowText(GetDlgItem(hDlg, IDC_QUERY), SAFESTR(query));
				pktSendString(pPak, query);
				pktSend(&pPak);
			}
			break;
		}
		

	}
	
	return FALSE;
}


