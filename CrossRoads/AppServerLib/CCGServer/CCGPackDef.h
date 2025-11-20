/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef U32 ContainerID;

//
// Define packs
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGRandomSlotDef
{
	// number of random cards from the group to generate for this deck
	const U32 count;								AST(PERSIST)

	CONST_STRING_POOLED printRunName;				AST(PERSIST POOL_STRING)
	CONST_STRING_POOLED groupName;					AST(PERSIST POOL_STRING)
} CCGRandomSlotDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct CCGPackDef
{
	const ContainerID containerID;					AST(PERSIST KEY)

	// The name of this pack.
	CONST_STRING_POOLED packName;					AST(PERSIST POOL_STRING)

	// Can this pack be issued unlimited number of times
	const bool unlimited;							AST(PERSIST)

	// Will be true if any of the slots contains random cards
	const bool containsRandomCards;					AST(PERSIST)

	// Number of this pack to issue.
	const U32 packCount;							AST(PERSIST)

	// The fixed cards in this pack
	CONST_INT_EARRAY fixedCards;					AST(PERSIST)

	// There is one slot for each card in the pack.  Each slot can either
	//  be a fixed card or randomly selected from a named group.
	CONST_EARRAY_OF(CCGRandomSlotDef) randomCards;	AST(PERSIST)
} CCGPackDef;

typedef struct NOCONST(CCGPackDef) NOCONST(CCGPackDef);

void CCG_PacksInitLate(void);

bool CCG_CheckPacksAvailable(char *packName, U32 count);

bool CCG_trh_CheckPacksAvailable(ATH_ARG NOCONST(CCGPackDef) *packDef, U32 count);
void CCG_trh_DecrementPackCount(ATH_ARG NOCONST(CCGPackDef) *packDef, U32 count);

CCGPackDef *CCG_GetPackDef(const char *packDefName);
