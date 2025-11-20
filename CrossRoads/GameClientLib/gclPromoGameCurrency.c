/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Player.h"
#include "GameAccountData/GameAccountData.h"
#include "accountnet.h"
#include "referencesystem.h"
#include "MicroTransactions.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPromoCurrencyBalance);
U32
gclPromoGameCurrency_GetBalance(Entity *playerEnt)
{
    GameAccountData *gameAccountData;
    const char *valueStr;
    int value = 0;

    if ( playerEnt == NULL || playerEnt->pPlayer == NULL || playerEnt->pPlayer->pPlayerAccountData == NULL || 
        microtrans_GetPromoGameCurrencyWithdrawNumericName() == NULL || microtrans_GetPromoGameCurrencyKey() == NULL )
    {
        return 0;
    }

    gameAccountData = GET_REF(playerEnt->pPlayer->pPlayerAccountData->hData);
    if ( gameAccountData == NULL )
    {
        return 0;
    }

    valueStr = AccountProxyFindValueFromKeyContainer(gameAccountData->eaAccountKeyValues, microtrans_GetPromoGameCurrencyKey());
    if ( valueStr != NULL )
    {
        value = atoi(valueStr);
    }

    return value;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPromoCurrencyWithdrawNumeric);
const char *
gclPromoGameCurrency_GetWithdrawNumeric(void)
{
    return microtrans_GetPromoGameCurrencyWithdrawNumericName();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ClaimPromoCurrency);
void
gclPromoGameCurrency_Claim(U32 quantity)
{
    ServerCmd_gslPromoGameCurrency_Claim(quantity);
}
