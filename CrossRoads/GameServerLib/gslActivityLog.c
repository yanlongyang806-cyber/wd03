/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslActivityLog.h"
#include "ActivityLogCommon.h"
#include "Entity.h"
#include "Player.h"
#include "entCritter.h"
#include "EntitySavedData.h"
#include "OfficerCommon.h"
#include "Message.h"
#include "PowerTree.h"
#include "Guild.h"
#include "inventoryCommon.h"

#include "objTransactions.h"
#include "gslActivityLog.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

static void
AddEntityLogEntry_CB(TransactionReturnVal *pReturn, void *cbData)
{

}

static void
AddEntityLogEntry(GlobalType entType, ContainerID entID, ActivityLogEntryType entryType, const char *argString, U32 time, float playedTime)
{
	TransactionReturnVal *pReturn;
	ActivityLogEntryTypeConfig *config;

	// if no config exists, then this activity type is disabled
	config = ActivityLog_GetTypeConfig(entryType);
	if ( config != NULL )
	{
		if ( config->addToPersonalLog )
		{
			pReturn = objCreateManagedReturnVal(AddEntityLogEntry_CB, NULL);
			AutoTrans_ActivityLog_tr_AddEntityLogEntry(pReturn, GLOBALTYPE_GAMESERVER, entType, entID, (int)entryType, argString, time, playedTime);
		}
	}
}

static void
AddGuildLogEntry_CB(TransactionReturnVal *pReturn, void *cbData)
{

}
static void
AddGuildLogEntry(Entity *pEnt, ActivityLogEntryType entryType, const char *argString, U32 time, bool includeSubjectID)
{
	TransactionReturnVal *pReturn;
	ActivityLogEntryTypeConfig *config;
	ContainerID subjectID;

	// make sure pEnt is a player with a guild
	if ( ( pEnt->pPlayer != NULL ) && guild_IsMember(pEnt) )
	{
		// if no config exists, then this activity type is disabled
		config = ActivityLog_GetTypeConfig(entryType);
		if ( config != NULL )
		{
			if ( config->addToGuildLog )
			{
				pReturn = objCreateManagedReturnVal(AddGuildLogEntry_CB, NULL);

				if ( includeSubjectID )
				{
					subjectID = pEnt->myContainerID;
				}
				else
				{
					subjectID = 0;
				}
				AutoTrans_ActivityLog_tr_AddGuildLogEntry(pReturn, GLOBALTYPE_GUILDSERVER, GLOBALTYPE_GUILD, pEnt->pPlayer->pGuild->iGuildID, (int)entryType, argString, time, subjectID);
			}
		}
	}
}

void
gslActivity_AddLevelUpEntry(Entity *pEnt, U32 newLevel)
{
	char stringBuf[20];
	U32 time = timeSecondsSince2000();
	float playedTime = 0.0f;

	if ( pEnt->pPlayer != NULL )
	{
		playedTime = pEnt->pPlayer->fTotalPlayTime;
	}

	// convert the level to a string
	itoa(newLevel, stringBuf, 10);

	AddEntityLogEntry(pEnt->myEntityType, pEnt->myContainerID, ActivityLogEntryType_LevelUp, stringBuf, time, playedTime);
	AddGuildLogEntry(pEnt, ActivityLogEntryType_LevelUp, stringBuf, time, true);
}

void
gslActivity_AddPetAddEntry(Entity *playerEnt, Entity *petEnt)
{
	PetDef *petDef;
	ActivityLogPetEventConfig *petEventConfig;
	U32 time;
	float playedTime = 0.0f;

	if ( ( petEnt == NULL ) || ( petEnt->pCritter == NULL ) || ( petEnt->pSaved == NULL ) || ( playerEnt == NULL ) || ( playerEnt->pSaved == NULL ) )
	{
		return;
	}

	petDef = GET_REF(petEnt->pCritter->petDef);
	if ( petDef == NULL )
	{
		return;
	}

	time = timeSecondsSince2000();

	// get pet event config based on pet type
	petEventConfig = ActivityLog_GetPetEventConfig(petdef_GetCharacterClassType(petDef));

	if ( playerEnt->pPlayer != NULL )
	{
		playedTime = playerEnt->pPlayer->fTotalPlayTime;
	}

	if ( petEventConfig != NULL )
	{
		// add the log entry to the player 
		AddEntityLogEntry(playerEnt->myEntityType, playerEnt->myContainerID, petEventConfig->playerAddPetEntryType, petEnt->pSaved->savedName, time, playedTime);

		// add the log entry to the pet
		AddEntityLogEntry(petEnt->myEntityType, petEnt->myContainerID, petEventConfig->petAddPetEntryType, playerEnt->pSaved->savedName, time, 0.0f);

		// add log entry to player's guild
		AddGuildLogEntry(playerEnt, petEventConfig->playerAddPetEntryType, petEnt->pSaved->savedName, time, true);
	}
	return;
}

