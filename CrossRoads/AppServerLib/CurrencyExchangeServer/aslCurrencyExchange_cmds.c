/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "aslCurrencyExchange.h"
#include "textparser.h"
#include "timing.h"
#include "AccountProxyCommon.h"

#include "AutoGen/aslCurrencyExchange_h_ast.h"
#include "AutoGen/CurrencyExchangeCommon_h_ast.h"

static CurrencyExchangeOperationRequest *
NewOperationRequest(CurrencyExchangeOperationType operationType, ContainerID accountID, ContainerID characterID, const char *charName, const char *debugName, const char *gameSpecificLogInfo)
{
    CurrencyExchangeOperationRequest *operation = StructCreate(parse_CurrencyExchangeOperationRequest);

    operation->operationType = operationType;
    operation->accountID = accountID;
    operation->characterID = characterID;
    operation->characterName = StructAllocString(charName);
    operation->arrivalTime = timeSecondsSince2000();
    operation->debugName = StructAllocString(debugName);
    operation->gameSpecificLogInfo = StructAllocString(gameSpecificLogInfo);
    return(operation);
}

void
aslCurrencyExchange_CreateOrderEx(ContainerID accountID, ContainerID characterID, const char *charName, CurrencyExchangeOrderType orderType, U32 quantity, U32 price, const char *debugName, const char *gameSpecificLogInfo, bool skipQuantityCheck)
{
    CurrencyExchangeOperationRequest *operation = NewOperationRequest(CurrencyOpType_CreateOrder, accountID, characterID, charName, debugName, gameSpecificLogInfo);

    operation->orderType = orderType;
    operation->quantity = quantity;
    operation->price = price;

    // Validate order quantity.
    if ( !skipQuantityCheck && ( ( quantity < gCurrencyExchangeConfig.minQuantityPerOrder) || ( quantity > gCurrencyExchangeConfig.maxQuantityPerOrder ) ) )
    {
        CurrencyExchangeOperationNotify(operation, CurrencyResultType_QuantityOutOfRange, "Received order with out of range quantity", false, true, true);
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

    // Validate order price.
    if ( ( price < gCurrencyExchangeConfig.minMTCPrice)  || ( price > gCurrencyExchangeConfig.maxMTCPrice ) )
    {
        CurrencyExchangeOperationNotify(operation, CurrencyResultType_PriceOutOfRange, "Received order with out of range price", false, true, true);
        StructDestroy(parse_CurrencyExchangeOperationRequest, operation);
        return;
    }

    CurrencyExchangeReceiveOperation(operation, false, false);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_CATEGORY(CurrencyExchange);
void
aslCurrencyExchange_CreateOrder(ContainerID accountID, ContainerID characterID, const char *charName, CurrencyExchangeOrderType orderType, U32 quantity, U32 price, const char *debugName, const char *gameSpecificLogInfo)
{
    aslCurrencyExchange_CreateOrderEx(accountID, characterID, charName, orderType, quantity, price, debugName, gameSpecificLogInfo, false);
}

// AL9 debug command for creating a sell order from the server monitor.
AUTO_COMMAND;
void
aslCurrencyExchange_CreateSellOrderDebug(ContainerID accountID, ContainerID characterID, const char *charName, U32 quantity, U32 price, const char *debugName)
{
    aslCurrencyExchange_CreateOrderEx(accountID, characterID, charName, OrderType_Sell, quantity, price, debugName, "", true);
}

// AL9 debug command for creating a buy order from the server monitor.
AUTO_COMMAND;
void
aslCurrencyExchange_CreateBuyOrderDebug(ContainerID accountID, ContainerID characterID, const char *charName, U32 quantity, U32 price, const char *debugName)
{
    aslCurrencyExchange_CreateOrderEx(accountID, characterID, charName, OrderType_Buy, quantity, price, debugName, "", true);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_CATEGORY(CurrencyExchange);
void
aslCurrencyExchange_WithdrawOrder(ContainerID accountID, ContainerID characterID, const char *charName, U32 orderID, const char *debugName, const char *gameSpecificLogInfo)
{
    CurrencyExchangeOperationRequest *operation = NewOperationRequest(CurrencyOpType_WithdrawOrder, accountID, characterID, charName, debugName, gameSpecificLogInfo);

    operation->orderID = orderID;

    CurrencyExchangeReceiveOperation(operation, false, false);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_CATEGORY(CurrencyExchange);
void
aslCurrencyExchange_ClaimMTC(ContainerID accountID, ContainerID characterID, const char *charName, U32 quantity, const char *debugName, const char *gameSpecificLogInfo)
{
    CurrencyExchangeOperationRequest *operation = NewOperationRequest(CurrencyOpType_ClaimMTC, accountID, characterID, charName, debugName, gameSpecificLogInfo);

    operation->quantity = quantity;

    CurrencyExchangeReceiveOperation(operation, false, false);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_CATEGORY(CurrencyExchange);
void
aslCurrencyExchange_ClaimTC(ContainerID accountID, ContainerID characterID, const char *charName, U32 quantity, const char *debugName, const char *gameSpecificLogInfo)
{
    CurrencyExchangeOperationRequest *operation = NewOperationRequest(CurrencyOpType_ClaimTC, accountID, characterID, charName, debugName, gameSpecificLogInfo);

    operation->quantity = quantity;

    CurrencyExchangeReceiveOperation(operation, false, false);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_CATEGORY(CurrencyExchange);
void
aslCurrencyExchange_RequestGlobalUIData(GlobalType serverType, ContainerID serverID)
{
	CurrencyExchange_AddSubscriber(serverType, serverID);
}

AUTO_COMMAND ACMD_NAME(CurrencyExchange_DumpOrderInfos) ACMD_CATEGORY(CurrencyExchange);
char *
CurrencyExchange_DumpOrderInfosCmd(int count)
{
    static char *outStr = NULL;
    estrClear(&outStr);
    CurrencyExchange_DumpOrderInfos(count, &outStr);

    return outStr;
}


AUTO_COMMAND ACMD_NAME(CurrencyExchange_CreateTestLock) ACMD_CATEGORY(CurrencyExchange);
void
CurrencyExchange_CreateTestLock(U32 accountID, const char *key)
{
	APChangeKeyValue(accountID, key, -1, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(CurrencyExchange);
void
CurrencyExchange_CancelAllOrdersShardOnly(void)
{
    aslCurrencyExchange_CancelAllOrdersShardOnly();
}