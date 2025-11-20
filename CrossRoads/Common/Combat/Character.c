/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_combat.h"
#include "Character_mods.h"
#include "CombatAdvantage.h"
#include "CombatCallbacks.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "CostumeCommonEntity.h"
#include "CombatReactivePower.h"
#include "DamageTracker.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "Estring.h"
#include "GameAccountDataCommon.h"
#include "ItemArt.h"
#include "interaction_common.h"
#include "logging.h"
#include "net.h"
#include "netpacket.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerEnhancements.h"
#include "PowersEnums_h_ast.h"
#include "PowerHelpers.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "CombatPowerStateSwitching.h"
#include "PowerVars.h"
#include "PowerReplace.h"
#include "PowerSlots.h"
#include "PowerSubtarget.h"
#include "queue_common.h"
#include "rand.h"
#include "RegionRules.h"
#include "RewardCommon.h"
#include "rewardCommon_h_ast.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "Team.h"
#include "TimedCallback.h"
#include "Tray.h"
#include "WorldGrid.h"
#include "aiDebugShared.h"
#include "species_common.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
	#include "EntityMovementManager.h"
	#include "EntityMovementDefault.h"
	#include "EntityMovementTactical.h"
	#include "EntityMovementProjectile.h"
	#include "Character_tick.h"
#endif

#if GAMECLIENT
	#include "ClientTargeting.h"
	#include "gclEntity.h"
	#include "dynFxInterface.h"
	#include "CostumeCommonGenerate.h"
	#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#endif

#if GAMESERVER
	#include "gslSuperCritterPet.h"
	#include "aiFCStruct.h"
	#include "aiLib.h"
	#include "cmdServerCharacter.h"
	#include "CharacterRespecServer.h"
	#include "gslCostume.h"
	#include "gslEntity.h"
	#include "gslEventSend.h"
	#include "gslInteraction.h"
    #include "gslLogSettings.h"
	#include "gslMapState.h"
	#include "gslOldEncounter.h"
	#include "gslPowerTransactions.h"
	#include "gslPVP.h"
	#include "gslPvPGame.h"
	#include "gslQueue.h"
	#include "gslSavedPet.h"
	#include "gslSendToClient.h"
	#include "gslTray.h"
	#include "inventoryTransactions.h"
	#include "Reward.h"
	#include "PowerTreeTransactions.h"
	#include "GameStringFormat.h"
	#include "aiPowers.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
	#include "GameServerLib.h"
	#include "Team.h"
	#include "PlayerDifficultyCommon.h"
	#include "mapstate_common.h"
#endif

#include "AutoGen/Character_h_ast.h"
#include "AutoGen/CharacterAttribs_h_ast.h"
#include "AutoGen/EntCritter_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "DamageTracker_h_ast.h"
#include "AutoGen/PowerActivation_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/AttribMod_h_ast.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/StatPoints_h_ast.h"



extern ParseTable parse_CharacterAttribs[];
#define TYPE_parse_CharacterAttribs CharacterAttribs
extern ParseTable parse_KillCreditTeam[];
#define TYPE_parse_KillCreditTeam KillCreditTeam

bool g_bDebugStats = 0;


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STARTUP(Combat) ASTRT_DEPS(CombatConfig, Powers, CharacterClasses, CharacterPaths, CombatMods, PowerTrees, PowersAutoDesc, StatPointPools, EntityBuild, AS_CombatReactivePower, AS_CombatPowerStateSwitching);
void CombatStartup(void)
{

}

// helper function for character_FindPowerByRef/character_FindPowerByRefComplete
static Power* character_findPowerByRefInternal(Power *ppow, PowerRef *ppowref)
{
	if (ppowref->iLinkedSub >= 0)
	{
		if(eaSize(&ppow->ppSubCombatStatePowers) > ppowref->iLinkedSub)
		{
			ppow = ppow->ppSubCombatStatePowers[ppowref->iLinkedSub];
		}
		else
		{
			return NULL;
		}
	}

	if (ppowref->iIdxSub >= 0)
	{
		if(eaSize(&ppow->ppSubPowers)>ppowref->iIdxSub)
		{
			ppow = ppow->ppSubPowers[ppowref->iIdxSub];
		}
		else
		{
			return NULL;
		}
	}

	return ppow;
}

// Returns a Power owned generally by the Character, otherwise returns NULL
Power *character_FindPowerByRef(Character *pchar, PowerRef *ppowref)
{
	if(ppowref->uiID)
	{
		Power *ppow = character_FindPowerByID(pchar, ppowref->uiID);
		if(ppow)
		{	// first see if we are referencing a linked power within, and get that one.
			return character_findPowerByRefInternal(ppow, ppowref);
		}
	}
	return NULL;
}

// Returns a Power owned generally by the Character, otherwise returns NULL
Power *character_FindPowerByRefComplete(Character *pchar, PowerRef *ppowref)
{
	if(ppowref->uiID)
	{
		Power *ppow = character_FindPowerByIDComplete(pchar,ppowref->uiID);
		if(ppow)
		{
			return character_findPowerByRefInternal(ppow, ppowref);
		}
	}
	return NULL;
}

// Returns a Power owned generally by the Character, otherwise returns NULL
Power *character_FindPowerByDef(Character *pchar, PowerDef *pdef)
{
	int i;
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pchar->ppPowers[i]->hDef))
		{
			return pchar->ppPowers[i];
		}
		if(pchar->ppPowers[i]->ppSubCombatStatePowers)
		{
			FOR_EACH_IN_EARRAY(pchar->ppPowers[i]->ppSubCombatStatePowers, Power, pSubStatePower)
			{
				if (pdef == GET_REF(pSubStatePower->hDef))
					return pSubStatePower;
			}
			FOR_EACH_END
		}
	}
	return NULL;
}

Power *character_FindComboParentByDef(Character *pchar, PowerDef *pdef)
{
	int i,j;
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pchar->ppPowers[i]->hDef))
		{
			return pchar->ppPowers[i];
		}
		for (j=eaSize(&pchar->ppPowers[i]->ppSubPowers)-1; j>=0; j--)
		{
			if(pdef == GET_REF(pchar->ppPowers[i]->ppSubPowers[j]->hDef))
			{
				return pchar->ppPowers[i]->ppSubPowers[j];
			}
		}
	}
	return NULL;
}

// Returns a Power owned internally by the Character, otherwise returns NULL
Power *character_FindPowerByDefPersonal(Character *pchar, PowerDef *pdef)
{
	int i;
	for(i=eaSize(&pchar->ppPowersPersonal)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pchar->ppPowersPersonal[i]->hDef))
		{
			return pchar->ppPowersPersonal[i];
		}
	}
	return NULL;
}

// Returns a Power temporarily owned by the Character, otherwise returns NULL
Power *character_FindPowerByDefTemporary(Character *pchar, PowerDef *pdef)
{
	int i;
	for(i=eaSize(&pchar->ppPowersTemporary)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pchar->ppPowersTemporary[i]->hDef))
		{
			return pchar->ppPowersTemporary[i];
		}
	}
	return NULL;
}

// Returns a Power owned generally by the Character, otherwise returns NULL.
//  If the Character owns multiple instances of the PowerDef, it finds the newest one
Power *character_FindNewestPowerByDef(Character *pchar, PowerDef *pdef)
{
	Power *ppow = NULL;
	int i;
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pchar->ppPowers[i]->hDef))
		{
			Power *ppowComp = pchar->ppPowers[i];
			if(!ppow 
				|| ppowComp->uiTimeCreated > ppow->uiTimeCreated
				|| (ppowComp->uiTimeCreated == ppow->uiTimeCreated
					&& ppowComp->uiID > ppow->uiID))
			{
				ppow = ppowComp;
			}
		}
	}
	return ppow;
}

// Returns a Power owned internally by the Character, otherwise returns NULL.
//  If the Character owns multiple instances of the PowerDef, it finds the newest one
Power *character_FindNewestPowerByDefPersonal(Character *pchar, PowerDef *pdef)
{
	Power *ppow = NULL;
	int i;
	for(i=eaSize(&pchar->ppPowersPersonal)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pchar->ppPowersPersonal[i]->hDef))
		{
			Power *ppowComp = pchar->ppPowersPersonal[i];
			if(!ppow 
				|| ppowComp->uiTimeCreated > ppow->uiTimeCreated
				|| (ppowComp->uiTimeCreated == ppow->uiTimeCreated
				&& ppowComp->uiID > ppow->uiID))
			{
				ppow = ppowComp;
			}
		}
	}
	return ppow;
}

// Returns a Power* owned generally by the Character, otherwise returns NULL
Power *character_FindPowerByName(Character *pchar, const char *pchName)
{
	PowerDef *pdef = powerdef_Find(pchName);
	if(pdef)
	{
		return character_FindPowerByDef(pchar,pdef);
	}
	return NULL;
}

// Returns a Power* owned internally by the Character, otherwise returns NULL
Power *character_FindPowerByNamePersonal(Character *pchar, const char *pchName)
{
	PowerDef *pdef = powerdef_Find(pchName);
	if(pdef)
	{
		return character_FindPowerByDefPersonal(pchar,pdef);
	}
	return NULL;
}

// Sadly, this does an S32 compare of U32 data, because the Powers array is sorted as S32
static int CmpPowerByID(void *pvContext, const S32 *piID, const Power** ppPower)
{
	S32 iIDCompare = *piID;
	S32 iIDPower = (S32)((*ppPower)->uiID);
	return iIDCompare < iIDPower ? -1 : (iIDCompare==iIDPower ? 0 : 1);
}

// Returns the Power* if there is one in the indexed earray with the matching ID, otherwise returns NULL
//  TODO(JW): Optimize: Make this a #define?
Power *powers_IndexedFindPowerByID(Power *const *const *pppPowers, U32 uiID)
{
	PERFINFO_AUTO_START_FUNC();
	{
		int s = eaSize(pppPowers);
		if(s)
		{
			S32 iID = (S32)uiID;
			Power **ppPower = bsearch_s(&iID,*pppPowers,s,sizeof(void*),CmpPowerByID,NULL);
			PERFINFO_AUTO_STOP();
			return ppPower ? *ppPower : NULL;
		}
	}
	PERFINFO_AUTO_STOP();
	return NULL;
}

// Returns the Power* if the character owns a power with the matching ID, otherwise returns NULL
//  TODO(JW): Optimize: Make this a #define?
Power *character_FindPowerByID(const Character *pchar, U32 uiID)
{
	PERFINFO_AUTO_START_FUNC();
	{
		int s = eaSize(&pchar->ppPowers);
		if(s)
		{
			S32 iID = (S32)uiID;
			Power **ppPower = bsearch_s(&iID,pchar->ppPowers,s,sizeof(void*),CmpPowerByID,NULL);
			PERFINFO_AUTO_STOP();
			return ppPower ? *ppPower : NULL;
		}
	}
	PERFINFO_AUTO_STOP();
	return NULL;
}

// Returns the Power* if the characters owns a power with the matching ID, otherwise returns NULL
// Searches more than just the owned powers list, but all the items and power trees for powers that may have not been
// added to that list
// ADVANCED VERSION MAY NOT BE DESIRED, THINK ABOUT WHY YOU ARE USING THIS FUNCTION
Power *character_FindPowerByIDComplete(const Character *pchar, U32 uiID)
{
	Power *ppowReturn = NULL;
	PERFINFO_AUTO_START_FUNC();
	{
		//First, see if we can find it in the actual Powers list
		ppowReturn = character_FindPowerByID(pchar,uiID);

		if(!ppowReturn)
			ppowReturn = item_FindPowerByID(pchar->pEntParent,uiID,NULL,NULL,NULL,NULL);

		if(!ppowReturn)
			ppowReturn = character_FindPowerByIDTree(pchar,uiID,NULL,NULL);
	}
	PERFINFO_AUTO_STOP();
	return ppowReturn;
}

// Returns a Power owned generally by the Character, otherwise returns NULL
Power *character_FindPowerByIDSubIdx(Character *pchar, U32 uiID, S32 iSubIdx)
{
	Power *ppow = character_FindPowerByID(pchar,uiID);
	if(iSubIdx>=0 && ppow && iSubIdx<eaSize(&ppow->ppSubPowers))
	{
		ppow = ppow->ppSubPowers[iSubIdx];
	}
	return ppow;
}



// Returns the PowerDef* if the character owns a power with the matching ID, otherwise returns NULL
//  TODO(JW): Optimize: Make this a #define?
PowerDef *character_FindPowerDefByID(Character *pchar, U32 uiID)
{
	Power *ppow = character_FindPowerByID(pchar,uiID);
	return ppow ? GET_REF(ppow->hDef) : NULL;
}

// Returns the PowerDef* if the character owns a power with the matching ID and sub index, otherwise returns NULL
//  -1 is the proper sub index to pass if you're not looking for a child of a combo power
PowerDef *character_FindPowerDefByIDSubIdx(SA_PARAM_NN_VALID Character *pchar, U32 uiID, S32 iSubIdx)
{
	PowerDef *pdef = NULL;
	Power *ppow = character_FindPowerByID(pchar,uiID);
	if(ppow)
	{
		pdef = GET_REF(ppow->hDef);
		if(pdef && iSubIdx >= 0)
		{
			if(pdef->eType==kPowerType_Combo
				&& eaSize(&pdef->ppOrderedCombos) > iSubIdx)
			{
				pdef = GET_REF(pdef->ppOrderedCombos[iSubIdx]->hPower);
			}
			else
			{
				pdef = NULL;
			}
		}
	}
	return pdef;
}

// Returns the Power* if the character owns a power with the matching ID, otherwise returns NULL.  Optionally
//  also checks to make sure the name matches.
Power *character_FindPowerByIDAndName(Character *pchar, U32 uiID, const char *pchNameOptional)
{
	Power *ppow = character_FindPowerByID(pchar,uiID);
	if(ppow && pchNameOptional)
	{
		PowerDef *pdef = GET_REF(ppow->hDef);
		if(!(pdef && 0==stricmp(pdef->pchName,pchNameOptional)))
		{
			ppow = NULL;
		}
	}
	return ppow;
}

// Returns the first Power* found on the Character that has the given Category, otherwise returns NULL
// Excludes any Powers unavailable due to BecomeCritter
Power *character_FindPowerByCategory(Character *pchar, const char *cpchCategory)
{
	int iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,cpchCategory);
	if(iCategory>=0)
	{
		int i,s=eaSize(&pchar->ppPowers);
		for(i=0; i<s; i++)
		{
			PowerDef *pdef = GET_REF(pchar->ppPowers[i]->hDef);
			if(pdef && -1!=eaiFind(&pdef->piCategories,iCategory) && !(pchar->bBecomeCritter && pchar->ppPowers[i]->eSource!=kPowerSource_AttribMod))
			{
				return pchar->ppPowers[i];
			}
		}
	}
	return NULL;
}


// Checks whether the Limited Use entity bucket fields need to be sent
S32 character_LimitedUseCheckUpdate(Character* pchar)
{
	U32 uTimeNow = timeSecondsSince2000();

	if (!eaSize(&pchar->ppPowersLimitedUse))
		return false;

	if (pchar->uLimitedUseTimestamp == uTimeNow)
		return false;

	return pchar->bLimitedUseDirty;
}

// Updates the Limited Use data for diffing next frame
void character_LimitedUseUpdate(Character* pchar)
{
	U32 uTimeNow = timeSecondsSince2000();

	if (uTimeNow != pchar->uLimitedUseTimestamp && pchar->bLimitedUseDirty)
	{
		pchar->uLimitedUseTimestamp = uTimeNow;
		pchar->bLimitedUseDirty = false;
	}
}

// Sends the Character's Limited Use data
void character_LimitedUseSend(Character* pchar, Packet* pak)
{
	int i, iSize = eaSize(&pchar->ppPowersLimitedUse);
	Power** eaPowersToSend = NULL;

	eaStackCreate(&eaPowersToSend, iSize);

	//filter out powers that don't need to be sent
	for(i = 0; i < iSize; i++)
	{
		Power* pPower = pchar->ppPowersLimitedUse[i];
		PowerDef* pDef = GET_REF(pPower->hDef);
		if (pDef->fLifetimeGame  || pDef->fLifetimeUsage)
			eaPush(&eaPowersToSend, pPower);
	}

	iSize = eaSize(&eaPowersToSend);
	pktSendBitsAuto(pak, iSize);

	for(i = 0; i < iSize; i++)
	{
		Power* pPower = eaPowersToSend[i];
		pktSendBitsAuto(pak, pPower->uiID);
		pktSendF32(pak, pPower->fLifetimeGameUsed);
		pktSendF32(pak, pPower->fLifetimeUsageUsed);
	}
	eaDestroy(&eaPowersToSend);
}

// Receives the Character's Limited Use data.  Safely consumes the data if there isn't a Character.
void character_LimitedUseReceive(Character* pchar, Packet* pak)
{
	U32 i, s = pktGetBitsAuto(pak);

	for (i = 0; i < s; i++)
	{
		U32 uPowerID = pktGetBitsAuto(pak);
		F32 fLifetimeGameUsed = pktGetF32(pak);
		F32 fLifetimeUsageUsed = pktGetF32(pak);

		if(pchar)
		{
			Power* pPower = character_FindPowerByID(pchar, uPowerID);
			
			if (!pPower)
				pPower = item_FindPowerByID(pchar->pEntParent, uPowerID, NULL, NULL, NULL, NULL);
			
			if (pPower)
			{
				pPower->fLifetimeGameUsed = fLifetimeGameUsed;
				pPower->fLifetimeUsageUsed = fLifetimeUsageUsed;
			}
		}
	}
}

// Sends the Character's Charge Data
void character_ChargeDataSend(Character *pchar, Packet *pak)
{
	if(pchar->pChargeData)
	{
		pktSendBool(pak,1);
		pktSendString(pak,REF_STRING_FROM_HANDLE(pchar->pChargeData->hMsgName));
		pktSendF32(pak,pchar->pChargeData->fTimeCharge);
		pktSendU32(pak,pchar->pChargeData->uiTimestamp);
	}
	else
	{
		pktSendBool(pak,0);
	}
}

// Receives the Character's Charge Data.  Safely consumes the data if there isn't a Character.
void character_ChargeDataReceive(Character *pchar, Packet *pak)
{
	bool b = pktGetBool(pak);

	if(pchar)
		StructDestroySafe(parse_CharacterChargeData,&pchar->pChargeData);

	if(b)
	{
		char buf[256];
		F32 fTimeCharge;
		U32 uiTimestamp;
		pktGetString(pak,SAFESTR(buf));
		fTimeCharge = pktGetF32(pak);
		uiTimestamp = pktGetU32(pak);

		if(pchar)
		{
			pchar->pChargeData = StructAlloc(parse_CharacterChargeData);
			SET_HANDLE_FROM_STRING(gMessageDict,buf,pchar->pChargeData->hMsgName);
			pchar->pChargeData->fTimeCharge = fTimeCharge;
			// Note - this timestamp is the server's pmTimestamp(0) as of the
			//  charge starting.  In theory the client's own pmTimestamp(0) should
			//  roughly match, but it doesn't necessarily account for network latency,
			//  so some adjustment may need to be performed here.
			pchar->pChargeData->uiTimestamp = uiTimestamp;
		}
	}
}



static void CharacterResetPowersArrayLimitedUse(SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract)
{
	int i;

	// Quick clear rather than destroy
	eaClearFast(&pchar->ppPowersLimitedUse);

	// Check Personal Powers
	for(i=eaSize(&pchar->ppPowersPersonal)-1; i>=0; i--)
	{
		PowerDef *pdef = GET_REF(pchar->ppPowersPersonal[i]->hDef);
		if(pdef && pdef->bLimitedUse)
		{
			eaPush(&pchar->ppPowersLimitedUse,pchar->ppPowersPersonal[i]);
		}
	}

	// Check AttribMod Powers
	for(i=eaSize(&pchar->modArray.ppPowers)-1; i>=0; i--)
	{
		PowerDef *pdef = GET_REF(pchar->modArray.ppPowers[i]->hDef);
		if(pdef && pdef->bLimitedUse)
		{
			eaPush(&pchar->ppPowersLimitedUse,pchar->modArray.ppPowers[i]);
		}
	}

	// Check Item Powers
	item_UpdateLimitedUsePowers(pchar->pEntParent, pExtract);

	// Dirty Limited Use Powers
	pchar->uLimitedUseTimestamp = timeSecondsSince2000();
	pchar->bLimitedUseDirty = true;
}

