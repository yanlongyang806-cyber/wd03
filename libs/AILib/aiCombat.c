#include "aiConfig.h"
#include "aiAvoid.h"
#include "aiBrawlerCombat.h"
#include "aiCombatRoles.h"
#include "aiCombatJob.h"
#include "aiDebug.h"
#include "aiDebugShared.h"
#include "aiGroupCombat.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiMultiTickAction.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "aiAnimList.h"
#include "AnimList_Common.h"

#include "AttribModFragility.h"
#include "beaconPath.h"
#include "Character.h"
#include "Character_target.h"
#include "CombatConfig.h"
#include "entCritter.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "EntityMovementDragon.h"
#include "gslMapState.h"
#include "PowersMovement.h"
#include "rand.h"
#include "RegionRules.h"
#include "StateMachine.h"
#include "tokenstore.h"
#include "worldgrid.h"
#include "aiFCExprFunc.h"
#include "Character_combat.h"

// move this at some point
#include "../WorldLib/wcoll/collcache.h"

#include "autogen/AILib_autogen_QueuedFuncs.h"
#include "aiConfig_h_ast.h"
#include "aiMovement_h_ast.h"

static int disableGlobalPowerRecharge = false;
AUTO_CMD_INT(disableGlobalPowerRecharge, disableGlobalPowerRecharge);

static int s_disableGrieved = false;
AUTO_CMD_INT(s_disableGrieved, disableGrieved);

static int disableAIPowerUsage = false;
AUTO_CMD_INT(disableAIPowerUsage, disableAIPowerUsage);

static F32 aiPredictedPositionDistance = 0;
AUTO_CMD_FLOAT(aiPredictedPositionDistance, aiPredictedPositionDistance);

static int aiStickyEnemies = false;
AUTO_CMD_INT(aiStickyEnemies, aiStickyEnemies);

static U32 overrideCombatMaxRunAwayCount = 0;
AUTO_CMD_INT(overrideCombatMaxRunAwayCount, overrideCombatMaxRunAwayCount);

AUTO_CMD_FLOAT(aiGlobalSettings.fCombatPositionRangeSensitivity, aiCombatPositionSensitivity);


static void aiCombatRemoveListRef(AIRelativeLoc *loc)
{
	loc->list = NULL;
}

void aiCombatInitRelativeLocs(Entity *be, AIVarsBase *aib)
{
	int i;
	AIConfig *config = aiGetConfig(be, aib);
	S32 iNumSlots = aiGlobalSettings.iCombatAngleGranularity + eaSize(&aiGlobalSettings.eaCombatPositionSlots);
	
	for(i=0; i<iNumSlots; i++)
	{
		if(!eaGet(&aib->relLocLists, i))
			eaPush(&aib->relLocLists, calloc(1, sizeof(AIRelativeLocList)));
		else
		{
			AIRelativeLocList *list = aib->relLocLists[i];

			eaClearEx(&list->locs, aiCombatRemoveListRef);
		}
	}

	for(; i<eaSize(&aib->relLocLists); i++)
		free(aib->relLocLists[i]);

	eaSetSize(&aib->relLocLists, iNumSlots);
}

void aiCombatCleanupRelLocs(Entity *be, AIVarsBase *aib)
{
	int i;

	for(i=0; i<eaSize(&aib->relLocLists); i++)
	{
		eaDestroyEx(&aib->relLocLists[i]->locs, aiCombatRemoveListRef);
	}
	eaDestroyEx(&aib->relLocLists, NULL);

	if(aib->myRelLoc && aib->myRelLoc->list)
		eaFindAndRemoveFast(&aib->myRelLoc->list->locs, aib->myRelLoc);

	SAFE_FREE(aib->myRelLoc);

	eaDestroyStruct(&aib->ratings, parse_AIDebugLocRating);
}

AUTO_COMMAND;
void aiCombatSetPositionGranularity(int granularity)
{
	if(granularity!=aiGlobalSettings.iCombatAngleGranularity)
	{
		Entity *ent;
		EntityIterator *iter;

		aiGlobalSettings.iCombatAngleGranularity = granularity;

		iter = entGetIteratorAllTypesAllPartitions(0, 0);

		while(ent = EntityIteratorGetNext(iter))
		{
			if(ent->aibase)
				aiCombatInitRelativeLocs(ent, ent->aibase);
		}

		EntityIteratorRelease(iter);
	}
}

typedef enum ShieldDirs
{
	SHIELD_DIR_NONE,
	SHIELD_DIR_FRONT,
	SHIELD_DIR_RIGHT,
	SHIELD_DIR_LEFT,
	SHIELD_DIR_BACK,
}ShieldDirs;

static F32 shieldDirToYaw(ShieldDirs dir)
{
	switch(dir)
	{
	xcase SHIELD_DIR_NONE:
		return PI;
	xcase SHIELD_DIR_FRONT:
		return 0;
	xcase SHIELD_DIR_RIGHT:
		return PI/2;
	xcase SHIELD_DIR_LEFT:
		return -PI/2;
	xcase SHIELD_DIR_BACK:
		return PI;
	}
	return 0;
}

static ShieldDirs yawToShieldDir(F32 yaw)
{
	yaw = fixAngle(yaw);  // [-PI,PI]
	if(yaw<=PI/4 && yaw>=-PI/4)
		return SHIELD_DIR_FRONT;
	if(yaw<=3*PI/4 && yaw >= PI/4)
		return SHIELD_DIR_RIGHT;
	if(yaw<=-PI/4 && yaw>=-3*PI/4)
		return SHIELD_DIR_LEFT;
	return SHIELD_DIR_BACK;
}

static ShieldDirs getWeakestShield(AttribMod **shields, F32 *lowestHealthOut)
{
	F32 lowestHealth = FLT_MAX;
	int i;
	int weakest = 0;

	for(i = eaSize(&shields)-1; i>=0; i--)
	{
		if(shields[i]->pFragility->fHealth<lowestHealth)
		{
			weakest = i+1;
			lowestHealth = shields[i]->pFragility->fHealth;
		}
	}

	if(lowestHealthOut)
		*lowestHealthOut = lowestHealth;

	return weakest;
}

static ShieldDirs getWeakestShieldPct(AttribMod **shields, F32 *lowestHealthPctOut)
{
	F32 lowestHealth = FLT_MAX;
	int i;
	int weakest = 0;

	for(i = eaSize(&shields)-1; i>=0; i--)
	{
		F32 pct = shields[i]->pFragility->fHealth / shields[i]->pFragility->fHealthMax * 100;
		if(pct<lowestHealth)
		{
			weakest = i+1;
			lowestHealth = pct;
		}
	}

	if(lowestHealthPctOut)
		*lowestHealthPctOut = lowestHealth;

	return weakest;
}

static ShieldDirs getHighestShield(AttribMod **shields, F32 *highestHealthPctOut)
{
	F32 highestHealth = -FLT_MAX;
	int i;
	int highest = 0;

	for(i = eaSize(&shields)-1; i>=0; i--)
	{
		if(shields[i]->pFragility->fHealth>highestHealth)
		{
			highest = i+1;
			highestHealth = shields[i]->pFragility->fHealth;
		}
	}

	if(highestHealthPctOut)
		*highestHealthPctOut = highestHealth;

	return highest;
}

static ShieldDirs getHighestShieldPct(AttribMod **shields, F32 *highestHealthPctOut)
{
	F32 highestHealth = -FLT_MAX;
	int i;
	int highest = 0;

	for(i = eaSize(&shields)-1; i>=0; i--)
	{
		F32 pct = shields[i]->pFragility->fHealth / shields[i]->pFragility->fHealthMax * 100;
		if(pct>highestHealth)
		{
			highest = i+1;
			highestHealth = shields[i]->pFragility->fHealth;
		}
	}

	if(highestHealthPctOut)
		*highestHealthPctOut = highestHealth;

	return highest;
}

// Returns the lowest shield percentage of any of the entities passed in (returns 0 if no
// entities are passed in)
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntArrayGetWeakestShieldPct);
F32 exprFuncEntArrayGetWeakestShieldPct(ACMD_EXPR_ENTARRAY_IN entsIn)
{
	int i;
	F32 weakestShieldPct = 100;

	if(!eaSize(entsIn))
		return 0;

	for(i = eaSize(entsIn)-1; i >= 0; i--)
	{
		F32 individualWeakestShieldPct;
		getWeakestShieldPct((*entsIn)[i]->pChar->ppModsShield, &individualWeakestShieldPct);
		if(individualWeakestShieldPct < weakestShieldPct)
			weakestShieldPct = individualWeakestShieldPct;
	}

	return weakestShieldPct;
}

static Entity* getHighestDamageEnt(Entity *e, AIVarsBase *aib)
{
	int i;
	Entity *highestDamage = NULL;
	F32 most_damage = -FLT_MAX;
	for(i = eaSize(&aib->statusTable)-1; i >= 0; i--)
	{
		AIStatusTableEntry *entry = aib->statusTable[i];

		if(entry->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE] > most_damage)
		{
			most_damage = entry->trackedAggroCounts[AI_NOTIFY_TARGET_SELF][AI_NOTIFY_TYPE_DAMAGE];
			highestDamage = entFromEntityRef(entGetPartitionIdx(e), entry->entRef);
		}
	}

	return highestDamage;
}

// convention: slot 0 is centered at 0 rad
static int aiCombatGetRelLocationFromAngle(F32 fAngle)
{
	// make sure the angle is positive, generate an index, add half a pie slice, floor it, and modulate by the total number of slices
	return ((int)floor((fixAngle(fAngle)+2.0f*PI)/(2.0f*PI/aiGlobalSettings.iCombatAngleGranularity)+0.5f))%aiGlobalSettings.iCombatAngleGranularity;
}

static int aiCombatGetRelLocationFromPos(Entity *e, AIVarsBase *aib, const Vec3 vecCandidatePos)
{
	F32 fCapsuleRadius = entGetPrimaryCapsuleRadius(e);
	Vec3 ePos;
	Vec3 attackTargetPos;
	Vec3 combatPosSlotPos;
	S32 i;
	S32 iClosest = -1;
	F32 fMinDistanceSquared = FLT_MAX;

	copyVec3(vecCandidatePos, ePos);
	ePos[1] = 0.f;

	// Get the attack target pos
	entGetPos(aib->attackTarget, attackTargetPos);
	attackTargetPos[1] = 0.f;

	for (i = 0; i < eaSize(&aiGlobalSettings.eaCombatPositionSlots); i++)
	{
		F32 fDistanceSquared; 
		copyVec3(attackTargetPos, combatPosSlotPos);
		combatPosSlotPos[0] += aiGlobalSettings.eaCombatPositionSlots[i]->fXOffset;
		combatPosSlotPos[2] += aiGlobalSettings.eaCombatPositionSlots[i]->fZOffset;

		fDistanceSquared = distance3Squared(combatPosSlotPos, ePos);

		if (fDistanceSquared < fMinDistanceSquared)
		{
			fMinDistanceSquared = fDistanceSquared;
			iClosest = i;
		}
	}
	return iClosest;
}

// Eval functions must return a value from 0.0f to 1.0f. 0.0f is that this phenomenon is not in evidence.  1.0f means it is
// very much the case here.

static F32 aiCombatPositionEvalTargetShields(Entity *e, AIVarsBase *aib, F32 curAngle, F32 weakest, F32 highest)
{
	ShieldDirs curShield = yawToShieldDir(curAngle);
	F32 angleRating = 0;

	if(curShield>0 && curShield-1<eaSize(&aib->attackTarget->pChar->ppModsShield))
	{
		F32 health = aib->attackTarget->pChar->ppModsShield[curShield-1]->pFragility->fHealth;
		F32 yawDiff = fabs(fixAngle(shieldDirToYaw(curShield)-curAngle));
		angleRating = (PI-yawDiff)/PI;												// Still prefer exact angle
		angleRating *= (highest-health)/(highest-weakest);							// Modify by health range
	}

	return angleRating;
}

static F32 aiCombatPositionEvalWeakShields(Entity *e, AIVarsBase *aib, F32 yawToTarget, F32 weakest, F32 highest)
{
	F32 ratio = 0;
	ShieldDirs curShield;
	curShield = yawToShieldDir(yawToTarget);

	if(curShield>0 && curShield-1<eaSize(&e->pChar->ppModsShield))
	{
		F32 health = e->pChar->ppModsShield[curShield-1]->pFragility->fHealth;
		ratio = (highest-health)/(highest-weakest);
	}

	return ratio;
}

static F32 aiCombatPositionEvalStrongShields(Entity *e, AIVarsBase *aib, F32 yawToTarget, F32 weakest, F32 highest)
{
	F32 ratio = 0;
	ShieldDirs curShield;
	curShield = yawToShieldDir(yawToTarget);

	if(curShield>0 && curShield-1<eaSize(&e->pChar->ppModsShield))
	{
		F32 health = e->pChar->ppModsShield[curShield-1]->pFragility->fHealth;
		ratio = (health-weakest)/(highest-weakest);
	}

	return ratio;
}

static F32 aiCombatPositionEvalWeakShieldMostDamage(Entity *e, AIVarsBase *aib, Vec3 curAnglePos, F32 curAngle, Entity *highestDamage, F32 weakest, F32 highest)
{
	F32 ratio = 0;
	Vec3 targetToMe;
	F32 yawToMe;
	F32 yawToHD;
	F32 shieldYaw;
	Vec3 hdPos;
	Vec3 hdDir;
	Vec3 myPos;
	ShieldDirs curShield;

	entGetPos(e, myPos);
	subVec3(curAnglePos, myPos, targetToMe);
	yawToMe = getVec3Yaw(targetToMe);

	entGetPos(highestDamage, hdPos);
	subVec3(hdPos, curAnglePos, hdDir);
	yawToHD = getVec3Yaw(hdDir);

	shieldYaw = yawToHD-yawToMe;
	curShield = yawToShieldDir(shieldYaw);

	if(curShield>0 && curShield-1<eaSize(&e->pChar->ppModsShield))
	{
		F32 health = e->pChar->ppModsShield[curShield-1]->pFragility->fHealth;
		ratio = (highest-health)/(highest-weakest);
	}

	return ratio;
}

static F32 aiCombatPositionEvalClumping(Entity *e, AIVarsBase *aib, F32 curAngle, Vec3 vCandidatePos, F32 targetRange, bool bDoingCombatMovement)
{
	// We don't really care if there are more than 3. 3 is already clumped.
	const F32 fMaxClumping = 3.0f;
	AIConfig *config = aiGetConfig(e, aib);
	int listIndex = config->useCombatPositionSlots ? aiCombatGetRelLocationFromPos(e, aib, vCandidatePos) : aiCombatGetRelLocationFromAngle(curAngle);	
	
	AIRelativeLocList *list;
	F32 rating = 0;

	if (listIndex >= 0 && config->useCombatPositionSlots)
	{
		// Combat position slots are after the pie slices
		listIndex += aiGlobalSettings.iCombatAngleGranularity;
	}
	
	list = listIndex == -1 ? NULL : eaGet(&aib->attackTarget->aibase->relLocLists, listIndex);

	// Get the list of followers for this location
	if(list)  // Would be weird otherwise, but no reason to crash
	{
		int j;
		
		for(j=0; j<eaSize(&list->locs); j++)
		{
			AIRelativeLoc *loc = list->locs[j];
			if (config->useCombatPositionSlots)
			{
				if (loc->entRef != e->myRef && (aib->currentlyMoving || bDoingCombatMovement))
				{
					rating += 1;
				}
			}
			else
			{
				if (loc->entRef != e->myRef && fabs(loc->range - targetRange) < aiGlobalSettings.fCombatPositionRangeSensitivity)
				{
					rating += 1;
				}
			}
		}
	}

	return MIN(rating/fMaxClumping,1.0f);
}

