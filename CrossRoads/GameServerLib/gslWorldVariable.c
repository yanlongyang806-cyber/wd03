#include "gslWorldVariable.h"

#include "Entity.h"
#include "EString.h"
#include "mission_common.h"
#include "MultiVal.h"
#include "SimpleParser.h"
#include "StringCache.h"
#include "WorldVariable.h"
#include "entCritter.h"
#include "ChoiceTable.h"
#include "gslMapVariable.h"
#include "rand.h"
#include "WorldGrid.h"
#include "GameBranch.h"
#include "ActivityCommon.h"
#include "gslActivity.h"

#include "AutoGen/WorldVariable_h_ast.h"
#include "AutoGen/gslWorldVariable_h_ast.h"
// Definitions for hardcoded Map Variables
WorldVariableDef **g_eaHardcodedWorldVariableDefs = NULL;
GlobalWorldVariableDefs g_GlobalWorldVariableDefs = {0};

// ----------------------------------------------------------------------------------
// Auto-Runs
// ----------------------------------------------------------------------------------

static void worldvariable_AddHardcodedDef(const char *pchName, WorldVariableType eType)
{
	WorldVariableDef *pDef = StructCreate(parse_WorldVariableDef);
	pDef->pcName = allocAddString(pchName);
	pDef->eType = eType;
	pDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
	pDef->pSpecificValue = StructCreate(parse_WorldVariable);
	pDef->pSpecificValue->pcName = pDef->pcName;
	pDef->pSpecificValue->eType = pDef->eType;
	eaPush(&g_eaHardcodedWorldVariableDefs, pDef);
}

AUTO_RUN;
void worldvariable_InitHardcodedWorldVariableDefs(void)
{
	// Force-Sidekick min/max level
	worldvariable_AddHardcodedDef(FORCESIDEKICK_MAPVAR_MIN, WVAR_INT);
	worldvariable_AddHardcodedDef(FORCESIDEKICK_MAPVAR_MAX, WVAR_INT);

	// Force Mission Return
	worldvariable_AddHardcodedDef(FORCEMISSIONRETURN_MAPVAR, WVAR_INT);

	// Flashback Mission
	worldvariable_AddHardcodedDef(FLASHBACKMISSION_MAPVAR, WVAR_STRING);
}


// ----------------------------------------------------------------------------------
// Support Functions
// ----------------------------------------------------------------------------------
WorldVariable *worldVariableCalcVariableAndAlloc(int iPartitionIdx, const WorldVariableDef* pVarDef, Entity *pSrcEnt, U32 seed, U32 uiTimeSecsSince2000)
{
	WorldVariable *pWorldVar = NULL;

	switch( pVarDef->eDefaultType ) {
		case WVARDEF_SPECIFY_DEFAULT:
			pWorldVar = StructClone(parse_WorldVariable, pVarDef->pSpecificValue);

		xcase WVARDEF_CHOICE_TABLE:
			if (!GET_REF(pVarDef->choice_table))
				return NULL;
			pWorldVar = StructClone(parse_WorldVariable, choice_ChooseValue(GET_REF(pVarDef->choice_table), pVarDef->choice_name, pVarDef->choice_index - 1, seed, uiTimeSecsSince2000));

		xcase WVARDEF_MAP_VARIABLE: {
			MapVariable* mapVar = mapvariable_GetByName(iPartitionIdx, pVarDef->map_variable);
			pWorldVar = StructClone(parse_WorldVariable, SAFE_MEMBER(mapVar, pVariable));
		}

		xcase WVARDEF_MISSION_VARIABLE: {
			MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pSrcEnt);
			Mission *pMission = mission_FindMissionFromRefString(pMissionInfo, pVarDef->mission_refstring);
			WorldVariableContainer *pWorldVarContainer = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pVarDef->mission_variable) : NULL;
			pWorldVar = StructCreate(parse_WorldVariable);
			if (pWorldVarContainer)
				worldVariableCopyFromContainer(pWorldVar, pWorldVarContainer);
		}

		xcase WVARDEF_EXPRESSION: {
			pWorldVar = StructCreate(parse_WorldVariable);
			worldVariableEvalExpression(iPartitionIdx, pSrcEnt, pVarDef->pExpression, pWorldVar, pVarDef->eType);
		}
		xcase WVARDEF_ACTIVITY_VARIABLE: {
			ActivityDef *pActivityDef = NULL;
			WorldVariableDef *pActivityVarDef = NULL;
			int i;
			for (i = 0; i < eaSize(&g_Activities.ppActivities); i++)
			{
				if (stricmp(g_Activities.ppActivities[i]->pchActivityName, pVarDef->activity_name) == 0)
				{
					pActivityDef = g_Activities.ppActivities[i]->pActivityDef;
					break;
				}
			}
			if (pActivityDef)
			{
				for (i = 0; i < eaSize(&pActivityDef->eaVaraiableDefs); i++)
				{
					if (stricmp(pActivityDef->eaVaraiableDefs[i]->pcName, pVarDef->activity_variable_name) == 0)
					{
						pActivityVarDef = pActivityDef->eaVaraiableDefs[i];
						break;
					}
				}
			}
			pWorldVar = pActivityVarDef ? 
				worldVariableCalcVariableAndAlloc(iPartitionIdx, pActivityVarDef, pSrcEnt, seed, uiTimeSecsSince2000)
				:  pVarDef->activity_default_value;
		}
	}

	if (pWorldVar)
		pWorldVar->pcName = pVarDef->pcName;
	return pWorldVar;
}