// List of EntityRefs that need to perform a reset
static int *s_perResetPowersArray = NULL;

static void CharacterResetPowersArrayTimedCallback(TimedCallback *pCallback, F32 fTime, UserData data)
{
	int i;
	for(i=eaiSize(&s_perResetPowersArray)-1; i>=0; i--)
	{
		EntityRef er = (EntityRef)s_perResetPowersArray[i];
		Entity *e = entFromEntityRefAnyPartition(er);
		if(e && e->pChar)
		{
			// If successful, will cause the EntityRef to get removed from the array using FindAndRemoveFast
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			character_ResetPowersArray(entGetPartitionIdx(e), e->pChar, pExtract);
		}
		else
		{
			eaiRemoveFast(&s_perResetPowersArray,i);
		}
	}

	if(eaiSize(&s_perResetPowersArray))
	{
		TimedCallback_Run(CharacterResetPowersArrayTimedCallback,NULL,2.f);
	}
}

// Flag the server sets when it does a reset, to temporarily disable the changed callback
static int s_bDisableChangedCallback = false;

// Causes the character to clear the existing powers array and refill it with the appropriate Powers
//  from various systems.  Called on the server after loading from the db, and called on the client
//  when needed.
void character_ResetPowersArray(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	int i,s=0;
	int bFailed = false;
	U32 *puiIDs = NULL;
	static Power **s_ppPowersAdded = NULL;
	static Power **s_ppPowersRemoved = NULL;
	static Power **s_ppPowersChanged = NULL;
	Power **ppOldPropPowers = NULL;
	RegionRules *pRegionRules = NULL;

#ifdef GAMESERVER
	//don't reset if this entity still needs to complete a puppet swap
	if (!entity_puppetSwapComplete(pchar->pEntParent))
	{
		// If this horrible early exit is hit, make sure we at least flag the Character
		//  as still needing a reset so it gets fixed asap.
		pchar->bResetPowersArray = true;
		// If the powers array isn't reset, the following powers lists need to be cleared
		//  because they cannot be trusted to have valid powers
		eaClear(&pchar->ppPowers);
		eaClear(&pchar->ppPowersLimitedUse);
		eaClear(&pchar->modArray.ppPowers);
		return;
	}
#endif

	PERFINFO_AUTO_START_FUNC();

#ifdef GAMESERVER

	if(entGetWorldRegionTypeOfEnt(pchar->pEntParent) == WRT_None)
		gslCacheEntRegion(pchar->pEntParent,pExtract);

#endif

	pRegionRules = getRegionRulesFromEnt(pchar->pEntParent);

#ifdef GAMESERVER

	if(pchar->pEntParent->aibase)
	{
		aiPowersResetPowersBegin(pchar->pEntParent,pchar->ppPowers);
	}

	// Build the list of pre-existing uiIDs.  If we've got a non-empty
	//  ppPowersResetCache we use that, otherwise build an array of power IDs from the tray
	s = eaSize(&pchar->ppPowersResetCache);
	if(!s)
	{
		entity_BuildPowerIDListFromTray(pchar->pEntParent, &puiIDs);
	}
	else
	{
		for(i=0; i<s; i++)
		{
			ea32Push(&puiIDs,pchar->ppPowersResetCache[i]->uiID);
		}
	}

	// Temporarily disable the changed callback, will perform the callback and re-enable
	//  at the end of the function
	s_bDisableChangedCallback = true;
	
#endif // GAMESERVER

	// clear our list of entCreate enhancements
	eaClear(&pchar->ppPowersEntCreateEnhancements);

	// Clear the main list and recreate it as indexed
	eaClear(&pchar->ppPowers);
	eaIndexedEnable(&pchar->ppPowers,parse_Power);

#ifdef GAMESERVER
	// Reset powers in power slots
	PowerReplace_reset(pchar->pEntParent);

	Entity_ReBuildPowerReplace(pchar->pEntParent);
#endif

	// Add all Powers in the personal list
	for(i=0; i<eaSize(&pchar->ppPowersPersonal); i++)
	{
		character_AddPower(iPartitionIdx,pchar,pchar->ppPowersPersonal[i],kPowerSource_Personal,pExtract);
	}

	// Add all Powers in the class list
	if(!IS_HANDLE_ACTIVE(pchar->hClassTemporary))
	{
		for(i=0; i<eaSize(&pchar->ppPowersClass); i++)
		{
			character_AddPower(iPartitionIdx,pchar,pchar->ppPowersClass[i],kPowerSource_Class,pExtract);
		}
	}
	// Add all Powers in the species list
	for(i=0; i<eaSize(&pchar->ppPowersSpecies); i++)
	{
		character_AddPower(iPartitionIdx,pchar,pchar->ppPowersSpecies[i],kPowerSource_Species,pExtract);
	}

	// Add all Powers in the temporary list
	for(i=0; i<eaSize(&pchar->ppPowersTemporary); i++)
	{
		character_AddPower(iPartitionIdx,pchar,pchar->ppPowersTemporary[i], kPowerSource_Temporary, pExtract);
	}

	// Add all Powers from PowerTrees
	if (!character_AddPowersFromPowerTrees(iPartitionIdx,pchar,pExtract))
	{
		bFailed = true;
	}

	// Add all Powers from Items
	if(pchar->pEntParent)
	{
		if(!item_AddPowersFromItems(iPartitionIdx, pchar->pEntParent, pExtract))
		{
			bFailed = true;
		}
	}

	// Add all Powers from AttribMods
	character_AddPowersFromAttribMods(iPartitionIdx,pchar,pExtract);


#ifdef GAMESERVER 
	eaCopy(&ppOldPropPowers, &pchar->ppPowersPropagation);
	eaDestroy(&pchar->ppPowersPropagation);

	if(pchar->pEntParent->pSaved)
	{
		ent_PropagatePowers(iPartitionIdx, pchar->pEntParent, ppOldPropPowers, pExtract);
	}

	// Free all Propagated powers
	eaDestroyStruct(&ppOldPropPowers,parse_Power);
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
#endif


	// Add all Powers from Propagation
	for(i=0; i<eaSize(&pchar->ppPowersPropagation); i++)
	{
		character_AddPower(iPartitionIdx,pchar,pchar->ppPowersPropagation[i],kPowerSource_Propagation,pExtract);
	}

	// Innates may not have changed, but we'll dirty them anyway
	character_DirtyInnatePowers(pchar);

	if(pRegionRules && ea32Size(&pRegionRules->piCategoryDoNotAdd) > 0)
	{
		int iCat;

		for(iCat=0;iCat<ea32Size(&pRegionRules->piCategoryDoNotAdd);iCat++)
		{
			for(i=eaSize(&pchar->ppPowers)-1;i>=0;i--)
			{
				PowerDef *pDef = GET_REF(pchar->ppPowers[i]->hDef);
				if(!pDef)
				{
					bFailed = true;
				}
				else if(ea32Find(&pDef->piCategories,pRegionRules->piCategoryDoNotAdd[iCat]) > -1)
				{
					eaRemove(&pchar->ppPowers,i);
				}
			}
		}	
	}


	// all powers that have enhancements and apply the enhanced fields to the power
	power_CalculateAttachEnhancementPowerFields(iPartitionIdx, pchar);


		
	// At this point the ppPowers array has been completely rebuilt.
	//  Now we can check what has changed, vanished or been added.
	// Note that for changed or added Powers, the client will request
	//  updated info, including recharge time.
	// TODO(JW): Due to the way adding Items works, the recharge tracking
	//  system needs to be re-implemented to be robust.  That would obviate
	//  the need to do all the client->server->client recharge sending here,
	//  which is non-optimal in latency (and just kinda dumb).  If that ever
	//  gets fixed, also remove the similar client update in character_AddPower()
	for(i=eaSize(&pchar->ppPowersResetCache)-1; i>=0; i--)
	{
		PowerResetCache *pCache = pchar->ppPowersResetCache[i];
		void *pvPower = pCache->pvPower;
		S32 iIndex = eaFind(&pchar->ppPowers,pvPower);
		if(iIndex>=0)
		{
			Power *ppow = pchar->ppPowers[iIndex];
			ppow->bNeedsResetCache = false;
			if(ppow->uiID!=pCache->uiID)
			{
				// Power has magically transformed into a different Power, fix the PowerResetCache

#ifdef GAMECLIENT
				// Before fixing the cache we need to manually swap fTimeRecharge on the client
				Power *ppowWas = character_FindPowerByIDComplete(pchar,pCache->uiID);
				if(ppowWas)
				{
					F32 fTimeRechargeSwap = ppowWas->fTimeRecharge;
					power_SetRecharge(iPartitionIdx,pchar,ppowWas,ppow->fTimeRecharge);
					power_SetRecharge(iPartitionIdx,pchar,ppow,fTimeRechargeSwap);
				}
				// Also request up to date information from the server on both Power IDs
				ServerCmd_power_requestinfo(ppow->uiID);
				ServerCmd_power_requestinfo(pCache->uiID);
#endif

				eaPush(&s_ppPowersChanged, ppow);
				pCache->uiID = ppow->uiID;
			}
		}
		else
		{
			// Power* disappeared, delete the PowerResetCache
			eaPush(&s_ppPowersRemoved, pvPower);
			free(pCache);
			eaRemoveFast(&pchar->ppPowersResetCache,i);
		}
	}

	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		if(ppow->bNeedsResetCache)
		{
			// Power is new, add a PowerResetCache
			PowerResetCache *pCache = malloc(sizeof(PowerResetCache));
			pCache->pvPower = (void*)ppow;
			pCache->uiID = ppow->uiID;
			eaPush(&pchar->ppPowersResetCache, pCache);
			eaPush(&s_ppPowersAdded, ppow);
			ppow->bNeedsResetCache = false;
#ifdef GAMECLIENT
			ServerCmd_power_requestinfo(ppow->uiID);
#endif
		}
	}


#ifdef GAMESERVER

	if(pchar->pEntParent->aibase)
	{
		aiPowersResetPowersEnd(pchar->pEntParent,pchar->ppPowers);
	}

	// Rebuild the limited use array
	CharacterResetPowersArrayLimitedUse(pchar, pExtract);

	// Perform the changed callback, and re-enable it for general use
	if(combatcbCharacterPowersChanged) combatcbCharacterPowersChanged(pchar);
	s_bDisableChangedCallback = false;

	// Dirty all systems which own Powers, just to be safe
	character_DirtyPowerTrees(pchar);
	character_DirtyItems(pchar);

	// If this is a real Entity, do the autofill work
	if(entGetRef(pchar->pEntParent))
	{
		SavedTray* pTray = entity_GetActiveTray(pchar->pEntParent);
		// TODO(JW): HACK: if there aren't any tray elements, throw away the IDs
		if(pTray && !eaSize(&pTray->ppTrayElems))
		{
			ea32Clear(&puiIDs);
		}
		entity_TrayAutoFill(pchar->pEntParent, puiIDs);
		character_PowerSlotsAutoSet(iPartitionIdx, pchar, puiIDs, pExtract);
	}

	// Log names of added IDs
	if (gbEnablePowersDataLogging)
	{
		if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
			s = eaSize(&pchar->ppPowers);
			for(i=0; i<s; i++)
			{
				if(-1 == eaiFind(&puiIDs,pchar->ppPowers[i]->uiID))
				{
					PowerDef *pdef = GET_REF(pchar->ppPowers[i]->hDef);
					if(pdef)
					{
						entLog(LOG_PLAYER,pchar->pEntParent,"AddPower","power %s",pdef->pchName);
					}
				}
			}
		}
	}

#endif // GAMESERVER

#ifdef GAMECLIENT

	// On the client, if we failed, set up a callback to try again later, otherwise
	//  make sure we don't reset again for this entity in the failure callback.
	if(bFailed)
	{
		EntityRef er = entGetRef(pchar->pEntParent);
		s = eaiSize(&s_perResetPowersArray);
		eaiPushUnique(&s_perResetPowersArray,er);
		if(!s)
		{
			// List was empty, so start the callback
			TimedCallback_Run(CharacterResetPowersArrayTimedCallback,NULL,2.f);
		}
	}
	else
	{
		EntityRef er = entGetRef(pchar->pEntParent);
		i = eaiFind(&s_perResetPowersArray,er);
		eaiRemoveFast(&s_perResetPowersArray,i);
	}

#endif // GAMECLIENT

	eaiDestroy(&puiIDs);
	eaClearFast(&s_ppPowersAdded);
	eaClearFast(&s_ppPowersRemoved);
	eaClearFast(&s_ppPowersChanged);

	pchar->bResetPowersArray = false;

	PERFINFO_AUTO_STOP();
}


typedef enum EComboPowerPickFailReason
{
	EComboPowerPickFailReason_NONE,
	EComboPowerPickFailReason_BAD_COMBO,
	EComboPowerPickFailReason_EXPR_REQUIRES,
	EComboPowerPickFailReason_CANNOT_WARP,
	EComboPowerPickFailReason_FAILEDMODE_REQUIRE,
	EComboPowerPickFailReason_FAILEDMODE_EXCLUDE,
	EComboPowerPickFailReason_EXPR_CLIENT_TARGET,

} EComboPowerPickFailReason;

static bool character_CanPickComboPower(int iPartitionIdx,
										Character *pchar,
										Entity *eTarget,
										PowerActivation *pact, 
										Power *ppow,
										PowerDef *pdef,
										S32 iComboIndex,
										bool *pbShouldSetHardTarget,
										Entity **ppentTargetOut,
										WorldInteractionNode **ppnodeTargetOut,
										EComboPowerPickFailReason *pFailReasonOut)
{
	CombatEvalPrediction ePredict = kCombatEvalPrediction_None;
	PowerCombo *pCombo = eaGet(&pdef->ppOrderedCombos, iComboIndex);
	PowerDef *pComboPowerDef = pCombo ? GET_REF(pCombo->hPower) : NULL;

	if (!pComboPowerDef)
	{
		if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_BAD_COMBO;
		return false;
	}
	
	if(pComboPowerDef && pComboPowerDef->bHasWarpAttrib)
	{
		if (!character_CanUseWarpPower(pchar, pComboPowerDef))
		{
			if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_CANNOT_WARP;
			return false;	
		}
	}

	if(ea32Size(&pCombo->piModeRequire))
	{
		int j;

		for(j=0;j<ea32Size(&pCombo->piModeRequire);j++)
		{
			if(ea32Find(&pchar->piPowerModes,pCombo->piModeRequire[j]) == -1)
			{
				if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_FAILEDMODE_REQUIRE;
				return false;	
			}
		}
	}

	if(ea32Size(&pCombo->piModeExclude))
	{
		int j;

		for(j=0;j<ea32Size(&pCombo->piModeExclude);j++)
		{
			if(ea32Find(&pchar->piPowerModes,pCombo->piModeExclude[j]) != -1)
			{
				if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_FAILEDMODE_EXCLUDE;
				return false;
			}
		}
	}

	if(pdef->bComboTargetRules)
	{

#ifdef GAMESERVER
		if(pCombo->pExprTargetClient && pact->iPredictedIdx>=0 && iComboIndex != pact->iPredictedIdx)
		{
			if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_EXPR_CLIENT_TARGET;
			return false;
		}
#endif

#ifdef GAMECLIENT
		if(ppentTargetOut && ppnodeTargetOut)
		{
			assert(pchar->pEntParent);
			if(!eTarget)
			{
				// No target entity passed in (should be the case on the client), see if we can find a legal one
				ClientTargetDef *pTarget = clientTarget_SelectBestTargetForPower(pchar->pEntParent, 
						ppow->ppSubPowers[iComboIndex], pbShouldSetHardTarget);
				if(pCombo->pExprTargetClient && !pTarget->entRef && !IS_HANDLE_ACTIVE(pTarget->hInteractionNode))
				{
					// Turns out we can't find a legal target, so don't pick this subpower
					if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_EXPR_CLIENT_TARGET;
					return false;
				}
				else
				{
					*ppentTargetOut = entFromEntityRef(iPartitionIdx, pTarget->entRef);
					*ppnodeTargetOut = GET_REF(pTarget->hInteractionNode);
				}
			}
			else
			{
				if(pCombo->pExprTargetClient && !target_IsLegalTargetForExpression(pchar->pEntParent,pCombo->pExprTargetClient,eTarget,NULL))
				{
					// Turns out the selected target isn't a legal target, so don't pick this subpower
					if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_EXPR_CLIENT_TARGET;
					return false;
				}
			}
		}
#endif
			
	}


	if(pCombo->pExprRequires)
	{
		if(pact->iPredictedIdx >= 0)
		{
			ePredict = (iComboIndex == pact->iPredictedIdx) ? kCombatEvalPrediction_True : kCombatEvalPrediction_False;
		}

		combateval_ContextReset(kCombatEvalContext_Activate);
		combateval_ContextSetupActivate(pchar,eTarget ? eTarget->pChar : NULL,pact,ePredict);
		if(!combateval_EvalNew(iPartitionIdx, pCombo->pExprRequires,kCombatEvalContext_Activate,NULL))
		{
			if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_EXPR_REQUIRES;
			return false;
		}
	}

	if (pFailReasonOut) *pFailReasonOut = EComboPowerPickFailReason_NONE;
	return true;
}


// Determines what power would actually be activated if the character attempted to activate the
//  supplied power.  Takes replacement powers into account, if bUseReplacementPower is set.  Will 
//	return the supplied power or replacement if it's not a combo, or one of the combo's sub powers.  
//	Target may be NULL, but should be passed in if known.  Activation may also be NULL. In certain 
//	cases, this function may desire to switch targets, and if so, it will return the new target in 
//	the out target.
Power *character_PickActivatedPower(int iPartitionIdx,
								    Character *pchar,
									Power *ppow,
									Entity *eTarget,
									Entity **ppentTargetOut,
									WorldInteractionNode **ppnodeTargetOut,
									bool *pbShouldSetHardTarget,
									PowerActivation *pact,
									S32 bQuiet,
									S32 bUseReplacementPower,
									ActivationFailureReason *peFailOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	static PowerActivation *s_pact = NULL;
	static S32 s_bClientBadCombo = false;
	Power *pPickedPower = NULL;
	PowerDef *pdef;

	if(bUseReplacementPower && ppow->uiReplacementID)
	{
		Power *ppowReplace = character_FindPowerByID(pchar,ppow->uiReplacementID);
		if (!bQuiet)
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate Replace%s: %d with %d\n",ppowReplace?"":" FAIL",ppow->uiID,ppow->uiReplacementID);
		if(ppowReplace)
		{
			ppow = ppowReplace;
		}
	}

	pdef = GET_REF(ppow->hDef);

	if(pdef && pdef->eType==kPowerType_Combo)
	{
		int i, iCombos, iPowers;

		if(!s_pact)
		{
			s_pact = poweract_Create();
		}

		if(!entIsServer())
		{
			// For client prediction, make a fake activation
			if(!pact)
			{
				// TODO(JW): Activate: Add more cleanup and useful predictive stuff here if it turns out we need it
				pact = s_pact;
				pact->uiTimestampQueued = pmTimestamp(0);
				pact->iPredictedIdx = -1;
			}
		}

		devassert(pact);

		iCombos = eaSize(&pdef->ppOrderedCombos);
		iPowers = eaSize(&ppow->ppSubPowers);

		// These are required to match.  If they don't, something has gone wrong.  On the server, probably
		//  something horrible.  On the client it may be slightly less horrible, and this may get called
		//  a lot, so only report once.
		if(iCombos!=iPowers)
		{
			if(entGetRef(pchar->pEntParent)	// AWESOME HACK! - Yet another hack because pet powers don't really exist but they're using the tray code as if they do
				&& (entIsServer() || !s_bClientBadCombo))
			{
				ErrorDetailsf("Character %s, Combo %s, Def %d, Power %d",CHARDEBUGNAME(pchar),pdef->pchName,iCombos,iPowers);
				devassert(iCombos==iPowers || (entIsServer() ? false : eaFind(&pchar->ppPowers,ppow) == -1));
				if(!entIsServer())
					s_bClientBadCombo = true;
			}
			return ppow;
		}

		for(i=0; i<iCombos; i++)
		{
			if(character_CanPickComboPower(iPartitionIdx, pchar, eTarget, pact, ppow, pdef, i, 
											pbShouldSetHardTarget, ppentTargetOut, ppnodeTargetOut, NULL))
			{
				pPickedPower = ppow->ppSubPowers[i];

				// Slightly hacky solution to combo powers that don't target self, that
				//  include self-targeted powers: If the child requires Self for the
				//  target, just forcibly switch the targt to yourself.
				if(ppentTargetOut || ppnodeTargetOut)
				{
					PowerTarget *pTarget;
					PowerDef *pdefChild = GET_REF(ppow->hDef);;
					pTarget = pdefChild ? GET_REF(pdefChild->hTargetMain) : NULL;
					if(pTarget && pTarget->bRequireSelf)
					{
						if(ppentTargetOut)
							*ppentTargetOut = pchar->pEntParent;
						if(ppnodeTargetOut)
							*ppnodeTargetOut = NULL;
					}
				}
				
				break;
			}
		}

	
		if(pact->iPredictedIdx >= 0 && pact->iPredictedIdx != i)
		{
			// there was a misprediction about what sub power to use! 
			// see if we will trust the client with their prediction 

			bool bUseClientPrediction = false;
						
			if (pchar->uiComboMispredictHeuristic < 10 && 
				pact->iPredictedIdx < eaSize(&ppow->ppSubPowers) &&
				g_CombatConfig.piPowerCategoriesAllowMispredict && 
				pdef->piCategories)
			{
				bUseClientPrediction = powerdef_hasCategory(pdef, g_CombatConfig.piPowerCategoriesAllowMispredict);
			}

			if (bUseClientPrediction)
			{
				EComboPowerPickFailReason eClientFailReason;

				character_CanPickComboPower(iPartitionIdx, pchar, eTarget, pact, ppow, pdef, 
											pact->iPredictedIdx, NULL, NULL, NULL, &eClientFailReason);

				if (eClientFailReason == EComboPowerPickFailReason_EXPR_REQUIRES)
				{
					pchar->uiComboMispredictHeuristic += 3;
				}
				else if (eClientFailReason == EComboPowerPickFailReason_FAILEDMODE_REQUIRE || 
							eClientFailReason == EComboPowerPickFailReason_FAILEDMODE_EXCLUDE)
				{
					pchar->uiComboMispredictHeuristic += 15;
				}
				else
				{
					bUseClientPrediction = false;
				}

			}

			if (bUseClientPrediction)
			{
				pPickedPower = ppow->ppSubPowers[pact->iPredictedIdx];
				if (!bQuiet)
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent,"**** Handled MISPREDICT: Character predicted child %d, server picked child %d\n",pact->iPredictedIdx,i);
			}
			else
			{
				if (!bQuiet)
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent,"Activate MISPREDICT: Character predicted child %d, server picked child %d\n",pact->iPredictedIdx,i);

				if(peFailOut && (!*peFailOut))
					*peFailOut = kActivationFailureReason_ComboMispredict;
			}
		}
		else if (pchar->uiComboMispredictHeuristic)
		{	// for every time we agree, bring down the mispredict
			pchar->uiComboMispredictHeuristic--;
		}

		if (pPickedPower)
			ppow = pPickedPower;
	}


	return ppow;
