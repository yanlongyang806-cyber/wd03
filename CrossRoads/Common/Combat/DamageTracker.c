/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DamageTracker.h"
#include "DamageTracker_h_ast.h"

#include "Entity.h"
#include "MemoryPool.h"

#include "entCritter.h"
#include "Character.h"
#include "CombatConfig.h"
#include "Powers.h"
#include "PowersEnums_h_ast.h"
#include "PowerAnimFX.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

MP_DEFINE(DamageTracker);
MP_DEFINE(CombatTrackerNet);

AUTO_RUN;
void InitDamageTrackerMemPools(void)
{
#ifdef GAMESERVER
	MP_CREATE(DamageTracker, 100);
	MP_CREATE(CombatTrackerNet, 100);
#endif

#ifdef GAMECLIENT
	MP_CREATE(DamageTracker, 1);
	MP_CREATE(CombatTrackerNet, 20);
#endif
}

SA_RET_NN_VALID static DamageTracker* DamageTrackerCreate(int iPartitionIdx,
													  EntityRef erOwner,
													  EntityRef erSource,
													  EntityRef erTarget,
													  F32 fDamage,
													  F32 fDamageNoResist,
													  U32 eDamageType,
													  U32 uiApplyID,
													  SA_PARAM_OP_VALID PowerDef *ppow,
													  U32 uiDefIdx,
													  SA_PARAM_OP_VALID U32 *puiFragileTypes,
													  CombatTrackerFlag eFlags)
{
	DamageTracker *pTracker; 
	Entity *eOwner;
	
	pTracker = MP_ALLOC(DamageTracker);

	eOwner = entFromEntityRef(iPartitionIdx,erOwner);
	if(eOwner && eOwner->erOwner)
	{
		erOwner = eOwner->erOwner;
		eOwner = entFromEntityRef(iPartitionIdx,erOwner);
	}
	if(!eOwner || !eOwner->pChar)
	{
		// Volume powers or deleted entity
		pTracker->erOwner = 0; 
		pTracker->iLevel = 0;
	}
	else
	{
		pTracker->erOwner = erOwner;
		pTracker->iLevel = eOwner->pChar->iLevelCombat;
	}

	pTracker->erSource = erSource;
	pTracker->erTarget = erTarget;
	pTracker->fDamage = fDamage;
	pTracker->fDamageNoResist = fDamageNoResist;
	pTracker->eDamageType = eDamageType;
	pTracker->uiApplyID = uiApplyID;
	if(ppow)
	{
		SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,ppow,pTracker->hPower);
		pTracker->uiDefIdx = uiDefIdx;
	}
	pTracker->puiFragileTypes = puiFragileTypes;
	pTracker->eFlags = eFlags;

	pTracker->uiTimestamp = timeSecondsSince2000();
	// TODO: team, sg id? 

	return pTracker;
}

// Destroys an existing tracker
void damageTrackerDestroy(DamageTracker *pTracker)
{
	REMOVE_HANDLE(pTracker->hPower);
	MP_FREE(DamageTracker,pTracker);
}

// Adds immediate damage when we know the attacker and damage type
DamageTracker *damageTracker_AddTick(int iPartitionIdx,
									 Character *pchar,
									 EntityRef erOwner,
									 EntityRef erSource,
									 EntityRef erTarget,
									 F32 fDamage,
									 F32 fDamageNoResist,
									 U32 eDamageType,
									 U32 uiApplyID,
									 PowerDef *ppow,
									 U32 uiDefIdx,
									 U32 *puiFragileTypes,
									 CombatTrackerFlag eFlags)
{
	Entity *eSource;
	DamageTracker *pTracker = NULL;

	PERFINFO_AUTO_START_FUNC();

	pTracker = DamageTrackerCreate(iPartitionIdx, erOwner, erSource, erTarget, fDamage, fDamageNoResist, eDamageType, uiApplyID, ppow, uiDefIdx, puiFragileTypes, eFlags);
	eaPush(&pchar->ppDamageTrackersTickIncoming, pTracker);

	eSource = entFromEntityRef(iPartitionIdx,erSource);
	if(eSource && eSource->pChar)
	{
		pTracker = DamageTrackerCreate(iPartitionIdx, erOwner, erSource, erTarget, fDamage, fDamageNoResist, eDamageType, uiApplyID, ppow, uiDefIdx, puiFragileTypes, eFlags);
		eaPush(&eSource->pChar->ppDamageTrackersTickOutgoing,pTracker);
	}
	
	PERFINFO_AUTO_STOP();

	return pTracker;
}

