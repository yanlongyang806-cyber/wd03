/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityITerator.h"
#include "error.h"
#include "Expression.h"
#include "gslEncounter.h"
#include "gslPartition.h"
#include "gslTriggerCondition.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"

#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Three times per second currently, maybe change this depending on performance
#define TRIGGERCOND_UPDATE_TICK 5

static U32 s_TriggerCondTick = 0;


static GameTriggerCondition **s_eaTriggerConditions = NULL;

static ExprContext *s_pTriggerConditionContext = NULL;


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaTriggerConditions.  No other
// function than triggercondition_GetByName and triggercondition_GetByEntry should
// be searching s_eaTriggerConditions.
// ----------------------------------------------------------------------------------
GameTriggerCondition *triggercondition_GetByName(const char *pcTriggerConditionName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcTriggerConditionName);

		if (pObject && pObject->type == WL_ENC_TRIGGER_CONDITION) {
			WorldTriggerCondition *pNamedTriggerCondition = (WorldTriggerCondition *)pObject;
			GameTriggerCondition *pGameTriggerCondition = triggercondition_GetByEntry(pNamedTriggerCondition);
			if (pGameTriggerCondition) {
				return pGameTriggerCondition;
			}
		}
	} else {
		int i;
		for(i=eaSize(&s_eaTriggerConditions)-1; i>=0; --i) {
			if (stricmp(pcTriggerConditionName, s_eaTriggerConditions[i]->pcName) == 0) {
				return s_eaTriggerConditions[i];
			}
		}
	}
	
	return NULL;
}


GameTriggerCondition *triggercondition_GetByEntry(WorldTriggerCondition *pTriggerCondition)
{
	int i;

	for(i=eaSize(&s_eaTriggerConditions)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaTriggerConditions[i], pWorldTriggerCondition) == pTriggerCondition) {
			return s_eaTriggerConditions[i];
		}
	}
	
	return NULL;
}

#define FOR_EACH_TRIGGER_CONDITION(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaTriggerConditions)-1; i##it##Index>=0; --i##it##Index) { GameTriggerCondition *it = s_eaTriggerConditions[i##it##Index];
#define FOR_EACH_TRIGGER_CONDITION2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GameTriggerCondition *it = s_eaTriggerConditions[i##it##Index];
// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaTriggerConditions.
// ----------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------
// Post Processing Logic
// ----------------------------------------------------------------------------------


static void triggercondition_PostProcess(GameTriggerCondition *pGameTriggerCondition)
{
	WorldTriggerConditionProperties *pProperties = SAFE_MEMBER(pGameTriggerCondition->pWorldTriggerCondition, properties);
	WorldScope *pScope = SAFE_MEMBER(pGameTriggerCondition->pWorldTriggerCondition, common_data.closest_scope);
	
	exprContextSetScope(s_pTriggerConditionContext, pScope);

	// Compile expression if necessary
	if (pProperties && pProperties->cond) {
		// Compile expression
		exprGenerate(pProperties->cond, s_pTriggerConditionContext);
	}

	//// Debug printing
	//if (pGameTriggerCondition->pcName) {
	//	printf("## Name='%s'\n", pTriggerCondition->pcName);
	//} else {
	//	printf("## Unnamed Trigger Condition\n");
	//}
}


