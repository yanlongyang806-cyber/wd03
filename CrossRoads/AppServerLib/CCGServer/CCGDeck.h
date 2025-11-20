/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

//
// A card set is a named list of cards.  This is used for grouping
//  cards within a deck.
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGCardSet
{
	CONST_STRING_POOLED setName;	AST(PERSIST, KEY, POOL_STRING)
	CONST_INT_EARRAY cardIDs;		AST(PERSIST)
} CCGCardSet;

//
// A deck contains all the cards that are used to play a game.
// A deck consists of several card sets.  In the champions CCG
//  the card sets would be something like this:
//  * hero - 1 card
//  * mission - 4 cards
//  * villains - 5 cards
//  * main - 40 cards
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGDeck
{
	const U32 deckNum;						AST(PERSIST)
	CONST_STRING_MODIFIABLE deckName;		AST(PERSIST)
	CONST_EARRAY_OF(CCGCardSet) cardSets;	AST(PERSIST)
} CCGDeck;

//
// Structures used to hold game specific deck definition
//
AUTO_STRUCT;
typedef struct CCGCardSetDef
{
	STRING_POOLED setName;				AST(KEY POOL_STRING NAME("Name"))
	U32 minCards;						AST(NAME("MinCards"))
	U32 maxCards;						AST(NAME("MaxCards"))
	STRING_EARRAY allowedTypes;			AST(POOL_STRING NAME("AllowedType"))
	bool editable;						AST(BOOLFLAG NAME("Editable"))
} CCGCardSetDef;

AUTO_STRUCT;
typedef struct CCGDeckDef
{
	EARRAY_OF(CCGCardSetDef) requiredSets;
} CCGDeckDef;

void CCG_DeckInitEarly(void);

typedef struct NOCONST(CCGDeck) NOCONST(CCGDeck);
typedef struct CCGPlayer CCGPlayer;
typedef struct CCGCallback CCGCallback;
typedef struct CCGTransactionReturnVal CCGTransactionReturnVal;

NOCONST(CCGDeck) *CCG_CreateEmptyDeck(const char *deckName, U32 deckNum);

// Returns a NOCONST copy of the requested deck, which can be edited.
// The copy can then be passed back via CCG_UpdateDeck() so that its changes can be committed
//  to the database.
NOCONST(CCGDeck) *CCG_GetDeckCopyForEditing(CCGPlayer *player, U32 deckNum);

// Initiates a transaction to update the deck that has been edited.
// If it doesn't return NULL, then the early checks failed and the callback will not be called
CCGTransactionReturnVal *CCG_UpdateDeck(CCGCallback *cb, CCGPlayer *player, NOCONST(CCGDeck) *editedDeck);
