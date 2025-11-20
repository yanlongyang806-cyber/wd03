#include "chatShardCluster.h"

#include "chatCommonStructs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "ResourceInfo.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatShardCluster_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

#define GUILD_NAMESPACE_STASH_SIZE (100)

static StashTable stClusterByName = NULL;
static StashTable stShardByName = NULL;

#define SHARDS_MERGED_FILE "server/chatShardMerged.txt"
static ShardMergerInfoList sShardMergerList = {0};

AUTO_TRANSACTION ATR_LOCKS(user, ".uShardMergerVersion, .eaCharacterShards");
enumTransactionOutcome trUpdateUserShards(ATR_ARGS, NOCONST(ChatUser) *user)
{
	EARRAY_CONST_FOREACH_BEGIN(sShardMergerList.eaShardMergeInfo, i, s);
	{
		ShardMergerInfo *info = sShardMergerList.eaShardMergeInfo[i];
		if (info->id > user->uShardMergerVersion)
		{
			int idx = eaFind(&user->eaCharacterShards, info->shardName);
			if (idx != -1)
			{
				if (info->destShardMergerName)
					user->eaCharacterShards[idx] = (char*) info->destShardMergerName;
				else
					eaRemove(&user->eaCharacterShards, idx);
			}
		}
	}
	EARRAY_FOREACH_END;
	user->uShardMergerVersion = sShardMergerList.shardMergeVersion;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ChatShard_UpdateUserShards(SA_PARAM_NN_VALID ChatUser *user)
{
	if (user->uShardMergerVersion >= sShardMergerList.shardMergeVersion)
		return;
	AutoTrans_trUpdateUserShards(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, user->id);
}

static void shardsMergedReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading Shards Merged...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	StructReset(parse_ShardMergerInfoList, &sShardMergerList);
	ParserReadTextFile(SHARDS_MERGED_FILE, parse_ShardMergerInfoList, &sShardMergerList, PARSER_OPTIONALFLAG);
	loadend_printf("done");
}

// Must be called before DB load
void ChatCluster_InitContainers(void)
{
	objRegisterNativeSchema(GLOBALTYPE_CHATSHARD, parse_ChatShard, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_CHATCLUSTER, parse_ChatCluster, NULL, NULL, NULL, NULL, NULL);

	stClusterByName = stashTableCreateWithStringKeys(3, StashDefault);
	stShardByName = stashTableCreateWithStringKeys(10, StashDefault);

	// Read in Shard Merge data
	if(!ParserReadTextFile(SHARDS_MERGED_FILE, parse_ShardMergerInfoList, &sShardMergerList, PARSER_OPTIONALFLAG));
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, SHARDS_MERGED_FILE, shardsMergedReloadCallback);
}

ChatShard *findShardByID(ContainerID id)
{
	return objGetContainerData(GLOBALTYPE_CHATSHARD, id);
}

ChatShard *findShardByName(const char *name)
{
	ChatShard *shard = NULL;
	PERFINFO_AUTO_START_FUNC();
	if (devassert(stShardByName))
		stashFindPointer(stShardByName, name, &shard);
	PERFINFO_AUTO_STOP_FUNC();
	return shard;
}

ChatCluster *findClusterByID(ContainerID id)
{
	return objGetContainerData(GLOBALTYPE_CHATCLUSTER, id);
}

ChatCluster *findClusterByName(const char *name)
{
	ChatCluster *cluster;
	PERFINFO_AUTO_START_FUNC();
	if (devassert(stClusterByName))
		stashFindPointer(stClusterByName, name, &cluster);
	PERFINFO_AUTO_STOP_FUNC();
	return cluster;
}

