#ifndef NO_EDITORS

#include "WorldEditorActorAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorEncounterAttributes.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "StateMachine.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/

#define MAX_VARIABLES 32

#define wleAEActorUpdateInit()\
	WorldActorProperties *actor;\
	WorldEncounterProperties *encounter;\
	assert(obj->type->objType == EDTYPE_ENCOUNTER_ACTOR);\
	actor = wleEncounterActorFromHandle(obj->obj, NULL);\
	encounter = wleEncounterFromHandle(obj->obj);\

#define wleAEActorApplyInit()\
	WleEncObjSubHandle *subHandle = (WleEncObjSubHandle*) obj->obj;\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldEncounterProperties *encounter;\
	WorldActorProperties *actor;\
	assert(obj->type->objType == EDTYPE_ENCOUNTER_ACTOR);\
	tracker = wleOpPropsBegin(subHandle->parentHandle);\
	if (!tracker)\
		return;\
	def = tracker ? tracker->def : NULL;\
	encounter = def ? def->property_structs.encounter_properties : NULL;\
	actor = (encounter && subHandle->childIdx >= 0 && subHandle->childIdx < eaSize(&encounter->eaActors)) ? encounter->eaActors[subHandle->childIdx] : NULL;\
	if (!actor)\
	{\
		wleOpPropsEnd();\
		return;\
	}\

#define wleAEActorApplyInitAt(i)\
	WleEncObjSubHandle *subHandle = (WleEncObjSubHandle*) objs[i]->obj;\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldEncounterProperties *encounter;\
	WorldActorProperties *actor;\
	assert(objs[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR);\
	tracker = wleOpPropsBegin(subHandle->parentHandle);\
	if (!tracker)\
		continue;\
	def = tracker ? tracker->def : NULL;\
	encounter = def ? def->property_structs.encounter_properties : NULL;\
	actor = (encounter && subHandle->childIdx >= 0 && subHandle->childIdx < eaSize(&encounter->eaActors)) ? encounter->eaActors[subHandle->childIdx] : NULL;\
	if (!actor)\
	{\
		wleOpPropsEndNoUIUpdate();\
		continue;\
	}\


typedef struct WleActorFSMVarsUI
{
	UIButton *removeVarButton;
	WleAEParamWorldVariableDef actorVarDef;
} WleActorFSMVarsUI;

typedef struct WleAEActorUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;
	UIButton *addVarButton;

	WleActorFSMVarsUI **FSMVars;
	WleActorFSMVarsUI **InheritedVars;
	WleActorFSMVarsUI **ExtraVars;

	struct
	{
		WleAEParamBool overrideFSM;
		WleAEParamBool overrideDisplayName;
		WleAEParamBool overrideDisplaySubName;
		WleAEParamBool overrideCostume;
		WleAEParamBool overrideFaction;
		WleAEParamText actorName;
		WleAEParamDictionary actorFSM;
		WleAEParamMessage actorDisplayName;
		WleAEParamMessage actorDisplaySubName;
		WleAEParamDictionary actorCostume;
		WleAEParamDictionary actorFactionDef;
		WleAEParamBool overrideCritterDef;
		WleAEParamDictionary actorCritterDef;
	} data;
} WleAEActorUI;


/********************
* GLOBALS
********************/
static WleAEActorUI wleAEGlobalActorUI;

/********************
* PARAMETER CALLBACKS
********************/
static void wleAEActorGetTracker(EditorObject *obj, TrackerHandle **handle) {
	WleEncObjSubHandle *subHandle = (WleEncObjSubHandle*) obj->obj;
	assert(obj->type->objType == EDTYPE_ENCOUNTER_ACTOR);
	*(handle) = subHandle->parentHandle;
}

static void wleAEActorNameUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if (actor)
		param->stringvalue = StructAllocString(actor->pcName);
	else
		param->stringvalue = NULL;
}

static void wleAEActorNameApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		actor->pcName = StructAllocString(param->stringvalue);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorOverrideDisplayNameUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if(actor) {
		param->boolvalue = (actor->displayNameMsg.pEditorCopy != NULL) || (GET_REF(actor->displayNameMsg.hMessage) != NULL);
	} else {
		param->boolvalue = false;
	}
}

static void wleAEActorOverrideDisplayNameApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (actor) 
		{
			if(!param->boolvalue)
			{
				StructReset(parse_DisplayMessage, &actor->displayNameMsg);
			}

			if(param->boolvalue && !actor->displayNameMsg.pEditorCopy && !GET_REF(actor->displayNameMsg.hMessage))
			{
				WorldEncounterProperties *pEncounter = wleEncounterFromHandle(objs[i]->obj);
				EncounterTemplate *pTemplate = pEncounter ? GET_REF(pEncounter->hTemplate) : NULL;
				EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pEncounter, actor);

				langMakeEditorCopy(parse_DisplayMessage, &actor->displayNameMsg, true);

				if(pActor)
				{
					Message* pMsg = encounterTemplate_GetActorDisplayMessage(pTemplate, pActor, PARTITION_CLIENT);
					static char* estrKey = NULL;

					if(!estrKey)
					{
						estrCreate(&estrKey);
					}
			
					estrPrintf(&estrKey, "%s_%s_DisplayName", def->name_str, actor->pcName);

					if(pMsg)
					{
						StructCopyAll(parse_Message, pMsg, actor->displayNameMsg.pEditorCopy);
					}

					groupDefFixupMessageKey( &actor->displayNameMsg.pEditorCopy->pcMessageKey, def, estrKey, NULL );
					if( !actor->displayNameMsg.pEditorCopy->pcScope || !actor->displayNameMsg.pEditorCopy->pcScope[ 0 ])
					{
						actor->displayNameMsg.pEditorCopy->pcScope = allocAddString(wleAEGlobalActorUI.data.actorDisplayName.source_key);
					}
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorDisplayNameUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	// if Display Name exists on the actor, update the field
	if (actor && (GET_REF(actor->displayNameMsg.hMessage) || actor->displayNameMsg.pEditorCopy)) {
		static char* estrKey = NULL;
		TrackerHandle* trackerHandle = NULL;
		GroupTracker* tracker = NULL;
		GroupDef* def = NULL;

		wleAEActorGetTracker(obj, &trackerHandle);
		tracker = trackerHandle ? trackerFromTrackerHandle(trackerHandle) : NULL;
		def = tracker ? tracker->def : NULL;

		if(def)
		{
			if(!estrKey)
				estrCreate(&estrKey);

			estrPrintf(&estrKey, "%s_%s_DisplayName", def->name_str, actor->pcName);

			langMakeEditorCopy(parse_DisplayMessage, &actor->displayNameMsg, true);

			groupDefFixupMessageKey( &actor->displayNameMsg.pEditorCopy->pcMessageKey, def, estrKey, NULL );
			if( !actor->displayNameMsg.pEditorCopy->pcScope || !actor->displayNameMsg.pEditorCopy->pcScope[ 0 ]) {
				actor->displayNameMsg.pEditorCopy->pcScope = allocAddString(param->source_key);
			}

			StructCopyAll(parse_Message, actor->displayNameMsg.pEditorCopy, &param->message);
			return;
		}
	} 

	StructReset(parse_Message, &param->message);
}


static void wleAEActorDisplayNameApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (EMPTY_TO_NULL(param->message.pcDefaultString))
		{
			static char* estrKey = NULL;

			if(!estrKey)
			{
				estrCreate(&estrKey);
			}

			estrPrintf(&estrKey, "%s_%s_DisplayName", def->name_str, actor->pcName);
			langMakeEditorCopy(parse_DisplayMessage, &actor->displayNameMsg, true);
			StructCopyAll(parse_Message, &param->message, actor->displayNameMsg.pEditorCopy);

			groupDefFixupMessageKey( &actor->displayNameMsg.pEditorCopy->pcMessageKey, def, estrKey, NULL );
			if( !actor->displayNameMsg.pEditorCopy->pcScope || !actor->displayNameMsg.pEditorCopy->pcScope[ 0 ])
			{
				actor->displayNameMsg.pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		else
		{
			StructReset(parse_DisplayMessage, &actor->displayNameMsg);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorOverrideDisplaySubNameUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if(actor) {
		param->boolvalue = (actor->displaySubNameMsg.pEditorCopy != NULL) || (GET_REF(actor->displaySubNameMsg.hMessage) != NULL);
	} else {
		param->boolvalue = false;
	}
}

static void wleAEActorOverrideDisplaySubNameApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (actor) 
		{
			if(!param->boolvalue)
			{
				StructReset(parse_DisplayMessage, &actor->displaySubNameMsg);
			}

			if(param->boolvalue && !actor->displaySubNameMsg.pEditorCopy && !GET_REF(actor->displaySubNameMsg.hMessage))
			{
				WorldEncounterProperties *pEncounter = wleEncounterFromHandle(objs[i]->obj);
				EncounterTemplate *pTemplate = pEncounter ? GET_REF(pEncounter->hTemplate) : NULL;
				EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pEncounter, actor);

				langMakeEditorCopy(parse_DisplayMessage, &actor->displaySubNameMsg, true);

				if(pActor)
				{
					Message* pMsg = encounterTemplate_GetActorDisplaySubNameMessage(pTemplate, pActor, PARTITION_CLIENT);
					static char* estrKey = NULL;

					if(!estrKey)
					{
						estrCreate(&estrKey);
					}

					estrPrintf(&estrKey, "%s_%s_DisplaySubName", def->name_str, actor->pcName);

					if(pMsg)
					{
						StructCopyAll(parse_Message, pMsg, actor->displaySubNameMsg.pEditorCopy);
					}

					groupDefFixupMessageKey( &actor->displaySubNameMsg.pEditorCopy->pcMessageKey, def, estrKey, NULL );
					if( !actor->displaySubNameMsg.pEditorCopy->pcScope || !actor->displaySubNameMsg.pEditorCopy->pcScope[ 0 ])
					{
						actor->displaySubNameMsg.pEditorCopy->pcScope = allocAddString(wleAEGlobalActorUI.data.actorDisplaySubName.source_key);
					}
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorDisplaySubNameUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	// if Display Name exists on the actor, update the field
	if (actor && (GET_REF(actor->displaySubNameMsg.hMessage) || actor->displaySubNameMsg.pEditorCopy)) {
		static char* estrKey = NULL;
		TrackerHandle* trackerHandle = NULL;
		GroupTracker* tracker = NULL;
		GroupDef* def = NULL;

		wleAEActorGetTracker(obj, &trackerHandle);
		tracker = trackerHandle ? trackerFromTrackerHandle(trackerHandle) : NULL;
		def = tracker ? tracker->def : NULL;

		if(def)
		{
			if(!estrKey)
				estrCreate(&estrKey);

			estrPrintf(&estrKey, "%s_%s_DisplaySubName", def->name_str, actor->pcName);

			langMakeEditorCopy(parse_DisplayMessage, &actor->displaySubNameMsg, true);

			groupDefFixupMessageKey( &actor->displaySubNameMsg.pEditorCopy->pcMessageKey, def, estrKey, NULL );
			if( !actor->displaySubNameMsg.pEditorCopy->pcScope || !actor->displaySubNameMsg.pEditorCopy->pcScope[ 0 ]) {
				actor->displaySubNameMsg.pEditorCopy->pcScope = allocAddString(param->source_key);
			}

			StructCopyAll(parse_Message, actor->displaySubNameMsg.pEditorCopy, &param->message);
			return;
		}
	} 

	StructReset(parse_Message, &param->message);
}


static void wleAEActorDisplaySubNameApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (EMPTY_TO_NULL(param->message.pcDefaultString))
		{
			static char* estrKey = NULL;

			if(!estrKey)
			{
				estrCreate(&estrKey);
			}

			estrPrintf(&estrKey, "%s_%s_DisplaySubName", def->name_str, actor->pcName);
			langMakeEditorCopy(parse_DisplayMessage, &actor->displaySubNameMsg, true);
			StructCopyAll(parse_Message, &param->message, actor->displaySubNameMsg.pEditorCopy);

			groupDefFixupMessageKey( &actor->displaySubNameMsg.pEditorCopy->pcMessageKey, def, estrKey, NULL );
			if( !actor->displaySubNameMsg.pEditorCopy->pcScope || !actor->displaySubNameMsg.pEditorCopy->pcScope[ 0 ])
			{
				actor->displaySubNameMsg.pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		else
		{
			StructReset(parse_DisplayMessage, &actor->displaySubNameMsg);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorOverrideCostumeUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if(actor) {
		param->boolvalue = actor->pCostumeProperties != NULL;
	} else {
		param->boolvalue = false;
	}
}

static void wleAEActorOverrideCostumeApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (actor) 
		{
			if(!param->boolvalue && actor->pCostumeProperties)
			{
				StructDestroySafe(parse_WorldActorCostumeProperties, &actor->pCostumeProperties);
			}

			if(param->boolvalue && !actor->pCostumeProperties)
			{
				actor->pCostumeProperties = StructCreate(parse_WorldActorCostumeProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorCostumeUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	// if Costume exists on the actor, update the field
	if (actor && actor->pCostumeProperties) {
		const char *str = REF_STRING_FROM_HANDLE(actor->pCostumeProperties->hCostume);
		if (str && str[0])
		{
			param->refvalue = StructAllocString(str);
			return;
		}
	} 

	param->refvalue = NULL;
}


static void wleAEActorCostumeApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (param->refvalue && param->refvalue[0])
		{
			if(!actor->pCostumeProperties)
			{
				actor->pCostumeProperties = StructCreate(parse_WorldActorCostumeProperties);
			}
			SET_HANDLE_FROM_STRING("PlayerCostume", param->refvalue, actor->pCostumeProperties->hCostume);
		}
		else if(actor && actor->pCostumeProperties)
		{
			REMOVE_HANDLE(actor->pCostumeProperties->hCostume);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorOverrideFactionUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if(actor) {
		param->boolvalue = actor->pFactionProperties != NULL;
	} else {
		param->boolvalue = false;
	}
}
static void wleAEActorOverrideFactionApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (actor) 
		{
			if(!param->boolvalue && actor->pFactionProperties)
			{
				StructDestroySafe(parse_WorldActorFactionProperties, &actor->pFactionProperties);
			}
			if(param->boolvalue && !actor->pFactionProperties)
			{
				actor->pFactionProperties = StructCreate(parse_WorldActorFactionProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorFactionUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	// if faction exists on the actor, update the field
	if (actor && actor->pFactionProperties) {
		const char *str = REF_STRING_FROM_HANDLE(actor->pFactionProperties->hCritterFaction);
		if (str && str[0])
		{
			param->refvalue = StructAllocString(str);
			return;
		}
	} 

	param->refvalue = NULL;
}

static void wleAEActorFactionApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (param->refvalue && param->refvalue[0])
		{
			if(!actor->pFactionProperties)
			{
				actor->pFactionProperties = StructCreate(parse_WorldActorFactionProperties);
			}
			SET_HANDLE_FROM_STRING("CritterFaction", param->refvalue, actor->pFactionProperties->hCritterFaction);
		}
		else if(actor && actor->pFactionProperties)
		{
			REMOVE_HANDLE(actor->pFactionProperties->hCritterFaction);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorOverrideFSMUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if(actor) {
		param->boolvalue = actor->bOverrideFSM || GET_REF(actor->hFSMOverride);
	} else {
		param->boolvalue = false;
	}
}

// Refreshes the cached FSM variable list used to populate the variable combo box
static void wleAEActorRefreshFSMVarList(FSM* pFSM, EditorObject** eaObjects)
{
	FSMExternVar** eaFSMVarDefs = NULL;
	WorldVariableDef** eaAllVariableDefs = NULL;
	WorldVariable** eaAllVariables = NULL;
	WorldVariableDef** eaExtraVariableDefs = NULL;
	int i;
	int iVarIdx;

	// Build FSM vars
	if(pFSM) {
		eaCreate(&eaFSMVarDefs);
		if(pFSM->externVarList)
			fsmGetExternVarNamesRecursive(pFSM, &eaFSMVarDefs, "Encounter" );

		for(i=0; i<eaSize(&eaFSMVarDefs) && i < MAX_VARIABLES; ++i) {
			FSMExternVar *pVar = eaFSMVarDefs[i];
			WleActorFSMVarsUI *pVarUI = wleAEGlobalActorUI.FSMVars[i];

			pVarUI->actorVarDef.key = pVar->name;
		}
		while(i < MAX_VARIABLES)
		{
			wleAEGlobalActorUI.FSMVars[i]->actorVarDef.key = NULL;
			i++;
		}
	}

	// Find all inherited variables across all selected actors
	eaCreate(&eaAllVariableDefs);
	eaIndexedEnable(&eaAllVariableDefs, parse_WorldVariableDef);
	eaCreate(&eaAllVariables);
	eaIndexedEnable(&eaAllVariables, parse_WorldVariable);
	eaCreate(&eaExtraVariableDefs);
	for(i = 0; i < eaSize(&eaObjects); i++)
	{
		WleEncObjSubHandle *subHandle = eaObjects[i]->obj;
		WorldActorProperties *pWActor = wleEncounterActorFromHandle(subHandle, NULL);
		WorldEncounterProperties *encounterProperties = wleEncounterFromHandle(subHandle);
		EncounterTemplate *pTemplate = encounterProperties ? GET_REF(encounterProperties->hTemplate) : NULL;
		EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, encounterProperties, pWActor);

		encounterTemplate_GetActorFSMVarDefs(pTemplate, pActor, &eaAllVariableDefs, &eaAllVariables);
		encounterTemplate_GetEncounterFSMVarDefs(pTemplate, &eaAllVariableDefs, &eaAllVariables);
		encounterTemplate_GetActorCritterFSMVars(pTemplate, pActor, PARTITION_CLIENT, &eaAllVariableDefs, &eaAllVariables);
		encounter_GetWorldActorFSMVarDefs(pWActor, &eaExtraVariableDefs, NULL);
	}

	// Setup a param for each variable
	iVarIdx = 0;
	for(i = 0; i < eaSize(&eaAllVariableDefs) && iVarIdx < MAX_VARIABLES; i++)
	{
		// Only add it if was not already added with the FSM vars
		if(!pFSM || !fsmExternVarFromName(pFSM, eaAllVariableDefs[i]->pcName, "Encounter"))
		{
			wleAEGlobalActorUI.InheritedVars[iVarIdx]->actorVarDef.key = eaAllVariableDefs[i]->pcName;
			iVarIdx++;
		}
	}
	for(i = 0; i < eaSize(&eaAllVariables) && iVarIdx < MAX_VARIABLES; i++)
	{
		if(!pFSM || !fsmExternVarFromName(pFSM, eaAllVariables[i]->pcName, "Encounter"))
		{
			wleAEGlobalActorUI.InheritedVars[iVarIdx]->actorVarDef.key = eaAllVariables[i]->pcName;
			iVarIdx++;
		}
	}
	// Clear remaininig params
	while(iVarIdx < MAX_VARIABLES)
	{
		wleAEGlobalActorUI.InheritedVars[iVarIdx]->actorVarDef.key = NULL;
		iVarIdx++;
	}

	// Setup a param for the extra variables
	iVarIdx = 0;
	for(i = 0; i < eaSize(&eaExtraVariableDefs) && iVarIdx < MAX_VARIABLES; i++)
	{
		// Only add it if was not already added with the FSM vars or the Inherited Vars
		if( (!pFSM || !fsmExternVarFromName(pFSM, eaExtraVariableDefs[i]->pcName, "Encounter")) &&
			(eaIndexedFindUsingString(&eaAllVariables, eaExtraVariableDefs[i]->pcName) == -1) &&
			(eaIndexedFindUsingString(&eaAllVariableDefs, eaExtraVariableDefs[i]->pcName) == -1))
		{
			wleAEGlobalActorUI.ExtraVars[iVarIdx]->actorVarDef.key = eaExtraVariableDefs[i]->pcName;
			iVarIdx++;
		}
	}
	// Clear remaininig params
	while(iVarIdx < MAX_VARIABLES)
	{
		wleAEGlobalActorUI.ExtraVars[iVarIdx]->actorVarDef.key = NULL;
		iVarIdx++;
	}

	// Clean up
	eaDestroy(&eaAllVariableDefs);
	eaDestroy(&eaAllVariables);
	eaDestroy(&eaExtraVariableDefs);
}


static void wleAEActorOverrideFSMApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (actor) 
		{
			actor->bOverrideFSM = param->boolvalue;
			if(!param->boolvalue && GET_REF(actor->hFSMOverride))
			{
				REMOVE_HANDLE(actor->hFSMOverride);
			}

			if(param->boolvalue && !GET_REF(actor->hFSMOverride))
			{
				WorldEncounterProperties *pEncounter = wleEncounterFromHandle(objs[i]->obj);
				EncounterTemplate *pTemplate = pEncounter ? GET_REF(pEncounter->hTemplate) : NULL;
				EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pEncounter, actor);
				FSM* pTemplateActorFSM = NULL;

				if(pActor)
				{
					pTemplateActorFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, PARTITION_CLIENT);
					SET_HANDLE_FROM_REFERENT("fsm", pTemplateActorFSM, actor->hFSMOverride);
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorFSMUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	FSM* pFSM;
	wleAEActorUpdateInit();
	
	// if FSM exists on the actor, update the field
	if (actor && IS_HANDLE_ACTIVE(actor->hFSMOverride)) {
		const char *str = REF_STRING_FROM_HANDLE(actor->hFSMOverride);
		pFSM = GET_REF(actor->hFSMOverride);
		if (str && str[0])
		{
			param->refvalue = StructAllocString(str);
			return;
		}
	} 
	
	param->refvalue = NULL;
}


static void wleAEActorFSMApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (param->refvalue && param->refvalue[0])
		{
			FSM* pFSM = NULL;

			SET_HANDLE_FROM_STRING("FSM", param->refvalue, actor->hFSMOverride);
			pFSM = GET_REF(actor->hFSMOverride);
		}
		else if (actor->bOverrideFSM && param->is_specified)
		{
			WorldEncounterProperties *pEncounter = wleEncounterFromHandle(objs[i]->obj);
			EncounterTemplate *pTemplate = pEncounter ? GET_REF(pEncounter->hTemplate) : NULL;
			EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pEncounter, actor);
			FSM* pTemplateActorFSM = NULL;

			if(pActor)
			{
				pTemplateActorFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, PARTITION_CLIENT);
				SET_HANDLE_FROM_REFERENT("fsm", pTemplateActorFSM, actor->hFSMOverride);
			}
		}
		else
		{
			REMOVE_HANDLE(actor->hFSMOverride);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorOverrideCritterDefUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	if(actor) {
		param->boolvalue = (actor->pCritterProperties != NULL);
	} else {
		param->boolvalue = false;
	}
}

static void wleAEActorOverrideCritterDefApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (actor) 
		{
			if(!param->boolvalue && actor->pCritterProperties)
			{
				StructDestroySafe(parse_WorldActorCritterProperties, &actor->pCritterProperties);
			}
			if(param->boolvalue && !actor->pCritterProperties)
			{
				WorldEncounterProperties *pEncounter = wleEncounterFromHandle(objs[i]->obj);
				EncounterTemplate *pTemplate = pEncounter ? GET_REF(pEncounter->hTemplate) : NULL;
				EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pEncounter, actor);
				CritterDef* pTemplateActorCritterDef = NULL;

				actor->pCritterProperties = StructCreate(parse_WorldActorCritterProperties);

				if(pActor)
				{
					pTemplateActorCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, PARTITION_CLIENT);
					SET_HANDLE_FROM_REFERENT("CritterDef", pTemplateActorCritterDef, actor->pCritterProperties->hCritterDef);
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorCritterDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	wleAEActorUpdateInit();

	// if FSM exists on the actor, update the field
	if (actor && actor->pCritterProperties) {
		const char *str = REF_STRING_FROM_HANDLE(actor->pCritterProperties->hCritterDef);
		if (str && str[0])
		{
			param->refvalue = StructAllocString(str);
			return;
		}
	} 

	param->refvalue = NULL;
}

static void wleAEActorCritterDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEActorApplyInitAt(i);

		if (param->refvalue && param->refvalue[0])
		{
			if(!actor->pCritterProperties)
			{
				actor->pCritterProperties = StructCreate(parse_WorldActorCritterProperties);
			}
			SET_HANDLE_FROM_STRING("CritterDef", param->refvalue, actor->pCritterProperties->hCritterDef);
		}
		else if(actor->pCritterProperties)
		{
			REMOVE_HANDLE(actor->pCritterProperties->hCritterDef);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorVarUpdate(WleAEParamWorldVariableDef *param, void* unused, EditorObject *obj)
{
	const char* pchKey = param ? param->key : NULL;
	wleAEActorUpdateInit();

	if(EMPTY_TO_NULL(pchKey)) {
		WorldVariableDef *pVarDef = NULL;
		EncounterTemplate *pTemplate = encounter ? GET_REF(encounter->hTemplate) : NULL;
		EncounterActorProperties *pTemplateActor = encounterTemplate_GetActorFromWorldActor(pTemplate, encounter, actor);
		CritterDef *pActorCritter = actor && actor->pCritterProperties ? GET_REF(actor->pCritterProperties->hCritterDef) : NULL;
		FSM *pFSM = actor ? GET_REF(actor->hFSMOverride) : NULL;
		EncounterAIProperties *pAIProperties = pTemplate ? encounterTemplate_GetAIProperties(pTemplate) : NULL;
		int iPartitionIdx = PARTITION_CLIENT;
		if(!pFSM && pTemplate)
			pFSM = encounterTemplate_GetActorFSM(pTemplate, pTemplateActor, iPartitionIdx);

		if(!pActorCritter && pTemplate && pTemplateActor)
		{
			pActorCritter = encounterTemplate_GetActorCritterDef(pTemplate, pTemplateActor, iPartitionIdx);
		}

		// Search for var on World Actor
		if(actor && actor->eaFSMVariableDefs) {
			pVarDef = eaIndexedGetUsingString(&actor->eaFSMVariableDefs, pchKey);
			if(pVarDef) {
				StructCopyAll(parse_WorldVariableDef, pVarDef, &param->var_def);
				param->is_specified = true;
				worldVariableDefCleanup(&param->var_def);
				param->scope = actor->pcName;
				return;
			}
		}

		// Search for inherited var
		// Template Actor
		if(pTemplateActor && pTemplateActor->eaVariableDefs) {
			pVarDef = eaIndexedGetUsingString(&pTemplateActor->eaVariableDefs, pchKey);
		}
		// Template
		if(!pVarDef && pAIProperties && pAIProperties->eaVariableDefs) {
			pVarDef = eaIndexedGetUsingString(&pAIProperties->eaVariableDefs, pchKey);
		}
		// Critter
		if(!pVarDef && pActorCritter && pActorCritter->ppCritterVars) {
			pVarDef = eaIndexedGetUsingString(&pActorCritter->ppCritterVars, pchKey);
		}

		// Add var if found
		if(pVarDef) {
			StructCopyAll(parse_WorldVariableDef, pVarDef, &param->var_def);
			param->is_specified = false;
			worldVariableDefCleanup(&param->var_def);
			return;
		}

		// Look for var on FSM if not found
		if(pFSM)
		{
			FSMExternVar *pFSMVar = fsmExternVarFromName(pFSM, pchKey, "Encounter");
			if(pFSMVar) {
				param->var_def.pcName = allocAddString(pchKey);
				param->var_def.eType = worldVariableTypeFromFSMExternVar(pFSMVar);
				worldVariableDefCleanup(&param->var_def);
				param->is_specified = false;
				return;
			}
		}
	}

	// Var not found, clear the param
	if(param) {
		param->key = NULL;
		StructReset(parse_WorldVariableDef, &param->var_def);
		param->is_specified = false;
	}
}

static void wleAEActorVarApply(WleAEParamWorldVariableDef *param, void* unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		const char* pchKey = param ? param->key : NULL;
		wleAEActorApplyInitAt(i);

		if(EMPTY_TO_NULL(pchKey) && actor)
		{
			if (param->is_specified) 
			{
				// Var is specified, copy it to the world actor
				WorldVariableDef *pNewDef = StructClone(parse_WorldVariableDef, &param->var_def);
				int idx = eaIndexedFindUsingString(&actor->eaFSMVariableDefs, pchKey);
				worldVariableDefCleanup(pNewDef);
				if(!actor->eaFSMVariableDefs)
				{
					eaCreate(&actor->eaFSMVariableDefs);
				}
				else if(idx > -1)
				{
					WorldVariableDef *pOldVar = eaRemove(&actor->eaFSMVariableDefs, idx);
					if(pOldVar)
					{
						StructDestroy(parse_WorldVariableDef, pOldVar);
					}
				}
				eaIndexedAdd(&actor->eaFSMVariableDefs, pNewDef);
			}
			else if(actor->eaFSMVariableDefs)
			{
				// Var is not specified, remove it from the world actor
				int idx = eaIndexedFindUsingString(&actor->eaFSMVariableDefs, pchKey);
				if(idx > -1)
				{
					WorldVariableDef *pFoundDef = eaRemove(&actor->eaFSMVariableDefs, idx);
					if(pFoundDef)
					{
						StructDestroy(parse_WorldVariableDef, pFoundDef);
					}
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEActorEditFSMButtonCB(UIButton *pButton, char* pcFSMName)
{
	if (EMPTY_TO_NULL(pcFSMName))
		emOpenFileEx(pcFSMName, "fsm");
}

static void wleAEActorAddVarButtonCB(UIButton *pButton, void* unused)
{
	EditorObject **objects = NULL;
	char* estrVarName = NULL;
	int iNameIdx = 0;
	int i;

	wleAEGetSelectedObjects(&objects);
	estrCreate(&estrVarName);
	// Determine the variable name
	for (i = 0; i < eaSize(&objects); i++)
	{
		WleEncObjSubHandle *subHandle = objects[i]->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, NULL);
		assert(objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR);
		if (!actor)
			continue;
		else {
			if(!EMPTY_TO_NULL(estrVarName))
				estrPrintf(&estrVarName, "New_Var%d", iNameIdx);

			while(eaIndexedFindUsingString(&actor->eaFSMVariableDefs, estrVarName) > -1)
			{
				iNameIdx++;
				estrPrintf(&estrVarName, "New_Var%d", iNameIdx);
			}
		}
	}

	// Add variable to all selected actors
	for (i = 0; i < eaSize(&objects); i++)
	{
		WleEncObjSubHandle *subHandle = objects[i]->obj;
		WorldActorProperties *actor = NULL;
		GroupDef *def;
		WorldEncounterProperties *encounter;
		GroupTracker *tracker;
		WorldVariableDef* pDef = NULL;

		assert(objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR);

		tracker = wleOpPropsBegin(subHandle->parentHandle);
		if (!tracker)
			continue;

		def = tracker ? tracker->def : NULL;
		encounter = def ? def->property_structs.encounter_properties : NULL;
		actor = (encounter && subHandle->childIdx >= 0 && subHandle->childIdx < eaSize(&encounter->eaActors)) ? encounter->eaActors[subHandle->childIdx] : NULL;

		if (actor) {		
			pDef = StructCreate(parse_WorldVariableDef);
			pDef->pcName = allocAddString(estrVarName);

			eaPush(&actor->eaFSMVariableDefs, pDef);

			wleOpPropsEnd();
		}
	}
	estrDestroy(&estrVarName);
}

static void wleAEActorRemoveVarButtonCB(UIButton *pButton, WleActorFSMVarsUI* pVarUI)
{
	EditorObject **objects = NULL;
	int i;

	// Remove variable from all selected actors
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		WleEncObjSubHandle *subHandle = objects[i]->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, NULL);

		assert(objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR);
		if (!actor)
			continue;
		else {
			GroupTracker *tracker;
			GroupDef *def;
			WorldEncounterProperties *encounter;
			tracker = wleOpPropsBegin(subHandle->parentHandle);
			if (!tracker)
				continue;
			
			def = tracker ? tracker->def : NULL;
			encounter = def ? def->property_structs.encounter_properties : NULL;
			actor = (encounter && subHandle->childIdx >= 0 && subHandle->childIdx < eaSize(&encounter->eaActors)) ? encounter->eaActors[subHandle->childIdx] : NULL;
			if (!actor)
			{
				wleOpPropsEnd();
				continue;
			}

			if(actor->eaFSMVariableDefs && pVarUI->actorVarDef.key) {
				int idx = eaIndexedFindUsingString(&actor->eaFSMVariableDefs, pVarUI->actorVarDef.key);
				if(idx > -1) {
					WorldVariableDef *pFoundDef = eaRemove(&actor->eaFSMVariableDefs, idx);
					if(pFoundDef)
						StructDestroy(parse_WorldVariableDef, pFoundDef);
				}
			}

			wleOpPropsEnd();
		}
	}
	if(pVarUI)
		pVarUI->actorVarDef.key = NULL;
}


/********************
* MAIN
********************/
int wleAEActorReload(EMPanel *panel, EditorObject *edObj)
{
	EditorObject **objects = NULL;
	EditorObject *obj = NULL;
	UIWidget *pLabelWidget = NULL;
	UISeparator *pSeparator = NULL;
	UIButton *button = NULL;
	UIAutoWidgetParams params = {0};
	UIAutoWidgetParams buttonParams = {0};
	int i;
	bool panelActive = true;
	const char* firstFSM = NULL;
	bool fsmMatch = true;
	const char* pchComments = NULL;
	const char* pchDisplayName = NULL;
	const char* pchDisplaySubName = NULL;
	FSM *pFSM = NULL;
	char* estrVarName = NULL;
	char* estrBuf = NULL;
	bool bCritterDefSourceMatch = true;
	bool bSingleActorEncounter = true;
	bool bCritterDefOverridden = false;
	bool bFillActorsInOrder = false;
	const char* pchActorName = NULL;
	EncounterSharedCritterGroupSource eCritterGroupSource = EncounterSharedCritterGroupSource_Specified;
	CritterDef* pCritterDef = NULL;
	int iPartitionIdx = PARTITION_CLIENT;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		WleEncObjSubHandle *subHandle = objects[i]->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, NULL);
		WorldEncounterProperties *encounterProperties = wleEncounterFromHandle(subHandle);
		EncounterTemplate *pTemplate = encounterProperties ? GET_REF(encounterProperties->hTemplate) : NULL;
		EncounterActorProperties *pTemplateActor = encounterTemplate_GetActorFromWorldActor(pTemplate, encounterProperties, actor);

		assert(objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR);
		if (!actor)
			continue;
		else if (!wleTrackerIsEditable(subHandle->parentHandle, false, false, false))
			panelActive = false;
		else {
			// See if all selected actors have the same FSM
			if(fsmMatch) {
				FSM* currentFSM = NULL;
				const char* currentFSMName = NULL;

				if(actor) {
					if(actor->bOverrideFSM) {
						currentFSM = GET_REF(actor->hFSMOverride);
					} else if(pTemplate) {
						if(pTemplateActor)
							currentFSM = encounterTemplate_GetActorFSM(pTemplate, pTemplateActor, iPartitionIdx);
					}
				}

				pFSM = currentFSM;

				currentFSMName = currentFSM ? NULL_TO_EMPTY(currentFSM->name) : "";

				if(!firstFSM) {
					firstFSM = currentFSMName;
				}
				if(!firstFSM || !currentFSMName || stricmp(firstFSM, currentFSMName))
				{
					fsmMatch = false;
					pFSM = NULL;
				}
			}

			if(pTemplateActor)
			{
				// Set initial comments text
				if(i == 0)
				{
					if(EMPTY_TO_NULL(pTemplateActor->nameProps.pchComments))
						pchComments = pTemplateActor->nameProps.pchComments;
				}
				else if(pchComments)
				{
					// Check for mis-matching comments
					if(stricmp(NULL_TO_EMPTY(pTemplateActor->nameProps.pchComments), pchComments)!=0) {
						pchComments = NULL;
					}
				}

				// Set Crittter Group
				if(i == 0)
				{
					eCritterGroupSource = encounterTemplate_GetCritterGroupSource(pTemplate);
					pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pTemplateActor, iPartitionIdx);
				} 
				else
				{
					CritterDef *pCurrentCritterDef = actor->pCritterProperties ? GET_REF(actor->pCritterProperties->hCritterDef) : NULL;
					if(!pCurrentCritterDef)
						pCurrentCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pTemplateActor, iPartitionIdx);

					if(bCritterDefSourceMatch && (eCritterGroupSource != pTemplateActor->critterProps.eCritterType))
					{
						bCritterDefSourceMatch = false;
					}
					if(pCritterDef && pCritterDef != pCurrentCritterDef)
					{
						pCritterDef = NULL;
					}
				}
			}

			// Check for Single Actor Template
			if(bSingleActorEncounter)
			{
				if(pTemplate)
				{
					EncounterActorProperties **eaActors = NULL;
					encounterTemplate_FillActorEarray(pTemplate, &eaActors);
					if(!eaActors || eaSize(&eaActors) != 1)
					{
						bSingleActorEncounter = false;
					}
					eaDestroy(&eaActors);
				}
			}

			// Check for overridden critter def
			if(actor && actor->pCritterProperties)
			{
				bCritterDefOverridden = true;
			}

			// Set initial display name
			if(i == 0)
			{
				pchDisplayName = actor ? TranslateDisplayMessage(actor->displayNameMsg) : NULL;

				if(!pchDisplayName)
				{
					//Message* pMsg = encounter_GetActorDisplayMessage(pTemplate, pTemplateActor, iPartitionIdx);
					Message* pMsg = encounter_GetActorDisplayMessage(iPartitionIdx, encounterProperties, actor);
					if(pMsg)
						pchDisplayName = TranslateMessagePtr(pMsg);
				}
			}
			else if(pchDisplayName)
			{
				const char* pchCurrentDisplayName = actor ? TranslateDisplayMessage(actor->displayNameMsg) : NULL;
				if(!pchCurrentDisplayName)
				{
					Message* pMsg = encounterTemplate_GetActorDisplayMessage(pTemplate, pTemplateActor, iPartitionIdx);
					if(pMsg)
						pchCurrentDisplayName = TranslateMessagePtr(pMsg);
				}

				// Check for mis-matching display names
				if(stricmp(NULL_TO_EMPTY(pchCurrentDisplayName), pchDisplayName)!=0) {
					pchDisplayName = NULL;
				}
			}

			// Set initial display sub name
			if(i == 0)
			{
				pchDisplaySubName = actor ? TranslateDisplayMessage(actor->displaySubNameMsg) : NULL;

				if(!pchDisplaySubName)
				{
					Message* pMsg = encounterTemplate_GetActorDisplaySubNameMessage(pTemplate, pTemplateActor, iPartitionIdx);
					if(pMsg)
						pchDisplaySubName = TranslateMessagePtr(pMsg);
				}
			}
			else if(pchDisplaySubName)
			{
				const char* pchCurrentDisplaySubName = actor ? TranslateDisplayMessage(actor->displaySubNameMsg) : NULL;
				if(!pchCurrentDisplaySubName)
				{
					Message* pMsg = encounterTemplate_GetActorDisplaySubNameMessage(pTemplate, pTemplateActor, iPartitionIdx);
					if(pMsg)
						pchCurrentDisplaySubName = TranslateMessagePtr(pMsg);
				}

				// Check for mis-matching display names
				if(stricmp(NULL_TO_EMPTY(pchCurrentDisplaySubName), pchDisplaySubName)!=0) {
					pchDisplaySubName = NULL;
				}
			}

			// Check for fillActorsInOrder flag
			if(encounterProperties && encounterProperties->bFillActorsInOrder)
			{
				bFillActorsInOrder = true;
			}

			// Set actor name
			if(i == 0) {
				pchActorName = actor ? actor->pcName : NULL;
			} else if(pchActorName) {
				if(actor && stricmp(actor->pcName, pchActorName) != 0)
					pchActorName = NULL;
			}
		}

	}
	if (i == 1)
	{
		obj = objects[0];
	}

	/***** fill data *****/
	wleAETextUpdate(&wleAEGlobalActorUI.data.actorName);
	wleAEBoolUpdate(&wleAEGlobalActorUI.data.overrideFSM);
	wleAEDictionaryUpdate(&wleAEGlobalActorUI.data.actorFSM);
	wleAEBoolUpdate(&wleAEGlobalActorUI.data.overrideDisplayName);
	wleAEBoolUpdate(&wleAEGlobalActorUI.data.overrideDisplaySubName);
	wleAEMessageUpdate(&wleAEGlobalActorUI.data.actorDisplayName);
	wleAEMessageUpdate(&wleAEGlobalActorUI.data.actorDisplaySubName);
	wleAEBoolUpdate(&wleAEGlobalActorUI.data.overrideCostume);
	wleAEDictionaryUpdate(&wleAEGlobalActorUI.data.actorCostume);
	wleAEBoolUpdate(&wleAEGlobalActorUI.data.overrideCritterDef);
	wleAEDictionaryUpdate(&wleAEGlobalActorUI.data.actorCritterDef);
	wleAEBoolUpdate(&wleAEGlobalActorUI.data.overrideFaction);
	wleAEDictionaryUpdate(&wleAEGlobalActorUI.data.actorFactionDef);

	// Refresh Vars
	wleAEActorRefreshFSMVarList(pFSM, objects);
	for(i = 0; i < MAX_VARIABLES; i++) {
		wleAEWorldVariableDefUpdate(&wleAEGlobalActorUI.FSMVars[i]->actorVarDef);
	}
	for(i = 0; i < MAX_VARIABLES; i++) {
		wleAEWorldVariableDefUpdate(&wleAEGlobalActorUI.InheritedVars[i]->actorVarDef);
	}
	for(i = 0; i < MAX_VARIABLES; i++) {
		wleAEWorldVariableDefUpdate(&wleAEGlobalActorUI.ExtraVars[i]->actorVarDef);
	}


	/****** rebuild UI ******/
	ui_RebuildableTreeInit(wleAEGlobalActorUI.autoWidget, &wleAEGlobalActorUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);

	// Name
	if(bFillActorsInOrder)
	{
		ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalActorUI.autoWidget->root, "Name", "The name used to reference the actor within it's encounter template.", "encounter_actor_name_label", &params, false);
		if(pchActorName)
		{
			params.alignTo += 99;
			ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, pchActorName, "encounter_actor_name_value", &params, false);
			params.alignTo -= 99;
		}
	}
	else
	{
		wleAETextAddWidget(wleAEGlobalActorUI.autoWidget, "Name", "The name used to reference the actor within it's encounter template.", "encounter_actor_name", &wleAEGlobalActorUI.data.actorName);
	}

	if(pchComments)
	{
		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, pchComments, "ActorComments", &params, true);
	}

	// Display Name checkbutton
	wleAEBoolAddWidget(wleAEGlobalActorUI.autoWidget, NULL, "Override the actor's Display Name", "overrideDisplayNameCheckbox", &wleAEGlobalActorUI.data.overrideDisplayName);
	params.alignTo += 25;
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "Disp. Name", "overrideDisplayNameLabel", &params, false);
	params.alignTo -= 25;

	if(wleAEGlobalActorUI.data.overrideDisplayName.boolvalue)
	{
		// Overridden Display Name
		wleAEMessageAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "The Display Name for this actor.", "DisplayName", &wleAEGlobalActorUI.data.actorDisplayName, false);
	} 
	else if(pchDisplayName)
	{
		// Inherited Display Name
		params.alignTo += 99;
		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, pchDisplayName, "InheritedDisplayNameValue", &params, false);
		params.alignTo -= 99;
	}

	// Display SubName checkbutton
	wleAEBoolAddWidget(wleAEGlobalActorUI.autoWidget, NULL, "Override the actor's Display Sub Name", "overrideDisplaySubNameCheckbox", &wleAEGlobalActorUI.data.overrideDisplaySubName);
	params.alignTo += 25;
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "Disp. SubName", "overrideDisplaySubNameLabel", &params, false);
	params.alignTo -= 25;

	if(wleAEGlobalActorUI.data.overrideDisplaySubName.boolvalue)
	{
		// Overridden Display Name
		wleAEMessageAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "The Display Sub Name for this actor.", "DisplaySubName", &wleAEGlobalActorUI.data.actorDisplaySubName, false);
	} 
	else if(pchDisplaySubName)
	{
		// Inherited Display Name
		params.alignTo += 99;
		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, pchDisplaySubName, "InheritedDisplaySubNameValue", &params, false);
		params.alignTo -= 99;
	}

	// Costume checkbutton
	wleAEBoolAddWidget(wleAEGlobalActorUI.autoWidget, NULL, "Override the actor's Costume", "overrideCostumeCheckbox", &wleAEGlobalActorUI.data.overrideCostume);
	params.alignTo += 25;
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "Costume", "overrideCostumeLabel", &params, false);
	params.alignTo -= 25;

	if(wleAEGlobalActorUI.data.overrideCostume.boolvalue)
	{
		// Overridden Display Name
		wleAEDictionaryAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "The Costume for this actor.", "Costume", &wleAEGlobalActorUI.data.actorCostume, false);
	} 

	// Faction
	wleAEBoolAddWidget(wleAEGlobalActorUI.autoWidget, NULL, "Override the actor's faction", "overrideFactionCheckbox", &wleAEGlobalActorUI.data.overrideFaction);
	params.alignTo += 25;
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "Faction", "overrideFactionLabel", &params, false);
	params.alignTo -= 25;

	if(wleAEGlobalActorUI.data.overrideFaction.boolvalue)
	{
		// Overridden Display Name
		wleAEDictionaryAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "The faction for this actor.", "Faction", &wleAEGlobalActorUI.data.actorFactionDef, false);
	} 

	// Critter Def
	if(bSingleActorEncounter || bCritterDefOverridden)
	{
		wleAEBoolAddWidget(wleAEGlobalActorUI.autoWidget, NULL, "Override the actor's Critter Def", "overrideCritterDefCheckbox", &wleAEGlobalActorUI.data.overrideCritterDef);
		params.alignTo += 25;
		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "Critter Def", "overrideCritterDefLabel", &params, false);
		params.alignTo -= 25;
	}
	
	if(bSingleActorEncounter)
	{
		if(wleAEGlobalActorUI.data.overrideCritterDef.boolvalue)
		{
			// Overridden Critter Def
			wleAEDictionaryAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "The Critter Def this actor uses.", "CritterDef", &wleAEGlobalActorUI.data.actorCritterDef, false);
		} 
		else
		{
			params.alignTo += 99;
			if(bCritterDefSourceMatch)
			{
				switch(eCritterGroupSource)
				{
					xcase ActorCritterType_CritterGroup:
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "(From Critter Group)  ", "CritterDefSource", &params, false);
					xcase ActorCritterType_FromTemplate:
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "(From Template)  ", "CritterDefSource", &params, false);
					xcase ActorCritterType_MapVariableGroup:
					case ActorCritterType_MapVariableDef:
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "(From Map Variable)  ", "CritterDefSource", &params, false);
					xcase ActorCritterType_PetContactList:
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "(From Pet Contact List)  ", "CritterDefSource", &params, false);
					xdefault:
						break;
				}
			}

			if(pCritterDef)
			{
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, pCritterDef->pchName, "InheritedCritterDefValue", &params, false);
			}
			params.alignTo -= 99;
		}
	}

	// FSM checkbutton
	wleAEBoolAddWidget(wleAEGlobalActorUI.autoWidget, NULL, "Override the actor's FSM", "overrideFSMCheckbox", &wleAEGlobalActorUI.data.overrideFSM);
	params.alignTo += 25;
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, "FSM", "overrideFSMLabel", &params, false);
	params.alignTo -= 25;

	if(wleAEGlobalActorUI.data.overrideFSM.boolvalue)
	{
		// Overridden FSM
		wleAEDictionaryAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "The FSM this actor uses.", "fsm", &wleAEGlobalActorUI.data.actorFSM, false);
	} 
	else if(fsmMatch)
	{
		// Inherited FSM
		params.alignTo += 99;
		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, NULL_TO_EMPTY(firstFSM), "InheritedFSMValue", &params, false);
		params.alignTo -= 99;
	}

	buttonParams.alignTo = 90;

	// Display Vars
	estrCreate(&estrVarName);
	estrCreate(&estrBuf);

	// FSM VARS
	// Separator with padding
	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_RebuildableTreeAddWidget(wleAEGlobalActorUI.autoWidget->root, UI_WIDGET(pSeparator), NULL, true, "VarSeparator", NULL);
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, NULL, "VarSpacer", NULL, true);
	pLabelWidget = ui_RebuildableTreeGetWidgetByName(wleAEGlobalActorUI.autoWidget, "VarSpacer");
	if(pLabelWidget)
		ui_WidgetSetHeight(pLabelWidget, 5);

	// Header
	ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalActorUI.autoWidget->root, "FSM Variables", "Variables which apply to the current FSM.", "FSMVarsHeader", NULL, true);
	pLabelWidget = ui_RebuildableTreeGetWidgetByName(wleAEGlobalActorUI.autoWidget, "FSMVarsHeader");
	if(pLabelWidget)
		ui_WidgetSetFont(pLabelWidget, "Default_Bold");

	// Vars
	for(i=0; i< MAX_VARIABLES; i++)
	{
		if(!EMPTY_TO_NULL(wleAEGlobalActorUI.FSMVars[i]->actorVarDef.key))
		{
			continue;
		}
		estrPrintf(&estrVarName, "FSMVar%d", i);
		wleAEWorldVariableDefAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "A World Variable Def used in this FSM.", NULL, estrVarName, &wleAEGlobalActorUI.FSMVars[i]->actorVarDef);
	}

	// ADDITIONAL VARS

	// Padding
	ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, NULL, "VarSpacer2", NULL, true);
	pLabelWidget = ui_RebuildableTreeGetWidgetByName(wleAEGlobalActorUI.autoWidget, "VarSpacer2");
	if(pLabelWidget)
		ui_WidgetSetHeight(pLabelWidget, 10);

	// Header
	ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalActorUI.autoWidget->root, "Additional Variables", "Variables which do not apply to the current FSM.", "InheritedVarsHeader", NULL, true);
	pLabelWidget = ui_RebuildableTreeGetWidgetByName(wleAEGlobalActorUI.autoWidget, "InheritedVarsHeader");
	if(pLabelWidget)
		ui_WidgetSetFont(pLabelWidget, "Default_Bold");

	// Inherited Vars
	for(i=0; i< MAX_VARIABLES; i++)
	{
		if(!EMPTY_TO_NULL(wleAEGlobalActorUI.InheritedVars[i]->actorVarDef.key))
		{
			continue;
		}		
		estrPrintf(&estrVarName, "InheritedVar%d", i);
		wleAEWorldVariableDefAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, NULL, "An inherited World Variable Def not used in the FSM.", NULL, estrVarName, &wleAEGlobalActorUI.InheritedVars[i]->actorVarDef);
	}

	// Extra Vars
	for(i=0; i< MAX_VARIABLES; i++)
	{
		if(!EMPTY_TO_NULL(wleAEGlobalActorUI.ExtraVars[i]->actorVarDef.key))
		{
			continue;
		}
		// Remove button

		// Padding
		estrPrintf(&estrBuf, "ExtraVarSpacer%d", i);
		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalActorUI.autoWidget->root, NULL, estrBuf, NULL, true);
		pLabelWidget = ui_RebuildableTreeGetWidgetByName(wleAEGlobalActorUI.autoWidget, estrBuf);
		if(pLabelWidget)
			ui_WidgetSetHeight(pLabelWidget, 5);
		// Var
		estrPrintf(&estrVarName, "ExtraVar%d", i);
		wleAEWorldVariableDefAddWidgetEx(wleAEGlobalActorUI.autoWidget->root, "    Name", "A World Variable Def defined on the World Actor and not used in the FSM.", NULL, estrVarName, &wleAEGlobalActorUI.ExtraVars[i]->actorVarDef);

		// Remove button
		wleAEGlobalActorUI.ExtraVars[i]->removeVarButton = ui_ButtonCreate("Remove Variable", 0, 0, wleAEActorRemoveVarButtonCB, wleAEGlobalActorUI.ExtraVars[i]);
		estrPrintf(&estrBuf, "RemButton%d", i);
		ui_RebuildableTreeAddWidget(wleAEGlobalActorUI.autoWidget->root, UI_WIDGET(wleAEGlobalActorUI.ExtraVars[i]->removeVarButton), NULL, true, estrBuf, &buttonParams);
	}

	// Add vars button
 	wleAEGlobalActorUI.addVarButton = ui_ButtonCreate("Add Variable", 0, 0, wleAEActorAddVarButtonCB, NULL);
 	ui_RebuildableTreeAddWidget(wleAEGlobalActorUI.autoWidget->root, UI_WIDGET(wleAEGlobalActorUI.addVarButton), NULL, true, "AddVarButton", NULL);

	estrDestroy(&estrVarName);
	estrDestroy(&estrBuf);

	// Text overview of entire actor
	// Only available if you select a single actor
	if (obj)
	{
		WleEncObjSubHandle *subHandle = obj->obj;
		WorldEncounterProperties *encounterProperties = wleEncounterFromHandle(subHandle);
		WorldActorProperties *pWorldActor = wleEncounterActorFromHandle(subHandle, NULL);
		EncounterTemplate *pTemplate = encounterProperties ? GET_REF(encounterProperties->hTemplate) : NULL;
		EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, encounterProperties, pWorldActor);

		// Actor Display
		if (pTemplate && pWorldActor)
		{
			ui_RebuildableTreeAddWidget(wleAEGlobalActorUI.autoWidget->root, UI_WIDGET(ui_SeparatorCreate(UIHorizontal)), NULL, true, "DisplaySeparator", NULL);

			if (pActor)
			{
				// Actor display
				wleAEEncounterShowActor(wleAEGlobalActorUI.autoWidget->root, pTemplate, pActor, pWorldActor, 0, 0);
			}
			else
			{
				ui_RebuildableTreeAddLabel(wleAEGlobalActorUI.autoWidget->root, "Actor has no matching definition", &params, true);
			}
		}
	}

	// Clean up
	eaDestroy(&objects);

	ui_RebuildableTreeDoneBuilding(wleAEGlobalActorUI.autoWidget);

	emPanelSetHeight(wleAEGlobalActorUI.panel, elUIGetEndY(wleAEGlobalActorUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalActorUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalActorUI.scrollArea->widget.children[0]->children) + 5;
	emPanelSetActive(wleAEGlobalActorUI.panel, panelActive);

	return WLE_UI_PANEL_OWNED;
}

