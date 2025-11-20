/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityLogEnums.h"
#include "Character.h"
#include "contact_common.h"
#include "earray.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntityIterator.h"
#include "estring.h"
#include "StringUtil.h"
#include "gslContact.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslMapVariable.h"
#include "gslMission.h"
#include "gslMissionLockout.h"
#include "gslMissionRequest.h"
#include "gslMission_transact.h"
#include "gslNotify.h"
#include "gslOpenMission.h"
#include "gslPartition.h"
#include "gslProgression.h"
#include "gslUserExperience.h"
#include "mission_common.h"
#include "mission_enums.h"
#include "gslMissionSet.h"
#include "missionset_common.h"
#include "nemesis_common.h"
#include "gslGameAction.h"
#include "gameaction_common.h"
#include "GlobalTypes.h"
#include "ShardVariableCommon.h"
#include "gslActivityLog.h"
#include "gslUGCTransactions.h"
#include "gslWaypoint.h"
#include "objpath.h"
#include "objtransactions.h"
#include "LoggedTransactions.h"
#include "StructDefines.h"
#include "timing.h"
#include "transactionsystem.h"
#include "ugcprojectcommon.h"
#include "UGCCommon.h"
#include "UGCProjectUtils.h"
#include "inventoryTransactions.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeTransactions.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "reward.h"
#include "rewardCommon.h"
#include "HashFunctions.h"
#include "AutoTransDefs.h"
#include "../StaticWorld/group.h"
#include "Player.h"
#include "mapstate_common.h"
#include "EntitySavedData.h"
#include "progression_common.h"
#include "qsortG.h"
#include "file.h"
#include "WorldGrid.h"

#include "ResourceDBSupport.h"
#include "ResourceDBUtils.h"
#include "gslResourceDBSupport.h"

#include "GameAccountDataCommon.h"
#include "progression_transact.h"

#include "logging.h"
#include "file.h"
#include "ServerLib.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/gameaction_common_h_ast.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/mission_enums_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"
#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ugcprojectcommon_h_ast.h"
#include "AutoGen/gslMission_transact_c_ast.h"
#include "AutoGen/progression_common_h_ast.h"

// ----------------------------------------------------------------------------------
// Defines and headers
// ----------------------------------------------------------------------------------

