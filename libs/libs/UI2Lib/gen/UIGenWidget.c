#include "textparser.h"
#include "UIGenWidget.h"
#include "UIGenPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_GenWidgetUpdateName(UIGenWidget *pGenWidget)
{
	if (IS_HANDLE_ACTIVE(pGenWidget->hGen))
	{
		char achName[256];
		sprintf(achName, "GEN_%s", REF_STRING_FROM_HANDLE_KNOWN_NONNULL(pGenWidget->hGen));
		ui_WidgetSetName(UI_WIDGET(pGenWidget), achName);
	}
	else
	{
		ui_WidgetSetName(UI_WIDGET(pGenWidget), NULL);
	}
}

UIGen *ui_GenWidgetGetGen(UIGenWidget *pGenWidget)
{
	return GET_REF(pGenWidget->hGen);
}

void ui_GenWidgetFree(UIGenWidget *pGenWidget)
{
	if( pGenWidget->bModalLayer )
	{
		ui_GenModalDeleted();
	}

	ui_GenWidgetSetGen(pGenWidget, NULL);
	ui_GenClearPointerUpdateCallback(pGenWidget);
	ui_WidgetFreeInternal(UI_WIDGET(pGenWidget));
}

void ui_GenWidgetSetGen(UIGenWidget *pGenWidget, UIGen *pGen)
{
	UIGen *pOldGen = ui_GenWidgetGetGen(pGenWidget);
	if (UI_GEN_NON_NULL(pOldGen) && pOldGen != pGen)
		ui_GenReset(pOldGen);
	if (!pGen || pGen == RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pGen->pchName))
	{
		SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pGen, pGenWidget->hGen);
		ui_GenWidgetUpdateName(pGenWidget);
	}
	else
	{
		assertmsg(0, "Gens in widgets must be in the dictionary");
	}
}

void ui_GenWidgetTick(UIGenWidget *pGenWidget, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenWidgetGetGen(pGenWidget);
	UI_GET_COORDINATES(pGenWidget);
	pGenWidget->fake.ScreenBox = box;
	pGenWidget->fake.fScale = g_GenState.fScale;
	if (UI_GEN_NON_NULL(pGen))
	{
		ui_GenUpdateCB(pGen, &pGenWidget->fake);
		ui_GenLayoutCB(pGen, &pGenWidget->fake);
		ui_GenManageTopLevel(pGen);
	}
}

void ui_GenWidgetFamilyRemoved(UIGenWidget *pGenWidget, UI_PARENT_ARGS)
{
	UIGen *pGen = ui_GenWidgetGetGen(pGenWidget);
	UI_GET_COORDINATES(pGenWidget);
	pGenWidget->fake.ScreenBox = box;
	pGenWidget->fake.fScale = g_GenState.fScale;
	if (UI_GEN_NON_NULL(pGen))
	{
		// Do minimal ticking as a just in case
		ui_GenUpdateCB(pGen, &pGenWidget->fake);

		ui_GenReset(pGen);
	}
}

void ui_GenWidgetDraw(UIGenWidget *pGenWidget, UI_PARENT_ARGS)
{
}

void ui_GenWidgetPointerUpdate(UIGenWidget *pGenWidget)
{
	UIGen* pGen = GET_REF(pGenWidget->hGen);
	ui_GenPointerUpdateCB(pGen, &pGenWidget->fake);
}

extern ParseTable parse_UIGenInternal[];
#define TYPE_parse_UIGenInternal UIGenInternal

UIGenWidget *ui_GenWidgetCreate(const char *pchGen, char chLayer)
{
	UIGenWidget *pGenWidget = calloc(1, sizeof(*pGenWidget));
	ui_WidgetInitialize(UI_WIDGET(pGenWidget), ui_GenWidgetTick, ui_GenWidgetDraw, ui_GenWidgetFree, NULL, NULL);
	pGenWidget->widget.familyRemovedF = ui_GenWidgetFamilyRemoved;
	pGenWidget->widget.uUIGenWidget = 1;
	if (pchGen)
	{
		SET_HANDLE_FROM_STRING(UI_GEN_DICTIONARY, pchGen, pGenWidget->hGen);
		ui_GenWidgetUpdateName(pGenWidget);
	}
	ui_GenSetPointerUpdateCallback(pGenWidget, ui_GenWidgetPointerUpdate);
	pGenWidget->fake.pResult = StructCreate(parse_UIGenInternal);
	pGenWidget->fake.chAlpha = 255;
	pGenWidget->fake.chLayer = chLayer;
	pGenWidget->fake.bIsRoot = true;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGenWidget), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	return pGenWidget;
}
