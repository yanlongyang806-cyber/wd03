/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CCGDeck.h"

#include "objTransactions.h"
#include "AppServerLib.h"
#include "LocalTransactionManager.h"
#include "StringCache.h"
#include "CCGPlayer.h"
#include "CCGPlayers.h"
#include "CCGPlayerData.h"
#include "CCGDeck.h"
#include "CCGServer.h"
#include "CCGCardID.h"
#include "CCGDefs.h"
#include "CCGTransactionReturnVal.h"

#include "earray.h"
#include "StashTable.h"
#include "logging.h"

#include "AutoGen/CCGDeck_h_ast.h"
#include "AutoGen/CCGPlayerData_h_ast.h"
#include "AutoGen/CCGTransactionReturnVal_h_ast.h"

#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

CCGDeckDef g_DeckDef;

void
CCG_DeckInitEarly(void)
{
	StructInit(parse_CCGDeckDef, &g_DeckDef);
	ParserLoadFiles("defs", "CCGDeck.def", "CCGDeck.bin", 0, parse_CCGDeckDef, &g_DeckDef);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
char * 
CCG_GetDeckDefs()
{
	char *retStr;
	estrCreate(&retStr);
	ParserWriteText(&retStr, parse_CCGDeckDef, &g_DeckDef, 0, 0, 0);
	return retStr;
}

//
// Create a new deck with the card sets defined in the global deck definition
//
NOCONST(CCGDeck) *
CCG_CreateEmptyDeck(const char *deckName, U32 deckNum)
{
	NOCONST(CCGDeck) *deck;

	deck = StructCreate(parse_CCGDeck);

	deck->deckNum = deckNum;
	deck->deckName = strdup(deckName);
	
	// create the card sets that the definition requires
	FOR_EACH_IN_EARRAY(g_DeckDef.requiredSets, CCGCardSetDef, cardSetDef)
	{
		NOCONST(CCGCardSet) * cardSet = StructCreate(parse_CCGCardSet);

		cardSet->setName = cardSetDef->setName;

		eaPush(&deck->cardSets, cardSet);
	}
	FOR_EACH_END

	return deck;
}

NOCONST(CCGDeck) *
CCG_GetDeckCopyForEditing(CCGPlayer *player, U32 deckNum)
{
	NOCONST(CCGDeck) *deck;

	devassertmsg((unsigned)eaSize(&player->data->decks) > deckNum, "attempt to edit invalid deck number");
	deck = StructClone(parse_CCGDeck, player->data->decks[deckNum]);

	return deck;
}

// Returns a string containing errors, or NULL if the deck validated
char *
CCG_ValidateDeck(CCGDeck *deck, U32 numCustomCards)
{
	char *errorString = NULL;

	FOR_EACH_IN_EARRAY(g_DeckDef.requiredSets, CCGCardSetDef, cardSetDef)
	{
		CCGCardSet *cardSet;
		U32 numCards;
		unsigned i;

		cardSet = eaIndexedGetUsingString(&deck->cardSets, cardSetDef->setName);
		if ( cardSet == NULL )
		{
			// required card set is missing
			estrConcatf(&errorString, ":error=required card set %s is missing", cardSetDef->setName);
			continue;
		}
		numCards = (unsigned)ea32Size(&cardSet->cardIDs);

		if ( ( numCards > cardSetDef->maxCards ) || ( numCards < cardSetDef->minCards ) )
		{
			// wrong number of cards
			estrConcatf(&errorString, ":error=card set %s length %u cards invalid", cardSetDef->setName, numCards);
		}

		// make sure all the cards are of the right type
		for( i = 0; i < numCards; i++ )
		{
			U32 cardID = cardSet->cardIDs[i];
			U32 cardNum;

			CCGCardDef *cardDef;
			const char *cardType;

			if ( CCG_IsCardCustom(cardID) )
			{
				// Should we validate custom cards?  Will need custom card count or
				//  player data.
				cardNum = CCG_GetCustomCardNum(cardID);
				if ( cardNum >= numCustomCards )
				{
					// custom card number is not valid
					estrConcatf(&errorString, ":error=custom card number %u(%u) invalid", cardID, cardNum);
				}
			}
			else
			{
				cardNum = CCG_GetCardNum(cardID);

				cardDef = eaIndexedGetUsingInt(&g_CardDefs.cardDefs, cardNum);

				if ( cardDef == NULL )
				{
					// undefined card
					estrConcatf(&errorString, ":error=undefined card %u(%u)", cardID, cardNum);
					continue;
				}

				cardType = cardDef->type;
				if ( !eaContains(&cardSetDef->allowedTypes, cardType) )
				{
					// card is not an allowed type
					estrConcatf(&errorString, ":error=card %u(%u) type %s not allowed", cardID, cardNum, cardType);
				}
			}
		}
	}
	FOR_EACH_END;

	return errorString;
}

AUTO_TRANS_HELPER;
bool
CCG_trh_ReturnDeckCardsToInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, int deckNum, bool onlyEditable)
{
	NOCONST(CCGDeck) *deck;
	bool editable;

	deck = playerData->decks[deckNum];

	FOR_EACH_IN_EARRAY(deck->cardSets, NOCONST(CCGCardSet), cardSet)
	{
		CCGCardSetDef *cardSetDef = eaIndexedGetUsingString(&g_DeckDef.requiredSets, cardSet->setName);
		editable = ( ( cardSetDef == NULL ) || cardSetDef->editable );
		
		// only return the cards in this cardset if it is editable or the onlyEditable flag is false
		if ( editable || ( onlyEditable == false ) )
		{
			int numCards = ea32Size(&cardSet->cardIDs);
			int i;

			for ( i = 0; i < numCards; i++ )
			{
				U32 cardID;
				NOCONST(CCGCardBucket) *bucket;

				// remove from the deck
				cardID = ea32Pop(&cardSet->cardIDs);

				bucket = eaIndexedGetUsingInt(&playerData->cardInventory, cardID);
				if ( bucket == NULL )
				{
					// card not found in inventory
					return false;
				}

				// add the card back to the card inventory
				bucket->freeCount++;
				if ( bucket->freeCount > bucket->totalCount )
				{
					// something is screwed up with the counts!
					return false;
				}
			}
		}
	}
	FOR_EACH_END;

	return true;
}



