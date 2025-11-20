#ifndef NO_EDITORS

#include "WorldEditorLayerFSMAttributes.h"
#include "WorldEditorLayerFSMAttributes_c_ast.h"

#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "Expression.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "WorldGrid.h"
#include "wlEncounter.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
#define WLE_AE_LAYER_FSM_ALIGN_WIDTH 150
#define WLE_AE_LAYER_FSM_ENTRY_WIDTH 200

#define wleAELayerFSMSetupParam(param, fieldName)\
	param.entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;\
	param.struct_offset = offsetof(GroupDef, layer_fsm_properties);\
	param.struct_pti = parse_WorldLayerFSMProperties;\
	param.struct_fieldname = fieldName

#define wleAELayerFSMUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldLayerFSMProperties *properties;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = trackerFromTrackerHandle(obj->obj);\
	def = tracker ? tracker->def : NULL;\
	properties = def ? def->property_structs.layer_fsm_properties : NULL

#define wleAELayerFSMApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldLayerFSMProperties *properties;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(obj->obj);\
	if (!tracker)\
		return;\
	def = tracker ? tracker->def : NULL;\
	properties = def ? def->property_structs.layer_fsm_properties : NULL;\
	if (!properties)\
	{\
		wleOpPropsEnd();\
		return;\
	}\

#define wleAELayerFSMApplyInitAt(i)\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldLayerFSMProperties *properties;\
	assert(objs[i]->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(objs[i]->obj);\
	if (!tracker)\
		continue;\
	def = tracker ? tracker->def : NULL;\
	properties = def ? def->property_structs.layer_fsm_properties : NULL;\
	if (!properties)\
	{\
		wleOpPropsEndNoUIUpdate();\
		continue;\
	}\

AUTO_STRUCT;
typedef struct WleAEParamLayerFSMVar
{
	WleAEParamFloat *floatVar;
	WleAEParamInt *intVar;
	WleAEParamText *pointVar;
	WleAEParamText *stringVar;
	WleAEParamMessage *messageVar;
	WleAEParamDictionary *animationVar;
	WleAEParamDictionary *critterDef;
	WleAEParamDictionary *critterGroup;
} WleAEParamLayerFSMVar;

typedef struct WleAELayerFSMUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;

	struct
	{
		WleAEParamDictionary layerFSM;
		WleAEParamLayerFSMVar** externVars;
	} data;

	// data cached
	char** named_point_names;
} WleAELayerFSMUI;

/********************
* GLOBALS
********************/
static WleAELayerFSMUI wleAEGlobalLayerFSMUI;

/********************
* MAIN
********************/
void wleAELayerFSMVarAddWidget(UIRebuildableTree *auto_widget, const char *name, const char *tooltip, const char *param_name, WleAEParamLayerFSMVar *param)
{
	if (param->floatVar) {
		wleAEFloatAddWidget(auto_widget, name, tooltip, param_name, param->floatVar, -FLT_MAX, FLT_MAX, 0.1);
	} else if (param->intVar) {
		wleAEIntAddWidget(auto_widget, name, tooltip, param_name, param->intVar, INT_MIN, INT_MAX, 1);
	} else if (param->pointVar) {
		wleAETextAddWidget(auto_widget, name, tooltip, param_name, param->pointVar);
	} else if (param->stringVar) {
		wleAETextAddWidget(auto_widget, name, tooltip, param_name, param->stringVar);
	} else if (param->messageVar) {
		wleAEMessageAddWidget(auto_widget, name, tooltip, param_name, param->messageVar);
	}
}

static WorldVariable* worldVariableFindVar( WorldVariable** vars, const char* varName )
{
	int it;
	for( it = 0; it != eaSize( &vars ); ++it ) {
		if( stricmp( varName, vars[ it ]->pcName ) == 0 ) {
			return vars[ it ];
		}
	}

	return NULL;
}

