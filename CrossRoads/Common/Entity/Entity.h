
/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef Entity_h
#define Entity_h
GCC_SYSTEM

#define GECAV_VERIFY {}//{void gecavVerify(void);gecavVerify();}

#include "entEnums.h"
#include "GlobalTypeEnum.h"
#include "Message.h"
#include "MultiVal.h"
#include "Quat.h"
#include "timing.h"
#include "UtilitiesLibEnums.h"
#include "CostumeCommonEnums.h"
#include "itemEnums.h"
#include "partition_enums.h"
#include "WorldLibEnums.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityGrid.h"
#endif

typedef U32 dtDrawSkeleton;
typedef U32 dtFxManager;
typedef U32 dtNode;
typedef U32 dtSkeleton;
typedef struct AIVars AIVars;
typedef struct AIVarsBase AIVarsBase;
typedef struct Capsule Capsule;
typedef struct Character Character;
typedef struct ChatBubbleDef ChatBubbleDef;
typedef struct CostumeTransformation CostumeTransformation;
typedef struct Critter Critter;
typedef struct CritterFaction CritterFaction;
typedef struct AllegianceDef AllegianceDef;
typedef struct EncounterUIData EncounterUIData;
typedef struct Entity Entity;
typedef struct EntityGridNode EntityGridNode;
typedef struct EntityAttach EntityAttach;
typedef struct EquippedArt EquippedArt;
typedef struct GameEventParticipant GameEventParticipant;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct ImbeddedListNode ImbeddedListNode;
typedef struct Inventory Inventory;
typedef struct InventoryV1 InventoryV1;
typedef struct PowerReplace PowerReplace;
typedef struct Message Message;
typedef struct Nemesis Nemesis;
typedef struct ParseTable ParseTable;
typedef struct PetCooldownTimer PetCooldownTimer;
typedef struct PetPowerState PetPowerState;
typedef struct PetRelationship PetRelationship;
typedef struct PCFXNoPersist PCFXNoPersist;
typedef struct PCMood PCMood;
typedef struct Power Power;
typedef struct PowerDef PowerDef;
typedef struct Player Player; 
typedef struct PlayerCostume PlayerCostume;
typedef struct PlayerDebug PlayerDebug;
typedef struct PlayerPetInfo PlayerPetInfo;
typedef struct PowerDef PowerDef;
typedef struct ProjectileEntity ProjectileEntity;
typedef struct PuppetEntity PuppetEntity;
typedef struct SavedEntityData SavedEntityData;
typedef struct SavedMapDescription SavedMapDescription;
typedef struct Team Team;
typedef struct TransformationDef TransformationDef;
typedef struct UIGen UIGen;
typedef struct UIStyleFont UIStyleFont;
typedef struct WLCostume WLCostume;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;
typedef struct WorldRegion WorldRegion;
typedef struct DisplaySprite DisplaySprite;
typedef struct EntityClientDamageFXData EntityClientDamageFXData;
typedef struct GenericLogReceiver GenericLogReceiver;
typedef struct AtlasTex AtlasTex;
typedef struct TeamUpRequest TeamUpRequest;
typedef enum InteractValidity InteractValidity;
typedef struct ActiveSuperCritterPet	ActiveSuperCritterPet;
typedef struct CharClassCategorySet CharClassCategorySet;
typedef struct UGCAccount UGCAccount;
typedef struct EmailV3 EmailV3;

#if GAMESERVER || GAMECLIENT
	typedef struct MovementManager MovementManager;
	typedef struct MovementManagerMsg MovementManagerMsg;
	typedef struct MovementRequester MovementRequester;
	typedef struct MovementDisabledHandle MovementDisabledHandle;
	typedef struct MovementCollSetHandle MovementCollSetHandle;
	typedef struct MovementCollGroupHandle MovementCollGroupHandle;
	typedef struct MovementCollBitsHandle MovementCollBitsHandle;
	typedef struct MovementNoCollHandle MovementNoCollHandle;
	typedef struct MovementGlobalMsg MovementGlobalMsg;
#endif

typedef struct NOCONST(Entity) NOCONST(Entity);

extern StaticDefineInt LootModeEnum[];
extern StaticDefineInt TeamModeEnum[];
extern ParseTable parse_CutsceneDef[];
#define TYPE_parse_CutsceneDef CutsceneDef
extern ParseTable parse_Entity[];
#define TYPE_parse_Entity Entity

#define NUM_RECENT_CONTACTS 10
#define ENTITY_DEFAULT_SEND_DISTANCE 300.0f

//WARNING!!! if you ever change this, grep for all occurrences of 10240 and change them too.
//It's hardwired in various places for dependency reasons. (Including some in structparser).
#define MAX_ENTITIES_PRIVATE 10240 // must be multiple of 32 (some wacky networking bitfields)
#define MAX_NAME_LEN 128
#define MAX_DESCRIPTION_LEN 2048

// The Fixup Version is used for performing structural fixes on the Entity structure
// When incrementing this, document who changed it, when, and why
// Version 0: From the dawn of time
// Version 1: UNUSED SPACE RESERVED FOR VERSIONING ON CO LIVE
// Version 2: UNUSED SPACE RESERVED FOR VERSIONING ON CO LIVE
// Version 3: UNUSED SPACE RESERVED FOR VERSIONING ON CO LIVE
// Version 4: UNUSED SPACE RESERVED FOR VERSIONING ON CO LIVE
// Version 5: SDANGELO : 10/28/2009 : Complete rework of PlayerCostume data structure
// Version 6: CMILLER : 1/13/2012 : Substantial changes to Inventories and Items
#define CURRENT_ENTITY_FIXUP_VERSION  6

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum GlobalEntityFlags {
	ENTITYFLAG_IS_PLAYER = 1 << 0, // needed for EntityGrid
	ENTITYFLAG_DEAD = 1 << 1, // Entity is dead, which matters to the powers system
	ENTITYFLAG_UNTARGETABLE = 1 << 2, // This entity is untargetable
	ENTITYFLAG_UNSELECTABLE = 1 << 3,	// This entity is unselectable
	ENTITYFLAG_CIVILIAN = 1 << 4, // To avoid doing a whole new GlobalType, setting this flag for civilians
	ENTITYFLAG_CIV_PROCESSING_ONLY = 1 << 5, // Only do extra cheap civilian processing
	ENTITYFLAG_PLAYER_DISCONNECTED = 1 << 6,
	ENTITYFLAG_PLAYER_LOGGING_IN = 1 << 7,
	ENTITYFLAG_DONOTSEND = 1 << 8, // Entity should not be sent to clients, but otherwise handled normally
	ENTITYFLAG_DONOTDRAW = 1 << 9, // Entity should not be drawn on clients, but may be sent to them
	ENTITYFLAG_IGNORE = 1 << 10, // Entity should be ignored by ai, powers system, etc.  Nearby encounters should still spawn.
	ENTITYFLAG_DESTROY = 1 << 11, // This entity needs to be destroyed 
	ENTITYFLAG_PLAYER_LOGGING_OUT = 1 << 12, // Entity is logging out or is being transferred to another map
	ENTITYFLAG_PET_LOGGING_IN = 1 << 13, // This is a newly-transferred player pet, that needs to have some setup run on it
	ENTITYFLAG_DONOTFADE = 1 << 14,  // Do not fade this entity on the client when it is destroyed
	ENTITYFLAG_PUPPETPROGRESS = 1 << 15, //This entity is in the progress of doing a puppet swap
	ENTITYFLAG_PLAYER_INVITE_WHITELIST_ENABLED = 1 << 16, //Deprecated
	ENTITYFLAG_PLAYER_TRADE_WHITELIST_ENABLED = 1 << 17, //Deprecated
	ENTITYFLAG_PLAYER_DUEL_WHITELIST_ENABLED = 1 << 18, //Deprecated
	ENTITYFLAG_VANITYPET = 1 << 19, // This is a vanity pet
	ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS = 1 << 20,
	ENTITYFLAG_CRITTERPET = 1 << 21, // This is a critter pet, and requires some specail handeling
	ENTITYFLAG_PROJECTILE = 1 << 22, // this is a projectile, need to filter out to ignore these ent types, like for AI
	ENTITYFLAG_PET_LOGGING_OUT = 1 << 23, // This pet is being sent back to the ObjectDB.
} GlobalEntityFlags;

typedef U32 EntityFlags; //what type to use for entity flags

AUTO_ENUM;
typedef enum MissionReturnErrorType
{
	MissionReturnErrorType_None,		ENAMES(None)
	MissionReturnErrorType_InvalidMap,	ENAMES(InvalidMap)
	MissionReturnErrorType_InCombat,	ENAMES(InCombat)
} MissionReturnErrorType;

AUTO_ENUM;
typedef enum NeedOrGreedChoice
{
    NeedOrGreedChoice_None,
    NeedOrGreedChoice_Pass,
    NeedOrGreedChoice_Greed,
    NeedOrGreedChoice_Need,
    NeedOrGreedChoice_Count
} NeedOrGreedChoice;
extern StaticDefineInt NeedOrGreedChoiceEnum[];

AUTO_STRUCT;
typedef struct CurrentHood
{
	const char* pchName;							AST( POOL_STRING )
	REF_TO(Message) hMessage;
} CurrentHood;

// This is used to keep track of the player's current status on the team
AUTO_ENUM;
typedef enum TeamState
{
	TeamState_Member,		// Part of the team
	TeamState_Invitee,		// Invited to the team
	TeamState_Requester,	// Requested to be added to the team
	TeamState_LoggedOut,	// Logged out improperly while in the team
} TeamState;

