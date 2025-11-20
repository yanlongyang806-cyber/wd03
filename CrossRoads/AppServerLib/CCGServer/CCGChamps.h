/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

//
// This file is for stuff that is clearly champions ccg specific
//

AUTO_STRUCT;
typedef struct CCGChampsPowerSetDef
{
	STRING_POOLED powerTreeName;	AST(POOL_STRING KEY)
	STRING_POOLED powerPackName;	AST(POOL_STRING)
	U32 heroBaseCard;
} CCGChampsPowerSetDef;

AUTO_STRUCT;
typedef struct CCGChampsPowerSetDefList
{
	EARRAY_OF(CCGChampsPowerSetDef) powerSetDefs;			AST(NAME("Power"))
} CCGChampsPowerSetDefList;

typedef struct NOCONST(CCGPlayerData) NOCONST(CCGPlayerData);
typedef struct CCGCallback CCGCallback;
typedef struct CCGTransactionReturnVal CCGTransactionReturnVal;
typedef struct CCGPlayer CCGPlayer;

void CCGChamps_InitPlayerData(NOCONST(CCGPlayerData) *playerData);

void CCGChamps_InitEarly(void);

void CCGChamps_GetCharacters(CCGCallback *cb, CCGPlayer *player);

CCGTransactionReturnVal *CCGChamps_CreateDeck(CCGCallback *cb, CCGPlayer *player, const char *charName);