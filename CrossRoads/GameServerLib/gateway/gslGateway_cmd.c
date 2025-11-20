/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "gslGatewaySession.h"

#include "entity.h"
#include "player.h"

#include "NumericConversionCommon.h"
#include "inventoryCommon.h"
#include "GameAccountDataCommon.h"
#include "LoggedTransactions.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(gateway_SetHidden) ACMD_LIST(gGatewayCmdList) ACMD_GLOBAL;
void gateway_SetHidden(Entity *e, U32 bHidden)
{
	if(e && e->pPlayer && e->pPlayer->pGatewayInfo)
	{
		AutoTrans_gateway_tr_SetHidden(NULL, GetAppGlobalType(), entGetType(e), entGetContainerID(e), bHidden);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void gslGateway_ConvertNumeric(Entity *pEnt, const char *conversionName)
{
	NumericConversionDef *conversionDef = NumericConversion_DefFromName(conversionName);
	U32 lastConversionTime = 0;
	U32 lastConversionQuantity = 0;
	ItemChangeReason reason = {0};
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if ( pEnt == NULL || pEnt->pPlayer == NULL )
	{
		return;
	}

	if ( conversionDef == NULL )
	{
		return;
	}

	if ( NumericConversion_QuantityRemaining(pEnt, &pEnt->pPlayer->eaNumericConversionStates, conversionDef) > 0 )
	{
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("NumericConversion", pEnt, NULL, NULL);

		inv_FillItemChangeReason(&reason, pEnt, "NumericConversion", conversionName);
		AutoTrans_trConvertNumeric(pReturn, GLOBALTYPE_GATEWAYSERVER, pEnt->myEntityType, pEnt->myContainerID, conversionName, &reason, pExtract);
	}
}