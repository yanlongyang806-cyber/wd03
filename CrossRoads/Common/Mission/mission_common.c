/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "allegiance.h"
#include "CharacterClass.h"
#include "contact_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "estring.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gameaction_common.h"
#include "GameStringFormat.h"
#include "itemcommon.h"
#include "MapDescription.h"
#include "mapstate_common.h"
#include "MemoryPool.h"
#include "mission_common.h"
#include "missionset_common.h"
#include "nemesis_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "ResourceManager.h"
#include "rewardcommon.h"
#include "storeCommon.h"
#include "StringCache.h"
#include "worldgrid.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "../StaticWorld/group.h"


#ifdef GAMESERVER
#include "gslMission.h"
#include "gslMissionDrop.h"
#include "gslMissionEvents.h"
#include "gslMission_transact.h"
#include "gslEventTracker.h"
#endif

#ifdef GAMECLIENT
#include "UIGen.h"
#include "gclEntity.h"
#endif

#include "AutoGen/mission_common_h_ast.h"
#include "Autogen/mission_enums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Context to track all data-defined tags
DefineContext *g_pDefineMissionTags = NULL;
// Context to track all data-defined UITypes
DefineContext *g_pDefineMissionUITypes = NULL;
// Context to track all mission warp costs
DefineContext* g_pDefineMissionWarpCost = NULL;

DefineContext *g_pDefineTutorialScreenRegions = NULL;

// Global data associated with each MissionUIType
MissionUITypes g_MissionUITypes = {0};

TutorialScreenRegions g_TutorialScreenRegions = {0};
S32 g_iNextTutorialScreenRegion;

DictionaryHandle g_MissionDictionary = NULL;

DictionaryHandle g_MissionCategoryDict = NULL;
DictionaryHandle g_MissionTemplateTypeDict = NULL;
DictionaryHandle g_MissionVarTableDict = NULL;
MissionTemplateType **g_MissionTemplateTypeList;

MissionSystemMessages g_MissionSystemMsgs = {0};

bool gValidateContactsOnNextTick = false;
const char** g_eaMissionsWaitingForOverrideProcessing = NULL;

static MissionWarpCostDefs s_MissionWarpCostDefs = {0};

// Pooled versions of Expression var names, for performance
const char *g_MissionVarName;
const char *g_MissionDefVarName;
const char *g_PlayerVarName;
const char *g_EncounterVarName;
const char *g_Encounter2VarName;
const char *g_EncounterTemplateVarName;


// ----------------------------------------------------------------------------------
// Type definitions and forward declarations
// ----------------------------------------------------------------------------------

#define MISSION_COOLDOWN_PST_OFFSET 8

// ----------------------------------------------------------------------------------
// AUTO_RUNs for initialization
// ----------------------------------------------------------------------------------

MP_DEFINE(Mission);

AUTO_RUN;
void missionsystem_Init(void)
{
	MP_CREATE_COMPACT(Mission, 128, 256, 0.80);

	g_MissionVarName = allocAddString("Mission");
	g_MissionDefVarName = allocAddString("MissionDef");
	g_PlayerVarName = allocAddString("Player");
	g_EncounterVarName = allocAddString("Encounter");
	g_Encounter2VarName = allocAddString("Encounter2");
	g_EncounterTemplateVarName = allocAddString("EncounterTemplate");
}

AUTO_RUN_LATE;
void missionsystem_InitMessages(void)
{
	SET_HANDLE_FROM_STRING(gMessageDict, "MissionSystem.FlashbackDisplayName", g_MissionSystemMsgs.hFlashbackDisplayName);
}


// ----------------------------------------------------------------------------------
// Mission Def Validation
// ----------------------------------------------------------------------------------

static bool missiondef_ValidateCond(MissionDef *pRootDef, MissionEditCond *pCond)
{
	bool bResult = true;
	int i;

	if (!pCond) {
		return true;
	}

	if (pCond->type == MissionCondType_Objective) {
		if (!missiondef_FindMissionByName(pRootDef, pCond->valStr)) {
			ErrorFilenamef( pRootDef->filename, "Unknown mission %s in condition", pCond->valStr);
			bResult = false;
		}
	} else if (((pCond->type == MissionCondType_Objective) || (pCond->type == MissionCondType_Expression)) && !pCond->valStr) {
		ErrorFilenamef( pRootDef->filename, "Empty conditions are not allowed");
		bResult = false;
	} else if (pCond->type == MissionCondType_Count) {
		if(pCond->iCount < 1)
		{
			ErrorFilenamef( pRootDef->filename, "Condition count must be at least 1 (%d)", pCond->iCount);
			bResult = false;
		} else if(pCond->iCount > eaSize(&pCond->subConds)) {
			ErrorFilenamef( pRootDef->filename, "Condition count must not be greater than the number of sub conditions (%d > %d)", pCond->iCount, eaSize(&pCond->subConds));
			bResult = false;
		}
	}

	// Recurse
	for(i=eaSize(&pCond->subConds)-1; i>=0; --i) {
		bResult &= missiondef_ValidateCond(pRootDef, pCond->subConds[i]);
	}

	return bResult;
}


// Pre-grant Mission Drops should only be used to drop Mission Grant items that grant this Mission.
static bool missiondef_ValidatePreGrantDropRewardTableRecursive(MissionDef *pDef, RewardTable *pTable)
{
	if (pTable)	{
		int i, n = eaSize(&pTable->ppRewardEntry);
		for (i = 0; i < n; i++)	{
			bool bResult = false;
			if (pTable->ppRewardEntry[i]->ChoiceType == kRewardChoiceType_Empty)	{
				// Empty things are OK
				bResult = true;
			} else if (pTable->ppRewardEntry[i]->ChoiceType == kRewardChoiceType_Choice
					   && pTable->ppRewardEntry[i]->Type == kRewardType_Item) {
				ItemDef *pItemDef = GET_REF(pTable->ppRewardEntry[i]->hItemDef);

				if (pItemDef && pItemDef->eType == kItemType_MissionGrant) {
					MissionDef *pGrantedMissionDef = GET_REF(pItemDef->hMission);
					if (pGrantedMissionDef && pDef && !stricmp(pGrantedMissionDef->name, pDef->name)) {
						bResult = true;
					}
				}
			}
			
			if (!bResult) {
				ErrorFilenamef(pDef->filename, "Invalid entry in PreMission Mission Drop.  Can only give MissionGrant Items that grant this Mission!");
				return false;
			}
		}
		return true;
	}
	return false;
}

static bool missiondef_ValidateNumericScale(MissionDef *pDef, MissionNumericScale *pScale)
{
	bool bResult = true;

	if (IsServer())
	{
		if (!pScale->pchNumeric || !pScale->pchNumeric[0])
		{
			ErrorFilenamef(pDef->filename, "Mission has a numeric scale entry with an empty numeric name");
			bResult = false;
		}
		else
		{
			ItemDef* pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, pScale->pchNumeric);
			if (!pItemDef)
			{
				ErrorFilenamef(pDef->filename, "Mission has a numeric scale entry that refers to a non-existent numeric '%s'", pScale->pchNumeric);
				bResult = false;
			}
			else if (pItemDef->eType != kItemType_Numeric)
			{
				ErrorFilenamef(pDef->filename, "Mission has a numeric scale entry with a non-numeric item specified '%s'", pScale->pchNumeric);
				bResult = false;
			}
		}
	}

	return bResult;
}

static bool missiondef_ValidateDrop(MissionDef *pDef, MissionDrop *pDrop)
{
	bool bResult = true;

	if (IsServer() && (pDrop->RewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDrop->RewardTableName))) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDrop->RewardTableName);
		bResult = false;
	}

	if (IsServer && (pDrop->RewardTableName && !killreward_Validate(RefSystem_ReferentFromString(g_hRewardTableDict, pDrop->RewardTableName), pDef->filename)))
	{
		ErrorFilenamef( pDef->filename, "Mission drop reward table cannot be granted from killed entities.");
		bResult = false;
	}

	if (IsServer() && (pDrop->type == MissionDropTargetType_Critter) && pDrop->value && !RefSystem_ReferentFromString(g_hCritterDefDict, pDrop->value)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent critter '%s'", pDrop->value);
		bResult = false;
	}

	if (IsServer() && (pDrop->type == MissionDropTargetType_Group) && pDrop->value && !RefSystem_ReferentFromString(g_hCritterGroupDict, pDrop->value)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent critter group '%s'", pDrop->value);
		bResult = false;
	}

	if (IsServer() && (pDrop->type == MissionDropTargetType_Nemesis || pDrop->type == MissionDropTargetType_NemesisMinion) && missiondef_GetType(pDef) != MissionType_Nemesis && missiondef_GetType(pDef) != MissionType_NemesisArc && missiondef_GetType(pDef) != MissionType_NemesisSubArc) {
		ErrorFilenamef( pDef->filename, "Nemesis MissionDrops can only be used on Nemesis, NemesisArc, or NemesisSubArc missions");
		bResult = false;
	}

	// Pre-grant Mission Drops should only be used to drop Mission Grant items that grant this Mission.
	// Throw an error if this is not the case.
	if (IsServer() && pDrop->whenType == MissionDropWhenType_PreMission) {
		RewardTable *pRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDrop->RewardTableName);
		bResult &= missiondef_ValidatePreGrantDropRewardTableRecursive(pDef, pRewardTable);
	}

	return bResult;
}


static bool missiondef_ConditionHasEventCount(MissionEditCond *pCond)
{
	int i;
	if (pCond && pCond->type == MissionCondType_Expression) {
		if (pCond->valStr && strstri(pCond->valStr, "missioneventcount")) {
			return true;
		}
	}
	if (pCond) {
		for (i = eaSize(&pCond->subConds)-1; i>=0; --i) {
			if (missiondef_ConditionHasEventCount(pCond->subConds[i])) {
				return true;
			}
		}
	}
	return false;
}


static bool missiondef_ValidateRequest(MissionDef *pDef, MissionDefRequest *pRequest)
{
	bool bResult = true;

	if (pRequest->eType == MissionDefRequestType_Mission) {
		MissionDef *pRequestedDef = GET_REF(pRequest->hRequestedDef);
		if (pRequestedDef) {
			if (!pRequestedDef->eRequestGrantType) {
				ErrorFilenamef(pDef->filename, "Mission %s is requesting %s, which doesn't have a Request grant type", pDef->pchRefString, pRequestedDef->pchRefString);
				bResult = false;
			}
		} else if (!pRequestedDef && IS_HANDLE_ACTIVE(pRequest->hRequestedDef)){
			ErrorFilenamef(pDef->filename, "Mission %s references invalid MissionDef %s", pDef->pchRefString, REF_STRING_FROM_HANDLE(pRequest->hRequestedDef));
			bResult = false;
		} else if (!pRequestedDef){
			ErrorFilenamef(pDef->filename, "Mission %s has a MissionRequest with no Mission specified", pDef->pchRefString);
			bResult = false;
		}
	} else if (pRequest->eType == MissionDefRequestType_MissionSet){
		MissionSet *pRequestedSet = GET_REF(pRequest->hRequestedMissionSet);
		if (pRequestedSet) {
			// All Missions within this MissionSet must be requestable
			int i;
			for (i = eaSize(&pRequestedSet->eaEntries)-1; i>=0; --i){
				MissionDef *pRequestedDef = GET_REF(pRequestedSet->eaEntries[i]->hMissionDef);
				if (pRequestedDef && !pRequestedDef->eRequestGrantType){
					ErrorFilenamef(pDef->filename, "Mission %s is requesting %s, which doesn't have a Request grant type", pDef->pchRefString, pRequestedDef->pchRefString);
					bResult = false;
				}
			}
			if (!eaSize(&pRequestedSet->eaEntries)) {
				ErrorFilenamef(pDef->filename, "Mission %s is requesting MissionSet %s, which is empty!", pDef->pchRefString, pRequestedSet->pchName);
				bResult = false;
			}
		} else if (!pRequestedSet && IS_HANDLE_ACTIVE(pRequest->hRequestedMissionSet)) {
			ErrorFilenamef(pDef->filename, "Mission %s references invalid MissionSet %s", pDef->pchRefString, REF_STRING_FROM_HANDLE(pRequest->hRequestedMissionSet));
			bResult = false;
		} else if (!pRequestedSet) {
			ErrorFilenamef(pDef->filename, "Mission %s has a MissionRequest with no MissionSet specified", pDef->pchRefString);
			bResult = false;
		}
	}

	return bResult;
}


