/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct WorldVariableArray WorldVariableArray;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapInfo ZoneMapInfo;

#define MAP_VAR_SEED_NAME "SYS_MAP_VAR_SEED"


typedef struct MapVariable
{
	// The map variable's name
	const char *pcName;         // POOL_STRING

	// The variable definition
	WorldVariableDef *pDef;

	// The current value
	WorldVariable *pVariable;
} MapVariable;


// Gets a variable, if one exists
SA_RET_OP_VALID MapVariable *mapvariable_GetByName(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVarName);

// Gets a variable, if one exists - also searches special hardcoded map variables
SA_RET_OP_VALID MapVariable *mapvariable_GetByNameIncludingCodeOnly(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVarName);

// Gets all the map variables
void mapvariable_GetAll(int iPartitionIdx, MapVariable*** ppMapVars);
const char* mapvariable_GetAllAsString(int iPartitionIdx);
void mapvariable_GetAllAsWorldVars(int iPartitionIdx, WorldVariable ***peaWorldVars);
void mapvariable_GetAllAsWorldVarsNoCopy(int iPartitionIdx, WorldVariable ***peaWorldVars);
SA_RET_NN_VALID WorldVariableArray *mapVariable_GetAllAsWorldVarArray(int iPartitionIdx);

// Used to notify of variable modified
void mapVariable_NotifyModified(int iPartitionIdx, MapVariable *pMapVar);

// Called on map load and unload
void mapvariable_MapLoad(ZoneMap *pZoneMap, int bFullInit);
void mapvariable_MapUnload(int bFullInit);
void mapvariable_MapValidate(ZoneMap *pZoneMap);

// multiple partitions can be loaded and loaded. these are 
// always called between map loads and unloads
void mapvariable_PartitionLoad(ZoneMap *pZoneMap, int iPartitionIdx, int bFullInit);
void mapvariable_PartitionUnload(int iPartitionIdx, bool bFullInit);
void mapvariable_PartitionValidate(int iPartitionIdx);


extern WorldVariable **g_eaMapVariableOverrides;
