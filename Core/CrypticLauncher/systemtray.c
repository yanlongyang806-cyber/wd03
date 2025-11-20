#include "systemtray.h"
#include "CrypticLauncher.h"
#include "resource_CrypticLauncher.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0500 // WinNT 4?  VS2010 gets this implicitly from _WIN32_WINNT and will use 0x0600 (WinXP).
#endif
#include "wininclude.h"

void systemTrayAdd(HWND hParent)
{
	NOTIFYICONDATA nid = {0};

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd   = hParent;
	nid.uID    = 0;
	nid.hIcon  = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_LAUNCHER));
	nid.uCallbackMessage = WM_APP_TRAYICON;
	nid.uFlags = NIF_ICON | NIF_MESSAGE;
	nid.uTimeout = 10 * 1000; // It sounds like some Windows versions just ignore this anyway

	Shell_NotifyIcon(NIM_ADD, &nid);
}

void systemTrayRemove(HWND hParent)
{
	NOTIFYICONDATA nid = {0};

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd   = hParent;
	nid.uID    = 0;

	Shell_NotifyIcon(NIM_DELETE, &nid);
}