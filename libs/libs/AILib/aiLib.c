#include "aiLib.h"

#include "aiAnimList.h"
#include "aiAvoid.h"
#include "aiBrawlerCombat.h"
#include "aiCivilian.h"
#include "aiConfig.h"
#include "aiCombatRoles.h"
#include "aiCombatJob.h"
#include "aiDebug.h"
#include "aiExtern.h"
#include "aiFormation.h"
#include "aiJobs.h"
#include "aiMessages.h"
#include "aiMovement.h"
#include "aiMovementModes.h"
#include "aiMultiTickAction.h"
#include "aiPowers.h"
#include "aiTeam.h"
#include "aiMastermind.h"

#include "AnimList_Common.h"
#include "BeaconPath.h"
#include "Character.h"
#include "Character_target.h"
#include "Cmdparse.h"
#include "CostumeCommonEntity.h"
#include "EntityExtern.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gslCritter.h"
#include "gslEntity.h"
#include "gslMapState.h"
#include "MemoryPool.h"
#include "PowerActivation.h"
#include "PowerApplication.h"
#include "PowersMovement.h"
#include "RegionRules.h"
#include "tokenstore.h"
#include "qsortG.h"
#include "ConsoleDebug.h"
#include "entCritter.h"
#include "gslEncounter.h"

// temp until expression checkin
#include "StringCache.h"

#include "StateMachine.h"
#include "Team.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "CommandQueue.h"
#include "CombatConfig.h"
#include "aiDebugShared.h"
#include "gslEncounterLog.h"

// move this at some point
#include "../WorldLib/wcoll/collcache.h"

#include "aiEnums_h_ast.h"
#include "aiStruct_h_ast.h"
#include "aiCombatRoles_h_ast.h"
#include "aiConfig_h_ast.h"

// For death cleanup
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"

typedef struct GameEvent GameEvent; // required by "ailib_autogen_queuedfuncs.c"

AIMapState aiMapState;

aiStateChangeCallback stateChangeCallback = NULL;

static ExprFuncTable* aiFuncTableStateAction		= NULL;
static ExprFuncTable* aiFuncTableStateOnEntry		= NULL;
static ExprFuncTable* aiFuncTableStateOnEntryFirst	= NULL;
static ExprFuncTable* aiFuncTableTransCondition		= NULL;
static ExprFuncTable* aiFuncTableTransAction		= NULL;
static ExprFuncTable* aiFuncTableGlobal				= NULL;

static ExprContext* aiStaticCheckContext = NULL;
static AIPreferredTargetClearedCallback g_fpPrefferedTargetClearedCallback = NULL;

AIGlobalSettings aiGlobalSettings;

bool aiDisableForUGC = false;

// Static variables for AI freezing during cutscenes
static bool s_bCheckForFrozenEnts = false;
static aiIsEntFrozenCallback s_isEntFrozenCB;

static S32 s_bDisableAmbientTethering = false;
AUTO_CMD_INT(s_bDisableAmbientTethering, aiDisableAmbientTether);

EntityRef *g_NeedOnEntry = NULL;

static int aiGetGroundNormal(WorldColl* wc, const Vec3 sourcePos, Vec3 outNormal);

void aiAggroExpressionLoad(void);
void aiAggroExpressionSetupContexts(void);
static EntityRef aiUpdatePreferredTarget(Entity* e, AIVarsBase* aib);

static void aiSetLeashType(AIVarsBase *aib, AILeashType eLeashType, bool bIgnoreOverride);

AIPartitionState* aiPartitionStateGet(int partitionIdx, int create)
{
	AIPartitionState *partition = eaGet(&aiMapState.partitions, partitionIdx);
	int i, numBuckets;

	if(partition)
		return partition;

	if(!create)
		return NULL;

	partition = callocStruct(AIPartitionState);
	partition->idx = partitionIdx;
	eaSet(&aiMapState.partitions, partition, partitionIdx);

	numBuckets = ARRAY_SIZE(partition->entBuckets);
	for(i=0; i<numBuckets; i++)
	{
		partition->entBuckets[i].entsByRef = stashTableCreateInt(10);
		partition->entBuckets[i].timeLastUpdate = 1+SEC_TO_ABS_TIME(1.0/numBuckets*i);
	}

	partition->priorityEntBucket.entsByRef = stashTableCreateInt(30);
	partition->priorityEntBucket.timeLastUpdate = 1;

	return partition;
}

void aiPartitionStateDestroy(int partitionIdx)
{
	AIPartitionState *partition = eaGet(&aiMapState.partitions, partitionIdx);
	int i;

	if(!partition)
		return;

	eaDestroy(&partition->envAvoidEntries);
	eaDestroy(&partition->envAvoidEntriesOld);
	eaDestroyEx(&partition->envAvoidNodes, beaconDestroyAvoidNode_CalledFromAI);

	for(i=0; i<ARRAY_SIZE(partition->entBuckets); i++)
	{
		StashTableIterator entiter;
		StashElement elem;
		
		stashGetIterator(partition->entBuckets[i].entsByRef, &entiter);
		while(stashGetNextElement(&entiter, &elem))
		{
			Entity *e = entFromEntityRefAnyPartition(stashElementGetIntKey(elem));

			if(!e)
				continue;

			e->aibase->entBucket = NULL;
		}

		stashTableDestroy(partition->entBuckets[i].entsByRef);
	}

	{
		StashTableIterator entiter;
		StashElement elem;
		
		stashGetIterator(partition->priorityEntBucket.entsByRef, &entiter);
		while(stashGetNextElement(&entiter, &elem))
		{
			Entity *e = entFromEntityRefAnyPartition(stashElementGetIntKey(elem));

			if(!e)
				continue;

			e->aibase->entBucket = NULL;
		}

		stashTableDestroy(partition->priorityEntBucket.entsByRef);
	}

	free(partition);

	eaSet(&aiMapState.partitions, NULL, partitionIdx);
}

ExprContext* aiGetStaticCheckExprContext()
{
	if(!aiStaticCheckContext)
	{
		devassertmsg(aiFuncTableStateAction, "Can't call aiGetStaticCheckExprContext before setting up the ai function tables");
		aiStaticCheckContext = exprContextCreate();
		aiSetupExprContext(NULL, NULL, PARTITION_STATIC_CHECK, aiStaticCheckContext, true);
	}

	return aiStaticCheckContext;
}

