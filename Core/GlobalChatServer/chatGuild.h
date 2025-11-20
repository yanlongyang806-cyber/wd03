#pragma once

typedef struct ChatUser ChatUser;
typedef struct ChatGuild ChatGuild;
typedef struct ChatGuildMember ChatGuildMember;

// Set of guilds that this chat user is a member of
AUTO_STRUCT;
typedef struct GlobalChatLinkUserGuilds
{
	INT_EARRAY guilds;					// Sorted array of guild IDs
	INT_EARRAY officerGuilds;			// Sorted array of guild IDs, that this user is an officer for
} GlobalChatLinkUserGuilds;

U32 GlobalChatGuildIdByName (U32 uChatServerID, const char *pchGuildName);
ChatGuild *GlobalChatFindGuild (U32 uChatServerID, U32 uGuildID);
void GlobalChatRequestGuildUpdate (U32 uChatServerID, U32 uGuildID);
void GlobalChatServer_UpdateGuildName (U32 uChatServerID, U32 uGuildID, char *pchGuildName);
void GlobalChatServer_InitializeGuildNameBatch (U32 uChatServerID, ChatGuild **ppGuilds);
void GlobalChatServer_InitializeGuildMembers(U32 uChatServerID, ChatGuild *pGuild);
void GlobalChatServer_AllGuildMembers(U32 uChatServerID, int *pMemberData);
void GlobalChatServer_AddGuildMember(U32 uChatServerID, ChatGuild *pGuild, ChatUser *user, ChatGuildMember *pMember);
void GlobalChatServer_RemoveGuildMember(U32 uChatServerID, ChatGuild *pGuild, ChatUser *user, ChatGuildMember *pMember);

// Return true if a user is a member of any guild.
bool GlobalChatServer_HasGuilds(ChatUser *pUser);

// Return true if a user is an officer of any guild.
bool GlobalChatServer_IsOfficer(ChatUser *pUser);

// Returns true if the user is in the specified guild (also checks if the user is an officer if specified)
bool GlobalChatServer_IsInGuild(SA_PARAM_NN_VALID ChatUser *pUser, SA_PARAM_NN_VALID ChatGuild *pGuild, bool bCheckOfficer);

// Get a list of guilds a user is in.
void GlobalChatServer_UserGuilds(ChatGuild ***eaGuilds, ChatUser *pUser);

// Get a list of guilds a user is an officer for.
void GlobalChatServer_UserOfficerGuilds(ChatGuild ***eaGuilds, ChatUser *pUser);

// Get the guild info for a user for the specified connected shard
GlobalChatLinkUserGuilds *GlobalChatServer_UserGuildsByShard(ChatUser *pUser, U32 uShardID);

// Get a list of guilds for which two users are both members.
void GlobalChatServer_CommonGuilds(ChatGuild ***eaGuilds, ChatUser *pLhs, ChatUser *pRhs);

// Get a list of guilds for which two users are both officers.
void GlobalChatServer_CommonOfficers(ChatGuild ***eaGuilds, ChatUser *pLhs, ChatUser *pRhs);