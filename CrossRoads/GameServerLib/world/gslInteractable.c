/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "beacon.h"
#include "Character.h"
#include "Character_combat.h"
#include "logging.h"
#include "ChoiceTable_common.h"
#include "contact_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityInteraction.h"
#include "EntityIterator.h"
#include "EntityMovementDoor.h"
#include "EntityMovementManager.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "error.h"
#include "estring.h"
#include "gameaction_common.h"
#include "GameEvent.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "ChoiceTable.h"
#include "gslContact.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslGameAction.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslInteractionManager.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslSendToClient.h"
#include "gslSpawnPoint.h"
#include "gslVolume.h"
#include "gslWorldVariable.h"
#include "gslPartition.h"
#include "interaction_common.h"
#include "InteractionManager_common.h"
#include "inventoryCommon.h"
#include "mapstate_common.h"
#include "mathutil.h"
#include "gslMission.h"
#include "mission_common.h"
#include "Player.h"
#include "Powers.h"
#include "queue_common.h"
#include "rand.h"
#include "RegionRules.h"
#include "reward.h"
#include "rewardcommon.h"
#include "StashTable.h"
#include "stringcache.h"
#include "StringFormat.h"
#include "transactionOutcomes.h"
#include "wlbeacon.h"
#include "wlEncounter.h"
#include "wlInteraction.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "EntityLib.h"
#include "Skills_DD.h"
#include "bounds.h"
#include "gslTeamCorral.h"

#include "gslInteractable_h_ast.h"
#include "AutoGen/EntityInteraction_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Defines and forward declarations
// ----------------------------------------------------------------------------------


static GameInteractable *interactable_AddFakeInteractable(int iPartitionIdx, GameInteractable *pParentInteractable, WorldInteractionEntry *pEntry);

typedef bool interactable_ActCallback(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, F32 fFadeInTime, char **estrErrString);


// ----------------------------------------------------------------------------------
// Static Data Initialization
// ----------------------------------------------------------------------------------

//the list and the hash both contain the same data, one is used for fast lookup and the other for iteration
static GameInteractable **s_eaInteractables = NULL;
StashTable s_stInteractables = NULL;
static Octree *s_pInteractableOctree = NULL;

// Ambient Jobs
static GameInteractable **s_eaAmbientJobInteractables = NULL;
static Octree *s_AmbientJobOctree = NULL;

// Combat jobs
static GameInteractable **s_eaCombatJobInteractables = NULL;
static Octree *s_CombatJobOctree = NULL;


static U32 s_iMaxInteractRange;
static U32 s_iMaxTargetRange;
static bool s_mapContainsPerEntVisExpressions;	//Does this map contain per-entity visibility expressions on any interactables?

static GameInteractable **s_eaGlobalInteractionInteractables = NULL;
static F32 s_fLastRegionCutoff;

const char *g_InteractableExprVarName = NULL;

static bool s_bInteractablesLoaded = false;

static bool s_bHasVolumeTriggeredGates = false;

static int s_LogicalRandomLock = -1;
AUTO_CMD_INT(s_LogicalRandomLock, LockLogicSpawn);


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaInteractables.  No other function
// than interactable_GetByName, interactable_GetByEntry, FOR_EACH_INTERACTABLE, and
// FOR_EACH_INTERACTABLE2 should be searching s_eaInteractables.
// ----------------------------------------------------------------------------------
GameInteractable *interactable_GetByName(const char *pcInteractableName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcInteractableName);

		if (pObject && (pObject->type == WL_ENC_INTERACTABLE)) {
			WorldNamedInteractable *pNamedInteractable = (WorldNamedInteractable*)pObject;
			GameInteractable *pGameInteractable = interactable_GetByEntry(pNamedInteractable->entry);
			if (pGameInteractable) {
				return pGameInteractable;
			}
		}
	} else {
		int i;
		for(i=eaSize(&s_eaInteractables)-1; i>=0; --i) {
			if (stricmp(s_eaInteractables[i]->pcName, pcInteractableName) == 0) {
				return s_eaInteractables[i];
			}
		}
	}
	
	return NULL;
}


GameInteractable *interactable_GetByEntry(WorldInteractionEntry *pEntry)
{
	GameInteractable* pInteractable;
	if ( stashAddressFindPointer(s_stInteractables, pEntry, &pInteractable) ) {
		return pInteractable;
	}
	return NULL;
}


GameInteractable *interactable_GetByNode(WorldInteractionNode *pNode)
{
	GameInteractable* interactable;

	if (!pNode) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();
	interactable = interactable_GetByEntry(wlInteractionNodeGetEntry(pNode));
	PERFINFO_AUTO_STOP();

	return interactable;
}


bool interactable_InteractableExists(WorldScope *pScope, const char *pcInteractableName)
{
	return interactable_GetByName(pcInteractableName, pScope) != NULL;
}


#define FOR_EACH_INTERACTABLE(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaInteractables)-1; i##it##Index>=0; --i##it##Index) {	GameInteractable *it = s_eaInteractables[i##it##Index];
#define FOR_EACH_INTERACTABLE2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GameInteractable *it = s_eaInteractables[i##it##Index];
#define FOR_EACH_INTERACTABLE_STEP(it,base,step) { int i##it##Index; for(i##it##Index=eaSize(&s_eaInteractables)-1-base; i##it##Index>=0; i##it##Index -= step) {	GameInteractable *it = s_eaInteractables[i##it##Index];

// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaInteractables.
// ----------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------
// Interactable Utilities
// ----------------------------------------------------------------------------------

GameInteractablePartitionState *interactable_GetPartitionState(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState = eaGet(&pInteractable->eaPartitionStates, iPartitionIdx);
	assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
	return pState;
}


bool interactable_PartitionExists(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState = eaGet(&pInteractable->eaPartitionStates, iPartitionIdx);
	return (pState != NULL);
}

// exactly the same as below, but I need one that doesn't assert
GameInteractLocationPartition* interactable_GetInteractLocationPartitionIfPresent(int iPartitionIdx, GameInteractLocation *pInteractLocation)
{
	GameInteractLocationPartition *pLocationPartition = eaGet(&pInteractLocation->eaPartitions, iPartitionIdx);
	return pLocationPartition;
}

GameInteractLocationPartition* interactable_GetInteractLocationPartition(int iPartitionIdx, GameInteractLocation *pInteractLocation)
{
	GameInteractLocationPartition *pLocationPartition = eaGet(&pInteractLocation->eaPartitions, iPartitionIdx);
	assertmsgf(pLocationPartition, "Partition %d does not exist", iPartitionIdx);
	return pLocationPartition;
}

int interactable_EvaluateExpr(int iPartitionIdx, GameInteractable *pInteractable, Entity *pPlayerEnt, Expression *pExpression)
{
	MultiVal retVal = {0};
	static ExprContext *s_Context = NULL;

	if (!s_Context) {
		s_Context = exprContextCreate();
		exprContextSetFuncTable(s_Context, interactable_CreateExprFuncTable());
	}

	if (!pPlayerEnt) {
		Errorf("Missing player entity on call to interactable_EvaluateExpr for expression '%s' so not evaluating it", exprGetCompleteString(pExpression));
		return 0;
	}

	// Set player into context
	exprContextSetSelfPtr(s_Context, pPlayerEnt);
	exprContextSetPartition(s_Context, iPartitionIdx);
	exprContextSetPointerVarPooled(s_Context, g_PlayerVarName, pPlayerEnt, NULL, false, true);

	// Set interactable into context
	exprContextSetPointerVarPooled(s_Context, g_InteractableExprVarName, pInteractable, NULL, false, true);

	// Set clickable tracker data
	exprContextSetPointerVar(s_Context, "ClickableTracker", pInteractable->pWorldEntry, NULL, false, true);

	// Set scope in context
	exprContextSetScope(s_Context, SAFE_MEMBER(pInteractable->pWorldInteractable, common_data.closest_scope));

	// Evaluate the expression
	exprEvaluate(pExpression, s_Context, &retVal);
	return MultiValGetInt(&retVal, NULL);
}


int interactable_EvaluateNonPlayerExpr(int iPartitionIdx, GameInteractable *pInteractable, Expression *pExpression)
{
	static ExprContext *s_Context = NULL;

	GameInteractablePartitionState *pState;
	MultiVal retVal = {0};
	int iResult;

	PERFINFO_AUTO_START_FUNC();

	if (!s_Context) {
		s_Context = exprContextCreate();
		exprContextSetFuncTable(s_Context, interactable_CreateNonPlayerExprFuncTable());
	}


	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

	// Set partition into context
	exprContextSetPartition(s_Context, iPartitionIdx);

	// Set interactable into context
	exprContextSetPointerVarPooled(s_Context, g_InteractableExprVarName, pInteractable, NULL, false, true);

	// Set clickable tracker data
	exprContextSetPointerVar(s_Context, "ClickableTracker", pInteractable->pWorldEntry, NULL, false, true);

	// Set scope in context
	exprContextSetScope(s_Context, SAFE_MEMBER(pInteractable->pWorldInteractable, common_data.closest_scope));

	// Set node information into context
	exprContextSetIntVar(s_Context, "CurrentState", pState->iChildIndex);
	exprContextSetIntVar(s_Context, "IsVisible", !pState->bHidden);
	exprContextSetIntVar(s_Context, "IsDisabled", pState->bDisabled);
	exprContextSetIntVar(s_Context, "IsSelectable", interactable_IsSelectable(pInteractable));

	// Evaluate the expression
	exprEvaluate(pExpression, s_Context, &retVal);

	iResult = MultiValGetInt(&retVal, NULL);

	PERFINFO_AUTO_STOP();
	return iResult;
}


bool interactable_ExecuteOnEachInteractable(VisitGameInteractable func, void *pClientPtr)
{
	if (!func) {
		return false;
	}

	FOR_EACH_INTERACTABLE(pInteractable) {
		if (func(pInteractable, pClientPtr)) {
			return true;
		}
	} FOR_EACH_END

		return false;
}


static bool interactable_ActOnInteractables(int iPartitionIdx, WorldScope *pScope, WorldLogicalGroup *pGroup, interactable_ActCallback callback, F32 fFadeTime, char **estrErrString)
{
	WorldEncounterObject *pObject;
	bool bChangedSomething = false;
	int i;

	for(i=eaSize(&pGroup->objects)-1; i>=0; --i) {
		pObject = pGroup->objects[i];
		if (pObject->type == WL_ENC_INTERACTABLE) {
			const char *pcName = worldScopeGetObjectName(pScope, pObject);
			bChangedSomething |= callback(iPartitionIdx, pScope, pcName, fFadeTime, estrErrString);

		} else if (pObject->type == WL_ENC_LOGICAL_GROUP) {
			bChangedSomething |= interactable_ActOnInteractables(iPartitionIdx, pScope, (WorldLogicalGroup*)pObject, callback, fFadeTime, estrErrString);
		}
	}
	return bChangedSomething;
}


// ----------------------------------------------------------------------------------
// Event Management
// ----------------------------------------------------------------------------------

int interactable_EventCount(int iPartitionIdx, GameInteractable *pInteractable, const char *pchEventName)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	int iEventCount;

	stashFindInt(pState->stEventLog, pchEventName, &iEventCount);
	return iEventCount;
}


void interactable_EventCountAdd(GameInteractable *pInteractable, GameEvent *pEvent, GameEvent *pSpecific, int iIncrement)
{
	GameInteractablePartitionState *pState;
	int iPartitionIdx;
	int iCount;
	
	if (!pEvent) {
		Errorf("NULL pEvent passed");
		return;
	}

	if (pEvent->pchEventName) {
		iPartitionIdx = SAFE_MEMBER(pEvent, iPartitionIdx);
		pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		iCount = interactable_EventCount(iPartitionIdx, pInteractable, pEvent->pchEventName) + iIncrement;
		stashAddInt(pState->stEventLog, pEvent->pchEventName, iCount, true);
	} else {
		Errorf("Interactable %s ignoring event because the event has no name", pInteractable->pcName);
	}
}


void interactable_EventCountSet(GameInteractable *pInteractable, GameEvent *pEvent, GameEvent *pSpecific, int iCount)
{
	GameInteractablePartitionState *pState;
	int iPartitionIdx;

	if (!pEvent) {
		Errorf("NULL pEvent passed");
		return;
	}

	if (pEvent->pchEventName) {
		iPartitionIdx = SAFE_MEMBER(pEvent, iPartitionIdx);
		pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		stashAddInt(pState->stEventLog, pEvent->pchEventName, iCount, true);
	} else {
		Errorf("Interactable %s ignoring event because the event has no name", pInteractable->pcName);
	}
}


// ----------------------------------------------------------------------------------
// Interactable World Space Access Functions
// ----------------------------------------------------------------------------------

F32 interactable_GetSphereBounds(GameInteractable *pInteractable, Vec3 vWorldMid)
{
	if (pInteractable) {
		copyVec3(pInteractable->pMainOctreeEntry->bounds.mid, vWorldMid);
		return pInteractable->pMainOctreeEntry->bounds.radius;
	}

	zeroVec3(vWorldMid);
	return 0;
}


void interactable_GetWorldMid(GameInteractable *pInteractable, Vec3 vWorldMid)
{
	if (pInteractable) {
		copyVec3(pInteractable->pMainOctreeEntry->bounds.mid, vWorldMid);
	} else {
		zeroVec3(vWorldMid);
	}
}


bool interactable_GetPosition(GameInteractable *pInteractable, Vec3 vPos)
{
	if (pInteractable) {
		if (pInteractable->pWorldEntry) {
			copyVec3(pInteractable->pWorldEntry->base_entry.bounds.world_matrix[3], vPos);
			return true;
		}
	}
	return false;
}


bool interactable_GetPositionByName(WorldScope *pScope, const char *pcInteractableName, Vec3 vPos)
{
	return interactable_GetPosition(interactable_GetByName(pcInteractableName, pScope), vPos);
}


// ----------------------------------------------------------------------------------
// Interactable Range Access Functions
// ----------------------------------------------------------------------------------

void interactable_ComputeMaxInteractRange(ZoneMap *pZoneMap)
{
	S32 i;
	WorldZoneMapScope *pScope = zmapGetScope(pZoneMap);
	U32 uMaxInteractRange = 5;
	U32 uMaxTargetRange = 5;

	if (pScope) {
		for(i=eaSize(&pScope->interactables)-1; i>=0; --i) {
			WorldInteractionEntry* pEntry = pScope->interactables[i]->entry;
			WorldInteractionProperties *pProps = pEntry ? pEntry->full_interaction_properties : NULL;
			U32 uInteractDist = pProps ? pProps->uInteractDistCached : 0;
			U32 uTargetDist = pProps ? pProps->uTargetDistCached : 0; 
			bool bPastDistCutoff = pProps ? pProps->bPastDistCutoff : 0;

			if (bPastDistCutoff) {
				GameInteractable* pGameInteractable = interactable_GetByEntry(pEntry);
				eaPush(&s_eaGlobalInteractionInteractables, pGameInteractable);
			}

			// Update running total: interact range
			if ( uInteractDist > uMaxInteractRange ) {
				uMaxInteractRange = uInteractDist;
			}

			// Update running total: target range
			if ( uTargetDist > uMaxTargetRange ) {
				uMaxTargetRange = uTargetDist;
			}
		}
	}

	if (s_iMaxInteractRange != uMaxInteractRange) {
		s_iMaxInteractRange = uMaxInteractRange;
		printf("Max Interact Range for interactables set to %d\n", uMaxInteractRange);
	}
	if (s_iMaxTargetRange != uMaxTargetRange) {
		s_iMaxTargetRange = uMaxTargetRange;
		printf("Max Target Range for interactables set to %d\n", uMaxTargetRange);
	}

	if (eaSize(&s_eaGlobalInteractionInteractables)) {
		printf("Last region's cutoff is %f, number of global interactables is currently %d\n", s_fLastRegionCutoff, eaSize(&s_eaGlobalInteractionInteractables));
		if (eaSize(&s_eaGlobalInteractionInteractables) > 20) {
			Errorf("There are more than 20 global interaction nodes (currently %d), please verify this is ok", eaSize(&s_eaGlobalInteractionInteractables));
		}
	}
	//If the computed max interact range is low, problems will arise when running the octree query later.
	// Specifically, the octree searches within this provided radius, but the interaction system
	// will factor in the entity's capsule size after the query has completed, resulting in 
	// a disagreement between the octree and the interaction system about whether something is in
	// range or not. Bumping up the min octree search query radius will solve this problem for 99% of
	// entity/node interaction pairs. If the entity or node are very large, it might still break.
	if (s_iMaxInteractRange < DEFAULT_NODE_INTERACT_DIST*2)
	{
		s_iMaxInteractRange = DEFAULT_NODE_INTERACT_DIST*2;
		printf("Max Interact Range for interactables increased to %d to compensate for capsule size.\n", s_iMaxInteractRange);
	}
}


U32 interactable_GetMaxInteractRange(void)
{
	return s_iMaxInteractRange;
}


U32 interactable_GetMaxTargetRange(void)
{
	return s_iMaxTargetRange;
}


U32 interactable_GetNodeInteractMaxRange(Entity *pEnt)
{
	return interactable_GetMaxInteractRange();
}


F32 interactable_GetCutoffDist(Entity* pEnt)
{
	RegionRules* pRules = getRegionRulesFromEnt(pEnt);
	return pRules ? pRules->fInteractDistCutoff : DEFAULT_NODE_INTERACT_DIST_CUTOFF;
}


// ----------------------------------------------------------------------------------
// Interactable Property Access Functions
// ----------------------------------------------------------------------------------

int interactable_GetNumOverrideEntries(GameInteractable *pInteractable, bool bIncludePostLoad)
{
	// Ensure overrides added during editing are included
	if(isDevelopmentMode() || isProductionMode() && isProductionEditMode())
		bIncludePostLoad = true;

	if(!devassert((int)pInteractable->uOverrideLoadCount <= eaSize(&pInteractable->eaOverrides)))
		return eaSize(&pInteractable->eaOverrides);

	return bIncludePostLoad ? eaSize(&pInteractable->eaOverrides) : pInteractable->uOverrideLoadCount;
}

int interactable_GetNumPropertyEntries(GameInteractable *pInteractable, bool bIncludePostLoad)
{
	// Return zero if properties not available
	if (!pInteractable || !pInteractable->pWorldEntry || !pInteractable->pWorldEntry->full_interaction_properties) {
		return 0;
	}

	return eaSize(&pInteractable->pWorldEntry->full_interaction_properties->eaEntries) + 
			interactable_GetNumOverrideEntries(pInteractable, bIncludePostLoad);
}


WorldInteractionPropertyEntry *interactable_GetPropertyEntry(GameInteractable *pInteractable, int iIndex)
{
	// Return null if properties not available
	if (!pInteractable || !pInteractable->pWorldEntry || !pInteractable->pWorldEntry->full_interaction_properties) {
		return NULL;
	}

	// return NULL if input is invalid
	if (iIndex < 0){
		return NULL;
	}

	// Return properties if in main list
	if (iIndex < eaSize(&pInteractable->pWorldEntry->full_interaction_properties->eaEntries)) {
		return pInteractable->pWorldEntry->full_interaction_properties->eaEntries[iIndex];
	}

	// Return properties if in override list
	iIndex -= eaSize(&pInteractable->pWorldEntry->full_interaction_properties->eaEntries);
	if (iIndex < eaSize(&pInteractable->eaOverrides)) {
		return pInteractable->eaOverrides[iIndex]->pEntry;
	}

	return NULL;
}


