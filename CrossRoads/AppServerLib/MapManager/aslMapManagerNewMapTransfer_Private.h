#pragma once
#include "globalTYpes.h"
#include "mapDescription.h"
#include "mechanics_common.h"
#include "svrGlobalInfo.h"
#include "IntFIFO.h"
#include "aslMapManagerConfig.h"

typedef struct MapSummaryList MapSummaryList;
typedef struct PlayerWasSentToMapLog PlayerWasSentToMapLog;

AUTO_ENUM;
typedef enum GameServerListType
{
	LISTTYPE_NORMAL, //these are all the maps that share a single map name
	LISTTYPE_UGC_PLAY, //these are all maps that share a single map name and are UGC play maps
	LISTTYPE_UGC_EDIT, // these are all the ugc edit maps
	LISTTYPE_PREEXISTING, //maps that the mapmanager doesn't yet know much about because they were created by someone else
	LISTTYPE_PRELOAD, //one preload list for every category that wants preload maps
} GameServerListType;

AUTO_ENUM;
typedef enum GameServerExeState
{
	//the GS has requested that the controller launch a new GS, waiting to hear back
	//with confirmation that it has launched (which will tell us its machine and so forth)
	GSSTATE_LAUNCH_REQUESTED_FROM_CONTROLLER,

	//this map existed when the mapmanager started (or was otherwise spawned by someone else).
	//The map manager has sent it a query
	GSSTATE_PREEXISTING_WAITING_FOR_DESCRIPTION,

	//the map manager just spawned this map, and is waiting for its handshaking
	GSSTATE_SPAWNED_WAITING_FOR_HANDSHAKE,

	//the map has done its initial handshaking with the server. We are now waiting while the map
	//loads. We will know it's done loading when it calls RemoteCommand_MapIsDoneLoading or else
	//when the controller informs us that its state is gslRunning, whichever happens first
	GSSTATE_LOADING,

	//the map is running
	GSSTATE_RUNNING,


	//the map was started as a "preload" map, and when it is ready for handshaking will be 
	//put into cold storage until its needed
	GSSTATE_PRELOAD_WAITING_FOR_HANDSHAKE,

	//the map is a preload map that is all ready to go
	GSSTATE_PRELOAD,
} GameServerExeState;


typedef struct TrackedGameServerExe TrackedGameServerExe;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "NumGameServers");
typedef struct MachineForGameServers
{
	const char *pMachineName; AST(POOL_STRING KEY)
		U32 iIP; AST(FORMATSTRING(HTML_IP=1))
		U32 iPublicIP; AST(FORMATSTRING(HTML_IP=1))

		int __SERVERMONONLY__numGameServers; AST(NAME(NumGameServers)) 
		TrackedGameServerExe **ppGameServers; //effectively unowned, although that's not really ever relevant
} MachineForGameServers;


//typically all the gameservers that share a map name, but ugc edit maps will be in separate lists
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "MapDescription, Type, NumGameServers, LaunchOne");
typedef struct GameServerList
{
	const char *pMapDescription; AST(POOL_STRING KEY) //CONST_MAPDESCRIPTION_UGCEDIT if this is the list of ugc edit maps
	GameServerListType eType;
	REF_TO(ZoneMapInfo) hZoneMapInfo;

	char *pSpecifiedCategoryName; AST(POOL_STRING)
	MapCategoryConfig *pCategoryConfig;
	MinMapCountConfig *pMinMapCount;

	//fixed up via FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED, DO NOT USE AT ALL EVER
	int __SERVERMONONLY__numGameServers; AST(NAME(NumGameServers)) 


	TrackedGameServerExe **ppGameServers;

	//earray of U32s with bits set to indicate which public indices are in use. So pUsedPublicIndices[0] tells us whether
	//indices 1..32 are in use (and most of the time, the earray will have only one member)
	U32 *pUsedPublicIndices;

	//if non-zero then this is a list of maps in a UGC project
	ContainerID iUGCContainerIDForPlayingServers;

	AST_COMMAND("LaunchOne", "NewMapTransfer_LaunchNewServerCmd $FIELD(MapDescription) $INT(How many partitions) $STRING(Extra command line options) $NORETURN")

} GameServerList;



