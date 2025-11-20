#include "aiMultiTickAction.h"
#include "aiAnimList.h"
#include "aiLib.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiMovement.h"
#include "EntityMovementTactical.h"
#include "EntityMovementManager.h"

#include "Character_Target.h"
#include "CommandQueue.h"
#include "gslMapState.h"
#include "MemoryPool.h"
#include "RegionRules.h"
#include "StateMachine.h"
#include "WorldGrid.h"

MP_DEFINE(AIPowerMultiTickAction);



typedef enum EForcedActionPriority
{
	EForcedActionPriority_NONE = 0,
	EForcedActionPriority_USER,
	EForcedActionPriority_INTERNAL_CRITICAL,
} EForcedActionPriority;




static int aiPowersDoMTA(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction* action);


// ------------------------------------------------------------------------------------------
static AIPowerMultiTickAction* aiMultiTickAction_Create()
{
	MP_CREATE(AIPowerMultiTickAction, 16);
	return MP_ALLOC(AIPowerMultiTickAction);
}

// ------------------------------------------------------------------------------------------
static void aiMultiTickAction_Destroy(AIPowerMultiTickAction *pMTA)
{
	if (pMTA->pAnimQueue)
	{
		CommandQueue_Destroy(pMTA->pAnimQueue);
		pMTA->pAnimQueue = NULL;
	}
	MP_FREE(AIPowerMultiTickAction, pMTA);
}

// ------------------------------------------------------------------------------------------
static __forceinline void aiMultiTickAction_CallCallback(AIPowerMultiTickAction* action, Entity *e, int bPowerUsed)
{
	if(action->completedCB)
	{
		action->completedCB(e, SAFE_MEMBER(action->powInfo, power), bPowerUsed);
		action->completedCB = NULL;
	}
}

// ------------------------------------------------------------------------------------------
void aiMultiTickAction_DestroyQueue(Entity *e, AIVarsBase *aib)
{
	eaDestroyEx(&aib->powers->ppMultiTickActionQueue, aiMultiTickAction_Destroy);
}

// ------------------------------------------------------------------------------------------
void aiMultiTickAction_ClearQueueEx(Entity *e, AIVarsBase *aib, bool bForceClearAll)
{
	if (bForceClearAll)
	{
		// go through all the MTAs and call any completed callbacks on them
		FOR_EACH_IN_EARRAY(aib->powers->ppMultiTickActionQueue, AIPowerMultiTickAction, pMTA)
			aiMultiTickAction_CallCallback(pMTA, e, false);
		FOR_EACH_END

		eaClearEx(&aib->powers->ppMultiTickActionQueue, aiMultiTickAction_Destroy);
	}
	else
	{
		// only remove the non-EForcedActionPriority_INTERNAL_CRITICAL MTAs
		S32 i = eaSize(&aib->powers->ppMultiTickActionQueue);
		while (i--)
		{
			AIPowerMultiTickAction *pMTA = aib->powers->ppMultiTickActionQueue[i];

			if (pMTA->iForcedActionPriority < EForcedActionPriority_INTERNAL_CRITICAL)
			{
				aiMultiTickAction_CallCallback(pMTA, e, false);
				aiMultiTickAction_Destroy(pMTA);
				eaRemove(&aib->powers->ppMultiTickActionQueue, i);
			}
		}
	}
	
}

