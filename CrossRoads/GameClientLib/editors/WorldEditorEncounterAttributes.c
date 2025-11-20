#ifndef NO_EDITORS

#include "WorldEditorEncounterAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorInteractionProp.h"
#include "WorldEditorOperations.h"
#include "WorldEditorClientMain.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorPrefs.h"
#include "EditorManager.h"
#include "groupdbmodify.h"
#include "encounter_common.h"
#include "EncounterTemplateEditor.h"
#include "entCritter.h"
#include "Entity.h"
#include "Expression.h"
#include "interaction_common.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "ChoiceTable_common.h"
#include "MultiEditFieldContext.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct wleAEEncounterUIData
{
	Vec3 vPos;

	const char **ppTemplateList;
	const char **ppPatrolList;

	GroupTracker *pTracker;
	WorldScope *pClosestScope;
	bool bSameScope;

	int iHasSpawnRules;
	int iHasOverrideRewards;
	int iHasEvent;

	UIRebuildableTree *pEncountersAutoWidget;
	UIRebuildableTree *pActorsAutoWidget;

	struct
	{
		UIWindow *window;
		UITextEntry *name;
		UITextEntry *scope;
		UICheckButton* childButton;
		char **eaScopes;
	} newTemplateWindow;

} wleAEEncounterUIData;
wleAEEncounterUIData g_EncounterUI = {0};

// Preference strings
#define WLE_PREF_EDITOR_NAME "WorldEditor"
#define WLE_PREF_CAT_UI	"UI"

#define wleAEEncounterUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldEncounterProperties *properties;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = trackerFromTrackerHandle(obj->obj);\
	def = tracker ? tracker->def : NULL;\
	properties = SAFE_MEMBER(def, property_structs.encounter_properties)

#define wleAEEncounterApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldEncounterProperties *properties;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(obj->obj);\
	if (!tracker)\
	return;\
	def = tracker ? tracker->def : NULL;\
	properties = SAFE_MEMBER(def, property_structs.encounter_properties);\
	if (!properties)\
	{\
		wleOpPropsEnd();\
		return;\
	}\


/********************
* PARAMETER CALLBACKS
********************/

static void wleAEEncounterCreateTemplateCancel(UIButton *button, void* unused) {
	if(g_EncounterUI.newTemplateWindow.window)
		ui_WindowClose(g_EncounterUI.newTemplateWindow.window);
}


static void wleAEEncounterCreateTemplateOk(UIButton *button, EditorObject *obj) {
	EncounterTemplate *pTemplate = NULL;
	const unsigned char* name = g_EncounterUI.newTemplateWindow.name ? ui_TextEntryGetText(g_EncounterUI.newTemplateWindow.name) : NULL;
	const unsigned char* scope = g_EncounterUI.newTemplateWindow.scope ? ui_TextEntryGetText(g_EncounterUI.newTemplateWindow.scope) : NULL;
	bool bChild = g_EncounterUI.newTemplateWindow.childButton ? ui_CheckButtonGetState(g_EncounterUI.newTemplateWindow.childButton) : false;

	wleAEEncounterApplyInit();

	if(properties && name) {
		REF_TO(EncounterTemplate) hTemplate;
		SET_HANDLE_FROM_STRING("EncounterTemplate", name, hTemplate);
		pTemplate = GET_REF(hTemplate);
		if (!pTemplate) {
			ETNewTemplateData *pData = calloc(1, sizeof(ETNewTemplateData));
			pData->pchName = strdup(name);
			pData->pchScope = strdup(scope);
			pData->pchMapForVars = strdup(zmapInfoGetPublicName(NULL));
			pData->pchOldTemplate = strdup(REF_STRING_FROM_HANDLE(properties->hTemplate));
			pData->bCreateChild = bChild;
			emNewDoc("EncounterTemplate", pData);
			if(pData->pDoc) {
				emSaveDocAs(pData->pDoc);
				SET_HANDLE_FROM_STRING("EncounterTemplate", name, properties->hTemplate);
			}
			free(pData);
			if(g_EncounterUI.newTemplateWindow.window)
				ui_WindowClose(g_EncounterUI.newTemplateWindow.window);
		} else {
			ui_DialogPopup("Template Already Exists", "An Encounter Template with that name already exists.  Please choose a different name.");
		}
		REMOVE_HANDLE(hTemplate);
	}

	wleOpPropsEnd();
}