//a player is going to be sent to this server as soon as the server and partition are ready
AUTO_STRUCT;
typedef struct PendingRequestCache
{
	U32 uPartitionID;
	ContainerID iEntContainerID;
	SlowRemoteCommandID iCmdID;
	ContainerID iTeamID;

	//filled in during SendPlayerToServerNow
	ContainerID iGameServerID; 
	U32 iUniqueCacheID;
	int iInstanceIndex;
} PendingRequestCache;

AUTO_STRUCT;
typedef struct PendingDoorDestinationRequest
{
	ContainerID uMapID; AST(KEY)
	MapSummaryList* pMapSummaryList;
	U32 uLastUpdateTime;
} PendingDoorDestinationRequest;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "description.MapDescription, gameServerGlobalInfo.NumPlayers, gameServerGlobalInfo.numPartitions");
typedef struct TrackedGameServerExe
{
	ContainerID iContainerID; AST(KEY)
	U32 pid;

	GameServerExeState eState;

	GameServerExe_Description description;

	MachineForGameServers *pMachine; AST(UNOWNED FORMATSTRING(HTML=1, HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING="Qlessthana_hrefQequalsQdoublequoteQfslashviewxpathQquestmarkxpathQequalsMapManagerQlbracket0QrbracketQperiodglobObjQperiodGameServerExeMachinesQlbracketQdollarKEYQdollarQrbracketQdoublequoteQgreaterthanQdollarKEYQdollarQlessthanQfslashaQgreaterthan"))
	GameServerList *pList; AST(UNOWNED FORMATSTRING(HTML=1, HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING="Qlessthana_hrefQequalsQdoublequoteQfslashviewxpathQquestmarkxpathQequalsMapManagerQlbracket0QrbracketQperiodglobObjQperiodGameServerExeListsQlbracketQdollarKEYQdollarQrbracketQdoublequoteQgreaterthanQdollarKEYQdollarQlessthanQfslashaQgreaterthan"))

	U32 uNextPartitionID; AST(DEF(1))

	//the gameserver wants to get its description, and send it as soon as ready. Most likely cause is that it's a preload map. Could theoretically
	//also happen in some weird corner case where the remote command from the controller that was launching the server returned SUPER slowly
	SlowRemoteCommandID iPendingGameServerDescriptionRequest; NO_AST

	// Door destination requests
	MapSummaryList* pDestMapStatusRequests;
	U32 uDestMapSampleTimestamp;

	GameServerGlobalInfo globalInfo;

	PendingRequestCache **ppPendingRequests;

	int iNumPlayersRecentlyLoggingIn; //incremented whenever a player is sent, decremented when the GS acknowledges getting it, also slowly decays

	//people who have requested debug transfer notifications, will be fulfilled as soon as machine name and pid are known
	NewOrExistingGameServerAddressRequesterInfo **pPendingDebugTransferNotifications;

	bool bToldToDie; //I've already told this server to die due to timeout

	bool bLocked; //locked via server monitoring, don't send anyone here

	//tracked who was sent to the map recently, so we can catch "crashy" players
	PlayerWasSentToMapLog **ppPlayersSentHere;

	AST_COMMAND("Create new partition", "CreateNewPartition $FIELD(ContainerID)")
} TrackedGameServerExe;

#define CONST_MAPDESCRIPTION_UGCEDIT "_UGC_EDIT"
#define CONST_MAPDESCRIPTION_PREEXISTING "_PREEXISTING"

#define CONST_MAPDESCRIPTION_PREFIX_PRELOAD "_PRELOAD_"

