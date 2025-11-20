#include "aiConfig.h"
#include "aiCivilian.h"
#include "aiDebug.h"
#include "aiDebugShared.h"
#include "aiJobs.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiCombatJob.h"
#include "aiTeam.h"

#include "AttribModFragility.h"
#include "beaconPath.h"
#include "Character.h"
#include "Character_target.h"
#include "entCritter.h"
#include "encounter_common.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "gslMapState.h"
#include "rand.h"
#include "StateMachine.h"
#include "tokenstore.h"
#include "CombatConfig.h"
#include "ResourceManager.h"

#include "fileutil.h"
#include "FolderCache.h"

#include "autogen/AILib_autogen_QueuedFuncs.h"
#include "aiConfig_h_ast.h"
#include "aiMovement_h_ast.h"

#include "StringCache.h"

#include "aiAmbient.h"

// Chat
#include "aiFCExprFunc.h"

// Animation
#include "AnimList_Common.h"
#include "aiAnimList.h"
#include "PowerAnimFX.h"

// Region Rules (for defaults)
#include "RegionRules.h"

// Ambient Job
#include "wlInteraction.h"
#include "gslInteractable.h"
#include "wlGroupPropertyStructs.h"
#include "wlEncounter.h"
#include "WorldGrid.h"

DictionaryHandle g_hAIAmbientDict;

#define DURATION_VARIANCE (0.30) // +/- 30%
#define DURATION_VARIANCE_HIGH	(0.7f)

extern ParseTable parse_GameInteractLocation[];
#define TYPE_parse_GameInteractLocation GameInteractLocation

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#include "AutoGen/aiAmbient_h_ast.h"

static GameInteractable* aiAmbientJob_FindBestJob(	int partitionIdx, 
													Entity* e, 
													const Vec3 vSearchPos, 
													F32 fRadius,
													bool bInitialJobsOnly);

typedef void (*AIAmbientOnEntryFunc)(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString);
typedef void (*AIAmbientOnExitFunc)(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString);
typedef void (*AIAmbientOnTickFunc)(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString);

typedef struct AIAmbientStateHandler
{
	AIAmbientOnEntryFunc onEntryFunc;
	AIAmbientOnTickFunc onExitFunc;
	AIAmbientOnTickFunc onTickFunc;
} AIAmbientStateHandler;

static StashTable s_stAIAmbientStateHandlers = NULL;

