/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ASL_QUEUE_H
#define ASL_QUEUE_H

#pragma once
GCC_SYSTEM

#include "GlobalTypes.h"
#include "referencesystem.h"
#include "Message.h"
//#include "MapDescription.h"

#include "queue_common_structs.h"
#include "queue_common_structs_h_ast.h"

typedef struct Entity Entity;
typedef struct PlayerQueueInfo PlayerQueueInfo;
typedef struct PlayerTeamMember PlayerTeamMember;
typedef struct QueueInfo QueueInfo;
typedef struct NOCONST(QueueInfo) NOCONST(QueueInfo);
typedef struct QueueDef QueueDef;
typedef struct QueueGroupDef QueueGroupDef;
typedef struct QueueMatch QueueMatch;
typedef struct QueueMember QueueMember;
typedef struct NOCONST(QueueMember) NOCONST(QueueMember);
typedef struct QueueInstance QueueInstance;
typedef struct NOCONST(QueueInstance) NOCONST(QueueInstance);
typedef struct NOCONST(QueueChallengeSetting) NOCONST(QueueChallengeSetting);
typedef struct QueueMemberPrefs QueueMemberPrefs;
typedef struct NOCONST(QueueMemberPrefs) NOCONST(QueueMemberPrefs);
typedef struct RandomActiveQueueList RandomActiveQueueList;
typedef struct RandomActiveQueues RandomActiveQueues;
typedef struct ActiveRandomQueueDef ActiveRandomQueueDef;


#define QUEUE_MAX_HISTORY 10

//How long a queue attempts to do simple matching before it tries brute force
#define QUEUE_BRUTE_FORCE_COUNT 20

// Launch delay when failure rate is low (seconds)
#define QUEUE_FAILED_LAUNCH_DELAY_NORMAL 5
#define QUEUE_FAILED_LAUNCH_COUNT_NORMAL 5
// Launch delay when failure rate is high
#define QUEUE_FAILED_LAUNCH_DELAY 120

#define QUEUE_FORCED_REMOVAL_MESG "QueueServer_ForcedRemoval"
#define QUEUE_CANCEL_MAP_LAUNCH_MESG "QueueServer_CancelPrivateLaunch"

AUTO_STRUCT;
typedef struct CachedTeamStruct
{
	ContainerID iTeamID;		AST(KEY)
		//Team container ID

	U32 *eaiEntityIds;
		//Who is on the team
} CachedTeamStruct;

AUTO_STRUCT;
typedef struct QueueTeamJoinTime
{
	ContainerID iTeamID;			AST(KEY)
		// The team container's ID

	U32 uiTime;
		//The time they joined the queue.  Used to override the join time of the queue member so they don't get processed until after this time.
} QueueTeamJoinTime;

AUTO_STRUCT;
typedef struct QueueCBStruct
{
	U32 iQueueID;
	U32 iInstanceID;
	U32 iEntID;
	U32 iTeamID;
	U32 iEntAccountID;
	U32 iSubjectID1;
	U32 iSubjectID2;
	S64 iMapKey;
	U32 iOwnerID;
	S32 iQueueVarVal;
	bool bPreventMapLaunch;
	const char* pchQueueVar;
	const char* pchMapName;	AST(POOL_STRING)
	QueueMatch* pMatch;
	QueueIntArray* pInviteList;
	QueueInstanceParams *pParams;
	QueueMemberPrefs *pPrefs;
} QueueCBStruct;

AUTO_STRUCT;
typedef struct QMapUpdate
{
	S64 iMapKey;
		//Which map/partition the player is going to
	U32 uiInstanceID;
		//Which instance
	U32 uAverageWaitTime;
		//The running average wait time for players to get into this instance
	U32 uWaitTime;
		//The wait time to get into this map
	QueueMapState eState;
		//Which state to set
	QueueMapState eLastState;
		//The previous state of the map
	QueueMatch* pMatch;
		//The match members that need to be added
	char* pchReason;
		// The reason for this map update
} QMapUpdate;

