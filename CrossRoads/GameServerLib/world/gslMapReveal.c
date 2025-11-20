#include "earray.h"
#include "WorldGrid.h"
#include "StringCache.h"
#include "Entity.h"
#include "MapRevealCommon.h"
#include "gslMapReveal.h"
#include "Entity_h_ast.h"
#include "RoomConn.h"
#include "MapRevealCommon_h_ast.h"
#include "Player.h"
#include "RegionRules.h"
#include "MapSnap.h"
#include "Player_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

int gMapRevealPlayerDist = MAPREVEAL_PLAYER_DISTANCE;
AUTO_CMD_INT(gMapRevealPlayerDist, SetMapRevealDistance);

static WorldRegion *GetRegion(Entity *pEnt, WorldRegion *pRegion)
{
	if (pEnt && !pRegion)
	{
		Vec3 v3Pos;
		entGetPos(pEnt, v3Pos);
		pRegion = worldGetWorldRegionByPos(v3Pos);
	}
	return pRegion;
}

static void MapRevealInfoClear(MapRevealInfo *pInfo)
{
	pInfo->eType = kMapRevealType_All;
	pInfo->uiRoomCount = 0;
	setVec3(pInfo->v3RegionMin, 1e6, 1e6, 1e6);
	setVec3(pInfo->v3RegionMax, -1e6, -1e6, -1e6);
	eaiClear(&pInfo->eaiRevealed);
}

static void FillRevealInfoForRegion(MapRevealInfo *pInfo, WorldRegion *pRegion)
{
	RoomConnGraph *pGraph = worldRegionGetRoomConnGraph(pRegion);
	Vec3 v3Min = {1e6, 1e6, 1e6};
	Vec3 v3Max = {-1e6, -1e6, -1e6};
	char achName[MAPREVEAL_MAX_KEY] = "";
	RegionRules * pRules = getRegionRulesFromRegion(pRegion);

	if (!(pInfo && pRegion))
		return;

	if (pInfo->pchRevealedString && eaiSize(&pInfo->eaiRevealed) == 0)
		ea32FromZipString(&pInfo->eaiRevealed, pInfo->pchRevealedString, true);

	PERFINFO_AUTO_START_FUNC();
	// If they forgot to set up rooms for this map, just give the player everything,
	// since we won't be able to back-reveal when they finally do.
	pInfo->eType = kMapRevealType_All;

	mapSnapRegionGetMapBounds(pRegion, v3Min, v3Max);
	mapRevealGetZoneAndRegionName(pRegion, SAFESTR(achName));
	pInfo->pchName = allocAddString(achName);
	copyVec3(v3Min, pInfo->v3RegionMin);
	copyVec3(v3Max, pInfo->v3RegionMax);
	pInfo->fGroundFocusHeight = worldRegionGetMapSnapData(pRegion)->fGroundFocusHeight;

	if (pGraph && eaSize(&pGraph->rooms) > 0)
	{
		if (pRules && pRules->bUseGridMapReveal)
			pInfo->eType = kMapRevealType_Grid;
		else if (pRules && pRules->bUseRoomMapReveal)
			pInfo->eType = kMapRevealType_EnteredRooms;			
		else if (v3Max[0] - v3Min[0] < MAPREVEAL_OUTSIDE_SIZE && v3Max[2] - v3Min[2] < MAPREVEAL_OUTSIDE_SIZE)
			pInfo->eType = kMapRevealType_EnteredRooms;
		else
			pInfo->eType = kMapRevealType_Grid;
		pInfo->uiRoomCount = eaSize(&pGraph->rooms);
		if (pInfo->eType == kMapRevealType_Grid)
		{
			S32 iTotalElems = (SQR(MAPREVEAL_MAX_BITS_PER_DIMENSION) / (sizeof(pInfo->eaiRevealed[0]) * 8));
			eaiSetSize(&pInfo->eaiRevealed, iTotalElems);
		}
		else if (pInfo->eType == kMapRevealType_EnteredRooms)
		{
			eaiSetSize(&pInfo->eaiRevealed, ((pInfo->uiRoomCount - 1) / sizeof(pInfo->eaiRevealed[0])) + 1);
		}
	}
	PERFINFO_AUTO_STOP();
}

#define NEARVEC3(v1, v2) (nearf((v1)[0], (v2)[0]) && nearf((v1)[1], (v2)[1]) && nearf((v1)[2], (v2)[2]))

