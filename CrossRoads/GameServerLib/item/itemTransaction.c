#include "itemCommon.h"
#include "Expression.h"
#include "wlCostume.h"
#include "Entity.h"
#include "Entity.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "itemCraft.h"
#include "Powers.h"
#include "rewardCommon.h"
#include "transactionsystem.h"
#include "earray.h"
#include "stdtypes.h"
#include "estring.h"
#include "TransactionOutcomes.h"
#include "StringCache.h"
#include "rand.h"
#include "Character.h"
#include "AutoGen/Character_h_ast.h"
#include "Character_combat.h"
#include "Character.h"
#include "Character.h"
#include "Powers.h"
#include "PowerTree.h"
#include "gslPowerTransactions.h"
#include "AutoGen/Powers_h_ast.h"
#include "gslEntity.h"
#include "objTransactions.h"
#include "gslEventSend.h"
#include "inventoryCommon.h"
#include "gslSendToClient.h"
#include "gslLogSettings.h"
#include "Color.h"
#include "PowerHelpers.h"
#include "reward.h"
#include "algoItem.h"
#include "algoPet.h"
#include "algoItemCommon.h"
#include "StructDefines.h"
#include "inventoryTransactions.h"
#include "AutoTransDefs.h"
#include "Guild.h"
#include "itemServer.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "SavedPetCommon.h"
#include "storeCommon.h"
#include "AutoGen/storeCommon_h_ast.h"
#include "tradeCommon.h"
#include "GameStringFormat.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "Microtransactions_Transact.h"
#include "NotifyEnum.h"
#include "accountnet_h_ast.h"
#include "AccountProxyCommon.h"

#include "itemTransaction.h"
#include "LoggedTransactions.h"
#include "loggingEnums.h"
#include "GroupProjectCommon.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"

#include "AutoGen/inventoryCommon_h_ast.h"
#include "GameAccountData_h_ast.h"


extern AlgoTables g_AlgoTables;

typedef struct RemoveItemCBData
{
	GlobalType ownerType;
	ContainerID ownerID;
	const ItemDef* pItemDef;

	TransactionReturnCallback CallbackFunc;
	void *pUserData;
} RemoveItemCBData;


bool itemtransaction_MoveItemAcrossEnts(SA_PARAM_NN_VALID TransactionReturnVal* pReturnVal,
										S32 iSrcType, U32 uiSrcID, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID,
										S32 iDstType, U32 uiDstID, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iCount,
										const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason)
{
	bool bSuccess = false;
	U32* eaSrcPets = NULL;
	U32* eaDstPets = NULL;
	Entity* pEntSrc = NULL;
	Entity* pEntDst = NULL;
	ea32Create(&eaSrcPets);
	ea32Create(&eaDstPets);

	// If both are players, then we need the pets for both players to handle the unique test
	// If one is a player and the other is a saved pet, we don't need to include the other pets since
	//    there is no gaining of an item and therefore we should not need a unique test
	// If one is a player and other is not a player or saved pet (such as a shared bank)
	//    then we need to lock the pets on the player to do the unique test.
	// If neither is a player, such as pet to pet, then we don't need to lock other pets.
	if ((iSrcType == GLOBALTYPE_ENTITYPLAYER) && (iDstType != GLOBALTYPE_ENTITYSAVEDPET))
	{
		pEntSrc = entFromContainerIDAnyPartition(iSrcType, uiSrcID);
		Entity_GetPetIDList(pEntSrc, &eaSrcPets);
	}
	if ((iDstType == GLOBALTYPE_ENTITYPLAYER) && (iSrcType != GLOBALTYPE_ENTITYSAVEDPET))
	{
		pEntDst = entFromContainerIDAnyPartition(iDstType, uiDstID);
		Entity_GetPetIDList(pEntDst, &eaDstPets);
	}

	if (uiSrcID != uiDstID || iSrcType != iDstType)
	{
		GameAccountDataExtract *pExtract = pEntSrc ? entity_GetCachedGameAccountDataExtract(pEntSrc) : NULL;


		AutoTrans_inv_ent_tr_MoveItemAcrossEnts(pReturnVal, GetAppGlobalType(), 
			iSrcType, uiSrcID, 
			GLOBALTYPE_ENTITYSAVEDPET, &eaSrcPets,
			iSrcBagID, iSrcSlotIdx, uSrcItemID,
			iDstType, uiDstID, 
			GLOBALTYPE_ENTITYSAVEDPET, &eaDstPets,
			iDstBagID, iDstSlotIdx, uDstItemID, iCount, pSrcReason, pDestReason, pExtract); 

		bSuccess = true;
	}
	else
	{
		ErrorfForceCallstack("Error: MoveItemAcrossEnts has a Src and Dst entity with matching container IDs and types");
	}

	ea32Destroy(&eaSrcPets);
	ea32Destroy(&eaDstPets);
	return bSuccess;
}

bool itemtransaction_MoveItemGuildAcrossEnts(const char* pchActionName, TransactionReturnCallback cbFunc, void* cbData,
											 Entity* pPlayerEnt, Entity* pTransEnt, Guild* pGuild, 
											 int bSrcGuild, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID, int iSrcEPValue, int iSrcCount, 
											 int bDstGuild, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iDstEPValue)
{
	bool bSuccess = false;
	U32* eaPets = NULL;
	ea32Create(&eaPets);
	if (!pchActionName || !pchActionName[0])
		pchActionName = "ItemMove";
	if (entGetType(pTransEnt) == GLOBALTYPE_ENTITYPLAYER)
	{
		Errorf("Error: MoveItemGuildAcrossEnts is trying to operate on a player, use MoveItemGuild instead.");
	}
	if (!bSrcGuild || !bDstGuild)
	{
		S32 iPetIdx;
		Entity_GetPetIDList(pPlayerEnt, &eaPets);
		iPetIdx = ea32Find(&eaPets, entGetContainerID(pTransEnt));
		if (iPetIdx >= 0)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			TransactionReturnVal* pReturnVal = LoggedTransactions_CreateManagedReturnValEnt(pchActionName, pPlayerEnt, cbFunc, cbData);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pPlayerEnt, "Item:MoveItemGuildAcrossEnts", pGuild->pcName);

			itemtransaction_MoveItemGuildAcrossEnts_Wrapper(pReturnVal, GetAppGlobalType(), 
							entGetType(pPlayerEnt), entGetContainerID(pPlayerEnt), 
							iPetIdx,
							GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
							GLOBALTYPE_GUILD, pGuild->iContainerID, GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID,
							bSrcGuild, iSrcBagID, iSrcSlotIdx, uSrcItemID, iSrcEPValue, iSrcCount, 
							bDstGuild, iDstBagID, iDstSlotIdx, uDstItemID, iDstEPValue, &reason, pExtract);
			bSuccess = true;
		}
		else
		{
			Errorf("Error: MoveItemGuildAcrossEnts was called with an invalid pet.");
		}
	}
	else
	{
		Errorf("Error: MoveItemGuildAcrossEnts was called with an item move from guild bag to guild bag.");
	}
	if (!bSuccess)
	{
		//TODO(MK): what to do here?
	}
	ea32Destroy(&eaPets);
	return bSuccess;
}

static void itemtransaction_RemoveItemFromBag_CB(TransactionReturnVal* returnVal, RemoveItemCBData* pData)
{
	if(pData)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			Entity* pEnt = entFromContainerIDAnyPartition(pData->ownerType, pData->ownerID);
			if(pEnt && pData->pItemDef && (pData->pItemDef->flags & kItemDefFlag_DoorKey)) {
				ClientCmd_ui_ScanInventoryForWaypoints(pEnt, true);
			}
		}
		if(pData->CallbackFunc)
			pData->CallbackFunc(returnVal, pData->pUserData);
	}

	SAFE_FREE(pData);
}

void itemtransaction_RemoveItemFromBagEx(Entity *pEnt, 
										 InvBagIDs BagID, const ItemDef *pItemDef, int iSlot, U64 uItemID, int iCount, 
										 int iPowerExpiredBag, int iPowerExpiredSlot, const ItemChangeReason *pReason, 
										 TransactionReturnCallback userFunc, void* userData)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	TransactionReturnVal* returnVal;
	RemoveItemCBData *cbData;
	
	cbData = calloc(1,sizeof(RemoveItemCBData));
	cbData->ownerID = entGetContainerID(pEnt);
	cbData->ownerType = entGetType(pEnt);
	cbData->pItemDef = pItemDef;
	cbData->CallbackFunc = userFunc;
	cbData->pUserData = userData;

	if (pItemDef && BagID == InvBagIDs_None) {
		//special case for remove item from all bags
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("AllBagsRemoveItem", pEnt, itemtransaction_RemoveItemFromBag_CB, cbData);
		AutoTrans_inventory_RemoveItemByDefName(returnVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pItemDef->pchName, iCount, pReason, pExtract);
	} else if (iSlot >= 0 && BagID != InvBagIDs_None) {
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("RemoveItem", pEnt, itemtransaction_RemoveItemFromBag_CB, cbData);
		if (inv_IsGuildBag(BagID)) {
			S32 iGuildID = guild_GetGuildID(pEnt);
			if (iGuildID) {
				AutoTrans_inv_guild_tr_RemoveItem(returnVal, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID, GLOBALTYPE_ENTITYGUILDBANK, iGuildID, BagID, iSlot, iCount, pReason);
			} else {
				free(returnVal);
			}
		} else {
			if (iPowerExpiredBag == InvBagIDs_None || iPowerExpiredSlot < 0)
			{
				AutoTrans_inv_ent_tr_RemoveItem(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					BagID, iSlot, uItemID, iCount, pReason, pExtract);
			}
			else
			{
				AutoTrans_inv_ent_tr_RemoveItemEx(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					BagID, iSlot, uItemID, iCount,
					iPowerExpiredBag, iPowerExpiredSlot, pReason, pExtract);
			}
		}
	} else if (pItemDef) {
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("RemoveItemByDef", pEnt, itemtransaction_RemoveItemFromBag_CB, cbData);
		if (iPowerExpiredBag == InvBagIDs_None || iPowerExpiredSlot < 0)
		{
			AutoTrans_inventorybag_RemoveItemByDefName(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				BagID, pItemDef->pchName, iCount, pReason, pExtract);
		}
		else
		{
			AutoTrans_inventorybag_RemoveItemByDefNameEx(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				BagID, pItemDef->pchName, iCount,
				iPowerExpiredBag, iPowerExpiredSlot, pReason, pExtract);
		}
	}
}

void itemtransaction_RemoveItemFromBag(Entity *pEnt, InvBagIDs BagID, const ItemDef *pItemDef, int iSlot, U64 uItemID, int iCount, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void* userData)
{
	itemtransaction_RemoveItemFromBagEx(pEnt, BagID, pItemDef, iSlot, uItemID, iCount, 0, -1, pReason, userFunc, userData);
}