static void aiCreateExprFuncTables(void)
{
	int i;
	const char* commonTags[] = {"util", "ai", "entity", "entityutil", "gameutil"};

	if(!aiFuncTableStateAction)
	{
		const char *tags[] = {"ai_movement","ai_powers"};

		aiFuncTableStateAction = exprContextCreateFunctionTable("AI_Action");		
		for(i=0; i<ARRAY_SIZE(commonTags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableStateAction, commonTags[i]);
		for(i=0; i<ARRAY_SIZE(tags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableStateAction, tags[i]);
	}

	if(!aiFuncTableStateOnEntry)
	{
		const char *tags[] = {"ai_powers"};

		aiFuncTableStateOnEntry = exprContextCreateFunctionTable("AI_OnEntry");
		for(i=0; i<ARRAY_SIZE(commonTags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableStateOnEntry, commonTags[i]);
		for(i=0; i<ARRAY_SIZE(tags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableStateOnEntry, tags[i]);
	}

	if(!aiFuncTableStateOnEntryFirst)
	{
		const char *tags[] = {"ai_powers"};

		aiFuncTableStateOnEntryFirst = exprContextCreateFunctionTable("AI_OnEntryFirst");
		for(i=0; i<ARRAY_SIZE(commonTags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableStateOnEntryFirst, commonTags[i]);
		for(i=0; i<ARRAY_SIZE(tags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableStateOnEntryFirst, tags[i]);
	}

	if(!aiFuncTableTransAction)
	{
		aiFuncTableTransAction = exprContextCreateFunctionTable("AI_TransAction");
		for(i=0; i<ARRAY_SIZE(commonTags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableTransAction, commonTags[i]);
		//for(i=0; i<ARRAY_SIZE(tags); i++)
			//exprContextAddFuncsToTableByTag(aiFuncTableTransAction, tags[i]);
	}

	if(!aiFuncTableTransCondition)
	{
		aiFuncTableTransCondition = exprContextCreateFunctionTable("AI_TransCondition");
		for(i=0; i<ARRAY_SIZE(commonTags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableTransCondition, commonTags[i]);
		//for(i=0; i<ARRAY_SIZE(tags); i++)
			//exprContextAddFuncsToTableByTag(aiFuncTableTransCondition, tags[i]);
	}

	if(!aiFuncTableGlobal)
	{
		const char *tags[] = {"ai_powers","ai_movement"};

		aiFuncTableGlobal = exprContextCreateFunctionTable("AI_Global");
		for(i=0; i<ARRAY_SIZE(commonTags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableGlobal, commonTags[i]);
		for(i=0; i<ARRAY_SIZE(tags); i++)
			exprContextAddFuncsToTableByTag(aiFuncTableGlobal, tags[i]);
	}
}

#define AIGLOBALSETTINGS_FILENAME	"ai/aiGlobalSettings.txt"
//typedef void (*FolderCacheCallback)(const char *relpath, int when);
void aiSettingsReload(const char *relpath, int when)
{
	loadstart_printf("Reloading AIGlobalSettings...");

	StructDeInit(parse_AIGlobalSettings, &aiGlobalSettings);

	ParserReadTextFile(relpath, parse_AIGlobalSettings, &aiGlobalSettings, 0);
	
	if (aiGlobalSettings.pBrawlerConfig)
	{
		aiBrawlerCombat_ValidateGlobalConfig(AIGLOBALSETTINGS_FILENAME, aiGlobalSettings.pBrawlerConfig);
		// use a big hammer to reload the aiconfigs incase there are some brawler overrides we need to reinherit
		aiConfigReloadAll();
	}

	loadend_printf("done");
}

void aiSettingsLoad(void)
{
	StructInit(parse_AIGlobalSettings, &aiGlobalSettings);
	ParserLoadFiles(NULL, "ai/aiGlobalSettings.txt", "aiGlobalSettings.bin", PARSER_OPTIONALFLAG | PARSER_SERVERSIDE, parse_AIGlobalSettings, &aiGlobalSettings);
	
	if (aiGlobalSettings.pBrawlerConfig)
		aiBrawlerCombat_ValidateGlobalConfig(AIGLOBALSETTINGS_FILENAME, aiGlobalSettings.pBrawlerConfig);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/aiglobalsettings.txt", aiSettingsReload);
}

int aiGetPartitionEntRef(EntityRef ref)
{
	Entity *e = entFromEntityRefAnyPartition(ref);

	if(e)
		return entGetPartitionIdx(e);

	return 0;
}

AUTO_STARTUP(AIEarly) ASTRT_DEPS(WorldLibMain);
void aiLibStartup(void)
{
	loadstart_printf("Initializing AI system...");
	aiCreateExprFuncTables();
	aiSettingsLoad();
	aiAggroExpressionSetupContexts();
	aiAggroExpressionLoad();
	aiConfigLoad(); // depends on aiAggroExpressionLoad()
	aiCivilianStartup();
	aiCombatRoles_Startup();
	aiMastermind_Startup();
	aiFormation_Startup();
	
	beaconSetPathFindPartitionCallback(aiGetPartitionEntRef);
	beaconSetPathFindIsFlyingCallback(aiMovementGetFlyingEntRef);
	beaconSetPathFindCanFlyCallback(aiMovementGetCanFlyEntRef);
	beaconSetPathFindAlwaysFlyingCallback(aiMovementGetAlwaysFlyEntRef);
	beaconSetPathFindNeverFlyingCallback(aiMovementGetNeverFlyEntRef);
	beaconSetPathFindTurnRateCallback(aiMovementGetTurnRateEntRef);
	beaconSetPathFindJumpHeightCallback(aiMovementGetJumpHeightEntRef);
	beaconSetPathFindJumpHeightMultCallback(aiMovementGetJumpHeightMultEntRef);
	beaconSetPathFindJumpDistMultCallback(aiMovementGetJumpDistMultEntRef);
	beaconSetPathFindJumpCostCallback(aiMovementGetJumpCostEntRef);

	worldLibSetAIGetStaticCheckExprContextFunc(aiGetStaticCheckExprContext);
	loadend_printf("done.");
}

AUTO_STARTUP(AIBeforePowers) ASTRT_DEPS(AIEarly, AIPowerConfigDefs);
void aiBeforePowersStartup(void)
{

}

AUTO_STARTUP(AI) ASTRT_DEPS(Expression, Powers, PowerModes, AIBeforePowers, Nemesis, AnimLists, AIAmbient, AICombatJob, AIGroupCombat, PlayerDifficulty);
void aiLibStartupAfterPowers(void)
{
	if(gConf.bBetterFSMValidation)
	{
		fsmLoad("AI", 
				"ai/BScript", 
				"aiBScript.bin", 
				aiGetStaticCheckExprContext(),
				aiFuncTableStateAction,
				aiFuncTableStateOnEntry,
				aiFuncTableStateOnEntryFirst,
				aiFuncTableTransCondition,
				aiFuncTableTransAction);

		fsmLoad("UGC", 
				"ai/UGC", 
				"aiUGC_NPC_FSM.bin", 
				aiGetStaticCheckExprContext(),
				aiFuncTableStateAction,
				aiFuncTableStateOnEntry,
				aiFuncTableStateOnEntryFirst,
				aiFuncTableTransCondition,
				aiFuncTableTransAction);

		fsmLoad("UGCMap", 
				"maps", 
				"aiUGCMap_NPC_FSM.bin", 
				aiGetStaticCheckExprContext(),
				aiFuncTableStateAction,
				aiFuncTableStateOnEntry,
				aiFuncTableStateOnEntryFirst,
				aiFuncTableTransCondition,
				aiFuncTableTransAction);
	}
	else
	{
		fsmLoad("AI", 
				"ai/BScript", 
				"aiBScript.bin", 
				aiGetStaticCheckExprContext(),
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal);

		fsmLoad("UGC", 
				"ai/UGC", 
				"aiUGC_NPC_FSM.bin", 
				aiGetStaticCheckExprContext(),
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal);

		fsmLoad("UGCMap", 
				"maps", 
				"aiUGCMap_NPC_FSM.bin", 
				aiGetStaticCheckExprContext(),
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal,
				aiFuncTableGlobal);
	}
	aiPowerConfigLoad();
}

MP_DEFINE(AIStatusTableEntry);

static AIStatusTableEntry* aiStatusTableEntryCreate()
{
	MP_CREATE(AIStatusTableEntry, 8);
	return MP_ALLOC(AIStatusTableEntry);
}

static void aiClearAssignedTarget(Entity *e, AIVarsBase *aib, AITeam *team)
{
	aiTeamSetAssignedTarget(team, aiTeamGetMember(e, aib, team), NULL, 0);
}

static void aiStatusTableEntryDestroy(Entity* e, AIStatusTableEntry* entry)
{
	Entity *target = entFromEntityRef(entGetPartitionIdx(e), entry->entRef);
	EntityRef myRef = entGetRef(e);
	AIVarsBase* aib = e->aibase;

	if(entry->inAvoidList)
		aiAvoidRemoveProximityVolumeEntry(e, e->aibase, entry);
	if(entry->inAttractList)
		aiAttractRemoveProximityVolumeEntry(e, e->aibase, entry);

	if(aib->member->assignedTarget==entry->entRef)
		aiClearAssignedTarget(e, aib, aib->team);
	
	if(aib->combatMember && aib->combatMember->assignedTarget==entry->entRef)
		aiClearAssignedTarget(e, aib, aib->combatTeam);

	eafDestroy(&entry->aggroCounters);
	eafDestroy(&entry->aggroCounterDecay);
	eafDestroy(&entry->aggroGauges);
	eafDestroy(&entry->aggroGaugeValues);

	MP_FREE(AIStatusTableEntry, entry);
}

//move this to its own file eventually
AIStatusTableEntry* aiStatusFind(Entity* e, AIVarsBase* aib, Entity* target, int create)
{
	AIStatusTableEntry* status;
	EntityRef targetRef;
	AITeamStatusEntry *teamStatus = NULL;

	if(!target->pChar)
	{
		if(create)
		{
			Vec3 pos;

			entGetPos(e, pos);
			ErrorDetailsf("%s->%s "LOC_PRINTF_STR, ENTDEBUGNAME(e), ENTDEBUGNAME(target), vecParamsXYZ(pos));
			ErrorfForceCallstack("Can't create status entry for non-combat entities");
			
			if(!isProductionMode())
				assertmsg(0, "Can't create status entry for non-combat entities");
		}
		return NULL;
	}

	targetRef = entGetRef(target);
	if(!stashIntFindPointer(aib->statusHashTable, targetRef, &status) && create)
	{
		status = aiStatusTableEntryCreate();
		status->entRef = targetRef;
		stashIntAddPointer(aib->statusHashTable, targetRef, status, false);
		eaPush(&aib->statusTable, status);

		// do this so there's no crazy 0 distances screwing things up
		status->distanceFromMe = 1;

		// push into the "clean status entries when I die" list
		eaiPush(&target->aibase->statusCleanup, entGetRef(e));

		status->ambientStatus = aiTeamStatusFind(aib->team, target, true, false);
		entGetPos(target, status->ambientStatus->lastKnownPos);
		if(aib->combatTeam)
		{
			status->combatStatus = aiTeamStatusFind(aib->combatTeam, target, true, false);
			entGetPos(target, status->combatStatus->lastKnownPos);
		}
	}

	return status;
}

void aiStatusRemove(Entity* e, AIVarsBase* aib, Entity* target, const char* reason)
{
	EntityRef targetRef = entGetRef(target);
	AIStatusTableEntry* status;

	if(stashIntRemovePointer(aib->statusHashTable, targetRef, &status))
	{
		if(status==aib->attackTargetStatus)
			aiSetAttackTarget(e, aib, NULL, NULL, 1);

		eaFindAndRemoveFast(&aib->statusTable, status);
		aiStatusTableEntryDestroy(e, status);

		AI_DEBUG_PRINT_TAG(e, AI_LOG_COMBAT, 3, AICLT_LEGALTARGET, "%s: removed from StatusList: %s", ENTDEBUGNAME(target), reason);

		// remove ourselves from their status cleanup
		eaiFindAndRemoveFast(&target->aibase->statusCleanup, entGetRef(e));
	}
}

F32 aiGetCosToPoint(const Mat4 mat, Vec3 pos)
{
	float cosBetween;
	Vec3 toMe;
	float length;

	subVec3(pos, mat[3], toMe);

	cosBetween = dotVec3(mat[2], toMe);

	// Assume the mat[2] vector has length 1, so only divide by the length of the vector to me.

	length = lengthVec3(toMe);
	if(length != 0.0)
	{
		cosBetween /= length;
	}
	else
	{
		cosBetween = 0.0;
	}

	// Check if the cosine is greater than the cosine of 120 degrees.

	return cosBetween;
}

int OVERRIDE_LATELINK_exprFuncHelperShouldExcludeFromEntArray(Entity* e)
{
	return aiCheckIgnoreFlags(e);
}

int aiIsValidTarget(Entity * pEnt, AITeam* team, Entity* target, AITeamStatusEntry* teamStatus)
{
	int doLevelCheck;
	
	if(aiCheckIgnoreFlags(target) || !team)
	{
		if (!aiGlobalSettings.untargetableIsTreatedNotVisible || 
			!(teamStatus && teamStatus->legalTarget && entCheckFlag(target, ENTITYFLAG_UNTARGETABLE|ENTITYFLAG_UNSELECTABLE)))
		{
			return false;
		}
	}

	if(target->aibase)
	{
		if(target->aibase->untargetable)
			return false;
	}

	if(!team->config.ignoreLevelDifference && (!pEnt || !pEnt->pChar || !pEnt->pChar->bLevelAdjusting))
	{
		Entity* checkEnt = target;

		doLevelCheck = !!entCheckFlag(target, ENTITYFLAG_IS_PLAYER);

		while(!doLevelCheck && checkEnt && checkEnt->erOwner)
		{
			checkEnt = entFromEntityRef(team->partitionIdx, checkEnt->erOwner);
			if(checkEnt && entCheckFlag(checkEnt, ENTITYFLAG_IS_PLAYER))
				doLevelCheck = true;
		}

		if(doLevelCheck && team->teamLevel + aiGlobalSettings.iAggroIgnoreLevelDelta < checkEnt->pChar->iLevelCombat)
		{
			// if level difference is too big, target is not valid unless they've damaged
			// you (or are otherwise a legal target)
			if(teamStatus)
				return teamStatus && teamStatus->legalTarget;
			else
			{
				// the only place that doesn't pass in teamStatus is the code that checks
				// whether it should add a status table entry, so this can return false
				// because there will by definition already be a status table entry if that's
				// the case
				return false;
			}
		}
	}

	return true;
}

// if I'm confused, check if I can attack these guys
// assumes target has passed other basic target validating checks
int aiIsValidConfusedTarget(AIVarsBase *aib, Entity *target )
{
	if (aib->confused)
	{	// when confused, target is only valid confuse target if its team is in combat
		// though we ignore the check if it's a player or player's pet
		AITeam *targetCombatTeam = aiTeamGetCombatTeam(target, target->aibase);
		if (targetCombatTeam->combatState != AITEAM_COMBAT_STATE_FIGHT && 
			!(entIsPlayer(target) || entIsPlayer(entGetOwner(target))) )
		{
			return false;
		}
	}

	return true;
}
 
void aiAddLegalTarget(Entity* e, AIVarsBase* aib, Entity* target)
{
	if(aiGlobalSettings.enableCombatTeams)
	{
		if(!aib->combatTeam)
		{
			AIConfig *config = aiGetConfig(e, aib);

			aiTeamAdd(aiTeamCreate(entGetPartitionIdx(e), NULL, true), e);
			ANALYSIS_ASSUME(aib->combatTeam);

			aiCombatAddTeamToCombatTeam(e, aib, true);

			aiTeamCopyTeamSettingsToCombatTeam(aib->team, aib->combatTeam);

			if (!aib->combatTeam->bIgnoreSocialAggroPulse)
				aiCombatDoSocialAggroPulse(e, aib, true);
		}
		aiTeamAddLegalTarget(aib->combatTeam, target);
	}
	else
		aiTeamAddLegalTarget(aib->team, target);
}

static bool _isEntityRefPlayer(int iPartitionIdx, EntityRef erEntity)
{
	Entity *e = entFromEntityRef(iPartitionIdx, erEntity);
	
	return e && entIsPlayer(e);
}

int aiIsOrHasLegalTarget(Entity* e, F32 range, bool bDisallowPlayerTeamAggro, Entity **ppAggroingEntityOut)
{
	int i, n;
	if (e && e->aibase && aiIsEntAlive(e))
	{	
		AIVarsBase* aib = e->aibase;
		AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);
		int iPartitionIdx = entGetPartitionIdx(e);

		if (combatTeam->combatState == AITEAM_COMBAT_STATE_FIGHT && 
			(!bDisallowPlayerTeamAggro || !_isEntityRefPlayer(iPartitionIdx, combatTeam->teamLeaderRef)))
		{
			// If we're aggrod ourselves then we're in combat
			if (ppAggroingEntityOut) *ppAggroingEntityOut = e;
			return true;
		}

		n = eaiSize(&aib->statusCleanup);
		for(i = 0; i < n; i++)
		{
			Entity* statusE = entFromEntityRef(iPartitionIdx, aib->statusCleanup[i]);
			AITeamStatusEntry *pEntry;
			AITeam* statusCombatTeam;

			if(!statusE || !aiIsEntAlive(statusE))
				continue;

			statusCombatTeam = aiTeamGetCombatTeam(statusE, statusE->aibase);

			if(statusCombatTeam->combatState != AITEAM_COMBAT_STATE_FIGHT)
				continue;
			if (bDisallowPlayerTeamAggro && _isEntityRefPlayer(iPartitionIdx, statusCombatTeam->teamLeaderRef))
				continue;

			if (pEntry = aiTeamStatusFind(statusCombatTeam, e, false, false))
			{
				// If anyone else has me a legal target I'm in combat
				if (pEntry->legalTarget)
				{
					if (range)
					{
						if (entGetDistance(statusE, NULL, e, NULL, NULL) <= range)
						{
							if (ppAggroingEntityOut) *ppAggroingEntityOut = statusE;
							return true;
						}
						else
						{
							// Outside radius, ignore them
							continue;
						}
					}

					if (ppAggroingEntityOut) *ppAggroingEntityOut = statusE;
					return true;
				}
			}				
		}		
	}
	return 0;
}

static void aiStatusTransferAggroToOwner(Entity* e, AIVarsBase* aib, AIConfig* config, Entity* owner, Entity* target)
{
	AIStatusTableEntry *status = aiStatusFind(target, target->aibase, e, 0);
	AIStatusTableEntry *statusOwner = aiStatusFind(target, target->aibase, owner, 0);
	F32 ratio = 1;

	if(!ratio || config->dontTransferAggroOnDeath)
		return;

	if(status && statusOwner)
	{
		int i, j;
		// Because it is all already added to the team, just shunt all the numbers over
		for(i=0; i < AI_NOTIFY_TARGET_COUNT; i++)
		{
			for(j=0; j < AI_NOTIFY_TYPE_TRACKED_COUNT; j++)
				statusOwner->trackedAggroCounts[i][j] += ratio * status->trackedAggroCounts[i][j];
		}
	}
}

void aiCleanupStatus(Entity *e, AIVarsBase *aib)
{
	int i;
	AIConfig* config = aiGetConfig(e, aib);
	int iPartitionIdx = entGetPartitionIdx(e);
	Entity* owner = entFromEntityRef(iPartitionIdx, e->erOwner);
	EntityRef myRef = entGetRef(e);
	static U32 *tmpStatusCleanup = NULL;

	// make a copy of the array since in aiTeamStatusRemove
	// there will be calls that will remove from statusCleanup that we will be iterating over
	// TODO: possibly fix this in a better way 
	eaiClearFast(&tmpStatusCleanup);
	eaiPushEArray(&tmpStatusCleanup, &aib->statusCleanup);

	// Remove all the status table entries ppl had referring to me so they don't reaggro
	// on resurrect/respawn
	for(i = eaiSize(&tmpStatusCleanup) - 1; i >= 0; i--)
	{
		Entity* statusE = entFromEntityRef(iPartitionIdx, tmpStatusCleanup[i]);

		if(!statusE)
			continue;

		if(owner)
			aiStatusTransferAggroToOwner(e, aib, config, owner, statusE);

		aiTeamStatusRemove(statusE->aibase->team, e, "StatusCleanup");
		if(statusE->aibase->combatTeam)
			aiTeamStatusRemove(statusE->aibase->combatTeam, e, "StatusCleanup (combat team)");
		
		eaiFindAndRemoveFast(&statusE->aibase->statusCleanup, entGetRef(e));
	}
	eaiClearFast(&aib->statusCleanup);
}

// Removes all tracked data of any enemies this entity knows about
AUTO_EXPR_FUNC(ai) ACMD_NAME(WipeAggroInfo);
void aiClearStatusTable(ACMD_EXPR_SELF Entity* e)
{
	AIVarsBase* aib = e->aibase;
	int i, n;

	aiSetAttackTarget(e, aib, NULL, NULL, true);

	n = eaSize(&aib->statusTable);
	for(i = 0; i < n; i++)
		aiStatusTableEntryDestroy(e, aib->statusTable[i]);

	if(eaSize(&aib->statusTable))
		eaSetSize(&aib->statusTable, 0);
	stashTableClear(aib->statusHashTable);

	ZeroArray(aib->totalTrackedDamage);
}

static bool aiIsEntityOnTeamWithAggro(int iPartitionIdx, Entity *e, AIVarsBase* aib, Entity* targetE)
{
	Team *teamContainer;
	S32 i;

	teamContainer = team_GetTeam(targetE);
	if(teamContainer)
	{
		for(i = eaSize(&aib->statusTable) - 1; i >= 0; i--)
		{
			AIStatusTableEntry* status = aib->statusTable[i];
			AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);
			if (teamStatus && teamStatus->legalTarget)
			{
				Entity* statusE = entFromEntityRef(iPartitionIdx, status->entRef);
				if (teamContainer == team_GetTeam(statusE))
					return true;
			}
		}
	}

	return false;
}

static __forceinline bool aiUpdateShouldAddLegalTarget(int partitionIdx, Entity *e, int playerAffiliated, 
														AIConfig* config, F32 localAwareRadius, 
														AIStatusTableEntry* status, AITeam* team, 
														Entity* statusBE, AIVarsBase* aib)
{
	return
		(	
			(!playerAffiliated && !config->dontAggroInAggroRadius) 
				|| 
			(
				status->distanceFromMe < localAwareRadius || 
				(!config->dontAggroInAggroRadius && ABS_TIME_SINCE_PARTITION(partitionIdx, status->time.enteredAggroRadius) > SEC_TO_ABS_TIME(config->stareDownTime))
			)
		) 
		&&
		(	team->combatState == AITEAM_COMBAT_STATE_AMBIENT || 
			team->combatState == AITEAM_COMBAT_STATE_STAREDOWN ||
			team->combatState == AITEAM_COMBAT_STATE_WAITFORFIGHT ||
			team->combatState == AITEAM_COMBAT_STATE_FIGHT && 
				(ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastStartedCombat) < SEC_TO_ABS_TIME(config->stareDownTime) ||
					ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastStartedCombat) < SEC_TO_ABS_TIME(2) || 
					aiIsEntityOnTeamWithAggro(partitionIdx, e, aib, statusBE))
		);
}

// 
static void aiUpdateStatusList_UpdateIfNotTeamLegalTarget(int partitionIdx, Entity* e, AIVarsBase* aib, AIConfig* config, 
															Entity* statusE, AIStatusTableEntry* status, 
															AITeam* team, AITeamStatusEntry *teamStatus, 
															int playerAffiliated, int isSeekTarget, F32 yDiff)
{
	if(teamStatus->legalTarget == false)
	{
		F32 localAwareRadius;
		F32 localAggroRadius;

		if (config->seekTargetOverrideAggroRadius == 0)
		{
			localAggroRadius = aib->aggroRadius;
		}
		else 
		{
			// check to see if this entity is on our target seek list
			if (!isSeekTarget)
			{
				localAggroRadius = aib->aggroRadius;
			}
			else
			{
				localAggroRadius = config->seekTargetOverrideAggroRadius;
			}
		}

		if (team->combatState == AITEAM_COMBAT_STATE_STAREDOWN)
		{
			localAggroRadius += config->staredownAdditiveAggroRadius;
		}

		localAggroRadius = character_DistApplyStealth(statusE, localAggroRadius, aiExternGetStealth(statusE), e->pChar->pattrBasic->fStealthSight);
		
		localAwareRadius = aib->awareRatio * localAggroRadius;
				
		if(status->distanceFromMe < localAggroRadius && 
			(!config->aggroYDiffCap || yDiff < config->aggroYDiffCap))
		{
			if(!status->time.enteredAggroRadius)
				status->time.enteredAggroRadius = ABS_TIME_PARTITION(partitionIdx);

			if(aiTeamTargetWithinLeash(aiTeamGetMember(e, aib, team), team, statusE, NULL) && 
				aiEvalTargetingRequires(e, aib, config, statusE))
			{
				if(aiUpdateShouldAddLegalTarget(partitionIdx, e, playerAffiliated, config, localAwareRadius, 
					status, team, statusE, aib))
				{
					teamStatus->timeLastAggressiveAction = ABS_TIME_PARTITION(partitionIdx);
					aiAddLegalTarget(e, aib, statusE);
				}

				if(!config->dontAggroInAggroRadius && team->combatState == AITEAM_COMBAT_STATE_AMBIENT)
					aiTeamTriggerStaredown(team);

				aib->time.lastHadStaredownTarget = ABS_TIME_PARTITION(partitionIdx);
			}
		}
		else
			status->time.enteredAggroRadius = 0;
	}
}


static void aiUpdateStatusList(Entity* e, AIVarsBase* aib, bool bSendAggro)
{
	int i, n, c;
	Vec3 myPyr = {0};
	Mat4 myMat;
	AIConfig* config = NULL;
	AITeam* team = NULL;
	AITeamMember* teamMember = NULL;
	Entity* owner = NULL;
	F32 myCollRadius;
	F32 myCollPlusPerception;
	static U32* legalTargets;
	int partitionIdx = -1;
	PerfInfoGuard *guard;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	config = aiGetConfig(e, aib);
	team = aiTeamGetCombatTeam(e, aib);
	teamMember = aiTeamGetMember(e, aib, team);
	owner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
	partitionIdx = entGetPartitionIdx(e);

	entGetFacePY(e, myPyr);

	createMat3YPR(myMat, myPyr);
	entGetPos(e, myMat[3]);

	mmGetCollisionRadius(e->mm.movement, &myCollRadius);
	myCollPlusPerception = myCollRadius + aib->aggroRadius;

	c = 0;

	// only evaluate the 50 closest things so you don't explode with a lot of stuff bundled together
	aiUpdateProxEnts(e,aib);
	for(i = 0; i < aib->proxEntsCount && c < 50; i++)
	{
		Entity* target = aib->proxEnts[i].e;
		AIStatusTableEntry* status;
		F32 checkRadius;

		checkRadius = myCollPlusPerception + aib->proxEnts[i].maxCollRadius;

		// Don't update status until 10 seconds pass or you enter combat (from being attacked, e.g.)
		if(!entIsPlayer(e) && !entIsPlayer(entGetOwner(e)) && (entIsPlayer(target) || entIsPlayer(entGetOwner(target))) && e->pCritter)
		{
			bool bDisableSpawnAggroDelay = false; 

			if (e->pCritter->encounterData.pGameEncounter &&
				e->pCritter->encounterData.pGameEncounter->pWorldEncounter &&
				e->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties &&
				e->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties->pSpawnProperties)
			{
				bDisableSpawnAggroDelay = e->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties->pSpawnProperties->bDisableAggroDelay;
			}

			if (!bDisableSpawnAggroDelay &&
				((zmapInfoGetMapType(NULL) == ZMTYPE_STATIC && e->pCritter->encounterData.bEnableSpawnAggroDelay) ||
				aiGlobalSettings.alwaysUseSpawnAggroDelay))
			{
				if(!ABS_TIME_PASSED_PARTITION(partitionIdx, aib->time.timeSpawned, team->minSpawnAggroTime) && !aib->time.lastEnteredCombat)
					continue;
			}
		}

		if(aib->proxEnts[i].maxDistSQR > SQR(checkRadius))
			continue;

		if(entCheckFlag(target, ENTITYFLAG_CIV_PROCESSING_ONLY))
		{
			// Note: Civilians do not have a pChar, so we're only doing the civilian scare check in this case.

			// If we are marked to scare pedestrians or we are in combat, scare any pedestrians in range
			if(aib->isHostileToCivilians || config->alwaysScarePedestrians || team->combatState == AITEAM_COMBAT_STATE_FIGHT)
			{
				F32 fScareDist;

				fScareDist = (team->combatState != AITEAM_COMBAT_STATE_FIGHT) ? config->pedestrianScareDistance : 
																				config->pedestrianScareDistanceInCombat;
				fScareDist += myCollRadius;

				if (aib->proxEnts[i].maxDistSQR <= SQR(fScareDist))
					aiCivScarePedestrian(target, e, myMat[3]);
			}

			continue;
		}

		if(!entIsAlive(target) || (!entIsPlayer(target) && !aiIsEntAlive(target)))
			continue;

		if(!target->pChar)
			continue;

		// Added due to rare crash caused by target->aibase being NULL
		if(!target->aibase)
		{
			continue;
		}

		if(!target->aibase->avoid.base.list && !target->aibase->attract.base.list)
		{
			bool bDisableSpawnAggroDelay = false;

			if(GET_REF(target->hCreatorNode))
				continue;

			if (e && e->pCritter && e->pCritter->encounterData.pGameEncounter &&
				e->pCritter->encounterData.pGameEncounter->pWorldEncounter &&
				e->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties &&
				e->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties->pSpawnProperties)
			{
				bDisableSpawnAggroDelay = e->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties->pSpawnProperties->bDisableAggroDelay;
			}

			if((entIsPlayer(target) || entIsPlayer(entGetOwner(target))) && !bDisableSpawnAggroDelay &&
				((zmapInfoGetMapType(NULL) == ZMTYPE_STATIC && e->pCritter && e->pCritter->encounterData.bEnableSpawnAggroDelay) || aiGlobalSettings.alwaysUseSpawnAggroDelay) &&
				ABS_TIME_SINCE_PARTITION(partitionIdx, target->aibase->time.timeSpawned) < SEC_TO_ABS_TIME(aiGlobalSettings.iIgnorePlayerAtSpawnTimeout))
				continue;

			if(!aiIsValidTarget(e, team, target, NULL) || !critter_IsKOS(partitionIdx, e, target))
				continue;

			if(aib->confused && !aiIsValidConfusedTarget(aib, target))
				continue;
		}

		// status list has entries for all possible targets, and all people you possibly have to avoid
		status = aiStatusFind(e, aib, target, true);
		status->maxDistSQR = aib->proxEnts[i].maxDistSQR;
		status->maxCollRadius = aib->proxEnts[i].maxCollRadius;
		status->maxDistSQRCheckTime = ABS_TIME_PARTITION(partitionIdx);
		c++;
	}

	n = eaSize(&aib->statusTable);

	eaiClear(&legalTargets);
	for(i = 0; i < n; i++)
	{
		AIStatusTableEntry* status = aib->statusTable[i];
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);
		Entity* statusE = entFromEntityRef(partitionIdx, status->entRef);
		AIVarsBase* statusAIB;
		int playerAffiliated;
		F32 localPerceptionRadius;
		U32 isKos;
 
		Vec3 targetPos;
		F32 yDiff;
		F32 cosToTarget;
		F32 checkRadius;
		int skipStatus = 0;
		int isSeekTarget = false;

		if(!statusE || !teamStatus)
			continue;

		statusAIB = statusE->aibase;

		isKos = critter_IsKOS(partitionIdx, e, statusE);

		// this has to happen before the ignoring to not ignore stuff there just for you 
		// to get scared
		if(statusAIB->avoid.base.list || status->inAvoidList)
		{
			int avoidTarget;

			PERFINFO_AUTO_START("Update avoid", 1);
			avoidTarget = aiShouldAvoidEntity(e, aib, statusE, statusAIB);
			if((U32)avoidTarget != status->inAvoidList)
			{
				if(avoidTarget)
					aiAvoidAddProximityVolumeEntry(e, aib, status);
				else
					aiAvoidRemoveProximityVolumeEntry(e, aib, status);
			}
			PERFINFO_AUTO_STOP();
		}

		if(statusAIB->softAvoid.base.list || status->inSoftAvoidList)
		{
			int softAvoidTarget;

			PERFINFO_AUTO_START("Update soft avoid", 1);
			softAvoidTarget = aiShouldSoftAvoidEntity(e, aib, statusE, statusAIB);
			if((U32)softAvoidTarget != status->inSoftAvoidList)
			{
				if(softAvoidTarget)
					aiSoftAvoidAddProximityVolumeEntry(e, aib, status);
				else
					aiSoftAvoidRemoveProximityVolumeEntry(e, aib, status);
			}
			PERFINFO_AUTO_STOP();
		}

		if((!isKos && statusAIB->attract.base.list) || status->inAttractList)
		{
			U32 attractToTarget = (!isKos && statusAIB->attract.base.list);

			if(attractToTarget != status->inAttractList)
			{
				if (attractToTarget)
					aiAttractAddProximityVolumeEntry(e, aib, status);
				else
					aiAttractRemoveProximityVolumeEntry(e, aib, status);
			}
		}

		checkRadius = aib->aggroRadius + myCollRadius + status->maxCollRadius;

		skipStatus = 0;
		if(status->maxDistSQRCheckTime == ABS_TIME_PARTITION(partitionIdx) &&
			status->maxDistSQR > SQR(checkRadius) &&
			!teamStatus->legalTarget)
		{
			skipStatus = 1;
		}

		if(!isKos)
		{
			// if you're confused, always update the last status update time to not lose targets
			if(aib->confused)
				teamStatus->timeLastStatusUpdate = ABS_TIME_PARTITION(partitionIdx);

			skipStatus = 1;
		}

		if(skipStatus)
		{
			status->skipCurTick = true;
			status->time.enteredAggroRadius = 0;
			continue;
		}

		status->skipCurTick = false;

		if(!aiIsValidTarget(e,team, statusE, teamStatus))
		{
			continue;
		}
		
		if (config->seekTargetOverrideAggroRadius)
		{
			if (eaiFind(&aib->eaSeekTargets, entGetRef(statusE)) == -1)
				isSeekTarget = true;
		}


		localPerceptionRadius = character_GetPerceptionDist(e->pChar, statusE->pChar);

		teamStatus->timeLastStatusUpdate = ABS_TIME_PARTITION(partitionIdx);

		entGetPos(statusE, targetPos);
		yDiff = vecY(targetPos) - vecY(myMat[3]);
		yDiff = ABS(yDiff);
		cosToTarget = aiGetCosToPoint(myMat, targetPos);

		status->distanceFromMe = entGetDistance(e, NULL, statusE, NULL, NULL);
		status->distanceFromSpawnPos = aiGetGuardPointDistance(e, aib, statusE);

		status->inFrontArc = cosToTarget > 0;

		if(status->time.lastCheckedLOS > ABS_TIME_PARTITION(partitionIdx))
			status->time.lastCheckedLOS = ABS_TIME_PARTITION(partitionIdx);

		if(status->entRef == aib->attackTargetRef || isSeekTarget ||
			ABS_TIME_SINCE_PARTITION(partitionIdx, status->time.lastCheckedLOS) > SEC_TO_ABS_TIME(3) )
		{
			S32 visible = false;
			F32 outOfFOVAggroRange = config->outOfFOVAggroRadius;
			EAINotVisibleReason notVisibleReason = EAINotVisibleReason_NONE;
			status->time.lastCheckedLOS = ABS_TIME_PARTITION(partitionIdx);

			if (aiGlobalSettings.stealthAffectsOutOfFOVRange)
			{
				outOfFOVAggroRange = character_DistApplyStealth(statusE, outOfFOVAggroRange, 
																aiExternGetStealth(statusE), 
																e->pChar->pattrBasic->fStealthSight);
			}

			if (entCheckFlag(statusE, ENTITYFLAG_UNTARGETABLE|ENTITYFLAG_UNSELECTABLE))
			{	// this will only happen if the 
				notVisibleReason = EAINotVisibleReason_UNTARGETABLE;
			}
			else if(team->combatState == AITEAM_COMBAT_STATE_FIGHT ||
					status->distanceFromMe < outOfFOVAggroRange ||
					status->entRef == aib->attackTargetRef ||
					cosToTarget > cos(RAD(config->fov/2)))
			{
				// TODO: make aiPointInViewCone and aiCollideRay work with capsules correctly (a la entGetDistance)
				if(status->distanceFromMe < localPerceptionRadius)
				{
					visible = !aiCollideRay(entGetPartitionIdx(e), e, myMat[3], statusE, targetPos, AICollideRayFlag_NONE);
					if (!visible)
					{
						notVisibleReason = EAINotVisibleReason_LOS;
					}
				}
				else
				{
					if (localPerceptionRadius == 0)
					{
						notVisibleReason = EAINotVisibleReason_PERCEPTION_STEALTH;
					}
					else
					{
						notVisibleReason = EAINotVisibleReason_OUTOF_PERCEPTION;
					}
					visible = false;
				}
			}
			else
			{
				notVisibleReason = EAINotVisibleReason_LOS;
				visible = false;
			}

			if(!visible && status->visible)
			{
				if(aiMovementGetMovementOrderType(e, aib)==AI_MOVEMENT_ORDER_ENT && 
					status->entRef==aiMovementGetMovementTargetEnt(e, aib) &&
					(!aiGlobalSettings.disableLeashingOnNonStaticMaps || zmapInfoGetMapType(NULL)==ZMTYPE_STATIC || team->bLeashOnNonStaticOverride))
				{
					aiMovementSetTargetEntity(e, aib, NULL, NULL, 0, AI_MOVEMENT_ORDER_ENT_UNSPECIFIED, 0);
				}
			}

			status->eNotVisibleReason = notVisibleReason;
			status->visible = visible;
		}

		playerAffiliated = !!statusE->pPlayer;

		if(statusE->erOwner)
		{
			Entity* checkEnt = statusE;

			while(!playerAffiliated && checkEnt && checkEnt->erOwner)
			{
				checkEnt = entFromEntityRef(partitionIdx, checkEnt->erOwner);
				if(checkEnt && checkEnt->pPlayer)
					playerAffiliated = true;
			}
		}

		if(status->visible)
		{ 
			// reset some variables if we're now truly visible
			status->eNotVisibleReason = EAINotVisibleReason_NONE;
			status->time.lastVisible = ABS_TIME_PARTITION(partitionIdx);
			entGetPos(statusE, teamStatus->lastKnownPos);
			status->lostTrack = 0;
			status->visitedLastKnown = 0;
					
			if(teamStatus->legalTarget == false)
			{
				aiUpdateStatusList_UpdateIfNotTeamLegalTarget(partitionIdx, e, aib, config, statusE, status, 
																team, teamStatus, playerAffiliated, isSeekTarget, yDiff);
			}
		}
		else
		{
			// if we're not visible due to EAINotVisibleReason_PERCEPTION_STEALTH, 
			// check if we want to aggro still if we have aiGlobalSettings.aggroOnPerceptionStealth 
			if (aiGlobalSettings.aggroOnPerceptionStealth && 
				(status->eNotVisibleReason == EAINotVisibleReason_PERCEPTION_STEALTH ||
					status->eNotVisibleReason == EAINotVisibleReason_UNTARGETABLE))
			{
				if(teamStatus->legalTarget == false)
				{
					aiUpdateStatusList_UpdateIfNotTeamLegalTarget(partitionIdx, e, aib, config, statusE, status, 
																	team, teamStatus, playerAffiliated, isSeekTarget, yDiff);
				}

				if (!status->time.lastVisible)
					status->time.lastVisible = ABS_TIME_PARTITION(partitionIdx);
			}

			if(ABS_TIME_SINCE_PARTITION(partitionIdx, status->time.lastVisible) > SEC_TO_ABS_TIME(config->targetMemoryDuration))
				status->lostTrack = 1;

			if(aiGlobalSettings.disableLeashingOnNonStaticMaps && zmapInfoGetMapType(NULL)!=ZMTYPE_STATIC && !team->bLeashOnNonStaticOverride)
			{
				status->visitedLastKnown = false;
				status->lostTrack = false;
				if (localPerceptionRadius != 0.f)
					entGetPos(statusE, teamStatus->lastKnownPos);
			}

			status->time.enteredAggroRadius = 0;
		}

		if(teamStatus->legalTarget)
			eaiPush(&legalTargets, status->entRef);

		if(1 /*doDecay*/)
		{
			int useRoleDamageSharing;
			AICombatRolesTeamRole* teamCombatRole;

			if(aiGlobalSettings.useCombatRoleDamageSharing && teamMember->pCombatRole)
			{
				useRoleDamageSharing = true;
				teamCombatRole = teamMember->pCombatRole->pTeamRole;
			}
			else
			{
				useRoleDamageSharing = false;
				teamCombatRole = NULL;
			}

			aiAggroDecay(e, aib, team, teamCombatRole, status, useRoleDamageSharing);
		}
	}

	if(bSendAggro && (eaiSize(&legalTargets) || eaSize(&e->pChar->ppAITargets)))
	{
		int size = eaiSize(&legalTargets);
		ea32QSort(legalTargets, cmpU32);

		eaSetSizeStruct(&e->pChar->ppAITargets, parse_CharacterAITargetInfo, size);

		for (i = size - 1; i >= 0; i--)
		{
			ZeroStruct(e->pChar->ppAITargets[i]);
			e->pChar->ppAITargets[i]->entRef = legalTargets[i];
		}

		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

AUTO_COMMAND ACMD_NAME(disableAI) ACMD_LIST(gEntConCmdList);
void entConDisableAI(Entity* e, int disable)
{
	e->aibase->disableAI = !!disable;
}

AUTO_COMMAND ACMD_NAME(disableAttackAI) ACMD_LIST(gEntConCmdList);
void entConDisableAttackAI(Entity* e, int disable)
{
	e->aibase->doAttackAI = !disable;
}

AUTO_COMMAND;
void entConEnableAIProximity(Entity* e, int enable)
{
	e->aibase->doProximity = !!enable;
}

AUTO_COMMAND ACMD_NAME(editAIConfig) ACMD_LIST(gEntConCmdList);
void entConEditAIConfig(Entity *e)
{
	AIConfig *baseConfig = GET_REF(e->aibase->config_use_accessor);
	char filename[MAX_PATH];

	if(baseConfig)
	{
		fileLocateWrite(baseConfig->filename, filename);
		fileOpenWithEditor(filename);
	}
}

int gDisableAICombatMovement = false;
AUTO_CMD_INT(gDisableAICombatMovement, DisableAICombatMovement);

int gAIConstantCombatMovement = false;
AUTO_CMD_INT(gAIConstantCombatMovement, AIConstantCombatMovement);

int gAIForceUpdateProxEnts = 0;

void aiForceProxUpdates(int on)
{
	gAIForceUpdateProxEnts = !!on;
}

// not perfectly accurate, in figuring out the "distance", but accurate enough
// given the speed difference for the ai
int cmpEntByDistForAIProxEnts(const EntAndDist* lhs, const EntAndDist* rhs)
{
	F32 diff = (lhs->maxDistSQR - SQR(lhs->maxCollRadius)) -
		(rhs->maxDistSQR - SQR(rhs->maxCollRadius));

	if(diff == 0)
		return 0;
	else
		return SIGN(diff);
}

void aiUpdateProxEnts(Entity *e, AIVarsBase *aib)
{
	Vec3 bePos;
	int partitionIdx = -1;
	PerfInfoGuard *guard;
	
	PERFINFO_AUTO_START_FUNC_GUARD(&guard);
	partitionIdx = entGetPartitionIdx(e);

	if(!gAIForceUpdateProxEnts && aib->time.refreshedProxEnts == ABS_TIME_PARTITION(partitionIdx))
	{
		PERFINFO_AUTO_STOP_GUARD(&guard);
		return;
	}

	entGetPos(e, bePos);
	PERFINFO_AUTO_START("entGridProximityLookup", 1);
	entGridProximityLookupDynArraySaveDist(entGetPartitionIdx(e), bePos, &aib->proxEnts, &aib->proxEntsCount,
		&aib->proxEntsMaxCount, aib->proximityRadius, 0, ENTITYFLAG_DEAD |
		ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE | ENTITYFLAG_PROJECTILE, e);
	PERFINFO_AUTO_STOP_START("qsort", 1);
	qsort(aib->proxEnts, aib->proxEntsCount, sizeof(aib->proxEnts[0]), cmpEntByDistForAIProxEnts);
	aib->time.refreshedProxEnts = ABS_TIME_PARTITION(partitionIdx);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

void aiGetLeashPosition(Entity *e, AIVarsBase *aib, Vec3 vOutPos)
{
	switch (aiGetLeashType(aib))
	{
		acase AI_LEASH_TYPE_RALLY_POSITION:
		{
			copyVec3(aiGetRallyPosition(aib), vOutPos);
		} 	
		xcase AI_LEASH_TYPE_ENTITY:
		{
			Entity* leashEnt = aib->erLeashEntity ? entFromEntityRef(entGetPartitionIdx(e), aib->erLeashEntity) : NULL;
			if (leashEnt)
			{
				entGetPos(leashEnt, vOutPos);
				return;
			}

			// can't find the owner, just use my current position
			entGetPos(e, vOutPos);
		}
		xcase AI_LEASH_TYPE_OWNER:
		{
			Entity* owner = e->erOwner ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;
			if (owner)
			{
				entGetPos(owner, vOutPos);
				return;
			}

			// can't find the owner, just use my current position
			entGetPos(e, vOutPos);
		} 

		xcase AI_LEASH_TYPE_DEFAULT:
		default:
		{
			AITeam *combatTeam = aiTeamGetCombatTeam(e, aib);
			devassert(combatTeam);

			if(combatTeam->teamOwner)
			{
				entGetPos(combatTeam->teamOwner, vOutPos);
				return;
			}

			if(combatTeam->roamingLeash && combatTeam->roamingLeashPointValid)
			{
				copyVec3(combatTeam->roamingLeashPoint, vOutPos);
				return;
			}

			copyVec3(combatTeam->spawnPos, vOutPos);
		}
	}
}

F32 aiGetCoherencyDist(Entity *e, AIConfig *config)
{
	if (!config)
		config = aiGetConfig(e, e->aibase);

	switch (aiGetLeashType(e->aibase))
	{
		xcase AI_LEASH_TYPE_RALLY_POSITION:
			return e->aibase->leashTypeOverride == AI_LEASH_TYPE_RALLY_POSITION ? e->aibase->coherencyCombatDistOverride : config->coherencyCombatHoldDist;
		xcase AI_LEASH_TYPE_OWNER:
		case AI_LEASH_TYPE_ENTITY:
			return config->coherencyCombatFollowDist;
		xdefault:
		acase AI_LEASH_TYPE_DEFAULT:
			return config->coherencyCombatHoldDist;
	}
}



F32 aiGetLeashingDistance(Entity *e, Entity* target, AIConfig *config, F32 powerRange)
{
	F32 fTargetLeashDistance;

	if(!config)
		config = aiGetConfig(e, e->aibase);

	// useCoherencyTargetRestriction is only valid if our leashType is not default 
	if(aiGetLeashType(e->aibase) != AI_LEASH_TYPE_DEFAULT && 
		config->useCoherencyTargetRestriction)
	{
		fTargetLeashDistance = aiGetCoherencyDist(e, config);
	}
	else 
	{
		// this might be a flag to either use the maxProtect distance on the character
		// or the team. Right now aiGetLeashDistance is only called inside aiTargetWithinLeash below
		fTargetLeashDistance = config->combatMaxProtectDist;
	}

	if(config->addPowersRangeToLeashDist)
	{
		fTargetLeashDistance += powerRange;
	}

	// if we are already targeting this guy, add some amount to the leash so we
	// don't lose them if they take a step back
	if(e->aibase->attackTarget == target)
	{
		fTargetLeashDistance += config->leashRangeCurrentTargetDistAdd;
	}

	if(target && (e->aibase->preferredTargetRef == entGetRef(target)))
	{
		fTargetLeashDistance += config->leashRangePreferredTargetDistAdd;
	}
	

	return fTargetLeashDistance;
}

int aiIsTargetWithinLeash(Entity *e, AIVarsBase *aib, Entity* target, F32 powerRange)
{
	AILeashType eLeashType = aiGetLeashType(aib);
	AITeam *team = aiTeamGetCombatTeam(e, aib);

	if(aiGlobalSettings.disableLeashingOnNonStaticMaps && 
		zmapInfoGetMapType(NULL)!=ZMTYPE_STATIC && 
		(team && !team->bLeashOnNonStaticOverride))
		return true;

	switch (eLeashType)
	{
		acase AI_LEASH_TYPE_RALLY_POSITION:
		{
			F32 fDistance = entGetDistance(NULL, aiGetRallyPosition(aib), target, NULL, NULL);

			return fDistance < aiGetLeashingDistance(e, target, NULL, powerRange);
		} 	
		xcase AI_LEASH_TYPE_ENTITY:
		case AI_LEASH_TYPE_OWNER:
		{
			F32 fDistance; 
			Entity* leashEnt;
			
			if (eLeashType == AI_LEASH_TYPE_ENTITY)
			{
				leashEnt = aib->erLeashEntity ? entFromEntityRef(entGetPartitionIdx(e), aib->erLeashEntity) : NULL;

			}
			else
			{
				leashEnt = e->erOwner ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;

			}

			if(! leashEnt)
			{	// if we don't have an entity to leash
				AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);
				AITeamMember* combatMember = aiTeamGetMember(e, aib, combatTeam);
				return aiTeamTargetWithinLeash(combatMember, combatTeam, target, NULL);
			}
			
			fDistance = entGetDistance(leashEnt, NULL, target, NULL, NULL);
			return fDistance < aiGetLeashingDistance(e, target, NULL, powerRange);
		} 
		
		xcase AI_LEASH_TYPE_DEFAULT:
		default:
		{
			// todo: possibly unfold this function or make sure to call aiGetLeashDistance from aiTeamTargetWithinLeash? 
			AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);
			AITeamMember* combatMember = aiTeamGetMember(e, aib, combatTeam);
			return aiTeamTargetWithinLeash(combatMember, combatTeam, target, NULL);
		}
	}
	
}

int aiIsPositionWithinCoherency(Entity *e, AIVarsBase *aib, const Vec3 vPos)
{
	Vec3 vLeashPos;
	F32 distSQR;
	F32 coherencyDist = aiGetCoherencyDist(e, NULL);
	
	aiGetLeashPosition(e, aib, vLeashPos);
	distSQR = distance3Squared(vLeashPos, vPos);
	return (distSQR < SQR(coherencyDist));
}


static void aiUpdateAttackTargetStatus(Entity* e, AIVarsBase* aib)
{
	AIConfig* config = NULL;
	AITeam* combatTeam = NULL;
	int partitionIdx = -1;
	PerfInfoGuard *guard;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	config = aiGetConfig(e, aib);
	combatTeam = aiTeamGetCombatTeam(e, aib);
	partitionIdx = entGetPartitionIdx(e);

	if(aib->attackTarget)
	{
		aib->attackTargetDistSQR = aib->attackTargetStatus->distanceFromMe *
			aib->attackTargetStatus->distanceFromMe;

		if(ABS_TIME_SINCE_PARTITION(partitionIdx, combatTeam->time.lastBothered) > SEC_TO_ABS_TIME(20))
			combatTeam->time.lastBothered = ABS_TIME_PARTITION(partitionIdx);
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

static void aiProcessProxEnts(Entity* e, AIVarsBase* aib)
{
	int i, n = eaSize(&aib->statusTable);
	Entity* newAttackTarget = NULL;
	AIStatusTableEntry* newAttackTargetStatus = NULL;
	Entity* confuseFallbackTarget = NULL;
	AIStatusTableEntry* confuseFallbackTargetStatus = NULL;
	F32 maxDanger = -FLT_MAX;
	AIConfig* config = NULL;
	int partitionIdx = -1;
	Entity* owner = NULL;
	F32 prefferedMaxRange = -1;
	EntityRef preferredTargetRef = 0;
	PerfInfoGuard *guard;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	config = aiGetConfig(e, aib);
	partitionIdx = entGetPartitionIdx(e);
	owner = entFromEntityRef(partitionIdx, e->erOwner);
	prefferedMaxRange = aiGetPreferredMaxRange(e, aib);

	preferredTargetRef = aiUpdatePreferredTarget(e, aib);
	if (!preferredTargetRef)
		preferredTargetRef = aiCombatRole_RequestPreferredTarget(e);
	
	if(e->pChar->bTauntActive && e->pChar->ppModsTaunt[0])
	{
		Entity* pTauntEnt;
		AttribMod *pTauntMod = e->pChar->ppModsTaunt[0];
		
		pTauntEnt = entFromEntityRef(partitionIdx, pTauntMod->erSource);
		
		// make sure we shouldn't ignore this attribMod due to having respawned
		if (pTauntEnt && !aiNotify_ShouldIgnoreApplyIDDueToRespawn(pTauntEnt, pTauntMod->uiApplyID))
		{
			aiSetAttackTarget(e, aib, pTauntEnt, NULL, false);

			PERFINFO_AUTO_STOP_GUARD(&guard);
			return;
		}
	}

	for(i = 0; i < n; i++)
	{
		AIStatusTableEntry* status = aib->statusTable[i];
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);
		Entity* statusE;
		bool bIsConfusedKOS = false;

		if (!teamStatus)
			continue;

		if(!teamStatus->legalTarget && !aib->confused && status->entRef != preferredTargetRef)
		{
			continue;
		}

		statusE = entFromEntityRef(partitionIdx, status->entRef);

		// TODO: this check should really be in aiAssignDangerValues, but I don't have character there
		if(!statusE || !aiIsEntAlive(statusE))
		{
			continue;
		}

		if(status->skipCurTick)
		{
			if (!confuseFallbackTarget && aib->confused && critter_IsKOSEx(partitionIdx, e, statusE, true))
			{	// we are confused and normally wouldn't attack this guy, 
				// but if there is no one valid to attack save this entity and target him as a last resort
				bIsConfusedKOS = true;
			}
			else
			{
				continue;
			}
		}

		if(entGetFlagBits(statusE) & ENTITYFLAG_UNTARGETABLE)
		{
			continue;
		}

		if(!aiIsTargetWithinLeash(e, aib, statusE, prefferedMaxRange))
		{
			continue;
		}

		if(statusE->aibase && statusE->aibase->untargetable)
		{
			continue; 
		}

		if(!aiEvalTargetingRequires(e, aib, config, statusE))
		{
			continue;
		}
		
		if(eaiFind(&e->pChar->perUntargetable, status->entRef) != -1)
		{
			continue;
		}

		if(!critter_IsKOS(partitionIdx, e, statusE))
		{
			if (bIsConfusedKOS)
			{	// we are confused and normally wouldn't attack this guy, 
				// but if there is no one valid to attack save this entity and target him as a last resort
				confuseFallbackTarget = statusE;
				confuseFallbackTargetStatus = status;
			}

			continue;
		}

		if(aib->confused && !aiIsValidConfusedTarget(aib, statusE))
		{
			continue; 
		}

		if(config->movementParams.immobile && !config->ignoreImmobileTargetRestriction)
		{	// if we are immobile, we need to check if our powers can reach the target
			F32 fRatingOut;
			U32 ratingOptions = AI_POWERS_RATE_IGNOREBONUSWEIGHT | AI_POWERS_RATE_COUNT_VALID_FAILS | AI_POWERS_RATE_USABLE;
			if(aib->powers->preferredAITagBit)
				ratingOptions |= AI_POWERS_RATE_PREFERRED;
			aiRatePowers(e, aib, statusE, SQR(status->distanceFromMe), ratingOptions, NULL, &fRatingOut);
			if (fRatingOut == 0.f)
			{
				continue;  // we have powers to use on this target
			}
		}

		if(status->entRef == preferredTargetRef && 
				(status->visible || status->eNotVisibleReason == EAINotVisibleReason_LOS) )
		{
			newAttackTarget = statusE;
			newAttackTargetStatus = status;
			break;
		}

		// The latter half checks if one is visible and the new one is not, switch targets to a lower aggro if:
		//     the time since the new target was visible is greater than the invisible attack target duration
		//     the last known position of the new target was visited
		if(status->totalBaseDangerVal > maxDanger ||
			(newAttackTargetStatus && status->visible && !newAttackTargetStatus->visible && 
				(ABS_TIME_SINCE_PARTITION(partitionIdx, newAttackTargetStatus->time.lastVisible)>SEC_TO_ABS_TIME(config->invisibleAttackTargetDuration) ||
				newAttackTargetStatus->visitedLastKnown)))
		{
			maxDanger = status->totalBaseDangerVal;
			newAttackTarget = statusE;
			newAttackTargetStatus = status;
		}
	}

	if (! newAttackTarget && confuseFallbackTarget)
	{
		newAttackTarget = confuseFallbackTarget;
		newAttackTargetStatus = confuseFallbackTargetStatus;
	}

	aiSetAttackTarget(e, aib, newAttackTarget, newAttackTargetStatus, false);

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

#define SEND_AGGRO_TIMER 1.0

static void aiUpdateEntityTracking(Entity* e, AIVarsBase* aib, AIConfig* config)
{
	int partitionIdx = -1;
	PerfInfoGuard *guard;
	
	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	partitionIdx = entGetPartitionIdx(e);

	aiExternUpdatePerceptionRadii(e, aib);
	if(aib->proximityRadius > 0)
	{
		aiUpdateProxEnts(e, aib);

		if(!config->dontAggroAtAll && aib->aggroRadius > 0 && (!entCheckFlag(e, ENTITYFLAG_UNTARGETABLE) || config->allowCombatWhileUntargetable))
		{
			bool bSendAggro = false;

			if (ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastSentAggroUpdate) > SEC_TO_ABS_TIME(SEND_AGGRO_TIMER))
			{
				bSendAggro = true;
				aib->time.lastSentAggroUpdate = ABS_TIME_PARTITION(partitionIdx);
			}

			aiUpdateStatusList(e, aib, bSendAggro);
			aiAssignDangerValues(e, aib, aib->baseDangerFactor, bSendAggro);
			aiProcessProxEnts(e, aib);
			aiUpdateAttackTargetStatus(e, aib);
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

// Returns the preferred target entity ref
// If the aiConfig enables target assisting, a preferred target is a friendly it will return their current target
static EntityRef aiUpdatePreferredTarget(Entity* e, AIVarsBase* aib) 
{
	if (aib->preferredTargetRef)
	{
		Entity *preferredTarget = entFromEntityRef(entGetPartitionIdx(e), aib->preferredTargetRef);

		if (!preferredTarget)
		{	// entity does not exist
			aiClearPreferredAttackTarget(e, aib);
			return 0;
		}

		if (aib->preferredTargetIsEnemy)
		{
			if (!aiIsEntAlive(preferredTarget))
			{
				aiClearPreferredAttackTarget(e, aib);
			}

			return aib->preferredTargetRef;
		}
		else
		{	// if we are targeting a friendly, don't clear the preferredTargetRef if they're dead
			if (!aiIsEntAlive(preferredTarget))
				return 0;

			return entity_GetTargetRef(preferredTarget);
		}
	}

	return 0;
}

void aiClearPreferredAttackTarget(Entity *e, AIVarsBase *aib)
{
	if(aib->preferredTargetRef)
	{
		aib->preferredTargetRef = 0;
		if (g_fpPrefferedTargetClearedCallback)
			g_fpPrefferedTargetClearedCallback(e);
	}
}

int aiSetPreferredAttackTarget(Entity *e, AIVarsBase *aib, Entity *target, int bAttackTarget)
{
	bool isTargetEnemy;
	if(!target || e == target || !aiIsEntAlive(e) )
	{
		aib->preferredTargetRef = 0;
		return false;
	}
	

	isTargetEnemy = !!critter_IsKOSEx(entGetPartitionIdx(e), e, target, true);

	if(!isTargetEnemy)
	{
		AIConfig *pConfig = aiGetConfig(e, aib);
		if (pConfig->dontAllowFriendlyPreferredTargets)
		{
			aib->preferredTargetRef = 0;
			return false; 
		}
	}

	// Force create the status so it will attack it even if it is a destructible
	aiStatusFind(e, aib, target, 1);	

	aib->preferredTargetRef = entGetRef(target);
	aib->preferredTargetIsEnemy = isTargetEnemy;

	if(isTargetEnemy && bAttackTarget)
	{
		F32 prefferedMaxRange = aiGetPreferredMaxRange(e, aib);

		// if the target is within our leash distance, set the attack target
		if (aiIsTargetWithinLeash(e, aib, target, prefferedMaxRange))
		{
			AITeam* combatTeam;
			aiSetAttackTarget(e, aib, target, NULL, true);

			// Do this AFTER set attack target, because otherwise we might not have the aib->combatTeam, which is preferable
			combatTeam = aiTeamGetCombatTeam(e, aib);

			if (combatTeam->combatState != AITEAM_COMBAT_STATE_FIGHT)
			{
				// force into the combat state
				aib->inCombat = true;
				combatTeam->memberInCombat = true;
				aiTeamEnterCombat(combatTeam);
			}
		}
	}

	return true;
}

int g_AIDebugAnims = 0;
AUTO_CMD_INT(g_AIDebugAnims, aiDebugAnims);

int g_AIDebugAnimsDisable = 0;
AUTO_CMD_INT(g_AIDebugAnimsDisable, aiDebugAnimsDisable);

int aiDebugAnimations(Entity* e, AIVarsBase *aib)
{
	int partitionIdx = entGetPartitionIdx(e);
	if(g_AIDebugAnimsDisable)
		return false;

	if(ABS_TIME_PARTITION(partitionIdx) < SEC_TO_ABS_TIME(60))
		return false;

	if (isDevelopmentMode())
	{
		struct tm t;
		time_t tt = time(NULL);
		localtime_s(&t, &tt);

		if (g_AIDebugAnims ||
			isAprilFools() &&
			t.tm_min == 0)
		{
			if(t.tm_hour != aib->time.last_hour &&
				(U32)(t.tm_sec & 0xF) == (entGetRef(e) & 0xF))
			{
				aib->time.last_hour = t.tm_hour;
				return true;
			}
		} 
		else 
		{
			return false;
		}
	}

	return false;
}

int aiDebugAnimationsTimeout(AIVarsBase *aib)
{
	if (isDevelopmentMode())
	{
		struct tm t;
		time_t tt = time(NULL);
		localtime_s(&t, &tt);
		return g_AIDebugAnimsDisable || (!g_AIDebugAnims && t.tm_min > 10);
	}
	
	return false;
}

typedef struct NameAndDesc
{
	const char* msgKeyName;
	const char* msgKeyDesc;
} NameAndDesc;

NameAndDesc **eaFormerF = NULL;
NameAndDesc **eaFormerM = NULL;

void aiBuildRandomNameList(const char* baseStr, NameAndDesc ***eaOut)
{
	int i;
	int misses;
	Message* msgName = NULL;
	Message* msgDesc = NULL;
	char* estr = NULL;
	NameAndDesc *nand = NULL;

	for(i=0, misses = 0; misses < 5; i++)
	{
		estrPrintf(&estr, "%s.%d", baseStr, i);

		msgName = RefSystem_ReferentFromString("Message", estr);

		if(!msgName)
		{
			misses++;
			continue;
		}

		estrPrintf(&estr, "%s.%d.Desc", baseStr, i);

		msgDesc = RefSystem_ReferentFromString("Message", estr);

		if(!msgDesc)
		{
			misses++;
			continue;
		}

		nand = callocStruct(NameAndDesc);
		nand->msgKeyDesc = msgDesc->pcMessageKey;
		nand->msgKeyName = msgName->pcMessageKey;
		eaPush(eaOut, nand);
	}

	estrDestroy(&estr);
}

void aiGetRandomNameInit()
{
	static int once = 0;

	if(!once)
	{
		once = true;

		aiBuildRandomNameList("Former.Male", &eaFormerM);
		aiBuildRandomNameList("Former.Female", &eaFormerF);
	}
}

void aiGetRandomName(Message **msgNameOut, Message **msgDescOut, S32 female)
{
	NameAndDesc *nand = NULL;
	aiGetRandomNameInit();

	*msgDescOut = NULL;
	*msgNameOut = NULL;

	if(female)
		nand = eaRandChoice(&eaFormerF);
	else
		nand = eaRandChoice(&eaFormerM);

	if(!nand)
		return;

	*msgNameOut = RefSystem_ReferentFromString("Message", nand->msgKeyName);
	*msgDescOut = RefSystem_ReferentFromString("Message", nand->msgKeyDesc);
}

S32 aiThinkTickDebugAllowed(Entity *e, AIVarsBase *aib)
{
	if(!e->pCritter)
		return false;
	if(e->pProjectile)
		return false;
	if(entCheckFlag(e, ENTITYFLAG_UNTARGETABLE) || entCheckFlag(e, ENTITYFLAG_UNSELECTABLE))
		return false;
	return true;
}

void aiThinkTickDebug(Entity *e, AIVarsBase *aib, S64 time)
{
	if(aib->thinkTickDebugged == 0 && isAprilFools())
	{
		aib->thinkTickDebugged = 1;
		if(aiThinkTickDebugAllowed(e, aib))
		{
			Message *msgName = NULL;
			Message *msgDesc = NULL;
			S32 female = Gender_Female == costumeEntity_GetEffectiveCostumeGender(e);

			aiGetRandomName(&msgName, &msgDesc, female);

			if(msgName || msgDesc)
				critter_OverrideDisplayMessage(e, msgName, msgDesc);
		}
	}
}

AIAnimList* aiDebugAnimationGet(void)
{
	static int filteredOnce = 0;
	static const char** lists = NULL;
	int i;

	if(!filteredOnce)
	{
		RefDictIterator iter;
		AIAnimList *al = NULL;

		RefSystem_InitRefDictIterator(g_AnimListDict, &iter);

		while(al = RefSystem_GetNextReferentFromIterator(&iter))
		{
			for(i=0; i<eaSize(&al->bits); i++)
			{
				const char* bit = al->bits[i];
				if(!stricmp(bit, "DANCE"))
				{
					eaPush(&lists, al->name);
					break;
				}
			}
		}

		filteredOnce = true;
	}

	for(i=0; i<5; i++)
	{
		const char* name = eaRandChoice(&lists);
		AIAnimList *al = RefSystem_ReferentFromString(g_AnimListDict, name);

		if(al)
			return al;
	}

	return NULL;
}

void aiSetPreferredAttackTargetClearedCallback(AIPreferredTargetClearedCallback callback)
{
	g_fpPrefferedTargetClearedCallback = callback;
}

// adds an entity to the critter's current seek target list. These targets will get an overridden aggro radius applied to them
void aiAddSeekTarget(Entity *e, Entity *target)
{
	eaiPush(&e->aibase->eaSeekTargets, entGetRef(target));
}

void aiRemoveSeekTarget(Entity *e, EntityRef erTarget)
{
	eaiRemoveFast(&e->aibase->eaSeekTargets, erTarget);
}


void aiSetAttackTarget(Entity* e, AIVarsBase* aib, Entity* newAttackTarget, AIStatusTableEntry* newAttackTargetStatus, int forceUpdate)
{
	AIConfig* config = aiGetConfig(e, aib);
	int partitionIdx = entGetPartitionIdx(e);

	PERFINFO_AUTO_START_FUNC();

	if(newAttackTarget){
		assert(!aib->destroying);
	}

	if(!forceUpdate && config->dontChangeAttackTarget && newAttackTarget)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(aib->attackTarget==newAttackTarget)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// Wait until your powers are done if you have a new target
	if(!forceUpdate && newAttackTarget && e->pChar && eaSize(&aib->powers->queuedPowers) > 0)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	
	if(forceUpdate)
	{	// if forced update, stop all attack powers on the current target
		aiStopAttackPowersOnTarget(e, aib->attackTarget);
	}

	if(aib->attackTarget && aib->attackTarget != newAttackTarget)
	{
		if(eaFindAndRemoveFast(&aib->attackTarget->aibase->attackerListMutable, e) < 0){
			FatalErrorf("Entity 0x%p missing from Entity 0x%p's attackerList.",
						e,
						aib->attackTarget);
		}
	}

	if(newAttackTarget && !newAttackTarget->pChar)
		newAttackTarget = NULL;  // Setting to a non-combat attack target will just clear the target

	if (aib->attackTarget)
	{
		// check if our current movement target & rotation targets are our old attack target,
		// if they are clear them. 
		if (aib->attackTarget->myRef == aiMovementGetMovementTargetEnt(e, aib))
		{
			aiMovementClearMovementTarget(e, e->aibase);
		}
		if (aib->attackTarget->myRef == aiMovementGetRotationTargetEnt(e, aib))
		{
			aiMovementClearRotationTarget(e, e->aibase);
		}
	}

	if(!newAttackTarget && aib->attackTarget)
	{
		aiConfigModRemoveAllMatching(e, e->aibase, "DontChangeAttackTarget", "1");
		if(eaFind(&aib->attackTarget->aibase->attackerList, e) >= 0)
		{
			FatalErrorf("Entity 0x%p still in Entity 0x%p's attackerList.",
						e,
						aib->attackTarget);
		}
		aib->attackTargetMutable = NULL;
		aib->attackTargetStatus = NULL;
		aib->attackTargetStatusOld = NULL;
		aib->attackTargetRef = 0;
		entity_SetTarget(e, 0);
		aiCombatSetFaceTarget(e, 0);
	}
	else if(aib->attackTarget != newAttackTarget)
	{
		AITeam* targetCombatTeam;
		AITeamStatusEntry *attackTargetTeamStatus = NULL;

		AI_DEBUG_PRINT_TAG(e, AI_LOG_COMBAT, 2, AICLT_ATTACKTARGET, "%s: Added as attack target", ENTDEBUGNAME(newAttackTarget));

		if(!newAttackTargetStatus)
			newAttackTargetStatus = aiStatusFind(e, aib, newAttackTarget, true);
		
		// explicitly setting something as attacktarget means you've "seen" it anyway
		newAttackTargetStatus->time.lastVisible = ABS_TIME_PARTITION(partitionIdx);

		if(	aib->attackTarget &&
			eaFind(&aib->attackTarget->aibase->attackerList, e) >= 0)
		{
			FatalErrorf("Entity 0x%p still in Entity 0x%p's attackerList.",
						e,
						aib->attackTarget);
		}

		aib->attackTargetMutable = newAttackTarget;
		aib->attackTargetStatus = newAttackTargetStatus;
		aib->attackTargetStatusOld = newAttackTargetStatus;
		aib->attackTargetRef = newAttackTargetStatus->entRef;

		aib->attackTargetStatus->time.becameAttackTarget = ABS_TIME_PARTITION(partitionIdx);

		entity_SetTarget(e, newAttackTargetStatus->entRef);
	
		aiCombatSetFaceTarget(e, aib->attackTargetRef);
		
		if(eaFind(&aib->attackTarget->aibase->attackerList, e) >= 0){
			FatalErrorf("Entity 0x%p already in Entity 0x%p's attackerList.",
						e,
						aib->attackTarget);
		}

		eaPush(&aib->attackTarget->aibase->attackerListMutable, e);

		targetCombatTeam = aiTeamGetCombatTeam(aib->attackTarget, aib->attackTarget->aibase);
		if(targetCombatTeam)
		{
			if(targetCombatTeam->config.addLegalTargetWhenTargeted && 
				(!config->controlledPet || targetCombatTeam->combatState!=AITEAM_COMBAT_STATE_FIGHT))
				aiAddLegalTarget(aib->attackTarget, aib->attackTarget->aibase, e);
		}

		attackTargetTeamStatus = aiGetTeamStatus(e, aib, aib->attackTargetStatus);
		if(attackTargetTeamStatus && !attackTargetTeamStatus->legalTarget)
		{
			attackTargetTeamStatus->legalTarget = true;
			aiAddLegalTarget(e, aib, aib->attackTarget);
		}
	}

	PERFINFO_AUTO_STOP();
}

// sets a target location usually set through the power's entCreation that will come from player targeting locations. 
// FSMs have an expression to get this location
void aiSetPowersEntCreateTargetLocation(Entity *e, const Vec3 vLoc)
{
	if (e && e->aibase)
	{
		copyVec3(vLoc, e->aibase->vecTargetPowersEntCreate);
	}
}

/*
static void aiSetBothered(Entity* e, AIVarsBase* aib, int on, const char* reason)
{
	if(!!on == aib->bothered)
		return;

	aib->bothered = !!on;

	// this should get handled by setting attack targets I would think
	//if(ABS_TIME_SINCE_PARTITION(partitionIdx, aib->team->time.lastBothered) > SEC_TO_ABS_TIME(20))
		//aib->team->time.lastBothered = ABS_TIME_PARTITION(partitionIdx);

	//AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 5, "Switched to being %sbothered %s%s%s",
		//aib->bothered ? "" : "not ", aib->bothered ? "(" : "", reason, aib->bothered ? ")" : "");

	//if(aiEnableAggroAvgBotheredModel && !aib->team->bothered)
		//aiSetAttackTarget(e, aib, NULL, NULL, false);
}
*/

static int aiQuickSleepHack = true;
static F32 aiQuickSleepHackRadius = 300;

void aiDisableSleep(int disable)
{
	aiQuickSleepHack = !disable;
}

AUTO_CMD_INT(aiQuickSleepHack, aiQuickSleepHack);
AUTO_CMD_FLOAT(aiQuickSleepHackRadius, aiQuickSleepHackRadius);

static void aiTickExecuteFSM(Entity *e, AIVarsBase *aib)
{
	if (aib->job && aib->encounterJobFSMDone)
	{
		eaFindAndRemoveFast(&aib->team->jobs,aib->job);
		aiJobDestroy(aib->job);
		aib->encounterJobFSMDone = 0;
	}
	if(aib->job)
	{
		PERFINFO_AUTO_START("Job FSM", 1);
		fsmExecute(aib->job->fsmContext, aib->job->exprContext);
		PERFINFO_AUTO_STOP();
	}
	else if(aib->doBScript)
	{
		FSMContext* pFSMContext = aiGetCurrentBaseFSMContext(e);
		if (pFSMContext)
		{
			const char* stateBeforeExecute = fsmGetState(pFSMContext);
			PERFINFO_AUTO_START("Non-job FSM", 1);
			if(fsmExecuteEx(pFSMContext, aib->exprContext, 1, !aib->skipOnEntry) && stateChangeCallback)
			{
				PERFINFO_AUTO_START("stateChangeCallback", 1);
				stateChangeCallback(e, stateBeforeExecute, fsmGetState(pFSMContext));
				PERFINFO_AUTO_STOP();
			}
			aib->skipOnEntry = 0;

			// set aib back for entity to its base fsmcontext. This is being done due to the fact if the override is set into the context and then freed then 
			// aibase->exprContext->fsmContext will be pointing to freed memory. Also it appears from reading the code the e->aibase->exprContext->fsmContext == e->aibase->fsmContext
			if(aib == e->aibase && e->aibase && e->aibase->exprContext &&
				exprContextGetFSMContext(e->aibase->exprContext) != e->aibase->fsmContext)
			{
				// our base expression context has been changed. put it back to the correct one
				exprContextSetFSMContext(e->aibase->exprContext, e->aibase->fsmContext);
			}

			PERFINFO_AUTO_STOP();
		}
	}
}


static void aiTickExecuteBehavior(Entity *e, AIVarsBase *aib)
{
	PERFINFO_AUTO_START_FUNC();
	if (!aiMultiTickAction_HasForcedActionQueued(e, aib))
	{
		aiTickExecuteFSM(e, aib);
	}
	else
	{
		aiMultiTickAction_ProcessActions(e, aib);
	}
	PERFINFO_AUTO_STOP();
}

#define aiCanSleep(e, ai) (ABS_TIME_SINCE_PARTITION(partitionIdx, ai->time.lastDamage[AI_NOTIFY_TYPE_DAMAGE]) > SEC_TO_ABS_TIME(3) && \
	   aiTeamGetCombatTeam(e, ai)->combatState == AITEAM_COMBAT_STATE_AMBIENT)

void aiCheckOfftickActions(Entity* e, AIVarsBase* aib, AIConfig* baseConfig)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&baseConfig->offtickActions)-1; i>=0; i--)
	{
		AIOfftickInstance *inst = NULL;
		AIOfftickConfig *otc = baseConfig->offtickActions[i];
		MultiVal answer = {0};
		AITeam* combatTeam = aiTeamGetCombatTeam(e, aib);

		PERFINFO_AUTO_START("OfftickAction-check", 1);

		if(!stashAddressFindPointer(aib->offtickInstances, otc, &inst))
		{
			inst = calloc(1, sizeof(AIOfftickInstance));
			inst->otc = otc;
			stashAddressAddPointer(aib->offtickInstances, otc, inst, 1);
		}

		inst->executedThisTick = 0;
		if(otc->initialize && !inst->initialized)
		{
			inst->initialized = 1;
			exprEvaluate(otc->initialize, aib->exprContext, &answer);
		}

		PERFINFO_AUTO_STOP();

		if(otc->maxCount && inst->executed>=otc->maxCount)
			continue;

		if(otc->maxPerCombat && inst->executedThisCombat>=otc->maxPerCombat)
			continue;

		if(otc->combatOnly && !aiTeamInCombat(combatTeam))
			continue;

		PERFINFO_AUTO_START("OfftickAction-exec", 1);
		exprEvaluate(otc->coarseCheck, aib->exprContext, &answer);
		PERFINFO_AUTO_STOP();

		if(answer.type==MULTI_INT && answer.int32)
		{
			inst->activeFine = 1;
		}
		else
		{
			inst->activeFine = 0;
		}
	}

	PERFINFO_AUTO_STOP();
}

static int aiCheckSleep(Entity* e, AIVarsBase* aib)
{
	int i;
	int foundPlayer = false;
	Vec3 myPos;
	int partitionIdx = entGetPartitionIdx(e);

	PERFINFO_AUTO_START_FUNC();

	aiUpdateProxEnts(e,aib);

	for(i = aib->proxEntsCount-1; i >= 0; i--)
	{
		if(aib->proxEnts[i].e->pPlayer)
		{
			foundPlayer = true;
			break;
		}
	}

	entGetPos(e, myPos);
	if(!foundPlayer && !cutscene_GetNearbyCutscenes(partitionIdx, myPos, aiQuickSleepHackRadius))
	{
		static Entity** sleepCheckEnts = NULL;
		entGridProximityLookupExEArray(partitionIdx, myPos, &sleepCheckEnts, aiQuickSleepHackRadius,
			ENTITYFLAG_IS_PLAYER, 0, e);

		// Go to sleep if there are no nearby players or cutscenes
		if(!eaSize(&sleepCheckEnts))
		{
			//aiMovementResetPath(e, aib);
			aib->sleeping = 1;
			aiMovementSetSleeping(e, aib, true);
			aib->time.startedSleeping = ABS_TIME_PARTITION(partitionIdx);
			PERFINFO_AUTO_STOP();// "Sleep check"
			return true;
		}
	}
	PERFINFO_AUTO_STOP();
	return false;
}

static __forceinline void aiCalculateSpawnAndOffset(Entity *e, AIVarsBase *aib)
{
	AITeam *team = aiTeamGetAmbientTeam(e, aib);
	Entity *owner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);

	if(!aib->determinedSpawnPoint)
	{
		aib->determinedSpawnPoint = 1;

		entGetPos(e, aib->spawnPos);
		copyVec3(aib->spawnPos, aib->ambientPosition);
		entGetRot(e, aib->spawnRot);
	}

	if(!aib->calculatedSpawnOffset)
	{
		aib->calculatedSpawnOffset = 1;

		if(owner)
		{
			Vec3 pos;
			Quat rot;
			Vec3 pyFace;
			entGetPos(owner, pos);
			entGetFacePY(owner, pyFace);

			subVec3(aib->spawnPos, pos, pos);
			yawQuat(pyFace[1], rot);
			quatRotateVec3(rot, pos, aib->spawnOffset);
		}
		else if(e->pCritter)
		{
			// the initial post-creation forced think tick may choose the combat team for aiTeamUpdate
			// when that happens, team->spawnPos below is 0,0,0 (since this function only operates on the ambient team)
			// if we use 0,0,0 then the spawnOffset will be in world space, and we can get crashes during aiMovementGoToSpawnPos (you'll see things try to move to 2x their leash coordinates)
			if (!team->calculatedSpawnPos) {
				aiTeamCalculateSpawnPos(team);
			}

			subVec3(aib->spawnPos, team->spawnPos, aib->spawnOffset);
		}
	}
}

static void aiCheckAmbientTethering(Entity *e, AIVarsBase *aib, AITeam *team, AIConfig *config)
{
	PERFINFO_AUTO_START_FUNC();

	if(entIsPlayer(e))
	{
		// At this time we do not want player entities tethering as on client closing (or crashing) the entity would move towards last map transition point
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!s_bDisableAmbientTethering && 
		!aib->disableAmbientTether &&
		config->tetherToAmbientPosition && 
		team->combatState == AITEAM_COMBAT_STATE_AMBIENT && 
		!config->movementParams.continuousCombatMovement)
	{
		AIMovementOrderType movOrder = aiMovementGetMovementOrderType(e, aib);
		if (movOrder == AI_MOVEMENT_ORDER_NONE && !aib->currentlyMoving)
		{
			Vec3 vCurPos;
			entGetPos(e, vCurPos);
			if (distance3Squared(vCurPos, aib->ambientPosition) > SQR(2.f))
			{
				AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Tethering back to ambient position." )
				aiMovementSetTargetPosition(e, aib, aib->ambientPosition, NULL, 0);
				aiMovementSetFinalFaceRot(e, aib, aib->spawnRot);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

void aiForceThinkTick(Entity* e, AIVarsBase* aib)
{
	if (!aib->forceThinkTick)
	{
		AIPartitionState *partition = aiPartitionStateGet(entGetPartitionIdx(e), true);
		aiRemoveFromBucket(e, aib);
		stashIntAddInt(partition->priorityEntBucket.entsByRef, entGetRef(e), 1, true);
		aib->forceThinkTick = 1;
	}
}

int g_AITickOncePerFrame = 0;
AUTO_CMD_INT(g_AITickOncePerFrame, aiThinkOncePerFrame);

S32 aiIsEnabled(Entity *e, AIVarsBase *aib)
{
	int partitionIdx = 0;
	AIPartitionState *partition = NULL;

	if(!aib)
		return 0;

	if(aib->disableAI)
		return 0;

	partitionIdx = entGetPartitionIdx(e);
	partition = aiPartitionStateGet(partitionIdx, false);

	// If there is a cutscene playing, check if the entity is frozen
	if(partition && partition->bCheckForFrozenEnts && s_isEntFrozenCB && s_isEntFrozenCB(e))
		return 0;

	return 1;
}

void aiFirstTickInit(Entity *e, AIVarsBase *aib)
{
	int partitionIdx = entGetPartitionIdx(e);

	Vec3 myPos;

	if(e->pChar)
		e->aibase->isSummonedAndExpires = character_DoesCharacterExpire(e->pChar);

	aib->time.timeSpawned = ABS_TIME_PARTITION(partitionIdx);

	entGetPos(e, myPos);
	vecY(myPos) += 0.1;  // False positive fixing

	if(aib->powers->hasFlightPowers && !aiMovementGetFlying(e, aib))
	{
		F32 groundDist = aiFindGroundDistance(worldGetActiveColl(partitionIdx), myPos, NULL);
		if (groundDist < 0.f || groundDist > 10.f)
			aiMovementFly(e, aib, 1);
	}
}

void aiPostFirstTickInit(Entity *e, AIVarsBase *aib)
{
	if(!aib->spawnBeacon && !aib->checkedSpawnBeacon)
	{
		Vec3 myPos;
		int partitionIdx = entGetPartitionIdx(e);
		AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);
		AIConfig *config = aiGetConfig(e, aib);
		AITeam *ambientTeam = aiTeamGetAmbientTeam(e, aib);

		aib->checkedSpawnBeacon = 1;

		entGetPos(e, myPos);

		if(!config->movementParams.immobile)
		{
			PERFINFO_AUTO_START("Find SpawnBeacon", 1);

			beaconSetPathFindEntity(entGetRef(e), 0, entGetPartitionIdx(e));
			aib->spawnBeacon = beaconGetClosestCombatBeacon(partitionIdx, myPos, NULL, 1, NULL, GCCB_PREFER_LOS, NULL);

			if(!aib->spawnBeacon)
			{
				aib->spawnBeacon = beaconGetClosestCombatBeacon(partitionIdx, myPos, NULL, 1, NULL, GCCB_IGNORE_LOS, NULL);

				if(aib->spawnBeacon)
				{
					entSetPos(e, aib->spawnBeacon->pos, 1, "Couldn't find beacon");
					aiTeamCalculateSpawnPos(ambientTeam);
				}
			}
			//BKH : This _might_ be something that we should add, although I'm guessing when this is the case it's already been setup as the team spawn pos
			//else
			//{
			//	entSetPos(e, aib->spawnBeacon->pos, 1, "AI post-1st tick initialization");
			//	aiTeamCalculateSpawnPos(ambientTeam);
			//}

			PERFINFO_AUTO_STOP();
		}
	}
}

void aiExecuteNextTickCmdQueue(AIVarsBase *aib)
{
	PERFINFO_AUTO_START("nextTickCmdQueue", 1);
	if(aib->nextTickCmdQueue)
	{
		CommandQueue_ExecuteAllCommands(aib->nextTickCmdQueue);
		// the execute will clear the queue, but destroy it so that we don't try to execute 
		// this multiple times, it is used infrequently enough to not need to keep this around.
		CommandQueue_Destroy(aib->nextTickCmdQueue);
		aib->nextTickCmdQueue = NULL;
	}
	PERFINFO_AUTO_STOP();
}

void aiPerFrameUpdate(Entity *e, AIVarsBase *aib)
{
	AILeashType eLeashType = -1;
	AITeam *combatTeam = NULL;
	AITeam *ambientTeam = NULL;
	int partitionIdx = entGetPartitionIdx(e);
	AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);
	AIConfig *config = NULL;

	PERFINFO_AUTO_START_FUNC();
	
	eLeashType = aiGetLeashType(aib);
	ambientTeam = aiTeamGetAmbientTeam(e, aib);
	combatTeam = aiTeamGetCombatTeam(e, aib);

	aiConfigLocalCleanup(aib);
	config = aiGetConfig(e, aib);

	if(aiGlobalSettings.enableSocialAggroPulse && 
		ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastSocialAggroPulse) > SEC_TO_ABS_TIME(2) &&
		aib->combatTeam && 
		!aib->combatTeam->bIgnoreSocialAggroPulse && 
		(aib->combatTeam->combatState == AITEAM_COMBAT_STATE_FIGHT))
	{
		aiCombatDoSocialAggroPulse(e, aib, false);	
	}

	if (eLeashType == AI_LEASH_TYPE_ENTITY)
	{
		// check to see if the entity still exists and if it is still alive
		Entity *eLeashEnt = (aib->erLeashEntity) ? entFromEntityRef(partitionIdx, aib->erLeashEntity) : NULL;
		if (!eLeashEnt || !entIsAlive(eLeashEnt))
		{
			aib->erLeashEntity = 0;
			aib->leashType = AI_LEASH_TYPE_DEFAULT;
		}
	}

	PERFINFO_AUTO_STOP_START("aiTick3", 1);
		aiExecuteNextTickCmdQueue(aib);

		PERFINFO_AUTO_START("aiAvoidUpdate", 1);
		aiAvoidUpdate(e, aib);
		PERFINFO_AUTO_STOP();

		aiCheckQueuedPowers(e, aib);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

void aiTriggeredChecks(Entity *e, AIVarsBase *aib)
{
	if(aib->spawnOffsetDirtied)
		aiMovementUpdateSpawnOffset(e, aib);

	if(aib->combatRolesDirty)
	{
		aiCombatRole_SetCombatRole(e, aib, aib->pchCombatRole, NULL);
		aib->combatRolesDirty = false; // only let it try and update once
	}
}

void aiTick(Entity* e, AIVarsBase* aib)
{
	S64 timePassed;
	int partitionIdx = -1;
	AIPartitionState *partition = NULL;
	F32 tickRate;
	AITeam* ambientTeam = NULL;
	AITeam* combatTeam = NULL;
	PerfInfoGuard *guard;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	partitionIdx = entGetPartitionIdx(e);
	partition = aiPartitionStateGet(partitionIdx, false);

#if 0
	if(aiDebugAnimations(e, aib))
	{
		AIAnimList *al = aiDebugAnimationGet();
		if(al)
			aiAnimListSetHold(e, al);
	}
	else if(aiDebugAnimationsTimeout(aib))
		aiAnimListClearHold(e);
#endif

	if(!aiIsEnabled(e, aib))
	{
		PERFINFO_AUTO_STOP_GUARD(&guard);
		return;
	}

	PERFINFO_AUTO_START("aiTick1", 1);

	aiTriggeredChecks(e, aib);

	timePassed = ABS_TIME_SINCE_PARTITION(partitionIdx, aib->lastThinkTick);
	// when a cutscene rewinds time, this ends up negative for any actors that were in it
	timePassed = CLAMP(timePassed, 0, SEC_TO_ABS_TIME(AI_TICK_RATE));

	aib->lastThinkTick = ABS_TIME_PARTITION(partitionIdx);

	PERFINFO_AUTO_STOP();

	if(ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.startedSleeping) < SEC_TO_ABS_TIME(3) && aiCanSleep(e, aib))
	{
		PERFINFO_AUTO_STOP_GUARD(&guard);
		return;
	}

	// pause the AI until a certain time, currently only used for spawn animations
	if(aib->time.pauseUntil > ABS_TIME_PARTITION(partitionIdx))
	{
		PERFINFO_AUTO_STOP_GUARD(&guard);
		return;
	}

	PERFINFO_AUTO_START("aiTick2", 1);

	aiPerFrameUpdate(e, aib);

	// only accumulate for a think tick after you know this is actually a tick you're active
	aib->thinkAcc += timePassed;

	if(g_AITickOncePerFrame)
		aib->forceThinkTick = 1;

	tickRate = AI_TICK_TIME(e);

	if(aib->fsmContext->messageRecvd)
		tickRate = AI_TICK_RATE;

	PERFINFO_AUTO_STOP();

	if(aib->forceThinkTick || aib->thinkAcc >= SEC_TO_ABS_TIME(tickRate))
	{
		AIConfig *baseConfig;
		AIConfig *config;
		PerfInfoGuard *thinkGuard;

		PERFINFO_AUTO_START_GUARD("thinkTick", 1, &thinkGuard);

		AI_DEBUG_PRINT(e, AI_LOG_TRACE, 6, "Tick");
		
		ambientTeam = aiTeamGetAmbientTeam(e, aib);
		combatTeam = aiTeamGetCombatTeam(e, aib);
		config = aiGetConfig(e, aib);

		aib->thinkAcc = 0;
		aib->forceThinkTick = 0;
		aib->fsmContext->messageRecvd = 0;

		// Note that if not in combat, ambientTeam==combatTeam
		if(combatTeam->time.lastTick != ABS_TIME_PARTITION(partitionIdx))
			aiTeamUpdate(combatTeam);

		aiThinkTickDebug(e, aib, ABS_TIME_PARTITION(partitionIdx));
		
		combatTeam = aiTeamGetCombatTeam(e, aib);  // aiTeamUpdate can destroy the combat team

		aiCalculateSpawnAndOffset(e, aib);

		aiMovementModeManager_Update(aib->movementModeManager);

		aiCheckAmbientTethering(e, aib, ambientTeam, config);

		// reset the uiRespawnApplyID, which guards against getting aggro for powers you applied before you die
		// we want to reset it just in case the player died soon before the powerApplyID wraps back to 1 
		if(aib->lastRespawnTime && ABS_TIME_SINCE_PARTITION(partitionIdx, aib->lastRespawnTime) > SEC_TO_ABS_TIME(20.f))
		{	
			aib->lastRespawnTime = 0;
			aib->uiRespawnApplyID = 0;
		}

		// track when we were last done moving
		if (aib->currentlyMoving)
		{
			aib->time.timeStoppedCurrentlyMoving = 0;
		}
		else if (!aib->time.timeStoppedCurrentlyMoving)
		{
			aib->time.timeStoppedCurrentlyMoving = ABS_TIME_PARTITION(partitionIdx);
		}

		if(aib->doProximity)
		{
			aiUpdateEntityTracking(e, aib, config);

			if(aib->hadFirstTick && aiQuickSleepHack && !aib->dontSleep && aiCanSleep(e, aib))
			{
				if(aiCheckSleep(e, aib))
				{
					PERFINFO_AUTO_STOP_GUARD(&thinkGuard);
					PERFINFO_AUTO_STOP_GUARD(&guard);
					return;
				}
			}

			aib->hadFirstTick = 1;
			aib->sleeping = 0;
			aiMovementSetSleeping(e, aib, false);

			baseConfig = GET_REF(aib->config_use_accessor);
			if(baseConfig && eaSize(&baseConfig->offtickActions))
				aiCheckOfftickActions(e, aib, baseConfig);

			aiTickExecuteBehavior(e, aib);
		}
		else if (aib->isHostileToCivilians) 
		{	// if not doProximity, if we are hostile to civilians, pulse a scare

			// note that civilian scaring is normally done within the if(aib->doProximity) block above, 
			// inside aiUpdateEntityTracking(). 
						
			F32 fScareDist;
			fScareDist = (combatTeam->combatState != AITEAM_COMBAT_STATE_FIGHT) ?	
								config->pedestrianScareDistance : config->pedestrianScareDistanceInCombat;	
			aiCivScareNearbyPedestrians(e, fScareDist);
		}

		PERFINFO_AUTO_STOP_GUARD(&thinkGuard);
	}

	if(stashGetCount(aib->offtickInstances))
	{
		StashTableIterator iter;
		StashElement elem;

		PERFINFO_AUTO_START("OfftickInstances",1);

		ambientTeam = aiTeamGetAmbientTeam(e, aib);
		combatTeam = aiTeamGetCombatTeam(e, aib);

		stashGetIterator(aib->offtickInstances, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			AIOfftickInstance *inst = stashElementGetPointer(elem);
			AIOfftickConfig *otc = inst->otc;
			MultiVal answer = {0};

			if(otc->combatOnly && !aiTeamInCombat(combatTeam))
				inst->activeFine = 0;

			if(!inst->activeFine)
				continue;
			
			exprEvaluate(otc->fineCheck, aib->exprContext, &answer);

			if(answer.type==MULTI_INT && answer.int32)
			{
				inst->executed++;
				if(combatTeam->combatState==AITEAM_COMBAT_STATE_FIGHT || combatTeam->combatState==AITEAM_COMBAT_STATE_WAITFORFIGHT)
					inst->executedThisCombat++;
				inst->executedThisTick++;

				if(otc->action)
					exprEvaluate(otc->action, aib->exprContext, &answer);

				if(otc->runAITick)
					aiTickExecuteFSM(e, aib);

				if((otc->maxCount && inst->executed>=otc->maxCount) || 
					(otc->maxPerThink && inst->executedThisTick>=otc->maxPerThink) || 
					(otc->maxPerCombat && inst->executedThisCombat>=otc->maxPerCombat))
				{
					inst->activeFine = 0;
				}
			}
		}

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

void aiAddToFirstList(Entity *e, AIVarsBase *aib)
{
	devassert(!entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY));

	eaiPushUnique(&aiMapState.needsInit, entGetRef(e));
}

void aiAddToBucket(Entity *e, AIVarsBase *aib)
{
	int i;
	int smallestCnt = 0;
	AIEntityBucket *bestBkt = NULL;
	AIPartitionState *partition = aiPartitionStateGet(entGetPartitionIdx(e), true);

	if(aib->entBucket)
	{
		devassert(stashIntFindInt(aib->entBucket->entsByRef, entGetRef(e), NULL));
		return;
	}

	if(aib->team && !aib->team->noUpdate)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(aib->team->members, AITeamMember, member)
		{
			if(member->memberBE && member->memberBE->aibase && member->memberBE->aibase->entBucket)
			{
				bestBkt = member->memberBE->aibase->entBucket;
				break;
			}
		}
		FOR_EACH_END;
	}

	if(!bestBkt)
	{
		for(i=0; i<ARRAY_SIZE(partition->entBuckets); i++)
		{
			if(!bestBkt || (int)stashGetCount(partition->entBuckets[i].entsByRef) < smallestCnt)
			{
				bestBkt = &partition->entBuckets[i];
				smallestCnt = stashGetCount(partition->entBuckets[i].entsByRef);
			}
		}
	}

	aib->entBucket = bestBkt;
	stashIntAddInt(bestBkt->entsByRef, entGetRef(e), 1, true);
}

void aiRemoveFromBucket(Entity *e, AIVarsBase *aib)
{
	if(aib->entBucket)
	{
		bool found = stashIntRemoveInt(aib->entBucket->entsByRef, entGetRef(e), NULL);

		if(!found)
		{
			if(isProductionMode())
				ErrorfForceCallstack("Removing ent from bucket that wasn't in the bucket");
			else
				assert(0);
		}
		aib->entBucket = NULL;
	}
}

static void _aiTickBucket(StashTable priorityBucket,StashTable entBucket)
{
	int j;
	StashTableIterator entiter;
	StashElement elem;
	static U32 *toRemoveArray = NULL;
	const bool bPriorityBucket = (priorityBucket == entBucket);

	stashGetIterator(entBucket, &entiter);
	eaiClearFast(&toRemoveArray);
	while(stashGetNextElement(&entiter, &elem))
	{
		EntityRef ref = stashElementGetIntKey(elem);
		Entity *e = entFromEntityRefAnyPartition(ref);
		bool bEntShouldBePriority;
		AIConfig *baseConfig;
		AIVarsBase const * aib;

		if(!e)
		{
			eaiPush(&toRemoveArray, ref);
			continue;
		}
		if(entCheckFlag(e, ENTITYFLAG_DESTROY))
		{
			eaiPush(&toRemoveArray, ref);
			e->aibase->entBucket = NULL;
			continue;
		}

		if(!entIsAlive(e))
		{
			PERFINFO_AUTO_START("aiDeadTick", 1);
			aiDeadTick(e, e->aibase);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			devassert(entGetType(e)!=GLOBALTYPE_ENTITYPROJECTILE);
			devassert(!entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY));

			aiTick(e, e->aibase);
		}

		aib = e->aibase;

		bEntShouldBePriority = false;
		baseConfig = GET_REF(aib->config_use_accessor);
		if (baseConfig && eaSize(&baseConfig->offtickActions))
			bEntShouldBePriority = true;
		if (aib->forceThinkTick)
			bEntShouldBePriority = true;
		if (aib->powers && eaSize(&aib->powers->queuedPowers))
			bEntShouldBePriority = true;

		if (bEntShouldBePriority != bPriorityBucket)
		{
			if (bEntShouldBePriority)
			{
				aiRemoveFromBucket(e, e->aibase);
				stashIntAddInt(priorityBucket, entGetRef(e), 1, true);
			}
			else
			{
				stashIntRemoveInt(priorityBucket, entGetRef(e), NULL);
				aiAddToBucket(e, e->aibase);
			}
		}
	}

	for(j=eaiSize(&toRemoveArray)-1; j>=0; j--)
	{
		stashIntRemoveInt(entBucket, toRemoveArray[j], NULL);
	}
}

void aiTickBuckets(void)
{
	int idx;
	static EntityRef *reInit = NULL;

	if( aiDisableForUGC ) {
		return;
	}

	for(idx=0; idx<eaSize(&aiMapState.partitions); idx++)
	{
		int i;
		AIPartitionState *partition = aiMapState.partitions[idx];
		AIEntityBucket *bucket;

		if(!partition)
			continue;

		if(mapState_IsMapPausedForPartition(idx))
			continue;

		for(i=0; i<ARRAY_SIZE(partition->entBuckets); i++)
		{
			bucket = &partition->entBuckets[i];

			if(ABS_TIME_SINCE(bucket->timeLastUpdate)>SEC_TO_ABS_TIME(AI_TICK_RATE))
			{
				bucket->timeLastUpdate = bucket->timeLastUpdate + SEC_TO_ABS_TIME(AI_TICK_RATE);

				_aiTickBucket(partition->priorityEntBucket.entsByRef,bucket->entsByRef);
			}
		}

		if(ABS_TIME_SINCE(partition->priorityEntBucket.timeLastUpdate)>SEC_TO_ABS_TIME(AI_PRIORITY_TICK_RATE))
		{
			partition->priorityEntBucket.timeLastUpdate = partition->priorityEntBucket.timeLastUpdate + SEC_TO_ABS_TIME(AI_PRIORITY_TICK_RATE);
			_aiTickBucket(partition->priorityEntBucket.entsByRef,partition->priorityEntBucket.entsByRef);
		}
	}

	eaiClearFast(&reInit);
	for(idx=eaiSize(&aiMapState.needsInit)-1; idx>=0; idx--)
	{
		Entity *e = entFromEntityRefAnyPartition(aiMapState.needsInit[idx]);
		int partitionIdx;

		if(!e)
			continue;

		partitionIdx = entGetPartitionIdx(e);

		if(entGetType(e)==GLOBALTYPE_ENTITYPROJECTILE ||
			entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY) ||
			!entIsAlive(e))
		{
			continue;
		}

		if(e->aibase->time.pauseUntil > ABS_TIME_PARTITION(partitionIdx))
		{
			eaiPush(&reInit, entGetRef(e));
			continue;
		}

		aiFirstTickInit(e, e->aibase);

		aiTick(e, e->aibase);

		aiPostFirstTickInit(e, e->aibase);		

		aiAddToBucket(e, e->aibase);
	}
	eaiClearFast(&aiMapState.needsInit);

	eaiPushEArray(&aiMapState.needsInit, &reInit);
}

void aiDeadTick(Entity* e, AIVarsBase* aib)
{
	PERFINFO_AUTO_START_FUNC();

	if(aib->doDeathTick && (aib->powers->hasAfterDeathPowers || aib->powers->hasDeadOrAlivePowers))
	{
		AIConfig* config = aiGetConfig(e, aib);

		aiCheckQueuedPowers(e, aib);

		aiUpdateEntityTracking(e, aib, config);

		aiUsePowers(e, aib, config, true, !config->dontRandomizePowerUse, aiPowersGetPostRechargeTime(e, aib, config));

		if(entIsPlayer(e))
			aiExecuteNextTickCmdQueue(aib);
	}

	PERFINFO_AUTO_STOP();
}

typedef void (*aiAddExternVarCallback)(Entity* e, ExprContext* context);

aiAddExternVarCallback addExternVarCallback = NULL;

void aiSetAddExternVarCallback(aiAddExternVarCallback callback)
{
	addExternVarCallback = callback;
}

aiAddExternVarCallback dbgExternVarCallback = NULL;
void aiSetDebugExternVarCallback(aiAddExternVarCallback callback)
{
	dbgExternVarCallback = callback;
}

void aiSetupExprContext(Entity* e, AIVarsBase* aib, int iPartitionIdx, ExprContext* context, int staticCheck)
{
	if(staticCheck)
	{
		exprContextSetAllowRuntimeSelfPtr(context);
		exprContextSetAllowRuntimePartition(context);
	}
	else
	{
		exprContextSetSelfPtr(context, e);
		exprContextSetPartition(context, iPartitionIdx);
	}
	exprContextSetPointerVarPooledCached(context, contextString, context, parse_ExprContext, true, true, &contextVarHandle);
	exprContextSetPointerVarPooledCached(context, meString, e, parse_Entity, true, true, &meVarHandle);
	exprContextSetPointerVarPooledCached(context, curStateTrackerString, NULL, parse_FSMStateTrackerEntry, true, true, &curStateTrackerVarHandle);
	exprContextSetPointerVarPooledCached(context, aiString, aib, parse_AIVarsBase, true, true, &aiVarHandle);
	exprContextSetFuncTable(context, aiFuncTableGlobal);

	if(addExternVarCallback)
		addExternVarCallback(e, context);
	if(dbgExternVarCallback)
		dbgExternVarCallback(e, context);
}

F32 aiGetGuardPointDistance(Entity* e, AIVarsBase* aib, Entity* target)
{
	Vec3 pos;
	
	aiGetSpawnPos(e, aib, pos);

	return entGetDistance(target, NULL, NULL, pos, NULL);
}

static FSMContext* fsmContext = NULL;
static ExprContext* exprContext = NULL;

AUTO_COMMAND;
void startFSM(char* fsmName)
{
	if(fsmContext)
		fsmContextDestroy(fsmContext);
	fsmContext = fsmContextCreateByName(fsmName, "Combat");
}

void aiSetStateChangeCallback(aiStateChangeCallback callback)
{
	stateChangeCallback = callback;
}

const char* aiGetState(Entity* e)
{
	FSMContext* pFSMContext = aiGetCurrentBaseFSMContext(e);
	if (!pFSMContext) 
		return NULL;

	return fsmGetState(pFSMContext);
}

const char* aiGetCombatState(Entity *e)
{
	AIVarsBase *aib = e->aibase;

	if(!aib->combatFSMContext)
		return NULL;

	return fsmGetState(aib->combatFSMContext);
}

const char* aiGetStatePath(Entity* e)
{
	FSMContext* pFSMContext = aiGetCurrentBaseFSMContext(e);
	if (!pFSMContext) 
		return NULL;
	
	return fsmGetFullStatePath(pFSMContext);
}

const char* aiGetStatePathWithJob(Entity* e)
{
	static char* stateStr = NULL;
	AIJob* job = e->aibase->job;
	FSMContext* pFSMContext = aiGetCurrentBaseFSMContext(e);
	if (!pFSMContext) 
		return NULL;

	if(!stateStr)
		estrCreate(&stateStr);

	estrClear(&stateStr);

	if(job)
	{
		estrConcatf(&stateStr, "Job: %s, %s (Orig FSM: ", aiGetJobName(e),
												fsmGetFullStatePath(job->fsmContext));
	}

	estrConcatf(&stateStr, "%s", fsmGetFullStatePath(pFSMContext));


	if(job)
		estrConcatf(&stateStr, ")");

	return stateStr;
}

AUTO_COMMAND ACMD_NAME(confused) ACMD_LIST(gEntConCmdList);
void entConConfused(Entity* e, int on)
{
	e->aibase->confused = !!on;
}

AUTO_COMMAND ACMD_NAME(aiPrintState) ACMD_LIST(gEntConCmdList);
void entConAiPrintState(Entity* e)
{
	printf("%s\n", aiGetStatePathWithJob(e));
} 

// Tests the AI Expression code
AUTO_COMMAND ACMD_LIST(gEntConCmdList) ACMD_GLOBAL;
void TestAIExpr(Entity* e, ACMD_SENTENCE testStr)
{
	MultiVal answer;
	Expression* expr = exprCreate();
	ExprContext* context;
	int created = false;
	CommandQueue **cmdQueue = NULL;
	ExprLocalData ***localData = NULL;

	if(!e->aibase || !e->aibase->exprContext)
	{
		context = exprContextCreate();
		aiSetupExprContext(e, e->aibase, entGetPartitionIdx(e), context, false);
		created = true;
	}
	else
		context = e->aibase->exprContext;

	if(e->aibase && e->aibase->fsmContext)
	{
		cmdQueue = &e->aibase->fsmContext->curTracker->exitHandlers;
		localData = &e->aibase->fsmContext->curTracker->localData;
	}
	else
	{
		static CommandQueue *staticQueue = NULL;
		static ExprLocalData **staticData = NULL;

		if(!staticQueue)
			staticQueue = CommandQueue_Create(10, 0);
		cmdQueue = &staticQueue;
		localData = &staticData;
	}

	if(exprGenerateFromString(expr, context, testStr, NULL))
	{
		exprContextCleanupPush(context, cmdQueue, localData);
		exprEvaluate(expr, context, &answer);
		exprContextCleanupPop(context);
	}

	exprDestroy(expr);

	if(created)
		exprContextDestroy(context);
}

F32 aiFindGroundDistance(WorldColl* wc, Vec3 sourcePos, F32* groundPos)
{
	F32 height;

	PERFINFO_AUTO_START_FUNC();
	
	height = heightCacheGetHeight(wc, sourcePos);

	if(height==-FLT_MAX)
	{
		if(groundPos)
			zeroVec3(groundPos);
		PERFINFO_AUTO_STOP();
		return -FLT_MAX;
	}

	if(groundPos)
	{
		copyVec3(sourcePos, groundPos);
		vecY(groundPos) = height;
	}

	PERFINFO_AUTO_STOP();

	return vecY(sourcePos) - height;
}

static int aiGetGroundNormal(WorldColl* wc, const Vec3 sourcePos, Vec3 outNormal)
{
	WorldCollCollideResults results = {0};
	Vec3 rayStart, rayEnd;

	PERFINFO_AUTO_START_FUNC();

	copyVec3(sourcePos, rayStart);
	copyVec3(sourcePos, rayEnd);
	rayStart[1] += 10.f;
	rayEnd[1] -= 20.f;

	if (wcRayCollide(wc, rayStart, rayEnd, WC_QUERY_BITS_WORLD_ALL, &results))
	{
		copyVec3(results.normalWorld, outNormal);
		PERFINFO_AUTO_STOP();

		return true;
	}
	
	PERFINFO_AUTO_STOP();
	zeroVec3(outNormal);
	return false;
}

// uses a raycast downwards from given position
int aiFindGroundPosition(WorldColl* wc, const Vec3 vSourcePos, Vec3 vOutGroundPos)
{
	WorldCollCollideResults results = {0};
	Vec3 rayStart, rayEnd;

	PERFINFO_AUTO_START_FUNC();

	copyVec3(vSourcePos, rayStart);
	copyVec3(vSourcePos, rayEnd);
	rayStart[1] += 10.f;
	rayEnd[1] -= 20.f;

	if (wcRayCollide(wc, rayStart, rayEnd, WC_QUERY_BITS_WORLD_ALL, &results))
	{
		copyVec3(results.posWorldImpact, vOutGroundPos);
		PERFINFO_AUTO_STOP();

		return true;
	}

	PERFINFO_AUTO_STOP();
	copyVec3(vSourcePos, vOutGroundPos);
	return false;
}

#define AI_STEP_UP_HEIGHT 1.42f
#define AI_CAPSULE_STEP_UP 0.01f

int aiCollideRayWorldCollEx(WorldColl* wc, Entity* e, const Vec3 sourcePos1, Entity* target,
							const Vec3 targetPos1, AICollideRayFlag flags, 
							F32 stepLength, AICollideRayResult *resultOut, Vec3 collPtOut)
{
#define AICOLLIDERAY_SET_OUTPUT(r,v)		if(resultOut)*resultOut=(r);if(collPtOut)copyVec3((v),collPtOut)
		
	Vec3 rayStart, rayEnd;
	Vec3 capStart, capEnd;
	WorldCollCollideResults results = {0,0,{0,0,0},{0,0,0},{0,0,0},{0,0,0},NULL,NULL,NULL,NULL,0};
	F32 fEntHeight = entGetHeight(e);
	F32 walkHeight = fEntHeight - 1.f;
	F32 fCapsuleLength = MAX(3.0f,fEntHeight-3.0f);
	int doWalkCheck = (flags & AICollideRayFlag_DOWALKCHECK);
	int collided;
	int i;

	// Critters below 10 ft can just use walk check, since it should be very cheap for them
	//  due to the height cache
	copyVec3(sourcePos1, rayStart);
	if(doWalkCheck)
	{
		if(walkHeight > 10.f)
		{
			F32 groundDist;
			vecY(rayStart) += 3.f;
			groundDist = aiFindGroundDistance(wc, rayStart, rayStart);
			if(groundDist == -FLT_MAX)
			{
				AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_START, targetPos1);
				return true;
			}
		}
		vecY(rayStart) += walkHeight;
	}
	else if(e)
	{
		entGetCombatPosDir(e, NULL, rayStart, NULL);
	}
	else
	{
		rayStart[1] += walkHeight;
	}

	copyVec3(targetPos1, rayEnd);
	if(doWalkCheck)
	{
		if(walkHeight > 10.f)
		{
			F32 groundDist;
			vecY(rayEnd) += 3.f;
			groundDist = aiFindGroundDistance(wc, rayEnd, rayEnd);
			if(groundDist == -FLT_MAX)
			{
				AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_END, targetPos1);
				return true;
			}
		}
		vecY(rayEnd) += walkHeight;
	}
	else if(target)
	{
		entGetCombatPosDir(target, NULL, rayEnd, NULL);
	}
	else
	{
		rayEnd[1] += walkHeight;
	}

	// if we're told to skip the raycast, assume the raycast passes with no collision
	if (!(flags & AICollideRayFlag_SKIPRAYCAST))
	{
		collided = wcRayCollide(wc, rayStart, rayEnd, WC_QUERY_BITS_COMBAT, &results);
		if (collided)
			collided = combat_ValidateHit(e, target, &results);

		if(collided && zmapIsUGCGeneratedMap(NULL))
		{
			int revColl = wcRayCollide(wc, rayEnd, rayStart, WC_QUERY_BITS_COMBAT, &results);
			if (revColl)
				revColl = combat_ValidateHit(e, target, &results);

			if(!revColl)
				collided = false;
		}
	}
	else 
		collided = false;
	

	if(!collided && (flags & AICollideRayFlag_DOCAPSULECHECK))
	{
		// Use unmodified positions
		copyVec3(sourcePos1, capStart);
		copyVec3(targetPos1, capEnd);
		vecY(capStart) += AI_CAPSULE_STEP_UP;
		vecY(capEnd) += AI_CAPSULE_STEP_UP;
		collided = wcCapsuleCollideHR(wc, capStart, capEnd, WC_QUERY_BITS_COMBAT, fCapsuleLength, 1.5f, &results);

		if (collided)
			collided = combat_ValidateHit(e, target, &results);

		if(collided && zmapIsUGCGeneratedMap(NULL))
		{
			int revColl = wcCapsuleCollideHR(wc, capEnd, capStart, WC_QUERY_BITS_COMBAT, fCapsuleLength, 1.5f, &results);
			if (revColl)
				revColl = combat_ValidateHit(e, target, &results);

			if(!revColl)
				collided = false;
		}
	}

	if(collided && !doWalkCheck)
	{
		AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_RAY, results.posWorldImpact);
		return true;
	}
	
	if(e && (flags & AICollideRayFlag_DOAVOIDCHECK))
	{
		Vec3 avoidRayStart, avoidRayEnd;

		copyVec3(sourcePos1, avoidRayStart);
		copyVec3(targetPos1, avoidRayEnd);

		//avoidRayStart[1] += 2;
		//avoidRayEnd[1] += 2;
		for(i = eaSize(&e->aibase->avoid.base.volumeEntities)-1; i >= 0; i--)
		{
			AIVolumeEntry* entry = e->aibase->avoid.base.volumeEntities[i];
			
			if(aiAvoidEntryCheckLine(e, avoidRayStart, avoidRayEnd, entry, true, NULL))
			{
				AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_AVOID, targetPos1);
				return true;
			}
		}
	}
	
	if(!doWalkCheck)
	{
		if (collided)
		{
			AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_RAY, results.posWorldImpact);
		}
		else
		{
			AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_NONE, targetPos1);
		}

		return collided;
	}

	{
		F32 curHeight, lastHeight, curStep, increment, distToTarget;
		Vec3 stepDir, testVec = {0};
		Vec3 lastCastPos;
		Vec3 groundPos;
		F32 lastGroundHeight;

		if(stepLength < 0.01f)
			stepLength = AI_DEFAULT_STEP_LENGTH;

		zeroVec3(groundPos);
		lastGroundHeight = 0.f;

		subVec3(rayEnd, rayStart, stepDir);
		distToTarget = lengthVec3(stepDir);

		// adjust stepLength so that we do the last walk check to the actual destination position
		stepLength = distToTarget/(floor(distToTarget/stepLength)+1.0f);

		normalVec3(stepDir);

		// 
		copyVec3(rayStart, lastCastPos);
		lastHeight = aiFindGroundDistance(wc, lastCastPos, groundPos);
		lastGroundHeight = groundPos[1];
		
		for(increment = stepLength; increment < distToTarget+stepLength*0.5f; increment += stepLength)
		{
			scaleAddVec3(stepDir,increment,rayStart,testVec);
			if(walkHeight>4.0)
			{
				testVec[1] = groundPos[1]+1.5;
				if(wcRayCollide(wc, lastCastPos, testVec, WC_QUERY_BITS_COMBAT, NULL))
				{
					if(!zmapIsUGCGeneratedMap(NULL) || wcRayCollide(wc, testVec, lastCastPos, WC_QUERY_BITS_COMBAT, NULL))
					{
						AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_WALK, groundPos);
						return true;
					}
				}
			}

			testVec[1] = groundPos[1]+walkHeight;
			if(wcRayCollide(wc, lastCastPos, testVec, WC_QUERY_BITS_COMBAT, NULL))
			{
				if(!zmapIsUGCGeneratedMap(NULL) || wcRayCollide(wc, testVec, lastCastPos, WC_QUERY_BITS_COMBAT, NULL))
				{
					AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_WALK, groundPos);
					return true;
				}
			}
			
			curHeight = aiFindGroundDistance(wc, testVec, groundPos);
			if(curHeight==-FLT_MAX)
			{
				AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_WALK, lastCastPos);
				return true;
			}
			curStep = groundPos[1] - lastGroundHeight;
			if(curStep > AI_STEP_UP_HEIGHT)
			{
				AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_WALK, lastCastPos);
				return true; // treat this as a collision for now i guess
			}
			lastGroundHeight = groundPos[1];
			
			copyVec3(testVec, lastCastPos);
		}

		// If the target position isn't visible at the end of the ground walking, it must be in something
		//  or underground
		#define HEIGHT_CACHE_GRANULARITY_OFFSET		(0.5f)
		// push the ground position up slightly to account for the height cache's granularity
		groundPos[1] += HEIGHT_CACHE_GRANULARITY_OFFSET;
		// get the distance between our ground and ray end, and if the distance is large enough, do a raycast
		// to make sure nothing is in the way.
		lastGroundHeight = groundPos[1] - rayEnd[1];
		if(	ABS(lastGroundHeight) > 1.0f && 
			wcRayCollide(wc, rayEnd, groundPos, WC_QUERY_BITS_COMBAT, &results))
		{
			AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_END, results.posWorldImpact);
			return true;
		}

		if (flags & AICollideRayFlag_DOCAPSULECHECK)
		{
			// Since we're doing a walk check, and a capsule check, we need to make sure that the final "walk to" position is somewhere we can actually be.
			Capsule cap;
			WorldCollCollideResults result;
			copyVec3(targetPos1, capEnd);
			capEnd[1] = groundPos[1];
			
			cap.fLength = fCapsuleLength;
			cap.fRadius =  1.5f;
			setVec3(cap.vStart, 0,  1.5f, 0);
			setVec3(cap.vDir, 0, 1, 0);
			if (wcCapsuleCollideCheck(wc,&cap,capEnd,WC_QUERY_BITS_COMBAT,&result))
			{
				// Set the result as capEnd because somewhere around there it collided,
				//   and because wcCapCollCheck doesn't give an answere.
				AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_END, capEnd);
				return true;
			}
		}

		groundPos[1] -= HEIGHT_CACHE_GRANULARITY_OFFSET+0.01;
		AICOLLIDERAY_SET_OUTPUT(AICollideRayResult_NONE, groundPos);
	}