AUTO_STRUCT 
AST_CONTAINER
AST_IGNORE(bAutoAcceptInvites) 
AST_IGNORE(eInviteResponse) 
AST_IGNORE(eEOIForSelf) 
AST_IGNORE(bEOIForOthers) 
AST_IGNORE(bAutoSidekickOnTeamJoin)
AST_IGNORE(iCompletedMissionsVersion)
AST_IGNORE(ppchCompletedMissions);
typedef struct PlayerTeam
{
	// To test somebody's team status, please use the macros in team.h
	
	// Whether or not a character is associated with a team is determined solely
	// by iTeamID. If iTeamID is zero, they aren't associated with a team in any
	// way, and eState is meaningless. If iTeamID is non-zero, then eState
	// determines the nature of their relationship with the team.
	
	const ContainerID iTeamID;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// Team container ID
	
	const ContainerID iRejoinID;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY)
		// Team container ID of the team to rejoin when logged out improperly
	
	REF_TO(Team) hTeam;							AST(SELF_ONLY COPYDICT(TEAM))
		// Handle to the team container

	U32 bMapLocal : 1;							AST(SELF_ONLY)
		// This is a "map local" team that cannot be left by normal means.
	
	U32 iTimeSinceHandleInit;					AST(NO_NETSEND)
		// Time since the handle was set, used to call validation routines if
		// the handle continues to resolve to NULL after a certain interval
	
	U32 iTimeSinceLastUpdate;					AST(NO_NETSEND)
		// Time since the player's info was last updated to the server, used to
		// make sure that the UpdateInfo transaction isn't called too often
	
	const TeamState eState;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// Specifies this player's relationship to the team
	
	CONST_STRING_MODIFIABLE pcInviterName;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY)
		// The name of the entity who invited this player to the team

	CONST_STRING_MODIFIABLE pcInviterHandle;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY)
		// The account handle of the entity who invited this player to the team
	
	const U32 iLogoutTime;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY)
		// The time when the player logged out, used to determine whether to try
		// to rejoin the team when they return
	
	U32 iVersion;								AST(NO_NETSEND)
		// The team version, to be compared to the iVersion on the team container
		// to determine if the member list needs to be updated
	
	U32 iInChat;								AST(NO_NETSEND)
		// The team container ID of the team chat channel the player is in, or 0
		// if they aren't in a chat channel, used to make sure that the player is
		// logged in to the correct team chat
	
	F32 fLastUpdate;							AST(NO_NETSEND)
		// The amount of time, in seconds, since the team data was last checked
	
	bool bTriedRejoining;						AST(NO_NETSEND)
		// Used to keep track of whether or not the player has already tried to
		// rejoin their team after being improperly disconnected.

	bool bUpdateTeamPowers;						AST(NO_NETSEND)
		// Used to tell this entity to update their team powers.
		// Currently only used for recruiting(referral bonus) powers

	bool bTeamMissionMapTransfer;				AST(NO_NETSEND)
		// Used to keep track of whether this player is on an away team map transfer(STO).
		// If the player leaves the team, then he/she will be removed from the transfer.

	U8  iNumMatchTries;							AST(NO_NETSEND)
		// Number of times matchmaking/open instancing has been tried

	bool bInTeamDialog;
		// Indicates if this team member is participating in a team dialog

	bool bIsTeamSpokesman;
		// Indicates if this team member is the team spokesman for the current dialog

	const S32 eMode;							AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY SUBTABLE(TeamModeEnum))
		// The default join mode for teams that are created by this player

	const S32 eLootMode;						AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY SUBTABLE(LootModeEnum))
		// The default loot mode for teams that are created by this player

	CONST_STRING_POOLED eLootQuality;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY POOL_STRING)
		// The default loot threshold for teams that are created by this player

/** Data used for calculating team size when transferring maps **/

	U32 iNearbyTeamSize;						AST(PERSIST NO_TRANSACT SELF_ONLY)
		// Set as the player leaves the map.  Used to calculate team size for encounters at the
		// start of the map.  This value is only set for a short amount of time.

	const char* pchDestinationMap;				AST(PERSIST NO_TRANSACT SELF_ONLY POOL_STRING)
		// This is the target map for iNearbyTeamSize.  If this does not match the current map when checking
		// iNearbyTeamSize, then iNearbyTeamsize should be ignored.

	U32 iTeamSizeTimestamp;
		// This is a non-persisted timestamp which is used to determine if the iNearbyTeamSize is still valid (and iAverageTeamLevel)
		// or if it needs to be recalculated upon map transfer.
		
	U32 iAverageTeamLevel;						AST(PERSIST NO_TRANSACT SELF_ONLY)
		// This is a non-persisted average team level used during map transfers to record the average player (so encounters are spawned correctly).
		// it is calculated with iNearbyTeamSize and uses iTeamSizeTimestamp and iAverageTeamLevelTime.

	U32 iAverageTeamLevelTime;					AST(PERSIST NO_TRANSACT SELF_ONLY)
		// This is the time that iAverageTeamLevel is valid to

	U32 iLastTeamIDForInitialMeeting;			AST(PERSIST NO_TRANSACT)
		// When a player joins a team the first time, they will either need to go to the lobby or go where the team leader is to play together.
		
	S32 lastRecruitType;						AST(PERSIST SOMETIMES_TRANSACT SERVER_ONLY)
		// Used to save the last recruitment type this character had, used for mission rewards

/****/

	DirtyBit dirtyBit;							AST(NO_NETSEND)	
} PlayerTeam;


// Character is now defined in Character.h

AST_PREFIX(WIKI(AUTO))
AUTO_STRUCT AST_CONTAINER AST_IGNORE(hReferencedCostume);
typedef struct CostumeRef
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)
	U16 dirtiedCount;
	U16 transformation;

	// The rule is to first look at the effective costume.  This is only present if powers/equipment alter the costume.
	// Then look at the stored costume, which is set for persisted entities
	// Then look at the substitute costume, which is used by special critters like Nemesis ones.
	// Then look at the referenced costume, which is what most critter entities will have
	// Destructible critters have no player costume.  They have a string name of the object it pulled geo from.
	PlayerCostume *pEffectiveCostume;
	PlayerCostume *pStoredCostume;
	PlayerCostume *pSubstituteCostume;
	REF_TO(PlayerCostume) hReferencedCostume;
	const char *pcDestructibleObjectCostume;
	
	//Contains the costume data for the player's mount
	PlayerCostume *pMountCostume;
	F32 fMountScaleOverride;

	//Client "dismount" prediction
	bool bPredictDismount;		AST(CLIENT_ONLY)

	// Mood is tracked outside the costume and is applied during costume generate
	CONST_REF_TO(PCMood) hMood;					AST(PERSIST SUBSCRIBE)

	// Additional FX can be set up in the world and are not persisted
	PCFXNoPersist **eaAdditionalFX;

	CostumeTransformation *pTransformation;
} CostumeRef;

extern ParseTable parse_CostumeRef[];
#define TYPE_parse_CostumeRef CostumeRef

// Used to notate properties of entities, for display on the client
AUTO_STRUCT;
typedef struct UIVar
{
	char*	    pchName;	AST(KEY POOL_STRING)
	MultiVal	Value;			    
} UIVar;

#define ENTITY_VIS_CHECK_INTERVAL_MS 250

AUTO_ENUM;
typedef enum EntityVisibilityState
{
	kEntityVisibility_Unknown,
	kEntityVisibility_Visible,
	kEntityVisibility_Hidden,
} EntityVisibilityState;

AUTO_STRUCT;
typedef struct EntityVisibilityCache
{
	U32 uiLastCheckTimeMs;
	EntityVisibilityState eState;
} EntityVisibilityCache;

AUTO_STRUCT;
typedef struct ChatBubble
{
	U32 uiStartTimeMs;
	U32 uiEndTimeMs;
	char *pchMessage;
	REF_TO(ChatBubbleDef) hDef;
} ChatBubble;

AUTO_STRUCT;
typedef struct DamageFloat
{
	U32 eOffsetFrom;
	F32 fMaxLifetime;
	F32 fLifetime;
	F32 fDelay;	//How long to wait before showing this floater after its creation, in seconds.
	Vec2 v2Pos;
	Vec2 v2Velocity;
	Vec2 v2DestPos;
	U32 iColor;
	U32 iColor2;
	F32 fScale;
	F32 fMaxPopout;
	F32 fPriority; //relative priority compared to other visible floaters in the same group
	char *pchMessage;
	float fAngle;

	AtlasTex* pIcon;	NO_AST
	U32 eIconOffsetFrom;
	Vec2 v2IconPos;
	Vec2 v2IconPosOffset;
	U32 iIconColor;
	F32 fIconScale;
	F32 fIconFade;

	REF_TO(UIStyleFont) hFont;

	Vec2 v2PosOffset;
	Vec2 v2VelocityBase;
	Vec2 v2Size;
	U32 uiPowID;
	S32 combatTrackerFlags;

	bool bCrit;	//this floater was a crit

} DamageFloat;

AUTO_STRUCT;
typedef struct DamageFloatGroup
{
	DamageFloat **eaDamageFloats;
} DamageFloatGroup;

AUTO_ENUM;
typedef enum OffscreenType
{
	OffscreenType_Target,
	OffscreenType_Team,
	OffscreenType_Player,
	OffscreenType_CritterFriendly,
	OffscreenType_CritterEnemy,
} OffscreenType;

