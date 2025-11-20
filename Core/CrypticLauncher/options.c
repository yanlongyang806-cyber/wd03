// CrypicLauncher options dialog functions

#include "options.h"
#include "CrypticLauncher.h"
#include "registry.h"
#include "GameDetails.h"
#include "launcherUtils.h"
#include "resource_CrypticLauncher.h"
#include "systemtray.h"

#include "earray.h"
#include "SimpleWindowManager.h"
#include "Prefs.h"
#include "AppLocale.h"
#include "NewControllerTracker_pub.h"

#include "windef.h"
#include "Windowsx.h"
#include "Commctrl.h"

#define FLAG_X 16
#define FLAG_Y 11

static int flag_id_map[] = {
	IDB_FLAG_US,
	IDB_FLAG_CN,
	IDB_FLAG_KR,
	IDB_FLAG_JP,
	IDB_FLAG_DE,
	IDB_FLAG_FR,
	IDB_FLAG_ES
};
static HBITMAP flags[ARRAY_SIZE_CHECKED(flag_id_map)];

// Allow CORE client to override our locale.
// This is subject to various limitations.  For instance, it will only work if that locale is actually available for that product.
static char *g_core_locale = NULL;
AUTO_COMMAND ACMD_NAME(CoreLocale) ACMD_CMDLINE;
void cmd_CoreLocale(const char *locale)
{
	SAFE_FREE(g_core_locale);
	g_core_locale = strdup(locale);
}

AUTO_RUN;
void LoadFlags(void)
{
	int n = locGetMaxLocaleCount(), i;
	HBITMAP hbmFlag;
	for(i=0; i<n; i++)
	{
		hbmFlag = LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(flag_id_map[i]), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
		flags[i] = hbmFlag;
	}
}

BOOL OptionsPreDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *window)
{
	if(iMsg >= WM_KEYDOWN && iMsg <= WM_KEYLAST &&
	   wParam == VK_RETURN)
	{
		window->pDialogCB(hDlg, WM_COMMAND, IDOK, 0, window);
		return TRUE;
	}
	return FALSE;
}

