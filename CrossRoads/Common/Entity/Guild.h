#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "GlobalTypeEnum.h"
#include "Message.h"
#include "itemEnums.h"

#define GUILD_MAX_SIZE 500
#define GUILD_CLEANUP_INTERVAL 30
#define GUILD_TIME_OUT_INTERVAL 30.f
#define GUILD_VALIDATE_MEMBER_TIME 60					// wait one minute
#define GUILD_VALIDATE_MEMBER_TIME_FAIL 0xffffffff		// don't try any more on this server
#define GUILD_BANK_LOG_MAX_SIZE 250
#define GUILD_UNIFORM_MAX_COUNT 12
#define GUILD_MAIL_TIME 86400
#define GUILD_LEADERSHIP_TIMEOUT (30 * 24 * 60 * 60) // 30 days, in seconds

typedef struct ActivityLogEntry ActivityLogEntry;
typedef struct Entity Entity;
typedef struct InventoryV1 InventoryV1;
typedef struct Item Item;
typedef struct ItemDef ItemDef;
typedef struct PCTextureDef PCTextureDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct SpeciesDef SpeciesDef;
typedef struct NOCONST(Guild) NOCONST(Guild);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef enum enumTransactionOutcome enumTransactionOutcome;

extern StaticDefineInt InvBagIDsEnum[];

bool guild_LazySubscribeToBank(void);
bool guild_LazyCreateBank(void);
const char *guildBankConfig_GetBankTabPurchaseNumericName(void);
const char *guildBankConfig_GetBankTabUnlockNumericName(void);
U32 guildBankConfig_GetBankTabPurchaseCost(int index);

AUTO_ENUM;
typedef enum
{
	GuildPermission_Invite =			(1 << 0),
	GuildPermission_Remove =			(1 << 1),
	GuildPermission_PromoteBelowRank =	(1 << 2),
	GuildPermission_PromoteToRank =		(1 << 3),
	GuildPermission_DemoteBelowRank =	(1 << 4),
	GuildPermission_DemoteAtRank =		(1 << 5),
	GuildPermission_Chat =				(1 << 6),
	GuildPermission_OfficerChat =		(1 << 7),
	GuildPermission_SetMotD =			(1 << 8),
	GuildPermission_Rename =			(1 << 9),
	GuildPermission_SetLook =			(1 << 10),
	GuildPermission_RenameRank =		(1 << 11),
	GuildPermission_SetPermission =		(1 << 12),
	GuildPermission_SetBankPermission =	(1 << 13),
	GuildPermission_BuyBankTab =		(1 << 14),
	GuildPermission_RenameBankTab =		(1 << 15),
	GuildPermission_GuildMail =			(1 << 16),
	GuildPermission_SeeOfficerComment =	(1 << 17),
	GuildPermission_ChangeOfficerComment = (1 << 18),
	GuildPermission_PostEvent =			(1 << 19),
	GuildPermission_StealEventLowerRank = (1 << 20), // Deprecated
	GuildPermission_StealEventCurRank =	(1 << 21), // Deprecated
	GuildPermission_SetRecruitInfo =	(1 << 22), 
	GuildPermission_RemoveEvent =		(1 << 23), // Deprecated
	GuildPermission_GuildMapInvites =	(1 << 24),
    GuildPermission_GuildProjectManagement = (1 << 25),
    GuildPermission_DonateToProjects =  (1 << 26),
    GuildPermission_BuyProvisioned =    (1 << 27),
    GuildPermission_ChangeAllegiance =  (1 << 28),
} GuildRankPermissions;

AUTO_ENUM;
typedef enum GuildStatUpdateOperation 
{
	GuildStatUpdateOperation_None,

	// Adds the input to the existing guild stat value
	GuildStatUpdateOperation_Add = 1,

	// Subtract the input from the existing guild stat value
	GuildStatUpdateOperation_Subtract = 1 << 1,

	// Sets the guild stat value to the greater of current value and input value
	GuildStatUpdateOperation_Max = 1 << 2,

	// Sets the guild stat value to the greater of current value and input value
	GuildStatUpdateOperation_Min = 1 << 3

} GuildStatUpdateOperation;

