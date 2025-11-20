#include "../../crossroads/appServerLib/UGCDataManager/aslUGCDataManager.h"
#include "EntDebugMenu.h"
#include "EntityLib.h"
#include "GameServerLib.h"
#include "EntityIterator.h"
#include "EntitySavedData.h"
#include "gslChat.h"
#include "gslCommandParse.h"
#include "gslTransactions.h"
#include "gslMapTransfer.h"
#include "logging.h"
#include "gslMission_transact.h"
#include "mission_common.h"
#include "gslMission.h"
#include "Player.h"
#include "ugcProjectCommon.h"
#include "stringUtil.h"
#include "ugcCommon.h"
#include "UGCProjectUtils.h"
#include "gslPartition.h"
#include "gslUGC_cmd.h"
#include "GameAccountData/GameAccountData.h"
#include "utilitiesLib.h"

#include "file.h"
#include "ServerLib.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "Autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

// How close players have to be to show up in the debug menu for certain CSR commands.
#define CSRMENU_PLAYER_DIST 100

// --------------------------------------------------------------------------
// Debug Menu Code
// --------------------------------------------------------------------------

typedef struct CSRTransactionReturnStruct
{
	char* missionName;
	U32 csrEntRef;
}CSRTransactionReturnStruct;

typedef void (*CSRDebugMenuAddCommandFunc)(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *root);

void csrdebug_CreateDebugMenu(Entity* playerEnt, DebugMenuItem* groupRoot);
static void csrdebug_AddCommandsForEachPlayer(Entity *pCsrEnt, Entity*** pppPlayerEnts, DebugMenuItem* root, CSRDebugMenuAddCommandFunc addItemFunc);
static void csrdebug_AddCompleteMissionCommands(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group);
static void csrdebug_AddMoveToPlayerCommand(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group);
static void csrdebug_AddSummonPlayerCommand(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group);
static void csrdebug_AddDropMissionCommands(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group);

AUTO_RUN;
void csrdebug_RegisterDebugMenu(void)
{
	debugmenu_RegisterNewGroup("CSR Commands", csrdebug_CreateDebugMenu);
}

void csrdebug_CreateDebugMenu(Entity* playerEnt, DebugMenuItem* groupRoot)
{
	DebugMenuItem *menuGroup;
	DebugMenuItem *menuItem;
	Entity** ppPlayers = NULL;
	Entity *currEnt = NULL;
	EntityIterator *entIter;

	PERFINFO_AUTO_START_FUNC();

	// Get a list of all players on the map
	entIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((currEnt = EntityIteratorGetNext(entIter)))
	{
		if (currEnt != playerEnt) 
			eaPush(&ppPlayers, currEnt);
	}
	EntityIteratorRelease(entIter);

	// Warp to a Player
	menuGroup = debugmenu_AddNewCommandGroup(groupRoot, "Move to player", "MoveToPlayer <playerName>", false);
	csrdebug_AddCommandsForEachPlayer(playerEnt, &ppPlayers, menuGroup, csrdebug_AddMoveToPlayerCommand);

	menuGroup = debugmenu_AddNewCommandGroup(groupRoot, "Summon player", "SummonPlayer <playerName>", false);
	csrdebug_AddCommandsForEachPlayer(playerEnt, &ppPlayers, menuGroup, csrdebug_AddSummonPlayerCommand);

	// -- Missions --
	PERFINFO_AUTO_START("per-Player Missions", 1);
	menuGroup = debugmenu_AddNewCommandGroup(groupRoot, "Missions", "CSR Commands for the Mission System", false);
	menuItem = debugmenu_AddNewCommandGroup(menuGroup, "Complete Mission/Task For Player", "Auto-complete missions or sub-tasks on a player.", false);
	csrdebug_AddCommandsForEachPlayer(playerEnt, &ppPlayers, menuItem, csrdebug_AddCompleteMissionCommands);

	menuItem = debugmenu_AddNewCommandGroup(menuGroup, "Drop Mission For Player", "Drop one of the player's missions.", false);
	csrdebug_AddCommandsForEachPlayer(playerEnt, &ppPlayers, menuItem, csrdebug_AddDropMissionCommands);
	PERFINFO_AUTO_STOP();

	eaDestroy(&ppPlayers);

	PERFINFO_AUTO_STOP();
}

static int entity_SortByPlayerName(const Entity** pEntA, const Entity** pEntB)
{
	if (!(*pEntA) || !(*pEntB))
		return (!!(*pEntA) - !!(*pEntB));
	if (!(*pEntA)->pPlayer || !(*pEntB)->pPlayer)
		return (!!(*pEntA)->pSaved - !!(*pEntB)->pSaved);

	return stricmp((*pEntA)->pSaved->savedName, (*pEntB)->pSaved->savedName);
}

