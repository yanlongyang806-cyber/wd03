/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslTransactions.h"
#include "gslExtern.h"
#include "objContainer.h"

#include "EntityLib.h"
#include "EntityResolver.h"
#include "GameServerLib.h"
#include "ServerLib.h"
#include "error.h"
#include "earray.h"
#include "EntityBuild.h"
#include "EntityBuild_h_ast.h"
#include "EntityIterator.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "gslCommandParse.h"
#include "AutoGen/Entity_h_ast.h"
#include "PowerTree.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "autogen/GameServerLib_autogen_SlowFuncs.h"
#include "file.h"
#include "gslContact.h"
#include "gslCostume.h"
#include "gslSendToClient.h"
#include "gslEntity.h"
#include "gslEntityNet.h"
#include "gslGameAccountData.h"
#include "gslLogSettings.h"
#include "LoggedTransactions.h"
#include "gslSavedPet.h"
#include "gslPowerTransactions.h"
#include "gslChat.h"
#include "EntityMovementManager.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "Powers.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "gslMission.h"
#include "gslMission_transact.h"
#include "mission_common.h"
#include "mission_common_h_ast.h"
#include "team.h"
#include "StringCache.h"
#include "../objects/objTransactionCommands.h"
#include "../objects/objLocks.h"
#include "logging.h"
#include "AutoTransDefs.h"
#include "inventoryTransactions.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Guild.h"
#include "Team.h"
#include "PlayerBooter.h"
#include "CostumeCommonEntity.h"
#include "entCritter.h"
#include "gslMechanics.h"
#include "Leaderboard.h"
#include "StaticWorld/ZoneMap.h"
#include "SteamCommonServer.h"
#include "gslPartition.h"

#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "Autogen/ObjectDB_autogen_remotefuncs.h"
#include "Autogen/AppServerLib_autogen_remotefuncs.h"
#include "Autogen/ChatServer_autogen_remotefuncs.h"
#include "Autogen/GameServerLib_autogen_remotefuncs.h"
#include "Autogen/Character_h_ast.h"
#include "Autogen/CharacterAttribs_h_ast.h"
#include "Autogen/MapDescription_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/rewardCommon_h_ast.h"

static bool sbFastEntityUpdates = false;
AUTO_CMD_INT(sbFastEntityUpdates, FastEntityUpdates);

static void entObjBackupCommitCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if (ent && ent->pSaved && ent->pSaved->pEntityBackup
		//&& 	(!AreFastLocalCopiesActive() || GetFastLocalModifyCopy(con->containerType, con->containerID) != ent->pSaved->pEntityBackup)
		)
	{
		ObjectPathOperation *operation = operations[0];
		// Skip the apply if it already happened in an auto transaction
		ParseTable *table_out;
		int column_out;
		void *structptr_out;
		int index_out;

		int pathFlags = OBJPATHFLAG_DONTLOOKUPROOTPATH;

		if (objPathResolveFieldWithResult(operation->pathEString, con->containerSchema->classParse, ent->pSaved->pEntityBackup, &table_out, &column_out, &structptr_out, &index_out, OBJPATHFLAG_DONTLOOKUPROOTPATH, NULL))
		{
			if (!objPathApplySingleOperation(table_out,column_out,structptr_out,index_out,operation->op,operation->valueEString,operation->quotedValue,NULL))
			{
				if (isDevelopmentMode())
				{
					Errorf("Failure to execute path operation on backup ent: %d %s %s.",operation->op, operation->pathEString, operation->valueEString);
				}
			}
		}
	}
}

static void *entObjGetBackup(GlobalType type, ContainerID id)
{
	Entity *ent = entFromContainerIDAnyPartition(type, id);
	if (ent && ent->pSaved && ent->pSaved->pEntityBackup)
	{
		return ent->pSaved->pEntityBackup;
	}
	return NULL;
}

static void *entObjGetAutoTransBackup(GlobalType type, ContainerID id)
{
	Entity *ent = entFromContainerIDAnyPartition(type, id);
	if (ent && ent->pSaved && ent->pSaved->pAutoTransBackup)
	{
		return ent->pSaved->pAutoTransBackup;
	}
	return NULL;
}

static void entObjTransformToPuppet_PostAddQueuedPet(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent)
	{
		ent->pSaved->bCheckPets = true;
		if(ent->pSaved->pPuppetMaster)
			ent->pSaved->pPuppetMaster->bPuppetCheckPassed = false;
	}
}

static void entObjSavedDirtyBit(Container* con, ObjectPathOperation **operations)
{
	Entity* e = con->containerData;
	entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
}

static void entObjPlayerDirtyBit(Container* con, ObjectPathOperation **operations)
{
	Entity* e = con->containerData;
	entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
}

static void entObjCharacterDirtyBit(Container* con, ObjectPathOperation **operations)
{
	Entity* e = con->containerData;
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
}

static bool entRootOnlyCB(Container *con, ObjectPathOperation *operation)
{
	return !strstri(operation->pathEString, ".");
}


static void entObjEntityDirtyBit(Container* con, ObjectPathOperation **operations)
{
	Entity* e = con->containerData;
	entity_SetDirtyBit(e, parse_Entity, e, false);
}

static void entObjBuildsChangePostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *pEnt = con->containerData;
	if(pEnt && pEnt->pSaved)
	{
		ObjectPathOperation *operation = operations[0];
		ParseTable *parseTable = NULL;
		EntityBuild *pBuild = NULL;
		char *fullPath = NULL;
		char *temp = NULL;

		fullPath = strdup(operation->pathEString);
		if (fullPath)
			temp = fullPath + strlen(fullPath);

		objPathGetStruct(fullPath, parse_Entity, pEnt, &parseTable, &pBuild);
		while (temp && temp > fullPath && parseTable != parse_EntityBuild)
		{
			while (temp > fullPath && *temp != '.')
				--temp;
			if (temp > fullPath)
				*temp = '\0';
			objPathGetStruct(fullPath, parse_Entity, pEnt, &parseTable, &pBuild);
		}

		if (pBuild && parseTable == parse_EntityBuild)
		{
			entity_SetDirtyBit(pEnt, parseTable, pBuild, true);
		}
		free(fullPath);
	}
}

static void gslGameAccountTimed_CB(TimedCallback *callback, F32 timeSinceLastCallback, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pEnt = entFromEntityRefAnyPartition(*pRef);
	if(pEnt && entGetAccountID(pEnt) > 0)
	{
		TransactionReturnVal *pReturn = LoggedTransactions_MakeEntReturnVal("Process_GameAccountData", pEnt);
		AutoTrans_gslGAD_tr_ProcessNewVersion(pReturn, GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), GLOBALTYPE_GAMEACCOUNTDATA, entGetAccountID(pEnt), false);
	}
	SAFE_FREE(pRef);
}

static void entObjPendingGameAccountCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];
		if(operation->op==TRANSOP_CREATE)
		{
			EntityRef *pRef = calloc(1, sizeof(EntityRef));
			*pRef = entGetRef(ent);
			TimedCallback_Run(gslGameAccountTimed_CB, pRef, 0.1f);
		}
	}
}

// Callback whenever a Power is added or removed from the Character's personal list.
//  Called AFTER the add or remove operation is completed.
static void entObjFixPowersPersonalPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pChar)
	{
		int i;
		for (i = eaSize(&operations)-1; i >= 0; i--)
		{
			ObjectPathOperation *operation = operations[i];
			if(operation->op==TRANSOP_CREATE || operation->op==TRANSOP_DESTROY)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);
				character_ResetPowersArray(entGetPartitionIdx(ent), ent->pChar, pExtract);
				break;
			}
		}
	}
}

// Callback whenever a Power is added or removed from the Character's class list.
//  Called AFTER the add or remove operation is completed.
static void entObjFixPowersClassPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pChar)
	{
		int i;
		for (i = eaSize(&operations)-1; i >= 0; i--)
		{
			ObjectPathOperation *operation = operations[i];
			if(operation->op==TRANSOP_CREATE || operation->op==TRANSOP_DESTROY)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);
				character_ResetPowersArray(entGetPartitionIdx(ent), ent->pChar, pExtract);
				break;
			}
		}
	}
}

// Callback whenever a Power is added or removed from the Character's PowerTrees.
//  Called BEFORE the add or remove operation is completed.
static void entObjFixPowersTreePreCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pChar)
	{
		ObjectPathOperation *operation = operations[0];
		if(operation->op==TRANSOP_DESTROY && strEndsWith(operation->pathEString,"pppowers"))
		{
			char *pchOperation = strdup(operation->pathEString);
			char *pchTemp = strrchr(pchOperation,'"');
			if(pchTemp)
				*pchTemp = 0;
			pchTemp = strrchr(pchOperation,'"');
			if(pchTemp)
			{
				PTNode *pNode = (PTNode*)character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, ent->pChar),NULL,pchTemp+1);
				if(pNode && eaSize(&pNode->ppPowers)>1)
				{
					int i;
					for(i=eaSize(&pNode->ppPowers)-2; i>=0; i--)
					{
						if(pNode->ppPowers[i] && power_DefDoesActivate(GET_REF(pNode->ppPowers[i]->hDef)))
							break;
					}
					if(i>=0)
					{
						U32 uiIDAct = pNode->ppPowers[i]->uiID;
						U32 uiIDDeleted = pNode->ppPowers[eaSize(&pNode->ppPowers)-1]->uiID;
						character_PowerSlotsReplaceID(ent->pChar,uiIDDeleted,uiIDAct);
					}
				}
			}
			free(pchOperation);
		}
	}
}

