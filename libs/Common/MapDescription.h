#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "GlobalComm.h"
#include "GlobalEnums.h"
#include "net/accountnet.h"
#include "StashTable.h"

typedef struct ZoneMapInfo ZoneMapInfo;


typedef struct NOCONST(SavedMapDescription) NOCONST(SavedMapDescription);

#define START_SPAWN "StartSpawn" // Special spawn point, indicates use the player spawn
#define POSITION_SET "SpawnAtPosition" //special spawn point, indicates use the position in the map description
#define SPAWN_AT_NEAR_RESPAWN "SpawnAtNearRespawn"	// spawn at the nearest re-spawn point that is unlocked. Used by changeinstance
#define SPAWN_AT_FURTHEST "SpawnAtFurthest"	// spawn at the furthest re-spawn point that anyone has ever reached on this map/partition

//flags used for both map searches and map possibilities.
AUTO_ENUM;
typedef enum enumMapSearchAndPossibilitiesFlags
{
	MAPSEARCH_NOCANCELLING = 1,
} enumMapSearchAndPossibilitiesFlags;

// NOTE: Keep these two in sync, they are cast between at various points

//also note that when you add new fields here, you likely want to add them to aslCreateGameServerDescription()

// This is the logical description of a map, and can be saved to the database, etc
AUTO_STRUCT;
typedef struct MapDescription
{
		U8 eMapType;					AST(SUBTABLE(ZoneMapTypeEnum))	// The type (static, mission, etc)
		U8 ownerType;					AST(SUBTABLE(GlobalTypeEnum)) // Non-static maps are often owned by either a player or a team

		// Indicates whether the spawn position can skip the beacon check
		bool bSpawnPosSkipBeaconCheck;

		const char* mapDescription;		AST(POOL_STRING) // Highest level info about a map (zone descriptor, mission name, etc)
		const char* mapDetail;			AST(POOL_STRING) // A sub identifier, may be null - currently unused
		const char* mapVariables;		AST(POOL_STRING) // Used as input parameters to maps

		int mapInstanceIndex;		// Differentiate between maps with same description (not for serious use)

		ContainerID ownerID;		

		Vec3 spawnPos;		AST(USERFLAG(TOK_USEROPTIONBIT_1))// Last known location of the player on this map
		Vec3 spawnPYR;		AST(USERFLAG(TOK_USEROPTIONBIT_1))// Last known rotation of the player on this map
		const char* spawnPoint;	AST(POOL_STRING, USERFLAG(TOK_USEROPTIONBIT_1))// If we need to go to a named point after map transfer
		ContainerID containerID; //container ID of this map, if known

		ZoneMapInfo *pZoneMapInfo; NO_AST //calculate this once only per mapDescription, as it's slow. Used internally on mapmanager

		bool bUGC; //if true, this is a UGC map
		bool bUGCEdit; //if true, this is a UGC edit map
		ContainerID iUGCProjectID; //should always be set if bUGC or bUGCEdit is set

		ContainerID iVirtualShardID; //if non-zero, this map was part of a "virtual shard" inside the main shard, and can
			

		//newly added for new partition code. Will eventually always be non-zero, a value of zero presumably
		//indicates that the old code was being used (which is fine for backward compatibility)
		U32 iPartitionID; AST(ADDNAMES("uPartitionID"))
} MapDescription;