static void wleAEEncounterCreateTemplateClicked(UIButton *button, EditorObject *obj)
{
	UILabel *nameLabel;
	UILabel *scopeLabel;
	char buf[1024];
	char name[1024];
	char* ptr;
	const char* pchFilename = zmapInfoGetFilename(NULL);
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	EncounterTemplate *pCurrentTemplate = NULL;
	wleAEEncounterUpdateInit();

	// Setup window
	g_EncounterUI.newTemplateWindow.window = ui_WindowCreate("New Template", 0, 0, 350, 120);
	elUICenterWindow(g_EncounterUI.newTemplateWindow.window);

	// Name
	// Automatically create the name
	pCurrentTemplate = properties ? GET_REF(properties->hTemplate) : NULL;
	if(pCurrentTemplate) {
		int iNextIdx = 2;
		sprintf(name, "%s_%s", pchMapName, pCurrentTemplate->pcName);
		strcpy(buf, name);
		while(RefSystem_ReferentFromString("EncounterTemplate", buf))
		{
			sprintf(buf, "%s_%d", name, iNextIdx);
			iNextIdx++;
		}
		strcpy(name, buf);
	}
	// Set the name field
	nameLabel = ui_LabelCreate("Template Name:", 5, 5);
	ui_WindowAddChild(g_EncounterUI.newTemplateWindow.window, nameLabel);
	g_EncounterUI.newTemplateWindow.name = ui_TextEntryCreate(name, 5, 8);
	ui_TextEntrySetEnterCallback(g_EncounterUI.newTemplateWindow.name, wleAEEncounterCreateTemplateOk, obj);
	ui_WidgetSetDimensionsEx((UIWidget*) g_EncounterUI.newTemplateWindow.name, 1, 20, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) g_EncounterUI.newTemplateWindow.name, 10 + nameLabel->widget.width, 5, 0, 0);
	ui_WindowAddChild(g_EncounterUI.newTemplateWindow.window, g_EncounterUI.newTemplateWindow.name);

	// Scope
	scopeLabel = ui_LabelCreate("Template Scope:", 5, 10 + nameLabel->widget.height);
	ui_WindowAddChild(g_EncounterUI.newTemplateWindow.window, scopeLabel);
	if(!g_EncounterUI.newTemplateWindow.eaScopes)
		eaCreate(&g_EncounterUI.newTemplateWindow.eaScopes);
	strcpy(buf, pchFilename);
	ptr = strrchr(buf, '/');
	if(ptr) {
		*ptr = '\0';
	} else {
		assert(0);
	}
	resGetUniqueScopes("EncounterTemplate", &g_EncounterUI.newTemplateWindow.eaScopes);
	g_EncounterUI.newTemplateWindow.scope = ui_TextEntryCreateWithStringCombo(buf, 5, 13 + nameLabel->widget.height, &g_EncounterUI.newTemplateWindow.eaScopes, true, true, false, true);
	ui_WidgetSetDimensionsEx((UIWidget*) g_EncounterUI.newTemplateWindow.scope, 1, 20, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx((UIWidget*) g_EncounterUI.newTemplateWindow.scope, 10 + nameLabel->widget.width, 5, 0, 0);
	ui_WindowAddChild(g_EncounterUI.newTemplateWindow.window, g_EncounterUI.newTemplateWindow.scope);

	if (pCurrentTemplate)
	{
		g_EncounterUI.newTemplateWindow.childButton = ui_CheckButtonCreate(10, 18 + nameLabel->widget.height + scopeLabel->widget.height, "Create child instead of clone", false);
		ui_WidgetSetDimensionsEx((UIWidget*) g_EncounterUI.newTemplateWindow.name, 1, 20, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPaddingEx((UIWidget*) g_EncounterUI.newTemplateWindow.name, 10 + nameLabel->widget.width, 5, 0, 0);
		ui_WindowAddChild(g_EncounterUI.newTemplateWindow.window, g_EncounterUI.newTemplateWindow.childButton);
	}
	// Buttons
	elUIAddCancelOkButtons(g_EncounterUI.newTemplateWindow.window, wleAEEncounterCreateTemplateCancel, NULL, wleAEEncounterCreateTemplateOk, obj);

	// Show Window
	ui_WindowSetResizable(g_EncounterUI.newTemplateWindow.window, true);
	ui_WindowSetModal(g_EncounterUI.newTemplateWindow.window, true);
	ui_WindowShow(g_EncounterUI.newTemplateWindow.window);
	ui_SetFocus(g_EncounterUI.newTemplateWindow.name);
}


static void wleAEEncounterEditTemplateClicked(UIButton *button, EditorObject *obj)
{
	wleAEEncounterUpdateInit();

	if (properties) 
	{
		EncounterTemplate *pTemplate = GET_REF(properties->hTemplate);
		if (pTemplate)
			emOpenFileEx(pTemplate->pcName, "EncounterTemplate");
	}
}

static void wleAEEncounterAddActor(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	TrackerHandle *pTrackerHandle = trackerHandleCreate(pTracker);
	wleTrackerDeselectAll();
	wleEncounterActorPlace(pTrackerHandle);
	trackerHandleDestroy(pTrackerHandle);
}

static void wleAEEncounterAddActorClicked(UIButton *button, UserData unused)
{
	wleAECallFieldChangedCallback(NULL, wleAEEncounterAddActor);
}

static void wleAEEncounterAddMissingActorsInternal(WorldEncounterProperties *pEncounter)
{
	EncounterTemplate *pTemplate;
	EncounterActorProperties** eaActors = NULL;
	int i,j;

	pTemplate = GET_REF(pEncounter->hTemplate);
	if (pTemplate) {
		encounterTemplate_FillActorEarray(pTemplate, &eaActors);
		if(pEncounter->bFillActorsInOrder)
		{
			int iMaxActors = encounterTemplate_GetMaxNumActors(pTemplate);
			char buf[256];

			// Add actors until we reach the max # of actors
			for(i=eaSize(&pEncounter->eaActors); i < iMaxActors; i++)
			{
				WorldActorProperties *pActor = StructCreate(parse_WorldActorProperties);
				sprintf(buf, "Actor_%d", i);
				pActor->pcName = allocAddString(buf);
				pActor->vPos[0] = (5*i);
				pActor->vPos[1] = 0;
				pActor->vPos[2] = 0;
				eaPush(&pEncounter->eaActors, pActor);
			}
		}
		else
		{
			for(i=0; i<eaSize(&eaActors); ++i) {
				// Look to see if actor is defined
				for(j=eaSize(&pEncounter->eaActors)-1; j>=0; --j) {
					if (stricmp(pEncounter->eaActors[j]->pcName, eaActors[i]->pcName) == 0) {
						break;
					}
				}
				// If actor is not defined, create the actor
				if (j < 0) {
					WorldActorProperties *pActor = StructCreate(parse_WorldActorProperties);
					pActor->pcName = eaActors[i]->pcName; // Both are pooled strings
					pActor->vPos[0] = (5*i);
					pActor->vPos[1] = 0;
					pActor->vPos[2] = 0;
					eaPush(&pEncounter->eaActors, pActor);
				}
			}
		}
	}
}

static void wleAEEncounterResetActors(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	// Remove all actors
	eaDestroyStruct(&pNewProps->encounter_properties->eaActors, parse_WorldActorProperties);
	// Re-create the actors
	wleAEEncounterAddMissingActorsInternal(pNewProps->encounter_properties);
}

static void wleAEEncounterResetActorsClicked(UIButton *button, UserData pUnused)
{
	wleAECallFieldChangedCallback(NULL, wleAEEncounterResetActors);
}

static void wleAEEncounterAddMissingActors(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	// Re-create the actors
	wleAEEncounterAddMissingActorsInternal(pNewProps->encounter_properties);
}

static void wleAEEncounterAddMissingActorsClicked(UIButton *button, EditorObject *obj)
{
	wleAECallFieldChangedCallback(NULL, wleAEEncounterAddMissingActors);
}

static void wleAEEncounterRemoveExtraActors(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	int i;
	WorldEncounterProperties *pProp = pNewProps->encounter_properties; 
	EncounterTemplate *pTemplate = GET_REF(pProp->hTemplate);
	if (pTemplate) {
		if(pProp->bFillActorsInOrder)
		{
			int iNumTemplateActors = encounterTemplate_GetMaxNumActors(pTemplate);
			// Remove actors until we have the maximum number of actors for any team size for this encounter
			while(eaSize(&pProp->eaActors) > iNumTemplateActors)
			{
				eaPop(&pProp->eaActors);
			}
		}
		else
		{
			// Remove all actors that have no matching name
			for(i=eaSize(&pProp->eaActors)-1; i>=0; --i) {
				if (!encounterTemplate_GetActorByName(pTemplate, pProp->eaActors[i]->pcName)) {
					StructDestroy(parse_WorldActorProperties, pProp->eaActors[i]);
					eaRemove(&pProp->eaActors, i);
				}
			}
		}
	} else {
		// Remove all actors since no template
		eaDestroyStruct(&pProp->eaActors, parse_WorldActorProperties);
	}
}

static void wleAEEncounterRemoveExtraActorsClicked(UIButton *button, EditorObject *obj)
{
	wleAECallFieldChangedCallback(NULL, wleAEEncounterRemoveExtraActors);
}

static void wleAEEncounterShowEncounter(UIRTNode *pRoot, EncounterTemplate *pTemplate)
{
	UIAutoWidgetParams firstParams = {0};
	UIAutoWidgetParams secondParams = {0};
	UIAutoWidgetParams thirdParams = {0};
	char buf[1024];
	char key[1024];
	int i,j;
	EncounterLevelProperties *pLevelProps = encounterTemplate_GetLevelProperties(pTemplate);
	EncounterAIProperties *pAIProps = encounterTemplate_GetAIProperties(pTemplate);
	EncounterSpawnProperties *pSpawnProps = encounterTemplate_GetSpawnProperties(pTemplate);
	EncounterSharedCritterGroupSource eTemplateCritterGroupSource = encounterTemplate_GetCritterGroupSource(pTemplate);
	CritterGroup* pTemplateCritterGroup = encounterTemplate_GetCritterGroup(pTemplate, PARTITION_CLIENT);
	EncounterCritterOverrideType eTemplateFactionSource = encounterTemplate_GetFactionSource(pTemplate);
	AIJobDesc** eaJobs = NULL;
	
	encounterTemplate_FillAIJobEArray(pTemplate, &eaJobs);

	firstParams.alignTo = 20;
	secondParams.alignTo = 140;
	thirdParams.alignTo = 40;

	// Level Properties
	if (pLevelProps)
	{

		ui_RebuildableTreeAddLabel(pRoot, "Level:", &firstParams, true);
		switch(pLevelProps->eLevelType)
		{
			xcase EncounterLevelType_Specified:
				if (pLevelProps->iSpecifiedMin == pLevelProps->iSpecifiedMax) {
					sprintf(buf, "(Specified) %d", pLevelProps->iSpecifiedMin);
				} else {
					sprintf(buf, "(Specified) %d to %d", pLevelProps->iSpecifiedMin, pLevelProps->iSpecifiedMax);
				}
			xcase EncounterLevelType_MapLevel:
				sprintf(buf, "(Map Level) %d", zmapInfoGetMapLevel(NULL));
			xcase EncounterLevelType_PlayerLevel:
				sprintf(buf, "(Player Level) Varies");
			xcase EncounterLevelType_MapVariable:
				sprintf(buf, "(Map Variable) %s", pLevelProps->pcMapVariable);
			xdefault:
				sprintf(buf, "(Value not understood by the editor)");
		}
		if (pLevelProps->eLevelType != EncounterLevelType_Specified) {
			bool bHasMin = false;

			if ((pLevelProps->iLevelOffsetMin != 0) || (pLevelProps->iLevelOffsetMax != 0)) {
				strcatf(buf, " offset %d to %d", pLevelProps->iLevelOffsetMin, pLevelProps->iLevelOffsetMax);
			}

			switch (pLevelProps->eClampType) 
			{
				xcase EncounterLevelClampType_Specified:
					if (pLevelProps->iClampSpecifiedMin > 0) {
						strcatf(buf, " [min=%d", pLevelProps->iClampSpecifiedMin);
						bHasMin = true;
					}
					if (pLevelProps->iClampSpecifiedMax > 0) {
						if (bHasMin) {
							strcatf(buf, ", max=%d]", pLevelProps->iClampSpecifiedMax);
						} else {
							strcatf(buf, " [max=%d]", pLevelProps->iClampSpecifiedMax);
						}
					} else if (bHasMin) {
						strcat(buf, "]");
					}
				xcase EncounterLevelClampType_MapLevel:
					{
						int iLevel = zmapInfoGetMapLevel(NULL);
						strcatf(buf, " [(Map Level) min=%d, max=%d]", iLevel + pLevelProps->iClampOffsetMin, iLevel + pLevelProps->iClampOffsetMax);
					}
				xcase EncounterLevelClampType_MapVariable:
					strcatf(buf, " [(Map Variable %s) offset=%d to %d]", pLevelProps->pcClampMapVariable, pLevelProps->iClampOffsetMin, pLevelProps->iClampOffsetMax);
				xdefault:
					strcatf(buf, " (Clamp value not understood by the editor)");
			}
		}
		ui_RebuildableTreeAddLabel(pRoot, buf, &secondParams, false);
	}
	else
	{
		ui_RebuildableTreeAddLabel(pRoot, "Level:", &firstParams, true);
		sprintf(buf, "(Map Level) %d", zmapInfoGetMapLevel(NULL));
		ui_RebuildableTreeAddLabel(pRoot, buf, &secondParams, false);
	}

	// Actor Shared Properties
	ui_RebuildableTreeAddLabel(pRoot, "Critter Group:", &firstParams, true);
	if (eTemplateCritterGroupSource == EncounterSharedCritterGroupSource_Specified) {
		if (pTemplateCritterGroup) {
			sprintf(buf, "(Specified) %s", pTemplateCritterGroup->pchName);
		} else {
			sprintf(buf, "(varies by actor)");
		}
	} else if (eTemplateCritterGroupSource == EncounterSharedCritterGroupSource_MapVariable) {
		const char* pchMapVar = encounterTemplate_GetCritterGroupMapVarName(pTemplate);
		if(pchMapVar) {
			sprintf(buf, "(Map Variable) %s", pchMapVar);
		} else {
			sprintf(buf, "(Map Variable) (null)");
		}
	} else {
		sprintf(buf, "(Value not understood by the editor)");
	}
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, "CritterGroupValue", &secondParams, false);

	ui_RebuildableTreeAddLabel(pRoot, "Faction:", &firstParams, true);
	if (eTemplateFactionSource == EncounterCritterOverrideType_Specified) {
		CritterFaction *pFaction = encounterTemplate_GetFaction(pTemplate);
		if (pFaction) {
			sprintf(buf, "(Specified) %s", pFaction->pchName);
		} else {
			sprintf(buf, "(varies by actor)");
		}
	} else if (eTemplateFactionSource == EncounterCritterOverrideType_FromCritter) {
		sprintf(buf, "(varies by actor)");
	} else {
		sprintf(buf, "(Value not understood by the editor)");
	}
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, "CritterFactionValue", &secondParams, false);

	// AI Properties
	if (pAIProps)
	{
		WorldVariable **eaVars = NULL;
		WorldVariableDef **eaVarDefs = NULL;
		int iNumTemplateVars, iNumGroupVars;
		FSM *pFSM = encounterTemplate_GetEncounterFSM(pTemplate);

		ui_RebuildableTreeAddLabel(pRoot, "FSM:", &firstParams, true);
		if (pAIProps->eFSMType == EncounterCritterOverrideType_Specified) {
			if (REF_STRING_FROM_HANDLE(pAIProps->hFSM)) {
				sprintf(buf, "(Specified) %s", REF_STRING_FROM_HANDLE(pAIProps->hFSM));
			} else {
				sprintf(buf, "(varies by actor)");
			}
		} else if (pAIProps->eFSMType == EncounterCritterOverrideType_FromCritter) {
			sprintf(buf, "(varies by actor)");
		} else {
			sprintf(buf, "(Value not understood by the editor)");
		}
		ui_RebuildableTreeAddLabelKeyed(pRoot, buf, "FSMValue", &secondParams, false);

		ui_RebuildableTreeAddLabel(pRoot, "FSM Vars:", &firstParams, true);

		encounterTemplate_GetEncounterFSMVarDefs(pTemplate, &eaVarDefs, &eaVars);
		iNumTemplateVars = eaSize(&eaVarDefs);
		encounterTemplate_GetEncounterGroupFSMVars(pTemplate, PARTITION_CLIENT, &eaVarDefs, &eaVars);
		iNumGroupVars = eaSize(&eaVars);

		for(i=0; i<eaSize(&eaVarDefs); ++i) {
			WorldVariableDef *pVarDef = eaVarDefs[i];
			char *pcLocation = "";
			char *estrText = NULL;
			if (i < iNumTemplateVars) {
				pcLocation = "Template";
			} 

			sprintf(key, "FSMVarDef%d", i);

			if(pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
				worldVariableToEString(pVarDef->pSpecificValue, &estrText);
				sprintf(buf, "Var: %s (%s) Specified Value = %s [From %s]", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType), estrText, pcLocation);
			} else if(pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE) {
				ChoiceTable* pChoice = GET_REF(pVarDef->choice_table);	
				const char* name = pChoice ? pChoice->pchName : "(null)";
				sprintf(buf, "Var: %s (%s) Choice Table = %s  Choice Name = %s [From %s]", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType), name, pVarDef->choice_name, pcLocation);
			} else if(pVarDef->eDefaultType == WVARDEF_MAP_VARIABLE) {
				sprintf(buf, "Var: %s (%s) Map Variable = %s [From %s]", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType), pVarDef->map_variable, pcLocation);
			}

			ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);
			if(estrText)
				estrDestroy(&estrText);
		}

		for(i=0; i<eaSize(&eaVars); ++i) {
			WorldVariable *pVar = eaVars[i];
			char *estrText = NULL;
			char *pcLocation = "";
			if (i < iNumGroupVars) {
				pcLocation = "Critter Group";
			}
			worldVariableToEString(pVar, &estrText);
			sprintf(key, "FSMVar%d", i);
			sprintf(buf, "Var: %s (%s) = %s [From %s]", pVar->pcName, worldVariableTypeToString(pVar->eType), estrText, pcLocation);
			ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);
			estrDestroy(&estrText);
		}

		if (pFSM) {
			FSMExternVar **eaExternVars = NULL;
			fsmGetExternVarNamesRecursive(pFSM, &eaExternVars, "Encounter");
			for(i=0; i<eaSize(&eaExternVars); ++i) {
				for(j=eaSize(&eaVars)-1; j>=0; --j) {
					if (stricmp(eaVars[j]->pcName, eaExternVars[i]->name) == 0) {
						break;
					}
				}
				if (j < 0) {
					for(j=eaSize(&eaVarDefs)-1; j>=0; --j) {
						if (stricmp(eaVarDefs[j]->pcName, eaExternVars[i]->name) == 0) {
							break;
						}
					}
					if (j < 0) {
						// Var not set so note that!
						sprintf(key, "FSMVar%d", i+eaSize(&eaVars));
						sprintf(buf, "Var: %s = NOT SET", eaExternVars[i]->name);
						ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);
					}
				}
			}
		}
		eaDestroy(&eaVars);
	}
	else
	{
		ui_RebuildableTreeAddLabel(pRoot, "FSM:", &firstParams, true);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "(varies by actor)", "FSMValue", &secondParams, false);

		ui_RebuildableTreeAddLabel(pRoot, "FSM Vars:", &firstParams, true);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "(varies by actor)", "FSMVarValue", &secondParams, false);
	}

	// Spawn properties
	if (pSpawnProps)
	{

		ui_RebuildableTreeAddLabel(pRoot, "Spawn Animation:", &firstParams, true);
		if (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_Specified) {
			sprintf(buf, "(Specified) %s", pSpawnProps->pcSpawnAnim);
		} else if (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_FromCritter) {
			sprintf(buf, "(From Critter) Varies");
		} else if (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_FromCritterAlternate){
			sprintf(buf, "(From Critter)(Alternate Anim) Varies");
		} else {
			sprintf(buf, "(Value not understood by the editor)");
		}
		ui_RebuildableTreeAddLabelKeyed(pRoot, buf, "SpawnAnimValue", &secondParams, false);

		ui_RebuildableTreeAddLabel(pRoot, "Is Ambush:", &firstParams, true);
		if (pSpawnProps->bIsAmbush) {
			ui_RebuildableTreeAddLabelKeyed(pRoot, "true", "IsAmbushValue", &secondParams, false);
		} else {
			ui_RebuildableTreeAddLabelKeyed(pRoot, "false", "IsAmbushValue", &secondParams, false);
		}
	}
	else
	{
		ui_RebuildableTreeAddLabel(pRoot, "Spawn Animation:", &firstParams, true);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "(varies by actor)", "SpawnAnimValue", &secondParams, false);

		ui_RebuildableTreeAddLabel(pRoot, "Is Ambush:", &firstParams, true);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "false", "IsAmbushValue", &secondParams, false);
	}

	// Job Properties
	if (eaSize(&eaJobs))
	{
		// SDANGELO TODO: Fill this in
	}

	// Wave Properties
	if (encounterTemplate_GetWaveProperties(pTemplate))
	{
		// SDANGELO TODO: Fill this in
	}

	eaDestroy(&eaJobs);
}


