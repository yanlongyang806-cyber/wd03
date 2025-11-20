#include "chatGuild.h"
#include "AutoGen/chatGuild_h_ast.h"

#include "chatdb.h"
#include "chatCommonStructs.h"
#include "chatCommonStructs_h_ast.h"
#include "chatGlobal.h"
#include "ChatServer/chatShared.h"
#include "chatShardCluster.h"
#include "cmdparse.h"
#include "earray.h"
#include "estring.h"
#include "msgsend.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "users.h"
#include "xmpp/XMPP_Chat.h"

#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

extern bool gbGlobalChatResponse;

ChatGuild **g_eaChatGuilds = NULL;
StashTable stGlobalChatAllGuilds = NULL;

// Remove this guild from the list of guilds a user is a member of.
static void GlobalChatGuildMembershipRemove(GlobalChatLinkStruct *pChatServer, ChatGuild *pGuild, ChatUser *pUser)
{
	GlobalChatLinkUserGuilds *guilds = NULL;
	int guildId;

	// Verify parameters.
	devassert(pChatServer && pGuild && pUser);
	if (!pChatServer || !pGuild || !pUser)
		return;
	guildId = pGuild->iGuildID;

	// Get this user's guild list.
	stashIntFindPointer(pChatServer->stUserGuilds, pUser->id, &guilds);
	
	// Remove this item from the user's guilds list.
	if (guilds)
	{
		eaiFindAndRemove(&guilds->guilds, guildId);
		eaiFindAndRemove(&guilds->officerGuilds, guildId);
	}
}

U32 GlobalChatGuildIdByName (U32 uChatServerID, const char *pchGuildName)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatServerID);
	int id = 0;
	if (linkStruct)
		stashFindInt(linkStruct->shardGuildNameStash, pchGuildName, &id);
	return id;
}

ChatGuild *GlobalChatFindGuild (U32 uChatServerID, U32 uGuildID)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatServerID);
	if (linkStruct)
	{
		ChatGuild *pGuild = NULL;
		if (stashIntFindPointer(linkStruct->shardGuildStash, uGuildID, &pGuild))
			return pGuild;
	}
	return NULL;
}

void GlobalChatServer_RemoveGuild (U32 uChatServerID, U32 uGuildID)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatServerID);
	if (linkStruct)
	{
		ChatGuild *pGuild = NULL;
		ChatShard *pShard = linkStruct->uShardID ? findShardByID(linkStruct->uShardID) : NULL;
		if (pShard)
			ChatShard_RemoveGuild(pShard, uGuildID);

		stashIntRemovePointer(linkStruct->shardGuildStash, uGuildID, &pGuild);
		if (pGuild)
		{
			char name[64] = "";
			if (pGuild->pchName)
				stashRemoveInt(linkStruct->shardGuildNameStash, pGuild->pchName, NULL);
			sprintf(name, "%s-%d", linkStruct->pShardName, pGuild->iGuildID);
			stashRemovePointer(stGlobalChatAllGuilds, name, NULL);
			StructDestroy(parse_ChatGuild, pGuild);
		}
	}
}

void GlobalChatServer_UpdateGuildName (U32 uChatServerID, U32 uGuildID, char *pchGuildName)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatServerID);

	if (linkStruct)
	{
		ChatGuild *pGuild = NULL;
		ChatShard *pShard = linkStruct->uShardID ? findShardByID(linkStruct->uShardID) : NULL;
		if (pShard)
			ChatShard_UpdateGuildName(pShard, uGuildID, pchGuildName);

		stashIntFindPointer(linkStruct->shardGuildStash, uGuildID, &pGuild);
		if (pGuild)
		{
			if (pGuild->pchName)
				stashRemoveInt(linkStruct->shardGuildNameStash, pGuild->pchName, NULL);
			pGuild->pchName = allocAddString(pchGuildName); // Update the guild name
		}
		else
		{
			char name[64] = "";
			pGuild = StructCreate(parse_ChatGuild);
			pGuild->iGuildID = uGuildID;
			pGuild->iLinkID = uChatServerID;
			pGuild->pchName = allocAddString(pchGuildName);
			stashIntAddPointer(linkStruct->shardGuildStash, pGuild->iGuildID, pGuild, false);
			sprintf(name, "%s-%d", linkStruct->pShardName, pGuild->iGuildID);
			stashAddPointer(stGlobalChatAllGuilds, name, pGuild, true);
		}
		stashAddInt(linkStruct->shardGuildNameStash, pGuild->pchName, uGuildID, true);
	}
}