ContactDef *interactable_GetContactDef(GameInteractable *pInteractable, int iIndex)
{
	WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, iIndex);
	WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
	if (pContactProps){
		return GET_REF(pContactProps->hContactDef);
	}
	return NULL;
}


void interactable_GetCategories(GameInteractable *pInteractable, const char ***peaCategories, Entity *pPlayerEnt)
{
	int i;
	int numOverrides;
	
	eaClear(peaCategories);
	eaCopy(peaCategories, &pInteractable->eaInteractableCategories);
	
	//Apply any categories from active mission overrides
	numOverrides = interactable_GetNumOverrideEntries(pInteractable, SAFE_MEMBER3(pPlayerEnt, pPlayer, missionInfo, bHasNamespaceMission));
	for(i = 0; i < numOverrides; i++){
		if(interactable_EvaluateExpr(entGetPartitionIdx(pPlayerEnt), pInteractable, pPlayerEnt, pInteractable->eaOverrides[i]->pEntry->pInteractCond)) {
			eaPush(peaCategories, interaction_GetCategoryName((pInteractable->eaOverrides)[i]->pEntry));
		}
	}
}


void interactable_GetTags(GameInteractable *pInteractable, const char ***peaTags, Entity *pPlayerEnt)
{
	eaClear(peaTags);
	eaCopy(peaTags, &pInteractable->eaTags);
}


// Returns the first detail texture name (pooled string) it finds among the interactable's interaction entries
const char *interactable_GetDetailTexture(GameInteractable *pInteractable)
{
	int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
	int i;
	for(i = 0; i < iNumEntries; i++) {
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
		WorldTextInteractionProperties *pTextProps = interaction_GetTextProperties(pEntry);

		if (pTextProps && EMPTY_TO_NULL(pTextProps->interactDetailTexture)) {
			return pTextProps->interactDetailTexture;
		}
	}
	return NULL;
}


void interactable_CheckForRelevantTooltipInfo(GameInteractable* pInteractable, const char** ppchConditionInfoOut)
{
	int i;
	char* tmpS = NULL;
	int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
	*ppchConditionInfoOut = NULL;
	for(i=0; i<iNumEntries; ++i) {
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
		Expression *pAttemptableCond = interaction_GetAttemptableCond(pEntry);

		if (pAttemptableCond) {
			NNOParseSkillsForTooltip(pAttemptableCond, ppchConditionInfoOut);
		}
	}
}


static bool interactable_IsDynamicSpawn(WorldInteractionPropertyEntry *pEntry)
{
	WorldTimeInteractionProperties *pTimeProps;
	
	PERFINFO_AUTO_START_FUNC();

	pTimeProps = interaction_GetTimeProperties(pEntry);
	if (pTimeProps && interaction_GetCooldownTime(pEntry) > 0 &&
		(pTimeProps->eDynamicCooldownType == WorldDynamicSpawnType_Dynamic || 
		(pTimeProps->eDynamicCooldownType == WorldDynamicSpawnType_Default && (zmapInfoGetMapType(NULL) == ZMTYPE_STATIC || zmapInfoGetMapType(NULL) == ZMTYPE_SHARED)))
		) {
			PERFINFO_AUTO_STOP();
			return true;
	}

	PERFINFO_AUTO_STOP();
	return false;
}


bool interactable_IsSelectable(GameInteractable *pInteractable)
{
	WorldInteractionNode *pNode = GET_REF(pInteractable->pWorldEntry->hInteractionNode);
	if (pNode) {
		return wlInteractionNodeIsSelectable(pNode);
	}
	return false;
}


U32 interactable_RewardPropsGetRewardLevel(Entity *pPlayerEnt, WorldRewardInteractionProperties *pRewardProps)
{
	U32 iLevel = 0;
	MapVariable *pMapVar;

	switch(pRewardProps->eRewardLevelType) 
	{
		xcase WorldRewardLevelType_Map:	
			iLevel = zmapInfoGetMapLevel(NULL);

		xcase WorldRewardLevelType_Custom:
			iLevel = pRewardProps->uCustomRewardLevel;

		xcase WorldRewardLevelType_Player:
			iLevel = entity_GetSavedExpLevel(pPlayerEnt);

		xcase WorldRewardLevelType_MapVariable:
			pMapVar = mapvariable_GetByName(entGetPartitionIdx(pPlayerEnt), pRewardProps->pcMapVarName);
			if (pMapVar) {
				if (pMapVar->pDef->eType == WVAR_INT) {
					iLevel = pMapVar->pVariable->iIntVal;
				} else {
					Errorf("Map variable '%s' is used for a Reward Level on an interactable, but is not an INT.", pRewardProps->pcMapVarName);
				}
			} else {
				Errorf("Map variable '%s' is used for a Reward Level on an interactable, but does not exist.", pRewardProps->pcMapVarName);
			}

		xdefault:
			assertmsg(0, "Unsupported reward level type.");
	}
	return iLevel;
}


F32 interactable_GetCooldownMultiplier(int iPartitionIdx, WorldInteractionNode *pNode, int iIndex)
{
	GameInteractable *pInteractable = interactable_GetByNode(pNode);
	WorldInteractionPropertyEntry *pEntry;
	static U32 iMask;
	int iNumNearbyNodes = 0, iNumOnCooldown = 0;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (!iMask) {
		// Note that NamedObject needs to be on this list or else mission overrides on
		// interaction properties don't work properly
		iMask = wlInteractionClassNameToBitMask("Clickable") | wlInteractionClassNameToBitMask("Contact") |
			wlInteractionClassNameToBitMask("Door") | wlInteractionClassNameToBitMask("NamedObject");
	}

	if (!pInteractable || !g_EnableDynamicRespawn) {
		PERFINFO_AUTO_STOP();
		return 0.f;
	}

	pEntry = interactable_GetPropertyEntry(pInteractable, iIndex);
	if (pEntry && interactable_IsDynamicSpawn(pEntry)) {
		static GameInteractable **s_eaNearbyInteractables = NULL;
		Vec3 vPos;
		interactable_GetPosition(pInteractable, vPos);
		eaClearFast(&s_eaNearbyInteractables);

		PERFINFO_AUTO_START("FindNearbyNodes", 1);
		interactable_QuerySphere(iPartitionIdx, iMask, NULL, vPos, 300.f, false, false, false, &s_eaNearbyInteractables);
		PERFINFO_AUTO_STOP();

		for (i = eaSize(&s_eaNearbyInteractables)-1; i>=0; --i) {
			GameInteractable *pOtherInteractable = s_eaNearbyInteractables[i];
			bool bDynamicCooldown = false;
			int iEntry, iNumEntries = interactable_GetNumPropertyEntries(pOtherInteractable, true);

			for (iEntry = 0; iEntry < iNumEntries; iEntry++) {
				WorldInteractionPropertyEntry *pOtherEntry = interactable_GetPropertyEntry(pOtherInteractable, iEntry);
				if (pOtherEntry && interactable_IsDynamicSpawn(pOtherEntry)) {
					bDynamicCooldown = true;
					break;
				}
			}

			if (pOtherInteractable && pOtherInteractable != pInteractable) {
				GameInteractablePartitionState *pOtherState = interactable_GetPartitionState(iPartitionIdx, pOtherInteractable);
				if(!pOtherState->bSleeping && bDynamicCooldown) {
					if (interaction_IsNodeOnCooldown(iPartitionIdx, interactable_GetWorldInteractionNode(pOtherInteractable))) {
						iNumOnCooldown++;
						iNumNearbyNodes++;
					} else if (!interactable_IsHiddenOrDisabled(iPartitionIdx, pOtherInteractable)) {
						iNumNearbyNodes++;
					}
				}
			}
		}

		// Hack to smooth out behavior with small numbers of nodes
		// If there are less than 5 nodes nearby, always act as if there are at least 5
		if (iNumNearbyNodes < 5) {
			iNumNearbyNodes = 5;
		}

		if (iNumNearbyNodes && g_fDynamicRespawnScale > 1.f) {
			F32 fRatio = ((F32)iNumOnCooldown/(F32)iNumNearbyNodes);
			PERFINFO_AUTO_STOP();
			return 1.f + (g_fDynamicRespawnScale-1.f)*fRatio;
		}
	}

	PERFINFO_AUTO_STOP();
	return 0.f;
}


bool interactable_IsSelectableCallback(WorldInteractionNode *pNode)
{
	GameInteractable *pInteractable = interactable_GetByNode(pNode);
	WorldInteractionPropertyEntry *pEntry;
	WorldDestructibleInteractionProperties *pDestructibleProps;
	int iNumEntries;
	int i;

	if (!pInteractable) {
		WorldInteractionEntry* pWorldEntry = wlInteractionNodeGetEntry( pNode );

		if (!pWorldEntry || !pWorldEntry->full_interaction_properties) {
			return false;
		}

		for(i=0;i<eaSize(&pWorldEntry->full_interaction_properties->eaEntries);i++) {
			pEntry = pWorldEntry->full_interaction_properties->eaEntries[i];
			pDestructibleProps = interaction_GetDestructibleProperties(pEntry);

			if (pDestructibleProps ) {
				CritterDef *pCritterDef = GET_REF(pDestructibleProps->hCritterDef);
				if (pCritterDef) {
					return !pCritterDef->bUnselectable;
				}
			}
		}

		return false;
	}

	// If it is destructible, check the critter def
	iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
	for(i=0; i<iNumEntries; ++i) {
		pEntry = interactable_GetPropertyEntry(pInteractable, i);
		pDestructibleProps = interaction_GetDestructibleProperties(pEntry);
		if (pDestructibleProps) {
			CritterDef *pCritterDef = GET_REF(pDestructibleProps->hCritterDef);
			if (pCritterDef) {
				return !pCritterDef->bUnselectable;
			}
		}
	}

	return false;
}


bool interactable_IsHideableEntry(WorldInteractionEntry *pWorldEntry)
{
	GameInteractable *pInteractable = interactable_GetByEntry(pWorldEntry);
	int iNumEntries;
	int i;

	// Check if the explicit hide or starts_hidden features are turned on
	if (!pWorldEntry->full_interaction_properties ||
		pWorldEntry->full_interaction_properties->bAllowExplicitHide ||
		pWorldEntry->full_interaction_properties->bStartsHidden) {
			return true;
	}

	if (!pInteractable) {
		return false;
	}

	iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
	for(i=0; i<iNumEntries; ++i) {
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
		WorldTimeInteractionProperties *pTimeProps;
		assert(pEntry);

		// All destructibles can be hidden
		if (interaction_GetEffectiveClass(pEntry) == pcPooled_Destructible) {
			return true;
		}

		// If it has a visible expression it can be hidden by that
		if (interaction_GetVisibleExpr(pEntry)) {
			return true;
		}

		// Others only become hidden if hidden during cooldown
		pTimeProps = interaction_GetTimeProperties(pEntry);
		if (pTimeProps && pTimeProps->bHideDuringCooldown) {
			return true;
		}
	}

	return false;
}


bool interactable_IsTraversableEntry(WorldInteractionEntry *pWorldEntry)
{
	int i;

	// look for a motion subentry part
	while (pWorldEntry && pWorldEntry->base_interaction_properties) {
		pWorldEntry = pWorldEntry->base_entry_data.parent_entry;
	}
	if (!pWorldEntry) {
		return false;
	}

	// look at the actual properties of the parent of the moving entry
	pWorldEntry = pWorldEntry->base_entry_data.parent_entry;
	if (!pWorldEntry || !pWorldEntry->full_interaction_properties) {
		return false;
	}

	for (i = 0; i < eaSize(&pWorldEntry->full_interaction_properties->eaEntries); i++) {
		WorldInteractionPropertyEntry *pEntry = pWorldEntry->full_interaction_properties->eaEntries[i];
		WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);

		if (pGateProps && pGateProps->bVolumeTriggered) {
			return true;
		}
	}
	return false;
}


bool interactable_IsInterruptable(GameInteractable *pInteractable, int iIndex)
{
	WorldInteractionPropertyEntry *pEntry;

	if (!pInteractable) {
		return false;
	}

	pEntry = interactable_GetPropertyEntry(pInteractable, iIndex);
	if (pEntry) {
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
		if (!pTimeProps ||
			(pTimeProps->bInterruptOnDamage || pTimeProps->bInterruptOnMove || pTimeProps->bInterruptOnPower)) {
				return true;
		}
	}
	return false;
}


bool interactable_IsDestructibleAndNotInteractable(GameInteractable *pInteractable)
{
	int iNumEntries;
	int i;
	bool bDestructible = false;
	bool bInteractable = false;

	if (!pInteractable) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
	for(i=0; i<iNumEntries; ++i) {
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
		const char *pcClass = interaction_GetEffectiveClass(pEntry);

		if (pcClass == pcPooled_Destructible) {
			bDestructible = true;
		} else if ((pcClass != pcPooled_NamedObject) &&
			(pcClass != pcPooled_Throwable)) {
				// All types except NamedObject, Destructible, and Throwable are interactable
				bInteractable = true;
				// Break since we know it's Interactiable and thus can not return true
				break;
		}
	}

	PERFINFO_AUTO_STOP();

	return bDestructible && !bInteractable;
}


bool interactable_IsNotDestructibleOrCanThrowObject(Entity* pEnt, GameInteractable *pInteractable, UserData *pData)
{
	WorldInteractionNode *pNode = interactable_GetWorldInteractionNode(pInteractable);
	return im_IsNotDestructibleOrCanThrowObject(pEnt, pNode, pData);
}


bool interactable_MapHasPerEntVisExpressions(void)
{
	return s_mapContainsPerEntVisExpressions;
}


WorldInteractionNode *interactable_GetWorldInteractionNode(GameInteractable *pGameInteractable)
{
	return SAFE_GET_REF(pGameInteractable, pWorldEntry->hInteractionNode);
}


// return true if has gate properties and set ppProperties if not NULL
bool interactable_HasGateProperties(WorldNamedInteractable *pWorldInteractable, WorldGateInteractionProperties **ppProperties)
{
	bool bResult = false;

	WorldInteractionEntry *pWorldInteractionEntry = SAFE_MEMBER(pWorldInteractable, entry);
	if (pWorldInteractionEntry) {
		WorldInteractionProperties *pFullInteractionProperties = pWorldInteractionEntry->full_interaction_properties;
		if (pFullInteractionProperties && pFullInteractionProperties->eaEntries) {
			int j;
			for(j = 0; j < eaSize(&pFullInteractionProperties->eaEntries); j++) {
				WorldInteractionPropertyEntry *pWorldInteractionProperty = pFullInteractionProperties->eaEntries[j];
				WorldGateInteractionProperties *pGateProperties = interaction_GetGateProperties(pWorldInteractionProperty);
				if (pGateProperties) {
					bResult = true;
					if (ppProperties) {
						*ppProperties = pGateProperties;
					}
					break;
				}
			}
		}
	}

	return bResult;
}


// ----------------------------------------------------------------------------------
// Interactable State Access Functions
// ----------------------------------------------------------------------------------

int interactable_GetPlayerOwner(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->iPlayerOwnerID;
	} else {
		return 0;
	}
}


void interactable_SetPlayerOwner(int iPartitionIdx, GameInteractable *pInteractable, int iOwner)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	pState->iPlayerOwnerID = iOwner;
}


void interactable_SetLastInteractIndex(int iPartitionIdx, GameInteractable *pInteractable, int iIndex)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	pState->iLastInteractIndex = iIndex;
}


DoorTarget *interactable_GetLastDoorTarget(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->pLastDoorTarget;
	} else {
		return NULL;
	}
}


void interactable_SetLastDoorTarget(int iPartitionIdx, GameInteractable *pInteractable, DoorTarget *pDoorTarget)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		StructDestroySafe(parse_DoorTarget, &pState->pLastDoorTarget);
		pState->pLastDoorTarget = StructClone(parse_DoorTarget, pDoorTarget);
	}
}


WorldVariable **interactable_GetLastDoorVariables(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->eaLastDoorVariables;
	} else {
		return NULL;
	}
}


void interactable_SetLastDoorVariables(int iPartitionIdx, GameInteractable *pInteractable, WorldVariable **eaVars)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		eaCopyStructs(&eaVars, &pState->eaLastDoorVariables, parse_WorldVariable);
	}
}


InteractionLootTracker *interactable_GetLootTracker(int iPartitionIdx, GameInteractable *pInteractable, bool bCreate)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		if (bCreate && !pState->pLootTracker) {
			pState->pLootTracker = StructCreate(parse_InteractionLootTracker);
			pState->pLootTracker->eaLootBags = NULL;
		}
		return pState->pLootTracker;
	} else {
		return NULL;
	}
}


InteractionLootTracker **interactable_GetLootTrackerAddress(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return &pState->pLootTracker;
	} else {
		return NULL;
	}
}


void interactable_ClearLootTracker(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		StructDestroySafe(parse_InteractionLootTracker, &pState->pLootTracker);
	}
}


// ----------------------------------------------------------------------------------
// Interactable Hide/Show/Active/Use State Functions
// ----------------------------------------------------------------------------------

bool interactable_IsActiveByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bActive;
	}
	return false;
}


bool interactable_IsDirectlyHidden(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bHidden;
	}
	return true;
}


bool interactable_IsHidden(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bHidden || pState->bParentHidden;
	}
	return true;
}


bool interactable_IsDisabled(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bDisabled;
	}
	return true;
}


int interactable_IsNodeHiddenCB(int iPartitionIdx, WorldInteractionNode *pNode)
{
	GameInteractable *pInteractable = interactable_GetByNode(pNode);
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bHidden || pState->bParentHidden;
	}
	return true;
}

int interactable_IsNodeDisabledCB(int iPartitionIdx, WorldInteractionNode *pNode)
{
	GameInteractable *pInteractable = interactable_GetByNode(pNode);
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bDisabled;
	}
	return true;
}

bool interactable_IsInUse(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		if (pState->iPlayerOwnerID) {
			Entity *pInteractEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->iPlayerOwnerID);
			if (pInteractEnt && pInteractEnt->pPlayer && pInteractEnt->pPlayer->InteractStatus.bInteracting) {
				return true;
			}
		}
	}
	return false;
}


bool interactable_IsHiddenOrDisabled(int iPartitionIdx, GameInteractable *pInteractable)
{
	if (pInteractable) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		return pState->bHidden || pState->bParentHidden || pState->bDisabled;
	}
	return true;
}


bool interactable_IsHiddenByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);
	return interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable);
}


bool interactable_IsInUseByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);
	return interactable_IsInUse(iPartitionIdx, pInteractable);
}


bool interactable_IsUsableByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);
	if (pInteractable) {
		return (!interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable) && !interactable_IsBusy(iPartitionIdx, pInteractable, NULL));
	}
	return false;
}