#endif
}

// Cancels and removes all mods on a character.  Optionally allowed mods marked to survive
//  death to linger.  Will also trigger expiration processing if the expiration reason is
//  something other than Unset.
void character_RemoveAllMods(int iPartitionIdx, Character *pchar, S32 bAllowSurvival, S32 bUnownedModsOnly, ModExpirationReason eReason, GameAccountDataExtract *pExtract)
{
	int i;

	// TODO(JW): Move all these arrays into the modArray, make sure all additions have a matching cancel, and
	//  if so remove the eaDestroy for types that might be relevant past death as other Characters tick, like
	//  DamageTriggers.
	eaDestroy(&pchar->ppModsDamageTrigger);
	eaDestroy(&pchar->ppModsShield);
	eaDestroy(&pchar->ppModsTaunt);
	eaDestroy(&pchar->ppModsAttribModFragilityHealth);
	eaDestroy(&pchar->ppModsAttribModFragilityScale);
	eaDestroy(&pchar->ppModsAttribModShieldPercentIgnored);
	eaDestroy(&pchar->ppCostumeChanges);
	eaDestroy(&pchar->ppCostumeModifies);
	eaDestroy(&pchar->ppRewardModifies);
	eaDestroy(&pchar->ppModsAIAggro);
	pchar->bTauntActive = false;

	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		if(pmod)
		{
			if(bAllowSurvival && pmod->pDef && pmod->pDef->bSurviveTargetDeath)
			{
				continue;
			}
			if(bUnownedModsOnly && pmod->erOwner==entGetRef(pchar->pEntParent))
			{
				continue;
			}

			if(eReason != kModExpirationReason_Unset)
			{
				character_ModExpireReason(NULL, pmod, eReason);
				character_ApplyModExpiration(iPartitionIdx,pchar,pmod,pExtract);
			}
			mod_Cancel(iPartitionIdx,pchar->modArray.ppMods[i],pchar,true,NULL,pExtract);
		}
	}

	// sets dirty bit
	modarray_RemoveAll(pchar, &pchar->modArray, bAllowSurvival, bUnownedModsOnly);
}

// Cancels all mods from the given power. erSource cannot be 0.  Optionally disables instead of cancels.
void character_CancelModsFromPowerID(Character *pchar,
									 U32 uiPowerID,
									 EntityRef erSource,
									 U32 uiActivationID,
									 S32 bDisable)
{
	int i;

	if(!erSource)
		return;

	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		if(pmod && pmod->uiPowerID == uiPowerID && erSource == pmod->erSource
			&& (!uiActivationID || pmod->uiActIDServer == uiActivationID))
		{
			if(bDisable)
			{
				pmod->bDisabled = true;
			}
			else
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}
		}
	}

	for(i=eaSize(&pchar->modArray.ppModsPending)-1;i>=0;i--)
	{
		AttribMod *pmod = pchar->modArray.ppModsPending[i];
		if(pmod && pmod->uiPowerID == uiPowerID && erSource == pmod->erSource && 
			(!uiActivationID || pmod->uiActIDServer == uiActivationID))
		{
			eaRemove(&pchar->modArray.ppModsPending,i);
			mod_Destroy(pmod);
		}
	}
}

// Cancels all mods from the given def.  Optionally disables instead of cancels.
//  If erSource is non-zero, only cancels the mods from that source.
void character_CancelModsFromDef(Character *pchar,
								 PowerDef *pdef,
								 EntityRef erSource,
								 U32 uiActivationID,
								 S32 bDisable)
{
	int i;

	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		if(pmod && pmod->pDef && pmod->pDef->pPowerDef == pdef 
			&& (!erSource || erSource==pmod->erSource)
			&& (!uiActivationID || pmod->uiActIDServer == uiActivationID))
		{
			if(bDisable)
			{
				pmod->bDisabled = true;
			}
			else
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}
		}
	}
	for(i=eaSize(&pchar->modArray.ppModsPending)-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppModsPending[i];
		if(pmod && pmod->pDef && pmod->pDef->pPowerDef == pdef
			&& (!erSource || erSource==pmod->erSource)
			&& (!uiActivationID || pmod->uiActIDServer == uiActivationID))
		{
			eaRemove(&pchar->modArray.ppModsPending,i);
			mod_Destroy(pmod);
		}
	}
}

void character_CacheAttribMods(Character *pchar, AttribMod ***pppOldCostumeChanges, AttribMod ***pppOldCostumeModifies, AttribMod ***pppOldRewardModifies)
{
	// Clean out the old costume attrib mods
	if(pchar->ppCostumeChanges)
	{
		eaCopy(pppOldCostumeChanges,&pchar->ppCostumeChanges);
		eaClearFast(&pchar->ppCostumeChanges);
	}
	if(pchar->ppCostumeModifies)
	{
		eaCopy(pppOldCostumeModifies,&pchar->ppCostumeModifies);
		eaClearFast(&pchar->ppCostumeModifies);
	}
	if(pchar->ppRewardModifies)
	{
		eaCopy(pppOldRewardModifies,&pchar->ppRewardModifies);
		eaClearFast(&pchar->ppRewardModifies);
	}
}

void character_RegenCostumeIfChanged(Character *pchar, AttribMod ***pppOldCostumeChanges, AttribMod ***pppOldCostumeModifies)
{
	S32 bRegenCostume = false;
	
	PERFINFO_AUTO_START_FUNC();
	
	if (eaSize(&pchar->ppCostumeChanges) > 0)
	{
		// Check if old costume changes match current costume changes
		if (eaSize(&pchar->ppCostumeChanges) == eaSize(pppOldCostumeChanges))
		{
			int i;
			eaQSort(pchar->ppCostumeChanges,attrib_sortfunc);

			for(i=eaSize(&pchar->ppCostumeChanges)-1; i>=0; --i)
			{
				if (pchar->ppCostumeChanges[i] != (*pppOldCostumeChanges)[i])
				{
					bRegenCostume = true;
					break;
				}
			}
		}
		else
		{
			bRegenCostume = true;
		}
	}
	else if (eaSize(pppOldCostumeChanges) > 0)
	{
		bRegenCostume = true;
	}

	if (!bRegenCostume && eaSize(&pchar->ppCostumeModifies) > 0)
	{
		// Check if old costume changes match current costume changes
		if (eaSize(&pchar->ppCostumeModifies) == eaSize(pppOldCostumeModifies))
		{
			int i;
			eaQSort(pchar->ppCostumeModifies,attrib_sortfunc);
			for(i=eaSize(&pchar->ppCostumeModifies)-1; i>=0; --i)
			{
				if (pchar->ppCostumeModifies[i] != (*pppOldCostumeModifies)[i])
				{
					bRegenCostume = true;
					break;
				}
			}
		}
		else
		{
			bRegenCostume = true;
		}
	}
	else if (eaSize(pppOldCostumeModifies) > 0)
	{
		bRegenCostume = true;
	}

	if (bRegenCostume)
	{
		costumeEntity_RegenerateCostume(pchar->pEntParent);
	}

	eaClearFast(pppOldCostumeChanges);
	eaClearFast(pppOldCostumeModifies);

	PERFINFO_AUTO_STOP();
}

void character_RegenRewardsIfChanged(Character *pchar, AttribMod ***pppRewardModifies)
{
	S32 bRegenRewards = false;
	int i;

	if(pchar && pchar->pEntParent && entGetType(pchar->pEntParent) == GLOBALTYPE_ENTITYPLAYER)
	{
		PERFINFO_AUTO_START_FUNC();

		if (!pppRewardModifies)
		{
			bRegenRewards = true;
		}
		else if (eaSize(&pchar->ppRewardModifies) > 0)
		{
			// Check if old costume changes match current costume changes
			if (eaSize(&pchar->ppRewardModifies) == eaSize(pppRewardModifies))
			{	
				eaQSort(pchar->ppRewardModifies,attrib_sortfunc);

				for(i=eaSize(&pchar->ppRewardModifies)-1; i>=0; --i)
				{
					if (pchar->ppRewardModifies[i] != (*pppRewardModifies)[i])
					{
						bRegenRewards = true;
						break;
					}
				}
			}
			else
			{
				bRegenRewards = true;
			}
		}
		else if (eaSize(pppRewardModifies) > 0)
		{
			bRegenRewards = true;
		}

		if (bRegenRewards)
		{
			RewardModifierList *pRewardModList = StructCreate(parse_RewardModifierList);
			// If this character doesn't have reward mods, skip iterating over the list
			for(i=eaSize(&pchar->ppRewardModifies)-1; i>=0; --i)
			{
				AttribMod *pmod = pchar->ppRewardModifies[i];
				AttribModDef *pmodDef = pmod->pDef;
				if(pmodDef->offAttrib == kAttribType_RewardModifier) //Paranoia
				{
					RewardModifierParams *pRewardParams = (RewardModifierParams*)pmodDef->pParams;
					const char *pchNumeric = NULL; //Pooled string
					NOCONST(RewardModifier) *pModifier = NULL;
					if(!pRewardParams)
						continue;

					pchNumeric = REF_STRING_FROM_HANDLE(pRewardParams->hNumeric);

					pModifier = CONTAINER_NOCONST(RewardModifier, eaIndexedGetUsingString(&pRewardModList->ppRewardMods, pchNumeric));
					if(!pModifier)
					{
						pModifier = StructCreateNoConst(parse_RewardModifier);

						pModifier->pchNumeric = allocAddString(pchNumeric);
						//Start at 1.0, and increase/decrease below
						pModifier->fFactor = 1.0f;

						eaIndexedEnable(&pRewardModList->ppRewardMods, parse_RewardModifier);
						eaPush(&pRewardModList->ppRewardMods, (RewardModifier*)pModifier);
					}

					//Add the factor to the modifier
					pModifier->fFactor += pmod->fMagnitude;
				}
			}
#ifdef GAMESERVER
			rewards_RegenRewardModList(pchar->pEntParent, pRewardModList, !pppRewardModifies);
#endif

			StructDestroy(parse_RewardModifierList, pRewardModList);
		}

		PERFINFO_AUTO_STOP();
	}
	if(pppRewardModifies)
		eaClearFast(pppRewardModifies);
}

// An entity that this character created was destroyed, so handle cleanup of that
void character_CreatedEntityDestroyed(Character *pchar,
									  Entity *pentCreated)
{
	EntityRef erCreated = entGetRef(pentCreated);
	int i;
	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		if(erCreated == pchar->modArray.ppMods[i]->erCreated)
		{
			character_ModExpireReason(pchar, pchar->modArray.ppMods[i], kModExpirationReason_Unset);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			break;
		}
	}
}

// This performs part of the character_Cleanup() function.  I have no idea why it's
//  split specifically along the lines it is.
void character_CleanupPartial(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int i;
	character_DeactivatePassives(iPartitionIdx,pchar);
	character_DeactivateToggles(iPartitionIdx,pchar, pmTimestamp(0),false,false);
	
	eaClearFast(&pchar->modArray.ppModsSaved);
	character_RemoveAllMods(iPartitionIdx,pchar,false,false,kModExpirationReason_Unset,pExtract);

	// This shouldn't happen, but apparently it managed to happen on live
	//  It's not the end of the world, we'll just recover gracefully, but I
	//  need to know why it's happening, so it's a devassert
	if(entIsServer())
	{
		if (eaSize(&pchar->modArray.ppPowers))
		{
			if (isDevelopmentMode())
			{
				assertmsg(0, "character_CleanupPartial: Not all powers were removed from the mod array!");
			}
			else
			{
				ErrorfForceCallstack("character_CleanupPartial: Not all powers were removed from the mod array!");
			}
		}
	}
	eaDestroy(&pchar->modArray.ppPowers);

	// Clean up power lists/references
	eaDestroy(&pchar->ppPowers);
	eaDestroy(&pchar->ppPowersLimitedUse);
	eaDestroy(&pchar->ppPowersEntCreateEnhancements);
	eaDestroyEx(&pchar->ppPowersResetCache,NULL);
	eaDestroyEx(&pchar->ppPowerActPassive,poweract_Destroy);
	for (i = 0; i < eaSize(&pchar->ppPowerActToggle); i++)
	{
		PowerActivation *pAct = pchar->ppPowerActToggle[i];
		if (pchar->pPowActFinished == pAct)
		{
			// So it won't try to double free it below
			pchar->pPowActFinished = NULL;
		}
		poweract_Destroy(pAct);
	}	
	eaDestroy(&pchar->ppPowerActToggle);
	eaDestroyEx(&pchar->ppPowerActAutoAttackServer,poweract_Destroy);

	// Clean up power activations
	character_ActAllCancel(iPartitionIdx, pchar, true);
	poweract_DestroySafe(&pchar->pPowActOverflow);
	poweract_DestroySafe(&pchar->pPowActQueued);
	poweract_DestroySafe(&pchar->pPowActCurrent);
	poweract_DestroySafe(&pchar->pPowActFinished);
#endif
}

// Cleans up all temporarily allocated data on a character
void character_Cleanup(int iPartitionIdx, Character *pchar, bool isReloading, GameAccountDataExtract *pExtract, bool bDoCleanup)
{
	PERFINFO_AUTO_START_FUNC();

	if(!isReloading || bDoCleanup)
	{
		character_CleanupPartial(iPartitionIdx,pchar, pExtract);
	}

	// Clean up stances
	if(IS_HANDLE_ACTIVE(pchar->hPowerEmitStance))
		REMOVE_HANDLE(pchar->hPowerEmitStance);
	eaDestroyEx(&pchar->ppPowerRefRecharge, powerref_Destroy);
	eaDestroyEx(&pchar->ppPowerRefChargeRefill, powerref_Destroy);
	powerref_DestroySafe(&pchar->pPowerRefStance);
	powerref_DestroySafe(&pchar->pPowerRefPersistStance);

	// Clean up attribs
	StructDestroy(parse_CharacterAttribs, pchar->pattrBasic);
	pchar->pattrBasic = NULL;

	// Clean up attrib accrual sets
	character_DirtyInnateEquip(pchar);
	character_DirtyInnatePowers(pchar);
	character_DirtyPowerStats(pchar);
	character_DirtyInnateAccrual(pchar);

	// Clean up PowerApplyStrengths
	eaDestroyStruct(&pchar->ppApplyStrengths, parse_PowerApplyStrength);

	damageTracker_ClearAll(pchar);
		
	CombatAdvantage_CleanupCharacter(pchar);

	#ifdef GAMESERVER
		gslPVPCleanup(pchar->pEntParent);
	#endif

	#if GAMESERVER || GAMECLIENT
		mrDestroy(&pchar->pPowersMovement);
	#endif

	pchar->pEntParent = NULL;
	pchar->bLoaded = false;

	PERFINFO_AUTO_STOP();
}

// Performs various sanity checks on a Character
void character_SanityCheck(Character *pchar)
{
	int i;
	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		devassertmsg(IS_HANDLE_ACTIVE(pmod->hPowerDef),"Character has AttribMod with no PowerDef handle");
	}
}

static void character_HandleSpecialModsOnDeath(Character *pchar)
{
	int i, s = eaSize(&pchar->modArray.ppMods);
	for(i=s-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		AttribType eAttrib = pmod->pDef->offAttrib;

		if (eAttrib == kAttribType_ModifyCostume)
		{
			// Update costume modifies on death
			eaPush(&pchar->ppCostumeModifies,pmod);
		}
		else if (eAttrib == kAttribType_SetCostume)
		{
			// Update costume changes on death
			eaPush(&pchar->ppCostumeChanges,pmod);
		}
	}
}

static void character_UpdateModsForDeath(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	// These static variables are gross.  I wish would could just put the earray memory on the stack.  Perhaps we can?  [RMARR - 9/27/11]
	static AttribMod **ppOldCostumeChanges = NULL;
	static AttribMod **ppOldCostumeModifies = NULL;
	static AttribMod **ppOldRewardModifies = NULL;

	//Cache the costumes and reward mods
	character_CacheAttribMods(pchar, &ppOldCostumeChanges, &ppOldCostumeModifies, &ppOldRewardModifies);

	// Clear attrib mods
	character_RemoveAllMods(iPartitionIdx,pchar,true,false,kModExpirationReason_CharacterDeath,pExtract);

	// Update attrib state of character
	character_AccrueMods(iPartitionIdx,pchar,0.0f,pExtract);

	// Special handling of special attribs on death
	character_HandleSpecialModsOnDeath(pchar);

	//Change costumes if we changed it in death
	character_RegenCostumeIfChanged(pchar, &ppOldCostumeChanges, &ppOldCostumeModifies);
	
	character_RegenRewardsIfChanged(pchar, &ppOldRewardModifies);
}

// assumes the character is already going to die for whatever reason.
// returns true if the character meets the conditions to enter the nearDeath state
bool character_MeetsNearDeathRequirements(Character *pChar, NearDeathConfig *pNearDeathConfig)
{
	if (pNearDeathConfig->ePowerModeRequired && ea32Find(&pChar->piPowerModes, pNearDeathConfig->ePowerModeRequired) == -1)
	{
		return false;
	}

	if (pNearDeathConfig->eDyingTimeAttrib >= 0 && IS_NORMAL_ATTRIB(pNearDeathConfig->eDyingTimeAttrib))
	{
		F32 *pfCur = F32PTR_OF_ATTRIB(pChar->pattrBasic, pNearDeathConfig->eDyingTimeAttrib);
		if (*pfCur <= 0)
			return false;
	}

	return pNearDeathConfig->fChance >= 1.f || randomPositiveF32() < pNearDeathConfig->fChance;
}

