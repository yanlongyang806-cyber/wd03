/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPackInventory.h"
#include "CCGCardID.h"
#include "CCGServer.h"
#include "CCGPackDef.h"
#include "CCGPrintRun.h"
#include "CCGPlayerData.h"
#include "CCGPlayer.h"
#include "CCGPlayers.h"
#include "CCGTransactionReturnVal.h"

#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "earray.h"
#include "StringCache.h"
#include "cmdparse.h"
#include "stdtypes.h"
#include "rand.h"
#include "logging.h"

#include "AutoGen/CCGPackInventory_h_ast.h"
#include "AutoGen/CCGPackDef_h_ast.h"
#include "AutoGen/CCGPrintRun_h_ast.h"
#include "AutoGen/CCGPlayerData_h_ast.h"
#include "AutoGen/CCGTransactionReturnVal_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

//
// count through the card definitions in the card group until
//  we find the one containing the card we are looking for.
//
AUTO_TRANS_HELPER;
NOCONST(CCGPrintRunCardDef) *
CCG_trh_CardOffsetToCard(ATH_ARG NOCONST(CCGPrintRunCardGroup) *cardGroup, U32 cardOffset)
{
	U32 currentOffset = 0;

	FOR_EACH_IN_EARRAY(cardGroup->cardDefs, NOCONST(CCGPrintRunCardDef), cardDef)
	{
		if ( cardOffset < ( cardDef->currentCount + currentOffset ) )
		{
			return cardDef;
		}
		currentOffset += cardDef->currentCount;
	}
	FOR_EACH_END

	return NULL;
}

AUTO_TRANS_HELPER;
void
CCG_trh_AddCardToPlayerInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, U32 cardNum)
{
	NOCONST(CCGCardBucket) *cardBucket = eaIndexedGetUsingInt(&playerData->cardInventory, cardNum);
	if ( cardBucket == NULL )
	{
		// bucket wasn't found, so we need to make one
		cardBucket = StructCreate(parse_CCGCardBucket);
		cardBucket->freeCount = 1;
		cardBucket->totalCount = 1;
		cardBucket->id = cardNum;
		eaPush(&playerData->cardInventory, cardBucket);
	}
	else
	{
		// increment card count in player's card inventory
		cardBucket->freeCount++;
		cardBucket->totalCount++;
	}
}

AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_OpenPack(ATR_ARGS, NOCONST(CCGPlayerData) *playerData, CONST_EARRAY_OF(NOCONST(CCGPrintRun)) printRuns, const char *packName)
{
	CCGPackDef *packDef = CCG_GetPackDef(packName);
	NOCONST(CCGPrintRunCardGroup) *cardGroup = NULL;
	U32 *cardOffsets = NULL;
	NOCONST(CCGPrintRunCardDef) **cardsTmp = NULL;
	U32 i;
	int n;

	char *requestDetailString = NULL;
	char *cardListString = NULL;

	estrStackCreate(&requestDetailString);
	estrPrintf(&requestDetailString, "action=OpenPack:player=%s[%d]:packName=%s", playerData->accountName, playerData->accountID, packName);
	estrStackCreate(&cardListString);
	
	printf("opening pack %s\n", packName);

	if ( packDef == NULL )
	{
		// pack def doesn't exist
		estrAppend2(ATR_RESULT_FAIL, "PackDef doesn't exist");
		AssertOrAlert("CCG_BAD_PACKNAME", "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
		estrDestroy(&requestDetailString);
		estrDestroy(&cardListString);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( !CCG_trh_CheckPacksInInventory(playerData, packName, 1) )
	{
		// pack isn't in player's inventory
		estrAppend2(ATR_RESULT_FAIL, "Pack not in player's inventory");
		AssertOrAlert("CCG_PACK_NOT_FOUND", "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
		estrDestroy(&requestDetailString);
		estrDestroy(&cardListString);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// remove the pack from the player's inventory
	CCG_trh_RemovePacksFromInventory(playerData, packName, 1);

	// add the fixed cards
	for( n = 0; n < ea32Size(&packDef->fixedCards); n++ )
	{
		// add the card to player's inventory
		CCG_trh_AddCardToPlayerInventory(playerData, packDef->fixedCards[n]);
		estrConcatf(&cardListString, "%u ", packDef->fixedCards[n]);
	}

	// generate random cards
	if ( packDef->containsRandomCards )
	{
		if ( printRuns == NULL )
		{
			// pack def contains random cards, but no print runs are available
			estrAppend2(ATR_RESULT_FAIL, "PackDef contains random cards, but no print runs are available");
			AssertOrAlert("CCG_PRINTRUNS_NOT_FOUND", "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
			estrDestroy(&requestDetailString);
			estrDestroy(&cardListString);

			return TRANSACTION_OUTCOME_FAILURE;
		}

		ea32Create(&cardOffsets);
		eaCreate(&cardsTmp);

		FOR_EACH_IN_EARRAY(packDef->randomCards, CCGRandomSlotDef, slot)
		{
			U32 cardOffset;
			NOCONST(CCGPrintRunCardDef) *cardDef;

			ea32Clear(&cardOffsets);
			eaClear(&cardsTmp);

			cardGroup = NULL;

			// find the card group we need for this slot
			FOR_EACH_IN_EARRAY(printRuns, NOCONST(CCGPrintRun), printRun)
			{
				if ( printRun->name == slot->printRunName ) // pooled strings
				{
					cardGroup = eaIndexedGetUsingString(&printRun->cardGroups, slot->groupName);
				}
			}
			FOR_EACH_END
			
			if ( ( cardGroup == NULL ) || ( slot->count > cardGroup->currentCount ) )
			{
				// not enough cards left in the current group!
				estrAppend2(ATR_RESULT_FAIL, "Not enough cards left in current group");
				AssertOrAlert("CCG_NOT_ENOUGH_CARDS", "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
				estrDestroy(&requestDetailString);
				estrDestroy(&cardListString);

				ea32Destroy(&cardOffsets);
				eaDestroy(&cardsTmp);
				return TRANSACTION_OUTCOME_FAILURE;
			}

			for ( i = 0; i < slot->count; i++ )
			{
				// generate a random offset into the card group
				cardOffset = randomIntRange(0, cardGroup->currentCount - 1);

				while ( ea32Contains(&cardOffsets, cardOffset) )
				{
					// we already hit this same offset so increment and try again
					cardOffset++;

					// wrap if necessary
					if ( cardOffset >= cardGroup->currentCount )
					{
						cardOffset = 0;
					}
				}

				ea32Push(&cardOffsets, cardOffset);
				
				// Find the card at the given offset and remember it.  We will
				//  add it to the player's inventory and update the counts when we are
				//  done.  In order for the offsets to work correctly, we need to look
				//  up all the cards in this group before updating any counts.
				cardDef = CCG_trh_CardOffsetToCard(cardGroup, cardOffset);

				if (cardDef == NULL)
				{
					// No card at the given offset.  This means that our internal accounting
					//  of card counts is probably messed up.
					estrAppend2(ATR_RESULT_FAIL, "No card at offset");
					AssertOrAlert("CCG_NO_CARD_AT_OFFSET", "%s:message=%s", requestDetailString, *ATR_RESULT_FAIL);
					estrDestroy(&requestDetailString);
					estrDestroy(&cardListString);

					ea32Destroy(&cardOffsets);
					eaDestroy(&cardsTmp);

					return TRANSACTION_OUTCOME_FAILURE;
				}

				eaPush(&cardsTmp, cardDef);
			}
			

			FOR_EACH_IN_EARRAY(cardsTmp, NOCONST(CCGPrintRunCardDef), cardDefTmp)
			{
				// subtract the card from the print run
				cardDefTmp->currentCount--;
				cardGroup->currentCount--;

				// add the card to player's inventory
				CCG_trh_AddCardToPlayerInventory(playerData, cardDefTmp->cardNum);
				estrConcatf(&cardListString, "%u ", cardDefTmp->cardNum);
			}
			FOR_EACH_END
		}
		FOR_EACH_END

		ea32Destroy(&cardOffsets);
		eaDestroy(&cardsTmp);
	}
	estrPrintf(ATR_RESULT_SUCCESS, "%s:cards=%s:message=%s", requestDetailString, cardListString, "Pack opened successfully");
	log_printf(LOG_CCG, *ATR_RESULT_SUCCESS);
	estrDestroy(&requestDetailString);
	estrDestroy(&cardListString);

	return TRANSACTION_OUTCOME_SUCCESS;
}

CCGTransactionReturnVal *
CCG_OpenPack(CCGCallback *cb, CCGPlayer *player, const char *packName)
{
	TransactionReturnVal *pReturn;
	U32 *printRunIDs = NULL;
	CCGPackDef *packDef;
	CCGPackCount *packCount;
	char *requestDetailString = NULL;
	char *message = NULL;

	estrStackCreate(&requestDetailString);
	estrPrintf(&requestDetailString, "action=OpenPack:player=%s[%d]:packName=%s", player->data->accountName, player->data->accountID, packName);

	packDef = CCG_GetPackDef(packName);
	if ( packDef == NULL )
	{
		// pack requested doesn't exist
		message = "message=PackDef doesn't exist";
		AssertOrAlert("CCG_BAD_PACKNAME", "%s:%s", requestDetailString, message);
		estrDestroy(&requestDetailString);

		return CCG_CreateTRV(false, message);
	}
	
	packCount = eaIndexedGetUsingString(&player->data->packInventory, packName);
	if ( ( packCount == NULL ) || ( packCount->count < 1 ) )
	{
		message = "message=Pack not in player's inventory";
		AssertOrAlert("CCG_PACK_NOT_FOUND", "%s:%s", requestDetailString, message);
		estrDestroy(&requestDetailString);

		return CCG_CreateTRV(false, message);
	}
	
	// collect the container IDs of all print runs that are referenced by
	//  this pack definition.
	ea32Create(&printRunIDs);

	FOR_EACH_IN_EARRAY(packDef->randomCards, CCGRandomSlotDef, slot)
	{
		CCGPrintRun *printRun = CCG_GetPrintRun(slot->printRunName);
		if ( printRun == NULL )
		{
			// pack def refers to a print run that doesn't exist

			message = "message=PackDef refers to PrintRun that doesn't exist";
			AssertOrAlert("CCG_PRINTRUN_NOT_FOUND", "%s:%s", requestDetailString, message);
			estrDestroy(&requestDetailString);

			return CCG_CreateTRV(false, message);
		}
		// if this print run is not already on the list, then add it
		if ( !ea32Contains(&printRunIDs, printRun->containerID) )
		{
			ea32Push(&printRunIDs, printRun->containerID);
		}
	}
	FOR_EACH_END

	pReturn = objCreateManagedReturnVal(CCG_GenericTransactionCallback, cb);
	AutoTrans_CCG_tr_OpenPack(pReturn, GLOBALTYPE_CCGSERVER, GLOBALTYPE_CCGPLAYER, player->data->containerID, GLOBALTYPE_CCGPRINTRUN, &printRunIDs, packName);

	ea32Destroy(&printRunIDs);

	return NULL;
}

AUTO_COMMAND ACMD_NAME(CCG_OpenPack) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGTransactionReturnVal *
CCG_OpenPackCmd(CmdContext *pContext, int authToken, const char *packName)
{
	CCGCallback *cb;
	CCGTransactionReturnVal *trv;
	CCGPlayer *player;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		// player isn't logged on
		return CCG_CreateTRV(false, "action=OpenPack:message=Player isn't logged in");
	}

	cb = CCG_CreateCallback(CCG_GenericCommandCallback, CCG_SetupSlowReturn(pContext));

	trv = CCG_OpenPack(cb, player, packName);

	if ( trv != NULL )
	{
		// the operation errored out before the transaction, so we just return now
		CCG_CancelSlowReturn(pContext);
	}

	return trv;
}

typedef struct OpenAllPacksCBData
{
	CCGCallback *originalCB;
	CCGPlayer *player;
} OpenAllPacksCBData;

static void
OpenAllPacks_CB(CCGTransactionReturnVal *trv, OpenAllPacksCBData *cbData)
{
	CCGPlayer *player = cbData->player;
	const char *packName;
	CCGCallback *cb;

	if ( trv->success )
	{
		if ( eaSize(&player->data->packInventory) > 0 )
		{
			devassertmsg(player->data->packInventory[0]->count > 0, "pack count in inventory is 0");
			packName = player->data->packInventory[0]->packType;

			cb = CCG_CreateCallback(OpenAllPacks_CB, cbData);
			trv = CCG_OpenPack(cb, player, packName);
			if ( trv == NULL )
			{
				return;
			}
			else
			{
				// callback won't ever be called
				CCG_FreeCallback(cb);
			}
		}
		else
		{
			// pack inventory empty
			trv = CCG_CreateTRV(true, "opened all packs");
		}

		// if we get here, we are either done or failed before the transaction
		//  in CCG_OpenPack()
		CCG_CallCallback(cbData->originalCB, trv);
		StructDestroy(parse_CCGTransactionReturnVal, trv);
	}
	else
	{
		// last open pack failed, so call the callback to tell the original caller that we are done
		CCG_CallCallback(cbData->originalCB, trv);
		// no need to free trv, since our caller will free it
	}
}

CCGTransactionReturnVal *
CCG_OpenAllPacks(CCGCallback *originalCB, CCGPlayer *player)
{
	const char *packName;
	CCGTransactionReturnVal *trv = NULL;
	CCGCallback *cb;
	OpenAllPacksCBData *cbData;

	if ( eaSize(&player->data->packInventory) > 0 )
	{
		devassertmsg(player->data->packInventory[0]->count > 0, "pack count in inventory is 0");
		packName = player->data->packInventory[0]->packType;

		cbData = (OpenAllPacksCBData *)malloc(sizeof(OpenAllPacksCBData));
		cbData->originalCB = originalCB;
		cbData->player = player;

		cb = CCG_CreateCallback(OpenAllPacks_CB, cbData);

		trv = CCG_OpenPack(cb, player, packName);
	}
	else
	{
		trv = CCG_CreateTRV(true, "no packs to open");
	}

	return trv;
}

AUTO_COMMAND ACMD_NAME(CCG_OpenAllPacks) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGTransactionReturnVal *
CCG_OpenAllPacksCmd(CmdContext *pContext, int authToken)
{
	CCGCallback *cb;
	CCGTransactionReturnVal *trv;
	CCGPlayer *player;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		// player isn't logged on
		return CCG_CreateTRV(false, "action=OpenPack:message=Player isn't logged in");
	}

	cb = CCG_CreateCallback(CCG_GenericCommandCallback, CCG_SetupSlowReturn(pContext));

	trv = CCG_OpenAllPacks(cb, player);

	if ( trv != NULL )
	{
		// the operation errored out before the transaction, so we just return now
		CCG_CancelSlowReturn(pContext);
	}

	return trv;
}