/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslGroupProject.h"
#include "GroupProjectCommon.h"
#include "objSchema.h"
#include "GlobalTypes.h"
#include "ResourceManager.h"
#include "objTransactions.h"
#include "StaticWorld\ZoneMap.h"
#include "gslPartition.h"
#include "Guild.h"
#include "Player.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "gslQueue.h"
#include "GameServerLib.h"
#include "Alerts.h"
#include "gateway/gslGatewayServer.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/gslGroupProject_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

U32 gDebugMapProject = 0;
AUTO_CMD_INT(gDebugMapProject, DebugMapProject);

// How long to wait after setting up the container subscription before asking if the container exists.
U32 gGroupProjectMapSubInitialWait = 5;
AUTO_CMD_INT(gGroupProjectMapSubInitialWait, GroupProjectMapSubInitialWait);

// If the container hasn't arrived by this time, then alert.
U32 gGroupProjectMapSubAlertTime = 30;
AUTO_CMD_INT(gGroupProjectMapSubAlertTime, GroupProjectMapSubAlertTime);

bool gbUsePresentButNullCheck = true;
AUTO_CMD_INT(gbUsePresentButNullCheck, UsePresentButNullCheck);

static GroupProjectMapPartitionState **s_MapPartitionState = NULL;

GroupProjectMapPartitionState *
gslGroupProject_GetGroupProjectMapPartitionState(int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState;
    pPartitionState = eaGet(&s_MapPartitionState, iPartitionIdx);

    devassert(pPartitionState != NULL);
    if ( pPartitionState == NULL )
    {
        ErrorDetailsf("partition index = %d", iPartitionIdx);
        Errorf("gslGroupProject: Attempt to access an invalid partition.");
    }
    else
    {
        devassert(pPartitionState->iPartitionIdx == iPartitionIdx);
        if ( pPartitionState->iPartitionIdx != iPartitionIdx )
        {
            ErrorDetailsf("array index = %d, partition state index = %d", iPartitionIdx, pPartitionState->iPartitionIdx);
            Errorf("gslGroupProject: GroupProjectMapPartitionState has incorrect partition index.");
        }
    }
    return pPartitionState;
}

void
gslGroupProject_SetQueuedMapState(GroupProjectMapPartitionState *pPartitionState, GroupProjectQueuedMapState queuedMapState)
{
    pPartitionState->queuedMapState = queuedMapState;

    pPartitionState->queuedMapStateTime = timeSecondsSince2000();

    if ( gDebugMapProject )
    {
        printf("Set queued map state: %d.\n", queuedMapState);
    }
}

void
gslGroupProject_SetSubState(GroupProjectMapPartitionState *pPartitionState, GroupProjectMapContainerSubscriptionState subState)
{
    pPartitionState->subState = subState;

    pPartitionState->subStateTime = timeSecondsSince2000();

    if ( gDebugMapProject )
    {
        printf("Set sub state: %d.\n", subState);
    }
}

void
gslGroupProject_SetInstanceOwner(int iPartitionIdx, ContainerID ownerID)
{
 	char idBuf[128];

	GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    // Set the reference to to a subscribed copy of the queue instance owner.
    SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), 
        ContainerIDToString(ownerID, idBuf), 
        pPartitionState->queuedInstanceOwner);

    gslGroupProject_SetQueuedMapState(pPartitionState, QueuedMapState_WaitingForOwner);

    if ( gDebugMapProject )
    {
        printf("Set instance owner: %d\n", ownerID);
    }
}

static void
SetGroupProjectReference(GroupProjectMapPartitionState *pPartitionState, GlobalType containerType, ContainerID containerID)
{
    pPartitionState->containerType = containerType;
    pPartitionState->containerID = containerID;

    if ( containerType == GLOBALTYPE_GROUPPROJECTCONTAINERGUILD )
    {
 		char idBuf[128];

		devassert(containerType == GLOBALTYPE_GROUPPROJECTCONTAINERGUILD);

        // Set the reference to to a subscribed copy of the guild group project container.
        SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(containerType), 
            ContainerIDToString(containerID, idBuf), 
            pPartitionState->guildProjectContainerRef);

        // Set the reference to to a subscribed copy of the guild container.
        SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), 
            ContainerIDToString(containerID, idBuf), 
            pPartitionState->guild);
    }

    pPartitionState->referenceSetTime = timeSecondsSince2000();

    if ( gDebugMapProject )
    {
        printf("Set group project reference: %d.\n", containerID);
    }

    // Start the container subscription state machine.
    gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_InitialWait);
}

