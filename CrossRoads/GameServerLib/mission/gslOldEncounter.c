/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiJobs.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "AutoTransDefs.h"
#include "beacon.h"
#include "Character.h"
#include "characterclass.h"
#include "contact_common.h"
#include "cutscene.h"
#include "CostumeCommonEntity.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "entdebugmenu.h"
#include "entityGrid.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntityMovementDefault.h"
#include "Expression.h"
#include "fileutil.h"
#include "foldercache.h"
#include "GameServerLib.h"
#include "gametimer.h"
#include "gslContact.h"
#include "gslCritter.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslInteractable.h"
#include "gslLandmark.h"
#include "gslmaptransfer.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslOldEncounter.h"
#include "gslOldEncounter_events.h"
#include "gslPartition.h"
#include "gslPatrolRoute.h"
#include "gslPlayerDifficulty.h"
#include "gslPowerTransactions.h"
#include "gslQueue.h"
#include "gslSpawnPoint.h"
#include "gslSendToClient.h"
#include "gslTriggerCondition.h"
#include "gslVolume.h"
#include "HashFunctions.h"
#include "interaction_common.h"
#include "mapstate_common.h"
#include "memorypool.h"
#include "message.h"
#include "mission_common.h"
#include "nemesis.h"
#include "nemesis_common.h"
#include "octree.h"
#include "oldencounter_common.h"
#include "PlayerDifficultyCommon.h"
#include "Powers.h"
#include "quat.h"
#include "rand.h"
#include "ReferenceSystem.h"
#include "RegionRules.h"
#include "resourceInfo.h"
#include "resourceManager.h"
#include "ScratchStack.h"
#include "sharedmemory.h"
#include "SimpleParser.h"
#include "stashtable.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "Team.h"
#include "timedeventqueue.h"
#include "timing.h"
#include "utils.h"
#include "wlBeacon.h"
#include "wlEncounter.h"
#include "wlInteraction.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "AutoGen/gslContact_h_ast.h"
#include "AutoGen/gslOldEncounter_h_ast.h"
#include "AutoGen/oldencounter_common_h_ast.h"
#include "AutoGen/encounter_enums_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/entEnums_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Expression_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/mission_enums_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"


// ----------------------------------------------------------------------------------
// Type definitions and forward declarations
// ----------------------------------------------------------------------------------

static int oldencounter_ExprLocationResolveEncounter(ExprContext *pContext, const char *pcName, Mat4 matOut, const char *pcBlamefile);
static CritterFaction* oldencounter_GetFaction(OldEncounter* encounter);

#define ENCOUNTER_SYSTEM_TICK 10
#define ENCOUNTER_SYSTEM_SLOW_TICK 60
#define ACTOR_FALLTHROUGHWORLD_CHECKDIST 1000

// Only used for cutscenes now
#define ENCOUNTER_DESPAWN_RADIUS 500

// Encounter will only check infrequently for spawn condition expression
#define MIN_SPAWN_COND_CHECK_SECONDS 1

// Distance the actor will look for a ground to snap to before spawning in midair
#define FIRST_ACTOR_SPAWN_SNAP_TO_DIST 7
#define SECOND_ACTOR_SPAWN_SNAP_TO_DIST 20
// Distance an actor will look for ground to snap to that doesn't want to snap
#define ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_UP		3
#define ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_DOWN	1

#define WAVE_ATTACK_POLL_INTERVAL 1.0f


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

OldEncounterPartitionState **s_eaOldEncounterPartitionStates;

static char **gLayerFiles = NULL;

int g_EncounterReloadCounter = 0;

// Encounter is needed to sort actors, but callback doesn't have a udata field.
// Work around this with a static variable for now
static OldEncounter* s_sortEncounter = NULL;

int g_checkEncounterExternVars = true;


// ----------------------------------------------------------------------------------
// General Utility Functions
// ----------------------------------------------------------------------------------

// Memory Pool interface for encounters
MP_DEFINE(OldEncounter);
static OldEncounter* encounter_Alloc(void) 
{ 
	MP_CREATE(OldEncounter, 10); 
	return MP_ALLOC(OldEncounter); 
}

static void encounter_Free(OldEncounter* encounter) 
{ 
	MP_FREE(OldEncounter, encounter); 
}


MP_DEFINE(OldEncounterGroup);
static OldEncounterGroup* encountergroup_Alloc(void) 
{ 
	MP_CREATE(OldEncounterGroup, 10); 
	return MP_ALLOC(OldEncounterGroup); 
}

static void encountergroup_Free(OldEncounterGroup* encounterGroup) 
{ 
	MP_FREE(OldEncounterGroup, encounterGroup); 
}


// ----------------------------------------------------------------------------------
// Partition State Functions
// ----------------------------------------------------------------------------------

OldEncounterPartitionState *oldencounter_GetPartitionState(int iPartitionIdx)
{
	OldEncounterPartitionState *pState = eaGet(&s_eaOldEncounterPartitionStates, iPartitionIdx);
	assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
	return pState;
}

static OldEncounterPartitionState *oldencounter_CreatePartitionState(int iPartitionIdx)
{
	OldEncounterPartitionState *pState = calloc(1,sizeof(OldEncounterPartitionState));

	pState->iPartitionIdx = iPartitionIdx;
	pState->pEncounterFromStaticEncounterHash = stashTableCreateAddress(8);
	pState->pEncounterOctree = octreeCreate();

	return pState;
}

static void oldencounter_DestroyPartitionState(OldEncounterPartitionState *pState)
{
	stashTableDestroy(pState->pEncounterFromStaticEncounterHash);
	octreeDestroy(pState->pEncounterOctree);
}


// ----------------------------------------------------------------------------------
// Encounter Def Fixup
// ----------------------------------------------------------------------------------


// Called from oldencounter_ValidateDefCB in oldencounter_common.c
int oldencounter_DefFixupPostProcess(EncounterDef* def)
{
	static ExprContext* exprContext = NULL;
	static ExprContext* perPlayerExprContext = NULL;
	static ExprContext* interactExprContext = NULL;

	int j, k, numActors, numActions, numJobs, numPoints;

	if (!exprContext) {
		exprContext = exprContextCreate();
		exprContextSetFuncTable(exprContext, encounter_CreateExprFuncTable());
		exprContextSetAllowRuntimePartition(exprContext);
		// Not allowing Self pointer

		perPlayerExprContext = exprContextCreate();
		exprContextSetFuncTable(perPlayerExprContext, encounter_CreatePlayerExprFuncTable());
		exprContextSetAllowRuntimePartition(perPlayerExprContext);
		exprContextSetAllowRuntimeSelfPtr(perPlayerExprContext);

		interactExprContext = exprContextCreate();
		exprContextSetFuncTable(interactExprContext, encounter_CreateInteractExprFuncTable());
		exprContextSetAllowRuntimePartition(interactExprContext);
		exprContextSetAllowRuntimeSelfPtr(interactExprContext);
	}

	// This exists so the beaconserver can load only the encounter data
	if (g_EncounterNoErrorCheck) {
		return 1;
	}

	// Generate the expression and then evaluate them all using the verification functions
	exprContextSetPointerVar(exprContext, "EncounterDef", def, parse_EncounterDef, false, true);
	exprContextSetPointerVar(perPlayerExprContext, "EncounterDef", def, parse_EncounterDef, false, true);
	exprContextSetPointerVar(interactExprContext, "EncounterDef", def, parse_EncounterDef, false, true);

	if (def->spawnCond) {
		if(def->bCheckSpawnCondPerPlayer) {
			exprGenerate(def->spawnCond, perPlayerExprContext);
		} else {
			exprGenerate(def->spawnCond, exprContext);
		}
	}
	if (def->successCond) {
		exprGenerate(def->successCond, exprContext);
	}
	if (def->failCond) {
		exprGenerate(def->failCond, exprContext);
	}
	if (def->waveCond) {
		exprGenerate(def->waveCond, exprContext);
	}

	// If there's a wave condition, there should also be an explicit success condition
	// TODO: there's validation in this function; the fixup and validation should be different steps
	if (def->waveCond && !def->successCond) {
		ErrorFilenameGroupf(def->filename, "Design", 2, "EncounterDef %s has a reinforce condition but no success condition.", def->name);
	}

	// Now do the same for all the actors
	numActors = eaSize(&def->actors);
	for (j = 0; j < numActors; j++) {
		OldActor* actor = def->actors[j];
		int var, numVars;
		OldActorInfo* actorInfo = actor->details.info;
		OldActorAIInfo* actorAIInfo = actor->details.aiInfo;
		FSM *fsm = NULL;

		if (!actorInfo && !actorAIInfo) {
			continue;
		}
		
		// Make sure all Actors have a name
		if (!actor->name) {
			char *dstStr = NULL;
			estrStackCreate(&dstStr);
			estrPrintf(&dstStr, "Actor%i", actor->uniqueID);
			actor->name = allocAddString(dstStr);
			estrDestroy(&dstStr);
		}

		// Error if name isn't unique
		for (k = 0; k < numActors; k++) {
			if (def->actors[k] && def->actors[k]->name && k != j && def->actors[k]->name == actor->name) {
				ErrorFilenamef(def->filename, "EncounterDef %s: Duplicate actors named %s", def->name, actor->name);
			}
		}

		if (actorAIInfo && GET_REF(actorAIInfo->hFSM)) {
			fsm = GET_REF(actorAIInfo->hFSM);
		}

		// Parse the variables into their sub variables
		if (actorAIInfo) {
			numVars = eaSize(&actorAIInfo->actorVars);
		} else {
			numVars = 0;
		}

		for (var = 0; var < numVars; var++) {
			char valStrCopy[4096];
			OldEncounterVariable* actorVar = actorAIInfo->actorVars[var];
			const char* valStr = MultiValGetString(&actorVar->varValue, NULL);
			FSMExternVar* externVar = NULL;

			if (fsm) {
				externVar = fsmExternVarFromName(fsm, actorVar->varName, "Encounter");
			}

			if (externVar && valStr && (g_checkEncounterExternVars && valStr[0] || strchr(valStr, '|'))) {
				char* last;
				char* currStr;

				strcpy(valStrCopy, valStr);
				currStr = strtok_r(valStrCopy, "|", &last);
				while (currStr) {
					char cleanStr[1024];
					MultiVal* newMulti = MultiValCreate();
					MultiValType type = MULTI_FLAGLESS_TYPE(externVar->type);
					strcpy(cleanStr, removeLeadingWhiteSpaces(currStr));
					removeTrailingWhiteSpaces(cleanStr);

					if(type == MULTI_INT) {
						int intVal = atoi(cleanStr);
						MultiValSetInt(newMulti, intVal);
					} else if (type == MULTI_FLOAT) {
						F32 floatVal = atof(cleanStr);
						MultiValSetFloat(newMulti, floatVal);
					} else if (type == MULTI_STRING || type == MULTIOP_LOC_STRING) {
						MultiValSetString(newMulti, cleanStr);
					} else {
						ErrorFilenamef(def->filename, "EncounterDef %s: unrecognized extern var type %s", def->name, MultiValTypeToReadableString(externVar->type));
					}

					if (externVar->scType) {
						exprStaticCheckWithType(exprContext, newMulti, externVar->scType, externVar->scTypeCategory, def->filename);
					}
					eaPush(&actorVar->parsedStrVals, newMulti);
					currStr = strtok_r(NULL, "|", &last);
				}
			}

			if (!IsClient() && externVar && valStr && valStr[0] && externVar->scType && (stricmp(externVar->scType, "message") == 0)) {
				// Don't run this on the client (which only happens in editors anyway) 
				// since messages are often missing on the client.

				// Var is a message key, so make sure it exists
				if (!RefSystem_ReferentFromString(gMessageDict, valStr)) {
					ErrorFilenamef(def->filename, "EncounterDef %s: actor %d FSM var '%s' refers to a non-existent message key '%s'", def->name, actor->uniqueID, actorVar->varName, valStr);
				}
				// Only encounters within maps should reference map variables
				// Disabled for now.  Not sure this is a reasonable check.
				if (def->filename && (strnicmp(def->filename,"Maps",4) != 0) && (strnicmp(valStr,"Maps",4) == 0)) {
					ErrorFilenamef(def->filename, "EncounterDef %s: actor %d FSM var '%s' refers to an unsafe map message key '%s'", def->name, actor->uniqueID, actorVar->varName, valStr);
				}
			}
		}
			
		// Verify the expressions are good
		if (actorInfo && actorInfo->spawnCond) {
			exprGenerate(actorInfo->spawnCond, exprContext);
		}

		// Only the actor's interact condition is currently used
		if (actorInfo && actorInfo->oldActorInteractProps.interactCond) {
			exprGenerate(actorInfo->oldActorInteractProps.interactCond, interactExprContext);
		}

		// Now check the actor for errors
		if(actorAIInfo && IS_HANDLE_ACTIVE(actorAIInfo->hFSM) && !GET_REF(actorAIInfo->hFSM)) {
			ErrorFilenameGroupf(def->filename, "Design", 2, "Invalid state machine for AI: %s.", REF_STRING_FROM_HANDLE(actorAIInfo->hFSM));
		}
	}

	// Named points in encounters used to use a Vec3, but now use a Mat4.  Convert any from the old style to the new
	numPoints = eaSize(&def->namedPoints);
	for(j=0; j<numPoints; j++) {
		OldNamedPointInEncounter* point = def->namedPoints[j];
		if (!vec3IsZero(point->relLoc)) {
			// Convert from old versions of the NamedPoint struct
			if (vec3IsZero(point->relLocation[3]) && !vec3IsZero(point->relLoc)) {
				copyVec3(point->relLoc, point->relLocation[3]);
			}
			zeroVec3(point->relLoc);
		}
		// Always perform this check, to fix any invalid points with no rotation information
		if (vec3IsZero(point->relLocation[0]) || vec3IsZero(point->relLocation[1]) || vec3IsZero(point->relLocation[2])) {
			identityMat3(point->relLocation);
		}
	}

	// Verify the level ranges are entered correctly and fixup them up as needed
	if (def->minLevel || def->maxLevel) {
		if (!def->minLevel) {
			def->minLevel = 1;
			ErrorFilenameGroupf(def->filename, "Design", 2, "EncounterDef %s: MinLevel is 0, but should be at least 1", def->name);
		}
		if (!def->maxLevel) {
			def->maxLevel = def->minLevel;
			ErrorFilenameGroupf(def->filename, "Design", 2, "EncounterDef %s: MaxLevel is 0, but should be at least 1", def->name);
		}
		if (def->minLevel > def->maxLevel) {
			int tmp = def->minLevel;
			def->minLevel = def->maxLevel;
			def->maxLevel = tmp;
			ErrorFilenameGroupf(def->filename, "Design", 2, "EncounterDef %s: MinLevel(%i) is greater than MaxLevel(%i)", def->name, def->maxLevel, def->minLevel);
		}
	}

	// Now generate the expressions for all actions
	// TODO: Setup On Load Verification for actions
	numActions = eaSize(&def->actions);
	for (j = 0; j < numActions; j++) {
		OldEncounterAction* action = def->actions[j];
		if (action->actionExpr) {
			exprGenerate(action->actionExpr, exprContext);
		}
	}

	numJobs = eaSize(&def->encJobs);
	for (j = 0; j < numJobs; j++) {
		// note: this returns whether all expressions were generated successfully, so the return val should
		// probably be used for bin generation at some point
		aiJobGenerateExpressions(def->encJobs[j]);
	}

	return 1;
}


