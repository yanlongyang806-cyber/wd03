#include "EditLibUIUtil.h"
#include "earray.h"
#include "EString.h"
#include "GraphicsLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


/********************
* GENERIC CALLBACKS
********************/
/******
* This is a generic callback that can be used for list columns with text callbacks.  This function
* assumes the list's model is an EArray of strings.
* PARAMS:
*   list - UIList being displayed
*   column - UIListColumn being displayed
*   row - int row being displayed
*   userData - UserData set for the column
*   output - EString handle to which the text should be copied
******/
void elUIListTextDisplay(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrPrintf(output, "%s", (char*)(*(list->peaModel))[row]);
}

/******
* This is a generic window closing callback; make sure to set the callback data to the window pointer.
* PARAMS:
*   thing - not used; fulfills UICloseFunc requirements
*   window - UIWindow to close
******/
bool elUIWindowClose(UIWidget *thing, UIWindow *window)
{
	ui_WindowHide(window);
	ui_WidgetQueueFree((UIWidget*) window);
	return true;
}


/********************
* USEFUL FUNCTIONS
********************/
/******
* This function centers the specified window in the viewport.
* PARAMS:
*   window - UIWindow to center
******/
void elUICenterWindow(UIWindow *window)
{
	int w, h;

	gfxGetActiveDeviceSize(&w, &h);
	ui_WidgetSetPosition((UIWidget*) window, (w / g_ui_State.scale - window->widget.width * window->widget.scale) / 2, (h / g_ui_State.scale - window->widget.height * window->widget.scale) / 2);
}

/******
* This function gets the x-coordinate corresponding to the rightmost corner of all UIUnitFixed width
* widgets in the specified widget group.
* PARAMS:
*   group - UIWidgetGroup to scan
* RETURNS:
*   x-coordinate belonging to rightmost edge of all UIUnitFixed width widgets in group
******/
int elUIGetEndX(UIWidgetGroup group)
{
	int i, x = 0;
	for (i = 0; i < eaSize(&group); i++)
	{
		UIWidget *widget = group[i];
		if (widget->widthUnit == UIUnitFixed)
			x = MAX(x, widget->x + widget->width);
	}
	return x;
}

/******
* This function gets the y-coordinate corresponding to the bottommost corner of all UIUnitFixed
* height widgets in the specified widget group.
* PARAMS:
*   group - UIWidgetGroup to scan
* RETURNS:
*   y-coordinate belonging to bottommost edge of all UIUnitFixed width widgets in group
******/
int elUIGetEndY(UIWidgetGroup group)
{
	int i, y = 0;
	for (i = 0; i < eaSize(&group); i++)
	{
		UIWidget *widget = group[i];
		if (widget->heightUnit == UIUnitFixed)
			y = MAX(y, widget->y + widget->height);
	}
	return y;
}

/******
* This function is used to create OK and Cancel buttons on a window; if the specified cancel callback and
* data are both NULL, a default cancel callback is used.
* PARAMS:
*   window - the UIWindow on which to add the buttons
*   cancelF - the cancel button callback; also used as the window close callback
*   cancelData - the cancel callback data
*   okF - the OK button callback
*   okData - the OK button callback data
******/
UIButton *elUIAddCancelOkButtons(UIWindow *window, UIActivationFunc cancelF, UserData cancelData, UIActivationFunc okF, UserData okData)
{
	UIButton *cancel;
	UIButton *ok;
	
	cancel = ui_ButtonCreate("Cancel", 5, 5, cancelF, cancelData);
	cancel->widget.width = 80;
	ok = ui_ButtonCreate("OK", elUINextX(cancel) + 5, cancel->widget.y, okF, okData);
	ok->widget.width = 80;
	if (!cancelF && !cancelData)
		ui_ButtonSetCallback(cancel, elUIWindowClose, window);
	cancel->widget.offsetFrom = ok->widget.offsetFrom = UIBottomRight;
	ui_WindowAddChild(window, cancel);
	ui_WindowAddChild(window, ok);
	return ok;
}