static F32 aiCombatPositionEvalOccupancy(Entity *e, AIVarsBase *aib, Vec3 vCandidatePos)
{
	F32 fSelfCapsuleRadius = entGetPrimaryCapsuleRadius(e);
	S32 i;
	S32 iPartitionIdx = entGetPartitionIdx(e);

	aiUpdateProxEnts(e, aib);
	for (i = 0; i < aib->proxEntsCount; i++)
	{
		Entity *pProxEnt = aib->proxEnts[i].e;

		if (!mmIsCollisionEnabled(pProxEnt->mm.movement))
		{
			continue;
		}
		
		if (aib->attackTarget == pProxEnt)
		{
			continue;
		}

		if (entGetType(pProxEnt) == GLOBALTYPE_ENTITYPLAYER)
		{
			if (!aib->currentlyMoving)
			{
				continue;
			}
			else
			{
				Vec3 vecPlayerVelocity;
				F32 fVel;
				setVec3same(vecPlayerVelocity, 0.f);
				entCopyVelocityFG(pProxEnt, vecPlayerVelocity);
				fVel = lengthVec3XZ(vecPlayerVelocity);

				if (fVel >= 0.0001f)
				{
					// Ignore moving players
					continue;
				}
			}
		}

		if (pProxEnt && !pProxEnt->aibase->currentlyMoving)
		{
			F32 fCapsuleRadiiSum = fSelfCapsuleRadius + entGetPrimaryCapsuleRadius(pProxEnt);
			Vec3 vecEntPos;
			entGetPos(pProxEnt, vecEntPos);			

			if (distance3XZ(vCandidatePos, vecEntPos) <= fCapsuleRadiiSum + aiGlobalSettings.fCombatPositionOccupancyCheckSensitivity)
			{
				return 1.f;
			}
		}		
	}
	return 0.f;
}

static F32 aiCombatPositionEvalSoftAvoid(Entity *e, AIVarsBase *aib, Vec3 vCandidatePos)
{
	// Each soft avoid entity has a magnitude that ranges between 0 and 100
	S32 iTotalMagnitude = 0;
	FOR_EACH_IN_EARRAY(aib->softAvoid.base.volumeEntities, AIVolumeEntry, volumeEntry)
	{
		AIVolumeSoftAvoidInstance *instance = (AIVolumeSoftAvoidInstance *)volumeEntry->instance;
		devassert(instance);
		devassert(volumeEntry->isSphere);

		if (volumeEntry->isSphere && instance && instance->magnitude > 0)
		{
			Entity *avoidEnt = entFromEntityRef(entGetPartitionIdx(e), volumeEntry->entRef);
			if(avoidEnt)
			{
				F32 distSQR;
				Vec3 avoidEntPos;
				entGetPos(avoidEnt, avoidEntPos);
				distSQR = distance3Squared(vCandidatePos, avoidEntPos);

				if (distSQR <= SQR(volumeEntry->sphereRadius + 2.f))
				{
					S32 iMagnitudeAdjusted = instance->magnitude;
					iTotalMagnitude += CLAMP(iMagnitudeAdjusted, 1, 100);
				}
			}
		}
	}
	FOR_EACH_END

	// Make sure the total magnitude is not over 100
	MIN1(iTotalMagnitude, 100);

	return iTotalMagnitude / 100.f;
}

static F32 aiCombatPositionEvalStayTogether(Entity *e, AIVarsBase *aib, Vec3 vCandidatePos)
{
	Vec3 vTeamCenter;
	AITeam * pTeam = aiTeamGetCombatTeam(e,aib);
	aiTeamGetAveragePosition(pTeam,vTeamCenter);

	return (50.0f-MIN(50.0f,distance3(vTeamCenter,vCandidatePos)))/50.0f;
}

static F32 aiCombatPositionEvalFlanking(Entity *e, AIVarsBase *aib, F32 curAngle, Vec3 vCandidatePos, F32 targetRange)
{
	AIConfig *config = aiGetConfig(e, aib);
	int listIndex = config->useCombatPositionSlots ? aiCombatGetRelLocationFromPos(e, aib, vCandidatePos) : aiCombatGetRelLocationFromAngle(curAngle+PI);
	AIRelativeLocList *list;
	F32 rating = 0;

	if (listIndex >= 0 && config->useCombatPositionSlots)
	{
		// Combat position slots are after the pie slices
		listIndex += aiGlobalSettings.iCombatAngleGranularity;
	}

	list = listIndex == -1 ? NULL : eaGet(&aib->attackTarget->aibase->relLocLists, listIndex);

	if(list)  // Would be weird otherwise, but no reason to crash
	{
		int j;
		for(j=0; j<eaSize(&list->locs); j++)
		{
			AIRelativeLoc *loc = list->locs[j];

			if(loc->entRef!=e->myRef && fabs(loc->range-targetRange)<aiGlobalSettings.fCombatPositionRangeSensitivity)
			{
				rating += 1;
			}
		}
	}

	// assuming right here that we only care about up to 1 guy in flanking position.  If not, this should change.
	return MIN(rating,1.0f);
}

static F32 aiCombatPositionEvalArcLimitedPowers(Entity *e, AIVarsBase *aib, F32 yMe_Abs, F32 yMe_To_AT_Abs, F32 yMe_To_Loc_Abs)
{
	F32 ratio = 0;
	int i;
	F32 totalRating = 0;
	U32 inArc;

	aiRatePowers(e, aib, aib->attackTarget, aib->attackTargetDistSQR, 0, NULL, &totalRating); 

	for(i=0; i<eaSize(&aib->powers->powInfos); i++)
	{
		AIPowerInfo *powInfo = aib->powers->powInfos[i];
		AIPowerConfig *powConf = aiGetPowerConfig(e, aib, powInfo);
	
		if(!powInfo->isArcLimitedPower || powConf->absWeight == 1)
			totalRating -= powInfo->curRating;
	}

	for(i=0; i<eaSize(&aib->powers->powInfos); i++)
	{
		PowerDef *powDef;
		AIPowerInfo *powInfo = aib->powers->powInfos[i];
		AIPowerConfig *powConf = aiGetPowerConfig(e, aib, powInfo);
		F32 yP_To_AT_Rel_Me, yMe_To_AT_Rel_Me, yMe_To_Loc_Rel_Me;
		
		if(!powInfo->isArcLimitedPower || powConf->absWeight == 1)
			continue;
	
		powDef = GET_REF(powInfo->power->hDef);
		if(!powDef)
			continue;

		yP_To_AT_Rel_Me = subAngle(yMe_To_AT_Abs, addAngle(yMe_Abs, powInfo->power->fYaw));

		yMe_To_AT_Rel_Me = subAngle(yMe_To_AT_Abs, yMe_Abs);
		yMe_To_Loc_Rel_Me = subAngle(yMe_To_Loc_Abs, yMe_Abs);

		if(yP_To_AT_Rel_Me > 0 && yMe_To_Loc_Rel_Me > 0 && yMe_To_Loc_Rel_Me < yP_To_AT_Rel_Me)
			inArc = 1;
		else if(yP_To_AT_Rel_Me < 0 && yMe_To_Loc_Rel_Me < 0 && yMe_To_Loc_Rel_Me > yP_To_AT_Rel_Me)
			inArc = 1;
		else 
			inArc = 0;

		if(inArc)
			ratio = 1;
	}

	return ratio;
}

static F32 aiCombatPositionEvalArcDistance(Entity *e, AIVarsBase *aib, F32 angle)
{
	return 1.0f-fabs(fixAngle(angle))/PI;
}

static F32 getYawMe_To_AT_Abs(Entity *e, Vec3 pMe, Vec3 pTarget)
{
	Vec3 dMe_To_Target;

	subVec3(pTarget, pMe, dMe_To_Target);

	return getVec3Yaw(dMe_To_Target);
}

static F32 aiCombatPositionEvalYOffset(Entity *e, AIVarsBase *aib, Vec3 attackTargetPos, Vec3 curAnglePos)
{
	F32 diff = fabs(vecY(attackTargetPos) - vecY(curAnglePos));

	diff = powf(diff,2.0f)/100.0f;

	return MIN(diff,1.0f);
}

static F32 aiCombatPositionGetWeightMagnitude(AICombatMovementConfig *cmconf)
{
	F32 fLargest = 0;
	int i;

	FORALL_PARSETABLE(parse_AICombatMovementConfig, i)
	{
		if (TOK_GET_TYPE(parse_AICombatMovementConfig[i].type) == TOK_F32_X && !(parse_AICombatMovementConfig[i].type & TOK_REDUNDANTNAME))
		{
			fLargest = MAX(fabs(TokenStoreGetF32(parse_AICombatMovementConfig, i, cmconf, 0, NULL)),fLargest);
		}
	}

	return fLargest;
}

static F32 aiCombatPositionEval(Entity *e, AIVarsBase *aib, AIDebugLocRating *ratingOut, Vec3 pos, F32 yAT_To_Loc_Abs, F32 yAT_Abs, Entity *hd, F32 range, bool bDoingCombatMovement)
{
	F32 rating = 0;
	AIConfig *config = aiGetConfig(e, aib);
	F32 t_highest, t_weakest;
	F32 s_highest, s_weakest;
	F32 yAT_To_Loc_Rel;
	F32 yMe_To_AT_Rel;
	F32 yMe_To_AT_Abs;
	F32 yMe_Abs;
	F32 yAT_To_Me_Abs;
	F32 yMe_To_Loc_Abs;
	F32 yMe_To_Loc_Rel;
	F32 dMe_To_Loc;
	Vec3 dTemp;
	Vec3 attackTargetPos;
	Vec3 curPos;
	Vec2 curPyFace;
	F32 yPosAngle = TWOPI/aiGlobalSettings.iCombatAngleGranularity;
	int i;
	ShieldDirs t_highest_dir, t_weakest_dir;
	ShieldDirs s_highest_dir, s_weakest_dir;

	for(i = eaSize(&aib->avoid.base.volumeEntities) - 1; i >= 0; i--)
	{
		if(aiAvoidEntryCheckPoint(e, pos, aib->avoid.base.volumeEntities[i],true /*isFG*/,NULL /*msg*/))
		{
			// you're not going to go there, so don't even pretend
			return -9999.9f;
		}
	}

	entGetPos(e, curPos);
	entGetPos(aib->attackTarget, attackTargetPos);

	subVec3(pos, curPos, dTemp);
	dMe_To_Loc = lengthVec3(dTemp);
	yMe_To_Loc_Abs = getVec3Yaw(dTemp);
	
	subVec3(curPos, attackTargetPos, dTemp);
	yAT_To_Me_Abs = getVec3Yaw(dTemp);
	
	entGetFacePY(e, curPyFace);

	yAT_To_Loc_Rel = EntityAngleToSourcePosUtil(aib->attackTarget->pChar->pEntParent, pos);
	yMe_To_AT_Rel = EntityAngleToSourcePosUtil(e, attackTargetPos);

	yMe_Abs = curPyFace[1];
	yMe_To_AT_Abs = getYawMe_To_AT_Abs(e, curPos, attackTargetPos);
	yMe_To_Loc_Rel = subAngle(yAT_To_Loc_Abs, yAT_To_Me_Abs);

	s_highest_dir = getHighestShield(e->pChar->ppModsShield, &s_highest);
	t_highest_dir = getHighestShield(aib->attackTarget->pChar->ppModsShield, &t_highest);
	
	s_weakest_dir = getWeakestShield(e->pChar->ppModsShield, &s_weakest);
	t_weakest_dir = getWeakestShield(aib->attackTarget->pChar->ppModsShield, &t_weakest);

	if (config->combatMovementParams.arcDistance) 
	{
		F32 eval = aiCombatPositionEvalArcDistance(e, aib, subAngle(yAT_To_Me_Abs, yAT_To_Loc_Abs));
		rating += eval*config->combatMovementParams.arcDistance;
		if(ratingOut)
			ratingOut->arcDistance = eval * config->combatMovementParams.arcDistance;
	}

	// should this always be done, based on the spawn position (leash seems to be team based)
	if (config->combatMovementParams.combatPositioningUseCoherency || 
		config->combatMovementParams.softCoherency ||
		aib->leashTypeOverride != AI_LEASH_TYPE_DEFAULT)
	{
		F32 coherencyDistSQR = aiGetCoherencyDist(e, config) + 0.1f;
		F32 distSQR;
		Vec3 vLeashPos;

		aiGetLeashPosition(e, aib, vLeashPos);
		distSQR = distance3Squared(vLeashPos, pos);
		coherencyDistSQR = SQR(coherencyDistSQR);
		if (distSQR > coherencyDistSQR)
		{	// if we are out of our coherency distance, 
			// subtract a relative amount, the further the lower it is rated 
			if (config->combatMovementParams.softCoherency)
			{
				rating -= config->combatMovementParams.softCoherency * (sqrt(distSQR/coherencyDistSQR) - 1.f);
			}
			else
			{
				rating -= 1.f;
			}
		}
		else if (config->combatMovementParams.softCoherency)
		{
			coherencyDistSQR = SQR(config->combatMovementParams.softCoherency);
			if (distSQR > coherencyDistSQR)
			{
				rating -= config->combatMovementParams.softCoherency * (sqrt(distSQR/coherencyDistSQR) - 1.f);
			}
		}
	}


	if(config->combatMovementParams.clumping)
	{
		F32 eval = aiCombatPositionEvalClumping(e, aib, yAT_To_Loc_Abs, pos, range, bDoingCombatMovement);
		rating += eval*config->combatMovementParams.clumping;
		if(ratingOut)
			ratingOut->clumping = eval*config->combatMovementParams.clumping;
	}

	if (config->combatMovementParams.softAvoid)
	{
		F32 eval = aiCombatPositionEvalSoftAvoid(e, aib, pos);
		rating += eval*config->combatMovementParams.softAvoid;
		if(ratingOut)
			ratingOut->softAvoid = eval*config->combatMovementParams.softAvoid;
	}

	if (config->combatMovementParams.avoidOccupiedPositions)
	{
		F32 eval = aiCombatPositionEvalOccupancy(e, aib, pos);
		rating += eval*config->combatMovementParams.avoidOccupiedPositions;
		if(ratingOut)
			ratingOut->avoidOccupiedPositions = eval*config->combatMovementParams.avoidOccupiedPositions;
	}

	if(config->combatMovementParams.stayTogether)
	{
		F32 eval = aiCombatPositionEvalStayTogether(e,aib,pos);
		rating += eval*config->combatMovementParams.stayTogether;
		if(ratingOut)
			ratingOut->stayTogether = eval*config->combatMovementParams.stayTogether;
	}

	if(config->combatMovementParams.flanking)
	{
		F32 eval = aiCombatPositionEvalFlanking(e, aib, yAT_To_Loc_Abs, pos, range);
		rating += eval*config->combatMovementParams.flanking;
		if(ratingOut)
			ratingOut->flanking = eval*config->combatMovementParams.flanking;
	}

	if(config->combatMovementParams.targetShields && t_highest_dir && t_weakest_dir && !nearf(t_highest, t_weakest))
	{
		F32 eval = aiCombatPositionEvalTargetShields(e, aib, yAT_To_Loc_Rel, t_weakest, t_highest);
		rating += eval*config->combatMovementParams.targetShields;
		if(ratingOut)
			ratingOut->targetShields = eval*config->combatMovementParams.targetShields;
	}

	if(config->combatMovementParams.turnWeakShieldTo && s_highest_dir && s_weakest_dir && !nearf(s_highest, s_weakest))
	{
		F32 eval = aiCombatPositionEvalWeakShields(e, aib, yMe_To_AT_Rel, s_weakest, s_highest);
		rating += eval*config->combatMovementParams.turnWeakShieldTo;
		if(ratingOut)
			ratingOut->turnWeakShieldTo = eval*config->combatMovementParams.turnWeakShieldTo;
	}

	if(config->combatMovementParams.turnStrongShieldTo && s_highest_dir && s_weakest_dir && !nearf(s_highest, s_weakest))
	{
		F32 eval = aiCombatPositionEvalStrongShields(e, aib, yMe_To_AT_Rel, s_weakest, s_highest);
		rating += eval*config->combatMovementParams.turnStrongShieldTo;
		if(ratingOut)
			ratingOut->turnStrongShieldTo = eval*config->combatMovementParams.turnStrongShieldTo;
	}

	if(config->combatMovementParams.currentLocation && fabs(yMe_To_Loc_Rel) < yPosAngle)
	{
		// Same position;
		rating += config->combatMovementParams.currentLocation;
	}

	if(config->combatMovementParams.neighborLocation && fabs(yMe_To_Loc_Rel) > yPosAngle && fabs(yMe_To_Loc_Rel) < 2*yPosAngle)
	{
		// Neighbor position
		rating += config->combatMovementParams.neighborLocation;
	}

	if(config->combatMovementParams.circlingManeuvers && fabs(yMe_To_Loc_Rel)>HALFPI && fabs(yMe_To_Loc_Rel)<4*PI/5)
	{
		rating += config->combatMovementParams.circlingManeuvers;
	}

	if(config->combatMovementParams.randomWeight)
	{
		rating += randomPositiveF32() * config->combatMovementParams.randomWeight;
	}

	if(config->combatMovementParams.turnWeakShieldToMostDamage && hd && s_highest_dir && s_weakest_dir && !nearf(s_highest, s_weakest))
	{
		F32 eval = aiCombatPositionEvalWeakShieldMostDamage(e, aib, pos, yAT_To_Loc_Abs, hd, s_weakest, s_highest);
		rating += eval*config->combatMovementParams.turnWeakShieldToMostDamage;
		if(ratingOut)
			ratingOut->turnWeakShieldToMostDamage = eval*config->combatMovementParams.turnWeakShieldToMostDamage;
	}

	if(config->combatMovementParams.preferArcLimitedLocations>0 && aib->powers->hasArcLimitedPowers)
	{
		F32 eval = aiCombatPositionEvalArcLimitedPowers(e, aib, yMe_Abs, yMe_To_AT_Abs, yMe_To_Loc_Abs);
		rating += eval*config->combatMovementParams.preferArcLimitedLocations;
		if(ratingOut)
			ratingOut->preferArcLimitedLocations = eval*config->combatMovementParams.preferArcLimitedLocations;
	}

	if(config->combatMovementParams.yOffset)
	{
		F32 eval = aiCombatPositionEvalYOffset(e, aib, attackTargetPos, pos);

		rating += eval * config->combatMovementParams.yOffset;

		if(ratingOut)
			ratingOut->yOffset = eval * config->combatMovementParams.yOffset;
	}

	return rating;
}

