#include "mastercontrolprogram.h"
#include "estring.h"
#include "../../utilities/sentryserver/sentry_comm.h"
#include "winUtil.h"
#include "MCPHttp.h"
#include "HttpServing.h"
#include "earray.h"
#include "estring.h"
#include "Sock.h"
#include "winUtil.h"
#include "HttpServing.h"
#include "HttpServingStats.h"
#include "timing.h"


bool ServerMonitorControlScreenTickFunc(SimpleWindow *pWindow)
{

	if (gbHttpMonitor)
	{
		BeginMCPHttpMonitoring();
	}



	return true;

}
BOOL ServerMonitorControlScreenDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	switch (iMsg)
	{




	
	case WM_INITDIALOG:
			HttpStats_Init();
			SetTimer(hDlg, 0, 1, NULL);
			SetWindowText(hDlg, STACK_SPRINTF("Monitoring port %d", pWindow->iUserIndex));
		
			SetWindowText(GetDlgItem(hDlg, IDC_DISABLE_ENABLE), "DISABLE");
			break;

	case WM_TIMER:
		{
			//uses pWindow->pUserData as "last time updated"
			U32 iCurTime = timeSecondsSince2000();
			if ((U32)((uintptr_t)pWindow->pUserData) != iCurTime)
			{
				HttpStats *pStats = GetHttpStats(pWindow->iUserIndex);
				char *pStatsTotalString = NULL;
				char *pSizeString = NULL;
				char *pStats15String = NULL;
				int i;
				(U32)((uintptr_t)pWindow->pUserData) = iCurTime;

				estrMakePrettyBytesString(&pSizeString, pStats->iTotalBytesSent);
				estrPrintf(&pStatsTotalString, "Total sent:\r\n%s", pSizeString);
				SetWindowText(GetDlgItem(pWindow->hWnd, IDC_STATS_TOTAL), pStatsTotalString);

				estrMakePrettyBytesString(&pSizeString, pStats->iBytesSentLast15Secs);
				estrPrintf(&pStatsTotalString, "Last 15 seconds:\r\n%s", pSizeString);
				SetWindowText(GetDlgItem(pWindow->hWnd, IDC_STATS_LAST15), pStatsTotalString);



				estrClear(&pStatsTotalString);

				for (i=0; i < eaSize(&pStats->ppIPStats); i++)
				{
					estrMakePrettyBytesString(&pSizeString, pStats->ppIPStats[i]->iTotalBytesSent);
					estrConcatf(&pStatsTotalString, "%s: %s\r\n", makeIpStr(pStats->ppIPStats[i]->iIP), 
						pSizeString);
				
					if (pStats->ppIPStats[i]->iBytesLast15Secs)
					{
						estrMakePrettyBytesString(&pSizeString, pStats->ppIPStats[i]->iBytesLast15Secs);
						estrConcatf(&pStats15String, "%s: %s\r\n", makeIpStr(pStats->ppIPStats[i]->iIP), 
							pSizeString);
					}
				}

				SetWindowText(GetDlgItem(pWindow->hWnd, IDC_CONNECTED_TOTAL), pStatsTotalString);
				SetWindowText(GetDlgItem(pWindow->hWnd, IDC_CONNECTED_LAST15), pStats15String);

					
				estrDestroy(&pSizeString);
				estrDestroy(&pStatsTotalString);


/*			HttpStatsReport *pReport = HttpServing_GetStatsReport();
			char *pIPString = NULL;
			char *pStatsString = NULL;
			int i;

			for (i=0; i < eaSize(&pReport->ppIPCounts); i++)
			{
				estrConcatf(&pIPString, "%s(%d)\r\n", makeIpStr(pReport->ppIPCounts[i]->iIP), pReport->ppIPCounts[i]->iRequestsLast15Minutes);
			}

			SetWindowText(GetDlgItem(pWindow->hWnd, IDC_CONNECTED_IPS), pIPString);
			estrDestroy(&pIPString);

			estrPrintf(&pStatsString, "Total requests: %d\r\nLast 15 Minutes: %d", pReport->iTotalRequests, pReport->iRequestsLast15Minutes);
			SetWindowText(GetDlgItem(pWindow->hWnd, IDC_STATS), pStatsString);
			estrDestroy(&pStatsString);*/
			}

		}

		break;


	case WM_COMMAND:
		switch (LOWORD (wParam))
		{		
		case IDC_DISABLE_ENABLE:
			if (HttpServing_IsDisabled(pWindow->iUserIndex))
			{
				HttpServing_Disable(pWindow->iUserIndex, false);
				SetWindowText(GetDlgItem(hDlg, IDC_DISABLE_ENABLE), "DISABLE");
			}
			else
			{
				HttpServing_Disable(pWindow->iUserIndex, true);
				SetWindowText(GetDlgItem(hDlg, IDC_DISABLE_ENABLE), "ENABLE");
			}
			break;


		case IDCANCEL:
			pWindow->bCloseRequested = true;
		break;
		}

	
		

	}
	
	return FALSE;
}


