#include "earray.h"
#include "UIGen.h"
#include "UIGenJail.h"
#include "UIGenWindowManager.h"
#include "UIGenWidget.h"
#include "UIGenPrivate.h"
#include "UIGen_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static struct
{
	UIGenWidget *pModalLayer;
	REF_TO(UIGen) hModalRoot;

	UIGenWidget *pWindowLayer;
	REF_TO(UIGen) hWindowRoot;

	UIGenWidget *pRootLayer;
	REF_TO(UIGen) hRoot;

	UIGenWidget *pCutsceneLayer;
	REF_TO(UIGen) hCutscene;

	int s_iModalWindows;
} s_GenLayers;

static UIGen *GenGetLayer(const char *pchName)
{
	UIGen *pLayer = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchName);
	if (!pLayer)
	{
		pLayer = ui_GenCreate(pchName, kUIGenTypeBox);
		pLayer->pBase->pos.Width.fMagnitude = 1.f;
		pLayer->pBase->pos.Width.eUnit = UIUnitPercentage;
		pLayer->pBase->pos.Height.fMagnitude = 1.f;
		pLayer->pBase->pos.Height.eUnit = UIUnitPercentage;
		ui_GenReset(pLayer);
		RefSystem_AddReferent(UI_GEN_DICTIONARY, pLayer->pchName, pLayer);
	}
	return pLayer;
}

//////////////////////////////////////////////////////////////////////////
// Manage a layer of gens that is modal, i.e. when it's up nothing not on
// its layer can be interacted with. Most things on the modal layer
// should be movable boxes, but I guess this is not required.

static void GenModalWidgetTick(UIGenWidget *pGenWidget, UI_PARENT_ARGS)
{
	ui_WidgetGroupSteal(UI_WIDGET(pGenWidget)->group, UI_WIDGET(pGenWidget));
	ui_GenWidgetTick(pGenWidget, UI_PARENT_VALUES);
}

bool ui_GenModalPending(void)
{
	return s_GenLayers.s_iModalWindows > 0;
}

void ui_GenModalShow(void)
{
	UIGen *pModal = GET_REF(s_GenLayers.hModalRoot);
	if (!s_GenLayers.pModalLayer)
	{
		s_GenLayers.pModalLayer = ui_GenWidgetCreate("Modal_Root", kUIGenLayerModal);
		UI_WIDGET(s_GenLayers.pModalLayer)->tickF = GenModalWidgetTick;
		UI_WIDGET(s_GenLayers.pModalLayer)->priority = 2;
		s_GenLayers.pModalLayer->bModalLayer = true;
	}
	ui_WidgetAddToDevice(UI_WIDGET(s_GenLayers.pModalLayer), NULL);
	ui_WidgetGroupSteal(UI_WIDGET(s_GenLayers.pModalLayer)->group, UI_WIDGET(s_GenLayers.pModalLayer));
}

void ui_GenModalHide(void)
{
	if (s_GenLayers.pModalLayer)
		ui_WidgetDestroy(&s_GenLayers.pModalLayer);
}

void ui_GenModalDeleted(void)
{
	s_GenLayers.s_iModalWindows = 0;
}

bool ui_GenLayersAddModalPopup(UIGen *pGen)
{
	UIGen *pModal = GET_REF(s_GenLayers.hModalRoot);
	bool bResult = ui_GenAddEarlyOverrideChild(pModal, pGen);
	GEN_PRINTF("AddModalPopup: Adding %s, count is %d.", pGen ? pGen->pchName : NULL, s_GenLayers.s_iModalWindows);
	if (bResult)
		s_GenLayers.s_iModalWindows++;
	if (s_GenLayers.s_iModalWindows)
		ui_GenModalShow();
	GEN_PRINTF("AddModalPopup: Added %s, count is %d.", pGen ? pGen->pchName : NULL, s_GenLayers.s_iModalWindows);
	return bResult;
}

void ui_GenModalInitialize(void)
{
	UIGen *pLayer = GenGetLayer("Modal_Root");
	if (pLayer->pBase)
		pLayer->pBase->bCaptureMouse = true;
	SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pLayer, s_GenLayers.hModalRoot);
	pLayer->bIsRoot = true;
	pLayer->bTopLevelChildren = true;
}

//////////////////////////////////////////////////////////////////////////
// Manage a layer of gens that is not modal but is still separate.
// Used for things like chat or trade windows.

void ui_GenWindowInitialize(void)
{
	UIGen *pLayer = GenGetLayer("Window_Root");
	SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pLayer, s_GenLayers.hWindowRoot);
	s_GenLayers.pWindowLayer = ui_GenWidgetCreate(pLayer->pchName, kUIGenLayerWindow);
	pLayer->bIsRoot = true;
	pLayer->bTopLevelChildren = true;
}

bool ui_GenLayersAddWindow(UIGen *pGen)
{
	UIGen *pLayer = GET_REF(s_GenLayers.hWindowRoot);
	GEN_PRINTF("AddWindow: Adding %s.", pGen ? pGen->pchName : NULL);
	return ui_GenAddEarlyOverrideChild(pLayer, pGen);
}

