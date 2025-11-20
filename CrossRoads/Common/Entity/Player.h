#pragma once
GCC_SYSTEM
#include "entEnums.h"
#include "EntityInteraction.h"
#include "GlobalTypeEnum.h"
#include "referencesystem.h"
#include "UtilitiesLibEnums.h"
#include "XBoxStructs.h"
#include "chatCommon.h"
#include "WorldLibEnums.h"
#include "SocialCommon.h"
#include "itemupgrade.h"
#include "UGCProjectCommon.h"
#include "MailCommon.h"

typedef struct AIDebug AIDebug;
typedef struct ArmamentSwapInfo ArmamentSwapInfo;
typedef struct AttribValuePair AttribValuePair;
typedef struct BeaconDebugInfo BeaconDebugInfo;
typedef struct CalendarEvent CalendarEvent;
typedef struct CalendarRequest CalendarRequest;
typedef struct ChatConfig ChatConfig;
typedef struct ChatMailList ChatMailList;
typedef struct ChatState ChatState;
typedef struct ClientLink ClientLink;
typedef struct ControlSchemes ControlSchemes;
typedef struct CombatDebugPerfEvent CombatDebugPerfEvent;
typedef struct CritterDef CritterDef;
typedef struct CritterGroup CritterGroup;
typedef struct CutsceneDef CutsceneDef;
typedef struct EncounterDebug EncounterDebug;
typedef struct Entity Entity;
typedef struct GameAccountData GameAccountData;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Guild Guild;
typedef struct GuildMapInvite GuildMapInvite;
typedef struct InteractInfo InteractInfo;
typedef struct InteriorDef InteriorDef;
typedef struct InventorySlot InventorySlot;
typedef struct InventorySlotLite InventorySlotLite;
typedef struct InventorySlotV1 InventorySlotV1;
typedef struct ItemAssignmentPersistedData ItemAssignmentPersistedData;
typedef struct ItemAssignmentPlayerData ItemAssignmentPlayerData;
typedef struct Message Message;
typedef struct MapDescription MapDescription;
typedef struct MapRevealInfo MapRevealInfo;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct MinimapWaypoint MinimapWaypoint;
typedef struct MicroTransactionPurchase MicroTransactionPurchase;
typedef struct MicroTransactionRef MicroTransactionRef;
typedef struct MissionInfo MissionInfo;
typedef struct NotifySetting NotifySetting;
typedef struct PCMood PCMood;
typedef struct PendingCalendarRequest PendingCalendarRequest;
typedef struct PlayerCostume PlayerCostume;
typedef struct PlayerNemesisState PlayerNemesisState;
typedef struct PlayerQueueInstance PlayerQueueInstance;
typedef struct PlayerQueuePenaltyData PlayerQueuePenaltyData;
typedef struct PlayerStatsInfo PlayerStatsInfo;
typedef struct PowerDef PowerDef;
typedef struct QueueDef QueueDef;
typedef struct QueueFailRequirementsData QueueFailRequirementsData;
typedef struct QueueInstantiationInfo QueueInstantiationInfo;
typedef struct RewardModifier RewardModifier;
typedef struct SavedCartPower SavedCartPower;
typedef struct StashTableImp* StashTable;
typedef struct TradeBag TradeBag;
typedef struct WorldScope WorldScope;
typedef struct WorldVariable WorldVariable;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct PlayerCostumeHolder PlayerCostumeHolder;
typedef struct RemoteContact RemoteContact;
typedef struct InteriorInvite InteriorInvite;
typedef struct AccountProxyKeyValueInfo AccountProxyKeyValueInfo;
typedef struct LabGameString LabGameString;
typedef struct ProgressionInfo ProgressionInfo;
typedef struct NumericConversionState NumericConversionState;
typedef struct CurrencyExchangeAccountData CurrencyExchangeAccountData;
typedef struct RewardGatedTypeData RewardGatedTypeData;
typedef struct EventDef EventDef;
typedef struct ItemBuyBack ItemBuyBack;
typedef struct ItemUpgradeInfo ItemUpgradeInfo;
typedef struct GroupProjectContainer GroupProjectContainer;
typedef struct SimpleCpuData SimpleCpuData;
typedef struct EmailV3 EmailV3;

typedef U32 dtFx;
typedef U32 PlayerFlags; //what type to use for player flags
typedef int PlayerDifficultyIdx;

extern StaticDefineInt ControlSchemeRegionTypeEnum[];
extern StaticDefineInt QueueCannotUseReasonEnum[];
extern StaticDefineInt LFGDifficultyModeEnum[];

#define PLAYER_MIN_THROTTLE -0.25f
#define PLAYER_MAX_THROTTLE 1.0f

#define PLAYER_MACRO_COUNT_MAX 64
#define PLAYER_MACRO_LENGTH_MAX 256		// Max string length for macros
#define PLAYER_MACRO_DESC_LENGTH_MAX 32	// Max description length for macros

typedef struct EntityPresenceInfo
{
	U32 * perHidden;
	EntityRef erOwner;
	int iRefCount;
} EntityPresenceInfo;

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum GlobalPlayerFlags {
	PLAYERFLAG_NEW_CHARACTER = 1 << 0, //character was just created, and needs to be finalized
	
} GlobalPlayerFlags;

AUTO_ENUM;
typedef enum PlayerType {
	kPlayerType_None,
	kPlayerType_Standard,
	kPlayerType_Premium,
	kPlayerType_SuperPremium,				// used for characters that can log in to premium or standard accounts

	kPlayerType_LAST = kPlayerType_SuperPremium,  EIGNORE
} PlayerType;

AUTO_STRUCT AST_CONTAINER;
typedef struct ActivatedPlayerSpawn
{
	CONST_STRING_MODIFIABLE mapName;				AST(PERSIST POOL_STRING_DB)
	// TODO: This should be CONST_STRING_POOLED.  Not sure why that's not working
	CONST_STRING_MODIFIABLE spawnPointName;			AST(PERSIST POOL_STRING_DB)
} ActivatedPlayerSpawn;

AUTO_STRUCT AST_CONTAINER;
typedef struct GamePermissionNumerics
{
	// name of the numeric
	CONST_STRING_MODIFIABLE pchKey;						AST(PERSIST ESTRING NAME(Key) KEY)
	
	// value 
	const S32 iValue;									AST(PERSIST)
	
	const bool bIsNumeric;								AST(PERSIST)
	
}GamePermissionNumerics;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerAccountData
{
	const ContainerID iAccountID;						AST(PERSIST)
		// Same as player.accountID, used when setting the reference to the game account data
	
	const U32 iVersion;									AST(PERSIST)
		// The version of the game account data struct I last looked at
	
	CONST_EARRAY_OF(AttribValuePair) eaPendingKeys;		AST(PERSIST FORCE_CONTAINER)
		// Keys that are pending to be written to the GameAccountData structure.

	CONST_REF_TO(GameAccountData) hData;				AST(PERSIST COPYDICT(GameAccountData) FORMATSTRING(EXPORT_CONTAINER_TYPE = "GameAccountData"))
		// A Copy-Dict reference to the game account data struture

	REF_TO(GameAccountData) hTempData;					AST(COPYDICT(GameAccountData) NO_NETSEND)
		
	CONST_EARRAY_OF(GamePermissionNumerics) eaGamePermissionMaxValueNumerics;	AST(SELF_ONLY PERSIST)
		// This is a list of game permission numerics to speed up transactions (index earray, these are maximum values)

	U32 uExtractLastUpdated;							AST(NO_NETSEND)
		// This is when the extract was last updated

	GameAccountDataExtract *pExtract;					AST(NO_NETSEND)
		// This is a copy of key game account data used for local processing

	U32 bSteamLogin : 1;								AST(NO_NETSEND)
		// If this player logged into the game with a steam account
} PlayerAccountData;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerNemesisInfo
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)
	
	CONST_EARRAY_OF(PlayerNemesisState) eaNemesisStates;	AST(PERSIST FORCE_CONTAINER SUBSCRIBE)
} PlayerNemesisInfo;

AUTO_STRUCT;
typedef struct LogoutTimer
{
	LogoutTimerType eType;
	U32 expirationTime;
	U32 timeRemaining;		// Redundant, but helps the client stay in synch
} LogoutTimer;
extern ParseTable parse_LogoutTimer[];
#define TYPE_parse_LogoutTimer LogoutTimer

// Contains data necessary to warp to a specific location on a map (not a spawn point).  Can be saved
//  on a Player for custom "recall" behavior.
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerPowersWarpToData
{
	char *pchMap;		AST(PERSIST, NO_TRANSACT)
		// Name of map

	Vec3 vecPos;		AST(PERSIST, NO_TRANSACT)
		// Position on map
} PlayerPowersWarpToData;

// When attempting to warp to a target, this container stores the information needed to transfer you.
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerWarpToData
{
	ContainerID	iEntID;						AST(PERSIST, NO_TRANSACT)
		// The container ID of the entity they're attempting to warp to

	GlobalType eContType;					AST(PERSIST, NO_TRANSACT)
		// The container type of the entity you're warping to

	ContainerID	iTeamID;					AST(PERSIST, NO_TRANSACT)
		// The container ID of the entity's team.  These need to match to warp.

	U32 iAccountID;							AST(PERSIST, NO_TRANSACT)
		// The account id of the target they're attempting to warp to

	char *pchMap;							AST(PERSIST, NO_TRANSACT)
		// Name of the map the target is on

	STRING_POOLED pcMapVariables;			AST(PERSIST, POOL_STRING, NO_TRANSACT)
		// The map variables of the map the target entity is on

	ContainerID iMapID;						AST(PERSIST, NO_TRANSACT)
		// The container ID of the map the target is on

	U32 uPartitionID;						AST(PERSIST, NO_TRANSACT)
		// The partition ID of the map the target is on

	int iInstance;							AST(PERSIST, NO_TRANSACT)
		// The instance the target was on

	char *pchSpawn;							AST(PERSIST, NO_TRANSACT)
		// The spawn point to go to
	
	Vec3 vecTarget;							AST(PERSIST, NO_TRANSACT)
		// The location to spawn at

	U64 uiItemId;							AST(PERSIST, NO_TRANSACT)
		// The item ID to charge for this warp

	U32 iTimestamp;							AST(PERSIST, NO_TRANSACT)
		// The time stamp of when the warp was started

	char *pchAllegiance;					AST(PERSIST, NO_TRANSACT)
		// The faction name of the entity being warped

	U32 bRecruitWarp : 1;					AST(PERSIST, NO_TRANSACT)
		// The target and source must be recruiting related
	U32 bMustBeTeamed : 1;
		// The target and source must be teamed
	U32 bMustBeSameAllegiance : 1;				AST(PERSIST, NO_TRANSACT)
		// The target and source must be the same faction
} PlayerWarpToData;

