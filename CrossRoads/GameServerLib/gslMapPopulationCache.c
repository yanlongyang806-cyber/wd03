/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslMapPopulationCache.h"
#include "MapDescription.h"
#include "earray.h"
#include "timing.h"
#include "stdtypes.h"
#include "StringCache.h"

#include "AutoGen/MapDescription_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

// The last time the cache was refreshed from the map manager.
static U32 s_MapPopulationCacheLastRefresh = 0;

// Flag to indicate if the cache has had its map list changed since the last refresh.
static bool s_MapPopulationCacheDirty = false;

static MapNameAndPopulationList *s_MapPopulationCache = NULL;

// Update the local cache of map populations with new population counts from the map manager.
void
gslMapPopulationCache_Update(MapNameAndPopulationList *mapList)
{
    int i;

    if ( s_MapPopulationCache == NULL )
    {
        return;
    }

    for ( i = eaSize(&mapList->eaMapList) - 1; i >= 0; i-- )
    {
        MapNameAndPopulation *tmpMapNameAndPopulation = mapList->eaMapList[i];
        MapNameAndPopulation *mapNameAndPopulation;

        // Find the map entry in the local cache.
        mapNameAndPopulation = eaIndexedGetUsingString(&s_MapPopulationCache->eaMapList, tmpMapNameAndPopulation->mapName);
        if ( mapNameAndPopulation )
        {
            // Update the player count in the cache.
            mapNameAndPopulation->uPlayerCount = tmpMapNameAndPopulation->uPlayerCount;
        }
    }
}

void
gslMapPopulationCache_Add(const char *mapName)
{
    MapNameAndPopulation *mapNameAndPopulation;

    // Create the cache if it doesn't exist.
    if ( s_MapPopulationCache == NULL )
    {
        s_MapPopulationCache = StructCreate(parse_MapNameAndPopulationList);
    }

    // Find the map entry in the local cache.
    mapNameAndPopulation = eaIndexedGetUsingString(&s_MapPopulationCache->eaMapList, mapName);
    if ( mapNameAndPopulation )
    {
        // Don't do anything if the map is already in the cache.
        return;
    }

    // Add map to the cache.
    mapNameAndPopulation = StructCreate(parse_MapNameAndPopulation);
    mapNameAndPopulation->mapName = allocAddString(mapName);
    eaIndexedAdd(&s_MapPopulationCache->eaMapList, mapNameAndPopulation);

    // Set the dirty flag so that the cache updates next frame.
    s_MapPopulationCacheDirty = true;
}

U32
gslMapPopulationCache_GetPopulation(const char *mapName)
{
    MapNameAndPopulation *mapNameAndPopulation;

    if ( s_MapPopulationCache == NULL )
    {
        return 0;
    }

    // Find the map entry in the local cache.
    mapNameAndPopulation = eaIndexedGetUsingString(&s_MapPopulationCache->eaMapList, mapName);
    if ( mapNameAndPopulation )
    {
        return mapNameAndPopulation->uPlayerCount;
    }

    return 0;
}

void
gslMapPopulationCache_Remove(const char *mapName)
{
    MapNameAndPopulation *mapNameAndPopulation;

    if ( s_MapPopulationCache == NULL )
    {
        return;
    }

    // Find and remove the map entry in the local cache.
    mapNameAndPopulation = eaIndexedRemoveUsingString(&s_MapPopulationCache->eaMapList, mapName);
}

void 
gslMapPopulationCache_BeginFrame(void)
{
    U32 curTime = timeSecondsSince2000();

    // Only update the cache if the cache exists, has one or more map in it, and has either the dirty bit set or the refresh time interval has expired.
    if ( s_MapPopulationCache && eaSize(&s_MapPopulationCache->eaMapList) > 0 && 
        ( s_MapPopulationCacheDirty || ( ( s_MapPopulationCacheLastRefresh + MAP_POPULATION_CACHE_UPDATE_INTERVAL ) < curTime ) ) )
    {
        RemoteCommand_aslMapManager_GetPopulationForMapList(GLOBALTYPE_MAPMANAGER, 0, objServerType(), objServerID(), s_MapPopulationCache);
        s_MapPopulationCacheDirty = false;
        s_MapPopulationCacheLastRefresh = curTime;
    }
}

// This command is used by the map manager to return the population counts.  Two non-returning remote commands are used to reduce load on the transaction server.
AUTO_COMMAND_REMOTE;
void 
GetPopulationForMapList_Return(MapNameAndPopulationList *mapList)
{
    gslMapPopulationCache_Update(mapList);
}

AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(RegisterInterestInMapPopulation);
void 
gslMapPopulationCache_ExprRegisterInterestInMapPopulation(const char *mapName)
{
    gslMapPopulationCache_Add(mapName);
}

AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(NoLongerInterestedInMapPopulationData);
void 
gslMapPopulationCache_ExprNoLongerInterestedInMapPopulationData(const char *mapName)
{
    gslMapPopulationCache_Remove(mapName);
}

AUTO_EXPR_FUNC(layerfsm) ACMD_NAME(ComputePointsBasedOnMapPopulation);
U32
gslMapPopulationCache_ExprComputePointsBasedOnMapPopulation(const char *mapName, const char *opposingMapName, U32 basePointsToAward)
{
    U32 mapPopulation = gslMapPopulationCache_GetPopulation(mapName);
    U32 opposingMapPopulation = gslMapPopulationCache_GetPopulation(opposingMapName);

    // If the map being awarded has the highest population, or if either map has zero population reward the base number of points.
    if ( ( mapPopulation == 0 ) || ( opposingMapPopulation == 0 ) || ( mapPopulation >= opposingMapPopulation ) )
    {
        return basePointsToAward;
    }

    // Compute number of points to reward as a ratio of the two map populations.
    return basePointsToAward * opposingMapPopulation / mapPopulation;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(RegisterInterestInMapPopulation);
void 
gslMapPopulationCache_CmdRegisterInterestInMapPopulation(const char *mapName)
{
    gslMapPopulationCache_Add(mapName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(NoLongerInterestedInMapPopulationData);
void 
gslMapPopulationCache_CmdNoLongerInterestedInMapPopulationData(const char *mapName)
{
    gslMapPopulationCache_Remove(mapName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(ComputePointsBasedOnMapPopulation);
U32
gslMapPopulationCache_CmdComputePointsBasedOnMapPopulation(const char *mapName, const char *opposingMapName, U32 basePointsToAward)
{
    return gslMapPopulationCache_ExprComputePointsBasedOnMapPopulation(mapName, opposingMapName, basePointsToAward);
}