static WorldVariable* wleAELayerFSMVariableFromIndex(WorldLayerFSMProperties *properties, int index)
{
	FSM* fsm;
	FSMExternVar** externVars = NULL;

	if (!properties) {
		return NULL;
	}

	fsm = GET_REF(properties->hFSM);
	if (!fsm) {
		return NULL;
	}

	fsmGetExternVarNamesRecursive(fsm, &externVars, "layer");
	{
		WorldVariable *var = worldVariableFindVar( properties->fsmVars, externVars[index]->name);
		eaDestroy(&externVars);
		return var;
	}
}

/// Ensure the FSM vars fully exist and are set up with resonable defaults.
static void wleAELayerFSMEnsureVars(FSM* fsm, WorldVariable*** fsmVars)
{
	FSMExternVar** fsmVarDefs = NULL;
	bool fsmVarsNeedsReset = false;

	if( !fsm ) {
		return;
	}
	fsmGetExternVarNamesRecursive( fsm, &fsmVarDefs, "layer" );

	if( eaSize( fsmVars ) != eaSize( &fsmVarDefs )) {
		fsmVarsNeedsReset = true;
	} else {
		int it;
		for( it = 0; it != eaSize( fsmVars ); ++it ) {
			FSMExternVar* varDef = fsmVarDefs[ it ];
			WorldVariable* var = (*fsmVars)[ it ];

			if(   var->eType != worldVariableTypeFromFSMExternVar( varDef )
				  || stricmp( var->pcName, varDef->name) != 0 ) {
				fsmVarsNeedsReset = true;
			}
		}
	}

	if( fsmVarsNeedsReset ) {
		WorldVariable** oldFsmVars = *fsmVars;
		int it;

		*fsmVars = NULL;
		for( it = 0; it != eaSize( &fsmVarDefs ); ++it ) {
			FSMExternVar* varDef = fsmVarDefs[ it ];
			WorldVariable* oldVar = worldVariableFindVar( oldFsmVars, varDef->name );
			WorldVariableType varType = worldVariableTypeFromFSMExternVar( varDef );

			if( oldVar && oldVar->eType == varType ) {
				eaPush( fsmVars, StructClone( parse_WorldVariable, oldVar ));
			} else {
				WorldVariable* varAccum = StructCreate( parse_WorldVariable );
				varAccum->pcName = allocAddString( varDef->name );
				varAccum->eType = varType;

				if( varAccum->eType == WVAR_MESSAGE ) {
					langMakeEditorCopy(parse_DisplayMessage, &varAccum->messageVal, true);
				}
				
				eaPush( fsmVars, varAccum );
			}
		}

		eaDestroyStruct( &oldFsmVars, parse_WorldVariable );
	}

	eaDestroy(&fsmVarDefs);
}

static void wleAELayerFSMVarUpdateFloat(WleAEParamFloat* param, void* data, EditorObject* obj)
{
	WorldVariable *var;
	
	wleAELayerFSMUpdateInit();
	
	if (!properties)
		return;

	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_FLOAT)
	{
		param->floatvalue = var->fFloatVal;
	}
}

static void wleAELayerFSMVarApplyFloat(WleAEParamFloat *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
		wleAELayerFSMApplyInitAt(i);
		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_FLOAT;
			var->fFloatVal = param->floatvalue;
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdateInt(WleAEParamInt* param, void* data, EditorObject* obj)
{
	WorldVariable *var;
	
	wleAELayerFSMUpdateInit();

	if (!properties)
		return;
	
	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_INT)
	{
		param->intvalue = var->iIntVal;
	}
}

static void wleAELayerFSMVarApplyInt(WleAEParamInt *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
	
		wleAELayerFSMApplyInitAt(i);
		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_INT;
			var->iIntVal = param->intvalue;
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdatePoint(WleAEParamText* param, void* data, EditorObject* obj)
{
	WorldVariable *var;
	
	wleAELayerFSMUpdateInit();

	if (!properties)
		return;

	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_LOCATION_STRING)
	{
		param->stringvalue = StructAllocString( var->pcStringVal );
	}
}

