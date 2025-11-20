#include "winutil.h"

#include "file.h"

#include "estring.h"
#include "logging.h"
#include "sysutil.h"
#include "earray.h"
#include "continuousBuilderSupport.h"
#include "error.h"
#include "MemTrack.h"
#include "utils.h"
#include "utf8.h"
#include "fileutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static HINSTANCE g_hInstance;
void winSetHInstance(HINSTANCE hInstance)
{
	g_hInstance = hInstance;
}

HINSTANCE winGetHInstance(void)
{
	if (!g_hInstance) {
		g_hInstance = GetModuleHandle(NULL);
	}
	return g_hInstance;
}


#if !_XBOX
#include <direct.h>
#include "convertutf.h"
#include "earray.h"
#include "StringUtil.h"
#include <stdio.h>
#include "timing.h"
#include <Tlhelp32.h>
#include "RegistryReader.h"
#include "genericdialog.h"
#include "jpeg.h"

#pragma comment(lib, "Comdlg32.lib")

void resizeControl(HWND hDlgParent, HWND hDlg, int dx, int dy, int dw, int dh)
{
	RECT rect={0};
	POINT pos = {0,0};

	GetWindowRect(hDlg, &rect); 
	pos.x = rect.left;
	pos.y = rect.top;
	ScreenToClient(hDlgParent, &pos);
	SetWindowPos(hDlg, NULL, pos.x + dx, pos.y + dy, rect.right - rect.left + dw, rect.bottom - rect.top + dh, SWP_NOZORDER);
}

typedef struct ControlList {
	unsigned int stretchx:1;
	unsigned int stretchy:1;
	unsigned int translatex:1;
	unsigned int translatey:1;
} ControlList;

static HWND parent=NULL;
static RECT alignme;
static RECT upperleft;

static BOOL CALLBACK EnumChildProc(HWND hwndChild, LPARAM lParam) 
{ 
	RECT rect;
	ControlList flags = {0,0,0,0};
	int id;
	int minx=upperleft.left+1, miny=upperleft.top+1;
	RECT rnew;

	if (GetParent(hwndChild)!=parent) {
		return TRUE;
	}

	id = GetWindowLong(hwndChild, GWL_ID); 

	GetWindowRect(hwndChild, &rect); 
	if (rect.left >= alignme.left) {
		flags.translatex = 1;
		flags.stretchx = 0;
	} else {
		if (rect.left <= upperleft.left && rect.left >= upperleft.left - 10) {
			// Left aligned
			flags.translatex = 0;
			flags.stretchx = 1;
			minx = upperleft.left;
		} else {
			flags.translatex = 0;
			flags.stretchx = 0;
		}
	}
	if (rect.top >= alignme.top) {
		flags.translatey = 1;
		flags.stretchy = 0;
	} else {
		if (rect.top <= upperleft.top && rect.top >= upperleft.top - 10) {
			// Top aligned
			miny=upperleft.top;
			flags.translatey = 0;
			flags.stretchy = 1;
		} else {
			flags.translatey = 0;
			flags.stretchy = 0;
		}
	}
	CopyRect(&rnew, &rect);
	OffsetRect(&rnew, flags.translatex*alignme.right, flags.translatey*alignme.bottom);
	rnew.right = flags.stretchx*alignme.right;
	rnew.bottom = flags.stretchy*alignme.bottom;
	if (rnew.left < minx && flags.translatex) {
		OffsetRect(&rnew, minx - rnew.left, 0);
	}
	if (rnew.top < miny && flags.translatey) {
		OffsetRect(&rnew, 0, miny - rnew.top);
	}
	rnew.right *= flags.stretchx;
	rnew.bottom *= flags.stretchy;
	resizeControl(GetParent(hwndChild), hwndChild,
		rnew.left - rect.left,
		rnew.top - rect.top,
		rnew.right,
		rnew.bottom
		);

	return TRUE;
}

typedef struct DlgResizeInfo {
	HWND hDlg;
	int lastw, lasth;
	int minw, minh;
} DlgResizeInfo;

static DlgResizeInfo **eaDRI_data=NULL;
static DlgResizeInfo ***eaDRI=&eaDRI_data;
DlgResizeInfo *getDRI(HWND hDlg)
{
	DlgResizeInfo *dri;
	int i;

	for (i=eaSize(eaDRI)-1; i>=0; i--) {
		dri = eaGet(eaDRI, i);
		if (dri->hDlg == hDlg) {
			return dri;
		}
	}
	dri = calloc(sizeof(DlgResizeInfo),1);
	eaPush(eaDRI, dri);
	dri->lastw = -1;
	dri->lasth = -1;
	dri->minh = -1;
	dri->minw = -1;
	dri->hDlg = hDlg;
	return dri;
}

void setDialogMinSize(HWND hDlg, WORD minw, WORD minh)
{
	DlgResizeInfo *dri = getDRI(hDlg);
	if (minw>0)
		dri->minw = minw;
	if (minh>0)
		dri->minh = minh;
}


