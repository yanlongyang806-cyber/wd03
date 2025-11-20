/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityCommon.h"
#include "aiLib.h"
#include "AutoTransDefs.h"
#include "beacon.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Character_target.h"
#include "cmdservercharacter.h"
#include "contact_common.h"
#include "entCritter.h"
#include "entdebugmenu.h"
#include "Entity.h"
#include "EntityGrid.h"
#include "entityiterator.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "error.h"
#include "estring.h"
#include "expression.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gametimer.h"
#include "gslActivity.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslEntity.h"
#include "gslGameAction.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslLandmark.h"
#include "gslMapReveal.h"
#include "gslMapState.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslMissionDebug.h"
#include "gslMissionDrop.h"
#include "gslMissionEvents.h"
#include "gslMissionLockout.h"
#include "gslMissionRequest.h"
#include "gslMissionTemplate.h"
#include "gslMission_transact.h"
#include "gslMissionSet.h"
#include "gslNamedPoint.h"
#include "gslNotify.h"
#include "gslOldEncounter.h"
#include "gslOpenMission.h"
#include "gslPartition.h"
#include "gslPlayerMatchStats.h"
#include "gslPlayerStats.h"
#include "gslResourceDBSupport.h"
#include "gslSendToClient.h"
#include "gslSocial.h"
#include "gslSpawnPoint.h"
#include "gslTriggerCondition.h"
#include "gslUGC.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "gslWorldVariable.h"
#include "Guild.h"
#include "interaction_common.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "logging.h"
#include "mapstate_common.h"
#include "MapDescription.h"
#include "MapRevealCommon.h"
#include "Message.h"
#include "mission_common.h"
#include "NameList.h"
#include "nemesis.h"
#include "nemesis_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "playerstats_common.h"
#include "progression_common.h"
#include "Quat.h"
#include "qsortG.h"
#include "rand.h"
#include "RegionRules.h"
#include "ResourceInfo.h"
#include "ResourceManager.h"
#include "Reward.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "survey.h"
#include "Team.h"
#include "timedeventqueue.h"
#include "timing.h"
#include "ugcprojectcommon.h"
#include "wcoll/collcache.h"
#include "wlBeacon.h"
#include "worldgrid.h"
#include "gslUGC_cmd.h"
#include "UGCProjectUtils.h"
#include "gslTeamCorral.h"
#include "utilitiesLib.h"

#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/MapDescription_h_ast.h"
#include "Entity_h_ast.h"
#include "mission_enums_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/playerstats_common_h_ast.h"

#include "AutoGen/AppserverLib_autogen_remotefuncs.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"

#include "gslMission_h_ast.h"

// ----------------------------------------------------------------------------------
// Type definitions and forward declarations
// ----------------------------------------------------------------------------------

#define MISSION_SYSTEM_TICK	5

// Mission Sharing defines
#define MISSION_SHARING_DIST_DEFAULT 500
#define MAX_SHARED_MISSIONS 25
#define MISSION_SHARING_TIMEOUT_SEC 30

#define ACTIVITY_UPDATE_FREQ 60

#define MISSION_OLDVERSIONERROR_MSG            "MissionSystem.MissionOldVersionError"
#define MISSION_NEMESISARCOLDVERSIONERROR_MSG  "MissionSystem.NemesisArcOldVersionError"
#define MISSION_FLASHBACKINVALIDMAPERROR_MSG   "MissionSystem.FlashbackInvalidMapError"
#define MISSION_SHARING_ACCEPTED_MSG           "MissionSystem.Sharing.Accepted"
#define MISSION_SHARING_DECLINED_MSG           "MissionSystem.Sharing.Declined"
#define MISSION_SHARING_NOTSHAREABLE_MSG       "MissionSystem.Sharing.MissionNotShareable"
#define MISSION_SHARING_TIMEEXPIRED_MSG        "MissionSystem.Sharing.MissionTimeExpired"
#define MISSION_SHARING_LOCKOUTEXPIRED_MSG     "MissionSystem.Sharing.MissionLockoutExpired"
#define MISSION_SHARING_ALREADYOFFERED_MSG     "MissionSystem.Sharing.AlreadyBeingOffered"
#define MISSION_SHARING_BUSY_MSG               "MissionSystem.Sharing.Busy"
#define MISSION_SHARING_OFFERED_MSG            "MissionSystem.Sharing.Offered"
#define MISSION_SHARING_HASMISSION_MSG         "MissionSystem.Sharing.AlreadyHasMission"
#define MISSION_SHARING_HASCOMPLETED_MSG       "MissionSystem.Sharing.AlreadyCompletedMission"
#define MISSION_SHARING_SECONDARYCOOLDOWN_MSG  "MissionSystem.Sharing.SecondaryCooldown"
#define MISSION_SHARING_TOOLOWLEVEL_MSG        "MissionSystem.Sharing.TooLowLevel"
#define MISSION_SHARING_INELIGIBLE_MSG         "MissionSystem.Sharing.DoesNotMeetRequirements"
#define MISSION_SHARING_OUTOFRANGE_MSG         "MissionSystem.Sharing.OutOfRange"


bool g_bHasMadLibsMissions = false;

// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

static U32 s_MissionTick = 0;

// Defined in mission_common.c
extern const char** g_eaMissionsWaitingForOverrideProcessing;

MissionConfig g_MissionConfig;

// ----------------------------------------------------------------------------------
// Mission Def Utilities
// ----------------------------------------------------------------------------------

static bool missiondef_HasOnStartRewardsRecursive(MissionDef *pDef)
{
	int i;
	if (pDef) {
		if (pDef->params && pDef->params->OnstartRewardTableName) {
			return true;
		}
		for (i = 0; i < eaSize(&pDef->ppOnStartActions); i++) {
			if ((pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_GrantSubMission) && 
				(pDef->ppOnStartActions[i]->pGrantSubMissionProperties)) {
				const char *pcSubMissionName = pDef->ppOnStartActions[i]->pGrantSubMissionProperties->pcSubMissionName;
				if (pcSubMissionName) {
					MissionDef *pChildDef = missiondef_FindMissionByName(pDef, pcSubMissionName);
					if (pChildDef && missiondef_HasOnStartRewardsRecursive(pChildDef)) {
						return true;
					}
				}
			}
		}
	}
	return false;
}


bool missiondef_HasOnStartTimerRecursive(MissionDef *pDef)
{
	int i;
	if (pDef->uTimeout) {
		return true;
	}
	for (i = 0; i < eaSize(&pDef->ppOnStartActions); i++) {
		if ((pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_GrantSubMission) && 
			(pDef->ppOnStartActions[i]->pGrantSubMissionProperties)) {
			const char *pcSubMissionName = pDef->ppOnStartActions[i]->pGrantSubMissionProperties->pcSubMissionName;
			if (pcSubMissionName) {
				MissionDef *pChildDef = missiondef_FindMissionByName(pDef, pcSubMissionName);
				if (pChildDef && missiondef_HasOnStartTimerRecursive(pChildDef)) {
					return true;
				}
			}
		}
	}
	return false;
}


U32 missiondef_OnStartTimerLengthRecursive(MissionDef *pDef)
{
	U32 uMinTimeout = pDef->uTimeout;
	int i;
	for (i = 0; i < eaSize(&pDef->ppOnStartActions); i++) {
		if ((pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_GrantSubMission) && 
			(pDef->ppOnStartActions[i]->pGrantSubMissionProperties)) {
			const char *pcSubMissionName = pDef->ppOnStartActions[i]->pGrantSubMissionProperties->pcSubMissionName;
			if (pcSubMissionName){
				MissionDef *pChildDef = missiondef_FindMissionByName(pDef, pcSubMissionName);
				if (pChildDef) {
					U32 uChildTimeout = missiondef_OnStartTimerLengthRecursive(pChildDef);
					if (uChildTimeout && (!uMinTimeout ||  uChildTimeout < uMinTimeout)) {
						uMinTimeout = uChildTimeout;
					}
				}
			}
		}
	}
	return uMinTimeout;
}

static bool missiondef_CheckPlayerAllegiance_RequireAll(MissionDef* pMissionDef, AllegianceDef* pEntAllegiance, AllegianceDef* pEntSubAllegiance)
{
	AllegianceDef* pRequiredAllegiance = NULL;
	bool bAllegianceMatched = false;
	bool bSubAllegianceMatched = pEntSubAllegiance ? false : true;
	int i;

	for (i = eaSize(&pMissionDef->eaRequiredAllegiances)-1; i >= 0; i--)
	{
		pRequiredAllegiance = GET_REF(pMissionDef->eaRequiredAllegiances[i]->hDef);
		if (pEntAllegiance == pRequiredAllegiance)
		{
			bAllegianceMatched = true;
		}
		else if (pEntSubAllegiance == pRequiredAllegiance)
		{
			bSubAllegianceMatched = true;
		}
		else
		{
			return false;
		}
	}

	return (bAllegianceMatched && bSubAllegianceMatched);
}

static bool missiondef_CheckPlayerAllegiance(MissionDef* pMissionDef, Entity* pEnt)
{
	AllegianceDef* pEntAllegiance = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef* pEntSubAllegiance = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;

	if (!eaSize(&pMissionDef->eaRequiredAllegiances))
	{
		return true;
	}

	if (pMissionDef->bRequireAllAllegiances)
	{
		return missiondef_CheckPlayerAllegiance_RequireAll(pMissionDef, pEntAllegiance, pEntSubAllegiance);
	}
	else
	{
		AllegianceDef* pRequiredAllegiance = NULL;
		int i;

		for (i = eaSize(&pMissionDef->eaRequiredAllegiances)-1; i >= 0; i--)
		{
			pRequiredAllegiance = GET_REF(pMissionDef->eaRequiredAllegiances[i]->hDef);
			if (pEntAllegiance == pRequiredAllegiance || pEntSubAllegiance == pRequiredAllegiance)
			{
				return true;
			}
		}
	}

	return false;
}

bool missiondef_CheckRequiredActivitiesActive(MissionDef* pMissionDef)
{
	if (eaSize(&pMissionDef->ppchRequiresAnyActivities) || eaSize(&pMissionDef->ppchRequiresAllActivities))
	{
		int i;
		for (i = eaSize(&pMissionDef->ppchRequiresAnyActivities)-1; i >= 0; i--)
		{
			if (gslActivity_IsActive(pMissionDef->ppchRequiresAnyActivities[i]))
			{
				return true;
			}
		}
		if (eaSize(&pMissionDef->ppchRequiresAllActivities))
		{
			for (i = eaSize(&pMissionDef->ppchRequiresAllActivities)-1; i >= 0; i--)
			{
				if (!gslActivity_IsActive(pMissionDef->ppchRequiresAllActivities[i]))
				{
					break;
				}
			}
			if (i < 0)
			{
				return true;
			}
		}
		return false;
	}
	return true;
}

void gslMission_NotifyActivityEnded(ActivityDef* pActivityDef)
{
	EntityIterator* pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* pCurrEnt = NULL;

	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		MissionInfo* pInfo = mission_GetInfoFromPlayer(pCurrEnt);
		if (pInfo) {
			int i;
			for (i = eaSize(&pActivityDef->ppchDependentMissionDefs)-1; i >= 0; i--) {
				const char* pchMissionDef = pActivityDef->ppchDependentMissionDefs[i];
				Mission* pMission = mission_FindMissionFromRefString(pInfo, pchMissionDef);

				// If the mission is in progress, flag as needing eval
				if (pMission && pMission->state == MissionState_InProgress) {
					mission_FlagAsNeedingEval(pMission);
				}
			}
		}
	}
	EntityIteratorRelease(pIter);
}

