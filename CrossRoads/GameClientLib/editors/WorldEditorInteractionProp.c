#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"
#include "Expression.h"
#include "StringCache.h"
#include "interaction_common.h"

#include "WorldGrid.h"
#include "WorldVariable.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "dynFXInfo.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "soundLib.h"
#include "wlEncounter.h"
#include "ChoiceTable_common.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorVolumeAttributes.h"
#include "wlEditorIncludes.h"
#include "tokenstore.h"
#include "qsortG.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
#define MAX_INTERACTIONS 9
#define MAX_VARIABLES 9
#define MAX_MOVE_DESCRIPTORS 10

#define WLE_AE_INTERACTION_ALIGN_WIDTH 85
#define WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH 125
#define WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH 1.0
#define WLE_AE_INTERACTION_NUM_ENTRY_WIDTH 80
#define WLE_AE_INTERACTION_HEADING 20
#define WLE_AE_INTERACTION_INDENT WLE_AE_INTERACTION_HEADING + 20
#define WLE_AE_INTERACTION_INDENT_LABEL WLE_AE_INTERACTION_INDENT + 20
#define WLE_AE_INTERACTION_INDENT_VALUE WLE_AE_INTERACTION_INDENT + 140

#define wleAEInteractionPropSetupParam(param, fieldName)\
	param.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;\
	param.left_pad = WLE_AE_INTERACTION_INDENT;\
	param.struct_offset = offsetof(GroupDef, property_structs.interaction_properties);\
	param.struct_pti = parse_WorldInteractionProperties;\
	param.struct_fieldname = fieldName

#define wleAEInteractionPropUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def = NULL;\
	WorldInteractionProperties *properties = NULL;\
	assert((obj->type->objType == EDTYPE_ENCOUNTER_ACTOR) || (obj->type->objType == EDTYPE_TRACKER));\
	if(obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)\
	{\
		WorldActorProperties *actor = wleEncounterActorFromHandle(obj->obj, NULL);\
		WleEncObjSubHandle *subHandle = (WleEncObjSubHandle*) obj->obj;\
		tracker = trackerFromTrackerHandle(subHandle->parentHandle);\
		def = tracker ? tracker->def : NULL;\
		properties = actor ? actor->pInteractionProperties : NULL;\
	}\
	else if (obj->type->objType == EDTYPE_TRACKER)\
	{\
		tracker = trackerFromTrackerHandle(obj->obj);\
		def = tracker ? tracker->def : NULL;\
		if (def && def->property_structs.server_volume.interaction_volume_properties)\
			properties = def->property_structs.server_volume.interaction_volume_properties;\
		else\
			properties = def ? def->property_structs.interaction_properties : NULL;\
	}

#define wleAEInteractionPropApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def = NULL;\
	WorldInteractionProperties *properties = NULL;\
	WorldInteractionProperties **propertiesPtr = NULL;\
	bool applyingToVolume = false;\
	assert((obj->type->objType == EDTYPE_ENCOUNTER_ACTOR) || (obj->type->objType == EDTYPE_TRACKER));\
	if(obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)\
	{\
		WleEncObjSubHandle *subHandle = (WleEncObjSubHandle*) obj->obj;\
		WorldEncounterProperties *encounter;\
		WorldActorProperties *actor;\
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
		propertiesPtr = &actor->pInteractionProperties;\
		properties = actor->pInteractionProperties;\
	}\
	else if (obj->type->objType == EDTYPE_TRACKER)\
	{\
		tracker = wleOpPropsBegin(obj->obj);\
		if (!tracker)\
			return;\
		def = tracker ? tracker->def : NULL;\
		if (!def)\
		{\
			wleOpPropsEnd();\
			return;\
		}\
		if ((def->property_structs.volume && !def->property_structs.volume->bSubVolume) && \
			stricmp(wleAEGlobalInteractionPropUI.data.applyAsNode.stringvalue, "Volume") == 0)\
		{\
			properties = def->property_structs.server_volume.interaction_volume_properties;\
			propertiesPtr = &def->property_structs.server_volume.interaction_volume_properties;\
			applyingToVolume = true;\
		}\
		else\
		{\
			properties = def->property_structs.interaction_properties;\
			propertiesPtr = &def->property_structs.interaction_properties;\
		}\
	}

#define wleAEInteractionPropApplyInitAt(i)\
	GroupTracker *tracker;\
	GroupDef *def = NULL;\
	WorldInteractionProperties *properties = NULL;\
	WorldInteractionProperties **propertiesPtr = NULL;\
	bool applyingToVolume = false;\
	assert((objs[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR) || (objs[i]->type->objType == EDTYPE_TRACKER));\
	if(objs[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR)\
	{\
		WleEncObjSubHandle *subHandle = (WleEncObjSubHandle*) objs[i]->obj;\
		WorldEncounterProperties *encounter;\
		WorldActorProperties *actor;\
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
		propertiesPtr = &actor->pInteractionProperties;\
		properties = actor->pInteractionProperties;\
	}\
	else if (objs[i]->type->objType == EDTYPE_TRACKER)\
	{\
		tracker = wleOpPropsBegin(objs[i]->obj);\
		if (!tracker)\
			continue;\
		def = tracker ? tracker->def : NULL;\
		if (!def)\
		{\
			wleOpPropsEndNoUIUpdate();\
			continue;\
		}\
		if ((def->property_structs.volume && !def->property_structs.volume->bSubVolume) && \
		stricmp(wleAEGlobalInteractionPropUI.data.applyAsNode.stringvalue, "Volume") == 0)\
		{\
			properties = def->property_structs.server_volume.interaction_volume_properties;\
			propertiesPtr = &def->property_structs.server_volume.interaction_volume_properties;\
			applyingToVolume = true;\
		}\
		else\
		{\
			properties = def->property_structs.interaction_properties;\
			propertiesPtr = &def->property_structs.interaction_properties;\
		}\
	}

typedef struct WleAEInteractionDoorVarPropUI
{
	WleAEParamWorldVariableDef var;
} WleAEInteractionDoorVarPropUI;

typedef struct WleAEInteractionMoveDescriptorUI
{
	WleAEParamBool specified;
	WleAEParamInt startChildIdx;
	WleAEParamInt destChildIdx;
	WleAEParamVec3 destPos;
	WleAEParamVec3 destRot;
} WleAEInteractionMoveDescriptorUI;

typedef struct WleAEInteractionEntryPropUI
{
	WleAEParamCombo interactionClass;
	
	// Basic properties
	WleAEParamBool isInteract;
	WleAEParamExpression interactCond;
	WleAEParamExpression attemptableCond;
	WleAEParamExpression successCond;
	WleAEParamBool isVisible;
	WleAEParamExpression visibleExpr;
	WleAEParamBool isCategory;
	WleAEParamCombo optActCategory;
	WleAEParamBool exclusive;
	WleAEParamBool autoExec;
	WleAEParamBool disablePowersInterrupt;
	WleAEParamBool allowDuringCombat;
	WleAEParamCombo priority;
	bool bPriorityChanged;
	//WleAEParamText visibleFX;

	// Destructible properties
	WleAEParamDictionary critterDef;
	WleAEParamDictionary critterOverrideDef;
	WleAEParamFloat respawnTime;
	WleAEParamText entityName;
	WleAEParamMessage displayName;
	WleAEParamInt critterLevel;
	WleAEParamDictionary onDeathPower;

	// Contact properties
	WleAEParamBool isContact;
	WleAEParamDictionary contactDef;
	WleAEParamText contactDialog;

	// Crafting station properties
	WleAEParamCombo craftingSkill;
	WleAEParamInt maxSkill;
	WleAEParamDictionary craftRewardDef;
	WleAEParamDictionary deconstructRewardDef;
	WleAEParamDictionary experimentRewardDef;

	// Door properties
	WleAEParamBool isDoor;
	WleAEParamWorldVariableDef doorDest;
	WleAEParamDictionary doorTransition;
	WleAEParamBool perPlayerDoor;
	WleAEParamBool singlePlayerDoor;
	WleAEParamBool includeTeammatesDoor;
	WleAEParamBool collectDestStatus;
    WleAEParamBool destinationSameOwner;
	WleAEParamCombo doorType;
	WleAEParamText doorID;
	WleAEParamDictionary queueDef;
	WleAEParamText doorKey;
	WleAEParamBool doorHasVariables;
	WleAEInteractionDoorVarPropUI **doorVariables;

	// Chair properties
	WleAEParamText chairBitHandlesPre;
	WleAEParamText chairBitHandlesHold;
	WleAEParamFloat chairTimeToMove;
	WleAEParamFloat chairTimePostHold;

	// Gate properties
	WleAEParamExpression critterUseExpr;
	WleAEParamBool volumeTriggered;
	WleAEParamBool startState;

	// FromDefinition properties
	WleAEParamText interactionDef; 
	WleAEParamBool showDefValues;
	bool bShowDefValues;

	// Timing properties
	WleAEParamBool isTiming;
	WleAEParamFloat useTime;
	WleAEParamMessage useTimeText;
	WleAEParamFloat activeTime;
	WleAEParamCombo cooldownType;
	WleAEParamFloat customCooldownTime;
	WleAEParamCombo dynamicCooldownType;
	WleAEParamBool teamUsableWhenActive;
	WleAEParamBool hideDuringCooldown;
	WleAEParamBool interruptOnPower;
	WleAEParamBool interruptOnDamage;
	WleAEParamBool interruptOnMove;
	WleAEParamBool noRespawn;

	// Animation properties
	WleAEParamBool isAnimation;
	WleAEParamDictionary interactAnim;

	// Sound properties
	WleAEParamBool isSound;
	WleAEParamText attemptSound; 
	WleAEParamText successSound; 
	WleAEParamText failureSound; 
	WleAEParamText interruptSound; 
	WleAEParamText movementTransStartSound;
	WleAEParamText movementTransEndSound;
	WleAEParamText movementReturnStartSound;
	WleAEParamText movementReturnEndSound;

	// Motion properties
	WleAEParamBool isMotion;
	WleAEParamFloat transitionTime;
	WleAEParamFloat destinationTime;
	WleAEParamFloat returnTime;
	WleAEParamBool transDuringUse;
	WleAEInteractionMoveDescriptorUI **moveDescriptors;

	// Action properties
	WleAEParamBool isAction;
	WleAEParamExpression attemptExpr;
	WleAEParamExpression successExpr;
	WleAEParamExpression failExpr;
	WleAEParamExpression interruptExpr;
	WleAEParamExpression noLongerActiveExpr;
	WleAEParamExpression cooldownExpr;
	WleAEParamGameAction successActions;
	WleAEParamGameAction failureActions;

	// Text properties
	WleAEParamBool isText;
	WleAEParamMessage usabilityOptionText;
	WleAEParamMessage interactOptionText;
	WleAEParamMessage interactDetailText;
	WleAEParamTexture interactDetailTexture;
	WleAEParamMessage successText;
	WleAEParamMessage failureText;

	// Reward properties
	WleAEParamBool isReward;
	WleAEParamDictionary rewardDef;
	WleAEParamDictionary onceOnlyRewardDef;
	WleAEParamCombo rewardLevelType;
	WleAEParamInt rewardCustomLevel;
	WleAEParamCombo rewardMapVariable;

	// Ambient Job properties
	WleAEParamBool isForCritters;
	WleAEParamBool isForCivilians;
	WleAEParamBool ambientJobInitialJob;
	WleAEParamInt ambientJobPriority;

} WleAEInteractionEntryPropUI;

typedef struct WleAEInteractionPropUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;

	WleAEInteractionEntryPropUI **entries;

	struct 
	{
		// Child geo properties
		WleAEParamInt childSelect;
		WleAEParamExpression childSelectExpr;

		// General property
		WleAEParamCombo applyAsNode;
		WleAEParamBool allowExplicitHide;
		WleAEParamBool startsHidden;

		WleAEParamInt interactDist;
		WleAEParamInt targetDist;
		WleAEParamCombo overrideFX;
		WleAEParamCombo additionalFX;
		WleAEParamBool isUntargetable;
		WleAEParamBool canTabSelect;
		WleAEParamMessage displayNameBasic;
		WleAEParamText interactionTypeTag;
		WleAEParamBool bVisEvalPerEnt;

	} data;

	char **spawnPointNames;
	char **mapNames;
} WleAEInteractionPropUI;


/********************
* TOOLTIP STRINGS
********************/

#define INTERACT_INTERACTCOND_TOOLTIP "When true, the node will be available for interaction.  Otherwise it won't appear to be interactable."
#define INTERACT_ATTEMPTABLECOND_TOOLTIP "When true, the interaction is attemptable as usual and the Success Expr will determine actual success of the attempt.  If false, a different interact FX will be shown, the mouseover tip will be different and interacting will short-circuit (no anim, no timer bar) and fail."
#define INTERACT_SUCCESSCOND_TOOLTIP "When true, the interaction is successful.  Otherwise it will fail."


/********************
* GLOBALS
********************/
static WleAEInteractionPropUI wleAEGlobalInteractionPropUI;


static int wleAEInteractCompareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}

/********************
* PARAMETER CALLBACKS
********************/
void wleAEInteractionPropClassPasteFree(WorldInteractionPropertyEntry *entry)
{
	StructDestroy(parse_WorldInteractionPropertyEntry, entry);
}

void wleAEInteractionPropClassPaste(const EditorObject **objs, WorldInteractionPropertyEntry *entry)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		// Certain interaction classes cannot be set on actors
		if((objs[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR) && entry &&
			(stricmp(entry->pcInteractionClass, "Destructible") == 0 ||
			 stricmp(entry->pcInteractionClass, "NamedObject") == 0  ||
			 stricmp(entry->pcInteractionClass, "Ambientjob") == 0   ||
			 stricmp(entry->pcInteractionClass, "CombatJob") == 0   ||
			 stricmp(entry->pcInteractionClass, "Gate") == 0))
		{
			wleOpPropsEndNoUIUpdate();
			continue;
		}

		// Paste the interaction entry
		if(propertiesPtr)
		{
			if (!(*propertiesPtr))
			{
				(*propertiesPtr) = StructCreate(parse_WorldInteractionProperties);
			}
			eaPush(&((*propertiesPtr)->eaEntries), StructClone(parse_WorldInteractionPropertyEntry, entry));
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static WleAEPasteData *wleAEInteractionPropClassCopy(const EditorObject *obj, void *data)
{
	int index = (intptr_t) data;
	wleAEInteractionPropUpdateInit();

	if(properties && properties->eaEntries && index >= 0 && index < eaSize(&properties->eaEntries))
	{
		WorldInteractionPropertyEntry *entry = StructClone(parse_WorldInteractionPropertyEntry, properties->eaEntries[index]);
		return wleAEPasteDataCreate(entry, wleAEInteractionPropClassPaste, wleAEInteractionPropClassPasteFree);
	}
	else
		return NULL;
}

static void wleAEInteractionPropClassUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();

		if (!properties || !properties->eaEntries || (eaSize(&properties->eaEntries) <= param->index) || !properties->eaEntries[param->index]->pcInteractionClass || !properties->eaEntries[param->index]->pcInteractionClass[0])
		{
			param->stringvalue = NULL;
			param->is_specified = false;
		}
		else
		{
			param->stringvalue = properties->eaEntries[param->index]->pcInteractionClass;
			param->is_specified = true;

			if(!stricmp(param->stringvalue, "FromDefinition")) {
				wleAEGlobalInteractionPropUI.entries[param->index]->interactCond.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->attemptableCond.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->successCond.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->visibleExpr.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->optActCategory.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->priority.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->exclusive.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->autoExec.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->disablePowersInterrupt.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->allowDuringCombat.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->contactDef.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
				wleAEGlobalInteractionPropUI.entries[param->index]->contactDialog.left_pad = WLE_AE_INTERACTION_INDENT_LABEL;
			}
		}
	}
	else
	{
		param->stringvalue = NULL;
		param->is_specified = false;
	}
}

static void wleAEInteractionPropClassApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (!param->stringvalue || param->stringvalue[0] == 0)
		{
			if (param->index < eaSize(&properties->eaEntries))
			{
				// Remove the current entry
				WorldInteractionPropertyEntry *entry;
			
				assert(properties->eaEntries);
				entry = properties->eaEntries[param->index];
				eaRemove(&properties->eaEntries, param->index);
				StructDestroy(parse_WorldInteractionPropertyEntry, entry);

				// If no more entries, then clear root properties
				if (eaSize(&properties->eaEntries) == 0)
				{
					if(propertiesPtr)
					{
						StructDestroySafe(parse_WorldInteractionProperties, propertiesPtr);
					} 
					else
					{
						StructDestroy(parse_WorldInteractionProperties, properties);
					}

					if(objs[i]->type->objType == EDTYPE_TRACKER)
					{
						def->property_structs.physical_properties.bIsChildSelect = 0;
						def->property_structs.physical_properties.iChildSelectIdx = 0;
					}
					groupDefRemoveVolumeType(def, "Interaction");
				}
			}
			wleOpPropsEndNoUIUpdate();
			continue;
		}

		// Create main properties structure if necessary
		if (!properties && propertiesPtr)
		{
			(*propertiesPtr) = StructCreate(parse_WorldInteractionProperties);
			properties = (*propertiesPtr);
			if (applyingToVolume)
			{
				groupDefAddVolumeType(def, "Interaction");
			}
		}
		assert(properties);

		// Create property entries if not present
		while (eaSize(&properties->eaEntries) <= param->index) 
		{
			WorldInteractionPropertyEntry *entry = StructCreate(parse_WorldInteractionPropertyEntry);
			entry->bUseExclusionFlag = 1;
			if (objs[i]->type->objType == EDTYPE_TRACKER)
			{
				entry->bExclusiveInteraction = 1;
			}
			entry->pcInteractionClass = (char*)allocAddString("NamedObject");
			eaPush(&properties->eaEntries, entry);
		}
		assert(properties->eaEntries);

		// Set the interaction class
		properties->eaEntries[param->index]->pcInteractionClass = (char*)allocAddString(param->stringvalue);

		// NOTE: Must update this when deciding on VISIBLE FIELDS
		// Cleanup unneeded data by type
		if (stricmp(param->stringvalue, "Contact") != 0)
		{
			StructDestroySafe(parse_WorldContactInteractionProperties, &properties->eaEntries[param->index]->pContactProperties);
		}
		if (stricmp(param->stringvalue, "CraftingStation") != 0)
		{
			StructDestroySafe(parse_WorldCraftingInteractionProperties, &properties->eaEntries[param->index]->pCraftingProperties);
		}
		if (stricmp(param->stringvalue, "Ambientjob") != 0 || stricmp(param->stringvalue, "CombatJob") != 0)
		{
			StructDestroySafe(parse_WorldAmbientJobInteractionProperties, &properties->eaEntries[param->index]->pAmbientJobProperties);
		}
		if (stricmp(param->stringvalue, "Chair") != 0)
		{
			StructDestroySafe(parse_WorldChairInteractionProperties, &properties->eaEntries[param->index]->pChairProperties);
		}
		if (stricmp(param->stringvalue, "Destructible") != 0)
		{
			StructDestroySafe(parse_WorldDestructibleInteractionProperties, &properties->eaEntries[param->index]->pDestructibleProperties);
		}
		if (stricmp(param->stringvalue, "Door") != 0)
		{
			StructDestroySafe(parse_WorldDoorInteractionProperties, &properties->eaEntries[param->index]->pDoorProperties);
		}
		if (stricmp(param->stringvalue, "Gate") != 0)
		{
			StructDestroySafe(parse_WorldGateInteractionProperties, &properties->eaEntries[param->index]->pGateProperties);
		}
		if (stricmp(param->stringvalue, "Clickable") != 0)
		{
			StructDestroySafe(parse_WorldRewardInteractionProperties, &properties->eaEntries[param->index]->pRewardProperties);
		}
		if (stricmp(param->stringvalue, "FromDefinition") != 0)
		{
			REMOVE_HANDLE(properties->eaEntries[param->index]->hInteractionDef);
			properties->eaEntries[param->index]->bOverrideInteract = false;
			properties->eaEntries[param->index]->bOverrideVisibility = false;
			properties->eaEntries[param->index]->bOverrideCategoryPriority = false;
			wleAEGlobalInteractionPropUI.entries[param->index]->interactCond.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->attemptableCond.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->successCond.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->visibleExpr.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->optActCategory.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->priority.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->exclusive.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->autoExec.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->disablePowersInterrupt.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->allowDuringCombat.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->contactDef.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[param->index]->contactDialog.left_pad = WLE_AE_INTERACTION_INDENT;
		}

		if ((stricmp(param->stringvalue, "Clickable") != 0) &&
			(stricmp(param->stringvalue, "Contact") != 0) &&
			(stricmp(param->stringvalue, "Door") != 0))
		{
			StructDestroySafe(parse_WorldActionInteractionProperties, &properties->eaEntries[param->index]->pActionProperties);
		}
		if ((stricmp(param->stringvalue, "Clickable") != 0) &&
			(stricmp(param->stringvalue, "Contact") != 0) &&
			(stricmp(param->stringvalue, "Door") != 0) &&
			(stricmp(param->stringvalue, "NamedObject") != 0))
		{
			StructDestroySafe(parse_WorldTimeInteractionProperties, &properties->eaEntries[param->index]->pTimeProperties);
		}
		if ((stricmp(param->stringvalue, "Clickable") != 0) &&
			(stricmp(param->stringvalue, "Contact") != 0) &&
			(stricmp(param->stringvalue, "CraftingStation") != 0) &&
			(stricmp(param->stringvalue, "Door") != 0) &&
			(stricmp(param->stringvalue, "FromDefinition") != 0))
		{
			exprDestroy(properties->eaEntries[param->index]->pInteractCond);
			properties->eaEntries[param->index]->pInteractCond = NULL;
			exprDestroy(properties->eaEntries[param->index]->pAttemptableCond);
			properties->eaEntries[param->index]->pAttemptableCond = NULL;
			exprDestroy(properties->eaEntries[param->index]->pSuccessCond);
			properties->eaEntries[param->index]->pSuccessCond = NULL;

			StructDestroySafe(parse_WorldTextInteractionProperties, &properties->eaEntries[param->index]->pTextProperties);
			StructDestroySafe(parse_WorldAnimationInteractionProperties, &properties->eaEntries[param->index]->pAnimationProperties);
		}

		// NOTE: Must update this when deciding on VISIBLE FIELDS
		// Create new data by type
		if (stricmp(param->stringvalue, "Chair") == 0)
		{
			if (!properties->eaEntries[param->index]->pChairProperties)
			{
				properties->eaEntries[param->index]->pChairProperties = StructCreate(parse_WorldChairInteractionProperties);
			}
		}
		else if (stricmp(param->stringvalue, "Contact") == 0)
		{
			if (!properties->eaEntries[param->index]->pContactProperties)
			{
				properties->eaEntries[param->index]->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
			}
		}
		else if (stricmp(param->stringvalue, "CraftingStation") == 0)
		{
			if (!properties->eaEntries[param->index]->pCraftingProperties)
			{
				properties->eaEntries[param->index]->pCraftingProperties = StructCreate(parse_WorldCraftingInteractionProperties);
			}
		}
		else if (stricmp(param->stringvalue, "Destructible") == 0)
		{
			if (!properties->eaEntries[param->index]->pDestructibleProperties)
			{
				properties->eaEntries[param->index]->pDestructibleProperties = StructCreate(parse_WorldDestructibleInteractionProperties);
			}
		}
		else if (stricmp(param->stringvalue, "Door") == 0)
		{
			if (!properties->eaEntries[param->index]->pDoorProperties)
			{
				properties->eaEntries[param->index]->pDoorProperties = StructCreate(parse_WorldDoorInteractionProperties);
			}
		} 
		else if (stricmp(param->stringvalue, "Ambientjob") == 0 || stricmp(param->stringvalue, "CombatJob") == 0)
		{
			if (!properties->eaEntries[param->index]->pAmbientJobProperties)
			{
				properties->eaEntries[param->index]->pAmbientJobProperties = StructCreate(parse_WorldAmbientJobInteractionProperties);
			}
		} 
		else if (stricmp(param->stringvalue, "Gate") == 0)
		{
			if (!properties->eaEntries[param->index]->pGateProperties)
			{
				properties->eaEntries[param->index]->pGateProperties = StructCreate(parse_WorldGateInteractionProperties);
			}
		}
		else if (stricmp(param->stringvalue, "FromDefinition") == 0)
		{
			if( (properties->eaEntries[param->index]->pInteractCond && !exprIsEmpty(properties->eaEntries[param->index]->pInteractCond)) || 
				(properties->eaEntries[param->index]->pAttemptableCond && !exprIsEmpty(properties->eaEntries[param->index]->pAttemptableCond)) ||
				(properties->eaEntries[param->index]->pSuccessCond && !exprIsEmpty(properties->eaEntries[param->index]->pSuccessCond)) )
			{
				properties->eaEntries[param->index]->bOverrideInteract = true;
			}

			if( properties->eaEntries[param->index]->pVisibleExpr && !exprIsEmpty(properties->eaEntries[param->index]->pVisibleExpr) )
			{
				properties->eaEntries[param->index]->bOverrideVisibility = true;
			}

			if( EMPTY_TO_NULL(properties->eaEntries[param->index]->pcCategoryName) || wleAEGlobalInteractionPropUI.entries[param->index]->bPriorityChanged )
			{
				properties->eaEntries[param->index]->bOverrideCategoryPriority = true;
			}

			if( properties->eaEntries[param->index]->pAnimationProperties )
			{
				wleAEGlobalInteractionPropUI.entries[param->index]->isAnimation.boolvalue = true;
			}

			if( properties->eaEntries[param->index]->pTextProperties )
			{
				wleAEGlobalInteractionPropUI.entries[param->index]->isText.boolvalue = true;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsCategoryUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = properties->eaEntries[param->index]->bOverrideCategoryPriority;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsCategoryApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue) 
			{
				// If values are empty and interaction is FromDef, then fill the fields with the def values
				if( (stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0))
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry)
					{
						if(!wleAEGlobalInteractionPropUI.entries[param->index]->bPriorityChanged)
						{
							properties->eaEntries[param->index]->iPriority = pDef->pEntry->iPriority;
						}

						if(!EMPTY_TO_NULL(properties->eaEntries[param->index]->pcCategoryName) || (stricmp(properties->eaEntries[param->index]->pcCategoryName, "None") == 0) )
						{
							properties->eaEntries[param->index]->pcCategoryName = strdup(pDef->pEntry->pcCategoryName);
						}
					}
				}
				properties->eaEntries[param->index]->bOverrideCategoryPriority = true;
			}
			else
			{
				properties->eaEntries[param->index]->bOverrideCategoryPriority = false;
				properties->eaEntries[param->index]->pcCategoryName = NULL;
				properties->eaEntries[param->index]->iPriority = 0;
				wleAEGlobalInteractionPropUI.entries[param->index]->bPriorityChanged = false;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropTypeTagUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties)
		{
			char *estr = NULL;
			int i;
			
			for (i = 0; i < eaSize(&properties->eaInteractionTypeTag); i++)
			{
				if (i)
					estrAppend2(&estr, ",");
				estrAppend2(&estr, properties->eaInteractionTypeTag[i]);
			}
			param->stringvalue = StructAllocString(estr);
			estrDestroy(&estr);
			return; 
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropTypeTagApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties)
		{
			eaDestroy(&properties->eaInteractionTypeTag);
			DivideString(param->stringvalue, ",", &properties->eaInteractionTypeTag,
				DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_ALLOCADD);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsInteractUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = properties->eaEntries[param->index]->bOverrideInteract;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsInteractApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue) 
			{
				properties->eaEntries[param->index]->bOverrideInteract = true;

				// If values are empty and interaction is FromDef, then fill the fields with the def values
				if(stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0)
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry)
					{
						if(pDef->pEntry->pInteractCond && (!properties->eaEntries[param->index]->pInteractCond || exprIsEmpty(properties->eaEntries[param->index]->pInteractCond)) )
						{
							properties->eaEntries[param->index]->pInteractCond = exprClone(pDef->pEntry->pInteractCond);
						}

						if(pDef->pEntry->pAttemptableCond && (!properties->eaEntries[param->index]->pAttemptableCond || exprIsEmpty(properties->eaEntries[param->index]->pAttemptableCond)) )
						{
							properties->eaEntries[param->index]->pAttemptableCond = exprClone(pDef->pEntry->pAttemptableCond);
						}

						if(pDef->pEntry->pSuccessCond && (!properties->eaEntries[param->index]->pSuccessCond || exprIsEmpty(properties->eaEntries[param->index]->pSuccessCond)) )
						{
							properties->eaEntries[param->index]->pSuccessCond = exprClone(pDef->pEntry->pSuccessCond);
						}
					
					}
				}
			}
			else
			{
				properties->eaEntries[param->index]->bOverrideInteract = false;
				if(properties->eaEntries[param->index]->pInteractCond)
				{
					exprDestroy(properties->eaEntries[param->index]->pInteractCond);
					properties->eaEntries[param->index]->pInteractCond = NULL;
				} 
				if (properties->eaEntries[param->index]->pAttemptableCond)
				{
					exprDestroy(properties->eaEntries[param->index]->pAttemptableCond);
					properties->eaEntries[param->index]->pAttemptableCond = NULL;
				}
				if (properties->eaEntries[param->index]->pSuccessCond)
				{
					exprDestroy(properties->eaEntries[param->index]->pSuccessCond);
					properties->eaEntries[param->index]->pSuccessCond = NULL;
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInteractCondUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{		
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pInteractCond);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropInteractCondApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			exprDestroy(properties->eaEntries[param->index]->pInteractCond);
			properties->eaEntries[param->index]->pInteractCond = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropAttemptableCondUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pAttemptableCond);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropAttemptableCondApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			exprDestroy(properties->eaEntries[param->index]->pAttemptableCond);
			properties->eaEntries[param->index]->pAttemptableCond = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropSuccessCondUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pSuccessCond);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropSuccessCondApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			exprDestroy(properties->eaEntries[param->index]->pSuccessCond);
			properties->eaEntries[param->index]->pSuccessCond = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsVisibleUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = properties->eaEntries[param->index]->bOverrideVisibility;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsVisibleApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue) 
			{
				properties->eaEntries[param->index]->bOverrideVisibility = true;

				// If values are empty and interaction is FromDef, then fill the fields with the def values
				if(stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0)
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry && pDef->pEntry->pVisibleExpr &&
						(!properties->eaEntries[param->index]->pVisibleExpr || exprIsEmpty(properties->eaEntries[param->index]->pVisibleExpr)) )
					{
						properties->eaEntries[param->index]->pVisibleExpr = exprClone(pDef->pEntry->pVisibleExpr);
					}
				}

			}
			else
			{
				properties->eaEntries[param->index]->bOverrideVisibility = false;
				exprDestroy(properties->eaEntries[param->index]->pVisibleExpr);
				properties->eaEntries[param->index]->pVisibleExpr = NULL;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();

}

static void wleAEInteractionPropVisibleExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			// If entry's value is empty and the class is FromDef, use the def's value to fill the param.
			if( (!properties->eaEntries[param->index]->pVisibleExpr || exprIsEmpty(properties->eaEntries[param->index]->pVisibleExpr))
				&& (properties->eaEntries[param->index]->bOverrideVisibility && stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0 ) )
			{
				InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
				if(pDef && pDef->pEntry) {
					param->exprvalue = exprClone(pDef->pEntry->pVisibleExpr);
					return;
				}
			} 

			param->exprvalue = exprClone(properties->eaEntries[param->index]->pVisibleExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropVisibleExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			exprDestroy(properties->eaEntries[param->index]->pVisibleExpr);
			properties->eaEntries[param->index]->pVisibleExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropVisibleBoolUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties)
		{
			param->boolvalue = !!properties->bEvalVisExprPerEnt;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropVisibleBoolApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties)
		{
			properties->bEvalVisExprPerEnt = !!param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropOptActCategoryUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (!EMPTY_TO_NULL(properties->eaEntries[param->index]->pcCategoryName)) {
				param->stringvalue = "None";
				return;
			} 

			param->stringvalue = properties->eaEntries[param->index]->pcCategoryName;
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropOptActCategoryApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->stringvalue && (stricmp(param->stringvalue, "None") == 0))
			{
				properties->eaEntries[param->index]->pcCategoryName = NULL;
				wleOpPropsEndNoUIUpdate();
				continue;
			} 
			properties->eaEntries[param->index]->pcCategoryName = allocAddString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropPriorityUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->stringvalue = StaticDefineIntRevLookup(WorldOptionalActionPriorityEnum, properties->eaEntries[param->index]->iPriority);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropPriorityApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			int newvalue = StaticDefineIntGetInt(WorldOptionalActionPriorityEnum, param->stringvalue);
			if (newvalue == -1)
			{
				newvalue = 0;
			}
			if(newvalue != properties->eaEntries[param->index]->iPriority && param->index < eaSize(&wleAEGlobalInteractionPropUI.entries))
			{
				wleAEGlobalInteractionPropUI.entries[param->index]->bPriorityChanged = true;
			}
			properties->eaEntries[param->index]->iPriority = newvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropExclusiveUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = interaction_EntryGetExclusive(properties->eaEntries[param->index], obj->type->objType == EDTYPE_TRACKER);
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropExclusiveApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			properties->eaEntries[param->index]->bExclusiveInteraction = param->boolvalue;
			properties->eaEntries[param->index]->bUseExclusionFlag = 1;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropAutoExecUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = properties->eaEntries[param->index]->bAutoExecute;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropAutoExecApply(WleAEParamBool *param,  void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			properties->eaEntries[param->index]->bAutoExecute = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDisablePowersInterruptUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = properties->eaEntries[param->index]->bDisablePowersInterrupt;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropDisablePowersInterruptApply(WleAEParamBool *param,  void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			properties->eaEntries[param->index]->bDisablePowersInterrupt = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropAllowDuringCombatUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = properties->eaEntries[param->index]->bAllowDuringCombat;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropAllowDuringCombatApply(WleAEParamBool *param,  void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			properties->eaEntries[param->index]->bAllowDuringCombat = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCritterDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pDestructibleProperties->hCritterDef);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropCritterDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("CritterDef", param->refvalue, properties->eaEntries[param->index]->pDestructibleProperties->hCritterDef);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pDestructibleProperties->hCritterDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCritterOverrideDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pDestructibleProperties->hCritterOverrideDef);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropCritterOverrideDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("CritterOverrideDef", param->refvalue, properties->eaEntries[param->index]->pDestructibleProperties->hCritterOverrideDef);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pDestructibleProperties->hCritterOverrideDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropOnDeathPowerUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pDestructibleProperties->hOnDeathPowerDef);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropOnDeathPowerApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("PowerDef", param->refvalue, properties->eaEntries[param->index]->pDestructibleProperties->hOnDeathPowerDef);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pDestructibleProperties->hOnDeathPowerDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropRespawnTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pDestructibleProperties->fRespawnTime;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEInteractionPropRespawnTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			properties->eaEntries[param->index]->pDestructibleProperties->fRespawnTime = param->floatvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCritterLevelUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			param->intvalue = properties->eaEntries[param->index]->pDestructibleProperties->uCritterLevel;
			return;
		}
	}
	param->intvalue = 0;
}

static void wleAEInteractionPropCritterLevelApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			properties->eaEntries[param->index]->pDestructibleProperties->uCritterLevel = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDisplayNameUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pDestructibleProperties->displayNameMsg;
			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}

			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropDisplayNameApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pDestructibleProperties->displayNameMsg;
		
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropEntityNameUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			param->stringvalue = StructAllocString(properties->eaEntries[param->index]->pDestructibleProperties->pcEntityName);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropEntityNameApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDestructibleProperties)
		{
			StructFreeString(properties->eaEntries[param->index]->pDestructibleProperties->pcEntityName);
			properties->eaEntries[param->index]->pDestructibleProperties->pcEntityName = StructAllocString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropContactDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pContactProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pContactProperties->hContactDef);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			} 
		} 
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropContactDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pContactProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("Contact", param->refvalue, properties->eaEntries[param->index]->pContactProperties->hContactDef);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pContactProperties->hContactDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropContactDialogUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pContactProperties)
		{
			param->stringvalue = StructAllocString(properties->eaEntries[param->index]->pContactProperties->pcDialogName);
			return;
		} 
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropContactDialogApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pContactProperties)
		{
			properties->eaEntries[param->index]->pContactProperties->pcDialogName = allocAddString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsDoorUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			param->boolvalue = 1;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsDoorApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue)
			{
				if(!properties->eaEntries[param->index]->pDoorProperties) 
				{
					properties->eaEntries[param->index]->pDoorProperties = StructCreate(parse_WorldDoorInteractionProperties);
				}

				// If entry's value is empty and the class is FromDef, use the def's value to fill the param.
				if( (stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0))
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					StructCopy(parse_WorldDoorInteractionProperties, pDef->pEntry->pDoorProperties, properties->eaEntries[param->index]->pDoorProperties, 0, 0, 0);
				}

			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pDoorProperties)
			{
				StructDestroySafe(parse_WorldDoorInteractionProperties, &properties->eaEntries[param->index]->pDoorProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsContactUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pContactProperties)
		{
			param->boolvalue = 1;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsContactApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue)
			{
				if(!properties->eaEntries[param->index]->pContactProperties) 
				{
					properties->eaEntries[param->index]->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
				}

				// If entry's value is empty and the class is FromDef, use the def's value to fill the param.
				if( (stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0) && !GET_REF(properties->eaEntries[param->index]->pContactProperties->hContactDef) )
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry && pDef->pEntry->pContactProperties)
					{
						COPY_HANDLE(properties->eaEntries[param->index]->pContactProperties->hContactDef, pDef->pEntry->pContactProperties->hContactDef);
						properties->eaEntries[param->index]->pContactProperties->pcDialogName = pDef->pEntry->pContactProperties->pcDialogName;
					}
				}

			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pContactProperties)
			{
				StructDestroySafe(parse_WorldContactInteractionProperties, &properties->eaEntries[param->index]->pContactProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropChairBitHandlesPreUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			char *val = NULL;
			int i;
			for (i = 0; i < eaSize(&properties->eaEntries[param->index]->pChairProperties->eaBitHandlesPre); i++)
			{
				if (i)
					estrConcatf(&val, ",");
				estrConcatf(&val, "%s", properties->eaEntries[param->index]->pChairProperties->eaBitHandlesPre[i]);
			}

			param->stringvalue = StructAllocString(val);
			estrDestroy(&val);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropChairBitHandlesPreApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			char *val = strdup(param->stringvalue);
			char *cursor = val;
			char *handle = NULL;

			eaDestroy(&properties->eaEntries[param->index]->pChairProperties->eaBitHandlesPre);
			while (handle = strsep(&cursor, ","))
			{
				removeLeadingAndFollowingSpaces(handle);
				eaPush(&properties->eaEntries[param->index]->pChairProperties->eaBitHandlesPre, allocAddString(handle));
			}

			free(val);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropChairBitHandlesHoldUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			char *val = NULL;
			int i;
			for (i = 0; i < eaSize(&properties->eaEntries[param->index]->pChairProperties->eaBitHandlesHold); i++)
			{
				if (i)
					estrConcatf(&val, ",");
				estrConcatf(&val, "%s", properties->eaEntries[param->index]->pChairProperties->eaBitHandlesHold[i]);
			}

			param->stringvalue = StructAllocString(val);
			estrDestroy(&val);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropChairBitHandlesHoldApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			char *val = strdup(param->stringvalue);
			char *cursor = val;
			char *handle = NULL;

			eaDestroy(&properties->eaEntries[param->index]->pChairProperties->eaBitHandlesHold);
			while (handle = strsep(&cursor, ","))
			{
				removeLeadingAndFollowingSpaces(handle);
				eaPush(&properties->eaEntries[param->index]->pChairProperties->eaBitHandlesHold, allocAddString(handle));
			}

			free(val);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropChairTimeToMoveUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pChairProperties->fTimeToMove;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEInteractionPropChairTimeToMoveApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			properties->eaEntries[param->index]->pChairProperties->fTimeToMove = param->floatvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropChairTimePostHoldUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pChairProperties->fTimePostHold;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEInteractionPropChairTimePostHoldApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pChairProperties)
		{
			properties->eaEntries[param->index]->pChairProperties->fTimePostHold = param->floatvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropGateCritterUseCondUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pGateProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pGateProperties->pCritterUseCond);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropGateCritterUseCondApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pGateProperties)
		{
			properties->eaEntries[param->index]->pGateProperties->pCritterUseCond = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropGateVolumeTriggeredUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pGateProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pGateProperties->bVolumeTriggered;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropGateVolumeTriggeredApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pGateProperties)
		{
			properties->eaEntries[param->index]->pGateProperties->bVolumeTriggered = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropGateStartStateUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pGateProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pGateProperties->bStartState;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropGateStartStateApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pGateProperties)
		{
			properties->eaEntries[param->index]->pGateProperties->bStartState = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCraftingSkillUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			param->stringvalue = StaticDefineIntRevLookup(WorldSkillTypeEnum, properties->eaEntries[param->index]->pCraftingProperties->eSkillFlags);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropCraftingSkillApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			properties->eaEntries[param->index]->pCraftingProperties->eSkillFlags = StaticDefineIntGetInt(WorldSkillTypeEnum, param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropMaxSkillUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			param->intvalue = properties->eaEntries[param->index]->pCraftingProperties->iMaxSkill;
			return;
		}
	}
	param->intvalue = 0;
}

static void wleAEInteractionPropMaxSkillApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			properties->eaEntries[param->index]->pCraftingProperties->iMaxSkill = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCraftRewardDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pCraftingProperties->hCraftRewardTable);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropCraftRewardDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("RewardTable", param->refvalue, properties->eaEntries[param->index]->pCraftingProperties->hCraftRewardTable);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pCraftingProperties->hCraftRewardTable);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDeconstructRewardDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pCraftingProperties->hDeconstructRewardTable);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropDeconstructRewardDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("RewardTable", param->refvalue, properties->eaEntries[param->index]->pCraftingProperties->hDeconstructRewardTable);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pCraftingProperties->hDeconstructRewardTable);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropExperimentRewardDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pCraftingProperties->hExperimentRewardTable);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropExperimentRewardDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pCraftingProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("RewardTable", param->refvalue, properties->eaEntries[param->index]->pCraftingProperties->hExperimentRewardTable);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pCraftingProperties->hExperimentRewardTable);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDistanceUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	/*
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->intvalue = properties->eaEntries[param->index]->interact_dist;
			return;
		}
	}
	param->intvalue = 0;
	*/

	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		
		if ( properties )
			param->intvalue = properties->uInteractDist;
	}
}

static void wleAEInteractionPropDistanceApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if ( properties )
		{
			properties->uInteractDist = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropTargetDistanceUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		
		if ( properties )
			param->intvalue = properties->uTargetDist;
	}
}

static void wleAEInteractionPropTargetDistanceApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if ( properties )
		{
			properties->uTargetDist = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropOverrideFXUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();

		if (properties)
			param->stringvalue = properties->pchOverrideFX;
	}
}

static void wleAEInteractionPropOverrideFXApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties)
		{
			properties->pchOverrideFX = allocAddString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropAdditionalFXUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();

		if (properties)
			param->stringvalue = properties->pchAdditionalUniqueFX;
	}
}

static void wleAEInteractionPropAdditionalFXApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties)
		{
			properties->pchAdditionalUniqueFX = allocAddString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}
static void wleAEInteractionPropUntargetableUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		
		if ( properties )
			param->boolvalue = !!properties->bUntargetable;
	}
}

static void wleAEInteractionPropUntargetableApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if ( properties )
		{
			properties->bUntargetable = !!param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCanTabSelectUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		
		if ( properties )
			param->boolvalue = !!properties->bTabSelect;
	}
}

static void wleAEInteractionPropCanTabSelectApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if ( properties )
		{
			properties->bTabSelect = !!param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

// MOTION

static void wleAEInteractionPropIsMotionUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			param->boolvalue = !!properties->eaEntries[param->index]->pMotionProperties;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropIsMotionApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			wleAEInteractionPropApplyInitAt(i);

			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
			{
				if (param->boolvalue && !properties->eaEntries[param->index]->pMotionProperties)
				{
					properties->eaEntries[param->index]->pMotionProperties = StructCreate(parse_WorldMotionInteractionProperties);
				}
				else if (!param->boolvalue)
				{
					StructDestroySafe(parse_WorldMotionInteractionProperties, &properties->eaEntries[param->index]->pMotionProperties);
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMotionTransTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pMotionProperties->fTransitionTime;
			return;
		}
	}
	param->floatvalue = 0.0f;
}

static void wleAEInteractionPropMotionTransTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			wleAEInteractionPropApplyInitAt(i);

			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] &&
				properties->eaEntries[param->index]->pMotionProperties)
			{
				properties->eaEntries[param->index]->pMotionProperties->fTransitionTime = param->floatvalue;
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMotionDestTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pMotionProperties->fDestinationTime;
			return;
		}
	}
	param->floatvalue = 0.0f;
}

static void wleAEInteractionPropMotionDestTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			wleAEInteractionPropApplyInitAt(i);

			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
			{
				properties->eaEntries[param->index]->pMotionProperties->fDestinationTime = param->floatvalue;
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMotionReturnTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pMotionProperties->fReturnTime;
			return;
		}
	}
	param->floatvalue = 0.0f;
}

static void wleAEInteractionPropMotionReturnTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			wleAEInteractionPropApplyInitAt(i);

			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
			{
				properties->eaEntries[param->index]->pMotionProperties->fReturnTime = param->floatvalue;
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMotionTransDuringUseUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pMotionProperties->bTransDuringUse;
			return;
		}
	}
	param->boolvalue = 0.0f;
}

static void wleAEInteractionPropMotionTransDuringUseApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			wleAEInteractionPropApplyInitAt(i);

			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pMotionProperties)
			{
				properties->eaEntries[param->index]->pMotionProperties->bTransDuringUse = param->boolvalue;
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMoveSpecifiedUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		int index, subIndex;
		wleAEInteractionPropUpdateInit();

		index = param->index / 1000;
		subIndex = param->index % 1000;
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
		{
			WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
			if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
			{
				param->boolvalue = true;
				return;
			}
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropMoveSpecifiedApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			int index, subIndex;
			wleAEInteractionPropApplyInitAt(i);

			index = param->index / 1000;
			subIndex = param->index % 1000;
			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
			{
				WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
				if (!param->boolvalue && eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
				{
					WorldMoveDescriptorProperties *pMoveDescriptor = eaRemove(&pMotionProps->eaMoveDescriptors, subIndex);
					StructDestroy(parse_WorldMoveDescriptorProperties, pMoveDescriptor);
				}
				else if (param->boolvalue && eaSize(&pMotionProps->eaMoveDescriptors) <= subIndex)
				{
					eaInsert(&pMotionProps->eaMoveDescriptors, StructCreate(parse_WorldMoveDescriptorProperties), subIndex);
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMoveStartChildIdxUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		int index, subIndex;
		wleAEInteractionPropUpdateInit();

		index = param->index / 1000;
		subIndex = param->index % 1000;
		if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
		{
			WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
			if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
			{
				param->intvalue = pMotionProps->eaMoveDescriptors[subIndex]->iStartChildIdx;
				return;
			}
		}
	}
	param->intvalue = -1;
} 

static void wleAEInteractionPropMoveStartChildIdxApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			int index, subIndex;
			wleAEInteractionPropApplyInitAt(i);

			index = param->index / 1000;
			subIndex = param->index % 1000;
			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
			{
				WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
				if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
				{
					pMotionProps->eaMoveDescriptors[subIndex]->iStartChildIdx = param->intvalue;
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMoveDestChildIdxUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		int index, subIndex;
		wleAEInteractionPropUpdateInit();

		index = param->index / 1000;
		subIndex = param->index % 1000;
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
		{
			WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
			if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
			{
				param->intvalue = pMotionProps->eaMoveDescriptors[subIndex]->iDestChildIdx;
				return;
			}
		}
	}
	param->intvalue = -1;
} 

static void wleAEInteractionPropMoveDestChildIdxApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			int index, subIndex;
			wleAEInteractionPropApplyInitAt(i);

			index = param->index / 1000;
			subIndex = param->index % 1000;
			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
			{
				WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
				if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
				{
					pMotionProps->eaMoveDescriptors[subIndex]->iDestChildIdx = param->intvalue;
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMoveDestPosUpdate(WleAEParamVec3 *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		int index, subIndex;
		wleAEInteractionPropUpdateInit();

		index = param->index / 1000;
		subIndex = param->index % 1000;
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
		{
			WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
			if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
			{
				copyVec3(pMotionProps->eaMoveDescriptors[subIndex]->vDestPos, param->vecvalue);
				return;
			}
		}
	}
	zeroVec3(param->vecvalue);
} 

static void wleAEInteractionPropMoveDestPosApply(WleAEParamVec3 *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			int index, subIndex;
			wleAEInteractionPropApplyInitAt(i);

			index = param->index / 1000;
			subIndex = param->index % 1000;
			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
			{
				WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
				if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
				{
					copyVec3(param->vecvalue, pMotionProps->eaMoveDescriptors[subIndex]->vDestPos);
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropMoveDestRotUpdate(WleAEParamVec3 *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		int index, subIndex;
		wleAEInteractionPropUpdateInit();

		index = param->index / 1000;
		subIndex = param->index % 1000;
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
		{
			WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
			if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
			{
				copyVec3(pMotionProps->eaMoveDescriptors[subIndex]->vDestRot, param->vecvalue);
				return;
			}
		}
	}
	zeroVec3(param->vecvalue);
}

static void wleAEInteractionPropMoveDestRotApply(WleAEParamVec3 *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			int index, subIndex;
			wleAEInteractionPropApplyInitAt(i);

			index = param->index / 1000;
			subIndex = param->index % 1000;
			if (properties && properties->eaEntries && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pMotionProperties)
			{
				WorldMotionInteractionProperties *pMotionProps = properties->eaEntries[index]->pMotionProperties;
				if (eaSize(&pMotionProps->eaMoveDescriptors) > subIndex)
				{
					copyVec3(param->vecvalue, pMotionProps->eaMoveDescriptors[subIndex]->vDestRot);
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropDoorDestUpdate(WleAEParamWorldVariableDef *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			WorldDoorInteractionProperties* props = properties->eaEntries[param->index]->pDoorProperties;
			
			param->is_specified = true;
			StructCopyAll(parse_WorldVariableDef, &props->doorDest, &param->var_def);
			param->var_def.eType = WVAR_MAP_POINT;
			if(!param->var_def.pSpecificValue) {
				param->var_def.pSpecificValue = StructCreate(parse_WorldVariable);
				param->var_def.pSpecificValue->eType = WVAR_MAP_POINT;
			}
			
			return;
		}
	}

	StructReset(parse_WorldVariableDef, &param->var_def);
	param->is_specified = false;
}

static void wleAEInteractionPropDoorDestApply(WleAEParamWorldVariableDef *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			WorldDoorInteractionProperties* props = properties->eaEntries[param->index]->pDoorProperties;

			StructCopyAll(parse_WorldVariableDef, &param->var_def, &props->doorDest);
			props->doorDest.eType = WVAR_MAP_POINT;
			if (props->doorDest.pSpecificValue)
			{
				props->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropTransitionOverrideUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		WorldDoorInteractionProperties* pDoor = NULL;
		wleAEInteractionPropUpdateInit();

		if ( properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] )
			pDoor = properties->eaEntries[param->index]->pDoorProperties;

		if ( pDoor )
		{
			param->refvalue = StructAllocString(REF_STRING_FROM_HANDLE(pDoor->hTransSequence));
			return;
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropTransitionOverrideApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if ( properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] )
		{
			WorldDoorInteractionProperties* pDoor = properties->eaEntries[param->index]->pDoorProperties;

			if ( pDoor )
			{
				SET_HANDLE_FROM_STRING( "DoorTransitionSequenceDef", param->refvalue, pDoor->hTransSequence );
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDoorHasVariablesUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties
			&& !param->disabled)
		{
			param->boolvalue = (eaSize(&properties->eaEntries[param->index]->pDoorProperties->eaVariableDefs) > 0);
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropDoorHasVariablesApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			char* pcZoneMap = properties->eaEntries[param->index]->pDoorProperties->doorDest.pSpecificValue->pcZoneMap;
			bool bIsMissionReturn = (stricmp(properties->eaEntries[param->index]->pDoorProperties->doorDest.pSpecificValue->pcStringVal, "MissionReturn") == 0);
			ZoneMapInfo *pZoneMap = NULL;

			if (pcZoneMap && !resHasNamespace(pcZoneMap))
			{
				pZoneMap = worldGetZoneMapByPublicName(pcZoneMap);
			}

			//It is not valid to have door variables when the door leads to the same map, so get rid of any that are there
			if (!param->boolvalue || (!pZoneMap && !bIsMissionReturn) )
			{
				int j;
				for(j = eaSize(&properties->eaEntries[param->index]->pDoorProperties->eaVariableDefs)-1; j >= 0; --j)
				{
					StructDestroy(parse_WorldVariableDef, properties->eaEntries[param->index]->pDoorProperties->eaVariableDefs[i]);
				}
				eaDestroy(&properties->eaEntries[param->index]->pDoorProperties->eaVariableDefs);
			}
			else
			{
				if (!eaSize(&properties->eaEntries[param->index]->pDoorProperties->eaVariableDefs))
				{
					WorldVariableDef *varDef = StructCreate(parse_WorldVariableDef);
					eaPush(&properties->eaEntries[param->index]->pDoorProperties->eaVariableDefs, varDef);
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDoorVarUpdate(WleAEParamWorldVariableDef *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		int index = param->index % 1000;
		int sub_index = param->index / 1000;
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pDoorProperties && (eaSize(&properties->eaEntries[index]->pDoorProperties->eaVariableDefs) > sub_index) && properties->eaEntries[index]->pDoorProperties->eaVariableDefs[sub_index])
		{
			StructCopyAll(parse_WorldVariableDef, properties->eaEntries[index]->pDoorProperties->eaVariableDefs[sub_index],
				&param->var_def);
			param->is_specified = true;
			return;
		}
	}
	StructReset( parse_WorldVariableDef, &param->var_def );
	param->is_specified = false;
}

static void wleAEInteractionPropDoorVarApply(WleAEParamWorldVariableDef *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int index = param->index % 1000;
		int sub_index = param->index / 1000;
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pDoorProperties)
		{
			if (param->is_specified)
			{
				int varNum = 1;
				while (eaSize(&properties->eaEntries[index]->pDoorProperties->eaVariableDefs) <= sub_index) 
				{
					WorldVariableDef *var = StructCreate(parse_WorldVariableDef);
					do 
					{
						char name[32];
						sprintf(name, "New_Variable_#%i", varNum++);
						var->pcName = allocAddString(name);
					} while (!eaPush(&properties->eaEntries[index]->pDoorProperties->eaVariableDefs, var));
				}
				StructDestroySafe( parse_WorldVariableDef, &properties->eaEntries[index]->pDoorProperties->eaVariableDefs[sub_index]);
				properties->eaEntries[index]->pDoorProperties->eaVariableDefs[sub_index]
				= StructClone( parse_WorldVariableDef, &param->var_def );
				worldVariableDefCleanup(properties->eaEntries[index]->pDoorProperties->eaVariableDefs[sub_index]);
			}
			else if (eaSize(&properties->eaEntries[index]->pDoorProperties->eaVariableDefs) > sub_index)
			{
				StructDestroy(parse_WorldVariableDef, properties->eaEntries[index]->pDoorProperties->eaVariableDefs[sub_index]);
				eaRemove(&properties->eaEntries[index]->pDoorProperties->eaVariableDefs, sub_index);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIncludeTeammatesDoorUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pDoorProperties->bIncludeTeammates;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropIncludeTeammatesDoorApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			properties->eaEntries[param->index]->pDoorProperties->bIncludeTeammates = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropPerPlayerDoorUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pDoorProperties->bPerPlayer;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropPerPlayerDoorApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			properties->eaEntries[param->index]->pDoorProperties->bPerPlayer = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropSinglePlayerDoorUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pDoorProperties->bSinglePlayer;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropSinglePlayerDoorApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			properties->eaEntries[param->index]->pDoorProperties->bSinglePlayer = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCollectDestStatusUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pDoorProperties->bCollectDestStatus;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropCollectDestStatusApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			properties->eaEntries[param->index]->pDoorProperties->bCollectDestStatus = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDestinationSameOwnerUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pDoorProperties->bDestinationSameOwner;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEInteractionPropDestinationSameOwnerApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			properties->eaEntries[param->index]->pDoorProperties->bDestinationSameOwner = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDoorTypeUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		int index = param->index % 1000;
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pDoorProperties)
		{
			param->stringvalue = StaticDefineIntRevLookup(WorldDoorTypeEnum, properties->eaEntries[index]->pDoorProperties->eDoorType);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropDoorTypeApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int index = param->index % 1000;
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pDoorProperties)
		{
			WorldDoorInteractionProperties* props = properties->eaEntries[index]->pDoorProperties;
		
			props->eDoorType = StaticDefineIntGetInt(WorldDoorTypeEnum, param->stringvalue);

			if (props->eDoorType == WorldDoorType_QueuedInstance)
			{
				// We can't have a QueuedInstance with a defined underlying pSpecificValue.
				// This causes eventual game problems and is being validated for during
				//	interaction_ValidatePropertyEntry in interaction_common.c
				// (see SVN 133981 and [STO-35643]).
				//
				StructDestroySafe(parse_WorldVariable, &(props->doorDest.pSpecificValue));
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDoorIDUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		int index = param->index % 1000;
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pDoorProperties)
		{
			param->stringvalue = StructAllocString(properties->eaEntries[index]->pDoorProperties->pcDoorIdentifier);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropDoorIDApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int index = param->index % 1000;
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > index) && properties->eaEntries[index] && properties->eaEntries[index]->pDoorProperties)
		{
			properties->eaEntries[index]->pDoorProperties->pcDoorIdentifier = allocAddString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}


static void wleAEInteractionPropDoorQueueDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pDoorProperties->hQueueDef);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropDoorQueueDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("QueueDef", param->refvalue, properties->eaEntries[param->index]->pDoorProperties->hQueueDef);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pDoorProperties->hQueueDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDoorKeyUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] 
		&& properties->eaEntries[param->index]->pDoorProperties && EMPTY_TO_NULL(properties->eaEntries[param->index]->pDoorProperties->pcDoorKey))
		{
			param->stringvalue = StructAllocString(properties->eaEntries[param->index]->pDoorProperties->pcDoorKey);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropDoorKeyApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pDoorProperties)
		{
			if (EMPTY_TO_NULL(param->stringvalue))
			{
				properties->eaEntries[param->index]->pDoorProperties->pcDoorKey = StructAllocString(param->stringvalue);
			}
			else if(properties->eaEntries[param->index]->pDoorProperties->pcDoorKey)
			{
				StructFreeStringSafe(&properties->eaEntries[param->index]->pDoorProperties->pcDoorKey);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInteractionDefUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->hInteractionDef);
			if (str && str[0])
			{
				param->stringvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropInteractionDefApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->stringvalue && param->stringvalue[0])
			{
				SET_HANDLE_FROM_STRING("InteractionDef", param->stringvalue, properties->eaEntries[param->index]->hInteractionDef);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->hInteractionDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropShowDefUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if(param->index >= 0 && param->index < eaSize(&wleAEGlobalInteractionPropUI.entries))
		param->boolvalue = wleAEGlobalInteractionPropUI.entries[param->index]->bShowDefValues;
}

static void wleAEInteractionPropShowDefApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		if(param->index >= 0 && param->index < eaSize(&wleAEGlobalInteractionPropUI.entries))
		{
			wleAEGlobalInteractionPropUI.entries[param->index]->bShowDefValues = param->boolvalue;
		}
	}
}


static void wleAEInteractionPropIsTimingUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = 1;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsTimingApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue) 
			{
				if (!properties->eaEntries[param->index]->pTimeProperties)
				{
					properties->eaEntries[param->index]->pTimeProperties = StructCreate(parse_WorldTimeInteractionProperties);

					// Set to the defaults used if this structure is not present
					properties->eaEntries[param->index]->pTimeProperties->bInterruptOnDamage = true;
					properties->eaEntries[param->index]->pTimeProperties->bInterruptOnMove = true;
					properties->eaEntries[param->index]->pTimeProperties->bInterruptOnPower = true;
				}

				// If entries' values are empty and the class is FromDef, use the def's value to fill the entries.
				if(stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0)
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry && pDef->pEntry->pTimeProperties)
					{
						DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTimeProperties->msgUseTimeText;
						if(!display_name_msg || (!GET_REF(display_name_msg->hMessage) && !display_name_msg->pEditorCopy))
						{
							StructCopyAll(parse_DisplayMessage, &pDef->pEntry->pTimeProperties->msgUseTimeText, display_name_msg);
							langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
							display_name_msg->pEditorCopy->pcMessageKey = NULL;
							display_name_msg->pEditorCopy->pcScope = NULL;
						}
					}
				}
			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pTimeProperties)
			{
				StructDestroySafe(parse_WorldTimeInteractionProperties, &properties->eaEntries[param->index]->pTimeProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropUseTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pTimeProperties->fUseTime;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEInteractionPropUseTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->fUseTime = param->floatvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropUseTimeTextUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTimeProperties->msgUseTimeText;
			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def , param->source_key, NULL);
			if(!display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[0]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropUseTimeTextApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTimeProperties->msgUseTimeText;
			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if(!display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[0])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropActiveTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pTimeProperties->fActiveTime;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEInteractionPropActiveTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->fActiveTime = param->floatvalue;
		}
		wleOpPropsEnd();
	}
}

static void wleAEInteractionPropCooldownTypeUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->stringvalue = StaticDefineIntRevLookup(WorldCooldownTimeEnum, properties->eaEntries[param->index]->pTimeProperties->eCooldownTime);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropCooldownTypeApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->eCooldownTime = StaticDefineIntGetInt(WorldCooldownTimeEnum, param->stringvalue);

			if (properties->eaEntries[param->index]->pTimeProperties->eCooldownTime != WorldCooldownTime_Custom)
			{
				properties->eaEntries[param->index]->pTimeProperties->fCustomCooldownTime = 0;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCustomCooldownTimeUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->floatvalue = properties->eaEntries[param->index]->pTimeProperties->fCustomCooldownTime;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEInteractionPropCustomCooldownTimeApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->fCustomCooldownTime = param->floatvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDynamicCooldownUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->stringvalue = StaticDefineIntRevLookup(WorldDynamicSpawnTypeEnum, properties->eaEntries[param->index]->pTimeProperties->eDynamicCooldownType);
			return;
		}
	}
	param->stringvalue = NULL;
}


static void wleAEInteractionPropDynamicCooldownApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->eDynamicCooldownType = StaticDefineIntGetInt(WorldDynamicSpawnTypeEnum, param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropHideDuringCooldownUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pTimeProperties->bHideDuringCooldown;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropHideDuringCooldownApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->bHideDuringCooldown = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropTeamUsableWhenActiveUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pTimeProperties->bTeamUsableWhenActive;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropTeamUsableWhenActiveApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->bTeamUsableWhenActive = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInterruptOnPowerUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pTimeProperties->bInterruptOnPower;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropInterruptOnPowerApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->bInterruptOnPower = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInterruptOnDamageUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pTimeProperties->bInterruptOnDamage;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropInterruptOnDamageApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->bInterruptOnDamage = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInterruptOnMoveUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pTimeProperties->bInterruptOnMove;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropInterruptOnMoveApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->bInterruptOnMove = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropNoRespawnUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			param->boolvalue = properties->eaEntries[param->index]->pTimeProperties->bNoRespawn;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropNoRespawnApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTimeProperties)
		{
			properties->eaEntries[param->index]->pTimeProperties->bNoRespawn = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsActionUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->boolvalue = 1;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsAnimationUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEInteractionPropUpdateInit();
	if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pAnimationProperties)
	{
		param->boolvalue = 1;
		return;
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsAnimationApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue)
			{
				if(!properties->eaEntries[param->index]->pAnimationProperties) 
				{
					properties->eaEntries[param->index]->pAnimationProperties = StructCreate(parse_WorldAnimationInteractionProperties);
				}

				// If entry's value is empty and the class is FromDef, use the def's value to fill the param.
				if( (stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0) &&
					!GET_REF(properties->eaEntries[param->index]->pAnimationProperties->hInteractAnim) )
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry && pDef->pEntry->pAnimationProperties)
					{
						COPY_HANDLE(properties->eaEntries[param->index]->pAnimationProperties->hInteractAnim, pDef->pEntry->pAnimationProperties->hInteractAnim);
					}
				}

				// Set to the defaults used if this structure is not present
			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pAnimationProperties)
			{
				StructDestroySafe(parse_WorldAnimationInteractionProperties, &properties->eaEntries[param->index]->pAnimationProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInteractAnimUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	wleAEInteractionPropUpdateInit();
	if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pAnimationProperties)
	{
		const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pAnimationProperties->hInteractAnim);
		if (str && str[0])
		{
			param->refvalue = StructAllocString(str);
			return;
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropInteractAnimApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pAnimationProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("AIAnimList", param->refvalue, properties->eaEntries[param->index]->pAnimationProperties->hInteractAnim);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pAnimationProperties->hInteractAnim);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}



static void wleAEInteractionPropIsSoundUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEInteractionPropUpdateInit();
	if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pSoundProperties)
	{
		param->boolvalue = 1;
		return;
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsSoundApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue)
			{
				WorldSoundInteractionProperties *pPropSound;
				if(!properties->eaEntries[param->index]->pSoundProperties) 
				{
					properties->eaEntries[param->index]->pSoundProperties = StructCreate(parse_WorldSoundInteractionProperties);
				}
				pPropSound = properties->eaEntries[param->index]->pSoundProperties;

				// If entries' values are empty and the class is FromDef, use the def's value to fill the params.
				if( (stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0))
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pPropSound && pDef && pDef->pEntry && pDef->pEntry->pSoundProperties)
					{
						WorldSoundInteractionProperties *pDefSound = pDef->pEntry->pSoundProperties;

						if(!EMPTY_TO_NULL(pPropSound->pchAttemptSound) && EMPTY_TO_NULL(pDefSound->pchAttemptSound))
						{
							pPropSound->pchAttemptSound = StructAllocString(pDefSound->pchAttemptSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchSuccessSound) && EMPTY_TO_NULL(pDefSound->pchSuccessSound))
						{
							pPropSound->pchSuccessSound = StructAllocString(pDefSound->pchSuccessSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchFailureSound) && EMPTY_TO_NULL(pDefSound->pchFailureSound))
						{
							pPropSound->pchFailureSound = StructAllocString(pDefSound->pchFailureSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchInterruptSound) && EMPTY_TO_NULL(pDefSound->pchInterruptSound))
						{
							pPropSound->pchInterruptSound = StructAllocString(pDefSound->pchInterruptSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchMovementTransStartSound) && EMPTY_TO_NULL(pDefSound->pchMovementTransStartSound))
						{
							pPropSound->pchMovementTransStartSound = StructAllocString(pDefSound->pchMovementTransStartSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchMovementTransEndSound) && EMPTY_TO_NULL(pDefSound->pchMovementTransEndSound))
						{
							pPropSound->pchMovementTransEndSound = StructAllocString(pDefSound->pchMovementTransEndSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchMovementReturnStartSound) && EMPTY_TO_NULL(pDefSound->pchMovementReturnStartSound))
						{
							pPropSound->pchMovementReturnStartSound = StructAllocString(pDefSound->pchMovementReturnStartSound);
						}

						if(!EMPTY_TO_NULL(pPropSound->pchMovementReturnEndSound) && EMPTY_TO_NULL(pDefSound->pchMovementReturnEndSound))
						{
							pPropSound->pchMovementReturnEndSound = StructAllocString(pDefSound->pchMovementReturnEndSound);
						}
					}
				}
			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pSoundProperties)
			{
				StructDestroySafe(parse_WorldSoundInteractionProperties, &properties->eaEntries[param->index]->pSoundProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropSoundUpdate(WleAEParamText *param, const char *fieldName, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && 
			properties->eaEntries[param->index]->pSoundProperties)
		{
			int column;
			const char *soundVal = NULL;

			if (ParserFindColumn(parse_WorldSoundInteractionProperties, fieldName, &column))
				soundVal = TokenStoreGetString(parse_WorldSoundInteractionProperties, column, properties->eaEntries[param->index]->pSoundProperties, 0, NULL);
			if (EMPTY_TO_NULL(soundVal))
			{
				param->stringvalue = StructAllocString(soundVal);
				return;
			}
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropSoundApply(WleAEParamText *param, const char *fieldName, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && 
			properties->eaEntries[param->index]->pSoundProperties)
		{
			int column;

			if (ParserFindColumn(parse_WorldSoundInteractionProperties, fieldName, &column))
			{
				if (EMPTY_TO_NULL(param->stringvalue))
				{
					TokenStoreSetString(parse_WorldSoundInteractionProperties, column, properties->eaEntries[param->index]->pSoundProperties, 0, param->stringvalue, NULL, NULL, NULL, NULL);
				}
				else
				{
					TokenStoreFreeString(parse_WorldSoundInteractionProperties, column, properties->eaEntries[param->index]->pSoundProperties, 0, NULL);
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}


static void wleAEInteractionPropIsActionApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue && !properties->eaEntries[param->index]->pActionProperties) 
			{
				properties->eaEntries[param->index]->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);
			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pActionProperties)
			{
				StructDestroySafe(parse_WorldActionInteractionProperties, &properties->eaEntries[param->index]->pActionProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropAttemptExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pActionProperties->pAttemptExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropAttemptExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			exprDestroy(properties->eaEntries[param->index]->pActionProperties->pAttemptExpr);
			properties->eaEntries[param->index]->pActionProperties->pAttemptExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropSuccessExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pActionProperties->pSuccessExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropSuccessExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			exprDestroy(properties->eaEntries[param->index]->pActionProperties->pSuccessExpr);
			properties->eaEntries[param->index]->pActionProperties->pSuccessExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEnd();
	}
}

static void wleAEInteractionPropFailureExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pActionProperties->pFailureExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropFailureExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			exprDestroy(properties->eaEntries[param->index]->pActionProperties->pFailureExpr);
			properties->eaEntries[param->index]->pActionProperties->pFailureExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInterruptExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pActionProperties->pInterruptExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropInterruptExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			exprDestroy(properties->eaEntries[param->index]->pActionProperties->pInterruptExpr);
			properties->eaEntries[param->index]->pActionProperties->pInterruptExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropNoLongerActiveExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pActionProperties->pNoLongerActiveExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropNoLongerActiveExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			exprDestroy(properties->eaEntries[param->index]->pActionProperties->pNoLongerActiveExpr);
			properties->eaEntries[param->index]->pActionProperties->pNoLongerActiveExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropCooldownExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->exprvalue = exprClone(properties->eaEntries[param->index]->pActionProperties->pCooldownExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropCooldownExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			exprDestroy(properties->eaEntries[param->index]->pActionProperties->pCooldownExpr);
			properties->eaEntries[param->index]->pActionProperties->pCooldownExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropSuccessActionsUpdate(WleAEParamGameAction *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->action_block = StructClone(parse_WorldGameActionBlock, &properties->eaEntries[param->index]->pActionProperties->successActions);

			wleAEFixupGameActionMessageKey(param->action_block, def, param->source_key);

			return;
		}
	}
	param->action_block = NULL;
}

static void wleAEInteractionPropSuccessActionsApply(WleAEParamGameAction *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			if (param->action_block)
			{
				StructCopyAll(parse_WorldGameActionBlock, param->action_block, &properties->eaEntries[param->index]->pActionProperties->successActions);
				wleAEFixupGameActionMessageKey(param->action_block, def, param->source_key);
			}
			else
			{
				int j;
				for(j = eaSize(&properties->eaEntries[param->index]->pActionProperties->successActions.eaActions) - 1; j >= 0; --j)
				{
					StructDestroy(parse_WorldGameActionProperties, properties->eaEntries[param->index]->pActionProperties->successActions.eaActions[j]);
				}
				eaDestroy(&properties->eaEntries[param->index]->pActionProperties->successActions.eaActions);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropFailureActionsUpdate(WleAEParamGameAction *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			param->action_block = StructClone(parse_WorldGameActionBlock, &properties->eaEntries[param->index]->pActionProperties->failureActions);

			wleAEFixupGameActionMessageKey(param->action_block, def, param->source_key);

			return;
		}
	}
	param->action_block = NULL;
}

static void wleAEInteractionPropFailureActionsApply(WleAEParamGameAction *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pActionProperties)
		{
			if (param->action_block)
			{
				StructCopyAll(parse_WorldGameActionBlock, param->action_block, &properties->eaEntries[param->index]->pActionProperties->failureActions);
				wleAEFixupGameActionMessageKey(param->action_block, def, param->source_key);
			}
			else
			{
				int j;
				for(j = eaSize(&properties->eaEntries[param->index]->pActionProperties->failureActions.eaActions) - 1; j >= 0; --j)
				{
					StructDestroy(parse_WorldGameActionProperties, properties->eaEntries[param->index]->pActionProperties->failureActions.eaActions[j]);
				}
				eaDestroy(&properties->eaEntries[param->index]->pActionProperties->failureActions.eaActions);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsTextUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			param->boolvalue = 1;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsTextApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue)
			{
				if(!properties->eaEntries[param->index]->pTextProperties)
				{
					properties->eaEntries[param->index]->pTextProperties = StructCreate(parse_WorldTextInteractionProperties);
				}

				// If entries' values are empty and the class is FromDef, use the def's value to fill the entries.
				if(stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0)
				{
					InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);
					if(pDef && pDef->pEntry && pDef->pEntry->pTextProperties)
					{
						DisplayMessage* display_name_msg;
					
						display_name_msg = &properties->eaEntries[param->index]->pTextProperties->usabilityOptionText;
						if(!display_name_msg || (!GET_REF(display_name_msg->hMessage) && !display_name_msg->pEditorCopy))
						{
							StructCopyAll(parse_DisplayMessage, &pDef->pEntry->pTextProperties->usabilityOptionText, display_name_msg);
							langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
							display_name_msg->pEditorCopy->pcMessageKey = NULL;
							display_name_msg->pEditorCopy->pcScope = NULL;
						}

						display_name_msg = &properties->eaEntries[param->index]->pTextProperties->interactOptionText;
						if(!display_name_msg || (!GET_REF(display_name_msg->hMessage) && !display_name_msg->pEditorCopy))
						{
							StructCopyAll(parse_DisplayMessage, &pDef->pEntry->pTextProperties->interactOptionText, display_name_msg);
							langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
							display_name_msg->pEditorCopy->pcMessageKey = NULL;
							display_name_msg->pEditorCopy->pcScope = NULL;
						}

						display_name_msg = &properties->eaEntries[param->index]->pTextProperties->interactDetailText;
						if(!display_name_msg || (!GET_REF(display_name_msg->hMessage) && !display_name_msg->pEditorCopy))
						{
							StructCopyAll(parse_DisplayMessage, &pDef->pEntry->pTextProperties->interactDetailText, display_name_msg);
							langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
							display_name_msg->pEditorCopy->pcMessageKey = NULL;
							display_name_msg->pEditorCopy->pcScope = NULL;
						}

						display_name_msg = &properties->eaEntries[param->index]->pTextProperties->successConsoleText;
						if(!display_name_msg || (!GET_REF(display_name_msg->hMessage) && !display_name_msg->pEditorCopy))
						{
							StructCopyAll(parse_DisplayMessage, &pDef->pEntry->pTextProperties->successConsoleText, display_name_msg);
							langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
							display_name_msg->pEditorCopy->pcMessageKey = NULL;
							display_name_msg->pEditorCopy->pcScope = NULL;
						}

						display_name_msg = &properties->eaEntries[param->index]->pTextProperties->failureConsoleText;
						if(!display_name_msg || (!GET_REF(display_name_msg->hMessage) && !display_name_msg->pEditorCopy))
						{
							StructCopyAll(parse_DisplayMessage, &pDef->pEntry->pTextProperties->failureConsoleText, display_name_msg);
							langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
							display_name_msg->pEditorCopy->pcMessageKey = NULL;
							display_name_msg->pEditorCopy->pcScope = NULL;
						}
					}
				}
			}
			else if (!param->boolvalue && properties->eaEntries[param->index]->pTextProperties)
			{
				StructDestroySafe(parse_WorldTextInteractionProperties, &properties->eaEntries[param->index]->pTextProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropUsabilityTextUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->usabilityOptionText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def , param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}

			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropUsabilityTextApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->usabilityOptionText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInteractTextUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->interactOptionText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def , param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}

			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropInteractTextApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->interactOptionText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInteractDetailTextUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->interactDetailText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def , param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}

			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropInteractDetailTextApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->interactDetailText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropInteractDetailTextureUpdate(WleAEParamTexture *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			param->texturename = StructAllocString(properties->eaEntries[param->index]->pTextProperties->interactDetailTexture);
			return;
		}
	}
	param->texturename = NULL;
}

static void wleAEInteractionPropInteractDetailTextureApply(WleAEParamTexture *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			properties->eaEntries[param->index]->pTextProperties->interactDetailTexture = allocAddString(param->texturename);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropSuccessTextUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->successConsoleText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropSuccessTextApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->successConsoleText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropFailureTextUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->failureConsoleText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropFailureTextApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pTextProperties)
		{
			DisplayMessage* display_name_msg = &properties->eaEntries[param->index]->pTextProperties->failureConsoleText;

			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropDisplayNameBasicUpdate(WleAEParamMessage *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties)
		{
			DisplayMessage* display_name_msg = &properties->displayNameMsg;
			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			groupDefFixupMessageKey( &display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL );
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) {
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
			StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &param->message);
			return;
		}
	}
	StructReset(parse_Message, &param->message);
}

static void wleAEInteractionPropDisplayNameBasicApply(WleAEParamMessage *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (properties)
		{
			DisplayMessage* display_name_msg = &properties->displayNameMsg;
			StructCopyAll(parse_Message, &param->message, display_name_msg->pEditorCopy);
			groupDefFixupMessageKey(&display_name_msg->pEditorCopy->pcMessageKey, def, param->source_key, NULL);
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(param->source_key);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropIsRewardUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			param->boolvalue = 1;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropIsRewardApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index])
		{
			if (param->boolvalue) 
			{
				InteractionDef* pDef = GET_REF(properties->eaEntries[param->index]->hInteractionDef);

				// If values are empty and interaction is FromDef, then fill the fields with the def values
				if(stricmp(properties->eaEntries[param->index]->pcInteractionClass, "FromDefinition") == 0)
				{
					properties->eaEntries[param->index]->pRewardProperties = StructClone(parse_WorldRewardInteractionProperties, pDef->pEntry->pRewardProperties);
				}
				if (!properties->eaEntries[param->index]->pRewardProperties)
				{
					properties->eaEntries[param->index]->pRewardProperties = StructCreate(parse_WorldRewardInteractionProperties);
				}
			}
			else
			{
				StructDestroySafe(parse_WorldRewardInteractionProperties, &properties->eaEntries[param->index]->pRewardProperties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropRewardDefUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			const char *str = REF_STRING_FROM_HANDLE(properties->eaEntries[param->index]->pRewardProperties->hRewardTable);
			if (str && str[0])
			{
				param->refvalue = StructAllocString(str);
				return;
			}
		}
	}
	param->refvalue = NULL;
}

static void wleAEInteractionPropRewardDefApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			if (param->refvalue && param->refvalue[0])
			{
				SET_HANDLE_FROM_STRING("RewardTable", param->refvalue, properties->eaEntries[param->index]->pRewardProperties->hRewardTable);
			}
			else
			{
				REMOVE_HANDLE(properties->eaEntries[param->index]->pRewardProperties->hRewardTable);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropRewardLevelTypeUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			param->stringvalue = StaticDefineIntRevLookup(WorldRewardLevelTypeEnum, properties->eaEntries[param->index]->pRewardProperties->eRewardLevelType);
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropRewardLevelTypeApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			properties->eaEntries[param->index]->pRewardProperties->eRewardLevelType = StaticDefineIntGetInt(WorldRewardLevelTypeEnum, param->stringvalue);

			if (properties->eaEntries[param->index]->pRewardProperties->eRewardLevelType != WorldRewardLevelType_Custom)
			{
				properties->eaEntries[param->index]->pRewardProperties->uCustomRewardLevel = 0;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropRewardCustomLevelUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			param->intvalue = properties->eaEntries[param->index]->pRewardProperties->uCustomRewardLevel;
			return;
		}
	}
	param->intvalue = 0;
}

static void wleAEInteractionPropRewardCustomLevelApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			properties->eaEntries[param->index]->pRewardProperties->uCustomRewardLevel = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropRewardMapVariableUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			param->stringvalue = properties->eaEntries[param->index]->pRewardProperties->pcMapVarName;
			return;
		}
	}
	param->stringvalue = NULL;
}

static void wleAEInteractionPropRewardMapVariableApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && properties->eaEntries[param->index]->pRewardProperties)
		{
			properties->eaEntries[param->index]->pRewardProperties->pcMapVarName = StructAllocString(param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

// --------------------------------------------------------------------------------------------------------
static void wleAEInteractionProp_AmbientJobCritterUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();

		if ((eaSize(&wleAEGlobalInteractionPropUI.entries) > param->index) &&
			properties && 
			(eaSize(&properties->eaEntries) > param->index) && 
			properties->eaEntries[param->index] && 
			properties->eaEntries[param->index]->pAmbientJobProperties)
		{
			if (&wleAEGlobalInteractionPropUI.entries[param->index]->isForCritters == param)
			{
				param->boolvalue = properties->eaEntries[param->index]->pAmbientJobProperties->isForCitters;
			}
			else if (&wleAEGlobalInteractionPropUI.entries[param->index]->isForCivilians == param)
			{
				param->boolvalue = properties->eaEntries[param->index]->pAmbientJobProperties->isForCivilians;
			}
			else if (&wleAEGlobalInteractionPropUI.entries[param->index]->ambientJobInitialJob == param)
			{
				param->boolvalue = properties->eaEntries[param->index]->pAmbientJobProperties->initialJob;
			}
			return;
		}
	}
	param->boolvalue = 0;
}

// --------------------------------------------------------------------------------------------------------
static void wleAEInteractionProp_AmbientJobCritterApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if ((eaSize(&wleAEGlobalInteractionPropUI.entries) > param->index) &&
			properties && 
			(eaSize(&properties->eaEntries) > param->index) && 
			properties->eaEntries[param->index])
		{
			WleAEInteractionEntryPropUI *pEntry = wleAEGlobalInteractionPropUI.entries[param->index];
			WorldAmbientJobInteractionProperties *pAmbientJobProperties;

			if (!properties->eaEntries[param->index]->pAmbientJobProperties)
			{
				properties->eaEntries[param->index]->pAmbientJobProperties = StructCreate(parse_WorldAmbientJobInteractionProperties);
			}
			pAmbientJobProperties = properties->eaEntries[param->index]->pAmbientJobProperties;

			if (&pEntry->isForCritters == param)
			{
				pAmbientJobProperties->isForCitters = param->boolvalue;
			}
			else if (&pEntry->isForCivilians == param)
			{
				pAmbientJobProperties->isForCivilians = param->boolvalue;
			}
			else if (&pEntry->ambientJobInitialJob == param)
			{
				pAmbientJobProperties->initialJob = param->boolvalue;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

// --------------------------------------------------------------------------------------------------------
static void wleAEInteractionProp_AmbientJobPriorityUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();

		if (properties && 
			(eaSize(&properties->eaEntries) > param->index) && 
			properties->eaEntries[param->index] && 
			properties->eaEntries[param->index]->pAmbientJobProperties)
		{
			param->intvalue = properties->eaEntries[param->index]->pAmbientJobProperties->iPriority;
			param->is_specified = true;
			return;
		}
	}
	param->intvalue = 0;
	param->is_specified = false;
}