void doDialogOnResize(HWND hDlg, WORD w, WORD h, int idAlignMe, int id)
{
	DlgResizeInfo *dri = getDRI(hDlg);
	int dw, dh;

	if (dri->lastw==-1) {
		dri->minw = dri->lastw = w;
		dri->minh = dri->lasth = h;
		return;
	}
	if (w < dri->minw)
		w = dri->minw;
	if (h < dri->minh)
		h = dri->minh;

	dw = w- dri->lastw, dh = h - dri->lasth;

	GetWindowRect(GetDlgItem(hDlg, idAlignMe), &alignme);
	GetWindowRect(GetDlgItem(hDlg, id), &upperleft);
	alignme.right = dw;
	alignme.bottom = dh;
	parent = hDlg;
	EnumChildWindows(hDlg, EnumChildProc, (LPARAM)NULL);
	dri->lastw = w;
	dri->lasth = h;
	RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

int NumLines(char *text)
{
	int ret = 1;
	while (*text) 
	{
		if (*text == '\n') ret++;
		text++;
	}
	return ret;
}

int LongestWord(char *text)
{
	int length, longest = 0;
	int beforeword = -1;
	int len = (int)strlen(text);
	int i;
	for (i = 0; i < len; i++)
	{
		if (text[i] == ' ')
		{
			length = i - beforeword - 1;
			if (length > longest) 
				longest = length;
			beforeword = i;
		}
	}
	length = i - beforeword - 1;
	if (length > longest)
		longest = length;
	return longest;
}

void OffsetWindow(HWND hDlg, HWND hWnd, int xdelta, int ydelta)
{
	RECT rc;
	GetWindowRect(hWnd, &rc);
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
	rc.left += xdelta; rc.right += xdelta;
	rc.top += ydelta;  rc.bottom += ydelta;
	MoveWindow(hWnd, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, FALSE);
}










// this is the error dialog resource description
// from "res2c assertdlg.res"
unsigned char ErrorResource[] = { // Error Dialog
	0x01, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x0A, 0xC8, 0x80, 
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE4, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x00, 
	0x72, 0x00, 0x79, 0x00, 0x70, 0x00, 0x74, 0x00, 0x69, 0x00, 0x63, 0x00, 0x20, 0x00, 0x53, 0x00, 
	0x74, 0x00, 0x75, 0x00, 0x64, 0x00, 0x69, 0x00, 0x6F, 0x00, 0x73, 0x00, 0x20, 0x00, 0x2D, 0x00, 
	0x20, 0x00, 0x45, 0x00, 0x72, 0x00, 0x72, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x20, 0x00, 0x44, 0x00, 
	0x69, 0x00, 0x61, 0x00, 0x6C, 0x00, 0x6F, 0x00, 0x67, 0x00, 0x00, 0x00, 0x08, 0x00, 0x90, 0x01, 
	0x00, 0x01, 0x4D, 0x00, 0x53, 0x00, 0x20, 0x00, 0x53, 0x00, 0x68, 0x00, 0x65, 0x00, 0x6C, 0x00, 
	0x6C, 0x00, 0x20, 0x00, 0x44, 0x00, 0x6C, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x50, 0x9C, 0x00, 0x7D, 0x00, 0x3D, 0x00, 0x0F, 0x00, 
	0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0x4F, 0x00, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x0A, 0x00, 0x7D, 0x00, 
	0x49, 0x00, 0x0F, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0x43, 0x00, 0x6F, 0x00, 
	0x70, 0x00, 0x79, 0x00, 0x20, 0x00, 0x74, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x43, 0x00, 0x6C, 0x00, 
	0x69, 0x00, 0x70, 0x00, 0x62, 0x00, 0x6F, 0x00, 0x61, 0x00, 0x72, 0x00, 0x64, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50, 
	0x0A, 0x00, 0x1C, 0x00, 0xCF, 0x00, 0x54, 0x00, 0xEB, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0x82, 0x00, 
	0x44, 0x00, 0x65, 0x00, 0x73, 0x00, 0x63, 0x00, 0x72, 0x00, 0x69, 0x00, 0x70, 0x00, 0x74, 0x00, 
	0x69, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x50, 0x0A, 0x00, 0x08, 0x00, 0xCF, 0x00, 0x10, 0x00, 
	0xEC, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0x82, 0x00, 0x46, 0x00, 0x61, 0x00, 0x75, 0x00, 0x6C, 0x00, 
	0x74, 0x00, 0x4D, 0x00, 0x65, 0x00, 0x73, 0x00, 0x73, 0x00, 0x61, 0x00, 0x67, 0x00, 0x65, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 
	0x57, 0x00, 0x7D, 0x00, 0x3B, 0x00, 0x0F, 0x00, 0x0D, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x00, 
	0x45, 0x00, 0x64, 0x00, 0x69, 0x00, 0x74, 0x00, 0x20, 0x00, 0x46, 0x00, 0x69, 0x00, 0x6C, 0x00, 
	0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 
	0xFF, 0xFF, 0x06, 0x00, 0xFF, 0xFF, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x10, 0x09, 0x04, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x61, 0x00, 0x73, 0x00, 0x73, 0x00, 0x65, 0x00, 
	0x72, 0x00, 0x74, 0x00, 0x64, 0x00, 0x6C, 0x00, 0x67, 0x00, 0x20, 0x00, 0x78, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x41, 0x00, 0x53, 0x00, 0x53, 0x00, 
	0x45, 0x00, 0x52, 0x00, 0x54, 0x00, 0x44, 0x00, 0x4C, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 
};
#define IDC_ERRORTEXT 1003
#define IDC_FAULTTEXT 1004
#define IDC_COPYTOCLIPBOARD 12
#define IDC_EDITFILE 13

typedef struct ErrorParams
{
	char* title;
	char* err;
	char* fault;
	int	  highlight;
	int   show_edit_file;
	HWND hParentDlg;
} ErrorParams;

char *utf8ToMbStatic(char *utf8string)
{
	wchar_t wbuffer[1024];
	static char sbuffer[1024];
	UTF8ToWideStrConvert(utf8string, wbuffer, ARRAY_SIZE(wbuffer));
	wcstombs(sbuffer, wbuffer, ARRAY_SIZE(sbuffer));
	return sbuffer;
}

// Message handler for error box.
LRESULT CALLBACK ErrorDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static char* errorbuf;
	static HFONT bigfont = 0;
	static int highlight;
	static int sTimerCounter;
	static HWND hParentDlg;


	switch (message)
	{
	case WM_INITDIALOG:
		{
			char title[1024];
			static char error[4096];
					   
			RECT rc, rc2;
			int heightneeded, widthneeded;
			int xdelta, ydelta;
			ErrorParams* param = (ErrorParams*)lParam;

			if (!param->title) param->title = "Program Error";

			hParentDlg = param->hParentDlg;

			Strncpyt(title,param->title);
			utf8ToMbcs(title,ARRAY_SIZE( title ));
			SetWindowText_UTF8(hDlg, title);

			Strncpyt(error,param->err);
 			utf8ToMbcs(error, ARRAY_SIZE( error ));
			SetWindowText_UTF8(GetDlgItem(hDlg, IDC_ERRORTEXT), error );
			errorbuf = error;
			
			highlight = param->highlight;
			EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);

			if (param->show_edit_file)
				ShowWindow(GetDlgItem(hDlg, IDC_EDITFILE), TRUE);
			else
				ShowWindow(GetDlgItem(hDlg, IDC_EDITFILE), FALSE);

			ShowCursor(1);

			// optionally show who is at fault
			if (param->fault)
			{
				LOGFONT lf;
				memset(&lf, 0, sizeof(LOGFONT));       // zero out structure
				lf.lfHeight = 20;                      // request a 12-pixel-height font
				//strcpy(lf.lfFaceName, "Arial");        // request a face name "Arial"
				lf.lfWeight = FW_BOLD;
				bigfont = CreateFontIndirect(&lf);
				SetWindowText_UTF8(GetDlgItem(hDlg, IDC_FAULTTEXT), param->fault);
				SendDlgItemMessage(hDlg, IDC_FAULTTEXT, WM_SETFONT, (WPARAM)bigfont, 0);
			}
			else
			{
				GetWindowRect(GetDlgItem(hDlg, IDC_ERRORTEXT), &rc);
				MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
				GetWindowRect(GetDlgItem(hDlg, IDC_FAULTTEXT), &rc2);
				MapWindowPoints(NULL, hDlg, (LPPOINT)&rc2, 2);
				ShowWindow(GetDlgItem(hDlg, IDC_FAULTTEXT), SW_HIDE);
				MoveWindow(GetDlgItem(hDlg, IDC_ERRORTEXT), rc.left, rc2.top, rc.right-rc.left, rc.bottom-rc2.top, FALSE);
			}

			// figure out the height and width needed for text - this 
			// is incorrect for a scaled font system, and the width is just
			// an approximation
			heightneeded = (int)(((float)12.5) * NumLines(param->err));
			widthneeded = (int)(((float)7) * LongestWord(param->err));

			// resize to correct height
			GetWindowRect(GetDlgItem(hDlg, IDC_ERRORTEXT), &rc);
			xdelta = 6 + widthneeded - rc.right + rc.left;
			ydelta = 6 + heightneeded - rc.bottom + rc.top;
			if (xdelta > 300) xdelta = 300; // cap growth
			if (ydelta > 200) ydelta = 200; 
			if (xdelta < 0) xdelta = 0;
			if (ydelta < 0) ydelta = 0;
			if (xdelta || ydelta)
			{
				MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
				MoveWindow(GetDlgItem(hDlg, IDC_ERRORTEXT), rc.left, rc.top, rc.right-rc.left+xdelta, rc.bottom-rc.top+ydelta, FALSE);
				
				// resize fault text
				GetWindowRect(GetDlgItem(hDlg, IDC_FAULTTEXT), &rc);
				MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
				MoveWindow(GetDlgItem(hDlg, IDC_FAULTTEXT), rc.left, rc.top, rc.right-rc.left+xdelta, rc.bottom-rc.top, FALSE);

				// resize main window
				GetWindowRect(hDlg, &rc);
				MoveWindow(hDlg, rc.left-xdelta/2, rc.top, rc.right-rc.left+xdelta, rc.bottom-rc.top+ydelta, FALSE);

				// move buttons
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDOK), xdelta/2, ydelta);
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_COPYTOCLIPBOARD), xdelta/2, ydelta);
				OffsetWindow(hDlg, GetDlgItem(hDlg, IDC_EDITFILE), xdelta/2, ydelta);
			}

			SetTimer(hDlg, 0, 1000, NULL);
			sTimerCounter = 0;
			return FALSE;
		}
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			EndDialog(hDlg, 0);
			ShowCursor(0);
			if (bigfont)
			{
				DeleteObject(bigfont);
				bigfont = 0;
			}
			return TRUE;
		}
		else if (LOWORD(wParam) == IDC_COPYTOCLIPBOARD)
		{
			winCopyToClipboard(errorbuf);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDC_EDITFILE)
		{
			if (errorbuf && strlen(errorbuf) > 8) {
				char filename[MAX_PATH];
				char *s;
				strncpy(filename, errorbuf+strlen("File: "), ARRAY_SIZE(filename)-1);
				s = strchr(filename, '\n');
				if (s) {
					*s = '\0';
					fileLocateWrite(filename, filename);
					fileOpenWithEditor(filename);
				}
			}
			return TRUE;
		}
		break;


	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_FAULTTEXT) && highlight) // turn the fault text red
		{
			SetTextColor((HDC)wParam, RGB(200, 0, 0));
			SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
			return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
		}
		break;

	case WM_TIMER:
		//after 5 seconds, flash the parent window, to make sure this dialog is not missed
		sTimerCounter++;
		if (sTimerCounter == 5)
		{
			if (hParentDlg)
			{
				flashWindow(hParentDlg);
			}
		}
		break;

	}
	return FALSE;
}