bool missiondef_CanBeOfferedAtAll(Entity *pPlayerEnt, MissionDef *pMissionDef, int *piNextOfferLevel, MissionOfferStatus *pOfferStatus, MissionCreditType *pCreditType)
{
	int iPlayerLevel;
	int iPartitionIdx;
	int iOfferLevel;
	MissionInfo *pInfo;
	Mission *pExistingMission;
	PlayerDebug *pDebug;
	MissionCreditType eCreditType = MissionCreditType_Primary;

	PERFINFO_AUTO_START_FUNC();

	if (pOfferStatus) {
		*pOfferStatus = MissionOfferStatus_OK;
	}
	if (pCreditType) {
		*pCreditType = -1;
	}
	
	if (!pMissionDef || !pPlayerEnt) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	
	// The player already has the Mission
	pExistingMission = mission_GetMissionFromDef(pInfo, pMissionDef);
	if (pExistingMission && (pExistingMission->state != MissionState_Failed)) {
		if (pOfferStatus) {
			*pOfferStatus = MissionOfferStatus_HasMission;
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	// It's a Nemesis Arc and they already have a Nemesis Arc
	if (missiondef_GetType(pMissionDef) == MissionType_NemesisArc) {
		int i,n = eaSize(&pInfo->missions);
		for(i=0; i<n; i++) {
			Mission *pMission = pInfo->missions[i];
			if (mission_GetType(pMission) == MissionType_NemesisArc) {
				if (pOfferStatus) {
					*pOfferStatus = MissionOfferStatus_HasNemesisArc;
				}
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	// They've already completed the mission and it's not repeatable
	pDebug = entGetPlayerDebug(pPlayerEnt, false);
	if ((!pDebug || !pDebug->canRepeatMissions) && !pMissionDef->repeatable && mission_GetCompletedMissionByDef(pInfo, pMissionDef)) {
		if (pOfferStatus) {
			*pOfferStatus = MissionOfferStatus_HasCompletedMission;
		}

		// We can offer this mission with CreditType "AlreadyCompleted"
		if (eCreditType == MissionCreditType_Primary) {
			eCreditType = MissionCreditType_AlreadyCompleted;
		}
	}

	// The mission has requirements and they don't qualify
	iPartitionIdx = entGetPartitionIdx_NoAssert(pPlayerEnt);
	if (iPartitionIdx <= 0){
		iPartitionIdx = PARTITION_IN_TRANSACTION; // This happens when we test to share missions with teammates
	}

	if
	(
		pMissionDef->missionReqs && (iPartitionIdx == PARTITION_IN_TRANSACTION || !mission_EvalRequirements(iPartitionIdx, pMissionDef, pPlayerEnt))
	)
	{
		// fail if sharing to teammate not on same map or if the eval fails
		// As there isn't a valid partition for the off-map teammate we can't even try to evaluate it correctly
		if (pOfferStatus) {
			*pOfferStatus = MissionOfferStatus_FailsRequirements;
		}

		// We can offer this mission with CreditType "Ineligible"
		if (eCreditType == MissionCreditType_Primary) {
			eCreditType = MissionCreditType_Ineligible;
		}
	}

	// This mission depends on activities (calendar events) being active
	if (!missiondef_CheckRequiredActivitiesActive(pMissionDef)) {
		if (pOfferStatus) {
			*pOfferStatus = MissionOfferStatus_FailsRequirements;
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	// This is a Requested mission and the player doesn't have a Request for it
	if (pMissionDef->eRequestGrantType) {
		int i;
		for (i = eaSize(&pInfo->eaMissionRequests)-1; i>=0; --i) {
			if ((GET_REF(pInfo->eaMissionRequests[i]->hRequestedMission) == pMissionDef) && 
				(pInfo->eaMissionRequests[i]->eState == MissionRequestState_Open)) {
				break;
			}
		}
		if (i<0) {
			if (pOfferStatus) {
				*pOfferStatus = MissionOfferStatus_FailsRequirements;
			}

			// We can offer this mission with CreditType "Ineligible"
			if (eCreditType == MissionCreditType_Primary){
				eCreditType = MissionCreditType_Ineligible;
			}
		}
	}

	// This is a mission with a cooldown, and they have completed it too recently
	if (pMissionDef->repeatable && (pMissionDef->fRepeatCooldownHours > 0 || pMissionDef->fRepeatCooldownHoursFromStart > 0))
	{
		const MissionCooldownInfo *pMissionInfo = mission_GetCooldownInfo(pPlayerEnt, pMissionDef->name);

		if(pMissionInfo->bIsInCooldown)
		{
			if(pOfferStatus)
			{
				*pOfferStatus = MissionOfferStatus_MissionOnCooldown;
			}

			// We can offer this mission with CreditType "Ineligible"
			if(eCreditType == MissionCreditType_Primary)
			{
				eCreditType = MissionCreditType_Ineligible;
			}

		}
	}

	// They're not the right level for the mission.  Check this last, so that we accurately report the next
	// available mission level.
	iPlayerLevel = entity_GetSavedExpLevel(pPlayerEnt);
	iOfferLevel = missiondef_GetOfferLevel(iPartitionIdx, pMissionDef, iPlayerLevel);
	if (iPlayerLevel < iOfferLevel || (pMissionDef->iMinLevel && iPlayerLevel < pMissionDef->iMinLevel)) {
		if (iPlayerLevel < iOfferLevel  && eCreditType == MissionCreditType_Primary) {
			//only set this if nothing else has marked us as ineligible because other functions assume that if the it is set then it is the reason player is ineligible.
			if (piNextOfferLevel && ((*piNextOfferLevel == -1) || (iOfferLevel < *piNextOfferLevel))) {
				*piNextOfferLevel = iOfferLevel;
			}
		}
		if (pOfferStatus) {
			*pOfferStatus = MissionOfferStatus_TooLowLevel;
		}
		
		// We can offer this mission with CreditType "Ineligible"
		if (eCreditType == MissionCreditType_Primary) {
			eCreditType = MissionCreditType_Ineligible;
		}
	}

	// Check required allegiance
	if (!missiondef_CheckPlayerAllegiance(pMissionDef, pPlayerEnt)) {
		if (pOfferStatus) {
			*pOfferStatus = MissionOfferStatus_InvalidAllegiance;
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	// See if we have completed this mission for Secondary credit too recently
	if (eCreditType != MissionCreditType_Primary) {
		CompletedMission *pCompletedSecondary = eaIndexedGetUsingString(&pInfo->eaRecentSecondaryMissions, pMissionDef->name);
		if (pCompletedSecondary && (pCompletedSecondary->completedTime + missiondef_GetSecondaryCreditLockoutTime(pMissionDef) > timeSecondsSince2000())) {
			if (pOfferStatus) {
				*pOfferStatus = MissionOfferStatus_SecondaryCooldown;
			}
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// If this is a progression mission, determine if the player has met the restrictions for it
	if (eaSize(&pMissionDef->ppchProgressionNodes))
	{
		if (!progression_CheckMissionRequirements(pPlayerEnt, pMissionDef))
		{
			if (pOfferStatus) {
				*pOfferStatus = MissionOfferStatus_FailsRequirements;
			}
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (pCreditType) {
		*pCreditType = eCreditType;
	}

	PERFINFO_AUTO_STOP();
	return true;
}


bool missiondef_CanBeOfferedAsPrimary(Entity *pPlayerEnt, MissionDef *pMissionDef, int *piNextOfferLevel, MissionOfferStatus *pOfferStatus)
{
	MissionCreditType eCreditType = 0;
	if (missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, piNextOfferLevel, pOfferStatus, &eCreditType)) {
		if (eCreditType == MissionCreditType_Primary) {
			return true;
		}
	}
	return false;
}


// Checks whether this MissionDef counts towards the "Max Active Missions" limit
bool missiondef_CountsTowardsMaxActive(MissionDef *pDef)
{
	// For now, all visible missions that aren't Perks count towards the limit
	if (pDef && missiondef_HasDisplayName(pDef) && pDef->missionType != MissionType_Perk){
		return true;
	} else {
		return false;
	}
}


// Calculates the level for the mission instance based on parameters in the missionDef
U8 missiondef_CalculateLevel(int iPartitionIdx, int iPlayerLevel, const MissionDef* pDef)
{
	PerfInfoGuard *piGuard;
	U8 iLevel = 1;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	if (pDef) {
		if (GET_REF(pDef->parentDef)) {
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
			return 0;
		}

		switch(pDef->levelDef.eLevelType) 
		{
			// Specified level
			xcase MissionLevelType_Specified:
					iLevel = pDef->levelDef.missionLevel;

			// Player Level
			xcase MissionLevelType_PlayerLevel:
					if (iPlayerLevel) {
						U8 iMinClamp = 1;
						U8 iMaxClamp = MAX_LEVELS;
						MissionLevelClamp *pClamp = pDef->levelDef.pLevelClamp;

						// Get Player Level
						iLevel = iPlayerLevel;

						// Clamp the player level
						if (pClamp) {
							switch(pClamp->eClampType) {
								// Specified clamp
								xcase MissionLevelClampType_Specified:
										if (pClamp->iClampSpecifiedMin) {
											iMinClamp = pClamp->iClampSpecifiedMin;
										}
										if (pClamp->iClampSpecifiedMax) {
											iMaxClamp = pClamp->iClampSpecifiedMax;
										}

								// Clamp by map level +- offset
								xcase MissionLevelClampType_MapLevel:
									{
										int iMapLevel = (int)zmapInfoGetMapLevel(NULL);
										iMinClamp = iMapLevel + pClamp->iClampOffsetMin;
										iMaxClamp = iMapLevel + pClamp->iClampOffsetMax;
									}

								// Clamp by map variable +- offset
								xcase MissionLevelClampType_MapVariable:
									{
										if (iPartitionIdx == PARTITION_IN_TRANSACTION) { 
											// This happens when checking for a teammate who is not on the map.
											// Return an error state
											PERFINFO_AUTO_STOP_GUARD(&piGuard);
											return 0; 
										} else {
											MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pClamp->pcClampMapVariable);
											if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_INT) {
												iMinClamp = pVar->pVariable->iIntVal + pClamp->iClampOffsetMin;
												iMaxClamp = pVar->pVariable->iIntVal + pClamp->iClampOffsetMax;
											}
										}
									}
							}
							// Make sure clamps are valid
							iMaxClamp = CLAMP(iMaxClamp, 1, MAX_LEVELS);
							iMinClamp = CLAMP(iMinClamp, 1, iMaxClamp);
						}
						iLevel = CLAMP(iLevel, iMinClamp, iMaxClamp);
					}

			// Map Level
			xcase MissionLevelType_MapLevel:
					iLevel = (int)zmapInfoGetMapLevel(NULL);

			// Map Variable
			xcase MissionLevelType_MapVariable:
				{
					MapVariable *pVar = mapvariable_GetByName(iPartitionIdx, pDef->levelDef.pchLevelMapVar);
					if (pVar && pVar->pVariable && pVar->pVariable->eType == WVAR_INT) {
						iLevel = pVar->pVariable->iIntVal;
					}
				}
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
	return iLevel;
}


// TODO: This should come from data at some point
int missiondef_GetOfferLevel(int iPartitionIdx, MissionDef *pMissionDef, int iPlayerLevel)
{
	int iMissionLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, pMissionDef);

	if (!iMissionLevel) {
		iMissionLevel = pMissionDef->levelDef.missionLevel;
	}

	if (eaSize(&g_MissionConfig.MissionOfferOffsets.ppOffsets)==0)
	{
		// No defined level offsets. Use the old default
		if (iMissionLevel <= 5) {
			return iMissionLevel - 1;
		} else if (iMissionLevel <= 10) {
			return iMissionLevel - 2;
		}
		return iMissionLevel - 3;
	}
	else
	{
		// Look through the data defined offset defs
		// Forward search because we want to get the first, lowest level
		S32 i;
		for (i = 0; i< eaSize(&g_MissionConfig.MissionOfferOffsets.ppOffsets); i++)
		{
			if (iMissionLevel <= g_MissionConfig.MissionOfferOffsets.ppOffsets[i]->iUpThroughLevel)
			{
				return(iMissionLevel + g_MissionConfig.MissionOfferOffsets.ppOffsets[i]->iOffset);
			}
		}
		// No explicit level def. Use the default
		return(iMissionLevel + g_MissionConfig.MissionOfferOffsets.iDefaultOffset);
	}
}


// When is this mission too low level for the player to care about?
// TODO: Should come from data at some point
int missiondef_GetMaxRewardLevel(int iPartitionIdx, MissionDef *pMissionDef, int iPlayerLevel)
{
	int iMissionLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, pMissionDef);

	if (!iMissionLevel) {
		iMissionLevel = pMissionDef->levelDef.missionLevel;
	}

	if (eaSize(&g_MissionConfig.MissionMaxRewardOffsets.ppOffsets)==0)
	{
		// No defined level offsets. Use the old default
		if (iMissionLevel <= 5) {
			return iMissionLevel + 3;
		} else if (iMissionLevel <= 10) {
			return iMissionLevel + 4;
		} else if (iMissionLevel <= 20) {
			return iMissionLevel + 4;
		} else if (iMissionLevel <= 40) {
			return iMissionLevel + 5;
		}
		return iMissionLevel + 5;
	}
	else
	{
		// Look through the data defined offset defs
		// Forward search because we want to get the first, lowest level
		S32 i;
		for (i = 0; i< eaSize(&g_MissionConfig.MissionMaxRewardOffsets.ppOffsets); i++)
		{
			if (iMissionLevel <= g_MissionConfig.MissionMaxRewardOffsets.ppOffsets[i]->iUpThroughLevel)
			{
				return(iMissionLevel + g_MissionConfig.MissionMaxRewardOffsets.ppOffsets[i]->iOffset);
			}
		}
		// No explicit level def. Use the default
		return(iMissionLevel + g_MissionConfig.MissionMaxRewardOffsets.iDefaultOffset);
	}
}


// ----------------------------------------------------------------------------------
// Mission Utilities
// ----------------------------------------------------------------------------------

void mission_GetDefsForTypeAndRange(MissionDef ***peaMissions, MissionType eType, MissionCategory *pCategory, int iMinLevel, int iMaxLevel)
{
	MissionDef *pDef;
	ResourceIterator resIterator;

	resInitIterator(g_MissionDictionary, &resIterator);

	while (resIteratorGetNext(&resIterator, NULL, &pDef)) {
		if (pDef->missionType == eType && GET_REF(pDef->hCategory) == pCategory) {
			if (pDef->levelDef.missionLevel >= iMinLevel && pDef->levelDef.missionLevel <= iMaxLevel) {
				eaPush(peaMissions, pDef);
			}
		}
	}
	resFreeIterator(&resIterator);
}


Mission* mission_GetChildMission(Mission *pMission, const MissionDef *pDefToMatch)
{
	int i, n = eaSize(&pMission->children);
	for (i = 0; i < n; i++) {
		Mission *pChildMission = pMission->children[i];
		MissionDef *pMissionDef = mission_GetDef(pChildMission);
		Mission *pSubchild = NULL;

		if (pMissionDef == pDefToMatch) {
			return pChildMission;
		}

		pMissionDef = mission_GetOrigDef(pChildMission);
		if (pMissionDef == pDefToMatch) {
			return pChildMission;
		}

		// Danger of infinite recursion if a mission has a really bizarre design
		pSubchild = mission_GetChildMission(pChildMission, pDefToMatch);
		if (pSubchild) {
			return pSubchild;
		}
	}
	return NULL;
}


Mission* mission_FindMissionFromDef(MissionInfo *pInfo, MissionDef *pDefToMatch)
{
	MissionDef *pRootDef = pDefToMatch;
	Mission *pMission = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Find the root def
	while (GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}

	// Find a root mission matching the root def
	pMission = mission_GetMissionFromDef(pInfo, pRootDef);

	if (pRootDef == pDefToMatch) {
		PERFINFO_AUTO_STOP_FUNC();
		return pMission;

	} else if (pMission) {
		pMission = mission_GetChildMission(pMission, pDefToMatch);
		PERFINFO_AUTO_STOP_FUNC();
		return pMission;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

//*****************************************************************************
// Description: Determines whether or not a mission has a chance of granting one
//				or more unique items as a reward for transitioning to eState
// 
// Returns:     < bool > True: one or more unique items may be granted
// Parameter:   < MissionDef * pDef >  Mission to examine
// Parameter:   < MissionState eState >  State the mission will be transitioning to
//*****************************************************************************
bool missiondef_HasUniqueItemsInRewardsForState(MissionDef *pDef, MissionState eState, MissionCreditType eCreditType)
{
	RewardTable *pTable1 = NULL;
	RewardTable *pTable2 = NULL;

	if (!pDef || !pDef->params) {
		return false;
	}

	switch(eState)
	{
		case MissionState_InProgress:
			pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnstartRewardTableName);

		xcase MissionState_Succeeded:

			if (pDef->params->ActivitySuccessRewardTableName && pDef->params->ActivitySuccessRewardTableName[0] &&
				pDef->params->pchActivityName && pDef->params->pchActivityName[0] &&
				gslActivity_IsActive(pDef->params->pchActivityName)) {
				pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->ActivitySuccessRewardTableName);
			} else if (pDef->params->OnsuccessRewardTableName) {
				pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnsuccessRewardTableName);
			}
		xcase MissionState_Failed:
			pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnfailureRewardTableName);

		xcase MissionState_TurnedIn:
			if (pDef->params->ActivityReturnRewardTableName && pDef->params->ActivityReturnRewardTableName[0] &&
				pDef->params->pchActivityName && pDef->params->pchActivityName[0] &&
				gslActivity_IsActive(pDef->params->pchActivityName)) {
				pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, "DefaultMissionNumerics");
				pTable2 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->ActivityReturnRewardTableName);
			} else if (eCreditType == MissionCreditType_Primary) {
				pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, "DefaultMissionNumerics");
				pTable2 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnreturnRewardTableName);
			} else if (eCreditType == MissionCreditType_AlreadyCompleted) {
				pTable1 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, "DefaultMissionNumerics");
				pTable2 = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnReplayReturnRewardTableName);
			}
			break;
	}

	if (pTable1 && rewardTable_HasUniqueItems(pTable1)) {
		return true;
	}

	if (pTable2 && rewardTable_HasUniqueItems(pTable2)) {
		return true;
	}

	return false;
}



void mission_GrantOnEnterMissions(Entity *pPlayerEnt, MissionInfo *pInfo)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, FLASHBACKMISSION_MAPVAR);
	MissionDef *pFlashbackDef = NULL;
	
	if (pMapVar && pMapVar->pVariable && pMapVar->pVariable->pcStringVal) {
		pFlashbackDef = RefSystem_ReferentFromString(g_MissionDictionary, pMapVar->pVariable->pcStringVal);
	}

	FOR_EACH_IN_REFDICT(g_MissionDictionary, MissionDef, pMissionDef) {
		if (pMissionDef->autoGrantOnMap && 
			(pMissionDef->missionType != MissionType_OpenMission) &&
			(pMissionDef->missionType != MissionType_Perk) &&
			(stricmp(pcMapName, pMissionDef->autoGrantOnMap) == 0) &&
			!mission_GetMissionFromDef(pInfo, pMissionDef))
		{
			MissionOfferParams params = {0};
			MissionCreditType eCreditType = MissionCreditType_Primary;
			MissionOfferStatus offerStatus;
			bool bCanBeOfferedAtAll;

			// If this happens to be the Flashback Mission, grant it with type "Flashback"
			if (pMissionDef == pFlashbackDef) {
				eCreditType = MissionCreditType_Flashback;
			}

			// If mission can be offered, set up params and add the mission
			// Otherwise, just don't grant it

			// Don't auto-grant missions for which the player is ineligible.  I don't think the logic here is super great, but that's the
			// only indicator we have at this point that this mission is not for this player. May need to be revisited if there are issues.  [RMARR - 1/9/12]
			bCanBeOfferedAtAll = missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, NULL, &offerStatus, &eCreditType);
			if (bCanBeOfferedAtAll && offerStatus == MissionOfferStatus_OK) {
				params.eCreditType = eCreditType;
				missioninfo_AddMission(iPartitionIdx, pInfo, pMissionDef, &params, NULL, NULL);
			}
		}
	}
	FOR_EACH_END;

	// If this map has a Flashback Mission assigned to it, queue a Mission Offer for that mission
	if (pFlashbackDef && !mission_GetMissionFromDef(pInfo, pFlashbackDef)) {
		mission_QueueMissionOffer(pPlayerEnt, NULL, pFlashbackDef, MissionCreditType_Flashback, 0, true);
	}
}


void mission_GrantPerkMissions(int iPartitionIdx, MissionInfo *pInfo)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	int i, n = eaSize(&pStruct->ppReferents);

	PERFINFO_AUTO_START_FUNC();

	if (eaCapacity(&pInfo->eaNonPersistedMissions) < 256) {
		// this might save some heap allocs
		eaSetCapacity(&pInfo->eaNonPersistedMissions, 256);
	}
	eaIndexedEnable(&pInfo->eaNonPersistedMissions, parse_Mission);

	for (i = 0; i < n; i++) {
		MissionDef *pDef = (MissionDef*)pStruct->ppReferents[i];
		if (pDef->missionType == MissionType_Perk) {
			mission_AddNonPersistedMission(iPartitionIdx, pInfo, pDef);
		}
	}

	PERFINFO_AUTO_STOP();
}


void mission_PostEntityCreateMissionInit(Entity *pPlayerEnt, bool bStartTracking)
{
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pMissionInfo) {
		int i, n = eaSize(&pMissionInfo->missions);
		int iPartitionIdx = PARTITION_STATIC_CHECK; // Note that the value is set to a legal value if bStartTracking is true

		PERFINFO_AUTO_START_FUNC();

		// Everything that is not tracked to the DB but needs to exist must be created here

		// If tracking is enabled (ie., this is an actual player and not a copy for reference),
		// initialize tracking and dirty bit
		if (bStartTracking) {
			iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

			if (!pMissionInfo->eventTracker) {
				pMissionInfo->eventTracker = eventtracker_Create(iPartitionIdx);
			}

			pMissionInfo->needsEval = 1;
		}

		// Any other initialization
		pMissionInfo->parentEnt = pPlayerEnt;
		pMissionInfo->bPerkPointsNeedEval = 1;
		pMissionInfo->bRequestsNeedEval = 1;

		// Call the mission initialization code(missions will handle traversing through children)
		for (i = 0; i < n; i++) {
			mission_PostMissionCreateInitRecursive(iPartitionIdx, pMissionInfo->missions[i], pMissionInfo, NULL, NULL, bStartTracking);
		}

		PERFINFO_AUTO_STOP();
	}
}


