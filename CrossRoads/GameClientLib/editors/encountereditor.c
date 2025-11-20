/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "contact_common.h"
#include "editlibgizmos.h"
#include "encounter_common.h"
#include "encountereditor.h"
#include "entcritter.h"
#include "gfxprimitive.h"
#include "graphicslib.h"
#include "inputMouse.h"
#include "oldencounter_common.h"
#include "statemachine.h"
#include "stringcache.h"
#include "worldgrid.h"

#include "AutoGen/oldencounter_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// ----------------------------------------------------------------------------------
// Encounter Editor Utility Functions
// ----------------------------------------------------------------------------------

static char* EDECreateUniqueEncounterName(const char* desiredName)
{
	static char nextName[GE_NAMELENGTH_MAX];
	int counter = 1;
	strcpy(nextName, desiredName);
	while (resGetInfo("EncounterDef", nextName))
	{
		sprintf(nextName, "%s%i", desiredName, counter);
		counter++;
	}
	return nextName;
}

static void EDEActorPropUIRefresh(EncounterEditDoc* encDoc, bool fullRefresh)
{
	OldActor** actorList = NULL;
	EncounterDef* def = encDoc->def;
	ActorPropUI* actorUI = encDoc->uiInfo.actorUI;
	int i, n = eaSize(&encDoc->selectedObjects);

	// Cleanup the actors
	oldencounter_TryCleanupActors(NULL, &def->actors);

	for (i = 0; i < n; i++)
	{
		GESelectedObject* selObject = encDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Actor)
		{
			OldActor* actor = def->actors[selObject->objIndex];
			eaPush(&actorList, actor);
		}
	}

	GEActorPropUIRefresh(encDoc->uiInfo.actorUI, &actorList, fullRefresh);
	if (eaSize(&actorList) && !actorUI->actorPropExpander->group)
		ui_ExpanderGroupInsertExpander(encDoc->uiInfo.propExpGroup, actorUI->actorPropExpander, 0);
	else if (!eaSize(&actorList) && actorUI->actorPropExpander->group)
		ui_ExpanderGroupRemoveExpander(encDoc->uiInfo.propExpGroup, actorUI->actorPropExpander);
	eaDestroy(&actorList);
}

static void EDENamedPointUIRefresh(EncounterEditDoc* encDoc)
{
	OldNamedPointInEncounter** namedPoints = NULL;
	EncounterDef* def = encDoc->def;
	int i, n = eaSize(&encDoc->selectedObjects);
	int numSelected;

	for (i = 0; i < n; i++)
	{
		GESelectedObject* selObject = encDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Point)
		{
			OldNamedPointInEncounter* point = def->namedPoints[selObject->objIndex];
			eaPush(&namedPoints, point);
		}
	}

	numSelected = eaSize(&namedPoints);

	if (numSelected == 1)
		ui_EditableSetText(encDoc->uiInfo.namedPointNameEntry, namedPoints[0]->pointName);
	else
		ui_EditableSetText(encDoc->uiInfo.namedPointNameEntry, "");

	if (numSelected && !encDoc->uiInfo.namedPointExpander->group)
		ui_ExpanderGroupInsertExpander(encDoc->uiInfo.propExpGroup, encDoc->uiInfo.namedPointExpander, 0);
	else if (!numSelected && encDoc->uiInfo.namedPointExpander->group)
		ui_ExpanderGroupRemoveExpander(encDoc->uiInfo.propExpGroup, encDoc->uiInfo.namedPointExpander);
}

static GESelectedObject* EDEFindSelectedActorObject(EncounterEditDoc* encDoc, int index)
{
	return GESelectedObjectFind(&encDoc->selectedObjects, GEObjectType_Actor, NULL, -1, index);
}
static GESelectedObject* EDEFindSelectedPointObject(EncounterEditDoc* encDoc, int index)
{
	return GESelectedObjectFind(&encDoc->selectedObjects, GEObjectType_Point, NULL, -1, index);
}

static OldActor** EDEGetSelectedActorList(EncounterEditDoc* encDoc)
{
	static OldActor** actorList = NULL;
	EncounterDef* def = encDoc->def;
	int i, numSelected = eaSize(&encDoc->selectedObjects);
	eaSetSize(&actorList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Actor)
		{
			OldActor* actor = def->actors[selObject->objIndex];
			eaPush(&actorList, actor);
		}
	}
	return actorList;
}

static int* EDEGetSelectedActorIndexList(EncounterEditDoc* encDoc)
{
	static int* actorIndexList = NULL;
	int i, numSelected = eaSize(&encDoc->selectedObjects);
	eaiSetSize(&actorIndexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Actor)
			eaiPush(&actorIndexList, selObject->objIndex);
	}
	return actorIndexList;
}
static int* EDEGetSelectedNamedPointIndexList(EncounterEditDoc* encDoc)
{
	static int* pointIndexList = NULL;
	int i, numSelected = eaSize(&encDoc->selectedObjects);
	eaiSetSize(&pointIndexList, 0);
	for (i = 0; i < numSelected; i++)
	{
		GESelectedObject* selObject = encDoc->selectedObjects[i];
		if (selObject->selType == GEObjectType_Point)
			eaiPush(&pointIndexList, selObject->objIndex);
	}
	return pointIndexList;
}

static int EDEFindSelectedCenter(EncounterEditDoc* encDoc, Vec3 centerOfActors)
{
	F32 scVal;
	Vec3 posOffset;
	Quat dontUseQuat;
	int totalActors = 0;
	OldActor** actorList = EDEGetSelectedActorList(encDoc);
	int i, numActors = eaSize(&actorList);
	int* pointList = EDEGetSelectedNamedPointIndexList(encDoc);
	int numPoints = eaiSize(&pointList);

	zeroVec3(centerOfActors);
	for (i = 0; i < numActors; i++)
	{
		oldencounter_GetActorPositionOffset(actorList[i], dontUseQuat, posOffset);
		addVec3(posOffset, centerOfActors, centerOfActors);
		totalActors++;
	}
	for (i = 0; i < numPoints; i++)
	{
		int pointIndex = pointList[i];
		OldNamedPointInEncounter * point = encDoc->def->namedPoints[pointIndex];
		addVec3(point->relLocation[3], centerOfActors, centerOfActors);
		totalActors++;
	}

	if (totalActors)
	{
		scVal = 1.0 / totalActors;
		scaleVec3(centerOfActors, scVal, centerOfActors);
		addVec3(centerOfActors, encDoc->encCenter[3], centerOfActors);
	}
	return totalActors;
}

static F32 EDEFindFurthestSelectedDist(EncounterEditDoc* encDoc, Vec3 centerPos)
{
	Vec3 actorPos;
	Quat dontUseQuat;
	F32 dist, furthestDistSquared = 0;
	OldActor** actorList = EDEGetSelectedActorList(encDoc);
	int i, numActors = eaSize(&actorList);
	for (i = 0; i < numActors; i++)
	{
		OldActor* actor = actorList[i];
		oldencounter_GetActorPositionOffset(actorList[i], dontUseQuat, actorPos);
		addVec3(actorPos, encDoc->encCenter[3], actorPos);
		dist = distance3SquaredXZ(actorPos, centerPos);
		if (dist > furthestDistSquared)
			furthestDistSquared = dist;
	}
	return sqrt(furthestDistSquared);
}

/************************************************************************/
/*            Encounter Properties UI Function Callbacks                */
/************************************************************************/ 

static void EDECritterGroupChangedCB(UIComboBox* combo, EncounterEditDoc* encDoc)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	const char *groupName = info?info->resourceName:NULL;
	EncounterDef* def = encDoc->def;

	REMOVE_HANDLE(def->critterGroup);
	if (groupName)
		SET_HANDLE_FROM_STRING(g_hCritterGroupDict, groupName, def->critterGroup);
	GESetDocUnsaved(encDoc);
}

static void EDEEncounterNameChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	const char* entryText = ui_TextEntryGetText(textEntry);
	EncounterDef* def = encDoc->def;

	// This is validated on save
	def->name = (char*)allocAddString(entryText);

	if(!def->name || !def->name[0])
	{
		def->name = (char*)allocAddString(encDoc->origDef->name);
	}

	if(!def->name || !def->name[0])
	{
		def->name = (char*)allocAddString("Encounter");
	}

	GESetDocUnsaved(encDoc);
}

static void EDEEncounterScopeChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	const char* entryText = ui_TextEntryGetText(textEntry);
	EncounterDef* def = encDoc->def;

	// This is validated on save
	if (entryText && entryText[0])
	{
		def->scope = (char*)allocAddString(entryText);
	}
	else
	{
		def->scope = NULL;
	}
	
	GESetDocUnsaved(encDoc);
}

