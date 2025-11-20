#include "AccountProxyCommon.h"
#include "AccountDataCache.h"
#include "gslAccountProxy.h"
#include "GlobalTypes.h"
#include "file.h"
#include "cmdparse.h"
#include "GameServerLib.h"
#include "gslMicroTransactions.h"
#include "MicroTransactions.h"
#include "NotifyCommon.h"
#include "gslSteam.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "microtransactions.h"
#include "ServerLib.h"
#include "gslEntity.h"
#include "EntityLib.h"
#include "mission_common.h"

#include "AutoGen/LoginCommon_h_ast.h"
#include "../Common/AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "Player.h"
#include "gslGameAccountData.h"
#include "autogen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "Autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "../Common/AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

#include "AutoGen/gslEntity_h_ast.h"

CmdList gAPCommandList;

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(AccountProxy);
void gslAccountProxyCommand(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CmdContext *pContext, ACMD_NAMELIST(gAPCommandList, COMMANDLIST) ACMD_SENTENCE cmd)
{
	VerifyServerTypeExistsInShard(GLOBALTYPE_ACCOUNTPROXYSERVER);
	cmdParseAndExecute(&gAPCommandList, cmd, pContext);
}

// Set the integer value of a key associated with your account.
AUTO_COMMAND ACMD_NAME(SetValue) ACMD_ACCESSLEVEL(9) ACMD_LIST(gAPCommandList);
void gslAPCmdSetKeyValueCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal)
{
	gslAPSetKeyValueCmd(pEntity, key, iVal);
}

// Unlock this Character Type
AUTO_COMMAND ACMD_NAME(SetAllegianceUnlock) ACMD_ACCESSLEVEL(7);
void gslAPCmdSetAllegianceUnlock(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S32 iVal)
{
	int i;
	char **eaTempNames = NULL;
	S32 *eaTempValues = NULL;

	DefineFillAllKeysAndValues(UnlockedAllegianceFlagsEnum,&eaTempNames,&eaTempValues);
	for (i = eaSize(&eaTempNames)-1; i > 0; --i)
	{
		if (!stricmp(key, eaTempNames[i]))
		{
			break;
		}
	}

	if (i >= 0 && pEntity && pEntity->pPlayer && pEntity->pPlayer->pPlayerAccountData && pEntity->pPlayer->pPlayerAccountData->iAccountID)
	{
		char temp[32];
		sprintf(temp, "%d", iVal);
		AutoTrans_slGAD_tr_SetAttrib(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA,
			pEntity->pPlayer->pPlayerAccountData->iAccountID, key, temp);
	}

	eaDestroy(&eaTempNames);
	eaiDestroy(&eaTempValues);
}

// Set the GM account flag 
AUTO_COMMAND ACMD_NAME(GMFlagSet) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(csr);
void gslAPCmdGMFlagSet(SA_PARAM_NN_VALID Entity *pEntity, S64 iVal)
{
	gslAPSetKeyValueCmd(pEntity, GetAccountGMKey(), iVal);
}

// Adds value to your tutorial done count
AUTO_COMMAND ACMD_NAME(TutorialDoneAdd) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr);
void gslAPCmdAccountTutorialDoneAdd(SA_PARAM_NN_VALID Entity *pEntity, S64 iVal)
{
	gslAPChangeKeyValueCmd(pEntity, GetAccountTutorialDoneKey(), iVal);
}

bool CanBanAccount(Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer)
	{
		if (pEnt->pPlayer->pCSRListener)
			return (pEnt->pPlayer->pCSRListener->listenerAccessLevel > pEnt->pPlayer->accessLevel);
		return (pEnt->pPlayer->accessLevel < ACCESS_GM);
	}
	
	return false;
}

// Ban this account
AUTO_COMMAND ACMD_NAME(Ban) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdBanFlagSet(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pCmdContext)
{
	if(!CanBanAccount(pEntity))
	{
		estrPrintf(pCmdContext->output_msg, "%s", "Can not Ban, access level not greater than target.");
		return;
	}
	
	gslAPSetKeyValueCmd(pEntity, GetAccountBannedKey(), 1);
	RemoteCommand_dbBootPlayerByAccountName_Remote(GLOBALTYPE_OBJECTDB, 0, pEntity->pPlayer->privateAccountName);
}

AUTO_COMMAND_REMOTE;
void gslAP_BanAccount(U32 uContainerID)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uContainerID);
	if (CanBanAccount(pEntity))
		gslAPCmdBanFlagSet(pEntity, NULL);
}