void
gslActivity_AddPetDismissEntry(Entity *playerEnt, Entity *petEnt)
{
	PetDef *petDef;
	ActivityLogPetEventConfig *petEventConfig;
	U32 time;
	float playedTime = 0.0f;

	if ( ( playerEnt == NULL ) || ( petEnt == NULL ) || ( petEnt->pCritter == NULL ) || ( petEnt->pSaved == NULL ) )
	{
		return;
	}

	petDef = GET_REF(petEnt->pCritter->petDef);
	if ( petDef == NULL )
	{
		return;
	}

	time = timeSecondsSince2000();

	// get pet event config based on pet type
	petEventConfig = ActivityLog_GetPetEventConfig(petdef_GetCharacterClassType(petDef));

	if ( playerEnt->pPlayer != NULL )
	{
		playedTime = playerEnt->pPlayer->fTotalPlayTime;
	}

	if ( petEventConfig != NULL )
	{
		// add the log entry to the player 
		AddEntityLogEntry(playerEnt->myEntityType, playerEnt->myContainerID, petEventConfig->playerDismissPetEntryType, petEnt->pSaved->savedName, time, playedTime);

		// add log entry to player's guild
		AddGuildLogEntry(playerEnt, petEventConfig->playerDismissPetEntryType, petEnt->pSaved->savedName, time, true);
	}

	// no need to add a log entry to the pet, since it is being deleted.

	return;
}

void
gslActivity_AddPetRenameEntry(Entity *playerEnt, Entity *petEnt, const char *oldName)
{
	PetDef *petDef;
	ActivityLogPetEventConfig *petEventConfig;
	U32 time;
	float playedTime = 0.0f;

	if ( ( petEnt == NULL ) || ( petEnt->pCritter == NULL ) || ( petEnt->pSaved == NULL ) )
	{
		return;
	}

	petDef = GET_REF(petEnt->pCritter->petDef);
	if ( petDef == NULL )
	{
		return;
	}

	time = timeSecondsSince2000();

	// get pet event config based on pet type
	petEventConfig = ActivityLog_GetPetEventConfig(petdef_GetCharacterClassType(petDef));

	if ( playerEnt->pPlayer != NULL )
	{
		playedTime = playerEnt->pPlayer->fTotalPlayTime;
	}

	if ( petEventConfig != NULL )
	{
		char *argString = NULL;

		// stick both old and new name in the arg string, delimited by a colon
		estrConcatf(&argString, "%s:%s", oldName, petEnt->pSaved->savedName);

		// add the log entry to the player 
		AddEntityLogEntry(playerEnt->myEntityType, playerEnt->myContainerID, petEventConfig->playerRenamePetEntryType, argString, time, playedTime);

		// add the log entry to the pet
		AddEntityLogEntry(petEnt->myEntityType, petEnt->myContainerID, petEventConfig->petRenamePetEntryType, argString, time, 0.0f);

		// add log entry to player's guild
		AddGuildLogEntry(playerEnt, petEventConfig->playerRenamePetEntryType, argString, time, true);

		// clean up the temporary string
		estrDestroy(&argString);
	}
}

void
gslActivity_AddPetPromoteEntry(Entity *playerEnt, Entity *petEnt)
{
	PetDef *petDef;
	ActivityLogPetEventConfig *petEventConfig;
	U32 time;
	float playedTime = 0.0f;

	if ( ( playerEnt == NULL ) || ( petEnt == NULL ) || ( petEnt->pCritter == NULL ) || ( petEnt->pSaved == NULL ) )
	{
		return;
	}

	petDef = GET_REF(petEnt->pCritter->petDef);
	if ( petDef == NULL )
	{
		return;
	}

	time = timeSecondsSince2000();

	// get pet event config based on pet type
	petEventConfig = ActivityLog_GetPetEventConfig(petdef_GetCharacterClassType(petDef));

	if ( playerEnt->pPlayer != NULL )
	{
		playedTime = playerEnt->pPlayer->fTotalPlayTime;
	}

	if ( petEventConfig != NULL )
	{
		char *argString = NULL;
		OfficerRankDef *rankDef;
		Message *rankMessage;

		rankDef = Officer_GetRankDef(inv_GetNumericItemValue(petEnt,"StarfleetRank"), GET_REF(playerEnt->hAllegiance), GET_REF(playerEnt->hSubAllegiance));
		if ( rankDef == NULL )
		{
			return;
		}

		rankMessage = GET_REF(rankDef->pDisplayMessage->hMessage);
		if ( rankMessage == NULL )
		{
			return;
		}

		// stick both pet name and new rank message key in the arg string, delimited by a colon
		estrConcatf(&argString, "%s:%s", petEnt->pSaved->savedName, rankMessage->pcMessageKey);

		// add the log entry to the player 
		AddEntityLogEntry(playerEnt->myEntityType, playerEnt->myContainerID, petEventConfig->playerPromotePetEntryType, argString, time, playedTime);

		// add the log entry to the pet
		AddEntityLogEntry(petEnt->myEntityType, petEnt->myContainerID, petEventConfig->petPromotePetEntryType, argString, time, 0.0f);

		// add log entry to player's guild
		AddGuildLogEntry(playerEnt, petEventConfig->playerPromotePetEntryType, argString, time, true);

		// clean up the temporary string
		estrDestroy(&argString);
	}
}