#undef AICOLLIDERAY_SET_OUTPUT

	return false;
}

int aiCollideRayEx(	int iPartitionIdx, Entity* e, const Vec3 sourcePos1, Entity* target,
					const Vec3 targetPos1, AICollideRayFlag flags, 
					F32 stepLength, AICollideRayResult *resultOut, Vec3 collPtOut)
{
	return aiCollideRayWorldCollEx(	worldGetActiveColl(iPartitionIdx),
									e,
									sourcePos1,
									target,
									targetPos1,
									flags,
									stepLength,
									resultOut,
									collPtOut);
}

static int checkWorldCollideFromAngle(	Entity* e, AIVarsBase* aib, const F32* sourcePos,
										const F32* targetPos, const F32* idealVec,
										F32* targetOut, F32 angle)
{
	if(angle)
	{
		F32 cosA = cos(angle);
		F32 sinA = sin(angle);
		Vec3 reachableVec;

		reachableVec[0] = idealVec[0] * cosA - idealVec[2] * sinA;
		reachableVec[1] = 0;
		reachableVec[2] = idealVec[0] * sinA + idealVec[2] * cosA;

		addVec3(targetPos, reachableVec, targetOut);
	}
	else
	{
		addVec3(targetPos, idealVec, targetOut);
	}
	
	{
		AICollideRayFlag flags = AICollideRayFlag_DOAVOIDCHECK;

		if (!(aib->powers->hasFlightPowers || aib->powers->alwaysFlight))
		{
			flags |= AICollideRayFlag_DOWALKCHECK;
		}
		
		return aiCollideRayEx(entGetPartitionIdx(e), e, sourcePos, NULL, targetOut, flags, AI_DEFAULT_STEP_LENGTH, NULL, targetOut);
	}
}