AUTO_TRANSACTION ATR_LOCKS(cluster, ".id, .shardIDs") ATR_LOCKS(shard, ".id, .clusterID");
enumTransactionOutcome trClusterAddShard(ATR_ARGS, NOCONST(ChatCluster) *cluster, NOCONST(ChatShard) *shard)
{
	eaiPushUnique(&cluster->shardIDs, shard->id);
	shard->clusterID = cluster->id;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(cluster, ".shardIDs") ATR_LOCKS(shard, ".id, .clusterID");
enumTransactionOutcome trClusterRemoveShard(ATR_ARGS, NOCONST(ChatCluster) *cluster, NOCONST(ChatShard) *shard)
{
	eaiFindAndRemove(&cluster->shardIDs, shard->id);
	shard->clusterID = 0;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(toRemove, ".shardIDs") ATR_LOCKS(toAdd, ".id, .shardIDs") ATR_LOCKS(shard, ".id, .clusterID");
enumTransactionOutcome trClusterAddAndRemoveShard(ATR_ARGS, NOCONST(ChatCluster) *toRemove, NOCONST(ChatCluster) *toAdd, NOCONST(ChatShard) *shard)
{
	eaiFindAndRemove(&toRemove->shardIDs, shard->id);
	eaiPushUnique(&toAdd->shardIDs, shard->id);
	shard->clusterID = toAdd->id;
	return TRANSACTION_OUTCOME_SUCCESS;
}

static STRING_EARRAY seaClustersBeingCreated = NULL;
static STRING_EARRAY seaShardsBeingCreated = NULL;

typedef struct ShardClusterContainerCreation
{
	U32 uID;
	bool bAwaitingShardCreation;
	bool bAwaitingClusterCreation;
} ShardClusterContainerCreation;
static EARRAY_OF(ShardClusterContainerCreation) seaShardsAwaitingContainers = NULL;

static void trAddChatShard_CB(TransactionReturnVal *returnVal, char *shardName)
{
	int idx;
	idx = eaFind(&seaShardsBeingCreated, shardName);
	if (devassert(idx != -1))
		eaRemove(&seaShardsBeingCreated, idx);

	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatShard *shard;
		EARRAY_CONST_FOREACH_REVERSE_BEGIN(seaShardsAwaitingContainers, i , s);
		{
			ShardClusterContainerCreation *createData = seaShardsAwaitingContainers[i];
			GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(createData->uID);
			if (linkStruct && stricmp(linkStruct->pShardName, shardName) == 0)
				createData->bAwaitingShardCreation = false;
		}
		EARRAY_FOREACH_END;

		shard = findShardByID(uID);
		if (shard)
			stashAddPointer(stShardByName, shard->name, shard, false);
	}
	free(shardName);
}

static void trAddChatCluster_CB(TransactionReturnVal *returnVal, char *clusterName)
{
	int idx;
	idx = eaFind(&seaClustersBeingCreated, clusterName);
	if (devassert(idx != -1))
		eaRemove(&seaClustersBeingCreated, idx);

	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = atoi(returnVal->pBaseReturnVals->returnString);
		ChatCluster *cluster;
		EARRAY_CONST_FOREACH_REVERSE_BEGIN(seaShardsAwaitingContainers, i , s);
		{
			ShardClusterContainerCreation *createData = seaShardsAwaitingContainers[i];
			GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(createData->uID);
			if (linkStruct && stricmp_safe(linkStruct->pClusterName, clusterName) == 0)
				createData->bAwaitingClusterCreation = false;
		}
		EARRAY_FOREACH_END;

		cluster = findClusterByID(uID);
		if (cluster)
			stashAddPointer(stClusterByName, cluster->name, cluster, false);
	}
	free(clusterName);
}

void processShardRegisterQueue(void)
{
	EARRAY_CONST_FOREACH_REVERSE_BEGIN(seaShardsAwaitingContainers, i , s);
	{
		ShardClusterContainerCreation *createData = seaShardsAwaitingContainers[i];
		GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(createData->uID);
		if (linkStruct && !createData->bAwaitingShardCreation && !createData->bAwaitingClusterCreation)
		{
			registerShard(linkStruct);
			eaRemove(&seaShardsAwaitingContainers, i);
			free(createData);
		}
	}
	EARRAY_FOREACH_END;
}

