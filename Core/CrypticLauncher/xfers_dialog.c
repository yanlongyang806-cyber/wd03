// CrypicLauncher options dialog functions

#include "xfers_dialog.h"
#include "CrypticLauncher.h"
#include "launcherUtils.h"
#include "resource_CrypticLauncher.h"

#include "earray.h"
#include "SimpleWindowManager.h"
#include "pcl_typedefs.h"
#include "pcl_client_struct.h"
#include "patchxfer.h"
#include "timing.h"
#include "net.h"

#include "windef.h"
#include "Windowsx.h"
#include "Commctrl.h"

static XferStateInfo **g_xfers = NULL;

char *g_colnames[] = {"Filename", "State", "Path", "Blocks", "%", "Requested"};
int g_colwidths[] =  {180,         80,      255,    80,      40,  60};
STATIC_ASSERT(ARRAY_SIZE_CHECKED(g_colnames) == ARRAY_SIZE_CHECKED(g_colwidths));
char g_buf_blocks[MAX_XFERS][64];
char g_buf_progress[MAX_XFERS][32];
char g_buf_requested[MAX_XFERS][64];

static void computeSpeedAvgs(LauncherSpeedData *data, S64 *half, S64 *five, S64 *thirty)
{
	F64 total=0, cur;
	F32 curtime;
	int i, curi;

	for(i=0; i<LAUNCHER_SPEED_SAMPLES; i++)
	{
		curi = ((data->head - 1 - i) + LAUNCHER_SPEED_SAMPLES) % LAUNCHER_SPEED_SAMPLES;
		curtime = data->times[curi];
		if(curtime <= 0)
			cur = 0;
		else
			cur = data->deltas[curi] / curtime;
		total += cur;
		if(i==0)
			*half = cur;
		else if(i==9)
			*five = total / 10;
		else if(i==59)
			*thirty = total / 60;
	}
}

BOOL XfersPreDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	if(iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST)
	{
		if(wParam == 'x')
		{
			PostMessage(pWindow->hWnd, WM_COMMAND, IDCANCEL, 0);
			return TRUE;
		}
	}
	return FALSE;
}

BOOL XfersDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *window)
{
	SimpleWindow *main_window = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	CrypticLauncherWindow *launcher;
	
	if(!main_window)
	{
		// Main window has been closed
		window->bCloseRequested = true;
		return FALSE;
	}
	launcher = main_window->pUserData;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			HANDLE lv;
			LVCOLUMN lvc; 
			int i;

			// Early out guard
			if(!launcher || !launcher->client || !launcher->client->xferrer)
				return TRUE;

			lv = GetDlgItem(hDlg, IDC_XFERLIST);

			// Populate columns
			lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvc.fmt = LVCFMT_LEFT;
			for(i=0; i<ARRAY_SIZE_CHECKED(g_colnames); i++)
			{
				lvc.iSubItem = i;
				lvc.pszText = g_colnames[i];
				lvc.cx = g_colwidths[i];
				ListView_InsertColumn(lv, i, &lvc);
			}

			// Add list items
			ListView_SetItemCountEx(lv, eaSize(&launcher->client->xferrer->xfers_order), 0);

			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			// Handler for the red X in the corner
			ListView_SetItemCountEx(GetDlgItem(hDlg, IDC_XFERLIST), 0, 0);
			window->bCloseRequested = true;
			break;
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR) lParam)->code)
		{
		case LVN_GETDISPINFO:
		{
			NMLVDISPINFOA *plvdi = (NMLVDISPINFOA*)lParam;
			PatchXfer *xfer;
			if(!launcher || !launcher->client || !launcher->client->xferrer || plvdi->item.iItem >= eaSize(&launcher->client->xferrer->xfers_order))
			{
				plvdi->item.pszText = "";
				break;
			}
			xfer = launcher->client->xferrer->xfers_order[plvdi->item.iItem];
			switch (plvdi->item.iSubItem)
			{
			case 0:
				{
				char *p = strrchr(xfer->filename_to_write, '/');
				if(p)
					plvdi->item.pszText = p+1;
				else
					plvdi->item.pszText = (char*)xfer->filename_to_write;
				}
				break;
			case 1:
				plvdi->item.pszText = xferGetState(xfer);
				break;
			case 2:
				plvdi->item.pszText = (char*)xfer->filename_to_write;
				break;
			case 3:
				sprintf(g_buf_blocks[plvdi->item.iItem], "%u/%u", xfer->blocks_so_far, xfer->blocks_total);
				plvdi->item.pszText = g_buf_blocks[plvdi->item.iItem];
				break;
			case 4:
				if(xfer->blocks_total == 0)
					plvdi->item.pszText = "0.0";
				else
				{
					sprintf(g_buf_progress[plvdi->item.iItem], "%.1f", 100.0 * xfer->blocks_so_far / xfer->blocks_total);
					plvdi->item.pszText = g_buf_progress[plvdi->item.iItem];
				}
				break;
			case 5:
				{
				F32 req_num;
				char *req_units;
				U32 req_prec;

				humanBytes(xfer->bytes_requested, &req_num, &req_units, &req_prec);
				sprintf(g_buf_requested[plvdi->item.iItem], "%.*f%s", req_prec, req_num, req_units);
				plvdi->item.pszText = g_buf_requested[plvdi->item.iItem];
				}
				break;

			}
		}
		break;
		}
		break;
	}

	return FALSE;
}