static int aiCombatGetCurRelLocation(Entity *e, AIVarsBase *aib)
{
	AIConfig *config = aiGetConfig(e, aib);

	if (config->useCombatPositionSlots)
	{
		Vec3 vEntPos;
		entGetPos(e, vEntPos);
		return aiCombatGetRelLocationFromPos(e, aib, vEntPos);
	}
	else
	{
		F32 targetAngle;
		Vec3 attackTargetPos;
		Vec3 targetVec;
		Vec3 ePos;
		int location;

		entGetPos(e, ePos);

		entGetPos(aib->attackTarget, attackTargetPos);
		subVec3(attackTargetPos, ePos, targetVec);
		targetVec[1] = 0;
		normalVec3(targetVec);
		targetAngle = getVec3Yaw(targetVec);
		location = aiCombatGetRelLocationFromAngle(targetAngle+PI);

		return location%aiGlobalSettings.iCombatAngleGranularity;
	}
}

// I changed this function to add capsule radii so that the AI choose better positions to try to achieve their desired ranges.
// This is still not correct for critters that have non-upright capsules.  [RMARR - 12/16/10]
static int aiCombatGetBestPosition(Entity *e, AIVarsBase *aib, F32 targetRange, Vec3 bestPosOut)
{
	AIConfig *config = aiGetConfig(e, aib);
	F32 bestRating = -FLT_MAX;
	int bestLocation = 0;
	Vec3 bestPos = {0,0,0};
	int i;
	int curLocation;
	Quat attackTargetRot;
	Vec3 attackTargetPyr;
	Vec3 targetDir;
	Vec3 targetPos;
	Vec3 myPos;
	Entity *highestDamage = NULL;
	F32 fAngleStep;
	int iPartitionIdx = entGetPartitionIdx(e);

	F32 fCapsuleRadiiSum = entGetPrimaryCapsuleRadius(e) + entGetPrimaryCapsuleRadius(aib->attackTarget);
	F32 fCenterDist = targetRange + fCapsuleRadiiSum;

	S32 iNumCombatSlots = config->useCombatPositionSlots ? eaSize(&aiGlobalSettings.eaCombatPositionSlots) : aiGlobalSettings.iCombatAngleGranularity; 

	PERFINFO_AUTO_START_FUNC();

	// This should not happen, but I'm not going to turn it on and crash everyone's game right away.  [RMARR - 12/15/10]
	//devassert(entGetPrimaryCapsule(e) && entGetPrimaryCapsule(aib->attackTarget));

	if(config->combatMovementParams.turnWeakShieldToMostDamage)
		highestDamage = getHighestDamageEnt(e, aib);

	curLocation = aiCombatGetCurRelLocation(e, aib);

	entGetPos(e, myPos);
	entGetPos(aib->attackTarget, targetPos);
	entGetRot(aib->attackTarget, attackTargetRot);
	entGetFacePY(aib->attackTarget, attackTargetPyr);

	eaClearStruct(&aib->ratings, parse_AIDebugLocRating);
	if(aiDebugEntRef == entGetRef(e))
	{
		while(eaSize(&aib->ratings) < iNumCombatSlots)
			eaPush(&aib->ratings, StructCreate(parse_AIDebugLocRating));
	}

	fAngleStep = TWOPI/aiGlobalSettings.iCombatAngleGranularity;
	for(i = 0; i < iNumCombatSlots; i++)
	{
		F32 rating;
		Vec3 curAnglePos;
		AIDebugLocRating *dbgRating;
		Vec3 vecCombatPosSlot;
		F32 curAngle;
		F32 fDist = fCenterDist;
		bool bDecidedToUseCurrentPos = false;

		if (config->useCombatPositionSlots)
		{
			vecCombatPosSlot[0] = aiGlobalSettings.eaCombatPositionSlots[i]->fXOffset;
			vecCombatPosSlot[1] = 0.f;
			vecCombatPosSlot[2] = aiGlobalSettings.eaCombatPositionSlots[i]->fZOffset;
		}

		curAngle = config->useCombatPositionSlots ? getVec3Yaw(vecCombatPosSlot) : fixAngle(i * fAngleStep);

		// If I'm already in this slice, then pick a position near me, to minimize my desire to wiggle around.
		if (i == curLocation)
		{
			F32 fMyActualCurAngle,fDummyPitch;
			Vec3 vOffset;
			F32 fCurDist, fMyActualCurRange;
			F32 prefMinRange = aiGetPreferredMinRange(e, aib);
			F32 prefMaxRange = aiGetPreferredMaxRange(e, aib);

			subVec3(myPos,targetPos,vOffset);
			fCurDist = lengthVec3(vOffset);
			fMyActualCurRange = fCurDist - fCapsuleRadiiSum;

			if (config->useCombatPositionSlots)
			{
				if (fMyActualCurRange >= prefMinRange && fMyActualCurRange <= prefMaxRange)
				{
					bDecidedToUseCurrentPos = true;

					curAngle = getVec3Yaw(vOffset);

					// Use the entity position because we don't want critters to wiggle around
					copyVec3(myPos, curAnglePos);
				}
			}
			else
			{
				getVec3YP(vOffset,&fMyActualCurAngle,&fDummyPitch);
				if (fabsf(subAngle(fMyActualCurAngle,curAngle)) < fAngleStep*0.5f)
				{
					curAngle = fMyActualCurAngle;
				}

				if (fMyActualCurRange >= prefMinRange && fMyActualCurRange <= prefMaxRange)
				{
					fDist = fCurDist;
				}
			}
		}

		if (config->useCombatPositionSlots && !bDecidedToUseCurrentPos)
		{
			// Use the combat position slot
			addVec3(targetPos, vecCombatPosSlot, curAnglePos);		
		}
		else if (!config->useCombatPositionSlots)
		{
			setVec3(targetDir, fDist * sinf(curAngle), 0, fDist * cosf(curAngle));
			addVec3(targetDir, targetPos, curAnglePos);
		}

		dbgRating = eaGet(&aib->ratings, i);

		if(dbgRating)
		{
			ZeroStruct(dbgRating);

			if (config->useCombatPositionSlots)
			{
				dbgRating->combatPosSlotIndex = i;
				dbgRating->vCombatPosSlot[0] = aiGlobalSettings.eaCombatPositionSlots[i]->fXOffset;
				dbgRating->vCombatPosSlot[2] = aiGlobalSettings.eaCombatPositionSlots[i]->fXOffset;
			}
		}

		if(config->combatMovementParams.yOffset)
		{
			AICollideRayResult res = 0;
			// we need to do a walk check to get the correct height
			aiCollideRayEx(iPartitionIdx, aib->attackTarget, targetPos, NULL, curAnglePos, AICollideRayFlag_DOWALKCHECK | AICollideRayFlag_SKIPRAYCAST, AI_DEFAULT_STEP_LENGTH, &res, curAnglePos);

			if(res && res!=AICollideRayResult_END)
			{
				if(dbgRating)
					dbgRating->rayCollResult = res;
				continue;
			}
		}

		rating = aiCombatPositionEval(e, aib, dbgRating, curAnglePos, curAngle, attackTargetPyr[1], highestDamage, fDist-fCapsuleRadiiSum, true);

		// Do not keep going to same point
		if(config->movementParams.continuousCombatMovement && !config->useCombatPositionSlots)
		{
			if(i==curLocation || i==(curLocation+1)%aiGlobalSettings.iCombatAngleGranularity || (i+1)%aiGlobalSettings.iCombatAngleGranularity==curLocation)
			{
				rating -= 2;
				if(dbgRating)
					dbgRating->sameLocPenalty = -2;
			}
		}
		
		if(rating > bestRating)
		{
			bestLocation = i;
			bestRating = rating;
			copyVec3(curAnglePos, bestPos);
		}
	
	}

	if(aib->ratings && aib->ratings[bestLocation])
		aib->ratings[bestLocation]->bestThisTick = 1;

	aib->lastLocRating = bestRating;
	copyVec3(bestPos, bestPosOut);

	AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 7, 
		"aiCombatGetBestPosition: Chose " LOC_PRINTF_STR ", for a range of %f from target at " LOC_PRINTF_STR,
				vecParamsXYZ(bestPos),
				targetRange,
				vecParamsXYZ(targetPos));

	PERFINFO_AUTO_STOP();

	return bestLocation;
}

AUTO_COMMAND_QUEUED();
void aiCombatReachedLastKnownPos(ACMD_POINTER Entity *e, ACMD_POINTER AIVarsBase *aib, EntityRef erTarget)
{
	Entity *target = entFromEntityRefAnyPartition(erTarget);
	AIStatusTableEntry *status;

	if(!target)
		return;

	status = aiStatusFind(e, aib, target, false);
	if(status)
		status->visitedLastKnown = 1;
}

static __forceinline int aiShouldUsePowers(Entity *e, AIVarsBase *aib, AIConfig *config, S64 *minTimeOut)
{
	S64 referenceTime;
	S64 nextAITick;
	int partitionIdx = entGetPartitionIdx(e);

	referenceTime = aiPowersGetPostRechargeTime(e, aib, config);
	if(minTimeOut)
		*minTimeOut = referenceTime;
	
	// This is set by aiKnockDownCritter to tell the AI not to attempt to cheat
	if(aib->powers->notAllowedToUsePowersUntil && aib->powers->notAllowedToUsePowersUntil > ABS_TIME_PARTITION(partitionIdx))
		return false;
	
	// Just a global to disable everything by command
	if(disableAIPowerUsage)
		return false;
	
	// Don't queue any more while you're waiting on another
	if(eaSize(&aib->powers->queuedPowers))
		return false;

	// This guy is powerless!
	if (config->dontUsePowers)
		return false;
	
	// Failsafe - if we haven't used a power for 10 seconds + recharge, use one in case tracking messed up
	if(ABS_TIME_PASSED(aib->time.lastUsedPower, config->globalPowerRecharge + 10))
		return true;

	// We have yet to receive notification from powers that the power is activated
	//  Technically we can lose a bit of time here, but there's little to be done there.
	//  We could possibly queue another power to let the powers system use it ASAP,
	//  but it's not worth the hassle for now.
	if(aib->time.lastUsedPower > aib->time.lastActivatedPower)
		return false;
	
	// Everything after here is intra-powers timing
	// Without a global recharge, we just fire 'em fast.
	if(disableGlobalPowerRecharge || !config->globalPowerRecharge)
		return true;

	// Never used one
	if(!aib->time.lastActivatedPower && !aib->time.lastUsedPower)
		return true;

	nextAITick = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(AI_TICK_TIME(e));

	if(ABS_TIME_PARTITION(partitionIdx) > referenceTime || 
		(!aiGlobalSettings.disableInfraTickPowerUsage && nextAITick > referenceTime))
	{
		return true;
	}
	
	return false;
}

typedef struct CombatMovementQueryOutput
{
	F32 targetRange;
	Vec3 explicitPos;
	int inRange;
	int doCombatMovement;
	int useExplicitPos;
	int getInRange;

	// Used when hit and wait combat movement style is enabled
	S32 brawlerDisengage : 1;
} CombatMovementQueryOutput;

static __forceinline int aiIsInCoherencyMode(AIVarsBase *aib, AIConfig* config)
{
	return (config->combatMovementParams.combatPositioningUseCoherency || aib->leashTypeOverride != AI_LEASH_TYPE_DEFAULT) && 
			aiGetLeashType(aib) != AI_LEASH_TYPE_DEFAULT && 
				(!aib->attackTargetRef || aib->attackTargetRef != aib->preferredTargetRef);
				// if we are attacking our preferred target, ignore coherency.	
}

