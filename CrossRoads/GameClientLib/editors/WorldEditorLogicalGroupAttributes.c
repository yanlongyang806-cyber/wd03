#ifndef NO_EDITORS

#include "WorldEditorLogicalGroupAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorOperations.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void wleAELogicalGroupReloadSpawnProps(LogicalGroupSpawnProperties **ppProps, const char *pchName)
{
	MEFieldContext *pContext;
	LogicalGroupSpawnProperties *pProp;

	if(eaSize(&ppProps) <= 0)
		return;
	pProp = ppProps[0];

	pContext = MEContextPushEA(pchName, ppProps, ppProps, parse_LogicalGroupSpawnProperties);

	MEContextAddLabel(pchName, pchName, NULL);
	MEContextIndentRight();

	MEContextAddEnum(kMEFieldType_Combo, LogicalGroupRandomTypeEnum,			"RandomType",		"Random Spawn Rule",	"Whether this group should spawn a fixed number of objects or a percentage.");
	if(!MEContextFieldDiff("RandomType") && pProp->eRandomType != LogicalGroupRandomType_None) {
		MEContextAddEnum(kMEFieldType_Combo, LogicalGroupSpawnAmountTypeEnum,	"SpawnAmountType",	"Fixed/Percent",		"Whether this group should spawn a fixed number of objects or a percentage.");
		MEContextAddMinMax(kMEFieldType_Spinner, 0, 100, 1,						"SpawnAmount",		"Spawn Amount",			"The number or percentage of items out of this group that should spawn at once.");
	}
	MEContextAddMinMax(kMEFieldType_Spinner, 0, 10000, 25,						"LockoutRadius",	"Lockout Radius",		"The radius that should space between items.");

	MEContextIndentLeft();

	MEContextPop(pchName);
}

static void wleAELogicalGroupReloadInteractable(LogicalGroupProperties **ppParentProps)
{
	int i;
	LogicalGroupSpawnProperties **ppProps = NULL;
	for ( i=0; i < eaSize(&ppParentProps); i++ ) {
		eaPush(&ppProps, &ppParentProps[i]->interactableSpawnProperties);
	}
	wleAELogicalGroupReloadSpawnProps(ppProps, "Interactable");
	eaDestroy(&ppProps);
}

static void wleAELogicalGroupReloadEncounter(LogicalGroupProperties **ppParentProps)
{
	int i;
	LogicalGroupSpawnProperties **ppProps = NULL;
	for ( i=0; i < eaSize(&ppParentProps); i++ ) {
		eaPush(&ppProps, &ppParentProps[i]->encounterSpawnProperties);
	}
	wleAELogicalGroupReloadSpawnProps(ppProps, "Encoutner");
	eaDestroy(&ppProps);
}


static void wleAELogicalGroupAddPropsCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(!pData->pGroup->properties) {
		pData->pGroup->properties = StructCreate(parse_LogicalGroupProperties);
	}
}

void wleAELogicalGroupAddProps(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAELogicalGroupAddPropsCB, NULL, NULL);
}

static void wleAELogicalGroupRemovePropsCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pGroup->properties) {
		StructDestroySafe(parse_LogicalGroupProperties, &pData->pGroup->properties);
	}
}

void wleAELogicalGroupRemoveProps(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAELogicalGroupRemovePropsCB, NULL, NULL);
}

static void wleAELogicalGroupPropChangedCB(MEField *pField, GroupTracker *pTracker, const LogicalGroup *pOld, LogicalGroup *pNew)
{
	if(pNew->properties) {
		if (pNew->properties->interactableSpawnProperties.eRandomType == LogicalGroupRandomType_None){
			pNew->properties->interactableSpawnProperties.eSpawnAmountType = 0;
			pNew->properties->interactableSpawnProperties.uSpawnAmount = 0;
		}
		if (pNew->properties->encounterSpawnProperties.eRandomType == LogicalGroupRandomType_None){
			pNew->properties->encounterSpawnProperties.eSpawnAmountType = 0;
			pNew->properties->encounterSpawnProperties.uSpawnAmount = 0;
		}
	}
}

int wleAELogicalGroupReload(EMPanel *panel, EditorObject *edObj)
{
	int i;
	MEFieldContext *pContext;
	EditorObject **ppObjects = NULL;
	LogicalGroupProperties **ppProps = NULL;
	LogicalGroupProperties *pProp;
	bool bPanelActive = true;
	bool bAllLogicalGroups = true;
	bool bAllHaveProps = true;

	wleAEGetSelectedObjects(&ppObjects);
	for (i = 0; i < eaSize(&ppObjects); i++) {
		LogicalGroup *pGroup;

		assert(ppObjects[i]->type->objType == EDTYPE_LOGICAL_GROUP);
		pGroup = ppObjects[i]->logical_grp_cpy;
		if(!pGroup) {
			bAllLogicalGroups = false;
			break;
		}

		if(!pGroup->properties) {
			bAllHaveProps = false;
		} else {
			eaPush(&ppProps, pGroup->properties);
		}

		if (!ppObjects[i]->logical_grp_editable)
			bPanelActive = false;
	}
	eaDestroy(&ppObjects);
	if (!bAllLogicalGroups) {
		eaDestroy(&ppProps);
		return WLE_UI_PANEL_INVALID;
	}
	if(eaSize(&ppProps) > 0)
		pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_LogicalGroupProps", ppProps, ppProps, parse_LogicalGroupProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAELogicalGroupPropChangedCB);

	if(bAllHaveProps) {

		wleAELogicalGroupReloadInteractable(ppProps);
		MEContextAddSpacer();
		wleAELogicalGroupReloadEncounter(ppProps);

		pContext->iXDataStart = MEFC_DEFAULT_X_DATA_START/2;
		MEContextAddButton("Remove Logical Group Properties", NULL, wleAELogicalGroupRemoveProps, NULL, "AddRemove", NULL, "Remove Logical Group Properties from this object.");
	} else {
		pContext->iXDataStart = 0;
		MEContextAddButton("Add Logical Group Properties", NULL, wleAELogicalGroupAddProps, NULL, 		"AddRemove", NULL, "Add Logical Group Properties to this object.");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, bPanelActive);

	MEContextPop("WorldEditor_LogicalGroupProps");
	eaDestroy(&ppProps);
	return (bAllHaveProps ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);
}

#endif