/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

//
// Defines a card
//
AUTO_STRUCT;
typedef struct CCGCardDef
{
	U32 cardNum;				AST(KEY NAME("CardNum"))
	STRING_POOLED type;			AST(POOL_STRING NAME("Type"))
	STRING_MODIFIABLE name;		AST(NAME("CardName"))
	STRING_POOLED powerSet;		AST(POOL_STRING NAME("PowerSet"))
	STRING_POOLED prereq;		AST(POOL_STRING NAME("Prerequisite"))
	STRING_POOLED trait;		AST(POOL_STRING NAME("Trait"))
	STRING_POOLED damageType;	AST(POOL_STRING NAME("DamageType"))
	STRING_MODIFIABLE action;	AST(NAME("Action"))
	STRING_MODIFIABLE victory;	AST(NAME("Victory"))
	U8 attack;					AST(NAME("Attack"))
	U8 defense;					AST(NAME("Defense"))
	U8 support;					AST(NAME("Support"))

	// more card semantics data goes here
} CCGCardDef;

//
// Define decks
//
AUTO_STRUCT;
typedef struct CCGCardDefList
{
	CCGCardDef **cardDefs;			AST(NAME("Card"))
} CCGCardDefList;



