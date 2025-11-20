/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMESERVERLIB_H_
#define GAMESERVERLIB_H_

#include "Entity.h"
#include "GlobalComm.h"
#include "MapDescription.h"
#include "timing.h" // for timing macros
#include "TimedCallback.h"



typedef struct CoarseTimerManager CoarseTimerManager;
typedef struct FrameLockedTimer FrameLockedTimer;
typedef struct InteractTarget InteractTarget;
typedef struct MovementClient MovementClient;
typedef struct ResourceCache ResourceCache;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCSearchResult UGCSearchResult;
typedef struct VirtualShard VirtualShard;

// These are defined in this library


typedef struct ClientLink
{
	NetLink*		netLink;
	U32				connectInstance;
	
	union {
		const EntityRef* const	localEntities;
		EntityRef*				localEntitiesMutable;
	};

	EntityRef*		debugEntities;
	
	AccessLevel		accessLevel;

	//we need to remember the highest entity number during our last send, so that if the highest-numbered entity
	//is deleted, we remember to send a delete
	//
	//if -1, no send has ever occurred
	int				iHighestActiveEntityDuringSendLastFrame;

	//info about how entities were sent last frame
	
	U32				entSendCountMax;
	U8				entSentLastFrameInfo[MAX_ENTITIES_PRIVATE];
	S32*			sentEntIndices;

	U32				doSendPacketVerifyData			: 1;
	U32				isExpectingEntityDiffs			: 1;
	U32				isExpectingMapStateDiff			: 1;
	U32				hasFullOutputBuffer				: 1;
	
	S32				accessLevelOfSentCommands;

	char*			clientInfoString;

	bool			clientLoggedIn;
	bool			clientWasLoggedIn;
	bool			noTimeOut;
	bool			disconnected;
	bool			readyForGeneralUpdates;

	int				clientLangID;

	//this packet is built by the handlers for TESTCLIENT commands and hangs around waiting to be sent every frame
	Packet			*pPendingTestClientCommandsPacket;

	//for each client, the Reference system tracks a cache of what referents it thinks that client already has
	ResourceCache	*pResourceCache;

	U32				lastWorldUpdateTime, lastWorldSkyUpdateTime; // used by WorldLib

	U32				demo_last_full_update_time; // used by demo record
	
	MovementClient*	movementClient;
	
	TimedCallback*	corruptionCallback;

	// For validation of requested UGC projects later
	UGCSearchResult* ugcSearchResult;

    // The time when we stopped sending client updates.  A field for debugging loading screen hangs.
    U32				debugStopUpdateTime;

	// The time we last got a client patching message, the greater of debugStopUpdateTime and uClientPatchTm are used for timing out
	U32				uClientPatchTm;

} ClientLink;

void gslPreMain(const char* pcAppName);
void gslMain(int argc, char **argv);
void gslEntityFrameUpdate(Entity* pent);

struct LocalTransactionManager;

//enum describing how the gameserver decided on its port and so forth. 
typedef enum enumGSLHowInitted
{
	GSLHOWINITTED_NONE,
	GSLHOWINITTED_NO_MAPMANAGER_MADE_UP_PORT,
	GSLHOWINITTED_MAPMANAGER_HANDSHAKE_FAILED_MADE_UP_PORT,
	GSLHOWINITTED_MAPMANAGER_HANDSHAKE_SUCCEEDED,
} enumGSLHowInitted;

//global state variables for all GameServers
typedef struct GameServerLibState
{
	GameServerDescription gameServerDescription;

	const char *pCreateNewUGCMap;

	char cmdLineMapPublicName[1024];
	
	FrameLockedTimer* flt;

	CoarseTimerManager* frameTimerManager;

	struct {
		struct {
			F32 acc;
			F32 interval;
		} seconds;
	} entSend;

	struct {
		struct {
			F32 scale;

			// These two times get scaled and clipped to 0.5s max.
			
			F32 cur;		// Elapsed time of previous frame.
			F32 prev;		// Elapsed time of two frames ago.
		} sim;
		
		// This is unscaled real world time.

		struct {
			F32 cur;		// Elapsed time of previous frame.
			F32 prev;		// Elapsed time of two frames ago.
		} reality;
	} secondsElapsed;
	
	F32 server_fps;
	U32 uiStartTime;

	struct {
		U32				lastClientConnectInstance;
		NetListen**		netListens;
		
		U32				notLoggedInCount;
		U32				loggedInCount;
		
		S32				linkCorruptionFrequency;
	} clients;

	enumGSLHowInitted eHowInitted;

	//if true, the you are a web request server
	bool gbWebRequestServer;

	//if true, the you are a web gameserver
	bool gbGatewayServer;

	//if true, the you are a serverBinner
	bool gbServerBinner;

	//if true, then you should stop accepting logins
	bool bLocked;

	//if true, we are currently doing ugc preview
	bool bCurrentlyInUGCPreviewMode;

	//keeps track of when we last requested a new port num from the map manager (in case map manager is down)
	U32 iLastTimeRequestedPort;

	//For error-tracking purposes
	U32 *piFailedPorts;
	
	S32 threadPriorityOverride;

	int iLowLevelIndexOnController; //saved here so that when we send our updates to the controller, it can 
		//find us quickly

	REF_TO(UGCProject) hUGCProjectFromSubscription;
	UGCProjectData *pLastPlayData;
	bool bAtomicPartsOfUGCPublishHappening;

	REF_TO(VirtualShard) hVirtualShardFromSubscription;

	bool bInformedMapManagerMapIsDoneLoading;

	int iLaggyFrames;
} GameServerLibState;