void errorDialogInternal(HWND hwnd, char *str, char* title, char* fault, int highlight) // title & fault optional
{
	ErrorParams params = {0};
	
	#if MAKE_ERRORS_LESS_ANNOYING
		return;
	#endif

	// Hack for holding Shift to ignore all pop-ups.  Shhh... don't tell anyone.
	if (((GetAsyncKeyState(VK_SHIFT) & 0x8000000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000000)) || errorGetVerboseLevel()==2) {
		return;
	}

	if (hwnd)
		ShowWindow(hwnd, SW_SHOW);

	params.title = title;
	params.err = str;
	params.fault = fault;
	params.highlight = highlight;
	params.show_edit_file = str && strStartsWith(str, "File: ");
	params.hParentDlg = hwnd;
	DialogBoxIndirectParam(winGetHInstance(), (LPDLGTEMPLATE)ErrorResource, hwnd, ErrorDlg, (LPARAM)&params);
}

static void msgAlertInternal(HWND hwnd, const char *str)
{
	ErrorParams params = {0};

	// Hack for holding Shift to ignore all pop-ups.  Shhh... don't tell anyone.
	if ((GetAsyncKeyState(VK_SHIFT) & 0x8000000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000000)) {
		printf("msgAlert: %s\n", str);
		return;
	}
	if (hwnd)
		ShowWindow(hwnd, SW_SHOW);

	params.err = strdup(str);
	params.hParentDlg = hwnd;
	DialogBoxIndirectParam(winGetHInstance(), (LPDLGTEMPLATE)ErrorResource, hwnd, ErrorDlg, (LPARAM)&params);
	SAFE_FREE(params.err);
}

HICON getIconColoredLetter(char letter, U32 colorRGB)
{
	HBITMAP hBitmapColor;
	HBITMAP hBitmapMask;
	ICONINFO info = {0};
	U32 buffer[16 * 16];
	HDC hdc;
	HICON hIcon;

	memset(buffer, 0?0:0xff, sizeof(buffer));
	
	if(0){
		buffer[0 + 0 * 16] = 0;
		buffer[0 + 15 * 16] = 0;
		buffer[15 + 0 * 16] = 0;
		buffer[15 + 15 * 16] = 0;
	}
	
	hBitmapMask = CreateBitmap(16, 16, 4, 8, buffer);
	
	//assert(hBitmapMask);
	
	ZeroArray(buffer);
	
	if(0){
		int i;
		for(i = 0; i < ARRAY_SIZE(buffer); i++){
			buffer[i] = 0x002020;//(rand() % 256) | ((rand() % 256) << 8);
		}
		
		hBitmapColor = CreateBitmap(16, 16, 4, 8, buffer);
	}else{
		HDC winHDC = CreateDC(L"DISPLAY", NULL, NULL, NULL);
		
		hdc = CreateCompatibleDC(winHDC);

		hBitmapColor = CreateCompatibleBitmap(winHDC, 16, 16);
		
		DeleteDC(winHDC);
	}
	     	
	SelectObject(hdc, hBitmapColor);
	
	//Rectangle(hdc, 0, 0, 10, 10);
	
	{
		HFONT hFont = NULL;
		S32 i;
		S32 curSize = 16;
		SIZE size = {0};
		
		while(!hFont && curSize > 5){
			hFont = CreateFont(curSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
			SelectObject(hdc, hFont);
			GetTextExtentPointA(hdc, &letter, 1, &size);
			if(curSize > 5 && (size.cx > 15 || size.cy > 15)){
				DeleteObject(hFont);
				hFont = NULL;
				curSize--;
			}
		}
				
		SetBkMode(hdc, TRANSPARENT);

		for(i = 0; i < 9; i++){
			SetTextColor(hdc, RGB(0, 0, 0));
			TextOutA(hdc, 8 - size.cx / 2 + (i % 3) - 1, 8 - size.cy / 2 + (i / 3) - 1, &letter, 1);
		}		

		SetTextColor(hdc, RGB((colorRGB >> 16) & 0xff, (colorRGB >> 8) & 0xff, (colorRGB >> 0) & 0xff));
		TextOutA(hdc, 8 - size.cx / 2, 8 - size.cy / 2, &letter, 1);
		DeleteObject(hFont);
	}
	
	DeleteDC(hdc);
	
	info.fIcon = TRUE;
	info.hbmColor = hBitmapColor;
	info.hbmMask = hBitmapMask;
	info.xHotspot = 0;
	info.yHotspot = 0;
	
	hIcon = CreateIconIndirect(&info);
	
	DeleteObject(hBitmapColor);
	DeleteObject(hBitmapMask);
	
	return hIcon;
}

void setWindowIconColoredLetter(HWND hwnd, char letter, U32 colorRGB)
{
	HICON hIcon = getIconColoredLetter(letter, colorRGB);

	if(hIcon){
		SendMessage(hwnd, WM_SETICON, (WPARAM)ICON_SMALL, (LPARAM)hIcon);
		SendMessage(hwnd, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);

		DeleteObject(hIcon);
		hIcon = NULL;
	}
}

void winRegisterMeEx(const char *commandName, const char *extension, const char *params) // Registers the current executable to handle files of the given extension
{
	char prog[CRYPTIC_MAX_PATH];
	char classname[CRYPTIC_MAX_PATH];
	RegReader reader;
	char key[CRYPTIC_MAX_PATH];
	char openstring[CRYPTIC_MAX_PATH];
	assert(extension[0]=='.');
	sprintf_s(SAFESTR(classname), "%s_auto_file", extension+1);
	Strcpy(prog,getExecutableName());
	backSlashes(prog);

	reader = createRegReader();
	sprintf_s(SAFESTR(key), "HKEY_CLASSES_ROOT\\%s", extension);
	initRegReader(reader, key);
	rrWriteString(reader, "", classname);
	destroyRegReader(reader);

	reader = createRegReader();
	sprintf_s(SAFESTR(key), "HKEY_CLASSES_ROOT\\%s\\shell\\%s\\command", classname, commandName);
	sprintf_s(SAFESTR(openstring), "\"%s\" %s", prog, params);
	initRegReader(reader, key);
	rrWriteString(reader, "", openstring);
	destroyRegReader(reader);

}

void winRegisterMe(const char *commandName, const char *extension) // Registers the current executable to handle files of the given extension
{
	winRegisterMeEx(commandName, extension, "\"%1\"");
}

char *winGetFileName_s(HWND hwnd, const char *fileMask, char *fileName, size_t fileName_size, bool save)
{
	OPENFILENAME theFileInfo;
	//char filterStrs[256];
	int		ret;
	char	base[_MAX_PATH];

	fileGetcwd(base,_MAX_PATH);
	memset(&theFileInfo,0,sizeof(theFileInfo));
	theFileInfo.lStructSize = sizeof(OPENFILENAME);
	theFileInfo.hwndOwner = hwnd;
	theFileInfo.hInstance = NULL;
	theFileInfo.lpstrFilter = UTF8_To_UTF16_malloc(fileMask);
	theFileInfo.lpstrCustomFilter = NULL;
	backSlashes(fileName);
	if (strEndsWith(fileName, "\\")) {
		fileName[strlen(fileName)-1] = '\0';
	}
	theFileInfo.lpstrFile = UTF8_To_UTF16_malloc(fileName);
	theFileInfo.nMaxFile = 255;
	theFileInfo.lpstrFileTitle = NULL;
	theFileInfo.lpstrInitialDir = NULL;
	theFileInfo.lpstrTitle = NULL;
	theFileInfo.Flags = OFN_LONGNAMES | OFN_OVERWRITEPROMPT;// | OFN_PATHMUSTEXIST;
	theFileInfo.lpstrDefExt = NULL;

	if (save)
		ret = GetSaveFileName(&theFileInfo);
	else
		ret = GetOpenFileName(&theFileInfo);
	_chdir(base);

	SAFE_FREE(theFileInfo.lpstrFile);
	SAFE_FREE(theFileInfo.lpstrFilter);

	if (ret)
		return fileName;
	else
		return NULL;
}

bool winExistsInRegPath(const char *path)
{
	char key[2048];
	char path_local[CRYPTIC_MAX_PATH];
	char oldpath[4096];
	RegReader rr = createRegReader();
	strcpy(path_local, path);
	backSlashes(path_local);	sprintf(key, "%s\\Environment", "HKEY_CURRENT_USER");
	initRegReader(rr, key);
	if (!rrReadString(rr, "PATH", SAFESTR(oldpath)))
		strcpy(oldpath, "");
	backSlashes(oldpath);
	destroyRegReader(rr);

	return !!strstri(oldpath, path_local);
}

bool winExistsInEnvPath(const char *path)
{
	char *path_env;
	char path_local[CRYPTIC_MAX_PATH];
	bool result;

	strcpy(path_local, path);
	backSlashes(path_local);
	_dupenv_s(&path_env, NULL, "PATH");
	backSlashes(path_env);
	if (path_env)
		result = !!strstri(path_env, path_local);
	else
		result = false;
	crt_free(path_env);
	return result;
}


void winAddToPath(const char *path, int prefix)
{
	char key[2048];
	char oldpath[4096];
	char oldpath_orig[4096];
	char newpath[4096]="";
	char path_local[CRYPTIC_MAX_PATH];
	char *last;
	char *str;
	RegReader rr = createRegReader();
	bool failedToRead=false;
	bool foundItAlready=false;
	strcpy(path_local, path);
	backSlashes(path_local);
	if (strEndsWith(path_local, "\\"))
		path_local[strlen(path_local)-1]='\0';
	sprintf(key, "%s\\Environment", "HKEY_CURRENT_USER");
	initRegReader(rr, key);
	if (!rrReadString(rr, "PATH", SAFESTR(oldpath))) {
		strcpy(oldpath, "");
		failedToRead = true;
	}
	strcpy(oldpath_orig, oldpath);

	if (prefix) {
		strcpy(newpath, path_local);
		strcat(newpath, ";");
	}
	str = strtok_r(oldpath, ";", &last);
	while (str) {
		backSlashes(str);
		if (strEndsWith(str, "\\"))
			str[strlen(str)-1]='\0';
		if (stricmp(path_local, str)==0) {
			// skip it
			foundItAlready = true;
		} else {
			strcat(newpath, str);
			strcat(newpath, ";");
		}
		str = strtok_r(NULL, ";", &last);
	}
	if (!prefix) {
		strcat(newpath, path_local);
		strcat(newpath, ";");
	}
	if (!foundItAlready && stricmp(newpath, oldpath_orig)!=0)
	{
		printf("Adding \"%s\" to system path.\n", path_local);
		filelog_printf("AddToPath", "Changed path from:\n%s\n\nTo:\n%s\n%s\n", oldpath_orig, newpath, failedToRead?"(Failed to read initial value)":"");
		rrWriteString(rr, "PATH", newpath);
		destroyRegReader(rr);
		{
			DWORD_PTR dw_ptr;
			SendMessageTimeout(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)"Environment", SMTO_NORMAL, 5000, &dw_ptr);
		}
	} else {
		destroyRegReader(rr);
	}
}