static void 
GetQueueInstanceOwnerIDCB(TransactionReturnVal *returnVal, void *cbData)
{
    U32 ownerID = 0;
    int partitionID = (intptr_t)cbData;
    int iPartitionIdx = partition_IdxFromID(partitionID);
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    if ( RemoteCommandCheck_aslQueue_GetInstanceOwnerID(returnVal, &ownerID) != TRANSACTION_OUTCOME_SUCCESS )
    {
        RemoteCommand_AlertIfServerTypeDoesntExist(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_QUEUESERVER, "Use of group project container on a queued map requires the queue server to be running", 60*60);
        ownerID = 0;
        if ( gDebugMapProject )
        {
            printf("Error returned when requesting instance owner from queue server.\n");
        }
    }

    if ( pPartitionState )
    {
        if ( ownerID )
        {
            // An ownerID was returned.  We need to subscribe to the player so that we can get the guild ID.
            gslGroupProject_SetInstanceOwner(iPartitionIdx, ownerID);

            if ( gDebugMapProject )
            {
                printf("Received instance owner from queue server.\n");
            }
        }
        else
        {
            gslGroupProject_SetQueuedMapState(pPartitionState, QueuedMapState_WaitingForPlayerInGuild);

            if ( gDebugMapProject )
            {
                printf("Instance owner zero returned from queue server.\n");
            }
        }
    }
}

static void
CheckContainerExistsCB(TransactionReturnVal *returnVal, void *cbData)
{
    int exists = 0;
    int partitionID = (intptr_t)cbData;
    int iPartitionIdx = partition_IdxFromID(partitionID);
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    if ( pPartitionState->subState == MapContainerSubState_SentContainerExistsQuery )
    {
        // We only need to do anything here if we are still in the state waiting for the container exists query.
        if ( RemoteCommandCheck_DBCheckSingleContainerExists(returnVal, &exists) == TRANSACTION_OUTCOME_SUCCESS )
        {
            if ( exists )
            {
                // Still waiting for the container to arrive.
                gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_FinalWait);
            }
            else
            {
                // The container does not exist.
                gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_NoContainer);
            }
        }
        else
        {
            // If the query failed, send an error and keep waiting.
            ErrorDetailsf("containerType=%d, containerID=%d", pPartitionState->containerType, pPartitionState->containerID);
            Errorf("Failed to query database for group project container's existence.");
            gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_FinalWait);
        }
    }
}

static bool s_badMapErrorSent = false;

