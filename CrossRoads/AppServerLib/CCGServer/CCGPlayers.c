/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPlayer.h"
#include "CCGPlayers.h"
#include "CCGCardID.h"
#include "CCGServer.h"
#include "CCGPlayerData.h"

#include "objTransactions.h"
#include "AppServerLib.h"
#include "StashTable.h"
#include "LocalTransactionManager.h"

//
// The stash table keeps track of all players.  It is indexed by the
//  auth token.
//
static StashTable players;

//
// Add the player to the collection of players.  Return an error
//  if the player already exists.
//
static bool
AddPlayer(CCGPlayer *player)
{
	return stashIntAddPointer(players, player->authToken, player, false);
}

static bool
RemovePlayer(CCGPlayer *player)
{
	return stashIntRemovePointer(players, player->authToken, NULL);
}

//
// For now we just return increasing integers for the auth tokens.
// Before launch we will want to change this to something less
//  predictable, presumably using random numbers.
//
static U32
GenAuthToken(void)
{
	static U32 lastAuthToken = 0;

	lastAuthToken++;

	return lastAuthToken;
}

//
// Quickly find a player by authToken.
//
CCGPlayer *
CCG_FindPlayer(U32 authToken)
{
	CCGPlayer *player = NULL;

	stashIntFindPointer(players, authToken, &player);

	return player;
}

//
// Slowly find a player by name, by iterating over all players in the
//  stash table.  This should only be done once when logging on.
//  Afterwards, everything should use authToken to reference players.
//
static CCGPlayer *
FindPlayerByName(char *accountName)
{
	FOR_EACH_IN_STASHTABLE(players, CCGPlayer, player) 
	{
		if ( strcmpi(player->data->accountName, accountName) == 0 )
		{
			return player;	
		}
	} FOR_EACH_END;

	return NULL;
}

void
CCG_PlayersInit(void)
{
	players = stashTableCreateInt(CCG_PLAYERS_TABLE_SIZE);
}

typedef struct LoginCBData
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;
	U32 accountID;
} LoginCBData;

static void 
Login_CB(TransactionReturnVal *pReturnVal, LoginCBData *loginCBData)
{

	CCGPlayerData *playerData;
	char *pFullRetString = NULL;

	if ( pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		CCGPlayer *player;
		char *authTokenString = NULL;

		playerData = CCG_GetPlayerDataByAccountID(loginCBData->accountID);

		// player exists in the database, just need to make an
		// auth token and player struct
		player = CCG_CreatePlayer(GenAuthToken(), playerData);
		AddPlayer(player);

		estrPrintf(&authTokenString, "%u", player->authToken);

		CCG_BuildXMLResponseStringWithType(&pFullRetString, "int", authTokenString);

		estrDestroy(&authTokenString);
	}
	else
	{
		// return a 0 auth token on error
		CCG_BuildXMLResponseStringWithType(&pFullRetString, "int", "0");
	}

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, loginCBData->pSlowReturnInfo);
}


//
// The web site will use this command to authenticate the user to
//  the CCG server.  The flow is something like this:
//  1) User logs on to the website.
//  2) Web site uses CCG_Login command to get an auth token
//     and inform the CCG server of the user's identity.
//  3) Web site passes the auth token as an argument to the
//     flash client.
//  4) Flash client sends the auth token to the CCG server
//     to identify it's connection.
//
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
U32
CCG_Login(CmdContext *pContext, U32 accountID, char *accountName)
{
	TransactionReturnVal *pReturn;

	CCGPlayer *player;
	CCGPlayerData *playerData;
	LoginCBData *loginCBData;

	player = FindPlayerByName(accountName);

	// quick out if we already have the player
	if ( player != NULL )
	{
		return player->authToken;
	}

	playerData = CCG_GetPlayerDataByName(accountName);

	if ( playerData != NULL )
	{
		// player exists in the database, just need to make an
		// auth token and player struct
		player = CCG_CreatePlayer(GenAuthToken(), playerData);
		AddPlayer(player);

		return player->authToken;
	}

	// If we get this far, then we have never seen the player before, and they
	//  don't have any player data yet.

	// setup callback data
	loginCBData = (LoginCBData *)malloc(sizeof(LoginCBData));
	loginCBData->pSlowReturnInfo = CCG_SetupSlowReturn(pContext);
	loginCBData->accountID = accountID;

	pReturn = objCreateManagedReturnVal(Login_CB, loginCBData);
	CCG_CreatePlayerData(pReturn, accountID, accountName);

	return 0;
}