static void mission_tr_PersistMissionAndChangeState(Entity *pEnt, Mission *pMission, MissionState eNewState, bool bForcePermanentComplete);
void mission_trh_DropFullChildren(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* mission, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
bool mission_trh_DropMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, const char* missionName, bool bHideNotification, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

#define MISSION_ADDED_MESG "MissionSystem.MissionAdded"
#define MISSION_ADD_ERROR_MESG "MissionSystem.MissionAddError"
#define MISSION_ADD_ERROR_ALREADY_EXISTS_MESG "MissionSystem.MissionAddErrorAlreadyExists"
#define MISSION_RETURNED_MESG "MissionSystem.MissionReturned"
#define MISSION_RETURN_ERROR_MESG "MissionSystem.MissionReturnError"
#define MISSION_DROPPED_MESG "MissionSystem.MissionDropped"
#define MISSION_REVIEW_REQUESTED_MESG "MissionSystem.MissionReviewRequest"
#define MISSION_DROP_ERROR_MESG "MissionSystem.MissionDropError"
#define MISSION_JOURNAL_FULL_MESG "MissionSystem.JournalFullError"
#define MISSION_PERK_DISCOVERED_MSG "MissionSystem.Perks.Discovered"
#define MISSION_PERK_COMPLETED_MSG "MissionSystem.Perks.Completed"
#define MISSION_UGC_UPGRADED_MSG "MissionSystem.MissionUGCUpgraded"
#define MISSION_UGC_UPGRADE_ERROR_MSG "MissionSystem.MissionUGCUpgradeError"

AUTO_STRUCT;
typedef struct DropMissionData	
{
	bool bUpdateCooldown;
	bool bHasRequests;
	GlobalType eEntType;
	ContainerID iEntID;
	const char* pchMissionName;		AST(POOL_STRING)
	U32 uStartTime;
	ContainerID iUGCID;
} DropMissionData;
extern ParseTable parse_DropMissionData[];
#define TYPE_parse_DropMissionData DropMissionData

// Defined in mission_common.c
extern const char** g_eaMissionsWaitingForOverrideProcessing;

// ----------------------------------------------------------------------------------
// Utility Functions to see what data will be locked
// ----------------------------------------------------------------------------------

bool missiondef_HasOnStartRewards(MissionDef *pDef)
{
	return (pDef && pDef->params && pDef->params->OnstartRewardTableName && pDef->params->OnstartRewardTableName[0]);
}

AUTO_TRANS_HELPER_SIMPLE;
bool missiondef_HasSuccessRewards(MissionDef *pDef)
{
	return (pDef && pDef->params && pDef->params->OnsuccessRewardTableName && pDef->params->OnsuccessRewardTableName[0]);
}

bool missiondef_HasFailureRewards(MissionDef *pDef)
{
	return (pDef && pDef->params && pDef->params->OnfailureRewardTableName && pDef->params->OnfailureRewardTableName[0]);
}

bool missiondef_MustLockInventoryForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockInventory(&pDef->ppSuccessActions, pRootDef))
			return true;
		if (missiondef_HasSuccessRewards(pDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockInventory(&pDef->ppFailureActions, pRootDef))
			return true;
		if (missiondef_HasFailureRewards(pDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockInventoryOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (missiondef_HasOnStartRewards(pDef)){
		return true;
	}
	if (gameaction_MustLockInventory(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missiondef_MustLockNPCEMailForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockNPCEMail(&pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockNPCEMail(&pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockNPCEMailOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockNPCEMail(&pDef->ppOnStartActions, pRootDef))
	{
		return true;
	}
	return false;
}

bool missiondef_MustLockMissionsForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockMissions(&pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockMissions(&pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockMissionsOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockMissions(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missiondef_MustLockNemesisForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockNemesis(&pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockNemesis(&pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockNemesisOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockNemesis(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missiondef_MustLockGameAccountForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockGameAccount(&pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockGameAccount(&pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockGameAccountOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockGameAccount(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missiondef_MustLockShardVariablesForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockShardVariables(&pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockShardVariables(&pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockShardVariablesOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockShardVariables(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missioninfo_MustLockShardVariablesForTurnIn(Mission *pMission, MissionDef *pRootDef)
{
	MissionDef *pDef = mission_GetDef(pMission);
	int i;

	if (!pDef) {
		return false;
	}

	if (gameaction_MustLockShardVariables(&pDef->ppOnReturnActions, pRootDef)) {
		return true;
	}
	for(i=eaSize(&pMission->children)-1; i>=0; --i) {
		if (missioninfo_MustLockShardVariablesForTurnIn(pMission->children[i], pRootDef)) {
			return true;
		}
	}
	return false;
}

bool missiondef_MustLockActivityLogForStateChange(MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockActivityLog(&pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockActivityLog(&pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockActivityLogOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockActivityLog(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missiondef_MustLockGuildOnStart(MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockGuild(&pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missiondef_MustLockGuildActivityLogForStateChange(bool inGuild, MissionDef *pDef, MissionDef *pRootDef, MissionState state)
{
	if (state == MissionState_Succeeded)
	{
		if (gameaction_MustLockGuildActivityLog(inGuild, &pDef->ppSuccessActions, pRootDef))
			return true;
	}
	else if (state == MissionState_Failed)
	{
		if (gameaction_MustLockGuildActivityLog(inGuild, &pDef->ppFailureActions, pRootDef))
			return true;
	}

	return false;
}

bool missiondef_MustLockGuildActivityLogOnStart(bool inGuild, MissionDef *pDef, MissionDef *pRootDef)
{
	if (gameaction_MustLockGuildActivityLog(inGuild, &pDef->ppOnStartActions, pRootDef)){
		return true;
	}
	return false;
}

bool missioninfo_MustLockGuildActivityLogForTurnIn(bool inGuild, Mission *pMission, MissionDef *pRootDef)
{
	MissionDef *pDef = mission_GetDef(pMission);
	int i;

	if (gameaction_MustLockGuildActivityLog(inGuild, &pDef->ppOnReturnActions, pRootDef)) {
		return true;
	}
	for(i=eaSize(&pMission->children)-1; i>=0; --i) {
		if (missioninfo_MustLockGuildActivityLogForTurnIn(inGuild, pMission->children[i], pRootDef)) {
			return true;
		}
	}
	return false;
}
// ----------------------------------------------------------------------------------
// Transaction Helper Functions
// ----------------------------------------------------------------------------------

AUTO_TRANS_HELPER_SIMPLE;
U32 mission_GetRewardSeedEx(U32 uEntID, MissionDef* pDef, U32 uMissionStartTime, U32 uTimesCompleted)
{
	U32 uSeed;
	if (pDef->missionType == MissionType_OpenMission || !pDef->repeatable)
	{
		uSeed = uEntID + uMissionStartTime;
	}
	else
	{
		uSeed = uEntID + hashString(pDef->pchRefString, false) + uTimesCompleted;
	}
	return uSeed;
}

// Get the seed to use for generating rewards for a specific mission
U32 mission_GetRewardSeed(Entity* pEnt, Mission* pMission, MissionDef* pDef)
{
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pEnt);

	if (pMission)
		pDef = mission_GetDef(pMission);

	if (pInfo && pDef)
	{
		U32 uTimesCompleted = mission_GetNumTimesCompletedByDef(pInfo, pDef);
		U32 uStartTime = SAFE_MEMBER(pMission, startTime);
		return mission_GetRewardSeedEx(pEnt->myContainerID, pDef, uStartTime, uTimesCompleted);
	}
	return 0;
}

// this creates a variable array out of a mission's variables; the returned array
// should be StructDestroy'ed afterward
AUTO_TRANS_HELPER
ATR_LOCKS(pMission, ".Eamissionvariables");
WorldVariableArray *mission_trh_CreateVariableArray(ATH_ARG NOCONST(Mission)* pMission)
{
	if (NONNULL(pMission))
	{
		WorldVariableArray *pVariableArray = StructCreate(parse_WorldVariableArray);
		int i;
		
		for (i = 0; i < eaSize(&pMission->eaMissionVariables); i++)
		{
			WorldVariable *pVariableCopy = StructCreate(parse_WorldVariable);
			worldVariableCopyFromContainer(pVariableCopy, (WorldVariableContainer*) pMission->eaMissionVariables[i]);
			eaPush(&pVariableArray->eaVariables, pVariableCopy);
		}

		return pVariableArray;
	}

	return NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(parentMission, ".Children");
NOCONST(Mission)* mission_trh_GetChildMission(ATH_ARG NOCONST(Mission)* parentMission, MissionDef *defToMatch)
{
	int i, n = eaSize(&parentMission->children);
	MissionDef *rootDefToMatch = defToMatch;
	
	while (GET_REF(rootDefToMatch->parentDef))
		rootDefToMatch = GET_REF(rootDefToMatch->parentDef);
	
	for (i = 0; i < n; i++)
	{
		if (parentMission->children[i]->missionNameOrig == defToMatch->name
			&& GET_REF(parentMission->children[i]->rootDefOrig) == rootDefToMatch)
			return parentMission->children[i];
	}
	return NULL;
}


// Created the rewardgated data needed for mission reward transactions
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt,".Pplayer.eaRewardGatedData");
RewardGatedDataInOut *mission_trh_CreateRewardGatedData(ATH_ARG NOCONST(Entity)* pEnt)
{
	RewardGatedDataInOut *pGatedData;
	U32 uTm = timeServerSecondsSince2000();
	S32 i;

	if(!g_RewardConfig.bUseRewardGatingMissions)
	{
		return NULL;
	}

	pGatedData = StructCreate(parse_RewardGatedDataInOut);

	for(i = 0; i < eaSize(&pEnt->pPlayer->eaRewardGatedData); ++i)
	{
		if(NONNULL(pEnt->pPlayer->eaRewardGatedData) && pEnt->pPlayer->eaRewardGatedData[i]->eType != RewardGatedType_None)
		{
			U32 uBlock = 0;
			U32 uBlockChar = 0;
			RewardGatedInfo *pRewardGatedInfo;
			NOCONST(RewardGatedTypeData) *pPlayerGated = pEnt->pPlayer->eaRewardGatedData[i];
			S32 iGatedIndex = 0;

			pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, pPlayerGated->eType);

			if(!pRewardGatedInfo)
			{
				// reward gated type ... error?
				continue;
			}

			// ### check for wrong gated type?

			if(pRewardGatedInfo->uHoursPerBlock)
			{
				uBlock = uTm / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
				uBlockChar = pPlayerGated->uTimeSet / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
			}

			if(uBlockChar == uBlock)
			{
				// return number of times already done this block
				if(pRewardGatedInfo->uNumberOfTimesToIncrement <= 1)
				{
					iGatedIndex = pPlayerGated->uNumTimes;
				}
				else
				{
					// rounded down
					iGatedIndex = pPlayerGated->uNumTimes / pRewardGatedInfo->uNumberOfTimesToIncrement;
				}
			}

			if(iGatedIndex > 0)
			{
				eaiPush(&pGatedData->eaCurrentGatedType, pPlayerGated->eType);
				eaiPush(&pGatedData->eaCurrentGatedValues, iGatedIndex);
			}
		}
	}

	return pGatedData;
}

// This changes the rewardgated data for a player. Do not call this outside of a transaction
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt,".Pplayer.eaRewardGatedData");
void mission_trh_ChangeRewardGatedData(ATH_ARG NOCONST(Entity)* pEnt, RewardGatedDataInOut *pGatedData)
{
	if(pGatedData)
	{
		S32 i;
		U32 tm = timeServerSecondsSince2000();

		for(i = 0; i < eaiSize(&pGatedData->eaGateTypesChanged); ++i)
		{
			S32 gatedType = pGatedData->eaGateTypesChanged[i];
			RewardGatedTypeData *pPlayerGated = eaIndexedGetUsingInt(&pEnt->pPlayer->eaRewardGatedData, gatedType);
			RewardGatedInfo *pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, gatedType);

			if(pRewardGatedInfo)
			{
				if(pPlayerGated)
				{
					U32 uBlock = 0;
					U32 uBlockChar = 0;

					if(pRewardGatedInfo->uHoursPerBlock)
					{
						uBlock = tm / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
						uBlockChar = pPlayerGated->uTimeSet / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
					}

					if(uBlockChar == uBlock)
					{
						++pPlayerGated->uNumTimes;
					}
					else
					{
						// New block set time used to 1
						pPlayerGated->uNumTimes = 1;
						pPlayerGated->uTimeSet = tm;
					}
				}
				else
				{
					// No information for this gated type, create it and set it with initial value (1)
					RewardGatedTypeData *pNewGatedData = StructCreate(parse_RewardGatedTypeData);

					pNewGatedData->eType = gatedType;
					pNewGatedData->uNumTimes = 1;
					pNewGatedData->uTimeSet = tm;

					if(!pEnt->pPlayer->eaRewardGatedData)
					{
						eaIndexedEnableNoConst(&pEnt->pPlayer->eaRewardGatedData, parse_RewardGatedTypeData);
					}

					eaIndexedAdd(&pEnt->pPlayer->eaRewardGatedData, pNewGatedData);
				}
			}
		}	// end for i

		// A test to see if removal of all gated data from the player. if there are no gated types before or after and the player has gated data. This means all of the data is in a past block
		// and its better to clear the gated data as there will be less fields to lock and look at in future calls
		if(!pGatedData->eaGateTypesChanged && !pGatedData->eaCurrentGatedType && NONNULL(pEnt->pPlayer->eaRewardGatedData))
		{
			eaDestroyStructNoConst(&pEnt->pPlayer->eaRewardGatedData, parse_RewardGatedTypeData);
		}
	}
}

// Generates mission rewards and stores them in pRewardBagsData.  This function is a intermediary step to reduce
// code duplication between different versions of mission_trh_GrantMissionRewards
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pplayer.eaRewardGatedData");
void mission_trh_GenerateMissionRewards(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt,
										MissionDef *pDef, 
										int eState, 
										ContactRewardChoices* rewardChoices, 
										U32 *seed, 
										MissionCreditType eCreditType, 
										int iMissionLevel, 
										U32 iMissionStartTime, 
										U32 iMissionEndTime,
										bool bSubMissionTurnIn,
										GiveRewardBagsData* pRewardBagsData,
										GameAccountDataExtract *pExtract,
										bool bUGCProject,
										bool bCreatedByAccount,
										bool bStatsQualifyForUGCRewards,
										bool bQualifyForUGCFeaturedRewards,
										F32 fAverageDurationInMinutes)
{
	int i, n;
	RecruitType eRecruitType = kRecruitType_None;
	bool bMissionQualifiesForUGCRewards = false;
	bool bMissionQualifiesForUGCFeaturedRewards = false;
	bool bMissionQualifiesForNonCombatUGCRewards = false;
	int iTimeLevel = 0;
	RewardContextData rewardContextData = {0};
	RewardGatedDataInOut *pGatedData = NULL;

	rewardContextData.iPlayerLevel = entity_trh_GetSavedExpLevelLimited(pEnt);
	rewardContextData.pExtract = pExtract;
	if(NONNULL(pEnt->pPlayer))
	{
		if (!rewardContextData.eaRewardMods)
		{
			eaIndexedEnable(&rewardContextData.eaRewardMods, parse_RewardModifier);
		}
		for(i = 0; i < eaSize(&pEnt->pPlayer->eaRewardMods); ++i)
		{
			eaIndexedAdd(&rewardContextData.eaRewardMods, (RewardModifier *)pEnt->pPlayer->eaRewardMods[i]);
		}
	}
	ent_trh_GetCharacterRewardContextInfo(&rewardContextData.CBIData, pEnt);

	if(NONNULL(pEnt->pTeam))
	{
		// use last recorded recruit type as its not worth going through team in this transaction to get an absolute up to the moment recruitment type
		eRecruitType = pEnt->pTeam->lastRecruitType;
	}

	if(bUGCProject)
	{
		bMissionQualifiesForUGCRewards = isProductionEditMode() || (bStatsQualifyForUGCRewards && !bCreatedByAccount);
		bMissionQualifiesForUGCFeaturedRewards = bQualifyForUGCFeaturedRewards;
		bMissionQualifiesForNonCombatUGCRewards = (pDef->ePlayType == ugcDefaultsGetNonCombatType());
		iTimeLevel = bMissionQualifiesForUGCRewards ? (isProductionEditMode() ? 30 : fAverageDurationInMinutes) : 0;
	}

	// create gated reward data
	pGatedData = mission_trh_CreateRewardGatedData(pEnt);

	reward_GenerateMissionActionRewards(PARTITION_IN_TRANSACTION, NULL /* Can't pass in whole player because it locks whole player */,
										pDef, eState, &pRewardBagsData->ppRewardBags, seed, eCreditType,
										bUGCProject ? rewardContextData.iPlayerLevel : iMissionLevel, iTimeLevel,
										iMissionStartTime, iMissionEndTime, eRecruitType, /*bUGCProject=*/bUGCProject,
										bMissionQualifiesForUGCRewards, bMissionQualifiesForUGCFeaturedRewards, bMissionQualifiesForNonCombatUGCRewards,
										bSubMissionTurnIn, /*bGenerateChestRewards=*/false, &rewardContextData, fAverageDurationInMinutes, pGatedData);
	eaDestroy(&rewardContextData.eaRewardMods);

	// changed gated info on character 
	mission_trh_ChangeRewardGatedData(pEnt, pGatedData);
	// destroy gated reward info
	if(pGatedData)
	{
		StructDestroy(parse_RewardGatedDataInOut, pGatedData);
	}

	if(rewardChoices)
	{
		n = eaSize(&rewardChoices->ppItemNames);
		for(i=0; i<n; i++)
		{
			eaPush(&pRewardBagsData->ppChoices, rewardChoices->ppItemNames[i]);
		}
	}

}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, .Pplayer.eaRewardGatedData")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome mission_trh_GrantMissionRewards(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt,
													   ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
													   MissionDef *pDef, 
													   int eState, 
													   ContactRewardChoices* rewardChoices, 
													   U32 *seed, 
													   MissionCreditType eCreditType, 
													   int iMissionLevel, 
													   U32 iMissionStartTime, 
													   U32 iMissionEndTime,
													   bool bSubMissionTurnIn,
													   const ItemChangeReason *pReason,
													   GameAccountDataExtract *pExtract,
													   bool bUGCProject,
													   bool bCreatedByAccount,
													   bool bStatsQualifyForUGCRewards,
													   bool bQualifyForUGCFeaturedRewards,
													   F32 fAverageDurationInMinutes)
{
	RewardOverflowBagPermission eAllowOverflowBag;
	GiveRewardBagsData pRewardBagsData = {0};
	bool success = true;
	S32 eFailBag = InvBagIDs_None;

	mission_trh_GenerateMissionRewards(ATR_PASS_ARGS, pEnt, pDef, eState, rewardChoices, seed, eCreditType, iMissionLevel, iMissionStartTime, iMissionEndTime, bSubMissionTurnIn, &pRewardBagsData, pExtract, bUGCProject, bCreatedByAccount, bStatsQualifyForUGCRewards, bQualifyForUGCFeaturedRewards, fAverageDurationInMinutes);

	eAllowOverflowBag = (pDef->missionType == MissionType_Perk) ? kRewardOverflow_AllowOverflowBag : kRewardOverflow_DisallowOverflowBag;

	if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, &pRewardBagsData, eAllowOverflowBag, &eFailBag, pReason, pExtract, NULL))
	{
		const char* pchBagFullMsgKey = inv_trh_GetBagFullMessageKey(ATR_PASS_ARGS, pEnt, (InvBagIDs)eFailBag, pExtract);
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, pchBagFullMsgKey, kNotifyType_InventoryFull);
		success = false;
	}
	eaDestroyStruct(&pRewardBagsData.ppRewardBags, parse_InventoryBag);
	eaDestroy(&pRewardBagsData.ppChoices);

	return (success?TRANSACTION_OUTCOME_SUCCESS:TRANSACTION_OUTCOME_FAILURE);
}

// Returns the number of times a player has completed the specified Mission.
AUTO_TRANS_HELPER
ATR_LOCKS(info, "completedMissions[]");
U32 missioninfo_trh_GetNumTimesCompleted(ATH_ARG NOCONST(MissionInfo)* info, const char *pchMissionName)
{
	NOCONST(CompletedMission) *pCompletedMission = eaIndexedGetUsingString(&info->completedMissions, pchMissionName);
	if (pCompletedMission){
		int i;
		U32 iNumTimesCompleted = 0;
		for (i = eaSize(&pCompletedMission->eaStats)-1; i>=0; --i){
			iNumTimesCompleted+=pCompletedMission->eaStats[i]->timesRepeated+1;
		}
		return iNumTimesCompleted?iNumTimesCompleted:1;
	}
	return 0;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pInfo, ".Unextrequestid, .Eamissionrequests[AO]");
void mission_trh_AddMissionRequests(ATR_ARGS, ATH_ARG NOCONST(MissionInfo)* pInfo, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef)
{
	int i;
	bool bAddedRequest = false;

	// Add any Mission Requests
	for (i = eaSize(&pDef->eaRequests)-1; i>=0; --i){
		NOCONST(MissionRequest) *pNewRequest = NULL;

		if (pDef->eaRequests[i]->eType == MissionDefRequestType_Mission && GET_REF(pDef->eaRequests[i]->hRequestedDef)){
			pNewRequest = StructCreateNoConst(parse_MissionRequest);
			COPY_HANDLE(pNewRequest->hRequestedMission, pDef->eaRequests[i]->hRequestedDef);
		} else if (pDef->eaRequests[i]->eType == MissionDefRequestType_MissionSet && GET_REF(pDef->eaRequests[i]->hRequestedMissionSet)){
			pNewRequest = StructCreateNoConst(parse_MissionRequest);
			COPY_HANDLE(pNewRequest->hRequestedMissionSet, pDef->eaRequests[i]->hRequestedMissionSet);
		}

		if (pNewRequest){
			pNewRequest->pchRequesterRef = pDef->pchRefString;
			pNewRequest->eState = MissionRequestState_Open;
			pNewRequest->uID = pInfo->uNextRequestID;
			pInfo->uNextRequestID++;
			eaIndexedAdd(&pInfo->eaMissionRequests, pNewRequest);
			bAddedRequest = true;
		}
	}

	if (bAddedRequest){
		QueueRemoteCommand_mission_RemoteFlagMissionRequestUpdate(ATR_RESULT_SUCCESS, 0, 0);
	}
}

AUTO_TRANS_HELPER;
void mission_trh_RemoveRequestsRecursive(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Mission)* pMission, ATH_ARG NOCONST(Mission)* pRootMission, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	char refstring[MAX_MISSIONREF_LEN];
	int i, j;

	EARRAY_OF(NOCONST(Mission)) eaChildMissions = NULL;

	if (NONNULL(pMission))
	{
		eaChildMissions = pMission->children;
	}
	else
	{
		eaChildMissions = pRootMission->children;
	}

	for (i = eaSize(&eaChildMissions)-1; i>=0; --i){
		mission_trh_RemoveRequestsRecursive(ATR_PASS_ARGS, pEnt, eaChildMissions[i], pRootMission, pReason, pExtract);
	}

	// We don't want to use the MissionDef's refstring here, because this needs to work even if the
	// MissionDef has been deleted 
	if (NONNULL(pMission)){
		sprintf(refstring, "%s::%s", pRootMission->missionNameOrig, pMission->missionNameOrig);
	} else {
		sprintf(refstring, "%s", pRootMission->missionNameOrig);
	}

	for (i = eaSize(&pEnt->pPlayer->missionInfo->eaMissionRequests)-1; i>=0; --i){
		if (!stricmp(pEnt->pPlayer->missionInfo->eaMissionRequests[i]->pchRequesterRef, refstring)){
			MissionRequest *pRequest = CONTAINER_RECONST(MissionRequest, eaRemove(&pEnt->pPlayer->missionInfo->eaMissionRequests, i));
			MissionDef *pRequestedDef = GET_REF(pRequest->hRequestedMission);

			//Drop the mission that was requested if there are no other requests open for it
			if (pRequestedDef){
				for (j = eaSize(&pEnt->pPlayer->missionInfo->eaMissionRequests)-1; j>=0; --j){
					if (GET_REF(pEnt->pPlayer->missionInfo->eaMissionRequests[j]->hRequestedMission) == pRequestedDef){
						break;
					}
				}
				if (j < 0){
					mission_tr_DropMission(ATR_PASS_ARGS, pEnt, pRequestedDef->name, pReason, /*NULL, */pExtract);
					
					// Also remove any items that could grant this mission
					inv_ent_trh_RemoveMissionItems(ATR_PASS_ARGS, pEnt, pRequestedDef->name, true, pReason, pExtract);
				}
			}

			StructDestroy(parse_MissionRequest, pRequest);
		}
	}
}

// Cleans up any "Recently Completed Secondary Missions" that have expired
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Missioninfo.Earecentsecondarymissions");
void mission_trh_CleanupRecentSecondaryList(ATH_ARG NOCONST(Entity)* pEnt)
{
	int i;
	for (i = eaSize(&pEnt->pPlayer->missionInfo->eaRecentSecondaryMissions)-1; i>=0; --i){
		NOCONST(CompletedMission) *pCompletedMission = pEnt->pPlayer->missionInfo->eaRecentSecondaryMissions[i];
		MissionDef *pDef = GET_REF(pCompletedMission->def);
		if (pCompletedMission->completedTime + missiondef_GetSecondaryCreditLockoutTime(pDef) < timeSecondsSince2000()){
			StructDestroyNoConst(parse_CompletedMission, pCompletedMission);
			eaRemove(&pEnt->pPlayer->missionInfo->eaRecentSecondaryMissions, i);
		}
	}
}

// This regrants any Titles that are supposed to be granted by Perks.  This fixes
// a case where some Titles were accidentally deleted, and it also lets us retroactively
// add Titles to Perks.
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Completedmissions, .pInventoryV2.Ppinventorybags, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype");
void mission_trh_FixupPerkTitles(ATH_ARG NOCONST(Entity)* pEnt, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	ItemDef **eaGrantedItems = NULL;

	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->missionInfo)){
		int i;
		for (i = 0; i < eaSize(&pEnt->pPlayer->missionInfo->completedMissions); i++){
			NOCONST(CompletedMission) *pMission = pEnt->pPlayer->missionInfo->completedMissions[i];
			MissionDef *pDef = GET_REF(pMission->def);
			if (pDef && pDef->missionType == MissionType_Perk && pDef->params){
				RewardTable *pRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnreturnRewardTableName);
				if (pRewardTable){
					rewardTable_GetAllGrantedItemsWithType(pRewardTable, kItemType_Title, &eaGrantedItems);
				}
			}
		}

		for (i = 0; i < eaSize(&eaGrantedItems); i++){
			if(!inv_trh_GetItemFromBagIDByName(ATR_EMPTY_ARGS, pEnt, InvBagIDs_Titles, eaGrantedItems[i]->pchName, pExtract)){
				Item *item = item_FromDefName(eaGrantedItems[i]->pchName);
				inv_AddItem(ATR_EMPTY_ARGS, pEnt, NULL, InvBagIDs_Titles, -1, item, eaGrantedItems[i]->pchName, ItemAdd_Silent, pReason, pExtract);
				StructDestroy(parse_Item,item);
			}
		}
	}

	eaDestroy(&eaGrantedItems);
}

// Removes expired MissionCooldown entries from pEnt's MissionCooldown list
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Missioninfo.Completedmissions");
void mission_trh_CleanupCooldownList(ATH_ARG NOCONST(Entity)* pEnt)
{
	if(NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->missionInfo) && NONNULL(pEnt->pPlayer->missionInfo->eaMissionCooldowns)) {
		int i;
		for (i=eaSize(&pEnt->pPlayer->missionInfo->eaMissionCooldowns)-1; i >= 0; i--)
		{
			// The mission is now in the "completed missions" earray and so this entry is unneccisary
			if(NONNULL(eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->completedMissions, pEnt->pPlayer->missionInfo->eaMissionCooldowns[i]->pchMissionName)))
			{
				StructDestroyNoConst(parse_MissionCooldown, eaRemove(&pEnt->pPlayer->missionInfo->eaMissionCooldowns, i));
				continue;
			} 
			else 
			{
				MissionDef* pDef = RefSystem_ReferentFromString(g_MissionDictionary, pEnt->pPlayer->missionInfo->eaMissionCooldowns[i]->pchMissionName);
				if(pDef)
				{

					CompletedMission *pCompletedMission = eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->completedMissions, pDef->name);
					MissionCooldown *pCooldown = eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->eaMissionCooldowns, pDef->name);

					const MissionCooldownInfo *minfo = mission_GetCooldownInfoInternal(pDef, pCooldown, pCompletedMission);

					// This mission is not in a cooldown block
					if
					(
						!minfo->bIsInCooldownBlock
					)
					{
						StructDestroyNoConst(parse_MissionCooldown, eaRemove(&pEnt->pPlayer->missionInfo->eaMissionCooldowns, i));
						continue;
					}
				}
			}
		}
	}
}

// Updates the start/end time for the cooldown entry with name, missionName, for ent.
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Eamissioncooldowns[AO], pPlayer.missionInfo.completedMissions[], pPlayer.missionInfo.eaMissionCooldowns[]");
bool mission_trh_UpdateCooldownForMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, const char* missionName, U32 startTime)
{
	MissionDef* missionDef = RefSystem_ReferentFromString(g_MissionDictionary, missionName);

	if(ISNULL(ent) || ISNULL(ent->pPlayer) || ISNULL(ent->pPlayer->missionInfo))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot update cooldown for mission: %s: Entity is invalid.", missionName);
		return false;
	}

	// If mission has repeat cooldown timer, persist the start and end time
	if(NONNULL(missionDef) && (missionDef->fRepeatCooldownHours || missionDef->fRepeatCooldownHoursFromStart))
	{
		// If the mission already has a completed mission, make the changes there
		NOCONST(CompletedMission)* pCompletedMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->completedMissions, missionName);
		U16 bHasRepeatCount = min(1, missionDef->iRepeatCooldownCount > 0);

		if(pCompletedMission)
		{
			NOCONST(CompletedMissionStats)* pStats = eaGet(&pCompletedMission->eaStats, 0);
			bool bInCooldown = false;
			if(pStats) {
			
				// if this has a repeat count then record see if count is updated or new times are added
				if(bHasRepeatCount > 0)
				{
					if(missionDef->fRepeatCooldownHours > 0) 
					{
						F32 fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(pStats->lastCompletedTime, missionDef->fRepeatCooldownHours, missionDef->bRepeatCooldownBlockTime);
						F32 fTimeHours = fTimeSec/3600.f;
						
						if (fTimeHours < missionDef->fRepeatCooldownHours)
						{
							// still in cooldown window
							bInCooldown = true;
						}
					}
					if(missionDef->fRepeatCooldownHoursFromStart > 0) 
					{
						F32 fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(pStats->lastStartTime, missionDef->fRepeatCooldownHoursFromStart, missionDef->bRepeatCooldownBlockTime);
						F32 fTimeHours = fTimeSec/3600.f;
						if (fTimeHours < missionDef->fRepeatCooldownHoursFromStart)
						{
							// in cooldown window
							bInCooldown = true;
						}
					}
				}
			
				if(bInCooldown)
				{
					// increase cooldown count
					++pCompletedMission->iRepeatCooldownCount;
				}
				else
				{
					// new cooldown count
					pStats->lastCompletedTime = timeSecondsSince2000();
					pStats->lastStartTime = startTime;
					pCompletedMission->iRepeatCooldownCount = bHasRepeatCount;
				}
				TRANSACTION_APPEND_LOG_SUCCESS("Completed Mission Stats updated due to dropped mission: %s for ent: %s", missionName, ent->debugName);
				return true;
			} else {
				pCompletedMission->completedTime = timeSecondsSince2000();
				pCompletedMission->startTime = startTime;
				TRANSACTION_APPEND_LOG_SUCCESS("Completed Mission updated due to dropped mission: %s for ent: %s", missionName, ent->debugName);
				return true;
			}
		}
		else
		{
			// Otherwise, make or edit the missionCooldown entry for the mission
			NOCONST(MissionCooldown)* missionCooldown = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->eaMissionCooldowns, missionName);
			if(!missionCooldown)
			{
				missionCooldown = StructCreateNoConst(parse_MissionCooldown);
				missionCooldown->pchMissionName = allocAddString(missionName);
				// new cooldown count
				missionCooldown->iRepeatCooldownCount = bHasRepeatCount;
				eaIndexedAdd(&ent->pPlayer->missionInfo->eaMissionCooldowns, missionCooldown);
				TRANSACTION_APPEND_LOG_SUCCESS("Mission Cooldown entry for mission: %s added to ent: %s", missionName, ent->debugName);
			}
			if(bHasRepeatCount > 0)
			{

				// dropped missions will have zero for times, need to make those non-zero
				if(missionCooldown->startTime == 0)
				{
					// we need a start and complete time
					// iRepeatCooldownCount count already set
					missionCooldown->completedTime = timeSecondsSince2000();
					missionCooldown->startTime = startTime;
				}
				else
				{
					// increase cooldown count
					++missionCooldown->iRepeatCooldownCount;
				}
			}
			else
			{
				missionCooldown->completedTime = timeSecondsSince2000();
				missionCooldown->startTime = startTime;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Mission Cooldown entry for mission: %s updated on ent: %s", missionName, ent->debugName);
			return true;
		}
	} 

	TRANSACTION_APPEND_LOG_FAILURE("Cannot update cooldown for a mission whose def has no cooldown set: %s", missionName);
	return false;
}

// return the value of a mission even counter
AUTO_TRANS_HELPER
ATR_LOCKS(pMission, "eaEventCounts[]");
S32 mission_trh_GetEventCount(ATH_ARG NOCONST(Mission)* pMission, const char* pchEventName)
{
	NOCONST(MissionEventContainer)* whichEvent = eaIndexedGetUsingString(&pMission->eaEventCounts, pchEventName);
	return whichEvent ? whichEvent->iEventCount : 0;
}

// ----------------------------------------------------------------------------------
// Callbacks that occur when a Mission is created or destroyed from a Transaction
// ----------------------------------------------------------------------------------

// These are called in gslTransactions.c

static void mission_tr_RemoveNonPersistedMission(Mission *mission)
{
	if (mission && mission->infoOwner)
	{
		Entity *pEnt = mission->infoOwner->parentEnt;
		eaFindAndRemove(&mission->infoOwner->eaNonPersistedMissions, mission);
		eaFindAndRemove(&mission->infoOwner->eaDiscoveredMissions, mission);
		mission_PreMissionDestroyDeinitRecursive(entGetPartitionIdx(pEnt), mission);
		StructDestroy(parse_Mission, mission);
	}
}

// Called on each Mission as it's created
void mission_tr_MissionPostCreateCB(Entity *pEnt, Mission *mission, Mission *parentMission)
{
	MissionInfo *info = mission_GetInfoFromPlayer(pEnt);
	MissionDef *pDef = NULL;
	Mission *pNonPersistedMission = NULL;

	// If the Mission was non-persisted before it was created, remove it from the non-persisted list
	if (!parentMission &&
		((pNonPersistedMission = eaIndexedGetUsingString(&info->eaNonPersistedMissions, mission->missionNameOrig)) ||
		 (pNonPersistedMission = eaIndexedGetUsingString(&info->eaDiscoveredMissions, mission->missionNameOrig))))
	{
		// Queue this with the Event system in case an Event is in the middle of being sent.  
		// Otherwise this non-persisted mission may miss the Event (if it's listening for it twice)
		eventtracker_QueueFinishedSendingCallback(mission_tr_RemoveNonPersistedMission, pNonPersistedMission, true);
	}

	// Initialize the mission
	mission_PostMissionCreateInit(entGetPartitionIdx(pEnt), mission, info, NULL, parentMission, true);

	// If the Mission requires lockout, add this player to the Lockout List
	pDef = mission_GetDef(mission);
	if (pDef && pDef->lockoutType)
	{
		missionlockout_AddPlayerToLockoutList(pDef, pEnt);
		if (pDef->lockoutType == MissionLockoutType_Team && !parentMission)
			mission_ShareMission(pEnt, pDef->name, true, false);
	}

	if (parentMission)
		mission_UpdateOpenChildren(parentMission);
	mission_UpdateTimestamp(pDef, mission);
}

// Called on the root Mission as it's destroyed
// In the future this may be called on every mission, instead of just the root
void mission_tr_MissionPreDestroyCB(Entity *pEnt, Mission *mission, Mission *parentMission)
{
	mission_PreMissionDestroyDeinitRecursive(entGetPartitionIdx(pEnt), mission);
}

// Called on the MissionInfo as it's created
void mission_tr_MissionInfoPostCreateCB(Entity *pEnt, MissionInfo *info)
{
	mission_PostEntityCreateMissionInit(pEnt, true);
}

// Called on the MissionInfo as it's destroyed.
void mission_tr_MissionInfoPreDestroyCB(Entity *pEnt, MissionInfo *info)
{
	mission_PreEntityDestroyMissionDeinit(entGetPartitionIdx(pEnt), pEnt);
}



// ----------------------------------------------------------------------------------
// Transaction to add a mission
// ----------------------------------------------------------------------------------

// This transaction helper creates and adds the mission, but doesn't run any of the actions or rewards
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Missions, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, pPlayer.missionInfo.eaMissionCooldowns[], .Pchar.Ilevelexp");
NOCONST(Mission)* mission_trh_AddMissionInternal(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, MissionDef *missionDef, const char* missionName, int iMissionLevel, const MissionOfferParams *pParams, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Mission)* newMission;
	NOCONST(Mission)* parentMission = NULL;
	MissionType eParentMissionType = MissionType_Normal;
	bool bFoundPrevUGCVersions = false;

	if (!missionDef)
		missionDef = missiondef_DefFromRefString(missionName);

	// Make sure the mission specified exists
	if (!missionDef){
		TRANSACTION_APPEND_LOG_FAILURE("No mission could be found matching: %s", missionName);
		return NULL;
	}

	// Check if there are any other missions for the same project (alters the success/error messages)
	if (missionDef->ugcProjectID) {
		FOR_EACH_IN_EARRAY(ent->pPlayer->missionInfo->missions, NOCONST(Mission), pActiveMission) {
			MissionDef *pActiveMissionDef = missiondef_DefFromRefString(pActiveMission->missionNameOrig);
			if (pActiveMissionDef && pActiveMissionDef != missionDef && pActiveMissionDef->ugcProjectID == missionDef->ugcProjectID) {
				bFoundPrevUGCVersions = true;
				break;
			}
		} FOR_EACH_END;
	}

	// Make sure the player doesn't already have this mission.  This has its own unique error message
	if (eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, missionName)){
		QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_FAIL, 0, 0, MISSION_ADD_ERROR_ALREADY_EXISTS_MESG, missionName, kNotifyType_MissionError);
		TRANSACTION_APPEND_LOG_FAILURE("Player already has a mission of name: %s", missionName);
		return NULL;
	}

	// Queue generic success/failure chat messages
	if (missionDef && missionDef->missionType != MissionType_Perk) {
		if (bFoundPrevUGCVersions) {
			QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_SUCCESS, 0, 0, MISSION_UGC_UPGRADED_MSG, missionName, kNotifyType_MissionStarted);
			QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_FAIL, 0, 0, MISSION_UGC_UPGRADE_ERROR_MSG, missionName, kNotifyType_MissionError);
		} else {
			QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_SUCCESS, 0, 0, MISSION_ADDED_MESG, missionName, kNotifyType_MissionStarted);
			QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_FAIL, 0, 0, MISSION_ADD_ERROR_MESG, missionName, kNotifyType_MissionError);
		}
		QueueRemoteCommand_UserExp_RemoteLogMissionGranted(ATR_RESULT_SUCCESS, 0, 0, missionName, NULL);
	}

	// Make sure the player hasn't completed this mission before, unless it's repeatable
	if (pParams && pParams->eCreditType != MissionCreditType_AlreadyCompleted && pParams->eCreditType != MissionCreditType_Flashback){
		if(!missionDef->repeatable && (eaIndexedGetUsingString(&ent->pPlayer->missionInfo->completedMissions, missionName))){
			TRANSACTION_APPEND_LOG_FAILURE("Player has already completed mission: %s", missionName);
			return NULL;
		}
	}

	// Make sure the mission isn't on cooldown
	if (missionDef->repeatable && (missionDef->fRepeatCooldownHours > 0 || missionDef->fRepeatCooldownHoursFromStart)){
		CompletedMission *pCompletedMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->completedMissions, missionName);
		F32 fTimeSec = 0;
		F32 fTimeHours = 0;
		U32 iRepeatCooldownCount = 0;

		if (pCompletedMission){
			if(missionDef->fRepeatCooldownHours > 0) 
			{
				fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(completedmission_GetLastCompletedTime(pCompletedMission), missionDef->fRepeatCooldownHours, missionDef->bRepeatCooldownBlockTime);
				iRepeatCooldownCount = completedmission_GetLastCooldownRepeatCount(pCompletedMission);
				fTimeHours = fTimeSec/3600.f;
				if (fTimeHours < missionDef->fRepeatCooldownHours && iRepeatCooldownCount >= missionDef->iRepeatCooldownCount){
					TRANSACTION_APPEND_LOG_FAILURE("Mission is still on cooldown: %s", missionName);
					return NULL;
				}
			}
			if(missionDef->fRepeatCooldownHoursFromStart > 0) 
			{
				fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(completedmission_GetLastStartedTime(pCompletedMission), missionDef->fRepeatCooldownHoursFromStart, missionDef->bRepeatCooldownBlockTime);
				fTimeHours = fTimeSec/3600.f;
				iRepeatCooldownCount = completedmission_GetLastCooldownRepeatCount(pCompletedMission);
				if (fTimeHours < missionDef->fRepeatCooldownHoursFromStart && iRepeatCooldownCount >= missionDef->iRepeatCooldownCount){
					TRANSACTION_APPEND_LOG_FAILURE("Mission is still on cooldown: %s", missionName);
					return NULL;
				}
			}
		} 
		else 
		{
			MissionCooldown *pCooldown = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->eaMissionCooldowns, missionName);
			if(pCooldown) 
			{
				if(missionDef->fRepeatCooldownHours > 0)
				{
					fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(pCooldown->completedTime, missionDef->fRepeatCooldownHours, missionDef->bRepeatCooldownBlockTime);
					fTimeHours = fTimeSec/3600.f;
					iRepeatCooldownCount = pCooldown->iRepeatCooldownCount;
					if (fTimeHours < missionDef->fRepeatCooldownHours && iRepeatCooldownCount >= missionDef->iRepeatCooldownCount){
						TRANSACTION_APPEND_LOG_FAILURE("Mission is still on cooldown: %s", missionName);
						return NULL;
					}
				}
				if(missionDef->fRepeatCooldownHoursFromStart > 0)
				{
					fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(pCooldown->startTime, missionDef->fRepeatCooldownHoursFromStart, missionDef->bRepeatCooldownBlockTime);
					fTimeHours = fTimeSec/3600.f;
					iRepeatCooldownCount = pCooldown->iRepeatCooldownCount;
					if (fTimeHours < missionDef->fRepeatCooldownHoursFromStart && iRepeatCooldownCount >= missionDef->iRepeatCooldownCount){
						TRANSACTION_APPEND_LOG_FAILURE("Mission is still on cooldown: %s", missionName);
						return NULL;
					}
				}
			}
		}
	}

	// Make sure the Mission is not an invalid type
	if (missiondef_GetType(missionDef) != MissionType_Normal && 
		missiondef_GetType(missionDef) != MissionType_Episode &&
		missiondef_GetType(missionDef) != MissionType_TourOfDuty &&
		missiondef_GetType(missionDef) != MissionType_Perk &&
		missiondef_GetType(missionDef) != MissionType_Nemesis &&
		missiondef_GetType(missionDef) != MissionType_NemesisArc &&
		missiondef_GetType(missionDef) != MissionType_NemesisSubArc &&
		missiondef_GetType(missionDef) != MissionType_AutoAvailable)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Mission has invalid type: %s", missionName);
		return NULL;
	}

	// Create and add to the mission list
	newMission = mission_CreateMission(PARTITION_IN_TRANSACTION, missionDef, iMissionLevel, ent->myContainerID);
	if (!newMission){
		TRANSACTION_APPEND_LOG_FAILURE("Mission could not be created: %s", missionName);
		return NULL;
	}

	if (!eaIndexedAdd(&ent->pPlayer->missionInfo->missions, newMission)) { 
		// Can get here if for some reason the player already has the mission
		// Including this even though we check above, just in case
		StructDestroyNoConst(parse_Mission, newMission);
		TRANSACTION_APPEND_LOG_FAILURE("Player already has a mission of name: %s", missionName);
		return NULL;
	}

	// Remove any missions for the same project, if on the player
	if(missionDef->ugcProjectID)
	{
		char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];
		resExtractNameSpace(missionName, pchNameSpace, pchBase);

		FOR_EACH_IN_EARRAY(ent->pPlayer->missionInfo->missions, NOCONST(Mission), pActiveMission) {
			MissionDef *pActiveMissionDef = missiondef_DefFromRefString(pActiveMission->missionNameOrig);
			if (pActiveMissionDef && pActiveMissionDef != missionDef && pActiveMissionDef->ugcProjectID == missionDef->ugcProjectID) {
				mission_trh_DropMission(ATR_PASS_ARGS, ent, pActiveMission->missionNameOrig, /*NULL, */true, pReason, pExtract);
			}
		} FOR_EACH_END;

		if(!isProductionEditMode())
			QueueRemoteCommand_mission_RequestUGCDataForMission(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), ent->myContainerID, pchNameSpace);
	}

	// If parent mission specified
	if (pParams && pParams->pchParentMission && pParams->pchParentMission[0]) {
		MissionDef *pRootDef = missiondef_DefFromRefString(pParams->pchParentMission);

		eParentMissionType = missiondef_GetType(pRootDef);
		parentMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pParams->pchParentMission);
		if (NONNULL(parentMission) && pParams->pchParentSubMission && pParams->pchParentSubMission[0])
			parentMission = eaIndexedGetUsingString(&parentMission->children, pParams->pchParentSubMission);

		if (NONNULL(parentMission)) {
			// If a parent is provided of type Episode, mark the child as hidden
			if (eParentMissionType == MissionType_Episode)
				newMission->bHiddenFullChild = true;

			// If a parent mission is provided, store the mission name on the parent 
			newMission->bChildFullMission = true;
		}
	}

	// Add any Mission Requests for this Mission
	mission_trh_AddMissionRequests(ATR_PASS_ARGS, ent->pPlayer->missionInfo, newMission, missionDef);

	// Switch to the assigned state and start the timer
	newMission->state = MissionState_InProgress;
	newMission->startTime = timeSecondsSince2000();

	// Apply other options from passed in parameters
	if (pParams){
		if (pParams->uTimerStartTime){
			newMission->timerStartTime = pParams->uTimerStartTime;
		}
		newMission->eCreditType = pParams->eCreditType;
	}

	// Send Mission state change Event
	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, missionDef->pchRefString, missiondef_GetType(missionDef), MissionState_Started, REF_STRING_FROM_HANDLE(missionDef->hCategory), true, /*UGCMissionData=*/NULL);
	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, missionDef->pchRefString, missiondef_GetType(missionDef), MissionState_InProgress, REF_STRING_FROM_HANDLE(missionDef->hCategory), true, /*UGCMissionData=*/NULL);

	return newMission;
}

