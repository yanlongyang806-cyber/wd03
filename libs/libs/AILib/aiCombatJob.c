#include "aiCombatJob.h"
#include "aiAmbient.h"
#include "aiMovement.h"
#include "aiLib.h"
#include "aiStruct.h"
#include "character.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gslMapState.h"
#include "RegionRules.h"
#include "wlInteraction.h"
#include "gslInteractable.h"
#include "wlGroupPropertyStructs.h"
#include "wlEncounter.h"
#include "WorldGrid.h"
#include "rand.h"
#include "StateMachine.h"
#include "autogen/AILib_autogen_QueuedFuncs.h"

DictionaryHandle g_hAICombatJobDict;

#define DURATION_VARIANCE_HIGH	(0.7f)

extern ParseTable parse_GameInteractLocation[];
#define TYPE_parse_GameInteractLocation GameInteractLocation

AUTO_RUN;
void RegisterAICombatJobDict(void)
{
	g_hAICombatJobDict = RefSystem_RegisterSelfDefiningDictionary("AICombatJob", false, parse_AICombatJob, true, true, NULL);
}

// Reloads the AICombat defs
void aiCombatJobDefaultsReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading AICombatJob...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath, g_hAICombatJobDict);

	loadend_printf(" done (%d AICombatJob)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hAICombatJobDict));
}

AUTO_STARTUP(AICombatJob);
void aiCombatJobLoad(void)
{
	loadstart_printf("Loading AICombatJob...");

	resLoadResourcesFromDisk(g_hAICombatJobDict, "ai", "AICombatJob.def", "AICombatJob.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/AICombatJob.def", aiCombatJobDefaultsReload);
	}

	loadend_printf(" done (%d AICombatJob).", RefSystem_GetDictionaryNumberOfReferents(g_hAICombatJobDict));
}

// Returns the combat job settings based on the given location
AICombatJob* aiCombatJob_GetCombatJobDef(const Vec3 vPos)
{
	if (vPos)
	{
		RegionRules *regionRules = RegionRulesFromVec3(vPos);
		if(regionRules)
		{
			AICombatJob *pAICombatJob;
			pAICombatJob = RefSystem_ReferentFromString(g_hAICombatJobDict, regionRules->aiCombatJobDefaults);
			if (pAICombatJob)
			{
				return pAICombatJob;
			}
		}
	}

	if(aiGlobalSettings.aiCombatJobDefaults)
	{
		return RefSystem_ReferentFromString(g_hAICombatJobDict, aiGlobalSettings.aiCombatJobDefaults);		
	}
	return NULL;
}


