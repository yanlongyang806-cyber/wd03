#include "AutoTransDefs.h"
#include "Entity.h"
#include "GameServerLib.h"
#include "gslMapState.h"
#include "gslPartition.h"
#include "gslQueue.h"
#include "mapstate_common.h"
#include "Message.h"
#include "objTransactions.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "Team.h"
#include "WorldGrid.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"

#define SECONDS_TO_WAIT_FOR_OWNER 30


/******
* This is the transaction that's invoked to change a player's personal difficulty setting.
******/
AUTO_TRANSACTION
ATR_LOCKS(pPlayerEnt, ".Pplayer.Idifficulty");
enumTransactionOutcome pd_tr_ChangeDifficulty(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iDifficulty)
{
	if (NONNULL(pPlayerEnt) && NONNULL(pPlayerEnt->pPlayer) && pd_GetDifficulty(iDifficulty))
	{
		pPlayerEnt->pPlayer->iDifficulty = iDifficulty;
		return TRANSACTION_OUTCOME_SUCCESS;
	}

	return TRANSACTION_OUTCOME_FAILURE;
}

/******
* This function is called when a player logs in.  If the player is the map owner, then the difficulty will be set using the player's setting
******/
void gslpd_SetDifficultyIfOwner(Entity* pEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if (pEnt && (gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_OWNED || gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_MISSION)) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		if(partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_ENTITYPLAYER && partition_OwnerIDFromIdx(iPartitionIdx) == entGetContainerID(pEnt) && pEnt->pPlayer) {
			mapState_SetDifficultyIfNotInitialized(iPartitionIdx, pEnt->pPlayer->iDifficulty);
		} else if(partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_TEAM && team_IsMember(pEnt) && partition_OwnerIDFromIdx(iPartitionIdx) == team_GetTeamID(pEnt)) {
			mapState_SetDifficultyIfNotInitialized(iPartitionIdx, pEnt->pPlayer->iDifficulty);
		}
	}

	PERFINFO_AUTO_STOP();
}

/******
* This function initializes the mapstate's difficulty according to the map owner's setting.
******/
bool gslpd_IsMapDifficultyInitialized(int iPartitionIdx)
{
	bool bHasOwner = false;
	QueuePartitionInfo* pQueuePartitionInfo = NULL;

	// immediately return if zero or only one default difficulty is specified for this game
	if (eaSize(&g_PlayerDifficultySet.peaPlayerDifficulties) < 2)
	{
		return true;
	}

	// Return if map state is marked as initialized
	if (mapState_IsDifficultyInitialized(iPartitionIdx)) {
		return true;
	}

	switch (gGSLState.gameServerDescription.baseMapDescription.eMapType)
	{
		xcase ZMTYPE_OWNED:
		acase ZMTYPE_MISSION:
		{
			if (partition_OwnerIDFromIdx(iPartitionIdx) &&
				partition_OwnerTypeFromIdx(iPartitionIdx))
			{
				bHasOwner = true;
			}
		}
		xcase ZMTYPE_PVP:
		acase ZMTYPE_QUEUED_PVE:
		{
			pQueuePartitionInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
			if (pQueuePartitionInfo)
			{
				bHasOwner = true;
			}
		}
	}

	// skip non-instance maps or maps that have no owner specified
	if (!bHasOwner)
	{
		mapState_SetDifficultyInitialized(iPartitionIdx);
		return true;
	}

	// map is owned by the QueueServer
	if (pQueuePartitionInfo)
	{
		mapState_SetDifficulty(iPartitionIdx, pQueuePartitionInfo->iQueueMapDifficulty);
	}
	// maps with entity owners
	else if (partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pOwnerEnt = partition_GetPlayerMapOwner(iPartitionIdx);
		if (pOwnerEnt && pOwnerEnt->pPlayer)
		{
			mapState_SetDifficulty(iPartitionIdx, pOwnerEnt->pPlayer->iDifficulty);
			return true;
		}
		return false;
	}
	// maps with team owners
	else if (partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_TEAM)
	{
		Team *pOwnerTeam = partition_GetTeamMapOwner(iPartitionIdx);
		if (pOwnerTeam)
		{
			mapState_SetDifficulty(iPartitionIdx, pOwnerTeam->iDifficulty);
			return true;
		}
		return false;
	}

	// Get here if owner type is not player or team.  Assume difficulty doesn't matter.
	mapState_SetDifficultyInitialized(iPartitionIdx);
	return true;
}