F32 character_NearDeathGetMaxDyingTime(Character *pChar, NearDeathConfig *pNearDeathConfig)
{
	if (pNearDeathConfig->eDyingTimeAttrib >= 0 && IS_NORMAL_ATTRIB(pNearDeathConfig->eDyingTimeAttrib))
	{
		F32 *pfCur = F32PTR_OF_ATTRIB(pChar->pattrBasic, pNearDeathConfig->eDyingTimeAttrib);
		return *pfCur;
	}

	return pNearDeathConfig->fTime;
}

// Find the Entity that owns the damage that is considered the killing blow on a dead Character.
//  Optionally returns the entire DamageTracker.
Entity *character_FindKiller(int iPartitionIdx, Character *pchar, DamageTracker **ppDamageTrackerOut)
{
	int i;
	Entity *pentKiller = NULL;
	F32 fKillingBlow=0;

	if(!pchar)
	{
		return NULL;
	}

    // if we are in near death and we have a killer saved, use that. 
    // ppDamageTrackerOut won't get set as it is now lost, but for the current uses it doesn't matter
	if (pchar->pNearDeath && pchar->pNearDeath->erKiller)
	{
		return entFromEntityRef(iPartitionIdx, pchar->pNearDeath->erKiller);
	}

	for(i=eaSize(&pchar->ppDamageTrackersTickIncoming)-1; i>=0; i--)
	{
		DamageTracker *pTracker = pchar->ppDamageTrackersTickIncoming[i];
		if(pTracker->fDamage > fKillingBlow)
		{
			Entity *eOwner = entFromEntityRef(iPartitionIdx, pTracker->erOwner);
			if(eOwner && eOwner!=pchar->pEntParent)
			{
				fKillingBlow = pTracker->fDamage;
				pentKiller = eOwner;
				if(ppDamageTrackerOut) (*ppDamageTrackerOut) = pTracker;
			}
		}
	}

	return pentKiller;
}


// sets the player's respawn time if there's a reason to
static void player_SetRespawnTime(Entity *pPlayerEnt)
{
#if GAMESERVER
	U32 uiWaveRespawnTime = zmapInfoGetRespawnWaveTime(NULL);
	U32 uiRespawnMin, uiRespawnMax, uiRespawnIncrement, uiAttritionTime;
	bool bHasRespawnData = zmapInfoGetRespawnTimes(NULL, &uiRespawnMin, &uiRespawnMax, &uiRespawnIncrement, &uiAttritionTime);

	if (bHasRespawnData)
	{
		U32 uiCurrentTime = timeSecondsSince2000();
		U32 uiRespawnCount = mapState_GetPlayerSpawnCount(pPlayerEnt);
		U32 uiRespawnTime;

		if (uiAttritionTime)
		{
			U32 uiLastRespawnTime = mapState_GetPlayerLastRespawnTime(pPlayerEnt);
			if (uiLastRespawnTime && uiCurrentTime - uiLastRespawnTime >= uiAttritionTime)
				uiRespawnIncrement = 0;
		}

		uiRespawnTime = uiRespawnMin + uiRespawnCount * uiRespawnIncrement;
		if (uiRespawnMax)
			MIN1(uiRespawnTime, uiRespawnMax);

		pPlayerEnt->pPlayer->uiRespawnTime = uiCurrentTime + uiRespawnTime;

		if (uiRespawnIncrement)
			mapState_SetPlayerSpawnCount(pPlayerEnt, uiRespawnCount+1);
		else if (uiRespawnCount)
			mapState_SetPlayerSpawnCount(pPlayerEnt, 1);

		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
	else if (uiWaveRespawnTime) // Zone map wave respawn trumps region rules respawn
	{
		U32 uiWaveTime = uiWaveRespawnTime - (timeSecondsSince2000() % uiWaveRespawnTime);

		pPlayerEnt->pPlayer->uiRespawnTime = timeSecondsSince2000() + uiWaveTime;

		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
	else
	{
		RegionRules *pRegionRules = getRegionRulesFromEnt(pPlayerEnt);
		if(pRegionRules)
		{		
			pPlayerEnt->pPlayer->uiRespawnTime = timeSecondsSince2000() + pRegionRules->uiRespawnTime;

			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
		}
	}
#endif
}

// helper function for character_TriggerCombatEventKill
static void character_TriggerCombatEventKill(	S32 iPartitionIdx, 
												GameAccountDataExtract * pExtract,
												Character *pchar,
												Entity *pentKiller,
												PowerDef *pPowerDefKill,
												AttribModDef *pAttribModDefKill, 
												F32 fDamage,
												F32 fDamageNoResist, 
												bool bTriggerNearDeathDead)
{
	if(bTriggerNearDeathDead && character_CombatEventTrack(pchar,kCombatEvent_NearDeathDead))
	{
		character_CombatEventTrackComplex(pchar, kCombatEvent_NearDeathDead, pentKiller, pPowerDefKill, 
											pAttribModDefKill, fDamage, fDamageNoResist, NULL);
	}

	if(character_CombatEventTrack(pchar,kCombatEvent_KillIn))
		character_CombatEventTrackComplex(pchar, kCombatEvent_KillIn, pentKiller, pPowerDefKill, pAttribModDefKill, fDamage, fDamageNoResist, NULL);

	if(character_CombatEventTrack(pentKiller->pChar,kCombatEvent_KillOut))
		character_CombatEventTrackComplex(pentKiller->pChar,kCombatEvent_KillOut,pchar->pEntParent,pPowerDefKill,pAttribModDefKill,fDamage,fDamageNoResist, NULL);

	//for(i=eaSize(&pentKiller->pChar->modArray.ppMods)-1; i>=0; i--)
	FOR_EACH_IN_EARRAY(pentKiller->pChar->modArray.ppMods, AttribMod, pmod)
	{
		//AttribMod *pmod = pentKiller->pChar->modArray.ppMods[i];
		if(pmod->pDef->offAttrib==kAttribType_KillTrigger)
		{
			PowerDef *pdef = NULL;
			KillTriggerParams *pParams = (KillTriggerParams*)pmod->pDef->pParams;
			S32 bFail = false;
			Character *pcharTargetType = mod_GetApplyTargetTypeCharacter(iPartitionIdx,pmod,&bFail);

			if(!bFail
				&& pParams
				&& (pdef=GET_REF(pParams->hDef))
				&& pmod->pSourceDetails
				&& (pParams->fChance==1 || randomPositiveF32() < pParams->fChance)
				&& (!pParams->bMagnitudeIsCharges || pmod->fMagnitude >= 1))
			{
				EntityRef erTarget = pParams->eTarget== kKillTriggerEntity_Self ? entGetRef(pentKiller) : entGetRef(pchar->pEntParent);
				ApplyUnownedPowerDefParams applyParams = {0};
				static Power **s_eaPowEnhancements = NULL;

				applyParams.pmod = pmod;
				applyParams.erTarget = erTarget;
				applyParams.pcharSourceTargetType = pcharTargetType;
				applyParams.pclass = GET_REF(pmod->pSourceDetails->hClass);
				applyParams.iLevel = pmod->pSourceDetails->iLevel;
				applyParams.iIdxMulti = pmod->pSourceDetails->iIdxMulti;
				applyParams.fTableScale = pmod->pSourceDetails->fTableScale;
				applyParams.iSrcItemID = pmod->pSourceDetails->iItemID;
				applyParams.bLevelAdjusting = pmod->pSourceDetails->bLevelAdjusting;
				applyParams.ppStrengths = pmod->ppApplyStrengths;
				applyParams.pCritical = pmod->pSourceDetails->pCritical;
				applyParams.erModOwner = pmod->erOwner;
				applyParams.uiApplyID = pmod->uiApplyID;
				applyParams.fHue = pmod->fHue;
				applyParams.pExtract = pExtract;

				if(pmod->erOwner)
				{
					Entity *pModOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
					if (pModOwner && pModOwner->pChar)
					{
						power_GetEnhancementsForAttribModApplyPower(iPartitionIdx, pModOwner->pChar, 
							pmod, EEnhancedAttribList_DEFAULT, pdef, 
							&s_eaPowEnhancements);
						applyParams.pppowEnhancements = s_eaPowEnhancements;
					}
				}

				character_ApplyUnownedPowerDef(iPartitionIdx, pentKiller->pChar, pdef, &applyParams);
				eaClear(&s_eaPowEnhancements);

				// Decrement charges, and expire if necessary
				if(pParams->bMagnitudeIsCharges)
				{
					entity_SetDirtyBit(pentKiller, parse_Character, pentKiller->pChar, false);
					pmod->fMagnitude -= 1;
					if(pmod->fMagnitude < 1)
					{
						character_ModExpireReason(pentKiller->pChar, pmod,kModExpirationReason_Charges);
					}
				}
			}
		}
	}
	FOR_EACH_END
}

// internal function when the character's death is being processed, finds the character's killer
// for NearDeath -> Death, this should only be called once
static void character_FindKillerAndTriggerCombatKillEvents(	S32 iPartitionIdx, 
															GameAccountDataExtract * pExtract,
															Character *pchar, 
															bool bTriggerNearDeathDead)
{
	DamageTracker* pDamageTrackerKill = NULL;
	Entity *pentKiller = NULL;
	Entity *pentKillSource = NULL;
	PowerDef *pPowerDefKill = NULL;
	AttribModDef *pAttribModDefKill = NULL;
	F32 fDamage = 0.f;
	F32 fDamageNoResist = 0.f;
	bool bTriggerCombatEventKill = false;
	
	pentKiller = character_FindKiller(iPartitionIdx, pchar, &pDamageTrackerKill);
	if(pDamageTrackerKill)
	{
		pentKillSource = entFromEntityRef(iPartitionIdx, pDamageTrackerKill->erSource);
		pPowerDefKill = GET_REF(pDamageTrackerKill->hPower);
		pAttribModDefKill = pPowerDefKill ? pPowerDefKill->ppOrderedMods[pDamageTrackerKill->uiDefIdx] : NULL;
		fDamage = pDamageTrackerKill->fDamage;
		fDamageNoResist = pDamageTrackerKill->fDamageNoResist;
	}

	// if we were told we should process NearDeathDead
	// see if the character's class has a near death config, if not don't trigger the event
	if(bTriggerNearDeathDead)
	{
		CharacterClass *pClass = character_GetClassCurrent(pchar);
		if(!pClass || !pClass->pNearDeathConfig)
			bTriggerNearDeathDead = false;
	}

	if(pentKiller && pentKiller->pChar)
	{
		character_TriggerCombatEventKill(	iPartitionIdx, pExtract, pchar, pentKiller, 
											pPowerDefKill, pAttribModDefKill, fDamage, fDamageNoResist, bTriggerNearDeathDead);
		bTriggerCombatEventKill = true;
	}

	//If we have a kill source and the killer is not the kill source, then do kill triggers on the source
	if(pentKillSource 
		&& pentKillSource->pChar
		&& pentKillSource != pentKiller )
	{
		character_TriggerCombatEventKill(	iPartitionIdx, pExtract, pchar, pentKiller, 
											pPowerDefKill, pAttribModDefKill, fDamage, fDamageNoResist, bTriggerNearDeathDead);
		bTriggerCombatEventKill = true;
	}

	if (!bTriggerCombatEventKill && bTriggerNearDeathDead)
	{	// we needed to send a kCombatEvent_NearDeathDead combat event, 
		// but we were killed but didn't have a killer for some reason (kill volume or something)
		if(character_CombatEventTrack(pchar,kCombatEvent_NearDeathDead))
		{
			character_CombatEventTrackComplex(pchar, kCombatEvent_NearDeathDead, pentKiller, pPowerDefKill, 
												pAttribModDefKill, fDamage, fDamageNoResist, NULL);
		}
	}
}

// if the character is in nearDeath, 
// get the killing blow information from it and then trigger the combat event, kCombatEvent_NearDeathDead 
void character_TriggerCombatNearDeathDeadEvent(int iPartitionIdx, Character *pchar)
{
	if (pchar->pNearDeath)
	{
		if(character_CombatEventTrack(pchar, kCombatEvent_NearDeathDead))
		{
			Entity *pentKiller = entFromEntityRef(iPartitionIdx, pchar->pNearDeath->erKiller);
			PowerDef *pPowerDefKill = GET_REF(pchar->pNearDeath->hKillingBlowDef);
			AttribModDef *pAttribModDefKill = pPowerDefKill ? pPowerDefKill->ppOrderedMods[pchar->pNearDeath->iKillingAttribMod] : NULL;
						
			character_CombatEventTrackComplex(pchar, 
												kCombatEvent_NearDeathDead, 
												pentKiller, 
												pPowerDefKill, 
												pAttribModDefKill, 
												pchar->pNearDeath->fFatalDamageAmount, 
												pchar->pNearDeath->fFatalDamageAmountNoResist, NULL);
		}
	}
}

// Forces a Character into the NearDeath state, if they're alive and not already NearDeath.
// If the timer is negative, the NearDeath state is forever.  If the timer is 0 it uses the
//  default timer for the particular Character (if the Character doesn't normally go into
//  NearDeath, the default timer is forever).  Otherwise the NearDeath state will
//  use the specified timer.
// Returns the resulting timer value (-1 for forever), or 0 if something went wrong.
F32 character_NearDeathEnter(int iPartitionIdx, Character *pchar, F32 fTimer)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
	return 0;
#else
	if(entIsAlive(pchar->pEntParent) && !pchar->pNearDeath)
	{
		CharacterClass *pClass = character_GetClassCurrent(pchar);
		if(pClass && pClass->pNearDeathConfig)
		{
			GameAccountDataExtract * pExtract = entity_GetCachedGameAccountDataExtract(pchar->pEntParent);
			MovementRequester*	mrProjectile;
			DamageTracker* pDamageTrackerKill = NULL;
			Entity* pentKiller = NULL;
			
			U32 uiTimestamp = pmTimestamp(0);
			if(fTimer == 0.f)
			{
				fTimer = character_NearDeathGetMaxDyingTime(pchar, pClass->pNearDeathConfig);
			}
			if(fTimer <= 0)
				fTimer = POWERS_FOREVER;
			pchar->pattrBasic->fHitPoints = 0;
									
			
			pchar->pNearDeath = StructCreate(parse_NearDeath);
			pchar->pNearDeath->fTimer = fTimer;
				
			// save any information about the killing blow so we have it for later
			pentKiller = character_FindKiller(iPartitionIdx, pchar, &pDamageTrackerKill);
			if(pDamageTrackerKill)
			{
				pchar->pNearDeath->erKiller = pentKiller ? pentKiller->myRef : 0;
				pchar->pNearDeath->erKillerSource = pDamageTrackerKill->erSource;
				COPY_HANDLE(pchar->pNearDeath->hKillingBlowDef, pDamageTrackerKill->hPower);
				pchar->pNearDeath->iKillingAttribMod = pDamageTrackerKill->uiDefIdx;
				pchar->pNearDeath->fFatalDamageAmount = pDamageTrackerKill->fDamage;
				pchar->pNearDeath->fFatalDamageAmountNoResist = pDamageTrackerKill->fDamageNoResist;
			}
			
			entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);
			if (eaSize(&pClass->pNearDeathConfig->ppchBits))
				character_StickyBitsOn(pchar,1,0,kPowerAnimFXType_NearDeath,0,pClass->pNearDeathConfig->ppchBits,uiTimestamp);
			if (eaSize(&pClass->pNearDeathConfig->pchAnimStanceWords))
				character_StanceWordOn(pchar,1,0,kPowerAnimFXType_NearDeath,0,pClass->pNearDeathConfig->pchAnimStanceWords,uiTimestamp);
			
			pmIgnoreStart(pchar, pchar->pPowersMovement,PMOVE_NEARDEATH,kPowerAnimFXType_None,uiTimestamp,NULL);
			//no rolling during neardeath
			mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical, TACTICAL_NEARDEATH_UID, TDF_ALL, pmTimestamp(0));

			if (mmRequesterGetByNameFG(pchar->pEntParent->mm.movement, "ProjectileMovement", &mrProjectile)) 
			{
				mrProjectileSetNearDeath(mrProjectile);
			}
			character_UpdateModsForDeath(iPartitionIdx,pchar,pExtract);

			// Cancel activations
			character_ActAllCancel(iPartitionIdx, pchar, true);

#ifdef GAMESERVER
			if (!entIsPlayer(pchar->pEntParent))
			{
				aiCleanupStatus(pchar->pEntParent, pchar->pEntParent->aibase); 
			}

			// Stop interaction
			interaction_EndInteractionAndDialog(iPartitionIdx, pchar->pEntParent, false, true, true);

			eventsend_RecordNearDeath(pchar->pEntParent);

			gslQueue_HandlePlayerNearDeathEnter(pchar->pEntParent, pentKiller);

			// if there is a wave respawn time for this map, set it now
			if (pchar->pEntParent->pPlayer)
				player_SetRespawnTime(pchar->pEntParent);

			character_FindKillerAndTriggerCombatKillEvents(iPartitionIdx, pExtract, pchar, false);
#endif
			


			return (fTimer == POWERS_FOREVER ? -1 : fTimer);
		}
		else
		{
			Errorf("character class has no nearDeath config\n");
		}
	}

	return 0;
#endif
}

void DEFAULT_LATELINK_character_ProjSpecificDeathLogString(Entity *pEnt, bool beforePenalty, char **outString)
{
	return;
}

//Character's time ran out or they respawned while dying.
void character_NearDeathExpire(Character *pchar, GameAccountDataExtract *pExtract)
{

#if GAMESERVER || GAMECLIENT 

	PERFINFO_AUTO_START("Death",1);

	if(pchar->bUseDeathOverrides)
		entDie(pchar->pEntParent, pchar->fTimeToLingerOverride, pchar->bGiveRewardsOverride, pchar->bGiveEventCreditOverride,pExtract);
	else
		entDie(pchar->pEntParent, -1, true, true, pExtract);

	if(pchar->pNearDeath)
	{
		U32 uiTimestamp = pmTimestamp(0);
		CharacterClass *pClass = character_GetClassCurrent(pchar);
		pmIgnoreStop(pchar, pchar->pPowersMovement,PMOVE_NEARDEATH,kPowerAnimFXType_None,uiTimestamp);
		mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical, TACTICAL_NEARDEATH_UID, pmTimestamp(0) );

		if(eaSize(&pClass->pNearDeathConfig->ppchBits))
			character_StickyBitsOff(pchar,1,0,kPowerAnimFXType_NearDeath,0,pClass->pNearDeathConfig->ppchBits,uiTimestamp);
		if (eaSize(&pClass->pNearDeathConfig->pchAnimStanceWords))
			character_StanceWordOff(pchar,1,0,kPowerAnimFXType_NearDeath,0,pClass->pNearDeathConfig->pchAnimStanceWords,uiTimestamp);
		StructDestroySafe(parse_NearDeath,&pchar->pNearDeath);
		entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);
	}

	PERFINFO_AUTO_STOP();

#endif

}

// Removes the NearDeath state
void character_NearDeathRevive(Character *pchar)
{
#if GAMESERVER || GAMECLIENT 
	if (pchar->pNearDeath)
	{
		U32 uiTimestamp = pmTimestamp(0);
		CharacterClass *pClass = character_GetClassCurrent(pchar);

		pmIgnoreStop(pchar, pchar->pPowersMovement,PMOVE_NEARDEATH,kPowerAnimFXType_None,uiTimestamp);
		mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical, TACTICAL_NEARDEATH_UID, pmTimestamp(0) );

		if(eaSize(&pClass->pNearDeathConfig->ppchBits))
			character_StickyBitsOff(pchar,1,0,kPowerAnimFXType_NearDeath,0,pClass->pNearDeathConfig->ppchBits,uiTimestamp);
		if (eaSize(&pClass->pNearDeathConfig->pchAnimStanceWords))
			character_StanceWordOff(pchar,1,0,kPowerAnimFXType_NearDeath,0,pClass->pNearDeathConfig->pchAnimStanceWords,uiTimestamp);
		StructDestroySafe(parse_NearDeath,&pchar->pNearDeath);
		entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);

		// Update ItemArt just to be safe
		entity_UpdateItemArtAnimFX(pchar->pEntParent);