// --------------------------------------------------------------------------------------------------------
static void wleAEInteractionProp_AmbientJobPriorityApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && (eaSize(&properties->eaEntries) > param->index) && properties->eaEntries[param->index] && param->is_specified)
		{
			if (!properties->eaEntries[param->index]->pAmbientJobProperties)
			{
				properties->eaEntries[param->index]->pAmbientJobProperties = StructCreate(parse_WorldAmbientJobInteractionProperties);
			}
			properties->eaEntries[param->index]->pAmbientJobProperties->iPriority = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropChildSelectUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && properties->pChildProperties)
		{
			param->is_specified = true;
			param->intvalue = properties->pChildProperties->iChildIndex;
			return;
		}
	}
	param->is_specified = false;
	param->intvalue = 0;
}

static void wleAEInteractionPropChildSelectApply(WleAEParamInt *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);
		if (!param->is_specified && properties && properties->pChildProperties)
		{
			StructDestroy(parse_WorldChildInteractionProperties, properties->pChildProperties);
			properties->pChildProperties = NULL;
		}
		else if (param->is_specified)
		{
			if (!properties->pChildProperties)
			{
				properties->pChildProperties = StructCreate(parse_WorldChildInteractionProperties);
			}
			properties->pChildProperties->iChildIndex = param->intvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropChildSelectExprUpdate(WleAEParamExpression *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties && properties->pChildProperties)
		{
			param->exprvalue = exprClone(properties->pChildProperties->pChildSelectExpr);
			return;
		}
	}
	param->exprvalue = NULL;
}

static void wleAEInteractionPropChildSelectExprApply(WleAEParamExpression *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties && properties->pChildProperties)
		{
			exprDestroy(properties->pChildProperties->pChildSelectExpr);
			properties->pChildProperties->pChildSelectExpr = exprClone(param->exprvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropApplyAsNodeUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	GroupTracker *tracker;
	GroupDef *def;

	param->stringvalue = "Node";
	if (obj->type->objType == EDTYPE_TRACKER)
	{		
		tracker = trackerFromTrackerHandle(obj->obj);
		def = tracker ? tracker->def : NULL;
		if (def && (
			(def->property_structs.server_volume.interaction_volume_properties) ||
			(!def->property_structs.interaction_properties && (def->property_structs.volume && !def->property_structs.volume->bSubVolume))))
		{
			param->stringvalue = "Volume";
		}
	}
}

static void wleAEInteractionPropApplyAsNodeApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	bool refreshUI = false;
	for (i = 0; i < eaSize(&objs); i++)
	{
		GroupTracker *tracker;
		GroupDef *def;

		if (objs[i]->type->objType == EDTYPE_TRACKER)
		{
			tracker = wleOpPropsBegin(objs[i]->obj);
			if (!tracker)
			{
				continue;
			}
			def = tracker ? tracker->def : NULL;
			if (!def)
			{
				wleOpPropsEndNoUIUpdate();
				refreshUI = true;
				continue;
			}

			if (stricmp(param->stringvalue, "Node") == 0 && def->property_structs.server_volume.interaction_volume_properties)
			{
				def->property_structs.interaction_properties = def->property_structs.server_volume.interaction_volume_properties;
				def->property_structs.server_volume.interaction_volume_properties = NULL;
				groupDefRemoveVolumeType(def, "Interaction");
			}
			else if (stricmp(param->stringvalue, "Volume") == 0 && def->property_structs.interaction_properties)
			{
				int j;
				WorldInteractionPropertyEntry **eaEntries;
				def->property_structs.server_volume.interaction_volume_properties = def->property_structs.interaction_properties;
				def->property_structs.interaction_properties = NULL;
				groupDefAddVolumeType(def, "Interaction");

				eaEntries = def->property_structs.server_volume.interaction_volume_properties->eaEntries;
				for (j = 0; j < eaSize(&eaEntries); j++)
				{
					WorldInteractionPropertyEntry *pEntry = eaEntries[j];
					if(stricmp(pEntry->pcInteractionClass, "Destructible") == 0 || stricmp(pEntry->pcInteractionClass, "Gate") == 0)
					{
						pEntry->pcInteractionClass = (char*)allocAddString("Clickable");
					}
					StructDestroySafe(parse_Expression, &pEntry->pVisibleExpr);
				}
			}
			wleOpPropsEndNoUIUpdate();
			refreshUI = true;
		}
	}
	if (refreshUI)
	{
		wleOpRefreshUI();
	}
}

static void wleAEInteractionPropExplicitHideUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties)
		{
			param->boolvalue = !!properties->bAllowExplicitHide;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropExplicitHideApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties)
		{
			properties->bAllowExplicitHide = !!param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEInteractionPropStartsHiddenUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER || obj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		wleAEInteractionPropUpdateInit();
		if (properties)
		{
			param->boolvalue = !!properties->bStartsHidden;
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEInteractionPropStartsHiddenApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEInteractionPropApplyInitAt(i);

		if (properties)
		{
			properties->bStartsHidden = !!param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

/********************
* Display Functions
********************/

// Shows all class information except for the class header and contact information (split here since the contact may still be editable if using an interaction def)
static void wleAEInteraction_ShowEntryClassBody(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams)
{
	const char* pcClass = interaction_GetEffectiveClass(pPropEntry);
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];
	char buf[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	// Crafting Station
	if (stricmp(pcClass, "CraftingStation") == 0 && pPropEntry->pCraftingProperties) {
		sprintf(key, "%sDefCraftingSkill%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Skill Type", "The skill type of this crafting station.", key, &labelParams, true);
		sprintf(key, "%sDefCraftingSkillValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, StaticDefineIntRevLookup(WorldSkillTypeEnum, pPropEntry->pCraftingProperties->eSkillFlags), key, &valueParams, false);

		sprintf(key, "%sDefCraftingMaxSkill%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Max Skill", "The maximum skill this station operates at (caps the player's skill).  Zero means no maximum.", key, &labelParams, true);
		sprintf(key, "%sDefCraftingMaxSkillValue%d", keyPrefix, iInteractionIndex);
		sprintf(buf, "%d", pPropEntry->pCraftingProperties->iMaxSkill);
		ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);

		if(GET_REF(pPropEntry->pCraftingProperties->hCraftRewardTable)) 
		{
			sprintf(key, "%sDefCraftingReward%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Craft Reward", "Reward table granted on successful crafting.", key, &labelParams, true);
			sprintf(key, "%sDefCraftingRewardValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pCraftingProperties->hCraftRewardTable)), key, &valueParams, false);
		}

		if(GET_REF(pPropEntry->pCraftingProperties->hDeconstructRewardTable))
		{
			sprintf(key, "%sDefCraftingDeconstructReward%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Deconstruct Reward", "Reward table granted on successful deconstruct.", key, &labelParams, true);
			sprintf(key, "%sDefCraftingDeconstructRewardValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pCraftingProperties->hDeconstructRewardTable)), key, &valueParams, false);		
		}

		if(GET_REF(pPropEntry->pCraftingProperties->hExperimentRewardTable))
		{
			sprintf(key, "%sDefCraftingExperimentReward%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Experiment Reward", "Reward table granted on successful experiment.", key, &labelParams, true);
			sprintf(key, "%sDefCraftingExperimentRewardValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pCraftingProperties->hExperimentRewardTable)), key, &valueParams, false);		
		}
	}

	// Destructible
	if ( stricmp(pcClass, "Destructible") == 0 && pPropEntry->pDestructibleProperties ) {

		if(GET_REF(pPropEntry->pDestructibleProperties->hCritterDef))
		{
			sprintf(key, "%sDefDestructibleCritter%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Critter", "What critter def this should turn into.", key, &labelParams, true);
			sprintf(key, "%sDefDestructibleCritterValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pDestructibleProperties->hCritterDef)), key, &valueParams, false);		
		}

		if(GET_REF(pPropEntry->pDestructibleProperties->hCritterOverrideDef))
		{
			sprintf(key, "%sDefDestructibleCritterOverride%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Override", "Overrides for the specified critter def.", key, &labelParams, true);
			sprintf(key, "%sDefDestructibleCritterOverrideValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pDestructibleProperties->hCritterOverrideDef)), key, &valueParams, false);		
		}

		sprintf(key, "%sDefDestructibleRespawnTime%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Respawn Time", "The amount of time in seconds before the node re-spawns.", key, &labelParams, true);
		sprintf(key, "%sDefDestructibleRespawnTimeValue%d", keyPrefix, iInteractionIndex);
		sprintf(buf, "%f", pPropEntry->pDestructibleProperties->fRespawnTime);
		ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);		

		if(GET_REF(pPropEntry->pDestructibleProperties->hOnDeathPowerDef))
		{
			sprintf(key, "%sDefDestructibleOnDeathPower%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "On Death Power", "The power to be executed when the object is destroyed.", key, &labelParams, true);
			sprintf(key, "%sDefDestructibleOnDeathPowerValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pDestructibleProperties->hOnDeathPowerDef)), key, &valueParams, false);		
		}

		if(EMPTY_TO_NULL(pPropEntry->pDestructibleProperties->pcEntityName))
		{
			sprintf(key, "%sDefDestructibleEntityName%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Entity Name", "The entity name to be used when created.", key, &labelParams, true);
			sprintf(key, "%sDefDestructibleEntityNameValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(pPropEntry->pDestructibleProperties->pcEntityName), key, &valueParams, false);		
		}

		if(GET_REF(pPropEntry->pDestructibleProperties->displayNameMsg.hMessage))
		{
			sprintf(key, "%sDefDestructibleDisplayName%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Display Name", "The Display name for the object. If not provided, the object will have no name.", key, &labelParams, true);
			sprintf(key, "%sDefDestructibleDisplayNameValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(TranslateDisplayMessage(pPropEntry->pDestructibleProperties->displayNameMsg)), key, &valueParams, false);		
		}

		sprintf(key, "%sDefDestructibleCritterLevel%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Critter Level", "The spawned level of the critter.  If zero, the map level is used instead.", key, &labelParams, true);
		sprintf(key, "%sDefDestructibleCritterLevelValue%d", keyPrefix, iInteractionIndex);
		sprintf(buf, "%d", pPropEntry->pDestructibleProperties->uCritterLevel);
		ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);		
	}

}

// Displays the entry class in uneditable format
static void wleAEInteraction_ShowEntryClass(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams)
{
	const char* pcClass = interaction_GetEffectiveClass(pPropEntry);

	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	// From Def
	if (stricmp(pPropEntry->pcInteractionClass, "FromDefinition") == 0)
	{
		sprintf(key, "%sInteractionDef%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interaction Def", "The Interaction Def used as a base for this interaction", key, &labelParams, true);
		sprintf(key, "%sInteractionDefValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->hInteractionDef)), key, &valueParams, false);
	}

	sprintf(key, "%sDefClassHeading%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Class Properties", "Properties associated with the class of this entry.", key, baseParams, true);

	sprintf(key, "%sDefClass%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Class", "The class of this entry.", key, &labelParams, true);
	sprintf(key, "%sDefClassValue%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyed(root, pcClass, key, &valueParams, false);

	// Contact
	if (stricmp(pcClass, "Contact") == 0 && pPropEntry->pContactProperties) {
		sprintf(key, "%sDefContact%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Contact", "Which contact def is used.", key, &labelParams, true);
		sprintf(key, "%sDefContactValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pContactProperties->hContactDef)), key, &valueParams, false);
		sprintf(key, "%sDefContactDialog%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Dialog Name", "Which dialog on the contact def is used.", key, &labelParams, true);
		sprintf(key, "%sDefContactDialogValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(pPropEntry->pContactProperties->pcDialogName), key, &valueParams, false);
	}

	wleAEInteraction_ShowEntryClassBody(root, pPropEntry, iInteractionIndex, keyPrefix, baseParams);
}

// Shows the entry class information with the contact and door editable
static void wleAEInteraction_ShowDefEntryClass(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, WleAEInteractionEntryPropUI *entry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams, bool actors)
{
	const char* pcClass = interaction_GetEffectiveClass(pPropEntry);
	UIAutoWidgetParams headerParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	// From Def
	if (stricmp(pPropEntry->pcInteractionClass, "FromDefinition") == 0)
	{
		sprintf(key, "%sInteractionDef%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interaction Def", "The Interaction Def used as a base for this interaction", key, &labelParams, true);
		sprintf(key, "%sInteractionDefValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->hInteractionDef)), key, &valueParams, false);
	}

	sprintf(key, "%sDefClassHeading%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Class Properties", "Properties associated with the class of this entry.", key, baseParams, true);

	sprintf(key, "%sDefClass%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Class", "The class of this entry.", key, &labelParams, true);
	sprintf(key, "%sDefClassValue%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyed(root, pcClass, key, &valueParams, false);

	sprintf(key, "%sExclusive%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Exclusive", "This determines whether only one person can interact with this entity at a time.", key, &labelParams, true);
	sprintf(key, "%sExclusiveValue%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyed(root, interaction_EntryGetExclusive(pPropEntry, !actors) ? "True" : "False", key, &valueParams, false);

	sprintf(key, "%sAutoExec%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Auto Execute", "This determines whether this interaction will execute immediately when an entity is in range and meets interaction requirements.", key, &labelParams, true);
	sprintf(key, "%sAutoExecValue%d", keyPrefix, iInteractionIndex);
	ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->bAutoExecute ? "True" : "False", key, &valueParams, false);

	// Contact
	if (stricmp(pcClass, "Contact") == 0 && pPropEntry->pContactProperties) {
		if(entry) {
			wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Contact", "Use a custom contact def.", "isContact", &entry->isContact);
			if (entry->isContact.boolvalue)
			{
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Contact", "Which contact def is used.", "contactdef", &entry->contactDef);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dialog Name", "Which dialog on the contact def is used.", "contactdialog", &entry->contactDialog);
			} else if(entry->bShowDefValues) {
				labelParams.alignTo += 20;
				valueParams.alignTo += 20;
				sprintf(key, "%sDefContact%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Contact", "Which contact def is used.", key, &labelParams, true);
				sprintf(key, "%sDefContactValue%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pContactProperties->hContactDef)), key, &valueParams, false);
				sprintf(key, "%sDefContactDialog%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Dialog Name", "Which dialog on the contact def is used.", key, &labelParams, true);
				sprintf(key, "%sDefContactDialogValue%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(pPropEntry->pContactProperties->pcDialogName), key, &valueParams, false);
			}
		} 
	}

	// Door
	if (stricmp(pcClass, "Door") == 0 && pPropEntry->pDoorProperties) {
		char buf[256];
		wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Door", "Use custom door props.", "isDoor", &entry->isDoor);
		if (entry->isDoor.boolvalue)
		{
			wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door Type", "Sets the door type", "doorType", &entry->doorType);
			wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door ID", "Used for joining teammates together when a teammate uses a 'JoinTeammate' door.", "doorID", &entry->doorID);
		} 
		else if(entry->bShowDefValues) 
		{
			sprintf(key, "%sDefDoorType%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Door Type", "The 'MapMove' type is used for same and cross-map moves.  The 'QueuedInstance' type queues up a later move. The 'JoinTeammate' type is used to allow players to join their teammates on other maps.", key, &labelParams, true);
			sprintf(key, "%sDefDoorTypeValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, StaticDefineIntRevLookup(WorldDoorTypeEnum, pPropEntry->pDoorProperties->eDoorType), key, &valueParams, false);		

			sprintf(key, "%sDefDoorID%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Door ID", "Used for joining teammates together when a teammate uses a 'JoinTeammate' door.", key, &labelParams, true);
			sprintf(key, "%sDefDoorIDValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pDoorProperties->pcDoorIdentifier, key, &valueParams, false);		
		}

		if(pPropEntry->pDoorProperties->eDoorType == WorldDoorType_QueuedInstance)
		{
			if (entry->isDoor.boolvalue)
			{
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Queued Instance", "If the door type is \"Queued Instance\"; this is the instance it will queue the player.", "queuedef", &entry->queueDef);
			}
			else if(entry->bShowDefValues) 
			{
				sprintf(key, "%sDefDoorQueue%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Queue Name", "The name of the queue to put the player on.", key, &labelParams, true);
				sprintf(key, "%sDefDoorQueueValue%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pDoorProperties->hQueueDef)), key, &valueParams, false);		
			}
		} 
		else 
		{
			if (entry->isDoor.boolvalue)
			{
				if(stricmp(entry->doorType.stringvalue, "Keyed") == 0) {
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door Key", "The name which must match the key name on a door key item in order for that item to create a door interaction.  A door key item's key name is the value of the map variable, \"MAP_ENTRY_KEY\" on the map where the item was created.", "DoorKey", &entry->doorKey);
				} else if (stricmp(entry->doorType.stringvalue, "JoinTeammate") != 0) {
					wleAEWorldVariableDefAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Destination", "Determines how the destination will be chosen.", "doorDest", &entry->doorDest);
				}

				if (stricmp(entry->doorType.stringvalue, "JoinTeammate") != 0)
				{
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Per Player", "Creates one interaction choice for each teammate who meets the Interact condition.  Door must go to an OWNED map.", "PerPlayerDoor", &entry->perPlayerDoor);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Single Player", "Creates one interaction choice that is not shared among teammates.  Door must go to an OWNED map.", "SinglePlayerDoor", &entry->singlePlayerDoor);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Include Teammates", " Brings all of the interacting player's teammates along to the destination.", "IncludeTeammatesDoor", &entry->includeTeammatesDoor);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Collect Destination Status", "Collects summary information about the destination of this door.", "CollectDestStatus", &entry->collectDestStatus);
                    wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Destination Map Has Same Owner", "The destination map should have the same owner as this map.", "DestinationSameWoner", &entry->destinationSameOwner);

					if(!actors) {
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door Variables", "Whether or not the door passes variables to the map.", "HasVariables", &entry->doorHasVariables);
						if (entry->doorHasVariables.boolvalue)
						{
							int sub_index;
							for(sub_index=0; sub_index < MAX_VARIABLES; ++sub_index) {
								WleAEInteractionDoorVarPropUI* doorVar = entry->doorVariables[sub_index];
								char param_name[256];
								sprintf(buf, "Var #%d", sub_index + 1);
								sprintf(param_name, "doorVar%d", sub_index);
								wleAEWorldVariableDefAddWidget(wleAEGlobalInteractionPropUI.autoWidget, buf, "The Name of the map variable", param_name, &doorVar->var);

								if (!doorVar->var.is_specified && !doorVar->var.var_name_diff && !doorVar->var.var_init_from_diff && !doorVar->var.var_value_diff && !doorVar->var.spec_diff)
									break;
							}
						}
					}
				}

				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Transition", "Transition to play when entering this door", "TransitionOverride", &entry->doorTransition);

			}
			else if(entry->bShowDefValues) 
			{
				if(pPropEntry->pDoorProperties->eDoorType == WorldDoorType_Keyed) {
					sprintf(key, "%sDefDoorKey%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Door Key", "The name which must match the key name on a door key item in order for that item to create a door interaction.  A door key item's key name is the value of the map variable, \"MAP_ENTRY_KEY\" on the map where the item was created.", key, &labelParams, true);
					sprintf(key, "%ssDefDoorKeyValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(pPropEntry->pDoorProperties->pcDoorKey), key, &valueParams, false);
				} else if (pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) {
					sprintf(key, "%sDefDoorDestination%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Destination", "The destination map point (zone map, spawn point).  The spawn point is optional", key, &labelParams, true);
					sprintf(key, "%sDefDoorDestinationValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, WorldVariableDefToString(&pPropEntry->pDoorProperties->doorDest), key, &valueParams, false);
				}
				if (pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) {
					sprintf(key, "%sDefDoorPerPlayer%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Per Player", "Creates one interaction choice for each teammate who meets the Interact condition.  Door must go to an OWNED map.", key, &labelParams, true);
					sprintf(key, "%sDefDoorPerPlayerValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pDoorProperties->bPerPlayer ? "True" : "False", key, &valueParams, false);

					sprintf(key, "%sDefDoorSinglePlayer%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Single Player", "Creates one interaction choice that is not shared among teammates.  Door must go to an OWNED map.", key, &labelParams, true);
					sprintf(key, "%sDefDoorSinglePlayerValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pDoorProperties->bSinglePlayer ? "True" : "False", key, &valueParams, false);

					sprintf(key, "%sDefDoorIncludeTeammates%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Include Teammates", "Brings all of the interacting player's teammates along to the destination.", key, &labelParams, true);
					sprintf(key, "%sDefDoorIncludeTeammatesValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pDoorProperties->bIncludeTeammates ? "True" : "False", key, &valueParams, false);

					sprintf(key, "%sDefDoorCollectStatus%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Collect Status", "Collects summary information about the destination of this door.", key, &labelParams, true);
					sprintf(key, "%sDefDoorCollectStatusValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pDoorProperties->bCollectDestStatus ? "True" : "False", key, &valueParams, false);

                    sprintf(key, "%sDefDoorDestinationSameOwner%d", keyPrefix, iInteractionIndex);
                    ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Destination Map Has Same Owner", "The destination map should have the same owner as this map.", key, &labelParams, true);
                    sprintf(key, "%sDefDoorDestinationSameOwnerValue%d", keyPrefix, iInteractionIndex);
                    ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pDoorProperties->bDestinationSameOwner ? "True" : "False", key, &valueParams, false);

				}

				if (GET_REF(pPropEntry->pDoorProperties->hTransSequence))
				{
					sprintf(key, "%sDefDoorTransition%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Transition", "A sequence to play prior to moving to the new location.", key, &labelParams, true);
					sprintf(key, "%sDefDoorTransitionValue%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyed(root, REF_STRING_FROM_HANDLE(pPropEntry->pDoorProperties->hTransSequence), key, &valueParams, false);
				}

				if (pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate && pPropEntry->pDoorProperties->eaVariableDefs && eaSize(&pPropEntry->pDoorProperties->eaVariableDefs))
				{
					int i;

					sprintf(key, "%sDefDoorVariableHeader%d", keyPrefix, iInteractionIndex);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Variables To Set", "Map variables to set on the transition map.", key, &labelParams, true);

					labelParams.alignTo += WLE_AE_INTERACTION_INDENT;

					for(i=0; i < eaSize(&pPropEntry->pDoorProperties->eaVariableDefs); i++)
					{
						sprintf(key, "%sDefDoor%dVariable%d", keyPrefix, iInteractionIndex, i);
						sprintf(buf, "Variable #%d:", i);
						ui_RebuildableTreeAddLabelKeyed(root, buf, key, &labelParams, true);
						sprintf(key, "%sDefDoor%dVariableValue%d", keyPrefix, iInteractionIndex, i);
						ui_RebuildableTreeAddLabelKeyed(root, WorldVariableDefToString(pPropEntry->pDoorProperties->eaVariableDefs[i]), key, &valueParams, false);
					}
					labelParams.alignTo -= WLE_AE_INTERACTION_INDENT;
				}
			}
		}
	}

	wleAEInteraction_ShowEntryClassBody(root, pPropEntry, iInteractionIndex, keyPrefix, baseParams);
}

// Shows the entry's timing information
static void wleAEInteraction_ShowEntryTiming(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams)
{
	UIAutoWidgetParams headerParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];
	char buf[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	if (pPropEntry->pTimeProperties)
	{
		sprintf(key, "%sTimingHeading%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Timing", "Timing values defined by the entry.  If not set, these defaults are used: no use time, no active time, no cooldown, interrupted on move/power/damage.", key, baseParams, true);

		sprintf(key, "%sUseTime%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Use Time", "The time (in secs) required to interact.  Zero means no wait.", key, &labelParams, true);
		sprintf(key, "%sUseTimeValue%d", keyPrefix, iInteractionIndex);
		sprintf(buf, "%f", pPropEntry->pTimeProperties->fUseTime);
		ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);

		if(GET_REF(pPropEntry->pTimeProperties->msgUseTimeText.hMessage)) {
			sprintf(key, "%sUseTimeText%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Use Time Text", "A custom message to display while interacting with a use time.", key, &labelParams, true);
			sprintf(key, "%sUseTimeTextValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, TranslateDisplayMessage(pPropEntry->pTimeProperties->msgUseTimeText), key, &valueParams, false);
		}

		sprintf(key, "%sActiveTime%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Active Time", "The time (in secs) after interacting before cooldown starts.  Zero means no wait.", key, &labelParams, true);
		sprintf(key, "%sActiveTimeValue%d", keyPrefix, iInteractionIndex);
		sprintf(buf, "%f", pPropEntry->pTimeProperties->fActiveTime);
		ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);
		
		sprintf(key, "%sNoRespawn%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "No Respawn", "If true, the object will never end cooldown.", key, &labelParams, true);
		sprintf(key, "%sNoRespawnValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pTimeProperties->bNoRespawn ? "True" : "False", key, &valueParams, false);

		if (!pPropEntry->pTimeProperties->bNoRespawn)
		{
			sprintf(key, "%sCooldownTime%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Cooldown Time", "The time (in seconds) after when interaction is not allowed after it stops being active.  None=0, Short=30, Medium=300, Long=3600.", key, &labelParams, true);
			sprintf(key, "%sCooldownTimeValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, StaticDefineIntRevLookup(WorldCooldownTimeEnum, pPropEntry->pTimeProperties->eCooldownTime), key, &valueParams, false);

			if (pPropEntry->pTimeProperties->eCooldownTime == WorldCooldownTime_Custom)
			{
				sprintf(key, "%sCustomTime%d", keyPrefix, iInteractionIndex);
				ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Custom Time", "The time (in seconds) after when interaction is not allowed after it stops being active.  Zero means no cooldown.", key, &labelParams, true);
				sprintf(key, "%sCustomTimeValue%d", keyPrefix, iInteractionIndex);
				sprintf(buf, "%f", pPropEntry->pTimeProperties->fCustomCooldownTime);
				ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);
			}

			sprintf(key, "%sDynamicCooldown%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Dynamic Cooldown", "Whether this interactable should automatically adjust its cooldown period when other nearby interactables are also on cooldown.", key, &labelParams, true);
			sprintf(key, "%sDynamicCooldownValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, StaticDefineIntRevLookup(WorldDynamicSpawnTypeEnum, pPropEntry->pTimeProperties->eDynamicCooldownType), key, &valueParams, false);
		}
		sprintf(key, "%sTeamUsableWhenActive%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Usable By Team When Active", "If true, the object is usable by teammates of the interactor during its active period.", key, &labelParams, true);
		sprintf(key, "%sTeamUsableWhenActiveValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pTimeProperties->bTeamUsableWhenActive ? "True" : "False", key, &valueParams, false);

		sprintf(key, "%sHideDuringCooldown%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Hide On Cooldown", "If true, the object is not visible during the cooldown period.  Note that this is ignored if the Visible Expr has a value.", key, &labelParams, true);
		sprintf(key, "%sHideDuringCooldownValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pTimeProperties->bHideDuringCooldown ? "True" : "False", key, &valueParams, false);

		sprintf(key, "%sInterruptOnMove%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interrupt On Move", "If true, moving will interrupt the interact.", key, &labelParams, true);
		sprintf(key, "%sInterruptOnMoveValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pTimeProperties->bInterruptOnMove ? "True" : "False", key, &valueParams, false);

		sprintf(key, "%sInterruptOnPower%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interrupt On Power", "If true, using a power will interrupt the interact.", key, &labelParams, true);
		sprintf(key, "%sInterruptOnPowerValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pTimeProperties->bInterruptOnPower ? "True" : "False", key, &valueParams, false);

		sprintf(key, "%sInterruptOnDamage%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interrupt On Dmg", "If true, receiving damage will interrupt the interact.", key, &labelParams, true);
		sprintf(key, "%sInterruptOnDamageValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pTimeProperties->bInterruptOnDamage ? "True" : "False", key, &valueParams, false);
	}

}

// Shows the entry's sound properties
static void wleAEInteraction_ShowEntrySound(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams)
{
	UIAutoWidgetParams headerParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	if(pPropEntry->pSoundProperties)
	{
		sprintf(key, "%sSoundHeading%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Sounds", "Custom sounds defined by the entry to be played from the interactable.", key, baseParams, true);

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchAttemptSound)) {
			sprintf(key, "%sAttemptSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Attempt Sound", "Sound played from the interactable when interaction starts.", key, &labelParams, true);
			sprintf(key, "%sAttemptSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchAttemptSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchSuccessSound)) {
			sprintf(key, "%sSuccessSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Success Sound", "Sound played from the interactable upon success.", key, &labelParams, true);
			sprintf(key, "%sSuccessSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchSuccessSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchFailureSound)) {
			sprintf(key, "%sFailureSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Failure Sound", "Sound played from the interactable upon failure.", key, &labelParams, true);
			sprintf(key, "%sFailureSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchFailureSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchInterruptSound)) {
			sprintf(key, "%sInterruptSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interrupt Sound", "Sound played from the interactable when interact is interrupted.", key, &labelParams, true);
			sprintf(key, "%sInterruptSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchInterruptSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchMovementTransStartSound)) {
			sprintf(key, "%sMovementTransStartSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Movement Trans Start Sound", "Sound played from the interactable when movement transition begins.", key, &labelParams, true);
			sprintf(key, "%sMovementTransStartSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchMovementTransStartSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchMovementTransEndSound)) {
			sprintf(key, "%sMovementTransEndSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Movement Trans End Sound", "Sound played from the interactable when movement transition ends.", key, &labelParams, true);
			sprintf(key, "%sMovementTransEndSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchMovementTransEndSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchMovementReturnStartSound)) {
			sprintf(key, "%sMovementReturnStartSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Movement Return Start Sound", "Sound played from the interactable when movement return begins.", key, &labelParams, true);
			sprintf(key, "%sMovementReturnStartSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchMovementReturnStartSound, key, &valueParams, false);
		}

		if(EMPTY_TO_NULL(pPropEntry->pSoundProperties->pchMovementReturnEndSound)) {
			sprintf(key, "%sMovementReturnEndSound%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Movement Return End Sound", "Sound played from the interactable when movement return ends.", key, &labelParams, true);
			sprintf(key, "%sMovementReturnEndSoundValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, pPropEntry->pSoundProperties->pchMovementReturnEndSound, key, &valueParams, false);
		}
	}
}


// Shows the entry's actions
static void wleAEInteraction_ShowEntryActions(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams)
{
	UIAutoWidgetParams headerParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];
	char buf[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	if(pPropEntry->pActionProperties)
	{
		sprintf(key, "%sActionsHeading%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Actions", "Custom actions defined by the entry.  If not set, no actions are performed.", key, baseParams, true);

		if(pPropEntry->pActionProperties->pAttemptExpr && !exprIsEmpty(pPropEntry->pActionProperties->pAttemptExpr)) {
			sprintf(key, "%sActionsAttemptExpr%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Attempt Expr", "Expression run when start interacting.", key, &labelParams, true);
			sprintf(key, "%sActionsAttemptExprValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, exprGetCompleteString(pPropEntry->pActionProperties->pAttemptExpr), key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->pSuccessExpr && !exprIsEmpty(pPropEntry->pActionProperties->pSuccessExpr)) {
			sprintf(key, "%sActionsSuccessExpr%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Success Expr", "Expression run on success.", key, &labelParams, true);
			sprintf(key, "%sActionsSuccessExprValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root,exprGetCompleteString(pPropEntry->pActionProperties->pSuccessExpr), key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->pFailureExpr && !exprIsEmpty(pPropEntry->pActionProperties->pFailureExpr)) {
			sprintf(key, "%sActionsFailExpr%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Fail Expr", "Expression run on failure", key, &labelParams, true);
			sprintf(key, "%sActionsFailExprValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, exprGetCompleteString(pPropEntry->pActionProperties->pFailureExpr), key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->pInterruptExpr && !exprIsEmpty(pPropEntry->pActionProperties->pInterruptExpr)) {
			sprintf(key, "%sActionsInterruptExpr%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Interrupt Expr", "Expression run when interact is interrupted.", key, &labelParams, true);
			sprintf(key, "%sActionsInterruptExprValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, exprGetCompleteString(pPropEntry->pActionProperties->pInterruptExpr), key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->pNoLongerActiveExpr && !exprIsEmpty(pPropEntry->pActionProperties->pNoLongerActiveExpr)) {
			sprintf(key, "%sActionsNoLongerActiveExpr%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "No Longer Active Expr", "Expression run when it enters cooldown.", key, &labelParams, true);
			sprintf(key, "%sActionsNoLongerActiveExprValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, exprGetCompleteString(pPropEntry->pActionProperties->pNoLongerActiveExpr), key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->pCooldownExpr && !exprIsEmpty(pPropEntry->pActionProperties->pCooldownExpr)) {
			sprintf(key, "%sActionsCooldownExpr%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Cooldown Expr", "Expression run when it finishes cooldown.", key, &labelParams, true);
			sprintf(key, "%sActionsCooldownExprValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, exprGetCompleteString(pPropEntry->pActionProperties->pCooldownExpr), key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->successActions.eaActions) {
			sprintf(key, "%sActionsSuccessActions%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Success Actions", "Number of transactional actions to perform on success. (See interaction def for a list of the actions performed)", key, &labelParams, true);
			sprintf(key, "%sActionsSuccessActionsValue%d", keyPrefix, iInteractionIndex);
			sprintf(buf, "%d", eaSize(&pPropEntry->pActionProperties->successActions.eaActions));
			ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);
		}

		if(pPropEntry->pActionProperties->failureActions.eaActions) {
			sprintf(key, "%sActionsFailureActions%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Failure Actions", "Number of transactional actions to perform on failure. (See interaction def for a list of the actions performed)", key, &labelParams, true);
			sprintf(key, "%sActionsFailureActionsValue%d", keyPrefix, iInteractionIndex);
			sprintf(buf, "%d", eaSize(&pPropEntry->pActionProperties->failureActions.eaActions));
			ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);
		}
	}
}

// Shows the entry's rewards
static void wlAEInteraction_ShowEntryRewards(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams)
{
	UIAutoWidgetParams headerParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	char key[256];
	char buf[256];

	labelParams.alignTo = baseParams->alignTo + 20;
	valueParams.alignTo = baseParams->alignTo + 140;

	if(pPropEntry->pRewardProperties)
	{
		sprintf(key, "%sRewardsHeading%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Rewards", "Rewards defined by the entry.  If not set, no rewards are granted.", key, baseParams, true);

		if(GET_REF(pPropEntry->pRewardProperties->hRewardTable)) {
			sprintf(key, "%sRewardTable%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Reward Table", "Reward table granted on successful interact.", key, &labelParams, true);
			sprintf(key, "%sRewardTableValue%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyed(root, NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pPropEntry->pRewardProperties->hRewardTable)), key, &valueParams, false);
		}

		sprintf(key, "%sRewardLevel%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Reward Level", "Where to get the level used when generating rewards.", key, &labelParams, true);
		sprintf(key, "%sRewardLevelValue%d", keyPrefix, iInteractionIndex);
		ui_RebuildableTreeAddLabelKeyed(root, StaticDefineIntRevLookup(WorldRewardLevelTypeEnum, pPropEntry->pRewardProperties->eRewardLevelType), key, &valueParams, false);

		if (pPropEntry->pRewardProperties->eRewardLevelType == WorldRewardLevelType_Custom)
		{
			sprintf(key, "%sCustomLevel%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Custom Level", "The level to use when generating rewards.", key, &labelParams, true);
			sprintf(key, "%sCustomLevelValue%d", keyPrefix, iInteractionIndex);
			sprintf(buf, "%d", pPropEntry->pRewardProperties->uCustomRewardLevel);
			ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);
		}
		
		if (pPropEntry->pRewardProperties->eRewardLevelType == WorldRewardLevelType_MapVariable)
		{
			sprintf(key, "%sMapVar%d", keyPrefix, iInteractionIndex);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(root, "Map Variable", "The map variable to use as level when generating rewards.", key, &labelParams, true);
			sprintf(key, "%sMapVarValue%d", keyPrefix, iInteractionIndex);
			sprintf(buf, "%s", pPropEntry->pRewardProperties->pcMapVarName);
			ui_RebuildableTreeAddLabelKeyed(root, buf, key, &valueParams, false);
		}
	}
}

// Adds the entire entry in an uneditable form to the rebuildable tree.  Uses baseParams for indentation.  Appends index and prepends keyPrefix to 
// each key to make sure keys are unique.  This handles the case  where several interaction lists are being displayed (i.e. showing an entire encounter)
void wleAEInteractionPropShowEntry(UIRTNode *pRoot, WorldInteractionPropertyEntry* pPropEntry, int index, const char* keyPrefix, UIAutoWidgetParams *baseParams)
{
	InteractionDef *pDef = pPropEntry ? GET_REF(pPropEntry->hInteractionDef) : NULL;
	WorldInteractionPropertyEntry *pDefEntry = pDef ? pDef->pEntry : NULL;
	WorldInteractionPropertyEntry *pTempEntry = NULL;
	char buf[256];
	char key[256];
	UIAutoWidgetParams headerParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};

	if(!pPropEntry || !pRoot || !baseParams)
		return;


	sprintf(key, "%sInteractionHeading%d", keyPrefix, index+1);
	sprintf(buf, "Interaction #%d", index+1);
	ui_RebuildableTreeAddLabelKeyed(pRoot, buf, key, baseParams, true);

	headerParams.alignTo = baseParams->alignTo + 20;
	labelParams.alignTo = headerParams.alignTo + 20;
	valueParams.alignTo = labelParams.alignTo + 120;

	// 			sprintf(key, "%sInteractionClassLabel%d", keyPrefix, index+1);
	// 			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Class", "How the interaction is initiated.", key, &labelParams, true);
	// 			sprintf(key, "%sInteractionClassValue%d", keyPrefix, index+1);
	// 			ui_RebuildableTreeAddLabelKeyed(pRoot, pPropEntry->pcInteractionClass, key, &labelParams, true);

	wleAEInteraction_ShowEntryClass(pRoot, pPropEntry, index, keyPrefix, &headerParams);

	sprintf(key, "%sGeneralProperties%d", keyPrefix, index+1);
	ui_RebuildableTreeAddLabelKeyed(pRoot, "General Properties", key, &headerParams, true);

	// Basic properties
	if(!pDefEntry || pPropEntry->bOverrideInteract) {
		pTempEntry = pPropEntry;
	} else {
		pTempEntry = pDefEntry;
	}

	if(pTempEntry && pTempEntry->pcInteractionClass &&
		(	
		stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0||
		stricmp(pTempEntry->pcInteractionClass, "Contact") == 0||
		stricmp(pTempEntry->pcInteractionClass, "CraftingStation") == 0||
		stricmp(pTempEntry->pcInteractionClass, "Door")	== 0||
		stricmp(pTempEntry->pcInteractionClass, "Gate") == 0||
		stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0
		)						 
		) 
	{
		if(!exprIsEmpty(pTempEntry->pInteractCond)) {
			sprintf(key, "%sInteractCondLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Interact Expr", INTERACT_INTERACTCOND_TOOLTIP, key, &labelParams, true);
			sprintf(key, "%sInteractCondValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, NULL_TO_EMPTY(exprGetCompleteString(pTempEntry->pInteractCond)), key, &valueParams, false);
		}

		if(!exprIsEmpty(pTempEntry->pAttemptableCond)) {
			sprintf(key, "%sAttemptableCondLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Usable Expr", INTERACT_ATTEMPTABLECOND_TOOLTIP, key, &labelParams, true);
			sprintf(key, "%sAttemptableCondValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, NULL_TO_EMPTY(exprGetCompleteString(pTempEntry->pAttemptableCond)), key, &valueParams, false);
		}
		
		if(!exprIsEmpty(pTempEntry->pSuccessCond)) {
			sprintf(key, "%sSuccessCondLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Success Expr", INTERACT_SUCCESSCOND_TOOLTIP, key, &labelParams, true);
			sprintf(key, "%sSuccessCondValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, NULL_TO_EMPTY(exprGetCompleteString(pTempEntry->pSuccessCond)), key, &valueParams, false);
		}
	}

	// Visibility
	if(!pDefEntry || pPropEntry->bOverrideVisibility) {
		pTempEntry = pPropEntry;
	} else {
		pTempEntry = pDefEntry;
	}

	if(pTempEntry && !exprIsEmpty(pTempEntry->pVisibleExpr)) {
		sprintf(key, "%sVisibleExprLabel%d", keyPrefix, index+1);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Visible Expr", "When true, the node is visible.  Otherwise it disappears.  If this expression has a value, it always supercedes any hide on cooldown.", key, &labelParams, true);
		sprintf(key, "%sVisibleExprValue%d", keyPrefix, index+1);
		ui_RebuildableTreeAddLabelKeyed(pRoot, NULL_TO_EMPTY(exprGetCompleteString(pTempEntry->pVisibleExpr)), key, &valueParams, false);
	}

	// Category
	if(!pDefEntry || pPropEntry->bOverrideVisibility) {
		pTempEntry = pPropEntry;
	} else {
		pTempEntry = pDefEntry;
	}

	if(pTempEntry && pTempEntry->pcInteractionClass &&
		(	
		stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0 ||
		stricmp(pTempEntry->pcInteractionClass, "Contact") == 0||
		stricmp(pTempEntry->pcInteractionClass, "CraftingStation") == 0||
		stricmp(pTempEntry->pcInteractionClass, "Door") == 0||
		stricmp(pTempEntry->pcInteractionClass, "Gate") == 0||
		stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0
		)						 
		)
	{
		if(EMPTY_TO_NULL(pTempEntry->pcCategoryName)) {
			sprintf(key, "%sCategoryLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Category", "An optional hint to the UI about what category of interaction this is.", key, &labelParams, true);
			sprintf(key, "%sCategoryValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, NULL_TO_EMPTY(pTempEntry->pcCategoryName), key, &valueParams, false);
		}
		sprintf(key, "%sPriorityLabel%d", keyPrefix, index+1);
		ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Priority", "An optional hint to the UI about the priority of this interaction.", key, &labelParams, true);
		sprintf(key, "%sPriorityValue%d", keyPrefix, index+1);
		ui_RebuildableTreeAddLabelKeyed(pRoot, StaticDefineIntRevLookup(WorldOptionalActionPriorityEnum, pTempEntry->iPriority), key, &valueParams, false);
	}

	// Timing properties
	if(pPropEntry && !pPropEntry->pTimeProperties && pDefEntry) {
		pTempEntry = pDefEntry;
	} else {
		pTempEntry = pPropEntry;
	}

	if ( pTempEntry && pTempEntry->pcInteractionClass && 
		(	
		stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0 ||
		stricmp(pTempEntry->pcInteractionClass, "Contact") == 0 ||
		stricmp(pTempEntry->pcInteractionClass, "Door") == 0 ||
		stricmp(pTempEntry->pcInteractionClass, "Gate") == 0||
		stricmp(pPropEntry->pcInteractionClass, "NamedObject") == 0 ||
		stricmp(pPropEntry->pcInteractionClass, "FromDefinition") == 0
		)						 
		) 
	{
		wleAEInteraction_ShowEntryTiming(pRoot, pTempEntry, index, keyPrefix, &headerParams);
	}


	// Animation properties
	if(pPropEntry && !pPropEntry->pAnimationProperties && pDefEntry) {
		pTempEntry = pDefEntry;
	} else {
		pTempEntry = pPropEntry;
	}

	if (pTempEntry && pTempEntry->pcInteractionClass && 
		((stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Contact") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "CraftingStation") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Door") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Gate") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0))
		)
	{
		if(pTempEntry->pAnimationProperties && GET_REF(pTempEntry->pAnimationProperties->hInteractAnim)) {
			sprintf(key, "%sAnimationLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Interact Anim", "Animation for the player to execute while interacting.", key, &labelParams, true);
			sprintf(key, "%sAnimationValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, REF_STRING_FROM_HANDLE(pTempEntry->pAnimationProperties->hInteractAnim), key, &valueParams, false);
		}
	}

	// Sound properties
	if(pPropEntry && !pPropEntry->pSoundProperties && pDefEntry) {
		pTempEntry = pDefEntry;
	} else {
		pTempEntry = pPropEntry;
	}

	if (pTempEntry && pTempEntry->pcInteractionClass && 
		((stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Contact") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "CraftingStation") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Door") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Gate") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0))
		)
	{
		wleAEInteraction_ShowEntrySound(pRoot, pTempEntry, index, keyPrefix, &headerParams);
	}

	// Action properties
	if(pPropEntry && !pPropEntry->pActionProperties && pDefEntry) {
		pTempEntry = pDefEntry;
	} else {
		pTempEntry = pPropEntry;
	}

	if (pTempEntry && pTempEntry->pcInteractionClass && 
		((stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Contact") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Door") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Gate") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0))
		)
	{
		wleAEInteraction_ShowEntryActions(pRoot, pTempEntry, index, keyPrefix, &headerParams);
	}

	// Text properties
	if(pPropEntry && !pPropEntry->pTextProperties && pDefEntry) {
		pTempEntry = pDefEntry;
	} else {
		pTempEntry = pPropEntry;
	}

	if (pTempEntry && pTempEntry->pcInteractionClass && 
		((stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Contact") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "CraftingStation") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Door") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "Gate") == 0) ||
		(stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0))
		)
	{
		if(pTempEntry->pTextProperties && GET_REF(pTempEntry->pTextProperties->usabilityOptionText.hMessage)) {
			sprintf(key, "%sUsabilityTextLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Usability Text", "The text displayed to indicate usability requirements. (optional)", key, &labelParams, true);
			sprintf(key, "%sUsabilityTextValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, TranslateDisplayMessage(pTempEntry->pTextProperties->usabilityOptionText), key, &valueParams, false);
		}

		if(pTempEntry->pTextProperties && GET_REF(pTempEntry->pTextProperties->interactOptionText.hMessage)) {
			sprintf(key, "%sInteractTextLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Interact Text", "The text displayed before interacting. (optional)", key, &labelParams, true);
			sprintf(key, "%sInteractTextValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, TranslateDisplayMessage(pTempEntry->pTextProperties->interactOptionText), key, &valueParams, false);
		}

		if(pTempEntry->pTextProperties && GET_REF(pTempEntry->pTextProperties->interactDetailText.hMessage)) {
			sprintf(key, "%sInteractDetailTextLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Detail Text", "Auxiliary text that can be used by the UI. (optional)", key, &labelParams, true);
			sprintf(key, "%sInteractDetailTextValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, TranslateDisplayMessage(pTempEntry->pTextProperties->interactDetailText), key, &valueParams, false);
		}

		if(pTempEntry->pTextProperties && EMPTY_TO_NULL(pTempEntry->pTextProperties->interactDetailTexture)) {
			UISprite *sprite = ui_SpriteCreate(0, 0, 64, 64, pTempEntry->pTextProperties->interactDetailTexture);
			sprintf(key, "%sInteractDetailTextureLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Detail Texture", "Auxiliary texture that can be used by the UI. (optional)", key, &labelParams, true);
			sprintf(key, "%sInteractDetailTextureValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddWidget(pRoot, UI_WIDGET(sprite), NULL, false, key, &valueParams);
		}

		if(pTempEntry->pTextProperties && GET_REF(pTempEntry->pTextProperties->successConsoleText.hMessage)) {
			sprintf(key, "%sSuccessTextLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Success Text", "The text displayed on successful interact. (optional)", key, &labelParams, true);
			sprintf(key, "%sSuccessTextValue%d", keyPrefix, index+1);
		}

		if(pTempEntry->pTextProperties && GET_REF(pTempEntry->pTextProperties->failureConsoleText.hMessage)) {
			sprintf(key, "%sFailureTextLabel%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyedWithTooltip(pRoot, "Failure Text", "The text displayed on failed interact. (optional)", key, &labelParams, true);
			sprintf(key, "%sFailureTextValue%d", keyPrefix, index+1);
			ui_RebuildableTreeAddLabelKeyed(pRoot, TranslateDisplayMessage(pTempEntry->pTextProperties->failureConsoleText), key, &valueParams, false);
		}
	}

	// Reward properties
	if(!pPropEntry->pRewardProperties && pDefEntry) {
		pTempEntry = pDefEntry;
	} else {
		pTempEntry = pPropEntry;
	}

	if (pTempEntry)
	{
		if(pTempEntry->pcInteractionClass && (stricmp(pTempEntry->pcInteractionClass, "Clickable") == 0 || stricmp(pTempEntry->pcInteractionClass, "FromDefinition") == 0))
			wlAEInteraction_ShowEntryRewards(pRoot, pTempEntry, index, keyPrefix, &headerParams);
	}
}



/********************
* MAIN
********************/
static bool wleAEInteractionHasProps(GroupDef *def)
{
	return (def->property_structs.interaction_properties || def->property_structs.server_volume.interaction_volume_properties);
}

static bool wleAEActorInteractionHasProps(WorldActorProperties *pActor)
{
	return !!(pActor->pInteractionProperties);
}


int wleAEInteractionPropReload(EMPanel *panel, EditorObject *edObj)
{
	DictionaryEArrayStruct *pInteractionDefs = resDictGetEArrayStruct("InteractionDef");
	EditorObject **objects = NULL;
	EditorObject *trackerObj = NULL;
	WorldInteractionProperties *properties = NULL;
	bool panelActive = true;
	bool hide = false;
	int numChildren = -1;
	bool common_scope = false;
	bool hasProps = false;
	WorldScope *closest_scope = NULL;
	UIAutoWidgetParams params = {0};
	UIAutoWidgetParams indentParams = {0};
	UIAutoWidgetParams labelParams = {0};
	UIAutoWidgetParams valueParams = {0};
	StashTableIterator iter;
	StashElement elem;
	int i;
	int index, sub_index;
	int numMoveDescriptors = 0;
	bool bActorObjects = false;
	bool all_volumes = true;
	bool is_volume = false;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *tracker;

		assert(objects[i]->type->objType == EDTYPE_TRACKER || objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR);
		if(objects[i]->type->objType == EDTYPE_TRACKER)
		{
			tracker = trackerFromTrackerHandle(objects[i]->obj);
			if (!tracker || !tracker->def)
			{
				panelActive = false;
				all_volumes = false;
				continue;
			}

			if (wleNeedsEncounterPanels(tracker->def))
			{
				hide = true;
				break;
			}

			if (!tracker->def->property_structs.volume || tracker->def->property_structs.volume->bSubVolume)
			{
				all_volumes = false;
			}

			if (!hasProps && wleAEInteractionHasProps(tracker->def))
				hasProps = true;

			if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
				panelActive = false;
			if (numChildren == -1 || tracker->child_count < numChildren)
				numChildren = tracker->child_count;

			// Hide some panels if objects are not in the same scope
			if (!closest_scope)
			{
				common_scope = true;
				closest_scope = tracker->closest_scope;
			}
			else if (closest_scope != tracker->closest_scope)
			{
				common_scope = false;
			}
		}
		else if(objects[i]->type->objType == EDTYPE_ENCOUNTER_ACTOR)
		{
			WleEncObjSubHandle *subHandle = objects[i]->obj;
			WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, NULL);

			if (!actor) {
				hide = true;
				continue;
			} else {
				if (!wleTrackerIsEditable(subHandle->parentHandle, false, false, false))
					panelActive = false;

				common_scope = false;
				bActorObjects = true;

				if (!hasProps && wleAEActorInteractionHasProps(actor))
					hasProps = true;
			}

			// Hide some panels if volumes are not in the same scope
			tracker = trackerFromTrackerHandle(subHandle->parentHandle);
			if (!closest_scope)
			{
				common_scope = true;
				closest_scope = tracker->closest_scope;
			}
			else if (closest_scope != tracker->closest_scope)
			{
				common_scope = false;
			}

			all_volumes = false;
		}
		else
		{
			all_volumes = false;
		}
	}
	if (i == 1)
		trackerObj = objects[0];
	eaDestroy(&objects);

	if (trackerObj && trackerObj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(trackerObj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		properties = def ? def->property_structs.interaction_properties : NULL;
	}
	if (trackerObj && trackerObj->type->objType == EDTYPE_ENCOUNTER_ACTOR)
	{
		WleEncObjSubHandle *subHandle = trackerObj->obj;
		WorldActorProperties *actor = wleEncounterActorFromHandle(subHandle, NULL);
		properties = actor ? actor->pInteractionProperties : NULL;
	}

	if (hide)
		return WLE_UI_PANEL_INVALID;

	params.alignTo = WLE_AE_INTERACTION_HEADING;
	indentParams.alignTo = WLE_AE_INTERACTION_INDENT;
	labelParams.alignTo = WLE_AE_INTERACTION_INDENT_LABEL;
	valueParams.alignTo = WLE_AE_INTERACTION_INDENT_VALUE;

	// update spawn point names combo box values
	eaDestroyEx(&wleAEGlobalInteractionPropUI.spawnPointNames, NULL);
	if (common_scope && closest_scope && closest_scope->name_to_obj)
	{
		stashGetIterator(closest_scope->name_to_obj, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			WorldEncounterObject *obj = stashElementGetPointer(elem);
			if (obj->type == WL_ENC_SPAWN_POINT) {
				eaPush(&wleAEGlobalInteractionPropUI.spawnPointNames, strdup(stashElementGetStringKey(elem)));
			} else if (obj->type == WL_ENC_LOGICAL_GROUP) {
				WorldLogicalGroup *logical_group = (WorldLogicalGroup*)obj;
				for(i=eaSize(&logical_group->objects)-1; i>=0; --i) {
					if (logical_group->objects[i] && 
						logical_group->objects[i]->type == WL_ENC_SPAWN_POINT) {
						eaPush(&wleAEGlobalInteractionPropUI.spawnPointNames, strdup(worldScopeGetObjectName(closest_scope, obj)));
						break;
					}
				}
			}
		}
		eaQSort(wleAEGlobalInteractionPropUI.spawnPointNames, wleAEInteractCompareStrings);
	}

	// update map names combo box values
	eaClear(&wleAEGlobalInteractionPropUI.mapNames);
	{
		RefDictIterator zm_iter;
		ZoneMapInfo *zminfo;
		worldGetZoneMapIterator(&zm_iter);
		while (zminfo = worldGetNextZoneMap(&zm_iter))
		{
			eaPush(&wleAEGlobalInteractionPropUI.mapNames, (char*)allocAddString(zmapInfoGetPublicName(zminfo)));
		}
	}

	if(all_volumes)
		is_volume = (stricmp(wleAEGlobalInteractionPropUI.data.applyAsNode.stringvalue, "Volume") == 0);
	else 
		is_volume = false;

	for(index=0; index<eaSize(&wleAEGlobalInteractionPropUI.entries); ++index)
	{
		// Fill the interaction combo list appropriately
		if(bActorObjects)
		{
			eaClear(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values);
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Clickable"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Contact"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("CraftingStation"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Door"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("FromDefinition"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("TeamCorral"));
		}
		else
		{
			eaClear(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values);
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Ambientjob"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("CombatJob"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Chair"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Clickable"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Contact"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("CraftingStation"));
			if(!is_volume)
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Destructible"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Door"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("FromDefinition"));
			if(!is_volume)
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Gate"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("NamedObject"));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("TeamCorral"));
		}

		// Interaction Class
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass);
		
		// Basic properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isInteract);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactCond);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->attemptableCond);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->successCond);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isVisible);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->visibleExpr);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isCategory);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->optActCategory);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->priority);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->exclusive);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->autoExec);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->disablePowersInterrupt);
		if (!gConf.bAlwaysAllowInteractsInCombat)
			wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->allowDuringCombat);
		//wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->visibleFX);
		
		// Destructible properties
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->critterDef);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->onDeathPower);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->respawnTime);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->entityName);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->displayName);
		wleAEIntUpdate(&wleAEGlobalInteractionPropUI.entries[index]->critterLevel);

		// Contact properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isContact);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->contactDef);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->contactDialog);

		// Crafting station properties
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->craftingSkill);
		wleAEIntUpdate(&wleAEGlobalInteractionPropUI.entries[index]->maxSkill);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef);

		// Door properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isDoor);
		wleAEWorldVariableDefUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorDest);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorTransition);
		
		wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables.disabled = wleAEWorldVariableDefDoorHasVarsDisabled( &wleAEGlobalInteractionPropUI.entries[index]->doorDest );
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->perPlayerDoor);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->singlePlayerDoor);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->includeTeammatesDoor);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->collectDestStatus);
        wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->destinationSameOwner);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->queueDef);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorType);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorID);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorKey);

		// Chair properties
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold);

		// Gate properties
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->volumeTriggered);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->startState);

		for(sub_index=0; sub_index<MAX_VARIABLES; ++sub_index)
		{
			WorldVariable* mapDest = wleAEWorldVariableCalcVariableNonRandom( &wleAEGlobalInteractionPropUI.entries[index]->doorDest.var_def );
			wleAEGlobalInteractionPropUI.entries[index]->doorVariables[sub_index]->var.dest_map_name = SAFE_MEMBER(mapDest, pcZoneMap);
			wleAEWorldVariableDefUpdate(&wleAEGlobalInteractionPropUI.entries[index]->doorVariables[sub_index]->var);
		}

		// From Definition properties
		eaClear(&wleAEGlobalInteractionPropUI.entries[index]->interactionDef.available_values);
		for(i=0; i<eaSize(&pInteractionDefs->ppReferents); ++i) {
			InteractionDef *pDef = pInteractionDefs->ppReferents[i];
			if(bActorObjects)
			{
				if ((pDef->eType == InteractionDefType_Entity) || (pDef->eType == InteractionDefType_Any)) {
					eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionDef.available_values, (char*)pDef->pcName);
				}
			}
			else
			{
				if ((pDef->eType == InteractionDefType_Node) || (pDef->eType == InteractionDefType_Any)) {
					eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionDef.available_values, (char*)pDef->pcName);
				}
			}
		}
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactionDef);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->showDefValues);

		// Timing properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isTiming);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->useTime);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->useTimeText);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->activeTime);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->cooldownType);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->teamUsableWhenActive);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->hideDuringCooldown);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interruptOnPower);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interruptOnDamage);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interruptOnMove);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->noRespawn);

		// Animation properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isAnimation);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactAnim);

		// Ambient Job
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isForCritters);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isForCivilians);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->ambientJobInitialJob);
		wleAEIntUpdate(&wleAEGlobalInteractionPropUI.entries[index]->ambientJobPriority);
				

		// Sound properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->attemptSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->successSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->failureSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interruptSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->movementTransStartSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->movementTransEndSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->movementReturnStartSound);
		wleAETextUpdate(&wleAEGlobalInteractionPropUI.entries[index]->movementReturnEndSound);

		// Action properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isAction);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->attemptExpr);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->successExpr);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->failExpr);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interruptExpr);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr);
		wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr);
		wleAEGameActionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->successActions);
		wleAEGameActionUpdate(&wleAEGlobalInteractionPropUI.entries[index]->failureActions);

		// Text properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isText);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactOptionText);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactDetailText);
		wleAETextureUpdate(&wleAEGlobalInteractionPropUI.entries[index]->interactDetailTexture);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->successText);
		wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.entries[index]->failureText);
		
		// Reward properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isReward);
		wleAEDictionaryUpdate(&wleAEGlobalInteractionPropUI.entries[index]->rewardDef);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType);
		wleAEIntUpdate(&wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel);
		wleAEComboUpdate(&wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable);

		// Motion properties
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->isMotion);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->transitionTime);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->destinationTime);
		wleAEFloatUpdate(&wleAEGlobalInteractionPropUI.entries[index]->returnTime);
		wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->transDuringUse);
		for (sub_index = 0; sub_index < MAX_MOVE_DESCRIPTORS; ++sub_index)
		{
			wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.entries[index]->moveDescriptors[sub_index]->specified);
			wleAEIntUpdate(&wleAEGlobalInteractionPropUI.entries[index]->moveDescriptors[sub_index]->startChildIdx);
			wleAEIntUpdate(&wleAEGlobalInteractionPropUI.entries[index]->moveDescriptors[sub_index]->destChildIdx);
			wleAEVec3Update(&wleAEGlobalInteractionPropUI.entries[index]->moveDescriptors[sub_index]->destPos);
			wleAEVec3Update(&wleAEGlobalInteractionPropUI.entries[index]->moveDescriptors[sub_index]->destRot);
		}
	}

	// Child properties
	wleAEIntUpdate(&wleAEGlobalInteractionPropUI.data.childSelect);
	wleAEExpressionUpdate(&wleAEGlobalInteractionPropUI.data.childSelectExpr);
	wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.data.allowExplicitHide);
	wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.data.startsHidden);

	wleAEComboUpdate(&wleAEGlobalInteractionPropUI.data.applyAsNode);
	wleAEIntUpdate(&wleAEGlobalInteractionPropUI.data.interactDist);
	wleAEIntUpdate(&wleAEGlobalInteractionPropUI.data.targetDist);
	wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.data.isUntargetable);
	wleAEComboUpdate(&wleAEGlobalInteractionPropUI.data.overrideFX);
	wleAEComboUpdate(&wleAEGlobalInteractionPropUI.data.additionalFX);
	wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.data.canTabSelect);
	wleAEMessageUpdate(&wleAEGlobalInteractionPropUI.data.displayNameBasic);
	wleAETextUpdate(&wleAEGlobalInteractionPropUI.data.interactionTypeTag);
	wleAEBoolUpdate(&wleAEGlobalInteractionPropUI.data.bVisEvalPerEnt);


	// rebuild UI
	ui_RebuildableTreeInit(wleAEGlobalInteractionPropUI.autoWidget, &wleAEGlobalInteractionPropUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);

	// where to apply the properties (if ambiguous between volumes and regular interaction nodes)
	if (all_volumes)
		wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Apply to", "This option only appears for volumes; the following interaction properties can either be applied to the volume (for typical volume interaction behavior), or the properties can be applied to make the volume and actual server-and-client node, which is, in most cases NOT desired.", "apply_as_node", &wleAEGlobalInteractionPropUI.data.applyAsNode);

	for(index=0; index<eaSize(&wleAEGlobalInteractionPropUI.entries); ++index)
	{
		char buf[256];
		WleAEInteractionEntryPropUI* entry = wleAEGlobalInteractionPropUI.entries[index];
		WorldInteractionPropertyEntry *pPropEntry = properties && properties->eaEntries && index < eaSize(&properties->eaEntries) ? properties->eaEntries[index] : NULL;

		// NOTE: When making fields visible or not visible for a given type
		//       be sure to update the InteractionClass apply function near
		//       the "VISIBLE FIELDS" tag.  That code actively cleans up fields
		//       based on type and if it gets out of sync with the code here
		//       you can end up with things being mysteriously blanked

		// Interaction class
		sprintf(buf, "Class #%d", index+1);
		wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, buf, "How the interaction is initiated.", "pcInteractionClass", &entry->interactionClass);

		if (entry->interactionClass.is_specified && entry->interactionClass.stringvalue && entry->interactionClass.stringvalue[0])
		{
			InteractionDef *pPropDef = pPropEntry && stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 ? GET_REF(pPropEntry->hInteractionDef) : NULL;
			WorldInteractionPropertyEntry *pDefEntry = pPropDef ? pPropDef->pEntry : NULL;

			// Destructible-only properties
			if (stricmp(entry->interactionClass.stringvalue, "Destructible") == 0)
			{
				sprintf(buf, "DestructibleProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Destructible Properties", buf, &params, true);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Critter", "What critter def this should turn into. (required)", "critterdef", &entry->critterDef);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Override", "Overrides for the specified critter def. (optional)", "critteroverridedef", &entry->critterOverrideDef);
				wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Respawn Time", "The amount of time in seconds before the node re-spawns.", "respawntime", &entry->respawnTime, 0, 1000000, 1);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "On Death Power", "The power to be executed when the object is destroyed. (optional)","ondeathpower", &entry->onDeathPower);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Entity Name", "The entity name to be used when created. (optional)", "entityname", &entry->entityName);
				wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Display Name", "The Display name for the object. (optional) If not provided, the object will have no name.", "display_name_msg", &entry->displayName);
				wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Critter Level", "The spawned level of the critter.  If zero, the map level is used instead.", "critterlevel", &entry->critterLevel, 0, 60, 1);
			}

			// Ambient Job Only Properties
			if (stricmp(entry->interactionClass.stringvalue, "Ambientjob") == 0)
			{
				
			}

			// Chair-only properties
			if (stricmp(entry->interactionClass.stringvalue, "Chair") == 0)
			{
				sprintf(buf, "ChairProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Chair Properties", buf, &params, true);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Move Anims", "Comma-delimited list of anim bits to use when moving between standing and sitting positions.", "chairbithandlespre", &entry->chairBitHandlesPre);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Sit Hold Anims", "Comma-delimited list of anim bits to use when holding at the sitting position.", "chairbithandleshold", &entry->chairBitHandlesHold);
				wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Sit/Stand Time", "Time (in seconds) it takes to move between standing and sitting positions.", "chairtimetomove", &entry->chairTimeToMove, 0.0f, 10.0f, 0.1f);
				wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Stand Hold Time", "Time (in seconds) the character holds in place after standing.", "chairstandholdtime", &entry->chairTimePostHold, 0.0f, 10.0f, 0.1f);
			}
				
			// Contact-only properties
			if (stricmp(entry->interactionClass.stringvalue, "Contact") == 0)
			{
				sprintf(buf, "ContactProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Contact Properties", buf, &params, true);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Contact", "Which contact def should be used. (required)", "contactdef", &entry->contactDef);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dialog Name", "Which dialog on the contact def should be used. (optional)", "contactdialog", &entry->contactDialog);
			}

			// Crafting Station-only properties
			if (stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0)
			{
				sprintf(buf, "CraftingStationProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Crafting Station Properties", buf, &params, true);
				wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Skill Type", "The skill type of this crafting station.", "craftingskill", &entry->craftingSkill);
				wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Max Skill", "The maximum skill this station operates at (caps the player's skill).  -1 means no maximum.", "maxskill", &entry->maxSkill, -1, 10000, 1);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Crafting Reward", "Reward table granted on successful crafting. (optional)", "craftrewardtable", &entry->craftRewardDef);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Deconstruct Reward", "Reward table granted on successful deconstruct. (optional)", "deconstructrewardtable", &entry->deconstructRewardDef);
				wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Experiment Reward", "Reward table granted on successful experiment. (optional)", "experimentrewardtable", &entry->experimentRewardDef);
			}

			// Gate-only properties
			if (stricmp(entry->interactionClass.stringvalue, "Gate") == 0)
			{
				sprintf(buf, "GateProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Gate Properties", buf, &params, true);
				wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Critter Use Cond", "This expression determines whether critters can open this gate.", "gatecritteruse", &entry->critterUseExpr);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Volume Triggered", "This makes the gate open and close solely based on whether an entity is inside of the current groupdef's volume.", "gatevolumetriggered", &entry->volumeTriggered);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Start Open", "Setting this sets the gate to start in the open state.", "gatestartstate", &entry->startState);
			}

			// Door-only properties
			if (common_scope && (stricmp(entry->interactionClass.stringvalue, "Door") == 0))
			{
				sprintf(buf, "DoorProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Door Properties", buf, &params, true);
				wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door Type", "Sets the door type", "doorType", &entry->doorType);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door ID", "Used for joining teammates together when a teammate uses a 'JoinTeammate' door.", "doorID", &entry->doorID);

				if (stricmp(entry->doorType.stringvalue, "QueuedInstance") == 0) {
					wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Queued Instance", "If the door type is \"Queued Instance\"; this is the instance it will queue the player.", "queuedef", &entry->queueDef);
				} else {
					if(stricmp(entry->doorType.stringvalue, "Keyed") == 0) {
						wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door Key", "The name which must match the key name on a door key item in order for that item to create a door interaction.  A door key item's key name is the value of the map variable, \"MAP_ENTRY_KEY\" on the map where the item was created.", "DoorKey", &entry->doorKey);
					} else if (stricmp(entry->doorType.stringvalue, "JoinTeammate") != 0) {
						wleAEWorldVariableDefAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Destination", "Determines how the destination will be chosen.", "doorDest", &entry->doorDest);
					}

					if (stricmp(entry->doorType.stringvalue, "JoinTeammate") != 0)
					{
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Per Player", "Creates one interaction choice for each teammate who meets the Interact condition.  Door must go to an OWNED map.", "PerPlayerDoor", &entry->perPlayerDoor);
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Single Player", "Creates one interaction choice that is not shared among teammates.  Door must go to an OWNED map.", "SinglePlayerDoor", &entry->singlePlayerDoor);
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Include Teammates", " Brings all of the interacting player's teammates along to the destination.", "IncludeTeammatesDoor", &entry->includeTeammatesDoor);
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Collect Destination Status", "Collects summary information about the destination of this door.", "CollectDestStatus", &entry->collectDestStatus);
                        wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Destination Map Has Same Owner", "The destination map should have the same owner as this map", "DestinationSameOwner", &entry->destinationSameOwner);

                        if(!bActorObjects) {
							wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Door Variables", "Whether or not the door passes variables to the map.", "HasVariables", &entry->doorHasVariables);
							if (entry->doorHasVariables.boolvalue)
							{
								for(sub_index=0; sub_index < MAX_VARIABLES; ++sub_index) {
									WleAEInteractionDoorVarPropUI* doorVar = entry->doorVariables[sub_index];
									char param_name[256];
									sprintf(buf, "Var #%d", sub_index + 1);
									sprintf(param_name, "doorVar%d", sub_index);
									wleAEWorldVariableDefAddWidget(wleAEGlobalInteractionPropUI.autoWidget, buf, "The Name of the map variable", param_name, &doorVar->var);

									if (!doorVar->var.is_specified && !doorVar->var.var_name_diff && !doorVar->var.var_init_from_diff && !doorVar->var.var_value_diff && !doorVar->var.spec_diff)
										break;
								}
							}
						}
					}

					wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Transition", "Transition to play when entering this door", "TransitionOverride", &entry->doorTransition);
				}
			}

			// FromDefinition-only properties
			if (stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0)
			{
				sprintf(buf, "FromDefinitionProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "From Definition Properties", buf, &params, true);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Show Def Values", "Show values of properties inherited by the interaction def.", "showdefvalues", &entry->showDefValues);
				wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interaction Def", "Which interaction def should be used. (required)", "interactiondef", &entry->interactionDef);
				if(pDefEntry) {
					if(entry->bShowDefValues || (pDefEntry->pcCategoryName && stricmp(pDefEntry->pcCategoryName, "Contact")) )
						wleAEInteraction_ShowDefEntryClass(wleAEGlobalInteractionPropUI.autoWidget->root, pDefEntry, entry, index, "", &params, bActorObjects);
				}
			}

			sprintf(buf, "GeneralProperties%d", index+1);
			ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "General Properties", buf, &params, true);

			// Basic properties
			if (stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0) {
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Interact Condition", "Adds interaction conditions.", "isInteract", &entry->isInteract);
				// Display Def Defined Interact Values
				if(entry->bShowDefValues && pDefEntry && !pPropEntry->bOverrideInteract && pDefEntry->pcInteractionClass &&
						(	
							stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Contact") == 0||
							stricmp(pDefEntry->pcInteractionClass, "CraftingStation") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Door") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Gate") == 0
						)						 
					) 
				{
					if(!exprIsEmpty(pDefEntry->pInteractCond)) {
						sprintf(buf, "InteractCondLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Interact Expr", INTERACT_INTERACTCOND_TOOLTIP, buf, &labelParams, true);
						sprintf(buf, "InteractCondValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, NULL_TO_EMPTY(exprGetCompleteString(pDefEntry->pInteractCond)), buf, &valueParams, false);
					}

					if(!exprIsEmpty(pDefEntry->pAttemptableCond)) {
						sprintf(buf, "AttemptableCondLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Usable Expr", INTERACT_ATTEMPTABLECOND_TOOLTIP, buf, &labelParams, true);
						sprintf(buf, "AttemptableCondValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, NULL_TO_EMPTY(exprGetCompleteString(pDefEntry->pAttemptableCond)), buf, &valueParams, false);
					}
					
					if(!exprIsEmpty(pDefEntry->pSuccessCond)) {
						sprintf(buf, "SuccessCondLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Success Expr", INTERACT_SUCCESSCOND_TOOLTIP, buf, &labelParams, true);
						sprintf(buf, "SuccessCondValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, NULL_TO_EMPTY(exprGetCompleteString(pDefEntry->pSuccessCond)), buf, &valueParams, false);
					}
				}
			}
			// Editable Interact Fields
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "TeamCorral") == 0) ||
				((stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0) && entry->isInteract.boolvalue)
				)
			{
				wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interact Expr", INTERACT_INTERACTCOND_TOOLTIP, "pInteractCond", &entry->interactCond);
				wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Usable Expr", INTERACT_ATTEMPTABLECOND_TOOLTIP, "pAttemptableCond", &entry->attemptableCond);
				wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Success Expr", INTERACT_SUCCESSCOND_TOOLTIP, "pSuccessCond", &entry->successCond);				
			}

			// Visibility
			if (stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && !bActorObjects && !is_volume) {
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Visibility Condition", "Adds visibility conditions.", "isVisible", &entry->isVisible);
				// Display Def Defined Visibility Value
				if(entry->bShowDefValues && pDefEntry && !pPropEntry->bOverrideVisibility && pDefEntry->pcInteractionClass &&
						(	
							stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0||
							stricmp(pDefEntry->pcInteractionClass, "Contact") == 0||
							stricmp(pDefEntry->pcInteractionClass, "CraftingStation") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Destructable") == 0||
							stricmp(pDefEntry->pcInteractionClass, "Door") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "TeamCorral") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "NamedObject") == 0
						)						 
					)
				{
					if(!exprIsEmpty(pDefEntry->pVisibleExpr)) {
						sprintf(buf, "VisibleExprLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Visible Expr", "When true, the node is visible.  Otherwise it disappears.  If this expression has a value, it always supercedes any hide on cooldown.", buf, &labelParams, true);
						sprintf(buf, "VisibleExprValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, NULL_TO_EMPTY(exprGetCompleteString(pDefEntry->pVisibleExpr)), buf, &valueParams, false);
					}
				}
			}
			// Editable Visibility Fields
			if (!bActorObjects && !is_volume)
			{
				if((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "Destructible") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				   (stricmp(entry->interactionClass.stringvalue, "NamedObject") == 0) ||
				    (stricmp(entry->interactionClass.stringvalue, "TeamCorral") == 0) ||
				   ((stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0) && entry->isVisible.boolvalue))
				{
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Visible Expr", "When true, the node is visible.  Otherwise it disappears.  If this expression has a value, it always supercedes any hide on cooldown.", "pVisibleExpr", &entry->visibleExpr);
				}
			}

			// Category
			if (stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0) {
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Category Information", "Adds category and priority information.", "isCategory", &entry->isCategory);
				// Display Def Defined Category Values
				if(entry->bShowDefValues && pDefEntry && pDefEntry->pcInteractionClass && !pPropEntry->bOverrideCategoryPriority &&
						(	
							stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0||
							stricmp(pDefEntry->pcInteractionClass, "Gate") == 0||
							stricmp(pDefEntry->pcInteractionClass, "Contact") == 0||
							stricmp(pDefEntry->pcInteractionClass, "CraftingStation") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Door") == 0
						)						 
					)
				{
					if(EMPTY_TO_NULL(pDefEntry->pcCategoryName)) {
						sprintf(buf, "CategoryLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Category", "An optional hint to the UI about what category of interaction this is.", buf, &labelParams, true);
						sprintf(buf, "CategoryValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, NULL_TO_EMPTY(pDefEntry->pcCategoryName), buf, &valueParams, false);
					}
					sprintf(buf, "PriorityLabel%d", index+1);
					ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Priority", "An optional hint to the UI about the priority of this interaction.", buf, &labelParams, true);
					sprintf(buf, "PriorityValue%d", index+1);
					ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, StaticDefineIntRevLookup(WorldOptionalActionPriorityEnum, pDefEntry->iPriority), buf, &valueParams, false);
				}
			}
			// Editable Category Fields
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				((stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0) && entry->isCategory.boolvalue)
				)
			{
				wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Category", "An optional hint to the UI about what category of interaction this is.", "pcCategory", &entry->optActCategory);
				wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Priority", "An optional hint to the UI about the priority of this interaction.", "iPriority", &entry->priority);
				if (stricmp(entry->interactionClass.stringvalue, "FromDefinition") != 0)
				{
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Exclusive", "This determines whether only one person can interact with this entity at a time.", "exclusive", &entry->exclusive);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Auto Execute", "This determines whether this interaction will execute immediately when an entity is in range and meets interaction requirements.", "autoexec", &entry->autoExec);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "No Powers Interrupt", "This determines whether this interaction will interrupt powers", "disablepowersinterrupt", &entry->disablePowersInterrupt);
					if (!gConf.bAlwaysAllowInteractsInCombat)
					{
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Allow During Combat", "Is this interactable available during combat?", "allowduringcombat", &entry->allowDuringCombat);
					}
				}
			}

			// Timing properties
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "NamedObject") == 0))
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Timing", "Adds custom timing.  Otherwise these defaults are used: no use time, no active time, no cooldown, interrupted on move/power/damage.", "isTiming", &entry->isTiming);
				if (entry->isTiming.boolvalue)
				{
					if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
						(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
						(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
						(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
						(stricmp(entry->interactionClass.stringvalue, "Door") == 0))
					{
						wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Use Time", "The time (in secs) required to interact.  Zero means no wait.", "fUseTime", &entry->useTime, 0, 300, 1);
						if (entry->useTime.floatvalue > 0.0f)
						{
							wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Use Time Text", "A custom message to display while interacting with a use time", "msgUseTimeText", &entry->useTimeText);
						}
						wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Active Time", "The time (in secs) after interacting before cooldown starts.  Zero means no wait.", "fActiveTime", &entry->activeTime, 0, 3600, 1);
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "No Respawn", "If true, the object will never end cooldown.", "bNoRespawn", &entry->noRespawn);
						if (!entry->noRespawn.boolvalue)
						{
							wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Cooldown Time", "The time (in seconds) after when interaction is not allowed after it stops being active.  None=0, Short=30, Medium=300, Long=3600.", "cooldownType", &entry->cooldownType);
							if (entry->cooldownType.stringvalue && (stricmp(entry->cooldownType.stringvalue, "Custom") == 0))
							{
								wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Time", "The time (in seconds) after when interaction is not allowed after it stops being active.  Zero means no cooldown.", "custom_fCooldownTime", &entry->customCooldownTime, 0, 86400, 1);
							}
							wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dynamic Cooldown", "Whether this interactable should automatically adjust its cooldown period when other nearby interactables are also on cooldown.", "dynamicCooldownType", &entry->dynamicCooldownType);
						}
						if(!bActorObjects && !is_volume)
						{
							wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Usable By Team When Active", "If true, the object is usable by teammates of the interactor during its active period.", "bTeamUsableWhenActive", &entry->teamUsableWhenActive);
							wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Hide During Cooldown", "If true, the object is not visible during the cooldown period.  Note that this is ignored if the Visible Expr has a value.", "bHideDuringCooldown", &entry->hideDuringCooldown);
						}
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interrupt On Move", "If true, moving will interrupt the interact.", "interruptOnMove", &entry->interruptOnMove);
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interrupt On Power", "If true, using a power will interrupt the interact.", "interruptOnPower", &entry->interruptOnPower);
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interrupt On Damage", "If true, receiving damage will interrupt the interact.", "interruptOnDamage", &entry->interruptOnDamage);
					}
					else if(!is_volume) // Named Object
					{
						wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Hide During Cooldown", "If true, the object is not visible during the cooldown period.  Note that this is ignored if the Visible Expr has a value.", "bHideDuringCooldown", &entry->hideDuringCooldown);
					}
				}
			}
			// Display Def Defined Timing Values
			if ( (stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0) && entry->bShowDefValues && pDefEntry && pDefEntry->pcInteractionClass && 
					(	
						stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
						stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 ||
						stricmp(pDefEntry->pcInteractionClass, "Contact") == 0 ||
						stricmp(pDefEntry->pcInteractionClass, "Door") == 0 ||
						stricmp(pDefEntry->pcInteractionClass, "NamedObject") == 0
					)						 
				) 
			{
				wleAEInteraction_ShowEntryTiming(wleAEGlobalInteractionPropUI.autoWidget->root, pDefEntry, index, "", &indentParams);
			}


			// Animation properties
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0)
				)
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Animation", "Adds custom animation.  Otherwise no animation will occur.", "isAnimation", &entry->isAnimation);
				if (entry->isAnimation.boolvalue)
				{
					wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interact Anim", "Animation for the player to execute while interacting.", "interactAnim", &entry->interactAnim);
				} else if(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && entry->bShowDefValues && pDefEntry && pDefEntry->pAnimationProperties && pDefEntry->pcInteractionClass && 
							(	
								stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
								stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 ||
								stricmp(pDefEntry->pcInteractionClass, "Contact") == 0 ||
								stricmp(pDefEntry->pcInteractionClass, "CraftingStation") == 0 ||
								stricmp(pDefEntry->pcInteractionClass, "Door") == 0
							)						 
						) 
				{
					if(GET_REF(pDefEntry->pAnimationProperties->hInteractAnim)) {
						sprintf(buf, "AnimationLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Interact Anim", "Animation for the player to execute while interacting.", buf, &labelParams, true);
						sprintf(buf, "AnimationValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, REF_STRING_FROM_HANDLE(pDefEntry->pAnimationProperties->hInteractAnim), buf, &valueParams, false);
					}
				}
			}

			// Motion
			if (!bActorObjects && (
				(stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0)))
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Motion", "Whether or not there is custom motion for the interactable.", "CustomMotion", &entry->isMotion);
				if (entry->isMotion.boolvalue)
				{
					Vec3 vMin;
					Vec3 vMax;
					Vec3 vStep;

					wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Transition Time", "The amount of time it takes for the object to move to the destination", "CustomMotionTransTime", &entry->transitionTime, 0.0f, 1200.0f, 1.0f);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Transition During Use", "Determines whether the transition time occurs simultaneously with the use time.", "CustomMotionTransDuringUse", &entry->transDuringUse);
					wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dest Time", "How much time to spend in the destination position before beginning the return movement.", "CustomMotionDestTime", &entry->destinationTime, 0.0f, 1000.0f, 1.0f);
					wleAEFloatAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Return Time", "The amount of time it takes for the object to move back from the destination", "CustomMotionReturnTime", &entry->returnTime, 0.0f, 1200.0f, 1.0f);

					for (sub_index = 0; sub_index < MAX_MOVE_DESCRIPTORS; ++sub_index)
					{
						char buf2[256];

						sprintf(buf2, "CustomMotionDescriptor%i", sub_index);
						if (!entry->moveDescriptors[sub_index]->specified.boolvalue)
						{
							wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Add Move Descriptor", "Add/remove this move descriptor block.", buf2, &entry->moveDescriptors[sub_index]->specified);
							break;
						}
						else
						{
							sprintf(buf, "Move Descriptor #%i", sub_index);
							wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, buf, "Add/remove this move descriptor block.", buf2, &entry->moveDescriptors[sub_index]->specified);

							wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Start Child Index", "The index in this group of the child geo to move according to these settings", "CustomMotionStartChildIdx", &entry->moveDescriptors[sub_index]->startChildIdx, 0, numChildren - 1, 1);
							wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dest Child Index", "The index in this group of the child geo that will appear when this movement is complete (after the starting child geo is hidden)", "CustomMotionDestChildIdx", &entry->moveDescriptors[sub_index]->destChildIdx, -1, numChildren - 1, 1);

							setVec3same(vStep,1.0f);
							setVec3same(vMin, -1000.0f);
							setVec3same(vMax, 1000.0f);
							wleAEVec3AddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dest Position", "The offset postion to where this object will move", "CustomMotionPos", &entry->moveDescriptors[sub_index]->destPos, vMin, vMax, vStep);
							setVec3same(vMin, -360.0f);
							setVec3same(vMax, 360.0f);
							wleAEVec3AddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Dest Rotation", "The offset rotation to where this object will move", "CustomMotionRot", &entry->moveDescriptors[sub_index]->destRot, vMin, vMax, vStep);
						}
					}
				}
			}

			// Sound properties
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0)
				)
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Sounds", "Adds custom sounds to be played from the interactable.", "isSound", &entry->isSound);
				if (entry->isSound.boolvalue)
				{
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Attempt Sound", "Sound played from the interactable when interaction starts. (optional)", "attemptSound", &entry->attemptSound);
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Success Sound", "Sound played from the interactable upon success. (optional)", "successSound", &entry->successSound);
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Failure Sound", "Sound played from the interactable upon failure. (optional)", "failureSound", &entry->failureSound);
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interrupt Sound", "Sound played from the interactable when interact is interrupted. (optional)", "interruptSound", &entry->interruptSound);

					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Movement Trans Start Sound", "Sound played from the interactable when movement transition begins.", "movementTransStartSound", &entry->movementTransStartSound);
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Movement Trans End Sound", "Sound played from the interactable when movement transition ends.", "movementTransEndSound", &entry->movementTransEndSound);
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Movement Return Start Sound", "Sound played from the interactable when movement return begins.", "movementReturnStartSound", &entry->movementReturnStartSound);
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Movement Return End Sound", "Sound played from the interactable when movement return ends.", "movementReturnEndSound", &entry->movementReturnEndSound);	
				} else if(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && entry->bShowDefValues && pDefEntry && pDefEntry->pSoundProperties && pDefEntry->pcInteractionClass && 
					(	
					stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
					stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 ||
					stricmp(pDefEntry->pcInteractionClass, "Contact") == 0 ||
					stricmp(pDefEntry->pcInteractionClass, "CraftingStation") == 0 ||
					stricmp(pDefEntry->pcInteractionClass, "Door") == 0
					)						 
					) 
				{
					wleAEInteraction_ShowEntrySound(wleAEGlobalInteractionPropUI.autoWidget->root, pDefEntry, index, "", &indentParams);
				}
			}

			// Action properties
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "TeamCorral") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && entry->bShowDefValues && pDefEntry && pDefEntry->pcInteractionClass && 
				(	
				stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
				stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 ||
				stricmp(pDefEntry->pcInteractionClass, "Contact") == 0||
				stricmp(pDefEntry->pcInteractionClass, "Door") == 0
				))	)
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Actions", "Adds custom actions.  Otherwise no actions are performed.", "isAction", &entry->isAction);
				if (entry->isAction.boolvalue)
				{
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Attempt Expr", "Expression run when start interacting. (optional)", "pAttemptExpr", &entry->attemptExpr);
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Success Expr", "Expression run on success. (optional)", "pSuccessExpr", &entry->successExpr);
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Fail Expr", "Expression run on failure. (optional)", "fail_expr", &entry->failExpr);
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interrupt Expr", "Expression run when interact is interrupted. (optional)", "pInterruptExpr", &entry->interruptExpr);
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "No Longer Active Expr", "Expression run when it enters cooldown. (optional)", "pNoLongerActiveExpr", &entry->noLongerActiveExpr);
					wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Cooldown Expr", "Expression run when it finishes cooldown. (optional)", "pCooldownExpr", &entry->cooldownExpr);
					wleAEGameActionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Success Actions", "Transactional actions to perform on success. (optional)", "successActions", &entry->successActions);
					wleAEGameActionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Failure Actions", "Transactional actions to perform on failure. (optional)", "failureActions", &entry->failureActions);
				}
			}
			// Display Def Defined Action Values
			if(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && !entry->isAction.boolvalue && entry->bShowDefValues && pDefEntry && pDefEntry->pcInteractionClass && 
					(	
						stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
						stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 ||
						stricmp(pDefEntry->pcInteractionClass, "Contact") == 0||
						stricmp(pDefEntry->pcInteractionClass, "Door") == 0
					)						 
				) 
			{
				wleAEInteraction_ShowEntryActions(wleAEGlobalInteractionPropUI.autoWidget->root, pDefEntry, index, "", &indentParams);
			}

			// Text properties
			if ((stricmp(entry->interactionClass.stringvalue, "Chair") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Clickable") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Gate") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Contact") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "CraftingStation") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "Door") == 0) ||
				(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 ||
				(stricmp(entry->interactionClass.stringvalue, "TeamCorral") == 0)))
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Text", "Adds custom text.  Otherwise default text is used.", "isText", &entry->isText);
				if (entry->isText.boolvalue)
				{
					wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Usability Text", "The text displayed to indicate usability requirements. (optional)", "usability_option_text", &entry->usabilityOptionText);
					wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interact Text", "The text displayed before interacting. (optional)", "interact_option_text", &entry->interactOptionText);
					wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Detail Text", "Auxiliary text that can be used by the UI. (optional)", "interact_detail_text", &entry->interactDetailText);
					wleAETextureAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Detail Texture", "Auxiliary texture that can be used by the UI. (optional)", "interaction_detail_texture", &entry->interactDetailTexture);
					wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Success Text", "The text displayed on successful interact. (optional)", "success_text", &entry->successText);
					wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Failure Text", "The text displayed on failed interact. (optional)", "failure_text", &entry->failureText);
				} else if(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && entry->bShowDefValues && pDefEntry && pDefEntry->pTextProperties && pDefEntry->pcInteractionClass && 
						(	
							stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Contact") == 0||
							stricmp(pDefEntry->pcInteractionClass, "CraftingStation") == 0 ||
							stricmp(pDefEntry->pcInteractionClass, "Door") == 0
						)						 
					) 
				{
					if(GET_REF(pDefEntry->pTextProperties->usabilityOptionText.hMessage)) {
						sprintf(buf, "UsabilityTextLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Usability Text", "The text displayed to indicate usability requirements. (optional)", buf, &labelParams, true);
						sprintf(buf, "UsabilityTextValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, TranslateDisplayMessage(pDefEntry->pTextProperties->usabilityOptionText), buf, &valueParams, false);
					}

					if(GET_REF(pDefEntry->pTextProperties->interactOptionText.hMessage)) {
						sprintf(buf, "InteractTextLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Interact Text", "The text displayed before interacting. (optional)", buf, &labelParams, true);
						sprintf(buf, "InteractTextValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, TranslateDisplayMessage(pDefEntry->pTextProperties->interactOptionText), buf, &valueParams, false);
					}

					if(GET_REF(pDefEntry->pTextProperties->interactDetailText.hMessage)) {
						sprintf(buf, "InteractDetailTextLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Detail Text", "Auxiliary text that can be used by the UI. (optional)", buf, &labelParams, true);
						sprintf(buf, "InteractDetailTextValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, TranslateDisplayMessage(pDefEntry->pTextProperties->interactDetailText), buf, &valueParams, false);
					}

					if(EMPTY_TO_NULL(pDefEntry->pTextProperties->interactDetailTexture)) {
						UISprite *sprite = ui_SpriteCreate(0, 0, 64, 64, pDefEntry->pTextProperties->interactDetailTexture);
						sprintf(buf, "InteractDetailTextureLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Detail Texture", "Auxiliary texture that can be used by the UI. (optional)", buf, &labelParams, true);
						sprintf(buf, "InteractDetailTextureValue%d", index+1);
						ui_RebuildableTreeAddWidget(wleAEGlobalInteractionPropUI.autoWidget->root, UI_WIDGET(sprite), NULL, false, buf, &valueParams);
					}

					if(GET_REF(pDefEntry->pTextProperties->successConsoleText.hMessage)) {
						sprintf(buf, "SuccessTextLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Success Text", "The text displayed on successful interact. (optional)", buf, &labelParams, true);
						sprintf(buf, "SuccessTextValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, TranslateDisplayMessage(pDefEntry->pTextProperties->successConsoleText), buf, &valueParams, false);
					}

					if(GET_REF(pDefEntry->pTextProperties->failureConsoleText.hMessage)) {
						sprintf(buf, "FailureTextLabel%d", index+1);
						ui_RebuildableTreeAddLabelKeyedWithTooltip(wleAEGlobalInteractionPropUI.autoWidget->root, "Failure Text", "The text displayed on failed interact. (optional)", buf, &labelParams, true);
						sprintf(buf, "FailureTextValue%d", index+1);
						ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, TranslateDisplayMessage(pDefEntry->pTextProperties->failureConsoleText), buf, &valueParams, false);
					}
				}
			}

			// Reward properties
			if (stricmp(entry->interactionClass.stringvalue, "Clickable") == 0 ||
				stricmp(entry->interactionClass.stringvalue, "Gate") == 0 ||
				stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0
				)
			{
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Rewards", "Adds custom rewards.  Otherwise no rewards are granted.", "isReward", &entry->isReward);
				if (entry->isReward.boolvalue)
				{
					wleAEDictionaryAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Reward Table", "Reward table granted on successful interact. (required)", "rewardtable", &entry->rewardDef);
					wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Reward Level", "Where to get the level used when generating rewards.", "RewardLevelType", &entry->rewardLevelType);
					if (entry->rewardLevelType.stringvalue && (stricmp(entry->rewardLevelType.stringvalue, "Custom") == 0))
					{
						wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Custom Level", "The level to use when generating rewards.", "CustomRewardLevel", &entry->rewardCustomLevel, 1, 100, 1);
					}
					else if (entry->rewardLevelType.stringvalue && (stricmp(entry->rewardLevelType.stringvalue, "MapVariable") == 0))
					{
						WorldVariableDef ***peaVars = zmapInfoGetVariableDefs(NULL);
						eaClear(&wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.available_values);
						if (peaVars) {
							for (sub_index = 0; sub_index < eaSize(peaVars); sub_index++) {
								if ((*peaVars)[sub_index]->eType == WVAR_INT) {
									eaPush(&wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.available_values, (char*)(*peaVars)[sub_index]->pcName);
								}
							}
						}
						wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Map Variable", "The map variable to use as level when generating rewards.", "MapVarName", &entry->rewardMapVariable);
					}
				}
				else if(stricmp(entry->interactionClass.stringvalue, "FromDefinition") == 0 && entry->bShowDefValues && pDefEntry && pDefEntry->pcInteractionClass &&
					(
					stricmp(pDefEntry->pcInteractionClass, "Clickable") == 0 ||
					stricmp(pDefEntry->pcInteractionClass, "Gate") == 0 
					))
				{
					wlAEInteraction_ShowEntryRewards(wleAEGlobalInteractionPropUI.autoWidget->root, pDefEntry, index, "", &indentParams);
				}
			} 

			// Ambient Job properties
			if (stricmp(entry->interactionClass.stringvalue, "Ambientjob") == 0)
			{
				sprintf(buf, "AmbientProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Ambient Properties", buf, &params, true);

				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Critters Can Use", 
									"Allows regular critters to use this ambient job.", 
									"isForCritters", &entry->isForCritters);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Civilians Can Use", 
									"Allows civilian pedestrians to use this ambient job.", 
									"isForCivilians", &entry->isForCivilians);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Initial Job", 
									"When critters first enter ambient or combat, they will look to acquire these jobs initially.", 
									"initialJob", &entry->ambientJobInitialJob);
				wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Priority", 
									"Critters will prefer to use objects with a higher priority.",
									"Priority", &entry->ambientJobPriority, 0, 20, 1);
			}

			// Combat Job properties
			if (stricmp(entry->interactionClass.stringvalue, "CombatJob") == 0)
			{
				sprintf(buf, "CombatProperties%d", index+1);
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Combat Properties", buf, &params, true);

				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Critters Can Use", 
					"Allows regular critters to use this combat job.", 
					"isForCritters", &entry->isForCritters);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Civilians Can Use", 
					"Allows civilian pedestrians to use this combat job.", 
					"isForCivilians", &entry->isForCivilians);
				wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Initial Job", 
					"When critters first enter combat or ambient, they will look to acquire these jobs initially.", 
					"initialJob", &entry->ambientJobInitialJob);
				wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Priority", 
					"Critters will prefer to use objects with a higher priority.",
					"Priority", &entry->ambientJobPriority, 0, 20, 1);
			}

			if (index == 0)
			{
				ui_RebuildableTreeAddLabelKeyed(wleAEGlobalInteractionPropUI.autoWidget->root, "Root Properties", "RootProperties", &params, true);

				if (!bActorObjects && stricmp(entry->interactionClass.stringvalue, "Destructible") != 0)
				{
					wleAEMessageAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Display Text", "The text displayed above the interact. (optional)", "display_name_msg2", &wleAEGlobalInteractionPropUI.data.displayNameBasic);
				}

				// Child properties
				if (!bActorObjects && numChildren > 0)
				{
					wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Current State", "Makes the indicated child visible by default.", "child_select", &wleAEGlobalInteractionPropUI.data.childSelect, 0, numChildren - 1, 1);
					if (wleAEGlobalInteractionPropUI.data.childSelect.is_specified)
						wleAEExpressionAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "State Expr", "The expression that specifies the current state of this interaction node.", "child_select_expr", &wleAEGlobalInteractionPropUI.data.childSelectExpr);
				}

				if(!bActorObjects) 
				{
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Allow Explicit Hide", "If true, the node can be hidden through an explicit hide expression function.", "allowExplicitHide", &wleAEGlobalInteractionPropUI.data.allowExplicitHide);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Starts Hidden", "If true, the node starts hidden and can be unhidden using UnhideClickable.", "startsHidden", &wleAEGlobalInteractionPropUI.data.startsHidden);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Ent-specific Visiblity", "If checked, the Visibility expression will be evaluated on a per-entity basis.", "bVisEvalPerEnt", &wleAEGlobalInteractionPropUI.data.bVisEvalPerEnt);
				}

				if(!bActorObjects) 
				{
					wleAETextAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Type Tag", "A tag that identifies the object type. This field is not explicitly required to be populated.", "InteractionTypeTag", &wleAEGlobalInteractionPropUI.data.interactionTypeTag);
				}

				wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Interaction Distance", "The Distance the player must be within in order to interact", "interact_dist", &wleAEGlobalInteractionPropUI.data.interactDist,0,100000,5);

				if(!bActorObjects) 
				{
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Untargetable", "Flags this object as untargetable", "bUntargetable", &wleAEGlobalInteractionPropUI.data.isUntargetable);
				}

				if (!bActorObjects && !wleAEGlobalInteractionPropUI.data.isUntargetable.boolvalue )
				{
					wleAEIntAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Target Distance", "The Distance the player must be within in order for it to receive targeting info/be clicked on", "target_dist", &wleAEGlobalInteractionPropUI.data.targetDist,0,100000,5);
					wleAEBoolAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Can Tab Select", "Flags this object as tab selectable", "tab_select", &wleAEGlobalInteractionPropUI.data.canTabSelect);
					wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Override FX", "Set the FX to display for this object. This overrides both the regular interact FX as well as the alternate usability FX.", "OverrideFX", &wleAEGlobalInteractionPropUI.data.overrideFX);
					wleAEComboAddWidget(wleAEGlobalInteractionPropUI.autoWidget, "Additional Unique FX", "Set an additional FX which will be played when this node is interactable. Only one copy of the FX will be created no matter how many children this group has..", "AdditionalFX", &wleAEGlobalInteractionPropUI.data.additionalFX);
				}
			}
		} 
		else if (!entry->interactionClass.diff && !entry->interactionClass.spec_diff)
		{
			// Stop showing controls once we run into one that is blank and not different between selections
			break;
		}
	}

	ui_RebuildableTreeDoneBuilding(wleAEGlobalInteractionPropUI.autoWidget);
	emPanelSetHeight(wleAEGlobalInteractionPropUI.panel, elUIGetEndY(wleAEGlobalInteractionPropUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalInteractionPropUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalInteractionPropUI.scrollArea->widget.children[0]->children) + 5;
	emPanelSetActive(wleAEGlobalInteractionPropUI.panel, panelActive);

	return (hasProps ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);
}

static void wleAEInteractionPropAddCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pActor) {
		if(!wleAEActorInteractionHasProps(pData->pActor)) {
			pData->pActor->pInteractionProperties = StructCreate(parse_WorldInteractionProperties);
		}
	} else if (pData->pTracker->def) {
		if(!wleAEInteractionHasProps(pData->pTracker->def)) {
			pData->pTracker->def->property_structs.interaction_properties = StructCreate(parse_WorldInteractionProperties);
		}
	}
}

void wleAEInteractionPropAdd(void *unused, void *unused2)
{
	wleAEApplyToSelection(wleAEInteractionPropAddCB, NULL, NULL);
}

static void wleAEInteractionPropRemoveCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pActor) {
		StructDestroySafe(parse_WorldInteractionProperties, &pData->pActor->pInteractionProperties);
	} else if (pData->pTracker->def) {
		StructDestroySafe(parse_WorldInteractionProperties, &pData->pTracker->def->property_structs.interaction_properties);
		StructDestroySafe(parse_WorldInteractionProperties, &pData->pTracker->def->property_structs.server_volume.interaction_volume_properties);
	}
}

void wleAEInteractionPropRemove(void *unused, void *unused2)
{
	wleAEApplyToSelection(wleAEInteractionPropRemoveCB, NULL, NULL);
}

