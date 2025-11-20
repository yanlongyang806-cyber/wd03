#include "aiStruct.h"
#include "aiStructCommon.h"

#include "aiAvoid.h"
#include "aiBrawlerCombat.h"
#include "aiCivilian.h"
#include "aiCombatRoles.h"
#include "aiConfig.h"
#include "aiDebug.h"
#include "aiDebugShared_h_ast.h"
#include "aiExtern.h"
#include "aiFormation.h"
#include "aiGroupCombat.h"
#include "aiLib.h"
#include "aiJobs.h"
#include "aiMovement.h"
#include "aiMovementModes.h"
#include "aiPowers.h"
#include "aiTeam.h"
#include "aiMastermind.h"

#include "commandqueue.h"
#include "Character.h"
#include "Character_target.h"
#include "entCritter.h"
#include "gslMapState.h"
#include "gslPetCommand.h"
#include "MemoryPool.h"
#include "Player.h"
#include "rand.h"
#include "StateMachine.h"
#include "StructMod.h"

int enablePlayerAI = false;
AUTO_CMD_INT(enablePlayerAI, enablePlayerAI) ACMD_HIDE;

int enablePlayerAIMovement = true;
AUTO_CMD_INT(enablePlayerAIMovement, enablePlayerAIMovement) ACMD_HIDE;

MP_DEFINE(AIVarsBase);
MP_DEFINE(AIStanceConfigMod);

void aiVarsCreate(Entity* e)
{
	MP_CREATE(AIVarsBase, 16);

	e->aibase = MP_ALLOC(AIVarsBase);
}

static void aiVerifyCleanBeforeFreeing(AIVarsBase* aib){
	char* estr = NULL;
	if(!aib){
		return;
	}

	#define VERIFY(x) (aib->x ? estrConcatf(&estr, "%s%s", estr ? "," : "", #x) : 0)
	VERIFY(nextTickCmdQueue);
	VERIFY(offtickInstances);
	VERIFY(proxEnts);
	VERIFY(attackTarget);
	VERIFY(attackTargetStatus);
	VERIFY(attackTargetStatusOld);
	VERIFY(ratings);
	VERIFY(myRelLoc);
	VERIFY(relLocLists);
	VERIFY(leashAnimQueue);
	VERIFY(notVisibleTargetAnimQueue);
	VERIFY(team);
	VERIFY(member);
	VERIFY(combatTeam);
	VERIFY(combatMember);
	VERIFY(movement);
	VERIFY(statusTable);
	VERIFY(statusHashTable);
	VERIFY(statusCleanup);
	VERIFY(attackerList);
	VERIFY(fsmContext);
	VERIFY(combatFSMContext);
	VERIFY(currentCombatFSMContext);
	VERIFY(eaOverrideFSMStack);
	VERIFY(exprContext);
	VERIFY(fsmMessages);
	VERIFY(job);
	VERIFY(onDeathCleanup);
	VERIFY(powers);
	VERIFY(powersModes);
	VERIFY(logEntries);
	VERIFY(debugCurPath);
	VERIFY(stanceConfigMods);
	VERIFY(stateConfigMods);
	VERIFY(autocastPowers);
	VERIFY(calloutInfo);
	VERIFY(messageListens);
	VERIFY(movementModeManager);
	VERIFY(combatRoleVars);
	VERIFY(pFormationData);
	VERIFY(mastermindVars);
	VERIFY(eaSharedAggroEnts);
	VERIFY(eaSeekTargets);
	VERIFY(localData);
	VERIFY(aidAnimState);
	VERIFY(pTargetCombatJobInteractable);
	VERIFY(pTargetCombatJobInteractLocation);
	VERIFY(pBrawler);
	#undef VERIFY

	if(estr){
		devassertmsgf(	0,
						"YOU CAN IGNORE THIS FAILED ASSERT!\n"
						"Non-NULL fields in AIVarsBase after aiDestroy: %s", estr);
		estrDestroy(&estr);
	}
}