static bool MapRevealInfoIsAccurate(MapRevealInfo *pInfo, WorldRegion *pRegion)
{
	Vec3 v3Min = {1e6, 1e6, 1e6};
	Vec3 v3Max = {-1e6, -1e6, -1e6};
	RoomConnGraph *pGraph = worldRegionGetRoomConnGraph(pRegion);
	RegionRules * pRules = getRegionRulesFromRegion(pRegion);
	MapRevealType eType;

	if (!pInfo)
		return false;
	if (!(pGraph && pRegion && eaSize(&pGraph->rooms) > 0))
		return pInfo->eType == kMapRevealType_All;

	mapSnapRegionGetMapBounds(pRegion, v3Min, v3Max);

	if (pInfo->uiRoomCount != (U32)eaSize(&pGraph->rooms))
		return false;

	if (!(NEARVEC3(v3Min, pInfo->v3RegionMin) && NEARVEC3(v3Max, pInfo->v3RegionMax)))
		return false;

	// If we ever make ortho skew configurable per map, we really should get rid of these 2 variables (rename them to defaultblah, probably), and
	// change this check to check whether this actual map is using ortho skew
	if (gfCurrentMapOrthoSkewX || gfCurrentMapOrthoSkewZ)
	{
		F32 fFocusHeight = worldRegionGetMapSnapData(pRegion)->fGroundFocusHeight;
		if (fFocusHeight != pInfo->fGroundFocusHeight)
			return false;
	}

	if (pRules && pRules->bUseGridMapReveal)
		eType = kMapRevealType_Grid;
	else if (pRules && pRules->bUseRoomMapReveal)
		eType = kMapRevealType_EnteredRooms;			
	else if (v3Max[0] - v3Min[0] < MAPREVEAL_OUTSIDE_SIZE && v3Max[2] - v3Min[2] < MAPREVEAL_OUTSIDE_SIZE)
		eType = kMapRevealType_EnteredRooms;
	else
		eType = kMapRevealType_Grid;

	if (eType == kMapRevealType_EnteredRooms)
	{
		return (pInfo->eType == kMapRevealType_EnteredRooms
			&& ((U32)eaiSize(&pInfo->eaiRevealed) == ((pInfo->uiRoomCount - 1) / sizeof(pInfo->eaiRevealed[0])) + 1));
	}
	else
	{
		return (pInfo->eType == kMapRevealType_Grid
			&& (eaiSize(&pInfo->eaiRevealed) * sizeof(pInfo->eaiRevealed[0]) * 8) == SQR(MAPREVEAL_MAX_BITS_PER_DIMENSION));
	}
}

// Return the MapRevealInfo for the given region, creating it if it does not exist.
MapRevealInfo *gslMapRevealInfoGetOrCreateByRegion(Entity *pEnt, WorldRegion *pRegion)
{
	MapRevealInfo *pInfo = NULL;

	if (gConf.bDisableMapRevealData)
	{
		return NULL;
	}

	pRegion = GetRegion(pEnt, pRegion);
	// TODO: If this is too slow, we can cache the current region / accuracy at region-entry time.
	if (pEnt && pEnt->pPlayer && pRegion && !((pInfo = mapRevealInfoGetByRegion(pEnt, pRegion)) && MapRevealInfoIsAccurate(pInfo, pRegion)))
	{
		if (!pInfo)
			pInfo = StructCreate(parse_MapRevealInfo);
		FillRevealInfoForRegion(pInfo, pRegion);
		if (!pEnt->pPlayer->pUI)
			pEnt->pPlayer->pUI = StructCreate(parse_PlayerUI);
		eaPush(&pEnt->pPlayer->pUI->eaMapRevealInfos, pInfo);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}

	return pInfo;
}

//This is called everytime a player enters a volume. It makes sure that gslMapRevealCurrentLocation
//gets called immediately on entering a new room instead of after walking 20 feet into it.
void gslMapRevealEnterRoomVolumeCB(Entity *pEnt){
	if(pEnt->pPlayer->pUI){
		//force reveal
		Vec3 vSentinal = {-1e6, -1e6, -1e6};
		copyVec3(vSentinal, pEnt->pPlayer->pUI->vLastMapRevealPos);
		gslMapRevealCurrentLocation(pEnt);
	}
}