BOOL OptionsDialogFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *window)
{
	SimpleWindow *main_window;
	CrypticLauncherWindow *launcher;
	ShardInfo_Basic *shard = window->pUserData;
	U32 gameID;

	main_window = SimpleWindowManager_FindWindow(CL_WINDOW_MAIN, 0);
	if(!main_window)
	{
		window->bCloseRequested = true;
		return TRUE;
	}
	launcher = main_window->pUserData;

	if(shard)
		gameID = gdGetIDByName(shard->pProductName);
	else
		gameID = launcher->gameID;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
			HWND handle;
			int valid, regInt, i;
			bool fullscreen, verify;
			
			// If in all-games mode, set the title of the dialogs to match the game we are setting options for.
			if(launcher->allMode && shard)
			{
				const char *dispName = gdGetDisplayName(gameID);
				//nameDetails(shard->pProductName, NULL, &dispName);
				SetWindowText(hDlg, STACK_SPRINTF(FORMAT_OK(_("%s Options")), dispName));
			}

			// Check the fullscreen box if needed
			handle = GetDlgItem(hDlg, IDC_FULLSCREEN);
			if(shard)
			{
				fullscreen = PrefGetInt(shardPrefSet(shard), "GfxSettings.Fullscreen", 1);
				if(fullscreen)
					Button_SetCheck(handle, BST_CHECKED); 
			}
			else
				Button_Enable(handle, FALSE);

			// Check the safe mode box if needed
			if(launcher->useSafeMode)
			{
				handle = GetDlgItem(hDlg, IDC_SAFEMODE);
				Button_SetCheck(handle, BST_CHECKED);
			}

			// Set the command line dialog if needed
			if(launcher->commandLine)
			{
				handle = GetDlgItem(hDlg, IDC_COMMANDLINE);
				Edit_SetText(handle, launcher->commandLine);
			}

			// Set the tray icon boxes if needed
			if(launcher->showTrayIcon)
			{
				handle = GetDlgItem(hDlg, IDC_SHOWTRAY);
				Button_SetCheck(handle, BST_CHECKED);
			}
			if(launcher->minimizeTrayIcon)
			{
				handle = GetDlgItem(hDlg, IDC_MINTRAY);
				Button_SetCheck(handle, BST_CHECKED);
			}

			if(launcher->autoLaunch)
			{
				handle = GetDlgItem(hDlg, IDC_FASTLAUNCH);
				Button_SetCheck(handle, BST_CHECKED);
			}

			// Check the verify box if needed

			handle = GetDlgItem(hDlg, IDC_VERIFY);
			valid = readRegInt(shard?shard->pProductName:NULL, "VerifyOnNextUpdate", &regInt, NULL);
			verify = valid && regInt;
			if(verify)
				Button_SetCheck(handle, BST_CHECKED);

			// Populate the language dropdown
			{
				int n = locGetMaxLocaleCount(), l;
				char name[100];
				
				handle = GetDlgItem(hDlg, IDC_LANGUAGE);
				ComboBox_ResetContent(handle);
				for(i=0; i<n; i++)
				{
					if(gdIsLocValid(gameID, i))
					{
						UTF8ToACP(locGetDisplayName(i), SAFESTR(name));
						l = ComboBox_AddString(handle, name);
						ComboBox_SetItemData(handle, l, i);
					}
				}
				UTF8ToACP(locGetDisplayName(getCurrentLocale()), SAFESTR(name));
				ComboBox_SelectString(handle, 0, name);
				ComboBox_SetText(handle, name);
				if (g_core_locale)
					ComboBox_Enable(handle, FALSE);
			}

			// Select the correct option in the proxy dropdown
			handle = GetDlgItem(hDlg, IDC_PROXY);
			ComboBox_AddString(handle, _("None"));
			ComboBox_AddString(handle, _("US"));
			ComboBox_AddString(handle, _("EU"));
			if(launcher->proxy)
			{
				i = ComboBox_FindStringExact(handle, -1, launcher->proxy);
				if(i == CB_ERR)
					i = 0;
			}
			else
				i = 0;
			ComboBox_SetCurSel(handle, i);

			// Check the show-all-games box if needed
			if(launcher->gameID == 0)
			{	
				Button_SetCheck(GetDlgItem(hDlg, IDC_MULTIGAME), BST_CHECKED);
				Button_Enable(GetDlgItem(hDlg, IDC_MULTIGAME), FALSE);
			}
			else if(launcher->allMode)
				Button_SetCheck(GetDlgItem(hDlg, IDC_MULTIGAME), BST_CHECKED);

			// Check the proxy-patching box if needed
			if(launcher->proxy_patching)
				Button_SetCheck(GetDlgItem(hDlg, IDC_PROXYPATCH), BST_CHECKED);
			
			// Check the micropatching box if needed
			if(launcher->disable_micropatching)
				Button_SetCheck(GetDlgItem(hDlg, IDC_DISABLE_MICROPATCHING), BST_CHECKED);

			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDC_PROXYPATCH:
			switch( HIWORD(wParam) )
			{
			case BN_CLICKED:
				if(Button_GetCheck((HWND)lParam) == BST_CHECKED)
				{
					int ret = MessageBox(NULL, _("Enabling this option may reduce patching speed. Are you sure you want to enable it?"), _("Are you sure?"), MB_YESNO|MB_ICONWARNING);
					if(ret == IDNO)
						Button_SetCheck((HWND)lParam, BST_UNCHECKED);
				}
				
				break;
			}
			break;

		case IDCANCEL:
			// Handler for the red X in the corner
			window->bCloseRequested = true;
			break;

		case IDOK:
			{
				HWND hFullscreen, hSafeMode, hCommandLine, hVerify, handle;
				int len, cursel;
				bool updatePage, old, need_restart_warning = false;
				const char *productName = NULL;
				char *old_proxy;

				if(shard)
					productName = shard->pProductName;

				// Record the fullscreen state.
				if(shard)
				{
					hFullscreen = GetDlgItem(hDlg, IDC_FULLSCREEN);
					PrefStoreInt(shardPrefSet(shard), "GfxSettings.Fullscreen", Button_GetCheck(hFullscreen) == BST_CHECKED ? 1 : 0);
				}

				// Record the safemode setting.
				hSafeMode = GetDlgItem(hDlg, IDC_SAFEMODE);
				launcher->useSafeMode = Button_GetCheck(hSafeMode) == BST_CHECKED;

				// Record the command line setting
				hCommandLine = GetDlgItem(hDlg, IDC_COMMANDLINE);
				len = Edit_GetTextLength(hCommandLine);
				SAFE_FREE(launcher->commandLine);
				launcher->commandLine = malloc(len+1);
				Edit_GetText(hCommandLine, launcher->commandLine, len+1);
				writeRegStr(productName, "LauncherCommandLine", launcher->commandLine);

				// Record the verify state.
				hVerify = GetDlgItem(hDlg, IDC_VERIFY);
				if(Button_GetCheck(hVerify) == BST_CHECKED)
				{
					writeRegInt(productName, "VerifyOnNextUpdate", 2);
					if(!launcher->askVerify && shard)
						SendMessage(launcher->window->hWnd, WM_APP, CLMSG_RESTART_PATCH, (LPARAM)shard);
					launcher->askVerify = true;
					launcher->forceVerify = true;
				}
				else
				{
					writeRegInt(productName, "VerifyOnNextUpdate", 0);
					launcher->askVerify = false;
					launcher->forceVerify = false;
				}

				// Record the tray icon settings.
				launcher->showTrayIcon = Button_GetCheck(GetDlgItem(hDlg, IDC_SHOWTRAY)) == BST_CHECKED;
				writeRegInt(productName, "ShowTrayIcon", launcher->showTrayIcon);
				launcher->minimizeTrayIcon = Button_GetCheck(GetDlgItem(hDlg, IDC_MINTRAY)) == BST_CHECKED;
				writeRegInt(productName, "MinimizeToTray", launcher->minimizeTrayIcon);
				if(launcher->showTrayIcon)
					systemTrayAdd(launcher->window->hWnd);
				else if(!IsWindowVisible(launcher->window->hWnd) || IsIconic(launcher->window->hWnd))
				{
					if(!launcher->minimizeTrayIcon)
					{
						ShowWindow(launcher->window->hWnd, SW_NORMAL);
						systemTrayRemove(launcher->window->hWnd);
					}
				}
				else
					systemTrayRemove(launcher->window->hWnd);

				launcher->autoLaunch = Button_GetCheck(GetDlgItem(hDlg, IDC_FASTLAUNCH)) == BST_CHECKED;
				writeRegInt(productName, "AutoLaunch", launcher->autoLaunch);

				// Record the proxy setting.
				handle = GetDlgItem(hDlg, IDC_PROXY);
				len = ComboBox_GetTextLength(handle);
				old_proxy = launcher->proxy;
				launcher->proxy = malloc(len+1);
				ComboBox_GetText(handle, launcher->proxy, len+1);
				if(stricmp(NULL_TO_EMPTY(old_proxy), launcher->proxy)!=0 && stricmp(launcher->proxy, "US")==0)
				{
					int ret = MessageBox(NULL, _("Using the US proxy server may cause connection stability problems. Are you sure you want to enable it?"), _("Are you sure?"), MB_YESNO|MB_ICONWARNING);
					if(ret == IDNO)
					{
						SAFE_FREE(launcher->proxy);
						launcher->proxy = strdup(NULL_TO_EMPTY(old_proxy));
					}
				}
				writeRegStr(productName, "Proxy", launcher->proxy);
				SAFE_FREE(old_proxy);

				// Record the show-all-games setting.
				old = launcher->allMode;
				launcher->allMode = Button_GetCheck(GetDlgItem(hDlg, IDC_MULTIGAME)) == BST_CHECKED;
				writeRegInt(productName, "ShowAllGames", launcher->allMode);
				if(old != launcher->allMode)
					need_restart_warning = true;

				// Record the proxy-patching setting.
				old = launcher->proxy_patching;
				launcher->proxy_patching = Button_GetCheck(GetDlgItem(hDlg, IDC_PROXYPATCH)) == BST_CHECKED;
				writeRegInt(productName, "ProxyPatching", launcher->proxy_patching);
				if(old != launcher->proxy_patching)
					need_restart_warning = true;
				
				// Record the disable micropatching setting.
				old = launcher->disable_micropatching;
				launcher->disable_micropatching = Button_GetCheck(GetDlgItem(hDlg, IDC_DISABLE_MICROPATCHING)) == BST_CHECKED;
				writeRegInt(productName, "DisableMicropatching", launcher->disable_micropatching);
				// Don't show the warning dialog if you change the setting from the login page.
				if(old != launcher->disable_micropatching && launcher->state > CL_STATE_LOGINPAGELOADED)
					need_restart_warning = true;

				// Display the restart warning if needed
				if(need_restart_warning)
					MessageBox(NULL, _("You must restart the launcher for this change to take effect."), _("Restart required"), MB_OK|MB_ICONINFORMATION);

				// Record and activate the language
				handle = GetDlgItem(hDlg, IDC_LANGUAGE);
				cursel = ComboBox_GetCurSel(handle);
				if(cursel == CB_ERR)
					cursel = 0;
				else
					cursel = ComboBox_GetItemData(handle, cursel);
				updatePage = (cursel != getCurrentLocale());
				setLauncherLocale(cursel);
				writeRegStr(productName, "InstallLanguage", STACK_SPRINTF("%u", locGetWindowsLocale(cursel)));
				if(updatePage)
					SendMessage(launcher->window->hWnd, WM_APP, CLMSG_RELOAD_PAGE, 0);

				// Close the dialog
				window->bCloseRequested = true;
			}
		}
		break;
	case WM_MEASUREITEM: 
		{
			LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT) lParam; 

			if (lpmis->itemHeight < FLAG_Y + 2) 
				lpmis->itemHeight = FLAG_Y + 2; 
		}
		break; 
	case WM_DRAWITEM: 
		{
			LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT) lParam; 
			COLORREF clrBackground, clrForeground; 
			TEXTMETRIC tm; 
			HDC hdc; 
			int x, y, len;
			char *name;

			if (lpdis->itemID == -1)            // empty item 
				break; 

			// Determine the bitmaps used to draw the icon. 



			// The colors depend on whether the item is selected. 
			clrForeground = SetTextColor(lpdis->hDC, GetSysColor(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)); 
			clrBackground = SetBkColor(lpdis->hDC, GetSysColor(lpdis->itemState & ODS_SELECTED ? COLOR_HIGHLIGHT : COLOR_WINDOW)); 

			// Calculate the vertical and horizontal position. 

			GetTextMetrics(lpdis->hDC, &tm); 
			y = (lpdis->rcItem.bottom + lpdis->rcItem.top - 
				tm.tmHeight) / 2; 
			x = LOWORD(GetDialogBaseUnits()) / 4; 

			// Get and display the text for the list item. 
			len = ComboBox_GetLBTextLen(lpdis->hwndItem, lpdis->itemID);
			assert(len);
			name = malloc(len+1);
			ComboBox_GetLBText(lpdis->hwndItem, lpdis->itemID, name);
			assert(name && name[0]);

			ExtTextOut(lpdis->hDC, FLAG_X + 2 * x, y, 
				ETO_CLIPPED | ETO_OPAQUE, &lpdis->rcItem, 
				name, len, NULL); 

			// Restore the previous colors. 

			SetTextColor(lpdis->hDC, clrForeground); 
			SetBkColor(lpdis->hDC, clrBackground); 

			// Show the icon. 
			hdc = CreateCompatibleDC(lpdis->hDC); 
			assert(hdc);

			SelectObject(hdc, flags[lpdis->itemData]); 
			y = (lpdis->rcItem.bottom + lpdis->rcItem.top - FLAG_Y) / 2; 
			//BitBlt(lpdis->hDC, x, y, 
			//	FLAG_X, FLAG_Y, hdc, 0, 0, SRCAND); 
			//BitBlt(lpdis->hDC, x, y, 
			//	FLAG_X, FLAG_Y, hdc, 0, 0, SRCPAINT); 
			BitBlt(lpdis->hDC, x, y, 
				FLAG_X, FLAG_Y, hdc, 0, 0, SRCCOPY); 

			DeleteDC(hdc); 

			// If the item has the focus, draw focus rectangle. 

			if (lpdis->itemState & ODS_FOCUS) 
				DrawFocusRect(lpdis->hDC, &lpdis->rcItem); 

			free(name);
		}
		break; 
	}

	return FALSE;
}


