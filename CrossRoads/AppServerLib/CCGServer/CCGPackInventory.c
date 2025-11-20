/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPackInventory.h"
#include "CCGCardID.h"
#include "CCGServer.h"
#include "CCGPackDef.h"
#include "CCGPlayerData.h"
#include "CCGPlayer.h"
#include "CCGPlayers.h"

#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "earray.h"
#include "StringCache.h"
#include "cmdparse.h"
#include "stdtypes.h"

#include "AutoGen/CCGPackInventory_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

//
// This is a utility function to create the arrays needed for CCG_tr_BuyPacks().
// It can be called repeatedly to add more packs to the arrays.  It handles
//  duplicate calls with the same pack name, and does the lookup of the
//  CCGPackDef container IDs.
//
void
CCG_AddPackRequest(U32 **packIDsHandle, CCGPackRequests *packRequests, char *packName, U32 count)
{
	CCGPackNameAndCount *packNameAndCount;
	packNameAndCount = eaIndexedGetUsingString(&packRequests->requests, packName);

	if ( packNameAndCount )
	{
		// pack name already exists, so just increment count
		packNameAndCount->count += count;
	}
	else
	{
		CCGPackDef *packDef = CCG_GetPackDef(packName);
		if ( packDef != NULL )
		{
			ea32Push(packIDsHandle, packDef->containerID);
			packNameAndCount = StructCreate(parse_CCGPackNameAndCount);
			packNameAndCount->count = count;
			packNameAndCount->name = allocAddString(packName);

			eaPush(&packRequests->requests, packNameAndCount);
		}
	}
}

//
// Create a new card definition in a print run
//
// NOTE - The packs earray is keyed, so the earray code should prevent there from
//  being any duplicates.  Whoever is building the earray will need to be sure
//  to check the return value from eaPush(), or otherwise ensure that there are
//  no duplicates.
//
AUTO_TRANSACTION;
enumTransactionOutcome
CCG_tr_BuyPacks(ATR_ARGS, NOCONST(CCGPlayerData) *playerData, CONST_EARRAY_OF(NOCONST(CCGPackDef)) packDefs, const CCGPackRequests *packRequests)
{
	int i;
	int requestCount = eaSize(&packRequests->requests);

	if ( requestCount != eaSize(&packDefs) )
	{
		TRANSACTION_RETURN_FAILURE("size of packDefs != size of packRequests");
	}

	// First walk through the list of requested packs and make sure that
	//  there are enough available.
	for( i = 0; i < requestCount; i++ )
	{
		NOCONST(CCGPackDef) *packDef = packDefs[i];
		CCGPackNameAndCount *packNameAndCount = packRequests->requests[i];

		if ( !CCG_trh_CheckPacksAvailable(packDef, packNameAndCount->count) )
		{
			TRANSACTION_RETURN_FAILURE("There were not %d copies of pack %s available", packNameAndCount->count, packNameAndCount->name);
		}
	}

	// Now walk through the list of requested packs again, this time
	//  decrementing the global pack count and adding them to the player's
	//  inventory
	for( i = 0; i < requestCount; i++ )
	{
		NOCONST(CCGPackDef) *packDef = packDefs[i];
		CCGPackNameAndCount *packNameAndCount = packRequests->requests[i];

		CCG_trh_DecrementPackCount(packDef, packNameAndCount->count);

		CCG_trh_AddPacksToInventory(playerData, packNameAndCount->name, packNameAndCount->count);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void 
BuyPacksCmd_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = NULL;

	if ( pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		CCG_BuildXMLResponseStringWithType(&pFullRetString, "int", "1");
	}
	else
	{
		CCG_BuildXMLResponseStringWithType(&pFullRetString, "int", "0");
	}

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_NAME(CCG_BuyPacks) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int CCG_BuyPacksCmd(CmdContext *pContext, int authToken, CCGPooledStringList *packNames)
{
	TransactionReturnVal *pReturn;
	CCGPlayer *player;
	CCGPackRequests *requests;
	U32 *packDefIDs = NULL;

	player = CCG_FindPlayer(authToken);
	if ( player == NULL )
	{
		return 0;
	}

	ea32Create(&packDefIDs);
	requests = StructCreate(parse_CCGPackRequests);

	FOR_EACH_IN_EARRAY(packNames->list, char, packName)
	{
		if ( CCG_GetPackDef(packName) == NULL )
		{
			ea32Destroy(&packDefIDs);
			StructDestroy(parse_CCGPackRequests, requests);
			return 0;
		}
		CCG_AddPackRequest(&packDefIDs, requests, packName, 1);
	}
	FOR_EACH_END

	pReturn = objCreateManagedReturnVal(BuyPacksCmd_CB, CCG_SetupSlowReturn(pContext));
	AutoTrans_CCG_tr_BuyPacks(pReturn, GLOBALTYPE_CCGSERVER, GLOBALTYPE_CCGPLAYER, player->data->containerID, GLOBALTYPE_CCGPACKDEF, &packDefIDs, requests);

	ea32Destroy(&packDefIDs);
	StructDestroy(parse_CCGPackRequests, requests);

	return 0;
}

#include "CCGPackInventory_h_ast.c"