#include "gclDynamicsDebugUI.h"
#include "UILib.h"
#include "dynFxManager.h"
#include "Prefs.h"
#include "cmdparse.h"
#include "textparser.h"
#include "GfxPrimitive.h"
#include "quat.h"
#include "stringcache.h"
#include "gclBugReport.h"
#include "gclEntity.h"
#include "gclEntityNet.h"
#include "WorldLib.h"


extern ParseTable parse_DynFxLogLine[];
#define TYPE_parse_DynFxLogLine DynFxLogLine
extern ParseTable ParseDynDrawParticle[];
extern ParseTable parse_DynTransform[];
#define TYPE_parse_DynTransform DynTransform

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

UIWindow* pDynDebugWindow;
UIWindow* pStateLoggerWindow;
UIFilteredList *pList;
UITextArea* pStateText;

static DynFxLogLine* pSelectedLine = NULL;


#define LINE_HEIGHT 26
#define LINE_INDENT 5
#define LINE_WIDTH_SPACE 10

static bool bOnlyShowSelected;

AUTO_CMD_INT(bOnlyShowSelected, dfxLogOnlyShowSelected);

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxClearLog(void)
{
	pSelectedLine = NULL;
	dynFxClearLog();
	if (pList)
		ui_FilteredListRefresh(pList);
}

static int cmdParseGetCurrentValue(const char *var)
{
	char *ret=NULL;
	int curVal=0;
	if (0==globCmdParseAndReturn(var, &ret, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL)) {
		if (strStartsWith(ret, var)) {
			char *value = ret + strlen(var);
			while (*value==' ')
				value++;
			if (value[0]=='\"') {
				if (value[1]=='\"')
					curVal = 0;
				else
					curVal = 1;
			} else {
				sscanf(value, "%d", &curVal);
			}
		} else {
			Errorf("cmdParse failed on string \"%s\"", var);
		}
	} else {
		Errorf("cmdParse failed on string \"%s\" (or command is not bound to a variable?)", var);
	}
	estrDestroy(&ret);
	return curVal;
}


static void toggleCmdParseCheck(UIAnyWidget *widget, UserData userData)
{
	UICheckButton *check = (UICheckButton *)widget;
	const char *var = userData;
	int curVal=cmdParseGetCurrentValue(var);
	int desiredVal=ui_CheckButtonGetState(check);
	if (curVal != desiredVal) {
		if (0==globCmdParsef("%s %d", var, desiredVal)) {
			Errorf("cmdParse failed on string \"%s %d\"", var, desiredVal);
		}
	}
}

static void addCmdParseCheck(UIWindow *window, const char *title, const char *command, F32* x, F32* y, bool bNewLine)
{
	UICheckButton *check = ui_CheckButtonCreate(*x, *y, title, !!cmdParseGetCurrentValue(command));
	ui_CheckButtonSetToggledCallback(check, toggleCmdParseCheck, (UserData)command);
	ui_WindowAddChild(window, UI_WIDGET(check));
	if (bNewLine)
	{
		*y+=LINE_HEIGHT;
		*x = 0;
	}
	else
	{
		*x+= UI_WIDGET(check)->width + LINE_WIDTH_SPACE;
	}
}

static void addCmdParseButton(UIWindow *window, const char *title, const char *command, F32* x, F32 *y, bool bNewLine)
{
	UIButton *button = ui_ButtonCreate(title, *x, *y, NULL, NULL);
	ui_ButtonSetCommand(button, command);
	ui_WindowAddChild(window, UI_WIDGET(button));
	if (bNewLine)
	{
		*y+=LINE_HEIGHT;
		*x = 0;
	}
	else
	{
		*x+= UI_WIDGET(button)->width + LINE_WIDTH_SPACE;
	}
}

static void gclDynamicsDebugWindowTick(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS)
{
	static bool bOnlyShowSelectedSet = false;
	GamePrefStoreInt("GclDynamicsDebug_X", (int)window->widget.x);
	GamePrefStoreInt("GclDynamicsDebug_Y", (int)window->widget.y);
	GamePrefStoreInt("GclDynamicsDebug_H", (int)window->widget.height);
	GamePrefStoreInt("GclDynamicsDebug_W", (int)window->widget.width);
	GamePrefStoreInt("GclDynamicsDebug_LineSort", pList->pList->eaColumns[3]->eSort);

	if (bOnlyShowSelected)
		ui_FilteredListSetFilter(pList, ui_FilteredListGetSelectedString(pList));
	else if (bOnlyShowSelectedSet)
		ui_FilteredListSetFilter(pList, "");
	bOnlyShowSelectedSet = bOnlyShowSelected;

	ui_FilteredListRefresh(pList);
	ui_WindowTick(window, UI_PARENT_VALUES);

}

