/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entity.h"
#include "gslInteraction.h"
#include "gslInteractionManager.h"
#include "gslInteractable.h"
#include "interaction_common.h"
#include "Player.h"
#include "aiAnimList.h"
#include "allegiance.h"
#include "AnimList_Common.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "estring.h"
#include "GameStringFormat.h"
#include "gslEncounter.h"
#include "Character.h"
#include "RegionRules.h"
#include "NotifyCommon.h"
#include "WorldGrid.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

extern bool g_bEnableInteractionDebugLog;

// ----------------------------------------------------------------------------------
// Performing Interaction
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslInteract(Entity *pEnt, EntityRef entRef, const char *pcNodeKey, const char *pcVolumeName, int iIndex, int eTeammateType, U32 uTeammateID)
{
	bool bSuccess = interaction_PerformInteract(pEnt, entRef, pcNodeKey, pcVolumeName, iIndex, eTeammateType, uTeammateID, false);
	if (!bSuccess)
	{
		ClientCmd_NotifySend(pEnt, kNotifyType_InteractionDenied, entTranslateMessageKey(pEnt, "Interaction.Denied"), NULL, NULL);
	}
}


// This will stop the entity's loot interaction.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_SERVERONLY ACMD_PRIVATE;
void interactloot_StopInteracting(Entity *pPlayerEnt)
{
	if (SAFE_MEMBER2(pPlayerEnt, pPlayer, InteractStatus.interactTarget.bLoot))
		interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, false, true);
}

// A version of player_StopInteracting that can always be called by the client.
// This always forces the interaction to fail.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_SERVERONLY ACMD_HIDE;
void player_ClientStopInteracting(Entity *pPlayerEnt)
{
	interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true, true);
}

// ----------------------------------------------------------------------------------
// Utility Commands
// ----------------------------------------------------------------------------------

// Counts all nodes on the current map of the specific type which the player may interact with when they get within range
//  but ignoring some of them
static int gslCountNodesOfTypeWithRestrictions(Entity *pEnt, const char *pchCategory, bool bIgnoreHidden, bool bMatchRegions)
{
	int count = 0;

	int iPartitionIdx=-1;
	if (bIgnoreHidden)
	{
		iPartitionIdx = entGetPartitionIdx(pEnt);
	}
	

	if(!pchCategory) {
		count = -1;
	} else {
		GameInteractable** eaInteractables = NULL;
		int i;

		eaCreate(&eaInteractables);
		interactable_FindAllInteractablesOfCategory(&eaInteractables, pchCategory);

		for(i = eaSize(&eaInteractables)-1; i >=0; i--)
		{
			GameInteractable* pInteractable = eaInteractables[i];

			if (interaction_GetValidInteractNodeOptions(pInteractable, pEnt, NULL, false, !gConf.bDestructibleInteractOption))
			{
				if (bIgnoreHidden)
				{
					if (interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable))
					{
						if (!im_FindCritterforObject(iPartitionIdx, pInteractable->pcNodeName))
						{
							continue;
						}
					}
				}
			
				if (bMatchRegions)
				{
					Vec3 vEntPos;
					Vec3 vInteractablePos;
			
					entGetPos(pEnt, vEntPos);
					interactable_GetWorldMid(pInteractable, vInteractablePos);
		
					if (worldGetWorldRegionByPos(vInteractablePos) != worldGetWorldRegionByPos(vEntPos))
					{
						continue;
					}
				}

				count++;
			}
		}
		eaDestroy(&eaInteractables);
	}
	return(count);
}

// Counts all nodes on the current map of the specific type which the player may interact with when they get within range.
// Returns this count to the client via a client command.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslCountNodesOfType(Entity *pEnt, const char *pchCategory) 
{
	int count = 0;

	count = gslCountNodesOfTypeWithRestrictions(pEnt, pchCategory, false, false);
	ClientCmd_gclSetInteractionNodeCount(pEnt, count);
}


