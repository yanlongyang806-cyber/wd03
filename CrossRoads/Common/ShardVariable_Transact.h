/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once

typedef struct NOCONST(ShardVariableContainer) NOCONST(ShardVariableContainer);
typedef struct WorldVariable WorldVariable;
typedef enum enumTransactionOutcome enumTransactionOutcome;

void shardvariable_CreateContainer(void);

// ----------------------------------------------------------------------------
//  Transactions
// ----------------------------------------------------------------------------

enumTransactionOutcome shardvariable_tr_ClearVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const char *pcName);
enumTransactionOutcome shardvariable_tr_SetVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pNewVar);
enumTransactionOutcome shardvariable_tr_IncrementIntVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pDefaultVar, int iValue);
enumTransactionOutcome shardvariable_tr_IncrementFloatVariable(ATR_ARGS, NOCONST(ShardVariableContainer) *pValues, const WorldVariable *pDefaultVar, F32 fValue);