#include "aiMovementModes.h"

#include "aiConfig.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiMultiTickAction.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "Character.h"
#include "CombatConfig.h"
#include "EntityMovementTactical.h"
#include "gslMapState.h"
#include "rand.h"



static AIMovementMode* aiMovementMode_Create(Entity *pOwner, AIMovementModeType type);

static void aiMovementModeSprint_SetupHandlers();
static void aiMovementModeAim_SetupHandlers();
static void aiMovementModeRoll_SetupHandlers();
static void aiMovementModeSafetyTeleport_SetupHandlers();

typedef void (*AIMovementModeUpdate)(AIMovementMode *movementMode, AIMovementModeManager *manager);
typedef void (*AIMovementModeEnable)(AIMovementMode *movementMode, int bEnable);

// movement mode function table
typedef struct AIMovementModeHandlers
{
	AIMovementModeUpdate	fpUpdate;
	AIMovementModeEnable	fpEnable;
} AIMovementModeHandlers;

// Base movement mode struct
typedef struct AIMovementMode
{
	const AIMovementModeType eType;
	const AIMovementModeHandlers * const pHandler;

	Entity *pOwner;

	S64 timeLastToggle;

	U32 isExclusive : 1;
	U32 bActive : 1;
	U32 isFireAndForget : 1;
} AIMovementMode;

#define AIMOVEMENTMODE(amm)		(&(amm)->base)

static AIMovementModeHandlers s_SprintHandler = {0};
static AIMovementModeHandlers s_AimHandler = {0};
static AIMovementModeHandlers s_RollHandler = {0};
static AIMovementModeHandlers s_SafetyTeleport = {0};

// -------------------------------------------------------------------------------------------
AUTO_RUN;
void aiMovementModes_SetupHandlers(void)
{
	aiMovementModeSprint_SetupHandlers();
	aiMovementModeAim_SetupHandlers();
	aiMovementModeRoll_SetupHandlers();
	aiMovementModeSafetyTeleport_SetupHandlers();
}


// -------------------------------------------------------------------------------------------
// AIMovementModeManager
// -------------------------------------------------------------------------------------------

void aiMovementModeManager_CreateAndInitFromConfig(Entity *e, AIVarsBase *aib, AIConfig *pConfig)
{
	S32 count;

	if(!pConfig)
		return;

	count = eaiSize(&pConfig->peMovementModeTypes);
	if (count)
	{
		S32 i;

		// create
		{
			aib->movementModeManager = calloc(1, sizeof(AIMovementModeManager));
			aib->movementModeManager->pOwner = e;
		}

		for(i = 0; i < count; i++)
		{
			AIMovementModeType type = pConfig->peMovementModeTypes[i];
			aiMovementModeManager_AddMovementMode(aib->movementModeManager, type);
		}
	}
}

void aiMovementModeManager_Destroy(AIMovementModeManager **ppManager)
{
	if(*ppManager)
	{
		AIMovementModeManager *manager = *ppManager;
		
		eaDestroyEx(&manager->eaMovementModes, NULL);
		free(manager);
		
		*ppManager = NULL;
	}
}


