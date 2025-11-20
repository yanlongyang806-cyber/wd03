#include "gclMapState.h"
#include "mapstate_common.h"
#include "math.h"
#include "WorldVariable.h"

//-----------------------------------------------------------------------
//   Expression functions
//-----------------------------------------------------------------------

// Returns the int value of a public ShardVariable
AUTO_EXPR_FUNC(UIGen);
int mapState_FuncShardVariableGetInt(const char *pcName)
{
	WorldVariable *pVar = mapState_GetShardPublicVar(mapStateClient_Get(), pcName);
	if(pVar && pVar->eType==WVAR_INT) {
		return pVar->iIntVal;
	}
	return 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MapGetPublicVariableInt);
ExprFuncReturnVal mapState_FuncMapGetPublicVariableInt(ACMD_EXPR_INT_OUT piValOut, SA_PARAM_NN_STR const char *pcVarName)
{
	WorldVariable *pVar = mapState_GetPublicVarByName(mapStateClient_Get(), pcVarName);
	if (pVar && pVar->eType == WVAR_INT) {
		(*piValOut) = pVar->iIntVal;
	} else {
		(*piValOut) = 0;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MapGetPublicVariableFloat);
ExprFuncReturnVal mapState_FuncMapGetPublicVariableFloat(ACMD_EXPR_FLOAT_OUT pfValOut, SA_PARAM_NN_STR const char *pcVarName)
{
	WorldVariable *pVar = mapState_GetPublicVarByName(mapStateClient_Get(), pcVarName);
	if (pVar && pVar->eType == WVAR_FLOAT) {
		(*pfValOut) = pVar->fFloatVal;
	} else {
		(*pfValOut) = 0;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MapGetPublicVariableString);
ExprFuncReturnVal mapState_FuncMapGetPublicVariableString(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchValOut, SA_PARAM_NN_STR const char *pcVarName)
{
	WorldVariable *pVar = mapState_GetPublicVarByName(mapStateClient_Get(), pcVarName);
	if (pVar && pVar->eType == WVAR_STRING) {
		(*ppchValOut) = exprContextAllocString(pContext, pVar->pcStringVal);
	} else {
		(*ppchValOut) = 0;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MapValueGet);
ExprFuncReturnVal mapState_FuncMapValueGet(ACMD_EXPR_INT_OUT piValOut, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState = mapStateClient_Get();
	MultiVal *pmvMapValue = mapState_GetValue(pState, pcValueName);

	if (pmvMapValue) {
		ANALYSIS_ASSUME(pmvMapValue);
		if (MultiValIsNumber(pmvMapValue)) {
			(*piValOut) = MultiValGetInt(pmvMapValue, NULL);
		} else {
			(*piValOut) = 0;
		}
	} else {
		(*piValOut) = 0;
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MapStringGet);
ExprFuncReturnVal mapState_FuncMapStringGet(ACMD_EXPR_STRING_OUT ppcValOut, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState = mapStateClient_Get();
	MultiVal *pmvMapValue = mapState_GetValue(pState, pcValueName);

	if (pmvMapValue) {
		ANALYSIS_ASSUME(pmvMapValue);
		if (MultiValIsString(pmvMapValue)) {
			(*ppcValOut) = MultiValGetString(pmvMapValue, NULL);
		} else {
			(*ppcValOut) = "";
		}
	} else {
		(*ppcValOut) = "";
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsMapPaused);
U32 mapState_FuncIsMapPaused(void)
{
	MapState *pState = mapStateClient_Get();
	return pState ? pState->bPaused : false;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PauseTimeRemaining);
U32 mapState_FuncPauseTimeRemaining(void)
{
	MapState *pState = mapStateClient_Get();
	return pState ? (U32)ceilf(pState->fTimeControlTimer) : 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PauseList);
char *mapState_FuncPauseList(void)
{
	MapState *pState = mapStateClient_Get();
	return pState ? pState->pchTimeControlList : NULL;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetMapDifficulty);
int mapState_FuncGetMapDifficulty(void)
{
	MapState *pState = mapStateClient_Get();
	return mapState_GetDifficulty(pState);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ArePVPQueuesDisabled);
bool mapState_FuncArePVPQueuesDisabled(void)
{
	MapState *pState = mapStateClient_Get();
	return pState->bPVPQueuesDisabled;
}

