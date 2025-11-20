/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLQUEUE_H
#define GSLQUEUE_H

#pragma once
GCC_SYSTEM

//For scoreboard state, ugh
// TODO(BH): Remove this quickly for leaderboard tech
#include "mapstate_common.h"
#include "Queue_common.h"
#include "PvPGameCommon.h"


typedef struct Entity Entity;
typedef struct PlayerQueue PlayerQueue;
typedef struct PlayerQueueInstance PlayerQueueInstance;
typedef struct QueueDef QueueDef;
typedef struct QueueInfo QueueInfo;
typedef struct QueueList QueueList;
typedef struct QueueMatch QueueMatch;
typedef struct QueueInstanceParams QueueInstanceParams;
typedef struct Team Team;
typedef struct MapLeaderboard MapLeaderboard;

extern int g_iDebugQueueServer;

#define qDebugPrintf(format, ...) if(g_iDebugQueueServer) printf(format,__VA_ARGS__)

AUTO_STRUCT;
typedef struct QueuePendingChatMessage
{
	ContainerID uEntID;
	ContainerID uSubjectID;
	F32 fTimePending;
	char* pchChannel;
	const char* pchMessageKey; AST(POOL_STRING)
} QueuePendingChatMessage;


// This is where entities go that need to be put into a local team, but there isn't room
AUTO_STRUCT;
typedef struct EntLocalTeamPending
{
	U32 iEntID;					AST(KEY)
	U32 iTimestamp;
	U32 bTimedOut : 1;
} EntLocalTeamPending;

AUTO_STRUCT;
typedef struct LocalTeam
{
	S32 iLocalID;				AST(KEY)
		//The local ID key. 

	S32 iGroupIndex;
		// The queue group index that this local team belongs to

	ContainerID iTeamID;
		//The team's ID

	REF_TO(Team) hTeam;
		// The team structure on the team server

	INT_EARRAY piMembers;
		// The Entity IDs who should be on this team.

	INT_EARRAY piOnlineMembers;
		// The Entity IDs that shoudl be on this team and are also on the map

	INT_EARRAY piNewMemberList;
		// The latest member list this tick. Constructed during gslTeam_CreateSeparateTeamsFromGroups and gslTeam_CreateMergedTeamsFromGroups and
		//   then copied into piMembers. Since multiple local teams may be associated with a group, we need to temporarily store this on the struct

	U32 uNextTeamUpdateRequestTime;
		//When to request a new team container for this local team

	U32 uLastUpdateRequestID;
		//For tracking transaction requests to prevent spamming during DB load.
	
	U32 bUpdated : 1;
		//Did this get updated this tick

	U32 bUpdatedMembers : 1;
		//Did the members list get updated (need to push an update to the team server)

	U32 bCreatedTeam : 1;

} LocalTeam;

//The queue match taking place on this server
AUTO_STRUCT;
typedef struct QueuePartitionInfo
{
	int iPartitionIdx;						AST(KEY)
	bool bDebug;

	// When our partition starts up, we need to find out from the
	//   queue server if we are a queue-started instance and get
	//   appropriate info if we are.
	bool bQueueServerInstanceInfoRequested;
	bool bQueueServerInstanceInfoReceived;

	MapLeaderboard *pLeaderboard;
	int leaderboard_counter;
	int iLastScoreSize;
	QueueMatch *pMatch;
	
	// if the queue info has been cached
	bool bMergeGroups;

	// Auto team stuff
	bool bAutoTeamLocalTeamInitialized;	// Lets us know whether we initialized or not in the update
	bool bAutoTeamsEnabled;				// Pulled initially from the QueueDef. May be turned off when the match ends.
	bool bAutoTeamMembersCanBeOnOtherMap;	// Pulled from the QueueDef/QueueConf. Determines whether the team is set up allowing members to leave the map
	bool bAllowNonGroupedEntities;			// Set if the map was NOT started by the QueueServer (vs. a Map move or such). We boot non-grouped entities unless this is true
	
	LocalTeam** ppLocalTeams;
	EntLocalTeamPending** ppEntsLocalTeamPending;
	QueueGroupConcedeData** eaConcedeData;
	QueueGroupKickData** eaVoteKickData;
	QueueGameInfo* pGameInfo;
	U32 uQueueHadAnyPlayersTime;
	U32 uMaxLocalTeamID;
	U32 uWaitingForMatchInfoStartTime;
	S32 iQueueMapDifficulty;
	S32 iMaxLocalTeamsPerGroup;

	// a list of players that have already been placed on teams
	U32 *eaiPlacedMembers;

} QueuePartitionInfo;

