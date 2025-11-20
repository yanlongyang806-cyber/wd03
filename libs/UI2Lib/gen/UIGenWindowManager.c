#include "UIGenWindowManager.h"
#include "UIGen.h"
#include "UIGenPrivate.h"
#include "earray.h"
#include "ReferenceSystem.h"
#include "StringCache.h"
#include "error.h"
#include "textparser.h"
#include "ResourceManager.h"
#include "Expression.h"
#include "partition_enums.h"

#include "UIGen_h_ast.h"
#include "UIGenWindowManager_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static DictionaryHandle s_hWindowDict;
static S32 s_iPercentXColumn = -1;
static S32 s_iPercentYColumn = -1;
UIGenWindowManager *g_ui_pGenWindowManager;

AUTO_FIXUPFUNC;
TextParserResult ui_GenWindowParserFixup(UIGenWindow *pWindow, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_DESTRUCTOR)
	{
		// If this is a cloned window, make sure it properly cleans itself up.
		if (pWindow->pInstance)
		{
			ui_GenLayersRemoveWindow(pWindow->pInstance, true);
			eaPush(&g_GenState.eaFreeQueue, pWindow->pInstance);
			pWindow->pInstance = NULL;
		}
	}
	return PARSERESULT_SUCCESS;
}

static bool GenWindowDefValidate(UIGenWindowDef *pWindowDef)
{
	UI_GEN_FAIL_IF(pWindowDef, pWindowDef->iClones < 0, "%s: Specifies a negative window clone count.", pWindowDef->pchName);
	UI_GEN_FAIL_IF(pWindowDef, pWindowDef->iClones >= pWindowDef->iClones >= UI_GEN_MAX_WINDOWS, "%s: Specifies %d window clones, the current limit is %d.", pWindowDef->pchName, pWindowDef->iClones, UI_GEN_MAX_WINDOWS);
	UI_GEN_FAIL_IF(pWindowDef, pWindowDef->bModal && pWindowDef->bPersistOpen, "%s: Specifies both Modal and PersistOpen, you should not persist modal windows.", pWindowDef->pchName);

	UI_GEN_WARN_IF(pWindowDef, eaiSize(&pWindowDef->eaiAllowStates) > 1, "%s: Has more than one AllowInState specified, this behavior is undefined. Talk to Alex or Joshua to explain what you need.", pWindowDef->pchName);

	if (pWindowDef->pOnWindowAdded || pWindowDef->pOnWindowRemoved)
	{
		ExprContext *pContext = ui_GenGetContext(NULL);
		// This pointer must be set to NULL to work with static checking.
		exprContextSetPointerVar(pContext, "Self", NULL, parse_UIGen, true, true);
		// Likewise we need to make sure GenData will exist so we can set its type.
		exprContextSetPointerVar(pContext, "GenData", NULL, parse_UIGen, true, true);
		exprContextSetPartition(pContext, PARTITION_CLIENT);
		ParserScanForSubstruct(parse_UIGenWindowDef, pWindowDef, parse_Expression, 0, 0, ui_GenGenerateExpr, pContext);
	}

	return true;
}

