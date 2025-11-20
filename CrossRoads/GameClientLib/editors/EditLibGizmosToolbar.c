GCC_SYSTEM
#ifndef NO_EDITORS

#include "EditLibGizmosToolbar.h"
#include "EditLibGizmos.h"
#include "EditLibUIUtil.h"
#include "EditorObject.h"
#include "Color.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/******
* FORWARD DECLARATIONS
******/
typedef struct EditLibGizmosDisplay EditLibGizmosDisplay;

ParseTable parse_EditLibGizmosDisplay[];

/******
* DEFINITIONS
******/
typedef void (*EditLibGizmosDisplayUpdateFunc)(EditLibGizmosDisplay*);

/******
* Each gizmo type added to the toolbar should build its display.  Typically, each type will subclass this
* display to keep track of other necessary type-specific info.  These display are added to the toolbar and
* are selectively shown/hidden when the associated gizmo is selected.  The updateFunc is generally used to
* refresh the widgets.
******/
#endif
AUTO_STRUCT;
typedef struct EditLibGizmosDisplay
{
	char *name;

#ifndef NO_EDITORS
	UIWidget **widgets;								NO_AST
	void *gizmo;									NO_AST
	EditLibGizmosDisplayUpdateFunc updateFunc;		NO_AST
#endif
} EditLibGizmosDisplay;
#ifndef NO_EDITORS

/******
* Toolbars consist of multiple displays, each with an associated gizmo and UI.  The selectFunc callback
* is called whenever a gizmo is selected from the toolbar's selector combo box.
******/
typedef struct EditLibGizmosToolbar
{
	UIScrollArea *toolbar, *displayArea;
	UIComboBox *selector;
	int height;
	EditLibGizmosDisplay **displays;
	EditLibGizmosToolbarCallback selectFunc;
} EditLibGizmosToolbar;

/******
* TOOLBAR
******/
// Toolbar selector changed callback
static void elGizmosToolbarSelectorChanged(UIComboBox *selector, EditLibGizmosToolbar *tb)
{
	EditLibGizmosDisplay *display = ui_ComboBoxGetSelectedObject(selector);
	int i;

	if (display)
	{
		// hide old display widgets
		for (i = eaSize(&tb->displayArea->widget.children) - 1; i >= 0; i--)
			ui_ScrollAreaRemoveChild(tb->displayArea, tb->displayArea->widget.children[i]);

		// show new display widgets
		for (i = 0; i < eaSize(&display->widgets); i++)
			ui_ScrollAreaAddChild(tb->displayArea, display->widgets[i]);

		// update the new display
		if (display->updateFunc)
			display->updateFunc(display);

		// call any user-specified callback for changing of the active gizmo
		if (tb->selectFunc)
			tb->selectFunc(display->gizmo);
	}
}

/******
* This creates a new toolbar.
* PARAMS:
*   selectFunc - EditLibGizmosToolbarCallback called when the gizmo selector changes
*   height - int height of the toolbar widgets
* RETURNS:
*   EditLibGizmosToolbar created
******/
EditLibGizmosToolbar *elGizmosToolbarCreate(EditLibGizmosToolbarCallback selectFunc, int height)
{
	EditLibGizmosToolbar *tb = calloc(1, sizeof(EditLibGizmosToolbar));
	tb->selectFunc = selectFunc;
	tb->height = height;
	tb->toolbar = ui_ScrollAreaCreate(0, 0, 0, height, 0, 0, false, false);
	tb->selector = ui_ComboBoxCreate(0, 0, 80, parse_EditLibGizmosDisplay, &tb->displays, "name");
	tb->selector->widget.height = height;
	ui_ComboBoxSetSelectedCallback(tb->selector, elGizmosToolbarSelectorChanged, tb);
	ui_ScrollAreaAddChild(tb->toolbar, (UIWidget*) tb->selector);
	tb->displayArea = ui_ScrollAreaCreate(elUINextX(tb->selector) + 5, 0, 0, height, 0, 0, false, false);
	ui_ScrollAreaAddChild(tb->toolbar, (UIWidget*) tb->displayArea);
	ui_WidgetSetDimensions((UIWidget*) tb->toolbar, elUINextX(tb->selector), height);
	return tb;
}

/******
* This function gets the toolbar's encapsulating scroll area widget.
* PARAMS:
*   tb - EditLibGizmosToolbar whose widget is to be retrieved
* RETURNS:
*   UIScrollArea encapsulating specified toolbar's elements
******/
UIScrollArea *elGizmosToolbarGetWidget(EditLibGizmosToolbar *tb)
{
	return tb->toolbar;
}

