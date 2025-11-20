#pragma once
GCC_SYSTEM

AUTO_ENUM;
typedef enum ActivityType
{
	kActivityType_Status,
	kActivityType_Screenshot,
	kActivityType_Blog,
	kActivityType_LevelUp,
	kActivityType_Perk,
	kActivityType_Item,
	kActivityType_GuildCreate, ENAMES(Guild)
	kActivityType_GuildJoin, ENAMES(Guild)
	kActivityType_GuildLeave, ENAMES(Guild)
	kActivityType_GuildRankChange, ENAMES(Guild)

	kActivityType_Count, EIGNORE
} ActivityType;
extern StaticDefineInt ActivityTypeEnum[];

AUTO_STRUCT;
typedef struct SocialServices
{
	const char **ppServices; AST(POOL_STRING)
} SocialServices;

AUTO_ENUM;
typedef enum ActivityVerbosity
{
	kActivityVerbosity_None=0,
	kActivityVerbosity_Low=1,
	kActivityVerbosity_Medium=2,
	kActivityVerbosity_High=3,
	kActivityVerbosity_All=4,

	kActivityVerbosity_Count, EIGNORE
	kActivityVerbosity_Default=kActivityVerbosity_Medium, EIGNORE
} ActivityVerbosity;
extern StaticDefineInt ActivityVerbosityEnum[];