void mission_PreEntityDestroyMissionDeinit(int iPartitionIdx, Entity *pPlayerEnt)
{
	MissionInfo *pMissionInfo;

	PERFINFO_AUTO_START_FUNC();
	
	pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pMissionInfo) {
		int i, n = eaSize(&pMissionInfo->missions);

		// Call the mission deinitialization code(missions will handle traversing through children)
		// Do this before free any memory due to dependancies that mission code may have on the missioninfo
		for (i = 0; i < n; i++) {
			mission_PreMissionDestroyDeinitRecursive(iPartitionIdx, pMissionInfo->missions[i]);
		}

		// Free all non-persisted Missions
		n = eaSize(&pMissionInfo->eaNonPersistedMissions);
		for (i = 0; i < n; i++) {
			mission_PreMissionDestroyDeinitRecursive(iPartitionIdx, pMissionInfo->eaNonPersistedMissions[i]);
		}
		eaDestroyStruct(&pMissionInfo->eaNonPersistedMissions, parse_Mission);

		n = eaSize(&pMissionInfo->eaDiscoveredMissions);
		for (i = 0; i < n; i++) {
			mission_PreMissionDestroyDeinitRecursive(iPartitionIdx, pMissionInfo->eaDiscoveredMissions[i]);
		}
		eaDestroyStruct(&pMissionInfo->eaDiscoveredMissions, parse_Mission);

		// Free all memory we have allocated that will not get handled by the Textparser
		eventtracker_Destroy(pMissionInfo->eventTracker, false);
		pMissionInfo->eventTracker = NULL;
	}

	PERFINFO_AUTO_STOP();
}


typedef struct RequestNamespaceDefCallbackData {
	ContainerID entContainerID;
	const char *pcMissionName; // Pooled string
} RequestNamespaceDefCallbackData;


static void mission_RequestNamespaceDefCB(SA_PARAM_NN_VALID TransactionReturnVal *pReturn, RequestNamespaceDefCallbackData *pData)
{
	bool bIsPlayable = false;
	Mission *pMission;
	MissionInfo *pInfo;
	Entity *pEnt;

	// Get the return value from the function call
	RemoteCommandCheck_aslMapManager_IsNameSpacePlayable(pReturn, &bIsPlayable);

	// Check the entity
	pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->entContainerID);
	if (!pEnt || !pEnt->pPlayer) {
		// Entity is no longer present, so do nothing
		SAFE_FREE(pData);
		return;
	}
	pInfo = (MissionInfo*)pEnt->pPlayer->missionInfo;

	// Check if namespace exists
	if (!bIsPlayable) {
		// Namespace mission no longer exists so clean it up
		pMission = mission_GetMissionByOrigName(pInfo, pData->pcMissionName);
		if (pMission) {
			entLog(LOG_GSL, pEnt, "Mission-MissingUGCMission", "No UGC MissionDef found for mission '%s'. Mission will be removed.", pData->pcMissionName);
			pMission->infoOwner = pInfo;
			missioninfo_DropMission(pEnt, pInfo, pMission);
		}
		SAFE_FREE(pData);
		return;
	}

	// Namespace mission does exist, so mark it okay
	pMission = mission_GetMissionByOrigName(pInfo, pData->pcMissionName);
	if (pMission) {
		pMission->bCheckingValidity = false;
	}
	if (!RefSystem_ReferentFromString(g_MissionDictionary, pData->pcMissionName)) {
		MissionInit_ResourceDBDeferred( pEnt, pData->pcMissionName );
	}
	SAFE_FREE(pData);
}

static bool mission_RequestNamespaceDef(Entity *pEnt, Mission *pMission)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];

	if(!gslUGC_PlayingIsEnabled())
		return false; // if we have disabled playing UGC, then no namespace MissionDefs will be provided

	if(resExtractNameSpace(pMission->missionNameOrig, nameSpace, baseObjectName))
	{	
		RequestNamespaceDefCallbackData *pData = calloc(1,sizeof(RequestNamespaceDefCallbackData));
		TransactionReturnVal *pReturnVal = objCreateManagedReturnVal(mission_RequestNamespaceDefCB, pData);
		char buffer[ 1024 ];

		//bool bTransferedRecently = SAFE_MEMBER( pEnt->pSaved, timeLastImported ) > pEnt->pPlayer->timeLastVerifyEntityMissionData;

		pData->entContainerID = entGetContainerID(pEnt);
		pData->pcMissionName = pMission->missionNameOrig;
		sprintf(buffer, "MissionSystem validating mission with namespace %s is playable for %s.", nameSpace, pEnt->debugName);
		RemoteCommand_aslMapManager_IsNameSpacePlayable(pReturnVal, GLOBALTYPE_MAPMANAGER, 0, nameSpace, buffer);

		// Mark as being checked to avoid repeat calls
		pMission->bCheckingValidity = true;

		return true;
	}

	return false;
}

// Cleans up invalid/out-of-date Mission data on the player
// This only tries to fix 1 mission at a time, because dropping a mission can
// drop other missions, so this might unsafely modify the mission list as it iterates.
// Returns TRUE if all data is valid, otherwise this should be called again.
bool mission_VerifyEntityMissionData(Entity *pEnt)
{
	MissionInfo *pMissionInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
	if (pMissionInfo) {
		int i, n = eaSize(&pMissionInfo->missions);
		for (i = n-1; i >= 0; --i) {
			Mission *pMission = pMissionInfo->missions[i];
			MissionDef *pDef;

			// Skip if already checking this mission for validity
			if (pMission->bCheckingValidity) {
				continue;
			}

			// for UGC missions, this will force drop the mission if we have disabled playing UGC using the UGCPlayingEnabled GameServer AUTO_SETTING.
			if(!gslUGC_PlayingIsEnabled())
			{
				char nameSpace[RESOURCE_NAME_MAX_SIZE];
				char baseObjectName[RESOURCE_NAME_MAX_SIZE];

				if(resExtractNameSpace(pMission->missionNameOrig, nameSpace, baseObjectName))
				{
					entLog(LOG_GSL, pEnt, "Mission-UGCPlayingDisabled", "Force-dropping UGC missions because playing UGC is disabled. Mission '%s' will be removed.", pMission->missionNameOrig);
					missioninfo_DropMission(pEnt, pMissionInfo, pMission);
					return false;
				}
			}

			// See if the mission exists
			pDef = mission_GetDef(pMission);
			if (!pDef) {
				// If the mission doesn't exist locally, it may be a UGC namespace mission so check that
				if (mission_RequestNamespaceDef(pEnt, pMission)) {
					// Marked as being processed, so check next mission
					continue;
				} else {
					// Non-namespace mission with no MissionDef - this is bad data.
					entLog(LOG_GSL, pEnt, "Mission-MissingDefError", "No MissionDef found for mission '%s'. Mission will be removed.", pMission->missionNameOrig);
					missioninfo_DropMission(pEnt, pMissionInfo, pMission);
					return false;
				}
			} else if (pDef && pDef->version != pMission->version) {
				// The MissionDef has been updated, and this Mission needs to be restarted
				entLog(LOG_GSL, pEnt, "Mission-VersionUpdate", "Restarting mission '%s': version changed from %d to %d.", pDef->name, pMission->version, pDef->version);
				
				if (missiondef_GetType(pDef) == MissionType_NemesisArc || missiondef_GetType(pDef) == MissionType_NemesisSubArc){
					const char *pchMessage = langTranslateMessageKey(entGetLanguage(pEnt), MISSION_NEMESISARCOLDVERSIONERROR_MSG);
					ClientCmd_NotifySend(pEnt, kNotifyType_MissionError, pchMessage, pDef->pchRefString, pDef->pchIconName);
				} else {
					notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_OLDVERSIONERROR_MSG, kNotifyType_MissionError);
				}
				
				mission_RestartMission(entGetPartitionIdx(pEnt), pEnt, pMissionInfo, pMission, NULL);
				return false;
			} else if (pMission->eCreditType == MissionCreditType_Flashback) {
				// If we are on the mission's map, but the map isn't set up as a Flashback, drop the mission to avoid exploits
				if (pDef && eaSize(&pDef->eaObjectiveMaps) && pDef->eaObjectiveMaps[0]->pchMapName == zmapGetName(NULL)) {
					MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(entGetPartitionIdx(pEnt), FLASHBACKMISSION_MAPVAR);
					MissionDef *pFlashbackDef = NULL;

					if (pMapVar && pMapVar->pVariable && pMapVar->pVariable->pcStringVal) {
						pFlashbackDef = RefSystem_ReferentFromString(g_MissionDictionary, pMapVar->pVariable->pcStringVal);
					}

					if (!pFlashbackDef || pFlashbackDef != pDef) {
						notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_FLASHBACKINVALIDMAPERROR_MSG, kNotifyType_MissionError);
						missioninfo_DropMission(pEnt, pMissionInfo, pMission);
						return false;
					}
				}
			}
		}

		pEnt->pPlayer->timeLastVerifyEntityMissionData = timeSecondsSince2000();
	}

	return true;
}


void mission_RestartMissionEx(int iPartitionIdx, Entity *pEnt, MissionInfo *pInfo, Mission *pMission, const MissionOfferParams *pParams, TransactionReturnCallback cb, void* cbData)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	MissionOfferParams tempParams = {0};

	// Save anything that should be carried over from the old version of the mission
	if (!pParams) {
		tempParams.eCreditType = pMission->eCreditType;
		pParams = &tempParams;
	}

	// TODO - Maybe this should be a single transaction?
	missioninfo_DropMission(pEnt, pInfo, pMission);
	missioninfo_AddMission(iPartitionIdx, pInfo, pMissionDef, pParams, cb, cbData);
}


// Gets the number of missions the player has that count towards the "Max Active Missions" limit
U32 mission_GetNumMissionsTowardsMaxActive(MissionInfo *pInfo)
{
	U32 iNumMissions = 0;
	int i; 
	for (i = eaSize(&pInfo->missions)-1; i >= 0; --i) {
		MissionDef *pDef = mission_GetDef(pInfo->missions[i]);
		if (pDef && missiondef_CountsTowardsMaxActive(pDef)) {
			iNumMissions++;
		}
	}
	return iNumMissions;
}


// ----------------------------------------------------------------------------------
// General Mission Logic
// ----------------------------------------------------------------------------------

// This must run whenever the Mission is created on a server,
// either when the mission is created for the first time or when a player is loaded.
// If startTracking is true, prepare the mission for processing as well
void mission_PostMissionCreateInit(int iPartitionIdx, Mission *pMission, MissionInfo *pInfo, OpenMission *pOpenMission, Mission *pParentMission, bool bStartTracking)
{
	MissionDef *pDef = NULL;
	MissionDef *pOrigDef;

	PERFINFO_AUTO_START_FUNC();

	// Setup back pointers
	pMission->parent = pParentMission;
	pMission->infoOwner = pInfo;
	pMission->pOpenMission = pOpenMission;

	if (pParentMission) {
		COPY_HANDLE(pMission->rootDefOrig, pParentMission->rootDefOrig);
	} else {
		SET_HANDLE_FROM_STRING(g_MissionDictionary, pMission->missionNameOrig, pMission->rootDefOrig);
	}

	pOrigDef = mission_GetOrigDef(pMission);

	if(pInfo && !pInfo->bHasNamespaceMission)
		pInfo->bHasNamespaceMission = pMission && pMission->missionNameOrig && resHasNamespace(pMission->missionNameOrig);

	// Set up the MissionDef reference
	if (missiontemplate_MissionIsMadLibs(pOrigDef)) {
		MissionDef *pNewDef;

		PERFINFO_AUTO_START("MissionIsMadLibs", 1);

		pNewDef = StructClone(parse_MissionDef, pOrigDef);

		if (pNewDef && pOrigDef) {
			char *estrNewName = NULL;
			int id = 1;

			estrCreate(&estrNewName);

			// Create a new unique name for the missiondef
			// The unique name is the old RefString appended with the player's ID
			assertmsg(pInfo && pInfo->parentEnt, "MissionInfo with no parentEnt!");
			estrPrintf(&estrNewName, "%s(playerID%d)", pOrigDef->pchRefString, pInfo->parentEnt->myContainerID);
			pNewDef->name = (char*)allocAddString(estrNewName);
			while (RefSystem_ReferentFromString(g_MissionDictionary, pNewDef->name)) {
				estrPrintf(&estrNewName, "%s(playerID%d:%d)", pOrigDef->pchRefString, pInfo->parentEnt->myContainerID, ++id);
				pNewDef->name = (char*)allocAddString(estrNewName);
			}
			missiondef_CreateRefStringsRecursive(pNewDef, NULL);

			// Regenerate Mission from template using MadLibs values
			// Must be after new refkey is created so that child submissions
			// have the correct refkey
			StructDestroy(parse_TemplateVariableGroup, pNewDef->missionTemplate->rootVarGroup);
			pNewDef->missionTemplate->rootVarGroup = StructCreate(parse_TemplateVariableGroup);
			ParserReadText(pMission->pchTemplateVarGroupOverride, parse_TemplateVariableGroup, pNewDef->missionTemplate->rootVarGroup, 0);
			missiondef_PostProcessFixup(pNewDef, NULL); // This will generate from the template

			// Add MissionDef to dictionary
			// Must be after generating from Template so that generated sub-missions will be added
			RefSystem_AddReferent(g_MissionDictionary, pNewDef->name, pNewDef);
			SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pNewDef, pMission->rootDefOverride);
			pMission->missionNameOverride = StructAllocString(pNewDef->name);

			estrDestroy(&estrNewName);

			g_bHasMadLibsMissions = true;
		}
		PERFINFO_AUTO_STOP();
	}

	pDef = mission_GetDef(pMission);

	if (pMission->parent) {
		if (mission_HasUIString(pMission->parent) || pMission->parent->depth == 0) {
			pMission->depth = pMission->parent->depth+1;
		} else {
			pMission->depth = pMission->parent->depth;
		}
	}

	if (pDef && pDef->uTimeout) {
		pMission->expirationTime = mission_GetEndTime(pDef, pMission);
	}

	// Everything that is not tracked to the DB but needs to exist must be created here
	if (bStartTracking) {
		PERFINFO_AUTO_START("StartTracking", 1);
		mission_FlagAsNeedingEval(pMission);
		missionevent_StartTrackingEvents(iPartitionIdx, pDef, pMission);

		if (pInfo) {
			waypoint_GetMissionWaypoints(pInfo, pMission, &pInfo->waypointList, false);

		} else if (pOpenMission) {
			EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			Entity *pCurrEnt = NULL;

			while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
				MissionInfo *pCurrInfo = SAFE_MEMBER2(pCurrEnt, pPlayer, missionInfo);
				if (pCurrInfo && pCurrInfo->pchCurrentOpenMission == pOpenMission->pchName){
					waypoint_GetMissionWaypoints(pCurrInfo, pMission, &pCurrInfo->waypointList, false);
					mission_FlagInfoAsDirty(pCurrInfo);
				}
			}
			EntityIteratorRelease(pIter);
		}

		// Update the count of the mission.  It's necessary to do this before the mission system tick occurs,
		// or the mission will have its timestamp updated to the current time every time the player enters a map
		mission_UpdateCount(iPartitionIdx, pDef, pMission, false);

		PERFINFO_AUTO_STOP();
	}

	mission_FlagAsDirty(pMission);

	PERFINFO_AUTO_STOP();
}


void mission_PostMissionCreateInitRecursive(int iPartitionIdx, Mission *pMission, MissionInfo *pInfo, OpenMission *pOpenMission, Mission *pParentMission, bool bStartTracking)
{
	int i, n;

	if (!pParentMission && resHasNamespace(pMission->missionNameOrig) && !RefSystem_ReferentFromString(g_MissionDictionary, pMission->missionNameOrig)) {
		if ( !pInfo->bMissionsNeedVerification ) {
			MissionInit_ResourceDBDeferred( pInfo->parentEnt, pMission->missionNameOrig );
		}
		return;
	}

	mission_PostMissionCreateInit(iPartitionIdx, pMission, pInfo, pOpenMission, pParentMission, bStartTracking);

	// Do the same for all submissions
	n = eaSize(&pMission->children);
	for (i = 0; i < n; i++) {
		mission_PostMissionCreateInitRecursive(iPartitionIdx, pMission->children[i], pInfo, pOpenMission, pMission, bStartTracking);
	}
}


// This must be run when a Mission is destroyed
void mission_PreMissionDestroyDeinit(int iPartitionIdx, Mission *pMission)
{
	// Flag MissionInfo as dirty
	if (pMission->infoOwner) {
		mission_FlagInfoAsDirty(pMission->infoOwner);
	}

	// Deinitialize the mission
	missionevent_StopTrackingEvents(iPartitionIdx, pMission);

	// Clear mission waypoints
	if (pMission->infoOwner){
		waypoint_ClearWaypoints(pMission->infoOwner, pMission, false);

	} else if (mission_GetType(pMission) == MissionType_OpenMission) {
		EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		Entity *pCurrEnt = NULL;
		Mission *pRootMission = pMission;

		while (pRootMission->parent) {
			pRootMission = pRootMission->parent;
		}

		while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
			MissionInfo *pInfo = SAFE_MEMBER2(pCurrEnt, pPlayer, missionInfo);
			if (pInfo && pInfo->pchCurrentOpenMission == pRootMission->missionNameOrig){
				waypoint_ClearWaypoints(pInfo, pMission, false);
				mission_FlagInfoAsDirty(pInfo);
			}
		}
		EntityIteratorRelease(pIter);
	}

	// Destroy the missiondef, if this is a madlibs mission
	if (missiontemplate_MissionIsMadLibs(mission_GetOrigDef(pMission))) {
		MissionDef *pDef = GET_REF(pMission->rootDefOverride);
		if (pDef) {
			RefSystem_RemoveReferent(pDef, false);
			StructDestroy(parse_MissionDef, pDef);
		}
	}
}