static void wleAELayerFSMVarApplyPoint(WleAEParamText *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
		wleAELayerFSMApplyInitAt(i);
		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_LOCATION_STRING;
			var->pcStringVal = StructAllocString( param->stringvalue );
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdateMessage(WleAEParamMessage* param, void* data, EditorObject* obj)
{
	WorldVariable *var;
	
	wleAELayerFSMUpdateInit();

	if (!properties)
		return;

	var = wleAELayerFSMVariableFromIndex(properties, param->index);

	if (var && var->eType == WVAR_MESSAGE)
	{
		langMakeEditorCopy(parse_DisplayMessage, &var->messageVal, true);
		groupDefFixupMessageKey(&var->messageVal.pEditorCopy->pcMessageKey, def, param->source_key, NULL);
		if( !var->messageVal.pEditorCopy->pcScope || !var->messageVal.pEditorCopy->pcScope[ 0 ]) {
			var->messageVal.pEditorCopy->pcScope = allocAddString(param->source_key);
		}
		StructCopyAll(parse_Message, var->messageVal.pEditorCopy, &param->message);
	}
}

static void wleAELayerFSMVarApplyMessage(WleAEParamMessage *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
		wleAELayerFSMApplyInitAt(i);

		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_MESSAGE;
			StructCopyAll( parse_Message, &param->message, var->messageVal.pEditorCopy );
			groupDefFixupMessageKey(&var->messageVal.pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !var->messageVal.pEditorCopy->pcScope || !var->messageVal.pEditorCopy->pcScope[ 0 ])
			{
				var->messageVal.pEditorCopy->pcScope = allocAddString( param->source_key );
			}
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdateString(WleAEParamText* param, void* data, EditorObject* obj)
{
	WorldVariable *var;
	
	wleAELayerFSMUpdateInit();

	if (!properties)
		return;
	
	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_STRING)
		param->stringvalue = StructAllocString( var->pcStringVal );
}

static void wleAELayerFSMVarApplyString(WleAEParamText *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
		wleAELayerFSMApplyInitAt(i);
	
		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);
	
		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_STRING;
			var->pcStringVal = StructAllocString( param->stringvalue );
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdateAnimation(WleAEParamDictionary* param, void* data, EditorObject* obj)
{
	WorldVariable *var;

	wleAELayerFSMUpdateInit();

	if (!properties)
		return;

	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_ANIMATION)
	{
		StructFreeString( param->refvalue );
		param->refvalue = StructAllocString( var->pcStringVal );
	}
}

static void wleAELayerFSMVarApplyAnimation(WleAEParamDictionary *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;

		wleAELayerFSMApplyInitAt(i);

		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_ANIMATION;
			var->pcStringVal = StructAllocString( param->refvalue );
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdateCritterDef(WleAEParamDictionary* param, void* data, EditorObject* obj)
{
	WorldVariable *var;

	wleAELayerFSMUpdateInit();

	if (!properties)
		return;

	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_CRITTER_DEF)
	{
		StructFreeString( param->refvalue );
		param->refvalue = StructAllocString( REF_STRING_FROM_HANDLE( var->hCritterDef ) );
	}
}