AUTO_STRUCT;
typedef struct GuildStatDef
{
	// The name of the guild stat
	const char *pchName;								AST( STRUCTPARAM KEY POOL_STRING )

	// Display name for the guild stat
	DisplayMessage displayName;							AST(STRUCT(parse_DisplayMessage) NAME(DisplayName))

	// Operations valid on this guild stat
	GuildStatUpdateOperation eValidOperations;		AST(NAME("ValidOperations") FLAGS)

	// The initial value for the guild stat
	U32 iInitialValue;

	const char *pchFilename;							AST( CURRENTFILE )

} GuildStatDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildStat
{
	CONST_STRING_POOLED pchStatName;				AST(PERSIST STRUCTPARAM KEY POOL_STRING SUBSCRIBE)
	const S32 iValue;								AST(PERSIST SUBSCRIBE)

} GuildStat;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildStatsInfo
{
	// Current version of the guild stats info
	const U32 uiVersion;								AST(PERSIST SUBSCRIBE)

	// All the stats for the guild
	CONST_EARRAY_OF(GuildStat) eaGuildStats;			AST(PERSIST SUBSCRIBE)

} GuildStatsInfo;

AUTO_STRUCT;
typedef struct GuildThemeDef
{
	// The name of the guild theme
	const char *pchName;								AST(STRUCTPARAM KEY POOL_STRING)

	// Display name for the guild theme
	DisplayMessage displayName;							AST(STRUCT(parse_DisplayMessage) NAME(DisplayName))

	// The detailed description for the guild theme
	DisplayMessage description;							AST(STRUCT(parse_DisplayMessage) NAME(Description))

	const char *pchFilename;							AST(CURRENTFILE)
} GuildThemeDef;

AUTO_ENUM;
typedef enum
{
	GuildPermission_Deposit =			(1 << 0),
	GuildPermission_Withdraw =			(1 << 1),
} GuildBankPermissions;

AUTO_STRUCT;
typedef struct GuildRank
{
	const char *pcName;				AST(POOL_STRING STRUCTPARAM NAME("Name"))
	const char *pcDisplayNameMsg;	AST(POOL_STRING NAME("DisplayMessage"))
	GuildRankPermissions ePerms;	AST(FLAGS NAME("Permissions"))
} GuildRank;

AUTO_STRUCT;
typedef struct GuildRankList
{
	GuildRank **eaRanks;		AST(NAME("GuildRank"))
} GuildRankList;

AUTO_STRUCT;
typedef struct GuildEmblem
{
	REF_TO(PCTextureDef) hTexture; AST(STRUCTPARAM NAME("Name") REFDICT(CostumeTexture))
	U32 bBackground;				AST(STRUCTPARAM DEF(1))
	U32 bDetail;					AST(STRUCTPARAM) // Third type of texture
	U32 bFalse;						AST(STRUCTPARAM) // Place holder texture; Not to actually be shown or used; This was made a guild emblem to have a valid guild before players could choose a guild emblem
} GuildEmblem;

AUTO_STRUCT;
typedef struct GuildEmblemList
{
	GuildEmblem **eaEmblems;	AST(NAME("GuildEmblem"))
} GuildEmblemList;

AUTO_STRUCT;
typedef struct GuildConfig
{
	///////////////////////////////
	//  Allegiance Change

	// the numeric and cost required for changing the allegiance. Ignored if cost is zero.
	S32 iAllegianceChangeCost;
    STRING_POOLED pchAllegianceChangeNumeric;   AST(NAME("AllegianceChangeNumeric") POOL_STRING)

	///////////////////////////////
	//  AutoGuild stuff (was used for Champions
	
	// the base name of the auto autoguild guild, if this is not present then there is no auto guild
	const char *pcAutoName;			AST(NAME("Name") POOL_STRING)

	// the maximum number of seconds allowed for the auto guild new members
	U32 uMaxSecondsAllowed;

	// Add devs if set (ignore hours played if AL > 0 or IsDev)
	bool bAddDevs;

	// the description to use when creating the autoguild
	const char *pcDescription;		AST(NAME("Description") POOL_STRING)

	// the message of the day
	const char *pcMessage;			AST(POOL_STRING)

	// emblem data
	const char *pcEmblem;			AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary

	// guild colors, using 1 and 2 as this is the same name as in struct Guild
	const U32 iColor1;		
	const U32 iColor2;		

}GuildConfig;

AUTO_STRUCT AST_CONTAINER; 
typedef struct GuildBankLogEntryNames
{
	CONST_STRING_POOLED pcItemName;							AST(PERSIST SUBSCRIBE POOL_STRING)
}GuildBankLogEntryNames;

AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT(pItem);
typedef struct GuildBankLogEntry
{
	const U32 iTimestamp;									AST(PERSIST SUBSCRIBE)
	const U32 iEntID;										AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcEntName;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcEntAccount;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pcItemDef;							AST(PERSIST SUBSCRIBE POOL_STRING)
	const S32 iNumberMoved;									AST(PERSIST SUBSCRIBE)
	const InvBagIDs eBag;									AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(GuildBankLogEntryNames) ppItemNames;	AST(PERSIST SUBSCRIBE)
	
} GuildBankLogEntry;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildBankTabPermission
{
	const GuildBankPermissions ePerms;						AST(PERSIST SUBSCRIBE)
	const S32 iWithdrawLimit;								AST(PERSIST SUBSCRIBE)
	const S32 iWithdrawItemCountLimit;						AST(PERSIST SUBSCRIBE)
} GuildBankTabPermission;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildBankTabInfo
{
	CONST_STRING_MODIFIABLE pcName;							AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(GuildBankTabPermission) eaPermissions;	AST(PERSIST SUBSCRIBE)
} GuildBankTabInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildCustomRank
{
	CONST_STRING_POOLED pcName;					AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_STRING_MODIFIABLE pcDisplayName;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pcDefaultNameMsg;		AST(PERSIST SUBSCRIBE POOL_STRING)
	const GuildRankPermissions ePerms;			AST(PERSIST SUBSCRIBE)
} GuildCustomRank;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildWithdrawLimit
{
	const InvBagIDs eBagID;					AST(PERSIST SUBSCRIBE KEY)
	const U32 iTimestamp;					AST(PERSIST SUBSCRIBE)
	const S32 iWithdrawn;					AST(PERSIST SUBSCRIBE)
	const S32 iItemsWithdrawn;				AST(PERSIST SUBSCRIBE)
} GuildWithdrawLimit;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildMember
{
	const ContainerID iEntID;				AST(PERSIST SUBSCRIBE KEY)
	const U32 iJoinTime;					AST(PERSIST SUBSCRIBE)
	
	CONST_STRING_MODIFIABLE pcName;			AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcAccount;		AST(PERSIST SUBSCRIBE)
	const U32 iAccountID;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcStatus;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcLogName;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcOfficerComment;		AST(PERSIST SUBSCRIBE SERVER_ONLY)
	CONST_STRING_MODIFIABLE pcWhoOfficerComment;	AST(PERSIST SUBSCRIBE SERVER_ONLY)
	const U32 iOfficerCommentTime;					AST(PERSIST SUBSCRIBE SERVER_ONLY)
	CONST_STRING_MODIFIABLE pcPublicComment;	AST(PERSIST SUBSCRIBE)
	const U32 iPublicCommentTime;				AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pchClassName;		AST(PERSIST SUBSCRIBE POOL_STRING)
	const S32 iRank;						AST(PERSIST SUBSCRIBE)
	const U32 iLevel;						AST(PERSIST SUBSCRIBE)
	const S32 iOfficerRank;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pcMapName;			AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_STRING_POOLED pcMapVars;			AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_STRING_MODIFIABLE pcMapMsgKey;	AST(PERSIST SUBSCRIBE)
	const U32 iMapInstanceNumber;			AST(PERSIST SUBSCRIBE)
	const bool bOnline;						AST(PERSIST SUBSCRIBE)
	const U32 iLogoutTime;					AST(PERSIST SUBSCRIBE)
	const U32 eLFGMode;						AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(GuildWithdrawLimit) eaWithdrawLimits;	AST(PERSIST SUBSCRIBE)
} GuildMember;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildSpeciesPerm
{
	REF_TO(SpeciesDef) hSpeciesRef;							AST(PERSIST SUBSCRIBE REFDICT(Species) NAME(Species))
} GuildSpeciesPerm;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildClassPerm
{
	CONST_STRING_POOLED pchClass;							AST(PERSIST SUBSCRIBE POOL_STRING)
} GuildClassPerm;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildCostume
{
	CONST_OPTIONAL_STRUCT(PlayerCostume) pCostume;			AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	CONST_EARRAY_OF(GuildSpeciesPerm) eaSpeciesNotAllowed;	AST(PERSIST SUBSCRIBE)
	CONST_INT_EARRAY eaRanksNotAllowed;						AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(GuildClassPerm) eaClassNotAllowed;		AST(PERSIST SUBSCRIBE)
} GuildCostume;