/******
* This function essentially refreshes the toolbar's various displays.
* PARAMS:
*   tb - EditLibGizmosToolbar to refresh
******/
void elGizmosToolbarUpdate(EditLibGizmosToolbar *tb)
{
	int i;
	for (i = 0; i < eaSize(&tb->displays); i++)
	{
		if (tb->displays[i]->updateFunc)
			tb->displays[i]->updateFunc(tb->displays[i]);
	}
}

/******
* This can be used to set the toolbar selector to a particular gizmo.
* PARAMS:
*   tb - EditLibGizmosToolbar toolbar to set
*   gizmo - the gizmo to set the toolbar to
******/
void elGizmosToolbarSetActiveGizmo(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb, SA_PARAM_NN_VALID void *gizmo)
{
	int i;
	EditLibGizmosDisplay *currDisp = ui_ComboBoxGetSelectedObject(tb->selector);

	if (currDisp && currDisp->gizmo == gizmo)
		return;
	
	for (i = 0; i < eaSize(&tb->displays); i++)
	{
		if (tb->displays[i]->gizmo == gizmo)
			ui_ComboBoxSetSelectedObjectAndCallback(tb->selector, tb->displays[i]);
	}
}

/******
* UTIL FUNCTIONS
******/
/******
* This should be used for all display types to appropriately set a toolbar's width such that it encloses
* the display in its entirety.
* PARAMS:
*   tb - EditLibGizmosToolbar toolbar whose width will be modified
*   width - the width of the display; if this is smaller than the toolbar's current width, then nothing
*           will change
******/
static void elGizmosToolbarSetDisplayWidth(EditLibGizmosToolbar *tb, int width)
{
	if (tb->displayArea->widget.width < width)
	{
		tb->displayArea->widget.width = width;
		tb->toolbar->widget.width = elUINextX(tb->displayArea);
	}
}

/******
* This function does a few common things when a display is added.
* PARAMS:
*   tb - EditLibGizmosToolbar toolbar where the display is being added
*   display - EditLibGizmosDisplay being added
*   width - int width of the display
******/
static void elGizmosToolbarAddDisplay(EditLibGizmosToolbar *tb, EditLibGizmosDisplay *display, int width)
{
	eaPush(&tb->displays, display);
	if (eaSize(&tb->displays) == 1)
		ui_ComboBoxSetSelectedAndCallback(tb->selector, 0);
	elGizmosToolbarSetDisplayWidth(tb, width);
	elGizmosToolbarUpdate(tb);
}

/******
* This does common things when a display is removed from the toolbar.
* PARAMS:
*   tb - EditLibGizmosToolbar toolbar where the display is being removed
*   display - EditLibGizmosDisplay being removed
******/
static void elGizmosDisplayRemove(EditLibGizmosToolbar *tb, EditLibGizmosDisplay *display)
{
	int i;

	eaFindAndRemove(&tb->displays, display);
	if (ui_ComboBoxGetSelected(tb->selector) >= eaSize(&tb->displays))
		ui_ComboBoxSetSelectedAndCallback(tb->selector, eaSize(&tb->displays) - 1);
	for (i = 0; i < eaSize(&display->widgets); i++)
		ui_WidgetQueueFree(display->widgets[i]);
	free(display->name);
}

/******
* TRANSLATE GIZMO DISPLAY
******/
typedef struct EditLibGizmosTranslateDisplay
{
	EditLibGizmosDisplay display;

	// skins
	UISkin *blue, *green, *red;

	// widgets
	UIButton *axisX, *axisY, *axisZ;
	UIButton *worldPivot;
	UIButton *snapEnabled;
	UIComboBox *snapMode;
	UIScrollArea *snapArea;
	UIComboBox *snapWidth;
	UIButton *snapNormal;
	UIComboBox *snapNormalAxis;
	UIButton *snapClamp;

	// callback
	EditLibGizmosToolbarCallback changedFunc;
} EditLibGizmosTranslateDisplay;