// This will automatically group players and everything to make the menu as useable as possible
static void csrdebug_AddCommandsForEachPlayer(Entity *pCsrEnt, Entity*** pppPlayerEnts, DebugMenuItem* root, CSRDebugMenuAddCommandFunc addItemFunc)
{
	DebugMenuItem *groupItem = NULL, *menuItem = NULL;
	int i, n = eaSize(pppPlayerEnts);
	int iGroupEndIndex = 0;
	char buf[1024];

	eaQSort((*pppPlayerEnts), entity_SortByPlayerName);

	for(i=0; i<n; i++)
	{
		Entity *pCurrentEnt = (*pppPlayerEnts)[i];
		if (pCurrentEnt->pPlayer && pCurrentEnt->pSaved)
		{
			// Make a new subgroup if needed
			if (eaSize(pppPlayerEnts) > 10 && (i > iGroupEndIndex || i == 0))
			{
				iGroupEndIndex = MIN(iGroupEndIndex+10, eaSize(pppPlayerEnts)-1);
				sprintf(buf, "%s - %s", pCurrentEnt->pSaved->savedName, (*pppPlayerEnts)[iGroupEndIndex]->pSaved->savedName);
				groupItem = debugmenu_AddNewCommandGroup(root, buf, "See players in this range", false);
			}
			
			// Create menu item
			addItemFunc(pCsrEnt, pCurrentEnt, groupItem?groupItem:root);
		}
	}
}

static void csrdebug_AddMoveToPlayerCommand(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group)
{
	char playerNameBuf[1024];
	char buf[1024];
	sprintf(playerNameBuf, "%s (%s)", pPlayerEnt->pSaved->savedName, pPlayerEnt->pPlayer->privateAccountName);
	sprintf(buf, "movetoplayer \"%s\"", pPlayerEnt->pSaved->savedName);
	debugmenu_AddNewCommand(group, playerNameBuf, buf);
}

static void csrdebug_AddSummonPlayerCommand(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group)
{
	char playerNameBuf[1024];
	char buf[1024];
	sprintf(playerNameBuf, "%s (%s)", pPlayerEnt->pSaved->savedName, pPlayerEnt->pPlayer->privateAccountName);
	sprintf(buf, "summonplayer \"%s\"", pPlayerEnt->pSaved->savedName);
	debugmenu_AddNewCommand(group, playerNameBuf, buf);
}

static void csrdebug_AddMissionCompleteRecursive(DebugMenuItem *menuItem, Entity *pPlayerEnt, Mission *mission, int depth)
{
	MissionDef* currDef = mission_GetDef(mission);
	char cmdStr[512];
	static char *tmpStr = NULL;
	int i, n;

	if (currDef && (mission->state == MissionState_InProgress || mission->openChildren))
	{
		estrClear(&tmpStr);
		for (i = 0; i < depth; i++)
			estrAppend2(&tmpStr, "  ");
		estrAppend2(&tmpStr, GET_REF(currDef->uiStringMsg.hMessage)?TranslateMessageRef(currDef->uiStringMsg.hMessage):currDef->name); // TODO - Use display names?
		sprintf(cmdStr, "missioncompleteother \"%s\" \"%s\"", currDef->name, pPlayerEnt->pSaved->savedName);
		debugmenu_AddNewCommand(menuItem, tmpStr, cmdStr);
		
		n = eaSize(&mission->children);
		for (i = 0; i < n; i++)
			csrdebug_AddMissionCompleteRecursive(menuItem, pPlayerEnt, mission->children[i], depth+1);
	}
}

