#ifndef ITEMSERVER_H
#define ITEMSERVER_H

#include "ItemEnums.h"
#include "objTransactions.h"

typedef struct AllegianceDef AllegianceDef;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct InventoryBag InventoryBag;
typedef struct Item Item;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct ItemDef ItemDef;
typedef struct ItemPowerDefRef ItemPowerDefRef;
typedef struct NOCONST(Item) NOCONST(Item);
typedef struct Player Player;
typedef struct PowerDef PowerDef;

typedef struct MessageCBData
{
	EntityRef targetEnt;
	char* pMsg;
	TransactionReturnCallback cbFunc;
	void* cbData;
} MessageCBData;

AUTO_STRUCT;
typedef struct ItemMoveCBData
{
	EntityRef uiEntRef;
	int iSrcBagID;
	int iSrcSlot;
	int iDstBagID;
	int iDstSlot;
	int iCount;
} ItemMoveCBData;

AUTO_STRUCT;
typedef struct IdentifiedItemCBData
{
	EntityRef uiEntRef;
	U64 ID;
} IdentifiedItemCBData;

AUTO_STRUCT;
typedef struct ItemRewardPackCBData
{
	EntityRef erEnt;
	REF_TO(ItemDef) hRewardPackItem; AST(REFDICT(ItemDef))
	InventoryBag** eaRewardBags; AST(NO_INDEX)
} ItemRewardPackCBData;

AUTO_STRUCT;
typedef struct ItemOpenMicroSpecialCBData
{
	EntityRef erEnt;
	REF_TO(ItemDef) hItemDef; AST(REFDICT(ItemDef))

} ItemOpenMicroSpecialCBData;

typedef struct ItemEquipCB
{
	ContainerID cidEntID;
	GlobalType eEntType;
	S32 iDstBagID;
	S32 iSrcBagID;
	S32 iDstBagSlot;
	TransactionReturnCallback pCallback;
	void *pUserData;
}ItemEquipCB;

void Item_LoadItemTagInfo(void);

void item_MakeBags(SA_PARAM_NN_VALID Entity *pPlayer);
void item_ActivateCooldown( Entity * pEnt, Item * pItem );

void ClearNewItemList(Entity *pEnt);
void GiveItem(Entity *pEnt, char *pchItemName);
void GiveAndEquipItem(Entity *pEnt, char *pchItemName);
void RemoveItem(Entity *pEnt, char *pchItemName);

void item_RemoveFromBag(Entity *pEnt, S32 BagID, S32 iSlot, S32 iCount, const char* msgTUse);
void item_RemoveByID(Entity *pEnt, U64 itemID, U32 iCount, const ItemChangeReason *pReason);

bool item_AllPowersExpired(Item* pItem, bool bIncludeCharges);
bool item_AnyPowersExpired(Item* pItem, bool bIncludeCharges);

void ItemCollectPetOverflow(Entity *pPlayerEnt, GameAccountDataExtract *pExtract);

bool item_RemoveFromBagEx(Entity *pEnt, S32 BagID, S32 iSlot, S32 iCount, 
						  S32 iItemPowExpiredBag, S32 iItemPowExpiredSlot, bool bCheckDiscard,
						  const char* msgToUse, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract,
						  TransactionReturnCallback cbFunc, void* cbData);

NOCONST(Item)* item_CreateAlgoItem(ATR_ARGS, ItemDef *pBaseRecipeDef, ItemQuality eQuality, ItemPowerDefRef **eaItemPowerDefRefs);

void gslBagChangeActiveSlotWhenReady(Entity* pEnt, S32 eBagID, S32 iActiveIndex, S32 iNewActiveSlot, U32 uiTime, U32 uiRequestID, F32 fDelayOverride);
void gslUpdateActiveSlotRequests(Entity* pEnt, F32 fElapsed, GameAccountDataExtract* pExtract);


void gslItem_ChargeForWarp(Entity *pEnt, Item *pItem);
void gslItem_RollbackWarp(Entity *pEnt, Item *pItem);
void gslBagChangeActiveSlotWithIndexWhenReady(Entity* pEnt, S32 eBagID, S32 index, S32 iNewActiveSlot);
void gslBagClearActiveBagSlot(Entity* pEnt, S32 eBagID, S32 index);

// Needed by UGC:
void InventoryClear(Entity* pEnt);
void reward_execute_item(Entity *pEnt, Item *pItem);
#endif