// This is used to keep track of the player's current status with the guild
AUTO_ENUM;
typedef enum GuildState
{
	GuildState_Member,		// Part of the guild
	GuildState_Invitee,		// Invited to the guild
} GuildState;

AUTO_STRUCT;
typedef struct PlayerGuildLog
{
	char *pcLogEntry;
	U32 time;
} PlayerGuildLog;

AUTO_STRUCT;
typedef struct PlayerGuildOfficerComments
{
	ContainerID iEntID;
	char *pcOfficerComment;
	char *pcWhoOfficerComment;
	U32 iOfficerCommentTime;
} PlayerGuildOfficerComments;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerGuild
{
	// To test somebody's guild status, please use the macros in guild.h
	
	// Whether or not a character is associated with a guild is determined solely
	// by iGuildID. If iGuildID is zero, they aren't associated with a guild in any
	// way, and eState is meaningless. If iGuildID is non-zero, then eState
	// determines the nature of their relationship with the guild.
	
	const ContainerID iGuildID;				AST(PERSIST SUBSCRIBE)
		// Guild container ID
	
	REF_TO(Guild) hGuild;					AST(COPYDICT(GUILD))
		// Handle to the guild container
	
	REF_TO(Entity) hGuildBank;				AST(COPYDICT(EntityGuildBank)) //Non-persisted data

	U32 iTimeSinceHandleInit;				AST(NO_NETSEND)
		// Time since the handle was set, used to call validation routines if
		// the handle continues to resolve to NULL after a certain interval
	
	U32 iTimeSinceLastUpdate;				AST(NO_NETSEND)
		// Time since the player's info was last updated to the server, used to
		// make sure that the UpdateInfo transaction isn't called too often
	
	const GuildState eState;				AST(PERSIST SUBSCRIBE)
		// Specifies this player's relationship to the guild
	
	CONST_STRING_MODIFIABLE pcInviterName;	AST(PERSIST)
	CONST_STRING_MODIFIABLE pcInviterHandle;	AST(PERSIST)
		// The name of the player who invited this player to the guild
	
	U32 iVersion;							AST(NO_NETSEND)
		// The guild version, to be compared to the iVersion on the guild container
	
	bool bJoinedGuild;						AST(PERSIST NO_TRANSACT)
		// Set to true as soon as they join a guild, to keep track of whether to give
		// them an additional costume slot or not
	
	U32 iGuildChat;							AST(NO_NETSEND)
		// The guild container ID of the guild chat channel the player is in, or 0
		// if they aren't in a chat channel, used to make sure that the player is
		// logged in to the correct guild chat
	
	U32 iOfficerChat;						AST(NO_NETSEND)
		// The guild container ID of the officer chat channel the player is in, or 0
		// if they aren't in a chat channel, used to make sure that the player is
		// logged in to the correct officer chat
	
	F32 fLastUpdate;						AST(NO_NETSEND)
		// The amount of time, in seconds, since the guild data was last checked
	
	U32 uiDisplayedMotD;					AST(SERVER_ONLY PERSIST SUBSCRIBE NO_TRANSACT)
		// When the MotD was last displayed

	U32 uiDisplayedEvent;					AST(SERVER_ONLY PERSIST SUBSCRIBE NO_TRANSACT)
		// When an event change was last displayed

	PlayerGuildLog **eaBankLog;				// Player-local copy of the guild log, only populated when requested
	U32 iBankLogSize;						//The client will request parts of the bank log at a time but we need to know the full length available
	U32 iUpdated;							//The client knows the BankLog data is updated when this value changes

	PlayerCostumeHolder **eaGuildCostumes;	// Player-local copy of the guild uniforms available to the player, only populated when requested

	bool bUpdatedOfficerComments;			AST(NO_NETSEND)
	U32 iOCVersion;							AST(NO_NETSEND)
	PlayerGuildOfficerComments **eaOfficerComments;	//Officer comments that only gets sent to officers who have access

    REF_TO(GroupProjectContainer) hGroupProjectContainer; AST(COPYDICT(GroupProjectContainerGuild)) 
        // A non-persisted reference to the GroupProjectContainer for the player's guild.  Will only be filled in when needed.

	DirtyBit dirtyBit;						AST(NO_NETSEND)
} PlayerGuild;

// Microtransaction Information container
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerMTInfo
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)

	CONST_STRING_EARRAY eaOneTimePurchases;		AST(PERSIST)
		// Purchases that you can only get once per character that you have already received
		//  IE: Pre-order/Beta rewards

	CONST_EARRAY_OF(MicroTransactionPurchase) eaFirstPurchases;		AST(PERSIST FORCE_CONTAINER)
		// The first purchases this player made (for unique functional items)

} PlayerMTInfo;

AUTO_STRUCT;
typedef struct PlayerQueue
{
	const char *pchQueueName;			AST(POOL_STRING KEY)
	REF_TO(QueueDef) hDef;				AST(REFDICT(QueueDef))
	S32 eCannotUseReason;				AST(SUBTABLE(QueueCannotUseReasonEnum))
	PlayerQueueInstance** eaInstances;
} PlayerQueue;


AUTO_STRUCT;
typedef struct PlayerQueueInfo
{
	PlayerQueue **eaQueues;
		// The queues the player can enter or is in

	QueueFailRequirementsData** eaFailsAllReqs;	AST(SELF_ONLY)
		// The reason why the player cannot join any queues (QueueCannotUseReason)

	PlayerQueuePenaltyData* pPenaltyData;		AST(SELF_ONLY)
		// Timestamps indicating when a player will no longer have a leaver penalty applied for having previously left a queue match

	U32 uLeaverPenaltyDuration;					AST(SELF_ONLY)
		// If set, then a leaver penalty is enabled for the current map for the specified duration

	QueueInstantiationInfo* pQueueInstantiationInfo;			AST(SELF_ONLY)
		// If the player is attached to a queue instantiation, this info is pertinent

	bool bDataUpdateRequested;							NO_AST
		// The client (or some other part of the GameServer system) has requested that we update this players queue info
		// determine availability, get rid of unavailable queues, etc. Updated when the player has the UI open every a few times per minute

	U32 iLastDataProcessTime;						NO_AST
		// The last timestamp when we updated the queue datain  ProcessPlayerQueues.


	const char *pszAttemptQueueName;			NO_AST 
		// a queue to attempt to join next time the player queues are processed. Pooled String

} PlayerQueueInfo;

AUTO_ENUM;
typedef enum PlayerMapMoveType
{
	kPlayerMapMove_PowerPurchase,
	kPlayerMapMove_Permission,
	kPlayerMapMove_Warp,
} PlayerMapMoveType;

AUTO_ENUM;
typedef enum PlayerWhitelistFlags
{
	kPlayerWhitelistFlags_None = 0,
	kPlayerWhitelistFlags_Invites = (1 << 0), //This player has the invite whitelist turned on
	kPlayerWhitelistFlags_Trades = (1 << 1), //This player has the trade whitelist turned on
	kPlayerWhitelistFlags_PvPInvites = (1 << 2), //This player has the PvP invite whitelist turned on (duels or queue challenges)
} PlayerWhitelistFlags;

//Sent to the client to inform them about their upcoming map move they need to confirm
AUTO_STRUCT;
typedef struct PlayerMapMoveClient
{
	PlayerMapMoveType eType;

	MicroTransactionRef **ppMTRefs;
		//The microtransactions that can be purchased to allow you to mapmove

	REF_TO(Message) hDisplayName;
		// Display Name for the map they're traveling to

	char *pchRequestingEnt;
		//Who requested it

	U32 uiTimeStart;
		//When did this confirmation start

	U32 uiTimeToConfirm;
		// How long the user has to confirm.  0 for "off"

} PlayerMapMoveClient;

// Exact copy of all data used to trigger a normal mapmove, along with a confirmation flag.  Used
//  to indicate whether or not the player has confirmed they wish to make the transfer they have
//  requested.
// Also used to inform the client that they attempted to zone into a microtransacted zone.  The 
//  PlayerMapMoveClient structure above is passed to the client to show them how to 'fix' their
//  grievous mistake.
// Also used to confirm someone attempting to warp you into their map
AUTO_STRUCT;
typedef struct PlayerMapMoveConfirm
{
	PlayerMapMoveType eType;
	char *pcMapName;
	char *pcNamedSpawnPoint;
	char *pcQueueName;
	GlobalType eOwnerType;
	ContainerID uOwnerID;
	WorldVariable **eaVariables;
	WorldScope const *pScope;
		// Copy of parameters for spawnpoint_MovePlayerToMapAndSpawn

	PlayerWarpToData *pWarp;
		// A copy of the warp someone attempted on you
		//  Also used if you confirmed a warp and the zone happened to ALSO be microtransacted...

	U32 uiTimeStart;
		//When did this confirmation start

	U32 uiTimeToConfirm;
		// How long the user has to confirm.  0 for "off"

	U32 eFlags;
		//Transfer flags passed to map transfer code if needed
	
	U32 bConfirmed : 1;				NO_AST
		// Whether or not this move has been confirmed
} PlayerMapMoveConfirm;

// Used to store window/gen positions within the database.
AUTO_STRUCT AST_CONTAINER;
typedef struct UIPersistedPosition
{
	const char *pchName;					AST(KEY PERSIST NO_TRANSACT POOL_STRING_DB)
	S32 eOffsetFrom;						AST(PERSIST NO_TRANSACT)
	U16 iX;									AST(PERSIST NO_TRANSACT)
	U16 iY;									AST(PERSIST NO_TRANSACT)
	F32 fPercentX;							AST(PERSIST NO_TRANSACT)
	F32 fPercentY;							AST(PERSIST NO_TRANSACT)
	F32 fWidth;								AST(PERSIST NO_TRANSACT)
	F32 fHeight;							AST(PERSIST NO_TRANSACT)
	S32 iVersion;							AST(PERSIST NO_TRANSACT)
	U8 chPriority;							AST(PERSIST NO_TRANSACT)
	U32 uiTime;								AST(PERSIST NO_TRANSACT)
	const char *pchContents;				AST(PERSIST NO_TRANSACT)
} UIPersistedPosition;