void mission_PreMissionDestroyDeinitRecursive(int iPartitionIdx, Mission *pMission)
{
	int i, n = eaSize(&pMission->children);

	// First take care of all submissions
	for (i = 0; i < n; i++) {
		mission_PreMissionDestroyDeinitRecursive(iPartitionIdx, pMission->children[i]);
	}

	mission_PreMissionDestroyDeinit(iPartitionIdx, pMission);
}


void mission_UpdateOpenChildren(Mission *pMission)
{
	if (pMission) {
		int i, n = eaSize(&pMission->children);
		bool bHasOpenChildren = 0;
		bool bPrevOpenChildren = pMission->openChildren;

		// Find out if this mission has any open children.
		for (i = 0; i < n; i++) {
			MissionDef *pDef = mission_GetDef(pMission->children[i]);
			if (pDef && IS_HANDLE_ACTIVE(pDef->uiStringMsg.hMessage) && (pMission->children[i]->state == MissionState_InProgress || pMission->children[i]->openChildren)) {
				bHasOpenChildren = 1;
				break;
			}
		}

		// Update the number of open children
		pMission->openChildren = bHasOpenChildren;

		// If the number has changed, update the parent
		if (bHasOpenChildren != bPrevOpenChildren) {
			// The parent may no longer have any open children
			if (0 == bHasOpenChildren && pMission->parent) {
				mission_UpdateOpenChildren(pMission->parent);
			}
			mission_FlagAsDirty(pMission);
		}
	}
}


U32 mission_GetStartTime(const Mission *pMission)
{
	if (pMission) {
		while (pMission && (pMission->startTime == 0) && pMission->parent) {
			pMission = pMission->parent;
		}
		return pMission->startTime;
	}
	return 0;
}


U32 mission_GetEndTime(MissionDef *pDef, const Mission *pMission)
{
	// default to parent start time
	while (pMission && (pMission->startTime == 0) && pMission->parent) {
		pMission = pMission->parent;
	}

	if (pDef && pDef->uTimeout) {
		if (pMission->timerStartTime) {
			return pMission->timerStartTime + pDef->uTimeout;
		} else {
			return pMission->startTime + pDef->uTimeout;
		}
	}
	return 0;
}


U32 mission_TimeElapsed(Mission *pMission)
{
	if (pMission) {
		return timeSecondsSince2000() - mission_GetStartTime(pMission);
	}
	return 0;
}


bool mission_HasExpiredOnStartTimersRecursive(Mission *pMission, MissionDef *pDef, MissionDef *pRootDef)
{
	if (pMission && pDef && pRootDef) {
		int i;
		if (pDef->uTimeout && mission_GetEndTime(pDef, pMission) <= timeSecondsSince2000()) {
			return true;
		}
		for (i = 0; i < eaSize(&pDef->ppOnStartActions); i++) {
			if (pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_GrantSubMission && pDef->ppOnStartActions[i]->pGrantSubMissionProperties) {
				const char *pchSubMissionName = pDef->ppOnStartActions[i]->pGrantSubMissionProperties->pcSubMissionName;
				if (pchSubMissionName) {
					MissionDef *pChildDef = missiondef_FindMissionByName(pRootDef, pchSubMissionName);
					Mission *pChildMission = eaIndexedGetUsingString(&pMission->children, pchSubMissionName);
					if (pChildDef && pChildMission && mission_HasExpiredOnStartTimersRecursive(pChildMission, pChildDef, pRootDef)) {
						return true;
					}
				}
			}
		}
	}
	return false;
}


int mission_TimeRemaining(MissionDef *pDef, Mission *pMission)
{
	int iTimeLeft = (int)mission_GetEndTime(pDef, pMission) - (int)timeSecondsSince2000();
	return max(iTimeLeft, 0);
}


void mission_UpdateTimestamp(MissionDef *pDef, Mission *pMission)
{
	Entity *pEnt = (pMission && pMission->infoOwner) ? pMission->infoOwner->parentEnt : NULL;
	if (pEnt && pDef) {
		ClientCmd_mission_NotifyUpdate(pEnt, pDef->pchRefString);
	}

	// Update the player's "CurrentMissionObjective" for UI
	if (pMission && pMission->infoOwner && pDef) {
		MissionInfo *pInfo = pMission->infoOwner;
		MissionDef *pRootDef = pDef;

		while (pRootDef && GET_REF(pRootDef->parentDef)) {
			pRootDef = GET_REF(pRootDef->parentDef);
		}

		if (missiondef_HasDisplayName(pRootDef)
			&& (pRootDef->missionType == MissionType_Normal 
				|| pRootDef->missionType == MissionType_Nemesis
				|| pRootDef->missionType == MissionType_Episode 
				|| pRootDef->missionType == MissionType_TourOfDuty))
		{
			U32 uLastTime = pInfo->uLastActiveMissionTimestamp;

			pInfo->pchLastActiveMission = pRootDef->pchRefString;
			pInfo->uLastActiveMissionTimestamp = timeSecondsSince2000();
			mission_FlagInfoAsDirty(pInfo);

			if (pInfo->uLastActiveMissionTimestamp > uLastTime + ACTIVITY_UPDATE_FREQ)
			{
				gslSetCurrentActivity(pEnt, entTranslateDisplayMessage(pEnt, pRootDef->displayNameMsg));
				RemoteCommand_ChatPlayerInfo_UpdateActivityString(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), gslGetCurrentActivity(pEnt));
			}
		}
	}
}


int mission_EvalExpression(int iPartitionIdx, Mission *pMission, Expression *pExpr)
{
	static ExprContext *pNormalMissionContext = NULL;
	static ExprContext *pOpenMissionContext = NULL;

	ExprContext *pContext = NULL;
	MultiVal mvResultVal;

	// Initialize contexts
	if (!pNormalMissionContext || !pOpenMissionContext) {
		pNormalMissionContext = exprContextCreate();
		pOpenMissionContext = exprContextCreate();
		exprContextSetFuncTable(pNormalMissionContext, missiondef_CreateExprFuncTable());
		exprContextSetFuncTable(pOpenMissionContext, missiondef_CreateOpenMissionExprFuncTable());
	}

	// Pick which context to use
	if (mission_GetType(pMission) == MissionType_OpenMission) {
		pContext = pOpenMissionContext;
	} else {
		pContext = pNormalMissionContext;
	}

	// Set context variables
	exprContextSetPointerVarPooled(pContext, g_MissionVarName, pMission, parse_Mission, false, true);
	if (pMission->infoOwner && pMission->infoOwner->parentEnt) {
		exprContextSetSelfPtr(pContext, pMission->infoOwner->parentEnt);
		exprContextSetPointerVarPooled(pContext, g_PlayerVarName, pMission->infoOwner->parentEnt, parse_Entity, true, true);
		exprContextSetPartition(pContext, iPartitionIdx);
	} else {
		exprContextRemoveVarPooled(pContext, g_PlayerVarName);
		exprContextSetSelfPtr(pContext, NULL);
		exprContextSetPartition(pContext, iPartitionIdx);
	}

	// Evaluate expression
	exprEvaluate(pExpr, pContext, &mvResultVal);

	return MultiValGetInt(&mvResultVal, NULL);
}


int mission_EvalRequirements(int iPartitionIdx, MissionDef *pDef, Entity *pEnt)
{
	static ExprContext *pNormalMissionContext = NULL;
	static ExprContext *pOpenMissionContext = NULL;

	ExprContext *pContext = NULL;
	MultiVal mvResultVal;
	int iResult = 1;

	if (!pDef) {
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!pNormalMissionContext) {
		pNormalMissionContext = exprContextCreate();
		exprContextSetFuncTable(pNormalMissionContext, missiondef_CreateRequiresExprFuncTable());
	}
	if (!pOpenMissionContext) {
		pOpenMissionContext = exprContextCreate();
		exprContextSetFuncTable(pOpenMissionContext, missiondef_CreateOpenMissionRequiresExprFuncTable());
	}

	if (pDef->missionReqs) {
		if (missiondef_GetType(pDef) == MissionType_OpenMission) {
			pContext = pOpenMissionContext;
		} else {
			pContext = pNormalMissionContext;
		}

		exprContextSetSelfPtr(pContext, pEnt);
		exprContextSetPartition(pContext, iPartitionIdx);
		exprContextSetPointerVarPooled(pContext, g_MissionDefVarName, pDef, NULL, false, true);

		// If the entity is a player, add it to the context as "Player"
		if (entGetPlayer(pEnt)) {
			exprContextSetPointerVarPooled(pContext, g_PlayerVarName, pEnt, NULL, true, true);
		} else {
			exprContextRemoveVarPooled(pContext, g_PlayerVarName);
		}

		exprEvaluate(pDef->missionReqs, pContext, &mvResultVal);

		iResult = MultiValGetInt(&mvResultVal, NULL);
	} 

	PERFINFO_AUTO_STOP();
	return iResult;
}

int mission_EvalMapRequirements(int iPartitionIdx, MissionDef *pDef)
{
	static ExprContext *pOpenMissionContext = NULL;

	MultiVal mvResultVal;
	int iResult = 1;

	if (!pDef || missiondef_GetType(pDef) != MissionType_OpenMission) {
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!pOpenMissionContext) {
		pOpenMissionContext = exprContextCreate();
		exprContextSetFuncTable(pOpenMissionContext, missiondef_CreateOpenMissionExprFuncTable());
	}

	if (pDef->pMapRequirements) {
		exprContextSetSelfPtr(pOpenMissionContext, NULL);
		exprContextSetPartition(pOpenMissionContext, iPartitionIdx);
		exprContextSetPointerVarPooled(pOpenMissionContext, g_MissionDefVarName, pDef, NULL, false, true);
		exprContextRemoveVarPooled(pOpenMissionContext, g_PlayerVarName);

		exprEvaluate(pDef->pMapRequirements, pOpenMissionContext, &mvResultVal);

		iResult = MultiValGetInt(&mvResultVal, NULL);
	}

	PERFINFO_AUTO_STOP();
	return iResult;	
}

int mission_RunMapComplete(int iPartitionIdx, MissionDef *pDef, MissionState iState)
{
	static ExprContext *pOpenMissionContext = NULL;

	MultiVal mvResultVal;
	Expression *pExpr = NULL;
	int iResult = 1;

	if (!pDef || missiondef_GetType(pDef) != MissionType_OpenMission) {
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!pOpenMissionContext) {
		pOpenMissionContext = exprContextCreate();
		exprContextSetFuncTable(pOpenMissionContext, missiondef_CreateOpenMissionExprFuncTable());
	}

	if (pDef->pMapSuccess && iState == MissionState_Succeeded) {
		pExpr = pDef->pMapSuccess;
	}
	else if (pDef->pMapFailure && iState == MissionState_Failed) {
		pExpr = pDef->pMapFailure;
	}

	if (pExpr) {
		exprContextSetSelfPtr(pOpenMissionContext, NULL);
		exprContextSetPartition(pOpenMissionContext, iPartitionIdx);
		exprContextSetPointerVarPooled(pOpenMissionContext, g_MissionDefVarName, pDef, NULL, false, true);
		exprContextRemoveVarPooled(pOpenMissionContext, g_PlayerVarName);

		exprEvaluate(pExpr, pOpenMissionContext, &mvResultVal);

		iResult = MultiValGetInt(&mvResultVal, NULL);
	}

	PERFINFO_AUTO_STOP();
	return iResult;
}

bool mission_ConditionResult(int iPartitionIdx, Mission *pMission, MissionEditCond *pCond, MissionDef *pRootDef, bool bSucceeded)
{
	if(pMission && pCond)
	{
		MissionDef *pDef = mission_GetDef(pMission);
		if(pDef)
		{
			if((pCond->type == MissionCondType_And) || (pCond->type == MissionCondType_Or) || (pCond->type == MissionCondType_Count))
			{
				int i, count = 0;
				const int n = eaSize(&pCond->subConds);
				const int target = (pCond->type == MissionCondType_And) ? n : ((pCond->type == MissionCondType_Or) ? 1 : pCond->iCount);
				for(i = 0; i < n; i++)
				{
					if(mission_ConditionResult(iPartitionIdx, pMission, pCond->subConds[i], pRootDef, bSucceeded))
						count++;
					if(count >= target)
						return true;
					if((count + (n - i)) <= target) // our target is larger than the number remaining...
						break;
				}
			}
			else if(pCond->type == MissionCondType_Objective)
			{
				if(pCond->valStr)
				{
					MissionDef *pSubMissionDef = missiondef_ChildDefFromName(pRootDef, pCond->valStr);
					if(!pSubMissionDef && pRootDef != pDef)
						pSubMissionDef = missiondef_ChildDefFromName(pDef, pCond->valStr);
					if(pSubMissionDef)
					{
						if(missiondef_GetType(pSubMissionDef) == MissionType_OpenMission)
						{
							Mission *pSubMission = openmission_FindMissionFromRefString(iPartitionIdx, pSubMissionDef->pchRefString);
							if(pSubMission)
								return bSucceeded ? pSubMission->state == MissionState_Succeeded : pSubMission->state == MissionState_Failed;
						}
						else
						{
							if(pMission->infoOwner && pMission->infoOwner->parentEnt)
							{
								MissionInfo *pInfo = mission_GetInfoFromPlayer(pMission->infoOwner->parentEnt);
								if(pInfo)
								{
									Mission *pSubMission = mission_FindMissionFromRefString(pInfo, pSubMissionDef->pchRefString);
									if(pSubMission)
										return bSucceeded ? pSubMission->state == MissionState_Succeeded : pSubMission->state == MissionState_Failed;
								}
							}
							else
							{
								Mission *pSubMission = openmission_FindMissionFromRefString(iPartitionIdx, pSubMissionDef->pchRefString);
								if(pSubMission)
									return bSucceeded ? pSubMission->state == MissionState_Succeeded : pSubMission->state == MissionState_Failed;
							}
						}
					}
				}
			}
			else if(pCond->type == MissionCondType_Expression)
			{
				if(pCond->expression)
					return mission_EvalExpression(iPartitionIdx, pMission, pCond->expression);
				else
					return true;
			}
		}
	}

	return false;
}

bool mission_SuccessConditionResult(int iPartitionIdx, Mission *pMission, MissionDef *pRootDef)
{
	if(pMission)
	{
		MissionDef *pDef = mission_GetDef(pMission);
		if(pDef && pDef->meSuccessCond)
			return mission_ConditionResult(iPartitionIdx, pMission, pDef->meSuccessCond, pRootDef, true);
	}
	return false;
}

bool mission_FailureConditionResult(int iPartitionIdx, Mission *pMission, MissionDef *pRootDef)
{
	if(pMission)
	{
		MissionDef *pDef = mission_GetDef(pMission);
		if(pDef && pDef->meFailureCond)
			return mission_ConditionResult(iPartitionIdx, pMission, pDef->meFailureCond, pRootDef, false);
	}
	return false;
}

bool mission_ResetConditionResult(int iPartitionIdx, Mission *pMission, MissionDef *pRootDef)
{
	if(pMission)
	{
		MissionDef *pDef = mission_GetDef(pMission);
		if(pDef && pDef->meResetCond)
			return mission_ConditionResult(iPartitionIdx, pMission, pDef->meResetCond, pRootDef, true);
	}
	return false;
}

