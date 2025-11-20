
#include "UILabel.h"
#include "UIWindow.h"

#include "GraphicsLib.h"

#include "UIStatusWindow.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_StatusWindowClose(UIStatusWindow *stat_win)
{
	if( stat_win ) {
		UIWindow *window = stat_win->window;
		UI_WIDGET(window)->contextData = NULL;
		ui_WindowHide(window);
		ui_WidgetQueueFree(UI_WIDGET(window));
		free(stat_win);
	}
}

void ui_StatusWindowTick(UIWindow *window, UI_PARENT_ARGS)
{
	UIStatusWindow *stat_win = UI_WIDGET(window)->contextData;
	bool closing_window = false;

	if(!stat_win)
		return;

	if(!stat_win->no_timeout) {
		stat_win->time_left -= gfxGetFrameTime();
		if(stat_win->time_left <= 0) {
			if(stat_win->timeoutF)
				stat_win->timeoutF(stat_win, stat_win->timeoutData);
			stat_win->time_left = 0;
			closing_window = true;
		}
	}

	if(closing_window) {
		ui_StatusWindowClose(stat_win);
		return;
	} else if (stat_win->tickF) {
		stat_win->tickF(stat_win, stat_win->tickData);
	}

	ui_WindowTick(window, UI_PARENT_VALUES);
}

void ui_StatusWindowSetTickFunction(UIStatusWindow *stat_win, UIActivationFunc tickF, UserData tickData)
{
	stat_win->tickF = tickF;
	stat_win->tickData = tickData;
}

UIStatusWindow* ui_StatusWindow(const char *title, const char *message, F32 timeout, UIActivationFunc timeoutF, UserData timeoutData)
{
	UIStatusWindow *stat_win = (UIStatusWindow *)calloc(1, sizeof(UIStatusWindow));
	UIWindow *window;
	UILabel *label;
	int w, h;

	// Init Data
	if(timeout)
		stat_win->time_left = timeout;
	else 
		stat_win->no_timeout = true;
	stat_win->timeoutF = timeoutF;
	stat_win->timeoutData = timeoutData;

	// Create the window
	window = ui_WindowCreate(title, 0, 0, 300, 50);
	UI_WIDGET(window)->tickF = ui_StatusWindowTick;
	UI_WIDGET(window)->contextData = stat_win;
	ui_WindowSetModal(window, true);
	ui_WindowSetClosable(window, false);
	stat_win->window = window;

	// Lay out the message
	label = ui_LabelCreate(message, 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(label), 0, 0, 0, 0, UINoDirection);
	ui_WindowAddChild(window, label);

	// Show the window
	gfxGetActiveDeviceSize(&w, &h);
	ui_WidgetSetPosition((UIWidget*) window, (w / g_ui_State.scale - window->widget.width * window->widget.scale) / 2, (h / g_ui_State.scale - window->widget.height * window->widget.scale) / 2);
	ui_WindowPresent(window);

	return stat_win;
}