#if GAMESERVER
		// we are coming back to life, reset the respawn time on the server
		if (pchar->pEntParent->pPlayer)
		{
			pchar->pEntParent->pPlayer->uiRespawnTime = 0;
			entity_SetDirtyBit(pchar->pEntParent, parse_Player, pchar->pEntParent->pPlayer, false);
		}
#endif

	}
#endif
}

// Character part of entity dying
void character_Die(int iPartitionIdx, Character *pchar, F32 fTimeToLinger, int bGiveRewards, int bGiveKillCredit, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int i;
	Entity *e = pchar->pEntParent;
	U32 uiAnimTime = pmTimestamp(0);
	Entity *pentKiller = NULL;
	char *beforeDPLogString = NULL;
	char *afterDPLogString = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Animate death
	entity_DeathAnimationUpdate(e, true, uiAnimTime);

	for(i=eaSize(&pchar->ppDamageTrackersTickIncoming)-1; i>=0; i--)
	{
		DamageTracker *pTracker = pchar->ppDamageTrackersTickIncoming[i];
		PowerDef *ppow = GET_REF(pTracker->hPower);
		PowerAnimFX *pafx = ppow ? GET_REF(ppow->hFX) : 0;
		if(pafx)
		{
			Entity *eSource = entFromEntityRef(iPartitionIdx, pTracker->erSource);
			character_AnimFXDeath(iPartitionIdx, pchar, eSource ? eSource->pChar : NULL, pafx, uiAnimTime);
		}
	}
		
#ifdef GAMESERVER
	// Cancel riding
	if (entGetMount(e))
	{
		gslEntCancelRide(e);
	}
	else if (entGetRider(e))
	{
		gslEntCancelRide(entGetRider(e));
	}

	// credit, rewards
	{
		KillCreditTeam **eaCreditTeams = NULL;
		F32 fTotalDamage = reward_CalculateKillCredit(e, &eaCreditTeams);
		Critter *pCritter = e->pCritter;
		
		if(pCritter)
		{
			CritterDef *pCritterDef = GET_REF(pCritter->critterDef);

			// Reward only if rewards are loaded, we want to give them, and the bKilled flag hasn't been set
			if(bGiveRewards && !pCritter->bKilled && !pCritter->bDeathRewardsGiven)
			{
				reward_EntKill(e, &eaCreditTeams, fTotalDamage);
			}

			if(pCritter->bKilled)
			{
				// don't send kill credit event again
				bGiveKillCredit = false;
			}

			pCritter->bKilled = true;

			// If the loots are kept on the corpse and there is a loot
			if (gConf.bKeepLootsOnCorpses && 
				pCritter->bIsInteractable && 
				eaSize(&pCritter->eaLootBags) > 0)
			{
				pCritter->timeToLinger = 0;
				FOR_EACH_IN_EARRAY(pCritter->eaLootBags, InventoryBag, pBag)
				{
					pCritter->timeToLinger = max(pCritter->timeToLinger, pBag->pRewardBagInfo->LingerTime);
				}
				FOR_EACH_END
			}
			else
			{
				if(fTimeToLinger >= 0)
					pCritter->timeToLinger = fTimeToLinger;
				else if(pCritterDef)
					pCritter->timeToLinger = pCritterDef->lingerDuration;
			}		
		}
		else if(e->pPlayer)
		{
			if(bGiveRewards)
			{
				reward_PlayerKill(e, &eaCreditTeams, fTotalDamage);
			}
		}

		// Send Events
		if(bGiveKillCredit)
		{
			eventsend_RecordDeath(&eaCreditTeams, e);
			gslPVPGame_KillCredit(&eaCreditTeams, e);
		}
		else
		{
			eventsend_RecordDeath(NULL, e);
		}

		eaDestroyStruct(&eaCreditTeams, parse_KillCreditTeam);
	}

	// End duels
	gslPVPCleanup(e);
#endif


	if(e->pPlayer)
	{
#ifdef GAMESERVER
		PlayerDifficulty *pDifficulty = pd_GetDifficulty(mapState_GetDifficulty(mapState_FromPartitionIdx(iPartitionIdx)));
		const char *difficultyName = pDifficulty ? NULL_TO_EMPTY(pDifficulty->pchInternalName) : "";
		int teamSize = 1;
		Team *pTeam = team_GetTeam(e);

		// if the player respawn time is not yet set. 
		// uiRespawnTime should be reset once the player respawns via gslPlayerRespawn()
		if (e->pPlayer->uiRespawnTime == 0)
		{
			player_SetRespawnTime(e);
		}

		if ( pTeam != NULL )
		{
			teamSize = team_NumPresentMembers(pTeam);
		}

		// Reset the stuck respawn flag
		e->pPlayer->bStuckRespawn = false;

		// Stop interaction
		interaction_EndInteractionAndDialog(iPartitionIdx, e, false, true, true);

		if(damageTracker_HasBeenDamagedInPvP(iPartitionIdx,pchar))
		{
			// pvp death
			if (gbEnablePvPLogging)
			{
				entLog(LOG_DEATH, e, "PvPPlayerDeath", "map=%s,difficulty=%s,teamSize=%d", zmapInfoGetPublicName(NULL), difficultyName, teamSize);
			}
		}
		else
		{
			// pve death
			// get game specific log string before death penalty
			character_ProjSpecificDeathLogString(e, true, &beforeDPLogString);

			// execute the death penalty
			reward_DeathPenaltyExec(e);

			// get game specific log string after death penalty
			character_ProjSpecificDeathLogString(e, false, &afterDPLogString);

			if (gbEnableCombatDeathEventLogging)
			{
				entLog(LOG_DEATH, e, "PvEPlayerDeath", "map=%s,difficulty=%s,teamSize=%d,before=%s,after=%s", zmapInfoGetPublicName(NULL), 
					difficultyName, teamSize, NULL_TO_EMPTY(beforeDPLogString), NULL_TO_EMPTY(afterDPLogString));
			}
			estrDestroy(&beforeDPLogString);
			estrDestroy(&afterDPLogString);
		}

		// kill the player's pet's entity.
		if(scp_GetSummonedPetEntRef(e))
		{
			EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(e);
			gslQueueEntityDestroy(entFromEntityRef(entGetPartitionIdx(e), pSCPData->erSCP));
			pSCPData->erSCP = 0;
		}

#else
		// Stop interaction
		interaction_ClearPlayerInteractState(e);
#endif
	} 
	else if(entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET)
	{
#ifdef GAMESERVER
		PlayerDifficulty *pDifficulty = pd_GetDifficulty(mapState_GetDifficulty(mapState_FromPartitionIdx(iPartitionIdx)));
		const char *difficultyName = pDifficulty ? NULL_TO_EMPTY(pDifficulty->pchInternalName) : "";

		if(damageTracker_HasBeenDamagedInPvP(iPartitionIdx,pchar))
		{
			if (gbEnablePvPLogging) {
				// pvp pet death
				entLog(LOG_DEATH, e, "PvPPetDeath", "ownerID=%d,map=%s,difficulty=%s", e->pSaved ? e->pSaved->conOwner.containerID : 0, zmapInfoGetPublicName(NULL), difficultyName);
			}
		}
		else
		{
			// get game specific log string before death penalty
			character_ProjSpecificDeathLogString(e, true, &beforeDPLogString);

			// execute the death penalty
			reward_DeathPenaltyExec(e);

			// get game specific log string after death penalty
			character_ProjSpecificDeathLogString(e, false, &afterDPLogString);

			if (gbEnableCombatDeathEventLogging) {
				entLog(LOG_DEATH, e, "PvEPetDeath", "ownerID=%d,map=%s,difficulty=%s,before=%s,after=%s", e->pSaved ? e->pSaved->conOwner.containerID : 0, zmapInfoGetPublicName(NULL), 
					difficultyName, NULL_TO_EMPTY(beforeDPLogString), NULL_TO_EMPTY(afterDPLogString));
			}

			estrDestroy(&beforeDPLogString);
			estrDestroy(&afterDPLogString);
		}
#endif
	}
	//Super Critter Pets don't ever die, just enter a near-death state until healed or combat ends.
	//but the design might change, so leaving this commented out. -SIP 3JAN13
	else if(entCheckFlag(e, ENTITYFLAG_CRITTERPET) && e->erCreator)
	{
#ifdef GAMESERVER
		Entity *pOwner = entFromEntityRef(iPartitionIdx, e->erCreator);
		if (pOwner)
		{
			scp_PetDiedForceDismissCurrentPet(pOwner);
		}
#endif
	}

	// Cancel activations
	character_ActAllCancel(iPartitionIdx,pchar,true);

	// Drop held objects
	character_DropHeldObjectOnTarget(iPartitionIdx,pchar,0,pExtract);
	
	character_UpdateModsForDeath(iPartitionIdx,pchar,pExtract);

	// Make them dead with respect to the combat system
	pchar->pattrBasic->fHitPoints = 0.0f;
	if (!g_CombatConfig.bPowerAttribSurvivesCharDeath)
		pchar->pattrBasic->fPower = 0.0f;
#ifdef GAMESERVER
	character_DirtyAttribs(pchar);
#endif
	character_AttribPoolsEmpty(pchar);

	// Shut off toggles
	character_DeactivateToggles(iPartitionIdx,pchar,uiAnimTime,true,true);

	// Refresh passives
	character_RefreshPassives(iPartitionIdx,pchar, pExtract);
	
	pentKiller = character_FindKiller(iPartitionIdx, pchar, NULL);

	// if we were in nearDeath then we already processed the kill event
	if (!pchar->pNearDeath)
	{
		character_FindKillerAndTriggerCombatKillEvents(iPartitionIdx, pExtract, pchar, true);
	}
	else
	{
		character_TriggerCombatNearDeathDeadEvent(iPartitionIdx, pchar);
	}
	
	character_Wake(pchar);

#ifdef GAMESERVER
	gslQueue_HandlePlayerKilled(pchar->pEntParent,pentKiller);

	if (entIsPlayer(e))
	{
		ClearAllPowersOverrideFSM(e);
	}
#endif

	PERFINFO_AUTO_STOP();
#endif
}

// Cleans up the Character's personal Powers list
// Called from character_LoadTransact(), which is now an AUTO_TRANS_HELPER
AUTO_TRANS_HELPER;
void character_FixPowersPersonalHelper(ATH_ARG NOCONST(Character) *pchar)
{
	int i;
	for(i=eaSize(&pchar->ppPowersPersonal)-1; i>=0; i--)
	{
		S32 bValid = true;
		Power *ppow = (Power *)pchar->ppPowersPersonal[i];
		if(!ppow)
		{
			Errorf("Character had NULL personal Power\n");
			bValid = false;
		}
		else
		{
			PowerDef *pdef = GET_REF(ppow->hDef);
			if(!pdef)
			{
				if(IS_HANDLE_ACTIVE(ppow->hDef))
				{
					// TODO(JW): Powers: Actually, this could be OK if we just avoid
					//  adding it to the general list.
					Errorf("Character had personal Power with active but invalid PowerDef %s\n",REF_STRING_FROM_HANDLE(ppow->hDef));
				}
				else
				{
					Errorf("Character had personal Power with inactive PowerDef handle\n");
				}
				bValid = false;
			}
		}

		if(!bValid)
		{
			StructDestroy(parse_Power, ppow);
			eaRemove(&pchar->ppPowersPersonal,i);
		}
	}
}

// Adds an existing Power to the Character's general list, calls
//  the relevant functions to handle the side-effects.  Returns
//  false if something went wrong.
int character_AddPower(int iPartitionIdx,
					   Character *pchar,
					   Power *ppow,
					   PowerSource eSource,
					   GameAccountDataExtract *pExtract)
{
	int bValid = true;
	PowerDef *pdef;

	// Server has the additional requirement that the Power
	//  must have a valid PowerDef
	if(entIsServer())
	{
		if(!GET_REF(ppow->hDef))
		{
			PowersError("character_AddPower: Bad PowerDef");
			bValid = false;
		}
	}

	pdef = GET_REF(ppow->hDef);

	//Do not add any powers that have propagation active, unless they are from the propagation list
	//non-persisted critters do not care about propagation
	//HACK: allow the player to add it if they are a valid target for the power propagation rules
	if(pdef && pdef->powerProp.bPropPower && eSource != kPowerSource_Propagation && pchar->pEntParent->myEntityType != GLOBALTYPE_ENTITYCRITTER
		&& !ent_canTakePower(pchar->pEntParent,pdef))
	{
		return false;
	}

	ppow->eSource = eSource;

	if(bValid && ppow->uiID)
	{
		int i;

		ppow->bNeedsResetCache = true;

		// Create or fix the sub-Powers.  In theory we could skip this if we fail
		//  the following check that this Power isn't a dupe, but since the mail system
		//  managed to introduce dupes, we're going to do it now to be safe, so that
		//  other safety asserts don't get hit later.
		if(entIsServer())
		{
			power_CreateSubPowers(ppow);
			CombatPowerStateSwitching_CreateModePowers(pchar, ppow);
		}
		else
		{
			power_FixSubPowers(ppow);
			CombatPowerStateSwitching_FixSubPowers(pchar, ppow);
		}

		// Check if it's already in the list, or another Power with the same ID is in the list
		for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
		{
			if(ppow!=pchar->ppPowers[i] && ppow->uiID!=pchar->ppPowers[i]->uiID)
			{
				continue;
			}

			if(ppow==pchar->ppPowers[i])
			{
				// Already in the list
				ErrorDetailsf("Character %s, Power %s, Source %s",CHARDEBUGNAME(pchar),REF_STRING_FROM_HANDLE(ppow->hDef),StaticDefineIntRevLookup(PowerSourceEnum,ppow->eSource));
				devassertmsg(0,"character_AddPower: Already in the list");
			}
			else
			{
				// Different Power with the same ID
				ErrorDetailsf("Character %s, ID %d, NewPower %s, NewSource %s, ExistingPower %s, ExistingSource %s",CHARDEBUGNAME(pchar),ppow->uiID,REF_STRING_FROM_HANDLE(ppow->hDef),StaticDefineIntRevLookup(PowerSourceEnum,ppow->eSource),REF_STRING_FROM_HANDLE(pchar->ppPowers[i]->hDef),StaticDefineIntRevLookup(PowerSourceEnum,pchar->ppPowers[i]->eSource));
				devassertmsg(0,"character_AddPower: Duplicate ID");
			}

			bValid = false;
			break;
		}

		if(bValid)
		{
			// Add it to the list (mark the array as indexed just in case)
			eaIndexedEnable(&pchar->ppPowers,parse_Power);
			eaPush(&pchar->ppPowers,ppow);

			PowersDebugPrintEnt(EPowerDebugFlags_POWERS, pchar->pEntParent, "added Power %d %p to the general list at %p\n",ppow->uiID, ppow, pchar->ppPowers);

			if(entIsServer())
			{
				// Attach all Enhancements (after it's in the list)
				power_AttachEnhancements(iPartitionIdx,pchar,ppow);

				// if this power is an enhancement and can possibly attach to entCreates
				if (pdef && pdef->eType == kPowerType_Enhancement && pdef->pExprEnhanceEntCreate)
				{
					eaPushUnique(&pchar->ppPowersEntCreateEnhancements, ppow);
				}
			}

			// Update calls based on type
			if(pdef)
			{
				if(pdef->eType==kPowerType_Innate)
				{
					character_DirtyInnatePowers(pchar);
				}
				else if(pdef->eType==kPowerType_Passive)
				{
					character_ActivatePassives(iPartitionIdx, pchar, pExtract);
				}
				else if(pdef->bAutoAttackServer)
				{
					pchar->bAutoAttackServerCheck = true;
				}
			}

			// If it was recharging, put it back into the recharge list
			if(power_GetRecharge(ppow) > 0)
			{
				power_SetRecharge(iPartitionIdx,pchar,ppow,power_GetRecharge(ppow));
			}
#ifdef GAMECLIENT
			// TODO(JW): Yes, recharge tracking sucks - see the reset cache processing
			//  in character_ResetPowersArray()
			else if(power_FindInRefs(ppow,&pchar->ppPowerRefRecharge) != -1)
				ServerCmd_power_requestinfo(ppow->uiID);
#endif
			
			// Cancel any replacement
			power_SetPowerReplacementID(ppow,0);
			power_ResetCachedEnhancementFields(ppow);
		}
	}
	else
	{
		PowersError("character_AddPower: 0 ID");
		bValid = false;
	}

	// Run the callback, even if we failed, just to be safe
	if(!s_bDisableChangedCallback && combatcbCharacterPowersChanged)
	{
		combatcbCharacterPowersChanged(pchar);
	}

	return bValid;
}

// Marks the Character's attribs as dirty, necessary for any out-of-tick modifications
void character_DirtyAttribs_dbg(Character *pchar MEM_DBG_PARMS)
{
	entity_SetDirtyBitInternal(pchar->pEntParent,parse_CharacterAttribs,pchar->pattrBasic,false MEM_DBG_PARMS_CALL);
	entity_SetDirtyBitInternal(pchar->pEntParent, parse_Character, pchar, false MEM_DBG_PARMS_CALL);
}


// Marks the system that owns the Power as dirty on the Character, so the Power's data is sent
void character_DirtyPower_dbg(Character *pchar,
							  Power *ppow MEM_DBG_PARMS)
{
	ppow = power_GetBasePower(ppow);

	switch(ppow->eSource)
	{
	case kPowerSource_PowerTree:
		character_DirtyPowerTrees_dbg(pchar MEM_DBG_PARMS_CALL);
		break;
	case kPowerSource_Item:
		character_DirtyItems_dbg(pchar MEM_DBG_PARMS_CALL);
		break;
		// The other sources are always sent anyway
	}
}

// Marks the Character's PowerTrees as dirty.  Used by Powers to make sure their data gets sent when they change.
void character_DirtyPowerTrees_dbg(Character *pchar MEM_DBG_PARMS)
{
	int i;
	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		entity_SetDirtyBitInternal(pchar->pEntParent,parse_PowerTree,pchar->ppPowerTrees[i],true MEM_DBG_PARMS_CALL);
		entity_SetDirtyBitInternal(pchar->pEntParent, parse_Character, pchar, false MEM_DBG_PARMS_CALL);
	}
}

// Marks the Character's Inventory as dirty.  Used by Powers to make sure their data gets sent when they change.
void character_DirtyItems_dbg(Character *pchar MEM_DBG_PARMS)
{
	if(pchar->pEntParent && pchar->pEntParent->pInventoryV2)
	{
		entity_SetDirtyBitInternal(pchar->pEntParent,parse_Inventory,pchar->pEntParent->pInventoryV2,true MEM_DBG_PARMS_CALL);
	}
}



// Returns a pointer to the AttribMod on the Character that created the Power, if one exists
AttribMod *character_GetPowerCreatorMod(Character *pchar,
										Power *ppow)
{
	int i,j;
	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		for(j=eaSize(&pchar->modArray.ppMods[i]->ppPowersCreated)-1; j>=0; j--)
		{
			if(pchar->modArray.ppMods[i]->ppPowersCreated[j]==ppow)
			{
				return pchar->modArray.ppMods[i];
			}
		}
	}
	return NULL;
}

// Finds the attrib mod that created the given EntityRef
static AttribMod* FindCreatedAttribMod(EntityRef erCreator, 
									   EntityRef erCreatedRef)
{
	Entity *pCreator = entFromEntityRefAnyPartition(erCreator);

	if(pCreator && pCreator->pChar)
	{
		int i;
		for(i=eaSize(&pCreator->pChar->modArray.ppMods)-1; i>=0; i--)
		{
			AttribMod *pMod = pCreator->pChar->modArray.ppMods[i];

			if (pMod->erCreated == erCreatedRef)
			{
				return pMod;
			}
		}
	}

	return NULL;
}

// Returns whether or not the given character was created by another entity and if it expires
bool character_DoesCharacterExpire(Character *pchar)
{
	if(pchar->pEntParent)
	{
		EntityRef myRef = entGetRef(pchar->pEntParent);
		
		if(pchar->pEntParent->erCreator)
		{
			AttribMod* pMod = FindCreatedAttribMod(pchar->pEntParent->erCreator, myRef);
			if (pMod) 
				return pMod->fDurationOriginal > 0.f;
		}

		if(pchar->pEntParent->erOwner && 
			(pchar->pEntParent->erCreator != pchar->pEntParent->erOwner))
		{
			AttribMod* pMod = FindCreatedAttribMod(pchar->pEntParent->erOwner, myRef);
			if (pMod) 
				return pMod->fDurationOriginal > 0.f;
		}
	}

	return false;
}


