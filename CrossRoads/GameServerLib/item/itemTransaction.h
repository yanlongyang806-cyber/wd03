/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef ITEMTRANSACTION_H
#define ITEMTRANSACTION_H

#include "inventoryCommon.h"
#include "objTransactions.h"

typedef struct Entity Entity;
typedef struct Guild Guild;
typedef struct Item Item;
typedef struct ItemDef ItemDef;
typedef struct OldRewardTable OldRewardTable;
typedef struct TransactionReturnVal TransactionReturnVal;

bool itemtransaction_MoveItemAcrossEnts(SA_PARAM_NN_VALID TransactionReturnVal* pReturnVal,
										S32 iSrcType, U32 uiSrcID, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID,
										S32 iDstType, U32 uiDstID, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iCount,
										const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason);
bool itemtransaction_MoveItemGuildAcrossEnts(const char* pchActionName, TransactionReturnCallback cbFunc, void* cbData,
											 Entity* pPlayerEnt, Entity* pTransEnt, Guild* pGuild, 
											 int bSrcGuild, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID, int iSrcEPValue, int iSrcCount, 
											 int bDstGuild, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iDstEPValue);

void itemtransaction_RemoveItemFromBag(Entity *be, InvBagIDs BagID, const ItemDef *pRef, int iSlot, U64 uItemID, int iCount, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void* userData);
void itemtransaction_RemoveItemFromBagEx(Entity *pEnt, 
										 InvBagIDs BagID, const ItemDef *pItemDef, int iSlot, U64 uItemID, int iCount, 
										 int iPowExpiredBag, int iPowExpiredSlot, const ItemChangeReason *pReason,
										 TransactionReturnCallback userFunc, void* userData);

void itemtransaction_AddNumeric(Entity *pEnt, const char *itemName, F32 value, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void *pData);
void itemtransaction_SetNumeric(Entity *pEnt, const char *itemName, F32 value, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void *pData);

typedef struct GiveRewardBagsData GiveRewardBagsData;
void rewardbagsdata_Log(GiveRewardBagsData *pRewardBagsData, char **pestrSuccess);

void itemtransaction_MoveItemGuildAcrossEnts_Wrapper(TransactionReturnVal *returnVal, GlobalType eServerTypeToRunOn, 
														GlobalType pPlayerEnt_type, ContainerID pPlayerEnt_ID,
														int iPetIdx, GlobalType pPetIdx_type, const U32 * const * eaPets,
														GlobalType pGuild_type, ContainerID pGuild_ID,
														GlobalType pGuildBank_type, ContainerID pGuildBank_ID,
														int bSrcGuild, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID, int iSrcEPValue, int iSrcCount, 
														int bDstGuild, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iDstEPValue,
														const ItemChangeReason *pReason, const GameAccountDataExtract *pExtract);

void itemtransaction_MoveItemGuild_Wrapper(TransactionReturnVal *returnVal, GlobalType eServerTypeToRunOn, 
														GlobalType pPlayerEnt_type, ContainerID pPlayerEnt_ID, 
														GlobalType pPet_type, const U32 * const * eaPets,
														GlobalType pGuild_type, ContainerID pGuild_ID,
														GlobalType pGuildBank_type, ContainerID pGuildBank_ID,
														int bSrcGuild, int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID, int iSrcEPValue, int iSrcCount, 
														int bDstGuild, int iDstBagID, int iDstSlotIdx, U64 uDstItemID, int iDstEPValue,
														const ItemChangeReason *pReason, const GameAccountDataExtract *pExtract);

bool ItemOpenExperienceGiftCanBeFilled(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ItemDef* pItemDef);

#endif