bool mission_UpdateState(int iPartitionIdx, Entity *pEnt, Mission *pMission)
{
	MissionDef *pDef = NULL;
	MissionDef *pRootDef = NULL;
	int i, iNumChildren;

	PERFINFO_AUTO_START_FUNC();

	pDef = mission_GetDef(pMission);
	iNumChildren = eaSize(&pMission->children);

	// First, evaluate the children no matter what
	for (i = 0; i < iNumChildren; i++) {
		if (mission_NeedsEvaluation(pMission->children[i])) {
			if (mission_UpdateState(iPartitionIdx, pEnt, pMission->children[i])) {
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
	}

	// Mission got its eval, prevent constant checking
	pMission->needsEval = false;

	if (!pDef) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Now that we know that there's a def, check to see whether it has a parent def
	pRootDef = GET_REF(pDef->parentDef) ? GET_REF(pDef->parentDef) : pDef;

	// If the mission is a Perk, see if it's been "discovered"
	if (mission_GetType(pMission) == MissionType_Perk && !pMission->bDiscovered && !pMission->parent) {
		if (pDef->pDiscoverCond){
			// Discover Perk if expression returns True
			if (mission_EvalExpression(iPartitionIdx, pMission, pDef->pDiscoverCond)) {
				mission_DiscoverMission(pMission);
				PERFINFO_AUTO_STOP();
				return true;
			}
		} else {
			// Default: Discover perk as soon as it becomes persisted
			if (mission_IsPersisted(pMission)){
				mission_DiscoverMission(pMission);
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
	}

	// If the mission is complete, see if it should clean itself up now
	if (mission_IsComplete(pMission) && pMission->infoOwner) { 
		if (pDef == pRootDef && (!pDef->needsReturn || pDef->missionType == MissionType_Perk || pMission->eCreditType == MissionCreditType_Flashback)) {
			mission_UpdateTimestamp(pDef, pMission);
			if (pMission->state == MissionState_Succeeded) {
				mission_TurnInMissionInternal(pMission->infoOwner, pMission, NULL);
			} else if (pMission->state == MissionState_Failed) { 
				missioninfo_DropMission(pEnt, pMission->infoOwner, pMission);
			}
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	// Update the mission debug information
	missiondebug_UpdateDebugInfo(pMission);

	// Check to see if this mission meets its map requirements.
	if (pDef->pMapRequirements && !mission_EvalMapRequirements(iPartitionIdx, pDef)) {
		PERFINFO_AUTO_STOP();
		return true;
	}

	// Update the x/y count
	mission_UpdateCount(iPartitionIdx, pDef, pMission, true);

	mission_UpdateOpenChildren(pMission);

	// Evaluate failure conditions first
	if (pMission->state == MissionState_InProgress) {
		if (pDef->meFailureCond && mission_FailureConditionResult(iPartitionIdx, pMission, pRootDef)) {
			mission_tr_FailMission(iPartitionIdx, pMission);
			PERFINFO_AUTO_STOP();
			return true;
		} else if (pDef->missionType != MissionType_OpenMission && !missiondef_CheckRequiredActivitiesActive(pDef)) {
			missioninfo_DropMission(pEnt, pMission->infoOwner, pMission);
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	// With no success condition, the success condition is based on the success of the children
	if(!pDef->meSuccessCond) {
		bool bSuccess = true;
		int n = eaSize(&pMission->children);
		for (i = 0; i < n; i++) {
			Mission *pChild = pMission->children[i];
			if (pChild->state == MissionState_Failed && pMission->state == MissionState_InProgress) {
				// This mission can never be completed, so it should fail
				mission_tr_FailMission(iPartitionIdx, pMission);
				PERFINFO_AUTO_STOP();
				return true;
			} else if (pChild->state != MissionState_Succeeded) {
				bSuccess = 0;
			}
		}
		if (bSuccess && pMission->state == MissionState_InProgress) {
			if (pMission->infoOwner && pMission->infoOwner->parentEnt) {
				mission_CompleteMission(pMission->infoOwner->parentEnt, pMission, false);
			} else {
				mission_tr_CompleteMission(iPartitionIdx, pMission, false);
			}
			PERFINFO_AUTO_STOP();
			return true;
		} else if (!bSuccess && pMission->state == MissionState_Succeeded && !pMission->permaComplete && !pDef->doNotUncomplete) {
			// Mission is in Success state but is no longer succeeded; uncomplete
			mission_tr_UncompleteMission(iPartitionIdx, pMission);
			PERFINFO_AUTO_STOP();
			return true;
		}
	} else if (pDef->meSuccessCond && mission_SuccessConditionResult(iPartitionIdx, pMission, pRootDef)) {
		if (pMission->state == MissionState_InProgress) {
			if (pMission->infoOwner && pMission->infoOwner->parentEnt) {
				mission_CompleteMission(pMission->infoOwner->parentEnt, pMission, false);
			} else {
				mission_tr_CompleteMission(iPartitionIdx, pMission, false);
			}
			PERFINFO_AUTO_STOP();
			return true;
		}
	} else if (pDef->meSuccessCond && pMission->state == MissionState_Succeeded && !pMission->permaComplete && !pDef->doNotUncomplete) { // condition not true
		// Mission is in Success state but is no longer succeeded; uncomplete
		mission_tr_UncompleteMission(iPartitionIdx, pMission);
		PERFINFO_AUTO_STOP();
		return true;
	} else if (pDef->meResetCond && mission_ResetConditionResult(iPartitionIdx, pMission, pRootDef)) {
		// for now, resetting a mission will only reset the event counters
		FOR_EACH_IN_EARRAY(pMission->eaEventCounts, MissionEventContainer, pEvents)
			pEvents->iEventCount = 0;
		FOR_EACH_END
	}

	PERFINFO_AUTO_STOP();

	return false;
}


void mission_UpdateCount(int iPartitionIdx, MissionDef *pDef, Mission *pMission, bool bUpdateTimestamp)
{
	PERFINFO_AUTO_START_FUNC();

	if (pDef && pDef->successCount && pDef->successTarget) {
		int oldCount = pMission->count;
		int oldTarget = pMission->target;

		pMission->count = mission_EvalExpression(iPartitionIdx, pMission, pDef->successCount);
		pMission->target = mission_EvalExpression(iPartitionIdx, pMission, pDef->successTarget);
		if (pMission->count > pMission->target)
			pMission->count = pMission->target;

		if (pMission->count != oldCount || pMission->target != oldTarget) {
			if (bUpdateTimestamp)
				mission_UpdateTimestamp(pDef, pMission);
			mission_FlagAsDirty(pMission);
		}
	} else if(pDef && pDef->meSuccessCond && pDef->meSuccessCond->type == MissionCondType_Count) {
		int oldCount = pMission->count;
		int oldTarget = pMission->target;

		int i, n = eaSize(&pDef->meSuccessCond->subConds);

		MissionDef *pRootDef = pDef;
		while(GET_REF(pRootDef->parentDef))
			pRootDef = GET_REF(pRootDef->parentDef);

		pMission->target = pDef->meSuccessCond->iCount;
		pMission->count = 0;
		for(i = 0; i < n; i++)
			if(mission_ConditionResult(iPartitionIdx, pMission, pDef->meSuccessCond->subConds[i], pRootDef, true))
				pMission->count++;
		
		if (pMission->count > pMission->target)
			pMission->count = pMission->target;

		if (pMission->count != oldCount || pMission->target != oldTarget) {
			if (bUpdateTimestamp)
				mission_UpdateTimestamp(pDef, pMission);
			mission_FlagAsDirty(pMission);
		}
	} else {
		pMission->target = 0;
	}

	PERFINFO_AUTO_STOP();
}


bool mission_NeedsEvaluation(Mission *pMission)
{
	return pMission->needsEval;
}

static void mission_FlagAsNeedingEvalRecurse(SA_PARAM_NN_VALID Mission *pMission, SA_PARAM_NN_VALID Mission *pStopMission)
{
	pMission->needsEval = 1;

	if (pStopMission == pMission)
	{
		return;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pMission->children, Mission, pChildMission)
	{			
		mission_FlagAsNeedingEvalRecurse(pChildMission, pStopMission);
	}
	FOR_EACH_END
}

void mission_FlagAsNeedingEval(Mission *pMission)
{
	Mission *pRootMission = pMission;

	if (pMission->infoOwner) 
	{
		pMission->infoOwner->needsEval = 1;
	}

	while (pRootMission->parent)
	{
		pRootMission = pRootMission->parent;
	}

	// Mark all parent missions and sibling missions as needing evaluation
	if (pRootMission)
	{
		mission_FlagAsNeedingEvalRecurse(pRootMission, pMission);
	}	
}


void mission_FlagAsDirty(Mission *pMission)
{
	Entity	*e=0;

	if (pMission->infoOwner) {
		e = pMission->infoOwner->parentEnt;
	}
	if (pMission->infoOwner) {
		mission_FlagInfoAsDirty(pMission->infoOwner);
	}
	entity_SetDirtyBit(e, parse_Mission, pMission, true);

	for (; pMission; pMission = pMission->parent) {
		entity_SetDirtyBit(e,parse_Mission, pMission, true);
	}
}


void mission_FlagCompletedMissionAsDirty(MissionInfo *pInfo, CompletedMission *pCompletedMission)
{
	entity_SetDirtyBit(pInfo ? pInfo->parentEnt : 0, parse_CompletedMission, pCompletedMission, true);
	mission_FlagInfoAsDirty(pInfo);
}


void mission_FlagInfoAsDirty(MissionInfo *pInfo)
{
	if (pInfo->parentEnt) {
		entity_SetDirtyBit(pInfo->parentEnt, parse_MissionInfo, pInfo, true);
		entity_SetDirtyBit(pInfo->parentEnt, parse_Player, pInfo->parentEnt->pPlayer, true);
	} else {
		entity_SetDirtyBit(NULL, parse_MissionInfo, pInfo, true);
	}
}


bool mission_IsPersisted(SA_PARAM_NN_VALID const Mission *pMission)
{
	MissionInfo *pInfo = pMission->infoOwner;
	if (pInfo) {
		const Mission *pRootMission = pMission;
		while (pRootMission->parent) {
			pRootMission = pRootMission->parent;
		}

		if (eaFind(&pInfo->missions, pRootMission) != -1) {
			return true;
		}
	}
	return false;
}


Mission* mission_GetCurrentPhase(Mission *pMission)
{
	Mission *pCurMission = pMission;
	bool bFound;

	while (pCurMission) {
		int i;

		bFound = true;
		for(i=0; i<eaSize(&pCurMission->children); i++) {
			Mission *pChild = pCurMission->children[i];
			if (pChild->state==MissionState_InProgress) {
				bFound = false;
				pCurMission = pChild;
				break;
			}
		}

		if (bFound) {
			break;
		}
	}

	return pCurMission;
}


void mission_CompleteCurrentPhase(Entity *pPlayerEnt, Mission *pMission)
{
	Mission *pCurMission = mission_GetCurrentPhase(pMission);

	if (pCurMission) {
		mission_CompleteMission(pPlayerEnt, pCurMission, true);
	}
}


Entity *mission_FindEntForEvent(GameEvent *pListeningEvent, GameEvent *pSentEvent)
{
	EntityIterator *pIter;
	Entity *pEnt = NULL;
	Entity *pReturnEnt = NULL;

	pIter = entGetIteratorAllTypes(pSentEvent->iPartitionIdx, 0, 0);
	while(pEnt = EntityIteratorGetNext(pIter)) {
		if (eventtracker_EntityMatchesEvent(pListeningEvent, pSentEvent, pEnt)) {
			pReturnEnt = pEnt;
			break;
		}
	}
	EntityIteratorRelease(pIter);

	return pReturnEnt;
}

static void mission_TriggerCurrentConditionEventRecurse(Entity *pPlayerEnt, MissionDef *pDef, MissionEditCond *mec)
{
	if(mec)
	{
		int i;
		for(i=0; i<eaSize(&mec->subConds); i++)
			mission_TriggerCurrentConditionEventRecurse(pPlayerEnt, pDef, mec->subConds[i]);

		if(mec->expression)
		{
			ExprFuncTable *pFuncTable = missiondef_CreateExprFuncTable();
			int *eaiEventFuncs = NULL;

			exprFindFunctions(mec->expression, "MissionEventCount", &eaiEventFuncs);
			for(i=0; i<eaiSize(&eaiEventFuncs); i++)
			{
				const MultiVal *pmvParam = exprFindFuncParam(mec->expression, eaiEventFuncs[i], 0);
					
				if (MULTI_GET_TYPE(pmvParam->type) != MULTI_STRING) {
					continue;
				}

				FOR_EACH_IN_EARRAY(pDef->eaTrackedEvents, GameEvent, pEvent) {
					if (!stricmp(pEvent->pchEventName, pmvParam->str)) {
						GameEvent *pCopy = gameevent_CopyListener(pEvent);

						pCopy->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
						eventsend_AddEntSourceExtern(pCopy, entGetPartitionIdx(pPlayerEnt), pPlayerEnt, 1.0, true, 1.0, true);
						if (eventtracker_CaresAboutTarget(pEvent)) {
							Entity *pTarget = mission_FindEntForEvent(pEvent, pCopy);
							eventsend_AddEntTargetExtern(pCopy, entGetPartitionIdx(pTarget), pTarget, 1.0, true, 1.0, true);
						}
						eventtracker_SendEvent(pCopy->iPartitionIdx, pCopy, 1, EventLog_Add, false);

						eaClear(&pCopy->eaSources);
						eaClear(&pCopy->eaTargets);
						StructDestroy(parse_GameEvent, pCopy);
					}
				}
				FOR_EACH_END
			}
		}
	}
}

// TODO: Determine if this function will no longer work with "CountOf". If not, we can get rid of the successCond member, since it is never used elsewhere.
void mission_TriggerCurrentEvent(Entity *pPlayerEnt, Mission *pMission)
{
	Mission *pCurMission = mission_GetCurrentPhase(pMission);

	if (pCurMission) {
		MissionDef *pDef = mission_GetDef(pCurMission);

		if (pDef)
			mission_TriggerCurrentConditionEventRecurse(pPlayerEnt, pDef, pDef->meSuccessCond);
	}
}


// ----------------------------------------------------------------------------------
// Mission Action Logic
// ----------------------------------------------------------------------------------

static void mission_EvaluateDelayedAction(MissionActionDelayed *pAction)
{
	pAction->actionFunc(pAction);
}


// ---------------------------------------------------------------------
//  Creating Missions
// ---------------------------------------------------------------------

NOCONST(Mission)* mission_CreateMission(int iPartitionIdx, const MissionDef *pDef, U8 iMissionLevel, U32 iEntID)
{
	WorldVariable **eaMissionVariables = NULL;
	NOCONST(Mission) *pNewMission = NULL;
	const MissionDef *pRootDef = pDef;
	bool bSuccess = true;
	Entity *pEnt = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();
	
	// Manually initialize the mission, since it's much faster than StructCreate
	pNewMission = StructAlloc_dbg(parse_Mission, ParserGetTableInfo(parse_Mission) MEM_DBG_PARMS_INIT);

	while (GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}
	
	pNewMission->missionNameOrig = pDef->name;
	pNewMission->state = MissionState_InProgress;
	pNewMission->version = pDef->version;
	pNewMission->iLevel = iMissionLevel;

	if (iEntID)
	{
		pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	}

	if (pEnt && pDef->pchTrackedMission)
	{
		pNewMission->pchTrackedMission = StructAllocString(pDef->pchTrackedMission);
	}

	// Initialize mission variables
	eaMissionVariables = worldVariableCalcVariablesAndAllocRandom(iPartitionIdx, pDef->eaVariableDefs, pEnt, 0);
	if (eaSize(&eaMissionVariables) > 0) {
		// Because field is NO_INDEXED_PREALLOC we need to do this manually
		eaIndexedEnable((WorldVariableContainer***)&pNewMission->eaMissionVariables, parse_WorldVariableContainer);
	}
	for (i = 0; i < eaSize(&eaMissionVariables); i++) {
		NOCONST(WorldVariableContainer) *pMissionVarContainer = StructCreateNoConst(parse_WorldVariableContainer);
		worldVariableCopyToContainer(pMissionVarContainer, eaMissionVariables[i]);
		eaIndexedAdd(&pNewMission->eaMissionVariables, pMissionVarContainer);
	}
	eaDestroyStruct(&eaMissionVariables, parse_WorldVariable);

	// If the mission has a madlibs template, generate the variable values for the mission
	if (missiontemplate_MissionIsMadLibs(pDef)) {
		MissionTemplateType *pTemplateType = RefSystem_ReferentFromString(g_MissionTemplateTypeDict, pDef->missionTemplate->templateTypeName);
		TemplateVariableGroup *pVariableGroup = StructClone(parse_TemplateVariableGroup, pDef->missionTemplate->rootVarGroup);
		TemplateVariable **eaFinishedVariables = NULL;
		char *estrTemplateVarGroup = NULL;
		estrStackCreate(&estrTemplateVarGroup);
		
		if (missiontemplate_RandomizeTemplateValues(pTemplateType, pVariableGroup, &eaFinishedVariables, pDef->levelDef.missionLevel)) {
			ParserWriteText(&estrTemplateVarGroup, parse_TemplateVariableGroup, pVariableGroup, 0, 0, 0);
			pNewMission->pchTemplateVarGroupOverride = StructAllocString(estrTemplateVarGroup);
		} else {
			bSuccess = false;
		}
		estrDestroy(&estrTemplateVarGroup);
		StructDestroy(parse_TemplateVariableGroup, pVariableGroup);
	}

	if (bSuccess) {
		PERFINFO_AUTO_STOP();
		return pNewMission;
	} else {
		// Mission creation failed
		StructDestroyNoConst(parse_Mission, pNewMission);
		PERFINFO_AUTO_STOP();
		return NULL;
	}
}


// Adds a sub-mission to a non-persisted Mission
void mission_AddNonPersistedSubMission(int iPartitionIdx, Mission *pParentMission, const MissionDef *pMissionDef, bool bSkipActions)
{
	Mission *pNewMission = NULL;
	Mission *pRootMission = pParentMission;
	MissionInfo *pInfo = pParentMission ? pParentMission->infoOwner : NULL;
	OpenMission *pOpenMission = pParentMission ? pParentMission->pOpenMission : NULL;

	while (pRootMission && pRootMission->parent) {
		pRootMission = pRootMission->parent;
	}

	if (!pParentMission || mission_IsPersisted(pParentMission)) {
		return;
	}

	// Make sure the player doesn't already have this mission
	if (mission_GetChildMission(pRootMission, pMissionDef)) {
		return;
	}

	if (pParentMission && (pNewMission = (Mission*)mission_CreateMission(iPartitionIdx, pMissionDef, 0, 0))) {
		// Because field is NO_INDEXED_PREALLOC we need to do this manually
		eaIndexedEnable(&pParentMission->children, parse_Mission);

		if (!eaIndexedAdd((Mission***)&pParentMission->children, pNewMission)) { 
			// Can get here if for some reason the parent already has the sub-mission
			// Including this even though we check above, just in case.
			StructDestroy(parse_Mission, pNewMission);
			return;
		}

		NOCONSTMISSION(pNewMission)->displayOrder = eaSize(&pParentMission->children);
		NOCONSTMISSION(pNewMission)->state = MissionState_InProgress;
		NOCONSTMISSION(pNewMission)->startTime = timeSecondsSince2000();

		// Start time defaults to parent's start time
		if (pParentMission && pNewMission->startTime == pParentMission->startTime) {
			NOCONSTMISSION(pNewMission)->startTime = 0;
		}

		mission_PostMissionCreateInit(iPartitionIdx, pNewMission, pInfo, pOpenMission, pParentMission, true);

		// Perks shouldn't send this event, because it spams hundreds of them every time a player logs in
		if (missiondef_GetType(pMissionDef) != MissionType_Perk) {
			eventsend_RecordMissionState(iPartitionIdx, pInfo ? pInfo->parentEnt : NULL, pMissionDef->pchRefString, missiondef_GetType(pMissionDef), MissionState_InProgress, REF_STRING_FROM_HANDLE(pMissionDef->hCategory), false, 0);
		}

		if (bSkipActions) {
			// Run Actions (only granting sub-mission, other Actions will be performed when Mission is made persisted)
			gameaction_np_RunActionsSubMissionsOnly(iPartitionIdx, pNewMission, &pMissionDef->ppOnStartActions);
		} else {
			// Run all non-persisted Actions, throw errors on any that require transactions
			gameaction_np_RunActionsSubMissions(iPartitionIdx, pNewMission, &pMissionDef->ppOnStartActions);
		}
	}
}


// This adds a Mission as a non-persisted object.
void mission_AddNonPersistedMission(int iPartitionIdx, MissionInfo *pInfo, const MissionDef *pMissionDef)
{
	Mission *pNewMission = NULL;
	Entity *pPlayerEnt = pInfo->parentEnt;

	PERFINFO_AUTO_START_FUNC();

	// Make sure the player doesn't already have this mission
	if (eaIndexedFindUsingString(&pInfo->missions, pMissionDef->name) != -1) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (eaIndexedFindUsingString(&pInfo->eaNonPersistedMissions, pMissionDef->name) != -1) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (eaIndexedFindUsingString(&pInfo->eaDiscoveredMissions, pMissionDef->name) != -1) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Make sure the player hasn't completed this mission before, unless it's repeatable
	if (!pMissionDef->repeatable && (eaIndexedFindUsingString(&pInfo->completedMissions, pMissionDef->name) != -1)) {
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	pNewMission = (Mission*)mission_CreateMission(iPartitionIdx, pMissionDef, missiondef_CalculateLevel( iPartitionIdx, pPlayerEnt ? entity_GetSavedExpLevel(pPlayerEnt) : 0, pMissionDef), entGetContainerID(pPlayerEnt));
	if (pNewMission) 
	{
		GameProgressionNodeDef *pNodeDef = progression_GetNodeFromMissionName(pPlayerEnt, pMissionDef->name, NULL);

		if (!eaIndexedAdd(&pInfo->eaNonPersistedMissions, pNewMission)) { 
			// Can get here if for some reason the player already has the mission
			// Including this even though we check above, just in case
			StructDestroy(parse_Mission, pNewMission);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		NOCONSTMISSION(pNewMission)->state = MissionState_InProgress;
		NOCONSTMISSION(pNewMission)->startTime = timeSecondsSince2000();
		mission_PostMissionCreateInit(iPartitionIdx, pNewMission, pInfo, NULL, NULL, true);

		// Run Actions (only sub-missions, other Actions will be performed when Mission is made persisted)
		gameaction_np_RunActionsSubMissionsOnly(iPartitionIdx, pNewMission, &pMissionDef->ppOnStartActions);

		if (pNodeDef)
		{
			// Get the story root node
			GameProgressionNodeDef *pStoryRootNodeDef = progression_GetStoryRootNode(pNodeDef);

			// Make sure we really need to execute a transaction.
			if (g_GameProgressionConfig.bStoreMostRecentlyPlayedNode || 
				(pStoryRootNodeDef && pStoryRootNodeDef->bSetProgressionOnMissionAccept))
			{
				ANALYSIS_ASSUME(pPlayerEnt); // I have no idea if this is correct!  Code above seems to indicate that pPlayerEnt may be NULL... but I'm hearing otherwise from other programmers.
				AutoTrans_progression_tr_ExecutePostMissionAcceptTasks(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pPlayerEnt), pMissionDef->name);
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}


void mission_DiscoverMission(Mission *pMission)
{
	MissionInfo *pInfo = SAFE_MEMBER(pMission, infoOwner);
	Mission *pRootMission = pMission;
	int iIndex;

	if (!pInfo) {
		return;
	}

	while (pRootMission && pRootMission->parent) {
		pRootMission = pRootMission->parent;
	}

	if (!pRootMission) {
		return;
	}

	if ((iIndex = eaIndexedFindUsingString(&pInfo->eaNonPersistedMissions, pRootMission->missionNameOrig)) >= 0) {
		Mission *pFoundMission = eaRemove(&pInfo->eaNonPersistedMissions, iIndex);
		assert(pFoundMission);
		if (!pInfo->eaDiscoveredMissions) {
			eaCreate(&pInfo->eaDiscoveredMissions);
			eaIndexedEnable(&pInfo->eaDiscoveredMissions, parse_Mission);
		}
		eaIndexedAdd(&pInfo->eaDiscoveredMissions, pFoundMission);
		pMission = pFoundMission;
	}

	pMission->bDiscovered = true;
	mission_FlagAsNeedingEval(pMission);
	mission_FlagAsDirty(pMission);
}


void mission_TurnInMission(Entity *pPlayerEnt, MissionDef *pMissionDef, ContactRewardChoices *pRewardChoices)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	Mission *pMission = mission_GetMissionFromDef(pInfo, pMissionDef);

	if (pMission && pMission->state == MissionState_Succeeded) {
		mission_TurnInMissionInternal(pInfo, pMission, pRewardChoices);
		survey_Mission(pPlayerEnt, pMissionDef);
	}
}


// There was a bug that accidentally set the "Collect Resources" stat to U32_MAX
// This fixes that by setting the player's stat to their current resources value
// if they are at U32_MAX
static void mission_FixupResourcePlayerStat(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pStatsInfo) {
		U32 uValue = playerstat_GetValue(pEnt->pPlayer->pStatsInfo, "Prk_Collect_Resources");
		if (uValue == U32_MAX) {
			S32 iCurrentResources = inv_GetNumericItemValue(pEnt, "Resources");
			if (iCurrentResources < 0) {
				iCurrentResources = 0;
			}
			playerstats_SetByName(pEnt, "Prk_Collect_Resources", (U32)iCurrentResources);
		}		
	}
}

void mission_ClearPlaceholderPlayerStats(Entity *pEnt)
{
	PlayerStatsInfo *pStatsInfo = SAFE_MEMBER2(pEnt, pPlayer, pStatsInfo);
	
	if (pStatsInfo)
	{
		// Clean up any previous data in case resetting character
		eaDestroyStruct(&pStatsInfo->eaPlayerStatPlaceholders, parse_PlayerStat);
		eaCreate(&pStatsInfo->eaPlayerStatPlaceholders);
		eaIndexedEnable(&pStatsInfo->eaPlayerStatPlaceholders, parse_PlayerStat);
	}
}

void mission_PlayerEnteredMap(Entity *pPlayerEnt)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	static char *estrBuffer = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Hacky database fix-ups
	mission_FixupResourcePlayerStat(pPlayerEnt);

	// Clear placeholder stats
	mission_ClearPlaceholderPlayerStats(pPlayerEnt);

	// Auto Grant Missions
	mission_GrantOnEnterMissions(pPlayerEnt, pInfo);

	// Add Perks
	mission_GrantPerkMissions(entGetPartitionIdx(pPlayerEnt), pInfo);

	// Add a Nemesis Arc if needed
	nemesis_RefreshNemesisArc(pPlayerEnt);

	waypoint_FlagWaypointRefresh(pInfo);

	mapState_AddPlayerValues(pPlayerEnt);

	// Update player's waypoint list
	waypoint_UpdateLandmarkWaypoints(pPlayerEnt);
	waypoint_UpdateTrackedContactWaypoints(pPlayerEnt);
	waypoint_UpdateTeamCorralWaypoints(pPlayerEnt);

	// Update a player's timer list
	gametimer_RefreshGameTimersForPlayer(pPlayerEnt);

	// Update Story Arcs
	progression_UpdateCurrentProgression(pPlayerEnt);

	PERFINFO_AUTO_STOP();
}


void mission_PlayerLeftMap(Entity *pPlayerEnt)
{
	// Reset any logout timers
	if (pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pLogoutTimer) {
		StructDestroy(parse_LogoutTimer, pPlayerEnt->pPlayer->pLogoutTimer);
		pPlayerEnt->pPlayer->pLogoutTimer = NULL;
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}

	mechanics_LeaveMapEntityCleanup(pPlayerEnt);
}


// Helper function which refreshes the entity's remote contact list before completing the mission
void mission_CompleteMission(Entity *pEnt, Mission *pMission, bool bForcePermanentComplete)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	playermatchstats_ReportCompletedMission(pEnt, pMission);

	//Complete the mission
	mission_tr_CompleteMission(iPartitionIdx, pMission, bForcePermanentComplete);
}


void mission_OfferRandomAvailableMission(Entity *pEnt, const char *pcJournalCat)
{
	static MissionDef **eaMissionDefs = NULL;

	MissionCategory *pCat = RefSystem_ReferentFromString(g_MissionCategoryDict, pcJournalCat);

	eaClear(&eaMissionDefs);

	if (pCat && pEnt && pEnt->pChar) {	
		int iPlayerLevel = entity_GetSavedExpLevel(pEnt);
		int offset;
		int i;
		int size;
		MissionInfo* info = mission_GetInfoFromPlayer(pEnt);

		mission_GetDefsForTypeAndRange(&eaMissionDefs, MissionType_AutoAvailable, pCat, iPlayerLevel - 1, iPlayerLevel + 1);

		size = eaSize(&eaMissionDefs);
		offset = (size > 0) ? randomIntRange(0, size - 1) : 0; // Pick a random starting point
		for (i = 0; i < size; i++)
		{
			// We don't have information for a headshot or display name. Offer the mission anyway.
			//  The UI will eventually need to deal with the case of no name and no headshot info
			if (contact_OfferMissionWithHeadshot(pEnt, eaMissionDefs[(i + offset) % size], NULL, NULL))
			{
				break;
			}
		}
	}
}


bool mission_CanPlayerTakeRandomMission(Entity *pEnt, const char *pcJournalCat)
{
	MissionCategory *pCat = RefSystem_ReferentFromString(g_MissionCategoryDict, pcJournalCat);
	static MissionDef **eaMissionDefs = NULL;
	eaClear(&eaMissionDefs);

	if (pCat && pEnt && pEnt->pChar && pEnt->pPlayer) {
		int iPlayerLevel = entity_GetSavedExpLevel(pEnt);
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
		
		int size;

		int i, n = eaSize(&pInfo->missions);
		for (i = 0; i < n; i++) {
			Mission *pCurrMission = pInfo->missions[i];
			MissionDef *pMissionDef = mission_GetDef(pCurrMission);	
			if(pMissionDef && pMissionDef->missionType == MissionType_AutoAvailable && GET_REF(pMissionDef->hCategory) == pCat)
			{
				return false;
			}
		}

		mission_GetDefsForTypeAndRange(&eaMissionDefs, MissionType_AutoAvailable, pCat, iPlayerLevel - 1, iPlayerLevel + 1);

		size = eaSize(&eaMissionDefs);
		for (i = 0; i < size; i++) {
			if (missiondef_CanBeOfferedAsPrimary(pEnt, eaMissionDefs[i], NULL, NULL)) {
				return true;
			}
		}
	}

	return false;
}


// ------------------------------
//  Mission Sharing/Queueing
// ------------------------------

bool missiondef_IsShareable(MissionDef *pDef)
{
	if (pDef->missionType == MissionType_Nemesis || pDef->missionType == MissionType_NemesisArc || pDef->missionType == MissionType_NemesisSubArc || pDef->missionType == MissionType_TourOfDuty || pDef->missionType == MissionType_Perk) {
		return false;
	}
	if (pDef->eShareable == MissionShareableType_Never) {
		return false;
	}
	return true;
}


// This sends a message that a Shared Mission has been accepted.  (It does not change any data.)
void mission_SharedMissionAccepted(Entity *pPlayerEnt, QueuedMissionOffer *pShareInfo)
{
	if (pShareInfo && pPlayerEnt) {
		MissionDef *pDef = GET_REF(pShareInfo->hMissionDef);
		if (pDef) {
			Entity *pSharingPlayer = entFromContainerIDAnyPartition(pShareInfo->eSharerType, pShareInfo->uSharerID);
			if (pSharingPlayer && !pShareInfo->bSilent) {
				notify_SendMissionNotification(pSharingPlayer, pPlayerEnt, pDef, MISSION_SHARING_ACCEPTED_MSG, kNotifyType_SharedMissionAccepted);
			}
		}
	}
}


// This sends a message that a Shared Mission has been declined.  (It does not change any data.)
void mission_SharedMissionDeclined(Entity *pPlayerEnt, QueuedMissionOffer *pShareInfo)
{
	if (pShareInfo && pPlayerEnt) {
		MissionDef *pDef = GET_REF(pShareInfo->hMissionDef);
		if (pDef) {
			Entity *pSharingPlayer = entFromContainerIDAnyPartition(pShareInfo->eSharerType, pShareInfo->uSharerID);
			if (pSharingPlayer && !pShareInfo->bSilent) {
				notify_SendMissionNotification(pSharingPlayer, pPlayerEnt, pDef, MISSION_SHARING_DECLINED_MSG, kNotifyType_SharedMissionDeclined);
			}
		}
	}
}

// Update activities with mission dependencies
// TODO: This function is only additive, it never removes dependent MissionDefs from the activity, so it's
// possible that things can get slightly out-of-sync in development mode if a lot of editor work is being done
void missiondef_UpdateActivityMissionDependencies(MissionDef* pMissionDef)
{
	int i;
	for (i = eaSize(&pMissionDef->ppchRequiresAnyActivities)-1; i >= 0; i--) {
		const char* pchActivityName = pMissionDef->ppchRequiresAnyActivities[i];
		ActivityDef* pActivityDef = ActivityDef_Find(pchActivityName);
		if (pActivityDef) {
			int iIndex = (int)eaBFind(pActivityDef->ppchDependentMissionDefs, strCmp, pMissionDef->pchRefString);
			if (pMissionDef->pchRefString != eaGet(&pActivityDef->ppchDependentMissionDefs, iIndex)) {
				eaInsert(&pActivityDef->ppchDependentMissionDefs, pMissionDef->pchRefString, iIndex);
			}
		}
	}
	for (i = eaSize(&pMissionDef->ppchRequiresAllActivities)-1; i >= 0; i--) {
		const char* pchActivityName = pMissionDef->ppchRequiresAllActivities[i];
		ActivityDef* pActivityDef = ActivityDef_Find(pchActivityName);
		if (pActivityDef) {
			int iIndex = (int)eaBFind(pActivityDef->ppchDependentMissionDefs, strCmp, pMissionDef->pchRefString);
			if (pMissionDef->pchRefString != eaGet(&pActivityDef->ppchDependentMissionDefs, iIndex)) {
				eaInsert(&pActivityDef->ppchDependentMissionDefs, pMissionDef->pchRefString, iIndex);
			}
		}
	}
}


// Queues a Mission Offer for this player
void mission_QueueMissionOffer(Entity *pEnt, Entity *pSharer, MissionDef *pMissionDef, MissionCreditType eCreditType, U32 uTimerStartTime, bool bSilent)
{
	QueuedMissionOffer *pOffer = StructCreate(parse_QueuedMissionOffer);

	pOffer->timestamp = timeSecondsSince2000();
	pOffer->eSharerType = pSharer?entGetType(pSharer):0;
	pOffer->uSharerID = pSharer?entGetContainerID(pSharer):0;
	pOffer->bSilent = bSilent;
	pOffer->uTimerStartTime = uTimerStartTime;
	pOffer->eCreditType = eCreditType;
	SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pOffer->hMissionDef);

	eaPush(&pEnt->pPlayer->missionInfo->eaQueuedMissionOffers, pOffer);
}


// Queues a Mission Offer for this player from sharing
void mission_QueueMissionOfferShared(Entity *pEnt, Entity *pSharer, const char *pcMissionDefName, MissionCreditType eCreditType, U32 uTimerStartTime, bool bSilent)
{
	RemoteCommand_mission_RemoteSendQueueMissionOffer(GLOBALTYPE_ENTITYPLAYER, 
		pEnt->myContainerID, pEnt->myContainerID, 
		pSharer ? entGetType(pSharer) : 0, pSharer ? entGetContainerID(pSharer) : 0,
		pcMissionDefName, eCreditType, uTimerStartTime, bSilent);
}


// If this Mission has recently been shared/offered to this Player, get the QueuedMissionOffer
QueuedMissionOffer *mission_GetQueuedMissionOffer(Entity *pEnt, MissionDef *pDef)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	if (pInfo) {
		int i, n = eaSize(&pInfo->eaQueuedMissionOffers);
		for (i = 0; i < n; i++) {
			QueuedMissionOffer *pOffer = pInfo->eaQueuedMissionOffers[i];
			if (GET_REF(pOffer->hMissionDef) == pDef) {
				return pOffer;
			}
		}
	}

	return NULL;
}


// iShareDistanceOverride: if == 0 then no override, if > 0 override distance, if < 0 any distance including other maps
void mission_ShareMission(Entity *pEnt, const char *pcMissionDefName, bool bSilent, bool bAlwaysNotifyForMakePrimary)
{
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionDefName);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	Mission *pSharedMission = NULL;
	Team *pTeam = team_GetTeam(pEnt);
	Entity *pMissionEnt = pEnt;
	int i;
	
	if (!pDef || !pInfo || !pTeam) {
		return;
	}
	
	// Make sure player has access to the Mission
	pSharedMission = mission_FindMissionFromDef(pInfo, pDef);
	if (!pSharedMission && pTeam && team_IsTeamLeader(pEnt)) {
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++) {
			Entity *pMemberEnt = GET_REF(pTeam->eaMembers[i]->hEnt);
			pInfo = pMemberEnt ? mission_GetInfoFromPlayer(pMemberEnt) : NULL;
			pSharedMission = pInfo ? mission_FindMissionFromDef(pInfo, pDef) : NULL;
			if (pSharedMission) {
				pMissionEnt = pMemberEnt;
				break;
			}
		}
	}
	
	// If this happens there is no way to verify the mission and it can't be shared
	if (!pSharedMission) {
		if (!bSilent || bAlwaysNotifyForMakePrimary) {
			notify_SendMissionNotification(pEnt, pEnt, pDef, MISSION_SHARING_OUTOFRANGE_MSG, kNotifyType_SharedMissionError);
		}
		return;
	}
	
	// Make sure Mission is shareable
	if (!missiondef_IsShareable(pDef) || pSharedMission->bChildFullMission || pSharedMission->bHiddenFullChild){
		if (!bSilent || bAlwaysNotifyForMakePrimary){
			notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_SHARING_NOTSHAREABLE_MSG, kNotifyType_SharedMissionError);
		}
		return;
	}
	
	if (pSharedMission) {
		// If the mission has timers that are already expired, it can't be shared
		if (mission_HasExpiredOnStartTimersRecursive(pSharedMission, pDef, pDef)) {
			if (!bSilent || bAlwaysNotifyForMakePrimary){
				notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_SHARING_TIMEEXPIRED_MSG, kNotifyType_SharedMissionError);
			}
			return;
		}
	}
	
	// If this is a Lockout Mission, make sure the Lockout is still active
	if (pDef->lockoutType != MissionLockoutType_None && !missionlockout_PlayerInLockoutList(pDef, pMissionEnt, entGetPartitionIdx(pEnt))) {
		if (!bSilent || bAlwaysNotifyForMakePrimary) {
			notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_SHARING_LOCKOUTEXPIRED_MSG, kNotifyType_SharedMissionError);
		}
		return;
	}
	
	// Share Mission with all teammates
	for (i = 0; i < eaSize(&pTeam->eaMembers); i++) {
		Entity *pOtherEnt = GET_REF(pTeam->eaMembers[i]->hEnt);
		// Don't try to offer to the original holder of the mission. 
		// We need to compare container IDs since the Team eaMembers are subscribe copies
		// And use pMissionEnt instead of pEnt since the team leader can share offer missions they do not have themselves.
		if (pOtherEnt && (entGetContainerID(pOtherEnt) != entGetContainerID(pMissionEnt)))
		{
			MissionOfferStatus status;
			MissionCreditType eCreditType;

			// check mission status
			if (!missiondef_CanBeOfferedAtAll(pOtherEnt, pDef, NULL, &status, &eCreditType)) {
				switch (status){					
					xcase MissionOfferStatus_HasMission:
						if (!bSilent) {
							notify_SendMissionNotification(pEnt, pOtherEnt, pDef, MISSION_SHARING_HASMISSION_MSG, kNotifyType_SharedMissionError);
						}
					xcase MissionOfferStatus_SecondaryCooldown:
						if (!bSilent || bAlwaysNotifyForMakePrimary) {
							notify_SendMissionNotification(pEnt, pOtherEnt, pDef, MISSION_SHARING_SECONDARYCOOLDOWN_MSG, kNotifyType_SharedMissionError);
						}
					xdefault:
						if (!bSilent || bAlwaysNotifyForMakePrimary) {
							notify_SendMissionNotification(pEnt, pOtherEnt, pDef, MISSION_SHARING_INELIGIBLE_MSG, kNotifyType_SharedMissionError);
						}
				}
				continue;
			}
		
			if (mission_GetQueuedMissionOffer(pOtherEnt, pDef) != NULL
				|| (pOtherEnt->pPlayer->pInteractInfo->pContactDialog && pOtherEnt->pPlayer->pInteractInfo->pContactDialog->state == ContactDialogState_ViewOfferedMission && GET_REF(pOtherEnt->pPlayer->pInteractInfo->pContactDialog->hRootMissionDef) == pDef)) {
				// Mission is already being shared or offered from a Contact
				if (!bSilent) {
					notify_SendMissionNotification(pEnt, pOtherEnt, pDef, MISSION_SHARING_ALREADYOFFERED_MSG, kNotifyType_SharedMissionError);
				}
				continue;
			}
			
			if (pOtherEnt->pPlayer->missionInfo && eaSize(&pOtherEnt->pPlayer->missionInfo->eaQueuedMissionOffers) >= MAX_SHARED_MISSIONS) {
				// Player has too many Missions being shared already.
				notify_SendMissionNotification(pEnt, pOtherEnt, pDef, MISSION_SHARING_BUSY_MSG, kNotifyType_SharedMissionError);
				continue;
			}
			
			if (pOtherEnt->pPlayer->missionInfo) {
				// Share Mission with this teammate
				// see if this entity is on this server
				Entity *pLocalEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pOtherEnt->myContainerID);
				
				if (pLocalEntity) {
					// use direct mission offer
					mission_QueueMissionOffer(pLocalEntity, pMissionEnt, pDef, pSharedMission->eCreditType, 
						pSharedMission->timerStartTime ? pSharedMission->timerStartTime : pSharedMission->startTime, bSilent);				
				} else {
					// use remote mission offer as ent is not on this server
					mission_QueueMissionOfferShared(pOtherEnt, pMissionEnt, pcMissionDefName, pSharedMission->eCreditType, 
						pSharedMission->timerStartTime ? pSharedMission->timerStartTime : pSharedMission->startTime, bSilent);				
				}
				
				if (!bSilent) {
					notify_SendMissionNotification(pEnt, pOtherEnt, pDef, MISSION_SHARING_OFFERED_MSG, kNotifyType_SharedMissionOffered);
				}
			}
		}
	}
}