// ----------------------------------------------------------------------------------
// EncounterLayer Dictionary Load Logic
// ----------------------------------------------------------------------------------

static void encounterlayer_LayerReloadCB(const char* relpath, int when)
{
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (g_EncounterMasterLayer && !strstri(relpath, g_EncounterMasterLayer->mapFileName)) {
		// Force InitEncounters in several ticks
		g_EncounterReloadCounter = 3;
	}
}


AUTO_STARTUP(EncounterLayerInit) ASTRT_DEPS(Powers, Critters, AS_Messages, AI, Cutscenes);
void oldencounter_Startup(void)
{
	if (gConf.bAllowOldEncounterData) {
		exprRegisterLocationPrefix("encounter", oldencounter_ExprLocationResolveEncounter, false);

		// Create a callback for watching when the encounterlayers change
		if (isDevelopmentMode()) {
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "maps/*.encounterlayer", encounterlayer_LayerReloadCB);
		}
	}
}


// ----------------------------------------------------------------------------------
// Encounter/StaticEncounter Utility Functions
// ----------------------------------------------------------------------------------

static CritterFaction *oldencounter_GetMajorityFaction(OldEncounter *pEncounter)
{
	static CritterFaction** s_eaFactions = NULL;
	static int *s_eaiCounts = NULL;

	CritterFaction *pMajorityFaction = NULL;
	int iMajorityCount = 0;
	int i;

	eaClearFast(&s_eaFactions);
	eaiClearFast(&s_eaiCounts);

	if (pEncounter && GET_REF(pEncounter->staticEnc) && GET_REF(pEncounter->staticEnc)->spawnRule) {
		CritterFaction* pEncounterFaction = oldencounter_GetFaction(pEncounter);
		EncounterDef *pDef = GET_REF(pEncounter->staticEnc)->spawnRule;

		for (i = eaSize(&pDef->actors)-1; i>=0; --i) {
			CritterFaction *pActorFaction = pEncounterFaction;
			if (pDef->actors[i]->details.info) {
				CritterDef *pCritterDef = GET_REF(pDef->actors[i]->details.info->critterDef);
				if (GET_REF(pDef->actors[i]->details.info->critterFaction)) {
					pActorFaction = GET_REF(pDef->actors[i]->details.info->critterFaction);
				} else if (pCritterDef && GET_REF(pCritterDef->hFaction)) {
					pActorFaction = GET_REF(pCritterDef->hFaction);
				}
			}

			if (pActorFaction) {
				int pos = eaFind(&s_eaFactions, pActorFaction);
				if (pos >= 0){
					++s_eaiCounts[pos];
				} else {
					eaPush(&s_eaFactions, pActorFaction);
					eaiPush(&s_eaiCounts, 1);
				}
			}
		}
	}

	for (i = eaSize(&s_eaFactions)-1; i>=0; --i) {
		if (s_eaiCounts[i] > iMajorityCount){
			iMajorityCount = s_eaiCounts[i];
			pMajorityFaction = s_eaFactions[i];
		}
	}

	return pMajorityFaction;
}


static U32 oldencounter_GetMajorityGangID(OldEncounter *pEncounter)
{
	static U32 *s_eauGangIDs = NULL;
	static int *s_eaiCounts = NULL;

	U32 uMajorityGangID = 0;
	int iMajorityCount = 0;
	int i;

	ea32ClearFast(&s_eauGangIDs);
	eaiClearFast(&s_eaiCounts);

	if (pEncounter && GET_REF(pEncounter->staticEnc) && GET_REF(pEncounter->staticEnc)->spawnRule) {
		EncounterDef *pDef = GET_REF(pEncounter->staticEnc)->spawnRule;
		U32 uEncounterGangID = pDef->gangID;

		for (i = eaSize(&pDef->actors)-1; i>=0; --i) {
			U32 uActorGangID = uEncounterGangID;
			if (pDef->actors[i]->details.info) {
				CritterDef *pCritterDef = GET_REF(pDef->actors[i]->details.info->critterDef);
				if (pCritterDef && pCritterDef->iGangID) {
					uActorGangID = pCritterDef->iGangID;
				}
			}

			if (uActorGangID) {
				int pos = ea32Find(&s_eauGangIDs, uActorGangID);
				if (pos >= 0) {
					++s_eaiCounts[pos];
				} else {
					ea32Push(&s_eauGangIDs, uActorGangID);
					eaiPush(&s_eaiCounts, 1);
				}
			}
		}
	}

	for (i = ea32Size(&s_eauGangIDs)-1; i>=0; --i) {
		if (s_eaiCounts[i] > iMajorityCount) {
			iMajorityCount = s_eaiCounts[i];
			uMajorityGangID = s_eauGangIDs[i];
		}
	}

	return uMajorityGangID;
}


// ----------------------------------------------------------------------------------
// Encounter Instance Logic
// ----------------------------------------------------------------------------------

// Post-creation initialization
static void oldencounter_Init(OldEncounter* encounter)
{
	oldencounter_BeginTrackingEvents(encounter);
}


// Creates a new encounter by name
static OldEncounter* oldencounter_Create(OldEncounterPartitionState *pState, OldStaticEncounter* staticEnc)
{
	OldEncounter* newEncounter = encounter_Alloc();
	CritterFaction *pMajorityFaction = NULL;
	WorldRegion *pRegion;

	// Setup all fields for the new encounter
	newEncounter->iPartitionIdx = pState->iPartitionIdx;
	SET_HANDLE_FROM_STRING("StaticEncounter", staticEnc->name, newEncounter->staticEnc);
	newEncounter->eventLog = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
	newEncounter->eventLogSinceSpawn = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
	newEncounter->eventLogSinceComplete = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));

	// Create the context
	newEncounter->context = exprContextCreate();
	exprContextSetFuncTable(newEncounter->context, encounter_CreateExprFuncTable());
	exprContextSetPointerVarPooled(newEncounter->context, g_EncounterVarName, newEncounter, parse_OldEncounter, false, true);

	quatToMat(staticEnc->encRot, newEncounter->mat);
	copyVec3(staticEnc->encPos, newEncounter->mat[3]);

	newEncounter->spawnRadius = staticEnc->spawnRule->spawnRadius;

	pRegion = worldGetWorldRegionByPos(staticEnc->encPos);
	if (pRegion) {
		newEncounter->eRegionType = worldRegionGetType(pRegion);
	}

	// Initialize the encounter
	newEncounter->state = staticEnc->spawnRule->spawnCond ? EncounterState_Waiting : EncounterState_Asleep;
	pMajorityFaction = oldencounter_GetMajorityFaction(newEncounter);
	if (pMajorityFaction){
		SET_HANDLE_FROM_REFERENT(g_hCritterFactionDict, pMajorityFaction, newEncounter->hMajorityFaction);
	}
	newEncounter->uMajorityGangID = oldencounter_GetMajorityGangID(newEncounter);

	// Store the encounter in the static encounter to encounter hash
	stashAddressAddPointer(pState->pEncounterFromStaticEncounterHash, staticEnc, newEncounter, true);

	// Create event tracker
	newEncounter->eventTracker = eventtracker_Create(pState->iPartitionIdx);

	return newEncounter;
}


// Cleanup for the encounter
static void oldencounter_Destroy(OldEncounter* encounter)
{
	int i;

	oldencounter_StopTrackingEvents(encounter);

	stashTableDestroySafe(&encounter->eventLog);
	stashTableDestroySafe(&encounter->eventLogSinceSpawn);
	stashTableDestroySafe(&encounter->eventLogSinceComplete);

	if (encounter->context)
		exprContextDestroy(encounter->context);

	// Kill all the encounter's entities
	for(i=eaSize(&encounter->ents)-1; i>=0; --i) {
		gslQueueEntityDestroy(encounter->ents[i]);
		gslDestroyEntity(encounter->ents[i]);
	}
	eaDestroy(&encounter->ents);

	REMOVE_HANDLE(encounter->staticEnc);
	REMOVE_HANDLE(encounter->hMajorityFaction);
	REMOVE_HANDLE(encounter->hOwner);

	eventtracker_Destroy(encounter->eventTracker, false);

	StructDestroySafe(parse_GameEventParticipant, &encounter->pGameEventInfo);

	encounter_Free(encounter);
}


// Reset the encounter from its completed state back to waiting
void oldencounter_Reset(OldEncounter* encounter)
{
	EncounterDef* def = oldencounter_GetDef(encounter);

	PERFINFO_AUTO_START_FUNC();

	encounter->state = (def && def->spawnCond) ? EncounterState_Waiting : EncounterState_Asleep;
	encounter->status[0] = '\0';
	encounter->iNumNearbyPlayers = 0;

	if (encounter->eventLog)
		stashTableClear(encounter->eventLog);
	if (eaiSize(&encounter->entsWithCredit))
		eaiDestroy(&encounter->entsWithCredit);

	encounter->spawningPlayer = 0;
	eaDestroyEx(&encounter->ents, gslQueueEntityDestroy);
	REMOVE_HANDLE(encounter->hOwner);

	PERFINFO_AUTO_STOP();
}


// Put the encounter to sleep
static void oldencounter_Sleep(OldEncounter* encounter)
{
	oldencounter_Reset(encounter);

	encounter->state = EncounterState_Asleep;
	encounter->lastSleep = timeSecondsSince2000();
}


// Adds the encounter to the global encounter list
static void oldencounter_AddToEncounterList(OldEncounterPartitionState *pState, OldEncounter* encounter)
{
	eaPush(&pState->eaEncounters, encounter);

	encounter->octreeEntry.node = encounter;
	copyVec3(encounter->mat[3], encounter->octreeEntry.bounds.mid);
	encounter->octreeEntry.bounds.radius = 0;
	subVec3same(encounter->mat[3], encounter->octreeEntry.bounds.radius, encounter->octreeEntry.bounds.min);
	addVec3same(encounter->mat[3], encounter->octreeEntry.bounds.radius, encounter->octreeEntry.bounds.max);

	octreeAddEntry(pState->pEncounterOctree, &encounter->octreeEntry, OCT_ROUGH_GRANULARITY);
}


int oldencounter_EvalExpression(OldEncounter* encounter, Expression* expression)
{
	MultiVal resultVal;
	int iValue;

	PERFINFO_AUTO_START_FUNC();

	exprContextSetPartition(encounter->context, encounter->iPartitionIdx);
	exprEvaluate(expression, encounter->context, &resultVal);
	iValue = MultiValGetInt(&resultVal, NULL);

	PERFINFO_AUTO_STOP();
	return iValue;
}


EncounterDef* oldencounter_GetDef(OldEncounter* encounter)
{
	if (encounter) {
		OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
		if (staticEnc) {
			return staticEnc->spawnRule;
		}
	}
	return NULL;
}


Entity*** oldencounter_GetNearbyPlayers(OldEncounter* encounter, F32 dist)
{
	static Entity** nearbyPlayers = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();

	// Count ENTITYFLAG_IGNORE players; they should spawn encounters, even if the encounters don't aggro on them
	entGridProximityLookupExEArray(encounter->iPartitionIdx, encounter->mat[3], &nearbyPlayers, dist, ENTITYFLAG_IS_PLAYER, 0, NULL);

	// Hack - Ignore players who are on a team if the Team container doesn't exist yet.
	// (Encounters need to know the team size in order to spawn at the right size.)
	for (i = eaSize(&nearbyPlayers)-1; i>=0; --i){
		if (nearbyPlayers[i] && nearbyPlayers[i]->pTeam && IS_HANDLE_ACTIVE(nearbyPlayers[i]->pTeam->hTeam) && !GET_REF(nearbyPlayers[i]->pTeam->hTeam)){
			eaRemove(&nearbyPlayers, i);
		}
	}

	PERFINFO_AUTO_STOP();

	return &nearbyPlayers;
}