AUTO_STRUCT;
typedef struct EncounterUIData
{
	EntityRef			erEnt;
	U32					iCount;
	Vec2				vMin;
	Vec2				vMax;
	F32					fScreenDist;
	Vec3				vPosSum;	//position sum
	EncounterUIData**	eaChildren;
	S32					iValidChildren;
	const char*			pchGenName;		AST(POOL_STRING)
} EncounterUIData;

AUTO_STRUCT;
typedef struct OffscreenUIData
{
	EntityRef			erEnt;
	U32					iCount;
	U32					iCombined;
	S32					iEdge;
	F32					fPosition;
	S32					iDistanceIndex;
	OffscreenType		iType;
	EncounterUIData*	pEncData;		AST(UNOWNED)
	bool				bSame;
	bool				bEncounter;
	const char*			pchGenName;		AST(POOL_STRING)
} OffscreenUIData; 

typedef enum EUIDamageDirection
{
	EUIDamageDirection_FRONT = 0,
	EUIDamageDirection_RIGHT,
	EUIDamageDirection_BACK,
	EUIDamageDirection_LEFT,
	EUIDamageDirection_COUNT
} EUIDamageDirection;


// Per-entity UI information storage, client-only (non-persisted).
AUTO_STRUCT;
typedef struct EntityUI
{
	EntityVisibilityCache VisCache;
	ChatBubble **eaBubbles;

	bool bDraw : 1;
	bool bWasOnscreen : 1;

	UIGen *pGen; AST(LATEBIND)

	EncounterUIData* pEncounterData; AST(UNOWNED)
	OffscreenUIData* pOffscreenData; AST(UNOWNED)

	bool bLastOffscreenLoS;
	U32 uiLastOffscreenLoSCheck;
		// When the entity is offscreen, check LoS (when requested) periodically.
		// TODO(MK): find a reasonable way of combining this with OffscreenUIData

	U32 uiLastDamaged;
		// Used by the client for lifebar fade in/out, and for
		//  displaying damage counters. In milliseconds.

	U32 uiLastFlank;
		// Similar to uiLastDamaged, but notes the time the Entity
		//  was last affected by a CombatTrackerNet with the Flank flag

	U32 uiLastDamagedByPlayer;
		// same as uiLastDamged, but watches only the player.  If we need to watch more ents,
		// we will need to complicate the system

	F32 fLastDamageAngle;				AST(DEFAULT(-1))
		// The last attack angle relative to the player's facing direction
	F32 fLastDamageTangentAngle;
		// The angle of the vector tangent to the player's facing direction

	U32 uiLastDamageDirectionTimes[EUIDamageDirection_COUNT];
		// The last time we were damaged from one of the enumerated directions
	
	DamageFloatGroup **eaDamageFloatGroups;

	U32 uiLastTime;						AST(CLIENT_ONLY)
	Vec3 vBoxMin;						AST(CLIENT_ONLY)
	Vec3 vBoxMax;						AST(CLIENT_ONLY)
	U32 vuiLowerAtTime[3];				AST(CLIENT_ONLY)
		// Entity screen box calculation / smoothing / caching.

	F32 fBoundingAdjustVelocity;		NO_AST
		// used for screen bounding box adjustment
	
} EntityUI;

// Data for tracking non-persisted external influences upon the entity, currently just PowerVolumes
AUTO_STRUCT;
typedef struct EntityExternalInnate
{
	Power **ppPowersExternalInnate;				AST(NO_INDEX, LATEBIND)
		// NONINDEXED All Innate Powers granted to the Entity through external means (maps, volumes, etc)

	S32 iPowerVolumes;							NO_AST
		// Number of power volumes this entity is in which try to grant innate.  If this is larger than 0,
		//  we update the powerVolumeCache using the center point of the primary capsule, or the midpoint of the
		//  entity's box.

	WorldVolumeQueryCache *pPowerVolumeCache;	NO_AST
		// Cache object containing the volumes the entity's center is inside of

} EntityExternalInnate;


AUTO_STRUCT;
typedef struct EntityClientTargetFXNode
{
	U32	guidTarget;
		// This is a dtNode created as an artifical target for the client

	F32	fRange;
		// This is the range that the target node is placed along the entity's facing direction
} EntityClientTargetFXNode;

// This structure is included inline in Entity, but is pulled out here to keep
// the main structure looking cleaner
typedef struct EntityDynamicsData
{
	// Dynamics (FX + Animation): Mostly guids now to support dynamics thread

	dtNode				guidLocation;
		// This tells us where the entity is at any given time according to the server. You should not parent nodes to this node.

	dtNode				guidRoot;
		// This is the node that skeletons and fx managers are attached to, for finding children nodes.

	dtSkeleton			guidSkeleton;
		// The current state of this objects animations and skeleton

	dtDrawSkeleton		guidDrawSkeleton;
		// The drawable aspects of the skeleton

	dtFxManager			guidFxMan;
		// This is where fx on this entity are managed

	EntityClientDamageFXData* pDamageFXData;
	EntityClientTargetFXNode** eaTargetFXNodes;

	U32					contactAnimOverrideHandle;
} EntityDynamicsData;


#if !GAMESERVER && !GAMECLIENT
	typedef struct EntityMovementData
	{
		U32 nothing;
	} EntityMovementData;
#else
	// This structure is included inline in Entity, but is pulled out here to keep
	// the main structure looking cleaner
	typedef struct EntityMovementData
	{
		GenericLogReceiver*			glr;

		MovementManager*			movement;
		MovementRequester*			mrSurface;
		MovementRequester*			mrFlight;
		MovementRequester*			mrDoorGeo;
		MovementRequester*			mrTactical;
		MovementRequester*			mrDead;
		MovementRequester*			mrInteraction;
		MovementRequester*			mrEmote;
		U32							mrEmoteSetHandle;
		MovementRequester*			mrGrab;
		MovementRequester*			mrDragon;

		// Requester to lock down character movement
		MovementRequester*			mrDisabled;	

		// Separate requester to lock down character movement when deemed necessary by a CSR.
		// Kept separate so as not to conflict with other systems which must disable movement.
		MovementRequester*			mrDisabledCSR;

		MovementDisabledHandle*		mdhIgnored;
		MovementDisabledHandle*		mdhDisconnected;
		MovementDisabledHandle*		mdhPaused;

		MovementNoCollHandle*		mnchVanity;
		MovementNoCollHandle*		mnchPowers;
		MovementNoCollHandle*		mnchCostume;
		MovementNoCollHandle*		mnchExpression;
		MovementNoCollHandle*		mnchAttach;

		MovementCollSetHandle*		mcsHandle;
		MovementCollSetHandle*		mcsHandleDbg;

		MovementCollGroupHandle*	mcgHandle;
		MovementCollGroupHandle*	mcgHandleDbg;

		MovementCollBitsHandle*		mcbHandle;
		MovementCollBitsHandle*		mcbHandleDbg;

		U32							movementBodyHandle;
		U32*						debugBodyHandles;
	
		U32							spcSprintStart;
		F32							maxSprintDurationSeconds;
		U32							spcCooldownStart;
		U32							spcAimCooldownStart;
		F32							sprintCooldownSeconds;
		F32							sprintFuel;
		F32							rollCooldownSeconds;
		F32							aimCooldownSeconds;
	
		U32							isSprinting				: 1;
		U32							sprintUsesFuel			: 1;
		U32							isRolling				: 1;
		U32							cooldownIsFromRunning	: 1;
		U32							cooldownIsFromAiming	: 1;
	} EntityMovementData;
#endif