AUTO_STRUCT;
typedef struct QMemberUpdate
{
	ContainerID uiMemberID;
		//Which member
	U32 uiInstanceID;
		//Which instance
	PlayerQueueState eState;
		//Which state to set
	PlayerQueueState eLastState;
		//The member's last state
	U32 uiTeamID;
		//Which team to set
	S32 iGroupIndex;
		//The group index to set
	S64 iMapKey;
		//The map/partition that the member has been offered
	char* pchMessageKey;
		//The message to send to the player after this update completes
	U32 bUpdateTeam : 1;
		//If this is true, set the member's team ID to uiTeamID
	U32 bUpdateGroup : 1;
		//If this is true, set the member's group index to iGroupIndex
	U32 bUpdateMap : 1;
		//If this is true, set the member's map to uMapID
	U32 bWarning : 1;
		//If this is true, then send pchMessageKey as a warning notification
} QMemberUpdate;

AUTO_STRUCT;
typedef struct QMemberUpdateList
{
	EARRAY_OF(QMemberUpdate) eaUpdates;
} QMemberUpdateList;

AUTO_STRUCT;
typedef struct QInstanceUpdate
{
	U32 uiInstanceID;
		//Which instance
	U32 bRemove : 1;
		//Whether or not to remove the instance
	U32 bOvertime : 1;
		//Whether or not the instance is in overtime
	U32 bNewMap : 1;
		//Whether or not there is a new map loading
} QInstanceUpdate;

AUTO_STRUCT;
typedef struct QGeneralUpdate
{
	QInstanceUpdate*	pInstanceUpdate;
	QMapUpdate*			pMapUpdate;
	QMemberUpdate*		pMemberUpdate;
} QGeneralUpdate;

AUTO_STRUCT;
typedef struct QUpdateData
{
	ContainerID			uiQueueID;	AST(KEY)
	QGeneralUpdate**	eaList;
} QUpdateData;

AUTO_STRUCT;
typedef struct QUpdateList
{
	EARRAY_OF(QUpdateData) eaQueues;
} QUpdateList;


AUTO_STRUCT;
typedef struct QueuePenaltyData
{
	ContainerID uEntID; AST(KEY)
	QueuePenaltyCategoryData** eaCategoryData;
} QueuePenaltyData;

////////////////////////////////////////////////////////////////////////////
// Random queues
void aslQueue_LoadRandomQueueLists(void);

////////////////////////////////////////////////////////////////////////////

void aslQueue_PenalizePlayerByID(U32 uEntID, QueueDef* pDef);

void aslQueue_ReloadQueues(const char *pchRelPath, int UNUSED_when);		// Only use this on the QueueServer. Otherwise use Queues_ReloadQueues

void aslQueue_Log(SA_PARAM_NN_VALID QueueDef* pDef, 
	SA_PARAM_OP_VALID QueueInstance* pInstance, 
	SA_PARAM_NN_STR const char* pchString);

	//Finds a queue from the queue's container ID. Either the Local version, or the container.
QueueInfo* aslQueue_GetQueueLocal(ContainerID iQueueID);
QueueInfo* aslQueue_GetQueueContainer(ContainerID iQueueID);

QueueInfo* aslQueue_FindQueueByNameLocal(SA_PARAM_OP_STR const char* pchQueueName);			//Finds a localqueue by name

QueueMember *aslQueue_FindPlayer(SA_PARAM_OP_VALID QueueInfo *pQueueInfo, ContainerID iEntID);
	//Finds a member in the queue

int aslQueue_NumMembersInState(SA_PARAM_NN_VALID QueueInfo* pQueue, PlayerQueueState eState);

QMemberUpdate* aslQueue_AddMemberStateChangeEx(SA_PARAM_OP_VALID QMemberUpdate*** peaUpdates, SA_PARAM_NN_VALID QueueInstance* pInstance, SA_PARAM_NN_VALID QueueMember* pMember, PlayerQueueState eState, U32 uTeamID, bool bUpdateTeam, S32 iGroupIndex, bool bUpdateGroup, S64 iMapKey, bool bUpdateMap);
QMemberUpdate* aslQueue_AddMemberStateChange(SA_PARAM_OP_VALID QMemberUpdate*** peaUpdates, SA_PARAM_NN_VALID QueueInstance* pInstance, SA_PARAM_NN_VALID QueueMember* pMember, PlayerQueueState eState);
#define aslQueue_MemberQueued(pMember) ((pMember) ? (pMember->eState == PlayerQueueState_InQueue) : (false))
S32 aslQueue_trh_MemberActivated(ATH_ARG NOCONST(QueueMember) *pMember);
#define aslQueue_MemberActivated(pMember) aslQueue_trh_MemberActivated(CONTAINER_NOCONST(QueueMember, (pMember)))