void GlobalChatServer_InitializeGuildNameBatch (U32 uChatServerID, ChatGuild **ppGuilds)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatServerID);
	if (linkStruct)
	{
		ChatShard *pShard = linkStruct->uShardID ? findShardByID(linkStruct->uShardID) : NULL;
		if (pShard)
			ChatShard_SyncGuildList(pShard, ppGuilds);

		EARRAY_CONST_FOREACH_BEGIN(ppGuilds, i, size);
		{
			ChatGuild *pGuild = NULL;
			stashIntFindPointer(linkStruct->shardGuildStash, ppGuilds[i]->iGuildID, &pGuild);
			if (pGuild)
			{
				if (pGuild->pchName)
					stashRemoveInt(linkStruct->shardGuildNameStash, pGuild->pchName, NULL);
				pGuild->pchName = allocAddString(ppGuilds[i]->pchName); // Update the guild name
			}
			else
			{
				pGuild = StructClone(parse_ChatGuild, ppGuilds[i]);
				if (pGuild)
				{
					char name[64] = "";
					pGuild->iLinkID = uChatServerID;
					stashIntAddPointer(linkStruct->shardGuildStash, pGuild->iGuildID, pGuild, false);

					sprintf(name, "%s-%d", linkStruct->pShardName, pGuild->iGuildID);
					stashAddPointer(stGlobalChatAllGuilds, name, pGuild, true);
				}
			}
			stashAddInt(linkStruct->shardGuildNameStash, pGuild->pchName, pGuild->iGuildID, true);
		}
		EARRAY_FOREACH_END;
	}
}

void GlobalChatServer_AddGuildMember(U32 uChatLinkID, ChatGuild *pGuild, ChatUser *user, ChatGuildMember *pMember)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatLinkID);
	if (!devassert(pGuild && user && pMember))
		return;
	if (linkStruct)
	{
		ChatGuild *pStoredGuild = NULL;
		stashIntFindPointer(linkStruct->shardGuildStash, pGuild->iGuildID, &pStoredGuild);
		if (pStoredGuild)
		{
			int guildId = pGuild->iGuildID;
			GlobalChatLinkUserGuilds *guilds = NULL;
			int pos = (int)ea32BFind(pStoredGuild->pGuildMembers, cmpU32, user->id);
			if (pos == ea32Size(&pStoredGuild->pGuildMembers) || pStoredGuild->pGuildMembers && pStoredGuild->pGuildMembers[pos] != user->id)
				ea32Insert(&pStoredGuild->pGuildMembers, user->id, pos);

			// Get this user's guild list.
			stashIntFindPointer(linkStruct->stUserGuilds, user->id, &guilds);
			if (!guilds)
			{
				guilds = StructCreate(parse_GlobalChatLinkUserGuilds);
				devassert(stashIntAddPointer(linkStruct->stUserGuilds, user->id, guilds, false));
			}

			// Add this item to chat lists if it is not already present.
			if (pMember->bCanChat)
			{
				pos = (int)eaiBFind(guilds->guilds, guildId);
				if (pos == eaiSize(&guilds->guilds) || guilds->guilds[pos] != guildId)
					eaiInsert(&guilds->guilds, guildId, pos);
			}
			if (pMember->bIsOfficer)
			{
				pos = (int)eaiBFind(guilds->officerGuilds, guildId);
				if (pos == eaiSize(&guilds->officerGuilds) || guilds->officerGuilds[pos] != guildId)
					eaiInsert(&guilds->officerGuilds, guildId, pos);
			}
			XMPPChat_GuildListAdd(user, pStoredGuild);
			userPlayerUpdateGuild(uChatLinkID, user, pStoredGuild, pMember);
		}		
	}
}