// -------------------------------------------------------------------------------------------
int aiMovementModeManager_AddMovementMode(AIMovementModeManager *manager, AIMovementModeType type)
{
	AIMovementMode *pNewMode;

	// for now, not allowing multiple of the same type
	FOR_EACH_IN_EARRAY(manager->eaMovementModes, AIMovementMode, pMode)
		if(pMode->eType == type)
			return false;
	FOR_EACH_END
	
	devassertmsg(manager->pOwner, "aiMovementModeManager was not initialized.");

	pNewMode = aiMovementMode_Create(manager->pOwner, type);
	if(pNewMode)
	{
		eaPush(&manager->eaMovementModes, pNewMode);
		return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------------
void aiMovementModeManager_Update(AIMovementModeManager *manager)
{
	PERFINFO_AUTO_START_FUNC();
	if (manager)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(manager->eaMovementModes, AIMovementMode, pMode)
			pMode->pHandler->fpUpdate(pMode, manager);
			if (manager->pActiveExclusiveMode == pMode)
				break;
		FOR_EACH_END
	}
	PERFINFO_AUTO_STOP();
}

static AIMovementMode* aiMovementModeManager_GetActiveExclusiveMode(AIMovementModeManager *manager)
{
	return manager->pActiveExclusiveMode;
}

static void aiMovementModeManager_EnableMode(AIMovementModeManager *manager, AIMovementMode *mode, bool bEnable)
{
	if (mode->bActive == (U32)!!bEnable)
		return; // already enabled/disabled

	if (!bEnable)
	{	// always disable the mode
		mode->pHandler->fpEnable(mode, false);

		// if we are disabling the current exclusive mode, clear it
		if (manager->pActiveExclusiveMode == mode)
		{
			manager->pActiveExclusiveMode = NULL;
		}
		return;
	}
	else
	{
		if (manager->pActiveExclusiveMode && manager->pActiveExclusiveMode != mode)
		{
			manager->pActiveExclusiveMode->pHandler->fpEnable(manager->pActiveExclusiveMode, false);
			manager->pActiveExclusiveMode = NULL;
		}
		
		if (mode->isExclusive)
		{	// turn off any other modes 
			FOR_EACH_IN_EARRAY(manager->eaMovementModes, AIMovementMode, pMode)
				if (pMode->bActive)
					pMode->pHandler->fpEnable(pMode, false);
			FOR_EACH_END

			manager->pActiveExclusiveMode = mode;
		}

		mode->pHandler->fpEnable(mode, true);
	}
	
}

// -------------------------------------------------------------------------------------------
// AIMovementMode
// -------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------
static void aiMovementMode_Init(Entity *pOwner, AIMovementMode *movementMode, AIMovementModeType type)
{
#define ASSIGN_CONST(a,typ,v)		{typ *p = (typ*)(&(a)); *p=(v);}
	movementMode->pOwner = pOwner;

	ASSIGN_CONST(movementMode->eType, AIMovementModeType, type);
	
	// set up the handler 
	switch(movementMode->eType)
	{
		xcase AIMovementModeType_SPRINT:
			ASSIGN_CONST(movementMode->pHandler, AIMovementModeHandlers*, &s_SprintHandler);
			
		xcase AIMovementModeType_ROLL:
			ASSIGN_CONST(movementMode->pHandler, AIMovementModeHandlers*, &s_RollHandler);

		xcase AIMovementModeType_AIM:
			ASSIGN_CONST(movementMode->pHandler, AIMovementModeHandlers*, &s_AimHandler);
		
		xcase AIMovementModeType_SAFETY_TELEPORT:
			ASSIGN_CONST(movementMode->pHandler, AIMovementModeHandlers*, &s_SafetyTeleport);

		xdefault:
			devassertmsg(0, "Bad movement mode type; no handler specified.");
	}
}


// -------------------------------------------------------------------------------------------
// returns current dist to target squared
__forceinline static F32 _getMovementTargetDistSQR(Entity *e, Vec3 vOutCurPos, Vec3 vOutCurTargetPos)
{
	// get our target move position 
	if (! aiMovementGetTargetPosition(e, e->aibase, vOutCurTargetPos))
	{
		return 0.f;
	}

	entGetPos(e, vOutCurPos);
	return distance3Squared(vOutCurPos, vOutCurTargetPos);
}


// -------------------------------------------------------------------------------------------
static F32 aiMovementMode_GetMovementTargetDistSQR(Entity *pOwner)
{
	Vec3 vTargetPos, vCurPos;

	return _getMovementTargetDistSQR(pOwner, vCurPos, vTargetPos);
	/*
	if (aiMovementGetTargetPosition(pOwner, pOwner->aibase, vTargetPos))
	{
		entGetPos(pOwner, vCurPos);
		return distance3Squared(vCurPos, vTargetPos);
	}
	
	return 0.f;
	*/
}

// -------------------------------------------------------------------------------------------
static void aiMovementMode_Enable(AIMovementMode *movementMode, bool bEnable)
{
	int partitionIdx = entGetPartitionIdx(movementMode->pOwner);
	if (movementMode->isFireAndForget)
	{	// fire and forget movement modes don't get activated
		// just record the time
		movementMode->timeLastToggle = ABS_TIME_PARTITION(partitionIdx);
		return;
	}

	if (movementMode->bActive != (U32)bEnable)
	{
		movementMode->timeLastToggle = ABS_TIME_PARTITION(partitionIdx);
		movementMode->bActive = bEnable;
	}
}

// -------------------------------------------------------------------------------------------
static F32 aiMovementMode_GetTimeSinceLastToggle(AIMovementMode *movementMode)
{
	int partitionIdx = entGetPartitionIdx(movementMode->pOwner);
	S64 timeSince = ABS_TIME_SINCE_PARTITION(partitionIdx, movementMode->timeLastToggle);
	return ABS_TIME_TO_SEC(timeSince);
}

// -------------------------------------------------------------------------------------------
// AIMovementModeSprint - AIMovementModeType_SPRINT
// -------------------------------------------------------------------------------------------

typedef struct AIMovementModeSprint
{
	AIMovementMode		base;

} AIMovementModeSprint;

static const F32 sSprint_StartSprintDist = 30.f;
static const F32 sSprint_StopSprintDist = 15.f;


// -------------------------------------------------------------------------------------------
static AIMovementModeSprint* aiMovementModeSprint_Create(Entity *pOwner)
{
	AIMovementModeSprint *p = calloc(1, sizeof(AIMovementModeSprint));
		
	aiMovementMode_Init(pOwner, AIMOVEMENTMODE(p), AIMovementModeType_SPRINT);
	
	p->base.isExclusive = true;
	return p;
}

static void aiMovementModeSprint_EnableSprint(AIMovementModeSprint *movementMode, bool bEnable) 
{
	mrTacticalSetRunMode(movementMode->base.pOwner->mm.mrTactical, bEnable);
	aiMovementMode_Enable(AIMOVEMENTMODE(movementMode), bEnable);
}

static void aiMovementModeSprint_Update(AIMovementModeSprint *movementMode, AIMovementModeManager *manager)
{
	Entity *pOwner = movementMode->base.pOwner;
	if(!pOwner->mm.mrTactical)
		return;

	if (movementMode->base.bActive != pOwner->mm.isSprinting)
	{	// just in case we are not in the current mode we expect to be in
		aiMovementModeSprint_EnableSprint(movementMode, movementMode->base.bActive);
	}

	if (movementMode->base.bActive)
	{
		if(pOwner->erOwner && pOwner->aibase->team->combatState != AITEAM_COMBAT_STATE_FIGHT)
		{
			Entity *pController = entFromEntityRef(entGetPartitionIdx(pOwner), pOwner->erOwner);
			if (pController && pController->mm.isSprinting)
				return;
		}

		if(aiMovementMode_GetMovementTargetDistSQR(pOwner) <= SQR(sSprint_StopSprintDist))
		{
			aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), false);
		}
	}
	else
	{
		
		if(pOwner->erOwner && pOwner->aibase->team->combatState != AITEAM_COMBAT_STATE_FIGHT)
		{
			Entity *pController = entFromEntityRef(entGetPartitionIdx(pOwner), pOwner->erOwner);
			if (pController && pController->mm.isSprinting)
			{
				aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), true);
				return;
			}
		}
		
		if(aiMovementMode_GetMovementTargetDistSQR(pOwner) >= SQR(sSprint_StartSprintDist))
		{
			aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), true);
		}
	}
}