// Persisted version of above, which is saved on entities
//  
AUTO_STRUCT AST_CONTAINER;
typedef struct SavedMapDescription
{
		const U8 eMapType;						AST(PERSIST, SUBTABLE(ZoneMapTypeEnum)) // The type (static, mission, etc)
		//THIS FIELD IS NOT YET IMPLEMENTED
		const U8 ownerType;						AST(PERSIST, SUBTABLE(GlobalTypeEnum)) // Non-static maps are often owned by either a player or a team 	

		// Indicates whether the spawn position can skip the beacon check
		const bool bSpawnPosSkipBeaconCheck;			AST(PERSIST)

		CONST_STRING_POOLED mapDescription;	AST(PERSIST, SUBSCRIBE, POOL_STRING) // Highest level info about a map (zone name, mission name, etc)
		CONST_STRING_POOLED mapDetail;		AST(PERSIST, POOL_STRING) // A sub identifier, may be null - currently unused
		CONST_STRING_POOLED mapVariables;	AST(PERSIST, SUBSCRIBE, POOL_STRING) // Used as input parameters to maps

		const int mapInstanceIndex;		AST(PERSIST) // Differentiate between maps with same description (not for serious use)

		//THIS FIELD IS NOT YET IMPLEMENTED
		const ContainerID ownerID;		AST(PERSIST) 		

		const Vec3 spawnPos;			AST(PERSIST, USERFLAG(TOK_USEROPTIONBIT_1)) // Last known location of the player on this map
		const Vec3 spawnPYR;			AST(PERSIST, USERFLAG(TOK_USEROPTIONBIT_1)) // Last known rotation of the player on this map
		CONST_STRING_POOLED spawnPoint;		AST(PERSIST, POOL_STRING, USERFLAG(TOK_USEROPTIONBIT_1)) // If we need to go to a named point after map transfer
		const ContainerID containerID; AST(PERSIST)//container ID of this map, if known

		const ZoneMapInfo *pZoneMapInfo; NO_AST //calculate this once only per mapDescription, as it's slow 

		const bool bUGC; //if true, this is a UGC map
		const bool bUGCEdit; //if true, this is a UGC edit map
		const ContainerID iUGCProjectID; //should always be set if bUGC or bUGCEdit is set

		const ContainerID iVirtualShardID; //if non-zero, this map was part of a "virtual shard" inside the main shard, and can
			//only be moved to/seen by players in that virtual shard

		//newly added for new partition code. Will eventually always be non-zero, a value of zero presumably
		//indicates that the old code was being used (which is fine for backward compatibility)
		const U32 iPartitionID;  AST(PERSIST) 
} SavedMapDescription;
extern ParseTable parse_SavedMapDescription[];
#define TYPE_parse_SavedMapDescription SavedMapDescription

#define MapDescription_to_SavedMapDescription_DeConst(md)\
		(0 && ((MapDescription*)NULL == md) ? 0 : (NOCONST(SavedMapDescription) *)(md))

// This is the old version of SavedMapDescription that was used when map history was an array.
// It is being kept around so that we can read in old characters.  Do not change it!
AUTO_STRUCT AST_CONTAINER;
typedef struct obsolete_SavedMapDescription
{
	const U8 eMapType;						AST(PERSIST, SUBTABLE(ZoneMapTypeEnum)) // The type (static, mission, etc)
		//THIS FIELD IS NOT YET IMPLEMENTED
		const U8 ownerType;						AST(PERSIST, SUBTABLE(GlobalTypeEnum)) // Non-static maps are often owned by either a player or a team 	

		CONST_STRING_MODIFIABLE mapDescription;	AST(PERSIST, POOL_STRING) // Highest level info about a map (zone name, mission name, etc)
		CONST_STRING_MODIFIABLE mapDetail;		AST(PERSIST, POOL_STRING) // A sub identifier, may be null - currently unused
		CONST_STRING_MODIFIABLE mapVariables;	AST(PERSIST, SUBSCRIBE, POOL_STRING) // Used as input parameters to maps

		const int mapInstanceIndex;		AST(PERSIST) // Differentiate between maps with same description (not for serious use)

		//THIS FIELD IS NOT YET IMPLEMENTED
		const ContainerID ownerID;		AST(PERSIST) 		

		const Vec3 spawnPos;			AST(PERSIST, USERFLAG(TOK_USEROPTIONBIT_1)) // Last known location of the player on this map
		const Quat spawnRot;			AST(PERSIST, USERFLAG(TOK_USEROPTIONBIT_1)) // Last known rotation of the player on this map
		CONST_STRING_MODIFIABLE spawnPoint;		AST(PERSIST, POOL_STRING, USERFLAG(TOK_USEROPTIONBIT_1)) // If we need to go to a named point after map transfer
		const ContainerID containerID; AST(PERSIST)//container ID of this map, if known

		const ZoneMapInfo *pZoneMapInfo; NO_AST //calculate this once only per mapDescription, as it's slow 

} obsolete_SavedMapDescription;