void wleAEInteractionPropCreate(EMPanel *panel)
{
	int index;
	int sub_index;
	int i;
	char buf[256];
	const char *pchTemp = NULL;
	DictionaryEArrayStruct *pInteractionDefs = NULL;
	
	if (wleAEGlobalInteractionPropUI.autoWidget)
		return;

	wleAEGlobalInteractionPropUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalInteractionPropUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalInteractionPropUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	wleAEGlobalInteractionPropUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalInteractionPropUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalInteractionPropUI.scrollArea->widget.sb->alwaysScrollX = false;
	emPanelAddChild(panel, wleAEGlobalInteractionPropUI.scrollArea, false);

	// Make sure interaction defs are present
	resSetDictionaryEditMode("InteractionDef", true);
	resRequestAllResourcesInDictionary("InteractionDef");
	pInteractionDefs = resDictGetEArrayStruct("InteractionDef");
	resSetDictionaryEditMode("ChoiceTable", true);
	resSetDictionaryEditMode("PetContactList", true);

	// set parameter settings

	for(index=0; index<MAX_INTERACTIONS; ++index)
	{
		WleAEInteractionEntryPropUI *new_entry = calloc(1, sizeof(WleAEInteractionEntryPropUI));
		eaPush(&wleAEGlobalInteractionPropUI.entries, new_entry);

		// Preload available values for skill types
		DefineFillAllKeysAndValues(WorldSkillTypeEnum, &wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.available_values, NULL);
		DefineFillAllKeysAndValues(WorldCooldownTimeEnum, &wleAEGlobalInteractionPropUI.entries[index]->cooldownType.available_values, NULL);
		DefineFillAllKeysAndValues(WorldDynamicSpawnTypeEnum, &wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.available_values, NULL);
		DefineFillAllKeysAndValues(WorldRewardLevelTypeEnum, &wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.available_values, NULL);
		DefineFillAllKeysAndValues(WorldDoorTypeEnum, &wleAEGlobalInteractionPropUI.entries[index]->doorType.available_values, NULL);

		// Interaction Class
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.left_pad = 0;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.update_func = wleAEInteractionPropClassUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.apply_func = wleAEInteractionPropClassApply;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.can_unspecify = true;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.can_copy = true;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.copy_func = wleAEInteractionPropClassCopy;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.copy_data = (void*) (intptr_t) index;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.entry_width = 140;
		wleAEGlobalInteractionPropUI.entries[index]->interactionClass.index = index;
		// Show all types except "Throwable"
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Chair"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Clickable"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Contact"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("CraftingStation"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Destructible"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Door"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("FromDefinition"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Gate"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("NamedObject"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("Ambientjob"));
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values, (char*)allocAddString("CombatJob"));
		//wlInteractionGetClassNames(&wleAEGlobalInteractionPropUI.entries[index]->interactionClass.available_values);

		// Basic Properties
		wleAEGlobalInteractionPropUI.entries[index]->isInteract.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isInteract.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isInteract.update_func = wleAEInteractionPropIsInteractUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isInteract.apply_func = wleAEInteractionPropIsInteractApply;
		wleAEGlobalInteractionPropUI.entries[index]->isInteract.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interactCond.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactCond.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->interactCond.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactCond.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->interactCond.update_func = wleAEInteractionPropInteractCondUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactCond.apply_func = wleAEInteractionPropInteractCondApply;
		wleAEGlobalInteractionPropUI.entries[index]->interactCond.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.update_func = wleAEInteractionPropAttemptableCondUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.apply_func = wleAEInteractionPropAttemptableCondApply;
		wleAEGlobalInteractionPropUI.entries[index]->attemptableCond.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->successCond.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successCond.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->successCond.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successCond.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->successCond.update_func = wleAEInteractionPropSuccessCondUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->successCond.apply_func = wleAEInteractionPropSuccessCondApply;
		wleAEGlobalInteractionPropUI.entries[index]->successCond.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->isVisible.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isVisible.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isVisible.update_func = wleAEInteractionPropIsVisibleUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isVisible.apply_func = wleAEInteractionPropIsVisibleApply;
		wleAEGlobalInteractionPropUI.entries[index]->isVisible.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.update_func = wleAEInteractionPropVisibleExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.apply_func = wleAEInteractionPropVisibleExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->visibleExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->isCategory.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isCategory.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isCategory.update_func = wleAEInteractionPropIsCategoryUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isCategory.apply_func = wleAEInteractionPropIsCategoryApply;
		wleAEGlobalInteractionPropUI.entries[index]->isCategory.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->optActCategory.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->optActCategory.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->optActCategory.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->optActCategory.update_func = wleAEInteractionPropOptActCategoryUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->optActCategory.apply_func = wleAEInteractionPropOptActCategoryApply;
		wleAEGlobalInteractionPropUI.entries[index]->optActCategory.index = index;
		eaPush(&wleAEGlobalInteractionPropUI.entries[index]->optActCategory.available_values, (char*)allocAddString("None") );
		for (i = 0; i<eaSize(&g_eaOptionalActionCategoryDefs); i++){
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->optActCategory.available_values, (char*)g_eaOptionalActionCategoryDefs[i]->pcName);
		}

		wleAEGlobalInteractionPropUI.entries[index]->priority.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->priority.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->priority.entry_width = 140;
		wleAEGlobalInteractionPropUI.entries[index]->priority.update_func = wleAEInteractionPropPriorityUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->priority.apply_func = wleAEInteractionPropPriorityApply;
		wleAEGlobalInteractionPropUI.entries[index]->priority.index = index;
		for (i = 0; pchTemp = StaticDefineIntRevLookup(WorldOptionalActionPriorityEnum, i); i++){
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->priority.available_values, (char*)pchTemp);
		}
		wleAEGlobalInteractionPropUI.entries[index]->bPriorityChanged = false;

		wleAEGlobalInteractionPropUI.entries[index]->exclusive.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->exclusive.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->exclusive.update_func = wleAEInteractionPropExclusiveUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->exclusive.apply_func = wleAEInteractionPropExclusiveApply;
		wleAEGlobalInteractionPropUI.entries[index]->exclusive.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->autoExec.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->autoExec.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->autoExec.update_func = wleAEInteractionPropAutoExecUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->autoExec.apply_func = wleAEInteractionPropAutoExecApply;
		wleAEGlobalInteractionPropUI.entries[index]->autoExec.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->disablePowersInterrupt.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->disablePowersInterrupt.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->disablePowersInterrupt.update_func = wleAEInteractionPropDisablePowersInterruptUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->disablePowersInterrupt.apply_func = wleAEInteractionPropDisablePowersInterruptApply;
		wleAEGlobalInteractionPropUI.entries[index]->disablePowersInterrupt.index = index;

		if (!gConf.bAlwaysAllowInteractsInCombat)
		{
			wleAEGlobalInteractionPropUI.entries[index]->allowDuringCombat.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
			wleAEGlobalInteractionPropUI.entries[index]->allowDuringCombat.left_pad = WLE_AE_INTERACTION_INDENT;
			wleAEGlobalInteractionPropUI.entries[index]->allowDuringCombat.update_func = wleAEInteractionPropAllowDuringCombatUpdate;
			wleAEGlobalInteractionPropUI.entries[index]->allowDuringCombat.apply_func = wleAEInteractionPropAllowDuringCombatApply;
			wleAEGlobalInteractionPropUI.entries[index]->allowDuringCombat.index = index;
		}

		/*
		wleAEGlobalInteractionPropUI.entries[index]->visibleFX.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->visibleFX.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->visibleFX.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->visibleFX.update_func = wleAEInteractionPropVisibleFXUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->visibleFX.apply_func = wleAEInteractionPropVisibleFXApply;
		wleAEGlobalInteractionPropUI.entries[index]->visibleFX.index = index;
		*/

		// Destructible Properties
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.dictionary = "CritterDef";
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.update_func = wleAEInteractionPropCritterDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.apply_func = wleAEInteractionPropCritterDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->critterDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.dictionary = "CritterOverrideDef";
		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.update_func = wleAEInteractionPropCritterOverrideDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.apply_func = wleAEInteractionPropCritterOverrideDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->critterOverrideDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.dictionary = "PowerDef";
		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.update_func = wleAEInteractionPropOnDeathPowerUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.apply_func = wleAEInteractionPropOnDeathPowerApply;
		wleAEGlobalInteractionPropUI.entries[index]->onDeathPower.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.precision = 2;
		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.update_func = wleAEInteractionPropRespawnTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.apply_func = wleAEInteractionPropRespawnTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->respawnTime.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->entityName.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->entityName.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->entityName.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->entityName.update_func = wleAEInteractionPropEntityNameUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->entityName.apply_func = wleAEInteractionPropEntityNameApply;
		wleAEGlobalInteractionPropUI.entries[index]->entityName.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->displayName.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->displayName.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->displayName.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->displayName.update_func = wleAEInteractionPropDisplayNameUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->displayName.apply_func = wleAEInteractionPropDisplayNameApply;
		sprintf(buf, "InteractName%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->displayName.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->displayName.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->critterLevel.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterLevel.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->critterLevel.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterLevel.update_func = wleAEInteractionPropCritterLevelUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->critterLevel.apply_func = wleAEInteractionPropCritterLevelApply;
		wleAEGlobalInteractionPropUI.entries[index]->critterLevel.index = index;

		// Contact properties
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.dictionary = "Contact";
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.update_func = wleAEInteractionPropContactDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.apply_func = wleAEInteractionPropContactDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->contactDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->contactDialog.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->contactDialog.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->contactDialog.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->contactDialog.update_func = wleAEInteractionPropContactDialogUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->contactDialog.apply_func = wleAEInteractionPropContactDialogApply;
		wleAEGlobalInteractionPropUI.entries[index]->contactDialog.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->isContact.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isContact.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isContact.update_func = wleAEInteractionPropIsContactUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isContact.apply_func = wleAEInteractionPropIsContactApply;
		wleAEGlobalInteractionPropUI.entries[index]->isContact.index = index;

		// Crafting Station properties
		wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.update_func = wleAEInteractionPropCraftingSkillUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.apply_func = wleAEInteractionPropCraftingSkillApply;
		wleAEGlobalInteractionPropUI.entries[index]->craftingSkill.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->maxSkill.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->maxSkill.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->maxSkill.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->maxSkill.update_func = wleAEInteractionPropMaxSkillUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->maxSkill.apply_func = wleAEInteractionPropMaxSkillApply;
		wleAEGlobalInteractionPropUI.entries[index]->maxSkill.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.dictionary = "RewardTable";
		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.update_func = wleAEInteractionPropCraftRewardDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.apply_func = wleAEInteractionPropCraftRewardDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->craftRewardDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.dictionary = "RewardTable";
		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.update_func = wleAEInteractionPropDeconstructRewardDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.apply_func = wleAEInteractionPropDeconstructRewardDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->deconstructRewardDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.dictionary = "RewardTable";
		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.update_func = wleAEInteractionPropExperimentRewardDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.apply_func = wleAEInteractionPropExperimentRewardDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->experimentRewardDef.index = index;

		// Door properties

		wleAEGlobalInteractionPropUI.entries[index]->isDoor.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isDoor.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isDoor.update_func = wleAEInteractionPropIsDoorUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isDoor.apply_func = wleAEInteractionPropIsDoorApply;
		wleAEGlobalInteractionPropUI.entries[index]->isDoor.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->doorDest.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.entry_width = 1.0;
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.update_func = wleAEInteractionPropDoorDestUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.apply_func = wleAEInteractionPropDoorDestApply;
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.index = index;
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.source_map_name = SAFE_MEMBER(zmapGetInfo(NULL), map_name);
		wleAEGlobalInteractionPropUI.entries[index]->doorDest.no_name = true;
		
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.update_func = wleAEInteractionPropTransitionOverrideUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.apply_func = wleAEInteractionPropTransitionOverrideApply;
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.index = index;
		wleAEGlobalInteractionPropUI.entries[index]->doorTransition.dictionary = "DoorTransitionSequenceDef";
		
		wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables.update_func = wleAEInteractionPropDoorHasVariablesUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables.apply_func = wleAEInteractionPropDoorHasVariablesApply;
		wleAEGlobalInteractionPropUI.entries[index]->doorHasVariables.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->perPlayerDoor.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->perPlayerDoor.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->perPlayerDoor.update_func = wleAEInteractionPropPerPlayerDoorUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->perPlayerDoor.apply_func = wleAEInteractionPropPerPlayerDoorApply;
		wleAEGlobalInteractionPropUI.entries[index]->perPlayerDoor.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->singlePlayerDoor.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->singlePlayerDoor.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->singlePlayerDoor.update_func = wleAEInteractionPropSinglePlayerDoorUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->singlePlayerDoor.apply_func = wleAEInteractionPropSinglePlayerDoorApply;
		wleAEGlobalInteractionPropUI.entries[index]->singlePlayerDoor.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->includeTeammatesDoor.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->includeTeammatesDoor.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->includeTeammatesDoor.update_func = wleAEInteractionPropIncludeTeammatesDoorUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->includeTeammatesDoor.apply_func = wleAEInteractionPropIncludeTeammatesDoorApply;
		wleAEGlobalInteractionPropUI.entries[index]->includeTeammatesDoor.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->collectDestStatus.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->collectDestStatus.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->collectDestStatus.update_func = wleAEInteractionPropCollectDestStatusUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->collectDestStatus.apply_func = wleAEInteractionPropCollectDestStatusApply;
		wleAEGlobalInteractionPropUI.entries[index]->collectDestStatus.index = index;


        wleAEGlobalInteractionPropUI.entries[index]->destinationSameOwner.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
        wleAEGlobalInteractionPropUI.entries[index]->destinationSameOwner.left_pad = WLE_AE_INTERACTION_INDENT;
        wleAEGlobalInteractionPropUI.entries[index]->destinationSameOwner.update_func = wleAEInteractionPropDestinationSameOwnerUpdate;
        wleAEGlobalInteractionPropUI.entries[index]->destinationSameOwner.apply_func = wleAEInteractionPropDestinationSameOwnerApply;
        wleAEGlobalInteractionPropUI.entries[index]->destinationSameOwner.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->doorType.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorType.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->doorType.update_func = wleAEInteractionPropDoorTypeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->doorType.apply_func = wleAEInteractionPropDoorTypeApply;
		wleAEGlobalInteractionPropUI.entries[index]->doorType.index = index;
		wleAEGlobalInteractionPropUI.entries[index]->doorType.entry_width = 140;

		wleAEGlobalInteractionPropUI.entries[index]->doorID.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorID.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->doorID.update_func = wleAEInteractionPropDoorIDUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->doorID.apply_func = wleAEInteractionPropDoorIDApply;
		wleAEGlobalInteractionPropUI.entries[index]->doorID.index = index;
		wleAEGlobalInteractionPropUI.entries[index]->doorID.entry_width = 140;

		wleAEGlobalInteractionPropUI.entries[index]->queueDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->queueDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->queueDef.dictionary = "QueueDef";
		wleAEGlobalInteractionPropUI.entries[index]->queueDef.update_func = wleAEInteractionPropDoorQueueDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->queueDef.apply_func = wleAEInteractionPropDoorQueueDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->queueDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->doorKey.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorKey.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->doorKey.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->doorKey.update_func = wleAEInteractionPropDoorKeyUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->doorKey.apply_func = wleAEInteractionPropDoorKeyApply;
		wleAEGlobalInteractionPropUI.entries[index]->doorKey.index = index;

		// Chair Properties
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre.update_func = wleAEInteractionPropChairBitHandlesPreUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre.apply_func = wleAEInteractionPropChairBitHandlesPreApply;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesPre.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold.update_func = wleAEInteractionPropChairBitHandlesHoldUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold.apply_func = wleAEInteractionPropChairBitHandlesHoldApply;
		wleAEGlobalInteractionPropUI.entries[index]->chairBitHandlesHold.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove.update_func = wleAEInteractionPropChairTimeToMoveUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove.apply_func = wleAEInteractionPropChairTimeToMoveApply;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimeToMove.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold.update_func = wleAEInteractionPropChairTimePostHoldUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold.apply_func = wleAEInteractionPropChairTimePostHoldApply;
		wleAEGlobalInteractionPropUI.entries[index]->chairTimePostHold.index = index;

		// Gate Properties
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.update_func = wleAEInteractionPropGateCritterUseCondUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.apply_func = wleAEInteractionPropGateCritterUseCondApply;
		wleAEGlobalInteractionPropUI.entries[index]->critterUseExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->volumeTriggered.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->volumeTriggered.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->volumeTriggered.update_func = wleAEInteractionPropGateVolumeTriggeredUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->volumeTriggered.apply_func = wleAEInteractionPropGateVolumeTriggeredApply;
		wleAEGlobalInteractionPropUI.entries[index]->volumeTriggered.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->startState.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->startState.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->startState.update_func = wleAEInteractionPropGateStartStateUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->startState.apply_func = wleAEInteractionPropGateStartStateApply;
		wleAEGlobalInteractionPropUI.entries[index]->startState.index = index;

		// From Def Properties
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.update_func = wleAEInteractionPropInteractionDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.apply_func = wleAEInteractionPropInteractionDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.is_filtered = true;
		wleAEGlobalInteractionPropUI.entries[index]->interactionDef.index = index;

		eaClear(&wleAEGlobalInteractionPropUI.entries[index]->interactionDef.available_values);
		for(i=0; i<eaSize(&pInteractionDefs->ppReferents); ++i) {
			InteractionDef *pDef = pInteractionDefs->ppReferents[i];
			if ((pDef->eType == InteractionDefType_Node) || (pDef->eType == InteractionDefType_Any)) {
				eaPush(&wleAEGlobalInteractionPropUI.entries[index]->interactionDef.available_values, (char*)pDef->pcName);
			}
		}


		wleAEGlobalInteractionPropUI.entries[index]->bShowDefValues = true;
		wleAEGlobalInteractionPropUI.entries[index]->showDefValues.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->showDefValues.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->showDefValues.update_func = wleAEInteractionPropShowDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->showDefValues.apply_func = wleAEInteractionPropShowDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->showDefValues.index = index;


		// Timing properties
		wleAEGlobalInteractionPropUI.entries[index]->isTiming.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isTiming.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isTiming.update_func = wleAEInteractionPropIsTimingUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isTiming.apply_func = wleAEInteractionPropIsTimingApply;
		wleAEGlobalInteractionPropUI.entries[index]->isTiming.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->useTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->useTime.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->useTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->useTime.update_func = wleAEInteractionPropUseTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->useTime.apply_func = wleAEInteractionPropUseTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->useTime.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.update_func = wleAEInteractionPropUseTimeTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.apply_func = wleAEInteractionPropUseTimeTextApply;
		sprintf(buf, "useTimeText%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->useTimeText.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->activeTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->activeTime.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->activeTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->activeTime.update_func = wleAEInteractionPropActiveTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->activeTime.apply_func = wleAEInteractionPropActiveTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->activeTime.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->cooldownType.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownType.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownType.entry_width = 100;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownType.update_func = wleAEInteractionPropCooldownTypeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownType.apply_func = wleAEInteractionPropCooldownTypeApply;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownType.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime.left_pad = WLE_AE_INTERACTION_INDENT+40;
		wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime.update_func = wleAEInteractionPropCustomCooldownTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime.apply_func = wleAEInteractionPropCustomCooldownTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->customCooldownTime.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.entry_width = 100;
		wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.update_func = wleAEInteractionPropDynamicCooldownUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.apply_func = wleAEInteractionPropDynamicCooldownApply;
		wleAEGlobalInteractionPropUI.entries[index]->dynamicCooldownType.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->teamUsableWhenActive.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->teamUsableWhenActive.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->teamUsableWhenActive.update_func = wleAEInteractionPropTeamUsableWhenActiveUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->teamUsableWhenActive.apply_func = wleAEInteractionPropTeamUsableWhenActiveApply;
		wleAEGlobalInteractionPropUI.entries[index]->teamUsableWhenActive.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->hideDuringCooldown.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->hideDuringCooldown.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->hideDuringCooldown.update_func = wleAEInteractionPropHideDuringCooldownUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->hideDuringCooldown.apply_func = wleAEInteractionPropHideDuringCooldownApply;
		wleAEGlobalInteractionPropUI.entries[index]->hideDuringCooldown.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interruptOnPower.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnPower.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnPower.update_func = wleAEInteractionPropInterruptOnPowerUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnPower.apply_func = wleAEInteractionPropInterruptOnPowerApply;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnPower.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interruptOnDamage.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnDamage.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnDamage.update_func = wleAEInteractionPropInterruptOnDamageUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnDamage.apply_func = wleAEInteractionPropInterruptOnDamageApply;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnDamage.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interruptOnMove.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnMove.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnMove.update_func = wleAEInteractionPropInterruptOnMoveUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnMove.apply_func = wleAEInteractionPropInterruptOnMoveApply;
		wleAEGlobalInteractionPropUI.entries[index]->interruptOnMove.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->noRespawn.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->noRespawn.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->noRespawn.update_func = wleAEInteractionPropNoRespawnUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->noRespawn.apply_func = wleAEInteractionPropNoRespawnApply;
		wleAEGlobalInteractionPropUI.entries[index]->noRespawn.index = index;


		// Animation properties
		wleAEGlobalInteractionPropUI.entries[index]->isAnimation.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isAnimation.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isAnimation.update_func = wleAEInteractionPropIsAnimationUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isAnimation.apply_func = wleAEInteractionPropIsAnimationApply;
		wleAEGlobalInteractionPropUI.entries[index]->isAnimation.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.dictionary = "AIAnimList";
		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.update_func = wleAEInteractionPropInteractAnimUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.apply_func = wleAEInteractionPropInteractAnimApply;
		wleAEGlobalInteractionPropUI.entries[index]->interactAnim.index = index;

		// Sound properties
#define wleAEInteractionSetupSoundParam(param, fieldName)\
	wleAEGlobalInteractionPropUI.entries[index]->param.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;\
	wleAEGlobalInteractionPropUI.entries[index]->param.left_pad = WLE_AE_INTERACTION_INDENT+20;\
	wleAEGlobalInteractionPropUI.entries[index]->param.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;\
	wleAEGlobalInteractionPropUI.entries[index]->param.update_func = wleAEInteractionPropSoundUpdate;\
	wleAEGlobalInteractionPropUI.entries[index]->param.update_data = fieldName;\
	wleAEGlobalInteractionPropUI.entries[index]->param.apply_func = wleAEInteractionPropSoundApply;\
	wleAEGlobalInteractionPropUI.entries[index]->param.apply_data = fieldName;\
	wleAEGlobalInteractionPropUI.entries[index]->param.available_values = *(sndGetEventListStatic());\
	wleAEGlobalInteractionPropUI.entries[index]->param.is_filtered = true;\
	wleAEGlobalInteractionPropUI.entries[index]->param.index = index

		wleAEGlobalInteractionPropUI.entries[index]->isSound.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isSound.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isSound.update_func = wleAEInteractionPropIsSoundUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isSound.apply_func = wleAEInteractionPropIsSoundApply;
		wleAEGlobalInteractionPropUI.entries[index]->isSound.index = index;

		wleAEInteractionSetupSoundParam(attemptSound, "AttemptSound");
		wleAEInteractionSetupSoundParam(successSound, "SuccessSound");
		wleAEInteractionSetupSoundParam(failureSound, "FailureSound");
		wleAEInteractionSetupSoundParam(interruptSound, "InterruptSound");
		wleAEInteractionSetupSoundParam(movementTransStartSound, "MovementTransStartSound");
		wleAEInteractionSetupSoundParam(movementTransEndSound, "MovementTransEndSound");
		wleAEInteractionSetupSoundParam(movementReturnStartSound, "MovementReturnStartSound");
		wleAEInteractionSetupSoundParam(movementReturnEndSound, "MovementReturnEndSound");

		// Action properties
		wleAEGlobalInteractionPropUI.entries[index]->isAction.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isAction.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isAction.update_func = wleAEInteractionPropIsActionUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isAction.apply_func = wleAEInteractionPropIsActionApply;
		wleAEGlobalInteractionPropUI.entries[index]->isAction.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.update_func = wleAEInteractionPropAttemptExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.apply_func = wleAEInteractionPropAttemptExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->attemptExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->successExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->successExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successExpr.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->successExpr.update_func = wleAEInteractionPropSuccessExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->successExpr.apply_func = wleAEInteractionPropSuccessExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->successExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->failExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->failExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->failExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->failExpr.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->failExpr.update_func = wleAEInteractionPropFailureExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->failExpr.apply_func = wleAEInteractionPropFailureExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->failExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.context = g_pInteractionContext;
		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.update_func = wleAEInteractionPropInterruptExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.apply_func = wleAEInteractionPropInterruptExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->interruptExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.context = g_pInteractionNonPlayerContext;
		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.update_func = wleAEInteractionPropNoLongerActiveExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.apply_func = wleAEInteractionPropNoLongerActiveExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->noLongerActiveExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.context = g_pInteractionNonPlayerContext;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.update_func = wleAEInteractionPropCooldownExprUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.apply_func = wleAEInteractionPropCooldownExprApply;
		wleAEGlobalInteractionPropUI.entries[index]->cooldownExpr.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->successActions.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successActions.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->successActions.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successActions.update_func = wleAEInteractionPropSuccessActionsUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->successActions.apply_func = wleAEInteractionPropSuccessActionsApply;
		sprintf(buf, "successActions%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->successActions.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->successActions.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->failureActions.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->failureActions.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->failureActions.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->failureActions.update_func = wleAEInteractionPropFailureActionsUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->failureActions.apply_func = wleAEInteractionPropFailureActionsApply;
		sprintf(buf, "failureActions%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->failureActions.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->failureActions.index = index;

		// Text properties
		wleAEGlobalInteractionPropUI.entries[index]->isText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isText.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isText.update_func = wleAEInteractionPropIsTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isText.apply_func = wleAEInteractionPropIsTextApply;
		wleAEGlobalInteractionPropUI.entries[index]->isText.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.update_func = wleAEInteractionPropUsabilityTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.apply_func = wleAEInteractionPropUsabilityTextApply;
		sprintf(buf, "usabilityOptionText%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->usabilityOptionText.index = index;
		
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.update_func = wleAEInteractionPropInteractTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.apply_func = wleAEInteractionPropInteractTextApply;
		sprintf(buf, "interactOptionText%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->interactOptionText.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.update_func = wleAEInteractionPropInteractDetailTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.apply_func = wleAEInteractionPropInteractDetailTextApply;
		sprintf(buf, "interactDetailText%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailText.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->interactDetailTexture.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailTexture.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailTexture.update_func = wleAEInteractionPropInteractDetailTextureUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailTexture.apply_func = wleAEInteractionPropInteractDetailTextureApply;
		wleAEGlobalInteractionPropUI.entries[index]->interactDetailTexture.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->successText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successText.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->successText.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->successText.update_func = wleAEInteractionPropSuccessTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->successText.apply_func = wleAEInteractionPropSuccessTextApply;
		sprintf(buf, "successText%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->successText.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->successText.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->failureText.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->failureText.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->failureText.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->failureText.update_func = wleAEInteractionPropFailureTextUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->failureText.apply_func = wleAEInteractionPropFailureTextApply;
		sprintf(buf, "failureText%d", index);
		wleAEGlobalInteractionPropUI.entries[index]->failureText.source_key = strdup(buf);
		wleAEGlobalInteractionPropUI.entries[index]->failureText.index = index;

		// Reward properties
		wleAEGlobalInteractionPropUI.entries[index]->isReward.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isReward.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isReward.update_func = wleAEInteractionPropIsRewardUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isReward.apply_func = wleAEInteractionPropIsRewardApply;
		wleAEGlobalInteractionPropUI.entries[index]->isReward.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.dictionary = "RewardTable";
		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.update_func = wleAEInteractionPropRewardDefUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.apply_func = wleAEInteractionPropRewardDefApply;
		wleAEGlobalInteractionPropUI.entries[index]->rewardDef.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.left_pad = WLE_AE_INTERACTION_INDENT+20;
		wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.entry_width = 100;
		wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.update_func = wleAEInteractionPropRewardLevelTypeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.apply_func = wleAEInteractionPropRewardLevelTypeApply;
		wleAEGlobalInteractionPropUI.entries[index]->rewardLevelType.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel.left_pad = WLE_AE_INTERACTION_INDENT+40;
		wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel.update_func = wleAEInteractionPropRewardCustomLevelUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel.apply_func = wleAEInteractionPropRewardCustomLevelApply;
		wleAEGlobalInteractionPropUI.entries[index]->rewardCustomLevel.index = index;
		
		wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.left_pad = WLE_AE_INTERACTION_INDENT+40;
		wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.entry_width = 100;
		wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.update_func = wleAEInteractionPropRewardMapVariableUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.apply_func = wleAEInteractionPropRewardMapVariableApply;
		wleAEGlobalInteractionPropUI.entries[index]->rewardMapVariable.index = index;

		// Motion properties
		wleAEGlobalInteractionPropUI.entries[index]->isMotion.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isMotion.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isMotion.update_func = wleAEInteractionPropIsMotionUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isMotion.apply_func = wleAEInteractionPropIsMotionApply;
		wleAEGlobalInteractionPropUI.entries[index]->isMotion.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->transitionTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->transitionTime.left_pad = WLE_AE_INTERACTION_INDENT + 20;
		wleAEGlobalInteractionPropUI.entries[index]->transitionTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->transitionTime.update_func = wleAEInteractionPropMotionTransTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->transitionTime.apply_func = wleAEInteractionPropMotionTransTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->transitionTime.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->destinationTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->destinationTime.left_pad = WLE_AE_INTERACTION_INDENT + 20;
		wleAEGlobalInteractionPropUI.entries[index]->destinationTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->destinationTime.update_func = wleAEInteractionPropMotionDestTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->destinationTime.apply_func = wleAEInteractionPropMotionDestTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->destinationTime.index = index;
		
		wleAEGlobalInteractionPropUI.entries[index]->returnTime.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->returnTime.left_pad = WLE_AE_INTERACTION_INDENT + 20;
		wleAEGlobalInteractionPropUI.entries[index]->returnTime.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->returnTime.update_func = wleAEInteractionPropMotionReturnTimeUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->returnTime.apply_func = wleAEInteractionPropMotionReturnTimeApply;
		wleAEGlobalInteractionPropUI.entries[index]->returnTime.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->transDuringUse.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->transDuringUse.left_pad = WLE_AE_INTERACTION_INDENT + 20;
		wleAEGlobalInteractionPropUI.entries[index]->transDuringUse.update_func = wleAEInteractionPropMotionTransDuringUseUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->transDuringUse.apply_func = wleAEInteractionPropMotionTransDuringUseApply;
		wleAEGlobalInteractionPropUI.entries[index]->transDuringUse.index = index;

		for (sub_index = 0; sub_index < MAX_MOVE_DESCRIPTORS; ++sub_index)
		{
			int compound_index = 1000 * index + sub_index;
			WleAEInteractionMoveDescriptorUI *newMoveUI = calloc(1, sizeof(*newMoveUI));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->moveDescriptors, newMoveUI);

#define wleAEInteractionSetupMoveParam(param, funcPrefix) \
	param.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;\
	param.left_pad = WLE_AE_INTERACTION_INDENT + 40;\
	param.update_func = funcPrefix##Update;\
	param.apply_func = funcPrefix##Apply;\
	param.index = compound_index

			wleAEInteractionSetupMoveParam(newMoveUI->specified, wleAEInteractionPropMoveSpecified);
			newMoveUI->specified.left_pad = WLE_AE_INTERACTION_INDENT + 20;

			wleAEInteractionSetupMoveParam(newMoveUI->startChildIdx, wleAEInteractionPropMoveStartChildIdx);
			wleAEInteractionSetupMoveParam(newMoveUI->destChildIdx, wleAEInteractionPropMoveDestChildIdx);
			wleAEInteractionSetupMoveParam(newMoveUI->destPos, wleAEInteractionPropMoveDestPos);
			wleAEInteractionSetupMoveParam(newMoveUI->destRot, wleAEInteractionPropMoveDestRot);
		}
		
		// Ambient Job properties
		wleAEGlobalInteractionPropUI.entries[index]->isForCritters.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isForCritters.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isForCritters.update_func = wleAEInteractionProp_AmbientJobCritterUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isForCritters.apply_func = wleAEInteractionProp_AmbientJobCritterApply;
		wleAEGlobalInteractionPropUI.entries[index]->isForCritters.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->isForCivilians.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->isForCivilians.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->isForCivilians.update_func = wleAEInteractionProp_AmbientJobCritterUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->isForCivilians.apply_func = wleAEInteractionProp_AmbientJobCritterApply;
		wleAEGlobalInteractionPropUI.entries[index]->isForCivilians.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->ambientJobInitialJob.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobInitialJob.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobInitialJob.update_func = wleAEInteractionProp_AmbientJobCritterUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobInitialJob.apply_func = wleAEInteractionProp_AmbientJobCritterApply;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobInitialJob.index = index;

		wleAEGlobalInteractionPropUI.entries[index]->ambientJobPriority.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobPriority.left_pad = WLE_AE_INTERACTION_INDENT;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobPriority.update_func = wleAEInteractionProp_AmbientJobPriorityUpdate;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobPriority.apply_func = wleAEInteractionProp_AmbientJobPriorityApply;
		wleAEGlobalInteractionPropUI.entries[index]->ambientJobPriority.index = index;

		for (sub_index = 0; sub_index < MAX_VARIABLES; ++sub_index)
		{
			int compound_index = index + 1000 * sub_index;
			WleAEInteractionDoorVarPropUI *new_var_entry = calloc(1, sizeof(WleAEInteractionDoorVarPropUI));
			eaPush(&wleAEGlobalInteractionPropUI.entries[index]->doorVariables, new_var_entry);
		
			new_var_entry->var.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
			new_var_entry->var.left_pad = WLE_AE_INTERACTION_INDENT+20;
			new_var_entry->var.entry_width = 1.0;
			new_var_entry->var.update_func = wleAEInteractionPropDoorVarUpdate;
			new_var_entry->var.apply_func = wleAEInteractionPropDoorVarApply;
			new_var_entry->var.index = compound_index;
			new_var_entry->var.can_unspecify = true;
			new_var_entry->var.source_map_name = SAFE_MEMBER(zmapGetInfo(NULL), map_name);
			new_var_entry->var.dest_map_name = NULL;
		}
	}

	// Child Geo Properties
	wleAEGlobalInteractionPropUI.data.childSelect.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.childSelect.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.childSelect.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.childSelect.update_func = wleAEInteractionPropChildSelectUpdate;
	wleAEGlobalInteractionPropUI.data.childSelect.apply_func = wleAEInteractionPropChildSelectApply;
	wleAEGlobalInteractionPropUI.data.childSelect.can_unspecify = true;

	wleAEGlobalInteractionPropUI.data.childSelectExpr.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.childSelectExpr.left_pad = WLE_AE_INTERACTION_INDENT+20;
	wleAEGlobalInteractionPropUI.data.childSelectExpr.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.childSelectExpr.context = g_pInteractionNonPlayerContext;
	wleAEGlobalInteractionPropUI.data.childSelectExpr.update_func = wleAEInteractionPropChildSelectExprUpdate;
	wleAEGlobalInteractionPropUI.data.childSelectExpr.apply_func = wleAEInteractionPropChildSelectExprApply;

	wleAEGlobalInteractionPropUI.data.applyAsNode.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.applyAsNode.left_pad = WLE_AE_INTERACTION_HEADING;
	wleAEGlobalInteractionPropUI.data.applyAsNode.entry_width = 140;
	wleAEGlobalInteractionPropUI.data.applyAsNode.update_func = wleAEInteractionPropApplyAsNodeUpdate;
	wleAEGlobalInteractionPropUI.data.applyAsNode.apply_func = wleAEInteractionPropApplyAsNodeApply;
	eaPush(&wleAEGlobalInteractionPropUI.data.applyAsNode.available_values, "Node");
	eaPush(&wleAEGlobalInteractionPropUI.data.applyAsNode.available_values, "Volume");

	wleAEGlobalInteractionPropUI.data.allowExplicitHide.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.allowExplicitHide.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.allowExplicitHide.update_func = wleAEInteractionPropExplicitHideUpdate;
	wleAEGlobalInteractionPropUI.data.allowExplicitHide.apply_func = wleAEInteractionPropExplicitHideApply;

	wleAEGlobalInteractionPropUI.data.startsHidden.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.startsHidden.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.startsHidden.update_func = wleAEInteractionPropStartsHiddenUpdate;
	wleAEGlobalInteractionPropUI.data.startsHidden.apply_func = wleAEInteractionPropStartsHiddenApply;

	wleAEGlobalInteractionPropUI.data.interactDist.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.interactDist.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.interactDist.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.interactDist.update_func = wleAEInteractionPropDistanceUpdate;
	wleAEGlobalInteractionPropUI.data.interactDist.apply_func = wleAEInteractionPropDistanceApply;

	wleAEGlobalInteractionPropUI.data.targetDist.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.targetDist.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.targetDist.entry_width = WLE_AE_INTERACTION_NUM_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.targetDist.update_func = wleAEInteractionPropTargetDistanceUpdate;
	wleAEGlobalInteractionPropUI.data.targetDist.apply_func = wleAEInteractionPropTargetDistanceApply;

	wleAEGlobalInteractionPropUI.data.overrideFX.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.overrideFX.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.overrideFX.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.overrideFX.update_func = wleAEInteractionPropOverrideFXUpdate;
	wleAEGlobalInteractionPropUI.data.overrideFX.apply_func = wleAEInteractionPropOverrideFXApply;
	eaPush(&wleAEGlobalInteractionPropUI.data.overrideFX.available_values, ""); // Add a 'None' entry
	dynFxInfoGetAllNames(&wleAEGlobalInteractionPropUI.data.overrideFX.available_values);
	eaQSort(wleAEGlobalInteractionPropUI.data.overrideFX.available_values, strCmp);

	wleAEGlobalInteractionPropUI.data.additionalFX.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.additionalFX.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.additionalFX.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.additionalFX.update_func = wleAEInteractionPropAdditionalFXUpdate;
	wleAEGlobalInteractionPropUI.data.additionalFX.apply_func = wleAEInteractionPropAdditionalFXApply;
	eaPush(&wleAEGlobalInteractionPropUI.data.additionalFX.available_values, ""); // Add a 'None' entry
	dynFxInfoGetAllNames(&wleAEGlobalInteractionPropUI.data.additionalFX.available_values);
	eaQSort(wleAEGlobalInteractionPropUI.data.additionalFX.available_values, strCmp);

	wleAEGlobalInteractionPropUI.data.isUntargetable.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.isUntargetable.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.isUntargetable.update_func = wleAEInteractionPropUntargetableUpdate;
	wleAEGlobalInteractionPropUI.data.isUntargetable.apply_func = wleAEInteractionPropUntargetableApply;

	wleAEGlobalInteractionPropUI.data.canTabSelect.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.canTabSelect.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.canTabSelect.update_func = wleAEInteractionPropCanTabSelectUpdate;
	wleAEGlobalInteractionPropUI.data.canTabSelect.apply_func = wleAEInteractionPropCanTabSelectApply;

	wleAEGlobalInteractionPropUI.data.displayNameBasic.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.displayNameBasic.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.displayNameBasic.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.displayNameBasic.update_func = wleAEInteractionPropDisplayNameBasicUpdate;
	wleAEGlobalInteractionPropUI.data.displayNameBasic.apply_func = wleAEInteractionPropDisplayNameBasicApply;
	sprintf(buf, "displayNameBasic");
	wleAEGlobalInteractionPropUI.data.displayNameBasic.source_key = strdup(buf);

	wleAEGlobalInteractionPropUI.data.interactionTypeTag.entry_align = WLE_AE_INTERACTION_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.interactionTypeTag.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.interactionTypeTag.entry_width = WLE_AE_INTERACTION_TEXT_ENTRY_WIDTH;
	wleAEGlobalInteractionPropUI.data.interactionTypeTag.update_func = wleAEInteractionPropTypeTagUpdate;
	wleAEGlobalInteractionPropUI.data.interactionTypeTag.apply_func = wleAEInteractionPropTypeTagApply;

	wleAEGlobalInteractionPropUI.data.bVisEvalPerEnt.entry_align = WLE_AE_INTERACTION_DEEP_ALIGN_WIDTH;
	wleAEGlobalInteractionPropUI.data.bVisEvalPerEnt.left_pad = WLE_AE_INTERACTION_INDENT;
	wleAEGlobalInteractionPropUI.data.bVisEvalPerEnt.update_func = wleAEInteractionPropVisibleBoolUpdate;
	wleAEGlobalInteractionPropUI.data.bVisEvalPerEnt.apply_func = wleAEInteractionPropVisibleBoolApply;

}

#endif
