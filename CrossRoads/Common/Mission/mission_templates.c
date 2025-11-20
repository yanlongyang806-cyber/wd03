/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "mission_common.h"
#include "mission_common_h_ast.h"
#include "rewardCommon.h"
#include "GameEvent.h"
#include "GameEvent_h_ast.h"
#include "EString.h"
#include "StringCache.h"
#include "fileUtil.h"
#include "FolderCache.h"

#include "ResourceManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void ClearMissionDefForTemplate(MissionDef* def)
{
	const char* name = def->name;
	const char* filename = def->filename;
	MissionTemplate* missionTemplate = def->missionTemplate;
	const char* pchRefString = def->pchRefString;
	MissionType type = def->missionType;
	bool repeatable = def->repeatable;
	bool needsReturn = def->needsReturn;
	REF_TO(MissionDef) parentDef;
	DisplayMessage displayNameMsg;
	DisplayMessage detailStringMsg;
	DisplayMessage summaryMsg;
	DisplayMessage uiStringMsg;
	COPY_HANDLE(parentDef, def->parentDef);

	COPY_HANDLE(displayNameMsg.hMessage, def->displayNameMsg.hMessage);
#ifndef NO_EDITORS
	displayNameMsg.pEditorCopy = def->displayNameMsg.pEditorCopy;
	def->displayNameMsg.pEditorCopy = NULL;
#endif

	COPY_HANDLE(detailStringMsg.hMessage, def->detailStringMsg.hMessage);
#ifndef NO_EDITORS
	detailStringMsg.pEditorCopy = def->detailStringMsg.pEditorCopy;
	def->detailStringMsg.pEditorCopy = NULL;
#endif

	COPY_HANDLE(summaryMsg.hMessage, def->summaryMsg.hMessage);
#ifndef NO_EDITORS
	summaryMsg.pEditorCopy = def->summaryMsg.pEditorCopy;
	def->summaryMsg.pEditorCopy = NULL;
#endif

	COPY_HANDLE(uiStringMsg.hMessage, def->uiStringMsg.hMessage);
#ifndef NO_EDITORS
	uiStringMsg.pEditorCopy = def->uiStringMsg.pEditorCopy;
	def->uiStringMsg.pEditorCopy = NULL;
#endif

	def->name = NULL;
	def->filename = NULL;
	def->missionTemplate = NULL;
	def->pchRefString = NULL;

	StructReset(parse_MissionDef, def);

	def->name = name;
	def->filename = filename;
	def->missionTemplate = missionTemplate;
	def->pchRefString = pchRefString;
	def->missionType = type;
	def->repeatable = repeatable;
	def->needsReturn = needsReturn;
	COPY_HANDLE(def->parentDef, parentDef);
	REMOVE_HANDLE(parentDef);

	COPY_HANDLE(def->displayNameMsg.hMessage, displayNameMsg.hMessage);
	REMOVE_HANDLE(displayNameMsg.hMessage);
#ifndef NO_EDITORS
	def->displayNameMsg.pEditorCopy = displayNameMsg.pEditorCopy;
#endif

	COPY_HANDLE(def->detailStringMsg.hMessage, detailStringMsg.hMessage);
	REMOVE_HANDLE(detailStringMsg.hMessage);
#ifndef NO_EDITORS
	def->detailStringMsg.pEditorCopy = detailStringMsg.pEditorCopy;
#endif

	COPY_HANDLE(def->summaryMsg.hMessage, summaryMsg.hMessage);
	REMOVE_HANDLE(summaryMsg.hMessage);
#ifndef NO_EDITORS
	def->summaryMsg.pEditorCopy = summaryMsg.pEditorCopy;
#endif

	COPY_HANDLE(def->uiStringMsg.hMessage, uiStringMsg.hMessage);
	REMOVE_HANDLE(uiStringMsg.hMessage);
#ifndef NO_EDITORS
	def->uiStringMsg.pEditorCopy = uiStringMsg.pEditorCopy;
#endif
}