static void interactable_UpdateMapState(GameInteractablePartitionState *pState, GameInteractable *pInteractable)
{
	mapState_UpdateNodeEntry(pState->iPartitionIdx, pInteractable->pcNodeName, 
								pState->bHidden || pState->bParentHidden || pInteractable->bVisiblePerEntity, 
								pState->bDisabled, pState->uEntToWaitFor);
}


static void interactable_SetParentHidden(int iPartitionIdx, GameInteractable *pInteractable, bool bHide)
{
	GameInteractablePartitionState *pState;
	WorldInteractionNode *pNode;

	PERFINFO_AUTO_START_FUNC();

	pNode = GET_REF(pInteractable->pWorldEntry->hInteractionNode);
	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

	// Recurse if state changes
	if (pNode && ((bHide && !pState->bParentHidden) || (!bHide && pState->bParentHidden))) {
		// Hiding parent that was not hidden before or showing parent that was hidden
		if (!pState->bHidden) {
			// Recurse on children only if current node isn't itself hidden
			int i;
			for(i=eaSize(&pInteractable->eaChildren)-1; i>=0; --i) {
				GameInteractable *pChild = pInteractable->eaChildren[i];
				if (pState->iChildIndex < 0 || bHide) {
					interactable_SetParentHidden(iPartitionIdx, pChild, bHide);
				} else if ((pChild->iChildIndexInParent >= 0) && (pChild->iChildIndexInParent != pState->iChildIndex)) {
					interactable_SetParentHidden(iPartitionIdx, pChild, true);
				} else {
					interactable_SetParentHidden(iPartitionIdx, pChild, false);
				}
			}
		}
		pState->bParentHidden = bHide;

		worldInteractionEntrySetDisabled(iPartitionIdx, pInteractable->pWorldEntry, pState->bHidden || pState->bParentHidden || pState->bDisabled);
		interactable_UpdateMapState(pState, pInteractable);
	}

	PERFINFO_AUTO_STOP();
}


void interactable_SetHideState(int iPartitionIdx, GameInteractable *pInteractable, bool bHide, EntityRef uEntToWaitFor, bool bForce)
{
	WorldInteractionNode *pNode;
	GameInteractablePartitionState *pState;

	PERFINFO_AUTO_START_FUNC();

	pNode = GET_REF(pInteractable->pWorldEntry->hInteractionNode);
	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

	if (pNode && (bForce || (bHide != pState->bHidden))) {
		pState->bHidden = bHide;
		pState->uEntToWaitFor = uEntToWaitFor;

		// Apply the hide/show
		worldInteractionEntrySetDisabled(iPartitionIdx, pInteractable->pWorldEntry, pState->bHidden || pState->bParentHidden || pState->bDisabled);
		interactable_UpdateMapState(pState, pInteractable);

		// If parent is hidden, do nothing since that already was applied down the tree
		// Otherwise recurse down children
		if (!pState->bParentHidden) {
			int i;
			for(i=eaSize(&pInteractable->eaChildren)-1; i>=0; --i) {
				GameInteractable *pChild = pInteractable->eaChildren[i];
				if (pState->iChildIndex < 0) {
					interactable_SetParentHidden(iPartitionIdx, pChild, bHide);
				} else if ((pChild->iChildIndexInParent >= 0) && (pChild->iChildIndexInParent != pState->iChildIndex)) {
					interactable_SetParentHidden(iPartitionIdx, pChild, true);
				} else {
					interactable_SetParentHidden(iPartitionIdx, pChild, false);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


bool interactable_HideInteractableByName(int iPartitionIdx, WorldScope *pScope, const char* pcInteractableName, F32 fFadeOutTime, char **estrErrString)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);

	if (!pInteractable) {
		if (estrErrString) {
			estrPrintf(estrErrString, "Hide Interactable %s : no such interactable", pcInteractableName);
		}
		return false;
	}

	if (pInteractable->pWorldEntry) {
		if (pInteractable->pWorldEntry->full_interaction_properties && 
			!pInteractable->pWorldEntry->full_interaction_properties->bAllowExplicitHide) {
				// Error if not allowed to explicitly hide this node
				if (estrErrString) {
					estrPrintf(estrErrString, "Hide Interactable %s : This interactable is marked as unhideable", pcInteractableName);
				}
				return false;
		} else {
			interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_HIDE, 0, false );
		}
	}

	return true;
}


bool interactable_ShowInteractableByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, F32 fFadeInTime, char **estrErrString)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);

	if (!pInteractable) {
		if (estrErrString) {
			estrPrintf(estrErrString, "Hide Interactable %s : no such interactable", pcInteractableName);
		}
		return false;
	}

	interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);

	return true;
}


bool interactable_HideInteractableGroup(int iPartitionIdx, WorldScope *pScope, const char *pcGroupName, F32 fFadeOutTime, char **estrErrString)
{
	WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcGroupName);
	if (pObject && (pObject->type == WL_ENC_LOGICAL_GROUP)) {
		return interactable_ActOnInteractables(iPartitionIdx, pScope, (WorldLogicalGroup*)pObject, interactable_HideInteractableByName, fFadeOutTime, estrErrString);
	} else if (estrErrString) {
		estrPrintf(estrErrString, "Hide Interactable Group %s : no such group", pcGroupName);
	}
	return false;
}


bool interactable_ShowInteractableGroup(int iPartitionIdx, WorldScope *pScope, const char *pcGroupName, F32 fFadeInTime, char **estrErrString)
{
	WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcGroupName);
	if (pObject && (pObject->type == WL_ENC_LOGICAL_GROUP)) {
		return interactable_ActOnInteractables(iPartitionIdx, pScope, (WorldLogicalGroup*)pObject, interactable_ShowInteractableByName, fFadeInTime, estrErrString);
	} else if (estrErrString) {
		estrPrintf(estrErrString, "Show Interactable Group %s : no such group", pcGroupName);
	}
	return false;
}


void interactable_SetDisabledState(int iPartitionIdx, GameInteractable *pInteractable, bool bDisabled)
{
	WorldInteractionNode *pNode = GET_REF(pInteractable->pWorldEntry->hInteractionNode);
	if (pNode) {
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

		pState->bDisabled = bDisabled;

		// Apply the hide/show
		worldInteractionEntrySetDisabled(iPartitionIdx, pInteractable->pWorldEntry, pState->bHidden || pState->bParentHidden || pState->bDisabled);
		interactable_UpdateMapState(pState, pInteractable);
	}
}


void interactable_SetVisibleChild(int iPartitionIdx, GameInteractable *pInteractable, int iChildIndex, bool bForce)
{
	WorldInteractionNode *pNode;
	GameInteractablePartitionState *pState;

	PERFINFO_AUTO_START_FUNC();

	pNode = GET_REF(pInteractable->pWorldEntry->hInteractionNode);
	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

	if (pNode && (bForce || (iChildIndex != pState->iChildIndex))) {
		pState->iChildIndex = iChildIndex;

		// If this node isn't hidden, then child index shift affects parent hidden states
		if (!pState->bHidden && !pState->bParentHidden) {
			int i;
			for(i=eaSize(&pInteractable->eaChildren)-1; i>=0; --i) {
				GameInteractable *pChild = pInteractable->eaChildren[i];
				if ((iChildIndex >= 0) && (pChild->iChildIndexInParent >= 0) && (pChild->iChildIndexInParent != iChildIndex) && !g_InteractableVisible) {
					
					interactable_SetParentHidden(iPartitionIdx, pChild, true);
				} else {
					interactable_SetParentHidden(iPartitionIdx, pChild, false);
				}
			}
		}
	} 

	PERFINFO_AUTO_STOP();
}


bool interactable_SetVisibleChildByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, int iChildIndex, char **estrErrString)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);

	if (!pInteractable) {
		if (estrErrString) {
			estrPrintf(estrErrString, "Set Visible Child of Interactable %s : no such interactable", pcInteractableName);
		}
		return false;
	}

	interactable_SetVisibleChild(iPartitionIdx, pInteractable, iChildIndex, false);

	return true;
}


bool interactable_GetVisibleChild(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, int *result, char **estrErrString)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);
	GameInteractablePartitionState *pState;

	if (!pInteractable) {
		if (estrErrString) {
			estrPrintf(estrErrString, "Get Visible Child of Interactable %s : no such interactable", pcInteractableName);
		}
		return false;
	}

	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	*result = pState->iChildIndex;

	return true;
}


bool interactable_ResetInteractableByName(int iPartitionIdx, WorldScope *pScope, const char* pcInteractableName, F32 fUnused, char **estrErrString)
{
	GameInteractable *pInteractable = interactable_GetByName(pcInteractableName, pScope);

	if (!pInteractable) {
		if (estrErrString) {
			estrPrintf(estrErrString, "Reset Interactable %s : no such interactable", pcInteractableName);
		}
		return false;
	}

	if (pInteractable) {
		WorldInteractionNode *pNode = interactable_GetWorldInteractionNode(pInteractable);

		// reset interacted state
		if (pNode) {
			GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

			interaction_ResetInteractedNode(iPartitionIdx, pNode);

			// end exclusive interactions
			if (pState->iPlayerOwnerID) {
				Entity *pInteractEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->iPlayerOwnerID);
				if (pInteractEnt && pInteractEnt->pPlayer
					&& pInteractEnt->pPlayer->InteractStatus.bInteracting
					&& GET_REF(pInteractEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode) == pNode) {
						interaction_EndInteractionAndDialog(iPartitionIdx, pInteractEnt, false, true, true);
				}
			}
		}
	}

	return true;
}


bool interactable_IsGateOpen(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	return pState->bGateOpen;
}

int interactable_WasGateOpeningState(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	return pState->iGateWasOpeningState;
}

void interactable_SetGateWasOpeningState(int iPartitionIdx, GameInteractable *pInteractable, int iGateWasOpeningState)
{
	GameInteractablePartitionState *pState;
	
	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	pState->iGateWasOpeningState = iGateWasOpeningState;
}



// Helper function for interactable_ChangeGateOpenState
static void interactable_SetChildHideStateByIndex(int iPartitionIdx, GameInteractable *pInteractable, int iChildIndex, bool bHide)
{
	GameInteractable *pTemp, *pChild = NULL;
	int i;

	for(i=eaSize(&pInteractable->eaChildren)-1; i>=0; --i) {
		pTemp = pInteractable->eaChildren[i];
		if (pTemp->iChildIndexInParent == iChildIndex) {
			pChild = pTemp;
			break;
		}
	}
	if (pChild) {
		interactable_SetHideState(iPartitionIdx, pChild, bHide, 0, true);
	}
}


static void interactable_EntryChangeGateOpenState(int iPartitionIdx, 
												  GameInteractablePartitionState *pState,
												  GameInteractable *pInteractable,
												  WorldInteractionPropertyEntry *pEntry,
												  int iEntryIndex,
												  SA_PARAM_OP_VALID Entity *pPlayerEnt,
												  bool bNewState)
{
	WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
	if (!pState->bHidden && !pState->bParentHidden && (pPlayerEnt == NULL || pMotionProps == NULL || !pMotionProps->bTransDuringUse)) 
	{
		// Not hidden so run motion for the gate
		im_Interact(iPartitionIdx, pInteractable, NULL, NULL, pEntry, iEntryIndex, NULL);
	}
	else if ((pState->bHidden || pState->bParentHidden) && pMotionProps) 
	{
		// Gate is hidden so simulate gate behavior here so it looks right when later shown
		int i;

		for(i=eaSize(&pMotionProps->eaMoveDescriptors)-1; i>=0; --i) {
			WorldMoveDescriptorProperties *pMove = pMotionProps->eaMoveDescriptors[i];
			if (bNewState) {
				// New state is open
				interactable_SetChildHideStateByIndex(iPartitionIdx, pInteractable, pMove->iStartChildIdx, I_STATE_SHOW);
				interactable_SetChildHideStateByIndex(iPartitionIdx, pInteractable, pMove->iDestChildIdx, I_STATE_HIDE);
			} else {
				// New state is closed
				interactable_SetChildHideStateByIndex(iPartitionIdx, pInteractable, pMove->iStartChildIdx, I_STATE_HIDE);
				interactable_SetChildHideStateByIndex(iPartitionIdx, pInteractable, pMove->iDestChildIdx, I_STATE_SHOW);
			}
		}
	}
}

void interactable_ChangeGateOpenState(int iPartitionIdx, GameInteractable *pInteractable, SA_PARAM_OP_VALID Entity *pPlayerEnt, bool bNewState)
{
	GameInteractablePartitionState *pState;
	
	PERFINFO_AUTO_START_FUNC();
	
	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

	if (pState->bGateOpen != bNewState) {
		int iNumPropertyEntries = interactable_GetNumPropertyEntries(pInteractable, true);
		int i;

		for (i = 0; i < iNumPropertyEntries; i++) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
			WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);			
			if (pGateProps) 
			{
				interactable_EntryChangeGateOpenState(iPartitionIdx, pState, pInteractable, pEntry, i, pPlayerEnt, bNewState);
			}
		}

		pState->bGateOpen = bNewState;
	}

	PERFINFO_AUTO_STOP();
}

static void interactable_UpdateVolumeTriggeredGateState(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState;
	WorldInteractionEntry *pInteractEntry;
	WorldInteractionPropertyEntry *pGateEntry = NULL;
	Expression* pGateInteractCond = NULL;
	WorldVolume *pVolume = NULL;
	int i, iNumPropertyEntries;
	int iGateEntryIndex = -1;
	bool bFoundEnt = false;
	
	PERFINFO_AUTO_START_FUNC();
	
	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	iNumPropertyEntries = interactable_GetNumPropertyEntries(pInteractable, true);

	// This only modifies one entry, since the bGateOpen state lives on the partition state
	for (i = 0; i < iNumPropertyEntries; i++) {
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
		WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);
		if (pGateProps) {
			pGateEntry = pEntry;
			pGateInteractCond = interaction_GetInteractCond(pEntry);
			iGateEntryIndex = i;
			break;
		}
	}

	pInteractEntry = pInteractable->pWorldEntry;
	if (pInteractEntry && pInteractEntry->attached_volume_entry) {
		pVolume = eaGet(&pInteractEntry->attached_volume_entry->eaVolumes, iPartitionIdx);
	}

	if (pVolume) {
		// Query for entities in the volume
		WorldVolumeQueryCache **eaQueries = wlVolumeGetCachedQueries(pVolume);
					
		for(i = eaSize(&eaQueries) - 1; i >= 0; --i) {
			WorldVolumeQueryCache *pQuery = eaQueries[i];
						
			// Each "query cache" corresponds to a thing in the volume.
			if (pQuery && wlVolumeQueryCacheIsType(pQuery, s_EntityVolumeQueryType)) {
				Entity *pEnt = wlVolumeQueryCacheGetData(pQuery);
				if (entIsAlive(pEnt)) {
					if (pGateInteractCond) {
						if (interactable_EvaluateExpr(iPartitionIdx, pInteractable, pEnt, pGateInteractCond)) {
							bFoundEnt = true;
							break;
						}
					} else {
						bFoundEnt = true;
						break;
					}
				}
			}
		}
	}
	if (pState->bGateOpen != bFoundEnt) {
		interactable_EntryChangeGateOpenState(iPartitionIdx, pState, pInteractable, pGateEntry, iGateEntryIndex, NULL, bFoundEnt);
		pState->bGateOpen = bFoundEnt;
	}
	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Checking whether an Interactable can be used by a player
// ----------------------------------------------------------------------------------

// Helper function to check whether the interactable is busy or unavailable
bool interactable_IsBusy(int iPartitionIdx, GameInteractable *pInteractable, Entity *pPlayerEnt)
{
	GameInteractablePartitionState *pState;
	WorldInteractionNode *pNode = NULL;
	int iNumEntries;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (!pInteractable) {
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (pPlayerEnt) {
		assertmsgf(entGetPartitionIdx(pPlayerEnt) == iPartitionIdx, "Partition index of player %d does not match provided index of %d", entGetPartitionIdx(pPlayerEnt), iPartitionIdx);
	}

	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	if (pState->bActive || pState->bSleeping) {
		PERFINFO_AUTO_STOP();
		return true;
	}

	// If somebody else is currently using this interactable, it can't be interacted with
	// Maybe this check should be moved to interactable_EntryIsEnabled and work on a per-entry basis?
	if (pState->iPlayerOwnerID) {
		Entity *pInteractEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->iPlayerOwnerID);
		if (pInteractEnt && pInteractEnt->pPlayer && pInteractEnt->pPlayer->InteractStatus.bInteracting && (pInteractEnt != pPlayerEnt)) {
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	if (pInteractable->pWorldEntry) {
		pNode = GET_REF(pInteractable->pWorldEntry->hInteractionNode);
	}

	// If the interactable is "busy" (in cooldown), it can't be interacted with
	// Maybe this check should be moved to interactable_EntryIsEnabled and work on a per-entry basis?
	iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
	for(i=0; i<iNumEntries; ++i) {
		if (interaction_IsInteractTargetBusy2(iPartitionIdx, pPlayerEnt, 0, pNode, NULL, i)) {
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}


// Returns TRUE if the player can interact with at least one Property Entry on the node
bool interactable_CanEntityInteract(Entity *pPlayerEnt, GameInteractable *pInteractable)
{
	// Intentionally allow destructibles here, instead of using gConf.bDestructibleInteractOption, since
	//  this is the function called to find everything a player can even target, which is used on the
	//  client as a filter for actual targeting.
	return interaction_GetValidInteractNodeOptions(pInteractable, pPlayerEnt, NULL, false, false);
}

// Returns TRUE if the player can make an attempt on at least one Property Entry on the node
bool interactable_CanEntityAttempt(Entity *pPlayerEnt, GameInteractable *pInteractable)
{
	if (pInteractable==NULL)
	{
		return(false);
	}
	else
	{
		int i;
		int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, SAFE_MEMBER3(pPlayerEnt, pPlayer, missionInfo, bHasNamespaceMission));
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		WorldInteractionNode *pNode = interactable_GetWorldInteractionNode(pInteractable);
		
		for(i=0; i<iNumEntries; ++i) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
	
			PlayerDebug *pDebug = entGetPlayerDebug(pPlayerEnt, false);
			Expression *pAttemptableCond = interaction_GetAttemptableCond(pEntry);
	
			if ((pDebug && pDebug->allowAllInteractions) || !pAttemptableCond || interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pAttemptableCond))
			{
				return(true);
			}
		}
	}
	return(false);
}



bool interactable_EvaluateVisibilityForEntity(Entity *pEnt, GameInteractable* pInteractable)
{
	int iPartitionIdx;
	bool bVisible;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx( pEnt );

	bVisible = !interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable);

	// Per ent visibility requires additional checks
	if (pInteractable->bVisiblePerEntity && pInteractable->bHasVisibleExpression && bVisible) {
		int iNumEntries;
		int i;

		// Loop over all property entries
		iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
		for(i=0; i<iNumEntries && bVisible; ++i) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
			Expression *pVisibleExpr = interaction_GetVisibleExpr(pEntry);

			// Execute the visible expression
			if (pVisibleExpr) {
				// Only visible if all visible expressions are true
				bVisible = bVisible && interactable_EvaluateExpr(iPartitionIdx, pInteractable, pEnt, pVisibleExpr);
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return bVisible;
}


// ----------------------------------------------------------------------------------
// Interactable Query Functions
// ----------------------------------------------------------------------------------

static int interactable_CheckInQueryCB(GameInteractable *pInteractable, int iUnused, const Vec3 vCenter, F32 fRadius, InteractableQuery *pQuery)
{
	int iPartitionIdx;

	if (!pQuery) {
		return 0;
	}
	iPartitionIdx = pQuery->iPartitionIdx;
	
	if ((pQuery->bCheckHidden && interactable_IsHidden(iPartitionIdx, pInteractable)) || interactable_IsDisabled(iPartitionIdx, pInteractable)) {
		return 0;
	}

	if (pQuery->bCheckSelection && !interactable_IsSelectable(pInteractable)) {
		return 0;
	}

	if (!(pInteractable->uClassMask & pQuery->uClassMask)) {
		return 0;
	}

	if (!sphereSphereCollision(pInteractable->pMainOctreeEntry->bounds.mid, pInteractable->pMainOctreeEntry->bounds.radius, vCenter, fRadius)) {
		return 0;
	}

	//mulVecMat4(vCenter, local_data->inv_world_mat, local_mid);
	if (!boxSphereCollision(pInteractable->pMainOctreeEntry->bounds.min, pInteractable->pMainOctreeEntry->bounds.max, pInteractable->pMainOctreeEntry->bounds.mid, fRadius)) {
		return 0;
	}

	if (pQuery->bCheckLOS && !wlInteractionEntryCheckLineOfSight(iPartitionIdx, pInteractable->pWorldEntry, vCenter)) {
		return 0;
	}

	return 1;
}


void interactable_QuerySphere(int iPartitionIdx, U32 uClassMask, void *pUserData, const Vec3 vWorldMid, F32 fRadius, bool bCheckLOS, bool bCheckSelection, bool bCheckHidden, GameInteractable ***peaInteractables)
{
	InteractableQuery query = {0};

	query.iPartitionIdx = iPartitionIdx;
	query.uClassMask = uClassMask;
	query.pUserData = pUserData;
	query.bCheckLOS = bCheckLOS;
	query.bCheckSelection = bCheckSelection;
	query.bCheckHidden = bCheckHidden;
	
	octreeFindInSphereEA(s_pInteractableOctree, peaInteractables, vWorldMid, fRadius, interactable_CheckInQueryCB, &query);
}


// Pushes all interactables which have an entry of the specified category to the array.
void interactable_FindAllInteractablesOfCategory(GameInteractable ***peaInteractables, const char* pchCategory)
{
	int j;

	if (!pchCategory) {
		return;
	}

	FOR_EACH_INTERACTABLE(pInteractable) {
		int iNumEntries = 0;

		iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
		for(j=0; j<iNumEntries; ++j) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, j);
			const char* pcCategoryName = interaction_GetCategoryName(pEntry);
			if (pEntry && pcCategoryName!=NULL && stricmp(pcCategoryName, pchCategory) == 0) {
				eaPushUnique(peaInteractables, pInteractable);
				break;
			}
		}
	} FOR_EACH_END;
}


