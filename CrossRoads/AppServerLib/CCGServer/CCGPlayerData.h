/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct CCGDeck CCGDeck;
typedef struct CCGAttribute CCGAttribute;
typedef struct CCGCardBucket CCGCardBucket;
typedef struct CCGCustomCard CCGCustomCard;
typedef struct CCGPackCount CCGPackCount;
typedef struct TransactionReturnVal TransactionReturnVal;

//
// The card bucket is used to keep track of the player's card inventory.
// Each bucket keeps track of the number of instances of cards with the same
//  id in the players inventory.
// The player's card inventory is an array of buckets.
//
// totalCount - total number of this card that the player owns
// freeCount - number of this card that are not currently assigned to a deck
//
// NOTE - buckets are only used for the player card inventory, not for decks.
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGCardBucket
{
	const U32 id;			AST(PERSIST, KEY)
	const U16 totalCount;	AST(PERSIST)
	const U16 freeCount;	AST(PERSIST)
} CCGCardBucket;

//
// A custom card is defined by a list of name/value pairs.  It
//  is up to the game code to assign semantics to these attributes.
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGCustomCard
{
	const U32 id;								AST(PERSIST, KEY)
	CONST_EARRAY_OF(CCGAttribute) attributes;	AST(PERSIST)
} CCGCustomCard;

//
// Used to keep track of the number of a particular type of card pack
//  the player owns.
// NOTE - the contents of the pack are not determined until the pack
//  is "opened", at which time the exact cards are determined and added
//  to the player's card inventory.
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGPackCount
{
	CONST_STRING_POOLED packType;	AST(PERSIST, KEY, POOL_STRING)
	const U32 count;				AST(PERSIST)
} CCGPackCount;

//
// This is all the data that we persist about a CCG player
//
AUTO_STRUCT AST_CONTAINER;
typedef struct CCGPlayerData
{
	// Do I need both ID and name?  Is there any way to get name from ID cheaply?
	const U32 containerID;							AST(PERSIST, KEY)
	const U32 accountID;							AST(PERSIST)
	const char accountName[MAX_NAME_LEN];			AST(PERSIST)

	// Array of player's decks
	CONST_EARRAY_OF(CCGDeck) decks;					AST(PERSIST)

	// Any attributes we are tracking on the player
	CONST_EARRAY_OF(CCGAttribute) attributes;			AST(PERSIST)

	// The player's non-custom card inventory
	CONST_EARRAY_OF(CCGCardBucket) cardInventory;	AST(PERSIST)

	// Array of player's custom cards
	CONST_EARRAY_OF(CCGCustomCard) customCards;		AST(PERSIST)

	// Any packs that the player has purchased but not yet opened
	CONST_EARRAY_OF(CCGPackCount) packInventory;	AST(PERSIST)

	// Deck tokens can be exchanged for a new deck and hero card
	const U32 deckTokens;							AST(PERSIST)
} CCGPlayerData;

typedef struct NOCONST(CCGPlayerData) NOCONST(CCGPlayerData);

void CCG_PlayerDataInitLate(void);

void CCG_CreatePlayerData(TransactionReturnVal *pReturn, U32 accountID, char *accountName);

CCGPlayerData *CCG_GetPlayerDataByName(char *accountName);

CCGPlayerData *CCG_GetPlayerDataByAccountID(U32 accountID);

void CCG_trh_AddPacksToInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, const char *packName, U32 count);

void CCG_trh_RemovePacksFromInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, const char *packName, U32 count);

bool CCG_trh_CheckPacksInInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, const char *packName, U32 count);