int checkWorldCollideFromAngleAndMe(Entity* e, AIVarsBase* aib, const F32* myPos,
									const F32* targetPos, const F32* idealVec,
									F32 angle, F32* outPos, F32* LOSFromTargetOnlyPos,
									int rayCastFromMyPos, int* LOSFromTargetOnly)
{
	int collided = checkWorldCollideFromAngle(e, aib, targetPos, targetPos, idealVec, outPos, angle);
	if(!collided && rayCastFromMyPos)
	{
		collided = aiCollideRay(entGetPartitionIdx(e), e, myPos, NULL, outPos, AICollideRayFlag_DOWALKCHECK | AICollideRayFlag_DOAVOIDCHECK);
		if(collided)
		{
			*LOSFromTargetOnly = true;
			copyVec3(outPos, LOSFromTargetOnly);
		}
	}
	return collided;
}

void aiHandleRegionChange(Entity *e, S32 prevRegion, S32 curRegion)
{
	aiMovementHandleRegionChange(e, prevRegion, curRegion);
}

// Death-lite basically - clear aggro bits, but not actually dead
void aiResetForRespawn(Entity *e)
{
	AIVarsBase *aib = e->aibase;

	// Clear out the status table as well
	aiClearStatusTable(e);	

	// save the powerapplyID so we can use it to check if critters should ignore powers we cast before were dead
	e->aibase->uiRespawnApplyID = powerapp_GetCurrentID();
	e->aibase->lastRespawnTime = ABS_TIME_PARTITION(entGetPartitionIdx(e));

	aiClearPreferredAttackTarget(e, aib);

	aiClearAllQueuedPowers(e, aib);

	aiMultiTickAction_ClearQueue(e, aib);

	aiCleanupStatus(e, aib);

	aiCombatJob_OnDeathCleanup(e, aib);
}