// Mark the entity's current location as revealed. Needs to be called periodically,
// since there's no events for non-room-based movement.
// checks if reveal is needed, then calls gslMapRevealCircle()
void gslMapRevealCurrentLocation(Entity *pEnt)
{
	if (!gConf.bDisableMapRevealData && pEnt && pEnt->pPlayer)
	{
		Vec3 v3Pos;
		
		// Only do work if player has moved at least 20 feet since last time we did map reveal or if this is a new region.
		entGetPos(pEnt, v3Pos);
		if (!pEnt->pPlayer->pUI || (distance3XZSquared(v3Pos, pEnt->pPlayer->pUI->vLastMapRevealPos) > 400) ) 
		{
			if (pEnt->pPlayer->pUI) 
			{
				copyVec3(v3Pos, pEnt->pPlayer->pUI->vLastMapRevealPos);
			}
			gslMapRevealCircle(pEnt, NULL, v3Pos, gMapRevealPlayerDist);
		}
	}
}

// sets up for mapRevealGetBits, calls it, sets dirty bits on player.
void gslMapRevealCircle(Entity *pEnt, WorldRegion *pRegion, Vec3 v3Position, F32 fRadius)
{
	if (!gConf.bDisableMapRevealData)
	{
		MapRevealInfo *pInfo;
		RoomConnGraph *pGraph;

		PERFINFO_AUTO_START_FUNC();

		pRegion = GetRegion(pEnt, pRegion);

		PERFINFO_AUTO_START("gslMapRevealInfoGetOrCreateByRegion", 1);
		pInfo = gslMapRevealInfoGetOrCreateByRegion(pEnt, pRegion);
		PERFINFO_AUTO_STOP();

		pGraph = worldRegionGetRoomConnGraph(pRegion);

		if (pInfo && pGraph)
		{
			bool bDirty;

			PERFINFO_AUTO_START("mapRevealGetBits", 1);
			bDirty = mapRevealGetBits(pInfo, v3Position, fRadius, &pGraph->rooms, NULL, true);
			PERFINFO_AUTO_STOP();

			if (bDirty)
			{
				PERFINFO_AUTO_START("Compression of map reveal data", 1);
				ea32ToZipString(&pInfo->eaiRevealed, &pInfo->pchRevealedString, true);
				entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
				PERFINFO_AUTO_STOP();
			}
		}

		PERFINFO_AUTO_STOP();
	}
}