static int aiCombatMovement_Coherency(Entity *e, AIVarsBase *aib, const Vec3 vCurPos, AIConfig* config, CombatMovementQueryOutput *pOutput)
{
	Vec3 vLeashPos;
	F32 coherencyDist, distToLeashSQ;
	int partitionIdx = entGetPartitionIdx(e);

	aiGetLeashPosition(e, aib, vLeashPos);
	coherencyDist = aiGetCoherencyDist(e, config);
	distToLeashSQ = distance3Squared(vLeashPos, vCurPos);

	if (e->aibase->attackTarget)
	{
		coherencyDist += config->leashRangeCurrentTargetDistAdd + 15.f;
	}

	if (distToLeashSQ > SQR(coherencyDist))
	{
		copyVec3(vLeashPos, pOutput->explicitPos);
		pOutput->useExplicitPos = true;
		aib->time.lastCombatCoherencyCheck = ABS_TIME_PARTITION(partitionIdx);
		return true;
	}
	else if (ABS_TIME_PASSED(aib->time.lastCombatCoherencyCheck, 2.f))
	{
		coherencyDist *= 0.75f; 
		if (distToLeashSQ > SQR(coherencyDist))
		{
			copyVec3(vLeashPos, pOutput->explicitPos);
			pOutput->useExplicitPos = true;
			aib->time.lastCombatCoherencyCheck = ABS_TIME_PARTITION(partitionIdx);
			return true;
		}	
		else
		{
			aib->time.lastCombatCoherencyCheck = 0;
			aiMovementClearMovementTarget(e, aib);
		}
	}

	return false;
}

static __forceinline int aiCombatMovement_ValidatePosition(Entity *e, AIVarsBase *aib,  AIConfig* config, const Vec3 vPos)
{
	if (aiIsInCoherencyMode(aib, config))
		return aiIsPositionWithinCoherency(e, aib, vPos);

	return true;
}

static int aiCombatMovement_AttractVolumeEntries(Entity *e, AIVarsBase *aib, AIConfig* config, const Vec3 vCurPos, F32 prefMaxRange, CombatMovementQueryOutput *pOutput)
{
	#define ATTRACT_VOLUME_THRESHOLD	15.f
	F32 fDistance = 0.f;
	AIVolumeEntry *pVolume;

	pVolume = aiAttractGetClosestWithin(e, aib, ATTRACT_VOLUME_THRESHOLD, &fDistance);
	if (pVolume)
	{	// there is an attract volume pretty close, see if we can find a valid position inside
		// see if we have enough wiggle room with the preferred ranges to even get into the attract volume
		Vec3 vDesiredPos;

		if (fDistance > 0.f)
		{
			Vec3 vVolumeToMe, vVolumePos;
			F32 volumeRadius = aiVolumeEntry_GetRadius(pVolume);
			F32 targetDistance;
			aiVolumeEntry_GetPosition(pVolume, vVolumePos);

			subVec3(vCurPos, vVolumePos, vVolumeToMe);
			normalVec3(vVolumeToMe);
			targetDistance = volumeRadius * randomPositiveF32();
			targetDistance = CLAMP(targetDistance, 5.f, volumeRadius - 5.f);
			scaleAddVec3(vVolumeToMe, targetDistance, vVolumePos, vDesiredPos);

		}
		else
		{
			copyVec3(vCurPos, vDesiredPos);
		}

		if (aiCombatMovement_ValidatePosition(e, aib, config, vDesiredPos))
		{
			// check if we can still attack our target from the desired position
			F32 fDistanceSQ;
			Vec3 vAttackTargetPos;

			entGetPos(aib->attackTarget, vAttackTargetPos);
			fDistanceSQ = distance3Squared(vDesiredPos, vAttackTargetPos);
			if (fDistanceSQ < SQR(prefMaxRange))
			{
				// within our preferred max range, we should be able to attack
				if (fDistance > 0.f)
				{
					pOutput->useExplicitPos = false;
					copyVec3(vDesiredPos, pOutput->explicitPos);
				}
				return true;
			}
		}
	}

	return false;
}

static bool aiCombatCheckRelLocCurrent( Entity *e, int * piActualSlot )
{
	AIVarsBase *aib = e->aibase;
	AIRelativeLocList *pActualList = NULL;
	AIConfig *config = aiGetConfig(e, aib);

	if (aib->attackTarget == NULL)
	{
		return true;
	}

	*piActualSlot = aiCombatGetCurRelLocation(e,aib);

	if (*piActualSlot >= 0 && config->useCombatPositionSlots)
	{
		// Combat position slots are after the pie slices
		pActualList = eaGet(&aib->attackTarget->aibase->relLocLists, aiGlobalSettings.iCombatAngleGranularity + *piActualSlot);
	}
	else if (*piActualSlot >= 0)
	{
		pActualList = eaGet(&aib->attackTarget->aibase->relLocLists, *piActualSlot);
	}

	if (aib->myRelLoc == NULL)
	{
		// we haven't even locked in a location yet, so we'll just say we're current and let the normal code flow choose one for us
		return true;
	}

	if (pActualList == aib->myRelLoc->list)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void aiCombatRecordRelLoc(Entity * e, int iLocation, F32 fRange)
{
	AIVarsBase *aib = e->aibase;
	AIConfig *config = aiGetConfig(e, aib);

	if (iLocation >= 0 && config->useCombatPositionSlots)
	{
		// Combat position slots are after the pie slices
		iLocation += aiGlobalSettings.iCombatAngleGranularity;
	}

	if(!aib->myRelLoc)
		aib->myRelLoc = calloc(1, sizeof(AIRelativeLoc));

	// Let the attackTarget know we're following it
	if(iLocation>=0 && iLocation<eaSize(&aib->attackTarget->aibase->relLocLists))
	{
		// Take myRelLoc off of whatever list it was on before
		if(aib->myRelLoc->list)
		{
			eaFindAndRemoveFast(&aib->myRelLoc->list->locs, aib->myRelLoc);
		}

		// add it to the list for the chosen location
		aib->myRelLoc->entRef = e->myRef;
		aib->myRelLoc->range = fRange;
		eaPush(&aib->attackTarget->aibase->relLocLists[iLocation]->locs, aib->myRelLoc);
		aib->myRelLoc->list = aib->attackTarget->aibase->relLocLists[iLocation];
	}
}


static void aiCombatUpdateRelLocForAttackers(Entity * pTargetEnt,Vec3 const vTargetPos)
{
	AIVarsBase *pTargetAib = pTargetEnt->aibase;
	int iLoc,j;

	S32 iNumCombatPosSlots = eaSize(&aiGlobalSettings.eaCombatPositionSlots);

	// Update ranges only for the pie slices
	for(iLoc=0; iLoc < aiGlobalSettings.iCombatAngleGranularity; iLoc++)
	{
		AIRelativeLocList * list = pTargetAib->relLocLists[iLoc];
		for(j=0; j<eaSize(&list->locs); j++)
		{
			AIRelativeLoc *loc = list->locs[j];
		
			Entity * pAttacker = entFromEntityRefAnyPartition(loc->entRef);
			Vec3 vAttackerPos;
			AIVarsBase *aib = pAttacker->aibase;

			entGetPos(pAttacker, vAttackerPos);

			// See if we need to update our relLoc information
			if (!aib->currentlyMoving)
			{
				// Maybe we want to update our range any time we are not moving, whether the relLoc is current or not!
				int iActualCurrentSlot = -1;
				if (!aiCombatCheckRelLocCurrent(pAttacker,&iActualCurrentSlot))
				{
					// do it
					F32 fCapsuleRadiiSum = entGetPrimaryCapsuleRadius(pAttacker) + entGetPrimaryCapsuleRadius(pTargetEnt);
					F32 fActualRange = distance3(vTargetPos, vAttackerPos)-fCapsuleRadiiSum;
					aiCombatRecordRelLoc(pAttacker,iActualCurrentSlot,fActualRange);
					
					// I can't do this right now, because the low level movement code can refuse to allow me to pathfind, which will cause a chain
					// reaction causing me to choose a better position that I need pathfinding to get to.  Not doing this is a workaround for now,
					// that will make AI less responsive [RMARR 1/26/11]
					//aib->forceThinkTick = 1;
				}
			}
		}
	}

	// Update combat position slots
	for(iLoc = aiGlobalSettings.iCombatAngleGranularity; iLoc < aiGlobalSettings.iCombatAngleGranularity + iNumCombatPosSlots; iLoc++)
	{
		AIRelativeLocList * list = pTargetAib->relLocLists[iLoc];
		for(j = 0; j < eaSize(&list->locs); j++)
		{
			AIRelativeLoc *loc = list->locs[j];

			Entity * pAttacker = entFromEntityRefAnyPartition(loc->entRef);
			AIVarsBase *aib = pAttacker->aibase;

			// See if we need to update our relLoc information
			if (!aib->currentlyMoving)
			{
				// Maybe we want to update our range any time we are not moving, whether the relLoc is current or not!
				int iActualCurrentSlot = -1;
				if (!aiCombatCheckRelLocCurrent(pAttacker, &iActualCurrentSlot))
				{
					aiCombatRecordRelLoc(pAttacker, iActualCurrentSlot, 0.f);
				}
			}
		}
	}
}


static void aiCombatMovementQuery( Entity *e,  AIConfig* config, int allowCombatMovement, const Vec3 attackTargetPos, CombatMovementQueryOutput *pOutput)
{
	AICombatMovementConfig emptyMove = {0};
	AIVarsBase *aib = e->aibase;
	F32 prefMinRange = aiGetPreferredMinRange(e, aib);
	F32 prefMaxRange = aiGetPreferredMaxRange(e, aib);
	F32 fForceMoveMinRange;
	F32 fRangeSize;
	F32 fRangeInset;
	Vec3 bePos;
	F32 fCapsuleRadiiSum = entGetPrimaryCapsuleRadius(e) + entGetPrimaryCapsuleRadius(aib->attackTarget);
	int partitionIdx = entGetPartitionIdx(e);
	AITeam *pTeam = aiTeamGetCombatTeam(e, aib);


	if (prefMinRange > prefMaxRange)
	{
		prefMaxRange = prefMinRange;
	}

	fForceMoveMinRange = prefMinRange;

	if (config->movementParams.meleeCombatMovement)
	{
		// Minimize the AI's desire to counteract your own movements.
		fForceMoveMinRange = 0.0f;
	}

	fRangeSize = prefMaxRange-prefMinRange;

	// cheat in from the edge of the zone, so we don't vibrate.  At most 5, which was the previous default.
	fRangeInset = MIN(5.f,fRangeSize*0.25f);

	entGetPos(e, bePos);

	pOutput->inRange = false;
	pOutput->getInRange = false;
	pOutput->doCombatMovement = false;
	pOutput->brawlerDisengage = aiBrawlerCombat_ShouldDisengage(e, aib, config);
	pOutput->targetRange = 0.f;
		
	if(gDisableAICombatMovement || !allowCombatMovement || config->movementParams.immobile)
	{
		pOutput->inRange = true;
		pOutput->doCombatMovement = false;
		return;
	}
		
	if(aib->leavingAvoid)
		return;

	if(aiIsInCoherencyMode(aib, config))
	{
		if (aiCombatMovement_Coherency(e, aib, bePos, config, pOutput))
		{
			pOutput->doCombatMovement = true;
			return;
		}
	}

	if (pOutput->brawlerDisengage)
	{
		pOutput->doCombatMovement = true;
		return;
	}
	
	if(eaSize(&aib->attract.base.volumeEntities))
	{
		if (aiCombatMovement_AttractVolumeEntries(e, aib, config, bePos, prefMaxRange, pOutput))
			return;
	}

	// check the relloc for everyone on my target.  Inefficient?
	aiCombatUpdateRelLocForAttackers(aib->attackTarget, attackTargetPos);

	if (config->movementParams.meleeMovementOffset && aib->myRelLoc == NULL)
	{
		pOutput->doCombatMovement = true;
	}
	else if(!aib->attackTargetStatus->visible && 
			(!aiGlobalSettings.disableLeashingOnNonStaticMaps || zmapInfoGetMapType(NULL)==ZMTYPE_STATIC || (pTeam && pTeam->bLeashOnNonStaticOverride)))
	{
		if((!aib->currentlyMoving || config->movementParams.continuousCombatMovement) && 
			ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastTriedToFollow) > SEC_TO_ABS_TIME(5) &&
			aib->attackTargetStatus->time.lastVisible)
		{
			aib->time.lastTriedToFollow = ABS_TIME_PARTITION(partitionIdx);
			pOutput->doCombatMovement = true;
			pOutput->targetRange = prefMinRange + fRangeInset;
		}
		else
			pOutput->doCombatMovement = false;
	}
	else if(config->movementParams.continuousCombatMovement && !aib->currentlyMoving)
	{
		pOutput->doCombatMovement = true;
	}
	else if(prefMaxRange && (aib->attackTargetDistSQR < SQR(fForceMoveMinRange) ||
								aib->attackTargetDistSQR > SQR(prefMaxRange)))
	{
		if(overrideCombatMaxRunAwayCount && aib->combatRunAwayCount >= overrideCombatMaxRunAwayCount ||
			config->combatMaxRunAwayCount && aib->combatRunAwayCount >= config->combatMaxRunAwayCount)
		{
			if(config->movementParams.continuousCombatMovement || aib->currentlyMoving)
			{
				pOutput->inRange = false;
			}
			else
			{
				pOutput->inRange = true;
			}

			pOutput->doCombatMovement = false;
			pOutput->targetRange = distance3(attackTargetPos, bePos) - fCapsuleRadiiSum;
		}
		else
		{
			if(!config->movementParams.continuousCombatMovement && ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastCombatMovement) < SEC_TO_ABS_TIME(3))
			{
				// This is the hard limit on doCombatMovement more often than every 3 seconds (for this case)
				pOutput->doCombatMovement = false;
			}
			else if(aib->time.startedRunningAway && ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.startedRunningAway) > SEC_TO_ABS_TIME(config->prefMaxRangeMoveTime * 2))
			{
				// We feel that we need to back away from our target, and it's been long enough
				pOutput->doCombatMovement = true;
			}
			else if(aib->attackTargetDistSQR > SQR(prefMaxRange))
			{
				// We feel that we are out of range!
				pOutput->doCombatMovement = true;
				pOutput->getInRange = true;
			}
			else if(ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastInPreferredRange) > SEC_TO_ABS_TIME(config->prefMaxRangeMoveTime) &&
						aib->time.lastInPreferredRange >= aib->time.startedRunningAway)
			{
				pOutput->doCombatMovement = true;
			}
			else
			{
				pOutput->doCombatMovement = false;
				pOutput->targetRange = distance3(attackTargetPos, bePos) - fCapsuleRadiiSum;  // Don't run away
			}
		}
	}
	else if(config->combatMovementParams.timedPositionChanges &&
		ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastTargetUpdate) > SEC_TO_ABS_TIME(config->combatMovementParams.positionChangeTime))
	{
		pOutput->inRange = false;
		pOutput->doCombatMovement = true;
	}
	
	// Ok, we don't have to move cause of the above checks, but let's keep going.
	if (!pOutput->doCombatMovement && !config->combatMovementParams.skipPositionRevaluation)
	{
		if(!config->combatMovementParams.timedPositionChanges &&
			memcmp(&config->combatMovementParams, &emptyMove, sizeof(AICombatMovementConfig)) &&
			(!config->movementParams.continuousCombatMovement || ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastTargetUpdate)>SEC_TO_ABS_TIME(5)))
		{
			// Check if position rating is bad... but for continuous movers, only once per 5 seconds
			F32 rating = 0.0f;
			F32 fCurrentLocRating = -9999.9f;
			F32 curAngle;
			Vec3 curTargetPos; // (target means destination here)
			Vec3 curTargetDir;
			Entity *highestDamage;
			Quat attackRot;
			Vec3 attackPyr;
			F32 mag;
			bool bTookShortcut = false;

			F32 targetRange = distance3(attackTargetPos, bePos) - fCapsuleRadiiSum;  // Don't run away
			entGetRot(aib->attackTarget, attackRot);
			quatToPYR(attackRot, attackPyr);

			if(!aiMovementGetTargetPosition(e, aib, curTargetPos))  // Eval where I'm trying to go, not where I am
				copyVec3(bePos, curTargetPos);  // No movement target - oh well, eval where I am
			else
			{
				int xy = 0;
				++xy;
			}
			highestDamage = getHighestDamageEnt(e, aib);

			subVec3(curTargetPos, attackTargetPos, curTargetDir);
			curAngle = getVec3Yaw(curTargetDir);

			mag = aiCombatPositionGetWeightMagnitude(&config->combatMovementParams);
			rating = aiCombatPositionEval(e, aib, NULL, curTargetPos, curAngle, attackPyr[1], highestDamage, targetRange, false);

			if (aib->currentlyMoving && !config->useCombatPositionSlots && aib->attackTargetStatus->visible)
			{
				// Also eval our current actual position, to see if it would make a better choice than the position I am trying to get to
				F32 fActualCurRange;
				Vec3 vOffset;
				F32 fShortcutRange;
				Vec3 vShortcutPos;
				F32 fShortcutAngle;

				subVec3(bePos, attackTargetPos, vOffset);
				fActualCurRange = lengthVec3(vOffset)-fCapsuleRadiiSum;

				if (fActualCurRange < prefMaxRange)
				{
					fShortcutAngle = getVec3Yaw(vOffset);
					fShortcutRange = fActualCurRange;
					if (fShortcutRange < fForceMoveMinRange)
					{
						fShortcutRange = fForceMoveMinRange;
					}

					normalVec3(vOffset);
					scaleAddVec3(vOffset,fShortcutRange+fCapsuleRadiiSum,attackTargetPos,vShortcutPos);

					fCurrentLocRating = aiCombatPositionEval(e, aib, NULL, vShortcutPos, fShortcutAngle, attackPyr[1], highestDamage, fShortcutRange, false);
					if (fCurrentLocRating > rating+0.01f*mag)
					{
						// omg just stop!!
						Vec3 vTargetVec;
						int listIndex = config->useCombatPositionSlots ? aiCombatGetRelLocationFromPos(e, aib, vShortcutPos) : aiCombatGetRelLocationFromAngle(fShortcutAngle);
						if (listIndex >= 0)
						{
							aiCombatRecordRelLoc(e,listIndex,fShortcutRange);
						}						

						if (config->movementParams.meleeMovementOffset && fShortcutRange == fActualCurRange)
						{
							aiMovementResetPath(e,aib);
						}
						else
						{
							if (config->movementParams.meleeCombatMovement)
							{
								if (config->movementParams.meleeMovementOffset)
								{
									subVec3(vShortcutPos, attackTargetPos, vTargetVec);
									aiMovementSetTargetEntity(e, aib, aib->attackTarget, vTargetVec, 0, AI_MOVEMENT_ORDER_ENT_COMBAT_MOVETO_OFFSET, 0);
								}
								else
								{
									aiMovementSetTargetEntity(e, aib, aib->attackTarget, NULL, 0, AI_MOVEMENT_ORDER_ENT_COMBAT, 0);
								}
							}
							else
							{
								aiMovementSetTargetPosition(e, aib, vShortcutPos, NULL, 0);
							}
						}
						bTookShortcut = true;
					}
				}
			}


			if (!bTookShortcut)
			{
				pOutput->targetRange = prefMinRange + fRangeInset;
				if(rating < aib->lastLocRating-0.2f*mag)
				{
					//our position just isn't so great anymore
					pOutput->doCombatMovement = true;
				}
				else if((config->movementParams.continuousCombatMovement || config->combatMovementParams.periodicMovementUpdate) && 
							ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastTargetUpdate) > SEC_TO_ABS_TIME(5))
				{
					pOutput->doCombatMovement = true;
				}
				else
				{
					pOutput->doCombatMovement = false;
				}

				if (rating > aib->lastLocRating)
				{
					// our current position got better!  Make sure we keep track of this so we don't keep comparing to old info
					aib->lastLocRating = rating;
				}
			}

			pOutput->inRange = !pOutput->doCombatMovement;
		}
		else
		{
			pOutput->inRange = true;
			pOutput->doCombatMovement = false;
		}
	}
 
	// Add "Engaged Range" to AIConfig. If set and an enemy is within that range, the critter will not try to do any combat movement.
	// This allows NNO to set the ranged guys to try and run away, UNLESS they get in melee range with someone. [bzeigler  7/28/2009]
	if (config->combatEngagedRange && aib->attackTargetDistSQR < SQR(config->combatEngagedRange))
	{
		// This is bad code, because it can trump any number of reasons to move, like standing in fire [RMARR - 6/8/11]
		pOutput->inRange = true;
		pOutput->doCombatMovement = false;
		pOutput->targetRange = distance3(attackTargetPos, bePos) - fCapsuleRadiiSum;
		if(!aiStickyEnemies && aib->currentlyMoving)
			aiMovementResetPath(e, aib);
	}

	if(pOutput->doCombatMovement && !pOutput->targetRange)
	{
		// try to offset the player from the edge of the ranges, so we don't get jitter
		if(aib->attackTargetDistSQR < SQR(prefMinRange))
		{
			aib->combatRunAwayCount++;
			aib->time.startedRunningAway = ABS_TIME_PARTITION(partitionIdx);
			pOutput->targetRange = prefMinRange + fRangeInset;
		}
		else
			pOutput->targetRange = MAX(2, prefMaxRange - fRangeInset); // I don't like this hard-coded 2 [RMARR - 11/16/10]
	}

	{
		F32 localPerceptionRadius = character_GetPerceptionDist(e->pChar, aib->attackTarget->pChar);
		if(pOutput->targetRange > localPerceptionRadius)
		{
			pOutput->inRange = false;
			pOutput->doCombatMovement = true;

			pOutput->targetRange = MAX(localPerceptionRadius-1, 0);
		}
	}
}


