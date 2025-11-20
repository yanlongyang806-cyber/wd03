/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWERREPLACE_H_
#define POWERREPLACE_H_

#include "referencesystem.h"
#include "itemEnums.h"

GCC_SYSTEM

typedef struct Entity		Entity;
typedef struct Power		Power;
typedef struct Character	Character;

typedef struct NOCONST(PowerReplace) NOCONST(PowerReplace);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(InventoryBag) NOCONST(InventoryBag);

extern DictionaryHandle g_hPowerReplaceDefDict;

AUTO_STRUCT;
typedef struct PowerReplaceDef
{
	char *pchName;		AST(STRUCTPARAM KEY)
	InvBagIDs eBagID;	AST(STRUCTPARAM)
}PowerReplaceDef;

AUTO_STRUCT;
typedef struct PowerReplace
{
	U32 uiID;						AST()
	REF_TO(PowerReplaceDef) hDef;	AST(REFDICT(PowerReplaceDef))
	U32 uiBasePowerID;				AST()
	U32 uiReplacePowerID;			NO_AST
	Power **ppEnhancements;			NO_AST
}PowerReplace; 

NOCONST(PowerReplace) *powerReplace_GetFromIDHelper(ATH_ARG NOCONST(Entity)* pEnt, U32 uiSlotID);
const PowerReplace *PowerReplace_GetFromID(Entity *pEnt, U32 uiSlotID);
const PowerReplace *PowerReplace_GetFromBagID(Entity *pEnt, InvBagIDs uiBagID);
PowerReplace *Entity_FindPowerReplace(Entity *pEnt,const PowerReplaceDef *pDef);

void power_GetEnhancementsPowerReplace(int iPartitionIdx,Entity *pEnt,Power *ppow,Power ***ppPowersAttached);

void Entity_ReBuildPowerReplace(Entity *pEnt);

void PowerReplace_reset(Entity *pEnt);

#endif