// Callback whenever a Power is added or removed from the Character's PowerTrees.
//  Called AFTER the add or remove operation is completed.
static void entObjFixPowersTreePostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pChar)
	{
		int i;
		for (i = eaSize(&operations)-1; i >= 0; i--)
		{
			ObjectPathOperation *operation = operations[i];
			if(operation->op==TRANSOP_CREATE || operation->op==TRANSOP_DESTROY)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);
				character_ResetPowersArray(entGetPartitionIdx(ent), ent->pChar, pExtract);
				break;
			}
		}

		character_DirtyInnateAccrual(ent->pChar);
		character_DirtyPowerTrees(ent->pChar);
		character_PowerTreesFixup(ent->pChar);
	}
}

// Callback whenever a Stat is modified on a character.
static void entObjFixPowersStatsPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;

	if(ent && ent->pChar)
	{
		character_DirtyPowerStats(ent->pChar);
	}
}

// Sanity check callback whenever a Character is modified
static void entObjCharacterSanityCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;

	if(ent && ent->pChar)
	{
		character_SanityCheck(ent->pChar);
	}
}

// Callback for any time the player's nemeses change
static void entObjNemesesChangePostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if (ent)
	{
		entity_SetDirtyBit(ent, parse_PlayerNemesisInfo, &ent->pPlayer->nemesisInfo, true);
		// Player dirtybit set through transaction callbacks
	}
}

static void entObjInventoryChangePostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent->pInventoryV2)
	{
		entity_SetDirtyBit(ent, parse_Inventory,ent->pInventoryV2,true);
	}
}

// Callback whenever an entity's inventory changes
static void entObjInventoryChangeNumericPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if( ent )
	{
		const char* pchNumeric = operations[0]->valueEString;
		item_HandleInventoryChangeNumeric(ent, pchNumeric, false);
	}
}

//Check to see if the operation string contains a modification to the Numerics bag.
//This function makes basically the same assumptions about operation strings as entObjInventoryChangeNonNumericPostCB().
static bool entObjInventoryChangeNumericPostCBFilter(Container *con, ObjectPathOperation *operation)
{
	const char pcStringToFind[] = ".Pplitebags[";
	const int iPrefixLength = ARRAY_SIZE(pcStringToFind)-1;
	char* pchBagKeySubstring = strstri(operation->pathEString, pcStringToFind);
	bool retval = false;

	if (!pchBagKeySubstring)
		return false;

	pchBagKeySubstring += iPrefixLength;
	if (pchBagKeySubstring[0] == '\"')
	{
		//string or int bag
		retval = (strnicmp(pchBagKeySubstring, "\"Numeric\"", 9) == 0) ||
					(atoi(pchBagKeySubstring+1) == InvBagIDs_Numeric);
	}
	else
	{
		//int bag
		int iBagID = atoi(pchBagKeySubstring);
		retval = (iBagID == InvBagIDs_Numeric);
	}

	return retval;
}

// Callback whenever an entity's xp level changes
static void entObjInventoryChangeLevelNumericPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if( ent )
	{
		item_HandleInventoryChangeLevelNumeric(ent, false);
	}
}

static bool entObjInventoryChangeLevelNumericPostCBFilter(Container *con, ObjectPathOperation *operation)
{
	return !!strstri(operation->pathEString, "Ppindexedliteslots[\"Level\"]");
}

static void entObjInventoryChangeNonNumericPostCB(Container *con, ObjectPathOperation **operations)
{
	int i;
	bool bCheckCostume = false;
	Entity *ent = con->containerData;

	//This callback makes the following assumptions:
	// 1) All operation strings begin with the contents of pcPrefixAssumption. (Check out the matchString parameter when this callback is registered on lines 1710 and 1711.)
	// 2) If the next character exists, it will be '[', after which the bag will either represented by an integer or a string in quotes.
	// 3) operations->pathEString can be modified temporarily.
	
	//If the format of our transaction operation strings ever changes, 
	// the only fix this function should need is a modification to pcPrefixAssumption.
	const char pcPrefixAssumption[] = ".pInventoryV2.ppInventoryBags";
	const int iPrefixLength = ARRAY_SIZE(pcPrefixAssumption)-1;

	//Search all operation strings for bagIDs that might affect your costume.
	//Search backwards because equipbag operations tend to be last in the list,
	// allowing us an earlier exit.
	//Possible additional optimization: It's likely that we only need to check the first and last operation strings due to the way they are grouped.
	for (i = eaSize(&operations)-1; i >= 0; i--)
	{
		InvBagIDs id = 0;
		const char* pAssumedOpenBrace = (operations[i]->pathEString + iPrefixLength);
		
		if (*pAssumedOpenBrace == '\0')
		{
			//This operation is creating or destroying ppInventoryBags itself. Perhaps we are creating an inventory for a new entity?
			continue;
		}

		assertmsgf(*(pAssumedOpenBrace) == '[', "Got an unexpected operation string in entObjInventoryChangeNonNumericPostCB(): \"%s\"", operations[i]->pathEString);

		if (*(pAssumedOpenBrace + 1) == '\"')
		{
			//string (or rarely, an int in quotes) representation of a bag
			// replace ending quote with null terminator so we don't have to do a strcpy

			char* pEndQuote;
			pEndQuote = strchr(pAssumedOpenBrace+2, '\"');
			*pEndQuote = '\0';
			id = StaticDefineInt_FastStringToInt(InvBagIDsEnum, pAssumedOpenBrace+2, 0);
			*pEndQuote = '\"';
			if (id <= 0)
				id = atoi(pAssumedOpenBrace+2);
		}
		else
		{
			//int representation
			// _atoi64 works fine with any non-numeric terminating character (such as ']'), so no need to modify or copy.
			id = _atoi64(pAssumedOpenBrace+1);
		}
		if (invBagIDs_BagIDCanAffectCostume(id))
		{
			bCheckCostume = true;
			break;
		}
	}

	if( ent )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);
		int iPartitionIdx = entGetPartitionIdx(ent);

		if(ent->pChar)
		{
			character_ResetPowersArray(iPartitionIdx, ent->pChar, pExtract);
			character_updateTacticalRequirements(ent->pChar);
			character_DirtyItems(ent->pChar);
		}

		if(ent->pInventoryV2)
		{
			entSetActive( ent );
		}

		// Regen costume if we modified a bag that could need it.
		if (bCheckCostume)
			costumeEntity_ApplyItemsAndPowersToCostume(iPartitionIdx, ent, true, pExtract);

		if (ent->pPlayer)
		{
			ent->pPlayer->lastCalloutTime = 0; // Reset callout time

			contact_PlayerInventoryChanged(ent);
		}
	}
}

static void entObjGuildChangePostCB(Container *pContainer, ObjectPathOperation **ppOperations)
{
	Entity *pEnt = pContainer->containerData;
	if (pEnt) {
		gslGuild_HandlePlayerGuildChange(pEnt);
	}
}

static void entObjTeamChangePostCB(Container *pContainer, ObjectPathOperation **ppOperations)
{
	Entity *pEnt = pContainer->containerData;
	if (pEnt) {
		gslTeam_HandlePlayerTeamChange(pEnt);
	}
}

static void entObjFixMissionInfoPreCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];

		if(operation->op==TRANSOP_DESTROY)
		{
			MissionInfo *info = NULL;
			ParseTable *parseTable = NULL;
			char *fullPath = NULL;

			// Get the MissionInfo being destroyed
			estrStackCreate(&fullPath);
			estrPrintf(&fullPath, "%s", operation->pathEString);
			objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &info);
			if (info && parseTable == parse_MissionInfo)
			{
				mission_tr_MissionInfoPreDestroyCB(ent, info);
			}
			estrDestroy(&fullPath);
		}
	}
}

static void entObjFixMissionInfoPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];
		if(operation->op==TRANSOP_CREATE)
		{
			MissionInfo *info = NULL;
			ParseTable *parseTable = NULL;
			char *fullPath = NULL;

			// Get the MissionInfo being created
			estrStackCreate(&fullPath);
			estrPrintf(&fullPath, "%s", operation->pathEString);
			objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &info);
			if (info && parseTable == parse_MissionInfo)
			{
				mission_tr_MissionInfoPostCreateCB(ent, info);
			}
			estrDestroy(&fullPath);
		}
	}
}

static void entObjFixMissionPostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];
		if(operation->op==TRANSOP_CREATE)
		{
			ParseTable *parseTable = NULL;
			Mission *newMission = NULL;
			Mission *parentMission = NULL;
			char *fullPath = NULL;
			char *temp = NULL;

			// Get the new Mission
			estrStackCreate(&fullPath);
			estrPrintf(&fullPath, "%s[\"%s\"]", operation->pathEString, operation->valueEString);
			objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &newMission);
			if (newMission && parseTable == parse_Mission)
			{
				// Get the parent mission
				temp = strrchr(operation->pathEString, '.');
				if (temp) *temp = '\0';
				objPathGetStruct(operation->pathEString, parse_Entity, ent, &parseTable, &parentMission);
				if (parseTable != parse_Mission) // this was a top-level mission
					parentMission = NULL;
				if (temp) *temp = '.';

				mission_tr_MissionPostCreateCB(ent, newMission, parentMission);
			}
			estrDestroy(&fullPath);
		}
	}
}