extern ParseTable parse_MapDescription[];
#define TYPE_parse_MapDescription MapDescription

AUTO_STRUCT;
typedef struct DynamicPatchInfo
{
	char *pServer; // Patchserver to use
	int iPort;
	char *pPrefix; // Path prefix
	char *pClientProject; // Patch project
	char *pServerProject;
	char *pUploadProject;
	char *pResourceProject;
	int iBranch; // Patch branch
	char *pViewName; // Patch view (optional)
	int iTimeout; // Patching timeout in seconds
} DynamicPatchInfo;

extern ParseTable parse_DynamicPatchInfo[];
#define TYPE_parse_DynamicPatchInfo DynamicPatchInfo

AUTO_STRUCT;
typedef struct GameServerDescription_UgcStuff
{
	//note that ugc project container ID is in baseMapDescription

	//if this game server is a UGC map, what is its UUID?
	char UgcUUID[40]; 

	U32 iOwnerAccountID;

	char *pOwnerAccountName;
} GameServerDescription_UgcStuff;


//This is all the configuration info that a map gets from the MapManager after it
//starts up. This is also what the map provides to the MapManager if the MapManager crashes
//and restarts
AUTO_STRUCT;
typedef struct GameServerDescription
{
	bool bDescriptionIsActive;

	MapDescription baseMapDescription;

	int iMapPort;

	bool bEditMode; // This map is in edit mode - don't send new players here
	bool bAllowInstanceSwitchingBetweenOwnedMaps; //if true, then allow players
		//to try to do instance switching, which is usually only allowed on
		//static maps

	int iExpectedMaxPlayers; //if 0, this is unknown. It is not GUARANTEED that there will not be more than
	//this many players, but it is expected

	GameServerDescription_UgcStuff ugcStuff;

	// Patching information
	DynamicPatchInfo *patchInfo;

} GameServerDescription;

extern ParseTable parse_GameServerDescription[];
#define TYPE_parse_GameServerDescription GameServerDescription


AUTO_ENUM;
typedef enum MapSearchType
{
	MAPSEARCHTYPE_UNSPECIFIED, //no search type specified, during map transfer code transition this means use the old map transfer code

	MAPSEARCHTYPE_ALL_FOR_DEBUGGING, //ie, "showAllStatic". Give an option to log into each active partition of each active map, as well as a
		//new copy of each map

	MAPSEARCHTYPE_OWNED_MAP, //I want to go to a specifically owned (presumably mission) map. So if one exists with the specified owner, send
		//me there, otherwise create a new one

	MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES, //I want to go to a named static map, please send me info about all available instances of it so
		//I can choose which one to go to

	MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY, //I want to go to a specific container ID/partition ID, and only that
	MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_ONLY, //I want to go to a specific public index version of a named map, and only that

	MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_OR_OTHER, //I want to go to a specific container ID/partition ID, but
		//will fall back to any other version of it

	MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_OR_OTHER, //I want to go to a specific public index version of a named map, but will
		//will fall back to any other version of it


	MAPSEARCHTYPE_NEWPLAYER, //I'm a new player, I want to go to whatever map new players go to

	MAPSEARCHTYPE_NEWPLAYER_SKIPTUTORIAL, //I'm a new player, but am skipping the tutorial

	MAPSEARCHTYPE_FALLBACK_MAP,	// I lost my map, please put me at a safe map

	MAPSEARCHTYPE_ALL_OWNED_MAPS, //I'm doing a instance switch from an owned map, so I suspect there might be 
		//multiple of the same map owned by the same container, and want to get a list of all of them
} MapSearchType;