static void csrdebug_AddCompleteMissionCommands(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group)
{
	DebugMenuItem *playerGroup = NULL;
	DebugMenuItem *missionGroup = NULL;
	MissionInfo* missionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	char buf[1024];
	
	if (pPlayerEnt->pPlayer)
	{
		sprintf(buf, "%s (%s)", pPlayerEnt->pSaved->savedName, pPlayerEnt->pPlayer->privateAccountName);
		playerGroup = debugmenu_AddNewCommandGroup(group, buf, "Complete a mission or sub-task for this player", false);
		if (missionInfo)
		{
			DebugMenuItem *perkMenuGroup;
			int i, n = eaSize(&missionInfo->missions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->missions[i]);
				if (def && (def->missionType == MissionType_Normal
					|| def->missionType == MissionType_Nemesis 
					|| def->missionType == MissionType_NemesisArc 
					|| def->missionType == MissionType_NemesisSubArc 
					|| def->missionType == MissionType_Episode 
					|| def->missionType == MissionType_TourOfDuty
					|| def->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes

				{
					sprintf(buf, "%s", GET_REF(def->displayNameMsg.hMessage)?TranslateMessageRef(def->displayNameMsg.hMessage):def->name);
					missionGroup = debugmenu_AddNewCommandGroup(playerGroup, buf, "Complete all or part of this mission", false);
					csrdebug_AddMissionCompleteRecursive(missionGroup, pPlayerEnt, missionInfo->missions[i], 0);
				}
			}
			n = eaSize(&missionInfo->eaNonPersistedMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaNonPersistedMissions[i]);
				if (def && (def->missionType == MissionType_Normal
					|| def->missionType == MissionType_Nemesis 
					|| def->missionType == MissionType_NemesisArc
					|| def->missionType == MissionType_NemesisSubArc
					|| def->missionType == MissionType_Episode 
					|| def->missionType == MissionType_TourOfDuty
					|| def->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes

				{
					sprintf(buf, "%s", GET_REF(def->displayNameMsg.hMessage)?TranslateMessageRef(def->displayNameMsg.hMessage):def->name);
					missionGroup = debugmenu_AddNewCommandGroup(playerGroup, buf, "Complete all or part of this mission", false);
					csrdebug_AddMissionCompleteRecursive(missionGroup, pPlayerEnt, missionInfo->eaNonPersistedMissions[i], 0);
				}
			}
			n = eaSize(&missionInfo->eaDiscoveredMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaDiscoveredMissions[i]);
				if (def && (def->missionType == MissionType_Normal
					|| def->missionType == MissionType_Nemesis 
					|| def->missionType == MissionType_NemesisArc
					|| def->missionType == MissionType_NemesisSubArc
					|| def->missionType == MissionType_Episode 
					|| def->missionType == MissionType_TourOfDuty
					|| def->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes

				{
					sprintf(buf, "%s", GET_REF(def->displayNameMsg.hMessage)?TranslateMessageRef(def->displayNameMsg.hMessage):def->name);
					missionGroup = debugmenu_AddNewCommandGroup(playerGroup, buf, "Complete all or part of this mission", false);
					csrdebug_AddMissionCompleteRecursive(missionGroup, pPlayerEnt, missionInfo->eaDiscoveredMissions[i], 0);
				}
			}

			perkMenuGroup = debugmenu_AddNewCommandGroup(playerGroup, "Perks", "Complete a mission or sub-task for Perk missions", false);
			n = eaSize(&missionInfo->missions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->missions[i]);
				if(def && def->missionType == MissionType_Perk)
				{
					sprintf(buf, "%s", GET_REF(def->displayNameMsg.hMessage)?TranslateMessageRef(def->displayNameMsg.hMessage):def->name);
					missionGroup = debugmenu_AddNewCommandGroup(perkMenuGroup, buf, "Complete all or part of this perk", false);
					csrdebug_AddMissionCompleteRecursive(missionGroup, pPlayerEnt, missionInfo->missions[i], 0);
				}
			}
			n = eaSize(&missionInfo->eaNonPersistedMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaNonPersistedMissions[i]);
				if(def && def->missionType == MissionType_Perk)
				{
					sprintf(buf, "%s", GET_REF(def->displayNameMsg.hMessage)?TranslateMessageRef(def->displayNameMsg.hMessage):def->name);
					missionGroup = debugmenu_AddNewCommandGroup(perkMenuGroup, buf, "Complete all or part of this perk", false);
					csrdebug_AddMissionCompleteRecursive(missionGroup, pPlayerEnt, missionInfo->eaNonPersistedMissions[i], 0);
				}
			}
			n = eaSize(&missionInfo->eaDiscoveredMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaDiscoveredMissions[i]);
				if(def && def->missionType == MissionType_Perk)
				{
					sprintf(buf, "%s", GET_REF(def->displayNameMsg.hMessage)?TranslateMessageRef(def->displayNameMsg.hMessage):def->name);
					missionGroup = debugmenu_AddNewCommandGroup(perkMenuGroup, buf, "Complete all or part of this perk", false);
					csrdebug_AddMissionCompleteRecursive(missionGroup, pPlayerEnt, missionInfo->eaDiscoveredMissions[i], 0);
				}
			}
		}
	}
}

static void csrdebug_AddDropMissionCommand(DebugMenuItem *menuItem, Entity *pPlayerEnt, Mission *mission, int depth)
{
	MissionDef* currDef = mission_GetDef(mission);
	char cmdStr[512];
	static char *tmpStr = NULL;
	int i;

	if (currDef)
	{
		estrClear(&tmpStr);
		for (i = 0; i < depth; i++)
			estrAppend2(&tmpStr, "  ");
		estrAppend2(&tmpStr, GET_REF(currDef->displayNameMsg.hMessage)?TranslateMessageRef(currDef->displayNameMsg.hMessage):currDef->name);
		sprintf(cmdStr, "missiondropother \"%s\" \"%s\"", currDef->name, pPlayerEnt->pSaved->savedName);
		debugmenu_AddNewCommand(menuItem, tmpStr, cmdStr);
	}
}