typedef enum PVPAnimID
{
	kPVPAnimID_TeamFX = 1,
	kPVPAnimID_DOM_Step_FX,
	kPVPAnimID_DOM_Cap_FX,
	kPVPAnimID_DOM_Contest_FX
}PVPAnimID;

AUTO_STRUCT;
typedef struct QueuePartitionMatchUpdate
{
	U32 uPartitionID;
	QueueMatch* pMatch;
} QueuePartitionMatchUpdate;

typedef void (*UIQueueInfoFunction)(QueuePartitionInfo *pInfo);

LocalTeam* gslQueue_FindLocalTeamByID(U32 uTeamID);
void gslQueue_LocalTeamUpdate(QueuePartitionInfo* pInfo);
void gslQueue_LocalTeamTick(F32 fElapsed);

PlayerQueueInstance* gslQueue_FindPrivateInstance(Entity *pEnt, PlayerQueue** ppQueue);

QueuePartitionInfo *gslQueuePartitionInfoFromIdx(int iPartitionIdx);
void gslQueuePartition_ForEachPartition(UIQueueInfoFunction function);

void gslQueue_AddPendingMatchUpdate(U32 uPartitionID, QueueMatch* pMatch);
void gslQueue_UpdateQueueMatch(U32 uPartitionID, QueueMatch *pMatchMembers);
void gslQueuePartition_PrintMatchList(QueuePartitionInfo *pInfo);

bool gslQueue_CanSidekick(void);

void gslQueue_PlayerAddQueue(Entity *pEnt, QueueDef *pDef);

void gslQueue_RequestPlayerQueuesProcess(Entity *pEnt);
void gslQueue_ForcePlayerQueuesProcess(Entity *pEnt);

// ------------------------------
// Map Queue Processing
// ------------------------------

void queue_TickQueues(F32 fTime);

// Returns the map key
S64 gslQueue_FindBestMapToJoin(Entity* pEnt, const char* pchQueueName, U32* puInstanceID);
void gslQueue_Join(Entity *pEnt, const char* pchQueueName, const char* pchPassword, U32 uiTeamID, U32 uiInstanceID, S64 iMapKey, U32 bJoinNewMap, U32 bAutoAcceptOffer, QueueMemberPrefs *pMemberPrefs);
void gslQueue_JoinCmd(Entity *pEnt, const char* pchQueueName);
void gslQueue_TeamJoin(Entity *pEnt, const char* pchQueueName, U32 uiInstanceID, S64 iMapKey, U32 bJoinNewMap);
void queue_LeaveAllQueues(SA_PARAM_OP_VALID Entity* pEnt);

//Helper function to get the current queue def this game server is using
QueueDef *gslQueue_GetQueueDef(int iPartitionIdx);
//Returns true if this pvp match ignores mods marked to be ignored... 
bool gslQueue_IgnorePVPMods(int iPartitionIdx);

QueueCannotUseReason gslEntCannotUseQueue(Entity* pEnt, QueueDef* pDef, S32 bPreValidated, S32 bIgnoreLevelRestrictions, S32 bInActiveQueue);
QueueCannotUseReason gslEntCannotUseQueueInstance(Entity* pEnt, QueueInstanceParams* pParams, QueueDef* pDef,
												  S32 bPreValidated,S32 bIgnoreLevelRestrictions, S32 bCheckMissionRequirements, S32 bCheckCooldown);

// Gives the correct msg key for the cannot use reason
const char* gslGetCannotUseQueueReasonMsgKey(QueueCannotUseReason eReason);
const char* gslGetTeamCannotUseQueueReasonMsgKey(QueueCannotUseReason eReason);

// Returns null if the entity CAN use a queue, returns a message key to display if they cannot
const char *gslEntCannotUseQueueMsgKey(Entity* pEnt, QueueDef* pDef, S32 bPreValidated, S32 bIgnoreLevelRestrictions);
const char *gslEntCannotUseQueueInstanceMsgKey(Entity* pEnt, QueueInstanceParams* pParams, QueueDef* pDef, S32 bPreValidated, S32 bIgnoreLevelRestrictions);
const char *gslEntCannotUseQueuePrefs(Entity *pEnt, QueueMemberPrefs *pPrefs, QueueDef *pDef);


