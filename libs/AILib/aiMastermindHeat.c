#include "aiMastermindHeat.h"
#include "aiMastermind.h"
#include "aiExtern.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "Entity.h"
#include "Character.h"
#include "gslEncounter.h"
#include "entCritter.h"
#include "gslSpawnPoint.h"
#include "rand.h"
#include "RoomConn.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "beaconAStar.h"
#include "gslInteractable.h"
#include "EntityIterator.h"
#include "beaconPath.h"


typedef struct MMHeatPlayerUpdateStats
{
	F32 fMaxTeamHealth;
	F32 fCurrTeamHealth;

	F32 fAvgHealthPercent;

	F32 fMaxTimeSinceLastRoomChange;
	F32 fMinTimeSinceLastRoomChange;

	U32 numDead;

	S32 numInCombat;

	Vec3 vTeamBarycenter;
} MMHeatPlayerUpdateStats;


// some settings. some may be exposed later
static const F32 s_PlayerLowHPPercentThreshold = 0.3f; 
static const F32 s_TeamLowHPPercentThreshold = 0.5f;

static AIMMHeatManager s_heatManager = {0};

extern int g_bMastermindDebug;

void aiMastermind_DebugPrint(const char *format, ...);
AIMastermindDef *aiMastermind_GetDef();