void aiOnDeathCleanup(Entity* e)
{
	int i;
	AIVarsBase* aib = e->aibase;
	EntityRef myRef = entGetRef(e);

	if (!aib)
		return;

	aiResetForRespawn(e);

	if(aib->proximityRadius > 0)
	{
		aiUpdateProxEnts(e, aib);
		for(i=0; i<aib->proxEntsCount; i++)
		{
			if(aib->proxEnts[i].e->pPlayer)
				ClientCmd_sndCritterDeath(aib->proxEnts[i].e, e->myRef);
		}
	}

	if (aib->team && gConf.bLogEncounterSummary)
	{		
		bool bAnyAlive = false;
		for(i = eaSize(&aib->team->members)-1; i >= 0; i--)
		{
			if (entIsAlive(aib->team->members[i]->memberBE))
				bAnyAlive = true;
		}
		if (!bAnyAlive)
		{
			gslEncounterLog_Finish(aib->team->collId);
		}
	}
}

void aiOnUndeath(Entity *e)
{
	aiAddToBucket(e, e->aibase);
}

void aiGetSpawnPos(Entity *e, AIVarsBase *aib, Vec3 spawnPos)
{
	Entity* owner = e->erOwner ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;
	AITeam* team = aiTeamGetCombatTeam(e, aib);

	if(owner)
	{
		entGetPos(owner, spawnPos);
		addVec3(spawnPos, aib->spawnOffset, spawnPos);
	}
	else if(team->roamingLeash)
	{
		if(team->roamingLeashPointValid)
			addVec3(aib->spawnOffset, team->roamingLeashPoint, spawnPos);
		else
			entGetPos(e, spawnPos);
	}
	else
		copyVec3(aib->spawnPos, spawnPos);
}