static void entObjFixMissionPreCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];
		if(operation->op==TRANSOP_DESTROY)
		{
			ParseTable *parseTable = NULL;
			Mission *newMission = NULL;
			Mission *parentMission = NULL;
			char *fullPath = NULL;
			char *temp = NULL;

			// Get the Mission being destroyed
			estrStackCreate(&fullPath);
			estrPrintf(&fullPath, "%s[\"%s\"]", operation->pathEString, operation->valueEString);
			objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &newMission);
			if (newMission && parseTable == parse_Mission)
			{
				// Get the parent mission
				temp = strrchr(operation->pathEString, '.');
				if (temp) *temp = '\0';
				objPathGetStruct(operation->pathEString, parse_Entity, ent, &parseTable, &parentMission);
				if (parseTable != parse_Mission) // this was a top-level mission
					parentMission = NULL;
				if (temp) *temp = '.';

				mission_tr_MissionPreDestroyCB(ent, newMission, parentMission);
			}
			estrDestroy(&fullPath);
		}
	}
}

static void entObjMissionChangePostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];
		ParseTable *parseTable = NULL;
		Mission *pMission = NULL;
		char *fullPath = NULL;
		char *temp = NULL;

		// Get the Mission that was modified
		fullPath = strdup(operation->pathEString);
		if (fullPath)
			temp = fullPath + strlen(fullPath);

		objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &pMission);
		while (temp && temp > fullPath && parseTable != parse_Mission)
		{
			while (temp > fullPath && *temp != '.')
				--temp;
			if (temp > fullPath)
				*temp = '\0';
			objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &pMission);
		}

		if (pMission && parseTable == parse_Mission)
		{
			// Flag as dirty
			mission_FlagAsDirty(pMission);
		}
		free(fullPath);
	}
}

static void entObjCompletedMissionChangePostCB(Container *con, ObjectPathOperation **operations)
{
	Entity *ent = con->containerData;
	if(ent && ent->pPlayer)
	{
		ObjectPathOperation *operation = operations[0];
		ParseTable *parseTable = NULL;
		CompletedMission *pCompletedMission = NULL;
		char *fullPath = NULL;
		char *temp = NULL;

		// Get the CompletedMission that was modified
		fullPath = strdup(operation->pathEString);
		if (fullPath)
			temp = fullPath + strlen(fullPath);

		objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &pCompletedMission);
		while (temp && temp > fullPath && parseTable != parse_CompletedMission)
		{
			while (temp > fullPath && *temp != '.')
				--temp;
			if (temp > fullPath)
				*temp = '\0';
			objPathGetStruct(fullPath, parse_Entity, ent, &parseTable, &pCompletedMission);
		}

		if (pCompletedMission && parseTable == parse_CompletedMission)
		{
			// Flag as dirty
			mission_FlagCompletedMissionAsDirty(mission_GetInfoFromPlayer(ent), pCompletedMission);
		}
		free(fullPath);
	}
}

void gslConnectToTransactionServer(void)
{
	static char *pErrorString = NULL;


	loadstart_printf("Connecting to Transaction server... ");

	if (InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, &pErrorString))
	{
		loadend_printf("connected to %s.",gServerLibState.transactionServerHost);
		return;
	}
	else
	{
		//in dev mode, BGB shards sometimes don't have multiplexers, might as
		//well not crash
		if (isDevelopmentMode() && gServerLibState.bUseMultiplexerForTransactions)
		{
			loadstart_printf("First try failed: %s. Trying again without expecting a multiplexer", pErrorString);
			gServerLibState.bUseMultiplexerForTransactions = false;
			gServerLibState.bUseMultiplexerForLogging = false;

			if (InitObjectTransactionManager(
				GetAppGlobalType(),
				gServerLibState.containerID,
				gServerLibState.transactionServerHost,
				gServerLibState.transactionServerPort,
				gServerLibState.bUseMultiplexerForTransactions, &pErrorString))
			{
				loadend_printf("connected to %s.",gServerLibState.transactionServerHost);
				return;
			}
		}


		assertmsgf(0,"Failed to connect to transaction Server (this is fatal). Reason: %s", pErrorString);
	}
	estrDestroy(&pErrorString);
}

void entSimpleTransaction(Entity *ent, char *pTransName, char *msg)
{
	objRequestTransactionSimple(NULL,entGetType(ent),entGetContainerID(ent),pTransName, msg);
}


#undef entSimpleTransactionf
void entSimpleTransactionf(Entity *ent, char *pTransName, const char *query, ...)
{
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, query );
	estrConcatfv(&commandstr,query,va);
	va_end( va );

	entSimpleTransaction(ent,pTransName, commandstr);
	estrDestroy(&commandstr);
}


void gslSendEntityToDatabase(Entity *ent, bool bRunTransact)
{
	char *diffString = NULL;
	char *wrongDiff = NULL;
	Entity *bEnt = ent;
	Container *pObject;

	PERFINFO_AUTO_START_FUNC();

	devassertmsgf(!GetTransactionCurrentlyHappening(objLocalManager()), "Cannot send entity to DB while inside another transaction (%s)! Will corrupt data", GetTransactionCurrentlyHappening(objLocalManager()));

	// Make sure the position is current from mm.

	entUpdateView(ent);

	gslExternPlayerSave(ent, bRunTransact);

	pObject = entGetContainer(ent);

	estrStackCreateSize(&diffString,10000);
	estrStackCreateSize(&wrongDiff,10000); // Take this out for performance reasons after issues have been resolved

	StructWriteTextDiff(&diffString,parse_Entity,bEnt->pSaved->pEntityBackup,ent,NULL,TOK_PERSIST,TOK_NO_TRANSACT,TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT);
	StructWriteTextDiff(&diffString,parse_Entity,bEnt->pSaved->pEntityBackup,ent,NULL,TOK_PERSIST,TOK_SOMETIMES_TRANSACT,TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT);

	StructWriteTextDiff(&wrongDiff,parse_Entity,bEnt->pSaved->pEntityBackup,ent,NULL,TOK_PERSIST,0,0);

	// Use old, bad flags for now
	if (estrLength(&wrongDiff))
	{
		RemoteCommand_DBUpdateNonTransactedData(GLOBALTYPE_OBJECTDB,0,entGetType(ent),entGetContainerID(ent),wrongDiff);

		//filelog_printf("transactions.log","%s\n",diffString);
	}

	if (estrLength(&diffString) != estrLength(&wrongDiff))
	{
		entLog(LOG_GSL,ent,"BadTransactionDataModified","Transacted data was modified outside of a transaction: BAD DIFF %s GOOD DIFF %s",wrongDiff,diffString);
	}

	if (bEnt->pSaved->pEntityBackup)
	{
		StructReset(parse_Entity, bEnt->pSaved->pEntityBackup);
	}
	else
	{
		bEnt->pSaved->pEntityBackup = StructCreateWithComment(parse_Entity, "EntityBackup created in gslSendEntityToDatabase");
	}
	StructCopyFields(parse_Entity,ent,bEnt->pSaved->pEntityBackup,TOK_PERSIST,0);

	estrDestroy(&diffString);
	estrDestroy(&wrongDiff);

	PERFINFO_AUTO_STOP();
}