void GlobalChatServer_RemoveGuildMember(U32 uChatLinkID, ChatGuild *pGuild, ChatUser *user, ChatGuildMember *pMember)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uChatLinkID);
	if (linkStruct)
	{
		ChatGuild *pStoredGuild = NULL;
		stashIntFindPointer(linkStruct->shardGuildStash, pGuild->iGuildID, &pStoredGuild);
		if (pStoredGuild)
		{
			GlobalChatLinkUserGuilds *pGuilds = NULL;
			bool bOfficer = false;
			int i = (int)ea32BFind(pStoredGuild->pGuildMembers, cmpU32, user->id);
			if (pStoredGuild->pGuildMembers && i < ea32Size(&pStoredGuild->pGuildMembers) && pStoredGuild->pGuildMembers[i] == user->id)
				ea32Remove(&pStoredGuild->pGuildMembers, i);

			// Check if user was an officer before removing the guild IDs from the list
			if (stashIntFindPointer(linkStruct->stUserGuilds, user->id, &pGuilds))
			{
				if (eaiFind(&pGuilds->officerGuilds, pStoredGuild->iGuildID) >= 0)
					bOfficer = true;
			}
			GlobalChatGuildMembershipRemove(linkStruct, pStoredGuild, user);
			// TODO differentiate between kicks
			XMPPChat_GuildListRemove(user, pStoredGuild, bOfficer, false);
			userPlayerUpdateGuild(uChatLinkID, user, NULL, pMember);
		}
	}
}

