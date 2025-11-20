/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslTriggerCondition.h"

#include "EString.h"
#include "Expression.h"


// ----------------------------------------------------------------------------------
// Expression Functions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncTriggerConditionState_Check(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, const char *pcCondName, ACMD_EXPR_ERRSTRING errString)
{
	WorldScope *pScope = exprContextGetScope(pContext);

	if (!pcCondName) {
		estrPrintf(errString, "No condition name specified.");
		return ExprFuncReturnError;
	}
	if (!triggercondition_GetByName(pcCondName, pScope)) {
		estrPrintf(errString, "Cannot find condition \"%s\".", pcCondName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


// Gets the state of a trigger condition on an encounter layer
AUTO_EXPR_FUNC(encounter_action, ai, clickable) ACMD_NAME(TriggerConditionState);
ExprFuncReturnVal exprFuncTriggerConditionState(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piRet, const char *pcCondName, ACMD_EXPR_ERRSTRING errString)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	
	if (pcCondName) {
		GameTriggerCondition *pTrigger = triggercondition_GetByName(pcCondName, pScope);

		// Throw an error if the condition doesn't exist.  May someday want a "No-error" version of this
		if (pTrigger) {
			*piRet = triggercondition_GetState(iPartitionIdx, pTrigger);
		} else {
			*piRet = false;
			return ExprFuncReturnFinished;
		}
	} else {
		estrPrintf(errString, "No trigger condition name provided");
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}