void wleAEActorCreate(EMPanel *panel)
{
	int i = 1;
	int index;

	if (wleAEGlobalActorUI.autoWidget)
		return;

	wleAEGlobalActorUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalActorUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalActorUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, false, false);
	wleAEGlobalActorUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalActorUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	//wleAEGlobalActorUI.scrollArea->widget.sb->alwaysScrollX = false;
	emPanelAddChild(panel, wleAEGlobalActorUI.scrollArea, false);

	// set parameter settings
	// Name

	// Name
	wleAEGlobalActorUI.data.actorName.update_func = wleAEActorNameUpdate;
	wleAEGlobalActorUI.data.actorName.apply_func = wleAEActorNameApply;
	wleAEGlobalActorUI.data.actorName.entry_align = 75;
	wleAEGlobalActorUI.data.actorName.entry_width = 1;

	// FSM
	wleAEGlobalActorUI.data.overrideFSM.update_func = wleAEActorOverrideFSMUpdate;
	wleAEGlobalActorUI.data.overrideFSM.apply_func = wleAEActorOverrideFSMApply;
	wleAEGlobalActorUI.data.overrideFSM.entry_align = 1;
	wleAEGlobalActorUI.data.overrideFSM.left_pad = 1;

	wleAEGlobalActorUI.data.actorFSM.update_func = wleAEActorFSMUpdate;
	wleAEGlobalActorUI.data.actorFSM.apply_func = wleAEActorFSMApply;
	wleAEGlobalActorUI.data.actorFSM.entry_align = 75;
	wleAEGlobalActorUI.data.actorFSM.entry_width = 250;
	wleAEGlobalActorUI.data.actorFSM.dictionary = "FSM";
	wleAEGlobalActorUI.data.actorFSM.can_unspecify = false;

	// Display Name
	wleAEGlobalActorUI.data.overrideDisplayName.update_func = wleAEActorOverrideDisplayNameUpdate;
	wleAEGlobalActorUI.data.overrideDisplayName.apply_func = wleAEActorOverrideDisplayNameApply;
	wleAEGlobalActorUI.data.overrideDisplayName.entry_align = 1;
	wleAEGlobalActorUI.data.overrideDisplayName.left_pad = 1;

	wleAEGlobalActorUI.data.actorDisplayName.update_func = wleAEActorDisplayNameUpdate;
	wleAEGlobalActorUI.data.actorDisplayName.apply_func = wleAEActorDisplayNameApply;
	wleAEGlobalActorUI.data.actorDisplayName.entry_align = 75;
	wleAEGlobalActorUI.data.actorDisplayName.entry_width = 250;
	wleAEGlobalActorUI.data.actorDisplayName.can_unspecify = false;
	wleAEGlobalActorUI.data.actorDisplayName.source_key = "Actor_DisplayName";

	// Display Sub Name
	wleAEGlobalActorUI.data.overrideDisplaySubName.update_func = wleAEActorOverrideDisplaySubNameUpdate;
	wleAEGlobalActorUI.data.overrideDisplaySubName.apply_func = wleAEActorOverrideDisplaySubNameApply;
	wleAEGlobalActorUI.data.overrideDisplaySubName.entry_align = 1;
	wleAEGlobalActorUI.data.overrideDisplaySubName.left_pad = 1;

	wleAEGlobalActorUI.data.actorDisplaySubName.update_func = wleAEActorDisplaySubNameUpdate;
	wleAEGlobalActorUI.data.actorDisplaySubName.apply_func = wleAEActorDisplaySubNameApply;
	wleAEGlobalActorUI.data.actorDisplaySubName.entry_align = 75;
	wleAEGlobalActorUI.data.actorDisplaySubName.entry_width = 250;
	wleAEGlobalActorUI.data.actorDisplaySubName.can_unspecify = false;
	wleAEGlobalActorUI.data.actorDisplaySubName.source_key = "Actor_DisplaySubName";

	// Costume
	wleAEGlobalActorUI.data.overrideCostume.update_func = wleAEActorOverrideCostumeUpdate;
	wleAEGlobalActorUI.data.overrideCostume.apply_func = wleAEActorOverrideCostumeApply;
	wleAEGlobalActorUI.data.overrideCostume.entry_align = 1;
	wleAEGlobalActorUI.data.overrideCostume.left_pad = 1;

	wleAEGlobalActorUI.data.actorCostume.update_func = wleAEActorCostumeUpdate;
	wleAEGlobalActorUI.data.actorCostume.apply_func = wleAEActorCostumeApply;
	wleAEGlobalActorUI.data.actorCostume.entry_align = 75;
	wleAEGlobalActorUI.data.actorCostume.entry_width = 250;
	wleAEGlobalActorUI.data.actorCostume.dictionary = "PlayerCostume";
	wleAEGlobalActorUI.data.actorCostume.can_unspecify = false;

	// Faction
	wleAEGlobalActorUI.data.overrideFaction.update_func = wleAEActorOverrideFactionUpdate;
	wleAEGlobalActorUI.data.overrideFaction.apply_func = wleAEActorOverrideFactionApply;
	wleAEGlobalActorUI.data.overrideFaction.entry_align = 1;
	wleAEGlobalActorUI.data.overrideFaction.left_pad = 1;

	wleAEGlobalActorUI.data.actorFactionDef.update_func = wleAEActorFactionUpdate;
	wleAEGlobalActorUI.data.actorFactionDef.apply_func = wleAEActorFactionApply;
	wleAEGlobalActorUI.data.actorFactionDef.entry_align = 75;
	wleAEGlobalActorUI.data.actorFactionDef.entry_width = 250;
	wleAEGlobalActorUI.data.actorFactionDef.dictionary = "CritterFaction";
	wleAEGlobalActorUI.data.actorFactionDef.can_unspecify = false;

	// Critter Def
	wleAEGlobalActorUI.data.overrideCritterDef.update_func = wleAEActorOverrideCritterDefUpdate;
	wleAEGlobalActorUI.data.overrideCritterDef.apply_func = wleAEActorOverrideCritterDefApply;
	wleAEGlobalActorUI.data.overrideCritterDef.entry_align = 1;
	wleAEGlobalActorUI.data.overrideCritterDef.left_pad = 1;

	wleAEGlobalActorUI.data.actorCritterDef.update_func = wleAEActorCritterDefUpdate;
	wleAEGlobalActorUI.data.actorCritterDef.apply_func = wleAEActorCritterDefApply;
	wleAEGlobalActorUI.data.actorCritterDef.entry_align = 75;
	wleAEGlobalActorUI.data.actorCritterDef.entry_width = 250;
	wleAEGlobalActorUI.data.actorCritterDef.dictionary = "CritterDef";
	wleAEGlobalActorUI.data.actorCritterDef.can_unspecify = false;


	for(index = 0; index < MAX_VARIABLES; index++)
	{
		WleActorFSMVarsUI *new_var = calloc(1, sizeof(WleActorFSMVarsUI));
		eaPush(&wleAEGlobalActorUI.FSMVars, new_var);
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.update_func = wleAEActorVarUpdate;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.apply_func = wleAEActorVarApply;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.tracker_func = wleAEActorGetTracker;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.entry_align = 75;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.entry_width = 1;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.index = index;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.can_unspecify = true;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.source_map_name = zmapInfoGetPublicName(NULL);
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.no_name = false;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.display_if_unspecified = true;
		wleAEGlobalActorUI.FSMVars[index]->actorVarDef.key = NULL;
	}

	for(index = 0; index < MAX_VARIABLES; index++)
	{
		WleActorFSMVarsUI *new_var = calloc(1, sizeof(WleActorFSMVarsUI));
		eaPush(&wleAEGlobalActorUI.InheritedVars, new_var);
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.update_func = wleAEActorVarUpdate;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.apply_func = wleAEActorVarApply;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.tracker_func = wleAEActorGetTracker;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.entry_align = 75;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.entry_width = 1;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.index = index;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.can_unspecify = true;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.source_map_name = zmapInfoGetPublicName(NULL);
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.no_name = false;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.display_if_unspecified = true;
		wleAEGlobalActorUI.InheritedVars[index]->actorVarDef.key = NULL;
	}

	for(index = 0; index < MAX_VARIABLES; index++)
	{
		WleActorFSMVarsUI *new_var = calloc(1, sizeof(WleActorFSMVarsUI));
		eaPush(&wleAEGlobalActorUI.ExtraVars, new_var);
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.update_func = wleAEActorVarUpdate;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.update_data = "Extra";
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.apply_func = wleAEActorVarApply;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.apply_data = "Extra";
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.tracker_func = wleAEActorGetTracker;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.entry_align = 75;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.entry_width = 1;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.index = index;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.can_unspecify = false;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.source_map_name = zmapInfoGetPublicName(NULL);
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.no_name = false;
		wleAEGlobalActorUI.ExtraVars[index]->actorVarDef.display_if_unspecified = false;
	}
}

#endif