static void CharacterLevelCombatReduced(Character *pchar)
{
	// Just handles the special work we need to do when a Character's combat level is
	//  about to drop.  When this occurs, the Character loses any pets they have created
	//  or are about to create which live on themselves.  Anything AttribMods they own
	//  that are on another Character we're not going to touch.

	int i;
	EntityRef er = entGetRef(pchar->pEntParent);

	PERFINFO_AUTO_START_FUNC();
	
	// Expire actual mods
	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		if(pchar->modArray.ppMods[i]->erOwner==er)
		{
			AttribModDef *pdef = pchar->modArray.ppMods[i]->pDef;
			if(pdef && pdef->offAttrib==kAttribType_EntCreate)
			{
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				character_ModExpireReason(pchar, pchar->modArray.ppMods[i], kModExpirationReason_Unset);

			}
		}
	}

	// Destroy pending mods
	for(i=eaSize(&pchar->modArray.ppModsPending)-1; i>=0; i--)
	{
		if(pchar->modArray.ppModsPending[i]->erOwner==er)
		{
			AttribModDef *pdef = pchar->modArray.ppModsPending[i]->pDef;
			if(pdef && pdef->offAttrib==kAttribType_EntCreate)
			{
				mod_Destroy(pchar->modArray.ppModsPending[i]);
				eaRemoveFast(&pchar->modArray.ppModsPending,i);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void DEFAULT_LATELINK_character_ProjSpecificCombatLevelChange(Entity* pEnt, bool bLevelControlRemoved, char** pestrMsg)
{
#ifdef GAMESERVER
	entFormatGameMessageKey(pEnt, pestrMsg, "PowersMessage.Chat.CombatLevelChange",
		STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
#endif
}

#ifdef GAMESERVER
static int AddSidekickPowerCallback(Power *ppow, Character *pchar)
{
	int iSuccess = 0;
	int iDifference = abs(pchar->iLevelExp - pchar->iLevelCombat);

	if(!ppow)
		return 0;

	if(pchar->pLevelCombatControl)
	{
		pchar->pLevelCombatControl->uiSidekickingPowerID = ppow->uiID;
		iSuccess = 1;
	}
	else
	{
		iSuccess = 0;
	}

	((NOCONST(Power)*)ppow)->iLevel = iDifference;

	eaIndexedEnable(&pchar->ppPowersTemporary,parse_Power);
	eaPush(&pchar->ppPowersTemporary, ppow);

	pchar->bResetPowersArray = true;

	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	return iSuccess;
}

static void CharacterLevelCombatUpdatePower(Character *pchar)
{
	int iDifference = pchar->iLevelExp - pchar->iLevelCombat;

	//Remove the possible powers
	if(pchar->pLevelCombatControl)
		character_RemovePowerTemporary(pchar,pchar->pLevelCombatControl->uiSidekickingPowerID);

	if (iDifference < 0 && GET_REF(g_CombatConfig.hSidekickUpPower))
	{
		character_AddPowerTemporary(pchar,GET_REF(g_CombatConfig.hSidekickUpPower),AddSidekickPowerCallback,pchar);
	}
	else if(iDifference > 0 && GET_REF(g_CombatConfig.hSidekickDownPower))
	{
		character_AddPowerTemporary(pchar,GET_REF(g_CombatConfig.hSidekickDownPower),AddSidekickPowerCallback,pchar);
	}
}
#endif

static void CharacterLevelCombatChanged(int iPartitionIdx, Character *pchar, bool bLevelControlRemoved, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();

#ifdef GAMESERVER
	CharacterLevelCombatUpdatePower(pchar);
#endif

	// Dirty all the Character's innate data
	character_DirtyInnateEquip(pchar);
	character_DirtyInnatePowers(pchar);
	character_DirtyPowerStats(pchar);
	character_DirtyInnateAccrual(pchar);

	// Deactivate all Passives (ResetPowersArray will restart them)
	character_DeactivatePassives(iPartitionIdx, pchar);

	// Reset the Character's powers array
	character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
	
	// Ensure the Character performs full processing next tick
	character_Wake(pchar);

#ifdef GAMESERVER
	{
		char* estrMsg = NULL;
		estrStackCreate(&estrMsg);
		character_ProjSpecificCombatLevelChange(pchar->pEntParent, bLevelControlRemoved, &estrMsg);
		notify_NotifySend(pchar->pEntParent, kNotifyType_LevelUp, estrMsg, NULL, NULL);
		estrDestroy(&estrMsg);

		gslSavedPet_UpdateCombatLevel(pchar->pEntParent);
	}

	CharacterLevelCombatUpdatePower(pchar);
#endif

	PERFINFO_AUTO_STOP();
}

static const U32 uiMaxTimeInvalid = 60;
static void character_SideKickOutOfRangeMsg(SA_PARAM_NN_VALID Character *pchar, Entity *eLink)
{
	U32 uiNow = timeSecondsSince2000();
	U32 uiTimeRemaining = (pchar->pLevelCombatControl->uiTimestampInvalid) ?
		(pchar->pLevelCombatControl->uiTimestampInvalid + uiMaxTimeInvalid - uiNow) : 
	(uiMaxTimeInvalid);
	if(uiTimeRemaining != 0 && 
		uiTimeRemaining < 60 &&
		uiTimeRemaining % 10 == 0 &&
		pchar->pLevelCombatControl->uiTimestampInDanger != uiNow)
	{
#ifdef GAMESERVER
		char *pchMsg = NULL;
		estrStackCreate(&pchMsg);		

		if(eLink)
		{
			entFormatGameMessageKey(pchar->pEntParent, &pchMsg, "SideKicking.MaxRange",
				STRFMT_STRING("Target", entGetLocalName(eLink)),
				STRFMT_INT("TimeoutSeconds", uiTimeRemaining),
				STRFMT_END);
		}
		else
		{
			entFormatGameMessageKey(pchar->pEntParent, &pchMsg, "SideKicking.MaxRangeNoLink",
				STRFMT_INT("TimeoutSeconds", uiTimeRemaining),
				STRFMT_END);
		}
		notify_NotifySend(pchar->pEntParent, kNotifyType_SidekickingFailed, pchMsg, NULL, NULL);
		estrDestroy(&pchMsg);
#endif
		pchar->pLevelCombatControl->uiTimestampInDanger = uiNow;
	}

}

// Processes all the systems that can fiddle with combat level, and makes sure
//  the Character's iLevelCombat is current
void character_LevelCombatUpdate(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, bool bLevelControlRemoved, GameAccountDataExtract *pExtract)
{
	S32 bControlled = false;
	int iLevelCombatPrevious = pchar->iLevelCombat;

	ANALYSIS_ASSUME(pchar->pEntParent);

	if(pchar->pLevelCombatControl)
	{
		S32 bDestroy = true;

		PERFINFO_AUTO_START("character_LevelCombatUpdate Controlled", 1);

		if(pchar->pLevelCombatControl->iLevelForce)
		{
			// Direct forcing
			pchar->iLevelCombat = pchar->pLevelCombatControl->iLevelForce;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			bDestroy = false;
		}
		else if(pchar->pLevelCombatControl->erLink || pchar->pLevelCombatControl->cidLinkPlayer)
		{
			S32 bValidLink = false;
			S32 bLinkInDanger = false;
			
			// Link to another entity
			Entity *eLink = entFromEntityRef(iPartitionIdx, pchar->pLevelCombatControl->erLink);

			// EntityRef lookup failed, check for ContainerID
			if(!eLink && pchar->pLevelCombatControl->cidLinkPlayer)
			{
				eLink = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pchar->pLevelCombatControl->cidLinkPlayer);
				if(eLink)
				{
					// ContainerID worked, reset the EntityRef
					pchar->pLevelCombatControl->erLink = entGetRef(eLink);
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				}
			}

			// If the entity doesn't exist, isn't a Character, is owned, is
			//  having its own combat level controlled, or the character has an unset combat level,
			//  then the link is temporarily invalid
			if(!eLink
				|| !eLink->pChar
				|| eLink->erOwner
				|| eLink->pChar->pLevelCombatControl
				|| !eLink->pChar->iLevelCombat)
			{
				// elink not on map
				bValidLink = false;
				bDestroy = false;

				character_SideKickOutOfRangeMsg(pchar, NULL);
			}
			else if(eLink->pChar==pchar) // If the entity is you (somehow), the link must be undone
			{
				bValidLink = false;
				bDestroy = true;
			}
			else if(eLink->pChar->iLevelCombat < 1)
			{
				// the linked character is zoning into the map and therefore has no combat level, use invalid
				bValidLink = false;
				bDestroy = false;

				character_SideKickOutOfRangeMsg(pchar, eLink);
			}
			else
			{
				bValidLink = true;
				bDestroy = false;

				// Check optional requirements
				if(pchar->pLevelCombatControl->bLinkRequiresTeam)
				{
					if(!team_OnSameTeam(pchar->pEntParent,eLink))
						bValidLink = false;
				}
				if(pchar->pLevelCombatControl->fMaxRange && (!g_CombatConfig.bInfiniteSidekickRangeInstances || zmapInfoGetMapType(NULL) == ZMTYPE_STATIC))
				{
					F32 fDistance = entGetDistance(pchar->pEntParent, NULL, eLink, NULL, NULL);
					if(fDistance > pchar->pLevelCombatControl->fMaxRange)
					{
						character_SideKickOutOfRangeMsg(pchar, eLink);

						bValidLink = false;
					}
					else if(fDistance > pchar->pLevelCombatControl->fMaxRange * 0.75f)
					{
						bLinkInDanger = true;
					}
				}
			}

		

			if(bValidLink)
			{
				U32 uiNow = timeSecondsSince2000();
				// Perfectly valid, make sure everything is nicely updated
				pchar->iLevelCombat = eLink->pChar->iLevelCombat;

				// Set time stamp now in case we zone to a new map
				pchar->pLevelCombatControl->uiTimestampInvalid = uiNow;
				pchar->pLevelCombatControl->iLevelInvalid = pchar->iLevelCombat;

				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

				bDestroy = false;
				
				if(!bLinkInDanger)
				{
					if(uiNow > pchar->pLevelCombatControl->uiTimestampInDanger + 20)
						pchar->pLevelCombatControl->uiTimestampInDanger = 0;
				}
				else if(!pchar->pLevelCombatControl->uiTimestampInDanger)
				{
					pchar->pLevelCombatControl->uiTimestampInDanger = uiNow;
#ifdef GAMESERVER
					ClientCmd_NotifySend(pchar->pEntParent, kNotifyType_SidekickingFailed, entTranslateMessageKey(pchar->pEntParent, "SideKicking.NearMaxRange"), NULL, NULL);
#endif
				}
				else if(uiNow > pchar->pLevelCombatControl->uiTimestampInDanger + 20)
				{
					pchar->pLevelCombatControl->uiTimestampInDanger = uiNow;
#ifdef GAMESERVER
					ClientCmd_NotifySend(pchar->pEntParent, kNotifyType_SidekickingFailed, entTranslateMessageKey(pchar->pEntParent, "SideKicking.NearMaxRange"), NULL, NULL);
#endif
				}
			}
			else if(!bDestroy)
			{
				// Invalid but not forced to destroy, update the invalid timer
				U32 uiNow = timeSecondsSince2000();
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				if(!pchar->pLevelCombatControl->uiTimestampInvalid)
				{
					pchar->pLevelCombatControl->uiTimestampInvalid = uiNow;
					pchar->pLevelCombatControl->iLevelInvalid = pchar->iLevelCombat;
				}
				else
				{
					if(uiNow > pchar->pLevelCombatControl->uiTimestampInvalid + uiMaxTimeInvalid)
					{
						bDestroy = true;
					}
					else if(pchar->pLevelCombatControl->iLevelInvalid)
					{
						pchar->iLevelCombat = pchar->pLevelCombatControl->iLevelInvalid;
					}
				}
			}
		}

		// Control should be destroyed or used
		if(bDestroy)
		{
#ifdef GAMESERVER
			//Turn off sidekicking
			gslTeam_cmd_SetSidekicking(pchar->pEntParent, 0);
			if(pchar->pLevelCombatControl)
			{
				character_RemovePowerTemporary(pchar,pchar->pLevelCombatControl->uiSidekickingPowerID);
			}
#endif
			StructDestroySafe(parse_LevelCombatControl,&pchar->pLevelCombatControl);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
		else
		{
			bControlled = true;
		}

		PERFINFO_AUTO_STOP();
	}

	// Not controlled, and it's a player, so update it to xp level
	if(!bControlled && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		PERFINFO_AUTO_START("character_LevelCombatUpdate NotControlled", 1);

		pchar->iLevelCombat = entity_GetSavedExpLevel(pchar->pEntParent);

		// Detect an exp-level change.  This could be triggered by dropping out of
		//  a controlled combat level, but we don't really mind.
		if(iLevelCombatPrevious!=pchar->iLevelCombat)
		{
#ifdef GAMESERVER
			// Auto buy the power trees
			entity_PowerTreeAutoBuy(iPartitionIdx,pchar->pEntParent,NULL);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
#endif
		}

		PERFINFO_AUTO_STOP();
	}

	if(iLevelCombatPrevious!=pchar->iLevelCombat)
	{
		if(pchar->iLevelCombat < iLevelCombatPrevious)
		{
			// Special case when the combat level goes down
			CharacterLevelCombatReduced(pchar);
		}

		CharacterLevelCombatChanged(iPartitionIdx, pchar, bLevelControlRemoved, pExtract);
	}
}

// Clears the Character's controls on its iLevelCombat, if any
void character_LevelCombatNatural(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	if(pchar->pLevelCombatControl)
	{
#ifdef GAMESERVER
		if(pchar->pLevelCombatControl)
		{
			character_RemovePowerTemporary(pchar,pchar->pLevelCombatControl->uiSidekickingPowerID);
		}
#endif
		StructDestroySafe(parse_LevelCombatControl,&pchar->pLevelCombatControl);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		character_LevelCombatUpdate(iPartitionIdx, pchar, true, pExtract);
	}
}

// Sets the Character's forced iLevelCombat
void character_LevelCombatForce(int iPartitionIdx, Character *pchar, S32 iLevel, GameAccountDataExtract *pExtract)
{
#ifdef GAMESERVER
	if(pchar && pchar->pLevelCombatControl)
	{
		character_RemovePowerTemporary(pchar,pchar->pLevelCombatControl->uiSidekickingPowerID);
	}
#endif
	StructDestroySafe(parse_LevelCombatControl,&pchar->pLevelCombatControl);
	pchar->pLevelCombatControl = StructCreate(parse_LevelCombatControl);
	pchar->pLevelCombatControl->iLevelForce = MAX(1,iLevel);
	character_LevelCombatUpdate(iPartitionIdx, pchar, false, pExtract);
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
}

// Sets the Character's iLevelCombat to link to another entref
void character_LevelCombatLink(int iPartitionIdx, Character *pchar, EntityRef erLink, U32 bRequiresTeam, F32 fMaxRange, GameAccountDataExtract *pExtract)
{
	Entity *eLink;

	// Note that the requested link may not be legal.  The legality is checked in
	//  the update function automatically, and I'd rather not duplicate a bunch of
	//  code that is just going to get more and more complicated.  So we just assume
	//  whatever inputs we got here are legal, and leave it up to the update function
	//  to throw it away if it's not.
	
	// Cannot use level linking on pvp maps
	if(!pchar->pLevelCombatControl || !pchar->pLevelCombatControl->iLevelForce)
	{
#ifdef GAMESERVER
		if(pchar->pLevelCombatControl)
			character_RemovePowerTemporary(pchar,pchar->pLevelCombatControl->uiSidekickingPowerID);
#endif
		StructDestroySafe(parse_LevelCombatControl,&pchar->pLevelCombatControl);
		pchar->pLevelCombatControl = StructCreate(parse_LevelCombatControl);
		pchar->pLevelCombatControl->erLink = erLink;
		pchar->pLevelCombatControl->bLinkRequiresTeam = bRequiresTeam;
		pchar->pLevelCombatControl->fMaxRange = fMaxRange;

		eLink = entFromEntityRef(iPartitionIdx, erLink);
		if(eLink && entGetType(eLink)==GLOBALTYPE_ENTITYPLAYER)
		{
			pchar->pLevelCombatControl->cidLinkPlayer = entGetContainerID(eLink);
		}

		character_LevelCombatUpdate(iPartitionIdx, pchar, false, pExtract);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	}
}

void character_ResetPartial(int iPartitionIdx, Character *pchar, Entity *e, int mods, int unownedModsOnly, int act, int recharge, int period, int status, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	// Remove mods
	if(mods)
		character_RemoveAllMods(iPartitionIdx, pchar,false,unownedModsOnly,kModExpirationReason_Unset,pExtract);

	// Reset recharge state
	if(recharge)
	{
		int i;
		for(i=eaSize(&pchar->ppPowerRefRecharge)-1; i>=0; i--)
		{
			Power *ppow = character_FindPowerByRef(pchar,pchar->ppPowerRefRecharge[i]);

			if(ppow) 
			{
				ppow->fTimeRecharge = 0;

#ifdef GAMESERVER
				if(g_funcNotifyPowerRechargedCallback)
					g_funcNotifyPowerRechargedCallback(pchar->pEntParent, ppow);
#endif
			}
		}
		eaDestroyEx(&pchar->ppPowerRefRecharge,powerref_Destroy);
	}

	// Clean up activations
	if(act)
	{
		character_ActAllCancel(iPartitionIdx,pchar,true);
		character_ActFinishedDestroy(pchar);
		pchar->eChargeMode = kChargeMode_None;
	}

	// Deactivate periodic powers
	if(period)
	{
		character_DeactivatePassives(iPartitionIdx,pchar);
		character_DeactivateToggles(iPartitionIdx,pchar,pmTimestamp(0),true,false);
	}
/*	// This causes held characters that reset not to remove their hold PMIgnore.
	// Not sure why this was here, since it breaks any checks vs. last frame's values.
	if(status)
	{
		pchar->pattrBasic->fRoot = character_GetClassAttrib(pchar, kClassAttribAspect_Basic, kAttribType_Root);
		pchar->pattrBasic->fHold = character_GetClassAttrib(pchar, kClassAttribAspect_Basic, kAttribType_Hold);
		pchar->pattrBasic->fDisable = character_GetClassAttrib(pchar, kClassAttribAspect_Basic, kAttribType_Disable);
	}
*/
#endif
}

// Automatically spend the character's attrib stat points based on the character class
void character_AutoSpendStatPoints(Character *pchar)
{
#ifdef GAMESERVER
	CharacterClass* pClass = GET_REF(pchar->hClass);

	if(pClass)
	{	
		int count;

		for (count=eaSize(&pClass->ppAutoSpendStatPoints)-1; count>=0; --count)
		{
			StatPointPoolDef *pDef = StatPointPool_DefFromAttrib(pClass->ppAutoSpendStatPoints[count]->eType);
			if (pDef && entity_GetAssignedStatUnspent(CONTAINER_NOCONST(Entity, pchar->pEntParent), pDef->pchName) > 0)
			{
				int diff;
				diff = pClass->ppAutoSpendStatPoints[count]->iPoints - (int)*F32PTR_OF_ATTRIB(pchar->pattrBasic,pClass->ppAutoSpendStatPoints[count]->eType);
				if (diff > 0)
				{
					//BUY IT!!
					character_ModifyStatPointsByEnum(pchar->pEntParent, pDef->pchName, pClass->ppAutoSpendStatPoints[count]->eType, diff);
				}
			}

		}
	}
#endif GAMESERVER
}

// Takes an existing character and resets all their state (recharging and active powers, mods, pets, hp, etc)
void character_Reset(int iPartitionIdx, Character *pchar, Entity *e, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int i;

	PERFINFO_AUTO_START_FUNC();

	pchar->pEntParent = e;

	// Create my powers movement
	PM_CREATE_SAFE(pchar);

	// Reset attribs
	if (pchar->pattrBasic)
	{
		StructReset(parse_CharacterAttribs, pchar->pattrBasic);
	}
	else
	{
		pchar->pattrBasic = StructCreate(parse_CharacterAttribs);
	}

	character_ResetPartial(iPartitionIdx, pchar, e, true, false, true, true, true, true, pExtract);

	// Reset powers
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		// TODO(JW): Reset power
	}
	 
	character_ResetPowersArray(iPartitionIdx, pchar, pExtract);

	character_updateTacticalRequirements(pchar);

	pchar->bSkipAccrueMods = false;
	character_DirtyInnatePowers(pchar);
	character_DirtyPowerStats(pchar);
	character_AccrueMods(iPartitionIdx,pchar,0.0f,pExtract);
	character_DirtyInnateAccrual(pchar);
	if (g_CombatConfig.bCritterStats || entIsPlayer(e))
	{
		character_DirtyPowerStats(pchar);
		character_AccrueMods(iPartitionIdx,pchar,0.0f,pExtract);
	}


	pchar->pattrBasic->fHitPoints = pchar->pattrBasic->fHitPointsMax;
	pchar->pattrBasic->fPower = pchar->pattrBasic->fPowerEquilibrium;
	
	//Reset all pools when the character is reset
	character_AttribPoolsReset(pchar, false);

	pchar->fTimerRegeneration = 0.0f;
	pchar->fTimerRecovery = 0.0f;

	// TODO(JW): Set up movement modifiers?

	// Not sure if this belongs here
	character_ActivatePassives(iPartitionIdx,pchar,pExtract);
	damageTracker_ClearAll(pchar);

	character_AutoSpendStatPoints(pchar);

	PERFINFO_AUTO_STOP();
#endif
}

AUTO_FIXUPFUNC;
TextParserResult character_Fixup(Character *pchar,enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch (eFixupType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		pchar->bCanRegen[0] = true;
		pchar->bCanRegen[1] = true;
		pchar->bCanRegen[2] = true;
		break;
	}

	return bRet;
}

// Cleans up transacted data on a Character after loading from the db
AUTO_TRANS_HELPER
ATR_LOCKS(e, ".Pchar.Pppowertrees, .Pchar.Hspecies, .Pchar.Hclass, .Pchar.Ppassignedstats, .Pchar.Pppowersclass, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, .Pchar.Pppowerspersonal, .Pchar.Ilevelexp");
void character_LoadTransact(ATH_ARG NOCONST(Entity) *e)
{
	int i;
	StatPointPoolDef *pStatPointPoolDef;
	RefDictIterator iter;

	assert(NONNULL(e) && NONNULL(e->pChar));

	if(!GET_REF(e->pChar->hClass))
	{
		REMOVE_HANDLE(e->pChar->hClass);
		SET_HANDLE_FROM_STRING(g_hCharacterClassDict,"Default",e->pChar->hClass);
	}

	entity_FixPowersClassHelper(e);
	character_FixPowersPersonalHelper(e->pChar);

	// Finalize tree nodes
	// TODO(JW): Make this a single function call, merge with character_FixPTNodeEnhancementsHelper()
	for(i=eaSize(&e->pChar->ppPowerTrees)-1; i>=0; i--)
	{
		if(!GET_REF(e->pChar->ppPowerTrees[i]->hDef))
		{
			Alertf("Power Tree %s not found, removing tree from character",REF_STRING_FROM_HANDLE(e->pChar->ppPowerTrees[i]->hDef));
			eaRemove(&e->pChar->ppPowerTrees,i);
			continue;
		}
		powertree_FinalizeNodes(e->pChar->ppPowerTrees[i]);
	}

	// Fix AssignedStats
	RefSystem_InitRefDictIterator(g_hStatPointPoolDict, &iter);

	while(pStatPointPoolDef = (StatPointPoolDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		i = entity_GetAssignedStatUnspent(e, pStatPointPoolDef->pchName);
		if(i < 0)
		{
			// Spent more than allowed, destroy the earray to reset them all
			eaDestroyStructNoConst(&e->pChar->ppAssignedStats,parse_AssignedStats);
			break;
		}
	}

	character_FixPTNodeEnhancementsHelper(e->pChar);
}



// Deactivates Powers that shouldn't be active after a Character load
static void CharacterLoadDeactivatePowers(SA_PARAM_NN_VALID Character *pchar)
{
	S32 i;
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		Power *ppow = pchar->ppPowers[i];
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		if(!pdef
			|| pdef->eType == kPowerType_Passive
			|| pdef->eType == kPowerType_Toggle
			|| pdef->eType == kPowerType_Enhancement
			|| pdef->eType == kPowerType_Innate)
			continue;

		// Mark it inactive
		power_SetActive(ppow, false);
	}
}


// Cleans up or initializes non-transacted data on a Character after loading from the db
// if the character is new, character_Reset will be called, which will ignore bResetPowersArray
void character_LoadNonTransact(int iPartitionIdx, Entity *e, S32 bOffline)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	GameAccountDataExtract *pExtract;
	Character *pchar = e->pChar;
	S32 bLevelDecreased = false;

	PERFINFO_AUTO_START_FUNC();

	assert(pchar);

	// If this gets hit, it means someone is trying to run the loading code on this
	//  Character twice, which is not supported and could have undesirable side-effects.
	devassert(!pchar->bLoaded);

	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

	if(!bOffline)
	{
		PM_CREATE_SAFE(pchar);
	}

	// Set the default iLevelCombat and remove any level controls
	if(pchar->pEntParent->myEntityType == GLOBALTYPE_ENTITYPLAYER)
	{
		pchar->iLevelCombat = entity_GetSavedExpLevel(pchar->pEntParent);
	}
	else if(pchar->pEntParent->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		Entity *eOwner = entFromContainerIDAnyPartition(pchar->pEntParent->pSaved->conOwner.containerType,pchar->pEntParent->pSaved->conOwner.containerID);

		if(eOwner) {
			pchar->iLevelCombat = eOwner->pChar->iLevelCombat;
		} else {
			pchar->iLevelCombat = 1;

			if (!bOffline) {
				ErrorDetailsf("Character %s, Partition %d",CHARDEBUGNAME(pchar), iPartitionIdx);
				Errorf("Couldn't find owner entity when trying to initialize combat level of saved pet");
			}
		}
	}

	pExtract = entity_GetCachedGameAccountDataExtract(e);

	//Controlled level update
	{
		S32 iLevelPrevious = pchar->iLevelCombat;
		//If I had a previous Combat Control struct, use the correct level from that
		if(pchar->pLevelCombatControl)
		{
			if(pchar->pLevelCombatControl->iLevelForce)
				pchar->iLevelCombat = iLevelPrevious = pchar->pLevelCombatControl->iLevelForce;
			else if(pchar->pLevelCombatControl->iLevelInvalid)
				pchar->iLevelCombat = iLevelPrevious = pchar->pLevelCombatControl->iLevelInvalid;
		}
		
		//Then do the load non-transact logic which should set the level correctly.
		if(pchar->pLevelCombatControl && (zmapInfoGetMapType(NULL) == ZMTYPE_PVP || pchar->pLevelCombatControl->iLevelForce > 0))
		{
			character_LevelCombatNatural(iPartitionIdx, pchar, pExtract);
		}
		else
		{
			character_LevelCombatUpdate(iPartitionIdx, pchar, false, pExtract);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}

		if(iLevelPrevious > pchar->iLevelCombat)
		{
			bLevelDecreased = true;
		}
	}

#ifdef GAMESERVER
	entity_UpdateItemSetsCount(e);
	character_LoadAddTemporaryPowers(pchar);
	character_UpdateTemporaryPowerTrees(iPartitionIdx,pchar);
#endif

	character_LoadAttribMods(pchar, bOffline, bLevelDecreased);
	character_PowerTreesFixup(pchar);
	character_LoadFixCooldowns(iPartitionIdx, pchar);
	
	if(!pchar->pattrBasic || pchar->pattrBasic->fHitPoints<0)
	{	
		// If this is a new Character, run a full reset
		character_Reset(iPartitionIdx, pchar, pchar->pEntParent, pExtract);
	}
	else
	{	
		character_ResetPowersArray(iPartitionIdx, pchar, pExtract);

		pchar->bSkipAccrueMods = false;

		character_DirtyInnatePowers(pchar);
		character_DirtyPowerStats(pchar);		
		
		if(bOffline)
		{
			character_AccrueMods(iPartitionIdx, pchar, 0.0f, pExtract);
		}
		else
		{
			// Do the first run in bootstrapping mode
			character_AccrueModsEx(iPartitionIdx, pchar, 0.0f, pExtract, true, pchar->uiTimeLoggedOutForCombat);

			// Mark the innate accrual set as dirty because power stats
			// rely on reading the basic attributes which is not properly
			// calculated in the first pass.
			character_DirtyInnateAccrual(pchar);

			// Do the second pass
			character_AccrueMods(iPartitionIdx, pchar, 0.0f, pExtract);
		}	

		//Reset non-persisted attrib pools
		character_AttribPoolsReset(pchar, true);
	}

	// Recover Attributes
	character_LoadSavedAttributes(pchar);

	// Update movement state
	// Update movement state and death state
	if(!bOffline)
	{
		character_UpdateMovement(pchar,NULL);

		if(pchar->pattrBasic && pchar->pattrBasic->fHitPoints<=0)
			entDie(pchar->pEntParent,-1,false,false,pExtract);
	}
	//If for some reason the character loads from the DB without the build index and slot index in sync, fix them
	if(pchar->pSlots &&  e->pSaved && 
		pchar->pSlots->uiIndex != e->pSaved->uiIndexBuild)
	{
		pchar->pSlots->uiIndex = e->pSaved->uiIndexBuild;
	}


	// Make sure all slotting is valid
	character_PowerSlotsValidate(pchar);

	// Recover Activation State
	character_LoadActState(iPartitionIdx,pchar,&pchar->ppActivationState);
	eaDestroyStruct(&pchar->ppActivationState,parse_PowerActivationState);

	// Start up passives
	character_RefreshPassives(iPartitionIdx,pchar,pExtract);

	// Restart toggles
	character_RefreshToggles(iPartitionIdx,pchar,pExtract);

	// Make sure Powers that shouldn't be active aren't
	CharacterLoadDeactivatePowers(pchar);

#ifdef GAMESERVER

	CharacterTickRecharge(iPartitionIdx, pchar, 0, pchar->uiTimeLoggedOutForCombat);

	// Fix up the charge refill timers
	character_LoadTimeChargeRefillFixup(iPartitionIdx, pchar);
#endif

	pchar->uiTimeLoggedOutForCombat = 0;
	if(!bOffline)
	{
#ifdef GAMESERVER
		pchar->iFreeRespecAvailable = trhEntity_GetFreeRespecTime(CONTAINER_NOCONST(Entity, pchar->pEntParent));
		character_CheckAndPerformForceRespec(pchar);
#endif //GAMESERVER

		// Set up BattleForm based on optional state on this map
		if(g_CombatConfig.pBattleForm)
		{
			if(combatconfig_BattleFormOptional())
			{
				character_SetBattleForm(entGetPartitionIdx(e),pchar,false,true,false,pExtract);
			}
			else
			{
				character_SetBattleForm(entGetPartitionIdx(e),pchar,true,false,false,pExtract);
			}
		}

	}

	pchar->bLoaded = true;

	PERFINFO_AUTO_STOP();
#endif
}

// Clears out any information alloced in the PreSaveInfo struct
void CharacterPreSaveInfo_Destroy(CharacterPreSaveInfo *pPreSaveInfo)
{
	StructDestroy(parse_TempAttributes,pPreSaveInfo->pTempAttributes);
}

// Fills in a CharacterPreSaveInfo struct with all the current information on the character
void character_FillInPreSaveInfo(Character *pChar, CharacterPreSaveInfo *pPreSaveInfo)
{
	pPreSaveInfo->pTempAttributes = StructCreate(parse_TempAttributes);
	character_SaveTempAttributes(pChar,pPreSaveInfo->pTempAttributes);
}

// Cleans up a character before saving to the db
void character_PreSave(Entity *e)
{
	Character *pchar = e->pChar;
	character_SaveAttribMods(pchar);
	character_SaveCooldowns(pchar);
	character_SaveActState(pchar,&pchar->ppActivationState);
	//character_SaveAttributes(pchar);
}

// Sends the data necessary to have the client properly initialize its Character when it loads
void character_SendClientInitData(Character *pchar)
{
	Entity *e = pchar->pEntParent;
	if(entCheckFlag(e,ENTITYFLAG_IS_PLAYER))
	{
		int i,s;
		ClientCharacterInitData *pdata = StructAlloc(parse_ClientCharacterInitData);

		// Fill in the toggle PowerActivations
		s = eaSize(&pchar->ppPowerActToggle);
		for(i=0; i<s; i++)
		{
			PowerActivation *pact = pchar->ppPowerActToggle[i];
			PowerActivationState *pState = StructAlloc(parse_PowerActivationState);
			StructCopy(parse_PowerRef,&pact->ref,&pState->ref,0,0,0);
			pState->uchID = pact->uchID;
			pState->uiPeriod = pact->uiPeriod;
			pState->fTimerActivate = pact->fTimerActivate;
			pState->fTimeCharged = pact->fTimeCharged;
			pState->fTimeChargedTotal = pact->fTimeChargedTotal;
			pState->fTimeActivating = pact->fTimeActivating;
			eaPush(&pdata->ppActivationStateToggle,pState);
		}

		// Fill in the PowerRechargeState
		character_RechargeStateBuild(pchar,&pdata->rechargeState);

		for(i=0;i<eaSize(&pchar->ppCooldownTimers);i++)
		{
			CooldownTimer *pTimer = cooldowntimer_Create();
			StructCopy(parse_CooldownTimer,pchar->ppCooldownTimers[i],pTimer,0,0,0);
			eaPush(&pdata->ppCooldownTimers,pTimer);
		}


#ifdef GAMESERVER
		// Send the actual data via ClientCmd
		ClientCmd_powersCmdCharacterInitData(e,pdata);
#endif
		
		StructDestroy(parse_ClientCharacterInitData,pdata);
	}
}


static void CharacterCombatTrackerAINotify(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID CombatTrackerNet *pNet)
{
#ifdef	GAMESERVER
	static S32 s_bDisabled = false;
	if(s_bDisabled)
		return;

	PERFINFO_AUTO_START_FUNC();
	{
		S32 iPartitionIdx = entGetPartitionIdx(pchar->pEntParent);
		Entity *pAggroEnt;
		Entity *pOwnerEnt = entFromEntityRef(iPartitionIdx,pNet->erOwner);
		Entity *pSourceEnt = entFromEntityRef(iPartitionIdx,pNet->erSource);
		F32 fThreatScale = 1;

		pAggroEnt = aiDetermineAggroEntity(pchar->pEntParent, pSourceEnt, pOwnerEnt);

		if(pAggroEnt && pAggroEnt->pChar)
		{
			// TODO(JW): This is lazier than it should be (probably should be calculated at
			//  mod apply time), but we'll ignore that for now.
			fThreatScale = pAggroEnt->pChar->pattrBasic->fAIThreatScale;
		}

		s_bDisabled = !aiNotifyPowerMissed(pchar->pEntParent,pAggroEnt,fThreatScale);
	}
	PERFINFO_AUTO_STOP();
#endif

}

// Mark the Character's CombatTrackerList, so that it is considered changed on the client
void character_CombatTrackerListTouch(Character *pchar)
{
	if(!pchar->combatTrackerNetList.bTouched)
	{
		// Something happened to character, mark the entity as active
		entSetActive(pchar->pEntParent);

		// Mark it touched
		pchar->combatTrackerNetList.bTouched = 1;

		// Mark the net list deeply dirty
		entity_SetDirtyBit(pchar->pEntParent,parse_CombatTrackerNetList,&pchar->combatTrackerNetList,true);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

		// Update the id to a new non-zero value
		pchar->combatTrackerNetList.id++;
		if(!pchar->combatTrackerNetList.id)
		{
			pchar->combatTrackerNetList.id = 1;
		}
	}
}

// Adds a CombatTracker event to the Character
// @param pSecondaryDef: Optional secondary power, for example shields absorbing part 
//						 of the damage from pdef.  Usually NULL.
// @param fMagnitudeBase: usually unmitigated damage, shown in parens after fMagnitude.
CombatTrackerNet* character_CombatTrackerAdd(	Character *pchar,
												PowerDef *pdef,
												EntityRef erOwner,
												EntityRef erSource,
												PowerDef *pSecondaryDef,
												AttribType eType,
												F32 fMagnitude,
												F32 fMagnitudeBase,
												CombatTrackerFlag eFlags,
												F32 fDelay,
												S32 bAINotifyMiss)
{
	CombatTrackerNet *pNet = StructCreate(parse_CombatTrackerNet);

	pNet->erOwner = erOwner;
	pNet->erSource = erSource;
	pNet->eType = eType;
	pNet->fMagnitude = fMagnitude;
	if(fMagnitudeBase && fMagnitudeBase!=fMagnitude)
	{
		pNet->fMagnitudeBase = fMagnitudeBase;
	}
	COPY_HANDLE(pNet->hDisplayName,pdef->msgDisplayName.hMessage);
	if(pSecondaryDef){
		COPY_HANDLE(pNet->hSecondaryDisplayName,pSecondaryDef->msgDisplayName.hMessage);
	}
	pNet->eFlags = eFlags;
	pNet->fDelay = fDelay;
	pNet->pPowerDef = pdef;

	if (pdef && pdef->bForceHideDamageFloats)
		pNet->eFlags |= kCombatTrackerFlag_NoFloater;

	if(fDelay)
	{
		if(bAINotifyMiss)
			pNet->bAINotifyMiss = true;
		eaPush(&pchar->combatTrackerNetList.ppEventsBuffer,pNet);
	}
	else
	{
		eaPush(&pchar->combatTrackerNetList.ppEvents,pNet);
		character_CombatTrackerListTouch(pchar);

		if(bAINotifyMiss)
			CharacterCombatTrackerAINotify(pchar,pNet);
	}

	// Commented out by jpanttaja to reduce log load
	//entLog(LOG_COMBAT,pchar->pEntParent,"CombatTrackerAdded","%f %d owned by %d delay %f",fMagnitude,eType,erOwner,fDelay);
	return pNet;
}

// used to report immunity to an attrib
void character_CreateImmunityCombatTracker(	Character *pchar, 
											Entity *pSource,
											AttribMod *pMod, 
											AttribModDef *pModDef)
{
	CombatTrackerNet *pNet = character_CombatTrackerAdd(pchar, pModDef->pPowerDef, 
														pMod->erOwner, pMod->erSource, NULL,
														pModDef->offAttrib, 0.f, 0.f, 
														kCombatTrackerFlag_Immune, 0.5f, false);
	if (pNet && pSource && pSource->pPlayer && pSource->pChar)
	{
		Power* pPow = character_FindComboParentByDef(pSource->pChar, pModDef->pPowerDef);
		if (pPow)
		{
			while (pPow->pParentPower)
				pPow = pPow->pParentPower;
			pNet->powID = pPow->uiID;
		}
	}
}

#ifdef GAMECLIENT

// Copies all CombatTrackers in the regular event list into the client buffer list
void character_CombatTrackerBuffer(Character *pchar)
{
	if(pchar->combatTrackerNetList.id)
	{
		bool bHasDamageEvent = false;
		F32 fMaxDamagePercent = 0.f;
		int i,s=eaSize(&pchar->combatTrackerNetList.ppEvents);
		for(i=0; i<s; i++)
		{
			eaPush(&pchar->combatTrackerNetList.ppEventsBuffer,StructClone(parse_CombatTrackerNet,pchar->combatTrackerNetList.ppEvents[i]));

			if (ATTRIB_DAMAGE(pchar->combatTrackerNetList.ppEvents[i]->eType))
			{
				F32 fDamageAmount = MAX(0.f, pchar->combatTrackerNetList.ppEvents[i]->fMagnitude);
				F32 fDamagePercent = CLAMP((fDamageAmount / pchar->pattrBasic->fHitPointsMax), 0.f, 1.f);
				MAX1(fMaxDamagePercent, fDamagePercent);

				bHasDamageEvent = true;
			}
		}
		if (g_CombatConfig.pPlayerHitFXConfig &&
			bHasDamageEvent && 
			pchar->pEntParent == entActivePlayerPtr())
		{
			FOR_EACH_IN_EARRAY_FORWARDS(g_CombatConfig.pPlayerHitFXConfig->ppOptionalFX, ConditionalPlayerHitFX, pPlayerHitFx)
			{
				F32 fMinimumHPPercentage = CLAMP(pPlayerHitFx->fMinumumHPPercentage, 0.f, 1.f);
				F32 fMaximumHPPercentage = CLAMP(pPlayerHitFx->fMaximumHPPercentage, fMinimumHPPercentage, 1.f);
				if (pPlayerHitFx->pchPlayerHitFX &&
					pPlayerHitFx->pchPlayerHitFX[0] &&
					fMaxDamagePercent > pPlayerHitFx->fMinumumHPPercentage && fMaxDamagePercent <= pPlayerHitFx->fMaximumHPPercentage)
				{
					dtAddFx(pchar->pEntParent->dyn.guidFxMan, pPlayerHitFx->pchPlayerHitFX, 
						NULL, 0, 0, 0.0f, 0, NULL, eDynFxSource_HardCoded, NULL, NULL);	
				}
			}
			FOR_EACH_END
		}
		pchar->combatTrackerNetList.id = 0;
	}
}


void gclCharacter_ForceDismount(Character *pChar)
{
	if (g_CombatConfig.iMountPowerCategory &&
		pChar->pEntParent->costumeRef.pMountCostume && 
		!pChar->pEntParent->costumeRef.bPredictDismount)
	{
		pChar->pEntParent->costumeRef.bPredictDismount = true;
		costumeGenerate_FixEntityCostume(pChar->pEntParent);
		ServerCmd_DismountServer();
	}
}

bool gclCharacter_HasMountedCostume(Character *pChar)
{
	return g_CombatConfig.iMountPowerCategory && pChar->pEntParent->costumeRef.pMountCostume;
}

#endif

// Checks all the timers on the buffered event list and transfers them to the regular event list when the delay reaches 0
void character_CombatTrackerBufferTick(int iPartitionIdx, Character *pchar, F32 fTick)
{
	int i,s=eaSize(&pchar->combatTrackerNetList.ppEventsBuffer);
	if(s)
	{
		bool bChanged = false;

		PERFINFO_AUTO_START_FUNC();

		for(i=s-1;i>=0;i--)
		{
			CombatTrackerNet *pNet = pchar->combatTrackerNetList.ppEventsBuffer[i];

			pNet->fDelay -= fTick;

			if(pNet->fDelay <= 0.0f)
			{
				pNet->fDelay = 0;
				eaRemove(&pchar->combatTrackerNetList.ppEventsBuffer,i);
				eaPush(&pchar->combatTrackerNetList.ppEvents,pNet);
				bChanged = true;

				// Track timed combat events
				//  TODO(JW): The mapping of flag to event is a bit too hardcoded here
				if(pNet->eFlags & kCombatTrackerFlag_Miss)
				{
					Entity *eSource = entFromEntityRef(iPartitionIdx, pNet->erSource);
					character_CombatEventTrackInOut(pchar, kCombatEvent_MissInTimed, kCombatEvent_MissOutTimed, eSource,
													pNet->pPowerDef, NULL, 0, 0, NULL, NULL);
				}
				else if(pNet->eFlags & kCombatTrackerFlag_Dodge)
				{
					Entity *eSource = entFromEntityRef(iPartitionIdx, pNet->erSource);
					character_CombatEventTrackInOut(pchar, kCombatEvent_DodgeInTimed, kCombatEvent_DodgeOutTimed, 
													eSource, pNet->pPowerDef, NULL, 0, 0, NULL, NULL);
				}

				if(pNet->bAINotifyMiss)
					CharacterCombatTrackerAINotify(pchar,pNet);
			}
		}

		if(bChanged)
			character_CombatTrackerListTouch(pchar);

		PERFINFO_AUTO_STOP();
	}
}


// Fills in the PowerRechargeState for the Character
void character_RechargeStateBuild(Character *pchar, PowerRechargeState *pState)
{
	int i;
	for(i=eaSize(&pchar->ppPowerRefRecharge)-1; i>=0; i--)
	{
		Power *ppow = character_FindPowerByIDComplete(pchar,pchar->ppPowerRefRecharge[i]->uiID);
		if(ppow)
		{
			ea32Push(&pState->puiIDs,ppow->uiID);
			eafPush(&pState->pfTimes,ppow->fTimeRecharge);
		}
	}
}

// Applies the PowerRechargeState to the Character
void character_RechargeStateApply(int iPartitionIdx, Character *pchar, PowerRechargeState *pState)
{
	int i;
	for(i=ea32Size(&pState->puiIDs)-1; i>=0; i--)
	{
		Power *ppow = character_FindPowerByIDComplete(pchar,pState->puiIDs[i]);
		if(ppow)
		{
			power_SetRecharge(iPartitionIdx,pchar,ppow,pState->pfTimes[i]);
		}
	}
}

// Returns the value in the Character's *current* Class's table at the Character's combat level, otherwise 0
F32 character_PowerTableLookupOffset(Character *pchar, const char *pchTable, int iOffset)
{
	F32 r = 0.0f;
	CharacterClass *pClass = character_GetClassCurrent(pchar);

	if(pClass)
	{
		int idx = entity_GetCombatLevel(pchar->pEntParent) -1 + iOffset;
		r = class_powertable_Lookup(pClass,pchTable, MAX(idx,0));
	}

	return r;
}

// Returns the value in the Character's *true* Class's table at the specified index, otherwise 0.  
AUTO_TRANS_HELPER;
F32 entity_PowerTableLookupAtHelper(ATH_ARG NOCONST(Entity) *pEnt, const char *pchTable, int idx)
{
	F32 r;
	CharacterClass *pClass = NULL;
	if(NONNULL(pEnt) && NONNULL(pEnt->pChar))
		pClass = GET_REF(pEnt->pChar->hClass);

	if(pClass)
		r = class_powertable_Lookup(pClass,pchTable,idx);
	else
		r = powertable_Lookup(pchTable,idx);

	if(NONNULL(pEnt->pChar) && IS_HANDLE_ACTIVE(pEnt->pChar->hSpecies))
	{
		SpeciesDef* pSpecies = GET_REF(pEnt->pChar->hSpecies);
		int i = eaIndexedFindUsingString(&pSpecies->eaBonusTablePoints, pchTable);
		if (i >= 0)
		{
			r += pSpecies->eaBonusTablePoints[i]->pfValues[min(idx, eafSize(&pSpecies->eaBonusTablePoints[i]->pfValues)-1)];
		}
	}
	return r;
}



// Returns the matching PTNode if the characters owns this PTNodeDef.  Optionally returns the PowerTree.
PTNode *character_FindPowerTreeNode(Character *p, PTNodeDef *pDef, PowerTree **ppOutTree)
{
	if(p && pDef)
	{
		PTNodeDef *pClone = GET_REF(pDef->hNodeClone);
		int i;
		for(i=eaSize(&p->ppPowerTrees)-1; i>=0; i--)
		{
			PowerTree *pTree = p->ppPowerTrees[i];
			int j;
			for(j=eaSize(&pTree->ppNodes)-1; j>=0; j--)
			{
				PTNodeDef *pOwnedDef = GET_REF(p->ppPowerTrees[i]->ppNodes[j]->hDef);
				PTNodeDef *pOwnedClone = pOwnedDef ? GET_REF(pOwnedDef->hNodeClone) : NULL;
				// Comparing two nodes to see if they're the same is actually really annoying.
				if(pDef == pOwnedDef
					|| (pClone && pClone == pOwnedDef)
					|| (pClone && pClone == pOwnedClone)
					|| (pOwnedClone && pDef == pOwnedClone))
				{
					if(ppOutTree)
					{
						*ppOutTree = pTree;
					}
					return pTree->ppNodes[j];
				}
			}
		}
	}
	return NULL;
}

// Returns true if the Character switch in or out of BattleForm (based on their current BattleForm state)
S32 character_CanToggleBattleForm(Character *pchar)
{
	// Must have the BattleForm data in the CombatConfig
	if(!g_CombatConfig.pBattleForm)
		return false;

	// Must have an Allegiance
	if(!GET_REF(pchar->pEntParent->hAllegiance))
		return false;

	// Must be past the timer
	if(pchar->uiTimeBattleForm)
	{
		U32 uiNow = timeServerSecondsSince2000();
		if(uiNow < pchar->uiTimeBattleForm)
			return false;
	}

	// Must not be trying to leave while in combat or on a non-optional map
	if(pchar->bBattleForm && (pchar->uiTimeCombatExit || !combatconfig_BattleFormOptional()))
		return false;

	return true;
}

// Sets the Character in or out of BattleForm
void character_SetBattleForm(int iPartitionIdx, Character *pchar, bool bEnable, bool bForce, bool bTimer, GameAccountDataExtract *pExtract)
{
#ifdef GAMESERVER
	bEnable = !!bEnable;
	if(bForce
		|| ((bool)pchar->bBattleForm != bEnable
			&& character_CanToggleBattleForm(pchar)))
	{
		AllegianceDef *pAllegiance;
		pchar->bBattleForm = bEnable;
		entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,0);

		// Do a faction switch based on Allegiance if necessary
		pAllegiance = GET_REF(pchar->pEntParent->hAllegiance);
		if(pAllegiance)
		{
			CritterFaction *pFaction = GET_REF(pAllegiance->hFactionBattleForm);
			if(pFaction)
			{
				if(bEnable)
				{
					gslEntity_SetFactionOverrideByHandle(pchar->pEntParent, kFactionOverrideType_POWERS, REF_HANDLEPTR(pAllegiance->hFactionBattleForm));
				}
				else
				{
					gslEntity_ClearFaction(pchar->pEntParent, kFactionOverrideType_POWERS);
				}
			}
		}

		// Set to active costume 0 if disabled, or 1 if enabled
		costumetransaction_SetPlayerActiveCostume(pchar->pEntParent,kPCCostumeStorageType_Primary,bEnable);

		// Update the timer
		if(bTimer && g_CombatConfig.pBattleForm)
		{
			pchar->uiTimeBattleForm = timeSecondsSince2000();
			pchar->uiTimeBattleForm += bEnable ? g_CombatConfig.pBattleForm->uiTimerEnabled : g_CombatConfig.pBattleForm->uiTimerDisabled;

			// Temp hack to use the "Build_Changed_Power" like CO to trigger anim/fx
			{
				PowerDef *pPowerDef = powerdef_Find("Build_Changed_Power");
				if(pPowerDef)
				{
					character_ApplyUnownedPowerDefToSelf(iPartitionIdx, pchar,pPowerDef,pExtract);
				}
			}
		}
		else
		{
			pchar->uiTimeBattleForm = 0;
		}

		// Update ItemArt
		if(pchar->pEntParent->pEquippedArt)
			pchar->pEntParent->pEquippedArt->bCanUpdate = true;
		entity_UpdateItemArtAnimFX(pchar->pEntParent);
	}
#endif
}


S32 character_GetNextRank(Character *pChar, PTNodeDef *pDef)
{
	if(pChar && pDef)
	{
		PTNode *pNode = powertree_FindNode(pChar,NULL,pDef->pchNameFull);

		if(pNode && !pNode->bEscrow)
			return pNode->iRank + 1;
	}
/*
	if(pChar && pDef)
	{
		int i;
		for(i=eaSize(&pChar->ppPowerTrees)-1; i>=0; i--)
		{
			PowerTree *pTree = pChar->ppPowerTrees[i];
			int j;
			for(j=eaSize(&pTree->ppNodes)-1; j>=0; j--)
			{
				if(pDef == GET_REF(pTree->ppNodes[j]->hDef))
				{
					return pTree->ppNodes[j]->iRank + 1;
				}
			}
		}
	}
*/
	return 0;
}

int character_Table_TrainingPoints(Character *pChar, const char *pchTableName)
{
	int iReturn = 0;
	int iLevel = character_Find_TableTrainingLevel(pChar, pchTableName);

	if(powertable_Find(pchTableName))
	{
		CharacterClass *pClass = GET_REF(pChar->hClass);

		if(pClass)
			iReturn = (int)class_powertable_Lookup(pClass,pchTableName, MAX(iLevel,0));
	}

	return iReturn;
}

int character_ExpTreePoints(Character *pChar)
{
	int iReturn = 0;
	int iLevel = entity_CalculateFullExpLevelSlow(pChar->pEntParent);

	if(powertable_Find("TreePoints"))
	{
		CharacterClass *pClass = GET_REF(pChar->hClass);

		if(pClass)
			iReturn = (int)class_powertable_Lookup(pClass,"TreePoints", MAX(iLevel - 1,0));
	}

	return iReturn;
}


S32 entity_TreePointsToSpend(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(!pEnt || !pEnt->pChar)
		return 0;
	else
	{
		S32 iResult = character_Table_TrainingPoints(pEnt->pChar, "TreePoints") - entity_PointsSpent(CONTAINER_NOCONST(Entity, pEnt),"TreePoints");
		return iResult;
	}
}

bool character_CanBuyPowerTree(int iPartitionIdx, Character *pChar, PowerTreeDef *pTree)
{
	return entity_CanBuyPowerTreeHelper(iPartitionIdx, CONTAINER_NOCONST(Entity, pChar->pEntParent),pTree,false);
}

bool character_CanBuyPowerTreeNode( int iPartitionIdx, Character *pChar, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank )
{
	return entity_CanBuyPowerTreeNodeHelper(ATR_EMPTY_ARGS, iPartitionIdx, CONTAINER_NOCONST(Entity, pChar->pEntParent), CONTAINER_NOCONST(Entity, pChar->pEntParent), pGroup, pNodeDef, iRank, true, true, false, false);
}

bool character_CanBuyPowerTreeNodeIgnorePointsRank( Character *pChar, PowerTree *pTree, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank )
{
	return entity_CanBuyPowerTreeNodeIgnorePointsRankHelper(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pChar->pEntParent), CONTAINER_NOCONST(Entity, pChar->pEntParent), CONTAINER_NOCONST(PowerTree, pTree), pGroup, pNodeDef, iRank);
}

