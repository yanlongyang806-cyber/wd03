/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Login2Common.h"
#include "Login2ServerCommon.h"
#include "utilitiesLib.h"
#include "objTransactions.h"
#include "StringCache.h"
#include "ShardCluster.h"
#include "earray.h"

#include "AutoGen/Login2Common_h_ast.h"
#include "AutoGen/Login2ServerCommon_h_ast.h"
#include "AutoGen/ShardCluster_h_ast.h"

static const char *s_pooledShardName = NULL;

const char *
Login2_GetPooledShardName(void)
{
    if ( s_pooledShardName == NULL )
    {
        s_pooledShardName = allocAddString(GetShardNameFromShardInfoString());
    }

    return s_pooledShardName;
}

// Generate a unique request token for matching responses from inter-shard remote commands to their local state.
// Use the server ID to ensure uniqueness across process instances.
static U32 s_nextRequestTokenLow = 1;
U64
Login2_GenerateRequestToken(void)
{
    U32 tokenLow = s_nextRequestTokenLow++;

    return ( ((U64)objServerID()) << 24 ) | ( tokenLow & 0xffffff );
}

// Generate convert to a short token for places where we only have 32 bits.
U32
Login2_ShortenToken(U64 token)
{
    return ((U32)(token & 0xffffffffLL));
}

// Lengthen a short token so that we can use it for looking up stuff.
U64
Login2_LengthenShortToken(U32 shortToken)
{
    U64 tmpToken = (((U64)objServerID()) << 24);

    if ( ( shortToken & 0xff000000 ) == ( tmpToken & 0xff000000 ) )
    {
        // Top part of the short token matches bottom bits of the server ID, so we assume this is a good token.
        return tmpToken | shortToken;
    }

    // Token does not appear to be a match.
    return 0;
}

// Fill in a destination struct for the current shard and server.
void
Login2_FillDestinationStruct(Login2InterShardDestination *destination, U64 requestToken)
{
    destination->requestToken = requestToken;
    destination->shardName = Login2_GetPooledShardName();
    destination->serverType = objServerType();
    destination->serverID = objServerID();

    return;
}

Cluster_Overview *
Login2_GetShardClusterOverview(void)
{
    Cluster_Overview *clusterOverview = GetShardClusterOverview_EvenIfNotInCluster();
    
    return clusterOverview;
}


//
// Maintain an array of the names of all the shards in the cluster.
//
static STRING_EARRAY s_allShardNamesInCluster = NULL;
static STRING_EARRAY s_connectedShardNamesInCluster = NULL;

static bool s_shardNamesDirty = true;

void OVERRIDE_LATELINK_ShardClusterOverviewChanged(void)
{
    s_shardNamesDirty = true;
}

static void
RefreshShardNameArrays(void)
{
    Cluster_Overview *clusterOverview = Login2_GetShardClusterOverview();
    int i;

    eaClearFast(&s_allShardNamesInCluster);
    eaClearFast(&s_connectedShardNamesInCluster);

    for ( i = eaSize(&clusterOverview->ppShards) - 1; i >= 0; i-- )
    {
        ClusterShardSummary *shardSummary = clusterOverview->ppShards[i];
        const char *shardName = allocAddString(shardSummary->pShardName);
        eaPush(&s_allShardNamesInCluster, (char *)shardName);
        if ( shardSummary->eState == CLUSTERSHARDSTATE_CONNECTED || shardSummary->eState == CLUSTERSHARDSTATE_THATS_ME )
        {
            eaPush(&s_connectedShardNamesInCluster, (char *)shardName);
        }
    }

    s_shardNamesDirty = false;
}

char **
Login2_GetShardNamesInCluster(void)
{
    if ( s_shardNamesDirty )
    {
        RefreshShardNameArrays();
    }

    return s_allShardNamesInCluster;
}

char **
Login2_GetConnectedShardNamesInCluster(void)
{
    if ( s_shardNamesDirty )
    {
        RefreshShardNameArrays();
    }

    return s_connectedShardNamesInCluster;
}

#include "AutoGen/Login2ServerCommon_h_ast.c"