void
gslActivity_AddPetTrainEntry(Entity *playerEnt, Entity *petEnt, const char *newSkillNode)
{
	PetDef *petDef;
	ActivityLogPetEventConfig *petEventConfig;
	U32 time;
	float playedTime = 0.0f;

	if ( ( playerEnt == NULL ) || ( petEnt == NULL ) || ( petEnt->pCritter == NULL ) || ( petEnt->pSaved == NULL ) )
	{
		return;
	}

	petDef = GET_REF(petEnt->pCritter->petDef);
	if ( petDef == NULL )
	{
		return;
	}

	time = timeSecondsSince2000();

	// get pet event config based on pet type
	petEventConfig = ActivityLog_GetPetEventConfig(petdef_GetCharacterClassType(petDef));

	if ( playerEnt->pPlayer != NULL )
	{
		playedTime = playerEnt->pPlayer->fTotalPlayTime;
	}

	if ( petEventConfig != NULL )
	{
		char *argString = NULL;
		PTNodeDef *nodeDef;

		nodeDef = powertreenodedef_Find( newSkillNode );

		if ( nodeDef != NULL )
		{
			Message *powerNameMsg = GET_REF(nodeDef->pDisplayMessage.hMessage);
			if ( powerNameMsg != NULL )
			{

				// stick both pet name and new rank message key in the arg string, delimited by a colon
				estrConcatf(&argString, "%s:%s", petEnt->pSaved->savedName, powerNameMsg->pcMessageKey);

				// add the log entry to the player 
				AddEntityLogEntry(playerEnt->myEntityType, playerEnt->myContainerID, petEventConfig->playerTrainPetEntryType, argString, time, playedTime);

				// add the log entry to the pet
				AddEntityLogEntry(petEnt->myEntityType, petEnt->myContainerID, petEventConfig->petTrainPetEntryType, argString, time, 0.0f);

				// add log entry to player's guild
				AddGuildLogEntry(playerEnt, petEventConfig->playerTrainPetEntryType, argString, time, true);

				// clean up the temporary string
				estrDestroy(&argString);
				
			}
		}
	}
}

void
gslActivity_AddPetTradeEntry(Entity *srcEnt, Entity *destEnt, Entity *petEnt)
{
	ActivityLogPetEventConfig *petEventConfig;

	if ( ( srcEnt != NULL ) && ( destEnt != NULL ) && ( petEnt != NULL ) )
	{
		PetDef *petDef;
		U32 time;

		petDef = GET_REF(petEnt->pCritter->petDef);
		if ( petDef == NULL )
		{
			return;
		}

		time = timeSecondsSince2000();

		// get pet event config based on pet type
		petEventConfig = ActivityLog_GetPetEventConfig(petdef_GetCharacterClassType(petDef));

		if ( petEventConfig != NULL )
		{
			char *srcAndPetArgString = NULL;
			char *destAndPetArgString = NULL;
			char *srcAndDestArgString = NULL;
			float srcPlayedTime = 0.0f;
			float destPlayedTime = 0.0f;

			if ( srcEnt->pPlayer != NULL )
			{
				srcPlayedTime = srcEnt->pPlayer->fTotalPlayTime;
			}

			if ( destEnt->pPlayer != NULL )
			{
				destPlayedTime = destEnt->pPlayer->fTotalPlayTime;
			}

			// Create the various args strings that we need.  All have two names delimited by a colon.
			estrConcatf(&srcAndPetArgString, "%s:%s", srcEnt->pSaved->savedName, petEnt->pSaved->savedName);
			estrConcatf(&destAndPetArgString, "%s:%s", destEnt->pSaved->savedName, petEnt->pSaved->savedName);
			estrConcatf(&srcAndDestArgString, "%s:%s", srcEnt->pSaved->savedName, destEnt->pSaved->savedName);

			AddEntityLogEntry(srcEnt->myEntityType, srcEnt->myContainerID, petEventConfig->sourcePlayerTradePetEntryType, destAndPetArgString, time, srcPlayedTime);
			AddGuildLogEntry(srcEnt, petEventConfig->sourcePlayerTradePetEntryType, destAndPetArgString, time, true);

			AddEntityLogEntry(destEnt->myEntityType, destEnt->myContainerID, petEventConfig->destPlayerTradePetEntryType, srcAndPetArgString, time, destPlayedTime);
			AddGuildLogEntry(destEnt, petEventConfig->destPlayerTradePetEntryType, srcAndPetArgString, time, true);

			AddEntityLogEntry(petEnt->myEntityType, petEnt->myContainerID, petEventConfig->petTradePetEntryType, srcAndDestArgString, time, 0.0f);

			// free up the strings
			estrDestroy(&srcAndPetArgString);
			estrDestroy(&destAndPetArgString);
			estrDestroy(&srcAndDestArgString);
		}
	}
}