/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclCurrencyExchange.h"
#include "CurrencyExchangeCommon.h"
#include "timing.h"
#include "UIGen.h"
#include "Entity.h"
#include "Player.h"
#include "gclEntity.h"

#include "AutoGen/CurrencyExchangeCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("CurrencyExchangeGlobalUIData", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("CurrencyExchangePriceQuantity", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("CurrencyExchangeAccountData", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("CurrencyExchangeOpenOrder", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("CurrencyExchangeLogEntry", BUDGET_UISystem););

#define UIDATA_STALE_TIME_SECONDS 5
#define UIDATA_REQUEST_INTERVAL_SECONDS 5

static U32 sLastUIDataRequest = 0;
static U32 sLastUIDataUpdate = 0;
static CurrencyExchangeGlobalUIData sGlobalUIData = {0};

static void
RefreshCurrencyExchangeUIData(void)
{
	U32 curTime = timeSecondsSince2000();

	// If we have not asked for the global ui data in a while, ask again to refresh our subscription.
	if ( ( sLastUIDataRequest + UIDATA_REQUEST_INTERVAL_SECONDS ) < curTime )
	{
		ServerCmd_gslCurrencyExchange_RequestUIData();
		sLastUIDataRequest = curTime;
	}
}

static bool
GlobalUIDataCurrent(void)
{
	return ( ( sLastUIDataUpdate + UIDATA_STALE_TIME_SECONDS ) >= timeSecondsSince2000() );
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(CurrencyExchange);
void 
gclCurrencyExchange_ReturnGlobalUIData(CurrencyExchangeGlobalUIData *uiData)
{
	StructCopy(parse_CurrencyExchangeGlobalUIData, uiData, &sGlobalUIData, 0, 0, 0);
	sLastUIDataUpdate = timeSecondsSince2000();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetReadyToClaimMTCKey);
const char *
exprCurrencyExchange_GetReadyToClaimMTCKey(void)
{
	return(CurrencyExchangeConfig_GetMtcReadyToClaimEscrowAccountKey());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetReadyToClaimTC);
int 
exprCurrencyExchange_GetReadyToClaimTC(void)
{
	Entity *pEnt = entActivePlayerPtr();
	CurrencyExchangeAccountData *accountData;

	RefreshCurrencyExchangeUIData();

	if ( pEnt == NULL || pEnt->pPlayer == NULL )
	{
		return 0;
	}

	accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
	if ( accountData == NULL )
	{
		return 0;
	}

	return accountData->readyToClaimEscrowTC;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetHistoryList);
void 
exprCurrencyExchange_GetHistoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static ContainerID sLastAccountID = 0;
	static U32 sLastLogNext = U32_MAX;
	static EARRAY_OF(CurrencyExchangeLogEntry) sLogEntries = NULL;

	Entity *pEnt = entActivePlayerPtr();

	RefreshCurrencyExchangeUIData();

	if ( pEnt == NULL || pEnt->pPlayer == NULL )
	{
		// No entity, so clear the list.
		eaClearStruct(&sLogEntries, parse_CurrencyExchangeLogEntry);
	}
	else
	{
		CurrencyExchangeAccountData *accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
		if ( ( accountData == NULL ) || ( eaSize(&accountData->logEntries) == 0 ) )
		{
			// No account data, or empty account log, so clear the list.
			eaClearStruct(&sLogEntries, parse_CurrencyExchangeLogEntry);
		}
		else
		{
			// If the player is different or the log has changed, then rebuild the list.
			if ( ( pEnt->pPlayer->accountID != sLastAccountID ) || ( accountData->logNext != sLastLogNext ) )
			{
				int srci;
				int desti;
				int logSize = eaSize(&accountData->logEntries);

				sLastAccountID = pEnt->pPlayer->accountID;
				sLastLogNext = accountData->logNext;

				// Grow the array if necessary
				while ( eaSize(&sLogEntries) < logSize )
				{
					CurrencyExchangeLogEntry *entry = StructCreate(parse_CurrencyExchangeLogEntry);
					eaPush(&sLogEntries, entry);
				}

				// Shrink the array if it is too big
				while ( eaSize(&sLogEntries) > logSize )
				{
					StructDestroy(parse_CurrencyExchangeLogEntry, eaPop(&sLogEntries));
				}

				// logNext should point to the first chronological log entry, or past the end of the log if the log is not yet full size.
				// Initialize srci to one spot before, so that we can put the srci increment at the front of the loop and avoid
				//  duplication of the wrapping logic.
				srci = accountData->logNext - 1;

				// fill the list from the end (with the oldest) to the beginning (with the newest)
				for ( desti = logSize - 1; desti >= 0; desti-- )
				{
					srci++;
					if ( srci >= logSize )
					{
						srci = 0;
					}

					StructCopy(parse_CurrencyExchangeLogEntry, accountData->logEntries[srci], sLogEntries[desti], 0, 0, 0);
				}
			}
		}
	}
	ui_GenSetList(pGen, &sLogEntries, parse_CurrencyExchangeLogEntry);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetOpenOrderAmount);
