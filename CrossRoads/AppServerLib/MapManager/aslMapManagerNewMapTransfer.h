#pragma once


/*

How a mapmanager restarts in an already-running production shard.

When a mapmanage restarts, some number of gameservers are presumably already running. We need the restarting mapmanager to take over responsibility
for them, which means learning about them. Some of these gameservers are already fully running (in gslRunning), meaning they know all about 
themselves. Other gameservers are in various startup stages. They are now worthless, because it was the previous mapmanager that started them 
and that knew what it wanted to do with them.

So we start with the mapmanager in MMLSTATE_INIT. It fires off a GetServerList remote command to the controller and asks for a list of all 
gameservers that exist. It then switches to MML_STATE_SENT_STARTING_REMOTE_COMMANDS, where it remains for a while.

When the server list is returned from the controller, it sets a flag, and it also passes all the gameservers off to the new map transfer 
code (by calling NewMapTransfer_AddPreexistingMap for gameservers in gslRunning and NewMapTransfer_NonReadyPreexistingGameServerExists for others). 
The new map transfer code then tells the controller to kill all nonready servers, and then sends a remote command 
(gslGetGameServerDescriptionForNewMapTransfer) to each ready gameserver asking for its description. When it gets a description back it 
verifies that it's valid. If it's not valid (very unlikely, would have to involve someone starting up gameservers other than the map manager 
such that two partitions had the same public index or something) it kills the GS. Otherwise it fills in the info internally and 
now knows everything it needs to know about that GS.

Meanwhile, the mapmanger as a whole is still in MML_STATE_SENT_STARTING_REMOTE_COMMANDS and will so remain until 
NewMapTransfer_ReadyForNormalOperation() returns true. 
	
So NewMapTransfer_ReadyForNormalOperation() returns true when the number of GS descriptions it has gotten back is the same as the 
number of requests it sent. This means that at shard startup time, when there are no gameservers, it returns true instantly.

However, there's a chance that a GS will be in gslRunning but will be infinitely looped or something, meaning that it will never 
respond to the command (if a GS goes away during this time, the command will return with a failure, so that's fine). Therefore, 
there are two fallback conditions. There's a first stage fallback where after 5 seconds, if 90% of the gameservers have responded, 
we move on. There's also a second stage fallback where after 30 seconds we move on no matter what.

One important note: preexisting UGC gameservers will know their own zonemapinfos, but the mapmanager at startup time won't. Therefore, 
as part of the handshake, each preexisting gameserver sends along a copy of its zonemapinfo, so the mapmanager can add to its local dictionary
any one it doesn't already know about.

So when we are done with MML_STATE_SENT_STARTING_REMOTE_COMMANDS, we do one more important thing... we send the controller a list 
of all gameservers that we are now fully tracking, and tell it "kill all other gameservers" (using RemoteCommand_KillAllButSomeServersOfType). 
This will kill of any other corner case gameservers that were just starting up as we were starting up, and also any gslRunning ones that 
were hung and we didn't hear back from. Also, we now go through all machines and regenerate the lists of what gameserver ports are free.

At this point we finally go into MML_STATE_NORMAL_OPERATION. And (key point) only in NORMAL_OPERATION will the mapmanager respond to the 
normal "I'd like to do a map transfer please" type commands.


This all means that there is a window while the mapmanager is restarting during which all map transfers will fail, both ones that had 
been started with the previous mapmanager, and new ones that are requested while the mapmanager is starting up... but it's necessary to 
keep from any number of ugly situations that could arise when trying to operate with incomplete information.


One optimization that seems worth doing is that when transfer requests come in during the startup states, they could be cached and then 
fulfilled once normal operation starts rather than just failing. But of course transfer requests that came in during the actual appserver.exe 
startup were already failing anyhow, so it's not like it would stop there from being a discontinuity of service.
*/



