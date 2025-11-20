/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "GlobalTypeEnum.h"

typedef struct GuildRankList GuildRankList;
typedef struct GuildEmblemList GuildEmblemList;
typedef struct GuildCustomRank GuildCustomRank;

AUTO_STRUCT;
typedef struct ASLGuildCBData
{
	char *pcActionType;

	ContainerID iEntID;
	ContainerID iSubjectID;
	ContainerID iGuildID;
	ContainerID iTeamID;
	ContainerID iGuildEventID;

	const char *pchClassName;	AST(POOL_STRING)
} ASLGuildCBData;

Guild *aslGuild_GetGuild(ContainerID iGuildID);
Entity *aslGuild_GetGuildBank(ContainerID iGuildID);
Guild *aslGuild_GetGuildByName(const char *pcName, ContainerID iVirtualShardID);

void aslGuild_PushMemberAddToChatServer (U32 uGuildID, const char *pchGuildName, U32 uAccountId,
										 S32 iRank, const GuildCustomRank *const *ppRanks);
void aslGuild_PushMemberRemoveToChatServer (U32 uGuildID, const char *pchGuildName, U32 uAccountId);