// This is passed around between the login/gameserver and the mapmanager to indicate a player's choices
AUTO_STRUCT;
typedef struct MapSearchInfo
{
	MapDescription baseMapDescription;
	GlobalType playerType;
	ContainerID playerID;
	ContainerID iGuildID;
	ContainerID teamID;
	U32 playerAccountID;
	U32 developerAllStatic:1; // Send all maps
	U32 debugPosLogin:1; // Login to specific location
	U32 newCharacter:1; // If this character is a new character

	// If true, then this character recently did a shard transfer, so
	// he may have some bogus data sitting on him from the prev shard.
	U32 bExpectedTrasferFromShardNS:1;

	// If true, then the map manager will return a fallback map if the request would otherwise return
	//  no maps at all.  This is used to prevent people from getting stuck if their map history is
	//  empty or otherwise messed up.
	U32 requestFallback:1;

	// If true then we are requesting to be allowed into the map even if its population is already
	//  at the soft cap.
	U32 overSoftCapOK:1;

	//I'm afraid I'm blackholed, please log me back into my most recent static map
	U32 safeLogin:1;

	//when searching for an OWNED map, only return one that already exists, not a new one
	U32 bNoNewOwned:1;

	// Use the skip tutorial maps for new players
	U32 bSkipTutorial:1;

	//if true, search for existing maps in all virtual shards (only available through access level commands)
	U32 bAllVirtualShards:1;

	//if true, include full maps in the results
	U32 bShowFullMapsAsDisabled:1;

	//search flags which should be added to the possibleMapChoices and sent all the way down to the client
	enumMapSearchAndPossibilitiesFlags eFlags;


	//exclude the gameserver with this container ID, if it exists (presumably to keep people from trying to transfer
	//to their own gameserver)
	ContainerID gameServerIDToExclude;

	AccountProxyKeyValueInfoList *pAccountPermissions; 

	//access level of the account of the player doing the requesting
	int iAccessLevel;

	//if set, then this request is for a specific UGC map only
	ContainerID iUGCProjectID;
	const char *pUGCNameSpace;

	//if this is non-zero, then use the "new" map transfer partition code
	MapSearchType eSearchType;


	char *pAccountName;
	char *pPlayerName;
	char *pAllegiance;

	//internally used by mapmanager only, no one else should set/touch this
	bool _bBypassSoftLimits; NO_AST
} MapSearchInfo;

extern ParseTable parse_MapSearchInfo[];
#define TYPE_parse_MapSearchInfo MapSearchInfo

AUTO_ENUM;
// You must only use values up to 2^8-1
typedef enum enumMapSaturation
{
	MAP_SATURATION_NONE,
	MAP_SATURATION_LIGHT,
	MAP_SATURATION_MEDIUM,
	MAP_SATURATION_HIGH,

	MAP_SATURATION_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below
} enumMapSaturation;

#define enumMapSaturation_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(MAP_SATURATION_MAX <= (1 << (enumMapSaturation_NUMBITS - 1)));

#define MAP_SATURATION_LIGHT_CUTOFF 0.33
#define MAP_SATURATION_MEDIUM_CUTOFF 0.66

AUTO_ENUM;
typedef enum MapChoiceType
{
	MAPCHOICETYPE_UNSPECIFIED, //not using the new system
	MAPCHOICETYPE_FORCE_NEW, //must create a new map no matter what
	MAPCHOICETYPE_NEW_OR_EXISTING_OWNED, //I want to end up on a map I or my teammates own, don't care if it exists or not currently
	MAPCHOICETYPE_SPECIFIED_ONLY, //I'm specifying a specific container ID/partition ID or instance index. I want to end up on it only
	MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT, //I'm specifying a a specific container ID or instance index. Put me on it if possible, or 
									//any map of that type otherwise
	MAPCHOICETYPE_BEST_FIT, //Any map of the given name, wherever I fit nicely

	MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER, //debug only... I'm telling you a game server container ID, create a new partition specifically
			//on it
} MapChoiceType;