static TemplateVariableGroup* CreateTemplateVariableGroup(const char* groupName, bool startsOpen, bool hidden)
{
	TemplateVariableGroup* tempGroup = StructCreate(parse_TemplateVariableGroup);
	tempGroup->groupName = StructAllocString(groupName);
	tempGroup->hidden = hidden;
	tempGroup->startsOpen = startsOpen;
	return tempGroup;
}
TemplateVariableGroup* AddTemplateVariableGroup(TemplateVariableGroup* parentGroup, const char* groupName, bool startsOpen, bool hidden)
{
	TemplateVariableGroup* tempGroup = CreateTemplateVariableGroup(groupName, startsOpen, hidden);
	eaPush(&parentGroup->subGroups, tempGroup);
	return tempGroup;
}

static void AddTemplateVariable(TemplateVariableGroup* varGroup, const char* varName, TemplateVariableType type, MultiVal* value)
{
	TemplateVariable* templateVar;
	int i, n = eaSize(&varGroup->variables);

	// Don't add the same variable twice
	for(i=0; i<n; i++)
	{
		TemplateVariable* var = varGroup->variables[i];
		if(0 == stricmp(varName, var->varName))
			return;
	}

	templateVar = StructCreate(parse_TemplateVariable);
	templateVar->varName = allocAddString(varName);
	templateVar->varType = type;
	if(value)
		MultiValCopy(&templateVar->varValue, value);
	eaPush(&varGroup->variables, templateVar);
}

// Variables that are in every template
static void AddBoilerplateVariables(TemplateVariableGroup* rootVarGroup)
{
	MultiVal defaultValue = {0};

	rootVarGroup->groupName = StructAllocString("Template Variables");
	MultiValSetInt(&defaultValue, 1);
	AddTemplateVariable(rootVarGroup, "Level", TemplateVariableType_Int, &defaultValue);

//	AddTemplateVariable(rootVarGroup, "DisplayName", TemplateVariableType_Message, NULL);
//	AddTemplateVariable(rootVarGroup, "Details", TemplateVariableType_Message, NULL);

	// UI String is almost always the same as the display name
//	AddTemplateVariable(&type->variables, "UIString", TemplateVariableType_String, NULL);
	// Grant When Complete worked once but isn't used by any templates currently
//	AddTemplateVariable(varList, "Grant When Complete", TemplateVariableType_Mission, NULL);
}