typedef enum NewMapTransfer_WhyNotAcceptingLogins
{
	WHYNOTACCEPTINGLOGINS_ALLOWED, // Not NOT Accepting. Yay double-negatives!

	// Server reasons
	WHYNOTACCEPTINGLOGINS_TOLD_TO_DIE, // Server was already told to die
	WHYNOTACCEPTINGLOGINS_LOCKED, // locked
	WHYNOTACCEPTINGLOGINS_MAP_BANNED, // This map is in the Map Manager's banned maps list
	WHYNOTACCEPTINGLOGINS_SERVER_HARD_FULL, // Across-Partition Hard player limit exceeded.
	WHYNOTACCEPTINGLOGINS_SERVER_SOFT_FULL, // Across-Partition Soft player limit exceeded

	// Partition reasons
	WHYNOTACCEPTINGLOGINS_CONFIG_NOT_FOUND, // Map category config not found
	WHYNOTACCEPTINGLOGINS_PARTITION_HARD_FULL, // This partition's Hard player limit exceeded
	WHYNOTACCEPTINGLOGINS_PARTITION_SOFT_FULL, // This partition's Soft player limit exceeded

} NewMapTransfer_WhyNotAcceptingLogins;

typedef struct Controller_SingleServerInfo Controller_SingleServerInfo;

void NewMapTransfer_Init(void);
void NewMapTransfer_BeginNormalOperation(void);

void NewMapTransfer_InformMapManagerOfGameServerDeath(ContainerID iContainerID, bool bUnnaturalDeath);


//while transitioning between old and new map transfer code, this tells the old code to ignore this server
bool NewMapTransfer_IsHandlingServer(ContainerID iContainerID);


void NewMapTransfer_RequestNewOrExistingGameServerAddress(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID);

void NewMapTransfer_MapIsDoneLoading(ContainerID iID);

void NewMapTransfer_NormalOperation(void);

void NewMapTransfer_GameServerPortDidntWork(ContainerID iGameServerID, int iPortNum);
void NewMapTransfer_GameServerPortWorked(ContainerID iGameServerID, int iPortNum);

typedef struct DynamicPatchInfo DynamicPatchInfo;
typedef struct ServerLaunchDebugNotificationInfo ServerLaunchDebugNotificationInfo;
typedef struct GameServerExe_Description GameServerExe_Description;
typedef struct TrackedGameServerExe TrackedGameServerExe;

TrackedGameServerExe *NewMapTransfer_LaunchNewServer(GameServerExe_Description *pDescription, char *pReason,  
	DynamicPatchInfo *pPatchInfo, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, char *pExtraCommandLine_In, bool bPreLoad);



bool NewMapTransfer_GameServerIsAcceptingLogins(TrackedGameServerExe *pServer, bool bUseHardLimit, char **ppWhyNot, NewMapTransfer_WhyNotAcceptingLogins *peWhyNot);
bool NewMapTransfer_PartitionIsAcceptingLogins(TrackedGameServerExe *pServer, MapPartitionSummary *pSummary, bool bUseHardLimit, char **ppWhyNot, NewMapTransfer_WhyNotAcceptingLogins *peWhyNot);

void NewMapTransfer_GameServerReportsPlayerBeganLogin(ContainerID iGameServerID, U32 uPartitionID);

void NewMapTransfer_HereIsGSLGlobalInfo_ForMapManager(ContainerID iServerID, GameServerGlobalInfo *pInfo);

char *NewMapTransfer_GetDebugTransferNotificationLinkString(TrackedGameServerExe *pServer);

void NewMapTransfer_HereIsControllerServerInfo(Controller_SingleServerInfo *pServerInfo);

void NewMapTransfer_FixupOldMapSearchInfo(MapSearchInfo *pSearchInfo, char *pReason);

void NewMapTransfer_AddPreexistingMap(ContainerID iContainerID, char *pMachineName, U32 iIP, U32 iPublicIP, int iPid);

bool NewMapTransfer_ReadyForNormalOperation(void);

void NewMapTransfer_NonReadyPreexistingGameServerExists(ContainerID iContainerID);

void NewMapTransfer_CheckForPreloadMaps(void);

void NewMapTransfer_CreatePreloadMap(MapCategoryConfig *pCategory);

void NewMapTransfer_SendDescriptionToGameServer(TrackedGameServerExe *pServer, SlowRemoteCommandID iCmdID);

void NewMapTransfer_DoStartingMaps(void);

void NewMapTransfer_CheckForLaunchCutoffs(void);

void NewMapTransfer_KillGameServerDueToTimeoutIfAppropriate(ContainerID iServerID);

void NewMapTransfer_LogPlayerWasSentToMap(TrackedGameServerExe *pServer, ContainerID iEndContainerID);

void NewMapTransfer_DecayAllRecentlyLogginInCounts(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);