extern GameServerLibState gGSLState;
#define GAMESERVER_VSHARD_ID (gGSLState.gameServerDescription.baseMapDescription.iVirtualShardID)

// project server config, which are loaded from ProjectServerConfig.txt

AUTO_STRUCT;
typedef struct WebGameEventData
{
	const char *pcShardName;			AST( NAME(ShardName) POOL_STRING)
	const char *pcURL;					AST( NAME(URL))

} WebGameEventData;

AUTO_STRUCT;
typedef struct ProjectGameServerConfig
{
	S32 iMaximumMessagesPerLevelUp;		AST( NAME(MaximumMessagesPerLevelUp) )
	S32 iSilenceTime;					AST( NAME(SilenceTime) )
	S32 iStasisTime;					AST( NAME(StasisTime) )
	char *pLifetimeChannelName;			AST( NAME(LifetimeChannel) ESTRING DEFAULT("Lifetime"))

	STRING_EARRAY ppShardGlobalChannels; AST( NAME(ShardGlobalChannels) ESTRING)

	// The shard url pairs
	EARRAY_OF(WebGameEventData)	eaWebGameEventData;

} ProjectGameServerConfig;

extern ProjectGameServerConfig gProjectGameServerConfig;

//internal GSL functions
void gslSetConsoleTitle(void);
int gslNetworkInit(void);
void gslSleepForRestOfFrame(FrameLockedTimer* flt, F32 frameMinSeconds);
void gslPlayerLoggedIn(ClientLink* clientLink, Entity* e, S32 noTimeout, int locale);

void gslClientPrintfColor(ClientLink* clientLink, U32 color, const char* format, ...);
void gslClientGetEntityString(ClientLink* clientLink, char* bufferOut, S32 bufferOutLen);

#define entGetResourceCache(pEnt) SAFE_MEMBER3((pEnt), pPlayer, clientLink, pResourceCache)

Entity* player_FindInteractTarget(Entity* e, F32 fCheckDist);

//called every 10 seconds
void gslGameSpecificPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

//called every 5 seconds
void gslPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

//because there is no GameServer_Http.h
void PurgeScreenShotCache(void);

int gslDisconnectCallback(NetLink *link, ClientLink *clientLink);
int gslConnectCallback(NetLink *link, ClientLink *clientLink);

const char *gslGetGameServerName(void);


//once-per-frame function for game server http
void GameServerHttp_OncePerFrame(void);

// Used for tracking playable volumes
void gslPlayableCreate(WorldVolumeEntry *ent);
void gslPlayableDestroy(WorldVolumeEntry *ent);
WorldVolumeEntry** gslPlayableGet(void);


//if true, then we assume we can get garbage data from clients at any moment, and error instead of crashing
//when that happens
bool gslClientsAreUntrustworthy(void);

//call this when a client packet contains corrupt data. This handles logging and force-logout-ing
void gslHandleClientPacketCorruption(ClientLink *link, const char *message);


// Logging for activity streams.  Terminate the parameters with a NULL.
void logActivity(struct Player *pPlayer, const char *type, ...);

bool gslLoadZoneMapByName(const char *map_name);


bool gslStateIsWaitingForSomeSubscribedContainers(void);

#define gslFrameTimerAddInstance(tag) coarseTimerAddInstance(gGSLState.frameTimerManager, tag)
#define gslFrameTimerStopInstance(tag) coarseTimerStopInstance(gGSLState.frameTimerManager, tag)

//if true, the game server is currently in the midst of an "atomic" operation, or something like that, and thus
//can not time out and kill itself until that operation completes
bool gslMapCanNotTimeOutRightNow(void);

void TellControllerWeMayBeStallyForNSeconds(int iNumSeconds, char *pReason);
void TellControllerToLog(char* strLog);


typedef struct MapPartitionSummary MapPartitionSummary;
void HereIsPartitionInfoForUpcomingMapTransfer(U32 iCommandID, ContainerID iPlayerID, U32 uPartitionID, MapPartitionSummary *pSummary);





#endif
