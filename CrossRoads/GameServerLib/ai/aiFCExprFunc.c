#include "aiAnimList.h"
#include "aiDebug.h"
#include "aiExtern.h"
#include "aiFormation.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiPowers.h"
#include "aiTeam.h"
#include "aiJobs.h"

#include "AnimList_Common.h"
#include "AppLocale.h"
#include "AttribModFragility.h"
#include "Entity.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "Character_combat.h"
#include "CombatEval.h"
#include "cutscene.h"
#include "Dynfxinfo.h"
#include "Earray.h"
#include "estring.h"
#include "error.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityGrid.h"
#include "EntityLib.h"
#include "EntityMovementDragon.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslChat.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEntityNet.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "itemCommon.h"
#include "itemTransaction.h"
#include "inventoryCommon.h"
#include "MessageExpressions.h"
#include "oldencounter_common.h"
#include "gslMission.h"
#include "nemesis_common.h"
#include "Player.h"
#include "powers.h"
#include "PowerActivation.h"
#include "PowersMovement.h"
#include "timing.h"
#include "Reward.h"
#include "rand.h"
#include "Species_Common.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "aiFCExprFunc.h"
#include "gslMapVariable.h"

#include "../AILib/AutoGen/aiMovement_h_ast.h" // because FSMLDAnimList is in aiMovement.h
#include "../AILib/AutoGen/AILib_autogen_QueuedFuncs.h"
#include "AutoGen/GameServerLib_autogen_QueuedFuncs.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

// Returns the percentage of health the current entity has left
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetMyHealth, GetMyHealthPct100);
int exprFuncGetMyHealthPct100(ACMD_EXPR_SELF Entity* be)
{
	F32 health = 0;

	if ( be->pChar )
	{
		if ( be->pChar->pattrBasic->fHitPointsMax > 0 )
		{
			health = be->pChar->pattrBasic->fHitPoints / be->pChar->pattrBasic->fHitPointsMax;
			health *= 100.0f;
			health += 0.5f;
			
			return (int)health;
		}
	}
	
	return 0;
}

// Returns the percentage of health the current entity has left
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetMyHealthPoints);
int exprFuncGetMyHealthPoints(ACMD_EXPR_SELF Entity* be)
{
	F32 health = 0;

	if ( be->pChar )
	{
		health = be->pChar->pattrBasic->fHitPoints;
		return (int)health;
	}

	return 0;
}

// Sets my health to the specified value (0-hitPointsMax)
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetMyHealth);
int exprFuncSetMyHealth(ACMD_EXPR_SELF Entity *e, int health)
{
	if(e->pChar)
	{
		if(e->pChar->pattrBasic->fHitPointsMax > 0)
		{
			MAX1(health, 0);
			e->pChar->pattrBasic->fHitPoints = MIN(health, e->pChar->pattrBasic->fHitPointsMax);
			character_DirtyAttribs(e->pChar);

			return (int)e->pChar->pattrBasic->fHitPoints;
		}
	}

	return 0;
}

// Sets my health to the specified percentage (0-100)
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetMyHealthPct);
int exprFuncSetMyHealthPct(ACMD_EXPR_SELF Entity *e, F32 pct)
{
	if(e->pChar)
	{
		if(e->pChar->pattrBasic->fHitPointsMax > 0)
		{
			MAX1(pct, 0);
			pct /= 100;
			e->pChar->pattrBasic->fHitPoints = MIN(pct, 1) * e->pChar->pattrBasic->fHitPointsMax;
			character_DirtyAttribs(e->pChar);

			return (int)e->pChar->pattrBasic->fHitPoints;
		}
	}

	return 0;
}

// Forces a Character into the NearDeath state, if they're alive and not already NearDeath.
// If the timer is negative, the NearDeath state is forever.  If the timer is 0 it uses the
//  default timer for the particular Character (if the Character doesn't normally go into
//  NearDeath, the default timer is forever).  Otherwise the NearDeath state will
//  use the specified timer.
// Returns the resulting timer value (-1 for forever), or 0 if something went wrong.
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetNearDeath);
int exprFuncSetNearDeath(ACMD_EXPR_SELF Entity *e, int timeout)
{
	if(e->pChar)
		return character_NearDeathEnter(entGetPartitionIdx(e),e->pChar,timeout);

	return 0;
}

// Returns the average health of the passed in ent array as a percentage of their total health
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetHealthPct);
float exprFuncGetHealthPct(ACMD_EXPR_ENTARRAY_IN ents)
{
	U32 totalHealth = 0;
	int i, num = eaSize(ents);
	int count = 0;

	for(i = num - 1; i >= 0; i--)
	{
		Entity* be = (*ents)[i];
		F32 health;

		if(be->pChar)
		{
			health = be->pChar->pattrBasic->fHitPoints / be->pChar->pattrBasic->fHitPointsMax;
			health *= 100.0f;

			totalHealth += health;

			count++;
		}
	}

	if(count)
		return totalHealth / count;
	else
		return 0;
}

// Returns the average endurance of the passed in ent array as a percentage of their total health
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetEndurancePct);
float exprFuncGetEndurancePct(ACMD_EXPR_ENTARRAY_IN ents)
{
	U32 totalEnd = 0;
	int i, num = eaSize(ents);
	int count = 0;

	for(i = num - 1; i >= 0; i--)
	{
		Entity* be = (*ents)[i];
		F32 health;

		if(be->pChar)
		{
			health = be->pChar->pattrBasic->fPower / be->pChar->pattrBasic->fPowerMax;
			health *= 100.0f;

			totalEnd += health;

			count++;
		}
	}

	if(count)
		return totalEnd / count;
	else
		return 0;
}

// Returns the average shield percentage of the passed in ent array
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetShieldPct);
float exprFuncGetShieldPct(ACMD_EXPR_ENTARRAY_IN ents)
{
	U32 totalHealth = 0;
	int i, j, num = eaSize(ents);
	int count = 0;

	for(i = num - 1; i >= 0; i--)
	{
		Entity* be = (*ents)[i];
		F32 shieldPct;
		if(be->pChar)
		{
			AttribMod** shields = be->pChar->ppModsShield;
			F32 lowestShield = FLT_MAX;

			for(j = eaSize(&shields)-1; j >= 0; j--)
			{
				shieldPct = shields[j]->pFragility->fHealth / shields[j]->pFragility->fHealthMax; 
				shieldPct *= 100;
				totalHealth += shieldPct;

				count++;
			}
		}
	}

	if(count)
		return totalHealth / count;
	else
		return 0;
}

// Removes every entity from the passed in ent array except for the one with the lowest
// percentage health
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropLowestHealthPct);
void exprFuncEntCropLowestHealthPct(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, n = eaSize(entsInOut);
	F32 lowestHealthPct = FLT_MAX;
	Entity* lowestBE = NULL;

	for(i = 0; i < n; i++)
	{
		Entity* be = (*entsInOut)[i];
		F32 health;

		if(be->pChar)
		{
			health = be->pChar->pattrBasic->fHitPoints / be->pChar->pattrBasic->fHitPointsMax;

			if(health < lowestHealthPct)
			{
				lowestHealthPct = health;
				lowestBE = be;
			}
		}
	}

	eaSetSize(entsInOut, 0);

	if(lowestBE)
		eaPush(entsInOut, lowestBE);
}

// keeps all entity healths greater than or equal to healthPct
AUTO_EXPR_FUNC(ai, encounter_action, EntityUtil) ACMD_NAME(EntCropGreaterThanHealthPct);
void exprFuncEntCropGreaterThanHealthPct(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,F32 healthPct)
{
	int i, n = eaSize(entsInOut);
	for(i=n-1; i>=0; i--)
	{
		Entity* e = (*entsInOut)[i];
		if(e->pChar)
		{
			F32 health = e->pChar->pattrBasic->fHitPoints / e->pChar->pattrBasic->fHitPointsMax;
			if(health < healthPct)
			{
				eaRemoveFast(entsInOut,i);
			}
		}
	}
}

// exit current job
AUTO_EXPR_FUNC(ai) ACMD_NAME(EndJob);
void exprFuncEndJob(ACMD_EXPR_SELF Entity *e)
{
	AIVarsBase* aib = e->aibase;
	AIJob* job = aib->job;
	if (job)
		aib->encounterJobFSMDone = 1;
}

// keeps all entity healths less than or equal to healthPct
AUTO_EXPR_FUNC(ai, encounter_action, EntityUtil) ACMD_NAME(EntCropLessThanHealthPct);
void exprFuncEntCropLessThanHealthPct(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,F32 healthPct)
{
	int i, n = eaSize(entsInOut);
	for(i=n-1; i>=0; i--)
	{
		Entity* e = (*entsInOut)[i];
		if(e->pChar)
		{
			F32 health = e->pChar->pattrBasic->fHitPoints / e->pChar->pattrBasic->fHitPointsMax;
			if(health > healthPct)
			{
				eaRemoveFast(entsInOut,i);
			}
		}
	}
}