/******
* This function opens a tree all the way down to its leaves.
* PARAMS:
*   root - UITreeNode where to begin expanding
******/
void elUITreeExpandAll(UITreeNode *root)
{
	int i;
	ui_TreeNodeExpand(root);
	for (i = 0; i < eaSize(&root->children); i++)
		elUITreeExpandAll(root->children[i]);
}

/******
* This function takes a root tree node and ensures that the node beneath the root
* whose contents match the search pointer is visible (i.e. its parent is open).
* PARAMS:
*   root - UITreeNode root from where to start the search
*   searchContents - pointer to match with the target node's contents pointer
* RETURNS:
*   bool indicating whether a matching node was found
******/
bool elUITreeExpandToNode(UITreeNode *root, UserData searchContents)
{
	if (root->contents == searchContents)
	{
		root->tree->selected = root;
		return true;
	}
	else
	{
		int i;
		bool opened = root->open;
		bool found = false;

		if (!opened)
			ui_TreeNodeExpand(root);

		for (i = 0; i < eaSize(&root->children) && !found; i++)
			found = elUITreeExpandToNode(root->children[i], searchContents);

		if (!found && !opened)
			ui_TreeNodeCollapse(root);

		return found;
	}
}

typedef struct ElUIWaitDialogWin
{
	UIWindow *win;
	ElUICheckFunc checkFunc;
	UserData *checkData;
	ElUIConfirmFunc execFunc;
	UserData *execData;
	ElUIConfirmFunc timeoutFunc;
	UserData *timeoutData;
	int timeout;
} ElUIWaitDialogWin;

static bool elUIWaitDialogWinClose(UIWindow *win, UserData *unused)
{
	return false;
}

static void elUIWaitDialogWinTick(UIWindow *win, UI_PARENT_ARGS)
{
	ElUIWaitDialogWin *waitUI = (ElUIWaitDialogWin*) win->closeData;

	if (waitUI->checkFunc(waitUI->checkData))
	{
		if (waitUI->execFunc)
			waitUI->execFunc(waitUI->execData);
		elUIWindowClose(NULL, waitUI->win);
		free(waitUI);
	}
	else
	{
		if (waitUI->timeout > 0)
			waitUI->timeout--;
		if (waitUI->timeout == 0)
		{
			if (waitUI->timeoutFunc)
				waitUI->timeoutFunc(waitUI->timeoutData);
			elUIWindowClose(NULL, waitUI->win);
			free(waitUI);
		}
	}
	ui_WindowTick(win, pX, pY, pW, pH, pScale);
}

/******
* This function creates a modal dialog window that remains active until a
* particular check function returns true.  Once the check function is true,
* another specified function is called.  This is generally useful for indicating
* that the user must wait until a particular event has occurred (like loading data
* from server).
* PARAMS:
*   title - string title of the wait dialog window
*   msg - string message to display in the dialog
*   timeout - int number of frames to wait before timing out and executing the timeout function; -1 indicates no timeout
*   checkFunc - ElUICheckFunc called every frame until checkFunc returns true
*   checkData - UserData passed to checkFunc
*   execFunc - ElUIConfirmFunc called when checkFunc returns true
*   execData - UserData passed to execFunc
******/
void elUIWaitDialog(const char *title, SA_PARAM_NN_STR const char *msg, int timeout,
					ElUICheckFunc checkFunc, UserData checkData,
					ElUIConfirmFunc execFunc, UserData execData,
					ElUIConfirmFunc timeoutFunc, UserData timeoutData)
{
	UIWindow *win = ui_WindowCreate(title, 0, 0, 0, 0);
	UILabel *label = NULL;
	char labelText[1024];
	size_t span;
	ElUIWaitDialogWin *waitUI = calloc(1, sizeof(ElUIWaitDialogWin));

	waitUI->win = win;
	waitUI->checkFunc = checkFunc;
	waitUI->checkData = checkData;
	waitUI->execFunc = execFunc;
	waitUI->execData = execData;
	waitUI->timeoutFunc = timeoutFunc;
	waitUI->timeoutData = timeoutData;
	waitUI->timeout = timeout;

	do
	{
		span = strcspn(msg, "\n");
		memset(labelText, 0, ARRAY_SIZE_CHECKED(labelText));
		strncpy(labelText, msg, span);
		label = ui_LabelCreate(labelText, 5, label ? elUINextY(label) : 5);
		ui_WindowAddChild(win, label);
		win->widget.width = MAX(win->widget.width, elUINextX(label) + 5);
		msg += span;
		if (msg[0] != '\0')
			msg++;
	} while (msg[0] != '\0');

	win->widget.height = (label ? elUINextY(label) : 0) + 5;
	win->widget.tickF = elUIWaitDialogWinTick;
	ui_WindowSetCloseCallback(win, elUIWaitDialogWinClose, waitUI);
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}