// Unban this account
AUTO_COMMAND ACMD_NAME(UnBan) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdUnBanFlagSet(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pCmdContext)
{
	gslAPSetKeyValueCmd(pEntity, GetAccountBannedKey(), 0);
}


/* DEPRECATED
// Ban this account from UGC editing
AUTO_COMMAND ACMD_NAME(UgcEditBan) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdUgcEditBanFlagSet(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pCmdContext)
{
	if(!CanBanAccount(pEntity))
	{
		estrPrintf(pCmdContext->output_msg, "%s", "Can not UgcEditBan, access level not greater than target.");
		return;
	}
	
	gslAPSetKeyValueCmd(pEntity, GetAccountUgcEditBanKey(), 1);
}

// Unban this account from UGC editing
AUTO_COMMAND ACMD_NAME(UgcEditUnBan) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdUgcEditUnBanFlagSet(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pCmdContext)
{
	gslAPSetKeyValueCmd(pEntity, GetAccountUgcEditBanKey(), 0);
}
*/

// Ban this account from UGC Publishing
AUTO_COMMAND ACMD_NAME(UgcPublishBan) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdUgcPublishBanFlagSet(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pCmdContext)
{
	if(!CanBanAccount(pEntity))
	{
		estrPrintf(pCmdContext->output_msg, "%s", "Can not UgcPublishBan, access level not greater than target.");
		return;
	}
	
	gslAPSetKeyValueCmd(pEntity, GetAccountUgcPublishBanKey(), 1);

	//need to make sure to boot the guy also, as gameservers won't get the updated flag until he re-logs-in
	RemoteCommand_dbBootPlayerByAccountName_Remote(GLOBALTYPE_OBJECTDB, 0, pEntity->pPlayer->privateAccountName);

	//ban all his projects too
		// WOLF[2Nov11] This change came in as larger set of changes by Jared Finder.  For now on Star Trek we do not
		//   want this behaviour. We merely want to keep the person from publishing. Long term there should either
		//   be a separate command for banning the projects, or an option on this one to ban them.
//	RemoteCommand_BanUGCProjectsByAccountID(GLOBALTYPE_UGCDATAMANAGER, 0, pEntity->pPlayer->accountID, "UGCPublishBan");

	//tell all ugcsearch managers to flush their cache concerning this dude. Do it a few times in case of corner case
	//servers that are starting up right as this happens, also because it may execute before our transaction
	RemoteCommand_SendCommandRepeatedlyToAllServersOfType(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_UGCSEARCHMANAGER, 0, 5, 60, 5,
		STACK_SPRINTF("aslUGCSearchManagerInvalidatePermissionCache %u", pEntity->pPlayer->accountID));
}

// Unban this account from UGC Publishing
AUTO_COMMAND ACMD_NAME(UgcPublishUnBan) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdUgcPublishUnBanFlagSet(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pCmdContext)
{
	gslAPSetKeyValueCmd(pEntity, GetAccountUgcPublishBanKey(), 0);
}


// Suspend this account, 0 = forever, -1 no longer suspended, > 0 number of hours
AUTO_COMMAND ACMD_NAME(Suspend) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(XMLRPC, csr);
void gslAPCmdSuspendFlagSet(SA_PARAM_NN_VALID Entity *pEntity, F32 hoursToSuspend, CmdContext *pCmdContext)
{
	S32 iVal;
	if(hoursToSuspend >= 0.0f && !CanBanAccount(pEntity))
	{
		estrPrintf(pCmdContext->output_msg, "%s", "Can not Suspend, access level not greater than target.");
		return;
	}

	iVal = hoursToSuspend * 3600;
	if(iVal <= -1)
	{
		iVal = -1;	// no longer suspended as key of 0 or greater == suspension
	}
	else if(iVal > 0)
	{
		iVal += timeSecondsSince2000();
	}
	gslAPSetKeyValueCmd(pEntity, GetAccountSuspendedKey(), iVal);
	if(iVal >= 0)
	{
		RemoteCommand_dbBootPlayerByAccountName_Remote(GLOBALTYPE_OBJECTDB, 0, pEntity->pPlayer->privateAccountName);
	}
}

AUTO_EXPR_FUNC(player) ACMD_NAME(SetTutorialDone);
void exprPlayer_SetTutorialDone(SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pPlayer)
	{
		gslAPChangeKeyValueCmd(pEntity, GetAccountTutorialDoneKey(), 1);
	}
}

// Change the value of a key associated with your account.
AUTO_COMMAND ACMD_NAME(ChangeValue) ACMD_ACCESSLEVEL(9) ACMD_LIST(gAPCommandList);
void gslAPCmdChangeKeyValueCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal)
{
	gslAPChangeKeyValueCmd(pEntity, key, iVal);
}

