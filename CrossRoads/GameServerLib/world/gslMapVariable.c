/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ChoiceTable_common.h"
#include "error.h"
#include "GameServerLib.h"
#include "ChoiceTable.h"
#include "gslMapState.h"
#include "gslMapVariable.h"
#include "gslPartition.h"
#include "gslWorldVariable.h"
#include "mapstate_common.h"
#include "rand.h"
#include "ResourceManager.h"
#include "WorldGrid.h"
#include "StringCache.h"

// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

typedef struct PartitionMapVariables
{
	U32 uPartition_dbg;
	int iPartitionIdx;		// Partition
	
	// Regular map variables
	MapVariable **eaMapVariables; 

	// These are hardcoded map variables that should be accessible from code only
	MapVariable **eaCodeOnlyMapVariables;
} PartitionMapVariables;

static PartitionMapVariables **s_eaPartitionMapVariables = NULL;

// Overrides set by console commands and such
WorldVariable **g_eaMapVariableOverrides = NULL;


// ----------------------------------------------------------------------------------
// Support Functions
// ----------------------------------------------------------------------------------

static PartitionMapVariables *PartitionMapVariablesGet(int iPartitionIdx)
{
	PartitionMapVariables *pVars = eaGet(&s_eaPartitionMapVariables, iPartitionIdx);
	assertmsgf(pVars, "Partition %d does not exist for map variables", iPartitionIdx);
	return pVars;
}


// Gets a variable, if one exists
SA_RET_OP_VALID MapVariable *mapvariable_GetByName(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVarName)
{
	PartitionMapVariables *pVars;
	int i;

	if (iPartitionIdx == PARTITION_IN_TRANSACTION) {
		return NULL;
	}
	
	pVars = PartitionMapVariablesGet(iPartitionIdx);

	for(i=eaSize(&pVars->eaMapVariables)-1; i>=0; --i) {
		if (stricmp(pVars->eaMapVariables[i]->pcName, pcVarName) == 0) {
			return pVars->eaMapVariables[i];
		}
	}

	return NULL;
}


// Gets a variable, if one exists - also searches special hardcoded map variables
SA_RET_OP_VALID MapVariable *mapvariable_GetByNameIncludingCodeOnly(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVarName)
{
	PartitionMapVariables *pVars;
	int i;
	
	pVars = PartitionMapVariablesGet(iPartitionIdx);

	for(i=eaSize(&pVars->eaMapVariables)-1; i>=0; --i) {
		if (stricmp(pVars->eaMapVariables[i]->pcName, pcVarName) == 0) {
			return pVars->eaMapVariables[i];
		}
	}

	for(i=eaSize(&pVars->eaCodeOnlyMapVariables)-1; i>=0; --i) {
		if (stricmp(pVars->eaCodeOnlyMapVariables[i]->pcName, pcVarName) == 0) {
			return pVars->eaCodeOnlyMapVariables[i];
		}
	}

	return NULL;
}


// Gets all variables
void mapvariable_GetAll(int iPartitionIdx, MapVariable ***peaMapVars)
{
	PartitionMapVariables *pVars = PartitionMapVariablesGet(iPartitionIdx);
	eaCopy(peaMapVars, &pVars->eaMapVariables);
}


const char *mapvariable_GetAllAsString(int iPartitionIdx)
{
	PartitionMapVariables *pVars;
	WorldVariable **eaVars = NULL;
	const char *pchVars = NULL;
	int i;

	pVars = PartitionMapVariablesGet(iPartitionIdx);

	for (i=0; i < eaSize(&pVars->eaMapVariables); i++) {
		eaPush(&eaVars, pVars->eaMapVariables[i]->pVariable);
	}
	if (eaVars) {
		pchVars = worldVariableArrayToString(eaVars);
		eaDestroy(&eaVars);
	}

	return pchVars;
}