void registerShard(GlobalChatLinkStruct *linkStruct)
{
	ChatShard *shard = NULL;
	ChatCluster *cluster = NULL;
	ShardClusterContainerCreation *creationData = NULL;
	
	if (!devassert(!nullStr(linkStruct->pShardName)))
		return;

	// Create ChatShard container if necessary
	shard = findShardByName(linkStruct->pShardName);
	if (!shard)
	{
		creationData = calloc(1, sizeof(ShardClusterContainerCreation));
		creationData->uID = linkStruct->uID;
		creationData->bAwaitingShardCreation = true;

		eaPush(&seaShardsAwaitingContainers, creationData);
		if (eaFindString(&seaShardsBeingCreated, linkStruct->pShardName) == -1)
		{
			char *nameCopy = strdup(linkStruct->pShardName);
			NOCONST(ChatShard) *newShard = StructCreateNoConst(parse_ChatShard);
			newShard->name = allocAddString(linkStruct->pShardName);
			newShard->product = allocAddString(linkStruct->pProductName);
			newShard->category = allocAddString(linkStruct->pShardCategoryName);

			eaPush(&seaShardsBeingCreated, nameCopy);
			objRequestContainerCreateLocal(objCreateManagedReturnVal(trAddChatShard_CB, nameCopy), 
				GLOBALTYPE_CHATSHARD, newShard);
		}
	}

	// Create ChatCluster container if necessary
	if (!nullStr(linkStruct->pClusterName))
	{
		cluster = findClusterByName(linkStruct->pClusterName);
		if (!cluster)
		{
			if (!creationData)
			{
				creationData = calloc(1, sizeof(ShardClusterContainerCreation));
				creationData->uID = linkStruct->uID;
				eaPush(&seaShardsAwaitingContainers, creationData);
			}
			creationData->bAwaitingClusterCreation = true;
			if (eaFindString(&seaClustersBeingCreated, linkStruct->pClusterName) == -1)
			{
				char *nameCopy = strdup(linkStruct->pClusterName);
				NOCONST(ChatCluster) *newCluster = StructCreateNoConst(parse_ChatCluster);
				newCluster->name = allocAddString(linkStruct->pClusterName);
				eaPush(&seaClustersBeingCreated, nameCopy);
				objRequestContainerCreateLocal(objCreateManagedReturnVal(trAddChatCluster_CB, nameCopy), GLOBALTYPE_CHATCLUSTER, newCluster);
			}
		}
	}
	if (creationData)
		return;

	if (stricmp(shard->category, linkStruct->pShardCategoryName) != 0)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATSHARD, shard->id, "shardSetCategory", "set category = \"%s\"", linkStruct->pShardCategoryName);
	if (stricmp(shard->product, linkStruct->pProductName) != 0)
		objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATSHARD, shard->id, "shardSetProduct", "set product = \"%s\"", linkStruct->pProductName);
	if (!cluster && shard->clusterID != 0)
	{
		cluster = findClusterByID(shard->clusterID);
		if (cluster)
			AutoTrans_trClusterRemoveShard(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCLUSTER, shard->clusterID, GLOBALTYPE_CHATSHARD, shard->id);
		else
			objRequestTransactionSimplef(NULL, GLOBALTYPE_CHATSHARD, shard->id, "shardClearCluster", "set clusterID = 0");
	}
	else if (cluster && cluster->id != shard->clusterID)
	{
		ChatCluster *prevCluster = shard->clusterID ? findClusterByID(shard->clusterID) : NULL;

		if (prevCluster)
			AutoTrans_trClusterAddAndRemoveShard(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCLUSTER, prevCluster->id,
				GLOBALTYPE_CHATCLUSTER, cluster->id, GLOBALTYPE_CHATSHARD, shard->id);
		else
			AutoTrans_trClusterAddShard(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCLUSTER, cluster->id, GLOBALTYPE_CHATSHARD, shard->id);
	}

	shard->linkStructID = linkStruct->uID;
	linkStruct->uShardID = shard->id;
	// If guilds have already been received, force a sync
	if (linkStruct->shardGuildStash && stashGetCount(linkStruct->shardGuildStash))
	{
		EARRAY_OF(ChatGuild) ppGuilds = NULL;
		eaSetCapacity(&ppGuilds, stashGetCount(linkStruct->shardGuildStash));
		FOR_EACH_IN_STASHTABLE(linkStruct->shardGuildStash, ChatGuild, guild);
		{
			eaPush(&ppGuilds, guild);
		}
		FOR_EACH_END;
		ChatShard_SyncGuildList(shard, ppGuilds);
		eaDestroy(&ppGuilds);
	}
	if (cluster)
		linkStruct->uClusterID = cluster->id;
}