bool XfersTickFunc(SimpleWindow *pWindow)
{
	SimpleWindow *main_window = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	CrypticLauncherWindow *launcher;

	F32 rec_num, tot_num, act_num;
	char *rec_units, *tot_units, *act_units;
	U32 rec_prec, tot_prec, act_prec;
	int nxfers;
	static U32 last_update = -1;
	static U32 speeds_timer = -1;
	char buf[2048], ipbuf[32];
	S64 half, five, thirty;

	if(!main_window)
	{
		// Main window has been closed
		return true;
	}
	launcher = main_window->pUserData;
	if(!launcher)
		return true;

	if(last_update == -1)
		last_update = timerAlloc();

	// Don't run the rest more than 10 Hz
	if(timerElapsed(last_update) < 0.1)
		return true;
	timerStart(last_update);

	// This doesn't require a patchclient, so update it now
	if(launcher->lastControllerTracker)
		sprintf(buf, "ct = %s", launcher->lastControllerTracker);
	else
		sprintf(buf, "ct = No link");
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_CONTROLLERTRACKER), buf);

	// Guard against weird client states
	if(!launcher->client || !launcher->client->xferrer)
		return true;

	// Update the list view
	nxfers = eaSize(&launcher->client->xferrer->xfers_order);
	ListView_SetItemCountEx(GetDlgItem(pWindow->hWnd, IDC_XFERLIST), nxfers, 0);

	// Update the status line
	sprintf(buf, "%u/%u transfers", nxfers, MAX_XFERS);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_STATUS_XFERS), buf);

	// FIXME: This multithreaded access to the xferrer is probably not safe.
	humanBytes(launcher->client->xferrer->net_bytes_free, &rec_num, &rec_units, &rec_prec);
	humanBytes(launcher->client->xferrer->max_net_bytes, &tot_num, &tot_units, &tot_prec);
	sprintf(buf, "net_bytes_free = %.*f%s/%.*f%s, %d%% HTTP",
		rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, launcher->http_percent);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_STATUS_NETBYTES), buf);

	sprintf(buf, "root = \"%s\"", launcher->client->root_folder);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_STATUS_ROOT), buf);

	computeSpeedAvgs(&launcher->speed_received, &half, &five, &thirty);
	humanBytes(half, &rec_num, &rec_units, &rec_prec);
	humanBytes(five, &tot_num, &tot_units, &tot_prec);
	humanBytes(thirty, &act_num, &act_units, &act_prec);
	sprintf(buf, "Received: %.*f%s/%.*f%s/%.*f%s", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, act_prec, act_num, act_units);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_SPEED_RECV), buf);

	computeSpeedAvgs(&launcher->speed_actual, &half, &five, &thirty);
	humanBytes(half, &rec_num, &rec_units, &rec_prec);
	humanBytes(five, &tot_num, &tot_units, &tot_prec);
	humanBytes(thirty, &act_num, &act_units, &act_prec);
	sprintf(buf, "Actual: %.*f%s/%.*f%s/%.*f%s", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, act_prec, act_num, act_units);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_SPEED_ACT), buf);

	computeSpeedAvgs(&launcher->speed_link, &half, &five, &thirty);
	humanBytes(half, &rec_num, &rec_units, &rec_prec);
	humanBytes(five, &tot_num, &tot_units, &tot_prec);
	humanBytes(thirty, &act_num, &act_units, &act_prec);
	sprintf(buf, "Link: %.*f%s/%.*f%s/%.*f%s", rec_prec, rec_num, rec_units, tot_prec, tot_num, tot_units, act_prec, act_num, act_units);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_SPEED_LINK), buf);

	if(launcher->client->link)
		linkGetIpStr(launcher->client->link, SAFESTR(ipbuf));
	else
		sprintf(ipbuf, "No link");
	sprintf(buf, "server = %s", ipbuf);
	Static_SetText(GetDlgItem(pWindow->hWnd, IDC_SERVER), buf);

	return true;
}