//AUTO_TRANSACTION;
//enumTransactionOutcome trUpdateMapHistoryAndOtherPeriodicStuff(ATR_ARGS, NOCONST(Entity)* ent, NON_CONTAINER MapDescription *newDesc, float fSecsPassed)
//{
//	MapDescription *oldDesc = eaGet(&ent->pSaved->mapHistory,eaSize(&ent->pSaved->mapHistory) - 1);
//	if (oldDesc && IsSameMapDescription(oldDesc, newDesc))
//	{
//		StructCopy(parse_MapDescription, newDesc, oldDesc, 0, 0, 0);
//	}
//	else
//	{	
//		eaPush(&ent->pSaved->mapHistory, StructClone(parse_MapDescription, newDesc));
//	}
//
////	ent->pPlayer->fTotalPlayTime += fSecsPassed;
//	TRANSACTION_RETURN_LOG_SUCCESS("Updated Map History");
//}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Psaved.Blastmapstatic, .Psaved.Laststaticmap, .Psaved.Lastnonstaticmap");
enumTransactionOutcome trUpdateMapHistory(ATR_ARGS, NOCONST(Entity)* ent, NON_CONTAINER MapDescription *newDesc)
{
	MapDescription *oldDesc = NULL;
	if ( NONNULL(ent) && NONNULL(ent->pSaved) )
	{
		if ( ent->pSaved->bLastMapStatic )
		{
			oldDesc = (MapDescription *)ent->pSaved->lastStaticMap;
		}
		else
		{
			oldDesc = (MapDescription *)ent->pSaved->lastNonStaticMap;
		}

		if (oldDesc && IsSameMapDescription(oldDesc, newDesc))
		{
			StructCopy(parse_MapDescription, newDesc, oldDesc, 0, 0, 0);
			SavedMapUpdateRotationForPersistence(MapDescription_to_SavedMapDescription_DeConst(oldDesc));
		}
		else
		{	
			if ( newDesc->eMapType == ZMTYPE_STATIC )
			{
				StructDestroyNoConstSafe(parse_SavedMapDescription, &ent->pSaved->lastStaticMap);
				ent->pSaved->lastStaticMap = StructCloneVoid(parse_MapDescription, newDesc);
				SavedMapUpdateRotationForPersistence(ent->pSaved->lastStaticMap);
				ent->pSaved->bLastMapStatic = true;
			}
			else
			{
				StructDestroyNoConstSafe(parse_SavedMapDescription, &ent->pSaved->lastNonStaticMap);
				ent->pSaved->lastNonStaticMap = StructCloneVoid(parse_MapDescription, newDesc);
				SavedMapUpdateRotationForPersistence(ent->pSaved->lastNonStaticMap);
				ent->pSaved->bLastMapStatic = false;
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
NOCONST(SavedAttribute) *character_NewSavedAttribute(ATH_ARG NOCONST(Character) *pChar, TempAttribute *pAttribToCopy)
{
	NOCONST(SavedAttribute)* pNewAttribute = StructCreateNoConst(parse_SavedAttribute);

	StructCopyAllVoid(parse_SavedAttribute,pAttribToCopy,pNewAttribute);

	eaPush(&pChar->ppSavedAttributes,pNewAttribute);

	return pNewAttribute;
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Pchar.Ppsavedattributes, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pchar.Ilevelexp")
ATR_LOCKS(itemOwner, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pchar.Ilevelexp");
enumTransactionOutcome trUpdateSavedAttributes(ATR_ARGS, NOCONST(Entity) *ent, NON_CONTAINER TempAttributes *pAttributes, NOCONST(Entity) *itemOwner, U64 uiItemID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int iAttrib;

	for(iAttrib=0;iAttrib<eaSize(&pAttributes->ppAttributes);iAttrib++)
	{
		AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum,pAttributes->ppAttributes[iAttrib]->pchAttrib);
		int i;

		for(i=eaSize(&ent->pChar->ppSavedAttributes)-1;i>=0;i--)
		{
			AttribType eSavedAttrib = StaticDefineIntGetInt(AttribTypeEnum,ent->pChar->ppSavedAttributes[i]->pchAttrib);

			if(eSavedAttrib == eAttrib)
			{
				ent->pChar->ppSavedAttributes[i]->fValue = pAttributes->ppAttributes[iAttrib]->fValue;
				break;
			}
		}

		if(i==-1)
		{
			character_NewSavedAttribute(ent->pChar,pAttributes->ppAttributes[iAttrib]);
		}
	}
	

	if(uiItemID)
	{
		if(NONNULL(itemOwner))
		{
			if(!inv_RemoveItemByID(ATR_PASS_ARGS, itemOwner, uiItemID, 1, 0, pReason, pExtract))
			{
				TRANSACTION_RETURN_LOG_FAILURE("%s[%d] Unable to remove item by id %"FORM_LL"d",
					GlobalTypeToName(itemOwner->myEntityType), itemOwner->myContainerID, uiItemID);
			}
		}
		else if(!inv_RemoveItemByID(ATR_PASS_ARGS,ent,uiItemID,1,0,pReason,pExtract))
		{
			TRANSACTION_RETURN_LOG_FAILURE("%s[%d] Unable to remove item by id %"FORM_LL"d",
				GlobalTypeToName(ent->myEntityType), ent->myContainerID, uiItemID);
		}
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Attribute Updated");
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pTempPuppet, ".Ppsavedattributes");
void trhTempPuppetPreSave(ATH_ARG NOCONST(TempPuppetEntity) *pTempPuppet, NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo)
{
	int i;

	eaDestroyStructNoConst(&pTempPuppet->ppSavedAttributes,parse_SavedAttribute);

	for(i=0;i<eaSize(&pPreSaveInfo->pTempAttributes->ppAttributes);i++)
	{
		NOCONST(SavedAttribute) *pSavedAttribute = StructCreateNoConst(parse_SavedAttribute);
		StructCopyAllVoid(parse_SavedAttribute,pPreSaveInfo->pTempAttributes->ppAttributes[i],pSavedAttribute);
		eaPush(&pTempPuppet->ppSavedAttributes,pSavedAttribute);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pchar.Ppsavedattributes");
enumTransactionOutcome trCharacterPreSave(ATR_ARGS, NOCONST(Entity) *ent, NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo)
{
	int i;

	if(ISNULL(ent) || ISNULL(ent->pChar))
		TRANSACTION_RETURN_LOG_FAILURE("Attributes not saved, not a valid character");

	eaDestroyStructNoConst(&ent->pChar->ppSavedAttributes,parse_SavedAttribute);

	for(i=0;i<eaSize(&pPreSaveInfo->pTempAttributes->ppAttributes);i++)
	{
		character_NewSavedAttribute(ent->pChar,pPreSaveInfo->pTempAttributes->ppAttributes[i]);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Attributes Saved");
}


void gslEntityUpdateMapHistoryAndOtherPeriodicStuff(Entity *ent, float fSecsPassed)
{
	if (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		return;
	}

	if (ent->myEntityType == GLOBALTYPE_ENTITYPLAYER && !isProductionEditMode())
	{
		MapDescription newDesc = {0};
		TempAttributes newAttributes = {0};
		Quat spawnRot;

		PERFINFO_AUTO_START_FUNC();

		StructCopy(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription, &newDesc, 0, 0, 0);
		entGetPos(ent, newDesc.spawnPos);
		entGetRot(ent, spawnRot);
		quatToPYR(spawnRot, newDesc.spawnPYR);
		newDesc.spawnPoint = allocAddString(POSITION_SET);

		entity_updateLockedLeaderboardStats(ent);

		gslEntityPlayerLogStatePeriodic(ent, false, fSecsPassed);

		AutoTrans_trUpdateMapHistory(LoggedTransactions_CreateManagedReturnVal("UpdateMapHistory", NULL, NULL),
			GetAppGlobalType(),
			entGetType(ent),
			entGetContainerID(ent),
			&newDesc);

		PERFINFO_AUTO_STOP();
	}
}

// If an entity is locked, we need to defer until later so we don't end up with transact vs no-transact data conflicts
typedef struct DeferredEntitySave
{
	GlobalType type;
	ContainerID id;
	int framesDeferred;
} DeferredEntitySave;

bool gslEntitySafeToSend(Entity *ent)
{
	U32 ignored;
	return CanContainerBeLocked(0, ent->myEntityType, ent->myContainerID, &ignored);
}

void gslEntityBackupTick(void)
{
	// Saves each character once every 2048 seconds (34 minutes)
	//  sbFastEntityUpdates set to 1 changes it to 8 seconds, or a number > 1 gets you 2^x seconds
	U32		mask_charsave = sbFastEntityUpdates ? (sbFastEntityUpdates>1 ? (1<<sbFastEntityUpdates)-1 : 7) : 2047;
	int i;
	static		F32		fTimeBetweenSaves = 1.0f;
	static		U32		counter=0;
	static		int timer=-1;
	static		DeferredEntitySave **ppDeferred = 0;

	EntityIterator* iter;
	Entity *currEnt;

	if (timer < 0)
		timer = timerAlloc();
	if (timerElapsed(timer) < fTimeBetweenSaves)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	// Restart the timer
	timerAdd(timer, -fTimeBetweenSaves);
	counter = (counter+1) & mask_charsave;

	// Retry any deferred updates
	for (i = eaSize(&ppDeferred) - 1; i>= 0; i--)
	{
		currEnt = entFromContainerIDAnyPartition(ppDeferred[i]->type, ppDeferred[i]->id);
		if (!currEnt || entCheckFlag(currEnt, ENTITYFLAG_IGNORE))
		{
			// Entity no longer exists or is hidden, discard the update
			free(eaRemoveFast(&ppDeferred, i));

			continue;
		}
		if (gslEntitySafeToSend(currEnt))
		{
			gslSendEntityToDatabase(currEnt,true);
			gslEntityUpdateMapHistoryAndOtherPeriodicStuff(currEnt, fTimeBetweenSaves * (mask_charsave + 1));

			free(eaRemoveFast(&ppDeferred, i));
		}
		else if (ppDeferred[i]->framesDeferred > 50)
		{
			// It's been too long, log it and push the update anyway

			// Always log this.  Don't gate on gbEnablePetAndPuppetLogging
			entLog(LOG_CONTAINER, currEnt, "PushForced", "Non-transact data push forced due to constant locks");

			gslSendEntityToDatabase(currEnt,true);
			gslEntityUpdateMapHistoryAndOtherPeriodicStuff(currEnt, fTimeBetweenSaves * (mask_charsave + 1));

			free(eaRemoveFast(&ppDeferred, i));

		}
		else
		{
			ppDeferred[i]->framesDeferred++;
		}
	}

	iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while (currEnt = EntityIteratorGetNext(iter))
	{
		if ((entGetContainerID(currEnt) & mask_charsave)==counter)
		{
			if (gslEntitySafeToSend(currEnt))
			{
				gslSendEntityToDatabase(currEnt,true);
				gslEntityUpdateMapHistoryAndOtherPeriodicStuff(currEnt, fTimeBetweenSaves * (mask_charsave + 1));
			}
			else
			{
				DeferredEntitySave *pData = calloc(sizeof(DeferredEntitySave),1);
				pData->type = entGetType(currEnt);
				pData->id = entGetContainerID(currEnt);
				pData->framesDeferred = 1;
				eaPush(&ppDeferred, pData);
			}
		}
	}
	EntityIteratorRelease(iter);

	iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYSAVEDPET);
	while (currEnt = EntityIteratorGetNext(iter))
	{
		if ((entGetContainerID(currEnt) & mask_charsave)==counter)
		{
			if (gslEntitySafeToSend(currEnt))
			{	
				gslSendEntityToDatabase(currEnt,true);
				gslEntityUpdateMapHistoryAndOtherPeriodicStuff(currEnt, fTimeBetweenSaves * (mask_charsave + 1));
			}
			else
			{
				DeferredEntitySave *pData = calloc(sizeof(DeferredEntitySave),1);
				pData->type = entGetType(currEnt);
				pData->id = entGetContainerID(currEnt);
				pData->framesDeferred = 1;
				eaPush(&ppDeferred, pData);
			}
		}
	}
	EntityIteratorRelease(iter);

	iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPUPPET);
	while (currEnt = EntityIteratorGetNext(iter))
	{
		if ((entGetContainerID(currEnt) & mask_charsave)==counter)
		{
			if (gslEntitySafeToSend(currEnt))
			{	
				gslSendEntityToDatabase(currEnt,true);
				gslEntityUpdateMapHistoryAndOtherPeriodicStuff(currEnt, fTimeBetweenSaves * (mask_charsave + 1));
			}
			else
			{
				DeferredEntitySave *pData = calloc(sizeof(DeferredEntitySave),1);
				pData->type = entGetType(currEnt);
				pData->id = entGetContainerID(currEnt);
				pData->framesDeferred = 1;
				eaPush(&ppDeferred, pData);
			}
		}
	}
	EntityIteratorRelease(iter);
	
	PERFINFO_AUTO_STOP();
}

typedef struct EntityLogoutStructure
{
	SlowRemoteCommandID slowID;
	GlobalType containerType;
	ContainerID containerID;
	U32 iPlayerBooterHandle;
} EntityLogoutStructure;



static void LogoutPlayerBooterReturnCB(TransactionReturnVal *returnVal, EntityLogoutStructure *logoutStructure)
{	
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		objLog(LOG_LOGIN,logoutStructure->containerType,logoutStructure->containerID,0,NULL,NULL,NULL,
			"PlayerBooted", NULL, "success");
		PlayerBooterAttemptReturn(logoutStructure->iPlayerBooterHandle, true, "GameServer %u sucessfully completed a map transfer transaction",
			GetAppGlobalID());
	}
	else
	{
		
		objLog(LOG_LOGIN,logoutStructure->containerType,logoutStructure->containerID,0,NULL,NULL,NULL,
			"PlayerBooted", NULL, "failure, reason: %s", GetTransactionFailureString(returnVal));
		PlayerBooterAttemptReturn(logoutStructure->iPlayerBooterHandle, false, "Gameserver %u tried to complete a map transfer transaction, failed because: %s",
			GetAppGlobalID(), GetTransactionFailureString(returnVal));
	}
	SAFE_FREE(logoutStructure);
}

static void LogoutCommandReturnCB(TransactionReturnVal *returnVal, EntityLogoutStructure *logoutStructure)
{	
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		objLog(LOG_LOGIN,logoutStructure->containerType,logoutStructure->containerID,0,NULL,NULL,NULL,
			"ForceLogOut", NULL, "success");
		SlowRemoteCommandReturn_ForceEntityLogOut(logoutStructure->slowID,1);
	}
	else
	{
		objLog(LOG_LOGIN,logoutStructure->containerType,logoutStructure->containerID,0,NULL,NULL,NULL,
			"ForceLogOut", NULL, "failure, reason: %s", GetTransactionFailureString(returnVal));
		SlowRemoteCommandReturn_ForceEntityLogOut(logoutStructure->slowID,0);
	}
	SAFE_FREE(logoutStructure);
}
//Keep ForceEntityLogout in sync with OVERRIDE_LATELINK_AttemptToBootPlayerWithBooter
AUTO_COMMAND_REMOTE_SLOW(int);
void ForceEntityLogOut(U32 containerID, SlowRemoteCommandID iCmdID)
{
	Container *con = objGetContainer(GLOBALTYPE_ENTITYPLAYER,containerID);
	ClientLink *link;

	PERFINFO_AUTO_START_FUNC();

	if (!con || con->isBeingForceLoggedOut)
	{
		SlowRemoteCommandReturn_ForceEntityLogOut(iCmdID,0);
		PERFINFO_AUTO_STOP();
		return;
	}

	con->isBeingForceLoggedOut = true;

	link = entGetClientLink((Entity*)con->containerData);

	if (link)
	{
		link->clientLoggedIn = false;
		link->readyForGeneralUpdates = false;
		gslSendForceLogout(link, "ForcedLogout");
	
	}

	gslLogOutEntity(con->containerData, iCmdID, 0);

	PERFINFO_AUTO_STOP();
}

//Keep ForceEntityLogout in sync with OVERRIDE_LATELINK_AttemptToBootPlayerWithBooter
void OVERRIDE_LATELINK_AttemptToBootPlayerWithBooter(ContainerID iPlayerToBootID, U32 iHandle, char *pReason)
{
	Container *con;
	ClientLink *link;;

	PERFINFO_AUTO_START_FUNC();

	if (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER)
	{
		PlayerBooterAttemptReturn(iHandle, false, "Player %u is on a web request server and can't be booted", 
			iPlayerToBootID);
		PERFINFO_AUTO_STOP();
		return;
	}



	con = objGetContainer(GLOBALTYPE_ENTITYPLAYER,iPlayerToBootID);

	if (!con)
	{
		PlayerBooterAttemptReturn(iHandle, false, "Player %u appears not to be on Gameserver[%u]",
			iPlayerToBootID, GetAppGlobalID());
		PERFINFO_AUTO_STOP();
		return;
	}

	if (con->isBeingForceLoggedOut)
	{
		PlayerBooterAttemptReturn(iHandle, false, "Player %u seems to already be being logged out on Gameserver[%u]",
			iPlayerToBootID, GetAppGlobalID());
		PERFINFO_AUTO_STOP();
		return;
	}

	con->isBeingForceLoggedOut = true;

	link = entGetClientLink((Entity*)con->containerData);

	if (link)
	{
		link->clientLoggedIn = false;
		link->readyForGeneralUpdates = false;
		gslSendForceLogout(link, pReason);
	
	}

	gslLogOutEntity(con->containerData, 0, iHandle);

	PERFINFO_AUTO_STOP();
}








void gslLeaveMap(Entity *ent)
{
	PERFINFO_AUTO_START_FUNC();
	
	if (!entCheckFlag(ent,ENTITYFLAG_PLAYER_LOGGING_IN) && !entCheckFlag(ent,ENTITYFLAG_PLAYER_LOGGING_OUT))
	{
		// If we successfully logged in before, call the logout callbacks
		gslExternPlayerSave(ent,true);
		gslPlayerLeftMap(ent,true);
	}
	
	ent->pPlayer->clientLink = 0;		

	entClearCodeFlagBits(ent, ENTITYFLAG_PLAYER_LOGGING_IN);
	entSetCodeFlagBits(ent, ENTITYFLAG_PLAYER_LOGGING_OUT);
	entSetCodeFlagBits(ent, ENTITYFLAG_PLAYER_DISCONNECTED);
	entSetCodeFlagBits(ent, ENTITYFLAG_IGNORE);
	mmDisabledHandleCreate(&ent->mm.mdhIgnored, ent->mm.movement, __FILE__, __LINE__);
	gslEntitySetInvisibleTransient(ent, 1);

	if(entCheckFlag(ent, ENTITYFLAG_DONOTFADE))
		gslEntityAddDeleteFlags(ent, ENTITY_DELETE_NOFADE);

	// Send feedback to any CSR reps tracking this player
	gslSendCSRFeedback(ent, "Player is leaving map.");

	PERFINFO_AUTO_STOP();
}

static void LogoutNormalCB(TransactionReturnVal *returnVal, EntityLogoutStructure *logoutStructure)
{	
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		objLog(LOG_LOGIN,logoutStructure->containerType,logoutStructure->containerID,0,NULL,NULL,NULL,
			"NormalLogOut", NULL, "success");
	}
	else
	{
		objLog(LOG_LOGIN,logoutStructure->containerType,logoutStructure->containerID,0,NULL,NULL,NULL,
			"NormalLogOut", NULL, "failure");
	}
	SAFE_FREE(logoutStructure);
}

void gslLogOutEntityEx(Entity *ent, SlowRemoteCommandID iCmdID, U32 iPlayerBooterHandle, LogoffType eLogoffType)
{
	PERFINFO_AUTO_START_FUNC();
	{
	TransactionReturnVal *returnVal;	
	ContainerID id = entGetContainerID(ent);
	GlobalType type = entGetType(ent);
	EntityLogoutStructure *logoutStructure = calloc(sizeof(EntityLogoutStructure),1);

	assertmsgf(!iCmdID || !iPlayerBooterHandle, "gslLogOutEntity can't have both iCmdID and iPlayerBooterHandle");

	logoutStructure->slowID = iCmdID;
	logoutStructure->containerType = type;
	logoutStructure->containerID = id;
	logoutStructure->iPlayerBooterHandle = iPlayerBooterHandle;

	if (iPlayerBooterHandle)
	{
		returnVal = objCreateManagedReturnVal(LogoutPlayerBooterReturnCB,logoutStructure);
	}
	else if (iCmdID)
	{
		returnVal = objCreateManagedReturnVal(LogoutCommandReturnCB,logoutStructure);
	}
	else
	{
		returnVal = objCreateManagedReturnVal(LogoutNormalCB,logoutStructure);
	}

	gslLeaveMap(ent);
	gslExternPlayerLogout(ent, eLogoffType);

	objRequestContainerMove(returnVal, type, id, objServerType(), objServerID(), GLOBALTYPE_OBJECTDB, 0);
	}
	PERFINFO_AUTO_STOP();
}

void gslLogOutEntity(Entity *ent, SlowRemoteCommandID iCmdID, U32 iPlayerBooterHandle)
{
	gslLogOutEntityEx(ent, iCmdID, iPlayerBooterHandle, kLogoffType_Normal);
}

void gslForceLogOutEntity(Entity *ent, const char *pReason)
{
	Container *con;
	ClientLink *link;;

	con = entGetContainer(ent);

	con->isBeingForceLoggedOut = true;

	link = entGetClientLink(ent);

	if (link)
	{
		link->clientLoggedIn = false;
		link->readyForGeneralUpdates = false;
		gslSendForceLogout(link, pReason);
	}

	gslLogOutEntity(ent, 0, 0);
}

static void gslLogOutEntity_LogoffType(Entity *pEnt, F32 fTime, LogoffType eType)
{
	if(entGetAccessLevel(pEnt) < ACCESS_GM)
	{ 
		gslLogoff_StartTimer(pEnt, fTime, eType);
	}
	else
	{
		gslLogoff_StartTimer(pEnt, 0.f, eType);
	}
}

void gslLogOutEntityGoToCharacterSelect(Entity *pEnt)
{
	gslLogOutEntity_LogoffType(pEnt, g_pLogoffConfig.fNormalLogoffTime, kLogoffType_GoToCharacterSelect);
}

void gslLogOutMeetPartyInLobby(Entity *pEnt)
{
	gslLogOutEntity_LogoffType(pEnt, g_pLogoffConfig.fNormalLogoffTime, kLogoffType_MeetPartyInLobby);
}

void gslLogOutEntityNormal(Entity *pEnt)
{
	gslLogOutEntity_LogoffType(pEnt, g_pLogoffConfig.fNormalLogoffTime, kLogoffType_Normal);
}

void gslLogOutEntityDisconnect(Entity *pEnt)
{
	gslLogOutEntity_LogoffType(pEnt, g_pLogoffConfig.fDisconnectLogoffTime, kLogoffType_Disconnect);
}


// Replace the backup copy with the ent backup, so it sends any changed fields
enumTransactionOutcome PlayerLogoutCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	// Grab the real one, not the transaction copy
	Entity *bEnt = entFromContainerIDAnyPartition(newPlayer->myEntityType,newPlayer->myContainerID);

	if (!bEnt)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Ent");
	}

	// Players who are only summoned for a CSR command shouldn't have their map history modified
	if (!gslIsOfflineCSREnt(newPlayer->myEntityType, newPlayer->myContainerID) && IsGameServerSpecificallly_NotRelatedTypes()
		
		//players who never finished logging on shouldn't have their map history modified
		&& bEnt->initPlayerLoginPositionRun
		){
		gslEntityMapHistoryLeftMap(newPlayer, NULL);
	}

	if (bEnt->pSaved->pEntityBackup)
	{	
		StructCopyFieldsDeConst(parse_Entity, bEnt->pSaved->pEntityBackup, backupPlayer, TOK_PERSIST, 0);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Replace the backup copy with the ent backup, so it sends any changed fields
enumTransactionOutcome PetLogoutCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	// Grab the real one, not the transaction copy
	Entity *bEnt = entFromContainerIDAnyPartition(newPlayer->myEntityType,newPlayer->myContainerID);

	if (!bEnt)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Ent");
	}

	if (bEnt->pSaved->pEntityBackup)
	{	
		StructCopyFieldsDeConst(parse_Entity, bEnt->pSaved->pEntityBackup, backupPlayer, TOK_PERSIST, 0);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// override this per-project
void DEFAULT_LATELINK_PlayerLoginFixup(NOCONST(Entity) *newPlayer)
{

}

// Called when entity enters map for the first time
// This is INSIDE A TRANSACTION
enumTransactionOutcome PlayerLoginCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{	
	GameAccountDataExtract *pExtract;
	ItemChangeReason reason = {0};
	U32 iPartitionID;
	U32 uiCurrentTime = timeSecondsSince2000();
	U32 uiTimeOffline = 0;

	if (uiCurrentTime > newPlayer->pPlayer->iLastPlayedTime)
		uiTimeOffline = uiCurrentTime - newPlayer->pPlayer->iLastPlayedTime;

	newPlayer->pPlayer->uiContainerArrivalTime = uiCurrentTime;

	if (gGSLState.bLocked)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player %s attempted to log in, but the server was locked", ENTDEBUGNAME(newPlayer));
	}

	//for transfers to GatewayServer, always use partition 1
	if (gGSLState.gbGatewayServer || gGSLState.gbWebRequestServer)
	{
		iPartitionID = partition_IDFromIdx(1);
		assertmsgf(iPartitionID != -1, "Attempting to log player %s into a GATEWAYSERVER, but the default partition doesn't seem to exist",
			ENTDEBUGNAME(newPlayer));
	}
	else
	{
		iPartitionID = partition_GetUpcomingTransferToPartitionID_Raw(newPlayer->myContainerID);
		if (!iPartitionID)
		{
			TRANSACTION_RETURN_LOG_FAILURE("Player %s attempted to log in, but does not have a registered partition ID. Login failed", 
										   ENTDEBUGNAME(newPlayer));
		}
	}

// If the partition doesn't exist, then we must fail here or bad things will happen later.
	if (!partition_ExistsByID(iPartitionID))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Player %s attempted to log in, but partition %u longer exists. Login failed", 
									   ENTDEBUGNAME(newPlayer), iPartitionID);
	}


	pExtract = entity_GetCachedGameAccountDataExtract((Entity*)newPlayer);

	// Players who are only summoned for a CSR command shouldn't have their map history modified
	if (!gslIsOfflineCSREnt(newPlayer->myEntityType, newPlayer->myContainerID) && IsGameServerSpecificallly_NotRelatedTypes()){
		gslEntityMapHistoryEnteredMap(newPlayer);
	}
	
	// Make sure the Entity gets an initial EntityBuild, should it deserve one
	if(newPlayer->pSaved)
	{
		if(!eaSize(&newPlayer->pSaved->ppBuilds) && entity_BuildCanCreate(newPlayer))
		{
			BuildCreateParam param = {0};
			param.uiValidateTag = newPlayer->pSaved->uiBuildValidateTag;
			trEntity_BuildCreate(ATR_EMPTY_ARGS, newPlayer, &param);
		}
		else
		{
			entity_FixupBuilds(ATR_EMPTY_ARGS, newPlayer);
		}
	}

	inv_FillItemChangeReason(&reason, NULL, "LoginFixup", "Player");

	inv_ent_FixItemIDs(newPlayer);
	inv_ent_FixIndexedItemNames(newPlayer);
	inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS, newPlayer, true, true, &reason);
	inv_trh_UpdatePlayerBags(ATR_EMPTY_ARGS, newPlayer, pExtract);
	inv_trh_FixupEquipBags(ATR_EMPTY_ARGS, newPlayer, newPlayer, &reason, pExtract, true);
	if (costumetransaction_ShouldUpdateCostumeSlots((Entity*)newPlayer, false)) {
		const char *pcSlotSet = costumeEntity_GetSlotSetName((Entity*)newPlayer, false);
		costumeEntity_trh_FixupCostumeSlots(ATR_EMPTY_ARGS, NULL, newPlayer, pcSlotSet);
	}
	character_LoadTransact(newPlayer);
	mission_trh_CleanupRecentSecondaryList(newPlayer);
	mission_trh_FixupPerkTitles(newPlayer, &reason, pExtract);

	if (newPlayer->pPlayer->playerFlags & PLAYERFLAG_NEW_CHARACTER)
	{
		newPlayer->pPlayer->playerFlags &= ~PLAYERFLAG_NEW_CHARACTER;
		inv_ent_InitializeNewPlayerSettings(newPlayer);
		//gslExternPlayerFinishCreation(newPlayer);	
		
		//Set the reference to the game account data in new player initialization
		if(newPlayer->pPlayer->accountID
			&& newPlayer->pPlayer->pPlayerAccountData
			&& newPlayer->pPlayer->pPlayerAccountData->iAccountID != newPlayer->pPlayer->accountID)
		{
			gslGAD_tr_SetReference(ATR_EMPTY_ARGS, newPlayer);
		}
	}

	if (newPlayer->pChar)
	{
		newPlayer->pChar->uiTimeLoggedOutForCombat = uiTimeOffline;
	}

	if (locationType != GLOBALTYPE_GAMESERVER)
	{
		// We're coming from something other than a gameserver, so run initial login stuff
		newPlayer->pPlayer->bIsFirstSessionLogin = 1;
	}

	PlayerLoginFixup(newPlayer);

	return TRANSACTION_OUTCOME_SUCCESS;
}