//////////////////////////////////////////////////////////////////////////

#else

#include "wininclude.h"
#include "StringUtil.h"
#include "WorkerThread.h"
#include "RegistryReader.h"

typedef struct DlgData
{
	WCHAR wtitle[1024];
	WCHAR wstr[2048];
	LPCWSTR OK_str;
	DWORD flags;
} DlgData;

static WorkerThread *dialog_thread = 0;

static void dispatchdialog(void *user_data, void *data, WTCmdPacket *packet)
{
	DlgData *dlg = (DlgData *)data;
	XOVERLAPPED overlapped = {0};
	MESSAGEBOX_RESULT mbresult = {0};

	// this is non blocking, and the render thread must keep going for the message box to display
	XShowMessageBoxUI(0,dlg->wtitle,dlg->wstr,1,&dlg->OK_str,0,dlg->flags,&mbresult,&overlapped);
	mbresult.dwButtonPressed=10000;
	Sleep(500);
	while (mbresult.dwButtonPressed==10000)
		Sleep(5);
	
	if (dialog_thread)
		wtQueueMsg(dialog_thread, WT_CMD_USER_START, 0, 0);
}

static void dispatchdialogdone(void *user_data, void *data, WTCmdPacket *packet)
{
	// ok has been pressed, do something with it?
}

static void showxdialog(DlgData *data)
{
#if 0
	dispatchdialog(0, 0, data);
#else
	if (!dialog_thread)
	{
		dialog_thread = wtCreate(1<<20, 1<<10, NULL, "showxdialog");
		wtRegisterCmdDispatch(dialog_thread, WT_CMD_USER_START, dispatchdialog);
		wtRegisterMsgDispatch(dialog_thread, WT_CMD_USER_START, dispatchdialogdone);
		wtSetThreaded(dialog_thread, true, 0, false);
		wtStart(dialog_thread);
	}

	wtQueueCmd(dialog_thread, WT_CMD_USER_START, data, sizeof(DlgData));
#endif
}

static void errorDialogInternal(HWND hwnd, char *str, char* title, char* fault, int highlight) // title & fault optional
{
	DlgData data;

	UTF8ToWideStrConvert(title?title:"", data.wtitle, ARRAY_SIZE_CHECKED(data.wtitle));
	UTF8ToWideStrConvert(str?str:"", data.wstr, ARRAY_SIZE_CHECKED(data.wstr));
	data.flags = XMB_ERRORICON;
	data.OK_str = L"OK";
	showxdialog(&data);
}

static void msgAlertInternal(HWND hwnd, const char *str)
{
	DlgData data;

	UTF8ToWideStrConvert("Program Error", data.wtitle, ARRAY_SIZE_CHECKED(data.wtitle));
	UTF8ToWideStrConvert(str, data.wstr, ARRAY_SIZE_CHECKED(data.wstr));
	data.flags = XMB_ALERTICON;
	data.OK_str = L"OK";
	showxdialog(&data);
}


char *winGetFileName_s(HWND hwnd, const char *fileMask, char *fileName, size_t fileName_size, bool save)
{
	return NULL;
}

void winAddToPath(const char *path, int prefix)
{
}
bool winExistsInRegPath(const char *path)
{
	return true;
}

bool winExistsInEnvPath(const char *path)
{
	return true;
}

#endif

typedef struct ErrorDialogCallbackData {
	ErrorDialogCallback cb;
	void *userdata;
} ErrorDialogCallbackData;

static ErrorDialogCallbackData **errorDialogCallbackStack=NULL;
//static ErrorDialogCallback errorDialogCallback;
static MsgAlertCallback msgAlertCallback;

void setErrorDialogCallback(ErrorDialogCallback callback, void *userdata)
{
	eaClear(&errorDialogCallbackStack);
	pushErrorDialogCallback(callback, userdata);
}

void pushErrorDialogCallback(ErrorDialogCallback callback, void *userdata)
{
	ErrorDialogCallbackData *data = calloc(1, sizeof(ErrorDialogCallbackData));
	data->cb = callback;
	data->userdata = userdata;
	eaPush(&errorDialogCallbackStack, data);
}

void popErrorDialogCallback(void)
{
	ErrorDialogCallbackData *data = eaPop(&errorDialogCallbackStack);
	if(data) free(data);
}


void setMsgAlertCallback(MsgAlertCallback callback)
{
	msgAlertCallback = callback;
}

bool isOSShuttingDown(void)
{
#if !_XBOX
	return !!GetSystemMetrics(SM_SHUTTINGDOWN);
#else
	return 0;
#endif
}

void errorDialog(HWND hwnd, char *str, char* title, char* fault, int highlight) // title & fault optional
{
	#if MAKE_ERRORS_LESS_ANNOYING
		return;
	#endif

// #if _DEBUG
//     printf("errorDialog: (%s):\n%s\n%s\n\n", title, fault, str);
// #endif

	if (errorDialogCallbackStack && eaSize(&errorDialogCallbackStack) && eaTail(&errorDialogCallbackStack)->cb)
	{
		eaTail(&errorDialogCallbackStack)->cb(hwnd, str, title, fault, highlight, eaTail(&errorDialogCallbackStack)->userdata);
	} else
		errorDialogInternal(hwnd, str, title, fault, highlight);
}

void msgAlert(HWND hwnd, const char *str)
{
	if (g_isContinuousBuilder)
	{
		Errorf("Message Alert: %s", str);
	}
	else if (msgAlertCallback)
		msgAlertCallback(hwnd, str);
	else
		msgAlertInternal(hwnd, str);
}

#if !_XBOX
typedef struct
{
	int iLeft, iRight, iTop, iBottom;
	enumScaleBehavior eHorizBehavior;
	enumScaleBehavior eVertBehavior;
	int iChildID;
} SingleControlScaleInfo;

typedef struct MultiControlScaleManager
{
	HWND hParentWind;
	RECT originalParentRect;
	SingleControlScaleInfo **ppChildren;
} MultiControlScaleManager;


MultiControlScaleManager *BeginMultiControlScaling(HWND hParent)
{
	MultiControlScaleManager *pManager = calloc(sizeof(MultiControlScaleManager), 1);
	pManager->hParentWind = hParent;
	GetWindowRect(hParent, &pManager->originalParentRect);

	return pManager;
}

void ReInitMultiControlScaling(MultiControlScaleManager *pManager, HWND hNewParent)
{
	pManager->hParentWind = hNewParent;
}


