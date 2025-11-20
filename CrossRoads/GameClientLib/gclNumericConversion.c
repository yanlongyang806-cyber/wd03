/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "NumericConversionCommon.h"
#include "Entity.h"
#include "Player.h"
#include "gclEntity.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "GameAccountDataCommon.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ConvertNumeric");
void gclConvertNumeric(const char *conversionName)
{
    ServerCmd_gslConvertNumeric(conversionName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsNumericConversionAvailable");
bool gclIsNumericConversionAvailable(const char *conversionName)
{
    Entity* pEnt = entActivePlayerPtr();
    NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);
    ItemDef *sourceItemDef;

    if ( pEnt == NULL || pEnt->pPlayer == NULL || conversionDef == NULL )
    {
        return false;
    }

    sourceItemDef = GET_REF(conversionDef->hSourceNumeric);
    if ( sourceItemDef == NULL )
    {
        return false;
    }

    return ((NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef) > 0) && (inv_GetNumericItemValue(pEnt, sourceItemDef->pchName) > 0));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NumericConversionQuantityConverted");
U32 gclNumericConversionQuantityConverted(const char *conversionName)
{
    Entity* pEnt = entActivePlayerPtr();
    NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
    if ( pEnt == NULL || pEnt->pPlayer == NULL || conversionDef == NULL )
    {
        return false;
    }

    return conversionDef->quantityPerInterval + NumericConversion_GetBonusQuantity(pEnt,conversionDef, pExtract) - NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NumericConversionQuantityAvailable");
U32 gclNumericConversionQuantityAvailable(const char *conversionName)
{
    Entity* pEnt = entActivePlayerPtr();
    NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);

    if ( pEnt == NULL || pEnt->pPlayer == NULL || conversionDef == NULL )
    {
        return false;
    }

    return NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef);
}