void DEFAULT_LATELINK_PetLoginFixup(NOCONST(Entity) *newPlayer)
{

}


// Called when entity enters map for the first time
enumTransactionOutcome PetLoginCB(ATR_ARGS, NOCONST(Entity) *newPet, NOCONST(Entity) *backupPet, GlobalType locationType, ContainerID locationID)
{
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, NULL, "LoginFixup", "Pet");

	inv_ent_FixItemIDs(newPet);
	inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS, newPet, false, false, &reason);

	if (newPet->pChar)
		character_LoadTransact(newPet);
	PetLoginFixup(newPet);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Skilltype, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Eskillspecialization, .Psaved.Ugamespecificfixupversion, pInventoryV2.ppLiteBags[], .Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(pSavedPet, ".Psaved.Pscpdata, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pchar.Hspecies, .Psaved.Costumedata.Iactivecostume, .Psaved.Pptrayelems_Obsolete, .Psaved.Ppautoattackelems_Obsolete, .Psaved.Pppreferredpetids, .Pchar.Hclass, .Pchar.Ilevelexp, .Pinventoryv2, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Pppowertrees, .Pchar.Ppassignedstats, .Pchar.Pppowersclass, .Pchar.Pppowerspersonal, .Psaved.Ugamespecificfixupversion, .Psaved.Costumedata.Pcslotset, .Psaved.Costumedata.Islotsetversion, .Psaved.Costumedata.Eacostumeslots, .Pplayer.Playertype");