void MultiControlScaling_AddChild(MultiControlScaleManager *pManager, int iChildID, 
	enumScaleBehavior eHorizBehavior, enumScaleBehavior eVertBehavior)
{
	RECT rect;
	POINT pos;
	HWND hChildWindow;

	SingleControlScaleInfo *pSingle;
	if (!iChildID)
	{
		return;
	}

	hChildWindow = GetDlgItem(pManager->hParentWind, iChildID);


	pSingle = calloc(sizeof(SingleControlScaleInfo), 1);
	pSingle->eHorizBehavior = eHorizBehavior;
	pSingle->eVertBehavior = eVertBehavior;
	pSingle->iChildID = iChildID;
	
	GetWindowRect(hChildWindow, &rect);
	pos.x = rect.left;
	pos.y = rect.top;

	ScreenToClient(pManager->hParentWind, &pos);

	pSingle->iLeft = pos.x;
	pSingle->iTop = pos.y;
	pSingle->iRight = pSingle->iLeft + (rect.right - rect.left);
	pSingle->iBottom = pSingle->iTop + (rect.bottom - rect.top);


	eaPush(&pManager->ppChildren, pSingle);
}


void MultiControlScaling_Update(MultiControlScaleManager *pManager)
{
	RECT curParent;
	float fWidthRatio, fHeightRatio;

	int i;


	GetWindowRect(pManager->hParentWind, &curParent);

	fWidthRatio = (float)(curParent.right - curParent.left) / (float)(pManager->originalParentRect.right - pManager->originalParentRect.left);
	fHeightRatio = (float)(curParent.bottom - curParent.top) / (float)(pManager->originalParentRect.bottom - pManager->originalParentRect.top);

	for (i=0; i < eaSize(&pManager->ppChildren); i++)
	{
		SingleControlScaleInfo *pSingle = pManager->ppChildren[i];
		HWND hChildWindow = GetDlgItem(pManager->hParentWind, pSingle->iChildID);
		RECT newRect = {0};

		switch (pSingle->eHorizBehavior)
		{
		case SCALE_FULLRESIZE:
			newRect.left = pSingle->iLeft * fWidthRatio;
			newRect.right = pSingle->iRight * fWidthRatio;
			break;

		case SCALE_MOVE_NORESIZE:
			{
				int iNewCenter = (pSingle->iLeft + pSingle->iRight) / 2 * fWidthRatio;
				newRect.left = iNewCenter - (pSingle->iRight - pSingle->iLeft) / 2;
				newRect.right = newRect.left + pSingle->iRight - pSingle->iLeft;
			}
			break;

		case SCALE_LOCK_LEFT:
			newRect.left = pSingle->iLeft;
			newRect.right = pSingle->iRight;
			break;

		case SCALE_LOCK_RIGHT:
			newRect.right = (curParent.right - curParent.left) - (pManager->originalParentRect.right - pManager->originalParentRect.left - pSingle->iRight);
			newRect.left = newRect.right - (pSingle->iRight - pSingle->iLeft);
			break;

		case SCALE_LOCK_BOTH:
			newRect.right = (curParent.right - curParent.left) - (pManager->originalParentRect.right - pManager->originalParentRect.left - pSingle->iRight);
			newRect.left = pSingle->iLeft;
			break;
		}

		switch (pSingle->eVertBehavior)
		{
		case SCALE_FULLRESIZE:
			newRect.top = pSingle->iTop * fHeightRatio;
			newRect.bottom = pSingle->iBottom * fHeightRatio;
			break;

		case SCALE_MOVE_NORESIZE:
			{
				int iNewCenter = (pSingle->iTop + pSingle->iBottom) / 2 * fHeightRatio;
				newRect.top = iNewCenter - (pSingle->iBottom - pSingle->iTop) / 2;
				newRect.bottom = newRect.top + pSingle->iBottom - pSingle->iTop;
			}
			break;

		case SCALE_LOCK_TOP:
			newRect.top = pSingle->iTop;
			newRect.bottom = pSingle->iBottom;
			break;

		case SCALE_LOCK_BOTTOM:
			newRect.bottom = (curParent.bottom - curParent.top) - (pManager->originalParentRect.bottom - pManager->originalParentRect.top - pSingle->iBottom);
			newRect.top = newRect.bottom - (pSingle->iBottom - pSingle->iTop);
			break;

		case SCALE_LOCK_BOTH:
			newRect.bottom = (curParent.bottom - curParent.top) - (pManager->originalParentRect.bottom - pManager->originalParentRect.top - pSingle->iBottom);
			newRect.top = pSingle->iTop;
			break;
		}


		SetWindowPos(hChildWindow, NULL, newRect.left, newRect.top, 
			newRect.right - newRect.left, newRect.bottom - newRect.top, SWP_NOZORDER);
		InvalidateRect(hChildWindow, NULL, FALSE);
	}
}




void stringToJpeg(char *pInString, char *pFileName)
{
	const S16 *font_name = L"Arial";
	int font_size = 16;
	SIZE size = {0};
	U8 *pJpegBuf;
	int nColor;
	U8 *pBitmapBits;
	HBITMAP hBitmap;
	HFONT hFont;
	int x,y;
	int iJpegXSize;

	HDC hdc = CreateCompatibleDC(NULL);

	int nBMISize = sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD);
	BITMAPINFO *pBitmapInfo = (BITMAPINFO *) calloc(nBMISize, 1);


	pBitmapInfo->bmiHeader.biSize = sizeof(pBitmapInfo->bmiHeader);
	pBitmapInfo->bmiHeader.biWidth = 1200;
	pBitmapInfo->bmiHeader.biHeight = 28;
	pBitmapInfo->bmiHeader.biPlanes = 1;
	pBitmapInfo->bmiHeader.biBitCount = 8;
	pBitmapInfo->bmiHeader.biCompression = BI_RGB;
	pBitmapInfo->bmiHeader.biSizeImage = 
		(pBitmapInfo->bmiHeader.biWidth * 
		pBitmapInfo->bmiHeader.biHeight * 
		pBitmapInfo->bmiHeader.biBitCount) / 8;
	pBitmapInfo->bmiHeader.biXPelsPerMeter = 3200;
	pBitmapInfo->bmiHeader.biYPelsPerMeter = 3200;
	pBitmapInfo->bmiHeader.biClrUsed = 256;
	pBitmapInfo->bmiHeader.biClrImportant = 256;

	for(nColor = 0; nColor < 256; ++nColor)
	{
		pBitmapInfo->bmiColors[nColor].rgbBlue = pBitmapInfo->bmiColors[nColor].rgbGreen = pBitmapInfo->bmiColors[nColor].rgbRed
			= nColor; // (BYTE)((nColor > 128) ? 255 : 0);
		pBitmapInfo->bmiColors[nColor].rgbReserved = 0;
	}

	pBitmapBits=NULL;
	hBitmap = CreateDIBSection(hdc, pBitmapInfo, DIB_RGB_COLORS, (PVOID *) &pBitmapBits, NULL, 0);

	hFont = CreateFont(font_size, 0,
		0, 0,
		FW_BOLD,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		NONANTIALIASED_QUALITY, // NONANTIALIASED_QUALITY  ANTIALIASED_QUALITY
		VARIABLE_PITCH, // VARIABLE_PITCH, DEFAULT_PITCH | FF_SWISS,
		font_name);

	SelectObject(hdc, hFont);
	SelectObject(hdc, hBitmap);
	SelectObject(hdc, GetStockObject(WHITE_PEN)); 
	SelectObject(hdc, GetStockObject(WHITE_BRUSH)); 

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(0, 0, 0));
	SetBkColor(hdc, RGB(255, 255, 255));

//	FontInfo *font_info = callocStruct(FontInfo);


	GetTextExtentPoint_UTF8(hdc, pInString, (int)strlen(pInString), &size);

	Rectangle(hdc, 0, 0, pBitmapInfo->bmiHeader.biWidth, pBitmapInfo->bmiHeader.biHeight);
	TextOut_UTF8(hdc, 5, 0, pInString, (int)strlen(pInString));

	iJpegXSize = size.cx + 10;

	pJpegBuf = calloc(3 * iJpegXSize * pBitmapInfo->bmiHeader.biHeight, 1);

	for (y = 0; y < pBitmapInfo->bmiHeader.biHeight; y++)
	{
		for (x = 0; x < iJpegXSize; x++)
		{
			pJpegBuf[3 * (y * iJpegXSize + x)] = 
			pJpegBuf[3 * (y * iJpegXSize + x) + 1] = 
			pJpegBuf[3 * (y * iJpegXSize + x) + 2] = 
				((U8*)pBitmapBits)[(pBitmapInfo->bmiHeader.biHeight - 1 - y) * pBitmapInfo->bmiHeader.biWidth + x];
		}
	}

	{
		char* pDupFileName = strdup(pFileName);
		jpgSave(pDupFileName, (U8*)pJpegBuf, 3, iJpegXSize, pBitmapInfo->bmiHeader.biHeight, 95);

		free(pDupFileName);
	}
	
	free(pJpegBuf);
	DeleteObject(hBitmap);
	DeleteDC(hdc);
}