void mission_SetPrimaryMission(Entity *pEnt, const char *pcMissionDefName)
{
	Team *pTeam = team_GetTeam(pEnt);
	
	if (pTeam) {
		MissionDef *pDef;
		MissionInfo *pInfo;
		Mission *pMission = NULL;
		int i;
		
		if (!team_IsTeamLeader(pEnt)) {
			return;
		}
		
		if (!pcMissionDefName || pcMissionDefName[0] == 0) {
			// clear old mission
			RemoteCommand_aslTeam_SetPrimaryMission(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, NULL);
			return;
		}
		
		pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionDefName);
		if (!pDef) {
			return;
		}
		
		// Make sure at least one member of the team has the mission
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++) {
			Entity *pMemberEnt = GET_REF(pTeam->eaMembers[i]->hEnt);
			pInfo = pMemberEnt ? mission_GetInfoFromPlayer(pMemberEnt) : NULL;
			if(pInfo)
			{
				pMission = mission_FindMissionFromDef(pInfo, pDef);
				if (pMission) {
					break;
				}
			}
		}
		if (!pMission) {
			return;
		}
		
		RemoteCommand_aslTeam_SetPrimaryMission(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID, pcMissionDefName);

	} else {
		// primary solo mission
		MissionInfo *pMInfo = mission_GetInfoFromPlayer(pEnt);
		if (pMInfo) {
			MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionDefName);
			if (pDef) {
				pMInfo->pchPrimarySoloMission = pDef->name;
			} else {
				pMInfo->pchPrimarySoloMission = NULL;
			}
			mission_FlagInfoAsDirty(pMInfo);
		}
	}
}