// update callback
static void elGizmosToolbarTransUpdate(EditLibGizmosDisplay *display)
{
	EditLibGizmosTranslateDisplay *transUI = (EditLibGizmosTranslateDisplay*) display;
	bool worldPivot = TranslateGizmoGetAlignedToWorld(transUI->display.gizmo);
	bool snapEnabled = TranslateGizmoIsSnapEnabled(transUI->display.gizmo);
	EditSpecialSnapMode snapMode = TranslateGizmoGetSpecSnap(transUI->display.gizmo);
	bool snapNormal = TranslateGizmoGetSnapNormal(transUI->display.gizmo);
	bool snapInverse = TranslateGizmoGetSnapNormalInverse(transUI->display.gizmo);
	int snapNormalAxis = TranslateGizmoGetSnapNormalAxis(transUI->display.gizmo);
	bool snapClamp = TranslateGizmoGetSnapClamp(transUI->display.gizmo);
	bool x, y, z;

	// update axes
	TranslateGizmoGetAxesEnabled(transUI->display.gizmo, &x, &y, &z);
	if (x)
		ui_WidgetSkin((UIWidget*) transUI->axisX, transUI->red);
	else
		ui_WidgetSkin((UIWidget*) transUI->axisX, NULL);
	if (y)
		ui_WidgetSkin((UIWidget*) transUI->axisY, transUI->green);
	else
		ui_WidgetSkin((UIWidget*) transUI->axisY, NULL);
	if (z)
		ui_WidgetSkin((UIWidget*) transUI->axisZ, transUI->blue);
	else
		ui_WidgetSkin((UIWidget*) transUI->axisZ, NULL);

	// update world aligned
	ui_ButtonSetImage( transUI->worldPivot, (worldPivot ? "eui_icon_worldpivot_ON" : "eui_icon_worldpivot_OFF"));

	// update snapping enabled and snap mode
	ui_ButtonSetImage( transUI->snapEnabled, (snapEnabled ? "eui_icon_snapping_ON" : "eui_icon_snapping_OFF") );
	ui_SetActive((UIWidget*) transUI->snapMode, snapEnabled);
	if (snapMode != EditSnapNone)
		ui_ComboBoxSetSelected(transUI->snapMode, snapMode);
	else if (ui_ComboBoxGetSelected(transUI->snapMode) == -1)
		ui_ComboBoxSetSelected(transUI->snapMode, EditSnapGrid);

	// render the applicable widgets
	ui_ScrollAreaRemoveChild(transUI->snapArea, (UIWidget*) transUI->snapWidth);
	ui_ScrollAreaRemoveChild(transUI->snapArea, (UIWidget*) transUI->snapNormal);
	ui_ScrollAreaRemoveChild(transUI->snapArea, (UIWidget*) transUI->snapNormalAxis);
	ui_ScrollAreaRemoveChild(transUI->snapArea, (UIWidget*) transUI->snapClamp);
	if (snapMode == EditSnapNone || snapMode == EditSnapGrid)
		ui_ScrollAreaAddChild(transUI->snapArea, (UIWidget*) transUI->snapWidth);
	else
	{
		ui_ScrollAreaAddChild(transUI->snapArea, (UIWidget*) transUI->snapNormal);
		ui_ScrollAreaAddChild(transUI->snapArea, (UIWidget*) transUI->snapNormalAxis);
		ui_ScrollAreaAddChild(transUI->snapArea, (UIWidget*) transUI->snapClamp);
	}

	// update snap width
	ui_ComboBoxSetSelected(transUI->snapWidth, TranslateGizmoGetSnapResolution(transUI->display.gizmo) - 1);

	// update snap normal and axis
	ui_ButtonSetImage( transUI->snapNormal, (snapNormal ? "eui_icon_snap_normal_ON" : "eui_icon_snap_normal_OFF") );
	if (snapInverse)
		snapNormalAxis += 3;
	ui_ComboBoxSetSelected(transUI->snapNormalAxis, snapNormalAxis);
	ui_SetActive((UIWidget*) transUI->snapNormalAxis, snapNormal);

	// update snap clamp
	ui_ButtonSetImage( transUI->snapClamp, (snapClamp ? "eui_icon_snapclamping_ON" : "eui_icon_snapclamping_OFF") );
	ui_SetActive((UIWidget*) transUI->snapClamp, !(snapMode == EditSnapNone || snapMode == EditSnapGrid));
}