static void wleAELayerFSMVarApplyCritterDef(WleAEParamDictionary *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
		wleAELayerFSMApplyInitAt(i);

		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_CRITTER_DEF;
			SET_HANDLE_FROM_STRING("CritterDef", param->refvalue, var->hCritterDef);
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMVarUpdateCritterGroup(WleAEParamDictionary* param, void* data, EditorObject* obj)
{
	WorldVariable *var;

	wleAELayerFSMUpdateInit();

	if (!properties)
		return;

	var = wleAELayerFSMVariableFromIndex(properties, param->index);
	if (var && var->eType == WVAR_CRITTER_GROUP)
	{
		StructFreeString( param->refvalue );
		param->refvalue = StructAllocString( REF_STRING_FROM_HANDLE( var->hCritterGroup ) );
	}
}

static void wleAELayerFSMVarApplyCritterGroup(WleAEParamDictionary *param, void *data, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		WorldVariable *var;
		wleAELayerFSMApplyInitAt(i);

		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);

		var = wleAELayerFSMVariableFromIndex(properties, param->index);
		if (var)
		{
			var->eType = WVAR_CRITTER_GROUP;
			SET_HANDLE_FROM_STRING("CritterGroup", param->refvalue, var->hCritterGroup);
		}	
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAELayerFSMUpdate(WleAEParamDictionary* param, void* ignored, EditorObject* obj)
{
	wleAELayerFSMUpdateInit();

	if (!properties)
		return;

	param->refvalue = StructAllocString( REF_STRING_FROM_HANDLE( properties->hFSM ));
	param->is_specified = true;
}

static void wleAELayerFSMApply(WleAEParamDictionary *param, void *ignored, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAELayerFSMApplyInitAt(i);

		if (param->is_specified)
		{
			SET_HANDLE_FROM_STRING( "FSM", param->refvalue, properties->hFSM );
		}
		wleAELayerFSMEnsureVars(GET_REF(properties->hFSM), &properties->fsmVars);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

int wleAELayerFSMReload(EMPanel *panel, EditorObject *edObj)
{
	EditorObject **objects = NULL;
	int i;
	bool panelActive = true;
	bool allLayerFSMs = true;
	WorldScope *closest_scope = NULL;
	FSMExternVar** externVars = NULL;

	// Scan selected objects
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *tracker;

		assert(objects[i]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(objects[i]->obj);
		if (!tracker || !tracker->def)
		{
			// Hide panel if null tracker
			panelActive = allLayerFSMs = false;
			break;
		}
		if (!tracker->def->property_structs.layer_fsm_properties)
		{
			// Hide panel if not a spawn point
			allLayerFSMs = false;
			break;
		}

		// Disable panel if not editable
		if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
			panelActive = false;

		// Hide panel if layer fsm are not in the same scope
		if (!closest_scope)
			closest_scope = tracker->closest_scope;
		else if (closest_scope != tracker->closest_scope)
			allLayerFSMs = false;
	}
	eaDestroy(&objects);

	if (!allLayerFSMs)
		return WLE_UI_PANEL_INVALID;

	eaDestroyEx( &wleAEGlobalLayerFSMUI.named_point_names, NULL );
	if (closest_scope) {
		FOR_EACH_IN_STASHTABLE2(closest_scope->name_to_obj, it) {
			const char* key = stashElementGetKey( it );
			WorldEncounterObject* obj = stashElementGetPointer( it );

			if( obj->type == WL_ENC_NAMED_POINT ) {
				char buffer[ 256 ];
				sprintf( buffer, "namedpoint:%s", key );
				eaPush( &wleAEGlobalLayerFSMUI.named_point_names, strdup( buffer ));
			}
		} FOR_EACH_END;
		eaQSort( wleAEGlobalLayerFSMUI.named_point_names, utils_stricmp );
	}

	// fill data
	wleAEDictionaryUpdate(&wleAEGlobalLayerFSMUI.data.layerFSM);

	// Have to clear out the stringvalue, since it will hold a pointer
	// into named_point_names.  Deleting that pointer will be a double
	// delete.
	{
		int it;
		for( it = 0; it != eaSize( &wleAEGlobalLayerFSMUI.data.externVars ); ++it ) {
			WleAEParamLayerFSMVar* var = wleAEGlobalLayerFSMUI.data.externVars[ it ];
			if( var->pointVar ) {
				var->pointVar->stringvalue = NULL;
			}
		}
	}
	eaDestroyStruct( &wleAEGlobalLayerFSMUI.data.externVars, parse_WleAEParamLayerFSMVar );
	
	if (wleAEGlobalLayerFSMUI.data.layerFSM.refvalue && wleAEGlobalLayerFSMUI.data.layerFSM.refvalue[0]) {
		FSM* fsm = RefSystem_ReferentFromString("FSM", wleAEGlobalLayerFSMUI.data.layerFSM.refvalue);
		
		if (fsm)
		{
			int it;

			fsmGetExternVarNamesRecursive(fsm, &externVars, "layer");

			for( it = 0; it != eaSize( &externVars ); ++it ) {
				WleAEParamLayerFSMVar *varParam = StructCreate(parse_WleAEParamLayerFSMVar);

				// VARIABLE_TYPES: Add code below if add to the available variable types
				// You may or may not need a new pair of apply functions or a new param var

				switch(worldVariableTypeFromFSMExternVar( externVars[it] ))
				{
					case WVAR_FLOAT:
						varParam->floatVar = StructCreate(parse_WleAEParamFloat);
						varParam->floatVar->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->floatVar->left_pad = 0;
						varParam->floatVar->update_func = wleAELayerFSMVarUpdateFloat;
						varParam->floatVar->apply_func = wleAELayerFSMVarApplyFloat;
						varParam->floatVar->entry_width = 140;
						varParam->floatVar->index = it;
						wleAEFloatUpdate(varParam->floatVar);

					xcase WVAR_INT:
						varParam->intVar = StructCreate(parse_WleAEParamInt);
						varParam->intVar->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->intVar->left_pad = 0;
						varParam->intVar->update_func = wleAELayerFSMVarUpdateInt;
						varParam->intVar->apply_func = wleAELayerFSMVarApplyInt;
						varParam->intVar->entry_width = 140;
						varParam->intVar->index = it;
						wleAEIntUpdate(varParam->intVar);

					xcase WVAR_LOCATION_STRING:
						varParam->pointVar = StructCreate(parse_WleAEParamText);
						varParam->pointVar->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->pointVar->left_pad = 0;
						varParam->pointVar->update_func = wleAELayerFSMVarUpdatePoint;
						varParam->pointVar->apply_func = wleAELayerFSMVarApplyPoint;
						varParam->pointVar->entry_width = 1.0;
						varParam->pointVar->index = it;
						varParam->pointVar->available_values = wleAEGlobalLayerFSMUI.named_point_names;
						wleAETextUpdate(varParam->pointVar);

					xcase WVAR_MESSAGE:
						varParam->messageVar = StructCreate(parse_WleAEParamMessage);
						varParam->messageVar->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->messageVar->left_pad = 0;
						varParam->messageVar->update_func = wleAELayerFSMVarUpdateMessage;
						varParam->messageVar->apply_func = wleAELayerFSMVarApplyMessage;
						varParam->messageVar->entry_width = 1.0;
						varParam->messageVar->source_key = strdup(externVars[it]->name);
						varParam->messageVar->index = it;
						wleAEMessageUpdate(varParam->messageVar);

					xcase WVAR_STRING:
						varParam->stringVar = StructCreate(parse_WleAEParamText);
						varParam->stringVar->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->stringVar->left_pad = 0;
						varParam->stringVar->update_func = wleAELayerFSMVarUpdateString;
						varParam->stringVar->apply_func = wleAELayerFSMVarApplyString;
						varParam->stringVar->entry_width = 1.0;
						varParam->stringVar->index = it;
						wleAETextUpdate(varParam->stringVar);

					xcase WVAR_ANIMATION:
						varParam->animationVar = StructCreate(parse_WleAEParamDictionary);
						varParam->animationVar->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->animationVar->left_pad = 0;
						varParam->animationVar->update_func = wleAELayerFSMVarUpdateAnimation;
						varParam->animationVar->apply_func = wleAELayerFSMVarApplyAnimation;
						varParam->animationVar->entry_width = 1.0;
						varParam->animationVar->index = it;
						wleAEDictionaryUpdate(varParam->animationVar);

					xcase WVAR_CRITTER_DEF:
						varParam->critterDef = StructCreate(parse_WleAEParamDictionary);
						varParam->critterDef->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->critterDef->left_pad = 0;
						varParam->critterDef->update_func = wleAELayerFSMVarUpdateCritterDef;
						varParam->critterDef->apply_func = wleAELayerFSMVarApplyCritterDef;
						varParam->critterDef->entry_width = 1.0;
						varParam->critterDef->index = it;
						wleAEDictionaryUpdate(varParam->critterDef);

					xcase WVAR_CRITTER_GROUP:
						varParam->critterGroup = StructCreate(parse_WleAEParamDictionary);
						varParam->critterGroup->entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
						varParam->critterGroup->left_pad = 0;
						varParam->critterGroup->update_func = wleAELayerFSMVarUpdateCritterGroup;
						varParam->critterGroup->apply_func = wleAELayerFSMVarApplyCritterGroup;
						varParam->critterGroup->entry_width = 1.0;
						varParam->critterGroup->index = it;
						wleAEDictionaryUpdate(varParam->critterGroup);
				}

				eaPush(&wleAEGlobalLayerFSMUI.data.externVars, varParam);
			}
		}
	}

	// rebuild UI
	ui_RebuildableTreeInit(wleAEGlobalLayerFSMUI.autoWidget, &wleAEGlobalLayerFSMUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);
	wleAEDictionaryAddWidget(wleAEGlobalLayerFSMUI.autoWidget, "FSM", "The FSM to use", "layer_fsm", &wleAEGlobalLayerFSMUI.data.layerFSM);

	if (eaSize(&externVars) && eaSize(&wleAEGlobalLayerFSMUI.data.externVars))
	{
		int it;
		for( it = 0; it != eaSize( &wleAEGlobalLayerFSMUI.data.externVars ); ++it ) {
			char buffer[64];

			sprintf(buffer, "layer_extern_var_%d", it);
			wleAELayerFSMVarAddWidget(wleAEGlobalLayerFSMUI.autoWidget,
									  externVars[it]->name,
									  "External variable from FSM", buffer,
									  wleAEGlobalLayerFSMUI.data.externVars[it]);
		}
	}
	
	ui_RebuildableTreeDoneBuilding(wleAEGlobalLayerFSMUI.autoWidget);
	emPanelSetHeight(wleAEGlobalLayerFSMUI.panel, elUIGetEndY(wleAEGlobalLayerFSMUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalLayerFSMUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalLayerFSMUI.scrollArea->widget.children[0]->children) + 5;
	emPanelSetActive(wleAEGlobalLayerFSMUI.panel, panelActive);
	
	eaDestroy(&externVars);

	return WLE_UI_PANEL_OWNED;
}

void wleAELayerFSMCreate(EMPanel *panel)
{
	int i = 1;

	if (wleAEGlobalLayerFSMUI.autoWidget)
		return;

	wleAEGlobalLayerFSMUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalLayerFSMUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalLayerFSMUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	wleAEGlobalLayerFSMUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalLayerFSMUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	emPanelAddChild(panel, wleAEGlobalLayerFSMUI.scrollArea, false);

	// set parameter settings
	wleAEGlobalLayerFSMUI.data.layerFSM.dictionary = "FSM";
	wleAEGlobalLayerFSMUI.data.layerFSM.entry_align = WLE_AE_LAYER_FSM_ALIGN_WIDTH;
	wleAEGlobalLayerFSMUI.data.layerFSM.update_func = wleAELayerFSMUpdate;
	wleAEGlobalLayerFSMUI.data.layerFSM.apply_func = wleAELayerFSMApply;

	// cache the named points
}

static void wleAELayerFSMRefresh(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	wleAERefresh();
}

AUTO_RUN_LATE;
void wleAELayerFSMInit(void)
{
	resDictRegisterEventCallback("FSM", wleAELayerFSMRefresh, NULL);
}

#include "WorldEditorLayerFSMAttributes_c_ast.c"

#endif