static void csrdebug_AddDropMissionCommands(Entity *pCsrEnt, Entity *pPlayerEnt, DebugMenuItem *group)
{
	DebugMenuItem *playerGroup = NULL;
	MissionInfo* missionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	char buf[1024];
	
	if (pPlayerEnt->pPlayer)
	{
		sprintf(buf, "%s (%s)", pPlayerEnt->pSaved->savedName, pPlayerEnt->pPlayer->privateAccountName);
		playerGroup = debugmenu_AddNewCommandGroup(group, buf, "Drop a mission for this player", false);
		if (missionInfo)
		{
			DebugMenuItem *perkMenuGroup;
			int i, n = eaSize(&missionInfo->missions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->missions[i]);
				if (def && (def->missionType == MissionType_Normal
					|| def->missionType == MissionType_Nemesis 
					|| def->missionType == MissionType_NemesisArc
					|| def->missionType == MissionType_NemesisSubArc
					|| def->missionType == MissionType_Episode 
					|| def->missionType == MissionType_TourOfDuty
					|| def->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
				{				
					csrdebug_AddDropMissionCommand(playerGroup, pPlayerEnt, missionInfo->missions[i], 0);
				}
			}

			n = eaSize(&missionInfo->eaNonPersistedMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaNonPersistedMissions[i]);
				if (def && (def->missionType == MissionType_Normal
					|| def->missionType == MissionType_Nemesis 
					|| def->missionType == MissionType_NemesisArc
					|| def->missionType == MissionType_NemesisSubArc
					|| def->missionType == MissionType_Episode 
					|| def->missionType == MissionType_TourOfDuty
					|| def->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
				{
					csrdebug_AddDropMissionCommand(playerGroup, pPlayerEnt, missionInfo->eaNonPersistedMissions[i], 0);
				}
			}

			n = eaSize(&missionInfo->eaDiscoveredMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaDiscoveredMissions[i]);
				if (def && (def->missionType == MissionType_Normal
					|| def->missionType == MissionType_Nemesis 
					|| def->missionType == MissionType_NemesisArc
					|| def->missionType == MissionType_NemesisSubArc
					|| def->missionType == MissionType_Episode 
					|| def->missionType == MissionType_TourOfDuty
					|| def->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
				{
					csrdebug_AddDropMissionCommand(playerGroup, pPlayerEnt, missionInfo->eaDiscoveredMissions[i], 0);
				}
			}

			perkMenuGroup = debugmenu_AddNewCommandGroup(playerGroup, "Perks", "Drop a Perk mission for this player", false);
			n = eaSize(&missionInfo->missions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->missions[i]);
				if(def && def->missionType == MissionType_Perk)
					csrdebug_AddDropMissionCommand(perkMenuGroup, pPlayerEnt, missionInfo->missions[i], 0);
			}

			n = eaSize(&missionInfo->eaNonPersistedMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaNonPersistedMissions[i]);
				if(def && def->missionType == MissionType_Perk)
					csrdebug_AddDropMissionCommand(perkMenuGroup, pPlayerEnt, missionInfo->eaNonPersistedMissions[i], 0);
			}

			n = eaSize(&missionInfo->eaDiscoveredMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = mission_GetDef(missionInfo->eaDiscoveredMissions[i]);
				if(def && def->missionType == MissionType_Perk)
					csrdebug_AddDropMissionCommand(perkMenuGroup, pPlayerEnt, missionInfo->eaDiscoveredMissions[i], 0);
			}
		}
	}
}

static void CSRReturnDialogEnt(const char* title, const char* text, Entity *pCsrEnt)
{
	if(pCsrEnt)
	{
		ClientCmd_GameDialogGenericMessage(pCsrEnt, title, text);
	}
}

static void CSRReturnDialog(const char* title, const char* text, EntityRef csrEntRef)
{
	CSRReturnDialogEnt(title, text, entFromEntityRefAnyPartition(csrEntRef));
}

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------
// TODO - Not sure what the access level of these should be.  For now they are all 2-3.
// TODO #2 - Player name may not be the correct way to specify a player.

// -------------------------
// Move/Warp commands
// -------------------------

void ServerAdmin_CSRFindPlayerTransactionReturn(TransactionReturnVal *returnVal, EntityRef *pCsrEntRef)
{
	static char *owner = 0;
	enumTransactionOutcome eOutcome = gslGetContainerOwnerFromNameReturn(returnVal, &owner);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && owner)
	{
		CSRReturnDialog("Found Player:", owner, *pCsrEntRef);
	}
	else if (pCsrEntRef)
	{
		CSRReturnDialog("Failure", "Couldn't find player!", *pCsrEntRef);
	}
	
}