// axis callbacks
static void elGizmosToolbarTransAxisX(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	EditSpecialSnapMode mode = TranslateGizmoGetSpecSnap(transUI->display.gizmo);

	if (mode == EditSnapNone || mode == EditSnapGrid || TranslateGizmoIsSnapAlongAxes(transUI->display.gizmo))
	{
		bool x, y, z;
		TranslateGizmoGetAxesEnabled(transUI->display.gizmo, &x, &y, &z);
		if (TranslateGizmoIsSnapAlongAxes(transUI->display.gizmo) && x && !y && !z)
			TranslateGizmoDisableAxes(transUI->display.gizmo);
		else
			TranslateGizmoToggleAxes(transUI->display.gizmo, true, false, false);
	}
	else
		TranslateGizmoSetAxes(transUI->display.gizmo, true, false, false);
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}
static void elGizmosToolbarTransAxisY(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	EditSpecialSnapMode mode = TranslateGizmoGetSpecSnap(transUI->display.gizmo);
	if (mode == EditSnapNone || mode == EditSnapGrid || TranslateGizmoIsSnapAlongAxes(transUI->display.gizmo))
	{
		bool x, y, z;
		TranslateGizmoGetAxesEnabled(transUI->display.gizmo, &x, &y, &z);
		if (TranslateGizmoIsSnapAlongAxes(transUI->display.gizmo) && !x && y && !z)
			TranslateGizmoDisableAxes(transUI->display.gizmo);
		else
			TranslateGizmoToggleAxes(transUI->display.gizmo, false, true, false);
	}
	else
		TranslateGizmoSetAxes(transUI->display.gizmo, false, true, false);
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}
static void elGizmosToolbarTransAxisZ(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	EditSpecialSnapMode mode = TranslateGizmoGetSpecSnap(transUI->display.gizmo);
	if (mode == EditSnapNone || mode == EditSnapGrid || TranslateGizmoIsSnapAlongAxes(transUI->display.gizmo))
	{
		bool x, y, z;
		TranslateGizmoGetAxesEnabled(transUI->display.gizmo, &x, &y, &z);
		if (TranslateGizmoIsSnapAlongAxes(transUI->display.gizmo) && !x && !y && z)
			TranslateGizmoDisableAxes(transUI->display.gizmo);
		else
			TranslateGizmoToggleAxes(transUI->display.gizmo, false, false, true);
	}
	else
		TranslateGizmoSetAxes(transUI->display.gizmo, false, false, true);
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}

// world pivot callback
static void elGizmosToolbarTransWorldPivot(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	TranslateGizmoSetAlignedToWorld(transUI->display.gizmo, !TranslateGizmoGetAlignedToWorld(transUI->display.gizmo));
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}

// snap enable callback
static void elGizmosToolbarTransSnapEnabled(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	TranslateGizmo *transGizmo = transUI->display.gizmo;
	if (TranslateGizmoIsSnapEnabled(transGizmo))
		TranslateGizmoSetSpecSnap(transGizmo, EditSnapNone);
	else
	{
		EditSpecialSnapMode mode = (EditSpecialSnapMode) ui_ComboBoxGetSelected(transUI->snapMode);
		TranslateGizmoSetSpecSnap(transGizmo, mode);
		if (mode != EditSnapGrid)
			TranslateGizmoDisableAxes(transUI->display.gizmo);
	}
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}

// snap mode callback
static void elGizmosToolbarTransSnapMode(UIComboBox *cb, EditLibGizmosTranslateDisplay *transUI)
{
	EditSpecialSnapMode mode = ui_ComboBoxGetSelected(cb);
	EditSpecialSnapMode oldMode = TranslateGizmoGetSpecSnap(transUI->display.gizmo);
	TranslateGizmoSetSpecSnap(transUI->display.gizmo, mode);
	if (mode != EditSnapNone && mode != EditSnapGrid && (oldMode == EditSnapNone || oldMode == EditSnapGrid))
		TranslateGizmoDisableAxes(transUI->display.gizmo);
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}

// snap width callback
static void elGizmosToolbarTransSnapWidth(UIComboBox *cb, EditLibGizmosTranslateDisplay *transUI)
{
	int selected = ui_ComboBoxGetSelected(cb);
	if (selected != -1)
	{
		TranslateGizmoSetSnapResolution(transUI->display.gizmo, selected + 1);
		if (transUI->changedFunc)
			transUI->changedFunc(transUI->display.gizmo);
	}
}

// snap normal callback
static void elGizmosToolbarTransSnapNormal(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	TranslateGizmoSetSnapNormal(transUI->display.gizmo, !TranslateGizmoGetSnapNormal(transUI->display.gizmo));
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}

// snap normal axis callback
static void elGizmosToolbarTransSnapNormalAxis(UIComboBox *cb, EditLibGizmosTranslateDisplay *transUI)
{
	const char *snap = ui_ComboBoxGetSelectedObject(cb);
	char axis;
	if (snap)
	{
		if (snap[0] == '-')
		{
			TranslateGizmoSetSnapNormalInverse(transUI->display.gizmo, true);
			axis = snap[1];
		}
		else
		{
			TranslateGizmoSetSnapNormalInverse(transUI->display.gizmo, false);
			axis = snap[0];
		}
		TranslateGizmoSetSnapNormalAxis(transUI->display.gizmo, axis - 'X');
		if (transUI->changedFunc)
			transUI->changedFunc(transUI->display.gizmo);
	}
}