MissionTemplate* missiontemplate_CreateNewTemplate(MissionTemplateType* tempType, int level, const char* mapName)
{
	MissionTemplate* returnTemplate = StructCreate(parse_MissionTemplate);
	TemplateVariable* levelVar, *mapVar;

	devassert(tempType);

	returnTemplate->rootVarGroup = StructClone(parse_TemplateVariableGroup, &tempType->rootVarGroup);

	// If the template cares about level or map and one was passed in, set that variable
	if(level > 0 && (levelVar = missiontemplate_LookupTemplateVar(returnTemplate, "Level")))
	{
		MultiValSetInt(&levelVar->varValue, level);
	}
	if(mapName && (mapVar = missiontemplate_LookupTemplateVar(returnTemplate, "Map")))
	{
		MultiValSetString(&mapVar->varValue, mapName);
	}

	returnTemplate->templateTypeName = (char*)allocAddString(tempType->templateName);

	return returnTemplate;
}
static void BoilerplateCB(MissionDef* def, MissionDef* parentDef)
{
	MissionTemplate* missionTemplate = def->missionTemplate;
	TemplateVariable* templateVar = NULL;
	int tempInt;
	const char* tempString = NULL;

	// Clear the mission def
	ClearMissionDefForTemplate(def);

	// Set level
	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Level");
	tempInt = templateVar ? MultiValGetInt(&templateVar->varValue, NULL) : 1;
	if(tempInt < 1 || tempInt > 50)
		ErrorFilenamef(def->filename, "Template level out of bounds");
	def->levelDef.missionLevel = tempInt;

	// Set flavor text
	// This text is on the base Mission now, not a template var
/*
	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "DisplayName");
	tempString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;
	if(tempString)
	{
		REMOVE_HANDLE(def->displayNameMsg.hMessage);
		SET_HANDLE_FROM_STRING(gMessageDict, tempString, def->displayNameMsg.hMessage);
	}

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Details");
	tempString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;
	if(tempString)
	{
		REMOVE_HANDLE(def->detailStringMsg.hMessage);
		SET_HANDLE_FROM_STRING(gMessageDict, tempString, def->detailStringMsg.hMessage);
	}
*/

	// If there's a UI string specified, use it.  Otherwise, use the display name
//	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "UIString");
//	tempString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;
//	if(tempString)
//		def->uiString = StructAllocString(tempString);
//	else if(def->displayName)
//	def->uiString = StructAllocString(def->displayName);
//	if (IS_HANDLE_ACTIVE(def->displayNameMsg.hMessage))
//		COPY_HANDLE(def->uiStringMsg.hMessage, def->displayNameMsg.hMessage);

	// Add floaters when the mission is granted and completed
	/*
	if(def->uiString)
	{
		GameAction *action = StructCreate(parse_GameAction);
		MultiVal *textVar = MultiValCreate();
		MultiVal *colorVar = MultiValCreate();
		char buf[256];
		Vec3 color;

		sprintf(buf, "Granted: %s", def->uiString);
		MultiValSetString(textVar, buf);
		setVec3(color, 0, 0.8, 0);
		MultiValSetVec3(colorVar, &color);

		action->type = MissionActionType_SendFloater;
		eaPush(&action->params, textVar);
		eaPush(&action->params, colorVar);
		eaPush(&def->meOnStartActions, action);
	}
	if(def->uiString)
	{
		GameAction *action = StructCreate(parse_GameAction);
		MultiVal *textVar = MultiValCreate();
		MultiVal *colorVar = MultiValCreate();
		char buf[256];
		Vec3 color;

		sprintf(buf, "Complete: %s", def->uiString);
		MultiValSetString(textVar, buf);
		setVec3(color, 0, 0, 0.8);
		MultiValSetVec3(colorVar, &color);

		action->type = MissionActionType_SendFloater;
		eaPush(&action->params, textVar);
		eaPush(&action->params, colorVar);
		eaPush(&def->meSuccessActions, action);
	}
	*/
	

	// Grant when complete: mission to grant when complete
	// Not currently used by any templates
/*	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Grant When Complete");
	tempString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;
	if(tempString)
	{
		// Create a new mission action
		GameAction *action = StructCreate(parse_GameAction);
		MultiVal *val = MultiValCreate();
		MultiValSetString(val, tempString);

		action->type = MissionActionType_GrantMission;
		eaPush(&action->params, val);

		eaPush(&def->meSuccessActions, action);
	}
	*/
}
void TextOnlyTemplateRegister()
{
	if(NULL == RefSystem_ReferentFromString(g_MissionTemplateTypeDict, "Text Only"))
	{
		MissionTemplateType* type = StructCreate(parse_MissionTemplateType);
		type->CBFunc = BoilerplateCB;
		type->templateName = (char*)allocAddString("Text Only");

		AddBoilerplateVariables(&type->rootVarGroup);

		RefSystem_AddReferent(g_MissionTemplateTypeDict,type->templateName,type);
		eaPush(&g_MissionTemplateTypeList, type);
	}
}

static void CollectFromCritterGroupCB(MissionDef* def, MissionDef* parentDef)
{
	MissionTemplate* missionTemplate = def->missionTemplate;
	TemplateVariable* templateVar = NULL;
	const char* mapString, *critterDef, *critterGroup, *itemName;
	int numToCollect;

	// Call boilerplate callback first.  This clears the mission def.
	BoilerplateCB(def, parentDef);

	// Kill-task specific variables
	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Map");
	mapString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Critter Def");
	critterDef = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Critter Group");
	critterGroup = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Item");
	itemName = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Num To Collect");
	numToCollect = templateVar ? MultiValGetInt(&templateVar->varValue, NULL) : 0;

	if(numToCollect <= 0)
		numToCollect = 1;

	// Simple error checking.  TODO: Should be a callback per type
	if(!critterDef && !critterGroup)
	{
		ErrorFilenamef(def->filename, "Collect task template doesn't have critter def or critter group");
	}
	else if(critterDef && critterGroup)
	{
		ErrorFilenamef(def->filename, "Collect task has both critter def and critter group; it should only have one");
	}
	else if(!itemName)
	{
		ErrorFilenamef(def->filename, "Collect task has no item name; it should have one");
	}

	if(critterDef || critterGroup)
	{
		MissionDrop *missionDrop = StructCreate(parse_MissionDrop);
		Reward *reward = NULL;
		MissionEditCond* succCond = StructCreate(parse_MissionEditCond);
		char exprBuffer[2048];
		if(numToCollect > 1)
			succCond->showCount = MDEShowCount_Show_Count;
		succCond->type = MissionCondType_Expression;

		sprintf(exprBuffer, "PlayerItemCount(\"%s\") >= %d", itemName, numToCollect);
		succCond->valStr = StructAllocString(exprBuffer);

		def->meSuccessCond = succCond;

		if (critterDef)
		{
			missionDrop->type = MissionDropTargetType_Critter;
			missionDrop->value = (char*)allocAddString(critterDef);
		}
		else if (critterGroup)
		{
			missionDrop->type = MissionDropTargetType_Group;
			missionDrop->value = (char*)allocAddString(critterGroup);
		}

		//!!!!  hack to force a single item reward
		//!!!!  special reward table name __itemdrop_%_itemname
		//!!!!  template code should probably pass in a reward table name instead of item name
		{
			char * TmpName = NULL;
			int DropPercentage = 50;

			estrStackCreate(&TmpName);
			estrPrintf(&TmpName, "__itemdrop_%d_%s", DropPercentage, itemName);

			missionDrop->RewardTableName = StructAllocString(TmpName);

			estrDestroy(&TmpName);
		}

		if (!def->params)
			def->params = StructCreate(parse_MissionDefParams);
		eaPush(&def->params->missionDrops, missionDrop);
	}
}