S32 
exprCurrencyExchange_GetOpenOrderAmount(S32 orderType, S32 orderPrice)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 iTotal = 0;

	RefreshCurrencyExchangeUIData();

	if ( pEnt && pEnt->pPlayer )
	{
		CurrencyExchangeAccountData *accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
		int i;

		for ( i = 0; i < eaSize(&accountData->openOrders); i++ )
		{
			if ( ( accountData->openOrders[i]->orderType == orderType ) && ( accountData->openOrders[i]->price == (U32)orderPrice ) )
			{
				iTotal += accountData->openOrders[i]->quantity;
			}
		}
	}

	return iTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetOpenOrderConsumedTC);
S32 
exprCurrencyExchange_GetOpenOrderConsumedTC(S32 orderType, S32 orderPrice)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 iTotal = 0;

	RefreshCurrencyExchangeUIData();

	if ( pEnt && pEnt->pPlayer )
	{
		CurrencyExchangeAccountData *accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
		int i;

		for ( i = 0; i < eaSize(&accountData->openOrders); i++ )
		{
			if ( ( accountData->openOrders[i]->orderType == orderType ) && ( accountData->openOrders[i]->price == (U32)orderPrice ) )
			{
				iTotal += accountData->openOrders[i]->consumedTC;
			}
		}
	}

	return iTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetOpenOrderConsumedMTC);
S32 
exprCurrencyExchange_GetOpenOrderConsumedMTC(S32 orderType, S32 orderPrice)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 iTotal = 0;

	RefreshCurrencyExchangeUIData();

	if ( pEnt && pEnt->pPlayer )
	{
		CurrencyExchangeAccountData *accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
		int i;

		for ( i = 0; i < eaSize(&accountData->openOrders); i++ )
		{
			if ( ( accountData->openOrders[i]->orderType == orderType ) && ( accountData->openOrders[i]->price == (U32)orderPrice ) )
			{
				iTotal += accountData->openOrders[i]->consumedMTC;
			}
		}
	}

	return iTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetOrderList);
void 
exprCurrencyExchange_GetOrderList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static EARRAY_OF(CurrencyExchangeOpenOrder) sOpenOrders = NULL;

	Entity *pEnt = entActivePlayerPtr();

	RefreshCurrencyExchangeUIData();

	if ( pEnt == NULL || pEnt->pPlayer == NULL )
	{
		// No entity, so clear the list.
		eaClearStruct(&sOpenOrders, parse_CurrencyExchangeOpenOrder);
	}
	else
	{
		CurrencyExchangeAccountData *accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
		if ( ( accountData == NULL ) || ( eaSize(&accountData->openOrders) == 0 ) )
		{
			// No account data, or no open orders, so clear the list.
			eaClearStruct(&sOpenOrders, parse_CurrencyExchangeOpenOrder);
		}
		else
		{
			int numOrders = 0;
			int i;

			// fill the list from the end (with the oldest) to the beginning (with the newest)
			for ( i = 0; i < eaSize(&accountData->openOrders); i++ )
			{
				if ( ( accountData->openOrders[i]->orderType == OrderType_Buy ) || ( accountData->openOrders[i]->orderType == OrderType_Sell ) )
				{
					// Grow the array if necessary
					while ( eaSize(&sOpenOrders) < ( numOrders + 1 ) )
					{
						CurrencyExchangeOpenOrder *entry = StructCreate(parse_CurrencyExchangeOpenOrder);
						eaPush(&sOpenOrders, entry);
					}
					StructCopy(parse_CurrencyExchangeOpenOrder, accountData->openOrders[i], sOpenOrders[numOrders], 0, 0, 0);

					numOrders++;
				}

			}

			// Shrink the array if it is too big
			while ( eaSize(&sOpenOrders) > numOrders )
			{
				StructDestroy(parse_CurrencyExchangeOpenOrder, eaPop(&sOpenOrders));
			}
		}
	}
	ui_GenSetList(pGen, &sOpenOrders, parse_CurrencyExchangeOpenOrder);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetNumOrders);
