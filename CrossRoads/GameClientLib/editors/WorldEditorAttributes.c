#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorPlacementAttributes.h"
#include "WorldEditorLightAttributes.h"
#include "WorldEditorInteractionProp.h"
#include "WorldEditorMiscProp.h"
#include "WorldEditorTerrainProp.h"
#include "WorldEditorSoundAttributes.h"
#include "WorldEditorAppearanceAttributes.h"
#include "WorldEditorVolumeAttributes.h"
#include "WorldEditorAutoPlacementAttributes.h"
#include "WorldEditorPlanetGen.h"
#include "WorldEditorBuildingGen.h"
#include "WorldEditorSolarSystem.h"
#include "WorldEditorDebrisFieldGen.h"
#include "WorldEditorNebulaGen.h"
#include "WorldEditorCurveAttributes.h"
#include "WorldEditorNameAttributes.h"
#include "WorldEditorSpawnPointAttributes.h"
#include "WorldEditorPatrolAttributes.h"
#include "WorldEditorEncounterAttributes.h"
#include "WorldEditorActorAttributes.h"
#include "WorldEditorEncounterHackAttributes.h"
#include "WorldEditorTriggerConditionAttributes.h"
#include "WorldEditorLayerFSMAttributes.h"
#include "WorldEditorWind.h"
#include "WorldEditorLogicalGroupAttributes.h"
#include "WorldEditorOperations.h"
#include "WorldEditorOptions.h"
#include "WorldEditorAmbientJob.h"
#include "WorldEditorUI.h"
#include "WorldEditorUtil.h"
#include "WorldGrid.h"
#include "MultiEditField.h"
#include "MultiEditFieldContext.h"
#include "CurveEditor.h"
#include "EditLibUIUtil.h"
#include "EditorPrefs.h"
#include "tokenstore.h"
#include "StringCache.h"
#include "wlGroupPropertyStructs.h"
#include "groupdbModify.h"

#include "wlGroupPropertyStructs_h_ast.h"

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/******
* ATTRIBUTE EDITOR MANAGEMENT
******/
WleAEState wleAEGlobalState;


/******
* GENERAL FUNCTIONS
******/
static int wleAEPanelsAlphaCompare(const WleAEPanel **panel1, const WleAEPanel **panel2)
{
	return strcmpi((*panel1)->name, (*panel2)->name);
}

/******
* This function populates the specified EArray with all panels that are registered to the specified
* EditorObjectType.
* PARAMS:
*   type - EditorObjectType whose panels are to be found
*   panels - EArray of EMPanels that will be populated with all panels registered with the specified type
* RETURNS:
*   bool true if at least one panel was populated; false otherwise
******/
bool wleAEGetPanelsForType(EditorObjectType *type, WleAEPanel ***panels)
{
	int i, j;
	bool ret = false;
	bool add;
	EditorObjectType **allTypes = NULL;

	if (type->objType == EDTYPE_DUMMY)
	{
		EditorObject **selection = NULL;
		edObjSelectionGetAll(&selection);
		for (i = 0; i < eaSize(&selection); i++)
			eaPushUnique(&allTypes, selection[i]->type);
		eaDestroy(&selection);
	}
	else
		eaPush(&allTypes, type);

	for (i = 0; i < eaSize(&wleAEGlobalState.panels); i++)
	{
		add = true;
		for (j = 0; j < eaSize(&allTypes); j++)
		{
			if (eaFind(&wleAEGlobalState.panels[i]->types, allTypes[j]) == -1)
			{
				add = false;
				break;
			}
		}
		if (j && add)
		{
			ret = true;
			if (panels)
				eaPush(panels, wleAEGlobalState.panels[i]);
		}
	}
	eaDestroy(&allTypes);
	if (panels)
	{
		WleAEPanel *placement = NULL, *name = NULL, *misc = NULL;
		eaQSort((*panels), wleAEPanelsAlphaCompare);

		// hardcode certain ordering among panels
		for (i = 0; i < eaSize(panels); i++)
		{
			if (strcmpi((*panels)[i]->name, "Placement") == 0)
				placement = (*panels)[i];
			else if (strcmpi((*panels)[i]->name, "Name") == 0)
				name = (*panels)[i];
			else if (strcmpi((*panels)[i]->name, "Misc") == 0)
				misc = (*panels)[i];
		}
		if (placement)
			eaMove(panels, 0, eaFind(panels, placement));
		if (name)
			eaMove(panels, 0, eaFind(panels, name));
		if (misc)
			eaMove(panels, eaSize(panels) - 1, eaFind(panels, misc));
	}
	return ret;
}

