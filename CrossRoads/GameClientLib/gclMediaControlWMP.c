#if !PLATFORM_CONSOLE

#include "gclMediaControl.h"
#include "NotifyCommon.h"
#include "GfxConsole.h"

#include "file.h"
#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static HWND g_hwnd = NULL;

static bool createWnd(void)
{
	// Create windows class
	static bool isRegistered = false;
	WNDCLASS wndclass = {0};
	wndclass.lpfnWndProc = DefWindowProc;
	wndclass.lpszClassName = "cryptic-wmp";

	if (isRegistered == false)
	{
		if (RegisterClass(&wndclass) == 0)
		{
			return false;
		}
	}
	isRegistered = true;

	// Create a window.
	if ((g_hwnd = CreateWindow(
		"cryptic-wmp",
		"cryptic-wmp",
		WS_OVERLAPPEDWINDOW | WS_DISABLED,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		NULL,
		NULL)) == NULL)
	{
		return false;
	}

	return true;

}

static void wmpConnect(void)
{
	createWnd();
}

AUTO_RUN;
void gclMediaControlWMPRegister(void)
{
	if(isDevelopmentMode())
		gclMediaControlRegister("WMP", wmpConnect, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

#endif