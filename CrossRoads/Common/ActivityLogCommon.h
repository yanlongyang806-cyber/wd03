/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "ActivityLogEnums.h"
#include "Message.h"
#include "CombatEnums.h"

typedef struct Entity Entity;
typedef struct Guild Guild;
typedef U32 ContainerID;

AUTO_STRUCT AST_CONTAINER;
typedef struct ActivityLogEntry
{
	// This ID is unique per player
	const U32 entryID;					AST(PERSIST SUBSCRIBE KEY)

	// The type of entry
	const S32 type;						AST(PERSIST SUBSCRIBE SUBTABLE(ActivityLogEntryTypeEnum))

	// Argument string for this log entry
	CONST_STRING_MODIFIABLE argString;	AST(PERSIST SUBSCRIBE)

	// Time the event happened
	const U32 time;						AST(PERSIST SUBSCRIBE)

	// The subject of the log entry
	const ContainerID subjectID;		AST(PERSIST SUBSCRIBE)

	// For player activity logs we record the player's fTotalPlayTime at the time the event happened
	const float playedTimeAtEvent;		AST(PERSIST SUBSCRIBE)
} ActivityLogEntry;

AUTO_STRUCT;
typedef struct ActivityLogEntryTypeConfig
{
	// The entry type that this config is for.
	S32 entryType;									AST(KEY STRUCTPARAM SUBTABLE(ActivityLogEntryTypeEnum))

	// If true, events of this type will be added to the personal log
	bool addToPersonalLog;							AST(BOOLFLAG)

	// If true, events of this type will be added to the guild log
	bool addToGuildLog;								AST(BOOLFLAG)

	// Should the argument string be translated
	bool translateArg;								AST(BOOLFLAG)

	// Should the second argument string (of a split string) be translated
	bool translateArg2;								AST(BOOLFLAG)

	// The argString should be split into two strings, delimited by a newline
	bool splitArgString;							AST(BOOLFLAG)

	// The message to display as the diary entry title for entries of this type
	DisplayMessage diaryTitleDisplayStringMsg;		AST(NAME(DiaryTitleDisplayString) STRUCT(parse_DisplayMessage))

	// The message to display as the diary entry body for entries of this type
	DisplayMessage diaryBodyDisplayStringMsg;		AST(NAME(DiaryBodyDisplayString) STRUCT(parse_DisplayMessage))

	// The message to display in the guild log for entries of this type
	DisplayMessage guildLogDisplayStringMsg;		AST(NAME(GuildLogDisplayString) STRUCT(parse_DisplayMessage))
} ActivityLogEntryTypeConfig;

AUTO_STRUCT;
typedef struct ActivityLogPetEventConfig
{
	// The type of pet that this configuration is for
	const S32 petType;								AST(KEY STRUCTPARAM SUBTABLE(CharClassTypesEnum))

	// The ActivityLogEntryType to use for the player when adding a pet of this type
	const S32 playerAddPetEntryType;				AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the player when dismissing a pet of this type
	const S32 playerDismissPetEntryType;			AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the player when renaming a pet of this type
	const S32 playerRenamePetEntryType;				AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the source player when trading a pet of this type
	const S32 sourcePlayerTradePetEntryType;		AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the destination player when trading a pet of this type
	const S32 destPlayerTradePetEntryType;			AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the pet when adding a pet of this type
	const S32 petAddPetEntryType;					AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the pet when renaming a pet of this type
	const S32 petRenamePetEntryType;				AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the pet when trading a pet of this type
	const S32 petTradePetEntryType;					AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the player when promoting a pet of this type
	const S32 playerPromotePetEntryType;			AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the pet when promoting a pet of this type
	const S32 petPromotePetEntryType;				AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the player when training a pet of this type
	const S32 playerTrainPetEntryType;				AST(SUBTABLE(ActivityLogEntryTypeEnum))

	// The ActivityLogEntryType to use for the pet when training a pet of this type
	const S32 petTrainPetEntryType;					AST(SUBTABLE(ActivityLogEntryTypeEnum))
} ActivityLogPetEventConfig;

AUTO_STRUCT;
typedef struct ActivityLogConfig
{
	// Maximum number of entries to keep for a guild.  Will remove older entries when this limit is reached.
	// A value of zero means no limit.
	S32 maxGuildLogSize;

	// Per activity type configuration.
	EARRAY_OF(ActivityLogEntryTypeConfig) entryTypeConfigs;		AST(NAME("EntryTypeConfig"))

	// Configuration for pet events.
	EARRAY_OF(ActivityLogPetEventConfig) petEventConfigs;		AST(NAME("PetEventConfig"))
} ActivityLogConfig;

ActivityLogEntryTypeConfig *ActivityLog_GetTypeConfig(ActivityLogEntryType entryType);
int ActivityLog_GetMaxGuildLogSize(void);
ActivityLogPetEventConfig *ActivityLog_GetPetEventConfig(CharClassTypes petType);
const char ***ActivityLog_GetEntryTypeNames(void);
void ActivityLog_FormatEntry(Language lang, char **estrDest, ActivityLogEntry *pLogEntry, Guild *guild);