/// Utility function that calculates an array of variables.
///
/// This function allocates a new array that should be freed with
/// eaDestroyStruct().
WorldVariable** worldVariableCalcVariablesAndAlloc(int iPartitionIdx, WorldVariableDef** varDefs, Entity *pSrcEnt, U32 seed, U32 uiTimeSecsSince2000)
{
	WorldVariable** accum = NULL;
	
	int it;
	for(it = 0; it != eaSize(&varDefs); ++it) {
		WorldVariableDef* varDef = varDefs[it];
		WorldVariable* var = worldVariableCalcVariableAndAlloc(iPartitionIdx, varDef, pSrcEnt, seed, uiTimeSecsSince2000);

		if (!var)
			continue;
		eaPush(&accum, var);
	}

	return accum;
}

/// Utility function that calculates an array of variables. Uses a random seed with every variable.
///
/// This function allocates a new array that should be freed with
/// eaDestroyStruct().
WorldVariable** worldVariableCalcVariablesAndAllocRandom(int iPartitionIdx, WorldVariableDef** varDefs, Entity *pSrcEnt, U32 uiTimeSecsSince2000)
{
	WorldVariable** accum = NULL;
	
	int it;
	for(it = 0; it != eaSize(&varDefs); ++it) {
		WorldVariableDef* varDef = varDefs[it];
		WorldVariable* var = worldVariableCalcVariableAndAlloc(iPartitionIdx, varDef, pSrcEnt, randomInt(), uiTimeSecsSince2000);

		if (!var)
			continue;
		eaPush(&accum, var);
	}

	return accum;
}

/// Utility function that calculates an array of variables without allocating each element.
///
/// Array should be destroyed with eaDestroy().
/// Note (JDJ): this should not be used anymore because of the possible use of mission variables,
///             which are WorldVariableContainers and MUST be alloced into a WorldVariable since
///             the definition can differ slightly from WorldVariable.
/*WorldVariable** worldVariableCalcVariables(WorldVariableDef** varDefs, const Mission *pMission, int seed)
{
	WorldVariable** accum = NULL;
	int it;
	for(it = 0; it != eaSize(&varDefs); ++it) {
		WorldVariable* var = worldVariableCalcVariable(varDefs[it], pMission, seed);
		if (var)
			eaPush(&accum, var);
	}
	return accum;
}*/

/// Utility function that creates array of variable defs from each def's "specific value"
///
/// Array should be destroyed with eaDestroy().
WorldVariable** worldVariableGetSpecificValues(WorldVariableDef** varDefs)
{
	WorldVariable** accum = NULL;
	int it;
	for(it = 0; it != eaSize(&varDefs); ++it) {
		WorldVariable* var = varDefs[it]->pSpecificValue;
		if (var)
			eaPush(&accum, var);
	}
	return accum;
}

void worldVariableEvalExpression(int iPartitionIdx, Entity *pSrcEnt, Expression *pExpr, WorldVariable *pResultVar, WorldVariableType eVarType)
{
	ExprContext *pExprContext = worldVariableGetExprContext();
	MultiVal resultVal = {0};

	// set context pointers
	if (!pSrcEnt) {
		// If no entity, then use a context without player.
		// SelfPtr and "Player" pointer set to NULL in next few lines
		pExprContext = worldVariableGetNoPlayerExprContext();
	}
	exprContextSetPointerVarPooled(pExprContext, "Player", pSrcEnt, parse_Entity, false, true);
	exprContextSetSelfPtr(pExprContext, pSrcEnt);
	exprContextSetPartition(pExprContext, iPartitionIdx);

	// evaluate the expression
	exprEvaluate(pExpr, pExprContext, &resultVal);

	// convert to world variable
	worldVariableFromMultival(&resultVal, pResultVar, eVarType);
}