// Indicates whether the critter can use the combat job location
bool aiCombatJob_CanUseJob(Entity *e, GameInteractable *pJobInteractable, GameInteractLocation *pJob)
{
	int partitionIdx = entGetPartitionIdx(e);
	GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(partitionIdx, pJob);
	if (!locPart->bOccupied && 
		locPart->secondaryCooldownTime < ABS_TIME_PARTITION(partitionIdx) && 
		pJob->pWorldInteractLocationProperties &&
		(REF_HANDLE_IS_ACTIVE(pJob->pWorldInteractLocationProperties->hSecondaryFsm) && e->pChar))
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

// Indicates whether one of the combat jobs passed is vacant
__forceinline static bool aiCombatJob_DoesHaveOpenSlots(Entity *e, GameInteractable *pCombatJobInteractable, GameInteractLocation **eaGameInteractLocation)
{
	FOR_EACH_IN_EARRAY(eaGameInteractLocation, GameInteractLocation, pJob)
		if (aiCombatJob_CanUseJob(e, pCombatJobInteractable, pJob))
			return true;
	FOR_EACH_END

	return false;
}

void aiCombatJob_AssignLocation(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID GameInteractable *pTargetJobInteractable, SA_PARAM_NN_VALID GameInteractLocation *pTargetLocation, bool bIssueMovementOrder)
{
	WorldInteractLocationProperties *pWorldInteractLocationProperties;
	GameInteractLocationPartition *pState = interactable_GetInteractLocationPartition(entGetPartitionIdx(e), pTargetLocation);

	pWorldInteractLocationProperties = pTargetLocation->pWorldInteractLocationProperties; // read-only

	copyVec3(pWorldInteractLocationProperties->vPos, e->aibase->vecTargetCombatJobPos);

	// issue move command and flag it
	if (bIssueMovementOrder)
	{
		aiMovementSetTargetPosition(e, e->aibase, e->aibase->vecTargetCombatJobPos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
		aiMovementSetFinalFaceRot(e, e->aibase, pWorldInteractLocationProperties->qOrientation);
		e->aibase->movementOrderGivenForCombatJob = true;
	}
	else
	{
		e->aibase->movementOrderGivenForCombatJob = false;
	}

	// I don't think we can go to a job without a location.  Adding this to catch a crash, hopefully.  [RMARR - 8/6/12]
	devassert(pTargetLocation);

	// store the job
	e->aibase->pTargetCombatJobInteractable = pTargetJobInteractable;
	e->aibase->pTargetCombatJobInteractLocation = pTargetLocation;

	e->aibase->movingToCombatJob = true;
	e->aibase->reachedCombatJob = false;

	pState->bOccupied = true; // mark occupied
	pState->bReachedLocation = false; // but we're not there yet
}

// Chooses the best combat job for the critter
static bool aiCombatJob_FindBestLocation(SA_PARAM_NN_VALID GameInteractable *pCombatJobInteractable,
										 SA_PARAM_NN_VALID Entity* e, 
										 SA_PARAM_NN_VALID GameInteractable **ppBestInteract,
										 SA_PARAM_NN_VALID GameInteractLocation **ppBestInteractLocation,
										 SA_PARAM_NN_VALID S32 *piBestPriority,
										 SA_PARAM_NN_VALID F32 *pfBestDistance,
										 SA_PARAM_NN_VALID bool *pbBestHasLOS)
{
	GameInteractLocation *pBestCombatJobLocation = NULL;
	S32 iBestPriority = INT_MIN;
	F32 fBestDistance = FLT_MAX;
	bool bBestHasLOS = false;

	F32 fDistance;
	bool bHasLOS;	
	Vec3 vecEntPos;

	entGetPos(e, vecEntPos);

	FOR_EACH_IN_EARRAY(pCombatJobInteractable->eaInteractLocations, GameInteractLocation, pCombatJobLocation)
	{
		if (!aiCombatJob_CanUseJob(e, pCombatJobInteractable, pCombatJobLocation))
			continue;

		// Check line of sight
		bHasLOS = !aiCollideRay(entGetPartitionIdx(e), e, vecEntPos, NULL, pCombatJobLocation->pWorldInteractLocationProperties->vPos, AICollideRayFlag_DOWALKCHECK);
		
		// Get the distance between the entity and the job location
		fDistance = distance3(vecEntPos, pCombatJobLocation->pWorldInteractLocationProperties->vPos);

		if ((bHasLOS && !bBestHasLOS) ||
			((pCombatJobLocation->pWorldInteractLocationProperties->iPriority > iBestPriority || fDistance < fBestDistance) && (bHasLOS || !bBestHasLOS)))

		{
			pBestCombatJobLocation = pCombatJobLocation;
			iBestPriority = pCombatJobLocation->pWorldInteractLocationProperties->iPriority;			
			fBestDistance = fDistance;
			bBestHasLOS = bHasLOS;
		}
	}
	FOR_EACH_END

	// We found the best location in the current job
	if(pBestCombatJobLocation)
	{
		// Do the overall comparisons
		if ((bBestHasLOS && !(*pbBestHasLOS)) || (fBestDistance < *pfBestDistance && (bBestHasLOS || !(*pbBestHasLOS))))
		{
			*ppBestInteract = pCombatJobInteractable;
			*ppBestInteractLocation = pBestCombatJobLocation;
			*piBestPriority = pCombatJobInteractable->pAmbientJobProperties->iPriority;
			*pfBestDistance = fBestDistance;
			*pbBestHasLOS = bBestHasLOS;

			return true;
		}
	}

	return false;
}

bool aiCombatJob_AssignToCombatJob(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID FSMLDCombatJob *pCombatJobData, bool bInitialJobsOnly)
{
	AIConfig* config = aiGetConfig(e, e->aibase);
	int partitionIdx = entGetPartitionIdx(e);
	
	if (e->pChar == NULL || e->pChar->bTauntActive || (config && config->dontSearchForCombatJobs))
	{
		return false;
	}

	if (!e->aibase->movingToCombatJob && !e->aibase->reachedCombatJob)
	{
		bool bIsAssignedToJob = false;
		Vec3 vSearchPos;
		GameInteractable **ppGameInteractables = NULL;
		GameInteractable *pBestInteract = NULL;
		GameInteractLocation *pBestGameInteractLocation = NULL;
		AICombatJob *pAICombatJob = NULL;

		// get nearby combat job interactables (and shuffle)
		entGetPos(e, vSearchPos);

		// Get the combat job def
		pAICombatJob = aiCombatJob_GetCombatJobDef(vSearchPos);

		if (pAICombatJob == NULL)
		{
			return false;
		}

		// Find all possible combat job interactables around the critter
		interactable_FindCombatJobInteractables(vSearchPos, pAICombatJob->fCombatJobAwarenessRadius, &ppGameInteractables);

		{
			S32 iBestPriority = INT_MIN;
			F32 fBestDistance = FLT_MAX;
			bool bBestHasLOS = false;

			FOR_EACH_IN_EARRAY(ppGameInteractables, GameInteractable, pInteractable)
			{
				S32 iPriority = 0;

				if (interactable_IsHiddenOrDisabled(partitionIdx, pInteractable))
					continue; 
				if (!bInitialJobsOnly && interactable_GetCombatJobCooldownTime(partitionIdx, pInteractable) > ABS_TIME_PARTITION(partitionIdx))
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

				if (!aiCombatJob_DoesHaveOpenSlots(e, pInteractable, pInteractable->eaInteractLocations))
				{
					continue;
				}

				if (iPriority >= iBestPriority)
				{
					aiCombatJob_FindBestLocation(pInteractable, e, &pBestInteract, &pBestGameInteractLocation, &iBestPriority, &fBestDistance, &bBestHasLOS);
				}
			}
			FOR_EACH_END
		}


		if(pBestInteract)
		{
			//
			// Found One
			//

			// Assign combat job
			aiCombatJob_AssignLocation(e, pBestInteract, pBestGameInteractLocation, true);

			bIsAssignedToJob = true;
		}

		eaDestroy(&ppGameInteractables);

		return bIsAssignedToJob;
	}
	else
	{
		return true;
	}
}

static void aiCombatJob_DestroyFSM(Entity *e, FSMLDCombatJob *pCombatJobData)
{
	if(pCombatJobData->jobFsmContext)
	{
		fsmExitCurState(pCombatJobData->jobFsmContext);

		fsmContextDestroy(pCombatJobData->jobFsmContext);
		pCombatJobData->jobFsmContext = NULL;
	}
	if(pCombatJobData->jobExprContext)
	{
		exprContextDestroy(pCombatJobData->jobExprContext);
		pCombatJobData->jobExprContext = NULL;
	}
}

static void aiCombatJob_StartFSM(Entity *e, FSMLDCombatJob *pCombatJobData, FSM *fsm)
{
	aiCombatJob_DestroyFSM(e, pCombatJobData);

	pCombatJobData->jobFsmContext = fsmContextCreate(fsm);
	pCombatJobData->jobExprContext = exprContextCreate();
	aiSetupExprContext(e, e->aibase, entGetPartitionIdx(e), pCombatJobData->jobExprContext, false);
}

static float aiCombatJob_RandomDuration(F32 input, F32 variance)
{
	return input * ((randomF32() * variance) + 1.0);
}

static void aiCombatJob_CheckForOccupiedApplyCooldown(int iPartitionIdx, GameInteractable *pJobInteractable, F32 cooldownAvg)
{
	bool bAllOccupied = true;
	F32 randomTime;

	FOR_EACH_IN_EARRAY(pJobInteractable->eaInteractLocations, GameInteractLocation, pLoc)
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(iPartitionIdx, pLoc);
		if (!locPart->bOccupied)
		{
			bAllOccupied = false;
			break;
		}
	}
	FOR_EACH_END

	if (bAllOccupied)
	{
		randomTime = aiCombatJob_RandomDuration(cooldownAvg, DURATION_VARIANCE_HIGH);
		interactable_SetCombatJobCooldownTime(iPartitionIdx, pJobInteractable, ABS_TIME_PARTITION(iPartitionIdx) + SEC_TO_ABS_TIME(randomTime));
	}
}

static void aiCombatJob_ResetCombatJobState(AIVarsBase *aib)
{
	aib->pTargetCombatJobInteractable = NULL;
	aib->pTargetCombatJobInteractLocation = NULL;
	zeroVec3(aib->vecTargetCombatJobPos);
	aib->insideCombatJobFSM = false;
	aib->noCombatMovementInCombatJobFSM = false;
	aib->movingToCombatJob = false;
	aib->movementOrderGivenForCombatJob = false;
	aib->reachedCombatJob = false;
}

static void aiCombatJob_GiveUpCurrentCombatJobLocation(SA_PARAM_NN_VALID Entity *e)
{
	// safely release the occupied job slot
	int partitionIdx = entGetPartitionIdx(e);
	GameInteractLocation *pJobLoc = e->aibase->pTargetCombatJobInteractLocation;
	GameInteractable *pJobInteractable = e->aibase->pTargetCombatJobInteractable;
	if(interactable_CombatJobExists(pJobInteractable, pJobLoc))
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartitionIfPresent(entGetPartitionIdx(e), pJobLoc);
		if(locPart)
		{
			AICombatJob *pCombatJobDef = aiCombatJob_GetCombatJobDef(NULL);
			if (pCombatJobDef)
			{
				F32 randomTime;
				int iPartitionIdx = entGetPartitionIdx(e);

				randomTime = aiCombatJob_RandomDuration(pCombatJobDef->fCombatJobLocationCooldown, DURATION_VARIANCE_HIGH);
				locPart->secondaryCooldownTime = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(randomTime);

				aiCombatJob_CheckForOccupiedApplyCooldown(iPartitionIdx, pJobInteractable, pCombatJobDef->fCombatJobCooldown);
			}

			locPart->bOccupied = false;
		}
	}
}

