#include "gclCursorMode.h"
#include "StashTable.h"
#include "StringCache.h"
#include "UIGen.h"
#include "inputLib.h"
#include "inputMouse.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct CursorMode
{
	// name of the mode
	const char *pchModeName;

	// name of the cursor used for this mode
	const char *pchCursorName;

	// the function that is called when a mouse click has been received 
	OnClickHandler	fpClickHandler;
	
	OnEnterExitModeHandler fpOnEnterHandler;

	OnEnterExitModeHandler fpOnExitHandler;

	OnUpdateHandler fpUpdateHandler;

} CursorMode;

static StashTable s_pCursorTargetingTable;

static const CursorMode *s_pDefaultMode = NULL;
static const CursorMode *s_pCurrentMode = NULL;
static bool s_bAllowNextClick = false;

extern bool gbNoGraphics;

static CursorMode* _cursorTargetignMode_GetModeByName(const char *pchModeName)
{
	CursorMode *pMode = NULL;
	stashFindPointer(s_pCursorTargetingTable, pchModeName, &pMode);
	return pMode;
}

static bool _cursorTargetingMode_SetMode(const CursorMode *mode)
{
	UIGenState eState;
	char achGenStateName[1000];
	if (!mode || s_pCurrentMode == mode)
		return true;

	s_bAllowNextClick = false;

	// Turn off the old state.
	if (s_pCurrentMode)
	{
		// Disable the global gen state, if any.
		sprintf(achGenStateName, "CursorMode_%s", s_pCurrentMode->pchModeName);
		eState = ui_GenGetState(achGenStateName);
		if (eState >= 0)
			ui_GenSetGlobalState(eState, false);
		// Call per-state special exit handler.
		if (s_pCurrentMode->fpOnExitHandler)
		{
			s_pCurrentMode->fpOnExitHandler();
		}

	}

	s_pCurrentMode = mode;

	// Turn on the global gen state, if any.
	sprintf(achGenStateName, "CursorMode_%s", mode->pchModeName);
	eState = ui_GenGetState(achGenStateName);
	if (eState >= 0)
		ui_GenSetGlobalState(eState, true);

	ui_SetCurrentDefaultCursor(s_pCurrentMode->pchCursorName);
	ui_SetCursorByName(s_pCurrentMode->pchCursorName);
	
	if (s_pCurrentMode->fpOnEnterHandler)
	{
		s_pCurrentMode->fpOnEnterHandler();
	}

	return true;
}

void gclCursorMode_Register(const char *pchModeName, const char *pchCursorName, 
							OnClickHandler clickHandler, 
							OnEnterExitModeHandler enterHander,
							OnEnterExitModeHandler exitHandler,
							OnUpdateHandler updateHandler)
{
	CursorMode *pTargetingMode;

	if (!s_pCursorTargetingTable)
	{
		s_pCursorTargetingTable = stashTableCreateWithStringKeys(4, StashDefault);
	}

	if (stashFindPointer(s_pCursorTargetingTable, pchModeName, NULL))
		return;

	pTargetingMode = calloc(1, sizeof(CursorMode));
	devassert(pTargetingMode);

	pTargetingMode->fpClickHandler = clickHandler;
	pTargetingMode->fpOnEnterHandler = enterHander;
	pTargetingMode->fpOnExitHandler = exitHandler;
	pTargetingMode->fpUpdateHandler = updateHandler;
	pTargetingMode->pchModeName = allocAddString(pchModeName);
	
	if (! pchCursorName)
		pchCursorName = "Default";

	pTargetingMode->pchCursorName = allocAddString(pchCursorName);

	stashAddPointer(s_pCursorTargetingTable, pchModeName, pTargetingMode, false);
}

void gclCursorMode_SetDefault(const char *pchModeName)
{
	CursorMode *pMode = NULL;
	if (!stashFindPointer(s_pCursorTargetingTable, pchModeName, &pMode))
		return;

	s_pDefaultMode = pMode;
	
	if (!s_pCurrentMode)
		gclCursorMode_ChangeToDefault();
}

bool gclCursorMode_SetModeByName(const char *pchModeName)
{
	CursorMode *pMode = NULL;
	if (!stashFindPointer(s_pCursorTargetingTable, pchModeName, &pMode))
		return false;

	return _cursorTargetingMode_SetMode(pMode);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclCursorMode_ChangeToDefault(void)
{
	_cursorTargetingMode_SetMode(s_pDefaultMode);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorMode_ChangeToDefault");
void exprCursorMode_ChangeToDefault()
{
	_cursorTargetingMode_SetMode(s_pDefaultMode);
}

const char* gclCursorMode_GetCurrent()
{
	if (s_pCurrentMode)
		return s_pCurrentMode->pchModeName;

	return "";
}

void gclCursorMode_OnClick(bool bButtonDown)
{
	if (s_pCurrentMode)
	{
		s_pCurrentMode->fpClickHandler(bButtonDown);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorMode_IsDefault");
bool gclCursorMode_IsDefault()
{
	return s_pCurrentMode == s_pDefaultMode;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Interface, Powers);
void cursorClick(bool bButtonDown)
{
	gclCursorMode_OnClick(bButtonDown);
}


// If true is passed, do not cancel the current cursor mode even if the mouse is clicked.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CursorModeAllowThisClick);
void gclCursorModeAllowThisClick(bool bAllow)
{
	s_bAllowNextClick = bAllow;
}

void gclCursorMode_OncePerFrame(void)
{
	if (!s_bAllowNextClick && !gbNoGraphics && inpCheckHandled() && (mouseDown(MS_LEFT) || mouseDown(MS_RIGHT)) )
	{
		gclCursorMode_ChangeToDefault();
	}
	else if (s_pCurrentMode && s_pCurrentMode->fpUpdateHandler)
	{
		s_pCurrentMode->fpUpdateHandler();
	}
	s_bAllowNextClick = false;
}