static void aiMovementModeSprint_SetupHandlers()
{
	s_SprintHandler.fpUpdate = (AIMovementModeUpdate) aiMovementModeSprint_Update;
	s_SprintHandler.fpEnable = (AIMovementModeEnable) aiMovementModeSprint_EnableSprint;
}


// -------------------------------------------------------------------------------------------
// AIMovementModeType_AIM
// -------------------------------------------------------------------------------------------

typedef struct AIMovementModeAim
{
	AIMovementMode		base;

} AIMovementModeAim;

static const F32 sAim_DisableAimMoveDist = 5.f;
static const F32 sAim_EnableAimMoveDist = 10.f;
static const F32 sAim_ChanceToStartAiming = 50.f;
static const F32 sAim_ChanceToStopAiming = 30.f;
static const F32 sAim_LastAimThrottleTime = 2.5f;

static int randomChance(F32 fChance)
{
	return (randomPositiveF32() * 100.f) < fChance;
}
static int wasDamagedRecently(Entity *e)
{
	int partitionIdx = entGetPartitionIdx(e);
	return ABS_TIME_SINCE_PARTITION(partitionIdx, e->aibase->time.lastDamage[AI_NOTIFY_TYPE_DAMAGE]) <= SEC_TO_ABS_TIME(2);
}