bool interactable_FindClosest(Entity* pEnt, float* maxDist, Vec3 vClosestPos, const char** ppchPooledClasses,
							  bool bDoValidInteractCheck, bool bIgnoreHidden, bool bCheckRegionMatch)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);	// Really only needed for bIgnore Hidden
	int j;
	F32 fMaxDistSqr = *maxDist * *maxDist;
	bool bFound = false;
	Vec3 vPos;

	entGetPos(pEnt, vPos);

	FOR_EACH_INTERACTABLE(pInteractable) {
		int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
		bool bHasClasses = false;

		if (bIgnoreHidden)
		{
			if (interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable)) {
				if (!im_FindCritterforObject(iPartitionIdx, pInteractable->pcNodeName)) {
					continue;
				}
			}
		}

		if (bCheckRegionMatch)
		{
			Vec3 vNodePos;
			interactable_GetWorldMid(pInteractable, vNodePos);
			if (worldGetWorldRegionByPos( vNodePos ) != worldGetWorldRegionByPos( vPos ))
			{
				continue;
			}
		}

		if (bDoValidInteractCheck)
		{
			if (!interaction_GetValidInteractNodeOptions(pInteractable, pEnt, NULL, false, !gConf.bDestructibleInteractOption))
			{
				continue;
			}
		}

		for(j=0; j<iNumEntries; ++j) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, j);
			assert(pEntry);

			if (!ppchPooledClasses || eaFind(&ppchPooledClasses, interaction_GetEffectiveClass(pEntry)) >= 0) {
				bHasClasses = true;
			}
		}

		if (bHasClasses) {
			WorldCellEntry *pCell;
			Vec3 vWorldMin, vWorldMax;
			F32 fDistSqr;

			// Determine the distance to the interactable
			pCell = &pInteractable->pWorldEntry->base_entry;
			mulBoundsAA(pCell->shared_bounds->local_min, pCell->shared_bounds->local_max, pCell->bounds.world_matrix, vWorldMin, vWorldMax);
			fDistSqr = distanceToBoxSquared(vWorldMin, vWorldMax, vPos);

			// Track closest interactable
			if (fDistSqr < fMaxDistSqr)
			{
				fMaxDistSqr = fDistSqr;
				bFound = true;
				if (vClosestPos) {
					// CD: use .25 feet up from the middle of the bottom of the geometry as an approximation
					*maxDist = sqrt( fMaxDistSqr );
					setVec3(vClosestPos, 0.5f * (vWorldMin[0] + vWorldMax[0]), vWorldMin[1] + 0.25f, 0.5f * (vWorldMin[2] + vWorldMax[2]));
				}
			}
		}
	} FOR_EACH_END;

	return bFound;
}


GameInteractable* interactable_FindClosestInteractableWithCheck(Entity *pEnt, 
	U32 uInteractClassMask, 
	InteractableTestCallbackEnt fTestCallback, 
	UserData pCallbackData, 
	F32 fMaxDistance, 
	bool bCheckRegionMatch, 
	bool bCheckLoS, 
	F32 *pfDistOut)
{
	Vec3 vEntPos;
	int i, iNumInteractables;
	F32 fNodeDist, fClosestDist = 0;
	F32 fCutoff;
	GameInteractable **eaInteractables=NULL;
	GameInteractable *pClosestInteractable = NULL;
	int iPartitionIdx = entGetPartitionIdx( pEnt );

	PERFINFO_AUTO_START_FUNC();

	entGetPos(pEnt, vEntPos);
	vEntPos[1] += 5.0;	// Temporary fix; check LOS from character's chest
	fCutoff = interactable_GetCutoffDist(pEnt);
	interactable_QuerySphere(iPartitionIdx, uInteractClassMask, pEnt, vEntPos, MIN(fCutoff, fMaxDistance), bCheckLoS, false, true, &eaInteractables);
	interactable_AddGlobalInteractables(iPartitionIdx, uInteractClassMask, vEntPos, fMaxDistance, fCutoff, bCheckLoS, false, true, &eaInteractables);
	iNumInteractables = eaSize(&eaInteractables);
	for (i = 0; i < iNumInteractables; i++) {
		Vec3 vNodePos;
		GameInteractable *pInteractable = eaInteractables[i];

		if (fTestCallback && !fTestCallback(pEnt, pInteractable, pCallbackData)) {
			continue;
		}

		if (interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable)) {
			if (!im_FindCritterforObject(iPartitionIdx, pInteractable->pcNodeName)) {
				continue;
			}
		}

		interactable_GetWorldMid(pInteractable, vNodePos);
		
		if (bCheckRegionMatch)
		{
			if (worldGetWorldRegionByPos( vNodePos ) != worldGetWorldRegionByPos( vEntPos ))
			{
				continue;
			}
		}

		fNodeDist = distance3(vNodePos, vEntPos);
		if (!pClosestInteractable || (fNodeDist < fClosestDist)) {
			pClosestInteractable = pInteractable;
			fClosestDist = fNodeDist;
		}
	}
	eaDestroy(&eaInteractables);

	if (pfDistOut && pClosestInteractable) {
		*pfDistOut = fClosestDist;
	}

	PERFINFO_AUTO_STOP();
	return pClosestInteractable;
}


GameInteractable* interactable_FindClosestInteractable(Entity *pEnt, U32 uInteractClassMask, F32 *pfCheckDist)
{
	return interactable_FindClosestInteractableWithCheck(pEnt, uInteractClassMask, NULL, NULL, interactable_GetNodeInteractMaxRange(pEnt),
														 false, // Region Match
														 true,  // LOSCheck
														 pfCheckDist);
}


bool interactable_FindClosestDoor(const Vec3 vPos, Vec3 vClosestDoorPos)
{
	int j;
		F32 fMaxDistSqr = FLT_MAX;
	bool bFoundDoor = false;

	FOR_EACH_INTERACTABLE(pInteractable) {
		int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
		bool bIsDoor = false;

		for(j=0; j<iNumEntries; ++j) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, j);
			assert(pEntry);
			if (interaction_GetEffectiveClass(pEntry) == pcPooled_Door) {
				bIsDoor = true;
			}
		}

		if (bIsDoor) {
			WorldCellEntry *pCell;
			Vec3 vWorldMin, vWorldMax;
			F32 fDistSqr;

			// Determine the distance to the interactable
			pCell = &pInteractable->pWorldEntry->base_entry;
			mulBoundsAA(pCell->shared_bounds->local_min, pCell->shared_bounds->local_max, pCell->bounds.world_matrix, vWorldMin, vWorldMax);
			fDistSqr = distanceToBoxSquared(vWorldMin, vWorldMax, vPos);

			// Track closest interactable
			if (fDistSqr < fMaxDistSqr) {
				fMaxDistSqr = fDistSqr;
				bFoundDoor = true;
				if (vClosestDoorPos) {
					// CD: use .25 feet up from the middle of the bottom of the door geometry as an approximation
					setVec3(vClosestDoorPos, 0.5f * (vWorldMin[0] + vWorldMax[0]), vWorldMin[1] + 0.25f, 0.5f * (vWorldMin[2] + vWorldMax[2]));
				}
			}
		}
	} FOR_EACH_END;

	return bFoundDoor;
}

static FoundDoorStruct* interactable_MaybeCreateDoorInteract( Entity* pUsePlayerEnt, GameInteractable* pInteractable, const WorldVariableDef* pDoorDest)
{
	if (pDoorDest->eDefaultType == WVARDEF_SPECIFY_DEFAULT && pDoorDest->pSpecificValue) {
				
		//pcZoneMap is a map name, but sometimes it's null and pcStringval has "missionReturn"
		if (pDoorDest->pSpecificValue->pcZoneMap || pDoorDest->pSpecificValue->pcStringVal)
		{
			bool bInclude = true;
			if (pUsePlayerEnt)
			{
				if (!interactable_CanEntityInteract(pUsePlayerEnt,pInteractable))
				{
					bInclude = false;
				}
			}

			if (bInclude)
			{
				Vec3 vWorldMin, vWorldMax;
				WorldCellEntry *pCell;
				FoundDoorStruct *pDoor = StructCreate(parse_FoundDoorStruct);

				pCell = &pInteractable->pWorldEntry->base_entry;
				mulBoundsAA(pCell->shared_bounds->local_min, pCell->shared_bounds->local_max, pCell->bounds.world_matrix, vWorldMin, vWorldMax);

				pDoor->pDoorName = strdup(pDoorDest->pSpecificValue->pcZoneMap ? pDoorDest->pSpecificValue->pcZoneMap : pDoorDest->pSpecificValue->pcStringVal);
				pDoor->vPos[0] = (vWorldMin[0] + vWorldMax[0]) /2;
				pDoor->vPos[1] = (vWorldMin[1] + vWorldMax[1]) /2;
				pDoor->vPos[2] = (vWorldMin[2] + vWorldMax[2]) /2;

				return pDoor;
			}
		}
	}

	return NULL;
}


//helper for interactable_FindAllDoors() to extract warp actions (doors) from 
// game actions.
static void interactable_addDoorsFromWorldGameActionProperties(WorldGameActionProperties ** eaActions, Entity* pUsePlayerEnt, GameInteractable* pInteractable, FoundDoorStruct ***peaDoors, WorldVariableDef*** peaOptionalDestinationCache)
{
	int actionIt;
	for( actionIt = 0; actionIt != eaSize( &eaActions ); ++actionIt ) 
	{
		WorldGameActionProperties* pAction = eaActions[ actionIt ];
		if( pAction->eActionType == WorldGameActionType_Warp && pAction->pWarpProperties ) 
		{
			WorldVariable* warpDestinationValue = pAction->pWarpProperties->warpDest.pSpecificValue;

			//don't add the door if it goes to this map and doesn't name a spawnpoint
			if(stricmp(zmapInfoGetPublicName(NULL), warpDestinationValue->pcZoneMap) || warpDestinationValue->pcStringVal)
			{
				FoundDoorStruct* pDoor = interactable_MaybeCreateDoorInteract( pUsePlayerEnt, pInteractable, &pAction->pWarpProperties->warpDest );
				if( pDoor ) 
				{
					eaPush( peaDoors, pDoor );
					if (peaOptionalDestinationCache)
					{
						eaPush(peaOptionalDestinationCache, &pAction->pWarpProperties->warpDest);
					}
				}
			}
		}
	}
}

void interactable_FindAllDoors(FoundDoorStruct ***peaDoors, Entity * pUsePlayerEnt, bool includeWarpActions)
{
	int j;
	//right now there is never more than one image menu on a map (but multiple instances in PE)
	//so this will be faster.  If there are more image menus this should be changed.
	WorldVariableDef** eaVisibleImageMenuDestinations = NULL;
	ContactImageMenuData* pCurrentImageMenuData = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (pUsePlayerEnt)
	{
		contact_EvalSetupContext(pUsePlayerEnt, NULL);
	}

	FOR_EACH_INTERACTABLE(pInteractable) {
		int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);

		for(j=0; j<iNumEntries; ++j) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, j);
			WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);
			assert(pEntry);

			if (pDoorProps) {
				FoundDoorStruct* pDoor = interactable_MaybeCreateDoorInteract( pUsePlayerEnt, pInteractable, &pDoorProps->doorDest );
				if( pDoor ) {
					eaPush( peaDoors, pDoor );
				}
			}

			if( includeWarpActions && pEntry && pEntry->pcInteractionClass == pcPooled_Clickable && pEntry->pActionProperties ) {
				interactable_addDoorsFromWorldGameActionProperties(pEntry->pActionProperties->successActions.eaActions, pUsePlayerEnt, pInteractable, peaDoors, NULL);
			}

			//doors through image menus:
			if(includeWarpActions && pEntry && pEntry->pContactProperties)
			{
				ContactDef* pContactDef = GET_REF(pEntry->pContactProperties->hContactDef);
				if(pContactDef && pContactDef->pImageMenuData){
					int i;
					if (pContactDef->pImageMenuData == pCurrentImageMenuData)
					{
						//This has the image menu we cached, so add door with this interactable but use the cached destinations.
						for(i = 0; i < eaSize(&eaVisibleImageMenuDestinations); i++)
						{
							FoundDoorStruct* pDoor = interactable_MaybeCreateDoorInteract( pUsePlayerEnt, pInteractable, eaVisibleImageMenuDestinations[i] );
							if( pDoor )
							{
								eaPush( peaDoors, pDoor );
							}
						}
					}
					else
					{
						//this is a new image menu.  Clear & rebuild the cache while getting the doors the hard way.

						ContactImageMenuItem **eaImageMenuItems = NULL;
						contact_GetImageMenuItems(pContactDef, &eaImageMenuItems);

						eaClearFast(&eaVisibleImageMenuDestinations);

						for (i = 0; i < eaSize(&eaImageMenuItems); i++)
						{
							ContactImageMenuItem *item = eaImageMenuItems[i];
							if( (!item->visibleCondition || !pUsePlayerEnt || contact_EvaluateAfterManualContextSetup(item->visibleCondition))
								&&	(!item->requiresCondition || !pUsePlayerEnt || contact_EvaluateAfterManualContextSetup(item->requiresCondition))
								&& item->action && item->action->eaActions)
							{
								interactable_addDoorsFromWorldGameActionProperties(item->action->eaActions, pUsePlayerEnt, pInteractable, peaDoors, &eaVisibleImageMenuDestinations);
							}
						}

						eaDestroy(&eaImageMenuItems);
					}
				}
			}
		}
	} FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}


void interactable_GetDoorConnections(DoorConn ***peaDoors)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	int j;

	FOR_EACH_INTERACTABLE(pInteractable) {
		int iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);

		for(j=0; j<iNumEntries; ++j) {
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, j);
			WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);
			char* pcZoneMap = NULL;
			char* pcSpawnPoint = NULL;
			assert(pEntry);

			if (  pDoorProps
				&& pDoorProps->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT
				&& pDoorProps->doorDest.pSpecificValue) {
					pcZoneMap = pDoorProps->doorDest.pSpecificValue->pcZoneMap;
					pcSpawnPoint = pDoorProps->doorDest.pSpecificValue->pcStringVal;
			}

			if (pDoorProps &&
				pcSpawnPoint &&
				(!pcZoneMap || (stricmp(pcZoneMap, pcMapName) == 0))) {

					// Determine the spawn position
					Vec3 vSpawnPosition;
					if (spawnpoint_GetSpawnPosition(pcSpawnPoint, SAFE_MEMBER(pInteractable->pWorldInteractable, common_data.closest_scope), vSpawnPosition))  {
						DoorConn *pConn;
						WorldInteractionEntry *pChild = NULL;
						WorldCellEntry **eaEntries = NULL;

						// Make a door connection
						pConn = calloc(1, sizeof(DoorConn));
						copyVec3(pInteractable->pWorldEntry->base_entry.bounds.world_mid, pConn->src);
						copyVec3(vSpawnPosition, pConn->dst);
						pConn->interactionEntry = pInteractable->pWorldEntry;
						eaPush(peaDoors, pConn);

						// Adam says this next block is required.  It fixed some bug long ago.
						// What it does is look for child objects of the interactable, and if those children
						// are also interactable, then it also makes them into doors with the same target
						eaPushEArray(&eaEntries, &pInteractable->pWorldEntry->child_entries);
						while(pChild = eaPop(&eaEntries)) {
							if(pChild->base_entry.type == WCENT_INTERACTION) {
								eaPushEArray(&eaEntries, &pChild->child_entries);
							}

							pConn = calloc(1, sizeof(DoorConn));
							copyVec3(pChild->base_entry.bounds.world_mid, pConn->src);
							copyVec3(vSpawnPosition, pConn->dst);
							pConn->interactionEntry = pChild;
							eaPush(peaDoors, pConn);
						}
					}
			}
		}
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Interactable Global Interaction List Tracking
// ----------------------------------------------------------------------------------

void interactable_ClearGlobalInteractableInfo(void)
{
	eaClearFast(&s_eaGlobalInteractionInteractables);
	s_fLastRegionCutoff = 0;
}