void rewardbagsdata_Log(GiveRewardBagsData *pRewardBagsData, char **pestrSuccess)
{
	int i;
	if(!pRewardBagsData || !pestrSuccess)
		return;
	for (i = 0; i < eaSize(&pRewardBagsData->ppRewardBags); i++)
	{
		InventoryBag *pRewardBag = pRewardBagsData->ppRewardBags[i];
		BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pRewardBag));
		char *div = "";
		for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			int count = bagiterator_GetItemCount(iter);
			Item *pItem  = (Item*)bagiterator_GetItem(iter);
			estrConcatf(pestrSuccess, "%s%i%s", div, count, item_GetLogString(pItem));
			div = ",";
		}
		bagiterator_Destroy(iter);
	}
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pchar.Hpath, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Skilltype, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems")
ATR_LOCKS(eaPets, ".pInventoryV2.Peaowneduniqueitems, .Pcritter.Petdef");
enumTransactionOutcome tr_InventoryCraftItem(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
											 char *pchRecipeName, GiveRewardBagsData *pRewardBagsData, int iSkillMax,
											 const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	Item *item;
	ItemDef *pDef; 
	ItemDef *pResult;
	char *estrSuccess = NULL;
	int i;
	int iPlayerSkill = inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pEnt, "Skilllevel");
	BagIterator *recipe_iter = NULL;

	if (ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		TRANSACTION_RETURN_LOG_FAILURE("Invalid params");

	// get a pointer to the recipe item
	recipe_iter = inv_bag_trh_FindItemByDefName(ATR_PASS_ARGS, pEnt,InvBagIDs_Recipe,pchRecipeName,NULL);
	
	if (!recipe_iter)
		TRANSACTION_RETURN_LOG_FAILURE( "Player does not know recipe %s", pchRecipeName );
     
	pDef = bagiterator_GetDef(recipe_iter);
	bagiterator_Destroy(recipe_iter);
	if (!pDef)
		TRANSACTION_RETURN_LOG_FAILURE( "Invalid def name");

	if (iSkillMax >= 0 && pDef->pRestriction && pDef->pRestriction->iSkillLevel > (U32) iSkillMax)
		TRANSACTION_RETURN_LOG_FAILURE( "Recipe '%s' skill level exceeds maximum allowed level of %i", pDef->pchName, iSkillMax);

	if (iSkillMax >= 0 && pDef->pRestriction && pDef->pRestriction->iSkillLevel > (U32) iPlayerSkill)
		TRANSACTION_RETURN_LOG_FAILURE( "Recipe '%s' skill level exceeds your player level of %i", pDef->pchName, iSkillMax);

	// make sure the can execute
	//if( !item_IsContructable(ent,pRecipe) )
	//	TRANSACTION_RETURN(TRANSACTION_OUTCOME_FAILURE, "Player cannot create item from recipe %s", recipeName );

	// remove required resources
	if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, "resources", -((S32)pDef->pCraft->iResource), pReason ) )
		return TRANSACTION_OUTCOME_FAILURE;

	for(i = eaSize(&pDef->pCraft->ppPart)-1; i>=0; i--)
	{
		ItemDef *pPart = GET_REF(pDef->pCraft->ppPart[i]->hItem);

		if (!pPart)
		{
			TRANSACTION_RETURN_LOG_FAILURE( "Could not remove Recipe component" );
		}

		if(inventory_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, pPart->pchName, item_GetComponentCount(pDef->pCraft->ppPart[i]), pReason, pExtract) == TRANSACTION_OUTCOME_FAILURE)
			TRANSACTION_RETURN_LOG_FAILURE( "Could not remove Recipe component %s", pPart->pchName );
	}

	pResult = GET_REF(pDef->pCraft->hItemResult);
	if (!pResult)
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Could not access Recipe product def" );
	}

	item = item_FromDefName(pResult->pchName);

	CONTAINER_NOCONST(Item, item)->count = pDef->pCraft->iResultCount;

	if (inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, InvBagIDs_Inventory, -1, item, pResult->pchName, 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
	{
		StructDestroySafe(parse_Item,&item);
		TRANSACTION_RETURN_LOG_FAILURE( "Could not add Recipe product item(s)" );
	}
	StructDestroySafe(parse_Item,&item);

	estrPrintf(&estrSuccess, "Craft %s(%i) %i[I(%s)]->{",
		StaticDefineIntRevLookup(SkillTypeEnum, pEnt->pPlayer->SkillType),
		inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "SkillLevel"),
		pDef->pCraft->iResultCount,
		pResult->pchName);

	// log contents of reward bags
	rewardbagsdata_Log(pRewardBagsData, &estrSuccess);

	if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, pRewardBagsData, kRewardOverflow_DisallowOverflowBag, NULL, pReason, pExtract, NULL))
	{
		estrDestroy(&estrSuccess);
		TRANSACTION_RETURN_LOG_FAILURE( "Could not give rewards" );
	}

	estrConcatf(&estrSuccess, "}");
	TRANSACTION_APPEND_LOG_SUCCESS("%s", estrSuccess);
	estrDestroy(&estrSuccess);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp");
enumTransactionOutcome tr_InventoryRemoveRecipeComponents(ATR_ARGS, NOCONST(Entity)* pEnt, char* recipeName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	ItemCraftingComponent **eaComponents = NULL;
	ItemDef* pRecipe = item_DefFromName(recipeName);
	NOCONST(Item) *pRemovedItem = NULL;
	int i;

	if (!pRecipe)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Couldn't find recipe.");
	}

	item_GetAlgoIngredients(pRecipe, &eaComponents, 0, 0, 0);

	// Remove the components
	for(i = eaSize(&eaComponents)-1; i>=0; i--) {
		ItemDef *pPart = GET_REF(eaComponents[i]->hItem);

		if (!pPart) {
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Could not remove Recipe component");
		}

		if(inventory_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, pPart->pchName, (int)(eaComponents[i]->fCount), pReason, pExtract) == TRANSACTION_OUTCOME_FAILURE) {
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Could not remove Recipe component %s", pPart->pchName);
		}
	}
	eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);

	TRANSACTION_APPEND_LOG_SUCCESS("Successfully removed all components for recipe %s", pRecipe->pchName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pchar.Hpath, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Skilltype, .Hallegiance, .Hsuballegiance, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome tr_InventoryCraftAlgoItem(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
												 NON_CONTAINER CraftData *pCraftData, GiveRewardBagsData *pRewardBagsData, 
												 int iSkillMax, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item) *pRecipeItem = NULL;
	NOCONST(Item) *pRemovedItem = NULL;
	NOCONST(Item) *pFinalItem = NULL;
	ItemDef *pBaseRecipeDef = NULL;
	ItemDef *pBaseItemDef = NULL;
	ItemDef *pQualityRecipeDef = NULL;
	NOCONST(ItemPowerDefRef) **eaItemPowerDefRefs = NULL;
	ItemDef *pResultDef = NULL;
	ItemCraftingComponent **eaComponents = NULL;
	AlgoItemLevelsDef *pAlgoItemLevels = NULL;
	char *estrSuccess = NULL;
	const char *pcLogString;
	int i;
	int iLevel;

	// Get ingredients for the base recipe
	pRecipeItem = inv_trh_GetItemFromBagIDByName(ATR_PASS_ARGS, pEnt, InvBagIDs_Recipe, pCraftData->pcBaseItemRecipeName, pExtract);
	if (!pRecipeItem)
		TRANSACTION_RETURN_LOG_FAILURE("Player does not know recipe %s", pCraftData->pcBaseItemRecipeName);
	pBaseRecipeDef = GET_REF(pRecipeItem->hItem);
	if (!pBaseRecipeDef)
		TRANSACTION_RETURN_LOG_FAILURE("Invalid item def name %s", REF_STRING_FROM_HANDLE(pRecipeItem->hItem));
	if (iSkillMax >= 0 && pBaseRecipeDef->pRestriction && pBaseRecipeDef->pRestriction->iSkillLevel > (U32) iSkillMax)
		TRANSACTION_RETURN_LOG_FAILURE( "Recipe '%s' skill level exceeds maximum allowed level of %i", pBaseRecipeDef->pchName, iSkillMax);
	pBaseItemDef = pBaseRecipeDef->pCraft ? GET_REF(pBaseRecipeDef->pCraft->hItemResult) : NULL;
	if (!pBaseItemDef)
		TRANSACTION_RETURN_LOG_FAILURE("Invalid item def name %s", pBaseRecipeDef->pCraft ? REF_STRING_FROM_HANDLE(pBaseRecipeDef->pCraft->hItemResult) : NULL);

	iLevel = pBaseItemDef->iLevel;
	item_GetAlgoIngredients(pBaseRecipeDef, &eaComponents, 0, iLevel, pCraftData->eQuality);
	
	// Validate and get ingredients for all the power recipes
	for (i = 0; i < ITEM_POWER_GROUP_COUNT; i++) {
		ItemDef *pRecipeDef = NULL;
		ItemPowerDef *pPowerDef = NULL;
		NOCONST(ItemPowerDefRef) *pPowerDefRef = NULL;
		pRecipeItem = NULL;
		
		if ((pBaseRecipeDef->Group & (1<<i)) == 0) {
			continue;
		}
		
		if (eaSize(&pCraftData->eaItemPowerRecipes) < (i+1) || !pCraftData->eaItemPowerRecipes[i] || !pCraftData->eaItemPowerRecipes[i][0]) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Base recipe %s requires item power in group %d", pBaseRecipeDef->pchName, i+1);
		}
		
		pRecipeItem = inv_trh_GetItemFromBagIDByName(ATR_PASS_ARGS, pEnt, InvBagIDs_Recipe, pCraftData->eaItemPowerRecipes[i], pExtract);
		if (!pRecipeItem) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Player does not know recipe %s", pCraftData->eaItemPowerRecipes[i]);
		}
		
		pRecipeDef = GET_REF(pRecipeItem->hItem);
		if (!pRecipeDef) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Invalid item def name %s", pCraftData->eaItemPowerRecipes[i]);
		}
		
		if (!(eaiSize(&pRecipeDef->peRestrictBagIDs)==0 || item_MatchAnyRestrictBagIDs(pRecipeDef, pBaseItemDef->peRestrictBagIDs) >= 0)) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Item power recipe %s not allowed in bag %s", pCraftData->eaItemPowerRecipes[i], StaticDefineIntRevLookup(InvBagIDsEnum, eaiGet(&pBaseItemDef->peRestrictBagIDs,0)));
		}
		
		if (pEnt->pPlayer->SkillType != pRecipeDef->kSkillType) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Item power recipe %s does not have the same skill type as the player", pRecipeDef->pchName);
		}
		
		if ((pRecipeDef->Group & (1<<i)) == 0) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Item power recipe %s not allowed is item power group %d", pRecipeDef->pchName, i);
		}
		
		if (!item_GetAlgoIngredients(pRecipeDef, &eaComponents, i, iLevel, pCraftData->eQuality)) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Could not find correct crafting table entry for item power recipe %s at level %d with quality %s",
				pRecipeDef->pchName, iLevel, StaticDefineIntRevLookup(ItemQualityEnum, pCraftData->eQuality));
		}
		
		pPowerDef = GET_REF(pRecipeDef->pCraft->hItemPowerResult);
		if (!pPowerDef) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Invalid power def name %s", REF_STRING_FROM_HANDLE(pRecipeDef->pCraft->hItemPowerResult));
		}
		
		pPowerDefRef = StructCreateNoConst(parse_ItemPowerDefRef);
		SET_HANDLE_FROM_REFDATA(g_hItemPowerDict, pPowerDef->pchName, pPowerDefRef->hItemPowerDef);
		pPowerDefRef->iPowerGroup = i;
		eaPush(&eaItemPowerDefRefs, pPowerDefRef);
	}
	
	// Remove the components
	for(i = eaSize(&eaComponents)-1; i>=0; i--) {
		ItemDef *pPart = GET_REF(eaComponents[i]->hItem);
		
		if (!pPart) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Could not remove Recipe component");
		}
		
		
		
		if (inventory_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, pPart->pchName, (int)(eaComponents[i]->fCount), pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS) {
			eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
			TRANSACTION_RETURN_LOG_FAILURE("Could not remove Recipe component %s", pPart->pchName);
		}
	}
	eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
	
	// Actually make the item
	pFinalItem = item_CreateAlgoItem(ATR_PASS_ARGS, pBaseRecipeDef, pCraftData->eQuality, (ItemPowerDefRef**) eaItemPowerDefRefs);
	eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
	if (!pFinalItem) {
		TRANSACTION_RETURN_LOG_FAILURE("Could not create algo item");
	}
	pFinalItem->count = pBaseRecipeDef->pCraft->iResultCount;
	// Need to do this ahead of time, because inv_ent_trh_AddItem destroys the item
	pcLogString = item_GetLogString((Item*) pFinalItem);
	
	if (inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, InvBagIDs_Inventory, -1, (Item*)pFinalItem, pBaseRecipeDef->pchName, 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS) {
		StructDestroyNoConstSafe(parse_Item, &pFinalItem);
		TRANSACTION_RETURN_LOG_FAILURE("Could not add Recipe product item(s)");
	}

	StructDestroyNoConstSafe(parse_Item, &pFinalItem);
	estrPrintf(&estrSuccess, "Craft %s(%i) %i%s->{",
		StaticDefineIntRevLookup(SkillTypeEnum, pEnt->pPlayer->SkillType),
		inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "SkillLevel"),
		pBaseRecipeDef->pCraft->iResultCount,
		pcLogString);
	
	// log contents of reward bags
	rewardbagsdata_Log(pRewardBagsData, &estrSuccess);
	
	if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, pRewardBagsData, kRewardOverflow_DisallowOverflowBag, NULL, pReason, pExtract, NULL)) {
		estrDestroy(&estrSuccess);
		TRANSACTION_RETURN_LOG_FAILURE("Could not give rewards");
	}
	
	estrConcatf(&estrSuccess, "}");
	TRANSACTION_APPEND_LOG_SUCCESS("%s", estrSuccess);
	estrDestroy(&estrSuccess);
	return TRANSACTION_OUTCOME_SUCCESS;
}
/*
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype");
enumTransactionOutcome tr_ItemInfuse(ATR_ARGS, NOCONST(Entity)* pEnt, int iBagID, int iBagSlotIdx, int iInfuseSlot, const char *pchItemOption, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item) *pItem;
	ItemDef *pItemDef, *pItemDefOption;
	InfuseSlotDef *pInfuseSlotDef;
	InfuseOption *pInfuseOption;
	NOCONST(InfuseSlot) *pInfuseSlot;
	int iRank = 0;
	int iCost;
	
	pItemDefOption = RefSystem_ReferentFromString(g_hItemDict,pchItemOption);
	if(!pItemDefOption || pItemDefOption->eType!=kItemType_Numeric)
		TRANSACTION_RETURN_LOG_FAILURE("Item %s does not exist or is not a numeric", pchItemOption);

	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, false, iBagID, iBagSlotIdx, pExtract);
	if(ISNULL(pItem))
		TRANSACTION_RETURN_LOG_FAILURE("No Item in bag %d slot %d", iBagID, iBagSlotIdx);

	pItemDef = GET_REF(pItem->hItem);
	if(!pItemDef)
		TRANSACTION_RETURN_LOG_FAILURE("Item has no ItemDef");

	pInfuseSlotDef = item_GetInfuseSlotDef(pItemDef, iInfuseSlot);
	if(!pInfuseSlotDef)
		TRANSACTION_RETURN_LOG_FAILURE("ItemDef does not have valid InfuseSlotDef at index %d", iInfuseSlot);

	pInfuseOption = infuse_GetDefOption(pInfuseSlotDef,pItemDefOption);
	if(!pInfuseOption)
		TRANSACTION_RETURN_LOG_FAILURE("InfuseSlotDef does not have an option for %s", pchItemOption);

	pInfuseSlot = eaIndexedGetUsingInt(&pItem->ppInfuseSlots,iInfuseSlot);
	if(NONNULL(pInfuseSlot) && GET_REF(pInfuseSlot->hItem)==pItemDefOption)
		iRank = pInfuseSlot->iRank + 1;

	ANALYSIS_ASSUME(pInfuseOption->ppRanks); // sigh
	if(iRank >= eaSize(&pInfuseOption->ppRanks))
		TRANSACTION_RETURN_LOG_FAILURE("InfuseSlotOption does not have any more ranks");

	iCost = pInfuseOption->ppRanks[iRank]->iCost;
	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pEnt,true,pchItemOption,-iCost,pReason))
		TRANSACTION_RETURN_LOG_FAILURE("Unable to pay cost %d",iCost);

	if(ISNULL(pInfuseSlot))
	{
		pInfuseSlot = CONTAINER_NOCONST(InfuseSlot, StructCreate(parse_InfuseSlot));
		SET_HANDLE_FROM_REFERENT(g_hInfuseSlotDict,pInfuseSlotDef,pInfuseSlot->hDef);
		SET_HANDLE_FROM_REFERENT(g_hItemDict,pItemDefOption,pInfuseSlot->hItem);
		pInfuseSlot->iIndex = iInfuseSlot;
		eaIndexedEnableNoConst(&pItem->ppInfuseSlots,parse_InfuseSlot);
		eaIndexedAdd(&pItem->ppInfuseSlots,pInfuseSlot);
	}
	else if(GET_REF(pInfuseSlot->hItem)!=pItemDefOption)
	{
		// Replacing an existing slot with a new option, wipe the refund tracker
		SET_HANDLE_FROM_REFERENT(g_hItemDict,pItemDefOption,pInfuseSlot->hItem);
		pInfuseSlot->iRefund = 0;
	}

	pInfuseSlot->iRank = iRank;
	pInfuseSlot->iRefund += iCost;

	// Rather than try to duplicate the code to find Power*s on the Item instance, remove them
	//  and make new ones for the upgrade, etc, we'll just do the slightly sleazy fix, which is
	//  to call the fixup code that already does all that hard work.
	item_trh_FixupPowers(pItem);
	item_trh_FixupPowerIDs(pEnt, pItem);

	TRANSACTION_RETURN_LOG_SUCCESS("InfuseSlot set to option %s rank %d paid %d",pchItemOption,iRank,iCost);
}
*/