// This is called to remove damage of the type from the incoming tick list
void damageTracker_Immunity(Character *pchar, U32 eDamageType)
{
	int i;
	for(i=eaSize(&pchar->ppDamageTrackersTickIncoming)-1; i>=0; i--)
	{
		DamageTracker * dt = pchar->ppDamageTrackersTickIncoming[i];
		if( dt->eDamageType == eDamageType )
		{
			dt = eaRemove(&pchar->ppDamageTrackersTickIncoming, i);
			damageTrackerDestroy(dt);
		}
	}
}

// Free and clear the damage trackers
void damageTracker_ClearAll(Character *pchar)
{
	eaDestroyEx(&pchar->ppDamageTrackersTickIncoming,damageTrackerDestroy);
	eaDestroyEx(&pchar->ppDamageTrackersTickOutgoing,damageTrackerDestroy);
	eaDestroyEx(&pchar->ppDamageTrackers,damageTrackerDestroy);
}

// Accumulate a single DamageTracker from a tick into the Character's longterm DamageTracker data
void character_DamageTrackerAccum(int iPartitionIdx, Character *pchar, DamageTracker *pTracker, F32 fCredit)
{
	int i;
	DamageTracker *pOld = NULL;
	EntityRef erOwner = pTracker->erOwner;

	// Find existing tracker
	for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
	{
		if(erOwner == pchar->ppDamageTrackers[i]->erOwner)
		{
			pOld = pchar->ppDamageTrackers[i];
			break;
		}
	}

	if(!pOld)
	{
		pOld = DamageTrackerCreate(iPartitionIdx,erOwner,0,pTracker->erTarget,0,0,0,0,NULL,0,NULL,0);
		eaPush(&pchar->ppDamageTrackers, pOld);
	}

	pOld->fDamage += pTracker->fDamage*fCredit;
	pOld->iLevel = MAX(pOld->iLevel, pTracker->iLevel);
	pOld->uiTimestamp = MAX(pOld->uiTimestamp, pTracker->uiTimestamp);
}

// Accumulate the basic data for a damage event into the Character's longterm DamageTracker data
void character_DamageTrackerAccumEvent(int iPartitionIdx, Character *pchar, EntityRef erOwner, F32 fDamage)
{
	int i;
	DamageTracker *pOld = NULL;
	U32 uiTimestamp = timeSecondsSince2000();
	Entity *eOwner = entFromEntityRef(iPartitionIdx,erOwner);
	S32 iLevel;

	eOwner = entFromEntityRef(iPartitionIdx,erOwner);
	if(eOwner && eOwner->erOwner)
	{
		erOwner = eOwner->erOwner;
		eOwner = entFromEntityRef(iPartitionIdx,erOwner);
	}
	if(!eOwner || !eOwner->pChar)
	{
		// Volume powers or deleted entity
		erOwner = 0;
		iLevel = 0;
	}
	else
	{
		iLevel = eOwner->pChar->iLevelCombat;
	}

	// Find existing tracker
	for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
	{
		if(erOwner == pchar->ppDamageTrackers[i]->erOwner)
		{
			pOld = pchar->ppDamageTrackers[i];
			break;
		}
	}

	if(!pOld)
	{
		pOld = DamageTrackerCreate(iPartitionIdx,erOwner,0,entGetRef(pchar->pEntParent),0,0,0,0,NULL,0,NULL,0);
		eaPush(&pchar->ppDamageTrackers, pOld);
	}

	pOld->fDamage += fDamage;
	pOld->iLevel = MAX(pOld->iLevel, iLevel);
	pOld->uiTimestamp = MAX(pOld->uiTimestamp, uiTimestamp);
}