// Command to create a new instance of a map for the queueing system
void aslQueue_MapCreateRequest(SA_PARAM_NN_VALID QueueDef *pDef, SA_PARAM_NN_VALID QueueInstance *pInstance, const char* pchMapName, U32 uiRequestID, QueueMember **ppMatchMembers);


///////////////////////////////////////
//  Random Queue
void aslQueue_InitRandomQueueLists(void);
int aslQueue_PickNextRandomQueue(RandomActiveQueueList *pList, int **peaIllegalIndexs);
ActiveRandomQueueDef *aslQueue_ActivateRandomQueue(RandomActiveQueueList *pList, int iQueueIndex);
bool aslQueue_IsRandomQueueActive(QueueDef *pDef);

///////////////////////////////////////

//Called while in normal run mode
int QueueServerOncePerFrame(F32 fElapsed);

//Called during startup while waiting for the containers call to return
int QueueServerStartupOncePerFrame(F32 fElapsed);

int QueueServerLibInit(void);

extern CachedTeamStruct **s_eaCachedTeams;
extern S32 s_bQueueServerReady;

//Is the server ready?
__forceinline bool aslQueue_ServerReady(void) { return s_bQueueServerReady; }

void aslQueue_SendChatMessageToMembersEx(SA_PARAM_NN_VALID QueueInfo* pQueue, ContainerID uInstanceID, const char* pchMessageKey, ContainerID uSubjectID, ContainerID uIgnoreEntID, PlayerQueueState eState, bool bPrivate);
#define aslQueue_SendPrivateChatMessageToMembers(pQueue, uInstanceID, pchMessageKey, uSubjectID, uIgnoreEntID, eState) aslQueue_SendChatMessageToMembersEx(pQueue, uInstanceID, pchMessageKey, uSubjectID, uIgnoreEntID, eState, true)
#define aslQueue_SendChatMessageToMembers(pQueue, uInstanceID, pchMessageKey, uSubjectID, uIgnoreEntID, eState) aslQueue_SendChatMessageToMembersEx(pQueue, uInstanceID, pchMessageKey, uSubjectID, uIgnoreEntID, eState, false)

void aslQueue_LeaveAll(ContainerID iEntID, bool bLeaveIfInMap);

bool aslQueue_trh_UpdateInstance(ATH_ARG NOCONST(QueueInfo)* pQueue, QInstanceUpdate* pUpdate);
#define aslQueue_UpdateInstance(pQueue,pUpdate) aslQueue_trh_UpdateInstance(CONTAINER_NOCONST(QueueInfo, (pQueue)),pUpdate)

bool aslQueue_trh_UpdateMap(ATH_ARG NOCONST(QueueInfo) *pQueue, QMapUpdate* pUpdate);
#define aslQueue_UpdateMap(pQueue,pUpdate) aslQueue_trh_UpdateMap(CONTAINER_NOCONST(QueueInfo, (pQueue)),pUpdate)

bool aslQueue_trh_UpdateMember(ATH_ARG NOCONST(QueueInfo) *pQueue, QMemberUpdate* pUpdate);
#define aslQueue_UpdateMember(pQueue,pUpdate) aslQueue_trh_UpdateMember(CONTAINER_NOCONST(QueueInfo, (pQueue)),pUpdate)

// Map creation time stuff
void aslQueue_CreateNewMap(QueueInfo* pQueue, QueueInstance* pInstance, QueueDef* pDef, bool bTransact, const char *pchDefaultMapName);

NOCONST(QueueGameSetting)* aslQueue_trh_FindSetting(ATH_ARG NOCONST(QueueInstance)* pInstance, const char* pchQueueVar);
#define aslQueue_FindSetting(pInstance, pchQueueVar) (QueueGameSetting*)aslQueue_trh_FindSetting(CONTAINER_NOCONST(QueueInstance, pInstance), pchQueueVar)

#endif // ASL_QUEUE_H