// Removes the cost of an item from a player
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags");
bool item_trh_ItemRemoveCost(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const ItemDef *pItemDef, StoreItemCostInfoList* pCostInfoList, int iBuybackCost, int iCount, const char *pcCurrency, const ItemDefRef **pOverrideValueRecipes, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	const ItemDef **ppValueRecipes = NULL;
	S32 i;

	// make sure the item def exists
	if (!pItemDef)
	{
		TRANSACTION_APPEND_LOG_FAILURE("No item could be found");
		return false;
	}

	// check for value recipes

	// in this case, if the cost is being calculated for an ItemValue type of item, the item
	// def MUST have a result item
	if (pItemDef->eType == kItemType_ItemValue)
	{
		eaPush(&ppValueRecipes, pItemDef);
		pItemDef = GET_REF(pItemDef->pCraft->hItemResult);
		if (!pItemDef)
		{
			TRANSACTION_APPEND_LOG_FAILURE("Value recipe '%s' has no result item", ppValueRecipes[0]->pchName);
			eaDestroy(&ppValueRecipes);
			return false;
		}
	}
	else
	{
		const ItemDefRef **tmpValueRecipes = pOverrideValueRecipes;

		// if there are no override Value Recipes, then use the ones from the item
		if ( eaSize(&tmpValueRecipes) == 0 )
		{
			tmpValueRecipes = pItemDef->ppValueRecipes;
		}
		for (i = 0; i < eaSize(&tmpValueRecipes); i++)
		{
			ItemDef *pValueRecipe = SAFE_GET_REF(tmpValueRecipes[i], hDef);
			if (pValueRecipe)
				eaPush(&ppValueRecipes, pValueRecipe);
		}
	}

	// Take cost from player
	// If a currency is specified, use it, otherwise fall back on the value recipe
	// If both a currency and a value recipe are present, it's because bForceUseCurrency is on
	if (pcCurrency && pcCurrency[0])
	{
		S32 iCost = 0;
		S32 iTotalCost;
		S32 iStoreBuyCount = iCount;

		if (pCostInfoList)
		{
			for (i = eaSize(&pCostInfoList->eaCostInfo)-1; i >= 0; i--)
			{
				StoreItemCostInfo* pCostInfo = pCostInfoList->eaCostInfo[i];
				ItemDef* pCostItemDef = GET_REF(pCostInfo->hItemDef);
				if (pCostItemDef && stricmp(pCostItemDef->pchName, pcCurrency)==0)
				{
					iCost = pCostInfo->iCount;
					break;
				}
			}
			if (i < 0)
			{
				TRANSACTION_APPEND_LOG_FAILURE("Cost numeric %s not found", pcCurrency);
				eaDestroy(&ppValueRecipes);
				return false;
			}
			iStoreBuyCount = iCount / MAX(pCostInfoList->iStoreItemCount, 1);
		}
		else
		{
			iCost = iBuybackCost;
		}

		iTotalCost = iCost * iStoreBuyCount;

		eaDestroy(&ppValueRecipes);
		if (inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pcCurrency, -iTotalCost, pReason))
			return true;
		else
		{
			TRANSACTION_APPEND_LOG_FAILURE("Could not remove %i '%s' from player", iTotalCost, pcCurrency);
			return false;
		}
	}
	else
	{
		S32 j, k;
		S32 iStoreBuyCount = iCount;
		ItemDef **ppCostInfoComponents = NULL;

		if (pCostInfoList) {
			iStoreBuyCount = iCount / MAX(pCostInfoList->iStoreItemCount, 1);
		}

		// do validation on the value recipes
		if (!ppValueRecipes)
		{
			TRANSACTION_APPEND_LOG_FAILURE("Neither a currency nor a value recipe was specified for the cost of '%s'", pItemDef->pchName);
			return false;
		}

		for (i = 0; i < eaSize(&ppValueRecipes); i++)
		{
			if (!ppValueRecipes[i]->pCraft || eaSize(&ppValueRecipes[i]->pCraft->ppPart) == 0)
			{
				TRANSACTION_APPEND_LOG_FAILURE("Value recipe '%s' has no components", ppValueRecipes[i]->pchName);
				eaDestroy(&ppValueRecipes);
				eaDestroy(&ppCostInfoComponents);
				return false;
			}

			for (j = eaSize(&ppValueRecipes[i]->pCraft->ppPart) - 1; j >= 0; j--)
			{
				ItemDef *pComponentDef = GET_REF(ppValueRecipes[i]->pCraft->ppPart[j]->hItem);
				F32 fCost = 0.0f;
				S32 iTotalCost;

				if (!pComponentDef)
				{
					TRANSACTION_APPEND_LOG_FAILURE("Component '%s' does not exist", REF_STRING_FROM_HANDLE(ppValueRecipes[i]->pCraft->ppPart[j]->hItem));
					eaDestroy(&ppValueRecipes);
					eaDestroy(&ppCostInfoComponents);
					return false;
				}

				// If this component was already found in the CostInfo list, skip it 
				if (eaFind(&ppCostInfoComponents, pComponentDef) >= 0)
				{
					continue;
				}

				if (pCostInfoList)
				{
					for (k = eaSize(&pCostInfoList->eaCostInfo)-1; k >= 0; k--)
					{
						if (pComponentDef == GET_REF(pCostInfoList->eaCostInfo[k]->hItemDef))
						{
							fCost = pCostInfoList->eaCostInfo[k]->iCount;
							break;
						}
					}
					if (k < 0)
					{
						TRANSACTION_APPEND_LOG_FAILURE("Cost item %s not found", pComponentDef->pchName);
						eaDestroy(&ppValueRecipes);
						eaDestroy(&ppCostInfoComponents);
						return false;
					}
					else
					{
						eaPush(&ppCostInfoComponents, pComponentDef);
					}
				}
				else
				{
					fCost = ppValueRecipes[i]->pCraft->ppPart[j]->fCount;
				}

				iTotalCost = (S32)(fCost * iStoreBuyCount);

				if (pComponentDef->eType == kItemType_Numeric)
				{
					if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pComponentDef->pchName, -iTotalCost, pReason))
					{
						TRANSACTION_APPEND_LOG_FAILURE("Could not remove %i '%s' from player", iTotalCost, pComponentDef->pchName);
						eaDestroy(&ppValueRecipes);
						eaDestroy(&ppCostInfoComponents);
						return false;
					}
				}
				else
				{
					int res = inventory_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, pComponentDef->pchName, iTotalCost, pReason, pExtract);

					if (res != TRANSACTION_OUTCOME_SUCCESS)
					{
						TRANSACTION_APPEND_LOG_FAILURE("Could not remove %i of item '%s' from player", iTotalCost, pComponentDef->pchName);
						eaDestroy(&ppValueRecipes);
						eaDestroy(&ppCostInfoComponents);
						return false;
					}
				}
			}
		}

		eaDestroy(&ppValueRecipes);
		eaDestroy(&ppCostInfoComponents);
		return true;
	}
}

// Buy an item with a costume unlock by unlocking the costume directly
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Accountid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Ppinventorybags, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Pplayer.Pugckillcreditlimit, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype")
ATR_LOCKS(pProxyLock, ".Plock.Uaccountid, .Plock.Pkey, .Plock.Result, .Plock.Fdestroytime, .Plock.Etransactiontype");
enumTransactionOutcome tr_ItemBuyCostume(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(AccountProxyLockContainer)* pProxyLock, ItemDef *pItemDef, StoreItemCostInfoList* pCostInfoList, const char *pcCurrency, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	bool bUnlockedSomething = false;
	NOCONST(Item) *pItem = NULL;

	if(ISNULL(pEnt))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No entity player to perform the actions.");
	}

	pItem = CONTAINER_NOCONST(Item, item_FromEnt(pEnt, pItemDef->pchName,0,NULL,0));

	// make sure the specified item exists
	if (!pItemDef || ISNULL(pItem))
		TRANSACTION_RETURN_LOG_FAILURE("No item could be found");

	//Bind the item
	pItem->flags |= kItemFlag_Bound;

	// remove the cost of the item, depending on whether it's a normal purchase or AS key purchase
	if (ISNULL(pProxyLock) && !item_trh_ItemRemoveCost(ATR_PASS_ARGS, pEnt, pItemDef, pCostInfoList, 0, 1, pcCurrency, NULL, pReason, pExtract)) {
		TRANSACTION_RETURN_LOG_FAILURE("The cost of item [%s<%"FORM_LL"u>] could not be deducted from player", pItemDef->pchName, pItem->id);
	} else if (NONNULL(pProxyLock) && !APFinalizeKeyValue(pProxyLock, pEnt->pPlayer->accountID, pcCurrency, APRESULT_COMMIT, TransLogType_Other)) {
		TRANSACTION_RETURN_LOG_FAILURE("The Account Server key '%s' cost of item '%s' could not be deducted from player", pcCurrency, pItemDef->pchName);
	}

	// make sure the costume item has costumes
	if (pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock || eaSize(&pItemDef->ppCostumes) == 0)
		TRANSACTION_RETURN_LOG_FAILURE("Item [%s<%"FORM_LL"u>] does not have costume unlocks.", pItemDef->pchName, pItem->id);

	// unlock the costume(s)
	bUnlockedSomething = inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, false, pItem, pExtract);

	StructDestroyNoConst(parse_Item, pItem);

	if (bUnlockedSomething)
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
		TRANSACTION_RETURN_LOG_FAILURE("Player has already unlocked all costumes on item [%s]", pItemDef->pchName);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Eaunlockedcostumerefs");