//A name and column in percentage of it's parent's screenbox
AUTO_STRUCT AST_CONTAINER;
typedef struct UIPersistedColumn
{
	const char *pchColName;					AST(PERSIST NO_TRANSACT)
	F32 fPercentWidth;						AST(PERSIST NO_TRANSACT)
} UIPersistedColumn;

//Used the store the ordering and width of columns in UIGenLists
AUTO_STRUCT AST_CONTAINER;
typedef struct UIPersistedList
{
	const char *pchName;					AST(KEY PERSIST NO_TRANSACT)
	S32 iVersion;							AST(PERSIST NO_TRANSACT)
	U32 uiTime;								AST(PERSIST NO_TRANSACT)
	UIPersistedColumn **eaColumns;			AST(PERSIST NO_TRANSACT NO_INDEX)
} UIPersistedList;

AUTO_STRUCT AST_CONTAINER;
typedef struct UIPersistedWindow
{
	const char *pchName;					AST(KEY PERSIST NO_TRANSACT)
	U32 uiTime;								AST(PERSIST NO_TRANSACT)
	U32 bfWindows[8];						AST(PERSIST NO_TRANSACT)
} UIPersistedWindow;

// Indexed by key, but actually both key and command should be
// unique within an earray of these.
AUTO_STRUCT AST_CONTAINER AST_IGNORE(Regions);
typedef struct EntityKeyBind
{
	const char *pchKey;			AST(NAME(Key) PERSIST NO_TRANSACT)
	const char *pchCommand;		AST(NAME(Command) PERSIST NO_TRANSACT)
	
	S32 eSchemeRegions;			AST(NAME(SchemeRegions) FLAGS PERSIST NO_TRANSACT SUBTABLE(ControlSchemeRegionTypeEnum))
		// Deprecated
	
	S32 bSecondary : 1;			AST(NAME(Secondary) PERSIST NO_TRANSACT)
		// Whether or not this is a secondary key
} EntityKeyBind;

AUTO_STRUCT AST_CONTAINER;
typedef struct EntityKeyBinds
{
	const char *pchProfile;	AST(NAME(Profile) PERSIST NO_TRANSACT)
		// Valid if these binds are for a specific profile, NULL if bound to all profiles
	
	EntityKeyBind **eaBinds; AST(NAME(KeyBind) PERSIST NO_TRANSACT)
		// The list of binds for this profile
} EntityKeyBinds;

// Icon types to show on the minimap / map. In Player.h because players can
// customize what bits are in/off. Note that this means this data is persisted,
// albeit in the LooseUI structure.
AUTO_ENUM;
typedef enum MapIconInfoType
{
	MapIconInfoType_None				= 0,

	// Players and entities
	MapIconInfoType_Self				= 1 << 0,
	MapIconInfoType_Pet					= 1 << 1,
	MapIconInfoType_Team				= 1 << 2,
	MapIconInfoType_Guild				= 1 << 3,
	MapIconInfoType_Player				= 1 << 4,
	MapIconInfoType_Foe					= 1 << 5,
	MapIconInfoType_NPC					= 1 << 6,

	// All different kinds of contacts
	MapIconInfoType_Contact				= 1 << 7,
	MapIconInfoType_PowerTrainer		= 1 << 8,
	MapIconInfoType_CraftingTrainer		= 1 << 9,
	MapIconInfoType_GuildContact		= 1 << 10,
	MapIconInfoType_Vendor				= 1 << 11,
	MapIconInfoType_Bank				= 1 << 12,
	MapIconInfoType_SharedBank			= 1 << 13,
	MapIconInfoType_GuildBank			= 1 << 14,
	MapIconInfoType_Mail				= 1 << 15,
	MapIconInfoType_Tailor				= 1 << 16,
	MapIconInfoType_PvP					= 1 << 17,
	MapIconInfoType_Nemesis				= 1 << 18,
	MapIconInfoType_Omega				= 1 << 19,

	// Missions contacts
	MapIconInfoType_MissionContact				= 1 << 20,
	MapIconInfoType_MissionContactRepeatable	= 1 << 21,
	MapIconInfoType_MissionContactUnavailable	= 1 << 22,
	MapIconInfoType_MissionContactLowLevel		= 1 << 23,

	//	Mission waypoints
	MapIconInfoType_OpenMission			= 1 << 24,
	MapIconInfoType_MissionWaypoint		= 1 << 25,

	// Locations
	MapIconInfoType_Landmark			= 1 << 26,
	MapIconInfoType_Waypoint			= 1 << 27,
	MapIconInfoType_SavedWaypoint		= 1 << 28,

	// Misc
	MapIconInfoType_HarvestNode			= 1 << 29,
	MapIconInfoType_Camera				= 1 << 30,

	// possibly useful unions
	MapIconInfoType_PlayerCharacter = MapIconInfoType_Team | MapIconInfoType_Guild 
	| MapIconInfoType_Player,

	MapIconInfoType_AnyMissionContact = MapIconInfoType_MissionContact
	| MapIconInfoType_MissionContactRepeatable | MapIconInfoType_MissionContactUnavailable
	| MapIconInfoType_MissionContactLowLevel,

	MapIconInfoType_AnyContact = MapIconInfoType_Contact |MapIconInfoType_PowerTrainer 
	| MapIconInfoType_CraftingTrainer | MapIconInfoType_Vendor | MapIconInfoType_Bank 
	| MapIconInfoType_SharedBank | MapIconInfoType_GuildBank | MapIconInfoType_Tailor 
	| MapIconInfoType_PvP | MapIconInfoType_Nemesis | MapIconInfoType_Omega 
	| MapIconInfoType_AnyMissionContact,

	MapIconInfoType_Entity = MapIconInfoType_PlayerCharacter | MapIconInfoType_Foe
	| MapIconInfoType_AnyContact,

	MapIconInfoType_All = 0xFFFFFFFF
} MapIconInfoType;

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum OverHeadEntityFlags
{
	OVERHEAD_ENTITY_FLAG_NEVER = 0,
	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME	    = 1 << 0,
	OVERHEAD_ENTITY_FLAG_TARGETED_NAME		    = 1 << 1,
	OVERHEAD_ENTITY_FLAG_ALWAYS_NAME		    = 1 << 2,
	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE	    = 1 << 3,
	OVERHEAD_ENTITY_FLAG_TARGETED_LIFE		    = 1 << 4,
	OVERHEAD_ENTITY_FLAG_ALWAYS_LIFE		    = 1 << 5,
	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_RETICLE     = 1 << 6,
	OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE	    = 1 << 7,
	OVERHEAD_ENTITY_FLAG_ALWAYS_RETICLE		    = 1 << 8,
	OVERHEAD_ENTITY_FLAG_DAMAGED_NAME		    = 1 << 9,
	OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE		    = 1 << 10,
	OVERHEAD_ENTITY_FLAG_DAMAGED_RETICLE	    = 1 << 11,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_NAME    = 1 << 12,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_LIFE    = 1 << 13,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_RETICLE = 1 << 14,
	OVERHEAD_ENTITY_FLAG_ALWAYS_NAME_CONTACTS	= 1 << 15,

	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODENAME	= 1 << 16,
	OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODENAME		= 1 << 17,
	OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODENAME		= 1 << 18,
	OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODENAME		= 1 << 19,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODENAME	= 1 << 20,
	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODELIFE	= 1 << 21,
	OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODELIFE		= 1 << 22,
	OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODELIFE		= 1 << 23,
	OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODELIFE		= 1 << 24,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODELIFE	= 1 << 25,
	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODERETICLE	= 1 << 26,
	OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODERETICLE		= 1 << 27,
	OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODERETICLE		= 1 << 28,
	OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODERETICLE		= 1 << 29,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODERETICLE	= 1 << 30,

	OVERHEAD_ENTITY_FLAG_MOUSE_OVER = OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_RETICLE,
	OVERHEAD_ENTITY_FLAG_TARGETED = OVERHEAD_ENTITY_FLAG_TARGETED_NAME | OVERHEAD_ENTITY_FLAG_TARGETED_LIFE | OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE,
	OVERHEAD_ENTITY_FLAG_ALWAYS = OVERHEAD_ENTITY_FLAG_ALWAYS_NAME | OVERHEAD_ENTITY_FLAG_ALWAYS_LIFE | OVERHEAD_ENTITY_FLAG_ALWAYS_RETICLE,
	OVERHEAD_ENTITY_FLAG_DAMAGED = OVERHEAD_ENTITY_FLAG_DAMAGED_NAME | OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_RETICLE,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET = OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_NAME | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_LIFE | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_RETICLE,

	OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODE = OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODENAME | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODERETICLE,
	OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODE = OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODENAME | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODERETICLE,
	OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODE = OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODENAME | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODERETICLE,
	OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODE = OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODENAME | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODERETICLE,
	OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODE = OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODENAME | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODERETICLE,

	OVERHEAD_ENTITY_FLAG_ALL = OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME | OVERHEAD_ENTITY_FLAG_TARGETED_NAME | OVERHEAD_ENTITY_FLAG_ALWAYS_NAME | OVERHEAD_ENTITY_FLAG_DAMAGED_NAME | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_NAME |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE | OVERHEAD_ENTITY_FLAG_TARGETED_LIFE | OVERHEAD_ENTITY_FLAG_ALWAYS_LIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_LIFE |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_RETICLE | OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE | OVERHEAD_ENTITY_FLAG_ALWAYS_RETICLE | OVERHEAD_ENTITY_FLAG_DAMAGED_RETICLE | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_RETICLE |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODENAME | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODENAME | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODENAME | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODENAME |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODELIFE |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODERETICLE | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODERETICLE | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODERETICLE | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODERETICLE,

	OVERHEAD_ENTITY_FLAG_POWERMODEALL = 
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODENAME | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODENAME | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODENAME | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODENAME |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODELIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODELIFE |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODERETICLE | OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODERETICLE | OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODERETICLE | OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODERETICLE,

	OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS = 
		OVERHEAD_ENTITY_FLAG_ALWAYS_NAME |
		OVERHEAD_ENTITY_FLAG_TARGETED_LIFE |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE |
		OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE |
		OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE,
	
	OVERHEAD_ENTITY_FLAG_NPCDEFAULTS = 
		OVERHEAD_ENTITY_FLAG_TARGETED_NAME|
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME|
		OVERHEAD_ENTITY_FLAG_TARGETED_LIFE |
		OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE |
		OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE |
		OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE,

} OverHeadEntityFlags;

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum OverHeadEntityTypes
{
	OVERHEAD_ENTITY_TYPE_ENEMY = 0,
	OVERHEAD_ENTITY_TYPE_FRIENDLY_NPC,
	OVERHEAD_ENTITY_TYPE_FRIEND,
	OVERHEAD_ENTITY_TYPE_SUPERGROUP,
	OVERHEAD_ENTITY_TYPE_TEAM,
	OVERHEAD_ENTITY_TYPE_PET,
	OVERHEAD_ENTITY_TYPE_PLAYER,
	OVERHEAD_ENTITY_TYPE_ENEMY_PLAYER,
	OVERHEAD_ENTITY_TYPE_SELF,
	OVERHEAD_ENTITY_TYPE_COUNT

} OverHeadEntityTypes;

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum OverHeadReticleFlags
{
	OVERHEAD_RETICLE_HIGHLIGHT	= 1 << 0,
	OVERHEAD_RETICLE_BOX		= 2 << 0,

} OverHeadReticleFlags;