static void EDEFactionChangedCB(UIComboBox* combo, EncounterEditDoc* encDoc)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	EncounterDef* def = encDoc->def;
	REMOVE_HANDLE(def->faction);
	if (info)
	{
		SET_HANDLE_FROM_STRING(g_hCritterFactionDict, info->resourceName, def->faction);
	}
	GESetDocUnsaved(encDoc);
}

static void EDEFailActionChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	int i, size = eaSize(&encDoc->def->actions);
	
	// Find the first Failure Action
	for (i = 0; i < size; ++i)
		if (encDoc->def->actions[i]->state == EncounterState_Failure)
			break;

	// If there isn't a Failure action, create one
	if (i == size)
	{
		eaPush(&encDoc->def->actions, StructCreate(parse_OldEncounterAction));
		encDoc->def->actions[i]->state = EncounterState_Failure;
	}

	// Update the Success action
	GEUpdateExpressionFromExpression(&encDoc->def->actions[i]->actionExpr, newExpr);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
}

static void EDEFailCondChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	GEUpdateExpressionFromExpression(&encDoc->def->failCond, newExpr);
	GESetDocUnsaved(encDoc);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
}

static void EDEGangIDChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int gangID = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->gangID = gangID;
	GESetDocUnsaved(encDoc);
}

static void EDELockoutRadiusChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int lockoutRadius = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->lockoutRadius = lockoutRadius;
	GESetDocUnsaved(encDoc);
}

static void EDEMaxLevelChangedCB(UIEditable* levelEntry, EncounterEditDoc* encDoc)
{
	EncounterDef* encDef = encDoc->def;
	const char* levelStr = ui_EditableGetText(levelEntry);
	encDef->maxLevel = atoi(levelStr);
	GESetDocUnsaved(encDoc);
}

static void EDEMinLevelChangedCB(UIEditable* levelEntry, EncounterEditDoc* encDoc)
{
	EncounterDef* encDef = encDoc->def;
	const char* levelStr = ui_EditableGetText(levelEntry);
	encDef->minLevel = atoi(levelStr);
	GESetDocUnsaved(encDoc);
}

static void EDEUsePlayerLevelCheckBoxChangedCB(UICheckButton* checkButton, EncounterEditDoc* encDoc)
{
	EncounterDef* def = encDoc->def;
	def->bUsePlayerLevel = checkButton->state;
	GESetDocUnsaved(encDoc);
}

static void EDEAmbushCheckBoxChangedCB(UICheckButton* checkButton, EncounterEditDoc* encDoc)
{
	EncounterDef* def = encDoc->def;
	def->bAmbushEncounter = checkButton->state;
	GESetDocUnsaved(encDoc);
}

static void EDEDynamicSpawnChangedCB(UIComboBox* pCombo, int iNewValue, EncounterEditDoc* encDoc)
{
	EncounterDef* def = encDoc->def;
	def->eDynamicSpawnType = iNewValue;
	GESetDocUnsaved(encDoc);
}

static void EDERespawnTimeChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int respawnTime = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->respawnTimer = respawnTime;
	GESetDocUnsaved(encDoc);
}

static void EDESpawnAnimChangedCB(UIEditable * text, EncounterEditDoc *encDoc)
{
	encDoc->def->spawnAnim = (char*)allocAddString(ui_EditableGetText(text));
	GESetDocUnsaved(encDoc);
}

static void EDESpawnChanceChangedCB(UISlider* slider, bool bFinished, EncounterEditDoc* encDoc)
{
	char countStr[64];
	EncounterPropUI* encounterUI = encDoc->uiInfo.encounterUI;
	EncounterDef* encDef = encDoc->def;
	U32 spawnChance = ui_IntSliderGetValue(slider);
	sprintf(countStr, "%u", spawnChance);
	ui_LabelSetText(encounterUI->chanceCount, countStr);
	encDef->spawnChance = spawnChance;
	GESetDocUnsaved(encDoc);
}

static void EDESpawnCondChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	GEUpdateExpressionFromExpression(&encDoc->def->spawnCond, newExpr);
	GESetDocUnsaved(encDoc);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
}

static void EDESpawnPerPlayerCheckBoxChangedCB(UICheckButton* checkButton, EncounterEditDoc* encDoc)
{
	EncounterDef* def = encDoc->def;
	def->bCheckSpawnCondPerPlayer = ui_CheckButtonGetState(checkButton);
	GESetDocUnsaved(encDoc);
}

static void EDESpawnRadiusChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int spawnRadius = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->spawnRadius = spawnRadius;
	GESetDocUnsaved(encDoc);
}

static void EDESuccessActionChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	int i, size = eaSize(&encDoc->def->actions);
	
	// Find the first Success Action
	for (i = 0; i < size; ++i)
		if (encDoc->def->actions[i]->state == EncounterState_Success)
			break;

	// If there isn't a Success action, create one
	if (i == size)
	{
		eaPush(&encDoc->def->actions, StructCreate(parse_OldEncounterAction));
		encDoc->def->actions[i]->state = EncounterState_Success;
	}

	// Update the Success action
	GEUpdateExpressionFromExpression(&encDoc->def->actions[i]->actionExpr, newExpr);
	GESetDocUnsaved(encDoc);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
}

static void EDESuccessCondChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	GEUpdateExpressionFromExpression(&encDoc->def->successCond, newExpr);
	GESetDocUnsaved(encDoc);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
}

static void EDETeamSizeChangedCB(UISlider* slider, bool bFinished, EncounterEditDoc* encDoc)
{
	char countStr[64];
	EncounterPropUI* encounterUI = encDoc->uiInfo.encounterUI;
	encDoc->activeTeamSize = ui_IntSliderGetValue(slider);
	sprintf(countStr, "%i", encDoc->activeTeamSize);
	ui_LabelSetText(encounterUI->teamSizeCount, countStr);
	EDEActorPropUIRefresh(encDoc, true);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, false);
}

static void EDEWaveCondChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	GEUpdateExpressionFromExpression(&encDoc->def->waveCond, newExpr);
	GESetDocUnsaved(encDoc);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
}

static void EDEWaveIntervalChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int waveInterval = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->waveInterval = waveInterval;
	GESetDocUnsaved(encDoc);
}

static void EDEWaveMaxDelayChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int newVal = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->waveDelayMax = newVal;
	GESetDocUnsaved(encDoc);
}

static void EDEWaveMinDelayChangedCB(UITextEntry* textEntry, EncounterEditDoc* encDoc)
{
	int newVal = atoi(ui_TextEntryGetText(textEntry));
	encDoc->def->waveDelayMin = newVal;
	GESetDocUnsaved(encDoc);
}

/************************************************************************/
/*              Actor Properties UI Function Callbacks                  */
/************************************************************************/

static void EDEModifySelectedActors(EncounterEditDoc* encDoc, GEActorChangeFunc changeFunc, const void *pChangeData)
{
	if (encDoc && encDoc->def && changeFunc)
	{
		int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
		int i;
		for (i = 0; i < eaiSize(&selActorIndexList); i++)
		{
			OldActor* actor = eaGet(&encDoc->def->actors, selActorIndexList[i]);
			if (actor){
				changeFunc(actor, NULL, pChangeData);
			}				
		}
	}
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, true);
}

static void EDEChangeActorName(EncounterDef* def, int* whichActors, char* actorName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			if (!actor->name || stricmp(actor->name, actorName))
			{
				char* uniqueName = GECreateUniqueActorName(def, actorName);
				actor->name = GEAllocPooledStringIfNN(uniqueName);
			}
		}
	}
}

static void EDEActorNameChangedCB(UIEditable* nameEntry, EncounterEditDoc* encDoc)
{
	char* actorName = (char*)ui_EditableGetText(nameEntry);
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorName(encDoc->def, selActorIndexList, actorName);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
}

static void EDEChangeActorCritter(EncounterDef* def, int* whichActors, const char* newCritterName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			REMOVE_HANDLE(actorInfo->critterDef);
			if (newCritterName && resGetInfo("CritterDef", newCritterName))
			{
				SET_HANDLE_FROM_STRING(g_hCritterDefDict, newCritterName, actorInfo->critterDef);
			}
		}
	}
}

static void EDEActorCritterSelectedCB(UITextEntry* entry, EncounterEditDoc* encDoc)
{
	const char* critterName = ui_TextEntryGetText(entry);
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorCritter(encDoc->def, selActorIndexList, critterName);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, true);
}

static void EDEChangeActorFSM(EncounterDef* def, int* whichActors, char* fsmName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorAIInfo* actorAIInfo = GEGetActorAIInfoForUpdate(actor, actor);
			REMOVE_HANDLE(actorAIInfo->hFSM);
			SET_HANDLE_FROM_STRING(gFSMDict, fsmName, actorAIInfo->hFSM);
		}
	}
}

static void EDEActorFSMChangedCB(UITextEntry* entry, EncounterEditDoc* encDoc)
{
	char* fsmName = (char*)ui_TextEntryGetText(entry);
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorFSM(encDoc->def, selActorIndexList, fsmName);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
}