AUTO_STRUCT;
typedef struct PossibleMapChoice
{
	MapChoiceType eChoiceType; //MAPCHOICETYPE_UNSPECIFIED means use the old system

	int iNumPlayers;
	enumMapSaturation saturation : enumMapSaturation_NUMBITS; AST(SUBTABLE(enumMapSaturationEnum))
	U32 bLastMap:1;
	U32 bDebugLogin:1;
	U32 bNewMap:1; // if true, they specifically requested a new map
	U32 bEditMap:1; // if true, we are opening a map in Production Edit mode
	U32 bMapIsLoading:1; //if true, this map is still loading, not yet running

	U32 bNotALegalChoice:1; //if true, this map is being included but should be grayed out (so that the
	//player can see that the map exists but is full, or something)
	//This is a folder in Tree Mode

	//these two fields attempt to make clear where your team is, but are based on container subscriptions so
	//can occasionally be out of sync if things have changed rapidly.
	U32 bYourTeamLeaderIsThere:1; AST(USERFLAG(TOK_USEROPTIONBIT_1))	// don't compare this field

	//if set, they requested a static map, and none existed. So give them a new one. UNLESS you can find one
	//they can log into, in which case, give them that one instead
	U32 bPossiblyNewStatic:1;

	// If set, this is a new character spawn
	U32 bNewCharacterSpawn:1;

	//set on the gameserver when making a choice. This means "I looked at the choices and picked
	//the one I thought was best, but if a teammate went to one of these maps in the last 
	//brief period of time, go there instead". This is because of a potential race condition
	//when two teammates both move at the same time
	U32 bGoWhereATeammateRecentlyWentInsteadIfPossible:1;

	//if true, this instance is hard full (and bNotALegalChoice is true too)
	U32 bIsHardFull:1;

	// if true, this instance is just soft full (and bNotALegalChoice is true, unless you have a teammate there)
	U32 bIsSoftFull:1;

	//if true, this is the instance where the requester currently is.
	U32 bIsCurrent:1;

	int iNumTeamMembersThere;	AST(USERFLAG(TOK_USEROPTIONBIT_1))	// don't compare this field

	//same for guild
	int iNumGuildMembersThere;	AST(USERFLAG(TOK_USEROPTIONBIT_1))	// don't compare this field

	// And our friend count
	int iNumFriendsThere;		AST(USERFLAG(TOK_USEROPTIONBIT_1))	// don't compare this field

	// Embedded so we can easily make a UIList of PossibleMapChoices
	// that also displays map description details.
	MapDescription baseMapDescription; AST(EMBEDDED_FLAT)

	//we should probably munge this before it's sent to the client and back, although it's not hugely relevant
	char newMapReason[256];

	//if bNotALegalChoice is true, this string should explain why it is illegal, ie "is full" or "is locked".
	char notLegalChoiceReason[256];

	// Patching information for UGC and star-cluster maps
	DynamicPatchInfo *patchInfo;
} PossibleMapChoice;

//true if this choice refers to a specific partition/server, false otherwise
static __forceinline bool CHOICE_IS_SPECIFIC(PossibleMapChoice *pChoice) { return pChoice->eChoiceType == MAPCHOICETYPE_SPECIFIED_ONLY || pChoice->eChoiceType == MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT; }

extern ParseTable parse_PossibleMapChoice[];
#define TYPE_parse_PossibleMapChoice PossibleMapChoice

AUTO_STRUCT;
typedef struct PossibleMapChoices
{
	enumMapSearchAndPossibilitiesFlags eFlags;

	PossibleMapChoice **ppChoices;
} PossibleMapChoices;