AST_PREFIX( WIKI(AUTO) )
AUTO_STRUCT AST_CONTAINER WIKI("Entity Struct Documentation") AST_FORMATSTRING(HTTP_HEADER_COMMAND="GetEntityHTMLHeader") AST_IGNORE(hAlignment) AST_CREATION_COMMENT_FIELD(pCreationComment);
typedef struct Entity
{
	/*****
	Entity struct no longer uses TOK_PUPPET_NO_COPY for the puppet system. Any persisted and transacted fields added to this structure
	that need to be included in the puppet system need to be added to the transaction helper Entity_PuppetCopy found in gslSavedPet.c
	*****/

	// ---- Basic information about an entity --------------------------------------------------

	EntityRef			myRef;							NO_AST
		// The entRef this entity, which corresponds to array index

	S64					lastActive;						NO_AST
		// When it was last active, used to decide if it should be inactive

	const ContainerID	myContainerID;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE KEY)
		// The transaction system ID, which may be 0 if this is not available to it

	const GlobalType	myEntityType;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SUBTABLE(GlobalTypeEnum))
		// The type of this entity, which is project specific

	EntityFlags			myCodeEntityFlags;				AST(SERVER_ONLY, NO_NETSEND, FLAGS, SUBTABLE(GlobalEntityFlagsEnum))
	EntityFlags			myDataEntityFlags;				AST(SERVER_ONLY, NO_NETSEND, FLAGS, SUBTABLE(GlobalEntityFlagsEnum))
		// Two sets of flags to make sure code and data aren't stomping each other 
		// (would like a MOD system, akin to AIConfig).  When setting, |s them together into myEntityFlags.
		// When checking, uses myEntityFlags still.

	EntityFlags			myEntityFlags;					AST(SERVER_ONLY, FLAGS, SUBTABLE(GlobalEntityFlagsEnum))
		// Generic flags. Anyone can use these, but they are put up
		//  here so that they can be passed as masking flags to
		//  searches
		// Now sent through the shit bucket instead of normal entity sending because
		//  it changes too often

	U32					mySendFlags;					AST(SERVER_ONLY)
	U32					sentThisFrame;					AST(SERVER_ONLY)
		// Flags concerning the send status of this entity. ENT_SEND_FLAG_XXX

	int					iPartitionIdx_UseAccessor;		AST(SERVER_ONLY)
		// Partition the Entity belongs to.  Not sent because the client should have no need to know
		//  what partition it's in - every Entity it knows about should be in the same partition.


	//auto-filled in by textparser on creation
	char *pCreationComment; NO_AST


	// ---- Entity Location --------------------------------------------------

	Vec3				posNextFrame;					NO_AST
	U32					locationNextFrameValid:1;		NO_AST
	F32					collRadiusCached;				NO_AST

	Vec3				pos_use_accessor;				AST(PERSIST NO_TRANSACT SERVER_ONLY)
	Quat				rot_use_accessor;				AST(PERSIST NO_TRANSACT SERVER_ONLY)
	Vec2				pyFace_use_accessor;			NO_AST
	U32					frameWhenViewChanged;			NO_AST
	U32					frameWhenViewSet;				NO_AST
	U32					posViewIsAtRest	: 1;			NO_AST
	U32					rotViewIsAtRest : 1;			NO_AST
	U32					pyFaceViewIsAtRest : 1;			NO_AST
	// this is for Parser dirty bits optimization
	U32					dirtyBitSet : 1;				NO_AST
	U32					nearbyPlayer : 1;				NO_AST
	U32					isInvisibleTransient : 1;		NO_AST
	U32					isInvisiblePersistent : 1;		AST(NAME(invisible) PERSIST NO_TRANSACT SERVER_ONLY)

	U32					initPlayerLoginPositionRun : 1; NO_AST
	U32					bFakeEntity : 1;				NO_AST
	const char*			astrRegion;						AST(POOL_STRING) 


	// ---- Important Sub-structures --------------------------------------------------

	const char			debugName[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// The debug name of this entity

	CONST_OPTIONAL_STRUCT(Player) pPlayer;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// Player-specific information

	CONST_OPTIONAL_STRUCT(Character) pChar;				AST(PERSIST SUBSCRIBE LATEBIND FORCE_CONTAINER)
		// Character information, such as hitpoints and powers

	CONST_OPTIONAL_STRUCT(SavedEntityData) pSaved;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// Information to be saved to the database, that is used by players and other saved entities and only sent to self and teammates

	CONST_OPTIONAL_STRUCT(EntityAttach) pAttach;
		// Has all the entity attaching/riding stuff if necessary

	CONST_OPTIONAL_STRUCT(Critter) pCritter;			AST(PERSIST SUBSCRIBE LATEBIND FORCE_CONTAINER)
		// Critter-specific information

	CONST_OPTIONAL_STRUCT(Nemesis) pNemesis;			AST(PERSIST SUBSCRIBE SELF_ONLY LATEBIND FORCE_CONTAINER)
		// Nemesis-specific information. Never filled in on "real" entities, only for container subscription

	CONST_OPTIONAL_STRUCT(Inventory) pInventoryV2;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY FORCE_CONTAINER)  
		//optional inventory bag(s)attached to this entity
		//Inventory has its own special puppet copy code

	CONST_OPTIONAL_STRUCT(InventoryV1) pInventoryV1_Deprecated;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY FORCE_CONTAINER ADDNAMES(pInventory, Inventory))  
		//old inventory, loaded for fixup only and then destroyed.

	// Item ID 
	const U32			ItemIDMax;						AST(PERSIST SUBSCRIBE SELF_ONLY)

	CONST_OPTIONAL_STRUCT(PlayerTeam) pTeam;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)
		// Team information for this entity
		// This is not stored on the Player structure in order to allow for the possibility of NPCs on
		// teams in the future

	CONST_OPTIONAL_STRUCT(EmailV3) pEmailV3;			AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
		//Combined NPC/Player email.

	TeamUpRequest *pTeamUpRequest;					AST(SELF_ONLY)
		// Open team request. This player has stepped into a volume that allows him to join an active open team

	CONST_REF_TO(CritterFaction) hFaction;				AST(PERSIST SUBSCRIBE REFDICT(CritterFaction))
		// The KOS faction

	CONST_REF_TO(CritterFaction) hSubFaction;			AST(PERSIST SUBSCRIBE REFDICT(CritterFaction))
		// Player's sub faction

	REF_TO(CritterFaction) hFactionOverride;			AST(REFDICT(CritterFaction))
		// A temporary override faction used by PvP maps

	REF_TO(CritterFaction) hPowerFactionOverride;		AST(REFDICT(CritterFaction))
		// The temporary override faction from powers.  Wears off when the power does.

	REF_TO(AllegianceDef) hAllegiance;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(Allegiance))

	REF_TO(AllegianceDef) hSubAllegiance;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(Allegiance))

	REF_TO(WLCostume)	hWLCostume;						AST(NO_NETSEND)
		// The graphics state of the costume

	CostumeRef			costumeRef;						AST(PERSIST SUBSCRIBE)
		// Which costume to use for drawing this skel

	const Gender eGender;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE NAME(Gender))
		// The gender of this entity (e.g. the avatar, not the account holder)

	F32 fHue;
		// Override hue for PowerFX, CostumeFX, etc.  Set at entity creation time, from CritterDef or
		//  EntCreate AttribMod.

	EquippedArt* pEquippedArt;							AST(SELF_ONLY)
		// State of ItemArt stuff from Inventory (perhaps should be inside Inventory struct?).
		//  Does not need to be sent until it needs to be used for prediction.

	UIVar**				UIVars;
		// Vars for the UI that can be set in an FSM or whereever
	

	// ---- Runtime Tracking Data --------------------------------------------------

	// Dynamics (FX + Animation): Mostly guids now to support dynamics thread
	EntityDynamicsData dyn;								NO_AST

	// Physics / Controls / Movement.
	EntityMovementData mm;								NO_AST

	EntityGridNode*		egNode;							NO_AST
	EntityGridNode*		egNodePlayer;					NO_AST

	WorldVolumeQueryCache*	volumeCache;				NO_AST
		// Cache object containing the volumes the entity is inside of

	ProjectileEntity*	pProjectile;					NO_AST
		// if it is an awesome projectile 

	// Relationships between entities

	EntityRef					erCreator;
		// If this entity was created by another, this is the creator

	EntityRef					erOwner;
		// If this entity was created by another, this is the ultimate creator

	REF_TO(WorldInteractionNode) hCreatorNode;
		// If this entity was created by an interaction node, this is the node

	EntityRef					erCreatorTarget;		AST(SERVER_ONLY)
		// If this entity was created by another, this was their main target

	// Mission and AI information

	CurrentHood* currentNeighborhood;					AST(SELF_ONLY NAME("Neighborhood") )
		// Which neighborhood the entity is in.

	GameEventParticipant *pGameEventInfo;				AST(SERVER_ONLY LATEBIND)
		// Cached info for GameEvents, for performance

	union {
		AIVarsBase*		aibase;							AST(SERVER_ONLY UNOWNED ADDNAMES("ai") LATEBIND)
		AIVars*			ai;								NO_AST
	};

	// Powers replacement
	U32					uiPowerReplaceIDMax;			AST(SELF_ONLY)
	PowerReplace**		ppPowerReplaces;				AST(FORCE_CONTAINER SELF_ONLY)

	EntityExternalInnate* externalInnate;				AST(SELF_ONLY)
		// Data tracking the external innate effects on the entity

	S64 iAICombatTeamID;
		// This ID is generated by casting the AI combat team pointer in the game server to an integer. It's used by the client to check if the entities are from the same aggro group.
	

	// ---- Client Only Data --------------------------------------------------

	// UI information

	U32 uiUpdateInactiveEntUI;							AST(CLIENT_ONLY)
		// Part of what this timer checks is if pEntUI should be created,
		// so it can't live on pEntUI unfortunately.
	EntityUI *pEntUI;									AST(CLIENT_ONLY)
		// Client-side only information about UI

	// LOD / Fadeout Information

	F32					fEntitySendDistance;
		// Entity send distance
	F32					fEntityMinSeeAtDistance;
		// This entity can always be seen at least this far regardless of perception value

	U32					bFadeOutAndThenRemove : 1;		AST(CLIENT_ONLY)
	U32					bNoInterpAlpha : 1;				AST(CLIENT_ONLY)
	U32					bNoInterpAlphaOnSpawn : 1;
		// If Preserve alpha is set, then for one frame the alpha goal will be ignored and the set alpha will be used
	U32					bPreserveAlpha : 1;				AST(CLIENT_ONLY)
	U32					bImperceptible : 1;				AST(CLIENT_ONLY) 
		// this gets set based on whether this entity can be perceived by the client
			
	U32					bForceFadeOut : 1;				AST(CLIENT_ONLY)
		// If cut scene is hiding this entity to control an entity that looks the same
	U32					bInCutscene : 1;				AST(CLIENT_ONLY)
	U32					bVisionEffectDeath : 1;			NO_AST
			
	U32					bDeadBodyFaded : 1;				AST(CLIENT_ONLY)
		// used by the deadBodies system

	U32					bDeathPredicted : 1;			AST(CLIENT_ONLY)
		// death prediction, client only flag

	U8					factionDirtiedCount;
		// used to notify client that the faction has changed

		// If this is set, it forces the entity to fade out
	F32					fAlpha;							AST(CLIENT_ONLY)
	F32					fHideTime;						AST(CLIENT_ONLY)
		// don't set me directly, this gets calculated every frame in gclEntityTick
	F32					fCameraCollisionFade;			AST(DEFAULT(1) CLIENT_ONLY)


	// ---- Other Entity Data --------------------------------------------------

	STRING_POOLED astrMissionToGrant;			AST(PERSIST NO_TRANSACT SERVER_ONLY POOL_STRING)
		// A single mission to grant during entity tick.  This exists
		// so the LoginServer can grant UGC missions.

    U32 lastProjSpecificLogTime;                        AST(SERVER_ONLY)
	char * estrProjSpecificLogString;					AST(ESTRING SERVER_ONLY)
		// Per entity description

	U32 iSeedNumber;									AST(SERVER_ONLY)
	int iBoxNumber[3];									AST(SERVER_ONLY)

	ImbeddedListNode*	pNodeNearbyPlayer;				NO_AST
		// used by the player net sending to determine which entities are nearby a player
		// if this node is allocated, then it means it wants to get flagged when it is determined that it 
		// is nearby a player- the nearbyPlayer flag above will be set. 

	const U32 FixupVersion;								AST(PERSIST)
	
	DirtyBit dirtyBit;									AST(SERVER_ONLY)


	AST_COMMAND("Get RAM Usage", "ServerMonGetEntityRAMUsage $FIELD(myEntityType) $FIELD(myContainerID) $INT(Should allocate a bunch of extra copies then free them to test RAM reporting)", "\q$SERVERTYPE\q = \qObjectDB\q OR \q$SERVERTYPE\q = \qCloneObjectDB\q")
	AST_COMMAND("Check Container Location", "DebugCheckContainerLoc $FIELD(myEntityType) $FIELD(myContainerID)", "\q$SERVERTYPE\q = \qObjectDB\q")
	AST_COMMAND("Boot player","ForceLogout $FIELD(myEntityType) $FIELD(myContainerID) $CONFIRM(Really boot this player?)","\q$FIELD(myEntityType)\q = \qEntityPlayer\q AND \q$SERVERTYPE\q = \qGameServer\q")
	AST_COMMAND("Set Access Level","SetAccessLevelByContainerID $FIELD(myContainerID) $INT(New Access Level) $CONFIRM(Really set the access level?)","\q$FIELD(myEntityType)\q = \qEntityPlayer\q AND \q$SERVERTYPE\q = \qGameServer\q")
	AST_COMMAND("Send Message","BroadcastMessageToPlayer $FIELD(myContainerID) $STRING(Message To Send)$NORETURN","\q$FIELD(myEntityType)\q = \qEntityPlayer\q")
	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity $FIELD(myEntityType) $FIELD(myContainerID) $STRING(Transaction String)$CONFIRM(Really apply this transaction?)")
	AST_COMMAND("Dump Container", "ServerMonDumpEntity $FIELD(myEntityType) $FIELD(myContainerID)", "\q$SERVERTYPE\q = \qObjectDB\q")
	AST_COMMAND("Dump Container on Clone", "ServerMonDumpEntity $FIELD(myEntityType) $FIELD(myContainerID)", "\q$SERVERTYPE\q = \qCloneObjectDB\q")
} Entity;									// The basic client+server object for describing characters and interactive, physical objects
AST_PREFIX()