void aiSetLeashTypePos(Entity *e, AIVarsBase *aib, const Vec3 rallyPt)
{
	copyVec3(rallyPt, aib->rallyPosition);
	aib->leashType = AI_LEASH_TYPE_RALLY_POSITION;

	aib->erLeashEntity = 0;
}

void aiSetLeashTypeOwner(Entity *e, AIVarsBase *aib)
{
	Entity* owner = e->erOwner ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;
	if (owner)
	{
		aib->leashType = AI_LEASH_TYPE_OWNER;
	}
	else
	{
		aib->leashType = AI_LEASH_TYPE_DEFAULT;
	}

	zeroVec3(aib->rallyPosition);
	aib->erLeashEntity = 0;
}

void aiSetLeashTypeEntity(Entity *e, AIVarsBase *aib, Entity *eLeashTo)
{
	if (eLeashTo)
	{
		aib->erLeashEntity = eLeashTo->myRef;
		aib->leashType = AI_LEASH_TYPE_ENTITY;
	}
	else
	{
		aib->erLeashEntity = 0;
		aib->leashType = AI_LEASH_TYPE_DEFAULT;
	}
	
	zeroVec3(aib->rallyPosition);
}

Entity* aiGetLeashEntity(Entity *e, AIVarsBase *aib)
{
	AILeashType eLeashType = aiGetLeashType(aib);
	if (eLeashType == AI_LEASH_TYPE_ENTITY)
	{
		return (aib->erLeashEntity) ? entFromEntityRef(entGetPartitionIdx(e), aib->erLeashEntity) : NULL;
	}
	else if (eLeashType == AI_LEASH_TYPE_OWNER)
	{
		return (e->erOwner) ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;
	}

	return NULL;
}