AUTO_STRUCT;
typedef struct PlayerShowOverhead
{
	OverHeadEntityFlags eShowEnemy;			AST(DEFAULT(OVERHEAD_ENTITY_FLAG_NPCDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowFriendlyNPC;	AST(DEFAULT(OVERHEAD_ENTITY_FLAG_NPCDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowFriends;		AST(DEFAULT(OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowTeam;			AST(DEFAULT(OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowSupergroup;	AST(DEFAULT(OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowPet;			AST(DEFAULT(OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowEnemyPlayer;	AST(DEFAULT(OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowPlayer;		AST(DEFAULT(OVERHEAD_ENTITY_FLAG_PLAYERDEFAULTS) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))
	OverHeadEntityFlags eShowSelf;			AST(DEFAULT(OVERHEAD_ENTITY_FLAG_NEVER) FLAGS SUBTABLE(OverHeadEntityFlagsEnum))

	bool bShowPlayerTitles;					AST(DEFAULT(true))
	bool bShowPlayerRoles;					AST(DEFAULT(true))
	bool bShowDamageFloaters;				AST(DEFAULT(true))
	bool bShowPetDamageFloaters;			AST(DEFAULT(true))
	bool bShowTeamDamageFloaters;			AST(DEFAULT(true))
	bool bShowAllPlayerDamageFloaters;		AST(DEFAULT(true))
	bool bShowOwnedEntityDamageFloaters;	AST(DEFAULT(true))
	bool bShowInteractionIcons;				AST(DEFAULT(true))
	bool bShowPlayerPowerDisplayNames;		AST(DEFAULT(true))
	bool bShowUnrelatedDamageFloaters;
	bool bShowHostileHealingFloaters;

	bool bDontShowPlayerOutgoing;		
	bool bDontShowPlayerIncoming;		
		// only obeyed if bShowDamageFloaters is set

	bool bShowCriticalStatusInfo;			AST(DEFAULT(true))
	OverHeadReticleFlags eShowReticlesAs;	AST(DEFAULT(OVERHEAD_RETICLE_HIGHLIGHT) FLAGS SUBTABLE(OverHeadReticleFlagsEnum))

} PlayerShowOverhead;

AUTO_ENUM;
typedef enum PlayerNotifyAudioMode
{
	PlayerNotifyAudioMode_Unset = -1,
	PlayerNotifyAudioMode_Off = 0,
	PlayerNotifyAudioMode_Standard = 1,
	PlayerNotifyAudioMode_Suggestion = 2,
} PlayerNotifyAudioMode;

AUTO_STRUCT;
typedef struct PlayerHUDOptions
{
	// The scheme region these settings apply to
	S32 eRegion;							AST(NAME(Region) DEFAULT(kControlSchemeRegionType_None) SUBTABLE(ControlSchemeRegionTypeEnum))
	// The audio notification mode
	PlayerNotifyAudioMode eNotifyAudioMode;	AST(NAME(NotifyAudioMode) DEFAULT(PlayerNotifyAudioMode_Standard))
	// What to show on the overhead	
	PlayerShowOverhead ShowOverhead;		AST(NAME(ShowOverhead))

	U8 uiTrayMode;							AST(NAME(TrayMode))
	U8 uiPowerLevelsMode;					AST(NAME(PowerLevelsMode))

	// Should tray elements show tooltips?
	U8 bHideTrayTooltips;					AST(NAME(HideTrayTooltips))
	
	// Map Icon flags
	MapIconInfoType eMapIconFlags;			AST(DEFAULT(MapIconInfoType_All))

	// The version of the settings
	S32 iVersion;
} PlayerHUDOptions;

AUTO_STRUCT;
typedef struct PlayerHUDOptionsPowerMode
{
	// The scheme region these settings apply to
	S32 eRegion;							AST(NAME(Region) DEFAULT(kControlSchemeRegionType_None) SUBTABLE(ControlSchemeRegionTypeEnum))

	// The power modes valid in this region
	INT_EARRAY eaiPowerModes;				AST(NAME(PowerMode) SUBTABLE(PowerModeEnum))

	// The entity types that should be filtered with the power modes
	INT_EARRAY eaiEnableTypes;				AST(NAME(EnableType) SUBTABLE(OverHeadEntityTypesEnum))

	// Enable the PowerMode options for the entity name
	bool bEnableName;						AST(NAME(EnableName))

	// Enable the PowerMode options for the entity life
	bool bEnableLife;						AST(NAME(EnableLife))

	// Enable the PowerMode options for the entity reticle
	bool bEnableReticle;					AST(NAME(EnableReticle))
} PlayerHUDOptionsPowerMode;

AUTO_STRUCT;
typedef struct PlayerHUDOptionsStruct
{
	EARRAY_OF(PlayerHUDOptions) eaHUDOptions; AST(NAME(HUDOptions))
	EARRAY_OF(PlayerHUDOptionsPowerMode) eaPowerModeOptions; AST(NAME(PowerModeOptions))
} PlayerHUDOptionsStruct;

AUTO_STRUCT;
typedef struct PlayerPetPersistedOrder
{
	// The slot number the command is in
	S32 iSlot;												AST(KEY)

	// The "command" to execute
	const char *pchCommand;									AST(POOL_STRING)

	// The last modified timestamp
	U32 uTime;

	// If set, then the command represents a power
	U32 bPower : 1;

	// If set, then the command represents an AI state
	U32 bAiState : 1;
} PlayerPetPersistedOrder;

#define MAX_PLAYER_PET_PERSISTED_ORDER 15

// Stored information for the UI that should not cause an error even if we
// can't load it because the schema format changed. Note that everything
// in this structure is "persisted" - transient data should go in PlayerUI.
AUTO_STRUCT;
typedef struct PlayerLooseUI
{
	EARRAY_OF(SavedCartPower) ppSavedCartPowers;
		//saved powers in the power cart list

	// The list of open windows
	UIPersistedWindow **eaPersistedWindows;				AST(NAME(PersistedWindow))

	// A flat space separated string of TeamMemberPlayingStyle enum names
	char *pchPlayingStyles;

	//HUD Options that are saved per region
	EARRAY_OF(PlayerHUDOptions) eaHUDOptions;			AST(NAME(HUDOptions))

	// The pet tray order information
	EARRAY_OF(PlayerPetPersistedOrder) eaPetCommandOrder; AST(NAME(PetCommandOrder))

	// Whether or not the tray is locked
	U32 bLockTray : 1;
	// Should we not automatically send the user to "best" map for them?
	U32 bShowMapChoice : 1;								AST(NAME(ShowMapChoice))
	// If set, this will disable the bulletin window from automatically showing up on the HUD

	// Should we prompt the user to sidekick?
	U32 bAutoSidekick: 1;								AST(NAME(AutoSideKick) DEFAULT(1))

	// STO-Specific Options
	U32 bShowAstrometrics : 1;							AST(NAME(ShowAstrometrics) DEFAULT(1))
	U32 bShowDistantSystems : 1;						AST(NAME(ShowDistantSystems) DEFAULT(1))
	U32 bShowNearSystems : 1;							AST(NAME(ShowNearSystems) DEFAULT(1))
	U32 bShowNearSystemTooltips : 1;					AST(NAME(ShowNearSystemTooltips) DEFAULT(1))

	//These fields have moved to PlayerHUDOptions
	PlayerNotifyAudioMode eNotifyAudioMode_Obsolete;	AST(NAME(eNotifyAudioMode) DEFAULT(PlayerNotifyAudioMode_Standard))
	PlayerShowOverhead* pShowOverhead_Obsolete;			AST(NAME(ShowOverhead))
	U8 uiTrayMode_Obsolete;								AST(NAME(uiTrayMode))
	U8 uiPowerLevelsMode_Obsolete;						AST(NAME(uiPowerLevelsMode))
} PlayerLooseUI;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerUIPair
{
	const char *pchKey;		AST(KEY PERSIST NO_TRANSACT)
	char *pchValue;			AST(PERSIST NO_TRANSACT)
	U32 uiTime;				AST(PERSIST NO_TRANSACT)
} PlayerUIPair;

// Information stored for player-defined macros
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerMacro
{
	U32 uMacroID; AST(NAME(MacroID) PERSIST NO_TRANSACT KEY)
		// Unique ID given to this macro

	char* pchMacro; AST(NAME(Macro) PERSIST NO_TRANSACT)
		// The macro string
	
	char* pchDescription; AST(NAME(Desc) PERSIST NO_TRANSACT)
		// A short description of the macro
	
	const char* pchIcon; AST(NAME(Icon) PERSIST NO_TRANSACT POOL_STRING)
		// The icon to display for this macro
} PlayerMacro;

// Stored UI information for a player. Persisted.
// Things marked with TOK_USEROPTIONBIT_2 are not saved when /ui_save is called
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerUIMapRegionScale
{
	WorldRegionType eType; AST(KEY STRUCTPARAM REQUIRED PERSIST NO_TRANSACT)
	F32 fScale; AST(STRUCTPARAM REQUIRED PERSIST NO_TRANSACT)
} PlayerUIMapRegionScale;

// Stored UI information for a player. Persisted.
// Things marked with TOK_USEROPTIONBIT_2 are not saved when /ui_save is called
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerUI
{
	UIPersistedPosition **eaStoredPositions;		AST(PERSIST NO_TRANSACT)
	UIPersistedList **eaStoredLists;				AST(PERSIST NO_TRANSACT)
	PlayerUIPair **eaPairs;							AST(PERSIST NO_TRANSACT)

	int ePowerTooltipDetail;						AST(PERSIST NO_TRANSACT SUBTABLE(AutoDescDetailEnum))
	int ePowerInspectDetail;						AST(PERSIST NO_TRANSACT SUBTABLE(AutoDescDetailEnum))
	ControlSchemes *pSchemes;						AST(PERSIST NO_TRANSACT SELF_ONLY FORCE_CONTAINER ALWAYS_ALLOC)
	MapRevealInfo **eaMapRevealInfos;				AST(PERSIST NO_TRANSACT NAME(MapRevealInfo) FORCE_CONTAINER USERFLAG(TOK_USEROPTIONBIT_2))
	Vec3 vLastMapRevealPos;							NO_AST // Tracks position of last map reveal on this map

	EARRAY_OF(PlayerMacro) eaMacros;				AST(PERSIST NO_TRANSACT)
		// A list of player-defined macros
	U32 uMacroIDMax;								AST(PERSIST NO_TRANSACT)
		// The last macro ID created

	// Notification settings for the player
	EARRAY_OF(NotifySetting) eaNotifySettings;		AST(NAME(NotifySetting) PERSIST NO_TRANSACT FORCE_CONTAINER)
	S32 iNotifySettingVersion;						AST(SELF_ONLY)

	// Canonical copy of this data is on the mail server, the client just gets a cache of it.
	ChatMailList *pMailList;						AST(CLIENT_ONLY USERFLAG(TOK_USEROPTIONBIT_2))
	// Value the server may use to tell the client that there is new mail on the server's copy of the mailList.
	bool bUnreadMail;								AST(CLIENT_ONLY USERFLAG(TOK_USEROPTIONBIT_2))

	ChatConfig *pChatConfig;						AST(PERSIST NO_TRANSACT FORCE_CONTAINER)
	ChatState *pChatState;							AST(ALWAYS_ALLOC USERFLAG(TOK_USEROPTIONBIT_2))

	EntityKeyBinds **eaBindProfiles;				AST(NAME(Binds) PERSIST NO_TRANSACT)

	// The client should periodically poke the server so we know when someone goes AFK.
	U32 uiLastClientPoke;							AST(SERVER_ONLY USERFLAG(TOK_USEROPTIONBIT_2))
	bool bIdleKickWarned;							AST(SERVER_ONLY USERFLAG(TOK_USEROPTIONBIT_2))

	PlayerLooseUI *pLooseUI;						AST(ALWAYS_ALLOC)
	char *pchLooseUI;								AST(PERSIST NO_TRANSACT ESTRING USERFLAG(TOK_USEROPTIONBIT_2))

	// The time last emote used by the player, so we don't send the same chat text
	// over and over for the same emote used several times in a row.
	const char *pchLastEmote;						AST(POOL_STRING SERVER_ONLY USERFLAG(TOK_USEROPTIONBIT_2))
	U32 uiLastEmoteTime;							AST(SERVER_ONLY USERFLAG(TOK_USEROPTIONBIT_2))
	EntityRef hLastEmoteTarget;						AST(SERVER_ONLY USERFLAG(TOK_USEROPTIONBIT_2))

	WorldRegionType	eLastRegion;					AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_2))
		// The last region the player was in. This + pEnt->eCurRegion is used
		// to show specific UI when switching between region types.

	PlayerUIMapRegionScale **eaRegionScales;		AST(PERSIST NO_TRANSACT)

	bool bGoldenPathActive;							AST(PERSIST NO_TRANSACT DEFAULT(1))

	bool bDisallowGuildInvites;						AST(PERSIST NO_TRANSACT USERFLAG(TOK_USEROPTIONBIT_2))

	DirtyBit dirtyBit;								AST(NO_NETSEND USERFLAG(TOK_USEROPTIONBIT_2))

	U8 uiLastLevelPowersMenuAt;						AST(PERSIST NO_TRANSACT DEFAULT(1))
	U8 uiLevelUpWizardDismissedAt;					AST(PERSIST NO_TRANSACT DEFAULT(1))

} PlayerUI;

//Lighter weight, non-persisted version of CooldownTimer
AUTO_STRUCT;
typedef struct PetCooldownTimer
{
	S32 iPowerCategory;
		// The power category defined by PowerCategoriesEnum

	F32 fCooldown;
		// The cooldown time value
} PetCooldownTimer;

extern ParseTable parse_PetCooldownTimer[];
#define TYPE_parse_PetCooldownTimer PetCooldownTimer

// Tracks data about the current state of a Power owned by a pet that is visible to the Player that
//  owns the pet
AUTO_STRUCT;
typedef struct PetPowerState
{
	REF_TO(PowerDef) hdef;
		// Reference to the PowerDef in question

	F32 fTimerRecharge;			AST(CLIENT_ONLY)
		// Current recharge timer.  Client decrements this in its own tick based on the
		//  fAttribSpeedRecharge in the PlayerPetInfo.

	F32 fTimerRechargeBase;		AST(CLIENT_ONLY)
		// The base recharge timer.  Set once in PowerPetSetRechargeClient, then cleared
		//  when fTimerRecharge reaches 0.

	U32 bQueuedForUse : 1;
		// If the AI has acknowledged that this power is being queued for use

	U32 bAIUsageDisabled : 1;
		// If the AI is not allowed to use this Power on its own (can still be executed by
		//  request from the client)

	U32 bResetDirty : 1;		NO_AST
} PetPowerState;

typedef struct PlayerPetRallyPoint
{
	EntityRef erPet;
	Vec3 vPosition;
	dtFx hRallyPointFx;
} PlayerPetRallyPoint;

AUTO_STRUCT;
typedef struct PetCommandNameInfo
{
	const char* pchName;								AST(POOL_STRING)
	REF_TO(Message) pchDisplayName;
	const char* pchIcon;								AST(POOL_STRING RESOURCEDICT(Texture))

	// Hide command in UI
	bool bHidden;
} PetCommandNameInfo;

AUTO_STRUCT;
typedef struct PetStanceInfo
{
	const char* curStance;								AST(POOL_STRING)
	PetCommandNameInfo** validStances;					
} PetStanceInfo;

// Tracks client-visible data about a particular pet of a Player, propagated down to the client
//  during standard netsend.
AUTO_STRUCT;
typedef struct PlayerPetInfo
{
	int iPartitionIdx;
	EntityRef iPetRef;
		// EntityRef for the pet

	const char* curPetState;							AST(POOL_STRING)
	PetCommandNameInfo** validStates;

	PetStanceInfo** eaStances;

	PetPowerState **ppPowerStates;
		// Powers the pet has with useful state information

	PetCooldownTimer** ppCooldownTimers;
		// Array of cooldown timers for the pet.

	F32 fAttribSpeedRecharge;
		// Copy of the pet's current fSpeedRecharge attribute, so the Player can properly run
		//  a recharge tick on their copy of the PetPowerStates

	F32 fAttribSpeedCooldown;
		// Copy of the pet's current fSpeedCooldown attribute, so the Player can properly run
		//  a recharge tick on their copy of the PetPowerStates

} PlayerPetInfo;

AUTO_STRUCT;
typedef struct CSRListenerInfo
{
	GlobalType listenerType;
	ContainerID listenerID;
	char *listenerName;
	char *listenerAccount;
	U32 uValidUntilTime;   // Hack - the CSR rep will get everything from this player until this time has expired
	AccessLevel listenerAccessLevel;
} CSRListenerInfo;
extern ParseTable parse_CSRListenerInfo[];
#define TYPE_parse_CSRListenerInfo CSRListenerInfo

AUTO_STRUCT;
typedef struct PlayerDebug
{
	// If the player is viewing combat debug info, who they're looking at is here
	EntityRef erCombatDebug;						NO_AST

	// Combat event perf data
	CombatDebugPerfEvent **ppCombatEvents;

	// Encounter debugging information, only filled out if the player enables it
	EncounterDebug* encDebug;

	// If showServerFPS is true, currServerFPS will be updated with the current server fps
	bool showServerFPS;
	F32 currServerFPS;
	U32 clientsLoggedInCount;
	U32 clientsNotLoggedInCount;

	// Number of spawned entities, active encounters, and total encounters, for the "budget" display
	U32 numSpawnedEnts;
	U32 numRunningEncs;
	U32 numTotalEncs;
	U32 spawnedFSMCost;
	U32 potentialFSMCost;

	// If the player is allowed to repeat missions they've already completed
	bool canRepeatMissions;

	// All AI debug info being sent to this player
	AIDebug* aiDebugInfo;

	// Beacon info sent to/from server
	BeaconDebugInfo *bcnDebugInfo;

	U32 showPathPerfCmds : 1;
	U32 showPathPerfLiving : 1;
	U32 showPhysicsPerfMenu : 1;
	U32 combatDebugPerf : 1;
	U32 showMissionDebugMenu : 1;
	U32 allowAllInteractions : 1;
} PlayerDebug;

AUTO_ENUM;
typedef enum NPCEmailType
{
	kNPCEmailType_Default = 0,
	kNPCEmailType_ExpiredAuction
}NPCEmailType;

// The npc mail structure, created to make sure that the items passed from NPCs to player are transaction safe.
AUTO_STRUCT AST_CONTAINER;
typedef struct NPCEMailData
{
	const S32 iNPCEMailID;														AST(PERSIST SUBSCRIBE)	// npc email ID
	const U32 sentTime;															AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE fromName;											AST(ESTRING PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE subject;											AST(ESTRING PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE body;												AST(ESTRING PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(InventorySlot) ppItemSlot;									AST(PERSIST NO_INDEX FORCE_CONTAINER SUBSCRIBE)
	const bool bRead;															AST( PERSIST SUBSCRIBE)
	const NPCEmailType	eType;													AST( PERSIST SUBSCRIBE)
} NPCEMailData;

AUTO_STRUCT AST_CONTAINER;
typedef struct NPCEMail
{
	const S32 iLastUsedID;														AST( PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(NPCEMailData) mail;											AST( PERSIST SUBSCRIBE )
	U32 uLastSyncTime;															AST( PERSIST NO_TRANSACT SUBSCRIBE )
	bool bReadAll;																AST( PERSIST NO_TRANSACT SUBSCRIBE )
} NPCEMail;

// The npc mail structure, created to make sure that the items passed from NPCs to player are transaction safe.
AUTO_STRUCT AST_CONTAINER;
typedef struct NPCEMailDataV1
{
	const S32 iNPCEMailID;														AST(PERSIST)	// npc email ID
	const U32 sentTime;															AST(PERSIST)
	CONST_STRING_MODIFIABLE fromName;											AST(ESTRING PERSIST)
	CONST_STRING_MODIFIABLE subject;											AST(ESTRING PERSIST)
	CONST_STRING_MODIFIABLE body;												AST(ESTRING PERSIST)
	CONST_EARRAY_OF(InventorySlotV1) ppItemSlot;									AST(PERSIST NO_INDEX FORCE_CONTAINER)
	const bool bRead;															AST( PERSIST )
} NPCEMailDataV1;

AUTO_STRUCT AST_CONTAINER;
typedef struct NPCEMailV1
{
	const S32 iLastUsedID;														AST( PERSIST )
	CONST_EARRAY_OF(NPCEMailDataV1) mail;											AST( PERSIST )
	U32 uLastSyncTime;															AST( PERSIST NO_TRANSACT )
	bool bReadAll;																AST( PERSIST NO_TRANSACT )
} NPCEMailV1;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerActivityEntry
{
	CONST_STRING_POOLED service;	AST(PERSIST POOL_STRING)
	CONST_STRING_POOLED type;		AST(PERSIST POOL_STRING)
} PlayerActivityEntry;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerActivityEnrollment
{
	CONST_STRING_POOLED service;		AST(PERSIST POOL_STRING)
	const U32 state;					AST(PERSIST)
	CONST_STRING_MODIFIABLE userdata;	AST(PERSIST ESTRING)
} PlayerActivityEnrollment;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerActivityVerbosity
{
	CONST_STRING_POOLED service;		AST(PERSIST POOL_STRING)
	const ActivityVerbosity level;		AST(PERSIST)
} PlayerActivityVerbosity;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerActivity
{
	CONST_EARRAY_OF(PlayerActivityEntry) disabled;			AST(PERSIST)
	CONST_EARRAY_OF(PlayerActivityEnrollment) enrollment;	AST(PERSIST)
	CONST_EARRAY_OF(PlayerActivityVerbosity) verbosity;		AST(PERSIST)
} PlayerActivity;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerVisitedMap
{
	CONST_STRING_POOLED pchMapName;		AST( PERSIST POOL_STRING KEY )
	CONST_STRING_EARRAY eaMapVariables;	AST( PERSIST ) // Not pooled strings
} PlayerVisitedMap;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerVisitedMaps
{
	// This dirty bit is never set, because the visited maps are now only updated as part of the container move transaction when
	// the player logs in to a gameserver, which happens before the first entity send to the client.  Having it here will reduce 
	// the cost of diffing the player struct by not comparing the visited maps(which could be large) during the diff.
	DirtyBit dirtyBit;					AST( NO_NETSEND )
	CONST_EARRAY_OF(PlayerVisitedMap) eaMaps; AST( PERSIST )
} PlayerVisitedMaps;

AUTO_STRUCT AST_CONTAINER;
typedef struct InteriorRef
{
	REF_TO(InteriorDef) hDef; AST(PERSIST)
} InteriorRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct DaboBet
{
	const S64 iBetIndex;			AST(NAME(BetIndex,uSpinIndex) PERSIST KEY)
	CONST_STRING_POOLED pchNumeric;	AST(PERSIST POOL_STRING)
	const int iBetAmount;			AST(PERSIST)
} DaboBet;

AUTO_STRUCT;
typedef struct LabGameEntData
{
	LabGameString *pLabGameString;	AST(LATEBIND)

	int *eaiPoints;
	U32 iLastUnit;
	U32 iTimer;						AST(SERVER_ONLY)
} LabGameEntData;

AUTO_STRUCT AST_CONTAINER; 
typedef struct MinigameData		//Persisted minigame data
{
	CONST_EARRAY_OF(DaboBet) eaDaboBets; AST(NAME(DaboBets,pDaboBet) PERSIST)
} MinigameData;

AUTO_STRUCT;
typedef struct TempMinigameData //Non-persisted minigame data
{
	LabGameEntData *pLabGameEntData;
} TempMinigameData;

AUTO_STRUCT;
typedef struct PlayerEntCopyForInfo
{
	ContainerID	myContainerID;
	GlobalType	myEntityType;
	REF_TO(Entity) hEnt;				AST(COPYDICT(ENTITYPLAYER))
} PlayerEntCopyForInfo;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerAuctionData
{
	// The auctions player bid on. We might want to put a limit on the number of auction IDs we store here.
	CONST_CONTAINERID_EARRAY eaiAuctionsBid;		AST(PERSIST SELF_ONLY SUBSCRIBE)
} PlayerAuctionData;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerUGCKillCreditLimit
{
	const S32 iExpEarned; AST(PERSIST SERVER_ONLY)
		// The amount of exp earned for the time interval
	const U32 uTimestamp; AST(PERSIST SERVER_ONLY)
		// The timestamp that was first used for the time interval
} PlayerUGCKillCreditLimit;

// Tracks cooldowns for queues
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerQueueCooldown
{
	const char* pchCooldownDef; AST(POOL_STRING KEY PERSIST NO_TRANSACT SELF_ONLY)
	U32 uStartTime;				AST(PERSIST NO_TRANSACT SELF_ONLY)
} PlayerQueueCooldown;

// Event information for the player
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerSubscribedEvent
{
	// The event
	REF_TO(EventDef) hEvent;			AST(KEY PERSIST NO_TRANSACT SELF_ONLY NAME("Event"))

	// The start time and end times for the subscribed event
	// We store this to know which event instance the player is subscribed to
	U32 uEventStartTime;				AST(PERSIST NO_TRANSACT SELF_ONLY NAME("EventStartTime"))
	U32 uEventEndTime;					AST(PERSIST NO_TRANSACT SELF_ONLY NAME("EventEndTime"))

	// Set to true when we send the pre-event notification to the player
	bool bPreEventNotificationSent;		AST(PERSIST NO_TRANSACT SERVER_ONLY)
} PlayerSubscribedEvent;
extern ParseTable parse_PlayerSubscribedEvent[];
#define TYPE_parse_PlayerSubscribedEvent PlayerSubscribedEvent

// Event information for the player
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerEventInfo
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)

	// The last time the player requested events
	U32 uLastRequestTime;						AST(SERVER_ONLY)

	// The lower time bound for calendar requests
	U32 uRequestStartDate;						AST(SELF_ONLY)

	// The upper time bound for calendar requests 
	U32 uRequestEndDate;						AST(SELF_ONLY)

	// List of events requested by the player
	CalendarEvent **eaRequestedEvents;			AST(SELF_ONLY)

	// List of currently active events
	CalendarEvent **eaActiveEvents;				AST(SELF_ONLY)

	CalendarRequest *pServerCalendar;			AST(SELF_ONLY)

	// List of pending event requests
	PendingCalendarRequest **eaPendingRequests; AST(SERVER_ONLY)

	// The list of events which the player subscribed to.
	PlayerSubscribedEvent **eaSubscribedEvents;	AST(PERSIST NO_TRANSACT SELF_ONLY)
} PlayerEventInfo;
extern ParseTable parse_PlayerEventInfo[];
#define TYPE_parse_PlayerEventInfo PlayerEventInfo

AUTO_STRUCT AST_CONTAINER;
typedef struct GatewayInfo
{
	const bool bHidden : 1;  AST(PERSIST SUBSCRIBE)
} GatewayInfo;


AUTO_STRUCT AST_CONTAINER AST_IGNORE(eaAwayTeamPetIDs) AST_IGNORE(eaAwayTeamCritterIDs) AST_IGNORE(bNotLookingForGroup) AST_IGNORE(bAnonymous) AST_IGNORE(uPreviousBulletinTime) AST_IGNORE_STRUCT(pUGCSubscription);
typedef struct Player
{
	// Basic Player information	

	const AccessLevel		accessLevel;				AST(PERSIST SUBSCRIBE SELF_ONLY)
	const AccessLevel		accountAccessLevel;			AST(PERSIST SUBSCRIBE SELF_ONLY)
		// The access level of a player, which is used for command processing
		 
	const char				privateAccountName[MAX_NAME_LEN];	AST(PERSIST SELF_ONLY)
	const char				publicAccountName[MAX_NAME_LEN];	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
	const U32				accountID;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// The public and private account names and account ID that correspond to this player. 

	const int				loginCookie;				AST(PERSIST SELF_ONLY)
		// A cookie that is used to verify player transfers
	const int				langID;						AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE)
		// The language the player logged in with, needs to be available to read in transactions, never write to it during a transaction
	const PlayerFlags		playerFlags;				AST(PERSIST FLAGS SUBTABLE(GlobalPlayerFlagsEnum) SERVER_ONLY)
		// Various entity flags

	const PlayerType		playerType;					AST(PERSIST DEFAULT(kPlayerType_Premium))
		//The type of player this is (Standard, premium etc).  Used for free-2-play

	const U8 uiRespecConversions;						AST(PERSIST SELF_ONLY)
		// The number of times this character has abused the free to play conversion system to get free respecs
		//  Used in premium to standard or standard to premium player type conversions
	
	const U32				iCreatedTime;				AST(PERSIST SELF_ONLY FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
		// The time at which this player character was created

	U32						iLastPlayedTime;			AST(PERSIST SUBSCRIBE NO_TRANSACT SERVER_ONLY)
		// Time this character was last touched. Currently used for objectdb loading

	const float				fTotalPlayTime;				AST(PERSIST SUBSCRIBE NO_TRANSACT SERVER_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
		// Seconds

	U32						uRecentBulletinTime;		AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// The most recent time and the previous time this player received bulletins

	S32						iKillCreditCounter;			AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// Keeps track of how many kills the player is allowed before ceasing to give kill credit
	
	F32						fKillCreditLimitAccum;		NO_AST
		// Accumulator for kill credit limit ticks

	CONST_OPTIONAL_STRUCT(PlayerUGCKillCreditLimit) pUGCKillCreditLimit; AST(PERSIST SERVER_ONLY)
		// Kill credit limit specifically on UGC content

	// Respawn
	U32			uiRespawnTime;							AST(SELF_ONLY)
		//Time stamp that indicates when a player can respawn

	// Progression information
	CONST_OPTIONAL_STRUCT(ProgressionInfo) pProgressionInfo;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_AND_TEAM_ONLY ALWAYS_ALLOC FORCE_CONTAINER NAME("ProgressionInfo"))
		// Contains all game progression related information for a player

	// Mission information

	CONST_OPTIONAL_STRUCT(MissionInfo) missionInfo;		AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY ALWAYS_ALLOC FORCE_CONTAINER)
		// Contains all mission related information for a player

	CONST_OPTIONAL_STRUCT(PlayerStatsInfo) pStatsInfo;  AST(PERSIST NO_TRANSACT SELF_ONLY ALWAYS_ALLOC FORCE_CONTAINER )
	
	CONST_EARRAY_OF(ActivatedPlayerSpawn) eaPersistActivatedSpawns; AST(PERSIST SELF_ONLY NAME("ActivatedSpawns"))
		// List of activated spawn points

	CONST_OPTIONAL_STRUCT(PlayerVisitedMaps) pVisitedMaps;	AST(PERSIST SELF_ONLY ALWAYS_ALLOC FORCE_CONTAINER)

	CONST_EARRAY_OF(InteriorRef) eaInteriorUnlocks;		AST(PERSIST SELF_ONLY FORCE_CONTAINER)

	const char** eaRecentContacts;						AST(SERVER_ONLY POOL_STRING)
	U32 uRecentContactsIndex;							AST(SERVER_ONLY)

	StashTable hoodMusicTimes;							NO_AST
		// Tracks times when a player last heard a particular neighborhood's music

	PlayerNemesisInfo nemesisInfo;						AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY)
		// List of all nemeses, current and past
	U32 nextAmbushTime;									AST(SERVER_ONLY)		
		// When to roll again to see if the player should be ambushed.  Not persisted
	U32 lastCalloutTime;								AST(SERVER_ONLY)
		// When we last had a callout
	U32 lastOnClickReportTime;							AST(SERVER_ONLY)
		// when we last reported an on target click
	U32 lastKillCalloutTime;							AST(SERVER_ONLY)
		// the last time we had kill callout

	const PlayerDifficultyIdx iDifficulty;				AST(PERSIST SUBSCRIBE INT)
		// player difficulty setting
	
	LogoutTimer* pLogoutTimer;							AST(SELF_ONLY)

	F32 fLogoffTime;									AST(SELF_ONLY)
		// When the entity uses the /logout function, this informs them of the time remaining before
		//  They'll be logged off.

	CutsceneDef*			pCutscene;					AST(SELF_ONLY)
		// The list of camera locations for the player's cutscene, so that entities there will spawn correctly

		
	// Interaction values

	InteractInfo *pInteractInfo;						AST(SELF_ONLY ALWAYS_ALLOC)
		// Info about what the player is currently interacting with

	EntInteractStatus InteractStatus;					AST(SELF_ONLY)
		// Interaction status for the entity

	ItemUpgradeInfo ItemUpgradeInfo;					AST(SELF_ONLY)


	// Powers-related data

	PlayerPowersWarpToData *pWarpTo;					AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// Single saved WarpTo point that can be set by the WarpSet attribute

	F32 fMovementThrottle;								AST(DEFAULT(1) SELF_ONLY)
		// The movement throttle for the player
	const S32 SkillType;								AST(PERSIST SELF_ONLY SUBTABLE(SkillTypeEnum))
		// the players crafting skill type		

	CONST_INT_EARRAY eSkillSpecialization;				AST(PERSIST SELF_ONLY SUBTABLE(ItemTagEnum))
		// the players crafting specialization

	// Social Systems

	char **eaSubscribedChannels;						AST(NO_NETSEND)
		// A list of currently subscribed channels (only used in game server)
	
	CONST_OPTIONAL_STRUCT(PlayerGuild) pGuild;			AST(PERSIST SELF_ONLY SUBSCRIBE)
	char *pcGuildName;									AST(PERSIST NO_TRANSACT SUBSCRIBE LOGIN_SUBSCRIBE)
	ContainerID iGuildID;								AST(PERSIST NO_TRANSACT SUBSCRIBE LOGIN_SUBSCRIBE)
		// The guild the player is in

	U32 uiLastGuildStatsInfoVersion;					AST(PERSIST NO_TRANSACT)
		// The version of the guild stats info processed most recently.
		// This number is used to detect when to re-evaulate temporary powers which would be granted by the guild.
	
	PlayerQueueInfo* pPlayerQueueInfo;					AST(SELF_ONLY)
		// Queue information for this entity.  Refreshed periodically from the queue server

	PlayerQueueCooldown** eaQueueCooldowns;				AST(PERSIST NO_TRANSACT SELF_ONLY)
		// Tracks cooldown timers for queues

	CONST_OPTIONAL_STRUCT(PlayerActivity) pActivity;	AST(PERSIST SELF_ONLY)
		// Activity stream and social media settings

	char *pchActivityString;							AST(SELF_AND_TEAM_ONLY)
		// Automatically generated string that is sent to the chat server for friends and team search

	TeamMode eLFGMode;									AST(PERSIST NO_TRANSACT SUBSCRIBE LOGIN_SUBSCRIBE)
		// The looking for group mode of this individual player

	LFGDifficultyMode eLFGDifficultyMode;				AST(PERSIST NO_TRANSACT)

	PlayerWhitelistFlags eWhitelistFlags;				AST(PERSIST NO_TRANSACT SELF_ONLY)
		// Whitelist options

	U32 lastAdvertTime;									AST(PERSIST NO_TRANSACT NO_NETSEND)
		// Last time a server told this player to listen to an ad

	// Networking	

	int					needsFileUpdates;				NO_AST 
		// Set by Xbox clients in development mode
	
	Packet*				msgPak;							NO_AST 
		// The packet we use to send messages to the client
	
	ClientLink*			clientLink;						NO_AST 
		// the clientlink for this player, only valid on server
	
	CONST_STRING_MODIFIABLE xuid;						AST(PERSIST SUBSCRIBE)
		// Xbox Live user id
	
		
	// UI

	PlayerUI *pUI;										AST(PERSIST SELF_ONLY ALWAYS_ALLOC NO_TRANSACT)
		// UI data for this player.
	
	MinimapWaypoint** ppMyWaypoints;					AST(SELF_ONLY)
	
	CONST_REF_TO(Message) pTitleMsgKey;					AST(PERSIST SUBSCRIBE)
		// Player title.
		// This used to be a string, hence the improper hungarian notation.

	// Minigame data
	CONST_OPTIONAL_STRUCT(MinigameData) pMinigameData;	AST(PERSIST SELF_ONLY)
	TempMinigameData *pTempMinigameData;				AST(SELF_ONLY) //Non persisted minigame data
	
	// Item Assignments
	CONST_OPTIONAL_STRUCT(ItemAssignmentPersistedData) pItemAssignmentPersistedData; AST(PERSIST SELF_ONLY FORCE_CONTAINER SUBSCRIBE)
	ItemAssignmentPlayerData* pItemAssignmentData;		AST(SELF_ONLY) //Non-persisted data
	const char* pchLastItemAssignmentVolume; AST(SERVER_ONLY PERSIST NO_TRANSACT POOL_STRING) // The last volume used for generating assignments
	
	// Shared bank ref
	REF_TO(Entity) hSharedBank;							AST(SELF_ONLY COPYDICT(EntitySharedBank)) //Non-persisted data

	// Last time this character tried to init shared bank
	U32 uSharedBankInitTime;							AST(SELF_ONLY)

	// Trade

	TradeBag *pTradeBag;
	EntityRef erTradePartner;
		
	// Crafting

	U32 iLastCraftTime;									NO_AST
	U32 iLastExperimentTime;							NO_AST

	// Pets

	EARRAY_OF(PlayerPetInfo) petInfo;					AST(SELF_ONLY)

	EARRAY_OF(PlayerPetRallyPoint) eaPetRallyPoints;	NO_AST

	// Refs to other players
	EARRAY_OF(PlayerEntCopyForInfo) eaPlayerEntCopyForInfo;	AST(SERVER_ONLY)

	// CSR 

	Vec3	last_good_pos[8];							NO_AST
		// recent places you weren't stuck, for unstuck command

	CSRListenerInfo *pCSRListener;						AST(SERVER_ONLY)
		// Info about the CSR rep listening to this character, meaning we should forward messages
		
	const U32 iStasis;									AST(PERSIST SELF_ONLY)
		// Stasis timer
	
	U32 iTimeLastScanForInteractables;					AST(SELF_ONLY)
		// The last time the Character successfully called ScanForClickies

	U32 uiTimeMovementReset;							NO_AST
		// Timestamp.  When the Player last ran the MovementReset command

	ArmamentSwapInfo *pArmamentSwapInfo;				AST(SELF_ONLY)
		// optional struct for armament swapping

	// Debugging

	PlayerDebug* debugInfo;								AST(SELF_ONLY)
		// All debug related fields should go within this sub structure

	// NPC email (email from npcs etc).
	CONST_OPTIONAL_STRUCT(NPCEMail) pEmailV2;	AST(ADDNAMES(pEmailV2_Deprecated) PERSIST SELF_ONLY ALWAYS_ALLOC FORCE_CONTAINER SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(NPCEMailV1) pEmailV1_Deprecated;	AST(PERSIST SELF_ONLY ALWAYS_ALLOC FORCE_CONTAINER ADDNAMES(pEmail))

	CONST_OPTIONAL_STRUCT(PlayerMTInfo) pMicroTransInfo;	AST(PERSIST SELF_ONLY ALWAYS_ALLOC)

	CONST_OPTIONAL_STRUCT(PlayerAccountData) pPlayerAccountData;	AST(PERSIST SELF_ONLY ALWAYS_ALLOC FORCE_CONTAINER )

	CONST_OPTIONAL_STRUCT(PlayerAuctionData) pPlayerAuctionData;	AST(PERSIST SELF_ONLY SUBSCRIBE)

	// Bitfield, put all bitfield values here for data packing reasons
	U32	bMapTransferPending : 1;						AST(SERVER_ONLY)
	
	U32 bMovingToLocation : 1;							AST(SELF_ONLY)

	U32 bStuckRespawn : 1;								AST(SELF_ONLY)

	U32 bDisableRespawn : 1;							AST(SELF_ONLY)
		// Requested Respawns have been disabled by the pvp system. 

	U32 bUseFacingPitch : 1;							AST(SELF_ONLY)
		// Use the player's facing pitch when getting facing direction

	const U32	bIgnoreBootTimer : 1;					AST(PERSIST)
		// Whether we ignore map boot timers in gslMechanics.c

	U32 bEnableAutoLoot : 1;							NO_AST
		// Whether or not the player has enabled auto-loot

	const U32 bAutoJoinTeamVoiceChat : 1;					AST(PERSIST DEFAULT(1))
		// Indicates if the player joins a voice chat session whenever they join the team

	const U32 bIsGM : 1;								AST(PERSIST)

	const U32 bWipeEmail : 1;							AST(PERSIST SERVER_ONLY)
		// NPC Email and auctions to characters that no longer exist should be wiped when syncing npc email

	U32 bTimeControlPause : 1;							AST(SELF_ONLY)
		// The player has paused the game. (SELF_ONLY because I can't image why anyone else would care.)
	U32 bTimeControlAllowed : 1;						AST(SELF_ONLY)
		// The player is allowed to pause the game. (SELF_ONLY because I can't image why anyone else would care.)

	U32 bIsFirstSessionLogin : 1;						AST(SERVER_ONLY PERSIST SOMETIMES_TRANSACT)
		// Is this the first login to a gameserver this session??

	const U32 bIsDev : 1;								AST(PERSIST)
		// Like the bIsGM flag above, but for devs.

	U32 bIgnoreClientPowerActivations : 1;				AST(SELF_ONLY)

	U32 bTacticalInputSinceLastCharacterTickQueue : 1;	NO_AST
		// only set when a combatConfig turns on tactical input power canceling, Used for queue canceling 

	U32 bGuildInitialTemporaryPowerEvaluationDone;		NO_AST
		// We re-evaluate the temporary powers no matter what when the Guild dictionary becomes available the first time in the game server.
		// This helps expressions such as EntGuildIsTheme and EntGuildMemberClassCount to work properly

	// Doors

	//This is used to determine what sort of action to take when moving through a door
	WorldRegionType iPrevDoorRegion;					AST(PERSIST NO_TRANSACT SERVER_ONLY DEFAULT(-1))

	// Transfer to the following map when the client finishes playing a movie
	MapDescription* pMovieMapTransfer;					AST(SERVER_ONLY)
	char* pchActiveMovieName;							AST(SERVER_ONLY)

	//The arrival sequence to play when spawning
	const char* pchTransitionSequence;					AST(POOL_STRING SERVER_ONLY)

	PlayerMapMoveConfirm *pMapMoveConfirm;				AST(SERVER_ONLY)
		// Temporary data about a pending map move that needs to be confirmed

	U32 eLoginWaiting;						NO_AST
		//This player's login is waiting for X, Y and Z

	//These are set whenever a player uses a door with a door; they allow teammates
	//to use "JoinTeammate" doors to join identically ID'ed teammates.
	const char *pchLastUsedDoorIdentifier;				AST(PERSIST SUBSCRIBE NO_TRANSACT SELF_AND_TEAM_ONLY POOL_STRING)
	const char *pchLastUsedDoorMapName;					AST(PERSIST SUBSCRIBE NO_TRANSACT SELF_AND_TEAM_ONLY POOL_STRING)
	const char *pchLastUsedDoorSpawnPointName;			AST(PERSIST SUBSCRIBE NO_TRANSACT SELF_AND_TEAM_ONLY POOL_STRING)
	ContainerID uLastUsedDoorMapID;						AST(PERSIST SUBSCRIBE NO_TRANSACT SELF_AND_TEAM_ONLY)
	U32 uLastUsedDoorPartitionID;						AST(PERSIST SUBSCRIBE NO_TRANSACT SELF_AND_TEAM_ONLY)
	
	// Used for Warping to targets (players you referred to the game)
	
	PlayerWarpToData *pWarp;				AST(PERSIST NO_TRANSACT SERVER_ONLY ADDNAMES(pRecruitWarp,RecruitWarp))
		// When attempting to warp to your target, this data specifies where to go
		//  The name is for posterity, it now functions for all warping

	const U32 uiLastRecruitWarpTime;					AST(PERSIST SELF_ONLY)
		// Set after a successful warp to your recruit
		
	
	CONST_INT_EARRAY iCurrentCharacterIDs;				AST(PERSIST SERVER_ONLY)
	// the list of characters whose email should not be deleted

	// Interiors
	// The invites are not currently persisted, so they only last until the
	//  player leaves their current map.  If we decide we want them
	//  to stick around, this can be changed to be persisted, and then
	//  the code that modifies the invites will need to be a transaction.
	InteriorInvite **interiorInvites;					AST(SELF_ONLY)

	GuildMapInvite **eaGuildMapInvites;					AST(SELF_ONLY)

	DirtyBit dirtyBit;									AST(NO_NETSEND)


	CONST_OPTIONAL_STRUCT(XBoxSpecificData) pXBoxSpecificData;		AST(PERSIST SUBSCRIBE FORCE_CONTAINER)

	// A cache of key/value pairs from the account server.
	// NOTE: This may not always be right, don't use it for anything important. <NPK 2010-01-14>
	AccountProxyKeyValueInfo **ppKeyValueCache;		

	//what virtual shard this entity lives on
	const ContainerID					iVirtualShardID;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	EntityPresenceInfo * pPresenceInfo;						NO_AST
	// will contain a list of entities that should be hidden for the team, due to state of quests, etc. (pseudo-phasing)
	// This may be a pointer to a struct that is owned by the team leader.

	// reward info

	CONST_EARRAY_OF(RewardModifier) eaRewardMods;			AST(PERSIST FORCE_CONTAINER SELF_ONLY)

	U32 uLastRewardTeam;									AST(SERVER_ONLY)
		// records the last time this team member got a non-roll over reward in round robin
		// used to determine who gets the next reward
	U32 uLastRewardCount;									AST(SERVER_ONLY)
		// count for items gained on this team
	U32 uiLastRewardTime;									AST(SERVER_ONLY)
		// records the last time this team member got a non-roll over reward in round robin

	EARRAY_OF(RewardGatedTypeData) eaRewardGatedData;		AST(PERSIST FORCE_CONTAINER SELF_ONLY SOMETIMES_TRANSACT)
	// The data for gated rewards, don't mix transacted (missions) rewardgated and non-transacted

	U32 uLastLotsPostedByPlayer;							AST(SELF_ONLY)
		// the number of lots that the player had search for for self. Used for limiting number of auctions by a player

    U32 uiContainerArrivalTime;                             NO_AST
        // records the time that this container arrived on the current gameserver
            
    CONST_EARRAY_OF(NumericConversionState) eaNumericConversionStates; AST(PERSIST FORCE_CONTAINER SELF_ONLY)
		// The state used to limit numeric conversions over a time interval

	REF_TO(CurrencyExchangeAccountData) hCurrencyExchangeAccountData;			AST(SELF_ONLY COPYDICT(CurrencyExchange))
		// Subscription to the currency account data for the player.  This field is not persisted.  It is set
		//  manually when the currency account data is needed (mainly when the player is interacting with the
		//  currency exchange, to provide open orders, order log and escrow balances to the UI).

	U32 timeLastVerifyEntityMissionData;				AST(PERSIST NO_TRANSACT SUBSCRIBE SELF_ONLY)

	// Event stuff
	CONST_OPTIONAL_STRUCT(PlayerEventInfo) pEventInfo;  AST(PERSIST NO_TRANSACT SELF_ONLY ALWAYS_ALLOC FORCE_CONTAINER)

	EARRAY_OF(ItemBuyBack) eaItemBuyBackList;				AST(SELF_ONLY)
	// The list of items the character can buy back, this is not persisted

	U32 uBuyBackId;											AST(SELF_ONLY)
	// The current buyback id

	U32 uBuyBackTime;										AST(SELF_ONLY)
	// The next time a buy sell operation can be done

	// UGC account - please use entGetUGCAccount(pEntity) to access the following 2 fields. The correct one will always be returned.
	// when in same shard as UGCDataManager
	REF_TO(UGCAccount) hUGCAccount;							AST(SELF_ONLY COPYDICT(UGCAccount))
	// when in a different shard as UGCDataManager
	UGCAccount *pUGCAccount;								AST(SELF_ONLY)
	// We want to throttle players' ability to perform actions that request updated UGC Account data. This is mainly for UGC Achievements, right now.
	// Things like author following go through special code to make subscription state be available in the UGC Account immediately.
	U32 iLastUGCAccountRequestTimestamp;					AST(SELF_ONLY)

    REF_TO(GroupProjectContainer) hGroupProjectContainer; AST(COPYDICT(GroupProjectContainerPlayer)) 
    // A non-persisted reference to the GroupProjectContainer for the player's personal projects.  Will only be filled in when needed.

	// for giving AL9 clients data about map CPU usage
	SimpleCpuData *pSimpleCpuData;		AST(NAME(SimpleCpuData) SELF_ONLY)

	CONST_OPTIONAL_STRUCT(GatewayInfo) pGatewayInfo;    AST(PERSIST SELF_ONLY ALWAYS_ALLOC SUBSCRIBE)
		// Preferences for Gateway

    // The time (seconds since 2000) that the player's current play session should end due to anti-addiction rules.
    const U32 addictionPlaySessionEndTime;                        AST(PERSIST)
} Player;
extern ParseTable parse_Player[];
#define TYPE_parse_Player Player