void aiVarsDestroy(Entity* e)
{
	aiVerifyCleanBeforeFreeing(e->aibase);
	MP_FREE(AIVarsBase, e->aibase);
}

AIStanceConfigMod* aiStanceConfigModCreate()
{
	MP_CREATE(AIStanceConfigMod, 128);
	return (AIStanceConfigMod*)MP_ALLOC(AIStanceConfigMod);
}

void aiStanceConfigModDestroy(AIStanceConfigMod *pMod)
{
	if (pMod)
	{
		eaiDestroy(&pMod->configMods);
		eaiDestroy(&pMod->powerConfigMods);
		MP_FREE(AIStanceConfigMod, pMod);
	}
	
}

void aiSetupTestCritter(Entity* e)
{
	aiConfigModAddFromString(e, e->aibase, "ignoreLeashDespawn", "1", NULL);
}

void aiInit(Entity* e, CritterDef* critter, AIInitParams *pInitParams)
{
	static AIInitParams s_defaultInitParams = {0};
	AIVarsBase* aib;
	AIConfig* config;
	int partitionIdx = entGetPartitionIdx(e);

	PERFINFO_AUTO_START_FUNC();

	if (!pInitParams)
		pInitParams = &s_defaultInitParams;

	if(!e->aibase)
		aiVarsCreate(e);
	if(!e->aibase)
	{
		// Mostly to make analyze happy, but suppose if it ran out of RAM...
		PERFINFO_AUTO_STOP();
		return;
	}

	aib = e->aibase;

	aib->fsmMessages = stashTableCreateWithStringKeys(4, StashDefault);

	if(pInitParams->fsmOverride && pInitParams->fsmOverride[0])
		aib->fsmContext = fsmContextCreateByName(pInitParams->fsmOverride, "Combat");
	else if(critter)
		aib->fsmContext = fsmContextCreateByName(REF_STRING_FROM_HANDLE(critter->hFSM), "Combat");
	else if (!entGetPlayer(e))
		aib->fsmContext = fsmContextCreateByName("Combat", "Combat");
	else
		aib->fsmContext = fsmContextCreateByName("DoNothing", "Combat");

	devassertmsg(aib->fsmContext, "AI cannot function without an FSMContext");

	aib->fsmContext->messages = aib->fsmMessages;
	
	
	if(!aib->combatFSMContext && critter && IS_HANDLE_ACTIVE(critter->hCombatFSM))
	{
		FSM* combatFSM = GET_REF(critter->hCombatFSM);
		if(combatFSM)
		{
			aib->combatFSMContext = fsmContextCreate(combatFSM);

			aib->combatFSMContext->messages = aib->fsmMessages;
		}
	}

	PERFINFO_AUTO_START("AI ExprContext Setup", 1);
	aib->exprContext = exprContextCreate();
	exprContextSetAllowRuntimeSelfPtr(aib->exprContext);
	exprContextSetAllowRuntimePartition(aib->exprContext);
	exprContextSetFSMContext(aib->exprContext, aib->fsmContext);
	aiSetupExprContext(e, aib, entGetPartitionIdx(e), aib->exprContext, false);
	PERFINFO_AUTO_STOP();

	// TODO: remove this temporary hack (just here to make milestone 3 map not error)
	if(critter && critter->pchAIConfig && RefSystem_ReferentFromString("AIConfig", critter->pchAIConfig))
	{
		SET_HANDLE_FROM_STRING("AIConfig", critter->pchAIConfig, aib->config_use_accessor);
	}
	else if (entIsPlayer(e) && aiGlobalSettings.pchDefaultPlayerAIConfig)
	{
		SET_HANDLE_FROM_STRING("AIConfig", aiGlobalSettings.pchDefaultPlayerAIConfig, aib->config_use_accessor);
	}
	else
	{
		SET_HANDLE_FROM_STRING("AIConfig", "Default", aib->config_use_accessor);
	}

	aib->uiRandomSeed = randomU32();

	config = aiGetConfig(e, aib);

	if(stateChangeCallback)
		stateChangeCallback(e, NULL, fsmGetState(aib->fsmContext));

	aib->offtickInstances = stashTableCreateAddress(4);

	aib->statusHashTable = stashTableCreateInt(4);
	aib->baseDangerFactor = 1;
	aib->minGrievedHealthLevel = 1.0;

	aib->useDynamicPrefRange = config->useDynamicPrefRange;

	aib->lastThinkTick = ABS_TIME_PARTITION(partitionIdx);
	aib->thinkAcc = 0;
	aib->forceThinkTick = 1;
	if(pInitParams->fSpawnLockdownTime)
		aib->time.pauseUntil = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(pInitParams->fSpawnLockdownTime);

	if(enablePlayerAI || !entGetPlayer(e))
	{
		aib->doProximity = 1;
		aib->doBScript = 1;
		aib->doDeathTick = 1;
		aib->doAttackAI = 1;
		aiCivilianUpdateIsHostile(e);
	}
	else if (entGetPlayer(e))
	{
		fsmExecute(aib->fsmContext, aib->exprContext);
	}
	

	if((enablePlayerAI || enablePlayerAIMovement) && e->mm.movement)
	{
		aiMovementCreate(&aib->movement, e->mm.movement);
		aiMovementUpdateConfigSettings(e, e->aibase, aiGetConfig(e, e->aibase));
	}

	aib->powers = aiPowerEntityInfoCreate();

	aiAvoidAddEnvironmentEntries(e, aib);

	aiCombatInitRelativeLocs(e, aib);

	aiExternUpdatePerceptionRadii(e, aib);
 
	if(config->preferredAITag)
	{
		aib->powers->preferredAITagBit = StaticDefineIntGetInt(PowerAITagsEnum, config->preferredAITag);
		if(aib->powers->preferredAITagBit == -1)
		{
			ErrorFilenamef(config->filename, "Unknown power AI tag: %s", config->preferredAITag);
			aib->powers->preferredAITagBit = 0;
		}
	}

	aiMovementModeManager_CreateAndInitFromConfig(e, aib, config);
	
	if(config->controlledPet)
	{
		PetCommands_InitializeControlledPetInfo(e, critter, config);
	}

	aib->untargetable = config->untargetable;

	{
		const char *pchCombatRoleName = pInitParams->pchCombatRoleName ? 
											pInitParams->pchCombatRoleName : config->pchCombatRoleName;

		aiCombatRole_SetCombatRole(e, aib, pchCombatRoleName, pInitParams->pCombatRoleDef);
	}

	if (!aib->currentCombatFSMContext)
	{
		aiSetCurrentCombatFSMContext(aib, false);
	}

	aiMastermind_AIEntCreatedCallback(e);	

	aiAddToFirstList(e, aib);

	PERFINFO_AUTO_STOP();
}

