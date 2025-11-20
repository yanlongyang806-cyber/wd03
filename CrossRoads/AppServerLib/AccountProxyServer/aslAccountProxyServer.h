/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "net/accountnet.h"
#include "serverlib.h"

typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct AccountProxyProductList AccountProxyProductList;
typedef struct AccountProxyDiscountList AccountProxyDiscountList;

void aslAPSetMTKillSwitch(int iActive);
void aslAPSetBillingKillSwitch(int iActive);

// Initiates a request to create an authentication ticket for the given account and 
// returns the random number identifying the ticket as the slow remote command return value.
void aslAPCreateTicketForOnlineAccount(U32 iAccountID, U32 iIp, SlowRemoteCommandID iCmdID);

SA_RET_NN_VALID const AccountProxyProductList *aslAPRequestProductList(U32 uTimestamp, U32 uVersion);
SA_RET_NN_VALID const AccountProxyDiscountList *aslAPRequestDiscountList(U32 uTimestamp, U32 uVersion);
void aslAPSendLockRequest(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, AccountProxySetOperation operation);
void aslAPMoveKeyValue(SlowRemoteCommandID iCmdID, U32 uSrcAccountID, SA_PARAM_NN_STR const char *pSrcKey, U32 uDestAccountID, SA_PARAM_NN_STR const char *pDestKey, S64 iValue);
void aslAPSetKeyValue(SlowRemoteCommandID iCmdID, U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, AccountProxySetOperation operation);
void aslAPRequestAllKeyValues(U32 accountID, U32 playerID);

//Returns a product by ID.  The return is allocated.  Make sure to destroy it.
SA_RET_OP_VALID AccountProxyProduct *aslAPGetProductByID(SA_PARAM_NN_STR const char *pCategory, U32 uProductID);
//Unlike "GetProductByID" this return is _not_ cloned
AccountProxyProduct *aslAPGetProductByName(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_STR const char *pProductName);

int AccountProxyServerLibOncePerFrame(F32 fElapsed);
void aslAPGetAllKeyValues(SlowRemoteCommandID iCmdID, U32 accountID, U32 playerID);
void aslAPGetSubbedTime(SlowRemoteCommandID iCmdID, U32 accountID, SA_PARAM_OP_STR const char *pProductInternalName);
void aslAPGetAccountLinkingStatus(SlowRemoteCommandID iCmdID, U32 accountID);
void aslAPGetAccountPlayedTime(SlowRemoteCommandID iCmdID, U32 uAccountID, const char *pProduct, const char *pCategory);
void aslAPGetAccountData(SlowRemoteCommandID iCmdID, U32 uAccountID, const char *pProduct, const char *pCategory, const char *pShard, const char *pCluster);
void aslAPRequestRecruitInfo(U32 uAccountID);
void aslAPGetAccountIDFromDisplayName(SlowRemoteCommandID iCmdID, SA_PARAM_NN_STR const char *pDisplayName);
void aslAPSetNumCharacters(U32 uAccountID, U32 uNumCharacters, bool bChange);
void aslAPLogoutNotification(SA_PARAM_NN_VALID const AccountLogoutNotification *pLogout);
void aslAPSendProductListDirectToServer(GlobalType eServerType, ContainerID serverID, SA_PARAM_NN_STR const char *pCategory, bool bForHammer);
void aslAPWebSrvGameEventRequest(AccountProxyWebSrvGameEvent *pGameEvent);