// -------------------------------------------------------------------------------------------
static AIMovementModeAim* aiMovementModeAim_Create(Entity *pOwner)
{
	AIMovementModeAim *p = calloc(1, sizeof(AIMovementModeAim));

	aiMovementMode_Init(pOwner, AIMOVEMENTMODE(p), AIMovementModeType_AIM);
	return p;
}

// -------------------------------------------------------------------------------------------
static void aiMovementModeAim_Enable(AIMovementModeAim *movementMode, int bEnable) 
{
	mrTacticalSetAimMode(movementMode->base.pOwner->mm.mrTactical, bEnable);
	aiMovementMode_Enable(AIMOVEMENTMODE(movementMode), bEnable);
}

// -------------------------------------------------------------------------------------------
static void aiMovementModeAim_Update(AIMovementModeAim *movementMode, AIMovementModeManager *manager)
{
	Entity *pOwner = movementMode->base.pOwner;
	if(!pOwner->mm.mrTactical || !pOwner->pChar)
		return;

	if (movementMode->base.bActive != pOwner->pChar->bIsCrouching)
	{	// just in case we are not in the current mode we expect to be in
		aiMovementModeAim_Enable(movementMode, movementMode->base.bActive);
	}

	if (movementMode->base.bActive)
	{
		if (pOwner->aibase->team->combatState != AITEAM_COMBAT_STATE_FIGHT)
		{
			Entity *pController = pOwner->erOwner ? entFromEntityRef(entGetPartitionIdx(pOwner), pOwner->erOwner) : NULL;

			if (pController)
			{	// when ooc - only go into aiming if my owner is
				if (!pController->pChar || !pController->pChar->bIsCrouching)
				{
					aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), false);
				}

				return;
			}

			aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), false);
			return;
		}
		else if (pOwner->aibase->team->combatState == AITEAM_COMBAT_STATE_FIGHT)
		{
			if(aiMovementMode_GetMovementTargetDistSQR(pOwner) >= SQR(sAim_DisableAimMoveDist))
			{
				aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), false);
				return;
			}
			else
			{
				if (wasDamagedRecently(pOwner) && 
					(aiMovementMode_GetTimeSinceLastToggle(AIMOVEMENTMODE(movementMode)) > sAim_LastAimThrottleTime) &&
					randomChance(sAim_ChanceToStopAiming))
				{
					aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), false);
					return;
				}
				
			}
		}
	}
	else
	{
		if (pOwner->aibase->team->combatState != AITEAM_COMBAT_STATE_FIGHT)
		{
			if (pOwner->erOwner)
			{
				Entity *pController = entFromEntityRef(entGetPartitionIdx(pOwner), pOwner->erOwner);
				if (pController && pController->pChar && pController->pChar->bIsCrouching)
				{
					aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), true);
					return;
				}
			}
		}
		else if (pOwner->aibase->team->combatState == AITEAM_COMBAT_STATE_FIGHT)
		{
			F32 fPrefMaxRange = aiGetPreferredMaxRange(pOwner, pOwner->aibase);

			if(fPrefMaxRange > 10.f && 
				aiMovementMode_GetTimeSinceLastToggle(AIMOVEMENTMODE(movementMode)) > 5.f &&
				aiMovementMode_GetMovementTargetDistSQR(pOwner) <= SQR(sAim_EnableAimMoveDist))
			{
				F32 aimChance = sAim_ChanceToStartAiming;
				if (wasDamagedRecently(pOwner))
					aimChance *= 0.75f;
			
				if (randomChance(aimChance))
				{
					aiMovementModeManager_EnableMode(manager, AIMOVEMENTMODE(movementMode), true);
					return;
				}
			}
		}
		
	}
	
}