static void
GroupProjectMapTick(GroupProjectMapPartitionState *pPartitionState)
{
    bool handleActive = false;

    if ( pPartitionState->projectType == GroupProjectType_Guild ) 
    {
        // If the guild project container handle is active, we know the guild.
        handleActive = IS_HANDLE_ACTIVE(pPartitionState->guildProjectContainerRef);

        if ( !handleActive )
        {
            // We need to figure out the ID of the guild that the map's group project belongs to.
            if ( zmapInfoGetIsGuildOwned(NULL) )
            {
                static bool errorGenerated = false;

                // This is a guild owned map, so the guild ID is just the owner ID of the map.
                if ( partition_OwnerTypeFromIdx(pPartitionState->iPartitionIdx) == GLOBALTYPE_GUILD )
                {
                    // Set the reference to a subscribed copy of the guild group project container.
                    SetGroupProjectReference(pPartitionState, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, partition_OwnerIDFromIdx(pPartitionState->iPartitionIdx));

                    handleActive = true;
                }
                else if ( errorGenerated == false )
                {
                    // Generate an error if the map is now owned by a guild.  Throttled to once per gameserver instance.
                    ErrorDetailsf("partitionIdx=%d, ownerType=%d, ownerID=%d", pPartitionState->iPartitionIdx, 
                        partition_OwnerTypeFromIdx(pPartitionState->iPartitionIdx), partition_OwnerIDFromIdx(pPartitionState->iPartitionIdx) );
                    Errorf("GroupProjectMapTick: Map should be guild owned but isn't.");
                    errorGenerated = true;
                }
            }
            else if ( zmapInfoGetMapType(NULL) == ZMTYPE_QUEUED_PVE )
            {
                // Queued maps have more complicated logic to determine which group project container to use, so we have 
                //  a little state machine to keep track of the process.
                switch (pPartitionState->queuedMapState)
                {
                case QueuedMapState_None:
                    {
                        // Ask the queue server for the instance owner.
                        QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(pPartitionState->iPartitionIdx);
                        const char* queueDefName = SAFE_MEMBER2(pInfo, pGameInfo, pchQueueDef);
                        ContainerID mapID;
                        U32 partitionID;
                        S64 mapKey;
                        TransactionReturnVal *returnVal;

                        mapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
                        partitionID = partition_IDFromIdx(pPartitionState->iPartitionIdx);
                        mapKey = queue_GetMapKey(mapID, partitionID);

                        gslGroupProject_SetQueuedMapState(pPartitionState, QueuedMapState_WaitingForOwnerID);

                        returnVal = objCreateManagedReturnVal(GetQueueInstanceOwnerIDCB, (void *)(intptr_t)partitionID);

                        RemoteCommand_aslQueue_GetInstanceOwnerID(returnVal, GLOBALTYPE_QUEUESERVER, 0, queueDefName, mapKey);
                        if ( gDebugMapProject )
                        {
                            printf("Requesting instance owner from queue server.\n");
                        }
                    }
                    break;
                case QueuedMapState_WaitingForOwnerID:
                    // Do nothing here.
                    break;
                case QueuedMapState_WaitingForOwner:
                    {
                        Entity *ownerEnt;

                        // Waiting for the queue instance owner to show up.

                        ownerEnt = GET_REF(pPartitionState->queuedInstanceOwner);
                        if ( ownerEnt )
                        {
                            if ( guild_IsMember(ownerEnt) )
                            {
                                // Set the reference to to a subscribed copy of the guild group project container.
                                SetGroupProjectReference(pPartitionState, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, guild_GetGuildID(ownerEnt));
                                handleActive = true;
                            }
                            else
                            {
                                // Owner is not in a guild, so just look for anyone in a guild.
                                gslGroupProject_SetQueuedMapState(pPartitionState, QueuedMapState_WaitingForPlayerInGuild);
                            }
                        }
                    }
                    break;
                case QueuedMapState_WaitingForPlayerInGuild:
                    {
                        U32 curTime = timeSecondsSince2000();

                        // Iterate players on the map looking for one that is a guild member.

                        // Don't check more than once per second.
                        if ( curTime > pPartitionState->lastPlayerScanTime )
                        {
                            EntityIterator *iter;
                            Entity *playerEnt;

                            pPartitionState->lastPlayerScanTime = curTime;

                            iter = entGetIteratorSingleType(pPartitionState->iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
                            while ( ( playerEnt = EntityIteratorGetNext(iter) ) )
                            {
                                if ( guild_IsMember(playerEnt) )
                                {
                                    SetGroupProjectReference(pPartitionState, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, guild_GetGuildID(playerEnt));
                                    handleActive = true;
                                    break;
                                }
                            }
                            EntityIteratorRelease(iter);
                        }
                    }
                    break;
                }

            }
            else
            {
                if ( !s_badMapErrorSent )
                {
                    Errorf("Attempted to get group project container for map that is not guild owned or queued.");
                    s_badMapErrorSent = true;
                }
            }
        }

    }

    if ( handleActive )
    {
        GroupProjectContainer *projectContainer = GET_REF(pPartitionState->guildProjectContainerRef);

        switch ( pPartitionState->subState )
        {
        default:
        case MapContainerSubState_None:
            ErrorDetailsf("state=%d:", pPartitionState->subState);
            Errorf("Group project container sub state for map invalid");
            break;
        case MapContainerSubState_InitialWait:
            if ( projectContainer != NULL )
            {
                gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_ContainerRefValid);
            }
            else
            {
				if(gbUsePresentButNullCheck)
				{
					if( REF_IS_REFERENT_SET_BY_SOURCE_FROM_HANDLE(pPartitionState->guildProjectContainerRef) )
					{
						gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_NoContainer);
					}
				}
				else
				{
					if ( ( pPartitionState->referenceSetTime + gGroupProjectMapSubInitialWait ) < timeSecondsSince2000() )
					{
						// First timeout hit.  Time to ask if the container exists.
						TransactionReturnVal *returnVal = objCreateManagedReturnVal(CheckContainerExistsCB, (void *)(intptr_t)partition_IDFromIdx(pPartitionState->iPartitionIdx));
						RemoteCommand_DBCheckSingleContainerExists(returnVal, GLOBALTYPE_OBJECTDB, 0, pPartitionState->containerType, pPartitionState->containerID);
						gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_SentContainerExistsQuery);
					}
				}
            }
            break;
        case MapContainerSubState_SentContainerExistsQuery:
            if ( projectContainer != NULL )
            {
                // If the container arrives just go to the final state.
                gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_ContainerRefValid);
            }
            break;
        case MapContainerSubState_FinalWait:
            if ( projectContainer != NULL )
            {
                // If the container arrives just go to the final state.
                gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_ContainerRefValid);
            }
            else if ( !pPartitionState->containerLateAlertSent && ( (pPartitionState->referenceSetTime + gGroupProjectMapSubAlertTime) < timeSecondsSince2000() ) )
            {
                // If the container takes too long, send an alert.
                pPartitionState->containerLateAlertSent = true;
                TriggerAlert("GROUPPROJECT_NOCONTAINERFORMAP", 
                    STACK_SPRINTF("GroupProject container for map has not arrived after %d seconds.", gGroupProjectMapSubAlertTime), 
                    ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0);

                Errorf("GroupProject container for map has not arrived after %d seconds.", gGroupProjectMapSubAlertTime);
            }
            break;
        case MapContainerSubState_NoContainer:
            if ( projectContainer != NULL )
            {
                // If the container arrives just go to the final state.
                gslGroupProject_SetSubState(pPartitionState, MapContainerSubState_ContainerRefValid);
            }
            // Nothing to do since this is a final state.
            break;
        case MapContainerSubState_ContainerRefValid:
            devassert(projectContainer != NULL);
            // Nothing to do since this is a final state.
            break;
        }
    }
}

