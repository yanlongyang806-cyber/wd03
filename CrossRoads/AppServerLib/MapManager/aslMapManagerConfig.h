#pragma once

#include "textParser.h"

typedef struct TrackedMap TrackedMap;

//config information for a "category" of maps
//
//Note that these categories are generally subcategories of the "real" map type, as defined in worldgrid.h 
//(ie, ZMTYPE_MISSION, ZMTYPE_OWNED, ZMTYPE_STATIC). 
//
//There are a few "magic" category names. If a map doesn't have a specific category, it will first look for
//maptype_default (ie, "static_default" or "mission_default"). If that doesn't exist, it will look for "default".
AUTO_STRUCT;
typedef struct MapCategoryConfig
{
	char *pCategoryName; AST(STRUCTPARAM POOL_STRING)
	//if the number of players in a map falls beelow the min number, and there are multiple instances of that map,
	//we want to close the smallest one so that the maps aren't too empty. Precise algorithm TBD.
	int iMinPlayers;

	//once the number of players in a partition hits this number, players can't go into this map without good reason
	//(ie, joining their team)
	int iMaxPlayers_Soft; 

	//once the number of players in a partition this number, players can't go into this map at all
	int iMaxPlayers_Hard;

	//same as the above, but adds all partitions on the GS together
	int iMaxPlayers_AcrossPartitions_Soft;
	int iMaxPlayers_AcrossPartitions_Hard;

	//if all the maps that exist have more than this many players, launch another one immediately
	int iNewMapLaunchCutoff;

	//if non-zero, then don't do memory leak alerts until this many megabytes have potentially leaked
	int iMegsBeforeMemLeakAlerts;

	int iMaxPartitions; AST(DEFAULT(1))

	int iLaunchWeight; AST(DEFAULT(1))

    //If this is true, then instances of this map will be forced to shut down when the last partition closes, rather than waiting around for another partition to start on them.
    bool bAlwaysShutDownWhenLastPartitionCloses;

    //Do not allow more instances of this map to start if there are more than this many already running.
    int iMaxInstances;

	char **ppPermissionTokens_Require;
	char **ppPermissionTokens_Exclude;

	int iNumPreloadMaps; //if non-zero, then try to keep this many maps around in a "ready to go" state for quick map transfers/launches

	ContainerID *pPreloadMapContainerIDs; NO_AST

	U32 iUsedFields[1]; AST(USEDFIELD)

	bool bAllowMultipleCopiesOfOwnedMaps;

//	TrackedMap **ppPreloadedMaps; NO_AST
} MapCategoryConfig;

//config information for a single map
AUTO_STRUCT;
typedef struct SingleMapConfig
{
	char *pPublicName; AST(STRUCTPARAM)
	char *pCategoryName; AST(STRUCTPARAM POOL_STRING)
} SingleMapConfig;

//minimum limits of how many of a map should be running
AUTO_STRUCT;
typedef struct MinMapCountConfig
{
	char *pMapName; AST(STRUCTPARAM)
	int iMinCount; AST(STRUCTPARAM)
} MinMapCountConfig;

AUTO_STRUCT;
typedef struct MapTypeCorruptionWhiteListEntry
{
	char *pMapName; AST(KEY STRUCTPARAM)
	ZoneMapType eType1; AST(STRUCTPARAM)
	ZoneMapType eType2; AST(STRUCTPARAM)
} MapTypeCorruptionWhiteListEntry;


AUTO_STRUCT;
typedef struct GlobalMapManagerConfig
{
	char *pLoadingComment; AST(ESTRING, FORMATSTRING(HTML_PREFORMATTED=1))

	MapCategoryConfig **ppCategories; AST(NAME(Category))
	SingleMapConfig **ppSingleMaps; AST(NAME(Map))

	char **ppStartingMaps; AST(NAME(StartingMap)) //what maps (presumably static maps) are started up at startup time
	char **ppNewCharacterMaps; AST(NAME(NewCharacterMap)) // What maps new characters are allowed to start on
	char **ppSkipTutorialMaps; AST(NAME(SkipTutorialMap)) // What maps new characters are allowed to start on when the skip the tutorial
	char *pFallbackStaticMap; AST(NAME(FallbackStaticMap)) // This is the map a player will get sent to if they don't have a valid map in their map history

	char **ppBannedMaps; AST(NAME(BannedMap)) //maps that are (presumably temporarily) banned for some reason... will be snipped out of
		//all possible choices and addresses at the last minute, so will probably fail somewhat non-elegantly

	MapTypeCorruptionWhiteListEntry **ppMapTypeCorruptionWhiteList; AST(NAME(MapTypeCorruptionWhiteList)) //maps which have had their type change somewhat
		//recently, so don't generate MAPTYPECORRUPTION alerts for them

	MinMapCountConfig **ppMinMapCounts; AST(NAME(MinMapCount))

	int iMaxEditServers; NO_AST

	//can either be an int or "nn_PER_GS_MACHINE"
	char *pMaxEditServers; AST(NAME(MaxEditServers) DEF("16_PER_GS_MACHINE"))


} GlobalMapManagerConfig;

extern MapCategoryConfig gFallbackDefaultConfig;