static void triggercondition_PostProcessAll(void)
{
	// Post-process all trigger conditions
	FOR_EACH_TRIGGER_CONDITION(pTriggerCond) {
		triggercondition_PostProcess(pTriggerCond);
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Trigger Condition List Logic
// ----------------------------------------------------------------------------------


static void triggercondition_Free(GameTriggerCondition *pGameTriggerCondition)
{
	eaDestroyEx(&pGameTriggerCondition->eaPartitionStates, NULL);
	free(pGameTriggerCondition);
}


static void triggercondition_AddWorldTriggerCondition(const char *pcName, WorldTriggerCondition *pWorldTriggerCondition)
{
	GameTriggerCondition *pGameTriggerCondition = calloc(1,sizeof(GameTriggerCondition));
	if (pcName) {
		pGameTriggerCondition->pcName = allocAddString(pcName);
	}
	pGameTriggerCondition->pWorldTriggerCondition = pWorldTriggerCondition;
	eaPush(&s_eaTriggerConditions, pGameTriggerCondition);
}


static void triggercondition_ClearList(void)
{
	eaDestroyEx(&s_eaTriggerConditions, triggercondition_Free);
}


// Check if a trigger condition exists
bool triggercondition_TriggerConditionExists(const char *pcName, const WorldScope *pScope)
{
	return triggercondition_GetByName(pcName, pScope) != NULL;
}


// Get the state of a trigger condition
int triggercondition_GetState(int iPartitionIdx, GameTriggerCondition *pTriggerCondition)
{
	TriggerConditionPartitionState *pState = eaGet(&pTriggerCondition->eaPartitionStates, iPartitionIdx);
	assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
	return pState->state;
}


// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------


void triggercondition_MapValidate(ZoneMap *pZoneMap)
{
	// Check that no two trigger conditions have the same name on the map
	FOR_EACH_TRIGGER_CONDITION(pTriggerCondition1) {
		FOR_EACH_TRIGGER_CONDITION2(pTriggerCondition1, pTriggerCondition2) {
			if (pTriggerCondition1->pcName && pTriggerCondition2->pcName && stricmp(pTriggerCondition1->pcName, pTriggerCondition2->pcName) == 0) {
				ErrorFilenamef(layerGetFilename(pTriggerCondition1->pWorldTriggerCondition->common_data.layer),
							   "Map has two trigger conditions with name '%s'.  All trigger conditions must have unique names.", pTriggerCondition1->pcName);
			}
		} FOR_EACH_END;
	} FOR_EACH_END;
}


// When a player first enters a map, tell them which trigger conditions are "on"
// This assumes that all FX created by trigger conditions start "off".  This is true initially, but if a client
// enters a map twice without clearing its FX in between, it could be bad
void triggercondition_InitClientTriggerConditions(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	int i;
	int n;

	n = eaSize(&s_eaTriggerConditions);
	for(i=0; i<n; i++) {
		GameTriggerCondition *pTriggerCond = s_eaTriggerConditions[i];
		TriggerConditionPartitionState *pState = eaGet(&pTriggerCond->eaPartitionStates, iPartitionIdx);
		if (pState && pState->state != 0) {
			ClientCmd_UpdateTriggerCondFx(pPlayerEnt, pTriggerCond->pcName, pState->state);
		}
	}
}


void triggercondition_ResetClientTriggerConditions(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	int i;
	int n;

	n = eaSize(&s_eaTriggerConditions);
	for(i=0; i<n; i++) {
		GameTriggerCondition *pTriggerCond = s_eaTriggerConditions[i];
		TriggerConditionPartitionState *pState = eaGet(&pTriggerCond->eaPartitionStates, iPartitionIdx);
		if (pState && pState->state != 0) {
			pState->state = 0;
			ClientCmd_UpdateTriggerCondFx(pPlayerEnt, pTriggerCond->pcName, pState->state);
		}
	}
}

static int triggercondition_EvaluateTriggerCondition(const GameTriggerCondition *pTriggerCond, int iPartitionIdx)
{
	if(pTriggerCond->pWorldTriggerCondition->properties->cond)
	{
		MultiVal ret;

		exprContextClear(s_pTriggerConditionContext);
		exprContextSetScope(s_pTriggerConditionContext, pTriggerCond->pWorldTriggerCondition->common_data.closest_scope);
		exprContextSetPartition(s_pTriggerConditionContext, iPartitionIdx);
		exprEvaluate(pTriggerCond->pWorldTriggerCondition->properties->cond, s_pTriggerConditionContext, &ret);

		return MultiValGetInt(&ret, false);
	}

	return 1;
}

void triggercondition_UpdateTriggerConditions(void)
{
	if(!g_EncounterProcessing)
		return;

	// Only trigger once every TRIGGERCOND_UPDATE_TICK frames
	if(s_TriggerCondTick++ % TRIGGERCOND_UPDATE_TICK)
		return;

	// Search all conditions in all layers
	FOR_EACH_TRIGGER_CONDITION(pTriggerCond)
	{
		int iPartitionIdx;
		for(iPartitionIdx = 0; iPartitionIdx < eaSize(&pTriggerCond->eaPartitionStates); ++iPartitionIdx)
		{
			TriggerConditionPartitionState *pState = pTriggerCond->eaPartitionStates[iPartitionIdx];
			if(pState)
			{
				int state = triggercondition_EvaluateTriggerCondition(pTriggerCond, iPartitionIdx);
				if(pState->bStateNeverUpdated || state != pState->state)
				{
					// Send an update to each player in this partition. Probably inefficient
					Entity *currEnt;
					EntityIterator *iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
					while(currEnt = EntityIteratorGetNext(iter))
						ClientCmd_UpdateTriggerCondFx(currEnt, pTriggerCond->pcName, state);
					EntityIteratorRelease(iter);

					pState->state = state;
					pState->bStateNeverUpdated = false;
				}
			}
		}
	} FOR_EACH_END;
}


void triggercondition_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	// Create the state for the partition on all trigger conditions if it doesn't exist, else reset it
	FOR_EACH_TRIGGER_CONDITION(pTriggerCondition) {
		TriggerConditionPartitionState *pState = eaGet(&pTriggerCondition->eaPartitionStates, iPartitionIdx);
		if(!pState)
		{
			pState = calloc(1,sizeof(TriggerConditionPartitionState));
			eaSet(&pTriggerCondition->eaPartitionStates, pState, iPartitionIdx);
		}
		pState->bStateNeverUpdated = true;
		pState->state = 0; // we still assume 0 if anyone queries the value while still not updated
	} FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}


void triggercondition_PartitionUnload(int iPartitionIdx)
{
	// Destroy the state for the partition on all trigger conditions
	FOR_EACH_TRIGGER_CONDITION(pTriggerCondition)
	{
		TriggerConditionPartitionState *pState = eaGet(&pTriggerCondition->eaPartitionStates, iPartitionIdx);
		if(pState)
		{
			free(pState);
			eaSet(&pTriggerCondition->eaPartitionStates, NULL, iPartitionIdx);
		}
	} FOR_EACH_END;
}


void triggercondition_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;

	// Clear all data
	triggercondition_ClearList();

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	// Find all trigger conditions in all scopes
	if(pScope)
	{
		int i;
		for(i = eaSize(&pScope->trigger_conditions) - 1; i >= 0; --i)
		{
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->trigger_conditions[i]->common_data);
			triggercondition_AddWorldTriggerCondition(pcName, pScope->trigger_conditions[i]);
		}
	}

	// Post-process after load
	triggercondition_PostProcessAll();

	// Reinitialize any open partitions
	partition_ExecuteOnEachPartition(triggercondition_PartitionLoad);
}


void triggercondition_MapUnload(void)
{
	triggercondition_ClearList();
}


ExprFuncTable* triggercondition_CreateExprFuncTable()
{
	static ExprFuncTable* s_triggerFuncTable = NULL;
	if(!s_triggerFuncTable)
	{
		s_triggerFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_triggerFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_triggerFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_triggerFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_triggerFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_triggerFuncTable, "util");
	}
	return s_triggerFuncTable;
}


AUTO_RUN;
void triggercondition_InitSystem(void)
{
	s_pTriggerConditionContext = exprContextCreate();
	exprContextSetFuncTable(s_pTriggerConditionContext, triggercondition_CreateExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pTriggerConditionContext);
}