AUTO_TRANS_HELPER;
bool
CCG_trh_PopulateDeckFromPrototype(ATH_ARG NOCONST(CCGPlayerData) *playerData, CCGDeck *prototypeDeck, int deckNum, bool onlyEditable)
{
	NOCONST(CCGDeck) *deck;
	bool editable;

	deck = playerData->decks[deckNum];

	FOR_EACH_IN_EARRAY(deck->cardSets, NOCONST(CCGCardSet), cardSet)
	{
		CCGCardSetDef *cardSetDef = eaIndexedGetUsingString(&g_DeckDef.requiredSets, cardSet->setName);
		editable = ( ( cardSetDef == NULL ) || cardSetDef->editable );

		// only copy the cards in this cardset if it is editable or the onlyEditable flag is false
		if ( editable || ( onlyEditable == false ) )
		{
			CCGCardSet *sourceCardSet = eaIndexedGetUsingString(&prototypeDeck->cardSets, cardSet->setName);

			if ( sourceCardSet != NULL )
			{
				int numCards = ea32Size(&sourceCardSet->cardIDs);
				int i;

				for ( i = 0; i < numCards; i++ )
				{
					U32 cardID;
					NOCONST(CCGCardBucket) *bucket;

					cardID = sourceCardSet->cardIDs[i];

					// find the bucket in player's inventory for this card
					bucket = eaIndexedGetUsingInt(&playerData->cardInventory, cardID);
					if ( bucket == NULL )
					{
						// card not found in inventory
						return false;
					}

					if ( bucket->freeCount == 0 )
					{
						// card not available in inventory
						return false;
					}

					// update inventory
					bucket->freeCount--;
					
					// add card to deck
					ea32Push(&cardSet->cardIDs, cardID);
				}
			}
		}
	}
	FOR_EACH_END;

	return true;
}

AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_UpdateDeck(ATR_ARGS, NOCONST(CCGPlayerData) *playerData, NON_CONTAINER CCGDeck *prototypeDeck)
{
	char *validationReturn = NULL;
	char *requestDetailString = NULL;
	U32 deckNum = prototypeDeck->deckNum;

	estrStackCreate(&requestDetailString);
	estrPrintf(&requestDetailString, "action=UpdateDeck:player=%s[%d]:deckNum=%d", playerData->accountName, playerData->accountID, deckNum);

	// make sure the deck number is valid
	if ( (unsigned)eaSize(&playerData->decks) <= deckNum )
	{
		// deck doesn't exist
		estrAppend2(ATR_RESULT_FAIL, "Invalid Deck Number");
		devassertmsgf( (unsigned)eaSize(&playerData->decks) > deckNum, "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL );
		estrDestroy(&requestDetailString);
		return TRANSACTION_OUTCOME_FAILURE;
	}
	// make sure the new deck has the right number and types of cards based on the deck definition
	validationReturn = CCG_ValidateDeck(prototypeDeck, eaSize(&playerData->customCards));
	if ( validationReturn != NULL )
	{
		// deck doesn't exist
		estrAppend2(ATR_RESULT_FAIL, "Updated deck failed validation");
		devassertmsgf( (unsigned)eaSize(&playerData->decks) > deckNum, "%s:message=%s%s", requestDetailString, *ATR_RESULT_FAIL, validationReturn );
		estrDestroy(&requestDetailString);
		estrDestroy(&validationReturn);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// return cards from previous deck to player's inventory
	if ( !CCG_trh_ReturnDeckCardsToInventory(playerData, deckNum, true) )
	{
		// error returning cards to inventory
		estrAppend2(ATR_RESULT_FAIL, "Unable to return cards to inventory");
		devassertmsgf( (unsigned)eaSize(&playerData->decks) > deckNum, "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL );
		estrDestroy(&requestDetailString);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// add cards from updated deck to deck
	if ( !CCG_trh_PopulateDeckFromPrototype(playerData, prototypeDeck, deckNum, true) )
	{
		// error returning cards to inventory
		estrAppend2(ATR_RESULT_FAIL, "Unable to add cards to deck");
		devassertmsgf( (unsigned)eaSize(&playerData->decks) > deckNum, "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL );
		estrDestroy(&requestDetailString);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	estrPrintf(ATR_RESULT_SUCCESS, "%s:message=%s", requestDetailString, "Deck updated successfully");
	log_printf(LOG_CCG, *ATR_RESULT_SUCCESS);
	estrDestroy(&requestDetailString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// This function is used to commit deck edits to a deck that was fetched
//  with CCG_GetDeckCopyForEditing().
CCGTransactionReturnVal *
CCG_UpdateDeck(CCGCallback *cb, CCGPlayer *player, NOCONST(CCGDeck) *editedDeck)
{
	char *requestDetailString = NULL;
	U32 deckNum = editedDeck->deckNum;
	char *message = NULL;
	TransactionReturnVal *pReturn;
	CCGPlayerData *playerData = player->data;
	char *validationReturn = NULL;

	estrStackCreate(&requestDetailString);
	estrPrintf(&requestDetailString, "action=UpdateDeck:player=%s[%d]:deckNum=%d", playerData->accountName, playerData->accountID, deckNum);

	// make sure the deck number is valid
	if ( (unsigned)eaSize(&playerData->decks) <= deckNum )
	{
		// deck doesn't exist
		message =  "Invalid Deck Number";
		devassertmsgf( (unsigned)eaSize(&playerData->decks) > deckNum, "%s:message=%s", requestDetailString, message );
		estrDestroy(&requestDetailString);
		return CCG_CreateTRV(false, message);
	}
	// make sure the new deck has the right number and types of cards based on the deck definition
	validationReturn = CCG_ValidateDeck((CCGDeck *)editedDeck, eaSize(&playerData->customCards));
	if ( validationReturn != NULL )
	{
		// deck validation failed
		message = "Updated deck failed validation";
		devassertmsgf( (unsigned)eaSize(&playerData->decks) > deckNum, "%s:message=%s%s", requestDetailString, message, validationReturn );
		estrDestroy(&requestDetailString);
		estrDestroy(&validationReturn);
		return CCG_CreateTRV(false, message);
	}

	pReturn = objCreateManagedReturnVal(CCG_GenericTransactionCallback, cb);

	AutoTrans_CCG_tr_UpdateDeck(pReturn, GLOBALTYPE_CCGSERVER, GLOBALTYPE_CCGPLAYER, player->data->containerID, (CCGDeck *)editedDeck);

	return NULL;
}

static U32
FindAvailableCardOfTypes(CCGPlayerData *playerData, StashTable usedCards, STRING_EARRAY types)
{
	FOR_EACH_IN_EARRAY(playerData->cardInventory, CCGCardBucket, bucket)
	{
		if ( bucket->freeCount > 0 )
		{
			// There might be cards of this type free.  Still need to check
			//  usedCards stash table
			U32 cardID = bucket->id;
			const char *cardType = CCG_GetCardType(cardID);

			if ( cardType != NULL )
			{
				if ( eaContains(&types, cardType) )
				{
					int count;
					// card is of the right type
					if ( stashIntFindInt(usedCards, cardID, &count) )
					{
						// at least one of this card already used
						if ( count < bucket->freeCount )
						{
							// we can use this card
							// remember new count
							count++;
							stashIntAddInt(usedCards, cardID, count, true);

							return cardID;
						}
						// if we get here there are not any more of this card, and we just loop to the next bucket
					}
					else
					{
						// haven't used this card yet
						stashIntAddInt(usedCards, cardID, 1, true);
						return cardID;
					}
				}
			}
		}
	}
	FOR_EACH_END;

	return 0;
}

// populate the player's initial deck with whatever cards they have that meet the deck requirements
CCGTransactionReturnVal *
CCG_PopulateInitialDeck(CCGCallback *cb, CCGPlayer *player)
{
	NOCONST(CCGDeck) *deck;
	StashTable usedCardMap = stashTableCreateInt(0);
	CCGTransactionReturnVal *trv;

	deck = CCG_GetDeckCopyForEditing(player, 0);

	// only fill in the sets that are required by the card set defs
	FOR_EACH_IN_EARRAY(g_DeckDef.requiredSets, CCGCardSetDef, cardSetDef)
	{
		if ( cardSetDef->editable )
		{
			// we only do editable card sets
			NOCONST(CCGCardSet) *cardSet;
			unsigned i;

			cardSet = eaIndexedGetUsingString(&deck->cardSets, cardSetDef->setName);

			// make sure the card set is empty
			devassertmsg(ea32Size(&cardSet->cardIDs) == 0, "card set not empty when populating initial deck");

			for ( i = 0; i < cardSetDef->maxCards; i++ )
			{
				U32 cardID;

				cardID = FindAvailableCardOfTypes(player->data, usedCardMap, cardSetDef->allowedTypes);

				if ( cardID == 0 )
				{
					devassertmsg(cardID != 0, "didn't find card when populating initial deck");
				}
				else
				{
					ea32Push(&cardSet->cardIDs, cardID);
				}
			}
		}
	}
	FOR_EACH_END;

	stashTableDestroy(usedCardMap);

	trv = CCG_UpdateDeck(cb, player, deck);
	StructDestroy(parse_CCGDeck, deck);

	return trv;
}

AUTO_COMMAND ACMD_NAME(CCG_PopulateInitialDeck) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGTransactionReturnVal *
CCG_PopulateInitialDeckCmd(CmdContext *pContext, int authToken)
{
	CCGCallback *cb;
	CCGPlayer *player;
	CCGTransactionReturnVal *trv = NULL;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		// player isn't logged on
		return CCG_CreateTRV(false, "action=PopulateInitialDeck:message=Player isn't logged in");
	}

	cb = CCG_CreateCallback(CCG_GenericCommandCallback, CCG_SetupSlowReturn(pContext));

	trv = CCG_PopulateInitialDeck(cb, player);
	if ( trv != NULL )
	{
		// the operation errored out before the transaction, so we just return now
		CCG_CancelSlowReturn(pContext);
	}

	return trv;
}

AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_BuyDeckTokens(ATR_ARGS, NOCONST(CCGPlayerData) *playerData, U32 numTokens)
{
	playerData->deckTokens += numTokens;

	return TRANSACTION_OUTCOME_SUCCESS;
}

void
CCG_BuyDeckTokens(CCGCallback *cb, CCGPlayer *player, U32 numTokens)
{
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(CCG_GenericTransactionCallback, cb);
	AutoTrans_CCG_tr_BuyDeckTokens(pReturn, GLOBALTYPE_CCGSERVER, GLOBALTYPE_CCGPLAYER, player->data->containerID, numTokens);
}

AUTO_COMMAND ACMD_NAME(CCG_BuyDeckTokens) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGTransactionReturnVal *
CCG_BuyDeckTokensCmd(CmdContext *pContext, int authToken, U32 numTokens)
{
	CCGCallback *cb;
	CCGPlayer *player;
	CCGTransactionReturnVal *trv = NULL;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		// player isn't logged on
		return CCG_CreateTRV(false, "action=OpenPack:message=Player isn't logged in");
	}

	cb = CCG_CreateCallback(CCG_GenericCommandCallback, CCG_SetupSlowReturn(pContext));

	CCG_BuyDeckTokens(cb, player, numTokens);

	return NULL;
}

#include "CCGDeck_h_ast.c"