int sortHealthPct(const Entity **left, const Entity **right)
{
	F32 l_pct, r_pct;
	F32 h, hmax;
	aiExternGetHealth(*left, &h, &hmax);
	l_pct = h/hmax;

	aiExternGetHealth(*right, &h, &hmax);
	r_pct = h/hmax;

	return l_pct < r_pct;
}

// Sorts entities in the array by health pct, descending
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntSortByHealthPctDsc);
void exprFuncSortByHealthPctDsc(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	eaQSort(*entsInOut, sortHealthPct);
}

// Reverses an ent array... useful for sorted arrays when you want to crop from the lowest
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntArrayReverse);
void exprFuncEntReverse(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	eaReverse(entsInOut);
}

// Removes every entity from the passed in ent array except for <count>, from the beginning
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropSortedCount);
void exprFuncEntCropSortedCount(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, int count)
{
	int i;
	int n = eaSize(entsInOut);

	for(i=n-1; i>=0; i--)
	{
		if(i>=count)
			eaRemoveFast(entsInOut, i);
	}
}

// Removes non-combat entities from the ent array
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropCombat);
void exprFuncEntCropCombat(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i;

	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		if(!(*entsInOut)[i]->pChar)
			eaRemoveFast(entsInOut, i);
	}
}


static void EntCropByArcInternal(ACMD_EXPR_SELF Entity *e,
						 ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,
						 F32 fCenterAngle,
						 F32 fArc,
						 S32 bCropInsideArc)
{
	S32 i;
	Vec3 myPos;

	entGetPos(e, myPos);
	
	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Vec3 vDirToTarget;
		F32 fYaw;

		entGetPos((*entsInOut)[i], vDirToTarget);
		subVec3(vDirToTarget, myPos, vDirToTarget);
		fYaw = getVec3Yaw(vDirToTarget);

		fYaw = subAngle(fYaw, fCenterAngle);

		if (bCropInsideArc)
		{	
			if(ABS(fYaw) > fArc)
				eaRemoveFast(entsInOut, i);
		}
		else
		{	
			if(ABS(fYaw) < fArc)
				eaRemoveFast(entsInOut, i);
		}
		
	}

}
// Removes critters that are not outside the specified arc (relative to facing)
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropOutsideArc);
void exprFuncEntCropOutsideArc(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, F32 arc)
{
	Vec2 myPY;
	
	arc = RAD(arc) * 0.5f;

	entGetFacePY(e, myPY);

	EntCropByArcInternal(e, entsInOut, myPY[1], arc, false);
}

// Removes critters that are not inside the specified arc (relative to facing)
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropInsideArc);
void exprFuncEntCropInsideArc(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, F32 arc)
{
	Vec2 myPY;

	arc = RAD(arc) * 0.5f;

	entGetFacePY(e, myPY);

	EntCropByArcInternal(e, entsInOut, myPY[1], arc, true);
}


// Crops the list to only include entities who are within the specified arc from the source entity.
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropWithinOffcenterArc);
void exprFuncEntCropWithinOffcenterArc(	ACMD_EXPR_SELF Entity *e, 
										ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, 
										F32 arc, 
										F32 centerAngle)
{
	Vec2 myPY;

	arc = RAD(arc) * 0.5f;

	entGetFacePY(e, myPY);
	
	myPY[1] = addAngle(RAD(centerAngle), myPY[1]);
	
	EntCropByArcInternal(e, entsInOut, myPY[1], arc, true);
}

// crops the list to only include entities that are within the specified arc with an offset, from the entity's root rotation (not its facing)
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropOutsideRootArcWithOffset);
void exprFuncEntCropOutsideFacingArcWithOffset( ACMD_EXPR_SELF Entity *e, 
												ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, 
												F32 fArc, 
												F32 fCenterAngle)
{
	Vec3 vCurRootDir;
	Quat qRot;
	F32 fRootYaw;
	
	entGetRot(e, qRot);
	quatToMat3_2(qRot, vCurRootDir);
	getVec3YP(vCurRootDir, &fRootYaw, NULL);

	fArc = RAD(fArc) * 0.5f;

	fRootYaw = addAngle(fRootYaw, RAD(fCenterAngle));
	EntCropByArcInternal(e, entsInOut, fRootYaw, fArc, true);
}

// Removes critters that are *outside* the specified arc range relative to the front of the source entities
// If visibleToAny is set, then crop the entities that are not within the arc of any of the source entities,
// otherwise crop the entities that are not within the arc of all of the source entities.
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropInsideArcEntArray);
void exprFuncEntCropInsideArcEntArray(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, 
									  ACMD_EXPR_ENTARRAY_IN entsIn, 
									  F32 arc, 
									  bool visibleToAny)
{
	int i, j;

	arc = RAD(arc)/2;

	if(!eaSize(entsIn))
	{
		eaClearFast(entsInOut);
	}
	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Vec3 dir;
		F32 yaw;

		entGetPos((*entsInOut)[i], dir);

		for(j=eaSize(entsIn)-1; j >= 0; j--)
		{
			Vec3 entPos, entPY;
			Entity* e = (*entsIn)[j];
			entGetPos(e, entPos);
			entGetFacePY(e, entPY);

			subVec3(dir, entPos, dir);
			yaw = getVec3Yaw(dir);

			yaw = subAngle(yaw, entPY[1]);

			if(fabs(yaw)>arc)
			{
				if(!visibleToAny)
					break;
			}
			else if(visibleToAny)
			{
				break;
			}
		}
		if((visibleToAny && j < 0) || (!visibleToAny && j >= 0))
		{
			eaRemoveFast(entsInOut, i);
		}
	}
}

// removes inoutents that can't see inent(s)
// If visibleToAny is set, then crop the entities can't see any inent
// otherwise crop the entities that can't see all inents
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropCanSpotInsideArcEntArray);
void exprFuncEntCropCanSpotInsideArcEntArray(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, 
	ACMD_EXPR_ENTARRAY_IN entsIn, 
	F32 arc, 
	bool visibleToAny)
{
	int i, j;

	arc = RAD(arc)/2;

	if(!eaSize(entsIn))
	{
		eaClearFast(entsInOut);
	}
	for(i=eaSize(entsInOut)-1; i>=0; i--)
	{
		Vec3 entPos, entPY;
		F32 yaw;

		entGetPos((*entsInOut)[i], entPos);
		entGetFacePY((*entsInOut)[i], entPY);

		for(j=eaSize(entsIn)-1; j >= 0; j--)
		{
			Vec3 dir;
			Entity* e = (*entsIn)[j];
			entGetPos(e, dir);

			subVec3(dir, entPos, dir);
			yaw = getVec3Yaw(dir);

			yaw = subAngle(yaw, entPY[1]);

			if(fabs(yaw)>arc)
			{
				if(!visibleToAny)
					break;
			}
			else if(visibleToAny)
			{
				break;
			}
		}
		if((visibleToAny && j < 0) || (!visibleToAny && j >= 0))
		{
			eaRemoveFast(entsInOut, i);
		}
	}
}

// Removes every entity from the passed in ent array except for the one with the lowest
// percentage shields remaining
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropLowestShieldPct);
void exprFuncEntCropLowestShieldPct(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, j, n = eaSize(entsInOut);
	F32 lowestHealthPct = FLT_MAX;
	Entity* lowestBE = NULL;
	
	for(i = 0; i < n; i++)
	{
		Entity* be = (*entsInOut)[i];
		if(be->pChar)
		{
			AttribMod** shields = be->pChar->ppModsShield;
			F32 lowestShield = FLT_MAX;

			for(j = eaSize(&shields)-1; j >= 0; j--)
			{
				F32 shieldPct = shields[j]->pFragility->fHealth / shields[j]->pFragility->fHealthMax; 
				if(shieldPct < lowestShield)
					lowestShield = shieldPct;
			}

			if(lowestShield < lowestHealthPct)
			{
				lowestHealthPct = lowestShield;
				lowestBE = be;
			}
		}
	}

	eaSetSize(entsInOut, 0);

	if(lowestBE)
		eaPush(entsInOut, lowestBE);
}