// The idea here is to represent the minimum amount of progress anyone has made on the mission.
// However, if this player owns the mission, we want to display their counts for objectives (because otherwise it is confusing)
static void mission_ApplyToConsolidatedTeamMission(NOCONST(Mission) *pTeamPrimaryMission, MissionDef *pDef, const Mission *pMission, bool bUpdateCounts)
{
	U32 uExpirationTime = mission_GetEndTime(pDef, pMission);
	int i;

	if (pMission->state == MissionState_InProgress) {
		pTeamPrimaryMission->state = MissionState_InProgress;
	} else if (pMission->state == MissionState_Failed) {
		pTeamPrimaryMission->state = MissionState_Failed;
	}

	if (!pTeamPrimaryMission->expirationTime || pTeamPrimaryMission->expirationTime > uExpirationTime) {
		pTeamPrimaryMission->expirationTime = uExpirationTime;
	}

	if (bUpdateCounts) {
		MIN1(pTeamPrimaryMission->count, pMission->count);
		MAX1(pTeamPrimaryMission->target, pMission->target);
	}

	for (i = eaSize(&pTeamPrimaryMission->children)-1; i>=0 ; --i) {
		NOCONST(Mission)* pTeamChild = pTeamPrimaryMission->children[i];
		const Mission *pChild = pTeamChild ? eaIndexedGetUsingString(&pMission->children, pTeamChild->missionNameOrig) : NULL;
		MissionDef *pChildDef = mission_GetDef(pChild);

		if (pChild) {
			mission_ApplyToConsolidatedTeamMission(pTeamChild, pChildDef, pChild, bUpdateCounts);
		} else {
			StructDestroyNoConst(parse_Mission, pTeamChild);
			eaRemove(&pTeamPrimaryMission->children, i);
		}
	}

	mission_FlagAsDirty((Mission*)pTeamPrimaryMission);
}


// Consolidates the team's primary mission into a fake mission to display in the UI
void mission_UpdateTeamMission(Entity *pEnt)
{
	MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
	Team *pTeam = team_GetTeam(pEnt);
	const char *pcTeamPrimaryMissionName = NULL;
	bool bChanged = false;
	int i;

	if (pInfo) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);

		// Find the team's Primary Mission, if any
		if (pTeam && team_IsMember(pEnt) && pTeam->pchPrimaryMission) {
			pcTeamPrimaryMissionName = pTeam->pchPrimaryMission;
		}

		// If the fake mission is outdated, clean it up (and restore waypoints for the real mission)
		if (pInfo->pTeamPrimaryMission && pInfo->pTeamPrimaryMission->missionNameOrig != pcTeamPrimaryMissionName) {
			Mission *pRealMission = eaIndexedGetUsingString(&pInfo->missions, pInfo->pTeamPrimaryMission->missionNameOrig);
			waypoint_ClearWaypoints(pInfo, pInfo->pTeamPrimaryMission, true);
			if (pRealMission) {
				waypoint_GetMissionWaypoints(pInfo, pRealMission, &pInfo->waypointList, true);
			}
			StructDestroySafe(parse_Mission, &pInfo->pTeamPrimaryMission);
			pInfo->pchTeamCurrentObjective = NULL;
			bChanged = true;
		}

		// If there is a primary mission, update the fake copy
		if (pcTeamPrimaryMissionName) {
			Mission *pRealMission = eaIndexedGetUsingString(&pInfo->missions, pcTeamPrimaryMissionName);

			// Clear outdated waypoints
			if (pInfo->needsEval && pInfo->pTeamPrimaryMission) {
				waypoint_ClearWaypoints(pInfo, pInfo->pTeamPrimaryMission, true);
				StructDestroySafe(parse_Mission, &pInfo->pTeamPrimaryMission);
				pInfo->pchTeamCurrentObjective = NULL;
			} else if (pInfo->needsEval && pRealMission) {
				waypoint_ClearWaypoints(pInfo, pRealMission, true);
			}

			for (i = 0; i < eaSize(&pTeam->eaMembers); i++) {
				Entity *pTeammate = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
				bool bSubscriptionCopy = false;
				if (!pTeammate) {
					pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
					bSubscriptionCopy = true;
				}

				if (SAFE_MEMBER2(pTeammate, pPlayer, missionInfo)){
					Mission *pTeammateMission = eaIndexedGetUsingString(&pTeammate->pPlayer->missionInfo->missions, pcTeamPrimaryMissionName);
					if (pTeammateMission) {
						if (!pInfo->pTeamPrimaryMission) {
							pInfo->pTeamPrimaryMission = StructCreate(parse_Mission);
							StructCopy(parse_Mission, pTeammateMission, pInfo->pTeamPrimaryMission, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, TOK_SERVER_ONLY);
							mission_PostMissionCreateInitRecursive(iPartitionIdx, pInfo->pTeamPrimaryMission, pInfo, NULL, NULL, false);
							mission_FlagAsDirty(pInfo->pTeamPrimaryMission);
						} else {
							MissionDef *pTeammateDef = mission_GetDef(pTeammateMission);
							mission_ApplyToConsolidatedTeamMission(CONTAINER_NOCONST(Mission, pInfo->pTeamPrimaryMission), pTeammateDef, pTeammateMission, (!bSubscriptionCopy && !pRealMission));
						}
					}
				}
			}

			// Update waypoints, only if the mission is In Progress or the player actually has the mission
			// (Shadow missions for teammates shouldn't show "return to contact" waypoints)
			if (pInfo->needsEval && pInfo->pTeamPrimaryMission && 
				(pInfo->pTeamPrimaryMission->state == MissionState_InProgress || pRealMission)) {
				waypoint_GetMissionWaypoints(pInfo, pInfo->pTeamPrimaryMission, &pInfo->waypointList, true);
			}

			// Update the current objective for the team mission
			if (pInfo->pTeamPrimaryMission) {
				Mission *pObjective = mission_GetFirstInProgressObjective(pInfo->pTeamPrimaryMission);
				pInfo->pchTeamCurrentObjective = pObjective?pObjective->missionNameOrig:NULL;
			}

			bChanged = true;
		}

		if (bChanged) {
			mission_FlagInfoAsDirty(pInfo);
		}
	}
}

// ----------------------------------------------------------------------------------
// Override Processing
// ----------------------------------------------------------------------------------

void missiondef_ApplyInteractableOverrides(MissionDef *pDef)
{
	int j;
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	if (pDef && pDef->ppInteractableOverrides) {
		for(j=eaSize(&pDef->ppInteractableOverrides)-1; j>=0; --j) {
			InteractableOverride *pOverride = pDef->ppInteractableOverrides[j];
			if (!pOverride->pcMapName || (stricmp(pOverride->pcMapName, pcMapName) == 0)) {
				interactable_ApplyInteractableOverride(pDef->name, pDef->filename, pOverride->pcInteractableName, pOverride->pcTypeTagName, pOverride->pPropertyEntry);
			}
		}
	}
}


void missiondef_InitInteractableOverrides(MissionDef* pDef)
{
	if (pDef && pDef->ppInteractableOverrides) {
		interactable_InitOverridesMatchingName(pDef->name);
		volume_InitOverridesMatchingName(pDef->name);
	}
}


static void missiondef_ApplyNamespacedContactOverrides(MissionDef *pDef)
{
	S32 j;
	if (pDef) {
		for (j = 0; j < eaSize(&pDef->ppSpecialDialogOverrides); j++) {
			SpecialDialogOverride *pOverride = pDef->ppSpecialDialogOverrides[j];
			ContactDef *pContactDef = contact_DefFromName(pOverride->pcContactName);
			if (contact_CanApplySpecialDialogOverride(pDef->name, pDef->filename, pContactDef, pOverride->pSpecialDialog, false)) {
				contact_ApplyNamespacedSpecialDialogOverride(pDef->name, pContactDef, pOverride->pSpecialDialog);
			}
		}
		for (j = 0; j < eaSize(&pDef->ppMissionOfferOverrides); j++) {
			MissionOfferOverride *pOverride = pDef->ppMissionOfferOverrides[j];
			ContactDef *pContactDef = contact_DefFromName(pOverride->pcContactName);
			if (contact_CanApplyMissionOfferOverride(pDef->name, pDef->filename, pContactDef, pOverride->pMissionOffer, false)) {
				contact_ApplyNamespacedMissionOfferOverride(pContactDef, pOverride->pMissionOffer);
			}
		}
		for (j = 0; j < eaSize(&pDef->ppImageMenuItemOverrides); j++) {
			ImageMenuItemOverride *pOverride = pDef->ppImageMenuItemOverrides[j];
			ContactDef *pContactDef = contact_DefFromName(pOverride->pcContactName);
			if (contact_CanApplyImageMenuItemOverride(pDef->name, pDef->filename, pContactDef, pOverride->pImageMenuItem, false)) {
				contact_ApplyNamespacedImageMenuItemOverride(pDef->name, pContactDef, pOverride->pImageMenuItem);
			}
		}
	}
}

void missiondef_ApplyOverrides(MissionDef *pDef)
{
	if(pDef && contactsystem_IsLoaded() && interactable_AreInteractablesLoaded() && volume_AreVolumesLoaded())
	{
		missiondef_ApplyInteractableOverrides(pDef);
		if (resExtractNameSpace_s(pDef->name, NULL, 0, NULL, 0))
		{
			missiondef_ApplyNamespacedContactOverrides(pDef);
		}
	}
}

void missiondef_InitOverrides(MissionDef *pDef)
{
	if(pDef && contactsystem_IsLoaded() && interactable_AreInteractablesLoaded() && volume_AreVolumesLoaded())
	{
		missiondef_InitInteractableOverrides(pDef);
		// Contact overrides are inited when the mission loads
	}
}