typedef bool (*UICloseFunc)(UIAnyWidget *, UserData);

bool gclDynamicsDebugClose(UIWindow* pWindow, UserData data)
{
	dynDebugState.bFxLogging = false;
	return true;
}

// -1 means toggle
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxLogger(int show)
{
	if (!pDynDebugWindow)
	{
		UIListColumn* pCol;
		F32 x=0, y=2;
		pDynDebugWindow = ui_WindowCreate("FX Log",
			GamePrefGetInt("GclDynamicsDebug_X", 8),
			GamePrefGetInt("GclDynamicsDebug_Y", 600),
			GamePrefGetInt("GclDynamicsDebug_W", 280),
			GamePrefGetInt("GclDynamicsDebug_H", 450)
			);
		pDynDebugWindow->widget.tickF = gclDynamicsDebugWindowTick;
		ui_WindowSetCloseCallback(pDynDebugWindow, gclDynamicsDebugClose, NULL);

		addCmdParseButton(pDynDebugWindow, "ClearLog", "dfxClearLog", &x, &y, false);
		addCmdParseButton(pDynDebugWindow, "Show State", "dfxStateLoggerToggle", &x, &y, false);
		addCmdParseCheck(pDynDebugWindow, "Show Only Selected", "dfxLogOnlyShowSelected", &x, &y, true);

		pList = ui_FilteredListCreateParseName("FX ID", parse_DynFxLogLine, &fxLog.eaLines, "FXID", 20);
		pCol = pList->pList->eaColumns[0];
		ui_ListColumnSetWidth(pCol, true, 1.0f);
		ui_ListColumnSetSortable(pCol, true);


		pCol = ui_ListColumnCreateParseName("Time", "Time", NULL);
		ui_ListColumnSetSortable(pCol, true);
		ui_ListColumnSetWidth(pCol, true, 1.0f);
		ui_ListAppendColumn(pList->pList, pCol);

		pCol = ui_ListColumnCreateParseName("Type", "FXInfo", NULL);
		ui_ListColumnSetSortable(pCol, true);
		ui_ListColumnSetWidth(pCol, true, 1.0f);
		ui_ListAppendColumn(pList->pList, pCol);

		pCol = ui_ListColumnCreateParseName("Line", "esLine", NULL);
		pCol->eSort = GamePrefGetInt("GclDynamicsDebug_LineSort", UISortAscending);
		ui_ListColumnSetSortable(pCol, true);
		ui_ListColumnSetWidth(pCol, true, 1.0f);
		ui_ListAppendColumn(pList->pList, pCol);

		pList->bDontAutoSort = true;
		pList->bDontAutoScroll = true;

		ui_WidgetSetPositionEx((UIWidget*)pList,x,y,0,0,UITopLeft);
		ui_WidgetSetDimensionsEx((UIWidget *)pList, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);


		ui_WindowAddChild(pDynDebugWindow, pList);
		dynDebugState.bFxLogging = true;
	}

	if (show < 0)
	{
		if (ui_WindowIsVisible(pDynDebugWindow))
		{
			ui_WindowHide(pDynDebugWindow);
			dynDebugState.bFxLogging = false;
		}
		else
		{
			ui_WindowShow(pDynDebugWindow);
			dynDebugState.bFxLogging = true;
		}
	}
	else if (show && !ui_WindowIsVisible(pDynDebugWindow))
	{
		ui_WindowShow(pDynDebugWindow);
		dynDebugState.bFxLogging = true;
	}
	else if (!show && ui_WindowIsVisible(pDynDebugWindow))
	{
		ui_WindowHide(pDynDebugWindow);
		dynDebugState.bFxLogging = false;
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxLoggerToggle(void)
{
	dfxLogger(-1);
}

static void gclDynamicsStateWindowTick(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS)
{
	GamePrefStoreInt("GclDynamicsState_X", (int)window->widget.x);
	GamePrefStoreInt("GclDynamicsState_Y", (int)window->widget.y);
	GamePrefStoreInt("GclDynamicsState_H", (int)window->widget.height);
	GamePrefStoreInt("GclDynamicsState_W", (int)window->widget.width);

	pSelectedLine = (DynFxLogLine*)ui_FilteredListGetSelectedObject(pList);

	if (pSelectedLine)
	{
		if (pSelectedLine->pState)
		{
			char* esState = NULL;
			char* esTransform = NULL;
			ParserWriteText(&esState, ParseDynDrawParticle, pSelectedLine->pState->pDraw, WRITETEXTFLAG_PRETTYPRINT, 0, 0);
			ParserWriteText(&esTransform, parse_DynTransform, &pSelectedLine->pState->xform, WRITETEXTFLAG_PRETTYPRINT, 0, 0);
			estrConcatf(&esState, "World Space Transform\n-------------------------\n");
			estrAppend(&esState, &esTransform);
			ui_TextAreaSetText(pStateText, esState);
			estrDestroy(&esState);
			estrDestroy(&esTransform);

			{
				Mat4 stateMat;
				quatToMat(pSelectedLine->pState->xform.qRot, stateMat);
				copyVec3(pSelectedLine->pState->xform.vPos, stateMat[3]);
				gfxDrawAxes3D(stateMat, 3.0f);
			}
		}
		else
		{
			ui_TextAreaSetText(pStateText, "Selection has no Particle");
		}
	}
	else
	{
		ui_TextAreaSetText(pStateText, "No Selection");
	}

	ui_WindowTick(window, UI_PARENT_VALUES);
}

bool gclDynamicsStateClose(UIWindow* pWindow, UserData data)
{
	dynDebugState.bFxLogState = false;
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxStateLogger(int show)
{
	if (!pStateLoggerWindow)
	{
		F32 x=0, y=2;
		pStateLoggerWindow = ui_WindowCreate("FX Log",
			GamePrefGetInt("GclDynamicsState_X", 400),
			GamePrefGetInt("GclDynamicsState_Y", 200),
			GamePrefGetInt("GclDynamicsState_W", 280),
			GamePrefGetInt("GclDynamicsState_H", 450)
			);
		pStateLoggerWindow->widget.tickF = gclDynamicsStateWindowTick;
		ui_WindowSetCloseCallback(pDynDebugWindow, gclDynamicsStateClose, NULL);

		pStateText = ui_TextAreaCreate("No State Found");

		ui_WidgetSetPositionEx((UIWidget*)pStateText,x,y,0,0,UITopLeft);
		ui_WidgetSetDimensionsEx((UIWidget *)pStateText, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);


		ui_WindowAddChild(pStateLoggerWindow, pStateText);
	}

	if (show < 0)
	{
		if (ui_WindowIsVisible(pStateLoggerWindow))
		{
			ui_WindowHide(pStateLoggerWindow);
			dynDebugState.bFxLogState = false;
		}
		else
		{
			ui_WindowShow(pStateLoggerWindow);
			dynDebugState.bFxLogState = true;
		}
	}
	else if (show && !ui_WindowIsVisible(pStateLoggerWindow))
	{
		ui_WindowShow(pStateLoggerWindow);
		dynDebugState.bFxLogState = true;
	}
	else if (!show && ui_WindowIsVisible(pStateLoggerWindow))
	{
		ui_WindowHide(pStateLoggerWindow);
		dynDebugState.bFxLogState = false;
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxStateLoggerToggle(void)
{
	dfxStateLogger(-1);
}

typedef struct FXCbugData
{
	const char* pcTemp;
} FXCbugData;

typedef struct AnimationCbugData
{
	const char* pcTemp;
} AnimationCbugData;

void gclFXCBugCallback(void** ppStruct, ParseTable** ppti, char **estrLabel)
{
	FXCbugData fxData = {0};
	fxData.pcTemp = allocAddString("Testing FX");
}

void gclAnimationCBugCallback(void** ppStruct, ParseTable** ppti, char **estrLabel)
{
	AnimationCbugData animData = {0};
	animData.pcTemp = allocAddString("Testing Animation");
}

static void gclDynamicsDebugRegisterCallbacks(void)
{
	cBugAddCustomDataCallback("CBug.Category.FX", gclFXCBugCallback);
	cBugAddCustomDataCallback("CBug.Category.Animation", gclAnimationCBugCallback);
	wlSetEntityNumFuncs(gclNumEntities, gclNumClientOnlyEntities);
}

void gclDynamicsDebugStartup(void)
{
	gclDynamicsDebugRegisterCallbacks();
}