// Removes every entity from the passed in ent array except for the one with the highest
// percentage health
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropHighestHealthPct);
void exprFuncEntCropHighestHealthPct(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut)
{
	int i, n = eaSize(entsInOut);
	F32 highestHealthPct = -FLT_MAX;
	Entity* highestBE = NULL;
	
	for(i = 0; i < n; i++)
	{
		Entity* be = (*entsInOut)[i];
		F32 health;

		if(be->pChar)
		{
			health = be->pChar->pattrBasic->fHitPoints / be->pChar->pattrBasic->fHitPointsMax;
			health *= 100.0f;

			if(health > highestHealthPct)
			{
				highestHealthPct = health;
				highestBE = be;
			}
		}
	}

	eaSetSize(entsInOut, 0);

	if(highestBE)
		eaPush(entsInOut, highestBE);
}

// Uses the power with the passed in name on the entity itself
// NOTE: the entity needs to already have this power
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(UsePowerOnSelf);
ExprFuncReturnVal exprFuncUsePowerOnSelf(ACMD_EXPR_SELF Entity* be,
							ACMD_EXPR_DICT(PowerDef) const char* powerName,
							ACMD_EXPR_ERRSTRING errStr)
{
	GameAccountDataExtract *pExtract;
	int iPartitionIdx;

	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at "
			LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(be);
	iPartitionIdx = entGetPartitionIdx(be);

	character_ActAllCancel(iPartitionIdx, be->pChar, true);
	character_ActivatePowerByNameServerBasic(iPartitionIdx, be->pChar, powerName, be, NULL, true, pExtract);

	return ExprFuncReturnFinished;
}

// Returns whether the critter can use the power of the passed in name on an ent array
// NOTE: the entity needs to already have this power
// NOTE: you can only use a power on one entity, so passing in multiple to this function will
// cause an error
AUTO_EXPR_FUNC(ai) ACMD_NAME(CanUsePowerOnEntArray);
ExprFuncReturnVal exprFuncCanUsePowerOnEntArray(ACMD_EXPR_SELF Entity* be, ExprContext* context,
								ACMD_EXPR_INT_OUT outInt,
								ACMD_EXPR_ENTARRAY_IN entsIn,
								ACMD_EXPR_DICT(PowerDef) const char* powerName,
								ACMD_EXPR_ERRSTRING errStr)
{
	AIVarsBase* aib = be->aibase;
	AIPowerInfo* powInfo;
	AIPowerConfig* powConf;

	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}
	if(eaSize(entsIn) > 1)
	{
		estrPrintf(errStr, "Can only use power on one ent at a time");
		return ExprFuncReturnError;
	}
	else if(!eaSize(entsIn)) // no ents is ok
	{
		*outInt = false;
		return ExprFuncReturnFinished;
	}

	powInfo = aiPowersFindInfo(be, aib, powerName);

	if(!powInfo)
	{
		estrPrintf(errStr, "Critter does not have power %s", powerName);
		return ExprFuncReturnError;
	}

	powConf = aiGetPowerConfig(be, aib, powInfo);

	*outInt = !!aiPowersAllowedToExecutePower(be, aib, (*entsIn)[0], powInfo, powConf, NULL);

	return ExprFuncReturnFinished;
}

// Uses the power of the passed in name on an ent array
// NOTE: the critter needs to already have this power
// NOTE: you can only use a power on one entity, so passing in multiple to this function will
// cause an error
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(UsePowerOnEntArray);
ExprFuncReturnVal exprFuncUsePowerOnEntArray(ACMD_EXPR_SELF Entity* be, ExprContext* context,
								ACMD_EXPR_ENTARRAY_IN entsIn,
								ACMD_EXPR_DICT(PowerDef) const char* powerName,
								ACMD_EXPR_ERRSTRING errStr)
{
	GameAccountDataExtract *pExtract;
	int iPartitionIdx;

	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}
	if(eaSize(entsIn) > 1)
	{
		estrPrintf(errStr, "Can only use power on one ent at a time");
		return ExprFuncReturnError;
	}
	else if(!eaSize(entsIn)) // no ents is ok
		return ExprFuncReturnFinished;

	pExtract = entity_GetCachedGameAccountDataExtract(be);
	iPartitionIdx = entGetPartitionIdx(be);

	character_ActAllCancel(iPartitionIdx, be->pChar, true);
	character_ActivatePowerByNameServerBasic(iPartitionIdx, be->pChar, powerName, (*entsIn[0]), NULL, true, pExtract);

	return ExprFuncReturnFinished;
}

// ------------------------------------------------------------------------------------------------------------------
static bool queuePowerHelper(	Entity* be, 
								ACMD_EXPR_ENTARRAY_IN entsIn, 
								const char* pchPowerName,
								SA_PARAM_NN_VALID AIPowerInfo **ppowInfoOut,
								ACMD_EXPR_ERRSTRING errStr)
{
	*ppowInfoOut = NULL;

	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return false;
	}
	if(eaSize(entsIn) > 1)
	{
		estrPrintf(errStr, "Can only use power on one ent at a time");
		return false;
	}
	else if(!eaSize(entsIn)) // no ents is ok
		return true;

	*ppowInfoOut = aiPowersFindInfo(be, be->aibase, pchPowerName);

	if(!(*ppowInfoOut))
	{
		estrPrintf(errStr, "Critter does not have power %s", pchPowerName);
		return false;
	}

	return true;
}

// Uses the power of the passed in name on an ent array
// NOTE: the critter needs to already have this power
// NOTE: you can only use a power on one entity, so passing in multiple to this function will cause an error
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(QueuePowerOnEnt);
ExprFuncReturnVal exprFuncQueuePowerOnEnt(	ACMD_EXPR_SELF Entity* be, 
											ExprContext* context,
											ACMD_EXPR_ENTARRAY_IN entsIn,
											ACMD_EXPR_DICT(PowerDef) const char* powerName,
											ACMD_EXPR_ERRSTRING errStr)
{
	AIPowerInfo* powInfo = NULL;
	
	if (!queuePowerHelper(be, entsIn, powerName, &powInfo, errStr))
	{
		return ExprFuncReturnError;
	}

	if (!powInfo)
		return ExprFuncReturnFinished;
	
	aiQueuePowerAtTime(be, be->aibase, powInfo, (*entsIn[0]), (*entsIn[0]), 0, NULL);
		
	return ExprFuncReturnFinished;
}

// Uses the power of the passed in name on the position of the entity in the entArray
// NOTE: the critter needs to already have this power
// NOTE: you can only use a power on one entity, so passing in multiple to this function will cause an error
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(QueuePowerOnEntPos);
ExprFuncReturnVal exprFuncQueuePowerOnEntPos(	ACMD_EXPR_SELF Entity* be, 
												ExprContext* context,
												ACMD_EXPR_ENTARRAY_IN entsIn,
												ACMD_EXPR_DICT(PowerDef) const char* powerName,
												ACMD_EXPR_ERRSTRING errStr)
{
	AIPowerInfo* powInfo = NULL;
	Vec3 vTargetPos;

	if (!queuePowerHelper(be, entsIn, powerName, &powInfo, errStr))
	{
		return ExprFuncReturnError;
	}

	if (!powInfo)
		return ExprFuncReturnFinished;

	entGetPos((*entsIn[0]), vTargetPos);

	aiQueuePowerTargetedPosAtTime(be, be->aibase, powInfo, vTargetPos, 0);

	return ExprFuncReturnFinished;
}


// Uses the power with the passed in name on a point
// NOTE: the entity needs to already have this power
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(UsePowerOnPoint);
ExprFuncReturnVal exprFuncUsePowerOnPoint(ACMD_EXPR_SELF Entity* be,
							 ACMD_EXPR_DICT(PowerDef) const char* powerName,
							 ACMD_EXPR_LOC_MAT4_IN matIn, ACMD_EXPR_ERRSTRING errStr)
{
	GameAccountDataExtract *pExtract;
	int iPartitionIdx;

	if(vec3IsZero(matIn[3]))
	{
		estrPrintf(errStr, "Being told to use power on (0, 0, 0), pretty sure that's not intended");
		return ExprFuncReturnError;
	}
	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(be);
	iPartitionIdx = entGetPartitionIdx(be);

	character_ActAllCancel(iPartitionIdx, be->pChar, true);
	character_ActivatePowerByNameServerBasic(iPartitionIdx, be->pChar, powerName, NULL, matIn[3], true, pExtract);

	return ExprFuncReturnFinished;
}

// Turns off the power with the passed in name when it's running on the entity itself
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(TurnOnPowerOnSelf);
ExprFuncReturnVal ExprFuncTurnOnPowerOnSelf(ACMD_EXPR_SELF Entity* be,
								ACMD_EXPR_DICT(PowerDef) const char* powerName,
								ACMD_EXPR_ERRSTRING errStr)
{
	Power* pow;
	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}

	pow = character_FindPowerByName(be->pChar, powerName);

	if(pow && !pow->bActive)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(be);

		character_ActivatePowerServerBasic(entGetPartitionIdx(be), be->pChar, pow, be, NULL, true, false, pExtract);
	}
	return ExprFuncReturnFinished;
}