bool DEFAULT_LATELINK_TripleControlCOverride(void)
{
	return false;
}

// Default action of useSafeCloseHandler().
static void DefaultSafeCloseAction(DWORD fdwCtrlType)
{
	if (fdwCtrlType == CTRL_C_EVENT)
	{
		if (TripleControlCOverride())
		{
			return;
		}
	}

	printf("Exiting. (PID = %u)\n", getpid());
	exit(-1);
}

// Current safe close action
static SafeCloseActionType safeCloseAction = DefaultSafeCloseAction;

// Handle a Ctrl-C event.
static BOOL removeCloseButton_consoleCtrlHandler(DWORD fdwCtrlType)
{
	static U64 iThreeTimes[3] = {0};
	static U8 iIndex = 0;

	switch (fdwCtrlType){ 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
			printf("Exiting. (PID = %u)\n", getpid());
			safeCloseAction(fdwCtrlType);
			return TRUE;

		case CTRL_CLOSE_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			{
				U64 iCurTime = timeMsecsSince2000();
				iIndex = (iIndex + 1)%3;
				iThreeTimes[iIndex] = iCurTime;

				if (iThreeTimes[(iIndex + 1) %3] > iCurTime - 3000 && iThreeTimes[(iIndex + 2) %3] > iCurTime - 3000)
					safeCloseAction(fdwCtrlType);
				else
				{
					consolePushColor();
					consoleSetColor(0, COLOR_RED | COLOR_BRIGHT);
					printf("Ctrl-C received - press twice more quickly to actually close this app (PID = %u)\n", getpid());
					consolePopColor();
				}

				return TRUE; 
			}

		// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
}

// Disable the console close button.
void disableConsoleCloseButton(void)
{
	HWND h;
	HMENU sm;
	h = GetConsoleWindow();
	sm = GetSystemMenu(h, false);
	RemoveMenu(sm, SC_CLOSE, MF_BYCOMMAND);
}

// Use the "safe" Ctrl-C handler that requires Ctrl-C to be pressed three times.
void useSafeCloseHandler()
{
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)removeCloseButton_consoleCtrlHandler, TRUE);
}

// Set the action that useSafeCloseHandler() takes when Ctrl-C is pressed.  Returns the previous one.
SafeCloseActionType setSafeCloseAction(SafeCloseActionType SafeCloseActionFptr)
{
	SafeCloseActionType old = safeCloseAction;
	safeCloseAction = SafeCloseActionFptr;
	return old;
}

// Disable the console close button, and use safe Ctrl-C handler.
AUTO_COMMAND ACMD_COMMANDLINE;
void removeConsoleCloseButton(int iDummy)
{
	//disable close close button
	disableConsoleCloseButton();

	//whenever we are disabling the console close button we also hook up our special must-press-ctrl-c-three-times ctrl handler
	useSafeCloseHandler();
}

U8* stringToBuffer(char *pInString, int *pOutXSize, int *pOutYSize)
{
	int font_size = 16;
	SIZE size = {0};
	int nColor;
	U8 *pBitmapBits;
	HBITMAP hBitmap;
	HFONT hFont;
	int x,y;
	U8 *pOutBuffer;

	HDC hdc = CreateCompatibleDC(NULL);

	int nBMISize = sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD);
	BITMAPINFO *pBitmapInfo = (BITMAPINFO *) calloc(nBMISize, 1);


	pBitmapInfo->bmiHeader.biSize = sizeof(pBitmapInfo->bmiHeader);
	pBitmapInfo->bmiHeader.biWidth = 1200;
	pBitmapInfo->bmiHeader.biHeight = 28;
	pBitmapInfo->bmiHeader.biPlanes = 1;
	pBitmapInfo->bmiHeader.biBitCount = 8;
	pBitmapInfo->bmiHeader.biCompression = BI_RGB;
	pBitmapInfo->bmiHeader.biSizeImage = 
		(pBitmapInfo->bmiHeader.biWidth * 
		pBitmapInfo->bmiHeader.biHeight * 
		pBitmapInfo->bmiHeader.biBitCount) / 8;
	pBitmapInfo->bmiHeader.biXPelsPerMeter = 3200;
	pBitmapInfo->bmiHeader.biYPelsPerMeter = 3200;
	pBitmapInfo->bmiHeader.biClrUsed = 256;
	pBitmapInfo->bmiHeader.biClrImportant = 256;

	for(nColor = 0; nColor < 256; ++nColor)
	{
		pBitmapInfo->bmiColors[nColor].rgbBlue = pBitmapInfo->bmiColors[nColor].rgbGreen = pBitmapInfo->bmiColors[nColor].rgbRed
			= nColor; // (BYTE)((nColor > 128) ? 255 : 0);
		pBitmapInfo->bmiColors[nColor].rgbReserved = 0;
	}

	pBitmapBits=NULL;
	hBitmap = CreateDIBSection(hdc, pBitmapInfo, DIB_RGB_COLORS, (PVOID *) &pBitmapBits, NULL, 0);

	hFont = CreateFont(font_size, 0,
		0, 0,
		FW_BOLD,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		NONANTIALIASED_QUALITY, // NONANTIALIASED_QUALITY  ANTIALIASED_QUALITY
		VARIABLE_PITCH, // VARIABLE_PITCH, DEFAULT_PITCH | FF_SWISS,
		L"Arial");

	SelectObject(hdc, hFont);
	SelectObject(hdc, hBitmap);
	SelectObject(hdc, GetStockObject(WHITE_PEN)); 
	SelectObject(hdc, GetStockObject(WHITE_BRUSH)); 

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(0, 0, 0));
	SetBkColor(hdc, RGB(255, 255, 255));

//	FontInfo *font_info = callocStruct(FontInfo);


	GetTextExtentPoint_UTF8(hdc, pInString, (int)strlen(pInString), &size);

	Rectangle(hdc, 0, 0, pBitmapInfo->bmiHeader.biWidth, pBitmapInfo->bmiHeader.biHeight);
	TextOut_UTF8(hdc, 5, 0, pInString, (int)strlen(pInString));

	*pOutXSize = size.cx + 10;
	*pOutYSize = pBitmapInfo->bmiHeader.biHeight - 10;

	pOutBuffer = calloc((*pOutXSize) * (*pOutYSize), 1);
	for (y = 0; y < *pOutYSize; y++)
	{
		for (x = 0; x < *pOutXSize; x++)
		{
			pOutBuffer[y * (*pOutXSize) + x] = 
				((U8*)pBitmapBits)[(pBitmapInfo->bmiHeader.biHeight - 1 - y) * pBitmapInfo->bmiHeader.biWidth + x];
		}
	}

	
	DeleteObject(hBitmap);
	DeleteDC(hdc);

	return pOutBuffer;
}


void SetWindowTextCleanedup(HWND hWnd, const char *text)
{
	char *buf=NULL;
	estrStackCreate(&buf);
	estrCopy2(&buf, text);
	estrFixupNewLinesForWindows(&buf);
	SetWindowText_UTF8(hWnd, buf);
	estrDestroy(&buf);
}

#endif