static int GenWindowDefResValidate(enumResourceEventType eType, const char *pDictName, const char *pchWindow, UIGenWindowDef *pWindowDef, U32 iUserID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		GenWindowDefValidate(pWindowDef);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static void GenWindowDefResEvent(enumResourceEventType eType, const char *pDictName, const char *pchWindow, UIGenWindowDef *pWindowDef, UIGenWindowManager *pWindowManager)
{
	S32 i;
	if (!pWindowDef || !pWindowManager)
		return;

	switch (eType)
	{
	case RESEVENT_RESOURCE_MODIFIED:
		for (i = eaSize(&pWindowManager->eaActiveWindows) - 1; i >= 0; i--)
		{
			UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
			if (GET_REF(pWindow->hDef) == pWindowDef && pWindow->chClone >= pWindowDef->iClones)
			{
				eaRemove(&pWindowManager->eaActiveWindows, i);
				ui_GenWindowManagerHide(pWindowManager, pWindow, true);
				StructDestroy(parse_UIGenWindow, pWindow);
			}
		}
		break;
	}
}

static void GenReloadEvent(enumResourceEventType eType, const char *pDictName, const char *pchGen, UIGen *pGen, UIGenWindowManager *pWindowManager)
{
	S32 i;

	if (!pWindowManager)
		return;

	switch (eType)
	{
	case RESEVENT_INDEX_MODIFIED:
		// If a Gen was modified, make sure to re-create the instance
		if (pchGen && *pchGen)
		{
			for (i = eaSize(&pWindowManager->eaActiveWindows) - 1; i >= 0; i--)
			{
				UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
				if (!stricmp(REF_STRING_FROM_HANDLE(pWindow->hTemplate), pchGen) && pWindow->pInstance)
				{
					bool bVisible = pWindow->bVisible;
					ui_GenWindowManagerHide(pWindowManager, pWindow, true);
					eaPush(&g_GenState.eaFreeQueue, pWindow->pInstance);
					pWindow->pInstance = ui_GenClone(GET_REF(pWindow->hTemplate));
					pWindow->pInstance->chClone = pWindow->chClone;
					if (bVisible)
						ui_GenWindowManagerShow(pWindowManager, pWindow);
				}
			}
		}
		break;
	case RESEVENT_RESOURCE_MODIFIED:
		// If a Gen was modified, make sure to re-create the instance
		for (i = eaSize(&pWindowManager->eaActiveWindows) - 1; i >= 0; i--)
		{
			UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
			if (GET_REF(pWindow->hTemplate) == pGen && pWindow->pInstance)
			{
				bool bVisible = pWindow->bVisible;
				ui_GenWindowManagerHide(pWindowManager, pWindow, true);
				eaPush(&g_GenState.eaFreeQueue, pWindow->pInstance);
				pWindow->pInstance = ui_GenClone(GET_REF(pWindow->hTemplate));
				pWindow->pInstance->chClone = pWindow->chClone;
				if (bVisible)
					ui_GenWindowManagerShow(pWindowManager, pWindow);
			}
		}
		break;
	case RESEVENT_RESOURCE_REMOVED:
		// If a Gen was removed, make sure to remove all the windows that refer to the Gen.
		for (i = eaSize(&pWindowManager->eaActiveWindows) - 1; i >= 0; i--)
		{
			UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
			if (GET_REF(pWindow->hTemplate) == pGen)
			{
				ui_GenWindowManagerHide(pWindowManager, pWindow, true);
				eaRemove(&pWindowManager->eaActiveWindows, i);
				StructDestroy(parse_UIGenWindow, pWindow);
			}
		}
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////
// Utility functions

static UIGenWindowDef *ui_GetDefaultWindow(void)
{
	UIGenWindowDef *pWindow = RefSystem_ReferentFromString(s_hWindowDict, "DefaultWindow");
	if (!pWindow)
	{
		pWindow = StructCreate(parse_UIGenWindowDef);
		pWindow->pchName = allocAddString("DefaultWindow");
		RefSystem_AddReferent(s_hWindowDict, pWindow->pchName, pWindow);
	}
	return pWindow;
}

static UIGenWindowDef *ui_GetDefaultModal(void)
{
	UIGenWindowDef *pModal = RefSystem_ReferentFromString(s_hWindowDict, "DefaultModal");
	if (!pModal)
	{
		pModal = StructCreate(parse_UIGenWindowDef);
		pModal->pchName = allocAddString("DefaultModal");
		pModal->bModal = true;
		RefSystem_AddReferent(s_hWindowDict, pModal->pchName, pModal);
	}
	return pModal;
}

static UIGenWindowDef *ui_GenGetWindowDef(UIGen *pGen)
{
	if (pGen && pGen->pchWindow && *pGen->pchWindow)
	{
		UIGenWindowDef *pWindowDef = RefSystem_ReferentFromString(s_hWindowDict, pGen->pchWindow);
		if (!pWindowDef)
			ErrorFilenamef(pGen->pchFilename, "%s: Is trying to use UIGenWindowDef %s that does not exist.", pGen->pchName, pGen->pchWindow);
		return pWindowDef;
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// UIGen Window API

UIGen *ui_GenWindowGetGen(UIGenWindow *pWindow)
{
	if (pWindow)
	{
		return pWindow->pInstance ? pWindow->pInstance : GET_REF(pWindow->hTemplate);
	}
	return NULL;
}

UIGenWindow *ui_GenWindowManagerFindWindow(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, U8 chClone)
{
	UIGenWindow *pWindow = NULL;
	S32 i;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	for (i = 0; i < eaSize(&pWindowManager->eaActiveWindows); i++)
	{
		pWindow = pWindowManager->eaActiveWindows[i];
		if (GET_REF(pWindow->hDef) == pWindowDef)
		{
			if ((GET_REF(pWindow->hTemplate) == pGen && pWindow->chClone == chClone)
				|| (chClone == 0 && pWindow->pInstance == pGen))
			{
				return pWindow;
			}
		}
	}

	return NULL;
}

bool ui_GenWindowManagerAddWindow(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow)
{
	if (!pWindow)
		return false;
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (eaFind(&pWindowManager->eaActiveWindows, pWindow) < 0)
	{
		UIGenWindowDef *pDef = GET_REF(pWindow->hDef);
		UIGen *pGen = ui_GenWindowGetGen(pWindow);
		if (pDef && pDef->pOnWindowAdded && pGen)
			ui_GenRunAction(pGen, pDef->pOnWindowAdded);
		eaPush(&pWindowManager->eaActiveWindows, pWindow);
		return true;
	}
	return false;
}

bool ui_GenWindowManagerRemoveWindow(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow)
{
	if (!pWindow)
		return false;
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (eaFindAndRemove(&pWindowManager->eaActiveWindows, pWindow) >= 0)
	{
		UIGenWindowDef *pDef = GET_REF(pWindow->hDef);
		UIGen *pGen = ui_GenWindowGetGen(pWindow);
		if (pDef && pDef->pOnWindowRemoved && pGen)
			ui_GenRunAction(pGen, pDef->pOnWindowRemoved);
		return true;
	}
	return false;
}

UIGenWindow *ui_GenWindowManagerCreateClonedWindow(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, U8 chClone)
{
	UIGenWindow *pWindow;

	if (!pWindowDef || !pGen)
		return NULL;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	pWindow = ui_GenWindowManagerFindWindow(pWindowManager, pWindowDef, pGen, chClone);

	if (!pWindow)
	{
		if (!devassertmsgf(ui_GenInDictionary(pGen), "Gen %s is not in the dictionary.", pGen ? pGen->pchName : "NULL"))
			return NULL;

		// Create new window
		pWindow = StructCreate(parse_UIGenWindow);
		SET_HANDLE_FROM_REFERENT(s_hWindowDict, pWindowDef, pWindow->hDef);
		SET_HANDLE_FROM_REFERENT(g_GenState.hGenDict, pGen, pWindow->hTemplate);
		pWindow->pInstance = ui_GenClone(pGen);
		pWindow->pInstance->chClone = chClone;
		pWindow->chClone = chClone;
	}

	return pWindow;
}

UIGenWindow *ui_GenWindowManagerCreateWindow(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen)
{
	UIGenWindow *pWindow;

	if (!pWindowDef || !pGen)
		return NULL;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	pWindow = ui_GenWindowManagerFindWindow(pWindowManager, pWindowDef, pGen, 0);

	if (!pWindow)
	{
		if (!devassertmsgf(ui_GenInDictionary(pGen), "Gen %s is not in the dictionary.", pGen ? pGen->pchName : "NULL"))
			return NULL;

		// Create new window
		pWindow = StructCreate(parse_UIGenWindow);
		SET_HANDLE_FROM_REFERENT(s_hWindowDict, pWindowDef, pWindow->hDef);
		SET_HANDLE_FROM_REFERENT(g_GenState.hGenDict, pGen, pWindow->hTemplate);
		pWindow->chClone = 0;
	}

	return pWindow;
}

S32 ui_GenWindowManagerGetNextClone(UIGenWindowManager *pWindowManager, UIGen *pGen)
{
	U32 aiUsedClones[UI_GEN_MAX_WINDOWS / 32];
	UIGenWindow *pWindow;
	S32 i;

	if (!pGen)
		return -1;

	if (!devassertmsgf(ui_GenInDictionary(pGen), "Gen %s is not in the dictionary.", pGen ? pGen->pchName : "NULL"))
		return -1;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	memset(aiUsedClones, 0, sizeof(aiUsedClones));
	for (i = 0; i < eaSize(&pWindowManager->eaActiveWindows); i++)
	{
		pWindow = pWindowManager->eaActiveWindows[i];
		if (pWindow->pInstance && GET_REF(pWindow->hTemplate) == pGen)
			SETB(aiUsedClones, pWindow->chClone);
	}

	for (i = 0; i < UI_GEN_MAX_WINDOWS; i++)
	{
		if (!TSTB(aiUsedClones, i))
			return i;
	}

	return UI_GEN_MAX_WINDOWS;
}

void ui_GenWindowManagerPersistWindows(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen)
{
	U32 aiUsedClones[UI_GEN_MAX_WINDOWS / 32];
	S32 i;

	if (!pWindowDef || !pGen)
		return;
	if (!pWindowDef->bPersistOpen)
		return;
	if (!g_GenState.cbOpenWindowsSet)
		return;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	// Figure out which clone's have been used.
	memset(aiUsedClones, 0, sizeof(aiUsedClones));
	for (i = 0; i < eaSize(&pWindowManager->eaActiveWindows); i++)
	{
		UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
		if (GET_REF(pWindow->hDef) == pWindowDef && GET_REF(pWindow->hTemplate) == pGen)
			SETB(aiUsedClones, pWindow->chClone);
	}

	g_GenState.cbOpenWindowsSet(pGen->pchName, MAX(1, pWindowDef->iClones), aiUsedClones);
}

void ui_GenWindowManagerOpenPersisted(UIGenWindowManager *pWindowManager)
{
	U32 aiUsedClones[UI_GEN_MAX_WINDOWS / 32];
	const char **eaOpenWindowNames = NULL;
	int i, j;

	if (!g_GenState.cbOpenWindowsGet || !g_GenState.cbOpenWindowsNames)
		return;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	eaStackCreate(&eaOpenWindowNames, 1000);
	g_GenState.cbOpenWindowsNames(&eaOpenWindowNames);

	// Go through the open window names and check to see if there are windows that need to be opened
	for (i = 0; i < eaSize(&eaOpenWindowNames); i++)
	{
		UIGen *pGen = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, eaOpenWindowNames[i]);
		UIGenWindowDef *pWindowDef = ui_GenGetWindowDef(pGen);
		if (pWindowDef && pWindowDef->bPersistOpen)
		{
			g_GenState.cbOpenWindowsGet(pGen->pchName, MAX(1, pWindowDef->iClones), aiUsedClones);

			for (j = MAX(1, pWindowDef->iClones) - 1; j >= 0; j--)
			{
				UIGenWindow *pWindow = NULL;

				if (TSTB(aiUsedClones, j))
				{
					if (pWindowDef->iClones)
						pWindow = ui_GenWindowManagerCreateClonedWindow(pWindowManager, pWindowDef, pGen, (U8) j);
					else
						pWindow = ui_GenWindowManagerCreateWindow(pWindowManager, pWindowDef, pGen);
				}

				if (pWindow)
				{
					ui_GenWindowManagerAddWindow(pWindowManager, pWindow);
					ui_GenWindowManagerShow(pWindowManager, pWindow);
				}
			}
		}
	}

	eaDestroy(&eaOpenWindowNames);
}

UIGenWindow *ui_GenWindowManagerGetWindow(UIGenWindowManager *pWindowManager, UIGen *pGen)
{
	UIGenWindow *pLastClone = NULL;
	S32 i;

	if (!pGen)
		return NULL;
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	for (i = eaSize(&pWindowManager->eaActiveWindows) - 1; i >= 0; i--)
	{
		UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
		UIGenWindowDef *pDef = GET_REF(pWindow->hDef);
		if (pDef)
		{
			if (pDef->iClones && pWindow->pInstance == pGen)
			{
				return pWindow;
			}
			else if (GET_REF(pWindow->hTemplate) == pGen)
			{
				if (!pDef->iClones)
					return pWindow;
				if (!pLastClone)
					pLastClone = pWindow;
			}
		}
	}

	return pLastClone;
}

bool ui_GenWindowManagerAdd(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, S32 iWindowClone)
{
	UIGenWindow *pWindow;
	bool bAdded = false;

	if (!pWindowDef || !pGen)
		return false;
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (!ui_GenInDictionary(pGen))
	{
		Errorf("%s: Trying to add a gen that is not in the dictionary as a window.", pGen->pchFilename);
		return false;
	}

	if (pWindowDef->iClones > 1)
	{
		S32 iClone = iWindowClone >= 0 ? iWindowClone : ui_GenWindowManagerGetNextClone(pWindowManager, pGen);

		// Out of clones
		if (iClone >= UI_GEN_MAX_WINDOWS || iClone >= pWindowDef->iClones)
			return false;

		pWindow = ui_GenWindowManagerCreateClonedWindow(pWindowManager, pWindowDef, pGen, (U8) iClone);
	}
	else
	{
		pWindow = ui_GenWindowManagerCreateWindow(pWindowManager, pWindowDef, pGen);
	}

	if (pWindow && ui_GenWindowManagerAddWindow(pWindowManager, pWindow))
	{
		ui_GenWindowManagerShow(pWindowManager, pWindow);
		ui_GenWindowManagerPersistWindows(pWindowManager, pWindowDef, pGen);
		return true;
	}
	return false;
}

bool ui_GenWindowManagerAddPos(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, S32 iWindowClone, float fPercentX, float fPercentY)
{
	UIGenWindow *pWindow;
	bool bAdded = false;

	if (!pWindowDef || !pGen)
		return false;
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (!ui_GenInDictionary(pGen))
	{
		Errorf("%s: Trying to add a gen that is not in the dictionary as a window.", pGen->pchFilename);
		return false;
	}

	if (pWindowDef->iClones > 1)
	{
		S32 iClone = iWindowClone >= 0 ? iWindowClone : ui_GenWindowManagerGetNextClone(pWindowManager, pGen);

		// Out of clones
		if (iClone >= UI_GEN_MAX_WINDOWS || iClone >= pWindowDef->iClones)
			return false;

		pWindow = ui_GenWindowManagerCreateClonedWindow(pWindowManager, pWindowDef, pGen, (U8) iClone);
	}
	else
	{
		pWindow = ui_GenWindowManagerCreateWindow(pWindowManager, pWindowDef, pGen);
	}

	if (pWindow && ui_GenWindowManagerAddWindow(pWindowManager, pWindow))
	{
		// Initialize the position
		UIGen *pInstance = ui_GenWindowGetGen(pWindow);
		if (pInstance)
		{
			UIGenInternal *pOverride = ui_GenGetCodeOverrideEarly(pInstance, true);
			ParseTable *pTable = ui_GenGetType(pInstance);
			pOverride->pos.fPercentX = fPercentX;
			pOverride->pos.fPercentY = fPercentY;
			TokenSetSpecified(pTable, s_iPercentXColumn, pOverride, -1, true);
			TokenSetSpecified(pTable, s_iPercentYColumn, pOverride, -1, true);
			ui_GenMarkDirty(pInstance);
			pWindow->bSetPosition = true;
		}

		ui_GenWindowManagerShow(pWindowManager, pWindow);
		ui_GenWindowManagerPersistWindows(pWindowManager, pWindowDef, pGen);
		return true;
	}
	return false;
}

bool ui_GenWindowManagerRemove(UIGenWindowManager *pWindowManager, UIGen *pGen, bool bForce)
{
	UIGenWindow *pWindow;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	pWindow = ui_GenWindowManagerGetWindow(pWindowManager, pGen);
	if (pWindow)
	{
		// If it's a cloned window, always force the removal.
		if (pWindow->pInstance)
			bForce = true;

		ui_GenWindowManagerHide(pWindowManager, pWindow, bForce);

		if (ui_GenWindowManagerRemoveWindow(pWindowManager, pWindow))
		{
			ui_GenWindowManagerPersistWindows(pWindowManager, GET_REF(pWindow->hDef), GET_REF(pWindow->hTemplate));
		}

		if (pWindow->bSetPosition)
		{
			UIGen *pInstance = ui_GenWindowGetGen(pWindow);
			if (pInstance)
			{
				UIGenInternal *pOverride = ui_GenGetCodeOverrideEarly(pInstance, true);
				ParseTable *pTable = ui_GenGetType(pInstance);
				TokenSetSpecified(pTable, s_iPercentXColumn, pOverride, -1, false);
				TokenSetSpecified(pTable, s_iPercentYColumn, pOverride, -1, false);
				ui_GenMarkDirty(pInstance);
			}
		}

		StructDestroy(parse_UIGenWindow, pWindow);
		return true;
	}
	return false;
}

bool ui_GenWindowManagerShow(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow)
{
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (pWindow)
	{
		UIGenWindowDef *pDef = GET_REF(pWindow->hDef);
		UIGen *pGen = ui_GenWindowGetGen(pWindow);
		bool bModal = pDef != NULL && pDef->bModal;
		if ((!UI_GEN_READY(pGen) || !pWindow->bVisible) && ui_GenWindowManagerIsVisible(pWindowManager, pDef))
		{
			pWindow->bVisible = true;

			if (bModal)
				return ui_GenLayersAddModalPopup(pGen);
			return ui_GenLayersAddWindow(pGen);
		}
	}
	return false;
}

bool ui_GenWindowManagerHide(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow, bool bForce)
{
	UIGen *pGen = ui_GenWindowGetGen(pWindow);
	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (pWindow && pGen)
	{
		pWindow->bVisible = false;

		return ui_GenLayersRemoveWindow(pGen, bForce);
	}
	return false;
}

bool ui_GenWindowManagerIsVisible(UIGenWindowManager *pWindowManager, UIGenWindowDef *pDef)
{
	S32 i;

	if (!pWindowManager)
		pWindowManager = g_ui_pGenWindowManager;

	if (pDef)
	{
		for (i = 0; i < eaiSize(&pDef->eaiHideStates); i++)
		{
			UIGenState eState = eaiGet(&pDef->eaiHideStates, i);
			if (TSTB(pWindowManager->bfStates, eState))
				return false;
		}

		for (i = 0; i < eaiSize(&pDef->eaiRequireStates); i++)
		{
			UIGenState eState = eaiGet(&pDef->eaiRequireStates, i);
			if (!TSTB(pWindowManager->bfStates, eState))
				return false;
		}

		if (!eaiSize(&pDef->eaiAllowStates))
			return true;

		for (i = 0; i < eaiSize(&pDef->eaiAllowStates); i++)
		{
			UIGenState eState = eaiGet(&pDef->eaiAllowStates, i);
			if (TSTB(pWindowManager->bfStates, eState))
				return true;
		}
	}

	return false;
}

static bool s_bForceTick = false;
void ui_GenWindowManagerForceTick()
{
	s_bForceTick = true;
}

///////////////////////////////////////////////////////////////////////////////
// Existing Window API

bool ui_GenAddWindow(UIGen *pGen)
{
	if (pGen)
	{
		UIGenWindowDef *pWindow = ui_GenGetWindowDef(pGen);
		if (!pWindow)
			pWindow = ui_GetDefaultWindow();

		if (devassertmsg(pWindow, "DefaultWindow could not be created"))
		{
			return ui_GenWindowManagerAdd(NULL, pWindow, pGen, -1);
		}
	}
	return false;
}

bool ui_GenAddWindowPos(UIGen *pGen, float fPercentX, float fPercentY)
{
	if (pGen)
	{
		UIGenWindowDef *pWindow = ui_GenGetWindowDef(pGen);
		if (!pWindow)
			pWindow = ui_GetDefaultWindow();

		if (devassertmsg(pWindow, "DefaultWindow could not be created"))
		{
			return ui_GenWindowManagerAddPos(NULL, pWindow, pGen, -1, fPercentX, fPercentY);
		}
	}
	return false;
}

bool ui_GenAddWindowId(UIGen *pGen, S32 iWindowId)
{
	if (pGen && iWindowId >= 0)
	{
		UIGenWindowDef *pWindow = ui_GenGetWindowDef(pGen);
		if (!pWindow)
			pWindow = ui_GetDefaultWindow();

		if (devassertmsg(pWindow, "DefaultWindow could not be created"))
		{
			return ui_GenWindowManagerAdd(NULL, pWindow, pGen, iWindowId);
		}
	}
	return false;
}

bool ui_GenAddWindowIdPos(UIGen *pGen, S32 iWindowId, float fPercentX, float fPercentY)
{
	if (pGen && iWindowId >= 0)
	{
		UIGenWindowDef *pWindow = ui_GenGetWindowDef(pGen);
		if (!pWindow)
			pWindow = ui_GetDefaultWindow();

		if (devassertmsg(pWindow, "DefaultWindow could not be created"))
		{
			return ui_GenWindowManagerAddPos(NULL, pWindow, pGen, iWindowId, fPercentX, fPercentY);
		}
	}
	return false;
}

bool ui_GenRemoveWindow(UIGen *pGen, bool bForce)
{
	return ui_GenWindowManagerRemove(NULL, pGen, bForce);
}

bool ui_GenAddModalPopup(UIGen *pGen)
{
	if (pGen)
	{
		UIGenWindowDef *pWindow = ui_GenGetWindowDef(pGen);
		if (!pWindow)
			pWindow = ui_GetDefaultModal();

		if (devassertmsg(pWindow, "DefaultModal could not be created"))
		{
			return ui_GenWindowManagerAdd(NULL, pWindow, pGen, -1);
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////
// Widget

static void ui_GenWindowManagerPerformTick(UIGenWindowManager *pWindowManager)
{
	S32 i;
	bool bChanged = s_bForceTick;
	bool bForceRemove = true;

	for (i = 0; i < eaiSize(&g_GenState.eaiGlobalOffStates); i++)
	{
		UIGenState eState = eaiGet(&g_GenState.eaiGlobalOffStates, i);
		if (TSTB(pWindowManager->bfStates, eState))
		{
			CLRB(pWindowManager->bfStates, eState);
			bChanged = true;
		}
	}

	for (i = 0; i < eaiSize(&g_GenState.eaiGlobalOnStates); i++)
	{
		UIGenState eState = eaiGet(&g_GenState.eaiGlobalOnStates, i);
		if (!TSTB(pWindowManager->bfStates, eState))
		{
			SETB(pWindowManager->bfStates, eState);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		s_bForceTick = false;
		ui_GenWindowManagerOpenPersisted(pWindowManager);

		for (i = 0; i < eaSize(&pWindowManager->eaActiveWindows); i++)
		{
			UIGenWindow *pWindow = pWindowManager->eaActiveWindows[i];
			UIGen *pGen = ui_GenWindowGetGen(pWindow);
			bool bVisible = ui_GenWindowManagerIsVisible(pWindowManager, GET_REF(pWindow->hDef));

			if (UI_GEN_READY(pGen) && !bVisible)
			{
				ui_GenWindowManagerHide(pWindowManager, pWindow, bForceRemove);
			}
			else if (!UI_GEN_READY(pGen) && bVisible)
			{
				ui_GenWindowManagerShow(pWindowManager, pWindow);
			}
		}
	}
}

void ui_GenWindowManagerTick(UIGenWindowManager *pWindowManager, UI_PARENT_ARGS)
{
	// Respond to any state changes made in prior widget tick callbacks,
	// which in theory are only Root and Jails (and possibly future widgets
	// that need to run before).
	ui_GenWindowManagerPerformTick(pWindowManager);
}

void ui_GenWindowManagerDraw(UIGenWindowManager *pWindowManager, UI_PARENT_ARGS)
{
}

void ui_GenWindowManagerFree(UIGenWindowManager *pWindowManager)
{
	eaDestroyStruct(&pWindowManager->eaActiveWindows, parse_UIGenWindow);
}

UIGenWindowManager *ui_GenWindowManagerCreate(void)
{
	UIGenWindowManager *pWindowManager = calloc(1, sizeof(UIGenWindowManager));
	ui_WidgetInitialize(UI_WIDGET(pWindowManager), ui_GenWindowManagerTick, ui_GenWindowManagerDraw, ui_GenWindowManagerFree, NULL, NULL);
	pWindowManager->widget.uUIGenWidget = 1;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pWindowManager), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	return pWindowManager;
}

///////////////////////////////////////////////////////////////////////////////
// Expressions

AUTO_EXPR_CMD(UIGen) ACMD_NAME(GenWindowManagerRemoveAll);
bool ui_GenExprWindowManagerRemoveAll(void)
{
	UIGenWindowManager *pWindowManager = g_ui_pGenWindowManager;
	S32 iRemoved = 0;

	while (eaSize(&pWindowManager->eaActiveWindows) >= 0)
	{
		UIGenWindow *pWindow = eaPop(&pWindowManager->eaActiveWindows);
		ui_GenWindowManagerHide(pWindowManager, pWindow, false);
		StructDestroy(parse_UIGenWindow, pWindow);
		iRemoved++;
	}

	return iRemoved > 0;
}

// Get a gen by name.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWindow);
SA_RET_NN_VALID UIGen *ui_GenExprFindWindow(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchName, S32 iWindowId)
{
	UIGenWindowManager *pWindowManager = g_ui_pGenWindowManager;
	UIGen *pGen = NULL;
	UIGenWindow *pWindow = NULL;
	static const char *s_pchLastName;
	static S32 s_iLastWindowId;
	static UIGen *s_pLastGen;
	static U32 s_uiFrame;

	if (pchName == s_pchLastName && s_iLastWindowId == iWindowId && s_uiFrame == g_ui_State.uiFrameCount)
		return s_pLastGen;
	if (!(pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, pchName)))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "%s: Not found in UIGen reference dictionary. This should not have passed static check. Defaulting to root, this will go poorly!", pchName);
		pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, "Root");
		assert(pGen);
	}
	else
	{
		S32 i;
		for (i = 0; i < eaSize(&pWindowManager->eaActiveWindows); i++)
		{
			if (GET_REF(pWindowManager->eaActiveWindows[i]->hTemplate) == pGen
				&& pWindowManager->eaActiveWindows[i]->chClone == iWindowId)
			{
				pWindow = pWindowManager->eaActiveWindows[i];
				break;
			}
		}
	}
	s_iLastWindowId = iWindowId;
	s_uiFrame = g_ui_State.uiFrameCount;
	s_pchLastName = pchName;
	s_pLastGen = pWindow ? ui_GenWindowGetGen(pWindow) : pGen;
	return s_pLastGen;
}

// See if a window is open.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWindowOpen);
bool ui_GenExprWindowOpen(ExprContext *pContext, ACMD_EXPR_DICT("UIGen") const char *pchName, S32 iWindowId)
{
	UIGenWindowManager *pWindowManager = g_ui_pGenWindowManager;
	UIGen *pGen = NULL;
	UIGenWindow *pWindow = NULL;
	static const char *s_pchLastName;
	static S32 s_iLastWindowId;
	static bool s_bOpen;
	static U32 s_uiFrame;

	if (pchName == s_pchLastName && s_iLastWindowId == iWindowId && s_uiFrame == g_ui_State.uiFrameCount)
		return s_bOpen;
	if (!(pGen = RefSystem_ReferentFromString(g_GenState.hGenDict, pchName)))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "%s: Not found in UIGen reference dictionary. This should not have passed static check. Defaulting to root, this will go poorly!", pchName);
	}
	else
	{
		S32 i;
		for (i = 0; i < eaSize(&pWindowManager->eaActiveWindows); i++)
		{
			if (GET_REF(pWindowManager->eaActiveWindows[i]->hTemplate) == pGen
				&& pWindowManager->eaActiveWindows[i]->chClone == iWindowId)
			{
				pWindow = pWindowManager->eaActiveWindows[i];
				break;
			}
		}
	}
	s_iLastWindowId = iWindowId;
	s_uiFrame = g_ui_State.uiFrameCount;
	s_pchLastName = pchName;
	s_bOpen = pWindow != NULL;
	return pWindow != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Initialization

void ui_GenSetOpenWindowsCallbacks(UIGenGetOpenWindows cbOpenWindowsNames, UIGenOpenWindows cbOpenWindowsGet, UIGenOpenWindows cbOpenWindowsSet)
{
	g_GenState.cbOpenWindowsNames = cbOpenWindowsNames;
	g_GenState.cbOpenWindowsGet = cbOpenWindowsGet;
	g_GenState.cbOpenWindowsSet = cbOpenWindowsSet;
}

void ui_GenWindowManagerOncePerFrameInput(void)
{
	// Force showing & hiding the windows before the pointer update phase,
	// (preferably) before the UI has a chance to run any expression code.
	ui_GenWindowManagerPerformTick(g_ui_pGenWindowManager);
}

void ui_GenWindowManagerLoad(void)
{
	resDictManageValidation(s_hWindowDict, GenWindowDefResValidate);
	resDictRegisterEventCallback(s_hWindowDict, GenWindowDefResEvent, g_ui_pGenWindowManager);
	resDictRegisterEventCallback(g_GenState.hGenDict, GenReloadEvent, g_ui_pGenWindowManager);
	resLoadResourcesFromDisk(s_hWindowDict, "ui/gens/", ".window", NULL, PARSER_OPTIONALFLAG);

	// Initialize the default window defs if they weren't overriden
	devassertmsg(ui_GetDefaultWindow(), "Failed to create UIGenWindowDef DefaultWindow");
	devassertmsg(ui_GetDefaultModal(), "Failed to create UIGenWindowDef DefaultModal");
}

AUTO_RUN;
void ui_GenWindowManagerInit(void)
{
	s_hWindowDict = RefSystem_RegisterSelfDefiningDictionary("UIGenWindowDef", false, parse_UIGenWindowDef, true, true, NULL);
	g_ui_pGenWindowManager = ui_GenWindowManagerCreate();
	ui_WidgetSetName(UI_WIDGET(g_ui_pGenWindowManager), "Default_Gen_WindowManager");

	assertmsg((s_iPercentXColumn = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.fPercentX))) > 0, "Unable to find column for PercentX");
	assertmsg((s_iPercentYColumn = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.fPercentY))) > 0, "Unable to find column for PercentY");
}

#include "UIGenWindowManager_h_ast.c"