// ------------------------------------------------------------------------------------------
static void aiMultiTickAction_BeginOrder(Entity *e)
{
	if (e->aibase->fsmContext)
	{
		fsmExitCurState(e->aibase->fsmContext);
	}
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_HasAction(Entity* e, AIVarsBase* aib)
{
	return eaSize(&aib->powers->ppMultiTickActionQueue) != 0;
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_HasForcedActionQueued(Entity* e, AIVarsBase* aib)
{
	FOR_EACH_IN_EARRAY(aib->powers->ppMultiTickActionQueue, AIPowerMultiTickAction, pMTA);
		if (pMTA->iForcedActionPriority > EForcedActionPriority_NONE)
			return true;
	FOR_EACH_END;

	return false;
}

// ------------------------------------------------------------------------------------------
// Removes any MTA queued that has the patching AIPowerInfo
void aiMultiTickAction_RemoveQueuedAIPowerInfos(Entity* e, AIVarsBase* aib, AIPowerInfo *powInfo)
{
	S32 i;
	for (i = eaSize(&aib->powers->ppMultiTickActionQueue) - 1; i >= 0; i--)
	{
		AIPowerMultiTickAction *pMTA = aib->powers->ppMultiTickActionQueue[i];
		if (pMTA->powInfo == powInfo)
		{
			eaRemove(&aib->powers->ppMultiTickActionQueue, i);
			aiMultiTickAction_Destroy(pMTA);
		}
	}
}


// ------------------------------------------------------------------------------------------
static int aiMultiTickAction_QueueInternal(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction *pNewMTA, bool bClearQueue)
{
	if (bClearQueue)
	{
		aiMultiTickAction_ClearQueueEx(e, aib, false);
	}

	eaPush(&aib->powers->ppMultiTickActionQueue, pNewMTA);

	if (eaSize(&aib->powers->ppMultiTickActionQueue) == 1)
	{
		if (pNewMTA->iForcedActionPriority > 0)
		{
			aiMultiTickAction_BeginOrder(e);
			return true;
		}
		else
		{
			return aiPowersDoMTA(e, aib, pNewMTA);
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------
static int aiMultiTickAction_compare(AIPowerMultiTickAction *lhs, AIPowerMultiTickAction *rhs)
{
	return lhs->targetRef == rhs->targetRef &&
			lhs->type == rhs->type &&
			lhs->powInfo == rhs->powInfo &&
			lhs->forceUseTarget && rhs->forceUseTarget &&
			lhs->iForcedActionPriority == rhs->iForcedActionPriority &&
			nearSameVec3(lhs->targetPos, rhs->targetPos);
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_QueuePower(Entity* e, AIVarsBase* aib, Entity* target,
								 AIPowerActionType actionType, AIPowerInfo* powerInfo,
								 U32 flags, MultiTickActionClearedCallback cb)
{
	AIPowerMultiTickAction *pMTA;

	if (!target)
		return false;

	pMTA = aiMultiTickAction_Create();
	pMTA->type = actionType;
	pMTA->targetRef = entGetRef(target);
	pMTA->powInfo = powerInfo;
	pMTA->hostileTarget = critter_IsKOS(entGetPartitionIdx(e), e, target);
	pMTA->forceUseTarget = !!(flags & MTAFlag_FORCEUSETARGET);
	pMTA->combatNoMove = !!(flags & MTAFlag_COMBATNOMOVE);
	pMTA->iForcedActionPriority = (flags & MTAFlag_USERFORCEDACTION) ? EForcedActionPriority_USER : EForcedActionPriority_NONE;
	pMTA->completedCB = cb;

	

	{
		bool isUserForcedAction = !!(flags & MTAFlag_USERFORCEDACTION);

		// if it's a user action, we should check that they are not sending a duplicate action to be performed
		if (isUserForcedAction)
		{
			S32 numQueued = eaSize(&aib->powers->ppMultiTickActionQueue);
			if (numQueued)
			{
				AIPowerMultiTickAction *pCurrentMTA = aib->powers->ppMultiTickActionQueue[0];
				devassert(pCurrentMTA);
				if (aiMultiTickAction_compare(pCurrentMTA, pMTA))
				{
					// being told to do the same thing, don't do anything
					// but report success
					return true;
				}
			}
		}

		return aiMultiTickAction_QueueInternal(e, aib, pMTA, isUserForcedAction);
	}
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_QueueGotoPos(Entity *e, AIVarsBase *aib, const Vec3 vGotoPos)
{
	AIPowerMultiTickAction *pMTA;

	pMTA = aiMultiTickAction_Create();
	
	copyVec3(vGotoPos, pMTA->targetPos);
	pMTA->type = AI_POWER_ACTION_GOTOPOS;
	pMTA->iForcedActionPriority = EForcedActionPriority_USER;

	return aiMultiTickAction_QueueInternal(e, aib, pMTA, false);
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_QueueCombatRoll(Entity *e, AIVarsBase *aib, const Vec3 vRollVec)
{
	AIPowerMultiTickAction *pMTA;

	pMTA = aiMultiTickAction_Create();

	copyVec3(vRollVec, pMTA->targetPos);
	pMTA->type = AI_POWER_ACTION_ROLL;
	pMTA->iForcedActionPriority = EForcedActionPriority_INTERNAL_CRITICAL;

	return aiMultiTickAction_QueueInternal(e, aib, pMTA, false);
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_QueueSafetyTeleport(Entity *e, AIVarsBase *aib, const Vec3 vTeleportPos)
{
	AIPowerMultiTickAction *pMTA;

	pMTA = aiMultiTickAction_Create();
	
	copyVec3(vTeleportPos, pMTA->targetPos);
	pMTA->type = AI_POWER_ACTION_SAFETY_TELEPORT;
	pMTA->iForcedActionPriority = EForcedActionPriority_INTERNAL_CRITICAL;

	return aiMultiTickAction_QueueInternal(e, aib, pMTA, true);
}

// ------------------------------------------------------------------------------------------
SA_ORET_OP_VALID static Entity* aiPowersMTA_GetTargetInfo(Entity* e, const AIPowerMultiTickAction* action, SA_PARAM_NN_VALID F32 *dist)
{
	Entity* targetBE = entFromEntityRef(entGetPartitionIdx(e), action->targetRef);

	if(targetBE && (U32)critter_IsKOS(entGetPartitionIdx(e), e, targetBE) == action->hostileTarget)
	{
		*dist = entGetDistance(e, NULL, targetBE, NULL, NULL);
		return targetBE;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------
static int aiPowersDoMTA_ExplicitPower(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction* action)
{
	F32 dist = 0.f;
	Entity *targetBE;
	AIPowerConfig* curPowConfig;
	bool bIsUserAction = action->iForcedActionPriority == EForcedActionPriority_USER;

	if (action->usedPower)
	{
		if (!aiIsUsingPowers(e, aib))
		{
			action->finishedAction = true;
		}
		return true;
	}

	targetBE = aiPowersMTA_GetTargetInfo(e, action, &dist);
	if (!targetBE)
	{
		action->finishedAction = true;
		aiMultiTickAction_CallCallback(action, e, false);
		return false;
	}

	devassert(action->powInfo);

	curPowConfig = aiGetPowerConfig(e, aib, action->powInfo);
	
	if(!bIsUserAction && !aiPowersAllowedToExecutePower(e, aib, targetBE, action->powInfo, curPowConfig, NULL))
	{
		action->finishedAction = true;
		aiMultiTickAction_CallCallback(action, e, false);
		return false;
	}

	// ignoring min distance for now
	// but what really needs to happen is that if the entity is within the min distance that we need to find a position
	// that is min+ distance away from the target.
	// I tried something trivial, but it needs to validate the position and I'm not ready to do that right now
	if(targetBE == e || /*dist >= curPowConfig->minDist &&*/ dist <= curPowConfig->maxDist)
	{
		if(!action->powInfo->isInterruptedOnMovement || action->stoppedMoving)
		{
			action->usedPower = true;
			aiUsePower(e, aib, action->powInfo, targetBE, targetBE, NULL, false, NULL, action->forceUseTarget, bIsUserAction);
			// if we have a callback to tell someone that we've completed this power
			aiMultiTickAction_CallCallback(action, e, true);
		}

		action->stoppedMoving = true;
		if (!action->combatNoMove)
			aiMovementClearMovementTarget(e, aib);
		return true;
	}
	
	if (!action->combatNoMove)
	{
		aiMovementSetTargetEntity(e, aib, targetBE, NULL, 0, AI_MOVEMENT_ORDER_ENT_UNSPECIFIED, AI_MOVEMENT_TARGET_CRITICAL);
	}
	else
	{	// not allowed to move, we can't perform the action
		action->finishedAction = true;
		aiMultiTickAction_CallCallback(action, e, false);
		return false;
	}
	
	return true;
}

// ------------------------------------------------------------------------------------------
static int aiPowersDoMTA_PowerAction(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction* action)
{
	S32 i;
	F32 dist = 0.f;
	Entity *targetBE;
	AIPowerInfo* bestInfo = NULL;
	AIPowerConfig* bestConfig = NULL;
	F32 bestDist = FLT_MAX;


	targetBE = aiPowersMTA_GetTargetInfo(e, action, &dist);
	if (!targetBE)
	{
		action->finishedAction = true;
		return false;
	}

	for(i = eaSize(&aib->powers->powInfos)-1; i >= 0; i--)
	{
		AIPowerInfo* curPowInfo = aib->powers->powInfos[i];
		int validPower = false;

		switch(action->type)
		{
			acase AI_POWER_ACTION_HEAL:
				validPower = curPowInfo->isHealPower;
			xcase AI_POWER_ACTION_SHIELD_HEAL:
				validPower = curPowInfo->isShieldHealPower;
			xcase AI_POWER_ACTION_RES:
				validPower = curPowInfo->isResPower;
			xcase AI_POWER_ACTION_BUFF:
				validPower = curPowInfo->isBuffPower;
		}

		if(validPower)
		{
			AIPowerConfig* curPowConfig = aiGetPowerConfig(e, aib, curPowInfo);

			if(curPowConfig->absWeight <= 0.0)
				continue;

			if(!aiPowersAllowedToExecutePower(e, aib, targetBE, curPowInfo, curPowConfig, NULL))
				continue;

			if(targetBE == e ||
				dist >= curPowConfig->minDist && dist <= curPowConfig->maxDist)
			{
				action->finishedAction = true;
				aiUsePower(e, aib, curPowInfo, targetBE, targetBE, NULL, false, NULL, false, false);
				PERFINFO_AUTO_STOP();
				return true;
			}
			else if(dist > curPowConfig->maxDist)
			{
				F32 distDiff = curPowConfig->maxDist - dist;
				if(distDiff < bestDist)
				{
					bestInfo = curPowInfo;
					bestConfig = curPowConfig;
				}
			}
			else
			{
				F32 distDiff = dist - curPowConfig->maxDist;
				if(distDiff < bestDist)
				{
					bestInfo = curPowInfo;
					bestConfig = curPowConfig;
				}
			}
		}
	}

	// haven't found a power in range
	if(bestInfo)
	{
		aiMovementSetTargetEntity(e, aib, targetBE, NULL, 0, AI_MOVEMENT_ORDER_ENT_UNSPECIFIED, AI_MOVEMENT_TARGET_CRITICAL);
		return true;
	}

	action->finishedAction = true;
	return false;
}

// ------------------------------------------------------------------------------------------
static int aiPowersDoMTA_GotoPos(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction* action)
{
	Vec3 vCurPos;

	entGetPos(e, vCurPos);

	if (distance3Squared(vCurPos, action->targetPos) <= SQR(3.f))
	{
		action->finishedAction = true;
		return true;
	}

	if (!aiMovementSetTargetPosition(e, aib, action->targetPos, NULL, AI_MOVEMENT_TARGET_CRITICAL))
	{
		action->finishedAction = true;
		return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------
static int aiPowersDoMTA_Roll(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction* action)
{
	// flags usages are misnomers
	if (! action->usedPower)
	{
		aiMovementClearMovementTarget(e, aib);
		mrTacticalPerformRoll(e->mm.mrTactical, getVec3Yaw(action->targetPos), mmGetProcessCountAfterSecondsFG(0));
		if (aib->attackTarget)
		{
			aiMovementSetFinalFaceEntity(e, aib, aib->attackTarget);
		}
		action->usedPower = true;
	}
	else if (! action->stoppedMoving)
	{	// hacky - wait an ai think tick 
		action->stoppedMoving = true;
	}
	else if (!e->mm.isRolling)
	{
		action->finishedAction = true;
	}

	return true;
}

// ------------------------------------------------------------------------------------------
static int aiPowersDoMTA_SafteyTeleport(Entity *e, AIVarsBase *aib, AIPowerMultiTickAction *action)
{
	int partitionIdx = entGetPartitionIdx(e);
	if (! action->stoppedMoving)
	{
		AIAnimList *pAnimList = NULL;
		
		aiMovementResetPath(e, aib);
		action->stoppedMoving = true;
		action->timer = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(0.95f);

		// start the teleport in anim
		{
			ExprFuncReturnVal retval;
			MultiVal answer = {0};
			char *pErrStr = NULL;
			retval = exprContextGetExternVar(aib->exprContext, "Encounter", "ai_TeleportInAnim", MULTI_STRING, &answer, &pErrStr);
			if(retval != ExprFuncReturnError && answer.str[0]) 
			{
				pAnimList = RefSystem_ReferentFromString("AIAnimList", answer.str);
			}
		}

		// start the teleport in anim
		if(pAnimList)
		{
			aiAnimListSet(e, pAnimList, &action->pAnimQueue);
		}

		// wait until the next tick to actually teleport
	}
	else if (! action->usedPower) 
	{
		action->usedPower = true;

		{
			Vec3 vDestPos;
			aiFindGroundPosition(worldGetActiveColl(entGetPartitionIdx(e)), action->targetPos, vDestPos);
			entSetPos(e, vDestPos, 1, "LeashTeleport");
		}
	}
	else 
	{
		if (ABS_TIME_PARTITION(partitionIdx) > action->timer)	
		{
			// stop the anim
			if (action->pAnimQueue)
			{
				CommandQueue_ExecuteAllCommands(action->pAnimQueue);
				CommandQueue_Destroy(action->pAnimQueue);
				action->pAnimQueue = NULL;
			}
			action->finishedAction = true;
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------
static int aiPowersDoMTA(Entity* e, AIVarsBase* aib, AIPowerMultiTickAction* action)
{
	// if this is a user forced action, ignore the fact that we are using powers
	if (action->iForcedActionPriority != EForcedActionPriority_USER && aiIsUsingPowers(e, aib))
		return true;

	// todo: refactor MTAs- 
	// MTAs should probably become sub-classed with some handlers 
	// as they are becoming used for more than just power usages
	if (action->powInfo)
	{
		return aiPowersDoMTA_ExplicitPower(e, aib, action);
	}
	else 
	{
		switch(action->type)
		{
			case AI_POWER_ACTION_SHIELD_HEAL:
			case AI_POWER_ACTION_HEAL:
			case AI_POWER_ACTION_RES:
			case AI_POWER_ACTION_BUFF:
			case AI_POWER_ACTION_OUT_OF_COMBAT:
				return aiPowersDoMTA_PowerAction(e, aib, action);
			
			xcase AI_POWER_ACTION_GOTOPOS:
				return aiPowersDoMTA_GotoPos(e, aib, action);

			xcase AI_POWER_ACTION_ROLL:
				return aiPowersDoMTA_Roll(e, aib, action);
			
			xcase AI_POWER_ACTION_SAFETY_TELEPORT:
				return aiPowersDoMTA_SafteyTeleport(e, aib, action);

			xdefault:
				devassertmsg(0, "Bad MTA");
			return false;
		}
	}
}

// ------------------------------------------------------------------------------------------
static AIPowerMultiTickAction* popMTAQueue(Entity *e, AIPowerMultiTickAction ***pppMTAQueue)
{
	// current MultiTickAction has completed or was terminated
	AIPowerMultiTickAction* pCurMTA = eaRemove(pppMTAQueue, 0);
	if (pCurMTA)
	{
		aiMultiTickAction_Destroy(pCurMTA);
		pCurMTA = NULL;
	}

	pCurMTA	= eaHead(pppMTAQueue);
	if (pCurMTA)
	{
		if (pCurMTA->iForcedActionPriority > EForcedActionPriority_NONE)
		{
			aiMultiTickAction_BeginOrder(e);
		}
		return pCurMTA;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------
int aiMultiTickAction_ProcessActions(Entity* e, AIVarsBase* aib)
{
	AIPowerMultiTickAction* pCurMTA = eaHead(&aib->powers->ppMultiTickActionQueue);
	S32 numIterations = 2;

	while (pCurMTA && --numIterations)
	{
		if (pCurMTA->finishedAction == true)
		{	// current MultiTickAction has completed or was terminated
			// continue onto the next
			pCurMTA = popMTAQueue(e, &aib->powers->ppMultiTickActionQueue);
			continue;
		}

		// target is still valid
		aiPowersDoMTA(e, aib, pCurMTA);
		if (pCurMTA->finishedAction == true)
		{
			popMTAQueue(e, &aib->powers->ppMultiTickActionQueue);
		}
		return true;
	}

	return false;
}