static LogicalGroup* wleAEGetLogicalGroupFromObject(EditorObject *pObject, GroupTracker **pScopeTrackerOut)
{
	WorldZoneMapScope *pZmapScope = zmapGetScope(NULL);
	if(pZmapScope) {
		WorldLogicalGroup *pLogicalGroup;
		if (pObject->obj && stashFindPointer(pZmapScope->scope.name_to_obj, pObject->obj, &pLogicalGroup) && pLogicalGroup) {
			GroupTracker *pScopeTracker = pLogicalGroup->common_data.closest_scope->tracker;
			if (!pScopeTracker || !pScopeTracker->def)
				pScopeTracker = layerGetTracker(pLogicalGroup->common_data.layer);
			if(pScopeTracker && pScopeTracker->def) {
				if(pScopeTrackerOut)
					*pScopeTrackerOut = pScopeTracker;
				return wleFindLogicalGroup(pScopeTracker->def, pLogicalGroup->common_data.closest_scope, pLogicalGroup);
			}
		}
	}
	return NULL;
}

static void wleAEMakePropCopies()
{
	int i;
	EditorObject **ppObjects = NULL;
	wleAEGetSelectedObjects(&ppObjects);
	for (i = 0; i < eaSize(&ppObjects); i++) {
		EditorObject *pObject = ppObjects[i];
		if(pObject->type->objType == EDTYPE_TRACKER) {
			GroupTracker *pTracker = trackerFromTrackerHandle(pObject->obj);
			StructDestroySafe(parse_GroupProperties, &pObject->props_cpy);
			if (pTracker && pTracker->def) {
				pObject->props_cpy = StructClone(parse_GroupProperties, &pTracker->def->property_structs);
			}
		} else if (pObject->type->objType == EDTYPE_LOGICAL_GROUP) {
			GroupTracker *pScopeTracker;
			LogicalGroup *pGroup = wleAEGetLogicalGroupFromObject(pObject, &pScopeTracker);
			StructDestroySafe(parse_LogicalGroup, &pObject->logical_grp_cpy);
			pObject->logical_grp_editable = false;
			if(pGroup) {
				pObject->logical_grp_cpy = StructClone(parse_LogicalGroup, pGroup);
				pObject->logical_grp_editable = wleTrackerIsEditable(trackerHandleFromTracker(pScopeTracker), false, false, false);
			}
		}
	}
	eaDestroy(&ppObjects);
}

void wleAESetActive(EditorObject *edObj)
{
	EMEditorDoc *doc = wleGetWorldEditorDoc();
	int i;

	if (!doc)
		return;

	// clear out attributes selected for copying
	eaClear(&wleAEGlobalState.selectedAttributes);

	if (wleAEGlobalState.selected)
		editorObjectDeref(wleAEGlobalState.selected);
	wleAEGlobalState.selected = edObj;
	if (edObj)
		editorObjectRef(edObj);

	// decide which panels should render and reload them
	wleAEGlobalState.changed = false;
	for (i = 0; i < eaSize(&wleAEGlobalState.panels); i++)
		eaFindAndRemove(&doc->em_panels, wleAEGlobalState.panels[i]->panel);
	eaClear(&wleAEGlobalState.ownedPanels);
	eaClear(&wleAEGlobalState.validPanels);
	if (edObj)
	{
		WleAEPanel **typePanels = NULL;

		wleAEMakePropCopies();
		wleAEGetPanelsForType(edObj->type, &typePanels);

		for (i = 0; i < eaSize(&typePanels); i++)
		{
			WleAEPanel *typePanel = typePanels[i];
			WleUIPanelValid panelValid = typePanel->reloadFunc(typePanel->panel, edObj);

			typePanel->currentPanelMode = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, "AttribPanelMode", typePanel->name, WLE_UI_PANEL_MODE_NORMAL);

			if(typePanel->allways_force_shown) {
				if(panelValid != WLE_UI_PANEL_INVALID)
					eaPush(&doc->em_panels, typePanel->panel);
				continue;
			}

			switch(panelValid) {
			case WLE_UI_PANEL_INVALID:
				break;
			case WLE_UI_PANEL_UNOWNED:
				if(typePanel->currentPanelMode == WLE_UI_PANEL_MODE_NORMAL) {
					eaPush(&wleAEGlobalState.validPanels, typePanel);
					break;
				} else {
					//Fall through to WLE_UI_PANEL_OWNED
				}
			case WLE_UI_PANEL_OWNED:
				eaPush(&wleAEGlobalState.ownedPanels, typePanel);
				break;
			default:
				assertmsg(false, "Invalide return from panel reload function");			
			}
		}

		eaDestroy(&typePanels);
	}
	for ( i=0; i < eaSize(&wleAEGlobalState.ownedPanels); i++ ) 
	{
		WleAEPanel *typePanel = wleAEGlobalState.ownedPanels[i];
		if(	typePanel->currentPanelMode == WLE_UI_PANEL_MODE_FORCE_SHOWN ||
			typePanel->allways_force_shown ||
			EditorPrefGetInt(WLE_PREF_EDITOR_NAME, "AttribPanelSelected", typePanel->name, 0)) {
			eaPush(&doc->em_panels, typePanel->panel);
		}
	}
	wleUIAttribViewPanelRefresh();

	wleAEGlobalState.changed = true;

	// set target in editing selector
	if (edObj && edObj->type->compareFunc)
	{
		bool found = false;

		for (i = 0; i < eaSize(&editorUIState->trackerTreeUI.selection); i++)
		{
			if (edObjCompareForUI(edObj, editorUIState->trackerTreeUI.selection[i]) == 0)
			{
				found = true;
				ui_ComboBoxSetSelectedObject(editorUIState->trackerTreeUI.editing, editorUIState->trackerTreeUI.selection[i]);
				break;
			}
		}
		if (!found)
			ui_ComboBoxSetSelected(editorUIState->trackerTreeUI.editing, -1);
	}
	else if (edObj == editorUIState->trackerTreeUI.allSelect)
		ui_ComboBoxSetSelectedObject(editorUIState->trackerTreeUI.editing, edObj);
	else
		ui_ComboBoxSetSelected(editorUIState->trackerTreeUI.editing, -1);
}