///////////////////////////////////////////////////////
// Cluster and Shard Guilds

static void clusterStashInit(ChatCluster *cluster)
{
	char clusterName[128];
	cluster->guildNameStash = stashTableCreateWithStringKeys(GUILD_NAMESPACE_STASH_SIZE, StashDefault);
	cluster->guildNameReserved = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);

	sprintf(clusterName, "Cluster_%s", cluster->name);
	resRegisterDictionaryForStashTable(clusterName, RESCATEGORY_OTHER, 0, cluster->guildNameStash, parse_ShardGuild);
	sprintf(clusterName, "Cluster_%s_rsv", cluster->name);
	resRegisterDictionaryForStashTable(clusterName, RESCATEGORY_OTHER, 0, cluster->guildNameReserved, parse_ShardGuild);
}

ShardGuild *clusterFindGuild (ChatCluster *cluster, const char *guildName)
{
	ShardGuild *guild;
	if (!cluster->guildNameStash)
		return NULL;
	if (stashFindPointer(cluster->guildNameStash, guildName, &guild))
		return guild;
	return NULL;
}

static void clusterAddGuild(ChatCluster *cluster, ContainerID shardID, ContainerID guildID, const char *guildName)
{
	ShardGuild *guild = StructCreate(parse_ShardGuild);
	guild->shardID = shardID;
	guild->guildID = guildID;

	if (!cluster->guildNameStash)
		clusterStashInit(cluster);
	if (!devassert(stashAddPointer(cluster->guildNameStash, guildName, guild, false)))
	{
		AssertOrAlert("GLOBALCHATSERVER.GUILDNAME_RESERVED_CONFLICT", "Failed to add guild %s[%d] for cluster '%s'", guildName, guildID, cluster->name);
		StructDestroy(parse_ShardGuild, guild);
	}
}

static ShardGuild *clusterRemoveGuild(ChatCluster *cluster, const char *guildName)
{
	ShardGuild *guild;
	if (!cluster->guildNameStash)
		return NULL;
	if (stashRemovePointer(cluster->guildNameStash, guildName, &guild))
		return guild;
	return NULL;
}

static void clusterAddReservedName(ChatCluster *cluster, ContainerID shardID, const char *guildName)
{
	ShardGuild *guild = StructCreate(parse_ShardGuild);
	guild->shardID = shardID;
	guild->guildID = 0;

	if (!cluster->guildNameReserved)
		clusterStashInit(cluster);
	if (!devassert(stashAddPointer(cluster->guildNameReserved, guildName, guild, false)))
	{
		AssertOrAlert("GLOBALCHATSERVER.GUILDNAME_RESERVED_CONFLICT", "Failed to reserve guild name '%s' for cluster '%s'", guildName, cluster->name);
		StructDestroy(parse_ShardGuild, guild);
	}
}

static ShardGuild *clusterRemoveReservedName(ChatCluster *cluster, const char *guildName)
{
	ShardGuild *guild;
	if (!cluster->guildNameReserved)
		return NULL;
	if (stashRemovePointer(cluster->guildNameReserved, guildName, &guild))
		return guild;
	return NULL;
}