AUTO_ENUM;
typedef enum
{
	GuildEventReplyType_NoReply,
	GuildEventReplyType_Accept,
	GuildEventReplyType_Maybe,
	GuildEventReplyType_Refuse
} GuildEventReplyType;

AUTO_ENUM;
typedef enum
{
	GuildEventRecurType_Once = 			0,
	GuildEventRecurType_OneDay = 		1,
	GuildEventRecurType_TwoDays = 		2,
	GuildEventRecurType_ThreeDays = 	3,
	GuildEventRecurType_FourDays = 		4,
	GuildEventRecurType_OneWeek = 		7,
	GuildEventRecurType_TwoWeeks = 		14,
	GuildEventRecurType_ThreeWeeks = 	21,
	GuildEventRecurType_FourWeeks = 	28
} GuildEventRecurType;

#define MAX_GUILD_EVENTS 16
#define MAX_GUILD_EVENT_TITLE_LEN 64
#define MAX_GUILD_EVENT_DESC_LEN 2000
#define MAX_GUILD_EVENT_OWNED 3
#define MAX_GUILD_EVENT_LEN (DAYS(1) - MINUTES(1))
#define MAX_GUILD_EVENT_FUTURE_START DAYS(131)
#define MAX_GUILD_EVENT_RECURR DAYS(28)
#define MAX_GUILD_EVENT_MESSAGE_LEN 64
#define MAX_GUILD_EVENT_TIME_PAST_REPLY MINUTES(15)
#define MAX_GUILD_EVENT_FUTURE_REPLY DAYS(56)
#define MIN_GUILD_EVENT_FUTURE_REPLY DAYS(7)
#define MAX_GUILD_EVENT_FUTURE_REPLY_COUNT 4
#define MIN_GUILD_EVENT_TIME_PAST_REMOVE MINUTES(15)
#define MIN_GUILD_EVENT_TIME_PAST_REMOVE_REPLY MINUTES(25)

AUTO_STRUCT;
typedef struct GuildMapInvite
{
	char* pchGuildName;
	ContainerID uGuildID;
	ContainerID uMapID;		AST(SERVER_ONLY)
	U32 uPartitionID;		AST(SERVER_ONLY)
} GuildMapInvite;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildEventReply
{
	const U64 uiKey;									AST(PERSIST SUBSCRIBE KEY)

	const ContainerID iMemberID;						AST(PERSIST SUBSCRIBE)
	const GuildEventReplyType eGuildEventReplyType;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcReplyMessage;				AST(PERSIST SUBSCRIBE)
	const U32 iStartTime;								AST(PERSIST SUBSCRIBE) //If event recurs this is the start time the reply applies too
	U32 iLastRemindedTime;								AST(PERSIST NO_TRANSACT SUBSCRIBE)

	// Deprecated
	const U32 iReplyExpiresTime;						AST(PERSIST SUBSCRIBE)
	U32 iRemoved;										AST(PERSIST NO_TRANSACT SUBSCRIBE)
	// End Deprecated

} GuildEventReply;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildEventCanceledInfo
{
	const U32 iCanceledStartTime;						AST(PERSIST SUBSCRIBE)
		CONST_STRING_MODIFIABLE pcDescription;				AST(PERSIST SUBSCRIBE)
} GuildEventCanceledInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildEvent
{
	const U32 uiID;										AST(PERSIST SUBSCRIBE KEY)

	CONST_STRING_MODIFIABLE pcTitle;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcDescription;				AST(PERSIST SUBSCRIBE)

	const U32 iStartTimeTime;							AST(PERSIST SUBSCRIBE)
	const U32 iDuration;								AST(PERSIST SUBSCRIBE)
	const GuildEventRecurType eRecurType;				AST(PERSIST SUBSCRIBE)
	const S32 iRecurrenceCount;							AST(PERSIST SUBSCRIBE)
	const bool bCanceled;								AST(PERSIST SUBSCRIBE)

	const S32 iMinGuildRank;							AST(PERSIST SUBSCRIBE)
	const S32 iMinGuildEditRank;						AST(PERSIST SUBSCRIBE)
	const U32 iMinLevel;								AST(PERSIST SUBSCRIBE)
	const U32 iMaxLevel;								AST(PERSIST SUBSCRIBE)
	const U32 iMinAccepts;								AST(PERSIST SUBSCRIBE)
	const U32 iMaxAccepts;								AST(PERSIST SUBSCRIBE)

	const U32 iEventUpdated;							AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(GuildEventReply) eaReplies;			AST(PERSIST SUBSCRIBE)

	U32 bRecurInTransaction : 1;						AST_NOT(PERSIST) AST(NO_NETSEND NO_TEXT_SAVE)

	// Deprecated
	const ContainerID iEventOwnerID;					AST(PERSIST SUBSCRIBE)
	const ContainerID iAccountID;						AST(PERSIST SUBSCRIBE)
	const U32 iEndTimeTime;								AST(PERSIST SUBSCRIBE)
	U32 iRemoved;										AST(PERSIST NO_TRANSACT SUBSCRIBE)
	const U32 iDaysRecurring;							AST(PERSIST SUBSCRIBE) //Stored in seconds
	const U32 iRemoveTime;								AST(PERSIST SUBSCRIBE)
	U32 iCleanedCanceledTime;							AST(PERSIST NO_TRANSACT SUBSCRIBE)
	CONST_EARRAY_OF(GuildEventCanceledInfo) eaCanceledStartTime;	AST(PERSIST SUBSCRIBE)
	INT_EARRAY eaIDsOfPlayersNotified;					AST(PERSIST NO_TRANSACT SUBSCRIBE)
	// End Deprecated

} GuildEvent;