// snap clamp callback
static void elGizmosToolbarTransSnapClamp(UIButton *button, EditLibGizmosTranslateDisplay *transUI)
{
	TranslateGizmoSetSnapClamp(transUI->display.gizmo, !TranslateGizmoGetSnapClamp(transUI->display.gizmo));
	if (transUI->changedFunc)
		transUI->changedFunc(transUI->display.gizmo);
}

/******
* This function is used to add a TranslateGizmo display to the specified toolbar.
* PARAMS:
*   tb - EditLibGizmosToolbar where the display is being added
*   transGizmo - TranslateGizmo being added
*   name - string name to show in the toolbar's selector
*   changedFunc - EditLibGizmosToolbarCallback to call when the gizmo is changed in any way through the UI
******/
void elGizmosToolbarAddTranslateGizmo(EditLibGizmosToolbar *tb, TranslateGizmo *transGizmo, const char *name, EditLibGizmosToolbarCallback changedFunc)
{
	EditLibGizmosTranslateDisplay *newDisplay = calloc(1, sizeof(EditLibGizmosTranslateDisplay));
	UIButton *button;
	UIComboBox *cb;
	UIScrollArea *area;
	int i, width1, width2;
	void ***snapTypes = calloc(1, sizeof(void **));
	void ***normalAxes = calloc(1, sizeof(void**));
	void ***snapWidths = calloc(1, sizeof(void**));
	float *widths = GizmoGetSnapWidths();

	newDisplay->display.name = strdup(name);
	newDisplay->display.updateFunc = elGizmosToolbarTransUpdate;
	newDisplay->display.gizmo = transGizmo;
	newDisplay->changedFunc = changedFunc;
	TranslateGizmoSetToolbar(transGizmo, tb);

	// initialize skins
	newDisplay->green = ui_SkinCreate(NULL);
	ui_SkinSetButton(newDisplay->green, colorFromRGBA(0x00FF0077));
	newDisplay->blue = ui_SkinCreate(NULL);
	ui_SkinSetButton(newDisplay->blue, colorFromRGBA(0x0000FF77));
	newDisplay->red = ui_SkinCreate(NULL);
	ui_SkinSetButton(newDisplay->red, colorFromRGBA(0xFF000077));

	// create the various UI widgets for the display
	// -X,Y,Z
	button = ui_ButtonCreate("X", 0, 0, elGizmosToolbarTransAxisX, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "x-Axis");
	button->widget.height = tb->height;
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->axisX = button;
	button = ui_ButtonCreate("Y", elUINextX(button), 0, elGizmosToolbarTransAxisY, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "y-Axis");
	button->widget.height = tb->height;
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->axisY = button;
	button = ui_ButtonCreate("Z", elUINextX(button), 0, elGizmosToolbarTransAxisZ, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "z-Axis");
	button->widget.height = tb->height;
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->axisZ = button;

	// -world pivot
	button = ui_ButtonCreateImageOnly("eui_icon_worldpivot_OFF", elUINextX(button) + 5, 0, elGizmosToolbarTransWorldPivot, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "World Pivot");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->worldPivot = button;

	// -snap enabled
	button = ui_ButtonCreateImageOnly("eui_icon_snapping_OFF", elUINextX(button) + 5, 0, elGizmosToolbarTransSnapEnabled, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "Snap Enabled");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->snapEnabled = button;

	// -snap mode
	for (i = 0; i <= EditSnapSmart; i++)
	{
		switch(i)
		{
			xcase EditSnapGrid:
				eaPush(snapTypes, "Grid");
			xcase EditSnapVertex:
				eaPush(snapTypes, "Vertex");
			xcase EditSnapMidpoint:
				eaPush(snapTypes, "Midpoint");
			xcase EditSnapEdge:
				eaPush(snapTypes, "Edge");
			xcase EditSnapFace:
				eaPush(snapTypes, "Face");
			xcase EditSnapTerrain:
				eaPush(snapTypes, "Terrain");
			xcase EditSnapSmart:
				eaPush(snapTypes, "Auto");
		}
	}
	cb = ui_ComboBoxCreate(elUINextX(button), 0, 70, NULL, snapTypes, NULL);
	ui_WidgetSetTooltipString((UIWidget*) cb, "Snap Mode");
	cb->widget.height = tb->height;
	ui_ComboBoxSetSelectedCallback(cb, elGizmosToolbarTransSnapMode, newDisplay);
	eaPush(&newDisplay->display.widgets, (UIWidget*)cb);
	newDisplay->snapMode = cb;
	area = ui_ScrollAreaCreate(elUINextX(cb) + 5, 0, 0, tb->height, 0, 0, false, false);
	newDisplay->snapArea = area;
	eaPush(&newDisplay->display.widgets, (UIWidget*)area);

	// -snap resolution
	for (i = 0; i < GIZMO_NUM_WIDTHS; i++)
	{
		char val[15];
		sprintf(val, "%.2f ft", widths[i]);
		eaPush(snapWidths, strdup(val));
	}
	cb = ui_ComboBoxCreate(0, 0, 90, NULL, snapWidths, NULL);
	ui_WidgetSetTooltipString((UIWidget*) cb, "Snap Width");
	cb->widget.height = tb->height;
	ui_ComboBoxSetSelectedCallback(cb, elGizmosToolbarTransSnapWidth, newDisplay);
	ui_ScrollAreaAddChild(newDisplay->snapArea, (UIWidget*) cb);
	newDisplay->snapWidth = cb;
	width1 = elUINextX(cb);

	// -snap normal
	button = ui_ButtonCreateImageOnly("eui_icon_snap_normal_OFF", 0, 0, elGizmosToolbarTransSnapNormal, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "Snap Normal");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	ui_ScrollAreaAddChild(newDisplay->snapArea, (UIWidget*) button);
	newDisplay->snapNormal = button;

	// -snap normal axis
	eaPush(normalAxes, "X");
	eaPush(normalAxes, "Y");
	eaPush(normalAxes, "Z");
	eaPush(normalAxes, "-X");
	eaPush(normalAxes, "-Y");
	eaPush(normalAxes, "-Z");
	cb = ui_ComboBoxCreate(elUINextX(button), 0, 35, NULL, normalAxes, NULL);
	ui_WidgetSetTooltipString((UIWidget*) cb, "Snap Normal Axis");
	cb->widget.height = tb->height;
	ui_ComboBoxSetSelectedCallback(cb, elGizmosToolbarTransSnapNormalAxis, newDisplay);
	ui_ScrollAreaAddChild(newDisplay->snapArea, (UIWidget*) cb);
	newDisplay->snapNormalAxis = cb;

	// -snap clamping
	button = ui_ButtonCreateImageOnly("eui_icon_snapclamping_OFF", elUINextX(cb) + 5, 0, elGizmosToolbarTransSnapClamp, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "Snap Clamp");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	ui_ScrollAreaAddChild(newDisplay->snapArea, (UIWidget*) button);
	newDisplay->snapClamp = button;
	width2 = elUINextX(button);

	area->widget.width = MAX(width1, width2);
	elGizmosToolbarAddDisplay(tb, (EditLibGizmosDisplay*) newDisplay, elUINextX(area));
}

