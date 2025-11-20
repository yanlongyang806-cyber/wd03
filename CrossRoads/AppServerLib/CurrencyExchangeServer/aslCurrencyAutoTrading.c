/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslCurrencyAutoTrading.h"
#include "aslCurrencyExchange.h"
#include "objContainer.h"

// This setting is used to enable the automated selling of Time Currency on the currency exchange.
bool gEnableCurrencyAutoTrading = false;
AUTO_CMD_INT(gEnableCurrencyAutoTrading, EnableCurrencyAutoTrading) ACMD_AUTO_SETTING(CurrencyExchange, CURRENCYEXCHANGESERVER);

// Which account ID to use for auto trading.  This account will accumulate zen in it's ready to claim escrow bucket key/value.
U32 gAutoTradingAccountID = 0;
AUTO_CMD_INT(gAutoTradingAccountID, AutoTradingAccountID) ACMD_AUTO_SETTING(CurrencyExchange, CURRENCYEXCHANGESERVER);

bool
aslCurrencyExchange_IsAutoTradingEnabled(void)
{
    return ( CurrencyExchangeIsEnabled() && gEnableCurrencyAutoTrading && ( gAutoTradingAccountID != 0 ) );
}

static CurrencyExchangeAccountData *
GetAutoTradeAccountContainer(void)
{
    Container *container;
    if ( aslCurrencyExchange_IsAutoTradingEnabled() )
    {
        container = objGetContainer(GLOBALTYPE_CURRENCYEXCHANGE, gAutoTradingAccountID);
        if ( container )
        {
            return container->containerData;
        }
    }

    return NULL;
}

U32
aslCurrencyExchange_NumAutoTradeOrders(void)
{
    int numOrders = 0;
    CurrencyExchangeAccountData *currencyAccountData;
    int i;

    currencyAccountData = GetAutoTradeAccountContainer();
    if ( currencyAccountData )
    {
        for ( i = eaSize(&currencyAccountData->openOrders) - 1; i >= 0; i-- )
        {
            CurrencyExchangeOpenOrder *openOrder = currencyAccountData->openOrders[i];
            // Record the IDs of all open orders.
            if ( openOrder->orderType != OrderType_None )
            {
                numOrders++;
            }
        }
    }

    return numOrders;
}

void
aslCurrencyExchange_WithdrawAllAutoTradeOrders(void)
{
    CurrencyExchangeAccountData *currencyAccountData;
    int i;
    static U32 *orderIDs = NULL;

    ea32ClearFast(&orderIDs);

    currencyAccountData = GetAutoTradeAccountContainer();
    if ( currencyAccountData )
    {
        for ( i = eaSize(&currencyAccountData->openOrders) - 1; i >= 0; i-- )
        {
            CurrencyExchangeOpenOrder *openOrder = currencyAccountData->openOrders[i];
            // Record the IDs of all open orders.
            if ( openOrder->orderType != OrderType_None )
            {
                ea32Push(&orderIDs, openOrder->orderID);
            }
        }

        // Submit requests to withdraw the orders.
        for ( i = ea32Size(&orderIDs) - 1; i >= 0; i-- )
        {
            aslCurrencyExchange_WithdrawOrder(gAutoTradingAccountID, 1, "Automated Trader", orderIDs[i], "Automated Trader", "");
        }
    }
}