#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef enum GlobalType GlobalType;
typedef struct Cluster_Overview Cluster_Overview;

// This structure is passed to inter-shard remote commands to tell them where to return the results.
AUTO_STRUCT;
typedef struct Login2InterShardDestination
{
    const char *shardName;      AST(POOL_STRING)
    GlobalType serverType;
    ContainerID serverID;

    // This is an application specific value that can be used to disambiguate different responses.
    U64 requestToken;
} Login2InterShardDestination;

AUTO_STRUCT;
typedef struct Login2CharacterDetailDBReturn
{
    char *playerCharacterString;                AST(ESTRING)
    STRING_EARRAY activePuppetStrings;          AST(ESTRING)
} Login2CharacterDetailDBReturn;

// Build a destination struct for the current shard and server.
void Login2_FillDestinationStruct(Login2InterShardDestination *destination, U64 requestToken);

// Generate a unique request token for matching responses from inter-shard remote commands to their local state.
U64 Login2_GenerateRequestToken(void);

// Generate convert to a short token for places where we only have 32 bits.
U32 Login2_ShortenToken(U64 token);

// Lengthen a short token so that we can use it for looking up stuff.
U64 Login2_LengthenShortToken(U32 shortToken);

// Return the name of the current shard as a pooled string.
const char *Login2_GetPooledShardName(void);

Cluster_Overview *Login2_GetShardClusterOverview(void);

char **Login2_GetShardNamesInCluster(void);
char **Login2_GetConnectedShardNamesInCluster(void);
