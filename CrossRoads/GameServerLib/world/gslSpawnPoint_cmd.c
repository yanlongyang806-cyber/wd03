/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "gslSpawnPoint.h"
#include "WorldGrid.h"
#include "objTransactions.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

// ----------------------------------------------------------------------------------
// Debug Commands
// ----------------------------------------------------------------------------------

static void spawnpoint_NullTransRetValCB(TransactionReturnVal *pReturn, void *udata)
{
	// Do nothing
}

// respawn: moves the player to the nearest spawn without having them be dead first
AUTO_COMMAND ACMD_NAME(respawn);
void spawnpoint_CmdMoveToRespawn(Entity *pPlayerEnt)
{
	if (pPlayerEnt) {
		spawnpoint_MovePlayerToNearestSpawn(pPlayerEnt, false, true);
	}
}


// respawnAtStart: moves the player to the start spawn
AUTO_COMMAND ACMD_NAME(respawnAtStart) ACMD_SERVERCMD ACMD_ACCESSLEVEL(7);
void spawnpoint_CmdMovePlayerToStartSpawn(Entity *pPlayerEnt)
{
	if (pPlayerEnt) {
		spawnpoint_MovePlayerToStartSpawn(pPlayerEnt, true);
	}
}


// moveToSpawn: moves the player to the specified spawn point
AUTO_COMMAND ACMD_NAME(movePlayerToSpawn);
void encounter_CmdMovePlayerToSpawn(Entity *pPlayerEnt, const char *pcSpawnPointName)
{
	if (pPlayerEnt) {
		spawnpoint_MovePlayerToNamedSpawn(pPlayerEnt, pcSpawnPointName, NULL, false);
	}
}

// spawnActivateReset: Reset a player's activated spawn points
AUTO_COMMAND ACMD_NAME(spawnActivateReset);
void spawnpoint_SpawnActivateReset(Entity* playerEnt)
{
	TransactionReturnVal *pRetval = objCreateManagedReturnVal(spawnpoint_NullTransRetValCB, NULL);
	AutoTrans_tr_SpawnActivatedReset(pRetval, GetAppGlobalType(), entGetType(playerEnt), entGetContainerID(playerEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void spawnpoint_NavToSpawn(Entity* pPlayerEnt, const char* pchSpawnPoint)
{
	if (pPlayerEnt && pchSpawnPoint && pchSpawnPoint[0]) {
		GameSpawnPoint* pSpawnPoint = spawnpoint_GetByNameForSpawning(pchSpawnPoint, NULL);
		if (pSpawnPoint && pSpawnPoint->pWorldPoint) {
			ClientCmd_NavToSpawn_ReceivePosition(pPlayerEnt, pSpawnPoint->pWorldPoint->spawn_pos);
		}
	}
}