// Not a full validation function yet.  Something is better than nothing.
bool missiondef_Validate(MissionDef *pDef, MissionDef *pRootDef, bool bIsChild)
{
	MissionDef **eaGrantedMissions = NULL;
	const char *pcTempFileName;
	S32 i, j;
	bool bResult = true;

	if ( !resIsValidName(pDef->name) ) {
		ErrorFilenamef( pDef->filename, "Mission name is illegal: '%s'", pDef->name );
		bResult = false;
	}

	if( !bIsChild && !resIsValidScope(pDef->scope) ) {
		ErrorFilenamef( pDef->filename, "Mission scope is illegal: '%s'", pDef->scope );
		bResult = false;
	} else if (bIsChild && pDef->scope) {
		ErrorFilenamef( pDef->filename, "Child mission '%s' should not have a scope", pDef->name );
		bResult = false;
	}

	if (!bIsChild) {
		pcTempFileName = pDef->filename;
		if (resFixPooledFilename(&pcTempFileName, pDef->scope && resIsInDirectory(pDef->scope, "maps/") ? NULL : "defs/missions", pDef->scope, pDef->name, "mission"))  {
			if (IsServer()) {				
				char nameSpace[RESOURCE_NAME_MAX_SIZE];
				char baseObjectName[RESOURCE_NAME_MAX_SIZE];
				char baseObjectName2[RESOURCE_NAME_MAX_SIZE];
				if (!resExtractNameSpace(pcTempFileName, nameSpace, baseObjectName) || 
					!resExtractNameSpace(pDef->filename, nameSpace, baseObjectName2) ||
					stricmp(baseObjectName, baseObjectName2) != 0) {
					ErrorFilenamef( pDef->filename, "Mission filename does not match name '%s' scope '%s'", pDef->name, pDef->scope);
					bResult = false;
				}
			}
		}
	}

	// Level Validation
	if (!bIsChild) {
		switch(pDef->levelDef.eLevelType) 
		{
			xcase MissionLevelType_Specified:
				if (pDef->levelDef.missionLevel < 1 || pDef->levelDef.missionLevel > MAX_LEVELS) {
					ErrorFilenamef( pDef->filename, "Mission %s level is out of bounds: %d (should be between 1 and %d)", pDef->name, pDef->levelDef.missionLevel, MAX_LEVELS);
					bResult = false;
				}

			xcase MissionLevelType_PlayerLevel:
				if (pDef->levelDef.pLevelClamp) {
					switch(pDef->levelDef.pLevelClamp->eClampType) 
					{
						xcase MissionLevelClampType_Specified:
							if (pDef->levelDef.pLevelClamp->iClampSpecifiedMin < 0 || pDef->levelDef.pLevelClamp->iClampSpecifiedMin > MAX_LEVELS) {
								ErrorFilenamef( pDef->filename, "Mission %s: specified minimum level clamp is out of bounds: %d (should be between 0 and %d)", pDef->name, pDef->levelDef.pLevelClamp->iClampSpecifiedMin, MAX_LEVELS);
								bResult = false;
							}

							if (pDef->levelDef.pLevelClamp->iClampSpecifiedMax < 0 || pDef->levelDef.pLevelClamp->iClampSpecifiedMax > MAX_LEVELS) {
								ErrorFilenamef( pDef->filename, "Mission %s: specified maximum level clamp is out of bounds: %d (should be between 0 and %d)", pDef->name, pDef->levelDef.pLevelClamp->iClampSpecifiedMax, MAX_LEVELS);
								bResult = false;
							}

							if (pDef->levelDef.pLevelClamp->iClampSpecifiedMin && pDef->levelDef.pLevelClamp->iClampSpecifiedMax && (pDef->levelDef.pLevelClamp->iClampSpecifiedMin > pDef->levelDef.pLevelClamp->iClampSpecifiedMax)) {
								ErrorFilenamef( pDef->filename, "Mission %s: specified minimum level clamp (%d) is greater than the specified maximum level clamp (%d)", pDef->name, pDef->levelDef.pLevelClamp->iClampSpecifiedMin, pDef->levelDef.pLevelClamp->iClampSpecifiedMax);
								bResult = false;
							}
						xcase MissionLevelClampType_MapVariable:
							if (!EMPTY_TO_NULL(pDef->levelDef.pLevelClamp->pcClampMapVariable)) {
								ErrorFilenamef( pDef->filename, "Mission %s uses a map variable as a level clamp, but no map variable is specified", pDef->name);
								bResult = false;
							}
						// not xcase since both MapLevel and MapVariable have specified offsets
						case MissionLevelClampType_MapLevel:
							if (pDef->levelDef.pLevelClamp->iClampOffsetMin >= MAX_LEVELS) {
								ErrorFilenamef( pDef->filename, "Mission %s: minimum level clamp offset is too large: %d (should be less than %d)", pDef->name, pDef->levelDef.pLevelClamp->iClampOffsetMin, MAX_LEVELS);
								bResult = false;
							}

							if (pDef->levelDef.pLevelClamp->iClampOffsetMax >= MAX_LEVELS) {
								ErrorFilenamef( pDef->filename, "Mission %s: maximum level clamp offset is too large: %d (should be less than %d)", pDef->name, pDef->levelDef.pLevelClamp->iClampOffsetMax, MAX_LEVELS);
								bResult = false;
							}

							if (pDef->levelDef.pLevelClamp->iClampOffsetMin > pDef->levelDef.pLevelClamp->iClampOffsetMax) {
								ErrorFilenamef( pDef->filename, "Mission %s: minimum level clamp offset (%d) is greater than the maximum level clamp offset (%d)", pDef->name, pDef->levelDef.pLevelClamp->iClampOffsetMin, pDef->levelDef.pLevelClamp->iClampOffsetMax);
								bResult = false;
							}
					}
				} 
			xcase MissionLevelType_MapVariable:
				if (!EMPTY_TO_NULL(pDef->levelDef.pchLevelMapVar)) {
					ErrorFilenamef( pDef->filename, "Mission %s uses a map variable to calculate its level, but no map variable is specified", pDef->name);
					bResult = false;
				}
			case MissionLevelType_MapLevel:
				if (pDef->missionType != MissionType_OpenMission && pDef->eShareable != MissionShareableType_Never) {
					ErrorFilenamef( pDef->filename, "Mission %s uses a map variable or map level to calculate its level, but is not of type Open Mission and is Sharable.  The mission type must be set to 'Open Mission' or the sharable type must be set to 'Never'", pDef->name);
					bResult = false;
				}
		}
	}

	// Check that if the mission scales with team size, we don't also have a team size set
	if (!bIsChild && pDef->bScalesForTeamSize && pDef->iSuggestedTeamSize != 0) {
		ErrorFilenamef( pDef->filename, "Mission %s: Team Size should be 0 when 'Scales For Team' option is selected.", pDef->name);
		bResult = false;
	}

	// Validate Events
	for (i = 0; i < eaSize(&pDef->eaTrackedEvents); i++) {
		bool bSuccess;
		char *estrError;

		// Validate Event Names
		if (!pDef->eaTrackedEvents[i]->pchEventName){
			ErrorFilenamef( pDef->filename, "Mission '%s' has an Event with no name!", pDef->name);
			bResult = false;
		} else {
			for (j = 0; j < i; j++){
				if (pDef->eaTrackedEvents[j]->pchEventName && !stricmp(pDef->eaTrackedEvents[i]->pchEventName, pDef->eaTrackedEvents[j]->pchEventName)){
					ErrorFilenamef( pDef->filename, "Mission '%s' has duplicate Event Names: '%s'", pDef->name, pDef->eaTrackedEvents[i]->pchEventName);
					bResult = false;
					break;
				}
			}
		}
		estrStackCreate(&estrError);
		bSuccess = gameevent_Validate(pDef->eaTrackedEvents[i], &estrError, pDef->eaTrackedEvents[i]->pchEventName, (missiondef_GetType(pDef) != MissionType_OpenMission));
		if (!bSuccess) {
			ErrorFilenamef(pDef->filename, "%s", estrError);
		}
		estrDestroy(&estrError);
	}

	// Validate Open Mission scoreboard events
	for (i = 0; i < eaSize(&pDef->eaOpenMissionScoreEvents); i++) {
		bool bSuccess;
		char *estrError;
		GameEvent *pEvent = pDef->eaOpenMissionScoreEvents[i]->pEvent;

		if (pEvent) {
			// Open Mission scoreboard events must be scoped to a player, so that it knows who to give points to
			if (pEvent->tMatchSource != TriState_Yes && pEvent->tMatchTarget != TriState_Yes){
				ErrorFilenamef( pDef->filename, "Open Mission '%s' has an unscoped Scoreboard event.  Scoreboard Events must have either 'MatchSource' or 'MatchTarget' set.", pDef->pchRefString);
			}

			estrStackCreate(&estrError);
			bSuccess = gameevent_Validate(pEvent, &estrError, "Scoreboard Event", true);
			if (!bSuccess) {
				ErrorFilenamef(pDef->filename, "%s", estrError);
			}
			estrDestroy(&estrError);
		}
	}

	if (IsServer() && !GET_REF(pDef->parentDef) && REF_STRING_FROM_HANDLE(pDef->parentDef)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent parent mission '%s'", REF_STRING_FROM_HANDLE(pDef->parentDef));
		bResult = false;
	}

	// Validate messages
	if (IsServer() && !GET_REF(pDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		bResult = false;
	}
	if (IsServer() && !GET_REF(pDef->uiStringMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->uiStringMsg.hMessage)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->uiStringMsg.hMessage));
		bResult = false;
	}
	if (IsServer() && !GET_REF(pDef->detailStringMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->detailStringMsg.hMessage)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->detailStringMsg.hMessage));
		bResult = false;
	}
	if (IsServer() && !GET_REF(pDef->summaryMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->summaryMsg.hMessage)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->summaryMsg.hMessage));
		bResult = false;
	}
	if (IsServer() && !GET_REF(pDef->msgReturnStringMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->msgReturnStringMsg.hMessage)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->msgReturnStringMsg.hMessage));
		bResult = false;
	}
	if (IsServer() && !GET_REF(pDef->splatDisplayMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->splatDisplayMsg.hMessage)) {
		ErrorFilenamef( pDef->filename, "Mission refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->splatDisplayMsg.hMessage));
		bResult = false;
	}

	// Check actions
	bResult &= gameaction_ValidateActions(&pDef->ppOnStartActions, NULL, pRootDef, pDef, true, pDef->filename);
	bResult &= gameaction_ValidateActions(&pDef->ppSuccessActions, NULL, pRootDef, pDef, true, pDef->filename);
	bResult &= gameaction_ValidateActions(&pDef->ppFailureActions, NULL, pRootDef, pDef, true, pDef->filename);
	bResult &= gameaction_ValidateActions(&pDef->ppOnReturnActions, NULL, pRootDef, pDef, true, pDef->filename);
	
	// Make sure there are no GiveItem Actions on missions for unsafe items, or ChangeNemesisState actions except on mission return
	for (i=0; i < eaSize(&pDef->ppOnStartActions); i++){
		if (pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_GiveItem){
			ItemDef * pItemDef = GET_REF(pDef->ppOnStartActions[i]->pGiveItemProperties->hItemDef);
			if (item_IsUnsafeGrant(pItemDef))
			{
				ErrorFilenamef( pDef->filename, "You are not allowed to use GiveItem Actions with this type of item on Missions");
				bResult = false;
			}
		}
		if (pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_ChangeNemesisState){
			ErrorFilenamef( pDef->filename, "ChangeNemesisState Actions are only allowed as OnReturn actions on Missions");
			bResult = false;
		}
		if (pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_GADAttribValue){
			ErrorFilenamef( pDef->filename, "GameAccount Actions are only allowed as OnReturn actions on Missions");
			bResult = false;
		}
	}
	for (i=0; i < eaSize(&pDef->ppSuccessActions); i++){
		if (pDef->ppSuccessActions[i]->eActionType == WorldGameActionType_GiveItem){
			ItemDef * pItemDef = GET_REF(pDef->ppSuccessActions[i]->pGiveItemProperties->hItemDef);
			if (item_IsUnsafeGrant(pItemDef))
			{
				ErrorFilenamef( pDef->filename, "You are not allowed to use GiveItem Actions with this type of item on Missions");
				bResult = false;
			}
		}
		if (pDef->ppSuccessActions[i]->eActionType == WorldGameActionType_ChangeNemesisState){
			ErrorFilenamef( pDef->filename, "ChangeNemesisState Actions are only allowed as OnReturn actions on Missions");
			bResult = false;
		}
		if (pDef->ppSuccessActions[i]->eActionType == WorldGameActionType_GADAttribValue){
			ErrorFilenamef( pDef->filename, "GameAccount Actions are only allowed as OnReturn actions on Missions");
			bResult = false;
		}
	}
	for (i=0; i < eaSize(&pDef->ppFailureActions); i++){
		if (pDef->ppFailureActions[i]->eActionType == WorldGameActionType_GiveItem){
			ItemDef * pItemDef = GET_REF(pDef->ppFailureActions[i]->pGiveItemProperties->hItemDef);
			if (item_IsUnsafeGrant(pItemDef))
			{
				ErrorFilenamef( pDef->filename, "You are not allowed to use GiveItem Actions with this type of item on Missions");
				bResult = false;
			}
		}
		if (pDef->ppFailureActions[i]->eActionType == WorldGameActionType_ChangeNemesisState){
			ErrorFilenamef( pDef->filename, "ChangeNemesisState Actions are only allowed as OnReturn actions on Missions");
			bResult = false;
		}
		if (pDef->ppFailureActions[i]->eActionType == WorldGameActionType_GADAttribValue){
			ErrorFilenamef( pDef->filename, "GameAccount Actions are only allowed as OnReturn actions on Missions");
			bResult = false;
		}
	}
	for (i=0; i < eaSize(&pDef->ppOnReturnActions); i++){
		if (pDef->ppOnReturnActions[i]->eActionType == WorldGameActionType_GiveItem){
			ItemDef * pItemDef = GET_REF(pDef->ppOnReturnActions[i]->pGiveItemProperties->hItemDef);
			if (item_IsUnsafeGrant(pItemDef))
			{
				ErrorFilenamef( pDef->filename, "You are not allowed to use GiveItem Actions with this type of item on Missions");
				bResult = false;
			}
		}
	}

#ifdef GAMESERVER
	// For Perks, check that they don't do anything OnStart that would cause them to lock too much data
	if (!bIsChild && missiondef_GetType(pDef) == MissionType_Perk &&
		(missiondef_MustLockInventoryOnStart(pDef, pDef) 
		|| missiondef_MustLockMissionsOnStart(pDef, pDef)
		|| missiondef_MustLockNemesisOnStart(pDef, pDef))) {
			ErrorFilenamef( pDef->filename, "Error: Perks are not allowed to modify the player with any OnStart actions or rewards");
		bResult = false;
	}
#endif

	// Check conditions
	bResult &= missiondef_ValidateCond(pRootDef, pDef->meSuccessCond);
	bResult &= missiondef_ValidateCond(pRootDef, pDef->meFailureCond);
	bResult &= missiondef_ValidateCond(pRootDef, pDef->meResetCond);

	// Validate Rewards
	if (pDef->params) {
		RewardTable *pTable = NULL;
			
		// Validate OnStart Rewards
		if (IsServer() && pDef->params->OnstartRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnstartRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->OnstartRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->OnstartRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnstartRewardTableName))) {
			// Clickable and Choose rewards are not allowed here
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_MissionReward) || rewardTable_HasItemsWithType(pTable, kRewardPickupType_Choose, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in OnStart Reward Table '%s'", pDef->params->OnstartRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in OnStart Reward Table '%s'", pDef->params->OnstartRewardTableName);
				bResult = false;
			}

			// Perks cannot have OnStart rewards
			if (missiondef_GetType(pRootDef) == MissionType_Perk){
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in OnStart Reward Table '%s'", pDef->params->OnstartRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in OnStart Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->OnstartRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission onstart has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->OnstartRewardTableName);
				bResult = false;
			}
		}

		// Validate OnFailure Rewards
		if (IsServer() && pDef->params->OnfailureRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnfailureRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->OnfailureRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->OnfailureRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnfailureRewardTableName))) {
			if (rewardTable_HasUnsafeDirectGrants(pTable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pTable, kRewardPickupType_Choose, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in OnFailure Reward Table '%s'", pDef->params->OnfailureRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in OnFailure Reward Table '%s'", pDef->params->OnfailureRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in OnFailure Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->OnfailureRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission failure has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->OnfailureRewardTableName);
				bResult = false;
			}

		}

		// Validate OnSuccess Rewards
		if (IsServer() && pDef->params->OnsuccessRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnsuccessRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->OnsuccessRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->OnsuccessRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnsuccessRewardTableName))) {
			// Direct items in OnSuccess Rewards are OK if the Success Condition is "0", because that means
			// the mission can never complete without some external transaction
			if (rewardTable_HasUnsafeDirectGrants(pTable, RewardContextType_SubMissionTurnIn) && !(pDef->meSuccessCond && pDef->meSuccessCond->type == MissionCondType_Expression && !stricmp(pDef->meSuccessCond->valStr, "0"))) {
				RewardTable *pErrorRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnsuccessRewardTableName);
				ErrorFilenameTwof( pDef->filename, pTable->pchFileName, "Mission has direct or choose item grants in OnSuccess Reward Table '%s'", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

			// Clickable and Choose rewards are always wrong (the UI and transactions don't support Choices here)
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_SubMissionTurnIn) || rewardTable_HasItemsWithType(pTable, kRewardPickupType_Choose, RewardContextType_SubMissionTurnIn)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in OnSuccess Reward Table '%s'", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_SubMissionTurnIn)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in OnSuccess Reward Table '%s'", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in OnSuccess Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

			// Root Missions probably shouldn't grant Mission Items on Success, so this is probably a mistake.
			// (If someone has a legitimate reason to do so, feel free to remove this - but it caused 
			// problems with Perk titles once)
			if (!bIsChild && rewardTable_HasMissionItems(pTable, true, pDef)){
				ErrorFilenamef( pDef->filename, "Mission grants Mission Items in its OnSuccess Reward Table '%s'.  This is probably a mistake.", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission success has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

		}

		// Validate OnReturn Rewards
		if (IsServer() && pDef->params->OnreturnRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnreturnRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->OnreturnRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->OnreturnRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnreturnRewardTableName))) {
			// Item rewards are disallowed if the mission doesn't need to be returned to a Contact and is not a perk mission
			if (!pDef->needsReturn && rewardTable_HasUnsafeDirectGrants(pTable, RewardContextType_MissionReward) && pDef->missionType != MissionType_Perk) {
				ErrorFilenamef( pDef->filename, "Mission has direct or choose item grants in OnReturn Reward Table '%s', but doesn't need to be returned to a Contact", pDef->params->OnreturnRewardTableName);
				bResult = false;
			}

			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in OnReturn Reward Table '%s'", pDef->params->OnreturnRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in OnReturn Reward Table '%s'", pDef->params->OnreturnRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in OnReturn Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->OnreturnRewardTableName);
				bResult = false;
			}

			// Missions probably shouldn't grant Mission Items on Return, so this is probably a mistake.
			// (If someone has a legitimate reason to do so, feel free to remove this - but it caused 
			// problems with Perk titles once)
			if (rewardTable_HasMissionItems(pTable, true, pDef)) {
				ErrorFilenamef( pDef->filename, "Mission grants Mission Items in its OnReturn Reward Table '%s'.  This is probably a mistake.", pDef->params->OnsuccessRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission return has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->OnreturnRewardTableName);
				bResult = false;
			}

		}

		// Validate OnReplayReturn Rewards
		if (IsServer() && pDef->params->OnReplayReturnRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnReplayReturnRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->OnReplayReturnRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->OnReplayReturnRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnReplayReturnRewardTableName))) {
			// Item rewards are disallowed if the mission doesn't need to be returned to a Contact
			if (!pDef->needsReturn && rewardTable_HasUnsafeDirectGrants(pTable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has direct or choose item grants in ReplayOnReturn Reward Table '%s', but doesn't need to be returned to a Contact", pDef->params->OnReplayReturnRewardTableName);
				bResult = false;
			}

			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in ReplayOnReturn Reward Table '%s'", pDef->params->OnReplayReturnRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in ReplayOnReturn Reward Table '%s'", pDef->params->OnReplayReturnRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in ReplayOnReturn Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->OnReplayReturnRewardTableName);
				bResult = false;
			}

			// Missions probably shouldn't grant Mission Items on Return, so this is probably a mistake.
			// (If someone has a legitimate reason to do so, feel free to remove this - but it caused 
			// problems with Perk titles once)
			if (rewardTable_HasMissionItems(pTable, true, pDef)) {
				ErrorFilenamef( pDef->filename, "Mission grants Mission Items in its ReplayOnReturn Reward Table '%s'.  This is probably a mistake.", pDef->params->OnReplayReturnRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission replay return has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->OnReplayReturnRewardTableName);
				bResult = false;
			}

		}

		// Validate ActivitySuccess Rewards
		if (IsServer() && pDef->params->ActivitySuccessRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->ActivitySuccessRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->ActivitySuccessRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->ActivitySuccessRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->ActivitySuccessRewardTableName))) {
			// Item rewards are disallowed if the mission doesn't need to be returned to a Contact
			if (!pDef->needsReturn && rewardTable_HasUnsafeDirectGrants(pTable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has direct or choose item grants in ActivitySuccess Reward Table '%s', but doesn't need to be returned to a Contact", pDef->params->ActivitySuccessRewardTableName);
				bResult = false;
			}

			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in ActivitySuccess Reward Table '%s'", pDef->params->ActivitySuccessRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in ActivitySuccess Reward Table '%s'", pDef->params->ActivitySuccessRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in ActivitySuccess Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->ActivitySuccessRewardTableName);
				bResult = false;
			}

			// Missions probably shouldn't grant Mission Items on Return, so this is probably a mistake.
			if (rewardTable_HasMissionItems(pTable, true, pDef)) {
				ErrorFilenamef( pDef->filename, "Mission grants Mission Items in its ActivitySuccess Reward Table '%s'.  This is probably a mistake.", pDef->params->ActivitySuccessRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission activity success has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->ActivitySuccessRewardTableName);
				bResult = false;
			}

		}

		// Validate ActivityReturn Rewards
		if (IsServer() && pDef->params->ActivityReturnRewardTableName && !RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->ActivityReturnRewardTableName)) {
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->ActivityReturnRewardTableName);
			bResult = false;
		}
		if (IsServer() && pDef->params->ActivityReturnRewardTableName && (pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->ActivityReturnRewardTableName))) {
			// Item rewards are disallowed if the mission doesn't need to be returned to a Contact
			if (!pDef->needsReturn && rewardTable_HasUnsafeDirectGrants(pTable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has direct or choose item grants in ActivityReturn Reward Table '%s', but doesn't need to be returned to a Contact", pDef->params->ActivityReturnRewardTableName);
				bResult = false;
			}

			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has invalid item grants in ActivityReturn Reward Table '%s'", pDef->params->ActivityReturnRewardTableName);
				bResult = false;
			}

			// Reward Tables with Items must specify a Pickup Type
			if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_None, RewardContextType_MissionReward)) {
				ErrorFilenamef( pDef->filename, "Mission has unspecified pickup type for item grants in ActivityReturn Reward Table '%s'", pDef->params->ActivityReturnRewardTableName);
				bResult = false;
			}

			// Can't use expressions in mission reward tables
			if (rewardTable_HasExpression(pTable)) {
				ErrorFilenamef( pDef->filename, "Mission has an expression in ActivityReturn Reward Table '%s'.  Expression can't be used in mission reward tables.", pDef->params->ActivityReturnRewardTableName);
				bResult = false;
			}

			// Missions probably shouldn't grant Mission Items on Return, so this is probably a mistake.
			if (rewardTable_HasMissionItems(pTable, true, pDef)) {
				ErrorFilenamef( pDef->filename, "Mission grants Mission Items in its ActivityReturn Reward Table '%s'.  This is probably a mistake.", pDef->params->ActivityReturnRewardTableName);
				bResult = false;
			}

			if(rewardTable_HasAlgorithm(pTable, kRewardAlgorithm_Gated))
			{
				ErrorFilenamef( pDef->filename, "Mission activity return has an kRewardAlgorithm_Gated Reward Table '%s'.  kRewardAlgorithm_Gated can't be used in mission reward tables.", pDef->params->ActivityReturnRewardTableName);
				bResult = false;
			}

		}

		if (((pDef->params->ActivityReturnRewardTableName && pDef->params->ActivityReturnRewardTableName[0]) ||
			 (pDef->params->ActivitySuccessRewardTableName && pDef->params->ActivitySuccessRewardTableName[0])) &&
			 !pDef->params->pchActivityName)
		{
			ErrorFilenamef(pDef->filename, "Mission specifies an activity reward table but does not specify an activity");
			bResult = false;
		}
		else if ((!pDef->params->ActivityReturnRewardTableName || !pDef->params->ActivityReturnRewardTableName[0]) &&
				 (!pDef->params->ActivitySuccessRewardTableName || !pDef->params->ActivitySuccessRewardTableName[0]) &&
				 pDef->params->pchActivityName)
		{
			ErrorFilenamef(pDef->filename, "Mission specifies an activity but does not specify an activity reward table");
			bResult = false;
		}

		// Validate numeric scales
		for(i=eaSize(&pDef->params->eaNumericScales)-1; i>=0; --i) {
			missiondef_ValidateNumericScale(pDef, pDef->params->eaNumericScales[i]);
		}

		// Validate drops
		for(i=eaSize(&pDef->params->missionDrops)-1; i>=0; --i)	{
			bResult &= missiondef_ValidateDrop(pDef, pDef->params->missionDrops[i]);
		}
	}


	// Validate all Open Mission rewards
	if (IsServer() && pDef->params) {
		RewardTable *pGoldTable = NULL, *pSilverTable = NULL, *pBronzeTable = NULL, *pDefaultTable = NULL;
		RewardTable *pFailureGoldTable = NULL, *pFailureSilverTable = NULL, *pFailureBronzeTable = NULL, *pFailureDefaultTable = NULL;

		// Validate that tables exist
		if (pDef->params->pchGoldRewardTable && !(pGoldTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchGoldRewardTable))){
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchGoldRewardTable);
			bResult = false;
		}
		if (pDef->params->pchSilverRewardTable && !(pSilverTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchSilverRewardTable))){
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchSilverRewardTable);
			bResult = false;
		}
		if (pDef->params->pchBronzeRewardTable && !(pBronzeTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchBronzeRewardTable))){
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchBronzeRewardTable);
			bResult = false;
		}
		if (pDef->params->pchDefaultRewardTable && !(pDefaultTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchDefaultRewardTable))){
			ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchDefaultRewardTable);
			bResult = false;
		}
		if(!pDef->bSuppressUnreliableOpenRewardErrors)
		{
			if (pGoldTable && (rewardTable_HasItemsWithType(pGoldTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pGoldTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Gold Reward Table '%s'", pDef->params->pchGoldRewardTable);
					bResult = false;
			}

			if (pSilverTable && (rewardTable_HasItemsWithType(pSilverTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pSilverTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Silver Reward Table '%s'", pDef->params->pchSilverRewardTable);
					bResult = false;
			}

			if (pBronzeTable && (rewardTable_HasItemsWithType(pBronzeTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pBronzeTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Bronze Reward Table '%s'", pDef->params->pchBronzeRewardTable);
					bResult = false;
			}

			if (pDefaultTable && (rewardTable_HasItemsWithType(pDefaultTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pDefaultTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Default Reward Table '%s'", pDef->params->pchDefaultRewardTable);
					bResult = false;
			}

			if (pDef->params->pchFailureGoldRewardTable && !(pFailureGoldTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchFailureGoldRewardTable))){
				ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchFailureGoldRewardTable);
				bResult = false;
			}
			if (pDef->params->pchFailureSilverRewardTable && !(pFailureSilverTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchFailureSilverRewardTable))){
				ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchFailureSilverRewardTable);
				bResult = false;
			}
			if (pDef->params->pchFailureBronzeRewardTable && !(pFailureBronzeTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchFailureBronzeRewardTable))){
				ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchFailureBronzeRewardTable);
				bResult = false;
			}
			if (pDef->params->pchFailureDefaultRewardTable && !(pFailureDefaultTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->pchFailureDefaultRewardTable))){
				ErrorFilenamef( pDef->filename, "Mission refers to non-existent reward table '%s'", pDef->params->pchFailureDefaultRewardTable);
				bResult = false;
			}

			if (pFailureGoldTable && (rewardTable_HasItemsWithType(pFailureGoldTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pFailureGoldTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Failure Gold Reward Table '%s'", pDef->params->pchFailureGoldRewardTable);
					bResult = false;
			}

			if (pFailureSilverTable && (rewardTable_HasItemsWithType(pFailureSilverTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pFailureSilverTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Failure Silver Reward Table '%s'", pDef->params->pchFailureSilverRewardTable);
					bResult = false;
			}

			if (pFailureBronzeTable && (rewardTable_HasItemsWithType(pFailureBronzeTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pFailureBronzeTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Failure Bronze Reward Table '%s'", pDef->params->pchFailureBronzeRewardTable);
					bResult = false;
			}

			if (pFailureDefaultTable && (rewardTable_HasItemsWithType(pFailureDefaultTable, kRewardPickupType_Clickable, RewardContextType_MissionReward)
				|| rewardTable_HasItemsWithType(pFailureDefaultTable, kRewardPickupType_Choose, RewardContextType_MissionReward))) {
					ErrorFilenamef( pDef->filename, "Mission has invalid item grants in Open Mission Failure Default Reward Table '%s'", pDef->params->pchFailureDefaultRewardTable);
					bResult = false;
			}
		}
	}

	if (pDef->bIsHandoff && pDef->meSuccessCond) {
		ErrorFilenamef(pDef->filename, "Mission %s or its parent is a Handoff mission but has a Success Condition!", pDef->name);
		bResult = false;
	}

	if (pDef->bIsHandoff && !pDef->needsReturn) {
		ErrorFilenamef(pDef->filename, "Mission %s is a Handoff mission but does not need to be returned to a Contact!", pDef->name);
		bResult = false;
	}

	if (!bIsChild && pDef->fRepeatCooldownHours != 0 && !pDef->repeatable) {
		ErrorFilenamef(pDef->filename, "Mission %s has a Repeat Cooldown but is not repeatable!", pDef->name);
		bResult = false;
	}

	if (!bIsChild && pDef->params && (pDef->params->NumericRewardScale || pDef->params->iScaleRewardOverTime) && !missiondef_HasDisplayName(pDef)) {
		ErrorFilenamef(pDef->filename, "Mission %s is an invisible Mission, but has a non-zero reward scale!", pDef->name);
		bResult = false;
	}

	if (!bIsChild && pDef->needsReturn && !missiondef_HasDisplayName(pDef)) {
		ErrorFilenamef(pDef->filename, "Mission %s is an invisible Mission, but still requires turn-in!  Invisible missions need 'NeedsReturn' set to FALSE so that they will resolve completely.", pDef->name);
		bResult = false;
	}

	// any mission with Events needs to have "Do Not Uncomplete" set, as a safety-net for transactional issues
	if (pDef && pDef->meSuccessCond && missiondef_ConditionHasEventCount(pDef->meSuccessCond) && !pDef->doNotUncomplete) {
		ErrorFilenamef(pDef->filename, "Mission %s may uncomplete.  Any event-based mission must have the 'Never Uncomplete' flag set to True.", pDef->name);
		bResult = false;
	}

	// Validate Interactable overrides
	for(i=eaSize(&pDef->ppInteractableOverrides)-1; i>=0; i--) {
		InteractableOverride* pOverride = pDef->ppInteractableOverrides[i];
		if ((!pOverride->pcInteractableName || !pOverride->pcInteractableName[0]) &&
			(!pOverride->pcTypeTagName || !pOverride->pcTypeTagName[0])) {
			ErrorFilenamef(pDef->filename, "Mission %s has an interactable override with no name or tag!", pDef->name);
			bResult = false;
		}
	}
	
	if (IsServer())
	{
		// Validate Special Dialog overrides
		for (i = 0; i < eaSize(&pDef->ppSpecialDialogOverrides); ++i) {
			char *estrCurrentDialogName = NULL;
			SpecialDialogOverride* pOverride = pDef->ppSpecialDialogOverrides[i];
			ContactDef* pContact = pOverride->pcContactName ? contact_DefFromName(pOverride->pcContactName) : NULL;

			if (!pContact) {
				ErrorFilenamef(pDef->filename, "Mission %s has a special dialog override with no valid contact!", pDef->name);
				bResult = false;
			}
			if (!pOverride->pSpecialDialog) {
				ErrorFilenamef(pDef->filename, "Mission %s has a special dialog override with no valid special dialog!", pDef->name);
				bResult = false;
			} else if (pContact) {
				bResult &= contact_ValidateSpecialDialogBlock(pContact, pOverride->pSpecialDialog, pDef->name, pDef->ppSpecialDialogOverrides, pDef->ppMissionOfferOverrides);
			}

			// Make sure the dialog name is unique across all dialogs
			for (j = i + 1; j < eaSize(&pDef->ppSpecialDialogOverrides); j++) {
				if (pDef->ppSpecialDialogOverrides[j] &&
					pDef->ppSpecialDialogOverrides[j]->pSpecialDialog &&
					pOverride->pSpecialDialog &&
					stricmp(pDef->ppSpecialDialogOverrides[j]->pSpecialDialog->name, pOverride->pSpecialDialog->name) == 0)
				{
					ErrorFilenamef(pDef->filename, "Mission has more than one special dialog named '%s'", pOverride->pSpecialDialog->name);
					bResult = false;
					break;
				}
			}

			if (pOverride->pSpecialDialog && pOverride->pSpecialDialog->name && pOverride->pSpecialDialog->name[0])	{
				estrPrintf(&estrCurrentDialogName, "%s/%s", pDef->name, pOverride->pSpecialDialog->name);
			}

			if (estrCurrentDialogName) {
				// Also compare with the mission offer names
				FOR_EACH_IN_EARRAY_FORWARDS(pDef->ppMissionOfferOverrides, MissionOfferOverride, pMissionOfferOverride) {
					if (pMissionOfferOverride && 
						pMissionOfferOverride->pMissionOffer &&
						pMissionOfferOverride->pMissionOffer->pchSpecialDialogName &&
						stricmp(pMissionOfferOverride->pMissionOffer->pchSpecialDialogName, estrCurrentDialogName) == 0) 
					{
						ErrorFilenamef(pDef->filename, "Mission has both a special dialog and a mission offer named '%s'", pOverride->pSpecialDialog->name);
						bResult = false;
						break;
					}
				} FOR_EACH_END
			}

			estrDestroy(&estrCurrentDialogName);
		}

		// Validate Mission Offer overrides
		for (i = 0; i < eaSize(&pDef->ppMissionOfferOverrides); ++i) {
			MissionOfferOverride* pOverride = pDef->ppMissionOfferOverrides[i];
			ContactDef* pContact = pOverride->pcContactName ? contact_DefFromName(pOverride->pcContactName) : NULL;

			if (!pContact) {
				ErrorFilenamef(pDef->filename, "Mission %s has a mission offer override with no valid contact!", pDef->name);
				bResult = false;
			}
			if (!pOverride->pMissionOffer) {
				ErrorFilenamef(pDef->filename, "Mission %s has a mission offer override with no valid mission offer!", pDef->name);
				bResult = false;
			} else if (pContact) {
				bResult &= contact_ValidateMissionOffer(pContact, pOverride->pMissionOffer, pDef->name, pDef->ppSpecialDialogOverrides, pDef->ppMissionOfferOverrides);
			}

			if (pOverride->pMissionOffer) {
				for (j = eaSize(&pOverride->pMissionOffer->eaRequiredAllegiances)-1; j >= 0; j--) {
					AllegianceRef* pAllegianceRef = pOverride->pMissionOffer->eaRequiredAllegiances[j];
					if (!IS_HANDLE_ACTIVE(pAllegianceRef->hDef)) {
						ErrorFilenamef(pDef->filename, "Mission %s has an invalid required allegiance for contact mission offer %s!", pDef->name, pOverride->pcContactName);
						bResult = false;
					} else if (!GET_REF(pAllegianceRef->hDef)) {
						ErrorFilenamef(pDef->filename, "Mission %s specifies a non-existent required allegiance %s for contact mission offer %s!", 
							pDef->name, REF_STRING_FROM_HANDLE(pAllegianceRef->hDef), pOverride->pcContactName);
						bResult = false;
					}
				}
			}
			if (pOverride->pMissionOffer && pOverride->pMissionOffer->pchSpecialDialogName) {
				for (j = i + 1; j < eaSize(&pDef->ppMissionOfferOverrides); j++) {
					if (pDef->ppMissionOfferOverrides[j]->pMissionOffer &&
						pDef->ppMissionOfferOverrides[j]->pMissionOffer->pchSpecialDialogName &&
						stricmp(pDef->ppMissionOfferOverrides[j]->pMissionOffer->pchSpecialDialogName, pOverride->pMissionOffer->pchSpecialDialogName) == 0)
					{
						ErrorFilenamef( pDef->filename, "Mission has more than one mission offer named '%s'", pOverride->pMissionOffer->pchSpecialDialogName);
						bResult = false;
						break;
					}
				}
			}
		}

		// Validate ImageMenuItem overrides
		for (i = 0; i < eaSize(&pDef->ppImageMenuItemOverrides); ++i) {
			ImageMenuItemOverride* pOverride = pDef->ppImageMenuItemOverrides[i];
			ContactDef* pContact = pOverride->pcContactName ? contact_DefFromName(pOverride->pcContactName) : NULL;

			if (!pContact) {
				ErrorFilenamef(pDef->filename, "Mission %s has a ImageMenuItem override with no valid contact!", pDef->name);
				bResult = false;
			}

			if (pContact) {
				bResult &= contact_ValidateImageMenuItem(pContact, pOverride->pImageMenuItem, pDef->name);
			}
		}
	}

	// Validate MissionRequests
	if (pDef && eaSize(&pDef->eaRequests) && missiondef_GetType(pRootDef) != MissionType_NemesisArc && missiondef_GetType(pRootDef) != MissionType_NemesisSubArc) {
		ErrorFilenamef(pDef->filename, "Mission %s: MissionRequests are currently only allowed on NemesisArc missions", pDef->pchRefString);
		bResult = false;
	}

	// Validate each request
	if (IsServer()) {
		for (i = eaSize(&pDef->eaRequests)-1; i>=0; --i){
			bResult &= missiondef_ValidateRequest(pDef, pDef->eaRequests[i]);
		}
	}

	// Validate Requested missions
	if (pDef->eRequestGrantType) {
		bool bFoundDrop = false;

		if (bIsChild){
			ErrorFilenamef(pDef->filename, "Mission %s: Child Missions can't be requested", pDef->pchRefString);
			bResult = false;
		}

		if (!pDef->repeatable) {
			ErrorFilenamef(pDef->filename, "Requested missions must be repeatable");
			bResult = false;
		}

		// Can't have a Requires expression right now for tech reasons. This might change
		if (pDef->missionReqs) {
			ErrorFilenamef(pDef->filename, "Requested missions can't have a Requires expression");
			bResult = false;
		}

		// See if this mission has a pre-mission MissionDrop
		for (i = 0; pDef->params && i < eaSize(&pDef->params->missionDrops); i++) {
			if (pDef->params->missionDrops[i]->whenType == MissionDropWhenType_PreMission) {
				bFoundDrop = true;
				break;
			}
		}

		switch (pDef->eRequestGrantType){
			xcase MissionRequestGrantType_Contact:
				// This should be granted from a ContactDef somewhere - not sure how to validate that
				if (bFoundDrop) {
					ErrorFilenamef(pDef->filename, "Requested missions with a grant type of 'Contact' has a PreMission MissionDrop, which is probably a mistake.  (Did you mean to use a request type of 'Drop'?)");
					bResult = false;
				}
			xcase MissionRequestGrantType_Direct:
				// This is just granted immediately.  No validation is required, really
				if (bFoundDrop) {
					ErrorFilenamef(pDef->filename, "Requested missions with a grant type of 'Direct' has a PreMission MissionDrop, which is probably a mistake.  (Did you mean to use a request type of 'Drop'?)");
					bResult = false;
				}
			xcase MissionRequestGrantType_Drop:
				if (!bFoundDrop) {
					ErrorFilenamef(pDef->filename, "Requested missions with a grant type of 'Drop' must have a PreMission MissionDrop!");
					bResult = false;
				}
			xdefault:
				ErrorFilenamef(pDef->filename, "Programmer error: Missing validation for a MissionRequestGrantType");
				bResult = false;
		}
	}

	// Repeatable Missions
	if ((pDef->fRepeatCooldownHours > 0 || pDef->fRepeatCooldownHoursFromStart > 0) && !pDef->repeatable) {
		ErrorFilenamef(pDef->filename, "Mission has a repeatable cooldown but is not a repeatable mission.");
		bResult = false;
	}
	if (pDef->fRepeatCooldownHours < 0) {
		ErrorFilenamef(pDef->filename, "Repeatable cooldown from last completed time is less than 0.  All repeatable cooldowns must be greater than or equal to 0.");
		bResult = false;
	}
	if(pDef->fRepeatCooldownHoursFromStart < 0) {
		ErrorFilenamef(pDef->filename, "Repeatable cooldown from last started time is less than 0.  All repeatable cooldowns must be greater than or equal to 0.");
		bResult = false;
	}

	// TODO: Validate "missionTemplate" data

	// SDANGELO: Disabled until designers fix current data
	//if (!isChild && (!def->pchObjectiveMap || !def->pchObjectiveMap[0]) && (!def->pchCategory || !def->pchCategory[0])) {
	//	ErrorFilenamef(def->filename, "Mission %s does not have an objective map or a category defined.", def->name);
	//	result = false;
	//}

	// For now, at least verify that categories that have been entered are valid
	if (!bIsChild && IS_HANDLE_ACTIVE(pDef->hCategory) && !GET_REF(pDef->hCategory)){
		ErrorFilenamef(pDef->filename, "Mission %s has invalid category '%s'.", pDef->name, REF_STRING_FROM_HANDLE(pDef->hCategory));
		bResult = false;
	}

	// TODO: Find a way to validate map names
	//	pchObjectiveMap
	//	pchReturnMap
	//	autoGrantOnMap
	//	waypoint (MissionWaypoint)
	//    mapName

	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaWaypoints, MissionWaypoint, pWaypoint) {
		if ((pWaypoint->type != MissionWaypointType_None) && !pWaypoint->name) {
			ErrorFilenamef(pDef->filename, "Mission %s has a waypoint type set but with no name of what to waypoint!", pDef->name);
		}
		if ((pWaypoint->type != MissionWaypointType_None) && !pWaypoint->mapName && !pWaypoint->bAnyMap) {
			ErrorFilenamef(pDef->filename, "Mission %s has a waypoint type set but with no map name!", pDef->name);
		}
		if ((pWaypoint->type != MissionWaypointType_None) && pWaypoint->mapName && pWaypoint->bAnyMap) {
			ErrorFilenamef(pDef->filename, "Mission %s has a waypoint with a map name, but also has the 'Any Map' option set!", pDef->name);
		}
	} FOR_EACH_END

	// Validate variable defs
	for (i = 0; i < eaSize(&pDef->eaVariableDefs); i++) {
		WorldVariableDef *pVariableDef = pDef->eaVariableDefs[i];
		if (!pVariableDef) {
			continue;
		}
		if (!pVariableDef->pcName || !pVariableDef->pcName[0]) {
			ErrorFilenamef(pDef->filename, "Mission %s has an unnamed variable.", pDef->name);
			bResult = false;

		} else if (pVariableDef->eDefaultType == WVARDEF_MAP_VARIABLE && missiondef_GetType(pDef) != MissionType_OpenMission) {
			ErrorFilenamef(pDef->filename, "Mission %s's variable %s cannot be initialized from a map variable unless the mission is an open mission.", pDef->name, pVariableDef->pcName);
			bResult = false;

		} else if (pVariableDef->eDefaultType == WVARDEF_MISSION_VARIABLE ||
				   pVariableDef->eDefaultType == WVARDEF_EXPRESSION) {
			ErrorFilenamef(pDef->filename, "Mission %s's variable %s cannot be initialized from another mission variable or expression.", pDef->name, pVariableDef->pcName);
			bResult = false;

		} else if (IsServer()) {
			bResult &= worldVariableValidateDef(pVariableDef, pVariableDef, pDef->name, pDef->filename);
		}
	}

	// Validate restrictions due to DisableCompletionTracking flag
	if (pDef->bDisableCompletionTracking) {
		if (pDef->repeatable) {
			ErrorFilenamef(pDef->filename, "Mission %s's completion tracking is disabled, and it is flagged as repeatable.  This is redundant; if a mission's completion tracking is disabled, then it is always repeatable.", pDef->name);
			bResult = false;
		}
		if (pDef->fRepeatCooldownHours != 0 || pDef->fRepeatCooldownHoursFromStart != 0) {
			ErrorFilenamef(pDef->filename, "Mission %s's completion tracking is disabled, but it has a cooldown is specified.  Since completion tracking is disabled, there is no way to track how much time has passed since the mission was completed.", pDef->name);
			bResult = false;
		}
	}

	// Check required allegiances
	for (i = eaSize(&pDef->eaRequiredAllegiances)-1; i >= 0; i--) {
		AllegianceRef* pAllegianceRef = pDef->eaRequiredAllegiances[i];
		if (!IS_HANDLE_ACTIVE(pAllegianceRef->hDef)) {
			ErrorFilenamef(pDef->filename, "Mission has an empty required allegiance");
			bResult = false;
		}
		else if (!GET_REF(pAllegianceRef->hDef)) {
			ErrorFilenamef(pDef->filename, "Mission references a non-existent required allegiance %s", REF_STRING_FROM_HANDLE(pAllegianceRef->hDef));
			bResult = false;
		}
	}

	// Validate warp data
	if (pDef->pWarpToMissionDoor) {
		if (!pDef->pWarpToMissionDoor->pchMapName) {
			ErrorFilenamef(pDef->filename, "Mission has warp properties but does not specify a map");
			bResult = false;
		}
		if (IS_HANDLE_ACTIVE(pDef->pWarpToMissionDoor->hTransSequence) && !GET_REF(pDef->pWarpToMissionDoor->hTransSequence)) {
			ErrorFilenamef(pDef->filename, "Mission references a non-existent transition sequence %s", REF_STRING_FROM_HANDLE(pDef->pWarpToMissionDoor->hTransSequence));
			bResult = false;
		}
	}

	// Recurse
	for(i=eaSize(&pDef->subMissions)-1; i>=0; --i) {
		bResult &= missiondef_Validate(pDef->subMissions[i], pRootDef, true);
	}

	return bResult;
}


// ----------------------------------------------------------------------------------
// Mission Def Fixup Logic
// ----------------------------------------------------------------------------------

static void missiondef_FixupConvertCondition(MissionDef *pDef, MissionEditCond *pMeCond, MissionDef *pParentDef, bool bSuccess)
{
	MissionDef *pRootDef = pParentDef ? pParentDef : pDef;

	if(pMeCond)
	{
		int i;
		const int n = eaSize(&pMeCond->subConds);
		for(i = 0; i < n; i++)
		{
			MissionEditCond *pSubCond = pMeCond->subConds[i];
			missiondef_FixupConvertCondition(pDef, pSubCond, pParentDef, bSuccess);
		}

		if(pMeCond->type == MissionCondType_Expression && pMeCond->valStr)
			pMeCond->expression = exprCreateFromString(pMeCond->valStr, pDef->filename);
		else if (pMeCond->type == MissionCondType_Objective)
		{
			char* eStr;
			char* successStr = bSuccess ? "Succeeded" : "Failed";
			if (!pMeCond->valStr) {
				ErrorFilenameGroupf(pDef->filename, "Design", 2, "Objective condition does not have a mission name");
			} else {
				MissionDef *pSubMission = missiondef_ChildDefFromName(pRootDef, pMeCond->valStr);

				// Temporary fix: also check submissions of this mission to be compatible with old-style missions
				if (!pSubMission && pRootDef != pDef) {
					pSubMission = missiondef_ChildDefFromName(pDef, pMeCond->valStr);
				}

				if (!pSubMission) {
					ErrorFilenameGroupf(pDef->filename, "Design", 2, "Objective: %s, does not exist", pMeCond->valStr);
				} else {
					pSubMission->isRequired = 1;
					estrStackCreate(&eStr);
					if (missiondef_GetType(pDef) == MissionType_OpenMission) {
						estrPrintf(&eStr, "(OpenMissionState%s(\"%s\"))", successStr, pSubMission->pchRefString);
					} else {
						estrPrintf(&eStr, "(MissionState%s(\"%s\"))", successStr, pSubMission->pchRefString);
					}
					pMeCond->expression = exprCreateFromString(eStr, pDef->filename);
					estrDestroy(&eStr);
				}
			}
		}
	}
}

static bool missiondef_FixupGenerateCountConditions(MissionDef *pDef, MissionEditCond *pRootCond)
{
	int i, n = eaSize(&pRootCond->subConds);

	if (pRootCond->type != MissionCondType_Expression) {
		pDef->showCount = MDEShowCount_Show_Count;
	}

	// See if the root condition can be split on >=
	if (pRootCond->type == MissionCondType_Expression && pRootCond->showCount != MDEShowCount_Normal) {
		char *estrCountCond = estrStackCreateFromStr(pRootCond->valStr);
		char *pcTargetCond = 0;

		if (estrCountCond) {
			int numParens = 0;
			// Split string on ">="
			pcTargetCond = estrCountCond;
			while (*pcTargetCond != '\0') {
				if (*pcTargetCond == '(') {
					numParens++;
				} else if (*pcTargetCond == ')' && numParens>0) {
					numParens--;
				} else if (*pcTargetCond == '>' && *(pcTargetCond+1) == '=' && numParens == 0) {
					*(pcTargetCond) = '\0';
					pcTargetCond+=2;
					break;
				}
				pcTargetCond++;
			}

			// If the string was successfully split, create expressions for the left and right side
			if (*pcTargetCond != '\0') {
				StructDestroySafe(parse_Expression, &pDef->successCount);
				StructDestroySafe(parse_Expression, &pDef->successTarget);
				pDef->successCount = exprCreateFromString(estrCountCond, pDef->filename);
				pDef->successTarget = exprCreateFromString(pcTargetCond, pDef->filename);
				pDef->showCount = pRootCond->showCount;
				estrDestroy(&estrCountCond);
				return true;
			}
		}
		estrDestroy(&estrCountCond);
	}

	// Didn't work; try all sub-conditions
	for (i = 0; i < n; i++) {
		if (missiondef_FixupGenerateCountConditions(pDef, pRootCond->subConds[i])) {
			return true;
		}
	}

	return false;
}


static void missiondef_FixupEditorFormatConversion(MissionDef *pDef, MissionDef *pParentDef)
{
	StructDestroySafe(parse_Expression, &pDef->successCount);
	StructDestroySafe(parse_Expression, &pDef->successTarget);
	
	if(pDef->meFailureCond)
	{
		missiondef_FixupConvertCondition(pDef, pDef->meFailureCond, pParentDef, false);
		missiondef_FixupGenerateCountConditions(pDef, pDef->meFailureCond);
	}

	if(pDef->meSuccessCond)
	{
		missiondef_FixupConvertCondition(pDef, pDef->meSuccessCond, pParentDef, true);
		missiondef_FixupGenerateCountConditions(pDef, pDef->meSuccessCond);
	}
	
	if(pDef->meResetCond)
	{
		missiondef_FixupConvertCondition(pDef, pDef->meResetCond, pParentDef, true);
		missiondef_FixupGenerateCountConditions(pDef, pDef->meResetCond);
	}
}


static void missiondef_FixupGenerateVarExpressionsForGroup(ExprContext *pContext, TemplateVariableGroup *pGroup)
{
	int i, n = eaSize(&pGroup->variables);
	for (i = 0; i < n; i++) {
		if (pGroup->variables[i]->varValueExpression) {
			exprContextSetStringVar(pContext, "varName", pGroup->variables[i]->varName);
			if (pGroup->variables[i]->varDependency) {
				exprContextSetStringVar(pContext, "varDependency", pGroup->variables[i]->varDependency);
			}

			exprGenerate(pGroup->variables[i]->varValueExpression, pContext);

			exprContextRemoveVar(pContext, "varName");
			if (pGroup->variables[i]->varDependency) {
				exprContextRemoveVar(pContext, "varDependency");
			}
		}
	}

	n = eaSize(&pGroup->subGroups);
	for (i = 0; i < n; i++) {
		if (pGroup->subGroups[i]) {
			missiondef_FixupGenerateVarExpressionsForGroup(pContext, pGroup->subGroups[i]);
		}
	}
}


static void missiondef_FixupGenerateVarExpressions(MissionDef *pMissionDef, ExprContext *pContext)
{
	exprContextSetPointerVarPooled(pContext, g_MissionDefVarName, pMissionDef, parse_MissionDef, 0, 0);
	if (pMissionDef->missionTemplate) {
		missiondef_FixupGenerateVarExpressionsForGroup(pContext, pMissionDef->missionTemplate->rootVarGroup);
	}
}


// fixup the Missions reward scale
void missiondef_FixupRewardScale(MissionDef *pMissionDef)
{
	if (!IS_HANDLE_ACTIVE(pMissionDef->parentDef)) {
		if (!pMissionDef->params) {
			pMissionDef->params = StructCreate(parse_MissionDefParams);
		}
	} else {
		//sub-mission,  always set scale to 0
		if (!pMissionDef->params) {
			pMissionDef->params = StructCreate(parse_MissionDefParams);
		}

		pMissionDef->params->NumericRewardScale = 0;
		pMissionDef->params->NumericRewardScaleIneligible = 0;
		pMissionDef->params->NumericRewardScaleAlreadyCompleted = 0;
		pMissionDef->params->iScaleRewardOverTime = 0;

		eaDestroyStruct(&pMissionDef->params->eaNumericScales, parse_MissionNumericScale);
	}
}

void missiondef_ExprGenerateCondRecurse(MissionEditCond *pCond, ExprContext *pContext)
{
	if(pCond)
	{
		int i;
		const int n = eaSize(&pCond->subConds);
		for(i = 0; i < n; i++)
			missiondef_ExprGenerateCondRecurse(pCond->subConds[i], pContext);

		if(pCond->expression)
			exprGenerate(pCond->expression, pContext);
	}
}

// Before calling this, make sure all MissionDefs (including children) have RefStrings
int missiondef_PostProcessFixup(MissionDef *pDef, MissionDef *pParentDef)
{
	static ExprContext *pNormalMissionExprContext = NULL;
	static ExprContext *pNormalMissionRequiresExprContext = NULL;
	static ExprContext *pOpenMissionExprContext = NULL;
	static ExprContext *pOpenMissionRequiresExprContext = NULL;
	static ExprContext *pTemplateExprContext = NULL;

	ExprContext *pContext = NULL;
	ExprContext *pRequiresContext = NULL;
	int i, n;

	if (!pNormalMissionExprContext) {
		pNormalMissionExprContext = exprContextCreate();
		exprContextSetFuncTable(pNormalMissionExprContext, missiondef_CreateExprFuncTable());
		exprContextSetPointerVarPooled(pNormalMissionExprContext, g_PlayerVarName, NULL, parse_Entity, true, true);
		exprContextSetAllowRuntimeSelfPtr(pNormalMissionExprContext);
		exprContextSetAllowRuntimePartition(pNormalMissionExprContext);
	}
	if (!pNormalMissionRequiresExprContext) {
		pNormalMissionRequiresExprContext = exprContextCreate();
		exprContextSetFuncTable(pNormalMissionRequiresExprContext, missiondef_CreateRequiresExprFuncTable());
		exprContextSetPointerVarPooled(pNormalMissionRequiresExprContext, g_PlayerVarName, NULL, parse_Entity, true, true);
		exprContextSetAllowRuntimeSelfPtr(pNormalMissionRequiresExprContext);
		exprContextSetAllowRuntimePartition(pNormalMissionRequiresExprContext);
	}
	if (!pOpenMissionExprContext) {
		pOpenMissionExprContext = exprContextCreate();
		exprContextSetFuncTable(pOpenMissionExprContext, missiondef_CreateOpenMissionExprFuncTable());
		exprContextSetPointerVarPooled(pOpenMissionExprContext, g_PlayerVarName, NULL, parse_Entity, true, true);
		exprContextSetAllowRuntimePartition(pOpenMissionExprContext);
		// Does not allow self pointer
	}
	if (!pOpenMissionRequiresExprContext) {
		pOpenMissionRequiresExprContext = exprContextCreate();
		exprContextSetFuncTable(pOpenMissionRequiresExprContext, missiondef_CreateOpenMissionRequiresExprFuncTable());
		exprContextSetPointerVarPooled(pOpenMissionRequiresExprContext, g_PlayerVarName, NULL, parse_Entity, true, true);
		exprContextSetAllowRuntimeSelfPtr(pOpenMissionRequiresExprContext);
		exprContextSetAllowRuntimePartition(pOpenMissionRequiresExprContext);
	}
	if (!pTemplateExprContext) {
		pTemplateExprContext = exprContextCreate();
		exprContextSetFuncTable(pTemplateExprContext, missiontemplate_CreateTemplateVarExprFuncTable());
		// No Self or Partition
	}

	// Choose which Expression Context to use based on the mission's type
	if (missiondef_GetType(pDef) == MissionType_OpenMission){
		pContext = pOpenMissionExprContext;
		pRequiresContext = pOpenMissionRequiresExprContext;
	} else {
		pContext = pNormalMissionExprContext;
		pRequiresContext = pNormalMissionRequiresExprContext;
	}

	// If the mission uses a template, generate a real mission def from the template
	// This doesn't need to be recursive, since all mission defs will be post-processed
	// Do this before touching any submissions, because the template may add submissions
	missiontemplate_GenerateDefFromTemplate(pDef, false);

	// Post process all sub-missions.
	n = eaSize(&pDef->subMissions);
	for (i = 0; i < n; ++i) {
		missiondef_PostProcessFixup(pDef->subMissions[i], pDef);
	}

	// Before we evaluate the missions, we need to run post processing to create
	// the expressions from the intermediate data stored in the def
	missiondef_FixupEditorFormatConversion(pDef, pParentDef);

	if (IsServer()) {
		// Generate the expression and then evaluate them all using the verification functions
		exprContextSetPointerVarPooled(pContext, g_MissionDefVarName, pDef, parse_MissionDef, false, true);

		missiondef_ExprGenerateCondRecurse(pDef->meSuccessCond, pContext);
		missiondef_ExprGenerateCondRecurse(pDef->meFailureCond, pContext);
		missiondef_ExprGenerateCondRecurse(pDef->meResetCond, pContext);

		if (pDef->missionReqs) {
			exprGenerate(pDef->missionReqs, pRequiresContext);
		}
		if (pDef->pMapRequirements) {
			exprGenerate(pDef->pMapRequirements, pContext);
		}
		if (pDef->successCount) {
			exprGenerate(pDef->successCount, pContext);
		}
		if (pDef->successTarget) {
			exprGenerate(pDef->successTarget, pContext);
		}
		if (pDef->pDiscoverCond) {
			exprGenerate(pDef->pDiscoverCond, pContext);
		}
		if (pDef->pMapSuccess) {
			exprGenerate(pDef->pMapSuccess, pContext);
		}
		if (pDef->pMapFailure) {
			exprGenerate(pDef->pMapFailure, pContext);
		}

		missiondef_FixupGenerateVarExpressions(pDef, pTemplateExprContext);

		gameaction_GenerateActions(&pDef->ppOnStartActions, pParentDef, pDef->filename);
		gameaction_GenerateActions(&pDef->ppSuccessActions, pParentDef, pDef->filename);
		gameaction_GenerateActions(&pDef->ppFailureActions, pParentDef, pDef->filename);
		gameaction_GenerateActions(&pDef->ppOnReturnActions, pParentDef, pDef->filename);

		for (i = 0; i < eaSize(&pDef->eaVariableDefs); i++) {
			WorldVariableDef *pVariableDef = pDef->eaVariableDefs[i];
			worldVariableDefGenerateExpressions(pVariableDef, pDef->name, pDef->filename);
		}

		for (i=0; i < eaSize(&pDef->ppSpecialDialogOverrides); i++) {
			SpecialDialogBlock *pDialog = pDef->ppSpecialDialogOverrides[i]->pSpecialDialog;
			if (pDialog) {
				contact_SpecialDialogPostProcess(pDialog, pDef->filename);
			}
		}

		for (i=0; i < eaSize(&pDef->ppImageMenuItemOverrides); i++) {
			ContactImageMenuItem* pItem = pDef->ppImageMenuItemOverrides[i]->pImageMenuItem;
			if (pItem) {
				contact_ImageMenuItemPostProcess(pItem, pDef->filename);
			}
		}
	}

	// After all post processing on the mission, do the error checking
	if (!pDef->name) {
		ErrorFilenameGroupf(pDef->filename, "Design", 2, "Error: MissionDef does not have a name");
	}

	return 1;
}


static void missiondef_LoadDisplayMessagesToList(MissionDef *pMissionDef)
{
	int i, n;

	n = eaSize(&pMissionDef->subMissions);
	for(i=0; i<n; i++) {
		missiondef_LoadDisplayMessagesToList(pMissionDef->subMissions[i]);
	}
}

// ----------------------------------------------------------------------------------
// Mission Def Dictionary
// ----------------------------------------------------------------------------------

static void missiondef_DictionaryChangeCB(enumResourceEventType eType, const char *pcDictName, const char *pcRefData, Referent pReferent, void *pUserData)
{
	MissionDef *pDef = (MissionDef*)pReferent;
	int i, n;

	if (!isProductionMode() || isProductionEditMode()) {
		// Updates Missions Index based on dictionary change
		if (eType == RESEVENT_RESOURCE_ADDED ||	eType == RESEVENT_RESOURCE_MODIFIED) {
			resUpdateInfo(ALL_MISSIONS_INDEX, pDef->pchRefString, parse_MissionDef, pReferent, ".name", ".scope", NULL, ".comments", NULL, false, false);
			
			// Iterate submissions
			n = eaSize(&pDef->subMissions);
			for (i = 0; i < n; i++) {
				resUpdateInfo(ALL_MISSIONS_INDEX, pDef->subMissions[i]->pchRefString, parse_MissionDef, pDef->subMissions[i], ".name", ".scope", NULL, ".comments", NULL, false, false);
			}
		}

		if (eType == RESEVENT_RESOURCE_REMOVED)	{
			resUpdateInfo(ALL_MISSIONS_INDEX, pDef->pchRefString, parse_MissionDef, NULL, ".name", ".scope", NULL, ".comments", NULL, false, false);

			// Iterate submissions
			n = eaSize(&pDef->subMissions);
			for (i = 0; i < n; i++) {
				resUpdateInfo(ALL_MISSIONS_INDEX, pDef->subMissions[i]->pchRefString, parse_MissionDef, NULL, ".name", ".scope", NULL, ".comments", NULL, false, false);
			}
		}
	}

	// On the Server it also updates the mission tracking
	#ifdef GAMESERVER
	if (eType == RESEVENT_RESOURCE_ADDED ||	eType == RESEVENT_RESOURCE_MODIFIED) {
		missionevent_StartTrackingForName(pcRefData);
	}

	if (eType == RESEVENT_RESOURCE_REMOVED || eType == RESEVENT_RESOURCE_PRE_MODIFIED) {
		missionevent_StopTrackingForName(pcRefData);
	}
	#endif

	// On the gameserver, maintain a list of pre-mission Mission Drops
	#ifdef GAMESERVER
	if (eType == RESEVENT_RESOURCE_ADDED ||	eType == RESEVENT_RESOURCE_MODIFIED) {
		if (!gConf.bAllowOldEncounterData || g_EncounterMasterLayer) {
			missiondrop_RegisterGlobalMissionDrops(pDef);
		}
	}

	if (eType == RESEVENT_RESOURCE_REMOVED || eType == RESEVENT_RESOURCE_PRE_MODIFIED) {
		missiondrop_UnregisterGlobalMissionDrops(pDef);
	}
	#endif

	// On the gameserver, handle the mission resource changes. Handling of missions without namespaces are different.
	#ifdef GAMESERVER
	if (pDef)
	{
		static ContactDef **eaContactsToUpdate = NULL;
		bool bResourceHasNamespace = resExtractNameSpace_s(pDef->name, NULL, 0, NULL, 0);

		if (eaContactsToUpdate == NULL && (eType == RESEVENT_RESOURCE_PRE_MODIFIED || eType == RESEVENT_RESOURCE_MODIFIED))
		{
			eaIndexedEnable(&eaContactsToUpdate, parse_ContactDef);
		}

		if (eType == RESEVENT_RESOURCE_REMOVED || eType == RESEVENT_RESOURCE_PRE_MODIFIED)
		{
			missiondef_RemoveOverrides(pDef);
			if (!bResourceHasNamespace)
			{
				contact_MissionRemovedOrPreModifiedFixup(pDef, eType == RESEVENT_RESOURCE_REMOVED ? NULL : &eaContactsToUpdate);
			}
		}
		else if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
		{
			eaPushUnique(&g_eaMissionsWaitingForOverrideProcessing, pDef->name);
			if (!bResourceHasNamespace)
			{
				contact_MissionAddedOrPostModifiedFixup(pDef, eType == RESEVENT_RESOURCE_ADDED ? NULL : &eaContactsToUpdate);
			}
		}
	}
	#endif
}

static void missiondef_UpdateGameProgressionNodes(MissionDef* pDef)
{
	GameProgressionNodeDef* pNodeDef;
	ResourceIterator ResIterator;
	int i;

	eaDestroy(&pDef->ppchProgressionNodes);

	resInitIterator(g_hGameProgressionNodeDictionary, &ResIterator);
	while (resIteratorGetNext(&ResIterator, NULL, &pNodeDef))
	{
		if (pNodeDef->pMissionGroupInfo)
		{
			for (i = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions)-1; i >= 0; i--)
			{
				GameProgressionMission* pProgMission = pNodeDef->pMissionGroupInfo->eaMissions[i];
				if (stricmp(pDef->name, pProgMission->pchMissionName)==0)
				{
					eaPush(&pDef->ppchProgressionNodes, allocAddString(pNodeDef->pchName));
					break;
				}
			}
		}
	}
	resFreeIterator(&ResIterator);
}

static int missiondef_ValidateCB(enumResourceValidateType eType, const char *pcDictName, const char *pcResourceName, MissionDef *pMissionDef, U32 userID)
{
	switch(eType)
	{
		// Post text reading: do fixup that should happen before binning.  Generate expressions, do most post-processing here
		//also happens after receiving missions from the resource dB, for basically the same reasons
		// This specifically falls through so that resource db receives are treated similarly to post text reads
		xcase RESVALIDATE_POST_TEXT_READING:
			
		acase RESVALIDATE_POST_RESDB_RECEIVE:
			missiondef_FixupRewardScale(pMissionDef);	// Old data fixup; should be safe to remove soon
			missiondef_CreateRefStringsRecursive(pMissionDef, NULL);
			missiondef_PostProcessFixup(pMissionDef, NULL);
			missiondef_LoadDisplayMessagesToList(pMissionDef);
			return VALIDATE_HANDLED;

		// Post binning: gets run each time mission is read from bin.  Populate NO_AST fields here
		xcase RESVALIDATE_POST_BINNING:
			missiondef_UpdateGameProgressionNodes(pMissionDef);

		// Final location: after moving to shared memory.  Fix up pointers here
		xcase RESVALIDATE_FINAL_LOCATION:
			
		xcase RESVALIDATE_CHECK_REFERENCES:
			missiondef_Validate(pMissionDef, pMissionDef, false);
#ifdef GAMESERVER
			missiondef_UpdateActivityMissionDependencies(pMissionDef);
#endif
			if (!gIsValidatingAllReferences) {
				// If not validating all dictionaries, then we need to force extra validation
				gValidateContactsOnNextTick = true;
			}
			return VALIDATE_HANDLED;

		// Fix filename: called during saving.
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename(&pMissionDef->filename, pMissionDef->scope && resIsInDirectory(pMissionDef->scope, "maps/") ? NULL : "defs/missions", pMissionDef->scope, pMissionDef->name, "mission");
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


int missiondef_LoadMissionDefs(void)
{
	static int bLoadedOnce = false;

	DictionaryEArrayStruct *dictionaryStruct = NULL;

	if (IsServer()) {
		if (bLoadedOnce) {
			return 1;
		}

		resLoadResourcesFromDisk(g_MissionDictionary, "defs/missions;maps", ".mission", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | RESOURCELOAD_USERDATA);
		bLoadedOnce = true;

		// Shrink memory pool after loading into shared memory
		mpCompactPool(memPoolMission);
	}
	return 1;
}

AUTO_RUN;
void missiondef_RegisterMissionDictionary(void)
{
	g_MissionDictionary = RefSystem_RegisterSelfDefiningDictionary("Mission", false, parse_MissionDef, true, true, NULL);

	resDictManageValidation(g_MissionDictionary, missiondef_ValidateCB);
	resRegisterIndexOnlyDictionary(ALL_MISSIONS_INDEX, RESCATEGORY_INDEX);

	if (IsServer()) {
		resDictRegisterEventCallback(g_MissionDictionary, missiondef_DictionaryChangeCB, NULL);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_MissionDictionary, ".name", ".scope", NULL, ".comments", NULL);
		}

		resDictProvideMissingResources(g_MissionDictionary);
		resDictProvideMissingResources(ALL_MISSIONS_INDEX);

		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_MissionDictionary);
	} else {
		resDictRequestMissingResources(g_MissionDictionary, 8, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(ALL_MISSIONS_INDEX, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
}


// ----------------------------------------------------------------------------------
// Mission Category dictionary
// ----------------------------------------------------------------------------------

static int missiondef_ValidateCategoryCB(enumResourceValidateType eType, const char *pcDictName, const char *pcResourceName, MissionCategory *pCategory, U32 userID)
{
	switch(eType)
	{
		// Post text reading: do fixup that should happen before binning.  Generate expressions, do most post-processing here
		xcase RESVALIDATE_POST_TEXT_READING:

		// Post binning: gets run each time mission is read from bin.  Populate NO_AST fields here
		xcase RESVALIDATE_POST_BINNING:

		// Final location: after moving to shared memory.  Fix up pointers here
		xcase RESVALIDATE_FINAL_LOCATION:

		// Fix filename: called during saving.
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename(&pCategory->filename, "defs/missioncategories", pCategory->scope, pCategory->name, "missioncategory");
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


int missiondef_LoadCategories(void)
{
	static int bLoadedOnce = false;

	if (bLoadedOnce) {
		return 1;
	}

	resLoadResourcesFromDisk(g_MissionCategoryDict, "defs/missioncategories", ".missioncategory", NULL,  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	bLoadedOnce = true;
	return 1;
}


AUTO_RUN;
void missiondef_RegisterMissionCategoryDictionary(void)
{
	g_MissionCategoryDict = RefSystem_RegisterSelfDefiningDictionary("MissionCategory", false, parse_MissionCategory, true, true, NULL);

	resDictManageValidation(g_MissionCategoryDict, missiondef_ValidateCategoryCB);

	if (IsServer()) {
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_MissionCategoryDict, ".name", ".scope", NULL, NULL, NULL);
		}

		resDictProvideMissingResources(g_MissionCategoryDict);
	} else {
		resDictRequestMissingResources(g_MissionCategoryDict, 8, false, resClientRequestSendReferentCommand);
	}
}

// ----------------------------------------------------------------------------------
// Mission Def Utilities
// ----------------------------------------------------------------------------------

MissionDef *missiondef_FindMissionByName(MissionDef *pRootDef, const char *pcName)
{
	int i;
	MissionDef *pResultDef;

	if (!pcName) {
		return NULL;
	}
	if (!pRootDef) {
		return RefSystem_ReferentFromString(g_MissionDictionary, pcName);
	}

	if (pRootDef->name && (stricmp(pRootDef->name, pcName) == 0)) {
		return pRootDef;
	}

	// Check child missions first
	for (i = eaSize(&pRootDef->subMissions) - 1; i >= 0; --i) {
		if (!pRootDef->subMissions[i]) {
			continue;
		}

		pResultDef = missiondef_FindMissionByName(pRootDef->subMissions[i], pcName);
		if (pResultDef) {
			return pResultDef;
		}
	}

	return NULL;
}


MissionDef* missiondef_DefFromRefString(const char *pcRefString)
{
	MissionDef *pDef = NULL;
	char parentName[MAX_MISSIONREF_LEN];
	char *pcChildName = NULL;

	PERFINFO_AUTO_START_FUNC();

	devassertmsg(strlen(pcRefString) < MAX_MISSIONREF_LEN, "Mission ref string is too long, increase buffer size!");
	strcpy(parentName, pcRefString);
	pcChildName = strstr(parentName, "::");
	if (pcChildName) {
		*pcChildName = 0;
		pcChildName += 2;
	}
	
	pDef = (MissionDef*)RefSystem_ReferentFromString(g_MissionDictionary, parentName);
	
	if (pDef && pcChildName) {
		pDef = missiondef_ChildDefFromName(pDef, pcChildName);
	}

	// Special error check for old-style mission names with more than one level of mission hierarchy
	if (pcChildName && !pDef) {
		const char *pcString = NULL;
		ANALYSIS_ASSUME(pcChildName != NULL);
		pcString = strstr(pcChildName, "::");
		if (pcString) {
			Errorf("Couldn't find mission %s.  Instead of A::B::C, try A::C.", pcRefString);
		}
	}

	PERFINFO_AUTO_STOP();

	return pDef;
}


MissionDef* missiondef_ChildDefFromName(const MissionDef *pMissionDef, const char *pcName)
{
	if (pMissionDef && pcName) {
		int i, n = eaSize(&pMissionDef->subMissions);
		for (i = 0; i < n; i++) {
			if (!stricmp(pMissionDef->subMissions[i]->name, pcName)) {
				return pMissionDef->subMissions[i];
			}
		}
	}
	return NULL;
}


AUTO_TRANS_HELPER_SIMPLE;
MissionDef* missiondef_ChildDefFromNamePooled(const MissionDef *pMissionDef, const char *pcPooledName)
{
	if (pMissionDef && pcPooledName) {
		int i, n = eaSize(&pMissionDef->subMissions);
		for (i = 0; i < n; i++) {
			if (pMissionDef->subMissions[i]->name == pcPooledName) {
				return pMissionDef->subMissions[i];
			}
		}
	}
	return NULL;
}


bool missiondef_HasUIString(MissionDef *pDef)
{
	if (pDef && GET_REF(pDef->uiStringMsg.hMessage)) {
		const char *pcText = TranslateDisplayMessage(pDef->uiStringMsg);
		if (pcText && pcText[0]) {
			return true;
		}
	}
	return false;
}


bool missiondef_HasSummaryString(MissionDef *pDef)
{
	if (pDef && GET_REF(pDef->summaryMsg.hMessage)) {
		const char *pcText = TranslateDisplayMessage(pDef->summaryMsg);
		if (pcText && pcText[0]) {
			return true;
		}
	}
	return false;
}


bool missiondef_HasDisplayName(MissionDef *pDef)
{
	if (pDef && GET_REF(pDef->displayNameMsg.hMessage)) {
		const char *pcText = TranslateDisplayMessage(pDef->displayNameMsg);
		if (pcText && pcText[0]) {
			return true;
		}
	}
#ifndef NO_EDITORS
	else if (pDef && pDef->displayNameMsg.pEditorCopy) {
		const char *pcText = TranslateMessagePtr(pDef->displayNameMsg.pEditorCopy);
		if (pcText && pcText[0]) {
			return true;
		}
	}
#endif
	if (quickLoadMessages) { // HACK to prevent errors
		return true;
	}
	return false;
}


bool missiondef_HasReturnString(MissionDef *pDef)
{
	if (pDef && GET_REF(pDef->msgReturnStringMsg.hMessage)) {
		const char *pcText = TranslateDisplayMessage(pDef->msgReturnStringMsg);
		if (pcText && pcText[0]) {
			return true;
		}
	}
#ifndef NO_EDITORS
	else if (pDef && pDef->msgReturnStringMsg.pEditorCopy) {
		const char *pcText = TranslateMessagePtr(pDef->msgReturnStringMsg.pEditorCopy);
		if (pcText && pcText[0]) {
			return true;
		}
	}
#endif
	return false;
}


bool missiondef_HasFailedReturnString(MissionDef *pDef)
{
	if (pDef && GET_REF(pDef->failReturnMsg.hMessage)) {
		const char *pcText = TranslateDisplayMessage(pDef->failReturnMsg);
		if (pcText && pcText[0]) {
			return true;
		}
	}
#ifndef NO_EDITORS
	else if (pDef && pDef->failReturnMsg.pEditorCopy) {
		const char *pcText = TranslateMessagePtr(pDef->failReturnMsg.pEditorCopy);
		if (pcText && pcText[0]) {
			return true;
		}
	}
#endif
	return false;
}


void missiondef_CreateRefString(MissionDef *pDef, MissionDef *pParentDef)
{
	static char *estrBuffer = NULL;

	if (pParentDef && pParentDef->name) {
		SET_HANDLE_FROM_STRING(g_MissionDictionary, pParentDef->name, pDef->parentDef);
	}
	if (pDef->name) {
		estrClear(&estrBuffer);
		if (!pParentDef) {
			estrCopy2(&estrBuffer, pDef->name);
		} else {
			estrPrintf(&estrBuffer, "%s::%s", pParentDef->pchRefString, pDef->name);
		}
		pDef->pchRefString = allocAddString(estrBuffer);
	}
}


void missiondef_CreateRefStringsRecursive(MissionDef *pDef, MissionDef *pParentDef)
{
	int i, n = eaSize(&pDef->subMissions);

	missiondef_CreateRefString(pDef, pParentDef);

	for (i = 0; i < n; ++i) {
		missiondef_CreateRefString(pDef->subMissions[i], pDef);
	}
}


// Creates a list of all missions that can be granted from these actions.  Assumes that only GrantMission actions can actually grant missions
static void missiondef_FindAllGrantedMissionsInActions(WorldGameActionProperties ***peaActions, MissionDef *pRootDef, MissionDef ***peaGrantedMissions, FindGrantMissionCallback callbackFunc, void *pUserData)
{
	int i, n = eaSize(peaActions);

	if (!pRootDef || !peaGrantedMissions) {
		Errorf("Programmer error in missiondef_FindAllGrantedMissions");	// bail out
	}

	for(i=0; i<n; i++) {
		WorldGameActionProperties *pAction = (*peaActions)[i];

		if (pAction->eActionType == WorldGameActionType_GrantSubMission && pAction->pGrantSubMissionProperties) {
			// Doesn't look for global missions, only ones that are children of the root (but it could!)
			const char *pcSubMissionName = pAction->pGrantSubMissionProperties->pcSubMissionName;
			MissionDef *pChild = pcSubMissionName ? missiondef_ChildDefFromNamePooled(pRootDef, pcSubMissionName) : NULL;
			if (pChild)	{
				// Search recursively if we haven't already searched this node
				if (-1 == eaFind(peaGrantedMissions, pChild)) {
					eaPush(peaGrantedMissions, pChild);
					missiondef_FindAllGrantedMissions(pChild, pRootDef, peaGrantedMissions, callbackFunc, pUserData);
				}
			}

			if (callbackFunc) {
				callbackFunc(pcSubMissionName, pChild, pRootDef, pAction, pUserData);
			}
		}
	}
}


void missiondef_FindAllGrantedMissions(MissionDef *pDef, MissionDef *pRootDef, MissionDef ***peaGrantedMissions, FindGrantMissionCallback callbackFunc, void *pUserData)
{
	MissionDef **eaTempMissionList = NULL;

	// If the user didn't supply an eaArray, they don't care about the list of missions; create a temporary list
	if (!peaGrantedMissions) {
		peaGrantedMissions = &eaTempMissionList;
	}

	if (pDef && pRootDef && peaGrantedMissions) {
		missiondef_FindAllGrantedMissionsInActions(&pDef->ppOnStartActions, pRootDef, peaGrantedMissions, callbackFunc, pUserData);
		missiondef_FindAllGrantedMissionsInActions(&pDef->ppSuccessActions, pRootDef, peaGrantedMissions, callbackFunc, pUserData);
		missiondef_FindAllGrantedMissionsInActions(&pDef->ppFailureActions, pRootDef, peaGrantedMissions, callbackFunc, pUserData);
		missiondef_FindAllGrantedMissionsInActions(&pDef->ppOnReturnActions, pRootDef, peaGrantedMissions, callbackFunc, pUserData);
	}

	// Clear tempGrantedMissions if we used it
	eaDestroy(&eaTempMissionList);
}


bool missiondef_RemoveUnusedSubmissions(MissionDef *pDef)
{
	bool bRemoved = false;

	if (pDef) {
		int i, n = eaSize(&pDef->subMissions);
		MissionDef **eaGrantedMissions = NULL;

		// Find all missions that could be granted from the root mission
		missiondef_FindAllGrantedMissions(pDef, pDef, &eaGrantedMissions, NULL, NULL);

		// Delete all missions that couldn't be granted
		for(i=n-1; i>=0; i--) {
			if (-1 == eaFind(&eaGrantedMissions, pDef->subMissions[i]))	{
				MissionDef *pDeleteDef = pDef->subMissions[i];
				eaRemove(&pDef->subMissions, i);
				StructDestroy(parse_MissionDef, pDeleteDef);
				bRemoved = true;
			}
		}
		eaDestroy(&eaGrantedMissions);
	}

	return bRemoved;
}


void missiondef_CleanSubmission(MissionDef *pDef)
{
	int i;

	if (pDef->scope) {
		pDef->scope = NULL;
	}
	if (pDef->eaObjectiveMaps) {
		eaDestroy(&pDef->eaObjectiveMaps);
	}
	if (pDef->pchReturnMap) {
		pDef->pchReturnMap = NULL;
	}
	if (pDef->autoGrantOnMap) {
		pDef->autoGrantOnMap = NULL;
	}
	if (IS_HANDLE_ACTIVE(pDef->displayNameMsg.hMessage)) {
		REMOVE_HANDLE(pDef->displayNameMsg.hMessage);
	}
	if (IS_HANDLE_ACTIVE(pDef->detailStringMsg.hMessage)) {
		REMOVE_HANDLE(pDef->detailStringMsg.hMessage);
	}
	if (IS_HANDLE_ACTIVE(pDef->msgReturnStringMsg.hMessage)) {
		REMOVE_HANDLE(pDef->msgReturnStringMsg.hMessage);
	}
#ifndef NO_EDITORS
	if (pDef->displayNameMsg.pEditorCopy && pDef->displayNameMsg.pEditorCopy->pcDefaultString) {
		StructFreeString(pDef->displayNameMsg.pEditorCopy->pcDefaultString);
		pDef->displayNameMsg.pEditorCopy->pcDefaultString = NULL;
	}
	if (pDef->detailStringMsg.pEditorCopy && pDef->detailStringMsg.pEditorCopy->pcDefaultString) {
		StructFreeString(pDef->detailStringMsg.pEditorCopy->pcDefaultString);
		pDef->detailStringMsg.pEditorCopy->pcDefaultString = NULL;
	}
	if (pDef->msgReturnStringMsg.pEditorCopy && pDef->msgReturnStringMsg.pEditorCopy->pcDefaultString) {
		StructFreeString(pDef->msgReturnStringMsg.pEditorCopy->pcDefaultString);
		pDef->msgReturnStringMsg.pEditorCopy->pcDefaultString = NULL;
	}
#endif
	pDef->missionType = 0;
	pDef->uTimeout = 0;
	pDef->repeatable = 0;
	pDef->needsReturn = 1;
	pDef->bIsHandoff = 0;
	StructReset( parse_MissionLevelDef, &pDef->levelDef );	
	pDef->eReturnType = MissionReturnType_None;
	if (pDef->missionReqs) {
		exprDestroy(pDef->missionReqs);
		pDef->missionReqs = NULL;
	}
	if (pDef->params) {
		pDef->params->NumericRewardScale = 0;
		pDef->params->NumericRewardScaleIneligible = 0;
		pDef->params->NumericRewardScaleAlreadyCompleted = 0;
		pDef->params->iScaleRewardOverTime = 0;
		eaDestroyStruct(&pDef->params->eaNumericScales, parse_MissionNumericScale);
	}

	// Clean out submissions
	for(i=eaSize(&pDef->subMissions)-1; i>=0; --i) {
		StructDestroy(parse_MissionDef, pDef->subMissions[i]);
	}
	eaDestroy(&pDef->subMissions);
}


// Gets how often this mission can be completed for secondary credit (seconds)
U32 missiondef_GetSecondaryCreditLockoutTime(MissionDef *pDef)
{
	// For now this is a per-project value... it may eventually vary on different missions
	return gConf.fSecondaryMissionCooldownHours*60*60;
}

ExprFuncTable* missiondef_CreateExprFuncTable(void)
{
	static ExprFuncTable* s_missionFuncTable = NULL;

	if (!s_missionFuncTable) {
		s_missionFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_missionFuncTable, "Mission");
		exprContextAddFuncsToTableByTag(s_missionFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_missionFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_missionFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_missionFuncTable, "entityutil");
	}
	return s_missionFuncTable;
}


ExprFuncTable* missiondef_CreateRequiresExprFuncTable(void)
{
	static ExprFuncTable* s_missionRequiresFuncTable = NULL;

	if (!s_missionRequiresFuncTable) {
		s_missionRequiresFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_missionRequiresFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_missionRequiresFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_missionRequiresFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_missionRequiresFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_missionRequiresFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_missionRequiresFuncTable, "entityutil");
	}
	return s_missionRequiresFuncTable;
}


ExprFuncTable* missiondef_CreateOpenMissionExprFuncTable(void)
{
	static ExprFuncTable* s_openMissionFuncTable = NULL;

	if (!s_openMissionFuncTable) {
		s_openMissionFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_openMissionFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_openMissionFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_openMissionFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_openMissionFuncTable, "OpenMission");
		exprContextAddFuncsToTableByTag(s_openMissionFuncTable, "util");
	}
	return s_openMissionFuncTable;
}


ExprFuncTable* missiondef_CreateOpenMissionRequiresExprFuncTable(void)
{
	static ExprFuncTable* s_openMissionRequiresFuncTable = NULL;

	if (!s_openMissionRequiresFuncTable) {
		s_openMissionRequiresFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_openMissionRequiresFuncTable, "entityutil");
	}
	return s_openMissionRequiresFuncTable;
}


MissionMap* missiondef_GetMissionMap(MissionDef *pDef, const char *pcMapName)
{
	int i;

	if (pDef && pDef->eaObjectiveMaps && pcMapName) {
		for(i=eaSize(&pDef->eaObjectiveMaps)-1; i>=0; i--) {
			if(!stricmp(pDef->eaObjectiveMaps[i]->pchMapName, pcMapName)) {
				return pDef->eaObjectiveMaps[i];
			}
		}
	}
	return NULL;
}


bool missiondef_HasObjectiveOnMap(MissionDef *pDef, const char *pcMapName, const char *pcMapVars) 
{
	int i;

	if (pDef && pcMapName && pDef->eaObjectiveMaps) {
		// Find map by name
		for(i=eaSize(&pDef->eaObjectiveMaps)-1; i>=0; i--) {
			MissionMap *pMap = pDef->eaObjectiveMaps[i];
			const char *pcCurrMapVars = worldVariableArrayToString(pMap->eaWorldVars);

			if (!pcCurrMapVars || !pcMapVars || stricmp(pcMapVars,pcCurrMapVars)==0) {
				if (pMap->pchMapName && stricmp(pMap->pchMapName, pcMapName)==0) {
					return true;
				}
			}
		}
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Mission Utilities
// ----------------------------------------------------------------------------------

bool mission_IsComplete(Mission *pMission)
{
	return (pMission->state == MissionState_Succeeded || pMission->state == MissionState_Failed);
}


MissionDef *mission_GetOrigDef(const Mission *pMission)
{
	if (pMission) {
		MissionDef *pRootDef = GET_REF(pMission->rootDefOrig);
		if (pRootDef && pRootDef->name && pMission->missionNameOrig) {
			if (pRootDef->name == pMission->missionNameOrig) {
				return pRootDef;
			} else {
				return missiondef_ChildDefFromNamePooled(pRootDef, pMission->missionNameOrig);
			}
		}
	}
	return NULL;
}


bool mission_HasUIString(const Mission *pMission)
{
	if (pMission) {
		MissionDef *pDef = mission_GetDef(pMission);
		return missiondef_HasUIString(pDef);
	}
	return false;
}


bool mission_HasDisplayName(const Mission *pMission)
{
	if (pMission) {
		MissionDef *pDef = mission_GetDef(pMission);
		return missiondef_HasDisplayName(pDef);
	}
	return false;
}


bool mission_HasReturnString(const Mission *pMission)
{
	if (pMission) {
		MissionDef *pDef = mission_GetDef(pMission);
		return missiondef_HasReturnString(pDef);
	}
	return false;
}


bool mission_HasFailedReturnString(const Mission *pMission)
{
	if (pMission) {
		MissionDef *pDef = mission_GetDef(pMission);
		return missiondef_HasFailedReturnString(pDef);
	}
	return false;
}


MissionDef *mission_GetDef(const Mission *pMission)
{
	if (pMission) {
		if (pMission->missionNameOverride) {
			MissionDef *pRootDef = GET_REF(pMission->rootDefOverride);
			if (pRootDef && pRootDef->name) {
				if (!stricmp(pRootDef->name, pMission->missionNameOverride)) {
					return pRootDef;
				} else {
					return missiondef_ChildDefFromName(pRootDef, pMission->missionNameOverride);
				}
			}
		} else {
			return mission_GetOrigDef(pMission);
		}
	}
	return NULL;
}


Mission* mission_FindChildByName(Mission *pParent, const char *pcMissionName)
{
	Mission *pRet = NULL;
	int i, n = eaSize(&pParent->children);
	
	for (i = 0; i < n; i++) {
		if ( pParent->children[i]->missionNameOverride && !stricmp(pParent->children[i]->missionNameOverride, pcMissionName)) {
			return pParent->children[i];
		}
		if ( pParent->children[i]->missionNameOrig && !stricmp(pParent->children[i]->missionNameOrig, pcMissionName)) {
			return pParent->children[i];
		}
		pRet = mission_FindChildByName(pParent->children[i], pcMissionName);
		if (pRet) {
			return pRet;
		}
	}
	return NULL;
}


// Returns the number of items added
int mission_GetSubmissions(Mission *pMission, Mission ***peaMissionList, int iInsertAt, MissionComparatorFunc comparatorFunc, MissionAddToListCB addCB, MissionExpandChildrenCB expandCB, bool bRecurse)
{
	int i, n = eaSize(&pMission->children);
	int iNumAdded = 0;
	
	if (iInsertAt > eaSize(peaMissionList) || iInsertAt < 0) {
		iInsertAt = eaSize(peaMissionList);  // add to the end
	}

	// Insert all children into the array
	for (i = 0; i < n; i++) {
		Mission *pSubMission = pMission->children[i];
		if (pSubMission && (!addCB || addCB(pSubMission))){
			eaInsert(peaMissionList, pSubMission, iInsertAt+iNumAdded);
			iNumAdded++;
		}
	}
	devassert(iInsertAt+iNumAdded <= eaSize(peaMissionList));

	// Sort the children that were added
	if (comparatorFunc && iNumAdded > 1) {
		qsort((void*)((*peaMissionList)+iInsertAt), iNumAdded, sizeof(Mission*), comparatorFunc);
	}

	// Recurse to sub-misisons
	if (bRecurse) {
		int tmp = iNumAdded;
		int pos = iInsertAt;
		for (i = 0; i < tmp && pos < eaSize(peaMissionList); i++, pos++){
			if (!expandCB || expandCB((*peaMissionList)[pos])){
				int iAdded = mission_GetSubmissions((*peaMissionList)[pos], peaMissionList, pos+1, comparatorFunc, addCB, expandCB, true);
				iNumAdded += iAdded;
				pos += iAdded;
			}
		}
	}
	return iNumAdded;
}


// Returns true if pMission has a mission or submission on the map(pchMapName,pchMapVars)
bool mission_HasMissionOrSubMissionOnMap(Mission *pMission, const char *pcMapName, const char *pcMapVars, bool bCheckInProgress)
{
	if ((!bCheckInProgress || pMission->state == MissionState_InProgress)
			&& missiondef_HasObjectiveOnMap(mission_GetDef(pMission), pcMapName, pcMapVars)) {
		return true;

	} else if (pMission) {
		int i;
		for(i = eaSize(&pMission->children)-1; i >= 0; i--) {
			Mission *pSubMission = pMission->children[i];
			if (mission_HasMissionOrSubMissionOnMap(pSubMission, pcMapName, pcMapVars, bCheckInProgress)) {
				return true;
			}
		}
	}
	return false;
}


// Gets the number of seconds remaining before the Mission expires
U32 mission_GetTimeRemainingSeconds(const Mission *pMission)
{
	MissionDef *pDef = mission_GetDef(pMission);
	if (pMission->expirationTime && pMission->expirationTime > timeServerSecondsSince2000()) {
		return MIN(pMission->expirationTime - timeServerSecondsSince2000(), (U32)pDef->uTimeout);
	}
	return 0;
}


MissionType mission_GetType(const Mission *pMission)
{
	MissionDef *pRootDef = GET_REF(pMission->rootDefOrig);
	return pRootDef ? pRootDef->missionType : MissionType_Normal;
}

// Returns the root mission def for the given mission def
MissionDef * missiondef_GetRootDef(MissionDef *pDef)
{
	MissionDef *pRootDef = pDef;
	while (pRootDef && GET_REF(pRootDef->parentDef)) 
	{
		pRootDef = GET_REF(pRootDef->parentDef);
	}
	return pRootDef;
}

MissionType missiondef_GetType(const MissionDef *pDef)
{
	const MissionDef *pRootDef = pDef;
	while (pRootDef && GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}
	return pRootDef ? pRootDef->missionType : MissionType_Normal;
}


Mission* mission_GetFirstInProgressObjective(const Mission *pMission)
{
	Mission *pResult = NULL;
	int iMinDisplayOrder = -1;
	int i;

	// Only consider InProgress missions
	if (pMission && pMission->state == MissionState_InProgress) {
		// Search children first, because this should return deeper nodes first
		for (i = 0; i < eaSize(&pMission->children); i++) {
			if (pMission->children[i]->displayOrder < iMinDisplayOrder || iMinDisplayOrder == -1){
				Mission *pTempResult = mission_GetFirstInProgressObjective(pMission->children[i]);
				if (pTempResult) {
					iMinDisplayOrder = pMission->children[i]->displayOrder;
					pResult = pTempResult;
				}
			}
		}

		// No InProgress children; return myself
		if (!pResult) {
			pResult = (Mission*)pMission;
		}
	}

	return pResult;
}


// ----------------------------------------------------------------------------------
// Mission Info Utilities
// ----------------------------------------------------------------------------------

MissionInfo* mission_GetInfoFromPlayer(const Entity *pEnt)
{
	return SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
}


Mission* mission_GetMissionByName(MissionInfo *pInfo, const char *pcMissionName)
{
	MissionDef *pMissionDef = missiondef_DefFromRefString(pcMissionName);
	return pMissionDef ? mission_GetMissionFromDef(pInfo, pMissionDef) : NULL;
}


// This is less efficient than getting by Def if the mission exists,
// but you need to use this one if the mission does not exist or you can't find it.
// This is normally used for namespace (UGC) missions.
Mission* mission_GetMissionByOrigName(MissionInfo *pInfo, const char *pcMissionName)
{
	int i;

	for(i=eaSize(&pInfo->missions)-1; i>=0; --i) {
		Mission *pMission = pInfo->missions[i];
		if (stricmp(pMission->missionNameOrig, pcMissionName) == 0) {
			return pMission;
		}
	}
	return NULL;
}

__forceinline static bool mission_UseSlowMissionLookup(void)
{
#ifdef GAMESERVER
	return g_bHasMadLibsMissions;
#else
	return true; // Always use slow lookup on the client, for now
#endif
}

// Finds a root-level mission matching the given def
Mission* mission_GetMissionFromDef(MissionInfo *pInfo, const MissionDef *pDefToMatch)
{
	if (!pDefToMatch) {
		return NULL;
	}
	if (mission_UseSlowMissionLookup())
	{
		int i, n = eaSize(&pInfo->missions);
		
		// Check persisted Mission list
		for (i = 0; i < n; i++) {
			Mission *pCurrMission = pInfo->missions[i];
			MissionDef *pMissionDef = GET_REF(pCurrMission->rootDefOverride);
			if (pMissionDef == pDefToMatch) {
				return pCurrMission;
			}
			pMissionDef = GET_REF(pCurrMission->rootDefOrig);
			if (pMissionDef == pDefToMatch) {
				return pCurrMission;
			}
		}

		// Check non-persisted Mission list
		n = eaSize(&pInfo->eaNonPersistedMissions);
		for (i = 0; i < n; i++) {
			Mission *pCurrMission = pInfo->eaNonPersistedMissions[i];
			MissionDef *pMissionDef = GET_REF(pCurrMission->rootDefOverride);
			if (pMissionDef == pDefToMatch) {
				return pCurrMission;
			}
			pMissionDef = GET_REF(pCurrMission->rootDefOrig);
			if (pMissionDef == pDefToMatch) {
				return pCurrMission;
			}
		}
	
		// Check the discovered perks list
		n = eaSize(&pInfo->eaDiscoveredMissions);
		for (i = 0; i < n; i++) {
			Mission *pCurrMission = pInfo->eaDiscoveredMissions[i];
			MissionDef *pMissionDef = GET_REF(pCurrMission->rootDefOverride);
			if (pMissionDef == pDefToMatch) {
				return pCurrMission;
			}
			pMissionDef = GET_REF(pCurrMission->rootDefOrig);
			if (pMissionDef == pDefToMatch) {
				return pCurrMission;
			}
		}
	}
	else
	{
		Mission *pMission;

		// Check persisted Mission list
		if (pMission = eaIndexedGetUsingString(&pInfo->missions, pDefToMatch->pchRefString))
			return pMission;
		// Check non-persisted Mission list
		if (pMission = eaIndexedGetUsingString(&pInfo->eaNonPersistedMissions, pDefToMatch->pchRefString))
			return pMission;
		// Check the discovered perks list
		if (pMission = eaIndexedGetUsingString(&pInfo->eaDiscoveredMissions, pDefToMatch->pchRefString))
			return pMission;
	}
	return NULL;
}


Mission* mission_GetMissionOrSubMissionByName(MissionInfo *pInfo, const char *pcMissionName)
{
	MissionDef *pMissionDef = missiondef_DefFromRefString(pcMissionName);
	return pMissionDef ? mission_GetMissionOrSubMission(pInfo, pMissionDef) : NULL;
}


static Mission *mission_FindChildSubMission(Mission *pMission, const MissionDef *pDefToMatch)
{
	int i;
	if (pMission->missionNameOrig && pDefToMatch->name && (stricmp(pMission->missionNameOrig, pDefToMatch->name) == 0)) {
		return pMission;
	}
	for(i=eaSize(&pMission->children)-1; i>=0; --i) {
		Mission* pResult = mission_FindChildSubMission(pMission->children[i], pDefToMatch);
		if (pResult) {
			return pResult;
		}
	}
	return NULL;
}


static Mission *mission_FindSubMission(Mission *pMission, const MissionDef *pDefToMatch)
{
	MissionDef *pMissionDef = GET_REF(pMission->rootDefOverride);
	if (pMissionDef == pDefToMatch) {
		return pMission;
	}
	pMissionDef = GET_REF(pMission->rootDefOrig);
	if (pMissionDef == pDefToMatch) {
		return pMission;
	}
	if (pMissionDef == GET_REF(pDefToMatch->parentDef)) {
		int i;
		for(i=eaSize(&pMission->children)-1; i>=0; --i) {
			Mission* pResult = mission_FindChildSubMission(pMission->children[i], pDefToMatch);
			if (pResult) {
				return pResult;
			}
		}
	}
	return NULL;
}


// Finds a mission matching the given def
Mission* mission_GetMissionOrSubMission(MissionInfo *pInfo, const MissionDef *pDefToMatch)
{
	int i, n = eaSize(&pInfo->missions);

	if (!pDefToMatch) {
		return NULL;
	}

	for (i = 0; i < n; i++) {
		Mission* pResult = mission_FindSubMission(pInfo->missions[i], pDefToMatch);
		if (pResult) {
			return pResult;
		}
	}

	// Check non-persisted Mission list
	n = eaSize(&pInfo->eaNonPersistedMissions);
	for (i = 0; i < n; i++) {
		Mission* pResult = mission_FindSubMission(pInfo->eaNonPersistedMissions[i], pDefToMatch);
		if (pResult) {
			return pResult;
		}
	}

	// Check discovered perks list
	n = eaSize(&pInfo->eaDiscoveredMissions);
	for (i = 0; i < n; i++) {
		Mission* pResult = mission_FindSubMission(pInfo->eaDiscoveredMissions[i], pDefToMatch);
		if (pResult) {
			return pResult;
		}
	}
	return NULL;
}


CompletedMission* mission_GetCompletedMissionByDef(const MissionInfo *pInfo, const MissionDef *pDefToMatch)
{
	if (pInfo && pDefToMatch && pDefToMatch->name) {
		return eaIndexedGetUsingString(&pInfo->completedMissions, pDefToMatch->name);
	}
	return NULL;
}


CompletedMission* mission_GetCompletedMissionByName(const MissionInfo *pInfo, const char *pcName)
{
	if (pInfo && pcName) {
		return eaIndexedGetUsingString(&pInfo->completedMissions, pcName);
	}
	return NULL;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
bool mission_FuncHasCompletedMission_LoadVerify(ExprContext *pContext, const char *pcMissionName)
{
#ifdef GAMESERVER
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;
	if (pMissionDef && pTargetMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		pEvent->type = EventType_MissionState;
		estrPrintf(&estrBuffer, "MissionComplete_%s", pcMissionName);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->pchMissionRefString = allocAddString(pcMissionName);
		pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
#endif
	return false;
}

AUTO_EXPR_FUNC(player, mission, UIGen, SmartAds) ACMD_NAME(HasCompletedMission) ACMD_EXPR_STATIC_CHECK(mission_FuncHasCompletedMission_LoadVerify);
bool mission_FuncHasCompletedMission(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	MissionInfo *pInfo;

#ifdef GAMECLIENT
	Entity *pPlayerEnt = entActivePlayerPtr();
#else
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
#endif

	if (!pPlayerEnt || !pcMissionName)
		return false;

#ifdef GAMESERVER
	if (!missiondef_DefFromRefString(pcMissionName))
	{
		Errorf("HasCompletedMission: Couldn't find mission %s", pcMissionName);
		return false;
	}
#endif

	pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (!pInfo)
		return false;

	if (mission_GetCompletedMissionByName(pInfo, pcMissionName))
		return true;

	return false;
}

U32 completedmission_GetNumTimesCompleted(const CompletedMission *pCompletedMission)
{
	if (pCompletedMission) {
		U32 iNumTimesCompleted = 0;
		int i;
		for (i = eaSize(&pCompletedMission->eaStats)-1; i>=0; --i) {
			iNumTimesCompleted += (pCompletedMission->eaStats[i]->timesRepeated+1);
		}
		return iNumTimesCompleted ? iNumTimesCompleted : 1;
	}
	return 0;
}


U32 completedmission_GetLastCompletedTime(const CompletedMission *pCompletedMission)
{
	if (pCompletedMission) {
		U32 iLastCompletedTime = pCompletedMission->completedTime;
		int i;
		for (i = eaSize(&pCompletedMission->eaStats)-1; i>=0; --i) {
			iLastCompletedTime = MAX(iLastCompletedTime, pCompletedMission->eaStats[i]->lastCompletedTime);
		}
		return iLastCompletedTime;
	}
	return 0;
}


AUTO_TRANS_HELPER_SIMPLE;
U32 completedmission_GetLastStartedTime(const CompletedMission *pCompletedMission)
{
	if (pCompletedMission) {
		U32 iLastStartedTime = pCompletedMission->startTime;
		int i;
		for (i = eaSize(&pCompletedMission->eaStats)-1; i>=0; --i) {
			iLastStartedTime = MAX(iLastStartedTime, pCompletedMission->eaStats[i]->lastStartTime);
		}
		return iLastStartedTime;
	}
	return 0;
}


U32 completedmission_GetLastCooldownRepeatCount(const CompletedMission *pCompletedMission)
{
	if (pCompletedMission) {
		return pCompletedMission->iRepeatCooldownCount;
	}
	return 0;
}


U32 mission_GetCooldownBlockStart(U32 uTimeToCheck, U32 uCoolDownHours, bool bUseBlockTime)
{
	if (uCoolDownHours > 0 && bUseBlockTime) {	
		U32 uCurHour = uTimeToCheck / SECONDS_PER_HOUR;
		U32 uBlocksSince = uCurHour  /  uCoolDownHours;					// round down to nearest block
		U32 uBlockSeconds = (uBlocksSince * uCoolDownHours) * SECONDS_PER_HOUR;		// convert back to seconds
		
		// now adjust 12 and 24 hour blocks
		// to make PST time to be 12:00am (or 1:00 during daylight savings)
		if (uCoolDownHours >= 12 && uCoolDownHours % 12 == 0) {
			uBlockSeconds += MISSION_COOLDOWN_PST_OFFSET * SECONDS_PER_HOUR;
			
			if (uBlockSeconds > uTimeToCheck) {
				// use previous block	
				uBlockSeconds -= uCoolDownHours * SECONDS_PER_HOUR;
			}
		}
		
		return uBlockSeconds;
	}
	
	// no cooldown or don't use blkock time therefore return passed in time
	return uTimeToCheck;	
}

// Returns pointer to static data that is invalid after next call
const MissionCooldownInfo *mission_GetCooldownInfoInternal(MissionDef *pMissionDef, MissionCooldown *pCooldown, CompletedMission *pCompletedMission)
{
	static MissionCooldownInfo missionInfo = {0};

	PERFINFO_AUTO_START_FUNC();

	// wipe the data on each call
	missionInfo.bIsInCooldown = 0;
	missionInfo.bIsInCooldownBlock = 0;
	missionInfo.uCooldownSecondsLeft = 0;
	missionInfo.uRepeatCount = 0;

	// Make sure the mission isn't on cooldown
	if (pMissionDef->repeatable && (pMissionDef->fRepeatCooldownHours > 0 || pMissionDef->fRepeatCooldownHoursFromStart)) {
		F32 fTimeSec = 0;
		F32 fTimeHours = 0;
		U32 iRepeatCooldownCount = 0;

		if (pCompletedMission) {
			if (pMissionDef->fRepeatCooldownHours > 0) {
				fTimeSec = timeServerSecondsSince2000() - mission_GetCooldownBlockStart(completedmission_GetLastCompletedTime(pCompletedMission), pMissionDef->fRepeatCooldownHours, pMissionDef->bRepeatCooldownBlockTime);
				iRepeatCooldownCount = completedmission_GetLastCooldownRepeatCount(pCompletedMission);
				fTimeHours = fTimeSec/3600.f;
				if (fTimeHours < pMissionDef->fRepeatCooldownHours) {
					if (iRepeatCooldownCount >= pMissionDef->iRepeatCooldownCount) {
						missionInfo.bIsInCooldown = true;				
					}
					missionInfo.bIsInCooldownBlock = true;
					missionInfo.uCooldownSecondsLeft = max(pMissionDef->fRepeatCooldownHours * 3600.0f - fTimeSec, missionInfo.uCooldownSecondsLeft);
					missionInfo.uRepeatCount = iRepeatCooldownCount;
				}
			}
			if (pMissionDef->fRepeatCooldownHoursFromStart > 0) {
				fTimeSec = timeServerSecondsSince2000() - mission_GetCooldownBlockStart(completedmission_GetLastStartedTime(pCompletedMission), pMissionDef->fRepeatCooldownHoursFromStart, pMissionDef->bRepeatCooldownBlockTime);
				fTimeHours = fTimeSec/3600.f;
				iRepeatCooldownCount = completedmission_GetLastCooldownRepeatCount(pCompletedMission);
				if (fTimeHours < pMissionDef->fRepeatCooldownHoursFromStart) {
					if (iRepeatCooldownCount >= pMissionDef->iRepeatCooldownCount) {
						missionInfo.bIsInCooldown = true;				
					}
					missionInfo.bIsInCooldownBlock = true;
					missionInfo.uCooldownSecondsLeft = max(pMissionDef->fRepeatCooldownHoursFromStart * 3600.0f - fTimeSec, missionInfo.uCooldownSecondsLeft);
					missionInfo.uRepeatCount = iRepeatCooldownCount;
				}
			}

		} else {
			if (pCooldown) {
				if (pMissionDef->fRepeatCooldownHours > 0) {
					fTimeSec = timeServerSecondsSince2000() - mission_GetCooldownBlockStart(pCooldown->completedTime, pMissionDef->fRepeatCooldownHours, pMissionDef->bRepeatCooldownBlockTime);
					fTimeHours = fTimeSec/3600.f;
					iRepeatCooldownCount = pCooldown->iRepeatCooldownCount;
					if (fTimeHours < pMissionDef->fRepeatCooldownHours) {
						if(iRepeatCooldownCount >= pMissionDef->iRepeatCooldownCount) {
							missionInfo.bIsInCooldown = true;				
						}
						missionInfo.bIsInCooldownBlock = true;
						missionInfo.uCooldownSecondsLeft = max(pMissionDef->fRepeatCooldownHours * 3600.0f - fTimeSec, missionInfo.uCooldownSecondsLeft);
						missionInfo.uRepeatCount = iRepeatCooldownCount;
					}
				}
				if (pMissionDef->fRepeatCooldownHoursFromStart > 0) {
					fTimeSec = timeServerSecondsSince2000() - mission_GetCooldownBlockStart(pCooldown->startTime, pMissionDef->fRepeatCooldownHoursFromStart, pMissionDef->bRepeatCooldownBlockTime);
					fTimeHours = fTimeSec/3600.f;
					iRepeatCooldownCount = pCooldown->iRepeatCooldownCount;
					if (fTimeHours < pMissionDef->fRepeatCooldownHoursFromStart) {
						if (iRepeatCooldownCount >= pMissionDef->iRepeatCooldownCount) {
							missionInfo.bIsInCooldown = true;				
						}
						missionInfo.bIsInCooldownBlock = true;
						missionInfo.uCooldownSecondsLeft = max(pMissionDef->fRepeatCooldownHoursFromStart * 3600.0f - fTimeSec, missionInfo.uCooldownSecondsLeft);
						missionInfo.uRepeatCount = iRepeatCooldownCount;
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return &missionInfo;

}

// Return cooldown information about this mission
// Returns pointer to static data which is overwritten on next call
const MissionCooldownInfo *mission_GetCooldownInfo(Entity *pEntity, const char *pcMissionName)
{
	// This is a static value that is always zero and exists just to return zero'd data
	static MissionCooldownInfo missionInfo = {0};

	MissionDef *pMissionDef;
	CompletedMission *pCompletedMission;
	MissionCooldown *pCooldown;

	pMissionDef = missiondef_DefFromRefString(pcMissionName);

	// Make sure the mission specified exists
	if (!pEntity || !pEntity->pChar || !pMissionDef || pMissionDef->missionType == MissionType_Perk)
	{	
		return &missionInfo;
	}

	pCompletedMission = eaIndexedGetUsingString(&pEntity->pPlayer->missionInfo->completedMissions, pcMissionName);
	pCooldown = eaIndexedGetUsingString(&pEntity->pPlayer->missionInfo->eaMissionCooldowns, pcMissionName);

	return mission_GetCooldownInfoInternal(pMissionDef, pCooldown, pCompletedMission);
}


// Returns the number of times a player has completed the specified Mission.
U32 mission_GetNumTimesCompletedByName(MissionInfo *pInfo, const char *pcMissionName)
{
	if (pInfo && pcMissionName) {
		CompletedMission *pCompletedMission = eaIndexedGetUsingString(&pInfo->completedMissions, pcMissionName);
		return completedmission_GetNumTimesCompleted(pCompletedMission);
	}
	return 0;
}

// Returns true if the current mission state is succeeded
bool mission_StateSucceeded(MissionInfo *pInfo, const char *pcMissionName)
{
	if (pInfo && pcMissionName) 
	{
		Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
		if (pMission && pMission->state == MissionState_Succeeded)
		{
			return true;
		}
	}
	return false;
}


U32 mission_GetNumTimesCompletedByDef(MissionInfo* pInfo, MissionDef *pDefToMatch)
{
	if (pInfo && pDefToMatch && pDefToMatch->name) {
		return mission_GetNumTimesCompletedByName(pInfo, pDefToMatch->name);
	}
	return 0;
}


U32 mission_GetLastCompletedTimeByName(const MissionInfo *pInfo, const char *pcMissionName)
{
	if (pInfo && pcMissionName){
		const CompletedMission *pCompletedMission = mission_GetCompletedMissionByName(pInfo, pcMissionName);
		return completedmission_GetLastCompletedTime(pCompletedMission);
	}
	return 0;
}


U32 mission_GetLastCompletedTimeByDef(const MissionInfo *pInfo, const MissionDef *pDef)
{
	if (pInfo && pDef) {
		const CompletedMission *pCompletedMission = mission_GetCompletedMissionByDef(pInfo, pDef);
		return completedmission_GetLastCompletedTime(pCompletedMission);
	}
	return 0;
}


Mission* mission_FindMissionFromRefString(MissionInfo *pInfo, const char *pcRefString)
{
	Mission *pMission = NULL;
	char parentName[MAX_MISSIONREF_LEN];
	char *pcChildName = NULL;
	
	if (pInfo && pcRefString) {
		devassertmsg(strlen(pcRefString) < MAX_MISSIONREF_LEN, "Mission ref string is too long, increase buffer size!");
		strcpy(parentName, pcRefString);
		pcChildName = strstr(parentName, "::");
		if (pcChildName) {
			*pcChildName = 0;
			pcChildName += 2;
		}
		
		pMission = eaIndexedGetUsingString(&pInfo->missions, parentName);
		if (!pMission) {
			pMission = eaIndexedGetUsingString(&pInfo->eaNonPersistedMissions, parentName);
		}
		if (!pMission) {
			pMission = eaIndexedGetUsingString(&pInfo->eaDiscoveredMissions, parentName);
		}
		if (pMission && pcChildName) {
			pMission = mission_FindChildByName(pMission, pcChildName);
		}

		return pMission;
	}
	return NULL;
}


Mission* mission_FindMissionFromUGCProjectID(MissionInfo *pInfo, U32 uProjectID)
{
	if (pInfo && uProjectID) {
		int i;
		for (i = eaSize(&pInfo->missions)-1; i >= 0; i--) {
			MissionDef* pDef = mission_GetDef(pInfo->missions[i]);
			if (pDef && pDef->ugcProjectID == uProjectID) {
				return pInfo->missions[i];
			}
		}
		for (i = eaSize(&pInfo->eaNonPersistedMissions)-1; i >= 0; i--) {
			MissionDef* pDef = mission_GetDef(pInfo->eaNonPersistedMissions[i]);
			if (pDef && pDef->ugcProjectID == uProjectID) {
				return pInfo->eaNonPersistedMissions[i];
			}
		}
		for (i = eaSize(&pInfo->eaDiscoveredMissions)-1; i >= 0; i--) {
			MissionDef* pDef = mission_GetDef(pInfo->eaDiscoveredMissions[i]);
			if (pDef && pDef->ugcProjectID == uProjectID) {
				return pInfo->eaDiscoveredMissions[i];
			}
		}
	}
	return NULL;
}


// Indicates if the mission info has any missions with the given state and tag
bool mission_HasMissionInStateByTag(MissionInfo *pMissionInfo, MissionTag eTag, MissionState eState)
{
	if (pMissionInfo) {
		FOR_EACH_IN_EARRAY_FORWARDS(pMissionInfo->missions, Mission, pMission) {
			if (pMission && 
				pMission->state == eState && 
				missiondef_HasTag(mission_GetDef(pMission), eTag))
			{
				return true;
			}
		} FOR_EACH_END

		FOR_EACH_IN_EARRAY_FORWARDS(pMissionInfo->eaNonPersistedMissions, Mission, pMission) {
			if (pMission && 
				pMission->state == eState && 
				missiondef_HasTag(mission_GetDef(pMission), eTag))
			{
				return true;
			}
		} FOR_EACH_END

		FOR_EACH_IN_EARRAY_FORWARDS(pMissionInfo->eaDiscoveredMissions, Mission, pMission) {
			if (pMission && 
				pMission->state == eState && 
				missiondef_HasTag(mission_GetDef(pMission), eTag))
			{
				return true;
			}
		} FOR_EACH_END
	}

	return false;
}

// ----------------------------------------------------------------------------------
// Mission Template Utilities
// ----------------------------------------------------------------------------------

void missiontemplate_GenerateDefFromTemplate(MissionDef *pDef, bool bRecursive)
{
	if (pDef->missionTemplate) {
		MissionTemplate *pMissionTemplate = pDef->missionTemplate;
		MissionTemplateType *pTemplateType = RefSystem_ReferentFromString("MissionTemplate", pMissionTemplate->templateTypeName);

		if (pTemplateType && pTemplateType->CBFunc) {
			pTemplateType->CBFunc(pDef, NULL);
		} else {
			ErrorFilenamef(pDef->filename, "Mission has template type that doesn't exist or that has no callback function.");
		}
	}

	// Generate templates in children if requested
	if (bRecursive) {
		// Make sure we get the size of the submissions now, since the template may have created new submissions
		int i, n = eaSize(&pDef->subMissions);
		for(i=0; i<n; i++) {
			missiontemplate_GenerateDefFromTemplate(pDef->subMissions[i], bRecursive);
		}
	}
}


// Assumes the var name is pooled
TemplateVariable* missiontemplate_LookupTemplateVarInVarGroup(TemplateVariableGroup *pVarGroup, const char *pcVarName, bool bRecursive)
{
	const char *pcVarNamePooled = allocFindString(pcVarName);
	int i, n = eaSize(&pVarGroup->variables);

	if (!pcVarNamePooled) {
		// The variable name should have been put in the string pool when the template type was registered
		Errorf("Template callback function uses unregistered template variable %s", pcVarName);
		return NULL;
	}

	for(i=0; i<n; i++) {
		TemplateVariable *pVar = pVarGroup->variables[i];
		if (pcVarNamePooled && pVar->varName && pcVarNamePooled == pVar->varName) {
			return pVar;
		}
	}

	if (bRecursive) {
		n = eaSize(&pVarGroup->subGroups);
		for(i=0; i<n; i++) {
			TemplateVariable *pVar = missiontemplate_LookupTemplateVarInVarGroup(pVarGroup->subGroups[i], pcVarNamePooled, true);
			if (pVar) {
				return pVar;
			}
		}
	}
	return NULL;
}


TemplateVariable* missiontemplate_LookupTemplateVar(MissionTemplate *pMissionTemplate, const char *pcVarName)
{
	if (pMissionTemplate) {
		return missiontemplate_LookupTemplateVarInVarGroup(pMissionTemplate->rootVarGroup, pcVarName, true);
	}
	return NULL;
}


void missiontemplate_FindChildGroupsByName(TemplateVariableGroup *pVarGroup, const char *pcGroupName, TemplateVariableGroup ***peaSubGroups)
{
	const char *pcPooledName = allocFindString(pcGroupName);
	int i, n = eaSize(&pVarGroup->subGroups);
	for (i = 0; i < n; ++i) {
		if (pVarGroup->subGroups[i]->groupName == pcPooledName) {
			eaPush(peaSubGroups, pVarGroup->subGroups[i]);
		}
	}
}


bool missiontemplate_VarGroupIsMadLibs(const TemplateVariableGroup *pGroup)
{
	int i, n = eaSize(&pGroup->variables);
	for (i = 0; i < n; i++) {
		if (pGroup->variables[i]->varValueExpression) {
			return true;
		}
	}
	n = eaSize(&pGroup->subGroups);
	for (i = 0; i < n; i++) {
		if (missiontemplate_VarGroupIsMadLibs(pGroup->subGroups[i])) {
			return true;
		}
	}
	return false;
}


ExprFuncTable* missiontemplate_CreateTemplateVarExprFuncTable()
{
	static ExprFuncTable* s_missionTemplateFuncTable = NULL;

	if (!s_missionTemplateFuncTable) {
		s_missionTemplateFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_missionTemplateFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_missionTemplateFuncTable, "mission_template");
	}
	return s_missionTemplateFuncTable;
}

S32 openmission_GetRewardTierForScoreEntry(OpenMissionScoreEntry ***peaSortedScores, S32 index, S32 iNumPlayers)
{
	int iTable;

	// Determine which Reward Table and what scaling to use based on player's rank
	if (index >= eaSize(peaSortedScores) || !peaSortedScores || !*peaSortedScores || !(*peaSortedScores)[index] || (*peaSortedScores)[index]->fPoints <= 0)
	{
		return -1;
	}
	else
	{
		F32 fTopScore = (*peaSortedScores)[0]->fPoints;

		if (fTopScore == (*peaSortedScores)[index]->fPoints)
		{
			iTable = 0;
		}
		else
		{
			int iPosition = (int)floor(iNumPlayers * .1 + .5) - 1;
			if (iPosition <= 0)
			{
				iPosition = 0;
			}

			fTopScore = (iPosition < eaSize(peaSortedScores) && (*peaSortedScores)[iPosition] ? (*peaSortedScores)[iPosition]->fPoints : -1);

			if (index <= MAX((int)(iNumPlayers *.2), 1) || (*peaSortedScores)[index]->fPoints == fTopScore)
			{
				iTable = 1;
			}
			else
			{
				iPosition = (int)floor(iNumPlayers * .5 + .5) - 1;

				if (iPosition <= 0)
				{
					iPosition = 0;
				}

				fTopScore = (iPosition < eaSize(peaSortedScores) && (*peaSortedScores)[iPosition] ? (*peaSortedScores)[iPosition]->fPoints : -1);

				if (index <= MAX((int)(iNumPlayers *.5), 1) || (*peaSortedScores)[index]->fPoints == fTopScore)
				{
					iTable = 2;
				}
				else
				{
					iTable = 3;
				}

			}

		}
	}

	return iTable;
}

// ---------------------------------------------
// Message Formatting Utilities
// ---------------------------------------------

void missionsystem_FormatMessagePtr(Language eLang, const char *pcSourceName, const Entity *pEnt, const MissionDef *pDef, U32 iNemesisID, char **ppcResult, Message *pMessage)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	Mission *pMission = NULL;
	const char *pcFormat = langTranslateMessageDefault(eLang, pMessage, NULL);
	WorldVariable **eaMapVars = NULL;
	MissionDef *pParentDef = GET_REF(pDef->parentDef);
	MissionType eType = pParentDef ? pParentDef->missionType : pDef->missionType;

	if (missiondef_GetType(pDef) == MissionType_OpenMission) {
		MapState *pMapState = mapState_FromEnt((Entity*)pEnt);
		OpenMission *pOpenMission = mapState_OpenMissionFromName(pMapState, pDef->name);
		pMission = SAFE_MEMBER(pOpenMission, pMission);
	} else if (pInfo) {
		pMission = mission_GetMissionOrSubMission(pInfo, pDef);
	}

	if (pcFormat) {
		if (iNemesisID) {
			Entity *pNemesisEnt = player_GetNemesisByID(pEnt, iNemesisID);
			if (eType == MissionType_OpenMission) {
				MapState *pMapState = mapState_FromEnt((Entity*)pEnt);
				eaCreate(&eaMapVars);
				mapState_GetAllPublicVars(pMapState, &eaMapVars);
				langFormatGameMessage(eLang, ppcResult, pMessage, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_MISSIONDEF(pDef), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_MISSIONVARS(SAFE_MEMBER(pMission, eaMissionVariables)), STRFMT_END);
				eaDestroy(&eaMapVars);
			} else {
				langFormatGameMessage(eLang, ppcResult, pMessage, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_MISSIONDEF(pDef), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MISSIONVARS(SAFE_MEMBER(pMission, eaMissionVariables)), STRFMT_END);
			}
		} else {
			if (eType == MissionType_OpenMission) {
				MapState *pMapState = mapState_FromEnt((Entity*)pEnt);
				eaCreate(&eaMapVars);
				mapState_GetAllPublicVars(pMapState, &eaMapVars);
				langFormatGameMessage(eLang, ppcResult, pMessage, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_MISSIONDEF(pDef), STRFMT_MAPVARS(eaMapVars), STRFMT_MISSIONVARS(SAFE_MEMBER(pMission, eaMissionVariables)), STRFMT_END);
				eaDestroy(&eaMapVars);
			} else {
				langFormatGameMessage(eLang, ppcResult, pMessage, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_MISSIONDEF(pDef), STRFMT_MISSIONVARS(SAFE_MEMBER(pMission, eaMissionVariables)), STRFMT_END);
			}
		}
	}
}


static void MissionWarpCostsLoadInternal(const char *pchPath, S32 iWhen)
{
	S32 i, s;

	loadstart_printf("Loading Mission Warp Costs...");

	StructReset(parse_MissionWarpCostDefs, &s_MissionWarpCostDefs);

	if (g_pDefineMissionWarpCost)
	{
		DefineDestroy(g_pDefineMissionWarpCost);
	}
	g_pDefineMissionWarpCost = DefineCreate();

	ParserLoadFiles(NULL, "defs/config/MissionWarpCosts.def", "MissionWarpCosts.bin", PARSER_OPTIONALFLAG, parse_MissionWarpCostDefs, &s_MissionWarpCostDefs);

	s = eaSize(&s_MissionWarpCostDefs.eaDefs);
	for (i = 0; i < s; i++)
	{
		MissionWarpCostDef* pDef = s_MissionWarpCostDefs.eaDefs[i];
		pDef->eCostType = i+1;
		DefineAddInt(g_pDefineMissionWarpCost, pDef->pchName, pDef->eCostType);
	}

	// Validation
	if (isDevelopmentMode() && IsGameServerBasedType())
	{
		for (i = 0; i < s; i++)
		{
			MissionWarpCostDef* pDef = s_MissionWarpCostDefs.eaDefs[i];
			if (!pDef->pchNumeric || !pDef->iNumericCost)
			{
				Errorf("MissionWarpCostDef '%s' does not specify a numeric item or has no cost", pDef->pchName);
			}
			else if (!item_DefFromName(pDef->pchNumeric))
			{
				Errorf("MissionWarpCostDef '%s' references non-existent numeric ItemDef '%s'", pDef->pchName, pDef->pchNumeric);
			}
		}
	}

	loadend_printf(" done (%d Mission Warp Costs).", s);
}

static void MissionWarpCostsLoad(void)
{
	MissionWarpCostsLoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/MissionWarpCosts.def", MissionWarpCostsLoadInternal);
}

AUTO_STARTUP(MissionWarpCostsServer) ASTRT_DEPS(Items);
void MissionWarpCostsLoadServer(void)
{
	MissionWarpCostsLoad();
}

AUTO_STARTUP(MissionWarpCostsClient);
void MissionWarpCostsLoadClient(void)
{
	MissionWarpCostsLoad();
}

void TutorialScreenRegionsReload(const char *relpath, int when)
{
	S32 i,s;

	loadstart_printf("Loading TutorialScreenRegions...");

	StructReset(parse_TutorialScreenRegions, &g_TutorialScreenRegions);

	ParserLoadFiles(NULL, "defs/config/TutorialScreenRegions.def", "TutorialScreenRegions.bin", PARSER_OPTIONALFLAG, parse_TutorialScreenRegions, &g_TutorialScreenRegions);
	s = eaSize(&g_TutorialScreenRegions.eaRegions);

	for (i = 0; i < s; i++)
	{
		const char *pchName = g_TutorialScreenRegions.eaRegions[i]->pchName;

		if (StaticDefineIntGetIntDefault(TutorialScreenRegionEnum, pchName, kTutorialScreenRegion_None) == kTutorialScreenRegion_None)
		{
			DefineAddInt(g_pDefineTutorialScreenRegions, pchName, g_iNextTutorialScreenRegion + kTutorialScreenRegion_FIRST_DATA_DEFINED);
			g_iNextTutorialScreenRegion++;
		}

		if (IsClient())
		{
			// Validate UIGen names on client.
			//
			// This is a bit temperamental, since the startup task doesn't directly
			// depend on UIGen's to load.
			const char *pchUIGen = g_TutorialScreenRegions.eaRegions[i]->pchUIGen;
			if (pchUIGen && *pchUIGen && !RefSystem_ReferentFromString("UIGen", pchUIGen))
			{
				ErrorFilenamef("defs/config/TutorialScreenRegions.def", "Tutorial screen region '%s' refers to UIGen '%s' not in dictionary", pchName, pchUIGen);
			}
		}
	}

	loadend_printf(" done (%d TutorialScreenRegions).", s);
}

AUTO_STARTUP(TutorialScreenRegions);
void TutorialScreenRegionsLoad(void)
{
	g_pDefineTutorialScreenRegions = DefineCreate();
	TutorialScreenRegionsReload("defs/config/TutorialScreenRegions.def", 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/TutorialScreenRegions.def", TutorialScreenRegionsReload);
}

// Loads all Mission UI Types
AUTO_STARTUP(MissionUITypes);
void MissionUITypesLoad(void)
{
	S32 i,s;

	loadstart_printf("Loading MissionUITypes...");
	ParserLoadFiles(NULL, "defs/config/MissionUITypes.def", "MissionUITypes.bin", PARSER_OPTIONALFLAG, parse_MissionUITypes, &g_MissionUITypes);
	g_pDefineMissionUITypes = DefineCreate();
	s = eaSize(&g_MissionUITypes.eaTypes);

	for (i = 0; i < s; i++) {
		MissionUITypeData* pData = g_MissionUITypes.eaTypes[i];
		pData->eUIType = i + 1;
		DefineAddInt(g_pDefineMissionUITypes, pData->pchName, pData->eUIType);
	}

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(MissionUITypeEnum, "MissionUIType_");
#endif

	loadend_printf(" done (%d MissionUITypes).", s);
}

// Loads all Mission Tags
AUTO_STARTUP(MissionTags);
void MissionTagsLoad(void)
{
	S32 i,s;
	MissionTags Data = {0};

	loadstart_printf("Loading MissionTags...");
	ParserLoadFiles(NULL, "defs/config/MissionTags.def", "MissionTags.bin", PARSER_OPTIONALFLAG, parse_MissionTags, &Data);
	g_pDefineMissionTags = DefineCreate();
	s = eaSize(&Data.ppTags);

	for (i = 0; i < s; i++) {
		DefineAddInt(g_pDefineMissionTags, Data.ppTags[i]->pchName, i+1);
	}

	loadend_printf(" done (%d MissionTags).", s);
	StructDeInit(parse_MissionTags, &Data);
}

// Returns whether or not the cooldown on the mission should be updated when dropping the mission
bool missiondef_DropMissionShouldUpdateCooldown(MissionDef* pDef)
{
	if (pDef && !pDef->bCooldownOnlyOnSuccess) {
		return (pDef->fRepeatCooldownHours || pDef->fRepeatCooldownHoursFromStart);
	}
	return false;
}

// Indicates if the mission has the given tag
bool missiondef_HasTag(SA_PARAM_OP_VALID MissionDef *pMissionDef, MissionTag eTag)
{
	if (pMissionDef) {		
		if (eaiFind(&pMissionDef->peMissionTags, eTag) >= 0) {
			return true;
		}
	}
	return false;
}

// Get MissionUITypeData associated with the MissionUIType
MissionUITypeData* mission_GetMissionUITypeData(MissionUIType eType)
{
	int i;
	for (i = eaSize(&g_MissionUITypes.eaTypes)-1; i >= 0; i--)
	{
		MissionUITypeData* pData = g_MissionUITypes.eaTypes[i];
		if (pData->eUIType == eType)
		{
			return pData;
		}
	}
	return NULL;
}

MissionWarpCostDef* missiondef_GetWarpCostDef(MissionDef* pMissionDef)
{
	if (pMissionDef && 
		pMissionDef->pWarpToMissionDoor && 
		pMissionDef->pWarpToMissionDoor->eCostType != kMissionWarpCostType_None)
	{
		const char* pchKey = StaticDefineIntRevLookup(MissionWarpCostTypeEnum, pMissionDef->pWarpToMissionDoor->eCostType);
		return eaIndexedGetUsingString(&s_MissionWarpCostDefs.eaDefs, pchKey);
	}
	return NULL;
}

bool mission_CanPlayerUseMissionWarp(Entity* pEnt, Mission* pMission)
{
	MissionDef* pMissionDef = mission_GetDef(pMission);
	if (pEnt && pMissionDef && pMissionDef->pWarpToMissionDoor)
	{
		S32 iLevel = entity_GetSavedExpLevel(pEnt);

		if (pMission->state != MissionState_InProgress)
		{
			return false;
		}
		if (!entIsAlive(pEnt) || entIsInCombat(pEnt))
		{
			return false;
		}
		if (!allegiance_CanPlayerUseWarp(pEnt))
		{
			return false;
		}
		if (pMissionDef->pWarpToMissionDoor->iRequiredLevel > iLevel)
		{
			return false;
		}
		return true;
	}
	return false;
}

const char *mission_GetMapDisplayNameFromMissionDef(MissionDef *pMissionDef)
{
	int i;
	if (pMissionDef && pMissionDef->eaObjectiveMaps && eaSize(&pMissionDef->eaObjectiveMaps))
	{
		for(i = 0; i < eaSize(&pMissionDef->eaObjectiveMaps); i++)
		{
			// Find the first displayable objective map
			if(!pMissionDef->eaObjectiveMaps[i]->bHideGotoString)
			{
				// Cache message if not already set
				if(!GET_REF(pMissionDef->eaObjectiveMaps[i]->hMapDisplayName) && pMissionDef->eaObjectiveMaps[i]->pchMapName) {
					ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(pMissionDef->eaObjectiveMaps[i]->pchMapName);
					if (zminfo)
					{
						const char *pchMessageKey = zminfo ? zmapInfoGetDisplayNameMsgKey(zminfo) : NULL;
						if (pchMessageKey && msgExists(pchMessageKey))
							SET_HANDLE_FROM_STRING(gMessageDict, pchMessageKey, pMissionDef->eaObjectiveMaps[i]->hMapDisplayName);
					}
				}

				return TranslateMessageRefDefault(pMissionDef->eaObjectiveMaps[i]->hMapDisplayName, "");
			}
		}
	}
	return "";
}

static bool mission_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

static bool mission_GetAudioAssets_HandleActionBlockOverrides		(const ActionBlockOverride			**ppActionBlockOverrides,		const char ***peaStrings);
static bool mission_GetAudioAssets_HandleDialogBlocks				(const DialogBlock					**eaDialogBlocks,				const char ***peaStrings);
static bool mission_GetAudioAssets_HandleImageMenuItemOverrides		(const ImageMenuItemOverride		**ppImageMenuItemOverrides,		const char ***peaStrings);
static bool mission_GetAudioAssets_HandleInteractableOverrides		(const InteractableOverride			**ppInteractableOverrides,		const char ***peaStrings);
static bool mission_GetAudioAssets_HandleMissionOfferOverrides		(const MissionOfferOverride			**ppMissionOfferOverrides,		const char ***peaStrings);
static bool mission_GetAudioAssets_HandleSpecialDialogActions		(const SpecialDialogAction			**ppSpecialDialogActions,		const char ***peaStrings);
static bool mission_GetAudioAssets_HandleSpecialDialogOverrides		(const SpecialDialogOverride		**ppSpecialDialogOverrides,		const char ***peaStrings);
static bool mission_GetAudioAssets_HandleWorldGameActionProperties	(const WorldGameActionProperties	**ppWorldGameActionProperties,	const char ***peaStrings);

static bool mission_GetAudioAssets_HandleActionBlockOverrides(const ActionBlockOverride **ppActionBlockOverrides, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppActionBlockOverrides, const ActionBlockOverride, pActionBlockOverride) {
		if (pActionBlockOverride->pSpecialActionBlock) {
			FOR_EACH_IN_EARRAY(pActionBlockOverride->pSpecialActionBlock->dialogActions, const SpecialDialogAction, pSpecialDialogAction) {
				bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pSpecialDialogAction->actionBlock.eaActions, peaStrings);
			} FOR_EACH_END;
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleDialogBlocks(const DialogBlock **eaDialogBlocks, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(eaDialogBlocks, const DialogBlock, pDialogBlock) {
		bResourceHasAudio |= mission_GetAudioAssets_HandleString(pDialogBlock->audioName, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleImageMenuItemOverrides(const ImageMenuItemOverride **ppImageMenuItemOverrides, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppImageMenuItemOverrides, const ImageMenuItemOverride, pImageMenuItemOverride) {
		if (pImageMenuItemOverride->pImageMenuItem &&
			pImageMenuItemOverride->pImageMenuItem->action)
		{
			bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pImageMenuItemOverride->pImageMenuItem->action->eaActions, peaStrings);
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleInteractableOverrides(const InteractableOverride **ppInteractableOverrides, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppInteractableOverrides, const InteractableOverride, pInteractableOverride) {
		if (pInteractableOverride->pPropertyEntry &&
			pInteractableOverride->pPropertyEntry->pSoundProperties)
		{
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchAttemptSound,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchSuccessSound,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchFailureSound,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchInterruptSound,peaStrings);

			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchMovementTransStartSound,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchMovementTransEndSound,		peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchMovementReturnStartSound,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pInteractableOverride->pPropertyEntry->pSoundProperties->pchMovementReturnEndSound,	peaStrings);
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleMissionOfferOverrides(const MissionOfferOverride **ppMissionOfferOverrides, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppMissionOfferOverrides, const MissionOfferOverride, pMissionOfferOverride) {
		if (pMissionOfferOverride->pMissionOffer) {
			bResourceHasAudio |= mission_GetAudioAssets_HandleDialogBlocks(pMissionOfferOverride->pMissionOffer->greetingDialog,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleDialogBlocks(pMissionOfferOverride->pMissionOffer->offerDialog,		peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleDialogBlocks(pMissionOfferOverride->pMissionOffer->inProgressDialog,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleDialogBlocks(pMissionOfferOverride->pMissionOffer->completedDialog,	peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleDialogBlocks(pMissionOfferOverride->pMissionOffer->failureDialog,		peaStrings);
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleSpecialDialogActions(const SpecialDialogAction **ppSpecialDialogActions, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppSpecialDialogActions, const SpecialDialogAction, pSpecialDialogAction) {
		bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pSpecialDialogAction->actionBlock.eaActions, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleSpecialDialogOverrides(const SpecialDialogOverride **ppSpecialDialogOverrides, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppSpecialDialogOverrides, const SpecialDialogOverride, pSpecialDialogOverride) {
		if (pSpecialDialogOverride->pSpecialDialog) {
			bResourceHasAudio |= mission_GetAudioAssets_HandleDialogBlocks(pSpecialDialogOverride->pSpecialDialog->dialogBlock, peaStrings);
			bResourceHasAudio |= mission_GetAudioAssets_HandleSpecialDialogActions(pSpecialDialogOverride->pSpecialDialog->dialogActions, peaStrings);
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleWorldGameActionProperties(const WorldGameActionProperties **ppWorldGameActionProperties, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppWorldGameActionProperties, const WorldGameActionProperties, pWorldGameActionProperties) {
		if (pWorldGameActionProperties->pSendNotificationProperties) {
			bResourceHasAudio |= mission_GetAudioAssets_HandleString(pWorldGameActionProperties->pSendNotificationProperties->pchSound, peaStrings);
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool mission_GetAudioAssets_HandleMissonDef(const MissionDef *pMissionDef, const char ***peaStrings)
{
	bool bResourceHasAudio = false;

	bResourceHasAudio |= mission_GetAudioAssets_HandleString(pMissionDef->pchSoundAmbient,			peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleString(pMissionDef->pchSoundCombat,			peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleString(pMissionDef->pchSoundOnComplete,		peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleString(pMissionDef->pchSoundOnContactOffer,	peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleString(pMissionDef->pchSoundOnStart,			peaStrings);

	bResourceHasAudio |= mission_GetAudioAssets_HandleActionBlockOverrides  (pMissionDef->ppSpecialActionBlockOverrides,peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleImageMenuItemOverrides(pMissionDef->ppImageMenuItemOverrides,		peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleInteractableOverrides	(pMissionDef->ppInteractableOverrides,		peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleMissionOfferOverrides (pMissionDef->ppMissionOfferOverrides,		peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleSpecialDialogOverrides(pMissionDef->ppSpecialDialogOverrides,		peaStrings);

	bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pMissionDef->ppOnStartActions, peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pMissionDef->ppSuccessActions, peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pMissionDef->ppFailureActions, peaStrings);
	bResourceHasAudio |= mission_GetAudioAssets_HandleWorldGameActionProperties(pMissionDef->ppOnReturnActions, peaStrings);

	FOR_EACH_IN_EARRAY(pMissionDef->subMissions, const MissionDef, pSubMission) {
		bResourceHasAudio |= mission_GetAudioAssets_HandleMissonDef(pSubMission, peaStrings);
	} FOR_EACH_END;

	return bResourceHasAudio;
}

void mission_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	MissionDef *pMissionDef;
	ResourceIterator rI;

	*ppcType = strdup("MissionDef");

	resInitIterator(g_MissionDictionary, &rI);
	while (resIteratorGetNext(&rI, NULL, &pMissionDef))
	{
		*puiNumData = *puiNumData + 1;
		if (mission_GetAudioAssets_HandleMissonDef(pMissionDef, peaStrings)) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "AutoGen/mission_common_h_ast.c"