// Returns the leash type for the AI
AILeashType aiGetLeashType(AIVarsBase *aib)
{
	if (aib)
	{
		return aib->leashTypeOverride == AI_LEASH_TYPE_DEFAULT ? aib->leashType : aib->leashTypeOverride;
	}
	return AI_LEASH_TYPE_DEFAULT;
}

// Returns the array pointer for the rally position vector
const F32 * aiGetRallyPosition(AIVarsBase *aib)
{
	if (aib->leashTypeOverride == AI_LEASH_TYPE_RALLY_POSITION)
	{
		return aib->rallyPositionOverride;
	}
	return aib->rallyPosition;
}

int aiIsInCombat(Entity *e)
{
	AITeam *pTeam = aiTeamGetCombatTeam(e, e->aibase);
	if (pTeam)
	{
		return aiTeamInCombat(pTeam);
	}

	return false;
}

F32 aiGetSpawnPosDist(Entity *e, AIVarsBase *aib)
{
	Vec3 targetPos;
	aiGetSpawnPos(e, aib, targetPos);
	return entGetDistance(e, NULL, NULL, targetPos, NULL);
}

int aiCloseEnoughToSpawnPos(Entity* e, AIVarsBase* aib)
{
	Entity* owner = e->erOwner ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;
	F32 dist;
	int closeEnough;
	AITeam* team = aiTeamGetAmbientTeam(e, aib);
	int partitionIdx = entGetPartitionIdx(e);

	dist = aiGetSpawnPosDist(e, aib);

	closeEnough = dist < AI_PROX_NEAR_DIST;

	if(closeEnough)
	{
		if(owner || team->roamingLeash && team->roamingLeashPointValid)
			return true;

		if(aib->time.lastNearSpawnPos && ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastNearSpawnPos) > SEC_TO_ABS_TIME(2))
			return true;

		if(!aib->time.lastNearSpawnPos)
			aib->time.lastNearSpawnPos = ABS_TIME_PARTITION(partitionIdx);

		if(dist < 0.5)
		{
			Quat curRot;

			entGetRot(e, curRot);

			return quatWithinAngle(curRot, aib->spawnRot, RAD(5));
		}
	}
	else
		aib->time.lastNearSpawnPos = 0;

	return false;
}

