#include "AttribMod.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "encounter_common.h"
#include "Entity.h"
#include "EntityMovementManager.h"
#include "PowersMovement.h"
#include "GlobalTypes.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "MemoryPool.h"

typedef struct GSLCombatDeathPrediction
{
	int						iPartition;
	EntityRef				erTarget;
	U32						uiTime;
	U32						uiApplyID;

} GSLCombatDeathPrediction;

// enabling requires gConf.bCombatDeathPrediction to be defined as well
static bool s_bDeathPredictionEnabled = true;
static bool s_bDeathPredictionDebugging = false;

AUTO_CMD_INT(s_bDeathPredictionEnabled, CombatDeathPrediction);


static GSLCombatDeathPrediction **s_eaDeathPredictionList = NULL;

MP_DEFINE(GSLCombatDeathPrediction);

#define CombatDeathPredictionDebugPrint(pchar, format,...) if(s_bDeathPredictionDebugging) combatdebug_PowersDebugPrint((pchar)->pEntParent,EPowerDebugFlags_DEATHPREDICT,format,##__VA_ARGS__)

// --------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void CombatDeathPredictionDebug(Entity *e, int bOn)
{
	s_bDeathPredictionDebugging	= !!bOn;

	if (e)
		ClientCmd_CombatDeathPredictionDebugClient(e, bOn);
}


// --------------------------------------------------------------------------------------------------------------------
bool gslCombatDeathPrediction_IsEnabled()
{
	return gConf.bCombatDeathPrediction && s_bDeathPredictionEnabled;
}

