/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerLib.h"
#include "EntityIterator.h"
#include "Entity.h"
#include "gslExtern.h"
#include "gslEntity.h"
#include "StringUtil.h"
#include "gslTransactions.h"
#include "LoggedTransactions.h"
#include "Player.h"
#include "file.h"
#include "gslSharedBank.h"
#include "GameAccountDataCommon.h"
#include "SharedBankCommon.h"
#include "EntityLib.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_PRIVATE;
void SharedBankLoadOrCreate(Entity *pEntity)
{
	if(pEntity && pEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER)
	{
		gslSharedBankLoadOrCreate(pEntity);
	}
}

// Set the ref to the container, do a shared bank create
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void SharedBankInit(Entity *pEnt, bool bDoCreate)
{
	if(pEnt && pEnt->pPlayer)
	{
		U32 uTm = timeSecondsSince2000();
		// if it already exists don't init again, and not m,ore than once per 2 seconds
		if(!GET_REF(pEnt->pPlayer->hSharedBank) && uTm > pEnt->pPlayer->uSharedBankInitTime + 2)
		{
			NOCONST(Entity) *pEntNoconst = CONTAINER_NOCONST(Entity, pEnt);

			pEnt->pPlayer->uSharedBankInitTime = uTm;

			if(bDoCreate)
			{
				// create and subscribe the bank container
				// note that even if this is called with the container already created the following call is safe.
				SharedBankLoadOrCreate(pEnt);
			}
			else
			{
				char idBuf[128];
				// subscribe the bank container
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSHAREDBANK), ContainerIDToString(pEnt->pPlayer->accountID, idBuf), pEntNoconst->pPlayer->hSharedBank);
			}
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

// Add numeric to the shared bank
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void SharedBank_AddNumeric(Entity *pEnt, S32 iToBank, const char *pcNumeric)
{
	if(pEnt && pEnt->pPlayer)
	{
		Entity *pSharedBankEnt = GET_REF(pEnt->pPlayer->hSharedBank);
		if(pSharedBankEnt)
		{
			gslSharedBank_TransferNumeric(pEnt, iToBank, pcNumeric);
		}
	}
}

