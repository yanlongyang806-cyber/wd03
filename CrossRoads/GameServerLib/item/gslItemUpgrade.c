#include "itemupgrade.h"
#include "gslItemUpgrade.h"
#include "objTransactions.h"
#include "EntityLib.h"
#include "Entity.h"
#include "textparser.h"
#include "GameAccountDataCommon.h"
#include "Player.h"
#include "referencesystem.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "LoggedTransactions.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/itemupgrade_h_ast.h"


static void trItemUpgrade_TransactionCallback(TransactionReturnVal *returnVal, ItemUpgradeStack *trData);

void itemUpgrade_UpgradeStack(Entity *pEnt, ItemUpgradeStack *pStack)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pItem = inv_GetItemByID(pEnt,pStack->uSrcItemId);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	SkillType eSkillTypeUpgrade = itemUpgrade_GetSkillUpgrade(pEnt,pItemDef);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pEnt, "Inventory:UpgradeStack", NULL);

	AutoTrans_inv_ent_tr_UpgradeItem(LoggedTransactions_CreateManagedReturnValEnt("ItemUpgradeStack",pEnt,trItemUpgrade_TransactionCallback,pStack),GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
		pStack->eSrcBagId,pStack->iSrcSlotIdx,pStack->uSrcItemId,pStack->eModBagId,pStack->iModSlotIdx,pStack->uModItemId,pStack->pchModDef,pStack->fChance,eSkillTypeUpgrade,&reason,pExtract);
}

void itemUpgrade_SetStackTimer(Entity *pEnt)
{
	pEnt->pPlayer->ItemUpgradeInfo.fFullUpgradeTime = itemUpgrade_GetUpgradeTime(pEnt);
	pEnt->pPlayer->ItemUpgradeInfo.fUpgradeTime = pEnt->pPlayer->ItemUpgradeInfo.fFullUpgradeTime;
}

void itemUpgrade_Tick(Entity *pEnt, F32 fTick)
{
	if(pEnt->pPlayer->ItemUpgradeInfo.fUpgradeTime > 0.0f)
	{
		pEnt->pPlayer->ItemUpgradeInfo.fUpgradeTime -= fTick;

		if(pEnt->pPlayer->ItemUpgradeInfo.fUpgradeTime <= 0.0f)
		{
			if (pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack
				&& pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack->iStackRemaining > 0)
			{
				itemUpgrade_UpgradeStack(pEnt,pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack);
			}
			else
			{
				StructDestroySafe(parse_ItemUpgradeStack,&pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack);
			}
		}
		if(pEnt->pPlayer->ItemUpgradeInfo.fFullUpgradeTime / 2 > pEnt->pPlayer->ItemUpgradeInfo.fUpgradeTime
			&& pEnt->pPlayer->ItemUpgradeInfo.eLastResult != kItemUpgradeResult_None)
		{
			pEnt->pPlayer->ItemUpgradeInfo.eLastResult = kItemUpgradeResult_None;
		}
	}
}

static void trItemUpgrade_TransactionCallback(TransactionReturnVal *returnVal, ItemUpgradeStack *trData)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,trData->eEntID);
	trData->iStackRemaining--;

	if(returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		trData->iStackRemaining = 0;
		if(pEntity)
			pEntity->pPlayer->ItemUpgradeInfo.eLastResult = kItemUpgradeResult_Broken;
	}
	else
	{
		if(pEntity) //need to find a way to figure out if failed the chance check
		{
			//pEntity->pPlayer->ItemUpgradeInfo.eLastResult = kItemUpgradeResult_Waiting;
		}
	}

	//if(trData->iStackRemaining > 0)
	//{
	//	if(pEntity)
	//	{
	//		//Clear any mod info
	//		if(pEntity->pPlayer->ItemUpgradeInfo.pCurrentStack->uModItemId)
	//		{
	//			pEntity->pPlayer->ItemUpgradeInfo.pCurrentStack->uModItemId = 0;
	//			pEntity->pPlayer->ItemUpgradeInfo.pCurrentStack->iModSlotIdx = 0;
	//			pEntity->pPlayer->ItemUpgradeInfo.pCurrentStack->eModBagId = InvBagIDs_None;
	//		}
	//	}
	//}
	
	if (SAFE_MEMBER(pEntity, pPlayer))
		StructDestroySafe(parse_ItemUpgradeStack,&pEntity->pPlayer->ItemUpgradeInfo.pCurrentStack);

	entity_SetDirtyBit(pEntity,parse_Player,pEntity->pPlayer,false);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
