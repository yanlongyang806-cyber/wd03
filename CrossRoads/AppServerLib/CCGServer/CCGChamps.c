/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGChamps.h"
#include "CCGPlayerData.h"
#include "CCGPackDef.h"
#include "CCGAttribute.h"
#include "CCGDeck.h"
#include "CCGCardID.h"
#include "CCGServer.h"
#include "CCGPlayer.h"
#include "CCGCommon.h"
#include "CCGTransactionReturnVal.h"
#include "CCGPlayers.h"

#include "objTransactions.h"
#include "AppServerLib.h"
#include "LocalTransactionManager.h"
#include "StringCache.h"

#include "logging.h"

#include "Powers.h"
#include "PowerTree.h"

#include "AutoGen/CCGChamps_h_ast.h"
#include "AutoGen/CCGPlayerData_h_ast.h"
#include "AutoGen/CCGAttribute_h_ast.h"
#include "AutoGen/CCGDeck_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/CCGCommon_h_ast.h"
#include "AutoGen/CCGTransactionReturnVal_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

CCGChampsPowerSetDefList g_PowerSetDefs;

void
CCGChamps_InitEarly(void)
{
	StructInit(parse_CCGChampsPowerSetDefList, &g_PowerSetDefs);
	ParserLoadFiles("defs", "CCGPowerSets.def", "CCGPowerSets.bin", 0, parse_CCGChampsPowerSetDefList, &g_PowerSetDefs);
}

//
// Do any champs specific whacking on the player data before it is
//  initially committed to the database.
//
void
CCGChamps_InitPlayerData(NOCONST(CCGPlayerData) *playerData)
{
	NOCONST(CCGPackCount) *packCount;
	NOCONST(CCGAttribute) *playerAttr;

	playerData->deckTokens = 1;

	// create starter pack
	packCount = StructCreate(parse_CCGPackCount);
	packCount->count = 1;
	packCount->packType = allocAddString("core.starter");

	eaPush(&playerData->packInventory, packCount);

	// new player also gets a booster pack
	packCount = StructCreate(parse_CCGPackCount);
	packCount->count = 1;
	packCount->packType = allocAddString("core.basicbooster");

	eaPush(&playerData->packInventory, packCount);

	// this is a flag that indicates that the player has not yet been
	//  awarded their power set specific pack
	playerAttr = StructCreate(parse_CCGAttribute);
	playerAttr->name = allocAddString("powerSetPack");
	playerAttr->value = strdup("1");

	eaPush(&playerData->attributes, playerAttr);

	// NOTE - new player also gets a hero card and a powerset specific
	//  pack.  Hero cards are created when a deck is created, since they
	//  are inherent to the deck.  They get their powerset specific
	//  pack when their first deck is created, since it is dependent
	//  on what they choose for their hero card.

	// Init some player stats
	playerAttr = StructCreate(parse_CCGAttribute);
	playerAttr->name = allocAddString("wins");
	playerAttr->value = strdup("0");

	eaPush(&playerData->attributes, playerAttr);

	playerAttr = StructCreate(parse_CCGAttribute);
	playerAttr->name = allocAddString("losses");
	playerAttr->value = strdup("0");

	eaPush(&playerData->attributes, playerAttr);

	playerAttr = StructCreate(parse_CCGAttribute);
	playerAttr->name = allocAddString("rank");
	playerAttr->value = strdup("1000");

	eaPush(&playerData->attributes, playerAttr);

	return;
}