enumTransactionOutcome SavedPetFixup(ATR_ARGS, NOCONST(Entity) *pOwner, NOCONST(Entity) *pSavedPet, int bIsPuppet, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	SavedPetFixupHelper(pOwner, pSavedPet, bIsPuppet, pReason, pExtract, true);
	return TRANSACTION_OUTCOME_SUCCESS;
}

void entObjTeamCB(Container *con, ObjectPathOperation **operations)
{
	Entity *pEnt = con->containerData;
	if(pEnt)
	{
		if(!team_IsWithTeam(pEnt))
		{
			Team* pTeam = pEnt->pTeam ? GET_REF(pEnt->pTeam->hTeam) : NULL;
			gslPetTeamList_LeaveTeam(pEnt, pTeam);
		}
		
		if(pEnt->pTeam)
			pEnt->pTeam->bUpdateTeamPowers = true;

		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_NONE);
	}
}

void entObjAccessLevelCB(Container *con, ObjectPathOperation **operations)
{
	Entity *pEnt = con->containerData;
	if(pEnt)
	{
		ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);
	}
}

AUTO_STARTUP(Schemas) ASTRT_DEPS(AS_CharacterAttribs, AS_AttribSets, ItemQualities, InventoryBagIDs, ItemTags, AS_ActivityLogEntryTypes, UsageRestrictionCategories, AS_ControlSchemeRegions, RewardGatedLoad, GroupProjectTaskSlotTypes);
void gslTransactionInit(void)
{
	loadstart_printf("Initializing Transaction System...");

	// Stuff for dealing with the general entity backup stuff
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjBackupCommitCB,"*",false,false,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjBackupCommitCB,"*",false,false,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPUPPET,entObjBackupCommitCB,"*",false,false,false,NULL);
	RegisterFastLocalCopyCB(GLOBALTYPE_ENTITYPLAYER, entFromContainerIDAnyPartition, entObjGetAutoTransBackup);
	RegisterFastLocalCopyCB(GLOBALTYPE_ENTITYSAVEDPET, entFromContainerIDAnyPartition, entObjGetAutoTransBackup);
	RegisterFastLocalCopyCB(GLOBALTYPE_ENTITYPUPPET, entFromContainerIDAnyPartition, entObjGetAutoTransBackup);