static void aiDoCombatMovement_TargetNoLongerVisible(Entity *e, AIVarsBase *aib, AIConfig *config, const Vec3 bePos)
{
	const F32 *pTargetPos = NULL;
	AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, aib->attackTargetStatus);

	if (!teamStatus)
		return;

	pTargetPos = teamStatus->lastKnownPos;
	if (aiCombatMovement_ValidatePosition(e, aib, config, pTargetPos))
	{
		if (aib->attackTargetStatus->eNotVisibleReason != EAINotVisibleReason_PERCEPTION_STEALTH &&
			aib->attackTargetStatus->eNotVisibleReason != EAINotVisibleReason_UNTARGETABLE)
		{
			// This needs to be critical, or else a recent pathfind will make the critter look stupid, 
			//  and it gives better chances of catching the hiding target
			aiMovementSetTargetPosition(e, aib, teamStatus->lastKnownPos, NULL, AI_MOVEMENT_TARGET_CRITICAL);

			if(distance3Squared(teamStatus->lastKnownPos, bePos)<SQR(4))
				aib->attackTargetStatus->visitedLastKnown = 1;
		}
		else
		{
			//We clear the targets when visibility is lost due to perception
			// in order to prevent the critters from crowding around the spot where a player used stealth
			aiMovementClearRotationTarget(e, aib);
			aiMovementClearMovementTarget(e, aib);

			if (aiGlobalSettings.pchStealthReactAnimList && 
				aib->attackTargetStatus->eNotVisibleReason == EAINotVisibleReason_PERCEPTION_STEALTH && 
				aib->notVisibleTargetAnimQueue == NULL)
			{
				AIAnimList *pAnimList = NULL;
				
				pAnimList = RefSystem_ReferentFromString(g_AnimListDict, aiGlobalSettings.pchStealthReactAnimList);
				if (pAnimList)
					aiAnimListSet(e, pAnimList, &aib->notVisibleTargetAnimQueue);
			}

		}
	}
}

static bool aiDoCombatMovement_Melee(Entity *e, AIVarsBase *aib, const Vec3 attackTargetPos, 
									 AIConfig* config, int usePowers, CombatMovementQueryOutput *pInput)
{
	int location;
	Vec3 targetPos;
	Vec3 targetVec;
	int partitionIdx = entGetPartitionIdx(e);
	if(usePowers && aib->powers->hasLungePowers && randomPositiveF32() < 0.33)
	{
		S32 i;

		for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
		{
			AIPowerInfo* curPowInfo = aib->powers->powInfos[i];
			AIPowerConfig* curPowConfig = aiGetPowerConfig(e, aib, curPowInfo);

			if(!curPowInfo->isLungePower)
				continue;
			if(aib->attackTargetDistSQR < SQR(curPowConfig->minDist) ||
				aib->attackTargetDistSQR > SQR(curPowConfig->maxDist))
				continue;
			if(!aiPowersAllowedToExecutePower(e, aib, aib->attackTarget, curPowInfo, curPowConfig, NULL))
				continue;

			aiUsePower(e, aib, curPowInfo, aib->attackTarget, aib->attackTarget, NULL, 0, NULL, false, false);
			return true;
		}
	}

	if(config->movementParams.meleeMovementOffset)
	{
		aib->time.lastTargetUpdate = ABS_TIME_PARTITION(partitionIdx);

		location = aiCombatGetBestPosition(e, aib, pInput->targetRange, targetPos);
		subVec3(targetPos, attackTargetPos, targetVec);

		aiCombatRecordRelLoc(e,location,pInput->targetRange);

		aiMovementSetTargetEntity(e, aib, aib->attackTarget, targetVec, 0, AI_MOVEMENT_ORDER_ENT_COMBAT_MOVETO_OFFSET, 0);
	}
	else
	{	
		if (!config->movementParams.meleeMovementUseRange || !pInput->targetRange)
		{
			aiMovementSetTargetEntity(e, aib, aib->attackTarget, NULL, 0, AI_MOVEMENT_ORDER_ENT_COMBAT, 0);
		}
		else
		{
			Vec3 vRange = {0};
			vRange[0] = pInput->targetRange;
			aiMovementSetTargetEntity(e, aib, aib->attackTarget, vRange, false, AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE_DIST, 0);
		}
	}

	return false;
}

static void aiDoCombatMovement_Default(Entity *e, AIVarsBase *aib, AIConfig* config, 
									   CombatMovementQueryOutput *pInput, 
									   const Vec3 bePos, const Vec3 attackTargetPos)
{
	AICombatMovementConfig emptyMove = {0};
	Vec3 targetVec;
	Vec3 targetPos;
	Vec3 targetPosFinal;
	F32 angle;
	int location;
	int partitionIdx = entGetPartitionIdx(e);
	AIConfig *targetconfig = NULL;

	aib->time.lastTargetUpdate = ABS_TIME_PARTITION(partitionIdx)+SEC_TO_ABS_TIME(randomPositiveF32()*0.5);

	// if you can't see someone anymore just go anywhere around them
	if(aib->attackTargetStatus->visible)
		angle = RAD(config->maxRunAwayAngle) / 2;
	else
		angle = RAD(180);

	if(aib->attackTarget)
		targetconfig = aiGetConfig(aib->attackTarget, aib->attackTarget->aibase);

	if(memcmp(&config->combatMovementParams, &emptyMove, sizeof(AICombatMovementConfig)))
	{
		location = aiCombatGetBestPosition(e, aib, pInput->targetRange, targetPos);
		subVec3(targetPos, attackTargetPos, targetVec);
		normalVec3(targetVec);
	}
	else
	{
		F32 targetAngle;
		subVec3(bePos, attackTargetPos, targetVec);
		copyVec3(attackTargetPos, targetPos);
		targetVec[1] = 0;
		normalVec3(targetVec);
		targetAngle = getVec3Yaw(targetVec);
		location = floor((fixAngle(targetAngle)+PI)/(2*PI/aiGlobalSettings.iCombatAngleGranularity)+0.5);
	}

	scaleVec3(targetVec, pInput->targetRange, targetVec);

	if(SAFE_MEMBER2(targetconfig, targetedConfig, yMin) != 0 || SAFE_MEMBER2(targetconfig, targetedConfig, yMax) != 0)
	{
		F32 diff = targetconfig->targetedConfig->yMax - targetconfig->targetedConfig->yMin;
		F32 targetY = attackTargetPos[1] + randomF32() * diff + targetconfig->targetedConfig->yMin;
		
		targetPos[1] = targetY;
		targetVec[1] = targetPos[1] - bePos[1];
	}
	else if(config->combatMaximumYVariance)
	{
		if(!aib->combatCurYVariance || ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastChangedCombatYVariance) > SEC_TO_ABS_TIME(15))
		{
			aib->combatCurYVariance = randomF32() * config->combatMaximumYVariance;
			aib->time.lastChangedCombatYVariance = ABS_TIME_PARTITION(partitionIdx);
		}

		if(targetPos[1] > bePos[0])
			targetVec[1] += aib->combatCurYVariance;
		else
			targetVec[1] -= aib->combatCurYVariance;
	}

	if(aiFindRunToPos(e, aib, bePos, attackTargetPos, targetVec, targetPos, targetPosFinal, angle) 
		|| aiMovementGetFlying(e,aib))
	{
		if (! aiCombatMovement_ValidatePosition(e, aib, config, targetPosFinal))
		{
			return;
		}

		if(aiPredictedPositionDistance)
		{
			Vec3 lastFrameVel;
			if(mmGetVelocityFG(aib->attackTarget->mm.movement, lastFrameVel))
			{
				scaleVec3(lastFrameVel, aiPredictedPositionDistance, lastFrameVel);
				addVec3(targetPosFinal, lastFrameVel, targetPosFinal);
			}
		}

		aiCombatRecordRelLoc(e,location,pInput->targetRange);

		aiMovementSetTargetPosition(e, aib, targetPosFinal, NULL, 0);
	}
	else
	{
		if (! aiCombatMovement_ValidatePosition(e, aib, config, attackTargetPos))
		{
			return;
		}

		aiMovementSetTargetPosition(e, aib, attackTargetPos, NULL, 0);
	}
}