static void aiCombatJob_SetCombatJobStateAfterCombat(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib)
{
	if (interactable_CombatJobExists(aib->pTargetCombatJobInteractable, aib->pTargetCombatJobInteractLocation))
	{
		// See if there is an ambient component of this job location
		if (interactable_AmbientJobExists(aib->pTargetCombatJobInteractable, aib->pTargetCombatJobInteractLocation))
		{
			aib->insideCombatJobFSM = false;
			aib->noCombatMovementInCombatJobFSM = false;
			aib->movingToCombatJob = true;
			aib->movementOrderGivenForCombatJob = false;
			aib->reachedCombatJob = false;
		}
		else
		{
			aiCombatJob_GiveUpCurrentCombatJobLocation(e);

			aiCombatJob_ResetCombatJobState(aib);
		}
	}
	else
	{
		aiCombatJob_ResetCombatJobState(aib);
	}
}

// Handles death
void aiCombatJob_OnDeathCleanup(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib)
{
	// Give up current job
	aiCombatJob_GiveUpCurrentCombatJobLocation(e);

	// Reset combat job state
	aiCombatJob_ResetCombatJobState(aib);
}

// Handles the case where the critter exits an ambient job
void aiCombatJob_OnAmbientJobExit(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib)
{
	// Give up current job
	aiCombatJob_GiveUpCurrentCombatJobLocation(e);

	// Reset combat job state
	aiCombatJob_ResetCombatJobState(aib);
}

