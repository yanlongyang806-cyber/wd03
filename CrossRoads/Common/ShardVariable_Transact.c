/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ShardVariable_Transact.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "transactionsystem.h"
#include "WorldVariable.h"

#include "AutoGen/WorldVariable_h_ast.h"
#include "shardVariableCommon.h"
#include "shardVariableCommon_h_ast.h"


extern int g_iShardVarTickCount;

// ----------------------------------------------------------------------------------
// Transaction Functions
// ----------------------------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(pValues, ".Eaworldvars");
bool shardvariable_trh_GetVariableByName(ATH_ARG NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pSearchVar, NOCONST(WorldVariableContainer) **ppResultVar, bool bOverwrite)
{
	NOCONST(WorldVariableContainer) *pVar;
	int i;

	// Return existing value if found
	for(i=eaSize(&pValues->eaWorldVars)-1; i>=0; --i) {
		pVar = pValues->eaWorldVars[i];
		if (pVar->pcName == pSearchVar->pcName) {
			if (bOverwrite) {
				// Clobber old value with the new one
				worldVariableCopyToContainer(pVar, pSearchVar);
			} else if (pVar->eType != pSearchVar->eType) {
				// Type mismatch!
				*ppResultVar = NULL;
				return false;
			}
			*ppResultVar = pVar;
			return true;
		}
	}

	// Create a value if not found
	pVar = StructCreateNoConst(parse_WorldVariableContainer);
	assert(pVar);
	worldVariableCopyToContainer(pVar, pSearchVar);
	eaPush(&pValues->eaWorldVars, pVar);
	*ppResultVar = pVar;
	return true;
}


AUTO_TRANSACTION
ATR_LOCKS(pValues, ".Eaworldvars, .Uclock");
enumTransactionOutcome shardvariable_tr_ClearVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const char *pcName)
{
	NOCONST(WorldVariableContainer) *pVar = NULL;
	int i;

	pcName = allocAddString(pcName);

	for(i=eaSize(&pValues->eaWorldVars)-1; i>=0; --i) {
		pVar = pValues->eaWorldVars[i];
		if (pVar->pcName == pcName) {
			StructDestroyNoConst(parse_WorldVariableContainer, pValues->eaWorldVars[i]);
			eaRemove(&pValues->eaWorldVars, i);
			++pValues->uClock;
			break;
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pValues, ".Eaworldvars, .Uclock");
enumTransactionOutcome shardvariable_tr_ClearAllVariables(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues)
{
	if (eaSize(&pValues->eaWorldVars)) {
		NOCONST(WorldVariableContainer) *pVar = NULL;
		int i;

		for(i=eaSize(&pValues->eaWorldVars)-1; i>=0; --i) {
			StructDestroyNoConst(parse_WorldVariableContainer, pValues->eaWorldVars[i]);
			eaRemove(&pValues->eaWorldVars, i);
		}
		++pValues->uClock;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pValues, ".Uclock, .Eaworldvars");
enumTransactionOutcome shardvariable_tr_SetVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pNewVar)
{
	NOCONST(WorldVariableContainer) *pVar = NULL;

	// Sets the value as requested
	if (!shardvariable_trh_GetVariableByName(pValues, pNewVar, &pVar, true)) {
		return TRANSACTION_OUTCOME_FAILURE;
	} else {
		++pValues->uClock;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}


AUTO_TRANSACTION
ATR_LOCKS(pValues, ".Uclock, .Eaworldvars");
enumTransactionOutcome shardvariable_tr_IncrementIntVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pDefaultVar, int iValue)
{
	NOCONST(WorldVariableContainer) *pVar = NULL;

	if (pDefaultVar->eType != WVAR_INT) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Gets the variable
	if (!shardvariable_trh_GetVariableByName(pValues, pDefaultVar, &pVar, false)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Increments the value
	pVar->iIntVal += iValue;
	++pValues->uClock;

	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pValues, ".Uclock, .Eaworldvars");
enumTransactionOutcome shardvariable_tr_IncrementFloatVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pDefaultVar, F32 fValue)
{
	NOCONST(WorldVariableContainer) *pVar = NULL;

	if (pDefaultVar->eType != WVAR_FLOAT) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Gets the variable
	if (!shardvariable_trh_GetVariableByName(pValues, pDefaultVar, &pVar, false)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Increments the value
	pVar->fFloatVal += fValue;
	++pValues->uClock;

	return TRANSACTION_OUTCOME_SUCCESS;
}