// The main transaction helper to add a mission
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Pprogressioninfo.Eareplaydata, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_trh_AddMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, 
											  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
											  ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
											  ATH_ARG NOCONST(Guild) *pGuild, 
											  const char* missionName,
											  int iMissionLevel,
											  const MissionOfferParams *pParams,
											  WorldVariableArray* pMapVariables,
											  GameActionDoorDestinationVarArray* pDoorDestVarArray,
											  const char* pchInitMapVars,
											  const char* pchMapName,
											  const ItemChangeReason *pReason,
											  GameAccountDataExtract *pExtract)
{
	MissionDef* pMissionDef = missiondef_DefFromRefString(missionName);
	NOCONST(Mission)* pNewMission = NULL;	

	// Make sure the mission specified exists
	if (!pMissionDef)
		TRANSACTION_RETURN_LOG_FAILURE("No mission could be found matching: %s", missionName);

	// Add the mission
	if (!(pNewMission = mission_trh_AddMissionInternal(ATR_PASS_ARGS, ent, pMissionDef, missionName, iMissionLevel, pParams, pReason, pExtract))){
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// For Flashback Missions, create a Door Key
	if (pParams && !pParams->pchParentMission && pParams->eCreditType == MissionCreditType_Flashback){
		if (inv_ent_tr_AddDoorKeyItemForFlashbackMission(ATR_PASS_ARGS, ent, pMissionDef, pNewMission->iLevel, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS){
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	// Grant sub-missions and execute other actions
	if (gameaction_trh_RunActionsSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, pNewMission, pMissionDef, &pMissionDef->ppOnStartActions, pMapVariables, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s", missionName);

	// Grant Rewards
	if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ pMissionDef, MissionState_InProgress, NULL, NULL, 0, pNewMission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
		return TRANSACTION_OUTCOME_FAILURE;

	// Set progression if required
	if (progression_trh_ExecutePostMissionAcceptTasks(ATR_PASS_ARGS, ent, pMissionDef) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Added mission: %s", missionName);
}

// This adds a mission, but doesn't lock any extra data.  This will fail if something else needed to be locked.
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Missions, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, pPlayer.missionInfo.eaMissionCooldowns[], .Pchar.Ilevelexp, \
.Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode");
enumTransactionOutcome mission_trh_AddMissionNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, const char* missionName, int iMissionLevel, const MissionOfferParams *pParams, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	MissionDef* pMissionDef = missiondef_DefFromRefString(missionName);
	NOCONST(Mission)* pNewMission = NULL;

	// Make sure the mission specified exists
	if (!pMissionDef)
		TRANSACTION_RETURN_LOG_FAILURE("No mission could be found matching: %s", missionName);

	// Add the mission
	if (!(pNewMission = mission_trh_AddMissionInternal(ATR_PASS_ARGS, ent, pMissionDef, missionName, iMissionLevel, pParams, pReason, pExtract))){
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Grant sub-missions and execute other actions
	if (gameaction_trh_RunActionsSubMissionsNoLocking(ATR_PASS_ARGS, ent, pNewMission, pMissionDef, pMissionDef, &pMissionDef->ppOnStartActions) != TRANSACTION_OUTCOME_SUCCESS)
		TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s", missionName);

	// Make sure there are no rewards
	if (missiondef_HasOnStartRewards(pMissionDef)){
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a Mission transaction, but Inventory must be locked!");
	}

	// Set progression if required
	if (progression_trh_ExecutePostMissionAcceptTasks(ATR_PASS_ARGS, ent, pMissionDef) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Added mission: %s", missionName);
}

AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Pprogressioninfo.Eareplaydata, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_AddMission(ATR_ARGS, NOCONST(Entity)* ent, 
											 ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
											 CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
											 NOCONST(Guild)* pGuild, 
											 const char* missionName,
											 int iMissionLevel,
											 const MissionOfferParams *pParams,
											 WorldVariableArray* pMapVariables,
											 GameActionDoorDestinationVarArray* pDoorDestVarArray,
											 const char* pchInitMapVars,
											 const char* pchMapName,
											 const ItemChangeReason *pReason,
											 GameAccountDataExtract *pExtract)
{
	return mission_trh_AddMission(ATR_PASS_ARGS, ent, eaPets, eaVarContainer, pGuild, missionName, iMissionLevel, pParams, pMapVariables, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Missions, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, pPlayer.missionInfo.eaMissionCooldowns[], .Pchar.Ilevelexp, \
			   .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode");
enumTransactionOutcome mission_tr_AddMissionNoLocking(ATR_ARGS, NOCONST(Entity)* ent, const char* missionName, int iMissionLevel, const MissionOfferParams *pParams, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return mission_trh_AddMissionNoLocking(ATR_PASS_ARGS, ent, missionName, iMissionLevel, pParams, pReason, pExtract);
}

// Call this to initiate the transaction
void missioninfo_AddMission(int iPartitionIdx, MissionInfo* info, MissionDef* missionDef, const MissionOfferParams *pParams, TransactionReturnCallback cb, UserData data)
{
	TransactionReturnVal* returnVal = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (info->parentEnt && missionDef->name)
	{
		// Perks are granted as non-persisted objects initially to reduce database storage
		if (missionDef->missionType == MissionType_Perk)
		{
			mission_AddNonPersistedMission(iPartitionIdx, info, missionDef);
		}
		else if (missionDef->missionType == MissionType_Normal || 
				missionDef->missionType == MissionType_Nemesis || missionDef->missionType == MissionType_NemesisArc ||
				missionDef->missionType == MissionType_NemesisSubArc ||
				missionDef->missionType == MissionType_Episode || missionDef->missionType == MissionType_TourOfDuty ||
				missionDef->missionType == MissionType_AutoAvailable)
		{
			GameAccountDataExtract *pExtract;
			bool bMustLockShardVars;
			bool bMustLockGuildLog;
			Entity *pEnt;
			int iMissionLevel;
			ItemChangeReason reason = {0};

			// Check for Mission Lockout
			if (missionDef->lockoutType != MissionLockoutType_None && missionlockout_MissionLockoutInProgress(iPartitionIdx, missionDef) && !missionlockout_PlayerInLockoutList(missionDef, info->parentEnt, iPartitionIdx)){
				// Player is "locked out" of the mission, so do nothing 
				// (this should only occur if two people clicked Accept on an Escort mission simultaneously or something)
				missioninfo_AddMission_Fail(cb, data);
				PERFINFO_AUTO_STOP();
				return;
			}

			// Check whether player has reached their maximum mission limit
			// !!! This is intentionally NOT transaction-safe, because checking this inside a transaction has
			//     some gnarly locking implications, and I don't think it's critical.  -BF
			if (gConf.iMaxActiveMissions && missiondef_CountsTowardsMaxActive(missionDef) && mission_GetNumMissionsTowardsMaxActive(info) >= gConf.iMaxActiveMissions){
				// Player has too many missions; send them a message and fail.			
				notify_SendMissionNotification(info->parentEnt, NULL, missionDef, MISSION_JOURNAL_FULL_MESG, kNotifyType_MissionJournalFull);
				missioninfo_AddMission_Fail(cb, data);
				PERFINFO_AUTO_STOP();
				return;
			}

			returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-Add", info->parentEnt, cb, data);

			pEnt = info->parentEnt;
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			bMustLockShardVars = missiondef_MustLockShardVariablesOnStart(missionDef, missionDef);
			bMustLockGuildLog = missiondef_MustLockGuildActivityLogOnStart(((NONNULL(pEnt->pPlayer)) && NONNULL(pEnt->pPlayer->pGuild)), missionDef, missionDef);
			iMissionLevel = missiondef_CalculateLevel(iPartitionIdx, entity_GetSavedExpLevel(pEnt), missionDef);
			
			inv_FillItemChangeReason(&reason, info->parentEnt, "Mission:AddMission", missionDef->name);

			// Determine which version of the transaction to use based on the locking requirements
			if (missiondef_MustLockInventoryOnStart(missionDef, missionDef) 
				|| missiondef_MustLockMissionsOnStart(missionDef, missionDef)
				|| missiondef_MustLockNemesisOnStart(missionDef, missionDef)
				|| missiondef_MustLockNPCEMailOnStart(missionDef, missionDef)
				|| missiondef_MustLockActivityLogOnStart(missionDef, missionDef)
				|| bMustLockShardVars
				|| bMustLockGuildLog
				|| (pParams && pParams->eCreditType == MissionCreditType_Flashback)){

					GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, missionDef->ppOnStartActions);
					WorldVariableArray* pMapVariableArray = NULL;
					const char* pchInitMapVars = NULL;
					const char* pchMapName = NULL;
					U32* eaPets = NULL;
					U32* eaEmptyList = NULL;

					if (gameaction_CountActionsByType(missionDef->ppOnStartActions, WorldGameActionType_GiveDoorKeyItem) > 0)
					{
						pMapVariableArray = mapVariable_GetAllAsWorldVarArray(iPartitionIdx);
						pchInitMapVars = partition_MapVariablesFromIdx(iPartitionIdx);
						pchMapName = zmapInfoGetPublicName(NULL);
					}

					// Don't pass in pets unless unique items are possible
					if (missiondef_HasUniqueItemsInRewardsForState(missionDef, MissionState_InProgress, pParams ? pParams->eCreditType : MissionCreditType_Primary)) {
						ea32Create(&eaPets);
						Entity_GetPetIDList(info->parentEnt, &eaPets);
					}

					AutoTrans_mission_tr_AddMission(returnVal, GetAppGlobalType(), 
						entGetType(info->parentEnt), entGetContainerID(info->parentEnt), 
						GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
						GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
						GLOBALTYPE_GUILD, bMustLockGuildLog ? info->parentEnt->pPlayer->pGuild->iGuildID : 0, 
						missionDef->name, iMissionLevel, pParams, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, 
						&reason, pExtract);
					ea32Destroy(&eaPets);

					StructDestroy(parse_WorldVariableArray, pMapVariableArray);
					StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);
			} else {
				AutoTrans_mission_tr_AddMissionNoLocking(returnVal, GetAppGlobalType(), 
						entGetType(info->parentEnt), entGetContainerID(info->parentEnt), 
						missionDef->name, iMissionLevel, pParams, &reason, pExtract);
			}

			if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS && entGetPrimaryMission(pEnt)==NULL)
			{
				mission_SetPrimaryMission(pEnt,missionDef->name);
			}
		}
		else
		{
			Errorf("Error: Can't add mission %s, unknown Mission type!", missionDef->pchRefString);
			missioninfo_AddMission_Fail(cb, data);
		}
	}

	PERFINFO_AUTO_STOP();
}

void missioninfo_AddMissionByName(int iPartitionIdx, MissionInfo* info, const char* missionName, TransactionReturnCallback cb, UserData data)
{
	MissionDef* missionDef = RefSystem_ReferentFromString(g_MissionDictionary, missionName);
	if (missionDef)
	{
		missioninfo_AddMission(iPartitionIdx, info, missionDef, NULL, cb, data);
	}
	else if (ResDbWouldTryToProvideResource(RESOURCETYPE_MISSIONDEF, missionName))
	{
		// ???
		MissionAdd_ResourceDBDeferred(info->parentEnt, missionName, cb, data);
	}
	else
	{
		AssertOrAlertWarning("ADD_MISSION_BY_NAME_INVALID",
			"EntityPlayer (%u) requested mission (%s) to be added, however mission name is not known and resource DB will not be providing it.",
			entGetContainerID(info->parentEnt), NULL_TO_EMPTY(missionName));

		missioninfo_AddMission_Fail(cb, data);
	}
}

void missioninfo_ClearMissionToGrant(Entity* pEnt)
{
	pEnt->astrMissionToGrant = NULL;
}

// change the cooldown of a mission if the mission is in a cooldown period
AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pPlayer.missionInfo.completedMissions[], pPlayer.missionInfo.eaMissionCooldowns[]");
enumTransactionOutcome mission_tr_ChangeCooldown(ATR_ARGS, NOCONST(Entity)* pEnt, 
	const char *pcMissionName, U32 bAdjustTime, U32 uTm, U32 bAdjustCount, U32 uCount)
{
	MissionDef* missionDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer) || ISNULL(pEnt->pPlayer->missionInfo))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Cannot update cooldown for mission: %s: Entity is invalid.", pcMissionName);
	}

	// If mission has repeat cooldown timer, persist the start and end time
	if(NONNULL(missionDef) && (missionDef->fRepeatCooldownHours || missionDef->fRepeatCooldownHoursFromStart))
	{
		// If the mission already has a completed mission, make the changes there
		NOCONST(CompletedMission)* pCompletedMission = eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->completedMissions, pcMissionName);
		U16 bHasRepeatCount = min(1, missionDef->iRepeatCooldownCount > 0);

		if(NONNULL(pCompletedMission))
		{
			NOCONST(CompletedMissionStats)* pStats = eaGet(&pCompletedMission->eaStats, 0);
			bool bInCooldown = false;
			if(pStats)
			{
				// if this has a repeat count then record see if count is updated or new times are added
				if(missionDef->fRepeatCooldownHours > 0) 
				{
					F32 fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(pStats->lastCompletedTime, missionDef->fRepeatCooldownHours, missionDef->bRepeatCooldownBlockTime);
					F32 fTimeHours = fTimeSec/3600.f;
					if (fTimeHours < missionDef->fRepeatCooldownHours)
					{
						// still in cooldown window
						bInCooldown = true;
					}
				}
				if(missionDef->fRepeatCooldownHoursFromStart > 0) 
				{
					F32 fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(pStats->lastStartTime, missionDef->fRepeatCooldownHoursFromStart, missionDef->bRepeatCooldownBlockTime);
					F32 fTimeHours = fTimeSec/3600.f;
					if (fTimeHours < missionDef->fRepeatCooldownHoursFromStart)
					{
						// in cooldown window
						bInCooldown = true;
					}
				}

				if(bInCooldown)
				{
					// increase cooldown count
					if(bAdjustCount && missionDef->iRepeatCooldownCount > 0)
					{
						pCompletedMission->iRepeatCooldownCount = uCount;
					}
					
					if(bAdjustTime)
					{
						pStats->lastCompletedTime = uTm;
						pStats->lastStartTime = uTm;
					}
				}
				
				TRANSACTION_RETURN_LOG_SUCCESS("Changed cooldown for mission: %s for ent: %s", pcMissionName, pEnt->debugName);
			}
		}
		else
		{
			// Otherwise, make or edit the missionCooldown entry for the mission
			NOCONST(MissionCooldown)* missionCooldown = eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->eaMissionCooldowns, pcMissionName);
			if(NONNULL(missionCooldown))
			{
				bool bInCooldown = false;
				// if this has a repeat count then record see if count is updated or new times are added
				if(missionDef->fRepeatCooldownHours > 0) 
				{
					F32 fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(missionCooldown->completedTime, missionDef->fRepeatCooldownHours, missionDef->bRepeatCooldownBlockTime);
					F32 fTimeHours = fTimeSec/3600.f;
					if (fTimeHours < missionDef->fRepeatCooldownHours)
					{
						// still in cooldown window
						bInCooldown = true;
					}
				}
				if(missionDef->fRepeatCooldownHoursFromStart > 0) 
				{
					F32 fTimeSec = timeSecondsSince2000() - mission_GetCooldownBlockStart(missionCooldown->startTime, missionDef->fRepeatCooldownHoursFromStart, missionDef->bRepeatCooldownBlockTime);
					F32 fTimeHours = fTimeSec/3600.f;
					if (fTimeHours < missionDef->fRepeatCooldownHoursFromStart)
					{
						// in cooldown window
						bInCooldown = true;
					}
				}

				if(bInCooldown)
				{
					// increase cooldown count
					if(bAdjustCount && missionDef->iRepeatCooldownCount > 0)
					{
						missionCooldown->iRepeatCooldownCount = uCount;
					}

					if(bAdjustTime)
					{
						missionCooldown->completedTime = uTm;
						missionCooldown->startTime = uTm;
					}
				}

			}
			TRANSACTION_RETURN_LOG_SUCCESS("Changed cooldown for mission: %s for ent: %s", pcMissionName, pEnt->debugName);
		}
	} 
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("Failed to change cooldown for mission: %s as this mission does not have a cooldown", pcMissionName);
	}
	
	TRANSACTION_RETURN_LOG_FAILURE("Failed to change cooldown for mission %s:", pcMissionName);
}

// change the cooldown for a mission
void missioninfo_ChangeCooldown(Entity *pEnt, const char *pcMissionName, bool bAdjustTime, U32 uTm, bool bAdjustCount, U32 uCount)
{
	if(pEnt && pcMissionName && (bAdjustTime || bAdjustCount))
	{
		TransactionReturnVal* returnVal = NULL;
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-Changecooldown", pEnt, NULL, NULL);
		
		AutoTrans_mission_tr_ChangeCooldown(returnVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pcMissionName, bAdjustTime, uTm, bAdjustCount, uCount);
	}
}