static void aiDoCombatMovement(Entity *e, AIConfig* config, CombatMovementQueryOutput *pInput, int usePowers, const Vec3 attackTargetPos)
{
	AIVarsBase *aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);

	if(!pInput->doCombatMovement)
	{
		if(pInput->inRange && aib->time.startedRunningAway)
		{
			aib->time.startedRunningAway = 0;
			aib->time.lastInPreferredRange = ABS_TIME_PARTITION(partitionIdx);
		}
		return;
	}

	aib->time.lastCombatMovement = ABS_TIME_PARTITION(partitionIdx);

	// Clear out run walk distance because we want the entity to be in full speed in combat
	aiMovementSetWalkRunDist(e, aib, 0.f, 0.f, 0);
	
	if (pInput->useExplicitPos)
	{
		aiMovementSetTargetPosition(e, aib, pInput->explicitPos, NULL, 0);
		return;
	}

	if (pInput->brawlerDisengage)
	{
		aiBrawlerCombat_DoBackAwayAndWanderMovement(e, config, attackTargetPos);
		return;
	}

	if(!pInput->inRange)
	{
		Vec3 bePos;
		entGetPos(e, bePos);

		// guys spawning inside of you can't move intelligently anyway...
		if(sameVec3(bePos, attackTargetPos))
			pInput->doCombatMovement = false;

		if(pInput->doCombatMovement)
		{
			if(!aib->attackTargetStatus->visible)
			{
				aiDoCombatMovement_TargetNoLongerVisible(e, aib, config, bePos);
			}
			else if(config->movementParams.meleeCombatMovement || 
					(aiGlobalSettings.forceMeleeCombatMovement && pInput->targetRange < aiGlobalSettings.forceMeleeCombatMovementThreshold))
			{
				aiDoCombatMovement_Melee(e, aib, attackTargetPos, config, usePowers, pInput);
			}
			else
			{
				aiDoCombatMovement_Default(e, aib, config, pInput, bePos, attackTargetPos);
			}
		}
	}
	else
	{
		aib->time.startedRunningAway = 0;
		aib->time.lastInPreferredRange = ABS_TIME_PARTITION(partitionIdx);
		if(!aiStickyEnemies && aib->currentlyMoving && !config->movementParams.continuousCombatMovement)
			aiMovementResetPath(e, aib);
	}

	return;
}

static void aiUpdateGrieved(Entity* e, AIConfig* config)
{
	int grieved;
	AIVarsBase* aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);

	if(s_disableGrieved)
		grieved = false;
	else if(ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.startedGrievedState) < SEC_TO_ABS_TIME(config->grievedRecoveryTime))
		grieved = true;
	else if(aib->time.endedGrievedState < aib->time.startedGrievedState ||
		ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.endedGrievedState) < SEC_TO_ABS_TIME(config->grievedRecoveryTime))
	{
		grieved = false;
	}
	else
	{
		S64 lastActionTime = MAX(aib->attackTargetStatus->time.becameAttackTarget, aib->time.lastUsedPower);
		lastActionTime = MAX(lastActionTime, aib->time.lastActivatedPower);

		if(ABS_TIME_SINCE_PARTITION(partitionIdx, lastActionTime) > SEC_TO_ABS_TIME(config->grievedRangedAttackTime))
			grieved = true;
		else
			grieved = false;
	}

	if(aib->grieved && !grieved)
	{
		aib->time.endedGrievedState = ABS_TIME_PARTITION(partitionIdx);
		aib->grieved = false;
	}
	else if(!aib->grieved && grieved)
	{
		aib->time.startedGrievedState = ABS_TIME_PARTITION(partitionIdx);
		aib->grieved = true;
	}
}

static int aiCombatShouldFaceTarget(Entity* pEnt, AIVarsBase* aib, AIConfig* pConfig)
{
	return	aiMovementGetTurnRate(pEnt)==0.0 && 
			!pConfig->movementParams.dontRotate && 
			!pConfig->combatMovementParams.combatDontFaceTarget;
}

void aiCombatEnableFaceTarget(Entity* pEnt, S32 bEnable)
{
	pmEnableFaceSelected(pEnt, bEnable);
	
	if (pEnt->mm.mrDragon)
	{
		EntityRef erFace = bEnable ? pEnt->aibase->attackTargetRef : 0;
		mrDragon_SetTargetFaceEntity(pEnt->mm.mrDragon, erFace);
	}
}

void aiCombatSetFaceTarget(Entity *pEnt, EntityRef erAttackTarget)
{
	EntityRef erPowersFacing = pmGetSelectedTarget(pEnt);
	
	if (pEnt->mm.mrDragon)
	{
		mrDragon_SetTargetFaceEntity(pEnt->mm.mrDragon, erAttackTarget);
	}

	// check to make sure the powers movement is facing our new target if not, force the update of the target. 
	if (erPowersFacing != erAttackTarget)
	{
		pmUpdateSelectedTarget(pEnt, true);
	}
}


static int aiDoAttackAction(Entity* e, AIVarsBase* aib, int usePowers, int allowCombatMovement)
{

	AIConfig* config = aiGetConfig(e, aib);
	CombatMovementQueryOutput movementQueryOutput = {0};
	Vec3 attackTargetPos;
	S64 minTimeToUse = 0;

	
	if (aib->chainLockedMovement)
		allowCombatMovement = false;

	aib->lastAttackActionAllowedMovement = !!allowCombatMovement;
	
 	if(!aib->attackTarget) // this gets updated before the behavior tick so should be valid
		return false; // no behavior for losing your attack target yet

	if(!aib->doAttackAI)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if(aib->attackTargetStatus->visible)
		entGetPos(aib->attackTarget, attackTargetPos);
	else
	{
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, aib->attackTargetStatus);
		copyVec3(teamStatus->lastKnownPos, attackTargetPos);
	}

	aiUpdateGrieved(e, config);

	if (config->pBrawlerCombatConfig && aiGlobalSettings.pBrawlerConfig)
		aiBrawlerCombat_Update(e, aib, config);
	
	aiCombatMovementQuery(e, config, allowCombatMovement, attackTargetPos, &movementQueryOutput);
	
	aiDoCombatMovement(e, config, &movementQueryOutput, usePowers, attackTargetPos);
	
	if (aib->notVisibleTargetAnimQueue && 
		(!aib->attackTargetStatus || aib->attackTargetStatus->eNotVisibleReason != EAINotVisibleReason_PERCEPTION_STEALTH))
	{
		CommandQueue_ExecuteAllCommands(aib->notVisibleTargetAnimQueue);
		aib->notVisibleTargetAnimQueue = NULL;
	}

	if (movementQueryOutput.brawlerDisengage)
		usePowers = false;

	if(aiCombatShouldFaceTarget(e, aib, config))
	{
		if(aib->attackTargetStatus->visible)
		{
			aiMovementSetFinalFaceEntity(e, aib, aib->attackTarget);
			aiCombatEnableFaceTarget(e, true);
		}
		else
		{
			aiMovementSetFinalFacePos(e, aib, attackTargetPos);
			aiCombatEnableFaceTarget(e, false);
		}
	}

	if(!usePowers)
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if(aiShouldUsePowers(e, aib, config, &minTimeToUse))
	{
		aiUsePowers(e, aib, config, false, !config->dontRandomizePowerUse, minTimeToUse);
	}

	PERFINFO_AUTO_STOP();
	return true;
}

void aiCombatOnPowerExecuted(Entity *e, AIVarsBase *aib, AIPowerInfo *info)
{
	U32 isPlayerOwnedPet = entIsPlayer(entGetOwner(e));
	
	AIConfig* config = aiGetConfig(e, aib);
	S32 iPartitionIdx = entGetPartitionIdx(e);
	bool bHitAndWaitCombatMovementIsActivated = false;

	if (!aib->chainPowerExecutionActive)
	{
		if (aib->chainLockedFacing)
		{
			aiMovementSetRotationFlag(e, false);
			aib->chainLockedFacing = false;
		}
		aib->chainLockedMovement = false;
	}

	if(info && aib->inCombat &&
		(!aiGlobalSettings.disableOnExecutePowerUsage ||
		bHitAndWaitCombatMovementIsActivated ||
		(aiGlobalSettings.overrideOnExecutePowerUsageForPlayerPets && isPlayerOwnedPet)))
	{
		AIGroupCombatSettings *aigcSettings = NULL;
		AITeam *combatTeam = aiTeamGetCombatTeam(e, aib);

		if(!info->isAttackPower)
			return;

		aigcSettings = GET_REF(combatTeam->aigcSettings);
		if(aigcSettings && aigcSettings->tokenSetting==AITS_TokensForAttacks)
		{
			AITeamMember *combatMember = aiTeamGetMember(e, aib, combatTeam);

			if(combatMember->numCombatTokens>=1)
			{
				combatMember->numCombatTokens -= 1;
			}
			else
			{
				return;
			}
		}

		aiDoAttackAction(e, aib, true, aib->lastAttackActionAllowedMovement);
	}
}

static void aiCombatMoveToLeash(Entity *be, AIVarsBase* aib, const Vec3 vLeashPos)
{
	AILeashType eLeashType = aiGetLeashType(aib);
	switch (eLeashType)
	{
		xcase AI_LEASH_TYPE_ENTITY:
		case AI_LEASH_TYPE_OWNER:
		{
			AIConfig* petConfig;
			Vec3 vCurPos, vToLeash, vEntityLeashPos;
			Entity* leashEnt;

			if (eLeashType == AI_LEASH_TYPE_ENTITY)
			{
				leashEnt = aib->erLeashEntity ? entFromEntityRef(entGetPartitionIdx(be), aib->erLeashEntity) : NULL;
			}
			else
			{
				leashEnt = be->erOwner ? entFromEntityRef(entGetPartitionIdx(be), be->erOwner) : NULL;
			}

			if (!leashEnt)
				return; 
			
			petConfig = aiGetConfig(be, aib);

			entGetPos(leashEnt, vEntityLeashPos);
			entGetPos(be, vCurPos);
			if (gConf.bNewAnimationSystem && fabsf(petConfig->movementParams.movementYOffset) > 0.000001f) {
				//doing this here to prevent feedback since the movementYOffset is introduced when determining the entity's target position -> change in the entity's position -> the position when you then grab it here
				vCurPos[1] -= petConfig->movementParams.movementYOffset;
			}

			subVec3(vEntityLeashPos, vCurPos, vToLeash);
			normalVec3(vToLeash);
			scaleVec3(vToLeash, 5.f, vToLeash);

			//	TODO: get a better movement offset ?
			aiMovementSetTargetEntity(be, aib, leashEnt, vToLeash, false, AI_MOVEMENT_ORDER_ENT_COMBAT, 0);
		}
		xcase AI_LEASH_TYPE_RALLY_POSITION:
		default:
		{
			//	do we want any movement offset? formations?
			aiMovementSetTargetPosition(be, aib, vLeashPos, NULL, 0);
		}
	}
}

void aiFormation_DoFormationMovement(Entity *e)
{
	Entity *eOwner = entGetOwner(e);
	if (eOwner)
	{
		aiMovementSetTargetEntity(e, e->aibase, eOwner, e->aibase->spawnOffset, 1, 
			AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET, AI_MOVEMENT_TARGET_CRITICAL);
	}
}

static void aiCombatCoherencyReposition(Entity* e, AIVarsBase* aib)
{
	Vec3 vCurPos;
	Vec3 vLeashPos;
	Vec3 vCurTargetMoveToPos;
	AIConfig* config = aiGetConfig(e, aib);
	F32 coherencyDist = aiGetCoherencyDist(e, config);
	AILeashType eLeashType = aiGetLeashType(aib);
	
	entGetPos(e, vCurPos);

	aiGetLeashPosition(e, aib, vLeashPos);
	
	// this could be a setting, but for now if the rally is the owner use the formation offset
	if (eLeashType == AI_LEASH_TYPE_OWNER)
	{
		aiFormation_DoFormationMovement(e);
		return;
	}
	else if (eLeashType == AI_LEASH_TYPE_ENTITY && aib->pchCombatRole)
	{	// see if out combat role gives us a coherency position
		if (aiCombatRole_RequestGuardPosition(aib->team, e))
			return;
	}
	

	// Otherwise we try to run back to inside our coherency
	if (aiMovementGetTargetPosition(e, aib, vCurTargetMoveToPos))
	{	// we are currently moving to some position, 
		// validate that it is within our coherency 
		F32 fDesiredCoherencyDist;
	
		F32 fDistToLeashSQ = distance3Squared(vLeashPos, vCurTargetMoveToPos);
		if (fDistToLeashSQ > SQR(coherencyDist))
		{	// our target movement position is not within the coherency distance
			aiCombatMoveToLeash(e, aib, vLeashPos);
			return;
		}
		
		fDesiredCoherencyDist = coherencyDist * .75f;
		// our current distance to the leash
		fDistToLeashSQ = distance3Squared(vCurTargetMoveToPos, vCurPos);
		if (fDistToLeashSQ <= SQR(5.f))
		{	// close enough, stop movement
			aiMovementClearMovementTarget(e, aib);
		}
	}
	else
	{
		F32 fMaxCoherencyDist = coherencyDist * 1.05f;
		F32 fLeashDistSQR = distance3Squared(vLeashPos, vCurPos);
		if (fLeashDistSQR >= SQR(fMaxCoherencyDist))
		{
			aiCombatMoveToLeash(e, aib, vLeashPos);
			return;
		}
	}
}

// ----------------------------------------------------------------------------------------------------------------
static int aiDoAssigmentAction(Entity* e, AIVarsBase* aib, AITeamMemberAssignment *pAssignment,
							   int allowCombatMovement)
{
	AIPowerActionType type;
	AIPowerInfo *pPowInfo;
	int didAction;
	U32 uMTAFlags = MTAFlag_FORCEUSETARGET;
	int partitionIdx = entGetPartitionIdx(e);

	switch (pAssignment->type)
	{
		xcase AITEAM_ASSIGNMENT_TYPE_HEAL:
			type = AI_POWER_ACTION_HEAL;
		xcase AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL:
			type = AI_POWER_ACTION_SHIELD_HEAL;
		xcase AITEAM_ASSIGNMENT_TYPE_BUFF:
			type = AI_POWER_ACTION_BUFF;
		xcase AITEAM_ASSIGNMENT_TYPE_RESSURECT:
			type = AI_POWER_ACTION_RES;
		xcase AITEAM_ASSIGNMENT_TYPE_CURE:
		default:
			type = AI_POWER_ACTION_USE_POWINFO;
	}

	
	if (!allowCombatMovement)
		uMTAFlags |= MTAFlag_COMBATNOMOVE;

	pPowInfo = aiPowersFindInfoByID(e, aib, pAssignment->powID);
	didAction = aiMultiTickAction_QueuePower(e, aib, pAssignment->target->memberBE, type, pPowInfo, uMTAFlags, NULL);
	
	if(didAction)
	{
		devassert(pAssignment->type > AITEAM_ASSIGNMENT_TYPE_NULL && 
					pAssignment->type < AITEAM_ASSIGNMENT_TYPE_COUNT);
		pAssignment->target->timeLastActedOn[pAssignment->type] = ABS_TIME_PARTITION(partitionIdx);
		pAssignment->forcedAssignment = false;
	}
	
	return didAction;
}