void wleAEEncounterShowActor(UIRTNode *pRoot, EncounterTemplate *pTemplate, EncounterActorProperties *pActor, WorldActorProperties *pWActor, int i, int iAlign)
{
	UIAutoWidgetParams firstParams = {0};
	UIAutoWidgetParams secondParams = {0};
	UIAutoWidgetParams thirdParams = {0};
	char buf[1024];
	char key[1024];
	int j,k;
	char *estrText = NULL;
	int iSpawnSizeIndex = 0;
	int iBossSizeIndex = 0;
	CritterDef *pCritterDef = pWActor && pWActor->pCritterProperties? GET_REF(pWActor->pCritterProperties->hCritterDef) : NULL;
	CritterGroup *pCritterGroup = NULL;
	WorldVariable **eaVars = NULL;
	WorldVariableDef **eaVarDefs = NULL;
	int iNumWActorVars, iNumActorVars, iNumTemplateVars, iNumCritterVars, iNumGroupVars;
	FSM *pFSM;
	EncounterSpawnProperties *pSpawnProps = encounterTemplate_GetSpawnProperties(pTemplate);
	bool bMultipledifficulties = (encounter_GetEncounterDifficultiesCount() > 1);
	int iPartitionIdx = PARTITION_CLIENT;
	WorldInteractionPropertyEntry** eaInteractionProps = NULL;

	if(!pCritterDef)
	{
		pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
	}

	if(pCritterDef)
	{
		pCritterGroup = GET_REF(pCritterDef->hGroup);
	}

	firstParams.alignTo = iAlign+20;
	secondParams.alignTo = iAlign+140;
	thirdParams.alignTo = iAlign+40;

	assert(ParserFindColumn(parse_EncounterActorSpawnProperties, "SpawnAtTeamSize", &iSpawnSizeIndex));

	// Comments
	if(pActor && pActor->nameProps.pchComments)
	{
		sprintf(key, "Comments%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Comments:", key, &firstParams, true);
		sprintf(key, "CommentsValue%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, pActor->nameProps.pchComments, key, &secondParams, false);
	}
	

	// Critter Type
	sprintf(key, "CritterType%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Critter Type:", key, &firstParams, true);
	if(pWActor && pWActor->pCritterProperties)
	{
		sprintf(buf, "(World Actor Critter Def) %s", REF_STRING_FROM_HANDLE(pWActor->pCritterProperties->hCritterDef));
	}
	else
	{
		switch(pActor->critterProps.eCritterType)
		{
			xcase ActorCritterType_CritterGroup:
				sprintf(buf, "(Critter Group) Group= %s", REF_STRING_FROM_HANDLE(pActor->critterProps.hCritterGroup));
			xcase ActorCritterType_FromTemplate:
			{
				CritterGroup* pTemplateCritterGroup = encounterTemplate_GetCritterGroup(pTemplate, iPartitionIdx);
				EncounterSharedCritterGroupSource eCritterGroupSource = encounterTemplate_GetCritterGroupSource(pTemplate);
				const char* pchMapVar = encounterTemplate_GetCritterGroupMapVarName(pTemplate);
				if (eCritterGroupSource == EncounterSharedCritterGroupSource_Specified && pTemplateCritterGroup) {
					sprintf(buf, "(From Template) Group=%s", pTemplateCritterGroup->pchName);
				} else if (eCritterGroupSource == EncounterSharedCritterGroupSource_MapVariable && pchMapVar) {
					sprintf(buf, "(From Template) Group=(Map Variable) %s", pchMapVar);
				} else {
					sprintf(buf, "(From Template) <Value Not Defined!>");
				}
			}
			xcase ActorCritterType_MapVariableGroup:
				sprintf(buf, "(Map Variable) Group=(Map Variable) %s", pActor->critterProps.pcCritterMapVariable);
			xcase ActorCritterType_CritterDef:
				sprintf(buf, "(Critter Def) Def= %s", REF_STRING_FROM_HANDLE(pActor->critterProps.hCritterDef));
			xcase ActorCritterType_MapVariableDef:
				sprintf(buf, "(Map Variable) Def=(Map Variable) %s", pActor->critterProps.pcCritterMapVariable);
			xcase ActorCritterType_PetContactList:
				sprintf(buf, "(Pet Contact List) Def= %s", REF_STRING_FROM_HANDLE(pActor->miscProps.hPetContactList));
			xdefault:
				sprintf(buf, "(Value not understood by the editor)");
		}
	}
	sprintf(key, "CritterTypeValue%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);

	// Critter Rank
	if ((pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) ||
		(pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) ||
		(pActor->critterProps.eCritterType == ActorCritterType_MapVariableGroup)) {
		sprintf(key, "ActorRank%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Rank / SubRank:", key, &firstParams, true);
		sprintf(buf, "%s / %s", pActor->critterProps.pcRank, pActor->critterProps.pcSubRank);
		sprintf(key, "ActorRankValue%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);
	}

	// Critter Faction
	sprintf(key, "ActorFaction%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Faction:", key, &firstParams, true);
	if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified) {
		sprintf(buf, "(Specified) %s", REF_STRING_FROM_HANDLE(pActor->factionProps.hFaction));
	} else if (pActor->factionProps.eFactionType == EncounterTemplateOverrideType_FromTemplate) {
		if (encounterTemplate_GetFactionSource(pTemplate) == EncounterCritterOverrideType_Specified) {
			CritterFaction* pTemplateFaction = encounterTemplate_GetFaction(pTemplate);
			sprintf(buf, "(From Template) %s", pTemplateFaction?pTemplateFaction->pchName:"(null)");
		} else if (pCritterDef) {
			sprintf(buf, "(From Critter) %s", REF_STRING_FROM_HANDLE(pCritterDef->hFaction));
		} else {
			sprintf(buf, "(From Critter) Varies by critter chosen");
		}
	} else {
		sprintf(buf, "(Value not understood by the editor)");
	}
	sprintf(key, "ActorFactionValue%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);

	// Display Name
	sprintf(key, "ActorDispName%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Display Name:", key, &firstParams, true);
	if (pWActor && pWActor->displayNameMsg.pEditorCopy) {
		sprintf(buf, "(From World Actor) %s", TranslateMessagePtr(pWActor->displayNameMsg.pEditorCopy));
	} else if (pWActor && GET_REF(pWActor->displayNameMsg.hMessage)) {
		sprintf(buf, "(From World Actor) %s", TranslateDisplayMessage(pWActor->displayNameMsg));
	} else if (pActor->nameProps.eDisplayNameType == EncounterCritterOverrideType_Specified) {
		sprintf(buf, "(Specified) %s", TranslateDisplayMessage(pActor->nameProps.displayNameMsg));
	} else if (pActor->nameProps.eDisplayNameType == EncounterCritterOverrideType_FromCritter) {
		if (pCritterDef) {
			sprintf(buf, "(From Critter) %s", TranslateDisplayMessage(pCritterDef->displayNameMsg));
		} else {
			sprintf(buf, "(From Critter) Varies by critter chosen");
		}
	} else {
		sprintf(buf, "(Value not understood by the editor)");
	}
	sprintf(key, "ActorDispNameValue%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);

	// Display SubName
	sprintf(key, "ActorDispSubName%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Display SubName:", key, &firstParams, true);
	if (pWActor && pWActor->displaySubNameMsg.pEditorCopy) {
		sprintf(buf, "(From World Actor) %s", TranslateMessagePtr(pWActor->displaySubNameMsg.pEditorCopy));
	} else if (pWActor && GET_REF(pWActor->displaySubNameMsg.hMessage)) {
		sprintf(buf, "(From World Actor) %s", TranslateDisplayMessage(pWActor->displaySubNameMsg));
	} else if (pActor->nameProps.eDisplaySubNameType == EncounterCritterOverrideType_Specified) {
		sprintf(buf, "(Specified) %s", TranslateDisplayMessage(pActor->nameProps.displaySubNameMsg));
	} else if (pActor->nameProps.eDisplaySubNameType == EncounterCritterOverrideType_FromCritter) {
		if (pCritterDef) {
			sprintf(buf, "(From Critter) %s", TranslateDisplayMessage(pCritterDef->displaySubNameMsg));
		} else {
			sprintf(buf, "(From Critter) Varies by critter chosen");
		}
	} else {
		sprintf(buf, "(Value not understood by the editor)");
	}
	sprintf(key, "ActorDispSubNameValue%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);

	if(bMultipledifficulties)
	{
		// Spawn at Size
		sprintf(key, "ActorSpawnSizeLabel1%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Spawn at:", key, &firstParams, true);
		sprintf(key, "ActorSpawnSizeLabel2%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Difficulty", key, &thirdParams, true);
		sprintf(key, "ActorSpawnSizeLabel3%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Team Size", key, &secondParams, false);
		for(j = 0; j < eaSize(&pActor->eaSpawnProperties); j++)
		{
			sprintf(buf, "%s", StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, pActor->eaSpawnProperties[j]->eSpawnAtDifficulty));
			sprintf(key, "ActorSpawnSizeDifficultyValue%d_%d", i,j);
			ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);

			FieldWriteText(parse_EncounterActorSpawnProperties, iSpawnSizeIndex, pActor->eaSpawnProperties[j], 0, &estrText, 0);
			sprintf(key, "ActorSpawnSizeValue%d_%d", i,j);
			ui_RebuildableTreeAddLabelKeyed(pRoot, estrText, key, &secondParams, false);
			estrClear(&estrText);
		}
		estrDestroy(&estrText);

		// Boss at Size
		sprintf(key, "ActorBossSizeLabel1%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Boss at:", key, &firstParams, true);
		sprintf(key, "ActorBossSizeLabel2%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Difficulty", key, &thirdParams, true);
		sprintf(key, "ActorBossSizeLabel3%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Team Size", key, &secondParams, false);
		for(j = 0; j < eaSize(&pActor->eaBossSpawnProperties); j++)
		{
			sprintf(buf, "%s", StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, pActor->eaBossSpawnProperties[j]->eSpawnAtDifficulty));
			sprintf(key, "ActorBossSizeDifficultyValue%d_%d", i,j);
			ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);

			FieldWriteText(parse_EncounterActorSpawnProperties, iSpawnSizeIndex, pActor->eaBossSpawnProperties[j], 0, &estrText, 0);
			sprintf(key, "ActorBossSizeValue%d_%d", i,j);
			ui_RebuildableTreeAddLabelKeyed(pRoot, estrText, key, &secondParams, false);
			estrClear(&estrText);
		}
		estrDestroy(&estrText);
	}
	else
	{
		// Spawn at Size
		sprintf(key, "ActorSpawnSizeLabel1%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Spawn at Tm Size:", key, &firstParams, true);
		if(eaSize(&pActor->eaSpawnProperties) > 0)
		{
			FieldWriteText(parse_EncounterActorSpawnProperties, iSpawnSizeIndex, pActor->eaSpawnProperties[0], 0, &estrText, 0);
			sprintf(key, "ActorSpawnSizeValue%d", i);
			ui_RebuildableTreeAddLabelKeyed(pRoot, estrText, key, &secondParams, false);
		}
		estrDestroy(&estrText);

		// Boss at Size
		sprintf(key, "ActorBossSizeLabel1%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Boss at Tm Size:", key, &firstParams, true);
		if(eaSize(&pActor->eaBossSpawnProperties) > 0)
		{
			FieldWriteText(parse_EncounterActorSpawnProperties, iSpawnSizeIndex, pActor->eaBossSpawnProperties[0], 0, &estrText, 0);
			sprintf(key, "ActorBossSizeDifficultyValue%d", i);
			ui_RebuildableTreeAddLabelKeyed(pRoot, estrText, key, &secondParams, false);
		}
		estrDestroy(&estrText);
	}

	// Spawn Anim
	sprintf(key, "ActorSpawnAnim%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Spawn Animation:", key, &firstParams, true);
	if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified) {
		sprintf(buf, "(Specified) %s", pActor->spawnInfoProps.pcSpawnAnim);
	} else if (pActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_FromTemplate) {
		if (pSpawnProps && (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_Specified)) {
			EncounterSpawnProperties *pTemplateSpawnProperties = encounterTemplate_GetSpawnProperties(pTemplate);
			if(pTemplateSpawnProperties)
				sprintf(buf, "(From Template) %s", pTemplateSpawnProperties->pcSpawnAnim);
		} else if (pSpawnProps && pCritterDef && pCritterDef->pchSpawnAnim && (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_FromCritter)) {
			sprintf(buf, "(From Critter) %s", pCritterDef->pchSpawnAnim);
		} else if (pSpawnProps && pCritterGroup && pCritterGroup->pchSpawnAnim && (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_FromCritter)) {
			sprintf(buf, "(From CritterGroup) %s", pCritterGroup->pchSpawnAnim);
		} else if (pSpawnProps && pCritterDef && pCritterDef->pchSpawnAnimAlternate && (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_FromCritterAlternate)) {
			sprintf(buf, "(From Critter) (Alternate Anim) %s", pCritterDef->pchSpawnAnimAlternate);
		} else if (pSpawnProps && pCritterGroup && (pSpawnProps->eSpawnAnimType == EncounterSpawnAnimType_FromCritterAlternate)) {
			sprintf(buf, "(From CritterGroup) (Alternate Anim) %s", pCritterGroup->pchSpawnAnimAlternate);
		} else {
			sprintf(buf, "(From Critter) Varies by critter chosen");
		}
	} else {
		sprintf(buf, "(Value not understood by the editor)");
	}
	sprintf(key, "ActorSpawnAnimValue%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);

	// Is Non Combat
	sprintf(key, "ActorNonCombat%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Is Non Combat:", key, &firstParams, true);
	sprintf(key, "ActorNonCombatValue%d", i);
	if (pActor->miscProps.bIsNonCombatant) {
		ui_RebuildableTreeAddLabelKeyed(pRoot, "true", key, &secondParams, false);
	} else {
		ui_RebuildableTreeAddLabelKeyed(pRoot, "false", key, &secondParams, false);
	}

	// LevelOffset
	sprintf(key, "ActorLevelOffset%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "Level Offset:", key, &firstParams, true);
	sprintf(key, "ActorLevelOffsetValue%d", i);
	sprintf(buf, "%d", pActor->critterProps.iLevelOffset);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);	

	// FSM
	sprintf(key, "ActorFSM%d", i);
	if(pWActor && pWActor->bOverrideFSM) {
		pFSM = GET_REF(pWActor->hFSMOverride);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "FSM:", key, &firstParams, true);
		sprintf(buf, "(From World Actor) %s", REF_STRING_FROM_HANDLE(pWActor->hFSMOverride));
	} else {
		pFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, iPartitionIdx);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "FSM:", key, &firstParams, true);
		if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified) {
			sprintf(buf, "(From Template Actor) %s", REF_STRING_FROM_HANDLE(pActor->fsmProps.hFSM));
		} else if (pActor->fsmProps.eFSMType == EncounterTemplateOverrideType_FromTemplate) {
			EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pTemplate);
			if (pAIProperties && (pAIProperties->eFSMType == EncounterCritterOverrideType_Specified)) {
				sprintf(buf, "(From Template) %s", REF_STRING_FROM_HANDLE(pAIProperties->hFSM));
			} else if (pCritterDef) {
				sprintf(buf, "(From Critter) %s", REF_STRING_FROM_HANDLE(pCritterDef->hFSM));
			} else {
				sprintf(buf, "(From Critter) Varies by critter chosen");
			}
		} else {
			sprintf(buf, "(Value not understood by the editor)");
		}
	}
	sprintf(key, "ActorFSMValue%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &secondParams, false);

	// FSM Vars
	sprintf(key, "ActorFSMVars%d", i);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "FSM Vars:", key, &firstParams, true);

	// Collect FSM vars and info on where from
	if(pWActor)
		encounter_GetWorldActorFSMVarDefs(pWActor, &eaVarDefs, &eaVars);
	iNumWActorVars = eaSize(&eaVarDefs);
	encounterTemplate_GetActorFSMVarDefs(pTemplate, pActor, &eaVarDefs, &eaVars);
	iNumActorVars = eaSize(&eaVarDefs);
	encounterTemplate_GetEncounterFSMVarDefs(pTemplate, &eaVarDefs, &eaVars);
	iNumTemplateVars = eaSize(&eaVarDefs);
	encounterTemplate_GetActorCritterFSMVars(pTemplate, pActor, iPartitionIdx, &eaVarDefs, &eaVars);
	iNumCritterVars = eaSize(&eaVars);
	encounterTemplate_GetEncounterGroupFSMVars(pTemplate, iPartitionIdx, &eaVarDefs, &eaVars);
	iNumGroupVars = eaSize(&eaVars);

	// List the variable def values
	for(j=0; j<eaSize(&eaVarDefs); ++j) 
	{
		WorldVariableDef *pVarDef = eaVarDefs[j];
		char *pcLocation = "";
		if(j < iNumWActorVars) {
			pcLocation = "World Actor";
		} else if (j < iNumActorVars) {
			pcLocation = "Template Actor";
		} else if (j < iNumTemplateVars) {
			pcLocation = "Template";
		}

		sprintf(key, "Actor%dFSMVarDef%d", i, j);

		if(pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
			if(pVarDef->eType == WVAR_MESSAGE)
			{
				estrPrintf(&estrText, "%s", TranslateDisplayMessage(pVarDef->pSpecificValue->messageVal));
			}
			else
			{
				worldVariableToEString(pVarDef->pSpecificValue, &estrText);
			}
			sprintf(buf, "Var: %s (%s) Specified Value = %s [From %s]", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType), estrText, pcLocation);
		} else if(pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE) {
			ChoiceTable* pChoice = GET_REF(pVarDef->choice_table);		
			const char* name = pChoice ? pChoice->pchName : "(null)";
			sprintf(buf, "Var: %s (%s) Choice Table = %s  Choice Name = %s [From %s]", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType), name, pVarDef->choice_name, pcLocation);
		} else if(pVarDef->eDefaultType == WVARDEF_MAP_VARIABLE) {
			sprintf(buf, "Var: %s (%s) Map Variable = %s [From %s]", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType), pVarDef->map_variable, pcLocation);
		}

		ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);
		if(estrText)
			estrDestroy(&estrText);
	}

	// List the variable values
	for(j=0; j<eaSize(&eaVars); ++j) 
	{
		WorldVariable *pVar = eaVars[j];
		char *pcLocation = "";
		if (j < iNumCritterVars) {
			pcLocation = "Critter";
		} else if (j < iNumGroupVars) {
			pcLocation = "Critter Group";
		}
		worldVariableToEString(pVar, &estrText);
		sprintf(key, "Actor%dFSMVar%d", i, j);
		sprintf(buf, "Var: %s (%s) = %s [From %s]", pVar->pcName, worldVariableTypeToString(pVar->eType), estrText, pcLocation);
		ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);
		estrDestroy(&estrText);
	}

	if (pFSM) {
		FSMExternVar **eaExternVars = NULL;
		fsmGetExternVarNamesRecursive(pFSM, &eaExternVars, "Encounter");
		for(j=0; j<eaSize(&eaExternVars); ++j) {
			for(k=eaSize(&eaVars)-1; k>=0; --k) {
				if (stricmp(eaVars[k]->pcName, eaExternVars[j]->name) == 0) {
					break;
				}
			}
			if (k < 0) {
				for(k=eaSize(&eaVarDefs)-1; k>=0; --k) {
					if (stricmp(eaVarDefs[k]->pcName, eaExternVars[j]->name) == 0) {
						break;
					}
				}
				if (k < 0) {
					// Var not set so note that!
					sprintf(key, "Actor%dFSMVarNot%d", i, j);
					sprintf(buf, "Var: %s = NOT SET", eaExternVars[j]->name);
					ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, &thirdParams, true);
				}
			}
		}
	}
	eaDestroy(&eaVars);

	// Display Interaction Information
	encounterTemplate_FillActorInteractionEarray(pTemplate, pActor, &eaInteractionProps);
	if(pActor->pInteractionProperties) {
		int index;

		for(index = 0; index < eaSize(&pActor->pInteractionProperties->eaEntries); index++)
		{
			sprintf(key, "Actor%d", i);
			wleAEInteractionPropShowEntry(pRoot, pActor->pInteractionProperties->eaEntries[index], index, key, &firstParams);
		}
	}
	eaDestroy(&eaInteractionProps);
}