bool character_CanBuyPowerTreeNodeNextRank(int iPartitionIdx, Character *pChar, PTGroupDef *pGroup, PTNodeDef *pNodeDef)
{
	PTNode *pNode = character_FindPowerTreeNode(pChar, pNodeDef, NULL);
	S32 iRank = 0;
	if (pNode && !pNode->bEscrow)
		iRank = pNode->iRank + 1;
	return character_CanBuyPowerTreeNode(iPartitionIdx, pChar, pGroup, pNodeDef, iRank);
}

bool character_CanBuyPowerTreeGroup(int iPartitionIdx, Character *pChar, PTGroupDef *pGroup)
{	
	return entity_CanBuyPowerTreeGroupHelper(ATR_EMPTY_ARGS, iPartitionIdx, CONTAINER_NOCONST(Entity, pChar->pEntParent), CONTAINER_NOCONST(Entity, pChar->pEntParent), pGroup);
}

const char *character_GetDefaultPlayingStyle(Character *pChar)
{
	CharacterClass *pClass = GET_REF(pChar->hClass);
	const char *pchType = pClass ? pClass->pchDefaultPlayingStyle : NULL;
	int i;

	if (pchType)
		return pchType;

	
	for(i=eaSize(&pChar->ppPowerTrees)-1;i>=0;i--)
	{
		PowerTreeDef *pDef = GET_REF(pChar->ppPowerTrees[i]->hDef);
		if (pDef && pDef->pchDefaultPlayingStyle)
			return pDef->pchDefaultPlayingStyle;
	}
	return NULL;
}