// --------------------------------------------------------------------------------------------------------------------
void gslCombatDeathPrediction_Tick()
{
	static U64 s_uiLastUpdateTime = 0;
	
	// every couple of seconds, remove old stale predictions
	if (ABS_TIME_SINCE(s_uiLastUpdateTime) > 2)
	{
		U32 uiTime = pmTimestamp(0);

		s_uiLastUpdateTime = ABS_TIME;

		FOR_EACH_IN_EARRAY(s_eaDeathPredictionList, GSLCombatDeathPrediction, pPrediction)
		{
			if (uiTime > pPrediction->uiTime && 
				(uiTime - pPrediction->uiTime)/MM_PROCESS_COUNTS_PER_SECOND > 2)
			{
				MP_FREE(GSLCombatDeathPrediction, pPrediction);
				eaRemoveFast(&s_eaDeathPredictionList, FOR_EACH_IDX(-, pPrediction));
			}
		}
		FOR_EACH_END
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gslCombatDeathPrediction_PartitionUnload(int iPartition)
{
	FOR_EACH_IN_EARRAY(s_eaDeathPredictionList, GSLCombatDeathPrediction, pPrediction)
	{
		if (pPrediction->iPartition == iPartition)
		{
			MP_FREE(GSLCombatDeathPrediction, pPrediction);
			eaRemoveFast(&s_eaDeathPredictionList, FOR_EACH_IDX(-, pPrediction));
		}
	}
	FOR_EACH_END
}

// --------------------------------------------------------------------------------------------------------------------
static void gslCombatDeathPrediction_Add(int iPartition, EntityRef erTarget, U32 uiTime, U32 uiApplyID)
{
	GSLCombatDeathPrediction *pPrediction; 

	MP_CREATE(GSLCombatDeathPrediction, 32);
	pPrediction = MP_ALLOC(GSLCombatDeathPrediction);
	if (pPrediction)
	{
		pPrediction->iPartition = iPartition;
		pPrediction->erTarget = erTarget;
		pPrediction->uiTime = uiTime;
		pPrediction->uiApplyID = uiApplyID;
		eaPush(&s_eaDeathPredictionList, pPrediction);
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gslCombatDeathPrediction_NotifyPowerCancel(int iPartition, Character *pChar, PowerActivation *pAct)
{
	if (!gConf.bCombatDeathPrediction || !s_bDeathPredictionEnabled || !entIsPlayer(pChar->pEntParent))
		return;

	FOR_EACH_IN_EARRAY(s_eaDeathPredictionList, GSLCombatDeathPrediction, pPrediction)
	{
		if (pPrediction->iPartition == iPartition && 
			pPrediction->uiApplyID == pAct->uiIDServer && 
			eaiFind(&pAct->perTargetsHit, pPrediction->erTarget) >= 0)
		{
			Entity *pEnt = entFromEntityRef(iPartition, pPrediction->erTarget);
			
			CombatDeathPredictionDebugPrint(pChar, "DeathPrediction: Power Canceled found prediction. Notifying client. "
											"EntityRef(%d) ActID(%d) TargetRef(%d) ApplyID(%d)",
											entGetRef(pChar->pEntParent), pAct->uchID, pPrediction->erTarget, pPrediction->uiApplyID);

			MP_FREE(GSLCombatDeathPrediction, pPrediction);
			eaRemoveFast(&s_eaDeathPredictionList, FOR_EACH_IDX(-, pPrediction));

			ClientCmd_PowersUnpredictCritterDeath(pChar->pEntParent, pAct->uchID);

			
			if (pEnt && pEnt->pChar)
			{	
				pEnt->pChar->uiPredictedDeathTime = 0;

				// see if there were any other predictions made for this guy, if so setup the death time
				FOR_EACH_IN_EARRAY(s_eaDeathPredictionList, GSLCombatDeathPrediction, pOther)
				{
					if (pOther->iPartition == iPartition && 
						pOther->erTarget == entGetRef(pEnt))
					{
						if (!pEnt->pChar->uiPredictedDeathTime || 
							pOther->uiTime < pEnt->pChar->uiPredictedDeathTime)
						{
							pEnt->pChar->uiPredictedDeathTime = pOther->uiTime;
						}
					}
				}
				FOR_EACH_END
			}
			
		}
	}
	FOR_EACH_END
}

// --------------------------------------------------------------------------------------------------------------------
// derived from mod_Process in CharacterAttribs.c 
// returns true if the damage would take the given character's health to or below 0
static bool gslCombatDeathPrediction_PeekProcessDamage(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	Entity *eSource;
	char *pchTrickPtr;
	AttribModDef *pmoddef;
	F32 fResist = 1.0f;
	F32 fResistTrue = 1.0f;
	F32 fImmune = 0.0f;
	F32 fAvoid = 0.f;
	F32 fBonus = 0.f;
	F32 fHp = 0.f;
	F32 fMag, fMagNoResist, fEffMag, fEffMagNoResist;

	
	assert(pmod);
	eSource = entFromEntityRef(iPartitionIdx, pmod->erSource);
	pchTrickPtr = (char*)pchar->pattrBasic;
	pmoddef = pmod->pDef;
	
	fMag = pmod->fMagnitude;
	fMagNoResist = fMag;

	// Default mod mitigation values
	character_ModGetMitigators(iPartitionIdx, pmod, pmoddef, pchar, &fResistTrue, &fResist, &fImmune, &fAvoid, NULL);

	if(fImmune > 0.0f)
	{	/// NOPE - if it is immune, just gtfo
		return false;
	}
	else if(pmoddef->eType&kModType_Magnitude)
	{
		// Scale the mag down by the resist
		fMag = fMag * fResistTrue / fResist; // fResist > 1 makes mag go down, fResist < 1 makes mag go up
	}
										
	// Apply avoidance
	if(fAvoid)
	{
		if(pmoddef->eType&kModType_Magnitude)
		{
			fMag /= 1.f + fAvoid;
		}
	}
		
	// only handling kAttribAspect_BasicAbs for now
	/*
	// Use an approximation of the damage about to be dealt
	if(eAspect==kAttribAspect_BasicFactNeg)
	{
		F32 fMax = pchar->pattrBasic->fHitPointsMax;
		fEffMag *= fMax;
		fEffMagNoResist *= fMax;
		//fMagAI *= fMax;
	}
	*/
	fEffMag = fMag;
	fEffMagNoResist = fMagNoResist;
	
	/// hmmm, make sure CharacterProcessShields doesn't change anything ?
	/// TODO: Add a PEEK value to CharacterProcessShields so it doesn't change any states
	fEffMagNoResist = fMagNoResist;

	// Check to see if the Shield attrib absorbs any damage
	if(fEffMag > 0 && eaSize(&pchar->ppModsShield))
	{
		F32 fResult = character_ProcessShields(iPartitionIdx, pchar, pmoddef->offAttrib, fEffMag, fEffMagNoResist, pmod, NULL, true, NULL);
		fMag *= fResult;
		fEffMag *= fResult;
	}

	fHp = *F32PTR_OF_ATTRIB(pchTrickPtr,kAttribType_HitPoints);
						
	/// Change to just peek don't change
	fHp -= fMag;
	return (fHp <= 0.f);
}

// --------------------------------------------------------------------------------------------------------------------
// go through pending mods and for the mods that qualify, 
void gslCombatDeathPrediction_DeathPredictionTick(int iPartitionIdx, Character *pChar)
{
	if (!gConf.bCombatDeathPrediction || !s_bDeathPredictionEnabled)
		return;

	if (!entIsAlive(pChar->pEntParent))
		return;
	
	if (pChar->uiPredictedDeathTime)
	{
		if (pChar->uiPredictedDeathTime != (U32)-1)
		{
			if (pmTimestamp(0) >= pChar->uiPredictedDeathTime)
			{
				// surprise, you're dead!
				// todo: might need to make sure the mod that processed this death prediction has triggered.
				pChar->uiPredictedDeathTime = -1;
				return;
			}
		}
		else
		{
			pChar->bKill = true;
			return;
		}
	}

	// check if we shouldn't process this for any reason
	if (entIsPlayer(pChar->pEntParent) || 
		pChar->bInvulnerable || 
		pChar->bUsingDoor ||
		pChar->bUnkillable ||
		encounter_IsDamageDisabled(iPartitionIdx))
	{
		return; 
	}

	FOR_EACH_IN_EARRAY(pChar->modArray.ppModsPending, AttribMod, pMod)
	{
		// check if the mod qualifies to be death predicted
		// check if the source is from a player
		AttribModDef *pModDef = pMod->pDef;
		Entity *eSource = NULL; 

		if (pMod->bDeathPeekCompleted ||
			pMod->erPersonal || 
			pModDef->pExprAffects || 
			!ATTRIB_DAMAGE(pModDef->offAttrib) ||
			(pModDef->offAspect != kAttribAspect_BasicAbs) ||
			(pModDef->eType & kModType_Duration) || 
			pModDef->fPeriod != 0.f)
		{
			continue;
		}

		eSource = entFromEntityRef(iPartitionIdx, pMod->erSource);
		if (!eSource || !entIsPlayer(eSource) || !eSource->pChar)
			continue;
		
		pMod->bDeathPeekCompleted = true;
		if (gslCombatDeathPrediction_PeekProcessDamage(iPartitionIdx, pMod, pChar))
		{
			// this is going to die, send the command to the client predicting the death
			U32 uiDeathTime = pmTimestamp(pMod->fTimer);
			PowerDef *pPowerDef = NULL;

			if (!pChar->uiPredictedDeathTime || uiDeathTime < pChar->uiPredictedDeathTime)
			{
				U32 uiPowerID = pMod->uiPowerID;
				Power *pPower;
				PowerRef powerRef = {0};
				PowerActivation *pAct = NULL;

				if (eSource->pChar->pPowActCurrent && 
					eSource->pChar->pPowActCurrent->uiIDServer == pMod->uiActIDServer)
				{// found the activation that started this
					pAct = eSource->pChar->pPowActCurrent;
				}
				else
				{	// we need the activation to be able to properly predict this and protect against 
					// bad predictions
					continue;
				}

				pChar->uiPredictedDeathTime = uiDeathTime;

				pPower = character_FindComboParentByDef(eSource->pChar, pModDef->pPowerDef);

				// get the true ID of the power since the one on the AttribMod uiPowerID might be the parent power
				if (pPower)
				{
					powerref_Set(&powerRef, pPower);
				}
				
				gslCombatDeathPrediction_Add(iPartitionIdx, entGetRef(pChar->pEntParent), uiDeathTime, pAct->uiIDServer);

				ClientCmd_PowersPredictCritterDeath(eSource, entGetRef(pChar->pEntParent), 
													&powerRef, (pAct) ? pAct->uchID : 0, uiDeathTime);
				REMOVE_HANDLE(powerRef.hdef);
			}
			break;
		}
	}
	FOR_EACH_END
}