// Decay the Character's longterm damage trackers
void character_DamageTrackerDecay(Character *pchar, F32 fDecay)
{
	int i,s;
	if(g_CombatConfig.pDamageDecay
		&& (s=eaSize(&pchar->ppDamageTrackers))
		&& (fDecay > 0
			|| g_CombatConfig.pDamageDecay->bRunEveryTick))
	{
		U32 bDecay = ((g_CombatConfig.pDamageDecay->fScaleDecay * fDecay) > 0);
		U32 bDecayers = false;
		F32 fDecayableDamage = 0;
		U32 uiTimeDiscard = g_CombatConfig.pDamageDecay->uiDelayDiscard ? timeSecondsSince2000() - g_CombatConfig.pDamageDecay->uiDelayDiscard : 0;
		U32 uiTimeDecay = g_CombatConfig.pDamageDecay->uiDelayDecay ? timeSecondsSince2000() - g_CombatConfig.pDamageDecay->uiDelayDecay : 0;
		F32 fMin = pchar->pattrBasic->fHitPoints * g_CombatConfig.pDamageDecay->fPercentDiscard;
		fDecay *= g_CombatConfig.pDamageDecay->fScaleDecay;
		
		for(i=s-1; i>=0; i--)
		{
			DamageTracker *pTracker = pchar->ppDamageTrackers[i];

			// Try delay-based full discard
			if(uiTimeDiscard)
			{
				if(pTracker->uiTimestamp < uiTimeDiscard)
				{
					damageTrackerDestroy(pTracker);
					eaRemoveFast(&pchar->ppDamageTrackers,i);
					continue;
				}
				else
				{
					character_SetSleep(pchar, 1 + pTracker->uiTimestamp - uiTimeDiscard);
				}
			}

			// Try decay
			if(uiTimeDecay)
			{
				if(pTracker->uiTimestamp < uiTimeDecay)
				{
					// Try min damage full discard
					if(fMin > 0 && pTracker->fDamage < fMin)
					{
						damageTrackerDestroy(pTracker);
						eaRemoveFast(&pchar->ppDamageTrackers,i);
						continue;
					}

					if(bDecay && pTracker->fDamage > 0)
					{
						bDecayers = pTracker->bDecay = true;
						fDecayableDamage += pTracker->fDamage;
					}
				}
				else 
				{
					if(fMin > 0 && pTracker->fDamage < fMin)
					{
						character_SetSleep(pchar, 1 + pTracker->uiTimestamp - uiTimeDecay);
					}
				}
			}
		}

		if(bDecayers)
		{
			F32 fDecayRemainder = fDecayableDamage > 0 ? 1 - ((g_CombatConfig.pDamageDecay->fScaleDecay * fDecay) / fDecayableDamage) : 1;
			MAX1(fDecayRemainder,0);
			
			for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
			{
				if(pchar->ppDamageTrackers[i]->bDecay)
				{
					DamageTracker *pTracker = pchar->ppDamageTrackers[i];
					pTracker->fDamage *= fDecayRemainder;
					pTracker->bDecay = false;
				}
			}

		}
	}
}

// Marks the highest incoming damage tracker as with the Kill CombatTracker flag.
//  This is NOT the same as the other code which finds the Entity that landed the
//  killing blow, because apparently that does special stuff.
DamageTracker *damagetracker_MarkKillingBlow(Character *pchar)
{
	DamageTracker *pTrackerKill = NULL;
	F32 fKillDamage = 0;
	int i;

	for(i=eaSize(&pchar->ppDamageTrackersTickIncoming)-1; i>=0; i--)
	{
		DamageTracker  *pTracker = pchar->ppDamageTrackersTickIncoming[i];

		// Higher (non-pseudo) damage
		if(pTracker->fDamage>fKillDamage && !(pTracker->eFlags&kCombatTrackerFlag_Pseudo))
		{
			pTrackerKill = pTracker;
			fKillDamage = pTracker->fDamage;
		}
	}

	// Flag it and return
	if(pTrackerKill!=NULL)
		pTrackerKill->eFlags |= kCombatTrackerFlag_Kill;

	return pTrackerKill;
}