// Get the name of the map this player is on
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(FindPlayer);
void ServerAdmin_CSRFindPlayer(Entity *pEnt, char *playerName)
{
	if (pEnt && playerName)
	{
		EntityRef *myRef = malloc(sizeof(EntityRef));
		*myRef = pEnt->myRef;
		gslGetContainerOwnerFromName(playerName, entGetVirtualShardID(pEnt), ServerAdmin_CSRFindPlayerTransactionReturn, myRef);
	}
}

void ServerAdmin_CSRMoveToPlayerTransactionReturn(TransactionReturnVal *returnVal, EntityRef *pCsrEntRef)
{
	ContainerID playerID = 0;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &playerID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && playerID)
	{
		RemoteCommand_GetDestinationForCSRMove(GLOBALTYPE_ENTITYPLAYER, playerID, playerID, GetAppGlobalType(), GetAppGlobalID(), *pCsrEntRef);
	}
	else
	{
		if(pCsrEntRef)
		{
			CSRReturnDialog("Failure", "Couldn't find player!", *pCsrEntRef);
		}
	}
	if(pCsrEntRef)
	{
		ANALYSIS_ASSUME(pCsrEntRef);
		free(pCsrEntRef);
	}
}

// MoveToPlayer <playerName>: Warps the CSR's character to the specified player, regardless of what map they are on
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(MoveToPlayer);
void ServerAdmin_CSRMoveToPlayer(Entity *pEnt, char *playerName)
{
	if (pEnt && playerName)
	{
		EntityRef *myRef = malloc(sizeof(EntityRef));
		*myRef = pEnt->myRef;
		gslGetPlayerIDFromNameWithRestore(playerName, entGetVirtualShardID(pEnt), ServerAdmin_CSRMoveToPlayerTransactionReturn, myRef);
	}
}

// MoveToPlayer <playerID>: Warps the CSR's character to the specified player, regardless of what map they are on
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(MoveToPlayerByID);
void ServerAdmin_CSRMoveToPlayerByID(Entity *pEnt, ContainerID iOtherPlayerID)
{
	RemoteCommand_GetDestinationForCSRMove(GLOBALTYPE_ENTITYPLAYER, iOtherPlayerID, iOtherPlayerID, GetAppGlobalType(), GetAppGlobalID(), entGetRef(pEnt));
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void GetDestinationForCSRMove_Return(U32 iEntRef, MapDescription *pMapDesc, Vec3 vPos, Quat qRot3)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt)
	{
		if (pMapDesc->mapDescription && pMapDesc->mapDescription[0])
		{
			// Move pEnt to the given map (which may be the entity's current map) with the given position, and rotation
			MapMoveWithDescriptionAndPosRot(pEnt, pMapDesc, vPos, qRot3, "GetDestinationForCSRMove_Return", true);
		}
		else
		{
			//fail, inform the user somehow.  This is hard because we may be on a different server
		}
	}
}

// Find the location of iEntIDToFind, then run a remote command to move eEntRefToMove to that location.
// eTypeForReturn and iContainerIDForReturn specify the server where eEntRefToMove lives.
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void GetDestinationForCSRMove(ContainerID iEntIDToFind, GlobalType eTypeForReturn, ContainerID iContainerIDForReturn, U32 eEntRefToMove)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntIDToFind);
	if (pEnt)
	{
		Vec3 vPos;
		Quat qRot3;

		MapDescription description = {0};

		// Get the location of pEnt
		entGetPos(pEnt, vPos);
		entGetRot(pEnt, qRot3);

		StructInit(parse_MapDescription, &description);
		StructCopy(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription, &description, 0, 0, 0);
		description.iPartitionID = partition_IDFromIdx(entGetPartitionIdx(pEnt));

		RemoteCommand_GetDestinationForCSRMove_Return(eTypeForReturn, iContainerIDForReturn, eEntRefToMove, 
			&description, vPos, qRot3);

		StructDeInit(parse_MapDescription, &description);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void MovePlayerForCSRSummon(ContainerID iContainerID, MapDescription *pMapDesc, Vec3 vPos, Quat qRot3, U32 eCsrEntRef)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
	if (pEnt)
	{
		if (pMapDesc->mapDescription && pMapDesc->mapDescription[0])
		{
			MapMoveWithDescriptionAndPosRot(pEnt, pMapDesc, vPos, qRot3, "GetDestinationForCSRMove_Return", true);
		}
		else
		{
			//fail, inform the user somehow.  This is hard because we may be on a different server
		}
	}
}