enumTransactionOutcome tr_ItemClearUnlockedCostumes(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;

	eaClearStructNoConst(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs, parse_PlayerCostumeRef);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Player entity spends the cost of the item and the item is removed from the target entity
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Accountid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Conowner.Containertype, .Pplayer.Pugckillcreditlimit, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppLiteBags")
ATR_LOCKS(pProxyLock, ".Plock.Uaccountid, .Plock.Pkey, .Plock.Result, .Plock.Fdestroytime, .Plock.Etransactiontype");
enumTransactionOutcome tr_ItemBuyAndRemove(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(AccountProxyLockContainer)* pProxyLock,
										 int TargetBagID, ItemDef *pItemDef, int iCount, StoreItemCostInfoList* pCostInfoList, 
										 const char *pcCurrency, StoreItemDef *pStoreItemDef,
										 const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	// make sure the item specified exists
	if (!pItemDef)
		TRANSACTION_RETURN_LOG_FAILURE("No item could be found");

	// make sure at least one is being purchased
	if (iCount < 1)
		TRANSACTION_RETURN_LOG_FAILURE("Failed to purchase and remove item.  Count (%i) is < 1", iCount);

	// remove the cost of the item, depending on whether it's a normal purchase or AS key purchase
	if (ISNULL(pProxyLock) && !item_trh_ItemRemoveCost(ATR_PASS_ARGS, pEnt, pItemDef, pCostInfoList, 0, iCount, pcCurrency, pStoreItemDef ? pStoreItemDef->ppOverrideValueRecipes : NULL, pReason, pExtract)) {
		TRANSACTION_RETURN_LOG_FAILURE("The cost of item '%s' could not be deducted from player", pItemDef->pchName);
	} else if (NONNULL(pProxyLock) && !APFinalizeKeyValue(pProxyLock, pEnt->pPlayer->accountID, pcCurrency, APRESULT_COMMIT, TransLogType_Other)) {
		TRANSACTION_RETURN_LOG_FAILURE("The Account Server key '%s' cost of item '%s' could not be deducted from player", pcCurrency, pItemDef->pchName);
	}

	if (!invbag_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, TargetBagID, pItemDef->pchName, iCount, pReason, pExtract)) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to remove item '%s' from bag '%s' on entity '%s'", pItemDef->pchName, StaticDefineIntRevLookup(InvBagIDsEnum, TargetBagID), NONNULL(pEnt)?pEnt->debugName:"");
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Player entity spends the cost of the item and the item is removed from the target entity
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Accountid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Ppinventorybags, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppLiteBags")
ATR_LOCKS(pTargetEnt, ".Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
ATR_LOCKS(pProxyLock, ".Plock.Uaccountid, .Plock.Pkey, .Plock.Result, .Plock.Fdestroytime, .Plock.Etransactiontype");
enumTransactionOutcome tr_ItemBuyAndRemoveAcrossEnts(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pTargetEnt, NOCONST(AccountProxyLockContainer)* pProxyLock,
													int TargetBagID, ItemDef *pItemDef, int iCount, StoreItemCostInfoList* pCostInfoList, 
													const char *pcCurrency, StoreItemDef *pStoreItemDef,
													const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	// make sure the item specified exists
	if (!pItemDef)
		TRANSACTION_RETURN_LOG_FAILURE("No item could be found");

	// make sure at least one is being purchased
	if (iCount < 1)
		TRANSACTION_RETURN_LOG_FAILURE("Failed to purchase and remove item.  Count (%i) is < 1", iCount);

	// remove the cost of the item, depending on whether it's a normal purchase or AS key purchase
	if (ISNULL(pProxyLock) && !item_trh_ItemRemoveCost(ATR_PASS_ARGS, pEnt, pItemDef, pCostInfoList, 0, iCount, pcCurrency, pStoreItemDef ? pStoreItemDef->ppOverrideValueRecipes : NULL, pReason, pExtract)) {
		TRANSACTION_RETURN_LOG_FAILURE("The cost of item '%s' could not be deducted from player", pItemDef->pchName);
	} else if (NONNULL(pProxyLock) && !APFinalizeKeyValue(pProxyLock, pEnt->pPlayer->accountID, pcCurrency, APRESULT_COMMIT, TransLogType_Other)) {
		TRANSACTION_RETURN_LOG_FAILURE("The Account Server key '%s' cost of item '%s' could not be deducted from player", pcCurrency, pItemDef->pchName);
	}

	if (!invbag_RemoveItemByDefName(ATR_PASS_ARGS, pTargetEnt, TargetBagID, pItemDef->pchName, iCount, pReason, pExtract)) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to remove item '%s' from bag '%s' on entity '%s'", pItemDef->pchName, StaticDefineIntRevLookup(InvBagIDsEnum, TargetBagID), NONNULL(pTargetEnt)?pTargetEnt->debugName:"");
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pPersistStore, ".Eainventory, .Uversion, .Unextupdatetime");
bool trh_PersistedStore_RemoveItem(ATH_ARG NOCONST(PersistedStore)* pPersistStore, U32 uUniqueID)
{
	if (NONNULL(pPersistStore))
	{
		S32 i;
		for (i = eaSize(&pPersistStore->eaInventory)-1; i >= 0; i--)
		{
			NOCONST(PersistedStoreItem)* pStoreItem = pPersistStore->eaInventory[i];
			if (pStoreItem->uID == uUniqueID)
			{
				StructDestroyNoConst(parse_PersistedStoreItem, eaRemove(&pPersistStore->eaInventory, i));
				pPersistStore->uVersion++;
				pPersistStore->uNextUpdateTime = 0;
				return true;
			}
		}
		return false;
	}
	return true;
}

// Buy an item into the specified inventory, slot of -1 means any available, otherwise adds to specific slot
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Accountid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
ATR_LOCKS(pPersistStore, ".Hstoredef, .Eainventory, .Uversion, .Unextupdatetime")
ATR_LOCKS(pProxyLock, ".Plock.Uaccountid, .Plock.Pkey, .Plock.Result, .Plock.Fdestroytime, .Plock.Etransactiontype")
ATR_LOCKS(projectContainer, ".Projectlist");
enumTransactionOutcome tr_ItemBuyIntoBag(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
										 NOCONST(PersistedStore)* pPersistStore,
										 NOCONST(AccountProxyLockContainer)* pProxyLock,
										 int BagID, int iSlot, ItemDef *pItemDef, int iCount, StoreItemCostInfoList* pCostInfoList, 
										 const char *pcCurrency, int bForceBind, NON_CONTAINER Item * pItem, 
										 const ItemChangeReason *pReason, GameAccountDataExtract *pExtract, 
                                         NOCONST(GroupProjectContainer) *projectContainer,
                                         StoreBuyExtraArgs *extraArgs)
{
	ItemDef *pActualItemDef = NULL;
	ItemDef *pRequiredNumeric = NULL;
	StoreItemDef *pStoreItemDef = NULL;

	// make sure the item specified exists
	if (!pItemDef)
		TRANSACTION_RETURN_LOG_FAILURE("No item could be found");
	
	// make sure at least one is being purchased
	if (iCount < 1)
		return TRANSACTION_OUTCOME_FAILURE;

	// Find StoreItemDef if needed
	if ( NONNULL(extraArgs) )
	{
		StoreDef *pStoreDef = GET_REF(extraArgs->hStoreDef);
		
		if (pStoreDef)
		{
			pStoreItemDef = eaGet(&pStoreDef->inventory, extraArgs->uStoreItemIndex);
		}
	}

    // Handle provisioning.
    if ( NONNULL(extraArgs) && extraArgs->bProvisioned )
    {
        NOCONST(GroupProjectState) *projectState;
        NOCONST(GroupProjectNumericData) *numericData;
        if ( ISNULL(projectContainer) )
        {
            TRANSACTION_RETURN_LOG_FAILURE("tr_ItemBuyIntoBag: No project container for provisioned store.");
        }

        projectState = eaIndexedGetUsingString(&projectContainer->projectList, extraArgs->provisioningProjectName);
        if ( ISNULL(projectState) )
        {
            TRANSACTION_RETURN_LOG_FAILURE("tr_ItemBuyIntoBag: No project state.");
        }
        
        numericData = eaIndexedGetUsingString(&projectState->numericData, extraArgs->provisioningNumericName);
        if ( ISNULL(numericData) )
        {
            TRANSACTION_RETURN_LOG_FAILURE("tr_ItemBuyIntoBag: Numeric not found.");
        }

        // Make sure there is enough provisioning for the number of items being purchased.
        if ( numericData->numericVal < iCount )
        {
            TRANSACTION_RETURN_LOG_FAILURE("tr_ItemBuyIntoBag: Insufficient provisioning.");
        }

        // Subtract number of items being purchased from the provisioning numeric.
        numericData->numericVal -= iCount;
    }
	
	// remove the cost of the item, depending on whether it's a normal purchase or AS key purchase
	if (ISNULL(pProxyLock) && !item_trh_ItemRemoveCost(ATR_PASS_ARGS, pEnt, pItemDef, pCostInfoList, 0, iCount, pcCurrency, pStoreItemDef ? pStoreItemDef->ppOverrideValueRecipes : NULL, pReason, pExtract)) {
		TRANSACTION_RETURN_LOG_FAILURE("The cost of item '%s' could not be deducted from player", pItemDef->pchName);
	} else if (NONNULL(pProxyLock) && !APFinalizeKeyValue(pProxyLock, pEnt->pPlayer->accountID, pcCurrency, APRESULT_COMMIT, TransLogType_Other)) {
		TRANSACTION_RETURN_LOG_FAILURE("The Account Server key '%s' cost of item '%s' could not be deducted from player", pcCurrency, pItemDef->pchName);
	}
	
	// If it's a value recipe, switch out the item def
	if (pItemDef->eType == kItemType_ItemValue) {
		pActualItemDef = pItemDef->pCraft ? GET_REF(pItemDef->pCraft->hItemResult) : NULL;
	} else {
		pActualItemDef = pItemDef;
	}
	if (!pActualItemDef) {
		TRANSACTION_RETURN_LOG_FAILURE("Item value recipe '%s' has no result item, so couldn't be sold", pItemDef->pchName);
	}
	
	// add the item to the bag (as a numeric if appropriate)
	if (pActualItemDef->eType == kItemType_Numeric) {
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pActualItemDef->pchName, iCount, pReason)) {
			TRANSACTION_RETURN_LOG_FAILURE("Failed to add %d of numeric '%s' to bag %i", iCount, pActualItemDef->pchName, BagID);
		}
	} else {
		if (pItem)
		{
			NOCONST(Item)* pItemClone = StructCloneDeConst(parse_Item, pItem);
			ItemDef* pdef = GET_REF(pItem->hItem);
			pItemClone->count = iCount;
			if (inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, BagID, iSlot, (Item*)pItemClone, pdef->pchName, bForceBind ? ItemAdd_ForceBind : 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
			{
				StructDestroyNoConstSafe(parse_Item,&pItemClone);
				TRANSACTION_RETURN_LOG_FAILURE("Failed to add item '%s' (Algo-Item) to bag %i, slot %i", pActualItemDef->pchName, BagID, iSlot);
			}
			StructDestroyNoConstSafe(parse_Item,&pItemClone);
		}
		else
		{
			NOCONST(Item)* item = CONTAINER_NOCONST(Item, item_FromDefName(pActualItemDef->pchName));
			item->count = iCount;
			if(item) 
			{	
				// run the algo pet here
				if(pActualItemDef->eType == kItemType_AlgoPet || pActualItemDef->eType == kItemType_STOBridgeOfficer)
				{
					NOCONST(SpecialItemProps)* pProps = item_trh_GetOrCreateSpecialProperties(item);
					pProps->pAlgoPet = algoPetDef_CreateNew(GET_REF(pActualItemDef->hAlgoPet),GET_REF(pActualItemDef->hPetDef),item_trh_GetQuality(item),item_trh_GetLevel(item),GET_REF(pEnt->hAllegiance),GET_REF(pEnt->hSubAllegiance),NULL);
				}

				if(pActualItemDef->flags & kItemDefFlag_ScaleWhenBought)
					item_trh_SetAlgoPropsLevel(item, entity_trh_GetSavedExpLevelLimited(pEnt));

				if (inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, BagID, iSlot, (Item*)item, pActualItemDef->pchName, bForceBind ? ItemAdd_ForceBind : 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
				{
					StructDestroyNoConstSafe(parse_Item,&item);
					TRANSACTION_RETURN_LOG_FAILURE("Failed to add item '%s' to bag %i, slot %i", pActualItemDef->pchName, BagID, iSlot);
				}
				StructDestroyNoConstSafe(parse_Item,&item);
			}
		}
	}

	// if there is a required numeric, then increment it
	if ( NONNULL(pStoreItemDef) )
	{
		pRequiredNumeric = GET_REF(pStoreItemDef->hRequiredNumeric);
		if ( NONNULL(pRequiredNumeric) )
		{
			if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pRequiredNumeric->pchName, pStoreItemDef->requiredNumericIncr, pReason)) {
				TRANSACTION_RETURN_LOG_FAILURE("Failed to add %d to required numeric '%s'", pStoreItemDef->requiredNumericIncr, pRequiredNumeric->pchName);
			}
		}
	}

	// If there is a persisted store, remove the item from it
    if (!trh_PersistedStore_RemoveItem(pPersistStore, extraArgs ? extraArgs->uStoreItemIndex : 0)) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to remove item %s from persisted store %s", 
			pActualItemDef->pchName, REF_STRING_FROM_HANDLE(pPersistStore->hStoreDef));
	}

	if (NONNULL(pItemDef) && (pItemDef->flags & kItemDefFlag_EquipOnPickup))
		QueueRemoteCommand_AutoEquipItem(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pItemDef->pchName, 0, BagID, iSlot);

	
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Sell an item.  Slot of < 0 means any available, otherwise sell from specific slot.
// Count of < 1 will fail (as making it sell all is more complicated than it sounds,
// and shouldn't really be needed anyway).
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome tr_ItemSellFromBag(ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, const char *pcItemName, int iSlot, U64 uItemID, int iCount, int iCost, const char *pcCurrency, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	ItemDef *pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, pcItemName);
	int success = false;
	
	// Make sure the item specified exists
	if (!pItemDef) {
		TRANSACTION_RETURN_LOG_FAILURE("No item could be found matching: %s", pcItemName);
	}
	
	if (iCount < 1) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (iSlot < 0)
	{
		success = !!invbag_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, BagID, pItemDef->pchName, iCount, pReason, pExtract);
	}
	else
	{
		NOCONST(Item)* pCheckItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, BagID, iSlot, pExtract);
		if (NONNULL(pCheckItem) && pCheckItem->id == uItemID)
		{
			Item* pItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, BagID, iSlot, iCount, pReason, pExtract);
			success = !!pItem;
			StructDestroySafe(parse_Item,&pItem);
		}
	}

	if(success)
	{
		TRANSACTION_APPEND_LOG_SUCCESS("Item sale: Item [%s] successfully removed from [%s] and destroyed", pcItemName, pEnt->debugName);
	}
	else
	{
		TRANSACTION_APPEND_LOG_FAILURE("Item sale: Failed to remove and destroy item [%s] from [%s]", pcItemName, pEnt->debugName);
	}
	
	if (success) {			
		if (inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pcCurrency, (S32)((F32)(iCost*iCount)/*-0.5*/), pReason)) {//removed the -0.5 because it was resulting in incorrect sale values. No idea when it was added in the first place.
			TRANSACTION_RETURN_LOG_SUCCESS("Sale of item [%s] succeeded for [%s]", pcItemName, pEnt->debugName);
		} else {
			TRANSACTION_RETURN_LOG_FAILURE("Sale of item [%s] failed: Numeric [%s] could not be added to [%s]", pcItemName, pcCurrency, pEnt->debugName);
		}
	} else {
		TRANSACTION_RETURN_LOG_FAILURE("Sale of item [%s] for [%s] failed", pcItemName, pEnt->debugName);
	}
}