void interactable_AddGlobalInteractables(int iPartitionIdx, U32 uClassMask, const Vec3 vCenter, F32 fRadius, F32 fRadiusCutoff, bool bCheckLOS, bool bCheckSelection, bool bCheckHidden, GameInteractable ***peaInteractables)
{
	int i;
	InteractableQuery query = {0};

	query.iPartitionIdx = iPartitionIdx;
	query.uClassMask = uClassMask;
	query.bCheckLOS = bCheckLOS;
	query.bCheckSelection = bCheckSelection;
	query.bCheckHidden = bCheckHidden;

	// This is the list of nodes to always check against if the radius is bigger than the
	// "maximum" search radius. It allows us to keep the search radius small and still have
	// a few interactables that have a range of (for instance) the whole map
	if (fRadius >= fRadiusCutoff) {
		for (i = eaSize(&s_eaGlobalInteractionInteractables)-1; i >= 0; i--) {
			GameInteractable *pInteractable = s_eaGlobalInteractionInteractables[i];

			if (eaFind(peaInteractables, pInteractable) != -1) {
				continue; // normal search already found this node, don't add it again
			}

			if (interactable_CheckInQueryCB(pInteractable, 0, vCenter, fRadius, &query)) {
				eaPush(peaInteractables, pInteractable);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Interactable Player Data Tracking
// ----------------------------------------------------------------------------------

WorldInteractionPropertyEntry *interactable_GetPropertyEntryForPlayer(Entity* pPlayerEnt, GameInteractable *pInteractable, int iIndex)
{
	if (iIndex >= 1000 && pPlayerEnt && pPlayerEnt->pPlayer) {
		int i = iIndex - 1000;
		if (pPlayerEnt->pPlayer->InteractStatus.eaOverrideEntries && i < eaSize(&pPlayerEnt->pPlayer->InteractStatus.eaOverrideEntries)) {
			return pPlayerEnt->pPlayer->InteractStatus.eaOverrideEntries[i];
		} 
	} 

	return interactable_GetPropertyEntry(pInteractable, iIndex);
}

//Clears the list of recently clicked clickables
void interactable_ClearPlayerRecentClickableData(Entity *pPlayerEnt)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo)
		eaDestroyStruct(&pPlayerEnt->pPlayer->pInteractInfo->recentlyCompletedInteracts, parse_InteractionInfo);
}

void interactable_ClearPlayerInteractableTrackingData(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer) {

		// Clear lists of interactable objects with FX on player
		eaDestroyStruct(&pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes, parse_TargetableNode);

		// Clear interaction tracking data
		eaDestroyStruct(&pPlayerEnt->pPlayer->InteractStatus.interactOptions.eaOptions, parse_InteractOption);

		interactable_ClearPlayerRecentClickableData(pPlayerEnt);

		// Clear auto-exec tracking data
		eaDestroyStruct(&pPlayerEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions, parse_InteractOption);

		//change some interaction data on the entity that allows interaction lists to be resent properly
		pPlayerEnt->pPlayer->InteractStatus.interactTargetCounter = 0;
		pPlayerEnt->pPlayer->InteractStatus.interactCheckCounter = 0;
		
		pPlayerEnt->pPlayer->InteractStatus.bResendInteractLists = true;

		// If the player is currently interacting with an interactable, interrupt their interaction
		if (IS_HANDLE_ACTIVE((pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode))){
			interaction_DoneInteracting(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true);
		}

		// Clear interaction overrides
		eaDestroyStruct(&pPlayerEnt->pPlayer->InteractStatus.eaOverrideEntries, parse_WorldInteractionPropertyEntry);

		entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayerEnt->pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
}


void interactable_ClearAllPlayerInteractableTrackingData(void)
{
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity *pPlayerEnt;

	while ((pPlayerEnt = EntityIteratorGetNext(pIter))) {
		interactable_ClearPlayerInteractableTrackingData(pPlayerEnt);
	}
	EntityIteratorRelease(pIter);
}


// ----------------------------------------------------------------------------------
// Interactable Job Access Functions
// ----------------------------------------------------------------------------------

S64 interactable_GetAmbientCooldownTime(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	return pState->ambientJobCooldownTime;
}


void interactable_SetAmbientCooldownTime(int iPartitionIdx, GameInteractable *pInteractable, S64 iTime)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	pState->ambientJobCooldownTime = iTime;
}


S64 interactable_GetCombatJobCooldownTime(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	return pState->combatJobCooldownTime;
}


void interactable_SetCombatJobCooldownTime(int iPartitionIdx, GameInteractable *pInteractable, S64 iTime)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	pState->combatJobCooldownTime = iTime;
}


void interactable_FindAmbientJobInteractables(Vec3 vPos, F32 fRadius, GameInteractable ***pppAmbientJobInteractables)
{
	octreeFindInSphereEA(s_AmbientJobOctree, pppAmbientJobInteractables, vPos, fRadius, NULL, NULL);
}


void interactable_FindCombatJobInteractables(Vec3 vPos, F32 fRadius, GameInteractable ***pppCombatJobInteractables)
{
	octreeFindInSphereEA(s_CombatJobOctree, pppCombatJobInteractables, vPos, fRadius, NULL, NULL);
}


GameInteractable** interactable_GetAmbientJobInteractables(void)
{
	return s_eaAmbientJobInteractables;
}


GameInteractable** interactable_GetCombatJobInteractables(void)
{
	return s_eaCombatJobInteractables;
}


bool interactable_AmbientJobExists(GameInteractable *pGameInteractable, GameInteractLocation *pGameInteractLocation)
{
	// optimization: could be replaced with stash table lookup
	if (pGameInteractable && pGameInteractLocation && 
		eaFind(&s_eaAmbientJobInteractables, pGameInteractable) >= 0) {
			if (eaFind(&pGameInteractable->eaInteractLocations, pGameInteractLocation) >= 0) {
				return true;
			}
	}

	return false;
}


bool interactable_CombatJobExists(GameInteractable *pGameInteractable, GameInteractLocation *pGameInteractLocation)
{
	// optimization: could be replaced with stash table lookup
	if (pGameInteractable && pGameInteractLocation && 
		eaFind(&s_eaCombatJobInteractables, pGameInteractable) >= 0) {
			if (eaFind(&pGameInteractable->eaInteractLocations, pGameInteractLocation) >= 0) {
				return true;
			}
	}

	return false;
}


// return true if has properties and set ppProperties if not NULL
static bool interactable_HasAmbientJobProperties(WorldNamedInteractable *pWorldInteractable, 
	WorldAmbientJobInteractionProperties **ppProperties)
{
	bool bResult = false;

	WorldInteractionEntry *pWorldInteractionEntry = SAFE_MEMBER(pWorldInteractable, entry);
	if (pWorldInteractionEntry) {
		WorldInteractionProperties *pFullInteractionProperties = pWorldInteractionEntry->full_interaction_properties;
		if (pFullInteractionProperties && pFullInteractionProperties->eaEntries) {
			int j;
			for(j = 0; j < eaSize(&pFullInteractionProperties->eaEntries); j++) {
				WorldInteractionPropertyEntry *pWorldInteractionProperty = pFullInteractionProperties->eaEntries[j];
				if (pWorldInteractionProperty->pAmbientJobProperties) {
					bResult = true;
					if (ppProperties) {
						*ppProperties = pWorldInteractionProperty->pAmbientJobProperties;
					}
					break;
				}
			}
		}
	}

	return bResult;
}


static int interactable_ValidateInteractLocation(const char *pcName, int iPartitionIdx, const WorldInteractLocationProperties *pLoc, Vec3 vOutGroundSnapped, const char *pchFilename)
{
	Capsule cap = {0};
	WorldCollCollideResults wcResults;
	Vec3 vPosSource, vPosTarget;

	setVec3(cap.vStart, 0.0f, 0.0f, 0.0f);
	setVec3(cap.vDir, 0.f, 1.f, 0.f);
	cap.fLength = 5.0f;
	cap.fRadius = 0.75f;

	copyVec3(pLoc->vPos, vPosSource);
	copyVec3(pLoc->vPos, vPosTarget);

	vPosSource[1] += 5.f; // 5 feet above the interact location
	vPosTarget[1] -= 5.f; // 5 feet below the interact location

	if (!worldCollideRay(iPartitionIdx, vPosSource, vPosTarget, WC_QUERY_BITS_WORLD_ALL, &wcResults)) 
	{
		if (!gConf.bSilentInteractLocationValidation)
		{
			if (pchFilename)
			{
				ErrorFilenamef(pchFilename, "An interact location could not be validated. Please make sure it is placed on the ground. Interactable Name: %s, Interact location: [%.2f, %.2f, %.2f].", 
					NULL_TO_EMPTY(pcName), pLoc->vPos[0], pLoc->vPos[1], pLoc->vPos[2]);
			}
			else
			{
				Errorf("An interact location could not be validated. Please make sure it is placed on the ground. Interactable Name: %s, Interact location: [%.2f, %.2f, %.2f].", 
					NULL_TO_EMPTY(pcName), pLoc->vPos[0], pLoc->vPos[1], pLoc->vPos[2]);
			}
		}

		return false;
	}

	// Set the ground position
	copyVec3(wcResults.posWorldImpact, vOutGroundSnapped);

	if (gConf.bLightInteractLocationValidation)
	{
		cap.fLength = 2.0f;
	}
	else
	{
		// Set the capsule position
		copyVec3(vOutGroundSnapped, vPosSource);
		vPosSource[1] += 1.25f; // Capsule is 1.25 feet above the ground
	}

	if (wcCapsuleCollideCheck(worldGetActiveColl(iPartitionIdx), 
		&cap, vPosSource, WC_QUERY_BITS_WORLD_ALL, &wcResults))
	{
		if (!gConf.bSilentInteractLocationValidation)
		{
			if (pchFilename)
			{
				ErrorFilenamef(pchFilename, "An interact location could not be validated. Please make sure it is not blocked by static geometry. Interactable Name: %s, Interact location: [%.2f, %.2f, %.2f].",
					NULL_TO_EMPTY(pcName), pLoc->vPos[0], pLoc->vPos[1], pLoc->vPos[2]);
			}
			else
			{
				Errorf("An interact location could not be validated. Please make sure it is not blocked by static geometry. Interactable Name: %s, Interact location: [%.2f, %.2f, %.2f].",
					NULL_TO_EMPTY(pcName), pLoc->vPos[0], pLoc->vPos[1], pLoc->vPos[2]);
			}
		}
		return false;
	}

	return true;
}


static bool interactable_HasValidAmbientJobLocation(GameInteractable *pGameInteractable)
{
	if (pGameInteractable) {
		FOR_EACH_IN_EARRAY_FORWARDS(pGameInteractable->eaInteractLocations, GameInteractLocation, pInteractLocation) {
			if (pInteractLocation && 
				pInteractLocation->pWorldInteractLocationProperties &&
				(REF_HANDLE_IS_ACTIVE(pInteractLocation->pWorldInteractLocationProperties->hFsm) || eaSize(&pInteractLocation->pWorldInteractLocationProperties->eaAnims) > 0)
				) {
					return true;
			}
		}
		FOR_EACH_END
	}

	return false;
}


static void interactable_AddAmbientJobInteractable(GameInteractable *pGameInteractable, WorldAmbientJobInteractionProperties *pAmbientJobProperties)
{
	// check if this has any job locations, if not, do not add it
	if (!interactable_HasValidAmbientJobLocation(pGameInteractable)) {
		return;
	}

	pGameInteractable->pAmbientJobProperties = pAmbientJobProperties;

	// Add to the specialized (exclusive) list of interactables
	eaPush(&s_eaAmbientJobInteractables, pGameInteractable);

	if (pGameInteractable->pWorldEntry && GET_REF(pGameInteractable->pWorldEntry->hInteractionNode)) {
		WorldInteractionNode *pNode;
		Vec3 vMidPos;

		pGameInteractable->pAmbientJobOctreeEntry = calloc(1, sizeof(OctreeEntry));

		interactable_GetWorldMid(pGameInteractable, vMidPos);

		pNode = GET_REF(pGameInteractable->pWorldEntry->hInteractionNode);
		pGameInteractable->pAmbientJobOctreeEntry->node = pGameInteractable;
		copyVec3(vMidPos, pGameInteractable->pAmbientJobOctreeEntry->bounds.mid);
		pGameInteractable->pAmbientJobOctreeEntry->bounds.radius = wlInteractionNodeGetRadius(pNode);
		subVec3same(vMidPos, pGameInteractable->pAmbientJobOctreeEntry->bounds.radius, pGameInteractable->pAmbientJobOctreeEntry->bounds.min);
		addVec3same(vMidPos, pGameInteractable->pAmbientJobOctreeEntry->bounds.radius, pGameInteractable->pAmbientJobOctreeEntry->bounds.max);

		octreeAddEntry(s_AmbientJobOctree, pGameInteractable->pAmbientJobOctreeEntry, OCT_ROUGH_GRANULARITY);
	}
}


static bool interactable_HasValidCombatJobLocation(GameInteractable *pGameInteractable)
{
	if (pGameInteractable) {
		FOR_EACH_IN_EARRAY_FORWARDS(pGameInteractable->eaInteractLocations, GameInteractLocation, pInteractLocation) {
			if (pInteractLocation && 
				pInteractLocation->pWorldInteractLocationProperties &&
				REF_HANDLE_IS_ACTIVE(pInteractLocation->pWorldInteractLocationProperties->hSecondaryFsm)) {
					return true;
			}
		}
		FOR_EACH_END
	}

	return false;
}


static void interactable_AddCombatJobInteractable(GameInteractable *pGameInteractable, WorldAmbientJobInteractionProperties *pAmbientJobProperties)
{
	// check if this has any job locations, if not, do not add it
	if (!interactable_HasValidCombatJobLocation(pGameInteractable)) {
		return;
	}

	pGameInteractable->pAmbientJobProperties = pAmbientJobProperties;

	// Add to the specialized (exclusive) list of interactables
	eaPush(&s_eaCombatJobInteractables, pGameInteractable);

	if (pGameInteractable->pWorldEntry && GET_REF(pGameInteractable->pWorldEntry->hInteractionNode)) {
		WorldInteractionNode *pNode;
		Vec3 vMidPos;

		pGameInteractable->pCombatJobOctreeEntry = calloc(1, sizeof(OctreeEntry));

		interactable_GetWorldMid(pGameInteractable, vMidPos);

		pNode = GET_REF(pGameInteractable->pWorldEntry->hInteractionNode);
		pGameInteractable->pCombatJobOctreeEntry->node = pGameInteractable;
		copyVec3(vMidPos, pGameInteractable->pCombatJobOctreeEntry->bounds.mid);
		pGameInteractable->pCombatJobOctreeEntry->bounds.radius = wlInteractionNodeGetRadius(pNode);
		subVec3same(vMidPos, pGameInteractable->pCombatJobOctreeEntry->bounds.radius, pGameInteractable->pCombatJobOctreeEntry->bounds.min);
		addVec3same(vMidPos, pGameInteractable->pCombatJobOctreeEntry->bounds.radius, pGameInteractable->pCombatJobOctreeEntry->bounds.max);

		octreeAddEntry(s_CombatJobOctree, pGameInteractable->pCombatJobOctreeEntry, OCT_ROUGH_GRANULARITY);
	}
}


// ----------------------------------------------------------------------------------
// Interactable Initialization and Cleanup
// ----------------------------------------------------------------------------------

static void interactable_FreeInteractLocationPartition(GameInteractLocationPartition *pState)
{
	free(pState);
}


static void interactable_FreeAmbientJobInteractable(GameInteractLocation *pInteractLoc)
{
	eaDestroyEx(&pInteractLoc->eaPartitions, interactable_FreeInteractLocationPartition);

	StructDestroy(parse_WorldInteractLocationProperties, pInteractLoc->pWorldInteractLocationProperties);
}

static void interactable_FreePartitionState(GameInteractablePartitionState *pState)
{
	int i;

	// Stop tracking events
	for(i=eaSize(&pState->eaTrackedEvents)-1; i>=0; --i) {
		eventtracker_StopTracking(pState->iPartitionIdx, pState->eaTrackedEvents[i], pState->pInteractable);
	}
	eaDestroyStruct(&pState->eaTrackedEvents, parse_GameEvent);

	// Free the event log
	stashTableDestroy(pState->stEventLog);

	// Free the loot tracker
	if (pState->pLootTracker)
		StructDestroySafe(parse_InteractionLootTracker, &pState->pLootTracker);

	// End motion (if any) for the interaction
	im_ForceEndMotionForNode(pState->iPartitionIdx, GET_REF(pState->pInteractable->pWorldEntry->hInteractionNode), true);

	// Free door data
	StructDestroySafe(parse_DoorTarget, &pState->pLastDoorTarget);
	eaDestroyStruct(&pState->eaLastDoorVariables, parse_WorldVariable);

	// Free the state
	free(pState);
}


static void interactable_FreeGameInteractable(GameInteractable *pGameInteractable)
{
	// Clean up partition specific data
	eaDestroyEx(&pGameInteractable->eaPartitionStates, interactable_FreePartitionState);

	// Clean up jobs
	eaDestroyEx(&pGameInteractable->eaInteractLocations, interactable_FreeAmbientJobInteractable);

	// Remove from oct trees
	if (pGameInteractable->pMainOctreeEntry) {
		free(pGameInteractable->pMainOctreeEntry);
	}
	if (pGameInteractable->pAmbientJobOctreeEntry) {
		free(pGameInteractable->pAmbientJobOctreeEntry);
	}
	if (pGameInteractable->pCombatJobOctreeEntry) {
		free(pGameInteractable->pCombatJobOctreeEntry);
	}

	// Clean up unshared events
	eaDestroyStruct(&pGameInteractable->eaUnsharedTrackedEvents, parse_GameEvent);

	estrDestroy(&pGameInteractable->estrDebugLog);
	eaDestroy(&pGameInteractable->eaInteractableCategories);
	eaDestroy(&pGameInteractable->eaTags);
	eaDestroy(&pGameInteractable->eaChildren);
	free(pGameInteractable);
}


static void interactable_InitMotionNodes(GameInteractable *pInteractable, GameInteractablePartitionState *pState, WorldInteractionPropertyEntry *pEntry)
{
	WorldInteractionNode *pMainNode = interactable_GetWorldInteractionNode(pInteractable);
	WorldInteractionEntry *pInteractEntry = pMainNode ? wlInteractionNodeGetEntry(pMainNode) : NULL;
	WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);

	if (pMotionProps && pInteractEntry) {
		int i,j;
		for (i = 0; i < eaSize(&pInteractEntry->child_entries); i++) {
			WorldCellEntry *pChildEntry = pInteractEntry->child_entries[i];
			if (pChildEntry->type == WCENT_INTERACTION) {
				GameInteractable *pChildInteractable;
				bool bHide = true;

				// Get the child interactable
				pChildInteractable = interactable_GetByEntry((WorldInteractionEntry*)pChildEntry);
				if (!pChildInteractable) {
					// Movable nodes can have "fake" interactable entries that need to be tracked
					// but are not fully operational
					pChildInteractable = interactable_AddFakeInteractable(pState->iPartitionIdx, pInteractable, (WorldInteractionEntry*)pChildEntry);
					assert(pChildInteractable);
				}

				// Look through movement descriptors to find proper starting state
				for (j = 0; j < eaSize(&pMotionProps->eaMoveDescriptors); j++) {
					WorldMoveDescriptorProperties *pDescriptor = pMotionProps->eaMoveDescriptors[j];
					if (pDescriptor->iDestChildIdx >= 0 && pDescriptor->iDestChildIdx != pDescriptor->iStartChildIdx) {
						WorldCellEntryData *pChildData = worldCellEntryGetData(pChildEntry);
						if (pState->bGateOpen) {
							if (pChildData->interaction_child_idx == pDescriptor->iDestChildIdx) {
								bHide = false;
							}
						} else {
							if (pChildData->interaction_child_idx == pDescriptor->iStartChildIdx) {
								bHide = false;
							}
						}
					}
				}
				if (bHide) {
					interactable_SetHideState(pState->iPartitionIdx, pChildInteractable, I_STATE_HIDE, 0, true);
				} else {
					interactable_SetHideState(pState->iPartitionIdx, pChildInteractable, I_STATE_SHOW, 0, true);
				}
			}
		}
	}
}