void wleAEEncounterShowActors(UIRTNode *pRoot, EncounterTemplate *pTemplate, WorldEncounterProperties* pWorldEncounter)
{
	UIAutoWidgetParams zeroParams = {0};
	UIAutoWidgetParams secondParams = {0};
	char key[1024];
	int i;
	EncounterActorProperties** eaActors = NULL;
	encounterTemplate_FillActorEarray(pTemplate, &eaActors);

	zeroParams.alignTo = 20;
	secondParams.alignTo = 160;

	for(i=0; i<eaSize(&eaActors); ++i) {
		EncounterActorProperties *pActor = eaActors[i];
		WorldActorProperties *pWActor = pWorldEncounter && pWorldEncounter->eaActors && i < eaSize(&pWorldEncounter->eaActors) ? pWorldEncounter->eaActors[i] : NULL;

		// Actor name
		sprintf(key, "ActorName%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, "Actor Name:", key, &zeroParams, true);
		sprintf(key, "ActorNameValue%d", i);
		ui_RebuildableTreeAddLabelKeyed(pRoot, pActor->pcName, key, &secondParams, false);

		wleAEEncounterShowActor(pRoot, pTemplate, pActor, pWActor, i, 20);
	}
	eaDestroy(&eaActors);
}

//////////////////////////////////////////////////////////////////////////
// Callbacks

static bool wleAESpawnPointExclude(GroupTracker *pTracker)
{
	GroupDef *pDef = pTracker->def;
	WorldEncounterProperties *pProp = pDef->property_structs.encounter_properties;

	if(!pProp)
		return true;

	if (!g_EncounterUI.pClosestScope)
		g_EncounterUI.pClosestScope = pTracker->closest_scope;
	else if (g_EncounterUI.pClosestScope != pTracker->closest_scope)
		g_EncounterUI.bSameScope = false;

	wleAEWLVALSetFromBool(&g_EncounterUI.iHasSpawnRules, !!pProp->pSpawnProperties);
	wleAEWLVALSetFromBool(&g_EncounterUI.iHasOverrideRewards, !!pProp->pRewardProperties);
	wleAEWLVALSetFromBool(&g_EncounterUI.iHasEvent, !!pProp->pEventProperties);

	if(!g_EncounterUI.pTracker)
		g_EncounterUI.pTracker = pTracker;

	return false;
}

static void wleAEEncounterShowOneOffsToggled(UICheckButton *pCheck, UserData pUnused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowOneOffEncounterTemplates", ui_CheckButtonGetState(pCheck));
	wleAERefresh();
}

static void wleAEEncounterShowEncounterDataToggled(UICheckButton *pCheck, UserData pUnused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowEncounterData", ui_CheckButtonGetState(pCheck));
	wleAERefresh();
}

static void wleAEEncounterShowActorDataToggled(UICheckButton *pCheck, UserData pUnused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowActorData", ui_CheckButtonGetState(pCheck));
	wleAERefresh();
}

static void wleAEEncounterDeleteSpawnProps(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	StructDestroySafe(parse_WorldEncounterSpawnProperties, &pNewProps->encounter_properties->pSpawnProperties);
}

static void wleAEEncounterMakeSpawnProps(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	WorldEncounterSpawnProperties *pSpawnProperties;
	if(pNewProps->encounter_properties->pSpawnProperties)
		return;

	pSpawnProperties = StructCreate(parse_WorldEncounterSpawnProperties);

	// Set to the defaults used if this structure is not present
	if ((zmapInfoGetMapType(NULL) == ZMTYPE_STATIC) || (zmapInfoGetMapType(NULL) == ZMTYPE_SHARED)) {
		pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_Medium;
	} else {
		pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_Never;
	}
	pSpawnProperties->eSpawnRadiusType = WorldEncounterRadiusType_Medium;

	pNewProps->encounter_properties->pSpawnProperties = pSpawnProperties;
}

static void wleAEEncounterSpawnRulesToggled(UICheckButton *pCheck, UserData pUnused)
{
	if(ui_CheckButtonGetState(pCheck)) {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterMakeSpawnProps);
	} else {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterDeleteSpawnProps);
	}
}