void aiRegisterIsEntFrozenCallback(aiIsEntFrozenCallback entFrozenCB)
{
	s_isEntFrozenCB = entFrozenCB;
}

void aiSetCheckForFrozenEnts(int partitionIdx, bool check)
{
	AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);

	if (partition)
	{
		partition->bCheckForFrozenEnts = check;
	}
}

void aiRewindAIFSMTime(int partitionIdx, S64 timeToRewind)
{
	g_ulAbsTimes[partitionIdx] -= timeToRewind;
}

void aiCutsceneRewindFSMContext(Entity *e, AIVarsBase *aib, FSMContext *context)
{
	StashTableIterator iter;
	StashElement elem;

	if(!context)
		return;

	// RewindAIFSMTime does this effectively
	//context->curTracker->lastEntryTime += ABS_TIME_PARTITION(partitionIdx)-aib->time.timeCutsceneStart;

	if(!context->messages)
		return;

	stashGetIterator(context->messages, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		AIMessage *msg = (AIMessage*)stashElementGetPointer(elem);
		int i;

		for(i=eaSize(&msg->entries)-1; i>=0; i--)
		{
			AIMessageEntry *entry = msg->entries[i];

			if(entry->time<=aib->time.timeCutsceneStart)
				break;

			entry->time = aib->time.timeCutsceneStart;
		}
	}
}

// This is called when a cutscene that includes this Entity is starting
void aiCutsceneEntStartCallback(Entity *e)
{
	AIVarsBase *aib = e->aibase;
	int partitionIdx = entGetPartitionIdx(e);

	aib->time.timeCutsceneStart = ABS_TIME_PARTITION(partitionIdx);
}

// This is called when a cutscene that includes this Entity is ending
void aiCutsceneEntEndCallback(Entity *e)
{
	int i;
	AIVarsBase *aib = e->aibase;

	FOR_EACH_IN_EARRAY(aib->eaOverrideFSMStack, AIOverrideFSM, pOverrideFSM)
	{
		aiCutsceneRewindFSMContext(e, aib, pOverrideFSM->fsmContext);
	}
	FOR_EACH_END

	aiCutsceneRewindFSMContext(e, aib, aib->fsmContext);
	aiCutsceneRewindFSMContext(e, aib, aib->combatFSMContext);
	if(aib->job)
		aiCutsceneRewindFSMContext(e, aib, aib->job->fsmContext);

	FORALL_PARSETABLE(parse_AIVarsTime, i)
	{
		if(TOK_GET_TYPE(parse_AIVarsTime[i].type)==TOK_INT64_X)
		{
			S64 t = TokenStoreGetInt64(parse_AIVarsTime, i, &aib->time, 0, NULL);
			TokenStoreSetInt64(parse_AIVarsTime, i, &aib->time, 0, MIN(t, aib->time.timeCutsceneStart), NULL, NULL);
		}
	}

	// Other random time things
	MIN1(aib->lastShortcutCheckTime, aib->time.timeCutsceneStart);
}

void aiFlagForInitialFSM(Entity *e)
{
	eaiPushUnique(&g_NeedOnEntry, entGetRef(e));
}

// ------------------------------------------------------------------------------------------
void aiSetCurrentCombatFSMContext(AIVarsBase *aib, int bUseCombatRoleFSM)
{
	FSMContext *targetContext = (bUseCombatRoleFSM && aib->combatRoleVars && aib->combatRoleVars->combatFSMContext) 
										? aib->combatRoleVars->combatFSMContext 
										: aib->combatFSMContext;

	if (targetContext && aib->currentCombatFSMContext != targetContext)
	{
		if (aib->currentCombatFSMContext)
			fsmExitCurState(aib->currentCombatFSMContext);
		aib->currentCombatFSMContext = targetContext;
	}
}

// ------------------------------------------------------------------------------------------
// ignores override FSMs and other combat FSMs on purpose 
void aiCopyCurrentFSMHandle(Entity* e, ReferenceHandle *pRefToFSM)
{
	fsmCopyCurrentFSMHandle(e->aibase->fsmContext, pRefToFSM);
}

// ------------------------------------------------------------------------------------------
// sets the base FSM of the AI
void aiSetFSM(Entity* e, SA_PARAM_NN_VALID FSM *fsm)
{
	if(e->aibase->fsmContext)
	{
		fsmExitCurState(e->aibase->fsmContext);
		fsmContextDestroy(e->aibase->fsmContext);
	}

	e->aibase->fsmContext = fsmContextCreate(fsm);
	if(e->aibase->fsmContext)
		e->aibase->fsmContext->messages = e->aibase->fsmMessages;

	exprContextSetFSMContext(e->aibase->exprContext, e->aibase->fsmContext);
}

// ------------------------------------------------------------------------------------------
void aiSetFSMByName(Entity* e, const char *pchFSMName)
{
	AIVarsBase *aib = e->aibase;
	FSM *pOldFSM = GET_REF(aib->fsmContext->origFSM);
	FSM *pNewFSM;

	if(pOldFSM && !stricmp(pOldFSM->name, pchFSMName))
		return;

	pNewFSM = fsmGetByName(pchFSMName);
	if (pNewFSM)
		aiSetFSM(e, pNewFSM);
}

// ------------------------------------------------------------------------------------------
// returns 0 on error
U32 aiPushOverrideFSMByName(Entity *e, const char *pchFSMName)
{
	FSM *pFSM = fsmGetByName(pchFSMName);
	if (pFSM)
	{
		static U32 s_FSMOverrideID = 1;
		AIOverrideFSM *pOverrideFSM = malloc(sizeof(AIOverrideFSM));

		pOverrideFSM->fsmContext = fsmContextCreate(pFSM);
		if (!pOverrideFSM->fsmContext)
		{
			free(pOverrideFSM);
			return 0;
		}

		// exit the current FSM state
		{
			FSMContext *pCurFSMContext = aiGetCurrentFSMContext(e);
			if (pCurFSMContext)
				fsmExitCurState(pCurFSMContext);
		}
		
		pOverrideFSM->fsmContext->messages = e->aibase->fsmMessages;

		pOverrideFSM->id = s_FSMOverrideID++;
		if (s_FSMOverrideID == 0) s_FSMOverrideID = 1;

		eaPush(&e->aibase->eaOverrideFSMStack, pOverrideFSM);
		return pOverrideFSM->id;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------
// removes the override FSM by ID that was given on creation of the override FSM
int aiRemoveOverrideFSM(Entity *e, U32 id)
{
	S32 i;
	for (i = eaSize(&e->aibase->eaOverrideFSMStack) - 1; i >= 0; --i)
	{
		AIOverrideFSM* pOverrideFSM = e->aibase->eaOverrideFSMStack[i]; 
		if (pOverrideFSM->id == id)
		{
			fsmExitCurState(pOverrideFSM->fsmContext);
			fsmContextDestroy(pOverrideFSM->fsmContext);
			eaRemove(&e->aibase->eaOverrideFSMStack, i);
			free(pOverrideFSM);
			return true;
		}
	}
	
	return false;
}

void aiRemoveAllOverrideFSMs(Entity *e)
{
	S32 i;
	for (i = eaSize(&e->aibase->eaOverrideFSMStack) - 1; i >= 0; --i)
	{
		AIOverrideFSM* pOverrideFSM = e->aibase->eaOverrideFSMStack[i]; 
		fsmExitCurState(pOverrideFSM->fsmContext);
		fsmContextDestroy(pOverrideFSM->fsmContext);
	}
	eaClearEx(&e->aibase->eaOverrideFSMStack, NULL);

}

// ------------------------------------------------------------------------------------------
// will return an override FSM context if there is one, otherwise returns the base fsmContext
FSMContext* aiGetCurrentBaseFSMContext(Entity *e)
{
	AIVarsBase *aib = e->aibase;

	if(eaSize(&aib->eaOverrideFSMStack))
	{
		AIOverrideFSM *pOverride = eaTail(&aib->eaOverrideFSMStack);
		devassert(pOverride && pOverride->fsmContext);
		return pOverride->fsmContext;
	}
	return aib->fsmContext;
}

// ------------------------------------------------------------------------------------------
// same as aiGetCurrentBaseFSMContext, but factors in the insideCombatFSM and the ai job
FSMContext* aiGetCurrentFSMContext(Entity *e)
{
	AIVarsBase *aib = e->aibase;

	if(eaSize(&aib->eaOverrideFSMStack))
	{
		AIOverrideFSM *pOverride = eaTail(&aib->eaOverrideFSMStack);
		devassert(pOverride && pOverride->fsmContext);
		return pOverride->fsmContext;
	}

	if(aib->insideCombatFSM)
		return aib->combatFSMContext;

	if(aib->job)
		return aib->job->fsmContext;
	
	return aib->fsmContext;
}

// ------------------------------------------------------------------------------------------
bool aiIsEntAlive(Entity* e)
{
	return entIsAlive(e) && (!e->pChar || !e->pChar->pNearDeath);
	
}

// ------------------------------------------------------------------------------------------
void aiLibOncePerFrame(void)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaiSize(&g_NeedOnEntry)-1; i>=0; i--)
	{
		Entity *e = entFromEntityRefAnyPartition(g_NeedOnEntry[i]);

		if(e)
		{
			if(entCheckFlag(e, ENTITYFLAG_DESTROY))
				continue;

			if(!entGetPlayer(e))
			{
				FSMState *state;
				AIVarsBase *aib = e->aibase;

				state = GET_REF(aib->fsmContext->curStateList[0]->stateRef);
				fsmExecuteEx(aib->fsmContext, aib->exprContext, 0, 1);
				aib->skipOnEntry = 1;
				aib->fsmContext->initialState = 1;  // Do this because we're not executing the action, so we don't want the first ACTUAL run to transition
			}
		}
	}
	eaiClearFast(&g_NeedOnEntry);

	aiCivilian_OncePerFrame();
	
	aiMastermind_OncePerFrame();

	PERFINFO_AUTO_STOP();
}

void aiPartitionLoad(int partitionIdx)
{
	EntityIterator *iter;
	Entity *e;

	PERFINFO_AUTO_START_FUNC();

	aiAvoidPartitionLoad(partitionIdx);

	PERFINFO_AUTO_START("FirstList", 1);

	iter = entGetIteratorAllTypes(partitionIdx, 0, ENTITYFLAG_PROJECTILE|ENTITYFLAG_DESTROY|ENTITYFLAG_CIV_PROCESSING_ONLY);
	while(e = EntityIteratorGetNext(iter))
	{
		if(!e->aibase->entBucket)
			aiAddToFirstList(e, e->aibase);
	}
	EntityIteratorRelease(iter);

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
}

void aiPartitionUnload(int partition)
{
	aiPartitionStateDestroy(partition);
}

void aiMapLoad(bool fullInit)
{
	if(fullInit)
	{
		FOR_EACH_IN_EARRAY(aiMapState.partitions, AIPartitionState, partition)
		{
			if (partition)
			{
				aiPartitionUnload(FOR_EACH_IDX(-, partition));
				aiPartitionLoad(FOR_EACH_IDX(-, partition));
			}
		}
		FOR_EACH_END
	}
	aiMastermind_OnMapLoad();
	aiFormation_OnMapLoad();
}

void aiMapUnload(void)
{
	FOR_EACH_IN_EARRAY(aiMapState.partitions, AIPartitionState, partition)
	{
		aiPartitionUnload(FOR_EACH_IDX(-, partition));
	}
	FOR_EACH_END

	aiMastermind_OnMapUnload();
}

int targetEntVarHandle = 0;
int targetEntStatusVarHandle = 0;
int powInfoVarHandle = 0;
int contextVarHandle = 0;
int meVarHandle = 0;
int curStateTrackerVarHandle = 0;
int aiVarHandle = 0;
int attribModDefHandle = 0;
int interactLocationVarHandle = 0;

const char* targetEntString;
const char* targetEntStatusString;
const char* powInfoString;
const char* contextString;
const char* meString;
const char* curStateTrackerString;
const char* aiString;
const char* attribModDefString;
const char* interactLocationString;

AUTO_RUN;
void aiRegisterPooledStrings(void)
{
	targetEntString = allocAddStaticString("targetEnt");
	targetEntStatusString = allocAddStaticString("targetStatus");
	powInfoString = allocAddStaticString("powInfo");
	contextString = allocAddStaticString("Context");
	meString = allocAddStaticString("Me");
	curStateTrackerString = allocAddStaticString("curStateTracker");
	aiString = allocAddStaticString("ai");
	attribModDefString = allocAddStaticString("ModDef");
	interactLocationString = allocAddStaticString("QueriedJobLocation");
}

typedef struct FSMLDPatrol FSMLDPatrol;
typedef struct NavPathWaypoint	NavPathWaypoint;
#include "ailib_autogen_queuedfuncs.c"