// ----------------------------------------------------------------------------------------------------------------
static void aiPickFightAction(Entity* e, AIVarsBase* aib, int doCombatMovement, ExprContext* context)
{
	AIConfig* config = aiGetConfig(e, aib);
	AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);
	AITeamMember* combatMember = aiGetTeamMember(e, aib);
	F32 total = 0.f;
	F32 curAttackWeight = 0.f;
	F32 curBuffWeight = 0.f;
	F32 curControlWeight = 0.f;
	F32 curCombatJobWeight = 0.f;	
	F32 curAssignmentWeight = 0.f;
	AITeamMemberAssignment	*pAssignment = NULL;
	int didAction = false;
	int numLoops = 0;
	int i;
	int partitionIdx = entGetPartitionIdx(e);

	
	if(GET_REF(combatTeam->aigcSettings))
	{
		// Check if we're allowed to even be active
		if(combatMember->timeCombatActiveStarted<=0)
			return;
	}

	// if we're in an avoid, and we're already moving out, don't keep thinking, just keep running
	if (aiMovementGetMovementOrderType(e, aib)!=AI_MOVEMENT_ORDER_NONE)
	{
		for(i = eaSize(&aib->avoid.base.volumeEntities) - 1; i >= 0; i--)
		{
			if(aiAvoidEntryCheckSelf(e, NULL, aib->avoid.base.volumeEntities[i]))
				return;
		}
	}

	// check if you're going somewhere to heal someone or something
	if(	aiMultiTickAction_ProcessActions(e, aib) )
		return;

	if(aib->time.enterCombatWaitTime && !ABS_TIME_PASSED(aib->time.enterCombatWaitTime, 0.f))
	{
		if (aib->attackTarget && 
			ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.enterCombatWaitTime) > SEC_TO_ABS_TIME(-0.4f) &&
			aiCombatShouldFaceTarget(e, aib, config))
		{
			aiMovementSetFinalFaceEntity(e, aib, aib->attackTarget);
		}
		return;
	}

	// decide if you want to heal/buff/attack if applicable
	
	// for healing, we were checking if the target was !critter_IsKOS
	// so, for now since the team determines healing and buffing, we'll only allow it to 
	// heal and buff when not confused
	if(! aib->confused)
	{
		pAssignment = aiTeamGetAssignmentForMember(combatTeam, e);
		if (pAssignment)
		{
			if (config->doTeamAssignmentActionImmediately)
			{	// we are being told to do the assignment immediately 
				if (aiDoAssigmentAction(e, aib, pAssignment, doCombatMovement))
					return;
			}
			else
			{
				curAssignmentWeight = config->teamAssignmentActionWeight;
				total += curAssignmentWeight;
			}
		}
	}

	// aib->preferredTargetRef attackActionWeight & controlPowerActionWeight override
	// rrp: I'm not sure I like doing it this here, but if an AI has a preferred target and its 
	// attackActionWeight/controlPowerActionWeight are 0, it will use the base config weight
	// For STO being in passive mode and then setting a target and forcing it to attack
	
	if(aib->attackTarget)
	{
		AIGroupCombatSettings *aigcSettings;
		F32 attackWeight = config->attackActionWeight;
		if (attackWeight == 0.f)
		{
			
			if (aib->preferredTargetRef && aib->attackTargetRef == aib->preferredTargetRef)
			{	// if we have a preferred target assigned as our attackTarget, set the attack weight
				// to our baseConfig's
				AIConfig* baseConfig = GET_REF(aib->config_use_accessor);
				if(baseConfig)
					attackWeight = baseConfig->attackActionWeight;
			}
		}

		aigcSettings = GET_REF(combatTeam->aigcSettings);
		if(aigcSettings && aigcSettings->tokenSetting==AITS_TokensForAttacks)
		{
			if(combatMember->numCombatTokens>=1)
			{
				combatMember->numCombatTokens -= 1;
				curAttackWeight = attackWeight;
				total += curAttackWeight;
			}
		}
		else
		{
			curAttackWeight = attackWeight;
			total += curAttackWeight;
		}
	}

	if(aib->powers->hasControlPowers)
	{
		F32 controlWeight = config->controlPowerActionWeight;
		if (controlWeight == 0.f)
		{
			if (aib->preferredTargetRef && aib->attackTargetRef == aib->preferredTargetRef)
			{	// if we have a preferred target assigned as our attackTarget, set the control weight
				// to our baseConfig's
				AIConfig* baseConfig = GET_REF(aib->config_use_accessor);
				if(baseConfig)
					controlWeight = baseConfig->controlPowerActionWeight;
			}
		}

		curControlWeight = controlWeight;
		total += curControlWeight;
	}
	
	if(aib->powers->hasBuffPowers)
	{
		curBuffWeight = config->buffActionWeight;
		total += curBuffWeight;
	}

	// Set the combat job weight
	curCombatJobWeight = !aib->insideCombatJobFSM ? config->combatJobSearchWeight : 0.f;
	total += curCombatJobWeight;

	while(!didAction && total)
	{
		F32 roll = randomPositiveF32() * total;
		numLoops++;	// Keep track of the number of loops, in case this is an infinite loop
		devassert(numLoops < 100000);
		if(roll < curAttackWeight)
		{
			didAction = aiDoAttackAction(e, aib, true, doCombatMovement);
			curAttackWeight = 0.f;
		}
		else if (roll < curAttackWeight + curControlWeight)
		{
			AIPowerInfo *pPowerInfo = NULL;
			Entity *pTarget = NULL;

			if (aiPowersPickControlPowerAndTarget(e, aib, &pPowerInfo, &pTarget))
			{
				U32 uMTAFlags = MTAFlag_FORCEUSETARGET;
				if (!doCombatMovement)
					uMTAFlags |= MTAFlag_COMBATNOMOVE;

				didAction = aiMultiTickAction_QueuePower(	e, aib, pTarget, 
															AI_POWER_ACTION_USE_POWINFO, pPowerInfo, 
															uMTAFlags, NULL);
			}

			curControlWeight = 0.f;
		}
		else if (roll < curAttackWeight + curControlWeight + curAssignmentWeight)
		{
			if (aiDoAssigmentAction(e, aib, pAssignment, doCombatMovement))
				return;

			curAssignmentWeight = 0.f;
		}
		else if (roll < curAttackWeight + curControlWeight + curAssignmentWeight + curCombatJobWeight)
		{
			FSMLDCombatJob* combatJobData = getMyData(context, parse_FSMLDCombatJob, (U64)"CombatJob");
			didAction = aiCombatJob_AssignToCombatJob(e, combatJobData, false);

			curCombatJobWeight = 0.f;
		}
		else // if (roll < curAttackWeight + curControlWeight + curAssignmentWeight + curCombatJobWeight + curBuffWeight)
		{
			AIPowerInfo *pBuffPowerInfo = NULL;
			Entity *pTarget = NULL;
			
			if (aiPowersPickBuffPowerAndTarget(e, aib, false, &pBuffPowerInfo, &pTarget))
			{
				U32 uMTAFlags = MTAFlag_FORCEUSETARGET;
				if (!doCombatMovement)
					uMTAFlags |= MTAFlag_COMBATNOMOVE;

				didAction = aiMultiTickAction_QueuePower(e, aib, pTarget, 
															AI_POWER_ACTION_BUFF, pBuffPowerInfo, 
															uMTAFlags, NULL);	
				if(didAction)
				{
					// assuming the target is on the team
					AITeamMember *pMember = aiTeamFindMemberByEntity(combatTeam, pTarget);
					if (pMember)
					{
						pMember->timeLastActedOn[AITEAM_ASSIGNMENT_TYPE_BUFF] = ABS_TIME_PARTITION(partitionIdx);
					}
					combatTeam->time.lastBuff = ABS_TIME_PARTITION(partitionIdx);
				}
			}

			curBuffWeight = 0.f;
		}

		total = curAttackWeight + curBuffWeight + curControlWeight + curAssignmentWeight + curCombatJobWeight;
	}

	
	if (doCombatMovement && 
		!didAction && 
		aiIsInCoherencyMode(aib, config) )
	{	// if we did no action, and we have a valid rally type 
		if (ABS_TIME_PASSED(aib->time.lastUsedPower, aiGlobalSettings.fCombatInactionFaceTargetTime))
		{
			aiMovementClearRotationTarget(e, aib);
			aiCombatEnableFaceTarget(e, false);
		}
		
		aiCombatCoherencyReposition(e, aib);		
	}
}

static int aiCombatIsValidReinforceTarget(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AITeam* reinforceTeam, SA_PARAM_NN_VALID Entity* reinforceTarget, int checkCombatState)
{
	// Any type of reinforce target is invalid if the critter is dealing with a combat job
	if (aib && (aib->movingToCombatJob || aib->reachedCombatJob))
	{
		return false;
	}

	if(checkCombatState && reinforceTeam->combatState != AITEAM_COMBAT_STATE_AMBIENT)
		return false;

	if(	reinforceTeam->dontReinforce ||
		!reinforceTarget->pChar ||
		!aiIsEntAlive(reinforceTarget) ||
		reinforceTarget->pPlayer ||
		entGetFaction(e)!= entGetFaction(reinforceTarget) ||
		entGetCritterGroup(e)!=entGetCritterGroup(reinforceTarget) ||
		critter_IsFactionKOS(entGetPartitionIdx(e), e, reinforceTarget))		// Support factions that hate themselves
	{
		return false;
	}

	return true;
}

void aiCombatAddMinThrottlePercentage(Entity* e, AIVarsBase* aib, FSMLDCombat* mydata, ACMD_EXPR_ERRSTRING errStr)
{
	char* throttleVal = NULL;

	estrStackCreate(&throttleVal);
	estrPrintf(&throttleVal, "%f", mydata->combatMinimumThrottlePercentage);
	mydata->combatMinThrottleConfigModHandle =
		aiConfigModAddFromString(e, aib, "MinimumThrottlePercentage",
			throttleVal, errStr);
	estrDestroy(&throttleVal);
}

AUTO_COMMAND_QUEUED();
void aiCombatExitHandler(ACMD_POINTER Entity* e, ACMD_POINTER AIVarsBase* aib, ACMD_POINTER FSMLDCombat* mydata)
{
	entSetActive(e);
	if((!aib->combatFSMContext || !aib->insideCombatFSM) && !aib->insideCombatJobFSM)
		aib->inCombat = false;
	
	aiCombatEnableFaceTarget(e, false);
	
	if(mydata->combatMinThrottleConfigModHandle)
		aiConfigModRemove(e, aib, mydata->combatMinThrottleConfigModHandle);
	if(mydata->continuousCombatMovementOffHandle)
		aiConfigModRemove(e, aib, mydata->continuousCombatMovementOffHandle);

	if (aib->notVisibleTargetAnimQueue)
	{
		CommandQueue_ExecuteAllCommands(aib->notVisibleTargetAnimQueue);
		aib->notVisibleTargetAnimQueue = NULL;
	}

	ZeroStruct(mydata);
}

AUTO_COMMAND_QUEUED();
void aiFakeCombatExitHandler(ACMD_POINTER Entity* e)
{
	FSMLDGenericSetData *ldFakeCombat = getMyDataFromData(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_FAKECOMBAT_KEY);
	e->pChar->bInvulnerable = 0;
	ldFakeCombat->setData = 0;
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
}

AUTO_COMMAND_QUEUED();
void aiCombatExitCombatFSM(ACMD_POINTER Entity *e, ACMD_POINTER AIVarsBase* aib)
{
	// Reset FSM by exiting current state and moving to start
	FSM *combatFSM = GET_REF(aib->combatFSMContext->origFSM);
	if(!combatFSM)
		return;
	fsmExitCurState(aib->combatFSMContext);
	fsmChangeState(aib->combatFSMContext, combatFSM->states[0]->name);
}