// Sell an item.  Slot of < 0 means any available, otherwise sell from specific slot.
// Count of < 1 will fail (as making it sell all is more complicated than it sounds,
// and shouldn't really be needed anyway).
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]") 
ATR_LOCKS(pPetEnt, ".Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome tr_ItemSellFromBagFromPet(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pPetEnt, int BagID, const char *pcItemName, int iSlot, U64 uItemID, int iCount, int iCost, const char *pcCurrency, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	ItemDef *pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, pcItemName);
	bool success = false;

	// Make sure the item specified exists
	if (!pItemDef) {
		TRANSACTION_RETURN_LOG_FAILURE("No item could be found matching: %s", pcItemName);
	}

	if (iCount < 1) {
		TRANSACTION_RETURN_LOG_FAILURE("Cannot remove item [%s], requested count to remove is < 0: %i", pcItemName, iCount);
	}

	if (iSlot < 0) {
		success = !!invbag_RemoveItemByDefName(ATR_PASS_ARGS, pPetEnt, BagID, pItemDef->pchName, iCount, pReason, pExtract);
	} else {
		NOCONST(Item)* pCheckItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pPetEnt, BagID, iSlot, pExtract);
		if (NONNULL(pCheckItem) && pCheckItem->id == uItemID)
		{
			Item* pItem = invbag_RemoveItem(ATR_PASS_ARGS, pPetEnt, false, BagID, iSlot, iCount, pReason, pExtract);
			success = !!pItem;
			StructDestroySafe(parse_Item,&pItem);
		}
	}

	if(success) {
		TRANSACTION_APPEND_LOG_SUCCESS("Item sale: Item [%s] successfully removed from [%s] and destroyed", pcItemName, pPetEnt->debugName);
	} else {
		TRANSACTION_APPEND_LOG_FAILURE("Item sale: Failed to remove and destroy item [%s] from [%s]", pcItemName, pPetEnt->debugName);
	}

	if (success) {
		if (inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pcCurrency, (S32)((F32)(iCost*iCount)/*-0.5*/), pReason)) {//removed the -0.5 because it was resulting in incorrect sale values. No idea when it was added in the first place.
			TRANSACTION_RETURN_LOG_SUCCESS("Sale of item [%s] from pet [%s] of player [%s] succeeded", pcItemName, pPetEnt->debugName, pEnt->debugName);
		} else {
			TRANSACTION_RETURN_LOG_FAILURE("Sale of item [%s] failed: Numeric [%s] could not be added to [%s]", pcItemName, pcCurrency, pEnt->debugName);
		}
	} else {
		TRANSACTION_RETURN_LOG_FAILURE("Sale of item [%s] from pet [%s] of player [%s] failed", pcItemName, pPetEnt->debugName, pEnt->debugName);
	}
}

// Buy the item back, put it into best inventory, This uses a passed in item (not through the buyback bag)
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Psaved.Ppuppetmaster.Curtype, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Pplayer.Playertype");
enumTransactionOutcome tr_ItemBuyback(ATR_ARGS, NOCONST(Entity)* pEnt, int iCount, int iCost, const char *pcCurrency, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract, NON_CONTAINER Item *pItem, int bagID)
{
	ItemDef *pItemDef;
	ItemDef *pActualItemDef = NULL;
	NOCONST(Entity) *eaPets = NULL;

	// make sure the entity specified exists
	if(ISNULL(pEnt))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Buyback failed: Purchasing entity is NULL");
	}

	if(ISNULL(pItem))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Buyback failed on entity [%s] due to NULL item", pEnt->debugName);
	}

	pItemDef = GET_REF(pItem->hItem);

	// make sure the item specified exists
	if(!pItemDef)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Buyback failed: No item (def) could be found");
	}

	// make sure at least one is being purchased
	if(iCount < 1)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Buyback of [%s] failed: requested count to buyback is < 1: %i", pItemDef->pchName, iCount);
	}

	if(!gConf.bAllowBuyback)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Buyback of [%s] failed: buyback gloabally disabled", pItemDef->pchName);
	}

	// If it's a value recipe, switch out the item def
	if(pItemDef->eType == kItemType_ItemValue)
	{
		pActualItemDef = pItemDef->pCraft ? GET_REF(pItemDef->pCraft->hItemResult) : NULL;
	}
	else
	{
		pActualItemDef = pItemDef;
	}

	if(!pActualItemDef)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Item value recipe [%s] has no result item, so couldn't be sold", pItemDef->pchName);
	}

	// remove the cost of the item
	if(!item_trh_ItemRemoveCost(ATR_PASS_ARGS, pEnt, pItemDef, NULL, iCost, iCount, pcCurrency, NULL, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("The cost of item [%s] could not be deducted from player [%s]", pItemDef->pchName, pEnt->debugName);
	}

	if(!inv_AddItem(ATR_PASS_ARGS, pEnt, &eaPets, 
		bagID, -1, pItem, pItemDef->pchName, ItemAdd_ClearID, pReason, pExtract) == TRANSACTION_OUTCOME_FAILURE)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Failed to buyback [%s] for player [%s]", pItemDef->pchName, pEnt->debugName);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Item [%s] successfully bought back for entity [%s]", pItemDef->pchName, pEnt->debugName);
}

//transaction to set a numeric items value
void itemtransaction_SetNumeric(Entity *pEnt, const char *itemName, F32 value, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void *pData)
{
	TransactionReturnVal* returnVal;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetNumeric", pEnt, userFunc, pData);
	AutoTrans_inv_tr_ApplyNumeric(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			itemName, value, NumericOp_SetTo, pReason);
}


void itemtransaction_AddNumeric(Entity *pEnt, const char *itemName, F32 value, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void *pData)
{
	TransactionReturnVal* returnVal;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("AddNumeric", pEnt, userFunc, pData);
	AutoTrans_inv_tr_ApplyNumeric(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			itemName, value, 0, pReason);
}

void itemtransaction_AddNumericProfile(Entity *pEnt, const char *itemName, F32 value, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void *pData)
{
	AutoTrans_inv_tr_ApplyNumeric(NULL, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			itemName, value, 0, pReason);
}

AUTO_COMMAND;
void expLoveMe(Entity* pClientEntity, int count)
{
	ItemChangeReason reason = {0};
	inv_FillItemChangeReason(&reason, pClientEntity, "Internal:ExpLoveMe", NULL);
	for(; count > 0; count--)
		itemtransaction_AddNumericProfile(pClientEntity, gConf.pcLevelingNumericItem, 10, NULL, NULL, &reason);
}




AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Ptitlemsgkey, pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]"); 
enumTransactionOutcome tr_SetCurrentTitle(ATR_ARGS, NOCONST(Entity)* pEnt, const char *TitleItemName, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBagLite)* pBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, 7 /* Literal InvBagIDs_Titles */, pExtract); 

	if (!ISNULL(pEnt->pPlayer))
	{
		if (pBag)
		{
			if(TitleItemName && *TitleItemName)
			{
				if (inv_lite_trh_CountItemByName(ATR_PASS_ARGS, pBag, TitleItemName) > 0)
				{
					ItemDef* pItemDef = RefSystem_ReferentFromString(g_hItemDict, TitleItemName);

					if (pItemDef)
					{
						COPY_HANDLE(pEnt->pPlayer->pTitleMsgKey, pItemDef->displayNameMsg.hMessage);

						TRANSACTION_RETURN_LOG_SUCCESS("Current title successfully set for entity [%s] from item [%s]", pEnt->debugName, pItemDef->pchName);
					} else {
						TRANSACTION_APPEND_LOG_FAILURE("Unable to set current title: Title item [%s] does not have a valid ItemDef", pItemDef->pchName);
					}
				} else {
					TRANSACTION_APPEND_LOG_FAILURE("Unable to set current title: Title item [%s] not found on entity [%s]", TitleItemName, pEnt->debugName);
				}
			}
			else
			{
				//Clear the Title
				REMOVE_HANDLE(pEnt->pPlayer->pTitleMsgKey);

				TRANSACTION_RETURN_LOG_SUCCESS("Current title successfully cleared for entity [%s]", pEnt->debugName);
			}
		} else {
			TRANSACTION_APPEND_LOG_FAILURE("Unable to set current title: unable to find title bag for entity [%s]", pEnt->debugName);
		}
	} else {
		TRANSACTION_APPEND_LOG_FAILURE("Unable to set current title: invalid entity [%s]", pEnt->debugName);
	}

	TRANSACTION_RETURN_LOG_FAILURE("Failed to set current title");
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void SetCurrentTitle(Entity *pEnt, const char* TitleItemName)
{
	GameAccountDataExtract *pExtract;
	TransactionReturnVal* returnVal;

	// If pet is being traded, don't allow the title to be changed
	if(entGetType(pEnt) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		Entity* pOwner = entGetOwner(pEnt);
		if(pOwner && trade_IsPetBeingTraded(pEnt, pOwner))
			return;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetTitle", pEnt, NULL, NULL);

	AutoTrans_tr_SetCurrentTitle(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			TitleItemName, pExtract);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Test);
void ClearCostumeUnlocks(Entity *pEnt)
{
	TransactionReturnVal* returnVal;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("ClearCostumeUnlocks", pEnt, NULL, NULL);
	AutoTrans_tr_ItemClearUnlockedCostumes(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt));
}

/////////////////////////////////////////////////////////////////////////
//  Power Factor level-up

typedef struct PowerFactorLevelUpCBData
{
	EntityRef entRef;
	U64 ulItemID;
} PowerFactorLevelUpCBData;

static void ItemLevelUpPowerFactorCB(TransactionReturnVal *pVal, PowerFactorLevelUpCBData *pData)
{
	Entity *pEnt = NULL;
	Item *pItem = NULL;
	if(pData)
	{
		pEnt = entFromEntityRefAnyPartition(pData->entRef);
		pItem = inv_GetItemByID(pEnt, pData->ulItemID);
	}
	if(pEnt)
	{
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		int iNewPowerLevel = pItem ? (int)(item_GetPowerFactor(pItem)) : 0;
		gslSendPrintf(pEnt, "Level PowerFactor to %d: [%s] on item [%s]",
			iNewPowerLevel,
			pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS ? "Success" : "Failure",
			pItemDef ? entTranslateDisplayMessage(pEnt, pItemDef->displayNameMsg) : "Unknown Item");
	}
	SAFE_FREE(pData);
}