// Used by both the kill-specific and kill-general templates
static void KillTaskCB(MissionDef* def, MissionDef* parentDef)
{
	MissionTemplate* missionTemplate = def->missionTemplate;
	TemplateVariable* templateVar = NULL;
	const char* mapString, *critterDef, *critterGroup, *staticEnc, *actorName;
	int numToKill;

	// Call boilerplate callback first.  This clears the mission def.
	BoilerplateCB(def, parentDef);

	// Kill-task specific variables
	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Map");
	mapString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Critter Def");
	critterDef = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Critter Group");
	critterGroup = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Static Encounter");
	staticEnc = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Actor Name");
	actorName = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Num To Kill");
	numToKill = templateVar ? MultiValGetInt(&templateVar->varValue, NULL) : 0;

	if(numToKill <= 0)
		numToKill = 1;


	// Simple error checking.  TODO: Should be a callback per type
	if(!critterDef && !critterGroup && !staticEnc)
	{
		ErrorFilenamef(def->filename, "Kill task template doesn't have critter def, critter group, or static encounter");
	}
	else if(actorName && critterDef)
	{
		ErrorFilenamef(def->filename, "Kill task has both actor name and critter def; it should only have one");
	}
	else if(critterDef && critterGroup)
	{
		ErrorFilenamef(def->filename, "Kill task has both critter def and critter group; it should only have one");
	}

	if(critterDef || critterGroup || staticEnc)
	{
		MissionEditCond* succCond = StructCreate(parse_MissionEditCond);
		GameEvent *killEvent = StructCreate(parse_GameEvent);
		char *eventString = NULL;
		char exprBuffer[2048];
		if(numToKill > 1)
			succCond->showCount = MDEShowCount_Show_Count;
		succCond->type = MissionCondType_Expression;

		// Create the event that this mission will use
		killEvent->type = EventType_Kills;
		killEvent->tMatchSourceTeam = TriState_Yes;
		if(critterDef)
			killEvent->pchTargetCritterName = StructAllocString(critterDef);
 		if(critterGroup)
			killEvent->pchTargetCritterGroupName = StructAllocString(critterGroup);
		if(staticEnc)
			killEvent->pchTargetStaticEncName = StructAllocString(staticEnc);
		if(actorName)
			killEvent->pchTargetActorName = StructAllocString(actorName);
		if(mapString)
			killEvent->pchMapName = allocAddString(mapString);

		estrCreate(&eventString);
		gameevent_WriteEventEscaped(killEvent, &eventString);
		sprintf(exprBuffer, "EventCount(\"%s\") >= %d", eventString, numToKill);

		succCond->valStr = StructAllocString(exprBuffer);

		def->meSuccessCond = succCond;

		estrDestroy(&eventString);
		StructDestroy(parse_GameEvent, killEvent);
	}
}
void KillGeneralTemplateRegister()
{
	const char* name = "Kill a Specific Critter";

	if(NULL == RefSystem_ReferentFromString(g_MissionTemplateTypeDict, name))
	{
		MissionTemplateType* type = StructCreate(parse_MissionTemplateType);
		TemplateVariableGroup* tempGroup;
		MultiVal defaultValue = {0};
		type->CBFunc = KillTaskCB;
		type->templateName = (char*)allocAddString(name);

		AddBoilerplateVariables(&type->rootVarGroup);

		// Kill task variables
		tempGroup = AddTemplateVariableGroup(&type->rootVarGroup, "Kill Task", 1, 0);
		AddTemplateVariable(tempGroup, "Actor Name", TemplateVariableType_String, NULL);
		AddTemplateVariable(tempGroup, "Critter Def", TemplateVariableType_CritterDef, NULL);
		AddTemplateVariable(tempGroup, "Static Encounter", TemplateVariableType_StaticEncounter, NULL);
		AddTemplateVariable(tempGroup, "Map", TemplateVariableType_Map, NULL);

		tempGroup->instructions = StructAllocString("Enter a static encounter name\nand either an actor name or a critter def\n");

		// Hidden variables
		tempGroup = AddTemplateVariableGroup(&type->rootVarGroup, "Hidden", 0, 1);
		MultiValSetInt(&defaultValue, 1);	// Default for kill task
		AddTemplateVariable(tempGroup, "Num To Kill", TemplateVariableType_Int, &defaultValue);

		RefSystem_AddReferent(g_MissionTemplateTypeDict,type->templateName,type);
		eaPush(&g_MissionTemplateTypeList, type);
	}
}
void KillSpecificTemplateRegister()
{
	const char* name = "Kill a Type of Critter";

	if(NULL == RefSystem_ReferentFromString(g_MissionTemplateTypeDict, name))
	{
		MissionTemplateType* type = StructCreate(parse_MissionTemplateType);
		TemplateVariableGroup* tempGroup;
		MultiVal defaultValue = {0};
		type->CBFunc = KillTaskCB;
		type->templateName = (char*)allocAddString(name);

		AddBoilerplateVariables(&type->rootVarGroup);

		// Kill task variables
		tempGroup = AddTemplateVariableGroup(&type->rootVarGroup, "Kill Task", 1, 0);
		MultiValSetInt(&defaultValue, 10);	// Default for kill task
		AddTemplateVariable(tempGroup, "Num To Kill", TemplateVariableType_Int, &defaultValue);
		AddTemplateVariable(tempGroup, "Critter Def", TemplateVariableType_CritterDef, NULL);
		AddTemplateVariable(tempGroup, "Critter Group", TemplateVariableType_CritterGroup, NULL);
		AddTemplateVariable(tempGroup, "Map", TemplateVariableType_Map, NULL);

		tempGroup->instructions = StructAllocString("Fill in a critter def or a critter group (not both!).\nAny kills of that def or group will count toward the mission.");

		RefSystem_AddReferent(g_MissionTemplateTypeDict,type->templateName,type);
		eaPush(&g_MissionTemplateTypeList, type);
	}
}