/******
* This is used to remove a TranslateGizmo from a toolbar.
* PARAMS:
*   tb - EditLibGizmosToolbar from which the gizmo is being removed
*   transGizmo - TranslateGizmo being removed
******/
void elGizmosToolbarRemoveTranslateGizmo(EditLibGizmosToolbar *tb, TranslateGizmo *transGizmo)
{
	int i;
	for (i = 0; i < eaSize(&tb->displays); i++)
	{
		if (tb->displays[i]->gizmo == transGizmo)
		{
			EditLibGizmosTranslateDisplay *display = (EditLibGizmosTranslateDisplay*) tb->displays[i];
			eaDestroy((char***) display->snapMode->model);
			eaDestroy((char***) display->snapNormalAxis->model);
			eaDestroyEx((char***) display->snapWidth->model, NULL);
			TranslateGizmoSetToolbar(display->display.gizmo, NULL);
			elGizmosDisplayRemove(tb, tb->displays[i]);
			free(display);
		}
	}
}

/******
* ROTATE GIZMO DISPLAY
******/
typedef struct EditLibGizmosRotateDisplay
{
	EditLibGizmosDisplay display;

	// widgets
	UIButton *worldPivot;
	UIButton *snapEnabled;
	UIComboBox *snapAngle;

	//UIScrollArea *snapArea;
	UIButton *snapNormal;
	UIComboBox *snapNormalAxis;

	// callback
	EditLibGizmosToolbarCallback changedFunc;
} EditLibGizmosRotateDisplay;