static void EDEActorFSMOpenCB(UIButton* button, EncounterEditDoc* encDoc)
{
	OldActor** selActorList = EDEGetSelectedActorList(encDoc);
	const char* commonFSM = GEFindCommonActorFSMName(&selActorList);
	if(commonFSM)
		emOpenFileEx(commonFSM, "fsm");
}

static void EDEChangeActorDisplayName(EncounterDef* def, int* whichActors, const Message* displayName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			if (!actor->displayNameMsg.pEditorCopy)
				actor->displayNameMsg.pEditorCopy = oldencounter_CreateDisplayNameMessageForEncDefActor(def, actor);
			else // Update the Key and Scope, in case this was a copy or something
			{
				Message *temp = oldencounter_CreateDisplayNameMessageForEncDefActor(def, actor);				
				actor->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(temp->pcMessageKey);
				actor->displayNameMsg.pEditorCopy->pcScope = allocAddString(temp->pcScope);
				StructDestroy(parse_Message, temp);
			}
			if (actor->displayNameMsg.pEditorCopy)
			{
				StructFreeString(actor->displayNameMsg.pEditorCopy->pcDefaultString);
				actor->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(displayName->pcDefaultString);
				if (displayName->pcDescription)
				{
					StructFreeString(actor->displayNameMsg.pEditorCopy->pcDescription);
					actor->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(displayName->pcDescription);
				}
				actor->displayNameMsg.pEditorCopy->bDoNotTranslate = displayName->bDoNotTranslate;
				actor->displayNameMsg.pEditorCopy->bFinal = displayName->bFinal;
			}
		}
	}
}

static void EDEActorDisplayNameChangedCB(UIMessageEntry* entry, EncounterEditDoc* encDoc)
{
	const Message* dispName = ui_MessageEntryGetMessage(entry);
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorDisplayName(encDoc->def, selActorIndexList, dispName);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
}

static void EDEChangeActorSpawnCond(EncounterDef* def, int* whichActors, Expression* spawnWhen)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			GEUpdateExpressionFromExpression(&actorInfo->spawnCond, spawnWhen);
		}
	}
}

static void EDEActorSpawnWhenChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorSpawnCond(encDoc->def, selActorIndexList, newExpr);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, true);
}

static void EDEChangeActorInteractCond(EncounterDef* def, int* whichActors, Expression* newCond)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			GEUpdateExpressionFromExpression(&actorInfo->oldActorInteractProps.interactCond, newCond);
		}
	}
}

static void EDEActorInteractCondChangedCB(Expression* newExpr, EncounterEditDoc* encDoc)
{
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorInteractCond(encDoc->def, selActorIndexList, newExpr);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, true);
}

static void EDEChangeActorSpawnEnabled(EncounterDef* def, int* whichActors, bool enableSpawn, U32 teamSize)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			if (enableSpawn)
				actor->disableSpawn &= ~(1 << teamSize);
			else
				actor->disableSpawn |= (1 << teamSize);
			actor->disableSpawn &= (~ActorScalingFlag_Inherited);
		}
	}
}

static void EDEActorSpawnEnabledChangedCB(UICheckButton* button, void* teamSizePtr)
{
	U32 teamSize = PTR_TO_U32(teamSizePtr);
	EncounterEditDoc* encDoc = (EncounterEditDoc*)GEGetActiveEditorDocEM("EncounterDef");
	if (encDoc)
	{
		int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
		EDEChangeActorSpawnEnabled(encDoc->def, selActorIndexList, ui_CheckButtonGetState(button), teamSize);
		GESetDocUnsaved(encDoc);
		EDEActorPropUIRefresh(encDoc, false);
		GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, false);
	}
}

static void EDEChangeActorBossBarEnabled(EncounterDef* def, int* whichActors, bool enableBossBar, U32 teamSize)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			if (enableBossBar)
				actor->useBossBar |= (1 << teamSize);
			else
				actor->useBossBar &= ~(1 << teamSize);
			actor->useBossBar &= (~ActorScalingFlag_Inherited);
		}
	}
}

static void EDEActorBossBarEnabledChangedCB(UICheckButton* button, void* teamSizePtr)
{
	U32 teamSize = PTR_TO_U32(teamSizePtr);
	EncounterEditDoc* encDoc = (EncounterEditDoc*)GEGetActiveEditorDocEM("EncounterDef");
	if (encDoc)
	{
		int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
		EDEChangeActorBossBarEnabled(encDoc->def, selActorIndexList, ui_CheckButtonGetState(button), teamSize);
		GESetDocUnsaved(encDoc);
		EDEActorPropUIRefresh(encDoc, false);
	}
}

static void EDEChangeActorRank(EncounterDef* def, int* whichActors, const char *pcRank)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			actorInfo->pcCritterRank = allocAddString(pcRank);
		}
	}
}

static void EDEActorRankChangedCB(UIComboBox* combo, EncounterEditDoc* encDoc)
{
	char *pcSelRank = NULL;
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	ui_ComboBoxGetSelectedAsString(combo, &pcSelRank);
	EDEChangeActorRank(encDoc->def, selActorIndexList, pcSelRank);
	estrDestroy(&pcSelRank);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, false);
}

static void EDEChangeActorSubRank(EncounterDef* def, int* whichActors, const char *pcSubRank)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			actorInfo->pcCritterSubRank = allocAddString(pcSubRank);
		}
	}
}

static void EDEActorSubRankChangedCB(UIComboBox* combo, EncounterEditDoc* encDoc)
{
	char *pcSelSubRank = NULL;
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	ui_ComboBoxGetSelectedAsString(combo, &pcSelSubRank);
	EDEChangeActorSubRank(encDoc->def, selActorIndexList, pcSelSubRank);
	estrDestroy(&pcSelSubRank);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
	GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, false);
}

static void EDEChangeActorGroup(EncounterDef* def, int* whichActors, const char* groupName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			REMOVE_HANDLE(actorInfo->critterGroup);
			if (groupName)
				SET_HANDLE_FROM_STRING(g_hCritterGroupDict, groupName, actorInfo->critterGroup);
		}
	}
}

static void EDEChangeActorFaction(EncounterDef* def, int* whichActors, const char* factionName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			REMOVE_HANDLE(actorInfo->critterFaction);
			if (factionName)
			{
				SET_HANDLE_FROM_STRING(g_hCritterFactionDict, factionName, actorInfo->critterFaction);
			}
		}
	}
}

static void EDEActorFactionChangedCB(UIComboBox* combo, EncounterEditDoc* encDoc)
{
	ResourceInfo *info = ui_ComboBoxGetSelectedObject(combo);
	const char *factionName = info?info->resourceName:NULL;
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorFaction(encDoc->def, selActorIndexList, factionName);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
}

static void EDEChangeActorSpawnAnim(EncounterDef * def, int *whichActors, char * spawnAnim )
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			actorInfo->pchSpawnAnim = (char*)allocAddString(spawnAnim);
		}
	}
}

static void EDEActorSpawnAnimChangedCB(UIEditable * text, EncounterEditDoc *encDoc)
{
	char* spawnAnim = (char*)ui_EditableGetText(text);
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorSpawnAnim(encDoc->def, selActorIndexList, spawnAnim);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
}

static void EDEChangeActorVar(EncounterDef* def, int* whichActors, const char* varName, const char* varStr)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorAIInfo* actorAIInfo = GEGetActorAIInfoForUpdate(actor, actor);
			OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(actorAIInfo, varName);
			if (varStr && varStr[0])
			{
				if (!actorVar)
				{
					actorVar = StructCreate(parse_OldEncounterVariable);
					actorVar->varName = (char*)allocAddString(varName);
					eaPush(&actorAIInfo->actorVars, actorVar);
				}
				MultiValSetString(&actorVar->varValue, varStr);
			}
			else if (actorVar)
			{
				eaFindAndRemove(&actorAIInfo->actorVars, actorVar);

				StructDestroy(parse_OldEncounterVariable, actorVar);
			}
		}
	}
}

static void EDEActorVarChangedCB(UIEditable* textEdit, const char* varName)
{
	const char* varStr = ui_EditableGetText(textEdit);
	EncounterEditDoc* encDoc = (EncounterEditDoc*)GEGetActiveEditorDocEM("EncounterDef");
	if (encDoc)
	{
		int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
		EDEChangeActorVar(encDoc->def, selActorIndexList, varName, varStr);
		GESetDocUnsaved(encDoc);
		EDEActorPropUIRefresh(encDoc, false);
	}
}