void aiCombatInternal(Entity* e, ExprContext* context, int doCombatMovement, int fake, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDCombat* mydata = getMyData(context, parse_FSMLDCombat, (U64)"Combat");	
	AIVarsBase* aib = e->aibase;
	AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);
	AIConfig* config = aiGetConfig(e, aib);	
	int partitionIdx = entGetPartitionIdx(e);
	if(!e->pChar)
		return;


	// Do not allow combat movement if the top layer FSM prevented it.
	if ((aib->insideCombatJobFSM && aib->noCombatMovementInCombatJobFSM) || 
		(aib->insideCombatFSM && aib->noCombatMovementInCombatFSM))
	{
		doCombatMovement = false;
	}

	if (fake)
	{
		FSMLDGenericSetData *ldFakeCombat = getMyDataFromData(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_FAKECOMBAT_KEY);
		FSMLDGenericSetData *ldInvulnerable = getMyDataFromDataIfExists(&e->aibase->localData, parse_FSMLDGenericSetData, FSMLD_INVULNERABLE_KEY);

		if(!ldFakeCombat->setData)
		{
			if(SAFE_MEMBER(ldInvulnerable, setData))
				estrPrintf(errString, "Attempting to do FakeCombat when Invulnerable was already set.  This is not what you want.  These settings overlap.");
			ldFakeCombat->setData = 1;
			e->pChar->bInvulnerable = 1;
		}
	}
	else
	{
		// For critters, running the combat FSM function puts you in Combat Mode for powers.  This seems reasonable, and gets them into the
		// combat state before TimerUse does.  (TimerUse occurs on the first power activation.)  I only put this in an else case, because
		// I don't know what "fake" combat is, and I didn't want to mess with it.  [RMARR - 6/3/13]
		character_SetCombatExitTime(e->pChar, 1.0f, true, true, NULL, NULL);
	}

	if(!mydata->setData && (!aib->insideCombatFSM || !aiGlobalSettings.combatFSMKeepsInCombat))
	{
		CommandQueue* exitHandlers = NULL;
		mydata->setData = 1;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Unable to call combat in this section - missing exit handlers");
			return;
		}

		QueuedCommand_aiMovementResetPath(exitHandlers, e, aib);
		QueuedCommand_aiClearAllQueuedPowers(exitHandlers, e, aib);
		QueuedCommand_aiCombatExitHandler(exitHandlers, e, aib, mydata);
		if(fake)
			QueuedCommand_aiFakeCombatExitHandler(exitHandlers,e);
		
		aib->inCombat = true;
		aib->time.lastEnteredCombat = ABS_TIME_PARTITION(partitionIdx);
		
		// reset this whenever you enter combat to avoid guys running away to get into position
		// right away
		aib->time.lastInPreferredRange = ABS_TIME_PARTITION(partitionIdx);
		aib->combatRunAwayCount = 0;
		
		entSetActive(e);

		if(aiCombatShouldFaceTarget(e, aib, config))
		{
			aiCombatEnableFaceTarget(e, true);
		}
		 
		combatTeam->memberInCombat = true;
		
		if(doCombatMovement && config->movementParams.continuousCombatMovement &&
			aiMovementGetMovementOrderType(e, aib)==AI_MOVEMENT_ORDER_NONE &&
			combatTeam->combatState != AITEAM_COMBAT_STATE_LEASH)
		{
			aiDoAttackAction(e, aib, false, doCombatMovement);
		}

		mydata->switchModsForLeashing = config->movementParams.continuousCombatMovement;
		if(config->combatMinimumThrottlePercentage)
		{
			mydata->switchModsForLeashing = true;
			mydata->combatMinimumThrottlePercentage = config->combatMinimumThrottlePercentage;
			aiCombatAddMinThrottlePercentage(e, aib, mydata, errString);
		}
				
		if (!aib->insideCombatFSM && config->combatJobSearchWeight)
		{
			FSMLDCombatJob* combatJobData = getMyData(context, parse_FSMLDCombatJob, (U64)"CombatJob");
			aiCombatJob_AssignToCombatJob(e, combatJobData, true);
		}
	}

	if(combatTeam->combatState == AITEAM_COMBAT_STATE_WAITFORFIGHT)
		aiTeamEnterCombat(combatTeam);

	if(mydata->switchModsForLeashing)
	{
		if(!mydata->leashModeOn && combatTeam->combatState == AITEAM_COMBAT_STATE_LEASH)
		{
			mydata->leashModeOn = true;
			aiConfigModRemove(e, aib, mydata->combatMinThrottleConfigModHandle);
			mydata->combatMinThrottleConfigModHandle = 0;
			mydata->continuousCombatMovementOffHandle = aiConfigModAddFromString(e, aib, "continuousCombatMovement", "0", errString);
		}
		else if(mydata->leashModeOn && combatTeam->combatState != AITEAM_COMBAT_STATE_LEASH)
		{
			mydata->leashModeOn = false;
			aiCombatAddMinThrottlePercentage(e, aib, mydata, errString);
			aiConfigModRemove(e, aib, mydata->continuousCombatMovementOffHandle);
			mydata->continuousCombatMovementOffHandle = 0;
		}
	}

	switch(combatTeam->combatState)
	{
	xcase AITEAM_COMBAT_STATE_AMBIENT:
		;
	xcase AITEAM_COMBAT_STATE_STAREDOWN:
	{
		int bFaceTarget = aiCombatShouldFaceTarget(e, aib, config);

		// see if we should be going into 

		if (aib->team && aib->member)
		{
			aiCombatRoleFormation_RequestSlot(aib->team, aib->member, NULL, !bFaceTarget);
		}
		
		if(bFaceTarget)
		{
			int i;
			F32 closestEntDist = FLT_MAX;
			Entity* closestEnt = NULL;
			F32 highestAggro = 0;
			Entity* highestAggroEnt = NULL;
			
			// todo: rrp make this into a utility status table func

			for(i = eaSize(&aib->statusTable)-1; i >= 0; i--)
			{
				AIStatusTableEntry* status = aib->statusTable[i];
				if(status->visible && status->distanceFromMe < closestEntDist &&
					status->distanceFromMe < aib->aggroRadius)
				{
					closestEntDist = status->distanceFromMe;
					closestEnt = entFromEntityRef(partitionIdx, status->entRef);
				}

				if(status->visible && status->distanceFromMe < aib->aggroRadius &&
					status->totalBaseDangerVal > highestAggro)
				{
					highestAggro = status->totalBaseDangerVal;
					highestAggroEnt = entFromEntityRef(partitionIdx, status->entRef);
				}
			}

			if(highestAggroEnt)
				aiMovementSetFinalFaceEntity(e, aib, highestAggroEnt);
			else if(closestEnt)
				aiMovementSetFinalFaceEntity(e, aib, closestEnt);
		}
	}
	xcase AITEAM_COMBAT_STATE_FIGHT: {
		int noReaquire = false;
		if(combatTeam->reinforceMember == aib->member)
		{
			int i;
			int hadReinforceTarget = !!aib->reinforceTarget;

			if(!combatTeam->reinforceTeam)
				aib->reinforceTarget = 0;

			if(aib->reinforceTarget)
			{
				Entity* reinforceTarget = entFromEntityRef(partitionIdx, aib->reinforceTarget);
				devassert(combatTeam->reinforceTeam && combatTeam->reinforceTeam->reinforceCandidate);
				if(!reinforceTarget)
				{
					// No target, clear and reacquire
					aiTeamClearReinforceTarget(e, aib, combatTeam, combatTeam->reinforceTeam, 1, 0);
				}
				else
				{
					if(!aiCombatIsValidReinforceTarget(e, aib, combatTeam->reinforceTeam, reinforceTarget, false))
					{
						// target no longer invalid
						aiTeamClearReinforceTarget(e, aib, combatTeam, combatTeam->reinforceTeam, 1, 0);
					}
					else if((combatTeam->reinforceTeam->combatState==AITEAM_COMBAT_STATE_FIGHT || 
						combatTeam->reinforceTeam->combatState==AITEAM_COMBAT_STATE_WAITFORFIGHT)) 
					{
						// target is in combat, check if they're in combat with guys I hate
						int found = 0;
						for(i=eaSize(&combatTeam->statusTable)-1; i>=0; i--)
						{
							AITeamStatusEntry *status = combatTeam->statusTable[i];
							AITeamStatusEntry *reinforceStatus = NULL;

							if(stashIntFindPointer(combatTeam->reinforceTeam->statusHashTable, status->entRef, &reinforceStatus) && 
								reinforceStatus->legalTarget)
							{
								found = 1;
								break;
							}
						}

						if(!found)
							aiTeamClearReinforceTarget(e, aib, combatTeam, combatTeam->reinforceTeam, 1, 0);
						else
						{
							noReaquire = true;
							aiTeamRequestReinforcements(e, aib, combatTeam, combatTeam->reinforceTeam);
						}
					}
				}
			}

			if(!noReaquire && !aib->reinforceTarget)
			{
				// these are sorted by distance
				aiUpdateProxEnts(e,aib);
				for(i = 0; i < aib->proxEntsCount; i++)
				{
					Entity* reinforceEnt = aib->proxEnts[i].e;
					F32 reinforceEntDistSQR = aib->proxEnts[i].maxDistSQR;
					AITeam* reinforceTeam = reinforceEnt->aibase->team;

					if(reinforceEntDistSQR < SQR(config->combatMaxReinforceDist) &&
						!reinforceTeam->reinforceCandidate && 
						!reinforceTeam->reinforced)
					{
						if(aiCombatIsValidReinforceTarget(e, aib, reinforceTeam, reinforceEnt, true))
						{
							aiTeamSetReinforceTarget(e, aib, combatTeam, reinforceTeam, reinforceEnt);
							break;
						}
					}
				}
			}

			if(hadReinforceTarget && !aib->reinforceTarget)
				aiMovementResetPath(e, aib);
		}

		// fall through
		if(aib->reinforceTarget)
		{
			Entity* reinforceTarget = entFromEntityRef(partitionIdx, aib->reinforceTarget);
			F32 dist = entGetDistance(e, NULL, reinforceTarget, NULL, NULL);

			devassert(reinforceTarget);

			if(dist < AI_PROX_NEAR_DIST)
			{
				devassert(combatTeam->reinforceTeam);
				aiTeamRequestReinforcements(e, aib, combatTeam, combatTeam->reinforceTeam);
				aiMovementResetPath(e, aib);
			}
			else
				aiMovementSetTargetEntity(e, aib, reinforceTarget, NULL, 0, AI_MOVEMENT_ORDER_ENT_COMBAT, AI_MOVEMENT_TARGET_CRITICAL);
		}
		else if (!aib->insideCombatJobFSM && (aib->movingToCombatJob || aib->reachedCombatJob))
		{
			FSMLDCombatJob* combatJobData = getMyData(context, parse_FSMLDCombatJob, (U64)"CombatJob");

			// Run the combat job assigned to the critter
			aiCombatJob_RunJob(e, context, doCombatMovement, combatJobData, errString);
		}
		else if(!aib->combatFSMContext || aib->insideCombatFSM || aib->insideCombatJobFSM || config->disableCombatFSM)
		{
			//TODO(AM): Make this fire once
			if(fake && aib->insideCombatFSM)
			{
				estrPrintf(errString, "Not allowed to call FakeCombat() from within combat FSM");
				return;
			}

			aiPickFightAction(e, aib, doCombatMovement, context);
		}
		else
		{
			if(!mydata->setCombatFSMData)
			{
				CommandQueue* exitHandlers = NULL;

				exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

				if(!exitHandlers)
				{
					estrPrintf(errString, "Unable to call combat in this section - missing exit handlers");
					return;
				}

				mydata->setCombatFSMData = 1;

				QueuedCommand_aiCombatExitCombatFSM(exitHandlers, e, aib);
			}
			aib->insideCombatFSM = true;
			aib->noCombatMovementInCombatFSM = !doCombatMovement;
			fsmExecute(aib->combatFSMContext, aib->exprContext);
			aib->noCombatMovementInCombatFSM = false;
			aib->insideCombatFSM = false;
		}
	}
	xcase AITEAM_COMBAT_STATE_LEASH:
		switch(aib->leashState)
		{
			xcase AI_LEASH_STATE_DONE: {
				if(!aiCloseEnoughToSpawnPos(e, aib))
				{
					aiMovementGoToSpawnPos(e, aib, 0);
				}
			}
		}
	}
}

void aiCombatReset(Entity *e, AIVarsBase *aib, int heal, int char_reset, int ignoreLevels)
{
	AIConfig *config = aiGetConfig(e, aib);

	if(!aiIsEntAlive(e))
		return;

	if(!config->dontDoGrievedHealing)
	{
		if(heal)
		{
			if(aiGlobalSettings.leashHealRate)
				aib->healing = 1;
			else
			{
				aib->healing = 0;
				if(ignoreLevels)
					e->pChar->pattrBasic->fHitPoints = e->pChar->pattrBasic->fHitPointsMax;
				else
					e->pChar->pattrBasic->fHitPoints = e->pChar->pattrBasic->fHitPointsMax * aib->minGrievedHealthLevel;

				character_DirtyAttribs(e->pChar);
			}
		}
		if(char_reset)
		{
			character_ResetPartial(entGetPartitionIdx(e),
									e->pChar, e, 
									!aiGlobalSettings.leashDontClearMods,
									true,
									true,
									!aiGlobalSettings.leashDontRechargePowers,
									false,
									!aiGlobalSettings.leashDontRemoveStatusEffects,
									NULL);
		}
	}
}

F32 aiCombatGetSocialAggroDist(Entity* e, AIVarsBase* aib, int primary)
{
	AIConfig* config = aiGetConfig(e, aib);
	Vec3 pos;
	RegionRules *rules;
	static int primaryDistCol = -1;
	static int secondaryDistCol = -1;
	static int inited = 0;
	static S32 aiConfigUsedFieldIndex = 0;

	entGetPos(e, pos);

	if(primary)
	{
		if(config->socialAggroPrimaryDist >= 0)
			return config->socialAggroPrimaryDist;

		rules = RegionRulesFromVec3(pos);
		if(rules && rules->fSocialAggroPrimaryDist>=0)
			return rules->fSocialAggroPrimaryDist;

		return 50;
	}
	else
	{
		if(config->socialAggroSecondaryDist>=0)
			return config->socialAggroSecondaryDist;

		rules = RegionRulesFromVec3(pos);
		if(rules && rules->fSocialAggroSecondaryDist>=0)
			return rules->fSocialAggroSecondaryDist;

		return 5;
	}
}

// will only AddTeamToCombatTeam if the config flag is set, socialAggroAlwaysAddTeamToCombatTeam
void aiCombatAddTeamToCombatTeam(Entity *e, AIVarsBase* aib, bool bDoSecondaryPulseOnAddedTeammates)
{
	// check if our team will always aggro the whole team
	if (aib->combatTeam && aib->team && aib->team->config.socialAggroAlwaysAddTeamToCombatTeam)
	{
		static AITeamMember **s_eaAddedMembers = NULL;
		
		eaClear(&s_eaAddedMembers);

		FOR_EACH_IN_EARRAY(aib->team->members, AITeamMember, pMember)
		{
			Entity *eMember = pMember->memberBE;
			// only if the member doesn't already have a combat team
			if (eMember && eMember->aibase && !eMember->aibase->combatTeam)
			{
				aiTeamAdd(aib->combatTeam, pMember->memberBE);
				if (bDoSecondaryPulseOnAddedTeammates)
					eaPush(&s_eaAddedMembers, pMember);
			}
		}
		FOR_EACH_END

		if (bDoSecondaryPulseOnAddedTeammates && !aib->team->bIgnoreSocialAggroPulse)
		{
			FOR_EACH_IN_EARRAY(s_eaAddedMembers, AITeamMember, pMember)
			{
				aiCombatDoSocialAggroPulse(pMember->memberBE, pMember->memberBE->aibase, false);
			}
			FOR_EACH_END
		}

		if (!aib->combatTeam->teamOwner)
		{
			if (aib->team->teamOwner && aiTeamFindMemberByEntity(aib->combatTeam, aib->team->teamOwner))
			{	// the team owner is on the combat team, transfer ownership to him
				aib->combatTeam->teamOwner = aib->team->teamOwner;
			}
		}
	}
}

void aiCombatDoSocialAggroPulse(Entity* e, AIVarsBase* aib, int primary)
{
	int i;
	F32 distSQR;
	int partitionIdx = entGetPartitionIdx(e);

	// don't pulse aggro under these conditions
	if (entIsPlayer(e) || !aiIsEntAlive(e))
		return;

	distSQR = aiCombatGetSocialAggroDist(e, aib, primary);
	distSQR = SQR(distSQR);

	aiUpdateProxEnts(e,aib);
	for(i=0; i<aib->proxEntsCount; i++)
	{
		Entity* proxE;
		AIVarsBase* proxAIB;
		AIStatusTableEntry *status;
		S32 visible = 0;
		AITeam *pTeam = NULL;

		if(aib->proxEnts[i].maxDistSQR > distSQR)
			break;

		proxE = aib->proxEnts[i].e;
		proxAIB = proxE->aibase;

		// Don't add enemies, non-combat guys, players, or dead things
		if(	!proxAIB ||
			!proxE->pChar || 
			proxE->pPlayer || 
			!aiIsEntAlive(proxE) ||
			proxAIB->combatTeam == aib->combatTeam ||
			critter_IsKOS(partitionIdx, e, proxE) )
		{
			continue;
		}

		status = aiStatusFind(e, aib, proxE, false);
		if(status)
			visible = status->visible;
		else
		{
			Vec3 p1, p2;
			entGetPos(e, p1);
			entGetPos(proxE, p2);
			visible = !aiCollideRay(entGetPartitionIdx(e), e, p1, proxE, p2, 0);
		}

		if(!visible)
			continue;

		pTeam = aiTeamGetCombatTeam(e, aib);
		if (pTeam && pTeam->bIgnoreSocialAggroPulse)
			continue;

		// check inner distance, to ignore LOS to character
		// otherwise check LOS to character
						
		if(!proxAIB->combatTeam)
		{
			aiTeamAdd(aib->combatTeam, proxE);
			aiCombatAddTeamToCombatTeam(proxE, proxAIB, true);
			aiCombatDoSocialAggroPulse(proxE, proxAIB, false);
		}
		else
		{
			// Merge the teams?
		}
	}

	aib->time.lastSocialAggroPulse = ABS_TIME_PARTITION(partitionIdx);
}

// Runs the normal combat code for the current entity
// NOTE: this still has to obey the entity's powers and AIConfig settings, so might not actually
// lead to this entity actually attacking anything. If you're not sure, you should probably
// use the overarching Combat FSM instead of calling Combat() manually, or using DefaultEnterCombat()
// and DefaultExitCombat() to control when you go into a state that runs Combat() to make sure
// the entity actually has an attack target and wants to attack
AUTO_EXPR_FUNC(ai) ACMD_NAME(Combat);
void exprFuncCombat(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errStr)
{
	aiCombatInternal(e, context, true, false, errStr);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(FakeCombat);
void exprFuncFakeCombat(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errStr)
{
	aiCombatInternal(e, context, true, true, errStr);
}

// Runs the normal combat code for the current entity but does no movement, so you can use this
// while giving other movement orders
// NOTE: use this only if you really know what you're doing
AUTO_EXPR_FUNC(ai) ACMD_NAME(CombatNoMove);
ExprFuncReturnVal exprFuncCombatNoMove(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errStr)
{
	ExprFuncReturnVal funcretval = exprFuncAddConfigMod(e, context, "SkipLeashing", "1", errStr);
	if(funcretval != ExprFuncReturnFinished)
		return funcretval;
	aiCombatInternal(e, context, false, false, errStr);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(FakeCombatNoMove);
ExprFuncReturnVal exprFuncFakeCombatNoMove(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errStr)
{
	ExprFuncReturnVal funcretval = exprFuncAddConfigMod(e, context, "SkipLeashing", "1", errStr);
	if(funcretval != ExprFuncReturnFinished)
		return funcretval;
	aiCombatInternal(e, context, false, true, errStr);
	return ExprFuncReturnFinished;
}