// ----------------------------------------------------------------------------------
// Validation
// ----------------------------------------------------------------------------------

static WorldVariableDef* gslWorldVariableHardcodedDefFromPooledName(const char *pchPooledName)
{
	int i;
	for (i = 0; i < eaSize(&g_eaHardcodedWorldVariableDefs); i++){
		if (pchPooledName && pchPooledName == g_eaHardcodedWorldVariableDefs[i]->pcName){
			return g_eaHardcodedWorldVariableDefs[i];
		}
	}
	return NULL;
}

bool gslWorldVariableValidateEx(ZoneMapInfo *zminfo, WorldVariable **vars, const char *reason, const char *filename, bool bIgnoreMessageOwnership)
{
	bool result = true;
	int i;

	for(i=eaSize(&vars)-1; i>=0; --i) {
		WorldVariableDef *def = zmapInfoGetVariableDefByName(zminfo, vars[i]->pcName);
		if (!def){
			def = gslWorldVariableHardcodedDefFromPooledName(vars[i]->pcName);
		}
		result &= worldVariableValidateEx(def, vars[i], reason, filename, bIgnoreMessageOwnership);
	}
	return result;
}

bool gslWorldVariableValidate(ZoneMapInfo *zminfo, WorldVariable **vars, const char *reason, const char *filename)
{
	return gslWorldVariableValidateEx(zminfo, vars, reason, filename, false);
}

// Pushes variables from the "in_vars" into the "out_vars" in a deterministic order
// and only if the type matches and the value is not the same as the default
void gslWorldVariableNormalizeVariables(ZoneMapInfo *zminfo, WorldVariable **in_vars, WorldVariable ***out_vars)
{
	int i, j;

	for(i=zmapInfoGetVariableCount(zminfo)-1; i>=0; --i) 
	{
		WorldVariableDef *def = zmapInfoGetVariableDef(zminfo, i);
		assert(def);
		for(j=eaSize(&in_vars)-1; j>=0; --j) {
			if ((stricmp(def->pcName, in_vars[j]->pcName) == 0) && 
				(def->eType == in_vars[j]->eType) &&
				((def->eDefaultType != WVARDEF_SPECIFY_DEFAULT) || !worldVariableEquals(in_vars[j], def->pSpecificValue))) {
					eaPush(out_vars, in_vars[j]);
			}
		}
	}

	for (i=eaSize(&g_eaHardcodedWorldVariableDefs)-1; i>=0; --i)
	{
		WorldVariableDef *def = g_eaHardcodedWorldVariableDefs[i];
		assert(def);
		for(j=eaSize(&in_vars)-1; j>=0; --j) {
			if ((stricmp(def->pcName, in_vars[j]->pcName) == 0) && 
				(eaFind(out_vars, in_vars[j]) == -1) &&
				(def->eType == in_vars[j]->eType) &&
				((def->eDefaultType != WVARDEF_SPECIFY_DEFAULT) || !worldVariableEquals(in_vars[j], def->pSpecificValue))) {
					eaPush(out_vars, in_vars[j]);
			}
		}
	}

	for(i=eaSize(&g_GlobalWorldVariableDefs.ppVarDefs)-1; i>=0; --i)
	{
		WorldVariableDef *def = g_GlobalWorldVariableDefs.ppVarDefs[i]->pVariableDef;

		assert(def);
		for(j=eaSize(&in_vars)-1; j>=0; --j) {
			if ((stricmp(def->pcName, in_vars[j]->pcName) == 0) && 
				(eaFind(out_vars, in_vars[j]) == -1) &&
				(def->eType == in_vars[j]->eType) &&
				((def->eDefaultType != WVARDEF_SPECIFY_DEFAULT) || !worldVariableEquals(in_vars[j], def->pSpecificValue))) {
					eaPush(out_vars, in_vars[j]);
			}
		}
	}
}

AUTO_STARTUP(GlobalWorldVars);
void gslGlobalWorldVarsLoad(void)
{
	char *pSharedMemoryName = NULL;	
	char *pBuffer = NULL;

	loadstart_printf("Loading Global World Variables...");

	MakeSharedMemoryName(GameBranch_GetFilename(&pBuffer, "GlobalWorldVars"), &pSharedMemoryName);
	ParserLoadFilesShared(pSharedMemoryName,NULL,"defs/GlobalWorldVars.def", "GlobalWorldVars.bin", PARSER_OPTIONALFLAG, parse_GlobalWorldVariableDefs, &g_GlobalWorldVariableDefs);

	loadend_printf(" done (%d Global World Variables)",eaSize(&g_GlobalWorldVariableDefs.ppVarDefs));
}

#include "AutoGen/gslWorldVariable_h_ast.c"