// ----------------------------------------------------------------------------------
// Transaction Helper to add a child mission
// ----------------------------------------------------------------------------------

AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Missions, .Pplayer.Pugckillcreditlimit, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme")
	ATR_LOCKS(parentMission, ".Starttime, .Eamissionvariables, .Children, .Missionnameorig");
enumTransactionOutcome mission_trh_AddChildMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, 
												   ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
												   ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, 
												   ATH_ARG NOCONST(Guild)* pGuild, 
												   ATH_ARG NOCONST(Mission)* parentMission, 
												   MissionDef *pDef,
												   WorldVariableArray* pMapVariableArray,
												   GameActionDoorDestinationVarArray* pDoorDestVarArray,
												   const char* pchInitMapVars,
												   const char* pchMapName,
												   const ItemChangeReason *pReason,
												   GameAccountDataExtract *pExtract)
{
	NOCONST(Mission)* newMission;
	int i;

	// Make sure the player doesn't already have this child mission
	if (mission_trh_GetChildMission(parentMission, pDef))
		TRANSACTION_RETURN_LOG_SUCCESS("Player already has a mission: %s", pDef->pchRefString);

	// Create and add to the parent's child list
	newMission = mission_CreateMission(PARTITION_IN_TRANSACTION, pDef, 0, ent->myContainerID);
	if (!newMission)
		TRANSACTION_RETURN_LOG_FAILURE("Mission could not be created: %s", pDef->pchRefString);

	// Because field is NO_INDEXED_PREALLOC we need to do this manually
	eaIndexedEnable((Mission***)&parentMission->children, parse_Mission);

	newMission->displayOrder = eaSize(&parentMission->children);
	if (!eaIndexedAdd(&parentMission->children, newMission))
	{
		// Can get here if the child mission already exists
		// Doing this even though it is checked above, just in case
		StructDestroyNoConst(parse_Mission, newMission);
		TRANSACTION_RETURN_LOG_SUCCESS("Player already has a mission: %s", pDef->pchRefString);
	}

	// Add root mission (i.e. parent) variables to child (if not already specified/overridden on child def) so
	// child can reference variables on parents
	if (eaSize(&parentMission->eaMissionVariables) > 0) {
		// Because field is NO_INDEXED_PREALLOC we need to do this manually
		eaIndexedEnable((WorldVariableContainer***)&newMission->eaMissionVariables, parse_WorldVariableContainer);

		for (i = 0; i < eaSize(&parentMission->eaMissionVariables); i++)
			eaIndexedAdd(&newMission->eaMissionVariables, StructCloneNoConst(parse_WorldVariableContainer, parentMission->eaMissionVariables[i]));
	}
	
	// Switch to the assigned state and start the timer
	newMission->state = MissionState_InProgress;
	newMission->startTime = timeSecondsSince2000();

	// Default to 0 if the start time is the same as the parent's
	if (newMission->startTime == parentMission->startTime)
		newMission->startTime = 0;

	// Add any Mission Requests for this Mission
	mission_trh_AddMissionRequests(ATR_PASS_ARGS, ent->pPlayer->missionInfo, newMission, pDef);

	// Send Mission state change Event
	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, pDef->pchRefString, missiondef_GetType(pDef), MissionState_InProgress, REF_STRING_FROM_HANDLE(pDef->hCategory), false, /*UGCMissionData=*/NULL);

	// Log mission progress for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionGranted(ATR_RESULT_SUCCESS, 0, 0, parentMission->missionNameOrig, pDef->pchRefString);

	// Run Actions
	if (gameaction_trh_RunActionsSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, newMission, pDef, &pDef->ppOnStartActions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s", pDef->pchRefString);

	// Grant Rewards
	if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ pDef, MissionState_InProgress, NULL, NULL, 0, newMission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
		return TRANSACTION_OUTCOME_FAILURE;

	TRANSACTION_RETURN_LOG_SUCCESS("Added mission: %s", pDef->pchRefString);
}

AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Eamissionrequests[AO]")
ATR_LOCKS(parentMission, ".Starttime, .Eamissionvariables, .Children");
enumTransactionOutcome mission_trh_AddChildMissionNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* parentMission, MissionDef *pDef, MissionDef *pRootDef)
{
	NOCONST(Mission)* newMission;
	int i;

	// Make sure the player doesn't already have this child mission
	if (mission_trh_GetChildMission(parentMission, pDef))
		TRANSACTION_RETURN_LOG_SUCCESS("Player already has a mission: %s", pDef->pchRefString);

	// Create and add to the parent's child list
	newMission = mission_CreateMission(PARTITION_IN_TRANSACTION, pDef, missiondef_CalculateLevel(PARTITION_IN_TRANSACTION, 0, pDef), ent->myContainerID);
	if (!newMission)
		TRANSACTION_RETURN_LOG_FAILURE("Mission could not be created: %s", pDef->pchRefString);

	// Because field is NO_INDEXED_PREALLOC we need to do this manually
	eaIndexedEnable((Mission***)&parentMission->children, parse_Mission);

	newMission->displayOrder = eaSize(&parentMission->children);
	if (!eaIndexedAdd(&parentMission->children, newMission))
	{
		// Can get here if the child mission already exists
		// Doing this even though it is checked above, just in case
		StructDestroyNoConst(parse_Mission, newMission);
		TRANSACTION_RETURN_LOG_SUCCESS("Player already has a mission: %s", pDef->pchRefString);
	}

	// Add root mission (i.e. parent) variables to child (if not already specified/overridden on child def) so
	// child can reference variables on parents
	if (eaSize(&parentMission->eaMissionVariables) > 0) {
		// Because field is NO_INDEXED_PREALLOC we need to do this manually
		eaIndexedEnable((WorldVariableContainer***)&newMission->eaMissionVariables, parse_WorldVariableContainer);

		for (i = 0; i < eaSize(&parentMission->eaMissionVariables); i++)
			eaIndexedAdd(&newMission->eaMissionVariables, StructCloneNoConst(parse_WorldVariableContainer, parentMission->eaMissionVariables[i]));
	}
	
	// Switch to the assigned state and start the timer
	newMission->state = MissionState_InProgress;
	newMission->startTime = timeSecondsSince2000();

	// Default to 0 if the start time is the same as the parent's
	if (newMission->startTime == parentMission->startTime)
		newMission->startTime = 0;

	// Send Mission state change Event
	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, pDef->pchRefString, missiondef_GetType(pRootDef), MissionState_InProgress, REF_STRING_FROM_HANDLE(pRootDef->hCategory), false, /*UGCMissionData=*/NULL);

	// Add any Mission Requests for this Mission
	mission_trh_AddMissionRequests(ATR_PASS_ARGS, ent->pPlayer->missionInfo, newMission, pDef);

	// Run Actions
	if (gameaction_trh_RunActionsSubMissionsNoLocking(ATR_PASS_ARGS, ent, newMission, pDef, pRootDef, &pDef->ppOnStartActions) != TRANSACTION_OUTCOME_SUCCESS)
		TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s", pDef->pchRefString);

	TRANSACTION_RETURN_LOG_SUCCESS("Added mission: %s", pDef->pchRefString);
}


// ----------------------------------------------------------------------------------
// Transaction to turn in a mission
// ----------------------------------------------------------------------------------

// Helper to run actions on turn-in
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Playertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns")
ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme")
ATR_LOCKS(mission, ".Children, .Permacomplete, .Eamissionvariables");
enumTransactionOutcome mission_trh_RunTurnInActionsRecursive(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent,
															 ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
															 ATH_ARG NOCONST(Guild)* pGuild,
															 ATH_ARG NOCONST(Mission)* mission,
															 MissionDef* pDef,
															 MissionDef *pRootDef,
															 const ItemChangeReason *pReason,
															 GameAccountDataExtract *pExtract)
{
	WorldVariableArray *pVariableArray = NULL;
	int i, n = eaSize(&mission->children);

	for (i = 0; i < n; i++)
	{
		if (mission->children[i]->state == MissionState_Succeeded)
		{
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, mission->children[i]->missionNameOrig);
			if (!pChildDef)
				TRANSACTION_RETURN_LOG_FAILURE("Could not find MissionDef for child mission: %s", mission->children[i]->missionNameOrig);
			if (mission_trh_RunTurnInActionsRecursive(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, mission->children[i], pChildDef, pRootDef, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
				return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	pVariableArray = mission_trh_CreateVariableArray(mission);
	if (gameaction_trh_RunActionsLockAll(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, &pDef->ppOnReturnActions, pVariableArray, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
	{
		// Hack so that Missions completed with debug commands can be turned in
		// even if the player doesn't have required turn-in items
		StructDestroy(parse_WorldVariableArray, pVariableArray);
		if (mission->permaComplete)
			return TRANSACTION_OUTCOME_SUCCESS;

		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroy(parse_WorldVariableArray, pVariableArray);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Helper to grant rewards on turn-in
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Psaved.Ppallowedcritterpets, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(mission, ".Children, .Ilevel, .Pugcmissiondata");
enumTransactionOutcome mission_trh_GrantTurnInRewardsRecursive(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, 
															   ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
															   ATH_ARG NOCONST(Mission)* mission, 
															   MissionDef* pDef, 
															   MissionDef *pRootDef, 
															   U32 uTimesCompleted, 
															   ContactRewardChoices* rewardChoices, 
															   MissionCreditType eCreditType, 
															   U32 uiMissionStartTime, 
															   U32 uiTurnInTime,
															   const ItemChangeReason *pReason,
															   GameAccountDataExtract *pExtract)
{
	int i, n = eaSize(&mission->children);

	for (i = 0; i < n; i++)
	{
		if (mission->children[i]->state == MissionState_Succeeded)
		{
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, mission->children[i]->missionNameOrig);
			if (!pChildDef)
				TRANSACTION_RETURN_LOG_FAILURE("Could not find MissionDef for child mission: %s", mission->children[i]->missionNameOrig);
			// Grant rewards for submission, but note that it's impossible to choose rewards for a submission.  Only the parent mission can have choosable rewards
			if (mission_trh_GrantTurnInRewardsRecursive(ATR_PASS_ARGS, ent, eaPets, mission->children[i], /*NULL,*/ pChildDef, pRootDef, uTimesCompleted, NULL, eCreditType, uiMissionStartTime, uiTurnInTime, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
				return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	// Grant Rewards
	{
		U32 seed = mission_GetRewardSeedEx(ent->myContainerID, pDef, uiMissionStartTime, uTimesCompleted);

		int iLevel =  (isProductionEditMode() || mission->pUGCMissionData) ? entity_trh_GetSavedExpLevel(ent) : mission->iLevel;
		bool bUGCProject = isProductionEditMode() ? !!pRootDef->ugcProjectID : !!mission->pUGCMissionData;
		bool bCreatedByAccount = mission->pUGCMissionData ? mission->pUGCMissionData->bCreatedByAccount : false;
		bool bQualifyForUGCFeaturedRewards =
			mission->pUGCMissionData ? (mission->pUGCMissionData->bProjectIsFeatured || (gConf.bUGCPreviouslyFeaturedMissionsQualifyForRewards && mission->pUGCMissionData->bProjectWasFeatured)) : false;
		bool bStatsQualifyForUGCRewards = isProductionEditMode() ? true : (mission->pUGCMissionData ? mission->pUGCMissionData->bStatsQualifyForUGCRewards : false);
		U32 iNumberOfPlays = isProductionEditMode() ? 20 : (mission->pUGCMissionData ? mission->pUGCMissionData->iNumberOfPlays : 0);
		F32 fAverageDurationInMinutes = isProductionEditMode() ? 30 : (mission->pUGCMissionData ? mission->pUGCMissionData->fAverageDurationInMinutes : 0);

		if(gConf.bUGCAveragePlayingTimeUsesCustomMapPlayingTime && mission->pUGCMissionData && 0 == iNumberOfPlays)
			fAverageDurationInMinutes = mission->pUGCMissionData->fPlayingTimeInMinutes;

		if (TRANSACTION_OUTCOME_SUCCESS != mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, pDef, MissionState_TurnedIn, rewardChoices, &seed, eCreditType, iLevel, uiMissionStartTime, uiTurnInTime, false, pReason, pExtract,
				bUGCProject, bCreatedByAccount, bStatsQualifyForUGCRewards, bQualifyForUGCFeaturedRewards, fAverageDurationInMinutes))
			return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Helper to remove completed full missions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Completedmissions")
ATR_LOCKS(mission, ".Childfullmissions, .Children");
void mission_trh_RemoveCompletedFullChildren(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* mission)
{
	int i;

	// Remove completed child full missions
	for(i=eaSize(&mission->childFullMissions)-1; i>=0; --i) 
	{
		NOCONST(CompletedMission) *childCompleted = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->completedMissions, mission->childFullMissions[i]);
		if (childCompleted)
		{
			eaFindAndRemove(&ent->pPlayer->missionInfo->completedMissions, childCompleted);
			StructDestroyNoConst(parse_CompletedMission, childCompleted);
		}
	}

	// Recurse on children
	for(i=eaSize(&mission->children)-1; i>=0; --i)
	{
		mission_trh_RemoveCompletedFullChildren(ATR_PASS_ARGS, ent, mission->children[i]);
	}
}

// The start of the turn in mission transaction (split up to avoid code duplication after duplicating the turn-in mission transaction to reduce locking)
AUTO_TRANS_HELPER;
static bool mission_trh_TurnInMission_Begin(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent,
 														ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, 
 														ATH_ARG NOCONST(Guild) *pGuild, 
 														char* missionName,
														MissionDef** ppMissionDef,
														NOCONST(Mission)** ppMission,
														U32* puNemesisID,
														U32* puTimesCompleted,
														const ItemChangeReason *pReason,
														GameAccountDataExtract *pExtract)
{
	MissionDef* missionDef = NULL;
	NOCONST(Mission)* mission = NULL;
	int i;

	// Make sure the MissionDef exists
	if (!(missionDef = RefSystem_ReferentFromString(g_MissionDictionary, missionName))) 
	{
		TRANSACTION_APPEND_LOG_FAILURE("No MissionDef could be found for mission: %s", missionName);
		return false;
	}

	mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, missionName);

	// Queue success/failure chat messages
	if (missionDef && missionDef->missionType != MissionType_Perk) {
		if(missionDef->ugcProjectID && mission)
			QueueRemoteCommand_notify_SetUGCMissionInfoReviewData(ATR_RESULT_SUCCESS, 0, 0, missionName, missionDef->ugcProjectID,
				mission->pUGCMissionData ? mission->pUGCMissionData->bPlayingAsBetaReviewer : false);

		QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_SUCCESS, 0, 0, MISSION_RETURNED_MESG, missionName, kNotifyType_MissionTurnIn);
		QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_FAIL, 0, 0, MISSION_RETURN_ERROR_MESG, missionName, kNotifyType_MissionError);
		QueueRemoteCommand_UserExp_RemoteLogMissionComplete(ATR_RESULT_SUCCESS, 0, 0, missionName, NULL, false);
	} else if (missionDef && missionDef->missionType == MissionType_Perk) {

		if (missionDef->bIsTutorialPerk)
		{
			Message *pMessage = GET_REF(missionDef->summaryMsg.hMessage);
			QueueRemoteCommand_notify_RemoteSendTutorialNotification(ATR_RESULT_SUCCESS, 0, 0, pMessage->pcMessageKey, missionDef->eTutorialScreenRegion, missionDef->pchIconName, kNotifyType_Tip_General);
		}
		else
		{
			// If this is a Perk, trigger a "Perk Completed" message
			QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_SUCCESS, 0, 0, MISSION_PERK_COMPLETED_MSG, missionName, kNotifyType_PerkCompleted);
			QueueRemoteCommand_UserExp_RemoteLogMissionComplete(ATR_RESULT_SUCCESS, 0, 0, missionName, NULL, false);
		}

		QueueRemoteCommand_player_RemoteSendUpdatedClassList(ATR_RESULT_SUCCESS, 0, 0);

		// Update Perk Points
		QueueRemoteCommand_mission_RemoteFlagPerkPointRefresh(ATR_RESULT_SUCCESS, 0, 0);
	}

	// Make sure the player has this mission to be removed
	if (!mission)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Player does not have a mission of name: %s", missionName);
		return false;
	}

	if (mission->state != MissionState_Succeeded)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot turn in uncompleted mission: %s", missionName);
		return false;
	}

	// If this is a nemesis mission, get the ID of the player's primary nemesis (do this now, because TurnInActions
	// may remove the nemesis)
	if((missionDef->missionType == MissionType_Nemesis || missionDef->missionType == MissionType_NemesisArc || missionDef->missionType == MissionType_NemesisSubArc) && NONNULL(ent->pPlayer))
	{
		bool nemesisFound = false;

		// Find the player's primary nemesis
		for (i = eaSize(&ent->pPlayer->nemesisInfo.eaNemesisStates)-1; i >= 0; i--) {
			if (ent->pPlayer->nemesisInfo.eaNemesisStates[i]->eState == NemesisState_Primary) {
				nemesisFound = true;
				(*puNemesisID) = ent->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID;
				break;
			}
		}
		if(!nemesisFound)
		{
			Errorf("Player completed nemesis mission %s, but has no primary nemesis.  Can't store nemesis on completed mission.", missionDef->name);
		}
	}

	// Run all Mission Actions that should occur on turn-in
	if (mission_trh_RunTurnInActionsRecursive(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, mission, missionDef, missionDef, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Mission could not perform all actions: %s", missionName);
		return false;
	}

	// Take all Mission Items (including grant items)
	if (!inv_ent_trh_RemoveMissionItems(ATR_PASS_ARGS, ent, missionName, true, pReason, pExtract))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Mission could not remove all Mission Items: %s", missionName);
		return false;
	}

	// Grant all Turn-In rewards
	(*puTimesCompleted) = missioninfo_trh_GetNumTimesCompleted(ent->pPlayer->missionInfo, missionName);

	//Set return values
	(*ppMissionDef) = missionDef;
	(*ppMission) = mission;

	return true;
}

// The start of the turn in mission transaction (split up to avoid code duplication after duplicating the turn-in mission transaction to reduce locking)
AUTO_TRANS_HELPER;
bool mission_trh_TurnInMission_End(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent,
									 char* missionName,
									 MissionDef* missionDef,
									 ATH_ARG NOCONST(Mission)* mission,
									 U32 uNemesisID,
									 const ItemChangeReason *pReason,
									 GameAccountDataExtract *pExtract)
{
	int i;
	bool bResult = true;

	// Save mission to the mission history
	if (missionDef->bDisableCompletionTracking)
	{
		if(ActivityLog_tr_AddEntityLogEntry(ATR_PASS_ARGS, ent, ActivityLogEntryType_SimpleMissionComplete, missionName, timeSecondsSince2000(), 0.0f) != TRANSACTION_OUTCOME_SUCCESS)
		{
			bResult = false;
		}
	} 
	else if (mission->eCreditType == MissionCreditType_Primary)
	{
		NOCONST(CompletedMission)* completedMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->completedMissions, missionName);
		NOCONST(CompletedMissionStats)* completedMissionStats = NULL;

		if (!completedMission)
		{
			// For MadLibs Missions: completedMission->def should be set to the origDef, not the override def
			completedMission = StructCreateNoConst(parse_CompletedMission);
			SET_HANDLE_FROM_STRING("Mission", missionName, completedMission->def);
			eaIndexedAdd(&ent->pPlayer->missionInfo->completedMissions, completedMission);
		}

		// Find the stats for this version of the mission, if any
		if (missionDef->missionType == MissionType_Nemesis || missionDef->missionType == MissionType_NemesisArc || missionDef->missionType == MissionType_NemesisSubArc){
			for (i = eaSize(&completedMission->eaStats)-1; i>=0; --i){
				if (completedMission->eaStats[i]->iNemesisID == uNemesisID){
					completedMissionStats = completedMission->eaStats[i];
				}
			}
		} else if (missionDef->repeatable) {
			completedMissionStats = eaGet(&completedMission->eaStats, 0);
		}

		// Update stats
		if (completedMissionStats){
			U32 duration = 0;
			bool bIncrement = false;
			U32 uStartTm, uLastTm;

			++completedMissionStats->timesRepeated;
			uLastTm = timeSecondsSince2000();
			uStartTm = mission->startTime;
			duration = uLastTm - uStartTm;
			if(duration < completedMissionStats->bestTime)
			{
				completedMissionStats->bestTime = duration;
			}

			if(missionDef->iRepeatCooldownCount > 0)
			{
				// repeat mission types with count need to have it incremented if they are in the window of last
				// completion
				if(missionDef->fRepeatCooldownHours > 0)
				{
					F32 fTimeSec = timeSecondsSince2000() - completedMissionStats->lastCompletedTime;
					F32 fTimeHours = fTimeSec/3600.f;
					if (fTimeHours < missionDef->fRepeatCooldownHours)
					{
						// still in cooldown window
						bIncrement = true;
					}
				}
				if(missionDef->fRepeatCooldownHoursFromStart > 0)
				{
					F32 fTimeSec = timeSecondsSince2000() - completedMissionStats->lastStartTime;
					F32 fTimeHours = fTimeSec/3600.f;
					if (fTimeHours < missionDef->fRepeatCooldownHoursFromStart)
					{
						// in cooldown window
						bIncrement = true;
					}
				}

				// if the mission is in cooldown range, increment, otherwise restart
				if(bIncrement)
				{
					++completedMission->iRepeatCooldownCount;				
				}
				else
				{
					// restart count
					completedMission->iRepeatCooldownCount = 1;
				}
			}

			if(!bIncrement)
			{
				completedMissionStats->lastCompletedTime = uLastTm;
				completedMissionStats->lastStartTime = uStartTm;
			}

		} 
		else if (missionDef->missionType == MissionType_Nemesis || missionDef->missionType == MissionType_NemesisArc || missionDef->missionType == MissionType_NemesisSubArc || missionDef->repeatable)
		{
			completedMissionStats = StructCreateNoConst(parse_CompletedMissionStats);
			completedMissionStats->firstCompletedTime = timeSecondsSince2000();
			completedMissionStats->lastCompletedTime = timeSecondsSince2000();
			completedMissionStats->firstStartTime = mission->startTime;
			completedMissionStats->lastStartTime = mission->startTime;
			completedMissionStats->bestTime = completedMissionStats->lastCompletedTime - completedMissionStats->lastStartTime;
			completedMissionStats->iNemesisID = uNemesisID;
			if(missionDef->iRepeatCooldownCount > 0)
			{
				// repeat mission types with count need to have it set at one
				completedMission->iRepeatCooldownCount = 1;
			}
			eaPush(&completedMission->eaStats, completedMissionStats);

			// If there's a Completed Time on this mission, it changed from non-repeatable to repeatable
			if (completedMission->completedTime){
				completedMissionStats->firstCompletedTime = completedMission->completedTime;
				completedMission->completedTime = 0;
				++completedMissionStats->timesRepeated;
			}

			// If there's a Start Time on this mission, it changed from non-repeatable to repeatable
			if (completedMission->startTime) {
				completedMissionStats->firstStartTime = completedMission->startTime;
				completedMission->startTime = 0;
			}

		} else {
			completedMission->completedTime = timeSecondsSince2000();
			completedMission->startTime = mission->startTime;
		}

		// If the mission is an EPSIODE and has full-mission children, remove them from the active and completed missions list
		// And mark this completed mission as hidden
		if (missionDef->missionType == MissionType_Episode)
		{
			mission_trh_DropFullChildren(ATR_PASS_ARGS, ent, mission, pReason, pExtract);
			mission_trh_RemoveCompletedFullChildren(ATR_PASS_ARGS, ent, mission);
			completedMission->bHidden = true;
		}

	} else if(!missionDef->bDisableCompletionTracking) {
		// For missions completed with secondary credit, add an entry to the Recent Secondary Missions list
		if (missiondef_GetSecondaryCreditLockoutTime(missionDef) > 0){
			NOCONST(CompletedMission)* completedMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->eaRecentSecondaryMissions, missionName);
			if (!completedMission)
			{
				// For MadLibs Missions: completedMission->def should be set to the origDef, not the override def
				completedMission = StructCreateNoConst(parse_CompletedMission);
				SET_HANDLE_FROM_STRING("Mission", missionName, completedMission->def);
				eaIndexedAdd(&ent->pPlayer->missionInfo->eaRecentSecondaryMissions, completedMission);

				// If mission has repeat cooldown timeer, persist the start and end time
				if(missionDef->fRepeatCooldownHours || missionDef->fRepeatCooldownHoursFromStart)
				{
					mission_trh_UpdateCooldownForMission(ATR_PASS_ARGS, ent, missionName, mission->startTime);
				}
			}
			completedMission->completedTime = timeSecondsSince2000();
			completedMission->startTime = mission->startTime;

			// For missions which have already been completed, update their mission stats
			if(mission->eCreditType == MissionCreditType_AlreadyCompleted) {
				NOCONST(CompletedMission)* oldCompletedMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->completedMissions, missionName);
				NOCONST(CompletedMissionStats)* completedMissionStats = oldCompletedMission ? eaGet(&oldCompletedMission->eaStats, 0) : NULL;

				if (completedMissionStats){
					U32 duration = 0;
					++completedMissionStats->timesRepeated;
					completedMissionStats->lastCompletedTime = completedMission->completedTime;
					completedMissionStats->lastStartTime = completedMission->startTime;
					duration = completedMissionStats->lastCompletedTime - completedMissionStats->lastStartTime;
					if(duration < completedMissionStats->bestTime) {
						completedMissionStats->bestTime = duration;
					}
				} else if (oldCompletedMission) {
					completedMissionStats = StructCreateNoConst(parse_CompletedMissionStats);
					completedMissionStats->lastCompletedTime = completedMission->completedTime;
					completedMissionStats->lastStartTime = completedMission->startTime;
					completedMissionStats->bestTime = completedMissionStats->lastCompletedTime - completedMissionStats->lastStartTime;

					// If there's a Completed Time on this mission, it changed from non-repeatable to repeatable
					if (oldCompletedMission->completedTime){
						completedMissionStats->firstCompletedTime = oldCompletedMission->completedTime;
						oldCompletedMission->completedTime = 0;
						++completedMissionStats->timesRepeated;
					} else {
						completedMissionStats->firstCompletedTime = completedMission->completedTime;
					}

					// If there's a Start Time on this mission, it changed from non-repeatable to repeatable
					if (oldCompletedMission->startTime) {
						completedMissionStats->firstStartTime = oldCompletedMission->startTime;
						oldCompletedMission->startTime = 0;
					} else {
						completedMissionStats->firstStartTime = completedMission->startTime;
					}

					eaPush(&oldCompletedMission->eaStats, completedMissionStats);
				} else {
					TRANSACTION_APPEND_LOG_FAILURE("Mission turned in with \"already completed\" credit, but completed mission info was not found on player: %s", missionName);
					bResult = false;
				}
			}
		}
	}

	if (!bResult)
	{
		return bResult;
	}

	// Update completed missions for the player
	progression_trh_UpdateCompletedMissionForPlayer(ATR_PASS_ARGS, ent, missionDef);
	
	// Update completed missions for the current team and the player
	if (NONNULL(ent->pTeam) && ent->pTeam->iTeamID && ent->pTeam->eState == TeamState_Member)
	{
		// Update the list of completed missions for the team
		progression_trh_UpdateCompletedMissionForTeam(ATR_PASS_ARGS, ent, missionDef);
	}

	// If the mission is a Nemesis Arc, make the player check whether they need another Nemesis Arc
	if (missionDef->missionType == MissionType_NemesisArc){
		QueueRemoteCommand_nemesis_RemoteRefreshNemesisArc(ATR_RESULT_SUCCESS, 0, 0);
	}

	// Succeed all Mission Requests that have requested this Mission
	if (mission->eCreditType == MissionCreditType_Primary){
		for (i = eaSize(&ent->pPlayer->missionInfo->eaMissionRequests)-1; i>=0; --i){
			if (GET_REF(ent->pPlayer->missionInfo->eaMissionRequests[i]->hRequestedMission) == missionDef){
				ent->pPlayer->missionInfo->eaMissionRequests[i]->eState = MissionRequestState_Succeeded;
				QueueRemoteCommand_mission_RemoteMissionFlagAsNeedingEval(ATR_RESULT_SUCCESS, 0, 0, ent->pPlayer->missionInfo->eaMissionRequests[i]->pchRequesterRef, false);
			}
		}
	}

	// Clean up all Mission Requests that belong to this Mission
	mission_trh_RemoveRequestsRecursive(ATR_PASS_ARGS, ent, NULL, mission, pReason, pExtract);

	// Cleanup mission cooldown earray since we're locking it anyway
	mission_trh_CleanupCooldownList(ent);

	// Find and destroy the mission from the player's list
	eaFindAndRemove(&ent->pPlayer->missionInfo->missions, mission);

	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, missionDef->pchRefString, missiondef_GetType(missionDef), MissionState_TurnedIn, REF_STRING_FROM_HANDLE(missionDef->hCategory), true,
		NONNULL(mission) ? CONTAINER_RECONST(UGCMissionData, mission->pUGCMissionData) : NULL);

	StructDestroyNoConst(parse_Mission, mission);

	return bResult;
}

