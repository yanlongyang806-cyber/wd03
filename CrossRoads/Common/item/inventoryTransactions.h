#pragma once
GCC_SYSTEM

#include "objTransactions.h"

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(EntityBuild) NOCONST(EntityBuild);
typedef struct InventoryBag InventoryBag;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct Item Item;
typedef struct MissionDef MissionDef;
typedef struct TradeBag TradeBag;
typedef struct MoveItemGuildStruct MoveItemGuildStruct;

bool inv_ent_trh_RemoveMissionItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pMissionName, bool bIncludeGrantItems, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
void invtransaction_trade(Entity *pEnt1, Entity *pEnt2, TradeBag *pTrade1, TradeBag *pTrade2, TransactionReturnCallback func, void *pvUserData );
int inv_ent_trh_BuildSwap(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(EntityBuild)* pBuild, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome inv_ent_tr_ClearBag(ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract);

enumTransactionOutcome inv_ent_tr_AddDoorKeyItemForFlashbackMission(ATR_ARGS, NOCONST(Entity)* pEnt, MissionDef *pMissionDef, int iMissionLevel, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);

void SetSkill(Entity* pEnt, char *pString);
void SetSkillType(Entity* pEnt, int SkillType);
void SetSkillLevel(Entity* pEnt, S32 value);

void inv_ent_FixItemIDs(ATH_ARG NOCONST(Entity) *pEnt);
void inv_ent_FixIndexedItemNames(NOCONST(Entity) *pEnt);
void inv_ent_InitializeNewPlayerSettings(NOCONST(Entity) *pEnt);

// Updates the uSetCount on all ItemSet-related Items in the Entity's Inventory
void entity_UpdateItemSetsCount(SA_PARAM_OP_VALID Entity *pEnt);

//Fixes up bound items, try to unlock costumes
bool inv_ent_FixupBoundItems(SA_PARAM_NN_VALID Entity *pEnt);

void inv_FixupBags(Entity *pEnt, TransactionReturnCallback, void *userData);
enumTransactionOutcome inv_tr_UpdateSharedBankBag(ATR_ARGS, NOCONST(Entity)* pEnt, S32 iBagID, GameAccountDataExtract *pExtract);

// new AddItem
// ===============================================================================

bool invtransaction_AddItem(Entity *pEnt, int BagID, int iSlot, Item *item, S32 eFlags, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void* userData);

enumTransactionOutcome inv_trh_UpdateAdditionalInventorySlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract);
enumTransactionOutcome inv_trh_UpdateBankBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 iBagID, GameAccountDataExtract *pExtract);

