/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "estring.h"
#include "Expression.h"
#include "gslMapVariable.h"
#include "gslPartition.h"
#include "gslWorldVariable.h"
#include "worldgrid.h"


// ----------------------------------------------------------------------------------
// Getting Map Variables
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetMapVariableString_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Gets the value of a map variable as a string
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableString);
ExprFuncReturnVal exprFuncGetMapVariableString(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		static char *estr = NULL;
		worldVariableToEString(pMapVar->pVariable, &estr);
		*ppcRet = exprContextAllocString(pContext, estr);
		return ExprFuncReturnFinished;
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableStringWithDefault);
ExprFuncReturnVal exprFuncGetMapVariableStringWithDefault(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, const char *pcDefault, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);
	static char *estr = NULL;

	if (pMapVar) {
		worldVariableToEString(pMapVar->pVariable, &estr);
	} else if (pcDefault) {
		estrPrintf(&estr, "%s", pcDefault);
	}
	*ppcRet = exprContextAllocString(pContext, estr);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetMapVariableInt_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} else if (pMapVar->pDef->eType != WVAR_INT) {
		estrPrintf(errString, "Map variable named '%s' is not an integer value", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Gets the value of a map variable as an integer
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableInt);
ExprFuncReturnVal exprFuncGetMapVariableInt(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_INT) {
			*piRet = pMapVar->pVariable->iIntVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not an integer value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableIntWithDefault);
ExprFuncReturnVal exprFuncGetMapVariableIntWithDefault(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, int iDefault, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_INT) {
			*piRet = pMapVar->pVariable->iIntVal;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not an integer value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		*piRet = iDefault;
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetMapVariableFloat_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} else if (pMapVar->pDef->eType != WVAR_FLOAT) {
		estrPrintf(errString, "Map variable named '%s' is not a float value", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Gets the value of a map variable as a float
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableFloat);
ExprFuncReturnVal exprFuncGetMapVariableFloat(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_FLOAT) {
			*pfRet = pMapVar->pVariable->fFloatVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableFloatWithDefault);
ExprFuncReturnVal exprFuncGetMapVariableFloatWithDefault(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, float fDefault, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_FLOAT) {
			*pfRet = pMapVar->pVariable->fFloatVal;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		*pfRet = fDefault;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncGetMapVariableMessage_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppchRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} else if (pMapVar->pDef->eType != WVAR_MESSAGE) {
		estrPrintf(errString, "Map variable named '%s' is not of type 'Message'", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Gets the value of a map variable as a message key
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapVariableMessage);
ExprFuncReturnVal exprFuncGetMapVariableMessage(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppchRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_MESSAGE) {
			*ppchRet = REF_STRING_FROM_HANDLE(pMapVar->pVariable->messageVal.hMessage);
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not of type 'Message'", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


// ----------------------------------------------------------------------------------
// Setting Map Variables
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSetMapVariableString_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, const char *pcValue, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} 

	return ExprFuncReturnFinished;
}


// Sets the value of a map variable as a string
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(SetMapVariableString);
ExprFuncReturnVal exprFuncSetMapVariableString(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, const char *pcValue, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (worldVariableFromString(pMapVar->pVariable, pcValue, errString)) {
			mapVariable_NotifyModified(iPartitionIdx, pMapVar);
			return ExprFuncReturnFinished;
		} else {
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSetMapVariableInt_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, int iValue, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} else if (pMapVar->pDef->eType != WVAR_INT) {
		estrPrintf(errString, "Map variable named '%s' is not an integer value", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Sets the value of a map variable as an integer
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(SetMapVariableInt);
ExprFuncReturnVal exprFuncSetMapVariableInt(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, int iValue, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_INT) {
			pMapVar->pVariable->iIntVal = iValue;
			mapVariable_NotifyModified(iPartitionIdx, pMapVar);
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not an integer value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSetMapVariableFloat_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, float fValue, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} else if (pMapVar->pDef->eType != WVAR_FLOAT) {
		estrPrintf(errString, "Map variable named '%s' is not an float value", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Sets the value of a map variable as a float
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(SetMapVariableFloat);
ExprFuncReturnVal exprFuncSetMapVariableFloat(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, float fValue, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_FLOAT) {
			pMapVar->pVariable->fFloatVal = fValue;
			mapVariable_NotifyModified(iPartitionIdx, pMapVar);
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSetMapVariableMessage_Check(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, const char* pchMessageKey, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (!pMapVar) {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	} else if (pMapVar->pDef->eType != WVAR_MESSAGE) {
		estrPrintf(errString, "Map variable named '%s' is not a message", pcVarName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Sets the value of a map variable as a message
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(SetMapVariableMessage);
ExprFuncReturnVal exprFuncSetMapVariableMessage(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcVarName, const char* pchMessageKey, ACMD_EXPR_ERRSTRING errString)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, pcVarName);

	if (pMapVar) {
		if (pMapVar->pDef->eType == WVAR_MESSAGE) {
			SET_HANDLE_FROM_STRING("Message", pchMessageKey, pMapVar->pVariable->messageVal.hMessage);
			mapVariable_NotifyModified(iPartitionIdx, pMapVar);
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Map variable named '%s' is not a message", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No map variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}