static void wleAEEncounterDeleteOverrideRewards(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	StructDestroySafe(parse_WorldEncounterRewardProperties, &pNewProps->encounter_properties->pRewardProperties);
}

static void wleAEEncounterMakeOverrideRewards(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	WorldEncounterRewardProperties *pRewardProperties;
	if(pNewProps->encounter_properties->pRewardProperties)
		return;

	pRewardProperties = StructCreate(parse_WorldEncounterRewardProperties);
	pNewProps->encounter_properties->pRewardProperties = pRewardProperties;
}

static void wleAEEncounterOverrideRewardsToggled(UICheckButton *pCheck, UserData pUnused)
{
	if(ui_CheckButtonGetState(pCheck)) {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterMakeOverrideRewards);
	} else {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterDeleteOverrideRewards);
	}
}

static void wleAEEncounterDeleteEventProps(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	StructDestroySafe(parse_WorldEncounterEventProperties, &pNewProps->encounter_properties->pEventProperties);
}

static void wleAEEncounterMakeEventProps(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(pNewProps->encounter_properties->pEventProperties)
		return;
	pNewProps->encounter_properties->pEventProperties = StructCreate(parse_WorldEncounterEventProperties);
}

static void wleAEEncounterEventRulesToggled(UICheckButton *pCheck, UserData pUnused)
{
	if(ui_CheckButtonGetState(pCheck)) {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterMakeEventProps);
	} else {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterDeleteEventProps);
	}
}