void CollectItemFromCritterGroupTemplateRegister()
{
	const char* name = "Collect an Item from a Critter";

	if(NULL == RefSystem_ReferentFromString(g_MissionTemplateTypeDict, name))
	{
		MissionTemplateType* type = StructCreate(parse_MissionTemplateType);
		TemplateVariableGroup* tempGroup;
		MultiVal defaultValue = {0};
		type->CBFunc = CollectFromCritterGroupCB;
		type->templateName = (char*)allocAddString(name);

		AddBoilerplateVariables(&type->rootVarGroup);

		// Collection task variables
		tempGroup = AddTemplateVariableGroup(&type->rootVarGroup, "Collect Task", 1, 0);
		MultiValSetInt(&defaultValue, 10);	// Default for kill task
		AddTemplateVariable(tempGroup, "Item", TemplateVariableType_Item, NULL);
		AddTemplateVariable(tempGroup, "Num To Collect", TemplateVariableType_Int, &defaultValue);
		AddTemplateVariable(tempGroup, "Critter Def", TemplateVariableType_CritterDef, NULL);
		AddTemplateVariable(tempGroup, "Critter Group", TemplateVariableType_CritterGroup, NULL);
		AddTemplateVariable(tempGroup, "Map", TemplateVariableType_Map, NULL);
		AddTemplateVariable(tempGroup, "Neighborhood", TemplateVariableType_Neighborhood, NULL);

		tempGroup->instructions = StructAllocString("Fill in a critter def or a critter group (not both!).\nAny critters of that def or group will drop the item.");

		RefSystem_AddReferent(g_MissionTemplateTypeDict,type->templateName,type);
		eaPush(&g_MissionTemplateTypeList, type);
	}
}