// update callback
static void elGizmosToolbarRotUpdate(EditLibGizmosDisplay *display)
{
	EditLibGizmosRotateDisplay *rotUI = (EditLibGizmosRotateDisplay*) display;
	bool worldPivot = RotateGizmoGetAlignedToWorld(rotUI->display.gizmo);
	bool snapEnabled = RotateGizmoIsSnapEnabled(rotUI->display.gizmo);
	bool snapNormal = TranslateGizmoGetSnapNormal(edObjHarnessGetTransGizmo());
	bool snapInverse = TranslateGizmoGetSnapNormalInverse(edObjHarnessGetTransGizmo());
	int snapNormalAxis = TranslateGizmoGetSnapNormalAxis(edObjHarnessGetTransGizmo());

	// update world aligned
	ui_ButtonSetImage( rotUI->worldPivot, (worldPivot ? "eui_icon_worldpivot_ON" : "eui_icon_worldpivot_OFF") );


	// update snap normal and axis
	ui_ButtonSetImage( rotUI->snapNormal, (snapNormal ? "eui_icon_snap_normal_ON" : "eui_icon_snap_normal_OFF") );
	if (snapInverse)
		snapNormalAxis += 3;
	ui_ComboBoxSetSelected(rotUI->snapNormalAxis, snapNormalAxis);
	ui_SetActive((UIWidget*) rotUI->snapNormalAxis, snapNormal);

	// update snapping enabled and snap angle
	ui_ButtonSetImage( rotUI->snapEnabled, (snapEnabled ? "eui_icon_snapping_ON" : "eui_icon_snapping_OFF") );
	ui_ComboBoxSetSelected(rotUI->snapAngle, RotateGizmoGetSnapResolution(rotUI->display.gizmo) - 1);
}

// world pivot callback
void elGizmosToolbarRotWorldPivot(UIButton *button, EditLibGizmosRotateDisplay *rotUI)
{
	RotateGizmoSetAlignedToWorld(rotUI->display.gizmo, !RotateGizmoGetAlignedToWorld(rotUI->display.gizmo));
	if (rotUI->changedFunc)
		rotUI->changedFunc(rotUI->display.gizmo);
}

// snap enabled callback
void elGizmosToolbarRotSnapEnabled(UIButton *button, EditLibGizmosRotateDisplay *rotUI)
{
	RotateGizmoEnableSnap(rotUI->display.gizmo, !RotateGizmoIsSnapEnabled(rotUI->display.gizmo));
	if (rotUI->changedFunc)
		rotUI->changedFunc(rotUI->display.gizmo);
}

// snap angle callback
void elGizmosToolbarRotSnapAngle(UIComboBox *cb, EditLibGizmosRotateDisplay *rotUI)
{
	int selected = ui_ComboBoxGetSelected(cb);
	if (selected != -1)
	{
		RotateGizmoSetSnapResolution(rotUI->display.gizmo, selected + 1);
		if (rotUI->changedFunc)
			rotUI->changedFunc(rotUI->display.gizmo);
	}
}

// snap normal callback
static void elGizmosToolbarRotSnapNormal(UIButton *button, TranslateGizmo *gizmo)
{
	EditLibGizmosRotateDisplay *rotUI = (EditLibGizmosRotateDisplay *)button->widget.userinfo;
	TranslateGizmoSetSnapNormal(gizmo, !TranslateGizmoGetSnapNormal(gizmo));
	if (rotUI->changedFunc)
		rotUI->changedFunc(gizmo);
}

// snap normal axis callback

static void elGizmosToolbarRotSnapNormalAxis(UIComboBox *cb, TranslateGizmo *gizmo)
{
	EditLibGizmosRotateDisplay *rotUI = (EditLibGizmosRotateDisplay *)cb->widget.userinfo;
	const char *snap = ui_ComboBoxGetSelectedObject(cb);
	char axis;
	if (snap)
	{
		if (snap[0] == '-')
		{
			TranslateGizmoSetSnapNormalInverse(gizmo, true);
			axis = snap[1];
		}
		else
		{
			TranslateGizmoSetSnapNormalInverse(gizmo, false);
			axis = snap[0];
		}
		TranslateGizmoSetSnapNormalAxis(gizmo, axis - 'X');
		if (rotUI->changedFunc)
			rotUI->changedFunc(gizmo);
	}
}