// Turns off the power with the passed in name when it's running on the entity itself
AUTO_EXPR_FUNC(ai_powers) ACMD_NAME(TurnOffPowerOnSelf);
ExprFuncReturnVal ExprFuncTurnOffPowerOnSelf(ACMD_EXPR_SELF Entity* be,
								ACMD_EXPR_DICT(PowerDef) const char* powerName,
								ACMD_EXPR_ERRSTRING errStr)
{
	Power *pow;

	if(!be->pChar)
	{
		Vec3 pos;
		entGetPos(be, pos);
		estrPrintf(errStr, "A non-combat critter cannot use a power (critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}
	pow = character_FindPowerByName(be->pChar, powerName);

	aiTurnOffPower(be, pow);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(AffectedByPowerAI);
int exprFuncAffectedByPowerAI(ACMD_EXPR_SELF Entity* e,
							  ACMD_EXPR_DICT(PowerDef) const char* powerName)
{
	return AffectedByPower(e->pChar, powerName);
}

AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropAffectedByPower);
void exprFuncEntCropAffectedByPower(ACMD_EXPR_ENTARRAY_IN_OUT ents, 
									ACMD_EXPR_DICT(PowerDef) const char* powerName)
{
	int i;

	for(i=eaSize(ents)-1; i>=0; i--)
	{
		Entity* e = (*ents)[i];

		if(!AffectedByPower(e->pChar, powerName))
			eaRemoveFast(ents, i);
	}
}

// Removes all entities from the list that are not affected by the given power's attrib
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropAffectedByPowerAttrib);
void exprFuncEntCropAffectedByPowerAttrib(ACMD_EXPR_ENTARRAY_IN_OUT ents, 
									ACMD_EXPR_DICT(PowerDef) const char* powerName,
									ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	int i;

	for(i=eaSize(ents)-1; i>=0; i--)
	{
		Entity* e = (*ents)[i];

		if(!AffectedByPowerAttrib(e->pChar, powerName, attribName))
			eaRemoveFast(ents, i);
	}
}

// Removes all entities from the list that are affected by the given power's attrib
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropNotAffectedByPowerAttrib);
void exprFuncEntCropNotAffectedByPowerAttrib(ACMD_EXPR_ENTARRAY_IN_OUT ents, 
									ACMD_EXPR_DICT(PowerDef) const char* powerName,
									ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	int i;

	for(i=eaSize(ents)-1; i>=0; i--)
	{
		Entity* e = (*ents)[i];

		if(AffectedByPowerAttrib(e->pChar, powerName, attribName))
			eaRemoveFast(ents, i);
	}
}


// Removes all entities from the list that do/do not match the given power's attrib that are owned by the given entity.
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(EntCropAffectedByPowerAttribFromOwner);
void exprFuncEntCropAffectedByPowerAttribFromOwner(	ACMD_EXPR_ENTARRAY_IN_OUT ents, 
													ACMD_EXPR_DICT(PowerDef) const char* powerName,
													ACMD_EXPR_ENUM(AttribType) const char *attribName,
													SA_PARAM_NN_VALID Entity *eAttribOwner, 
													bool bRemoveEntsAffected)
{
	int i;

	bRemoveEntsAffected = !!bRemoveEntsAffected;

	for(i=eaSize(ents)-1; i>=0; i--)
	{
		Entity* e = (*ents)[i];
		S32 bIsAffected = AffectedPowerAttribFromOwner(e->pChar, powerName, attribName, eAttribOwner);
		if(bIsAffected == bRemoveEntsAffected)
			eaRemoveFast(ents, i);
	}
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropNotAffectedByPower);
void exprFuncEntCropNotAffectedByPower(ACMD_EXPR_ENTARRAY_IN_OUT ents, 
									ACMD_EXPR_DICT(PowerDef) const char* powerName)
{
	int i;

	for(i=eaSize(ents)-1; i>=0; i--)
	{
		Entity* e = (*ents)[i];

		if(AffectedByPower(e->pChar, powerName))
			eaRemoveFast(ents, i);
	}
}
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetPowerRechargeTime);
ExprFuncReturnVal exprFuncGetPowerRechargeTime(ACMD_EXPR_SELF Entity *e,
											   ACMD_EXPR_FLOAT_OUT rechargeOut, 
											   ACMD_EXPR_DICT(PowerDef) const char* powerName,
											   ACMD_EXPR_ERRSTRING errStr)
{
	Power *pow;

	if(!e->pChar)
	{
		Vec3 pos;
		entGetPos(e, pos);
		estrPrintf(errStr, "A non-combat critter cannot check power recharge time(critter at " LOC_PRINTF_STR ")", vecParamsXYZ(pos));
		return ExprFuncReturnError;
	}

	pow = character_FindPowerByName(e->pChar, powerName);

	if(pow)
	{
		*rechargeOut = pow->fTimeRecharge;
	}

	return ExprFuncReturnFinished;
}

// Kills the current entity, and leaves the body around for <secToLinger>. Whether anyone receives
// credit for the kill can be controlled by passing in <giveRewards>.  Will fade out unless <dontfade> is true. 
// Also note that any expression functions like "PlayFX()" will not behave consistently if you kill the critter 
// they affect.
AUTO_EXPR_FUNC(ai) ACMD_NAME(Kill);
void exprFuncKill(ACMD_EXPR_SELF Entity* be, F32 secToLinger, int giveRewards, int giveKillCredit, int dontFade)
{
	if(dontFade)
		entSetCodeFlagBits(be, ENTITYFLAG_DONOTFADE);
	if(be->pChar)
	{
		be->pChar->bKill = true;
		be->pChar->bUseDeathOverrides = true;
		be->pChar->fTimeToLingerOverride = secToLinger;
		be->pChar->bGiveRewardsOverride = !!giveRewards;
		be->pChar->bGiveEventCreditOverride = !!giveKillCredit;
		character_Wake(be->pChar);
	}
	else
		entDie(be, secToLinger, giveRewards, giveKillCredit, NULL); 
}

// Kills all of the current entity's pets. See exprFuncKill.
AUTO_EXPR_FUNC(ai) ACMD_NAME(KillPets);
void exprFuncKillPets(ACMD_EXPR_SELF Entity* be, F32 secToLinger, int giveRewards, int giveKillCredit, int dontFade)
{
	AITeam* team = aiTeamGetAmbientTeam(be, be->aibase);

	if (team)
	{
		int i;
		for(i=0; i<eaSize(&team->members); i++)
		{
			Entity* e = team->members[i]->memberBE;
			if (e && e->erOwner == entGetRef(be))
			{
				exprFuncKill(e, secToLinger, giveRewards, giveKillCredit, dontFade);
			}
		}
	}
}

// Plays the specified music event for every entity within proximity range
AUTO_EXPR_FUNC(ai, mission) ACMD_NAME(PlayMusic);
void exprFuncPlayMusic(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, const char* soundName)
{
	int i;
	EntityRef beRef;

	beRef = entGetRef(be);

	// Play the music for this entity, if it's a player
	if(be->pPlayer)
	{
		Vec3 entityPos;
		entGetPos(be, entityPos);
		mechanics_playMusicAtLocation(iPartitionIdx, entityPos, soundName, exprContextGetBlameFile(context));
	}

	// Play for all other entities in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer)
			ClientCmd_sndPlayMusic(be->aibase->proxEnts[i].e, soundName, exprContextGetBlameFile(context), be->myRef);
}

// Resets all music for all entities within proximity range
AUTO_EXPR_FUNC(ai, mission) ACMD_NAME(ClearMusic);
void exprFuncClearMusic(ACMD_EXPR_SELF Entity *be, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx)
{
	int i;
	EntityRef beRef;

	beRef = entGetRef(be);

	// Clear for this entity, if it's a player
	if(be->pPlayer)
	{
		Vec3 entityPos;
		entGetPos(be, entityPos);
		mechanics_clearMusicAtLocation(iPartitionIdx, entityPos);
	}

	// Clear for all other entities in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer)
			ClientCmd_sndClearMusic(be->aibase->proxEnts[i].e);
}

