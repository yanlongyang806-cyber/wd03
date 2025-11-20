/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslAPAccountLocation.h"
#include "aslAPAccountLocation_c_ast.h"
#include "earray.h"
#include "GlobalTypes.h"
#include "GlobalTypes_h_ast.h"
#include "objTransactions.h"
#include "ServerLib.h"

#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

AUTO_STRUCT;
typedef struct AccountProxyAccountLocation
{
	U32 uAccountID;				AST(KEY)
	ContainerRef *pRef;
} AccountProxyAccountLocation;

typedef struct AccountLocationCBData
{
	AccountLocationCallback cb;
	void *userData;
} AccountLocationCBData;

//Used when an account is on the login server (to send live key/value updates)
static EARRAY_OF(AccountProxyAccountLocation) gAPAccountLocations = NULL;

// Callback for dbOnlineCharacterIDFromAccountID on the ObjectDB
static void CharacterFromAccountID_CB(U32 uResultID, AccountLocationCBData *pCBData)
{
	static ContainerRef *pRef = NULL;
	if(!pRef)
	{
		pRef = StructCreate(parse_ContainerRef);
		devassert(pRef!=NULL);
		pRef->containerType = GLOBALTYPE_ENTITYPLAYER;
	}

	PERFINFO_AUTO_START_FUNC();
	if(!uResultID)
	{
		(*pCBData->cb)(NULL, pCBData->userData);
	}
	else
	{
		pRef->containerID = uResultID;
		(*pCBData->cb)(pRef, pCBData->userData);
	}

	SAFE_FREE(pCBData);

	PERFINFO_AUTO_STOP_FUNC();
}

//Returns a pointer to a cloned/struct created container ref
void aslAPFindAccountLocation(U32 uAccountID, AccountLocationCallback pCallback, void *userData)
{
	AccountProxyAccountLocation *pLoc = eaIndexedGetUsingInt(&gAPAccountLocations, uAccountID);

	//If the account is at a known location, return the location to the callback
	if( pLoc )
	{
		(*pCallback)(pLoc->pRef, userData);
	}
	//Else, see if they're online by pinging the objectDB
	else
	{
		AccountLocationCBData *pCBData = calloc(1, sizeof(AccountLocationCBData));
		pCBData->userData = userData;
		pCBData->cb = pCallback;

		RequestOnlineCharacterIDFromAccountID(uAccountID, CharacterFromAccountID_CB, pCBData);
	}
}

AUTO_COMMAND_REMOTE;
void aslAPCmdSetAccountLocation(U32 uAccountID, GlobalType eType, ContainerID iContID)
{
	AccountProxyAccountLocation *pLoc = eaIndexedGetUsingInt(&gAPAccountLocations, uAccountID);
	if(!pLoc)
	{
		pLoc = StructCreate(parse_AccountProxyAccountLocation);
		pLoc->pRef = StructCreate(parse_ContainerRef);
		pLoc->uAccountID = uAccountID;
		eaPush(&gAPAccountLocations, pLoc);
		eaIndexedEnable(&gAPAccountLocations, parse_AccountProxyAccountLocation);
	}
	devassert(pLoc->pRef);
	pLoc->pRef->containerID = iContID;
	pLoc->pRef->containerType = eType;
}

AUTO_COMMAND_REMOTE;
void aslAPCmdRemoveAccountLocation(U32 uAccountID)
{
	int iIdx = eaIndexedFindUsingInt(&gAPAccountLocations, uAccountID);
	if(iIdx >= 0)
	{
		AccountProxyAccountLocation *pLoc = eaRemove(&gAPAccountLocations, iIdx);
		StructDestroy(parse_AccountProxyAccountLocation, pLoc);
	}
}

#include "aslAPAccountLocation_c_ast.c"