// This is the actual transaction
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Pplayer.Nemesisinfo.Eanemesisstates, .Hallegiance, .Hsuballegiance, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pplayer.Pprogressioninfo.Ppchwindbackmissions, .Pplayer.Missioninfo.Eamissioncooldowns, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Earecentsecondarymissions[AO], .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Pprogressioninfo.Pteamdata.Ppchcompletedmissions, .Pplayer.Pprogressioninfo.Ppchskippedmissions, .Pplayer.Pprogressioninfo.Eareplaydata, pPlayer.missionInfo.eaRecentSecondaryMissions[], .Pplayer.Playertype, .Pplayer.eaRewardGatedData")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_TurnInMission(ATR_ARGS, NOCONST(Entity)* ent, ContainerID entContainerID, 
												ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
												CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
												NOCONST(Guild) *pGuild, 
												char* missionName, 
												U32 uiTurnInTime, 
												ContactRewardChoices* rewardChoices,
												const ItemChangeReason *pReason,
												GameAccountDataExtract *pExtract)
{

	NOCONST(Mission)* mission = NULL;
	MissionDef* missionDef = NULL;
	U32 uTimesCompleted = 0;
	U32 uNemesisID = 0;

	TRANSACTION_APPEND_LOG_FAILURE("Entering mission_tr_TurnInMission for mission: %s", missionName);
	TRANSACTION_APPEND_LOG_SUCCESS("Entering mission_tr_TurnInMission for mission: %s", missionName);

	if(!mission_trh_TurnInMission_Begin(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, missionName, &missionDef, &mission, &uNemesisID, &uTimesCompleted, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Unable to turn in mission: %s", missionName);
	}

	if (mission_trh_GrantTurnInRewardsRecursive(ATR_PASS_ARGS, ent, eaPets, mission, missionDef, missionDef, uTimesCompleted, rewardChoices, mission->eCreditType, mission->startTime, uiTurnInTime, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant all rewards: %s", missionName);
	}

	if(!mission_trh_TurnInMission_End(ATR_PASS_ARGS, ent, missionName, missionDef, mission, uNemesisID, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Unable to turn in mission: %s", missionName);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully turned in mission: %s", missionName);
}

// This is exactly the same as mission_tr_TurnInMission.
// This is a separate transaction to make it easier to get stats on missions vs. perks
AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.Pugckillcreditlimit, .Pplayer.Pprogressioninfo.Ppchwindbackmissions, .Pplayer.Pprogressioninfo.Ppchskippedmissions, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Ppinventorybags, .Pplayer.Missioninfo.Eamissioncooldowns, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .pInventoryV2.Pplitebags, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Earecentsecondarymissions[AO], .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Pprogressioninfo.Pteamdata.Ppchcompletedmissions, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, pPlayer.missionInfo.eaRecentSecondaryMissions[], .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_TurnInPerkMission(ATR_ARGS, NOCONST(Entity)* ent, 
													ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
													CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
													NOCONST(Guild)* pGuild, 
													char* missionName, 
													U32 uiTurnInTime, 
													ContactRewardChoices* rewardChoices,
													const ItemChangeReason *pReason,
													GameAccountDataExtract *pExtract)
{
	return mission_tr_TurnInMission(ATR_PASS_ARGS, ent, ent->myContainerID, eaPets, eaVarContainer, pGuild, /*NULL,*/ missionName, uiTurnInTime, rewardChoices, pReason, pExtract);
}

typedef struct ChangeMissionStateData
{
	EntityRef erEnt;
	MissionDef* pRootDef;
	MissionDef* pChildDef;
	MissionState eNewState;
} ChangeMissionStateData;


static ChangeMissionStateData* mission_CreateChangeMissionStateData(Entity* pEnt, MissionDef* pRootDef, MissionDef* pChildDef, MissionState newState)
{
	ChangeMissionStateData* pData = calloc(1, sizeof(ChangeMissionStateData));
	pData->erEnt = entGetRef(pEnt);
	pData->pRootDef = pRootDef;
	pData->pChildDef = pChildDef;
	pData->eNewState = newState;
	return pData;
}

static void mission_TurnInMission_CB(TransactionReturnVal* pReturnVal, ChangeMissionStateData* pData)
{
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pData->erEnt);
		if (pEnt)
		{
			InteractInfo* pInteractInfo = SAFE_MEMBER(pEnt->pPlayer, pInteractInfo);
			
			// Refresh remote contacts
			if (pInteractInfo)
			{
				pInteractInfo->bUpdateRemoteContactsNextTick = true;
				pInteractInfo->bUpdateContactDialogOptionsNextTick = true;
			}
			// Refresh temporary powers
			if (pData->pRootDef && pData->pRootDef->missionType == MissionType_Perk)
			{
				if (pEnt && pEnt->pChar)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					if (character_UpdateTemporaryPowerTrees(entGetPartitionIdx(pEnt), pEnt->pChar))
					{
						character_ResetPowersArray(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
					}
				}
			}
			if ((pData->pRootDef && pData->pRootDef->bRefreshCharacterPowersOnComplete) ||
				(pData->pChildDef && pData->pChildDef->bRefreshCharacterPowersOnComplete))
			{
				entity_PowerTreeAutoBuy(entGetPartitionIdx(pEnt), pEnt, NULL);
			}
			
			// Update progression
			if (pData->pRootDef && eaSize(&pData->pRootDef->ppchProgressionNodes))
			{
				// Update the player's current progression
				progression_UpdateCurrentProgression(pEnt);
			}
		}
	}
	SAFE_FREE(pData);
}

// Call this to initiate the transaction
void mission_TurnInMissionInternal(MissionInfo* info, Mission* mission, ContactRewardChoices* rewardChoices)
{
	MissionDef* missionDef = mission_GetOrigDef(mission);  // Missions are keyed off the original def
	if (info->parentEnt && missionDef && missionDef->name && mission->state == MissionState_Succeeded)
	{
		Entity *pEnt = info->parentEnt;
		GameAccountData* pGameAccountData = entity_GetGameAccount( pEnt );
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ChangeMissionStateData* pData = mission_CreateChangeMissionStateData(pEnt, mission_GetDef(mission), missionDef, MissionState_Succeeded);
		TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-TurnIn", info->parentEnt, mission_TurnInMission_CB, pData);
		bool bMustLockShardVars = missioninfo_MustLockShardVariablesForTurnIn(mission, missionDef);
		bool bMustLockGuildLog = missioninfo_MustLockGuildActivityLogForTurnIn(((NONNULL(pEnt->pPlayer)) && NONNULL(pEnt->pPlayer->pGuild)), mission, missionDef);
		U32 uiCurrentTime = timeSecondsSince2000();
		ItemChangeReason reason = {0};

		U32* eaPets = NULL;
		U32* eaEmptyList = NULL;

		// Don't pass in pets unless unique items are possible
		if (missiondef_HasUniqueItemsInRewardsForState(missionDef,MissionState_TurnedIn,mission->eCreditType)) {
			ea32Create(&eaPets);
			Entity_GetPetIDList(pEnt, &eaPets);
		}

		if (missiondef_GetType(missionDef) == MissionType_Perk){
			inv_FillItemChangeReason(&reason, info->parentEnt, "Mission:TurnInPerk", missionDef->name);

			AutoTrans_mission_tr_TurnInPerkMission(returnVal, GetAppGlobalType(), 
				entGetType(info->parentEnt), entGetContainerID(info->parentEnt), 
				GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
				GLOBALTYPE_GUILD, bMustLockGuildLog ? info->parentEnt->pPlayer->pGuild->iGuildID : 0, 
				missionDef->name, uiCurrentTime, rewardChoices, &reason, pExtract);
		} else {
			inv_FillItemChangeReason(&reason, info->parentEnt, "Mission:TurnIn", missionDef->name);

			AutoTrans_mission_tr_TurnInMission(returnVal, GetAppGlobalType(),
				entGetType(info->parentEnt), entGetContainerID(info->parentEnt), entGetContainerID(info->parentEnt), 
				GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
				GLOBALTYPE_GUILD, bMustLockGuildLog ? info->parentEnt->pPlayer->pGuild->iGuildID : 0,
				missionDef->name, uiCurrentTime, rewardChoices, &reason, pExtract);

			if(missionDef->ugcProjectID && !isProductionEditMode())
				RemoteCommand_Intershard_aslUGCDataManager_MissionTurnIn(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
					missionDef->ugcProjectID, entGetAccountID( pEnt ), entGetContainerID( pEnt ));
		}

		ea32Destroy(&eaPets);
	}
}

// ----------------------------------------------------------------------------------
// Transaction to drop a mission
// ----------------------------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pchar.Ilevelexp");
bool mission_trh_DropMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, const char* missionName, bool bHideNotification, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Mission)* mission;
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, missionName);

	// Queue failure notification
	if (!bHideNotification) {
		QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_FAIL, 0, 0, MISSION_DROP_ERROR_MESG, missionName, kNotifyType_MissionError);
	}

	// Make sure the player has this mission to be removed
	if (!(mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, missionName))) {
		TRANSACTION_APPEND_LOG_FAILURE("Player does not have a mission of name: %s", missionName);
		return false;
	}

	// Queue success notification only if player actually has the mission
	if (!bHideNotification) {
		if(pDef->ugcProjectID)
			QueueRemoteCommand_notify_SetUGCMissionInfoReviewData(ATR_RESULT_SUCCESS, 0, 0, missionName, pDef->ugcProjectID,
				mission->pUGCMissionData ? mission->pUGCMissionData->bPlayingAsBetaReviewer : false);

		QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_SUCCESS, 0, 0, MISSION_DROPPED_MESG, missionName, kNotifyType_MissionDropped);
	}
	QueueRemoteCommand_UserExp_RemoteLogMissionComplete(ATR_RESULT_SUCCESS, 0, 0, missionName, NULL, true);

	// Remove the mission from the player's list.  This is done first in case something else
	// in this transaction tries to remove missions, to prevent a possible recursive loop.
	eaFindAndRemove(&ent->pPlayer->missionInfo->missions, mission);

	// Take all Mission Items
	// This shouldn't fail the transaction on failure, because we may be trying to drop an invalid mission
	inv_ent_trh_RemoveMissionItems(ATR_PASS_ARGS, ent, missionName, false, pReason, pExtract);

	// Clean up all Mission Requests that belong to this Mission
	mission_trh_RemoveRequestsRecursive(ATR_PASS_ARGS, ent, NULL, mission, pReason, pExtract);

	// Recursively drop full child missions AND remove any completed ones
	mission_trh_DropFullChildren(ATR_PASS_ARGS, ent, mission, pReason, pExtract);
	mission_trh_RemoveCompletedFullChildren(ATR_PASS_ARGS, ent, mission);

	// Destroy the mission
	StructDestroyNoConst(parse_Mission, mission);
	
	// Send dropped mission Event
	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, missionName, missiondef_GetType(pDef), MissionState_Dropped, pDef?REF_STRING_FROM_HANDLE(pDef->hCategory):NULL, true, /*UGCMissionData=*/NULL);

	return true;
}

// Helper to drop child full missions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pchar.Ilevelexp")
ATR_LOCKS(mission, ".Childfullmissions, .Children");
void mission_trh_DropFullChildren(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* mission, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;

	// Drop child full missions
	for (i = eaSize(&mission->childFullMissions) - 1; i >= 0; --i) 
		mission_trh_DropMission(ATR_PASS_ARGS, ent, mission->childFullMissions[i], /*NULL, */false, pReason, pExtract);

	// Recurse on children
	for (i = eaSize(&mission->children) - 1; i >= 0; --i)
		mission_trh_DropFullChildren(ATR_PASS_ARGS, ent, mission->children[i], pReason, pExtract);
}

// This is the actual transaction
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome mission_tr_DropMission(ATR_ARGS, NOCONST(Entity)* ent, const char* missionName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Mission) **missions = SAFE_MEMBER3(ent, pPlayer, missionInfo, missions);
	Mission *mission = missions ? eaIndexedGetUsingString(&missions, missionName) : NULL;
	ContainerID iUGCProjectID = mission ? UGCProject_GetProjectContainerIDFromUGCResource(mission->missionNameOrig) : 0;

	if(iUGCProjectID)
		QueueRemoteCommand_mission_IncrementDropCountStat(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), iUGCProjectID);

	if(!mission_trh_DropMission(ATR_PASS_ARGS, ent, missionName, false, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Failed to drop mission: %s", missionName);
	} else {
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully dropped mission: %s", missionName);
	}
}


AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pchar.Ilevelexp");
bool mission_trh_DropMissionNoRequests(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, char* missionName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Mission)* mission;
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, missionName);
	ContainerID iUGCProjectID = 0;

	// Queue failure notification
	QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_FAIL, 0, 0, MISSION_DROP_ERROR_MESG, missionName, kNotifyType_MissionError);

	// Make sure the player has this mission to be removed
	if (!(mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, missionName))) {
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s", missionName);
		return false;
	}

	iUGCProjectID = mission ? UGCProject_GetProjectContainerIDFromUGCResource(mission->missionNameOrig) : 0;
	if(iUGCProjectID)
	{
		QueueRemoteCommand_mission_IncrementDropCountStat(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), iUGCProjectID);
		QueueRemoteCommand_notify_SetUGCMissionInfoReviewData(ATR_RESULT_SUCCESS, 0, 0, missionName, iUGCProjectID,
			mission->pUGCMissionData ? mission->pUGCMissionData->bPlayingAsBetaReviewer : false);
	}

	// Queue success notification only if player actually has the mission
	QueueRemoteCommand_notify_RemoteSendMissionNotification(ATR_RESULT_SUCCESS, 0, 0, MISSION_DROPPED_MESG, missionName, kNotifyType_MissionDropped);
	QueueRemoteCommand_UserExp_RemoteLogMissionComplete(ATR_RESULT_SUCCESS, 0, 0, missionName, NULL, true);

	// Remove the mission from the player's list.  This is done first in case something else
	// in this transaction tries to remove missions, to prevent a possible recursive loop.
	eaFindAndRemove(&ent->pPlayer->missionInfo->missions, mission);

	// Take all Mission Items
	// This shouldn't fail the transaction on failure, because we may be trying to drop an invalid mission
	inv_ent_trh_RemoveMissionItems(ATR_PASS_ARGS, ent, missionName, false, pReason, pExtract);

	// Recursively drop full child missions AND remove completed ones
	mission_trh_DropFullChildren(ATR_PASS_ARGS, ent, mission, pReason, pExtract);
	mission_trh_RemoveCompletedFullChildren(ATR_PASS_ARGS, ent, mission);

	// Destroy the mission
	StructDestroyNoConst(parse_Mission, mission);

	// Send dropped mission Event
	QueueRemoteCommand_eventsend_RemoteRecordMissionState(ATR_RESULT_SUCCESS, 0, 0, missionName, missiondef_GetType(pDef), MissionState_Dropped, pDef?REF_STRING_FROM_HANDLE(pDef->hCategory):NULL, true, /*UGCMissionData=*/NULL);

	return true;
}

