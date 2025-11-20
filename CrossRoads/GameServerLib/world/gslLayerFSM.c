/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiLib.h"
#include "EntityIterator.h"
#include "error.h"
#include "Expression.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslLayerFSM.h"
#include "gslPartition.h"
#include "gslWorldVariable.h"
#include "mapstate_common.h"
#include "NameList.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"

#include "gslLayerFSM_h_ast.h"
#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Definitions
// ----------------------------------------------------------------------------------

static MultiVal *layerfsm_FSMExternVarLookup(ExprContext *pContext, const char *pcVarName);


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

static U32 s_LayerFSMTick = 0;

static GameLayerFSM **s_eaLayerFSMs = NULL;

static ExprContext *s_pLayerFSMStaticCheckContext = NULL;

static ExprFuncTable* s_layerFSMFuncTable = NULL;


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaLayerFSMs.  No other
// function than layerfsm_GetByName and layerfsm_GetByEntry should
// be searching s_eaLayerFSM.
// ----------------------------------------------------------------------------------
GameLayerFSM *layerfsm_GetByName(const char *pcName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcName);

		if (pObject && pObject->type == WL_ENC_LAYER_FSM) {
			WorldLayerFSM *pNamedLayerFSM = (WorldLayerFSM *)pObject;
			GameLayerFSM *pGameLayerFSM = layerfsm_GetByEntry(pNamedLayerFSM);
			if (pGameLayerFSM) {
				return pGameLayerFSM;
			}
		}
	} else {
		int i;
		for(i=eaSize(&s_eaLayerFSMs)-1; i>=0; --i) {
			if (stricmp(pcName, s_eaLayerFSMs[i]->pcName) == 0) {
				return s_eaLayerFSMs[i];
			}
		}
	}
	
	return NULL;
}


GameLayerFSM *layerfsm_GetByEntry(WorldLayerFSM *pLayerFSM)
{
	int i;

	for(i=eaSize(&s_eaLayerFSMs)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaLayerFSMs[i], pWorldLayerFSM) == pLayerFSM) {
			return s_eaLayerFSMs[i];
		}
	}
	
	return NULL;
}


static void** layerfsm_GetFSMArray(void)
{
	return s_eaLayerFSMs;
}


NameList* layerfsm_GetNameList(void)
{
	static NameList *namelist = NULL;

	if (!namelist) {
		namelist = CreateNameList_StructArray(parse_GameLayerFSM, ".Name", layerfsm_GetFSMArray);
	}

	return namelist;
}