// Replaces the currently playing music with the passed in music event for all entities
// within proximity range
AUTO_EXPR_FUNC(ai, mission) ACMD_NAME(ReplaceMusic);
void exprFuncReplaceMusic(ACMD_EXPR_SELF Entity *be, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, const char* soundName)
{
	int i;
	EntityRef beRef;

	beRef = entGetRef(be);

	// Replace the music for this entity, if it's a player
	if(be->pPlayer)
	{
		Vec3 entityPos;
		entGetPos(be, entityPos);
		mechanics_replaceMusicAtLocation(iPartitionIdx, entityPos, soundName, exprContextGetBlameFile(context));
	}

	// Replace for all other entities in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer)
			ClientCmd_sndReplaceMusic(be->aibase->proxEnts[i].e, soundName, exprContextGetBlameFile(context), be->myRef);
}

// Ends music for all entities within proximity range
AUTO_EXPR_FUNC(ai, mission) ACMD_NAME(EndMusic);
void exprFuncEndMusic(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_PARTITION iPartitionIdx)
{
	int i;
	EntityRef beRef;
	
	beRef = entGetRef(be);

	// End music for this entity, if it's a player
	if(be->pPlayer)
	{
		Vec3 entityPos;
		entGetPos(be, entityPos);
		mechanics_endMusicAtLocation(iPartitionIdx, entityPos);
	}

	// End for all other entities in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer)
			ClientCmd_sndEndMusic(be->aibase->proxEnts[i].e);
}

// Plays a voice-set sound to all players within proximity range
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(PlayVoiceSetSound);
void exprFuncPlayVoiceSetSound(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* soundName)
{
	static Entity** eaCutscenePlayers = NULL;
	int i;
	Vec3 pos;
	entGetPos(be, pos);

	if(!soundName[0])
		return;

	// Play sound for all cutscene players
	eaClearFast(&eaCutscenePlayers);
	cutscene_GetPlayersInCutscenesNearCritter(be, &eaCutscenePlayers);
	for(i = eaSize(&eaCutscenePlayers) - 1; i >= 0; i--)
		if(eaCutscenePlayers[i]->pPlayer)
			ClientCmd_playVoiceSetSound(eaCutscenePlayers[i], soundName, exprContextGetBlameFile(context), be->myRef);

	// Try to play the sound for all other players in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer && (eaFind(&eaCutscenePlayers, be->aibase->proxEnts[i].e)==-1))
			ClientCmd_playVoiceSetSound(be->aibase->proxEnts[i].e, soundName, exprContextGetBlameFile(context), be->myRef);
}


// Plays the specified music event for the active player
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(PlayMusicEvent);
void exprFuncPlayMusicEvent(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, const char* soundName)
{
	if(!soundName[0])
		return;

	// Play the music for this entity, if it's a player
	if(be && be->pPlayer)
	{
		Vec3 entityPos;
		entGetPos(be, entityPos);
		mechanics_playMusicAtLocation(iPartitionIdx, entityPos, soundName, exprContextGetBlameFile(context));
	}
}


// Plays a one shot sound to all players within proximity range
// TODO: non-AIs should actually use PlayOneShotSoundAtPoint and this should be removed from the player context
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(PlayOneShotSound);
void exprFuncPlayOneShotSound(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, const char* soundName)
{
	static Entity** eaCutscenePlayers = NULL;
	int i;
	Vec3 pos;
	entGetPos(be, pos);

	if(!soundName[0])
		return;

	// Play the sound for this entity, if it's a player
	// This works because if the player is playing a sound, his prox ents will not exist 
	// And obviously if a critter is playing a sound, it won’t have a player pointer.  
	if(be->pPlayer)
	{
		Vec3 entityPos;
		entGetPos(be, entityPos);
		mechanics_playOneShotSoundAtLocation(iPartitionIdx, entityPos, NULL, soundName, exprContextGetBlameFile(context));
	}

	// Play sound for all cutscene players
	eaClearFast(&eaCutscenePlayers);
	cutscene_GetPlayersInCutscenesNearCritter(be, &eaCutscenePlayers);
	for(i = eaSize(&eaCutscenePlayers) - 1; i >= 0; i--)
		if(eaCutscenePlayers[i]->pPlayer)
			ClientCmd_sndPlayRemote3dV2(eaCutscenePlayers[i], soundName, pos[0], pos[1], pos[2], exprContextGetBlameFile(context), be->myRef);

	// Try to play the sound for all other players in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer && (eaFind(&eaCutscenePlayers, be->aibase->proxEnts[i].e)==-1))
			ClientCmd_sndPlayRemote3dV2(be->aibase->proxEnts[i].e, soundName, pos[0], pos[1], pos[2], exprContextGetBlameFile(context), be->myRef);
}



// Plays a one shot sound to all players within proximity range (keeps the sound's position at the source entity)
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(PlayOneShotSoundFromEntity);
void exprFuncPlayOneShotSoundFromEntity(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* soundName)
{
	static Entity** eaCutscenePlayers = NULL;
	int i;
	Vec3 pos;
	entGetPos(be, pos);

	if(!soundName[0])
		return;

	// Play the sound for this entity, if it's a player
	// This works because if the player is playing a sound, his prox ents will not exist 
	// And obviously if a critter is playing a sound, it won’t have a player pointer.  
	if(be->pPlayer)
	{
		mechanics_playOneShotSoundFromEntity(entGetPartitionIdx(be), be, soundName, exprContextGetBlameFile(context));
	}

	// Play sound for all cutscene players
	eaClearFast(&eaCutscenePlayers);
	cutscene_GetPlayersInCutscenesNearCritter(be, &eaCutscenePlayers);
	for(i = eaSize(&eaCutscenePlayers) - 1; i >= 0; i--)
		if(eaCutscenePlayers[i]->pPlayer)
			ClientCmd_sndPlayRemote3dFromEntity(eaCutscenePlayers[i], soundName, be->myRef, exprContextGetBlameFile(context));
	

	// Try to play the sound for all other players in range
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
		if(be->aibase->proxEnts[i].e->pPlayer && (eaFind(&eaCutscenePlayers, be->aibase->proxEnts[i].e)==-1))
			ClientCmd_sndPlayRemote3dFromEntity(be->aibase->proxEnts[i].e, soundName, be->myRef, exprContextGetBlameFile(context));
}


// Plays a sound to all players in the specified ent array
// NOTE: it's generally a good idea to save the ent array you're playing a sound on so you can pass
// it in to StopSound() later
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(PlaySound);
void exprFuncPlaySound(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ENTARRAY_IN entsIn, const char* soundName)
{
	int i;
	Vec3 pos;
	entGetPos(be, pos);

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* target = (*entsIn)[i];
		if(target->pPlayer)
			ClientCmd_sndPlayRemote3dV2(target, soundName, pos[0], pos[1], pos[2], exprContextGetBlameFile(context), be->myRef);
	}
}

// Ends a sound played with PlaySound() for all players in the passed in ent array
AUTO_EXPR_FUNC(ai, encounter_action, OpenMission) ACMD_NAME(StopSound);
void exprFuncStopSound(ACMD_EXPR_ENTARRAY_IN entsIn, const char* soundName)
{
	int i;

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* target = (*entsIn)[i];
		if(target->pPlayer)
			ClientCmd_sndStopOneShot(target, soundName);
	}
}