// This is the actual transaction
// This version doesn't clean up Mission Requests, which means that there is less locking
AUTO_TRANSACTION
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp");
enumTransactionOutcome mission_tr_DropMissionNoRequests(ATR_ARGS, NOCONST(Entity)* ent, char* missionName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(!mission_trh_DropMissionNoRequests(ATR_PASS_ARGS, ent, missionName, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Failed to drop mission: %s", missionName);
	} else {
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully dropped mission: %s", missionName);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissioncooldowns, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp");
enumTransactionOutcome mission_tr_UpdateCooldownAndDropMission(ATR_ARGS, NOCONST(Entity)* ent, const char* missionName, U32 startTime, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(ISNULL(ent))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Update Cooldown and Drop Mission failed: NULL entity.");
	}

	if(!mission_trh_UpdateCooldownForMission(ATR_PASS_ARGS, ent, missionName, startTime))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Update Cooldown and Drop Mission failed: Failed to update cooldown of %s for ent:%s", missionName, ent->debugName);
	}

	if(!mission_trh_DropMission(ATR_PASS_ARGS, ent, missionName, false, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Update Cooldown and Drop Mission failed: Failed to drop mission %s for ent:%s", missionName, ent->debugName);
	}

	// Clean cooldown list since we're locking it anyway
	mission_trh_CleanupCooldownList(ent);

	TRANSACTION_RETURN_LOG_SUCCESS("Update Cooldown and Drop Mission succeded: Mission %s dropped for ent:%s", missionName, ent->debugName);
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissioncooldowns, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp");
enumTransactionOutcome mission_tr_UpdateCooldownAndDropMissionNoRequests(ATR_ARGS, NOCONST(Entity)* ent, char* missionName, U32 startTime, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(ISNULL(ent))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Update Cooldown and Drop Mission failed: NULL entity.");
	}

	if(!mission_trh_UpdateCooldownForMission(ATR_PASS_ARGS, ent, missionName, startTime))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Update Cooldown and Drop Mission failed: Failed to update cooldown of %s for ent:%s", missionName, ent->debugName);
	}

	if(!mission_trh_DropMissionNoRequests(ATR_PASS_ARGS, ent, missionName, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Update Cooldown and Drop Mission failed: Failed to drop mission %s for ent:%s", missionName, ent->debugName);
	}

	// Clean cooldown list since we're locking it anyway
	mission_trh_CleanupCooldownList(ent);

	TRANSACTION_RETURN_LOG_SUCCESS("Update Cooldown and Drop Mission succeded: Mission %s dropped for ent:%s", missionName, ent->debugName);
}

static void missioninfo_DropMissionCB(TransactionReturnVal* returnVal, void* pData)
{
	DropMissionData* pMissionData = (DropMissionData *) pData;
	if(pMissionData)
	{
		Entity* pEnt = entFromContainerIDAnyPartition(pMissionData->eEntType, pMissionData->iEntID);

		// Transaction failed, try to redo transaction without UGC project
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			if(pMissionData->iUGCID)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
				TransactionReturnVal* pNewReturnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-Drop", pEnt, NULL, NULL);
				ItemChangeReason reason = {0};
				inv_FillItemChangeReason(&reason, pEnt, "Mission:DropMission", pMissionData->pchMissionName);

				if(pMissionData->bUpdateCooldown)
				{
					if (pMissionData->bHasRequests){
						AutoTrans_mission_tr_UpdateCooldownAndDropMission(pNewReturnVal, GetAppGlobalType(), 
							pMissionData->eEntType, pMissionData->iEntID, 
							pMissionData->pchMissionName, pMissionData->uStartTime, 
							&reason, pExtract);
					} else {
						AutoTrans_mission_tr_UpdateCooldownAndDropMissionNoRequests(pNewReturnVal, GetAppGlobalType(),
							pMissionData->eEntType, pMissionData->iEntID,
							pMissionData->pchMissionName, pMissionData->uStartTime, 
							&reason, pExtract);
					}
				} 
				else
				{
					if (pMissionData->bHasRequests){
						AutoTrans_mission_tr_DropMission(pNewReturnVal, GetAppGlobalType(), 
							pMissionData->eEntType, pMissionData->iEntID, pMissionData->pchMissionName, 
							&reason, pExtract);
					} else {
						AutoTrans_mission_tr_DropMissionNoRequests(pNewReturnVal, GetAppGlobalType(), 
							pMissionData->eEntType, pMissionData->iEntID, pMissionData->pchMissionName, 
							&reason, pExtract);
					}
				}
			}
		}
		else
		{
			if(pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo)
				pEnt->pPlayer->missionInfo->needsEval = true;
		}

		StructDestroy(parse_DropMissionData, pMissionData);
	}
}

// Call this to initiate the transaction
void missioninfo_DropMission(Entity *pEnt, MissionInfo* info, Mission* mission)
{
	if (mission && !mission->parent)
	{
		if (mission_IsPersisted(mission) && info && info->parentEnt)
		{
			GameAccountDataExtract*pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			TransactionReturnVal* returnVal = NULL;
			// If mission has cooldown time, make sure it is recorded
			MissionDef* pDef = mission_GetDef(mission);
			ContainerID iUGCProjectID = mission && !isProductionEditMode() ? UGCProject_GetProjectContainerIDFromUGCResource(mission->missionNameOrig) : 0;
			bool bHasRequests = missionrequest_HasRequestsRecursive(info, mission, mission);
			bool bUpdateCooldown = missiondef_DropMissionShouldUpdateCooldown(pDef);
			DropMissionData* pCBData;
			ItemChangeReason reason = {0};

			if(iUGCProjectID)
			{
				pCBData = StructCreate(parse_DropMissionData);
				pCBData->bHasRequests = bHasRequests;
				pCBData->bUpdateCooldown = bUpdateCooldown;
				pCBData->eEntType = entGetType(pEnt);
				pCBData->iEntID = entGetContainerID(pEnt);
				pCBData->pchMissionName = mission->missionNameOrig;
				pCBData->uStartTime = mission->startTime;
				pCBData->iUGCID = iUGCProjectID;
				returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-Drop", pEnt, missioninfo_DropMissionCB, pCBData);
			}
			else
			{
				returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-Drop", pEnt, NULL, NULL);
			}

			inv_FillItemChangeReason(&reason, pEnt, "Mission:DropMission", mission->missionNameOrig);

			if(bUpdateCooldown)
			{
				if (bHasRequests){
					AutoTrans_mission_tr_UpdateCooldownAndDropMission(returnVal, GetAppGlobalType(), 
						entGetType(pEnt), entGetContainerID(pEnt), 
						mission->missionNameOrig, mission->startTime,
						&reason, pExtract);
				} else {
					AutoTrans_mission_tr_UpdateCooldownAndDropMissionNoRequests(returnVal, GetAppGlobalType(), 
						entGetType(pEnt), entGetContainerID(pEnt), 
						mission->missionNameOrig, mission->startTime,
						&reason, pExtract);
				}
			} 
			else
			{
				if (bHasRequests){
					AutoTrans_mission_tr_DropMission(returnVal, GetAppGlobalType(), 
						entGetType(pEnt), entGetContainerID(pEnt), 
						mission->missionNameOrig,
						&reason, pExtract);
				} else {
					AutoTrans_mission_tr_DropMissionNoRequests(returnVal, GetAppGlobalType(), 
						entGetType(pEnt), entGetContainerID(pEnt), 
						mission->missionNameOrig,
						&reason, pExtract);
				}
			}
		}
		else if (info && eaFind(&info->eaNonPersistedMissions, mission) >= 0)
		{
			mission_PreMissionDestroyDeinitRecursive(entGetPartitionIdx(pEnt), mission);
			eaFindAndRemove(&info->eaNonPersistedMissions, mission);
			StructDestroy(parse_Mission, mission);
		}
		else if (info && eaFind(&info->eaDiscoveredMissions, mission) >= 0)
		{
			mission_PreMissionDestroyDeinitRecursive(entGetPartitionIdx(pEnt), mission);
			eaFindAndRemove(&info->eaDiscoveredMissions, mission);
			StructDestroy(parse_Mission, mission);
		}
	}
}


// ----------------------------------------------------------------------------------
// Transactions to change a mission state
// ----------------------------------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(ent, "pPlayer.missionInfo.missions[]");
enumTransactionOutcome mission_AddUGCPlayingTime(ATR_ARGS, NOCONST(Entity)* ent, const char *pcMissionName, U32 iCompletionTimeInSeconds)
{
	NOCONST(Mission)* mission = NULL;

	// if the player doesn't have the mission, the transaction system will not even bother to send pPlayer
	if(ISNULL(ent->pPlayer) || ISNULL(ent->pPlayer->missionInfo) || ISNULL(ent->pPlayer->missionInfo->missions))
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have mission %s", pcMissionName);

	// Find the Mission
	mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pcMissionName);
	if(ISNULL(mission))
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have mission %s", pcMissionName);

	if(ISNULL(mission->pUGCMissionData))
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have UGC mission data for %s", pcMissionName);

	mission->pUGCMissionData->fPlayingTimeInMinutes += iCompletionTimeInSeconds / 60.0f;

	TRANSACTION_RETURN_LOG_SUCCESS("Updated player playing time for UGC mission %s", pcMissionName);
}

static void mission_np_ChangeMissionState(int iPartitionIdx, Mission *pMission, MissionState newState, bool bForcePermanentComplete)
{
	MissionDef *pDef = mission_GetDef(pMission);
	NOCONST(Mission) *pNoConstMission = CONTAINER_NOCONST(Mission, pMission);
	static Entity** eaPlayersWithCredit = NULL;
	Mission *pRootMission = pMission;

	while (pRootMission->parent)
		pRootMission = pRootMission->parent;

	// If this is an Open Mission, we should get a list of players who have points for this Mission
	eaClear(&eaPlayersWithCredit);
	if (mission_GetType(pRootMission) == MissionType_OpenMission){
		openmission_GetScoreboardEnts(iPartitionIdx, pRootMission, &eaPlayersWithCredit);
	}

	pNoConstMission->state = newState;

	// Set openmission participants
	if (  newState == MissionState_Succeeded && pMission->parent == NULL
		  && mission_GetType(pMission) == MissionType_OpenMission) {
		OpenMission* pOpenMission = openmission_GetFromName(iPartitionIdx, pDef->name);
		if(pOpenMission) {
			openmission_SetParticipants(pOpenMission);
			pOpenMission->uiSucceededTime = timeSecondsSince2000();

			// Many open missions don't have scoreboards, but we still want to give credit for mission
			// state changes. So, if there are no scoreboard events, just get the list of participants.
			if (!pRootMission->eaTrackedScoreboardEvents && !eaSize(&eaPlayersWithCredit))
			{
				int i;
				for (i = eaiSize(&pOpenMission->eaiParticipants) - 1; i >= 0; i--)
				{
					Entity *pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pOpenMission->eaiParticipants[i]);
					if (pEnt)
					{
						eaPush(&eaPlayersWithCredit, pEnt);
					}
				}
			}
		}
	} 

	if (newState == MissionState_Succeeded && bForcePermanentComplete)
		pNoConstMission->permaComplete = true;

	eventsend_RecordMissionStateMultipleEnts(iPartitionIdx, &eaPlayersWithCredit, pDef->pchRefString, missiondef_GetType(pDef), newState, REF_STRING_FROM_HANDLE(pDef->hCategory), (pMission && pMission->parent == NULL), pMission ? pMission->pUGCMissionData : NULL);

	if (pMission->parent){
		if (newState == MissionState_Succeeded)
			gameaction_np_RunActionsSubMissions(iPartitionIdx, pMission->parent, &pDef->ppSuccessActions);
		else if (newState == MissionState_Failed)
			gameaction_np_RunActionsSubMissions(iPartitionIdx, pMission->parent, &pDef->ppFailureActions);
	} else {
		if (newState == MissionState_Succeeded)
			gameaction_np_RunActions(iPartitionIdx, pMission, &pDef->ppSuccessActions);
		else if (newState == MissionState_Failed)
			gameaction_np_RunActions(iPartitionIdx, pMission, &pDef->ppFailureActions);
	}

	if (mission_GetType(pRootMission) == MissionType_OpenMission)
	{
		mission_RunMapComplete(iPartitionIdx, pDef, newState);
	}

	// Update data for all players in the mission
	if(mission_GetType(pMission) == MissionType_OpenMission)
	{
		EntityIterator* iter = NULL;
		Entity *currEnt = NULL;

		char namespacedMissionName[RESOURCE_NAME_MAX_SIZE];
		bool bIsSucceedingUGCMap = (!isProductionEditMode() && pMission && pMission == pRootMission && newState == MissionState_Succeeded && pDef->ugcProjectID);
		U32 completionTime = bIsSucceedingUGCMap ? timeSecondsSince2000() - pMission->startTime : 0;

		if(bIsSucceedingUGCMap)
		{
			char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBaseMapName[RESOURCE_NAME_MAX_SIZE];
			if(resExtractNameSpace(zmapGetName(NULL), pchNameSpace, pchBaseMapName))
				sprintf(namespacedMissionName, "%s:Mission", pchNameSpace);
			else
				bIsSucceedingUGCMap = false;
		}

		iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		currEnt = NULL;
		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			MissionInfo *pInfo = SAFE_MEMBER2(currEnt, pPlayer, missionInfo);
			if (pInfo && pInfo->pchCurrentOpenMission == pRootMission->missionNameOrig)
			{
				if(bIsSucceedingUGCMap)
				{
					log_printf(LOG_UGC, "EntityPlayer %u contributed to the OpenMission on map %s with duration %u for UGC mission %s", entGetContainerID(currEnt), zmapGetName(NULL), timeSecondsSince2000() - pMission->startTime, namespacedMissionName);

					AutoTrans_mission_AddUGCPlayingTime(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(),
						entGetType(currEnt), entGetContainerID(currEnt),
						namespacedMissionName,
						completionTime);
				}

				// Waypoint refresh
				waypoint_UpdateMissionWaypoints(pInfo, pMission);
				mission_FlagInfoAsDirty(pInfo);

				// Send notifications
				if (pDef && mission_HasDisplayName(pRootMission)){
					if (pMission == pRootMission){
						if(newState == MissionState_Succeeded && !pDef->bIsHandoff){
							notify_SendMissionNotification(currEnt, NULL, pDef, MISSION_COMPLETE_MSG, kNotifyType_OpenMissionSuccess);
						} else if(newState == MissionState_Failed) {
							notify_SendMissionNotification(currEnt, NULL, pDef, MISSION_FAILED_MSG, kNotifyType_OpenMissionFailed);
						}
					} else if (missiondef_HasUIString(pDef)){
						if(newState == MissionState_Succeeded){
							notify_SendMissionNotification(currEnt, NULL, pDef, MISSION_SUBOBJECTIVE_COMPLETE_MSG, kNotifyType_OpenMissionSubObjectiveComplete);
						}
					}
				}
			}
		}
		EntityIteratorRelease(iter);

		if(bIsSucceedingUGCMap)
			RemoteCommand_Intershard_aslUGCDataManager_RecordMapCompletion(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, pDef->ugcProjectID, zmapGetName(NULL), completionTime / 60.0f);
	}

	mission_FlagAsNeedingEval(pMission);
	mission_UpdateTimestamp(pDef, pMission);
	mission_FlagAsDirty(pMission);
}

// This actually changes the state of the Mission
AUTO_TRANS_HELPER
ATR_LOCKS(mission, ".State, .Permacomplete");
void mission_trh_ChangeMissionState(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Mission)* mission, MissionDef *pDef, int newState, U32 bForcePermanentComplete, U32 bIsRoot)
{
	// Change the state of the Mission
	mission->state = newState;

	if (newState == MissionState_Succeeded && bForcePermanentComplete)
		mission->permaComplete = true;

	// Send Events, update waypoints, send chat feedback, etc. all combined into this command
	QueueRemoteCommand_mission_RemoteMissionStateChange(ATR_RESULT_SUCCESS, 0, 0, pDef->pchRefString, newState, bIsRoot);
}