typedef struct ElUIProgressDialogWin
{
	UIWindow *win;
	UIProgressBar *bar;
	int count, timeout;
	ElUIProgressFunc progFunc;
	UserData *progData;
	ElUIConfirmFunc execFunc;
	UserData *execData;
} ElUIProgressDialogWin;

static bool elUIProgressDialogWinClose(UIWindow *win, UserData *unused)
{
	return false;
}

static void elUIProgressDialogWinTick(UIWindow *win, UI_PARENT_ARGS)
{
	ElUIProgressDialogWin *progressUI = (ElUIProgressDialogWin*) win->closeData;
	float progVal = progressUI->progFunc(progressUI->progData);

	progVal = CLAMP(progVal, 0.0f, 1.0f);
	ui_ProgressBarSet(progressUI->bar, progVal);
	if (progVal == 1.0f)
	{
		progressUI->execFunc(progressUI->execData);
		elUIWindowClose(NULL, progressUI->win);
		free(progressUI);
		return;
	}

	progressUI->count++;
	if (progressUI->count >= progressUI->timeout && progressUI->timeout > 0)
	{
		elUIWindowClose(NULL, progressUI->win);
		free(progressUI);
	}
	else
		ui_WindowTick(win, pX, pY, pW, pH, pScale);
}

/******
* This function creates a modal dialog window that remains active until a
* particular percentage check function returns 1.  Once the check function has returned 1,
* another specified function is called.  In the meantime, a progress bar in the dialog
* window fills up, the amount being determined by the value of the check function.
* PARAMS:
*   title - string title of the wait dialog window
*   msg - string message to display in the dialog
*   progFunc - ElUIProgressFunc called every frame until progFunc returns a value >= 1
*   progData - UserData passed to checkFunc
*   execFunc - ElUIConfirmFunc called when checkFunc returns true
*   execData - UserData passed to execFunc
*   timeout - number of frames to wait before closing the progress dialog without executing
*             execFunc
******/
void elUIProgressDialog(const char *title, const char *msg,
						ElUIProgressFunc progFunc, UserData progData,
						ElUIConfirmFunc execFunc, UserData execData,
						int timeout)
{
	UIWindow *win = ui_WindowCreate(title, 0, 0, 0, 0);
	UILabel *label = NULL;
	char labelText[1024];
	size_t span;
	ElUIProgressDialogWin *progressUI = calloc(1, sizeof(*progressUI));

	progressUI->win = win;
	progressUI->progFunc = progFunc;
	progressUI->progData = progData;
	progressUI->execFunc = execFunc;
	progressUI->execData = execData;
	progressUI->count = 0;
	progressUI->timeout = timeout;

	do
	{
		span = strcspn(msg, "\n");
		memset(labelText, 0, ARRAY_SIZE_CHECKED(labelText));
		strncpy(labelText, msg, span);
		label = ui_LabelCreate(labelText, 5, label ? elUINextY(label) : 5);
		ui_WindowAddChild(win, label);
		win->widget.width = MAX(win->widget.width, elUINextX(label) + 5);
		msg += span;
		if (msg[0] != '\0')
			msg++;
	} while (msg[0] != '\0');

	progressUI->bar = ui_ProgressBarCreate(0, (label ? elUINextY(label) : 0) + 5, 1);
	progressUI->bar->widget.widthUnit = UIUnitPercentage;
	ui_WindowAddChild(win, progressUI->bar);

	win->widget.height = elUINextY(progressUI->bar) + 5;
	win->widget.tickF = elUIProgressDialogWinTick;
	ui_WindowSetCloseCallback(win, elUIProgressDialogWinClose, progressUI);
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}