void missiondef_RefreshOverrides(MissionDef *pDef)
{
	PERFINFO_AUTO_START_FUNC();
	missiondef_RemoveOverrides(pDef);
	missiondef_ApplyOverrides(pDef);
	missiondef_InitOverrides(pDef);
	PERFINFO_AUTO_STOP();
}

void missiondef_RemoveInteractableOverrides(MissionDef* pDef)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	if (pDef && pDef->ppInteractableOverrides) {
		interactable_RemoveInteractableOverridesFromMission(pDef->name);
		volume_RemoveInteractableOverridesFromMission(pDef->name);
	}
}

void missiondef_RemoveNamespacedContactOverrides(MissionDef* pDef)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	if (pDef) {
		if (pDef->ppMissionOfferOverrides) {
			contact_RemoveNamespacedMissionOfferOverridesFromMission(pDef->name);
		}
		if (pDef->ppSpecialDialogOverrides) {
			contact_RemoveNamespacedSpecialDialogOverridesFromMission(pDef->name);
		}
		if (pDef->ppImageMenuItemOverrides) {
			contact_RemoveNamespacedImageMenuItemOverridesFromMission(pDef->name);
		}
	}
}


void missiondef_RemoveOverrides(MissionDef *pDef)
{
	if (pDef) {
		missiondef_RemoveInteractableOverrides(pDef);
		if (resExtractNameSpace_s(pDef->name, NULL, 0, NULL, 0))
		{
			missiondef_RemoveNamespacedContactOverrides(pDef);
		}		
	}
}


void mission_ApplyAllInteractableOverrides(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct(g_MissionDictionary);
	int i;

	for(i=eaSize(&pMissions->ppReferents)-1; i>=0; --i) {
		MissionDef *pDef = pMissions->ppReferents[i];
		missiondef_ApplyInteractableOverrides(pDef);
	}
}


void mission_ApplyAllNamespacedContactOverrides(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct(g_MissionDictionary);
	int i;

	for(i=eaSize(&pMissions->ppReferents)-1; i>=0; --i) {
		MissionDef *pDef = pMissions->ppReferents[i];
		if (pDef && resExtractNameSpace_s(pDef->name, NULL, 0, NULL, 0))
		{
			missiondef_ApplyNamespacedContactOverrides(pDef);
		}		
	}
}


static void mission_UpdatePerkPoints(MissionInfo *pInfo)
{
	int i, n = eaSize(&pInfo->completedMissions);

	PERFINFO_AUTO_START_FUNC();

	pInfo->iTotalPerkPoints = 0;
	
	for (i = 0; i < n; i++) {
		MissionDef *pDef = GET_REF(pInfo->completedMissions[i]->def);
		if (pDef && pDef->missionType == MissionType_Perk) {
			pInfo->iTotalPerkPoints += pDef->iPerkPoints;
		}
	}
	mission_FlagInfoAsDirty(pInfo);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Mission Processing Logic
// ----------------------------------------------------------------------------------

// Runs through a list of missions and updates their status accordingly
static void mission_ProcessEntityMissions(int iPartitionIdx, Entity *pEnt, MissionInfo *pInfo)
{
	InteractInfo *pInteractInfo;
	U32 uCurrentTime;

	if (!pInfo) {
		return;
	}

	// If the mission integrity needs to be verified, do that first
	if (pInfo->bMissionsNeedVerification) {
		PERFINFO_AUTO_START("MissionVerify", 1);

		if (mission_VerifyEntityMissionData(pInfo->parentEnt)) {
			pInfo->bMissionsNeedVerification = 0;
		} else {
			pInfo->iNumVerifyAttempts++;
			if (pInfo->iNumVerifyAttempts > 100){
				// Tried to verify 100 times and failed; something is probably wrong (unless over 100 missions have been deleted/had version changed)
				if (pEnt) {
					Errorf("Error: Failed to verify a player's mission data after 100 tries.  Something is probably wrong with this player's missions and it should be looked at.  Player is: %s", pEnt->debugName);
				}
				pInfo->bMissionsNeedVerification = 0;
				pInfo->iNumVerifyAttempts = 0;
			} else {
				// Missions still need more verification. Don't process missions until this is complete
				PERFINFO_AUTO_STOP(); // MisisonVerify
				return;
			}
		}

		PERFINFO_AUTO_STOP(); // MissionVerify
	}

	// Update Mission States
	if (pInfo->needsEval) {
		int i, n = eaSize(&pInfo->missions), m = eaSize(&pInfo->eaNonPersistedMissions);
		int iTotal = n + m + eaSize(&pInfo->eaDiscoveredMissions);
		bool uiChanged = false;

		PERFINFO_AUTO_START("Updating States", 1);

		pInfo->bHasNamespaceMission = false;
		for (i = 0; i < iTotal; i++) {
			Mission *pMission;

			if (i < n) {
				pMission = pInfo->missions[i];
			} else if (i < n + m) {
				pMission = pInfo->eaNonPersistedMissions[i-n];
			} else {
				pMission = pInfo->eaDiscoveredMissions[i-n-m];
			}

			if(!pInfo->bHasNamespaceMission && resHasNamespace(pMission->missionNameOrig)) {
				pInfo->bHasNamespaceMission = true;
			}

			if (mission_NeedsEvaluation(pMission)) {
				if (mission_UpdateState(iPartitionIdx, pEnt, pMission)) {
					break;
				}
			}
		}
		if (i == iTotal) {
			pInfo->needsEval = 0;
		}

		PERFINFO_AUTO_STOP(); // Updating States
	}


	// Update the Team Mission
	PERFINFO_AUTO_START("mission_UpdateTeamMission", 1);
	mission_UpdateTeamMission(pInfo->parentEnt);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("gslTeam_UpdateEnitityTeamCorralStatus", 1);
	gslTeam_UpdateEnitityTeamCorralStatus(pEnt, pInfo);
	PERFINFO_AUTO_STOP();

	// Update Waypoints
	if (pInfo->bWaypointsNeedEval) {
		PERFINFO_AUTO_START("Update Waypoints", 1);

		waypoint_UpdateAllMissionWaypoints(iPartitionIdx, pInfo);
		waypoint_UpdateLandmarkWaypoints(pInfo->parentEnt);
		waypoint_UpdateTrackedContactWaypoints(pInfo->parentEnt);
		waypoint_UpdateTeamCorralWaypoints(pInfo->parentEnt);
		pInfo->bWaypointsNeedEval = 0;

		PERFINFO_AUTO_STOP();
	}

	// Update Perk Points
	if (pInfo->bPerkPointsNeedEval) {
		mission_UpdatePerkPoints(pInfo);
		pInfo->bPerkPointsNeedEval = 0;
	}

	// Update Mission Requests
	if (pInfo->bRequestsNeedEval) {
		int i, n = eaSize(&pInfo->eaMissionRequests);
		for (i = 0; i < n; i++){
			if (missionrequest_Update(iPartitionIdx, pInfo, pInfo->eaMissionRequests[i])) {
				break;
			}
		}
		if (i == n) {
			pInfo->bRequestsNeedEval = 0;
		}
	}

	// Process Shared Missions
	PERFINFO_AUTO_START("Process Shared Missions", 1);

	pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	uCurrentTime = timeSecondsSince2000();

	// Check to see if the current shared mission has timed out or if the player has become ineligible
	if (pInteractInfo && pInteractInfo->pSharedMission) {
		MissionDef *pDef = GET_REF(pInteractInfo->pSharedMission->hMissionDef);
		if (!pDef
			|| (uCurrentTime - pInteractInfo->pSharedMission->timestamp >= MISSION_SHARING_TIMEOUT_SEC)
			|| !missiondef_CanBeOfferedAtAll(pInfo->parentEnt, pDef, NULL, NULL, NULL)
			|| (pDef->lockoutType != MissionLockoutType_None && !missionlockout_GetLockoutList(iPartitionIdx, pDef)))
		{
			interaction_EndInteractionAndDialog(iPartitionIdx, pInfo->parentEnt, false, true, true);
		}
	}

	if (eaSize(&pInfo->eaQueuedMissionOffers)) {
		MissionDef *pDef = GET_REF(pInfo->eaQueuedMissionOffers[0]->hMissionDef);

		// Check to see if the player has become ineligible for the next mission offer
		if ( !pDef
			// For now, only Shared Missions have a timeout
			|| (pInfo->eaQueuedMissionOffers[0]->uSharerID && (uCurrentTime - pInfo->eaQueuedMissionOffers[0]->timestamp >= MISSION_SHARING_TIMEOUT_SEC))
			|| !missiondef_CanBeOfferedAtAll(pInfo->parentEnt, pDef, NULL, NULL, NULL)
			|| (pDef->lockoutType != MissionLockoutType_None && !missionlockout_GetLockoutList(iPartitionIdx, pDef)))
		{
			mission_SharedMissionDeclined(pInfo->parentEnt, pInfo->eaQueuedMissionOffers[0]);
			StructDestroy(parse_QueuedMissionOffer, pInfo->eaQueuedMissionOffers[0]);
			eaRemove(&pInfo->eaQueuedMissionOffers, 0);

		} else if (pInfo->parentEnt && !interaction_IsPlayerInDialog(pInfo->parentEnt) && !interaction_IsPlayerInteracting(pInfo->parentEnt)) {
			// Offer the first mission in the queue to the player
			contact_OfferNextQueuedMission(pInfo->parentEnt);
		}
	}

	PERFINFO_AUTO_STOP(); // Process Shared Missions

	if (pEnt->astrMissionToGrant) 
	{
		PERFINFO_AUTO_START("Mission To Grant", 1);

		if (resNamespaceIsUGC(pEnt->astrMissionToGrant)) 
		{
			char ns[RESOURCE_NAME_MAX_SIZE];

			devassert(strEndsWith(pEnt->astrMissionToGrant, ":Mission"));

			// Extract the namespace
			resExtractNameSpace_s(pEnt->astrMissionToGrant, SAFESTR(ns), NULL, 0);				

			gslUGC_PlayProjectNonEditor(pEnt, ns, NULL, NULL, NULL, false);
		} 
		else 
		{
			missioninfo_AddMissionByName(entGetPartitionIdx(pEnt), pInfo, pEnt->astrMissionToGrant, NULL, NULL);
		}
		missioninfo_ClearMissionToGrant(pEnt);

		PERFINFO_AUTO_STOP(); // Mission To Grant
	}
}


void mission_OncePerFrame(F32 fTimeStep)
{
	Entity* pCurrEnt;
	U32 uWhichPlayer = 0;
	U32 uCurrTickModded = s_MissionTick % MISSION_SYSTEM_TICK;
	EntityIterator* pIter;

	PERFINFO_AUTO_START("MissionTick", 1);

	// Process all players
	// Don't process ENTITYFLAG_IGNORE ents here, just to save time; they're probably still loading the map
	pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		if ((uWhichPlayer % MISSION_SYSTEM_TICK) == uCurrTickModded) {
			MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
			int iPartitionIdx = entGetPartitionIdx(pCurrEnt);

			if (iPartitionIdx != PARTITION_ENT_BEING_DESTROYED) {
				PERFINFO_AUTO_START("MissionPlayerTick", 1);
				mission_ProcessEntityMissions(iPartitionIdx, pCurrEnt, pMissionInfo);
				PERFINFO_AUTO_STOP();
			}
		}
		
		uWhichPlayer++;
	}
	EntityIteratorRelease(pIter);

	s_MissionTick++;

	PERFINFO_AUTO_STOP();

	// Call into open mission system
	openmission_OncePerFrame(fTimeStep);
	
	// Process new overrides.  Normally only happens on server startup or when editing missions.
	if (g_eaMissionsWaitingForOverrideProcessing && eaSize(&g_eaMissionsWaitingForOverrideProcessing) &&
		contactsystem_IsLoaded() && interactable_AreInteractablesLoaded() && volume_AreVolumesLoaded()) {
		int i;
		static int runOnce = 0;
		for (i = eaSize(&g_eaMissionsWaitingForOverrideProcessing)-1; i >= 0; i--) {
			MissionDef *pDef = missiondef_DefFromRefString(g_eaMissionsWaitingForOverrideProcessing[i]);
			
			missiondef_RefreshOverrides(pDef);

			eaRemove(&g_eaMissionsWaitingForOverrideProcessing, i);
		}

		// On first load
		if(!runOnce)
		{
			runOnce = true;

			interactable_ScanOverrideCounts();
		}
	}
}


// ----------------------------------------------------------------------------------
// Mission Load and Validation
// ----------------------------------------------------------------------------------

static void missiondef_ValidateMapReferences(MissionDef *pDef, MissionDef *pRootDef, const char *pcMapPublicName)
{
	int i;

	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaWaypoints, MissionWaypoint, pWaypoint) {
		if (pWaypoint && pWaypoint->name && pWaypoint->mapName && pcMapPublicName && (stricmp(pWaypoint->mapName, pcMapPublicName) == 0)) {
			// Detect area waypoint references that don't match
			if (((pWaypoint->type == MissionWaypointType_Volume) || (pWaypoint->type == MissionWaypointType_AreaVolume)) &&
				(!volume_VolumeExists(pWaypoint->name, NULL))) {
				ErrorFilenamef(pDef->filename, "Mission %s has a volume waypoint on the current map set to %s, but no volume of this name exists!", pDef->name, pWaypoint->name);
			}

			// Detect missing clickies
			if (pWaypoint->type == MissionWaypointType_Clicky) {
				if (!interactable_InteractableExists(NULL, pWaypoint->name)) {
					ErrorFilenamef(pDef->filename, "Mission %s has an interactable waypoint on the current map set to %s, but no interactable of this name exists!", pDef->name, pWaypoint->name);
				}
			}

			// Detect missing named points
			if (pWaypoint->type == MissionWaypointType_NamedPoint) {
				if (!namedpoint_NamedPointExists(pWaypoint->name, NULL)) {
					ErrorFilenamef(pDef->filename, "Mission %s has a named point waypoint on the current map set to %s, but no named point of this name exists!", pDef->name, pWaypoint->name);
				}
			}

			// TODO: Add missing encounter check
		}
	} FOR_EACH_END

	// Recurse
	for(i=eaSize(&pDef->subMissions)-1; i>=0; --i) {
		missiondef_ValidateMapReferences(pDef->subMissions[i], pRootDef, pcMapPublicName);
	}
}


void mission_MapValidate(ZoneMap *pZoneMap)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct(g_MissionDictionary);
	const char *pcMapPublicName = zmapInfoGetPublicName(zmapGetInfo(pZoneMap));
	int i;

	if (pcMapPublicName){
		for(i=eaSize(&pMissions->ppReferents)-1; i>=0; --i) {
			MissionDef *pDef = pMissions->ppReferents[i];
			missiondef_ValidateMapReferences(pDef, pDef, pcMapPublicName);
		}
	}
}


void mission_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	missiondrop_RefreshGlobalMissionDrops();
	missionlockout_ResetPartition(iPartitionIdx);
	if( !gbMakeBinsAndExit ) {
		openmission_PartitionLoad(iPartitionIdx);
	}

	PERFINFO_AUTO_STOP();
}


void mission_PartitionUnload(int iPartitionIdx)
{
	missionlockout_ResetPartition(iPartitionIdx);
	if( !gbMakeBinsAndExit ) {
		openmission_PartitionUnload(iPartitionIdx);
	}
}


void mission_MapLoad(bool bFullInit)
{
	if (bFullInit) {
		// Things that need to happen on a map load
		missiondrop_RefreshGlobalMissionDrops();
		missionlockout_ResetAllPartitions();
	}

	if( !gbMakeBinsAndExit ) {
		openmission_MapLoad(bFullInit);
	}

	if (bFullInit) {
		if (!beaconIsBeaconizer()) {
			// Reset tracked events for missions.
			// This ensures that all the events are tracking properly for the new map.
			missionevent_StopTrackingAll();
			missionevent_StartTrackingAll();
		}
	}
}


void mission_MapUnload(void)
{
	missionlockout_ResetAllPartitions();
	if( !gbMakeBinsAndExit ) {
		openmission_MapUnload();
	}
}


AUTO_STARTUP(MissionCategories);
void mission_LoadCategories(void)
{
	missiondef_LoadCategories();
}


AUTO_STARTUP(Missions) ASTRT_DEPS(Items, AS_Messages, MissionVars, MissionTemplates, RewardsCommon, Critters, MissionCategories, MissionPlayTypes, MissionTags, MissionUITypes, TutorialScreenRegions Projectiles, Allegiance, AS_GameProgression, Powers);
void mission_LoadMissionData(void)
{
	// Load all def files first
	missiondef_LoadMissionDefs();

	// Now initialize all of our systems
	timedeventqueue_Create("Mission", mission_FlagAsNeedingEval, NULL);
	timedeventqueue_Create("MissionActions", mission_EvaluateDelayedAction, NULL);
}

AUTO_STARTUP(MissionConfig);
void mission_LoadMissionConfig(void)
{
	loadstart_printf("Loading Missionconfig.def... ");

	ParserLoadFiles(NULL, "defs/config/MissionConfig.def", "MissionConfig.bin", PARSER_OPTIONALFLAG, parse_MissionConfig, &g_MissionConfig);

	loadend_printf(" done load missionconfig.def");
}

#include "gslMission_h_ast.c"