/******
* This function is used to add a RotateGizmo display to the specified toolbar.
* PARAMS:
*   tb - EditLibGizmosToolbar where the display is being added
*   rotGizmo - RotateGizmo being added
*   name - string name to show in the toolbar's selector
*   changedFunc - EditLibGizmosToolbarCallback to call when the gizmo is changed in any way through the UI
******/
void elGizmosToolbarAddRotateGizmo(EditLibGizmosToolbar *tb, RotateGizmo *rotGizmo, const char *name, EditLibGizmosToolbarCallback changedFunc)
{
	EditLibGizmosRotateDisplay *newDisplay = calloc(1, sizeof(EditLibGizmosRotateDisplay));
	UIButton *button;
	UIComboBox *cb;
	int i;

	//UIScrollArea *area;
	char ***snapAngles = calloc(1, sizeof(char**));
	void ***normalAxes = calloc(1, sizeof(void**));
	int *angles = GizmoGetSnapAngles();

	newDisplay->display.name = strdup(name);
	newDisplay->display.updateFunc = elGizmosToolbarRotUpdate;
	newDisplay->display.gizmo = rotGizmo;
	newDisplay->changedFunc = changedFunc;
	RotateGizmoSetToolbar(rotGizmo, tb);

	// create the various UI widgets for the display
	// -world pivot
	button = ui_ButtonCreateImageOnly("eui_icon_worldpivot_OFF", 0, 0, elGizmosToolbarRotWorldPivot, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "World Pivot");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->worldPivot = button;

	// -snap enabled
	button = ui_ButtonCreateImageOnly("eui_icon_snapping_OFF", elUINextX(button) + 5, 0, elGizmosToolbarRotSnapEnabled, newDisplay);
	ui_WidgetSetTooltipString((UIWidget*) button, "Snap Enabled");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->snapEnabled = button;

	// -snap resolution
	for (i = 0; i < GIZMO_NUM_ANGLES; i++)
	{
		char val[10];
		sprintf(val, "%i deg", angles[i]);
		eaPush(snapAngles, strdup(val));
	}
	cb = ui_ComboBoxCreate(elUINextX(button), 0, 60, NULL, snapAngles, NULL);
	ui_WidgetSetTooltipString((UIWidget*) cb, "Snap Angle");
	cb->widget.height = tb->height;
	ui_ComboBoxSetSelectedCallback(cb, elGizmosToolbarRotSnapAngle, newDisplay);
	eaPush(&newDisplay->display.widgets, (UIWidget*)cb);
	newDisplay->snapAngle = cb;


	//area = ui_ScrollAreaCreate(elUINextX(cb) + 5, 0, 0, tb->height, 0, 0, false, false);
	//newDisplay->snapArea = area;
	//eaPush(&newDisplay->display.widgets, (UIWidget*)area);

	// -snap normal
	button = ui_ButtonCreateImageOnly("eui_icon_snap_normal_OFF", elUINextX(cb), 0, elGizmosToolbarRotSnapNormal, edObjHarnessGetTransGizmo());
	button->widget.userinfo = newDisplay;
	ui_WidgetSetTooltipString((UIWidget*) button, "Snap Normal");
	ui_WidgetSetDimensions((UIWidget*) button, tb->height, tb->height);
	ui_ButtonSetImageStretch( button, true );
	//ui_ScrollAreaAddChild(newDisplay->snapArea, (UIWidget*) button);
	eaPush(&newDisplay->display.widgets, (UIWidget*)button);
	newDisplay->snapNormal = button;

	// -snap normal axis
	eaPush(normalAxes, "X");
	eaPush(normalAxes, "Y");
	eaPush(normalAxes, "Z");
	eaPush(normalAxes, "-X");
	eaPush(normalAxes, "-Y");
	eaPush(normalAxes, "-Z");
	cb = ui_ComboBoxCreate(elUINextX(button), 0, 35, NULL, normalAxes, NULL);
	ui_WidgetSetTooltipString((UIWidget*) cb, "Snap Normal Axis");
	cb->widget.height = tb->height;
	cb->widget.userinfo = newDisplay;
	ui_ComboBoxSetSelectedCallback(cb, elGizmosToolbarRotSnapNormalAxis, edObjHarnessGetTransGizmo());
	eaPush(&newDisplay->display.widgets, (UIWidget*)cb);
	//ui_ScrollAreaAddChild(newDisplay->snapArea, (UIWidget*) cb);
	newDisplay->snapNormalAxis = cb;

	elGizmosToolbarAddDisplay(tb, (EditLibGizmosDisplay*) newDisplay, elUINextX(cb));
}

/******
* This is used to remove a Rotate from a toolbar.
* PARAMS:
*   tb - EditLibGizmosToolbar from which the gizmo is being removed
*   rotGizmo - RotateGizmo being removed
******/
void elGizmosToolbarRemoveRotateGizmo(EditLibGizmosToolbar *tb, RotateGizmo *rotGizmo)
{
	int i;
	for (i = 0; i < eaSize(&tb->displays); i++)
	{
		if (tb->displays[i]->gizmo == rotGizmo)
		{
			EditLibGizmosRotateDisplay *display = (EditLibGizmosRotateDisplay*) tb->displays[i];
			eaDestroy((char***) display->snapAngle->model);
			RotateGizmoSetToolbar(rotGizmo, NULL);
			elGizmosDisplayRemove(tb, tb->displays[i]);
			free(display);
		}
	}
}

#endif

#include "EditLibGizmosToolbar_c_ast.c"