void ServerAdmin_CSRSummonPlayerTransactionReturn(TransactionReturnVal *returnVal, EntityRef *pCsrEntRef)
{
	ContainerID playerID = 0;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &playerID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && playerID)
	{
		Entity *pCsrEnt = entFromEntityRefAnyPartition(*pCsrEntRef);
		Vec3 vPos;
		Quat qRot3;

		if (pCsrEnt)
		{
			MapDescription *pTempDesc = StructClone(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription);
			pTempDesc->iPartitionID = partition_IDFromIdx(entGetPartitionIdx(pCsrEnt));
			entGetPos(pCsrEnt, vPos);
			entGetRot(pCsrEnt, qRot3);
			RemoteCommand_MovePlayerForCSRSummon(GLOBALTYPE_ENTITYPLAYER, playerID, playerID, pTempDesc, vPos, qRot3, *pCsrEntRef);
			StructDestroy(parse_MapDescription, pTempDesc);

		}
	}
	else
	{
		if(pCsrEntRef)
		{
			CSRReturnDialog("Failure", "Couldn't find player to summon!", *pCsrEntRef);
		}
	}
	if(pCsrEntRef)
	{
		free(pCsrEntRef);
	}
}

// SummonPlayer <playerName>: Moves the player to the location of the CSR, regardless of what map they are on
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(SummonPlayer);
void ServerAdmin_CSRSummonPlayer(Entity *pEnt, ACMD_SENTENCE playerName)
{
	if (pEnt && playerName)
	{
		EntityRef *pCsrEntRef = malloc(sizeof(EntityRef));
		*pCsrEntRef = pEnt->myRef;
		gslGetPlayerIDFromNameWithRestore(playerName, entGetVirtualShardID(pEnt), ServerAdmin_CSRSummonPlayerTransactionReturn, pCsrEntRef);
	}
}


// -------------------------
// Mission Commands
// -------------------------

// MissionCompleteOther <missionName> <playername>: Completes mission of the given name for the given player.
// Player must already have that mission.
// To complete a sub-mission, use "missioncompleteother missionname::submissionname"
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(MissionCompleteOther);
void ServerAdmin_CSRMissionCompleteOther(Entity* pEnt, ACMD_NAMELIST("AllMissionsIndex", RESOURCEDICTIONARY) char *missionName, char *playerName)
{
	if (pEnt && playerName && missionName)
	{
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		estrPrintf(&estrBuffer, "missioncomplete %s", missionName);
		RunCSRCommand(pEnt, playerName, estrBuffer);
		estrDestroy(&estrBuffer);
	}
}

// MissionDropOther <missionName> <playername>: Drops mission of the given name for the given player.
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(MissionDropOther);
void ServerAdmin_CSRMissionDropOther(Entity* pEnt, ACMD_NAMELIST(g_MissionNameList) char *missionName, char *playerName)
{
	if (pEnt && playerName && missionName)
	{
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		estrPrintf(&estrBuffer, "player_DropMission %s", missionName);
		RunCSRCommand(pEnt, playerName, estrBuffer);
		estrDestroy(&estrBuffer);
	}
}

// Just for debugging, replace with proper CSR command
AUTO_COMMAND;
void DebugRenamePlayer(ContainerID id, char *newName)
{
	RemoteCommand_dbRenamePlayer(NULL, GLOBALTYPE_OBJECTDB, 0, id, newName);
}

// UGC Commands

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(ugc_TempBanProject);
void TempBanUGCProject(SA_PARAM_NN_VALID Entity *pEnt, bool bBan)
{
	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	if (!pProject)
	{
		CSRReturnDialogEnt("ugc_TempBanProject Project failed", "Can't run ban UGC project on this gameserver... it's not a UGC gameserver", pEnt);
		return;
	}

	RemoteCommand_Intershard_aslUGCDataManager_TempBanProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetContainerID(pEnt),
		pProject->id, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), bBan);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(ugc_TempBanProjectByID);
void TempBanUGCProjectByID(SA_PARAM_NN_VALID Entity *pEnt, char *pIDString, bool bBan)
{
	U32 uID = 0;
	bool bIsSeries = false;
	if (((UGCIDString_StringToInt(pIDString, &uID, &bIsSeries) && !bIsSeries) || StringToUint(pIDString, &uID)) && uID)
	{
		RemoteCommand_Intershard_aslUGCDataManager_TempBanProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			entGetContainerID(pEnt),
			uID, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), bBan);
	}
	else
	{
		CSRReturnDialogEnt("ugc_TempBanProjectByID failed", STACK_SPRINTF("Couldn't interpret %s as either an ID string or project UID", pIDString), pEnt);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(ugc_DisableAutoBanProjectByID);
void DisableAutoBanProjectByID(SA_PARAM_NN_VALID Entity *pEnt, char *pIDString, bool bDisableAutoBan)
{
    U32 uID = 0;
	bool bIsSeries = false;
	if (((UGCIDString_StringToInt(pIDString, &uID, &bIsSeries) && !bIsSeries) || StringToUint(pIDString, &uID)) && uID)
    {
		RemoteCommand_Intershard_aslUGCDataManager_DisableAutoBanForProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			entGetContainerID(pEnt),
			uID, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), bDisableAutoBan);
    }
    else
    {
        CSRReturnDialogEnt("ugc_DisableAutoBanProjectByID failed", STACK_SPRINTF("Couldn't interpret %s as either an ID string or project UID", pIDString), pEnt);
    }
}