static void EDEChangeActorVarMessage(EncounterDef* def, int* whichActors, const char* varName, const Message* varMessage)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = oldencounter_GetActorInfo(actor);
			OldActorAIInfo* actorAIInfo = GEGetActorAIInfoForUpdate(actor, actor);
			OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(actorAIInfo, varName);
			FSM *fsm = oldencounter_GetActorFSM(actorInfo, actorAIInfo);
			Message *defaultMessage = oldencounter_CreateVarMessageForEncounterDefActor(def, actor, varName, SAFE_MEMBER(fsm, name));

			// Create var if it doesn't exist
			if (!actorVar)
			{
				actorVar = StructCreate(parse_OldEncounterVariable);
				actorVar->varName = (char*)allocAddString(varName);
				eaPush(&actorAIInfo->actorVars, actorVar);
			}

			// Update message
			GEGenericUpdateMessage(&actorVar->message.pEditorCopy, varMessage, defaultMessage);

			// Update message key in the variable
			if (isValidMessage(actorVar->message.pEditorCopy)){
				MultiValSetString(&actorVar->varValue, actorVar->message.pEditorCopy->pcMessageKey);
			} else {
				eaFindAndRemove(&actorAIInfo->actorVars, actorVar);
				StructDestroy(parse_OldEncounterVariable, actorVar);
			}
			StructDestroy(parse_Message, defaultMessage);
		}
	}
}

static void EDEActorVarMessageChangedCB(UIMessageEntry* msgEntry, const char* varName)
{
	const Message* varMsg = ui_MessageEntryGetMessage(msgEntry);
	EncounterEditDoc* encDoc = (EncounterEditDoc*)GEGetActiveEditorDocEM("EncounterDef");
	if (encDoc)
	{
		int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
		EDEChangeActorVarMessage(encDoc->def, selActorIndexList, varName, varMsg);
		GESetDocUnsaved(encDoc);
		EDEActorPropUIRefresh(encDoc, false);
	}
}

static void EDEChangeActorContact(EncounterDef* def, int* whichActors, const char* contactName)
{
	int i, n = eaiSize(&whichActors);
	for (i = 0; i < n; i++)
	{
		int actorIndex = whichActors[i];
		if (actorIndex >= 0 && actorIndex < eaSize(&def->actors))
		{
			OldActor* actor = def->actors[actorIndex];
			OldActorInfo* actorInfo = GEGetActorInfoForUpdate(actor, actor);
			REMOVE_HANDLE(actorInfo->contactScript);
			if (contactName)
			{
				SET_HANDLE_FROM_STRING(g_ContactDictionary, contactName, actorInfo->contactScript);
			}
		}
	}
}

static void EDEActorContactChangedCB(UIEditable* editable, EncounterEditDoc* encDoc)
{
	const char* contactName = ui_EditableGetText(editable);
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	EDEChangeActorContact(encDoc->def, selActorIndexList, contactName);
	GESetDocUnsaved(encDoc);
	EDEActorPropUIRefresh(encDoc, false);
}

// Named point callback
static void EDEPointNameChangedCB(UIEditable* nameEntry, EncounterEditDoc* encDoc)
{
	char* newName = (char*)ui_EditableGetText(nameEntry);
	int* selPointIndexList = EDEGetSelectedNamedPointIndexList(encDoc);

	// Only change one point's name
	if(selPointIndexList)
	{
		if (encDoc->def->namedPoints && (selPointIndexList[0] < eaSize(&encDoc->def->namedPoints)))
		{
			OldNamedPointInEncounter* point = encDoc->def->namedPoints[selPointIndexList[0]];
			point->pointName = (char*)allocAddString(newName);
		}
	}

	GESetDocUnsaved(encDoc);
	EDENamedPointUIRefresh(encDoc);
}