// Grants the current entity the passed in amount of XP (if it's a player)
AUTO_EXPR_FUNC(player) ACMD_NAME(GrantXP);
int exprFuncGrantXP(ExprContext* context, int iValue)
{
	ErrorFilenamef(exprContextGetBlameFile(context), "GrantXP: deprecated");

	return 1;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(RemoveItem);
ExprFuncReturnVal exprFuncRemoveItem(ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_RES_DICT(ItemDef) const char *itemName, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	ItemDef *itemDef = item_DefFromName(itemName);

	if(!itemDef)
		return ExprFuncReturnFinished;

	if(itemDef->eType==kItemType_Numeric)
	{
		estrPrintf(errString, "RemoveItem does not support numerics - attempted to remove %s", itemName);
		return ExprFuncReturnError;		
	}

	for(i=0; i<eaSize(ents); i++)
		itemtransaction_RemoveItemFromBag((*ents)[i], InvBagIDs_None, itemDef, -1, 0, -1, NULL, NULL, NULL);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(RemoveItemCount);
ExprFuncReturnVal exprFuncRemoveItemCount(ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_RES_DICT(ItemDef) const char *itemName, int count, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	ItemDef *itemDef = item_DefFromName(itemName);

	if(!itemDef)
		return ExprFuncReturnFinished;

	if(itemDef->eType==kItemType_Numeric)
	{
		estrPrintf(errString, "RemoveItem does not support numerics - attempted to remove %s", itemName);
		return ExprFuncReturnError;		
	}

	for(i=0; i<eaSize(ents); i++)
		itemtransaction_RemoveItemFromBag((*ents)[i], InvBagIDs_None, itemDef, -1, 0, count, NULL, NULL, NULL);

	return ExprFuncReturnFinished;
}

// If the index is valid, that value is used for the split (if any).
// If the index is out of range, it picks a random index.
// Returns the index that was used.
static int exprSplitText(char **pestr, const char *pText, int index)
{
	const char *ptr = pText;
	int count = 0;
	int iBrackets = 0;

	// Count the vertical bars in the text
	// Ignore things inside {} because |'s are also used for string formatting code
	while (ptr && *ptr)
	{
		if (*ptr == '{'){
			++iBrackets;
		} else if (*ptr == '}'){
			--iBrackets;
		} else if (*ptr == '|' && iBrackets == 0){
			++count;
		}
		++ptr;
	}
	
	// Pick a random entry if index is not legal
	if ((index < 0) || (index > count))
		index = randInt(count+1);

	// Skip leading spaces, new lines and tabs
	while (pText && (*pText == ' ' || *pText == '\n' || *pText == '\r' || *pText == '\t'))
		pText++;

	// Copy the appropriate substring into the estring.
	count = 0;
	ptr = pText;
	while (ptr && *ptr)
	{
		if (*ptr == '{'){
			++iBrackets;
		} else if (*ptr == '}'){
			--iBrackets;
		} else if (*ptr == '|' && iBrackets == 0){
			++count;
			if(count > index)
				break;

			// Skip leading spaces, new lines and tabs
			pText = ptr+1;
			while (*pText == ' ' || *pText == '\n' || *pText == '\r' || *pText == '\t')
				pText++;
		}
		++ptr;
	}

	// Remove trailing spaces, new lines and tabs
	--ptr;
	while (ptr && ptr > pText && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t'))
		--ptr;
	++ptr;

	estrConcat(pestr, pText, (ptr - pText));

	return index;
}

// This is called by the actual implementation
void aiSayMessageInternal(ACMD_EXPR_SELF Entity* be, Entity *target, ExprContext* context,
							const char* messageKey, const char* pChatBubbleName, F32 duration)
{
	static Entity** nearbyPlayers = NULL;
	static Entity** cutscenePlayers = NULL;
	static char *ppchTranslatedEstr[LANGUAGE_MAX] = {0};  // cached string for each language, before formatting
	static char *estrDest = NULL;
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, messageKey);
	Entity *pRandomPlayer = NULL;   // a random nearby player
	Entity *pClosestPlayer = NULL;  // the closest nearby player
	Entity *pMapOwner = NULL;
	Entity *pSpawningPlayer = NULL;
	Entity *pNemesis = NULL;  // The Nemesis to use for this message
	EntityRef beRef = 0;
	F32 fMinDistSq = -1.0f;
	F32 fMaxSayMessageDistanceSquared = 0.f;
	Vec3 entPos;
	Vec3 playerPos;
	int splitIndex = -1;
	int langID = -1;
	int i, n;
	AIConfig *pAIConfig = NULL;
	int iPartitionIdx;

	if(!be || !pMessage)
		return;

	// Get the AI config for this entity
	pAIConfig = aiGetConfig(be, be->aibase);

	if (pAIConfig)
		fMaxSayMessageDistanceSquared = pAIConfig->sayMessageDistance;
	fMaxSayMessageDistanceSquared *= fMaxSayMessageDistanceSquared;

	if (!pChatBubbleName)
		pChatBubbleName = entity_GetChatBubbleDefName(be);

	eaClearFast(&nearbyPlayers);
	eaClearFast(&cutscenePlayers);
	for (i = 0; i < LANGUAGE_MAX; i++){
		estrClear(&(ppchTranslatedEstr[i]));
	}

	entGetPos(be, entPos);

	beRef = entGetRef(be);
	aiUpdateProxEnts(be, be->aibase);
	for(i = be->aibase->proxEntsCount - 1; i >= 0; i--)
	{
		if(be->aibase->proxEnts[i].e->pPlayer &&
			(fMaxSayMessageDistanceSquared <= 0.f || (fMaxSayMessageDistanceSquared > 0.f && be->aibase->proxEnts[i].maxDistSQR <= fMaxSayMessageDistanceSquared)))
		{
			eaPush(&nearbyPlayers, be->aibase->proxEnts[i].e);
		}
	}

	// Prioritize non-cutscene players for the random nearby player
	n = eaSize(&nearbyPlayers);
	if (n){
		pRandomPlayer = eaGet(&nearbyPlayers, randInt(n));
	}

	// TODO: Very ugly hack, pending Raoul/James discussion about the best way to handle this.
	// A better way to make the change here would be to return or maintain a list of players
	// whose cutscenes are within the AI's perception radius.
	// Also send text to players watching this entity in a cutscene
	// Since this is a hack, doesn't check that the player's cutscene is actually close to the entity
	iPartitionIdx = entGetPartitionIdx(be);
	cutscene_GetPlayersInCutscenes(&cutscenePlayers, iPartitionIdx);
	n = eaSize(&cutscenePlayers);
	for(i=n-1; i>=0; --i)
	{
		Entity* pEnt = cutscenePlayers[i];
		if(pEnt && (-1 == eaFind(&nearbyPlayers, pEnt))){
			eaPush(&nearbyPlayers, pEnt);
		}
	}

	// If a random player couldn't be found among the nearby players, use a player from a cutscene
	n = eaSize(&nearbyPlayers);
	if (n && !pRandomPlayer){
		pRandomPlayer = eaGet(&nearbyPlayers, randInt(n));
	}

	// Find the closest player
	for (i = 0; i < n; i++){
		F32 distSq;
		entGetPos(nearbyPlayers[i], playerPos);
		distSq = distance3Squared(playerPos, entPos);
		if (fMinDistSq < 0 || distSq < fMinDistSq){
			fMinDistSq = distSq;
			pClosestPlayer = nearbyPlayers[i];
		}
	}

	// Find the map owner
	pMapOwner = partition_GetPlayerMapOwner(iPartitionIdx);

	// Find the Spawning Player
	if (be && be->pCritter && be->pCritter->spawningPlayer){
		pSpawningPlayer = entFromEntityRef(iPartitionIdx, be->pCritter->spawningPlayer);
	}

	// Pick a Nemesis to use
	// If this critter is a Nemesis or Nemesis Minion, use that Nemesis
	// If there is a Target, Map Owner, or Spawning Player, use that player's Nemesis
	if (be && be->pCritter && GET_REF(be->pCritter->hSavedPet)){
		pNemesis = GET_REF(be->pCritter->hSavedPet);
	} else if (be && be->pCritter && GET_REF(be->pCritter->hSavedPetOwner)){
		pNemesis = GET_REF(be->pCritter->hSavedPetOwner);
	} else if (target){
		pNemesis = player_GetPrimaryNemesis(target);
	} else if (pMapOwner){
		pNemesis = player_GetPrimaryNemesis(entFromContainerID(iPartitionIdx,entGetType(pMapOwner),entGetContainerID(pMapOwner)));
	} else if (pSpawningPlayer){
		pNemesis = player_GetPrimaryNemesis(pSpawningPlayer);
	}

	// Say the message to all nearby players in their language
	for(i=0; i<n; i++)
	{
		langID = entGetLanguage(nearbyPlayers[i]);

		if (langID < 0 || langID >= LANGUAGE_MAX){
			continue;
		}

		// Translate message and split based on '|' characters only if this language hasn't been translated yet
		if (!ppchTranslatedEstr[langID] || !ppchTranslatedEstr[langID][0]){
			const char *pchText = langTranslateMessage(langID, pMessage);

			// Split text based on '|' characters
			if (pchText){
				// If all languages have the same number of '|'-delimited blocks, this will return
				// the same block for all languages.  If not, it might be random.
				splitIndex = exprSplitText(&(ppchTranslatedEstr[langID]), pchText, splitIndex);
			}
		}

		if (ppchTranslatedEstr[langID] && ppchTranslatedEstr[langID][0])
		{
			ChatUserInfo *pInfo = ServerChat_CreateLocalizedUserInfoFromEnt(be, nearbyPlayers[i]);
			WorldVariable **eaMapVars = NULL;
			estrClear(&estrDest);
			eaCreate(&eaMapVars);

			mapvariable_GetAllAsWorldVarsNoCopy(iPartitionIdx, &eaMapVars);

			// Format text
			langFormatGameString(langID, &estrDest, ppchTranslatedEstr[langID], 
				STRFMT_ENTITY_KEY("Entity", target ? target : nearbyPlayers[i]), 
				STRFMT_ENTITY_KEY("RandomPlayer", pRandomPlayer), 
				STRFMT_ENTITY_KEY("ClosestPlayer", pClosestPlayer), 
				STRFMT_ENTITY_KEY("MapOwner", pMapOwner), 
				STRFMT_ENTITY_KEY("Nemesis", pNemesis), 
				STRFMT_MAPVARS(eaMapVars),
				STRFMT_CRITTER(be), 
				STRFMT_END);

			eaDestroy(&eaMapVars);

			if(pChatBubbleName)
				ClientCmd_EntSayMsgWithBubble(nearbyPlayers[i], pInfo, duration, pChatBubbleName, estrDest);
			else
				ClientCmd_EntSayMsg(nearbyPlayers[i], pInfo, duration, estrDest);

			StructDestroy(parse_ChatUserInfo, pInfo);
		}
	}
}

void aiSayExternMessageVar(Entity *e, ExprContext* context,
						   const char* category, const char* name, F32 duration)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_STRING, &answer, NULL);

	if(retval != ExprFuncReturnFinished)
		return;

	if(!answer.str[0])
		return;

#ifdef GAMESERVER
	aiSayMessageInternal(e, NULL, context, answer.str, NULL, duration);
#endif
}