// Decode guild membership from batch send.
void GlobalChatServer_AllGuildMembers(U32 uChatServerID, int *pMemberData)
{
	int i;
	int size;
	ChatGuildMember member = {0};

	PERFINFO_AUTO_START_FUNC();
	member.uCharacterID = 0;

	// Check data format.
	size = ea32Size(&pMemberData);
	if (!size || pMemberData[0] != CHATSERVER_CHATGUILDMEMBERARRAY_MAGIC)
	{
		AssertOrAlert("CHATSERVER.CHATGUILDMEMBERARRAY_MAGIC", "The guild member data from the shard has an unknown format.");
		return;
	}

	// Record all guild membership data.
	for (i = 1; i != size;)
	{
		U32 guildId;
		ChatGuild *guild;
		int guildSize;
		int j;

		// Get guild.
		guildId = pMemberData[i];
		++i;
		if (i == size)
		{
			AssertOrAlert("CHATSERVER.CHATGUILDMEMBERARRAY_SIZE_MISSING", "The guild member data is corrupted.");
			return;
		}
		guild = GlobalChatFindGuild(uChatServerID, guildId);
		if (!guild)
		{
			AssertOrAlert("CHATSERVER.CHATGUILDMEMBERARRAY_UNKNOWN", "Unknown guild in guild member dump");
			return;
		}

		// Get guild membership size.
		guildSize = pMemberData[i];
		++i;
		if (i + guildSize*2 > size)
		{
			AssertOrAlert("CHATSERVER.CHATGUILDMEMBERARRAY_TRUNCATED", "The guild member data is corrupted.");
			return;
		}

		// Process guild members.
		for (j = i; j != i + guildSize * 2; j += 2)
		{
			U32 accountId = pMemberData[j];
			U32 flags = pMemberData[j + 1];
			ChatUser *user = userFindByContainerId(accountId);

			if (user && *user->handle)
			{
				member.uAccountID = accountId;
				member.bCanChat = (flags & CHATSERVER_CHATGUILDMEMBERARRAY_CHAT) != 0;
				member.bIsOfficer = (flags & CHATSERVER_CHATGUILDMEMBERARRAY_OFFICER) != 0;
				GlobalChatServer_AddGuildMember(uChatServerID, guild, user, &member);
			}
		}
		i = j;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Return true if a user is a member of a guild.
bool GlobalChatServer_HasGuilds(ChatUser *pUser)
{
	StashTableIterator linkIterator;
	StashElement linkElement;
	bool hasGuilds = false;

	// Iterate over all guilds on all connected shards.
	GlobalChatGetShardListIterator(&linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		GlobalChatLinkUserGuilds *guilds = NULL;
		GlobalChatLinkStruct *link = (GlobalChatLinkStruct*) stashElementGetPointer(linkElement);

		// Get this user's guilds.
		stashIntFindPointer(link->stUserGuilds, pUser->id, &guilds);
		if (!guilds)
			continue;

		// Check for guilds.
		if (eaiSize(&guilds->guilds))
		{
			hasGuilds = true;
			break;
		}
	}

	return hasGuilds;
}

// Return true if a user is an officer of a guild.
bool GlobalChatServer_IsOfficer(ChatUser *pUser)
{
	StashTableIterator linkIterator;
	StashElement linkElement;
	bool isOfficer = false;

	// Iterate over all guilds on all connected shards.
	GlobalChatGetShardListIterator(&linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		GlobalChatLinkUserGuilds *guilds = NULL;
		GlobalChatLinkStruct *link = (GlobalChatLinkStruct*) stashElementGetPointer(linkElement);

		// Get this user's guilds.
		stashIntFindPointer(link->stUserGuilds, pUser->id, &guilds);
		if (!guilds)
			continue;

		// Check for guilds.
		if (eaiSize(&guilds->officerGuilds))
		{
			isOfficer = true;
			break;
		}
	}

	return isOfficer;
}

bool GlobalChatServer_IsInGuild(ChatUser *pUser, ChatGuild *pGuild, bool bCheckOfficer)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(pGuild->iLinkID);
	if (linkStruct)
	{
		GlobalChatLinkUserGuilds *guilds = NULL;

		// Get this user's guilds.
		if (stashIntFindPointer(linkStruct->stUserGuilds, pUser->id, &guilds))
		{
			if (bCheckOfficer)
			{
				if (eaiFind(&guilds->officerGuilds, pGuild->iGuildID) >= 0)
					return true;
			}
			else
			{
				if (eaiFind(&guilds->guilds, pGuild->iGuildID) >= 0)
					return true;
			}
		}
	}
	return false;
}

// Get a list of guilds a user is in.
void GlobalChatServer_UserGuilds(ChatGuild ***eaGuilds, ChatUser *pUser)
{
	StashTableIterator linkIterator;
	StashElement linkElement;

	// Validate parameters.
	devassert(eaGuilds);
	if (!eaGuilds)
		return;
	eaClear(eaGuilds);
	if (!pUser)
		return;

	// Iterate over all guilds on all connected shards.
	GlobalChatGetShardListIterator(&linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		GlobalChatLinkUserGuilds *guilds = NULL;
		GlobalChatLinkStruct *link = (GlobalChatLinkStruct*) stashElementGetPointer(linkElement);

		// Get this user's guilds.
		stashIntFindPointer(link->stUserGuilds, pUser->id, &guilds);
		if (!guilds)
			continue;

		// Collect each guild.
		EARRAY_INT_CONST_FOREACH_BEGIN(guilds->guilds, i, n);
		{
			ChatGuild *guild = NULL;
			if (stashIntFindPointer(link->shardGuildStash, guilds->guilds[i], &guild))
				eaPush(eaGuilds, guild);
		}
		EARRAY_FOREACH_END;
	}
}

// Get a list of guilds a user is an officer for.
void GlobalChatServer_UserOfficerGuilds(ChatGuild ***eaGuilds, ChatUser *pUser)
{
	StashTableIterator linkIterator;
	StashElement linkElement;

	// Validate parameters.
	devassert(eaGuilds);
	if (!eaGuilds)
		return;
	eaClear(eaGuilds);
	if (!pUser)
		return;

	// Iterate over all guilds on all connected shards.
	GlobalChatGetShardListIterator(&linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		GlobalChatLinkUserGuilds *guilds = NULL;
		GlobalChatLinkStruct *link = (GlobalChatLinkStruct*) stashElementGetPointer(linkElement);

		// Get this user's guilds.
		stashIntFindPointer(link->stUserGuilds, pUser->id, &guilds);
		if (!guilds)
			continue;

		// Collect each guild.
		EARRAY_INT_CONST_FOREACH_BEGIN(guilds->officerGuilds, i, n);
		{
			ChatGuild *guild = NULL;
			if (stashIntFindPointer(link->shardGuildStash, guilds->officerGuilds[i], &guild))
				eaPush(eaGuilds, guild);
		}
		EARRAY_FOREACH_END;
	}
}

GlobalChatLinkUserGuilds *GlobalChatServer_UserGuildsByShard(ChatUser *pUser, U32 uShardID)
{
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uShardID);
	if (linkStruct)
	{
		GlobalChatLinkUserGuilds *guilds = NULL;
		// Get this user's guilds.
		if (stashIntFindPointer(linkStruct->stUserGuilds, pUser->id, &guilds))
		{
			return guilds;
		}
	}
	return NULL;
}