AUTO_COMMAND_QUEUED();
void aiCombatJob_ExitFSM(ACMD_POINTER Entity *e, ACMD_POINTER FSMLDCombatJob *pCombatJobData)
{
	aiCombatJob_DestroyFSM(e, pCombatJobData);

	// Set the proper combat job state
	aiCombatJob_SetCombatJobStateAfterCombat(e, e->aibase);

	ZeroStruct(pCombatJobData);
}

// Handles exiting team fight state
void aiCombatJob_TeamExitFightState(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib)
{
	// Set the proper combat job state
	aiCombatJob_SetCombatJobStateAfterCombat(e, aib);
}


// Runs the assigned combat job for the given critter
void aiCombatJob_RunJob(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID ExprContext* context, int doCombatMovement, SA_PARAM_NN_VALID FSMLDCombatJob *pCombatJobData, char **errString)
{
	if (e->pChar && e->pChar->bTauntActive)
	{
		aiCombatJob_DestroyFSM(e, pCombatJobData);
		ZeroStruct(pCombatJobData);
		aiCombatJob_OnAmbientJobExit(e, e->aibase);
		return;
	}

	if(e->aibase->movingToCombatJob)
	{
		if (!e->aibase->movementOrderGivenForCombatJob)
		{
			aiMovementSetTargetPosition(e, e->aibase, e->aibase->vecTargetCombatJobPos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
			aiMovementSetFinalFaceRot(e, e->aibase, e->aibase->pTargetCombatJobInteractLocation->pWorldInteractLocationProperties->qOrientation);
			e->aibase->movementOrderGivenForCombatJob = true;
		}
		else
		{
			F32 fDistanceThreshold = SQR(5.0);

			if(!e->aibase->currentlyMoving)
			{
				Vec3 vEntPos;
				entGetPos(e, vEntPos);

				if(distance3Squared(vEntPos,  e->aibase->vecTargetCombatJobPos) < fDistanceThreshold)
				{
					FSM *pFSM = NULL;
					GameInteractLocationPartition *locPart = NULL;

					if(!interactable_CombatJobExists(e->aibase->pTargetCombatJobInteractable, e->aibase->pTargetCombatJobInteractLocation))
					{
						// Combat job no longer exists for some reason
						ZeroStruct(pCombatJobData);
						return;
					}

					locPart = interactable_GetInteractLocationPartition(entGetPartitionIdx(e), e->aibase->pTargetCombatJobInteractLocation);

					pFSM = e->aibase->pTargetCombatJobInteractLocation->pWorldInteractLocationProperties ?
						GET_REF(e->aibase->pTargetCombatJobInteractLocation->pWorldInteractLocationProperties->hSecondaryFsm) : NULL;

					aiCombatJob_StartFSM(e, pCombatJobData, pFSM);

					locPart->bReachedLocation = true; // we're there now

					e->aibase->movingToCombatJob = false;
					e->aibase->reachedCombatJob = true; 
				}
			}
		}
	}
	else 
	{
		if (pCombatJobData->jobFsmContext)
		{
			GameInteractable *pTargetCombatJobInteractable = e->aibase->pTargetCombatJobInteractable;
			bool bInteractableExists = interactable_CombatJobExists(pTargetCombatJobInteractable, e->aibase->pTargetCombatJobInteractLocation);

			if(!pCombatJobData->setCombatJobFSMData)
			{
				CommandQueue* exitHandlers = NULL;

				exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

				if(!exitHandlers)
				{
					estrPrintf(errString, "Unable to call combat job in this section - missing exit handlers");
					return;
				}

				pCombatJobData->setCombatJobFSMData = 1;

				QueuedCommand_aiCombatJob_ExitFSM(exitHandlers, e, pCombatJobData);
			}

			if (bInteractableExists)
			{
				PERFINFO_AUTO_START("Combat-job FSM", 1);
				e->aibase->insideCombatJobFSM = true;
				e->aibase->noCombatMovementInCombatJobFSM = !doCombatMovement;
				exprContextSetPointerVarPooledCached(e->aibase->exprContext, interactLocationString, e->aibase->pTargetCombatJobInteractLocation, parse_GameInteractLocation, true, true, &interactLocationVarHandle);

				// combat job FSMs change the scope to that of the pTargetCombatJobInteractable if the scope exists
				// then reset it after the FSM executes
				{
					WorldScope* pSavedScope = exprContextGetScope(pCombatJobData->jobExprContext);
					WorldScope* pNewScope = SAFE_MEMBER2(pTargetCombatJobInteractable, pWorldInteractable, common_data.closest_scope);

					if (pNewScope)
						exprContextSetScope(pCombatJobData->jobExprContext, pNewScope);

					fsmExecute(pCombatJobData->jobFsmContext, pCombatJobData->jobExprContext);

					if (pNewScope)
						exprContextSetScope(pCombatJobData->jobExprContext, pSavedScope);
				}
				
				exprContextRemoveVarPooled(e->aibase->exprContext, interactLocationString);
				e->aibase->noCombatMovementInCombatJobFSM = false;
				e->aibase->insideCombatJobFSM = false;
				PERFINFO_AUTO_STOP();
			}


			if (e->aibase->combatJobFSMDone || !bInteractableExists)
			{	
				// we were told to stop the combat job FSM
				e->aibase->combatJobFSMDone = false;

				// Give up current job
				aiCombatJob_GiveUpCurrentCombatJobLocation(e);

				// Reset combat job state
				aiCombatJob_ResetCombatJobState(e->aibase);

				aiCombatJob_ExitFSM(e, pCombatJobData);
			}
		}
	}
}

// Tells the critter to finish using the combat job. 
AUTO_EXPR_FUNC(ai) ACMD_NAME(CombatJobFinish);
ExprFuncReturnVal exprFuncCombatJobFinish(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	AIVarsBase* aib = e->aibase;

	aib->combatJobFSMDone = true;

	return ExprFuncReturnFinished;
}

AUTO_FIXUPFUNC;
TextParserResult fixupFSMLDCombatJob(FSMLDCombatJob* data, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR: {
			if(data->jobFsmContext)
				fsmContextDestroy(data->jobFsmContext);
			if(data->jobExprContext)
				exprContextDestroy(data->jobExprContext);

			data->jobFsmContext = NULL;
			data->jobExprContext = NULL;
		}
	}
	return PARSERESULT_SUCCESS;
}

#include "AutoGen/aiCombatJob_h_ast.c"