// ------------------------------------------------------------------------------------------------------------------
static void aiAmbientRemoveAnimation(Entity *be, FSMLDAmbient *ambientData)
{
	if(ambientData->addedAnimList)
	{
		CommandQueue_ExecuteAllCommands(ambientData->animationCommandQueue);
		ambientData->addedAnimList = 0;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiAmbient_JobDestroyFSM(Entity *be, FSMLDAmbient *ambientData)
{
	if(ambientData->jobFsmContext)
	{
		fsmExitCurState(ambientData->jobFsmContext);

		fsmContextDestroy(ambientData->jobFsmContext);
		ambientData->jobFsmContext = NULL;
	}
	if(ambientData->jobExprContext)
	{
		exprContextDestroy(ambientData->jobExprContext);
		ambientData->jobExprContext = NULL;
	}
	if(ambientData->animationCommandQueue)
	{
		CommandQueue_Destroy(ambientData->animationCommandQueue);
		ambientData->animationCommandQueue = NULL;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiAmbient_JobStartFSM(Entity *be, FSMLDAmbient *ambientData, FSM *fsm)
{
	aiAmbient_JobDestroyFSM(be, ambientData);

	ambientData->jobFsmContext = fsmContextCreate(fsm);
	ambientData->jobExprContext = exprContextCreate();
	aiSetupExprContext(be, be->aibase, entGetPartitionIdx(be), ambientData->jobExprContext, false);
}


// ------------------------------------------------------------------------------------------------------------------
static void aiAmbient_SetAnimList(Entity *be, FSMLDAmbient *ambientData, AIAnimList *animList)
{
	if(!animList)
		return;

	aiAmbientRemoveAnimation(be, ambientData);

	if(aiAnimListSet(be, animList, &ambientData->animationCommandQueue))
	{
		ambientData->addedAnimList = 1;
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void aiAmbient_SetAnimListByName(Entity *be, FSMLDAmbient *ambientData, const char *animList)
{
	AIAnimList *pAnimList = RefSystem_ReferentFromString(g_AnimListDict, animList);

	if(!pAnimList)
		return;
	
	aiAmbient_SetAnimList(be, ambientData, pAnimList);
}

// ------------------------------------------------------------------------------------------------------------------
int aiAmbient_GetAction(GameInteractLocation *pJob, FSM **ppFSM, AIAnimList **ppAnimList)
{
	if (pJob)
	{
		// FSMs have priority 
		*ppFSM = GET_REF(pJob->pWorldInteractLocationProperties->hFsm);
		if (*ppFSM)
		{
			*ppAnimList = NULL;
			return true;
		}

		{
			S32 numAnims = eaSize(&pJob->pWorldInteractLocationProperties->eaAnims);
			if (numAnims)
			{
				const char *pchJobName;
				numAnims = randomIntRange(0, numAnims-1);
				pchJobName = eaGet(&pJob->pWorldInteractLocationProperties->eaAnims, numAnims);

				if (pchJobName)
				{
					*ppAnimList = RefSystem_ReferentFromString(g_AnimListDict, pchJobName);
					return *ppAnimList != NULL;
				}

				*ppAnimList = NULL;
			}
		}

		return false;
	}

	return false;

}

// ------------------------------------------------------------------------------------------------------------------
void aiAmbient_SetOverrideMoveSpeed(Entity *e, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	if(!mydata->speedConfigModHandle && mydata->wanderSpeed > 0.0)
	{
		char* speedstr = NULL;
		estrStackCreate(&speedstr);
		estrPrintf(&speedstr, "%f", mydata->wanderSpeed);
		mydata->speedConfigModHandle = aiConfigModAddFromString(e, e->aibase, "OverrideMovementSpeed", speedstr, errString);
		estrDestroy(&speedstr);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiAmbientSwitchToState(FSMLDAmbientState eState, Entity* e, ExprContext* context, FSMLDAmbient* ambientData, ACMD_EXPR_ERRSTRING errString)
{
	AIAmbientStateHandler *pHandlers;
	
	// Exit Existing State
	if(stashIntFindPointer(s_stAIAmbientStateHandlers, ambientData->currentState, &pHandlers))
	{
		if(pHandlers->onExitFunc)
		{
			pHandlers->onExitFunc(e, context, ambientData, errString);
		}
	}

	// Set New State
	ambientData->currentState = eState; 

	// load general state information
	switch(ambientData->currentState)
	{
		case FSM_AMBIENT_IDLE:
		default:
			ambientData->stateDuration = aiAmbientRandomDuration(ambientData->idleDuration, DURATION_VARIANCE);
			break;
		case FSM_AMBIENT_CHAT:
			ambientData->stateDuration = aiAmbientRandomDuration(ambientData->chatDuration, DURATION_VARIANCE);
			break;
		case FSM_AMBIENT_WANDER:
			ambientData->stateDuration = aiAmbientRandomDuration(ambientData->wanderDuration, DURATION_VARIANCE);
			break;
		case FSM_AMBIENT_JOB:
			ambientData->stateDuration = aiAmbientRandomDuration(ambientData->jobDuration, DURATION_VARIANCE);
			break;
	}

	// inform system we're entering the state
	ambientData->onEntry = true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiAmbientChooseState(Entity* e, ExprContext* context, FSMLDAmbient* ambientData, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAmbientState prevState = ambientData->currentState;

	// choose new state
	bool isJobActive = false;
	F32 total = 0.0;
	FSMLDAmbientState newState = FSM_AMBIENT_IDLE; // default
	int partitionIdx = entGetPartitionIdx(e);

	// add up the weights
	if(ambientData->isIdleActive) total += ambientData->idleWeight;
	if(ambientData->isChatActive) total += ambientData->chatWeight;
	if(ambientData->isWanderActive) total += ambientData->wanderWeight;

	// if we were just in an ambient job FSM, do not go back to doing a job 
	if(ambientData->isJobActive && !ambientData->jobFsmContext && ABS_TIME_PARTITION(partitionIdx) > ambientData->ambientJobCooldown)
	{
		isJobActive = true;
		total += ambientData->jobWeight;
	}
		
	if(total > 0.0)
	{
		F32 choice = randomPositiveF32();
		F32 idleTest = ambientData->isIdleActive ? ambientData->idleWeight / total : 0.0;
		F32 chatTest = ambientData->isChatActive ? ambientData->chatWeight / total : 0.0;
		F32 wanderTest = ambientData->isWanderActive ? ambientData->wanderWeight / total : 0.0;
		F32 jobTest = isJobActive ? ambientData->jobWeight / total : 0.0;
		bool done = false;

		while(!done) 
		{
			if(choice < idleTest)
			{
				newState = FSM_AMBIENT_IDLE;
				break;
			} 
			choice -= idleTest;

			if(choice < chatTest)
			{
				newState = FSM_AMBIENT_CHAT;
				break;
			} 
			choice -= chatTest;

			if(choice < wanderTest)
			{
				newState = FSM_AMBIENT_WANDER;
				break;
			} 
			choice -= wanderTest;

			if(choice < jobTest)
			{
				if (ambientData->bIgnoreFindJob)
				{	// we're ignoring finding job until our count goes back to 0
					ambientData->failedFindJobCount--;
					if (ambientData->failedFindJobCount < 0)
					{
						ambientData->failedFindJobCount = 0;
						ambientData->bIgnoreFindJob = false;
					}
				}
				else 
				{
					newState = FSM_AMBIENT_JOB;
					break;
				}
			} 
			choice -= jobTest;

			done = true;
		}
	}

	// special case override
	// to leave Job, must go to wander or move back to spawn point to free the position
	if(ambientData->currentState == FSM_AMBIENT_JOB && newState != FSM_AMBIENT_JOB)
	{
		if(ambientData->isWanderActive)
		{
			newState = FSM_AMBIENT_WANDER;
		}
		else
		{
			newState = FSM_AMBIENT_GOTOSPAWN;
		}
	}

	//
	// SWITCH
	//
	if(newState != ambientData->currentState)
	{
		aiAmbientSwitchToState(newState, e, context, ambientData, errString);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiAmbient_ChooseInitialState(	int partitionIdx, 
											Entity* e, 
											ExprContext* context, 
											FSMLDAmbient* mydata, 
											ACMD_EXPR_ERRSTRING errString)
{

	if (e->aibase->pTargetCombatJobInteractable && e->aibase->pTargetCombatJobInteractLocation)
	{
		GameInteractLocationPartition *pLocPart = NULL;

		if (interactable_AmbientJobExists(e->aibase->pTargetCombatJobInteractable, e->aibase->pTargetCombatJobInteractLocation) &&
			(pLocPart = interactable_GetInteractLocationPartition(partitionIdx, e->aibase->pTargetCombatJobInteractLocation)) &&
			pLocPart->bOccupied &&
			mydata->pTargetJobInteractable == NULL)
		{
			mydata->pTargetJobInteractable = e->aibase->pTargetCombatJobInteractable;
			mydata->pTargetLocation = e->aibase->pTargetCombatJobInteractLocation;

			e->aibase->pTargetCombatJobInteractable = NULL;
			e->aibase->pTargetCombatJobInteractLocation = NULL;

			aiAmbientSwitchToState(FSM_AMBIENT_JOB, e, context, mydata, errString);
			return;
		}
	}
	else
	{
		Vec3 vSearchPos;
		GameInteractable* pInitialJob = NULL;
		GameInteractLocation *pGameInteractLocation = NULL;

		aiGetSpawnPos(e, e->aibase, vSearchPos);

		pInitialJob = aiAmbientJob_FindBestJob(partitionIdx, e, vSearchPos, mydata->fJobAwarenessRadius, true);

		if (pInitialJob && aiAmbientChooseAvailableJob(partitionIdx, pInitialJob, e, &pGameInteractLocation, NULL))
		{
			mydata->pTargetJobInteractable = pInitialJob;
			mydata->pTargetLocation = pGameInteractLocation;
			aiAmbientSwitchToState(FSM_AMBIENT_JOB, e, context, mydata, errString);
			return;
		}
	}
	
	aiAmbientChooseState(e, context, mydata, errString);
}

AUTO_COMMAND_QUEUED();
void aiAmbientExitHandler(ACMD_POINTER Entity* e, ACMD_POINTER AIVarsBase* aib, ACMD_POINTER FSMLDAmbient* ambientData)
{
	if(ambientData->speedConfigModHandle)
	{
		aiConfigModRemove(e, aib, ambientData->speedConfigModHandle);
	}

	if(ambientData->waypointSplineConfigModHandle)
	{
		aiConfigModRemove(e, aib, ambientData->waypointSplineConfigModHandle);
	}

	SAFE_FREE(ambientData->chatAnimation);
	SAFE_FREE(ambientData->idleAnimation);
	SAFE_FREE(ambientData->chatMessageKey);

	// safely release our ownership of the job position
	if(interactable_AmbientJobExists(ambientData->pTargetJobInteractable, ambientData->pTargetLocation))
	{		
		if (interactable_CombatJobExists(ambientData->pTargetJobInteractable, ambientData->pTargetLocation))
		{
			// Keep occupying the same job location if there is a combat job for it
			aiCombatJob_AssignLocation(e, ambientData->pTargetJobInteractable, ambientData->pTargetLocation, false);
		}
		else
		{
			GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(entGetPartitionIdx(e), ambientData->pTargetLocation);
			locPart->bOccupied = false;
		}		
	}

	aiAmbientRemoveAnimation(e, ambientData);

	aiAmbient_JobDestroyFSM(e, ambientData);

	ZeroStruct(ambientData);
}

float aiAmbientRandomDuration(F32 input, F32 variance)
{
	return input * (1 + randomPositiveF32() * variance);
}

// ----------------------------------------------------------------------------------------------------------------
AIAmbient* aiAmbient_GetAmbientDef(const Vec3 vPos)
{
	if (vPos)
	{
		RegionRules *regionRules = RegionRulesFromVec3(vPos);
		if(regionRules)
		{
			AIAmbient *pAIAmbient;
			pAIAmbient = RefSystem_ReferentFromString(g_hAIAmbientDict, regionRules->aiAmbientDefaults);
			if (pAIAmbient)
				return pAIAmbient;
		}
	}
	
	if(aiGlobalSettings.aiAmbientDefaults)
	{
		return RefSystem_ReferentFromString(g_hAIAmbientDict, aiGlobalSettings.aiAmbientDefaults);		
	}
	return NULL;
}

// ----------------------------------------------------------------------------------------------------------------
void aiAmbientInitDefaults(Entity *e, ExprContext* context, FSMLDAmbient *mydata, ACMD_EXPR_ERRSTRING errString)
{
	ExprFuncReturnVal retval;
	MultiVal answer = {0};
	Vec3 curPos;
	RegionRules *regionRules;
	AIAmbient *pAIAmbient = NULL;
	AIVarsBase* aib = e->aibase;
	bool bIsChatPossible;
	int partitionIdx = entGetPartitionIdx(e);

	entGetPos(e, curPos);
	regionRules = RegionRulesFromVec3(curPos);
	if(regionRules)
	{
		pAIAmbient = RefSystem_ReferentFromString(g_hAIAmbientDict, regionRules->aiAmbientDefaults);
	}
	else if(aiGlobalSettings.aiAmbientDefaults)
	{
		pAIAmbient = RefSystem_ReferentFromString(g_hAIAmbientDict, aiGlobalSettings.aiAmbientDefaults);		
	}
	else
	{
		Errorf("Could not find default for AIAmbientDefaults (check RegionRules and/or AIGlobalSettings)");
	}

	//
	// Wander settings
	//

	retval = exprContextGetExternVar(context, "Encounter", "Amb_WanderActive", MULTI_INT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->isWanderActive = pAIAmbient ? pAIAmbient->bIsWanderActive : false;
	else mydata->isWanderActive = !!answer.intval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_WanderSpeed", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->wanderSpeed = pAIAmbient ? pAIAmbient->fWanderSpeed : 0.0f;
	else mydata->wanderSpeed = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_WanderDuration", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->wanderDuration = pAIAmbient ? pAIAmbient->fWanderDuration : 10.0f;
	else mydata->wanderDuration = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_WanderWeight", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->wanderWeight = pAIAmbient ? pAIAmbient->fWanderWeight : 1.0f;
	else mydata->wanderWeight = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_WanderDistance", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->wanderDistance = pAIAmbient ? pAIAmbient->fWanderDistance : 40.0f;
	else mydata->wanderDistance = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_WanderIdleTime", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->wanderIdleTime = pAIAmbient ? pAIAmbient->fWanderIdleTime : 4.2f;
	else mydata->wanderIdleTime = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_MaxWanderPath", MULTI_INT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->wanderMaxPath = pAIAmbient ? pAIAmbient->iMaxWanderPath : 1;
	else mydata->wanderMaxPath = answer.intval;
	
	//
	// Chat settings
	//

	// make sure we have chat anim and chat text
	bIsChatPossible = false;
	mydata->isChatActive = false;

	StructFreeStringSafe(&mydata->chatAnimation);
	retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatAnim", MULTI_STRING, &answer, errString);
	if(retval != ExprFuncReturnError && (answer.str && answer.str[0]))
	{

	}
	else
	{
		if(pAIAmbient && pAIAmbient->pchChatAnimation && pAIAmbient->pchChatAnimation[0])
		{
			mydata->chatAnimation = StructAllocString(pAIAmbient->pchChatAnimation);
		}
	}
	
	StructFreeStringSafe(&mydata->chatMessageKey);
	retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatText", MULTI_STRING, &answer, errString);
	if(retval != ExprFuncReturnError && (answer.str && answer.str[0])) 
	{
		bIsChatPossible = true;
		mydata->chatMessageKey = StructAllocString(answer.str);
	}
	else
	{
		if(pAIAmbient && pAIAmbient->pchChatMessageKey && pAIAmbient->pchChatMessageKey[0])
		{
			mydata->chatMessageKey = StructAllocString(pAIAmbient->pchChatMessageKey);
			bIsChatPossible = true;
		}
	}

	retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatDuration", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->chatDuration = pAIAmbient ? pAIAmbient->fChatDuration : 10.0;
	else mydata->chatDuration = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatWeight", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->chatWeight = pAIAmbient ? pAIAmbient->fChatWeight : 1.0;
	else mydata->chatWeight = answer.floatval;

	if(bIsChatPossible)
	{
		retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatActive", MULTI_INT, &answer, errString);
		if(retval == ExprFuncReturnError) mydata->isChatActive = false;
		else mydata->isChatActive = !!answer.intval;
	}

	//
	// Idle settings
	//

	// check if we have an anim
	StructFreeStringSafe(&mydata->idleAnimation);
	retval = exprContextGetExternVar(context, "Encounter", "Amb_IdleAnim", MULTI_STRING, &answer, errString);
	if(retval != ExprFuncReturnError && (answer.str && answer.str[0]))
	{
	
	}
	else
	{
		if(pAIAmbient && pAIAmbient->pchIdleAnimation && pAIAmbient->pchIdleAnimation[0])
		{
			mydata->idleAnimation = StructAllocString(pAIAmbient->pchIdleAnimation);
		}
	}

	retval = exprContextGetExternVar(context, "Encounter", "Amb_IdleWeight", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->idleWeight = pAIAmbient ? pAIAmbient->fIdleWeight : 1.0;
	else mydata->idleWeight = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_IdleDuration", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->idleDuration = pAIAmbient ? pAIAmbient->fIdleDuration : 10.0;
	else mydata->idleDuration = answer.floatval;

	mydata->isIdleActive = pAIAmbient ? pAIAmbient->bIsIdleActive : true;
	

	//
	// Job settings
	//

	retval = exprContextGetExternVar(context, "Encounter", "Amb_JobActive", MULTI_INT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->isJobActive = pAIAmbient ? pAIAmbient->bIsJobActive : false;
	else mydata->isJobActive = !!answer.intval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_JobDuration", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->jobDuration = pAIAmbient ? pAIAmbient->fJobDuration : 10.0;
	else mydata->jobDuration = answer.floatval;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_JobWeight", MULTI_FLOAT, &answer, errString);
	if(retval == ExprFuncReturnError) mydata->jobWeight = pAIAmbient ? pAIAmbient->fJobWeight : 1.0;
	else mydata->jobWeight = answer.floatval;

	mydata->fJobAwarenessRadius = pAIAmbient ? pAIAmbient->fJobAwarenessRadius : 50.0;

	{
		F32 fRandTime = aiAmbientRandomDuration(10, DURATION_VARIANCE_HIGH);
		mydata->ambientJobCooldown = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(fRandTime);
	}


	// Flying check --
	{
		Vec3 myPos;

		mydata->isFlying = false;
		
		// Check if we're more than 10 ft above the ground and able to fly... if so, fly!
		
		entGetPos(e, myPos);
		vecY(myPos) += 0.1;  // False positive fixing

		if(aib->powers && aib->powers->hasFlightPowers && !aiMovementGetFlying(e, aib))
		{
			F32 groundDist = aiFindGroundDistance(worldGetActiveColl(entGetPartitionIdx(e)), myPos, NULL);
			if (groundDist < 0.f || groundDist > 10.f)
			{
				mydata->isFlying = true;
			}
		}
		else if(aib->powers && aib->powers->alwaysFlight)
		{
			mydata->isFlying = true;
		}


		mydata->distBeforeWaypointToSpline = pAIAmbient ? pAIAmbient->fDistBeforeWaypointToSpline : 3.0;
		mydata->airWander = pAIAmbient ? pAIAmbient->fWanderAirRange : 20.0;
		mydata->groundRelative = false;
	}
}


void aiAmbientIdle_OnEntry(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	ExprFuncReturnVal retval;
	MultiVal answer = {0};

	const char *animation = NULL;

	retval = exprContextGetExternVar(context, "Encounter", "Amb_IdleAnim", MULTI_STRING, &answer, errString);
	if(retval == ExprFuncReturnFinished && (answer.str && answer.str[0]))
	{
		animation = answer.str;

		// only copy for debugging
		if(isDevelopmentMode())
		{
			StructFreeStringSafe(&mydata->idleAnimation);
			mydata->idleAnimation = strdup(animation);
		}
	}
	else
	{
		animation = mydata->idleAnimation;
	}

	if(animation)
	{
		aiAmbient_SetAnimListByName(e, mydata, animation);	
	}

	aiMovementResetPath(e, e->aibase);
}

void aiAmbientChat_OnEntry(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	ExprFuncReturnVal retval;
	MultiVal answer = {0};

	const char *animation = NULL;
	const char *messageKey = NULL;

	// Chat animation
	retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatAnim", MULTI_STRING, &answer, errString);
	if(retval == ExprFuncReturnFinished && (answer.str && answer.str[0]))
	{
		animation = answer.str;

		// only copy for debugging
		if(isDevelopmentMode())
		{
			StructFreeStringSafe(&mydata->chatAnimation);
			mydata->chatAnimation = strdup(animation);
		}
	}
	else
	{
		animation = mydata->chatAnimation;
	}

	if(animation)
	{
		aiAmbient_SetAnimListByName(e, mydata, animation);
	}

	// Chat text message key
	retval = exprContextGetExternVar(context, "Encounter", "Amb_ChatText", MULTI_STRING, &answer, errString);
	if(retval == ExprFuncReturnFinished && (answer.str && answer.str[0]))
	{
		messageKey = answer.str;

		// only copy for debugging
		if(isDevelopmentMode())
		{
			StructFreeStringSafe(&mydata->chatMessageKey);
			mydata->chatMessageKey = strdup(messageKey);
		}
	}
	else
	{
		messageKey = mydata->chatMessageKey;
	}

	if(messageKey)
	{
		aiSayMessageInternal(e, NULL, context, messageKey, NULL, mydata->chatDuration);
	}

	aiMovementResetPath(e, e->aibase);
}

void aiAmbientWander_OnEntry(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	AIVarsBase* aib = e->aibase;
	FSMLDWander* wanderData;

	// Add config mod (check if we previously added one)
	if(mydata->waypointSplineConfigModHandle)
	{
		aiConfigModRemove(e, aib, mydata->waypointSplineConfigModHandle);
	}

	aiAmbient_SetOverrideMoveSpeed(e, mydata, errString);

	if(mydata->isFlying)
	{
		char* pchDistBeforeWaypointToSpline = NULL;
		estrStackCreate(&pchDistBeforeWaypointToSpline);
		estrPrintf(&pchDistBeforeWaypointToSpline, "%f", mydata->distBeforeWaypointToSpline);
		mydata->waypointSplineConfigModHandle = aiConfigModAddFromString(e, e->aibase, "distBeforeWaypointToSpline", pchDistBeforeWaypointToSpline, errString);
		estrDestroy(&pchDistBeforeWaypointToSpline);
	}

	wanderData = getMyData(context, parse_FSMLDWander, 0);
	if(wanderData)
	{
		wanderData->wanderDistSQR = SQR(mydata->wanderDistance);
		wanderData->wanderIdleTimeAvg = mydata->wanderIdleTime;
		wanderData->wanderMaxPath = mydata->wanderMaxPath;
	}

	aiAmbientRemoveAnimation(e, mydata);
}

void aiAmbientWander_OnTick(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{

	if(mydata->isFlying)
	{
		wanderAirInternal(e, context, NULL, -1 * mydata->airWander, mydata->airWander, mydata->groundRelative, errString);
	}
	else
	{
		
		wanderGroundInternal(e, context, NULL, errString);
	}
}

// -------------------------------------------------------------------------------------------------------
bool aiAmbient_ShouldEntityIgnoreJob(Entity* e, GameInteractable *pJobInteractable, GameInteractLocation *pJob)
{
	if(pJob->pWorldInteractLocationProperties->pIgnoreCond)
	{
		MultiVal answer = {0};
		AIVarsBase* aib = e->aibase;
		aiUpdateProxEnts(e, aib);
		exprContextCleanupPush(aib->exprContext, NULL, NULL);
		exprContextSetPointerVarPooledCached(aib->exprContext, interactLocationString, pJob, parse_GameInteractLocation, true, true, &interactLocationVarHandle);		

		// ambientJob expressions change the scope to that of the pTargetCombatJobInteractable if the scope exists
		// then reset it after the FSM executes
		{
			WorldScope* pSavedScope = exprContextGetScope(aib->exprContext);
			WorldScope* pNewScope = SAFE_MEMBER2(pJobInteractable, pWorldInteractable, common_data.closest_scope);

			if (pNewScope)
				exprContextSetScope(aib->exprContext, pNewScope);

			exprEvaluate(pJob->pWorldInteractLocationProperties->pIgnoreCond, aib->exprContext, &answer);

			if (pNewScope)
				exprContextSetScope(aib->exprContext, pSavedScope);
		}

		exprContextRemoveVarPooled(aib->exprContext, interactLocationString);
		exprContextCleanupPop(aib->exprContext);

		if(answer.intval > 0)
			return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------
bool aiAmbient_CanUseJob(int partitionIdx, Entity *e, GameInteractable *pJobInteractable, GameInteractLocation *pJob)
{
	GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(partitionIdx, pJob);

	if (!locPart->bOccupied && 
		locPart->cooldownTime < ABS_TIME_PARTITION(partitionIdx) && 
		pJob->pWorldInteractLocationProperties &&
		(REF_HANDLE_IS_ACTIVE(pJob->pWorldInteractLocationProperties->hFsm) || eaSize(&pJob->pWorldInteractLocationProperties->eaAnims) > 0) &&
		(!REF_HANDLE_IS_ACTIVE(pJob->pWorldInteractLocationProperties->hSecondaryFsm) || (e && e->pChar))) // Ambient-Combat job locations are only for combat entities
	{
		if(e && pJob->pWorldInteractLocationProperties->pIgnoreCond)
		{	
			if (aiAmbient_ShouldEntityIgnoreJob(e, pJobInteractable, pJob))
				return false;
		}
		return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------
__forceinline static bool aiAmbientDoesHaveOpenSlots(Entity *e, GameInteractable *pJobInteractable, GameInteractLocation **eaGameInteractLocation)
{
	FOR_EACH_IN_EARRAY(eaGameInteractLocation, GameInteractLocation, pJob)
		if (aiAmbient_CanUseJob(entGetPartitionIdx(e), e, pJobInteractable, pJob))
			return true;
	FOR_EACH_END

	return false;
}

// -------------------------------------------------------------------------------------------------------
bool aiAmbientChooseAvailableJob(int partitionIdx,
								 GameInteractable *pBestInteract,
								 Entity* e, 
								 GameInteractLocation **ppWorldAmbientJob,
								 S32 *piSlot)
{
	GameInteractLocation *pBestAmbientJob = NULL;
	S32 iBestPriority = -INT_MAX;
	S32 iBestIdx = 0;

	
	FOR_EACH_IN_EARRAY(pBestInteract->eaInteractLocations, GameInteractLocation, pAmbientJob)
	{
		if (!aiAmbient_CanUseJob(partitionIdx, e, pBestInteract, pAmbientJob))
			continue;
	
		if (pAmbientJob->pWorldInteractLocationProperties->iPriority > iBestPriority)
		{
			iBestPriority = pAmbientJob->pWorldInteractLocationProperties->iPriority;
			pBestAmbientJob = pAmbientJob;
			iBestIdx = ipAmbientJobIndex;
		}
		else if (pAmbientJob->pWorldInteractLocationProperties->iPriority == iBestPriority && randomBool())
		{
			pBestAmbientJob = pAmbientJob;
			iBestIdx = ipAmbientJobIndex;
		}
	}
	FOR_EACH_END

	if(pBestAmbientJob)
	{
		if (piSlot) *piSlot = iBestIdx;
		if (ppWorldAmbientJob) *ppWorldAmbientJob = pBestAmbientJob;
	}

	return pBestAmbientJob != NULL;
}

// --------------------------------------------------------------------------------------------------------------
static GameInteractable* aiAmbientJob_FindBestJob(	int partitionIdx, 
													Entity* e, 
													const Vec3 vSearchPos, 
													F32 fRadius,
													bool bInitialJobsOnly)
{
	static GameInteractable **ppGameInteractables = NULL;
	
	GameInteractable *pBestInteract = NULL;
	S32 iBestPriority = -INT_MAX;
	
	eaClear(&ppGameInteractables);

	interactable_FindAmbientJobInteractables((F32*)vSearchPos, fRadius, &ppGameInteractables);

	FOR_EACH_IN_EARRAY(ppGameInteractables, GameInteractable, pInteractable)
	{
		S32 iPriority = 0;
		
		if (interactable_IsHiddenOrDisabled(partitionIdx, pInteractable))
			continue; 
		if (!bInitialJobsOnly && interactable_GetAmbientCooldownTime(partitionIdx, pInteractable) > ABS_TIME_PARTITION(partitionIdx))
			continue; // still on cooldown

		if (pInteractable->pAmbientJobProperties)
		{
			if (!pInteractable->pAmbientJobProperties->isForCitters)
				continue;
			if (bInitialJobsOnly && !pInteractable->pAmbientJobProperties->initialJob)
				continue;

			iPriority = pInteractable->pAmbientJobProperties->iPriority;
		}
		else if (bInitialJobsOnly)
		{
			continue;
		}

		if (!aiAmbientDoesHaveOpenSlots(e, pInteractable, pInteractable->eaInteractLocations))
			continue;

		if (iPriority > iBestPriority)
		{
			iBestPriority = iPriority;
			pBestInteract = pInteractable;
		}
		else if (iPriority == iBestPriority && randomBool())
		{
			pBestInteract = pInteractable;
		}
	}
	FOR_EACH_END
	
	return pBestInteract;
}

// --------------------------------------------------------------------------------------------------------------
static void aiAmbientJob_StartJob(	int partitionIdx, 
									Entity* e,
									FSMLDAmbient* mydata,
									GameInteractable *pInteractable,
									GameInteractLocation *pGameInteractLocation,
									ACMD_EXPR_ERRSTRING errString)
{
	WorldInteractLocationProperties *pWorldInteractLocationProperties;
	GameInteractLocationPartition *pLocPartition = NULL;

	pLocPartition = interactable_GetInteractLocationPartition(partitionIdx, pGameInteractLocation);

	pWorldInteractLocationProperties = pGameInteractLocation->pWorldInteractLocationProperties; // read-only

	copyVec3(pWorldInteractLocationProperties->vPos, mydata->vTargetPos);

	// issue move command and flag it
	aiMovementSetTargetPosition(e, e->aibase, mydata->vTargetPos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
	aiMovementSetFinalFaceRot(e, e->aibase, pWorldInteractLocationProperties->qOrientation);

	// store the job
	mydata->pTargetJobInteractable = pInteractable;
	mydata->pTargetLocation = pGameInteractLocation;

	mydata->bIsMovingToJobTarget = true;

	pLocPartition->bOccupied = true; // mark occupied
	pLocPartition->bReachedLocation = false; // but we're not there yet

	aiAmbient_SetOverrideMoveSpeed(e, mydata, errString);
}

// --------------------------------------------------------------------------------------------------------------
void aiAmbientJob_OnEntry(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	int partitionIdx = entGetPartitionIdx(e);
	GameInteractLocationPartition *locPart = NULL;
	
	// make sure to turn off any animation
	aiAmbientRemoveAnimation(e, mydata);

	// reset to default status
	mydata->bReachedJobTarget = false;
	e->aibase->ambientJobFSMDone = false;
	mydata->bIsMovingToJobTarget = false;
		
	if (!(mydata->pTargetJobInteractable && mydata->pTargetLocation))
	{	// if we weren't given explicit jobs get nearby Ambient Job interactables (and shuffle)
		GameInteractable *pBestInteract = NULL;
		GameInteractLocation *pGameInteractLocation = NULL;
		Vec3 vSearchPos;

		aiGetSpawnPos(e, e->aibase, vSearchPos);

		pBestInteract = aiAmbientJob_FindBestJob(partitionIdx, e, vSearchPos, mydata->fJobAwarenessRadius, false);

		if(pBestInteract && aiAmbientChooseAvailableJob(partitionIdx, pBestInteract, e, &pGameInteractLocation, NULL))
		{
			aiAmbientJob_StartJob(	partitionIdx,
									e, 
									mydata, 
									pBestInteract, 
									pGameInteractLocation, 
									errString);
		}
		else
		{
			mydata->failedFindJobCount++;
			if (mydata->failedFindJobCount >= 3)
			{
				mydata->bIgnoreFindJob = true;
			}
			mydata->stateDuration = 0.f;
		}

	}
	else 
	{	// we were given an explicit job to perform
		if (interactable_AmbientJobExists(mydata->pTargetJobInteractable, mydata->pTargetLocation))
		{
			// we were told to use the given job
			aiAmbientJob_StartJob(	partitionIdx,
									e, 
									mydata, 
									mydata->pTargetJobInteractable, 
									mydata->pTargetLocation, 
									errString);
		}
		else
		{
			mydata->pTargetJobInteractable = NULL;
			mydata->pTargetLocation = NULL;
		}
	}
}

// --------------------------------------------------------------------------------------------------------------
void aiAmbientJob_OnTick(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	if(mydata->bIsMovingToJobTarget)
	{
		F32 fDistanceThreshold = SQR(5.0);

		if(!e->aibase->currentlyMoving)
		{
			Vec3 vEntPos;
			entGetPos(e, vEntPos);

			if(distance3Squared(vEntPos,  mydata->vTargetPos) < fDistanceThreshold)
			{
				FSM *pFSM = NULL;
				AIAnimList *pAnimList = NULL;
				GameInteractLocationPartition *locPart = NULL;
				
				if(! interactable_AmbientJobExists(mydata->pTargetJobInteractable, mydata->pTargetLocation))
				{
					mydata->stateDuration = 0.f;
					return;
				}

				if (aiAmbient_GetAction(mydata->pTargetLocation, &pFSM, &pAnimList))
				{
					if (pFSM)
					{
						aiAmbient_JobStartFSM(e, mydata, pFSM);
						mydata->bStateDoesNotCheckDuration = true;
					}
					else if (pAnimList)
					{
						aiAmbient_SetAnimList(e, mydata, pAnimList);
					}
				}

				locPart = interactable_GetInteractLocationPartition(entGetPartitionIdx(e), mydata->pTargetLocation);
				locPart->bReachedLocation = true; // we're there now

				mydata->bIsMovingToJobTarget = false;
				mydata->bReachedJobTarget = true; 
			}
		}
	}
	else 
	{
		bool bInteractableExists = interactable_AmbientJobExists(mydata->pTargetJobInteractable, mydata->pTargetLocation);

		if (mydata->jobFsmContext && bInteractableExists)
		{
			// const char* stateBeforeExecute = fsmGetState(aib->fsmContext);
			PERFINFO_AUTO_START("Ambient-job FSM", 1);
			exprContextSetPointerVarPooledCached(e->aibase->exprContext, interactLocationString, mydata->pTargetLocation, parse_GameInteractLocation, true, true, &interactLocationVarHandle);
			
			// ambientJob FSMs change the scope to that of the pTargetCombatJobInteractable if the scope exists
			// then reset it after the FSM executes
			{
				WorldScope* pSavedScope = exprContextGetScope(mydata->jobExprContext);
				WorldScope* pNewScope = SAFE_MEMBER2(mydata->pTargetJobInteractable, pWorldInteractable, common_data.closest_scope);

				if (pNewScope)
					exprContextSetScope(mydata->jobExprContext, pNewScope);

				fsmExecuteEx(mydata->jobFsmContext, mydata->jobExprContext, true, true);
				
				if (pNewScope)
					exprContextSetScope(mydata->jobExprContext, pSavedScope);
			}

			exprContextRemoveVarPooled(e->aibase->exprContext, interactLocationString);
			PERFINFO_AUTO_STOP();
		}

		if (e->aibase->ambientJobFSMDone || !bInteractableExists)
		{	// we were told to stop the ambient job FSM
			e->aibase->ambientJobFSMDone = false;
			// force a state change
			mydata->bStateDoesNotCheckDuration = false;
			mydata->stateDuration = 0.f;
		}

	}
}

// --------------------------------------------------------------------------------------------------------------
void aiAmbientJob_CheckForOccupiedApplyCooldown(int partitionIdx, GameInteractable *pJobInteractable, F32 cooldownAvg)
{
	bool bAllOccupied = true;
	F32 randomTime;

	FOR_EACH_IN_EARRAY(pJobInteractable->eaInteractLocations, GameInteractLocation, loc)
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(partitionIdx, loc);
		if (!locPart->bOccupied)
		{
			bAllOccupied = false;
			break;
		}
	}
	FOR_EACH_END

	if (bAllOccupied)
	{
		randomTime = aiAmbientRandomDuration(cooldownAvg, DURATION_VARIANCE_HIGH);
		interactable_SetAmbientCooldownTime(partitionIdx, pJobInteractable, ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(randomTime));
	}
}

// --------------------------------------------------------------------------------------------------------------
void aiAmbientJob_OnExit(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	// safely release the occupied job slot
	int partitionIdx = entGetPartitionIdx(e);

	if(interactable_AmbientJobExists(mydata->pTargetJobInteractable, mydata->pTargetLocation))
	{
		int iPartitionIdx = entGetPartitionIdx(e);
		AIAmbient *pAmbientDef = aiAmbient_GetAmbientDef(NULL);
		GameInteractLocation *pJobLoc = mydata->pTargetLocation;
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(iPartitionIdx, pJobLoc);

		if (pAmbientDef)
		{
			GameInteractable *pJobInteractable = mydata->pTargetJobInteractable;
			F32 randomTime;
			
			randomTime = aiAmbientRandomDuration(pAmbientDef->fJobLocationCooldown, DURATION_VARIANCE_HIGH);
			locPart->cooldownTime = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(randomTime);

			randomTime = aiAmbientRandomDuration(pAmbientDef->fJobCritterCooldown, DURATION_VARIANCE_HIGH);
			mydata->ambientJobCooldown = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(randomTime);

			aiAmbientJob_CheckForOccupiedApplyCooldown(iPartitionIdx, pJobInteractable, pAmbientDef->fJobCooldown);
		}
		
		locPart->bOccupied = false;

		aiCombatJob_OnAmbientJobExit(e, e->aibase);		
	}

	// make sure to turn off any animation
	aiAmbientRemoveAnimation(e, mydata);
	
	aiAmbient_JobDestroyFSM(e, mydata);

	mydata->pTargetJobInteractable = NULL;
	mydata->pTargetLocation = NULL;

	mydata->bStateDoesNotCheckDuration = false;

	mydata->bIsMovingToJobTarget = false;
}

// --------------------------------------------------------------------------------------------------------------
static void aiAmbientGotoSpawn_OnEntry(Entity* e, ExprContext* context, FSMLDAmbient* mydata, ACMD_EXPR_ERRSTRING errString)
{
	// make sure to turn off any animation
	aiAmbientRemoveAnimation(e, mydata);

	aiMovementGoToSpawnPos(e, e->aibase, AI_MOVEMENT_TARGET_CRITICAL);
}

// --------------------------------------------------------------------------------------------------------------
static void aiAmbientSetHandlersForState(int iStateKey, AIAmbientOnEntryFunc onEntryFunc, AIAmbientOnTickFunc onTickFunc, AIAmbientOnExitFunc onExitFunc)
{
	AIAmbientStateHandler *pAIAmbientStateHandler = (AIAmbientStateHandler*)calloc(1, sizeof(AIAmbientStateHandler));

	pAIAmbientStateHandler->onEntryFunc = onEntryFunc;
	pAIAmbientStateHandler->onTickFunc = onTickFunc;
	pAIAmbientStateHandler->onExitFunc = onExitFunc;

	stashIntAddPointer(s_stAIAmbientStateHandlers, iStateKey, (void*)pAIAmbientStateHandler, true);
}

// --------------------------------------------------------------------------------------------------------------
static void aiAmbientInternal(Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAmbient* mydata = getMyData(context, parse_FSMLDAmbient, (U64)"Ambient");
	AIVarsBase* aib = e->aibase;
	AIConfig* config = aiGetConfig(e, aib);
	int partitionIdx = entGetPartitionIdx(e);

	if(!mydata->setData)
	{
		CommandQueue* exitHandlers = NULL;

		mydata->setData = 1;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Unable to call ambient in this section - missing exit handlers");
			return;
		}


		QueuedCommand_aiMovementResetPath(exitHandlers, e, aib);
		QueuedCommand_aiAmbientExitHandler(exitHandlers, e, aib, mydata);

		// initial delay settings
		mydata->lastTimeOffset = ABS_TIME_PARTITION(partitionIdx);
		
		// General

		// init defaults
		aiAmbientInitDefaults(e, context, mydata, errString);

		// State settings
		mydata->isActive =	mydata->isChatActive || 
							mydata->isIdleActive || 
							mydata->isJobActive || 
							mydata->isWanderActive;

		// choose an initial state
		if(mydata->isActive)
		{
			aiAmbient_ChooseInitialState(partitionIdx, e, context, mydata, errString);
		}

	}

	//
	// Main State Handler
	//

	if(mydata->isActive)
	{
		AIAmbientStateHandler *pHandlers;
		
		if(stashIntFindPointer(s_stAIAmbientStateHandlers, mydata->currentState, &pHandlers))
		{
			// entry
			if(mydata->onEntry)
			{
				if(pHandlers->onEntryFunc)
				{
					pHandlers->onEntryFunc(e, context, mydata, errString);
				}
				
				mydata->onEntry = false;
				mydata->lastTimeOffset = ABS_TIME_PARTITION(partitionIdx);
			}

			if(pHandlers->onTickFunc)
			{
				pHandlers->onTickFunc(e, context, mydata, errString);
			}
		}

		// check for transition
		if(!mydata->bStateDoesNotCheckDuration &&
			ABS_TIME_SINCE_PARTITION(partitionIdx, mydata->lastTimeOffset) >= SEC_TO_ABS_TIME(mydata->stateDuration))
		{
			aiAmbientChooseState(e, context, mydata, errString);
		}
	}
}

// Function to expose Extern Vars for FSM
AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCAmbient(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_WanderActive", NULL, NULL, MULTI_INT, "bool", false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_WanderSpeed", "Amb_WanderActive", "MovementSpeed", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_WanderDuration", "Amb_WanderActive", "Duration", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_WanderWeight", "Amb_WanderActive", "AmbientWeight", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_WanderDistance", "Amb_WanderActive", "WanderDistance", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_WanderIdleTime", "Amb_WanderActive", "Duration", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_MaxWanderPath", "Amb_WanderActive", "WanderPath", MULTI_INT, NULL, false, errString))
		return ExprFuncReturnError;
	

	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_ChatActive", NULL, NULL, MULTI_INT, "bool", false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_ChatDuration", "Amb_ChatActive", "Duration", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_ChatWeight", "Amb_ChatActive", "AmbientWeight", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_ChatAnim", "Amb_ChatActive", NULL, MULTI_STRING, "AIAnimList", false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_ChatText", "Amb_ChatActive", "ChatString", MULTI_STRING, "message", true, errString))
		return ExprFuncReturnError;

	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_IdleDuration", NULL, "Duration", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_IdleWeight", NULL, "AmbientWeight", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_IdleAnim", NULL, NULL, MULTI_STRING, "AIAnimList", false, errString))
		return ExprFuncReturnError;


	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_JobActive", NULL, NULL, MULTI_INT, "bool", false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_JobDuration", "Amb_JobActive", "Duration", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;
	if(ExprFuncReturnError == exprContextExternVarSC(context, "Encounter", "Amb_JobWeight", "Amb_JobActive", "AmbientWeight", MULTI_FLOAT, NULL, false, errString))
		return ExprFuncReturnError;

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(Ambient) ACMD_EXPR_STATIC_CHECK(exprFuncSCAmbient);
ExprFuncReturnVal exprFuncAmbient(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	aiAmbientInternal(e, context, errStr);

	return ExprFuncReturnFinished;
}

void exprFuncCalloutFinished(Entity *e);

// Tells the critter to finish using the ambient job. 
// This expression works on both regular critters and civilians.
AUTO_EXPR_FUNC(ai) ACMD_NAME(AmbientExitJob);
ExprFuncReturnVal exprFuncAmbientExitJob(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	AIVarsBase* aib = e->aibase;

	if (e->pCritter && (e->pCritter->civInfo || aib->calloutInfo))
	{
		exprFuncCalloutFinished(e);
	}
	else
	{
		aib->ambientJobFSMDone = true;
	}
	
	return ExprFuncReturnFinished;
}

// Gets the number of users that are current at and using the ambient job
AUTO_EXPR_FUNC(ai) ACMD_NAME(AmbientJobGetNumUsers);
int exprFuncAmbientJobGetNumUsers(ACMD_EXPR_SELF Entity *e)
{
	GameInteractable *pJob = NULL;
	S32 count = 0;

	if (e->pCritter && e->pCritter->civInfo)
	{
		pJob = aiCivPedestrian_GetCurrentAmbientJob(e);
		if (!pJob)
			return 0;
	}
	else
	{
		FSMLDAmbient* ambientData = getMyData(e->aibase->exprContext, parse_FSMLDAmbient, (U64)"Ambient");
		if (!ambientData)
			return 0;
		if(!interactable_AmbientJobExists(ambientData->pTargetJobInteractable, ambientData->pTargetLocation))
			return 0;
		
		pJob = ambientData->pTargetJobInteractable;
	}

	// go through and count the number of occupied jobs that are marked as reached
	FOR_EACH_IN_EARRAY(pJob->eaInteractLocations, GameInteractLocation, pJobLoc)
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(entGetPartitionIdx(e), pJobLoc);
		if (locPart->bOccupied && locPart->bReachedLocation)
			count++;
	}
	FOR_EACH_END

	return count;
}

// Returns the first animation defined in the job anim list for the ambient job location critter is occupying
AUTO_EXPR_FUNC(ai) ACMD_NAME(AmbientJobGetAnimListFromNode);
const char * exprFuncAmbientJobGetAnimListFromNode(ACMD_EXPR_SELF Entity* e)
{
	if (e)
	{
		FSMLDAmbient* mydata = getMyData(e->aibase->exprContext, parse_FSMLDAmbient, (U64)"Ambient");
		if (mydata && mydata->pTargetLocation && 
			mydata->pTargetLocation->pWorldInteractLocationProperties &&
			eaSize(&mydata->pTargetLocation->pWorldInteractLocationProperties->eaAnims) > 0)
		{
			return mydata->pTargetLocation->pWorldInteractLocationProperties->eaAnims[0];
		}
	}

	return "";
}

// Crops the entities who are not in the given range to the job
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropDistanceToJobBetween);
void exprFuncEntCropDistanceToJobBetween(ACMD_EXPR_SELF Entity* be, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, F32 minDist, F32 maxDist)
{
	GameInteractLocation *pInteractLocation = exprContextGetVarPointerPooled(be->aibase->exprContext, interactLocationString, parse_GameInteractLocation);
	
	if (pInteractLocation && pInteractLocation->pWorldInteractLocationProperties)
	{
		S32 i, n = eaSize(entsInOut);

		for(i = n - 1; i >= 0; i--)
		{
			F32 entDist = entGetDistance(NULL, pInteractLocation->pWorldInteractLocationProperties->vPos, (*entsInOut)[i], NULL, NULL);
			if(entDist > maxDist || entDist < minDist)
				eaRemoveFast(entsInOut, i);
		}
	}
}

// Returns the distance between the first entity and the job in the expression context. If no entity is passed to the function maximum distance is returned.
AUTO_EXPR_FUNC(ai) ACMD_NAME(DistanceToJob);
float exprFuncEntCropDistanceToJob(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN ents)
{
	int iNumEnt = eaSize(ents);

	if (iNumEnt > 0)
	{
		Entity *pEnt = (*ents)[0];
		GameInteractLocation *pInteractLocation = exprContextGetVarPointerPooled(e->aibase->exprContext, interactLocationString, parse_GameInteractLocation);

		if (pInteractLocation && pInteractLocation->pWorldInteractLocationProperties)
		{
			return entGetDistance(NULL, pInteractLocation->pWorldInteractLocationProperties->vPos, pEnt, NULL, NULL);
		}
	}

	return FLT_MAX;
}


void aiAmbientDefaultsReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading AIAmbient...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath, g_hAIAmbientDict);

	loadend_printf(" done (%d AIAmbient)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hAIAmbientDict));
}


AUTO_RUN;
void RegisterAIAmbientDict(void)
{
	g_hAIAmbientDict = RefSystem_RegisterSelfDefiningDictionary("AIAmbient", false, parse_AIAmbient, true, true, NULL);

	//resDictManageValidation(g_hAIAmbientDict, aiAmbientValidateCB);
}

AUTO_STARTUP(AIAmbient);
void aiAmbientLoad(void)
{
	loadstart_printf("Loading AIAmbient...");

	resLoadResourcesFromDisk(g_hAIAmbientDict, "ai", "AIAmbient.def", "AIAmbient.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/AIAmbient.def", aiAmbientDefaultsReload);
	}

	loadend_printf(" done (%d AIAmbient).", RefSystem_GetDictionaryNumberOfReferents(g_hAIAmbientDict));


	// setup function handlers
	s_stAIAmbientStateHandlers = stashTableCreateInt(5);

	aiAmbientSetHandlersForState(FSM_AMBIENT_IDLE, aiAmbientIdle_OnEntry, NULL, NULL);
	aiAmbientSetHandlersForState(FSM_AMBIENT_CHAT, aiAmbientChat_OnEntry, NULL, NULL);
	aiAmbientSetHandlersForState(FSM_AMBIENT_WANDER, aiAmbientWander_OnEntry, aiAmbientWander_OnTick, NULL);
	aiAmbientSetHandlersForState(FSM_AMBIENT_JOB, aiAmbientJob_OnEntry, aiAmbientJob_OnTick, aiAmbientJob_OnExit);
	aiAmbientSetHandlersForState(FSM_AMBIENT_GOTOSPAWN, aiAmbientGotoSpawn_OnEntry, NULL, NULL);
}

AUTO_COMMAND ACMD_NAME(AIAmbientSetState) ACMD_LIST(gEntConCmdList);
void exprFuncAIAmbientSetState(ACMD_EXPR_SELF Entity* be, ACMD_NAMELIST(FSMLDAmbientStateEnum, STATICDEFINE) const char* flagname)
{
	FSMLDAmbientState eState = StaticDefineIntGetInt(FSMLDAmbientStateEnum, flagname);

	if(be)
	{
		AIVarsBase* aib = be->aibase;
		if(aib && aib->exprContext)
		{
			ExprLocalData ***localData = NULL;
			CommandQueue **cmdQueue = NULL;
			FSMLDAmbient* pAmbientData;

			cmdQueue = &aib->fsmContext->curTracker->exitHandlers;
			localData = &aib->fsmContext->curTracker->localData;
			exprContextCleanupPush(aib->exprContext, cmdQueue, localData);

			pAmbientData = getMyData(aib->exprContext, parse_FSMLDAmbient, (U64)"Ambient");
			if(pAmbientData)
			{
				ACMD_EXPR_ERRSTRING_STATIC errString = NULL;
				aiAmbientSwitchToState(eState, be, aib->exprContext, pAmbientData, errString);
			}

			exprContextCleanupPop(aib->exprContext);
		}
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupFSMLDAmbientData(FSMLDAmbient* data, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR: {
			if(data->animationCommandQueue)
				CommandQueue_Destroy(data->animationCommandQueue);
			if(data->jobFsmContext)
				fsmContextDestroy(data->jobFsmContext);
			if(data->jobExprContext)
				exprContextDestroy(data->jobExprContext);

			data->animationCommandQueue = NULL;
			data->jobFsmContext = NULL;
			data->jobExprContext = NULL;
		}
	}
	return PARSERESULT_SUCCESS;
}


#include "AutoGen/aiAmbient_h_ast.c"
