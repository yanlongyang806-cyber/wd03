#include "aiBrawlerCombat.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "gslMapState.h"
#include "MemoryPool.h"
#include "rand.h"
#include "TextParserSimpleInheritance.h"

MP_DEFINE(BrawlerCombatData);

static BrawlerRoleConfig* aiBrawlerCombat_GetBrawlerRoleConfig(BrawlerCombatGlobalConfig *pGlobalConfig, BrawlerCombatConfig *pConfig);
static BrawlerRoleConfig* aiBrawlerCombat_GetBrawlerRoleConfigByName(BrawlerCombatGlobalConfig *pGlobalConfig, const char *pchTag);


// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_InheritAIGlobalSettings(BrawlerCombatConfig *pConfig)
{
	if (pConfig && pConfig->pOverride)
	{
		BrawlerRoleConfig *pGlobalRole = aiBrawlerCombat_GetBrawlerRoleConfigByName(aiGlobalSettings.pBrawlerConfig, pConfig->pchTag);

		if (pGlobalRole)
		{
			SimpleInheritanceApply(parse_BrawlerRoleConfig, pConfig->pOverride, pGlobalRole, NULL, NULL);
		}
	}
}


// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_ValidateGlobalConfig(const char *pchFilename, BrawlerCombatGlobalConfig *pConfig)
{
	if (pConfig->fForceEngageTimeOnDamage <= 0)
	{
		ErrorFilenamef(pchFilename, "BrawlerConfig: ForceEngageTimeOnDamage must be greater than 0.");
		pConfig->fForceEngageTimeOnDamage = 1.f;
	}

	FOR_EACH_IN_EARRAY(pConfig->eaBrawlerRoles, BrawlerRoleConfig, pRole)
	{
		if (!pRole->pchTag)
		{
			ErrorFilenamef(pchFilename, "BrawlerConfig: A tag was not specified for a role.");
			continue;
		}
		else
		{
			// make sure there are no duplicate tags
			FOR_EACH_IN_EARRAY(pConfig->eaBrawlerRoles, BrawlerRoleConfig, pOtherRole)
			{
				if (pOtherRole != pRole && pRole->pchTag == pOtherRole->pchTag)
				{
					ErrorFilenamef(pchFilename, "BrawlerConfig: Duplicate tag of %s.", pRole->pchTag);
				}
			}
			FOR_EACH_END
		}

		if (pRole->iMaxEngaged <= 0)
		{
			ErrorFilenamef(pchFilename, "BrawlerConfig: %s MaxEngaged must be greater than 0", pRole->pchTag);
		}
	}
	FOR_EACH_END
}

// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_ValidateConfig(AIConfig *pConfig)
{
	if (pConfig->pBrawlerCombatConfig)
	{
		if (!aiGlobalSettings.pBrawlerConfig)
		{
			ErrorFilenamef(pConfig->filename, "BrawlerCombatConfig specified, but no BrawlerConfig defined on the aiGlobalSettings.");
			return;
		}

		if (!aiBrawlerCombat_GetBrawlerRoleConfig(aiGlobalSettings.pBrawlerConfig, pConfig->pBrawlerCombatConfig))
		{
			ErrorFilenamef(pConfig->filename, "BrawlerCombatConfig using the tag \"%s\" no tag specified on aiGlobalSettings's BrawlerConfig.", pConfig->pBrawlerCombatConfig->pchTag);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_Initialze(Entity *e, AIVarsBase *aib, AIConfig* pAIConfig)
{
	if (!aib->pBrawler)
	{
		MP_CREATE(BrawlerCombatData, 64);
		aib->pBrawler = MP_ALLOC(BrawlerCombatData);

		aib->pBrawler->pchTag = pAIConfig->pBrawlerCombatConfig->pchTag;
	}
}

// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_Shutdown(AIVarsBase *aib)
{
	if (aib->pBrawler)
	{
		MP_FREE(BrawlerCombatData, aib->pBrawler);
		aib->pBrawler = NULL;
	}
}

// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_SetState(AIVarsBase *aib, EBrawlerCombatState eNewState)
{
	if (!aib->pBrawler || aib->pBrawler->eState == eNewState)
		return;
	
	aib->pBrawler->eState = eNewState;

	zeroVec3(aib->pBrawler->vWaitingPoint);
	aib->pBrawler->bBackingAway = false;
	aib->pBrawler->bGettingCloser = false;
	aib->pBrawler->lastMoveTowardsEnemyTime = 0;
}

// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_ForceEngage(S32 iPartitionIdx, AIVarsBase *aib, F32 fForceTime)
{
	S64 forceEndTime;
	if (!aib->pBrawler)
		return;

	if (aib->pBrawler->eState == EBrawlerCombatState_DISENGAGED)
	{
		aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ATTEMPTING_ENGAGE);
	}
	
	// force the AI to move, reset the lastCombatMovement so it won't disallow from moving immediately.
	aib->time.lastCombatMovement = 0;

	forceEndTime = ABS_TIME_PARTITION(iPartitionIdx) + SEC_TO_ABS_TIME(fForceTime);
	
	// if we were set to end later, keep that time
	if (forceEndTime > aib->pBrawler->forceEngageTimeEnd)
	{
		aib->pBrawler->forceEngageTimeEnd = forceEndTime;
	}
}


// --------------------------------------------------------------------------------------------------------------------
// pchTag is assumed to be a pooled string
static BrawlerRoleConfig* aiBrawlerCombat_GetBrawlerRoleConfigByName(BrawlerCombatGlobalConfig *pGlobalConfig, const char *pchTag)
{
	FOR_EACH_IN_EARRAY(pGlobalConfig->eaBrawlerRoles, BrawlerRoleConfig, pRole)
	{
		if (pRole->pchTag == pchTag)
			return pRole;
	}
	FOR_EACH_END

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
// searches the role list to find a role with the matching tag. I don't expect there to be many roles. 
static BrawlerRoleConfig* aiBrawlerCombat_GetBrawlerRoleConfig(BrawlerCombatGlobalConfig *pGlobalConfig, BrawlerCombatConfig *pConfig)
{
	if (pConfig->pOverride)
		return pConfig->pOverride;

	return aiBrawlerCombat_GetBrawlerRoleConfigByName(pGlobalConfig, pConfig->pchTag);
}


// --------------------------------------------------------------------------------------------------------------------
void aiBrawlerCombat_Update(Entity *e, AIVarsBase *aib, AIConfig* pAIConfig) 
{
	devassert(pAIConfig->pBrawlerCombatConfig);

	if (aib->attackTarget && aib->attackTarget->aibase && pAIConfig->pBrawlerCombatConfig->pchTag)
	{
		// check if there are other people engaged with my target
		// see how many of the tags taht we are care about are engaged
		BrawlerRoleConfig *pRoleConfig = aiBrawlerCombat_GetBrawlerRoleConfig(aiGlobalSettings.pBrawlerConfig, pAIConfig->pBrawlerCombatConfig);
		AIVarsBase *pTargetAI = aib->attackTarget->aibase;
		S32 iEntsEngagedCount = 0;
		S32 iEntsAttemptingCount = 0;
		S32 iEntsForcedEngaged = 0;
		S32 iPartitionIdx = entGetPartitionIdx(e);


		if (!pTargetAI|| !pRoleConfig)
			return; // can't find a config, this should have been validated

		if (!aib->pBrawler)
		{
			if (pAIConfig->pBrawlerCombatConfig)
			{
				aiBrawlerCombat_Initialze(e, aib, pAIConfig);
			}
			else return;
		}
		
		if (aib->pBrawler->forceEngageTimeEnd)
		{
			if (aib->pBrawler->eState == EBrawlerCombatState_ENGAGED && 
				ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->pBrawler->forceEngageTimeEnd) > 0)
			{
				aib->pBrawler->forceEngageTimeEnd = 0;
			}
			else
			{
				if (aib->pBrawler->eState != EBrawlerCombatState_ENGAGED)
					aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ATTEMPTING_ENGAGE);
				return;
			}
		}

		if (eaSize(&pTargetAI->attackerList) < pRoleConfig->iMaxEngaged)
		{
			if (aib->pBrawler->eState != EBrawlerCombatState_ENGAGED)
				aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ATTEMPTING_ENGAGE);
			return;
		}

		FOR_EACH_IN_EARRAY(pTargetAI->attackerList, Entity, pAttacker)
		{
			if (pAttacker != e && pAttacker->aibase && pAttacker->aibase->pBrawler && aiIsEntAlive(pAttacker))
			{
				BrawlerCombatData *pAttackerBrawler = pAttacker->aibase->pBrawler;

				// see if this attacker is one that I'd care about counting as engaged
				if (pAttackerBrawler->eState != EBrawlerCombatState_DISENGAGED && 
					(pRoleConfig->pchTag == pAttackerBrawler->pchTag || 
					 eaFind(&pRoleConfig->pchSiblingTags, pAttackerBrawler->pchTag)>=0))
				{
					if (pAttackerBrawler->forceEngageTimeEnd)
						++iEntsForcedEngaged;

					if (pAttacker->aibase->pBrawler->eState == EBrawlerCombatState_ENGAGED)
					{
						if (++iEntsEngagedCount >= pRoleConfig->iMaxEngaged)
							break;
					}
					else if (pAttacker->aibase->pBrawler->eState == EBrawlerCombatState_ATTEMPTING_ENGAGE)
					{
						++iEntsAttemptingCount;
					}
				}
			}
		}
		FOR_EACH_END

		if (iEntsEngagedCount < pRoleConfig->iMaxEngaged)
		{	// not enough engaged, if we were disengaged try and engage
			S32 iMaxAttempingEngage = (S32)ceil( (pRoleConfig->iMaxEngaged - iEntsEngagedCount) * 2.f);

			if (iEntsAttemptingCount > iMaxAttempingEngage)
			{
				aiBrawlerCombat_SetState(aib, EBrawlerCombatState_DISENGAGED);
			}
			else if (aib->pBrawler->eState == EBrawlerCombatState_DISENGAGED)
			{
				aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ATTEMPTING_ENGAGE);
			}
		}
		else 
		{	// too many others engaged, disengage
			aiBrawlerCombat_SetState(aib, EBrawlerCombatState_DISENGAGED);
		}


		if (aib->pBrawler->eState == EBrawlerCombatState_DISENGAGED)
		{
			// we are currently disengaged, see if we should engage
			
			// Check if we received damage in the last second
			if (aiGlobalSettings.pBrawlerConfig->fForceEngageTimeOnDamage &&
				ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->time.lastDamage[AI_NOTIFY_TYPE_DAMAGE]) < 
				  SEC_TO_ABS_TIME(1.f) )
			{
				aiBrawlerCombat_ForceEngage(iPartitionIdx, aib, aiGlobalSettings.pBrawlerConfig->fForceEngageTimeOnDamage);
			}
			else if (pRoleConfig->fPercentChancePerTickToEngage && iEntsForcedEngaged < pRoleConfig->iMaxEngaged)
			{
				if (100.f*randomPositiveF32() <= pRoleConfig->fPercentChancePerTickToEngage)
				{
					aiBrawlerCombat_ForceEngage(iPartitionIdx, aib, aiGlobalSettings.pBrawlerConfig->fForceEngageTimeOnDamage);
				}
			}
		}
	}

	if (aib->pBrawler->eState == EBrawlerCombatState_ENGAGED && aib->attackTargetStatus)
	{
		F32 prefMaxRange = aiGetPreferredMaxRange(e, aib) + 5.f;

		if (aib->attackTargetStatus->distanceFromMe > prefMaxRange)
		{
			aiBrawlerCombat_SetState(aib, EBrawlerCombatState_ATTEMPTING_ENGAGE);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
bool aiBrawlerCombat_ShouldDisengage(Entity *e, AIVarsBase *aib, AIConfig* config)
{
	return aib->pBrawler && aib->pBrawler->eState == EBrawlerCombatState_DISENGAGED;
}


// --------------------------------------------------------------------------------------------------------------------
// Does the back away and wander motion when the critter is using the hit and wait style combat movement
void aiBrawlerCombat_DoBackAwayAndWanderMovement(Entity *e, AIConfig* pAIConfig, const Vec3 vAttackTargetPos)
{
	AIVarsBase *aib = e->aibase;
	Vec3 vEntPos;
	F32 fMinDistance;
	F32 fMaxDistance;
	F32 fDistToTarget;
	S32 iPartitionIdx = entGetPartitionIdx(e);
	BrawlerRoleConfig *pRoleConfig = NULL;
	F32 fCapsuleRadiiSum = 0.f;

	if (pAIConfig->pBrawlerCombatConfig == NULL || !aib->pBrawler)
	{
		return;
	}

	fCapsuleRadiiSum = entGetPrimaryCapsuleRadius(e);

	if (aib->attackTarget)
	{
		fCapsuleRadiiSum += entGetPrimaryCapsuleRadius(aib->attackTarget);
	}
	
	pRoleConfig = aiBrawlerCombat_GetBrawlerRoleConfig(aiGlobalSettings.pBrawlerConfig, pAIConfig->pBrawlerCombatConfig);

	if (!pRoleConfig)
		return; // can't find a config, this should have been validated


	// Get the min and max distances
	fMinDistance = MAX(0.f, pRoleConfig->fMinDistanceToTargetWhileWaiting);
	fMaxDistance = MAX(fMinDistance, pRoleConfig->fMaxDistanceToTargetWhileWaiting);

	// Get the entities current position
	entGetPos(e, vEntPos);

	// Clear out run walk distance because we want the entity to be in full speed in combat
	aiMovementSetWalkRunDist(e, aib, 0.f, 0.f, 0);

	if (aib->attackTargetStatus)
	{
		if (!aib->attackTargetStatus->visible && aib->attackTargetStatus->eNotVisibleReason == EAINotVisibleReason_PERCEPTION_STEALTH)
		{
			//We don't want to do anything if the target has disappeared
			fDistToTarget = fMinDistance;
			aiMovementClearMovementTarget(e, aib);
			aiMovementClearRotationTarget(e, aib);
		}
		else
		{
			fDistToTarget = aib->attackTargetStatus->distanceFromMe;
		}
	}
	else
	{
		// Check if we're within the preferred range
		fDistToTarget = distance3Squared(vEntPos, vAttackTargetPos) - SQR(fCapsuleRadiiSum);
	}
	
	if (fDistToTarget >= fMinDistance && fDistToTarget <= fMaxDistance)
	{
		// We're in the preferred range and happy. Don't do anything.
	}
	else
	{		
		if (fDistToTarget < fMinDistance)
		{
			if (ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->pBrawler->lastMoveTowardsEnemyTime) > SEC_TO_ABS_TIME(3.f))
			{
				// We're too close
				S32 iIterationCount = 0;
				F32 fCurAngle = 0.f;
				const S32 iMaxIterationCount = 13;
				Vec3 vTargetPoint;
				bool bBackAwayPointIsValid = false;
				S32 iItProxEnt;

				aib->pBrawler->lastMoveTowardsEnemyTime = ABS_TIME_PARTITION(iPartitionIdx);


				while (!bBackAwayPointIsValid && iIterationCount <= iMaxIterationCount)
				{
					Vec3 vecDir;
					F32 fBackAwayDistance = CLAMP(pRoleConfig->fBackAwayDistance, fMinDistance, fMaxDistance);
					F32 fDistanceToUse = fCapsuleRadiiSum + fMinDistance + ((fBackAwayDistance - fMinDistance) * randomPositiveF32());	

					bBackAwayPointIsValid = true;

					aib->pBrawler->bBackingAway = true;
					aib->pBrawler->bGettingCloser = false;

					// Calculate the difference vector
					subVec3(vEntPos, vAttackTargetPos, vTargetPoint);

					// Normalize the difference vector
					normalVec3(vTargetPoint);

					if (vec3IsZero(vTargetPoint))
					{
						// Handle the case where the entity and the target was exactly on the same spot
						randomVec3(vTargetPoint);
						vTargetPoint[1] = 0.f;
						normalVec3(vTargetPoint);
					}

					// Rotate by the current angle
					copyVec3(vTargetPoint, vecDir);
					rotateVecAboutAxis(RAD(fCurAngle), (F32 *)upvec, vecDir, vTargetPoint);

					scaleVec3(vTargetPoint, fDistanceToUse, vTargetPoint);
					addVec3(vAttackTargetPos, vTargetPoint, vTargetPoint);

					{
						AICollideRayResult result = AICollideRayResult_NONE;
						AICollideRayFlag collideFlags = AICollideRayFlag_DOAVOIDCHECK | 
														AICollideRayFlag_DOWALKCHECK | 
														AICollideRayFlag_SKIPRAYCAST |
														AICollideRayFlag_DOCAPSULECHECK;						
						Vec3 vecHitPoint;

						if (aiCollideRayEx(iPartitionIdx, e, vEntPos, NULL, vTargetPoint, collideFlags, 
											AI_DEFAULT_STEP_LENGTH, 
											&result, vecHitPoint) && 
											result != AICollideRayResult_AVOID)
						{
							if (result == AICollideRayResult_AVOID)
							{
								// We don't want to back away towards avoid zones
								bBackAwayPointIsValid = false;
							}
							else
							{
								// Use the collision point and step back just a little
								F32 fLength;
								Vec3 vecDiff;
								subVec3(vecHitPoint, vEntPos, vecDiff);
								fLength = lengthVec3(vecDiff);
								fLength = MAX(0.f, fLength - 0.35f);
								normalVec3(vecDiff);
								scaleVec3(vecDiff, fLength, vecDiff);
								addVec3(vEntPos, vecDiff, vTargetPoint);
							}
						}
					}

					// Validate position
					if (bBackAwayPointIsValid)
					{
						aiUpdateProxEnts(e, aib);
						for (iItProxEnt = 0; iItProxEnt < aib->proxEntsCount; iItProxEnt++)
						{
							Entity *pProxEnt = aib->proxEnts[iItProxEnt].e;

							if (pProxEnt &&
								pProxEnt != e &&
								pProxEnt->aibase && pProxEnt->aibase->pBrawler &&
								!vec3IsZero(pProxEnt->aibase->pBrawler->vWaitingPoint) &&
								distance3Squared(vTargetPoint, pProxEnt->aibase->pBrawler->vWaitingPoint) <= SQR(4.f))
							{
								bBackAwayPointIsValid = false;
								break;
							}
						}
					}

					if (!bBackAwayPointIsValid)
					{
						// Do another iteration
						++iIterationCount;

						if (iIterationCount % 2 == 1)
						{
							// Add 10 degrees
							fCurAngle = fabsf(fCurAngle);
							fCurAngle += 10;
						}
						else
						{
							// Try the point on the opposite side
							fCurAngle *= -1.f;
						}						
					}
				}				

				// Store the point we're backing to
				copyVec3(vTargetPoint, aib->pBrawler->vWaitingPoint);

				aiMovementSetTargetPosition(e, aib, vTargetPoint, NULL, 0);
			}
		}
		else
		{
			//if (!aib->pBrawler->bGettingCloser)
			if (ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->pBrawler->lastMoveTowardsEnemyTime) > SEC_TO_ABS_TIME(3.f))
			{
				F32 fHalfDist = (fMaxDistance - fMinDistance) * 0.5f;
				// We're too far, move towards the entity
				Vec3 vRange = {0};

				vRange[0] = fMinDistance + (fHalfDist * randomPositiveF32());

				aiMovementSetTargetEntity(e, aib, aib->attackTarget, vRange, 0, AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE_DIST, 0);

				// Zero out the back away point
				zeroVec3(aib->pBrawler->vWaitingPoint);

				aib->pBrawler->bBackingAway = false;
				aib->pBrawler->bGettingCloser = true;
				aib->pBrawler->lastMoveTowardsEnemyTime = ABS_TIME_PARTITION(iPartitionIdx);
			}
		}
	}
}

#include "aiBrawlerCombat_h_ast.c"