// This changes the state of a SubMission and runs any appropriate actions/grants rewards
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Eamissionrequests[AO], pPlayer.missionInfo.missions[]");
enumTransactionOutcome mission_tr_ChangeSubMissionStateNoLocking(ATR_ARGS, NOCONST(Entity)* ent, const char* pchRootMissionName, const char* pchParentMissionName, const char* pchMissionName, int newState, U32 bForcePermanentComplete)
{
	NOCONST(Mission)* mission = NULL;
	NOCONST(Mission)* parentMission = NULL;
	MissionDef* missionDef = NULL;
	MissionDef* parentMissionDef = NULL;
	MissionDef* rootMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchRootMissionName);

	// Get the Root Mission
	mission = parentMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchRootMissionName);

	// Find the Mission
	if (mission && pchMissionName && stricmp(pchRootMissionName, pchMissionName))
		mission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, mission), pchMissionName));

	// Find the Parent mission
	if (parentMission && pchParentMissionName && stricmp(pchRootMissionName, pchParentMissionName))
		parentMission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, parentMission), pchParentMissionName));

	// Make sure the player has this mission
	if (!mission)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s::%s", pchRootMissionName, pchMissionName);

	// Make sure the MissionDef exists
	if (!(missionDef = missiondef_ChildDefFromName(rootMissionDef, pchMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s::%s", pchRootMissionName, pchMissionName);

	// Make sure there's a parent mission
	if (!parentMission)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s::%s", pchRootMissionName, pchParentMissionName);

	// Make sure the parent MissionDef exists
	if (!stricmp(pchParentMissionName, pchRootMissionName))
		parentMissionDef = rootMissionDef;
	else if (!(parentMissionDef = missiondef_ChildDefFromName(rootMissionDef, pchParentMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s::%s", pchRootMissionName, pchParentMissionName);

	// Change the state of the mission
	mission_trh_ChangeMissionState(ATR_PASS_ARGS, ent, mission, missionDef, newState, bForcePermanentComplete, false);

	// Run actions and grant rewards
	if (newState == MissionState_Succeeded)
	{
		if (gameaction_trh_RunActionsSubMissionsNoLocking(ATR_PASS_ARGS, ent, parentMission, parentMissionDef, rootMissionDef, &missionDef->ppSuccessActions) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s::%s", pchRootMissionName, pchMissionName);
		if (missiondef_HasSuccessRewards(missionDef)){
			TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a Mission transaction, but Inventory must be locked!");
		}
	}
	else if (newState == MissionState_Failed)
	{
		if (gameaction_trh_RunActionsSubMissionsNoLocking(ATR_PASS_ARGS, ent, parentMission, parentMissionDef, rootMissionDef, &missionDef->ppFailureActions) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s::%s", pchRootMissionName, pchMissionName);
		if (missiondef_HasFailureRewards(missionDef)){
			TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a Mission transaction, but Inventory must be locked!");
		}
	}
		
	// Record state change for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionState(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName, pchMissionName, newState);

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully changed state to %s for mission: %s::%s", StaticDefineIntRevLookup(MissionStateEnum, newState), pchRootMissionName, pchMissionName);
}

// Exactly the same as mission_tr_ChangeSubMissionStateNoLocking.  This is a separate 
// transaction to make it easier to get stats on missions vs. perks
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Eamissionrequests[AO], pPlayer.missionInfo.missions[]");
enumTransactionOutcome mission_tr_ChangePerkSubMissionStateNoLocking(ATR_ARGS, NOCONST(Entity)* ent, const char* pchRootMissionName, const char* pchParentMissionName, const char* pchMissionName, int newState, U32 bForcePermanentComplete)
{
	return mission_tr_ChangeSubMissionStateNoLocking(ATR_PASS_ARGS, ent, pchRootMissionName, pchParentMissionName, pchMissionName, newState, bForcePermanentComplete);
}

// This changes the state of a SubMission and runs any appropriate actions/grants rewards
AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Pugckillcreditlimit, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_ChangeSubMissionState(ATR_ARGS, NOCONST(Entity)* ent, 
														ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
														CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
														NOCONST(Guild)* pGuild, 
														const char* pchRootMissionName, 
														const char* pchParentMissionName, 
														const char* pchMissionName, 
														int newState, 
														U32 bForcePermanentComplete,
														const ItemChangeReason *pReason,
														GameAccountDataExtract *pExtract)
{
	NOCONST(Mission)* mission = NULL;
	NOCONST(Mission)* parentMission = NULL;
	MissionDef* missionDef = NULL;
	MissionDef* parentMissionDef = NULL;
	MissionDef* rootMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchRootMissionName);
	U32 uTimesCompleted = missioninfo_trh_GetNumTimesCompleted(ent->pPlayer->missionInfo, pchParentMissionName);
	U32 seed;

	// Get the Root Mission
	mission = parentMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchRootMissionName);

	// Find the Mission
	if (mission && pchMissionName && stricmp(pchRootMissionName, pchMissionName))
		mission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, mission), pchMissionName));
	
	// Find the Parent mission
	if (parentMission && pchParentMissionName && stricmp(pchRootMissionName, pchParentMissionName))
		parentMission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, parentMission), pchParentMissionName));

	// Make sure the player has this mission
	if (!mission)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s::%s", pchRootMissionName, pchMissionName);

	// Make sure the MissionDef exists
	if (!(missionDef = missiondef_ChildDefFromName(rootMissionDef, pchMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s::%s", pchRootMissionName, pchMissionName);

	// Make sure there's a parent mission
	if (!parentMission)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s::%s", pchRootMissionName, pchParentMissionName);

	// Make sure the parent MissionDef exists
	if (!stricmp(pchParentMissionName, pchRootMissionName))
		parentMissionDef = rootMissionDef;
	else if (!(parentMissionDef = missiondef_ChildDefFromName(rootMissionDef, pchParentMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s::%s", pchRootMissionName, pchParentMissionName);

	// Change the state of the mission
	mission_trh_ChangeMissionState(ATR_PASS_ARGS, ent, mission, missionDef, newState, bForcePermanentComplete, false);
	
	seed = mission_GetRewardSeedEx(ent->myContainerID, parentMissionDef, parentMission->startTime, uTimesCompleted);

	// Run actions and grant rewards
	if (newState == MissionState_Succeeded)
	{
		int iLevel =  (isProductionEditMode() || parentMission->pUGCMissionData) ? entity_trh_GetSavedExpLevel(ent) : parentMission->iLevel;
		bool bUGCProject = isProductionEditMode() ? !!parentMissionDef->ugcProjectID : !!parentMission->pUGCMissionData;
		bool bCreatedByAccount = parentMission->pUGCMissionData ? parentMission->pUGCMissionData->bCreatedByAccount : false;
		bool bStatsQualifyForUGCRewards = isProductionEditMode() ? true : (parentMission->pUGCMissionData ? parentMission->pUGCMissionData->bStatsQualifyForUGCRewards : false);
		bool bQualifyForUGCFeaturedRewards =
			!!(parentMission->pUGCMissionData && (parentMission->pUGCMissionData->bProjectIsFeatured || (gConf.bUGCPreviouslyFeaturedMissionsQualifyForRewards && parentMission->pUGCMissionData->bProjectWasFeatured)));
		U32 iNumberOfPlays = isProductionEditMode() ? 20 : (parentMission->pUGCMissionData ? parentMission->pUGCMissionData->iNumberOfPlays : 0);
		F32 fAverageDurationInMinutes = isProductionEditMode() ? 30 : (parentMission->pUGCMissionData ? parentMission->pUGCMissionData->fAverageDurationInMinutes : 0);

		if(gConf.bUGCAveragePlayingTimeUsesCustomMapPlayingTime && parentMission->pUGCMissionData && 0 == iNumberOfPlays)
			fAverageDurationInMinutes = parentMission->pUGCMissionData->fPlayingTimeInMinutes;

		if (gameaction_trh_RunActionsSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, parentMission, parentMissionDef, &missionDef->ppSuccessActions, NULL, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s::%s", pchRootMissionName, pchMissionName);

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, missionDef, MissionState_Succeeded, NULL, &seed, 0, iLevel, 0, 0, true, pReason, pExtract, bUGCProject, bCreatedByAccount, bStatsQualifyForUGCRewards, bQualifyForUGCFeaturedRewards, fAverageDurationInMinutes) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchMissionName);
	}
	else if (newState == MissionState_Failed)
	{
		if (gameaction_trh_RunActionsSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, parentMission, parentMissionDef, &missionDef->ppFailureActions, NULL, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s::%s", pchRootMissionName, pchMissionName);

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ missionDef, MissionState_Failed, NULL, &seed, 0, mission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchMissionName);
	}

	// Record state change for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionState(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName, pchMissionName, newState);

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully changed state to %s for mission: %s::%s", StaticDefineIntRevLookup(MissionStateEnum, newState), pchRootMissionName, pchMissionName);
}

// Exactly the same as mission_tr_ChangeSubMissionState.  This is a separate 
// transaction to make it easier to get stats on missions vs. perks
AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Pugckillcreditlimit, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_ChangePerkSubMissionState(ATR_ARGS, NOCONST(Entity)* ent, 
															ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
															CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
															NOCONST(Guild) *pGuild, 
															const char* pchRootMissionName, 
															const char* pchParentMissionName, 
															const char* pchMissionName, 
															int newState, 
															U32 bForcePermanentComplete,
															const ItemChangeReason *pReason,
															GameAccountDataExtract *pExtract)
{
	return mission_tr_ChangeSubMissionState(ATR_PASS_ARGS, ent, eaPets, eaVarContainer, pGuild, /*NULL,*/ pchRootMissionName, pchParentMissionName, pchMissionName, newState, bForcePermanentComplete, pReason, pExtract);
}

// This changes the state of a Root mission, but doesn't try to perform any Actions/Rewards.  This does less locking.
AUTO_TRANSACTION
ATR_LOCKS(ent, "pPlayer.missionInfo.missions[]");
enumTransactionOutcome mission_tr_ChangeRootMissionStateNoLocking(ATR_ARGS, NOCONST(Entity)* ent, const char* pchMissionName, int newState, U32 bForcePermanentComplete, int bRecordUGCCompletion)
{
	NOCONST(Mission)* mission = NULL;
	MissionDef* missionDef = NULL;
	ContainerID iUGCProjectID = 0;

	// if the player doesn't have the mission, the transaction system will not even bother to send pPlayer
	if ( ISNULL(ent->pPlayer) || ISNULL(ent->pPlayer->missionInfo) || ISNULL(ent->pPlayer->missionInfo->missions) )
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s", pchMissionName);
	}

	// Get the Mission
	mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchMissionName);

	// Make sure the player has this mission
	if (!mission)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s", pchMissionName);

	iUGCProjectID = mission ? UGCProject_GetProjectContainerIDFromUGCResource(mission->missionNameOrig) : 0;

	// Make sure the MissionDef exists
	if (!(missionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s", pchMissionName);

	// Change the state of the mission
	mission_trh_ChangeMissionState(ATR_PASS_ARGS, ent, mission, missionDef, newState, bForcePermanentComplete, true);

	// Grant new missions to the parent mission
	if (newState == MissionState_Succeeded)
	{
		char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];

		if (gameaction_trh_RunActionsNoLocking(ATR_PASS_ARGS, &missionDef->ppSuccessActions) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not perform all actions: %s", pchMissionName);

		if (missiondef_HasSuccessRewards(missionDef))
			TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a Mission transaction, but Inventory must be locked!");

		if(resExtractNameSpace(pchMissionName, pchNameSpace, pchBase) && 0 == stricmp(pchBase, "Mission") && iUGCProjectID)
			QueueRemoteCommand_mission_RecordCompletion(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), iUGCProjectID, pchNameSpace, (timeSecondsSince2000() - mission->startTime) / 60, bRecordUGCCompletion);
	}
	else if (newState == MissionState_Failed)
	{
		if (gameaction_trh_RunActionsNoLocking(ATR_PASS_ARGS, &missionDef->ppFailureActions) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not perform all actions: %s", pchMissionName);
		if (missiondef_HasFailureRewards(missionDef)){
			TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a Mission transaction, but Inventory must be locked!");
		}
	}

	// Record state change for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionState(ATR_RESULT_SUCCESS, 0, 0, pchMissionName, NULL, newState);

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully changed state to %s for mission: %s", StaticDefineIntRevLookup(MissionStateEnum, newState), pchMissionName);
}

// Exactly the same as mission_tr_ChangeRootMissionStateNoLocking.  
// This is a separate transaction to make it easier to get stats on missions vs. perks
AUTO_TRANSACTION
ATR_LOCKS(ent, "pPlayer.missionInfo.missions[]");
enumTransactionOutcome mission_tr_ChangePerkRootMissionStateNoLocking(ATR_ARGS, NOCONST(Entity)* ent, const char* pchMissionName, int newState, U32 bForcePermanentComplete)
{
	return mission_tr_ChangeRootMissionStateNoLocking(ATR_PASS_ARGS, ent, pchMissionName, newState, bForcePermanentComplete, /*bRecordUGCCompletion=*/false);
}

// This changes the state of a Root mission, and tries to perform all possible actions/rewards.
// This locks a lot of stuff; use trRootChangeMissionStateNoActions when possible.
AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Pugckillcreditlimit, .Pteam.Lastrecruittype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Costumedata.Eaunlockedcostumerefs, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pplayer.Earewardmods, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_ChangeRootMissionState(ATR_ARGS, NOCONST(Entity)* ent, 
														 ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
														 CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
														 NOCONST(Guild)* pGuild,
														 const char* pchMissionName, 
														 int newState, 
														 U32 bForcePermanentComplete,
														 const ItemChangeReason *pReason,
														 GameAccountDataExtract *pExtract,
														 int bRecordUGCCompletion)
{
	WorldVariableArray *pVariableArray = NULL;
	NOCONST(Mission)* mission = NULL;
	MissionDef* missionDef = NULL;

	// Get the Mission
	mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchMissionName);

	// Make sure the player has this mission
	if (!mission)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not have a mission of name: %s", pchMissionName);

	// Make sure the MissionDef exists
	if (!(missionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s", pchMissionName);

	// Change the state of the mission
	mission_trh_ChangeMissionState(ATR_PASS_ARGS, ent, mission, missionDef, newState, bForcePermanentComplete, true);

	// Create variable array
	pVariableArray = mission_trh_CreateVariableArray(mission);

	// Grant new missions to the parent mission
	if (newState == MissionState_Succeeded)
	{
		ContainerID iUGCProjectID = mission ? UGCProject_GetProjectContainerIDFromUGCResource(mission->missionNameOrig) : 0;
		char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];

		bool bUGCProjectInPreviewMode = isProductionEditMode() && !!missionDef->ugcProjectID;
		bool bUGCProjectInLiveMode = !!mission->pUGCMissionData;

		bool bCreatedByAccount = bUGCProjectInPreviewMode ? false : (bUGCProjectInLiveMode && mission->pUGCMissionData->bCreatedByAccount);
		bool bStatsQualifyForUGCRewards = bUGCProjectInPreviewMode ? true : (bUGCProjectInLiveMode && mission->pUGCMissionData->bStatsQualifyForUGCRewards);
		bool bQualifyForUGCFeaturedRewards =
			!!(mission->pUGCMissionData && (mission->pUGCMissionData->bProjectIsFeatured || (gConf.bUGCPreviouslyFeaturedMissionsQualifyForRewards && mission->pUGCMissionData->bProjectWasFeatured)));
		int iLevel = (bUGCProjectInPreviewMode || bUGCProjectInLiveMode) ? entity_trh_GetSavedExpLevel(ent) : mission->iLevel;
		U32 iNumberOfPlays = bUGCProjectInPreviewMode ? 20 : (mission->pUGCMissionData ? mission->pUGCMissionData->iNumberOfPlays : 0);
		F32 fAverageDurationInMinutes = bUGCProjectInPreviewMode ? 30 : (mission->pUGCMissionData ? mission->pUGCMissionData->fAverageDurationInMinutes : 0);

		if(gConf.bUGCAveragePlayingTimeUsesCustomMapPlayingTime && mission->pUGCMissionData && 0 == iNumberOfPlays)
			fAverageDurationInMinutes = mission->pUGCMissionData->fPlayingTimeInMinutes;

		if (gameaction_trh_RunActionsLockAll(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, &missionDef->ppSuccessActions, pVariableArray, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not perform all actions: %s", pchMissionName);
		}

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ missionDef, MissionState_Succeeded, NULL, NULL, 0, mission->iLevel, 0, 0, false, pReason, pExtract, bUGCProjectInPreviewMode || bUGCProjectInLiveMode, bCreatedByAccount, bStatsQualifyForUGCRewards, bQualifyForUGCFeaturedRewards, fAverageDurationInMinutes) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchMissionName);
		}

		if(resExtractNameSpace(pchMissionName, pchNameSpace, pchBase) && 0 == stricmp(pchBase, "Mission") && iUGCProjectID)
			QueueRemoteCommand_mission_RecordCompletion(ATR_RESULT_SUCCESS, GetAppGlobalType(), GetAppGlobalID(), iUGCProjectID, pchNameSpace, (timeSecondsSince2000() - mission->startTime) / 60, bRecordUGCCompletion);
	}
	else if (newState == MissionState_Failed)
	{
		if (gameaction_trh_RunActionsLockAll(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, &missionDef->ppFailureActions, pVariableArray, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not perform all actions: %s", pchMissionName);
		}

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ missionDef, MissionState_Failed, NULL, NULL, 0, mission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchMissionName);
		}
	}

	// Record state change for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionState(ATR_RESULT_SUCCESS, 0, 0, pchMissionName, NULL, newState);

	StructDestroy(parse_WorldVariableArray, pVariableArray);
	TRANSACTION_RETURN_LOG_SUCCESS("Successfully changed state to %s for mission: %s", StaticDefineIntRevLookup(MissionStateEnum, newState), pchMissionName);
}

// Exactly the same as mission_tr_ChangeRootMissionStateNoLocking.  
// This is a separate transaction to make it easier to get stats on missions vs. perks
AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Pugckillcreditlimit, .Pteam.Lastrecruittype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Costumedata.Eaunlockedcostumerefs, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pplayer.Earewardmods, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_ChangePerkRootMissionState(ATR_ARGS, NOCONST(Entity)* ent, 
															 ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
															 CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
															 NOCONST(Guild)* pGuild, 
															 const char* pchMissionName, 
															 int newState, 
															 U32 bForcePermanentComplete,
															 const ItemChangeReason *pReason,
															 GameAccountDataExtract *pExtract)
{
	return mission_tr_ChangeRootMissionState(ATR_PASS_ARGS, ent, eaPets, eaVarContainer, pGuild, /*NULL, */pchMissionName, newState, bForcePermanentComplete, pReason, pExtract, /*bRecordUGCCompletion=*/false);
}

static void mission_ChangeMissionState_CB(TransactionReturnVal* pReturnVal, ChangeMissionStateData* pData)
{
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
		InteractInfo* pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);

		// Refresh temporary powers
		if (pData->pRootDef && pData->pRootDef->missionType == MissionType_Perk)
		{
			if (pEnt && pEnt->pChar)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
				if (character_UpdateTemporaryPowerTrees(entGetPartitionIdx(pEnt), pEnt->pChar))
				{
					character_ResetPowersArray(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
				}
			}
		}
		//Powers refresh
		if (pEnt &&
			((pData->pRootDef && pData->pRootDef->bRefreshCharacterPowersOnComplete)
			|| (pData->pChildDef && pData->pChildDef->bRefreshCharacterPowersOnComplete))
			&& pData->eNewState == MissionState_Succeeded)
		{
			entity_PowerTreeAutoBuy(entGetPartitionIdx(pEnt), pEnt, NULL);
		}

		//Refresh remote contacts
		if (pInteractInfo)
		{
			pInteractInfo->bUpdateRemoteContactsNextTick = true;
			pInteractInfo->bUpdateContactDialogOptionsNextTick = true;
		}
		if (SAFE_MEMBER(pEnt, pSaved))
		{
			entity_SetDirtyBit(pEnt,parse_PlayerCostumeData, &pEnt->pSaved->costumeData, true);
			entity_SetDirtyBit(pEnt,parse_SavedEntityData, pEnt->pSaved, true);
		}
	}
	SAFE_FREE(pData);
}


// This selects between several versions of the Change State transactions to lock as little as possible
// Todo - finer granularity if needed for performance
static void mission_tr_ChangeMissionState(int iPartitionIdx, Mission* mission, MissionState newState, bool bForcePermanentComplete)
{
	MissionDef *missionDef = mission_GetDef(mission);
	Mission *rootMission = mission;
	MissionDef *rootMissionDef = NULL;
	Mission *parentMission = mission->parent;
	while (rootMission->parent)
		rootMission = rootMission->parent;
	rootMissionDef = mission_GetDef(rootMission);

	if (mission && rootMission && missionDef)
	{
		if (mission_IsPersisted(mission) && mission->infoOwner && mission->infoOwner->parentEnt)
		{
			Entity *pEnt = mission->infoOwner->parentEnt;
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			MissionDef *pRootDef = mission_GetDef(rootMission);
			ChangeMissionStateData* pData = mission_CreateChangeMissionStateData(pEnt, pRootDef, missionDef, newState);
			TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-ChangeState", mission->infoOwner->parentEnt, mission_ChangeMissionState_CB, pData);
			ItemChangeReason reason = {0};
			
			bool bMustLockShardVars = missiondef_MustLockShardVariablesForStateChange(missionDef, pRootDef, newState);
			bool bMustLockGuildLog = missiondef_MustLockGuildActivityLogForStateChange(((NONNULL(pEnt->pPlayer)) && NONNULL(pEnt->pPlayer->pGuild)), missionDef, pRootDef, newState);
			if (parentMission)
			{
				if (missiondef_MustLockInventoryForStateChange(missionDef, pRootDef, newState) 
					|| missiondef_MustLockMissionsForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockNemesisForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockNPCEMailForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockActivityLogForStateChange(missionDef, pRootDef, newState)
					|| bMustLockShardVars
					|| bMustLockGuildLog){

					U32* eaPets = NULL;
					U32* eaEmptyList = NULL;

					// Don't pass in pets unless unique items are possible
					if (missiondef_HasUniqueItemsInRewardsForState(missionDef,newState,rootMission->eCreditType)) {
						ea32Create(&eaPets);
						Entity_GetPetIDList(pEnt, &eaPets);
					}
						
					if (pRootDef->missionType == MissionType_Perk){
						inv_FillItemChangeReason(&reason, mission->infoOwner->parentEnt, "Mission:ChangeStatePerkSubMission", mission->missionNameOrig);

						AutoTrans_mission_tr_ChangePerkSubMissionState(returnVal, GetAppGlobalType(), 
							entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt),
							GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
							GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
							GLOBALTYPE_GUILD, bMustLockGuildLog ? mission->infoOwner->parentEnt->pPlayer->pGuild->iGuildID : 0, 
							rootMission->missionNameOrig, parentMission->missionNameOrig, mission->missionNameOrig, 
							newState, bForcePermanentComplete, &reason, pExtract);
						ea32Destroy(&eaPets);
					} else {
						Entity* pParentEnt = mission->infoOwner->parentEnt;
						inv_FillItemChangeReason(&reason, mission->infoOwner->parentEnt, "Mission:ChangeStateSubMission", mission->missionNameOrig);

						AutoTrans_mission_tr_ChangeSubMissionState(returnVal, GetAppGlobalType(), 
							entGetType(pParentEnt), entGetContainerID(pParentEnt), 
							GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
							GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
							GLOBALTYPE_GUILD, bMustLockGuildLog ? mission->infoOwner->parentEnt->pPlayer->pGuild->iGuildID : 0, 
							rootMission->missionNameOrig, parentMission->missionNameOrig, mission->missionNameOrig, 
							newState, bForcePermanentComplete, &reason, pExtract);
						ea32Destroy(&eaPets);
					}

				} else {
					if (pRootDef->missionType == MissionType_Perk){
						AutoTrans_mission_tr_ChangePerkSubMissionStateNoLocking(returnVal, GetAppGlobalType(), entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt), rootMission->missionNameOrig, parentMission->missionNameOrig, mission->missionNameOrig, newState, bForcePermanentComplete);
					} else {
						AutoTrans_mission_tr_ChangeSubMissionStateNoLocking(returnVal, GetAppGlobalType(), entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt), rootMission->missionNameOrig, parentMission->missionNameOrig, mission->missionNameOrig, newState, bForcePermanentComplete);
					}
				}
			}
			else
			{
				bool bRecordUGCCompletion = false;
				if(!isProductionEditMode() && pRootDef->ugcProjectID)
				{
					char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];
					if(resExtractNameSpace(pRootDef->pchRefString, pchNameSpace, pchBase) && 0 == stricmp(pchBase, "Mission"))
					{
						bRecordUGCCompletion = gConf.uUGCHoursBetweenRecordingCompletionForEntity
							? !entity_HasRecentlyCompletedUGCProject(pEnt, pRootDef->ugcProjectID, 3600 * gConf.uUGCHoursBetweenRecordingCompletionForEntity)
							: true;
					}
				}

				if (missiondef_MustLockInventoryForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockMissionsForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockNemesisForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockNPCEMailForStateChange(missionDef, pRootDef, newState)
					|| missiondef_MustLockActivityLogForStateChange(missionDef, pRootDef, newState)
					|| bMustLockShardVars
					|| bMustLockGuildLog)
				{
					U32* eaPets = NULL;
					U32* eaEmptyList = NULL;

					// Don't pass in pets unless unique items are possible
					if (missiondef_HasUniqueItemsInRewardsForState(missionDef,newState,rootMission->eCreditType)) {
						ea32Create(&eaPets);
						Entity_GetPetIDList(pEnt, &eaPets);
					}

					if (pRootDef->missionType == MissionType_Perk){
						inv_FillItemChangeReason(&reason, mission->infoOwner->parentEnt, "Mission:ChangeStatePerk", mission->missionNameOrig);

						AutoTrans_mission_tr_ChangePerkRootMissionState(returnVal, GetAppGlobalType(), 
							entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt),
							GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
							GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
							GLOBALTYPE_GUILD, bMustLockGuildLog ? mission->infoOwner->parentEnt->pPlayer->pGuild->iGuildID : 0, 
							mission->missionNameOrig, newState, bForcePermanentComplete, &reason, pExtract);
					} else {
						inv_FillItemChangeReason(&reason, mission->infoOwner->parentEnt, "Mission:ChangeState", mission->missionNameOrig);

						AutoTrans_mission_tr_ChangeRootMissionState(returnVal, GetAppGlobalType(),
							entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt), 
							GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
							GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
							GLOBALTYPE_GUILD, bMustLockGuildLog ? mission->infoOwner->parentEnt->pPlayer->pGuild->iGuildID : 0, 
							mission->missionNameOrig, newState, bForcePermanentComplete, &reason, pExtract, bRecordUGCCompletion);
					}
					ea32Destroy(&eaPets);
				} else {
					if (pRootDef->missionType == MissionType_Perk){
						AutoTrans_mission_tr_ChangePerkRootMissionStateNoLocking(returnVal, GetAppGlobalType(), entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt), mission->missionNameOrig, newState, bForcePermanentComplete);
					} else {
						AutoTrans_mission_tr_ChangeRootMissionStateNoLocking(returnVal, GetAppGlobalType(), entGetType(mission->infoOwner->parentEnt), entGetContainerID(mission->infoOwner->parentEnt), mission->missionNameOrig, newState, bForcePermanentComplete, bRecordUGCCompletion);
					}
				}
			}
		}
		else
		{
			if (mission_GetType(mission) == MissionType_OpenMission)
			{
				mission_np_ChangeMissionState(iPartitionIdx, mission, newState, bForcePermanentComplete);
			}
			else if (mission->infoOwner && mission->infoOwner->parentEnt)
			{
				mission_tr_PersistMissionAndChangeState(mission->infoOwner->parentEnt, mission, newState, bForcePermanentComplete);
			}
		}
	}
}

// Convenience functions
void mission_tr_CompleteMission(int iPartitionIdx, Mission* mission, bool bForcePermanentComplete)
{
	mission_tr_ChangeMissionState(iPartitionIdx, mission, MissionState_Succeeded, bForcePermanentComplete);
}