// Get the full list of key values.
AUTO_COMMAND ACMD_NAME(GetKeyValues) ACMD_ACCESSLEVEL(9) ACMD_LIST(gAPCommandList);
void gslAPCmdGetKeyValues(SA_PARAM_NN_VALID Entity *pEntity)
{
	gslAPGetKeyValuesCmd(pEntity);
}

// Get the account ID for a display name.
AUTO_COMMAND ACMD_NAME(GetAccountIDFromDisplayName) ACMD_ACCESSLEVEL(9) ACMD_LIST(gAPCommandList);
void gslAPCmdGetAccountIDFromDisplayName(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pDisplayName)
{
	gslAPGetAccountIDFromDisplayNameCmd(pEntity, pDisplayName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslAPCmdRequestMTCatalog(SA_PARAM_NN_VALID Entity *pEntity)
{
	gslAPRequestMTCatalog(pEntity);
}

static void gslAPCmdGetMOTD_CB(const AccountProxyProduct *pProduct, void *userPtr)
{
	EntityRef iRef = (intptr_t)userPtr;
	Entity *pEnt = entFromEntityRefAnyPartition(iRef);

	if( pEnt )
	{
		ClientCmd_gclMicroTrans_SetMOTD(pEnt, pProduct);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslAPCmdGetMOTD(SA_PARAM_NN_VALID Entity *pEntity)
{
	static char pcBuffer[128];
	MicroTrans_ShardConfig *pConfig = eaIndexedGetUsingInt(&g_MicroTransConfig.ppShardConfigs, g_eMicroTrans_ShardCategory);
	if( pConfig
		 && pConfig->pMOTD 
		 && pConfig->pMOTD->pchCategory 
		 && pConfig->pMOTD->pchName )
	{
		if(!pcBuffer[0])
		{
			sprintf(pcBuffer, "%s.", microtrans_GetShardCategoryPrefix());
			if(strnicmp(pcBuffer, pConfig->pMOTD->pchCategory, strlen(pcBuffer)) == 0)
			{
				strcpy(pcBuffer, pConfig->pMOTD->pchCategory);
			}
			else
			{
				sprintf(pcBuffer, "%s.%s", microtrans_GetShardCategoryPrefix(), pConfig->pMOTD->pchCategory);
			}
		}
		APGetProductByName(pcBuffer, pConfig->pMOTD->pchName, gslAPCmdGetMOTD_CB, (void *)((intptr_t)entGetRef(pEntity)));
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslAPCmdRequestAllKeyValues(SA_PARAM_NN_VALID Entity *pEntity)
{
	if(entGetAccessLevel(pEntity))
		gslAPRequestAllKeyValues(pEntity);
}

AUTO_COMMAND_REMOTE;
void gslAPCmdClientCacheSetAllKeyValues(GlobalType eType,ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pList)
{
	APClientCacheSetAllKeyValues(eType, uContainerID, uAccountID, pList);
}

void gslAPUpdateGADCacheCB(TransactionReturnVal *pReturnVal, void *pUserdata)
{
	ContainerID uEntID = (ContainerID)((intptr_t)pUserdata);
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uEntID);

	if (SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo))
		pEnt->pPlayer->pInteractInfo->bUpdateContactDialogOptionsNextTick = true;
}

AUTO_COMMAND_REMOTE;
void gslAPCmdClientCacheSetKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo)
{
	APUpdateGADCache(GLOBALTYPE_GAMESERVER, uAccountID, pInfo->pKey, pInfo->pValue, gslAPUpdateGADCacheCB, (void*)((intptr_t)uContainerID));
	APClientCacheSetKeyValue(eType, uContainerID, uAccountID, pInfo);
}

AUTO_COMMAND_REMOTE;
void gslAPCmdClientCacheRemoveKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_STR const char *pKey)
{
	APUpdateGADCache(GLOBALTYPE_GAMESERVER, uAccountID, pKey, NULL, gslAPUpdateGADCacheCB, (void*)((intptr_t)uContainerID));
	APClientCacheRemoveKeyValue(eType, uContainerID, uAccountID, pKey);
}

// New commands for use with coupon item
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_NAME(PurchaseProductEx);
void gslAPCmdPurchaseProductEx(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pCategory, U32 uProductID, SA_PARAM_OP_VALID const Money *pExpectedPrice, SA_PARAM_OP_STR const char *pPaymentMethodVID, U64 uItemId)
{
	MicroTransactionCategory *pMTCategory = microtrans_CategoryFromStr(pCategory);
	if(pEntity && pEntity->pPlayer)
	{
		char pcIP[17] = {0};
		U32 uItemDiscount = 0;
		Item *pItem = NULL;


		if(uItemId > 0)
		{
			MicroTransactionInfo *pMTInfo = gslAPGetProductList();
			pItem = inv_GetItemByID(pEntity, uItemId);
			if(!MicroTrans_ValidDiscountItem(pEntity, uProductID, pItem, &pMTInfo->ppProducts))
			{
				// in data/messages/microtransactions.ms
				notify_NotifySend(pEntity, kNotifyType_MicroTransFailed, langTranslateMessageKey(entGetLanguage(pEntity), "MicroTrans_Cant_Apply Coupon"), NULL, NULL);
				return;
			}
			uItemDiscount = MicroTrans_GetItemDiscount(pEntity, pItem);
		}

		if(pEntity->pPlayer->clientLink && pEntity->pPlayer->clientLink->netLink)
			linkGetIpStr(pEntity->pPlayer->clientLink->netLink, pcIP, sizeof(pcIP));

		if(pMTCategory)
		{
			if(pEntity->pPlayer->accountID && uProductID)
			{
				char pcBuffer[128];
				sprintf(pcBuffer, "%s.%s", microtrans_GetShardCategoryPrefix(), pMTCategory->pchName);
				APPurchaseProduct(pEntity, entGetAccountID(pEntity), entGetLanguage(pEntity), pcBuffer, uProductID, pExpectedPrice, uItemDiscount, pPaymentMethodVID, pcIP, uItemId);
				// May not want this notification for point-based purchases
				//notify_NotifySend(pEntity, kNotifyType_MicroTrans_PointBuyPending, langTranslateMessageKey(entGetLanguage(pEntity), "MicroTrans_Purchase_Pending"), NULL, NULL);
			}
		}
		else if(pPaymentMethodVID && entGetAccountID(pEntity))
		{
			APPurchaseProduct(pEntity, entGetAccountID(pEntity), entGetLanguage(pEntity), pCategory, uProductID, pExpectedPrice, uItemDiscount, pPaymentMethodVID, pcIP, uItemId);
			notify_NotifySend(pEntity, kNotifyType_MicroTrans_PointBuyPending, langTranslateMessageKey(entGetLanguage(pEntity), "MicroTrans_Purchase_Pending"), NULL, NULL);
		}
	}
}

// old command without item for discount
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslAPCmdPurchaseProduct(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pCategory, U32 uProductID, SA_PARAM_OP_VALID const Money *pExpectedPrice, SA_PARAM_OP_STR const char *pPaymentMethodVID)
{
	gslAPCmdPurchaseProductEx(pEntity, pCategory, uProductID, pExpectedPrice, pPaymentMethodVID, 0);
}

AUTO_COMMAND ACMD_NAME(GetSubbedTime) ACMD_ACCESSLEVEL(9) ACMD_LIST(gAPCommandList);
void gslAPCmdGetSubbedTime(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_STR const char *productInternalName)
{
	gslAPGetSubbedTimeCmd(pEntity, productInternalName);
}

AUTO_COMMAND ACMD_NAME(GetSubbedDays) ACMD_ACCESSLEVEL(9);
void gslAPCmdGetDays(SA_PARAM_NN_VALID Entity *pEntity)
{
	gslAPGetSubbedDaysCmd(pEntity);
}

static void GetPaymentMethod_CB(U32 entityRef, U32 accountID, PaymentMethodsResponse *pResponse)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)entityRef);

	if(pEnt)
	{
		ClientCmd_gclMicroTrans_RecvPaymentMethods(pEnt, pResponse);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslAPCmdGetPaymentMethods(Entity *pEnt, U64 uSteamID)
{
	APRequestPaymentMethods(pEnt, 0, uSteamID, GetPaymentMethod_CB);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslAPCmdRequestPointBuyCatalog(Entity *pEnt)
{
	gslAPRequestPointBuyCatalog(pEnt);
}

AUTO_COMMAND ACMD_NAME(GiveMeCPoints) ACMD_ACCESSLEVEL(9);
void gslAPCmdGiveCPoints(Entity *pEnt, S64 iPoints)
{
	gslAPChangeKeyValueCmd(pEnt, microtrans_GetShardCurrency(), iPoints);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(GiveMeCPoints);
void gslAPCmdGiveCPointsDefault(Entity *pEnt)
{
	gslAPChangeKeyValueCmd(pEnt, microtrans_GetShardCurrency(), 1000000);
}

AUTO_COMMAND ACMD_NAME(TakeMyCPoints) ACMD_ACCESSLEVEL(9);
void gslAPCmdTakeCPoints(Entity *pEnt, S64 iPoints)
{
	gslAPChangeKeyValueCmd(pEnt, microtrans_GetShardCurrency(), -iPoints);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(TakeMyCPoints);
void gslAPCmdTakeCPointsDefault(Entity *pEnt)
{
	gslAPSetKeyValueCmd(pEnt, microtrans_GetShardCurrency(), 0);
}

//
// XMLRPC Commands for Perfect world
//

// rename this character
AUTO_COMMAND ACMD_NAME(rename_character) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(XMLRPC, csr);
S32 gslAPCmdRenameCharacter(SA_PARAM_NN_VALID Entity *pEntity, const char *pcNewName)
{
	if(pEntity && pEntity->pPlayer && pcNewName && pcNewName[0])
	{
		RemoteCommand_dbRenamePlayer(NULL, GLOBALTYPE_OBJECTDB, 0, pEntity->myContainerID, pcNewName);
		return true;
	}
	
	return false;
}

static void gslPwMapItemAdd(PWInventory *pPWInv, Item *pItem)
{
	if(pItem)
	{
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		if(pItemDef)
		{
			PWMappedItem *pPWItem = StructCreate(parse_PWMappedItem);

			estrPrintf(&pPWItem->esName, "%s", pItemDef->pchName);
			pPWItem->uCount = pItem->count;
			pPWItem->uId = pItem->id;
			if(pItem->pchDisplayName)
			{
				estrPrintf(&pPWItem->esDisplayName, "%s", pItem->pchDisplayName);
			}

			eaPush(&pPWInv->eaMappedItems, pPWItem);
		}
	}
}

// return the inventory for this character
AUTO_COMMAND ACMD_NAME(list_inventory) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(XMLRPC);
PWInventory* gslAPCmdListInventory(SA_PARAM_NN_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pInventoryV2)
	{
		PWInventory *pInv = StructCreate(parse_PWInventory);
		S32 NumBags, iBag;

		NumBags = eaSize(&pEntity->pInventoryV2->ppInventoryBags);
		for(iBag=0;iBag<NumBags;iBag++)
		{
			S32 NumSlots,iSlot;

			InventoryBag *pBag = pEntity->pInventoryV2->ppInventoryBags[iBag];

			NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
			for(iSlot=0;iSlot<NumSlots;iSlot++)
			{
				InventorySlot *pSlot = pBag->ppIndexedInventorySlots[iSlot];
				gslPwMapItemAdd(pInv, pSlot->pItem);
			}
		}		

		NumBags = eaSize(&pEntity->pInventoryV2->ppLiteBags);
		for(iBag=0;iBag<NumBags;iBag++)
		{
			S32 NumSlots,iSlot;

			InventoryBagLite *pBag = pEntity->pInventoryV2->ppLiteBags[iBag];

			NumSlots = eaSize(&pBag->ppIndexedLiteSlots);
			for(iSlot=0;iSlot<NumSlots;iSlot++)
			{
				InventorySlotLite *pSlot = pBag->ppIndexedLiteSlots[iSlot];

				ItemDef *pItemDef = GET_REF(pSlot->hItemDef);
				if(pItemDef)
				{
					PWMappedItem *pPWItem = StructCreate(parse_PWMappedItem);

					estrPrintf(&pPWItem->esName, "%s", pItemDef->pchName);
					pPWItem->uCount = pSlot->count;

					eaPush(&pInv->eaMappedItems, pPWItem);
				}
			}
		}		

		return pInv;
	}

	return NULL;
}

#if 0

AUTO_COMMAND ACMD_NAME(MicrotransHammer) ACMD_ACCESSLEVEL(9);
void gslAPCmdMicrotransHammer(const char *pchCategory, F32 fSuccessRate)
{
	gslAPMicrotransHammer(pchCategory, fSuccessRate);
}

AUTO_COMMAND ACMD_NAME(MicrotransHammerDelay) ACMD_ACCESSLEVEL(9);
void gslAPCmdMicrotransHammerDelay(F32 fDelay)
{
	gslAPMicrotransHammerDelay(fDelay);
}

AUTO_COMMAND ACMD_NAME(MicrotransHammerCurrency) ACMD_ACCESSLEVEL(9);
void gslAPCmdMicrotransHammerCurrency(const char *pchCurrency)
{
	gslAPMicrotransHammerCurrency(pchCurrency);
}

#endif // 0
