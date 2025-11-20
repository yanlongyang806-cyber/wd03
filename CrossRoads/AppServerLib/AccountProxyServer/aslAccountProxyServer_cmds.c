/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslAccountProxyServer.h"
#include "AccountDataCache.h"
#include "accountnet.h"

#include "AccountDataCache_h_ast.h"
#include "accountnet_h_ast.h"

AUTO_COMMAND_REMOTE_SLOW(AccountProxySetResponse *);
void aslAPCmdSendLockRequest(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, AccountProxySetOperation operation)
{
	aslAPSendLockRequest(iCmdID, uAccountID, key, iValue, operation);
}

AUTO_COMMAND_REMOTE_SLOW(int); // Actually AccountKeyValueResult
void aslAPCmdSetKeyValue(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, AccountProxySetOperation operation)
{
	aslAPSetKeyValue(iCmdID, uAccountID, key, iValue, operation);
}

AUTO_COMMAND_REMOTE_SLOW(AccountProxyMoveResponse *);
void aslAPCmdMoveKeyValue(SlowRemoteCommandID iCmdID, U32 uSrcAccountID, SA_PARAM_NN_STR const char *pSrcKey, U32 uDestAccountID, SA_PARAM_NN_STR const char *pDestKey, S64 iValue)
{
	aslAPMoveKeyValue(iCmdID, uSrcAccountID, pSrcKey, uDestAccountID, pDestKey, iValue);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
ACMD_STATIC_RETURN const AccountProxyProductList *aslAPCmdRequestProductList(U32 uTimestamp, U32 uVersion)
{
	return aslAPRequestProductList(uTimestamp, uVersion);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
ACMD_STATIC_RETURN const AccountProxyDiscountList *aslAPCmdRequestDiscountList(U32 uTimestamp, U32 uVersion)
{
	return aslAPRequestDiscountList(uTimestamp, uVersion);
}

AUTO_COMMAND_REMOTE;
void aslAPCmdRequestAllKeyValues(U32 accountID, U32 playerID)
{
	aslAPRequestAllKeyValues(accountID, playerID);
}

AUTO_COMMAND_REMOTE;
void aslAPCmdRequestRecruitInfo(U32 accountID)
{
	aslAPRequestRecruitInfo(accountID);
}

AUTO_COMMAND_REMOTE_SLOW(AccountProxyKeyValueInfoList *);
void aslAPCmdGetAllKeyValues(SlowRemoteCommandID iCmdID, U32 accountID, U32 playerID)
{
	aslAPGetAllKeyValues(iCmdID, accountID, playerID);
}

AUTO_COMMAND_REMOTE_SLOW(U32);
void aslAPCmdGetSubbedTime(SlowRemoteCommandID iCmdID, U32 accountID, SA_PARAM_OP_STR const char *pProductInternalName)
{
	aslAPGetSubbedTime(iCmdID, accountID, pProductInternalName);
}

AUTO_COMMAND_REMOTE_SLOW(AccountProxyLinkingStatusResponse *);
void aslAPCmdGetAccountLinkingStatus(SlowRemoteCommandID iCmdID, U32 accountID)
{
	aslAPGetAccountLinkingStatus(iCmdID, accountID);
}

AUTO_COMMAND_REMOTE_SLOW(AccountProxyPlayedTimeResponse *);
void aslAPCmdGetAccountPlayedTime(SlowRemoteCommandID iCmdID, U32 uAccountID, const char *pProduct, const char *pCategory)
{
	aslAPGetAccountPlayedTime(iCmdID, uAccountID, pProduct, pCategory);
}

AUTO_COMMAND_REMOTE_SLOW(AccountProxyAccountDataResponse *);
void aslAPCmdGetAccountData(SlowRemoteCommandID iCmdID, U32 uAccountID, const char *pProduct, const char *pCategory, const char *pShard, const char *pCluster)
{
	aslAPGetAccountData(iCmdID, uAccountID, pProduct, pCategory, pShard, pCluster);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
AccountProxyProduct *aslAPCmdGetProductByID(SA_PARAM_NN_STR const char *pCategory, U32 uProductID)
{
	return aslAPGetProductByID(pCategory, uProductID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
SA_RET_OP_VALID AccountProxyProduct *aslAPCmdGetProductByName(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_STR const char *pProductName)
{
	return StructClone(parse_AccountProxyProduct, aslAPGetProductByName(pCategory, pProductName));
}

AUTO_COMMAND_REMOTE_SLOW(U32);
void aslAPCmdGetAccountIDFromDisplayName(SlowRemoteCommandID iCmdID, SA_PARAM_NN_STR const char *pDisplayName)
{
	aslAPGetAccountIDFromDisplayName(iCmdID, pDisplayName);
}

AUTO_COMMAND_REMOTE;
void aslAPCmdSetNumCharacters(U32 uAccountID, U32 uNumCharacters, bool bChange)
{
	aslAPSetNumCharacters(uAccountID, uNumCharacters, bChange);
}

AUTO_COMMAND_REMOTE;
void aslAPCmdLogoutNotification(SA_PARAM_NN_VALID const AccountLogoutNotification *pLogout)
{
	aslAPLogoutNotification(pLogout);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void aslAPCmdSetMTKillSwitch(int iActive)
{
	aslAPSetMTKillSwitch(iActive);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void aslAPCmdSetBillingKillSwitch(int iActive)
{
	aslAPSetBillingKillSwitch(iActive);
}

// Game server calls this function to generate a ticket
// for the entity, so they can go back to the character
// select without actually logging off
AUTO_COMMAND_REMOTE_SLOW(U32);
void aslAPCmdCreateTicketForOnlineAccount(U32 iAccountID, U32 iIp, SlowRemoteCommandID iCmdID)
{
	aslAPCreateTicketForOnlineAccount(iAccountID, iIp, iCmdID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslAPCmdWebSrvGameEventRequest(AccountProxyWebSrvGameEvent *pGameEvent)
{
	aslAPWebSrvGameEventRequest(pGameEvent);
}