static void interactable_InitVisibleChildren(GameInteractable *pInteractable, GameInteractablePartitionState *pState, WorldInteractionProperties *pProps, WorldInteractionNode *pNode, WorldInteractionEntry *pEntry)
{
	if (pProps->pChildProperties) {
		int i;

		// Make sure child entries have Game Interactables
		for (i = 0; i < eaSize(&pEntry->child_entries); i++) {
			WorldCellEntry *pChildEntry = pEntry->child_entries[i];
			if (pChildEntry->type == WCENT_INTERACTION) {
				// Get the child interactable
				GameInteractable *pChildInteractable = interactable_GetByEntry((WorldInteractionEntry*)pChildEntry);
				if (!pChildInteractable) {
					// Movable nodes can have "fake" interactable entries that need to be tracked
					// but are not fully operational
					pChildInteractable = interactable_AddFakeInteractable(pState->iPartitionIdx, pInteractable, (WorldInteractionEntry*)pChildEntry);
					assert(pChildInteractable);
				}
			}
		}

		// Process the visible children state
		if (pInteractable->bHasChildExpression && pNode && pProps->pChildProperties->pChildSelectExpr) {
			int iChildIndex = interactable_EvaluateNonPlayerExpr(pState->iPartitionIdx, pInteractable, pProps->pChildProperties->pChildSelectExpr);
			interactable_SetVisibleChild(pState->iPartitionIdx, pInteractable, iChildIndex, true);
		}
		else if (pNode && pProps->pChildProperties->iChildIndex >= 0) {
			interactable_SetVisibleChild(pState->iPartitionIdx, pInteractable, pProps->pChildProperties->iChildIndex, true);
		}			
	} else {
		// Want to make sure this gets called on every branch through here
		interactable_SetVisibleChild(pState->iPartitionIdx, pInteractable, I_NO_CHILD_INDEX, true);
	}
}


static void interactable_InitInteractablesFromLogicalGroup(int iPartitionIdx, WorldLogicalGroup *pLogicalGroup)
{
	GameInteractable** eaInteractables = NULL;
	GameInteractable** eaSleepInteractables = NULL;
	int i;
	int iNumToSpawn;

	if (pLogicalGroup && pLogicalGroup->properties && 
		((pLogicalGroup->properties->interactableSpawnProperties.eRandomType != LogicalGroupRandomType_None) ||
		(pLogicalGroup->properties->interactableSpawnProperties.fLockoutRadius > 0))
		) {
			// Collect all Interactables
			for (i=eaSize(&pLogicalGroup->objects)-1; i>=0; --i){
				WorldEncounterObject *pObject = pLogicalGroup->objects[i];
				if (pObject && pObject->type == WL_ENC_INTERACTABLE) {
					WorldInteractionEntry *pEntry = ((WorldNamedInteractable*)pLogicalGroup->objects[i])->entry;
					GameInteractable *pInteractable = interactable_GetByEntry(pEntry);
					if (pInteractable) {
						eaPush(&eaInteractables, pInteractable);
					}
				}
			}

			if (pLogicalGroup->properties->interactableSpawnProperties.eRandomType == LogicalGroupRandomType_None) {
				iNumToSpawn = eaSize(&eaInteractables);
			} else if (pLogicalGroup->properties->interactableSpawnProperties.eSpawnAmountType == LogicalGroupSpawnAmountType_Percentage) {
				iNumToSpawn = ((F32)pLogicalGroup->properties->interactableSpawnProperties.uSpawnAmount/100.f) * eaSize(&eaInteractables);
			} else {
				iNumToSpawn = pLogicalGroup->properties->interactableSpawnProperties.uSpawnAmount;
			}

			// Pick the ones that are supposed to spawn
			if (pLogicalGroup->properties->interactableSpawnProperties.fLockoutRadius > 0) {
				while((iNumToSpawn > 0) && (eaSize(&eaInteractables) > 0)) {
					GameInteractable *pSpawnInteractable;
					Vec3 vSpawnPos = { 0,0,0 };
					Vec3 vTestPos = { 0,0,0 };
					F32 fRadiusSquared = pLogicalGroup->properties->interactableSpawnProperties.fLockoutRadius * pLogicalGroup->properties->interactableSpawnProperties.fLockoutRadius;
					int iRandom = randomIntRange(0, eaSize(&eaInteractables) - 1);

					// Pick a random one to spawn
					if(s_LogicalRandomLock>=0 && s_LogicalRandomLock < eaSize(&eaInteractables))
						iRandom = s_LogicalRandomLock;

					pSpawnInteractable = eaInteractables[iRandom];
					interactable_GetPosition(pSpawnInteractable, vSpawnPos);
					eaRemove(&eaInteractables, iRandom);
					--iNumToSpawn;

					// Remove all others from the list that are within lockout radius of this one
					for(i=eaSize(&eaInteractables)-1; i>=0; --i) {
						GameInteractable *pTestInteractable = eaInteractables[i];
						interactable_GetPosition(pTestInteractable, vTestPos);
						if (fRadiusSquared > distance3Squared(vSpawnPos, vTestPos)) {
							eaPush(&eaSleepInteractables, pTestInteractable);
							eaRemove(&eaInteractables, i);
						}
					}
				}
				// Any ones not picked for spawn and not already removed for lockout also go to sleep
				for(i=eaSize(&eaInteractables)-1; i>=0; --i) {
					eaPush(&eaSleepInteractables, eaInteractables[i]);
				}
				eaDestroy(&eaInteractables);
			} else {
				// Simple if no lockout radius.  Just remove the number that need to spawn.
				for (i=iNumToSpawn-1; i>=0; --i){
					int iRandom = (eaSize(&eaInteractables) > 0) ? randomIntRange(0, eaSize(&eaInteractables) - 1) : 0;
					if(s_LogicalRandomLock>=0 && s_LogicalRandomLock < eaSize(&eaInteractables))
						iRandom = s_LogicalRandomLock;
					eaRemove(&eaInteractables, iRandom);
				}
				eaSleepInteractables = eaInteractables;
			}

			// Put the proper ones to sleep
			for (i=eaSize(&eaSleepInteractables)-1; i>=0; --i){
				WorldInteractionPropertyEntry *pEntry;
				WorldTimeInteractionProperties *pTimeProps;
				GameInteractablePartitionState *pState;

				// Set to sleeping
				pState = interactable_GetPartitionState(iPartitionIdx, eaSleepInteractables[i]);
				pState->bSleeping = true;

				// They should be hidden if they are Hide During Cooldown.  Just checks the first Interaction Entry.
				pEntry = interactable_GetPropertyEntry(eaSleepInteractables[i], 0);
				pTimeProps = interaction_GetTimeProperties(pEntry);
				if (pTimeProps && pTimeProps->bHideDuringCooldown) {
					interactable_SetHideState(iPartitionIdx, eaSleepInteractables[i], I_STATE_HIDE, 0, true);
				}
			}
	}
	eaDestroy(&eaSleepInteractables);
}


static void interactable_InitCachedData(GameInteractable *pInteractable, WorldInteractionEntry *pEntry)
{
	WorldInteractionProperties *pProps = pEntry->full_interaction_properties;
	WorldInteractionNode *pNode = GET_REF(pEntry->hInteractionNode);
	RegionRules* pRules = NULL;
	F32 fCutoff = FLT_MAX;

	if (!pProps)
		return;

	if (pNode) {
		WorldRegion* pRegion;
		Vec3 vPos;

		wlInteractionNodeGetWorldMid( pNode, vPos );
		pRegion = worldGetWorldRegionByPos( vPos );
		pRules = pRegion ? getRegionRulesFromRegionType(worldRegionGetType(pRegion)) : NULL;

		fCutoff = pRules ? pRules->fInteractDistCutoff : DEFAULT_NODE_INTERACT_DIST_CUTOFF;
		s_fLastRegionCutoff = fCutoff;
	}

	// Set cached distances
	pProps->uInteractDistCached = pProps->uInteractDist;
	pProps->uTargetDistCached = pProps->uTargetDist;

	// If distance is 0, then it is assumed that the user wants the default range
	if ((pProps->uTargetDistCached == 0) || (pProps->uInteractDistCached == 0)) { 

		if (pProps->uInteractDistCached == 0) {
			if (pRules && (pRules->fDefaultInteractDist > 0.0)) {
				pProps->uInteractDistCached = pRules->fDefaultInteractDist;
			} else {
				pProps->uInteractDistCached = DEFAULT_NODE_INTERACT_DIST;
			}
		}

		if (pProps->uTargetDistCached == 0) {
			S32 i;
			U32 uTargetDist = 0;

			// Check to see if there's a override target range for this interactable
			for (i = eaSize(&g_eaOptionalActionCategoryDefs)-1; i >= 0; i--) {
				WorldOptionalActionCategoryDef* pOptActCatDef = g_eaOptionalActionCategoryDefs[i];
				
				if (eaFind(&pInteractable->eaInteractableCategories, pOptActCatDef->pcName) >= 0) {
					if (pOptActCatDef->uOverrideTargetDist > uTargetDist) {
						uTargetDist = pOptActCatDef->uOverrideTargetDist;
					}
				}
			}
			// Use the override range, if found. Else try the default range on the RegionRules. Else use the global value.
			if (uTargetDist > 0) {
				pProps->uTargetDistCached = uTargetDist;
			} else if (pRules && (pRules->fDefaultInteractTargetDist > 0.0)) {
				pProps->uTargetDistCached = (U32)pRules->fDefaultInteractTargetDist;
			} else {
				pProps->uTargetDistCached =  DEFAULT_NODE_TARGET_DIST;
			}
		}
	}
	if (pProps->uInteractDistCached > fCutoff) 
	{
		pProps->bPastDistCutoff = true;
	}
	if (pProps->uTargetDistCached > fCutoff) 
	{
		pProps->bPastDistCutoff = true;
	}
}


static void interactable_InitPartitionPropertyEntry(GameInteractable *pInteractable, GameInteractablePartitionState *pState, WorldInteractionProperties *pProps, const char *pcFilename, const char *pcName, WorldInteractionPropertyEntry *pEntry)
{
	const char *pchEffectiveClass = interaction_GetEffectiveClass(pEntry);

	pState->bGateOpen = false;

	if (pchEffectiveClass == pcPooled_Gate) {
		WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);
		if (pGateProps) {
			pState->bGateOpen = pGateProps->bStartState;

			if(pGateProps->bVolumeTriggered)
				s_bHasVolumeTriggeredGates = true;
		}
	}

	// Set initial hidden children for interactables with motion properties
	interactable_InitMotionNodes(pInteractable, pState, pEntry);
}


static void interactable_InitBasePropertyEntry(GameInteractable *pInteractable, WorldInteractionProperties *pProps, const char *pcFilename, const char *pcName, WorldInteractionPropertyEntry *pEntry)
{
	Expression *pVisibleExpr = interaction_GetVisibleExpr(pEntry);
	const char *pchEffectiveClass;

	interaction_InitPropertyEntry(pEntry, g_pInteractionContext, pcFilename, "Interactable", pcName, pProps->bEvalVisExprPerEnt);

	if (pVisibleExpr) {
		pInteractable->bHasVisibleExpression = true;
		if (pProps->bEvalVisExprPerEnt) {
			pInteractable->bVisiblePerEntity = true;
			s_mapContainsPerEntVisExpressions = true;
		}
	}

	pchEffectiveClass = interaction_GetEffectiveClass(pEntry);

	// Can interact if it is not a named object
	if (pchEffectiveClass != pcPooled_NamedObject && 
		pchEffectiveClass != pcPooled_AmbientJob && 
		pchEffectiveClass != pcPooled_CombatJob) {
		pInteractable->bCanBeInteractedWith = true;
	}

	if (pchEffectiveClass == pcPooled_Destructible) {
		pInteractable->bIsDestructible = true;
	}

	if (pchEffectiveClass == pcPooled_Gate) {
		WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);
		if (pGateProps) {
			pInteractable->bIsVolumeTriggeredGate = pGateProps->bVolumeTriggered;
			pInteractable->pCritterUseCond = exprClone(pGateProps->pCritterUseCond);
		}
	}
}


static void interactable_InitParentage(GameInteractable *pInteractable)
{
	WorldNamedInteractable *pWorldInteractable = SAFE_MEMBER(pInteractable, pWorldInteractable);

	if (pWorldInteractable) {
		WorldNamedInteractable *pParentObject = NULL;
		GameInteractable *pParent = NULL;

		if (pWorldInteractable->common_data.parent_node_entry) {
			pParent = interactable_GetByEntry(pWorldInteractable->common_data.parent_node_entry);
			pParentObject = pParent->pWorldInteractable;
		} else if (pWorldInteractable->common_data.parent_node_object) {
			pParentObject = pWorldInteractable->common_data.parent_node_object;
			if (pParentObject) {
				// find the parent
				FOR_EACH_INTERACTABLE(pInteractable2) {
					if (pInteractable2->pWorldInteractable == pParentObject) {
						pParent = pInteractable2;
						break;
					}
				} FOR_EACH_END
			}
			if (!pParent) {
				Errorf("Parent entry of node %s is not a node", pInteractable->pcName);
			}
		}

		// Apply the parent information
		if (pParent) {
			// Set up parent/child relationship
			assert(pParent != pInteractable); // Make sure does not point to itself as a parent
			pInteractable->pParent = pParent;
			pInteractable->iChildIndexInParent = pWorldInteractable->common_data.parent_node_child_idx;
			eaPush(&pParent->eaChildren, pInteractable);
		}
	}
}


static void interactable_EarlyInitPartition(int iPartitionIdx, GameInteractable *pInteractable, GameInteractablePartitionState *pState)
{
	F32 fRandomCooldown;

	// Fake interactables have no world interactable
	if (!pInteractable || !pInteractable->pWorldInteractable) {
		return;
	}

	// Basic init		
	pState->bActive = false;
	pState->bSleeping = false;
	pState->bHidden = false;
	pState->bDisabled = false;
	pState->iChildIndex = I_NO_CHILD_INDEX;

	// Set up random cooldown
	fRandomCooldown = randomPositiveF32() * 10.f;
	pState->ambientJobCooldownTime = ABS_TIME_PARTITION(iPartitionIdx) + SEC_TO_ABS_TIME(fRandomCooldown);

	// Create event log
	if (!pState->stEventLog) {
		pState->stEventLog = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
	}
}


static void interactable_LateInitPartition(GameInteractable *pInteractable, GameInteractablePartitionState *pState)
{
	WorldInteractionEntry *pEntry = SAFE_MEMBER(pInteractable, pWorldEntry);
	WorldInteractionNode *pNode = pEntry ? GET_REF(pEntry->hInteractionNode) : NULL;
	int iPartitionIdx = pState->iPartitionIdx;
	int i;

	// Fake interactables have no world interactable
	if (!pInteractable || !pInteractable->pWorldInteractable) {
		return;
	}

	if (pEntry && pEntry->full_interaction_properties) {
		WorldInteractionProperties *pProps = pEntry->full_interaction_properties;

		// Initialize visibility state
		if (pEntry && pNode) {
			bool bHidden = false;
			if (pEntry->full_interaction_properties) {
				bHidden = pEntry->full_interaction_properties->bStartsHidden;
			}
			interactable_SetHideState(iPartitionIdx, pInteractable, bHidden, 0, true);
		}

		// Set initial hidden children for interactables with motion properties
		interactable_InitVisibleChildren(pInteractable, pState, pProps, pNode, pEntry);

		// Init entries
		for(i=eaSize(&pProps->eaEntries)-1; i>=0; --i) {
			interactable_InitPartitionPropertyEntry(pInteractable, pState, pProps, layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), pInteractable->pcName, pProps->eaEntries[i]);
		}

		// Init overrides
		for(i=eaSize(&pInteractable->eaOverrides)-1; i>=0; --i) {
			GameInteractableOverride *pOverride = pInteractable->eaOverrides[i];
			interactable_InitPartitionPropertyEntry(pInteractable, pState, pProps, pOverride->pcFilename, pInteractable->pcName, pOverride->pEntry);
		}
	}

	// Copy events for this partition
	eaDestroyStruct(&pState->eaTrackedEvents, parse_GameEvent);
	for(i=0; i<eaSize(&pInteractable->eaUnsharedTrackedEvents); ++i) {
		GameEvent *pEvent = StructClone(parse_GameEvent, pInteractable->eaUnsharedTrackedEvents[i]);
		pEvent->iPartitionIdx = iPartitionIdx;
		eaPush(&pState->eaTrackedEvents, pEvent);
	}

	// Start tracking events for this partition
	for(i=eaSize(&pState->eaTrackedEvents)-1; i>=0; --i) {
		eventtracker_StartTracking(pState->eaTrackedEvents[i], SAFE_MEMBER2(pInteractable, pWorldInteractable, common_data.closest_scope), pInteractable, interactable_EventCountAdd, interactable_EventCountSet);
	}

	// On partition load force update of map state for the interactable
	interactable_UpdateMapState(pState, pInteractable);
}