static void aiMovementModeAim_SetupHandlers()
{
	s_AimHandler.fpUpdate = (AIMovementModeUpdate) aiMovementModeAim_Update;
	s_AimHandler.fpEnable = (AIMovementModeEnable) aiMovementModeAim_Enable;
}

// -------------------------------------------------------------------------------------------
// AIMovementModeType_ROLL
// -------------------------------------------------------------------------------------------

typedef struct AIMovementModeRoll
{
	AIMovementMode		base;

} AIMovementModeRoll;

static const F32 sRoll_Throttle = 3.25f;
static const F32 sRoll_Chance = 35.f;

// -------------------------------------------------------------------------------------------
static AIMovementModeRoll* aiMovementModeRoll_Create(Entity *pOwner)
{
	AIMovementModeRoll *p = calloc(1, sizeof(AIMovementModeRoll));

	aiMovementMode_Init(pOwner, AIMOVEMENTMODE(p), AIMovementModeType_ROLL);
	p->base.isFireAndForget = true;
	return p;
}

// -------------------------------------------------------------------------------------------
static void aiMovementModeRoll_Enable(AIMovementModeRoll *movementMode, int bEnable) 
{
	// mrTacticalPerformRoll(movementMode->base.pOwner->mrTactical);
	aiMovementMode_Enable(AIMOVEMENTMODE(movementMode), bEnable);
}