AUTO_TRANSACTION ATR_LOCKS(shard, ".guildList[AO], .guildList[], .guildsReserved");
enumTransactionOutcome trShardUpdateGuild(ATR_ARGS, NOCONST(ChatShard) *shard, int guildID, const char *guildName)
{
	NOCONST(GuildInfo) *info = eaIndexedGetUsingInt(&shard->guildList, guildID);
	int idx;

	if (!guildID || nullStr(guildName))
		return TRANSACTION_OUTCOME_FAILURE;
	idx = eaFindString(&shard->guildsReserved, guildName);
	if (idx != -1)
	{
		estrDestroy(&shard->guildsReserved[idx]);
		eaRemove(&shard->guildsReserved, idx);
	}
	if (!info)
	{
		info = StructCreateNoConst(parse_GuildInfo);
		info->id = guildID;
		eaIndexedAdd(&shard->guildList, info);
	}
	info->name = allocAddString(guildName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(shard, ".guildList[]");
enumTransactionOutcome trShardRemoveGuild(ATR_ARGS, NOCONST(ChatShard) *shard, int guildID)
{
	NOCONST(GuildInfo) *info = eaIndexedRemoveUsingInt(&shard->guildList, guildID);
	if (info)
		StructDestroyNoConst(parse_GuildInfo, info);
	else
		return TRANSACTION_OUTCOME_FAILURE;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(shard, ".guildsReserved");
enumTransactionOutcome trShardReserveGuildName(ATR_ARGS, NOCONST(ChatShard) *shard, const char *guildName)
{
	if (eaFindString(&shard->guildsReserved, guildName) != -1)
		return TRANSACTION_OUTCOME_FAILURE;
	eaPush(&shard->guildsReserved, estrDup(guildName));
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(shard, ".guildsReserved");
enumTransactionOutcome trShardUnreserveGuildName(ATR_ARGS, NOCONST(ChatShard) *shard, const char *guildName)
{
	int idx = eaFindString(&shard->guildsReserved, guildName);
	if (idx != -1)
	{
		estrDestroy(&shard->guildsReserved[idx]);
		eaRemove(&shard->guildsReserved, idx);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

void ChatShard_UpdateGuildName(ChatShard *shard, int guildID, const char *guildName)
{
	GuildInfo *curInfo = eaIndexedGetUsingInt(&shard->guildList, guildID);
	if (shard->clusterID && (!curInfo || stricmp(curInfo->name, guildName) != 0))
	{
		ChatCluster *cluster = findClusterByID(shard->clusterID);
		if (devassert(cluster))
		{
			ShardGuild *guild;
			if (curInfo)
			{
				ShardGuild *cache = clusterRemoveGuild(cluster, curInfo->name);
				if (cache)
					StructDestroy(parse_ShardGuild, cache);
			}
			// Should always be reserved under the same shard ID
			if (stashFindPointer(cluster->guildNameReserved, guildName, &guild))
			{
				if (devassert(guild->shardID == shard->id))
				{
					clusterRemoveReservedName(cluster, guildName);
					StructDestroy(parse_ShardGuild, guild);
					clusterAddGuild(cluster, shard->id, guildID, allocAddString(guildName));
				}
			}
		}
	}
	AutoTrans_trShardUpdateGuild(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATSHARD, shard->id, guildID, guildName);
}

bool ChatShard_ReserveGuildName(ChatShard *shard, const char *guildName)
{
	ChatCluster *cluster;
	ShardGuild *guild;

	if (!shard->clusterID)
		return true;
	cluster = findClusterByID(shard->clusterID);
	assert(cluster);
	guild = clusterFindGuild(cluster, guildName);
	if (guild)
		return false;
	if (!cluster->guildNameReserved)
		clusterStashInit(cluster);
	if (stashFindInt(cluster->guildNameReserved, guildName, NULL))
		return false;
	clusterAddReservedName(cluster, shard->id, guildName);
	AutoTrans_trShardReserveGuildName(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATSHARD, shard->id, guildName);
	return true;
}

void ChatShard_ReleaseReservedGuildName(ChatShard *shard, const char *guildName)
{
	ChatCluster *cluster;
	if (!shard->clusterID)
		return;
	cluster = findClusterByID(shard->clusterID);
	assert(cluster);
	clusterRemoveReservedName(cluster, guildName);
	AutoTrans_trShardUnreserveGuildName(NULL, GLOBALTYPE_CHATSERVER, GLOBALTYPE_CHATSHARD, shard->id, guildName);
}

void ChatShard_RemoveGuild(ChatShard *shard, U32 uGuildID)
{
	if (shard->clusterID)
	{
		ChatCluster *cluster = findClusterByID(shard->clusterID);
		if (devassert(cluster))
		{
			GuildInfo *guild = eaIndexedGetUsingInt(&shard->guildList, uGuildID);
			if (guild)
			{
				ShardGuild *cache = clusterRemoveGuild(cluster, guild->name);
				if (cache)
					StructDestroy(parse_ShardGuild, cache);
			}
		}
	}
	AutoTrans_trShardRemoveGuild(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATSHARD, shard->id, uGuildID);
}

AUTO_TRANSACTION ATR_LOCKS(shard, ".guildList, .guildsReserved");
enumTransactionOutcome trShardReplaceGuilds(ATR_ARGS, NOCONST(ChatShard) *shard, NON_CONTAINER ChatGuildList *guildList)
{
	eaDestroyEString(&shard->guildsReserved);
	eaDestroyStructNoConst(&shard->guildList, parse_GuildInfo);
	eaIndexedEnableNoConst(&shard->guildList, parse_GuildInfo);
	EARRAY_CONST_FOREACH_BEGIN(guildList->ppGuildList, i, s);
	{
		NOCONST(GuildInfo) *info = StructCreateNoConst(parse_GuildInfo);
		info->id = guildList->ppGuildList[i]->iGuildID;
		info->name = allocAddString(guildList->ppGuildList[i]->pchName);
		eaIndexedAdd(&shard->guildList, info);
	}
	EARRAY_FOREACH_END;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Called after DB Load
void ChatCluster_InitGuildStash(void)
{
	ContainerIterator iter = {0};
	ChatCluster *cluster;
	ChatShard *shard;

	objInitContainerIteratorFromType(GLOBALTYPE_CHATCLUSTER, &iter);
	while (cluster = objGetNextObjectFromIterator(&iter))
	{
		clusterStashInit(cluster);
		devassert(stashAddPointer(stClusterByName, cluster->name, cluster, false));
		EARRAY_INT_CONST_FOREACH_BEGIN(cluster->shardIDs, i, s);
		{
			shard = findShardByID(cluster->shardIDs[i]);
			if (shard)
			{
				EARRAY_CONST_FOREACH_BEGIN(shard->guildList, j, t);
				{
					clusterAddGuild(cluster, shard->id, shard->guildList[j]->id, shard->guildList[j]->name);
				}
				EARRAY_FOREACH_END;
				EARRAY_CONST_FOREACH_BEGIN(shard->guildsReserved, j, t);
				{
					clusterAddReservedName(cluster, shard->id, shard->guildsReserved[j]);
				}
				EARRAY_FOREACH_END;
			}
		}
		EARRAY_FOREACH_END;
	}
	objClearContainerIterator(&iter);

	objInitContainerIteratorFromType(GLOBALTYPE_CHATSHARD, &iter);
	while (shard = objGetNextObjectFromIterator(&iter))
	{
		devassert(stashAddPointer(stShardByName, shard->name, shard, false));
	}
	objClearContainerIterator(&iter);
}

void ChatShard_SyncGuildList(ChatShard *shard, EARRAY_OF(ChatGuild) eaGuilds)
{
	ChatGuildList guildList = {0};
	ChatCluster *cluster;
	
	eaIndexedEnable(&guildList.ppGuildList, parse_ChatGuild);
	EARRAY_CONST_FOREACH_BEGIN(eaGuilds, i, s);
	{
		eaIndexedAdd(&guildList.ppGuildList, eaGuilds[i]);
	}
	EARRAY_FOREACH_END;

	cluster = findClusterByID(shard->clusterID);
	if (cluster)
	{
		// Clear anything that's reserved
		EARRAY_CONST_FOREACH_BEGIN(shard->guildsReserved, i, s);
		{
			clusterRemoveReservedName(cluster, shard->guildsReserved[i]);
		}
		EARRAY_FOREACH_END;

		if (eaSize(&shard->guildList))
		{
			EARRAY_OF(ChatGuild) eaToUpdate = NULL;
			INT_EARRAY eaiToRemove = NULL;

			EARRAY_CONST_FOREACH_BEGIN(shard->guildList, i, s);
			{
				ChatGuild *guild = eaIndexedGetUsingInt(&guildList.ppGuildList, shard->guildList[i]->id);
				if (!guild)
					eaiPush(&eaiToRemove, shard->guildList[i]->id);
				else
				{
					// Remove guilds from list that were still known, update if name changed
					if (stricmp(guild->pchName, shard->guildList[i]->name) != 0)
						eaPush(&eaToUpdate, guild);
					eaIndexedRemoveUsingInt(&guildList.ppGuildList, shard->guildList[i]->id);
				}
			}
			EARRAY_FOREACH_END;

			EARRAY_CONST_FOREACH_BEGIN(eaToUpdate, i, s);
			{
				ChatGuild *guild = eaToUpdate[i];
				GuildInfo *info = eaIndexedGetUsingInt(&shard->guildList, guild->iGuildID);
				ShardGuild *cache = clusterRemoveGuild(cluster, info->name);
				if (cache)
					StructDestroy(parse_ShardGuild, cache);
				clusterAddGuild(cluster, shard->id, guild->iGuildID, guild->pchName);
			}
			EARRAY_FOREACH_END;

			EARRAY_INT_CONST_FOREACH_BEGIN(eaiToRemove, i, s);
			{
				GuildInfo *info = eaIndexedGetUsingInt(&shard->guildList, eaiToRemove[i]);
				ShardGuild *cache = clusterRemoveGuild(cluster, info->name);
				if (cache)
					StructDestroy(parse_ShardGuild, cache);
			}
			EARRAY_FOREACH_END;

			eaDestroy(&eaToUpdate);
			eaiDestroy(&eaiToRemove);
		}
		// All guilds that are left are new
		EARRAY_CONST_FOREACH_BEGIN(guildList.ppGuildList, i, s);
		{
			ChatGuild *guild = guildList.ppGuildList[i];
			clusterAddGuild(cluster, shard->id, guild->iGuildID, guild->pchName);
		}
		EARRAY_FOREACH_END;
	}
	eaDestroy(&guildList.ppGuildList);
	guildList.ppGuildList = eaGuilds;
	AutoTrans_trShardReplaceGuilds(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATSHARD, shard->id, &guildList);
}

static void ShardDestroy(SA_PARAM_NN_VALID ChatShard *shard)
{
	if (shard->clusterID)
	{
		ChatCluster *cluster = findClusterByID(shard->clusterID);
		if (cluster)
		{
			EARRAY_CONST_FOREACH_BEGIN(shard->guildList, i, s);
			{
				clusterRemoveGuild(cluster, shard->guildList[i]->name);
			}
			EARRAY_FOREACH_END;
			EARRAY_CONST_FOREACH_BEGIN(shard->guildsReserved, i, s);
			{
				clusterRemoveReservedName(cluster, shard->guildsReserved[i]);
			}
			EARRAY_FOREACH_END;
			AutoTrans_trClusterRemoveShard(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATCLUSTER, shard->clusterID, GLOBALTYPE_CHATSHARD, shard->id);
		}
	}
	stashRemovePointer(stShardByName, shard->name, NULL);
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_CHATSHARD, shard->id);
}

AUTO_COMMAND ACMD_CATEGORY(ChatAdmin);
void ShardCluster_RemoveShardByID(U32 uID)
{
	ChatShard *shard = findShardByID(uID);
	if (!shard)
		return;
	ShardDestroy(shard);
}

AUTO_COMMAND ACMD_CATEGORY(ChatAdmin);
void ShardCluster_RemoveShardByName(const char *name)
{
	ChatShard *shard = findShardByName(name);
	if (!shard)
		return;
	ShardDestroy(shard);
}

// TODO(Theo) implement rename, merging?

#include "AutoGen/chatShardCluster_h_ast.c"