#if GAMESERVER || GAMECLIENT
	typedef struct EntityMovementThreadData {
		EntGridPosCopy entGridPosCopy;
		EntGridPosCopy entGridPosCopyPlayer;
	} EntityMovementThreadData;
#endif

// Other utility
#define ENTDEBUGNAME(e)		(e->debugName)
#define CHARDEBUGNAME(c)	SAFE_MEMBER2((c), pEntParent, debugName)

// These flag bits shouldn't be set more than once; if two different functions are trying to set them,
// the flags should be refcounted or the functions should be rewritten.
//#define FLAG_BITS_SET_ONCE_ONLY (ENTITYFLAG_IGNORE)
#define FLAG_BITS_SET_ONCE_ONLY (0)

typedef struct EntityInfo
{
	U8 active : 1;
	U8 regularDiffNextFrame : 1;
	U8 regularDiffThisFrame : 1;
}EntityInfo;

//the number of bits which describes the max number of entities, ie, 16 means 64k entities
//This needs to be kept in sync with MAX_ENTITIES_PRIVATE, 1 << MAX_ENTITIES_BITS must be >= MAX_ENTITIES_PRIVATE
#define MAX_ENTITIES_BITS (16)

#define MAX_IDS_PER_SLOT (1 << (32 - MAX_ENTITIES_BITS))

#define INDEX_FROM_REFERENCE(iRef) ( (iRef) & ( (1 << MAX_ENTITIES_BITS) - 1))

#ifdef GAMESERVER
	extern EntityInfo entInfo[MAX_ENTITIES_PRIVATE];

	#define entGetRefIndex(e) INDEX_FROM_REFERENCE(e->myRef)

	#define ENTINFO_BY_INDEX(index) entInfo[(index)] 
	#define ENTINFO(e) ENTINFO_BY_INDEX(entGetRefIndex(e))

	#define ENTACTIVE_BY_INDEX(index) ENTINFO_BY_INDEX(index).active
	#define ENTACTIVE(e) ENTINFO(e).active

	static __forceinline void entSetActive(SA_PARAM_NN_VALID Entity* e) { ENTACTIVE(e) = true; e->lastActive = ABS_TIME; }
#else
	#define entSetActive(e)
	#define ENTACTIVE_BY_INDEX(index) 1
#endif

#if GAMESERVER || GAMECLIENT
	void entGridUpdate(Entity* e, int create);  // entitygrid.h includes Entity.h already so I guess this gets checked at least
#endif

// for telling ParserSend what has been updated and should be considered for sending to clients
void entity_SetDirtyBitInternal(Entity *ent,ParseTable table[], void *pStruct, bool bSubStructsChanged MEM_DBG_PARMS);
#define entity_SetDirtyBit(ent, pti, structptr, unused) entity_SetDirtyBitInternal(ent, pti, structptr, unused MEM_DBG_PARMS_INIT)

static __forceinline bool entIsPlayer(SA_PARAM_OP_VALID const Entity *pEnt) { return SAFE_MEMBER(pEnt,myEntityType) == GLOBALTYPE_ENTITYPLAYER; }    
static __forceinline bool entIsProjectile(SA_PARAM_NN_VALID const Entity *pEnt) { return pEnt->myEntityType == GLOBALTYPE_ENTITYPROJECTILE; }    
static __forceinline EntityRef entGetRef(SA_PARAM_NN_VALID const Entity *pEnt) { return pEnt->myRef; }
static __forceinline ContainerID entGetContainerID(SA_PARAM_NN_VALID const Entity *pEnt) { return pEnt->myContainerID; }
static __forceinline GlobalType entGetType(SA_PARAM_NN_VALID const Entity *pEnt) { return pEnt->myEntityType; }
static __forceinline EntityFlags entCheckFlag(SA_PARAM_NN_VALID const Entity *pEnt, EntityFlags eFlagBitsToCheck) { return pEnt->myEntityFlags & eFlagBitsToCheck; }
static __forceinline void entSetCodeFlagBits(SA_PARAM_NN_VALID Entity *pEnt, EntityFlags eFlagBitsToSet) 
	{ devassert(0 == (pEnt->myCodeEntityFlags & eFlagBitsToSet & FLAG_BITS_SET_ONCE_ONLY)); 
	  pEnt->myCodeEntityFlags |= eFlagBitsToSet; 
	  pEnt->myEntityFlags = pEnt->myDataEntityFlags | pEnt->myCodeEntityFlags;
	  #if GAMESERVER || GAMECLIENT
		  entGridUpdate(pEnt, false);
	  #endif
	  entSetActive(pEnt);}
static __forceinline void entSetDataFlagBits(SA_PARAM_NN_VALID Entity *pEnt, EntityFlags eFlagBitsToSet) 
	{ devassert(0 == (pEnt->myDataEntityFlags & eFlagBitsToSet & FLAG_BITS_SET_ONCE_ONLY)); 
	  pEnt->myDataEntityFlags |= eFlagBitsToSet; 
	  pEnt->myEntityFlags = pEnt->myDataEntityFlags | pEnt->myCodeEntityFlags;
	  #if GAMESERVER || GAMECLIENT
		  entGridUpdate(pEnt, false);
	  #endif
	  entSetActive(pEnt);}
static __forceinline void entClearCodeFlagBits(SA_PARAM_NN_VALID Entity *pEnt, EntityFlags eFlagBitsToClear) 
	  { pEnt->myCodeEntityFlags &= ~eFlagBitsToClear; 
		pEnt->myEntityFlags = pEnt->myDataEntityFlags | pEnt->myCodeEntityFlags;
	    #if GAMESERVER || GAMECLIENT
			entGridUpdate(pEnt, false);
	    #endif
		entSetActive(pEnt); }
static __forceinline void entClearDataFlagBits(SA_PARAM_NN_VALID Entity *pEnt, EntityFlags eFlagBitsToClear) 
	  { pEnt->myDataEntityFlags &= ~eFlagBitsToClear; 
		pEnt->myEntityFlags = pEnt->myDataEntityFlags | pEnt->myCodeEntityFlags;
		#if GAMESERVER || GAMECLIENT
			entGridUpdate(pEnt, false);
		#endif
		entSetActive(pEnt); }
static __forceinline EntityFlags entGetFlagBits(SA_PARAM_NN_VALID const Entity* pEnt) { return pEnt->myEntityFlags; }
static __forceinline Player *entGetPlayer(SA_PARAM_NN_VALID const Entity *pEnt) { return pEnt->pPlayer;}
static __forceinline Character *entGetChar(SA_PARAM_NN_VALID const Entity *pEnt) { return pEnt->pChar;}


