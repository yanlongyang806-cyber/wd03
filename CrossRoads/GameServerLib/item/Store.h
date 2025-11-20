/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "itemEnums.h"

typedef struct ContactDef ContactDef;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct StoreDef StoreDef;
typedef struct StoreDiscountInfo StoreDiscountInfo;
typedef struct StoreItemInfo StoreItemInfo;
typedef struct StoreSellableItemInfo StoreSellableItemInfo;
typedef struct ContactDialog ContactDialog;

void store_GetStoreItemInfo(Entity *pPlayerEnt, ContactDef *pContactDef, StoreDef *pStoreDef,
								StoreItemInfo ***peaItemInfo, StoreItemInfo ***peaUnavailableItemInfo,
								StoreDiscountInfo ***peaDiscountInfo, GameAccountDataExtract *pExtract);
void store_GetStoreOwnedItemInfo(Entity *pPlayerEnt, Entity* pOwnerEnt, ContactDef *pContactDef, StoreDef *pStoreDef, StoreItemInfo ***peaItemInfo, InvBagIDs eBagToSearch, GameAccountDataExtract *pExtract);

void store_RefreshStoreItemInfo(Entity *pPlayerEnt, StoreItemInfo ***peaItemInfo, StoreItemInfo ***peaUnavailableItemInfo, GameAccountDataExtract *pExtract);
void store_UpdateStoreProvisioning(Entity *pPlayerEnt, ContactDialog *pDialog, GameAccountDataExtract *pExtract);

void injuryStore_SetTarget(Entity* pPlayerEnt, U32 uiTargetEntType, U32 uiTargetEntID);

bool store_UpdateSellItemInfo(Entity* pEnt, StoreDef* pStore, StoreSellableItemInfo*** peaSellableItems, GameAccountDataExtract *pExtract);

void store_Close(Entity* pPlayerEnt);

bool gslPersistedStore_UpdateItemInfo(Entity* pEnt, StoreItemInfo ***peaItemInfo);
void gslPersistedStore_PlayerAddRequest(Entity* pEnt, StoreDef* pStoreDef);
void gslPersistedStore_PlayerRemoveRequests(Entity* pEnt);