int 
exprCurrencyExchange_GetNumOrders(SA_PARAM_NN_VALID UIGen *pGen)
{
    Entity *pEnt = entActivePlayerPtr();

    RefreshCurrencyExchangeUIData();

    if ( pEnt == NULL || pEnt->pPlayer == NULL )
    {
        return 0;
    }
    else
    {
        CurrencyExchangeAccountData *accountData = GET_REF(pEnt->pPlayer->hCurrencyExchangeAccountData);
        if ( accountData == NULL )
        {
            return 0;
        }
        
        return eaSize(&accountData->openOrders);
    }
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetPriceList);
void 
exprCurrencyExchange_GetPriceList(SA_PARAM_NN_VALID UIGen *pGen, int orderType)
{
	static EARRAY_OF(CurrencyExchangePriceQuantity) sBuyPriceList = NULL;
	static EARRAY_OF(CurrencyExchangePriceQuantity) sSellPriceList = NULL;
	EARRAY_OF(CurrencyExchangePriceQuantity) srcPriceList = NULL;
	EARRAY_OF(CurrencyExchangePriceQuantity) *destPriceListHandle = NULL;


	RefreshCurrencyExchangeUIData();

	if ( orderType == OrderType_Buy )
	{
		srcPriceList = sGlobalUIData.buyPrices;
		destPriceListHandle = &sBuyPriceList;
	}
	else
	{
		srcPriceList = sGlobalUIData.sellPrices;
		destPriceListHandle = &sSellPriceList;
	}

	if ( GlobalUIDataCurrent() )
	{
		int listSize = eaSize(&srcPriceList);
		int i;

		// Grow the array if necessary
		while ( eaSize(destPriceListHandle) < listSize )
		{
			CurrencyExchangePriceQuantity *price = StructCreate(parse_CurrencyExchangePriceQuantity);
			eaPush(destPriceListHandle, price);
		}

		// Shrink the array if it is too big
		while ( eaSize(destPriceListHandle) > listSize )
		{
			StructDestroy(parse_CurrencyExchangePriceQuantity, eaPop(destPriceListHandle));
		}

		// fill the list from the end (with the oldest) to the beginning (with the newest)
		for ( i = 0; i < listSize; i++ )
		{
			StructCopy(parse_CurrencyExchangePriceQuantity, srcPriceList[i], (*destPriceListHandle)[i], 0, 0, 0);
		}
	}
	else
	{
		// If the global ui data is out of date then clear the list.
		eaClearStruct(destPriceListHandle, parse_CurrencyExchangePriceQuantity);
	}

	ui_GenSetList(pGen, destPriceListHandle, parse_CurrencyExchangePriceQuantity);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetStatus);
S32 
exprCurrencyExchange_GetStatus(void)
{
	if (sGlobalUIData.enabled)
		return 1;
	if (!sLastUIDataUpdate)
		return -1;
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetMinimumPrice);
S32 
exprCurrencyExchange_GetMinimumPrice(void)
{
	if (!sLastUIDataUpdate)
		return -1;
	return sGlobalUIData.minMTCPrice;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetMaximumPrice);
S32 
exprCurrencyExchange_GetMaximumPrice(void)
{
	if (!sLastUIDataUpdate)
		return -1;
	return sGlobalUIData.maxMTCPrice;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetMinimumQuantity);
S32 
exprCurrencyExchange_GetMinimumQuantity(void)
{
	if (!sLastUIDataUpdate)
		return -1;
	return sGlobalUIData.minQuantityPerOrder;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetMaximumQuantity);
S32 
exprCurrencyExchange_GetMaximumQuantity(void)
{
	if (!sLastUIDataUpdate)
		return -1;
	return sGlobalUIData.maxQuantityPerOrder;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrencyExchange_GetMaxiumOrders, CurrencyExchange_GetMaximumOrders);
S32 
exprCurrencyExchange_GetMaxiumOrders(void)
{
	if (!sLastUIDataUpdate)
		return -1;
	return sGlobalUIData.maxPlayerOpenOrders;
}

AUTO_RUN_LATE;
void
gclCurrencyExchange_Init(void)
{
	ui_GenInitStaticDefineVars(CurrencyExchangeOperationTypeEnum, "CurrencyOpType_");
	ui_GenInitStaticDefineVars(CurrencyExchangeOrderTypeEnum, "OrderType_");
}