void EDESetupUI(EncounterEditDoc* encDoc)
{
	EncounterEditorUI* uiInfo = &encDoc->uiInfo;

	// Main properties window for the encounter and actor
	// Contains an expander group that holds an expander for all the different properties
	uiInfo->propertiesWindow = ui_WindowCreate("Properties", 800, 200, 415, 500);
	uiInfo->propExpGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition((UIWidget*)uiInfo->propExpGroup, 0, 0);
	ui_WidgetSetDimensionsEx((UIWidget*)uiInfo->propExpGroup, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(uiInfo->propertiesWindow, uiInfo->propExpGroup);

	// Setup the encounter window
	uiInfo->encounterUI = GEEncounterPropUICreate(encDoc, 0, EDEEncounterNameChangedCB, EDEAmbushCheckBoxChangedCB, EDESpawnPerPlayerCheckBoxChangedCB, NULL, NULL, EDEDynamicSpawnChangedCB, EDESpawnCondChangedCB, EDESuccessCondChangedCB, EDEFailCondChangedCB, EDESuccessActionChangedCB, EDEFailActionChangedCB, EDEWaveCondChangedCB, EDEWaveIntervalChangedCB, EDEWaveMinDelayChangedCB, EDEWaveMaxDelayChangedCB, EDESpawnRadiusChangedCB, EDELockoutRadiusChangedCB, EDETeamSizeChangedCB, encDoc->activeTeamSize, NULL, EDECritterGroupChangedCB, EDEFactionChangedCB, EDEGangIDChangedCB, EDESpawnChanceChangedCB, NULL, NULL, EDEMinLevelChangedCB, EDEMaxLevelChangedCB, EDEUsePlayerLevelCheckBoxChangedCB, EDESpawnAnimChangedCB, EDERespawnTimeChangedCB, EDEEncounterScopeChangedCB);
	GEEncounterPropUIRefreshSingle(uiInfo->encounterUI, encDoc->def, NULL, NULL, true);
	ui_ExpanderGroupAddExpander(uiInfo->propExpGroup, uiInfo->encounterUI->encPropExpander);

	// Setup the actor window
	uiInfo->actorUI = GEActorPropUICreate(encDoc, EDEActorNameChangedCB, EDEActorCritterSelectedCB, EDEActorFSMChangedCB, EDEActorFSMOpenCB, EDEActorSpawnEnabledChangedCB, EDEActorRankChangedCB, EDEActorSubRankChangedCB, EDEActorFactionChangedCB, NULL, NULL, NULL, NULL, EDEActorContactChangedCB, EDEActorDisplayNameChangedCB, EDEActorSpawnWhenChangedCB, EDEActorInteractCondChangedCB, EDEActorBossBarEnabledChangedCB, EDEActorVarChangedCB, EDEActorVarMessageChangedCB, EDEActorSpawnAnimChangedCB, EDEModifySelectedActors);

	// Setup the named point UI
	uiInfo->namedPointExpander = ui_ExpanderCreate("Named Point", 0);
	uiInfo->namedPointExpander->openedHeight = GETextEntryCreate(&uiInfo->namedPointNameEntry, "Name", "", 0, 0, 140, 0, 60, GE_VALIDFUNC_NONE, EDEPointNameChangedCB, encDoc, uiInfo->namedPointExpander);
	ui_ExpanderSetOpened(uiInfo->namedPointExpander, true);
}

static void EDESelectActor(EncounterEditDoc* encDoc, int whichActor, bool additive)
{
	GESelectObject(&encDoc->selectedObjects, GEObjectType_Actor, NULL, -1, whichActor, additive);
	EDEActorPropUIRefresh(encDoc, true);
	EDENamedPointUIRefresh(encDoc);
}
static void EDESelectNamedPoint(EncounterEditDoc* encDoc, int whichPoint, bool additive)
{
	GESelectObject(&encDoc->selectedObjects, GEObjectType_Point, NULL, -1, whichPoint, additive);
	EDENamedPointUIRefresh(encDoc);
}

/************************************************************************/
/*               Encounter Editor Key Bind Callbacks                    */
/************************************************************************/

static void EDEFixupActorMessages(EncounterDef *encDef, OldActor *actor)
{
	OldActorInfo *actorInfo = actor->details.info;
	OldActorAIInfo *aiInfo = actor->details.aiInfo;

	// Fix the display name message
	if (actor->displayNameMsg.pEditorCopy)
	{
		Message *defaultMsg = oldencounter_CreateDisplayNameMessageForEncDefActor(encDef, actor);
		GEGenericUpdateMessage(&actor->displayNameMsg.pEditorCopy, NULL, defaultMsg);
		StructDestroy(parse_Message, defaultMsg);
	}

	// Fix all messages in Extern Vars
	if (aiInfo)
	{
		int iVar, numVars = eaSize(&aiInfo->actorVars);
		for (iVar = numVars-1; iVar >= 0; --iVar)
		{
			OldEncounterVariable *var = aiInfo->actorVars[iVar];
			FSM *fsm = oldencounter_GetActorFSM(actorInfo, aiInfo);
			FSMExternVar *externVar = (fsm&&var&&var->varName)?fsmExternVarFromName(fsm, var->varName, "encounter"):0;
			if (externVar && externVar->scType && (stricmp(externVar->scType, "message") == 0))
			{
				Message *defaultMsg = oldencounter_CreateVarMessageForEncounterDefActor(encDef, actor, var->varName, SAFE_MEMBER(fsm, name));
				GEGenericUpdateMessage(&var->message.pEditorCopy, NULL, defaultMsg);
				MultiValSetString(&var->varValue, defaultMsg->pcMessageKey);
				StructDestroy(parse_Message, defaultMsg);
			}
			else if (fsm && !externVar)
			{
				eaFindAndRemove(&aiInfo->actorVars, var);
				StructDestroy(parse_OldEncounterVariable, var);
			}
		}
	}
}

int EDENextUniqueActorID(EncounterDef* def)
{
	int highestUniqueID = 0;
	int i, n = eaSize(&def->actors);
	for (i = 0; i < n; i++)
		if (def->actors[i]->uniqueID > highestUniqueID)
			highestUniqueID = def->actors[i]->uniqueID;
	return highestUniqueID + 1;
}

int EDENextUniquePointID(EncounterDef* def)
{
	int highestUniqueID = 0;
	int i, n = eaSize(&def->namedPoints);
	for (i = 0; i < n; i++)
		if (def->namedPoints[i]->id > highestUniqueID)
			highestUniqueID = def->namedPoints[i]->id;
	return highestUniqueID + 1;
}

static void EDEMoveActor(EncounterDef* def, int whichActor, Mat4 newRelMat, bool makeCopy)
{
	if (whichActor >= 0 && whichActor < eaSize(&def->actors))
	{
		OldActor* actor = def->actors[whichActor];
		OldActor* changeActor = actor;

		// Since this is a new actor, make a copy then force team size to 1 to update default position
		if (makeCopy)
		{
			changeActor = GEActorCreate(EDENextUniqueActorID(def), actor);
			EDEFixupActorMessages(def, changeActor);
			eaPush(&def->actors, changeActor);
		}

		GEApplyActorPositionChange(changeActor, newRelMat, actor);
	}
}

void EDEAddNamedPoint(EncounterDef* def, Mat4 pointMat)
{
	OldNamedPointInEncounter* point = StructCreate(parse_OldNamedPointInEncounter);

	copyMat4(pointMat, point->relLocation);
	point->id = EDENextUniquePointID(def);

	eaPush(&def->namedPoints, point);
}

static void EDEMoveEncounterOriginByOffset(EncounterDef* def, Vec3 offsetChange)
{
	int i, n = eaSize(&def->actors);
	for (i = 0; i < n; i++)
	{
		OldActor* actor = def->actors[i];
		OldActorPosition* actorPos = actor->details.position;
		if (actorPos)
			addVec3(actorPos->posOffset, offsetChange, actorPos->posOffset);
	}
}

void EDEMovePoint(EncounterDef* def, int whichPoint, Mat4 newLoc, bool makeCopy)
{
	if (whichPoint >= 0 && whichPoint < eaSize(&def->namedPoints))
	{
		OldNamedPointInEncounter* point = def->namedPoints[whichPoint];

		// Since this is a new actor, make a copy then force team size to 1 to update default position
		if (makeCopy)
		{
			EDEAddNamedPoint(def, newLoc);
		}
		else
			copyMat4(newLoc, point->relLocation);
	}
}

// Apply the tool to find the changes that need to be made to the placed items
static void EDEPasteSelected(EncounterEditDoc* encDoc)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	EncounterDef* def = encDoc->def;
	if (GEPlacementToolIsInPlacementMode(placementTool))
	{
		Vec3 originChange;
		Mat4 actorMat, originMat, newObjectMat;
		int i, n, oldActorSize = eaSize(&def->actors);
		int oldPointSize = eaSize(&def->namedPoints);
		if (placementTool->moveOrigin)
		{
			copyMat4(unitmat, originMat);
			GEPlacementToolApply(placementTool, originMat);
			addVec3(encDoc->encCenter[3], originMat[3], encDoc->encCenter[3]);
			negateVec3(originMat[3], originChange);
			EDEMoveEncounterOriginByOffset(def, originChange);
			placementTool->moveOrigin = 0;
		}
		else if (placementTool->createNew)
		{
			copyMat4(unitmat, newObjectMat);
			GEPlacementToolApply(placementTool, newObjectMat);

			if(encDoc->typeToCreate == GEObjectType_Actor)
				GEEncounterDefAddActor(def, GET_REF(encDoc->newCritter), newObjectMat, EDENextUniqueActorID(def));
			else if(encDoc->typeToCreate == GEObjectType_Point)
				EDEAddNamedPoint(def, newObjectMat);
			placementTool->createNew = 0;
		}
		else if (placementTool->moveSelected)
		{
			bool makeCopy = GEPlacementToolShouldCopy(placementTool);
			int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
			int* selPointIndexList = EDEGetSelectedNamedPointIndexList(encDoc);
			int numSelected = eaiSize(&selActorIndexList);

			// Move all selected actors
			for (i = 0; i < numSelected; i++)
			{
				int actorIndex = selActorIndexList[i];
				if (def->actors && (actorIndex < eaSize(&def->actors)))
				{
					OldActor* actor = def->actors[actorIndex];
					GEFindActorMat(actor, encDoc->encCenter, actorMat);
					GEPlacementToolApply(placementTool, actorMat);
					subVec3(actorMat[3], encDoc->encCenter[3], actorMat[3]);
					EDEMoveActor(def, actorIndex, actorMat, makeCopy);
				}
			}

			// Move all selected points
			numSelected = eaiSize(&selPointIndexList);
			for (i = 0; i < numSelected; i++)
			{
				int pointIndex = selPointIndexList[i];
				if (def->namedPoints && (pointIndex < eaSize(&def->namedPoints)))
				{
					OldNamedPointInEncounter* point = def->namedPoints[pointIndex];
					GEFindEncPointMat(point, encDoc->encCenter, actorMat);
					GEPlacementToolApply(placementTool, actorMat);
					EDEMovePoint(def, pointIndex, actorMat, makeCopy);
				}
			}

			placementTool->moveSelected = placementTool->copySelected = 0;
		}

		// Assume newly placed actors are most important, clear old selected and force all new to be selected
		n = eaSize(&def->actors);
		if (n != oldActorSize)
		{
			eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);
			for (i = oldActorSize; i < n; i++)
				EDESelectActor(encDoc, i, true);
		}
		n = eaSize(&def->namedPoints);
		if (n != oldPointSize)
		{
			eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);
			for (i = oldPointSize; i < n; i++)
				EDESelectNamedPoint(encDoc, i, true);
		}

		EDEActorPropUIRefresh(encDoc, true);
		EDENamedPointUIRefresh(encDoc);
		GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, false);
		GESetDocUnsaved(encDoc);
	}
}

static void EDECopyEncounterActor(EncounterEditDoc* encDoc, bool makeCopy, bool useGizmos, const Vec3 selActorPos, const Vec3 clickOffset)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	if (!GEPlacementToolIsInPlacementMode(placementTool))
	{
		Vec3 toolOrigin, moveCenter;
		placementTool->useGizmos = !!useGizmos;
		if (EDEFindSelectedCenter(encDoc, moveCenter))
		{
			if (!placementTool->useGizmos)
				copyVec3(selActorPos, toolOrigin);
			else
				copyVec3(moveCenter, toolOrigin);
			GEPlacementToolReset(placementTool, toolOrigin, clickOffset);
			placementTool->moveSelected = 1;
			placementTool->copySelected = !!makeCopy;
		}
	}
}

static void EDECancelAction(EncounterEditDoc* encDoc)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	if (!GEPlacementToolCancelAction(placementTool))
	{
		eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);
		EDEActorPropUIRefresh(encDoc, true);
		EDENamedPointUIRefresh(encDoc);
	}
}

static void EDEDeleteActor(EncounterDef* def, int* whichActors)
{
	int i, n = eaiSize(&whichActors);

	// Free all the actors but just set the old positions to null
	for (i = 0; i < n; i++)
	{
		int whichActor = whichActors[i];
		if (whichActor >= 0 && whichActor < eaSize(&def->actors))
		{
			StructDestroy(parse_OldActor, def->actors[whichActor]);
			def->actors[whichActor] = NULL;
		}
	}

	// Now remove all the NULLs
	for (i = eaSize(&def->actors) - 1; i >= 0; i--)
		if (!def->actors[i])
			eaRemove(&def->actors, i);
}

static void EDEDeleteEncounterPoints(EncounterDef* def, int* whichPoints)
{
	int i, n = eaiSize(&whichPoints);

	// Free all the points but just set the old positions to null
	for (i = 0; i < n; i++)
	{
		int whichPoint = whichPoints[i];
		if (whichPoint >= 0 && whichPoint < eaSize(&def->namedPoints))
		{
			StructDestroy(parse_OldNamedPointInEncounter, def->namedPoints[whichPoint]);
			def->namedPoints[whichPoint] = NULL;
		}
	}

	// Now remove all the NULLs
	for (i = eaSize(&def->namedPoints) - 1; i >= 0; i--)
		if (!def->namedPoints[i])
			eaRemove(&def->namedPoints, i);
}

