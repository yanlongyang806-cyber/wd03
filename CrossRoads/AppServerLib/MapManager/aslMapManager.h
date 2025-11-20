/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "GlobalComm.h"
#include "LocalTransactionManager.h"
#include "MapDescription.h"
#include "remoteautocommandsupport.h"
#include "StashTable.h"
#include "svrGlobalInfo.h"
#include "aslMapManagerConfig.h"

typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct MapDescription MapDescription;
typedef struct GameServerDescription GameServerDescription;
typedef struct MapSearchInfo MapSearchInfo;
typedef struct MapSummaryList MapSummaryList;
typedef struct PossibleUGCProject PossibleUGCProject;
typedef struct UGCProject UGCProject;
typedef struct ServerLaunchDebugNotificationInfo ServerLaunchDebugNotificationInfo;
typedef struct DynamicPatchInfo DynamicPatchInfo;
typedef struct UGCProjectVersion UGCProjectVersion;
typedef struct UGCProjectVersionMapInfo UGCProjectVersionMapInfo;

/*the way the map manager talks to the login code is as follows:
(1) The login code says "give me a list of all possible places for this character to log in"
(2) The map manager sends a collection of PossibleMapChoice structs to the login code
(3) The login code displays this list to the user, who makes a choice
(4) The map manager send the selected PossibleMapChoice back to the map manager
(5) The map manager now creates the appropriate map server (if necessary) and sends the IP/port/UID of the
requested map to the login code*/

#define MAPNAME_MAXLENGTH MAX_PATH



AUTO_STRUCT;
typedef struct PendingNewOrExistingRequest
{
	SlowRemoteCommandID iCommandID; AST(INT)
	ContainerID iPlayerEntID;
	MapPartitionSummary *pPartitionSummary;
} PendingNewOrExistingRequest;



AUTO_STRUCT;
typedef struct PlayerWasSentToMapLog
{
	ContainerID iPlayerContainerID;
	U32 iTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
} PlayerWasSentToMapLog;



typedef struct MapManagerState
{
	bool bDoStartingMaps; //if true, then use the list of starting maps from the map manager config file
						  //to create a bunch of maps at start time (ie, starting static maps)

	bool bDoLaunchCutoffs; //if true, pay attention to the iNewMapLaunchCutoff values in the map manager configs,
		//and always launch new maps when all existing maps are full to at least that level

	bool bLaunchCutoffsIgnoreNonexistant; //if true, then as long as there are no copies of a map running at all,
		//ignore launch cutoffs for that map. (debug only, presumably)
		
	bool bAllowSkipTutoral;
		// If true this flag allows developer systems to launch characters as they would normally, ie.e correct maps etc.
		// also allows account keys to be read

	U32 *piDoneHandshakingMapIDs;
	//IDs of maps that have finished handshaking this frame
} MapManagerState;

extern MapManagerState gMapManagerState;


AUTO_STRUCT;
typedef struct PartitionHandshakeCache
{
	int iCommandID;
	ContainerID iGameServerID;
	U32 iRequestTime;
	ContainerID iEntContainerID;
	ReturnedGameServerAddress retVal;
} PartitionHandshakeCache;


int MapManagerLibOncePerFrame(F32 fElapsed);

bool aslCreateGameServerDescription(MapDescription *map, GameServerDescription *server, ContainerID iContainerID);

bool MapIsAcceptingLoginsEx(TrackedMap *pMap, bool bAllowUGC, bool bHardLimit, char *whyString, int whyString_size);
#define MapIsAcceptingLogins(pMap, bHardLimit, str) MapIsAcceptingLoginsEx(pMap, false, bHardLimit, SAFESTR(str))

void addPatchInfoForUGCProject(PossibleUGCProject *pPossibleProject, UGCProject *pProject);
DynamicPatchInfo *CreatePatchInfoForNameSpace(const char *pNameSpace, bool bForPlaying);

MapCategoryConfig *FindCategoryByNameAndType(const char *pMapName, ZoneMapType eType, char **ppOutSingleCategoryName /*NOT AN ESTRING*/);

//must be pooled string
MapCategoryConfig *FindCategoryByName(const char *pName);

//if a player is sent to a map and then that map crashes within the next PSTCM_PLAYER_SENT_TO_MAP_INTERVAL seconds,
//we record that internally, and eventually 
//possibly send an alert if a player was sent to maps right before they crashed more than
//PSTCM_COUNT times in any PSTCM_INTERVAL seconds
#define PSTCM_COUNT 3
#define PSTCM_INTERVAL (24 * 60 * 60)
#define PSTCM_PLAYER_SENT_TO_MAP_INTERVAL (5 * 60)

void RegisterPlayerWasSentToMapBeforeItCrashed(PlayerWasSentToMapLog *pPlayerLog, U32 iMapContainerID);


TrackedMap *CreateAndAddNewMap(MapDescription *pDescription, char *pReason, bool bEditMode, 
	const UGCProject *pUGCProject, const UGCProjectVersion *pUGCProjectVersion, const UGCProjectVersionMapInfo *pUGCProjectVersionMap,
	DynamicPatchInfo *pPatchInfo, ServerLaunchDebugNotificationInfo *pLaunchNotificationDebugInfo, char *pExtraCommandLine);




ContainerID GetNextGameServerID(void);

LATELINK;
bool MapNameShouldBeExcludedFromMapNameFileForMCP(const char *pMapName);

extern GlobalMapManagerConfig gMapManagerConfig;

extern bool gbDebugTransferNotifications;

typedef enum
{
	MML_STATE_INIT,
	MML_STATE_SENT_STARTING_REMOTE_COMMANDS,
	MML_STATE_NORMAL,
} enumMapManagerLibState;

extern enumMapManagerLibState eMMLState;

extern StashTable sCategoriesByCategoryNamePointer;

//how users on the mapmanager itself call into the aslMapManagerRequestZoneMapInfoByPublicName system
typedef void (*ZoneMapInfoRequestCBFunc)(ZoneMapInfo *pZMInfo, void *pUserData);
void LocallyRequestZoneMapInfoByPublicName(const char *pName, ZoneMapInfoRequestCBFunc pCB, void *pUserData);

bool MapManager_SpecialMapTypeCorruptionAlertHandling(const char *pKey, const char *pString);

bool aslMapManager_IsNameSpacePlayableHelper(char *pNameSpace);