//	RegisterFastLocalCopyCB(GLOBALTYPE_ENTITYPLAYER, entObjGetBackup, entObjGetAutoTransBackup);
//	RegisterFastLocalCopyCB(GLOBALTYPE_ENTITYSAVEDPET, entObjGetBackup, entObjGetAutoTransBackup);

	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjTransformToPuppet_PostAddQueuedPet,".pSaved.pPuppetMaster.ppPuppets[*].curID",false,false,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjTransformToPuppet_PostAddQueuedPet,".pSaved.ppOwnedContainers[*].conID",false,false,false,NULL);

	// Dirty bits on container modification
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER, entObjSavedDirtyBit, ".pSaved.*", true, false, false, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER, entObjPlayerDirtyBit, ".pPlayer.*", true, false, false, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER, entObjCharacterDirtyBit, ".pChar.*", true, false, false, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER, entObjEntityDirtyBit, "*", true, false, false, entRootOnlyCB);

	// When a player's build changes
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER, entObjBuildsChangePostCB, ".pSaved.ppBuilds*", false, false, false, NULL);

	// When the player has pending game account changes
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER, entObjPendingGameAccountCB, ".pPlayer.pPlayerAccountData.eaPendingKeys*", false, false, false, NULL);

	// Special case callbacks
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixPowersPersonalPostCB,".pChar.ppPowersPersonal",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjFixPowersPersonalPostCB,".pChar.ppPowersPersonal",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixPowersClassPostCB,".pChar.ppPowersClass",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjFixPowersClassPostCB,".pChar.ppPowersClass",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixPowersTreePreCB,".pChar.ppPowerTrees*",false,false,true,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjFixPowersTreePreCB,".pChar.ppPowerTrees*",false,false,true,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixPowersTreePostCB,".pChar.ppPowerTrees*",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjFixPowersTreePostCB,".pChar.ppPowerTrees*",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixPowersStatsPostCB,".pChar.ppAssignedStats*",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjCharacterSanityCB,".pChar.*",false,false,false,NULL);

	// When the player joins or leaves a team or guild
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjTeamCB,".pTeam.iTeamID",true,false,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjTeamCB,".pTeam.eState",true,false,false,NULL);

	// When Access level changes
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjAccessLevelCB,".pPlayer.AccessLevel",true,false,false,NULL);

	//register callback for any time entity inventory changes
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjInventoryChangeNumericPostCB,".pInventoryV2.ppLiteBags*",true,false,false,entObjInventoryChangeNumericPostCBFilter);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjInventoryChangeNumericPostCB,".pInventoryV2.ppLiteBags*",true,false,false,entObjInventoryChangeNumericPostCBFilter);
	//IF YOU CHANGE THESE COMMMIT MATCH STRINGS, ALSO CHANGE THE GUTS OF entObjInventoryChangeNonNumericPostCB().
	//  It makes assumptions about the operation strings it receives because 
	//  they must first have matched these strings.
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjInventoryChangePostCB,".pInventoryV2*",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjInventoryChangeNonNumericPostCB,".pInventoryV2.ppInventoryBags*",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYSAVEDPET,entObjInventoryChangeNonNumericPostCB,".pInventoryV2.ppInventoryBags*",false,true,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjInventoryChangeLevelNumericPostCB,".pInventoryV2*",true,false,false,entObjInventoryChangeLevelNumericPostCBFilter);
	
	// Register callback for any time the player's guild or team changes
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjGuildChangePostCB,".pPlayer.pGuild*",true,false,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjTeamChangePostCB,".pTeam*",true,false,false,NULL);
	
	// Register callback for any time the player's nemeses change
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjNemesesChangePostCB,".pPlayer.nemesisInfo*",false,false,false,NULL);

	// Register Mission callbacks
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixMissionInfoPreCB,".pPlayer.missionInfo",false,false,true,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixMissionInfoPostCB,".pPlayer.missionInfo",false,false,false,NULL);

	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixMissionPreCB,".pPlayer.missionInfo.missions",false,false,true,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixMissionPreCB,".pPlayer.missionInfo.missions*.children",false,false,true,NULL);

	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixMissionPostCB,".pPlayer.missionInfo.missions",false,false,false,NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjFixMissionPostCB,".pPlayer.missionInfo.missions*.children",false,false,false,NULL);

	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjMissionChangePostCB,".pPlayer.missionInfo.missions[*].*",false,false,false,NULL);
	
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_ENTITYPLAYER,entObjCompletedMissionChangePostCB,".pPlayer.missionInfo.completedMissions[*].*",true,false,false,NULL);
		
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_RETURN_CONTAINER_TO, PlayerLogoutCB);
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_RECEIVE_CONTAINER_FROM, PlayerLoginCB);

	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYSAVEDPET, TRANSACTION_RETURN_CONTAINER_TO, PetLogoutCB);
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYSAVEDPET, TRANSACTION_RECEIVE_CONTAINER_FROM, PetLoginCB);

	if (!isProductionMode())
	{	
		loadstart_printf("Writing out schema files... ");
		objExportNativeSchemas();
		loadend_printf("Done.");
	}

	objLoadAllGenericSchemas();

	loadend_printf("done.");
}

void gslGetPlayerIDFromNameWithRestore(const char* name, U32 iVirtualShardID, TransactionReturnCallback GetPlayerIDFromNameAndVShard_CB, void *userData)
{
	RemoteCommand_dbIDFromPlayerReferenceWithRestore(objCreateManagedReturnVal(GetPlayerIDFromNameAndVShard_CB, userData),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, iVirtualShardID);
	
}

enumTransactionOutcome gslGetPlayerIDFromNameWithRestoreReturn(TransactionReturnVal *returnVal, ContainerID *returnID)
{
	if (returnVal && returnID)
		return RemoteCommandCheck_dbIDFromPlayerReferenceWithRestore(returnVal, returnID);
	return TRANSACTION_OUTCOME_FAILURE;
}

void gslGetPlayerIDFromName(const char* name, U32 iVirtualShardID, TransactionReturnCallback GetPlayerIDFromNameAndVShard_CB, void *userData)
{
	RemoteCommand_dbIDFromPlayerReference(objCreateManagedReturnVal(GetPlayerIDFromNameAndVShard_CB, userData),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, iVirtualShardID);
	
}

enumTransactionOutcome gslGetPlayerIDFromNameReturn(TransactionReturnVal *returnVal, ContainerID *returnID)
{
	if (returnVal && returnID)
		return RemoteCommandCheck_dbIDFromPlayerReference(returnVal, returnID);
	return TRANSACTION_OUTCOME_FAILURE;
}

void gslGetPetIDFromName(const char* name, ContainerID iVirtualShardID,  TransactionReturnCallback GetPetIDFromName_CB, void *userData)
{
	RemoteCommand_dbIDFromPetReference(objCreateManagedReturnVal(GetPetIDFromName_CB, userData),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, iVirtualShardID);
}