// ATR_LOCKS copied from identify. Assumably we need to worry about the levelup item being in many different places
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Pplitebags, .Pinventoryv2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid");
enumTransactionOutcome item_tr_LevelUpPowerFactor(ATR_ARGS, NOCONST(Entity) *pEnt, U64 ulItemID, const char* pchLevelUpItemDefName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item) *pItem = NULL;
	const char *pchLogName = "(Unknown Entity)";
	const char *pchLogItemName = "(Unknown Item)";
	ItemDef *pItemDef = NULL;
	int iCount;
	ItemDef* pLevelUpDef;
	int iIntPowerFactor;
	
	
	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity invalid, failed to level up PowerFactor on item.");
	}

	if(NONNULL(pEnt->debugName))
		pchLogName = pEnt->debugName;


	pItem = inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, ulItemID,NULL,NULL,InvGetFlag_NoBuyBackBag);

	if (!ISNULL(pItem)) pItemDef = GET_REF(pItem->hItem);

	if (ISNULL(pItem) || ISNULL(pItemDef))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity [%s]: Failed to level up PowerFactor on item [%"FORM_LL"u]",
			pchLogName,
			ulItemID);
	}
	
	//////////////	
	// Get item that is the levelup Item. Remove the item from the inventory

	iCount = inv_trh_FindItemCountByDefName(ATR_PASS_ARGS, pEnt, pchLevelUpItemDefName, 1, true, pReason, pExtract);  // This will remove one of the item
	pLevelUpDef = RefSystem_ReferentFromString(g_hItemDict, pchLevelUpItemDefName);

	if (pLevelUpDef==NULL || iCount <= 0 || pLevelUpDef->eType != kItemType_PowerFactorLevelUp)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity [%s]: Failed to use %s to level up PowerFactor on item [%"FORM_LL"u]",
			pchLogName,
			pchLevelUpItemDefName,
			ulItemID);
	}

	iIntPowerFactor = (int)(pLevelUpDef->iPowerFactor);

	//////////////	
	

	// Validate for proper categories. The item being powered up has to match at least one category from the book
	if (!itemdef_HasItemCategory(pItemDef, pLevelUpDef->peCategories))
	{
		// Failed.
		TRANSACTION_RETURN_LOG_FAILURE("Entity [%s]: Failed to level up PowerFactor to %d on item [%"FORM_LL"u]",
			pchLogName,
			iIntPowerFactor,
			ulItemID);
	}
	
	// Validate for proper power setting. We can only increase power. We can only increment from one level lower 
	//
	///  The def uses a I32 so make sure we are not larger than 127 because it eventually becomes an S8
	if (pLevelUpDef->iPowerFactor > 127 || item_trh_GetPowerFactor(pItem) != pLevelUpDef->iPowerFactor-1)
	{
		// Failed.
		TRANSACTION_RETURN_LOG_FAILURE("Entity [%s]: Failed to level up PowerFactor to %d on item [%"FORM_LL"u]",
			pchLogName,
			iIntPowerFactor,
			ulItemID);
	}

	// SET IT NOW
	item_trh_SetAlgoPropsPowerFactor(pItem, pLevelUpDef->iPowerFactor);

	if(NONNULL(pItemDef) && NONNULL(pItemDef->pchName))
	{
		pchLogItemName = pItemDef->pchName;
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Entity [%s]: Successfully leveled powerFactor to %d on [%"FORM_LL"u][%s]",
		pchLogName,
	    iIntPowerFactor,
		ulItemID,
		pchLogItemName);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void itemLevelUpPowerFactor(Entity* pEnt, U64 ulItemID, const char* pchLevelUpItemDefName)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	PowerFactorLevelUpCBData *pCBData = calloc(1, sizeof(PowerFactorLevelUpCBData));
	TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnValEnt("LevelUpPowerFactorCmd", pEnt, ItemLevelUpPowerFactorCB, pCBData);
	
	inv_FillItemChangeReason(&reason, pEnt, "Powers:LevelUpPowerFactor", NULL);

	pCBData->entRef = entGetRef(pEnt);
	pCBData->ulItemID = ulItemID;
	AutoTrans_item_tr_LevelUpPowerFactor(pVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), ulItemID, pchLevelUpItemDefName, &reason, pExtract);
}

/////////////////////////////////////////////////////////////////////////

typedef struct BindCBData
{
	EntityRef entRef;
	U64 ulItemID;
	bool bBind;
} BindCBData;

static void SetBindItemStateCB(TransactionReturnVal *pVal, BindCBData *pData)
{
	Entity *pEnt = NULL;
	Item *pItem = NULL;
	if(pData)
	{
		pEnt = entFromEntityRefAnyPartition(pData->entRef);
		pItem = inv_GetItemByID(pEnt, pData->ulItemID);
	}
	if(pEnt)
	{
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		gslSendPrintf(pEnt, "%sItem: [%s] on item [%s]",
			pData->bBind ? "Bind" : "Unbind",
			pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS ? "Success" : "Failure",
			pItemDef ? entTranslateDisplayMessage(pEnt, pItemDef->displayNameMsg) : "Unknown Item");
	}
	SAFE_FREE(pData);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
enumTransactionOutcome item_tr_SetBindState(ATR_ARGS, NOCONST(Entity) *pEnt, U64 ulItemID, S32 bBind)
{
	NOCONST(Item) *pItem = NULL;
	const char *pchLogName = "(Unknown Entity)";
	const char *pchLogItemName = "(Unknown Item)";
	ItemDef *pItemDef = NULL;
	const char *pchBind = bBind ? "bind" : "unbind";
	
	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity invalid, failed to %s item.",
			pchBind);
	}
	
	if(NONNULL(pEnt->debugName))
		pchLogName = pEnt->debugName;

	pItem = inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, ulItemID,NULL,NULL,InvGetFlag_NoBuyBackBag);

	if(ISNULL(pItem))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity [%s]: Failed to %s item [%"FORM_LL"u]",
			pchLogName,
			pchBind,
			ulItemID);
	}

	pItemDef = GET_REF(pItem->hItem);
	if(NONNULL(pItemDef) && NONNULL(pItemDef->pchName))
	{
		pchLogItemName = pItemDef->pchName;
	}

	if(bBind)
		pItem->flags |= kItemFlag_Bound;
	else
		pItem->flags &= ~(kItemFlag_Bound);

	TRANSACTION_RETURN_LOG_SUCCESS("Entity [%s]: Successfully set bind state [%s] to [%"FORM_LL"u][%s]",
		pchLogName,
		bBind ? "Bound" : "Unbound",
		ulItemID,
		pchLogItemName);
}

static int findItemsInBindState(Entity *pEnt, const char *pchItemName, U64 ulItemID, bool bBound, Item **ppItem)
{
	ItemDef *pItemDef = item_DefFromName(pchItemName);
	
	devassert(ppItem);

	if(pItemDef)
	{
		int ii;
		int iTotalItems = 0;
		for(ii=eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1;ii>=0;ii--)
		{
			InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[ii];
			BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pBag));

			for(;!bagiterator_Stopped(iter); bagiterator_Next(iter))
			{
				Item *pItem = (Item*)bagiterator_GetItem(iter);
				ItemDef *def = bagiterator_GetDef(iter);

				if(!def || 0!=strcmp(def->pchName,pchItemName))
					continue;

				if(!pItem || !((pItem->flags & kItemFlag_Bound) == bBound))
					continue;

				(*ppItem) = pItem;
			}
			bagiterator_Destroy(iter);
		}
		return iTotalItems;
	}
	return -1;
}

//Unbinds an item named ItemName and optionally provide an itemID
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Standard, Inventory);
void UnbindItem(CmdContext *pContext, Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName, U64 ulItemID)
{
	Item *pItem = NULL;
	int iTotalItems = findItemsInBindState(pEnt, pchItemName, ulItemID, true, &pItem);

	if(iTotalItems >= 0)
	{
		if(pItem)
		{
			BindCBData *pCBData = calloc(1, sizeof(BindCBData));
			TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnValEnt("SetBindStateCmd", pEnt, SetBindItemStateCB, pCBData);
			pCBData->entRef = entGetRef(pEnt);
			pCBData->ulItemID = pItem->id;
			pCBData->bBind = false;
			AutoTrans_item_tr_SetBindState(pVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pItem->id, pCBData->bBind );
		}
		else
		{
			if(!iTotalItems)
			{
				estrConcatf(pContext->output_msg, "UnbindItem: Couldn't find item in player's inventory.");
			}
			else
			{
				estrConcatf(pContext->output_msg, "UnbindItem: Items found are all unbound.");
			}
		}
	}
	else
	{
		estrConcatf(pContext->output_msg, "UnbindItem: No such item to unbind.");
	}
}

