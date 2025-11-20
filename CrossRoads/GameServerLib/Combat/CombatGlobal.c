/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatGlobal.h"

#include "Entity.h"
#include "MemoryPool.h"
#include "timing.h"

#include "AttribMod.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_combat.h"
#include "mapstate_common.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct GlobalAttribMod
{
	AttribMod *pmod;
		// The actual AttribMod

	int iPartitionIdx;
		// The partition

} GlobalAttribMod;

typedef struct GlobalPowerActivation
{
	PowerActivation *pact;
		// The actual PowerActivation

	Vec3 vecSource;
		// The source location

	int iPartitionIdx;
		// The partition
	
	REF_TO(CharacterClass) hClass;
		// The CharacterClass to use

	S32 iLevel;
		// The level to use

	F32 fDelay;
		// The delay before actually starting

	ChargeMode eChargeMode;
		// The current charging mode

} GlobalPowerActivation;

static GlobalAttribMod **s_ppMods = NULL;
static GlobalPowerActivation **s_ppActs = NULL;

MP_DEFINE(GlobalAttribMod);
MP_DEFINE(GlobalPowerActivation);

// Adds an AttribMod to the global AttribMod list
void combat_GlobalAttribModAdd(AttribMod *pmod, int iPartitionIdx)
{
	GlobalAttribMod *pglob = NULL;
	
	MP_CREATE(GlobalAttribMod,5);
	pglob = MP_ALLOC(GlobalAttribMod);

	pglob->iPartitionIdx = iPartitionIdx;
	pglob->pmod = pmod;

	eaPush(&s_ppMods,pglob);
}

static void CombatGlobalAttribModDestroy(int i)
{
	mod_Destroy(s_ppMods[i]->pmod);
	MP_FREE(GlobalAttribMod,s_ppMods[i]);
	eaRemoveFast(&s_ppMods,i);
}

// Adds an Activation to the global Activation list
void combat_GlobalActivationAdd(PowerDef *pdef,
								Vec3 vecSource,
								int iPartitionIdx,
								EntityRef erTarget,
								Vec3 vecTarget,
								CharacterClass *pclass,
								int iLevel,
								F32 fDelay)
{
	GlobalPowerActivation *pglob = NULL;
	PowerActivation *pact = NULL; 

	MP_CREATE(GlobalPowerActivation,5);
	pglob = MP_ALLOC(GlobalPowerActivation);

	// Source data
	copyVec3(vecSource,pglob->vecSource);
	pglob->iPartitionIdx = iPartitionIdx;
	if(pclass)
	{
		SET_HANDLE_FROM_REFERENT(g_hCharacterClassDict,pclass,pglob->hClass);
	}
	pglob->iLevel = iLevel;
	pglob->fDelay = fDelay;

	// Regular activation data
	pact = poweract_Create();
	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdef,pact->hdef);
	pact->erTarget = erTarget;
	if(vecTarget)
	{
		copyVec3(vecTarget,pact->vecTarget);
		PowersDebugPrint(EPowerDebugFlags_ACTIVATE, "GlobalActivation: %s from <%.2f %.2f %.2f> to <%.2f %.2f %.2f> (%s %d)\n",
						pdef->pchName,
						vecParamsXYZ(vecSource),
						vecParamsXYZ(vecTarget),
						pclass ? pclass->pchName : "No Class",
						iLevel);
	}
	else
	{
		PowersDebugPrint(EPowerDebugFlags_ACTIVATE, "GlobalActivation: %s from <%.2f %.2f %.2f> to %d (%s %d)\n",
						pdef->pchName,
						vecParamsXYZ(vecSource),
						erTarget,
						pclass ? pclass->pchName : "No Class",
						iLevel);
	}
	pglob->pact = pact;

	if(pdef->fTimeCharge)
	{
		pglob->eChargeMode = kChargeMode_Current;
		pglob->pact->fTimeChargeRequired = pdef->fTimeCharge;
	}

	eaPush(&s_ppActs,pglob);
}

