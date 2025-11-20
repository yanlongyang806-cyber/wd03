#ifndef NO_EDITORS

#include "WorldEditorAmbientJob.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "StringCache.h"

#include "WorldGrid.h"
#include "WorldEditorOperations.h"
#include "WorldEditorOptions.h"
#include "WorldEditorUtil.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "FSMEditorMain.h"
#include "Expression.h"
#include "wlInteraction.h"
#include "wlGroupPropertyStructs.h"
#include "MultiEditFieldContext.h"

#include "autogen/WorldEditorAmbientJob_c_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static int g_AnimCount;

// no reason we can't allow more, but this seems like a sane number for the number of different anims
#define MAX_JOB_ANIMS	8
AUTO_STRUCT;
typedef struct wleAEIntractLocUIData
{
	const char *pchNewAnim;				AST(NAME("NewAnim") POOL_STRING)
} wleAEIntractLocUIData;
wleAEIntractLocUIData g_IntractLocUI = {0};

static bool wleAEIntractLocCritCheck(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, void *data)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		bool ret;
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		const char *eventName = NULL;

		if (tracker && tracker->def && tracker->def->property_structs.sound_sphere_properties)
			eventName = tracker->def->property_structs.sound_sphere_properties->pcEventName;
		if (!eventName && tracker && tracker->def && tracker->def->property_structs.client_volume.sound_volume_properties)
			eventName = tracker->def->property_structs.client_volume.sound_volume_properties->event_name;

		if (wleCriterionStringTest(eventName ? eventName : "", val, cond, &ret))
			return ret;
	}
	return false;
}

static bool wleAEIntractLocExclude(GroupTracker *tracker)
{
	GroupDef *def = tracker->def;
	int iAnimCount;

	if(def->property_structs.interact_location_properties) {
		iAnimCount = eaSize(&def->property_structs.interact_location_properties->eaAnims);
	} else {
		iAnimCount = 0;
	}

	if(g_AnimCount == WL_VAL_UNSET) {
		g_AnimCount = iAnimCount;
	} else if (g_AnimCount != iAnimCount) {
		g_AnimCount = WL_VAL_DIFF;
	}

	return false;
}

static void wleAEIntractLocChanged(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(g_IntractLocUI.pchNewAnim) 
		eaPush(&pNewProps->interact_location_properties->eaAnims, g_IntractLocUI.pchNewAnim);
}

static void wleAEIntractLocDeleteAnim(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	int iIdx = pField->arrayIndex;
	assert(iIdx >= 0 && iIdx < eaSize(&pNewProps->interact_location_properties->eaAnims));
	eaRemove(&pNewProps->interact_location_properties->eaAnims, iIdx);
}

static void wleAEIntractLocDeleteAnimButtonCB(UIButton *pButton, MEFieldContextEntry *pEntry)
{
	wleAECallFieldChangedCallback(ENTRY_FIELD(pEntry), wleAEIntractLocDeleteAnim);
}

int wleAEInteractLocationReload(EMPanel *panel, EditorObject *edObj)
{
	static WleCriterion *pCrit = NULL;
	static WleAEPropStructData pPropData = {"InteractLocation", parse_WorldInteractLocationProperties};
	WorldInteractLocationProperties **ppProps = NULL;
	WorldInteractLocationProperties *pProp = NULL;
	MEFieldContext *pContext;
	bool bHasCombatJob = false;
	bool bHasCombatJobSame = true;
	U32 iRetFlags;

	// setup filter criteria
	if(!pCrit) {
		pCrit = StructCreate(parse_WleCriterion);
		eaiPush(&pCrit->allConds, WLE_CRIT_EQUAL);
		eaiPush(&pCrit->allConds, WLE_CRIT_NOT_EQUAL);
		eaiPush(&pCrit->allConds, WLE_CRIT_CONTAINS);
		eaiPush(&pCrit->allConds, WLE_CRIT_BEGINS_WITH);
		eaiPush(&pCrit->allConds, WLE_CRIT_ENDS_WITH);
		pCrit->checkCallback = wleAEIntractLocCritCheck;
		pCrit->propertyName = StructAllocString("Interact Location");
		wleCriterionRegister(pCrit);
	}

	g_AnimCount = WL_VAL_UNSET;
	ppProps = (WorldInteractLocationProperties**)wleAEGetSelectedDataFromPath("InteractLocation", wleAEIntractLocExclude, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing)) {
		pProp = ppProps[0];
	}

	g_IntractLocUI.pchNewAnim = NULL;

	pContext = MEContextPushEA("WorldEditor_InteractLocationProps", ppProps, ppProps, parse_WorldInteractLocationProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAEIntractLocChanged);

	if(pProp) {
		
		MEContextAddDict(kMEFieldType_ValidatedTextEntry, "FSM",	"FSM",					"Ambient FSM",			"FSM dictating how critter behaves in ambient mode.");
		MEContextAddDict(kMEFieldType_ValidatedTextEntry, "FSM",	"SecondaryFSM",			"Combat FSM",			"FSM dictating how critter behaves in combat mode.");
		MEContextAddExpr(fsmEditorFindContextByName("AI"),			"IgnoreConditionBlock",	"Ignore Expression",	"When true, a critter will not take this interaction location.");
		MEContextAddMinMax(kMEFieldType_Spinner, 0, 20, 1,			"Priority",				"Priority",				"Critters will prefer to use objects with a higher priority.");

		assert(g_AnimCount != WL_VAL_UNSET);
		if(g_AnimCount != WL_VAL_DIFF) {
			int i;
			char pchDisplayText[256];
			assert(g_AnimCount >= 0 && g_AnimCount <= eaSize(&pProp->eaAnims));
			sprintf(pchDisplayText, "Job Animation");
			for ( i=0; i < g_AnimCount; i++ ) {
				MEFieldContextEntry *pEntry;
				pEntry = MEContextAddDictIdx(kMEFieldType_Combo, "AIAnimList",									"Anim", i,	pchDisplayText,		"Animation for the critter to execute while performing the job");
				MEContextEntryAddActionButton(pEntry, "X", NULL, wleAEIntractLocDeleteAnimButtonCB, pEntry, 0,									"Delete an animation");
				sprintf(pchDisplayText, "Job Animation %d", i+2);
			}
			if(g_AnimCount < MAX_JOB_ANIMS) {
				MEContextPush("Animations", &g_IntractLocUI, &g_IntractLocUI, parse_wleAEIntractLocUIData);
				MEContextAddDict(kMEFieldType_Combo, "AIAnimList",												"NewAnim",	pchDisplayText,		"Animation for the critter to execute while performing the job");
				MEContextPop("Animations");
			}
		}

		pContext->iXDataStart = MEFC_DEFAULT_X_DATA_START/2;
		MEContextAddButton("Remove Interact Location", NULL, wleAERemovePropsToSelection, &pPropData, 	"AddRemove",		NULL,				"Remove Interact Location Properties from this object.");
	} else {
		pContext->iXDataStart = 0;
		MEContextAddButton("Add Interact Location", NULL, wleAEAddPropsToSelection, &pPropData, 		"AddRemove",		NULL,				"Add Interact Location Properties to this object.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_InteractLocationProps");

	return (pProp ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);
}

#include "autogen/WorldEditorAmbientJob_c_ast.c"

#endif