// Callback for "Patrol" template that listens for volume entered events
static void PatrolCB(MissionDef* def, MissionDef* parentDef)
{
	MissionTemplate* missionTemplate = def->missionTemplate;
	TemplateVariableGroup* volumeList = NULL;
	TemplateVariable* templateVar = NULL;
	const char* mapString;
	char exprBuffer[4096];	// Could be a long string

	// Values used in loop to add patrol volumes
	const char* volumeName;
	char eventBuffer[1096];	// Buffer for a single volume's addition
	char* estring = NULL;	// Estring for a single event
	GameEvent *enteredEvent = StructCreate(parse_GameEvent);

	int i, numVolumes;

	// Call boilerplate callback first.  This clears the mission def.
	BoilerplateCB(def, parentDef);

	// This template only supports volumes on one map; get the map if there is one
	templateVar = missiontemplate_LookupTemplateVar(missionTemplate, "Map");
	mapString = templateVar ? MultiValGetString(&templateVar->varValue, NULL) : NULL;

	// Error checking
	if(!missionTemplate->rootVarGroup || !missionTemplate->rootVarGroup->subGroups
			|| !missionTemplate->rootVarGroup->subGroups[0]->subGroups)
	{
		ErrorFilenamef(def->filename, "Template isn't a valid patrol template");
		return;
	}
	volumeList = missionTemplate->rootVarGroup->subGroups[0];
	numVolumes = eaSize(&volumeList->subGroups);
	if(numVolumes <= 0)
		ErrorFilenamef(def->filename, "No volumes specified in patrol template");

	// Build the expression string.  It will eventually look like:
	//		((EventCount("volumeEntered.foo")>0) + (EventCount("volumeEntered.bar")>0)) > 2
	strcpy(exprBuffer, "(");

	estrCreate(&estring);

	for(i=0; i<numVolumes; i++)
	{
		// Get the volume name
		templateVar = missiontemplate_LookupTemplateVarInVarGroup(volumeList->subGroups[i], "Volume Name", true);
		if(!templateVar)
		{
			ErrorFilenamef(def->filename, "Volume in patrol list with no name");
			continue;
		}
		volumeName = MultiValGetString(&templateVar->varValue, false);

		// Create a game event string
		// Note that this event doesn't allow team completion; each member of a team must visit the volume themselves
		StructReset(parse_GameEvent, enteredEvent);
		enteredEvent->type = EventType_VolumeEntered;
		enteredEvent->tSourceIsPlayer = TriState_Yes;
		enteredEvent->pchVolumeName = StructAllocString(volumeName);
		if(mapString)
			enteredEvent->pchMapName = allocAddString(mapString);

		gameevent_WriteEventEscaped(enteredEvent, &estring);

		// Create a string for this event countand add it to the buffer
		if(i==0)
			sprintf(eventBuffer, "(EventCount(\"%s\")>0)", estring);
		else
			sprintf(eventBuffer, " + (EventCount(\"%s\")>0)", estring);
		strcat(exprBuffer, eventBuffer);
	}

	sprintf(eventBuffer, ") >= %d", numVolumes);
	strcat(exprBuffer, eventBuffer);

	// Now create a mission complete condition based on this event string
	{
		MissionEditCond* succCond = StructCreate(parse_MissionEditCond);
		succCond->type = MissionCondType_Expression;
		if(numVolumes > 1)
			succCond->showCount = MDEShowCount_Show_Count;

		succCond->valStr = StructAllocString(exprBuffer);
		def->meSuccessCond = succCond;
	}

	if(enteredEvent)
		StructDestroy(parse_GameEvent, enteredEvent);
	if(estring)
		estrDestroy(&estring);

}
void PatrolTemplateRegister()
{
	const char* name = "Enter One or More Volumes";

	if(NULL == RefSystem_ReferentFromString(g_MissionTemplateTypeDict, name))
	{
		MissionTemplateType* type = StructCreate(parse_MissionTemplateType);
		TemplateVariableGroup* tempGroup, *tempGroup2;
		MultiVal defaultValue = {0};
		type->CBFunc = PatrolCB;
		type->templateName = (char*)allocAddString(name);

		AddBoilerplateVariables(&type->rootVarGroup);

		// Patrol task list of volumes
		tempGroup = AddTemplateVariableGroup(&type->rootVarGroup, "Volume List", 1, 0);
		tempGroup->list = true;
		AddTemplateVariable(tempGroup, "Map", TemplateVariableType_Map, NULL);

		// Add the list prototype.  All volumes will share the same variables
		tempGroup2 = CreateTemplateVariableGroup("Volume", 1, 0);
		AddTemplateVariable(tempGroup2, "Volume Name", TemplateVariableType_Volume, NULL);

		tempGroup->listPrototype = tempGroup2;
		tempGroup->instructions = StructAllocString("The player must enter each of the volumes in the list (in any order).\nAll volumes must be on the same map.");

		RefSystem_AddReferent(g_MissionTemplateTypeDict,type->templateName,type);
		eaPush(&g_MissionTemplateTypeList, type);
	}
}