static void wleAEEncounterDeleteWaveProps(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	StructDestroySafe(parse_EncounterWaveProperties, &pNewProps->encounter_properties->pSpawnProperties->pWaveProps);
}

static void wleAEEncounterMakeWaveProps(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(pNewProps->encounter_properties->pSpawnProperties->pWaveProps)
		return;
	pNewProps->encounter_properties->pSpawnProperties->pWaveProps = StructCreate(parse_EncounterWaveProperties);
}

static void wleAEEncounterWavePropsToggled(UICheckButton *pCheck, UserData pUnused)
{
	if(ui_CheckButtonGetState(pCheck)) {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterMakeWaveProps);
	} else {
		wleAECallFieldChangedCallback(NULL, wleAEEncounterDeleteWaveProps);
	}
}

static void wleAEEncounterRadiusCombo(UIComboBox* cb, S32 row, bool inBox, UserData ignored, char** outEstr)
{
	int val = ui_ComboBoxEnumRowToVal(cb, row);
	estrPrintf(outEstr, "%s", StaticDefineIntRevLookup(WorldEncounterRadiusTypeEnum, val));
	if(val != WorldEncounterRadiusType_Custom) {
		F32 fRadius = encounter_GetRadiusValue(val, 0, g_EncounterUI.vPos);
		if (fRadius >= 1000000) {
			estrConcatf(outEstr, " (%s)", "Always Spawns");
		} else {
			estrConcatf(outEstr, " (%g ft)", fRadius);
		}
	}
}

static void wleAEEncounterTimerCombo(UIComboBox* cb, S32 row, bool inBox, UserData ignored, char** outEstr)
{
	int val = ui_ComboBoxEnumRowToVal(cb, row);
	estrPrintf(outEstr, "%s", StaticDefineIntRevLookup(WorldEncounterTimerTypeEnum, val));
	if(val != WorldEncounterTimerType_Custom) {
		F32 fTime = encounter_GetTimerValue(val, 0, g_EncounterUI.vPos);
		if (fTime < 0) {
			estrConcatf(outEstr, " (%s)", "Never Respawns");
		} else {
			estrConcatf(outEstr, " (%g sec)", fTime);
		}
	}
}

static void wleAEEncounterWaveTimerCombo(UIComboBox* cb, S32 row, bool inBox, UserData ignored, char** outEstr)
{
	int val = ui_ComboBoxEnumRowToVal(cb, row);
	estrPrintf(outEstr, "%s", StaticDefineIntRevLookup(WorldEncounterWaveTimerTypeEnum, val));
	if(val != WorldEncounterWaveTimerType_Custom) {
		F32 fTime = encounter_GetWaveTimerValue(val, 0, g_EncounterUI.vPos);
		estrConcatf(outEstr, " (%g sec)", fTime);
	}
}

static void wleAEEncounterWaveDelayCombo(UIComboBox* cb, S32 row, bool inBox, UserData ignored, char** outEstr)
{
	int val = ui_ComboBoxEnumRowToVal(cb, row);
	estrPrintf(outEstr, "%s", StaticDefineIntRevLookup(WorldEncounterWaveDelayTimerTypeEnum, val));
	if(val != WorldEncounterWaveDelayTimerType_Custom) {
		F32 fMinTime, fMaxTime;
		encounter_GetWaveDelayTimerValue(val, 0, 0, g_EncounterUI.vPos, &fMinTime, &fMaxTime);
		if(fMinTime == fMaxTime) {
			estrConcatf(outEstr, " (%g sec)", fMinTime);
		} else { 
			estrConcatf(outEstr, " (%g sec - %g sec)", fMinTime, fMaxTime);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Reload Functions

static void wleAEEncounterSpawnRulesWaveReload(WorldEncounterSpawnProperties **ppParentProps)
{
	int i;
	EncounterWaveProperties **ppProps=NULL;
	EncounterWaveProperties *pProp;
	MEFieldContextEntry *pEntry;

	for ( i=0; i < eaSize(&ppParentProps); i++ ) {
		WorldEncounterSpawnProperties *pParent = ppParentProps[i];
		assert(pParent->pWaveProps);
		eaPush(&ppProps, pParent->pWaveProps);
	}
	assert(eaSize(&ppProps) > 0);
	pProp = ppProps[0];

	MEContextPushEA("Event", ppProps, ppProps, parse_EncounterWaveProperties);

	MEContextAddExpr(exprContextGetGlobalContext(),											"WaveCondition",		"Wave Condition",	"When this is true, the wave will continue.");

	pEntry = MEContextAddEnum(kMEFieldType_Combo, WorldEncounterWaveTimerTypeEnum,			"WaveIntervalType",		"Wave Interval",	"The is the time between waves.");
	ui_ComboBoxSetTextCallback(ENTRY_FIELD(pEntry)->pUICombo, wleAEEncounterWaveTimerCombo, NULL);
	if(!MEContextFieldDiff("WaveIntervalType") && pProp->eWaveIntervalType == WorldEncounterWaveTimerType_Custom) {
		MEContextIndentRight();
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 3600, 1,								"WaveInterval",			"Time",				"This is the time in seconds.");
		MEContextIndentLeft();
	}

	pEntry = MEContextAddEnum(kMEFieldType_Combo, WorldEncounterWaveDelayTimerTypeEnum,		"WaveDelayType",		"Spawn Radius",		"The distance a player must approach within for the encounter to spawn.  Distance is in feet.  'None' means to never trigger on player approach.  'Always' means to always spawn even if no player present.");
	ui_ComboBoxSetTextCallback(ENTRY_FIELD(pEntry)->pUICombo, wleAEEncounterWaveDelayCombo, NULL);
	if(!MEContextFieldDiff("WaveDelayType") && pProp->eWaveDelayType == WorldEncounterWaveDelayTimerType_Custom) {
		MEContextIndentRight();
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 3600, 1,								"WaveDelayMin",			"Time Min",			"This is the min time in seconds.");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 3600, 1,								"WaveDelayMax",			"Time Max",			"This is the max time in seconds.");
		MEContextIndentLeft();
	}

	MEContextPop("Event");
}