#define FOR_EACH_LAYER_FSM_STRIDE(it,stride) {							\
	int i##it##Index;													\
	int numFSMs##it = eaSize(&s_eaLayerFSMs);							\
	if (s_eaLayerFSMs)																\
		for(i##it##Index=s_LayerFSMTick%(stride); i##it##Index<numFSMs##it; i##it##Index+=stride) {	\
			GameLayerFSM *it = s_eaLayerFSMs[i##it##Index];
#define FOR_EACH_LAYER_FSM(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaLayerFSMs)-1; i##it##Index>=0; --i##it##Index) { GameLayerFSM *it = s_eaLayerFSMs[i##it##Index];
#define FOR_EACH_LAYER_FSM2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GameLayerFSM *it = s_eaLayerFSMs[i##it##Index];
// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaLayerFSMs.
// ----------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------
// LayerFSM List Logic
// ----------------------------------------------------------------------------------

void layerfsm_GetByFSMName(SA_PARAM_NN_STR const char *pcName, SA_PARAM_OP_VALID const WorldScope *pScope, SA_PARAM_NN_VALID GameLayerFSM ***peaFSMListResult)
{
	assert(eaSize(peaFSMListResult) == 0);
	
	FOR_EACH_LAYER_FSM(pLayerFSM) {
		FSM *pFSM = GET_REF(pLayerFSM->pWorldLayerFSM->properties->hFSM);
		if (pFSM && (0 == stricmp(pcName, pFSM->name))) {
			eaPush(peaFSMListResult, pLayerFSM);
		}
	} FOR_EACH_END;
}


// Check if a layer fsm exists
bool layerfsm_LayerFSMExists(const char *pcName, const WorldScope *pScope)
{
	return layerfsm_GetByName(pcName, pScope) != NULL;
}


static const char* layerfsm_Filename(GameLayerFSM *pLayerFSM)
{
	return layerGetFilename(pLayerFSM->pWorldLayerFSM->common_data.layer);
}


FSMContext *layerfsm_GetFSMContext(GameLayerFSM *pLayerFSM, int iPartitionIdx)
{
	LayerFSMPartitionState *pState = eaGet(&pLayerFSM->eaPartitionStates, iPartitionIdx);
	assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
	return pState->pFSMContext;
}

static void layerfsm_PartitionFree(LayerFSMPartitionState *pState)
{
	if (pState->pFSMContext) {
		aiMessageDestroyAll(pState->pFSMContext);
		fsmContextDestroy(pState->pFSMContext);
	}
	if(pState->pExprContext) {
		exprContextDestroy(pState->pExprContext);
	}
	free(pState);
}


static void layerfsm_Free(GameLayerFSM *pGameLayerFSM)
{
	eaDestroyEx(&pGameLayerFSM->eaPartitionStates, layerfsm_PartitionFree);

	free(pGameLayerFSM);
}

static ExprContext* layerfsm_CreateExprContext(GameLayerFSM *pGameLayerFSM)
{
	// Set up expression context shared by all partitions on this FSM
	ExprContext *pExprContext = exprContextCreate();
	exprContextSetFuncTable(pExprContext, s_layerFSMFuncTable);
	exprContextSetPointerVar(pExprContext, "Context", pExprContext, parse_ExprContext, false, true);
	exprContextSetPointerVar(pExprContext, "curStateTracker", NULL, parse_FSMStateTrackerEntry, false, true);
	exprContextAddExternVarCategory(pExprContext, "layer", fsmRegisterExternVar, layerfsm_FSMExternVarLookup, fsmRegisterExternVarSCType);
	exprContextSetPointerVar(pExprContext, "INTERNAL_LayerFSM", pGameLayerFSM->pWorldLayerFSM, parse_WorldLayerFSM, false, true);
	exprContextSetScope(pExprContext, pGameLayerFSM->pWorldLayerFSM->common_data.closest_scope);
	exprContextSetAllowRuntimePartition(pExprContext);

	return pExprContext;
}

static void layerfsm_AddWorldLayerFSM(const char *pcName, WorldLayerFSM *pWorldLayerFSM)
{
	GameLayerFSM *pGameLayerFSM = calloc(1,sizeof(GameLayerFSM));
	if (pcName) {
		pGameLayerFSM->pcName = allocAddString(pcName);
	}
	pGameLayerFSM->pWorldLayerFSM = pWorldLayerFSM;

	eaPush(&s_eaLayerFSMs, pGameLayerFSM);
}

static void layerfsm_ClearList(void)
{
	eaDestroyEx(&s_eaLayerFSMs, layerfsm_Free);
}


// ----------------------------------------------------------------------------------
// Layer FSM variable lookup
// ----------------------------------------------------------------------------------

// return list of all vars
WorldVariable **layerfsm_GetAllVars(GameLayerFSM *pFSM)
{
	WorldLayerFSMProperties *pProps = pFSM->pWorldLayerFSM->properties;
	int i;
	WorldVariable** pWorldVars = NULL;
	

	for(i=eaSize(&pProps->fsmVars)-1; i>=0; --i) {
		WorldVariable *pWorldVar = pProps->fsmVars[i];
		if (pWorldVar->pcName) {
			eaPush(&pWorldVars, pWorldVar);
		}
	}
	return pWorldVars;
}

WorldVariable *layerfsm_LookupVar(GameLayerFSM *pFSM, const char *pcVarName)
{
	WorldLayerFSMProperties *pProps = pFSM->pWorldLayerFSM->properties;
	int i;

	for(i=eaSize(&pProps->fsmVars)-1; i>=0; --i) {
		WorldVariable *pWorldVar = pProps->fsmVars[i];
		if (pcVarName && pWorldVar->pcName && (0 == stricmp(pcVarName, pWorldVar->pcName))) {
			return pWorldVar;
		}
	}
	return NULL;
}


static MultiVal *layerfsm_FSMExternVarLookup(ExprContext *pContext, const char* varName)
{
	WorldLayerFSM *pWorldLayerFSM = exprContextGetVarPointerUnsafe(pContext, "INTERNAL_LayerFSM");
	GameLayerFSM *pLayerFSM = layerfsm_GetByEntry(pWorldLayerFSM);

	if (pLayerFSM) {
		WorldVariable *pWorldVar = layerfsm_LookupVar(pLayerFSM, varName);
		if (pWorldVar) {
			MultiVal *pMultival = exprContextAllocScratchMemory(pContext, sizeof(*pMultival));
			worldVariableToMultival(pContext, pWorldVar, pMultival);
			return pMultival;
		}
	}

	{
		MultiVal *pMultival = exprContextAllocScratchMemory(pContext, sizeof(*pMultival));
		MultiValSetStringForExpr(pContext, pMultival, "");
		return pMultival;
	}
}


// ----------------------------------------------------------------------------------
// Layer FSM Utilities
// ----------------------------------------------------------------------------------

void layerfsm_OncePerFrame(void)
{
	if (!g_EncounterProcessing) {
		return;
	}
	
	// Only update some of the layer fsms each frame
	FOR_EACH_LAYER_FSM_STRIDE(pLayerFSM, LAYERFSM_UPDATE_TICK) {
		int iPartitionIdx;
		// Update all partitions for the chosen FSM
		for(iPartitionIdx=eaSize(&pLayerFSM->eaPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
			LayerFSMPartitionState *pState = pLayerFSM->eaPartitionStates[iPartitionIdx];

			if (!pState || !pState->pFSMContext || mapState_IsMapPausedForPartition(iPartitionIdx)) {
				continue;
			}
		
			PERFINFO_AUTO_START("layer fsm fsmExecute", 1);

			exprContextSetPartition(pState->pExprContext, iPartitionIdx);

			if (fsmExecute(pState->pFSMContext, pState->pExprContext)) {
				eventsend_EncLayerFSMStateChange(iPartitionIdx, GET_REF(pLayerFSM->pWorldLayerFSM->properties->hFSM),
												(char*)fsmGetState(pState->pFSMContext));
			}
			PERFINFO_AUTO_STOP();
		}
	} FOR_EACH_END;

	// This number is used by the STRIDE macro above
	++s_LayerFSMTick;
}


// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------

void layerfsm_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	// Create the state for the partition on all layer FSMs if it doesn't exist, else reset it
	FOR_EACH_LAYER_FSM(pLayerFSM) {
		LayerFSMPartitionState *pState = eaGet(&pLayerFSM->eaPartitionStates, iPartitionIdx);
		FSM *pFSM;

		// Create state if it doesn't exist
		if (!pState) {
			pState = calloc(1,sizeof(LayerFSMPartitionState));
			eaSet(&pLayerFSM->eaPartitionStates, pState, iPartitionIdx);
		}
		
		// Check to make sure there's a valid FSM with either no group or the encounter layer group
		pFSM = GET_REF(pLayerFSM->pWorldLayerFSM->properties->hFSM);
		if (pFSM && (!pFSM->group || strStartsWith(pFSM->group, "Encounterlayerfsms"))) {
			// Setup context
			pState->pFSMContext = fsmContextCreate(pFSM);
			pState->pFSMContext->messages = stashTableCreateWithStringKeys(4, StashDefault);

			pState->pExprContext = layerfsm_CreateExprContext(pLayerFSM);
		} else {
			// Clean up any pre-existing context
			if (pState->pFSMContext) {
				fsmContextDestroy(pState->pFSMContext);
				pState->pFSMContext = NULL;
			}
			if (pState->pExprContext) {
				exprContextDestroy(pState->pExprContext);
				pState->pExprContext = NULL;
			}
		}
	} FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}


void layerfsm_PartitionUnload(int iPartitionIdx)
{
	// Destroy the state for the partition on all layer FSMs
	FOR_EACH_LAYER_FSM(pLayerFSM) {
		LayerFSMPartitionState *pState = eaGet(&pLayerFSM->eaPartitionStates, iPartitionIdx);
		if (pState) {
			// Clean up state
			layerfsm_PartitionFree(pState);
			eaSet(&pLayerFSM->eaPartitionStates, NULL, iPartitionIdx);
		}
	} FOR_EACH_END;
}


void layerfsm_MapValidate(ZoneMap *pZoneMap, bool bIsMapEdit)
{
	// Check that no two layer fsms have the same name on the map
	FOR_EACH_LAYER_FSM(pLayerFSM1) {
		FOR_EACH_LAYER_FSM2(pLayerFSM1, pLayerFSM2) {
			if (stricmp(pLayerFSM1->pcName, pLayerFSM2->pcName) == 0) {
				ErrorFilenamef(layerfsm_Filename(pLayerFSM1), "Map has two layer fsms with name '%s'.  All layer fsms must have unique names.", pLayerFSM1->pcName);
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	// Check that every layer fsm has a valid FSM
	FOR_EACH_LAYER_FSM(pLayerFSM) {
		FSM *pFSM = GET_REF(pLayerFSM->pWorldLayerFSM->properties->hFSM);

		if (!pFSM) {
			if(!bIsMapEdit) {
				ErrorFilenameGroupf(layerfsm_Filename(pLayerFSM), "Design", 2, "LayerFSM %s does not exist",
									REF_STRING_FROM_HANDLE(pLayerFSM->pWorldLayerFSM->properties->hFSM));
			}
		} else if (pFSM->group && !strStartsWith(pFSM->group, "Encounterlayerfsms")) {
			ErrorFilenameGroupf(layerfsm_Filename(pLayerFSM), "Design", 2, "Trying to use critter FSM %s as a Layer FSM",
								REF_STRING_FROM_HANDLE(pLayerFSM->pWorldLayerFSM->properties->hFSM));
		}
	} FOR_EACH_END;

	// Perform static checking on each fsm variable
	FOR_EACH_LAYER_FSM(pLayerFSM) {
		FSM *pFSM = GET_REF(pLayerFSM->pWorldLayerFSM->properties->hFSM);
		int i;

		if (pFSM) {
			// set up the self pointer
			exprContextSetPointerVar(s_pLayerFSMStaticCheckContext, "INTERNAL_LayerFSM", pLayerFSM->pWorldLayerFSM, parse_WorldLayerFSM, false, true);

			for (i=eaSize(&pLayerFSM->pWorldLayerFSM->properties->fsmVars)-1; i>=0; --i) {
				WorldVariable *pWorldVar = pLayerFSM->pWorldLayerFSM->properties->fsmVars[i];
				FSMExternVar *pExternVar = (pWorldVar ? fsmExternVarFromName(pFSM, pWorldVar->pcName, "layer") : NULL);

				if (pExternVar && pExternVar->scType) {
					MultiVal multival = {0};
					worldVariableToMultival(NULL, pWorldVar, &multival);
					exprStaticCheckWithType(s_pLayerFSMStaticCheckContext, &multival, pExternVar->scType, pExternVar->scTypeCategory, layerfsm_Filename(pLayerFSM));
					MultiValClear(&multival);
				}
			}
		}
	} FOR_EACH_END;
}


void layerfsm_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i;

	// Clear all data
	layerfsm_ClearList();

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	// Find all layer fsms in all scopes
	if(pScope) {
		for(i=eaSize(&pScope->layer_fsms)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->layer_fsms[i]->common_data);
			layerfsm_AddWorldLayerFSM(pcName, pScope->layer_fsms[i]);
		}
	}

	// Reinitialize any open partitions
	partition_ExecuteOnEachPartition(layerfsm_PartitionLoad);
}


void layerfsm_MapUnload(void)
{
	layerfsm_ClearList();
}



AUTO_STARTUP(LayerFSMInit) ASTRT_DEPS(Expression, Powers, Critters, AS_Messages, AI, Cutscenes);
void layerfsm_InitSystem(void)
{
	// Set up expression context function table for layer FSMs
	s_layerFSMFuncTable = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(s_layerFSMFuncTable, "util");
	exprContextAddFuncsToTableByTag(s_layerFSMFuncTable, "layerFSM");
	exprContextAddFuncsToTableByTag(s_layerFSMFuncTable, "gameutil");
	exprContextAddFuncsToTableByTag(s_layerFSMFuncTable, "entityutil");
	exprContextAddFuncsToTableByTag(s_layerFSMFuncTable, "encounter_action");
	
	// Set up FSM context for static checking use
	s_pLayerFSMStaticCheckContext = exprContextCreate();
	exprContextSetFuncTable(s_pLayerFSMStaticCheckContext, s_layerFSMFuncTable);
	exprContextSetPointerVar(s_pLayerFSMStaticCheckContext, "Context", s_pLayerFSMStaticCheckContext, parse_ExprContext, false, true);
	exprContextSetPointerVar(s_pLayerFSMStaticCheckContext, "curStateTracker", NULL, parse_FSMStateTrackerEntry, false, true);
	exprContextAddExternVarCategory(s_pLayerFSMStaticCheckContext, "layer", fsmRegisterExternVar, layerfsm_FSMExternVarLookup, fsmRegisterExternVarSCType);
	exprContextSetAllowRuntimePartition(s_pLayerFSMStaticCheckContext);

	fsmLoad("EncounterLayerFSMs", 
			"ai/EncLayer", 
			"EncLayerFSMs.bin", 
			s_pLayerFSMStaticCheckContext,
			s_layerFSMFuncTable,
			s_layerFSMFuncTable,
			s_layerFSMFuncTable,
			s_layerFSMFuncTable,
			s_layerFSMFuncTable);
}


#include "gslLayerFSM_h_ast.c"