static int oldencounter_ExprLocationResolveEncounter(ExprContext *pContext, const char *pcName, Mat4 matOut, const char *pcBlamefile)
{
	OldStaticEncounter *pEnc = oldencounter_StaticEncounterFromName(pcName);
	if (pEnc) {
		copyMat4(unitmat, matOut);
		copyVec3(pEnc->encPos, matOut[3]);
		return true;
	}

	return false;
}


__forceinline bool oldencounter_IsWaitingToSpawn(OldEncounter* encounter)
{ 
	return (encounter->state == EncounterState_Asleep) || (encounter->state == EncounterState_Waiting); 
}


__forceinline bool oldencounter_IsRunning(OldEncounter* encounter)
{ 
	return (encounter->state >= EncounterState_Spawned) && (encounter->state <= EncounterState_Aware); 
}


__forceinline bool oldencounter_IsComplete(OldEncounter* encounter)
{
	return (encounter->state >= EncounterState_Success) && (encounter->state <= EncounterState_Off);
}


static bool oldencounter_AllEntsAreDead(OldEncounter* encounter)
{
	int i, n = eaSize(&encounter->ents);
	for (i = 0; i < n; i++) {
		if (entIsAlive(encounter->ents[i])) {
			return false;
		}
	}
	return true;
}


static CritterFaction* oldencounter_GetFaction(OldEncounter* encounter)
{
	OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
	if (staticEnc) {
		CritterFaction* faction = GET_REF(staticEnc->encFaction);
		if (faction) {
			return faction;
		}
		return GET_REF(staticEnc->spawnRule->faction);
	}
	return NULL;
}


U32 oldencounter_GetLevel(OldEncounter* encounter)
{
	EncounterDef* def = oldencounter_GetDef(encounter);
	OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);

	// If this encounter is set to use the player's level, see if there is a player we can use
	if(def->bUsePlayerLevel) {
		Entity *pPlayer = NULL;
		
		if (zmapInfoGetMapType(NULL) == ZMTYPE_OWNED){
			// If this is an Owned map, try to use the map owner's level
			pPlayer = partition_GetPlayerMapOwner(encounter->iPartitionIdx);
			if (pPlayer && pPlayer->pChar) {
				return entity_GetSavedExpLevel(pPlayer);
			} else {
				if (staticEnc) {
					ErrorFilenamef(staticEnc->pchFilename, "Error spawning encounter %s: Trying to use the player's level on an Owned map, but no Map Owner is available!", staticEnc->name);
				}
				return mechanics_GetMapLevel(encounter->iPartitionIdx);
			}
		} else {
			// Otherwise, this may be an ambush - try to use the spawning player's level
			pPlayer = entFromEntityRef(encounter->iPartitionIdx, encounter->spawningPlayer);
			if(pPlayer && pPlayer->pChar) {
				return pPlayer->pChar->iLevelCombat;
			} else {
				if (staticEnc) {
					ErrorFilenamef(staticEnc->pchFilename, "Error spawning encounter %s: Trying to use the player's level, but no spawning player is available!", staticEnc->name);
				}
				return mechanics_GetMapLevel(encounter->iPartitionIdx);
			}
		}
	}

	if (staticEnc && (staticEnc->minLevel || staticEnc->maxLevel)) {
		return randomIntRange(staticEnc->minLevel, staticEnc->maxLevel);
	}
	if (def && (def->minLevel || def->maxLevel)) {
		return randomIntRange(def->minLevel, def->maxLevel);
	}
	if (staticEnc && staticEnc->layerParent && staticEnc->layerParent->layerLevel) {
		return staticEnc->layerParent->layerLevel;
	}
	return mechanics_GetMapLevel(encounter->iPartitionIdx);
}


CritterGroup* oldencounter_GetCritterGroup(OldEncounter* encounter)
{
	OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
	if (staticEnc) {
		if (GET_REF(staticEnc->encCritterGroup)) {
			return GET_REF(staticEnc->encCritterGroup);
		}
		return GET_REF(staticEnc->spawnRule->critterGroup);
	}
	return NULL;
}


char* oldencounter_GetSpawnAnim(OldEncounter* encounter)
{
	OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
	if (staticEnc) {
		if (staticEnc->spawnAnim) {
			return staticEnc->spawnAnim;
		}
		return staticEnc->spawnRule->spawnAnim;
	}
	return NULL;
}

static S32 oldencounter_PlayerDifficultyTeamSize(int iPartitionIdx)
{
	if(zmapInfoGetMapType(NULL) != ZMTYPE_STATIC)
	{
		PlayerDifficulty *pdiff = pd_GetDifficulty(mapState_GetDifficulty(mapState_FromPartitionIdx(iPartitionIdx)));
		if(pdiff)
		{
			if(pdiff->DisableTeamSizeMapVarName)
			{
				// find map and block changing difficulty if map var is present
				MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, pdiff->DisableTeamSizeMapVarName);
				if(pMapVar)
				{
					return 0;
				}
			}

			// return override team size
			return pdiff->iTeamSizeOverride;
		}
	}

	// no change
	return 0;
}

// Do a different player count check on mission maps?
static U32 oldencounter_GetActivePlayerCount(OldEncounter* encounter)
{
	U32 playerCount = partition_GetPlayerCount(encounter->iPartitionIdx);
	OldStaticEncounter* staticEnc = encounter ? GET_REF(encounter->staticEnc) : NULL;

	// TODO(BF): This could be unstable. Instead the team size should be set on
	// map creation and never change, I think.
	// Use the team size of the spawning player, if it's greater than the number
	// of players on the map.
	if (encounter && encounter->spawningPlayer){
		Entity *pSpawningEnt = entFromEntityRef(encounter->iPartitionIdx, encounter->spawningPlayer);
		if (pSpawningEnt && pSpawningEnt->pTeam && team_IsMember(pSpawningEnt)) {
			Team *pTeam = GET_REF(pSpawningEnt->pTeam->hTeam);
			if (pTeam) {
				MAX1(playerCount, eaUSize(&pTeam->eaMembers));
			}
		}
	}

	// Is there a team size set in the debugger?
	if (g_ForceTeamSize) {
		playerCount = g_ForceTeamSize;
	}
	else if (oldencounter_PlayerDifficultyTeamSize(encounter->iPartitionIdx))
	{
		playerCount = oldencounter_PlayerDifficultyTeamSize(encounter->iPartitionIdx);
	}

	// Is there a layer override on the number of players?
	if (staticEnc && staticEnc->layerParent && staticEnc->layerParent->forceTeamSize) {
		playerCount = staticEnc->layerParent->forceTeamSize;
	}

	playerCount = CLAMP(playerCount, 1, MAX_TEAM_SIZE);
	return playerCount;
}


static bool oldencounter_ShouldSpawnActor(OldEncounter* encounter, OldActor* actor)
{
	U32 numPlayers = oldencounter_GetActivePlayerCount(encounter);
	OldActorInfo* actorInfo = oldencounter_GetActorInfo(actor);
	
	// Check num player spawn scaling
	if (!oldencounter_IsEnabledAtTeamSize(actor, numPlayers)) {
		return false;
	}

	// Check the spawn condition last(most likely the most expensive)
	if (actorInfo->spawnCond && !oldencounter_EvalExpression(encounter, actorInfo->spawnCond)) {
		return false;
	}

	return true;
}


int oldencounter_NumLivingEnts(OldEncounter* encounter)
{
	int numLiving = 0, i, n = eaSize(&encounter->ents);
	for (i = 0; i < n; i++) {
		if (entIsAlive(encounter->ents[i])) {
			numLiving++;
		}
	}
	return numLiving;
}


int oldencounter_NumEntsToSpawn(OldEncounter* encounter)
{
	int teamSize = oldencounter_GetActivePlayerCount(encounter);
	EncounterDef* def = oldencounter_GetDef(encounter);
	int i, n = eaSize(&def->actors);
	int count = 0;

	for(i=0; i<n; i++) {
		if (oldencounter_ShouldSpawnActor(encounter, def->actors[i])) {
			count++;
		}
	}

	return count;
}


void oldencounter_EvaluateAction(int iPartitionIdx, OldEncounterAction* action, ExprContext* context)
{
	MultiVal resultVal;
	if (action->actionExpr) {
		exprContextSetPartition(context, iPartitionIdx);
		exprEvaluate(action->actionExpr, context, &resultVal);
	}
}


static void oldencounter_TriggerActions(OldEncounter* encounter)
{
	EncounterDef* def = oldencounter_GetDef(encounter);
	if (def) {
		int i, n = eaSize(&def->actions);
		for (i = 0; i < n; i++) {
			OldEncounterAction* action = def->actions[i];
			if (action->state == encounter->state) {
				oldencounter_EvaluateAction(encounter->iPartitionIdx, action, encounter->context);
			}
		}
	}
}


// Transition an encounter to its new state
static void oldencounter_StateTransition(OldEncounter* encounter, EncounterState newState)
{
	if (encounter->state != newState) {
		encounter->state = newState;
		oldencounter_TriggerActions(encounter);
		eventsend_EncounterStateChange(encounter->iPartitionIdx, encounter, newState);
	}
}


// Go through the list of players near this encounter and set off events if a new player entered
static void oldencounter_UpdateProximityList(OldEncounter* encounter)
{
	// Currently we only update the nearby player list prespawn. Do we need to update this once spawned?
	if (oldencounter_IsWaitingToSpawn(encounter)) {
		Entity*** nearbyPlayers;

		PERFINFO_AUTO_START("oldEncounter_UpdateProximityList: Waiting to Spawn", 1);

		nearbyPlayers = oldencounter_GetNearbyPlayers(encounter, encounter->spawnRadius);
		encounter->iNumNearbyPlayers = eaSize(nearbyPlayers);

		PERFINFO_AUTO_STOP();

	} else if (encounter->state == EncounterState_Spawned) {
		// TODO: The 200 value here should probably be a globally set, 
		//       per-project (or perhaps per-zone) data value.
		bool bBecomeActive = false;
		Entity*** nearbyPlayers;

		PERFINFO_AUTO_START("oldEncounter_UpdateProximityList: Spawned", 1);

		nearbyPlayers = oldencounter_GetNearbyPlayers(encounter, 200);
		if (eaSize(nearbyPlayers) || g_encounterIgnoreProximity) {
			bBecomeActive = true;
		} else if (cutscene_GetNearbyCutscenes(encounter->iPartitionIdx, encounter->mat[3], ENCOUNTER_DESPAWN_RADIUS)) {
			bBecomeActive = true;
		}
		if (bBecomeActive) {
			oldencounter_StateTransition(encounter, EncounterState_Active);
		}
		encounter->iNumNearbyPlayers = eaSize(nearbyPlayers);
		
		PERFINFO_AUTO_STOP();
	}
}

static void oldencounter_TrySleep(OldEncounter* encounter)
{
	if (oldencounter_IsRunning(encounter)) {
		OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
		bool canSleep = true;

		PERFINFO_AUTO_START("oldEncounter_TrySleep: Check sleep", 1);

		// Cannot go to sleep if it is "no despawn", a PvP map, or has a nearby cut scene 
		if (g_encounterDisableSleeping || 
			g_encounterIgnoreProximity ||
			(staticEnc && staticEnc->noDespawn) ||
			(zmapInfoGetMapType(NULL) == ZMTYPE_PVP) ||
			(cutscene_GetNearbyCutscenes(encounter->iPartitionIdx, encounter->mat[3], ENCOUNTER_DESPAWN_RADIUS))) 
		{
			canSleep = false;
		}

		if (canSleep) {
			static Entity** nearbyPlayers = NULL;
			int i, n = eaSize(&encounter->ents);
			Vec3 geoAvg;
			F32 maxDist = encounter->spawnRadius + 20;
			F32 tempDist;

			zeroVec3(geoAvg);

			for(i = eaSize(&encounter->ents)-1; i >= 0; i--) {
				Vec3 entPos;
				entGetPos(encounter->ents[i], entPos);
				addVec3(geoAvg, entPos, geoAvg);
			}

			// Add the encounter's center (in case the critters have been knocked far away from their starting point
			addVec3(geoAvg, encounter->mat[3], geoAvg);
			n++;

			// Find the critters' center (or use the encounter center if there are no critters)
			geoAvg[0] /= n;
			geoAvg[1] /= n;
			geoAvg[2] /= n;

			// Get the largest distance any critter is from this center
			for(i = eaSize(&encounter->ents)-1; i >= 0; i--) {
				Vec3 entPos;

				entGetPos(encounter->ents[i], entPos);
				tempDist = distance3(geoAvg, entPos);
				tempDist += encounter->ents[i]->fEntitySendDistance;
				maxDist = MAX(tempDist, maxDist);
			}

			// Also check the distance from the encounter to the center
			tempDist = distance3(geoAvg, encounter->mat[3]);
			maxDist = MAX(tempDist, maxDist);

			// Make sure to count ENTITYFLAG_IGNORE players here; they should still spawn encounters, even if the critters don't attack them
			if (maxDist <= 500) {
				entGridProximityLookupExEArray(encounter->iPartitionIdx, geoAvg, &nearbyPlayers, maxDist, ENTITYFLAG_IS_PLAYER, 0, NULL);
			}

			// If there's nobody nearby, reset the encounter unless it's part of a zone event or has noDespawn set
			if (maxDist <= 500 && !eaSize(&nearbyPlayers)) {
				if (g_EventLogDebug) {
					EncounterDef *def = oldencounter_GetDef(encounter);
					printf("No players near encounter %s, going to sleep.\n", def->name);
				}
				oldencounter_Sleep(encounter);
			}
		}
		PERFINFO_AUTO_STOP();
	}
}