//Binds an item named ItemName and optionally provide an itemID
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Standard, Inventory);
void BindItem(CmdContext *pContext, Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName, U64 ulItemID)
{
	Item *pItem = NULL;
	int iTotalItems = findItemsInBindState(pEnt, pchItemName, ulItemID, false, &pItem);

	if(iTotalItems >= 0)
	{
		if(pItem)
		{
			BindCBData *pCBData = calloc(1, sizeof(BindCBData));
			TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnValEnt("SetBindStateCmd", pEnt, SetBindItemStateCB, pCBData);
			pCBData->entRef = entGetRef(pEnt);
			pCBData->ulItemID = pItem->id;
			pCBData->bBind = true;
			AutoTrans_item_tr_SetBindState(pVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pItem->id, pCBData->bBind );
		}
		else
		{
			if(!iTotalItems)
			{
				estrConcatf(pContext->output_msg, "BindItem: Couldn't find item in player's inventory.");
			}
			else
			{
				estrConcatf(pContext->output_msg, "BindItem: Items found are all bound.");
			}
		}
	}
	else
	{
		estrConcatf(pContext->output_msg, "BindItem: No such item to bind.");
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome item_tr_OpenRewardPack(ATR_ARGS, 
											  NOCONST(Entity)* pEnt,
											  CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
											  S32 eRewardPackBagID,
											  S32 iRewardPackBagSlot,
											  U64 uRewardPackItemID,
											  GiveRewardBagsData* pRewardBagsData,
											  const ItemChangeReason *pReason,
											  GameAccountDataExtract* pExtract)
{
	NOCONST(Item)* pItem;
	ItemDef* pRewardPackItemDef;
	RewardPackLog *pRewardPackLog = StructCreate(parse_RewardPackLog);
	
	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, eRewardPackBagID, iRewardPackBagSlot, pExtract);

	if (ISNULL(pItem) || pItem->id != uRewardPackItemID)
	{
		StructDestroy(parse_RewardPackLog, pRewardPackLog);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pRewardPackItemDef = GET_REF(pItem->hItem);

	if (!pRewardPackItemDef || !itemdef_trh_VerifyUsageRestrictions(ATR_PASS_ARGS, pEnt, pEnt, pRewardPackItemDef, 0, NULL))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	else if (pRewardPackItemDef->pRewardPackInfo && 
			 IS_HANDLE_ACTIVE(pRewardPackItemDef->pRewardPackInfo->hRequiredItem) && 
			 pRewardPackItemDef->pRewardPackInfo->bConsumeRequiredItems)
	{
		ItemDef* pRequiredItemDef = GET_REF(pRewardPackItemDef->pRewardPackInfo->hRequiredItem);
		S32 iRequiredCount = pRewardPackItemDef->pRewardPackInfo->iRequiredItemCount;
		enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_FAILURE;

		if (pRequiredItemDef)
		{
			eOutcome = inventory_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, pRequiredItemDef->pchName, iRequiredCount, pReason, pExtract);
		}
		if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	pItem = CONTAINER_NOCONST(Item, invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, eRewardPackBagID, iRewardPackBagSlot, 1, pReason, pExtract));
	if (ISNULL(pItem))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	StructDestroyNoConst(parse_Item, pItem);

	if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, pRewardBagsData, kRewardOverflow_AllowOverflowBag, NULL, pReason, pExtract, pRewardPackLog))
	{
		StructDestroy(parse_RewardPackLog, pRewardPackLog);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (pRewardPackItemDef)
	{
		char *pTemp = NULL;
		char *pTempEscaped = NULL;

		estrStackCreate(&pTemp);
		estrStackCreate(&pTempEscaped);

		pRewardPackLog->pchEntityName = StructAllocString(pEnt->debugName);
		pRewardPackLog->pchRewardPackName = StructAllocString(pRewardPackItemDef->pchName);

		ParserWriteText(&pTemp, parse_RewardPackLog, pRewardPackLog, 0, 0, TOK_NO_LOG);
		estrAppendEscaped(&pTempEscaped, pTemp);

		if (gbEnableEconomyLogging) {
			TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_REWARDPACK, "OpenRewardPack", "%s", pTempEscaped);
		}

		estrDestroy(&pTemp);
		estrDestroy(&pTempEscaped);
	}

	StructDestroy(parse_RewardPackLog, pRewardPackLog);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// The following micro-transactions is used for items to execute a grant of some microtransactions.
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pAccountData, ".Eakeys");
bool item_trh_GrantMicroSpecial(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
	ATH_ARG NOCONST(GameAccountData) *pAccountData,
	SpecialPartType eSpecialType,
	U32 qty,
	const ItemChangeReason *pReason)
{
	bool bReturnValue = false;

	if(qty > 0)
	{

		switch(eSpecialType)
		{
		case kSpecialPartType_BankSize:
			{
				if(inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, "BankSizeMicrotrans", qty, pReason))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_SharedBankSize:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetSharedBankSlotGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_InventorySize:
			{
				if(inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, "AddInvSlotsMicrotrans", qty, pReason))
				{
					bReturnValue = true;
				}
				break;
			}

		case kSpecialPartType_CharSlots:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetCharSlotsGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}

		case kSpecialPartType_SuperPremium:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetSuperPremiumGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}

		case kSpecialPartType_CostumeSlots:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetCostumeSlotsGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_Respec:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetRespecTokensGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_Rename:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetRenameTokensGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_Retrain:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetRetrainTokensGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_OfficerSlots:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetOfficerSlotsGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}

		case kSpecialPartType_CostumeChange:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetFreeCostumeChangeGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_ShipCostumeChange:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetFreeShipCostumeChangeGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_ItemAssignmentCompleteNow:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetItemAssignmentCompleteNowGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		case kSpecialPartType_ItemAssignmentUnslotItem:
			{
				if(slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pAccountData, MicroTrans_GetItemAssignmentUnslotTokensGADKey(), qty, 0, 100000))
				{
					bReturnValue = true;
				}
				break;
			}
		default:
			{
				bReturnValue = false;
			}
		}
	}
	else
	{
		// qty 0 is ok for type none
		if(eSpecialType == kSpecialPartType_None)
		{
			return true;
		}
	}

	return bReturnValue;
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Psaved.Ppbuilds, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Iversion, .Psaved.Uiindexbuild, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(pGameAccountData, ".Eatokens, .Eakeys, .Iversion, .Eapermissions, .Umaxcharacterlevelcached, .Bbilled");
enumTransactionOutcome item_tr_GrantMicroSpecial(ATR_ARGS, 
	NOCONST(Entity)* pEnt,
	NOCONST(GameAccountData) * pGameAccountData,
	S32 eBagID,
	S32 iBagSlot,
	U64 uItemID,
	const ItemChangeReason *pReason,
	GameAccountDataExtract* pExtract)
{
	NOCONST(Item)* pItem;
	ItemDef *pItemDef;
	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, eBagID, iBagSlot, pExtract);

	if (ISNULL(pItem) || pItem->id != uItemID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Bad item for item_tr_GrantMicroSpecial");
	}

	pItemDef = GET_REF(pItem->hItem);
	if(!pItemDef || pItemDef->eType != kItemType_GrantMicroSpecial)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Bad itemdef for item_tr_GrantMicroSpecial");
	}

	if(!item_trh_GrantMicroSpecial(ATR_PASS_ARGS, pEnt, pGameAccountData, pItemDef->eSpecialPartType, pItemDef->uSpecialPartCount, pReason))	// qty set to one for now
	{
		TRANSACTION_RETURN_LOG_FAILURE("item_tr_GrantMicroSpecial can't grant special for itemdef %s", pItemDef->pchName);
	}

	// grant a game permission
	if(pItemDef->pchPermission)
	{
		if(NONNULL(pGameAccountData))
		{

			GamePermissionDef *pPermission = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions,pItemDef->pchPermission);

			if(!pPermission)
			{
				TRANSACTION_RETURN_LOG_FAILURE("item_tr_GrantMicroSpecial can't find permission %s for itemdef %s",pItemDef->pchPermission, pItemDef->pchName);
			}
			else
			{
				bool bUnlocked = false;
				NOCONST(GamePermission) *pGADPermission;
				pGADPermission = eaIndexedGetUsingString(&pGameAccountData->eaPermissions, pItemDef->pchPermission);

				if(pGADPermission)
				{
					// If this does more than unlock a permission its ok to allow the transaction
					if(pItemDef->eSpecialPartType == kSpecialPartType_None)
					{
						TRANSACTION_RETURN_LOG_FAILURE("item_tr_GrantMicroSpecial already has permission %s for itemdef %s",pItemDef->pchPermission, pItemDef->pchName);
					}
				}
				else if(!trhMicroTrans_GrantPermission(ATR_PASS_ARGS, pEnt, pGameAccountData, pPermission))
				{
					TRANSACTION_RETURN_LOG_FAILURE("item_tr_GrantMicroSpecial faieldto grant permission %s for itemdef %s",pItemDef->pchPermission, pItemDef->pchName);
				}
			}
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("No game account data for item_tr_GrantMicroSpecial");
		}
	}

	pItem = CONTAINER_NOCONST(Item, invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, eBagID, iBagSlot, 1, pReason, pExtract));
	if (ISNULL(pItem))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Unable to remove itemdef %s in item_tr_GrantMicroSpecial", pItemDef->pchName);
	}
	StructDestroyNoConst(parse_Item, pItem);

	TRANSACTION_RETURN_LOG_SUCCESS("item_tr_GrantMicroSpecial succeeded.");
}


AUTO_TRANS_HELPER;
bool ItemOpenExperienceGiftCanBeFilled(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef)
{

	S32 exp = inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pEnt, gConf.pcLevelingNumericItem);
	S32 expAfter = exp - pItemDef->uExperienceGift * g_RewardConfig.GiftData.uConversionRate;
	S32 expNeeded = NUMERIC_AT_LEVEL(LevelFromLevelingNumeric(exp));

	if(expAfter >= expNeeded)
	{
		// success
		return true;
	}

	// not enough experience
	return false;

}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[], .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp,, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome item_tr_ExperienceGiftFill(ATR_ARGS, 
	NOCONST(Entity)* pEnt,
	S32 eBagID,
	S32 iBagSlot,
	U64 uItemID,
	GameAccountDataExtract* pExtract,
	const ItemChangeReason *pReason)
{
	NOCONST(Item)* pItem;
	ItemDef *pItemDef;
	S32 expCost;

	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, eBagID, iBagSlot, pExtract);

	if (ISNULL(pItem) || pItem->id != uItemID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pItemDef = GET_REF(pItem->hItem);
	if(!pItemDef || pItemDef->eType != kItemType_ExperienceGift)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// check for legal fill
	if(!ItemOpenExperienceGiftCanBeFilled(ATR_PASS_ARGS, pEnt, pItemDef))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// remove the experience
	expCost = pItemDef->uExperienceGift * g_RewardConfig.GiftData.uConversionRate;
	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, gConf.pcLevelingNumericItem, -expCost, pReason))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Fill the item
	pItem->flags |= kItemFlag_Full;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Uiindexbuild, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome item_tr_ExperienceGiftGive(ATR_ARGS, 
	NOCONST(Entity)* pEnt,
	S32 eBagID,
	S32 iBagSlot,
	U64 uItemID,
	GameAccountDataExtract* pExtract,
	const ItemChangeReason *pReason)
{
	NOCONST(Item)* pItem;
	ItemDef *pItemDef;

	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, eBagID, iBagSlot, pExtract);

	if (ISNULL(pItem) || pItem->id != uItemID)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pItemDef = GET_REF(pItem->hItem);
	if(!pItemDef || pItemDef->eType != kItemType_ExperienceGift || (pItem->flags & kItemFlag_Full) == 0)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if(entity_trh_GetSavedExpLevel(pEnt) > g_RewardConfig.GiftData.uMaxGiveLevel)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Give the experience
	if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, gConf.pcLevelingNumericItem, pItemDef->uExperienceGift, pReason))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// remove one of these
	pItem = CONTAINER_NOCONST(Item, invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, eBagID, iBagSlot, 1, pReason, pExtract));
	if (ISNULL(pItem))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	StructDestroyNoConst(parse_Item, pItem);
	return TRANSACTION_OUTCOME_SUCCESS;
}


void itemtransaction_MoveItemGuildAcrossEnts_Wrapper(TransactionReturnVal *returnVal, GlobalType eServerTypeToRunOn, 
														GlobalType pPlayerEnt_type, ContainerID pPlayerEnt_ID,
														int iPetIdx, GlobalType pPetIdx_type, const U32 * const * eaPets,
														GlobalType pGuild_type, ContainerID pGuild_ID,
														GlobalType pGuildBank_type, ContainerID pGuildBank_ID,
														int bSrcGuild, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID, int iSrcEPValue, int iSrcCount, 
														int bDstGuild, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iDstEPValue,
														const ItemChangeReason *pReason, const GameAccountDataExtract *pExtract)
{
	MoveItemGuildStruct srcdestinfo = {0};
	srcdestinfo.bSrcGuild = bSrcGuild;
	srcdestinfo.iSrcSlotIdx = iSrcSlotIdx;
	srcdestinfo.uSrcItemID = uSrcItemID;
	srcdestinfo.iSrcEPValue = iSrcEPValue;
	srcdestinfo.iSrcCount = iSrcCount;
	srcdestinfo.bDstGuild = bDstGuild;
	srcdestinfo.iDstSlotIdx = iDstSlotIdx;
	srcdestinfo.uDstItemID = uDstItemID;
	srcdestinfo.iDstEPValue = iDstEPValue;

	AutoTrans_inv_ent_tr_MoveItemGuildAcrossEnts(returnVal, eServerTypeToRunOn, pPlayerEnt_type, pPlayerEnt_ID, iPetIdx, pPetIdx_type, eaPets, pGuild_type, pGuild_ID, pGuildBank_type, pGuildBank_ID, &srcdestinfo, iSrcBagID, iDstBagID, pReason, pExtract);
}

void itemtransaction_MoveItemGuild_Wrapper(TransactionReturnVal *returnVal, GlobalType eServerTypeToRunOn, 
														GlobalType pPlayerEnt_type, ContainerID pPlayerEnt_ID, 
														GlobalType pPet_type, const U32 * const * eaPets,
														GlobalType pGuild_type, ContainerID pGuild_ID,
														GlobalType pGuildBank_type, ContainerID pGuildBank_ID,
														int bSrcGuild, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID, int iSrcEPValue, int iSrcCount, 
														int bDstGuild, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iDstEPValue,
														const ItemChangeReason *pReason, const GameAccountDataExtract *pExtract)
{
	MoveItemGuildStruct srcdestinfo = {0};
	srcdestinfo.bSrcGuild = bSrcGuild;
	srcdestinfo.iSrcSlotIdx = iSrcSlotIdx;
	srcdestinfo.uSrcItemID = uSrcItemID;
	srcdestinfo.iSrcEPValue = iSrcEPValue;
	srcdestinfo.iSrcCount = iSrcCount;
	srcdestinfo.bDstGuild = bDstGuild;
	srcdestinfo.iDstSlotIdx = iDstSlotIdx;
	srcdestinfo.uDstItemID = uDstItemID;
	srcdestinfo.iDstEPValue = iDstEPValue;

	AutoTrans_inv_ent_tr_MoveItemGuild(returnVal, eServerTypeToRunOn, pPlayerEnt_type, pPlayerEnt_ID, pPet_type, eaPets, pGuild_type, pGuild_ID, pGuildBank_type, pGuildBank_ID, &srcdestinfo, iSrcBagID, iDstBagID, pReason, pExtract);
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Uiindexbuild, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome item_tr_ApplyDyeToItem(ATR_ARGS, 
	NOCONST(Entity)* pEnt,
	int iDyeBag, int iDyeSlot,
	int iItemBag, int iItemSlot,
	int iChannel,
	ItemChangeReason *pReason)
{
	NOCONST(Item)* pDye = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, iDyeBag, iDyeSlot, NULL);
	ItemDef* pDyeDef = SAFE_GET_REF(pDye, hItem);
	NOCONST(Item)* pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, iItemBag, iItemSlot, NULL);
	NOCONST(AlgoItemProps)* pProps;
	bool bDyed = false;
	int i;

	if (!pDye || !pItem || iChannel > 3)
		return TRANSACTION_OUTCOME_FAILURE;

    pProps = item_trh_GetOrCreateAlgoProperties(pItem);

	if (!pProps->pDyeData)
		pProps->pDyeData = StructCreateNoConst(parse_DyeData);

	if (IS_HANDLE_ACTIVE(pDyeDef->hDyeMat))
		COPY_HANDLE(pProps->pDyeData->hMat, pDyeDef->hDyeMat);

	//pack 4 vec3s into a U8[12] to save space.
	if (pDyeDef->eType == kItemType_DyeBottle)
	{
		pProps->pDyeData->DyeColors[iChannel*3 + 0] = pDyeDef->vDyeColor0[0]*255 + 0.5;
		pProps->pDyeData->DyeColors[iChannel*3 + 1] = pDyeDef->vDyeColor0[1]*255 + 0.5;
		pProps->pDyeData->DyeColors[iChannel*3 + 2] = pDyeDef->vDyeColor0[2]*255 + 0.5;
	}
	else if (pDyeDef->eType == kItemType_DyePack)
	{
//		Channel 0
		{
			pProps->pDyeData->DyeColors[0] = pDyeDef->vDyeColor0[0]*255 + 0.5;
			pProps->pDyeData->DyeColors[1] = pDyeDef->vDyeColor0[1]*255 + 0.5;
			pProps->pDyeData->DyeColors[2] = pDyeDef->vDyeColor0[2]*255 + 0.5;
		}
//		Channel 1
		{
			pProps->pDyeData->DyeColors[3] = pDyeDef->vDyeColor1[0]*255 + 0.5;
			pProps->pDyeData->DyeColors[4] = pDyeDef->vDyeColor1[1]*255 + 0.5;
			pProps->pDyeData->DyeColors[5] = pDyeDef->vDyeColor1[2]*255 + 0.5;
		}
//		Channel 2
		{
			pProps->pDyeData->DyeColors[6] = pDyeDef->vDyeColor2[0]*255 + 0.5;
			pProps->pDyeData->DyeColors[7] = pDyeDef->vDyeColor2[1]*255 + 0.5;
			pProps->pDyeData->DyeColors[8] = pDyeDef->vDyeColor2[2]*255 + 0.5;
		}
//		Channel 3
		{
			pProps->pDyeData->DyeColors[9] = pDyeDef->vDyeColor3[0]*255 + 0.5;
			pProps->pDyeData->DyeColors[10] = pDyeDef->vDyeColor3[1]*255 + 0.5;
			pProps->pDyeData->DyeColors[11] = pDyeDef->vDyeColor3[2]*255 + 0.5;
		}
	}
	else
		return TRANSACTION_OUTCOME_FAILURE;

	for (i = 0; i < 12; i++)
	{
		if (pProps->pDyeData->DyeColors[i] != 0)
		{
			bDyed = true;
			break;
		}
	}

	if (!bDyed)
	{
		StructDestroyNoConst(parse_DyeData, pProps->pDyeData);
		pProps->pDyeData = NULL;
	}
	inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, false, inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iDyeBag, NULL), iDyeSlot, 1, pReason);

	return TRANSACTION_OUTCOME_SUCCESS;
}