extern char* exprCurAutoTrans;
static __forceinline int entGetPartitionIdxEx(SA_PARAM_OP_VALID const Entity *pEnt, const char *func, const char *file, long line)
{
	if (!pEnt) {
		devassertmsgf(pEnt, "entGetPartitionIdx called with NULL Entity from %s %s %d",func,file,line);
		return PARTITION_UNINITIALIZED;
	}

#ifndef GAMECLIENT
	// Can get partition index in a transaction if we're not in a transaction or if it knows it's in a transaction
	if (!pEnt->bFakeEntity) {
		assertmsgf(!exprCurAutoTrans || (pEnt->iPartitionIdx_UseAccessor == PARTITION_IN_TRANSACTION), 
						"Cannot access a partition from an autotransaction (%s)", exprCurAutoTrans);
	}
#endif
#ifdef GAMESERVER
	if (!pEnt->bFakeEntity) {
		assertmsgf(pEnt->iPartitionIdx_UseAccessor != 0, "Entities with partition 0 are not allowed on the game server.  Could the programmer be trying to get the index from a container subscription entity?  If so, that is not allowed.");
	}
#endif

	return pEnt->iPartitionIdx_UseAccessor;
}

#define entGetPartitionIdx(e) entGetPartitionIdxEx((e),__FUNCTION__,__FILE__,__LINE__)
#define entGetPartitionIdx_NoAssert(e)	((e) ? (e)->iPartitionIdx_UseAccessor : PARTITION_UNINITIALIZED)

//this should really be a forceinline, but I can't figure out how to do that without headers #including things we don't
//want them to include
#define entGetVirtualShardID(pEnt)  ((pEnt)->pPlayer ? (pEnt)->pPlayer->iVirtualShardID : 0)

void entSetLanguage(SA_PARAM_NN_VALID Entity *pEnt, int langID);
static __forceinline void entGetPosForNextFrame(SA_PARAM_NN_VALID Entity* pEnt, Vec3 vOutput) { if (pEnt->locationNextFrameValid) copyVec3(pEnt->posNextFrame, vOutput); else copyVec3(pEnt->pos_use_accessor, vOutput); }
void entGetPosDir(SA_PARAM_NN_VALID Entity *e, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vOutputPos, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vOutputDir);
SA_ORET_OP_VALID WorldRegion* entGetWorldRegionOfEnt(SA_PARAM_NN_VALID Entity* ent);
WorldRegionType entGetWorldRegionTypeOfEnt(SA_PARAM_OP_VALID Entity* ent);
static __forceinline CritterFaction* entGetFaction(SA_PARAM_NN_VALID const Entity* pEnt) {CritterFaction *faction = NULL;faction = GET_REF(pEnt->hFactionOverride);if(faction) return faction;faction = GET_REF(pEnt->hPowerFactionOverride);if(faction) return faction;return GET_REF(pEnt->hFaction);}
static __forceinline CritterFaction* entGetSubFaction(SA_PARAM_NN_VALID const Entity* pEnt) {return GET_REF(pEnt->hSubFaction);}

// Returns true if the entity is not null and is not flagged as dead
// on the client, if bDeathPredicted is set - regard this entity as dead
static __forceinline bool entIsAlive(SA_PARAM_NN_VALID const Entity* e) 
{ 
	return 
#ifdef GAMECLIENT 
		!e->bDeathPredicted &&
#endif
		!entCheckFlag(e,ENTITYFLAG_DEAD | ENTITYFLAG_DESTROY); 
}

static __forceinline bool entIsCivilian(SA_PARAM_NN_VALID const Entity *e) {return entCheckFlag(e, ENTITYFLAG_CIVILIAN) == ENTITYFLAG_CIVILIAN;}

// Vaguely combat-related Entity utility
EntityRef entity_GetTargetRef(SA_PARAM_OP_VALID Entity *e);
SA_RET_OP_VALID Entity* entity_GetTarget(SA_PARAM_OP_VALID Entity *e);
SA_RET_OP_VALID Entity* entity_GetTargetDual(SA_PARAM_OP_VALID Entity *e);
SA_RET_OP_VALID Entity* entity_GetAssistTarget(SA_PARAM_OP_VALID Entity *e);
SA_RET_OP_VALID Entity* entity_GetFocusTarget(SA_PARAM_OP_VALID Entity *e);

bool entIsInCombat(const Entity* pEnt);
bool entIsSprinting(SA_PARAM_NN_VALID const Entity *pEnt);
void entGetSprintTimes(	SA_PARAM_NN_VALID const Entity* e,
						F32* secondsUsedOut,
						F32* secondsTotalOut,
						S32* sprintUsesFuelOut,
						F32* sprintFuelOut);
void entGetSprintCooldownTimes(	SA_PARAM_NN_VALID const Entity* e,
								F32* secondsUsedOut,
								F32* secondsTotalOut);
void entGetRollCooldownTimes(SA_PARAM_NN_VALID const Entity* e, F32* secondsUsedOut, F32* secondsTotalOut);
void entGetAimCooldownTimes(	SA_PARAM_NN_VALID const Entity* e,
	F32* secondsUsedOut,
	F32* secondsTotalOut);
bool entIsAiming(SA_PARAM_NN_VALID Entity *pEnt);
bool entIsCrouching(SA_PARAM_NN_VALID Entity *pEnt);
bool entIsRolling(SA_PARAM_NN_VALID Entity *pEnt);
bool entIsUsingShooterControls(Entity *e);

// Note that this matrix uses the facing PY for the rotation.
// The pitch is ignored if the player is using a control scheme
// in which the UseFacingPitch flag is on. This flag sets the pitch
// based on the camera and that's definitely not what we want to use
// for the body orientation.
void entGetBodyMat(SA_PARAM_NN_VALID Entity* e, Mat4 mat);

void entGetVisualMat(SA_PARAM_NN_VALID Entity* e, Mat4 mat);
void entSetPos(SA_PARAM_NN_VALID Entity* e, const Vec3 vPos, bool bUpdateMM, const char* reason);
void entSetRot(SA_PARAM_NN_VALID Entity* e, const Quat qRot, bool bUpdateMM, const char* reason);
void entSetPosRotFace(SA_PARAM_NN_VALID Entity* e, const Vec3 vPos, const Quat qRot, const Vec2 pyFace, bool bUpdateMM, bool bUpdateClientCam, const char* reason);
void entSetFacePY(Entity* e, const Vec2 pyFace, const char* reason);
void entSetDynOffset(SA_PARAM_NN_VALID Entity* e, const Vec3 posOffset);

void entGetFaceSpaceMat3(SA_PARAM_NN_VALID Entity* e, Mat3 mat);
void entGetFaceSpaceQuat(SA_PARAM_NN_VALID Entity* e, Quat qRot);

bool entGetBoneMat(SA_PARAM_NN_VALID Entity* e, const char *bone, Mat4 mat);
bool entGetBonePos(SA_PARAM_NN_VALID Entity* e, const char *bone, Vec3 pos);

void entUpdateView(SA_PARAM_NN_VALID Entity* e);

// defines to avoid including player.h
#define entGetLanguage(pEnt) ((pEnt) && (pEnt)->pPlayer) ? (pEnt)->pPlayer->langID : locGetLanguage(getCurrentLocale())
int entGetAccessLevel(Entity const *e); // DON'T USE IN TRANSACTIONS! use SAFE_MEMBER2(e,pPlayer,accessLevel)
#define entGetAccountID(pEnt) SAFE_MEMBER2((pEnt), pPlayer, accountID) 
#define entGetClientLink(pEnt) SAFE_MEMBER2((pEnt), pPlayer, clientLink)
#define entGetNetLink(pEnt) SAFE_MEMBER3((pEnt), pPlayer, clientLink, netLink)
UGCAccount *entGetUGCAccount(Entity const *e);

extern U32 frameFromMovementSystem;

#if !(GAMESERVER || GAMECLIENT)
	#define entGetPos(e, vOutput)		(copyVec3((e)->pos_use_accessor, vOutput))
	#define entGetRot(e, qOutput)		(copyQuat((e)->rot_use_accessor, qOutput))
	#define entGetFacePY(e, pyFaceOut)	(copyVec2((e)->pyFace_use_accessor, pyFaceOut))
#else
	#define entViewIsCurrent(e, atRest)											\
			(	!(e)->atRest &&													\
				(e)->frameWhenViewSet == frameFromMovementSystem				\
				||																\
				(e)->atRest &&													\
				subS32((e)->frameWhenViewSet, (e)->frameWhenViewChanged) >= 0)

	#define entUpdateViewIfNotCurrent(e, atRest)								\
			(	!entViewIsCurrent(e, atRest) &&									\
				(entUpdateView(e),0))

	#define entGetPos(e, vOutput)												\
			(	entUpdateViewIfNotCurrent(e, posViewIsAtRest),					\
				copyVec3((e)->pos_use_accessor, vOutput))

	#define entGetRot(e, qOutput)												\
			(	entUpdateViewIfNotCurrent(e, rotViewIsAtRest),					\
				copyQuat((e)->rot_use_accessor, qOutput))

	#define entGetFacePY(e, pyFaceOut)											\
			(	entUpdateViewIfNotCurrent(e, pyFaceViewIsAtRest),				\
				copyVec2((e)->pyFace_use_accessor, pyFaceOut))
#endif

// These functions are included in this library

// Gets an Entity from an Ent Ref
SA_RET_OP_VALID Entity *entFromEntityRef(int iPartitionIdx, EntityRef iRef);
SA_RET_OP_VALID Entity *entFromEntityRefAnyPartition(EntityRef iRef);
static __forceinline Character *charFromEntRef(int iPartitionIdx, EntityRef iRef) {Entity *be = entFromEntityRef(iPartitionIdx, iRef); return SAFE_MEMBER(be, pChar); }