static int oldencounter_CmpActorStrength(const OldActor** a1, const OldActor** a2)
{
	U32 numPlayers = oldencounter_GetActivePlayerCount(s_sortEncounter);
	OldActorInfo* a1Info = oldencounter_GetActorInfo((OldActor*)*a1);
	OldActorInfo* a2Info = oldencounter_GetActorInfo((OldActor*)*a2);
	CritterDef* a1Def = GET_REF(a1Info->critterDef);
	CritterDef* a2Def = GET_REF(a2Info->critterDef);
	int a1Order = critterRankGetOrder(a1Info->pcCritterRank);
	int a2Order = critterRankGetOrder(a2Info->pcCritterRank);

	if (a1Order > a2Order) {
		return -1;
	} else if (a1Order < a2Order) {
		return 1;
	} else {
		// compare subranks
		int a1SubOrder = critterSubRankGetOrder(a1Info->pcCritterSubRank);
		int a2SubOrder = critterSubRankGetOrder(a2Info->pcCritterSubRank);
		if (a1SubOrder == a2SubOrder) {
			if (a1Def && !a2Def) {
				return -1;
			} else if (a2Def && !a1Def) {
				return 1;
			}
			return 0;
		} else if (a1SubOrder >  a2SubOrder) {
			return -1;
		} else {
			return 1;
		}
	}
	return 0;
}


const char* oldencounter_GetEncounterName(OldEncounter *encounter)
{
	OldStaticEncounter *staticEnc = GET_REF(encounter->staticEnc);
	if(staticEnc) {
		return staticEnc->name;
	}
	return NULL;
}


void oldencounter_GetEncounterActorPosition(OldEncounter *encounter, OldActor *actor, Vec3 outVec, Quat outQuat)
{
	Quat actorQuat;
	Mat4 actorMat, worldMat;

	oldencounter_GetActorPositionOffset(actor, actorQuat, actorMat[3]);
	quatToMat(actorQuat, actorMat);
	mulMat4(encounter->mat, actorMat, worldMat);
	if (outQuat) {
		mat3ToQuat(worldMat, outQuat);
	}
	if (outVec) {
		copyVec3(worldMat[3], outVec);
	}
}


void oldencounter_GetStaticEncounterActorPosition(OldStaticEncounter *staticEnc, OldActor *actor, Vec3 outVec, Quat outQuat)
{
	Quat actorQuat;
	Mat4 actorMat, staticEncMat, worldMat;

	quatToMat(staticEnc->encRot, staticEncMat);
	copyVec3(staticEnc->encPos, staticEncMat[3]);

	oldencounter_GetActorPositionOffset(actor, actorQuat, actorMat[3]);
	quatToMat(actorQuat, actorMat);

	mulMat4(staticEncMat, actorMat, worldMat);
	if (outQuat) {
		mat3ToQuat(worldMat, outQuat);
	}
	if (outVec) {
		copyVec3(worldMat[3], outVec);
	}
}


void oldencounter_AttachActor(Entity* ent, OldEncounter* encounter, OldActor* actor, U32 teamSize)
{
	Critter* critter = SAFE_MEMBER(ent, pCritter);
	if (critter) {
		OldStaticEncounter* pStaticEnc = GET_REF(encounter->staticEnc);

		critter->encounterData.activeTeamSize = teamSize;
		critter->encounterData.sourceActor = actor;
		critter->encounterData.parentEncounter = encounter;
		oldencounter_GetEncounterActorPosition(encounter, actor, critter->encounterData.origPos, NULL);
		entity_SetDirtyBit(ent, parse_Critter, critter, false);
		critter->iEncounterKey = pStaticEnc ? hashString(pStaticEnc->name,true) : 0;

		eaPush(&encounter->ents, ent);
	}
}


static Entity* oldencounter_GetNemesisEntForSpawn(OldEncounter *pEncounter, EncounterDef *pEncDef)
{
	Entity *pEnt = NULL;

	if (pEncDef && pEncounter && pEncDef->pchOwnerMapVar) {
		pEnt = GET_REF(pEncounter->hOwner);
	} else {
		pEnt = partition_GetPlayerMapOwner(pEncounter->iPartitionIdx);

		if (!pEnt && pEncounter && pEncounter->spawningPlayer) {
			pEnt = entFromEntityRef(pEncounter->iPartitionIdx, pEncounter->spawningPlayer);
		}
	}
	return pEnt ? player_GetPrimaryNemesis(entFromContainerID(pEncounter->iPartitionIdx,entGetType(pEnt),entGetContainerID(pEnt))) : NULL;
}

static Entity* oldencounter_CreateNemesis(Entity *pNemesisEnt, const char *pchFSM, OldEncounter *pEnc, OldActor *pActor, GameEncounter *pEnc2, int iActorIndex, int iLevel, int iTeamSize, const char *pcSubRank, int iPartitionIdx, CritterFaction* pFaction, Message* pDisplayNameMsg, const char * pchSpawnAnim, F32 fSpawnTime, Entity* spawningPlayer, EntityRef erOwner, EntityRef erCreator, AITeam* aiTeam, const char* blameFile)
{
	Entity* pEnt = NULL;

	if (pNemesisEnt && pNemesisEnt->pNemesis){
		NemesisPowerSet *pPowerSet = nemesis_NemesisPowerSetFromName(pNemesisEnt->pNemesis->pchPowerSet);
		CritterDef *pCritterDef = pPowerSet?RefSystem_ReferentFromString(g_hCritterDefDict, pPowerSet->pcCritter):NULL;
		PlayerCostume *pCostume = costumeEntity_GetSavedCostume(pNemesisEnt, 0);
		const char *pchName = entGetPersistedName(pNemesisEnt);

		if (pPowerSet && pCritterDef){
			CritterCreateParams params = {0};

			params.enttype = GLOBALTYPE_ENTITYCRITTER;
			params.iPartitionIdx = iPartitionIdx;
			params.fsmOverride = pchFSM;
			params.pEncounter = pEnc;
			params.pActor = pActor;
			params.pEncounter2 = pEnc2;
			params.iActorIndex = iActorIndex;
			params.iLevel = iLevel;
			params.iTeamSize = iTeamSize;
			params.pcSubRank = pcSubRank;
			params.pFaction = pFaction;
			params.pDisplayNameMsg = pDisplayNameMsg;
			params.pchSpawnAnim = pchSpawnAnim;
			params.fSpawnTime = fSpawnTime;
			params.spawningPlayer = spawningPlayer;
			params.erOwner = erOwner;
			params.erCreator = erCreator;
			params.aiTeam = aiTeam;
			params.pCostume = pCostume;


			// Spawn the correct critter for this Nemesis Power Set
			pEnt = critter_CreateByDef(pCritterDef, &params, blameFile, true);


			// Override various things
			if (pEnt && pEnt->pCritter){
				char idBuf[128];

				pEnt->pCritter->voiceSet = nemesis_ChooseDefaultVoiceSet(pNemesisEnt);

				// A display name set from an Actor should still take precedence, I think
				if(!pDisplayNameMsg){
					pEnt->pCritter->displayNameOverride = StructAllocString(entGetPersistedName(pNemesisEnt));
				}

				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pNemesisEnt->myContainerID, idBuf), pEnt->pCritter->hSavedPet);

				COPY_HANDLE(CONTAINER_NOCONST(Entity, pEnt)->costumeRef.hMood, pNemesisEnt->costumeRef.hMood);

				pEnt->fHue = pNemesisEnt->pNemesis->fPowerHue;

				entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
			}
		}
	}
	return pEnt;
}