bool checkForRequiredClientFiles(void)
{
	bool bMissing=false;
	if (isProductionMode())
	{
		// Nothing to check here?
		// To even know we're in production mode, we must have had hogg files.
		// If a production user is missing his hogg files, it will think it's in
		//  development mode below.
	} else {
		// Development mode
		if (!fileExists("shaders/D3D/error.vhl")) // The first critical file loaded
			bMissing = true;
	}
	if (bMissing)
	{

#if _XBOX
		DlgData data;
		char *title = "Unable to find data files.";
		char *message1 =
			"If this problem persists, reinstalling the application may fix the situation.\n\n"
			"If this is a dev build, the likely cause is being unable to access your PC share with an appropriate username and password because of one of:\n";
		char *message2 =
			"  - Not correctly setting up the username/password.\n"
			"  - Installing a new XDK version (may have wiped the username/password).\n"
			"  - Changing your domain password.\n\n"
			"Please verify your username and password are set up appropriately.";
		char *message3 =
			"A less common, but possible, cause is an incorrectly set up GameDataDir.txt (this should be automatically generated for you).";
		// Something special
		UTF8ToWideStrConvert(title, data.wtitle, ARRAY_SIZE_CHECKED(data.wtitle));

		UTF8ToWideStrConvert(message1, data.wstr, ARRAY_SIZE_CHECKED(data.wstr));
		data.flags = XMB_ERRORICON;
		data.OK_str = L"Continue...";
		dispatchdialog(0, &data, 0);

		Sleep(1000); // Wait for the previous dialog to clear
		UTF8ToWideStrConvert(message2, data.wstr, ARRAY_SIZE_CHECKED(data.wstr));
		dispatchdialog(0, &data, 0);

		Sleep(1000); // Wait for the previous dialog to clear
		UTF8ToWideStrConvert(message3, data.wstr, ARRAY_SIZE_CHECKED(data.wstr));
		data.OK_str = L"OK";
		dispatchdialog(0, &data, 0);
#else
		char title[1024];
		sprintf(title, "%s: Missing Required Files", getExecutableName());
		MessageBox_UTF8(NULL, "Unable to find required game data files.  If this problem persists, reinstalling the application may fix the situation.", title, MB_ICONERROR|MB_OK);
#endif
	}
	return !bMissing;
}

#if _MSC_VER < 1600 // VS10 includes this
typedef struct _CONSOLE_SCREEN_BUFFER_INFOEX {
	ULONG cbSize;
	COORD dwSize;
	COORD dwCursorPosition;
	WORD wAttributes;
	SMALL_RECT srWindow;
	COORD dwMaximumWindowSize;
	WORD wPopupAttributes;
	BOOL bFullscreenSupported;
	COLORREF ColorTable[16];
} CONSOLE_SCREEN_BUFFER_INFOEX, *PCONSOLE_SCREEN_BUFFER_INFOEX;
#endif

// Vista+
static bool consoleSetPaletteAtRunTime(U32 palette[16])
{
	int i;
	HANDLE hConsoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFOEX sbi = {0};
	typedef BOOL (WINAPI *tGetConsoleScreenBufferInfoEx)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
	tGetConsoleScreenBufferInfoEx pGetConsoleScreenBufferInfoEx = NULL;
	tGetConsoleScreenBufferInfoEx pSetConsoleScreenBufferInfoEx = NULL;
	HINSTANCE hKernel32Dll = LoadLibrary( L"kernel32.dll" );
	if (hKernel32Dll)
	{
		pGetConsoleScreenBufferInfoEx = (tGetConsoleScreenBufferInfoEx)GetProcAddress(hKernel32Dll, "GetConsoleScreenBufferInfoEx");
		pSetConsoleScreenBufferInfoEx = (tGetConsoleScreenBufferInfoEx)GetProcAddress(hKernel32Dll, "SetConsoleScreenBufferInfoEx");
		FreeLibrary(hKernel32Dll);
	}
	if (pGetConsoleScreenBufferInfoEx && pSetConsoleScreenBufferInfoEx)
	{
		sbi.cbSize = sizeof(sbi);
		pGetConsoleScreenBufferInfoEx(hConsoleOut, &sbi);
		for(i = 0; i < 16; i++)
			sbi.ColorTable[i] = palette[i];
		return pSetConsoleScreenBufferInfoEx(hConsoleOut, &sbi);
	}
	return false;
}

void consoleSetPalette(U32 palette[16])
{
#if !_XBOX
	RegReader rr = createRegReader();
	char key[MAX_PATH*2];
	char *s;
	char *s2, *s2_starting;
	int i;
	strcpy(key, "HKEY_CURRENT_USER\\Console\\");
	s = key + strlen(key);
	s2_starting = s2 = strdup(GetCommandLine());
	strncpy_s(s, ARRAY_SIZE(key) - (s - key), (*s2=='\"')?(s2+1):s2, _TRUNCATE);
	if (*s2 == '\"')
	{
		s2 = strchr(s, '\"');
		*s2 = '\0';
	} else {
		s2 = strchr(s, ' ');
		if (s2)
			*s2 = '\0';
	}
	strchrReplace(s, '\\', '_');
	//strchrReplace(s, '/', '_');
	initRegReader(rr, key);
	for (i=0; i<16; i++)
	{
		char valueName[20];
		sprintf(valueName, "ColorTable%02d", i);
		rrWriteInt(rr, valueName, palette[i]);
	}
	destroyRegReader(rr);

	consoleSetPaletteAtRunTime(palette);
	free(s2_starting);
#endif
}

#if !_XBOX

void estrGetWindowText(char **ppEstr, HWND hWnd)
{
	GetWindowText_UTF8(hWnd, ppEstr);
}


bool SetTextFast(HWND hWnd, const char *text)
{
	S16 *pWideInText = NULL;
	bool bChanged = false;
	int iLen;

	pWideInText = UTF8_To_UTF16_malloc(text);


	iLen = pWideInText ? (int)wcslen(pWideInText) : 0;

	if (iLen > 1023)
	{
		int iBufSize = iLen + 16;
		S16 *pBuf = malloc(iBufSize * sizeof(S16));
		GetWindowText(hWnd, pBuf, iBufSize - 1);
		if (wcscmp(pBuf, pWideInText?pWideInText:L"")!=0) 
		{
			bChanged = true;
			SetWindowText(hWnd, pWideInText);
		}

		free(pBuf);
		
		
		
	}
	else
	{
		static S16 buf[1024];
		GetWindowText(hWnd, SAFESTR(buf));
		if (wcscmp(buf, pWideInText?pWideInText:L"")!=0) 
		{
			bChanged = true;
			SetWindowText(hWnd, pWideInText);
		}
	}

	SAFE_FREE(pWideInText);

	return bChanged;
}



bool SetTextFastf(HWND hWnd, FORMAT_STR const char *text, ...)
{
	char *pFullString = NULL;
	bool bRetVal;
	estrStackCreate(&pFullString);
	estrGetVarArgs(&pFullString, text);
	bRetVal = SetTextFast(hWnd, pFullString);
	estrDestroy(&pFullString);
	return bRetVal;
}

#endif


void winCheckAccessibilityShortcuts(CheckWhen when)
{
#if !PLATFORM_CONSOLE
	static STICKYKEYS g_StartupStickyKeys = {sizeof(STICKYKEYS), 0};
	static TOGGLEKEYS g_StartupToggleKeys = {sizeof(TOGGLEKEYS), 0};
	static FILTERKEYS g_StartupFilterKeys = {sizeof(FILTERKEYS), 0};    
	static RegReader rr;
	static bool currentlyDisabled = false;
	bool bNeedReenable = false;
	bool bNeedDisable = false;
	switch (when)
	{
		xcase CheckWhen_Startup:
		{
			int value=0;

			// Save the current sticky/toggle/filter key settings so they can be restored them later
			SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &g_StartupStickyKeys, 0);
			SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &g_StartupToggleKeys, 0);
			SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &g_StartupFilterKeys, 0);

			rr = createRegReader();
			initRegReader(rr, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE");
			if (rrReadInt(rr, "NeedAccessRestore", &value))
			{
				// Get previous values from the registry (will restore on task switch or process exit
				rrReadInt(rr, "AccessRestore_Sticky", &g_StartupStickyKeys.dwFlags);
				rrReadInt(rr, "AccessRestore_Toggle", &g_StartupToggleKeys.dwFlags);
				rrReadInt(rr, "AccessRestore_Filter", &g_StartupFilterKeys.dwFlags);
			}
		}
		xcase CheckWhen_RunTimeInactive:
			assert(rr); // Must have called this at startup first
			if (currentlyDisabled)
				bNeedReenable = true;
		xcase CheckWhen_RunTimeActive:
			assert(rr); // Must have called this at startup first
			if (!currentlyDisabled)
				bNeedDisable = true;
		xcase CheckWhen_Shutdown:
			if (rr && currentlyDisabled)
				bNeedReenable = true;
	}

	if (bNeedReenable)
	{
		// Restore to original, reset registry flag
		STICKYKEYS sk = g_StartupStickyKeys;
		TOGGLEKEYS tk = g_StartupToggleKeys;
		FILTERKEYS fk = g_StartupFilterKeys;

		SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &sk, 0);
		SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &tk, 0);
		SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &fk, 0);

		rrDelete(rr, "NeedAccessRestore");
		rrDelete(rr, "AccessRestore_Sticky");
		rrDelete(rr, "AccessRestore_Toggle");
		rrDelete(rr, "AccessRestore_Filter");

		currentlyDisabled = false;
	}
	if (bNeedDisable)
	{
		STICKYKEYS skOff = g_StartupStickyKeys;
		TOGGLEKEYS tkOff = g_StartupToggleKeys;
		FILTERKEYS fkOff = g_StartupFilterKeys;

		currentlyDisabled = true;

		rrWriteInt(rr, "AccessRestore_Sticky", g_StartupStickyKeys.dwFlags);
		rrWriteInt(rr, "AccessRestore_Toggle", g_StartupToggleKeys.dwFlags);
		rrWriteInt(rr, "AccessRestore_Filter", g_StartupFilterKeys.dwFlags);
		rrWriteInt(rr, "NeedAccessRestore", 1);

		// Disable StickyKeys/etc shortcuts but if the accessibility feature is on, 
		// then leave the settings alone as its probably being usefully used
		if( (skOff.dwFlags & SKF_STICKYKEYSON) == 0 )
		{
			// Disable the hotkey and the confirmation
			skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
			skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;

			SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &skOff, 0);
		}

		if( (tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0 )
		{
			// Disable the hotkey and the confirmation
			tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
			tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;

			SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &tkOff, 0);
		}

		if( (fkOff.dwFlags & FKF_FILTERKEYSON) == 0 )
		{
			// Disable the hotkey and the confirmation
			fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
			fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;

			SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &fkOff, 0);
		}

	}