// Counts all "Harvest" nodes and plays an effect on the requesting
// player that points toward the closest interesting object.
// NOTE: This looks for destructibles in addition to clickables
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ScanForClickies) ACMD_SERVERCMD ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void gslScanForClickies(Entity *pEnt) 
{
	int count = 0;
	GameInteractable** eaInteractables = NULL;
	GameInteractable* pClosestInteractable = NULL;
	static const char** s_ppchClickableAllowed = NULL;
	static const char** s_ppchDestructibleAllowed = NULL;
	int i;
	Vec3 interestingPos = {0};
	F32 interestingDist;
	U32 curTime = timeServerSecondsSince2000();
	RegionRules* rr = getRegionRulesFromEnt(pEnt);
	AllegianceDef* pAllegiance = GET_REF(pEnt->hAllegiance);
	AllegianceDef* pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
	RegionAllegianceFXData* pAllegianceFX = NULL;

	if(   !entIsAlive( pEnt ) || !pEnt->pPlayer || !rr || !rr->bAllowScanForInteractables
		  || curTime - pEnt->pPlayer->iTimeLastScanForInteractables < rr->iScanForInteractablesCooldown ) {
		return;
	}
	
	pEnt->pPlayer->iTimeLastScanForInteractables = curTime;
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

	// Count harvest nodes. Ignore Hiddens and require region matching
	count = gslCountNodesOfTypeWithRestrictions(pEnt, "Harvest", true, true);


	if (s_ppchClickableAllowed == NULL)
	{
		eaPush(&s_ppchClickableAllowed, pcPooled_Clickable);
	}
	if (s_ppchDestructibleAllowed == NULL)
	{
		eaPush(&s_ppchDestructibleAllowed, pcPooled_Destructible);
	}
	
	interestingDist = rr->fScanForInteractablesRange;

	// Check for interact, ignore hidden, check regions
	interactable_FindClosest(pEnt, &interestingDist, interestingPos, s_ppchClickableAllowed, true, true, true);
	
	// Do NOT check for interact, do ignore hidden, do check regions.
	//	 mblattel[13Oct11] The interact validity checker has odd special cases for destructibles. So we want to ignore it.
	//   The code I am replacing used interactable_FindClosestInteractableWithCheck which also skipped the validity check.
	interactable_FindClosest(pEnt, &interestingDist, interestingPos, s_ppchDestructibleAllowed, false, true, true);


/****************************
	//  mblattel[13Oct11]  Older code to deal with destructibles.
	//	F32 fCloseNodeDist = -1.0f;
	//	static U32 s_uMask = 0;
	//		s_uMask |= wlInteractionClassNameToBitMask("Destructible");
	

	if (rr->fScanForInteractablesRange > FLT_EPSILON)
	{
		pClosestInteractable = interactable_FindClosestInteractableWithCheck(pEnt, s_uMask, NULL, NULL, rr->fScanForInteractablesRange,
																				true, // MatchRegion
																				false, // CheckLOS
																				&fCloseNodeDist);
	}
	
	if (pClosestInteractable && fCloseNodeDist < interestingDist)
	{
		interactable_GetWorldMid(pClosestInteractable, interestingPos);
		interestingDist = fCloseNodeDist;
	}
****************************/

	///////////////////
	//  We have our interesting object, etc. Now choose an FX

	for (i = eaSize(&rr->eaScanForInteractablesAllegianceFX)-1; i >= 0; i--)
	{
		AllegianceDef* pRegionAllegiance = GET_REF(rr->eaScanForInteractablesAllegianceFX[i]->hAllegiance);
		if (!pRegionAllegiance || pRegionAllegiance == pSubAllegiance)
		{
			pAllegianceFX = rr->eaScanForInteractablesAllegianceFX[i];
			break;
		}
		else if (pRegionAllegiance == pAllegiance)
		{
			pAllegianceFX = rr->eaScanForInteractablesAllegianceFX[i];
			if (!pSubAllegiance)
			{
				break;
			}
		}
	}

	if (pAllegianceFX)
	{
		char* estrNotification = NULL;
		const char* pchAnimListName = pAllegianceFX->pchAnimList;
		const char** ppchFXNames;

		if (interestingDist < rr->fScanForInteractablesRange)
		{
			ppchFXNames = pAllegianceFX->ppchSuccessFX;
		}
		else
		{
			ppchFXNames = pAllegianceFX->ppchFailFX;
		}

		aiAnimListSetOneTick(pEnt, RefSystem_ReferentFromString(g_AnimListDict, pchAnimListName));
		
		character_FlashFX(entGetPartitionIdx(pEnt), 
						  pEnt->pChar, 
						  0, 
						  0, 
						  kPowerAnimFXType_STOScanForClickies, 
						  NULL, 
						  interestingPos, 
						  NULL, 
						  NULL,
						  NULL,
						  ppchFXNames, 
						  NULL, 
						  0, 
						  pmTimestamp(0), 
						  0,
						  0);						  
		
		estrStackCreate(&estrNotification);
		entFormatGameMessageKey(pEnt, &estrNotification,
								(count == 1 ? "ScanForClickies.SingleResult" : "ScanForClickies.Result"),
								STRFMT_INT("AnomalyCount", count), STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_Default, estrNotification, NULL, NULL);
		estrDestroy(&estrNotification);
	}
}


// ----------------------------------------------------------------------------------
// Debugging
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME("Encountersystemdebug_AllowAllInteractions", "AllowAllInteractions");
void encountersystemdebug_AllowAllInteractions(Entity *pPlayerEnt, int enable)
{
	PlayerDebug* pDebug = entGetPlayerDebug(pPlayerEnt, enable);
	if (pDebug){
		if (enable){
			pDebug->allowAllInteractions = 1;
		} else {
			pDebug->allowAllInteractions = 0;
		}
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_NAME("EnableInteractionDebugLog");
void enacountersystemdebug_EnableInteractionDebugLog(int enable)
{
	g_bEnableInteractionDebugLog = enable;
}