bool ui_GenLayersRemoveWindow(UIGen *pGen, bool bForce)
{
	UIGen *pWindow = GET_REF(s_GenLayers.hWindowRoot);
	UIGen *pModal = GET_REF(s_GenLayers.hModalRoot);
	GEN_PRINTF("RemoveWindow: Removing %s.", pGen ? pGen->pchName : NULL);
	if (ui_GenRemoveChild(pWindow, pGen, bForce))
	{
		GEN_PRINTF("RemoveWindow: Removed %s from the window layer.", pGen ? pGen->pchName : NULL);
		if (bForce)
			ui_GenQueueReset(pGen);
		return true;
	}
	else if (ui_GenRemoveChild(pModal, pGen, bForce))
	{
		if (bForce)
			ui_GenQueueReset(pGen);
		if (s_GenLayers.s_iModalWindows > 0)
			s_GenLayers.s_iModalWindows--;
		// Do not hide the modal root here even if nothing is left; something
		// may be added in the same frame before it actually gets reset. It will
		// be removed at the top of the next frame if no modal windows remain.
		GEN_PRINTF("RemoveWindow: Removed %s from the modal layer. %d remain.", pGen ? pGen->pchName : NULL, s_GenLayers.s_iModalWindows);
		return true;
	}
	else if (ui_GenJailKeeperRemove(NULL, pGen))
	{
		GEN_PRINTF("RemoveWindow: Removed %s from a jail.", pGen ? pGen->pchName : NULL);
		return true;
	}
	else
		return false;
}

//////////////////////////////////////////////////////////////////////////
// Root layer, where most things go...

void ui_GenRootInitialize(void)
{
	UIGen *pLayer = GenGetLayer("Root");
	SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pLayer, s_GenLayers.hRoot);
	s_GenLayers.pRootLayer = ui_GenWidgetCreate(pLayer->pchName, kUIGenLayerRoot);
	pLayer->bIsRoot = true;
}

//////////////////////////////////////////////////////////////////////////
// Cutscene layer

void ui_GenCutsceneInitialize(void)
{
	UIGen *pLayer = GenGetLayer("Cutscene_Root");
	SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pLayer, s_GenLayers.hCutscene);
	s_GenLayers.pCutsceneLayer = ui_GenWidgetCreate(pLayer->pchName, kUIGenLayerCutscene);
	pLayer->bIsRoot = true;
	pLayer->bIsCutsceneRoot = true;
}

//////////////////////////////////////////////////////////////////////////

void ui_GenLayersInitialize(void)
{
	ui_GenRootInitialize();
	ui_GenWindowInitialize();
	ui_GenModalInitialize();
	ui_GenCutsceneInitialize();

	// Widgets are ticked in reverse order they are used here
	//   * Window manager needs to run before WindowLayer and ModalLayer
	//   * ModalLayer is priority 2
	//   * Window manager should run after Jails and Root, so that they have
	//     a chance to set any global states that windows depend on. So that
	//     windows will open this frame.
	//   * Jails should run after Root, so that Root has a chance to manage
	//     any state that Jails depend on.

	UI_WIDGET(s_GenLayers.pWindowLayer)->priority = 3;
	UI_WIDGET(g_ui_pGenWindowManager)->priority = 4;
	UI_WIDGET(g_ui_pDefaultKeeper)->priority = 5;
	UI_WIDGET(s_GenLayers.pRootLayer)->priority = 6;

	ui_WidgetAddToDevice(UI_WIDGET(s_GenLayers.pWindowLayer), NULL);
	ui_WidgetAddToDevice(UI_WIDGET(g_ui_pGenWindowManager), NULL);
	ui_WidgetAddToDevice(UI_WIDGET(g_ui_pDefaultKeeper), NULL);
	ui_WidgetAddToDevice(UI_WIDGET(s_GenLayers.pRootLayer), NULL);
	ui_WidgetAddToDevice(UI_WIDGET(s_GenLayers.pCutsceneLayer), NULL);

	ui_WidgetGroupSteal(UI_WIDGET(s_GenLayers.pWindowLayer)->group, UI_WIDGET(s_GenLayers.pWindowLayer));
	ui_WidgetGroupSteal(UI_WIDGET(g_ui_pGenWindowManager)->group, UI_WIDGET(g_ui_pGenWindowManager));
	ui_WidgetGroupSteal(UI_WIDGET(g_ui_pDefaultKeeper)->group, UI_WIDGET(g_ui_pDefaultKeeper));
	ui_WidgetGroupSteal(UI_WIDGET(s_GenLayers.pRootLayer)->group, UI_WIDGET(s_GenLayers.pRootLayer));
	ui_WidgetGroupSteal(UI_WIDGET(s_GenLayers.pCutsceneLayer)->group, UI_WIDGET(s_GenLayers.pCutsceneLayer));

	ui_WidgetSetFamily(UI_WIDGET(s_GenLayers.pWindowLayer), UI_FAMILY_GAME);
	ui_WidgetSetFamily(UI_WIDGET(g_ui_pGenWindowManager), UI_FAMILY_GAME);
	ui_WidgetSetFamily(UI_WIDGET(g_ui_pDefaultKeeper), UI_FAMILY_GAME);
	ui_WidgetSetFamily(UI_WIDGET(s_GenLayers.pRootLayer), UI_FAMILY_GAME);
	ui_WidgetSetFamily(UI_WIDGET(s_GenLayers.pCutsceneLayer), UI_FAMILY_CUTSCENE);
}

void ui_GenLayersReset(void)
{
	if (UI_GEN_READY(GET_REF(s_GenLayers.hRoot)))
		ui_GenReset(GET_REF(s_GenLayers.hRoot));
	if (UI_GEN_READY(GET_REF(s_GenLayers.hWindowRoot)))
		ui_GenReset(GET_REF(s_GenLayers.hWindowRoot));
	if (UI_GEN_READY(GET_REF(s_GenLayers.hModalRoot)))
		ui_GenReset(GET_REF(s_GenLayers.hModalRoot));
	if (UI_GEN_READY(GET_REF(s_GenLayers.hCutscene)))
		ui_GenReset(GET_REF(s_GenLayers.hCutscene));
	s_GenLayers.s_iModalWindows = 0;
	ui_GenWindowManagerForceTick();
}
