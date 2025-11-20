#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

#include "Autogen/guildCommonStructs_h_ast.h"
#include "Message.h"

AUTO_STRUCT;
typedef struct GuildEventData
{
	U32 uiID;

	char *pcTitle;
	char *pcDescription;

	U32 iStartTimeTime;
	U32 iDuration;
	U32 eRecurType;
	S32 iRecurrenceCount;

	S32 iMinGuildRank;
	S32 iMinGuildEditRank;
	U32 iMinLevel;
	U32 iMaxLevel;
	U32 iMinAccepts;
	U32 iMaxAccepts;
} GuildEventData;