void aiInitTeam(Entity* e, AITeam* team)
{
	if(team)
	{
		aiTeamAdd(team, e);
	}
	else
	{
		if(e->erOwner)
		{
			Entity* owner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);

			if(!owner && e->erCreator)
			{
				Entity* creator = entFromEntityRef(entGetPartitionIdx(e), e->erCreator);

				if(creator)
					owner = creator;
			}

			devassertmsg(owner && owner->aibase && owner->aibase->team,
				"Team has to be created on a fully initialized owner before pets can be spawned");
			if(owner)
			{
				team = owner->aibase->team;
				aiTeamAdd(team, e);

				if(owner->aibase->combatTeam)
					aiTeamAdd(owner->aibase->combatTeam, e);
			}
		}

		if(!team)
		{
			if(entIsPlayer(e))
			{
				// I wanted to check if there was already an appropriate AITeam to put me on,
				// to avoid the merge later, but it's too early, and I don't know the info I need [RMARR - 5/25/11]
				team = aiTeamCreate(entGetPartitionIdx(e), e, false);
			}
			else
				team = aiTeamCreate(entGetPartitionIdx(e), NULL, false);
			aiTeamAdd(team, e);
		}
	}

	if (team)
	{
		// first member added attempts to set the combat roles def
		if (! aiTeamGetCombatRolesDef(team))
		{
			AIConfig *config = aiGetConfig(e, e->aibase);
			if (config && config->pchCombatRoleDef)
			{
				aiCombatRole_TeamSetCombatRolesDef(team, config->pchCombatRoleDef);
			}
		}
		
		// This is now the primary way for pet owner formations to be created
		aiFormation_TryToAddToFormation(entGetPartitionIdx(e), team,e);
	}

	aiFlagForInitialFSM(e);
}