//not really a "CSR" command, per se, as it's just a normal command that someone can execute. But designed to be executed
//by CSRs
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(BanUGCProject, ugc_BanProject);
void BanUGCProject(SA_PARAM_NN_VALID Entity *pEnt, bool bBan)
{
	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	if (!pProject)
	{
		CSRReturnDialogEnt("Ban UGC Project failed", "Can't run ban UGC project on this gameserver... it's not a UGC gameserver", pEnt);
		return;
	}

	RemoteCommand_Intershard_aslUGCDataManager_PermBanProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetContainerID(pEnt),
		pProject->id, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), bBan);
}

//ban the ugc project with the specified ID, which can be either a numeric ID or an ID string
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(BanUGCProjectByID, ugc_BanProjectByID);
void BanUGCProjectByID(SA_PARAM_NN_VALID Entity *pEnt, char *pIDString, bool bBan)
{
	U32 uID = 0;
	bool bIsSeries = false;
	if (((UGCIDString_StringToInt(pIDString, &uID, &bIsSeries) && !bIsSeries) || StringToUint(pIDString, &uID)) && uID)
	{
		RemoteCommand_Intershard_aslUGCDataManager_PermBanProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			entGetContainerID(pEnt),
			uID, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), bBan);
	}
	else
	{
		CSRReturnDialogEnt("ugc_BanProjectByID failed", STACK_SPRINTF("Couldn't interpret %s as either an ID string or project UID", pIDString), pEnt);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(ugc_ClearNaughtyValue);
void ClearNaughtyValueForUGCProjectByID(SA_PARAM_NN_VALID Entity *pEnt, char *pIDString)
{
	U32 uID = 0;
	bool bIsSeries = false;
	if (((UGCIDString_StringToInt(pIDString, &uID, &bIsSeries) && bIsSeries) || StringToUint(pIDString, &uID)) && uID)
	{
		RemoteCommand_Intershard_aslUGCDataManager_ClearNaughtyValueForProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			entGetContainerID(pEnt),
			uID, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName));
	}
	else
	{
		CSRReturnDialogEnt("ugc_ClearNaughtyValue failed", STACK_SPRINTF("Couldn't interpret %s as either an ID string or project UID", pIDString), pEnt);
	}
}

//(project name) (reviewerName) (1=hide, 0=show) Hide (or show) a review by the given reviewer on the ugc project with the given name. 
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(SetUGCReviewHidden, ugc_SetReviewHidden);
void SetReviewHidden(SA_PARAM_NN_VALID Entity *pEnt, const char* pIDString, const char* pReviewerName, bool bHidden)
{
	U32 iProjectID = 0;
	bool bIsSeries = false;

	if (!UGCIDString_StringToInt(pIDString, &iProjectID, &bIsSeries) && !bIsSeries)
	{
		StringToUint(pIDString, &iProjectID);
	}

	entLog(LOG_UGC, pEnt, "UGCHideReviewAttempt", "Proj %s ID %d. Reviewer: %s. Hide: %d",
		pIDString, iProjectID, pReviewerName, bHidden);

	if (iProjectID!=0)
	{
		RemoteCommand_Intershard_aslUGCDataManager_ProjectReviewSetHidden(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			GetShardNameFromShardInfoString(),
			entGetContainerID(pEnt),
			iProjectID, pReviewerName, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), bHidden);
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void CSRPrintUGCReportData_Return(ContainerID entContainerID, UGCProjectReportQuery *pQuery)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt && pQuery)
	{
		const char* pchBanStatus;
		int i, c = 0;
		char* estrReport = NULL;
		estrStackCreate(&estrReport);

		if (pQuery->bBanned) {
			pchBanStatus = "Banned";
		} else if (pQuery->bTemporarilyBanned) {
			pchBanStatus = "Temporarily Banned";
		} else {
			pchBanStatus = "Not Banned";
		}
		estrConcatf(&estrReport, "Project Owner: %s (ID %d)\n", pQuery->pchOwnerAccountName, pQuery->uOwnerAccountID);
		estrConcatf(&estrReport, "Ban Status: %s\n", pchBanStatus);
		estrConcatf(&estrReport, "Naughty Value: %d\n\n", pQuery->iNaughtyValue);

		for (i = eaSize(&pQuery->eaReports)-1; i >= 0; i--)
		{
			UGCProjectReport* pReport = pQuery->eaReports[i];
			estrConcatf(&estrReport, "Report %d\n", ++c);
			estrConcatf(&estrReport, "Reporter Account: %s (ID %d)\n", pReport->pchAccountName, pReport->uAccountID);
			estrConcatf(&estrReport, "Report Time: %s\n", timeGetDateStringFromSecondsSince2000(pReport->uReportTime));
			estrConcatf(&estrReport, "Reason: %s\n", pReport->pchReason);
			estrConcatf(&estrReport, "Details: %s\n", pReport->pchDetails);
		}

		ServerChat_SendChatMessage(pEnt, kChatLogEntryType_System, estrReport, NULL);
		estrDestroy(&estrReport);
	}
	else
	{
		CSRReturnDialogEnt("UGC Print Report Data Failed", "Unknown UGC Project ID", pEnt);
	}
}