static void wleAEEncounterSpawnRulesReload(WorldEncounterProperties **ppParentProps)
{
	int i;
	WorldEncounterSpawnProperties **ppProps=NULL;
	WorldEncounterSpawnProperties *pProp;
	MEFieldContextEntry *pEntry;
	MEFieldContext *pContext;
	int iHasWave = WL_VAL_UNSET;

	for ( i=0; i < eaSize(&ppParentProps); i++ ) {
		WorldEncounterProperties *pParent = ppParentProps[i];
		assert(pParent->pSpawnProperties);
		eaPush(&ppProps, pParent->pSpawnProperties);
		wleAEWLVALSetFromBool(&iHasWave, !!pParent->pSpawnProperties->pWaveProps);
	}
	assert(eaSize(&ppProps) > 0);
	pProp = ppProps[0];

	pContext = MEContextPushEA("SpawnRules", ppProps, ppProps, parse_WorldEncounterSpawnProperties);
	pContext->bDontSortComboEnums = true;

	// hide some mutually exclusive spawn properties to the Mastermind spawn type
	// note: preserving order of spawn rules editor layout
	if (!MEContextFieldDiff("MastermindSpawnType") && pProp->eMastermindSpawnType != WorldEncounterMastermindSpawnType_DynamicOnly) {

		MEContextAddExpr(exprContextGetGlobalContext(),										"SpawnCondition",		"Spawn Condition",			"The encounter will not spawn unless this is true");
		MEContextAddEnum(kMEFieldType_Combo, WorldEncounterSpawnCondTypeEnum,				"SpawnConditionType",	"Spawn Condition Type",		"A Normal condition type is faster than a RequiresPlayer condition type, but the latter allows the expression to use player-specific expression functions.");
	
		pEntry = MEContextAddEnum(kMEFieldType_Combo, WorldEncounterRadiusTypeEnum,			"SpawnRadiusType",		"Spawn Radius Type",		"The distance a player must approach within for the encounter to spawn.  Distance is in feet.  'None' means to never trigger on player approach.  'Always' means to always spawn even if no player present.");
		ui_ComboBoxSetTextCallback(ENTRY_FIELD(pEntry)->pUICombo, wleAEEncounterRadiusCombo, NULL);
		if(!MEContextFieldDiff("SpawnRadiusType") && pProp->eSpawnRadiusType == WorldEncounterRadiusType_Custom) {
			MEContextIndentRight();
			MEContextAddMinMax(kMEFieldType_SliderText, 0, 5000, 10,						"SpawnRadius",			"Custom Radius",			"The custom value for the spawn radius");
			MEContextIndentLeft();
		}

		pEntry = MEContextAddEnum(kMEFieldType_Combo, WorldEncounterTimerTypeEnum,			"RespawnTimerType",		"Respawn Time",				"The time before the encounter will respawn.  The time is in seconds.  'Never' means never respawn.  'None' means respawn immediately.");
		ui_ComboBoxSetTextCallback(ENTRY_FIELD(pEntry)->pUICombo, wleAEEncounterTimerCombo, NULL);
		if(!MEContextFieldDiff("RespawnTimerType") && pProp->eRespawnTimerType == WorldEncounterTimerType_Custom) {
			MEContextIndentRight();
			MEContextAddMinMax(kMEFieldType_SliderText, 0, 250000, 1,						"RespawnTimer",			"Custom Time",				"The custom value for the respawn timer");
			MEContextIndentLeft();
		}

		MEContextAddMinMax(kMEFieldType_SliderText, 0, 3600, 1,								"SpawnDelay",			"Spawn Delay",				"Time in seconds (give or take a second due to the fact we only update every so often) to wait after it is valid to spawn, to actually spawn.");

		MEContextAddExpr(exprContextGetGlobalContext(),										"DespawnCondition",		"Despawn Condition",		"The encounter will despawn forcibly if this is true");

		MEContextAddSpacer();
		MEContextAddCheck(wleAEEncounterWavePropsToggled, NULL, (iHasWave == WL_VAL_TRUE),	"WaveDef",				"Wave Definition",			"If not selected, the template wave properties are used.");
		if(iHasWave == WL_VAL_TRUE) {
			MEContextIndentRight();
			wleAEEncounterSpawnRulesWaveReload(ppProps);
			MEContextIndentLeft();
		}

		MEContextAddSpacer();
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 100, 1,								"SpawnChance",			"Spawn Chance",				"The percentage chance (0 to 100) that the encounter will spawn");
		MEContextAddMinMax(kMEFieldType_SliderText, 0, 10000, 25,							"LockoutRadius",		"Lockout Radius",			"If any encounters are active within this radius, this encounter will not spawn");
		MEContextAddEnum(kMEFieldType_Combo, WorldEncounterDynamicSpawnTypeEnum,			"DynamicSpawnType",		"Dynamic Spawn",			"Settings for dynamically altering spawn time.  Default: static on all maps except static maps.  Dynamic: Always dynamic.  Static: Always static.");
	}

	MEContextAddSpacer();
	MEContextAddSimple(kMEFieldType_Check,													"NoDespawn",			"No Despawn",				"If true, the encounter will never despawn.  Otherwise, it may despawn.");
	MEContextAddSimple(kMEFieldType_Check,													"SnapToGround",			"Snap to Ground",			"If true, the actors will be re-positioned when spawned to be standing on the ground.");
	MEContextAddEnum(kMEFieldType_Combo, WorldEncounterSpawnTeamSizeEnum,					"ForceTeamSize",		"Force Team Size",			"When spawning, this encounter will use this value as the player's team size instead of using the player's actual detected team size.");
	MEContextAddEnum(kMEFieldType_Combo, WorldEncounterMastermindSpawnTypeEnum,				"MastermindSpawnType",	"MM Spawn Type",			"Marks this encounter for the use of the Mastermind spawning system.");
	MEContextAddSimple(kMEFieldType_TextEntry,												"SpawnTag",				"Spawn Tag",				"An identifying tag for this spawn");
	MEContextAddSimple(kMEFieldType_Check,													"DisableAggroDelay",	"Disable Aggro Delay",		"If true, critters in this encounter can aggro immediately upon spawning.");
	if(encounter_GetEncounterDifficultiesCount() > 1) {
		MEContextAddMinMax(kMEFieldType_SliderText, -256, 256, 1,							"DifficultyOffset",		"Enc. Difficulty Offset",	"Offsets the current encounter difficulty by the specified amount before determining if the actors in this encounter should spawn.");	
	}

	MEContextPop("SpawnRules");
}

static void wleAEEncounterOverrideRewardsReload(WorldEncounterProperties **ppParentProps)
{
	int i;
	WorldEncounterRewardProperties **ppProps=NULL;
	WorldEncounterRewardProperties *pProp;
	MEFieldContextEntry *pEntry;
	MEFieldContext *pContext;
	int iHasWave = WL_VAL_UNSET;

	for (i=0; i < eaSize(&ppParentProps); i++) {
		WorldEncounterProperties *pParent = ppParentProps[i];
		assert(pParent->pRewardProperties);
		eaPush(&ppProps, pParent->pRewardProperties);
	}
	assert(eaSize(&ppProps) > 0);
	pProp = ppProps[0];

	pContext = MEContextPushEA("OverrideRewards", ppProps, ppProps, parse_WorldEncounterRewardProperties);
	pContext->bDontSortComboEnums = true;

	pEntry = MEContextAddEnum(kMEFieldType_Combo, WorldEncounterRewardTypeEnum, "RewardType", "Reward Type", "If set, override rewards on the encounter template or critter");
	if(!MEContextFieldDiff("RewardType") && pProp->eRewardType != kWorldEncounterRewardType_DefaultRewards) {
		MEContextAddDict(kMEFieldType_ValidatedTextEntry, "RewardTable", "RewardTable", "Reward Table", "The reward table to use.");
	}
	pEntry = MEContextAddEnum(kMEFieldType_Combo, WorldEncounterRewardLevelTypeEnum, "RewardLevelType", "Reward Level Type", "Decides what level to grant rewards.");
	if(!MEContextFieldDiff("RewardLevelType") && pProp->eRewardLevelType == kWorldEncounterRewardLevelType_SpecificLevel) {
		MEContextAddSimple(kMEFieldType_TextEntry, "RewardLevel", "Reward Level", "Sets a specific reward level");
	}

	MEContextPop("OverrideRewards");
}


static void wleAEEncounterEventReload(WorldEncounterProperties **ppParentProps)
{
	int i;
	WorldEncounterEventProperties **ppProps=NULL;
	WorldEncounterEventProperties *pProp;

	for ( i=0; i < eaSize(&ppParentProps); i++ ) {
		WorldEncounterProperties *pParent = ppParentProps[i];
		assert(pParent->pEventProperties);
		eaPush(&ppProps, pParent->pEventProperties);
	}
	assert(eaSize(&ppProps) > 0);
	pProp = ppProps[0];

	MEContextPushEA("Event", ppProps, ppProps, parse_WorldEncounterEventProperties);
	MEContextAddExpr(exprContextGetGlobalContext(),	"SuccessCondition",		"Success Condition",	"The success event will be generated when this expression is true.  If this is blank, the encounter succeeds when all combatant actors are defeated.");
	MEContextAddExpr(exprContextGetGlobalContext(),	"FailureCondition",		"Failure Condition",	"The failure event will be generated when this expression is true.  If this is blank, the encounter will not fail.");
	MEContextAddExpr(exprContextGetGlobalContext(),	"SuccessAction",		"Success Action",		"This expression will be executed when the encounter succeeds.");
	MEContextAddExpr(exprContextGetGlobalContext(),	"FailureAction",		"Failure Action",		"This expression will be executed when the encounter fails.");
	MEContextPop("Event");
}

static void wleAEEncounterPropChangedCB(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOld, GroupProperties *pNew)
{
	WorldEncounterProperties *pOldProps = pOld->encounter_properties;
	WorldEncounterProperties *pNewProps = pNew->encounter_properties;
	WorldEncounterSpawnProperties *pOldSpawnProps, *pNewSpawnProps; 

	if (pOldProps && pNewProps)
	{
		pOldSpawnProps = pOldProps->pSpawnProperties;
		pNewSpawnProps = pNewProps->pSpawnProperties;

		if(pOldSpawnProps && pNewSpawnProps) {
			if( pOldSpawnProps->eMastermindSpawnType != pNewSpawnProps->eMastermindSpawnType && 
				pNewSpawnProps->eMastermindSpawnType == WorldEncounterMastermindSpawnType_DynamicOnly) {

					pNewSpawnProps->eSpawnRadiusType = WorldEncounterRadiusType_None;
					pNewSpawnProps->fSpawnRadius = 0.f;
					pNewSpawnProps->eRespawnTimerType = WorldEncounterTimerType_None;
					pNewSpawnProps->fRespawnTimer = 0.f;
					pNewSpawnProps->eDyamicSpawnType = WorldEncounterDynamicSpawnType_Default;
					pNewSpawnProps->fSpawnDelay = 0.f;
				pNewSpawnProps->fSpawnChance = 100.f;
				pNewSpawnProps->fLockoutRadius = 0.f;

				// always reset the following values
				if (pNewSpawnProps->pSpawnCond) {
					exprDestroy(pNewSpawnProps->pSpawnCond);
					pNewSpawnProps->pSpawnCond = NULL;
				}
				if (pNewSpawnProps->pDespawnCond) {
					exprDestroy(pNewSpawnProps->pDespawnCond);
					pNewSpawnProps->pDespawnCond = NULL;
				}
				pNewSpawnProps->eSpawnCondType = WorldEncounterSpawnCondType_Normal;
			}
		}
	}

	if(eaSize(&pNewProps->eaActors)) {
		if(pNewProps->bFillActorsInOrder && !pOldProps->bFillActorsInOrder) {
			int i;
			char buf[256];
			// Rename actors
			for(i = 0; i < eaSize(&pNewProps->eaActors); i++) {
				sprintf(buf, "Actor_%d", i);
				pNewProps->eaActors[i]->pcName = allocAddString(buf);
			}
		}
	}

}

