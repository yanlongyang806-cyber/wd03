/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CurrencyExchangeCommon.h"
#include "GameServerLib.h"
#include "Entity.h"
#include "Player.h"
#include "EntitySavedData.h"
#include "EntityLib.h"
#include "objSchema.h"
#include "ResourceManager.h"
#include "NotifyEnum.h"
#include "GameStringFormat.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/CurrencyExchangeCommon_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

// How often (in seconds) to send global ui data to servers.
#define SEND_UIDATA_TO_CLIENTS_INTERVAL 1

// How long (in seconds) to continue sending global ui data to a server without getting a request.
#define SEND_UIDATA_TO_CLIENTS_EXPIRE_INTERVAL 10

// How often (in seconds) to request global ui data from the currency exchange server.
#define REQUEST_UI_DATA_INTERVAL 5

static U32 sLastClientUIDataRequest = 0;
static U32 sLastGlobalUIDataUpdateTime = 0;
static U32 sLastRequestForUIData = 0;
static CurrencyExchangeGlobalUIData sGlobalUIData = {0};

static void
SendGlobalUIDataToClient(GlobalType entityType, ContainerID entityID)
{
	Entity *pEnt;
	devassert(entityType == GLOBALTYPE_ENTITYPLAYER);
	
	pEnt = entFromContainerIDAnyPartition(entityType, entityID);
	if ( pEnt != NULL && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
		ClientCmd_gclCurrencyExchange_ReturnGlobalUIData(pEnt, &sGlobalUIData);
	}
}

void
gslCurrencyExchange_OncePerFrame(void)
{
	static U32 sLastUpdateTime = 0;
	U32 curTime = timeSecondsSince2000();

	if ( curTime != sLastUpdateTime )
	{
		// Send UI data updates to subscribing clients.
		CurrencyExchange_UpdateSubscribers(SendGlobalUIDataToClient, SEND_UIDATA_TO_CLIENTS_INTERVAL, SEND_UIDATA_TO_CLIENTS_EXPIRE_INTERVAL);

		// If there are current subscribers and the request interval has passed, than request again.
		if ( ( CurrencyExchange_NumSubscribers() > 0 ) && ( ( sLastRequestForUIData + REQUEST_UI_DATA_INTERVAL ) < curTime ) )
		{
			RemoteCommand_aslCurrencyExchange_RequestGlobalUIData(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, objServerType(), objServerID());
		}

		sLastUpdateTime = curTime;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_CATEGORY(CurrencyExchange);
void
gslCurrencyExchange_ReturnGlobalUIData(CurrencyExchangeGlobalUIData *uiData)
{
	StructCopy(parse_CurrencyExchangeGlobalUIData, uiData, &sGlobalUIData, 0, 0, 0);
	sLastGlobalUIDataUpdateTime = timeSecondsSince2000();
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_CATEGORY(CurrencyExchange);
void
gslCurrencyExchange_NotifyPlayer(ContainerID playerID, CurrencyExchangeResultType resultType, CurrencyExchangeOperationType operationType)
{
	const char *resultTypeStr;
	const char *opTypeStr;
	static char *reasonString = NULL;
	static char *msgKeyString = NULL;
	static char *reasonMsgKeyString = NULL;
	static char *formattedString = NULL;

	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, playerID);

	if ( pEnt != NULL )
	{
		opTypeStr = StaticDefineIntRevLookup(CurrencyExchangeOperationTypeEnum, operationType);

		if ( resultType == CurrencyResultType_Success )
		{
			estrPrintf(&msgKeyString, "CurrencyExchange.Success.Operation.%s", opTypeStr);
			estrClear(&formattedString);
			entFormatGameMessageKey(pEnt, &formattedString, msgKeyString,
				STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
			ClientCmd_NotifySend(pEnt, kNotifyType_CurrencyExchangeSuccess, formattedString, NULL, NULL);
		}
		else if ( resultType == CurrencyResultType_Info || resultType == CurrencyResultType_DuplicateWithdrawal )
		{
			// Do nothing for these two results.
		}
		else
		{
			resultTypeStr = StaticDefineIntRevLookup(CurrencyExchangeResultTypeEnum, resultType);
			estrPrintf(&msgKeyString, "CurrencyExchange.Failure.Operation.%s", opTypeStr);
			estrPrintf(&reasonMsgKeyString, "CurrencyExchange.Failure.Result.%s", resultTypeStr);
			estrClear(&reasonString);
			estrClear(&formattedString);
			entFormatGameMessageKey(pEnt, &reasonString, reasonMsgKeyString,
				STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
			entFormatGameMessageKey(pEnt, &formattedString, msgKeyString,
				STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_STRING("Reason", reasonString), STRFMT_END);
			ClientCmd_NotifySend(pEnt, kNotifyType_CurrencyExchangeFailure, formattedString, NULL, NULL);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(CurrencyExchange) ACMD_PRIVATE;
void
gslCurrencyExchange_RequestUIData(Entity *pEnt)
{
    if ( entGetVirtualShardID(pEnt) == 0 )
    {
	    // Add a subscription to the currency account data so that it will be available to the UI.
	    if(!IS_HANDLE_ACTIVE(pEnt->pPlayer->hCurrencyExchangeAccountData))
	    {
			char idBuf[128];
		    SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_CURRENCYEXCHANGE), ContainerIDToString(pEnt->pPlayer->accountID, idBuf), pEnt->pPlayer->hCurrencyExchangeAccountData);
		    entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	    }
	    CurrencyExchange_AddSubscriber(pEnt->myEntityType, pEnt->myContainerID);
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CurrencyExchange) ACMD_HIDE;
void
CurrencyExchange_CreateBuyOrder(Entity *pEnt, U32 quantity, U32 price)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
    {
        RemoteCommand_aslCurrencyExchange_CreateOrder(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, OrderType_Buy, quantity, price, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CurrencyExchange) ACMD_HIDE;
void
CurrencyExchange_CreateSellOrder(Entity *pEnt, U32 quantity, U32 price)
{
    if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
    {
        RemoteCommand_aslCurrencyExchange_CreateOrder(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, OrderType_Sell, quantity, price, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
    }
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CurrencyExchange) ACMD_HIDE;
void
CurrencyExchange_WithdrawOrder(Entity *pEnt, U32 orderID)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
        RemoteCommand_aslCurrencyExchange_WithdrawOrder(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, orderID, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CurrencyExchange) ACMD_HIDE;
void
CurrencyExchange_ClaimTC(Entity *pEnt, U32 quantity)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
        RemoteCommand_aslCurrencyExchange_ClaimTC(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, quantity, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CurrencyExchange) ACMD_HIDE;
void
CurrencyExchange_ClaimMTC(Entity *pEnt, U32 quantity)
{
	if ( pEnt && pEnt->pPlayer && pEnt->pSaved && ( entGetVirtualShardID(pEnt) == 0 ) )
	{
        RemoteCommand_aslCurrencyExchange_ClaimMTC(GLOBALTYPE_CURRENCYEXCHANGESERVER, 0, pEnt->pPlayer->accountID, pEnt->myContainerID, pEnt->pSaved->savedName, quantity, pEnt->debugName, entity_GetProjSpecificLogString(pEnt));
	}
}

void
gslCurrencyExchange_SchemaInit(void)
{
	objRegisterNativeSchema(GLOBALTYPE_CURRENCYEXCHANGE, parse_CurrencyExchangeAccountData, NULL, NULL, NULL, NULL, NULL);

	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_CURRENCYEXCHANGE), false, parse_CurrencyExchangeAccountData, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_CURRENCYEXCHANGE), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_CURRENCYEXCHANGE));
}