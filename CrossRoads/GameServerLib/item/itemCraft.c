

#include "Entity.h"

#include "LocalTransactionManager.h"
#include "objTransactions.h"

#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "itemServer.h"
#include "itemTransaction.h"
#include "inventoryTransactions.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

#include "earray.h"
#include "Expression.h"
#include "gslSendToClient.h"
#include "Color.h"
#include "rand.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "gslLoggedTransactions.h"

// Give the player this much "resource" (money).
AUTO_COMMAND ACMD_CATEGORY(Standard, Inventory);
void GrantResource( Entity *e, S32 iAmount )
{
	itemtransaction_AddNumeric(e, InvBagIDs_Numeric, "Resources", iAmount, NULL, NULL);
}

// Give the player this rank in a skill.
AUTO_COMMAND ACMD_CATEGORY(Standard, Inventory);
void GrantSkill( Entity *e, char *skill, S32 iAmount )
{
	SetSkill(e, skill);
	SetSkillLevel(e, iAmount);
}

bool item_CraftingEval(Entity *e, ItemDef *pRecipe)
{
//	static ExprContext* context = NULL;
//	MultiVal mval;
//	Expression * pExpr =0;
//
//	if(!context)
//		context = exprContextCreate();
//
//	if( pRecipe->pCraft->pRequires )
//	{
//		exprEvaluate(pRecipe->pCraft->pRequires,context,&mval);
//		return (bool)!!MultiValGetInt(&mval, NULL);
//	}
//	else
		return 1;
}


bool item_IsContructable( Entity *e, Item * pRecipe )
{
	ItemDef *pDef = GET_REF(pRecipe->hItem);
	ItemCraftingTable *pCraft = pDef ? pDef->pCraft : NULL;
	int i;

	if( !pCraft )
		return 0;

//	if ( (inventory_GetResource(e) < pCraft->iResource) ||
//		 (!entity_HasSkill(e,pCraft->eSkillType )) ||
//		 ((U32)entity_GetSkillLevel(e) < pCraft->iSkillLevel) )
//		return 0;

	if( !item_CraftingEval(e, pDef) )
		return 0;

	// search inventory for ingredients
	for( i=eaSize(&pCraft->ppPart)-1; i>=0; i-- )
	{
		ItemCraftingComponent *pComp = pCraft->ppPart[i];
		if( item_CountOwned(e, GET_REF(pComp->hItem)) < item_GetComponentCount(pComp) )
			return 0;
	}

	return 1;
}

// Craft an item from a recipe with this name.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory);
bool item_Construct( Entity *e, const char * pchRecipeName )
{
	Item *pRecipe=0;
	ItemDef *pDef;
	TransactionReturnValStruct* returnVal;

	
	pRecipe = inv_GetItemFromBagIDByName( e, InvBagIDs_Recipe, (char*)pchRecipeName);

	if(!pRecipe)
	{
		gslSendTaggedMessage(e,ColorRed, "Reward", "You do not know recipe %s", pchRecipeName );
		return 0;
	}

	pDef = GET_REF(pRecipe->hItem);

	returnVal = gslCreateManagedReturnValLoggedEnt("ItemConstruct", e, NULL, NULL);

	AutoTrans_tr_InventoryConstructItem(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), pchRecipeName);
	return 1;
}

bool item_IsDeconstructableForParts( Entity * e, Item *pItem )
{
	ItemDef *pDef = GET_REF(pItem->hItem);
	ItemDef *pRecipe = pDef ? GET_REF(pDef->hCraftRecipe) : NULL;
	//ItemDef *pComponent = GET_REF(pItem->hComponent);

	if( pRecipe )//|| pComponent)
		return 1;

	return 0;
}

// Destroy the item in this inventory bag and slot for components.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory);
bool item_DeconstructForParts( Entity *e, S32 BagID, S32 iSlot)
{
	Item *pItem = inv_GetItemFromBag( e, BagID, iSlot);
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL ;
	TransactionReturnValStruct* returnVal;


	if (!pItem || !pDef)
		return false;

	returnVal = gslCreateManagedReturnValLoggedEnt("ItemDeconstruct", e, NULL, NULL);

	AutoTrans_tr_InventoryDeconstuctItemForParts(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), BagID, iSlot );

	return true;
}

// Destroy an item with this name in your main bag for components.
AUTO_COMMAND ACMD_CATEGORY(Standard, Inventory);
void deconstructForParts( Entity * e, char * pchName )
{
	Item * pItem = inv_GetItemFromBagIDByName(e, InvBagIDs_Inventory, pchName);

	if( pItem )
	{
		InvBagIDs tmpBagID;
		int tmpSlotIdx;

		inv_GetSlotByItemName(e, InvBagIDs_Inventory, pchName, &tmpBagID, &tmpSlotIdx);

		item_DeconstructForParts(e, tmpBagID, tmpSlotIdx);
	}
	else
		gslSendTaggedMessage(e,ColorRed, "Reward", "Item %s Not Found", pchName );
}


bool item_IsDeconstructableForResource( Entity * e, Item *pItem )
{
	ItemDef *pDef = GET_REF(pItem->hItem);
	
	if(pDef && pDef->flags&kItemDefFlag_Enigma)
		return 0;
	return 1;
}

// Destroy the item in this bag and slot for resource points.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory);
bool item_DeconstructForResource( Entity *e, S32 BagID, S32 iSlot)
{

//!!!! under construction
	return 0;
#if 0
	Item *pItem = inv_GetItemFromBag( e, BagID, iSlot);
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if (!pDef)
		return 0;

	if( !item_IsDeconstructableForResource(e, pItem) )
	{
		gslSendTaggedMessage(e,ColorRed, "Reward", "Item %s Not Deconstructable for resource", pDef->pchName );
		return 0;
	}

	if(AutoTrans_tr_InventoryDeconstuctItemForResource(0, GetAppGlobalType(), entGetType(e), entGetContainerID(e), BagID, iSlot ))
	{
		gslSendTaggedMessage(e,ColorGreen, "Reward","You received %i resource (%i total).", (int)(pDef->fValue/2), e->pPlayer->inventory->iResource  );
	}
	return 1;
#endif
}

// Destroy an item with this name in your main bag for resource.
AUTO_COMMAND ACMD_CATEGORY(Standard, Inventory);
void deconstructForResource( Entity * e, char * pchName )
{
	InvBagIDs BagID = InvBagIDs_None;
	int SlotIdx = -1;
	Item *pItem = NULL;

	if (!e)
		return;

	inv_GetSlotByItemName(e, InvBagIDs_Inventory, (char*)pchName, &BagID, &SlotIdx);

	if ( SlotIdx == -1 )
		return;

	pItem = inv_GetItemFromBag( e, InvBagIDs_Inventory, SlotIdx);

	if( pItem )
		item_DeconstructForResource(e, InvBagIDs_Inventory, SlotIdx);
	else
		gslSendTaggedMessage(e,ColorRed, "Reward", "Item %s Not Found", pchName );
}


