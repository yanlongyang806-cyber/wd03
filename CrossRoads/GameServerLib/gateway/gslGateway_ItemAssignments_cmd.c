/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "ItemAssignmentsUICommon.h"
#include "gslItemAssignments.h"
#include "ItemAssignments.h"

#include "gslGatewaySession.h"
#include "Entity.h"
#include "Player.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemEnums.h"

#include "textparser.h"

#include "ItemAssignmentsUICommon_h_ast.h"
#include "itemAssignments_h_ast.h"
#include "itemEnums_h_ast.h"
#include "itemCommon_h_ast.h"

#include "gslGatewayContainerMapping.h"

void gslGateway_UseSessionItemAssignmentsCachedStruct(Entity *pEnt)
{
	GatewaySession *pSession = wgsFindSessionForAccountId(pEnt->pPlayer->accountID);

	if(pSession)
	{
		if(!pSession->pItemAssignmentsCache)
		{
			pSession->pItemAssignmentsCache = StructCreate(parse_ItemAssignmentCachedStruct);
		}

		pIACache = pSession->pItemAssignmentsCache;
	}
}

AUTO_COMMAND ACMD_NAME(ItemAssignments_CollectRewards) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void gslGateway_ItemAssignments_CollectRewards(Entity *pEnt, U32 ItemAssignmentID)
{
	gslGateway_UseSessionItemAssignmentsCachedStruct(pEnt);

	gslItemAssignments_CollectRewards(pEnt,ItemAssignmentID);
}

void gslGateway_ItemAssignments_ClearSlottedItems(Entity *pEnt)
{
	S32 i;

	gslGateway_UseSessionItemAssignmentsCachedStruct(pEnt);

	for (i = eaSize(&pIACache->eaSlots)-1; i >= 0; i--)
	{
		ItemAssignmentsClearSlottedItem(pIACache->eaSlots[i]);
	}

	eaSetSizeStruct(&pIACache->eaSlots, parse_ItemAssignmentSlotUI, 0);
}

AUTO_COMMAND ACMD_NAME(ItemAssignments_StartAssignment) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
bool gslGateway_ItemAssignments_StartAssignmentAtSlot(Entity *pEnt, const char* pchAssignmentDef)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	GatewaySession *pSession =  wgsFindSessionForAccountId(pEnt->pPlayer->accountID);
	int iSlot = 0;

	gslGateway_UseSessionItemAssignmentsCachedStruct(pEnt);

	if (pDef && pDef == SAFE_GET_REF2(pSession,pItemAssignmentsCache,hCurrentDef))
	{
		ItemAssignmentSlots Slots = {0};
		S32 i, iSize = eaSize(&pIACache->eaSlots);

		// do a check to make sure this slot is valid
		if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
		{
			if (pEnt)
			{	
				for(i=0;iSlot<g_ItemAssignmentSettings.pStrictAssignmentSlots->iMaxActiveAssignmentSlots;iSlot++)
				{
					if (ItemAssignments_IsValidNewItemAssignmentSlot(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots, iSlot))
					{	
						break;
					}
				}
				if(iSlot==g_ItemAssignmentSettings.pStrictAssignmentSlots->iMaxActiveAssignmentSlots)
				{
					// todo: say that we failed because the assignment slot was invalid
					return false;
				}
			}
		}

		for (i = 0; i < iSize; i++)
		{
			ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];
			if (pSlotUI->uItemID)
			{
				NOCONST(ItemAssignmentSlottedItem)* pSlot = StructCreateNoConst(parse_ItemAssignmentSlottedItem);
				pSlot->uItemID = pSlotUI->uItemID;
				pSlot->iAssignmentSlot = pSlotUI->iAssignmentSlot;
				eaPush(&Slots.eaSlots, (ItemAssignmentSlottedItem*)pSlot);
			}
		}

		gslItemAssignments_StartNewAssignment(pEnt,pchAssignmentDef, iSlot, &Slots);
		StructDeInit(parse_ItemAssignmentSlots, &Slots);
		gslGateway_ItemAssignments_ClearSlottedItems(pEnt);
		session_ReleaseContainer(pSession,GW_GLOBALTYPE_CRAFTING_DETAIL,REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		//session_ContainerModified(pSession, GW_GLOBALTYPE_CRAFTING_DETAIL, REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		REMOVE_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef);
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_NAME(ItemAssignments_CancelAssignment) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void gslGateway_ItemAssignments_CancelAssignment(Entity *pEnt, U32 uAssignmentID)
{
	gslItemAssignments_CancelActiveAssignment(pEnt,uAssignmentID);
}

AUTO_COMMAND ACMD_NAME(ItemAssignments_FinishEarly) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void gslGateway_ItemAssignments_FinishEarly(Entity *pEnt, U32 uAssignmentID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if(pAssignment)
		gslItemAssignments_CompleteAssignment(pEnt,pAssignment,NULL,true,false);
}

AUTO_COMMAND ACMD_NAME(ItemAssignments_SlotItem) ACMD_ACCESSLEVEL(0) ACMD_LIST(gGatewayCmdList);
void gslGateway_ItemAssignments_SlotItem(Entity *pEnt, S32 iAssignmentSlot, U64 uiID)
{
	BagIterator *iter = NULL;
	ItemAssignmentSlotUI* pSlotUI;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	GatewaySession *pSession = wgsFindSessionForAccountId(pEnt->pPlayer->accountID);

	if(!pEnt)
		return;

	gslGateway_UseSessionItemAssignmentsCachedStruct(pEnt);

	pSlotUI = eaGet(&pIACache->eaSlots, iAssignmentSlot);

	if(!pSlotUI)
		return;

	if(uiID == 0)
	{
		ItemAssignmentsClearSlottedItem(pSlotUI);
		session_ContainerModified(pSession,GW_GLOBALTYPE_CRAFTING_DETAIL,REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		return;
	}

	iter = inv_trh_FindItemByIDEx(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, pEnt),NULL, uiID, false, true);

	if(iter && ItemAssignmentsCanSlotItem(pEnt, iAssignmentSlot, bagiterator_GetCurrentBagID(iter),iter->i_cur,pExtract))
	{
		Item *pItem = invbag_GetItem(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, pEnt), bagiterator_GetCurrentBagID(iter), iter->i_cur, pExtract);

		if(pItem)
		{
			ItemAssignmentsSlotItemCheckSwap(pEnt, pSlotUI, bagiterator_GetCurrentBagID(iter), iter->i_cur, pItem);
			session_ContainerModified(pSession, GW_GLOBALTYPE_CRAFTING_DETAIL, REF_STRING_FROM_HANDLE(pSession->pItemAssignmentsCache->hCurrentDef));
		}
	}
}