void
gslGroupProject_PerFrame(void)
{
    int i;
    for ( i = eaSize(&s_MapPartitionState) - 1; i >= 0; i-- )
    {
        GroupProjectMapPartitionState *pPartitionState = s_MapPartitionState[i];

        // Process any partitions that have a group project.
        if ( pPartitionState && ( pPartitionState->projectType != GroupProjectType_None ) )
        {
            GroupProjectMapTick(pPartitionState);
        }
    }
}

bool
gslGroupProject_GroupProjectMapDataReady(GroupProjectType projectType, int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    if ( pPartitionState == NULL )
    {
        return false;
    }

    if ( pPartitionState->projectType == GroupProjectType_None )
    {
        pPartitionState->projectType = projectType;
    }

    return ((pPartitionState->subState == MapContainerSubState_NoContainer) || (pPartitionState->subState == MapContainerSubState_ContainerRefValid));
}

static GroupProjectContainer *
GetGroupProjectContainerForMap(GroupProjectType projectType, int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    if ( pPartitionState == NULL )
    {
        return NULL;
    }

    if ( pPartitionState->projectType == GroupProjectType_None )
    {
        pPartitionState->projectType = projectType;
    }

    if ( projectType == GroupProjectType_Guild ) 
    {
        // If the guild project container handle is active, we know the guild.
        if ( IS_HANDLE_ACTIVE(pPartitionState->guildProjectContainerRef) )
        {
            GroupProjectContainer *projectContainer = GET_REF(pPartitionState->guildProjectContainerRef);
            return projectContainer;
        }
    }

    return NULL;
}

GroupProjectState *
gslGroupProject_GetGroupProjectStateForMap(GroupProjectType projectType, const char *projectName, int iPartitionIdx)
{
    GroupProjectContainer *projectContainer = GetGroupProjectContainerForMap(projectType, iPartitionIdx);

    if ( projectContainer != NULL )
    {
        GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState != NULL )
        {
            return projectState;
        }
    }

    return NULL;
}

const char *
gslGroupProject_GetGuildAllegianceForGroupProjectMap(int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    Guild *guild;

    if ( pPartitionState == NULL )
    {
        return NULL;
    }

    if ( pPartitionState->overrideAllegiance != NULL )
    {
        return pPartitionState->overrideAllegiance;
    }

    if ( pPartitionState->projectType == GroupProjectType_None )
    {
        pPartitionState->projectType = GroupProjectType_Guild;
    }

    if ( pPartitionState->allegianceName == NULL )
    {
        guild = GET_REF(pPartitionState->guild);
        if ( guild != NULL )
        {
            pPartitionState->allegianceName = guild->pcAllegiance;
        }
    }
    return pPartitionState->allegianceName;
}