const char* aiGetExternMessageVar(Entity *e, ExprContext* context, const char *category, const char* name)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_STRING, &answer, NULL);

	if(retval != ExprFuncReturnFinished)
		return NULL;

	if(!answer.str[0])
		return NULL;

	return answer.str;
}

const char *entGenderStr(Entity *e)
{
	Gender eGender = SAFE_MEMBER(e, eGender);
	if (eGender == Gender_Unknown && SAFE_MEMBER(e, pChar))
	{
		SpeciesDef *pSpecies = GET_REF(e->pChar->hSpecies);
		if (pSpecies)
			eGender = pSpecies->eGender;
	}
	return StaticDefineIntRevLookupNonNull(GenderEnum, eGender);
}

void aiSayVoiceMessage(Entity *e, ExprContext* context, const char* msg_suffix)
{
	static char *estr = NULL;

	const char* msg_var = aiGetExternMessageVar(e, context, "encounter", "VoiceGroup");
	const char* gen_str;

	if(!msg_suffix || !msg_suffix[0])
		return;

	if(!msg_var || !msg_var[0])
		return;

	gen_str = entGenderStr(e);

	if(!gen_str || !gen_str[0])
		return;

	estrPrintf(&estr, "%s_%s_%s",	msg_var, 
									gen_str,
									msg_suffix);

	aiSayMessageInternal(e, NULL, context, estr, NULL, 3);
}

// Sends an "FSMPoke" Event that references the specified entities
AUTO_EXPR_FUNC(entity) ACMD_NAME(SendPokeEvent);
void exprFuncSendPokeEvent(ACMD_EXPR_SELF Entity* pEnt, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char *pchMessage)
{
	eventsend_RecordPoke(iPartitionIdx, pEnt, *ents, pchMessage);
}

// Sends an "FSMPoke" Event with no source entity
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(SendPokeEventNoSource);
void exprFuncSendPokeEventNoSource(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char *pchMessage)
{
	eventsend_RecordPoke(iPartitionIdx, NULL, *ents, pchMessage);
}

// Sends an "FSMPoke" Event with no source or target entities
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(SendPokeEventEmpty);
void exprFuncSendPokeEventEmpty(ACMD_EXPR_PARTITION iPartitionIdx, const char *pchMessage)
{
	eventsend_RecordPoke(iPartitionIdx, NULL, NULL, pchMessage);
}

// Sets an override for the ContactDef on a Critter
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetContactOverride);
void exprFuncSetContactOverride(ACMD_EXPR_SELF Entity *pEnt, const char* pchContactDefName)
{
	if (pEnt && pEnt->pCritter)
	{
		if (!gConf.bAllowOldEncounterData || !pEnt->pCritter->encounterData.parentEncounter)
		{
			Errorf("SetContactOverride expression function called on an Encounter2 encounter.  This is not allowed.");
		} 
		else if (pchContactDefName && RefSystem_ReferentFromString("Contact", pchContactDefName))
		{
			SET_HANDLE_FROM_STRING("Contact", pchContactDefName, pEnt->pCritter->encounterData.hContactDefOverride);
			pEnt->pCritter->bIsInteractable = true;
		}
	}
}

// Removes any ContactDef override from a Critter, resetting it to its original ContactDef
AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearContactOverride);
void exprFuncClearContactOverride(ACMD_EXPR_SELF Entity *pEnt)
{
	if (pEnt && pEnt->pCritter)
	{
		if (!gConf.bAllowOldEncounterData || !pEnt->pCritter->encounterData.parentEncounter)
		{
			Errorf("ClearContactOverride expression function called on an Encounter2 encounter.  This is not allowed.");
		}
		else
		{
			CritterDef *pDef = GET_REF(pEnt->pCritter->critterDef);
			OldActor *pActor = pEnt->pCritter->encounterData.sourceActor;

			REMOVE_HANDLE(pEnt->pCritter->encounterData.hContactDefOverride);

			if (GET_REF(pDef->hInteractionDef) || critterdef_HasOldInteractProps(pDef) || oldencounter_HasInteractionProperties(pActor)){
				pEnt->pCritter->bIsInteractable = true;
			} else {
				pEnt->pCritter->bIsInteractable = false;
			}
		}
	}
}

AUTO_COMMAND_QUEUED();
void aiAnimListExitHandler(ACMD_POINTER FSMLDAnimList* pAnimList)
{
	if (pAnimList)
	{
		CommandQueue_ExecuteAllCommands(pAnimList->animListCommandQueue);
		CommandQueue_Destroy(pAnimList->animListCommandQueue);
		pAnimList->animListCommandQueue = NULL;
	}
}

// Plays the specified anim list on the current entity
// Can be specified as either OnEntry/OnFirstEntry or in the Action, and will play until the
// state switches
AUTO_EXPR_FUNC(ai) ACMD_NAME(AnimListSet);
void exprFuncAnimListSet(ACMD_EXPR_SELF Entity* be, ExprContext* context,
						 ACMD_EXPR_DICT(AIAnimList) const char* animList)
{
	FSMContext* fsmContext = NULL;
	FSMStateTrackerEntry* tracker = NULL;
	FSMLDAnimList* mydata = NULL;
	U64 key = PTR_TO_UINT(exprContextGetCurExpression(context));
	AIAnimList *pAnimList = NULL;
	
	animList = (animList && animList[0]) ? allocFindString(animList) : NULL;
	pAnimList = animList ? RefSystem_ReferentFromString(g_AnimListDict, animList) : NULL;

	mydata = getMyDataIfExists(context, parse_FSMLDAnimList, key);
		
	if(!pAnimList)
	{	// check if we already have an animList set, if we do we need to clear it
		if (!mydata)
			return;
	}
	
	fsmContext = exprContextGetFSMContext(context);
	tracker = fsmContext->curTracker;

	if(!tracker->exitHandlers)
		tracker->exitHandlers = CommandQueue_Create(16, false);

	if (!mydata)
	{
		mydata = getMyData(context, parse_FSMLDAnimList, key);
		QueuedCommand_aiAnimListExitHandler(tracker->exitHandlers, mydata);
		QueuedCommand_deleteMyData(tracker->exitHandlers, context, parse_FSMLDAnimList, &tracker->localData, key);
	}

	if(mydata->addedAnimList)
	{	// if we've already set the animList, check if we are changing it
		if (mydata->pchCurrentAnimList != animList)
		{
			CommandQueue_ExecuteAllCommands(mydata->animListCommandQueue);
			mydata = getMyData(context, parse_FSMLDAnimList, key);
			mydata->addedAnimList = false;
		}
	}

	if(!mydata->addedAnimList)
	{
		mydata->addedAnimList = true;
		mydata->pchCurrentAnimList = animList;
		aiAnimListSet(be, pAnimList, &mydata->animListCommandQueue);
	}
}

AUTO_COMMAND;
void PlayerAnimListSet(Entity* be, ACMD_NAMELIST("AIAnimList", REFDICTIONARY) const char* animList)
{
	AIAnimList *pAnimList = RefSystem_ReferentFromString(g_AnimListDict, animList);

	if(!pAnimList)
		return;

	if (be && be->pPlayer && be->pPlayer->InteractStatus.pEndInteractCommandQueue){
		CommandQueue_ExecuteAllCommands(be->pPlayer->InteractStatus.pEndInteractCommandQueue);
	}

	if (be && be->pPlayer)
		aiAnimListSet(be, pAnimList, &be->pPlayer->InteractStatus.pEndInteractCommandQueue);
}

AUTO_COMMAND;
void PlayerAnimListClear(Entity* be)
{
	if (be && be->pPlayer && be->pPlayer->pInteractInfo && be->pPlayer->InteractStatus.pEndInteractCommandQueue){
		CommandQueue_ExecuteAllCommands(be->pPlayer->InteractStatus.pEndInteractCommandQueue);
	}
}