// Returns true if the given entity Ref likely existed recently (not 100% trustworthy)
//(entity slots are reused, so this will be true until that slot has been reused a certain # of times)
#define ENT_CREATIONS_PER_SLOT_THAT_COUNT_AS_RECENT (16)
bool entHasRefExistedRecently(EntityRef iRef);

// The order of initialization/cleanup is:
// entInitializeCommon
// entExternInitializeCommon
// gslInitializeEntity
// gslExternInitializeEntity
// ..
// gslExternCleanupEntity
// gslCleanupEntity
// entExternCleanupCommon
// entCleanupCommon

#if GAMESERVER || GAMECLIENT
	void entMovementDefaultMsgHandler(const MovementManagerMsg* msg);
#endif

// Do some common initialization of an entity
void entInitializeCommon(Entity* e, bool bCreateMovement);

// Do cleanup that requires a full entity
void entPreCleanupCommon(Entity* e, bool isReloading);

// Do some common cleanup of an entity
void entCleanupCommon(int iPartitionIdx, SA_PARAM_NN_VALID Entity* e, bool isReloading, bool bDoCleanup);

// If the entity has any splats, invalidate them such that they regenerate before rendering.
void entInvalidateSplats(Entity *e);

// Sets the entity's death bits and movement restrictions
void entity_DeathAnimationUpdate(SA_PARAM_NN_VALID Entity * pEnt, int bDead, U32 uiTime);

// Causes the ent to die if it is not already dead
#define entDie(e, timeToLinger, giveRewards, giveKillCredit, pExtract) entDieEx(e, timeToLinger, giveRewards, giveKillCredit, pExtract, __FILE__, __LINE__)
void entDieEx(SA_PARAM_OP_VALID Entity *e, F32 timeToLinger, int giveRewards, int giveKillCredit, GameAccountDataExtract *pExtract, const char* file, int line);

// Returns true if the entity from the given entref has disconnected
bool entIsDisconnected(SA_PARAM_OP_VALID Entity *e);

// Returns the height of the entity from the ground, up to the maximum distance
F32 entHeight(SA_PARAM_NN_VALID Entity *e, F32 fMaxDistance);

// Copies the instantaneous foreground velocity of the entity into the vector
void entCopyVelocityFG(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID Vec3 vecVelocity);

// get the value of an entities UI var
bool entGetUIVar(SA_PARAM_NN_VALID Entity *e, SA_PARAM_OP_STR const char* VarName, SA_PRE_NN_FREE SA_POST_NN_VALID MultiVal* pMultiVal);

// Gets ent from entref or "selected" or "me" passed in as string
Entity* entGetClientTarget(Entity* be, const char* target, EntityRef* erTargetOut);

// Returns the whether or not an entity has something selected, and where it is if so
bool entGetClientSelectedTargetPos( Entity* be, Vec3 vPos );

// Returns true if the player is ignoring the ent with the given container ID
bool playerIsIgnoring(SA_PARAM_NN_VALID Player *pPlayer, ContainerID ignore);

// Adds a camera position to the list of cutscene camera positions on the player
//void playerAddCutsceneCameraPos(SA_PARAM_NN_VALID Player* pPlayer, Vec3 pos, F32 moveTime, F32 holdTime);
//void playerAddCutsceneTargetPos(SA_PARAM_NN_VALID Player* pPlayer, Vec3 pos, F32 moveTime, F32 holdTime);

// Return distance from capsule to capsule, as well as set direction vector
// If entity isn't passed in, use point as backup
// TargetOut is a point on the target capsule's line, and you can orient towards it if you want to find the closest point
F32 entGetDistance(Entity *beSource, const Vec3 posSource, Entity *beTarget, const Vec3 posTarget, Vec3 targetOut);

// Same as entGetDistance, but will return the source position found for the distance calculation
F32 entGetDistanceSourcePos(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 sourceOut, Vec3 targetOut);

// Same as entGetDistance, but will use the pointSource and/or pointTarget as the entities position if they are valid
F32 entGetDistanceAtPositions(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 targetOut);

// Same as entGetDistance, but with a position offset for the source
F32 entGetDistanceOffset(Entity *beSource, const Vec3 posSource, const Vec3 offsetSource, Entity *beTarget, const Vec3 posTarget, Vec3 targetOut);

F32 entGetDistanceXZ(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 targetOut);

// Returns distance between line and given entity
// Useful for mouse select, etc
F32 entLineDistanceEx(const Vec3 pointSource, F32 sourceRadius, const Vec3 pointDir, F32 length, Entity *beTarget, Vec3 targetOut, bool bAdjustForCrouch);
#define entLineDistance(pointSource, sourceRadius, pointDir, length, beTarget, targetOut) entLineDistanceEx(pointSource, sourceRadius, pointDir, length, beTarget, targetOut, false)

// Get the width and height of this entity
F32 entGetHeightBasedOnSkeleton(Entity* beTarget);
F32 entGetHeightEx(Entity* e, bool bAdjustForCrouch);
#define entGetHeight(e) entGetHeightEx(e, false)
F32 entGetWidth(Entity *e);
const Capsule* entGetPrimaryCapsule(Entity *e);
const Capsule*const* entGetCapsules(Entity *e);

F32 entGetPrimaryCapsuleRadius(Entity *e);
bool entGetPrimaryCapsuleWorldSpaceBounds(Entity *pEnt, Vec3 vCapBoundMinOut, Vec3 vCapBoundMaxOut);
F32 entGetWidth(Entity* entity);

// Get the bounding box of this entity
void entGetLocalBoundingBox(Entity* e, Vec3 vBoundMin, Vec3 vBoundMax, bool bGetTargetingBounds);

F32 entGetBoundingSphere(Entity* entity, Vec3 vCenterOut);

// Log data about an entity
int entLog_vprintf(int eCategory, Entity *entity, const char *action, FORMAT_STR char const *oldFmt, va_list ap);
int entLog(int eCategory, Entity *entity, const char *action, FORMAT_STR char const *fmt, ...);
#define entLog(eCategory, entity, action, fmt, ...) entLog(eCategory, entity, action, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
int entLogWithStruct(int eCategory, Entity *eneity, const char *action, void *pStruct, ParseTable *pTPI);
int entLogPairs(int eCategory, Entity *entity, const char *action, ...);
#define ENTLOG_PAIRS(eCategory, entity, action, ...) entLogPairs(eCategory, entity, action, LOG_PAIR_EXPAND(__VA_ARGS__), NULL)

// Send a simple message to an entity
void entPrintf(Entity *entity, FORMAT_STR char const *fmt, ...);
#define entPrintf(entity, fmt, ...) entPrintf(entity, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Is the entity targetable or selectable in any way?
bool entIsTargetable(SA_PARAM_NN_VALID Entity *e);

// Is the entity selectable in any way?
bool entIsSelectable(SA_PARAM_OP_VALID Character *source, SA_PARAM_OP_VALID Entity *eTarget);

// does the entity send a command to the server saying it was clicked on? (but not selectable)
// civilians use this
bool entReportsOnTargetClick(Entity *e);

// Gets the localized name of the entity.
const char* entGetLangName(SA_PARAM_NN_VALID const Entity* pEnt, Language eLang);
const char* entGetLangNameUntranslated(const Entity* pEnt, bool* pbIsMessageKey);
#define entGetLocalName(pEnt) entGetLangName(pEnt, locGetLanguage(getCurrentLocale()))

bool entGetGenderNameFromString(const Entity* pEnt, Language eLangID, const char* pchString, char** pestrOut);

const char *entGetLangSubName(SA_PARAM_NN_VALID const Entity *ent, Language eLang);
#define entGetLocalSubName(pEnt) entGetLangSubName(pEnt, locGetLanguage(getCurrentLocale()))
const char *entGetLangSubNameNoDebug(SA_PARAM_NN_VALID const Entity *ent, Language eLang);
#define entGetLocalSubNameNoDebug(pEnt) entGetLangSubNameNoDebug(pEnt, locGetLanguage(getCurrentLocale()))

// Gets the public account name of an entity, or the localized name if there is no account name.
const char *entGetAccountOrLangName(SA_PARAM_OP_VALID const Entity *ent, Language eLang);
#define entGetAccountOrLocalName(pEnt) entGetAccountOrLangName(pEnt, locGetLanguage(getCurrentLocale()))

// Get the persisted name of an entity; assert if not a persisted entity.
const char *entGetPersistedName(SA_PARAM_NN_VALID const Entity *e);

// Get the message for the entity's class name
Message *entGetClassNameMsg(SA_PARAM_NN_VALID const Entity *pEnt);

// Various accessors for pet/riding stuff

// Get entity's owner
Entity *entGetOwner(Entity *e);

// Get entity's creator
Entity *entGetCreator(Entity *e);

// Is this entity a primary pet?
bool entIsPrimaryPet(Entity *e);

// Return primary pet of entity, or NULL if none 
Entity *entGetPrimaryPet(Entity *e);

// If entity is riding another entity, return it
Entity *entGetMount(Entity *e);

// If we're being ridden by another entity, return it
Entity *entGetRider(const Entity *e);

// Do the two entities have the same owner (i.e. are they owned/controlled by the same player)?
bool entity_trh_IsOwnerSame(ATR_ARGS, NOCONST(Entity) *pEnt1, NOCONST(Entity) *pEnt2);
#define entity_IsOwnerSame(pEnt1, pEnt2) entity_trh_IsOwnerSame(ATR_EMPTY_ARGS, pEnt1, pEnt2)