AUTO_STRUCT;
typedef struct GuildRecruitMember
{
	ContainerID iContainerID;
	U32 iLevel;
	S32 iOfficerRank;
	char *pcName;
	char *pcAccount;
	S32 iRank;
	char *pcRankName;
} GuildRecruitMember;

AUTO_STRUCT;
typedef struct GuildRecruitSearchCat
{
	const char *pcName;				AST(POOL_STRING)
} GuildRecruitSearchCat;

AUTO_STRUCT;
typedef struct GuildRecruitInfo
{
	ContainerID iContainerID;						AST(KEY)
	char *pcName;
	char *pcRecruitMessage;
	char *pcWebSite;

	const char *pcEmblem;						AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblemColor0;
	U32 iEmblemColor1;
	F32 fEmblemRotation; // [-PI, PI)
	const char *pcEmblem2;						AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	U32 iEmblem2Color0;
	U32 iEmblem2Color1;
	F32 fEmblem2Rotation; // [-PI, PI)
	F32 fEmblem2X; // -100 to 100
	F32 fEmblem2Y; // -100 to 100
	F32 fEmblem2ScaleX;							AST(DEF(1.0f)) // 0 to 100
	F32 fEmblem2ScaleY;							AST(DEF(1.0f)) // 0 to 100
	const char *pcEmblem3;						AST(POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary (Detail)

	U32 iColor1;
	U32 iColor2;

	const char *pcGuildAllegiance;				AST(POOL_STRING) // The guild allegiance

	GuildRecruitMember **eaMembers;

	int iMinLevelRecruit;
	GuildRecruitSearchCat **eaRecruitCat;
} GuildRecruitInfo;

AUTO_STRUCT;
typedef struct GuildRecruitInfoList
{
	GuildRecruitInfo **eaGuilds;
	F32 timeTaken;
	int cooldownEnd;
} GuildRecruitInfoList;

AUTO_STRUCT;
typedef struct GuildRecruitTagDef
{
	const char *pcName;				AST(POOL_STRING STRUCTPARAM NAME("Name"))
	REF_TO(Message) displayNameMsg;
	F32 fOrder;
} GuildRecruitTagDef;

AUTO_STRUCT;
typedef struct GuildRecruitCatDef
{
	const char *pcName;				AST(POOL_STRING STRUCTPARAM NAME("Name"))
	REF_TO(Message) displayNameMsg;
	F32 fOrder;
	GuildRecruitTagDef **eaGuildRecruitTagDef;	AST(NAME("Tag"))
} GuildRecruitCatDef;

AUTO_STRUCT;
typedef struct GuildRecruitParam
{
	U32 iMaxNumSearchResults;
	U32 iMaxNumSearchTraverse;

	GuildRecruitCatDef **eaGuildRecruitCatDef;	AST(NAME("Category"))
} GuildRecruitParam;

AUTO_STRUCT AST_CONTAINER;
typedef struct GuildRecruitCat
{
	CONST_STRING_POOLED pcName;		AST(PERSIST SUBSCRIBE POOL_STRING)
} GuildRecruitCat;

AUTO_STRUCT;
typedef struct GuildRecruitSearchRequest
{
	GuildRecruitSearchCat **eaGuildIncludeSearchCat;
	GuildRecruitSearchCat **eaGuildExcludeSearchCat;

	char *stringSearch; // String defining specific name to look for

	int iRequesterAccountID;
	int iRequesterEntID;
	int iRequesterLevel;
	ContainerID iRequesterVirtualShardID;
} GuildRecruitSearchRequest;

AUTO_STRUCT;
typedef struct GuildBankConfig
{
	U32 iLazySubscribeToBank;
	U32 iLazyCreateBank;

    // This is the name of the numeric currency that is used to buy guild bank slots.
    STRING_POOLED bankTabPurchaseNumericName;   AST(POOL_STRING)

    // The numeric on the guild bank container that unlocks tabs.
    STRING_POOLED bankTabUnlockNumericName;     AST(POOL_STRING)

    // This is an array containing the price of each successive guild bank bag.
    INT_EARRAY bankTabPurchaseCosts;            AST(NAME(TabCost))

} GuildBankConfig;


AUTO_STRUCT AST_CONTAINER;
typedef struct GuildBadEmblem
{
	CONST_STRING_POOLED pcEmblem;						AST(PERSIST SUBSCRIBE POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	const U32 iEmblemColor0;							AST(PERSIST SUBSCRIBE)
	const U32 iEmblemColor1;							AST(PERSIST SUBSCRIBE)
	const F32 fEmblemRotation;							AST(PERSIST SUBSCRIBE) // [-PI, PI)
	CONST_STRING_POOLED pcEmblem2;						AST(PERSIST SUBSCRIBE POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	const U32 iEmblem2Color0;							AST(PERSIST SUBSCRIBE)
	const U32 iEmblem2Color1;							AST(PERSIST SUBSCRIBE)
	const F32 fEmblem2Rotation;							AST(PERSIST SUBSCRIBE) // [-PI, PI)
	const F32 fEmblem2X;								AST(PERSIST SUBSCRIBE) // -100 to 100
	const F32 fEmblem2Y;								AST(PERSIST SUBSCRIBE) // -100 to 100
	const F32 fEmblem2ScaleX;							AST(PERSIST SUBSCRIBE DEF(1.0f)) // 0 to 100
	const F32 fEmblem2ScaleY;							AST(PERSIST SUBSCRIBE DEF(1.0f)) // 0 to 100
	CONST_STRING_POOLED pcEmblem3;						AST(PERSIST SUBSCRIBE POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary (Detail)
} GuildBadEmblem;

//Version 0: base
//Version 1: Same as Entity Fixup 6. InventoryV2 update.
//Version 2: Guild Event update
//Version 3: Indexed guild event reply update
#define CURRENT_GUILD_FIXUP_VERSION 3

AUTO_STRUCT AST_CONTAINER AST_IGNORE(bRecruitSilenced) AST_IGNORE(eaiReporterAccountIDs);
typedef struct Guild
{
	const ContainerID iContainerID;						AST(PERSIST SUBSCRIBE KEY)

	const ContainerID iVirtualShardID;					AST(PERSIST SUBSCRIBE)

	const int iFixupVersion;							AST(PERSIST SUBSCRIBE)

	const U32 iCreatedOn;								AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pcAllegiance;					AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_EARRAY_OF(GuildMember) eaMembers;				AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(GuildMember) eaInvites;				AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(GuildCustomRank) eaRanks;			AST(PERSIST SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(InventoryV1) pInventoryDeprecated;		AST(PERSIST SUBSCRIBE FORCE_CONTAINER ADDNAMES(pInventory))
	CONST_EARRAY_OF(GuildBankLogEntry) eaBankLog;		AST(PERSIST SUBSCRIBE SERVER_ONLY)

	const U32 uOldBankLogIdx;							AST(PERSIST SUBSCRIBE SERVER_ONLY)	// the next bank log that will be removed (oldest log entry)

	CONST_EARRAY_OF(GuildCostume) eaUniforms;			AST(PERSIST SUBSCRIBE)
	
	CONST_STRING_MODIFIABLE pcName;						AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcMotD;						AST(PERSIST SUBSCRIBE)
	const U32 iMotDUpdated;								AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcDescription;				AST(PERSIST SUBSCRIBE)

	CONST_STRING_POOLED pcEmblem;						AST(PERSIST SUBSCRIBE POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	const U32 iEmblemColor0;							AST(PERSIST SUBSCRIBE)
	const U32 iEmblemColor1;							AST(PERSIST SUBSCRIBE)
	const F32 fEmblemRotation;							AST(PERSIST SUBSCRIBE) // [-PI, PI)
	CONST_STRING_POOLED pcEmblem2;						AST(PERSIST SUBSCRIBE POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary
	const U32 iEmblem2Color0;							AST(PERSIST SUBSCRIBE)
	const U32 iEmblem2Color1;							AST(PERSIST SUBSCRIBE)
	const F32 fEmblem2Rotation;							AST(PERSIST SUBSCRIBE) // [-PI, PI)
	const F32 fEmblem2X;								AST(PERSIST SUBSCRIBE) // -100 to 100
	const F32 fEmblem2Y;								AST(PERSIST SUBSCRIBE) // -100 to 100
	const F32 fEmblem2ScaleX;							AST(PERSIST SUBSCRIBE DEF(1.0f)) // 0 to 100
	const F32 fEmblem2ScaleY;							AST(PERSIST SUBSCRIBE DEF(1.0f)) // 0 to 100
	CONST_STRING_POOLED pcEmblem3;						AST(PERSIST SUBSCRIBE POOL_STRING) // Name of a PCTextureDef in CostumeTexture dictionary (Detail)
	CONST_OPTIONAL_STRUCT(GuildBadEmblem) pGuildBadEmblem;		AST(PERSIST SUBSCRIBE SERVER_ONLY)

	const U32 iColor1;									AST(PERSIST SUBSCRIBE)
	const U32 iColor2;									AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(ActivityLogEntry) eaActivityEntries; AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	const U32 iNextActivityLogEntryID;					AST(PERSIST SUBSCRIBE)
	const U32 iLastGuildMailTime;						AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(GuildEvent) eaEvents;				AST(PERSIST SUBSCRIBE)
	const U32 iEventUpdated;							AST(PERSIST SUBSCRIBE)
	U32 iLastRemoveEventTime;							AST(SUBSCRIBE) //Only remove one event per second per guild to help prevent many remote transactions from being called all at once; People tend to choose common times to start and end their events
	const U32 iNextGuildEventIndex;						AST(PERSIST SUBSCRIBE)

	const U32 iVersion;									AST(PERSIST SUBSCRIBE)
	const U32 iOCVersion;								AST(PERSIST SUBSCRIBE) // Officer Comment specific versioning

	//Guild recruiting stuff
	CONST_STRING_MODIFIABLE pcRecruitMessage;			AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcWebSite;					AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(GuildRecruitCat) eaRecruitCat;		AST(PERSIST SUBSCRIBE)
	const int iMinLevelRecruit;							AST(PERSIST SUBSCRIBE)
	const bool bHideRecruitMessage;						AST(PERSIST SUBSCRIBE)
	const bool bHideMembers;							AST(PERSIST SUBSCRIBE)

	// The guild stats is stored in this struct
	CONST_OPTIONAL_STRUCT(GuildStatsInfo) pGuildStatsInfo; AST(PERSIST SUBSCRIBE)

	// The theme for the guild
	REF_TO(GuildThemeDef) hTheme;						AST(PERSIST SUBSCRIBE REFDICT(GuildThemeDef))

	// This is a guild that is owned by the server (its an automatic guild that players are placed into)
	const bool bIsOwnedBySystem;						AST(PERSIST SUBSCRIBE SELF_ONLY)

} Guild;

AUTO_STRUCT;
typedef struct GuildList
{
	U32 *eaiGuilds;
} GuildList;

extern GuildRankList g_GuildRanks;
extern GuildEmblemList g_GuildEmblems;
extern GuildConfig gGuildConfig;

extern DictionaryHandle g_GuildStatDictionary;
extern DictionaryHandle g_GuildThemeDictionary;

#define guild_InSameGuild(pEnt1, pEnt2) ((pEnt1) && (pEnt1)->pPlayer && (pEnt2) && (pEnt2)->pPlayer && (pEnt1)->pPlayer->iGuildID && (pEnt1)->pPlayer->iGuildID == (pEnt2)->pPlayer->iGuildID)

// These don't work on the client unless pEnt is yourself.
#define guild_WithGuild(pEnt) (pEnt && (pEnt)->pPlayer && (pEnt)->pPlayer->pGuild && (pEnt)->pPlayer->pGuild && (pEnt)->pPlayer->pGuild->iGuildID)
#define guild_IsMember(pEnt) (guild_WithGuild(pEnt) && (pEnt)->pPlayer->pGuild->eState == GuildState_Member)
#define guild_IsInvite(pEnt) (guild_WithGuild(pEnt) && (pEnt)->pPlayer->pGuild->eState == GuildState_Invitee)
#define guild_IsGuildMember(pEnt, pGuild) (guild_IsMember(pEnt) && (pEnt)->pPlayer->pGuild->iGuildID == pGuild->iContainerID)
#define guild_IsGuildInvite(pEnt, pGuild) (guild_IsInvite(pEnt) && (pEnt)->pPlayer->pGuild->iGuildID == pGuild->iContainerID)
#define guild_IsGuildIDMember(pEnt, iGuildID) (guild_IsMember(pEnt) && (pEnt)->pPlayer->pGuild->iGuildID == iGuildID)
#define guild_IsGuildIDInvite(pEnt, iGuildID) (guild_IsInvite(pEnt) && (pEnt)->pPlayer->pGuild->iGuildID == iGuildID)
#define guild_GetGuild(pEnt) (guild_WithGuild(pEnt) ? GET_REF((pEnt)->pPlayer->pGuild->hGuild) : NULL)
#define guild_GetGuildBank(pEnt) (guild_WithGuild(pEnt) ? GET_REF((pEnt)->pPlayer->pGuild->hGuildBank) : NULL)
#define guild_GetGuildID(pEnt) (guild_WithGuild(pEnt) ? (pEnt)->pPlayer->pGuild->iGuildID : 0)
#define guild_RankHasPermissions(iRank, eaRanks, ePermissions) (((iRank) >= 0 && (iRank) < eaSize(&(eaRanks))) ? (((eaRanks)[iRank]->ePerms & (ePermissions)) == (ePermissions)) : false)
#define guild_HasPermission(iRank, pGuild, ePermissions) guild_RankHasPermissions(iRank, (pGuild)->eaRanks, ePermissions)
#define guild_FindMember(pEnt) (guild_GetGuild(pEnt) ? eaIndexedGetUsingInt(&guild_GetGuild(pEnt)->eaMembers, (pEnt)->myContainerID) : NULL)
#define guild_FindInvite(pEnt) (guild_GetGuild(pEnt) ? eaIndexedGetUsingInt(&guild_GetGuild(pEnt)->eaInvites, (pEnt)->myContainerID) : NULL)
#define guild_FindMemberInGuild(pEnt, pGuild) ((pGuild) ? (eaIndexedGetUsingInt(&(pGuild)->eaMembers, (pEnt)->myContainerID)) : NULL)
#define guild_FindMemberInGuildEntID(iEntID, pGuild) ((pGuild) ? (eaIndexedGetUsingInt(&(pGuild)->eaMembers, (iEntID))) : NULL)

void guild_FixupRanks(GuildRankList* pRankList);

const char *guild_GetBankTabName(Entity *pEnt, InvBagIDs eBagID);

#ifdef GAMESERVER
void gslGuild_EntityUpdate(Entity *pEnt);
void gslGuild_HandlePlayerGuildChange(Entity *pEnt);
#endif

U32 guildevent_GetReplyCount(Guild *pGuild, GuildEvent *pGuildEvent, U32 iStartTime, GuildEventReplyType eReplyType);
U64 guildevent_GetReplyKey(U32 iMemberID, U32 iStartTime);

bool guild_CanClaimLeadership(GuildMember *pGuildMember, Guild *pGuild);

bool guild_AccountIsMember(Guild* pGuild, unsigned int iAccountID);

GuildRecruitParam *Guild_GetGuildRecruitParams(void);

// This is here so that the gameaction system can call this directly
enumTransactionOutcome aslGuild_tr_UpdateGuildStat(ATR_ARGS, NOCONST(Guild) *pGuildContainer, const char *pchStatName, S32 eOperation, S32 iValue);