void mission_tr_FailMission(int iPartitionIdx, Mission* mission)
{
	mission_tr_ChangeMissionState(iPartitionIdx, mission, MissionState_Failed, false);
}

void mission_tr_UncompleteMission(int iPartitionIdx, Mission* mission)
{
	mission_tr_ChangeMissionState(iPartitionIdx, mission, MissionState_InProgress, false);
}

// ----------------------------------------------------------------------------------
// Transaction to reset the player's MissionInfo
// ----------------------------------------------------------------------------------

// this is an internal callback made when the transaction completes
static void MIPT_Reset(TransactionReturnVal* returnVal, void *entRef)
{
	Entity* playerEnt = entFromEntityRefAnyPartition(PTR_TO_U32(entRef));
	if (playerEnt && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		MissionInfo* info = mission_GetInfoFromPlayer(playerEnt);
		int iPartitionIdx = entGetPartitionIdx(playerEnt);
		int i;

		for(i=eaSize(&info->eaDiscoveredMissions)-1; i>=0; --i) {
			mission_PreMissionDestroyDeinitRecursive(iPartitionIdx, info->eaDiscoveredMissions[i]);
		}
		eaClearStruct(&info->eaDiscoveredMissions, parse_Mission);
		mission_ClearPlaceholderPlayerStats(playerEnt);
		mission_GrantOnEnterMissions(playerEnt, info);
		mission_GrantPerkMissions(iPartitionIdx, info);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Missioninfo, .Pplayer.Pprogressioninfo");
enumTransactionOutcome trResetMissionInfo(ATR_ARGS, NOCONST(Entity)* ent)
{
	StructResetNoConst(parse_MissionInfo, ent->pPlayer->missionInfo);

	// Also resets the progression information
	StructResetNoConst(parse_ProgressionInfo, ent->pPlayer->pProgressionInfo);

	TRANSACTION_RETURN_LOG_SUCCESS("Mission Info Reset");
}

// Call this to initiate the transaction
void missioninfo_ResetMissionInfo(Entity* playerEnt)
{
	MissionInfo* info = mission_GetInfoFromPlayer(playerEnt);
	if (info)
	{
		TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-ResetMissionInfo", playerEnt, MIPT_Reset, U32_TO_PTR(entGetRef(playerEnt)));
		AutoTrans_trResetMissionInfo(returnVal, GetAppGlobalType(), entGetType(playerEnt), entGetContainerID(playerEnt));
	}
}

// ----------------------------------------------------------------------------------
// Transaction helper to find a Mission, or create it if it doesn't exist
// ----------------------------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Missioninfo.Missions, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, pPlayer.missionInfo.eaMissionCooldowns[], .Pchar.Ilevelexp, \
.Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode");
NOCONST(Mission)* mission_trh_GetOrCreateMission(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, const char *pchRootMissionName, const char *pchMissionName, int iMissionLevel, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Mission) *pRootMission = NULL;
	NOCONST(Mission) *pMission = NULL;

	pRootMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchRootMissionName);
	if (!pRootMission)
	{
		PERFINFO_AUTO_START("AddMission",1);
		// Add Mission if it doesn't exist
		if (mission_trh_AddMissionNoLocking(ATR_PASS_ARGS, ent, pchRootMissionName, iMissionLevel, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			PERFINFO_AUTO_STOP();
			return NULL;
		}
		PERFINFO_AUTO_STOP();
		pRootMission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchRootMissionName);
	}

	if (!pRootMission)
		return NULL;

	// Find the correct child mission
	if (!pchMissionName || !stricmp(pchRootMissionName, pchMissionName))
		pMission = pRootMission;
	else
		pMission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, pRootMission), pchMissionName));

	return pMission;
}

// ----------------------------------------------------------------------------------
// Transactions to both add a Mission and immediately set an EventCount
// ----------------------------------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Missions, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, .Psaved.Conowner.Containerid, pPlayer.missionInfo.eaMissionCooldowns[], .Pchar.Ilevelexp, \
.pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode");
enumTransactionOutcome mission_tr_AddMissionAndUpdateEventCount(ATR_ARGS, NOCONST(Entity)* ent, const char* pchRootMissionName, const char *pchMissionName, int iMissionLevel, const char *pchEventName, int iEventCount, int bSet, int bDiscovered, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Mission) *pMission = NULL;
	NOCONST(MissionEventContainer) *pEventContainer = NULL;
	MissionDef *pDef = NULL;

	if (!(pMission = mission_trh_GetOrCreateMission(ATR_PASS_ARGS, ent, pchRootMissionName, pchMissionName, iMissionLevel, pReason, pExtract))){
		if (pchMissionName && stricmp(pchMissionName, pchRootMissionName)){
			TRANSACTION_RETURN_LOG_FAILURE("Could not find or create mission: %s::%s", pchRootMissionName, pchMissionName);
		} else {
			TRANSACTION_RETURN_LOG_FAILURE("Could not find or create mission: %s", pchRootMissionName);
		}
	}
	if (bDiscovered)
		QueueRemoteCommand_mission_RemoteDiscoverMission(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName);
	QueueRemoteCommand_mission_RemoteUpdateEventCount(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName, pchMissionName, pchEventName, iEventCount, bSet);

	pDef = mission_GetOrigDef((Mission*)pMission);
	if (pDef)
		QueueRemoteCommand_mission_RemoteMissionFlagAsNeedingEval(ATR_RESULT_SUCCESS, 0, 0, pDef->pchRefString, false);

	TRANSACTION_RETURN_LOG_SUCCESS("Success");
}

void mission_tr_PersistMissionAndUpdateEventCount(Entity *pEnt, Mission *pMission, const char *pchEventName, int iEventCount, bool bSet)
{
	MissionDef *pDef = mission_GetDef(pMission);
	Mission *pRootMission = pMission;
	TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-PersistAndUpdateEventCount", pEnt, NULL, NULL);

	while (pRootMission && pRootMission->parent)
	{
		pRootMission = pRootMission->parent;
	}

	if (pMission && pRootMission)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int iMissionLevel = missiondef_CalculateLevel(entGetPartitionIdx(pEnt), entity_GetSavedExpLevel(pEnt), pDef);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Mission:PersistPerk", NULL);

		AutoTrans_mission_tr_AddMissionAndUpdateEventCount(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pRootMission->missionNameOrig, pMission->missionNameOrig, iMissionLevel, pchEventName, 
				iEventCount, (bSet?1:0), pRootMission->bDiscovered, &reason, pExtract);
	}
}


// ----------------------------------------------------------------------------------
// Transactions to both add a Mission and immediately change state
// ----------------------------------------------------------------------------------

AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pteam.Lastrecruittype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Costumedata.Eaunlockedcostumerefs, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pplayer.Earewardmods, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_AddMissionAndChangeRootMissionState(ATR_ARGS, NOCONST(Entity)* ent, 
																	  ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
																	  CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
																	  NOCONST(Guild)* pGuild, 
																	  const char *pchRootMissionName,
																	  int iMissionLevel,
																	  int eNewState, 
																	  U32 bForcePermanentComplete,
																	  int bDiscovered,
																	  const ItemChangeReason *pReason,
																	  GameAccountDataExtract *pExtract)
{
	WorldVariableArray *pVariableArray = NULL;
	NOCONST(Mission) *pRootMission = NULL;
	MissionDef *pRootDef = NULL;

	// Make sure the MissionDef exists
	if (!(pRootDef = RefSystem_ReferentFromString(g_MissionDictionary, pchRootMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s", pchRootMissionName);

	if (!(pRootMission = mission_trh_GetOrCreateMission(ATR_PASS_ARGS, ent, pchRootMissionName, NULL, iMissionLevel, pReason, pExtract))){
		TRANSACTION_RETURN_LOG_FAILURE("Could not find or create mission: %s", pchRootMissionName);
	}

	// Change the Mission's state
	mission_trh_ChangeMissionState(ATR_PASS_ARGS, ent, pRootMission, pRootDef, eNewState, bForcePermanentComplete, true);

	// Create variable array
	pVariableArray = mission_trh_CreateVariableArray(pRootMission);

	// Run all Actions and grant Rewards
	if (eNewState == MissionState_Succeeded)
	{
		if (gameaction_trh_RunActionsLockAll(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, &pRootDef->ppSuccessActions, pVariableArray, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not perform all actions: %s", pchRootMissionName);
		}

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ pRootDef, MissionState_Succeeded, NULL, NULL, 0, pRootMission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchRootMissionName);
		}
	}
	else if (eNewState == MissionState_Failed)
	{
		if (gameaction_trh_RunActionsLockAll(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, &pRootDef->ppFailureActions, pVariableArray, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not perform all actions: %s", pchRootMissionName);
		}

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ pRootDef, MissionState_Failed, NULL, NULL, 0, pRootMission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
		{
			StructDestroy(parse_WorldVariableArray, pVariableArray);
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchRootMissionName);
		}
	}

	// Set discovered flag
	if (bDiscovered)
		QueueRemoteCommand_mission_RemoteDiscoverMission(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName);

	// Record state change for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionState(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName, NULL, eNewState);

	StructDestroy(parse_WorldVariableArray, pVariableArray);
	TRANSACTION_RETURN_LOG_SUCCESS("Success");
}

AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.pEmailV2.Mail, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Unextrequestid, .Pteam.Iteamid, .Pteam.Estate, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome mission_tr_AddMissionAndChangeSubMissionState(ATR_ARGS, NOCONST(Entity)* ent, 
																	 CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
																	 CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer,
																	 NOCONST(Guild) *pGuild, 
																	 const char* pchRootMissionName, 
																	 const char* pchParentMissionName, 
																	 const char *pchMissionName, 
																	 int iMissionLevel,
																	 int eNewState, 
																	 U32 bForcePermanentComplete,
																	 int bDiscovered,
																	 const ItemChangeReason *pReason,
																	 GameAccountDataExtract *pExtract)
{
	NOCONST(Mission) *pRootMission = NULL;
	NOCONST(Mission) *pParentMission = NULL;
	NOCONST(Mission) *pMission = NULL;
	MissionDef *pRootDef = NULL;
	MissionDef *pParentDef = NULL;
	MissionDef *pDef = NULL;

	// Make sure the Root MissionDef exists
	if (!(pRootDef = RefSystem_ReferentFromString(g_MissionDictionary, pchRootMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s", pchRootMissionName);

	// Make sure the MissionDef exists
	if (!(pDef = missiondef_ChildDefFromName(pRootDef, pchMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s::%s", pchRootMissionName, pchMissionName);

	// Make sure the parent MissionDef exists
	if (!stricmp(pchParentMissionName, pchRootMissionName))
		pParentDef = pRootDef;
	else if (!(pParentDef = missiondef_ChildDefFromName(pRootDef, pchParentMissionName)))
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found for mission: %s::%s", pchRootMissionName, pchParentMissionName);

	// Find the root Mission
	if (!(pRootMission = mission_trh_GetOrCreateMission(ATR_PASS_ARGS, ent, pchRootMissionName, NULL, iMissionLevel, pReason, pExtract))){
		TRANSACTION_RETURN_LOG_FAILURE("Could not find or create mission: %s", pchRootMissionName);
	}

	// Find the child Mission
	pMission = pRootMission;
	if (pMission && pchMissionName && stricmp(pchRootMissionName, pchMissionName)){
		if (!(pMission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, pMission), pchMissionName)))){
			TRANSACTION_RETURN_LOG_FAILURE("Could not find or create mission: %s::%s", pchRootMissionName, pchMissionName);
		}
	}
	
	// Find the Parent mission
	pParentMission = pRootMission;
	if (pParentMission && pchParentMissionName && stricmp(pchRootMissionName, pchParentMissionName)){
		if (!(pParentMission = CONTAINER_NOCONST(Mission, mission_FindChildByName(CONTAINER_RECONST(Mission, pParentMission), pchParentMissionName)))){
			TRANSACTION_RETURN_LOG_FAILURE("Could not find or create mission: %s::%s", pchRootMissionName, pchParentMissionName);
		}
	}

	// Change the Mission's state
	mission_trh_ChangeMissionState(ATR_PASS_ARGS, ent, pMission, pDef, eNewState, bForcePermanentComplete, false);

	// Run actions and grant rewards
	if (eNewState == MissionState_Succeeded)
	{
		if (gameaction_trh_RunActionsSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, pParentMission, pParentDef, &pDef->ppSuccessActions, NULL, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s::%s", pchRootMissionName, pchMissionName);

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ pDef, MissionState_Succeeded, NULL, NULL, 0, pMission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchMissionName);
	}
	else if (eNewState == MissionState_Failed)
	{
		if (gameaction_trh_RunActionsSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, pParentMission, pParentDef, &pDef->ppFailureActions, NULL, NULL, NULL, NULL, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission's children could not be granted: %s::%s", pchRootMissionName, pchMissionName);

		if (mission_trh_GrantMissionRewards(ATR_PASS_ARGS, ent, eaPets, /*NULL,*/ pDef, MissionState_Failed, NULL, NULL, 0, pMission->iLevel, 0, 0, false, pReason, pExtract, false, false, false, false, 0) != TRANSACTION_OUTCOME_SUCCESS)
			TRANSACTION_RETURN_LOG_FAILURE("Mission could not grant rewards: %s", pchMissionName);
	}

	// Set discovered flag
	if (bDiscovered)
		QueueRemoteCommand_mission_RemoteDiscoverMission(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName);

	// Record state change for user experience system
	QueueRemoteCommand_UserExp_RemoteLogMissionState(ATR_RESULT_SUCCESS, 0, 0, pchRootMissionName, pchMissionName, eNewState);

	TRANSACTION_RETURN_LOG_SUCCESS("Success");
}

static void mission_tr_PersistMissionAndChangeState(Entity *pEnt, Mission *pMission, MissionState eNewState, bool bForcePermanentComplete)
{
	MissionDef *pDef = mission_GetDef(pMission);
	MissionDef *pRootDef;
	Mission *pRootMission = pMission;
	Mission *pParentMission = pMission ? pMission->parent : NULL;

	while (pRootMission && pRootMission->parent)
		pRootMission = pRootMission->parent;
	pRootDef = mission_GetDef(pRootMission);

	if (pMission && pRootMission)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		U32* eaPets = NULL;
		U32* eaEmptyList = NULL;
		TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("Mission-PersistAndChangeState", pEnt, NULL, NULL);
		bool bMustLockShardVars = missiondef_MustLockShardVariablesForStateChange(pDef, pRootDef, eNewState);
		bool bMustLockGuildLog = missiondef_MustLockGuildActivityLogForStateChange(((NONNULL(pEnt->pPlayer)) && NONNULL(pEnt->pPlayer->pGuild)), pDef, pRootDef, eNewState);
		ContainerID iGuildID = 0;
		int iMissionLevel = missiondef_CalculateLevel(entGetPartitionIdx(pEnt), entity_GetSavedExpLevel(pEnt), pDef);
		ItemChangeReason reason = {0};

		if ( bMustLockGuildLog )
		{
			// the assert is to keep the compiler happy.  We know they are non-null because bMustLockGuildLog
			//  will only be set if the player has a guild.
			devassert(NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pGuild));
			iGuildID = pEnt->pPlayer->pGuild->iGuildID;
		}

		// Don't pass in pets unless unique items are possible
		if (missiondef_HasUniqueItemsInRewardsForState(pDef,eNewState,pRootMission->eCreditType)) {
			ea32Create(&eaPets);
			Entity_GetPetIDList(pEnt, &eaPets);
		}

		if (pMission == pRootMission)
		{
			inv_FillItemChangeReason(&reason, pEnt, "Mission:PersistPerk", pMission->missionNameOrig);

			AutoTrans_mission_tr_AddMissionAndChangeRootMissionState(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt),
					GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
					GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
					GLOBALTYPE_GUILD, iGuildID, 
					pMission->missionNameOrig, iMissionLevel, eNewState, bForcePermanentComplete, pRootMission->bDiscovered, 
					&reason, pExtract);
		}
		else if (pParentMission)
		{
			inv_FillItemChangeReason(&reason, pEnt, "Mission:PersistPerk", pMission->missionNameOrig);

			AutoTrans_mission_tr_AddMissionAndChangeSubMissionState(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
					GLOBALTYPE_SHARDVARIABLE, bMustLockShardVars ? shardVariable_GetContainerIDList() : &eaEmptyList,
					GLOBALTYPE_GUILD, iGuildID, 
					pRootMission->missionNameOrig, pParentMission->missionNameOrig, pMission->missionNameOrig, 
					iMissionLevel, eNewState, bForcePermanentComplete, pRootMission->bDiscovered, 
					&reason, pExtract);
		}
		ea32Destroy(&eaPets);
	}
}

// ----------------------------------------------------------------------------------
// Transaction to change the mission being requested by a Mission Request
// ----------------------------------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pPlayer.missionInfo.eaMissionRequests[]");
enumTransactionOutcome missionrequest_tr_SetRequestedMission(ATR_ARGS, NOCONST(Entity)* pEnt, U32 uRequestID, const char *pchNewMissionName)
{
	MissionDef *pNewDef = RefSystem_ReferentFromString(g_MissionDictionary, pchNewMissionName);
	NOCONST(MissionRequest) *pRequest = eaIndexedGetUsingInt(&pEnt->pPlayer->missionInfo->eaMissionRequests, uRequestID);

	// Make sure the MissionDef exists
	if (!pNewDef){
		TRANSACTION_RETURN_LOG_FAILURE("No MissionDef could be found named: %s", pchNewMissionName);
	}

	if (!pRequest){
		TRANSACTION_RETURN_LOG_FAILURE("No Mission Request could be found with ID: %u", uRequestID);
	}

	SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pNewDef, pRequest->hRequestedMission);
	QueueRemoteCommand_mission_RemoteFlagMissionRequestUpdate(ATR_RESULT_SUCCESS, 0, 0);
	TRANSACTION_RETURN_LOG_SUCCESS("Mission Request successfully updated");
}

void missionrequest_SetRequestedMission(Entity* pEnt, U32 uRequestID, MissionDef *pNewDef)
{
	if (pEnt && pNewDef){
		TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("MissionRequest-SetRequestedMission", pEnt, NULL, NULL);
		AutoTrans_missionrequest_tr_SetRequestedMission(returnVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), uRequestID, pNewDef->name);
	}
}

// ----------------------------------------------------------------------------------
// Transaction to force a Mission Request to succeed
// ----------------------------------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pPlayer.missionInfo.eaMissionRequests[]");
enumTransactionOutcome missionrequest_tr_ForceComplete(ATR_ARGS, NOCONST(Entity)* pEnt, U32 uRequestID)
{
	NOCONST(MissionRequest) *pRequest = eaIndexedGetUsingInt(&pEnt->pPlayer->missionInfo->eaMissionRequests, uRequestID);

	if (!pRequest){
		TRANSACTION_RETURN_LOG_FAILURE("No Mission Request could be found with ID: %u", uRequestID);
	}

	pRequest->eState = MissionRequestState_Succeeded;
	QueueRemoteCommand_mission_RemoteMissionFlagAsNeedingEval(ATR_RESULT_SUCCESS, 0, 0, pRequest->pchRequesterRef, false);

	TRANSACTION_RETURN_LOG_SUCCESS("Mission Request successfully updated");
}

void missionrequest_ForceComplete(Entity* pEnt, U32 uRequestID)
{
	if (pEnt){
		TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("MissionRequest-ForceComplete", pEnt, NULL, NULL);
		AutoTrans_missionrequest_tr_ForceComplete(returnVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), uRequestID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Missioninfo.Missions");
enumTransactionOutcome mission_tr_CreateUGCMissionData(ATR_ARGS, NOCONST(Entity)* pEnt, const char *strNameSpace, S32 iCreatedByAccount, S32 iStatsQualifyForUGCRewards,
	U32 iNumberOfPlays, F32 fAverageDurationInMinutes, S32 iProjectIsFeatured, S32 iProjectWasFeatured)
{
	if(strNameSpace)
	{
		FOR_EACH_IN_EARRAY(pEnt->pPlayer->missionInfo->missions, NOCONST(Mission), pMission)
		{
			char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];
			resExtractNameSpace(pMission->missionNameOrig, pchNameSpace, pchBase);

			if(0 == stricmp(strNameSpace, pchNameSpace))
			{
				if(!pMission->pUGCMissionData)
					pMission->pUGCMissionData = StructCreateNoConst(parse_UGCMissionData);

				pMission->pUGCMissionData->bCreatedByAccount = !!iCreatedByAccount;
				pMission->pUGCMissionData->bStatsQualifyForUGCRewards = !!iStatsQualifyForUGCRewards;
				pMission->pUGCMissionData->iNumberOfPlays = iNumberOfPlays;
				pMission->pUGCMissionData->fAverageDurationInMinutes = fAverageDurationInMinutes;
				pMission->pUGCMissionData->bProjectIsFeatured = !!iProjectIsFeatured;
				pMission->pUGCMissionData->bProjectWasFeatured = !!iProjectWasFeatured;

				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
		FOR_EACH_END;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Missioninfo.Missions");
enumTransactionOutcome mission_tr_SetUGCPlayingAsBetaReviewer(ATR_ARGS, NOCONST(Entity)* pEnt, const char *strNameSpace, int bPlayingAsBetaReviewer)
{
	if(strNameSpace)
	{
		FOR_EACH_IN_EARRAY(pEnt->pPlayer->missionInfo->missions, NOCONST(Mission), pMission)
		{
			char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];
			resExtractNameSpace(pMission->missionNameOrig, pchNameSpace, pchBase);

			if(0 == stricmp(strNameSpace, pchNameSpace))
			{
				if(!pMission->pUGCMissionData)
					pMission->pUGCMissionData = StructCreateNoConst(parse_UGCMissionData);

				pMission->pUGCMissionData->bPlayingAsBetaReviewer = bPlayingAsBetaReviewer;

				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
		FOR_EACH_END;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

void missioninfo_AddMission_Fail(TransactionReturnCallback cb, UserData data)
{
	if(cb)
	{
		TransactionReturnVal val;
		val.eOutcome = TRANSACTION_OUTCOME_FAILURE;
		cb(&val, data);
	}
}

#include "AutoGen/gslMission_transact_c_ast.c"
