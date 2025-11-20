//// Misc server utilities dealing with variable evaluation.
#pragma once
GCC_SYSTEM

#include "AppLocale.h"
#include "GlobalEnums.h"

typedef struct Entity Entity;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct MissionInfo MissionInfo;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct MultiVal MultiVal;
typedef struct ZoneMapInfo ZoneMapInfo;

typedef enum WorldVariableType WorldVariableType;

AUTO_STRUCT;
typedef struct GlobalWorldVariableDef
{
	WorldVariableDef *pVariableDef;					
	U32 *eMapTypes;									AST(NAME("MapType") SUBTABLE(ZoneMapTypeEnum))
}GlobalWorldVariableDef;

AUTO_STRUCT;
typedef struct GlobalWorldVariableDefs
{
	GlobalWorldVariableDef **ppVarDefs;				AST(NAME("GlobalVar"))
}GlobalWorldVariableDefs;

void worldVariableEvalExpression(int iPartitionIdx, Entity *pSrcEnt, Expression *pExpr, WorldVariable *pResultVar, WorldVariableType eVarType);

SA_RET_OP_VALID WorldVariable *worldVariableCalcVariableAndAlloc(int iPartitionIdx, SA_PARAM_NN_VALID const WorldVariableDef* pVarDef, Entity *pSrcEnt, U32 seed, U32 uiTimeSecsSince2000);
WorldVariable** worldVariableCalcVariablesAndAlloc(int iPartitionIdx, WorldVariableDef** varDefs, Entity *pSrcEnt, U32 seed, U32 uiTimeSecsSince2000);
WorldVariable** worldVariableCalcVariablesAndAllocRandom(int iPartitionIdx, WorldVariableDef** varDefs, Entity *pSrcEnt, U32 uiTimeSecsSince2000);
//WorldVariable** worldVariableCalcVariables(WorldVariableDef** varDefs, const Mission *pMission, int seed);
WorldVariable** worldVariableGetSpecificValues(WorldVariableDef** varDefs);

bool gslWorldVariableValidate(ZoneMapInfo *zminfo, WorldVariable **vars, const char *reason, const char *filename);
bool gslWorldVariableValidateEx(ZoneMapInfo *zminfo, WorldVariable **vars, const char *reason, const char *filename, bool bIgnoreMessageOwnership);

// Pushes variables from the "in_vars" into the "out_vars" in a deterministic order
// and only if the type matches and the value is not the same as the default
void gslWorldVariableNormalizeVariables(ZoneMapInfo *zminfo, WorldVariable **in_vars, WorldVariable ***out_vars);

extern WorldVariableDef **g_eaHardcodedWorldVariableDefs;
extern GlobalWorldVariableDefs g_GlobalWorldVariableDefs;