// Actually deletes points in the encounter, too
static void EDEDeleteEncounterActor(EncounterEditDoc* encDoc)
{
	EncounterDef* def = encDoc->def;
	if (!GEPlacementToolIsInPlacementMode(&encDoc->placementTool))
	{
		int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
		int* selPointIndexList = EDEGetSelectedNamedPointIndexList(encDoc);
		EDEDeleteActor(def, selActorIndexList);
		EDEDeleteEncounterPoints(def, selPointIndexList);
		eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);
		EDEActorPropUIRefresh(encDoc, true);
		EDENamedPointUIRefresh(encDoc);
		GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, false);
		GESetDocUnsaved(encDoc);
	}
}

static void EDEMoveEncounterOrigin(EncounterEditDoc* encDoc)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	if (!GEPlacementToolIsInPlacementMode(placementTool))
	{
		GEPlacementToolReset(placementTool, encDoc->encCenter[3], zerovec3);
		placementTool->moveOrigin = 1;
		placementTool->useGizmos = 1;
	}
}

static void EDEInputLeftClick(EncounterEditDoc* encDoc)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	EncounterDef* def = encDoc->def;
	bool inPlaceMode = GEPlacementToolIsInPlacementMode(placementTool);

	if (inPlaceMode && !placementTool->useGizmos)
	{
		EDEPasteSelected(encDoc);
		if (placementTool->useAdditiveSelect)
			placementTool->createNew = 1;
	}
	else if (!inPlaceMode)
	{
		F32 distance = -1;
		int clickedActor = GEWhichActorUnderMouse(def, encDoc->encCenter, NULL, NULL, &distance);
		int clickedPoint = GEWhichEncounterPointUnderMouse(def, encDoc->encCenter, NULL, NULL, &distance);

		// If we found a point, it was nearer than the nearest actor
		if(clickedPoint != -1)
			EDESelectNamedPoint(encDoc, clickedPoint, placementTool->useAdditiveSelect);
		else	// Clears the selection list if there's no clickedActor
			EDESelectActor(encDoc, clickedActor, placementTool->useAdditiveSelect);
	}
}

static void EDESnapCamera(EncounterEditDoc* encDoc)
{
	Vec3 centerPos;
	GfxCameraController* camera = gfxGetActiveCameraController();
	int numSelected = EDEFindSelectedCenter(encDoc, centerPos);
	if (numSelected)
	{
		// Bump the center position up so it isn't looking at the objects feet
		centerPos[1] += 5;
		gfxCameraControllerSetTarget(camera, centerPos);
		if (numSelected > 1)
			camera->camdist = ME_MINSNAPDIST + EDEFindFurthestSelectedDist(encDoc, centerPos) * 2;
		else
			camera->camdist = ME_MINSNAPDIST;
	}
	else
	{
		globCmdParse("Camera.center");
	}
}


static GEPlacementTool* EDEGetPlacementTool(EncounterEditDoc* encDoc)
{
	return &encDoc->placementTool;
}

static bool EDEBeginQuickPlace(EncounterEditDoc* encDoc)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	EncounterDef* def = encDoc->def;
	Vec3 selActorPos, clickOffset;
	F32 distance = -1;
	int whichActor = GEWhichActorUnderMouse(def, encDoc->encCenter, selActorPos, clickOffset, &distance);
	int whichPoint = GEWhichEncounterPointUnderMouse(def, encDoc->encCenter, selActorPos, clickOffset, &distance);

	// If a point was found, it is closer than the nearest actor
	if(whichPoint != -1)
	{
		// If the point is not already selected, select it
		if (!EDEFindSelectedPointObject(encDoc, whichPoint))
			EDESelectNamedPoint(encDoc, whichPoint, placementTool->useAdditiveSelect);
		EDECopyEncounterActor(encDoc, false, false, selActorPos, clickOffset);
		return true;
	}
	else if(whichActor != -1)
	{
		// If the actor is not already selected, select it
		if (!EDEFindSelectedActorObject(encDoc, whichActor))
			EDESelectActor(encDoc, whichActor, placementTool->useAdditiveSelect);
		EDECopyEncounterActor(encDoc, false, false, selActorPos, clickOffset);
		return true;
	}
	return false;
}

typedef struct EDEUndoStackState{
	EncounterDef *prevState;
	EncounterDef *nextState;
} EDEUndoStackState;

static bool EDEUndo(EncounterEditDoc* eeDoc, EDEUndoStackState* state)
{
	// Deselect all objects
	EDECancelAction(eeDoc);
	eaDestroyEx(&eeDoc->selectedObjects, GESelectedObjectDestroyCB);

	// Destroy the old UI
	EDEDestroyUIEM(eeDoc);

	// Copy over the current def
	StructCopyFields(parse_EncounterDef, state->prevState, eeDoc->def, 0, 0);
	if (eeDoc->previousState)
		StructDestroy(parse_EncounterDef, eeDoc->previousState);
	eeDoc->previousState = StructCloneFields(parse_EncounterDef, eeDoc->def);

	// Recreate the UI
	EDESetupUIEM(eeDoc);
	return true;
}

static bool EDERedo(EncounterEditDoc* eeDoc, EDEUndoStackState* state)
{
	// Deselect all objects
	EDECancelAction(eeDoc);
	eaDestroyEx(&eeDoc->selectedObjects, GESelectedObjectDestroyCB);

	// Destroy the old UI
	EDEDestroyUIEM(eeDoc);

	// Copy over the current def
	StructCopyFields(parse_EncounterDef, state->nextState, eeDoc->def, 0, 0);
	if (eeDoc->previousState)
		StructDestroy(parse_EncounterDef, eeDoc->previousState);
	eeDoc->previousState = StructCloneFields(parse_EncounterDef, eeDoc->def);

	// Recreate the UI
	EDESetupUIEM(eeDoc);
	return true;
}

static EDEUndoStackState *EDESaveState(EncounterEditDoc* eeDoc)
{
	EDEUndoStackState *state = calloc(1, sizeof(EDEUndoStackState));
	
	state->prevState = eeDoc->previousState;
	state->nextState = StructCloneFields(parse_EncounterDef, eeDoc->def);
	eeDoc->previousState = StructCloneFields(parse_EncounterDef, eeDoc->def);

	return state;
}
static bool EDEFreeState(void* unused, EDEUndoStackState* state)
{
	StructDestroy(parse_EncounterDef, state->nextState);
	StructDestroy(parse_EncounterDef, state->prevState);
	free(state);
	return true;
}

EditDocDefinition EDEDocDefinition = 
{
	EDEPasteSelected,			// Paste
	EDECopyEncounterActor,		// Cut/Copy
	EDECancelAction,			// Cancel
	EDEDeleteEncounterActor,	// Delete
	EDEMoveEncounterOrigin,		// Move Origin
	NULL,						// Move SpawnLoc
	NULL,						// Freeze Selected Objects
	NULL,						// Unfreeze all frozen objects
	EDEInputLeftClick,			// LeftClick
	NULL,						// RightClick
	NULL,						// Group
	NULL,						// Ungroup
	NULL,						// Attach
	NULL,						// Detach
	EDESnapCamera,				// Center Camera
	EDEGetPlacementTool,			// PlacementTool from Doc
	EDEBeginQuickPlace,			// Begin Quickplace
	EDEPlaceObject,				// Place an object
	EDESaveState,				// Push an undo/redo state
	EDEUndo,					// Revert to a previous (or future) state
	EDERedo,					// Revert to a previous (or future) state
	EDEFreeState,				// Free a saved state
};

/************************************************************************/
/*                  Encounter Editor AM Callbacks                       */
/************************************************************************/

void EDEFixupEncounterMessages(EncounterDef *encDef)
{
	int i, n = eaSize(&encDef->actors);
	for (i = 0; i < n; i++)
	{
		OldActor *actor = encDef->actors[i];
		EDEFixupActorMessages(encDef, actor);
	}
}

static void EDEInitEditorMessages(EncounterDef *def)
{
	langMakeEditorCopy(parse_EncounterDef, def, false);
	EDEFixupEncounterMessages(def);
}