AUTO_TRANSACTION;
enumTransactionOutcome
CCGChamps_tr_CreateDeck(ATR_ARGS, NOCONST(CCGPlayerData) *playerData, const char *deckName, const char *heroName, U32 baseHeroCardNum, const char *powerPackName)
{
	CCGPackDef *packDef;
	bool addPowerPack = false;
	NOCONST(CCGCustomCard) *heroCard;
	NOCONST(CCGDeck) *deck;
	NOCONST(CCGCardSet) *heroCardSet;

	// XXX - check that base card is a hero card
	char *requestDetailString = NULL;

	estrStackCreate(&requestDetailString);
	estrPrintf(&requestDetailString, "action=CreateDeck:player=%s[%d]:deckName=%s:heroName=%s:baseHeroCardNum=%u:powerPackName=%s", 
		playerData->accountName, playerData->accountID, deckName, heroName, baseHeroCardNum, powerPackName);

	// make sure the player has a deck token
	if ( playerData->deckTokens < 1 )
	{
		// player doesn't have a deck token
		estrAppend2(ATR_RESULT_FAIL, "No deck token");
		log_printf(LOG_CCG, "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
		estrDestroy(&requestDetailString);

		return TRANSACTION_OUTCOME_FAILURE;
	}

	// check for the flag that tells us to add the powerset specific card pack,
	//  and remove the flag attribute if it is there.
	if ( CCG_trh_AttributeExists(&playerData->attributes, "powerSetPack") )
	{
		addPowerPack = true;
		CCG_trh_RemoveAttribute(&playerData->attributes, "powerSetPack");
	}

	if ( addPowerPack )
	{
		// make sure the power pack exists
		packDef = CCG_GetPackDef(powerPackName);
		if ( packDef == NULL )
		{
			// pack def doesn't exist
			estrAppend2(ATR_RESULT_FAIL, "PackDef doesn't exist");
			AssertOrAlert("CCG_BAD_PACKNAME", "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
			estrDestroy(&requestDetailString);

			return TRANSACTION_OUTCOME_FAILURE;
		}

		// add the powerset pack to the player's pack inventory
		CCG_trh_AddPacksToInventory(playerData, powerPackName, 1);
	}

	// create the hero card
	heroCard = StructCreate(parse_CCGCustomCard);
	heroCard->id = eaSize(&playerData->customCards);
	CCG_trh_SetAttributeString(&heroCard->attributes, "heroName", heroName);
	CCG_trh_SetAttributeU32(&heroCard->attributes, "baseCard", baseHeroCardNum);
	eaPush(&playerData->customCards, heroCard);

	// create the deck and add it to the player record
	deck = CCG_CreateEmptyDeck(deckName, eaSize(&playerData->decks));
	eaPush(&playerData->decks, deck);

	// add the hero card to the deck
	heroCardSet = eaIndexedGetUsingString(&deck->cardSets, "Hero");
	assertmsg(heroCardSet != NULL, "heroCardSet is NULL when creating deck");
	ea32Push(&heroCardSet->cardIDs, heroCard->id | CCG_CARD_FLAG_CUSTOM);

	// decrement deck tokens
	playerData->deckTokens--;

	estrPrintf(ATR_RESULT_SUCCESS, "%s:message=%s", requestDetailString, "Deck created successfully");
	log_printf(LOG_CCG, *ATR_RESULT_SUCCESS);
	estrDestroy(&requestDetailString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

CCGTransactionReturnVal *
CCGChamps_CreateDeck(CCGCallback *cb, CCGPlayer *player, const char *charName)
{
	CCGChampsPowerSetDef *powerSetDef;
	CCGCharacterInfo *charInfo;

	char *requestDetailString = NULL;
	char *message;
	TransactionReturnVal *pReturn;
	const char *powerPackName = NULL;

	char *deckName = NULL;

	estrStackCreate(&requestDetailString);
	estrPrintf(&requestDetailString, "action=CreateDeck:player=%s[%d]:charName=%s", player->data->accountName, player->data->accountID, charName);

	// make sure the character info has been fetched
	if ( player->characterInfos == NULL )
	{
		// character infos haven't been fetched
		message = "message=character info hasn't been fetched yet";
		assertmsgf(player->characterInfos != NULL, "%s:%s", requestDetailString, message);
		estrDestroy(&requestDetailString);

		return CCG_CreateTRV(false, message);
	}

	// find the particular character
	charInfo = eaIndexedGetUsingString(&player->characterInfos->charInfo, charName);
	if ( charInfo == NULL )
	{
		// character doesn't exist
		message = "message=character doesn't exist";
		assertmsgf(player->characterInfos != NULL, "%s:%s", requestDetailString, message);
		estrDestroy(&requestDetailString);

		return CCG_CreateTRV(false, message);
	}

	// find the power set definition
	powerSetDef = eaIndexedGetUsingString(&g_PowerSetDefs.powerSetDefs, charInfo->powerTree);
	if ( powerSetDef == NULL )
	{
		// power set doesn't exist
		message = "message=power set doesn't exist";
		assertmsgf(player->characterInfos != NULL, "%s:%s", requestDetailString, message);
		estrDestroy(&requestDetailString);

		return CCG_CreateTRV(false, message);
	}

	// check deck tokens
	if ( player->data->deckTokens < 1 )
	{
		// no deck token
		message = "message=no deck token";
		assertmsgf(player->characterInfos != NULL, "%s:%s", requestDetailString, message);
		estrDestroy(&requestDetailString);

		return CCG_CreateTRV(false, message);
	}

	// check for the flag that tells us to add the powerset specific card pack
	if ( CCG_AttributeExists(&player->data->attributes, (const char *)"powerSetPack") )
	{
		CCGPackDef *packDef;
		powerPackName = powerSetDef->powerPackName;

		// make sure the pack exists
		packDef = CCG_GetPackDef(powerPackName);
		if ( packDef == NULL )
		{
			// pack doesn't exist
			message = "message=power set pack doesn't exist";
			assertmsgf(player->characterInfos != NULL, "%s:%s", requestDetailString, message);
			estrDestroy(&requestDetailString);

			return CCG_CreateTRV(false, message);
		}
	}

	// create the initial deck name
	estrPrintf(&deckName, "%s's Deck", charName);

	pReturn = objCreateManagedReturnVal(CCG_GenericTransactionCallback, cb);
	AutoTrans_CCGChamps_tr_CreateDeck(pReturn, GLOBALTYPE_CCGSERVER, GLOBALTYPE_CCGPLAYER, player->data->containerID, deckName, charName, powerSetDef->heroBaseCard, powerPackName);

	estrDestroy(&deckName);

	return NULL;
}

AUTO_COMMAND ACMD_NAME(CCG_CreateDeck) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGTransactionReturnVal *
CCG_CreateDeckCmd(CmdContext *pContext, int authToken, const char *charName)
{
	CCGCallback *cb;
	CCGTransactionReturnVal *trv;
	CCGPlayer *player;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		// player isn't logged on
		return CCG_CreateTRV(false, "action=CreateDeck:message=Player isn't logged in");
	}

	cb = CCG_CreateCallback(CCG_GenericCommandCallback, CCG_SetupSlowReturn(pContext));

	trv = CCGChamps_CreateDeck(cb, player, charName);

	if ( trv != NULL )
	{
		// the operation errored out before the transaction, so we just return now
		CCG_CancelSlowReturn(pContext);
	}

	return trv;
}

//
// Extract the primary power tree name from the player's powers.
// The string argument is a textparser string containing an array of
//  PowerTree structs.
// The "primary" power is selected by finding the power with "auto" in
//  the name that has the earliest create date.  This should be the
//  character's energy builder power.  We return the name of the power
//  tree that this power belongs to.  This will be used to select
//  the correct base card for the player's hero card.
//
static char *
GetPowerTree(char *powersString)
{
	char *powerTreeName = NULL;

	char *firstPowerTreeName = NULL;
	U32 firstCreated = U32_MAX;

	char *powerName = NULL;
	PowerTree **powerTrees = NULL;
	eaCreate(&powerTrees);

	eaStructArrayFromString(&powerTrees, parse_PowerTree, powersString);

	FOR_EACH_IN_EARRAY(powerTrees, PowerTree, powerTree)
	{
		powerTreeName = REF_DATA_FROM_HANDLE(powerTree->hDef);
		FOR_EACH_IN_EARRAY(powerTree->ppNodes, PTNode, node)
		{
			powerName = REF_DATA_FROM_HANDLE(node->hDef);

			// check if power name contains "auto"
			if ( ( powerName != NULL ) && ( strstri(powerName, "auto") != NULL ) )
			{
				FOR_EACH_IN_EARRAY(node->ppPurchaseTracker, PowerPurchaseTracker, tracker)
				{
					if ( tracker->uiOrderCreated < firstCreated )
					{
						firstCreated = tracker->uiOrderCreated;
						firstPowerTreeName = powerTreeName;
					}
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	if ( firstPowerTreeName == NULL)
	{
		return NULL;
	}
	else
	{
		return estrCreateFromStr(firstPowerTreeName);
	}
}

static void 
GetCharacters_CB(TransactionReturnVal *pReturnVal, CCGCallback *cb)
{
	CCGPlayer *player = cb->internalData;

	CCGCharacterInfos *infos = NULL;

	CCGTransactionReturnVal *trv;

	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		CCGCharacterInfos *returnInfos = StructCreate(parse_CCGCharacterInfos);

		RemoteCommandCheck_DBReturnCCGCharacterInfo(pReturnVal, &infos);

		FOR_EACH_IN_EARRAY(infos->charInfo, CCGCharacterInfo, pCharacter)
		{
			CCGCharacterInfo *info = StructCreate(parse_CCGCharacterInfo);

			info->id = pCharacter->id;
			strcpy(info->name, pCharacter->name);

			// This is kind of tricky.  The character info coming from the DB server
			//  will contain a textparser string containing all the powers of the
			//  character.  When we create the CCGCharacterInfo that we will
			//  keep locally on the CCG server, we extract just the primary
			//  power set name for the character, and that is what the powerTree
			//  field contains.
			info->powerTree = GetPowerTree(pCharacter->powerTree);

			if ( info->powerTree != NULL )
			{
				eaPush(&returnInfos->charInfo, info);
			}
			else
			{
				StructDestroy(parse_CCGCharacterInfo, info);
			}
		}
		FOR_EACH_END

		player->characterInfos = returnInfos;
	}
	else
	{
		if ( player->characterInfos != NULL )
		{
			StructDestroy(parse_CCGCharacterInfos, player->characterInfos);
		}
		player->characterInfos = NULL;
	}

	trv = CCG_TRVFromTransactionRet(pReturnVal, NULL);

	CCG_CallCallback(cb, trv);

	StructDestroy(parse_CCGTransactionReturnVal, trv);
}

void
CCGChamps_GetCharacters(CCGCallback *cb, CCGPlayer *player)
{
	cb->internalData = player;
	RemoteCommand_DBReturnCCGCharacterInfo(objCreateManagedReturnVal(GetCharacters_CB, cb),
		GLOBALTYPE_OBJECTDB, 0, player->data->accountID);
}

typedef struct GetCharactersCBData
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;
	CCGPlayer *player;
} GetCharactersCBData;

static void
GetCharactersCmd_CB(CCGTransactionReturnVal *trv, GetCharactersCBData *cbData)
{
	char *pFullRetString = NULL;

	CCG_BuildXMLResponseString(&pFullRetString, parse_CCGCharacterInfos, cbData->player->characterInfos);

	DoSlowCmdReturn(trv->success, pFullRetString, cbData->pSlowReturnInfo);
}

AUTO_COMMAND ACMD_NAME(CCG_GetCharacters) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGCharacterInfos *
CCG_GetCharactersCmd(CmdContext *pContext, int authToken)
{
	CCGCallback *cb;
	CCGPlayer *player;
	GetCharactersCBData *cbData;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		// player isn't logged on
		return NULL;
	}

	cbData = (GetCharactersCBData *)malloc(sizeof(GetCharactersCBData));
	cbData->player = player;
	cbData->pSlowReturnInfo = CCG_SetupSlowReturn(pContext);
	
	cb = CCG_CreateCallback(GetCharactersCmd_CB, cbData);

	CCGChamps_GetCharacters(cb, player);

	return NULL;
}

#include "CCGChamps_h_ast.c"