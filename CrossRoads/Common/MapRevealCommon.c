#include "WorldGrid.h"
#include "Entity.h"
#include "Expression.h"
#include "MapRevealCommon.h"
#include "Player.h"
#include "RoomConn.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

MapRevealInfo *mapRevealInfoGetByRegion(Entity *pEnt, WorldRegion *pRegion)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->eaMapRevealInfos)
	{
		if (!pRegion)
		{
			Vec3 v3Pos;
			entGetPos(pEnt, v3Pos);
			pRegion = worldGetWorldRegionByPos(v3Pos);
		}
		if (pRegion)
		{
			char achName[MAPREVEAL_MAX_KEY];

			// this is a goofy place to do this, but this code is evolving (i.e., we may someday make this configurable per-map), and I think this will work for now.
			mapSnapUpdateRegion(pRegion);

			mapRevealGetZoneAndRegionName(pRegion, SAFESTR(achName));
			return eaIndexedGetUsingString(&pEnt->pPlayer->pUI->eaMapRevealInfos, achName);
		}
	}
	return NULL;
}

//puts "zoneName.regionName" into strBufferOut.
void mapRevealGetZoneAndRegionName(WorldRegion *pRegion, char *strBufferOut, int strBufferOut_size)
{
	const char* regionName = worldRegionGetRegionName(pRegion);
	const char* zoneName = zmapInfoGetPublicName(zmapGetInfo(worldRegionGetZoneMap(pRegion)));
	if (!regionName){
		regionName = MAPREVEAL_DEFAULT_REGION_NAME;
	}
	if (!zoneName){
		zoneName = MAPREVEAL_DEFAULT_ZONE_NAME;
	}
	sprintf_s(SAFESTR2(strBufferOut), "%s.%s", regionName, zoneName);
}


F32 mapRevealGetPercentage(MapRevealInfo *pInfo)
{
	if (pInfo)
	{
		U32 iBits = eaiSize(&pInfo->eaiRevealed) * sizeof(pInfo->eaiRevealed[0]) * 8;
		U32 iCount = 0;
		U32 i;
		switch (pInfo->eType)
		{
		case kMapRevealType_EnteredRooms:
			MIN1(iBits, pInfo->uiRoomCount);
			break;
		case kMapRevealType_Grid:
			MIN1(iBits, SQR(MAPREVEAL_MAX_BITS_PER_DIMENSION));
			break;
		default:
			return 1.f;
		}

		for (i = 0; i < iBits; i++)
		{
			if (TSTB(pInfo->eaiRevealed, i))
				iCount++;
		}

		return (F32)iCount / (F32)iBits;
	}
	return 0.f;
}

bool mapRevealHasBeenRevealed(MapRevealInfo *pInfo, Vec3 v3Pos, F32 fRadius, Room ***peaRooms)
{
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
				//  - We are within the provided radius of its 3D box
				if (v3Pos[1] >= (*peaRooms)[i]->bounds_min[1]
					&& v3Pos[1] <= (*peaRooms)[i]->bounds_max[1]
					&& distanceToBoxSquared((*peaRooms)[i]->bounds_min, (*peaRooms)[i]->bounds_max, v3Pos) <= fRadius * fRadius
				)
				{
					if (TSTB(pInfo->eaiRevealed, i))
						return true;
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
					Vec3 v3BitPosMin = {vMin[0] + iX * fFeetPerBitX, v3Pos[1], vMin[1] + fFeetPerBitZ * iZ + fFeetPerBitZ};
					Vec3 v3BitPosMax = {vMin[0] + iX * fFeetPerBitX + fFeetPerBitX, v3Pos[1], vMin[1] + fFeetPerBitZ * iZ + fFeetPerBitZ};
					if (iBit >= 0 && (U32)iBit < eaiSize(&pInfo->eaiRevealed) * sizeof(pInfo->eaiRevealed[0]) * 8 && distanceToBoxSquared(v3BitPosMin, v3BitPosMax, v3Pos) < fRadius * fRadius)
						if (TSTB(pInfo->eaiRevealed, iBit))
							return true;
				}
			}
		}
		break;
	}
	return false;
}

// Return the percentage of the current map revealed by the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetMapRevealPercentage");
F32 exprEntGetMapRevealPercentage(SA_PARAM_OP_VALID Entity *pEnt)
{
	return pEnt ? mapRevealGetPercentage(mapRevealInfoGetByRegion(pEnt, NULL)) : 0.f;
}

#include "MapRevealCommon_h_ast.c"