bool OptionsTickFunc(SimpleWindow *pWindow)
{
	return true;
}


void loadRegistrySettings(CrypticLauncherWindow *launcher, const char *productName)
{
	char buf[1024]={0}, *tmp;
	U32 i;
	bool updatePage;

	// Load the language from the registry.
	if(readRegStr(productName, "InstallLanguage", SAFESTR(buf), launcher->history))
	{
		i = strtol(buf, &tmp, 10);
		if(buf == tmp)
			i = 1033;
	}
	else
		i = 1033;
	i = locGetIDByWindowsLocale(i);

	// Override with the CORE locale, if specified.
	if (g_core_locale)
		i = locGetIDByName(g_core_locale);

	// Check if this locale is allowed for this game, if not default to English
	if(!gdIsLocValid(productName?gdGetIDByExecutable(productName):launcher->gameID, i))
		i = locGetIDByLanguage(LANGUAGE_ENGLISH);
	updatePage = (i != (U32)getCurrentLocale());
	
	setLauncherLocale(i);
	// If the language is changing, update the page
	if(updatePage && launcher->window)
		SendMessage(launcher->window->hWnd, WM_APP, CLMSG_RELOAD_PAGE, 0);

	// Load tray icon options
	if(!readRegInt(productName, "ShowTrayIcon", &i, launcher->history))
		i = 1;
	launcher->showTrayIcon = i;
	if(!readRegInt(productName, "MinimizeToTray", &i, launcher->history))
		i = 0;
	launcher->minimizeTrayIcon = i;

	if(!readRegInt(productName, "AutoLaunch", &i, launcher->history))
		i = 0;
	launcher->autoLaunch = i;

	// Update the running systray params to match the active settings
	if(launcher->window)
	{
		if(launcher->showTrayIcon)
			systemTrayAdd(launcher->window->hWnd);
		else if(!IsWindowVisible(launcher->window->hWnd) || IsIconic(launcher->window->hWnd))
		{
			if(!launcher->minimizeTrayIcon)
			{
				ShowWindow(launcher->window->hWnd, SW_NORMAL);
				systemTrayRemove(launcher->window->hWnd);
			}
		}
		else
			systemTrayRemove(launcher->window->hWnd);
	}

	// Load the gameclient command line options used last time
	SAFE_FREE(launcher->commandLine);
	if(readRegStr(productName, "LauncherCommandLine", SAFESTR(buf), launcher->history))
		launcher->commandLine = strdup(buf);

	// Check if we need to verify
	if(readRegInt(productName, "VerifyOnNextUpdate", &i, NULL))
	{
		launcher->askVerify = i > 0;
		launcher->forceVerify = i > 1;
	}

	// Load the proxy config
	SAFE_FREE(launcher->proxy);
	if(readRegStr(productName, "Proxy", SAFESTR(buf), launcher->history))
		launcher->proxy = strdup(buf);

	// Load the all-games option
	if(!readRegInt(productName, "ShowAllGames", &i, launcher->history))
		i = 0;
	launcher->allMode = i;

	// Load the all-games option
	if(!readRegInt(productName, "ProxyPatching", &i, launcher->history))
		i = 0;
	launcher->proxy_patching = i;

	// Load the disable micropatching option
	if(!readRegInt(productName, "DisableMicropatching", &i, launcher->history))
		i = 0;
	launcher->disable_micropatching = i;
}