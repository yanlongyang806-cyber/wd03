#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ReferenceSystem.h"
#include "GroupProjectCommon.h"

typedef struct GroupProjectState GroupProjectState;
typedef enum GroupProjectType GroupProjectType;
typedef struct GroupProjectContainer GroupProjectContainer;
typedef struct Guild Guild;
typedef struct Entity Entity;
typedef U32 ContainerID;
typedef enum GlobalType GlobalType;
typedef struct ItemDef ItemDef;

AUTO_ENUM;
typedef enum GroupProjectQueuedMapState
{
    QueuedMapState_None,

    // First we ask the queue server for the owner of the instance.
    QueuedMapState_WaitingForOwnerID,

    // If there is an owner, then we get a subscription to the owner so we can get his guild ID.
    QueuedMapState_WaitingForOwner,

    // If there is no owner, then just wait for a player to show up.
    QueuedMapState_WaitingForPlayerInGuild,
} GroupProjectQueuedMapState;

AUTO_ENUM;
typedef enum GroupProjectMapContainerSubscriptionState
{
    MapContainerSubState_None,

    // We initially wait for the referenced container to arrive.  In most cases it will arrive quickly and we are done.
    MapContainerSubState_InitialWait,

    // If the initial wait times out, we query the objectDB to see if the container exists.
    MapContainerSubState_SentContainerExistsQuery,

    // The objectDB told us the container does not exists.  The container reference is valid and returns NULL.
    MapContainerSubState_NoContainer,

    // The objectDB told us the container exists, so we wait until it shows up.
    MapContainerSubState_FinalWait,

    // The container has arrived and the reference is valid.
    MapContainerSubState_ContainerRefValid,
} GroupProjectMapContainerSubscriptionState;

AUTO_STRUCT;
typedef struct GroupProjectMapPartitionState
{
    int iPartitionIdx;

    // What type of group project do we want on this map.
    GroupProjectType projectType;

    // The allegiance of the guild owner of the map.
    STRING_POOLED allegianceName;       AST(POOL_STRING)

    // Used during development to override the guild allegiance.
    STRING_POOLED overrideAllegiance;   AST(POOL_STRING)

    // The container type of the group project container for this map.
    GlobalType containerType;

    // The container ID of the group project container for this map.
    ContainerID containerID;

    REF_TO(GroupProjectContainer) guildProjectContainerRef;  AST(COPYDICT(GroupProjectContainerGuild))

    // This is used to get the allegiance of the guild that owns the group project that is being referenced by this map.
    REF_TO(Guild) guild;  AST(COPYDICT(Guild))

    // This is used to get the guild of the queue owner for queued maps.
    REF_TO(Entity) queuedInstanceOwner; AST(COPYDICT(EntityPlayer))

    // State variable used for finding which guild's group project container to use for a queued map.
    GroupProjectQueuedMapState queuedMapState;

    // The time that the current queued map state was set.
    U32 queuedMapStateTime;

    // The last time we scanned for a player in a guild.
    U32 lastPlayerScanTime;

    // The time that the guild and group project references were set.
    U32 referenceSetTime;

    // State variable used for determining whether the group project container reference is valid.
    GroupProjectMapContainerSubscriptionState subState;

    // The time we entered the current state.
    U32 subStateTime;

    // This flag is set to true when an alert is sent due to the group project container not arriving after an extended time.
    bool containerLateAlertSent;

} GroupProjectMapPartitionState;

void gslGroupProject_SchemaInit(void);
void gslGroupProject_PartitionLoad(int iPartitionIdx);
void gslGroupProject_PartitionUnload(int iPartitionIdx);
GroupProjectState *gslGroupProject_GetGroupProjectStateForMap(GroupProjectType projectType, const char *projectName, int iPartitionIdx);
const char *gslGroupProject_GetGuildAllegianceForGroupProjectMap(int iPartitionIdx);
GroupProjectMapPartitionState *gslGroupProject_GetGroupProjectMapPartitionState(int iPartitionIdx);
void gslGroupProject_SetQueuedMapState(GroupProjectMapPartitionState *pPartitionState, GroupProjectQueuedMapState queuedMapState);
void gslGroupProject_SetInstanceOwner(int iPartitionIdx, ContainerID ownerID);
bool gslGroupProject_GroupProjectMapDataReady(GroupProjectType projectType, int iPartitionIdx);
bool gslGroupProject_GetGroupProjectNumericValueFromMap(int iPartitionIdx, GroupProjectType projectType, const char *projectName, const char *numericName, S32 *pRet, char **errString);
S32 gslGroupProject_GetGroupProjectNumericFromPlayer(Entity *playerEnt, GroupProjectType projectType, const char *projectName, const char *numericName);