// Get a list of common guilds between two users.
void GlobalChatServer_CommonGuilds(ChatGuild ***eaGuilds, ChatUser *pLhs, ChatUser *pRhs)
{
	StashTableIterator linkIterator;
	StashElement linkElement;

	// Validate parameters.
	devassert(eaGuilds);
	if (!eaGuilds)
		return;
	eaClear(eaGuilds);
	if (!pLhs || !pRhs)
		return;

	// Iterate over all guilds on all connected shards.
	GlobalChatGetShardListIterator(&linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		GlobalChatLinkUserGuilds *lhsGuilds = NULL, *rhsGuilds = NULL;
		GlobalChatLinkStruct *link = (GlobalChatLinkStruct*) stashElementGetPointer(linkElement);
		int i = 0, j = 0;

		// Get the users' guilds.
		stashIntFindPointer(link->stUserGuilds, pLhs->id, &lhsGuilds);
		stashIntFindPointer(link->stUserGuilds, pRhs->id, &rhsGuilds);
		if (!lhsGuilds || !rhsGuilds)
			continue;

		// Find guild intersection over sorted guild list.
		while (i < eaiSize(&lhsGuilds->guilds) && j < eaiSize(&rhsGuilds->guilds))
		{
			if (lhsGuilds->guilds[i] == rhsGuilds->guilds[j])
			{
				ChatGuild *guild = NULL;
				if (stashIntFindPointer(link->shardGuildStash, lhsGuilds->guilds[i], &guild))
					eaPush(eaGuilds, guild);
				++j;
				++i;
			}
			else if (lhsGuilds->guilds[i] < rhsGuilds->guilds[j])
				++i;
			else
				++j;
		}
	}
}

// Get a list of guilds for which two users are both officers.
void GlobalChatServer_CommonOfficers(ChatGuild ***eaGuilds, ChatUser *pLhs, ChatUser *pRhs)
{
	StashTableIterator linkIterator;
	StashElement linkElement;

	// Validate parameters.
	devassert(eaGuilds);
	if (!eaGuilds)
		return;
	eaClear(eaGuilds);
	if (!pLhs || !pRhs)
		return;

	// Iterate over all guilds on all connected shards.
	GlobalChatGetShardListIterator(&linkIterator);
	while (stashGetNextElement(&linkIterator, &linkElement))
	{
		GlobalChatLinkUserGuilds *lhsGuilds = NULL, *rhsGuilds = NULL;
		GlobalChatLinkStruct *link = (GlobalChatLinkStruct*) stashElementGetPointer(linkElement);
		int i = 0, j = 0;

		// Get the users' guilds.
		stashIntFindPointer(link->stUserGuilds, pLhs->id, &lhsGuilds);
		stashIntFindPointer(link->stUserGuilds, pRhs->id, &rhsGuilds);
		if (!lhsGuilds || !rhsGuilds)
			continue;

		// Find guild intersection over sorted guild list.
		while (i < eaiSize(&lhsGuilds->officerGuilds) && j < eaiSize(&rhsGuilds->officerGuilds))
		{
			if (lhsGuilds->officerGuilds[i] == rhsGuilds->officerGuilds[j])
			{
				ChatGuild *guild = NULL;
				if (stashIntFindPointer(link->shardGuildStash, lhsGuilds->officerGuilds[i], &guild))
					eaPush(eaGuilds, guild);
				++j;
				++i;
			}
			else if (lhsGuilds->officerGuilds[i] < rhsGuilds->officerGuilds[j])
				++i;
			else
				++j;
		}
	}
}