EncounterDef* EDENewEncounter(const char* name, EncounterEditDoc* encDoc)
{
	const char* encounterName;
	char outFile[MAX_PATH];
	EncounterDef* encounter;
	encDoc->docDefinition = &EDEDocDefinition;
	
	// Now create the encounter if this is a new doc, otherwise open and reload existing encounter
	encounterName = name ? name : EDECreateUniqueEncounterName("Encounter");
	encounter = StructCreate(parse_EncounterDef);
	encounter->name = (char*)allocAddString(encounterName);
	sprintf(outFile, "defs/encounters/%s.encounter", encounter->name);
	encounter->filename = allocAddString(outFile);
	encDoc->newAndNeverSaved = 1;

	encDoc->def = encounter;
	encDoc->origDef = StructClone(parse_EncounterDef, encounter);
	encDoc->activeTeamSize = 1;

	// Setup the encounter doc UI
	EDESetupUI(encDoc);
	//encDoc->amDoc.primary_ui_window = encDoc->uiInfo.encounterUI->window;
	// MERGEFIX

	// Reset all aspects of the placement tool
	copyMat4(unitmat, encDoc->encCenter);
	encDoc->placementTool.rotGizmo = RotateGizmoCreate();
	encDoc->placementTool.transGizmo = TranslateGizmoCreate();
	GEPlacementToolReset(&encDoc->placementTool, zerovec3, zerovec3);

	EDEInitEditorMessages(encounter);

	return encounter;
}

void EDEOpenEncounter(EncounterDef *def, EncounterEditDoc* encDoc)
{
	encDoc->docDefinition = &EDEDocDefinition;
	encDoc->def = StructClone(parse_EncounterDef, def);
	encDoc->origDef = StructClone(parse_EncounterDef, def);
	encDoc->activeTeamSize = 1;

	// Reset all aspects of the placement tool
	copyMat4(unitmat, encDoc->encCenter);
	encDoc->placementTool.rotGizmo = RotateGizmoCreate();
	encDoc->placementTool.transGizmo = TranslateGizmoCreate();
	GEPlacementToolReset(&encDoc->placementTool, zerovec3, zerovec3);

	EDEInitEditorMessages(encDoc->def);
	EDEInitEditorMessages(encDoc->origDef);
}

void EDECloseEncounter(EncounterEditDoc* encDoc)
{
	// Delete the copy of the Encounter
	StructDestroy(parse_EncounterDef, encDoc->def);
	encDoc->def = NULL;

	// Free everything else allocated by the doc
	StructDestroy(parse_EncounterDef, encDoc->origDef);
	RotateGizmoDestroy(encDoc->placementTool.rotGizmo);
	TranslateGizmoDestroy(encDoc->placementTool.transGizmo);
	eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);
}

EMTaskStatus EDESaveEncounter(EncounterEditDoc* editDoc)
{
	EncounterDef* encDef = editDoc->def;
	EMTaskStatus status;
	if (emHandleSaveResourceState(editDoc->emDoc.editor, encDef->name, &status)) {
		return status;
	}

	// --- Pre-save validation goes here ---

	// Ensure that the encounter has a name
	if (!editDoc->def->name || !editDoc->def->name[0])
	{
		editDoc->def->name = (char*)allocAddString(editDoc->origDef->name);
		if (!editDoc->def->name || !editDoc->def->name[0])
		{
			editDoc->def->name = (char*)allocAddString("Encounter");
		}
	}

	// Ensure that the encounter has a unique name
	if (stricmp(editDoc->def->name, editDoc->origDef->name) ||
		stricmp(editDoc->def->scope?editDoc->def->scope:"", editDoc->origDef->scope?editDoc->origDef->scope:""))
	{
		char *name = EDECreateUniqueEncounterName(editDoc->def->name);
		editDoc->def->name = (char*)allocAddString(name);
		if (name && editDoc->uiInfo.encounterUI && editDoc->uiInfo.encounterUI->nameEntry)
			ui_EditableSetText(editDoc->uiInfo.encounterUI->nameEntry, name);

		// Update the editor manager and ui related fields
		EDENameChangedEM(editDoc->def->name, editDoc);
	} 

	// Fixup the encounter's messages
	EDEFixupEncounterMessages(editDoc->def);

	resSetDictionaryEditMode(g_EncounterDictionary, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Aquire the lock
	if (!resGetLockOwner(g_EncounterDictionary, encDef->name)) {
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(editDoc->emDoc.editor, encDef->name, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(g_EncounterDictionary, encDef->name, encDef);
		return EM_TASK_INPROGRESS;
	}
	// Get here if have the lock

	// Send save to server
	emSetResourceStateWithData(editDoc->emDoc.editor, encDef->name, EMRES_STATE_SAVING, editDoc);
	resRequestSaveResource(g_EncounterDictionary, encDef->name, encDef);
	return EM_TASK_INPROGRESS;
}

/*
static void EDESaveEncounterAsConfirm(const char *dir, const char *file, EncounterEditDoc* encDoc)
{
	EncounterDef* encDef = encDoc->def;
	char fileName[MAX_PATH];

	sprintf(fileName, "%s/%s", dir, file);
	encDef->filename = allocAddFilename(filename);
	GESetDocFileEM(&encDoc->emDoc, fileName);

	EDESaveEncounter(encDoc);
	GECheckoutDocEM(&encDoc->emDoc);
}

void EDESaveEncounterAs(EncounterEditDoc* encDoc)
{
	EncounterDef* encDef = encDoc->def;
	UIWindow *browser;
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];

	fileLocateWrite(encDef->filename, startDir);
	fileLocateWrite("defs/encounters", topDir);
	backSlashes(startDir);
	backSlashes(topDir);
	getDirectoryName(startDir);
	browser = ui_FileBrowserCreate("Save encounter as", "Save", UIBrowseNew, UIBrowseFiles,
		topDir, startDir, ".encounter", NULL, NULL, EDESaveEncounterAsConfirm, encDoc);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
}
*/

void EDEProcessInput(EncounterEditDoc* encDoc)
{
	EncounterDef* def = encDoc->def;
	GEPlacementTool* placementTool = &encDoc->placementTool;

	// This should be done first before we handle the state of the placement tool
	GEPlacementToolUpdate(placementTool);

	// Handle a marquee selection searching only for actors and named points
	if (placementTool->processMarqueeSelect)
	{
		Mat4 actorMat, camMat;
		int i, n = eaSize(&def->actors);

		// Clear the current selected list if not using additive(alt)
		if (!placementTool->useAdditiveSelect)
			eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);

		// Now project all actors to the screen to see if they are within the marquee selection
		gfxGetActiveCameraMatrix(camMat);
		for (i = 0; i < n; i++)
		{
			Vec3 actorBL, actorTR;
			Vec3 actorCenter;
			OldActor* actor = def->actors[i];
			GEFindActorMat(actor, encDoc->encCenter, actorMat);
			copyVec3(actorMat[3], actorCenter);
			actorCenter[1] += 5.0; // estimate the center
			GEFindScreenCoords(g_GEDisplayDefs.actDispDef, actorMat, placementTool->scrProjMat, actorBL, actorTR);
			gfxGetActiveCameraMatrix(camMat);
			if (GEIsLegalMargqueeSelection(actorCenter, camMat) && boxBoxCollision(actorBL, actorTR, placementTool->mouseMin, placementTool->mouseMax))
				EDESelectActor(encDoc, i, true);
		}

		// Do the same for named points
		n = eaSize(&def->namedPoints);
		for (i = 0; i < n; i++)
		{
			Vec3 screenBL, screenTR;
			Vec3 local_min, local_max;
			Mat4 pointMat;
			OldNamedPointInEncounter* point = def->namedPoints[i];
			int c;

			for(c=0; c<3; c++)	// Bounding box is ugly here
			{
				local_min[c] = -0.7;
				local_max[c] = 0.7;
			}

			GEFindEncPointMat(point, encDoc->encCenter, pointMat);
			GEFindScreenCoordsEx(local_min, local_max, pointMat, placementTool->scrProjMat, screenBL, screenTR);
			if (GEIsLegalMargqueeSelection(pointMat[3], camMat) && boxBoxCollision(screenBL, screenTR, placementTool->mouseMin, placementTool->mouseMax) && !point->frozen)
			{
				EDESelectNamedPoint(encDoc, i, true);
			}
		}

		placementTool->processMarqueeSelect = 0;
		EDEActorPropUIRefresh(encDoc, true);
		EDENamedPointUIRefresh(encDoc);
	}

	// Currently placing something using quickrotate and no longer dragging or rotating means paste
	if (GEPlacementToolIsInPlacementMode(placementTool) && !placementTool->useGizmos && !placementTool->isQuickPlacing && !placementTool->isRotating && !placementTool->createNew)
		EDEPasteSelected(encDoc);
}

void EDEDrawEncounter(EncounterEditDoc* encDoc)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	EncounterDef* def = encDoc->def;
	int* selActorIndexList = EDEGetSelectedActorIndexList(encDoc);
	int* selPointIndexList = EDEGetSelectedNamedPointIndexList(encDoc);
	Mat4 camMat;
	F32 distToEncSquared = 0.0f;
	gfxGetActiveCameraMatrix(camMat);
	distToEncSquared = distance3Squared(camMat[3], encDoc->encCenter[3]);

	// Draw the currently loaded encounter
	GEDrawEncounter(def, encDoc->encCenter, selActorIndexList, selPointIndexList, false, false, false, false, encDoc->activeTeamSize, false, false);

	// We are moving the origin, display things differently
	if (GEPlacementToolIsInPlacementMode(placementTool))
	{
		if (placementTool->moveSelected)
		{
			Mat4 actorMat;
			bool makeCopy = GEPlacementToolShouldCopy(placementTool);
			OldActor** actorList = EDEGetSelectedActorList(encDoc);
			int* pointList = EDEGetSelectedNamedPointIndexList(encDoc);
			int i, n = eaSize(&actorList);
			for (i = 0; i < n; i++)
			{
				OldActor* actor = actorList[i];
				GEFindActorMat(actor, encDoc->encCenter, actorMat);
				GEPlacementToolApply(placementTool, actorMat);
				GEDrawActor(actor, actorMat, true, makeCopy ? GE_COPY_COLOR : GE_MOVE_COLOR, encDoc->activeTeamSize, false, distToEncSquared);
			}
			n = eaiSize(&pointList);
			for (i = 0; i < n; i++)
			{
				int pointIndex = pointList[i];
				if (encDoc->def->namedPoints && (pointIndex < eaSize(&encDoc->def->namedPoints))) 
				{
					OldNamedPointInEncounter* point = encDoc->def->namedPoints[pointIndex];
					Mat4 pointMat;

					GEFindEncPointMat(point, encDoc->encCenter, pointMat);
					GEPlacementToolApply(placementTool, pointMat);
					GEDrawPoint(pointMat, true, makeCopy ? GE_COPY_COLOR : GE_MOVE_COLOR);
				}
			}
		}
		else if (placementTool->moveOrigin)
		{
			Mat4 encCenterMat;
			copyMat4(encDoc->encCenter, encCenterMat);

			// Draw a red bounding box over the old encounter center
			gfxDrawBox3D(g_GEDisplayDefs.encDispDef->bounds.min, g_GEDisplayDefs.encDispDef->bounds.max, encCenterMat, GE_SEL_COLOR, 5);

			// Apply the placement tool, but reset the rotation as it has no meaning in relative space
			GEPlacementToolApply(placementTool, encCenterMat);
			copyMat3(encDoc->encCenter, encCenterMat);

			// Draw another center icon and a green bounding box around the potential new location
			worldAddTempGroup(g_GEDisplayDefs.encDispDef, encCenterMat, NULL, false);
			gfxDrawBox3D(g_GEDisplayDefs.encDispDef->bounds.min, g_GEDisplayDefs.encDispDef->bounds.max, encCenterMat, GE_MOVE_COLOR, 5);
		}
		else if (placementTool->createNew)
		{
			Mat4 actorMat;
			copyMat4(encDoc->encCenter, actorMat);
			GEPlacementToolApply(placementTool, actorMat);
			if(encDoc->typeToCreate == GEObjectType_Actor)
				GEDrawActor(NULL, actorMat, true, GE_COPY_COLOR, encDoc->activeTeamSize, false, distToEncSquared);
			else if(encDoc->typeToCreate == GEObjectType_Point)
				GEDrawPoint(actorMat, true, GE_COPY_COLOR);
		}
	}
}