void wleAESetActiveQueued(EditorObject *edObj)
{
	if (wleAEGlobalState.queuedSelected)
		editorObjectDeref(wleAEGlobalState.queuedSelected);
	wleAEGlobalState.queuedSelected = edObj;
	if (edObj)
		editorObjectRef(edObj);
	wleAEGlobalState.queuedClear = !edObj;
}

void wleAERefresh(void)
{
	if (!wleAEGlobalState.selected || wleAEGlobalState.applying_data || wleAEGlobalState.queuedSelected || wleAEGlobalState.queuedClear)
		return;

	editorObjectRef(wleAEGlobalState.selected);
	wleAESetActiveQueued(wleAEGlobalState.selected);
	editorObjectDeref(wleAEGlobalState.selected);
}

/******
* This function creates all of the UI for the various attribute editor panels.  All panels must
* be registered by this time.
* PARAMS:
*   windowList - UIWindow earray handle on which to push the attribute editor window.
******/
void wleAECreate(EMEditorDoc *doc)
{
	EMPanel *panel;
	int i;

	PERFINFO_AUTO_START_FUNC();
	// go through each registered panels and create the UI for it
	for (i = 0; i < eaSize(&wleAEGlobalState.panels); i++)
	{
		panel = emPanelCreate("Selection", wleAEGlobalState.panels[i]->name, 100);
		wleAEGlobalState.panels[i]->createFunc(panel);
		wleAEGlobalState.panels[i]->panel = panel;
	}

	wleAEGlobalState.changed = true;

	PERFINFO_AUTO_STOP();
}

/*******
* This function handles the processing of the queued activation of an editor object
* for the attribute editor.
******/
void wleAEOncePerFrame(void)
{
	if (wleAEGlobalState.queuedSelected || wleAEGlobalState.queuedClear)
	{
		wleAESetActive(wleAEGlobalState.queuedSelected);
		if (wleAEGlobalState.queuedSelected)
			editorObjectDeref(wleAEGlobalState.queuedSelected);
		wleAEGlobalState.queuedSelected = NULL;
		wleAEGlobalState.queuedClear = false;
	}
}

void wleAESetApplyingData(bool applying_data)
{
	wleAEGlobalState.applying_data = applying_data;
}


EditorObject *wleAEGetSelected(void)
{
	return wleAEGlobalState.selected;
}