// -------------------------------------------------------------------------------------------
static int aiMovementModeRoll_GetRollTargetOrigin(Entity *e, AIVarsBase *aib, Vec3 vRollTargetOrigin)
{
	S32 count;
	if (aib->attackTarget)
	{
		entGetPos(aib->attackTarget, vRollTargetOrigin);
		return true;
	}

	count = eaSize(&aib->attackerList);
	if (count)
	{
		S32 i;

		entGetPos(aib->attackerList[0], vRollTargetOrigin);

		for (i = 1; i < count; i++)
		{
			Vec3 vPos;
			entGetPos(aib->attackerList[i], vPos);
			interpVec3(0.5f, vPos, vRollTargetOrigin, vRollTargetOrigin);
		}

		return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------------
static int aiMovementModeRoll_ValidateRollVec(Entity *e, AIVarsBase *aib, const Vec3 vCurPos, Vec3 vRollVec)
{
	Vec3 vTargetRollPos; 
	Vec3 vHitPtOut;
	AIConfig *config;
	AICollideRayFlag flags;

	addVec3(vCurPos, vRollVec, vTargetRollPos);
	
	// check if the target position is within some of our position restrictions
	// check if it will move outside of our preferred range of our current target
	if (aib->attackTarget)
	{
		F32 fPrefMinRange = aiGetPreferredMinRange(e, aib);
		F32 fPrefMaxRange = aiGetPreferredMaxRange(e, aib);
		Vec3 vAttackPos, vFromCurToAttack, vFromRollPosToAttack;
		F32 fDistSq;
	
		entGetPos(aib->attackTarget, vAttackPos);
		
		fDistSq = distance3Squared(vTargetRollPos, vAttackPos);
		
		if (fDistSq < SQR(fPrefMinRange) || fDistSq > SQR(fPrefMaxRange))
			return 0; // the roll will put us out of range

		subVec3(vAttackPos, vCurPos, vFromCurToAttack);
		subVec3(vAttackPos, vTargetRollPos, vFromRollPosToAttack);
		if( dotVec3(vFromCurToAttack, vFromRollPosToAttack) < 0.f)
			return 0;// will be rolling passed the target
	}

	config = aiGetConfig(e, aib);
	if (config->combatMovementParams.combatPositioningUseCoherency || aib->leashTypeOverride != AI_LEASH_TYPE_DEFAULT)
	{
		Vec3 vLeashPos; 
		F32 coherencyDist;

		aiGetLeashPosition(e, aib, vLeashPos);
		coherencyDist = aiGetCoherencyDist(e, config);
		if (distance3Squared(vLeashPos, vTargetRollPos) > SQR(coherencyDist))
			return 0; // roll will put us out of coherency 
	}


	flags = AICollideRayFlag_DOWALKCHECK | AICollideRayFlag_DOCAPSULECHECK;
	if (aiCollideRayEx(entGetPartitionIdx(e), e, vCurPos, NULL, vTargetRollPos, flags, AI_DEFAULT_STEP_LENGTH, NULL, vHitPtOut))
	{
		// hit something? todo: find out if the position is good enough to use anyway
		return 0;
	}

	return 1;
}



// -------------------------------------------------------------------------------------------
static void aiMovementModeRoll_Update(AIMovementModeRoll *movementMode, AIMovementModeManager *manager)
{
	Entity *pOwner = movementMode->base.pOwner;
	if(!pOwner->mm.mrTactical)
		return;

	if( pOwner->aibase->team->combatState != AITEAM_COMBAT_STATE_FIGHT ||
		aiMovementMode_GetTimeSinceLastToggle(AIMOVEMENTMODE(movementMode)) <= sRoll_Throttle ||
		aiMultiTickAction_HasAction(pOwner, pOwner->aibase) || 
		aiIsUsingPowers(pOwner, pOwner->aibase) ||
		!wasDamagedRecently(pOwner) ||
		aiMovementGetMovementOrderType(pOwner, pOwner->aibase) != AI_MOVEMENT_ORDER_NONE)
	{
		return;
	}

	if (randomChance(sRoll_Chance))
	{
		AIVarsBase *aib = pOwner->aibase;
		Vec3 vRollTargetOrigin, vRollDirBasis, vCurPos, vRollVec;
		F32 fRollAngleDelta, fRollAngle, fRollDirBasisAngle;
		F32 fDir = randomBool() ? 1.f : -1.f;

		// get a relative position to roll 'from '
		if (!aiMovementModeRoll_GetRollTargetOrigin(pOwner, aib, vRollTargetOrigin))
			return;

		entGetPos(pOwner, vCurPos);
		subVec3(vCurPos, vRollTargetOrigin, vRollDirBasis);

		fRollDirBasisAngle = getVec3Yaw(vRollDirBasis);
		fRollAngleDelta = RAD(25.f) +  randomPositiveF32() * HALFPI;

		fRollAngle = fixAngle(fRollDirBasisAngle + fRollAngleDelta);
		// todo: probably need to get the pitch of the ground
		sphericalCoordsToVec3(vRollVec, fRollAngle, HALFPI, g_CombatConfig.tactical.roll.rollDef.fRollDistance);

		if (aiMovementModeRoll_ValidateRollVec(pOwner, aib, vCurPos, vRollVec))
		{
			aiMultiTickAction_QueueCombatRoll(pOwner, aib, vRollVec);
			aiMovementModeRoll_Enable(movementMode, true);
			return;
		}


		fRollAngle = fixAngle(fRollDirBasisAngle - fRollAngleDelta);
		// todo: probably need to get the pitch of the ground
		sphericalCoordsToVec3(vRollVec, fRollAngle, HALFPI, g_CombatConfig.tactical.roll.rollDef.fRollDistance);

		if (aiMovementModeRoll_ValidateRollVec(pOwner, aib, vCurPos, vRollVec))
		{
			aiMultiTickAction_QueueCombatRoll(pOwner, aib, vRollVec);
			aiMovementModeRoll_Enable(movementMode, true);
			return;
		}
	}
}

// -------------------------------------------------------------------------------------------
static void aiMovementModeRoll_SetupHandlers()
{
	s_RollHandler.fpUpdate = (AIMovementModeUpdate) aiMovementModeRoll_Update;
	s_RollHandler.fpEnable = (AIMovementModeEnable) aiMovementModeRoll_Enable;
}

// -------------------------------------------------------------------------------------------
// AAIMovementModeType_SAFETY_TELEPORT
// -------------------------------------------------------------------------------------------
static S32 s_bSafteyTeleport_Debug = 0;
AUTO_CMD_INT(s_bSafteyTeleport_Debug, aiMovModeSafetyTp_Debug);

typedef struct AIMovementModeSafetyTeleport
{
	AIMovementMode		base;
	
	Vec3				vLastPos;
	Vec3				vInitialPos;
	F32					fInitialStrandedDistSQR;
	F32					fLastStrandedDistSQR;
	S64					timeStranded;
	F32					fTimeoutInfluence;
	//S64					curBadnessTime;
	//S64					timeLast;

} AIMovementModeSafetyTeleport;


// -------------------------------------------------------------------------------------------
static AIMovementModeSafetyTeleport* aiMovementModeSafetyTeleport_Create(Entity *pOwner)
{
	AIMovementModeSafetyTeleport *p = calloc(1, sizeof(AIMovementModeSafetyTeleport));

	aiMovementMode_Init(pOwner, AIMOVEMENTMODE(p), AIMovementModeType_SAFETY_TELEPORT);
	p->base.isFireAndForget = true;
	return p;
}

// -------------------------------------------------------------------------------------------
static void aiMovementModeSafetyTeleport_Enable(AIMovementModeSafetyTeleport *movementMode, int bEnable) 
{
	aiMovementMode_Enable(AIMOVEMENTMODE(movementMode), bEnable);
}


static F32 s_fAvgStepTime = 0.5f;

// -------------------------------------------------------------------------------------------
static void aiMovementModeSafetyTeleport_Update(AIMovementModeSafetyTeleport *movementMode, 
												AIMovementModeManager *manager)
{
	Entity *pOwner = movementMode->base.pOwner;
	AIConfig *config;
	int partitionIdx = entGetPartitionIdx(pOwner);
	// 
	if(aiMovementMode_GetTimeSinceLastToggle(AIMOVEMENTMODE(movementMode)) < 5.f)
		return;
	
	config = aiGetConfig(pOwner, pOwner->aibase);
	
	if (movementMode->timeStranded)
	{
		Vec3 vTargetPos, vCurPos;
		F32 fCurDistToTargetSQR = _getMovementTargetDistSQR(pOwner, vCurPos, vTargetPos);
		F32 fGainThreshold; 
		if (fCurDistToTargetSQR < SQR(config->movModeSafetyTeleport.fStrandedDist))
		{	// we are close enough again
			movementMode->timeStranded = 0;

			if (s_bSafteyTeleport_Debug)
			{
				printf("aiMovModeSafetyTP: Entity(%d) is within the stranded dist threshold.\n", entGetRef(pOwner));
			}
			return;
		}


		#define MAX_TIME_GAIN	(2.f)
		#define TIMEOUT_LOSS_PER_TICK	(0.25f)	
		#define TIMEOUT_GAIN_PER_TICK	(0.25f)

		// check to see if we are gaining on our destination, above some average assumed speed on our location

		fGainThreshold = config->movModeSafetyTeleport.fAvgDistPerSec * s_fAvgStepTime;
		if (fCurDistToTargetSQR - fGainThreshold >= movementMode->fLastStrandedDistSQR)
		{	// not getting any better
			movementMode->fTimeoutInfluence -= TIMEOUT_LOSS_PER_TICK;
		}
		else 
		{
			movementMode->fLastStrandedDistSQR = fCurDistToTargetSQR;

			movementMode->fTimeoutInfluence += TIMEOUT_GAIN_PER_TICK;
			if (movementMode->fTimeoutInfluence > MAX_TIME_GAIN)
				movementMode->fTimeoutInfluence = MAX_TIME_GAIN;
		}
		

		// Don't allow a teleport right away, give a couple seconds before we try teleporting
		if (! ABS_TIME_PASSED(movementMode->timeStranded, config->movModeSafetyTeleport.fMinTimeCheckingStranded))
		{	
			return;
		}


		if (s_bSafteyTeleport_Debug)
		{
			F32 fTimeStranded;
			S64 timeStranded = ABS_TIME_SINCE_PARTITION(partitionIdx, movementMode->timeStranded);
			fTimeStranded = ABS_TIME_TO_SEC(timeStranded);
			
			printf("aiMovModeSafetyTP: Entity(%d) %.2f seconds."
					"Time influence: %.2f seconds."
					"Time Left: %.2f seconds.\n", 
					entGetRef(pOwner), 
					fTimeStranded,
					movementMode->fTimeoutInfluence,
					(config->movModeSafetyTeleport.fStrandedTimeout + movementMode->fTimeoutInfluence) - fTimeStranded);
		}


		{
			F32 fTargetTimeoutThreshold;

			if (fCurDistToTargetSQR >= SQR(config->movModeSafetyTeleport.fStrandedDistMax))
			{
				fTargetTimeoutThreshold = 0.f;
			}
			else
			{
				fTargetTimeoutThreshold = config->movModeSafetyTeleport.fStrandedTimeout;
			}
			
			
			fTargetTimeoutThreshold += movementMode->fTimeoutInfluence;

			// check if we should teleport, distance or overall time beyond the stranded distance time
			if (ABS_TIME_PASSED(movementMode->timeStranded, fTargetTimeoutThreshold) )
			{	// we've hit a time threshold, do the teleport
				aiMultiTickAction_QueueSafetyTeleport(pOwner, pOwner->aibase, vTargetPos);
				aiMovementModeSafetyTeleport_Enable(movementMode, true);
				if (s_bSafteyTeleport_Debug)
				{
					printf("aiMovModeSafetyTP: Entity(%d) teleporting to target position.\n", entGetRef(pOwner));
				}

				movementMode->timeStranded = 0;
				return;
			}
		}
				
	}
	else
	{
		Vec3 vTargetPos, vCurPos;
		F32 fCurDistToTargetSQR;

		{
			AILeashType leashType = aiGetLeashType(pOwner->aibase);

			// do not allow this teleport if we're in combat, 
			// note: but we are allowing to teleport after this gets triggered and are in combat
			if (aiIsInCombat(pOwner))
				return;
			// maybe somewhat game specific, but don't start the teleport checking 
			// if our current position is a rally point
			if (leashType == AI_LEASH_TYPE_RALLY_POSITION)
				return;
		}
		
		fCurDistToTargetSQR = _getMovementTargetDistSQR(pOwner, vCurPos, vTargetPos);
				
		if(fCurDistToTargetSQR > SQR(config->movModeSafetyTeleport.fStrandedDist))
		{
			// we're far from where we want to go, start checking if we are stranded enough to teleport
			copyVec3(vCurPos, movementMode->vInitialPos);
			copyVec3(vCurPos, movementMode->vLastPos);
			movementMode->fInitialStrandedDistSQR = fCurDistToTargetSQR;
			movementMode->fLastStrandedDistSQR = fCurDistToTargetSQR;
			movementMode->timeStranded = ABS_TIME_PARTITION(partitionIdx);
			movementMode->fTimeoutInfluence = 0;
			//movementMode->curBadnessTime = 0;
			
			if (s_bSafteyTeleport_Debug)
			{
				printf( "aiMovModeSafetyTP: Entity(%d) is too far away." 
						"Beginning stranded test.\n", 
						entGetRef(pOwner));
			}
		}
	}
	
}

// -------------------------------------------------------------------------------------------
static void aiMovementModeSafetyTeleport_SetupHandlers()
{
	s_SafetyTeleport.fpUpdate = (AIMovementModeUpdate) aiMovementModeSafetyTeleport_Update;
	s_SafetyTeleport.fpEnable = (AIMovementModeEnable) aiMovementModeSafetyTeleport_Enable;
}


// -------------------------------------------------------------------------------------------
static AIMovementMode* aiMovementMode_Create(Entity *pOwner, AIMovementModeType type)
{
	switch(type)
	{
		xcase AIMovementModeType_SPRINT:
			return (AIMovementMode*)aiMovementModeSprint_Create(pOwner);

		xcase AIMovementModeType_ROLL: 
			return (AIMovementMode*)aiMovementModeRoll_Create(pOwner);

		xcase AIMovementModeType_AIM:
			return (AIMovementMode*)aiMovementModeAim_Create(pOwner);

		xcase AIMovementModeType_SAFETY_TELEPORT:
			return (AIMovementMode*)aiMovementModeSafetyTeleport_Create(pOwner);

		xdefault:
			return NULL;
	}

	return NULL;
}