enumTransactionOutcome gslGetPetIDFromNameReturn(TransactionReturnVal *returnVal, ContainerID *returnID)
{
	if (returnVal && returnID)
		return RemoteCommandCheck_dbIDFromPetReference(returnVal, returnID);
	return TRANSACTION_OUTCOME_FAILURE;
}


void gslGetContainerOwnerFromName(const char* name, ContainerID iVirtualShardID,  TransactionReturnCallback GetContainerOwnerFromName_CB, void *userData)
{
	RemoteCommand_dbContainerOwnerFromPlayerRef(objCreateManagedReturnVal(GetContainerOwnerFromName_CB, userData),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, iVirtualShardID);
}

enumTransactionOutcome gslGetContainerOwnerFromNameReturn(TransactionReturnVal *returnVal, char **owner)
{
	if (returnVal && owner)
		return RemoteCommandCheck_dbContainerOwnerFromPlayerRef(returnVal, owner);
	return TRANSACTION_OUTCOME_FAILURE;
}

void gslGetPlayerNameFromID(ContainerID id, TransactionReturnCallback GetPlayerIDFromName_CB, void *userData)
{	
	RemoteCommand_dbNameFromID(objCreateManagedReturnVal(GetPlayerIDFromName_CB, userData),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, id);
}

enumTransactionOutcome gslGetPlayerNameFromIDReturn(TransactionReturnVal *returnVal, char **returnName)
{
	if (returnVal && returnName)
		return RemoteCommandCheck_dbNameFromID(returnVal, returnName);
	return TRANSACTION_OUTCOME_FAILURE;
}
void gslGetAccountIDFromName(const char* name, ContainerID iVirtualShardID, TransactionReturnCallback GetPlayerIDFromName_CB, void *userData)
{
	RemoteCommand_dbAccountIDFromPlayerReference(objCreateManagedReturnVal(GetPlayerIDFromName_CB, userData),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, iVirtualShardID);
	
}

enumTransactionOutcome gslGetAccountIDFromNameReturn(TransactionReturnVal *returnVal, U32 *returnID)
{
	if (returnVal && returnID)
		return RemoteCommandCheck_dbAccountIDFromPlayerReference(returnVal, returnID);
	return TRANSACTION_OUTCOME_FAILURE;
}

void gslFindPlayers(PlayerFindFilterStruct *pFilters, TransactionReturnCallback FindPlayers_CB, void *userData)
{
	RemoteCommand_ChatServerGetOnlinePlayers(objCreateManagedReturnVal(FindPlayers_CB, userData), 
		GLOBALTYPE_CHATSERVER, 0, pFilters);
}

enumTransactionOutcome gslFindPlayersReturn(TransactionReturnVal *returnVal, ChatPlayerList **pList)
{
	if(returnVal && pList)
		return RemoteCommandCheck_ChatServerGetOnlinePlayers(returnVal, pList);
	return TRANSACTION_OUTCOME_FAILURE;
}

void gslFindTeams(PlayerFindFilterStruct *pFilters, TransactionReturnCallback FindPlayers_CB, void *userData)
{
	RemoteCommand_ChatServerGetTeamsToJoin(objCreateManagedReturnVal(FindPlayers_CB, userData), 
		GLOBALTYPE_CHATSERVER, 0, pFilters);
}

enumTransactionOutcome gslFindTeamsReturn(TransactionReturnVal *returnVal, ChatTeamToJoinList **pList)
{
	if(returnVal && pList)
		return RemoteCommandCheck_ChatServerGetTeamsToJoin(returnVal, pList);
	return TRANSACTION_OUTCOME_FAILURE;
}


AUTO_TRANS_HELPER;
void trhLockEntireEnt(ATR_ARGS, ATH_ARG ATR_ALLOW_FULL_LOCK NOCONST(Entity)* pEnt)
{

	//entGetContainerID((Entity *)pEnt);
}


// This is just for testing
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Psaved.Conowner.Containerid, .Pplayer.Missioninfo.Missions[AO], pPlayer.missionInfo.missions[]");
enumTransactionOutcome trTestTransactionPerf(ATR_ARGS, NOCONST(Entity)* ent, const char *pchMissionName)
{
	NOCONST(Mission)* mission = NULL;
	MissionDef* missionDef = NULL;

	trhLockEntireEnt(ATR_PASS_ARGS, ent);
	ent->pSaved->conOwner.containerID++;

	// Get the Mission
	mission = eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchMissionName);

	if (!mission)
	{
		mission = StructCreateNoConst(parse_Mission);
		mission->missionNameOrig = allocAddString(pchMissionName);
		eaIndexedAdd(&ent->pPlayer->missionInfo->missions, mission);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Updated");
}

AUTO_COMMAND;
void TestTransactionPerf(Entity *pEnt)
{
	int i;
	F32 timeElapsed;
	static int timer = -1;
	if (timer == -1)
		timer = timerAlloc();
	timerStart(timer);

	for (i = 0; i < 100; i++)
	{
		AutoTrans_trTestTransactionPerf(NULL,
			GetAppGlobalType(),
			entGetType(pEnt),
			entGetContainerID(pEnt),
			"badmissionname");		
	}
	timeElapsed = timerElapsed(timer);
	timerPause(timer);

	printf("Test Transaction perf took %f seconds with result\n",timeElapsed);
	
}

AUTO_COMMAND_REMOTE;
void LockGameServerRemotely(bool bLock)
{
	gGSLState.bLocked = bLock;

}

///////////////////////////////////////////////////////////////////////////////////////////
// Resolving player handles
///////////////////////////////////////////////////////////////////////////////////////////

typedef struct GSLResolveHandleCBData
{
	EntityRef iRef;
	ResolveHandleCallback pSuccessCallback;
	ResolveHandleCallback pFailureCallback;
	void *pCallbackData;
} GSLResolveHandleCBData;

static void gslPlayerResolveHandle_CB(TransactionReturnVal *pReturn, GSLResolveHandleCBData *pData) {
	ContainerID uiPlayerID;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameReturn(pReturn, &uiPlayerID);
	Entity *pEnt = entFromEntityRefAnyPartition(pData->iRef);

	if (!pEnt || eOutcome != TRANSACTION_OUTCOME_SUCCESS || !uiPlayerID) 
	{
		if (pData->pFailureCallback)
		{
			pData->pFailureCallback(pEnt, 0, 0, 0, pData->pCallbackData);
		}
	}
	else
	{
		if (pData->pSuccessCallback)
		{
			pData->pSuccessCallback(pEnt, uiPlayerID, 0, 0, pData->pCallbackData);
		}
	}
	SAFE_FREE(pData);
}

static GSLResolveHandleCBData *gslPlayerMakeCBData(	EntityRef iRef,
													ResolveHandleCallback pSuccessCallback, 
													ResolveHandleCallback pFailureCallback,
													void* pCallbackData)
{
	GSLResolveHandleCBData *pData = malloc(sizeof(GSLResolveHandleCBData));
	pData->iRef = iRef;
	pData->pSuccessCallback = pSuccessCallback;
	pData->pFailureCallback = pFailureCallback;
	pData->pCallbackData = pCallbackData;
	return pData;
}

void gslPlayerResolveHandle(	Entity *pEnt, const char *pcInputName, 
								ResolveHandleCallback pSuccessCallback, 
								ResolveHandleCallback pFailureCallback, 
								void* pCallbackData)
{
	char *pcResolvedName = NULL;
	ContainerID iResolvedID = 0;
	U32 uiResolvedAccountID = 0;
	U32 uiResolvedLoginServerID = 0;
	ContainerID uiTeamID = 0;
	
	if (ResolveKnownAccountID(pEnt, pcInputName, &pcResolvedName, NULL, &iResolvedID, &uiResolvedAccountID, &uiResolvedLoginServerID) != kEntityResolve_Ambiguous) 
	{
		GSLResolveHandleCBData *pData;
		if (iResolvedID) 
		{
			pSuccessCallback( pEnt, iResolvedID, uiResolvedAccountID, uiResolvedLoginServerID, pCallbackData );
			return;
		} 
		else if (pcResolvedName && pcResolvedName[0]) 
		{
			pData = gslPlayerMakeCBData(pEnt->myRef, pSuccessCallback, pFailureCallback, pCallbackData);
			gslGetPlayerIDFromName(pcResolvedName, entGetVirtualShardID(pEnt), gslPlayerResolveHandle_CB, pData);
			return;
		} 
		else if (strchr(pcInputName, '@')) 
		{
			pData = gslPlayerMakeCBData(pEnt->myRef, pSuccessCallback, pFailureCallback, pCallbackData);
			gslGetPlayerIDFromName(pcInputName, entGetVirtualShardID(pEnt), gslPlayerResolveHandle_CB, pData);
			return;
		}
	}
	
	if ( pFailureCallback ) 
	{
		pFailureCallback( pEnt, 0, 0, 0, pCallbackData );
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Bautojointeamvoicechat");
enumTransactionOutcome trSetAutoJoinTeamVoiceChatFlag(ATR_ARGS, NOCONST(Entity) *pEnt, U32 bAutoJoinTeamVoiceChat)
{
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		TRANSACTION_RETURN_LOG_FAILURE("AutoJoinTeamVoiceChat flag is not saved, not a valid character.");

	pEnt->pPlayer->bAutoJoinTeamVoiceChat = bAutoJoinTeamVoiceChat;

	TRANSACTION_RETURN_LOG_SUCCESS("AutoJoinTeamVoiceChat flag for the player is saved.");
}