static int missionvartableFixFilenameCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	if (eType == RESVALIDATE_FIX_FILENAME)
	{	
		MissionVarTable *varTable = pResource;
		resFixPooledFilename(&varTable->filename, "defs/MissionVars", NULL, varTable->pchName, "missionvars");
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
int RegisterMissionTemplateDictionaries(void)
{
	g_MissionTemplateTypeDict = RefSystem_RegisterSelfDefiningDictionary("MissionTemplate", false, parse_MissionTemplateType, true, true, NULL);
	g_MissionVarTableDict = RefSystem_RegisterSelfDefiningDictionary("MissionVarTable", false, parse_MissionVarTable, true, true, NULL);

	resDictManageValidation(g_MissionVarTableDict, missionvartableFixFilenameCB);

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_MissionVarTableDict, ".Name", NULL, NULL, NULL, NULL);
		}
		resDictProvideMissingResources(g_MissionVarTableDict);
	} 
	else
	{
		resDictRequestMissingResources(g_MissionVarTableDict, 16, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

static void templateVarTableReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading MissionVarTables...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionaryWithFlags(relpath, g_MissionVarTableDict, PARSER_OPTIONALFLAG))
	{
		ErrorFilenamef(relpath, "Error reloading MissionVarTable file: %s", relpath);
	}

	loadend_printf("done");
}


AUTO_STARTUP(MissionVars) ASTRT_DEPS(RewardsCommon);
int templateLoadAllVarTables(void)
{
	static int loadedOnce = false;

	if (IsServer())
	{
		if (loadedOnce)
		{
			return 1;
		}
		loadstart_printf("Loading MissionVarTables...");

		resLoadResourcesFromDisk(g_MissionVarTableDict, "defs", ".missionvars", "MissionVars.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

		loadedOnce = true;

		if(isDevelopmentMode())
		{
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/*.missionvars", templateVarTableReloadCallback);
		}

		loadend_printf("done");	
	}
	return 1;
}

AUTO_STARTUP(MissionTemplates);
void missiontemplate_RegisterAllTemplateTypes(void)
{
	static bool registered = false;
	if (!registered)
	{
		// Temporary fix for combo boxes not having the "extra" option at the top
		MissionTemplateType* type = StructCreate(parse_MissionTemplateType);
		type->templateName = (char*)allocAddString("No Template");
		eaPush(&g_MissionTemplateTypeList, type);
	//	RefSystem_AddReferent(g_MissionTemplateTypeDict,type->templateName,type);	

		// For now, all templates are hard-coded.  Eventually, we may want to load one or more types of designer-created
		// template files and use them to register new types of templates.
	//	TextOnlyTemplateRegister();	// This template is an example only
		KillGeneralTemplateRegister();
		KillSpecificTemplateRegister();
		CollectItemFromCritterGroupTemplateRegister();
		PatrolTemplateRegister();

		/* for example:
		 * for each defs/missions/*.missionTemplate file
		 *   VariableMissionTemplateStruct* varList = LoadFile(file)
		 *   VariableTemplateTypeRegister(varList)
		 */
		registered = true;
	}
}