// TODO: On load, force the actor fields to exist
static bool oldencounter_SpawnActor(OldActor* actor, OldEncounter* encounter, EncounterDef* def, U32 critterLevel, CritterFaction* encFaction, CritterGroup* encCritterGroup,
								    CritterDef*** underlingList, CritterDef*** excludeList, char * encSpawnAnim, Entity* spawningPlayer, AITeam* aiTeam)
{
	Entity* newEnt = NULL;
	U32 numPlayers = oldencounter_GetActivePlayerCount(encounter);
	OldActorInfo* actorInfo = oldencounter_GetActorInfo(actor);
	OldActorAIInfo* actorAIInfo = oldencounter_GetActorAIInfo(actor);
	ContactDef* contactDef = GET_REF(actorInfo->contactScript);
	CritterDef* critterDef = GET_REF(actorInfo->critterDef);
	CritterGroup* critterGroup = GET_REF(actorInfo->critterGroup) ? GET_REF(actorInfo->critterGroup) : encCritterGroup;
	FSM* fsmOverride = GET_REF(actorAIInfo->hFSM);
	const char *spawnAnim = actorInfo->pchSpawnAnim ? actorInfo->pchSpawnAnim : NULL;
	F32 spawnLockdownTime = actorInfo->fSpawnLockdownTime;
	CritterFaction* faction = GET_REF(actorInfo->critterFaction);
	OldStaticEncounter *pStaticEnc = GET_REF(encounter->staticEnc);
	WorldRegionType eRegion = encounter->eRegionType;
	PlayerDifficultyMapData *pDifficultyMapData = NULL;

	PERFINFO_AUTO_START_FUNC();

	pDifficultyMapData = pd_GetDifficultyMapData(mapState_GetDifficulty(mapState_FromPartitionIdx(encounter->iPartitionIdx)), zmapInfoGetPublicName(NULL), eRegion);

	if (!faction) {
		faction = encFaction;
	}
	if (!spawnAnim) {
		spawnAnim = encSpawnAnim;
	}

	// Adjust critter level according to difficulty
	if (pDifficultyMapData) {
		critterLevel += pDifficultyMapData->iLevelModifier;
	}

	if (actorInfo->eCritterType == Actor1CritterType_Nemesis || (critterDef && !stricmp(critterDef->pchName, "Nemesis"))) {
		// Try to create a Nemesis
		Entity *pNemesisEnt = oldencounter_GetNemesisEntForSpawn(encounter, def);
		if (pNemesisEnt) {
			newEnt = oldencounter_CreateNemesis(pNemesisEnt, fsmOverride?fsmOverride->name:NULL, encounter, actor, NULL, 0, critterLevel, numPlayers, actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank, encounter->iPartitionIdx, faction, GET_REF(actor->displayNameMsg.hMessage), spawnAnim, spawnLockdownTime, spawningPlayer, 0, 0, aiTeam, def->filename);
		}

		if (!newEnt && critterDef) {
			CritterCreateParams createParams = {0};
			
			createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
			createParams.iPartitionIdx = encounter->iPartitionIdx;
			createParams.fsmOverride = fsmOverride?fsmOverride->name:NULL;
			createParams.pEncounter = encounter;
			createParams.pActor = actor;
			createParams.iLevel = critterLevel;
			createParams.iTeamSize = numPlayers;
			createParams.pcSubRank = actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank;
			createParams.pFaction = faction;
			createParams.pCritterGroupDisplayNameMsg = GET_REF(actor->critterGroupDisplayNameMsg.hMessage);
			createParams.pDisplayNameMsg = GET_REF(actor->displayNameMsg.hMessage);
			createParams.pchSpawnAnim = spawnAnim;
			createParams.fSpawnTime = spawnLockdownTime;
			createParams.spawningPlayer = spawningPlayer;
			createParams.aiTeam = aiTeam;
			newEnt = critter_CreateByDef(critterDef, &createParams, def->filename, true);
		}

		if (!newEnt) {
			ErrorFilenamef(def->filename, "Tried to spawn a Nemesis for encounter %s, but no Nemesis found!", pStaticEnc->name);
		}

	} else if (actorInfo->eCritterType == Actor1CritterType_NemesisMinion || (critterGroup && !critterDef && !stricmp(critterGroup->pchName, "NemesisMinions"))) {
		// Try to create a Nemesis Minion
		Entity *pNemesisEnt = oldencounter_GetNemesisEntForSpawn(encounter, def);
		if (pNemesisEnt) {
			newEnt = critter_CreateNemesisMinion(pNemesisEnt, actorInfo->pcCritterRank ? actorInfo->pcCritterRank : g_pcCritterDefaultRank, fsmOverride?fsmOverride->name:NULL, encounter, actor, NULL, 0, critterLevel, numPlayers, actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank, encounter->iPartitionIdx, faction, GET_REF(actor->displayNameMsg.hMessage), excludeList, spawnAnim, spawnLockdownTime, spawningPlayer, aiTeam);
		}

		if (!newEnt && critterGroup) {
			newEnt = critter_FindAndCreate(critterGroup, actorInfo->pcCritterRank ? actorInfo->pcCritterRank : g_pcCritterDefaultRank, encounter, actor, NULL, 0, critterLevel, numPlayers, actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank, GLOBALTYPE_ENTITYCRITTER, encounter->iPartitionIdx, fsmOverride?fsmOverride->name:NULL, faction, GET_REF(actor->critterGroupDisplayNameMsg.hMessage), GET_REF(actor->displayNameMsg.hMessage), 0, excludeList, spawnAnim, spawnLockdownTime, spawningPlayer, aiTeam, NULL, NULL);
		}

		if (!newEnt) {
			ErrorFilenamef(def->filename, "Tried to spawn a Nemesis Minion for encounter %s, but no Nemesis found!", pStaticEnc->name);
		}

	} else {
		if (critterDef){
			CritterCreateParams createParams = {0};

			createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
			createParams.iPartitionIdx = encounter->iPartitionIdx;
			createParams.fsmOverride = fsmOverride?fsmOverride->name:NULL;
			createParams.pEncounter = encounter;
			createParams.pActor = actor;
			createParams.iLevel = critterLevel;
			createParams.iTeamSize = numPlayers;
			createParams.pcSubRank = actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank;
			createParams.pFaction = faction;
			createParams.pCritterGroupDisplayNameMsg = GET_REF(actor->critterGroupDisplayNameMsg.hMessage);
			createParams.pDisplayNameMsg = GET_REF(actor->displayNameMsg.hMessage);
			createParams.pchSpawnAnim = spawnAnim;
			createParams.fSpawnTime = spawnLockdownTime;
			createParams.spawningPlayer = spawningPlayer;
			createParams.aiTeam = aiTeam;

			newEnt = critter_CreateByDef(critterDef, &createParams, def->filename, true);

		} else {
			int i, numUnderlings = eaSize(underlingList);
			CritterCreateParams createParams = {0};

			createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
			createParams.iPartitionIdx = encounter->iPartitionIdx;
			createParams.fsmOverride = fsmOverride?fsmOverride->name:NULL;
			createParams.pEncounter = encounter;
			createParams.pActor = actor;
			createParams.iLevel = critterLevel;
			createParams.iTeamSize = numPlayers;
			createParams.pcSubRank = actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank;
			createParams.pFaction = faction;
			createParams.pCritterGroupDisplayNameMsg = GET_REF(actor->critterGroupDisplayNameMsg.hMessage);
			createParams.pDisplayNameMsg = GET_REF(actor->displayNameMsg.hMessage);
			createParams.pchSpawnAnim = spawnAnim;
			createParams.fSpawnTime = spawnLockdownTime;
			createParams.spawningPlayer = spawningPlayer;
			createParams.aiTeam = aiTeam;

			for (i = 0; i < numUnderlings; i++) {
				if (critterRankEquals((*underlingList)[i]->pcRank, actorInfo->pcCritterRank)) {
					newEnt = critter_CreateByDef((*underlingList)[i], &createParams, def->filename, true);
					eaRemove(underlingList, i);
					break;
				}
			}

			if (!newEnt){
				newEnt = critter_FindAndCreate(critterGroup, actorInfo->pcCritterRank ? actorInfo->pcCritterRank : g_pcCritterDefaultRank, encounter, actor, NULL, 0, critterLevel, numPlayers, actorInfo->pcCritterSubRank ? actorInfo->pcCritterSubRank : g_pcCritterDefaultSubRank, GLOBALTYPE_ENTITYCRITTER, encounter->iPartitionIdx, fsmOverride?fsmOverride->name:NULL, faction, GET_REF(actor->critterGroupDisplayNameMsg.hMessage), GET_REF(actor->displayNameMsg.hMessage), 0, excludeList, spawnAnim, spawnLockdownTime, spawningPlayer, aiTeam, NULL, NULL);
			}
		}
	}

	if (newEnt && newEnt->pCritter) {
		Quat newEntRot;
		Vec3 newEntPos;

		ANALYSIS_ASSUME(newEnt != NULL);
		// Get the actor position in the world
		oldencounter_GetEncounterActorPosition(encounter, actor, newEntPos, newEntRot);

		// Snap the actor's position to the ground in case the terrain got moved or something
		if(GET_REF(encounter->staticEnc) && !GET_REF(encounter->staticEnc)->bNoSnapToGround) {
			S32 bFloorFound = false;
			worldSnapPosToGround(encounter->iPartitionIdx, newEntPos, FIRST_ACTOR_SPAWN_SNAP_TO_DIST, -FIRST_ACTOR_SPAWN_SNAP_TO_DIST, &bFloorFound);

			// If we couldn't find any ground for the actor, try again with a bigger delta
			if (!bFloorFound) {
				worldSnapPosToGround(encounter->iPartitionIdx, newEntPos, SECOND_ACTOR_SPAWN_SNAP_TO_DIST, -SECOND_ACTOR_SPAWN_SNAP_TO_DIST, &bFloorFound);
			}

			if (bFloorFound) {
				// If floor was hit, add a y-bias
				vecY(newEntPos) += 0.1;
				if (gConf.bNewAnimationSystem) {
					mrSurfaceSetSpawnedOnGround(newEnt->mm.mrSurface, true);
				}
			}
		} else {
			S32 bFloorFound = false;
			worldSnapPosToGround(encounter->iPartitionIdx, newEntPos, ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_UP, -ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_DOWN, &bFloorFound);

			if (bFloorFound) {
				// If floor was hit, add a y-bias
				vecY(newEntPos) += 0.1;
				if (gConf.bNewAnimationSystem) {
					mrSurfaceSetSpawnedOnGround(newEnt->mm.mrSurface, true);
				}
			}
		}

		entSetPos(newEnt, newEntPos, 1, __FUNCTION__);
		if (!quatIsNormalized(newEntRot)) {
			quatNormalize(newEntRot);
		}
		entSetRot(newEnt, newEntRot, 1, __FUNCTION__);
		if (oldencounter_ShouldUseBossBar(actor, numPlayers)) {
			MultiVal intVal = {0};
			MultiValSetInt(&intVal, 1);
			entSetUIVar(newEnt, "Boss", &intVal);
		}
		if (!critterDef) {
			critterDef = GET_REF(newEnt->pCritter->critterDef);
		}
		if (critterDef) {
			int i, size = eaSize(&critterDef->ppUnderlings);
			for (i = 0; i < size; i++) {
				CritterDef *underling = critter_DefGetByName(critterDef->ppUnderlings[i]);
				if (underling) {
					eaPush(underlingList, underling);
				}
			}
		}

		// If the actor is a contact, mark the critter as interactable
		if (oldencounter_HasInteractionProperties(actor)) {
			newEnt->pCritter->bIsInteractable = true;
		}

		// Set the gang, unless this is a non-combat entity (in which case the gang doesn't matter)
		if (newEnt->pChar && def->gangID) {
			newEnt->pChar->gangID = def->gangID;
			entity_SetDirtyBit(newEnt, parse_Character, newEnt->pChar, false);
		}

		// See if we've exceeded a spawn limit
		if (critterDef && critterDef->iSpawnLimit > 0) {
			int i, n = eaSize(&encounter->ents);
			int count = 0;

			for(i=0; i<n; i++) {
				CritterDef* entDef = GET_REF(encounter->ents[i]->pCritter->critterDef);
				if (entDef == critterDef) {
					++count;
				}
			}
			if (critterDef && (count >= critterDef->iSpawnLimit)) {
				eaPush(excludeList, critterDef);
			}
		}

		// set difficulty modifiers
		if (newEnt->pCritter) {
			PowerDef *pDifficultyPower = SAFE_GET_REF(pDifficultyMapData, hPowerDef);
			if (pDifficultyMapData) {
				newEnt->pCritter->fNumericRewardScale = pDifficultyMapData->fNumericRewardScale;
				{
					RewardTableRef* pRef = StructCreate(parse_RewardTableRef);
					COPY_HANDLE(pRef->hRewardTable, pDifficultyMapData->hRewardTable);
					eaPush(&newEnt->pCritter->eaAdditionalRewards, pRef);
				}
			}

			// add powers from difficulty setting
			if (pDifficultyPower && newEnt->pChar) {
				character_AddPowerPersonal(encounter->iPartitionIdx,newEnt->pChar, pDifficultyPower, 0, true, NULL);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	return !!newEnt;
}


void oldencounter_Spawn(EncounterDef *def, OldEncounter* encounter)
{
	int num_living_ents = oldencounter_NumLivingEnts(encounter);

	PERFINFO_AUTO_START_FUNC();

	if (def && num_living_ents < 100) {
		int i, prevNumEnts = eaSize(&encounter->ents), n = eaSize(&def->actors);
		U32 encounterLevel = oldencounter_GetLevel(encounter);
		CritterFaction* encFaction = oldencounter_GetFaction(encounter);
		CritterGroup* encCritterGroup = oldencounter_GetCritterGroup(encounter);
		char * encSpawnAnim = oldencounter_GetSpawnAnim(encounter);
		CritterDef** underlingList = 0;
		CritterDef** excludeList = NULL;	// CritterDefs that shoudn't be spawned
		int spawned = false;

		AITeam* team = aiTeamCreate(encounter->iPartitionIdx, NULL, false);

		// Sort all actors in order of descending strength.  This ensures
		// the correct behavior when spawning Underlings.
		// Set an encounter on the static variable as a kludge
		s_sortEncounter = encounter;
		eaQSort(def->actors, oldencounter_CmpActorStrength);

		// Place all actors from strongest to weakest
		for (i = 0; i < n; i++) {
			if (oldencounter_ShouldSpawnActor(encounter, def->actors[i])) {
				bool did_spawn = oldencounter_SpawnActor(def->actors[i], encounter, def, encounterLevel, encFaction, encCritterGroup, &underlingList, &excludeList, encSpawnAnim, entFromEntityRef(encounter->iPartitionIdx, encounter->spawningPlayer), team);
				spawned = spawned || did_spawn;
			}
		}

		eaDestroy(&underlingList);
		eaDestroy(&excludeList);

		if (spawned) {
			aiTeamAddJobs(team, def->encJobs, def->filename);
		}

		encounter->lastSpawned = timeSecondsSince2000();
		stashTableClear(encounter->eventLogSinceSpawn);
		oldencounter_StateTransition(encounter, EncounterState_Spawned);

		if (!aiTeamGetMemberCount(team)) {
			aiTeamDestroy(team);
		}
	}
	PERFINFO_AUTO_STOP();
}


OldEncounter** oldencounter_GetEncountersWithinDistance(OldEncounterPartitionState *pState, const Vec3 pos, F32 distance)
{
	static OldEncounter** nearbyEncs = NULL;
	eaClearFast(&nearbyEncs);
	octreeFindInSphereEA(pState->pEncounterOctree, &nearbyEncs, pos, distance, NULL, NULL);
	return nearbyEncs;
}


int oldencounter_GetNumEncounters(int iPartitionIdx)
{
	OldEncounterPartitionState *pState = oldencounter_GetPartitionState(iPartitionIdx);
	return eaSize(&pState->eaEncounters);
}


// The encounter is about to spawn; find a player nearby who can spawn it.  For many encounters, this means any
// player at all
static bool oldencounter_FindSpawningPlayer(EncounterDef *def, OldEncounter* encounter)
{
	// TODO: can this be cached?
	Entity*** nearbyPlayers;
	int i, n;
	bool playerFound = false;

	PERFINFO_AUTO_START_FUNC();

	nearbyPlayers = oldencounter_GetNearbyPlayers(encounter, encounter->spawnRadius);
	n = eaSize(nearbyPlayers);

	// Checking the spawnWhen expression requires setting up the encounter's context to use player pointers
	if (def->bCheckSpawnCondPerPlayer) {
		exprContextSetFuncTable(encounter->context, encounter_CreatePlayerExprFuncTable());
	}

	// For each player, see if this encounter can spawn for them
	for(i=0; (!playerFound && i<n); i++) {
		Entity* playerEnt = (*nearbyPlayers)[i];
		playerFound = true;

		// Check the spawnWhen expression if it needs to be checked
		if(def->bCheckSpawnCondPerPlayer && def->spawnCond) {
			exprContextSetSelfPtr(encounter->context, playerEnt);
			exprContextSetPointerVarPooled(encounter->context, g_PlayerVarName, playerEnt, NULL, false, true);
			playerFound = oldencounter_EvalExpression(encounter, def->spawnCond);
		}

		// If this is an ambush, make the ambush roll.  This may affect the player's ambush cooldown, so don't make
		// this roll until after the spawn condition is true
		// Note that if the encounter has a spawn chance or lockout or something, it may trigger the player's cooldown
		// without spawning.
		if (playerFound && def->bAmbushEncounter) {
			if (!encounter_TryPlayerAmbush(playerEnt)) {
				playerFound = false;
			}
		}

		// If we've found a match, set the spawning player for this encounter
		if (playerFound) {
			encounter->spawningPlayer = playerEnt->myRef;
		}
	}

	// Remove any players from the expression context and reset the context's function table
	exprContextRemoveVar(encounter->context, "Player");
	exprContextSetFuncTable(encounter->context, encounter_CreateExprFuncTable());

	PERFINFO_AUTO_STOP();

	return playerFound;
}


// Check that this encounter is allowed to try to spawn.
// If this check fails, the encounter will keep trying to spawn every time a player enters its radius
// Compare with encounter_ValidateSpawnConditions below (which shuts the encounter off if it fails)
static bool oldencounter_ValidatePreSpawnConditions(EncounterDef *def, OldEncounter* encounter, bool bCutSceneNearby)
{
	Entity *pOwner = NULL;
	bool spawningPlayerFound;

	if (!def) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	if (def->pchOwnerMapVar) {
		MapVariable *pMapVar = mapvariable_GetByName(encounter->iPartitionIdx, def->pchOwnerMapVar);
		if (pMapVar && pMapVar->pVariable 
			&& pMapVar->pVariable->eType == WVAR_PLAYER 
			&& pMapVar->pVariable->uContainerID != 0)
		{
			char idBuf[128];
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pMapVar->pVariable->uContainerID, idBuf), encounter->hOwner);
			
			// Don't allow this encounter to spawn until the Owner is loaded
			pOwner = GET_REF(encounter->hOwner);
			if (!pOwner 
				|| ( player_GetPrimaryNemesisID(pOwner) 
					 && !player_GetPrimaryNemesis(entFromContainerID(encounter->iPartitionIdx, entGetType(pOwner), entGetContainerID(pOwner)))
					) ) {
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	// If the spawn condition can be evaluated once for all, do so
	if (!def->bCheckSpawnCondPerPlayer && def->spawnCond) {
		// Try to avoid spamming the spawn condition expression
		U32 uCurrentTime = timeSecondsSince2000();
		if (uCurrentTime - encounter->lastSpawnExprCheck < MIN_SPAWN_COND_CHECK_SECONDS && !bCutSceneNearby) {
			PERFINFO_AUTO_STOP();
			return false;
		}
		encounter->lastSpawnExprCheck = uCurrentTime;

		if (!oldencounter_EvalExpression(encounter, def->spawnCond)) {
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// Find a player that can spawn this encounter.  For most encounters, this is trivial (find any nearby player)
	spawningPlayerFound = oldencounter_FindSpawningPlayer(def, encounter);

	// Some encounters won't spawn if there isn't a valid player nearby
	if (!spawningPlayerFound && (def->bCheckSpawnCondPerPlayer || def->bAmbushEncounter)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}


// Check that this encounter is allowed to spawn.
// If this check fails, the encounter will shut down and can't be spawned again until its respawn timer
// has elapsed (if this map supports respawn).
// Compare with encounter_ValidatePreSpawnConditions above (which keeps being tested until it's true)
static bool oldencounter_ValidateSpawnConditions(OldEncounterPartitionState *pState, EncounterDef *def, OldEncounter* encounter)
{
	OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
	OldEncounterGroup* encGroup = NULL;

	if (!def) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC();
	
	// Check to see if the encounter will go off
	if (def->spawnChance < 100) {
		U32 probRoll = 1 + (randomInt() % 100);
		U32 probability = CLAMP(def->spawnChance, 0, 100);
		if (probRoll > probability) {
			sprintf(encounter->status, "Failed Roll: %i > %i", probRoll, probability);
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// If lockout is enabled, check that there isn't already another active encounter nearby
	if (staticEnc && staticEnc->layerParent->useLockout && def->lockoutRadius > 0) {
		OldEncounter** nearbyEncs = oldencounter_GetEncountersWithinDistance(pState, encounter->mat[3], def->lockoutRadius);
		int i, n = eaSize(&nearbyEncs);
		for (i = 0; i < n; i++) {
			OldEncounter* currEnc = nearbyEncs[i];
			if (oldencounter_IsRunning(currEnc) && (!encounter->encGroup || (encounter->encGroup != currEnc->encGroup))) {
				sprintf(encounter->status, "Too Close");
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


static bool oldencounter_IsRespawnEnabled(void)
{
	return (zmapInfoGetMapType(NULL) == ZMTYPE_STATIC) || (zmapInfoGetMapType(NULL) == ZMTYPE_SHARED);
}


void oldencounter_Complete(EncounterDef *def, OldEncounter* encounter, EncounterState state)
{
	PERFINFO_AUTO_START_FUNC();

	encounter->lastCompleted = timeSecondsSince2000();

	// If respawn is enabled for this encounter, put it in the queue to respawn
	if (oldencounter_IsRespawnEnabled() && def && def->respawnTimer != 0) {
		encounter->fSpawnTimer = def->respawnTimer;
	}

	oldencounter_StateTransition(encounter, state);

	// Clear sinceComplete event log
	stashTableClear(encounter->eventLogSinceComplete);

	PERFINFO_AUTO_STOP();
}


static void oldencounter_UpdateState(OldEncounterPartitionState *pState, OldEncounter* encounter)
{
	EncounterDef* def;

	// Now update the encounter based on which state it is in
    if (oldencounter_IsWaitingToSpawn(encounter)) {
		bool bShouldSpawn = false;
		bool bCutSceneNearby;

		PERFINFO_AUTO_START("oldencounter_UpdateState: waiting to spawn", 1);

		// Waiting encounters spawn as soon as their pre-spawn validation check passes (below)
		bCutSceneNearby = cutscene_GetNearbyCutscenes(encounter->iPartitionIdx, encounter->mat[3], encounter->spawnRadius);

		// Sleeping encounters have to have someone nearby to spawn, unless they're part of a zone event
		if ((encounter->state == EncounterState_Asleep) && 
			!encounter->iNumNearbyPlayers &&
			!bCutSceneNearby &&
			!g_encounterIgnoreProximity) 
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		// All encounters need to pass a pre-spawn condition check.  This checks the spawnCondition, and also checks
		// that there are players nearby (for ambush spawns or spawns that are checked per player).
		def = oldencounter_GetDef(encounter);
		if (!oldencounter_ValidatePreSpawnConditions(def, encounter, bCutSceneNearby)) {
			PERFINFO_AUTO_STOP();
			return;			
		}

		if (oldencounter_ValidateSpawnConditions(pState, def, encounter)) {
			bShouldSpawn = true;
		} else {
			oldencounter_Complete(def, encounter, EncounterState_Off);
		}

		PERFINFO_AUTO_STOP();

		if (bShouldSpawn) {
			PERFINFO_AUTO_START("oldencounter_UpdateState: spawning", 1);

			if (encounter->lastSleep && (encounter->lastSleep > encounter->lastCompleted)) {
				U32 uTimeDiff = timeSecondsSince2000() - encounter->lastSleep;
				if (uTimeDiff < 15) {
					ADD_MISC_COUNT(1, "Spawn After Sleep < 15 sec");
				} else if (uTimeDiff < 60) {
					ADD_MISC_COUNT(1, "Spawn After Sleep < 60 sec");
				} else if (uTimeDiff < 300) {
					ADD_MISC_COUNT(1, "Spawn After Sleep < 300 sec");
				} else {
					ADD_MISC_COUNT(1, "Spawn After Sleep > 300 sec");
				}
			} else if (encounter->lastCompleted && (encounter->lastCompleted > encounter->lastSleep)) {
				ADD_MISC_COUNT(1, "Spawn After Completed");
			} else {
				ADD_MISC_COUNT(1, "Spawn First Time");
			}

			oldencounter_Spawn(def, encounter);
			if (def->waveCond && def->waveInterval > 0) {
				encounter->fSpawnTimer = def->waveInterval;
			}

			PERFINFO_AUTO_STOP();
		}

	} else if (oldencounter_IsRunning(encounter)) {
		PERFINFO_AUTO_START("oldencounter_UpdateState: running", 1);

		// Check success condition before failure condition
		def = oldencounter_GetDef(encounter);
		if (def->successCond && oldencounter_EvalExpression(encounter, def->successCond)) {
			oldencounter_Complete(def, encounter, EncounterState_Success);
		} else if (!def->successCond && oldencounter_AllEntsAreDead(encounter)) {
			oldencounter_Complete(def, encounter, EncounterState_Success);
		} else if (def->failCond && oldencounter_EvalExpression(encounter, def->failCond)) {
			oldencounter_Complete(def, encounter, EncounterState_Failure);
		} else if (def->waveCond && def->waveInterval > 0 && encounter->fSpawnTimer <= 0) {
			if (encounter->bWaveReady) {
				// If the encounter is already scheduled to spawn, just do it.
				oldencounter_Spawn(def, encounter);
				encounter->fSpawnTimer = def->waveInterval;
				encounter->bWaveReady = false;

			} else if (oldencounter_EvalExpression(encounter, def->waveCond)) {
				int delay = randomIntRange(def->waveDelayMin, def->waveDelayMax);
				if (delay) { // Schedule the encounter to spawn after a delay
					encounter->bWaveReady = true;
					encounter->fSpawnTimer = delay;

				} else { // spawn the encounter right away
					oldencounter_Spawn(def, encounter);
					encounter->fSpawnTimer = def->waveInterval;
				}

			} else { // It's time to spawn, but condition hasn't been met yet.  Poll until condition is met.
				encounter->fSpawnTimer = WAVE_ATTACK_POLL_INTERVAL;
			}
		}

		PERFINFO_AUTO_STOP();

	} else if (oldencounter_IsComplete(encounter)) {
		PERFINFO_AUTO_START("oldencounter_UpdateState: complete", 1);
		def = oldencounter_GetDef(encounter);
		if (oldencounter_IsRespawnEnabled() && (def->respawnTimer != 0) && (encounter->fSpawnTimer <= 0)) {
			oldencounter_Reset(encounter);
		}
		PERFINFO_AUTO_STOP();
	}
}


static void oldencounter_CheckFallThroughWorld(OldEncounter *encounter)
{
	int i;

	// Don't check for falling through world for entities in space region
	if (encounter->eRegionType == WRT_Space) {
		return;
	}

	// Scan all living entities
	for (i = eaSize(&encounter->ents)-1; i>=0; --i) {
		Entity *ent = encounter->ents[i];
		if (entIsAlive(ent) && ent->pCritter) {
			CritterEncounterData *pEncounterData = &ent->pCritter->encounterData;

			if (pEncounterData->sourceActor && pEncounterData->activeTeamSize) {
				Vec3 entPos;

				entGetPos(ent, entPos);

				if (entPos[1] - pEncounterData->origPos[1] < -ACTOR_FALLTHROUGHWORLD_CHECKDIST) {
					OldStaticEncounter *staticEnc = GET_REF(encounter->staticEnc);
					char *actorName = NULL;
					estrStackCreate(&actorName);

					oldencounter_GetActorName(pEncounterData->sourceActor, &actorName);
					
					ErrorDetailsf("Error: %s::%s has fallen through the world!\n(%.2f %.2f %.2f - %.2f %.2f %.2f)", 
						staticEnc->name, actorName, vecParamsXYZ(entPos), vecParamsXYZ(pEncounterData->origPos));

					ErrorFilenamef(staticEnc->pchFilename, "Error: Critter from encounter '%s' has fallen through the world!", 
						staticEnc->name);

					oldencounter_RemoveActor(ent);
					gslQueueEntityDestroy(ent);
					estrDestroy(&actorName);
				}
			}
		}
	}
}


OldEncounter* oldencounter_FromStaticEncounterIfExists(int iPartitionIdx, OldStaticEncounter* staticEnc)
{
	OldEncounterPartitionState *pState = eaGet(&s_eaOldEncounterPartitionStates, iPartitionIdx);
	OldEncounter *pEncounter = NULL;
	if (pState) {
		stashAddressFindPointer(pState->pEncounterFromStaticEncounterHash, staticEnc, &pEncounter);
	}
	return pEncounter;
}


OldEncounter* oldencounter_FromStaticEncounter(int iPartitionIdx, OldStaticEncounter* staticEnc)
{
	OldEncounterPartitionState *pState = oldencounter_GetPartitionState(iPartitionIdx);
	OldEncounter *pEncounter;
	stashAddressFindPointer(pState->pEncounterFromStaticEncounterHash, staticEnc, &pEncounter);
	return pEncounter;
}


OldEncounter* oldencounter_FromStaticEncounterNameIfExists(int iPartitionIdx, const char* staticEncName)
{
	OldStaticEncounter* staticEnc = oldencounter_StaticEncounterFromName(staticEncName);
	if (!staticEnc) {
		return NULL;
	}
	return oldencounter_FromStaticEncounterIfExists(iPartitionIdx, staticEnc);
}


OldEncounter* oldencounter_FromStaticEncounterName(int iPartitionIdx, const char* staticEncName)
{
	OldStaticEncounter* staticEnc = oldencounter_StaticEncounterFromName(staticEncName);
	if (!staticEnc) {
		return NULL;
	}
	return oldencounter_FromStaticEncounter(iPartitionIdx, staticEnc);
}


U32 oldencounter_ActorGetMaxFSMCost(OldActor* actor)
{
	int cost = 1;
	
	if (!actor) {
		return 0;
	}

	if (actor->details.aiInfo) {
		FSM* fsm = GET_REF(actor->details.aiInfo->hFSM);
		if (fsm && fsm->cost > cost) {
			cost = fsm->cost;
		}
	}

	return cost;
}


static U32 oldencounter_EncounterDefGetFSMCost(EncounterDef* encDef)
{
	int i, n, cost = 0;

	if (!encDef) {
		return 0;
	}

	n = eaSize(&encDef->actors);
	for(i=0; i<n; i++) {
		cost += oldencounter_ActorGetMaxFSMCost(encDef->actors[i]);
	}

	return cost;
}


U32 oldencounter_GetPotentialFSMCost(int iPartitionIdx)
{
	OldEncounterPartitionState *pState = oldencounter_GetPartitionState(iPartitionIdx);
	int i, n = eaSize(&pState->eaEncounters);
	int cost = 0;

	for (i = 0; i < n; i++) {
		OldStaticEncounter* staticEnc = GET_REF(pState->eaEncounters[i]->staticEnc);
		if (staticEnc) {
			cost += oldencounter_EncounterDefGetFSMCost(staticEnc->spawnRule);
		}
	}
	return cost;
}


static void oldencounter_GetAllSpawnedEntitiesFromEnc(OldEncounter* encounter, Entity*** entities)
{
	int i, n = eaSize(&encounter->ents);
	for(i=0; i<n; i++) {
		eaPush(entities, encounter->ents[i]);
	}
}


void oldencounter_GetAllSpawnedEntities(int iPartitionIdx, Entity*** entities)
{
	OldEncounterPartitionState *pState = oldencounter_GetPartitionState(iPartitionIdx);
	int i, n = eaSize(&pState->eaEncounters);

	for (i = 0; i < n; i++)
	{
		oldencounter_GetAllSpawnedEntitiesFromEnc(pState->eaEncounters[i], entities);
	}
}


int oldencounter_GetNumRunningEncounters(int iPartitionIdx)
{
	OldEncounterPartitionState *pState = oldencounter_GetPartitionState(iPartitionIdx);
	int i, n = eaSize(&pState->eaEncounters);
	int count = 0;

	for(i=0; i<n; i++) {
		if (oldencounter_IsRunning(pState->eaEncounters[i])) {
			count++;
		}
	}

	return count;
}


// TODO: These rules are not yet defined so it just returns everyone nearby
void oldencounter_GetRewardedPlayers(OldEncounter* encounter, Entity*** rewardedPlayers)
{
	int i, n;
	oldencounter_GetRewardedEnts(encounter, rewardedPlayers);
	
	// remove those that aren't players
	n = eaSize(rewardedPlayers);
	for (i = n-1; i >= 0; --i) {
		if (!(*rewardedPlayers)[i]->pPlayer) {
			eaRemove(rewardedPlayers, i);
		}
	}
}


void oldencounter_GetRewardedEnts(OldEncounter* encounter, Entity*** rewardedEnts)
{
	int i, n = eaiSize(&encounter->entsWithCredit);
	for (i = 0; i < n; i++) {
		Entity *ent = NULL;
		if (ent = entFromEntityRef(encounter->iPartitionIdx, encounter->entsWithCredit[i])) {
			eaPush(rewardedEnts, ent);
		}
	}
}


void oldencounter_GatherBeaconPositions(void)
{
	int i, j, k;

	// Add the spawn information from all the encounter layers
	for(i=eaSize(&g_EncounterMasterLayer->encLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = g_EncounterMasterLayer->encLayers[i];
		for(j=eaSize(&pLayer->staticEncounters)-1; j>=0; --j) {
			char actorIDStr[1024];
			OldStaticEncounter *pStaticEnc = pLayer->staticEncounters[j];

			for(k=eaSize(&pStaticEnc->spawnRule->actors)-1; k>=0; --k) {
				OldActor *pActor = pStaticEnc->spawnRule->actors[k];
				if (pActor->details.position) {
					Quat qRot;
					Vec3 vActorPos;

					oldencounter_GetStaticEncounterActorPosition(pStaticEnc, pActor, vActorPos, qRot);

					sprintf(actorIDStr, "%s:%i", pStaticEnc->name, pActor->uniqueID);
					beaconAddCritterSpawn(vActorPos, actorIDStr);
				}
			}

			for(k=eaSize(&pStaticEnc->spawnRule->namedPoints)-1; k>=0; --k) {
				OldNamedPointInEncounter *np = pStaticEnc->spawnRule->namedPoints[k];

				assert(np->hasAbsLoc);
				beaconAddUsefulPoint(np->absLocation[3]);
			}
		}
	}
}


void oldencounter_RemoveActor(Entity* ent)
{
	Critter* critter = SAFE_MEMBER(ent, pCritter);
	if (critter) {
		if (critter->encounterData.parentEncounter) {
			eaFindAndRemove(&critter->encounterData.parentEncounter->ents, ent);
			critter->encounterData.parentEncounter = NULL;
		}
		critter->encounterData.sourceActor = NULL;
	}
}


Entity* oldencounter_EntFromEncActorName(int iPartitionIdx, const char *pchStaticEncName, const char *pchActorName)
{
	OldEncounter *pEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pchStaticEncName);
	if (pEncounter && pchActorName) {
		int i;
		for (i = eaSize(&pEncounter->ents)-1; i>=0; --i) {
			if (pEncounter->ents[i] && pEncounter->ents[i]->pCritter && pEncounter->ents[i]->pCritter->encounterData.sourceActor
				&& pEncounter->ents[i]->pCritter->encounterData.sourceActor->name
				&& !stricmp(pEncounter->ents[i]->pCritter->encounterData.sourceActor->name, pchActorName)) 
			{
				return pEncounter->ents[i];
			}
		}
	}
	return NULL;
}


void oldencounter_ActorPosFromEncActorName(int iPartitionIdx, const char *pchStaticEncName, const char *pchActorName, Vec3 outPos)
{
	OldEncounter *pEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pchStaticEncName);
	if (pEncounter && pchActorName) {
		OldStaticEncounter *pStaticEnc = GET_REF(pEncounter->staticEnc);
		if (pStaticEnc && pStaticEnc->spawnRule) {
			OldActor *pActor = oldencounter_FindDefActorByName(pStaticEnc->spawnRule, pchActorName);
			if (pActor) {
				oldencounter_GetEncounterActorPosition(pEncounter, pActor, outPos, NULL);
			}
		}
	}
}


static bool oldencounter_IsOnSameGangOrFaction(OldEncounter *pEncA, OldEncounter *pEncB)
{
	if (pEncA->uMajorityGangID || pEncB->uMajorityGangID) {
		return pEncA->uMajorityGangID == pEncB->uMajorityGangID;
	}
	return GET_REF(pEncA->hMajorityFaction) && (GET_REF(pEncA->hMajorityFaction) == GET_REF(pEncB->hMajorityFaction));
}


static void oldencounter_OldUpdateDynamicSpawnRate(OldEncounterPartitionState *pState, OldEncounter *pEnc)
{
	EncounterDef *pDef;

	// Skip if not waiting to respawn, or already respawning
	if (!pEnc || !oldencounter_IsComplete(pEnc) || (pEnc->fSpawnTimer <= 0)) {
		return;
	}

	// Get here if there is a respawn timer running
	pDef = oldencounter_GetDef(pEnc);
	if (pDef && oldencounter_IsDefDynamicSpawn(pDef)) {
		OldEncounter** eaNearbyEncs = NULL;
		int iNumEnabledEncs = 0, iNumOnCooldown = 0;
		int i;

		eaNearbyEncs = oldencounter_GetEncountersWithinDistance(pState, pEnc->mat[3], pEnc->spawnRadius);

		for (i = eaSize(&eaNearbyEncs)-1; i >= 0; --i) {
			EncounterDef *pOtherDef = oldencounter_GetDef(eaNearbyEncs[i]);
			if (eaNearbyEncs[i] && eaNearbyEncs[i] != pEnc && oldencounter_IsOnSameGangOrFaction(pEnc, eaNearbyEncs[i]) && pOtherDef && oldencounter_IsDefDynamicSpawn(pOtherDef)) {
				if (eaNearbyEncs[i]->fSpawnTimer > 0 && (eaNearbyEncs[i]->state == EncounterState_Success || eaNearbyEncs[i]->state == EncounterState_Failure)) {
					iNumOnCooldown++;
				}
				if (eaNearbyEncs[i]->state != EncounterState_Off && eaNearbyEncs[i]->state != EncounterState_Disabled && eaNearbyEncs[i]->state != EncounterState_Waiting) {
					iNumEnabledEncs++;
				}
			}
		}

		// Hack to smooth out behavior with small numbers of encounters
		// If there are less than 5 encounters nearby, always act as if there are at least 5
		if (iNumEnabledEncs < 5) {
			iNumEnabledEncs = 5;
		}

		if (iNumEnabledEncs && g_fDynamicRespawnScale > 1.f) {
			F32 fRatio = ((F32)iNumOnCooldown/(F32)iNumEnabledEncs);
			pEnc->fSpawnRateMultiplier = 1.f + (g_fDynamicRespawnScale-1.f)*fRatio;
		} else {
			pEnc->fSpawnRateMultiplier = 0.f;
		}
	} else {
		pEnc->fSpawnRateMultiplier = 0.f;
	}
}


WorldScopeNamePair **oldencounter_GetEncounterScopeNames(OldStaticEncounter *encounter)
{
	PERFINFO_AUTO_START_FUNC();

	if (!encounter->eaScopeNames) {
		WorldScopeNamePair *scopeName = StructCreate(parse_WorldScopeNamePair);
		eaPush(&encounter->eaScopeNames, scopeName);
	}

	encounter->eaScopeNames[0]->scope = (WorldScope*) zmapGetScope(NULL);
	encounter->eaScopeNames[0]->name = allocAddString(encounter->name);

	PERFINFO_AUTO_STOP();
	return encounter->eaScopeNames;
}


WorldScopeNamePair **oldencounter_GetGroupScopeNames(OldStaticEncounterGroup *encounterGroup)
{
	PERFINFO_AUTO_START_FUNC();

	if (!encounterGroup->eaScopeNames) {
		WorldScopeNamePair *scopeName = StructCreate(parse_WorldScopeNamePair);
		eaPush(&encounterGroup->eaScopeNames, scopeName);
	}

	encounterGroup->eaScopeNames[0]->scope = (WorldScope*) zmapGetScope(NULL);
	encounterGroup->eaScopeNames[0]->name = allocAddString(encounterGroup->groupName);

	PERFINFO_AUTO_STOP();
	return encounterGroup->eaScopeNames;
}


// ----------------------------------------------------------------------------------
// Encounter Group Logic
// ----------------------------------------------------------------------------------

static OldEncounterGroup* oldencounter_CreateGroup(int iPartitionIdx, OldStaticEncounterGroup* staticEncGroup)
{
	OldEncounterGroup* newGroup = encountergroup_Alloc();
	newGroup->iPartitionIdx = iPartitionIdx;
	newGroup->groupDef = staticEncGroup;
	return newGroup;
}


static void oldencounter_DestroyGroup(OldEncounterGroup* encounterGroup)
{
	eaDestroy(&encounterGroup->childEncs);
	eaDestroyEx(&encounterGroup->subGroups, oldencounter_DestroyGroup);
	encountergroup_Free(encounterGroup);
}


static void oldencounter_AddToEncounterGroupList(OldEncounterPartitionState *pState, OldStaticEncounterGroup* staticEncGroup, OldEncounterGroup* parentGroup)
{
	int i, n;

	// Weighted groups spawn some of their children encounters, but not all.
	// UI-only groups spawn all their children as normal encounters
	if (oldencounter_StaticEncounterGroupIsWeighted(staticEncGroup)) {
		OldEncounterGroup* encounterGroup = oldencounter_CreateGroup(pState->iPartitionIdx, staticEncGroup);
		encounterGroup->parentGroup = parentGroup;
		eaPush(&pState->eaEncounterGroups, encounterGroup);

	} else {
		// Add all children with no parent group
		n = eaSize(&staticEncGroup->childList);
		for (i = 0; i < n; i++) {
			oldencounter_AddToEncounterGroupList(pState, staticEncGroup->childList[i], parentGroup);
		}
	}
}


static void oldencounter_RefreshEncountersInGroup(OldEncounterPartitionState *pState, OldEncounterGroup* encounterGroup)
{
	int weightSum = 0;
	int i, numToSpawn, numUnspawnedChildren, numSpawnedChildren;
	OldStaticEncounter** childStaticEncs = NULL;
	OldStaticEncounterGroup** childStaticEncGroups = NULL;
	int numUnspawnedEncounters, numSpawnedEncounters = eaSize(&encounterGroup->childEncs);
	int numUnspawnedGroups, numSpawnedGroups = eaSize(&encounterGroup->subGroups);

	// Get the list of all potential encounter this group can spawn, remove the ones already spawned
	oldencounter_FunctionalGroupGetFunctionalGroupChildren(encounterGroup->groupDef, &childStaticEncGroups);
	for (i = 0; i < numSpawnedGroups; i++) {
		OldEncounterGroup* childGroup = encounterGroup->subGroups[i];
		eaFindAndRemove(&childStaticEncGroups, childGroup->groupDef);
	}

	oldencounter_FunctionalGroupGetStaticEncounterChildren(encounterGroup->groupDef, &childStaticEncs);
	for (i = 0; i < numSpawnedEncounters; i++) {
		OldEncounter* childEnc = encounterGroup->childEncs[i];
		OldStaticEncounter* staticEnc = GET_REF(childEnc->staticEnc);
		eaFindAndRemove(&childStaticEncs, staticEnc);
	}

	// Sum up the weights of all children that are unspawned
	numUnspawnedEncounters = eaSize(&childStaticEncs);
	numUnspawnedGroups = eaSize(&childStaticEncGroups);
	for (i = 0; i < numUnspawnedEncounters; i++) {
		weightSum += childStaticEncs[i]->spawnWeight;
	}
	for (i = 0; i < numUnspawnedGroups; i++) {
		weightSum += childStaticEncGroups[i]->groupWeight;
	}
	
	// Now figure out how many new things we need to spawn
	numUnspawnedChildren = numUnspawnedEncounters + numUnspawnedGroups;
	numSpawnedChildren = numSpawnedEncounters + numSpawnedGroups;
	numToSpawn = MIN(numUnspawnedChildren, (encounterGroup->groupDef->numToSpawn - numSpawnedChildren));
	for (i = 0; i < numToSpawn; i++) {
		int runningSum = 0;
		int randVal = (weightSum > 0) ? randomIntRange(0, weightSum - 1) : 0;
		bool madeSelection = false;
		int j, remainingEncs = eaSize(&childStaticEncs);

		for (j = 0; j < remainingEncs; j++) {
			runningSum += childStaticEncs[j]->spawnWeight;
			if (randVal < runningSum) {
				OldEncounter* newEncounter = oldencounter_Create(pState, childStaticEncs[j]);
				newEncounter->encGroup = encounterGroup;
				eaPush(&encounterGroup->childEncs, newEncounter);

				oldencounter_AddToEncounterList(pState, newEncounter);

				madeSelection = true;
				weightSum -= childStaticEncs[j]->spawnWeight;
				eaRemove(&childStaticEncs, j);
				break;
			}
		}

		// If we still haven't made a selection, we are spawning a group
		if (!madeSelection) {
			int remainingGroups = eaSize(&childStaticEncGroups);
			for (j = 0; j < remainingGroups; j++) {
				runningSum += childStaticEncGroups[j]->groupWeight;
				if (randVal < runningSum) {
					OldEncounterGroup* newGroup = oldencounter_CreateGroup(pState->iPartitionIdx, childStaticEncGroups[j]);
					newGroup->parentGroup = encounterGroup;
					eaPush(&encounterGroup->subGroups, newGroup);

					oldencounter_RefreshEncountersInGroup(pState, newGroup);

					madeSelection = true;
					weightSum -= childStaticEncGroups[j]->groupWeight;
					eaRemove(&childStaticEncGroups, j);
					break;
				}
			}
		}

		assertmsg(madeSelection, "Random selection isn't working, did weight summing or something change?");
	}

	eaDestroy(&childStaticEncs);
	eaDestroy(&childStaticEncGroups);
}


// ----------------------------------------------------------------------------------
// Contact Logic
// ----------------------------------------------------------------------------------

void oldencounter_GetContactLocations(ContactLocation ***peaContactLocations)
{
	int i, n;

	n = eaSize(&g_EncounterMasterLayer->encLayers);
	for (i = 0; i < n; i++) {
		EncounterLayer* currLayer = g_EncounterMasterLayer->encLayers[i];
		int j, numEncounters = eaSize(&currLayer->staticEncounters);

		for (j = 0; j < numEncounters; j++) {
			OldStaticEncounter *staticEnc = currLayer->staticEncounters[j];
			EncounterDef *def = staticEnc->spawnRule;

			if (def) {
				int k, m = eaSize(&def->actors);
				for (k = 0; k < m; k++) {
					OldActor *a = def->actors[k];
					ContactDef *contact = NULL;

					if (a->details.info && (contact = GET_REF(a->details.info->contactScript))) {
						ContactLocation *contactLoc = StructCreate(parse_ContactLocation);
						Quat tmpQuat;
						oldencounter_GetStaticEncounterActorPosition(staticEnc, a, contactLoc->loc, tmpQuat);
						contactLoc->pchContactDefName = contact->name;
						contactLoc->pchStaticEncName = staticEnc->name;
						eaPush(peaContactLocations, contactLoc);
					}
				}
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Tick Function
// ----------------------------------------------------------------------------------

// Main processing loop for encounters
void oldencounter_OncePerFrame(F32 fTimeStep)
{
	static U32 s_EncounterTick = 0;
	static F32 s_fTimeSinceUpdate[ENCOUNTER_SYSTEM_TICK];
	F32 fTimeStepThisTick;
	int i, numEncounters;
	int iPartitionIdx;
	PerfInfoGuard* piGuardFunc;
	
	if (!g_EncounterProcessing) {
		return;
	}

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuardFunc);

	// Some actions (like reloading layers) need to cause a delayed InitEncounters
	if (g_EncounterReloadCounter > 0) {
		--g_EncounterReloadCounter;
		if (g_EncounterReloadCounter == 0) {
			game_MapReInit();

			PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
			return;
		}
	}

	// Update Elapsed Time
	for (i = 0; i < ENCOUNTER_SYSTEM_TICK; i++) {
		s_fTimeSinceUpdate[i] += fTimeStep;
	}
	fTimeStepThisTick = s_fTimeSinceUpdate[s_EncounterTick % ENCOUNTER_SYSTEM_TICK];

	// Loop over partitions
	for(iPartitionIdx=eaSize(&s_eaOldEncounterPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
		OldEncounterPartitionState *pState = s_eaOldEncounterPartitionStates[iPartitionIdx];

		// Skip partition if no state
		if (!pState) {
			continue;
		}
		if (!pState->bIsRunning) {
			// Wait on this partition until it's able to run
			if (!encounter_IsMapOwnerAvailable(iPartitionIdx) ||
				gslQueue_WaitingForQueueData(iPartitionIdx) ||
				!gslpd_IsMapDifficultyInitialized(iPartitionIdx)) {
				continue;
			}

			// Once we get past the above test, this partition is able to run and can skip testing in future ticks
			pState->bIsRunning = true;
		}
		if (mapState_IsMapPausedForPartition(iPartitionIdx)) {
			continue;
		}

		// Slower tick (once per two seconds)
		numEncounters = eaSize(&pState->eaEncounters);
		for (i = s_EncounterTick % ENCOUNTER_SYSTEM_SLOW_TICK; i < numEncounters; i += ENCOUNTER_SYSTEM_SLOW_TICK) {
			OldEncounter* encounter = pState->eaEncounters[i];
			PerfInfoGuard *piGuard;

			if (g_EnableDynamicRespawn) {
				PERFINFO_AUTO_START_GUARD("oldencounter_OldUpdateDynamicSpawnRate", 1, &piGuard);
				oldencounter_OldUpdateDynamicSpawnRate(pState, encounter);
				PERFINFO_AUTO_STOP_GUARD(&piGuard);
			}

			PERFINFO_AUTO_START_GUARD("oldencounter_CheckFallThroughWorld", 1, &piGuard);
			oldencounter_CheckFallThroughWorld(encounter);
			PERFINFO_AUTO_STOP_GUARD(&piGuard);

			PERFINFO_AUTO_START_GUARD("oldencounter_UpdateProximityList", 1, &piGuard);
			oldencounter_UpdateProximityList(encounter);
			PERFINFO_AUTO_STOP_GUARD(&piGuard);

			PERFINFO_AUTO_START_GUARD("oldencounter_TrySleep", 1, &piGuard);
			oldencounter_TrySleep(encounter);
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
		}

		// Regular tick (3 times per second)
		for (i = s_EncounterTick % ENCOUNTER_SYSTEM_TICK; i < numEncounters; i += ENCOUNTER_SYSTEM_TICK) {
			OldEncounter* encounter = pState->eaEncounters[i];
			PerfInfoGuard *piGuard;

			PERFINFO_AUTO_START_GUARD("oldencounter_UpdateSpawnTimer", 1, &piGuard);
			if (encounter->fSpawnTimer > 0) {
				if (encounter->fSpawnRateMultiplier) {
					encounter->fSpawnTimer -= fTimeStepThisTick * encounter->fSpawnRateMultiplier;
				} else {
					encounter->fSpawnTimer -= fTimeStepThisTick;
				}
			}
			PERFINFO_AUTO_STOP_GUARD(&piGuard);

			PERFINFO_AUTO_START_GUARD("oldencounter_UpdateState", 1, &piGuard);
			oldencounter_UpdateState(pState, encounter);
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
		}
	}

	// Clear elapsed time on this tick
	s_fTimeSinceUpdate[s_EncounterTick % ENCOUNTER_SYSTEM_TICK] = 0.0f;
	
	++s_EncounterTick;

	PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
}


// ----------------------------------------------------------------------------------
// Map reload due to dictionary change logic
// ----------------------------------------------------------------------------------

static void oldencounter_LayerReloadOnSaveCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	EncounterLayer *pEncounterLayer = pResource;
	if (eType == RESEVENT_RESOURCE_MODIFIED) {
		// Force InitEncounters in several ticks
		g_EncounterReloadCounter = 3;
	}
}


// ----------------------------------------------------------------------------------
// Map Load/Unload/Execution Logic
// ----------------------------------------------------------------------------------

void oldencounter_PartitionValidate(void)
{
	// Validation
	if (!g_EncounterNoErrorCheck) {
		if (g_EncounterMasterLayer) {
			int i,j;
			for (i = 0; i < eaSize(&g_EncounterMasterLayer->encLayers); i++) {
				EncounterLayer* encLayer = g_EncounterMasterLayer->encLayers[i];
				int numEncs = eaSize(&encLayer->staticEncounters);

				// Encounter Layer Error Checking
				for (j = 0; j < numEncs; j++) {
					OldStaticEncounter* staticEnc = encLayer->staticEncounters[j];
					EncounterDef *pSpawnRule = staticEnc->spawnRule;

					// Verify the level ranges are entered correctly and fixup them up as needed
					if (staticEnc->minLevel || staticEnc->maxLevel) {
						if (!staticEnc->minLevel) {
							staticEnc->minLevel = 1;
							ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "StaticEncounter %s: MinLevel is 0, but should be at least 1", staticEnc->name);
						}
						if (!staticEnc->maxLevel) {
							staticEnc->maxLevel = staticEnc->minLevel;
							ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "StaticEncounter %s: MaxLevel is 0, but should be at least 1", staticEnc->name);
						}
						if (staticEnc->minLevel > staticEnc->maxLevel) {
							int tmp = staticEnc->minLevel;
							staticEnc->minLevel = staticEnc->maxLevel;
							staticEnc->maxLevel = tmp;
							ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "StaticEncounter %s: MinLevel(%i) is greater than MaxLevel(%i)", staticEnc->name, staticEnc->maxLevel, staticEnc->minLevel);
						}
					}
				}
			}
		}
	}
}


void oldencounter_MapValidate(void)
{
	if (gConf.bAllowOldEncounterData && g_EncounterMasterLayer) {
		int i,j;
		for (i = 0; i < eaSize(&g_EncounterMasterLayer->encLayers); i++) {
			EncounterLayer* encLayer = g_EncounterMasterLayer->encLayers[i];
			int numEncs = eaSize(&encLayer->staticEncounters);

			// Encounter Layer Error Checking
			for (j = 0; j < numEncs; j++) {
				OldStaticEncounter* staticEnc = encLayer->staticEncounters[j];
				EncounterDef *pSpawnRule = staticEnc->spawnRule;

				// Verify the level ranges are entered correctly and fixup them up as needed
				if (staticEnc->minLevel || staticEnc->maxLevel) {
					if (!staticEnc->minLevel) {
						staticEnc->minLevel = 1;
						ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "StaticEncounter %s: MinLevel is 0, but should be at least 1", staticEnc->name);
					}
					if (!staticEnc->maxLevel) {
						staticEnc->maxLevel = staticEnc->minLevel;
						ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "StaticEncounter %s: MaxLevel is 0, but should be at least 1", staticEnc->name);
					}
					if (staticEnc->minLevel > staticEnc->maxLevel) {
						int tmp = staticEnc->minLevel;
						staticEnc->minLevel = staticEnc->maxLevel;
						staticEnc->maxLevel = tmp;
						ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "StaticEncounter %s: MinLevel(%i) is greater than MaxLevel(%i)", staticEnc->name, staticEnc->maxLevel, staticEnc->minLevel);
					}
				}
			}
		}
	}
}


void oldencounter_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	if (gConf.bAllowOldEncounterData) {
		OldEncounterPartitionState *pState;
		int i, n, numGroups;

		// Create state (destroying previous if present)
		pState = eaGet(&s_eaOldEncounterPartitionStates, iPartitionIdx);
		if (pState) {
			// Destroy prior state so we can re-create
			oldencounter_PartitionUnload(iPartitionIdx);
		}
		pState = oldencounter_CreatePartitionState(iPartitionIdx);
		eaSet(&s_eaOldEncounterPartitionStates, pState, iPartitionIdx);

		// Add all the non grouped encounters to the master list
		n = eaSize(&g_EncounterMasterLayer->encLayers);
		for (i = 0; i < n; i++) {
			EncounterLayer* currLayer = g_EncounterMasterLayer->encLayers[i];
			int j, numEncounters = eaSize(&currLayer->staticEncounters);

			for (j = 0; j < numEncounters; j++) {
				OldStaticEncounter* staticEnc = currLayer->staticEncounters[j];
				if (staticEnc->name && !oldencounter_StaticEncounterFindWeightedParent(staticEnc)) {
					oldencounter_AddToEncounterList(pState, oldencounter_Create(pState, staticEnc));
				}
			}

			// Populate the group list starting at the root
			oldencounter_AddToEncounterGroupList(pState, &currLayer->rootGroup, NULL);
		}

		// Second pass for initialization
		n=eaSize(&pState->eaEncounters);
		for(i=0; i<n; ++i) {
			OldEncounter *encounter = pState->eaEncounters[i];
			oldencounter_Init(encounter);
		}

		// Create all child encounters from our group list
		numGroups = eaSize(&pState->eaEncounterGroups);
		for (i = 0; i < numGroups; i++) {
			oldencounter_RefreshEncountersInGroup(pState, pState->eaEncounterGroups[i]);
		}

		// Do validation
		oldencounter_PartitionValidate();
	}

	PERFINFO_AUTO_STOP();
}


void oldencounter_PartitionUnload(int iPartitionIdx)
{
	if (gConf.bAllowOldEncounterData) {
		OldEncounterPartitionState *pState = eaGet(&s_eaOldEncounterPartitionStates, iPartitionIdx);
		if (!pState) {
			return; // It's okay to try to unload something that does not exist
		}

		// Clear the stash table so lookups that happen during destroy find NULL values
		stashTableClear(pState->pEncounterFromStaticEncounterHash);

		// Clean up encounters and groups
		eaDestroyEx(&pState->eaEncounterGroups, oldencounter_DestroyGroup);
		eaDestroyEx(&pState->eaEncounters, oldencounter_Destroy);

		// Destroy the state
		oldencounter_DestroyPartitionState(pState);
		eaSet(&s_eaOldEncounterPartitionStates, NULL, iPartitionIdx);
	}
}


void oldencounter_MapLoad(ZoneMap *pZoneMap, bool bFullInit)
{
	if (gConf.bAllowOldEncounterData) {
		if (bFullInit) {
			// Reload layer data
			oldencounter_UnloadLayers();
			oldencounter_LoadLayers(pZoneMap);
		}

		// Reinitialize any open partitions
		partition_ExecuteOnEachPartition(oldencounter_PartitionLoad);
	}
}


void oldencounter_MapUnload(void)
{
	if (gConf.bAllowOldEncounterData) {
		// Shut down all partitions
		int i;
		for(i=eaSize(&s_eaOldEncounterPartitionStates)-1; i>=0; --i) {
			if (s_eaOldEncounterPartitionStates[i]) {
				oldencounter_PartitionUnload(i);
			}
		}

		// Unload layer data
		oldencounter_UnloadLayers();
	}
}


AUTO_RUN;
void oldencounter_AutoRunInit(void)
{
	if (gConf.bAllowOldEncounterData) {
		if (!g_EncounterLayerDictionary) {
			oldencounter_RegisterEncounterLayerDictionary();
		}
		oldencounter_AllowLayerLoading();
		resDictRegisterEventCallback(g_EncounterLayerDictionary, oldencounter_LayerReloadOnSaveCB, NULL);
	}
}


#include "gslOldEncounter_h_ast.c"
