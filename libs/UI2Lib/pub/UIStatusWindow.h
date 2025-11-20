/***************************************************************************



***************************************************************************/

#ifndef UI_STATUSWINDOW_H
#define UI_STATUSWINDOW_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UIWindow UIWindow;

typedef struct UIStatusWindow
{
	UIWindow *window; //Must be first
	F32 time_left;
	bool no_timeout;

	UIActivationFunc tickF;
	UserData tickData;

	UIActivationFunc timeoutF;
	UserData timeoutData;

} UIStatusWindow;

UIStatusWindow* ui_StatusWindow(const char *title, const char *message, F32 timeout, UIActivationFunc timeoutF, UserData timeoutData);
void ui_StatusWindowClose(UIStatusWindow *stat_win);
void ui_StatusWindowSetTickFunction(UIStatusWindow *stat_win, UIActivationFunc tickF, UserData tickData);

#endif // UI_STATUSWINDOW_H

