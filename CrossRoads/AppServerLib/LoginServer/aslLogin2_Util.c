/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_Util.h"
#include "aslLogin2_GetCharacterChoices.h"
#include "ShardCluster.h"
#include "stdtypes.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "autogen/AppServerLib_autogen_SlowFuncs.h"

static int s_ServerIndexCounter = 0;

ContainerID
aslLogin2_GetRandomServerOfTypeInShard(const char *shardName, GlobalType serverType)
{
    ContainerID serverID = 0;
    Cluster_Overview *clusterOverview = GetShardClusterOverview_EvenIfNotInCluster();

    if ( clusterOverview )
    {
        ClusterShardSummary *shardSummary = eaIndexedGetUsingString(&clusterOverview->ppShards, shardName);
        if ( shardSummary && shardSummary->pMostRecentStatus )
        {
            int numServers;
            ClusterServerTypeStatus *serverTypeStatus = eaIndexedGetUsingInt(&shardSummary->pMostRecentStatus->ppServersByType, serverType);
            
            if ( serverTypeStatus )
            {
                int index;
                numServers = eaSize(&serverTypeStatus->ppServers);
                
                index = s_ServerIndexCounter % numServers;
                s_ServerIndexCounter++;

                return serverTypeStatus->ppServers[index]->iContainerID;
            }
        }
    }

    return 0;
}

void
GetCharacterChoicesCmdCB(Login2CharacterChoices *characterChoices, void *userData)
{
    SlowRemoteCommandID commandID = (SlowRemoteCommandID)((intptr_t)userData);
    SlowRemoteCommandReturn_aslLogin2_GetCharacterChoicesCmd(commandID, characterChoices);
}

AUTO_COMMAND_REMOTE_SLOW(Login2CharacterChoices *) ACMD_IFDEF(GAMESERVER);
void aslLogin2_GetCharacterChoicesCmd(SlowRemoteCommandID commandID, ContainerID accountID)
{
    if ( accountID )
    {
        aslLogin2_GetCharacterChoices(accountID, GetCharacterChoicesCmdCB, (void *)((intptr_t)commandID));
    }
    else
    {
        SlowRemoteCommandReturn_aslLogin2_GetCharacterChoicesCmd(commandID, NULL);
    }
}