void EDEImportEncounterDef(EncounterDef* def, EncounterDef* importDef)
{
	char* defName = (char*)allocAddString(def->name);
	const char* filename = allocAddString(def->filename);
	StructCopyAll(parse_EncounterDef, importDef, def);
	def->name = defName;
	def->filename = filename;
}

void EDEPlaceObject(EncounterEditDoc* encDoc, const char* name, const char* type)
{
	GEPlacementTool* placementTool = &encDoc->placementTool;
	if (!GEPlacementToolIsInPlacementMode(placementTool))
	{
		// Reset old placement fields
		GEPlacementToolReset(placementTool, encDoc->encCenter[3], zerovec3);
		REMOVE_HANDLE(encDoc->newCritter);
		placementTool->useGizmos = 0;

		// Setup the needed fields based on what type is being placed
		if (!stricmp(type, "CritterDef"))
		{
			SET_HANDLE_FROM_STRING(g_hCritterDefDict, name, encDoc->newCritter);
			placementTool->createNew = 1;
		}
		else if (!stricmp(type, "EncounterDef"))
		{
			EncounterDef* importDef = oldencounter_DefFromName((char*)name);
			if (importDef && (importDef != encDoc->def) && (UIYes == ui_ModalDialog("Confirm Encounter Import", "Are you sure you want to import an encounter? (This will clear all information currently stored in this encounter)", ColorBlack, UIYes | UINo)))
			{
				EDEImportEncounterDef(encDoc->def, importDef);
				eaDestroyEx(&encDoc->selectedObjects, GESelectedObjectDestroyCB);
				GEEncounterPropUIRefreshSingle(encDoc->uiInfo.encounterUI, encDoc->def, NULL, NULL, true);
				EDEActorPropUIRefresh(encDoc, true);
				EDENamedPointUIRefresh(encDoc);
				GESetDocUnsaved(encDoc);
			}
		}
	}
}


static void EDEInputBeginQuickRotate(GEPlacementTool* placementTool, bool startDrag)
{
	placementTool->useRotateMovement = !!startDrag;
	if (placementTool->isQuickPlacing && startDrag && GEFindMouseLocation(placementTool, placementTool->rotMousePos))
	{
		int tmpX;
		mousePos(&tmpX, &placementTool->qrMy);
		placementTool->isRotating = 1;
	}
	if (!startDrag && placementTool->isRotating)
	{
		placementTool->isRotating = 0;
	}
}

void EDEPlacementBore(GEPlacementTool* placementTool, bool isDown)
{
	placementTool->useBoreSelect = !!isDown;
}

void EDEPlacementAdditive(GEPlacementTool* placementTool, bool isDown)
{
	placementTool->useAdditiveSelect = !!isDown;
}

void EDEPlacementToggleGizmo(GEPlacementTool* placementTool, bool isDown)
{
	if (placementTool->useGizmos && isDown)
	{
		placementTool->useRotateTool ^= 1;
		TranslateGizmoSetMatrix(placementTool->transGizmo, placementTool->gizmoMat);
		RotateGizmoSetMatrix(placementTool->rotGizmo, placementTool->gizmoMat);
	} else if (!placementTool->useGizmos)
		EDEInputBeginQuickRotate(placementTool, isDown);
}

void EDEPlacementToggleCopy(GEPlacementTool* placementTool, bool isDown)
{
	placementTool->useCopySelect = !!isDown;
}

void EDEInputLeftDrag(GEEditorDocPtr editorDoc, GEPlacementTool* placementTool, bool startDrag, EDEEnableQPFunc enableQPFunc)
{
	if (startDrag)
	{
		if (!GEPlacementToolIsInPlacementMode(placementTool))
		{
			// We aren't moving anything, try to begin quickplace mode
			// Find out if there is an object under the mouse, if there is, begin movement
			// If there is not, instead begin marquee selection
			if (enableQPFunc(editorDoc))
			{
				placementTool->isQuickPlacing = 1;
				mouseDownPos(MS_LEFT, &placementTool->qpMx, &placementTool->qpMy);		
				if (placementTool->useRotateMovement)
					EDEInputBeginQuickRotate(placementTool, true);
			}
			else
			{
				mouseDownPos(MS_LEFT, &placementTool->marqueeMx, &placementTool->marqueeMy);
				placementTool->inMarqueeSelect = 1;
			}
		}
	}
	else
	{
		if (GEPlacementToolIsInPlacementMode(placementTool))
		{
			if (!placementTool->useGizmos)
			{
				placementTool->isQuickPlacing = 0;
			}
		}
		else if (placementTool->inMarqueeSelect)
		{
			int mx, my, scrWd, scrHt;
			mousePos(&mx, &my);
			if (placementTool->marqueeMx != mx || placementTool->marqueeMy != my)
			{
				Mat44 viewMat;
				GfxCameraView* cam = gfxGetActiveCameraView();
				gfxGetActiveSurfaceSize(&scrWd, &scrHt);

				// Calculate the matrix to project the actors to 2d screenspace
				mat43to44(cam->frustum.viewmat, viewMat);
				mulMat44Inline(cam->projection_matrix, viewMat, placementTool->scrProjMat);

				// Create a box based on mouse input; 0 the z value
				placementTool->mouseMin[0] = min(mx, placementTool->marqueeMx);
				placementTool->mouseMin[1] = min(my, placementTool->marqueeMy); 
				placementTool->mouseMax[0] = max(mx, placementTool->marqueeMx);
				placementTool->mouseMax[1] = max(my, placementTool->marqueeMy);
				placementTool->mouseMin[2] = placementTool->mouseMax[2] = 0;

				// Enable the marquee select which the logic of should be handled elsewhere
				placementTool->processMarqueeSelect = 1;
			}
			placementTool->inMarqueeSelect = 0;
		}
	}
}

#endif