static void CombatGlobalActivationDestroy(int i)
{
	poweract_Destroy(s_ppActs[i]->pact);
	REMOVE_HANDLE(s_ppActs[i]->hClass);
	MP_FREE(GlobalPowerActivation,s_ppActs[i]);
	eaRemoveFast(&s_ppActs,i);
}

static void CombatGlobalTickMods(F32 fRate)
{
	int i, s = eaSize(&s_ppMods);
	if(s)
	{
		PERFINFO_AUTO_START_FUNC();

		for(i=s-1; i>=0; i--)
		{
			AttribMod *pmod = s_ppMods[i]->pmod;
			int iPartitionIdx = s_ppMods[i]->iPartitionIdx;

			if (mapState_IsMapPausedForPartition(iPartitionIdx)) {
				continue;
			}

			mod_ProcessSpecialUnowned(pmod,fRate,iPartitionIdx, NULL);

			if(pmod->fDuration < 0)
				CombatGlobalAttribModDestroy(i);
		}

		PERFINFO_AUTO_STOP();
	}
}

static void CombatGlobalTickActs(F32 fRate)
{
	int i, s = eaSize(&s_ppActs);
	if(s)
	{
		PERFINFO_AUTO_START_FUNC();

		for(i=s-1; i>=0; i--)
		{
			GlobalPowerActivation *pglob = s_ppActs[i];
			PowerActivation *pact = pglob->pact;
			PowerDef *pdef = GET_REF(pact->hdef);
			
			if (mapState_IsMapPausedForPartition(pglob->iPartitionIdx)) {
				continue;
			}

			// Startup delay
			if(pglob->fDelay>0)
			{
				pglob->fDelay -= fRate;
				continue;
			}

			// Charge up
			if(pglob->eChargeMode==kChargeMode_Current)
			{
				if(pact->fTimeCharged==0)
				{
					// Animate charge
					if(pdef)
					{
						PowerAnimFX *pafx = GET_REF(pdef->hFX);
						if(pafx)
						{
							Entity *eTarget = entFromEntityRef(pglob->iPartitionIdx, pact->erTarget);
							Character *pchar = eTarget ? eTarget->pChar : NULL;
							location_AnimFXChargeOn(pglob->vecSource,pglob->iPartitionIdx,pafx,pchar,pact->vecTarget,pmTimestamp(0),(U32)((U64)pglob));
						}
					}
				}

				if(pact->fTimeCharged >= pact->fTimeChargeRequired)
				{
					// Stop charging
					pglob->eChargeMode=kChargeMode_None;
				}
				else
				{
					pact->fTimeCharged += fRate; // TODO: Make this modified by SpeedCharge
				}
			}
			
			if(pglob->eChargeMode==kChargeMode_None)
			{

				// Activate!
				if(!pact->bActivated)
				{
					CharacterClass *pclass = GET_REF(pglob->hClass);
					if(pdef)
					{
						location_ApplyPowerDef(pglob->vecSource,pglob->iPartitionIdx,pdef,pact->erTarget,pact->vecTarget,NULL,NULL,pclass,pglob->iLevel,0);
					}
					pglob->pact->bActivated = true;
				}

				if(!pdef || pglob->pact->fTimeActivating > pdef->fTimeActivate)
					CombatGlobalActivationDestroy(i);
				else
					pact->fTimeActivating += fRate; // TODO: Make this modified by SpeedPeriod
			}

		}

		PERFINFO_AUTO_STOP();
	}
}

// Runs a tick for global combat data
void combat_GlobalTick(F32 fRate)
{
	CombatGlobalTickMods(fRate);
	CombatGlobalTickActs(fRate);
}

// Cleanup necessary for global combat data when a partition goes away
void combat_GlobalPartitionUnload(int iPartitionIdx)
{
	int i;
	
	for(i=eaSize(&s_ppMods)-1; i>=0; i--)
	{
		if(s_ppMods[i]->iPartitionIdx==iPartitionIdx)
			CombatGlobalAttribModDestroy(i);
	}
	
	for(i=eaSize(&s_ppActs)-1; i>=0; i--)
	{
		if(s_ppActs[i]->iPartitionIdx==iPartitionIdx)
			CombatGlobalActivationDestroy(i);
	}
}