typedef struct DyeItemCBData
{
	GlobalType ownerType;
	ContainerID ownerID;
	U64 itemID;
	U64 dyeID;
} DyeItemCBData;

typedef struct TransmutateItemCBData
{
	GlobalType ownerType;
	ContainerID ownerID;
	U64 uMainItemID;
	U64 uTransmutateToItemID;
} TransmutateItemCBData;

static void DyeItemCB(TransactionReturnVal* returnVal, DyeItemCBData* pData)
{
	if(pData)
	{
		Entity* pEnt = entFromContainerIDAnyPartition(pData->ownerType, pData->ownerID);
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
			ClientCmd_NotifySend(pEnt, kNotifyType_ExperimentComplete, TranslateMessageKey("Dye.Success"), NULL, NULL);
		}
		else
			ClientCmd_NotifySend(pEnt, kNotifyType_ExperimentFailed, TranslateMessageKey("Dye.Failure"), NULL, NULL);
	}

	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Inventory) ACMD_PRIVATE;
void ApplyDyeToItem(Entity *pEnt, 
	U64 itemID, U64 dyeID, int iChannel)
{
	GameAccountDataExtract *pExtract;
	ItemChangeReason reason = {0};
	TransactionReturnVal* returnVal;
	DyeItemCBData* pData = calloc(1, sizeof(DyeItemCBData));
	int iItemSlot = 0, iDyeSlot = 0;
	InvBagIDs iItemBag = InvBagIDs_None, iDyeBag = InvBagIDs_None;
    NOCONST(Item) *tmpItem1 = NULL;
    NOCONST(Item) *tmpItem2 = NULL;

	pData->ownerType = pEnt->myEntityType;
	pData->ownerID = pEnt->myContainerID;
	pData->itemID = itemID;
	pData->dyeID = dyeID;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	inv_FillItemChangeReason(&reason, pEnt, "Inventory:DyeItem", NULL);

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("ApplyDyeToItem", pEnt, DyeItemCB, pData);

	tmpItem1 = inv_trh_GetItemByID(ATR_EMPTY_ARGS, (NOCONST(Entity)*)pEnt, dyeID, &iDyeBag, &iDyeSlot, 0);
	tmpItem2 = inv_trh_GetItemByID(ATR_EMPTY_ARGS, (NOCONST(Entity)*)pEnt, itemID, &iItemBag, &iItemSlot, 0);

    if ( ( tmpItem1 != NULL ) && ( tmpItem2 != NULL ) )
    {
	    AutoTrans_item_tr_ApplyDyeToItem(returnVal, GetAppGlobalType(), 
		    entGetType(pEnt), entGetContainerID(pEnt), 
		    iDyeBag, iDyeSlot, iItemBag, iItemSlot, iChannel, 
			&reason);
    }
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pugckillcreditlimit, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Erscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome item_tr_TransmutateItem(ATR_ARGS, NOCONST(Entity)* pEnt,
	S32 iMainItemBag, S32 iMainItemSlot, S32 iTransmutateToItemBag, S32 iTransmutateToItemSlot, S32 iCost, ItemChangeReason* pReason)
{
	NOCONST(Item)* pMainItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, iMainItemBag, iMainItemSlot, NULL);
	NOCONST(Item)* pTransmutateToItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, iTransmutateToItemBag, iTransmutateToItemSlot, NULL);
	ItemDef* pMainItemDef = SAFE_GET_REF(pMainItem, hItem);
	ItemDef* pTransmutateToItemDef = SAFE_GET_REF(pTransmutateToItem, hItem);

	NOCONST(SpecialItemProps)* pProps;

	if (ISNULL(pMainItem) || ISNULL(pTransmutateToItem) || !pMainItemDef || !pTransmutateToItemDef || 
		!item_CanTransMutate(pMainItemDef) ||
		!item_CanTransMutate(pTransmutateToItemDef) ||
		!item_CanTransMutateTo(pMainItemDef, pTransmutateToItemDef))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (g_ItemConfig.pchTransmuteCurrencyName && iCost > 0 && !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, g_ItemConfig.pchTransmuteCurrencyName, -iCost, pReason))
	{
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "Transmutation.InsufficientCurrency", kNotifyType_ItemRequired);
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay transmutation cost of %i %s", iCost, g_ItemConfig.pchTransmuteCurrencyName);
	}

	if (pTransmutateToItem->pSpecialProps &&
		pTransmutateToItem->pSpecialProps->pTransmutationProps &&
		IS_HANDLE_ACTIVE(pTransmutateToItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef))
	{
		// If the appearance item is already transmuted, use its appearance instead.
		pTransmutateToItemDef = GET_REF(pTransmutateToItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
	}

	pProps = item_trh_GetOrCreateSpecialProperties(pMainItem);

	if (ISNULL(pProps->pTransmutationProps))
	{
		pProps->pTransmutationProps = StructCreateNoConst(parse_ItemTransmutationProps);
	}

	// Copy the appearance
	SET_HANDLE_FROM_REFERENT(g_hItemDict, pTransmutateToItemDef, pProps->pTransmutationProps->hTransmutatedItemDef);

	// Copy the dye
	if( pTransmutateToItem->pAlgoProps && pTransmutateToItem->pAlgoProps->pDyeData )
	{
		NOCONST(AlgoItemProps)* pMainItemAlgoProps = item_trh_GetOrCreateAlgoProperties(pMainItem);
		NOCONST(AlgoItemProps)* pTransmuteToAlgoProps = item_trh_GetOrCreateAlgoProperties(pTransmutateToItem);

		if (ISNULL(pMainItemAlgoProps->pDyeData))
		{
			pMainItemAlgoProps->pDyeData = StructCreateNoConst(parse_DyeData);
		}
		StructCopyNoConst(parse_DyeData, pTransmuteToAlgoProps->pDyeData, pMainItemAlgoProps->pDyeData,0,0,0);
	}

	inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, false, inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iTransmutateToItemBag, NULL), iTransmutateToItemSlot, 1, pReason);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void TransmutateItemCB(TransactionReturnVal* returnVal, TransmutateItemCBData* pData)
{
	if(pData)
	{
		Entity * pEnt = entFromContainerIDAnyPartition(pData->ownerType, pData->ownerID);
		if (pEnt)
		{
			if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
			{
				ClientCmd_NotifySend(pEnt, kNotifyType_ItemTransmutationSuccess, TranslateMessageKey("ItemTransmutation.Success"), NULL, NULL);
			}
			else
			{
				ClientCmd_NotifySend(pEnt, kNotifyType_ItemTransmutationFailure, TranslateMessageKey("ItemTransmutation.Failure"), NULL, NULL);
			}
		}
	}

	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Inventory) ACMD_PRIVATE;
void TransmutateItem(Entity *pEnt, 
	U64 uMainItemID, U64 uTransmutateToItemID)
{
	GameAccountDataExtract *pExtract;
	TransactionReturnVal* returnVal;
	TransmutateItemCBData* pData = calloc(1, sizeof(TransmutateItemCBData));
	S32 iMainItemSlot, iTransmutateToItemSlot;
	S32 iCost;
	InvBagIDs iMainItemBag, iTransmutateToItemBag;
	Item* pStats;
	Item* pAppearance;
	ItemChangeReason reason = {0};

	// Set up callback params
	pData->ownerType = pEnt->myEntityType;
	pData->ownerID = pEnt->myContainerID;
	pData->uMainItemID = uMainItemID;
	pData->uTransmutateToItemID = uTransmutateToItemID;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("TransmutateItem", pEnt, TransmutateItemCB, pData);

	// Get the bag and slot indexes
	pStats = CONTAINER_RECONST(Item, inv_trh_GetItemByID(ATR_EMPTY_ARGS, (NOCONST(Entity)*)pEnt, uMainItemID, &iMainItemBag, &iMainItemSlot, 0));
	pAppearance = CONTAINER_RECONST(Item, inv_trh_GetItemByID(ATR_EMPTY_ARGS, (NOCONST(Entity)*)pEnt, uTransmutateToItemID, &iTransmutateToItemBag, &iTransmutateToItemSlot, 0));

	iCost = itemeval_GetTransmutationCost(pEnt->iPartitionIdx_UseAccessor, pEnt, pStats, pAppearance);
	
	inv_FillItemChangeReason(&reason, pEnt, "Item Transmutation", "UseNumeric");

	AutoTrans_item_tr_TransmutateItem(returnVal, GetAppGlobalType(), 
		entGetType(pEnt), entGetContainerID(pEnt), 
		iMainItemBag, iMainItemSlot, iTransmutateToItemBag, iTransmutateToItemSlot, iCost, &reason);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome item_tr_DiscardTransmutation(ATR_ARGS, NOCONST(Entity)* pEnt,
	S32 iItemBag, S32 iItemSlot)
{
	NOCONST(Item)* pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, iItemBag, iItemSlot, NULL);

	if (!pItem || 
		!pItem->pSpecialProps || 
		!pItem->pSpecialProps->pTransmutationProps ||
		!IS_HANDLE_ACTIVE(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroyNoConst(parse_ItemTransmutationProps, pItem->pSpecialProps->pTransmutationProps);
	pItem->pSpecialProps->pTransmutationProps = NULL;

	item_trh_FixupSpecialProps(pItem);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void DiscardItemTransmutationCB(TransactionReturnVal* returnVal, TransmutateItemCBData* pData)
{
	if(pData)
	{
		Entity * pEnt = entFromContainerIDAnyPartition(pData->ownerType, pData->ownerID);
		if (pEnt)
		{
			if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
			{
				ClientCmd_NotifySend(pEnt, kNotifyType_ItemTransmutationDiscardSuccess, TranslateMessageKey("ItemTransmutation.Success"), NULL, NULL);
			}
			else
			{
				ClientCmd_NotifySend(pEnt, kNotifyType_ItemTransmutationDiscardFailure, TranslateMessageKey("ItemTransmutation.Failure"), NULL, NULL);
			}
		}
	}

	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Inventory) ACMD_PRIVATE;
void DiscardItemTransmutation(Entity *pEnt, U64 uItemID)
{
	GameAccountDataExtract *pExtract;
	TransactionReturnVal* returnVal;
	TransmutateItemCBData* pData = calloc(1, sizeof(TransmutateItemCBData));
	S32 iItemSlot;
	InvBagIDs iItemBag;

	// Set up callback params
	pData->ownerType = pEnt->myEntityType;
	pData->ownerID = pEnt->myContainerID;
	pData->uMainItemID = uItemID;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("DiscardItemTransmutation", pEnt, DiscardItemTransmutationCB, pData);

	// Get the bag and slot indexes
	inv_trh_GetItemByID(ATR_EMPTY_ARGS, (NOCONST(Entity)*)pEnt, uItemID, &iItemBag, &iItemSlot, 0);

	AutoTrans_item_tr_DiscardTransmutation(returnVal, GetAppGlobalType(), 
		entGetType(pEnt), entGetContainerID(pEnt), 
		iItemBag, iItemSlot);
}