//reveals parts of the map, based on the MapRevealType pInfo->eType.
//returns whether anything new was revealed.
bool mapRevealGetBits(MapRevealInfo *pInfo, Vec3 v3Pos, F32 fRadius, Room ***peaRooms, S32 **peaiBits, bool bFillBit)
{
	bool bChanged = false;
	S32 i = 0;
	switch (pInfo->eType)
	{
	case kMapRevealType_EnteredRooms:
		if (peaRooms)
		{
			for (i = 0; i < eaSize(peaRooms); i++)
			{
				// A room is visible if
				//  - We are within its Y bounds (never reveal things on other floors)
				//  - We are within the room
				//  - radius used is 0 for rooms (plus tolerance)
				if (v3Pos[1] >= (*peaRooms)[i]->bounds_min[1] - MAPREVEAL_TOLERANCE
					&& v3Pos[1] <= (*peaRooms)[i]->bounds_max[1] + MAPREVEAL_TOLERANCE
					&& distanceToBoxSquared((*peaRooms)[i]->bounds_min, (*peaRooms)[i]->bounds_max, v3Pos) <= 0 + MAPREVEAL_TOLERANCE * MAPREVEAL_TOLERANCE
					)
				{
					if (!TSTB(pInfo->eaiRevealed, i))
					{
						MAPREVEAL_PRINTF("Revealing %s:\tRoom %d", pInfo->pchName, i);
						bChanged = true;
						if (bFillBit)
							SETB(pInfo->eaiRevealed, i);
						if (peaiBits)
							eaiPush(peaiBits, i);
					}
				}
			}
		}
		break;
	case kMapRevealType_Grid:
		{
			F32 fWidth;
			F32 fHeight;
			F32 fFeetPerBitX;
			F32 fFeetPerBitZ;
			F32 fBaseX;
			F32 fBaseZ;
			S32 iCellMinX;
			S32 iCellMaxX;
			S32 iCellMinZ;
			S32 iCellMaxZ;
			S32 iX;
			S32 iZ;
			Vec2 vMin,vMax;
			Vec2 vSkew = {gfCurrentMapOrthoSkewX,gfCurrentMapOrthoSkewZ};
			F32 fFocusHeight = pInfo->fGroundFocusHeight;
			WorldRegion * pRegion = worldGetWorldRegionByPos(v3Pos);

			if (pRegion)
			{
				v3Pos[0] += gfCurrentMapOrthoSkewX*(v3Pos[1]-fFocusHeight);
				v3Pos[2] += gfCurrentMapOrthoSkewZ*(v3Pos[1]-fFocusHeight);
			}

			mapSnapGetExtendedBounds(pInfo->v3RegionMin,pInfo->v3RegionMax,vSkew,fFocusHeight,vMin,vMax);

			fWidth = vMax[0] - vMin[0];
			fHeight = vMax[1] - vMin[1];
			fFeetPerBitX = fWidth / MAPREVEAL_MAX_BITS_PER_DIMENSION;
			fFeetPerBitZ = fHeight / MAPREVEAL_MAX_BITS_PER_DIMENSION;
			fBaseX = v3Pos[0] - vMin[0];
			fBaseZ = v3Pos[2] - vMin[1];
			iCellMinX = floor((fBaseX - fRadius) / fFeetPerBitX);
			iCellMinZ = floor((fBaseZ - fRadius) / fFeetPerBitZ);
			iCellMaxX = ceil((fBaseX + fRadius) / fFeetPerBitX);
			iCellMaxZ = ceil((fBaseZ + fRadius) / fFeetPerBitZ);
			iCellMinX = CLAMP(iCellMinX, 0, MAPREVEAL_MAX_BITS_PER_DIMENSION - 1);
			iCellMinZ = CLAMP(iCellMinZ, 0, MAPREVEAL_MAX_BITS_PER_DIMENSION - 1);
			iCellMaxX = CLAMP(iCellMaxX, 0, MAPREVEAL_MAX_BITS_PER_DIMENSION - 1);
			iCellMaxZ = CLAMP(iCellMaxZ, 0, MAPREVEAL_MAX_BITS_PER_DIMENSION - 1);
			for (iX = iCellMinX; iX <= iCellMaxX; iX++)
			{
				for (iZ = iCellMinZ; iZ <= iCellMaxZ; iZ++)
				{
					S32 iBit = MAPREVEAL_GRID_BIT(iX, iZ);
					Vec3 v3BitPosMin = {vMin[0] + iX * fFeetPerBitX, v3Pos[1] - MAPREVEAL_TOLERANCE, vMin[1] + fFeetPerBitZ * iZ + fFeetPerBitZ};
					Vec3 v3BitPosMax = {vMin[0] + iX * fFeetPerBitX + fFeetPerBitX, v3Pos[1] +MAPREVEAL_TOLERANCE, vMin[1] + fFeetPerBitZ * iZ + fFeetPerBitZ};
					if (iBit >= 0 && (U32)iBit < eaiSize(&pInfo->eaiRevealed) * sizeof(pInfo->eaiRevealed[0]) * 8 && distanceToBoxSquared(v3BitPosMin, v3BitPosMax, v3Pos) < fRadius * fRadius)
					{
						if (!TSTB(pInfo->eaiRevealed, iBit))
						{
							MAPREVEAL_PRINTF("Revealing %s:\tCell %d,%d\tBit %d", pInfo->pchName, iX, iZ, iBit);
							bChanged = true;
							if (bFillBit)
								SETB(pInfo->eaiRevealed, iBit);
							if (peaiBits)
								eaiPush(peaiBits, iBit);
						}
					}
				}
			}
		}
		break;
	default:
		break;
	}
	return bChanged;
}


void gslMapRevealReset(Entity *pEnt, WorldRegion *pRegion)
{
	MapRevealInfo *pInfo = mapRevealInfoGetByRegion(pEnt, pRegion);
	if (pInfo)
	{
		S32 i = eaIndexedFind(&pEnt->pPlayer->pUI->eaMapRevealInfos, pInfo);
		eaRemove(&pEnt->pPlayer->pUI->eaMapRevealInfos, i);
		StructDestroySafe(parse_MapRevealInfo, &pInfo);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Reset the current map's reveal state.
AUTO_COMMAND ACMD_NAME("MapRevealResetCurrent") ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_SERVERONLY;
void gslCmdMapRevealResetCurrent(Entity *pEnt)
{
	gslMapRevealReset(pEnt, NULL);
}

// Reset the reveal state of all maps.
AUTO_COMMAND ACMD_NAME("MapRevealResetAll") ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_SERVERONLY;
void gslCmdMapRevealResetAll(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		// can't eaDestroy because we need to keep the indexing flags
		eaClearStruct(&pEnt->pPlayer->pUI->eaMapRevealInfos, parse_MapRevealInfo);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}