// Init the data that is not partition specific
static void interactable_InitBase(GameInteractable *pInteractable)
{
	WorldInteractionEntry *pEntry = SAFE_MEMBER(pInteractable, pWorldEntry);
	WorldInteractionNode *pNode = pEntry ? GET_REF(pEntry->hInteractionNode) : NULL;
	int i, j;

	// Fake interactables have no world interactable
	if (!pInteractable || !pInteractable->pWorldInteractable) {
		return;
	}

	eaClear(&pInteractable->eaInteractableCategories);
	eaClear(&pInteractable->eaTags);

	pInteractable->bHasVisibleExpression = false;
	pInteractable->bIsVolumeTriggeredGate = false;
	pInteractable->bVisiblePerEntity = false;
	pInteractable->bHasChildExpression = false;
	pInteractable->bCanBeInteractedWith = false;
	pInteractable->bIsDestructible = false;

	// Set up based on internal fields
	if (pEntry && pEntry->full_interaction_properties) {
		WorldInteractionProperties *pProps = pEntry->full_interaction_properties;

		// Set the game interactable into the player and non-player contexts
		exprContextSetPointerVarPooled(g_pInteractionContext, g_InteractableExprVarName, pInteractable, parse_GameInteractable, false, true);
		exprContextSetScope(g_pInteractionContext, SAFE_MEMBER(pInteractable->pWorldInteractable, common_data.closest_scope));
		exprContextSetPointerVarPooled(g_pInteractionNonPlayerContext, g_InteractableExprVarName, pInteractable, parse_GameInteractable, false, true);
		exprContextSetScope(g_pInteractionNonPlayerContext, SAFE_MEMBER(pInteractable->pWorldInteractable, common_data.closest_scope));

		if (pProps->pChildProperties) {
			if (pProps->pChildProperties->pChildSelectExpr) {
				if (!exprGenerate(pProps->pChildProperties->pChildSelectExpr, g_pInteractionNonPlayerContext)) {
					ErrorFilenamef(layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), 
						"Interactable %s has an invalid State Expression.", pInteractable->pcName);
				}
				pInteractable->bHasChildExpression = true;
			}
			if (pProps->pChildProperties->iChildIndex >= 0) {
				pInteractable->bHasChildIndex = true;
			}
		}

		// Init entries
		for(i=eaSize(&pProps->eaEntries)-1; i>=0; --i) {
			interactable_InitBasePropertyEntry(pInteractable, pProps, layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), pInteractable->pcName, pProps->eaEntries[i]);
		
			// Collect category names
			{
				const char* pcCategoryName = interaction_GetCategoryName(pProps->eaEntries[i]);
				if (pcCategoryName) 
				{
					eaPushUnique(&pInteractable->eaInteractableCategories, pcCategoryName);
				}
			}

			for(j=eaSize(&pProps->eaInteractionTypeTag)-1; j>=0; --j)
				eaPushUnique(&pInteractable->eaTags, pProps->eaInteractionTypeTag[j]);
		}

		// Init overrides
		for(i=eaSize(&pInteractable->eaOverrides)-1; i>=0; --i) {
			GameInteractableOverride *pOverride = pInteractable->eaOverrides[i];
			interactable_InitBasePropertyEntry(pInteractable, pProps, pOverride->pcFilename, pInteractable->pcName, pOverride->pEntry);
		}

		// Clear values from the world context to avoid cross-talk
		exprContextClearPartition(g_pInteractionContext);
		exprContextRemoveVarPooled(g_pInteractionContext, g_InteractableExprVarName);
		exprContextSetScope(g_pInteractionContext, NULL);
		exprContextClearPartition(g_pInteractionNonPlayerContext);
		exprContextRemoveVarPooled(g_pInteractionNonPlayerContext, g_InteractableExprVarName);
		exprContextSetScope(g_pInteractionNonPlayerContext, NULL);
	}

	// Init Cached Data
	if (pEntry) {
		interactable_InitCachedData(pInteractable, pEntry);
	}
}


void interactable_InitOverridesMatchingName(const char *pcName)
{
	FOR_EACH_INTERACTABLE(pInteractable) {
		int i;
		for (i=0; i < eaSize(&pInteractable->eaOverrides); i++) {
			if (stricmp(pInteractable->eaOverrides[i]->pcName, pcName) == 0) {
				if (pInteractable->pWorldEntry && pInteractable->pWorldEntry->full_interaction_properties) {
					interactable_InitBasePropertyEntry(pInteractable, pInteractable->pWorldEntry->full_interaction_properties, pInteractable->eaOverrides[i]->pcFilename, pInteractable->pcName, pInteractable->eaOverrides[i]->pEntry);
				}
			}
		}
	}
	FOR_EACH_END;
}


void interactable_ScanOverrideCounts(void)
{
	FOR_EACH_INTERACTABLE(pInteractable) {
		pInteractable->uOverrideLoadCount = eaSize(&pInteractable->eaOverrides);
	}
	FOR_EACH_END;
}

void interactable_GatherBeaconPositions(void)
{
	// Add all ambient job positions
	int j;
	for(j = 0; j < eaSize(&s_eaAmbientJobInteractables); j++) {
		int k;
		GameInteractable *pGameInteractable = s_eaAmbientJobInteractables[j];

		for(k = 0; k < eaSize(&pGameInteractable->eaInteractLocations); k++) {
			GameInteractLocation *pLoc = pGameInteractable->eaInteractLocations[k];
			if (REF_HANDLE_IS_ACTIVE(pLoc->pWorldInteractLocationProperties->hFsm) || eaSize(&pLoc->pWorldInteractLocationProperties->eaAnims) > 0) {
				beaconAddUsefulPoint(pLoc->pWorldInteractLocationProperties->vPos);
			}			
		}
	}

	// Add all combat job positions
	for(j = 0; j < eaSize(&s_eaCombatJobInteractables); j++) {
		int k;
		GameInteractable *pGameInteractable = s_eaCombatJobInteractables[j];

		for(k = 0; k < eaSize(&pGameInteractable->eaInteractLocations); k++) {
			GameInteractLocation *pLoc = pGameInteractable->eaInteractLocations[k];
			if (REF_HANDLE_IS_ACTIVE(pLoc->pWorldInteractLocationProperties->hSecondaryFsm)) {
				beaconAddUsefulPoint(pLoc->pWorldInteractLocationProperties->vPos);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Interactable Processing
// ----------------------------------------------------------------------------------

static void interactable_WakeRandomInteractableFromGroup(int iPartitionIdx, WorldLogicalGroup *pLogicalGroup)
{
	GameInteractable **eaInteractables = NULL;
	GameInteractable **eaSpawnedInteractables = NULL;
	GameInteractable *pInteractable;
	WorldInteractionPropertyEntry *pPropEntry;
	WorldInteractionEntry *pEntry;
	InteractTarget target = {0};
	F32 fLockoutRadius = pLogicalGroup->properties->interactableSpawnProperties.fLockoutRadius;
	int indexToWake;
	int i, j;

	// First, find ones that are sleeping (or awake if have lockout radius)
	for (i=eaSize(&pLogicalGroup->objects)-1; i>=0; --i){
		if (pLogicalGroup->objects[i]->type == WL_ENC_INTERACTABLE) {
			pEntry = ((WorldNamedInteractable*)pLogicalGroup->objects[i])->entry;
			pInteractable = interactable_GetByEntry(pEntry);
			if (pInteractable && GET_REF(pEntry->hInteractionNode)){
				GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
				if (pState->bSleeping) {
					eaPush(&eaInteractables, pInteractable);
				} else if (fLockoutRadius > 0) {
					eaPush(&eaSpawnedInteractables, pInteractable);
				}
			}
		}
	}

	// Remove sleeping ones that are within the lockout radius of spawned ones
	if (fLockoutRadius > 0) {
		F32 fRadiusSquared = fLockoutRadius * fLockoutRadius;
		Vec3 vTestPos, vSpawnedPos;

		for(i=eaSize(&eaInteractables)-1; i>=0; --i) {
			GameInteractable *pTestInteractable = eaInteractables[i];
			interactable_GetPosition(pTestInteractable, vTestPos);

			for(j=eaSize(&eaSpawnedInteractables)-1; j>=0; --j) {
				GameInteractable *pSpawnedInteractable = eaSpawnedInteractables[j];
				interactable_GetPosition(pSpawnedInteractable, vSpawnedPos);
				if (fRadiusSquared > distance3Squared(vTestPos, vSpawnedPos)) {
					break;
				}
			}
			if (j >= 0) {
				eaRemove(&eaInteractables, i);
			}
		}
	}
	
	if (eaSize(&eaInteractables)) {
		GameInteractablePartitionState *pState;

		// Select one at random 
		indexToWake = randomIntRange(0, eaSize(&eaInteractables) - 1);
		pInteractable = eaInteractables[indexToWake];
		pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

		// Wake it up
		pPropEntry = interactable_GetPropertyEntry(pInteractable, pState->iLastInteractIndex);
		COPY_HANDLE(target.hInteractionNode, pInteractable->pWorldEntry->hInteractionNode);
		target.iInteractionIndex = pState->iLastInteractIndex;
		pState->bSleeping = false;
		pState->iLastInteractIndex = 0;
		interaction_AddInteractedObject(iPartitionIdx, &target, 0, 0, 0.f, interaction_GetCooldownTime(pPropEntry), false, true, false);
		REMOVE_HANDLE(target.hInteractionNode);
	}

	// Clean up
	eaDestroy(&eaInteractables);
	eaDestroy(&eaSpawnedInteractables);
}


void interactable_Activate(Entity *pPlayerEnt, GameInteractable *pInteractable, int iIndex)
{
	if (pInteractable) {
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		pState->iLastInteractIndex = iIndex;
	}
}


void interactable_SetActive(int iPartitionIdx, GameInteractable *pInteractable, bool bActive)
{
	GameInteractablePartitionState *pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
	pState->bActive = bActive;
}


void interactable_SetNoLongerActive(int iPartitionIdx, WorldInteractionNode *pNode, int iIndex, EntityRef entRef)
{
	GameInteractable *pInteractable = interactable_GetByNode(pNode);
	WorldLogicalGroup *pLogicalGroup = NULL;
	WorldInteractionPropertyEntry *pEntry;
	Entity *pPlayerEnt = entFromEntityRef(iPartitionIdx, entRef);
	GameInteractablePartitionState *pState;

	if (!pInteractable) {
		return;
	}

	pState = interactable_GetPartitionState(iPartitionIdx, pInteractable);

	if (pInteractable->pWorldInteractable) {
		pLogicalGroup = pInteractable->pWorldInteractable->common_data.parent_group;
	}

	pEntry = interactable_GetPropertyEntry(pInteractable, iIndex);
	if (pEntry) {
		WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);

		// Run "no longer active" action (if any)
		if (pActionProps && pActionProps->pNoLongerActiveExpr) {
			interactable_EvaluateNonPlayerExpr(iPartitionIdx, pInteractable, pActionProps->pNoLongerActiveExpr);
		}

		// Hide the clickable (if desired)
		if (pTimeProps && pTimeProps->bHideDuringCooldown) {
			im_ForceEndMotionForNode(iPartitionIdx, pNode, false);
			interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_HIDE, 0, false);
		}
	}

	// Send the event for ending active state
	eventsend_RecordInteractableEndActive(iPartitionIdx, pPlayerEnt, pInteractable);

	// Clear the interaction tracking info
	pState->bActive = false;
	pState->iPlayerOwnerID = 0;
	StructDestroySafe(parse_DoorTarget, &pState->pLastDoorTarget);
	eaDestroyStruct(&pState->eaLastDoorVariables, parse_WorldVariable);

	// If this interactable is controlled by a logical group, it should go to sleep instead of entering Cooldown.
	if (pLogicalGroup && pLogicalGroup->properties && pLogicalGroup->properties->interactableSpawnProperties.eRandomType == LogicalGroupRandomType_Continuous){
		InteractTarget target = {0};
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, target.hInteractionNode);
		target.iInteractionIndex = iIndex;

		pState->bSleeping = true;
		interaction_RemoveInteractedObject(iPartitionIdx, &target);
		interactable_WakeRandomInteractableFromGroup(iPartitionIdx, pLogicalGroup);
		REMOVE_HANDLE(target.hInteractionNode);
	} else {
		pState->iLastInteractIndex = 0;
	}
}


void interactable_FinishCooldown( int iPartitionIdx, GameInteractable *pInteractable, WorldInteractionNode *pNode, int iIndex, Entity *pPlayerEnt )
{
	WorldInteractionPropertyEntry *pEntry;

	if (!pInteractable) {
		return;
	}

	pEntry = interactable_GetPropertyEntry(pInteractable, iIndex);
	if (pEntry) {
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
		// If was hidden during cooldown, unhide it
		if (pTimeProps && pTimeProps->bHideDuringCooldown) {
			GameInteractablePartitionState *pState = eaGet(&pInteractable->eaPartitionStates, iPartitionIdx);
			assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
			interactable_InitMotionNodes(pInteractable, pState, pEntry);
			interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);
		}

		if (pEntry->pcInteractionClass == pcPooled_TeamCorral)
		{
			TeamCorralInfo *pCorral = gslTeam_FindTeamCorralInfo(pPlayerEnt);
			
			if (pCorral)
			{
				gslTeam_DestroyTeamCorral(pCorral);
			}
		}
	}
}


// This causes only 1/6 of interactables to be processed each frame
#define INTERACTION_STEPS_PER_FRAME 6

void interactable_OncePerFrame(F32 fTimeStep)
{
	static int iForceChange = 0;
	static int iCurrentStep = 0;
	// Added an auto command that overrides the visibility of interactables for use in the world editor. The command is InteractableVisible 1
	if (g_InteractableVisible != g_InteractableVisibleOld) {
		g_InteractableVisibleOld = g_InteractableVisible;
		iForceChange = INTERACTION_STEPS_PER_FRAME;
	}
	// Check all interactables
	FOR_EACH_INTERACTABLE_STEP(pInteractable, iCurrentStep, INTERACTION_STEPS_PER_FRAME) {
		WorldInteractionProperties *pProperties;
		int i;
		int iPartitionIdx;
		
		// Skip interactables that are not properly defined
		if (!pInteractable->pWorldEntry) {
			continue;
		}

		// Skip interactables that have no visible or child expression and are not volume
		// triggered gates.  Then we don't need to iterate partitions
		if (!pInteractable->bHasVisibleExpression &&
			!pInteractable->bHasChildExpression &&
			!pInteractable->bIsVolumeTriggeredGate) {
				continue;
		}

		pProperties = pInteractable->pWorldEntry->full_interaction_properties;

		// Iterate partitions
		for(iPartitionIdx = eaSize(&pInteractable->eaPartitionStates)-1; iPartitionIdx >=0; --iPartitionIdx) {
			GameInteractablePartitionState *pState = pInteractable->eaPartitionStates[iPartitionIdx];

			// Skip empty partition
			if (!pState || mapState_IsMapPausedForPartition(iPartitionIdx)) {
				continue;
			}

			// Do visible expression (if any)
			if (pInteractable->bHasVisibleExpression && !pInteractable->bIsDestructible) {
				bool bVisible = true;
				bool bChangeVisibility = true;
				int iNumEntries;

				PERFINFO_AUTO_START("CheckVisibleExpression", 1);
				
				if (pInteractable->bVisiblePerEntity) {
					// Skip visibility check if using per-ent visibility
					bChangeVisibility = false;
				} else {
					// Normal visibility, so iterate properties

					// Loop over all property entries
					iNumEntries = interactable_GetNumPropertyEntries(pInteractable, true);
					for(i=0; i<iNumEntries && bVisible; ++i) {
						WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
						Expression *pVisibleExpr = interaction_GetVisibleExpr(pEntry);
						if (pVisibleExpr) {
							// All visible expressions must be true for the result to be true
							bVisible = bVisible && interactable_EvaluateNonPlayerExpr(iPartitionIdx, pInteractable, pVisibleExpr);
						}
					}
				}
				// Since there is at least one visible expression, apply results
				if (bVisible && bChangeVisibility) {
					interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);
				} else if (bChangeVisibility){
					interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_HIDE, 0, false);
				}

				PERFINFO_AUTO_STOP();
			}
			
			// Do child index expression (if any)
			if (pInteractable->bHasChildExpression && !pInteractable->bIsDestructible) {
				PERFINFO_AUTO_START("CheckChildExpression", 1);

				// Apply child select expression if present
				if (pProperties->pChildProperties && pProperties->pChildProperties->pChildSelectExpr) {
					int iChildIndex = interactable_EvaluateNonPlayerExpr(iPartitionIdx, pInteractable, pProperties->pChildProperties->pChildSelectExpr);
					interactable_SetVisibleChild(iPartitionIdx, pInteractable, iChildIndex, iForceChange>0); // Added an auto command that overrides the visibility of interactables for use in the world editor. The command is InteractableVisible 1
				}

				PERFINFO_AUTO_STOP();
			}
			
			// Do volume triggered gate (if any)
			if (pInteractable->bIsVolumeTriggeredGate) {
				PERFINFO_AUTO_START("CheckVolumeTriggeredGate", 1);
				
				interactable_UpdateVolumeTriggeredGateState(iPartitionIdx, pInteractable);

				PERFINFO_AUTO_STOP();
			}
		}
	} FOR_EACH_END;

	// Increment the step
	++iCurrentStep;
	if (iCurrentStep >= INTERACTION_STEPS_PER_FRAME) {
		iCurrentStep = 0;
	}
	if (iForceChange>0) {
		--iForceChange;
	}
}


// ----------------------------------------------------------------------------------
// Interactable Tracking Logic
// ----------------------------------------------------------------------------------

static void interactable_AddInteractable(const char *pcName, const char *pcGroupName, WorldNamedInteractable *pWorldInteractable)
{
	GameInteractable *pGameInteractable = calloc(1,sizeof(GameInteractable));
	WorldInteractionNode *pNode;
	const char *pchFilename = NULL;

	// Get the layer filename
	if (pWorldInteractable->common_data.layer)
	{
		pchFilename = layerGetFilename(pWorldInteractable->common_data.layer);
	}

	if (pcName) {
		pGameInteractable->pcName = allocAddString(pcName);
	}
	if (pcGroupName) {
		pGameInteractable->pcGroupName = allocAddString(pcGroupName);
	}

	pGameInteractable->pWorldInteractable = pWorldInteractable;
	pGameInteractable->pWorldEntry = pWorldInteractable->entry;

	pNode = GET_REF(pGameInteractable->pWorldEntry->hInteractionNode);
	if (pNode) {
		pGameInteractable->pcNodeName = wlInteractionNodeGetKey(pNode);
		pGameInteractable->uClassMask = wlInteractionNodeGetClass(pNode);
	}

	// store in the master list
	eaPush(&s_eaInteractables, pGameInteractable);
	devassert(stashAddressAddPointer(s_stInteractables,pGameInteractable->pWorldEntry,pGameInteractable,false));

	// store in the master octree
	if (pNode) {
		Vec3 vMidPos;

		pGameInteractable->pMainOctreeEntry = calloc(1, sizeof(OctreeEntry));

		wlInteractionNodeGetWorldMid(pNode, vMidPos);

		pGameInteractable->pMainOctreeEntry->node = pGameInteractable;
		copyVec3(vMidPos, pGameInteractable->pMainOctreeEntry->bounds.mid);
		pGameInteractable->pMainOctreeEntry->bounds.radius = wlInteractionNodeGetRadius(pNode);
		subVec3same(vMidPos, pGameInteractable->pMainOctreeEntry->bounds.radius, pGameInteractable->pMainOctreeEntry->bounds.min);
		addVec3same(vMidPos, pGameInteractable->pMainOctreeEntry->bounds.radius, pGameInteractable->pMainOctreeEntry->bounds.max);

		octreeAddEntry(s_pInteractableOctree, pGameInteractable->pMainOctreeEntry, OCT_ROUGH_GRANULARITY);
	}

	// add any interactable locations 
	if (pGameInteractable->pWorldEntry && pGameInteractable->pWorldEntry->full_interaction_properties) {
		WorldInteractionProperties *pInteractionProperties = pGameInteractable->pWorldEntry->full_interaction_properties;
		FOR_EACH_IN_EARRAY_FORWARDS(pInteractionProperties->peaInteractLocations, WorldInteractLocationProperties, pInteractLoc) {
			Vec3 vGroundPos;
			if (interactable_ValidateInteractLocation(pcName, worldGetAnyCollPartitionIdx(), pInteractLoc, vGroundPos, pchFilename)) {
				GameInteractLocation *pGameInteractLoc;

				pGameInteractLoc = calloc(1, sizeof(GameInteractLocation));
				// deep copy data
				pGameInteractLoc->pWorldInteractLocationProperties = StructClone(parse_WorldInteractLocationProperties, pInteractLoc);
				assert(pGameInteractLoc->pWorldInteractLocationProperties);
		
				copyVec3(vGroundPos, pGameInteractLoc->pWorldInteractLocationProperties->vPos);

				eaPush(&pGameInteractable->eaInteractLocations, pGameInteractLoc);
			}
		} FOR_EACH_END

	}

	// Check for Ambient and Combat Jobs
	{
		WorldAmbientJobInteractionProperties *pAmbientJobProperties;
		if(interactable_HasAmbientJobProperties(pWorldInteractable, &pAmbientJobProperties)) 
		{
			interactable_AddAmbientJobInteractable(pGameInteractable, pAmbientJobProperties);
			interactable_AddCombatJobInteractable(pGameInteractable, pAmbientJobProperties);
		}
	}

}


