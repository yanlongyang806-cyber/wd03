/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "estring.h"
#include "Expression.h"
#include "gslShardVariable.h"
#include "worldgrid.h"
#include "gslWorldVariable.h"
#include "ShardVariableCommon.h"

// ----------------------------------------------------------------------------------
// Getting Shard Variables
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetShardVariableString_Check(ExprContext *pContext, ACMD_EXPR_STRING_OUT ret, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (!pShardVar) {
		shardvariable_ErrorOnNotFound(pcVarName, errString);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Gets the value of a shard variable as a string
AUTO_EXPR_FUNC(util) ACMD_NAME(GetShardVariableString);
ExprFuncReturnVal exprFuncGetShardVariableString(ExprContext *pContext, ACMD_EXPR_STRING_OUT ret, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (pShardVar) {
		static char *estr = NULL;
		worldVariableToEString(pShardVar->pVariable, &estr);
		*ret = exprContextAllocString(pContext, estr);
		return ExprFuncReturnFinished;
	} else {
		shardvariable_ErrorOnNotFound(pcVarName, errString);
		return ExprFuncReturnError;
	}
}

AUTO_EXPR_FUNC(util) ACMD_NAME(GetShardVariableStringWithDefault);
ExprFuncReturnVal exprFuncGetShardVariableStringWithDefault(ExprContext *pContext, ACMD_EXPR_STRING_OUT ret, const char *pcVarName, const char *pcDefault, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);
	static char *estr = NULL;

	if (pShardVar) {
		worldVariableToEString(pShardVar->pVariable, &estr);
	} else if (pcDefault) {
		estrPrintf(&estr, "%s", pcDefault);
	}
	*ret = exprContextAllocString(pContext, estr);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetShardVariableInt_Check(ExprContext *pContext, ACMD_EXPR_INT_OUT ret, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (!pShardVar) {
		shardvariable_ErrorOnNotFound(pcVarName, errString);
		return ExprFuncReturnError;
	} else if (pShardVar->pDefault->eType != WVAR_INT) {
		estrPrintf(errString, "Shard variable named '%s' is not an integer value", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Gets the value of a shard variable as an integer
AUTO_EXPR_FUNC(util) ACMD_NAME(GetShardVariableInt);
ExprFuncReturnVal exprFuncGetShardVariableInt(ExprContext *pContext, ACMD_EXPR_INT_OUT ret, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (pShardVar) {
		if (pShardVar->pDefault->eType == WVAR_INT) {
			*ret = pShardVar->pVariable->iIntVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Shard variable named '%s' is not an int value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		shardvariable_ErrorOnNotFound(pcVarName, errString);
		return ExprFuncReturnError;
	}
}

AUTO_EXPR_FUNC(util) ACMD_NAME(GetShardVariableIntWithDefault);
ExprFuncReturnVal exprFuncGetShardVariableIntWithDefault(ExprContext *pContext, ACMD_EXPR_INT_OUT ret, const char *pcVarName, int iDefault, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (pShardVar) {
		if (pShardVar->pDefault->eType == WVAR_INT) {
			*ret = pShardVar->pVariable->iIntVal;
		} else {
			estrPrintf(errString, "Shard variable named '%s' is not an integer value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		*ret = iDefault;
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetShardVariableFloat_Check(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT ret, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (!pShardVar) {
		shardvariable_ErrorOnNotFound(pcVarName, errString);
		return ExprFuncReturnError;
	} else if (pShardVar->pDefault->eType != WVAR_FLOAT) {
		estrPrintf(errString, "Shard variable named '%s' is not a float value", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Gets the value of a shard variable as a float
AUTO_EXPR_FUNC(util) ACMD_NAME(GetShardVariableFloat);
ExprFuncReturnVal exprFuncGetShardVariableFloat(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT ret, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (pShardVar) {
		if (pShardVar->pDefault->eType == WVAR_FLOAT) {
			*ret = pShardVar->pVariable->fFloatVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Shard variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		shardvariable_ErrorOnNotFound(pcVarName, errString);
		return ExprFuncReturnError;
	}
}

AUTO_EXPR_FUNC(util) ACMD_NAME(GetShardVariableFloatWithDefault);
ExprFuncReturnVal exprFuncGetShardVariableFloatWithDefault(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT ret, const char *pcVarName, float fDefault, ACMD_EXPR_ERRSTRING errString)
{
	ShardVariable *pShardVar = shardvariable_GetByName(pcVarName);

	if (pShardVar) {
		if (pShardVar->pDefault->eType == WVAR_FLOAT) {
			*ret = pShardVar->pVariable->fFloatVal;
		} else {
			estrPrintf(errString, "Shard variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		*ret = fDefault;
	}
	return ExprFuncReturnFinished;
}

// Sets a shard variable
AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(SetShardVariable);
ExprFuncReturnVal shardvariable_ExprSet(const char *pcName, const char *pcValue, ACMD_EXPR_ERRSTRING errString)
{
	static char *estrBuf = NULL;
	ShardVariable *pShardVar;

	estrClear(&estrBuf);
	pShardVar = shardvariable_GetByName(pcName);
	if (pShardVar) {
		WorldVariable *pWorldVar = StructClone(parse_WorldVariable, pShardVar->pVariable);
		assert(pWorldVar);
		worldVariableFromString(pWorldVar, pcValue, &estrBuf);
		shardvariable_SetVariable(pWorldVar, &estrBuf);
		StructDestroy(parse_WorldVariable, pWorldVar);
	} else {
		shardvariable_ErrorOnNotFound(pcName, errString);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Increment an integer shard variable
AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(IncrementShardVariableInt);
ExprFuncReturnVal shardvariable_ExprIncrementInt(const char *pcName, int iValue, ACMD_EXPR_ERRSTRING errString)
{
	if (!shardvariable_IncrementIntVariable(pcName, iValue, errString))
	{
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Increment a float shard variable
AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(IncrementShardVariableFloat);
ExprFuncReturnVal shardvariable_ExprIncrementFloat(const char *pcName, F32 fValue, ACMD_EXPR_ERRSTRING errString)
{
	if (!shardvariable_IncrementFloatVariable(pcName, fValue, errString))
	{
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

// Reset a shard variable
AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(ResetShardVariable);
ExprFuncReturnVal shardvariable_ExprReset(char *pcName, ACMD_EXPR_ERRSTRING errString)
{
	if (!shardvariable_ResetVariable(pcName, errString))
	{
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}
