/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef SERVERTRADE_H_
#define SERVERTRADE_H_

#include "GlobalTypeEnum.h"

typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct TradeBag TradeBag;
typedef struct TransactionReturnVal TransactionReturnVal;

typedef struct NOCONST(Entity) NOCONST(Entity);

// Trade server commands
void trade_AddTradeItem(SA_PARAM_NN_VALID Entity* pEnt, S32 SrcBagId, S32 iSrcSlot, S32 iTargetSlot, int count);
void trade_AddTradeItemFromEnt(SA_PARAM_NN_VALID Entity* pEnt, S32 iSrcType, U32 iSrcID, S32 SrcBagId, S32 iSrcSlot, S32 iTargetSlot, int count);
void trade_AddTradeNumericItem(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pItemDefName, int count);
void trade_AddTradeNumericItemFromEnt(SA_PARAM_NN_VALID Entity* pEnt, S32 iSrcType, U32 iSrcID, SA_PARAM_NN_STR const char* pItemDefName, int count);
void trade_RemoveTradeItem(SA_PARAM_NN_VALID Entity* pEnt, S32 iSlot);
void trade_RemoveTradeNumericItem(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pItemDefName, int count);
void trade_RequestTrade(SA_PARAM_NN_VALID Entity* pPlayerEnt, EntityRef erTargetEntRef);
void trade_Cancel(SA_PARAM_NN_VALID Entity* pPlayerEnt);
void trade_Accept(SA_PARAM_NN_VALID Entity* pPlayerEnt);

// Trade server command helpers
void trade_ClearTradeItems(SA_PARAM_NN_VALID Entity* pEnt);
void trade_ClearAcceptance(SA_PARAM_NN_VALID Entity* pPlayerEnt);

// Callback function for trade transactions
static void trade_AcceptTrade_Callback(TransactionReturnVal* returnVal, void* pvUserData);

bool trade_trh_IsValid(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt1, ATH_ARG NOCONST(Entity) *pEnt2, TradeBag *pTrade1, TradeBag *pTrade2);
bool trade_trh_OfferIsValid(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt1, ATH_ARG NOCONST(Entity) *pEnt2, TradeBag *pTrade, GameAccountDataExtract *pExtract1, GameAccountDataExtract *pExtract2);

#endif