static void destroyAIOverrideFSM(AIOverrideFSM *pOverrideFSM)
{
	if (pOverrideFSM->fsmContext)
		fsmContextDestroy(pOverrideFSM->fsmContext);
	free (pOverrideFSM);
}

// temporarily off for debugging
#pragma optimize( "", off)
void aiDestroy(Entity* e)
{
	AIVarsBase* aib;
	EntityRef myRef;
	AITeam * pTeam=NULL, *pCombatTeam=NULL;

	PERFINFO_AUTO_START_FUNC();

	aib = e->aibase;
	myRef = entGetRef(e);

	ASSERT_FALSE_AND_SET(aib->destroying);

	aiMastermind_AIEntDestroyedCallback(e);
	aigcEntDestroyed(e);

	PERFINFO_AUTO_START("attacktargets", 1);

	aiSetAttackTarget(e, aib, NULL, NULL, true);

	while(eaSize(&aib->attackerList))
	{
		Entity* attacker = eaTail(&aib->attackerList);

		aiSetAttackTarget(attacker, attacker->aibase, NULL, NULL, true);
	}

	PERFINFO_AUTO_STOP_START("nextTickCmdQueue", 1);

	if(aib->nextTickCmdQueue)
	{
		CommandQueue_ExecuteAllCommands(aib->nextTickCmdQueue);
		CommandQueue_Destroy(aib->nextTickCmdQueue);
		aib->nextTickCmdQueue = NULL;
	}

	if(aib->job)
		aiJobUnassign(e, aib->job);

	PERFINFO_AUTO_STOP_START("onDeathCleanupQueue", 1);

	if(aib->onDeathCleanup)
	{
		CommandQueue_ExecuteAllCommands(aib->onDeathCleanup);
		CommandQueue_Destroy(aib->onDeathCleanup);
		aib->onDeathCleanup = NULL;
	}

	PERFINFO_AUTO_STOP_START("StatusTable cleanup", 1);

	aiClearStatusTable(e);
	eaAssertEmptyAndDestroy(&aib->statusTable);
	eaAssertEmptyAndDestroy(&aib->attackerListMutable);
	assert(!stashGetCount(aib->statusHashTable));
	stashTableDestroySafe(&aib->statusHashTable);

	aiOnDeathCleanup(e);
	eaiDestroy(&aib->statusCleanup);

	assert(aib->fsmContext->messages == aib->fsmMessages);
	aib->fsmMessages = NULL;
	aiMessageDestroyAll(aib->fsmContext);

	if (aib->pFormationData)
	{
		if (aib->pFormationData->pFormation->erLeader == e->myRef)
		{
			// If I have any pets, clear off their formation data.
			// This is actually for NPC pets.  Two possible other ways we can handle this:
			//		* NPC pet owners could not get private formations
			//		* We could switch these pets over to other owners and/or other formations here
			// I don't know if this really needs to be separated from the aiFormation_Destroy below, but I'm just trying to be cautious.
			int i;
			for (i=0;i < eaSize(&aib->team->members);i++)
			{
				Entity* memberBE = aib->team->members[i]->memberBE;
				// I was just checking memberBE->erOwner here before, but this is a much more directly correct check.
				// That being said, I don't know why a pet would be in a formation that was not owned by their owner.  Could point to a bug.
				if (memberBE != e && memberBE->aibase->pFormationData && (memberBE->aibase->pFormationData->pFormation == aib->pFormationData->pFormation))
				{
					aiFormationData_Free(memberBE->aibase->pFormationData);
					memberBE->aibase->pFormationData = NULL;
				}
			}
		}
	}

	// stash these for debugging
	pTeam = aib->team;
	pCombatTeam = aib->combatTeam;
	aiTeamRemove(&aib->team, e);
	if(aib->combatTeam)
		aiTeamRemove(&aib->combatTeam, e);

	if (aib->pFormationData)
	{
		AIFormation * pFormation = aib->pFormationData->pFormation;
		aiFormationData_Free(aib->pFormationData);
		aib->pFormationData = NULL;
		if (pFormation->erLeader == e->myRef)
		{
			aiFormation_Destroy(&pFormation);
		}
	}

	aiMovementClearOverrideSpeed(e, aib);
	
	{
		AIConfig* config = aiGetConfig(e, aib);

		// update owner's pet info after removing from team
		if(config->controlledPet)
		{
			Entity* owner = entGetOwner(e);
			if(owner)
				PetCommands_UpdatePlayerPetInfo(owner, false, myRef);
		}
	}

	// destroy all the config mod data
	eaDestroyEx(&aib->stanceConfigMods, aiStanceConfigModDestroy);

	aiCombatRole_DestroyCombatRoleVars(e, aib);
	aiMastermind_DestroyAIVars(e);
	
	PERFINFO_AUTO_STOP_START("misc destroy", 1);

	aiMovementDestroy(&aib->movement);

	aiDestroyPowers(e, aib);
	eaiDestroy(&aib->powersModes);
	eaDestroyEx(&aib->autocastPowers, NULL);

	eaDestroyEx(&aib->localData, fsmLocalDataDestroy);
	eaDestroyEx(&aib->eaOverrideFSMStack, destroyAIOverrideFSM);
	fsmContextDestroy(aib->fsmContext);
	aib->fsmContext = NULL;
	if(aib->combatFSMContext){
		fsmContextDestroy(aib->combatFSMContext);
		aib->combatFSMContext = NULL;
	}
	aib->currentCombatFSMContext = NULL;
	exprContextDestroy(aib->exprContext);
	aib->exprContext = NULL;

	PERFINFO_AUTO_STOP_START("misc destroy 2", 1);
	aiDebugLogClear(e);

	aiConfigModDestroyAll(e, aib);
	aiConfigLocalCleanup(aib);
	REMOVE_HANDLE(aib->config_use_accessor);

	aiConfigDestroyOfftickInstances(e, aib);
	stashTableDestroySafe(&aib->offtickInstances);

	PERFINFO_AUTO_STOP_START("misc destroy 3", 1);
	// all actual AIAvoidEntries have already been cleaned up by the movement system destructor
	aiAvoidDestroy(e, aib);
	aiAttractDestroy(e, aib);
	aiSoftAvoidDestroy(e, aib);

	SAFE_FREE(aib->proxEnts);

	aiCombatCleanupRelLocs(e, aib);

	SAFE_FREE(aib->calloutInfo);

	aiMovementModeManager_Destroy(&aib->movementModeManager);
	aiBrawlerCombat_Shutdown(aib);

	eaiDestroy(&aib->eaSharedAggroEnts);
	eaiDestroy(&aib->eaSeekTargets);

	SAFE_FREE(aib->aidAnimState);

	eaDestroyStruct(&aib->debugCurPath, parse_AIDebugWaypoint);

	eaDestroy(&aib->messageListens);

	if(aib->leashAnimQueue)
	{
		CommandQueue_Destroy(aib->leashAnimQueue);
		aib->leashAnimQueue = NULL;
	}
	if(aib->notVisibleTargetAnimQueue)
	{
		CommandQueue_Destroy(aib->notVisibleTargetAnimQueue);
		aib->notVisibleTargetAnimQueue = NULL;
	}

	aiVarsDestroy(e);
	
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}
#pragma optimize( "", on)

#include "aiDebugShared.h"
#include "aiStruct_h_ast.c"
#include "aiEnums_h_ast.c"