void mapvariable_GetAllAsWorldVars(int iPartitionIdx, WorldVariable ***peaWorldVars)
{
	int i;
	PartitionMapVariables *pVars;

	PERFINFO_AUTO_START_FUNC();
	
	pVars = PartitionMapVariablesGet(iPartitionIdx);

	for(i = 0; i < eaSize(&pVars->eaMapVariables); i++) {
		if (pVars->eaMapVariables[i]->pVariable) {
			WorldVariable* pWorldVar = StructClone(parse_WorldVariable, pVars->eaMapVariables[i]->pVariable);
			if (pWorldVar) {
				eaPush(peaWorldVars, pWorldVar);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


void mapvariable_GetAllAsWorldVarsNoCopy(int iPartitionIdx, WorldVariable ***peaWorldVars)
{
	int i;
	PartitionMapVariables *pVars;

	PERFINFO_AUTO_START_FUNC();
	
	pVars = PartitionMapVariablesGet(iPartitionIdx);

	for(i = 0; i < eaSize(&pVars->eaMapVariables); i++) {
		WorldVariable* pWorldVar = pVars->eaMapVariables[i]->pVariable;
		if (pWorldVar) {
			eaPush(peaWorldVars, pWorldVar);
		}
	}

	PERFINFO_AUTO_STOP();
}


WorldVariableArray *mapVariable_GetAllAsWorldVarArray(int iPartitionIdx)
{
	WorldVariableArray *pVariableArray = StructCreate(parse_WorldVariableArray);
	mapvariable_GetAllAsWorldVars(iPartitionIdx, &pVariableArray->eaVariables);
	return pVariableArray;
}


// This is called after every location that modifies a map variable
void mapVariable_NotifyModified(int iPartitionIdx, MapVariable *pMapVar)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	const char *pcName = pMapVar->pcName;
	if (pcName && mapState_GetPublicVarByName(pState, pcName)) {
		mapState_SetPublicVar(iPartitionIdx, pMapVar->pVariable, true);
	}
}


// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------

static void mapvariable_Free(MapVariable *pMapVar)
{
	StructDestroy(parse_WorldVariable, pMapVar->pVariable);
	free(pMapVar);
}


static void mapvariable_AddVariable(int iPartitionIdx, WorldVariableDef *pDef, int iSeed, PartitionMapVariables *pVars)
{
	MapVariable *pMapVar = calloc(1,sizeof(MapVariable));
	pMapVar->pcName = pDef->pcName;
	pMapVar->pDef = pDef;

	pMapVar->pVariable = worldVariableCalcVariableAndAlloc(iPartitionIdx, pDef, NULL, iSeed, 0);
	if (!pMapVar->pVariable) {
		pMapVar->pVariable = StructCreate(parse_WorldVariable);
		assert(pMapVar->pVariable);
		pMapVar->pVariable->eType = pDef->eType;
	}

	if (!pMapVar->pVariable->pcName) {
		pMapVar->pVariable->pcName = pDef->pcName;
	}

	eaPush(&pVars->eaMapVariables, pMapVar);

	if (pDef->bIsPublic) {
		mapState_AddPublicVar(iPartitionIdx, pMapVar->pVariable);
	}
}


static void mapvariable_AddCodeOnlyVariable(int iPartitionIdx, WorldVariableDef *pDef, int iSeed, PartitionMapVariables *pVars)
{
	MapVariable *pMapVar = calloc(1,sizeof(MapVariable));
	pMapVar->pcName = pDef->pcName;
	pMapVar->pDef = pDef;

	pMapVar->pVariable = worldVariableCalcVariableAndAlloc(iPartitionIdx, pDef, NULL, iSeed, 0);
	if (!pMapVar->pVariable) {
		pMapVar->pVariable = StructCreate(parse_WorldVariable);
		assert(pMapVar->pVariable);
		pMapVar->pVariable->eType = pDef->eType;
	}

	if (!pMapVar->pVariable->pcName) {
		pMapVar->pVariable->pcName = pDef->pcName;
	}

	eaPush(&pVars->eaCodeOnlyMapVariables, pMapVar);

	if (pDef->bIsPublic) {
		mapState_AddPublicVar(iPartitionIdx, pMapVar->pVariable);
	}
}


static void mapvariable_ClearList(PartitionMapVariables *pVars)
{
	eaDestroyEx(&pVars->eaMapVariables, mapvariable_Free);
	eaDestroyEx(&pVars->eaCodeOnlyMapVariables, mapvariable_Free);
}


void mapvariable_PartitionValidate(int iPartitionIdx)
{
	PartitionMapVariables *pVars;
	int i,j;

	PERFINFO_AUTO_START_FUNC();
	
	pVars = PartitionMapVariablesGet(iPartitionIdx);

	// Make sure there are no duplicate variables defined
	for(i=eaSize(&pVars->eaMapVariables)-1; i>=0; --i) {
		for(j=i-1; j>=0; --j) {
			if (stricmp(pVars->eaMapVariables[i]->pcName, pVars->eaMapVariables[j]->pcName) == 0) {
				ErrorFilenamef(zmapGetFilename(NULL), "Duplicate map variable named '%s'", pVars->eaMapVariables[i]->pcName);
			}
		}
	}

	// Check types and defaults
	for(i=eaSize(&pVars->eaMapVariables)-1; i>=0; --i) {
		WorldVariableDef *pDef = pVars->eaMapVariables[i]->pDef;

		if (!resIsValidName(pDef->pcName)) {
			ErrorFilenamef(zmapGetFilename(NULL), "Map variable named '%s' is not properly formed.  It must start with an alphabetic character and contain only alphanumerics, underscore, dot, and dash.", pVars->eaMapVariables[i]->pcName);
		}

		if (pDef->eType == WVAR_NONE) {
			ErrorFilenamef(zmapGetFilename(NULL), "Map variable named '%s' has illegal type 'NONE'", pVars->eaMapVariables[i]->pcName);
		}
		if (pDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
			if (!pDef->pSpecificValue) {
				ErrorFilenamef(zmapGetFilename(NULL), "Map variable named '%s' is missing its default value", pVars->eaMapVariables[i]->pcName);
			} else {
				worldVariableValidate(pDef, pDef->pSpecificValue, "Map variable", zmapGetFilename(NULL));
			}
		}
		if (pDef->eDefaultType == WVARDEF_CHOICE_TABLE) {
			if (!GET_REF(pDef->choice_table)) {
				if (!IS_HANDLE_ACTIVE(pDef->choice_table)) {
					ErrorFilenamef(zmapGetFilename(NULL), "Map variable named '%s' is missing its choice table", pVars->eaMapVariables[i]->pcName);
				} else {
					ErrorFilenamef(zmapGetFilename(NULL), "Map variable named '%s' refers to non-existant choice table '%s'", pVars->eaMapVariables[i]->pcName, REF_STRING_FROM_HANDLE(pDef->choice_table));
				}
			} else {
				ChoiceTable* choice = GET_REF(pDef->choice_table);
				const char* choiceName = pDef->choice_name;
				if( choice_ValueType( choice, choiceName ) != pDef->eType ) {
					ErrorFilenamef(zmapGetFilename(NULL), "Map variable named '%s' is of type %s but has a choice table with a different type",
						pVars->eaMapVariables[i]->pcName, StaticDefineIntRevLookup(WorldVariableTypeEnum, pDef->eType));
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


// Passing NULL for pZoneMap uses the current map, so that's a safe option
void mapvariable_PartitionLoad(ZoneMap *pZoneMap, int iPartitionIdx, int bFullInit)
{
	WorldVariable **eaMapVars = NULL;
	int i, j, iNumVars, iSeed;
	ZoneMapInfo *zminfo;
	PartitionMapVariables *pVars;

	PERFINFO_AUTO_START_FUNC();

	// See if partition data already exists, and create if not
	pVars = eaGet(&s_eaPartitionMapVariables, iPartitionIdx);
	if (!pVars) {
		pVars = calloc(1,sizeof(PartitionMapVariables));
		pVars->iPartitionIdx = iPartitionIdx;
		pVars->uPartition_dbg = partition_IDFromIdx(iPartitionIdx);
		eaSet(&s_eaPartitionMapVariables, pVars, iPartitionIdx);
	}

	// pZoneMap is NULL on partition init outside of load, which gets current map's info, which is correct
	zminfo = zmapGetInfo(pZoneMap);

	//randomize seed
	iSeed = randomInt();

	if (bFullInit) {
		// Clear all data
		mapvariable_ClearList(pVars);

		// Get input vars
		worldVariableStringToArray(partition_MapVariablesFromIdx(iPartitionIdx), &eaMapVars);
		gslWorldVariableValidateEx(zminfo, eaMapVars, "Map initialization from entry door (using map variable)", zmapGetFilename(NULL), true);

		//search for pre-set seed var
		if (gConf.bAllowSpecifiedMapVarSeed) {
			WorldVariableDef *pSeedDef = zmapInfoGetVariableDefByName(zminfo, MAP_VAR_SEED_NAME);
			MapVariable *pSeed = NULL;
			if (pSeedDef) {
				// Add the seed
				mapvariable_AddVariable(iPartitionIdx, pSeedDef, iSeed, pVars);
				pSeed = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, MAP_VAR_SEED_NAME);
			}
			if (pSeed) {
				WorldVariable *pSeedValue = NULL;

				// Look for seed in passed-in vars
				for(i=eaSize(&eaMapVars)-1; i>=0; i--) {
					if (stricmp(eaMapVars[i]->pcName, MAP_VAR_SEED_NAME) == 0) {
						break;
					}
				}

				if (i >= 0) {
					pSeedValue = eaMapVars[i];
				} else {		
					// Look for seed in overridden vars
					for(i=eaSize(&g_eaMapVariableOverrides)-1; i>=0; --i) {
						if (stricmp(g_eaMapVariableOverrides[i]->pcName, MAP_VAR_SEED_NAME) == 0) {
							break;
						}
					}

					if (i >= 0) {
						pSeedValue = g_eaMapVariableOverrides[i];
					}
				}

				// Set seed
				if (pSeedValue) {
					StructCopyAll(parse_WorldVariable, pSeedValue, pSeed->pVariable);
					mapState_SetPublicVar(iPartitionIdx, pSeed->pVariable, false);
					iSeed = pSeedValue->iIntVal;
				}
			}
		}
	}

	// Initialize all variables
	iNumVars = zmapInfoGetVariableCount(zminfo);
	for(i=0; i<iNumVars; ++i) {
		WorldVariableDef* varDef = zmapInfoGetVariableDef(zminfo, i);
		if (!varDef) {
			continue;
		}
		
		for(j = eaSize(&pVars->eaMapVariables)-1; j >= 0 ; --j) {
			if (pVars->eaMapVariables[j]->pcName == varDef->pcName) {
				break;
			}
		}

		if (j >= 0) {
			continue;
		}
		
		mapvariable_AddVariable(iPartitionIdx, varDef, iSeed, pVars);
	}

	// Initialize global map variables
	iNumVars = eaSize(&g_GlobalWorldVariableDefs.ppVarDefs);
	for(i=0;i<iNumVars; ++i)
	{
		GlobalWorldVariableDef *varDef = g_GlobalWorldVariableDefs.ppVarDefs[i];

		if(!varDef)
			continue;

		if(ea32Size(&varDef->eMapTypes) && ea32Find(&varDef->eMapTypes,zmapInfoGetMapType(zminfo)) == -1)
			continue;

		for(j = eaSize(&pVars->eaMapVariables)-1; j>=0; --j)
		{
			if(pVars->eaMapVariables[j]->pcName == varDef->pVariableDef->pcName)
				break;
		}

		if(j>=0){
			continue;
		}

		mapvariable_AddVariable(iPartitionIdx, varDef->pVariableDef,iSeed,pVars);
	}

	// Initialize hardcoded map variables
	iNumVars = eaSize(&g_eaHardcodedWorldVariableDefs);
	for(i=0; i<iNumVars; ++i) {
		WorldVariableDef* varDef = g_eaHardcodedWorldVariableDefs[i];
		if (!varDef) {
			continue;
		}

		for(j = eaSize(&pVars->eaMapVariables)-1; j >= 0 ; --j) {
			if (pVars->eaMapVariables[j]->pcName == varDef->pcName) {
				break;
			}
		}
		if (j >= 0) {
			continue;
		}

		for(j = eaSize(&pVars->eaCodeOnlyMapVariables)-1; j >= 0 ; --j) {
			if (pVars->eaCodeOnlyMapVariables[j]->pcName == varDef->pcName) {
				break;
			}
		}
		if (j >= 0) {
			continue;
		}

		mapvariable_AddCodeOnlyVariable(iPartitionIdx, varDef, iSeed, pVars);
	}

	//If the map state has recently been cleared, this will reset all of the public map variables.
	//If not, it doesn't hurt to set them again.
	for (i = 0; i < eaSize(&pVars->eaMapVariables); ++i)
	{
		MapVariable *pVar = pVars->eaMapVariables[i];

		if (pVar)
		{
			mapState_SetPublicVar(iPartitionIdx, pVar->pVariable, true);
		}
	}

	if (bFullInit) {
		// Apply the map state
		for(i=eaSize(&eaMapVars)-1; i>=0; --i) {
			MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, eaMapVars[i]->pcName);
			if (pMapVar && (pMapVar->pVariable->eType == eaMapVars[i]->eType)) {
				StructCopyAll(parse_WorldVariable, eaMapVars[i], pMapVar->pVariable);
				mapState_SetPublicVar(iPartitionIdx, pMapVar->pVariable, false);
			}
		}

		// Apply overrides from commands, etc.
		// Don't validate the variables here because sometimes these will be set from the
		// command line so that they apply to all maps, and this shouldn't give errors on servers
		// where the variable doesn't apply.
		for(i=eaSize(&g_eaMapVariableOverrides)-1; i>=0; --i) {
			MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, g_eaMapVariableOverrides[i]->pcName);
			if (pMapVar && (pMapVar->pVariable->eType == g_eaMapVariableOverrides[i]->eType)) {
				StructCopyAll(parse_WorldVariable, g_eaMapVariableOverrides[i], pMapVar->pVariable);
				mapState_SetPublicVar(iPartitionIdx, pMapVar->pVariable, false);
			}
		}
	}
	eaDestroyStruct(&eaMapVars, parse_WorldVariable);

	PERFINFO_AUTO_STOP();
}


static void mapvariable_PartitionLoadWrapper(int iPartitionIdx, int *pbFullInit)
{
	mapvariable_PartitionLoad(NULL, iPartitionIdx, *pbFullInit);
}


void mapvariable_PartitionUnload(int iPartitionIdx, bool bFullInit)
{
	if (bFullInit){
		PartitionMapVariables *pVars = eaGet(&s_eaPartitionMapVariables, iPartitionIdx);
		if (pVars) { // okay for this to be NULL
			mapvariable_ClearList(pVars);
			eaSet(&s_eaPartitionMapVariables, NULL, iPartitionIdx);
			free(pVars);
		}
	}
}


void mapvariable_MapValidate(ZoneMap *pZoneMap)
{
	int i;

	// Validate any open partitions
	for(i=0; i<eaSize(&s_eaPartitionMapVariables); ++i) {
		if (s_eaPartitionMapVariables[i]) {
			mapvariable_PartitionValidate(i);
		}
	}
}


void mapvariable_MapLoad(ZoneMap *pZoneMap, int bFullInit)
{
	// Reload any open partitions
	partition_ExecuteOnEachPartitionWithData(mapvariable_PartitionLoadWrapper, &bFullInit);
}


void mapvariable_MapUnload(int bFullInit)
{
	int i;

	// Destroy any open partitions
	for(i=0; i<eaSize(&s_eaPartitionMapVariables); ++i) {
		if (s_eaPartitionMapVariables[i]) {
			mapvariable_PartitionUnload(i, bFullInit);
		}
	}
}

// Dbg functions created for partition testing

char* mapvariable_CreateMapVariableDbgEx(Entity *pEnt, const char *pcName, ACMD_NAMELIST(WorldVariableTypeEnum, STATICDEFINE) const char *pcType, const char *pcValue)
{
	static char *res = NULL;
	WorldVariableType eType = StaticDefineIntGetInt(WorldVariableTypeEnum, pcType);
	MapVariable *pMapVar = calloc(1,sizeof(MapVariable));
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	PartitionMapVariables *pVars = PartitionMapVariablesGet(iPartitionIdx);
	estrClear(&res);

	if(mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, pcName))
	{
		estrPrintf(&res,"var %s already exists. aborting", pcName);
		return res;
	}

	pMapVar->pcName = strdup(pcName);
	pMapVar->pVariable = StructCreate(parse_WorldVariable);
	pMapVar->pVariable->eType = eType;
	pMapVar->pVariable->pcName = StructAllocString(pcName);
	if(worldVariableFromString(pMapVar->pVariable, pcValue, &res))
	{
		eaPush(&pVars->eaMapVariables, pMapVar);
		estrPrintf(&res, "added var %s", pcName);
	}

	return res;
}

// created for partition testing
char* mapvariable_CreateMapVariableDbg(Entity *pEnt, const char *pcName, const char *pcValue)
{
	return mapvariable_CreateMapVariableDbgEx(pEnt, pcName, "String", pcValue);
}
