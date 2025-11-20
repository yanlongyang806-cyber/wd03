#pragma once

#include "GlobalTypeEnum.h"
#include "StashTable.h"

typedef struct GlobalChatLinkStruct GlobalChatLinkStruct;
typedef struct ChatGuild ChatGuild;
typedef struct ChatUser ChatUser;

AUTO_STRUCT;
typedef struct ShardMergerInfo
{
	U32 id;
	const char *shardName; AST(POOL_STRING)
	
	// Name of shard that is being merged to; empty string or NULL indicates shard deletion
	const char *destShardMergerName; AST(POOL_STRING)
} ShardMergerInfo;

AUTO_STRUCT;
typedef struct ShardMergerInfoList
{
	U32 shardMergeVersion; // also is the last ShardMergerInfo.id value assigned
	EARRAY_OF(ShardMergerInfo) eaShardMergeInfo;
} ShardMergerInfoList;

AUTO_STRUCT;
typedef struct ShardGuild
{
	ContainerID shardID;
	ContainerID guildID;
} ShardGuild;

// Persisted Cluster and Shard Info
AST_PREFIX(PERSIST)

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildInfo
{
	const ContainerID id; AST(KEY)
	CONST_STRING_POOLED name; AST(POOL_STRING)
} GuildInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatShard
{
	const ContainerID id; AST(KEY)
	const ContainerID clusterID;

	CONST_STRING_POOLED name; AST(POOL_STRING)
	CONST_STRING_POOLED product; AST(POOL_STRING)
	CONST_STRING_POOLED category; AST(POOL_STRING)

	CONST_EARRAY_OF(GuildInfo) guildList;
	// Reserved names, persisted to handle unexpected disconnects
	CONST_STRING_EARRAY guildsReserved; AST(ESTRING)

	U32 linkStructID; AST_NOT(PERSIST)
} ChatShard;

AUTO_STRUCT AST_CONTAINER;
typedef struct ChatCluster
{
	const ContainerID id; AST(KEY)
	CONST_STRING_POOLED name; AST(POOL_STRING)
	CONST_CONTAINERID_EARRAY shardIDs;

	// Non-persisted stash of guild name to ShardGuild
	StashTable guildNameStash; AST_NOT(PERSIST) // key = Name (Pooled String), value = ShardGuild struct
	StashTable guildNameReserved; AST_NOT(PERSIST) // key = Name (Deep Copy), value = ShardGuild struct (guild ID = 0)
} ChatCluster;
AST_PREFIX();

void ChatShard_UpdateUserShards(SA_PARAM_NN_VALID ChatUser *user);
void ChatCluster_InitContainers(void);
ChatShard *findShardByID(ContainerID id);
ChatCluster *findClusterByID(ContainerID id);

void processShardRegisterQueue(void);
void registerShard(GlobalChatLinkStruct *linkStruct);

void ChatCluster_InitGuildStash(void);

void ChatShard_UpdateGuildName(SA_PARAM_NN_VALID ChatShard *shard, int guildID, const char *guildName);
bool ChatShard_ReserveGuildName(SA_PARAM_NN_VALID ChatShard *shard, const char *guildName);
void ChatShard_ReleaseReservedGuildName(SA_PARAM_NN_VALID ChatShard *shard, const char *guildName);
void ChatShard_RemoveGuild(SA_PARAM_NN_VALID ChatShard *shard, U32 uGuildID);
void ChatShard_SyncGuildList(SA_PARAM_NN_VALID ChatShard *shard, EARRAY_OF(ChatGuild) eaGuilds);