extern ParseTable parse_PossibleMapChoices[];
#define TYPE_parse_PossibleMapChoices PossibleMapChoices

AUTO_STRUCT;
typedef struct ReturnedGameServerAddress
{
	ContainerID iContainerID; //container ID of the gameserver
	U32 uPartitionID;
	int iInstanceIndex;
	U32 iIPs[2];
	int iPortNum;
	int iCookie; //added by the login server as it passes this from the map manager to the client
	char errorString[1024]; //if iContainerID is 0, then no gameserver returned, this string
	//should be set
} ReturnedGameServerAddress;

extern ParseTable parse_ReturnedGameServerAddress[];
#define TYPE_parse_ReturnedGameServerAddress ReturnedGameServerAddress

bool IsSameMapDescription(MapDescription *map1, MapDescription *map2);

void CopySpawnInfo(MapDescription *pDest, MapDescription *pSrc);

bool MapShouldOnlyBeReturnedToIfSameInstanceExists(MapDescription *pMap);


//a map which you never want to "go back" to once it's left, so it should never exist in the map history
//except as the top element
bool MapCanOnlyBeOnTopOfMapHistory(MapDescription *pMap);

//if true, then for some reason we should not change the map history for this entity
typedef struct Entity Entity;
bool DontModifyMapHistory(const Entity *pEnt);



//normally when a client requests possible maps and there's only one element in the list, it's
//chosen automatically. But this is clearly wrong in some cases... for instance, if choosing between
//UGC maps and there's only one in the list.
bool PossibleMapChoiceCanBeAutomaticallyChosen(PossibleMapChoice *pChoice);

#define UNKNOWNMAPTYPE "UNKNOWNMAPTYPE"
#define MAPTYPECORRUPTION "MAPTYPECORRUPTION"
//checks if the map passed in has the expected map type. Returns the best
//map type it can figure. Sets error string if there is an error (which should probably generate an alert or something)
ZoneMapType MapCheckRequestedType(const char *pMapName, ZoneMapType eAssumedType, char **ppOutErrorString, char **ppOutAlertKey_NOT_AN_ESTRING);

//
// Any saved map that is persisted on the character (map history) should have
//  the pitch and roll values set to zero to reduce database writes.
//
void SavedMapUpdateRotationForPersistence(NOCONST(SavedMapDescription) *pSavedMap);

//
// Copy the obsolete version of SavedMapDescription to the new version.
//  This is used to copy old stack map history to the new version.
//
NOCONST(SavedMapDescription) *CopyOldSavedMapDescription(obsolete_SavedMapDescription *oldDesc);



//if true, then whenever a player successfully transfers OFF this map, tell the map that it can now
//immediately close if it has no more players on it
bool MapCanBeClosedImmediatelyOnPlayerTransferOff(MapDescription* pDesc);

//beta version of this structure... passed between mapmanager and GS when mapmanager
//is telling GS about partitioning
AUTO_STRUCT;
typedef struct MapPartitionSummary
{
	U32 uPartitionID; //NOT the internal index, rather a never-reused-on-this-GS.exe ID
	const char *pMapVariables;
	ContainerID iVirtualShardID;
	int iPublicIndex; //end-user-facing ID

	GlobalType eOwnerType;
	ContainerID iOwnerID;

	//perf info filled in by the GS
	int iNumPlayers;

	//used internally by mapmanager -- TODO, move these into a wrapper structure on MM
	int iNumPlayersRecentlyLoggingIn;
	
	int iNumPendingRequests; NO_AST//should precisely correspond to the # elements in pServer->ppPendingRequests referring to this
							 //partition
	
	U32 iLastTimeSendPlayerThere;	AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	U32 bAssignedOpenInstance : 1;
	StashTable sTeamsSentThereByID; NO_AST
} MapPartitionSummary;

extern ParseTable parse_MapPartitionSummary[];
#define TYPE_parse_MapPartitionSummary MapPartitionSummary