// Turns the invulnerable flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetInvulnerable);
void exprFuncSetInvulnerable(ACMD_EXPR_SELF Entity* e, S32 on, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDGenericSetData *ldFakeCombat = getMyDataFromDataIfExists(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_FAKECOMBAT_KEY);
	FSMLDGenericSetData *ldInvulnerable = getMyDataFromData(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_INVULNERABLE_KEY);

	if(on)
	{
		if(!ldInvulnerable->setData)
		{
			if(SAFE_MEMBER(ldFakeCombat, setData))
				estrPrintf(errString, "Attempting to set Invulnerable when in FakeCombat.  This is not what you want.  These settings overlap.");
			ldInvulnerable->setData = true;
		}
	}
	else
		ldInvulnerable->setData = false;

	if(e->pChar)
	{
		e->pChar->bInvulnerable = !!on;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

// Turns the untargetable flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetUntargetable);
ExprFuncReturnVal exprFuncSetUntargetable(ACMD_EXPR_SELF Entity* e, ExprContext *context, S32 on, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDGenericSetData *ldUntargetable = getMyDataFromData(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_UNTARGETABLE_KEY);
	FSMLDGenericSetData *ldUnattackable = getMyDataFromDataIfExists(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_UNATTACKABLE_KEY);

	if(SAFE_MEMBER(ldUnattackable, setData))
	{
		estrPrintf(errString, "Attempting to SetUntargetable when Unattackable was set.  This is not what you want.  These settings overlap.");
		return ExprFuncReturnError;
	}

	if(on)
	{
		entSetDataFlagBits(e, ENTITYFLAG_UNTARGETABLE);
		entSetDataFlagBits(e, ENTITYFLAG_UNSELECTABLE);

		ldUntargetable->setData = true;
	}
	else
	{
		entClearDataFlagBits(e, ENTITYFLAG_UNTARGETABLE);
		entClearDataFlagBits(e, ENTITYFLAG_UNSELECTABLE);

		ldUntargetable->setData = false;
	}

	return ExprFuncReturnFinished;
}

// Turns the untargetable flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetUnattackable);
ExprFuncReturnVal exprFuncSetUnattackable(ACMD_EXPR_SELF Entity* e, ExprContext *context, S32 on, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDGenericSetData *ldUnattackable = getMyDataFromData(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_UNATTACKABLE_KEY);
	FSMLDGenericSetData *ldUntargetable = getMyDataFromDataIfExists(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_UNTARGETABLE_KEY);

	if(SAFE_MEMBER(ldUntargetable, setData))
	{
		estrPrintf(errString, "Attempting to SetUnattackable when Untargetable was set.  This is not what you want.  These settings overlap.");
		return ExprFuncReturnError;
	}

	if(on)
	{
		entSetDataFlagBits(e, ENTITYFLAG_UNTARGETABLE);

		ldUnattackable->setData = true;
	}
	else
	{
		entClearDataFlagBits(e, ENTITYFLAG_UNTARGETABLE);

		ldUnattackable->setData = false;
	}

	return ExprFuncReturnFinished;
}

// Return arithmetic mean of formation positions, ignoring non-formation ents
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetFormationPos);
void exprFuncGetFormationPos(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT matOut, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	int num = eaSize(entsIn);
	int count = 0;
	int ownerCount = 0;
	Vec3 failPos;
	if(num)
	{
		int i;

		copyMat4(unitmat, matOut);
		for(i = 0; i < num; i++)
		{
			Entity *e = (*entsIn)[i];
			AIVarsBase *aib = e->aibase;
			
			entGetPos(e, failPos);

			if(aib->pFormationData)
			{
				Vec3 pos;
				AIFormation *formation = aib->pFormationData->pFormation;
				Entity *leader = entFromEntityRef(iPartitionIdx, formation->erLeader);

				if (leader && aib->pFormationData->pCurrentSlot)
				{
					if(formation->bSettled)
						copyVec3(formation->vFormationPos, pos);
					else 
						entGetPos(leader, pos);

					addVec3(pos, aib->pFormationData->pCurrentSlot->vOffset, pos);
					addVec3(matOut[3], pos, matOut[3]);

					count++;
				}
				else if(leader)
					entGetPos(leader, failPos);
			}
		}

		if(count>0)
		{
			matOut[3][0] /= count;
			matOut[3][1] /= count;
			matOut[3][2] /= count;
		}
		else // Fallback
			copyVec3(failPos, matOut[3]);
	}
}

// Returns true if the critter is currently being interacted with at least one player.
AUTO_EXPR_FUNC(ai) ACMD_NAME(IsCritterCurrentlyInteracted);
bool exprFuncIsCritterCurrentlyInteracted(ACMD_EXPR_SELF Entity* e)
{
	if (e && e->pCritter)
	{
		return ea32Size(&e->pCritter->encounterData.perInteractingPlayers) > 0;
	}

	return false;
}

// Regenerates the entity's static random seed, which is used for the StaticRandom() functions
AUTO_EXPR_FUNC(ai) ACMD_NAME(StaticRandomRegenerate);
void exprFuncStaticRandomRegenerate(ACMD_EXPR_SELF Entity* e)
{
	if(e && e->aibase)
	{
		e->aibase->uiRandomSeed = randomU32();
	}
}

// Returns a float in the range [min..max) which is generally static for the entity,
//  though it can be regenerated using StaticRandomRegenerate().
AUTO_EXPR_FUNC(ai) ACMD_NAME(StaticRandomFloat);
F32 exprFuncStaticRandomFloat(ACMD_EXPR_SELF Entity* e, F32 min, F32 max)
{
	F32 r = min;
	if(e && e->aibase)
	{
		U32 uiRandomSeed = e->aibase->uiRandomSeed;
		F32 d = randomPositiveF32Seeded(&uiRandomSeed,RandType_BLORN_Static);
		r = min + d * (max-min);
	}
	return r;
}

// 
AUTO_EXPR_FUNC(ai) ACMD_NAME(DragonUserPowerOnEntArrayOverrideTargetFacing);
ExprFuncReturnVal exprFuncDragonUserPowerOnEntArrayOverrideTargetFacing(ACMD_EXPR_SELF Entity *pEnt, 
																		ExprContext* pContext,
																		ACMD_EXPR_ENTARRAY_IN eaEntsIn,
																		ACMD_EXPR_DICT(PowerDef) const char* pchPowerName,
																		ACMD_EXPR_ERRSTRING errStr)
{
	ExprFuncReturnVal ret;

	if (!pEnt->mm.mrDragon)
	{
		estrPrintf(errStr, "Entity does not have a dragon requester, aborting.");
		return ExprFuncReturnError;
	}

	ret = exprFuncQueuePowerOnEnt(pEnt, pContext, eaEntsIn, pchPowerName, errStr);
	if (ret == ExprFuncReturnFinished)
	{
		Power *ppow = character_FindPowerByName(pEnt->pChar, pchPowerName);
		if (ppow)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			EntityRef erTargetRef = entGetRef(*eaEntsIn[0]);
			mrDragon_SetOverrideTargetAndPowersAngleOffset(pEnt->mm.mrDragon, erTargetRef, (pDef ? pDef->fYaw : 0.f));
		}
	}

	return ret;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(DragonUserPowerAtEntPosOverrideTargetFacing);
ExprFuncReturnVal exprFuncDragonUserPowerAtEntPosOverrideTargetFacing(	ACMD_EXPR_SELF Entity *pEnt, 
																		ExprContext* pContext,
																		ACMD_EXPR_ENTARRAY_IN eaEntsIn,
																		ACMD_EXPR_DICT(PowerDef) const char* pchPowerName,
																		ACMD_EXPR_ERRSTRING errStr)
{
	ExprFuncReturnVal ret;

	if (!pEnt->mm.mrDragon)
	{
		estrPrintf(errStr, "Entity does not have a dragon requester, aborting.");
		return ExprFuncReturnError;
	}

	ret = exprFuncQueuePowerOnEntPos(pEnt, pContext, eaEntsIn, pchPowerName, errStr);
	if (ret == ExprFuncReturnFinished)
	{
		Power *ppow = character_FindPowerByName(pEnt->pChar, pchPowerName);
		if (ppow)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			EntityRef erTargetRef = entGetRef(*eaEntsIn[0]);
			F32 fYaw = (pDef ? pDef->fYaw : 0.f);
			Vec3 vEntPos;
			entGetPos((*eaEntsIn[0]), vEntPos);

			mrDragon_SetOverrideTargetPosAndPowersAngleOffset(pEnt->mm.mrDragon, vEntPos, fYaw);
		}
	}

	return ret;
}