#ifndef GSLGAMEACCOUNTDATA_H
#define GSLGAMEACCOUNTDATA_H

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct ItemDef ItemDef;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);

AUTO_ENUM;
typedef enum GADPermission
{
	kGADPermission_Server	= (1<<0),
	kGADPermission_GM		= (1<<1),
	kGADPermission_Client	= (1<<2),
} GADPermission;

AUTO_STRUCT;
typedef struct GADAttribPermission
{
	char *pchAttribute;				AST(KEY)
		//The attribute that the permission relates to

	GADPermission ePermission;		AST(FLAGS)
		// The permissions that this attribute has
} GADAttribPermission;

AUTO_STRUCT;
typedef struct GADAttribPermissions
{
	GADAttribPermission **eaPermissions;  AST(NAME("GADAttribPermission") NO_INDEX)
} GADAttribPermissions;

void gslGAD_FixupGameAccount(Entity *pEnt, bool bInitialLogin);

bool gslGAD_trh_GrantEachVanityPet(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG  NOCONST(GameAccountData) *pData);
void gslGAD_UnlockVanityPet(Entity *pEnt, ItemDef *pItemDef, U64 ulItemID);
enumTransactionOutcome item_ent_tr_UnlockVanityPets(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GameAccountData) *pAccountData, ItemDef *pItemDef, U64 ulItemID, const ItemChangeReason *pReason);

void gslGAD_SetAttrib(Entity *pEnt, const char *pchAttrib, const ACMD_SENTENCE pchValue);
void gslGAD_ProcessNewVersion(Entity *pEnt, bool bForceProcess, bool bInitialL);

enumTransactionOutcome gslGAD_tr_SetReference(ATR_ARGS, NOCONST(Entity) *pEnt);

extern GADAttribPermission **g_eaGADPermissions;

#endif //GSLGAMEACCOUNTDATA_H