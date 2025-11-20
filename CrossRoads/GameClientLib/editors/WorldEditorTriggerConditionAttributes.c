#ifndef NO_EDITORS

#include "WorldEditorTriggerConditionAttributes.h"

#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "Expression.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

int wleAETriggerConditionReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"TriggerCondition", parse_WorldTriggerConditionProperties};
	WorldTriggerConditionProperties **ppProps = NULL;
	WorldTriggerConditionProperties *pProp = NULL;
	MEFieldContext *pContext;
	U32 iRetFlags;

	ppProps = (WorldTriggerConditionProperties**)wleAEGetSelectedDataFromPath("TriggerCondition", NULL, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing))
		pProp = ppProps[0];
	else 
		return WLE_UI_PANEL_INVALID;

	pContext = MEContextPushEA("WorldEditor_TriggerCondition", ppProps, ppProps, parse_WorldTriggerConditionProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	MEContextAddExpr(exprContextGetGlobalContext(),	"condBlock", "Condition", "Condition the trigger keeps track of.");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_TriggerCondition");

	return WLE_UI_PANEL_OWNED;
}

#endif