// Prints report data for a project to the chat logc
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(ugc_PrintReportData);
void PrintUGCReportData(Entity *pEnt, char *pIDString)
{
	U32 uID = 0;
	bool bIsSeries = false;
	if (((UGCIDString_StringToInt(pIDString, &uID, &bIsSeries) && !bIsSeries) || StringToUint(pIDString, &uID)) && uID)
	{
		RemoteCommand_Intershard_CSRQueryUGCProjectReportData(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, GetShardNameFromShardInfoString(), entGetContainerID(pEnt), uID);
	}
	else
	{
		CSRReturnDialogEnt("ugc_PrintReportData failed", STACK_SPRINTF("Couldn't interpret %s as either an ID string or project UID", pIDString), pEnt);
	}
}

// Add a CS granted character slot to the account of the given entity
AUTO_COMMAND ACMD_NAME(GrantCharSlot) ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(XMLRPC, csr);
void CSRGrantCharSlot(SA_PARAM_NN_VALID Entity *pEntity)
{
	if ( pEntity && pEntity->pPlayer )
	{
		AutoTrans_trCSRGrantCharSlot(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, pEntity->pPlayer->accountID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(gameAccountData, ".Eakeys");
enumTransactionOutcome trCSRGrantCharSlot(ATR_ARGS, NOCONST(GameAccountData) *gameAccountData)
{
	if (slGAD_trh_ChangeAttrib(ATR_PASS_ARGS, gameAccountData, MicroTrans_GetCSRCharSlotsGADKey(), 1))
		return TRANSACTION_OUTCOME_SUCCESS;

	return TRANSACTION_OUTCOME_FAILURE;
}

//////////////////////////////////////////////////////////////////////
/// Mark PROJECT-ID as featured.  If START-TIME or END-TIME is
/// the string "NOW", use the current time.  END-TIME can be "", which
/// will make it not expire.
AUTO_COMMAND ACMD_NAME(ugcFeatured_AddProject) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedAddProject(SA_PARAM_NN_VALID Entity* pEnt, ContainerID projectId, char* strDetails, const char* startTime, const char* endTime)
{
	U32 iStartTimestamp = gslUGC_FeaturedTimeFromString( startTime );
	U32 iEndTimestamp = gslUGC_FeaturedTimeFromString( endTime );

	RemoteCommand_Intershard_aslUGCDataManager_FeaturedAddProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetContainerID(pEnt),
		projectId, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), strDetails, iStartTimestamp, iEndTimestamp);
}

//////////////////////////////////////////////////////////////////////
/// Remove PROJECT-ID from featured and featured archives.
///
/// If this is a copy project, then it also deletes the project (which
/// will hide it).
AUTO_COMMAND ACMD_NAME(ugcFeatured_RemoveProject) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedRemoveProject(SA_PARAM_NN_VALID Entity* pEnt, ContainerID projectId )
{
	RemoteCommand_Intershard_aslUGCDataManager_FeaturedRemoveProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetContainerID(pEnt),
		projectId, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName));
}

//////////////////////////////////////////////////////////////////////
/// Forcably end PROJECT-ID from being featured at END-TIME.
///
/// If PROJECT-ID has already ended and END-TIMESTAMP would change the
/// end time, this instead does nothing.
AUTO_COMMAND ACMD_NAME(ugcFeatured_ArchiveProject) ACMD_ACCESSLEVEL(4);
void gslUGC_FeaturedArchiveProject(SA_PARAM_NN_VALID Entity* pEnt, ContainerID projectID, const char* endTime)
{
	U32 iEndTimestamp = gslUGC_FeaturedTimeFromString(endTime);

	RemoteCommand_Intershard_aslUGCDataManager_FeaturedArchiveProject(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(),
		entGetContainerID(pEnt),
		projectID, SAFE_MEMBER2(pEnt, pPlayer, publicAccountName), iEndTimestamp);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void CSR_Return(ContainerID entContainerID, bool bSuccess, char *pcDetails)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
		ClientCmd_GameDialogGenericMessage(pEnt, bSuccess ? "Success" : "Failure", pcDetails);

	if(strlen(pcDetails) > 4)
		log_printf(LOG_UGC, "%s", pcDetails + 4);
}