////////////////////////////////////////////////
// Remote Commands

AUTO_COMMAND_REMOTE;
void ChatServerGuildCreateFailed(const char *guildName)
{
	if (!nullStr(guildName))
	{
		NetLink *localChatLink = chatServerGetCommandLink();
		U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
		GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uID);
		if (linkStruct)
		{
			ChatShard *pShard = linkStruct->uShardID ? findShardByID(linkStruct->uShardID) : NULL;
			if (pShard)
				ChatShard_ReleaseReservedGuildName(pShard, guildName);
		}
	}
}

AUTO_COMMAND_REMOTE;
void ChatServerReserveGuildName(const char *guildName, int iCmdID)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	GlobalChatLinkStruct *linkStruct = GlobalChatGetShardData(uID);
	bool bSuccess = false;

	if (linkStruct && !nullStr(guildName))
	{
		ChatShard *pShard = linkStruct->uShardID ? findShardByID(linkStruct->uShardID) : NULL;
		if (pShard)
			bSuccess = ChatShard_ReserveGuildName(pShard, guildName);
	}
	sendCommandToLinkEx(localChatLink, "ChatServerReserveGuildName_Response", "%d %d", bSuccess, iCmdID);
}

AUTO_COMMAND_REMOTE;
void ChatServerRemoveGuild(CmdContext *context, ContainerID iGuildID)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	GlobalChatServer_RemoveGuild(uID, iGuildID);
}

AUTO_COMMAND_REMOTE;
void ChatServerUpdateGuild(CmdContext *context, ContainerID iGuildID, char *pchName)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	GlobalChatServer_UpdateGuildName(uID, iGuildID, pchName);
}

AUTO_COMMAND_REMOTE;
void ChatServerUpdateGuildBatch(CmdContext *context, ChatGuildList *pGuildList)
{
	NetLink *localChatLink;
	U32 uID;

	PERFINFO_AUTO_START_FUNC();

	localChatLink = chatServerGetCommandLink();
	uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	GlobalChatServer_InitializeGuildNameBatch(uID, pGuildList->ppGuildList);

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND_REMOTE;
void ChatServerAllGuildMembers(CmdContext *context, ChatGuildMemberArray *pMemberArray)
{
	NetLink *localChatLink;
	U32 uID;

	PERFINFO_AUTO_START_FUNC();
	loadstart_printf("Processing guild member list...");

	localChatLink = chatServerGetCommandLink();
	uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	GlobalChatServer_AllGuildMembers(uID, pMemberArray->pMemberData);
	loadend_printf(" done (%d items)", eaiSize(&pMemberArray->pMemberData));
	PERFINFO_AUTO_STOP_FUNC();
}

// Add a member to a guild or update the chat permissions of an existing member.
AUTO_COMMAND_REMOTE;
void ChatServerAddGuildMemberByIdEx(CmdContext *context, ChatGuild *pGuild, ChatGuildMember *pMember)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	ChatUser *user = userFindByContainerId(pMember->uAccountID);
	if (user)
		GlobalChatServer_AddGuildMember(uID, pGuild, user, pMember);
}
AUTO_COMMAND_REMOTE;
void ChatServerAddGuildMemberById(CmdContext *context, ChatGuild *pGuild, U32 uAccountId, U32 uFlags)
{
	ChatGuildMember member = {0};
	member.uAccountID = uAccountId;
	member.uCharacterID = 0;
	member.bCanChat = (uFlags & CHATSERVER_CHATGUILDMEMBERARRAY_CHAT) != 0;
	member.bIsOfficer = (uFlags & CHATSERVER_CHATGUILDMEMBERARRAY_OFFICER) != 0;

	ChatServerAddGuildMemberByIdEx(context, pGuild, &member);
}