S32 character_Find_TableTrainingLevel(Character *pChar, const char *pchTableName)
{
	return entity_Find_TableTrainingLevel(CONTAINER_NOCONST(Entity, pChar->pEntParent),CONTAINER_NOCONST(Entity, pChar->pEntParent), pchTableName); 
}

//TODO(MM): Must take into account enhancements, as well as stat points
S32 character_FindTrainingLevel(Character *pChar)
{
	return character_Find_TableTrainingLevel(pChar, "TreePoints"); 
}


bool character_IsLegalTarget(Character *pchar, EntityRef ref)
{
	int i;
	if (!pchar)
		return false;
	for (i = eaSize(&pchar->ppAITargets) - 1; i>= 0; i--)
	{
		if (pchar->ppAITargets[i]->entRef == ref)
		{
			return true;
		}
	}
	return false;
}

F32 character_GetRelativeDangerValue(Character *pchar, EntityRef ref)
{
	int i;
	if (!pchar)
		return 0.0f;
	if (!gConf.bClientDangerData)
		return 0.0f;
	for (i = eaSize(&pchar->ppAITargets) - 1; i>= 0; i--)
	{
		if (pchar->ppAITargets[i]->entRef == ref)
		{
			return pchar->ppAITargets[i]->relativeDangerVal;
		}
	}
	return 0.0f;
}

S32 character_IgnoresExternalAnimBits(Character *pchar, EntityRef erSource)
{
	return  pchar->pEntParent 
			&& pchar->pEntParent->pCritter 
			&& pchar->pEntParent->pCritter->bIgnoreExternalAnimBits 
			&& pchar->pEntParent->myRef != erSource;
}

AUTO_TRANS_HELPER;
void character_trh_GetPowerTreeVersion(ATH_ARG NOCONST(Character)* pChar, U32* puVersion, U32* puFullRespecVersion)
{
	U32 uModCount = NONNULL(pChar) ? pChar->uiPowerTreeModCount : 0;
	U32 uMask = ~0;
	uMask <<= POWER_TREE_VERSION_NUMBITS;
	if (puVersion)
		(*puVersion) = uModCount & ~uMask;
	if (puFullRespecVersion)
		(*puFullRespecVersion) = uModCount & uMask;
}

bool character_CanUseWarpPower(Character *pchar, PowerDef *ppowDef)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	Entity* pent = pchar->pEntParent;

	if (!allegiance_CanPlayerUseWarp(pent))
	{
		return false;
	}
	if (!ppowDef->bHasWarpAttrib)
	{
		return false;
	}
	else
	{
		AttribModDef *pWarpDefMod = NULL;

		while (pWarpDefMod = powerdef_GetWarpAttribMod(ppowDef, true, pWarpDefMod))
		{
			if (pWarpDefMod->offAttrib == kAttribType_WarpTo)
			{
				WarpToParams *pParams = (WarpToParams*)pWarpDefMod->pParams;
				if (pParams->bDisallowSameMapTransfer)
				{
					const char* pchMap = zmapInfoGetPublicName(NULL);
					if (pchMap == pParams->cpchMap)
					{
						return false;
					}
				}

				// MattK said: TODO: Check to see if this queue map allows the player to leave and return to it
				if (!pParams->bAllowedInQueueMap && queue_IsQueueMap(zmapInfoGetMapType(NULL)))
				{
					return false;
				}
			}
		}
	}
#endif
	return true;
}

void character_updateTacticalRequirements(Character *pchar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (g_CombatConfig.tactical.aim.peAimRequiredItemCategory)
	{
		Entity *pEnt = pchar->pEntParent;

		if (pEnt->mm.mrTactical)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			bool bFoundItem = false;
			if (pExtract)
			{
				// see if we have any of the required items equipped
				bFoundItem = inv_ent_HasAnyEquippedItemsWithCategory(	pEnt, 
																		g_CombatConfig.tactical.aim.peAimRequiredItemCategory, 
																		pExtract);
			}


			if (!bFoundItem)
			{
				mrTacticalNotifyPowersStart(pEnt->mm.mrTactical, TACTICAL_REQUIREMENTS_UID, TDF_AIM, pmTimestamp(0));
			}
			else
			{
				mrTacticalNotifyPowersStop(pEnt->mm.mrTactical, TACTICAL_REQUIREMENTS_UID, pmTimestamp(0));
			}
		}
	}
#endif
}

// returns false if the activate rules failed
int character_CheckSourceActivateRules(Character *pChar, PowerActivateRules eActivateRules)
{
	if (pChar)
	{
		bool bAllowWhileAlive = !!(eActivateRules & kPowerActivateRules_SourceAlive);
		bool bAllowWhileDead = !!(eActivateRules & kPowerActivateRules_SourceDead);
		bool bIsDead = entCheckFlag(pChar->pEntParent,ENTITYFLAG_DEAD) || (pChar->pNearDeath);

		return (!bIsDead && bAllowWhileAlive) || (bIsDead && bAllowWhileDead);
	}

	return false;
}


#include "AutoGen/Character_h_ast.c"
