/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct WorldScope WorldScope;
typedef struct WorldTriggerCondition WorldTriggerCondition;
typedef struct ZoneMap ZoneMap;

typedef struct TriggerConditionPartitionState
{
	// When a partition is first loaded, the trigger condition states are not initialized to the result of the Expression. This flag
	// provides this information so the first time the state is initialized, it can be communicated to the connected clients. This fixes
	// an editor bug that led to an FX continuing even if the Expression is changed so that its result is 0.
	bool bStateNeverUpdated;

	int state;
} TriggerConditionPartitionState;

typedef struct GameTriggerCondition
{
	// The spawn point's map-level name
	const char *pcName;

	// The world spawn point
	WorldTriggerCondition *pWorldTriggerCondition;

	// The trigger condition's state per partition
	TriggerConditionPartitionState **eaPartitionStates;
} GameTriggerCondition;


// Gets a trigger condition, if one exists
SA_RET_OP_VALID GameTriggerCondition *triggercondition_GetByName(SA_PARAM_NN_STR const char *pcTriggerConditionName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Translates over to GameTriggerCondition from a WorldTriggerCondition
SA_RET_OP_VALID GameTriggerCondition *triggercondition_GetByEntry(SA_PARAM_NN_VALID WorldTriggerCondition *pTriggerCondition);

// Check if a trigger condition exists
bool triggercondition_TriggerConditionExists(const char *pcName, const WorldScope *pScope);

// Get the state of a trigger condition
int triggercondition_GetState(int iPartitionIdx, GameTriggerCondition *pTriggerCondition);

// Turn on initial trigger conditions.
void triggercondition_InitClientTriggerConditions(SA_PARAM_NN_VALID Entity* playerEnt);

// Turn off all trigger conditions that are currently "on"
// Used when doing initEncounters to simulate the player leaving the map
void triggercondition_ResetClientTriggerConditions(SA_PARAM_NN_VALID Entity* playerEnt);

// Main processing loop for trigger conditions (currently, they can change world FX)
void triggercondition_UpdateTriggerConditions(void);

// Called on partition load and unload
void triggercondition_PartitionLoad(int iPartitionIdx);
void triggercondition_PartitionUnload(int iPartitionIdx);

// Called on map load and unload
void triggercondition_MapLoad(ZoneMap *pZoneMap);
void triggercondition_MapUnload(void);
void triggercondition_MapValidate(ZoneMap *pZoneMap);

