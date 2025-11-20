#include "AlgoPet.h"
#include "Entity.h"
#include "EntityLib.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "estring.h"
#include "Expression.h"
#include "FolderCache.h"
#include "GameAccountData\GameAccountData.h"
#include "AutoGen\GameAccountData_h_ast.h"
#include "ItemAssignments.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "loggingEnums.h"
#include "microtransactions_common.h"
#include "rewardCommon.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "stringcache.h"
#include "resourceManager.h"
#include "NotifyCommon.h"
#include "net.h"
#include "worldgrid.h"
#include "SuperCritterPet.h"

#include "Character.h"
#include "CharacterClass.h"
#include "Character_h_ast.h"
#include "CommandQueue.h"
#include "EntityBuild.h"
#include "AutoGen/EntityBuild_h_ast.h"
#include "entCritter.h"
#include "entCritter_h_ast.h"
#include "mission_common.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "PowerAnimFX.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerHelpers.h"
#include "SavedPetCommon.h"
#include "qsortG.h"
#include "UGCCommon.h"
#include "Guild.h"
#include "Guild_h_ast.h"
#include "AlgoItemCommon.h"
#include "OfficerCommon.h"
#include "SavedPetCommon.h"
#include "GamePermissionsCommon.h"
#include "SuperCritterPet.h"

#include "AutoTransDefs.h"
#include "MemoryPool.h"

#include "itemEnums_h_ast.h"

#ifdef GAMECLIENT
#include "GameClientLib.h"
#endif

#ifdef GAMESERVER
#include "SavedPetTransactions.h"
#include "aiAnimList.h"
#include "AnimList_Common.h"
#include "AlgoItem.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "GameServerLib.h"
#include "gslEventSend.h"
#include "gslLogSettings.h"
#include "gslMapVariable.h"
#include "gslPartition.h"
extern AlgoTables g_AlgoTables;
extern int algoitem_GetRandomQuality(int i, const char *pcRank, U32* pSeed, bool alwaysGenerate);
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hInvSlotDefDict;
static InvBagSlotTables s_BagSlotTables = {0};
static InvBagCategories s_BagCategories = {0};
InvBagExtraIDs g_InvBagExtraIDs = {0};

MP_DEFINE(BagIterator);

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//static routine protos


const char* inv_trh_GetItemDefNameFromItem(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem);
NOCONST(Item)* inv_trh_GetItemPtrFromSlot(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx);
//int inv_trh_GetSlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, const char *name, InventorySlotIDDef *pIDDef);
bool inv_trh_SwapItems(ATR_ARGS, bool bSilent, ATH_ARG NOCONST(Entity)* pDestEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int DestSlotIdx, ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets, ATH_ARG NOCONST(InventoryBag)* pSrcBag, int SrcSlotIdx, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract);

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
// removed from header

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that add items to a bag

//input:
//either a Ent/BagID or a Bag pointer
//SlotIdx = -1 specifies first open slot, or end of bag | SlotIdx = -2 specifies end of bag only
//either and Item pointer or a ItemDef name
//count specifies the # of duplicate items to add  (depending on stack limits, may only effect slot counts)

//output:
//int returns dest slot idx in the dest bag that item was put into, -1 on error
//bool returns true if add was successful, otherwise false

bool inv_bag_AddItem(InventoryBag* pDestBag, int iDestSlotIdx, Item* pItem, int iCount, ItemAddFlags eFlags);

bool inv_bag_trh_AddItemFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, const char* ItemDefName, int count, int overrideLevel, const char *pcRank, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
bool inv_bag_AddItemFromDef(InventoryBag* pDestBag, int SlotIdx, const char* ItemDefName, int count, int overrideLevel, const char* rank, ItemAddFlags eFlags, GameAccountDataExtract *pExtract);

bool inv_ent_trh_AddItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int BagID, int SlotIdx, NOCONST(Item)* pItem, ItemAddFlags eFlags, U64** peaItemIDs, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
bool inv_ent_AddItem(Entity* ent, int BagID, int SlotIdx, NOCONST(Item)* pItem, int Count, ItemAddFlags eFlags, GameAccountDataExtract *pExtract);

bool inv_bag_trh_AddMissionItemFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, const char* ItemDefName, int count, int overrideLevel, const char *pcRank, ItemAddFlags eFlags, const char* pcMission, bool bUseOverrideLevel, U32 *pSeed, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
bool inv_bag_AddMissionItemFromDef(Entity* pEnt, InventoryBag* pDestBag, int iSlotIdx, const char* pcItemDefName, int iCount, int iOverrideLevel, const char* pcRank, ItemAddFlags eFlags, const char* pcMission, U32* pSeed, GameAccountDataExtract *pExtract);
bool inv_bag_AddMissionItemFromDefLevel(Entity* pEnt, InventoryBag* pDestBag, int iSlotIdx, const char* pcItemDefName, int iCount, int iOverrideLevel, const char* pcRank, ItemAddFlags eFlags, const char* pcMission, bool useOverrideLevel, U32* pSeed, GameAccountDataExtract *pExtract);
bool inv_ent_trh_AddMissionItemFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int iBagID, int iSlotIndex, const char *pchItemDefName, int iCount, ItemAddFlags eFlags, const char* pcMission, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
bool inv_ent_AddMissionItemFromDef(Entity* pEnt, int iBagID, int iSlotIndex, const char* pchItemDefName, int iCount, ItemAddFlags eFlags, const char* pcMission, GameAccountDataExtract *pExtract);

// Item creation
// ===============================================================================

NOCONST(Item)* inv_ContainerItemInstanceFromDef(ItemDef *pItemDef, GlobalType eContainerType, ContainerID eContainerID,const char *pcRank, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, ItemQuality eQuality);
NOCONST(Item)* inv_ent_ContainerItemInstanceFromSavedPet(Entity* pEnt, ContainerID iSavedPetID);

//@@@@@@@@@@@@@@@@@@@@
// Iterators

//def of callback used in foreach item bag scan routines
// WARNING: "ItemLite" Items does not include an Item instance, only an ItemDef.  For this reason
//  the callback may receive NULL for pItem.  Additionally it's possible for an Item instance to have
//  an invalid ItemDef, so you may get NULL for that as well.
typedef void (*inv_trh_foreachitem_Callback)(NOCONST(Item)* pItem, ItemDef *pItemDef, NOCONST(InventoryBag)* pBag, NOCONST(InventorySlot)* pSlot, void* pData);
typedef void (*inv_foreachitem_Callback)(Item* pItem, ItemDef *pItemDef, InventoryBag* pBag, InventorySlot* pSlot, void* pData);

void inv_bag_trh_ForEachItemInBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, inv_trh_foreachitem_Callback pCallback, void *pData);
#define inv_bag_ForEachItemInBag(pBag, pCallback, pData) inv_bag_trh_ForEachItemInBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (pBag)), (inv_trh_foreachitem_Callback) pCallback, pData)

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that makes lists of things in bags

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get count of items in a specified bag/slot




//!!!! this return data used to be handled with an earray, but that required dynamic mem allocation that was not
// optimal considering that this routine can be called multiple times per frame.  Changed to a fixed array that can
// be allocated on the stack for that reason

#define NUM_INVENTORY_LIST_ENTRIES	1000

typedef struct InventoryListEntry
{
	InventoryBag *pBag;
	InventorySlot *pSlot;

}InventoryListEntry;

typedef struct InventoryList
{
	int count;
	InventoryListEntry Entry[NUM_INVENTORY_LIST_ENTRIES];

}InventoryList;

int inv_bag_trh_GetItemListEx(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, SA_PARAM_OP_STR const char *ItemDefName, ItemCategory eCategory, InventoryList *pInventoryList);
int inv_bag_trh_GetItemList(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, SA_PARAM_OP_STR const char *ItemDefName, InventoryList *pInventoryList);
#define inv_bag_GetItemList(pBag, ItemDefName, pInventoryList) inv_bag_trh_GetItemList(ATR_EMPTY_ARGS,  CONTAINER_NOCONST(InventoryBag, (pBag)), ItemDefName, pInventoryList)

int inv_ent_trh_GetItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, SA_PARAM_NN_STR const char *ItemDefName, InventoryList *pInventoryList, GameAccountDataExtract *pExtract);
#define inv_ent_GetItemList(pEnt, BagID, ItemDefName, pInventoryList) inv_ent_trh_GetItemList(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), false, BagID, ItemDefName, pInventoryList)

// This function has been removed until further notice due to the high chance it will trigger an assert by clearing NUM_INVENTORY_LIST_ENTRIES
//int inv_ent_trh_AllBagsGetItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char *ItemDefName, InventoryList *pInventoryList);
//#define inv_ent_AllBagsGetItemList(pEnt, ItemDefName, pInventoryList) inv_ent_trh_AllBagsGetItemList(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), false, ItemDefName, pInventoryList)

int inv_ent_trh_GetBagList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InventoryList *pInventoryList);
#define inv_ent_GetBagList(pEnt, pInventoryList) inv_ent_trh_GetBagList(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), false, pInventoryList)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that remove items

bool inv_ent_trh_RemoveItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 uiItemId, int count);
NOCONST(Item)* inv_bag_trh_RemoveItemByDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBag)* pBag, const char* ItemDefName, int count, const ItemChangeReason *pReason);

//take item(s) from a bag and returns pointer to it
//item struct is not destroyed

//input:
//either Bag pointer/slot or Ent/BagID/slot
//count is # of items to remove,  count = -1 means remove all


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


AUTO_TRANS_HELPER;
void inv_InventorySlotSetNameFromIndex(ATH_ARG NOCONST(InventorySlot)* pSlot, S32 iIndex)
{
	char achIndex[30];
	sprintf(achIndex, "%6.6d", iIndex);
	pSlot->pchName = allocAddString(achIndex);
}

AUTO_TRANS_HELPER_SIMPLE;
NOCONST(InventorySlot) *inv_InventorySlotCreate(S32 iIndex)
{
	NOCONST(InventorySlot) *pSlot = StructCreateNoConst(parse_InventorySlot);
	inv_InventorySlotSetNameFromIndex(pSlot, iIndex);
	return pSlot;
}

// Convenience function to get a bag category by name
InvBagCategory* inv_GetBagCategoryByName(const char* pchBagCategoryName)
{
	if (pchBagCategoryName && pchBagCategoryName[0])
	{
		return eaIndexedGetUsingString(&s_BagCategories.eaCategories, pchBagCategoryName);
	}
	return NULL;
}

// Get a bag category from a list of BagIDs
InvBagCategory* inv_GetBagCategoryByBagIDs(InvBagIDs* peBagIDs)
{
	if (eaSize(&s_BagCategories.eaCategories))
	{
		static InvBagIDs* s_peBagIDs = NULL;
		S32 i, j;

		eaiCopy(&s_peBagIDs, &peBagIDs);
		eaiQSort(s_peBagIDs, intCmp);

		for (i = eaSize(&s_BagCategories.eaCategories)-1; i >= 0; i--)
		{
			InvBagCategory* pBagCategory = s_BagCategories.eaCategories[i];
			
			if (eaiSize(&s_peBagIDs) != eaiSize(&pBagCategory->peBagIDs))
			{
				continue;
			}
			for (j = eaiSize(&pBagCategory->peBagIDs)-1; j >= 0; j--)
			{
				if (pBagCategory->peBagIDs[j] != s_peBagIDs[j])
				{
					break;
				}
			}
			if (j < 0) // Found a match
			{
				return pBagCategory;
			}
		}
	}
	return NULL;
}

static int cmpInventorySlots(const InventorySlot** ppSlotA, const InventorySlot** ppSlotB)
{
	const InventorySlot* pSlotA = (*ppSlotA);
	const InventorySlot* pSlotB = (*ppSlotB);
	ItemDef* pItemDefA = SAFE_GET_REF(pSlotA->pItem, hItem);
	ItemDef* pItemDefB = SAFE_GET_REF(pSlotB->pItem, hItem);
	InvBagIDs eRestrictBagA = InvBagIDs_None;
	InvBagIDs eRestrictBagB = InvBagIDs_None;
	int i;

	if (ISNULL(pItemDefA))
	{
		return 1;
	}
	if (ISNULL(pItemDefB))
	{
		return -1;
	}
	if (pItemDefA->eType != pItemDefB->eType)
	{
		return (int)(pItemDefA->eType - pItemDefB->eType);
	}
	for (i = eaiSize(&pItemDefA->peRestrictBagIDs)-1; i >= 0; i--)
	{
		if (pItemDefA->peRestrictBagIDs[i] > eRestrictBagA)
			eRestrictBagA = pItemDefA->peRestrictBagIDs[i];
	}
	for (i = eaiSize(&pItemDefB->peRestrictBagIDs)-1; i >= 0; i--)
	{
		if (pItemDefB->peRestrictBagIDs[i] > eRestrictBagB)
			eRestrictBagB = pItemDefB->peRestrictBagIDs[i];
	}
	if (eRestrictBagA != eRestrictBagB)
	{
		return (int)(eRestrictBagA - eRestrictBagB);
	}
	return stricmp(pSlotA->pItem->pchDisplayName, pSlotB->pItem->pchDisplayName);
}


S64EarrayWrapper* inv_bag_CreateSortData(Entity* pEnt, InventoryBag* pBag)
{
	S64EarrayWrapper* pResult = NULL;

	if (pEnt && pBag)
	{
		InventorySlot** ppSlots = NULL;
		int i;

		eaPushEArray(&ppSlots, &pBag->ppIndexedInventorySlots);
		
		for (i = eaSize(&ppSlots)-1; i >= 0; i--)
		{
			InventorySlot* pSlot = ppSlots[i];
			if (pSlot->pItem)
			{
				// Generate the display name for this item
				item_GetName(pSlot->pItem, pEnt);
			}
		}

		pResult = StructCreate(parse_S64EarrayWrapper);
		if (eaSize(&ppSlots) > 1)
		{
			eaQSort(ppSlots, cmpInventorySlots);
		}
		eaSetSizeStruct(&pResult->eaValues, parse_S64Struct, eaSize(&ppSlots));
		for (i = 0; i < eaSize(&ppSlots); i++)
		{
			InventorySlot* pSlot = ppSlots[i];
			if (pSlot->pItem)
			{
				pResult->eaValues[i]->iInt = pSlot->pItem->id;
			}
			else
			{
				pResult->eaValues[i]->iInt = 0;
			}
		}
		eaDestroy(&ppSlots);
	}
	return pResult;
}


//create an item instance from a def
NOCONST(Item)* inv_ItemInstanceFromDefName( const char* ItemDefName, int iCharacterLevel, int overrideLevel, const char *pcRank, AllegianceDef *pAllegiance,
	AllegianceDef *pSubAllegiance, bool bUseOverrideLevel, U32 *pSeed)
{
	ItemDef* pItemDef = item_DefFromName(ItemDefName);
	
	if(pItemDef)
	{
		NOCONST(Item)* pItem = NULL;
#ifdef GAMECLIENT
		//on the client, if we're trying to make a fake pet for the UI, bail out on missing refs so we can try again next frame.
		if (pItemDef->eType == kItemType_SuperCritterPet)
		{
			SuperCritterPetDef* pPetDef = GET_REF(pItemDef->hSCPdef);
			if (!pPetDef ||
				REF_IS_SET_BUT_ABSENT(pPetDef->hCachedClassDef) ||
				REF_IS_SET_BUT_ABSENT(pPetDef->hCritterDef))
				return NULL;
		}
#endif
		pItem = StructCreateNoConst(parse_Item);
		
		SET_HANDLE_FROM_REFDATA(g_hItemDict,pItemDef->pchName,pItem->hItem);
		
		if (pItemDef->flags & kItemDefFlag_Unidentified_Unsafe)
		{
			pItem->flags |= kItemFlag_Unidentified_Unsafe;
		}
#ifdef GAMESERVER
		if (pItemDef->flags & kItemDefFlag_RandomAlgoQuality)
		{
			NOCONST(AlgoItemProps)* pProps = (NOCONST(AlgoItemProps)*)item_trh_GetOrCreateAlgoProperties(pItem);
			int iTableIdx = (pItemDef->flags & kItemDefFlag_LevelFromSource ? overrideLevel : pItemDef->iLevel) - 1;
			if(iTableIdx >= g_AlgoTables.MaxLevel)
				iTableIdx = g_AlgoTables.MaxLevel - 1;//because GetRandomQuality counts backwards.

			pProps->Quality = algoitem_GetRandomQuality(iTableIdx, pcRank, NULL, true);
			pProps->Quality = pProps->Quality >= 0 ? pProps->Quality : pItemDef->Quality;
		}
		// TODO (JDJ): Quality is N/A for some types of defs (everything aside from components, devices, weapons, bags,
		// algopets, and upgrades (see ItemEditor.c); for those, it'd be safer to set the quality to White manually
		// in case the def has some lingering Quality value

		
		if(bUseOverrideLevel && overrideLevel && pItemDef->eType != kItemType_SuperCritterPet)
		{
			item_trh_SetAlgoPropsLevel(pItem, min(overrideLevel, g_AlgoTables.MaxLevel));
			item_trh_SetAlgoPropsMinLevel(pItem, min(overrideLevel, g_AlgoTables.MaxLevel));
			pItem->flags |= kItemFlag_Algo;	// item is now an algo type item, i.e. has level changed
		}
		else if ((pItemDef->flags & kItemDefFlag_LevelFromSource) && overrideLevel)
		{
			item_trh_SetAlgoPropsLevel(pItem, min(overrideLevel, g_AlgoTables.MaxLevel));
			item_trh_SetAlgoPropsMinLevel(pItem, min(overrideLevel, g_AlgoTables.MaxLevel));
			pItem->flags |= kItemFlag_Algo;	// item is now an algo type item, i.e. has level changed
		} 
		else if ((pItemDef->flags & kItemDefFlag_ScaleWhenBought) && iCharacterLevel)
		{
			item_trh_SetAlgoPropsLevel(pItem, min(iCharacterLevel, g_AlgoTables.MaxLevel));
			item_trh_SetAlgoPropsMinLevel(pItem, min(iCharacterLevel, g_AlgoTables.MaxLevel));
			pItem->flags |= kItemFlag_Algo;	// item is now an algo type item, i.e. has level changed
		}
#else
		
		if(bUseOverrideLevel && overrideLevel && pItemDef->eType != kItemType_SuperCritterPet)
		{
			item_trh_SetAlgoPropsLevel(pItem, overrideLevel);
			item_trh_SetAlgoPropsMinLevel(pItem, overrideLevel);
		}
		else if ((pItemDef->flags & kItemDefFlag_LevelFromSource) && overrideLevel)
		{
			item_trh_SetAlgoPropsLevel(pItem, overrideLevel);
			item_trh_SetAlgoPropsMinLevel(pItem, overrideLevel);
		} 
		else if ((pItemDef->flags & kItemDefFlag_ScaleWhenBought) && iCharacterLevel) 
		{
			item_trh_SetAlgoPropsLevel(pItem, iCharacterLevel);
			item_trh_SetAlgoPropsMinLevel(pItem, iCharacterLevel);
		}
#endif
		if(!bUseOverrideLevel)
		{
			AlgoItemLevelsDef *pAlgoItemLevels = eaIndexedGetUsingString(&g_CommonAlgoTables.ppAlgoItemLevels, StaticDefineIntRevLookup(ItemQualityEnum, item_trh_GetQuality(pItem)));
			if ((pItemDef->flags & kItemDefFlag_LevelFromQuality) != 0 && pAlgoItemLevels ) {
				if(item_trh_GetMinLevel(pItem) > 0)
					item_trh_SetAlgoPropsLevel(pItem, pAlgoItemLevels->level[item_trh_GetMinLevel(pItem)-1]);
				else if(item_trh_GetLevel(pItem) > 0)
					item_trh_SetAlgoPropsLevel(pItem, pAlgoItemLevels->level[item_trh_GetLevel(pItem)-1]);
			}
		}
	
		// create the powers here
		if (!item_trh_FixupPowers(pItem) && IsClient())
		{
			//If we failed to create all the powers on the client (i.e. a fake item), return NULL so we can try again later.
			StructDestroyNoConst(parse_Item, pItem);
			return NULL;
		}

		item_trh_FillInPreSlottedGems(pItem, iCharacterLevel, pcRank, pAllegiance, pSeed);
		
		// run the algo pet here
		if(pItemDef->eType == kItemType_AlgoPet || pItemDef->eType == kItemType_STOBridgeOfficer)
		{
			NOCONST(SpecialItemProps)* pProps = item_trh_GetOrCreateSpecialProperties(pItem);
			pProps->pAlgoPet = algoPetDef_CreateNew(GET_REF(pItemDef->hAlgoPet),GET_REF(pItemDef->hPetDef),item_GetQuality((Item*)pItem),item_trh_GetLevel(pItem),pAllegiance,pSubAllegiance,pSeed);
		}
		//supercritterpets
		if(pItemDef->eType == kItemType_SuperCritterPet)
		{
			NOCONST(SpecialItemProps)* pProps = item_trh_GetOrCreateSpecialProperties(pItem);
			SuperCritterPetDef* pPetDef = GET_REF(pItemDef->hSCPdef);
			if (bUseOverrideLevel && overrideLevel)
				pProps->pSuperCritterPet = scp_CreateFromDef(pPetDef, CONTAINER_RECONST(Item, pItem), overrideLevel);
			else
				pProps->pSuperCritterPet = scp_CreateFromDef(pPetDef, CONTAINER_RECONST(Item, pItem), 1);

		}

		if (*(U32 *)&pItemDef->iPowerFactor > 0xff)
		{
			Errorf("Powerfactor %d magnitude is too large.",pItemDef->iPowerFactor);
		}

		return pItem;
	}
	
	return NULL;
}

NOCONST(Item)* inv_UnidentifiedWrapperFromDefName(const char* pchWrapperDefName, const char* pchResultDefName)
{
	NOCONST(Item)* pWrapper = inv_ItemInstanceFromDefName(pchWrapperDefName, 0, 0, NULL, NULL, NULL, false, NULL);
	NOCONST(SpecialItemProps)* pProps = item_trh_GetOrCreateSpecialProperties(pWrapper);
	REF_HANDLE_SET_FROM_STRING(g_hItemDict, pchResultDefName, pProps->hIdentifiedItemDef);
	if (!GET_REF(pProps->hIdentifiedItemDef))
	{
		//failed
		Errorf("An unidentified wrapper item failed to set its result item def. Wrapper: %s Identified def: %s", pchWrapperDefName, pchResultDefName);
		StructDestroyNoConst(parse_Item, pWrapper);
		return NULL;
	}
	return pWrapper;
}

U32 DEFAULT_LATELINK_inv_ent_getEntityItemQuality(Entity *pEnt)
{
	return kItemQuality_None;
}

// Performs checks to see if the saved pet can be transferred, and creates the item from the saved pet.
// Does not remove the saved pet
NOCONST(Item)* inv_ent_ContainerItemInstanceFromSavedPet(Entity* pEnt, ContainerID iSavedPetID)
{
	ItemDef *pItemDef;
	PetDef *pPetDef;
	char idBuf[128];
	Entity *pSavedPet = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(iSavedPetID, idBuf));
	int i;
	ItemQuality eQuality = kItemQuality_None;

	//Verify that the entity adding the saved pet, is the owner of the saved pet
	if(!pEnt || !pEnt->pPlayer || !pSavedPet || !pSavedPet->pSaved || pSavedPet->pSaved->conOwner.containerID != pEnt->myContainerID || pSavedPet->pSaved->conOwner.containerType != pEnt->myEntityType
		|| !pEnt->pSaved)
		return NULL;

	pPetDef = GET_REF(pSavedPet->pCritter->petDef);
	pItemDef = pPetDef ? GET_REF(pPetDef->hTradableItem) : NULL;

	if(!pItemDef || pItemDef->eType != kItemType_Container)
		return NULL;

	for(i=eaSize(&pEnt->pSaved->ppOwnedContainers)-1;i>=0;i--)
	{
		if(iSavedPetID == pEnt->pSaved->ppOwnedContainers[i]->conID)
		{
			PuppetEntity *pPuppet = SavedPet_GetPuppetFromPet(pEnt,pEnt->pSaved->ppOwnedContainers[i]);
			Entity *pEntPet = SavedPet_GetEntity(entGetPartitionIdx(pEnt), pEnt->pSaved->ppOwnedContainers[i]);

			// Pet is alive
			if(pEnt->pSaved->ppOwnedContainers[i]->curEntity)
			{
#if GAMESERVER || GAMECLIENT
				notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("TradeError_OnlinePet"), NULL, NULL);
#endif
				return NULL;
			}

			// Puppet is active
			if(pPuppet && pPuppet->eState == PUPPETSTATE_ACTIVE)
			{
#if GAMESERVER || GAMECLIENT
				notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("TradeError_ActivePuppet"), NULL, NULL);
#endif
				return NULL;
			}

			// Pet has items in its inventory
			if(pEntPet && pEntPet->pInventoryV2)
			{
				int NumBags,ItemCount,ii;

				NumBags = eaSize(&pEntPet->pInventoryV2->ppInventoryBags);

				ItemCount = 0;
				for(ii=0;ii<NumBags;ii++)
				{
					if(pEntPet->pInventoryV2->ppInventoryBags[ii]->BagID != InvBagIDs_Numeric)
						ItemCount += inv_bag_CountItems(pEntPet->pInventoryV2->ppInventoryBags[ii], NULL);
				}

				if(ItemCount)
				{
#if GAMESERVER || GAMECLIENT
					notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("TradeError_PetWithItems"), NULL, NULL);
#endif
					return NULL;
				}
			}

			eQuality = inv_ent_getEntityItemQuality(pEntPet);
			break;
		}
	}
	// Pet not found
	if(i==-1)
	{
#if GAMESERVER || GAMECLIENT
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("TradeError_PetNotFound"), NULL, NULL);
#endif
		return NULL;
	}

	return inv_ContainerItemInstanceFromDef(pItemDef, GLOBALTYPE_ENTITYSAVEDPET, iSavedPetID, 0, pEnt ? GET_REF(pEnt->hAllegiance) : NULL, pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL, eQuality);
}







// 
// ===============================================================================

NOCONST(Item)* inv_ContainerItemInstanceFromDef(ItemDef *pItemDef, GlobalType eContainerType, ContainerID eContainerID,const char *pcRank, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, ItemQuality eQuality)
{
	NOCONST(Item) *pReturn = NULL;
	if(eContainerType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		char idBuf[128];
		pReturn = inv_ItemInstanceFromDefName(pItemDef->pchName,0,0,pcRank,pAllegiance,pSubAllegiance,false,NULL);
		pReturn->pSpecialProps = StructCreateNoConst(parse_SpecialItemProps);
		pReturn->pSpecialProps->pContainerInfo = StructCreateNoConst(parse_ItemContainerInfo);
		pReturn->pSpecialProps->pContainerInfo->eContainerType = eContainerType;
		if (eQuality != kItemQuality_None)
		{
			if (!pReturn->pAlgoProps)
			{
				pReturn->pAlgoProps = StructCreateNoConst(parse_AlgoItemProps);
				pReturn->pAlgoProps->Quality = eQuality;
			}
		}
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(eContainerID, idBuf),pReturn->pSpecialProps->pContainerInfo->hSavedPet);
	}

	return pReturn;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
void inv_trh_AddSlotToBagWithDef(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, InventorySlotIDDef *pDef)
{
	//First, look for an empty slot to place the def on

	int iNumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;
	NOCONST(InventorySlot) *pSlot = NULL;

	if(!pDef)
		return;

	for(ii=0; ii<iNumSlots; ii++)
	{
		pSlot = pBag->ppIndexedInventorySlots[ii];
		if(!pSlot->pItem && !IS_HANDLE_ACTIVE(pSlot->hSlotType))
		{
			//make this one the slot type
			SET_HANDLE_FROM_REFERENT(g_hInvSlotDefDict,pDef,pSlot->hSlotType);
			return;
		}
	}

	pSlot = inv_InventorySlotCreate(ii);
	SET_HANDLE_FROM_REFERENT(g_hInvSlotDefDict,pDef,pSlot->hSlotType);
	eaPush(&pBag->ppIndexedInventorySlots,pSlot);
}

//return pointer to an inv slot structure from given bag pointer & slot idx
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
NOCONST(InventorySlot)* inv_trh_GetSlotPtr(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	NOCONST(InventorySlot)* pSlot = NULL;

	if ( (SlotIdx >= 0) && (SlotIdx < NumSlots) )
	{
		pSlot = pBag->ppIndexedInventorySlots[SlotIdx];
	}

	return pSlot;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
NOCONST(InventorySlot)* inv_ent_trh_GetSlotPtr(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(InventorySlot)* pSlot = NULL;

	if ( ISNULL(pEnt))
		return NULL;

	if ( ISNULL(pEnt->pInventoryV2))
		return NULL;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);

	if (ISNULL(pBag))
		return NULL;

	pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, SlotIdx);

	if (ISNULL(pSlot))
		return NULL;

	return pSlot;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[]");
NOCONST(InventorySlot)* inv_guildbank_trh_GetSlotPtr(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, InvBagIDs iBagID, int iSlotIdx)
{
	NOCONST(InventoryBag)* pBag = NULL;
	
	if (ISNULL(pGuildBank) || ISNULL(pGuildBank->pInventoryV2)) {
		return NULL;
	}
	
	pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, iBagID);
	if (ISNULL(pBag)) {
		return NULL;
	}
	
	return inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, iSlotIdx);
}

//Given a bag and slot index, return the Item pointer for that slot
//return NULL if no valid item for the given info
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
NOCONST(Item)* inv_trh_GetItemPtrFromSlot(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx)
{
	NOCONST(InventorySlot)* pSlot = inv_trh_GetSlotPtr(ATR_PASS_ARGS, pBag, SlotIdx);

	if (NONNULL(pSlot))
	{
		return pSlot->pItem;
	}

	return NULL;
}

//check if a given item def name matches an item in a specified bag/slot
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
bool inv_trh_MatchingItemInSlot(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx, const char* ItemDefName)
{
	const char* tmpName = inv_trh_GetItemDefNameFromItem(ATR_PASS_ARGS, inv_trh_GetItemPtrFromSlot(ATR_PASS_ARGS, pBag,SlotIdx));

	if (tmpName)
	{
		ANALYSIS_ASSUME(tmpName != NULL);
		return (strcmp(tmpName, ItemDefName) == 0);
	}

	return false;
}

AUTO_TRANS_HELPER;
bool inv_trh_CanEquipUnique(ATR_ARGS, ItemDef* pItemDef, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	S32 i;

	if(ISNULL(pItemDef) || ISNULL(pBag))
	{
		return false;
	}

	if((pItemDef->flags & kItemDefFlag_UniqueEquipOnePerBag) == 0)
	{
		// not a unique equip item
		return true;
	}

	if((invbag_trh_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_DeviceBag)) == 0)
	{
		// not an equip bag
		return true;
	}

	// see if we have the same itemdef in this bag
	for(i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); ++i)
	{
		NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[i];
		if(NONNULL(pSlot))
		{
			if(pSlot->pItem && 
				inv_trh_MatchingItemInSlot(ATR_PASS_ARGS, pBag, i, pItemDef->pchName))
			{
				return false;			
			}
		}
	}

	return true;
}

AUTO_TRANS_HELPER;
bool inv_trh_CanEquipUniqueCheckSwap(ATR_ARGS, 
									 ATH_ARG NOCONST(Entity)* pEnt, 
									 ItemDef* pSrcItemDef, 
									 ItemDef* pDstItemDef, 
									 NOCONST(InventoryBag)* pSrcBag, 
									 NOCONST(InventoryBag)* pDstBag)
{
	if ((!pSrcItemDef || !(pSrcItemDef->flags & kItemDefFlag_UniqueEquipOnePerBag)) && 
		(!pDstItemDef || !(pDstItemDef->flags & kItemDefFlag_UniqueEquipOnePerBag)))
	{
		return true;
	}
	if (NONNULL(pSrcBag) && NONNULL(pDstBag) && pSrcBag->BagID == pDstBag->BagID)
	{
		return true;
	}
	if (pSrcItemDef && pDstItemDef && pSrcItemDef == pDstItemDef)
	{
		return true;
	}
	if (pSrcItemDef && !inv_CanEquipUniqueNoConst(pSrcItemDef, pDstBag))
	{
		return false;
	}
	if (pDstItemDef && !inv_CanEquipUniqueNoConst(pDstItemDef, pSrcBag))
	{
		return false;
	}
	return true;
}

bool inv_bag_HasAnyUniqueItems(InventoryBag* pBag)
{
	if (pBag)
	{
		int i;
		for (i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
		{
			InventorySlot* pSlot = pBag->ppIndexedInventorySlots[i];
			ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
			if (pItemDef && (pItemDef->flags & kItemDefFlag_Unique))
			{
				return true;
			}
		}
	}
	return false;
}

// Returns true if there any unique items in the bag array
bool inv_CheckUniqueItemsInBags(InventoryBag** eaBags)
{
	int i;
	for (i = eaSize(&eaBags)-1; i >= 0; i--)
	{
		if (inv_bag_HasAnyUniqueItems(eaBags[i]))
		{
			return true;
		}
	}
	return false;
}

//get the specified slot into an inventory bag, creates the slot if it doesn't already exist.
//allocates a new slot if one is required
//SlotIdx = -1 specifies first open slot, or end of bag | SlotIdx = -2 specifies end of bag only
//returns dest slot idx in the dest bag that item/bag was put into, -1 on error
AUTO_TRANS_HELPER;
int inv_trh_GetSlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, const char *name, InventorySlotIDDef *pIDDef)
{
	int NumSlots = eaSize(&pDestBag->ppIndexedInventorySlots);
	int DestSlotIdx = -1;
	int ii;
	NOCONST(InventorySlot)* pSlot = NULL;


	//check for specific slot
	if ( SlotIdx >= 0 )
	{
		int iMaxSlots;
		//verify that this is not a name indexed bag, if name index do not allow slot to be specified
		if (invbag_trh_flags(pDestBag) & InvBagFlag_NameIndexed)
		{
			Errorf("Attempt to specify slot in name indexed bag %s\n", StaticDefineIntRevLookup(InvBagIDsEnum,invbag_trh_bagid(pDestBag)) );
		}

		//make sure that there is room in this bag
		//max count of 0 means unlimited size
		iMaxSlots = invbag_trh_maxslots(pEnt, pDestBag);
		if (iMaxSlots >= 0 && SlotIdx+1 > iMaxSlots)
			return -1;

		DestSlotIdx = SlotIdx;
	}

	//else see if there is an open slot in this bag, if we're not name indexed
	if ( (DestSlotIdx==-1) && ( SlotIdx == -1 ) && ( NumSlots > 0 ) && !(invbag_trh_flags(pDestBag) & InvBagFlag_NameIndexed))
	{
		for (ii=0; ii<NumSlots; ii++)
		{
			pSlot = pDestBag->ppIndexedInventorySlots[ii];

			if ( !pSlot->pItem )
			{
				if(IS_HANDLE_ACTIVE(pSlot->hSlotType))
				{
					if(GET_REF(pSlot->hSlotType) != pIDDef)
						continue;
				}
				DestSlotIdx = ii;
				break;
			}
		}
	}

	if(DestSlotIdx==-1 && pIDDef && invbag_trh_bagid(pDestBag) == pIDDef->eMainBagID) //will not add slots that require a def
		return -1;

	if(DestSlotIdx>=0 && pIDDef && invbag_trh_bagid(pDestBag) == pIDDef->eMainBagID) //Check to make sure the item fits in this slot
	{
		if(DestSlotIdx >= NumSlots)
			return -1;
		else
		{
			if(GET_REF(pDestBag->ppIndexedInventorySlots[DestSlotIdx]->hSlotType) != pIDDef)
				return -1;
		}
	}

	//else if not empty slot, check for end of bag
	if ( (DestSlotIdx==-1) && (SlotIdx < 0) )
	{
		int iMaxSlots = invbag_trh_maxslots(pEnt, pDestBag);
		//make sure that there is room in this bag
		//max count of -1 means unlimited size
		if (iMaxSlots >= 0 && NumSlots+1 > iMaxSlots)
			return -1;

		DestSlotIdx = NumSlots;
	}

	//fill out earray with empty slots if required
	for (ii=NumSlots; ii<=DestSlotIdx; ii++)
	{
		if ( invbag_trh_flags(pDestBag) & InvBagFlag_NameIndexed )
		{
			pSlot = StructCreateNoConst(parse_InventorySlot);
			assertmsgf(name && name[0], "Attempt to add slot to name indexed bag without a valid name. %s\n", StaticDefineIntRevLookup(InvBagIDsEnum,invbag_trh_bagid(pDestBag)) );
			pSlot->pchName = allocAddString(name);	
		}
		else
			pSlot = inv_InventorySlotCreate(ii);
		eaPushUnique(&pDestBag->ppIndexedInventorySlots, pSlot);
		if (ii==DestSlotIdx)
		{
			DestSlotIdx = eaIndexedFindUsingString(&pDestBag->ppIndexedInventorySlots, pSlot->pchName );
			break;
		}
	}

	return DestSlotIdx;
}



//remove any empty slots from the end of a Bag
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots, .Inv_Def, .Bagid");
void inv_trh_CollapseBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;
	NOCONST(InventorySlot)* pSlot = NULL;

	for (ii=NumSlots-1; ii>=0; ii--)
	{
		pSlot = pBag->ppIndexedInventorySlots[ii];

		if ( !pSlot->pItem && !IS_HANDLE_ACTIVE(pSlot->hSlotType))
		{
			NOCONST(InventorySlot)* pPoppedSlot = eaRemove(&pBag->ppIndexedInventorySlots, ii);

			//!!!! pSlot and pPoppedSlot should be equal here, if not something is wrong
			//should be able to free either one
			assert( pSlot == pPoppedSlot );

			StructDestroyNoConst(parse_InventorySlot, pSlot);
		}
		else if( !(invbag_trh_flags(pBag) & InvBagFlag_NameIndexed) )
		{
			//done on first non-empty slot if bag is not indexed by name
			break;
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
bool inv_bag_trh_BagEmpty(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;
	NOCONST(InventorySlot)* pSlot = NULL;

	//if bag has Max slots, scan for an empty slot
	for (ii=NumSlots-1; ii>=0; ii--)
	{
		pSlot = pBag->ppIndexedInventorySlots[ii];

		if ( pSlot->pItem )
		{
			return false;
		}
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool inv_ent_trh_BagEmpty(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pDestBag = NULL;


	if ( ISNULL(pEnt))
		return false;

	if ( ISNULL(pEnt->pInventoryV2))
		return false;

	pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pDestBag))
	{ 
		TRANSACTION_APPEND_LOG_FAILURE( "No Bag %s on Ent %s", 
			                            StaticDefineIntRevLookup(InvBagIDsEnum,BagID), 
										pEnt->debugName );
		return false;
	}

	return inv_bag_trh_BagEmpty(ATR_PASS_ARGS, pDestBag);
}



AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], .Pplayer.Playertype")
ATR_LOCKS(pBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Inv_Def, .Bagid");
bool inv_bag_trh_BagFull(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, InventorySlotIDDef *pInvSlotIDDef)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;
	NOCONST(InventorySlot)* pSlot = NULL;
	int MaxSlots = invbag_trh_maxslots(pEnt, pBag);

	if (MaxSlots < 0 || (NumSlots < MaxSlots))
		return false;

	//if bag has Max slots, scan for an empty slot
	for (ii=NumSlots-1; ii>=0; ii--)
	{
		pSlot = pBag->ppIndexedInventorySlots[ii];

		if ( !pSlot->pItem 
			&& (!IS_HANDLE_ACTIVE(pSlot->hSlotType) || GET_REF(pSlot->hSlotType) == pInvSlotIDDef))
		{
			return false;
		}
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], .Pplayer.Playertype")
ATR_LOCKS(pBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Inv_Def, .Bagid");
bool inv_bag_trh_GetNumEmptySlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, InventorySlotIDDef *pInvSlotIDDef)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;
	S32 iUsedSlots = 0;
	NOCONST(InventorySlot)* pSlot = NULL;
	int MaxSlots = invbag_trh_maxslots(pEnt, pBag);

	// count used slots
	for (ii = 0 ; ii < NumSlots; ++ii)
	{
		pSlot = pBag->ppIndexedInventorySlots[ii];

		if(!pSlot->pItem 
			&& (!IS_HANDLE_ACTIVE(pSlot->hSlotType) || GET_REF(pSlot->hSlotType) == pInvSlotIDDef))
		{
			// empty
		}
		else
		{
			++iUsedSlots;
		}
	}

	return MaxSlots - iUsedSlots;
}

AUTO_TRANS_HELPER;
int inv_bag_trh_GetFirstEmptySlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	int NumSlots = 0;
	int ii;
	S32 iUsedSlots = 0;
	NOCONST(InventorySlot)* pSlot = NULL;
	int MaxSlots = 0;

	if (NONNULL(pBag))
	{
		NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
		MaxSlots = invbag_trh_maxslots(pEnt, pBag);
		
		for (ii = 0 ; ii < NumSlots; ++ii)
		{
			pSlot = pBag->ppIndexedInventorySlots[ii];

			if(!pSlot->pItem)
			{
				return ii;
			}
		}
		return MaxSlots > NumSlots ? NumSlots : -1;
	}
	return -1;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, .pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]"); 
int inv_ent_trh_GetFirstEmptySlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs eBag)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, eBag, NULL);

	return inv_bag_trh_GetFirstEmptySlot(ATR_PASS_ARGS, pEnt, pBag);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool inv_ent_trh_BagFull(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, InventorySlotIDDef *pInvSlotID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pDestBag = NULL;


	if ( ISNULL(pEnt))
		return false;

	if ( ISNULL(pEnt->pInventoryV2))
		return false;

	pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pDestBag))
	{ 
		TRANSACTION_APPEND_LOG_FAILURE( "No Bag %s on Ent %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), pEnt->debugName );
		return false;
	}

	return inv_bag_trh_BagFull(ATR_PASS_ARGS, pEnt, pDestBag, pInvSlotID);
}


//swap items between two bag/slots
//returns 0 on success, -1 on error
//swaps all stacked items in each slot
AUTO_TRANS_HELPER
	ATR_LOCKS(pDestEnt, ".Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Conowner.Containertype, .Psaved.Conowner.Containerid, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pDestBag, ".*")
	ATR_LOCKS(pSrcEnt, ".Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Conowner.Containertype, .Psaved.Conowner.Containerid, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(eaSrcPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pSrcBag, ".*");
bool inv_trh_SwapItems(	ATR_ARGS, bool bSilent, 
						ATH_ARG NOCONST(Entity)* pDestEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, 
						ATH_ARG NOCONST(InventoryBag)* pDestBag, int DestSlotIdx, 
						ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
						ATH_ARG NOCONST(InventoryBag)* pSrcBag, int SrcSlotIdx,
						const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
						GameAccountDataExtract *pExtract)
{
	//if both slot indices are valid, perform the swap
	if (DestSlotIdx>=0 && SrcSlotIdx>=0)
	{
		NOCONST(Item)* pItemSrc;
		NOCONST(Item)* pItemDst;
		ItemAddFlags eFlags = ItemAdd_OverrideStackRules | (bSilent ? ItemAdd_Silent : 0);
		bool bIsOwnerSame = entity_trh_IsOwnerSame(ATR_PASS_ARGS, pSrcEnt, pDestEnt);
		
		pItemSrc = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pSrcEnt, true, pSrcBag, SrcSlotIdx, -1, pSrcReason);
		
		if ( ISNULL(pItemSrc) )
			return false;
		
		pItemDst = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pDestEnt, true, pDestBag, DestSlotIdx, -1, pDestReason);

		if ( ISNULL(pItemDst) )
		{
			inv_bag_trh_AddItem(ATR_PASS_ARGS, pSrcEnt, eaSrcPets, pSrcBag, SrcSlotIdx, pItemSrc, eFlags, NULL, pSrcReason, pExtract);
			return false;
		}

		//clear the power ids if the items are moving between owner ents
		if (!bIsOwnerSame)
		{
			item_trh_ClearPowerIDs(pItemSrc);
			item_trh_ClearPowerIDs(pItemDst);
		}

		// if ents are the same or have the same owner, ignore the uniqueness flag
		if (bIsOwnerSame)
			eFlags |= ItemAdd_IgnoreUnique;

		inv_bag_trh_AddItem(ATR_PASS_ARGS, pSrcEnt, eaSrcPets, pSrcBag, SrcSlotIdx, pItemDst, eFlags, NULL, pSrcReason, pExtract);
		inv_bag_trh_AddItem(ATR_PASS_ARGS, pDestEnt, eaDstPets, pDestBag, DestSlotIdx, pItemSrc, eFlags, NULL, pDestReason, pExtract);
			
		return true;
	}

	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
NOCONST(InventorySlot)* inv_trh_GetSlotFromBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx)
{
	NOCONST(InventorySlot)* pSlot = NULL;
	if(NONNULL(pBag))
	{
		int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);

		//make sure slot is in range
		if ( (SlotIdx < 0) || ( SlotIdx >= NumSlots) )
			return NULL;

		pSlot = pBag->ppIndexedInventorySlots[SlotIdx];
	}

	return pSlot;
}


//return a pointer to the name of an ItemDef when given an item pointer
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Hitem");
const char* inv_trh_GetItemDefNameFromItem(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pItemDef;

	if ( ISNULL(pItem) )
		return NULL;

	pItemDef = GET_REF(pItem->hItem);

	if ( NONNULL(pItemDef) )
	{
		return pItemDef->pchName;
	}

	return NULL;
}
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//inventory data loading routines


DefineContext *g_InvExtraBagIDs;
DictionaryHandle g_hDefaultInventoryDict;

static bool defaultinv_validate( DefaultInventory *inv_def )
{
	S32 i, j, iSize = eaSize(&inv_def->InventoryBags);
	bool bSuccess = true;

	for (i = 0; i < iSize; i++)
	{
		InvBagDef* pBagDef = inv_def->InventoryBags[i];

		if (!GET_REF(pBagDef->msgBagFull.hMessage) && REF_STRING_FROM_HANDLE(pBagDef->msgBagFull.hMessage))
		{
			ErrorFilenamef(pBagDef->fname, "InvBagDef references non-existent BagFull message '%s'", 
				REF_STRING_FROM_HANDLE(pBagDef->msgBagFull.hMessage));
			bSuccess = false;
		}

		for (j = eaSize(&pBagDef->ppItemMoveEvents)-1; j >= 0; j--)
		{
			InvBagItemMoveEvent* pItemMoveEvent = pBagDef->ppItemMoveEvents[j];

			if (pItemMoveEvent->pchPowerCooldownCategory && pItemMoveEvent->pchPowerCooldownCategory[0])
			{
				S32 iCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pItemMoveEvent->pchPowerCooldownCategory);

				if (iCategory < 0)
				{
					ErrorFilenamef(pBagDef->fname, "InvBagItemMoveDef: Invalid power category (%s) on bag (%s)", 
						pItemMoveEvent->pchPowerCooldownCategory, StaticDefineIntRevLookup(InvBagIDsEnum,pBagDef->BagID));
					bSuccess = false;
				}
			}
		}
		if (pBagDef->eType == InvBagType_ItemLite)
		{
			if (pBagDef->flags & ~(InvBagFlag_GuildBankBag))
			{
				ErrorFilenamef(pBagDef->fname, "InvBagDef %s is of type ItemLite and only supports the GuildBankBag flag. Lite bags always behave as though they are NameIndexed/StorageOnly/Nocopy", StaticDefineIntRevLookup(InvBagIDsEnum,pBagDef->BagID));
			}
		}
		else
		{
			if ((pBagDef->BagID == InvBagIDs_Numeric) ||
				(pBagDef->BagID == InvBagIDs_Tokens) || 
				(pBagDef->BagID == InvBagIDs_Numeric))
			{
				ErrorFilenamef(pBagDef->fname, "InvBagDef %s MUST be an ItemLite bag to work properly.", StaticDefineIntRevLookup(InvBagIDsEnum,pBagDef->BagID));
			}
		}
	}
	return bSuccess;
}

static int defaultinv_validate_CB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DefaultInventory *inv_def, U32 userID)
{
    switch (eType)
    {	
		xcase RESVALIDATE_POST_TEXT_READING:
			if (IsClient()) {
				// Client does not need these references
				eaDestroyStruct(&inv_def->GrantedItems, parse_DefaultItemDef);
				return VALIDATE_HANDLED;
			}

        xcase RESVALIDATE_CHECK_REFERENCES: 
			if (IsGameServerBasedType() && !isProductionMode())
			{
				defaultinv_validate(inv_def);
				return VALIDATE_HANDLED;
			}

    };
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void inv_AutoRun(void)
{
	g_InvExtraBagIDs = DefineCreate();

	g_hInvSlotDefDict = RefSystem_RegisterSelfDefiningDictionary("InventorySlotIDDef", false, parse_InventorySlotIDDef, true, true, NULL);

	g_hDefaultInventoryDict = RefSystem_RegisterSelfDefiningDictionary("DefaultInventory", false, parse_DefaultInventory, true, true, NULL);

    resDictManageValidation(g_hDefaultInventoryDict, defaultinv_validate_CB);
}

#define INV_BAG_ID_FILE "InventoryBags"
#define INV_BAG_CATEGORY_FILE "InventoryBagCategories"
#define INV_SLOT_ID_FILE "SlotIDs"

static InvBagSlotTable* inv_FindBagSlotTableByName(const char* pchName)
{
	if (pchName && pchName[0])
	{
		return eaIndexedGetUsingString(&s_BagSlotTables.eaTables, pchName);
	}
	return NULL;
}

static void InventoryBagSlotTables_Validate(void)
{
	int i, j;
	for (i = eaSize(&s_BagSlotTables.eaTables)-1; i >= 0; i--)
	{
		InvBagSlotTable* pTable = s_BagSlotTables.eaTables[i];
		int iTableSize = eaSize(&pTable->eaEntries);
		
		if (!iTableSize)
		{
			Errorf("InvBagSlotTable %s: Table must have at least one entry", pTable->pchName);
		}

		for (j = 1; j < eaSize(&pTable->eaEntries); j++)
		{
			InvBagSlotTableEntry* pPrevEntry = pTable->eaEntries[j-1];
			InvBagSlotTableEntry* pEntry = pTable->eaEntries[j];

			if (pPrevEntry->iNumericValue > pEntry->iNumericValue)
			{
				Errorf("InvBagSlotTable %s: Table entries are not in order (%d and %d)", 
					pTable->pchName, pPrevEntry->iNumericValue, pEntry->iNumericValue);
			}
			else if (pPrevEntry->iNumericValue == pEntry->iNumericValue)
			{
				Errorf("InvBagSlotTable %s: Found two table entries with the same value %d", 
					pTable->pchName, pEntry->iNumericValue);
			}
		}
	}
}

static void InventoryBagSlotTables_LoadInternal(const char *pchPath, S32 iWhen)
{
	// Don't load on app servers, other than specified servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer() && !IsGuildServer()) {
		return;
	}
	
	StructReset(parse_InvBagSlotTables, &s_BagSlotTables);
	eaIndexedEnable(&s_BagSlotTables.eaTables, parse_InvBagSlotTable);

	loadstart_printf("Inventory Bag Slot Tables... ");

	ParserLoadFiles(NULL, 
		"defs/config/InvBagSlotTables.def",
		"InvBagSlotTables.bin",
		PARSER_OPTIONALFLAG, 
		parse_InvBagSlotTables,
		&s_BagSlotTables);

	if (isDevelopmentMode() && IsGameServerBasedType())
	{
		InventoryBagSlotTables_Validate();
	}
	loadend_printf(" done (%d Tables).", eaSize(&s_BagSlotTables.eaTables));
}

AUTO_STARTUP(InventoryBagSlotTables);
void InventoryBagSlotTables_Load(void)
{
	InventoryBagSlotTables_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/InvBagSlotTables.def", InventoryBagSlotTables_LoadInternal);
}

S32 InvBagSlotTable_FindMaxSlots(InvBagSlotTable *pTable)
{
	if (pTable)
	{
		InvBagSlotTableEntry *pTail = eaTail(&pTable->eaEntries);
		if (pTail)
		{
			return pTail->iMaxSlots;
		}
	}

	return 0;
}

S32 inv_GetBagMaxSlotTableMaxSlots(InventoryBag *pBag)
{
	if (pBag)
	{
		const InvBagDef *pDef = invbag_def(pBag);
		if (pDef && pDef->pchMaxSlotTable)
		{
			InvBagSlotTable *pTable = inv_FindBagSlotTableByName(pDef->pchMaxSlotTable);
			if (pTable)
			{
				return InvBagSlotTable_FindMaxSlots(pTable);
			}
		}
	}

	return 0;
}


AUTO_STARTUP(InventoryBagIDs);
void invIDs_Load(void)
{
	int ii; 

	// Don't load on app servers, other than specified servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer() && !IsGuildServer()) {
		return;
	}
	
	// Read in per-project Bag IDs and add them to the lookup table.

	loadstart_printf("Loading Inventory Bag IDs... ");

	ParserLoadFiles( NULL, "defs/config/"INV_BAG_ID_FILE".def", INV_BAG_ID_FILE".bin", 0, parse_InvBagExtraIDs, &g_InvBagExtraIDs);

	//do all with bCanAffectCostume = false first.
	for (ii = 0; ii < eaSize(&g_InvBagExtraIDs.ppIDs); ii++)
	{
		int j;
		InvBagExtraID *pBagID = g_InvBagExtraIDs.ppIDs[ii];
		for(j=0; j<eaSize(&pBagID->ppNames); j++)
		{
			DefineAddIntByHandle(&g_InvExtraBagIDs, pBagID->ppNames[j], InvBagIDs_Loot + 1 + ii, true);
		}
	}

	if (StaticDefineIntGetInt(InvBagIDsEnum, "Max") == -1)
		DefineAddIntByHandle(&g_InvExtraBagIDs, "Max", InvBagIDs_Loot + 1 + ii, true);

	loadend_printf("done (%d).", ii); 
}

bool invBagIDs_BagIDCanAffectCostume(InvBagIDs id)
{
	if (id > InvBagIDs_Loot)
	{
		id -= InvBagIDs_Loot+1;
		if (id < eaSize(&g_InvBagExtraIDs.ppIDs))
			return g_InvBagExtraIDs.ppIDs[id]->bCanAffectCostume;
	}
	return false;
}

AUTO_STARTUP(InvBagCategories) ASTRT_DEPS(InventoryBagIDs, InventoryBagSlotTables);
void invBagCategories_Load(void)
{
	int i;

	// Don't load on app servers, other than specified servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer() && !IsGuildServer()) {
		return;
	}
	
	loadstart_printf("Loading Inventory Bag Categories... ");

	ParserLoadFiles(NULL, "defs/config/"INV_BAG_CATEGORY_FILE".def", INV_BAG_CATEGORY_FILE".bin", PARSER_OPTIONALFLAG, parse_InvBagCategories, &s_BagCategories);
	for (i = 0; i < eaSize(&s_BagCategories.eaCategories); i++)
	{
		InvBagCategory* pBagCategory = s_BagCategories.eaCategories[i];
		if (eaiSize(&pBagCategory->peBagIDs) > 1)
		{
			eaiQSort(pBagCategory->peBagIDs, intCmp);
		}
		else
		{
			Errorf("Inventory Bag Category %s has less than 2 bag IDs specified, which is invalid", pBagCategory->pchName);
		}
	}
	loadend_printf("done (%d).", i); 
}

AUTO_RUN;
void inv_MemPoolCreate(void)
{
	MP_CREATE(BagIterator, 5);
}

AUTO_STARTUP(InventoryBags) ASTRT_DEPS(InvBagCategories, InventoryBagIDs, AnimLists, PowerCategories, ItemTags);
void inv_Load(void)
{
	// Don't load on app servers, other than specified servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer() && !IsGuildServer()) {
		return;
	}

	resLoadResourcesFromDisk(g_hInvSlotDefDict, NULL, "defs/config/"INV_SLOT_ID_FILE".def", INV_SLOT_ID_FILE".bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	if (IsClient()) {
		resLoadResourcesFromDisk(g_hDefaultInventoryDict, NULL, "defs/config/InventorySets.def", "InventorySetsClient.bin", 0 );
	} else {
		resLoadResourcesFromDisk(g_hDefaultInventoryDict, NULL, "defs/config/InventorySets.def", "InventorySets.bin", RESOURCELOAD_SHAREDMEMORY );
	}
	
    if(!RefSystem_ReferentFromString(g_hDefaultInventoryDict,"PlayerDefault"))
        ErrorFilenamef("defs/config/InventorySets.def","missing required inventory 'PlayerDefault'"); // could just make it and add it here, but feeling lazy

    if(!RefSystem_ReferentFromString(g_hDefaultInventoryDict,"Loot"))
        ErrorFilenamef("defs/config/InventorySets.def","missing required inventory 'Loot'");

}
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to add/remove bags

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_AddBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, NOCONST(InventoryBag)* pBag, GameAccountDataExtract *pExtract)
{
    if ( ISNULL(pEnt))
        return false;
    
    if (inv_trh_GetBag(ATR_PASS_ARGS,pEnt,pBag->BagID, pExtract))
        return false;
	eaIndexedAdd(&pEnt->pInventoryV2->ppInventoryBags, pBag);
	return true;
}

int inv_ent_AddLootBag(Entity* pEnt, InventoryBag* pBag)
{
	if ( ISNULL(pEnt) || ISNULL(pEnt->pCritter))
		return false;

	eaIndexedDisable(&pEnt->pCritter->eaLootBags);
	eaPush(&pEnt->pCritter->eaLootBags, pBag);
	entity_SetDirtyBit(pEnt, parse_Critter, pEnt->pCritter, 0);

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Pplitebags");
NOCONST(InventoryBagLite) *inv_ent_trh_AddLiteBagFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, InvBagDef *pBagDef)
{
	NOCONST(InventoryBagLite)* pBag = NULL;

	if(ISNULL(pEnt))
		return NULL;

	if(pBagDef == NULL)
		return NULL;

	pBag = StructCreateNoConst(parse_InventoryBagLite);
	pBag->BagID = pBagDef->BagID;

	eaIndexedPushUsingIntIfPossible(&pEnt->pInventoryV2->ppLiteBags, pBag->BagID, pBag);

	return pBag;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags[AO]");
NOCONST(InventoryBag) *inv_ent_trh_AddBagFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, InvBagDef *pBagDef)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if(ISNULL(pEnt))
		return NULL;

	if(pBagDef == NULL)
		return NULL;

	pBag = StructCreateNoConst(parse_InventoryBag);
	pBag->BagID = pBagDef->BagID;

	// if it's an activeweapon bag, setup the active slots array
	if (pBagDef->flags & InvBagFlag_ActiveWeaponBag)
	{
		S32 i, maxActiveSlots;
		maxActiveSlots = pBagDef->maxActiveSlots ? pBagDef->maxActiveSlots : 1;
		eaiSetSize(&pBag->eaiActiveSlots, maxActiveSlots);
		
		i = 0;
		if (eaiSize(&pBagDef->eaiDefaultActiveSlots))
		{
			S32 numDefault = eaiSize(&pBagDef->eaiDefaultActiveSlots);
			numDefault = MAX(numDefault, maxActiveSlots);
			for (; i < numDefault; ++i)
			{
				S32 slot = pBagDef->eaiDefaultActiveSlots[i];
				
				pBag->eaiActiveSlots[i] = (slot >= 0 && slot < pBagDef->MaxSlots) ? slot : -1;
			}
		}

		for (; i < maxActiveSlots; ++i)
		{	// set the rest to -1, no active slot
			pBag->eaiActiveSlots[i] = -1;
		}

		// for legacy, if we only have 1 active slot, 
		// set the first slot as the active 0 slot
		if (pBagDef->maxActiveSlots == 1 && !pBagDef->eaiDefaultActiveSlots)
		{
			pBag->eaiActiveSlots[0] = 0;
		}
	}

	eaIndexedAdd(&pEnt->pInventoryV2->ppInventoryBags, pBag);

	if(pBagDef->ppSlotIDs)
	{
		int i;
		
		for(i=0;i<eaSize(&pBagDef->ppSlotIDs);i++)
		{
			inv_trh_AddSlotToBagWithDef(ATR_PASS_ARGS,pBag,GET_REF(pBagDef->ppSlotIDs[i]->hSlot));
		}
	}

	return pBag;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
NOCONST(InventoryBag)* inv_ent_trh_RemoveBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID)
{
	int idx;

	if (ISNULL(pEnt))
		return false;

	idx = eaIndexedFindUsingInt(&pEnt->pInventoryV2->ppInventoryBags,BagID);

	if (  idx != -1 )
	{
		NOCONST(InventoryBag)*pBag = eaRemove(&pEnt->pInventoryV2->ppInventoryBags, idx);
		return pBag;
	}

	return NULL;
}
//routines to add/remove bags
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to add items to a bag

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pplayer.Playertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pDestBag, ".*")
	ATR_LOCKS(pItem, ".*");
int inv_bag_trh_AddItemFast(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int SlotIdx, ATH_ARG NOCONST(Item)* pItem, int count, ItemAddFlags eFlags, U64** peaItemIDs, const ItemChangeReason *pReason)
{
	int DestSlotIdx = -1;
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	NOCONST(InventorySlot)* pSlot = NULL;
	InventoryList tmpInventoryList = {0};
	int MatchCount = 0;
	bool ItemMatchesDestSlot = false;
	int StackLimit = 1;
	char* pchEntDebugName = NONNULL(pEnt) ? pEnt->debugName : "(null)";

	if(ISNULL(pDestBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot add item to bag [(null)] on entity [%s]: Bag is NULL",
			pchEntDebugName);
		goto inv_AddItem_exit;
	}
	if (!count)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot add item to bag [%s] on entity [%s]: Count is 0",
			StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), pchEntDebugName);
		goto inv_AddItem_exit;
	}

	// If this is not a critter and this is trying to add a critter item to a NoCopy bag, then fail
	if (NONNULL(pItem) && NONNULL(pEnt) && pEnt->myEntityType != GLOBALTYPE_ENTITYCRITTER &&
		item_GetIDTypeFromID(pItem->id) == kItemIDType_Critter && (invbag_trh_flags(pDestBag) & InvBagFlag_NoCopy))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot add critter type items to NoCopy bag %s on entity %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), pchEntDebugName);
		goto inv_AddItem_exit;
	}

	// Special restriction for Injuries and bags flagged as 'LockToRestrictBags'
	if (NONNULL(pItemDef) && (pItemDef->eType == kItemType_Injury || (pItemDef->flags & kItemDefFlag_LockToRestrictBags)))
	{
		bool bRestricted = true;

		if (pDestBag->BagID == InvBagIDs_Loot)
		{
			bRestricted = false;
		}
		else if (pItemDef->eType == kItemType_Injury && pDestBag->BagID == InvBagIDs_Injuries)
		{
			bRestricted = false;
		}
		else if (eaiFind(&pItemDef->peRestrictBagIDs, pDestBag->BagID) >= 0)
		{
			bRestricted = false;
		}

		if (bRestricted)
		{
			TRANSACTION_APPEND_LOG_FAILURE("Cannot add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on entity [%s]: Injury item not allowed in this bag",				
				pItemDef->pchName, pItem->id, count,
				StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), DestSlotIdx,
				pchEntDebugName);
			goto inv_AddItem_exit;
		}
	}

	if(NONNULL(pItemDef) && pItemDef->eType == kItemType_Numeric && pDestBag->BagID != InvBagIDs_Numeric)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on entity [%s]: Numeric item not allowed in this bag",				
			pItemDef->pchName, pItem->id, count,
			StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), DestSlotIdx,
			pchEntDebugName);
		goto inv_AddItem_exit;
	}

	//find matches just in case we need them
	if ( NONNULL(pItemDef) )
	{
		MatchCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pDestBag, pItemDef->pchName, &tmpInventoryList );
		ItemMatchesDestSlot = inv_trh_MatchingItemInSlot(ATR_PASS_ARGS, pDestBag, SlotIdx, pItemDef->pchName);
		StackLimit = pItemDef->iStackLimit;
	}

	//see if this is a unique item that this entity already has in inventory
	if ( NONNULL(pItemDef) &&
		 !(eFlags & ItemAdd_IgnoreUnique) &&
		 (pItemDef->flags & kItemDefFlag_Unique) && 
		 inv_ent_trh_HasUniqueItem(ATR_PASS_ARGS, pEnt, eaPets, pItemDef->pchName) )
	{
		const char* pchErrorMsgKey = "InventoryUI.DuplicateUniqueItem";
		DestSlotIdx = -2;	//error out transaction
#ifdef GAMESERVER
		if (GET_REF(pItemDef->hPetDef))
		{
			pchErrorMsgKey = "InventoryUI.DuplicateUniquePetItem";
		}
		QueueRemoteCommand_notify_RemoteSendItemNotification(ATR_RESULT_FAIL, 0, 0, pchErrorMsgKey, pItemDef->pchName, kNotifyType_DuplicateUniqueItem);
#endif

		TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to Ent [%s]: Item is unique and a copy of it already exists on the target",
			pItemDef->pchName,pItem->id,count,
			pchEntDebugName);
		goto inv_AddItem_exit;
	}

	//get the dest slot ptr in case we need it
	if ( SlotIdx >= 0 ) 
		pSlot=inv_trh_GetSlotPtr(ATR_PASS_ARGS, pDestBag, SlotIdx);

	//check if the item is being put into a specific slot that already has a matching item in it 
	if( NONNULL(pSlot) && ItemMatchesDestSlot )
	{
		if( StackLimit <= 0 )
		{
			//no stack limit
			pSlot->pItem->count += count;
			DestSlotIdx = SlotIdx;
		}
		else
		{
			if( (pSlot->pItem->count + count) <= StackLimit )
			{
				//just bump the item count
				pSlot->pItem->count += count;
				DestSlotIdx = SlotIdx;
			}
			else
			{
				TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on Ent [%s]: not enough room in specified bag/slot",
					pItemDef->pchName,pItem->id,count,
					StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), SlotIdx,
					pchEntDebugName);
			}
		}
		//done
		goto inv_AddItem_exit;
	}

	// Run init/fixup on the item
	inv_trh_InitAndFixupItem(ATR_PASS_ARGS, pEnt, pItem, true, pReason);
	
	//check for case of specific slot not specified, matching item(s) exist
	if ( SlotIdx<0 && MatchCount>0 )
	{
		int ii;

		for(ii=0; ii<MatchCount; ii++)
		{
			InventoryListEntry* pListEntry = &tmpInventoryList.Entry[ii];
			NOCONST(InventoryBag)* pBag = CONTAINER_NOCONST(InventoryBag, pListEntry->pBag);
			NOCONST(InventorySlot)* pMatchSlot = CONTAINER_NOCONST(InventorySlot, pListEntry->pSlot);

			if (ISNULL(pListEntry)) 
				continue;
			if (ISNULL(pBag)) 
				continue;
			if (ISNULL(pMatchSlot)) 
				continue;

			// Check to make sure item is defined in slot (matchcount can be > 0 for name-indexed bags even if slot is empty)
			if(!pMatchSlot->pItem)
			{
				NOCONST(Item)* pTmpItem = StructCloneNoConst(parse_Item, pItem);
				pMatchSlot->pItem = pTmpItem;
				if (peaItemIDs)
					ea64PushUnique(peaItemIDs, pTmpItem->id);
			}
			else
			{
				// Add item
				if ( StackLimit <= 0 )
				{
					//no stack limit
					pMatchSlot->pItem->count += count;
					DestSlotIdx = eaIndexedFindUsingString(&pBag->ppIndexedInventorySlots, pListEntry->pSlot->pchName); //success

					goto inv_AddItem_exit;
				}
				else
				{
					//fit as many as possible into matching items
					int AddCount = (pMatchSlot->pItem->count+count <= pItemDef->iStackLimit ? count : pItemDef->iStackLimit-pMatchSlot->pItem->count);

					pMatchSlot->pItem->count += AddCount;
					count -= AddCount;

					if ( count <= 0 )
					{
						DestSlotIdx = eaIndexedFindUsingString(&pBag->ppIndexedInventorySlots, pListEntry->pSlot->pchName);
						goto inv_AddItem_exit;
					}
				}
			}
		}


		//may exit this loop with item count > 0 if matching slots could not hold all items to add
	}

	// If a valid slot was specified, add the item to the new slot
	if (SlotIdx >= 0)
	{
		DestSlotIdx = inv_trh_GetSlot(ATR_PASS_ARGS, pEnt, pDestBag, SlotIdx, pItemDef ? pItemDef->pchName : NULL, pItemDef && IS_HANDLE_ACTIVE(pItemDef->hSlotID) ? GET_REF(pItemDef->hSlotID) : NULL);
		pSlot = eaGet(&pDestBag->ppIndexedInventorySlots,DestSlotIdx);

		if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
		{
			TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on Ent [%s]: destination slot already contains an item (%s).",
				pItemDef->pchName,pItem->id,count,
				StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), SlotIdx,
				pchEntDebugName,
				REF_STRING_FROM_HANDLE(pSlot->pItem->hItem));
			DestSlotIdx = -1;
			goto inv_AddItem_exit;
		}
		else if (NONNULL(pSlot))
		{
			pSlot->pItem = pItem;
			if (peaItemIDs)
				ea64PushUnique(peaItemIDs, pItem->id);
			pItem = NULL;
		}
		else
		{
			DestSlotIdx = -1;
		}
	}
	else
	{
		//fall through here when no slot was specified, and no matching items or all matching item slots full
		while (count>0)
		{
			DestSlotIdx = inv_trh_GetSlot(ATR_PASS_ARGS, pEnt, pDestBag, SlotIdx, pItemDef ? pItemDef->pchName : NULL, pItemDef && IS_HANDLE_ACTIVE(pItemDef->hSlotID) ? GET_REF(pItemDef->hSlotID) : NULL);

			if (DestSlotIdx >= 0)
			{
				int tmpcount = StackLimit>0 && count>StackLimit ? StackLimit : count;
				NOCONST(Item)* pTmpItem;

				pSlot = eaGet(&pDestBag->ppIndexedInventorySlots,DestSlotIdx);

				if ( ISNULL(pSlot) )
				{
					TRANSACTION_APPEND_LOG_FAILURE("slot error");
					DestSlotIdx = -1;
					goto inv_AddItem_exit;
				}

				if (tmpcount >= count)
				{
					pTmpItem = pItem;
					pItem = NULL;
				}
				else
				{
					pTmpItem = StructCloneNoConst(parse_Item, pItem);
					if(NONNULL(pTmpItem))
					{
						pTmpItem->id = 0;	// create a new ID for this item
						item_trh_SetItemID(pEnt, pTmpItem);
					}
				}

				pSlot->pItem = pTmpItem;
				if (peaItemIDs)
					ea64PushUnique(peaItemIDs, pTmpItem->id);

				pSlot->pItem->count = tmpcount;

				count -= tmpcount;
			}
			else
			{
				break;
			}
		}
	}
	if (DestSlotIdx < 0)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on entity [%s]: not enough room in bag",
			SAFE_MEMBER(pItemDef, pchName), SAFE_MEMBER(pItem, id), count,
			StaticDefineIntRevLookup(InvBagIDsEnum, pDestBag->BagID), DestSlotIdx,
			pchEntDebugName);

		DestSlotIdx = -1;
	}

	//single exit point, need to do cleanup on exit
inv_AddItem_exit:

	if ( NONNULL(pItem) )
	{
		//destroy the passed in item
		StructDestroyNoConst(parse_Item, pItem);
	}
	
	return DestSlotIdx;
}

//return the max possible stack count that can be dropped given an item and a bag to put it in
//Note: this function finds the total free stack space in the bag, and will not return more than pItemDef->iStackLimit.
//it is important to note that pItemDef->iStackLimit is *NOT* the item's current stack size
AUTO_TRANS_HELPER
ATR_LOCKS(pDestBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Inv_Def, .Bagid");
int inv_bag_trh_GetMaxDropCountWithDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ItemDef* pItemDef)
{
	int ii;
	int iFreeStackCount = 0;

	if (ISNULL(pDestBag) || ISNULL(pItemDef))
		return 0;

	if ( inv_bag_trh_BagFull(ATR_PASS_ARGS, pEnt, pDestBag, GET_REF(pItemDef->hSlotID)) )
	{
		if ( pItemDef->iStackLimit > 1 )
		{
			for(ii=0;ii<invbag_trh_maxslots(pEnt, pDestBag);ii++)
			{
				if ( inv_trh_MatchingItemInSlot( ATR_PASS_ARGS, pDestBag, ii, pItemDef->pchName ) )
				{
					int iSlotCount = inv_bag_trh_GetSlotItemCount( ATR_PASS_ARGS, pDestBag, ii );

					iFreeStackCount += ( pItemDef->iStackLimit - iSlotCount );
				}
			}
		}
	}
	else
	{
		iFreeStackCount = pItemDef->iStackLimit;
	}

	return ( iFreeStackCount > pItemDef->iStackLimit ) ? pItemDef->iStackLimit : iFreeStackCount;
}

//return the max possible stack count that can be dropped given an item and a bag to put it in
//Note: this function finds the total free stack space in the bag, and will not return more than pItemDef->iStackLimit.
//it is important to note that pItemDef->iStackLimit is *NOT* the item's current stack size
AUTO_TRANS_HELPER
ATR_LOCKS(pDestBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Inv_Def, .Bagid")
ATR_LOCKS(pItem, ".Hitem");
int inv_bag_trh_GetMaxDropCount(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pItemDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;
	return inv_bag_trh_GetMaxDropCountWithDef(ATR_PASS_ARGS, pEnt, pDestBag, pItemDef);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pDestBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Inv_Def, .Bagid");
bool inv_bag_trh_CanItemDefFitInBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ItemDef* pItemDef, int iCount)
{
	int i, iFreeStackCount = 0;
	bool bBagFull;

	if (ISNULL(pDestBag) || ISNULL(pItemDef)) 
		return false;

	bBagFull = inv_bag_trh_BagFull(ATR_PASS_ARGS, pEnt, pDestBag, GET_REF(pItemDef->hSlotID));
	if (bBagFull || iCount > pItemDef->iStackLimit)
	{
		if (!bBagFull || pItemDef->iStackLimit > 1)
		{
			for (i = 0; i < invbag_trh_maxslots(pEnt, pDestBag); i++)
			{
				NOCONST(InventorySlot)* pSlot = inv_trh_GetSlotPtr(ATR_PASS_ARGS, pDestBag, i);
				if (ISNULL(pSlot))
				{
					iFreeStackCount += pItemDef->iStackLimit;
					if (iFreeStackCount >= iCount) 
					{
						return true;
					}
				}
				else if (!pSlot->pItem ||
					inv_trh_MatchingItemInSlot(ATR_PASS_ARGS, pDestBag, i, pItemDef->pchName))
				{
					int iSlotCount = inv_bag_trh_GetSlotItemCount(ATR_PASS_ARGS, pDestBag, i);
					
					iFreeStackCount += (pItemDef->iStackLimit - iSlotCount);

					if (iFreeStackCount >= iCount) 
					{
						return true;
					}
				}
			}
		}
		return false;
	}
	return true;
}

//can a designated "count" from a stackable item fit in a destination bag?
AUTO_TRANS_HELPER
ATR_LOCKS(pDestBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Inv_Def, .Bagid")
ATR_LOCKS(pItem, ".Hitem");
bool inv_bag_trh_CanItemFitInBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pDestBag, ATH_ARG NOCONST(Item)* pItem, int count)
{
	ItemDef* pItemDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;
	return inv_bag_trh_CanItemDefFitInBag(ATR_PASS_ARGS, pEnt, pDestBag, pItemDef, count);
}


// This helper does not destroy the passed-in Item
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppallowedcritterpets, .Psaved.Ppownedcontainers, .pInventoryV2.peaowneduniqueitems, .pInventoryV2.Pplitebags[], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.peaowneduniqueitems")
ATR_LOCKS(pDestBag, ".*")
ATR_LOCKS(pAddItem, ".*");
int inv_bag_trh_AddItemMain(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int iSlotIdx, ATH_ARG NOCONST(Item) *pAddItem, int iCount, ItemAddFlags eFlags, U64** peaItemIDs, const ItemChangeReason *pReason)
{
	NOCONST(Item) *pItemCopy = StructCloneNoConst(parse_Item, pAddItem);
	int iDestSlotIdx = -1;

	if (NONNULL(pDestBag))
	{
		// this always destroys the passed-in item
		iDestSlotIdx = inv_bag_trh_AddItemFast(ATR_PASS_ARGS, pEnt, eaPets, pDestBag, iSlotIdx, pItemCopy, iCount, eFlags, peaItemIDs, pReason);

		if (iDestSlotIdx >= 0)
		{
			NOCONST(Item) *pItem = inv_trh_GetItemPtrFromSlot(ATR_PASS_ARGS, pDestBag, iDestSlotIdx);
			if (NONNULL(pItem))
				inv_trh_ItemAddedCallbacks(ATR_PASS_ARGS, pEnt, !!(eFlags & ItemAdd_Silent), pItem, pDestBag, iDestSlotIdx, iCount, !!(eFlags & ItemAdd_ForceBind), pReason);
		}
	}
	else
	{
		// destroy the item copy
		TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on Ent [%s]: destination bag is NULL.",
			pItemCopy && pItemCopy->pchDisplayName?pItemCopy->pchDisplayName:"",
			pItemCopy?pItemCopy->id:0,iCount,
			"(null)", iDestSlotIdx,
			NONNULL(pEnt) ? pEnt->debugName : "(null)");
		StructDestroyNoConst(parse_Item, pItemCopy);
	}

	return iDestSlotIdx;
}

// This helper always destroys the passed-in Item
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppallowedcritterpets, .Psaved.Ppownedcontainers, .pInventoryV2.peaowneduniqueitems, .pInventoryV2.Ppinventorybags[], .pInventoryV2.Pplitebags[], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.peaowneduniqueitems")
ATR_LOCKS(pDestBag, ".*")
ATR_LOCKS(pAddItem, ".*");
bool inv_bag_trh_AddItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(InventoryBag)* pDestBag, int iDestSlotIdx, ATH_ARG NOCONST(Item)* pAddItem, ItemAddFlags eFlags, U64** peaItemIDs, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	ItemDef *pAddItemDef = NONNULL(pAddItem) ? GET_REF(pAddItem->hItem) : NULL;
	char* pchEntDebugName = "(null)"; // Debug name of pEnt. Used because the logging in this function exceeds StructParser's limit of recursing function calls
	int i, j;
	U64 itemID = 0;
	bool bSuccess = true;
	int iCount = pAddItem->count;

	if (ISNULL(pDestBag) || !pAddItemDef)
	{
		StructDestroyNoConst(parse_Item, pAddItem);
		TRANSACTION_APPEND_LOG_FAILURE("Item Add Failed: destination bag or itemDef is NULL");
		return false;
	}

	if (NONNULL(pEnt))
	{
		pchEntDebugName = pEnt->debugName;
	}

	itemID = pAddItem->id;

	// special handling for when items are added to standard player inventory without specifying a slot
	if (invbag_trh_bagid(pDestBag) == InvBagIDs_Inventory && iDestSlotIdx == -1 && !(eFlags & ItemAdd_OverrideStackRules))
	{
		NOCONST(InventoryBag) **peaInventoryBags = NULL;
		NOCONST(InventoryBag) **peaBagsWithItem = NULL;
		NOCONST(InventoryBag) *pPlayerBagBag = (NONNULL(pEnt) && NONNULL(pEnt->pInventoryV2)) ? eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 9 /* Literal InvBagIDs_PlayerBags */) : NULL;
		NOCONST(InventoryBag) *pCurrBag = NULL;
	
		// aggregate earray of all bags to consider
		eaPush(&peaInventoryBags, pDestBag);
		if (NONNULL(pPlayerBagBag))
		{
			for (i = 0; i < invbag_trh_maxslots(pEnt, pPlayerBagBag); i++)
			{
				pCurrBag = inv_trh_PlayerBagFromSlotIdx(ATR_PASS_ARGS, pEnt, i);

				if (NONNULL(pCurrBag) && GamePermissions_trh_CanAccessBag(pEnt, pCurrBag->BagID, pExtract))
					eaPush(&peaInventoryBags, pCurrBag);
			}
		}

		// apply stacking rules here (per Cormac's design; I wipe my hands of this one - JDJ):
		// (we attempt to add items according to the following situations starting with the primary
		// inventory bag and going through the player bags)

		// 1) add the item to bags that have non-full stacks of the item
		for (i = 0; i < eaSize(&peaInventoryBags) && iCount > 0; i++)
		{
			pCurrBag = peaInventoryBags[i];
			for (j = 0; j < invbag_trh_maxslots(pEnt, pCurrBag) && iCount > 0; j++)
			{
				if (inv_trh_MatchingItemInSlot(ATR_PASS_ARGS, pCurrBag, j, pAddItemDef->pchName))
				{
					int iSlotCount = inv_bag_trh_GetSlotItemCount(ATR_PASS_ARGS, pCurrBag, j);
					int iFreeCount = pAddItemDef->iStackLimit - iSlotCount;
					int iNumToAdd = MIN(iCount, iFreeCount);

					eaPushUnique(&peaBagsWithItem, pCurrBag);

					if (iNumToAdd > 0)	
					{
						if (inv_bag_trh_AddItemMain(ATR_PASS_ARGS, pEnt, eaPets, pCurrBag, j, pAddItem, iNumToAdd, eFlags, peaItemIDs, pReason) >= 0)
							iCount -= iNumToAdd;
						else
						{
							TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to existing stack in bag:slot [%s:%d] on Ent [%s].",
								pAddItemDef->pchName, itemID, iNumToAdd,
								StaticDefineIntRevLookup(InvBagIDsEnum,pCurrBag->BagID), j,
								pchEntDebugName);
							eaDestroy(&peaInventoryBags);
							eaDestroy(&peaBagsWithItem);
							StructDestroyNoConst(parse_Item, pAddItem);
							return false;
						}
					}
				}
			}
		}

		// 2) once all stacks are full (or if there are no non-full stacks), add the item to empty slots on bags with full stacks of the item
		for (i = 0; i < eaSize(&peaBagsWithItem) && iCount > 0; i++)
		{
			int iAvailableSpace, iNumToAdd;

			pCurrBag = peaBagsWithItem[i];
			iAvailableSpace = inv_bag_trh_AvailableSlots(ATR_PASS_ARGS, pEnt, pCurrBag) * pAddItemDef->iStackLimit;
			iNumToAdd = MIN(iCount, iAvailableSpace);

			if (iNumToAdd > 0)
			{
				if (inv_bag_trh_AddItemMain(ATR_PASS_ARGS, pEnt, eaPets, pCurrBag, -1, pAddItem, iNumToAdd, eFlags, peaItemIDs, pReason) >= 0)
					iCount -= iNumToAdd;
				else
				{
					TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to empty slot in bag with existing items: bag:slot [%s:-1] on Ent [%s].",
						pAddItemDef->pchName, itemID, iNumToAdd,
						StaticDefineIntRevLookup(InvBagIDsEnum,pCurrBag->BagID),
						pchEntDebugName);
					eaDestroy(&peaInventoryBags);
					eaDestroy(&peaBagsWithItem);
					StructDestroyNoConst(parse_Item, pAddItem);
					return false;
				}
			}
		}

		// 3) once all bags with the item are full, add the item to the first available slots among the remaining of the bags
		for (i = 0; i < eaSize(&peaInventoryBags) && iCount > 0; i++)
		{
			int iAvailableSpace, iNumToAdd;

			pCurrBag = peaInventoryBags[i];

			// skip the bag if it was already considered
			if (eaFind(&peaBagsWithItem, pCurrBag) >= 0)
				continue;

			iAvailableSpace = inv_bag_trh_AvailableSlots(ATR_PASS_ARGS, pEnt, pCurrBag) * pAddItemDef->iStackLimit;
			iNumToAdd = MIN(iCount, iAvailableSpace);

			if (iNumToAdd > 0)
			{
				if (inv_bag_trh_AddItemMain(ATR_PASS_ARGS, pEnt, eaPets, pCurrBag, -1, pAddItem, iNumToAdd, eFlags, peaItemIDs, pReason) >= 0)
					iCount -= iNumToAdd;
				else
				{
					TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to bag:slot [%s:%d] on Ent [%s].",
						pAddItemDef->pchName, itemID, iNumToAdd,
						StaticDefineIntRevLookup(InvBagIDsEnum,pCurrBag->BagID), -1,
						pchEntDebugName);
					eaDestroy(&peaInventoryBags);
					eaDestroy(&peaBagsWithItem);
					StructDestroyNoConst(parse_Item, pAddItem);
					return false;
				}
			}
		}

		eaDestroy(&peaInventoryBags);
		eaDestroy(&peaBagsWithItem);

		// 4) if some items still remain to be added, then return false, as they do not fit in
		// the player's inventory
		if (iCount > 0)
		{
			bSuccess = false;
		}
	}
	else if(invbag_trh_flags(pDestBag) & InvBagFlag_EquipBag)
	{
		//special handling for upgrade items that are marked as primary or secondary when no slot is specified
		if (iDestSlotIdx < 0 && NONNULL(pEnt))
		{
			if (item_IsPrimaryEquip(pAddItemDef))
			{
				iDestSlotIdx = 0;
			}
			else if (item_IsSecondaryEquip(pAddItemDef))
			{
				int s = invbag_trh_maxslots(pEnt, pDestBag);
				for (i = 1; i < s; i++)
				{
					if (!inv_bag_trh_GetSlotItemCount(ATR_PASS_ARGS, pDestBag, i))
					{
					 	iDestSlotIdx = i;
						break;
					}
				}
				if (iDestSlotIdx < 0)
				{
					bSuccess = false;
				}
			}
		}
		if (bSuccess && inv_bag_trh_AddItemMain(ATR_PASS_ARGS, pEnt, eaPets, pDestBag, iDestSlotIdx, pAddItem, iCount, eFlags, peaItemIDs, pReason) < 0)
		{
			bSuccess = false;
		}
	}
	else
	{
		bSuccess = inv_bag_trh_AddItemMain(ATR_PASS_ARGS, pEnt, eaPets, pDestBag, iDestSlotIdx, pAddItem, iCount, eFlags, peaItemIDs, pReason) >= 0;
	}

	if (!bSuccess && ((eFlags & ItemAdd_UseOverflow) || (GET_REF(pAddItemDef->hPetDef) && (pAddItemDef->flags & kItemDefFlag_EquipOnPickup))))
	{
		// Try adding the remaining items to the overflow bag
		NOCONST(InventoryBag) *pOverflowBag = NULL;
		
		if (NONNULL(pEnt) && NONNULL(pEnt->pInventoryV2))
		{
			pOverflowBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 33 /* Literal InvBagIDs_Overflow */);
		}
		if (NONNULL(pOverflowBag))
		{
			bSuccess = inv_bag_trh_AddItemMain(ATR_PASS_ARGS, pEnt, eaPets, pOverflowBag, -1, pAddItem, iCount, eFlags, peaItemIDs, pReason) >= 0;
		}
	}

	if (bSuccess)
	{
		TRANSACTION_APPEND_LOG_SUCCESS("Item [%s<%"FORM_LL"u>]x%i successfully added to Ent [%s].",
			pAddItemDef->pchName, itemID, iCount, pchEntDebugName);
	}
	else
	{
		TRANSACTION_APPEND_LOG_FAILURE("Unable to add item [%s<%"FORM_LL"u>]x%i to Ent [%s].",
			pAddItemDef->pchName, itemID, iCount, pchEntDebugName);
	}

	// destroy the passed in item
	StructDestroyNoConst(parse_Item, pAddItem);

	return bSuccess;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pDestBag, ".ppIndexedLiteSlots[]");
NOCONST(InventorySlotLite)* inv_lite_trh_GetOrCreateLiteSlot(ATH_ARG NOCONST(InventoryBagLite)* pDestBag, const char* pchName)
{
	NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pDestBag->ppIndexedLiteSlots, pchName);
	if (!pSlot)
	{
		ItemDef* pDef = RefSystem_ReferentFromString(g_hItemDict, pchName);
		pSlot = StructCreateNoConst(parse_InventorySlotLite);
		pSlot->pchName = allocAddString(pchName);
		pSlot->count = 0;
		SET_HANDLE_FROM_REFERENT(g_hItemDict, pDef, pSlot->hItemDef);

		eaIndexedPushUsingStringIfPossible(&pDestBag->ppIndexedLiteSlots, pchName, pSlot);
	}
	return pSlot;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pDestBag, ".ppIndexedLiteSlots[]");
bool inv_lite_trh_AddItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pDestBag, const char* pchDefName, int iCount, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (ISNULL(pDestBag) || !pchDefName)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Item Add Failed: destination bag or itemDef is NULL");
		return false;
	}
	else
	{
		NOCONST(InventorySlotLite)* pSlot = inv_lite_trh_GetOrCreateLiteSlot(pDestBag, pchDefName);
		pSlot->count = max(0, pSlot->count + iCount);

		return true;
	}
}

// This helper will destroy the passed-in Item
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
bool inv_ent_trh_AddItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int BagID, int SlotIdx, NOCONST(Item)* pItem, ItemAddFlags eFlags, U64** peaItemIDs, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt ,BagID, pExtract);
	NOCONST(InventoryBagLite)* pLiteBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt ,BagID, pExtract);
	int DestSlotIdx = -1;
	bool XPitem = false;

	if ( ISNULL(pEnt))
	{
		StructDestroyNoConst(parse_Item, pItem);
		return false;
	}

	if ( ISNULL(pEnt->pInventoryV2))
	{
		StructDestroyNoConst(parse_Item, pItem);
		return false;
	}

	if (pBag)
	{
		return inv_bag_trh_AddItem(ATR_PASS_ARGS, pEnt, eaPets, pBag, SlotIdx, pItem, eFlags, peaItemIDs, pReason, pExtract);
	}
	else if (pLiteBag)
	{
		ItemDef* pDef = GET_REF(pItem->hItem);
		if (pDef->eType == kItemType_Numeric)
			return inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, eFlags & ItemAdd_Silent, pLiteBag, pDef->pchName, pItem->count, pItem->numeric_op, pReason);
		else if (inv_lite_trh_AddItem(ATR_PASS_ARGS, pLiteBag, pDef->pchName, pItem->count, eFlags, pReason, pExtract))
		{
#ifdef GAMESERVER
			{
				ItemTransCBData *pData = StructCreate(parse_ItemTransCBData);
				Message* pMsg;

				pMsg = GET_REF(pDef->displayNameMsg.hMessage);
				if (pMsg)
				{
					eaPush(&pData->ppchNamesUntranslated, pMsg->pcMessageKey);
				}
				pData->bTranslateName = true;
				pData->pchItemDefName = StructAllocString(pDef->pchName);
				pData->iCount = pItem->count;
				pData->kQuality = pDef->Quality; 
				pData->bLite = true;

				// store the destination bag
				pData->eBagID = pLiteBag->BagID;

				QueueRemoteCommand_eventsend_RemoteRecordBagGetsItem(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pLiteBag->BagID);
				if (!(eFlags & ItemAdd_Silent))
				{
					QueueRemoteCommand_RemoteAddItemCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
					QueueRemoteCommand_RemoteAddItemEventCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
				}

				// destroy data to prevent memory leak
				eaClear(&pData->ppchNamesUntranslated);
				StructDestroy(parse_ItemTransCBData, pData);
			}
#endif
			TRANSACTION_APPEND_LOG_SUCCESS("Item [%s]x%i successfully added to Ent [%s].",
				pDef->pchName, pItem->count, pEnt->debugName);
			return true;
		}
	}

	TRANSACTION_APPEND_LOG_FAILURE( "Unable to add item [%s<%"FORM_LL"u>]x%i: No Bag %s on Ent %s", 
		pItem && pItem->pchDisplayName?pItem->pchDisplayName:"",pItem?pItem->id:0,pItem->count,
		StaticDefineIntRevLookup(InvBagIDsEnum,BagID), pEnt->debugName );
	StructDestroyNoConst(parse_Item, pItem);
	return false;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppbuilds, .Pplayer.Pugckillcreditlimit, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
bool inv_ent_trh_AddItemFromDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int BagID, int SlotIdx, const char* ItemDefName, int Count, int overrideLevel, const char *pcRank, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item)* pItem = inv_ItemInstanceFromDefName( ItemDefName,entity_trh_GetSavedExpLevelLimited(pEnt), overrideLevel, pcRank, NONNULL(pEnt) ? GET_REF(pEnt->hAllegiance) : NULL, NONNULL(pEnt) ? GET_REF(pEnt->hSubAllegiance) : NULL, false, NULL);
	bool retcode = false;

	if ( ISNULL(pItem) )
	{
		TRANSACTION_APPEND_LOG_FAILURE("failed to create item from def %s", ItemDefName );
		return false;
	}

	pItem->count = Count;

	return inv_ent_trh_AddItem(ATR_PASS_ARGS, pEnt, eaPets, BagID, SlotIdx, pItem, eFlags, NULL, pReason, pExtract);
}


//routines to add items to a bag
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to remove items from a bag

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(pBag, ".Bagid, .Ppindexedinventoryslots, .Inv_Def");
NOCONST(Item)* inv_bag_trh_RemoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx, int count, const ItemChangeReason *pReason)
{
	int NumSlots = 0;
	NOCONST(InventorySlot)* pSlot = NULL;
	NOCONST(Item)* pItem;
	const char *ItemDefName = "";
	char* pchEntDebugName = NONNULL(pEnt) ? pEnt->debugName : "(null)";
	int del_count = 0;
	int iBagID = 0;

	if(ISNULL(pBag)) {
		TRANSACTION_APPEND_LOG_FAILURE("Failed to remove item from Bag:Slot [(null):%d] on Ent [%s]: specified bag is NULL.", SlotIdx, pchEntDebugName);
		return NULL;
	}

	iBagID = pBag->BagID;
	NumSlots = eaSize(&pBag->ppIndexedInventorySlots);

	//make sure slot is in range
	if ( (SlotIdx < 0) || ( SlotIdx+1 > NumSlots) ) {
		TRANSACTION_APPEND_LOG_FAILURE("Failed to remove item from Bag:Slot [%s:%d] on Ent [%s]: slot is out of range", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iBagID),SlotIdx, 
			pchEntDebugName);
		return NULL;
	}

	pSlot = pBag->ppIndexedInventorySlots[SlotIdx];

	//make sure that this slot is an Item
	if (!pSlot->pItem)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Failed to remove item from Bag:Slot [%s:%d] on Ent [%s]: specified slot does not contain an item.", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iBagID),SlotIdx, 
			pchEntDebugName);
		return NULL;
	}

	//get the itemdef name
	if ( invbag_trh_flags(pBag) & InvBagFlag_NameIndexed )
	{
		ItemDefName = pSlot->pchName;
	}
	else
	{
		ItemDefName = inv_trh_GetItemDefNameFromItem(ATR_PASS_ARGS, pSlot->pItem);
	}

	if ( (invbag_trh_flags(pBag) & InvBagFlag_PlayerBagIndex) &&
		 inv_trh_PlayerBagFail(ATR_PASS_ARGS, pEnt, pBag, SlotIdx) )
	{
		TRANSACTION_APPEND_LOG_FAILURE("Failed to remove item from Bag:Slot [%s:%d] on Ent [%s]: can not remove non-empty player bag", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iBagID),SlotIdx, 
			pchEntDebugName);
		return NULL;
	}


	if ( count >= 0 )
	{
		if ( pSlot->pItem->count >= count )
		{
			pSlot->pItem->count -= count;
			del_count += count;
			
			if(pSlot->pItem->count > 0)
			{
				// Charges used on Powers for stack left in the slot is reset to 0
				int PowerIdx, NumPowers = eaSize(&pSlot->pItem->ppPowers);
				for(PowerIdx=0; PowerIdx<NumPowers; PowerIdx++)
				{
					power_SetChargesUsedHelper(pSlot->pItem->ppPowers[PowerIdx], 0);
				}
			}
		}
		else
		{
			TRANSACTION_APPEND_LOG_FAILURE("Failed to remove item from Bag:Slot [%s:%d] on Ent [%s]: Not enough items to satisfy remove count (%d).", 
				StaticDefineIntRevLookup(InvBagIDsEnum, iBagID),SlotIdx, 
				pchEntDebugName, count);
			return NULL;
		}
	}
	else
	{
		//special case for remove all
		del_count += pSlot->pItem->count;
		pSlot->pItem->count = 0;
	}

	if ( pSlot->pItem->count > 0 )
	{
		//create a new item instance to pass back to the caller
		pItem = inv_ItemInstanceFromDefName( ItemDefName, entity_trh_GetSavedExpLevelLimited(pEnt), 0, 0, NONNULL(pEnt) ? GET_REF(pEnt->hAllegiance) : NULL, NONNULL(pEnt) ? GET_REF(pEnt->hSubAllegiance) : NULL, false, NULL);
		pItem->count = del_count;
	}
	else
	{
		pItem = pSlot->pItem;
		pItem->count = del_count;

		pSlot->pItem = NULL;

		// Update the Entity's Build
		if(invbag_trh_flags(pBag) & (InvBagFlag_EquipBag | InvBagFlag_DeviceBag | InvBagFlag_ActiveWeaponBag))
		{
			inv_ent_trh_BuildCurrentSetItem(ATR_PASS_ARGS, pEnt, bSilent, invbag_trh_bagid(pBag), SlotIdx, 0);
		}

		inv_trh_CollapseBag(ATR_PASS_ARGS, pBag);
	}

	if(NONNULL(pItem)) {
		TRANSACTION_APPEND_LOG_SUCCESS("Successfully removed item[%s<%"FORM_LL"u>]x%i from Bag:Slot [%s:%d] on Ent [%s].",
			pItem->pchDisplayName?pItem->pchDisplayName:"",pItem->id,count,
			StaticDefineIntRevLookup(InvBagIDsEnum, iBagID),SlotIdx, 
			pchEntDebugName);
	}

	inv_trh_ItemRemovedCallbacks(ATR_PASS_ARGS, pEnt, bSilent, pItem, pBag, SlotIdx, del_count, pReason );

	return pItem;
}



AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
bool inv_ent_trh_RemoveItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 uiItemId, int count)
{
	InventoryList tmpInventoryList = {0};
	int ii;
	int NumBags;

	if ( ISNULL(pEnt)) {
		TRANSACTION_APPEND_LOG_FAILURE("Remove Item by ID [%"FORM_LL"u]x%i from Entity [(null)] failed: Entity is NULL.", uiItemId, count);
		return false;
	}

	if ( ISNULL(pEnt->pInventoryV2)) {
		TRANSACTION_APPEND_LOG_FAILURE("Remove Item by ID [%"FORM_LL"u]x%i from Entity [%s] failed: Entity's inventory is NULL.", 
			uiItemId, count, pEnt->debugName);
		return false;
	}

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	
	for (ii=0; ii<NumBags; ii++)
	{
		InvBagFlag eFlags = InvBagFlag_DefaultInventorySearch;
		if (NONNULL(pEnt->pInventoryV2->ppInventoryBags[ii]) && (invbag_trh_flags(pEnt->pInventoryV2->ppInventoryBags[ii]) & eFlags)) {
			inv_bag_trh_GetItemList(ATR_PASS_ARGS, pEnt->pInventoryV2->ppInventoryBags[ii], NULL, &tmpInventoryList);
		}
	}
	
	if ( tmpInventoryList.count > 0 )
	{
		for(ii=0; ii<tmpInventoryList.count; ii++)
		{
			InventoryListEntry* pListEntry = &tmpInventoryList.Entry[ii];
			NOCONST(InventoryBag)* pMatchBag = CONTAINER_NOCONST(InventoryBag, pListEntry->pBag);
			NOCONST(InventorySlot)* pSlot = CONTAINER_NOCONST(InventorySlot, pListEntry->pSlot);
			ItemDef *pItemDef = NULL;

			if (!pListEntry) continue;
			if (!pMatchBag) continue;
			if (!pSlot) continue;
			if (!pSlot->pItem)
			{
				continue;
			}

			//check to see if item has a matching mission reference
			if ( pSlot->pItem->id == uiItemId)
			{
				int iRemoved = min(pSlot->pItem->count,count);
				//inv_trh_ItemRemovedCallbacks(ATR_PASS_ARGS, pEnt, false, pSlot->pItem, pMatchBag, -1, iRemoved );

				assertmsgf((pMatchBag->BagID != InvBagIDs_Numeric), "Removing numeric item: %s! Numeric items should never actually be removed.  Possible duplicate item id?", pSlot->pchName);

				if(count > iRemoved)
				{
					// not enough items !
					TRANSACTION_APPEND_LOG_FAILURE("Remove Item by ID [%"FORM_LL"u]x%i from Entity [%s] failed: Not enough items of requested ID. %i items found.", 
						uiItemId, count, pEnt->debugName, iRemoved);
					return false;
				}

				pSlot->pItem->count -= iRemoved;

				TRANSACTION_APPEND_LOG_SUCCESS("Successfully removed Item [%s<%"FORM_LL"u>]x%i from Entity [%s].", 
					(pSlot->pItem && pSlot->pItem->pchDisplayName)?pSlot->pItem->pchDisplayName:"",
					uiItemId, count, pEnt->debugName);

				//destroy the item instance
				if(pSlot->pItem->count <= 0)
				{
					StructDestroyNoConst(parse_Item, pSlot->pItem);
					pSlot->pItem = NULL;
					inv_trh_CollapseBag(ATR_PASS_ARGS, pMatchBag);
				}

				return true;
				
			}
		}
	}

	TRANSACTION_APPEND_LOG_FAILURE("Remove Item by ID [%"FORM_LL"u]x%i from Entity [%s] failed: Unable to find an item by that ID.", 
		uiItemId, count, pEnt->debugName);

	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppbuilds, .pInventoryV2.Ppinventorybags, .Pplayer.Playertype, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, pInventoryV2.ppLiteBags[]");
NOCONST(Item)* inv_ent_trh_RemoveItemFromInventoryList(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, bool bSilent, const char *pcItemDefName, int iCount, InventoryList *pList, ItemChangeReason *pReason)
{
	int MatchingSlotCount = 0;
	int MatchingItemCount = 0;
	NOCONST(Item)* pItem = NULL;
	int i;
	int del_count = 0;
	bool bPlayerBag = false;

	if(ISNULL(pEnt))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Remove Item [%s]x%i from inventory list for [%s] non-existent entity.",
			pcItemDefName, iCount, "(null)");
		return NULL;
	}
	
	MatchingSlotCount = pList->count;
	
	for (i = 0; i < MatchingSlotCount; i++) {
		if (pList->Entry[i].pSlot->pItem)
			MatchingItemCount += pList->Entry[i].pSlot->pItem->count;
	}
	
	if ((iCount > 0) && ( MatchingItemCount < iCount ))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Remove Item [%s]x%i from Inventory list for Entity [%s] failed: Not enough items to satisfy remove count. %i items found.",
			pcItemDefName, iCount, pEnt->debugName, MatchingItemCount);
		return NULL;
	}
	
	if ( MatchingSlotCount > 0 )
	{
		//special case for remove all
		if (iCount == -1)
			iCount = MatchingItemCount;
		
		for(i = 0; i < MatchingSlotCount; i++) {
			InventoryListEntry* pListEntry = &pList->Entry[i];
			NOCONST(InventoryBag)* pMatchBag = CONTAINER_NOCONST(InventoryBag, pListEntry->pBag);
			NOCONST(InventorySlot)* pSlot = CONTAINER_NOCONST(InventorySlot, pListEntry->pSlot);

			if (ISNULL(pListEntry)) continue;
			if (ISNULL(pMatchBag)) continue;
			if (ISNULL(pSlot)) continue;
			if (ISNULL(pSlot->pItem)) continue;


			if ( (invbag_trh_flags(pMatchBag) & InvBagFlag_PlayerBagIndex) &&
				 inv_trh_PlayerBagFail(ATR_PASS_ARGS, pEnt, pMatchBag, atoi(pSlot->pchName) ) )
				continue;

			if ( invbag_trh_flags(pMatchBag) & InvBagFlag_PlayerBagIndex )
				bPlayerBag = true;

			{
				//remove as many as possible 
				int RemoveCount = (pSlot->pItem->count >= iCount ? iCount : pSlot->pItem->count);

				pSlot->pItem->count -= RemoveCount;	
				iCount -= RemoveCount;
				del_count += RemoveCount;

				//save a copy of the first removed item to pass back to caller
				if (ISNULL(pItem))
				{
					pItem = StructCloneNoConst(parse_Item, pSlot->pItem);
				}	

				if (pSlot->pItem->count <= 0)
				{
					//destroy the item instance
					StructDestroyNoConst(parse_Item, pSlot->pItem);
					pSlot->pItem = NULL;

					inv_trh_CollapseBag(ATR_PASS_ARGS, pMatchBag);
				}

				inv_trh_ItemRemovedCallbacks(ATR_PASS_ARGS, pEnt, bSilent, pItem, pMatchBag, -1, RemoveCount, pReason );

				if ( iCount <= 0 )
				{
					break;
				}
			}
		}

		// Update the Entity's Build
		inv_ent_trh_BuildCurrentFill(ATR_PASS_ARGS, pEnt, bSilent);
	}
	else
	{
		//only will get here if remove all and no matching items in inventory
		//don't fail in this case
	}

	if(NONNULL(pItem))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Item [%s<%"FORM_LL"u>]x%i successfully removed from inventory list for Entity [%s].",
			pcItemDefName, pItem->id, iCount, pEnt->debugName);
	} else {
		TRANSACTION_APPEND_LOG_FAILURE("Remove Item [%s]x%i from inventory list for Entity [%s] failed: No item found.",
			pcItemDefName, iCount, pEnt->debugName);
	}
	
	return pItem;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Ppinventorybags, .Pplayer.Playertype, .pInventoryV2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pBag, ".*");
NOCONST(Item)* inv_bag_trh_RemoveItemByDef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBag)* pBag, const char* ItemDefName, int count, const ItemChangeReason *pReason )
{
	InventoryList tmpInventoryList = {0};
	int MatchingSlotCount = 0;
	int MatchingItemCount = 0;
	NOCONST(Item)* pItem = NULL;
	int ii;
	int del_count = 0;
	char* pchEntDebugName = NONNULL(pEnt) ? pEnt->debugName : "(null)";


	MatchingSlotCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, ItemDefName, &tmpInventoryList);
	MatchingItemCount = inv_bag_trh_CountItems(ATR_PASS_ARGS, pBag, ItemDefName, -1);

	if ( (count > 0) && ( MatchingItemCount < count ) )
	{
		TRANSACTION_APPEND_LOG_FAILURE("Failed to remove item by Def [%s]x%i from Entity [%s]. Not enough items to satisfy remove count; only %i items found.",
			ItemDefName, count, pchEntDebugName, MatchingItemCount);
		return NULL;
	}

	if ( MatchingSlotCount > 0 )
	{
		//special case for remove all
		if (count == -1)
			count = MatchingItemCount;

		for(ii=0; ii<MatchingSlotCount; ii++)
		{
			InventoryListEntry* pListEntry = &tmpInventoryList.Entry[ii];
			NOCONST(InventoryBag)* pMatchBag = CONTAINER_NOCONST(InventoryBag, pListEntry->pBag);
			NOCONST(InventorySlot)* pSlot = CONTAINER_NOCONST(InventorySlot, pListEntry->pSlot);

			if (ISNULL(pListEntry)) continue;
			if (ISNULL(pMatchBag)) continue;
			if (ISNULL(pSlot)) continue;
			if (ISNULL(pSlot->pItem)) continue;

			if ( (invbag_trh_flags(pMatchBag) & InvBagFlag_PlayerBagIndex) &&
				inv_trh_PlayerBagFail(ATR_PASS_ARGS, pEnt, pMatchBag, atoi(pSlot->pchName) ) )
				continue;

			{
				//remove as many as possible 
				int RemoveCount = (pSlot->pItem->count >= count ? count : pSlot->pItem->count);

				pSlot->pItem->count -= RemoveCount;
				count -= RemoveCount;
				del_count += RemoveCount;


				//save a copy of the first removed item to pass back to caller
				if (ISNULL(pItem))
				{
					pItem = StructCloneNoConst(parse_Item, pSlot->pItem);
				}	

				if (pSlot->pItem->count <= 0)
				{
					//destroy the item instance
					StructDestroyNoConst(parse_Item, pSlot->pItem);
					pSlot->pItem = NULL;

					inv_trh_CollapseBag(ATR_PASS_ARGS, pMatchBag);
				}


				if ( count <= 0 )
				{
					break;
				}
			}
		}

		// Update the Entity's Build
		inv_ent_trh_BuildCurrentFill(ATR_PASS_ARGS, pEnt, bSilent);

		inv_trh_ItemRemovedCallbacks(ATR_PASS_ARGS, pEnt, bSilent, pItem, pBag, -1, del_count, pReason );
	}
	else
	{
		//only will get here if remove all and no matching items in inventory
		//don't fail in this case
	}

	if(NONNULL(pItem))
	{
		TRANSACTION_APPEND_LOG_SUCCESS("Successfully removed item [%s<%"FORM_LL"u>]x%i from Entity [%s].",
			ItemDefName, pItem->id, count, pchEntDebugName);
	}

	return pItem;
}

//routines to remove items from a bag
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that move items within or between bag(s)

// This function removes the last item in the bag, moves the rest up a slot, and inserts the passed in item in slot 1
// Returns dest slot idx in the dest bag that item was moved into, -1 on error
AUTO_TRANS_HELPER
	ATR_LOCKS(pDstEnt, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pDstBag, ".*")
	ATR_LOCKS(pSrcEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(eaSrcPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pSrcBag, ".*");
bool inv_bag_trh_CycleSecondaryItems(ATR_ARGS, bool bSilent, 
									 ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets,
									 ATH_ARG NOCONST(InventoryBag)* pDstBag, 
									 ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
									 ATH_ARG NOCONST(InventoryBag) *pSrcBag, 
									 int iSrcSlot,
									 const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
									 GameAccountDataExtract *pExtract)
{
	S32 iSlot;
	S32 iNumBagSlots = invbag_trh_maxslots(pDstEnt, pDstBag);
	S32 iSrcBagID = invbag_trh_bagid(pSrcBag);
	S32 iDstBagID = invbag_trh_bagid(pDstBag);
	NOCONST(Item) *pSrcItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pSrcEnt, true, pSrcBag, iSrcSlot, 1, pSrcReason);
	NOCONST(Item) *pDstItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pDstEnt, true, pDstBag, iNumBagSlots-1, 1, pDestReason);
	char* pchSrcEntDebugName = NONNULL(pSrcEnt) ? pSrcEnt->debugName : "(null)";
	char* pchDstEntDebugName = NONNULL(pDstEnt) ? pDstEnt->debugName : "(null)";

	if (!pSrcItem) {
		TRANSACTION_APPEND_LOG_FAILURE(
			"Could not remove secondary upgrade from Bag %s:%d on Ent %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), iSrcSlot,
			pchDstEntDebugName);
		return false;
	}
	
	if (!pDstItem) {
		TRANSACTION_APPEND_LOG_FAILURE(
			"Could not remove secondary upgrade from Bag %s:%d on Ent %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), iNumBagSlots-1,
			pchDstEntDebugName);
		return false;
	}
	
	// Bump all items through secondary slots
	for(iSlot = iNumBagSlots-1; iSlot >= 2; iSlot--) {
		if (!inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pDstEnt, eaDstPets, pDstBag, iSlot, pDstEnt, eaDstPets, pDstBag, iSlot-1, 1, false, false, pSrcReason, pDestReason, pExtract)) {
			TRANSACTION_APPEND_LOG_FAILURE(
				"Could not move secondary upgrade from Bag %s:%d to Bag %s:%d on Ent %s", 
				StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), iSlot-1,
				StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), iSlot,
				pchDstEntDebugName);
			return false;
		}
	}
	
	// Put new upgrade into upgrade slot
	if (!inv_bag_trh_AddItem(ATR_PASS_ARGS, pDstEnt, eaDstPets, pDstBag, 1, pSrcItem, ItemAdd_Silent, NULL, pDestReason, pExtract)) {
		TRANSACTION_APPEND_LOG_FAILURE(
			"Could not move secondary upgrade into Bag %s:%d on Ent %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), 1,
			pchDstEntDebugName);
		return false;
	}
	
	// Put old upgrade back into inventory
	if (!inv_bag_trh_AddItem(ATR_PASS_ARGS, pSrcEnt, eaSrcPets, pSrcBag, iSrcSlot, pDstItem, ItemAdd_Silent, NULL, pSrcReason, pExtract)) {
		TRANSACTION_APPEND_LOG_FAILURE(
			"Could not move secondary upgrade into Bag %s:%d on Ent %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), iSrcSlot,
			pchSrcEntDebugName);
		return false;
	}
	
	TRANSACTION_APPEND_LOG_SUCCESS(
		"Secondary Upgrade Equipped from Bag %s:%d to Bag %s:%d on Ent %s", 
		StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), iSrcSlot,
		StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), 1,
		pchDstEntDebugName);
	return true;
}

AUTO_TRANS_HELPER;
S32 inv_bag_trh_ItemMoveFindBestEquipSlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG NOCONST(InventoryBag)* pDstBag, ItemDef* pSrcItemDef, S32 bTrySwap, S32* pbCycleSecondary)
{
	S32 i, iDstSlot = -1;
	S32 iNumBagSlots = invbag_trh_maxslots(pDstEnt, pDstBag);

	//If we're moving a secondary item into an equip/weapon bag with an unspecified slot, special handling is required
	if(item_IsSecondaryUpgrade(pSrcItemDef)) {
		// Secondary items always go in slots above 1, so there must be at least 2 slots in this bag
		if (iNumBagSlots <= 1) {
			return -1;
		}
		// Check if there is an empty secondary slot to put it into
		for(i = 1; i < iNumBagSlots; i++) {
			NOCONST(InventorySlot)* pSlot = eaGet(&pDstBag->ppIndexedInventorySlots, i);
			if (ISNULL(pSlot) || !pSlot->pItem) {
				iDstSlot = i;
				break;
			}
		}
		// If no empty slots were found, we need to cycle the item out of the top slot, and put this in
		// the bottom slot, which is exactly what CycleSecondaryItems does.
		if (iDstSlot < 0) {
			if (*pbCycleSecondary) {
				(*pbCycleSecondary) = true;
			}
			iDstSlot = 1;
		}
	} else if(item_IsPrimaryUpgrade(pSrcItemDef)) {
		// If we are trying to equip a primary upgrade item into an equip bag, try to fit it into the first slot
		NOCONST(InventorySlot)* pSlot = eaGet(&pDstBag->ppIndexedInventorySlots, 0);
		if (bTrySwap || ISNULL(pSlot) || !pSlot->pItem) {
			iDstSlot = 0;
		}
	} else {
		//If it's an "any" slot, try to find an open slot
		for(i = 0; i < iNumBagSlots; i++) {
			NOCONST(InventorySlot)* pSlot = eaGet(&pDstBag->ppIndexedInventorySlots, i);
			if (ISNULL(pSlot) || !pSlot->pItem) {
				iDstSlot = i;
				break;
			}
		}
		if (bTrySwap && iDstSlot < 0) {
			iDstSlot = 0;
		}
	}
	return iDstSlot;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Return the actual number of items which will be moving from src to dst and vice-versa.
// This code should for the most part mirror inv_bag_trh_MoveItem.
// Any functional changes here should likely be represented in those functions as well.

AUTO_TRANS_HELPER;
void inv_bag_trh_GetActualMovingCount(ATR_ARGS,
						  ATH_ARG NOCONST(InventoryBag)* pDstBag, int iDstSlot,
						  ATH_ARG NOCONST(InventoryBag)* pSrcBag, int iSrcSlot, int iCount,
						  int *pSourceToDestCount, int *pDestToSourceCount)
{
	NOCONST(Item) *pSrcItem = NULL;
	NOCONST(Item) *pDstItem = NULL;
	NOCONST(InventorySlot)* pDstSlot = NULL;
	NOCONST(InventorySlot)* pSrcSlot = NULL;
	ItemDef *pSrcItemDef = NULL;
	ItemDef *pDstItemDef = NULL;
	bool bItemsAreMatching = false;
	bool bItemsCanStack = false;
	bool bRoomOnDestStack = false;
	int srcBagFlags = 0;
	int dstBagFlags = 0;
	
	if (pSourceToDestCount!=NULL)
	{
		*pSourceToDestCount=0;
	}
	if (pDestToSourceCount!=NULL)
	{
		*pDestToSourceCount=0;
	}
 
	if (ISNULL(pSrcBag) || ISNULL(pDstBag)) 
	{
		return;
	}
	
	dstBagFlags = invbag_trh_flags(pDstBag);
	srcBagFlags = invbag_trh_flags(pSrcBag);
	
	// Get the source slot and source item def
	pSrcSlot = eaGet(&pSrcBag->ppIndexedInventorySlots, iSrcSlot);
	if ( ISNULL(pSrcSlot) )
		return;
	
	pSrcItemDef = SAFE_GET_REF2(pSrcSlot,pItem,hItem);
	if (ISNULL(pSrcItemDef) || ISNULL(pSrcSlot->pItem)) {
		return;
	}

	// if (iDstSlot < 0 && (invbag_trh_flags(pDstBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag)))	{}
	// If we're equipping an item, it needs special handling (see inv_bag_trh_MoveItem)
	//  But for purposes of counting, we don't have a specific destination, so swapping or stacking are not really an issue.
	//  So just use the regular source counting code.

	// Now that we've got that out of the way, get the destination slot and item def
	pDstSlot = iDstSlot >= 0 ? eaGet(&pDstBag->ppIndexedInventorySlots, iDstSlot) : NULL;
	pDstItemDef = NONNULL(pDstSlot) && NONNULL(pDstSlot->pItem) ? GET_REF(pDstSlot->pItem->hItem) : NULL;

	// Figure out the stacking
	if (NONNULL(pSrcItemDef) && NONNULL(pDstItemDef)) {
		bItemsAreMatching = pSrcItemDef == pDstItemDef;
		bItemsCanStack = pSrcItemDef->iStackLimit > 1;
	}
	
	// If the count isn't specified, use the full count
	if (iCount < 0) {
		iCount = pSrcSlot->pItem->count;
	}
	
	if ( bItemsCanStack && ( ISNULL(pDstSlot) || ISNULL(pDstSlot->pItem) || ( pDstSlot->pItem->count < pSrcItemDef->iStackLimit ) ) )
		bRoomOnDestStack = true;
	
	//if source and dest are both specified and if items don't match or there is no room to stack, then check for swapping
	if ( NONNULL(pDstItemDef) && ( !bItemsAreMatching || !bRoomOnDestStack ) )
	{
		// If we're not moving the entire source stack, we can't swap.
		if (pSrcSlot->pItem->count != iCount)
		{
			return;
		}
		
		// Otherwise we can swap
		if (bItemsAreMatching)
		{
			// We are moving some number of items onto a full stack. The net is that some number move from dest to source.
			if (pDestToSourceCount!=NULL)
			{
				*pDestToSourceCount=(pDstSlot->pItem->count - iCount);
			}
		}
		else
		{
			// It's a swap
			if (pSourceToDestCount!=NULL)
			{
				*pSourceToDestCount=iCount;
			}
			if (pDestToSourceCount!=NULL && NONNULL(pDstSlot->pItem))
			{
				*pDestToSourceCount=pDstSlot->pItem->count;
			}
		}
		return;
	}
	
	//if stacking items, make sure to only move what will fit
	if ( bItemsCanStack )
	{
		int iMaxCount = pDstItemDef->iStackLimit - pDstSlot->pItem->count;
		
		if ( iCount > iMaxCount )
			iCount = iMaxCount;
	}
	
	if (pSourceToDestCount!=NULL)
	{
		*pSourceToDestCount=iCount;
	}
}

AUTO_TRANS_HELPER;
bool inv_trh_CanGemSlot(ATH_ARG NOCONST(Item) *pHolder, ATH_ARG NOCONST(Item) *pGem, int iDestGemSlot)
{
	ItemDef *pHolderDef = SAFE_GET_REF(pHolder, hItem);
	ItemDef *pGemDef = SAFE_GET_REF(pGem, hItem);

	if (pHolder && pHolder->pSpecialProps && pHolder->pSpecialProps->pSuperCritterPet)
	{
		if (scp_IsGemSlotLockedOnPet(CONTAINER_RECONST(SuperCritterPet, pHolder->pSpecialProps->pSuperCritterPet), iDestGemSlot))
			return false;
	}

	return 
		pHolderDef 
		&& pGemDef
		&& !item_trh_IsUnidentified(pHolder)
		&& !item_trh_IsUnidentified(pGem)
		&& iDestGemSlot >= 0
		&& iDestGemSlot < eaSize(&pHolderDef->ppItemGemSlots)
		&& pGemDef->eGemType != kItemGemType_None
		&& (pHolderDef->ppItemGemSlots[iDestGemSlot]->eType == kItemGemType_Any || 
		pHolderDef->ppItemGemSlots[iDestGemSlot]->eType & pGemDef->eGemType);
}

AUTO_TRANS_HELPER;
void inv_trh_GemItemToSlot(ATH_ARG NOCONST(Item) *pItem, int idx, ATH_ARG NOCONST(Item) *pGem)
{
	NOCONST(SpecialItemProps)* pProps = item_trh_GetOrCreateSpecialProperties(pItem);
	NOCONST(ItemGemSlot)* pSlot = NULL;

	ItemDef *pHolderDef = GET_REF(pItem->hItem);
	ItemDef* pGemDef = SAFE_GET_REF(pGem, hItem);

	while (idx >= eaSize(&pProps->ppItemGemSlots))
	{
		eaPush(&pProps->ppItemGemSlots, StructCreateNoConst(parse_ItemGemSlot));
	}

	pSlot = pProps->ppItemGemSlots[idx];

	// make sure all the powers are there
	item_trh_FixupPowers(pGem);

	eaCopyStructs(&(Power**)pGem->ppPowers,&(Power**)pSlot->ppPowers,parse_Power);

	if(pHolderDef && (pHolderDef->flags & kItemDefFlag_LevelFromQuality))
	{
		int i;
		int iLevelToSet = item_trh_GetGemPowerLevel(pItem);

		//Subtract the level that was added
		for(i=0;i<eaSize(&pSlot->ppPowers);i++)
		{
			pSlot->ppPowers[i]->iLevel = iLevelToSet;
		}
	}

	if (pGemDef && ((pGemDef->flags & kItemDefFlag_BindOnEquip) || (pGem->flags & kItemFlag_Bound)))
		pItem->flags |= kItemFlag_Bound;

	if (pGemDef && ((pGemDef->flags & kItemDefFlag_BindToAccountOnEquip) || (pGem->flags & kItemFlag_BoundToAccount)))
		pItem->flags |= kItemFlag_BoundToAccount;

	SET_HANDLE_FROM_REFERENT(g_hItemDict,GET_REF(pGem->hItem),pSlot->hSlottedItem);

	// Fix up the item since we slotted the gem. This function call will add proper powers to the item
	item_trh_FixupPowers(pItem);

	StructDestroy(parse_Item,(Item*)pGem);
}

AUTO_TRANS_HELPER;
bool inv_trh_GemItem(ATH_ARG NOCONST(Item) *pHolder,ATH_ARG NOCONST(Item) *pGem, int iDestGemSlot)
{
	if(iDestGemSlot != -1)
	{
		if(inv_trh_CanGemSlot(pHolder,pGem,iDestGemSlot))
		{
			inv_trh_GemItemToSlot(pHolder, iDestGemSlot, pGem);
			return true;
		}
	}
	else
	{
		ItemDef *pHolderItemDef = GET_REF(pHolder->hItem);
		S32 iNumSlots = pHolderItemDef ? eaSize(&pHolderItemDef->ppItemGemSlots) : 0;
		if (iNumSlots > 0)
		{
			S32 i;
			// Find the first open slot
			for (i = 0; i <= iNumSlots; i++)
			{
				if (inv_trh_CanGemSlot(pHolder, pGem, i) && 
					(pHolder->pSpecialProps == NULL || 
					i >= eaSize(&pHolder->pSpecialProps->ppItemGemSlots) ||
					!IS_HANDLE_ACTIVE(pHolder->pSpecialProps->ppItemGemSlots[i]->hSlottedItem)))
				{
					inv_trh_GemItemToSlot(pHolder, i, pGem);
					return true;
				}
			}
		}
		
	}

	return false;
}

AUTO_TRANS_HELPER;
bool inv_trh_RemoveGemmedItem(ATR_ARGS, ATH_ARG NOCONST(Item) *pHolder, int iGemSlot,NOCONST(Item) **ppItemOut)
{
	NOCONST(Item)* pItemReturned = NULL;

	S32 iSlotCount = pHolder->pSpecialProps ? eaSize(&pHolder->pSpecialProps->ppItemGemSlots) : 0;

	if(ppItemOut)
	{
		ItemDef* pGemDef;

		*ppItemOut = iGemSlot < iSlotCount  ? 
			inv_ItemInstanceFromDefName(REF_STRING_FROM_HANDLE(pHolder->pSpecialProps->ppItemGemSlots[iGemSlot]->hSlottedItem),1,0,NULL,NULL,NULL,false,NULL) : NULL;

		if(!(*ppItemOut))
			return false;

		pGemDef = GET_REF((*ppItemOut)->hItem);

		if (pGemDef && (pGemDef->flags & (kItemDefFlag_BindOnEquip | kItemDefFlag_BindOnPickup)))
			(*ppItemOut)->flags |= kItemFlag_Bound;

		if (pGemDef && (pGemDef->flags & (kItemDefFlag_BindToAccountOnEquip | kItemDefFlag_BindToAccountOnPickup)))
			(*ppItemOut)->flags |= kItemFlag_BoundToAccount;

	}

	if (iGemSlot == iSlotCount - 1)
	{
		StructDestroyNoConst(parse_ItemGemSlot, pHolder->pSpecialProps->ppItemGemSlots[iGemSlot]);
		eaRemove(&pHolder->pSpecialProps->ppItemGemSlots, iGemSlot);
	}
	else
	{
		StructResetNoConst(parse_ItemGemSlot, pHolder->pSpecialProps->ppItemGemSlots[iGemSlot]);
	}
	
	item_trh_FixupSpecialProps(pHolder); 
	return true;
}

AUTO_TRANS_HELPER;
bool inv_bag_trh_UnGemItem(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pSrcBag, int SrcSlotIdx, U64 uSrcItemID, int iGemSlotIdx, ATH_ARG NOCONST(InventoryBag) *pDestBag, int DestSlotIdx, const char *pchCostNumeric, int iCost, GameAccountDataExtract *pExtract, const ItemChangeReason *pReason)
{
	NOCONST(Item) *pSrcItem = NULL;
	NOCONST(Item) *pSrcItemHolder = NULL;
	ItemDef *pSrcItemDef = NULL;
	ItemDef *pDstItemDef = NULL;
	NOCONST(InventorySlot)* pDstSlot = NULL;
	NOCONST(InventorySlot)* pSrcSlot = NULL;

	if(pSrcBag->BagID == InvBagIDs_Buyback)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Src bag may not be a buyback bag.");
		return false;
	}

	pSrcSlot = eaGet(&pSrcBag->ppIndexedInventorySlots, SrcSlotIdx);
	pSrcItemDef = SAFE_GET_REF2(pSrcSlot,pItem,hItem);
	if (ISNULL(pSrcItemDef) || ISNULL(pSrcSlot->pItem)) {
		// There is no provision for an unspecified or empty source slot
		return false;
	}

	pSrcItemHolder = pSrcSlot->pItem;

	if(!pSrcItemHolder->pSpecialProps || 
		!pSrcItemHolder->pSpecialProps->ppItemGemSlots 
		|| iGemSlotIdx > eaSize(&pSrcItemHolder->pSpecialProps->ppItemGemSlots))
	{
		return false;
	}

	if(!GET_REF(pSrcItemHolder->pSpecialProps->ppItemGemSlots[iGemSlotIdx]->hSlottedItem))
		return false;

	// Get the dest slot and dest item def
	pDstSlot = eaGet(&pDestBag->ppIndexedInventorySlots, DestSlotIdx);
	pDstItemDef = SAFE_GET_REF2(pDstSlot,pItem,hItem);

	if(pDstItemDef && pDstItemDef != pSrcItemDef)
		return false;

	//Remove the old item
	inv_trh_RemoveGemmedItem(ATR_PASS_ARGS,pSrcItemHolder,iGemSlotIdx,&pSrcItem);

	//Add it to the bag
	if(!pSrcItem || !inv_bag_trh_AddItem(ATR_PASS_ARGS,pEnt,NULL,pDestBag,DestSlotIdx,pSrcItem,0,NULL,NULL,pExtract))
		return false;

	//charge the cost
	if(iCost && !inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pEnt,false,pchCostNumeric,iCost * -1,pReason))
		return false;

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GEMS, "ItemGemUnSlotted",
		"EntID %d BaseItem \"%s\" GemItem \"%s\" GemTier %d ItemLevel %d",
		pEnt->myContainerID, GET_REF(pSrcItemHolder->hItem) ? GET_REF(pSrcItemHolder->hItem)->pchName : NULL, pSrcItemDef->pchName,itemUpgrade_FindCurrentRank(GET_REF(pSrcItem->hItem), NULL),item_trh_GetLevel(pSrcItemHolder));

	return true;
}

AUTO_TRANS_HELPER;
bool inv_bag_trh_MoveGemItem(ATR_ARGS,
							ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, 
							ATH_ARG NOCONST(InventoryBag)* pDstBag, int iDstSlot, U64 uDstItemID, int iDstGemSlot,
							ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
							ATH_ARG NOCONST(InventoryBag)* pSrcBag, int iSrcSlot, U64 uSrcItemID, int bSrcBuyBackBagAllowed,
							const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
							GameAccountDataExtract *pExtract)
{
	NOCONST(Item) *pSrcItem = NULL;
	NOCONST(Item) *pDstItem = NULL;
	NOCONST(Item) *pSrcItemHolder = NULL;
	NOCONST(Item) *pDstItemHolder = NULL;
	NOCONST(InventorySlot)* pDstSlot = NULL;
	NOCONST(InventorySlot)* pSrcSlot = NULL;
	ItemDef *pSrcItemDef = NULL;
	ItemDef *pDstItemDef = NULL;
	bool bItemsAreMatching = false;
	bool bItemsCanStack = false;
	bool bRoomOnDestStack = false;
	int bagFlags = 0;
	int iSrcGemmedSlot = -1;

	if (ISNULL(pSrcBag) || ISNULL(pDstBag)) 
	{
		return false;
	}

	if(!bSrcBuyBackBagAllowed && pSrcBag->BagID == InvBagIDs_Buyback)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Src bag may not be a buyback bag.");
		return false;
	}

	bagFlags = invbag_trh_flags(pDstBag);

	// Get the source slot and source item def
	pSrcSlot = eaGet(&pSrcBag->ppIndexedInventorySlots, iSrcSlot);
	pSrcItemDef = SAFE_GET_REF2(pSrcSlot,pItem,hItem);
	if (ISNULL(pSrcItemDef) || ISNULL(pSrcSlot->pItem)) {
		// There is no provision for an unspecified or empty source slot
		return false;
	}

	pSrcItem = pSrcSlot->pItem;

	// Get the source slot and source item def
	pDstSlot = eaGet(&pDstBag->ppIndexedInventorySlots, iDstSlot);
	pDstItemDef = SAFE_GET_REF2(pDstSlot,pItem,hItem);
	if (ISNULL(pDstItemDef) || ISNULL(pDstSlot->pItem)) {
		// There is no provision for an unspecified or empty source slot
		return false;
	}

	pDstItemHolder = pDstSlot->pItem;

	// Find if this is the actual gem, or the holder of the gem
	if(pSrcItem->id != uSrcItemID)
	{
		pSrcItemHolder = pSrcItem;
	}

	if(pDstItemHolder->id != uDstItemID)
	{
		return false;
	}

	if(NONNULL(pSrcItemHolder))
	{
		inv_trh_RemoveGemmedItem(ATR_PASS_ARGS,pSrcItemHolder,iSrcGemmedSlot,&pSrcItem);
	}
	else
	{
		pSrcItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS,pSrcEnt,false,pSrcBag,iSrcSlot,1,pSrcReason);
	}


	if(NONNULL(pSrcItem))
	{
		//Clear destination item from its bag
		if(pDstItemHolder->pSpecialProps && 
			iDstGemSlot < eaSize(&pDstItemHolder->pSpecialProps->ppItemGemSlots) &&
			IS_HANDLE_ACTIVE(pDstItemHolder->pSpecialProps->ppItemGemSlots[iDstGemSlot]->hSlottedItem))
		{
			inv_trh_RemoveGemmedItem(ATR_PASS_ARGS,pDstItemHolder,iDstGemSlot,&pDstItem);
		}
		
		//Gem the item
		if(!inv_trh_GemItem(pDstItemHolder,pSrcItem,iDstGemSlot))
			return false;

		//Place the destination item to the sources original spot
		if(pDstItem && eaSize(&s_ItemGemConfig.ppUnslotCosts) == 0)
		{
			if(pSrcItemHolder)
			{
				inv_trh_GemItem(pSrcItemHolder,pDstItem,iSrcGemmedSlot);
			}
			else
			{
				if(!inv_bag_trh_AddItem(ATR_PASS_ARGS,pSrcEnt,eaSrcPets,pSrcBag,iSrcSlot,pDstItem,0,NULL,NULL,pExtract))
					return false;
			}
		}
		else if(pDstItem)
		{
			StructDestroy(parse_Item, (Item*)pDstItem);
		}

		if (NONNULL(pSrcEnt))
		{
			item_trh_FixupPowerIDs(pSrcEnt, pDstItemHolder);
		}

		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GEMS, "ItemGemSlotting",
			"EntID %d BaseItem \"%s\" GemItem \"%s\" GemTier %d ItemLevel %d",
			pSrcEnt->myContainerID, pDstItemDef->pchName, pSrcItemDef->pchName, itemUpgrade_FindCurrentRank(pSrcItemDef, NULL), item_trh_GetLevel(pDstItemHolder));
		return true;
	}

	return false;
}
	

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

//move item(s) from one bag/slot to another bag/slot
//DestSlotIdx = -1 specifies first open slot, or end of bag | DestSlotIdx = -2 specifies end of bag only
//Count specifies # of items to move, -1 specifies all items in slot
//returns dest slot idx in the dest bag that item was moved into, -1 on error
//NOTE: you may also need to modify inv_bag_trh_GetActualMovingCount. 
AUTO_TRANS_HELPER;
bool inv_bag_trh_MoveItem(ATR_ARGS, bool bSilent, 
						  ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, 
						  ATH_ARG NOCONST(InventoryBag)* pDstBag, int iDstSlot,
						  ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
						  ATH_ARG NOCONST(InventoryBag)* pSrcBag, int iSrcSlot, int iCount, 
						  int bSrcBuyBackBagAllowed, bool bUseOverflow,
						  const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
						  GameAccountDataExtract *pExtract)
{
	NOCONST(Item) *pSrcItem = NULL;
	NOCONST(Item) *pDstItem = NULL;
	NOCONST(InventorySlot)* pDstSlot = NULL;
	NOCONST(InventorySlot)* pSrcSlot = NULL;
	ItemDef *pSrcItemDef = NULL;
	ItemDef *pDstItemDef = NULL;
	bool bItemsAreMatching = false;
	bool bItemsCanStack = false;
	bool bRoomOnDestStack = false;
	int iSrcCount = 0;
	int iDstCount = 0;
	int bagFlags = 0;
	
	if (ISNULL(pSrcBag) || ISNULL(pDstBag)) 
	{
		return false;
	}
	
	if(!bSrcBuyBackBagAllowed && pSrcBag->BagID == InvBagIDs_Buyback)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Src bag may not be a buyback bag.");
		return false;
	}
	
	bagFlags = invbag_trh_flags(pDstBag);
	
	//!!!! for now, Items in ItemLite bags can not be moved
	if ((invbag_trh_type(pSrcBag) == InvBagType_ItemLite) || (invbag_trh_type(pDstBag) == InvBagType_ItemLite)) {
		return false;
	}
	
	// Get the source slot and source item def
	pSrcSlot = eaGet(&pSrcBag->ppIndexedInventorySlots, iSrcSlot);
	pSrcItemDef = SAFE_GET_REF2(pSrcSlot,pItem,hItem);
	if (ISNULL(pSrcItemDef) || !pSrcSlot->pItem || ISNULL(pSrcSlot->pItem)) {
		// There is no provision for an unspecified or empty source slot
		return false;
	}

	// Don't allow the movement of unmovable items
	if (pSrcItemDef->flags & kItemDefFlag_CantMove) {
		return false;
	}

	iSrcCount = pSrcSlot->pItem->count;

	// If we're equipping an item, it needs special handling
	if (iDstSlot < 0 && (invbag_trh_flags(pDstBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag))) { 
		S32 bCycleSecondary = false;
		iDstSlot = inv_bag_trh_ItemMoveFindBestEquipSlot(ATR_PASS_ARGS, pDstEnt, pDstBag, pSrcItemDef, false, &bCycleSecondary);
		if (iDstSlot < 0) {
			return false;
		} else if (bCycleSecondary) {
			return inv_bag_trh_CycleSecondaryItems(ATR_PASS_ARGS, bSilent, pDstEnt, eaDstPets, pDstBag, pSrcEnt, eaSrcPets, pSrcBag, iSrcSlot, pSrcReason, pDestReason, pExtract);
		}
	}

	// Now that we've got that out of the way, get the destination slot and item def
	pDstSlot = iDstSlot >= 0 ? eaGet(&pDstBag->ppIndexedInventorySlots, iDstSlot) : NULL;
	pDstItemDef = NONNULL(pDstSlot) && NONNULL(pDstSlot->pItem) ? GET_REF(pDstSlot->pItem->hItem) : NULL;
	
	if (pDstSlot && pDstSlot->pItem)
		iDstCount = pDstSlot->pItem->count;

	// Figure out the stacking
	if (NONNULL(pSrcItemDef) && NONNULL(pDstItemDef)) {
		bItemsAreMatching = pSrcItemDef == pDstItemDef;
		bItemsCanStack = pSrcItemDef->iStackLimit > 1;
	}
	
	// If the count isn't specified, use the full count
	if (iCount < 0) {
		iCount = iSrcCount;
	}


	// If the source slot is a player bag, and the player bag isn't empty, fail
	if ((invbag_trh_flags(pSrcBag) & InvBagFlag_PlayerBagIndex) && inv_trh_PlayerBagFail(ATR_PASS_ARGS, pSrcEnt, pSrcBag, iSrcSlot)) {
		return false;
	}
	
	// If the destination slot is a player bag, and the player bag isn't empty, fail
	// (Note that dropping items into a player bag is handled by changing the destination
	//  bag before calling the move transaction.)
	if ( pDstItemDef &&
		(invbag_trh_flags(pDstBag) & InvBagFlag_PlayerBagIndex) &&
		inv_trh_PlayerBagFail(ATR_PASS_ARGS, pDstEnt, pDstBag, iDstSlot ) )
	{
		return false;
	}
	
	if ( bItemsCanStack && ( ISNULL(pDstSlot) || ( iDstCount < pSrcItemDef->iStackLimit ) ) )
		bRoomOnDestStack = true;
	
	//if source and dest are both specified and you are moving the entire stack, then swap
	if (NONNULL(pDstItemDef) && iSrcCount == iCount && (!bRoomOnDestStack || !bItemsAreMatching)) {
		return inv_trh_SwapItems(ATR_PASS_ARGS, bSilent, pDstEnt, eaDstPets, pDstBag, iDstSlot, pSrcEnt, eaSrcPets, pSrcBag, iSrcSlot, pSrcReason, pDestReason, pExtract);
	}
	
	//if items don't match or there is no room to stack, then exit
	if ( NONNULL(pDstItemDef) && ( !bItemsAreMatching || !bRoomOnDestStack ) )
		return false;
	
	if ( ISNULL(pSrcSlot) )
		return false;
	
	// special case handling to deal with all items in a slot
	if ( iCount == -1 )
	{
		iCount = iSrcCount;
	}
	
	//if stacking items, make sure to only move what will fit
	if ( bItemsCanStack )
	{
		int iMaxCount = pDstItemDef->iStackLimit - iDstCount;
		
		if ( iCount > iMaxCount )
			iCount = iMaxCount;
	}
	
	//normal case is to remove the item for source slot and put it into dest slot
	pSrcItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pSrcEnt, true, pSrcBag, iSrcSlot, iCount, pSrcReason);
	
	if (NONNULL(pSrcItem)) {
		ItemDef *pItemDef = GET_REF(pSrcItem->hItem);
		ItemAddFlags eFlags = 0;
		bool bIsOwnerSame = entity_trh_IsOwnerSame(ATR_PASS_ARGS, pSrcEnt, pDstEnt);

		bool bAddSilently = !pItemDef ||
			!((invbag_trh_bagid(pDstBag) == InvBagIDs_Recipe && (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemPowerRecipe)) ||
			invbag_trh_bagid(pSrcBag) == InvBagIDs_Overflow);
		
		//clear the power ids if the item is moving between owner ents
		if (!bIsOwnerSame)
			item_trh_ClearPowerIDs(pSrcItem);

		// set silent flag
		// bSilent can only be used to force verbosity (TODO (JDJ): figure some better way of doing this; this
		// is only fine for now because almost all invocations of this pass bSilent as true)
		if (bAddSilently && bSilent)
			eFlags |= ItemAdd_Silent;

		if (bUseOverflow)
			eFlags |= ItemAdd_UseOverflow;

		// if moving from the overflow or buyback bag, treat it as if the item was being added to the inventory from an external
		// source
		if (invbag_trh_bagid(pSrcBag) != InvBagIDs_Overflow && invbag_trh_bagid(pSrcBag) != InvBagIDs_Buyback)
			eFlags |= ItemAdd_OverrideStackRules;

		// ignore uniqueness when moving between ents with the same owner
		if (bIsOwnerSame)
			eFlags |= ItemAdd_IgnoreUnique;

		return inv_bag_trh_AddItem(ATR_PASS_ARGS, pDstEnt, eaDstPets, pDstBag, iDstSlot, pSrcItem, eFlags, NULL, pDestReason, pExtract);
	} else {
		return false;
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Itemidmax, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype");
bool inv_ent_trh_MoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, int SrcBagID, int DestBagID, int SrcSlotIdx, int DestSlotIdx, int Count, int bSrcBuyBackBagAllowed, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pDestBag = NULL;
	NOCONST(InventoryBag)* pSrcBag = NULL;


	if ( ISNULL(pEnt))
		return false;

	if ( ISNULL(pEnt->pInventoryV2))
		return false;

	pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, DestBagID, pExtract);
	pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, SrcBagID, pExtract);

	if (ISNULL(pSrcBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE( "No Bag %s on Ent %s", StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), pEnt->debugName );
		return false;
	}

	if (ISNULL(pDestBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("No Bag %s on Ent %s", StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), pEnt->debugName );
		return false;
	}
	
	if(!bSrcBuyBackBagAllowed && pSrcBag->BagID == InvBagIDs_Buyback)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Buyback bag as src without bSrcBuyBackBagAllowed set to true.");
		return false;
	}

	return inv_bag_trh_MoveItem(ATR_PASS_ARGS, bSilent, pEnt, NULL, pDestBag, DestSlotIdx, pEnt, NULL, pSrcBag, SrcSlotIdx, Count, bSrcBuyBackBagAllowed, false, pReason, pReason, pExtract);
}

//routines that move items within or between bag(s)
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that traverse a bag

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
void inv_bag_trh_ForEachItemInBag( ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, inv_trh_foreachitem_Callback pCallback, void *pData )
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;

	//loop for all slots in this bag
	for(ii=0; ii<NumSlots; ii++)
	{
		NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[ii];
		if (pSlot->pItem)
		{
			//int jj;
			//for(jj=0; jj<pSlot->count; jj++)		//loop once for each multiple of an item
			//!!!! for now just one item for all multiples with count passed back in slot
			{
				//execute the callback for items
				if ( NONNULL(pCallback) )
				{
					ItemDef *pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
					(*pCallback)(pSlot->pItem, pItemDef, pBag, pSlot, pData);
				}
			}
		}
	}
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that clear bags

//delete all items in a bag (and nested bags)
//does not remove the bags
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots, .Inv_Def, .Bagid");
void inv_bag_trh_ClearBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	int ii;

	//loop for all slots in this bag
	for(ii=0; ii<NumSlots; ii++)
	{
		NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[ii];

		if (pSlot->pItem)
		{
			StructDestroyNoConst(parse_Item, pSlot->pItem);
			pSlot->pItem = NULL;
		}
	}

	inv_trh_CollapseBag(ATR_PASS_ARGS, pBag);

#ifdef GAMESERVER
	//if dest bag is a PlayerBagIndex bag then queue up update player bags command
	if ( invbag_trh_flags(pBag) & InvBagFlag_PlayerBagIndex )
		QueueRemoteCommand_RemoteUpdatePlayerBags(ATR_RESULT_SUCCESS, 0, 0);
#endif
}
AUTO_TRANS_HELPER
	ATR_LOCKS(pBag, ".Ppindexedliteslots, .Bagid");
void inv_lite_trh_ClearBag(ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pBag)
{
	int NumSlots = eaSize(&pBag->ppIndexedLiteSlots);
	int ii;
	NOCONST(InventorySlotLite)* pSlot = NULL;

	for (ii=NumSlots-1; ii>=0; ii--)
	{
		pSlot = pBag->ppIndexedLiteSlots[ii];
		if (!(pBag->BagID == InvBagIDs_Numeric && stricmp(pSlot->pchName,"Level") == 0))//not allowed to remove level numeric
		{
			eaRemove(&pBag->ppIndexedLiteSlots, ii);
			StructDestroyNoConst(parse_InventorySlotLite, pSlot);
		}
		else
		{
			pBag->ppIndexedLiteSlots[ii]->count = 1;
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.peaOwnedUniqueItems");
bool inv_ent_trh_ClearAllBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(Item)* pItem = NULL;
	int NumBags, ii;


	if ( ISNULL(pEnt))
		return false;
			
	if ( ISNULL(pEnt->pInventoryV2))
		return false;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	for(ii=0;ii<NumBags;ii++)
	{
		inv_bag_trh_ClearBag(ATR_PASS_ARGS, pEnt->pInventoryV2->ppInventoryBags[ii]);
	}

	NumBags = eaSize(&pEnt->pInventoryV2->ppLiteBags);
	for(ii=0;ii<NumBags;ii++)
	{
		inv_lite_trh_ClearBag(ATR_PASS_ARGS, pEnt->pInventoryV2->ppLiteBags[ii]);
	}
	eaDestroyStructNoConst(&pEnt->pInventoryV2->peaOwnedUniqueItems, parse_OwnedUniqueItem);
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags");
bool inv_guildbank_trh_ClearAllBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank)
{
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(Item)* pItem = NULL;
	int NumBags, i;
	
	if (ISNULL(pGuildBank)) {
		return false;
	}
	
	if (ISNULL(pGuildBank->pInventoryV2)) {
		return false;
	}
	
	NumBags = eaSize(&pGuildBank->pInventoryV2->ppInventoryBags);
	
	for(i=0; i < NumBags; i++) {
		inv_bag_trh_ClearBag(ATR_PASS_ARGS, pGuildBank->pInventoryV2->ppInventoryBags[i]);
	}

	NumBags = eaSize(&pGuildBank->pInventoryV2->ppLiteBags);
	for(i=0;i<NumBags;i++)
	{
		inv_lite_trh_ClearBag(ATR_PASS_ARGS, pGuildBank->pInventoryV2->ppLiteBags[i]);
	}
	return true;
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//fixup routines

AUTO_TRANS_HELPER;
void inv_trh_FixupItem(ATR_ARGS,ATH_ARG NOCONST(Item) *pItem, ATH_ARG NOCONST(InventorySlot) *pSlot, ATH_ARG NOCONST(Entity)* pEnt,  bool bFixItemIDs, const ItemChangeReason *pReason)
{
	ItemDef *pItemDef = GET_REF(pItem->hItem);

	inv_trh_InitAndFixupItem(ATR_PASS_ARGS, pEnt, pItem, bFixItemIDs, pReason);

	if(pItem->id && !item_idCheck(pEnt, pItem, NULL))
	{
		pSlot->pItem->id = 0;
		item_trh_SetItemID(pEnt, pItem);
	}
	item_trh_FixupPowers(pItem);
	item_trh_FixupPowerIDs(pEnt, pItem);
}

AUTO_TRANS_HELPER 
ATR_LOCKS(pEnt, ".Psaved.Pscpdata, .pInventoryV2.Ppinventorybags, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Itemidmax, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containertype, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Pplayer.Playertype");
void inv_trh_FixupInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bFixItemIDs, const ItemChangeReason *pReason)
{

	int NumBags,iBag;

	if ( ISNULL(pEnt))
		return;

	if ( ISNULL(pEnt->pInventoryV2))
		return;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	eaDestroyStructNoConst(&pEnt->pInventoryV2->peaOwnedUniqueItems, parse_OwnedUniqueItem);
	for(iBag=0;iBag<NumBags;iBag++)
	{
		int NumSlots,iSlot,iFlags;

		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];
		iFlags = invbag_trh_flags(pBag);

		NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
		for(iSlot=0;iSlot<NumSlots;iSlot++)
		{
			NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];

			if (NONNULL(pSlot->pItem) )
			{
				ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);

				if (!pSlot->pItem->count && pItemDef && pItemDef->eType != kItemType_Numeric)
				{
					StructDestroyNoConstSafe(parse_Item, &pSlot->pItem);
					continue;
				}

				inv_trh_FixupItem(ATR_PASS_ARGS,pSlot->pItem,pSlot,pEnt,bFixItemIDs,pReason);

				//BH 08/31/2010: This used to be in what was called "inv_trh_FixupItem", but since that function 
				// gets called to also initialize items, I have moved it here.
				if(pItemDef)
				{
					//Cache unique item def names
					if (pItemDef->flags & kItemDefFlag_Unique)
						inv_ent_trh_RegisterUniqueItem(ATR_PASS_ARGS, pEnt, pItemDef->pchName, true);

					if(!(pSlot->pItem->flags & kItemFlag_Bound) && pItemDef->flags & kItemDefFlag_BindOnPickup)
					{
						pSlot->pItem->flags |= kItemFlag_Bound;

						//Passing null for the extract here because I don't care if the player could access this bag or not
						inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, false, pSlot->pItem, NULL);
						if(pItemDef->bDeleteAfterUnlock)
						{
							NOCONST(Item) *pRemItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, true, pBag, iSlot, -1, pReason);
							StructDestroyNoConst(parse_Item, pRemItem);
						} 
					}
				}

				/***
				SDANGELO: Disabled this code because it broke STO.
							Before enabling, we need to consider if this can be done safely.
							Right now it tries to replicate logic from the normal item stuff,
							but it's slightly different and error-prone to do this.
							It also won't work on STO pets and puppets because their validation
							Requires access to the owner entity to work and this code doesn't 
							do that properly... which was causing items on puppets that were legal
							to be considered illegal and get moved to the overflow bag.

				// validate that the item can stay in its current location (most of this
				// taken from item_ItemMoveDestValidInternal)
				if (!pItemDef || 
					pBag->BagID == InvBagIDs_Inventory || 
					(iFlags & (InvBagFlag_PlayerBag | InvBagFlag_BankBag | InvBagFlag_GuildBankBag)) || 
					pBag->BagID == InvBagIDs_Overflow)
				{
					continue;
				}
				if (((iFlags & InvBagFlag_SpecialBag) && !itemdef_VerifyUsageRestrictions((Entity*) pEnt, pItemDef, 0, NULL)) ||
					((iFlags & InvBagFlag_PlayerBagIndex) && pItemDef->eType != kItemType_Bag) ||
					((iFlags & InvBagFlag_RecipeBag) && pItemDef->eType != kItemType_ItemRecipe && pItemDef->eType != kItemType_ItemPowerRecipe) ||
					((iFlags & InvBagFlag_DeviceBag) && pItemDef->eType != kItemType_Device) ||
					((iFlags & InvBagFlag_WeaponBag || iFlags & InvBagFlag_ActiveWeaponBag) && (pItemDef->eType != kItemType_Weapon))
				{
					bMoveToOverflow = true;
				}
				if ((iFlags & InvBagFlag_EquipBag))
				{
					if (pItemDef->eType != kItemType_Upgrade)
					{
						bMoveToOverflow = true;
					}
					else if (item_IsSecondaryUpgrade(pItemDef) && iSlot == 0)
					{
						bMoveToOverflow = true;
					}
					else if (item_IsPrimaryUpgrade(pItemDef) && iSlot > 0)
					{
						bMoveToOverflow = true;
					}
				}
				if (!item_IsRecipe(pItemDef) &&
					!((pItemDef->eRestrictBagID == InvBagIDs_None && pItemDef->eRestrictBag2ID == InvBagIDs_None) ||
						(pItemDef->eRestrictBagID == pBag->BagID) ||
						(pItemDef->eRestrictBag2ID == pBag->BagID)))
				{
					bMoveToOverflow = true;
				}

				if (bMoveToOverflow)
				{
					inv_ent_trh_MoveItem(ATR_PASS_ARGS, pEnt, false, pBag->BagID, InvBagIDs_Overflow, iSlot, -1, -1);
				}
				***/

				if(pSlot->pItem->pSpecialProps && pSlot->pItem->pSpecialProps->ppItemGemSlots)
				{
					int iGemSlot;

					for(iGemSlot=0;iGemSlot<eaSize(&pSlot->pItem->pSpecialProps->ppItemGemSlots);iGemSlot++)
					{
						if(IS_HANDLE_ACTIVE(pSlot->pItem->pSpecialProps->ppItemGemSlots[iGemSlot]->hSlottedItem))
						{
							//TODO(MM) Write a fix up func to make sure all the powers are there 
							//inv_trh_FixupItem(ATR_PASS_ARGS,pSlot->pItem->ppItemGemSlots[iGemSlot]->pSlottedItem,NULL,pEnt,bFixItemIDs,pReason);
						}
					}
				}
				//Move deprecated pets out of the pet bag and fixup their state.
				if (pBag->BagID == InvBagIDs_SuperCritterPets &&
					(!pItemDef || 
					pItemDef->eType != kItemType_SuperCritterPet || 
					!GET_REF(pItemDef->hSCPdef)))
				{
					Item* pInvalidItem = NULL;
					if (!pItemDef)
						Errorf("Entity %s had item with nonexistant def: \"%s\". This is very bad!", pEnt->debugName, REF_STRING_FROM_HANDLE(pSlot->pItem->hItem));
					else
					{
						if (!GET_REF(pItemDef->hSCPdef))
							Errorf("Entity %s had item with nonexistant SCP def: \"%s\". This is very bad!", pEnt->debugName, REF_STRING_FROM_HANDLE(pItemDef->hSCPdef));
						inv_ent_trh_MoveItem(ATR_PASS_ARGS, pEnt, true, InvBagIDs_SuperCritterPets, InvBagIDs_Overflow, iSlot, -1, 1, false, NULL, NULL); 
					}
					scp_trh_ResetActivePet(ATR_PASS_ARGS, pEnt, 0, iSlot);
				}
			}
		}
	}

	//remove zeroed itemlites, update uniques
	NumBags = eaSize(&pEnt->pInventoryV2->ppLiteBags);
	for(iBag=0;iBag<NumBags;iBag++)
	{
		int NumSlots,iSlot;

		NOCONST(InventoryBagLite) *pBag = pEnt->pInventoryV2->ppLiteBags[iBag];

		NumSlots = eaSize(&pBag->ppIndexedLiteSlots);
		for(iSlot=NumSlots-1;iSlot>=0;iSlot--)
		{
			ItemDef* pDef = RefSystem_ReferentFromString(g_hItemDict, pBag->ppIndexedLiteSlots[iSlot]->pchName);
			SET_HANDLE_FROM_REFERENT(g_hItemDict, pDef, pBag->ppIndexedLiteSlots[iSlot]->hItemDef);
			
			if (pBag->ppIndexedLiteSlots[iSlot]->count <= 0)
			{
				eaRemove(&pBag->ppIndexedLiteSlots, iSlot);
			}
			else if(pDef && (pDef->flags & kItemDefFlag_Unique))
				inv_ent_trh_RegisterUniqueItem(ATR_PASS_ARGS, pEnt, pDef->pchName, true);
		}
	}

	inv_trh_UpdateItemSets(ATR_PASS_ARGS, pEnt, pReason);
}

//This function gets called to fixup an item AND to init an item
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Itemidmax")
ATR_LOCKS(pItem, ".Id, .Flags, .Pppowers, .Hitem, .Palgoprops, .pSpecialProps");
void inv_trh_InitAndFixupItem(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG  NOCONST(Item) *pItem, bool bFixItemIDs, const ItemChangeReason *pReason)
{
#ifdef GAMESERVER
	int i;
	ItemDef* pItemDef = GET_REF(pItem->hItem);
	
	//fixup item IDs
	//Critters don't need unique item ids.
	if(bFixItemIDs && NONNULL(pEnt) && pEnt->myEntityType != GLOBALTYPE_ENTITYCRITTER)
	{
		//Only set the id, if the id has yet to be set, or the item ID has a conflict with another item
		//in the players inventory
		if(pItem->id == 0)
		{	
			item_trh_SetItemID(pEnt,pItem);
		}
	}
	
	if (pItemDef)
	{
		AlgoItemLevelsDef *pAlgoItemLevels;

		// reset level on to appropriate value on all items
		pAlgoItemLevels = eaIndexedGetUsingString(&g_CommonAlgoTables.ppAlgoItemLevels, StaticDefineIntRevLookup(ItemQualityEnum, item_trh_GetQuality(pItem)));
		if (((pItem->flags & kItemFlag_Algo) || (pItemDef->flags & kItemDefFlag_LevelFromQuality) != 0)
			&& pAlgoItemLevels)
		{
			if(item_trh_GetMinLevel(pItem) > 0)
				item_trh_SetAlgoPropsLevel(pItem, pAlgoItemLevels->level[item_trh_GetMinLevel(pItem) - 1]);
			else
				item_trh_SetAlgoPropsLevel(pItem, pAlgoItemLevels->level[pItemDef->iLevel-1]);
		}
	}
	
	// Fixup Power levels
	for(i=eaSize(&pItem->ppPowers)-1; i>=0; i--)
	{
		NOCONST(Power)* ppow = pItem->ppPowers[i];
		ppow->iLevel = item_trh_GetLevel(pItem);
	}

	//Check to see if optional structs can be freed.
	item_trh_FixupAlgoProps(pItem);
	item_trh_FixupSpecialProps(pItem);
#endif
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Itemidmax, .Psaved.Ppallowedcritterpets, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Playertype, .pInventoryV2.ppLiteBags[]");
void inv_trh_UpdateItemSets(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, const ItemChangeReason *pReason)
{
	S32 iBag, iSlot, iBagItemSet = -1;
	ItemDef **ppItemSets = NULL;
	S32 *piItemSetCount = NULL;

	if(!g_bItemSets || ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return;

	for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag >= 0; iBag--)
	{
		NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];

		if(pBag->BagID==InvBagIDs_ItemSet)
		{
			iBagItemSet = iBag;
			continue;
		}

		if(!(invbag_trh_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag)))
			continue;

		for (iSlot = eaSize(&pBag->ppIndexedInventorySlots)-1; iSlot >= 0; iSlot--)
		{
			NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[iSlot];
			if(pSlot->pItem)
			{
				ItemDef *pItemSet = NULL;
				ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);
				if(pItemDef)
				{
					S32 iItemSet;
					for (iItemSet = eaSize(&pItemDef->ppItemSets)-1; iItemSet >= 0; iItemSet--)
					{
						pItemSet = GET_REF(pItemDef->ppItemSets[iItemSet]->hDef);
						if(pItemSet)
						{
							S32 iSet = eaFind(&ppItemSets,pItemSet);
							if(iSet>=0)
							{
								ANALYSIS_ASSUME(piItemSetCount && piItemSetCount[iSet]);
								piItemSetCount[iSet]++;
							}
							else
							{
								eaPush(&ppItemSets,pItemSet);
								eaiPush(&piItemSetCount,1);
							}
						}
					}
				}
			}
		}
	}

	if(iBagItemSet>=0)
	{
		NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[iBagItemSet];
		S32 *piItemSetMap = NULL;
		S32 iSets = eaSize(&ppItemSets);
		if(!iSets)
		{
			inv_bag_trh_ClearBag(ATR_PASS_ARGS, pEnt->pInventoryV2->ppInventoryBags[iBagItemSet]);
		}
		else
		{
			ItemDef **ppItemDefsAdd = NULL;
			S32 iSet, iSlots = eaSize(&pBag->ppIndexedInventorySlots);
			S32 bRemoved = false;
			eaiSetSize(&piItemSetMap,iSlots); // Map is full of 0's, which will use to indicate unmapped
			for(iSet=0; iSet<iSets; iSet++)
			{
				ItemDef *pSet = ppItemSets[iSet];
				S32 iCount = piItemSetCount[iSet];

				for(iSlot=iSlots-1; iSlot>=0; iSlot--)
				{
					if(!piItemSetMap[iSlot]
						&& pBag->ppIndexedInventorySlots[iSlot]->pItem
						&& GET_REF(pBag->ppIndexedInventorySlots[iSlot]->pItem->hItem)==pSet)
					{
						piItemSetMap[iSlot] = iSet + 1; // Map the slot to the ItemSet (add one so 0 means unmapped)
						break;
					}
				}

				if(iSlot<0)
				{
					// Didn't find an existing Item, toss the ItemDef into the list to add
					eaPush(&ppItemDefsAdd,pSet);
				}
			}

			// Remove the Items that aren't mapped to a set anymore
			for(iSlot=iSlots-1; iSlot>=0; iSlot--)
			{
				if(!piItemSetMap[iSlot])
				{
					// Direct destroy and cleanup, rather than calling RemoveItem
					NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];
					if(pSlot->pItem)
					{
						StructDestroyNoConst(parse_Item, pSlot->pItem);
						pSlot->pItem = NULL;

						bRemoved = true;
					}
				}
			}

			// Add Items from ppItemDefsAdd
			for(iSet=0; iSet<eaSize(&ppItemDefsAdd); iSet++)
			{
				// Direct create and add, rather than calling AddItemFromDef
				NOCONST(Item)* pItem = inv_ItemInstanceFromDefName(ppItemDefsAdd[iSet]->pchName, 0, 0, NULL, NULL, NULL, false, NULL);
				if(pItem)
				{
					inv_trh_InitAndFixupItem(ATR_PASS_ARGS, pEnt, pItem, true, pReason);
 					if (inv_bag_trh_AddItemFast(ATR_PASS_ARGS, pEnt, NULL, pBag, -1, pItem, 1, ItemAdd_Silent, NULL, pReason) >= 0)
					{
						item_trh_FixupPowerIDs(pEnt, pItem);
					}
				}
			}

			if(bRemoved)
				inv_trh_CollapseBag(ATR_PASS_ARGS, pBag);

			eaDestroy(&ppItemDefsAdd);
			eaiDestroy(&piItemSetMap);
		}

#ifdef GAMESERVER
		// Post-processing necessary when ItemSet state may have changed
		QueueRemoteCommand_RemoteUpdateItemSetsPost(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);
#endif
	}

	eaDestroy(&ppItemSets);
	eaiDestroy(&piItemSetCount);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
void inv_trh_ClearItemIDs(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	int iBag, iSlot;
	if (ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return;

	for (iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag >= 0; iBag--)
	{
		NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];

		for (iSlot = eaSize(&pBag->ppIndexedInventorySlots)-1; iSlot >= 0; iSlot--)
		{
			NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[iSlot];
			if (NONNULL(pSlot->pItem))
			{
				pSlot->pItem->id = 0;
			}
		}
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOwner, ".Pplayer.Skilltype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Eskillspecialization, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pEnt, ".Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pchar.Hclass, .Hallegiance, .Hsuballegiance")
	ATR_LOCKS(pItem, ".Flags, .Hitem, .Palgoprops.Minlevel_Useaccessor")
	ATR_LOCKS(pBag, ".Inv_Def, .Bagid");
S32 inv_trh_ItemEquipValid(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem, ATH_ARG NOCONST(InventoryBag) *pBag, int iSlot)
{
	ItemDef *pItemDef = NULL;
	if(ISNULL(pItem) || ISNULL(pBag))
		return false;

	pItemDef = GET_REF(pItem->hItem);

	if(ISNULL(pItemDef))
		return false;

	if ((invbag_trh_flags(pBag) & InvBagFlag_DeviceBag) && pItemDef->eType != kItemType_Device)
	{
		return false;
	}
	else
	{
		bool bIsWeaponBag = ((invbag_trh_flags(pBag) & InvBagFlag_WeaponBag) || (invbag_trh_flags(pBag) & InvBagFlag_ActiveWeaponBag));
		bool bIsEquipBag = invbag_trh_flags(pBag) & InvBagFlag_EquipBag;
		if (bIsWeaponBag || bIsEquipBag)
		{
			//If the item changed from primary to secondary or vice versa
			if (item_IsSecondaryEquip(pItemDef) && iSlot == 0)
			{
				return false;
			}
			else if(item_IsPrimaryEquip(pItemDef) && iSlot != 0)
			{
				return false;
			}

			if (pItemDef->eType == kItemType_Weapon)
			{
				if (!bIsWeaponBag)
				{
					return false;
				}
			}
			else if (pItemDef->eType == kItemType_Upgrade)
			{
				if (!bIsEquipBag)
				{
					return false;
				}
			}
		}
	}
	// Fail if the item has restrict bags and the destination bag ID doesn't match one of those IDs
	if (eaiSize(&pItemDef->peRestrictBagIDs) && eaiFind(&pItemDef->peRestrictBagIDs, invbag_trh_bagid(pBag)) < 0)
	{
		return false;
	}
	// Check usage restrictions
	if (!itemdef_trh_VerifyUsageRestrictions(ATR_PASS_ARGS, pOwner, pEnt, pItemDef, item_trh_GetMinLevel(pItem), NULL))
	{
		return false;
	}
	// Special check for items slotted on assignments
	if ((pItem->flags & kItemFlag_SlottedOnAssignment) && !ItemAssignment_trh_CanSlottedItemResideInBag(pBag))
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOwner, ".Pplayer.Skilltype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Eskillspecialization, pInventoryV2.ppLiteBags[], .Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]" )
	ATR_LOCKS(pEnt, ".Pchar.Ppsecondarypaths, .Pchar.Hpath, .pInventoryV2.Ppinventorybags, .Pchar.Hclass, .Hallegiance, .Hsuballegiance, .Pplayer.Playertype, .Psaved.Ppallowedcritterpets, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, pInventoryV2.ppLiteBags[]");
void inv_trh_FixupEquipBags(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity)* pEnt, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract, bool bAllowOwnerChange)
{
	int NumBags,iBag;

	if (ISNULL(pEnt) || ISNULL(pOwner) || ISNULL(pOwner->pPlayer))
		return;

	if (ISNULL(pEnt->pInventoryV2))
		return;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	for(iBag=0; iBag<NumBags; ++iBag)
	{
		int NumSlots,iSlot;
		S32 iMaxSlots;
		bool bReCheck;
		S32 iTotalChecks = 0;

		do 
		{
			NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];
			bReCheck = false;
			++iTotalChecks;		// if too many checks are done then something has gone wrong, this will prevent it from hanging. 

			// Work on any bag that the player can equip to
			if(!(invbag_trh_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag)))
				continue;

			NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
			iMaxSlots = invbag_trh_maxslots(pEnt, pBag);
			for(iSlot = NumSlots -1; iSlot >= 0; --iSlot)
			{
				NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];

				if ( NONNULL(pSlot->pItem) )
				{
					S32 bValid = true;
					ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);
					ItemDefFlag eItemDefFlags = SAFE_MEMBER(pItemDef, flags);

					// items without a def should be ignored
					if(!pItemDef)
					{
						continue;
					}

					//If this item doesn't belong here anymore, remove it (into the overflow bag)
					// Do not touch ItemDefs flagged as LockToRestrictBags, as they cannot go into the overflow bag
					if(!(eItemDefFlags & kItemDefFlag_LockToRestrictBags) &&
						item_GetIDTypeFromID(pSlot->pItem->id) != kItemIDType_Critter &&
						(!inv_trh_ItemEquipValid(ATR_PASS_ARGS, pOwner, pEnt, pSlot->pItem, pBag, iSlot) ||
						!inv_ent_trh_EquipLimitCheck(ATR_PASS_ARGS, pEnt, pBag, pItemDef, pBag->BagID, iSlot, NULL, NULL)) ||
						(iMaxSlots > 0 && iSlot >= iMaxSlots))
					{
						// move to the owners overflow bag (i.e. player ent)
						NOCONST(InventoryBag)* pOverflowBag = inv_trh_GetBag(ATR_PASS_ARGS, pOwner, 33 /* Literal InvBagIDs_Overflow */, pExtract);

						// This check is due to the fact that the there all call to this function that are just tests
						// Only allow an actual transaction to modify the owner. A test will just delete the item which is fine as diff on the container will still show a change
						if(bAllowOwnerChange)
						{
							inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pOwner, NULL, pOverflowBag, -1, pEnt, NULL,  pBag, iSlot, -1, false, false, pReason, pReason, pExtract);
						}
						else
						{
							NOCONST(Item) *pRemItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, true, pBag, iSlot, pSlot->pItem->count, pReason);
							StructDestroyNoConstSafe(parse_Item, &pRemItem);
						}
						//NOTIFY the user their item got moved into the overflow bag?
						bReCheck = true;
						break;
					}

					//Unlock any costumes on the items that the player has equipped
					if(pSlot->pItem->flags & kItemFlag_Bound && 
						((pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock && eaSize(&pItemDef->ppCostumes)) || 
						((pSlot->pItem->flags & kItemFlag_Algo) && pSlot->pItem->pSpecialProps && GET_REF(pSlot->pItem->pSpecialProps->hCostumeRef))) )
					{
						inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, false, pSlot->pItem,pExtract);
						if(pItemDef->bDeleteAfterUnlock)
						{
							NOCONST(Item) *pRemItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, true, pBag, iSlot, pSlot->pItem->count, pReason);
							StructDestroyNoConst(parse_Item, pRemItem);
							bReCheck = true;
							break;
						}
					}
				}
			}
		}while(bReCheck && iTotalChecks < 10);
	}
}

//fixup routines
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pinventoryv2");
bool inv_ent_trh_AddInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if ( ISNULL(pEnt))
		return false;


	if ( ISNULL(pEnt->pInventoryV2) )
	{
		pEnt->pInventoryV2 = StructCreateNoConst(parse_Inventory);
	}

	if (NONNULL(pEnt->pInventoryV2))
		return true;
	else
		return false;
}

//initialize inventory bags and do a general fixup pass
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Pscpdata, .Pchar.Ilevelexp, .Psaved.Ppbuilds, .Pinventoryv2, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Pplayer.Playertype");
bool inv_ent_trh_InitAndFixupInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, DefaultInventory *inv_template, bool bAddNoCopyBags, bool bFixItemIDs, const ItemChangeReason *pReason)
{
	int NumBags;
	int ii;
	NOCONST(InventoryBag)* bag = NULL;

	if (ISNULL(inv_template))
	{
		return false;
	}

	//add the inventory if it does not exist
	if ( !inv_ent_trh_AddInventory(ATR_PASS_ARGS, pEnt) )
	{
		return false;
	}

	// get number of bags for this class
    NumBags = eaSize(&inv_template->InventoryBags);
    SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict,inv_template,pEnt->pInventoryV2->inv_def);

	// loop for all bags specified on class
	for (ii=0; ii<NumBags; ii++)
	{
		InvBagDef* bag_template = inv_template->InventoryBags[ii];
        
		if ( ISNULL(bag_template)  )
            continue;

		if(!bAddNoCopyBags && bag_template->flags & InvBagFlag_NoCopy)
			continue;
        if (bag_template->eType == InvBagType_ItemLite)
		{
			NOCONST(InventoryBagLite)* pLiteBag = NULL;
			pLiteBag = inv_trh_GetLiteBag( ATR_PASS_ARGS, pEnt, bag_template->BagID, NULL );	
			if(ISNULL(pLiteBag))
			{
				pLiteBag = inv_ent_trh_AddLiteBagFromDef(ATR_PASS_ARGS, pEnt, bag_template);
			}
			SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict,inv_template,pLiteBag->inv_def);
		}
		else
		{
			bag = inv_trh_GetBag( ATR_PASS_ARGS, pEnt, bag_template->BagID, NULL );	
			if(ISNULL(bag))
			{
				bag = inv_ent_trh_AddBagFromDef(ATR_PASS_ARGS, pEnt, bag_template);
			}
			SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict,inv_template,bag->inv_def);
			// passing in NULL for pExtract which will prevent game permission check (we want all possible bags regardless of permissions)

			// ab: right now it isn't valid to have additional slots in 
			//     anything but bank and player bags. should be an error
			//     at some point.
			// MK: inventory can now specify additional slots
			if(!INRANGE(bag->BagID,InvBagIDs_PlayerBags,InvBagIDs_Loot) && bag->BagID != InvBagIDs_Inventory)
			{
				bag->n_additional_slots = 0;
			}

			if(bag->BagID==InvBagIDs_Buyback)
			{
				inv_bag_trh_ClearBag(ATR_PASS_ARGS, bag);
			}

		}
    }

	//update the level
	// If this Entity doesn't have a pChar, it probably doesn't need a Level
	if (NONNULL(pEnt->pChar))
	{
        int iLevel;

        // Set level here if no level
		iLevel = entity_trh_CalculateExpLevelSlow(pEnt, false);
		inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pEnt, true, "Level", iLevel, pReason );

		// Update saved level value
		pEnt->pChar->iLevelExp = iLevel;
	}


	//fixup items
	inv_trh_FixupInventory(ATR_PASS_ARGS, pEnt, bFixItemIDs, pReason);

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Earanks[AO]")
ATR_LOCKS(pGuildBank, ".Pinventoryv2, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool inv_guildbank_trh_InitializeInventory(ATR_ARGS, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank, DefaultInventory *inv_template)
{
	S32 i, j;
	S32 num_bags;
	NOCONST(InventoryBag) *bag;
	
	pGuildBank->myEntityType = GLOBALTYPE_ENTITYGUILDBANK;

	if (ISNULL(pGuildBank->pInventoryV2)) {
		pGuildBank->pInventoryV2 = StructCreateNoConst(parse_Inventory);
	}
	
	if (ISNULL(pGuildBank->pInventoryV2) || ISNULL(inv_template)) {
		TRANSACTION_APPEND_LOG_FAILURE("Could not create guild inventory.");
		return false;
	}
	
	SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict,inv_template,pGuildBank->pInventoryV2->inv_def);
	num_bags = eaSize(&inv_template->InventoryBags);
	for (i = 0; i < num_bags; i++) 
	{
		InvBagDef* bag_template = inv_template->InventoryBags[i];
		
		if (ISNULL(bag_template))
			continue;
		

		if (bag_template->eType == InvBagType_ItemLite)
		{
			NOCONST(InventoryBagLite)* pLiteBag = NULL;
			pLiteBag = inv_trh_GetLiteBag( ATR_PASS_ARGS, pGuildBank, bag_template->BagID, NULL );	
			if(ISNULL(pLiteBag))
			{
				pLiteBag = inv_ent_trh_AddLiteBagFromDef(ATR_PASS_ARGS, pGuildBank, bag_template);
			}
			SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict,inv_template,pLiteBag->inv_def);
			pLiteBag->pGuildBankInfo = StructAllocNoConst(parse_GuildBankTabInfo);
			for (j = 0; j < eaSize(&pGuild->eaRanks); j++) {
				eaPush(&pLiteBag->pGuildBankInfo->eaPermissions, StructAllocNoConst(parse_GuildBankTabPermission));
				pLiteBag->pGuildBankInfo->eaPermissions[j]->ePerms = j < eaSize(&pGuild->eaRanks) - 1 ? 0 : GuildPermission_Deposit | GuildPermission_Withdraw;
				pLiteBag->pGuildBankInfo->eaPermissions[j]->iWithdrawLimit = 0;
				pLiteBag->pGuildBankInfo->eaPermissions[j]->iWithdrawItemCountLimit = 0;
			}
		}
		else
		{
			bag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, bag_template->BagID);

			if (ISNULL(bag))
			{
				bag = StructCreateNoConst(parse_InventoryBag);
				bag->BagID = bag_template->BagID;
				eaIndexedAdd(&pGuildBank->pInventoryV2->ppInventoryBags, bag);
			}
			SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict,inv_template,bag->inv_def);

			if (invbag_trh_flags(bag) & InvBagFlag_GuildBankBag) {
				bag->pGuildBankInfo = StructAllocNoConst(parse_GuildBankTabInfo);
				for (j = 0; j < eaSize(&pGuild->eaRanks); j++) {
					eaPush(&bag->pGuildBankInfo->eaPermissions, StructAllocNoConst(parse_GuildBankTabPermission));
					bag->pGuildBankInfo->eaPermissions[j]->ePerms = j < eaSize(&pGuild->eaRanks) - 1 ? 0 : GuildPermission_Deposit | GuildPermission_Withdraw;
					bag->pGuildBankInfo->eaPermissions[j]->iWithdrawLimit = 0;
					bag->pGuildBankInfo->eaPermissions[j]->iWithdrawItemCountLimit = 0;
				}
			}
		}
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, .pInventoryV2.peaowneduniqueitems");
void inv_ent_trh_AddInventoryItemsFromDefaultItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, DefaultItemDef ***peaDefaultItemList, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (peaDefaultItemList)
	{
		int i;
		for (i = 0;i < eaSize(peaDefaultItemList);i++)
		{
			DefaultItemDef *pDefaultItemDef = (*peaDefaultItemList)[i];
			ItemDef* pItemDef = GET_REF(pDefaultItemDef->hItem);

			if ( !pItemDef)
				continue;

			if ( pItemDef->eType == kItemType_Numeric )
			{
				inv_ent_trh_SetNumeric(ATR_PASS_ARGS,pEnt,true,pItemDef->pchName,pDefaultItemDef->iCount, pReason);
			}
			else
			{
				inv_ent_trh_AddItemFromDef(ATR_PASS_ARGS,pEnt,NULL,pDefaultItemDef->eBagID,-1,pItemDef->pchName,pDefaultItemDef->iCount, 0, 0, ItemAdd_Silent, pReason, pExtract);
			}
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems");
void inv_ent_trh_AddInventoryItemsFromInvSet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, DefaultInventory *pInventory, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (pInventory->GrantedItems)
		inv_ent_trh_AddInventoryItemsFromDefaultItemList(ATR_PASS_ARGS, pEnt, &pInventory->GrantedItems, pReason, pExtract);
}

//this is called in a transaction when a player entity is created and when it is loaded
//verify that the inventory data is valid and fix it up if it is not
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Pscpdata, .Pchar.Hclass, .Pchar.Ilevelexp, .Psaved.Ppbuilds, .Pinventoryv2, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Pplayer.Playertype");
bool inv_ent_trh_VerifyInventoryData(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bAddNoCopyBags, bool bFixupItemIDs, const ItemChangeReason *pReason)
{
	if(NONNULL(pEnt->pChar))
	{
		CharacterClass *pClass = GET_REF(pEnt->pChar->hClass);
		if(pClass)
		{
			DefaultInventory *pInventory = GET_REF(pClass->hInventorySet);
			if(pInventory)
			{
				inv_ent_trh_InitAndFixupInventory(ATR_PASS_ARGS, pEnt, pInventory, bAddNoCopyBags, bFixupItemIDs, pReason);
				return true;
			}
		}
	}

	if(NONNULL(pEnt->pPlayer)){
		inv_ent_trh_InitAndFixupInventory(ATR_PASS_ARGS, pEnt, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict,"PlayerDefault"),bAddNoCopyBags, bFixupItemIDs, pReason);
		return true;
	} else if (NONNULL(pEnt->pNemesis)){
		inv_ent_trh_InitAndFixupInventory(ATR_PASS_ARGS, pEnt, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict,"Nemesis"),bAddNoCopyBags, bFixupItemIDs, pReason);
		return true;
	}
	return false;
}

//this should only be called upon the first init of any entity. This will give all the items
//in the inventory structure to the entity, and equip them if asked to
//InitializeInventoryData MUST be called before this
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Itemidmax, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems");
void inv_ent_trh_AddInventoryItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	bool bItemsAdded = false;
	if(pEnt->pChar)
	{
		CharacterClass *pClass = GET_REF(pEnt->pChar->hClass);
		CharacterPath** eaPaths = NULL;
		int i;

		if(pClass)
		{
			DefaultInventory *pInventory = GET_REF(pClass->hInventorySet);

			if(pInventory)
			{
				inv_ent_trh_AddInventoryItemsFromInvSet(ATR_PASS_ARGS, pEnt, pInventory, pReason, pExtract);
				bItemsAdded = true;
			}
		}

		eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);

		entity_trh_GetChosenCharacterPaths(pEnt, &eaPaths);

		// Add all inventory items based on character path
		for (i = 0; i < eaSize(&eaPaths); i++)
		{
			inv_ent_trh_AddInventoryItemsFromDefaultItemList(ATR_PASS_ARGS, pEnt, &eaPaths[i]->eaDefaultItems, pReason, pExtract);
			bItemsAdded = true;
		}
	}
	if(!ISNULL(pEnt->pPlayer) && !bItemsAdded)
		inv_ent_trh_AddInventoryItemsFromInvSet(ATR_PASS_ARGS, pEnt,(DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict,"PlayerDefault"), pReason, pExtract);
}

//routines used to initialize inventory
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that make lists of things in bags

typedef struct GetItemListData
{
	const char *ItemDefName;
	ItemCategory eCategory;
	ItemDef* pFindItemDef; //added for perf, compare ref pointers instead of strcmp on name
	InventoryList *pInventoryList;
	U64 uiItemId;
}GetItemListData;

// AB: this is pretty bad lock-wise, should be replaced with iterators at some point
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Id")
ATR_LOCKS(pBag, ".*")
ATR_LOCKS(pSlot, ".*");
void GetItemListCallback(ATH_ARG NOCONST(Item)* pItem, ItemDef *pItemDef, ATH_ARG NOCONST(InventoryBag)* pBag, ATH_ARG NOCONST(InventorySlot)* pSlot, void* pData)
{
	GetItemListData *pGetItemListData = (GetItemListData*)pData;
	InventoryListEntry *pListEntry = NULL;


	if ( pGetItemListData->pInventoryList->count < 0 ||
		 pGetItemListData->pInventoryList->count >= NUM_INVENTORY_LIST_ENTRIES )
	{
		assertmsg(0, "INVENTORY_LIST overflow");
	}

	pListEntry = &pGetItemListData->pInventoryList->Entry[pGetItemListData->pInventoryList->count];


	//verify match pointers
	if (ISNULL(pBag) || ISNULL(pSlot) || ISNULL(pGetItemListData))
		return;

	if ( pGetItemListData->ItemDefName &&
		 (pGetItemListData->ItemDefName[0]) )
	{
		bool matched = false;

		if ( NONNULL(pItemDef) )
		{
			if (NONNULL(pGetItemListData->pFindItemDef))
			{
				//if we have a ref to match use that instead of slow name compare
				if (pItemDef == pGetItemListData->pFindItemDef)
					matched = true;
			}
			else
			{
				//source ref is NULL but name is not, do a name compare
				//should never execute
				if ( pItemDef->pchName &&
					 (stricmp(pItemDef->pchName,pGetItemListData->ItemDefName)==0) )
					 matched = true;
			}
		}

		if ( matched )
		{
			//name matched
			pListEntry->pBag = (InventoryBag*)pBag;
			pListEntry->pSlot = (InventorySlot*)pSlot;

			if ( pGetItemListData->pInventoryList->count < NUM_INVENTORY_LIST_ENTRIES-1 )
				pGetItemListData->pInventoryList->count++;
			else
				assertmsg(0, "INVENTORY_LIST overflow");
		}
	}
	else if (pGetItemListData->uiItemId)
	{
		if (NONNULL(pItem) && pGetItemListData->uiItemId == pItem->id)
		{
			// item id matched
			pListEntry->pBag = (InventoryBag*)pBag;
			pListEntry->pSlot = (InventorySlot*)pSlot;

			if (pGetItemListData->pInventoryList->count < NUM_INVENTORY_LIST_ENTRIES - 1)
				pGetItemListData->pInventoryList->count++;
			else
				assertmsg(0, "INVENTORY_LIST overflow");
		}
	}
	else if (pGetItemListData->eCategory > kItemCategory_None)
	{
		ItemCategory eCat = pGetItemListData->eCategory;
		if (NONNULL(pItem) && ea32Find((U32**)&pItemDef->peCategories, eCat) >= 0)
		{
			//category matched
			pListEntry->pBag = (InventoryBag*)pBag;
			pListEntry->pSlot = (InventorySlot*)pSlot;
			
			if (pGetItemListData->pInventoryList->count < NUM_INVENTORY_LIST_ENTRIES - 1)
				pGetItemListData->pInventoryList->count++;
			else
				assertmsg(0, "INVENTORY_LIST overflow");
		}
	}
	else
	{
		//no match name specified, just add all items
		pListEntry->pBag = (InventoryBag*)pBag;
		pListEntry->pSlot = (InventorySlot*)pSlot;
		if ( pGetItemListData->pInventoryList->count < NUM_INVENTORY_LIST_ENTRIES-1 )
			pGetItemListData->pInventoryList->count++;
		else
			assertmsg(0, "INVENTORY_LIST overflow");
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
int inv_bag_trh_GetItemListEx(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, const char *ItemDefName, ItemCategory eCategory, InventoryList *pInventoryList)
{
	if  ( ItemDefName && ItemDefName[0] && (invbag_trh_flags(pBag) & InvBagFlag_NameIndexed) )
	{
		//looking for a specific item in a name indexed bag
		//do a quick lookup
		InventoryListEntry *pListEntry = NULL;
		NOCONST(InventorySlot) *pSlot = eaIndexedGetUsingString(&pBag->ppIndexedInventorySlots, ItemDefName);

		if (NONNULL(pSlot))
		{
			if ( pInventoryList->count < 0 ||
				pInventoryList->count >= NUM_INVENTORY_LIST_ENTRIES )
			{
				assertmsg(0, "INVENTORY_LIST overflow");
			}

			pListEntry = &pInventoryList->Entry[pInventoryList->count];

			//no match name specified, just add all items
			pListEntry->pBag = (InventoryBag*)pBag;
			pListEntry->pSlot = (InventorySlot*)pSlot;
			if ( pInventoryList->count < NUM_INVENTORY_LIST_ENTRIES-1 )
				pInventoryList->count++;
			else
				assertmsg(0, "INVENTORY_LIST overflow");
		}
	}
	else
	{
		//bag is not name indexed or did not specify a name, must iterate through all items in bag

		GetItemListData data = {0};

		data.ItemDefName = ItemDefName; // this will be NULL or string is empty
		data.eCategory = eCategory;
		data.pFindItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, ItemDefName);
		data.pInventoryList = pInventoryList;

		//pInventoryList->count = 0;
		inv_bag_trh_ForEachItemInBag(ATR_PASS_ARGS, pBag, GetItemListCallback, &data);
	}

	return pInventoryList->count;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
int inv_bag_trh_GetItemList( ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, const char *ItemDefName, InventoryList *pInventoryList )
{
	return inv_bag_trh_GetItemListEx(ATR_PASS_ARGS, pBag, ItemDefName, -1, pInventoryList);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_GetItemList( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char *ItemDefName, InventoryList *pInventoryList, GameAccountDataExtract *pExtract )
{
	NOCONST(InventoryBag)* pBag;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);

	if (ISNULL(pBag))
		return 0;

	return inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, ItemDefName, pInventoryList);
}

/* This function has been removed until further notice due to the high chance it will trigger an assert by clearing NUM_INVENTORY_LIST_ENTRIES
AUTO_TRANS_HELPER;
int inv_ent_trh_AllBagsGetItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char *ItemDefName, InventoryList *pInventoryList)
{
	int NumBags,ii;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;


	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	for(ii=0;ii<NumBags;ii++)
	{
		inv_bag_trh_GetItemList(ATR_PASS_ARGS, bSilent, pEnt->pInventoryV2->ppInventoryBags[ii], ItemDefName, pInventoryList);
	}

	return pInventoryList->count;
}
*/

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
int inv_ent_trh_GetBagList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InventoryList *pInventoryList)
{
	int NumBags,ii;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return 0;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	for(ii=0;ii<NumBags;ii++)
	{
		pInventoryList->Entry[ii].pBag = (InventoryBag*)pEnt->pInventoryV2->ppInventoryBags[ii];
	}

	pInventoryList->count = NumBags;
	return NumBags;
}


//routines that make lists of things in bags
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that make simple lists of items in bags

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
int inv_bag_trh_GetSimpleItemList(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, Item ***pppItemList, bool bCopyStructs, S32 iReUseArrayElementStartIndex)
{
	InventoryList tmpInventoryList = {0};
	int SlotCount = 0;
	int ItemCount = 0;
	int ii;
	S32 iReUseArrayElementIndex = iReUseArrayElementStartIndex;

	SlotCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, NULL, &tmpInventoryList);

	for(ii=0; ii<SlotCount; ii++)
	{
		InventoryListEntry *pEntry = &tmpInventoryList.Entry[ii];

		if (ISNULL(pEntry->pSlot->pItem))
			continue;

		if (iReUseArrayElementStartIndex >= 0)
		{
			if (bCopyStructs)
			{
				Item *pItemCopy = eaGetStruct(pppItemList, parse_Item, iReUseArrayElementIndex++);
				StructCopyAll(parse_Item, pEntry->pSlot->pItem, pItemCopy);
			}
			else
			{
				if (iReUseArrayElementIndex < eaSize(pppItemList))
				{
					(*pppItemList)[iReUseArrayElementIndex] = pEntry->pSlot->pItem;
				}
				else
				{
					eaPush(pppItemList, pEntry->pSlot->pItem);
				}
				++iReUseArrayElementIndex;
			}
		}
		else
		{
			Item *pItemCopy = bCopyStructs ? StructClone(parse_Item, pEntry->pSlot->pItem) : pEntry->pSlot->pItem;
			eaPush(pppItemList, (Item*)pItemCopy);
		}			
		ItemCount++;
	}

	return ItemCount;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_GetSimpleItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, Item ***pppItemList, bool bCopyStructs, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return 0;

	return inv_bag_trh_GetSimpleItemList(ATR_PASS_ARGS, pBag, pppItemList, bCopyStructs, -1);
}

//routines that make simple lists of items in bags
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines that count things in Bags

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
int inv_bag_trh_CountItems( ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, const char *ItemDefName, ItemCategory eCategory )
{
	InventoryList tmpInventoryList = {0};
	int SlotCount = 0;
	int ItemCount = 0;
	int ii;

	PERFINFO_AUTO_START_FUNC();

	SlotCount = inv_bag_trh_GetItemListEx(ATR_PASS_ARGS, pBag, ItemDefName, eCategory, &tmpInventoryList);

	for(ii=0; ii<SlotCount; ii++)
	{
		InventoryListEntry *pEntry = &tmpInventoryList.Entry[ii];

		// Ignore count if slot type is empty
		if(pEntry->pSlot && NONNULL(pEntry->pSlot->pItem))
		{
			ItemDef* pDef = GET_REF(pEntry->pSlot->pItem->hItem);
			if (pDef && (pDef->eType == kItemType_Numeric))//numerics with a value of 0 or less can still be relevant due to their numericop
				ItemCount += max(1, pEntry->pSlot->pItem->count);
			else
				ItemCount += pEntry->pSlot->pItem->count;
		}
	}

	PERFINFO_AUTO_STOP();

	return ItemCount;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pBag, ".*");
int inv_lite_trh_CountItems( ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char *ItemDefName )
{
	if (ItemDefName)
	{
		NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, ItemDefName);
		return pSlot ? pSlot->count : 0;
	}
	else
	{
		int count = 0;
		BagIterator* pIter = invbag_IteratorFromLiteBag(pBag);
		while (!bagiterator_Stopped(pIter))
		{
			count += bagiterator_GetItemCount(pIter);
			bagiterator_Next(pIter);
		}
		bagiterator_Destroy(pIter);
		return count;
	}
}

AUTO_TRANS_HELPER;
int inv_ent_trh_CountItemList( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char ***peaItemNameList, GameAccountDataExtract *pExtract )
{
	NOCONST(InventoryBag)* pBag = NULL;
	int sum = 0;
	int i = 0;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return 0;

	for (i = 0; i < eaSize(peaItemNameList); i++)
	{
		sum += inv_bag_trh_CountItems(ATR_PASS_ARGS, pBag, (*peaItemNameList)[i], -1);
	}
	return sum;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_CountItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char *ItemDefName, ItemCategory eCategory, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
	{

		NOCONST(InventoryBagLite)* pLiteBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
		
		if (ISNULL(pLiteBag))
			return 0;

		return inv_lite_trh_CountItems(ATR_PASS_ARGS, pLiteBag, ItemDefName);
	}

	return inv_bag_trh_CountItems(ATR_PASS_ARGS, pBag, ItemDefName, eCategory);
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Pinventoryv2.Pplitebags");
int inv_ent_trh_AllBagsCountItems( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char *ItemDefName, InvBagIDs* peExcludeBags )
{
	int NumBags,ItemCount,ii;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;


	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	ItemCount = 0;
	for(ii=0;ii<NumBags;ii++)
	{
		NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[ii];
		
		if (peExcludeBags && eaiFind(&peExcludeBags, pBag->BagID) >= 0)
			continue;

		ItemCount += inv_bag_trh_CountItems(ATR_PASS_ARGS, pBag, ItemDefName, -1);
	}

	NumBags = eaSize(&pEnt->pInventoryV2->ppLiteBags);
	for(ii=0;ii<NumBags;ii++)
	{
		NOCONST(InventoryBagLite)* pBag = pEnt->pInventoryV2->ppLiteBags[ii];

		if (peExcludeBags && eaiFind(&peExcludeBags, pBag->BagID) >= 0)
			continue;

		ItemCount += inv_lite_trh_CountItems(ATR_PASS_ARGS, pBag, ItemDefName);
	}

	return ItemCount;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Pinventoryv2.Pplitebags");
bool inv_ent_trh_AllBagsCountItemsAtLeast( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char *ItemDefName, InvBagIDs* peExcludeBags, unsigned int uiAtLeast )
{
	unsigned int NumBags,ItemCount,ii;

	if ( ISNULL(pEnt))
		return false;

	if ( ISNULL(pEnt->pInventoryV2))
		return false;


	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	ItemCount = 0;
	for(ii=0;ii<NumBags && ItemCount < uiAtLeast;ii++)
	{
		NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[ii];

		if (peExcludeBags && eaiFind(&peExcludeBags, pBag->BagID) >= 0)
			continue;

		ItemCount += inv_bag_trh_CountItems(ATR_PASS_ARGS, pBag, ItemDefName, -1);
	}

	NumBags = eaSize(&pEnt->pInventoryV2->ppLiteBags);
	for(ii=0;ii<NumBags && ItemCount < uiAtLeast;ii++)
	{
		NOCONST(InventoryBagLite)* pBag = pEnt->pInventoryV2->ppLiteBags[ii];

		if (peExcludeBags && eaiFind(&peExcludeBags, pBag->BagID) >= 0)
			continue;

		ItemCount += inv_lite_trh_CountItems(ATR_PASS_ARGS, pBag, ItemDefName);
	}

	return ItemCount >= uiAtLeast;
}


AUTO_TRANS_HELPER;
bool inv_ent_trh_EquipLimitCheck(ATR_ARGS, 
								 ATH_ARG NOCONST(Entity)* pEnt, 
								 ATH_ARG NOCONST(InventoryBag)* pDstBag, 
								 ItemDef* pItemDef, 
								 S32 iIgnoreBagID,
								 S32 iIgnoreSlot,
								 ItemEquipLimitCategoryData** ppLimitCategory,
								 S32* piEquipLimit)
{
	// Only check equip, device, weapon, and active weapon bags
	InvBagFlag eFlags = InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag;
	S32 iCount = 0;
	S32 iCategoryCount = 0;
	if (NONNULL(pEnt) && NONNULL(pEnt->pInventoryV2) && pItemDef && pItemDef->pEquipLimit &&
		(!pDstBag || (invbag_trh_flags(pDstBag) & eFlags)))
	{
		ItemEquipLimitCategory eCategory = pItemDef->pEquipLimit->eCategory;
		S32 i, iNumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
		bool bAtLimit = false;

		for (i = 0; i < iNumBags; i++)
		{
			NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[i];

			if (invbag_trh_flags(pBag) & eFlags)
			{
				BagIterator* pIter = invbag_IteratorFromBag(pBag);
				for (; !bagiterator_Stopped(pIter); bagiterator_Next(pIter))
				{
					ItemDef* pCheckDef = bagiterator_GetDef(pIter);
					ItemEquipLimitCategory eCheckCategory = SAFE_MEMBER2(pCheckDef, pEquipLimit, eCategory);

					if (iIgnoreBagID >= 0 && pBag->BagID == iIgnoreBagID && pIter->i_cur == iIgnoreSlot)
					{
						continue;
					}
					if (pItemDef == pCheckDef && pItemDef->pEquipLimit->iMaxEquipCount)
					{
						if (++iCount >= pItemDef->pEquipLimit->iMaxEquipCount)
						{
							if (piEquipLimit)
							{
								(*piEquipLimit) = iCount;
							}
							bAtLimit = true;
							break;
						}
					}
					if (eCategory && eCategory == eCheckCategory)
					{
						ItemEquipLimitCategoryData* pData = item_GetEquipLimitCategory(pItemDef->pEquipLimit->eCategory);
						if (pData && ++iCategoryCount >= pData->iMaxItemCount)
						{
							bAtLimit = true;
							if (ppLimitCategory)
							{
								(*ppLimitCategory) = pData;
							}
							if (piEquipLimit)
							{
								(*piEquipLimit) = iCategoryCount;
							}
							break;
						}
					}
				}
				bagiterator_Destroy(pIter);

				if (bAtLimit)
				{
					return false;
				}
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool inv_ent_trh_EquipLimitCheckSwap(ATR_ARGS, 
									 ATH_ARG NOCONST(Entity)* pSrcEnt,
									 ATH_ARG NOCONST(Entity)* pDstEnt,
									 ATH_ARG NOCONST(InventoryBag)* pSrcBag,
									 ATH_ARG NOCONST(InventoryBag)* pDstBag, 
									 ItemDef* pSrcItemDef,
									 ItemDef* pDstItemDef,
									 ItemEquipLimitCategoryData** ppLimitCategory,
									 S32* piEquipLimit)
{
	InvBagFlag eFlags = InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag;
	ItemEquipLimitCategory eSrcCategory = SAFE_MEMBER2(pSrcItemDef, pEquipLimit, eCategory);
	ItemEquipLimitCategory eDstCategory = SAFE_MEMBER2(pDstItemDef, pEquipLimit, eCategory);

	if ((!pSrcItemDef || !pSrcItemDef->pEquipLimit) && 
		(!pDstItemDef || !pDstItemDef->pEquipLimit))
	{
		return true;
	}
	if ((invbag_trh_flags(pSrcBag) & eFlags) && (invbag_trh_flags(pDstBag) & eFlags))
	{
		return true;
	}
	if (pSrcItemDef && pDstItemDef && (pSrcItemDef == pDstItemDef || (eSrcCategory && eSrcCategory == eDstCategory)))
	{
		return true;
	}
	if (pSrcItemDef && !inv_ent_trh_EquipLimitCheck(ATR_PASS_ARGS, pDstEnt, pDstBag, pSrcItemDef, -1, -1, ppLimitCategory, piEquipLimit))
	{
		return false;
	}
	if (pDstItemDef && !inv_ent_trh_EquipLimitCheck(ATR_PASS_ARGS, pSrcEnt, pSrcBag, pDstItemDef, -1, -1, ppLimitCategory, piEquipLimit))
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.peaOwnedUniqueItems[]");
bool inv_ent_trh_CheckUniqueItemList(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pcItemName)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pInventoryV2) && eaIndexedGetUsingString(&pEnt->pInventoryV2->peaOwnedUniqueItems, pcItemName))
	{
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pcritter.Petdef");
bool ent_trh_EntIsPetFromPetDef(ATH_ARG NOCONST(Entity)* pEnt, PetDef* pDef)
{
	return (NONNULL(pEnt) && NONNULL(pEnt->pCritter) && pDef == GET_REF(pEnt->pCritter->petDef));
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppownedcontainers, .pInventoryV2.peaOwnedUniqueItems[]")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.peaOwnedUniqueItems[]");
bool inv_ent_trh_HasUniqueItem(	ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
								ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
								 const char* pcItemName)
{
	int i;
	ItemDef* pDef = RefSystem_ReferentFromString(g_hItemDict, pcItemName);
	PetDef* pPetDef = GET_REF(pDef->hPetDef);

	if (ISNULL(pEnt) || !pDef || !(pDef->flags & kItemDefFlag_Unique))
	{
		return false;
	}
	if (inv_ent_trh_CheckUniqueItemList(ATR_PASS_ARGS, pEnt, pcItemName))
	{
		return true;
	}
	if (NONNULL(eaPets))
	{
		for (i = eaSize(&eaPets)-1; i >= 0; i--)
		{
			if (NONNULL(eaPets[i]))
			{
				if (pPetDef && pPetDef->bIsUnique)
				{
					if (ent_trh_EntIsPetFromPetDef(eaPets[i], pPetDef))
					{
						return true;
					}
				}
				if (inv_ent_trh_CheckUniqueItemList(ATR_PASS_ARGS, eaPets[i], pcItemName))
				{
					return true;
				}
			}
		}
	}
	else
	{
		if (ISNULL(pEnt->pSaved))
		{
			return false;
		}
		for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
		{
			NOCONST(PetRelationship)* pPet = pEnt->pSaved->ppOwnedContainers[i];
			// THIS IS BAD - Since this is called from inside transactions, its success
			//  requires the ObjectDB to have filled in the relevant subscriptions,
			//  which is NOT guaranteed.
			//Entity* pPetEnt = GET_REF(pPet->hPetRef);
			char idBuf[128];
			Entity *pPetEnt = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(pPet->conID, idBuf));
			if (NONNULL(pPetEnt))
			{
				if (pPetDef && pPetDef->bIsUnique && NONNULL(pPetEnt->pCritter))
				{
					if (pPetDef == GET_REF(pPetEnt->pCritter->petDef))
					{
						return true;
					}
				}
				if (inv_ent_trh_CheckUniqueItemList(ATR_PASS_ARGS, CONTAINER_NOCONST(Entity, pPetEnt), pcItemName))
				{
					return true;
				}
			}
		}	
	}
	return false;
}
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".pInventoryV2.peaOwnedUniqueItems");
void inv_ent_trh_RegisterUniqueItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt,
	const char* pcItemName,
	bool bOwned)
{
	if (ISNULL(pEnt) || !pcItemName || ISNULL(pEnt->pInventoryV2))
	{
		return;
	}

	if (!bOwned)
	{
		int index = eaIndexedFindUsingString(&pEnt->pInventoryV2->peaOwnedUniqueItems, pcItemName);
		if (index > -1)
		{
			StructDestroyNoConst(parse_OwnedUniqueItem, pEnt->pInventoryV2->peaOwnedUniqueItems[index]);
			eaRemove(&pEnt->pInventoryV2->peaOwnedUniqueItems, index);
		}
	}
	else
	{
		NOCONST(OwnedUniqueItem)* pOwned = StructCreateNoConst(parse_OwnedUniqueItem);
		pOwned->pchName = allocAddString(pcItemName);
		if (!pEnt->pInventoryV2->peaOwnedUniqueItems)
			eaIndexedEnableNoConst(&pEnt->pInventoryV2->peaOwnedUniqueItems, parse_OwnedUniqueItem);
		eaIndexedPushUsingStringIfPossible(&pEnt->pInventoryV2->peaOwnedUniqueItems, pcItemName, pOwned);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
int inv_bag_trh_CountItemSlots(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, const char *ItemDefName)
{
	InventoryList tmpInventoryList = {0};
	int SlotCount = 0;

	SlotCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, ItemDefName, &tmpInventoryList);

	return SlotCount;
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get count of items in a specified bag/slot

//get count of items in a slot
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
int inv_bag_trh_GetSlotItemCount(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx)
{
	int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	NOCONST(InventorySlot)* pSlot = NULL;

	//make sure slot is in range
	if ( (SlotIdx < 0) || ( SlotIdx+1 > NumSlots) )
		return 0;

	pSlot = pBag->ppIndexedInventorySlots[SlotIdx];

	//make sure that this slot is an Item or itemlite
	if ( ISNULL(pSlot->pItem))
	{
		return 0;
	}

	return pSlot->pItem->count;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_GetSlotItemCount(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if ( ISNULL(pEnt))
		return 0;

	if ( ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return 0;

	return inv_bag_trh_GetSlotItemCount(ATR_PASS_ARGS, pBag, SlotIdx);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Hguildbank");
int inv_guildbank_trh_GetSlotItemCount(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs iBagID, int iSlotIdx)
{
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(Entity) *pGuildBank = NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pGuild) ? CONTAINER_NOCONST(Entity, GET_REF(pEnt->pPlayer->pGuild->hGuildBank)) : NULL;

	if (ISNULL(pGuildBank)) {
		return 0;
	}

	pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, iBagID);
	if (ISNULL(pBag)) {
		return 0;
	}
	
	return inv_bag_trh_GetSlotItemCount(ATR_PASS_ARGS, pBag, iSlotIdx);
}

//routines to get count of items in a specified bag/slot
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get count Bag info


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], .pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_GetMaxSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if (ISNULL(pEnt))
		return 0;

	if (ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return 0;

	return invbag_trh_maxslots(pEnt, pBag);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Bagid, .N_Additional_Slots, .Inv_Def");
void inv_bag_trh_SetMaxSlots(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, int MaxCount)
{
    const InvBagDef *def = invbag_trh_def(pBag);
    int additional;
    int maxSlots;
	if (ISNULL(pBag) || ISNULL(def))
		return;

    maxSlots = invbag_trh_basemaxslots(pEnt, pBag);
    if(maxSlots < 0)
    {
        Errorf("trying to set bottomless bag to finite size");
        return;
    }
    
	if(!INRANGE(pBag->BagID,InvBagIDs_PlayerBags,InvBagIDs_Loot) && pBag->BagID != InvBagIDs_Inventory)
    {
        Errorf("trying to set size on an invalid bagid");
        return;
    }
    
    additional = MaxCount - MAX(maxSlots,0);
	pBag->n_additional_slots = MAX(additional,0);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, .pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
void inv_ent_trh_SetMaxSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int MaxCount, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if (ISNULL(pEnt))
		return;

	if (ISNULL(pEnt->pInventoryV2))
		return;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return;

	inv_bag_trh_SetMaxSlots( ATR_PASS_ARGS, pEnt, pBag, MaxCount );
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots[AO]");
int inv_bag_trh_GetNumSlots(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	if (ISNULL(pBag))
		return 0;

	return eaSize(&pBag->ppIndexedInventorySlots);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_GetNumSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if (ISNULL(pEnt))
		return 0;

	if (ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return 0;

	return inv_bag_trh_GetNumSlots(ATR_PASS_ARGS, pBag);
}



AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
int inv_bag_trh_AvailableSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	if (!ISNULL(pBag))
	{
		S32 iNumSlots = invbag_trh_maxslots(pEnt, pBag);
		if (iNumSlots == -1)
			return -1;
		
		iNumSlots -= inv_bag_trh_CountItemSlots(ATR_PASS_ARGS, pBag, NULL);
		return MAX(iNumSlots, 0);
	}
	
	return 0;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppliteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
int inv_ent_trh_AvailableSlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	if (ISNULL(pEnt))
		return 0;

	if (ISNULL(pEnt->pInventoryV2))
		return 0;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return 0;

	return inv_bag_trh_AvailableSlots(ATR_PASS_ARGS, pEnt, pBag);
}


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@




//routines to get count Bag info
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//routines to get info about bags or items

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], .pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
NOCONST(InventoryBag)* inv_trh_GetBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs bag_id, GameAccountDataExtract *pExtract)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return NULL;

	if(!GamePermissions_trh_CanAccessBag(pEnt, bag_id, pExtract))		
	{
		return NULL;
	}
		
	return eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, bag_id);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
NOCONST(InventoryBagLite)* inv_trh_GetLiteBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs bag_id, GameAccountDataExtract *pExtract)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return NULL;

	if(!GamePermissions_trh_CanAccessBag(pEnt, bag_id, pExtract))		
	{
		return NULL;
	}

	return eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppLiteBags, bag_id);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[]");
NOCONST(InventoryBag)* inv_guildbank_trh_GetBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, InvBagIDs BagID)
{
	
	if (ISNULL(pGuildBank) || ISNULL(pGuildBank->pInventoryV2)) {
		return NULL;
	}
	
	return eaIndexedGetUsingInt(&pGuildBank->pInventoryV2->ppInventoryBags, BagID);
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pGuildBank, ".pInventoryV2.Pplitebags[]");
NOCONST(InventoryBagLite)* inv_guildbank_trh_GetLiteBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, InvBagIDs BagID)
{

	if (ISNULL(pGuildBank) || ISNULL(pGuildBank->pInventoryV2)) {
		return NULL;
	}

	// This assumes that the only guild lite bag accessed is the numeric bag. If the guild functionality is changed so that other 
	// bags can be accessed then this function will needed to be updated. 
	return eaIndexedGetUsingInt(&pGuildBank->pInventoryV2->ppLiteBags, BagID);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Savedname, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance")
ATR_LOCKS(pGuild, ".Eamembers, .Eabanklog, .Uoldbanklogidx")
ATR_LOCKS(pGuildBank, ".pInventoryV2.ppLiteBags[], .pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
bool inv_guildbank_trh_ManageBankWithdrawLimitAndLog(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank,
	S32 iRequestedCount, NOCONST(Item) *pSrcItem,
	bool bSrcGuild, NOCONST(InventoryBag) *pSrcBag, int iSrcBagID, S32 iSrcEPValue, S32 iSrcSlot,
	bool bDstGuild, NOCONST(InventoryBag) *pDstBag, int iDstBagID, S32 iDstEPValue, S32 iDstSlot)
{
	bool bSuccess = true;

	int iSourceToDestMoveCount=0;
	int iDestToSourceMoveCount=0;
	
	NOCONST(Item) *pDstItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pDstBag, iDstSlot);

	// We need to figure out how many items we are actually going to be moving based on stacking, equipping, etc.
	inv_bag_trh_GetActualMovingCount(ATR_PASS_ARGS,
									 pDstBag, iDstSlot,
									 pSrcBag, iSrcSlot,
									 iRequestedCount,
									 &iSourceToDestMoveCount, &iDestToSourceMoveCount);

	// Note it's possible for sourceToDestCount to be 0 if we are swapping similar items where the destination is a full stack.
	
	if (!bSrcGuild || !bDstGuild || pSrcBag->BagID != pDstBag->BagID) {		// WOLF[13Dec11]: This really should be != It looks like it was miscopied in SVN 92633.
		if (bSrcGuild && NONNULL(pSrcBag) && NONNULL(pSrcItem))
		{
			if (ISNULL(pGuild) || ISNULL(pGuildBank)) {
				bSuccess = false;
			} else {
				if (iSourceToDestMoveCount>0) {				
					if (pSrcBag->BagID != pDstBag->BagID && !inv_guildbank_trh_UpdateBankTabWithdrawLimit(ATR_PASS_ARGS, pEnt->myContainerID, pGuild, pGuildBank,
																										  iSrcBagID, iSourceToDestMoveCount, iSrcEPValue)) {
						bSuccess = false;
					} else {
						inv_guild_trh_AddLog(ATR_PASS_ARGS, pEnt, pGuild, pSrcItem, NULL, pSrcBag->BagID, -iSourceToDestMoveCount);
					}
				}
				if (bSuccess && iDestToSourceMoveCount>0 && pDstItem!=NULL) {				
					inv_guild_trh_AddLog(ATR_PASS_ARGS, pEnt, pGuild, pDstItem, NULL, pSrcBag->BagID, iDestToSourceMoveCount);
				}
			}
		}
		if (bDstGuild && NONNULL(pDstBag) && NONNULL(pSrcItem))
		{
			if (ISNULL(pGuild) || ISNULL(pGuildBank)) {
				bSuccess = false;
			} else {
				if (iDestToSourceMoveCount>0 && pDstItem!=NULL) {				
					if (pSrcBag->BagID != pDstBag->BagID && !inv_guildbank_trh_UpdateBankTabWithdrawLimit(ATR_PASS_ARGS, pEnt->myContainerID, pGuild, pGuildBank,
																											  iDstBagID, iDestToSourceMoveCount, iDstEPValue)) {
						bSuccess = false;
					} else {
						inv_guild_trh_AddLog(ATR_PASS_ARGS, pEnt, pGuild, pDstItem, NULL, pDstBag->BagID, -iDestToSourceMoveCount);
					}
				}
				if (bSuccess && iSourceToDestMoveCount>0) {				
					inv_guild_trh_AddLog(ATR_PASS_ARGS, pEnt, pGuild, pSrcItem, NULL, pDstBag->BagID, iSourceToDestMoveCount);
				}
			}
		}
	}
	
	return bSuccess;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pGuildBank, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[]");
NOCONST(GuildBankTabInfo) * inv_guildbank_trh_GetBankTabInfo(ATH_ARG NOCONST(Entity) *pGuildBank, InvBagIDs iBagID)
{
	NOCONST(GuildBankTabInfo) *pGuildBankInfo = NULL;
	NOCONST(InventoryBagLite) *pBagLite;
	NOCONST(InventoryBag) *pBagNormal;

	pBagLite = inv_guildbank_trh_GetLiteBag(ATR_EMPTY_ARGS, pGuildBank, iBagID);
	if(NONNULL(pBagLite) && NONNULL(pBagLite->pGuildBankInfo) && (invbaglite_trh_flags(pBagLite) & InvBagFlag_GuildBankBag) != 0)
	{
		pGuildBankInfo = pBagLite->pGuildBankInfo;
	}
	else
	{
		pBagNormal = inv_guildbank_trh_GetBag(ATR_EMPTY_ARGS, pGuildBank, iBagID);
		if(NONNULL(pBagNormal) && NONNULL(pBagNormal->pGuildBankInfo) && (invbag_trh_flags(pBagNormal) & InvBagFlag_GuildBankBag) != 0)
		{
			pGuildBankInfo = pBagNormal->pGuildBankInfo;
		}
	}

	return pGuildBankInfo;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Eamembers[]")
ATR_LOCKS(pGuildBank, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[]");
bool inv_guildbank_trh_CanWithdrawFromBankTab(ATR_ARGS, ContainerID iContainerID, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank, InvBagIDs iBagID, U32 iCount, U32 iValue) {
	S32 iWithdrawTotal;
	S32 iWithdrawLimit;
	U32 iTimestamp;
	bool result = true;
	NOCONST(GuildWithdrawLimit) *pWithdrawLimit;
	NOCONST(GuildMember) *pMember;
	NOCONST(GuildBankTabInfo) *pGuildBankInfo;
	
	pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iContainerID);
	if (ISNULL(pMember)) {
		return false;
	}
	
	pGuildBankInfo = inv_guildbank_trh_GetBankTabInfo(pGuildBank, iBagID);

	if(ISNULL(pGuildBankInfo))
	{
		return false;
	}

	iTimestamp = autoSecsSince2k();

	pWithdrawLimit = CONTAINER_NOCONST(GuildWithdrawLimit, eaIndexedGetUsingInt(&pMember->eaWithdrawLimits, iBagID));

	if(pMember->iRank < 0 || pMember->iRank >= eaSize(&pGuildBankInfo->eaPermissions))
	{
		return false;
	}

	if ((!gConf.bDisableGuildEPWithdrawLimit))
	{
		iWithdrawTotal = (NONNULL(pWithdrawLimit) && (iTimestamp - pWithdrawLimit->iTimestamp) < WITHDRAW_LIMIT_TIME) ? pWithdrawLimit->iWithdrawn + iValue : iValue;
		iWithdrawLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawLimit;
		if (!(iWithdrawLimit <= 0 || iWithdrawTotal <= iWithdrawLimit)) result = false;
	}

	if (result && gConf.bEnableGuildItemWithdrawLimit)
	{
		iWithdrawTotal = (NONNULL(pWithdrawLimit) && (iTimestamp - pWithdrawLimit->iTimestamp) < WITHDRAW_LIMIT_TIME) ? pWithdrawLimit->iItemsWithdrawn + iCount : iCount;
		iWithdrawLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawItemCountLimit;
		if (!(iWithdrawLimit <= 0 || iWithdrawTotal <= iWithdrawLimit)) result = false;
	}

	return result;

}

AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Eamembers[]")
ATR_LOCKS(pGuildBank, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[]");
bool inv_guildbank_trh_UpdateBankTabWithdrawLimit( ATR_ARGS, ContainerID iContainerID, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Entity) *pGuildBank, InvBagIDs iBagID, U32 iCount, U32 iValue )
{
	S32 iWithdrawTotal = 0;
	S32 iWithdrawLimit;
	S32 iItemWithdrawTotal = 0;
	S32 iItemWithdrawLimit;
	U32 iTimestamp;
	bool result1;
	NOCONST(GuildWithdrawLimit) *pWithdrawLimit;
	NOCONST(GuildMember) *pMember;
	NOCONST(GuildBankTabInfo) *pGuildBankInfo;
	
	pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, iContainerID);
	if (ISNULL(pMember)) {
		return false;
	}
	
	pGuildBankInfo = inv_guildbank_trh_GetBankTabInfo(pGuildBank, iBagID);

	if(ISNULL(pGuildBankInfo))
	{
		return false;
	}
	
	iTimestamp = timeSecondsSince2000();

	pWithdrawLimit = CONTAINER_NOCONST(GuildWithdrawLimit, eaIndexedGetUsingInt(&pMember->eaWithdrawLimits, iBagID));

	result1 = true;

	if(pMember->iRank < 0 || pMember->iRank >= eaSize(&pGuildBankInfo->eaPermissions))
	{
		return false;
	}

	if (!gConf.bDisableGuildEPWithdrawLimit)
	{
		iWithdrawTotal = (NONNULL(pWithdrawLimit) && (iTimestamp - pWithdrawLimit->iTimestamp) < WITHDRAW_LIMIT_TIME) ? pWithdrawLimit->iWithdrawn + iValue : iValue;
		iWithdrawLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawLimit;
		if (iWithdrawLimit > 0)
		{
			if (iWithdrawTotal > iWithdrawLimit)
			{
				return false;
			}
			result1 = false;
		}
	}

	if (gConf.bEnableGuildItemWithdrawLimit)
	{
		iItemWithdrawTotal = (NONNULL(pWithdrawLimit) && (iTimestamp - pWithdrawLimit->iTimestamp) < WITHDRAW_LIMIT_TIME) ? pWithdrawLimit->iItemsWithdrawn + iCount : iCount;
		iItemWithdrawLimit = pGuildBankInfo->eaPermissions[pMember->iRank]->iWithdrawItemCountLimit;
		if (iItemWithdrawLimit > 0)
		{
			if (iItemWithdrawTotal > iItemWithdrawLimit)
			{
				return false;
			}
			result1 = false;
		}
	}

	if (result1) {
		return true;
	}

	if (ISNULL(pWithdrawLimit)) {
		pWithdrawLimit = StructCreateNoConst(parse_GuildWithdrawLimit);
		pWithdrawLimit->eBagID = iBagID;
		pWithdrawLimit->iTimestamp = 0;
		pWithdrawLimit->iWithdrawn = 0;
		pWithdrawLimit->iItemsWithdrawn = 0;
		eaPush(&pMember->eaWithdrawLimits, pWithdrawLimit);
	}

	if (!gConf.bDisableGuildEPWithdrawLimit)
	{
		pWithdrawLimit->iWithdrawn = iWithdrawTotal;
	}
	if (gConf.bEnableGuildItemWithdrawLimit)
	{
		pWithdrawLimit->iItemsWithdrawn = iItemWithdrawTotal;
	}

	if (iTimestamp - pWithdrawLimit->iTimestamp >= WITHDRAW_LIMIT_TIME) {
		pWithdrawLimit->iTimestamp = iTimestamp;
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
NOCONST(Item)* inv_trh_GetItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 id, InvBagIDs *pRetBagID, int *pRetSlot, InvGetFlag getFlag)
{
	int NumBags,ii;

	if ( ISNULL(pEnt))
		return NULL;

	if ( ISNULL(pEnt->pInventoryV2))
		return NULL;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	for(ii=0;ii<NumBags;ii++)
	{
		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[ii];
		int ij;

		int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
		
		// There are times when we don't want to see the buy back bag
		if((getFlag & InvGetFlag_NoBuyBackBag) != 0 && pBag->BagID == InvBagIDs_Buyback)
		{
			continue;
		}

		// There are times when we don't want to see the buy back bag
		if((getFlag & InvGetFlag_NoBankBag) != 0 && (pBag->BagID  >= InvBagIDs_Bank && pBag->BagID <= InvBagIDs_Bank9))
		{
			continue;
		}
		//loop for all slots in this bag
		for(ij=0; ij<NumSlots; ij++)
		{
			NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[ij];

			if  (NONNULL(pSlot->pItem))
			{
				NOCONST(Item)* pItem = pSlot->pItem;
						
				if(pItem && pItem->id==id)
				{
					if(pRetBagID)
						*pRetBagID = invbag_trh_bagid(pBag);

					if(pRetSlot)
						*pRetSlot = ij;

					return pItem;
				}
			}
		}
	}

	return NULL;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
NOCONST(Item)* inv_bag_trh_GetItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx)
{
	NOCONST(InventorySlot)* pSlot = NULL;

	if ( ISNULL(pBag) )
		return NULL;

	pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, SlotIdx);

	if (ISNULL(pSlot))
		return NULL;

	//make sure that this slot is an Item
	if ( ISNULL(pSlot->pItem) )
	{
		return NULL;
	}

	return pSlot->pItem;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pBag, ".Ppindexedinventoryslots");
NOCONST(Item)* inv_bag_trh_GetItemByID(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, U64 id, int* pSlotOut, int* pCountOut)
{
	int iNumSlots,ii;

	if ( ISNULL(pBag))
		return NULL;

	iNumSlots = eaSize(&pBag->ppIndexedInventorySlots);
	//loop for all slots in this bag
	for(ii=0; ii<iNumSlots; ii++)
	{
		NOCONST(InventorySlot)* pSlot = pBag->ppIndexedInventorySlots[ii];

		if  (NONNULL(pSlot->pItem))
		{
			NOCONST(Item)* pItem = pSlot->pItem;

			if(pItem && pItem->id==id)
			{
				if (pCountOut)
					*pCountOut = pItem->count;
				if (pSlotOut)
					*pSlotOut = ii;
				return pItem;
			}
		}
	}
	

	return NULL;
}

// Wrapper to get an item from a bag.  Correctly gets item from guild bag if neccessary
Item* inv_GetItemFromBag(Entity* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract)
{
	if(inv_IsGuildBag(BagID)) {
		return (Item*) inv_guildbank_trh_GetItemFromBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, SlotIdx);
	} else {
		return (Item*) inv_trh_GetItemFromBag(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), BagID, SlotIdx, pExtract);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
NOCONST(Item)* inv_trh_GetItemFromBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(InventorySlot)* pSlot = NULL;

	if ( ISNULL(pEnt))
		return NULL;

	if ( ISNULL(pEnt->pInventoryV2))
		return NULL;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return NULL;

	pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, SlotIdx);

	if (ISNULL(pSlot))
		return NULL;

	//make sure that this slot is an Item
	if ( ISNULL(pSlot->pItem) )
	{
		return NULL;
	}

	return pSlot->pItem;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pguild.Hguild, .Pplayer.Pguild.Hguildbank");
NOCONST(Item)* inv_guildbank_trh_GetItemFromBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx)
{
	NOCONST(Guild) *pGuild = NULL;
	NOCONST(Entity) *pGuildBank = NULL;
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(InventorySlot)* pSlot = NULL;
	
	pGuild = NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pGuild) ? CONTAINER_NOCONST(Guild, GET_REF(pEnt->pPlayer->pGuild->hGuild)) : NULL;
	if (ISNULL(pGuild)) {
		return NULL;
	}

	pGuildBank = NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pGuild) ? CONTAINER_NOCONST(Entity, GET_REF(pEnt->pPlayer->pGuild->hGuildBank)) : NULL;

	if (ISNULL(pGuildBank)) {
		return NULL;
	}

	pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, BagID);
	if (ISNULL(pBag)) {
		return NULL;
	}
	
	pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, SlotIdx);
	if (ISNULL(pSlot)) {
		return NULL;
	}
	
	//make sure that this slot is an Item
	if (ISNULL(pSlot->pItem)) {
		return NULL;
	}
	
	return pSlot->pItem;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
NOCONST(Item)* inv_trh_GetItemFromBagIDByName( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char* ItemDefName, GameAccountDataExtract *pExtract )
{
	InventoryList tmpInventoryList = {0};
	int MatchCount = 0;
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(Item)* pItem = NULL;

	if ( ISNULL(pEnt))
		return NULL;

	if ( ISNULL(pEnt->pInventoryV2))
		return NULL;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return NULL;

	MatchCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, ItemDefName, &tmpInventoryList);

	if ( MatchCount > 0 )
	{
		InventoryListEntry* pListEntry = &tmpInventoryList.Entry[0];

		if ( NONNULL(pListEntry) &&
			 NONNULL(pListEntry->pSlot) &&
			 NONNULL(pListEntry->pSlot->pItem) )
		{
			pItem = CONTAINER_NOCONST(Item, pListEntry->pSlot->pItem);
		}
	}

	return pItem;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
NOCONST(Item)* inv_bag_trh_GetItemByName(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, const char* ItemDefName)
{
	NOCONST(Item)* pItem = NULL;

	if ( ISNULL(pBag))
		return NULL;

	PERFINFO_AUTO_START_FUNC();


	if ( invbag_trh_flags(pBag) & InvBagFlag_NameIndexed )
	{
        pItem = inv_bag_trh_GetIndexedItemByName(ATR_PASS_ARGS, pBag, ItemDefName);
	}
	else
	{
		//bag slots indexed by slot #

		InventoryList tmpInventoryList = {0};
		int MatchCount = 0;

		MatchCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, ItemDefName, &tmpInventoryList);

		if ( MatchCount > 0 )
		{
			InventoryListEntry* pListEntry = &tmpInventoryList.Entry[0];

			if ( NONNULL(pListEntry) &&
				NONNULL(pListEntry->pSlot) &&
				NONNULL(pListEntry->pSlot->pItem) )
			{
				pItem = CONTAINER_NOCONST(Item, pListEntry->pSlot->pItem);
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return pItem;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pBag, ".ppindexedliteslots[]");
int inv_lite_trh_CountItemByName(ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* ItemDefName)
{
	if ( ISNULL(pBag))
		return 0;
	else
	{
		NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, ItemDefName);
		return pSlot ? pSlot->count : 0;
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Inv_Def, .Bagid, ppIndexedInventorySlots[]");
NOCONST(Item)* inv_bag_trh_GetIndexedItemByName(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, const char* ItemDefName)
{
	NOCONST(Item)* pItem = NULL;
    
	if ( ISNULL(pBag))
		return NULL;
    
	PERFINFO_AUTO_START_FUNC();
    
    
    //bag slots indexed by name
	if ( invbag_trh_flags(pBag) & InvBagFlag_NameIndexed )
	{
        NOCONST(InventorySlot) *pSlot = eaIndexedGetUsingString(&pBag->ppIndexedInventorySlots, ItemDefName);
        
		if (NONNULL(pSlot))
			pItem = pSlot->pItem;
    }
    else
        Errorf("non-indexed bag passed to function that requires index");
    
	PERFINFO_AUTO_STOP();
    
	return pItem;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
void inv_trh_GetSlotByItemName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, const char* ItemDefName, InvBagIDs *pRetBagID, int *pRetSlot, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;

	*pRetBagID = InvBagIDs_None;
	*pRetSlot = -1;

	if ( ISNULL(pEnt))
		return;

	if ( ISNULL(pEnt->pInventoryV2))
		return;

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
		return;

	if ( invbag_trh_flags(pBag) & InvBagFlag_NameIndexed )
	{
		//bag slots indexed by name

		int index = eaIndexedFindUsingString(&pBag->ppIndexedInventorySlots, ItemDefName);

		if (index>=0)
		{
			*pRetBagID = invbag_trh_bagid(pBag);
			*pRetSlot = index;
		}
	}

	else
	{
		//bag slots indexed by slot #

		InventoryList tmpInventoryList = {0};
		int MatchCount = 0;

		MatchCount = inv_bag_trh_GetItemList(ATR_PASS_ARGS, pBag, ItemDefName, &tmpInventoryList );

		if ( MatchCount > 0 )
		{
			InventoryListEntry* pListEntry = &tmpInventoryList.Entry[0];

			if (NONNULL(pListEntry))
			{
				*pRetBagID = invbag_bagid(pListEntry->pBag);
				*pRetSlot = eaIndexedFindUsingString(&pListEntry->pBag->ppIndexedInventorySlots, pListEntry->pSlot->pchName);
			}
		}
	}
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//Scan through player index bag and update playerbag sizes based on bag items in index 

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, .pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
void inv_trh_UpdatePlayerBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pIndexBag = NULL;
	int SlotIdx;

	if ( ISNULL(pEnt) )
		return;

	if ( ISNULL(pEnt->pInventoryV2) )
		return;

	pIndexBag = CONTAINER_NOCONST(InventoryBag, eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 9 /* Literal InvBagIDs_PlayerBags */));
	if (ISNULL(pIndexBag))
		return;

	for(SlotIdx=0; SlotIdx<invbag_trh_maxslots(pEnt, pIndexBag); SlotIdx++)
	{
		NOCONST(Item)* pItem = inv_trh_GetItemPtrFromSlot(ATR_PASS_ARGS, pIndexBag, SlotIdx);
		ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		NOCONST(InventoryBag)* pPlayerBag = inv_trh_PlayerBagFromSlotIdx(ATR_PASS_ARGS, pEnt, SlotIdx );

		if (ISNULL(pPlayerBag))
			continue;
		
		if(!GamePermissions_trh_CanAccessBag(pEnt, pPlayerBag->BagID, pExtract))
		{
			continue;
		}

		if ( ISNULL(pItem) || 
			 ISNULL(pItemDef) ||
			 (pItemDef->eType != kItemType_Bag) )
		{
			//no valid bag item in slot, set corresponding player bag size to 0
			inv_bag_trh_SetMaxSlots(ATR_PASS_ARGS, pEnt, pPlayerBag, 0);
		}
		else
		{
			//set playerbag size based on size in item def
			inv_bag_trh_SetMaxSlots(ATR_PASS_ARGS, pEnt, pPlayerBag, pItemDef->iNumBagSlots);
		}
	}
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[]");
int inv_trh_GetNumericValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* ItemDefName)
{
	NOCONST(InventoryBagLite)* pBag;
	NOCONST(InventorySlotLite)* pSlot;

	if (ISNULL(pEnt))
		return 0;

	if (ISNULL(pEnt->pInventoryV2))
		return 0;

	// This lookup is hardcoded because the AUTO_TRANS parser can't look up enum values, so we can't just look up InvBagIDs_Numeric
	pBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */);
	if (ISNULL(pBag))
		return 0;

	pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, ItemDefName);
	if (ISNULL(pSlot))
		return 0;

	return pSlot->count;
}

// Retrieve the value of a numeric item, which is always in the Numeric bag
// There used to be a seperate "fast" version, but this was just made fast instead

S32 inv_GetNumericItemValueScaled(Entity* pEnt, const char* ItemDefName)
{
	S32 iNumericValue = inv_trh_GetNumericValue(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), ItemDefName);
	
	ItemDef* pItemDef = RefSystem_ReferentFromString(g_hItemDict, ItemDefName);
	if (pItemDef)
	{
		iNumericValue *= pItemDef->fScaleUI;
	}
	return iNumericValue;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pGuildBank, "pInventoryV2.ppLiteBags[]");
S32 inv_guildbank_trh_GetNumericItemValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* pGuildBank, S32 iBagID, const char* pcItemDefName)
{
	NOCONST(Item) *pItem = NULL;
	InventoryBagLite *pBag;
	InventorySlotLite *pSlot;
	
	if (ISNULL(pGuildBank) || ISNULL(pGuildBank->pInventoryV2)) {
		return 0;
	}
	
    pBag = (InventoryBagLite*)inv_guildbank_trh_GetLiteBag(ATR_PASS_ARGS,pGuildBank,iBagID);
	if (ISNULL(pBag)) {
		return 0;
	}
	
	pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, pcItemDefName);
	if (ISNULL(pSlot)) {
		return 0;
	}
	
	return pSlot->count;
}

const char *inv_GetNumericItemDisplayName(Entity* pEnt, const char* ItemDefName)
{
	ItemDef *pDef;

	if (!pEnt)
		return NULL;

	pDef = item_DefFromName(ItemDefName);
	if (!pDef) return NULL;

	return item_GetDefLocalNameFromEnt(pDef, pEnt, entGetLanguage(pEnt));
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pSlot, ".count");
bool inv_lite_trh_SimpleClampLow(ATH_ARG NOCONST(InventorySlotLite)* pSlot, ItemDef* pDef)
{
	if(!pDef)
		return false;

	// do a low limit check on the numeric
	if (pSlot->count < pDef->MinNumericValue)
	{
		if (pDef->flags & kItemDefFlag_TransFailonLowLimit)
			return false;
		else
			pSlot->count = pDef->MinNumericValue;
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pSlot, ".count");
bool inv_lite_trh_SimpleClampHigh(ATH_ARG NOCONST(InventorySlotLite)* pSlot, ItemDef* pDef, int* piOverflow)
{
	if(!pDef)
		return false;

	if(pSlot->count > pDef->MaxNumericValue)
	{
		if(piOverflow)
			(*piOverflow) = pSlot->count - pDef->MaxNumericValue;
		if (pDef->flags & kItemDefFlag_TransFailonHighLimit)
			return false;
		else
			pSlot->count = pDef->MaxNumericValue;
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pBag, ".ppIndexedLiteSlots");
bool inv_lite_trh_ClampNumericValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pchDefName, bool bSilent, S32 iOldValue, const ItemChangeReason *pReason)
{
	NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, pchDefName);
	ItemDef *pItemDef = pSlot ? GET_REF(pSlot->hItemDef) : NULL;
	S32 iOverflow = 0;
	S32 iPermissionLimit;

	if (ISNULL(pItemDef))
		return false;

	if(!inv_lite_trh_SimpleClampLow(pSlot, pItemDef))
	{
		return false;
	}

	if(gamePermission_Enabled() && pEnt->myEntityType != GLOBALTYPE_ENTITYGUILDBANK)
	{
		// Do high limit check based on permissions (not against guild bags)
		iPermissionLimit = GamePermissions_trh_GetCachedMaxNumeric(pEnt, pchDefName, true);
		if(pSlot->count > iPermissionLimit)
		{
			iOverflow = pSlot->count - iPermissionLimit;
			if (pItemDef->flags & kItemDefFlag_TransFailonHighLimit)
			{
				return false;
			}
			else
			{
				// Note: permissions will never lower old values
				// to less than what the player had before !!!
				// If this needs to be changed for products other than CO a new flag (ugh) will have to be added
				pSlot->count = max(min(iOldValue, pSlot->count), iPermissionLimit);
			}
		}
	}

	if(!inv_lite_trh_SimpleClampHigh(pSlot, pItemDef, &iOverflow))
	{
		return false;
	}

	// do specific clamp checks for varying bounds
	// !!!!  special case value cap for skill level
	// hard coded here but should probably be implemented in a more generic way if we need this
	// type of thing for more than just this
	if (stricmp(pchDefName,"SkillLevel") == 0)
	{
		S32 iLevelCap = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "SkillLevelCap");

		if (pSlot->count > iLevelCap )
		{
			//adjust value to cap
			pSlot->count = iLevelCap;
		}
	}

	if(iOverflow && IS_HANDLE_ACTIVE(pItemDef->hNumericOverflow))
	{
		iOverflow = (S32)iOverflow * pItemDef->fNumericOverflowMulti;
		return inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS,pEnt,bSilent,pBag,REF_STRING_FROM_HANDLE(pItemDef->hNumericOverflow),iOverflow,NumericOp_Add, pReason);
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pBag, ".ppIndexedLiteSlots");
bool inv_lite_trh_SetNumericInternal(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pchItemDefName, int count, bool bAdded, const ItemChangeReason *pReason)
{
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pchItemDefName);
	NOCONST(InventorySlotLite)* pSlot = inv_lite_trh_GetOrCreateLiteSlot(pBag, pchItemDefName);
	S32 iOldValue;

	if (ISNULL(pItemDef))
		return false;

	iOldValue = pSlot->count;
	pSlot->count = count;

	if (pItemDef->eType == kItemType_Numeric)
	{
		if (!inv_lite_trh_ClampNumericValue(ATR_PASS_ARGS, pEnt, pBag, pchItemDefName, bSilent, iOldValue, pReason))
		{
			// If the clamping failed, return the numeric to its original value.
			// In most cases this doesn't matter, because if this helper fails, the transaction
			//  will fail.  However there are cases where this is not true, so we don't want
			//  to leave around a garbage value which might get unintentionally persisted.
			pSlot->count = iOldValue;
			return false;
		}
		if (iOldValue == pSlot->count)
			return true;
	}

#if defined(GAMESERVER) || defined(APPSERVER)
	//queue up remote routine to flag add event
	if(pEnt->myEntityType!=GLOBALTYPE_NONE) // Don't bother with fake entities
	{
#if defined(GAMESERVER)
		ItemTransCBData *pData = StructCreate(parse_ItemTransCBData);

		eaPush(&pData->ppchNamesUntranslated, item_trh_GetDefLocalNameKeyFromEnt(pItemDef, pEnt));

		pData->bTranslateName = true;

		if (NONNULL(pItemDef))
			pData->pchItemDefName = StructAllocString(pItemDef->pchName);
		pData->type = bAdded;
		pData->value = bAdded ? pSlot->count - iOldValue : pSlot->count;
		pData->iCount = pData->value;
		pData->bSilent = bSilent;
		pData->bLite = true;

		if (pReason)
		{
			pData->bFromRollover = pReason->bFromRollover;
			pData->bFromStore = pReason->bFromStore;
			pData->pchSoldItem = pReason->pcDetail;
			copyVec3(pReason->vPos, pData->vOrigin);
		}

		if(pData->value && pItemDef->fScaleUI != 1.0f)
		{
			pData->value = ((S32)pSlot->count * pItemDef->fScaleUI) - ((S32)iOldValue * pItemDef->fScaleUI);

		}

		QueueRemoteCommand_RemoteNumericCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
		if (pData->iCount >= 0)
			QueueRemoteCommand_RemoteAddItemEventCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
		else
		{
			pData->iCount = -pData->iCount;
			QueueRemoteCommand_RemoteRemoveItemEventCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
		}
		eaClear(&pData->ppchNamesUntranslated);
		StructDestroy(parse_ItemTransCBData, pData);
#endif // GAMESERVER
		// Also log change for the economy log
		if (pItemDef->bLogForEconomy 
#ifdef GAMESERVER 
			&& gbEnableEconomyLogging
#endif
			) {
			TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GAMEECONOMY, "EconomyNumericChange",
				"Numeric \"%s\" Old %d New %d Added %d Reason \"%s\" Detail \"%s\" Map \"%s\" Pos \"%g,%g,%g\"",
				pItemDef->pchName, 
				iOldValue, pSlot->count, pSlot->count - iOldValue,
				(pReason && pReason->pcReason ? pReason->pcReason : "Unknown"), 
				(pReason && pReason->pcDetail ? pReason->pcDetail : "Unknown"),
				(pReason && pReason->pcMapName ? pReason->pcMapName : "Unknown"),
				(pReason ? pReason->vPos[0] : 0.0),
				(pReason ? pReason->vPos[1] : 0.0),
				(pReason ? pReason->vPos[2] : 0.0)
				);
		}
	}
#endif // GAMESERVER or APPSERVER

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pBag, ".Ppindexedliteslots");
bool inv_lite_trh_OnNumericChanged(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pchItemName, bool bSilent, const ItemChangeReason *pReason)
{
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pchItemName);
	NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, pchItemName);
	
	if (ISNULL(pItemDef))
		return false;

	// adjust level numeric when XP changes
	if (stricmp(pchItemName,gConf.pcLevelingNumericItem) == 0)
	{
		int iNewLevel = LevelFromLevelingNumeric(pSlot->count);
		pEnt->pChar->iLevelExp = iNewLevel; // Update saved level value
		return inv_lite_trh_SetNumericInternal(ATR_PASS_ARGS, pEnt, true, pBag, "level", iNewLevel, false, pReason);
	}
	else if (stricmp(pItemDef->pchName, "Level") == 0)
	{
		// adjust saved level when level changes explicitly
		pEnt->pChar->iLevelExp = pSlot->count;
	}

	return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
bool inv_ent_trh_SetNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason)
{
	return inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, bSilent, pcItemDefName, iValue, NumericOp_SetTo, pReason);
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
bool inv_ent_trh_AddNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason)
{
	return inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, bSilent, pcItemDefName, iValue, NumericOp_Add, pReason);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
bool inv_ent_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, const char* pcItemDefName, S32 iValue, NumericOp op, const ItemChangeReason *pReason)
{
	NOCONST(InventoryBagLite)* pBag = NULL;

	if ( ISNULL(pEnt))
		return false;

	if ( ISNULL(pEnt->pInventoryV2))
		return false;

	// This lookup is hardcoded because the AUTO_TRANS parser can't look up enum values, so we can't just look up InvBagIDs_Numeric
	pBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */);

	if (ISNULL(pBag))
		return false;
	return inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, iValue, op, pReason);
}
//						*Unused*
/*
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Itemidmax, pInventoryV2.ppInventoryBags[], .Pplayer.Playertype")
	ATR_LOCKS(pBag, ".ppIndexedLiteSlots[]");
bool inv_lite_trh_SetNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char*pcItemDefName, S32 iValue, const ItemChangeReason *pReason)
{
	return inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, iValue, NumericOp_SetTo, pReason);
}
*/

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pBag, ".Ppindexedliteslots");
bool inv_lite_trh_AddNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pcItemDefName, S32 iValue, const ItemChangeReason *pReason)
{
	return inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, iValue, NumericOp_Add, pReason);
}

AUTO_TRANS_HELPER;
void inv_trh_UpdateUGCKillCreditLimit(ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventorySlotLite)* pExpSlot, S32 iValue, NumericOp op)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pExpSlot))
	{
		if (IsGameServerSpecificallly_NotRelatedTypes() && ugcDefaultsGetKillCreditLimit())
		{
			U32 uCurrentTime = timeSecondsSince2000();

			if (!pEnt->pPlayer->pUGCKillCreditLimit)
			{
				pEnt->pPlayer->pUGCKillCreditLimit = StructCreateNoConst(parse_PlayerUGCKillCreditLimit);
				pEnt->pPlayer->pUGCKillCreditLimit->uTimestamp = uCurrentTime;
			}
			else
			{
				U32 uTimeInterval = MAX(ugcDefaultsGetKillCreditLimit()->uTimeInterval, 1);
				U32 uTimestamp = pEnt->pPlayer->pUGCKillCreditLimit->uTimestamp;
				U32 uLastTimeIntervalIndex = uTimestamp / uTimeInterval;
				U32 uThisTimeIntervalIndex = uCurrentTime / uTimeInterval;
				if (uLastTimeIntervalIndex != uThisTimeIntervalIndex)
				{
					pEnt->pPlayer->pUGCKillCreditLimit->iExpEarned = 0;
					pEnt->pPlayer->pUGCKillCreditLimit->uTimestamp = uCurrentTime;
				}
			}
			switch(op)
			{
				xcase NumericOp_Add:
				{
					pEnt->pPlayer->pUGCKillCreditLimit->iExpEarned += iValue;
				}
				xcase NumericOp_RaiseTo:
				acase NumericOp_SetTo:
				{
					if (iValue > pExpSlot->count)
					{
						S32 iValueToAdd = iValue - pExpSlot->count;
						pEnt->pPlayer->pUGCKillCreditLimit->iExpEarned += iValueToAdd;
					}
				}
			}
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pBag, ".ppIndexedLiteSlots");
bool inv_lite_trh_ApplyNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pcItemDefName, S32 iValue, NumericOp op, const ItemChangeReason *pReason)

{
	NOCONST(InventorySlotLite)* pSlot;
	bool bSucceeded = true;
	bool bIsLevellingNumeric = (stricmp(pcItemDefName, gConf.pcLevelingNumericItem) == 0);

	if (ISNULL(pBag))
	{
		return false;
	}

	pSlot = inv_lite_trh_GetOrCreateLiteSlot(pBag, pcItemDefName);

	// Update UGC kill credit limit
	if (pReason && pReason->bUGC && pReason->bKillCredit && stricmp(pcItemDefName,gConf.pcLevelingNumericItem) == 0)
	{
		inv_trh_UpdateUGCKillCreditLimit(pEnt, pSlot, iValue, op);
	}

    switch(op)
    {
		xcase NumericOp_Add:
		{
			S64 finalVal = pSlot->count;
			S64 originalValue = finalVal;
			S32 iNumBonus = 0;
			ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pcItemDefName);
			
			finalVal += iValue;
			
			if (bIsLevellingNumeric)
			{
				if (iValue > 0)
					scp_trh_AwardActivePetXP(ATR_PASS_ARGS, pEnt, iValue, false);
			}

			if (pItemDef)
			{
				bool bHasOverflowNumeric = IS_HANDLE_ACTIVE(pItemDef->hNumericOverflow);

				iNumBonus = eaSize(&pItemDef->eaBonusNumerics);	// if there isn't a def here a serious problem has occurred

				// Add bonus numeric exp if there is a positive value and there are bonus numerics 
				if(iNumBonus > 0)
				{
					S64 diffValue = finalVal - originalValue;
					if(diffValue > 0)
					{
						S32 i;
						S64 iMaxNumeric = pItemDef->MaxNumericValue;
						// Add additional bonus from other numerics 
						for(i = 0; (i < iNumBonus && bSucceeded && diffValue > 0 && (bHasOverflowNumeric || finalVal < iMaxNumeric)); ++i)
						{
							ItemDef *pBonusDef = GET_REF(pItemDef->eaBonusNumerics[i]->hItem);
							if(pBonusDef)
							{
								int iBonusCount = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pBonusDef->pchName);
								if(iBonusCount > 0)
								{
									// how much can be given at maximum? This rounds up and will always return a value of at least one
									S64 iGive = (pBonusDef->uBonusPercent * diffValue + 99) / 100;
									S64 maxBonus = iMaxNumeric - finalVal;

									if(!bHasOverflowNumeric)
									{
										// Don't go over max for this numeric
										iGive = min(maxBonus, iGive);	
									}

									// reduce to amount left
									iGive = min(iGive, iBonusCount);

									if(iGive > 0)
									{
										// how much is left of the bonus numeric?
										S64 iBonusFinal = iBonusCount - iGive;

										// reduce amount left that the next bonus can use
										S64 iReduceDiff = (iGive * 100) / pBonusDef->uBonusPercent;
										// alway deduct at least one
										iReduceDiff = max(1,iReduceDiff);
										diffValue -= iReduceDiff;

										if (pBonusDef->flags & kItemDefFlag_SCPBonusNumeric)
										{
											//This is a bonus numeric that should apply to your active SCP and not yourself

											if (bIsLevellingNumeric)
											{
												if (iGive > 0)
													scp_trh_AwardActivePetXP(ATR_PASS_ARGS, pEnt, iGive, true);
											}
										}
										else
										{
											// Add the give amount to the total of the target numeric
											finalVal += iGive;
										}

										// Apply maximum / min to bonus numeric
										if(iBonusFinal > INT_MAX)
											iBonusFinal = INT_MAX;
										else if (iBonusFinal < INT_MIN)
											iBonusFinal = INT_MIN;

										// adjust the value of the bonus value
										bSucceeded = inv_lite_trh_SetNumericInternal(ATR_PASS_ARGS, pEnt, bSilent, pBag, pBonusDef->pchName, (S32)iBonusFinal, true, pReason);				
										if(!bSucceeded)
										{
											return false;
										}

									}
								}
							}
						}
					}
				}
			}
			else
			{
				if(pReason && pReason->pcDetail)
				{
					ErrorDetailsf("Can't grant <%s> with reason %s", pcItemDefName, pReason->pcDetail);
				}
				else
				{
					ErrorDetailsf("Can't grant <%s> without a reason", pcItemDefName);
				}

				Errorf("inv_lite_trh_ApplyNumeric attempted to grant a numeric with a non-existant def: ");
				return false;
			}

			if(finalVal > INT_MAX)
				finalVal = INT_MAX;
			else if (finalVal < INT_MIN)
				finalVal = INT_MIN;

			// adjust the value
			bSucceeded = inv_lite_trh_SetNumericInternal(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, (S32)finalVal, true, pReason);				

		}
		xcase NumericOp_RaiseTo: 
			if (pSlot->count < iValue)
				bSucceeded = inv_lite_trh_SetNumericInternal(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, iValue, false, pReason);
		xcase NumericOp_LowerTo: 
			if (pSlot->count > iValue)
				bSucceeded = inv_lite_trh_SetNumericInternal(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, iValue, false, pReason);
	    xcase NumericOp_SetTo:
		    bSucceeded = inv_lite_trh_SetNumericInternal(ATR_PASS_ARGS, pEnt, bSilent, pBag, pcItemDefName, iValue, true, pReason);
    }

	if (!bSucceeded)
		return false;

	// adjust any dependent numerics
	if (!inv_lite_trh_OnNumericChanged(ATR_PASS_ARGS, pEnt, pBag, pcItemDefName, bSilent, pReason))
		return false;

    return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEntSrc, ".pInventoryV2.Pplitebags[]")
ATR_LOCKS(pEntDst, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .pInventoryV2.Pplitebags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance");
bool inv_trh_CopyNumerics(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEntSrc, ATH_ARG NOCONST(Entity)* pEntDst, const ItemChangeReason *pReason )
{
	S32 i;
	NOCONST(InventoryBagLite) *pBagSrc, *pBagDst;
	
	if (ISNULL(pEntSrc) || ISNULL(pEntDst))
		return false;

	if (ISNULL(pEntSrc->pInventoryV2) || ISNULL(pEntDst->pInventoryV2))
		return false;

	pBagSrc = eaIndexedGetUsingInt(&pEntSrc->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */);
	
	if (ISNULL(pBagSrc))
		return false;

	pBagDst = eaIndexedGetUsingInt(&pEntDst->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */);

	if (ISNULL(pBagDst))
		return false;

	for ( i = 0; i < eaSize( &pBagSrc->ppIndexedLiteSlots ); i++ )
	{
		NOCONST(InventorySlotLite) *pSlot = pBagSrc->ppIndexedLiteSlots[i];

		if ( ISNULL(pSlot) || pSlot->count == 0 )
			continue;
		
		inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS, pEntDst, true, pBagDst, pSlot->pchName, pSlot->count, NumericOp_SetTo, pReason);
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[]");
S32 inv_trh_HasToken(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* ItemDefName)
{
	InventoryBagLite *pBag;
	InventorySlotLite *pSlot;

	if (ISNULL(pEnt))
		return 0;

	if (ISNULL(pEnt->pInventoryV2))
		return 0;
	
	pBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppLiteBags, 6 /* Literal InvBagIDs_Tokens */);

	if (ISNULL(pBag))
		return 0;

	pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, ItemDefName);
	if (ISNULL(pSlot))
		return 0;

	// If the slot for the item exists, then you have the token
	return(1);
}
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags[], .Pplayer.Playertype")
ATR_LOCKS(pBuild, ".Ppitems");
void inv_ent_trh_BuildFill(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(EntityBuild)* pBuild)
{
	int NumBags,ii;

	eaDestroyStructNoConst(&pBuild->ppItems,parse_EntityBuildItem);

	if ( ISNULL(pEnt))
		return;

	if ( ISNULL(pEnt->pInventoryV2))
		return;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	for(ii=0;ii<NumBags;ii++)
	{
		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[ii];
		if(invbag_trh_flags(pBag) & (InvBagFlag_EquipBag | InvBagFlag_DeviceBag | InvBagFlag_ActiveWeaponBag))
		{
			int ij;
			for(ij=0; ij<invbag_trh_maxslots(pEnt, pBag); ij++)
			{
				NOCONST(Item) *pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS,pBag,ij);
				NOCONST(EntityBuildItem) *pBuildItem = CONTAINER_NOCONST(EntityBuildItem, StructAlloc(parse_EntityBuildItem));
				pBuildItem->eBagID = invbag_trh_bagid(pBag);
				pBuildItem->iSlot = ij;
				pBuildItem->ulItemID = pItem ? pItem->id : 0;
				eaPush(&pBuild->ppItems,pBuildItem);
			}
		}
	}

	return;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags[], .Pplayer.Playertype");
void inv_ent_trh_BuildCurrentFill(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent)
{
	int Builds;
	NOCONST(EntityBuild) *pBuild;

	if ( ISNULL(pEnt))
		return;

	if ( ISNULL(pEnt->pInventoryV2))
		return;

	if ( ISNULL(pEnt->pSaved))
		return;

	Builds = eaSize(&pEnt->pSaved->ppBuilds);

	if(Builds <= (S32)pEnt->pSaved->uiIndexBuild)
		return;

	pBuild = pEnt->pSaved->ppBuilds[pEnt->pSaved->uiIndexBuild];

	if(!pBuild)
		return;

	inv_ent_trh_BuildFill(ATR_PASS_ARGS,pEnt,bSilent,pBuild);

	return;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Uiindexbuild, .Psaved.Ppbuilds");
void inv_ent_trh_BuildCurrentSetItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, InvBagIDs BagID, int iSlot, U64 itemID)
{
	int Builds, ii;
	NOCONST(EntityBuild) *pBuild;

	if ( ISNULL(pEnt))
		return;

	if ( ISNULL(pEnt->pSaved))
		return;

	Builds = eaSize(&pEnt->pSaved->ppBuilds);

	if(Builds <= (S32)pEnt->pSaved->uiIndexBuild)
		return;

	pBuild = pEnt->pSaved->ppBuilds[pEnt->pSaved->uiIndexBuild];

	if(!pBuild || pBuild->bSwappingOut)
		return;

	for(ii=eaSize(&pBuild->ppItems)-1; ii>=0; ii--)
	{
		if(pBuild->ppItems[ii]->eBagID==BagID && pBuild->ppItems[ii]->iSlot==iSlot)
		{
			pBuild->ppItems[ii]->ulItemID = itemID;
			break;
		}
	}

	if(ii<0)
	{
		NOCONST(EntityBuildItem) *pBuildItem = CONTAINER_NOCONST(EntityBuildItem, StructAlloc(parse_EntityBuildItem));
		pBuildItem->eBagID = BagID;
		pBuildItem->iSlot = iSlot;
		pBuildItem->ulItemID = itemID;
		eaPush(&pBuild->ppItems,pBuildItem);
	}
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .pInventoryV2.ppLiteBags, .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
InvBagIDs inv_trh_GetBestRestrictBagHelper(ATH_ARG NOCONST(Entity)* pEnt, 
										   ItemDef* pItemDef, 
										   int iCount, 
										   bool bCheckBag, 
										   InvBagFlag eExcludedFlags,
										   GameAccountDataExtract *pExtract)
{
	S32 iRestrictBagIdx;
 	for (iRestrictBagIdx = 0; iRestrictBagIdx < eaiSize(&pItemDef->peRestrictBagIDs); iRestrictBagIdx++)
	{
		InvBagIDs eRestrictBagID = pItemDef->peRestrictBagIDs[iRestrictBagIdx];
		NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, eRestrictBagID, pExtract);
		
		if (ISNULL(pBag) || (invbag_trh_flags(pBag) & eExcludedFlags))
		{
			NOCONST(InventoryBagLite)* pLiteBag = inv_trh_GetLiteBag(ATR_EMPTY_ARGS, pEnt, eRestrictBagID, pExtract);
			if (NONNULL(pLiteBag))
				return eRestrictBagID;

			continue;

		}
		if (!bCheckBag || inv_bag_trh_CanItemDefFitInBag(ATR_EMPTY_ARGS, pEnt, pBag, pItemDef, iCount))
		{
			return eRestrictBagID;
		}
	}
	return InvBagIDs_None;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
InvBagIDs inv_trh_GetBestRestrictBagForItemDef(ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef, int iCount, bool bGive, InvBagFlag eExcludedFlags, GameAccountDataExtract *pExtract)
{
	InvBagIDs BagID = InvBagIDs_None;
	InvBagIDs eRestrictBagID;

	if (!inv_ent_trh_EquipLimitCheck(ATR_EMPTY_ARGS, pEnt, NULL, pItemDef, -1, -1, NULL, NULL))
	{
		eExcludedFlags |= (InvBagFlag_EquipBag|InvBagFlag_DeviceBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag);
	}

	// If this item has a preferred restrict bag, use that
	eRestrictBagID = itemAcquireOverride_GetPreferredRestrictBag(pItemDef);
	if (eRestrictBagID != InvBagIDs_None)
	{
		return eRestrictBagID;
	}
	// Find the first bag with a free slot
	eRestrictBagID = inv_trh_GetBestRestrictBagHelper(pEnt, pItemDef, iCount, true, eExcludedFlags, pExtract);
	
	// If this isn't a bGive or the item is flagged as LockToRestrictBags, find the first valid bag
	if (eRestrictBagID == InvBagIDs_None && (!bGive || (pItemDef->flags & kItemDefFlag_LockToRestrictBags)))
	{
		eRestrictBagID = inv_trh_GetBestRestrictBagHelper(pEnt, pItemDef, iCount, false, eExcludedFlags, pExtract);
	}

	if (eRestrictBagID != InvBagIDs_None)
	{
		switch (pItemDef->eType)
		{
		default:
			BagID = eRestrictBagID;
			break;

		//some types should not reply with a bag when it is an item give
		case kItemType_Upgrade:
		case kItemType_ItemRecipe:
		case kItemType_ItemPowerRecipe:
			if (!bGive)
				BagID = eRestrictBagID;
			break;
		}
	}

	return BagID;
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Ilevelexp, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Hallegiance, .Hsuballegiance, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics");
InvBagIDs inv_trh_GetBestBagForItemDef(ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef, int iCount, bool bGive, GameAccountDataExtract *pExtract)
{
	bool found = false;
	InvBagIDs BagID = InvBagIDs_None;
	InvBagFlag eExcludedFlags = 0;

	if(ISNULL(pItemDef))
		return InvBagIDs_Inventory;

	PERFINFO_AUTO_START_FUNC();
	if(bGive && (pItemDef->flags & kItemDefFlag_BindOnEquip))
	{
		//Cannot move a given BOE into an equipment bag
		eExcludedFlags |= InvBagFlag_BindBag;
	}

 	if ( iCount <= 0 )
	{
		iCount = pItemDef->iStackLimit;
	}

	if (bGive && !itemdef_trh_VerifyUsageRestrictions(ATR_EMPTY_ARGS, pEnt, pEnt, pItemDef, 0, NULL))
	{
		eExcludedFlags |= InvBagFlag_EquipBag | InvBagFlag_DeviceBag;
	}
	
	if ( iCount > 0 )
	{
		//if the item has a restrict bag specified on it, try that
		if (eaiSize(&pItemDef->peRestrictBagIDs) > 0)
		{
			if (!bGive || !gConf.bDontAutoEquipUpgrades || !(pItemDef->eType == kItemType_Upgrade ||
				pItemDef->eType == kItemType_Weapon))
				BagID = inv_trh_GetBestRestrictBagForItemDef(pEnt, pItemDef, iCount, bGive, eExcludedFlags, pExtract);
		}
		else //figure out the best place for this item to be added
		{
			switch (pItemDef->eType)
			{
			default:
				BagID = InvBagIDs_Inventory;
				break;

			case kItemType_Numeric:
				BagID = InvBagIDs_Numeric;
				break;

			case kItemType_ItemRecipe:
			case kItemType_ItemPowerRecipe:
				if ( !bGive )
					BagID = InvBagIDs_Recipe;
				break;

			case kItemType_Callout:
				BagID = InvBagIDs_Callout;
				break;

			case kItemType_Lore:
				BagID = InvBagIDs_Lore;
				break;

			case kItemType_Token:
				BagID = InvBagIDs_Tokens;
				break;

			case kItemType_Title:
				BagID = InvBagIDs_Titles;
				break;

			//!!!!also need to check any slotting restrictions here
			case kItemType_Upgrade:
				PERFINFO_AUTO_START("Upgrade", 1);
				//search for first non-full equip bag
				if (!bGive && !gConf.bDontAutoEquipUpgrades)
				{
					int BagIdx;
					if (!inv_ent_trh_EquipLimitCheck(ATR_EMPTY_ARGS, pEnt, NULL, pItemDef, -1, -1, NULL, NULL))
					{
						PERFINFO_AUTO_STOP();
						break;
					}
					for(BagIdx=1; BagIdx<InvBagIDs_Max; BagIdx++)
					{
						NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, BagIdx, pExtract);

						if ( NONNULL(pBag) &&
							(invbag_trh_flags(pBag) & InvBagFlag_EquipBag) &&
							!inv_ent_trh_BagFull(ATR_EMPTY_ARGS, pEnt, BagIdx, GET_REF(pItemDef->hSlotID), pExtract) &&
							inv_trh_CanEquipUnique(ATR_EMPTY_ARGS, pItemDef, pBag) &&
							item_trh_ItemDefMeetsBagRestriction(pItemDef, pBag))
						{
							BagID = BagIdx;
							break;
						}
					}
				}
				PERFINFO_AUTO_STOP();
				break;

			//!!!!also need to check any slotting restrictions here
			case kItemType_Device:
				//search for first non-full device bag
				{
					int BagIdx;
					PERFINFO_AUTO_START("Device", 1);
					if (!inv_ent_trh_EquipLimitCheck(ATR_EMPTY_ARGS, pEnt, NULL, pItemDef, -1, -1, NULL, NULL))
					{
						PERFINFO_AUTO_STOP();
						break;
					}
					for(BagIdx=1; BagIdx<InvBagIDs_Max; BagIdx++)
					{
						NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, BagIdx, pExtract);

						if ( NONNULL(pBag) &&
							(invbag_trh_flags(pBag) & InvBagFlag_DeviceBag) && 
							!(invbag_trh_flags(pBag) & eExcludedFlags) &&
							inv_trh_CanEquipUnique(ATR_EMPTY_ARGS , pItemDef, pBag) &&
							item_trh_ItemDefMeetsBagRestriction(pItemDef, pBag))
						{
							if (!inv_ent_trh_BagFull(ATR_EMPTY_ARGS, pEnt, BagIdx, GET_REF(pItemDef->hSlotID), pExtract))
							{
								BagID = BagIdx;
								break;
							}
							if (inv_bag_trh_GetMaxDropCountWithDef(ATR_EMPTY_ARGS, pEnt, pBag, pItemDef) >= iCount)
							{
								BagID = BagIdx;
								break;
							}
						}
					}
					PERFINFO_AUTO_STOP();
					break;
				}
				//!!!!also need to check any slotting restrictions here
			case kItemType_Bag:
				//search for first non-full playerbags bag
				PERFINFO_AUTO_START("Bag", 1);
				{
					int BagIdx;
					for(BagIdx=1; BagIdx<InvBagIDs_Max; BagIdx++)
					{
						NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, BagIdx, pExtract);
						if ( NONNULL(pBag) && 
							(invbag_trh_flags(pBag) & InvBagFlag_PlayerBagIndex) &&
							!inv_ent_trh_BagFull(ATR_EMPTY_ARGS, pEnt, BagIdx, GET_REF(pItemDef->hSlotID), pExtract) )
						{
							// check to see if there are more free slots than we have restricted bags
							// games without permissions enabled will always have iNumRestricted == 0
							S32 iNumRestricted = GamePermissions_trh_GetNumberOfRestrictedBags(pEnt, pExtract);
							
							if(inv_bag_trh_GetNumEmptySlots(ATR_EMPTY_ARGS, pEnt, pBag, GET_REF(pItemDef->hSlotID)) > iNumRestricted)
							{
								BagID = BagIdx;
								break;
							}
						}
					}
				}
				PERFINFO_AUTO_START("Device", 1);
				break;

				//!!!!also need to check any slotting restrictions here
			case kItemType_Weapon:
				//search for first non-full weapon bag
				PERFINFO_AUTO_START("Weapon", 1);
				if (!bGive && !gConf.bDontAutoEquipUpgrades)
				{
					int BagIdx;
					if (!inv_ent_trh_EquipLimitCheck(ATR_EMPTY_ARGS, pEnt, NULL, pItemDef, -1, -1, NULL, NULL))
					{
						PERFINFO_AUTO_START("Device", 1);
						break;
					}
					for(BagIdx=1; BagIdx<InvBagIDs_Max; BagIdx++)
					{
						NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, BagIdx, pExtract);

						if ( NONNULL(pBag) &&
							(invbag_trh_flags(pBag) & (InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag)) &&
							!inv_ent_trh_BagFull(ATR_EMPTY_ARGS, pEnt, BagIdx, GET_REF(pItemDef->hSlotID), pExtract) )
						{
							BagID = BagIdx;
							break;
						}
					}
				}
				PERFINFO_AUTO_START("Device", 1);
				break;
			case kItemType_Injury:
				// Injuries should always be placed in injury bag
				{
					BagID = InvBagIDs_Injuries;
				}
			break;
			}
		}
	}

	if (BagID == InvBagIDs_None)
		BagID = InvBagIDs_Inventory;

	PERFINFO_AUTO_STOP();
	return BagID;
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
// ui expression routines

static int StaticCheckInvBag(ExprContext *context, MultiVal *pMV, char **estrError)
{
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}
	else if(pMV->str[0] && stricmp(pMV->str, MULTI_DUMMY_STRING)==0)
	{
		// This is a static check and the parameter is a function or variable
		// We can't static check this. --poz
	}
	else if(pMV->str[0] && -1==StaticDefineIntGetInt(InvBagIDsEnum,pMV->str))
	{
		estrPrintf(estrError, "Invalid %s %s", "InvBag", pMV->str);
		return false;
	}
	return true;
}

AUTO_STARTUP(ExpressionSCRegister);
void InvExprInit(void)
{
	exprRegisterStaticCheckArgumentType("InvBag", NULL, StaticCheckInvBag);
}

// Inputs: Entity, InvBag name and slot
// Return: Returns the ID of the first Power on the item in the given bag's slot.
//  Returns 0 for failure cases.
AUTO_EXPR_FUNC(UIGen);
U32 GetItemPowerIDForBagAndSlot(SA_PARAM_OP_VALID Entity *pent,
				   ACMD_EXPR_SC_TYPE(InvBag) const char* invBagName,
				   S32 slot)
{
	U32 uiID = 0;
	if(pent)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
		Item *pItem = inv_GetItemFromBag(pent,StaticDefineIntGetInt(InvBagIDsEnum,invBagName),slot,pExtract);
		if(pItem && pItem->ppPowers)
		{
			uiID = pItem->ppPowers[0]->uiID;
		}
	}
	return uiID;
}

// Inputs: Entity, InvBag name and slot
// Return: Returns the ID of the first Power on the item in the given bag's slot.
//  Returns 0 for failure cases.
AUTO_EXPR_FUNC(UIGen);
SA_RET_OP_VALID Item *GetItemForBagAndSlot(
	SA_PARAM_OP_VALID Entity *pent,
	ACMD_EXPR_SC_TYPE(InvBag) const char* invBagName,
	S32 slot)
{
	Item *pResult = NULL;

	if(pent)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
		pResult = inv_GetItemFromBag(pent,StaticDefineIntGetInt(InvBagIDsEnum,invBagName),slot,pExtract);
	}

	return pResult;
}


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Eainteriorunlocks, .pInventoryV2.Ppinventorybags")
ATR_LOCKS(pItem, ".Flags, .Hitem, .Id");
bool inv_trh_UnlockInterior( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem )
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pItem) && (pItem->flags & kItemFlag_Bound)) 
	{
		int i;
		ItemDef *pItemDef = GET_REF(pItem->hItem);

		if (pItemDef)
		{
			for (i = eaSize(&pEnt->pPlayer->eaInteriorUnlocks)-1; i >= 0; i--)
			{
				if (GET_REF(pEnt->pPlayer->eaInteriorUnlocks[i]->hDef) == GET_REF(pItemDef->hInterior))
				{
					break;
				}
			}
			if (i < 0)
			{
				NOCONST(InteriorRef) *pInteriorRef = StructCreateNoConst(parse_InteriorRef);
				COPY_HANDLE(pInteriorRef->hDef, pItemDef->hInterior);
				eaPush(&pEnt->pPlayer->eaInteriorUnlocks, pInteriorRef);	

				//Remove the item
				inv_ent_trh_RemoveItemByID(ATR_PASS_ARGS, pEnt, pItem->id, 1);
#ifdef GAMESERVER
				QueueRemoteCommand_RemoteInteriorUnlockCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);
#endif
				return true;
			}
		}
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys");
bool inv_trh_UnlockCostume( ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, PlayerCostume *pCostume, GameAccountDataExtract *pExtract) 
{
	int i;
	char *estrItem = NULL;
	if (NONNULL(pCostume)) 
	{
		if(pCostume->bAccountWideUnlock) {

			AttribValuePair *pPair = NULL;

			estrStackCreate(&estrItem);

			MicroTrans_FormItemEstr(&estrItem, GetShortProductName(), kMicroItemType_PlayerCostume, pCostume->pcName, 1);
			pPair = eaIndexedGetUsingString(&pExtract->eaCostumeKeys, estrItem);

			if(ISNULL(pPair)) {
				NOCONST(AttribValuePair) *pNewPair = StructCreateNoConst(parse_AttribValuePair);

				pNewPair->pchAttribute = StructAllocString(estrItem);
				pNewPair->pchValue = StructAllocString("1");
				eaPush(&pEnt->pPlayer->pPlayerAccountData->eaPendingKeys, pNewPair);
				estrDestroy(&estrItem);

				return true;
			}
			estrDestroy(&estrItem);

		} else {
			bool bFound = false;
			for (i = eaSize(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs)-1; i >= 0; i--) {
				if (REF_STRING_FROM_HANDLE(pEnt->pSaved->costumeData.eaUnlockedCostumeRefs[i]->hCostume) == RefSystem_StringFromReferent(pCostume)) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				NOCONST(PlayerCostumeRef) *pCostumeRef = StructCreateNoConst(parse_PlayerCostumeRef);
				SET_HANDLE_FROM_REFERENT("PlayerCostume", pCostume, pCostumeRef->hCostume);
				eaPush(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs, pCostumeRef);

				return true;
			}
		}
	}

	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys")
ATR_LOCKS(pItem, ".Flags, .Hitem, .pspecialprops.Hcostumeref");
bool inv_trh_UnlockCostumeOnItem( ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem, GameAccountDataExtract *pExtract )
{
	int i;
	bool bUnlockedSomething = false;
	char *estrItem = NULL;
	ItemDef *pItemDef = NULL;
	
	if (ISNULL(pEnt) || ISNULL(pEnt->pPlayer) || ISNULL(pItem) || !(pItem->flags & kItemFlag_Bound)) {
		return bUnlockedSomething;
	}

	if(!pExtract)
		return bUnlockedSomething;

	pItemDef = GET_REF(pItem->hItem);
	
	if ((pItem->flags & kItemFlag_Algo) && NONNULL(pItem->pSpecialProps)) 
		bUnlockedSomething = inv_trh_UnlockCostume(ATR_PASS_ARGS, pEnt, GET_REF(pItem->pSpecialProps->hCostumeRef),pExtract);

	if (NONNULL(pItemDef) && pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock && eaSize(&pItemDef->ppCostumes)) {
		for (i = eaSize(&pItemDef->ppCostumes)-1; i >= 0 ; i--) {
			bUnlockedSomething |= inv_trh_UnlockCostume(ATR_PASS_ARGS, pEnt, GET_REF(pItemDef->ppCostumes[i]->hCostumeRef),pExtract);
		}
	}

	estrDestroy(&estrItem);
	
#ifdef GAMESERVER

	if(bUnlockedSomething && NONNULL(pItemDef) && pItemDef->eType == kItemType_CostumeUnlock)
	{
		// bind item to make sure it can't be used by someone else
		pItem->flags |= kItemFlag_Bound;
	}
	
	//queue up remote routine
	if ( bUnlockedSomething 
		&& !bSilent )
	{
		QueueRemoteCommand_RemoteCostumeUnlockCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);
	}
#endif

	return bUnlockedSomething;
}

// A simple non-transaction check to see if this item has any costumes (costume mode kCostumeDisplayMode_Unlock) that are not unlocked
bool ItemEntHasUnlockedCostumes(const Entity *pEnt, const Item *pItem)
{
	bool bRet = false;
	ItemDef *pItemDef;
	S32 i;
	char *estrItem = NULL;
	GameAccountData *pData = NULL;

	if(!pEnt || !pItem || !pEnt->pPlayer)
	{
		return false;	
	}

	pItemDef = GET_REF(pItem->hItem);
	
	if(!pItemDef || pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock || eaSize(&pItemDef->ppCostumes) < 1)
	{
		return false;
	}

	pData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
	if(!pData)
	{
		return false;
	}
		
	for (i = 0; (!bRet && i < eaSize(&pItemDef->ppCostumes)); ++i)
	{
		S32 j;
		PlayerCostume *pCostume = GET_REF(pItemDef->ppCostumes[i]->hCostumeRef);
		
		if(!pCostume)
		{
			continue;
		}
		
		if(pCostume->bAccountWideUnlock)
		{
			AttribValuePair *pPair = NULL;

			if(!estrItem)
			{
				estrStackCreate(&estrItem);
			}
			else
			{
				estrClear(&estrItem);
			}

			MicroTrans_FormItemEstr(&estrItem, GetShortProductName(), kMicroItemType_PlayerCostume, pCostume->pcName, 1);
			pPair = eaIndexedGetUsingString(&pData->eaCostumeKeys, estrItem);
			
			if(pPair)
			{
				bRet = true;
				break;
			}
		}
		else
		{
			for(j = 0; j < eaSize(&pEnt->pSaved->costumeData.eaUnlockedCostumeRefs); ++j)
			{
				if(REF_STRING_FROM_HANDLE(pEnt->pSaved->costumeData.eaUnlockedCostumeRefs[j]->hCostume) == REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef))
				{
					bRet = true;
					break;
				}
			}
		}
	}

	estrDestroy(&estrItem);
	
	return bRet;
	
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
// Routines that log guild bank transactions

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Psaved.Savedname, .Pplayer.Publicaccountname")
	ATR_LOCKS(pGuild, ".Uoldbanklogidx, .Eabanklog")
	ATR_LOCKS(pItem, ".Flags, .Hitem, .Palgoprops.Ppitempowerdefrefs, .Pspecialprops.Pcontainerinfo.Hsavedpet, .Pspecialprops.Psupercritterpet.Pchname");
void inv_guild_trh_AddLog(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Item) *pItem, const char* pchItemDefName, InvBagIDs bagID, S32 iCount)
{
	static char pcAccountName[MAX_NAME_LEN+1];
	bool bRequiesTranslation;
	NOCONST(GuildBankLogEntry) *pLogEntry;

	if (ISNULL(pEnt) || ISNULL(pGuild) || iCount == 0)
	{
		return;
	}

	pLogEntry = StructCreateNoConst(parse_GuildBankLogEntry);
	pLogEntry->iEntID = pEnt->myContainerID;
	pLogEntry->pcEntName = StructAllocString(pEnt->pSaved->savedName);
	sprintf(pcAccountName, "@%s", pEnt->pPlayer->publicAccountName);
	pLogEntry->pcEntAccount = StructAllocString(pcAccountName);
	pLogEntry->iNumberMoved = iCount;
	pLogEntry->iTimestamp = timeSecondsSince2000();
	pLogEntry->eBag = bagID;
	if (NONNULL(pItem) && (pItem->flags & kItemFlag_Algo))
	{
		char **ppNames = NULL;
		S32 i;
		item_trh_GetNameUntranslated(pItem, 
			pEnt,
			&ppNames,
			&bRequiesTranslation);

		for(i = 0; i < eaSize(&ppNames); ++i)
		{
			NOCONST(GuildBankLogEntryNames) *pLogName = StructCreateNoConst(parse_GuildBankLogEntryNames);

			pLogName->pcItemName = allocAddString(ppNames[i]);
			eaPush(&pLogEntry->ppItemNames, pLogName);
		}

		eaClear(&ppNames);
		eaDestroy(&ppNames);
	}
	else if (NONNULL(pItem))
	{
		pLogEntry->pcItemDef = REF_STRING_FROM_HANDLE(pItem->hItem);
	}
	else
	{
		pLogEntry->pcItemDef = pchItemDefName;
	}

	// insert the bank log instead of pushing it onto the end and then removing indexes to fit GUILD_BANK_LOG_MAX_SIZE
	if(eaSize(&pGuild->eaBankLog) < GUILD_BANK_LOG_MAX_SIZE)
	{
		eaPush(&pGuild->eaBankLog, pLogEntry);
	}
	else
	{
		NOCONST(GuildBankLogEntry) *pOldLogEntry;

		// slow case for removing extra entries for old 500 size logs
		if(eaSize(&pGuild->eaBankLog) > GUILD_BANK_LOG_MAX_SIZE && pGuild->uOldBankLogIdx == 0)
		{
			S32 iRemoveNum = eaSize(&pGuild->eaBankLog) - GUILD_BANK_LOG_MAX_SIZE;
			S32 i;

			// destroy old structs
			for(i = 0; i < iRemoveNum; ++i)
			{
				pOldLogEntry = pGuild->eaBankLog[i];
				if(pOldLogEntry)
				{
					StructDestroyNoConst(parse_GuildBankLogEntry, pOldLogEntry);
				}
			}

			// remove extra range
			eaRemoveRange(&pGuild->eaBankLog, 0, iRemoveNum);
		}

		pOldLogEntry = pGuild->eaBankLog[pGuild->uOldBankLogIdx];
		if(pOldLogEntry)
		{
			StructDestroyNoConst(parse_GuildBankLogEntry, pOldLogEntry);
		}
		pGuild->eaBankLog[pGuild->uOldBankLogIdx] = pLogEntry;
		++pGuild->uOldBankLogIdx;
		if((S32)pGuild->uOldBankLogIdx >= eaSize(&pGuild->eaBankLog))
		{
			pGuild->uOldBankLogIdx = 0;
		}
	}
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


// AUTO_TRANS_HELPER;
// NOCONST(InventoryBag)* inv_trh_PlayerBagFromSlotIdxString(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, char* SlotIdx)
// {
// 	NOCONST(InventoryBag)*res;
// 	res = eaIndexedGetUsingString(&pEnt->pInventoryV2->ppInventoryBags,SlotIdx);
// 	return res;
// }

AUTO_TRANS_HELPER;
NOCONST(InventoryBag)* inv_trh_PlayerBagFromSlotIdx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int SlotIdx)
{
    NOCONST(InventoryBag)*res = NULL;
	switch(SlotIdx)
	{
		xcase 0:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 10 /* Literal InvBagIDs_PlayerBag1 */);
		xcase 1:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 11 /* Literal InvBagIDs_PlayerBag2 */);
		xcase 2:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 12 /* Literal InvBagIDs_PlayerBag3 */);
		xcase 3:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 13 /* Literal InvBagIDs_PlayerBag4 */);
		xcase 4:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 14 /* Literal InvBagIDs_PlayerBag5 */);
		xcase 5:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 15 /* Literal InvBagIDs_PlayerBag6 */);
		xcase 6:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 16 /* Literal InvBagIDs_PlayerBag7 */);
		xcase 7:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 17 /* Literal InvBagIDs_PlayerBag8 */);
		xcase 8:
			res = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 18 /* Literal InvBagIDs_PlayerBag9 */);
		xdefault:
			TRANSACTION_APPEND_LOG_FAILURE("PlayerBag slot out of range: %i", SlotIdx);
	}
    return res;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
ATR_LOCKS(pBag, ".Inv_Def, .Bagid");
bool inv_trh_PlayerBagFail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pBag, int SlotIdx )
{

	if (invbag_trh_flags(pBag) & InvBagFlag_PlayerBagIndex )
	{
		NOCONST(InventoryBag)* pPlayerBag = inv_trh_PlayerBagFromSlotIdx(ATR_PASS_ARGS, pEnt, SlotIdx );

		if ( NONNULL(pPlayerBag) )
		{
			if  ( !inv_bag_trh_BagEmpty(ATR_PASS_ARGS, pPlayerBag) )
				return true;
		}
	}

	return false;
}

//on the server only, there are special actions that must be taken when an item is added to an entity
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds")
ATR_LOCKS(pItem, ".Flags, .Id, .Hitem, .Pspecialprops.Hcostumeref, .Pppowers, .Palgoprops.Ppitempowerdefrefs, .Pspecialprops.Pcontainerinfo.Hsavedpet, .Palgoprops.Quality, .Pspecialprops.Ppitemgemslots, .Pspecialprops.Psupercritterpet.Pchname")
ATR_LOCKS(pDestBag, ".Bagid, .Inv_Def");
void inv_trh_ItemAddedCallbacks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem, ATH_ARG NOCONST(InventoryBag)* pDestBag, int DestSlotIdx, int count, bool bForceBind, const ItemChangeReason *pReason)
{
	ItemDef* pItemDef = NULL;

	// ab: this was outside of this function, but called in the exact same place everywhere this function was called.
	// moving it to this location to maintain identical behavior. 
#ifdef GAMESERVER

	if (NONNULL(pEnt) && NONNULL(pDestBag))
	{
		QueueRemoteCommand_eventsend_RemoteRecordBagGetsItem(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pDestBag->BagID);
	}

#endif


#ifndef GAMECLIENT
	if ( ISNULL(pEnt) ||
		 ISNULL(pItem) )
		 return;

	pItemDef = GET_REF(pItem->hItem);

	if ( ISNULL(pItemDef) )
		return;

	//init the item ID		
	item_trh_SetItemID(pEnt, pItem);

	//fixup the power IDs
	item_trh_FixupPowerIDs(pEnt, pItem);

	if((pItem->flags & kItemFlag_BoundToAccount) == 0)
	{
		// account bound items
		if (pItemDef->flags & kItemDefFlag_BindToAccountOnPickup)
		{
			pItem->flags |= kItemFlag_BoundToAccount;
		}

		if (invbag_trh_flags(pDestBag) & InvBagFlag_BindBag)
		{
			if (pItemDef->flags & kItemDefFlag_BindToAccountOnEquip)
			{
				pItem->flags |= kItemFlag_BoundToAccount;
			}
		}
	}

	if ( !(pItem->flags & kItemFlag_Bound) )	//bind the item if it needs to be bound
	{
		if (bForceBind || (pItemDef->flags & kItemDefFlag_BindOnPickup))
		{
			pItem->flags |= kItemFlag_Bound;
		}

		if (invbag_trh_flags(pDestBag) & InvBagFlag_BindBag)
		{
			if (pItemDef->flags & kItemDefFlag_BindOnEquip)
			{
				pItem->flags |= kItemFlag_Bound;
			}
		}

#ifdef GAMESERVER
		//Special GameServer only handling about what happens right after an item gets bound
		if(pItem->flags & kItemFlag_Bound)
		{
			if ((pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock || (pItem->flags & kItemFlag_Algo)) && 
				(eaSize(&pItemDef->ppCostumes) || (NONNULL(pItem->pSpecialProps) && GET_REF(pItem->pSpecialProps->hCostumeRef))))
			{
				QueueRemoteCommand_RemoteScheduleCostumeUnlockCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pItem->id);
			}
			if (GET_REF(pItemDef->hInterior))
			{
				QueueRemoteCommand_RemoteScheduleInteriorUnlockCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pItem->id);
			}
		}
#endif
	}


	// Update the Entity's Build
	if ( (invbag_trh_flags(pDestBag) & (InvBagFlag_EquipBag | InvBagFlag_DeviceBag | InvBagFlag_ActiveWeaponBag) ) &&
		 (DestSlotIdx >= 0) )
	{
		inv_ent_trh_BuildCurrentSetItem(ATR_PASS_ARGS, pEnt, bSilent, invbag_trh_bagid(pDestBag), DestSlotIdx, pItem->id);
	}

#ifdef GAMESERVER
	if(pEnt->myEntityType!=GLOBALTYPE_NONE) // Don't bother with fake entities
	{
		ItemTransCBData *pData = StructCreate(parse_ItemTransCBData);

		if (pItemDef->flags & kItemDefFlag_Unique)
			inv_ent_trh_RegisterUniqueItem(ATR_PASS_ARGS, pEnt, pItemDef->pchName, true);

		item_trh_GetNameUntranslated(pItem, pEnt, &pData->ppchNamesUntranslated, &pData->bTranslateName);
		if (NONNULL(pItemDef))
			pData->pchItemDefName = StructAllocString(pItemDef->pchName);
		pData->iCount = count;
		pData->kQuality = item_trh_GetQuality(pItem); 

		// store the destination bag
		pData->eBagID = invbag_trh_bagid(pDestBag);
		pData->iBagSlot = DestSlotIdx;
		pData->uiItemID = pItem->id;

		if (pReason)
		{
			pData->bFromStore = pReason->bFromStore;
		}

		//queue up remote routine to flag add event (ignore silent flag if the player is learning a re
		if (!bSilent)
		{
			QueueRemoteCommand_RemoteAddItemCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
			QueueRemoteCommand_RemoteAddItemEventCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
		}

		QueueRemoteCommand_RemoteItemMovedActions(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);

		//if dest bag is a PlayerBagIndex bag then queue up update player bags command
		if ( invbag_trh_flags(pDestBag) & InvBagFlag_PlayerBagIndex )
			QueueRemoteCommand_RemoteUpdatePlayerBags(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);

		//if the item has powers that need to be updated
		if ( item_trh_GetNumItemPowerDefs(pItem, true) > 0 
			|| item_trh_GetNumGemsPowerDefs(pItem) > 0)
		{
			QueueRemoteCommand_RemoteDirtyInnateEquipment(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);
		}

		// If the Item is part of one or more ItemSets and the bag is an EquipBag
		if(pItemDef && eaSize(&pItemDef->ppItemSets) && 
			(invbag_trh_flags(pDestBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag)))
		{
			GlobalType eOwnerType = pEnt->myEntityType;
			ContainerID uOwnerID = pEnt->myContainerID;
			if (NONNULL(pEnt->pSaved) && pEnt->pSaved->conOwner.containerID)
			{
				eOwnerType = pEnt->pSaved->conOwner.containerType;
				uOwnerID = pEnt->pSaved->conOwner.containerID;
			}
			QueueRemoteCommand_RemoteUpdateItemSets(ATR_RESULT_SUCCESS, eOwnerType, uOwnerID, pEnt->myEntityType, pEnt->myContainerID);
		}

		eaClear(&pData->ppchNamesUntranslated);
		StructDestroy(parse_ItemTransCBData, pData);
	}
#endif
#endif
}

//on the server only, there are special actions that must be taken when an item is added to an entity
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype")
ATR_LOCKS(pItem, ".Hitem, .Flags, .Palgoprops.Ppitempowerdefrefs, .Pspecialprops.Pcontainerinfo.Hsavedpet, .Pspecialprops.Ppitemgemslots, .Pspecialprops.Psupercritterpet.Pchname")
ATR_LOCKS(pSrcBag, ".Bagid, .Inv_Def");
void inv_trh_ItemRemovedCallbacks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(Item)* pItem, ATH_ARG NOCONST(InventoryBag)* pSrcBag, int SrcSlotIdx, int count, const ItemChangeReason *pReason )
{
	ItemDef* pItemDef = NULL;

#ifdef GAMESERVER
	if ( ISNULL(pEnt) ||
		 ISNULL(pItem) )
		 return;

	pItemDef = GET_REF(pItem->hItem);

	if ( ISNULL(pItemDef) )
		return;

	if(pEnt->myEntityType!=GLOBALTYPE_NONE) // Don't bother with fake entities
	{
		ItemTransCBData *pData = StructCreate(parse_ItemTransCBData);

		if (pItemDef->flags & kItemDefFlag_Unique)
			inv_ent_trh_RegisterUniqueItem(ATR_PASS_ARGS, pEnt, pItemDef->pchName, false);

		item_trh_GetNameUntranslated(pItem, pEnt, &pData->ppchNamesUntranslated, &pData->bTranslateName);
		pData->pchItemDefName = StructAllocString(pItemDef->pchName);
		pData->iCount = count;
		pData->eBagID = invbag_trh_bagid(pSrcBag);
		pData->iBagSlot = SrcSlotIdx; 

		//queue up remote routine to flag remove event
		if (!bSilent)
		{
			QueueRemoteCommand_RemoteRemoveItemCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
			QueueRemoteCommand_RemoteRemoveItemEventCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);
			if((pEnt->myEntityType != GLOBALTYPE_ENTITYCRITTER) && 
			   (pItemDef->bLogForTracking) &&
			   (pItemDef->eType != kItemType_Numeric))
			{
				TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_ITEMTRACKING, "ItemRemoved",
 						"Item \"%s\" Count %d Reason \"%s\" Detail \"%s\" Map \"%s\" Pos \"%g,%g,%g\"",
						pItemDef->pchName, 
						count,
						(pReason && pReason->pcReason ? pReason->pcReason : "Unknown"), 
						(pReason && pReason->pcDetail ? pReason->pcDetail : "Unknown"),
						(pReason && pReason->pcMapName ? pReason->pcMapName : "Unknown"),
						(pReason ? pReason->vPos[0] : 0.0),
						(pReason ? pReason->vPos[1] : 0.0),
						(pReason ? pReason->vPos[2] : 0.0)
						);
			}
		}

		QueueRemoteCommand_RemoteItemMovedActions(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pData);

		//if dest bag is a PlayerBagIndex bag then queue up update player bags command
		if ( invbag_trh_flags(pSrcBag) & InvBagFlag_PlayerBagIndex )
			QueueRemoteCommand_RemoteUpdatePlayerBags(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);

		//if the item has powers that need to be updated
		if ( item_trh_GetNumItemPowerDefs(pItem, true) > 0 
			|| item_trh_GetNumGemsPowerDefs(pItem) > 0)
		{
			QueueRemoteCommand_RemoteDirtyInnateEquipment(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);
		}

		// If the Item is part of one or more ItemSets and the bag is an EquipBag
		if(eaSize(&pItemDef->ppItemSets) && 
			(invbag_trh_flags(pSrcBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag)))
		{
			GlobalType eOwnerType = pEnt->myEntityType;
			ContainerID uOwnerID = pEnt->myContainerID;
			if (NONNULL(pEnt->pSaved) && pEnt->pSaved->conOwner.containerID)
			{
				eOwnerType = pEnt->pSaved->conOwner.containerType;
				uOwnerID = pEnt->pSaved->conOwner.containerID;
			}
			QueueRemoteCommand_RemoteUpdateItemSets(ATR_RESULT_SUCCESS, eOwnerType, uOwnerID, pEnt->myEntityType, pEnt->myContainerID);
		}

		eaClear(&pData->ppchNamesUntranslated);
		StructDestroy(parse_ItemTransCBData, pData);
	}
#endif
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
void inv_trh_ItemRemovedCallbacksBySlot(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, InvBagIDs BagID, int iSlot, int count, const ItemChangeReason *pReason , GameAccountDataExtract *pExtract)
{
	NOCONST(Item)* pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, BagID, iSlot, pExtract);
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);

	inv_trh_ItemRemovedCallbacks(ATR_PASS_ARGS, pEnt, bSilent, pItem, pBag, iSlot, count, pReason);
}

AUTO_EXPR_FUNC(UIGen, Player, Mission) ACMD_NAME(CanBuyBagPlayer);
bool GamePermissions_CanBuyBagPlayer(ExprContext* context, const char *pchBagID)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, "Player");
	if(pEntity)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum,pchBagID);
		bool bResult = GamePermissions_trh_CanBuyBag(CONTAINER_NOCONST(Entity, pEntity), iBagID, pExtract);

		return bResult;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen, Player, Mission) ACMD_NAME(GetNumericItemValue);
S32 ExprGetNumericItemValue(ExprContext* context, const char* pchItemName)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, "Player");

	if (playerEnt)
		return inv_GetNumericItemValue(playerEnt, pchItemName);

	return 0;
}

AUTO_EXPR_FUNC(UIGen, Player, Mission) ACMD_NAME(GetBankSizeNumerics);
S32 ExprGetBankSizeNumerics(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, "Player");

	if (playerEnt)
	{
		return inv_GetNumericItemValue(playerEnt, "BankSize") + inv_GetNumericItemValue(playerEnt, "BankSizeMicrotrans");
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen, Player) ACMD_NAME(GetGuildNumericItemValue);
S32 ExprGetGuildNumericItemValue(ExprContext* context, const char* pchItemName)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(context, "Player");
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);

	if (pGuild && pGuildBank) {
		return inv_guildbank_GetNumericItemValue(pGuildBank, InvBagIDs_Numeric, pchItemName);
	}
	
	return 0;
}

AUTO_EXPR_FUNC(UIGen, Player) ACMD_NAME(GetNumericItemValueScaled);
S32 ExprGetNumericItemValueScaled(ExprContext* context, const char* pchItemName)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, "Player");

	if (playerEnt)
		return inv_GetNumericItemValueScaled(playerEnt, pchItemName);

	return 0;
}

AUTO_EXPR_FUNC(UIGen, Player) ACMD_NAME(GetNumericItemValueForEnt);
S32 ExprGetNumericItemValueForEnt(ExprContext* context, SA_PARAM_OP_VALID Entity* pEnt, const char* pchItemName)
{
	if (pEnt)
		return inv_GetNumericItemValue(pEnt, pchItemName);

	return 0;
}

AUTO_EXPR_FUNC(UIGen, Player) ACMD_NAME(GetNumericItemValueScaledForEnt);
S32 ExprGetNumericItemValueScaledForEnt(ExprContext* context, SA_PARAM_OP_VALID Entity* pEnt, const char* pchItemName)
{
	if (pEnt)
		return inv_GetNumericItemValueScaled(pEnt, pchItemName);

	return 0;
}

AUTO_EXPR_FUNC(UIGen, Player) ACMD_NAME(HasToken);
S32 ExprHasToken(ExprContext* context, const char* pchItemName)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, "Player");

	if (playerEnt)
		return inv_HasToken(playerEnt, pchItemName);

	return 0;
}

AUTO_TRANS_HELPER;
const char* inv_trh_GetBagFullMessageKey(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs eBagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, eBagID, pExtract);
	InvBagDef const *pBagDef = invbag_trh_def(pBag);
	if (pBagDef && GET_REF(pBagDef->msgBagFull.hMessage))
	{
		return REF_STRING_FROM_HANDLE(pBagDef->msgBagFull.hMessage);
	}
	return INVENTORY_FULL_MSG;
}


AUTO_TRANS_HELPER
	ATR_LOCKS(bag, ".Inv_Def, .Bagid");
int invbag_trh_flags(ATH_ARG NOCONST(InventoryBag) *bag)
{
	InvBagDef const *bag_def = invbag_trh_def(bag);
	if( ISNULL(bag_def) )
		return 0;
	else
		return bag_def->flags;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(bag, ".Inv_Def, .Bagid");
int invbaglite_trh_flags(ATH_ARG NOCONST(InventoryBagLite) *bag)
{
	InvBagDef const *bag_def = invbaglite_trh_def(bag);
	if( ISNULL(bag_def) )
		return 0;
	else
		return bag_def->flags;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(bag, ".Inv_Def, .Bagid");
U8 invbag_trh_costumesetindex(ATH_ARG NOCONST(InventoryBag) *bag)
{
	InvBagDef const *bag_def = invbag_trh_def(bag);
	if( ISNULL(bag_def) )
		return 0;
	else
		return bag_def->iCostumeSetIndex;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(bag, ".Inv_Def, .Bagid");
InvBagType invbag_trh_type(ATH_ARG NOCONST(InventoryBag) *bag)
{
	InvBagDef const *bag_def = invbag_trh_def(bag);
	if( ISNULL(bag_def) )
		return 0;
	else
		return bag_def->eType;
}
AUTO_TRANS_HELPER
ATR_LOCKS(bag, ".Bagid");
int invbag_trh_bagid(ATH_ARG NOCONST(InventoryBag) *bag)
{
    if( ISNULL(bag) )
		return 0;
	else
		return(bag->BagID);
}

static int cmpBagSlotTableEntries(const InvBagSlotTableEntry** ppEntryA, const InvBagSlotTableEntry** ppEntryB)
{
	const InvBagSlotTableEntry* pEntryA = (*ppEntryA);
	const InvBagSlotTableEntry* pEntryB = (*ppEntryB);

	return pEntryA->iNumericValue - pEntryB->iNumericValue;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[]");
int invbag_trh_GetMaxSlotsFromTable(ATH_ARG NOCONST(Entity) *pEnt, InvBagSlotTable* pBagSlotTable, const char* pchTableNumeric)
{
	InvBagSlotTableEntry* pEntry;
	InvBagSlotTableEntry SearchEntry = {0};
	int iIndex;

	pEntry = &SearchEntry;
	SearchEntry.iNumericValue = inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pEnt, pchTableNumeric);
	iIndex = (int)eaBFind(pBagSlotTable->eaEntries, cmpBagSlotTableEntries, pEntry);
	pEntry = eaGet(&pBagSlotTable->eaEntries, iIndex);
	
	if (pEntry && pEntry->iNumericValue != SearchEntry.iNumericValue)
	{
		pEntry = eaGet(&pBagSlotTable->eaEntries, iIndex-1);
	}
	if (pEntry)
	{
		return pEntry->iMaxSlots;
	}
	return 0;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Playertype, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pBag, ".Inv_Def, .Bagid");
int invbag_trh_basemaxslots(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pBag)
{
    InvBagDef const *bag_def = invbag_trh_def(pBag);
	InvBagSlotTable* pBagSlotTable;
    const char* pchTableNumeric = NULL;

    if(ISNULL(bag_def))
        return 0;
    if(bag_def->MaxSlots<0 && !bag_def->pchMaxSlotTable)
        return -1;
    
    // Use the premium max slot table numeric if the player is premium.
    if ( NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && ( pEnt->pPlayer->playerType >= kPlayerType_Premium ) )
    {
        pchTableNumeric = REF_STRING_FROM_HANDLE(bag_def->hMaxSlotTableNumericPremium);
    }
    
    // If player is not premium, or there is no premium numeric specified, then use the standard numeric.
    if ( pchTableNumeric == NULL )
    {
        pchTableNumeric = REF_STRING_FROM_HANDLE(bag_def->hMaxSlotTableNumericStandard);
    }

	if(pBagSlotTable = inv_FindBagSlotTableByName(bag_def->pchMaxSlotTable))
	{
		return invbag_trh_GetMaxSlotsFromTable(pEnt, pBagSlotTable, pchTableNumeric);
	}
	return bag_def->MaxSlots;
}

S32 invbag_BaseSlotsByName(Entity *pEntity, const char *pcBagName, GameAccountDataExtract *pExtract)
{
	S32 iNumSlots = 0;
	if(pEntity && pcBagName)
	{
		S32 iPlayerBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
		NOCONST(InventoryBag) *pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), iPlayerBagID, pExtract);
		if(pBag)
		{
			iNumSlots = invbag_trh_basemaxslots(CONTAINER_NOCONST(Entity, pEntity), pBag);
		}
	}
	
	return iNumSlots;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], .Pplayer.Playertype")
ATR_LOCKS(pBag, ".N_Additional_Slots, .Inv_Def, .Bagid");
int invbag_trh_maxslots(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pBag)
{
	int iBaseMaxSlots = invbag_trh_basemaxslots(pEnt, pBag);
	if (iBaseMaxSlots < 0 || ISNULL(pBag))
		return iBaseMaxSlots;

	return iBaseMaxSlots + pBag->n_additional_slots;
}

// Indicates if the bag contains reportable items
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".Bagid, .Inv_Def");
bool invbag_trh_isReportable(ATH_ARG NOCONST(InventoryBag) *pBag)
{
	if(ISNULL(pBag))
		return false;

	// Bag type check
	if (pBag->BagID == InvBagIDs_Inventory)
		return true;

	// Get the flags for the bag
	return invbag_trh_flags(pBag) & InvBagFlag_Reportable;
}

S32 invbag_GetActiveSlot(InventoryBag *pBag, S32 activeIndex)
{
	if (!pBag || activeIndex < 0)
		return -1;
	
#ifdef GAMECLIENT
	if (activeIndex < eaiSize(&pBag->eaiPredictedActiveSlots))
	{
		return pBag->eaiPredictedActiveSlots[activeIndex];
	}
#endif
	if (!eaiSize(&pBag->eaiActiveSlots) && !eaiSize(&pBag->eaiPredictedActiveSlots))
	{
		const InvBagDef* pDef = invbag_def(pBag);
		if (!pDef)
			return -1;
		if (pDef->maxActiveSlots == 1 && activeIndex == 0) 
			return 0;
		return -1;
	}
	if (activeIndex < eaiSize(&pBag->eaiActiveSlots))
	{
		return pBag->eaiActiveSlots[activeIndex];	
	}
	
	return -1;
}

Item* invbag_GetActiveSlotItem(InventoryBag *pBag, S32 activeIndex)
{
	S32 slot;
	if (!pBag)
		return NULL;

	slot = invbag_GetActiveSlot(pBag, activeIndex);
	if (slot == -1)
		return NULL;
	
	return inv_bag_GetItem(pBag, slot);
}

bool invbag_IsActiveSlot(InventoryBag *pBag, S32 iSlot)
{
	if (!pBag || iSlot < 0)
		return false;

	if (!eaiSize(&pBag->eaiActiveSlots) && !eaiSize(&pBag->eaiPredictedActiveSlots))
	{
		const InvBagDef* pDef = invbag_def(pBag);
		if (pDef && pDef->maxActiveSlots == 1 && iSlot == 0) 
		{
			return true;
		}
	}
	else
	{
		S32 i, iSize = MAX(eaiSize(&pBag->eaiActiveSlots), eaiSize(&pBag->eaiPredictedActiveSlots));
		for (i = iSize-1; i >= 0; i--)
		{
			if (invbag_GetActiveSlot(pBag, i) == iSlot)
			{
				return true;
			}
		}
	}
	return false;
}

int invbag_maxActiveSlots(InventoryBag const *pBag) 
{ 
	const InvBagDef* pDef = invbag_def(pBag);
	return pDef ? pDef->maxActiveSlots : 0;
}

bool invbag_IsValidActiveSlotChange(Entity* pEnt, InventoryBag* pBag, int* piActiveSlots, S32 iActiveSlotIndex, S32 iNewActiveSlot)
{
	const InvBagDef* pDef = invbag_def(pBag);
	if (!pDef || !(pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		return false;
	}
	if (iActiveSlotIndex < 0 || iActiveSlotIndex >= pDef->maxActiveSlots)
	{	
		return false;
	}
	if (iNewActiveSlot >= invbag_maxslots(pEnt, pBag))
	{
		return false;
	}
	if (iActiveSlotIndex < eaiSize(&piActiveSlots) &&
		piActiveSlots[iActiveSlotIndex] == iNewActiveSlot)
	{
		return false;
	}
	return true;
}

bool invbag_CanChangeActiveSlot(Entity* pEnt, InventoryBag* pBag, S32 iActiveSlotIndex, S32 iNewActiveSlot)
{
	const InvBagDef* pDef = invbag_def(pBag);
	
	if (!pEnt || !pEnt->pChar || !pEnt->pInventoryV2)
	{
		return false;
	}
	if (!invbag_IsValidActiveSlotChange(pEnt, pBag, pBag->eaiActiveSlots, iActiveSlotIndex, iNewActiveSlot))
	{
		return false;
	}
	if (pEnt->pChar->pPowActCurrent || pEnt->pChar->pPowActQueued)
	{
		return false;
	}
	return true;
}

void invbag_HandleMoveEvents(Entity* pEnt, InventoryBag* pBag, S32 iNewActiveSlot, U32 uiTime, U32 uiRequestID, bool bCheckActiveSlot)
{
	const InvBagDef* pBagDef = invbag_def(pBag);
	if (pEnt && pEnt->pChar && pBagDef)
	{
		S32 i;
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		for (i = eaSize(&pBagDef->ppItemMoveEvents) - 1; i >= 0; i--)
		{
			const InvBagItemMoveEvent* pItemMoveEvent = pBagDef->ppItemMoveEvents[i];

			// Set the cooldown if a category is specified on the bag def
			if (IsServer() && pItemMoveEvent->fCooldownTime > 0.0f)
			{
				if (pItemMoveEvent->pchPowerCooldownCategory && pItemMoveEvent->pchPowerCooldownCategory[0])
				{
					S32 eCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pItemMoveEvent->pchPowerCooldownCategory);
					if (eCategory >= 0)
					{
						F32 fCooldown = uiRequestID ? pItemMoveEvent->fCooldownTime : 0.0f;
						character_CategorySetCooldown(iPartitionIdx,pEnt->pChar,eCategory,fCooldown);
					}
				}
			}

			// Play flash bits
			if (uiRequestID && pItemMoveEvent->ppchFlashBits && pItemMoveEvent->ppchFlashBits[0])
			{
				EntityRef erSource = entGetRef(pEnt);
				character_FlashBitsOn(pEnt->pChar,uiRequestID,0,kPowerAnimFXType_WeaponSwap,erSource,pItemMoveEvent->ppchFlashBits,uiTime,false,false,false,false);
			}
		}

		// Change stances
		if ((pBagDef->flags & InvBagFlag_ActiveWeaponBag) &&
			(!bCheckActiveSlot || invbag_IsActiveSlot(pBag, iNewActiveSlot)))
		{
			bool bRequireStanceChange = true;
			U32 uiStanceID = SAFE_MEMBER(pEnt->pChar->pPowerRefPersistStance, uiID);
			Power* pPow = NULL;
			InventorySlot* pSlot = eaGet(&pBag->ppIndexedInventorySlots, iNewActiveSlot);

			if (pSlot && pSlot->pItem)
			{
				for (i = 0; i < eaSize(&pSlot->pItem->ppPowers); i++)
				{
					Power* pItemPow = pSlot->pItem->ppPowers[i];
						
					if (pItemPow->uiID == uiStanceID)
					{
						bRequireStanceChange = false;
						break;
					}
					if (!pPow)
					{
						PowerDef* pItemPowDef = GET_REF(pItemPow->hDef);
						PowerAnimFX* pItemAnimFX = pItemPowDef ? GET_REF(pItemPowDef->hFX) : NULL;
						if (pItemAnimFX && 
							(pItemAnimFX->ppchPersistStanceStickyBits || 
								pItemAnimFX->ppchPersistStanceStickyFX))
						{
							// Enter the first valid persist stance
							pPow = pItemPow;
						}
					}
				}
			}
			if (bRequireStanceChange)
			{
				PowerDef* pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
				bool bInactiveStance = (pEnt->pChar->bPersistStanceInactive || !pEnt->pChar->pPowerRefPersistStance);
				if (!bInactiveStance)
				{
					character_EnterStance(iPartitionIdx, pEnt->pChar, NULL, NULL, true, uiTime);
				}
				character_EnterPersistStance(iPartitionIdx,pEnt->pChar,pPow,pPowDef,NULL,uiTime,uiRequestID,bInactiveStance);
			}
		}
	}
}

bool inv_AddActiveSlotChangeRequest(Entity* pEnt, 
									InventoryBag* pBag, 
									S32 iIndex, 
									S32 iNewActiveSlot, 
									U32 uiRequestID,
									F32 fDelayOverride)
{
	bool bSuccess = false;
	const InvBagDef* pDef = invbag_def(pBag);
	if (pEnt && pEnt->pInventoryV2 && pDef)
	{
		S32 i, iNumSwitchRequests = eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest);
		S32 iActiveSlots = MAX(eaiSize(&pBag->eaiActiveSlots), eaiSize(&pBag->eaiPredictedActiveSlots));
		int* piActiveSlots = NULL;

		for (i = 0; i < iActiveSlots; i++)
		{
			eaiPush(&piActiveSlots, invbag_GetActiveSlot(pBag, i));
		}
		for (i = 0; i < iNumSwitchRequests; i++)
		{
			InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[i];
			
			if (pRequest->eBagID == pBag->BagID && pRequest->iIndex < eaiSize(&piActiveSlots))
			{
				piActiveSlots[pRequest->iIndex] = pRequest->iNewActiveSlot;
			}
		}

		if (invbag_IsValidActiveSlotChange(pEnt, pBag, piActiveSlots, iIndex, iNewActiveSlot))
		{
			InvBagSlotSwitchRequest *pRequest = StructCreate(parse_InvBagSlotSwitchRequest);
			pRequest->eBagID = pBag->BagID;
			pRequest->iNewActiveSlot = iNewActiveSlot;
			pRequest->iIndex = iIndex;
			pRequest->uRequestID = uiRequestID;
			// Use the delay override value if it's greater than the default delay
			if (fDelayOverride > pDef->fChangeActiveSlotDelay) {
				pRequest->fDelay = fDelayOverride;
			} else {
				pRequest->fDelay = pDef->fChangeActiveSlotDelay;
			}
			eaPush(&pEnt->pInventoryV2->ppSlotSwitchRequest,pRequest);
			bSuccess = true;
		}

		eaiDestroy(&piActiveSlots);
	}
	return bSuccess;
}

//Checks whether an entity has sufficient items to craft the given recipe.
bool inv_EntCouldCraftRecipe(Entity* pEnt, ItemDef* pRecipe)
{
	ItemCraftingComponent **eaComponents = NULL;
	int i;
	int count;

	item_GetAlgoIngredients(pRecipe, &eaComponents, 0, 0, 0);

	for(i = eaSize(&eaComponents)-1; i>=0; i--) {
		ItemDef *pPart = GET_REF(eaComponents[i]->hItem);

		if (!pPart) {
			return false;
		}

		count = inv_ent_AllBagsCountItems(pEnt, pPart->pchName);

		if (count < eaComponents[i]->fCount) {
			return false;
		}
	}
	eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);

	return true;
}

// Pushes all equipped Items that have the ItemCategory into the earray
void inv_ent_FindEquippedItemsByCategory(Entity *pEnt, ItemCategory eCategory, Item ***pppItems, GameAccountDataExtract *pExtract)
{
	int i,j;
	InventoryList list = {0};

	if(pEnt && pEnt->pInventoryV2)
	{
		int iBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
		for(i=0; i<iBags; i++)

		{
			if(bag_IsEquipBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract) || bag_IsWeaponBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract))
			{
				list.count = 0;
				inv_bag_GetItemList(pEnt->pInventoryV2->ppInventoryBags[i],NULL,&list);

				for(j=list.count-1; j>=0; j--)
				{
					InventoryListEntry *pEntry = &list.Entry[j];
					Item *pItem = pEntry->pSlot->pItem;
					ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

					if (!pItemDef) 
						continue;

					if(-1!=eaiFind(&pItemDef->peCategories,eCategory))
					{
						eaPush(pppItems,pItem);
					}
				}
			}
		}
	}
}

// Pushes all equipped Items that have the ItemCategory into the earray
void inv_ent_FindItemsInBagByCategory(Entity *pEnt, ItemCategory eCategory, const char* pchBagName, Item ***pppItems, GameAccountDataExtract *pExtract)
{
	int i;
	InventoryList list = {0};

	if(pEnt && pEnt->pInventoryV2)
	{
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), StaticDefineIntGetInt(InvBagIDsEnum, pchBagName), pExtract);
		list.count = 0;
		inv_bag_GetItemList(pBag,NULL,&list);

		for(i=list.count-1; i>=0; i--)
		{
			InventoryListEntry *pEntry = &list.Entry[i];
			Item *pItem = pEntry->pSlot->pItem;
			ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

			if (!pItemDef) 
				continue;

			if(-1!=eaiFind(&pItemDef->peCategories,eCategory))
			{
				eaPush(pppItems,pItem);
			}
		}
	}
}

bool inv_ent_HasAllItemCategoriesEquipped(Entity *pEnt, int * piCategories, GameAccountDataExtract *pExtract, int *piMissingCategory)
{
	int* eaCategoriesCopy = NULL;
	int i,j,k;
	InventoryList list = {0};
	bool ret = false;

	// If they don't require any categories, then we're good
	if (piCategories == NULL || (eaiSize(&piCategories) == 0))
		return true;

	eaiCopy(&eaCategoriesCopy, &piCategories);
	if(pEnt && pEnt->pInventoryV2)
	{
		int iBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
		for(i=0; i<iBags; i++)
		{
			if(bag_IsEquipBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract) || bag_IsWeaponBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract))
			{
				list.count = 0;
				inv_bag_GetItemList(pEnt->pInventoryV2->ppInventoryBags[i],NULL,&list);

				for(j=list.count-1; j>=0; j--)
				{
					bool bHasCategories = true;
					InventoryListEntry *pEntry = &list.Entry[j];
					Item *pItem = pEntry->pSlot->pItem;
					ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

					if (!pItemDef) 
						continue;

					for (k=0;k<eaiSize(&pItemDef->peCategories);k++)
					{
						eaiFindAndRemoveFast(&eaCategoriesCopy, pItemDef->peCategories[k]);
					}
					if (eaiSize(&eaCategoriesCopy) == 0)
					{
						eaiDestroy(&eaCategoriesCopy);
						return true;
					}
				}
			}
		}
	}

	*piMissingCategory = eaCategoriesCopy[0];

	eaiDestroy(&eaCategoriesCopy);
	return false;
}

// Pushes all equipped Items that have the ItemCategory into the earray
void inv_ent_FindEquippedItemsWithCategories(Entity *pEnt, int * piCategories, Item ***pppItems, GameAccountDataExtract *pExtract)
{
	int i,j,k;
	InventoryList list = {0};

	if(pEnt && pEnt->pInventoryV2)
	{
		int iBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
		for(i=0; i<iBags; i++)
		{
			if(bag_IsEquipBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract) || bag_IsWeaponBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract))
			{
				list.count = 0;
				inv_bag_GetItemList(pEnt->pInventoryV2->ppInventoryBags[i],NULL,&list);

				for(j=list.count-1; j>=0; j--)
				{
					bool bHasCategories = true;
					InventoryListEntry *pEntry = &list.Entry[j];
					Item *pItem = pEntry->pSlot->pItem;
					ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

					if (!pItemDef) 
						continue;

					for (k=0;k<eaiSize(&piCategories);k++)
					{
						if(-1==eaiFind(&pItemDef->peCategories,piCategories[k]))
						{
							bHasCategories = false;
							break;
						}
					}

					if (bHasCategories)
					{
						eaPush(pppItems,pItem);
					}
				}
			}
		}
	}
}

// Returns true if there is any item found with any of the given categories
bool inv_ent_HasAnyEquippedItemsWithCategory(Entity *pEnt, const int * piCategories, GameAccountDataExtract *pExtract)
{
	int i,j,k;
	InventoryList list = {0};

	if(pEnt && pEnt->pInventoryV2)
	{
		int iBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
		for(i = 0; i < iBags; i++)
		{
			if(	bag_IsEquipBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract) || 
				bag_IsWeaponBag(pEnt, pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract))
			{
				list.count = 0;
				inv_bag_GetItemList(pEnt->pInventoryV2->ppInventoryBags[i],NULL,&list);

				for(j=list.count-1; j>=0; j--)
				{
					InventoryListEntry *pEntry = &list.Entry[j];
					Item *pItem = pEntry->pSlot->pItem;
					ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

					if (!pItemDef) 
						continue;

					for (k=0;k<eaiSize(&piCategories);k++)
					{
						if(-1 != eaiFind(&pItemDef->peCategories,piCategories[k]))
						{
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

void DEFAULT_LATELINK_inv_LevelNumericSet_GetNotifyString(Entity* pEnt, const char* pchItemDisplayName, S32 iLevel, char** pestr)
{
	entFormatGameMessageKey(pEnt, pestr, "Reward.NumericSet", STRFMT_STRING("Name", pchItemDisplayName),STRFMT_INT("Value", iLevel), STRFMT_END);
}



// new inventory functionality
// AB NOTE: eventually remove the above functionality 12/10/10
// ===============================================================================




// this function will do everything the plethora of AddItem functions can do. Convert users of the additem functions to use it instead
// as a first pass. after that we can re-write the functionality behind the scenes
AUTO_TRANSACTION 
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, pInventoryV2.ppInventoryBags[], .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome inv_AddItem(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int BagID, int SlotIdx, NON_CONTAINER Item* pItem, const char* pchItemDefName, int /*ItemAddFlags*/ eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	bool res = false;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	U64* eaItemIDs = NULL;
	const char* pchBroadcastMsgKey = NULL;
	int iCount;

	// AB NOTE: the current functions take ownership of the item and destroy it. do this clone so we don't mess up with the transact cleanup code
	//          this *will* get changed with the new inventory code here 12/08/10
	if(pItem)
		pItem = StructClone(parse_Item,pItem);

	if(!pItem)
	{
		estrConcatf(ATR_RESULT_FAIL,"inv_AddItem: NULL itemdef in item of id %"FORM_LL"d", SAFE_MEMBER(pItem,id));
		return TRANSACTION_OUTCOME_FAILURE;
	}
	iCount = pItem->count;

	if (eFlags & ItemAdd_ClearID)
		CONTAINER_NOCONST(Item, pItem)->id = 0;

	if (pItem->pRewardData)
	{
		pchBroadcastMsgKey = REF_STRING_FROM_HANDLE(pItem->pRewardData->hBroadcastChatMessage);
		REMOVE_HANDLE(pItem->pRewardData->hBroadcastChatMessage);
	}

	if(!pItemDef)
	{
		estrConcatf(ATR_RESULT_FAIL,"inv_AddItem: no item def found for passed item id %"FORM_LL"d", SAFE_MEMBER(pItem,id));
		return TRANSACTION_OUTCOME_FAILURE;
	}


	if (pItemDef->eType == kItemType_Numeric)
	{
		// numerics always go in numeric bag
		NOCONST(InventoryBagLite)* bag = NULL;
		if(NONNULL(pEnt->pInventoryV2))
		{
			bag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */);
		}
		if(bag)
			res = inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS,pEnt,eFlags&ItemAdd_Silent,bag,pchItemDefName,iCount, pItem->numeric_op, pReason);
		else
			estrConcatf(ATR_RESULT_FAIL,"inv_AddItem: couldn't get bag %i from passed entity %s", BagID, pEnt->debugName);
		StructDestroy(parse_Item,pItem);
		pItem = NULL;
	}
	else
	{
		// This does not even need to be transacted, as far as I am concerned [RMARR - 9/19/11]
		if (pItemDef->eType == kItemType_Lore)
		{
			NOCONST(InventoryBagLite)* pLoreBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, 5 /* Literal InvBagIDs_Lore */, pExtract);
			if (inv_lite_trh_CountItemByName(ATR_PASS_ARGS, pLoreBag, pchItemDefName))
			{
				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}

		res = inv_ent_trh_AddItem(ATR_PASS_ARGS, pEnt, eaPets, BagID, SlotIdx, CONTAINER_NOCONST(Item, pItem), eFlags, &eaItemIDs, pReason, pExtract);
	}

#ifdef GAMESERVER
	if(res && (pItemDef->flags & kItemDefFlag_EquipOnPickup))
	{
		QueueRemoteCommand_AutoEquipItem(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pchItemDefName, ea64Get(&eaItemIDs,0), BagID, SlotIdx);
	}
	if(gbEnableEconomyLogging && res && 
		(pEnt->myEntityType != GLOBALTYPE_ENTITYCRITTER) && 
		(pItemDef->bLogForTracking) &&
		(pItemDef->eType != kItemType_Numeric))
	{
		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_ITEMTRACKING, "ItemAdded",
 				"Item \"%s\" Count %d Reason \"%s\" Detail \"%s\" Map \"%s\" Pos \"%g,%g,%g\"",
				pItemDef->pchName, 
				iCount,
				(pReason && pReason->pcReason ? pReason->pcReason : "Unknown"), 
				(pReason && pReason->pcDetail ? pReason->pcDetail : "Unknown"),
				(pReason && pReason->pcMapName ? pReason->pcMapName : "Unknown"),
				(pReason ? pReason->vPos[0] : 0.0),
				(pReason ? pReason->vPos[1] : 0.0),
				(pReason ? pReason->vPos[2] : 0.0)
				);
	}
	if(res && pchBroadcastMsgKey && pchBroadcastMsgKey[0])
	{
		QueueRemoteCommand_ItemAddedBroadcastChatMessage(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pchItemDefName, pchBroadcastMsgKey);
	}
	QueueRemoteCommand_RecordNewItemID(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, ea64Get(&eaItemIDs,0));
#endif

	ea64Destroy(&eaItemIDs);

	if(res)
		return TRANSACTION_OUTCOME_SUCCESS;
	else
		return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER 
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
Item* invbag_GetItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagIDs BagID, int SlotIdx, GameAccountDataExtract *pExtract)
{
	return (Item*)inv_trh_GetItemFromBag(ATR_PASS_ARGS,pEnt, BagID, SlotIdx, pExtract);
}




AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp");
Item* invbag_RemoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, int BagID, int SlotIdx, int Count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(Item)* pItem;


	if (ISNULL(pEnt))
	{
		TRANSACTION_APPEND_LOG_FAILURE( "Failed to remove item in Bag:Slot [%s:%d] on Ent (null): Entity is NULL", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx );
		return NULL;
	}

	if ( ISNULL(pEnt->pInventoryV2))
	{
		TRANSACTION_APPEND_LOG_FAILURE( "Failed to remove item in Bag:Slot [%s:%d] on Ent [%s]: Entity's inventory is NULL", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx, pEnt->debugName );
		return NULL;
	}

	pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (ISNULL(pBag))
	{
		//remove an itemlite and return it as an item.
		NOCONST(InventoryBagLite)* pBagLite = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
		NOCONST(InventorySlotLite)* pSlot;
		int iRemoved;
		if (ISNULL(pBagLite))
		{
			TRANSACTION_APPEND_LOG_FAILURE( "Failed to remove item in Bag:Slot [%s:%d] on Ent [%s]: Requested bag not found on ent", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx, pEnt->debugName );
			return NULL;
		}
		pSlot = pBagLite->ppIndexedLiteSlots[SlotIdx];
		iRemoved = min(Count, pSlot->count);
		pSlot->count -= iRemoved;

		pItem = inv_ItemInstanceFromDefName(pSlot->pchName, 0, 0, NULL, NULL, NULL, false, NULL);
		pItem->count = iRemoved;

		return (Item*)pItem;
	}

	if (!item_trh_CanRemoveItem(inv_bag_trh_GetItem(ATR_PASS_ARGS, pBag, SlotIdx)))
	{
		return NULL;
	}

	pItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, bSilent, pBag, SlotIdx, Count, pReason);
	return (Item*)pItem;
}


//transaction to remove an item specified by a def name from all bags in player inventory
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags");
enumTransactionOutcome inventory_RemoveItemByDefName(ATR_ARGS, NOCONST(Entity)* pEnt, const char *def_name, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	S32 iCount = 0;

	if ( ISNULL(pEnt))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Remove item [%s]x%i from all bags on Ent [(null)] failed: Entity is NULL",
			def_name, count);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( ISNULL(pEnt->pInventoryV2))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Remove item [%s]x%i from all bags on Ent [%s] failed: Entity's inventory is NULL",
			def_name, count, pEnt->debugName);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	iCount += inv_bag_trh_FindItemCountByDefName(ATR_PASS_ARGS,pEnt,InvBagIDs_Overflow,def_name,count,true,pReason,pExtract);
	if(iCount == count)
	{
		TRANSACTION_APPEND_LOG_SUCCESS("Item [%s]x%i successfully removed from Bag [Overflow] on Ent [%s].",
			def_name, count, pEnt->debugName);
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	iCount += inv_trh_FindItemCountByDefNameEx(ATR_PASS_ARGS,pEnt,InvBagFlag_DefaultInventorySearch,def_name,count,true,pReason,pExtract);
	if(iCount == count)
	{
		TRANSACTION_APPEND_LOG_SUCCESS("Item [%s]x%i successfully removed from all bags on Ent [%s].",
			def_name, count, pEnt->debugName);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags");
enumTransactionOutcome inventory_RemoveAllItemByDefName(ATR_ARGS, NOCONST(Entity)* pEnt, const char *def_name, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	S32 iCount = 0;

	if (ISNULL(pEnt))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Remove all item [%s] from all bags on Ent [(null)] failed: Entity is NULL",
			def_name);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (ISNULL(pEnt->pInventoryV2))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Remove all item [%s] from all bags on Ent [%s] failed: Entity's inventory is NULL",
			def_name, pEnt->debugName);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	iCount = inv_trh_RemoveAllItemByDefName(ATR_PASS_ARGS, pEnt, def_name, pReason, pExtract);
	if (iCount > 0)
	{
		TRANSACTION_APPEND_LOG_SUCCESS("Item [%s] successfully removed from all bags on Ent [%s].",
			def_name, pEnt->debugName);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

/*
		MultiBagFindItem: Searches all bags in the passed-in Earray for items that match the provided criteria (criterium?)
		 Returns a BagIterator pointing to the first match.
		 Call this function with a BagIterator obtained from a previous call to find the next item that also matches.

		Available criteria:
		 - kFindItem_ByType: pSearchParam should be an ItemType enum value cast to a void pointer, NOT an actual pointer.
		 - kFindItem_ByName: pSearchParam should be the name of an itemdef.
		 - kFindItem_ByID: pSearchParam should be a pointer to a U64 itemID.
*/
AUTO_TRANS_HELPER;
BagIterator* inv_bag_trh_MultiBagFindItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)*** peaBags, BagIterator* pIter, ItemFindCriteria eCriteria, const void* pSearchParam)
{
	bool bCleanup = false;
	if(!pIter && eaSize(peaBags) <= 0)
		return NULL;

	if (!pIter)
	{
		pIter = invbag_IteratorFromBagEarray(peaBags);
		bCleanup = true;
	}

	bagiterator_Next(pIter);

	while (!bagiterator_Stopped(pIter))
	{
		NOCONST(Item)* pItem = bagiterator_GetItem(pIter);
		ItemDef* pDef = SAFE_GET_REF(pItem, hItem);

		if (!pItem)
		{
			bagiterator_Next(pIter);
			continue;
		}

		switch (eCriteria)
		{
		case kFindItem_ByType:
			{
				ItemType eType = (ItemType)pSearchParam;

				if (!pDef)
				{
					bagiterator_Next(pIter);
					continue;
				}

				if (pDef->eType == eType)
					return pIter;
			}break;
		case kFindItem_ByName:
			{
				const char* pchName = (const char*)pSearchParam;
				if (REF_STRING_FROM_HANDLE(pItem->hItem) == allocAddString(pchName))
					return pIter;
			}break;
		case kFindItem_ByID:
			{
				U64* pID = (U64*)pSearchParam;
				if (pItem && pItem->id == *pID)
					return pIter;
			}break;
		}
		bagiterator_Next(pIter);
	}

	if (bCleanup)
		bagiterator_Destroy(pIter);

	return NULL;
}

AUTO_TRANS_HELPER;
BagIterator* inv_bag_trh_FindItem(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, BagIterator* pIter, ItemFindCriteria eCriteria, const void* pData)
{
	NOCONST(InventoryBag)** eaBags = NULL;
	if (!pIter)
	{
		eaStackCreate(&eaBags, 1);
		eaPush(&eaBags, pBag);
	}
	return inv_bag_trh_MultiBagFindItem(ATR_PASS_ARGS, &eaBags, pIter, eCriteria, pData);
}

// make an iterator for a bag by def name.
// returns NULL if not found, or invalid params. 
// returned BagIterator must be free'd
// This iterator is valid so long as no non-iterator inventory functions operate on this bag.
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[]");
BagIterator* inv_bag_trh_FindItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, const char* def_name, BagIterator* res)
{
	NOCONST(InventoryBag) *bag;

	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2) || !def_name)
		return NULL;

	bag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, BagID); // lock entire bag, but with a INDEXED_NULLISOK one

	return inv_bag_trh_FindItem(ATR_PASS_ARGS, bag, res, kFindItem_ByName, def_name);
}

BagIterator* inv_bag_FindItem(Entity* pEnt, int BagID, BagIterator* res, ItemFindCriteria eCriteria, const void* pSearchParam, bool bIncludePlayerBags, bool bIncludeOverflow)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InvBagIDs i;
	NOCONST(InventoryBag)* pBag = NULL;
	NOCONST(InventoryBag)** eaBags = NULL;

	if (ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return NULL;

	eaStackCreate(&eaBags, 1 + (bIncludePlayerBags ? 9 : 0) + (bIncludeOverflow ? 1 : 0));

	if (bIncludeOverflow)
	{
		pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		if (pBag)
			eaPush(&eaBags, pBag);
	}
	if (bIncludePlayerBags)
	{
		for (i = InvBagIDs_PlayerBag1; i <= InvBagIDs_PlayerBag9; i++)
		{
			pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), i, pExtract);
			if (pBag)
				eaPush(&eaBags, pBag);
		}
	}

	pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);
	if (pBag)
		eaPush(&eaBags, pBag);

	return inv_bag_trh_MultiBagFindItem(ATR_EMPTY_ARGS, &eaBags, res, eCriteria, pSearchParam);

}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[]");
BagIterator* inv_bag_trh_FindItemByType(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, int eType, BagIterator* res)
{
	NOCONST(InventoryBag) *bag;

	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2) || !eType)
		return NULL;

	bag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, BagID); // lock entire bag, but with a INDEXED_NULLISOK one

	return inv_bag_trh_FindItem(ATR_PASS_ARGS, bag, res, kFindItem_ByType, (void*)eType);//don't judge me!
}

// find a bag in the player's inventory by id. The important word here is 'inventory'. This function implicitly
// excludes other bags that hang off the player's inventory, unless bSearchAllBags is set, in which case all bags will be searched
// NOTE: this stops at the first bag that has the ID and assumes global uniqueness for item ids.
AUTO_TRANS_HELPER ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
BagIterator* inv_trh_FindItemByIDEx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, BagIterator* pIter, U64 item_id, bool bIncludeOverflowBag, bool bSearchAllBags)
{
	int i;
	InvBagFlag inventory_bag_flags = InvBagFlag_DefaultInventorySearch;
	
	NOCONST(InventoryBag)** eaBags = NULL;
	if (NONNULL(pEnt))
	{
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); ++i)
		{
			NOCONST(InventoryBag)* bag = pEnt->pInventoryV2->ppInventoryBags[i];
			if (NONNULL(bag)) {
				if ( (bSearchAllBags || (invbag_trh_flags(bag) & inventory_bag_flags)) || 
					(bIncludeOverflowBag && bag->BagID == InvBagIDs_Overflow) ) 
				{
					eaPush(&eaBags, bag);
				}
			}
		}
	}

	pIter = inv_bag_trh_MultiBagFindItem(ATR_PASS_ARGS, &eaBags, pIter, kFindItem_ByID, &item_id);
	
	eaDestroy(&eaBags);
	
	return pIter;
}

AUTO_TRANS_HELPER ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags");
BagIterator* inv_trh_FindItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 item_id)
{
	return inv_trh_FindItemByIDEx(ATR_PASS_ARGS, pEnt, NULL, item_id, false, false);
}

// find an item in the player's inventory by UID in a given bag.
AUTO_TRANS_HELPER
ATR_LOCKS(pBag, ".*");
BagIterator* inv_bag_trh_FindItemByID(ATR_ARGS, ATH_ARG NOCONST(InventoryBag)* pBag, U64 item_id, BagIterator* res)
{
	if (res && (bagiterator_GetCurrentBag(res) != pBag))
	{
		bagiterator_Destroy(res);
		res = NULL;
	}

	if (!res)
		res = invbag_IteratorFromBag(pBag);

	return inv_trh_FindItemByIDEx(ATR_PASS_ARGS, NULL, res, item_id, false, false);
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pBag, ".Ppindexedliteslots[]");
S32 inv_lite_trh_FindItemCountByDefName(ATR_ARGS, ATH_ARG NOCONST(InventoryBagLite)* pBag, const char* pchDefName, int iCount, U32 bRemoveFound, GameAccountDataExtract* pExtract)
{
	NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, pchDefName);
	S32 iFound = 0;

	if (pSlot)
	{
		if (bRemoveFound)
		{
			int iRemove = pSlot->count;

			if (iFound + iRemove > iCount)
				iRemove = iCount - iFound;

			pSlot->count = max(0, pSlot->count - iRemove);
			iFound += iRemove;
		}
		else
		{
			iFound += pSlot->count;
		}
	}
	
	return iFound;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
S32 inv_bag_trh_FindItemCountByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 eBagID, const char* pchDefName, int iCount, U32 bRemoveFound, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	BagIterator* pIter = NULL;
	S32 iFound = 0;
	do {
		pIter = inv_bag_trh_FindItemByDefName(ATR_PASS_ARGS, pEnt, eBagID, pchDefName, pIter);
		if (pIter)
		{
			NOCONST(InventoryBag)* pBag = bagiterator_GetCurrentBag(pIter);
			NOCONST(InventorySlot)* pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, pIter->i_cur);

			if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
			{
				if (bRemoveFound)
				{
					Item* pItem;
					int iRemove = pSlot->pItem->count;
					if (iFound + iRemove > iCount)
						iRemove = iCount - iFound;

					if (pItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, eBagID, pIter->i_cur, iRemove, pReason, pExtract))
					{
						iFound += iRemove;
						StructDestroySafe(parse_Item, &pItem);
					}
				}
				else
				{
					iFound += pSlot->pItem->count;
				}
			}
		}
	} while (pIter && pIter->i_cur >= 0 && iFound < iCount);

	bagiterator_Destroy(pIter);
	return iFound;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
S32 inv_bag_trh_RemoveAllItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 eBagID, const char* pchDefName, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	int iFound = 0;
	BagIterator* pIter = NULL;
	do {
		pIter = inv_bag_trh_FindItemByDefName(ATR_PASS_ARGS, pEnt, eBagID, pchDefName, pIter);
		if (pIter)
		{
			NOCONST(InventoryBag)* pBag = bagiterator_GetCurrentBag(pIter);
			NOCONST(InventorySlot)* pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, pIter->i_cur);

			if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
			{
				Item* pItem;
				int iRemove = pSlot->pItem->count;

				if (pItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, eBagID, pIter->i_cur, iRemove, pReason, pExtract))
				{
					iFound += iRemove;
					StructDestroySafe(parse_Item, &pItem);
				}
			}
		}
	} while (pIter && pIter->i_cur >= 0);

	bagiterator_Destroy(pIter);
	return iFound;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags, pInventoryV2.peaowneduniqueitems");
S32 inv_trh_FindItemCountByDefNameEx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InvBagFlag searchBagFlags, const char* pchDefName, int iCount, U32 bRemoveFound, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	int i, iFound = 0;

	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return 0;

	for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
	{
		InvBagIDs eBagID = pEnt->pInventoryV2->ppInventoryBags[i]->BagID;

		if (!(invbag_trh_flags(pEnt->pInventoryV2->ppInventoryBags[i]) & searchBagFlags))
			continue;

		// When removing items its checking for an exact amount so don't remove too many (iCount - iFound). inv_bag_trh_FindItemCountByDefName stops after finding count regardless of removal
		iFound += inv_bag_trh_FindItemCountByDefName(ATR_PASS_ARGS, pEnt, eBagID, pchDefName, iCount - iFound, bRemoveFound, pReason, pExtract);

		if (iFound >= iCount)
			break;
	}

	if (iFound >= iCount)
		return iFound;

	//Do lite bags as well.
	for (i = eaSize(&pEnt->pInventoryV2->ppLiteBags)-1; i >= 0; i--)
	{
		NOCONST(InventoryBagLite)* pBag = pEnt->pInventoryV2->ppLiteBags[i];

		iFound += inv_lite_trh_FindItemCountByDefName(ATR_PASS_ARGS, pBag, pchDefName, iCount - iFound, bRemoveFound, pExtract);

		if (iFound >= iCount)
			break;
	}
	return iFound;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags, pInventoryV2.peaowneduniqueitems");
S32 inv_trh_FindItemCountByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pchDefName, int iCount, U32 bRemoveFound, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	InvBagFlag inventory_bag_flags = InvBagFlag_DefaultInventorySearch;
	return inv_trh_FindItemCountByDefNameEx(ATR_PASS_ARGS, pEnt, inventory_bag_flags, pchDefName, iCount, bRemoveFound, pReason, pExtract);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags, pInventoryV2.peaowneduniqueitems");
S32 inv_trh_RemoveAllItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pchDefName, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	InvBagFlag inventory_bag_flags = InvBagFlag_DefaultInventorySearch;
	int i, iFound = 0;

	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return 0;

	iFound += inv_bag_trh_RemoveAllItemByDefName(ATR_PASS_ARGS, pEnt, InvBagIDs_Overflow, pchDefName, pReason, pExtract);

	for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
	{
		InvBagIDs eBagID = pEnt->pInventoryV2->ppInventoryBags[i]->BagID;

		if (!(invbag_trh_flags(pEnt->pInventoryV2->ppInventoryBags[i]) & inventory_bag_flags))
			continue;

		iFound += inv_bag_trh_RemoveAllItemByDefName(ATR_PASS_ARGS, pEnt, eBagID, pchDefName, pReason, pExtract);
	}

	//Do lite bags as well.
	for (i = eaSize(&pEnt->pInventoryV2->ppLiteBags)-1; i >= 0; i--)
	{
		NOCONST(InventoryBagLite)* pBag = pEnt->pInventoryV2->ppLiteBags[i];
		NOCONST(InventorySlotLite)* pSlot = eaIndexedGetUsingString(&pBag->ppIndexedLiteSlots, pchDefName);
		if (pSlot)
		{
			iFound += pSlot->count;
			pSlot->count = 0;
		}
	}
	return iFound;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, .pInventoryV2.PpLitebags, pInventoryV2.peaowneduniqueitems,   .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Ilevelexp");
Item* inv_RemoveItemByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U64 item_id, int count, ItemAddFlags eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	bool bUseOverflow = !!(eFlags & ItemAdd_UseOverflow);
	BagIterator *iter = inv_trh_FindItemByIDEx(ATR_PASS_ARGS,pEnt,NULL, item_id, bUseOverflow, false);
	Item *item;
	if(!iter)
		return NULL;
	item = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, bagiterator_GetCurrentBagID(iter), iter->i_cur, count, pReason, pExtract);
	bagiterator_Destroy(iter);
	return item;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
S32 invbag_RemoveItemByDefName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, const char* def_name, int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	S32 iRemoved = inv_bag_trh_FindItemCountByDefName(ATR_PASS_ARGS, pEnt, BagID, def_name, count, true, pReason, pExtract);
	return (iRemoved == count);
}

static __forceinline BagIterator* bagiterator_CreateInternal()
{
	BagIterator* iter;
	iter = MP_ALLOC(BagIterator);
	iter->i_bag = -1;
	return iter;
}

AUTO_TRANS_HELPER ATR_LOCKS(bag, ".*");
BagIterator *invbag_IteratorFromBag(ATH_ARG NOCONST(InventoryBag) *bag)
{
	BagIterator* iter;
	if(ISNULL(bag))
		return NULL;

	iter = bagiterator_CreateInternal();
	eaPush(&iter->eaBags, CONTAINER_RECONST(InventoryBag, bag));
	iter->i_bag = 0;
	//start one past the end to solve first-slot match misses
	iter->i_cur = eaSize(&bag->ppIndexedInventorySlots);
	return iter;
}

AUTO_TRANS_HELPER ATR_LOCKS(peaBags, ".*");
BagIterator *invbag_IteratorFromBagEarray(ATH_ARG NOCONST(InventoryBag)*** peaBags)
{
	BagIterator* iter;
	InventoryBag* pBag = NULL;
	if(ISNULL(peaBags))
		return NULL;

	iter = bagiterator_CreateInternal();
	eaCopy(&iter->eaBags, (InventoryBag***)peaBags);
	iter->i_bag = eaSize(&iter->eaBags)-1;
	//start one past the end to solve first-slot match misses
	pBag = iter->eaBags[iter->i_bag];
	if (NONNULL(pBag))
		iter->i_cur = eaSize(&pBag->ppIndexedInventorySlots);
	else 
		iter->i_cur = 0;
	return iter;	
}

AUTO_TRANS_HELPER ATR_LOCKS(bag, ".*");
BagIterator *invbag_IteratorFromLiteBag(ATH_ARG NOCONST(InventoryBagLite) *bag)
{
	BagIterator* iter;
	if(ISNULL(bag))
		return NULL;

	iter = bagiterator_CreateInternal();
	eaPush(&iter->eaLitebags, CONTAINER_RECONST(InventoryBagLite, bag));
	iter->i_bag = 0;
	//start one past the end to solve first-slot match misses
	iter->i_cur = eaSize(&bag->ppIndexedLiteSlots);
	return iter;	
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]"); // ppInventoryBags is a INDEXED_NULLISOK lock
BagIterator *invbag_trh_IteratorFromEnt(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag) *bag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (bag)
		return invbag_IteratorFromBag(bag);
	return NULL;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]"); // ppInventoryBags is a INDEXED_NULLISOK lock
BagIterator *invbag_trh_LiteIteratorFromEnt(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBagLite) *litebag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (litebag)
		return invbag_IteratorFromLiteBag(litebag);
	return NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]"); // ppInventoryBags is a INDEXED_NULLISOK lock
BagIterator* invbag_trh_AddBagToIterator(ATR_ARGS, BagIterator* pIter, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract, int BagID)
{
	NOCONST(InventoryBag) *bag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	NOCONST(InventoryBagLite)* litebag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, BagID, pExtract);
	if (NONNULL(bag))
	{
		pIter->i_bag++;
		eaPush(&pIter->eaBags, CONTAINER_RECONST(InventoryBag, bag));
		//start one past the end to solve first-slot match misses
		pIter->i_cur = eaSize(&bag->ppIndexedInventorySlots);
	}
	else if (NONNULL(litebag))
	{
		pIter->i_bag++;
		eaPush(&pIter->eaLitebags, CONTAINER_RECONST(InventoryBagLite, litebag));
		//start one past the end to solve first-slot match misses
		pIter->i_cur = eaSize(&litebag->ppIndexedLiteSlots);
	}
	return pIter;
}

NOCONST(Item)* bagiterator_GetItem(BagIterator *iter)
{
	InventorySlot *slot;
	ItemDef *def = NULL;

	if(iter && iter->i_bag >= 0 && iter->i_cur >= 0)
	{
		InventoryBag* pBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);

		if(iter->i_cur >= eaSize(&pBag->ppIndexedInventorySlots))
		{
			// This can happen due to a bag collapse when removing items
			return NULL;
		}

		slot = eaGet(&pBag->ppIndexedInventorySlots, iter->i_cur);

		if(!slot)
			return NULL;

		// AB NOTE:  placeholder for now. the returned item must be modifiable.
		if(!slot->pItem)
			return NULL;

		return CONTAINER_NOCONST(Item, slot->pItem);
	}
	return NULL;
}

bool bagiterator_Next(BagIterator *iter)
{
	if(!iter)
		return false;

	if(iter->i_cur>=0)
		iter->i_cur--;
	if (iter->i_cur < 0)
	{
		iter->i_bag--;
		if (iter->i_bag >= 0)
		{
			InventoryBag* pBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);
			InventoryBagLite* pLiteBag = (InventoryBagLite*)bagiterator_GetCurrentBag(iter);
			//start one past the end to solve first-slot match misses
			if (pBag)
			{
				if (eaSize(&pBag->ppIndexedInventorySlots) <= 0)
					return bagiterator_Next(iter);
				else
					iter->i_cur = eaSize(&pBag->ppIndexedInventorySlots);
			}
			else if (pLiteBag)
			{
				if (eaSize(&pLiteBag->ppIndexedLiteSlots) <= 0)
					return bagiterator_Next(iter);
				else
					iter->i_cur = eaSize(&pLiteBag->ppIndexedLiteSlots);
			}
		}
	}
	return !bagiterator_Stopped(iter);
}

bool bagiterator_Stopped(BagIterator *iter)
{
	return !iter || (iter->i_cur < 0 && iter->i_bag < 0);
}

ItemDef* bagiterator_GetDef(BagIterator *iter)
{
	if(iter && iter->i_bag >= 0 && iter->i_cur >= 0)
	{
		InventorySlot *pSlot = NULL;
		InventorySlotLite *pLiteSlot = NULL;
		InventoryBag* pBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);
		InventoryBagLite* pLiteBag = (InventoryBagLite*)bagiterator_GetCurrentLiteBag(iter);
		ItemDef *def = NULL;

		if (pLiteBag)
		{
			pLiteSlot = eaGet(&pLiteBag->ppIndexedLiteSlots, iter->i_cur);
			if (pLiteSlot && IS_HANDLE_ACTIVE(pLiteSlot->hItemDef))
				return GET_REF(pLiteSlot->hItemDef);
			else if (pLiteSlot)
				return RefSystem_ReferentFromString(g_hItemDict, pLiteSlot->pchName);
		}
		else if (pBag)
		{
			if(iter->i_cur >= eaSize(&pBag->ppIndexedInventorySlots))
			{
				// This is possible due to bag collapse when removing items
				return NULL;
			}

			pSlot = eaGet(&pBag->ppIndexedInventorySlots, iter->i_cur);
		}
		if(!pSlot || !pSlot->pItem)
			return NULL;

		return GET_REF(pSlot->pItem->hItem);
	}
	return NULL;
}

bool bagiterator_ItemDefStillLoading(BagIterator *iter)
{
	if(iter && iter->i_bag >= 0 && iter->i_cur >= 0)
	{
		InventorySlot *slot;
		InventoryBag* pBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);
		InventoryBagLite* pLiteBag = (InventoryBagLite*)bagiterator_GetCurrentLiteBag(iter);
		ItemDef *def = NULL;

		if (pLiteBag)
		{
			return false;
		}

		slot = eaGet(&pBag->ppIndexedInventorySlots, iter->i_cur);
		if(!slot || !slot->pItem)
			return false;

		return REF_IS_SET_BUT_ABSENT(slot->pItem->hItem);
	}
	return false;
}

InvBagIDs bagiterator_GetCurrentBagID(BagIterator *iter)
{
	if(!iter || (!iter->eaBags && !iter->eaLitebags))
		return InvBagIDs_None;

	if (iter->i_bag < eaSize(&iter->eaBags))
		return iter->eaBags[iter->i_bag]->BagID;
	else if (iter->i_bag < eaSize(&iter->eaBags) + eaSize(&iter->eaLitebags))
		return iter->eaLitebags[iter->i_bag-eaSize(&iter->eaBags)]->BagID;

	return InvBagIDs_None;
}

NOCONST(InventoryBag)* bagiterator_GetCurrentBag(BagIterator *iter)
{
	if(!iter || !iter->eaBags)
		return NULL;

	if (iter->i_bag < eaSize(&iter->eaBags) && iter->i_bag >= 0)
		return CONTAINER_NOCONST(InventoryBag, iter->eaBags[iter->i_bag]);

	return NULL;
}

NOCONST(InventoryBagLite)* bagiterator_GetCurrentLiteBag(BagIterator *iter)
{
	if(!iter || !iter->eaLitebags)
		return NULL;

	if (iter->i_bag < eaSize(&iter->eaBags))
		return NULL;
	else if ((iter->i_bag < eaSize(&iter->eaBags) + eaSize(&iter->eaLitebags)) && iter->i_bag >= 0)
		return CONTAINER_NOCONST(InventoryBagLite, iter->eaLitebags[iter->i_bag-eaSize(&iter->eaBags)]);

	return NULL;
}

// locks entire bag, no way to get around this as TransServer can't know 
// what to lock. Okay since most iterators traverse the entire bag within
// a transaction.
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags");
Item *bagiterator_RemoveItem(ATR_ARGS, BagIterator *iter, ATH_ARG NOCONST(Entity)* ent,  int count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(!iter)
		return NULL;
	return invbag_RemoveItem(ATR_PASS_ARGS,ent,false,bagiterator_GetCurrentBagID(iter),iter->i_cur, count, pReason, pExtract);
}

// AB NOTE: required to hide data revamp. this function should be removed
InventorySlot *bagiterator_GetSlot(BagIterator *iter)
{
	InventoryBag* pBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);
	if(!iter || !pBag || iter->i_cur < 0)
		return 0;
	return  eaGet(&pBag->ppIndexedInventorySlots,iter->i_cur);
}

int bagiterator_GetSlotID(BagIterator *iter)
{
	InventoryBag* pBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);
	if(!iter || !pBag)
		return -1;

	return iter->i_cur;
}

// AB NOTE: required to hide data revamp. this function should be removed
int bagiterator_GetItemCount(BagIterator *iter)
{
	InventoryBagLite* pLite = (InventoryBagLite*)bagiterator_GetCurrentLiteBag(iter);
	if (pLite)
	{
		InventorySlotLite* pSlot = eaGet(&pLite->ppIndexedLiteSlots, iter->i_cur);
		return pSlot ? pSlot->count : 0;
	}
	else
	{
		InventorySlot *slot = bagiterator_GetSlot(iter);	
		if(!slot || !slot->pItem)
			return 0;
		return slot->pItem->count;
	}
}

// AB NOTE: another to destroy.
// slot name appears to be the def name, if indexed, a number as string otherwise.
char *bagiterator_GetSlotName(BagIterator *iter)
{
	InventorySlot *slot = bagiterator_GetSlot(iter);	
	if(!slot)
		return NULL;
	return (char*)slot->pchName;	
}


void bagiterator_Destroy(BagIterator *iter)
{
	if (iter)
	{
		eaDestroy(&iter->eaBags);
		eaDestroy(&iter->eaLitebags);
		MP_FREE(BagIterator, iter);
	}
}



// move an item from within an inventory or between entities
AUTO_TRANS_HELPER
	ATR_LOCKS(pDstEnt, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pSrcEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(eaSrcPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
bool inv_MoveItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, NOCONST(InventoryBag) *pDestBag, int iDstSlot, ATH_ARG NOCONST(Entity)* pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets, NOCONST(InventoryBag) *pSrcBag, int iSrcSlot, int iCount, int /*ItemAddFlags*/ eFlags, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract)
{
	bool bSilent = (eFlags & ItemAdd_Silent);
	bool bFromBuybackOkay = (eFlags & ItemAdd_FromBuybackOkay);
	
	if (ISNULL(pSrcBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null SrcBag passed. Ent %s", SAFE_MEMBER(pSrcEnt,debugName) );
		return false;
	}

	if (ISNULL(pDestBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null DestBag passed. Ent %s", SAFE_MEMBER(pDstEnt,debugName) );
		return false;
	}	

	return inv_bag_trh_MoveItem(ATR_PASS_ARGS, bSilent, pDstEnt, eaDstPets, pDestBag, iDstSlot, pSrcEnt, eaSrcPets, pSrcBag, iSrcSlot, iCount, bFromBuybackOkay, false, pSrcReason, pDestReason, pExtract);
}

AUTO_TRANS_HELPER;
bool inv_trh_UnGemItem(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(InventoryBag) *pSrcBag, int SrcSlotIdx, U64 uSrcItemID, int iGemSlotIdx, ATH_ARG NOCONST(InventoryBag) *pDestBag, int DestSlotIdx, const char *pchCostNumeric, int iCost, GameAccountDataExtract *pExtract, const ItemChangeReason *pReason)
{
	if (ISNULL(pSrcBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null SrcBag passed. Ent %s", SAFE_MEMBER(pEnt,debugName) );
		return false;
	}

	if (ISNULL(pDestBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null DestBag passed. Ent %s", SAFE_MEMBER(pEnt,debugName) );
		return false;
	}

	return inv_bag_trh_UnGemItem(ATR_PASS_ARGS, pEnt, pSrcBag, SrcSlotIdx, uSrcItemID, iGemSlotIdx, pDestBag, DestSlotIdx, pchCostNumeric, iCost, pExtract, pReason);
}

AUTO_TRANS_HELPER;
bool inv_trh_MoveGemItem(ATR_ARGS, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets, NOCONST(InventoryBag) *pDestBag, int iDstSlot, int iDstGemSlot, U64 uDstItemID, ATH_ARG NOCONST(Entity) *pSrcEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets, NOCONST(InventoryBag) *pSrcBag, int iSrcSlot, U64 uSrcItemID, int iCount, int /*ItemAddFlags*/ eFlags, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract, const char *pDestEntDebugName )
{
	bool bSilent = (eFlags & ItemAdd_Silent);
	bool bFromBuybackOkay = (eFlags & ItemAdd_FromBuybackOkay);

	if (ISNULL(pSrcBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null SrcBag passed. Ent %s", SAFE_MEMBER(pSrcEnt,debugName) );
		return false;
	}

	if (ISNULL(pDestBag) && pDestEntDebugName)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null DestBag passed. Ent %s", pDestEntDebugName );
		return false;
	}

	return inv_bag_trh_MoveGemItem(ATR_PASS_ARGS, eaDstPets, pDestBag, iDstSlot, uDstItemID, iDstGemSlot, pSrcEnt, eaSrcPets, pSrcBag, iSrcSlot, uSrcItemID, bFromBuybackOkay, pSrcReason, pDestReason, pExtract);
}

// locks all bags
AUTO_TRANS_HELPER 
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
bool inv_MoveItemSelf(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int SrcBagID, int DestBagID, int SrcSlotIdx, int DestSlotIdx, int Count, int /*ItemAddFlags*/ eFlags, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS,pEnt,DestBagID,pExtract);
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS,pEnt,SrcBagID,pExtract);
	return inv_MoveItem(ATR_PASS_ARGS, pEnt, NULL, pDestBag, DestSlotIdx, pEnt, NULL, pSrcBag, SrcSlotIdx, Count, eFlags, pReason, pReason, pExtract);
}


AUTO_TRANS_HELPER ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp");
Item* item_FromEnt(ATH_ARG NOCONST(Entity)* pEnt, const char* ItemDefName, int overrideLevel, const char *pcRank, ItemAddFlags eFlags)
{
	return (Item*) inv_ItemInstanceFromDefName( ItemDefName,entity_trh_GetSavedExpLevelLimited(pEnt), overrideLevel, pcRank, NONNULL(pEnt) ? GET_REF(pEnt->hAllegiance) : NULL, NONNULL(pEnt) ? GET_REF(pEnt->hSubAllegiance) : NULL, overrideLevel>0 , NULL);
}

Item* item_FromSavedPet(Entity* ent, S32 pet_id) {return (Item*)inv_ent_ContainerItemInstanceFromSavedPet(ent, pet_id);} 


Item* item_FromPetInfo(char const *def_name, ContainerID id, AllegianceDef *allegiance, AllegianceDef *suballegiance, ItemQuality quality)
{
	NOCONST(Item) *item = inv_ItemInstanceFromDefName(def_name,0,0,NULL,allegiance,suballegiance,false,NULL);
	if(!item)
		return NULL;
	else
	{
		char idBuf[128];
		NOCONST(SpecialItemProps)* pProps = (NOCONST(SpecialItemProps)*)item_trh_GetOrCreateSpecialProperties(item);
		pProps->pContainerInfo = StructCreateNoConst(parse_ItemContainerInfo);
		pProps->pContainerInfo->eContainerType = GLOBALTYPE_ENTITYSAVEDPET;
		if (quality != kItemQuality_None)
		{
			if (!item->pAlgoProps)
			{
				item->pAlgoProps = StructCreateNoConst(parse_AlgoItemProps);
				item->pAlgoProps->Quality = quality;
			}
		}
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(id, idBuf),pProps->pContainerInfo->hSavedPet);
	}
	return (Item*)item;
}

AUTO_TRANS_HELPER ATR_LOCKS(bag, ".Inv_Def, .Bagid");
const InvBagDef *invbag_trh_def(ATH_ARG NOCONST(InventoryBag) *bag)
{
	DefaultInventory const *inv_def = NULL;
	InvBagDef const *bag_def;
	if(NONNULL(bag))
	{
		inv_def = GET_REF(bag->inv_def);
	}

	if(ISNULL(inv_def))
		return NULL;
	bag_def = eaIndexedGetUsingInt(&inv_def->InventoryBags,bag->BagID);
	return bag_def;
}

AUTO_TRANS_HELPER ATR_LOCKS(bag, ".Inv_Def, .Bagid");
const InvBagDef *invbaglite_trh_def(ATH_ARG NOCONST(InventoryBagLite) *bag)
{
	DefaultInventory const *inv_def = NULL;
	InvBagDef const *bag_def;
	if(NONNULL(bag))
	{
		inv_def = GET_REF(bag->inv_def);
	}

	if(ISNULL(inv_def))
		return NULL;
	bag_def = eaIndexedGetUsingInt(&inv_def->InventoryBags,bag->BagID);
	return bag_def;
}

// Returns the highest item quality in the inventory bag
S32 invbag_GetHighestItemQuality(SA_PARAM_NN_VALID InventoryBag *pRewardBag, SA_PARAM_OP_VALID bool * pbHasMissionItem)
{
	S32 iMaxQuality = kItemQuality_None;

	FOR_EACH_IN_EARRAY_FORWARDS(pRewardBag->ppIndexedInventorySlots, InventorySlot, pInventorySlot)
	{
		if (pInventorySlot->pItem)
		{
			if (pbHasMissionItem)
			{
				ItemDef *pItemDef = GET_REF(pInventorySlot->pItem->hItem);
				if (pItemDef && pItemDef->eType == kItemType_Mission)
				{
					*pbHasMissionItem = true;
				}
			}
			
			if (item_GetQuality(pInventorySlot->pItem) > iMaxQuality)
			{
				iMaxQuality = item_GetQuality(pInventorySlot->pItem);
			}
		}
	}
	FOR_EACH_END

	return iMaxQuality;
}

bool inv_NeedsBagFixup(Entity *pEnt)
{
	bool bNeeded = false;
	GameAccountDataExtract *pExtract;
	InventoryBag *pInvBag;
	int i;

	PERFINFO_AUTO_START_FUNC();

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	//First check the inventory
	pInvBag = (InventoryBag *)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Inventory, pExtract);
	if (pInvBag) {
		S32 iNewInvSize, iAddInvSize, iExtraInvSize, iBaseInvSize;

		iBaseInvSize = invbag_basemaxslots(pEnt, pInvBag);
		iAddInvSize = GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_INVENTORY_SLOTS_FROM_NUMERIC, false) ? inv_GetNumericItemValue(pEnt, "AddInvSlots") : 0;
		iExtraInvSize = inv_GetNumericItemValue(pEnt, "AddInvSlotsMicrotrans");
		iNewInvSize = iBaseInvSize + iAddInvSize + iExtraInvSize;

		if (iNewInvSize > invbag_maxslots(pEnt, pInvBag)) {
			bNeeded = true;
		}
	}
	if(!bNeeded) {
		//Check the bank bags
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++) {
			InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
			if (invbag_flags(pBag) & InvBagFlag_BankBag) {
				S32 iBankSize, iCurBankSize, iBaseBankSize = 0;

				iBaseBankSize = invbag_basemaxslots(pEnt, pBag);
				iCurBankSize = invbag_maxslots(pEnt, pBag);
				iBankSize = ( GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_BANK_SLOTS_FROM_NUMERIC, false) ? inv_GetNumericItemValue(pEnt, "BankSize") : 0 ) + 
							inv_GetNumericItemValue(pEnt, "BankSizeMicrotrans") +
							iBaseBankSize;
				if (iBankSize != iCurBankSize) {
					bNeeded = true;
					break;
				}

                // If the number of actual slots used is greater than the expected bank size.
                if ( eaSize(&pBag->ppIndexedInventorySlots) > iBankSize )
                {
                    bNeeded = true;
                    break;
                }
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();	
	return bNeeded;
}

//returns the max overall quality of the passed-in bags (NOT derived from the items in the bags)
ItemQuality inv_FillRewardRequest(InventoryBag** eaRewardBags, InvRewardRequest* pRequest)
{
	S32 i, j;
	ItemQuality eMaxOverallQuality = kItemQuality_None;
	for (i = 0; i < eaSize(&eaRewardBags); i++)
	{
		InventoryBag* pRewardBag = eaRewardBags[i];
		eMaxOverallQuality = max(eMaxOverallQuality, pRewardBag->pRewardBagInfo->eRewardPackOverallQuality);
		for (j = 0; j < eaSize(&pRewardBag->ppIndexedInventorySlots); j++)
		{
			InventorySlot* pSlot = pRewardBag->ppIndexedInventorySlots[j];
			ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;

			if (!pItemDef || SAFE_MEMBER2(pSlot->pItem, pRewardData, bHideInUI))
				continue;

			if (pItemDef->eType == kItemType_Numeric)
			{
				ItemNumericData* pNumericData = StructCreate(parse_ItemNumericData);
				SET_HANDLE_FROM_REFERENT("ItemDef", pItemDef, pNumericData->hDef);
				pNumericData->iNumericValue = pSlot->pItem ? pSlot->pItem->count : 0;
				eaPush(&pRequest->eaNumericRewards, pNumericData);
			}
			else if (pSlot->pItem)
			{
				Item* pItemCopy = StructClone(parse_Item, pSlot->pItem);
				item_trh_RemovePowers(CONTAINER_NOCONST(Item, pItemCopy));
				StructDestroySafe(parse_ItemRewardData, &pItemCopy->pRewardData);
				eaPush(&pRequest->eaItemRewards, pItemCopy);
			}
		}
	}
	return eMaxOverallQuality;
}

bool inv_FillNumericRewardRequestClient(Entity* pEnt, ItemNumericData* pNumericData, InvRewardRequest* pOutRequest)
{
#ifdef GAMECLIENT
	const char* pchItemDef = REF_STRING_FROM_HANDLE(pNumericData->hDef);
	NOCONST(Item)* pItem = CONTAINER_NOCONST(Item, item_FromEnt(CONTAINER_NOCONST(Entity, pEnt),pchItemDef,0,NULL,0));
	if (pItem)
	{
		NOCONST(InventorySlot)* pInvSlot = inv_InventorySlotCreate(pOutRequest->iRewardSlots++);
		pInvSlot->pItem = pItem;
		pItem->count = pNumericData->iNumericValue;
		eaPush(&pOutRequest->eaRewards, CONTAINER_RECONST(InventorySlot, pInvSlot));
		return true;
	}
	else
	{
		//Create a fake numeric item and just set the ref/count. 
		//It's not like numeric items are anything more than a ref and count anyway.
		NOCONST(InventorySlot)* pInvSlot = inv_InventorySlotCreate(pOutRequest->iRewardSlots++);
		pInvSlot->pItem = StructCreateNoConst(parse_Item);
		COPY_HANDLE(pInvSlot->pItem->hItem, pNumericData->hDef);
		pInvSlot->pItem->count = pNumericData->iNumericValue;
		eaPush(&pOutRequest->eaRewards, CONTAINER_RECONST(InventorySlot, pInvSlot));
		return true;
	}
#endif
	return false;
}

// Takes a InvRewardRequest and processes the eaNumericRewards and eaItemRewards into the client_only list eaRewards
// bPlaceNumericsAfterItems determines whether the numerics are placed at the beginning or end of the list
void inv_FillRewardRequestClient(	Entity* pEnt, 
									InvRewardRequest* pInRequest, 
									InvRewardRequest* pOutRequest, 
									bool bPlaceNumericsAfterItems)
{
#ifdef GAMECLIENT
	S32 i, twice = 0;
	bool bProcessNumerics = !bPlaceNumericsAfterItems;
	eaClearStruct(&pOutRequest->eaRewards, parse_InventorySlot);
	
	// loop twice and depending on the flag bPlaceNumericsAfterItems, create the client-side eaRewards list
	// from the eaNumericRewards and eaItemRewards
	do {
		if (bProcessNumerics)
		{
			for (i = 0; i < eaSize(&pInRequest->eaNumericRewards); i++)
			{
				inv_FillNumericRewardRequestClient(pEnt, pInRequest->eaNumericRewards[i], pOutRequest);
			}
		}
		else
		{
			for (i = 0; i < eaSize(&pInRequest->eaItemRewards); i++)
			{
				NOCONST(InventorySlot)* pInvSlot = inv_InventorySlotCreate(pOutRequest->iRewardSlots++);
				Item* pItemCopy = StructClone(parse_Item, pInRequest->eaItemRewards[i]);
				item_trh_FixupPowers(CONTAINER_NOCONST(Item, pItemCopy));
				pInvSlot->pItem = CONTAINER_NOCONST(Item, pItemCopy);
				eaPush(&pOutRequest->eaRewards, CONTAINER_RECONST(InventorySlot, pInvSlot));
			}
		}
		
		bProcessNumerics = !bProcessNumerics;
	} while(++twice <= 1);
	
#endif
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
bool inv_trh_GiveRewardBags(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
							ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
							GiveRewardBagsData *pRewardBagsData, U32 eOverflow, S32* peFailBag,
							const ItemChangeReason *pReason,
							GameAccountDataExtract *pExtract,
							RewardPackLog *pRewardPackLog)
{
	int NumRewardBags = eaSize(&pRewardBagsData->ppRewardBags);
	int ii;

	if (ISNULL(pEnt)) {
		TRANSACTION_APPEND_LOG_FAILURE("Unable to give reward items: Target entity is NULL!");
		return false;
	}

	for(ii=NumRewardBags-1; ii>=0; ii--)
	{
		//InventoryBag *pRewardBag = (InventoryBag *)eaRemove(&pRewardBagsData->ppRewardBags, ii);
		InventoryBag *pRewardBag = pRewardBagsData->ppRewardBags[ii];

		if ( pRewardBag )
		{
			//skip empty bags
			ANALYSIS_ASSUME(pRewardBag != NULL);
			if (inv_bag_CountItems(pRewardBag, NULL) > 0)
			{
				switch (pRewardBag->pRewardBagInfo->PickupType)
				{
				case kRewardPickupType_Rollover:
				case kRewardPickupType_Interact:
					//these types of bags can never be given in a transaction
					ErrorDetailsf("Reward Table: %s", pRewardBag->pRewardBagInfo->pcRewardTable);
					ErrorfForceCallstack("Invalid reward bag type inside transaction.  Rollover and Interact bags cannot be rewarded by missions and similar places.");
					//DO NOT ignore this case. Fail the transaction.
					TRANSACTION_APPEND_LOG_FAILURE("Invalid reward bag type inside transaction");
					return false;

				case kRewardPickupType_Choose: 
					//this is a choose N of bag
					{
						int ItemCount, iItem, iGiven;

						ItemCount = eaSize(&pRewardBag->ppIndexedInventorySlots);
						iGiven = 0;

						for (iItem = 0; iItem < ItemCount ; iItem++)
						{
							InventorySlot *pSlot = pRewardBag->ppIndexedInventorySlots[iItem];
							Item *pItem = SAFE_MEMBER(pSlot,pItem);
							
							if (!pItem) 
								continue;

							if(pRewardBag->pRewardBagInfo->NumPicks && iGiven >= pRewardBag->pRewardBagInfo->NumPicks)
								break;

							if ( pRewardBag->pRewardBagInfo && pRewardBag->pRewardBagInfo->ExecuteType == kRewardExecuteType_AutoExec )
							{
								//skip auto execute items inside a transaction
							}
							else
							{
								ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

								if (pItemDef)
								{
									if ( StringArrayFind(pRewardBagsData->ppChoices, pItemDef->pchName) == -1 )
										continue;

									iGiven++;

									if (pItemDef->eType == kItemType_Numeric)
									{
										if ( inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, pItemDef->pchName, pItem->count, pItem->numeric_op, pReason) )
										{
											TRANSACTION_APPEND_LOG_SUCCESS(
												"Numeric Item %s value added %d on Ent %s", 
												pItemDef?pItemDef->pchName:pItem?pItem->pchDisplayName:"", 
												pItem->count,
												pEnt->debugName );

											if (pRewardPackLog)
											{
												RewardPackItem *pRewardPackItem = StructCreate(parse_RewardPackItem);
												pRewardPackItem->pchItemName = StructAllocString(pItemDef->pchName);
												pRewardPackItem->iCount = pItem->count;
												eaPush(&pRewardPackLog->ppRewardPackItems, pRewardPackItem);
											}
										}
										else
										{
											TRANSACTION_APPEND_LOG_FAILURE(
												"Could not add %d to Numeric Item %s on Ent %s", 
												pItem->count,
												pItemDef?pItemDef->pchName:pItem?pItem->pchDisplayName:"", 
												pEnt->debugName );
											return false;
										}							
									}
									else
									{
										InvBagIDs destBag;
										NOCONST(Item)* pItemCopy;
										NOCONST(InventoryBagLite)* pLiteBag = NULL;

										// It might be better to just make a list of what _is_ allowed in the overflow bag
										if ( eOverflow == kRewardOverflow_ForceOverflowBag 
											&& pItemDef->eType != kItemType_Title 
											&& pItemDef->eType != kItemType_Lore 
											&& pItemDef->eType != kItemType_Callout 
											&& pItemDef->eType != kItemType_Token)
										{
											destBag = InvBagIDs_Overflow;
										}
										else if (pItemDef->flags & kItemDefFlag_LockToRestrictBags)
										{
											destBag = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, pItem->count, true, pExtract);
											pLiteBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, destBag, pExtract);
										}
										else
										{
											// check for item type specific bag override
											destBag = itemAcquireOverride_FromMissionReward(pItemDef);
											pLiteBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, destBag, pExtract);
											if ( destBag == InvBagIDs_None )
											{
												destBag = InvBagIDs_Inventory;
											}

											if (destBag != InvBagIDs_Overflow && 
												eOverflow == kRewardOverflow_AllowOverflowBag && 
												!pLiteBag && !inv_bag_trh_CanItemDefFitInBag(ATR_PASS_ARGS, pEnt, inv_trh_GetBag(ATR_PASS_ARGS, pEnt, destBag, pExtract), pItemDef, pItem->count)) 
											{
												destBag = InvBagIDs_Overflow;
											}
										}

										pItemCopy = StructCloneDeConst(parse_Item, pItem);

										if ( !pItemCopy )
										{
											Errorf ("StructClone returned NULL");
											TRANSACTION_APPEND_LOG_FAILURE("StructClone returned NULL"); 
											return false;
										}

										if(pItemDef && pItemDef->bDeleteAfterUnlock && pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock)
										{
											pItemCopy->flags |= kItemFlag_Bound;
											inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, true, pItemCopy,pExtract);
											TRANSACTION_APPEND_LOG_SUCCESS(
												"Item [%s<%"FORM_LL"u>]x unlocked costume on Ent [%s]", 
												pItemDef?pItemDef->pchName:pItem?pItem->pchDisplayName:"", 
												pItemCopy->id, pEnt->debugName );
											StructDestroyNoConst(parse_Item, pItemCopy);

											if (pRewardPackLog)
											{
												RewardPackItem *pRewardPackItem = StructCreate(parse_RewardPackItem);
												pRewardPackItem->pchItemName = StructAllocString(pItemDef->pchName);
												pRewardPackItem->iCount = 1;
												eaPush(&pRewardPackLog->ppRewardPackItems, pRewardPackItem);
											}
										}
										else if (destBag != InvBagIDs_None)
										{
											S32 eFlags = ((destBag == InvBagIDs_Overflow) ? ItemAdd_Silent : 0) |
															((eOverflow == kRewardOverflow_AllowOverflowBag) ? ItemAdd_UseOverflow : 0) |
															ItemAdd_ClearID;
											S32 iCount = pItemCopy->count;
											if (inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, destBag, -1, (Item*)pItemCopy, pItemDef->pchName, eFlags, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
											{	
												TRANSACTION_APPEND_LOG_FAILURE(
													"Item Add Failed Item [%s]x%i Bag:Slot [%s:%d] on Ent [%s]", 
													pItemDef ? pItemDef->pchName: "", iCount,
													StaticDefineIntRevLookup(InvBagIDsEnum, destBag), -1,
													pEnt->debugName);

												if (peFailBag)
												{
													(*peFailBag) = destBag;
												}
												StructDestroyNoConstSafe(parse_Item, &pItemCopy);
												return false;
											}
											else
											{
												TRANSACTION_APPEND_LOG_SUCCESS( "Item [%s]x%i Added to Bag:Slot [%s:%d] on Ent [%s]", 
																				pItemDef ? pItemDef->pchName : "", iCount,
																				StaticDefineIntRevLookup(InvBagIDsEnum, destBag), -1,
																				pEnt->debugName);

												if (pRewardPackLog)
												{
													RewardPackItem *pRewardPackItem = StructCreate(parse_RewardPackItem);
													pRewardPackItem->pchItemName = StructAllocString(pItemDef->pchName);
													pRewardPackItem->iCount = iCount;
													eaPush(&pRewardPackLog->ppRewardPackItems, pRewardPackItem);
												}
											}
											StructDestroyNoConstSafe(parse_Item, &pItemCopy);
										}
										else
										{
											StructDestroyNoConstSafe(parse_Item, &pItemCopy);
										}
									}
								}
							}
						}
						if(iGiven < pRewardBag->pRewardBagInfo->NumPicks)
						{
							TRANSACTION_APPEND_LOG_FAILURE(
								"Item Add Failed on Ent %s because a choice among items was required and was not provided", 
								pEnt->debugName );
							return false;
						}
					}
					break;



				case kRewardPickupType_Direct:
					{
						int ItemCount, iItem;

						ItemCount = eaSize(&pRewardBag->ppIndexedInventorySlots);

						for (iItem = 0; iItem < ItemCount; iItem++)
						{
							InventorySlot *pSlot = pRewardBag->ppIndexedInventorySlots[iItem];
							Item *pItem = SAFE_MEMBER(pSlot,pItem);

							if (!pItem) continue;

							if ( pRewardBag->pRewardBagInfo && pRewardBag->pRewardBagInfo->ExecuteType == kRewardExecuteType_AutoExec )
							{
								//skip auto execute items inside a transaction
							}
							else
							{
								ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

								if (pItemDef && pItemDef->eType == kItemType_Numeric)
								{									
									if ( inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, pItemDef->pchName, pItem->count, pItem->numeric_op, pReason) )
									{
										TRANSACTION_APPEND_LOG_SUCCESS(
											"Numeric Item %s value added %d on Ent %s", 
											pItemDef->pchName, 
											pItem->count,
											pEnt->debugName );

											if (pRewardPackLog)
											{
												RewardPackItem *pRewardPackItem = StructCreate(parse_RewardPackItem);
												pRewardPackItem->pchItemName = StructAllocString(pItemDef->pchName);
												pRewardPackItem->iCount = pItem->count;
												eaPush(&pRewardPackLog->ppRewardPackItems, pRewardPackItem);
											}
									}
									else
									{
										TRANSACTION_APPEND_LOG_FAILURE(
											"Could not add %d to Numeric Item %s on Ent %s", 
											pItem->count,
											pItemDef->pchName, 
											pEnt->debugName );
										return false;
									}							
								}
								else
								{
									InvBagIDs destBag;
									NOCONST(InventoryBag)* pBag = NULL;
									NOCONST(Item)* pItemCopy;
									NOCONST(InventoryBagLite)* pLiteBag = NULL;

									//this var assignment to force lock of pEnt->pInventoryV2->ppInventoryBags to not confuse
									//eaIndexedGetUsingInt while using a var index
									NOCONST(InventoryBag) ***pppInventoryBags = &pEnt->pInventoryV2->ppInventoryBags;

									// It might be better to just make a list of what _is_ allowed in the overflow bag
									if ( eOverflow == kRewardOverflow_ForceOverflowBag && pItemDef
										&& pItemDef->eType != kItemType_Title 
										&& pItemDef->eType != kItemType_Lore 
										&& pItemDef->eType != kItemType_Callout 
										&& pItemDef->eType != kItemType_Token)
									{
										destBag = InvBagIDs_Overflow;
									}
									else if (pItemDef && (pItemDef->flags & kItemDefFlag_LockToRestrictBags))
									{
										destBag = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, pItem->count, true, pExtract);
										pLiteBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, destBag, pExtract);
									}
									else
									{
										destBag = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, -1, true, pExtract);
										pLiteBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, destBag, pExtract);
										if ( destBag == InvBagIDs_None )
										{
											destBag = InvBagIDs_Inventory;
										}

										if (destBag != InvBagIDs_Overflow &&
											eOverflow == kRewardOverflow_AllowOverflowBag && 
											!pLiteBag && !inv_bag_trh_CanItemDefFitInBag(ATR_PASS_ARGS, pEnt, inv_trh_GetBag(ATR_PASS_ARGS, pEnt,  destBag, pExtract), pItemDef, pItem->count))
										{//if this is going in a lite bag, it's always going to fit.
											destBag = InvBagIDs_Overflow;
										}
									}

									pItemCopy = StructCloneDeConst(parse_Item, pItem);

									if ( !pItemCopy )
									{
										Errorf ("StructClone returned NULL");
										TRANSACTION_APPEND_LOG_FAILURE("StructClone returned NULL"); 
										return false;
									}


									if(pItemDef && pItemDef->bDeleteAfterUnlock && pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock)
									{
										pItemCopy->flags |= kItemFlag_Bound;
										inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, true, pItemCopy, pExtract);
										TRANSACTION_APPEND_LOG_SUCCESS(
											"Item [%s<%"FORM_LL"u>]x unlocked costume on Ent [%s]", 
											pItemDef?pItemDef->pchName:pItem?pItem->pchDisplayName:"", 
											pItemCopy->id, pEnt->debugName );
										StructDestroyNoConst(parse_Item, pItemCopy);

										if (pRewardPackLog)
										{
											RewardPackItem *pRewardPackItem = StructCreate(parse_RewardPackItem);
											pRewardPackItem->pchItemName = StructAllocString(pItemDef->pchName);
											pRewardPackItem->iCount = 1;
											eaPush(&pRewardPackLog->ppRewardPackItems, pRewardPackItem);
										}
									}
									else if (destBag != InvBagIDs_None)
									{
										S32 eFlags = ((destBag == InvBagIDs_Overflow) ? ItemAdd_Silent : 0) | 
														((eOverflow == kRewardOverflow_AllowOverflowBag) ? ItemAdd_UseOverflow : 0) |
														ItemAdd_ClearID;
										S32 iCount = pItemCopy->count;
										if (inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, destBag, -1, (Item*)pItemCopy, pItemDef->pchName, eFlags, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
										{	
											TRANSACTION_APPEND_LOG_FAILURE(
												"Item Add Failed Item [%s]x%i Bag:Slot [%s:%d] on Ent [%s]", 
												pItemDef ? pItemDef->pchName: "", iCount,
												StaticDefineIntRevLookup(InvBagIDsEnum, destBag), -1,
												pEnt->debugName);

											if (peFailBag)
											{
												(*peFailBag) = destBag;
											}
											StructDestroyNoConstSafe(parse_Item, &pItemCopy);
											return false;
										}
										else
										{
											TRANSACTION_APPEND_LOG_SUCCESS( "Item [%s]x%i Added to Bag:Slot [%s:%d] on Ent [%s]", 
																			pItemDef ? pItemDef->pchName : "", iCount,
																			StaticDefineIntRevLookup(InvBagIDsEnum, destBag), -1,
																			pEnt->debugName);

											if (pRewardPackLog)
											{
												RewardPackItem *pRewardPackItem = StructCreate(parse_RewardPackItem);
												pRewardPackItem->pchItemName = StructAllocString(pItemDef->pchName);
												pRewardPackItem->iCount = iCount;
												eaPush(&pRewardPackLog->ppRewardPackItems, pRewardPackItem);
											}
										}
										StructDestroyNoConstSafe(parse_Item, &pItemCopy);
									}
									else
									{
										StructDestroyNoConstSafe(parse_Item, &pItemCopy);
									}
								}
							}
						}
					}
					break;
				}
			}
		}
	}
	return true;
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome inv_tr_GiveRewardBags(ATR_ARGS, NOCONST(Entity)* pEnt, 
											 CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
											 NON_CONTAINER GiveRewardBagsData *pRewardBagsData,
											 const ItemChangeReason *pReason,
											 GameAccountDataExtract *pExtract)
{
	if (!inv_trh_GiveRewardBags( ATR_PASS_ARGS, pEnt, eaPets, pRewardBagsData, kRewardOverflow_DisallowOverflowBag, NULL, pReason, pExtract, NULL))
	{	
		TRANSACTION_RETURN_LOG_FAILURE(
			"Give Bags Failed on Ent %s", 
			pEnt->debugName );
	}

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Give Bags succeeded on Ent %s", 
		pEnt->debugName );
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
bool inv_trh_GiveNumericBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, InventoryBag *pRewardBag, const ItemChangeReason *pReason)
{
	// use string instead of numeric enum to make structparser happy
	NOCONST(InventoryBagLite)* pBag = CONTAINER_NOCONST(InventoryBagLite, eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */ ));

	if (ISNULL(pEnt))
		return false;

    if(ISNULL(pBag))
    {
        TRANSACTION_APPEND_LOG_FAILURE("Could not find numerics bag on entity");
        return false;
    }
    

	if ( pRewardBag )
	{
		int ItemCount, iItem;
		ItemCount = eaSize(&pRewardBag->ppIndexedInventorySlots);

		for (iItem = 0; iItem < ItemCount; iItem++)
		{
			InventorySlot *pSlot = pRewardBag->ppIndexedInventorySlots[iItem];
			Item *pItem = SAFE_MEMBER(pSlot,pItem);

			if (!pItem)
				continue;
			
			if ( pRewardBag->pRewardBagInfo && pRewardBag->pRewardBagInfo->ExecuteType == kRewardExecuteType_AutoExec )
			{
				//skip auto execute items inside a transaction
			}
			else
			{
				ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

				devassert(!pItemDef || pItemDef->eType == kItemType_Numeric);
				if (pItemDef)
				{
					if ( inv_lite_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, pBag, pItemDef->pchName, pItem->count, pItem->numeric_op, pReason) )
					{
						TRANSACTION_APPEND_LOG_SUCCESS(
							"Numeric Item [%s<%"FORM_LL"u>] value added %d", 
							pItemDef->pchName, 
							pItem->id,
							pItem->count );
					}
					else
					{
						TRANSACTION_APPEND_LOG_FAILURE(
							"Could not add %d to Numeric Item [%s<%"FORM_LL"u>]", 
							pItem->count,
							pItemDef->pchName,
							pItem->id );
						return false;
					}							
				}
			}
		}
	}

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_tr_GiveNumericBag(ATR_ARGS, NOCONST(Entity)* pEnt, NON_CONTAINER InventoryBag *pRewardBag, const ItemChangeReason *pReason )
{
	if ( !inv_trh_GiveNumericBag( ATR_PASS_ARGS, pEnt, pRewardBag, pReason ) )
	{	
		TRANSACTION_RETURN_LOG_FAILURE( "Give Bag Numeric Failed" );
	}

	TRANSACTION_RETURN_LOG_SUCCESS( "Give Bag succeeded" );
}


void inv_FillItemChangeReason(ItemChangeReason *pReason, Entity *pEnt, const char *pcReason, const char *pcDetail)
{
	pReason->pcReason = allocAddString(pcReason);
	pReason->pcDetail = pcDetail;
	pReason->bUGC = !!zmapIsUGCGeneratedMap(NULL);

#if defined GAMESERVER || defined GAMECLIENT
	pReason->pcMapName = zmapInfoGetPublicName(NULL);
#else
	pReason->pcMapName = allocAddString(GlobalTypeToName(GetAppGlobalType()));
#endif

	if (pEnt) {
		entGetPos(pEnt, pReason->vPos);
	}
}

void inv_FillItemChangeReasonKill(ItemChangeReason *pReason, Entity *pEnt, Entity *pKilledEntity)
{
	if (pKilledEntity->pCritter) {
		CritterDef *pCritterDef = GET_REF(pKilledEntity->pCritter->critterDef);
		inv_FillItemChangeReason(pReason, pEnt, "Loot:KillCritter", pCritterDef->pchName);
	} else {
		inv_FillItemChangeReason(pReason, pEnt, "Loot:KillOther", pKilledEntity->debugName);
	}
	pReason->bKillCredit = true;
}

void inv_FillItemChangeReasonStore(ItemChangeReason *pReason, Entity *pEnt, const char *pcReason, const char *pcDetail)
{
	inv_FillItemChangeReason(pReason, pEnt, pcReason, pcDetail);
	pReason->bFromStore = true;
}

ItemChangeReason *inv_CreateItemChangeReason(Entity *pEnt, const char *pcReason, const char *pcDetail)
{
	ItemChangeReason *pReason = StructCreate(parse_ItemChangeReason);
	inv_FillItemChangeReason(pReason, pEnt, pcReason, pcDetail);
	return pReason;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOldItem, ".Flags, .Id, .Pppowers, .Ipowerfactor, .Level_Useaccessor, .Minlevel_Useaccessor, .Quality, .Ppitempowerdefrefs, .Palgopet, .Pcontainerinfo, .Pdoorkey, .Iwarpchargesused, .Hitem, .Hcostumeref, .Hmission");
NOCONST(Item)* inv_trh_ent_CreateV2ItemFromV1(ATR_ARGS, ATH_ARG NOCONST(ItemV1)* pOldItem, int count)
{
	NOCONST(Item)* pNewItem = StructCreateNoConst(parse_Item);
	ItemDef* pDef = GET_REF(pOldItem->hItem);
	pNewItem->flags = pOldItem->flags;
	pNewItem->id = pOldItem->id;
	pNewItem->ppPowers = pOldItem->ppPowers;
	pNewItem->count = count;
	pOldItem->ppPowers = NULL;
	COPY_HANDLE(pNewItem->hItem, pOldItem->hItem);
	if (pOldItem->iPowerFactor != 0 ||
		pOldItem->Level_UseAccessor != 0 || 
		pOldItem->MinLevel_UseAccessor != 0 || 
		pOldItem->Quality != 0 || 
		eaSize(&pOldItem->ppItemPowerDefRefs) > 0)
	{
		pNewItem->pAlgoProps = StructCreateNoConst(parse_AlgoItemProps);
		pNewItem->pAlgoProps->iPowerFactor = pOldItem->iPowerFactor;
		pNewItem->pAlgoProps->Level_UseAccessor = pOldItem->Level_UseAccessor;
		pNewItem->pAlgoProps->MinLevel_UseAccessor = pOldItem->MinLevel_UseAccessor;
		pNewItem->pAlgoProps->ppItemPowerDefRefs = pOldItem->ppItemPowerDefRefs;
		pNewItem->pAlgoProps->Quality = pOldItem->Quality;
		pOldItem->ppItemPowerDefRefs = NULL;
	}
	if (IS_HANDLE_ACTIVE(pOldItem->hCostumeRef) ||
		IS_HANDLE_ACTIVE(pOldItem->hMission) || 
		pOldItem->pAlgoPet || 
		pOldItem->pContainerInfo || 
		pOldItem->pDoorKey)
	{
		pNewItem->pSpecialProps = StructCreateNoConst(parse_SpecialItemProps);
		COPY_HANDLE(pNewItem->pSpecialProps->hCostumeRef, pOldItem->hCostumeRef);
		pNewItem->pSpecialProps->pDoorKey = pOldItem->pDoorKey;
		if(!pNewItem->pSpecialProps->pDoorKey && IS_HANDLE_ACTIVE(pOldItem->hMission))
		{
			pNewItem->pSpecialProps->pDoorKey = StructCreateNoConst(parse_ItemDoorKey);
		}

		// only copy this if its active which means pDoorKey is non-null
		if(IS_HANDLE_ACTIVE(pOldItem->hMission))
		{
			COPY_HANDLE(pNewItem->pSpecialProps->pDoorKey->hMission, pOldItem->hMission);
		}

		pNewItem->pSpecialProps->pAlgoPet = pOldItem->pAlgoPet;
		pNewItem->pSpecialProps->pContainerInfo = pOldItem->pContainerInfo;
		pOldItem->pAlgoPet = NULL;
		pOldItem->pContainerInfo = NULL;
		pOldItem->pDoorKey = NULL;
	}
	if (!pDef)
	{
		Errorf("Tried to migrate V1 item to V2 but it had a non-existant def %s!", REF_STRING_FROM_HANDLE(pOldItem->hItem));
	}
	else if (pDef->pWarp)
	{
		if (pDef->pWarp->iWarpChargesMax_DEPRECATED > 0)
		{
			pNewItem->count = pDef->pWarp->iWarpChargesMax_DEPRECATED - pOldItem->iWarpChargesUsed;
		}
	}
	return pNewItem;
}
AUTO_TRANS_HELPER
	ATR_LOCKS(pOldSlot, ".Pitem, .Bhidecostumes, .Pchname, .Hslottype, .Count")
	ATR_LOCKS(pNewSlot, ".Bhidecostumes, .Pchname, .Pitem, .Hslottype");
bool inv_trh_ent_MigrateSlotV1ToV2(ATR_ARGS, ATH_ARG NOCONST(InventorySlotV1)* pOldSlot, ATH_ARG NOCONST(InventorySlot)* pNewSlot)
{
	NOCONST(ItemV1)* pOldItem = pOldSlot->pItem;
	NOCONST(Item)* pNewItem = pOldSlot->pItem ? inv_trh_ent_CreateV2ItemFromV1(ATR_PASS_ARGS, pOldItem, pOldSlot->count) : NULL;

	pNewSlot->bHideCostumes = pOldSlot->bHideCostumes;
	pNewSlot->pchName = allocAddString(pOldSlot->pchName);
	COPY_HANDLE(pNewSlot->hSlotType, pOldSlot->hSlotType);
	pNewSlot->pItem = pNewItem;
	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOldBag, ".Ppindexedinventoryslots, .N_Additional_Slots, .Pguildbankinfo, .Bhidecostumes, .eaiActiveSlots")
	ATR_LOCKS(pNewBag, ".N_Additional_Slots, .Pguildbankinfo, .Bhidecostumes, .Ppindexedinventoryslots, .eaiActiveSlots");
bool inv_trh_ent_MigrateBagV1ToV2(ATR_ARGS, ATH_ARG NOCONST(InventoryBagV1)* pOldBag, ATH_ARG NOCONST(InventoryBag)* pNewBag)
{
	int i;
	for (i = 0; i < eaSize(&pOldBag->ppIndexedInventorySlots); i++)
	{
		NOCONST(InventorySlot)* pNewSlot = StructCreateNoConst(parse_InventorySlot);
		inv_trh_ent_MigrateSlotV1ToV2(ATR_PASS_ARGS, pOldBag->ppIndexedInventorySlots[i], pNewSlot);
		eaIndexedPushUsingStringIfPossible(&pNewBag->ppIndexedInventorySlots, pNewSlot->pchName, pNewSlot);
	}
	pNewBag->n_additional_slots = pOldBag->n_additional_slots;
	pNewBag->pGuildBankInfo = pOldBag->pGuildBankInfo;
	pOldBag->pGuildBankInfo = NULL;
	pNewBag->eaiActiveSlots = pOldBag->eaiActiveSlots;
	pOldBag->eaiActiveSlots = NULL;
	pNewBag->bHideCostumes = pOldBag->bHideCostumes;
	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOldBag, ".Ppindexedinventoryslots, .Bagid")
	ATR_LOCKS(pNewBagLite, ".Ppindexedliteslots");
bool inv_trh_ent_MigrateBagV1ToV2Lite(ATR_ARGS, ATH_ARG NOCONST(InventoryBagV1)* pOldBag, ATH_ARG NOCONST(InventoryBagLite)* pNewBagLite)
{
	int i;
	for (i = 0; i < eaSize(&pOldBag->ppIndexedInventorySlots); i++)
	{
		if (pOldBag->ppIndexedInventorySlots[i]->pItem)
		{
			NOCONST(InventorySlotLite)* pNewSlot = StructCreateNoConst(parse_InventorySlotLite);
			NOCONST(ItemV1)* pOldItem = pOldBag->ppIndexedInventorySlots[i]->pItem;
			ItemDef* pDef = GET_REF(pOldItem->hItem);
			if (!pDef)
			{
				Errorf("Failed to migrate v1 inventory bag %s, item def %s doesn't exist.", StaticDefineIntRevLookup(InvBagIDsEnum, pOldBag->BagID), REF_STRING_FROM_HANDLE(pOldItem->hItem));
				return false;
			}
			SET_HANDLE_FROM_REFERENT(g_hItemDict, pDef, pNewSlot->hItemDef);
			pNewSlot->pchName = allocAddString(pDef->pchName);
			pNewSlot->count = pDef->eType == kItemType_Numeric ? pOldItem->iNumericValue : pOldBag->ppIndexedInventorySlots[i]->count;
			if (!eaIndexedPushUsingStringIfPossible(&pNewBagLite->ppIndexedLiteSlots, pNewSlot->pchName, pNewSlot))
			{
				NOCONST(InventorySlotLite)* pExistSlot = eaIndexedGetUsingString(&pNewBagLite->ppIndexedLiteSlots, pNewSlot->pchName);
				StructDestroyNoConstSafe(parse_InventorySlotLite, &pNewSlot);
				if (!pExistSlot)
				{
					Errorf("Failed to migrate v1 inventory bag %s, item %s could not be added to array.", StaticDefineIntRevLookup(InvBagIDsEnum, pOldBag->BagID), REF_STRING_FROM_HANDLE(pOldItem->hItem));
					return false;
				}
				pExistSlot->count = pDef->eType == kItemType_Numeric ? pOldItem->iNumericValue : pOldBag->ppIndexedInventorySlots[i]->count;
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".pInventoryV2, .pInventoryV1_Deprecated, .pchar.ilevelexp, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool inv_trh_ent_MigrateInventoryV1ToV2(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	int i;
	NOCONST(InventoryV1)* pOldInv = pEnt->pInventoryV1_Deprecated;
	NOCONST(Inventory)* pNewInv = pEnt->pInventoryV2;
	if (NONNULL(pOldInv))
	{
		for (i = 0; i < eaSize(&pOldInv->ppInventoryBags); i++)
		{
			NOCONST(InventoryBagV1)* pOldBag = pOldInv->ppInventoryBags[i];
			NOCONST(InventoryBag)* pNewBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, pOldBag->BagID, NULL);
			NOCONST(InventoryBagLite)* pNewBagLite = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, pOldBag->BagID, NULL);
			if (pNewBag)
			{
				inv_trh_ent_MigrateBagV1ToV2(ATR_PASS_ARGS, pOldBag, pNewBag);
			}
			else if (pNewBagLite)
			{
				inv_trh_ent_MigrateBagV1ToV2Lite(ATR_PASS_ARGS, pOldBag, pNewBagLite);
			}
			else
			{
				DefaultInventory* pDefault = GET_REF(pOldBag->inv_def);
				InvBagDef* pBagDef = pDefault ? eaIndexedGetUsingInt(&pDefault->InventoryBags, pOldBag->BagID) : NULL;

				if (!pBagDef)
				{
					Errorf("Entity inventoryV2 migration encountered an unexpected issue: Bag def %s in default inventory %s no longer exists! Migration will attempt to continue, cross your fingers.", StaticDefineInt_FastIntToString(InvBagIDsEnum, pOldBag->BagID), REF_STRING_FROM_HANDLE(pOldBag->inv_def));

					//This is bad. An entity is being migrated whose DefaultInventory has been deleted entirely.
					//Try a best-guess migration, leave the bad reference intact.

					if (pOldBag->BagID == InvBagIDs_Numeric ||
						pOldBag->BagID == InvBagIDs_Lore ||
						pOldBag->BagID == InvBagIDs_Tokens ||
						pOldBag->BagID == InvBagIDs_Titles)
					{
						//this is a bag that should probably be a LiteBag under the new system.
						pNewBagLite = StructCreateNoConst(parse_InventoryBagLite);
						pNewBagLite->BagID = pOldBag->BagID;
						COPY_HANDLE(pNewBagLite->inv_def, pOldBag->inv_def);

						eaIndexedPushUsingIntIfPossible(&pEnt->pInventoryV2->ppLiteBags, pNewBagLite->BagID, pNewBagLite);

						inv_trh_ent_MigrateBagV1ToV2Lite(ATR_PASS_ARGS, pOldBag, pNewBagLite);
					}
					else
					{
						pNewBag = StructCreateNoConst(parse_InventoryBag);
						pNewBag->BagID = pOldBag->BagID;
						COPY_HANDLE(pNewBag->inv_def, pOldBag->inv_def);

						eaIndexedAdd(&pEnt->pInventoryV2->ppInventoryBags, pNewBag);

						inv_trh_ent_MigrateBagV1ToV2(ATR_PASS_ARGS, pOldBag, pNewBag);
					}
				}
				if (pBagDef && pBagDef->eType == InvBagType_Item)
				{
					pNewBag = inv_ent_trh_AddBagFromDef(ATR_PASS_ARGS, pEnt, eaIndexedGetUsingInt(&pDefault->InventoryBags, pOldBag->BagID));
					SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict, pDefault, pNewBag->inv_def);
					inv_trh_ent_MigrateBagV1ToV2(ATR_PASS_ARGS, pOldBag, pNewBag);
				}
				else if (pBagDef && pBagDef->eType == InvBagType_ItemLite)
				{
					pNewBagLite = inv_ent_trh_AddLiteBagFromDef(ATR_PASS_ARGS, pEnt, eaIndexedGetUsingInt(&pDefault->InventoryBags, pOldBag->BagID));
					SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict, pDefault, pNewBagLite->inv_def);
					inv_trh_ent_MigrateBagV1ToV2Lite(ATR_PASS_ARGS, pOldBag, pNewBagLite);
				}
			}
		}
	}

	if (NONNULL(pEnt->pChar))
		pEnt->pChar->iLevelExp = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "Level");
	return true;
}

//transaction helper to move all items from the source bag to the dest bag
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Pplayer.Playertype")
	ATR_LOCKS(pSrcBag, ".*")
	ATR_LOCKS(pDestBag, ".*");
int inv_ent_trh_MoveAllItemsFromBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(InventoryBag)* pSrcBag, ATH_ARG NOCONST(InventoryBag)* pDestBag, bool bUseOverflow, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	bool bFailed = false;
	int NumSlots;
	int i;

	if (!pSrcBag)
	{
		return false;
	}
	if (!pDestBag)
	{
		return false;
	}

	//loop for all slots in the source bag
	NumSlots = eaSize(&pSrcBag->ppIndexedInventorySlots);
	for(i = 0; i < NumSlots; ++i)
	{
		NOCONST(InventorySlot)* pSlot = pSrcBag->ppIndexedInventorySlots[i];

		if (pSlot->pItem)
		{
			if (!inv_bag_trh_MoveItem(	ATR_PASS_ARGS, true, pEnt, NULL, pDestBag, -1, 
				pEnt, NULL, pSrcBag, i, -1, false, bUseOverflow, pReason, pReason, pExtract))
				return false;
		}
	}
	return true;
}




#include "AutoGen/inventoryCommon_h_ast.c"