// ------------------------------------------------------------------------------------------------------------------
void aiMastermindHeatDef_Validate(AIMMHeatDef *pHeatDef, const char *pchFilename)
{
	S32 numTiers = eaSize(&pHeatDef->eapcEncounterTiers);

	if (eaSize(&pHeatDef->eaHeatLevels) == 0)
	{
		ErrorFilenamef(pchFilename, "No Heat levels defined.");
	}	
	if (numTiers == 0)
	{
		ErrorFilenamef(pchFilename, "No Encounter Tiers defined.");
	}

	FOR_EACH_IN_EARRAY(pHeatDef->eaHeatLevels, AIMMHeatLevelDef, pMMHeat)
	{
		S32 i;
		if (pMMHeat->iAllowedTiersMax >= numTiers)
		{
			ErrorFilenamef(pchFilename, 
				"Heat Level def has an allowed tier outside the range of the specified tier list.");

			pMMHeat->iAllowedTiersMax = numTiers - 1;
		}

		if (pMMHeat->iAllowedTiersMin > pMMHeat->iAllowedTiersMax)
			pMMHeat->iAllowedTiersMin = pMMHeat->iAllowedTiersMax;

		for (i = eaiSize(&pMMHeat->eaiTierSpawnOrder)-1; i >= 0; --i)
		{
			if (pMMHeat->eaiTierSpawnOrder[i] >= numTiers)
			{
				ErrorFilenamef(pchFilename, 
					"Heat Level def has a tier spawn order that indexes "
					"outside the range of the specified tier list.");

				eaiRemoveFast(&pMMHeat->eaiTierSpawnOrder, i);
			}
		}
	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
static AIMMHeatLevelDef* aiMastermindHeat_GetCurrentHeatLevelDef(AIMMHeatDef *pDef)
{
	S32 iHeatIndex = (S32)floor(s_heatManager.fHeatLevel);

	if (!pDef)
		return NULL;

	return eaGet(&pDef->eaHeatLevels, iHeatIndex);
}

// ------------------------------------------------------------------------------------------------------------------
static AIMMHeatDef* aiMastermindHeat_GetDef()
{
	AIMastermindDef *pMMDef = aiMastermind_GetDef();
	if (pMMDef)
		return pMMDef->pHeatDef;

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
Expression* aiMastermindHeat_GetExpr(SA_PARAM_NN_VALID const char *pszFieldName, 
									 SA_PARAM_NN_VALID AIMMHeatDef *pMMDef, 
									 SA_PARAM_NN_VALID AIMMHeatLevelDef *pHeatDef)
{
	S32 column = 0;
	void *pExpr;
	if (!ParserFindColumn(parse_AIMMHeatExprCallbacks, pszFieldName, &column))
		return NULL;

	pExpr = StructGetSubtable(parse_AIMMHeatExprCallbacks, column, &pHeatDef->exprCallbacks, 0, NULL, NULL);
	if (pExpr)
		return (Expression*)pExpr;

	pExpr = StructGetSubtable(parse_AIMMHeatExprCallbacks, column, &pMMDef->exprCallbacks, 0, NULL, NULL);
	if (pExpr)
		return (Expression*)pExpr;

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
Expression* aiMastermindHeat_GetExprForCurrentHeatLevel(const char *pszFieldName)
{
	AIMMHeatDef* pDef = aiMastermindHeat_GetDef();
	AIMMHeatLevelDef *pHeatDef;
	if (!pDef)
		return NULL;

	pHeatDef = aiMastermindHeat_GetCurrentHeatLevelDef(pDef);
	if (!pHeatDef)
		return NULL;

	return aiMastermindHeat_GetExpr(pszFieldName, pDef, pHeatDef);
}


// ------------------------------------------------------------------------------------------------------------------
// returns true if an expression was found
#define aiMastermindHeat_CallExpession(pszFieldName) aiMastermindHeat_CallExpessionEx(pszFieldName, NULL)
int aiMastermindHeat_CallExpessionEx(const char *pszFieldName, int *pReturnVal)
{
	Expression *pExpr;
	ExprContext *pExprContext = aiMastermind_GetExprContext();
	
	if (!pExprContext)
		return false;

	pExpr = aiMastermindHeat_GetExprForCurrentHeatLevel(pszFieldName);
	if (pExpr)
	{
		MultiVal answer = {0};

		// TODO_PARTITION: Mastermind
		exprContextSetPartition(pExprContext,PARTITION_UNINITIALIZED);

		exprEvaluate(pExpr, pExprContext, &answer);
		if(answer.type != MULTI_INT)
		{
			if (pReturnVal) 
				*pReturnVal = false;
		}
		else 
		{
			if (pReturnVal) 
				*pReturnVal = QuickGetInt(&answer);
		}

		exprContextClearPartition(pExprContext);

		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int pointOBBCollision(const Vec3 point, const Vec3 min, const Vec3 max, const Mat4 world_matrix)
{
	Vec3 vLocalPos, vTmpPos;

	subVec3(point, world_matrix[3], vTmpPos);
	mulVecMat3Transpose(vTmpPos, world_matrix, vLocalPos);

	return (pointBoxCollision(vLocalPos, min, max));
}

static int pointInVolume(const Vec3 vPos, const WorldVolumeEntry *ent)
{
	return pointOBBCollision(	vPos, ent->base_entry.shared_bounds->local_min,
								ent->base_entry.shared_bounds->local_max, 
								ent->base_entry.bounds.world_matrix);
	/*
	Vec3 vLocalPos, vTmpPos;

	subVec3(vPos, ent->base_entry.bounds.world_matrix[3], vTmpPos);
	mulVecMat3Transpose(vTmpPos, ent->base_entry.bounds.world_matrix, vLocalPos);

	return (pointBoxCollision(vLocalPos, ent->base_entry.shared_bounds->local_min, 
	ent->base_entry.shared_bounds->local_max));
	*/
}


// ------------------------------------------------------------------------------------------------------------------
GameInteractable* aiMastermindHeat_FindGateAtPortal(RoomPortal* pPortal)
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaCachedGates, GameInteractable, pGate)
	{
		Vec3 vGatePos;
		if (interactable_GetPosition(pGate, vGatePos))
		{
			if (pointOBBCollision(vGatePos, pPortal->bounds_min, pPortal->bounds_max, pPortal->world_mat))
				return pGate;
		}
	}	
	FOR_EACH_END

		return NULL;
}

// Room creation and removal
// ------------------------------------------------------------------------------------------------------------------
void aiMastermindHeat_RoomUpdateCallback(RoomPortal* pPortal)
{
	Room *pRoom;
	if (!pPortal->parent_room)
		return;

	pRoom = pPortal->parent_room;

	if (!pPortal->ai_roomPortal)
	{
		pPortal->ai_roomPortal = calloc(1, sizeof(AIMMRoomConn));
		// search to see if there is a gate here
		pPortal->ai_roomPortal->pDoor = aiMastermindHeat_FindGateAtPortal(pPortal);
		eaPush(&s_heatManager.eaRoomConns, pPortal);
	}


	if (!pRoom->ai_room)
	{
		pRoom->ai_room = calloc(1, sizeof(AIMMRoom));
		eaPush(&s_heatManager.eaRooms, pRoom);

		if (pRoom->volume_entry && 
			pRoom->volume_entry->server_volume.mastermind_volume_properties && 
			pRoom->volume_entry->server_volume.mastermind_volume_properties->safe_room_until_exit )
		{
			pRoom->ai_room->bIsSafeRoom = true;
		}
	}
	else
	{	// our list was cleared for some reason
		if (eaFind(&s_heatManager.eaRooms, pRoom) == -1)
		{
			eaPush(&s_heatManager.eaRooms, pRoom);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_DestroyRoomConn(RoomPortal* pPortal)
{
	if (pPortal->ai_roomPortal)
	{
		free(pPortal->ai_roomPortal);
		pPortal->ai_roomPortal = NULL;

		eaFindAndRemoveFast(&s_heatManager.eaRoomConns, pPortal);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_DestroyRoomData(Room *pRoom)
{
	if (pRoom->ai_room)
	{
		AIMMRoom *pSpawnerRoom = pRoom->ai_room;

		eaDestroy(&pSpawnerRoom->eaPlayersInRoom);
		eaDestroy(&pSpawnerRoom->eaStaticEncounters);
		eaDestroy(&pSpawnerRoom->eaMMEncounters);

		free(pRoom->ai_room);
		pRoom->ai_room = NULL;

		eaFindAndRemoveFast(&s_heatManager.eaRooms, pRoom);
		eaFindAndRemoveFast(&s_heatManager.eaPlayerOccupiedRooms, pRoom);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermindHeat_RoomDestroyCallback(RoomPortal* pPortal)
{
	Room *pRoom;
	if (!pPortal->parent_room)
		return;

	aiMastermindHeat_DestroyRoomConn(pPortal);

	pRoom = pPortal->parent_room;

	aiMastermindHeat_DestroyRoomData(pRoom);
}
//


// ------------------------------------------------------------------------------------------------------------------
// queries
// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMastermindHeat_NumPlayersInRoom(Room *pRoom, AIMMRoom *pSpawner)
{
	if (pSpawner) 
		return eaSize(&pSpawner->eaPlayersInRoom);
	else if (pRoom && pRoom->ai_room)
		return eaSize(&pRoom->ai_room->eaPlayersInRoom);
}

// ------------------------------------------------------------------------------------------------------------------
static MMHeatPlayer* aiMastermindHeat_GetPlayerHeatInfo(Entity *e)
{
	EntityRef eref = entGetRef(e);
	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pPlayerHeat)
		if (pPlayerHeat->erPlayer == eref)
		{
			return pPlayerHeat;
		}
	FOR_EACH_END

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline int isPlayerInCombat(Entity *ent)
{
	return (ent->pChar->uiTimeCombatExit != 0);
}

// ------------------------------------------------------------------------------------------------------------------
static int aiMastermindHeat_IsAPlayerInCombat()
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
	{
		Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);

		if (!pEnt || !pEnt->pChar || !aiIsEntAlive(pEnt))
			continue;
		if (isPlayerInCombat(pEnt))
			return true;
	}
	FOR_EACH_END

		return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiMastermindHeat_GetNumCrittersInCombat()
{
	// pick one of the players in combat and count the aibase->statusCleanup
	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
	{
		Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);
		if (!pEnt || !pEnt->pChar || !aiIsEntAlive(pEnt))
			continue;

		if (isPlayerInCombat(pEnt))
		{
			return eaiSize(&pEnt->aibase->statusCleanup);
		}
	}
	FOR_EACH_END

		return 0;
}

// ------------------------------------------------------------------------------------------------------------------
F32 aiMastermindHeat_QualifyEntity(Entity *e)
{
	if (e->pCritter)
	{
		CritterDef* pDef = GET_REF(e->pCritter->critterDef);
		if (pDef)
		{
			return critterRankGetDifficultyValue(pDef->pcRank, pDef->pcSubRank, 0);
		}
	}
	// return some default value
	return 0.5f;
}

// ------------------------------------------------------------------------------------------------------------------
F32 aiMastermindHeat_QualifyGameEncounter()
{

	return 0.f;
}

// ------------------------------------------------------------------------------------------------------------------
// counts and approximates the difficulty of the critters that are currently in combat with players
static void aiMastermindHeat_QualifyCrittersInCombat(MMCombatCritterInfo *combatCritterInfo)
{
	if (s_heatManager.timeLastUpdatedCombatCritterInfo != ABS_TIME)
	{
		s_heatManager.timeLastUpdatedCombatCritterInfo = ABS_TIME;

		ZeroStruct(&s_heatManager.cachedCombatCritterInfo);

		// pick one of the players in combat and count the aibase->statusCleanup
		FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
		{
			Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);
			S32 i, count;
			if (!pEnt || !pEnt->pChar || !aiIsEntAlive(pEnt) || !isPlayerInCombat(pEnt))
				continue;

			count = eaiSize(&pEnt->aibase->statusCleanup);
			s_heatManager.cachedCombatCritterInfo.numEntities = count;

			for (i = count -1; i >= 0; --i)
			{
				Entity *e = entFromEntityRef(PARTITION_UNINITIALIZED, pEnt->aibase->statusCleanup[i]);
				if (!e || !aiIsEntAlive(e))
					continue;
				s_heatManager.cachedCombatCritterInfo.difficulty += aiMastermindHeat_QualifyEntity(e);
			}
			break;
		}
		FOR_EACH_END
	}

	if (combatCritterInfo)
		CopyStructs(combatCritterInfo, &s_heatManager.cachedCombatCritterInfo, 1);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_GetAllEntitesInCombatWithPlayer(Entity ***peaEntities)
{
	// pick one of the players in combat and count the aibase->statusCleanup
	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
	{
		Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);
		S32 i, count;
		if (!pEnt || !pEnt->pChar || !aiIsEntAlive(pEnt) || !isPlayerInCombat(pEnt))
			continue;

		count = eaiSize(&pEnt->aibase->statusCleanup);
		for (i = count -1; i >= 0; --i)
		{
			Entity *e = entFromEntityRef(PARTITION_UNINITIALIZED, pEnt->aibase->statusCleanup[i]);
			if (!e || !aiIsEntAlive(e))
				continue;
			eaPush(peaEntities, e);
		}
	}
	FOR_EACH_END
}


// finds all placed encounters in each room and puts them in either a list for the MM to spawn from
// or a static encounter list
static void aiMastermindHeat_FindEncountersInRoom(Room *pRoom)
{
	if (pRoom->volume_entry && pRoom->ai_room)
	{
#if 0
		F32 fVolumeSize;
		GameEncounter **eaNearbyEncs;

		fVolumeSize = distance3(pRoom->bounds_mid, pRoom->bounds_max);
		eaNearbyEncs = encounter_GetEncountersWithinDistance(pRoom->bounds_mid, fVolumeSize);

		if (eaSize(&eaNearbyEncs))
		{
			FOR_EACH_IN_EARRAY(eaNearbyEncs, GameEncounter, pEncounter)
			{
				if (pEncounter->pWorldEncounter)
				{
					if (pointInVolume(pEncounter->pWorldEncounter->encounter_pos, pRoom->volume_entry))
					{
						if (pEncounter->pWorldEncounter->properties &&
							pEncounter->pWorldEncounter->properties->pSpawnProperties && 
							pEncounter->pWorldEncounter->properties->pSpawnProperties->eMastermindSpawnType != WorldEncounterMastermindSpawnType_None)
						{
							eaPush(&pRoom->ai_room->eaMMEncounters, pEncounter);
						}
						else
						{
							eaPush(&pRoom->ai_room->eaStaticEncounters, pEncounter);
						}
					}
				}

			}
			FOR_EACH_END
		}
#endif
	}
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
int	aiMMRoomCostToTargetCallback(AStarSearchData* data, Room* nodeParent, Room* node, RoomPortal* connectionToNode)
{	// take the linear distance from the midpoint of the room to the midpoint of the target room
	Room *pTargetRoom = (Room*)data->targetNode;
	return distance3Squared(pTargetRoom->bounds_mid, node->bounds_mid);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
int aiMMRoomTraverseCostCallback(AStarSearchData* data, Room* prevNode, RoomPortal* connFromPrev, 
								 Room* sourceNode, RoomPortal* connection)
{
	Room *ptarget;
	if (!sourceNode || !connection->neighbor || !connection->neighbor->parent_room)
		return INT_MAX;

	ptarget = connection->neighbor->parent_room;

	return distance3Squared(ptarget->bounds_mid, sourceNode->bounds_mid);

}

// ------------------------------------------------------------------------------------------------------------------------------------------------
int aiMMRoomGetConnectionsCallback(AStarSearchData* data, Room* curRoom, RoomPortal*** connBuffer, 
								   Room*** nodeBuffer, int* position, int* numConnectionsCount)
{
	int i;
	S32 count = 0;

	if (*position)
		return 0;

	for (i = 0; i < eaSize(&curRoom->portals); i++)
	{
		RoomPortal *portal = curRoom->portals[i];
		if (portal->neighbor)
		{
			S32 x;
			bool bFound  = false;
			for (x = 0; x < count; x++)
			{
				if ((*nodeBuffer)[x] == portal->neighbor->parent_room)
				{
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				(*nodeBuffer)[count] = portal->neighbor->parent_room;
				(*connBuffer)[count] = portal;	
				count++;
			}
		}
	}

	*position = 1;
	*numConnectionsCount = count;
	return count > 0;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
// returns the number of rooms away the source room is to the destination
static S32 aiMastermindHeat_FindDistanceBetweenRooms(Room *pSourceRoom, Room *pDestRoom)
{
	NavSearchFunctions	mmNavSearchFuncs = {
		(NavSearchCostToTargetFunction)aiMMRoomCostToTargetCallback, 
		(NavSearchCostFunction)aiMMRoomTraverseCostCallback,
		(NavSearchGetConnectionsFunction)aiMMRoomGetConnectionsCallback,
		NULL, NULL, NULL
	};
	AStarSearchData *astarData = createAStarSearchData();
	S32 iRoomDist = -1;

	initAStarSearchData(astarData);

	astarData->nodeAStarInfoOffset = offsetof(Room, astar_info);
	astarData->sourceNode = pSourceRoom;
	astarData->targetNode = pDestRoom;

	AStarSearch(astarData, &mmNavSearchFuncs);
	if (astarData->pathWasOutput)
	{
		iRoomDist = astarData->exploredNodeCount - 1;
	}

	destroyAStarSearchData(astarData);

	return iRoomDist;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------
// For all rooms, calculate the distance to the start room
static void aiMastermindHeat_CalculateRoomDepths()
{
	GameSpawnPoint *pSpawnPoint = spawnpoint_GetPlayerStartSpawn();
	Room *pStartRoom = NULL;
	if (!pSpawnPoint || !pSpawnPoint->pWorldPoint)
		return;
	// found out which room the spawn point is in
	FOR_EACH_IN_EARRAY(s_heatManager.eaRooms, Room, pRoom)
		if(pointInVolume(pSpawnPoint->pWorldPoint->spawn_pos, pRoom->volume_entry))
		{
			pStartRoom = pRoom;
		}
		FOR_EACH_END

			if (!pStartRoom)
			{
				aiMastermind_DebugPrint("Could not find the player start spawn point within any room volume.");
				return;
			}

			// for every room, find the distance to the start
			FOR_EACH_IN_EARRAY(s_heatManager.eaRooms, Room, pRoom)

				if (pRoom == pStartRoom)
				{
					pStartRoom->ai_room->roomDepth = 0;
					continue;
				}

				pRoom->ai_room->roomDepth = aiMastermindHeat_FindDistanceBetweenRooms(pRoom, pStartRoom);

				FOR_EACH_END
}


// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_FindPortalGates()
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaRoomConns, RoomPortal, pPortal)
		pPortal->ai_roomPortal->pDoor = aiMastermindHeat_FindGateAtPortal(pPortal);
	FOR_EACH_END
}

// Volume Entry / exit
// ------------------------------------------------------------------------------------------------------------------
void aiMastermindHeat_TrackVolumeEntered(WorldVolumeEntry *pEntry, Entity *pEnt)
{    
	AIMMRoom *pSpawnerRoom;
	Room *pRoom;
	if (!pEntry->room || !pEntry->room->ai_room)
		return;
	pRoom = pEntry->room;
	pSpawnerRoom = pEntry->room->ai_room;

	if (entIsPlayer(pEnt))
	{
		// if no players in this room yet, add it
		if (eaSize(&pSpawnerRoom->eaPlayersInRoom) == 0) 
		{
			devassert( eaFind(&s_heatManager.eaPlayerOccupiedRooms, pRoom) == -1);
			eaPush(&s_heatManager.eaPlayerOccupiedRooms, pRoom);
		}

		eaPush(&pSpawnerRoom->eaPlayersInRoom, pEnt);

		if (!pSpawnerRoom->bPlayerHasVisited)
		{
			pSpawnerRoom->bPlayerHasVisited = true;

			aiMastermind_DebugPrint("A player entered a new room for the first time.\n");
			aiMastermindHeat_CallExpession("ExprEnteredNewRoom");

			if (pSpawnerRoom->bIsSafeRoom)
			{
				aiMastermind_DebugPrint("A player entered a safe room for first time.\n");
			}
			s_heatManager.teamStats.timeAtLastNewRoom = ABS_TIME;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static int aiMastermindHeat_IsRoomSafe(SA_PARAM_NN_VALID Room *pRoom)
{
	return (pRoom->ai_room && pRoom->ai_room->bIsSafeRoom && !pRoom->ai_room->bPlayerHasLeft);
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermindHeat_TrackVolumeExited(WorldVolumeEntry *pEntry, Entity *e)
{
	AIMMRoom *pSpawnerRoom;
	Room *pRoom;

	if (!pEntry->room || !pEntry->room->ai_room)
		return;
	pRoom = pEntry->room;
	pSpawnerRoom = pEntry->room->ai_room;

	if (entIsPlayer(e))
	{
		MMHeatPlayer *pPlayerHeat;
		// a player exited this volume
		eaFindAndRemoveFast(&pSpawnerRoom->eaPlayersInRoom, e);

		// check if there are any players left, and if not remove from the list of player volumes.
		if (eaSize(&pSpawnerRoom->eaPlayersInRoom) == 0)
		{
			S32 idx = eaFindAndRemoveFast(&s_heatManager.eaPlayerOccupiedRooms, pRoom);
			devassert( idx != -1);
		}

		pPlayerHeat = aiMastermindHeat_GetPlayerHeatInfo(e);
		if (!pPlayerHeat)
		{
			devassert(pPlayerHeat);
			return;
		}

		if (!pSpawnerRoom->bPlayerHasLeft && pSpawnerRoom->bIsSafeRoom)
		{
			aiMastermind_DebugPrint("A player left a safe room. Room is no longer safe.");
		}

		pSpawnerRoom->bPlayerHasLeft = true;

		// counting leaving a room as a room change
		pPlayerHeat->timeAtLastRoomChange = ABS_TIME;
	}
}
//

// ------------------------------------------------------------------------------------------------------------------
static void _destroySentEncounter(MMSentEncounter *pSentEnc, AITeam *pTeam)
{
	if (!pSentEnc->staticEncounter)
	{	// 
		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
			entDie(pMember->memberBE, 0, false, false, NULL);
		FOR_EACH_END
	}
	free(pSentEnc);
}


// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_UpdateAllPlayerInfo(SA_PARAM_NN_VALID MMHeatPlayerUpdateStats *pStats)
{
	F32 barycenterWeight;
	bool bFirstPlayer = true;
	ZeroStruct(pStats);
	pStats->fMinTimeSinceLastRoomChange = FLT_MAX;
	pStats->fMaxTimeSinceLastRoomChange = 0.f;
	pStats->fAvgHealthPercent = -1.f;

	barycenterWeight = (F32)eaSize(&s_heatManager.eaPlayerHeatInfo);
	if (barycenterWeight)
		barycenterWeight = 1.f / barycenterWeight;

	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pMMPlayer)
	{
		Entity *pPlayerEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pMMPlayer->erPlayer);
		if (!pPlayerEnt)
		{	// we should have been told that the entity was destroyed
			devassert(pPlayerEnt);
			continue;
		}

		// get the player's health percent. if it's below our low HP threshold capture the current time
		{
			F32 curHealth, maxHealth, percentHealth;
			bool bIsAlive;

			bIsAlive = entIsAlive(pPlayerEnt);
			if (!pMMPlayer->playerIsDead && !bIsAlive)
			{
				aiMastermind_DebugPrint("Player died.");
				aiMastermindHeat_CallExpession("ExprOnPlayerDeath");
				pMMPlayer->playerIsDead = true;

			}
			else if (pMMPlayer->playerIsDead && bIsAlive)
			{
				pMMPlayer->timeAtLastRespawn = ABS_TIME;
			}
			pMMPlayer->playerIsDead = !bIsAlive;

			aiExternGetHealth(pPlayerEnt, &curHealth, &maxHealth);

			pMMPlayer->fLastHealth = curHealth;
			pStats->fMaxTeamHealth += maxHealth;
			pStats->fCurrTeamHealth += curHealth;

			if (maxHealth) percentHealth = curHealth / maxHealth;
			else percentHealth = 0.f;

			if (percentHealth < s_PlayerLowHPPercentThreshold)
			{
				pMMPlayer->timeAtLowHP = ABS_TIME;
			}

			if (!bFirstPlayer)
			{
				pStats->fAvgHealthPercent = (pStats->fAvgHealthPercent + percentHealth) * 0.5f;
			}
			else 
			{
				pStats->fAvgHealthPercent = percentHealth;
			}
		}

		if (entIsAlive(pPlayerEnt))
		{
			if (isPlayerInCombat(pPlayerEnt))
			{
				pStats->numInCombat ++;
			}
		}
		else
		{
			pStats->numDead ++;
		}

		// update the positions
		{
			Vec3 curPos;
			entGetPos(pPlayerEnt, curPos);
			scaleAddVec3(curPos, barycenterWeight, pStats->vTeamBarycenter, pStats->vTeamBarycenter);
			copyVec3(curPos, pMMPlayer->vLastPlayerPos);
		}


		// get the time since the player last room changed
		{
			U64 time = ABS_TIME_SINCE(pMMPlayer->timeAtLastRoomChange);
			F32 fTime = ABS_TIME_TO_SEC(time);

			if (fTime < pStats->fMinTimeSinceLastRoomChange)
				pStats->fMinTimeSinceLastRoomChange = fTime;
			if (fTime > pStats->fMaxTimeSinceLastRoomChange)
				pStats->fMaxTimeSinceLastRoomChange = fTime;
		}

		bFirstPlayer = true;
	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_UpdateTeamStats(SA_PARAM_NN_VALID MMHeatPlayerUpdateStats *pStats)
{
	// update the team information
	F32 teamHealthPercent;
	S32 numPlayers = eaSize(&s_heatManager.eaPlayerHeatInfo);

	if (pStats->fMaxTeamHealth) teamHealthPercent = pStats->fCurrTeamHealth / pStats->fMaxTeamHealth;
	else teamHealthPercent = 0.f;

	if (teamHealthPercent < s_TeamLowHPPercentThreshold)
	{
		s_heatManager.teamStats.timeAtLowHP = ABS_TIME;
	}
	s_heatManager.teamStats.fTeamHealthPercent = teamHealthPercent;
	s_heatManager.teamStats.fAvgHealthPercent = pStats->fAvgHealthPercent;


	// check for a wipe -
	if (ABS_TIME_SINCE(s_heatManager.timeLastWipe) > SEC_TO_ABS_TIME(60.f))
	{
		bool allWipe = true;
		// checking each member to see if they have all died recently (last 20 seconds)
		FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pMMPlayer)
			if (ABS_TIME_SINCE(pMMPlayer->timeAtLastRespawn) > SEC_TO_ABS_TIME(20.f))
			{
				allWipe = false;
				break;
			}
		FOR_EACH_END

		if (allWipe)
		{
			s_heatManager.timeLastWipe = ABS_TIME;
			aiMastermind_DebugPrint("Team wiped.");
			aiMastermindHeat_CallExpession("ExprOnWipe");
		}
	}


	// check if someone has died get the time the last person died
	if (pStats->numDead > s_heatManager.teamStats.numDead)
	{
		s_heatManager.teamStats.timeOfLastDeath = ABS_TIME;
	}
	else if ((U32)numPlayers == s_heatManager.teamStats.numDead)
	{
		// team is wiped, keep resetting these times
		s_heatManager.teamStats.timeAtLastCombat = ABS_TIME;
		s_heatManager.timeSpawnPeriod = ABS_TIME;
	}

	s_heatManager.teamStats.numDead = pStats->numDead;

	if (pStats->numInCombat)
	{
		s_heatManager.teamStats.timeAtLastCombat = ABS_TIME;
	}
	s_heatManager.teamStats.numInCombat = pStats->numInCombat;

	copyVec3(pStats->vTeamBarycenter, s_heatManager.teamStats.vPlayerBarycenter);
}

// ------------------------------------------------------------------------------------------------------------------
static bool _sendAtTargetEntity(SA_PARAM_NN_VALID Entity *critter, 
								SA_PARAM_NN_VALID Entity *player)
{
	return aiMovementSetTargetEntity(	critter, critter->aibase, player, 
										NULL, false, AI_MOVEMENT_ORDER_ENT_FOLLOW, 
										AI_MOVEMENT_TARGET_CRITICAL);
}


// ------------------------------------------------------------------------------------------------------------------
static AITeam* MMSentEncounter_getAITeam(MMSentEncounter *pSentEnc)
{
	GameEncounterPartitionState *pState = encounter_GetPartitionState(pSentEnc->iPartitionIdx, pSentEnc->pEncounter);
	FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, pEntity)
	{
		AITeam *pTeam = aiTeamGetCombatTeam(pEntity, pEntity->aibase);
		if (pTeam && pSentEnc->pTeam == pTeam)
		{
			return pTeam;
		}
	}
	FOR_EACH_END

	return NULL;
}
// ------------------------------------------------------------------------------------------------------------------
static bool aiMastermindHeat_SendTeamAtPlayer(MMSentEncounter* pSentEnc, AITeam *pTeam, Entity *pPlayer)
{
	bool bPathFound = true;

	pSentEnc->erPlayer = entGetRef(pPlayer);

	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		Entity *pEntity = pMember->memberBE; 
		if (!aiIsEntAlive(pEntity))
			continue;
		if (!aiIsInCombat(pEntity))
		{
			if (!_sendAtTargetEntity(pEntity, pPlayer))
				bPathFound = false;
		}
	}
	FOR_EACH_END

		pSentEnc->pathFailed = !bPathFound;

	return bPathFound;
}

// ------------------------------------------------------------------------------------------------------------------
// Finds the closest player as a straight line distance check (may want to do rooms)
static Entity* aiMastermindHeat_FindClosestPlayer(const Vec3 vPos, F32 secondsAliveThreshold) 
{
	F32 fBestDist = FLT_MAX;
	Entity *entClosestPlayer = NULL;

	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
	{
		Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);
		F32 distSQR;
		Vec3 vCurPos;
		if (!pEnt || !aiIsEntAlive(pEnt))
			continue;

		if (secondsAliveThreshold > 0 && 
			ABS_TIME_SINCE(pHeatPlayer->timeAtLastRespawn) < SEC_TO_ABS_TIME(secondsAliveThreshold))
			continue;


		entGetPos(pEnt, vCurPos);

		distSQR = distance3Squared(vPos, vCurPos);
		if (distSQR < fBestDist)
		{
			entClosestPlayer = pEnt;
			fBestDist = distSQR;
		}
	}
	FOR_EACH_END

		return entClosestPlayer;
}


// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_ReturnAmbushTeamToSpawnPos(MMSentEncounter *pSentEnc, AITeam *pTeam)
{
	pSentEnc->leashing = true;

	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		Entity *pEntity = pMember->memberBE;
		AIConfig* pConfig;
		if (!aiIsEntAlive(pEntity))
			continue;

		pConfig = aiGetConfig(pEntity, pEntity->aibase);
		if (pConfig)
		{	// turn off roaming leash so they run back to where they spawned
			if (pConfig->roamingLeash == 1)
				aiConfigModAddFromString(pEntity, pEntity->aibase, "roamingLeash", "0", NULL);
		}

		if (!aiIsInCombat(pEntity))
		{
			EntityRef erMoveTarget = aiMovementGetMovementTargetEnt(pEntity, pEntity->aibase);
			Entity *pMoveTargetEnt = erMoveTarget ? entFromEntityRef(PARTITION_UNINITIALIZED, erMoveTarget) : NULL;
			if (pMoveTargetEnt && entIsPlayer(pMoveTargetEnt))
			{
				aiMovementGoToSpawnPos(pEntity, pEntity->aibase, AI_MOVEMENT_TARGET_CRITICAL);
			}
		}
	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
// Room walk utility
// ------------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------------
typedef int (*fpRoomTraverse)(int iPartitionIdx, Room *pSourceRoom, Room *pCurrentRoom, void *pData);

static int aiMastermindHeat_TraverseRoom(int iPartitionIdx, Room *pSourceRoom, Room *pRoom, S32 walkDepth, F32 walkDist, fpRoomTraverse fp, void *pData)
{
	AIMMRoom *aiRoom = pRoom->ai_room;

	aiRoom->roomWalkFlag = s_heatManager.currentRoomWalkFlag;

	if (fp(iPartitionIdx, pSourceRoom, pRoom, pData))
		return true;

	if (walkDepth <= 0)
		return false;
	walkDepth --;


	FOR_EACH_IN_EARRAY(pRoom->portals, RoomPortal, pRoomPortal)
	{
		AIMMRoomConn *pRoomConn = pRoomPortal->ai_roomPortal;

		if (!pRoomPortal->neighbor || !pRoomPortal->neighbor->parent_room || 
			!pRoomPortal->neighbor->parent_room->ai_room )
			continue;  // cannot traverse past this 

		if (pRoomConn && pRoomConn->pDoor && !interactable_IsGateOpen(iPartitionIdx, pRoomConn->pDoor))
			continue; // closed door, for now we assume we can't open this door

		aiRoom = pRoomPortal->neighbor->parent_room->ai_room;
		if (aiRoom->roomWalkFlag == s_heatManager.currentRoomWalkFlag)
			continue; // already traversed this room

		// check the distance from the current room to this room. if it is under the distance threshold 
		// walk the room
		{
			F32 fDistToRoom = 0.f;
			if (walkDist - fDistToRoom > 0.f)
			{
				if (aiMastermindHeat_TraverseRoom(iPartitionIdx, pSourceRoom, pRoomPortal->neighbor->parent_room, 
					walkDepth, walkDist - fDistToRoom, fp, pData))
					return true;
			}
		}
	}
	FOR_EACH_END

		return false;
}

// ------------------------------------------------------------------------------------------------------------------
// note: this could possibly be abstracted some more to move to the roomConn system
static int aiMastermindHeat_WalkPlayerRooms(int iPartitionIdx, S32 walkDepth, F32 maxWalkDist, S32 ignorePlayerSafeRooms,
										fpRoomTraverse fp, void *pData)
{
	if (!fp) 
		return false;

	if (eaSize(&s_heatManager.eaPlayerOccupiedRooms) > 0)
	{
		bool bIgnoredAllDueToSafeRoom = true;
		s_heatManager.currentRoomWalkFlag++;

		// for each room the players are in, check the connected rooms 
		FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerOccupiedRooms, Room, pRoom)
		{
			if (!ignorePlayerSafeRooms && aiMastermindHeat_IsRoomSafe(pRoom))
			{
				continue;
			}
			bIgnoredAllDueToSafeRoom = false;

			// todo: calculate the distance to the room
			if (pRoom->ai_room && aiMastermindHeat_TraverseRoom(iPartitionIdx, pRoom, pRoom, walkDepth, maxWalkDist, fp, pData))
				return true;
		}
		FOR_EACH_END

			if (bIgnoredAllDueToSafeRoom)
			{
				aiMastermind_DebugPrint("All players are in safe room.");
			}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int isInOrSeekingCombat(Entity *pEnt)
{
	if (aiIsInCombat(pEnt))
		return true;
	{	// if not in combat, check if they are currently seeking out the player
		EntityRef erMoveTarget = aiMovementGetMovementTargetEnt(pEnt, pEnt->aibase);
		if (erMoveTarget)
		{
			Entity *pMoveTargetEnt = entFromEntityRef(PARTITION_UNINITIALIZED, erMoveTarget);
			if (pMoveTargetEnt && entIsPlayer(pMoveTargetEnt))
			{
				return true;
			}
		}
	}

	return false;
}

__forceinline static void evalCritterEntity(Entity *e, MMNearbyCritterInfo *pData)
{
	if (isInOrSeekingCombat(e))
	{
		pData->numForCombat ++;
		pData->difficultyInCombat += aiMastermindHeat_QualifyEntity(e);
	}
	else 
	{
		pData->numAmbient ++;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static int CountNearbyAmbientCrittersCallback(int iPartitionIdx, Room *pSourceRoom, Room *pCurrentRoom, MMNearbyCritterInfo *pData)
{
	AIMMRoom *aiRoom = pCurrentRoom->ai_room;

	// static encounters
	FOR_EACH_IN_EARRAY(aiRoom->eaStaticEncounters, GameEncounter, pEncounter)
	{
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		pData->numStaticCritters += eaSize(&pState->eaEntities);

		FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, pEntity)
		{
			if (!aiIsEntAlive(pEntity))
				continue;
			evalCritterEntity(pEntity, pData);
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	// MM encounters
	FOR_EACH_IN_EARRAY(aiRoom->eaMMEncounters, GameEncounter, pEncounter)
	{
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		pData->numMMSpawnedCritters += eaSize(&pState->eaEntities);

		FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, pEntity)
		{
			if (!aiIsEntAlive(pEntity))
				continue;
			evalCritterEntity(pEntity, pData);
		}
		FOR_EACH_END
	}
	FOR_EACH_END


		return false;
}

// ------------------------------------------------------------------------------------------------------------------
// counts the number of critters nearby that are doing ambient stuff 
// along with any nearby that are in or going for combat
static void aiMastermindHeat_CountNearbyCritters(int iPartitionIdx, SA_PARAM_OP_VALID MMNearbyCritterInfo *countInfo)
{
	if (s_heatManager.timeLastUpdatedCritterCountInfo != ABS_TIME)
	{
		s_heatManager.timeLastUpdatedCritterCountInfo = ABS_TIME;

		ZeroStruct(&s_heatManager.cachedCritterCountInfo);
		aiMastermindHeat_WalkPlayerRooms(iPartitionIdx, 2, FLT_MAX, true, (fpRoomTraverse)CountNearbyAmbientCrittersCallback, 
			&s_heatManager.cachedCritterCountInfo);
	}

	if (countInfo)
		CopyStructs(countInfo, &s_heatManager.cachedCritterCountInfo, 1);
}



// ------------------------------------------------------------------------------------------------------------------
// counts and approximates the difficulty of the encounters critters that were sent at the player by the MM
static void aiMastermindHeat_QualifySentCrittersInCombat(MMCombatCritterInfo *combatCritterInfo)
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaSentEncounters, MMSentEncounter, pSentEnc)
	{
		AITeam *pTeam = MMSentEncounter_getAITeam(pSentEnc);
		if (!pTeam || pTeam->combatState == AITEAM_COMBAT_STATE_LEASH)
			continue; 

		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
			if (!aiIsEntAlive(pMember->memberBE))
				continue;
		combatCritterInfo->numEntities++;
		combatCritterInfo->difficulty += aiMastermindHeat_QualifyEntity(pMember->memberBE);
		FOR_EACH_END
	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_QualifyCrittersInAndForCombat(MMCombatCritterInfo *combatCritterInfo)
{
	static Entity **s_eaEntitiesForCombat = NULL;

	eaClear(&s_eaEntitiesForCombat);

	aiMastermindHeat_GetAllEntitesInCombatWithPlayer(&s_eaEntitiesForCombat);

	// go through the sent encounters and push the unique entities
	FOR_EACH_IN_EARRAY(s_heatManager.eaSentEncounters, MMSentEncounter, pSentEnc)
	{
		AITeam *pTeam = MMSentEncounter_getAITeam(pSentEnc);
		if (!pTeam || pTeam->combatState == AITEAM_COMBAT_STATE_LEASH)
			continue;

		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
			if (!aiIsEntAlive(pMember->memberBE))
				continue;
		eaPushUnique(&s_eaEntitiesForCombat, pMember->memberBE);
		FOR_EACH_END
	}
	FOR_EACH_END

		combatCritterInfo->numEntities = eaSize(&s_eaEntitiesForCombat);
	FOR_EACH_IN_EARRAY(s_eaEntitiesForCombat, Entity, pEntity)
	{
		combatCritterInfo->difficulty += aiMastermindHeat_QualifyEntity(pEntity);
	}
	FOR_EACH_END

}



// ------------------------------------------------------------------------------------------------------------------
static MMSentEncounter* aiMastermindHeat_AddTrackedEncounter(int iPartitionIdx,
														 GameEncounter *pEncounter, 
														 AITeam *pTeam, 
														 Entity *pTargetPlayer,
														 bool bIsStatisEncounter)
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaSentEncounters, MMSentEncounter, pSentEncounter)
	{
		if (pSentEncounter->pEncounter == pEncounter && pSentEncounter->pTeam == pTeam)
		{	// already being tracked
			return pSentEncounter;
		}
	}
	FOR_EACH_END

	{
		MMSentEncounter *pSentEncounter = calloc(1, sizeof(MMSentEncounter));
		if (pSentEncounter)
		{
			pSentEncounter->pEncounter = pEncounter;
			pSentEncounter->iPartitionIdx = iPartitionIdx;
			pSentEncounter->pTeam = pTeam;
			pSentEncounter->staticEncounter = !!bIsStatisEncounter;
			pSentEncounter->erPlayer = pTargetPlayer ? entGetRef(pTargetPlayer) : 0;

			eaPush(&s_heatManager.eaSentEncounters, pSentEncounter);
		}
		return pSentEncounter;
	}

	return NULL;
}




// ------------------------------------------------------------------------------------------------------------------
// sends all critters on the team to ambush the given player
static bool aiMastermindHeat_SendAITeamToAmbushPlayer(SA_PARAM_NN_VALID MMSentEncounter* pSentEnc, 
												  SA_PARAM_NN_VALID AITeam *team, SA_PARAM_NN_VALID Entity *player)
{
	if (!aiTeamInCombat(team))
	{
		bool bPathFound = true;
		FSM *pOverrideFSM = fsmGetByName("Combat_Noambient");


		// set up the entities to do ambushing properly
		// - set their FSM to only do combat
		// - remove staredown times, 
		// - add roaming leash
		// etc
		FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
		{
			Entity *critter = pMember->memberBE;
			if (pOverrideFSM)
				aiSetFSM(critter, pOverrideFSM);

			{
				AIConfig* pConfig = aiGetConfig(critter, critter->aibase);
				if (pConfig)
				{
					if (pConfig->stareDownTime >= 0)
						aiConfigModAddFromString(critter, critter->aibase, "stareDownTime", "-1", NULL);
					if (!pConfig->roamingLeash)
						aiConfigModAddFromString(critter, critter->aibase, "roamingLeash", "1", NULL);
				}

				aiMovementSetWalkRunDist(critter, critter->aibase, 0, 0, 0);
			}

			// make sure these critters don't fall asleep
			critter->aibase->dontSleep = true;
		}
		FOR_EACH_END


			aiMastermindHeat_SendTeamAtPlayer(pSentEnc, team, player);

		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int canEntityBeSentToCombat(Entity *pEnt)
{
	return (pEnt->aibase->team->combatState != AITEAM_COMBAT_STATE_LEASH);
}

// ------------------------------------------------------------------------------------------------------------------
static int findEncounterToSend(int iPartitionIdx, Room *pSourceRoom, Room *pRoom, void *pData)
{
	const char *pcEncounterGroupName = (const char *)pData;
	AIMMRoom *aiRoom = pRoom->ai_room;

	// check the MM encounters first. if they aren't in combat or seeking the player, send them!
	FOR_EACH_IN_EARRAY(aiRoom->eaMMEncounters, GameEncounter, pEncounter)
	{
		GameEncounterPartitionState *pState;

		if ((pcEncounterGroupName && pEncounter->pEncounterGroup->pcName != pcEncounterGroupName) ||
			(!pcEncounterGroupName && pEncounter->pEncounterGroup) )
		{	// not the right encounter group
			continue;
		}

		pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, pEntity)
		{
			if (!aiIsEntAlive(pEntity))
				continue;

			if (!isInOrSeekingCombat(pEntity) && canEntityBeSentToCombat(pEntity))
			{
				Entity *pClosestPlayer;
				MMSentEncounter* pSentEnc;
				AITeam *pTeam = aiTeamGetCombatTeam(pEntity, pEntity->aibase);
				pClosestPlayer = aiMastermindHeat_FindClosestPlayer(pEncounter->pWorldEncounter->encounter_pos, -1);
				devassert(pClosestPlayer);

				pSentEnc = aiMastermindHeat_AddTrackedEncounter(entGetPartitionIdx(pClosestPlayer), pEncounter, pTeam, pClosestPlayer, true);
				if (!pSentEnc)
					return false;

				if (!aiMastermindHeat_SendAITeamToAmbushPlayer(pSentEnc, pEntity->aibase->team, pClosestPlayer))
				{
					pSentEnc->pathFailed = true;
				}

				{
					const char * encounterName = "unnamed";
					if (pEncounter->pEncounterGroup && pEncounter->pEncounterGroup->pcName)
						encounterName = pEncounter->pEncounterGroup->pcName;

					aiMastermind_DebugPrint("Sending Wave: Found spawned encounter to send to players: %s\n",
						encounterName );
				}

				return true;
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END

		return false;
}

static int aiMastermindHeat_SendNearbyEncounterAtPlayers(int iPartitionIdx, const char *pcEncounterGroupName)
{
	if (pcEncounterGroupName)
		pcEncounterGroupName = allocFindString(pcEncounterGroupName);

	return aiMastermindHeat_WalkPlayerRooms(iPartitionIdx, 2, FLT_MAX, false, (fpRoomTraverse)findEncounterToSend, (void*)pcEncounterGroupName);
}

// ------------------------------------------------------------------------------------------------------------------
// todo: this may need to be more accurate, as the encounters spawn from actor locations 
// not just the encounter position
static int aiMastermindHeat_CanPlayerSeeEncounter(GameEncounter *pEncounter)
{
	Vec3 vEncounterPos;

	copyVec3(pEncounter->pWorldEncounter->encounter_pos, vEncounterPos);
	vEncounterPos[1] += 3.f;

	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
	{
		Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);
		Vec3 vCurPos;
		if (!pEnt || !entIsAlive(pEnt))
			continue;

		entGetPos(pEnt, vCurPos);
		vCurPos[1] += 3.f;

		{
			WorldCollCollideResults results = {0};
			if(! worldCollideRay(entGetPartitionIdx(pEnt), vCurPos, vEncounterPos, WC_QUERY_BITS_WORLD_ALL, &results))
			{	// hit something
				return true;	
			}
		}
	}
	FOR_EACH_END

		return false;
}

// ------------------------------------------------------------------------------------------------------------------
typedef struct FindSpawnEncounterData
{
	const char		*pcEncounterGroupName;
	S32				iBestRoomDepth;
	S32				iBestRoomDepthDelta;
	GameEncounter	*pBestGameEncounter;
} FindSpawnEncounterData;


// check to see if there is a valid encounter in this room
static int FindSpawnEncounterCallback(int iPartitionIdx, Room *pSourceRoom, Room *pRoom, FindSpawnEncounterData *pData)
{
	AIMMRoom *aiRoom = pRoom->ai_room;
	AIMMRoom *sourceMMRoom = pSourceRoom->ai_room;

	// check this room to see if it has the goods
	if (eaSize(&aiRoom->eaPlayersInRoom) == 0)
	{
		FOR_EACH_IN_EARRAY(aiRoom->eaMMEncounters, GameEncounter, pEncounter)
		{
			S32 depthDelta;

			if ((pData->pcEncounterGroupName && pEncounter->pEncounterGroup->pcName != pData->pcEncounterGroupName) ||
				(!pData->pcEncounterGroupName && pEncounter->pEncounterGroup) )
			{	// not the right encounter group
				continue;
			}

			// check if any of the players can see the encounter
			if (aiMastermindHeat_CanPlayerSeeEncounter(pEncounter))
				continue;

			depthDelta = sourceMMRoom->roomDepth - aiRoom->roomDepth;
			depthDelta = ABS(depthDelta);


			if (depthDelta < pData->iBestRoomDepthDelta || 
				(depthDelta == pData->iBestRoomDepthDelta && aiRoom->roomDepth > pData->iBestRoomDepth) )
			{
				pData->iBestRoomDepthDelta = depthDelta;
				pData->iBestRoomDepth = aiRoom->roomDepth;
				pData->pBestGameEncounter = pEncounter;
			}
		}
		FOR_EACH_END
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
// finds an encounter nearby 
static int aiMastermindHeat_SpawnCrittersForPlayers(int iPartitionIdx, const char *pcEncounterGroupName)
{
	FindSpawnEncounterData	data = {0};

	data.iBestRoomDepth = -INT_MAX;
	data.iBestRoomDepthDelta = INT_MAX;

	if (pcEncounterGroupName)
		data.pcEncounterGroupName = allocFindString(pcEncounterGroupName);

	aiMastermindHeat_WalkPlayerRooms(iPartitionIdx, 2, FLT_MAX, false, (fpRoomTraverse)FindSpawnEncounterCallback, (void*)&data);

	if (data.pBestGameEncounter)
	{
		GameEncounter *pEncounter = data.pBestGameEncounter;
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		Entity *pClosestPlayer;
		// find the closest player to send the entities to
		pClosestPlayer = aiMastermindHeat_FindClosestPlayer(pEncounter->pWorldEncounter->encounter_pos, -1);
		devassert(pClosestPlayer);

		s_heatManager.timeLastSpawned = ABS_TIME;

		encounter_SpawnEncounter(pEncounter, pState);



		// get all the spawned entities and set them up for the ambush
		{
			FSM *pOverrrideFSM = fsmGetByName("Combat_Noambient");

			FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, ent)
			{
				// only the ones that we just spawned. 
				if (!ent->aibase->hadFirstTick)
				{
					AITeam *pTeam = aiTeamGetCombatTeam(ent, ent->aibase);
					MMSentEncounter* pSentEnc = aiMastermindHeat_AddTrackedEncounter(iPartitionIdx, pEncounter, pTeam, pClosestPlayer, false);

					aiMastermindHeat_SendAITeamToAmbushPlayer(pSentEnc, pTeam, pClosestPlayer);

					break;
				}
			}
			FOR_EACH_END
		}

		aiMastermind_DebugPrint("Sending Wave: Spawned a new encounter at loc (%.1f,%.1f,%.1f) from group %s.\n", 
			vecParamsXYZ(pEncounter->pWorldEncounter->encounter_pos),
			pcEncounterGroupName ? pcEncounterGroupName : "not-specified" );
		return true;
	}
	else
	{
		aiMastermind_DebugPrint("Could not find an encounter group named '%s' to spawn.", pcEncounterGroupName);
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiMastermindHeat_SpawnCrittersForPlayersByTier(int iPartitionIdx, S32 iTier)
{
	S32 iNumTiers;
	AIMMHeatDef* pDef = aiMastermindHeat_GetDef();

	if (!pDef)
		return false;

	iNumTiers = eaSize(&pDef->eapcEncounterTiers);
	if (!iNumTiers)
		return false;

	iTier = CLAMP(iTier, 0, iNumTiers);

	return aiMastermindHeat_SpawnCrittersForPlayers(iPartitionIdx, pDef->eapcEncounterTiers[iTier]);
}



// ------------------------------------------------------------------------------------------------------------------
static int aiMastermindHeat_IsAnyPlayerAlive()
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerHeatInfo, MMHeatPlayer, pHeatPlayer)
	{
		Entity *pEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pHeatPlayer->erPlayer);

		if (!pEnt || !pEnt->pChar || !aiIsEntAlive(pEnt))
			continue;

		return true;
	}
	FOR_EACH_END

		return false;
}




// ------------------------------------------------------------------------------------------------------------------
// Function returns whether or not the mastermind should spawn/send an encounter at the players
static int aiMastermindHeat_CanIntroduceNewEncounter(AIMMHeatLevelDef *pHeatLevelDef)
{
	if (!aiMastermindHeat_IsAnyPlayerAlive())
		return false;

	{
		int result = 0;
		if (aiMastermindHeat_CallExpessionEx("ExprCanSendWave", &result))
		{
			if (!result) // denied by expression
				return false;
		}
	}


	// health threshold gating
	if (s_heatManager.teamStats.fAvgHealthPercent < (pHeatLevelDef->fHealthPercentThreshold/100.f))
	{	// if at low health, allow 5 seconds of no combat
		if (!ABS_TIME_PASSED(s_heatManager.teamStats.timeAtLastCombat, 5.f))
		{
			return false;
		}
	}

	// estimated critters in combat gating
	{
		MMCombatCritterInfo combatCritterInfo = {0};
		S32 numPlayers;

		aiMastermindHeat_QualifyCrittersInAndForCombat(&combatCritterInfo);

		numPlayers = eaSize(&s_heatManager.eaPlayerHeatInfo);

		if (combatCritterInfo.difficulty >= (numPlayers + pHeatLevelDef->fDifficultyValueThreshold))
			return false; // too many critters in combat or seeking out players
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
// returns whether or not to force a spawn
static bool aiMastermindHeat_ShouldForceSpawn(AIMMHeatDef *pDef, AIMMHeatLevelDef *pHeatLevelDef)
{
	int result = 0;
	if (!aiMastermindHeat_IsAnyPlayerAlive() || s_heatManager.forcedEncounter != NULL)
		return false;

	// TODO: gate force wave spawning- only allow one forced wave spawn to be active at a time

	if (aiMastermindHeat_CallExpessionEx("ExprShouldForceWave", &result))
	{
		if (result)
		{
			aiMastermind_DebugPrint("Expression forcing a wave.");
			return true;
		}
	}

	if (pHeatLevelDef->fIdleTimeForceSpawn)
	{
		if (ABS_TIME_SINCE(s_heatManager.teamStats.timeAtLastCombat) > SEC_TO_ABS_TIME(pHeatLevelDef->fIdleTimeForceSpawn))
		{
			aiMastermind_DebugPrint("Attempting to force spawn due to: IdleTimeForceSpawn");
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static const char* aiMastermindHeat_GetTierGroupToSpawn(SA_PARAM_NN_VALID AIMMHeatDef *pDef, 
													SA_PARAM_NN_VALID AIMMHeatLevelDef *pCurHeatLevel)
{
	S32 count = eaiSize(&pCurHeatLevel->eaiTierSpawnOrder);
	if (count == 0)
	{
		S32 iIdx = randomIntRange(pCurHeatLevel->iAllowedTiersMin, pCurHeatLevel->iAllowedTiersMax);
		return eaGet(&pDef->eapcEncounterTiers, iIdx);
	}

	if (s_heatManager.iHeatTierSpawnOrderIndex >= count)
	{
		s_heatManager.iHeatTierSpawnOrderIndex = 0;
	}

	{
		S32 iIdx = pCurHeatLevel->eaiTierSpawnOrder[s_heatManager.iHeatTierSpawnOrderIndex];
		return eaGet(&pDef->eapcEncounterTiers, iIdx);
	}
}



// ------------------------------------------------------------------------------------------------------------------
// heat level adjusting funcs

static void aiMastermindHeat_SetHeatLevelMinMax(F32 fHeatMin, F32 fHeatMax)
{
	AIMMHeatDef *pDef = aiMastermindHeat_GetDef();
	if (!pDef)
		return;

	{
		S32 iNumHeatLevels = eaSize(&pDef->eaHeatLevels);

		if (fHeatMin > fHeatMax)
			fHeatMin = fHeatMax;

		s_heatManager.fHeatLevelMin = CLAMP(fHeatMin, 0.f, iNumHeatLevels);
		s_heatManager.fHeatLevelMax = CLAMP(fHeatMax, 0.f, iNumHeatLevels);
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_AddHeat(F32 fHeat, const char *reason)
{
	S32 iOldHeatLevel = (S32)floor(s_heatManager.fHeatLevel);
	S32 iNewHeatLevel;

	s_heatManager.fHeatLevel += fHeat;
	s_heatManager.fHeatLevel = CLAMP(s_heatManager.fHeatLevel, s_heatManager.fHeatLevelMin, s_heatManager.fHeatLevelMax);

	aiMastermind_DebugPrint("Heat adjusted: (%.2f). New Heat: (%.2f). Reason : %s.", 
		fHeat, s_heatManager.fHeatLevel, 
		(reason) ? reason : "No Reason Given");

	iNewHeatLevel = (S32)floor(s_heatManager.fHeatLevel);
	if (iNewHeatLevel != iOldHeatLevel)
	{
		aiMastermind_DebugPrint("Heat Level Changed! New heat level: %d", iNewHeatLevel);
		s_heatManager.iHeatTierSpawnOrderIndex = 0;
		// reset any timers?
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_SetHeatLevel(F32 fHeat, const char *reason)
{
	F32 fDeltaHeat = fHeat - s_heatManager.fHeatLevel;
	if (fDeltaHeat > 0.f)
	{
		aiMastermindHeat_AddHeat(fDeltaHeat, reason);
	}
}


// ------------------------------------------------------------------------------------------------------------------
// spawn period

static void aiMastermindHeat_AdjustSpawnPeriod(F32 seconds)
{
	s_heatManager.fSpawnDelayTime += seconds;
	aiMastermind_DebugPrint("Spawn Period Time adjusted. At delay of: %.2f seconds", s_heatManager.fSpawnDelayTime);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_ResetSpawnPeriod()
{
	s_heatManager.fSpawnDelayTime = 0;
	s_heatManager.timeSpawnPeriod = ABS_TIME;
	aiMastermind_DebugPrint("Spawn Period Time Reset.");
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_SetNextSpawnTime(F32 seconds)
{
	s_heatManager.fOverrideSpawnTime = seconds;
	aiMastermind_DebugPrint("Next Spawn Time overridden: %.2f", s_heatManager.fOverrideSpawnTime);
}

// ------------------------------------------------------------------------------------------------------------------
static F32 aiMastermindHeat_GetCurrentSpawnTime(const AIMMHeatLevelDef *pHeatLevelDef)
{
	F32 fDesiredSpawnTime = (s_heatManager.fOverrideSpawnTime > 0.f) ? 
		s_heatManager.fOverrideSpawnTime : pHeatLevelDef->fSpawnPeriod;
	fDesiredSpawnTime += s_heatManager.fSpawnDelayTime;
	return fDesiredSpawnTime;
}





// ------------------------------------------------------------------------------------------------------------------
static int aiMastermindHeat_AreAllPlayersInSafeRoom()
{
	if (eaSize(&s_heatManager.eaPlayerOccupiedRooms) > 0)
	{
		// for each room the players are in, check the connected rooms 
		FOR_EACH_IN_EARRAY(s_heatManager.eaPlayerOccupiedRooms, Room, pRoom)
		{
			if (!aiMastermindHeat_IsRoomSafe(pRoom))
			{
				return false;
			}
		}
		FOR_EACH_END
	}

	return true;
}


// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_HeatUpdate(AIMMHeatDef *pDef)
{
	int iPartitionIdx = PARTITION_UNINITIALIZED; // TODO_PARTITION: AI Mastermind 
	S32 iNumTiers;
	F32 fDesiredSpawnTime;
	AIMMHeatLevelDef *pHeatLevelDef;
	bool bForceSpawn = false;


	if (!pDef)
		return;

	iNumTiers = eaSize(&pDef->eapcEncounterTiers); 
	if (iNumTiers == 0)	
		return;

	pHeatLevelDef = aiMastermindHeat_GetCurrentHeatLevelDef(pDef);
	if (!pHeatLevelDef)
		return;

	if (!aiMastermindHeat_IsAnyPlayerAlive())
	{
		s_heatManager.timeSpawnPeriod = ABS_TIME + SEC_TO_ABS_TIME(pHeatLevelDef->fSpawnPeriod * 0.5f);
		return;
	}

	aiMastermindHeat_CallExpession("ExprPerTick");

	fDesiredSpawnTime = (s_heatManager.fOverrideSpawnTime > 0.f) ? 
								s_heatManager.fOverrideSpawnTime : pHeatLevelDef->fSpawnPeriod;
	fDesiredSpawnTime += s_heatManager.fSpawnDelayTime;
	
	if (!aiMastermindHeat_AreAllPlayersInSafeRoom())
		bForceSpawn = aiMastermindHeat_ShouldForceSpawn(pDef, pHeatLevelDef);


	{
		F32 timeSince = ABS_TIME_TO_SEC(ABS_TIME_SINCE(s_heatManager.timeSpawnPeriod));
		F32 fTimeLeft = fDesiredSpawnTime - timeSince;
		
		if (fTimeLeft < 0.f)
			fTimeLeft = 0.f;

		if (g_bMastermindDebug && fTimeLeft > 0.f)
		{
			F32 lastMod = fmod(s_heatManager.fLastSpawnTimeLeft, 30.f);
			F32 curMod = fmod(fTimeLeft, 30.f);
			if (curMod > lastMod)
			{
				aiMastermind_DebugPrint("Next spawn time in %.2f seconds", fTimeLeft);
			}
		}
		s_heatManager.fLastSpawnTimeLeft = fTimeLeft;

		
		if (bForceSpawn || ABS_TIME_SINCE(s_heatManager.timeSpawnPeriod) > SEC_TO_ABS_TIME(fDesiredSpawnTime))
		{
			s_heatManager.fSpawnDelayTime = 0.f;
			s_heatManager.fOverrideSpawnTime = 0.f;

			if (!bForceSpawn && !aiMastermindHeat_CanIntroduceNewEncounter(pHeatLevelDef))
			{	// can't spawn yet, wait half the current spawn period 
				F32 fTimeAdjust = pHeatLevelDef->fSpawnPeriod - pHeatLevelDef->fSpawnFailedResetTime;
				s_heatManager.timeSpawnPeriod = ABS_TIME - SEC_TO_ABS_TIME(fTimeAdjust);
				return;
			}

			{
				bool bFoundSpawn = true;
				const char *pcSpawnTierGroup = aiMastermindHeat_GetTierGroupToSpawn(pDef, pHeatLevelDef);

				// attempt to send a nearby MM encounter at the player and if there are none appropriate
				// spawn a new one
				if (!aiMastermindHeat_SendNearbyEncounterAtPlayers(iPartitionIdx, pcSpawnTierGroup))
				{
					if (!aiMastermindHeat_SpawnCrittersForPlayers(iPartitionIdx, pcSpawnTierGroup))
					{
						bFoundSpawn = false;
					}
				}

				if (bFoundSpawn && bForceSpawn)
				{
					MMSentEncounter *pSentEnc = eaTail(&s_heatManager.eaSentEncounters);
					if (pSentEnc)
					{
						s_heatManager.forcedEncounter = pSentEnc->pEncounter;
					}
				}

				s_heatManager.iHeatTierSpawnOrderIndex++;
				s_heatManager.timeSpawnPeriod = ABS_TIME;
				if (!bFoundSpawn)
				{	// couldn't find anything, lets try again soon
					F32 fTimeAdjust = pHeatLevelDef->fSpawnPeriod - pHeatLevelDef->fSpawnFailedResetTime;
					s_heatManager.timeSpawnPeriod -= SEC_TO_ABS_TIME(fTimeAdjust);
				}
			}
		}
	}

}
static void aiMastermindHeat_UpdateTrackedEncounters()
{
	S32 i;	
	
	for (i = eaSize(&s_heatManager.eaSentEncounters)-1; i >= 0; --i)
	{
		MMSentEncounter *pSentEnc = s_heatManager.eaSentEncounters[i];
		bool bCritterSeekingCombat = false;

		AITeam *pTeam = MMSentEncounter_getAITeam(pSentEnc);
		if (!pTeam)
		{	// 
			if (s_heatManager.forcedEncounter == pSentEnc->pEncounter)
				s_heatManager.forcedEncounter = NULL;

			eaRemoveFast(&s_heatManager.eaSentEncounters, i);
			free (pSentEnc);
			continue;
		}

		if (pSentEnc->leashing)
		{
			if (pTeam->combatState != AITEAM_COMBAT_STATE_LEASH)
			{
				eaRemoveFast(&s_heatManager.eaSentEncounters, i);
				_destroySentEncounter(pSentEnc, pTeam);
			}
			continue;
		}

		if (pSentEnc->pathFailed)
		{
			Entity *pPlayerEnt = entFromEntityRef(PARTITION_UNINITIALIZED, pSentEnc->erPlayer);
			if (!pPlayerEnt || !aiMastermindHeat_SendTeamAtPlayer(pSentEnc, pTeam, pPlayerEnt))
			{
				eaRemoveFast(&s_heatManager.eaSentEncounters, i);
				_destroySentEncounter(pSentEnc, pTeam);
				continue;
			}
			else
			{
				pSentEnc->pathFailed = false;
			}
		}

		if (pTeam->calculatedSpawnPos)
		{
			copyVec3(pTeam->spawnPos, pSentEnc->vSpawnPosition);
		}

		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
		{
			Entity *pEntity = pMember->memberBE;
			if (!aiIsEntAlive(pEntity))
				continue;

			if (aiIsInCombat(pEntity))
			{
				bCritterSeekingCombat = true;
				break;
			}
			else
			{	// not in combat, check if the ent is moving towards a player
				EntityRef erMoveTarget = aiMovementGetMovementTargetEnt(pEntity, pEntity->aibase);
				if (erMoveTarget)
				{
					Entity *pMoveTargetEnt = entFromEntityRef(PARTITION_UNINITIALIZED, erMoveTarget);
					if (pMoveTargetEnt && entIsPlayer(pMoveTargetEnt))
					{	
						if (aiIsEntAlive(pMoveTargetEnt))
						{	
							bCritterSeekingCombat = true;
							break;
						}
						else
						{	// player we were following is dead, find another player that hasn't died recently. 
							// otherwise go back to spawn pos
							Vec3 vPos;
							Entity *pOtherPlayer;
							entGetPos(pEntity, vPos);
							
							pOtherPlayer = aiMastermindHeat_FindClosestPlayer(vPos, 5.f);
							if (pOtherPlayer)
							{
								if (!aiMastermindHeat_SendTeamAtPlayer(pSentEnc, pTeam, pOtherPlayer))
								{
									pSentEnc->pathFailed = true;
								}

								bCritterSeekingCombat = true;
							}
							break;
						}
					}
				}
			}
		
		}
		FOR_EACH_END


		if (!bCritterSeekingCombat)
		{
			aiMastermindHeat_ReturnAmbushTeamToSpawnPos(pSentEnc, pTeam);

			// this isn't 100% accurate as more than one than one encounter could have been spawned
			// from this encounter 
			if (s_heatManager.forcedEncounter == pSentEnc->pEncounter)
				s_heatManager.forcedEncounter = NULL;
		}
	}
}	


// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_AddPlayer(Entity *e)
{
	if (!aiMastermindHeat_GetPlayerHeatInfo(e))
	{
		MMHeatPlayer *pMMPlayer;
		pMMPlayer = calloc(1, sizeof(MMHeatPlayer));

		pMMPlayer->erPlayer = entGetRef(e);
		pMMPlayer->timeAtLastRoomChange = ABS_TIME;

		aiExternGetHealth(e, &pMMPlayer->fLastHealth, NULL);

		entGetPos(e, pMMPlayer->vLastPlayerPos);
		copyVec3(pMMPlayer->vLastPlayerPos, pMMPlayer->vIdlePlayerPos);

		eaPush(&s_heatManager.eaPlayerHeatInfo, pMMPlayer);
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindHeat_FindAndAddAllPlayers()
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity* e;
	while(e = EntityIteratorGetNext(iter))
	{
		aiMastermindHeat_AddPlayer(e);
	}
}


// ------------------------------------------------------------------------------------------------------------------
void aiMastermindHeat_ResetHeatMinMax()
{
	AIMMHeatDef* pDef = aiMastermindHeat_GetDef();
	if (pDef)
	{
		s_heatManager.fHeatLevelMin = 0.f;
		s_heatManager.fHeatLevelMax = (F32)eaSize(&pDef->eaHeatLevels);
	}
	else
	{
		s_heatManager.fHeatLevelMin = 0.f;
		s_heatManager.fHeatLevelMax = 0.f;
	}
}

// ---------------------------------------------------------------
void aiMastermindHeat_FirstTickInit()
{
	aiMastermindHeat_CalculateRoomDepths();
	aiMastermindHeat_FindPortalGates();
	s_heatManager.timeSpawnPeriod = ABS_TIME;
}

// ---------------------------------------------------------------
void aiMastermindHeat_Initialize()
{
	aiMastermindHeat_FindAndAddAllPlayers();
	aiMastermindHeat_ResetHeatMinMax();
}

// ---------------------------------------------------------------
void aiMastermindHeat_OnMapUnload()
{
	// go through all the rooms and clear the tracked encounters
	FOR_EACH_IN_EARRAY(s_heatManager.eaRooms, Room, pRoom)
		if (pRoom->ai_room)
		{
			eaClear(&pRoom->ai_room->eaMMEncounters);
			eaClear(&pRoom->ai_room->eaStaticEncounters);
		}
	FOR_EACH_END

	s_heatManager.timeLastSpawned = ABS_TIME;
	s_heatManager.timeSpawnPeriod = ABS_TIME;
	s_heatManager.timeLastHeatUpdate = ABS_TIME;
	
	s_heatManager.forcedEncounter = NULL;

	eaClearEx(&s_heatManager.eaSentEncounters, NULL);
	eaClear(&s_heatManager.eaCachedGates);
	eaDestroy(&s_heatManager.eaRoomConns);

	
}


// ------------------------------------------------------------------------------------------------------------------
static int findGateInteractables(GameInteractable *p, void *pData)
{
	GameInteractable ***peaGates = (GameInteractable***)pData;
	WorldGateInteractionProperties *pProperty = NULL;
	if (interactable_HasGateProperties(p->pWorldInteractable, &pProperty))
	{	// ignore doors that auto-open
		if (!pProperty->bVolumeTriggered)
			eaPush(peaGates, p);
	}

	return 0;
}

// ---------------------------------------------------------------
void aiMastermindHeat_OnMapLoad()
{
	// go through all the rooms and clear the tracked encounters
	FOR_EACH_IN_EARRAY(s_heatManager.eaRooms, Room, pRoom)
		if (pRoom->ai_room)
		{
			eaClear(&pRoom->ai_room->eaMMEncounters);
			eaClear(&pRoom->ai_room->eaStaticEncounters);
		}
	FOR_EACH_END

	// the room connections are added before the encounters are initialized after map load
	FOR_EACH_IN_EARRAY(s_heatManager.eaRooms, Room, pRoom)
		aiMastermindHeat_FindEncountersInRoom(pRoom);
	FOR_EACH_END

	s_heatManager.timeLastSpawned = ABS_TIME;
	s_heatManager.timeSpawnPeriod = ABS_TIME;
	
	s_heatManager.timeLastHeatUpdate = ABS_TIME;

	eaClear(&s_heatManager.eaCachedGates);
	interactable_ExecuteOnEachInteractable(findGateInteractables, &s_heatManager.eaCachedGates);

	aiMastermindHeat_ResetHeatMinMax();
}

// ---------------------------------------------------------------
void aiMastermindHeat_Shutdown()
{
	FOR_EACH_IN_EARRAY(s_heatManager.eaRooms, Room, pRoom)
		aiMastermindHeat_DestroyRoomData(pRoom);
	FOR_EACH_END

	eaDestroy(&s_heatManager.eaRooms);
	eaDestroy(&s_heatManager.eaRoomConns);
	eaDestroy(&s_heatManager.eaPlayerOccupiedRooms);
	eaDestroyEx(&s_heatManager.eaPlayerHeatInfo, NULL);
	eaDestroyEx(&s_heatManager.eaSentEncounters, NULL);
	eaDestroy(&s_heatManager.eaCachedGates);

}

void aiMastermindHeat_Update(AIMastermindDef *pDef)
{
	AIMMHeatDef *pHeatDef = pDef->pHeatDef;
	MMHeatPlayerUpdateStats	stats = {0};

	if (eaSize(&s_heatManager.eaPlayerHeatInfo) == 0)
		return;

	aiMastermindHeat_UpdateAllPlayerInfo(&stats);
	aiMastermindHeat_UpdateTeamStats(&stats);
	aiMastermindHeat_UpdateTrackedEncounters();

	if (s_heatManager.isHeatEnabled)
	{
		if (ABS_TIME_SINCE(s_heatManager.timeLastHeatUpdate) > SEC_TO_ABS_TIME(1.f))
		{
			aiMastermindHeat_HeatUpdate(pHeatDef);
			s_heatManager.timeLastHeatUpdate = ABS_TIME;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------

// AUTO COMMANDS
// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiMastermindEnableHeat(S32 enable)
{
	if (s_heatManager.isHeatEnabled == (U32)!!enable)
		return; // no change

	s_heatManager.isHeatEnabled = !!enable;
	aiMastermind_DebugPrint("Heat %s", (s_heatManager.isHeatEnabled) ? "ENABLED" : "DISABLED");

}


AUTO_COMMAND;
void aiMastermindSetHeatLevel(F32 heat)
{
	aiMastermindHeat_SetHeatLevel(heat, "Auto-command");
}



AUTO_COMMAND;
void aiMastermindDumpHeatInfo()
{
	AIMMHeatLevelDef *pHeatLevelDef;
	F32 fCurSpawnTime;
	AIMMHeatDef* pDef = aiMastermindHeat_GetDef();

	printf("Mastermind Heat Info:\n");

	if (!pDef)
	{
		printf("Could not get mastermind def.\n");
		return ;
	}

	pHeatLevelDef = aiMastermindHeat_GetCurrentHeatLevelDef(pDef);
	if (!pHeatLevelDef)
	{
		printf("Could not get current heat level.\n");
		return;
	}

	// heat level
	printf("Heat Level: %.2f\n", s_heatManager.fHeatLevel);
	printf("Heat Level Min/Max: %.2f / %.2f\n", s_heatManager.fHeatLevelMin, s_heatManager.fHeatLevelMax);

	fCurSpawnTime = aiMastermindHeat_GetCurrentSpawnTime(pHeatLevelDef);
	printf("Current Spawn Time: %.2f\tTime To spawn: %.2f\n\n", fCurSpawnTime, s_heatManager.fLastSpawnTimeLeft);

}


// ------------------------------------------------------------------------------------------------------------------
// heat level expr

// Function to turn on/off the heat level spawning
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindEnableHeat);
ExprFuncReturnVal exprFuncMastermindEnableHeat(S32 enable)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindEnableHeat(enable);
	return ExprFuncReturnFinished;
}

// Function to set the heat level to a given number
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSetHeatLevel);
ExprFuncReturnVal exprFuncMastermindSetHeatLevel(F32 heatLevel)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_SetHeatLevel(heatLevel, "Via-Expression");
	return ExprFuncReturnFinished;
}

// Function to add to the heat level, will not go past min/max
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindAddHeat);
ExprFuncReturnVal exprFuncMastermindAddHeat(F32 heat)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_AddHeat(ABS(heat), "Via-Expression");
	return ExprFuncReturnFinished;
}

// Function to add to the heat level, will not go past min/max
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSubtractHeat);
ExprFuncReturnVal exprFuncMastermindSubtractHeat(F32 heat)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_AddHeat(-ABS(heat), "Via-Expression");
	return ExprFuncReturnFinished;

}

// Function to add to the heat level, will not go past min/max
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetHeatValue);
F32 exprFuncMastermindGetHeatValue()
{
	if (s_heatManager.isEnabled == false)
		return 0.f;

	return s_heatManager.fHeatLevel;

}

// ------------------------------------------------------------------------------------------------------------------
// 


// delays the next spawn by X seconds. 
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindDelayNextSpawn);
ExprFuncReturnVal exprFuncMastermindDelaySpawnPeriod(F32 seconds)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_AdjustSpawnPeriod(ABS(seconds));
	return ExprFuncReturnFinished;
}

// makes the next wave come sooner
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindHastenNextSpawn);
ExprFuncReturnVal exprFuncMastermindHastenNextSpawn(F32 seconds)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_AdjustSpawnPeriod(-ABS(seconds));
	return ExprFuncReturnFinished;
}

// resets the current spawn timer
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindResetSpawnPeriod);
ExprFuncReturnVal exprFuncMastermindResetSpawnPeriod()
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_ResetSpawnPeriod();
	return ExprFuncReturnFinished;
}

// sets the next spawn time to the given number of seconds, then reverts back to the normal spawn period.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSetNextSpawnTime);
ExprFuncReturnVal exprFuncMastermindSetNextSpawnTime(F32 seconds)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_SetNextSpawnTime(seconds);
	return ExprFuncReturnFinished;
}


// ------------------------------------------------------------------------------------------------------------------
// query exprs


// Counts the number of critters that are either in combat or seeking out the player 
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetNumNearbyCrittersForCombat);
int exprFuncMastermindGetNumNearbyCrittersForCombat(ACMD_EXPR_PARTITION iPartitionIdx)
{
	if (s_heatManager.isEnabled == false)
		return 0;

	{
		MMNearbyCritterInfo count = {0};

		aiMastermindHeat_CountNearbyCritters(iPartitionIdx, &count);

		return count.numForCombat;
	}
}

// Counts the number of critters that are not in combat and not seeking out the player
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetNumNearbyIdleCritters);
int exprFuncMastermindGetNumNearbyIdleCritters(ACMD_EXPR_PARTITION iPartitionIdx)
{
	if (s_heatManager.isEnabled == false)
		return 0;

	{
		MMNearbyCritterInfo count = {0};

		aiMastermindHeat_CountNearbyCritters(iPartitionIdx, &count);

		return count.numAmbient;
	}
}

// Spawns critters from a nearby encounter from the given encounter group name
// returns 1 if succeeded, 0 otherwise
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSpawnCrittersNearby);
int exprFuncMastermindSpawnCrittersNearby(ACMD_EXPR_PARTITION iPartitionIdx, const char* pchEncounterGroupName)
{
	if (s_heatManager.isEnabled == false)
	{
		return false;
	}

	if (!aiMastermindHeat_IsAnyPlayerAlive())
		return false;

	return aiMastermindHeat_SpawnCrittersForPlayers(iPartitionIdx, pchEncounterGroupName);
}

// Spawns critters from a nearby encounter from the given tier as defined on the mastermind def
// returns 1 if succeeded, 0 otherwise
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSpawnCrittersNearbyByTier); 
int exprFuncMastermindSpawnCrittersNearbyByTier(ACMD_EXPR_PARTITION iPartitionIdx, S32 tier)
{
	if (s_heatManager.isEnabled == false)
	{
		return false;
	}

	if (!aiMastermindHeat_IsAnyPlayerAlive())
		return false;

	return aiMastermindHeat_SpawnCrittersForPlayersByTier(iPartitionIdx, tier);
}


/*
// Sends a nearby encounter at the players, 
// returns 1 if succeeded, 0 otherwise
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSendNearbyEncounterAtPlayers);
int exprFuncMastermindSendNearbyEncounterAtPlayers(S32 tier)
{
if (s_heatManager.isEnabled == false)
{
return false;
}

if (!aiMastermindHeat_IsAnyPlayerAlive())
return false;

return aiMastermindHeat_SendNearbyEncounterAtPlayers();
}
*/

// Returns the team's health as a percent of the total health of all the members
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetTeamHealthPercent);
F32 exprFuncMastermindGetTeamHealthPercent()
{
	if (s_heatManager.isEnabled == false)
	{
		return 0.f;
	}
	return s_heatManager.teamStats.fTeamHealthPercent * 100.f;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetNumDeadPlayers);
S32 exprFuncMastermindGetNumDeadPlayers()
{
	if (s_heatManager.isEnabled == false)
	{
		return 0.f;
	}
	return s_heatManager.teamStats.numDead;
}

// Returns the average health percent of the members on the team
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetTeamAverageHealth);
F32 exprFuncMastermindGetTeamAverageHealth()
{
	if (s_heatManager.isEnabled == false)
	{
		return 0.f;
	}
	return s_heatManager.teamStats.fAvgHealthPercent;
}

// Returns the time in seconds since the last death of a player. 
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetTimeSinceLastDeath);
F32 exprFuncMastermindGetTimeSinceLastDeath()
{
	if (s_heatManager.isEnabled == false)
	{
		return FLT_MAX;
	} 

	if (s_heatManager.teamStats.timeOfLastDeath)
	{
		U64 timeSince = ABS_TIME_SINCE(s_heatManager.teamStats.timeOfLastDeath);
		return ABS_TIME_TO_SEC(timeSince);
	}

	return FLT_MAX;
}

// Returns the time in seconds since the team was last in combat 
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetTimeSinceLastCombat);
F32 exprFuncMastermindGetTimeSinceLastCombat()
{
	if (s_heatManager.isEnabled == false)
	{
		return FLT_MAX;
	}

	if (s_heatManager.teamStats.timeAtLastCombat)
	{
		U64 timeSince = ABS_TIME_SINCE(s_heatManager.teamStats.timeAtLastCombat);
		return ABS_TIME_TO_SEC(timeSince);
	}

	return FLT_MAX;
}

// Returns the time in seconds since any player entered a new room 
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindGetTimeSinceEnteredNewRoom);
F32 exprFuncMastermindGetTimeSinceEnteredNewRoom()
{
	if (s_heatManager.isEnabled == false)
	{
		return FLT_MAX;
	}

	if (s_heatManager.teamStats.timeAtLastNewRoom)
	{
		U64 timeSince = ABS_TIME_SINCE(s_heatManager.teamStats.timeAtLastNewRoom);
		return ABS_TIME_TO_SEC(timeSince);
	}

	return FLT_MAX;
}

// sets the min/max of the heat level
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MastermindSetHeatLevelMinMax);
ExprFuncReturnVal exprFuncMastermindSetHeatLevelMinMax(F32 heatMin, F32 heatMax)
{
	if (s_heatManager.isEnabled == false)
		return ExprFuncReturnError;

	aiMastermindHeat_SetHeatLevelMinMax(heatMin, heatMax);
	return ExprFuncReturnFinished;
}
