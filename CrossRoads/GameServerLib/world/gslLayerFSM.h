/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct CritterVar CritterVar;
typedef struct Entity Entity;
typedef struct ExprContext ExprContext;
typedef struct FSMContext FSMContext;
typedef struct NameList NameList;
typedef struct WorldLayerFSM WorldLayerFSM;
typedef struct WorldScope WorldScope;
typedef struct WorldVariable WorldVariable;
typedef struct ZoneMap ZoneMap;

#include "stdtypes.h"

#define LAYERFSM_UPDATE_TICK 15


typedef struct LayerFSMPartitionState
{
	FSMContext *pFSMContext;
	ExprContext *pExprContext;
} LayerFSMPartitionState;


// Autostruct only for namelist.  All fields other than name should be NO_AST
AUTO_STRUCT;
typedef struct GameLayerFSM
{
	// The spawn point's map-level name
	const char *pcName;

	// The world fsm
	WorldLayerFSM *pWorldLayerFSM;		NO_AST

	// Per-partition data
	LayerFSMPartitionState **eaPartitionStates;	NO_AST
} GameLayerFSM;

extern ParseTable parse_GameLayerFSM[];
#define TYPE_parse_GameLayerFSM GameLayerFSM

// Gets a layer fsm, if one exists
SA_RET_OP_VALID GameLayerFSM *layerfsm_GetByName(SA_PARAM_NN_STR const char *pcName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Gets a layer fsm, from its FSM's name, NOT BY THE PLACED OBJ'S NAME.
void layerfsm_GetByFSMName(SA_PARAM_NN_STR const char *pcName, SA_PARAM_OP_VALID const WorldScope *pScope, SA_PARAM_NN_VALID GameLayerFSM ***peaFSMListResult);

// Translates over to GameLayerFSM from a WorldLayerFSM
SA_RET_OP_VALID GameLayerFSM *layerfsm_GetByEntry(SA_PARAM_NN_VALID WorldLayerFSM *pLayerFSM);

// Check if a layer fsm exists
bool layerfsm_LayerFSMExists(const char *pcName, const WorldScope *pScope);

// Gets a namelist usable from autocommands
NameList* layerfsm_GetNameList(void);

// Returns list of all vars
WorldVariable **layerfsm_GetAllVars(GameLayerFSM *pFSM);

// Finds a variable from an layer FSM that matches the name and type
WorldVariable *layerfsm_LookupVar(SA_PARAM_NN_VALID GameLayerFSM *pFSM, SA_PARAM_NN_STR const char *pcVarName);

// Get the FSM context state for a given layer FSM and partition
FSMContext *layerfsm_GetFSMContext(GameLayerFSM *pLayerFSM, int iPartitionIdx);

// Main processing loop for layer fsms
void layerfsm_OncePerFrame(void);

// Called on partition load and unload
void layerfsm_PartitionLoad(int iPartitionIdx);
void layerfsm_PartitionUnload(int iPartitionIdx);

// Called on map load and unload
void layerfsm_MapLoad(ZoneMap *pZoneMap);
void layerfsm_MapUnload(void);
void layerfsm_MapValidate(ZoneMap *pZoneMap, bool bIsMapEdit);