int wleAEEncounterReload(EMPanel *panel, EditorObject *edObj)
{
	int i;
	EditorObject **ppObjects = NULL;
	EditorObject *pObj = NULL;
	DictionaryEArrayStruct *pEncounterTemplates = resDictGetEArrayStruct(g_hEncounterTemplateDict);
	WorldEncounterProperties **ppProps = NULL;
	WorldEncounterProperties *pProp;
	MEFieldContext *pContext;
	EncounterTemplate *pTemplate;
	MEFieldContextEntry *pEntry;
	bool bShowOneOffs, bShowEncounters, bShowActors;
	bool bHasSpawnRules, bHasOverrideRewrds, bHasEvent;
	U32 iRetFlags;

	g_EncounterUI.pTracker = NULL;
	g_EncounterUI.pClosestScope = NULL;
	g_EncounterUI.bSameScope = true;
	g_EncounterUI.iHasSpawnRules = WL_VAL_UNSET;
	g_EncounterUI.iHasOverrideRewards = WL_VAL_UNSET;
	g_EncounterUI.iHasEvent = WL_VAL_UNSET;
	ppProps = (WorldEncounterProperties**)wleAEGetSelectedDataFromPath("EncounterDef", wleAESpawnPointExclude, &iRetFlags);
	if(eaSize(&ppProps) == 0 || !g_EncounterUI.bSameScope || (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;

	wleAEGetSelectedObjects(&ppObjects);
	assert(eaSize(&ppObjects) > 0);
	pObj = ppObjects[0];

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_EncounterProperties", ppProps, ppProps, parse_WorldEncounterProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAEEncounterPropChangedCB);
	
	pTemplate = GET_REF(pProp->hTemplate);
	if(pTemplate) {
		Mat4 mLoc;
		trackerGetMat(g_EncounterUI.pTracker, mLoc);
		copyVec3(mLoc[3], g_EncounterUI.vPos);
	} else {
		setVec3same(g_EncounterUI.vPos, 0);
	}

	bShowOneOffs = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowOneOffEncounterTemplates", true);
	bShowEncounters = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowEncounterData", false);
	bShowActors = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ShowActorData", false);

	assert(g_EncounterUI.iHasSpawnRules != WL_VAL_UNSET);
	bHasSpawnRules = (g_EncounterUI.iHasSpawnRules == WL_VAL_TRUE);
	assert(g_EncounterUI.iHasOverrideRewards != WL_VAL_UNSET);
	bHasOverrideRewrds = (g_EncounterUI.iHasOverrideRewards == WL_VAL_TRUE);
	assert(g_EncounterUI.iHasEvent != WL_VAL_UNSET);
	bHasEvent = (g_EncounterUI.iHasEvent == WL_VAL_TRUE);

	// Fill template list
	eaClear(&g_EncounterUI.ppTemplateList);
	for(i=0; i<eaSize(&pEncounterTemplates->ppReferents); ++i) {
		EncounterTemplate *pEncTemplate = pEncounterTemplates->ppReferents[i];
		if(bShowOneOffs || !pEncTemplate->bOneOff)
			eaPush(&g_EncounterUI.ppTemplateList, (char*)pEncTemplate->pcName);
	}

	// Fill patrol list
	eaClear(&g_EncounterUI.ppPatrolList);
	worldGetObjectNames(WL_ENC_PATROL_ROUTE, &g_EncounterUI.ppPatrolList, NULL);

	pEntry = MEContextAddList(kMEFieldType_ValidatedTextEntry, &g_EncounterUI.ppTemplateList,		"Template",				"Encounter Template",		"Encounter Template used for this encounter");
	MEContextEntryAddActionButton(pEntry, "Edit", NULL, wleAEEncounterEditTemplateClicked, pObj, -1,													"Edit this template.");
	if(pTemplate)
		MEContextEntryAddActionButton(pEntry, "Clone/Child", NULL, wleAEEncounterCreateTemplateClicked, pObj, -1,										"Clone or make a child template.");
	else
		MEContextEntryAddActionButton(pEntry, "Create", NULL, wleAEEncounterCreateTemplateClicked, pObj, -1,											"Make a new template.");

	MEContextIndentRight();
	MEContextAddCheck(wleAEEncounterShowOneOffsToggled, NULL, bShowOneOffs,							"ShowOneOffs",			"Show One-Offs",			"If selected, Encounter Templates created using the 'Clone' button will show up in the list of possible Encounter Templates above.");
	MEContextIndentLeft();

	MEContextAddSpacer();
	MEContextAddCheck(wleAEEncounterSpawnRulesToggled, NULL, bHasSpawnRules,						"SpawnRules",			"Spawn Rules",				"If not selected, the default spawn rules are used.");
	if(bHasSpawnRules) {
		MEContextIndentRight();
		wleAEEncounterSpawnRulesReload(ppProps);
		MEContextIndentLeft();
	}

	MEContextAddSpacer();
	MEContextAddCheck(wleAEEncounterOverrideRewardsToggled, NULL, bHasOverrideRewrds,				"OverrideRewards",		"Override Rewards",			"If set, override rewards on the encounter template or critter.");
	if(bHasOverrideRewrds) {
		MEContextIndentRight();
		wleAEEncounterOverrideRewardsReload(ppProps);
		MEContextIndentLeft();
	}

	MEContextAddSpacer();
	MEContextAddSimple(kMEFieldType_TextEntry,														"OverrideSendDistance", "Override Send Distance",	"Override the default send distance for all critters in this encounter.");

	MEContextAddSpacer();
	MEContextAddCheck(wleAEEncounterEventRulesToggled, NULL, bHasEvent,								"Event",				"Events and Actions",		"If not selected, the default success condition is used.");
	if(bHasEvent) {
		MEContextIndentRight();
		wleAEEncounterEventReload(ppProps);
		MEContextIndentLeft();
	}

	MEContextAddSpacer();
	MEContextAddList(kMEFieldType_ValidatedTextEntry, &g_EncounterUI.ppPatrolList,					"PatrolRoute",			"Patrol Route",				"The patrol route the encounter should follow.");
	MEContextAddExpr(exprContextGetGlobalContext(),													"PresenceCondition",	"Presence Condition",		"If this condition is set, actors in this encounter will only be present when it evaluates to true");

	if(eaSize(&ppProps) == 1) {
		int iOrigIndent;
		MEContextAddSpacer();
		MEContextAddSimple(kMEFieldType_Check,														"FillActorsInOrder",	"Fill Actors In Order",		"If checked, actors will be filled based on the order they appear in the Encounter Template instead of by name.");

		MEContextAddLabel(																			"ActorsLabel",			"Actors:",					"Operations to be done on actors");
		iOrigIndent = pContext->iXDataStart;
		pContext->iXDataStart = pContext->iXPos;
		MEContextAddButton("Reset Actors", NULL, wleAEEncounterResetActorsClicked, NULL,			"ResetActors",			NULL,						"Reset actors back to original positions");
		MEContextAddButton("Add Actor", NULL, wleAEEncounterAddActorClicked, NULL,					"AddActor",				NULL,						"Add an actor");
		MEContextAddButton("Add Missing Actors", NULL, wleAEEncounterAddMissingActorsClicked, NULL,	"AddMissingActors",		NULL,						"Add all missing actors");
		MEContextAddButton("Remove Extra Actors", NULL, wleAEEncounterRemoveExtraActorsClicked, NULL,"RemoveExtraActors",	NULL,						"Remove all extra actors");
		pContext->iXDataStart = iOrigIndent;

		if(pTemplate) {
			int iTempY, iTempX;
			MEContextAddSpacer();
			MEContextAddCheck(wleAEEncounterShowEncounterDataToggled, NULL, bShowEncounters,		"ShowEncounter",		"View Encounter Template",	"Shows read-only information on the encounter if selected.");
			if(bShowEncounters) {
				MEFieldContext *pEncounterContext = MEContextPush("EncounterDataText", NULL, NULL, NULL);
				pEntry = MEContextCreateScrollAreaParent(250, "Scroll");
				ui_RebuildableTreeInit(g_EncounterUI.pEncountersAutoWidget, &pEntry->pCustomWidget->children, 0, 0, UIRTOptions_Default);

				wleAEEncounterShowEncounter(g_EncounterUI.pEncountersAutoWidget->root, pTemplate);

				ui_RebuildableTreeDoneBuilding(g_EncounterUI.pEncountersAutoWidget);
				iTempX = elUIGetEndX(pEntry->pCustomWidget->children[0]->children);
				iTempY = elUIGetEndY(pEntry->pCustomWidget->children[0]->children);
				((UIScrollArea*)pEntry->pCustomWidget)->autosize = false;
				pEntry->pCustomWidget->sb->scrollX = true;
				pEntry->pCustomWidget->sb->scrollY = false;
				ui_ScrollAreaSetSize((UIScrollArea*)pEntry->pCustomWidget, iTempX, iTempY);
				ui_WidgetSetHeightEx(pEntry->pCustomWidget, iTempY, UIUnitFixed);
				pEncounterContext->iYPos = iTempY;
				MEContextPop("EncounterDataText");
			}
			MEContextAddSpacer();
			MEContextAddCheck(wleAEEncounterShowActorDataToggled, NULL, bShowActors,				"ShowActor",			"View Actors",				"Shows read-only information on the actors if selected.");
			if(bShowActors) {
				MEFieldContext *pActorContext = MEContextPush("ActorsDataText", NULL, NULL, NULL);
				pEntry = MEContextCreateScrollAreaParent(250, "Scroll");
				ui_RebuildableTreeInit(g_EncounterUI.pActorsAutoWidget, &pEntry->pCustomWidget->children, 0, 0, UIRTOptions_Default);
				
				wleAEEncounterShowActors(g_EncounterUI.pActorsAutoWidget->root, pTemplate, pProp);
				
				ui_RebuildableTreeDoneBuilding(g_EncounterUI.pActorsAutoWidget);
				iTempX = elUIGetEndX(pEntry->pCustomWidget->children[0]->children) + 20;
				iTempY = elUIGetEndY(pEntry->pCustomWidget->children[0]->children) + 20;
				((UIScrollArea*)pEntry->pCustomWidget)->autosize = false;
				pEntry->pCustomWidget->sb->scrollX = true;
				pEntry->pCustomWidget->sb->scrollY = false;
				ui_ScrollAreaSetSize((UIScrollArea*)pEntry->pCustomWidget, iTempX, iTempY);
				ui_WidgetSetHeightEx(pEntry->pCustomWidget, iTempY, UIUnitFixed);
				pActorContext->iYPos = iTempY;
				MEContextPop("ActorsDataText");
			}
		}
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_EncounterProperties");

	return WLE_UI_PANEL_OWNED;	
}

static void wleAEEncounterTemplateDictionaryChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	editState.trackerRefreshRequested = true;
}

void wleAEEncounterCreate(EMPanel *panel)
{
	wleAEGenericCreate(panel);

	resSetDictionaryEditMode(g_hEncounterTemplateDict, true);
	resDictRegisterEventCallback(g_hEncounterTemplateDict, wleAEEncounterTemplateDictionaryChanged, NULL);
	resSetDictionaryEditMode("FSM", true);
	resRequestAllResourcesInDictionary(g_hEncounterTemplateDict);

	g_EncounterUI.pEncountersAutoWidget = ui_RebuildableTreeCreate();
	g_EncounterUI.pActorsAutoWidget = ui_RebuildableTreeCreate();
}

#endif
