/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef U32 ContainerID;

//
// Define print runs
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGPrintRunCardDef
{
	// the number of the card
	const U32 cardNum;					AST(PERSIST KEY)

	// how many of this card total in the print run
	const U32 originalCount;			AST(PERSIST)

	// how many of this card remaining in this run
	const U32 currentCount;				AST(PERSIST)
} CCGPrintRunCardDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct CCGPrintRunCardGroup
{
	CONST_STRING_POOLED name;						AST(PERSIST KEY POOL_STRING)

	const U32 originalCount;						AST(PERSIST)

	const U32 currentCount;							AST(PERSIST)

	CONST_EARRAY_OF(CCGPrintRunCardDef) cardDefs;	AST(PERSIST)
} CCGPrintRunCardGroup;

AUTO_STRUCT AST_CONTAINER;
typedef struct CCGPrintRun
{
	const ContainerID containerID;							AST(PERSIST KEY)
	CONST_STRING_POOLED name;								AST(PERSIST POOL_STRING)
	CONST_EARRAY_OF(CCGPrintRunCardGroup) cardGroups;		AST(PERSIST)

	const bool repeating;									AST(PERSIST BOOLFLAG)
	const bool inProduction;								AST(PERSIST BOOLFLAG)
} CCGPrintRun;


void CCG_PrintRunInitLate(void);
void CCG_PrintRunInitEarly(void);