DamageTracker *damageTracker_GetHighestDamager( Character * pchar )
{
	DamageTracker *max=NULL;
	int i;

	for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
	{
		if( !max || (pchar->ppDamageTrackers[i]->fDamage > max->fDamage) )
			max = pchar->ppDamageTrackers[i];
	}

	return max;
}

Entity *damageTracker_GetHighestDamagerEntity( int iPartitionIdx, Character * pchar )
{
	DamageTracker *max=NULL;
	int i;

	for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
	{
		Entity *e = entFromEntityRef(iPartitionIdx, pchar->ppDamageTrackers[i]->erOwner);

		if( e && ( !max || (pchar->ppDamageTrackers[i]->fDamage > max->fDamage) ) )
			max = pchar->ppDamageTrackers[i];
	}

	return ( max ? entFromEntityRef(iPartitionIdx, max->erOwner) : NULL );
}

// An entity is PvP flagged if it's a player or if it's a PvP-flagged critter.
// These critters shouldn't cause the death penalty
static bool entIsPvPFlagged(Entity* pEnt)
{
	if(!pEnt)
		return false;

	// If the player was killed by PvP, there's no death penalty
	// TODO: revisit what qualifies as being killed by another player
	if(pEnt->pPlayer)
	{
		return true;
	}
	else if(pEnt->pCritter)
	{
		CritterDef* pCritterDef = GET_REF(pEnt->pCritter->critterDef);
		if(pCritterDef && pCritterDef->bPvPFlagged)
			return true;
	}

	return false;
}

bool damageTracker_HasBeenDamagedInPvP(int iPartitionIdx, Character *pchar)
{
	int i;

	for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
	{
		Entity *e = entFromEntityRef(iPartitionIdx, pchar->ppDamageTrackers[i]->erOwner);
		if (e && e->pPlayer || entIsPvPFlagged(e))
			return true;
	}

	return false;		
}

//NOTE:  This function will need to be expanded to handle groups
int damageTracker_FillAttackerArray(int iPartitionIdx, Character *pchar, Entity*** ents)
{
	int i;

	eaClear(ents);
	for(i=eaSize(&pchar->ppDamageTrackers)-1; i>=0; i--)
	{
		Entity *e = entFromEntityRef(iPartitionIdx, pchar->ppDamageTrackers[i]->erOwner);
		if (e && e->pPlayer)
			eaPush(ents, e);
	}

	return eaSize(ents);		
}



// Constructs a CombatTrackerNet from the given DamageTracker
CombatTrackerNet *damageTracker_BuildCombatTrackerNet(DamageTracker *pTracker, Entity* pOwner)
{
	CombatTrackerNet *pNet = StructCreate(parse_CombatTrackerNet);
	PowerDef *pdef = GET_REF(pTracker->hPower);
	PowerAnimFX* pPowerArt = SAFE_GET_REF(pdef, hFX);
	//only do this for players
	Power* pPow = pOwner && pOwner->pPlayer && pOwner->pChar ? character_FindComboParentByDef(pOwner->pChar, pdef) : NULL;
	pNet->erOwner = pTracker->erOwner;
	pNet->erSource = pTracker->erSource;
	pNet->eType = pTracker->eDamageType;
	pNet->fMagnitude = pTracker->fDamage;
	if (pPow)
	{
		while (pPow->pParentPower)
			pPow = pPow->pParentPower;
		pNet->powID = pPow->uiID;
	}
	if(pTracker->fDamageNoResist!=pTracker->fDamage)
	{
		pNet->fMagnitudeBase = pTracker->fDamageNoResist;
	}
	pNet->eFlags = pTracker->eFlags;
	if(pTracker->pchDisplayNameKey)
	{
		SET_HANDLE_FROM_STRING(gMessageDict,pTracker->pchDisplayNameKey,pNet->hDisplayName);
	}
	else if(pdef)
	{
		COPY_HANDLE(pNet->hDisplayName,pdef->msgDisplayName.hMessage);
	}
	pNet->pPowerDef = pdef;

	if (pdef && pdef->bForceHideDamageFloats)
		pNet->eFlags |= kCombatTrackerFlag_NoFloater;
	return pNet;
}




#include "DamageTracker_h_ast.c"