static void interactable_InitFakeForPartition(int iPartitionIdx, GameInteractable *pInteractable)
{
	GameInteractablePartitionState *pParentState = pInteractable->pParent->eaPartitionStates[iPartitionIdx];
	GameInteractablePartitionState *pState = calloc(1, sizeof(GameInteractablePartitionState));

	pState->iPartitionIdx = iPartitionIdx;
	pState->pInteractable = pInteractable;

	if (pParentState->bHidden || pParentState->bParentHidden || (pParentState->iChildIndex < 0) || (pInteractable->iChildIndexInParent < 0)) {
		pState->bParentHidden = pParentState->bHidden || pParentState->bParentHidden;
	} else {
		pState->bParentHidden = (pParentState->iChildIndex != pInteractable->iChildIndexInParent);
	}

	eaSet(&pInteractable->eaPartitionStates, pState, iPartitionIdx);
	interactable_UpdateMapState(pState, pInteractable);
}


static GameInteractable *interactable_AddFakeInteractable(int iPartitionIdx, GameInteractable *pParentInteractable, WorldInteractionEntry *pEntry)
{
	GameInteractable *pInteractable;
	GameInteractablePartitionState *pState;
	WorldInteractionNode *pNode;
	WorldCellEntryData *pData;

	// Create the interactable
	pInteractable = calloc(1,sizeof(GameInteractable));
	pInteractable->pWorldEntry = pEntry;
	pState  = calloc(1,sizeof(GameInteractablePartitionState));
	pState->iPartitionIdx = iPartitionIdx;
	pState->pInteractable = pInteractable;
	eaSet(&pInteractable->eaPartitionStates,pState,iPartitionIdx);

	// Set the node name
	pNode = GET_REF(pEntry->hInteractionNode);
	if (pNode) {
		pInteractable->pcNodeName = wlInteractionNodeGetKey(pNode);
	}

	// Not put into Octree since we never want to search for these

	// Add it to the system
	eaPush(&s_eaInteractables, pInteractable);
	devassert(stashAddressAddPointer(s_stInteractables, pEntry, pInteractable, false));

	// Init into fake state
	pState->bActive = false;
	pState->bSleeping = false;
	pState->bHidden = false;
	pState->iChildIndex = I_NO_CHILD_INDEX;
	pInteractable->bHasVisibleExpression = false;
	pInteractable->bIsVolumeTriggeredGate = false;
	pInteractable->bVisiblePerEntity = false;
	pInteractable->bHasChildExpression = false;
	pInteractable->bCanBeInteractedWith = false;

	// Add as a child of the parent
	pInteractable->pParent = pParentInteractable;
	pData = worldCellEntryGetData((WorldCellEntry*)pEntry);
	pInteractable->iChildIndexInParent = pData->interaction_child_idx;
	eaPushUnique(&pParentInteractable->eaChildren, pInteractable);

	// Init the partitions required for this
	partition_ExecuteOnEachPartitionWithData(interactable_InitFakeForPartition, pInteractable);

	return pInteractable;
}


static void interactable_ClearInteractableList(void)
{
	if (s_pInteractableOctree) {
		octreeDestroy(s_pInteractableOctree);
		s_pInteractableOctree = NULL;
	}

	if (s_AmbientJobOctree) {
		octreeDestroy(s_AmbientJobOctree);
		s_AmbientJobOctree = NULL;
	}

	if (s_CombatJobOctree) {
		octreeDestroy(s_CombatJobOctree);
		s_CombatJobOctree = NULL;
	}

	eaClear(&s_eaAmbientJobInteractables);

	eaClear(&s_eaCombatJobInteractables);

	eaDestroyEx(&s_eaInteractables, interactable_FreeGameInteractable);
	if ( s_stInteractables ) {
		stashTableDestroy(s_stInteractables);
		s_stInteractables = NULL;
	}
}


void interactable_RemoveInteractableOverridesFromMission(const char *pcMissionName)
{
	if (pcMissionName && pcMissionName[0]) {
		FOR_EACH_INTERACTABLE(pInteractable) {
			int j;
			for (j=eaSize(&pInteractable->eaOverrides)-1; j >= 0 ; j--) {
				if(stricmp(pInteractable->eaOverrides[j]->pcName, pcMissionName) == 0) {
					free(pInteractable->eaOverrides[j]);
					eaRemove(&pInteractable->eaOverrides, j);

					if(j < (int)pInteractable->uOverrideLoadCount)
						pInteractable->uOverrideLoadCount--;
				}
			}
		} FOR_EACH_END;
	}
}


bool interactable_ApplyInteractableOverride(const char *pcMissionName, const char *pcFilename, const char *pcInteractableName, const char *pcTagNamePooled, WorldInteractionPropertyEntry *pEntry)
{
	GameInteractableOverride *pOverride;

	if (pcInteractableName && pcInteractableName[0]) {
		GameInteractable *pInteractable;

		// Check that the interactable exists
		pInteractable = interactable_GetByName(pcInteractableName, NULL);
		if (!pInteractable) {
			// This may be a volume interact override.  Search the volumes
			return volume_ApplyInteractableOverride(pcMissionName, pcFilename, pcInteractableName, pEntry);
		}

		// Add the properties
		pOverride = calloc(1, sizeof(GameInteractableOverride));
		pOverride->pcFilename = allocAddFilename(pcFilename);
		pOverride->pcName = allocAddString(pcMissionName);
		pOverride->pEntry = StructClone(parse_WorldInteractionPropertyEntry, pEntry);
		eaPush(&pInteractable->eaOverrides, pOverride);

		return true;

	} else if (pcTagNamePooled && pcTagNamePooled[0]) {
		// Find interactables with this tag
		FOR_EACH_INTERACTABLE(pInteractable) {
			WorldInteractionProperties *pProps = SAFE_MEMBER2(pInteractable, pWorldEntry, full_interaction_properties);
			if (pProps && interaction_HasTag(pProps, pcTagNamePooled)) {
				pOverride = calloc(1, sizeof(GameInteractableOverride));
				pOverride->pcFilename = allocAddFilename(pcFilename);
				pOverride->pcName = allocAddString(pcMissionName);
				pOverride->pEntry = StructClone(parse_WorldInteractionPropertyEntry, pEntry);
				eaPush(&pInteractable->eaOverrides, pOverride);				
			}
		}
		FOR_EACH_END;
	}

	return false;
}


static void interactable_ValidatePropertyEntry(const char *pcFilename, const char *pcName, WorldScope *pScope, WorldInteractionPropertyEntry *pEntry)
{
	interaction_ValidatePropertyEntry(pEntry, pScope, pcFilename, "Interactable", pcName); 
}


static void interactable_ValidateEntry(GameInteractable *pInteractable)
{
	WorldInteractionProperties *pProps = pInteractable->pWorldEntry->full_interaction_properties;
	int i;

	if (!pInteractable->pWorldInteractable || !pProps) {
		// It's a fake interactable used for movement, so no validation required
		return;
	}

	// Validate entries
	for(i=eaSize(&pProps->eaEntries)-1; i>=0; --i) {
		interactable_ValidatePropertyEntry(layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), pInteractable->pcName, pInteractable->pWorldInteractable->common_data.closest_scope, pProps->eaEntries[i]);

		if (pInteractable->bIsDestructible && (pProps->eaEntries[i]->pcInteractionClass != pcPooled_Destructible)) {
			ErrorFilenamef(layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), 
							"Interactable %s is both a destructible and a %s.  It is illegal to have other types when it is also a destructible.", pInteractable->pcName, pProps->eaEntries[i]->pcInteractionClass);
		}
	}

	// Validate overrides
	for(i=eaSize(&pInteractable->eaOverrides)-1; i>=0; --i) {
		GameInteractableOverride *pOverride = pInteractable->eaOverrides[i];
		interactable_ValidatePropertyEntry(pOverride->pcFilename, pInteractable->pcName, NULL, pOverride->pEntry);

		if (pInteractable->bIsDestructible && (pOverride->pEntry->pcInteractionClass != pcPooled_Destructible)) {
			ErrorFilenamef(pOverride->pcFilename, 
							"Interactable %s is both a destructible and a %s due to an override.  It is illegal to have other types when it is also a destructible.", pInteractable->pcName, pOverride->pEntry->pcInteractionClass);
		}
	}

	// Validate that destructible isn't badly mixed with visible expressions
	if (pInteractable->bIsDestructible && pInteractable->bHasVisibleExpression) {
		ErrorFilenamef(layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), 
						"Interactable %s is a destructible, but someone set up the visible expression.  This is not a legal combination", pInteractable->pcName);
	}
	if (pInteractable->bIsDestructible && (pInteractable->bHasChildExpression || pInteractable->bHasChildIndex)) {
		ErrorFilenamef(layerGetFilename(pInteractable->pWorldInteractable->common_data.layer), 
						"Interactable %s is a destructible, but someone set up the child-select expression or number.  This is not a legal combination", pInteractable->pcName);
	}
}


// This is called when missions reload due to editing
void interactable_ValidateOverrides(void)
{
	// Clear all overrides
	FOR_EACH_INTERACTABLE(pInteractable) {
		eaDestroy(&pInteractable->eaOverrides);
	} FOR_EACH_END;

	// Clear all volume overrides
	volume_ClearVolumeOverrides();

	// Re-apply all overrides
	mission_ApplyAllInteractableOverrides();

	// Initialize data that is not partition specific
	FOR_EACH_INTERACTABLE(pInteractable) {
		interactable_InitBase(pInteractable);
	} FOR_EACH_END;

	// Re-generate volume expressions
	volume_GenerateOverrideExpressions();

	// Re-validate overrides
	FOR_EACH_INTERACTABLE(pInteractable) {
		interactable_ValidateEntry(pInteractable);
	} FOR_EACH_END;
}


void interactable_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	// Clear map state of any previous state on this partition
	mapState_ClearNodeEntries(iPartitionIdx);

	// Notify interaction system of partition reset
	interaction_PartitionLoad(iPartitionIdx);

	// Create this partition information for every interactable
	FOR_EACH_INTERACTABLE(pInteractable) {
		GameInteractablePartitionState *pState;
		if (!pInteractable) {
			continue;
		}

		// Destroy previous state (if any) and re-create
		pState = eaGet(&pInteractable->eaPartitionStates, iPartitionIdx);
		if (pState) {
			interactable_FreePartitionState(pState);
		}
		pState = calloc(1, sizeof(GameInteractablePartitionState));
		assert(pState);
		pState->iPartitionIdx = iPartitionIdx;
		pState->pInteractable = pInteractable;
		eaSet(&pInteractable->eaPartitionStates, pState, iPartitionIdx);

		FOR_EACH_IN_EARRAY(pInteractable->eaInteractLocations, GameInteractLocation, pInteractLocation) {
			GameInteractLocationPartition *pLocationPartition = eaGet(&pInteractLocation->eaPartitions, iPartitionIdx);
			if (pState) {
				interactable_FreeInteractLocationPartition(pLocationPartition);
			}
			pLocationPartition = callocStruct(GameInteractLocationPartition);

			eaSet(&pInteractLocation->eaPartitions, pLocationPartition, iPartitionIdx);
		} FOR_EACH_END

		// Init the partition entry basic information
		interactable_EarlyInitPartition(iPartitionIdx, pInteractable, pState);
	} FOR_EACH_END

	PERFINFO_AUTO_STOP();
}


void interactable_PartitionLoadLate(int iPartitionIdx)
{
	WorldZoneMapScope *pScope = zmapGetScope(NULL);

	PERFINFO_AUTO_START_FUNC();

	// Init the partition information for every interactable
	FOR_EACH_INTERACTABLE(pInteractable) {
		GameInteractablePartitionState *pState;
		if (!pInteractable) {
			continue;
		}

		pState = eaGet(&pInteractable->eaPartitionStates, iPartitionIdx);
		if (!pState) {
			continue;
		}

		// Init the partition entry second pass
		interactable_LateInitPartition(pInteractable, pState);
	} FOR_EACH_END

	// Randomize which interactables spawn based on logical groups
	if (pScope) {
		int i;
		for(i=eaSize(&pScope->groups)-1; i>=0; --i) {
			interactable_InitInteractablesFromLogicalGroup(iPartitionIdx, pScope->groups[i]);
		}
	}

	PERFINFO_AUTO_STOP();
}


void interactable_PartitionUnload(int iPartitionIdx)
{
	// Clean up data in map state
	mapState_ClearNodeEntries(iPartitionIdx);

	// Notify interaction of unload on this partition
	interaction_PartitionUnload(iPartitionIdx);

	// Clean up each interactable
	FOR_EACH_INTERACTABLE(pInteractable) {
		GameInteractablePartitionState *pState;
		if (!pInteractable) {
			continue;
		}

		pState = eaGet(&pInteractable->eaPartitionStates,iPartitionIdx);
		if (!pState) {
			continue;
		}

		interactable_FreePartitionState(pState);

		eaSet(&pInteractable->eaPartitionStates, NULL, iPartitionIdx);

		FOR_EACH_IN_EARRAY(pInteractable->eaInteractLocations, GameInteractLocation, pInteractLocation) {
			GameInteractLocationPartition *pLocationPartition;

			pLocationPartition = eaGet(&pInteractLocation->eaPartitions, iPartitionIdx);
			if(!pLocationPartition)
				continue;

			interactable_FreeInteractLocationPartition(pLocationPartition);
			eaSet(&pInteractLocation->eaPartitions, NULL, iPartitionIdx);
		} FOR_EACH_END
	} FOR_EACH_END;

	// remove timers in this partition
	interactionManager_PartitionUnload(iPartitionIdx);
}


void interactable_MapValidate(void)
{
	// Check that no two interactables have the same name on the map
	FOR_EACH_INTERACTABLE(pInteractable1) {
		FOR_EACH_INTERACTABLE2(pInteractable1, pInteractable2) {
			if (pInteractable1->pcName && pInteractable2->pcName && stricmp(pInteractable1->pcName, pInteractable2->pcName) == 0) {
				Errorf("Map has more than one interactable with name '%s'.  All interactables must have unique names.", pInteractable1->pcName);
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	// Validate each interactable
	FOR_EACH_INTERACTABLE(pInteractable) {
		if (pInteractable->pWorldEntry) {
			interactable_ValidateEntry(pInteractable);
		}
	} FOR_EACH_END;
}


void interactable_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i;

	// Clear all data
	interactable_MapUnload();

	// Create new data
	s_mapContainsPerEntVisExpressions = false;
	s_stInteractables = stashTableCreateAddress( 1000 );
	s_pInteractableOctree = octreeCreate();
	s_AmbientJobOctree = octreeCreate();
	s_CombatJobOctree = octreeCreate();

	// Get zone map scopes
	pScope = zmapGetScope(pZoneMap);

	// Find all interactables in all scopes
	if (pScope) {
		for(i=eaSize(&pScope->interactables)-1; i>=0; --i) {
			WorldNamedInteractable *pWorldInteractable = pScope->interactables[i];

			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pWorldInteractable->common_data);
			const char *pcGroupName = worldScopeGetObjectGroupName(&pScope->scope, &pWorldInteractable->common_data);
			interactable_AddInteractable(pcName, pcGroupName, pWorldInteractable);
		}
	}

	// Apply all mission overrides
	mission_ApplyAllInteractableOverrides();

	// Cache parent relationships
	FOR_EACH_INTERACTABLE(pInteractable) {
		interactable_InitParentage(pInteractable);
	} FOR_EACH_END;

	// Generate expressions and set all the non-partition state values
	FOR_EACH_INTERACTABLE(pInteractable) {
		interactable_InitBase(pInteractable);
	} FOR_EACH_END;
	volume_GenerateOverrideExpressions();

	// Reload any active partitions
	partition_ExecuteOnEachPartition(interactable_PartitionLoad);

	// Debug printing
	//printf("## MAP LOAD\n");
	//for(i=eaSize(&s_eaInteractables)-1; i>=0; --i) {
	//	GameInteractable *pInteractable = s_eaInteractables[i];
	//	if (pInteractable->pcName) {
	//		printf("## Interactable Name='%s' (%p)\n", pInteractable->pcName, pInteractable->pWorldEntry);
	//	} else {
	//		printf("## Unnamed Interactable (%p)\n", pInteractable->pWorldEntry);
	//	}
	//}
	s_bInteractablesLoaded = true;
}


void interactable_MapLoadLate(ZoneMap *pZoneMap)
{
	// Apply to any active partitions
	partition_ExecuteOnEachPartition(interactable_PartitionLoadLate);
}


void interactable_MapUnload(void)
{
	s_bInteractablesLoaded = false;
	s_bHasVolumeTriggeredGates = false;

	interaction_ClearInteractedList();
	interactable_ClearInteractableList();
	interactable_ClearGlobalInteractableInfo();
	interactable_ClearAllPlayerInteractableTrackingData();
	mapState_ClearAllNodeEntries();
}


void interactable_ResetInteractables(void)
{
	interactable_MapLoad(worldGetActiveMap());
}


bool interactable_AreInteractablesLoaded(void)
{
	return s_bInteractablesLoaded;
}

bool interactable_HasVolumeTriggeredGates(void)
{
	return s_bHasVolumeTriggeredGates;
}


// ----------------------------------------------------------------------------------
// System Initialization
// ----------------------------------------------------------------------------------

AUTO_RUN;
void interaction_InitSystem(void)
{
	// Register for game interaction callbacks
	wlInteractionRegisterGameCallbacks(interactable_IsSelectableCallback);
	wlInteractionRegisterCallbacks(interactable_IsNodeHiddenCB, interactable_IsNodeDisabledCB);

	// Register for the check if the geometry can be hidden
	worldLibSetIsConsumableFunc(interactable_IsHideableEntry);
	worldLibSetIsTraversableFunc(interactable_IsTraversableEntry);
	worldLibSetInteractionNodeFreeFunc(im_HandleNodeDestroy);

	// Register the beacon callback
	beaconPathSetDoorCallback(interactable_FindClosestDoor);

	// Initialize the expression var name
	g_InteractableExprVarName = allocAddString("GameInteractable");
}


#include "gslInteractable_h_ast.c"