//Called when players login to a new map to update the queue state
void gslQueue_ent_CheckQueueState(Entity *pEnt);

// Called when the scoreboard is set for the pvp/qpve maps
void gslQueue_MapSetStateFromScoreboardStateEx(int iPartitionIdx, ScoreboardState eState, const char* pchWinnerFaction);
#define gslQueue_MapSetStateFromScoreboardState(iPartitionIdx, eState) gslQueue_MapSetStateFromScoreboardStateEx(iPartitionIdx, eState, NULL)
// Prevent the QueueServer from doing further matchmaking on this map
void gslQueue_StopTrackingMapOnQueueServer(int iPartitionIdx);
// Special handling for map transfer failures
void gslQueue_HandleMapTransferFailure(ContainerID uPlayerID);

// Checks whether the queue data on this map is ready
bool gslQueue_WaitingForQueueData(int iPartitionIdx);

void gslQueue_SendWarning(U32 iEntID, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey);
void gslQueue_ResultMessageEx(U32 iEntID, S32 eSubjectType, U32 iSubjectID, S32 eTargetType, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey, bool bIsWarning);
void gslQueue_ResultMessage(U32 iEntID, U32 iTargetEntID, const char *pcQueueName, const char *pcMessageKey);

bool gslQueue_NotifyPlayerCannotUseReason(Entity* pEnt, QueueDef* pDef, QueueCannotUseReason eReason);

// Allows a player to attempt to concede the match by starting a vote
bool gslQueue_ConcedeVote(Entity* pEnt);
bool gslQueue_HasGroupConceded(QueuePartitionInfo *pInfo, S32 iGroupIndex);
void gslQueue_CheckGroupsConceded(int iPartitionIdx, const char* pchFaction, bool bFriendlyFactions, int* piResult);
bool gslQueue_VoteKick(Entity* pEnt, U32 uPlayerID);

// Leave/Abandon stuff
bool gslQueue_IsLeaverPenaltyEnabled(int iPartitionIdx);
bool gslQueue_HandleLeaveMap(Entity* pEnt);
bool gslQueue_HandleAbandonMap(Entity* pInstigatorEnt, ContainerID uAbandoningEntID, bool bLeaveTeam);
void gslQueue_EntAbandonThisMapAndQueue(ContainerID uEntID, ContainerID uMapID, U32 uPartitionID, bool bDoMapLeaveIfOnMap);

bool gslQueue_HandlePlayerLogin(Entity* pEnt);
bool gslQueue_HandlePlayerLogout(Entity* pEnt);
void gslQueue_HandlePlayerKilled(Entity* pEnt, Entity* pKiller);
void gslQueue_HandlePlayerNearDeathEnter(Entity* pEnt, Entity* pKiller);

bool gslQueue_SendSystemMessageToChannel(SA_PARAM_NN_VALID Entity* pEnt, const char* pchChannel, const char* pchMessageKey, U32 uSubjectID, bool bAddToPendingOnFail, bool bAlwaysSucceed);

bool gslQueue_RemoveFromGroup(Entity *pEnt, QueueMatch *pInfo);
void gslQueue_ClearQueueList(void);

//Partition functions
void queue_PartitionLoad(int iPartitionIdx);
void queue_PartitionUnload(int iPartitionIdx);
void queue_MapLoad(void);
void queue_MapUnload(void);
bool queue_AddPartitionInfo(QueuePartitionInfo* pInfo);
bool queue_DestroyPartitionInfo(QueuePartitionInfo* pInfo);

void gslQueue_SetupDefaultMatch(QueuePartitionInfo *pInfo);

const char *pvp_GetPlayerSpawnPoint(Entity *pEnt);

extern QueueList *g_pQueueList;

bool gslQueue_IsQueueMap(void);
void gslQueueRefresh(Entity *pEnt);

bool gslQueue_IsRandomQueueActive(QueueDef *pDef);

// Return true if the entity is in a MatchGroup local to this partition, or if there is no Match yet.
bool gslQueue_EntIDIsInMatchGroup(QueuePartitionInfo *pInfo, int iEntID);

void glsQueue_DoLogoutKickForNonMembership(QueuePartitionInfo *pInfo, U32 uEntID);


#endif //GSLQUEUE_H