void** wleAEGetSelectedDataFromPath(const char *pchPath, WleAESelectedExcludeFunc cbExclude, U32 *iRetFlags)
{
	int i;
	static void **ppData = NULL;
	EditorObject **ppObjects = NULL;
	bool bFail = false;
	int column = 0;

	eaClear(&ppData);
	if(pchPath)
		assert(ParserFindColumn(parse_GroupProperties, pchPath, &column));

	if(iRetFlags)
		*iRetFlags = 0;

	wleAEGetSelectedObjects(&ppObjects);
	for (i = 0; i < eaSize(&ppObjects); i++)
	{
		GroupTracker *tracker;
		void *pNextData;

		assert(ppObjects[i]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(ppObjects[i]->obj);
		if (!tracker || !tracker->def) {
			bFail = true;
			break;
		}
		if(cbExclude && cbExclude(tracker)) {
			bFail = true;
			break;
		}
		assert(ppObjects[i]->props_cpy);
		if(pchPath) {
			pNextData = TokenStoreGetPointer(parse_GroupProperties, column, ppObjects[i]->props_cpy, 0, NULL);
			if(pNextData)
				eaPush(&ppData, pNextData);
			else if(iRetFlags)
				*iRetFlags |= WleAESelectedDataFlags_SomeMissing;
		} else {
			eaPush(&ppData, tracker->def);
		}

		if (iRetFlags && !wleTrackerIsEditable(ppObjects[i]->obj, false, false, false))
			*iRetFlags |= WleAESelectedDataFlags_Inactive;
		if (iRetFlags && tracker->def->model) 
			*iRetFlags |= WleAESelectedDataFlags_Model;
		if (iRetFlags && !groupIsObjLib(tracker->def)) 
			*iRetFlags |= WleAESelectedDataFlags_ObjLib;
	}

	eaDestroy(&ppObjects);
	if(iRetFlags && bFail)
		*iRetFlags |= WleAESelectedDataFlags_Failed;
	if (bFail || eaSize(&ppData)==0) {
		return NULL;
	}
	return ppData;
}

GroupChild* wleAEGetSingleSelectedGroupChild( bool prompt, bool* out_isEditable )
{
	EditorObject** eaObjects = NULL;
	bool scratch;
	wleAEGetSelectedObjects( &eaObjects );

	if( !out_isEditable ) {
		out_isEditable = &scratch;
	}

	
	if( eaSize( &eaObjects ) != 1 ) {
		*out_isEditable = false;
		return NULL;
	} else {
		GroupTracker* tracker;
		assert( eaObjects[ 0 ]->type->objType == EDTYPE_TRACKER );
		tracker = trackerFromTrackerHandle( eaObjects[ 0 ]->obj );

		if( !tracker || !tracker->parent ) {
			*out_isEditable = false;
			return NULL;
		}

		*out_isEditable = wleTrackerIsEditable( trackerHandleFromTracker( tracker->parent ), false, prompt, false );
		return eaGet( &tracker->parent->def->children, tracker->idx_in_parent );
	}

	*out_isEditable = false;
	return NULL;
}

GroupDef* wleAEGetSingleSelectedGroupDef( bool prompt, bool* out_isEditable )
{
	EditorObject** eaObjects = NULL;
	bool scratch;
	wleAEGetSelectedObjects( &eaObjects );

	if( !out_isEditable ) {
		out_isEditable = &scratch;
	}

	
	if( eaSize( &eaObjects ) != 1 || eaObjects[ 0 ]->type->objType != EDTYPE_TRACKER ) {
		*out_isEditable = false;
		return NULL;
	} else {
		GroupTracker* tracker;
		tracker = trackerFromTrackerHandle( eaObjects[ 0 ]->obj );

		if( !tracker || !tracker->parent ) {
			*out_isEditable = false;
			return NULL;
		}

		*out_isEditable = wleTrackerIsEditable( trackerHandleFromTracker( tracker->parent ), false, prompt, false );
		return tracker->def;
	}

	*out_isEditable = false;
	return NULL;
}

void wleAEApplyToSelection(wleAEApplyToSelectionCallback cbCallback, UserData pUserData, UserData pUserData2)
{
	int i;
	EditorObject **objects = NULL;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++) {
		WleAESelectionCBData pData = {0};
		if (objects[i]->type->objType == EDTYPE_TRACKER) {
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			if (def) {
				pData.pTracker = tracker;
				cbCallback(objects[i], &pData, pUserData, pUserData2);
			}
			if (tracker) {
				wleOpPropsEndNoUIUpdate();
			}
		} else if (objects[i]->type->objType == EDTYPE_LOGICAL_GROUP) {
			GroupTracker *pScopeTracker;
			LogicalGroup *pGroup = wleAEGetLogicalGroupFromObject(objects[i], &pScopeTracker);
			if(pGroup) {
				GroupTracker *tracker = wleOpPropsBegin(trackerHandleFromTracker(pScopeTracker));
				GroupDef *def = tracker ? tracker->def : NULL;
				if (def) {
					pData.pTracker = tracker;
					pData.pGroup = pGroup;
					cbCallback(objects[i], &pData, pUserData, pUserData2);
				}
				if (tracker) {
					wleOpPropsEndNoUIUpdate();
				}
			}
		} else if (objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR) {
			WleEncObjSubHandle *pHandle = objects[i]->obj;
			WorldActorProperties *pActor = wleEncounterActorFromHandle(pHandle, NULL);
			if(pActor) {
				GroupTracker *tracker = wleOpPropsBegin(pHandle->parentHandle);
				if (tracker) {
					pData.pTracker = tracker;
					pData.pActor = pActor;
					cbCallback(objects[i], &pData, pUserData, pUserData2);
					wleOpPropsEndNoUIUpdate();
				}
			}
		}
	}
	wleOpRefreshUI();
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEFieldChangedApplyCB(EditorObject *pObject, WleAESelectionCBData *pData, wleAEFieldChangedExtraCallback cbExtra, MEField *pField)
{
	if (pObject->type->objType == EDTYPE_TRACKER) {
		GroupDef *pDef = pData->pTracker->def;
		assert(pObject->props_cpy);
		if(cbExtra)
			cbExtra(pField, pData->pTracker, &pDef->property_structs, pObject->props_cpy);
		StructCopy(parse_GroupProperties, pObject->props_cpy, &pDef->property_structs, 0, 0, 0);
	} else if (pObject->type->objType == EDTYPE_LOGICAL_GROUP) {
		assert(pObject->logical_grp_cpy);
		if(cbExtra)
			cbExtra(pField, pData->pTracker, pData->pGroup, pObject->logical_grp_cpy);
		StructCopy(parse_LogicalGroup, pObject->logical_grp_cpy, pData->pGroup, 0, 0, 0);
	}
}

//Do not expose this.  If you bypass wleAEAddFieldChangedCallback and set directly you loose type casting checking.
static void wleAEFieldChanged(MEField *pField, bool bFinished, wleAEFieldChangedExtraCallback cbExtra)
{
	if(MEContextExists())
		return;
	wleAEApplyToSelection(wleAEFieldChangedApplyCB, cbExtra, pField);
}

void wleAECallFieldChangedCallback(MEField *pField, wleAEFieldChangedExtraCallback cbExtra)
{
	wleAEFieldChanged(pField, true, cbExtra);
}

void wleAEAddFieldChangedCallback(MEFieldContext *pContext, wleAEFieldChangedExtraCallback cbExtra)
{
	pContext->cbChanged = wleAEFieldChanged;
	pContext->bSkipSiblingChangedCallbacks = true;
	pContext->pChangedData = cbExtra;
}

static void wleAEGroupChildFieldChanged(MEField *pField, bool bFinished, wleAEFieldChangedExtraCallback cbExtra)
{
	EditorObject **objects = NULL;
	
	if(MEContextExists() || !bFinished)
		return;

	// MJF: Right now this only works if there's only one thing selected.
	wleAEGetSelectedObjects(&objects);
	if( eaSize( &objects ) != 1 || objects[0]->type->objType != EDTYPE_TRACKER ) {
		return;
	} else {
		GroupTracker* tracker = trackerFromTrackerHandle( objects[0]->obj );
		bool validated = true;

		assert( tracker );

		if( !tracker->parent ) {
			emStatusPrintf( "Layer \"%s\" can not be modified!", tracker->def ? tracker->def->name_str : "DELETED" );
			validated = false;
		}
		if( !wleTrackerIsEditable( trackerHandleFromTracker( tracker->parent ), false, true, false )) {
			validated = false;
		}

		if( !validated ) {
			return;
		}
		
		groupdbDirtyTracker( tracker->parent, tracker->idx_in_parent );
	}

	EditUndoBeginGroup(edObjGetUndoStack());
	
	// MJF: The changes have already happened -- is it too late?

	EditUndoEndGroup(edObjGetUndoStack());

	wleOpRefreshUI();
	
	eaDestroy(&objects);
}

void wleAECallGroupChildFieldChangedCallback(MEField *pField, wleAEFieldChangedExtraCallback cbExtra)
{
	wleAEGroupChildFieldChanged(pField, true, cbExtra);
}

void wleAEAddGroupChildFieldChangedCallback(MEFieldContext *pContext, wleAEFieldChangedExtraCallback cbExtra)
{
	pContext->cbChanged = wleAEGroupChildFieldChanged;
	pContext->pChangedData = cbExtra;
}

static void wleAEAddPropsToSelectionApplyCB(EditorObject *pObject, WleAESelectionCBData *pData, WleAEPropStructData *pPropData, int *column)
{
	void *pNextData;
	GroupDef *pDef = pData->pTracker->def;
	assert(pObject->props_cpy);
	pNextData = TokenStoreGetPointer(parse_GroupProperties, *column, pObject->props_cpy, 0, NULL);
	if(!pNextData) {
		pNextData = StructCreateVoid(pPropData->pTable);
		TokenStoreSetPointer(parse_GroupProperties, *column, pObject->props_cpy, 0, pNextData, NULL);
		StructCopy(parse_GroupProperties, pObject->props_cpy, &pDef->property_structs, 0, 0, 0);
	}
}

void wleAEAddPropsToSelection(void *pUnused, WleAEPropStructData *pPropData)
{
	int column;
	assert(ParserFindColumn(parse_GroupProperties, pPropData->pchPath, &column));
	wleAEApplyToSelection(wleAEAddPropsToSelectionApplyCB, pPropData, &column);
}

void wleAEMessageMakeEditorCopy(DisplayMessage *pMessage, const char *pchKey, const char *pchScope, const char *pchDescription)
{
	if (pMessage) {
		langMakeEditorCopy(parse_DisplayMessage, pMessage, true);
		if( !pMessage->pEditorCopy->pcMessageKey || !pMessage->pEditorCopy->pcMessageKey[ 0 ] ) {
			char pchKeyFull[256];
			sprintf(pchKeyFull, "_.%s_%s._", pchScope, pchKey);
			pMessage->pEditorCopy->pcMessageKey = StructAllocString(pchKeyFull);
		}
		if( !pMessage->pEditorCopy->pcDescription || !pMessage->pEditorCopy->pcDescription[ 0 ] ) {
			pMessage->pEditorCopy->pcDescription = StructAllocString(pchDescription);
		}
		if( !pMessage->pEditorCopy->pcScope || !pMessage->pEditorCopy->pcScope[ 0 ] ) {
			pMessage->pEditorCopy->pcScope = allocAddString(pchScope);
		}
	} 
}


static void wleAERemovePropsToSelectionApplyCB(EditorObject *pObject, WleAESelectionCBData *pData, WleAEPropStructData *pPropData, int *column)
{
	void *pNextData;
	GroupDef *pDef = pData->pTracker->def;
	assert(pObject->props_cpy);
	pNextData = TokenStoreGetPointer(parse_GroupProperties, *column, pObject->props_cpy, 0, NULL);
	if(pNextData) {
		StructDestroyVoid(pPropData->pTable, pNextData);
		TokenStoreSetPointer(parse_GroupProperties, *column, pObject->props_cpy, 0, NULL, NULL);
		StructCopy(parse_GroupProperties, pObject->props_cpy, &pDef->property_structs, 0, 0, 0);
	}
}

void wleAERemovePropsToSelection(void *pUnused, WleAEPropStructData *pPropData)
{
	int column;
	assert(ParserFindColumn(parse_GroupProperties, pPropData->pchPath, &column));
	wleAEApplyToSelection(wleAERemovePropsToSelectionApplyCB, pPropData, &column);
}

// the reload function should return a value from 0-2; 0 = panel should not be shown; 1 = panel should only be shown if manually added or 
// visibility set to show it; 2 = panel should be shown in auto mode
WleAEPanel* wleAERegisterPanel(char *name, WleAEPanelCallback reloadFunc, WleAEPanelCreateCallback createFunc, 
								WleAEPanelActionCallback addFunc, WleAEPanelActionCallback removeFunc, 
								const char *field_name, ParseTable *pti, EditorObjectType **types)
{
	WleAEPanel *panel = StructCreate(parse_WleAEPanel);
	panel->name = StructAllocString(name);
	panel->reloadFunc = reloadFunc;
	panel->createFunc = createFunc;
	panel->addProps = addFunc;
	panel->removeProps = removeFunc;
	panel->prop_data.pchPath = field_name;
	panel->prop_data.pTable = pti;
	eaCopy(&panel->types, &types);
	eaPush(&wleAEGlobalState.panels, panel);
	return panel;
}

void wleAEGenericCreate(EMPanel *panel)
{
	//TODO: Now that I know this function is useless I should remove references to it.
}

void wleAEWLVALSetFromBool(int *iCurrentVal, bool bNewVal)
{
	int iNewVal = (bNewVal ? WL_VAL_TRUE : WL_VAL_FALSE);
	if(*iCurrentVal == WL_VAL_UNSET) {
		*iCurrentVal = iNewVal;
	} else if (*iCurrentVal != iNewVal) {
		*iCurrentVal = WL_VAL_DIFF;
	}
}

#endif
AUTO_RUN;
void wleAERegisterAllPanels(void)
{
#ifndef NO_EDITORS
	EditorObjectType **types = NULL;
	WleAEPanel *panel;

	// logical groups
	eaPush(&types, editorObjectTypeGet(EDTYPE_LOGICAL_GROUP));
	wleAERegisterPanel("Logical Group", wleAELogicalGroupReload, wleAEGenericCreate, 
		wleAELogicalGroupAddProps, wleAELogicalGroupRemoveProps, NULL, NULL, types);
	eaClear(&types);

	// placement
	eaPush(&types, editorObjectTypeGet(EDTYPE_TRACKER));
	eaPush(&types, editorObjectTypeGet(EDTYPE_PATROL_POINT));
	eaPush(&types, editorObjectTypeGet(EDTYPE_ENCOUNTER_ACTOR));
	panel = wleAERegisterPanel("Placement", wleAEPlacementReload, wleAEPlacementCreate, NULL, NULL, NULL, NULL, types);
	panel->allways_force_shown = true;
	eaClear(&types);

	// actors
	eaPush(&types, editorObjectTypeGet(EDTYPE_ENCOUNTER_ACTOR));
	wleAERegisterPanel("Actor", wleAEActorReload, wleAEActorCreate, NULL, NULL, NULL, NULL, types);
	eaClear(&types);

	// name
	eaPush(&types, editorObjectTypeGet(EDTYPE_TRACKER));
	panel = wleAERegisterPanel("Name", wleAENameReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);
	panel->allways_force_shown = true;

	// appearance
	wleAERegisterPanel("Appearance", wleAEAppearanceReload, wleAEAppearanceCreate, NULL, NULL, NULL, NULL, types);

	// light
	wleAERegisterPanel("Light", wleAELightReload, wleAEGenericCreate, NULL, NULL, "LightProperties", parse_WorldLightProperties, types);

	// volume
	wleAERegisterPanel("Volume", wleAEVolumeReload, wleAEVolumeCreate, wleAEVolumeAdd, wleAEVolumeRemove, NULL, NULL, types);

	// auto-placement
	wleAERegisterPanel("Auto-Placement", wleAEAutoPlacementReload, wleAEAutoPlacementCreate, 
		NULL, NULL, "AutoPlacement", parse_WorldAutoPlacementProperties, types);

	// sound conn
	wleAERegisterPanel("Sound", wleAESoundReload, wleAEGenericCreate, NULL, NULL, "SoundSphere", parse_WorldSoundSphereProperties, types);

	// Path node
	wleAERegisterPanel("PathNode", wleAEPathNodeReload, wleAEGenericCreate, NULL, NULL, "PathNode", parse_WorldPathNodeProperties, types);

	// interaction
	eaPush(&types, editorObjectTypeGet(EDTYPE_ENCOUNTER_ACTOR));
	wleAERegisterPanel("Interaction", wleAEInteractionPropReload, wleAEInteractionPropCreate, wleAEInteractionPropAdd, wleAEInteractionPropRemove, NULL, NULL, types);
	eaClear(&types);
	eaPush(&types, editorObjectTypeGet(EDTYPE_TRACKER));

	// Physical
	wleAERegisterPanel("Physical Properties", wleAEPhysicalPropReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// LOD
	wleAERegisterPanel("LOD Stuff", wleAELODPropReload, wleAEGenericCreate, wleAELODPropAdd, wleAELODPropRemove, NULL, NULL, types);

	// UGC
	wleAERegisterPanel("UGC Room Object", wleAEUGCPropReload, wleAEGenericCreate, NULL, NULL, "UGCRoomObjectProperties", parse_WorldUGCRoomObjectProperties, types);

	// Ambient Job
	wleAERegisterPanel("Interaction Location", wleAEInteractLocationReload, wleAEGenericCreate, 
		NULL, NULL, "InteractLocation", parse_WorldInteractLocationProperties, types);

	// System
	wleAERegisterPanel("System Attributes", wleAESystemPropReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// terrain
	wleAERegisterPanel("Terrain", wleAETerrainPropReload, wleAEGenericCreate, wleAETerrainPropAdd, wleAETerrainPropRemove, NULL, NULL, types);

	// planet
	wleAERegisterPanel("Planet", wleAEPlanetGenReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// building
	wleAERegisterPanel("Building", wleAEBuildingGenReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// debris field
	wleAERegisterPanel("Debris Field", wleAEDebrisFieldGenReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// curve
	wleAERegisterPanel("Curve", wleAECurveReload, wleAEGenericCreate, NULL, NULL, "ChildCurve", parse_WorldChildCurve, types);

	// wind
	wleAERegisterPanel("Wind", wleAEWindReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// spawn point
	wleAERegisterPanel("Spawn Point", wleAESpawnPointReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// patrol route
	wleAERegisterPanel("Patrol Route", wleAEPatrolReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// encounter hack
	wleAERegisterPanel("Encounter Using EncounterDef", wleAEEncounterHackReload, wleAEEncounterHackCreate, NULL, NULL, NULL, NULL, types);

	// encounter
	wleAERegisterPanel("Encounter", wleAEEncounterReload, wleAEEncounterCreate, NULL, NULL, NULL, NULL, types);

	// trigger condition
	wleAERegisterPanel("Trigger Condition", wleAETriggerConditionReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	// layerFSM
	wleAERegisterPanel("Layer FSM", wleAELayerFSMReload, wleAELayerFSMCreate, NULL, NULL, NULL, NULL, types);

	// wind source
	wleAERegisterPanel("Wind Source", wleAEWindSourceReload, wleAEGenericCreate, NULL, NULL, NULL, NULL, types);

	eaDestroy(&types);
#endif
}
#ifndef NO_EDITORS

/********************
* ATTRIBUTE COPYING
********************/
void wleAECopyButtonClicked(UIButton *button, WleAECopyData *data)
{
	data->selected = !data->selected;

	if (data->selected)
		eaPush(&wleAEGlobalState.selectedAttributes, data);
	else
		eaFindAndRemove(&wleAEGlobalState.selectedAttributes, data);

	ui_ButtonSetText(button, data->selected ? "S" : "C");
}

void wleAECopyButtonFree(UIButton *button)
{
	WleAECopyData *data = button->clickedData;
	eaFindAndRemove(&wleAEGlobalState.selectedAttributes, data);
	if (data->copyFreeFunc)
		data->copyFreeFunc(data->copyData);
	SAFE_FREE(button->clickedData);
	ui_ButtonFreeInternal(button);
}

UIButton *wleAECopyButtonCreate(WleAECopyCallback copyFunc, UserData copyData, WleAECopyPasteFreeCallback freeFunc)
{
	WleAECopyData *data = calloc(1, sizeof(*data));
	UIButton *button = ui_ButtonCreate("C", 0, 0, wleAECopyButtonClicked, data);
	data->copyFunc = copyFunc;
	data->copyData = copyData;
	data->copyFreeFunc = freeFunc;
	button->widget.freeF = wleAECopyButtonFree;
	return button;
}

WleAEPasteData *wleAEPasteDataCreate(UserData pasteData, WleAEPasteCallback pasteFunc, WleAECopyPasteFreeCallback freeFunc)
{
	WleAEPasteData *retData = calloc(1, sizeof(*retData));
	retData->pasteFunc = pasteFunc;
	retData->freeFunc = freeFunc;
	retData->pasteData = pasteData;
	return retData;
}

void wleAEAttributeBufferFree(WleAEAttributeBuffer *buffer)
{
	int i;
	for (i = eaSize(&buffer->pasteData) - 1; i >= 0; i--)
		buffer->pasteData[i]->freeFunc(buffer->pasteData[i]->pasteData);
	eaDestroyEx(&buffer->pasteData, NULL);
	SAFE_FREE(buffer);
}

bool wleAECopyAttributes(void)
{
	EditorObject **selection = NULL;
	WleAEAttributeBuffer *buffer = calloc(1, sizeof(*buffer));
	int i;

	wleAEGetSelectedObjects(&selection);
	if (eaSize(&selection) == 0)
		emStatusPrintf("Cannot copy attributes without a selected object.");
	else if (eaSize(&selection) > 1)
		emStatusPrintf("Cannot copy merged attribute set from multiple objects.");
	else
	{
		for (i = 0; i < eaSize(&wleAEGlobalState.selectedAttributes); i++)
		{
			WleAEPasteData *pasteData = wleAEGlobalState.selectedAttributes[i]->copyFunc(selection[0], wleAEGlobalState.selectedAttributes[i]->copyData);
			if (pasteData)
				eaPush(&buffer->pasteData, pasteData);
		}
	}

	if (eaSize(&buffer->pasteData) > 0)
	{
		emAddToClipboardCustom("WorldEditorAttributeBuffer", wleAEAttributeBufferFree, buffer);
		return true;
	}
	else
	{
		wleAEAttributeBufferFree(buffer);
		return (eaSize(&wleAEGlobalState.selectedAttributes) > 0);
	}
}

void wleAEPasteAttributes(WleAEAttributeBuffer *buffer)
{
	EditorObject **selection = NULL;
	int i;

	wleAEGetSelectedObjects(&selection);
	if (eaSize(&selection) == 0)
		emStatusPrintf("Must select objects to paste attributes.");
	else
	{
		EditUndoBeginGroup(edObjGetUndoStack());
		for (i = 0; i < eaSize(&buffer->pasteData); i++)
			buffer->pasteData[i]->pasteFunc(selection, buffer->pasteData[i]->pasteData);
		EditUndoEndGroup(edObjGetUndoStack());
	}
}


#endif

#include "WorldEditorAttributesPrivate_h_ast.c"