void gslItemUpgrade_SetLastResult(U32 entID, ItemDef *pSourceItem, ItemDef *pResultItem, ItemUpgradeResult eResult)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,entID);

	pEntity->pPlayer->ItemUpgradeInfo.eLastResult = eResult;
	itemUpgrade_SetStackTimer(pEntity);
	entity_SetDirtyBit(pEntity,parse_Player,pEntity->pPlayer,false);

	if(eResult == kItemUpgradeResult_Success)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.Success",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashSuccess, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	else if(eResult == kItemUpgradeResult_Failure)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.Failure",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashFailure, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	else if(eResult == kItemUpgradeResult_FailureNoLoss)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.FailureNoLoss",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashFailure, estr, NULL, NULL);
		estrDestroy(&estr);
	}
	else if(eResult == kItemUpgradeResult_UserCancelled)
	{
		char *estr = NULL;
		estrStackCreate(&estr);
		entFormatGameMessageKey(pEntity,
			&estr, "Item.Upgrade.UserCancelled",
			STRFMT_ITEMDEF_KEY("SourceItem", pSourceItem),
			STRFMT_ITEMDEF_KEY("ResultItem", pResultItem),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_ItemSmashFailure, estr, NULL, NULL);
		estrDestroy(&estr);
	}
}


void itemUpgrade_BeginStack(Entity *pEnt, int iStackAmount, InvBagIDs eSrcBagID, int SrcSlotIdx, U64 uSrcItemID, InvBagIDs eModBagID, int ModSlotIdx, U64 uModItemID)
{
	if(!pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack)
	{
		ItemUpgradeStack *pNewStack = (ItemUpgradeStack*)StructCreate(parse_ItemUpgradeStack);
		Item *pModItem = inv_GetItemByID(pEnt,uModItemID);
		ItemDef *pModDef = pModItem ? GET_REF(pModItem->hItem) : NULL;
		Item *pBaseItem = inv_GetItemByID(pEnt,uSrcItemID);
		ItemDef *pBaseDef = pBaseItem ? GET_REF(pBaseItem->hItem) : NULL;

		pNewStack->eSrcBagId = eSrcBagID;
		pNewStack->iSrcSlotIdx = SrcSlotIdx;
		pNewStack->uSrcItemId = uSrcItemID;

		pNewStack->eModBagId = eModBagID;
		pNewStack->iModSlotIdx = ModSlotIdx;
		pNewStack->uModItemId = uModItemID;

		pNewStack->iStackRemaining = MIN(iStackAmount,itemUpgrade_GetMaxStackAllowed(pEnt));
		pNewStack->eEntID = pEnt->myContainerID;
		pNewStack->fChance = itemUpgrade_GetChanceForItem(pEnt,pBaseDef,pModDef);

		pNewStack->pchModDef = pModDef ? StructAllocString(pModDef->pchName) : NULL;

		pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack = pNewStack;

		itemUpgrade_SetStackTimer(pEnt);
	}
}

void itemUpgrade_CancelJob(Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		Item* pItemSrc = inv_GetItemFromBag(pEnt, pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack->eSrcBagId, pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack->iSrcSlotIdx, pExtract);
		ItemDef* pSrcDef = pItemSrc ? GET_REF(pItemSrc->hItem) : NULL;
		gslItemUpgrade_SetLastResult(pEnt->myContainerID, pSrcDef, NULL, kItemUpgradeResult_UserCancelled);
	
		StructDestroySafe(parse_ItemUpgradeStack,&pEnt->pPlayer->ItemUpgradeInfo.pCurrentStack);

		entity_SetDirtyBit(pEnt,parse_Player,pEnt->pPlayer,false);
	}
}