//used by the "new" map transfer code that goes along with partitions
AUTO_ENUM;
typedef enum GameServerExeType
{
	GSTYPE_UNSPEC, //no idea what this is
	GSTYPE_INVALID, //used by the mapmanager to communicate to the gameserver that something is wrong and that the handshaking won't work
	GSTYPE_NORMAL, //this is a static or mission map or pvp or something like that
	GSTYPE_NOT_A_GS, //this is a serverbinner or a webrequestserver or something
	GSTYPE_UGC_PLAY, //this is a playing UGC server
	GSTYPE_UGC_EDIT, //this is an editing UGC server
	GSTYPE_PREEXISTING, //this is a preexisting map, we don't know what type it is yet
	GSTYPE_PRELOAD, //this is a map that is going through the preload system
} GameServerExeType;

//what goes into GameServerExe_Description and what goes into TrackedGameServerExe? GameServerExe_Description is for
//stuff that the gameserver itself knows and cares about. TrackedGameServerExe is for stuff that the map manager cares
//about while tracking the gameserver
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "MapDescription, Type");
typedef struct GameServerExe_Description
{

	GameServerExeType eServerType;
	ZoneMapType eMapType;

	//the "name" of the map, ie "emptymap" or "Borg_Alpine_Brushtest" or something like that. 
	//The key identifying feature of a map
	const char *pMapDescription; AST(POOL_STRING)

	int iListeningPortNum; //what port clients will contact this GS on. Must be unique among GS's on a given machine

	ContainerID iUGCProjectID; //should always be set if eType is UGC_PLAY or UGC_EDIT

	bool bAllowInstanceSwitchingBetweenOwnedMaps;

	MapPartitionSummary **ppPartitions; //not sorted
} GameServerExe_Description;
extern ParseTable parse_GameServerExe_Description[];
#define TYPE_parse_GameServerExe_Description GameServerExe_Description

//a wrapped object used as the return value of gslGetGameServerDescriptionForNewMapTransfer
AUTO_STRUCT;
typedef struct GSDescription_And_ZoneMapInfo
{
	GameServerExe_Description *pDescription;
	ZoneMapInfo *pZoneMapInfo; AST(LATEBIND)
} GSDescription_And_ZoneMapInfo;

extern ParseTable parse_GSDescription_And_ZoneMapInfo[];
#define TYPE_parse_GSDescription_And_ZoneMapInfo GSDescription_And_ZoneMapInfo

//stuff which describes the server/player calling (basically so that the args can be kept simple)
AUTO_STRUCT;
typedef struct NewOrExistingGameServerAddressRequesterInfo
{
	char *pcRequestingShardName;
	GlobalType eRequestingServerType;
	ContainerID iRequestingServerID;
	ContainerID iEntContainerID;
	ContainerID iRequestingTeamID;
	const char *pPlayerName;
	U32 iPlayerAccountID;
	const char *pPlayerAccountName;
	int iPlayerLangID;
	U32 iPlayerIdentificationCookie;
} NewOrExistingGameServerAddressRequesterInfo;

AUTO_STRUCT;
typedef struct MapNameAndPopulation
{
    STRING_POOLED mapName;      AST(POOL_STRING KEY)
    U32 uPlayerCount;
} MapNameAndPopulation;

AUTO_STRUCT;
typedef struct MapNameAndPopulationList
{
    EARRAY_OF(MapNameAndPopulation) eaMapList;
} MapNameAndPopulationList;

SA_RET_OP_VALID PossibleMapChoice * ChooseBestMapChoice(SA_PARAM_NN_VALID PossibleMapChoice *** peaPossibleMapChoice);

//when locking a gameserver machine, some gameservers can be left running... no one else will be able to transfer in, but 
//everything will fail gracefully. Others, like pvp or owned maps, have to be killed immediately because otherwise furture map transfers
//will fail awkwardly
bool MapDescription_MapTypeSupportsLockingWithoutKilling(ZoneMapType eType);