//provides a list of comma separated name-value pairs that are put into the log every time entLog is called, logging
//globally-useful project-specific things. For instance "LEV 5, POW Mutant"
LATELINK;
char *entity_CreateProjSpecificLogString(Entity *entity);
#define entity_GetProjSpecificLogString(e) ((e) && (e)->estrProjSpecificLogString) ? (e)->estrProjSpecificLogString : entity_CreateProjSpecificLogString(e)

LATELINK;
void GameSpecific_HolsterRequest(Entity* pEnt, Entity* pOwner, bool bUnholster);

LATELINK;
void GameSpecific_CreateShieldFX(Entity* pEnt);

// Adds or removes an externally-owned innate Power on the Entity
S32 entity_UpdatePowerExternalInnate(SA_PARAM_NN_VALID Entity *pent, SA_PARAM_NN_VALID PowerDef *pdef, S32 bAdd);

// Notes that the entity entered or exited a volume that grants an innate power
void entity_ExternalInnateUpdateVolumeCount(SA_PARAM_NN_VALID Entity *pent, S32 bEnter);

//Verify that the character can interact with a targeted ent or interaction node
bool entity_VerifyInteractTarget(int iPartitionIdx, Entity *ent, Entity *eTarget, WorldInteractionNode *pTargetNode, U32 uNodeInteractDist, Vec3 vNodePosFallback, F32 fNodeRadiusFallback, bool bForPickup, InteractValidity* peFailOut);

void entity_UpdateTargetableNodes( Entity* ent );


//Get the close interaction range
F32 entity_GetCurrentRegionInteractDist(Entity *entPlayer);

F32 entity_GetPickupRange( Entity* e );

bool entity_IsNodeInRange(	Entity* pEntity, Vec3 vEntSource, WorldInteractionNode* pNode, F32 fRange, Vec3 vNodePosFallback, F32 fNodeRadiusFallback, 
							Vec3 vTargetOut, F32* fDistOut, bool bCheapTestsOnly );

//gets a subscribed pet entity, if the type is GLOBALTYPE_ENTITYSAVEDPET
SA_RET_OP_VALID Entity* entity_GetSubEntity(int iPartitionIdx, Entity* pOwner, GlobalType iType, ContainerID iContainerID);
SA_RET_OP_VALID PuppetEntity* entity_GetPuppetByTypeEx(SA_PARAM_NN_VALID Entity* pEnt, unsigned int iClass, SA_PARAM_OP_VALID CharClassCategorySet *pSet, bool bGetActive);
#define entity_GetPuppetByType(pEnt, pchClassType, pSet, bGetActive) entity_GetPuppetByTypeEx(pEnt, StaticDefineIntGetInt(CharClassTypesEnum, pchClassType), pSet, bGetActive)
SA_RET_OP_VALID Entity* entity_GetPuppetEntityByType(SA_PARAM_NN_VALID Entity* pEnt, const char* pchClassType, CharClassCategorySet *pSet, bool bGetActualEntity, bool bGetActive);

Entity* entity_CreateOwnerCopy( Entity* pOwner, Entity* pEnt, bool bCopyInventory, bool bCopyNumerics, bool bCopyPowerTrees, bool bCopyPuppets, bool bCopyAttribs);

//Get a seed number for a player
int Entity_GetSeedNumber(int iPartitionIdx, Entity *pEntity, Vec3 vSpawnPos);

// Functions to get Experience Level
// For the following functions, "ExpOf" means the total points required,
// and "ExpTo" means the points the character still needs to earn.

S32 entity_trh_CalculateExpLevelSlow(NOCONST(Entity) *pEnt, bool bUseFullLevel);
S32 entity_CalculateExpLevelSlow(Entity *pEnt);
S32 entity_CalculateFullExpLevelSlow(Entity *pEnt);

S32 entity_trh_GetSavedExpLevelLimited(ATH_ARG NOCONST(Entity) *pEnt);
S32 entity_trh_GetSavedExpLevel(NOCONST(Entity) *pEnt);
S32 entity_GetSavedExpLevelLimited(Entity *pEnt);
S32 entity_GetSavedExpLevel(Entity *pEnt);

S32 entity_ExpOfCurrentExpLevel(Entity *pEnt);
S32 entity_ExpOfNextExpLevel(Entity *pEnt);
S32 entity_ExpToNextExpLevel(Entity *pEnt);
F32 entity_PercentToNextExpLevel(Entity *pEnt);

// functions about skill specializtion
bool entity_PlayerHasSkillSpecializationInternal(const int * const * eSkillSpecialization, ItemTag eItemTag);
bool entity_trh_PlayerHasSkillSpecialization(ATH_ARG NOCONST(Entity) *pEnt, ItemTag eItemTag);
#define entity_PlayerHasSkillSpecialization(pEnt, eItemTag) entity_trh_PlayerHasSkillSpecialization(CONTAINER_NOCONST(Entity, (pEnt)), eItemTag)
bool entity_trh_CraftingCheckTag(ATH_ARG NOCONST(Entity) *pEnt, ItemTag eTag);
#define entity_CraftingCheckTag(pEnt, eTag) entity_trh_CraftingCheckTag(CONTAINER_NOCONST(Entity, (pEnt)), eTag)

S32 entity_trh_AllegianceSpeciesChange(ATR_ARGS, ATH_ARG NOCONST(Entity)* e, AllegianceDef *pDef);

SA_RET_NN_STR const char *entity_GetChatBubbleDefName(SA_PARAM_OP_VALID Entity *pEnt);

SA_RET_OP_VALID PlayerDebug* entGetPlayerDebug(SA_PARAM_NN_VALID Entity* e, bool create);

// Gets the PlayerPetInfo off of a Player based on the pet ref
PlayerPetInfo *player_GetPetInfo(Player *pPlayer, EntityRef erPet);
// Gets the cooldown timer for a power category on the specified pet
PetCooldownTimer *player_GetPetCooldownTimerForCategory(Player *pPlayer, EntityRef erPet, S32 iCategory);
// Gets the cooldown time from a PowerDef on the specified pet
F32 player_GetPetCooldownFromPowerDef(Player *pPlayer, EntityRef erPet, PowerDef *pDef);
// Gets the cooldown time from a power on the specified pet
F32 player_GetPetCooldownFromPower(Player *pPlayer, EntityRef erPet, Power *pPower);
// Gets the PetPowerState off of a Player based on the pet and PowerDef.  Should probably be in Player.c/h, if such
//  a thing existed
SA_RET_OP_VALID PetPowerState* player_GetPetPowerState(SA_PARAM_NN_VALID Player *pPlayer, EntityRef erPet, SA_PARAM_NN_VALID PowerDef *pdef);
SA_RET_OP_VALID PetPowerState* playerPetInfo_FindPetPowerStateByName(SA_PARAM_OP_VALID PlayerPetInfo *pPetInfo, SA_PARAM_OP_STR const char *pchName);

bool entity_IsMissionReturnAvailable(void);
bool entity_IsMissionReturnEnabled(Entity* ent);
MissionReturnErrorType entity_GetReturnFromMissionMapError(SA_PARAM_NN_VALID Entity* ent);

SA_RET_OP_VALID SavedMapDescription* entity_GetMapLeaveDestination(SA_PARAM_NN_VALID Entity* ent);

// Get the combat level of this entity
S32 entity_GetCombatLevel(Entity* pEnt);

// Get the last map, regardless of type
SavedMapDescription *entity_GetLastMap(Entity *pEntity);

// Get the last static map
SavedMapDescription *entity_GetLastStaticMap(Entity *pEntity);

// Get the last non-static map
SavedMapDescription *entity_GetLastNonStaticMap(Entity *pEntity);

// Set the current map
void entity_SetCurrentMap(NOCONST(Entity) *pEnt, SavedMapDescription *pNewMapDescription);

bool entity_PowerCartIsRespecRequired(SA_PARAM_NN_VALID Entity* pEnt);

// Check to see if an entity has already completed a UGC project
bool entity_HasRecentlyCompletedUGCProject(Entity* pEnt, ContainerID iProjectID, U32 uWithinSeconds);
#define entity_HasCompletedUGCProject(pEnt, iProjectID) entity_HasRecentlyCompletedUGCProject(pEnt, iProjectID, 0)
bool entity_HasCompletedUGCProjectSince(Entity* pEnt, ContainerID iProjectID, U32 uStartTime);

LATELINK;
void gameSpecificFixup(Entity* pEnt);

LATELINK;
int gameSpecificFixup_Version(void);

LATELINK;
int gameSpecificPreLoginFixup_Version(void);

void createEntityReport(char **pestrBuffer);

void entity_SendFXMessage(U32 er, const char* pchMessage);

F32 exprEntGetAttrib(SA_PARAM_OP_VALID Entity *pEntity, const char *attribName);

S32 entity_FindMacroByID(Entity* pEnt, U32 uMacroID);
S32 entity_FindMacro(Entity* pEnt, const char* pchMacroString, const char* pchDesc, const char* pchIcon);
bool entity_IsMacroValid(const char* pchMacroString, const char* pchDesc, const char* pchIcon);

bool entity_IsAutoLootEnabled(Entity* pEnt);
bool entity_ShouldAutoLootTarget(Entity* pEnt, Entity* pTarget);

bool entity_IsUGCCharacter(Entity *pEntity);

#if GAMESERVER || GAMECLIENT
void entCommonMovementGlobalMsgHandler(const MovementGlobalMsg* msg);
#endif

#endif