#endif
}

void winAboutToExitMaybeCrashed(void)
{
	winCheckAccessibilityShortcuts(CheckWhen_Shutdown);
}


/*-----------------------------------------------------
Dialog scrolling code from here:
http://www.codeproject.com/KB/dialog/scroll_dialog.aspx
-------------------------------------------------*/


void SD_ScrollClient(HWND hwnd, int bar, int pos);
void SD_OnHVScroll(HWND hwnd, int bar, UINT code);
int SD_GetScrollPos(HWND hwnd, int bar, UINT code);


BOOL SD_OnInitDialog(HWND hwnd)
{
    RECT rc = {0};
	GetClientRect(hwnd, &rc);

	{
		const SIZE sz = { rc.right - rc.left, rc.bottom - rc.top };

		SCROLLINFO si = {0};
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nPos = si.nMin = 1;

		si.nMax = sz.cx;
		si.nPage = sz.cx;
		SetScrollInfo(hwnd, SB_HORZ, &si, FALSE);

		si.nMax = sz.cy;
		si.nPage = sz.cy;
		SetScrollInfo(hwnd, SB_VERT, &si, FALSE);
	}
    return FALSE;
}

static int s_prevx = 1;
static int s_prevy = 1;

BOOL SD_OnInitDialog_ForceSize(HWND hwnd, int xSize, int ySize)
{
    RECT rc = {0};
	s_prevx = 1;
	s_prevy = 1;

	GetClientRect(hwnd, &rc);

	{
		SCROLLINFO si = {0};
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nPos = si.nMin = 1;

		si.nMax = xSize;
		si.nPage = xSize;
		SetScrollInfo(hwnd, SB_HORZ, &si, FALSE);

		si.nMax = ySize;
		si.nPage = ySize;
		SetScrollInfo(hwnd, SB_VERT, &si, FALSE);
	}
    return FALSE;
}

void SD_OnSize(HWND hwnd, UINT state, int cx, int cy)
{
    SCROLLINFO si = {0};
    const int bar[] = { SB_HORZ, SB_VERT };
    const int page[] = { cx, cy };
	size_t i;

	if(state != SIZE_RESTORED && state != SIZE_MAXIMIZED)
        return;

     si.cbSize = sizeof(SCROLLINFO);

    for(i = 0; i < ARRAYSIZE(bar); ++i)
    {
        si.fMask = SIF_PAGE;
        si.nPage = page[i];
        SetScrollInfo(hwnd, bar[i], &si, TRUE);

        si.fMask = SIF_RANGE | SIF_POS;
        GetScrollInfo(hwnd, bar[i], &si);

		{
			const int maxScrollPos = si.nMax - (page[i] - 1);

			// Scroll client only if scroll bar is visible and window's
			// content is fully scrolled toward right and/or bottom side.
			// Also, update window's content on maximize.
			const bool needToScroll =
				(si.nPos != si.nMin && si.nPos == maxScrollPos) ||
				(state == SIZE_MAXIMIZED);

			if(needToScroll)
			{
				SD_ScrollClient(hwnd, bar[i], si.nPos);
			}
		}
    }
}

void SD_OnHScroll(HWND hwnd, UINT code)
{
    SD_OnHVScroll(hwnd, SB_HORZ, code);
}

void SD_OnVScroll(HWND hwnd, UINT code)
{
    SD_OnHVScroll(hwnd, SB_VERT, code);
}

void SD_OnHVScroll(HWND hwnd, int bar, UINT code)
{
    const int scrollPos = SD_GetScrollPos(hwnd, bar, code);

    if(scrollPos == -1)
        return;

    SetScrollPos(hwnd, bar, scrollPos, TRUE);
    SD_ScrollClient(hwnd, bar, scrollPos);
}

void SD_ScrollClient(HWND hwnd, int bar, int pos)
{


    int cx = 0;
    int cy = 0;

    int *delta = (bar == SB_HORZ ? &cx : &cy);
    int *prev = (bar == SB_HORZ ? &s_prevx : &s_prevy);

    *delta = *prev - pos;
    *prev = pos;

    if(cx || cy)
    {
        ScrollWindow(hwnd, cx, cy, NULL, NULL);
    }
}

int SD_GetScrollPos(HWND hwnd, int bar, UINT code)
{
    SCROLLINFO si = {0};
	int result = -1;
	
	si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_TRACKPOS;
    GetScrollInfo(hwnd, bar, &si);

	{
		const int minPos = si.nMin;
		const int maxPos = si.nMax - (si.nPage - 1);

		

		switch(code)
		{
		case SB_LINEUP /*SB_LINELEFT*/:
			result = max(si.nPos - 1, minPos);
			break;

		case SB_LINEDOWN /*SB_LINERIGHT*/:
			result = min(si.nPos + 1, maxPos);
			break;

		case SB_PAGEUP /*SB_PAGELEFT*/:
			result = max(si.nPos - (int)si.nPage, minPos);
			break;

		case SB_PAGEDOWN /*SB_PAGERIGHT*/:
			result = min(si.nPos + (int)si.nPage, maxPos);
			break;

		case SB_THUMBPOSITION:
			// do nothing
			break;

		case SB_THUMBTRACK:
			result = si.nTrackPos;
			break;

		case SB_TOP /*SB_LEFT*/:
			result = minPos;
			break;

		case SB_BOTTOM /*SB_RIGHT*/:
			result = maxPos;
			break;

		case SB_ENDSCROLL:
			// do nothing
			break;
		}
	}

    return result;
}



void AddDirToPathEnivronmentVariable(const char *pDirName)
{
	char copyDir[CRYPTIC_MAX_PATH];
	char *pCurPath = NULL;

	strcpy(copyDir, pDirName);
	backSlashes(copyDir);

	estrGetEnvironmentVariable(&pCurPath, "PATH");

	estrConcatf(&pCurPath, ";%s", copyDir);
	SetEnvironmentVariable_UTF8("PATH", pCurPath);
	estrDestroy(&pCurPath);

}

// Get the Windows Thread Information Block for the current thread, fs:[0] on x86.
void *GetCurrentThreadTib()
{
	return NtCurrentTeb();
}

// Get a value that bounds all lower frames on the stack.
void *GetBoundingFramePointer()
{
	return *((void **)_AddressOfReturnAddress() - 1);
}


typedef struct SetWindowTitleByPIDCache
{
	U32 iPID;
	char *pText;
} SetWindowTitleByPIDCache;

static BOOL CALLBACK SetWindowTitleByPIDCB(HWND hwnd, LPARAM lParam)
{
	SetWindowTitleByPIDCache *pCache = (SetWindowTitleByPIDCache*)lParam;
	U32 iWindowPID;
	if(GetWindowThreadProcessId(hwnd, &iWindowPID))
	{
		if (iWindowPID == pCache->iPID)
		{
			SetWindowText_UTF8(hwnd, pCache->pText);
		}
	}
	return true;
}

//finds any/all windows associated with a given PID, sets their console titles (may only
//work dependably on Windows 7, and only if the process is one that you yourself created)
void SetWindowTitleByPID(U32 iPID, char *pText)
{
	SetWindowTitleByPIDCache cache = {iPID, pText};
	EnumWindows(SetWindowTitleByPIDCB, (LPARAM)(&cache));
}