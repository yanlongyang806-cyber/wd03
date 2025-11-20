/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPlayerData.h"
#include "CCGServer.h"
#include "CCGCardID.h"
#include "CCGAttribute.h"
#include "CCGDeck.h"
#include "CCGChamps.h"
#include "AppServerLib.h"
#include "UtilitiesLib.h"
#include "StashTable.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "objContainer.h"
#include "objIndex.h"
#include "StringCache.h"

#include "AutoGen/CCGPlayerData_h_ast.h"

static ObjectIndex *accountIDIndex;
static ObjectIndex *accountNameIndex;

//
// Global container add/remove callbacks
//
// These are used to keep the by name lookup stash tables up to date.
//
static void
addPlayerDataContainer_CB(Container *container, CCGPlayerData *playerData)
{
	if ( !objIndexInsert(accountIDIndex, playerData) )
	{
		printf("Failed to add PlayerData %s to accountID index\n", playerData->accountName);
	}

	if ( !objIndexInsert(accountNameIndex, playerData) )
	{
		printf("Failed to add PlayerData %s to accountName index\n", playerData->accountName);
	}

	printf("PlayerData Add Callback, containerID:%u\n", playerData->accountID);
}

static void
removePlayerDataContainer_CB(Container *container, CCGPlayerData *playerData)
{
	if ( !objIndexRemove(accountIDIndex, playerData) )
	{
		printf("Failed to remove PlayerData %s to accountID index\n", playerData->accountName);
	}

	if ( !objIndexRemove(accountNameIndex, playerData) )
	{
		printf("Failed to remove PlayerData %s to accountName index\n", playerData->accountName);
	}

	printf("PlayerData Remove Callback, containerID:%u\n", playerData->accountID);

}

static void
InitIndices(void)
{
	//For updating indices.
	objRegisterContainerTypeAddCallback(GLOBALTYPE_CCGPLAYER, addPlayerDataContainer_CB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_CCGPLAYER, removePlayerDataContainer_CB);

	accountIDIndex = objIndexCreateWithStringPath(0, 0, parse_CCGPlayerData, ".accountID");
	accountNameIndex = objIndexCreateWithStringPath(0, 0, parse_CCGPlayerData, ".accountName");
}

//
// get player data by account ID
//
CCGPlayerData *
CCG_GetPlayerDataByAccountID(U32 accountID)
{
	CCGPlayerData *playerData;
	ObjectIndexKey key = objIndexCreateKey_Int(accountIDIndex, accountID);

	if ( ( accountIDIndex->count > 0 ) && ( objIndexGet(accountIDIndex, key, 0, &playerData) ) )
	{
		return playerData;
	}
	return NULL;
}

//
// look up a player data by name
//
CCGPlayerData *
CCG_GetPlayerDataByName(char *accountName)
{
	CCGPlayerData *playerData;
	ObjectIndexKey key = objIndexCreateKey_String(accountNameIndex, accountName);

	if ( ( accountNameIndex->count > 0 ) && ( objIndexGet(accountNameIndex, key, 0, &playerData) ) )
	{
		return playerData;
	}
	return NULL;
}

//
// command to get full player data by name
//
AUTO_COMMAND ACMD_NAME(CCG_GetPlayerData) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
CCGPlayerData *
CCG_GetPlayerDataCmd(char *accountName)
{
	CCGPlayerData *playerData = CCG_GetPlayerDataByName(accountName);

	if ( playerData != NULL )
	{
		return StructClone(parse_CCGPlayerData, playerData);
	}

	return NULL;
}

//
// Create a new player data
//
void
CCG_CreatePlayerData(TransactionReturnVal *pReturn, U32 accountID, char *accountName)
{
	NOCONST(CCGPlayerData) *playerData = StructCreate(parse_CCGPlayerData);

	playerData->accountID = accountID;
	strncpy(playerData->accountName, accountName, MAX_NAME_LEN);

	// XXX - call champions ccg specific player init code
	CCGChamps_InitPlayerData(playerData);

	objRequestContainerCreate(pReturn, GLOBALTYPE_CCGPLAYER, playerData, objServerType(), objServerID());

	StructDestroy(parse_CCGPlayerData, playerData);
}

static void 
CreatePlayerDataCmd_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = CCG_CreateSimpleReturnString("Create PlayerData", pReturnVal);

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_NAME(CCG_CreatePlayerData) ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_CreatePlayerDataCmd(CmdContext *pContext, U32 accountID, char *accountName)
{
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(CreatePlayerDataCmd_CB, CCG_SetupSlowReturn(pContext));
	CCG_CreatePlayerData(pReturn, accountID, accountName);
}

//
// Init code for player data module
//
void
CCG_PlayerDataInitLate(void)
{
	static GlobalType playerDataContainerType = GLOBALTYPE_CCGPLAYER;
	
	InitIndices();

	aslAcquireContainerOwnership(&playerDataContainerType);
}

AUTO_TRANS_HELPER;
void
CCG_trh_AddPacksToInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, const char *packName, U32 count)
{
	NOCONST(CCGPackCount) *packCount;
	packCount = eaIndexedGetUsingString(&playerData->packInventory, packName);
	if ( packCount == NULL )
	{
		// Player doesn't have any of this kind of pack, so we need to create
		//  a new entry in their pack inventory.
		packCount = StructCreate(parse_CCGPackCount);
		packCount->packType = allocAddString(packName);
		packCount->count = count;
		eaPush(&playerData->packInventory, packCount);
	}
	else
	{
		packCount->count += count;
	}
}

//
// Check to see if the specified number of the given pack are present
//  in the player's pack inventory.
//
AUTO_TRANS_HELPER;
bool
CCG_trh_CheckPacksInInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, const char *packName, U32 count)
{
	NOCONST(CCGPackCount) *packCount;
	packCount = eaIndexedGetUsingString(&playerData->packInventory, packName);
	if ( ( packCount != NULL ) && ( packCount->count >= count ) )
	{
		return true;
	}

	return false;
}

//
// Remove some number of the named packs from the player's pack inventory
// 
// NOTE - doesn't do any validation.  It is the main transaction's
//  responsibility to do the validation.
//
AUTO_TRANS_HELPER;
void
CCG_trh_RemovePacksFromInventory(ATH_ARG NOCONST(CCGPlayerData) *playerData, const char *packName, U32 count)
{
	NOCONST(CCGPackCount) *packCount;
	int packIndex = eaIndexedFindUsingString(&playerData->packInventory, packName);

	if ( packIndex >= 0 )
	{
		packCount = eaGet(&playerData->packInventory, packIndex);
		packCount->count -= count;

		// if the player is down to zero of this pack, then remove it from their inventory
		if ( packCount->count == 0 )
		{
			eaRemove(&playerData->packInventory, packIndex);
		}
	}
}


//
// get rid of a player
// NOTE - this doesn't clean up well, so it shouldn't be used on a shard that has
//  people playing on it.
//
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void
CCG_DestroyPlayerData(char *playerName)
{
	CCGPlayerData *playerData = CCG_GetPlayerDataByName(playerName);
	if ( playerData != NULL )
	{
		objRequestContainerDestroy(NULL, GLOBALTYPE_CCGPLAYER, playerData->containerID, objServerType(), objServerID());
	}
}
#include "CCGPlayerData_h_ast.c"