// A member has left the guild.
AUTO_COMMAND_REMOTE;
void ChatServerRemoveGuildMemberByIdEx(CmdContext *context, ChatGuild *pGuild, ChatGuildMember *pMember)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	ChatUser *user = userFindByContainerId(pMember->uAccountID);
	if (user)
		GlobalChatServer_RemoveGuildMember(uID, pGuild, user, pMember);
}
AUTO_COMMAND_REMOTE;
void ChatServerRemoveGuildMemberById(CmdContext *context, ChatGuild *pGuild, U32 uAccountId)
{
	ChatGuildMember member = {0};
	member.uAccountID = uAccountId;
	member.uCharacterID = 0;
	ChatServerRemoveGuildMemberByIdEx(context, pGuild, &member);
}

AUTO_COMMAND_REMOTE;
void ChatServerGuildRankChange(CmdContext *context, ChatGuildRankData *pData)
{
	NetLink *localChatLink = chatServerGetCommandLink();
	U32 uID = localChatLink ? GetLocalChatLinkID(localChatLink) : 0;
	ChatGuild *pGuild = GlobalChatFindGuild(uID, pData->iGuildID);

	if (!pGuild)
		return;

	EARRAY_INT_CONST_FOREACH_BEGIN(pData->eaiMembers, i, s);
	{
		ChatUser *user = userFindByContainerId(pData->eaiMembers[i]);
		if (user)
		{
			PlayerInfoStruct *pInfo = findPlayerInfoByLocalChatServerID(user, uID);
			if (pInfo && pInfo->iPlayerGuild == pData->iGuildID)
			{
				if (pData->bCanGuildChat != pInfo->bCanGuildChat)
				{
					pInfo->bCanGuildChat = pData->bCanGuildChat;
					if (pInfo->bCanGuildChat)
						XMPPChat_NotifyGuildOnline(pGuild , user, false);
					else
						XMPPChat_NotifyGuildOffline(pGuild, user, false);
				}
				if (pData->bIsOfficer != pInfo->bIsOfficer)
				{
					pInfo->bIsOfficer = pData->bIsOfficer;
					if (pInfo->bIsOfficer)
						XMPPChat_NotifyGuildOnline(pGuild , user, true);
					else
						XMPPChat_NotifyGuildOffline(pGuild, user, true);
				}
			}
		}
	}
	EARRAY_FOREACH_END;
}

// Obsolete
AUTO_COMMAND_REMOTE;
void ChatServerAddGuildMemberWithFlags(CmdContext *context, ChatGuild *pGuild, const char *publicAccountName, U32 uFlags)
{
	ChatUser *user = userFindByHandle(publicAccountName);
	if (user)
		ChatServerAddGuildMemberById(context, pGuild, user->id, uFlags);
}

// Obsolete
AUTO_COMMAND_REMOTE;
void ChatServerRemoveGuildMember(CmdContext *context, ChatGuild *pGuild, const char *publicAccountName)
{
	ChatUser *user = userFindByHandle(publicAccountName);
	if (user)
		ChatServerRemoveGuildMemberById(context, pGuild, user->id);
}

// Obsolete
AUTO_COMMAND_REMOTE;
void ChatServerAddGuildMember(CmdContext *context, ChatGuild *pGuild, const char *publicAccountName)
{
	ChatServerAddGuildMemberWithFlags(context, pGuild, publicAccountName, 0);
}

// Obsolete
AUTO_COMMAND_REMOTE;
void ChatServerUpdateGuildMembers(CmdContext *context, ChatGuild *pGuild)
{
	return;
}

#include "AutoGen/chatGuild_h_ast.c"