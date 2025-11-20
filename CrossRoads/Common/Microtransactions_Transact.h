#ifndef MICROTRANSACTIONS_TRANSACT_H
#define MICROTRANSACTIONS_TRANSACT_H

#pragma once
GCC_SYSTEM

typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct ItemChangeReason ItemChangeReason;
typedef struct ItemDef ItemDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct GamePermissionDef GamePermissionDef;

AUTO_ENUM;
typedef enum MicroTransGrantPartResult
{
	kMicroTransGrantPartResult_Failed,
	kMicroTransGrantPartResult_NothingUnlocked,
	kMicroTransGrantPartResult_Success,
} MicroTransGrantPartResult;

//The main function that grants a MicroTransactionDef to an entity
bool trhMicroTransactionDef_Grant(ATR_ARGS,
								  ATH_ARG NOCONST(Entity)* pEnt,
								  CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
								  ATH_ARG NOCONST(GameAccountData) *pAccountData,
								  MicroTransactionRewards *pRewards,
								  const MicroTransactionDef *pDef,
								  const char *pDefName,
								  const ItemChangeReason *pReason);

// Unlock a costume reference for the entity
bool trhMicroTrans_UnlockCostumeRef(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, PlayerCostume *pCostume);
bool trhMicroTrans_UnlockCostumeRef_Force(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, PlayerCostume *pCostume);

// Unlock an item with costume references on it for the entity
bool trhMicroTrans_UnlockCostume(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, ItemDef *pItemDef);
bool trhMicroTrans_UnlockCostume_Force(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData) *pAccountData, ItemDef *pItemDef);

// Unlock an item with vanity pet power references on it for the entity
bool trhMicroTrans_UnlockVanityPet(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData, PowerDef *pPowerDef);
bool trhMicroTrans_UnlockVanityPet_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pAccountData, PowerDef *pPowerDef);

// Mark the fact that you purchased a product
void trhMicroTransDef_AddPurchaseStamp(ATH_ARG NOCONST(GameAccountData) *pAccountData,
									   const MicroTransactionDef *pDef,
									   const char *pDefName,
									   const AccountProxyProduct *pProduct,
									   const char *pCurrency);

bool trhMicroTrans_GrantPermission(ATR_ARGS, 
	ATH_ARG NOCONST(Entity) *pEnt,
	ATH_ARG NOCONST(GameAccountData) *pAccountData,
	GamePermissionDef *pPermissionDef);


#endif //MICROTRANSACTIONS_TRANSACT_H