// Gets the value of a GroupProjectNumeric associated with the current map.
// Returns false if the numeric specified is invalid, and true otherwise.
bool
gslGroupProject_GetGroupProjectNumericValueFromMap(int iPartitionIdx, GroupProjectType projectType, const char *projectName, const char *numericName, S32 *pRet, char **errString)
{
    GroupProjectState *projectState;
    GroupProjectNumericData *numericData;

    // Find the project state.
    projectState = gslGroupProject_GetGroupProjectStateForMap(projectType, projectName, iPartitionIdx);
    if ( projectState == NULL )
    {
        // If the project state can't be found, then check and see if the named project even exists.
        if ( RefSystem_ReferentFromString(g_GroupProjectDict, projectName) == NULL )
        {
            if ( errString )
            {
                estrPrintf(errString, "GroupProjectDef %s does not exist", projectName);
            }
            return false;
        }
        // If the project state can't be found, assume that the container doesn't exist or the subscription has not arrived yet and return 0.  We might
        //  need to do more here to differentiate between the subscription not having arrived yet and the project name being wrong.
        *pRet = 0;
        return true;
    }

    // Find the numeric.
    numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);
    if ( numericData == NULL )
    {
        GroupProjectDef *projectDef = GET_REF(projectState->projectDef);
        if ( projectDef )
        {
            if ( eaIndexedGetUsingString(&projectDef->validNumerics, numericName) )
            {
                // The numeric is valid, so return 0 which is the default value.
                *pRet = 0;
                return true;
            }
        }
        *pRet = 0;
        estrPrintf(errString, "GroupProjectNumericDef %s does not exist", numericName);
        return false;
    }

    // Return the value.
    *pRet = numericData->numericVal;
    return true;
}

S32
gslGroupProject_GetGroupProjectNumericFromPlayer(Entity *playerEnt, GroupProjectType projectType, const char *projectName, const char *numericName)
{
    GroupProjectContainer *projectContainer;

    projectContainer = GroupProject_ResolveContainer(playerEnt, projectType);

    if ( projectContainer )
    {
        GroupProjectState *projectState = eaIndexedGetUsingString(&projectContainer->projectList, projectName);
        if ( projectState )
        {
            GroupProjectNumericData *numericData = eaIndexedGetUsingString(&projectState->numericData, numericName);
            if ( numericData )
            {
                return numericData->numericVal;
            }
        }
    }

    return 0;
}

static GroupProjectMapPartitionState *
gslGroupProject_CreatePartitionState(int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState = StructCreate(parse_GroupProjectMapPartitionState);
    if ( pPartitionState != NULL )
    {
        pPartitionState->iPartitionIdx = iPartitionIdx;
    }

    return pPartitionState;
}

void
gslGroupProject_PartitionLoad(int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState = eaGet(&s_MapPartitionState, iPartitionIdx);

	PERFINFO_AUTO_START_FUNC();

    // Create state if it doesn't exist
    if ( pPartitionState == NULL )
    {
        pPartitionState = gslGroupProject_CreatePartitionState(iPartitionIdx);
        eaSet(&s_MapPartitionState, pPartitionState, iPartitionIdx);
    }

	PERFINFO_AUTO_STOP();
}

void
gslGroupProject_PartitionUnload(int iPartitionIdx)
{
    GroupProjectMapPartitionState *pPartitionState = gslGroupProject_GetGroupProjectMapPartitionState(iPartitionIdx);

    // should always be called with an existing partition
    devassert(pPartitionState != NULL);

    if ( pPartitionState != NULL )
    {
        StructDestroy(parse_GroupProjectMapPartitionState, pPartitionState);
        eaSet(&s_MapPartitionState, NULL, iPartitionIdx);
    }
    else
    {
        ErrorDetailsf("partition index = %d", iPartitionIdx);
        Errorf("gslGroupProject: Unloading a partition that doesn't exist");
    }
}

static void groupProjectGuildDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, GroupProjectContainer* proj, void *userData)
{
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER && proj)
	{
		gslGatewayServer_ContainerSubscriptionUpdate(eType, GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, proj->containerID);
	}
}

static void groupProjectPlayerDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, GroupProjectContainer* proj, void *userData)
{
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER && proj)
	{
		gslGatewayServer_ContainerSubscriptionUpdate(eType, GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, proj->containerID);
	}
}

void
gslGroupProject_SchemaInit(void)
{
    // Set up schema for GroupProject containers.
    objRegisterNativeSchema(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, parse_GroupProjectContainer, NULL, NULL, NULL, NULL, NULL);
    objRegisterNativeSchema(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, parse_GroupProjectContainer, NULL, NULL, NULL, NULL, NULL);

    RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), false, parse_GroupProjectContainer, false, false, NULL);
    resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD));
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), groupProjectGuildDictionaryChangeCB, NULL);

    RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER), false, parse_GroupProjectContainer, false, false, NULL);
    resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER));
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER), groupProjectPlayerDictionaryChangeCB, NULL);
}

#include "AutoGen/gslGroupProject_h_ast.c"