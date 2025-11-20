/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef Character_h__
#define Character_h__
GCC_SYSTEM

#include "PowersEnums.h" // For enums
#include "PowerActivation.h" // For ActivationFailureReason
#include "PowerModes.h" // For power mode enum
#include "StatPoints.h"
	
typedef struct CharacterPath		CharacterPath;
typedef struct AttribAccrualSet		AttribAccrualSet;
typedef struct AttribMod			AttribMod;
typedef struct Character			Character;
typedef struct CharacterAttribs		CharacterAttribs;
typedef struct CharacterAttribute	CharacterAttribute;
typedef struct CharacterClass		CharacterClass;
typedef struct CharacterPath		CharacterPath;
typedef struct CharacterPowerSlots	CharacterPowerSlots;
typedef struct CombatAdvantageNode	CombatAdvantageNode;
typedef struct CombatReactivePowerInfo CombatReactivePowerInfo;
typedef struct CombatTrackerNet		CombatTrackerNet;
typedef struct CooldownTimer		CooldownTimer;
typedef struct CritterFactionRelationship CritterFactionRelationship; 
typedef struct DamageTracker		DamageTracker;
typedef struct Entity				Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Item					Item;
typedef struct ModArray				ModArray;
typedef struct MovementRequester	MovementRequester;
typedef struct NOCONST(Character)	NOCONST(Character);
typedef struct NOCONST(Entity)	NOCONST(Entity);
typedef struct PTGroupDef			PTGroupDef;
typedef struct PTNode				PTNode;
typedef struct PTNodeDef			PTNodeDef;
typedef struct PTNodeEnhancementDef	PTNodeEnhancementDef;
typedef struct Power				Power;
typedef struct PowerActivation		PowerActivation;
typedef struct PowerActivationState	PowerActivationState;
typedef struct PowerDef				PowerDef;
typedef struct PowerEmit			PowerEmit;
typedef struct PowerList			PowerList;
typedef struct CombatPowerStateSwitchingInfo CombatPowerStateSwitchingInfo;
typedef struct PowerRef				PowerRef;
typedef struct PowerSlotSet			PowerSlotSet;
typedef struct PowerSubtargetNet	PowerSubtargetNet;
typedef struct PowerSubtargetChoice	PowerSubtargetChoice;
typedef struct PowerTree			PowerTree;
typedef struct PowerTreeDef			PowerTreeDef;
typedef struct PVPDuel				PVPDuel;
typedef struct PVPFlag				PVPFlag;
typedef struct RewardModifier		RewardModifier;
typedef struct SavedAttribute		SavedAttribute;
typedef struct SavedAttribStats		SavedAttribStats;
typedef struct SpeciesDef			SpeciesDef;
typedef struct CustomSpecies		CustomSpecies;
typedef struct TempAttributes		TempAttributes;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct CharacterAITargetInfo CharacterAITargetInfo;
typedef struct PowerStatBonusData	PowerStatBonusData;
typedef struct PowerTreeClientInfoList PowerTreeClientInfoList;

#include "CharacterClass.h"
#include "GlobalTypeEnum.h"
#include "referencesystem.h"
#include "CombatEvents.h"	// For CombatEvent enum
#include "AttribMod.h"
#include "pvp_common.h"

extern StaticDefineInt AttribTypeEnum[];

// Wrapper for an EArray of AttribMods, and related tracking information
AUTO_STRUCT AST_CONTAINER AST_IGNORE(ppMods);
typedef struct ModArray
{
	AttribMod **ppMods;					AST(NAME(Mods) LATEBIND)
		// EArray of active AttribMods
		//  The PERSIST-style name of this field is AST_IGNORED, as all persistence
		//  of AttribMods is now done through the ppModsSaved field.  However, since
		//  this field is still (currently) sent to SELF (and SUBSCRIBEd), it needs
		//  to be ASTd, so it is given a custom name.

	AttribMod **ppModsPending;			AST(LATEBIND, SERVER_ONLY)
		// EArray of AttribMods pending activation

	AttribMod **ppFragileMods;			NO_AST
		// EArray of AttribMods that can take damage, rebuilt every tick

	AttribMod **ppOverrideMods;			NO_AST
		// EArray of AttribMods that override attributes on the character

	Power **ppPowers;
		// INDEXED Powers the Character owns through AttribMods.  Built by the server as pointers
		//  to the real Powers on the AttribMods; the pointer on the AttribMod is SERVER_ONLY
		//  because AttribMods aren't indexed, so this indexed array is kept and sent to the
		//  client, and the Character's general list is built from here.

	AttribMod **ppModsSaved;			AST(PERSIST NO_TRANSACT LATEBIND FORCE_CONTAINER SERVER_ONLY)
		// The AttribMods we want to save.  Only valid immediately after character_PreSave(), or during
		//  character_PostLoad().

	CharacterAttribute **ppAttributes;	NO_AST
		// TEST: EArray of Attributes

	U32 bHasBasicDisableAffects : 1;
		// True if there are any active Basic kAttribType_Disable mods with Affects expresions in ppMods.
		//  Used to cache that and early exit from testing a specific Power's Disable state.

} ModArray;

AUTO_STRUCT;
typedef struct CombatTrackerNetList
{
	DirtyBit bDirty;					AST(NO_NETSEND)
		// Dirty bit

	U8 id : 7;
		// ID set by server to a non-zero value with there is new data.  Client sets this to
		//  0 when it processes data, so it knows not to reprocess it until the value changes.

	U8 bTouched : 1;					AST(SERVER_ONLY)
		// Flag set by server to know that it's updated the id already this tick

	CombatTrackerNet **ppEvents;
		// EArray of CombatTrackerNet updates, filled in and sent by server using normal send process

	CombatTrackerNet **ppEventsBuffer;	AST(NO_NETSEND)
		// Client: CombatTrackerNet updates, created and destroyed by client so processing can
		//  be handled during normal frames.
		// Server: Delayed CombatTrackerNets. When the delay is over, the event is moved to the regular
		//  EArray to be sent down to the client.

} CombatTrackerNetList;

// Tracks controls to a Character's combat level
AUTO_STRUCT AST_CONTAINER;
typedef struct LevelCombatControl
{
	int iLevelForce;						AST(PERSIST, NO_TRANSACT)
		// Combat level is forced to this value

	EntityRef erLink;
		// Combat level is linked to the combat level of this entity.  Not persisted.

	ContainerID cidLinkPlayer;		AST(PERSIST, NO_TRANSACT)
		// If the erLink refers to a PLAYER entity, this is its container id.  If
		//  the erLink is bad, we can use this to recover.

	U32 bLinkRequiresTeam : 1;		AST(PERSIST, NO_TRANSACT)
		// If this is a Link control, and it requires the target to be teamed

	U32 uiTimestampInDanger;			AST(PERSIST, NO_TRANSACT)
		// The time you entered the "in danger of losing the link" state

	U32 uiTimestampInvalid;			AST(PERSIST, NO_TRANSACT)
		// If the control is invalid, this field has the secondsSince2000()
		//  when it went invalid.  If it goes too long past this time,
		//  the control will be destroyed.

	int iLevelInvalid;				AST(PERSIST, NO_TRANSACT)
		// If the control is invalid, this field has the iLevelCombat when
		//  it went invalid.

	F32 fMaxRange;					AST(PERSIST, NO_TRANSACT)
		// If the link has a max range specified, monitor how far you are from the link

	U32 uiSidekickingPowerID;		AST(SERVER_ONLY)

} LevelCombatControl;

// Tracks a Power* and information required to properly reset the Character's Powers array
typedef struct PowerResetCache
{
	void *pvPower;
		// The pointer to the Power.  Used for pointer comparison and nothing else.

	U32 uiID;
		// The uiID of the Power.  Used to determine if the a still-valid Power*
		//  matching this cache is actually still pointing to the same Power.
		
} PowerResetCache;

// Defines how a Character uses the NearDeath system
AUTO_STRUCT;
typedef struct NearDeathConfig
{
	F32 fChance;					AST(SERVER_ONLY)
		// Chance for Character to enter the NearDeath state when they naturally reach 0 health

	F32 fTime;
		// How long the Character lingers before full death

	U32 ePowerModeRequired;			AST(NAME(PowerModeRequired) SUBTABLE(PowerModeEnum) SERVER_ONLY)
		// The character must have this power mode on before going into the near death state

	const char **ppchBits;			AST(NAME(Bits) POOL_STRING SERVER_ONLY)
		// Bits to play

	const char **pchAnimStanceWords;AST(NAME(StanceWords) POOL_STRING SERVER_ONLY)
		// anim to play

	AttribType eDyingTimeAttrib;	AST(NAME(DyingTimeAttrib), SUBTABLE(AttribTypeEnum), DEFAULT(-1))

} NearDeathConfig;

// Data relevant to a Character in the NearDeath state
AUTO_STRUCT;
typedef struct NearDeath
{
	F32 fTimer;					
		// Seconds.  Countdown linger time.

	EntityRef *perHostileInteracts;
		// list of Entities currently interacting with this NearDeath Character

	EntityRef *perFriendlyInteracts;
		// list of Entities currently interacting with this NearDeath Character
	
	//Last-hit credit info
	// Entering neardeath means that the damage mods that caused our "death" will be cleared out
	//  by the time our actual death occurs. The information is saved here to make things like
	//  PvP kill credit possible.
	EntityRef erKiller;
	EntityRef erKillerSource;
	REF_TO(PowerDef) hKillingBlowDef;	AST(REFDICT(PowerDef))
	int iKillingAttribMod;
	F32 fFatalDamageAmount;
	F32 fFatalDamageAmountNoResist;

} NearDeath;

// Hopefully generalized implementation of key-value point spending
AUTO_STRUCT AST_CONTAINER;
typedef struct CharacterPointSpent
{
	CONST_STRING_MODIFIABLE pchPoint;	AST(PERSIST SUBSCRIBE KEY POOL_STRING_DB)
		// Type of point

	const int iSpent;					AST(PERSIST SUBSCRIBE)
		// Number spent

} CharacterPointSpent;

AUTO_ENUM;
typedef enum CharacterTrainingType
{
	CharacterTrainingType_Give,	
	CharacterTrainingType_Replace,
	CharacterTrainingType_ReplaceEscrow,
} CharacterTrainingType;

extern StaticDefineInt CharacterTrainingTypeEnum[];

AUTO_STRUCT AST_CONTAINER;
typedef struct CharacterTraining
{
	const U32					uiCompleteTime;		AST( PERSIST SUBSCRIBE )
	const U32					uiStartTime;		AST( PERSIST SUBSCRIBE )
	const U32					uiBuyerType;		AST( PERSIST SUBSCRIBE )
	const U32					uiBuyerID;			AST( PERSIST SUBSCRIBE )		
	const U64					uiItemID;			AST( PERSIST SUBSCRIBE )
	REF_TO(PTNodeDef)			hNewNodeDef;		AST( PERSIST SUBSCRIBE )
	REF_TO(PTNodeDef)			hOldNodeDef;		AST( PERSIST SUBSCRIBE )
	const S32					iNewNodeRank;		AST( PERSIST SUBSCRIBE )
	const S32					iRefundAmount;		AST( PERSIST SUBSCRIBE )
	STRING_POOLED				pchRefundNumeric;	AST( PERSIST SUBSCRIBE POOL_STRING )
	const CharacterTrainingType	eType;				AST( PERSIST SUBSCRIBE SUBTABLE(CharacterTrainingTypeEnum) )
	
	U32 bCompletionPending : 1; NO_AST
} CharacterTraining;

// Structure used to track the current PowerActivation Charge stage of another Character,
//  as sent over the network. Used to support "cast bars" - an over-Character UI widget
//  that shows "casting" progress.
AUTO_STRUCT;
typedef struct CharacterChargeData
{
	REF_TO(Message)	hMsgName;
		// The name of the Power being Charged

	F32 fTimeCharge;
		// The time it takes for the Power to complete the Charge stage

	U32 uiTimestamp;
		// When the Charge stage started on the server, using pmTimestamp()

} CharacterChargeData;

AUTO_ENUM;
typedef enum InnateAttribModSource
{
	InnateAttribModSource_None,
	InnateAttribModSource_Power,
	InnateAttribModSource_Item,
	InnateAttribModSource_StatPoint,
} InnateAttribModSource;

AUTO_STRUCT;
typedef struct InnateAttribMod
{
	// The source of the innate attrib mod
	InnateAttribModSource eSource;	AST(SUBTABLE(InnateAttribModSourceEnum) SELF_ONLY)

	// The attribute modified
	AttribType eAttrib;				AST(SUBTABLE(AttribTypeEnum) SELF_ONLY)

	// The aspect of the attribute modified
	AttribAspect eAspect;			AST(SUBTABLE(AttribAspectEnum) SELF_ONLY)

	// If the source is a power or an item, this is the power which creates the innate attrib mod
	REF_TO(PowerDef) hPowerDef;		AST(SELF_ONLY)

	// If the source is an item, this is the item which creates the innate attrib mod
	REF_TO(ItemDef) hItemDef;		AST(SELF_ONLY)

	// The magnitude of the modification
	F32 fMag;						AST(SELF_ONLY)

} InnateAttribMod;
extern ParseTable parse_InnateAttribMod[];
#define TYPE_parse_InnateAttribMod InnateAttribMod

AUTO_STRUCT;
typedef struct InnateAttribModData
{
	// The list of all innate attrib mods
	InnateAttribMod **ppInnateAttribMods;		AST(SELF_ONLY)

	// The dirty bit always comes last
	DirtyBit dirtyBit;							AST(NO_NETSEND)
} InnateAttribModData;
extern ParseTable parse_InnateAttribModData[];
#define TYPE_parse_InnateAttribModData InnateAttribModData

AUTO_STRUCT AST_CONTAINER;
typedef struct Character
{
	Entity *pEntParent;										NO_AST			
		// Backpointer to Entity of the Character


	U32 dirtyID;											AST(SELF_ONLY)
		// Field to notify client that something about this Character is dirty and needs to be dealt with

	U32 dirtyClientMatchID;									AST(CLIENT_ONLY)
		// Field to notify client that something about this Character is dirty and needs to be dealt with
	

	const int iLevelExp;									AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY))
		// ONE-BASED experience level of the character
		// Should only be updated by transactions that modify the "Level" numeric
		// Changes rarely

	int iLevelCombat;
		// ONE-BASED level the Character fights at
		// Changes rarely

	LevelCombatControl *pLevelCombatControl;				AST(PERSIST NO_TRANSACT)
		// If iLevelCombat is being controlled by some system, this describes
		//  what is happening.  If this is NULL, the iLevelCombat is the
		//  natural combat level of the Character (the "xp" level).
		// Changes rarely


	CONST_REF_TO(CharacterClass) hClass;					AST(PERSIST SUBSCRIBE REFDICT(CharacterClass))
		// Class of the character
		// Changes rarely

	REF_TO(CharacterClass) hClassTemporary;					AST(REFDICT(CharacterClass))
		// Temporary non-persisted override of Class
		// Changes rarely

	U32 *puiTempClassPowers;								AST(SERVER_ONLY)
		// Powers added to player though assigned temporary class

	// character paths
	// YOU MUST CALL entity_GetEntityCharacterPaths() to aggregate all paths the character owns!

	// Primary path, often chosen at character creation.
	CONST_REF_TO(CharacterPath) hPath;						AST(PERSIST SUBSCRIBE REFDICT(CharacterPath))
	// These paths are purchased AFTER character creation.
	CONST_EARRAY_OF(AdditionalCharacterPath) ppSecondaryPaths;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER)

	CONST_REF_TO(SpeciesDef) hSpecies;						AST(PERSIST SUBSCRIBE REFDICT(Species))
	CONST_OPTIONAL_STRUCT(CustomSpecies) pCustomSpecies;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER LATEBIND)
		// Species of the character
		// Changes rarely


	// Powers

	const U32 uiPowerIDMax;									AST(PERSIST SELF_ONLY)
		// The current highest Power ID
		// Changes rarely

	U32 uiTempPowerIDMax;									NO_AST
		// The current highest Power ID for temporary (aka non-transacted) Powers.  Resets every mapmove, etc.

	CONST_EARRAY_OF(Power) ppPowersPersonal;				AST(PERSIST SELF_ONLY LATEBIND FORCE_CONTAINER)
		// INDEXED Powers the Character owns directly
		// Changes rarely

	CONST_EARRAY_OF(Power) ppPowersClass;					AST(PERSIST SELF_ONLY LATEBIND FORCE_CONTAINER)
		// INDEXED Powers the Character owns through its Class
		// Changes rarely

	CONST_EARRAY_OF(Power) ppPowersSpecies;					AST(PERSIST SELF_ONLY LATEBIND FORCE_CONTAINER)
		// INDEXED Powers the Character owns through its Species
		// Changes rarely

	Power **ppPowersTemporary;								AST(SELF_ONLY, LATEBIND, FORCE_CONTAINER)
		// INDEXED earray of powers the character temporarily owns
		
	Power **ppPowersPropagation;							AST(SELF_ONLY, LATEBIND, FORCE_CONTAINER, NO_INDEX)
		// INDEXED Powers the character owns though propagation
		// Removing Index because it doesn't seem to work with entity sending. 
		// Changes rarely

	Power **ppPowersEntCreateEnhancements;					NO_AST
		// NON-INDEXED All the enhancement powers that can possibly be applied to entCreated or enfProjectile created critters


	CharacterPowerSlots *pSlots;							AST(PERSIST SOMETIMES_TRANSACT SELF_ONLY LATEBIND FORCE_CONTAINER)
		// Structure that tracks the Character's slotted Powers.  Essentially a wrapper for an
		//  EArray and index into the EArray.  Created on demand.
		// Changes rarely

	PowerSlotSet *pSlotSetBecomeCritter;					AST(SELF_ONLY)
		// Tracks slotted Powers when the Character is affected by a BecomeCritter AttribMod

	Power **ppPowers;										NO_AST
		// INDEXED All Powers the Character has access to through various means

	Power **ppPowersLimitedUse;								NO_AST
		// NON-INDEXED EArray of any Power anywhere on the Character that has bLimitedUse set on its PowerDef
		//  Note that this can include Powers that aren't actually in the accessible Powers array (eg Powers
		//  on broken or unequipped items)

	PowerResetCache **ppPowersResetCache;					NO_AST
		// Earray of PowerResetCache data for use in character_ResetPowersArray

	CombatReactivePowerInfo	*pCombatReactivePowerInfo;
		// An optional struct, if the character has a reactive power

	CombatPowerStateSwitchingInfo *pCombatPowerStateInfo;			AST(SELF_ONLY)

	// Power Trees

	CONST_EARRAY_OF(PowerTree) ppPowerTrees;						AST(PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER USERFLAG(TOK_PUPPET_NO_COPY))
		// INDEXED Actual PowerTrees the Character owns
		// Changes rarely

	PowerTreeClientInfoList *pClientPowerTreeInfo;					AST(SUBSCRIBE)
		// Minimal power tree information sent to client. Only power trees marked with a special flag go into this list.

	CONST_EARRAY_OF(CharacterPointSpent) ppPointSpentPowerTrees;	AST(PERSIST SUBSCRIBE SELF_ONLY)
		// INDEXED EArray of points spent by the Character on PowerTree stuff
		// Changes rarely


	const U32 uiPowerTreeModCount;									AST(PERSIST SUBSCRIBE SELF_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
		// This consists of two numbers:
		// 1.) The lower bits represent the current PowerTree version that is incremented every time 
		// a power tree is modified. This informs containers that subscribe to this entity that it has been changed.
		// 2.) The upper bits represent the current "full respec" version for game-wide PowerTree resets. When this
		// number is incremented, the lower bits are reset.

	const U32 uiLastFreeRespecTime;									AST(PERSIST SELF_ONLY)
		// The last time this character used a free respec (All)

	const U32 uiLastForcedRespecTime;								AST(PERSIST SERVER_ONLY)
		// The last time this character used a forced respec (All)

	CONST_INT_EARRAY eaiTypeLastFreeRespecTime;						AST(PERSIST SELF_ONLY)
		// The last time this character used a free by type (see PTRespecGroupType)

	// Attrib purchasing (only in games that support it)

	CONST_EARRAY_OF(AssignedStats) ppAssignedStats;			AST(PERSIST SELF_ONLY FORCE_CONTAINER)
		// Actual stats that the character has purchased
		// Changes rarely

	CONST_EARRAY_OF(SavedAttribStats) ppSavedAttribStats;	AST(PERSIST SUBSCRIBE SERVER_ONLY FORCE_CONTAINER)
		// Used to save the value of AttribStats
		// Changes rarely
		// TODO: This needs to be fixed to not use POOL_STRING

	CONST_STRING_POOLED	pchCurrentAttribStatsPreset;		AST(PERSIST SELF_ONLY NAME(pchAttribStatsPreset,pchCurrentAttribStatsPreset) POOL_STRING)
		// The current "preset" being used, which should reside in ppSavedAttribStats
		// Changes rarely

	CONST_EARRAY_OF(SavedAttribute) ppSavedAttributes;		AST(PERSIST SUBSCRIBE SERVER_ONLY  FORCE_CONTAINER)
		// Used to save the value of Attributes that aren't hardcoded as persisted



	// Attributes, AttribMods and the innate accruals

	CharacterAttribs *pattrBasic;							AST(PERSIST NO_TRANSACT SUBSCRIBE LATEBIND FORCE_CONTAINER)
		// Character's current basic attributes (actual values for hp, stats, cc status, etc)
		//  Some subset of this still needs to be communicated to client, so until we come up with
		//  a better way of sending these, we send the whole thing (though most fields are SELF_ONLY).
		// Some fields change often, has a dirty bit

	PowerStatBonusData *pPowerStatBonusData;				AST(SELF_ONLY)
		// The list of power stat bonuses from each attribute
		// gConf.bUsePowerStatBonuses must be enabled for the game client to receive this information

	InnateAttribModData *pInnateAttribModData;				AST(SELF_ONLY)
		// The list of innate attrib mods originating from items, powers and stat points
		// gConf.bSendInnateAttribModData must be enabled for the game client to receive this information

	PowerApplyStrength **ppApplyStrengths;					NO_AST
		// If the Character has its strengths locked (generally a pet with locked strength), this
		//  is where the strengths are stored

	ModArray modArray;										AST(PERSIST NO_TRANSACT SUBSCRIBE SELF_ONLY)
		// Tracks all the AttribMods on this character
		// Changes often

	AttribModNet **ppModsNet;
		// EArray of AttribModNet structures sent to clients
		// Changes often, has a dirty bit

	U32 uiTimestampModsNet;									NO_AST
		// SecondsSince2000 of last time the durations of the AttribModNets in ppModsNet were sent/received

	AttribAccrualSet *pInnateAccrualSet;					AST(SELF_ONLY)
		// Character's accrued attributes from powers, stats, items, etc.  This is allocated using
		//  StructFoo, NOT MemoryPool, so it can be properly AST'd.
		// Changes rarely (depending on game), has a dirty bit

	AttribAccrualSet *pInnateEquipAccrualSet;				NO_AST
		// Character's accrued attributes from equipment

	AttribAccrualSet *pInnatePowersAccrualSet;				NO_AST
		// Character's accrued attributes from Innate powers
		// Points to a set in a hashtable

	AttribAccrualSet *pInnateStatPointsSet;					NO_AST
		// Character's accrued attributes from stats.


	// Training (only in games that support it)

	CONST_EARRAY_OF(CharacterTraining) ppTraining;			AST(PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER)


	// CombatEvents, PowerModes

	CombatEventState *pCombatEventState;					AST(SERVER_ONLY)
		// State of the Character's CombatEvents and related data

	int *piPowerModes;
		// Current PowerModes on a Character, regenerated every tick, sorted
		// Changes sometimes to often, might be SELF_ONLY-able?

	PowerModes** ppPowerModeHistory;						AST(SERVER_ONLY)
		// Recent PowerModes on Character with timestamps, copied from piPowerModes, used for prediction


	// Tracking for various Power states (recharging, active passives and toggles)
	
	CooldownTimer **ppCooldownTimers;						AST(PERSIST NO_TRANSACT SERVER_ONLY FORCE_CONTAINER)
		// List of cooldown timers

	CooldownRateModifier **ppSpeedCooldown;					AST(SELF_ONLY)
		// Affects the rate of cooldown for each power category (calculated from special mods)
		// This includes the innate cooldown

	PowerRef **ppPowerRefRecharge;							NO_AST
		// Tracks recharging powers

	PowerRef **ppPowerRefChargeRefill;						NO_AST
		// Tracks refilling charges on powers

	PowerActivation **ppPowerActPassive;					NO_AST
		// Tracks the list of running passive powers

	PowerActivation **ppPowerActToggle;						NO_AST
		// Tracks the list of running toggle powers

	PowerActivation **ppPowerActAutoAttackServer;			NO_AST
		// Tracks the list of running toggle autoattack powers

	PowerActivation **ppPowerActInstant;					NO_AST

	F32 fCooldownGlobalTimer;								NO_AST
		// Tracks the current value of the global cooldown

	CombatAdvantageNode **ppCombatAdvantages;

	U32 uiScheduledRootTime;								NO_AST
	U32 uiScheduledHoldTime;								NO_AST
		// used for client prediction when roots and holds attributes will actually become active
	
	// Power Activation
	
	PowerActivation *pPowActFinished;						NO_AST
		// The power the character most recently finished activating

	PowerActivation *pPowActCurrent;						NO_AST
		// The power the character is currently activating

	PowerActivation *pPowActQueued;							NO_AST
		// The power the character is currently waiting to activate

	PowerActivation *pPowActOverflow;						NO_AST
		// The power the character is currently waiting to activate AFTER the queued power.
		//  Used only on the server to account for latency in the prediction system.

	int eChargeMode;										NO_AST
		// Tracks which power the character is currently charging

	U32 uiPowerActSeq;										NO_AST
		// Track's the Character's PowerActivation seq number

	U8 uchPowerActSeqReset;									NO_AST
		// If non-zero, indicates the reset mechanism is enabled for uiPowerActSeq

	U8 uchPowerActSeqResets;								NO_AST
		// Number of times uchPowerActSeqReset has been used, which may cause the
		//  server to start ignoring the client if it gets too high (implies cheating)

	CharacterChargeData *pChargeData;						AST(NO_NETSEND)
		// Tracks data relevant to Charging, for displaying to the client, sent through the Entity Bucket

	PowerSubtargetChoice *pSubtarget;						AST(SELF_ONLY)
		// Tracks what the Character is subtargeting
		// Changes sometimes (only in games with Subtargeting)

	
	// Animation and Movement
		
	PowerRef *pPowerRefStance;								NO_AST
		// PowerRef that the Character is using as a stance

	PowerRef *pPowerRefPersistStance;						NO_AST
		// PowerRef that the Character is using as a persist stance

	REF_TO(PowerEmit) hPowerEmitStance;						NO_AST
		// PowerEmit associated with the stance

	const char *pchRefStanceStickyFX;						NO_AST
		// The first sticky FX in the list of FX used for this stance

	const char *pchRefPersistStanceStickyFX;				NO_AST
		// The first sticky FX in the list of FX used for this persist stance

	const char *pchRefPersistStanceStickyBits;				NO_AST
		// The first sticky bits in the list of bits used for this persist stance

	U32 uiStancePowerAnimFXID;								NO_AST
		// ID passed to the PowerAnimFX system to enter the stance, saved so we can exit properly

	U32 uiPersistStancePowerAnimFXID;						NO_AST
		// ID passed to the PowerAnimFX system to enter the persist stance, saved so we can exit properly

	REF_TO(PowerDef) hPowerDefStanceDefault;				AST(SELF_ONLY)
		// PowerDef that the Character's stance drops into by default
		// Changes rarely

	MovementRequester*	pPowersMovement;					NO_AST
		// The thing that handles powers-specific movement

	
	// Damage tracking

	DamageTracker **ppDamageTrackersTickIncoming;			NO_AST
		// This is a temp array of the damage that happened to me this tick
		//  The damage done here may not resolve because the target is immune

	DamageTracker **ppDamageTrackersTickOutgoing;			NO_AST
		// This is a temp array of the damage that I dealt
		//  The damage done here is also applied to any fragile mods I have

	DamageTracker **ppDamageTrackers;						NO_AST
		// The damage done here actually resolved and type does not matter

	CombatTrackerNetList combatTrackerNetList;
		// The damage tracker net updates
		// Changes often, has dirty bit

	PowerSubtargetNet **ppSubtargets;						AST(SERVER_ONLY)
		// The status of the Character's PowerSubtargets
		// Changes sometimes (only in games with subtargets)

	EntityRef erRingoutCredit;								AST(SERVER_ONLY)
		// When a character knocks another character into a kill volume
		// this character will get credit with the kill



	// Targeting

	EntityRef currentTargetRef;
		// The Entity currently targeted
		//  An EntCreate will set this to the target of the owner
		// Changes often

	EntityRef erTargetDual;
		// The non-Foe Entity currently targeted
		//  Only used by Players, and only when dual targeting is enabled
		// Changes often

	EntityRef erTargetFocus;
		// The Entity currently set as this character's focus
		// Only used by Players
		// Changes often

	REF_TO(WorldInteractionNode) currentTargetHandle;
		// InteractionNode key if targeting an InteractionNode instead of an Entity
		// Changes often

	int targetChangeID;										AST(SELF_ONLY)
		// change id used to coordinate client and server modifications
		// Changes often

	int focusTargetChangeID;
		// change id used to coordinate client and server modifications
		// Changes often

	U32 *perUntargetable;									AST(SELF_ONLY)
		// EntityRefs that this Character is not allowed to target, regenerated every tick, sorted
		// Changes rarely

	U32 *perHidden;											AST(SELF_ONLY)
		// EntityRefs that are hidden from the Character, regenerated every tick, sorted
		// Changes rarely

	CharacterAITargetInfo **ppAITargets;
		// Entity Ref and AI status info for entities this critter considers legal targets (to avoid sending down AIBase)
		// Changes often

	EntityRef erProxAssistTaget;								NO_AST
		// The Entity that is currently targeted by the proximity targeting
		// client-only
		

	// Object Holding (move into a substruct?)

	REF_TO(WorldInteractionNode) hHeldNode;					AST(SELF_ONLY)
		// WorldInteractionNode of held object (which no longer exists as an entity),
		//  NULL if not holding anything
		// Changes sometimes

	EntityRef erHeld;										AST(SELF_ONLY)
		// EntityRef of an Entity being held in some way (which still exists as an entity),
		//  0 if not holding anything
		// Changes sometimes

	F32 fHeldMass;											AST(SELF_ONLY)
		// Cached mass of held node, 0 if not holding anything or holding an entity
		// Changes sometimes

	EntityRef erHeldBy;
		// EntityRef of Entity that is holding this character in some way, 0 if not being held
		// Changes sometimes

	const char **ppchHeldFXNames;							AST(SERVER_ONLY, POOL_STRING)
		// Simple way to track names of FX being used to represent held objects.  Can get
		//  rid of this if we come up with a better way to track what you're holding.

	F32 fHeldHealth;										NO_AST
		// Health of held object (which no longer exists as an entity)


	// Regeneration (Will be removed at some point)
	
	F32 *pfTimersAttribPool;								AST(SERVER_ONLY)
		// EArray of timers for regen/decay of AttribPools.  Allocated when needed.

	F32 fTimerRegeneration;									NO_AST
		// Seconds.  How long until the next regen tick for the character

	F32 fTimerRecovery;										NO_AST
		// Seconds.  How long until the next recovery tick for the character

	bool bCanRegen[3];										NO_AST
		// Custom code for overriding stats. 0-Health Regen 1-Power 2-Breath


	// PvP

	PVPDuelState *pvpDuelState;								
	PVPFlag *pvpFlag;										
	U32 gangID;
		// Gang.  Overrides faction. Used in controlling critter behavior and in PvP
	PVPTeamFlag *pvpTeamDuelFlag;

	U32 uLimitedUseTimestamp; NO_AST
		// Holds last time limited use data was sent to the client

	// Utility fields

	NearDeath *pNearDeath;
		// Data about the Character's NearDeath state - if it exists the Character is NearDeath

	F32 fTimeToLingerOverride;								NO_AST
		// Seconds. Override for time to linger upon death.  Only used when bUseDeathOverrides is true.

	EntityRef primaryPetRef;
		// The ref of this character's primary pet
		// Changes rarely

	U32 uiTimeCombatExit;									AST(SELF_ONLY)
		// Timestamp.  When the character will no longer be in combat.  If 0, the
		//  character is not currently in combat.
		// Changes often, could be NO_AST?

	U32 uiTimeCombatVisualsExit;							AST(SELF_ONLY)
		// Timestamp.  A clone of uiTimeCombatExit that works ONLY on visuals, for itemArt, anims, etc, so that we can perform powers
		// as if we were in combat, but not really be in combat
		// Changes often, could be NO_AST?

	U32 uiTimeBattleForm;									AST(SELF_ONLY)
		// Timestamp.  When the Character is allowed to toggle their BattleForm state.

	U32 uiConfuseSeed;										AST(SELF_ONLY)
		// The seed used for randomly generating when targets should be switched
		// during a confuse
		// Changes rarely (except when confused?)

	U32 uiTimeLoggedOutForCombat;							NO_AST
		// Time this character was logged out. This will be added to the tick rate of attribmods
		// that are flagged bProcessOfflineTimeOnLogin during the character's first combat tick,
		// after which it will be cleared. Also can affect power recharge.

	U32 iFreeRespecAvailable;								AST(SELF_ONLY)
		// set to the time of the oldest respec or one closest in the future

	EARRAY_OF(Item) ppAutoExecItems;						AST(SERVER_ONLY)
		// Array of items that will apply their powers on the next combat tick

	F32 fTimerSleep;										NO_AST
		// Set to the amount of time the combat system thinks it can get away
		//  with not running combat ticks on this Character.

	F32 fTimeSlept;											NO_AST
		// Accumulated time the Character has slept through since it was last awake

	F32 fLastFallingImpactSpeed;							NO_AST
		// This value is stored only in the case where combat config
		// is set to use a special power for falling damage.

	U32 uiDeathCollisionTimer;								NO_AST
		// used for delaying the disabling of the character's collision for a short time after death

	F32 fRollWaitingTimer;									NO_AST
		// Debug timer to track how long the player has been waiting to roll

	U32 uiPowersCreatedEntityTime;							AST(SERVER_ONLY)
		// process count of when this entity was created. 
		// set if the character was created via a power's entCreate, projectileCreate
		
	S32 uiComboMispredictHeuristic;							NO_AST
		// a count of how many times this character has mispredicted

	// Utility flags

	U32 bIsRooted : 1;
		// Sent to clients, the actual state flag when rooted. 
		// We cannot reliably check attribBasic anymore as we are client predicting roots

	U32 bIsHeld : 1;
		// Sent to clients, the actual state flag when held. 
		// We cannot check attribBasic anymore as we are client predicting holds
		

	U32 bLoaded : 1;										NO_AST
		// If the Character has made it through character_LoadNonTransact(), and thus been loaded from the DB
	
	U32 bGiveRewardsOverride : 1;							NO_AST
		// Character should give rewards upon death.  Only used when bUseDeathOverrides is true.

	U32 bGiveEventCreditOverride : 1;						NO_AST
		// Character should give event credit upon death.  Only used when bUseDeathOverrides is true.

	U32 bUseDeathOverrides : 1;								NO_AST
		// Character should use death override values (fTimeToLingerOverride, bGiveRewardsOverride, bGiveEventCreditOverride)

	U32 bSkipAccrueMods : 1;								NO_AST
		// Character can skip accruing AttribMods

	U32 bCombatTick : 1;									NO_AST
		// Character is in the middle of a combat tick and is not sleeping

	U32 bAutoReapplyPassives : 1;							NO_AST
		// Character has Passives running with the bAutoReapply flag

	U32 bAutoReapplyToggles : 1;							NO_AST
		// Character has Toggles running with the bAutoReapply flag

	U32 bKill : 1;											NO_AST
		// Character should die next combat tick

	U32 bResetPowersArray : 1;								NO_AST
		// Set to true to request a reset to the Character's Powers array for the following situations:
		//  Reset from inside a transaction callback
		//  Reset when an immediate attempt might not be accurate (like inside mod accrual)
		//  Reset might occur multiple times during processing
		// The reset from this flag is processed both at the beginning of the Character's next TickPhaseOne
		//  and at the end of the Character's next TickPhaseThree

	U32 bTauntActive : 1;									NO_AST
		// Character has an active taunt on them

	U32 bAutoAttackServer : 1;								NO_AST
		// Character wants AutoAttackServer Powers active

	U32 bAutoAttackServerCheck : 1;							NO_AST
		// Character needs to check for new AutoAttackServer Powers

	U32 bStanceDefaultItem : 1;								NO_AST
		// Set to true when the Character sets their default stance from an item

	U32 bPersistStanceInactive : 1;							NO_AST
		// Set to true when the Character is using their inactive persistent stance

	U32 bInvulnerable : 1;
		// Character is immune to damage of any kind
		// Changes rarely

	U32 bUnstoppable : 1;
		// Character is immune to status effects
		// Changes rarely

	U32 bUnkillable : 1;
		// Character can not die from normal means
		// Changes rarely

	U32 bLevelAdjusting : 1;
		// This Character applies Powers at the combat level of the target Character,
		//  and other Characters apply Powers to this Character at its combat level
		//  if they are higher level.  Implies this Character doesn't use combat mods.

	U32 bSafe : 1;
		// This Character can not be affected by foes, and can only be affected by
		//  non-foes that are also Safe.

	U32 bBattleForm : 1;									AST(SELF_ONLY)
		// Character has switched into their battle form, rather than their civilian form.
		//  Toggling this can trigger a faction change (at the Powers level) and costume
		//  slot change.
		// Changes sometimes

	U32 bUsingDoor : 1;										NO_AST
		// Character is using a door, and is invulnerable and unstoppable

	U32 bDisableFaceActivate : 1;
		// True if the Character should never turn to face for a power activation
		// Changes rarely

	U32 bDisableFaceSelected : 1;							AST(SELF_ONLY)
		// True if the Character should not automatically turn toward his target
		// Changes rarely

	U32 bFaceSelectedIgnoreTarget : 1;						NO_AST
		// Makes selected facing pretend the entity has no selected target.
		//  Similar in effect but different than bDisableFaceSelected.
		//  Used by players to stop facing an offscreen target
		//  Used by AI that isn't running combat, but has an attack target

	//	U32 bFaceSelectedIgnoreTargetSystem : 1;
		// If set, will not turn torso to face target. Used to disable turn to face in a power anim
		// Changed by powers
		// JW: Commented out for now because it's useless

	U32 bRequireValidTarget : 1;							AST(SELF_ONLY)
		// True if the Character should perform valid target checks in the powers system (CombatConfig override)
		// Changes rarely

	U32 bUseCameraTargeting : 1;							AST(SELF_ONLY)
		// True if the character should use camera targeting for power activations (CombatConfig override)
		// Changes rarely

	U32 bDisablePowerQueuing : 1;							AST(SELF_ONLY)
		// True if powers should not queue
		// Changes rarely

	U32 bShooterControls : 1;								AST(SELF_ONLY)
		// Whether or not the Character is using shooter controls
		// Changes rarely

	U32 bIsAiming : 1;										NO_AST
		// Whether or not the Character is aiming
		// Changes rarely
	
	U32 bIsCrouching : 1;									NO_AST
		// Whether or not the Character is crouching
		// Changes rarely
	
	U32 bIsRolling : 1;										NO_AST
		// Whether or not the Character is rolling
		// Changes rarely
		
	U32 bIsWaitingToRoll : 1;								NO_AST
		// Whether or not the Character has a roll queued
		// Changes rarely

	U32 bIsPrimaryPet : 1;
		// Is the primary pet of erOwner
		// Changes rarely
		
	U32 bModsOwnedByOwner : 1;
		// If any AttribMods created by the Character's Powers are owned by the Character's
		//  erOwner instead of the Character.

	U32 bBecomeCritter : 1;
		// Character has temporarily become a critter, and therefore the
		//  only Powers available are from AttribMods.  We may eventually
		//  want to make this a flag system, but for now this is easiest
		// Changes rarely

	U32 bBecomeCritterTickPhaseTwo : 1;						NO_AST
		// Character bBecomeCritter was true last tick, but now we're in
		//  the middle of a new TickPhaseTwo, so bBecomeCritter is false,
		//  but we may still need to do work (such as accumulating innates)
		//  that needs a consistent answer.

	U32 bHasAvailableResearch : 1;							AST(CLIENT_ONLY)
		// Does the character have available research for this client?

	U32 bLimitedUseDirty : 1;								NO_AST
		// Used to denote that the limited use entity bucket info needs to get sent

	U32 bChargeDataDirty : 1;								NO_AST
		// Used to denote that the charge data entity bucket info needs to get sent

	U32 bUpdateFlightParams : 1;							NO_AST
		// Indicates we need to re-call pmSetFlightEnabled() because a non-Attribute
		//  flight behavior changed while flying.

	U32 bTacticalAimDisabledByCost : 1;						NO_AST
		// used to indicate that the tactical requester was disabled due to the associated attrib being depleted
	
	U32 bTacticalRollDisabledByCost : 1;					NO_AST
		// used to indicate that the tactical requester was disabled due to the associated attrib being depleted

	U32 bPowerActivationImmunity : 1;						NO_AST
		// when activating a power that has the ActivationImmunity flag set
			
	U32 bSpecialLargeMonster : 1;			
		// if set, handles a variety of special cases for combat. I consider this fairly hacky.
		// when calculating the combatPosition from the entity capsule do not clamp the position to within the entity collision capsule. 
		// for lurches and lunges, the combat position becomes the target.
		// for teleports, will always teleport to in front of the character
		
	// Temporary EArrays & data

	PowerActivationState **ppActivationState;			AST(PERSIST NO_TRANSACT SERVER_ONLY SELF_ONLY FORCE_CONTAINER)
		// Used to save the state of the PowerActivations
		// SELF_ONLY so that it is excluded from subscription

	AttribMod **ppModsAttribModFragilityHealth;			NO_AST
		// Temporary EArray of AttribModFragilityHealth AttribMods

	AttribMod **ppModsAttribModFragilityScale;			NO_AST
		// Temporary EArray of AttribModFragilityScale AttribMods

	AttribMod **ppModsAttribModShieldPercentIgnored;	NO_AST
		// Temporary EArray of AttribModShieldPercenttIgnored AttribMods

	AttribMod **ppModsDamageTrigger;					NO_AST
		// Temporary EArray of damage trigger AttribMods

	AttribMod **ppModsHeal;								NO_AST
		// Temporary EArray of heal AttribMods

	AttribMod **ppModsShare;							NO_AST
		// Temporary EArray of share AttribMods

	AttribMod **ppModsShield;							NO_AST
		// Temporary EArray of shield AttribMods to absorb damage

	AttribMod **ppModsTaunt;							NO_AST
		// Temporary EArray of taunt AttribMods.  If bTauntActive is true, the first element is the active taunt.

	AttribMod **ppModsAIAggro;							NO_AST
		// Temporary EArray of AI Aggro AttribMods.

	AttribMod **ppCostumeChanges;						NO_AST
		// Temporary EArray of costume changes applied to a character
	
	AttribMod **ppCostumeModifies;						NO_AST
		// Temporary EArray of costume modifies applied to a character

	AttribMod **ppRewardModifies;						NO_AST
		// Temporary EArray of reward system modifiers on a character

	F32 fPowerShieldRatio;								NO_AST
		// Current best power shield ratio on the characters

	U32 *puiPowerIDsSaved_Obsolete;						AST(PERSIST NO_TRANSACT SERVER_ONLY ADDNAMES(eaSavedPowersIDs,puiPowerIDsSaved))
		// This list is now obsolete. Saved IDs are now stored in the SavedTray structure.

	U32 uiPredictedDeathTime;							AST(SERVER_ONLY)
		// the time at which the death has been predicted for this entity, only used if GlobalConfig bCombatDeathPrediction is enabled
		
	const char *pcSwingingFX;							AST(POOL_STRING)
		// The current swinging fx

	DirtyBit dirtyBit;									AST(NO_NETSEND)
} Character;
extern ParseTable parse_Character[];
#define TYPE_parse_Character Character

AUTO_STRUCT;
typedef struct CharacterPreSaveInfo
{
	TempAttributes *pTempAttributes;
} CharacterPreSaveInfo;


// Simple structure to send down the complete Power recharge state
AUTO_STRUCT;
typedef struct PowerRechargeState
{

	U32 *puiIDs;
		// IDs of Powers that are recharging

	F32 *pfTimes;
		// fTimeRecharge for each recharging Power

} PowerRechargeState;



// Special structure sent to client after loading into a map, which contains data to initialize
//  some Character state which isn't normally sent from the server.
AUTO_STRUCT;
typedef struct ClientCharacterInitData
{

	PowerActivationState **ppActivationStateToggle;
		// Activation state of the Character's active toggle powers

	PowerRechargeState rechargeState;
		// The recharge state of all powers

	CooldownTimer **ppCooldownTimers;
		// The cooldown timers for all the power categories. 

} ClientCharacterInitData;




extern bool g_bDebugStats;

#define DBGSTATS_printf(format, ...) if(g_bDebugStats) printf(format,__VA_ARGS__)


// Misc macros
#define character_GetTarget(iPartitionIdx,pchar) entFromEntityRef(iPartitionIdx,(pchar)->currentTargetRef)
#define character_GetClassCurrent(pchar) (IS_HANDLE_ACTIVE((pchar)->hClassTemporary) ? GET_REF((pchar)->hClassTemporary) : GET_REF((pchar)->hClass))

__forceinline static void character_Wake(SA_PARAM_NN_VALID Character *pchar)
{
	pchar->fTimerSleep = 0;
}

__forceinline static void character_SetSleep(SA_PARAM_NN_VALID Character *pchar, F32 fTime)
{
	MIN1(pchar->fTimerSleep,fTime);
}

// Character initialization/destruction

// Takes an existing character and resets all their state (recharging and active powers, mods, pets, hp, etc)
void character_Reset(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Entity *e, GameAccountDataExtract *pExtract);

// Takes an existing character and resets the bits you want (subset of the above, even if all are true)
void character_ResetPartial(int iPartitionIdx, Character *pchar, Entity *e, int mods, int unownedModsOnly, int act, int recharge, int period, int status, GameAccountDataExtract *pExtract);

// Automatically spend the character's attrib stat points based on the character class
void character_AutoSpendStatPoints(Character *pchar);

// This performs part of the character_Cleanup() function.  I have no idea why it's
//  split specifically along the lines it is.
void character_CleanupPartial(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Cleans up all temporarily allocated data on a character, the docleanup parameter is passed in when changing partitions and will result in all mods being cleaned up and rebuilt
void character_Cleanup(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, bool isReloading, GameAccountDataExtract *pExtract, bool bDoCleanup);

// Performs various sanity checks on a Character
void character_SanityCheck(SA_PARAM_NN_VALID Character *pchar);

// assumes the character is already going to die for whatever reason.
// returns true if the character meets the conditions to enter the nearDeath state
bool character_MeetsNearDeathRequirements(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID NearDeathConfig *pNearDeathConfig);

F32 character_NearDeathGetMaxDyingTime(Character *pchar, NearDeathConfig *pNearDeathConfig);

// Forces a Character into the NearDeath state, if they're alive and not already NearDeath.
// If the timer is negative, the NearDeath state is forever.  If the timer is 0 it uses the
//  default timer for the particular Character (if the Character doesn't normally go into
//  NearDeath, the default timer is forever).  Otherwise the NearDeath state will
//  use the specified timer.
// Returns the resulting timer value (-1 for forever), or 0 if something went wrong.
F32 character_NearDeathEnter(int iPartitionIdx,SA_PARAM_NN_VALID Character *pchar, F32 fTimerOverride);

void character_NearDeathExpire(SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Character part of entity dying
void character_Die(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fTimeToLinger, int bGiveRewards, int bGiveKillCredit, GameAccountDataExtract *pExtract);

void character_NearDeathRevive(SA_PARAM_NN_VALID Character *pchar);
// called when a player is in nearDeath and is now dead. Triggers the combatEvent kCombatEvent_NearDeathDead
void character_TriggerCombatNearDeathDeadEvent(int iPartitionIdx, Character *pchar);


// Find the Entity that owns the damage that is considered the killing blow on a dead Character.
//  Optionally returns the entire DamageTracker.
SA_RET_OP_VALID Entity *character_FindKiller(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID DamageTracker **ppDamageTrackerOut);

// Adds an event to the Character's CombatEvent trackers
__forceinline static U8 character_CombatEventTrack(SA_PARAM_NN_VALID Character *pchar,
												   CombatEvent eEvent)
{
	U8 bHasTriggerComplex = false;
	if(!pchar->pCombatEventState)
		pchar->pCombatEventState = combatEventState_Create();
	pchar->pCombatEventState->auiCombatEventCountCurrent[eEvent]++;

	if (pchar->pCombatEventState->abCombatEventTriggerComplex[eEvent])
	{
		bHasTriggerComplex = true;
		character_Wake(pchar);
	}
	else if (pchar->pCombatEventState->abCombatEventTriggerSimple[eEvent])
	{
		character_Wake(pchar);
	}

	return bHasTriggerComplex;
}


// Powers work - fixing after load, adding and removing

// Cleans up the Character's personal Powers list
void character_FixPowersPersonalHelper(SA_PARAM_NN_VALID NOCONST(Character) *pchar);

// Adds an existing Power to the Character's general list, calls
//  the relevant functions to handle the side-effects.  Returns
//  false if something went wrong.
int character_AddPower(int iPartitionIdx,
					   SA_PARAM_NN_VALID Character *pchar,
					   SA_PARAM_NN_VALID Power *ppow,
					   PowerSource eSource,
					   GameAccountDataExtract *pExtract);


// Marks the Character's attribs as dirty, necessary for any out-of-tick modifications
#define character_DirtyAttribs(pchar) character_DirtyAttribs_dbg(pchar MEM_DBG_PARMS_INIT)
void character_DirtyAttribs_dbg(SA_PARAM_NN_VALID Character *pchar MEM_DBG_PARMS);

// Marks the system that owns the Power as dirty on the Character, so the Power's data is sent
#define character_DirtyPower(pchar, ppow) character_DirtyPower_dbg(pchar, ppow MEM_DBG_PARMS_INIT)
void character_DirtyPower_dbg(SA_PARAM_NN_VALID Character *pchar,
						  SA_PARAM_NN_VALID Power *ppow MEM_DBG_PARMS);

// Marks the Character's PowerTrees as dirty.  Used by Powers to make sure their data gets sent when they change.
#define character_DirtyPowerTrees(pchar) character_DirtyPowerTrees_dbg(pchar MEM_DBG_PARMS_INIT)
void character_DirtyPowerTrees_dbg(SA_PARAM_NN_VALID Character *pchar MEM_DBG_PARMS);

// Marks the Character's Inventory as dirty.  Used by Powers to make sure their data gets sent when they change.
#define character_DirtyItems(pchar) character_DirtyItems_dbg(pchar MEM_DBG_PARMS_INIT)
void character_DirtyItems_dbg(SA_PARAM_NN_VALID Character *pchar MEM_DBG_PARMS);

// Returns a Power owned generally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByRef(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerRef *ppowref);

// Returns a Power owned generally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByRefComplete(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerRef *ppowref);

// Returns a Power owned generally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByDef(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef);

// Returns a power owned generally by the character or the parent power if the supplied def is a combo.
SA_RET_OP_VALID Power *character_FindComboParentByDef(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef);

// Returns a Power owned internally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByDefPersonal(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef);

// Returns a Power temporarily owned by the Character, otherwise returns NULL.  Looks through ppPowersTemporary
SA_RET_OP_VALID Power *character_FindPowerByDefTemporary(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef);

// Returns a Power owned generally by the Character, otherwise returns NULL.
//  If the Character owns multiple instances of the PowerDef, it finds the newest one
SA_RET_OP_VALID Power *character_FindNewestPowerByDef(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef);

// Returns a Power owned internally by the Character, otherwise returns NULL.
//  If the Character owns multiple instances of the PowerDef, it finds the newest one
SA_RET_OP_VALID Power *character_FindNewestPowerByDefPersonal(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerDef *pdef);

// Returns a Power owned generally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByName(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *pchName);

// Returns a Power owned internally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByNamePersonal(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *pchName);

// Returns the Power* if there is one in the indexed earray with the matching ID, otherwise returns NULL
SA_RET_OP_VALID Power *powers_IndexedFindPowerByID(SA_PARAM_NN_VALID Power *const *const *pppPowers, U32 uiID);

// Returns the Power* if the character owns a power with the matching ID, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByID(SA_PARAM_NN_VALID const Character *pchar, U32 uiID);

// Returns the Power* if the characters owns a power with the matching ID, otherwise returns NULL
// Searches more than just the owned powers list, but all the items and power trees for powers that may have not been
// added to that list
// COMPLETE VERSION MAY NOT BE DESIRED, THINK ABOUT WHY YOU ARE USING THIS FUNCTION
Power *character_FindPowerByIDComplete(const Character *pchar, U32 uiID);

// Returns a Power owned generally by the Character, otherwise returns NULL
SA_RET_OP_VALID Power *character_FindPowerByIDSubIdx(SA_PARAM_NN_VALID Character *pchar, U32 uiID, S32 iSubIdx);

// Returns the PowerDef* if the character owns a power with the matching ID, otherwise returns NULL
SA_RET_OP_VALID PowerDef *character_FindPowerDefByID(SA_PARAM_NN_VALID Character *pchar, U32 uiID);

// Returns the PowerDef* if the character owns a power with the matching ID and sub index, otherwise returns NULL
//  -1 is the proper sub index to pass if you're not looking for a child of a combo power
SA_RET_OP_VALID PowerDef *character_FindPowerDefByIDSubIdx(SA_PARAM_NN_VALID Character *pchar, U32 uiID, S32 iSubIdx);

// Returns the Power* if the character owns a power with the matching ID, otherwise returns NULL.  Optionally
//  also checks to make sure the name matches.
SA_RET_OP_VALID Power *character_FindPowerByIDAndName(SA_PARAM_NN_VALID Character *pchar, U32 uiID, SA_PARAM_OP_STR const char *pchNameOptional);

// Returns the first Power* found on the Character that has the given Category, otherwise returns NULL
// Excludes any Powers unavailable due to BecomeCritter
SA_RET_OP_VALID Power *character_FindPowerByCategory(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *cpchCategory);


// Checks whether the Limited Use entity bucket fields need to be sent
S32 character_LimitedUseCheckUpdate(SA_PARAM_NN_VALID Character *pchar);

// Updates the Limited Use data for diffing next frame
void character_LimitedUseUpdate(SA_PARAM_NN_VALID Character *pchar);

// Sends the Character's Limited Use data
void character_LimitedUseSend(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);

// Receives the Character's Limited Use data.  Safely consumes the data if there isn't a Character.
void character_LimitedUseReceive(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);


// Sends the Character's Charge Data
void character_ChargeDataSend(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);

// Receives the Character's Charge Data.  Safely consumes the data if there isn't a Character.
void character_ChargeDataReceive(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);


// Processes all the systems that can fiddle with combat level, and makes sure
//  the Character's iLevelCombat is current
void character_LevelCombatUpdate(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, bool bLevelControlRemoved, GameAccountDataExtract *pExtract);

// Clears the Character's controls on its iLevelCombat, if any
void character_LevelCombatNatural(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Sets the Character's forced iLevelCombat
void character_LevelCombatForce(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, S32 iLevel, GameAccountDataExtract *pExtract);

// Sets the Character's iLevelCombat to link to another entref
void character_LevelCombatLink(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, EntityRef erLink, U32 bRequiresTeam, F32 MaxRange, GameAccountDataExtract *pExtract);


// Causes the character to clear the existing powers array and refill it with the appropriate Powers
//  from various systems.  Called on the server after loading from the db, and called on the client
//  when needed.
void character_ResetPowersArray(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Determines what power would actually be activated if the character attempted to activate the
//  supplied power.  Takes replacement powers into account, if bUseReplacementPower is set.  Will 
//	return the supplied power or replacement if it's not a combo, or one of the combo's sub powers.  
//	Target may be NULL, but should be passed in if known.  Activation may also be NULL. In certain 
//	cases, this function may desire to switch targets, and if so, it will return the new target in 
//	the out target.
SA_RET_NN_VALID Power *character_PickActivatedPower(int iPartitionIdx,
												SA_PARAM_NN_VALID Character *pchar,
												SA_PARAM_NN_VALID Power *ppow,
												SA_PARAM_OP_VALID Entity *eTarget,
												SA_PARAM_OP_VALID Entity **ppentTargetOut,
												SA_PARAM_OP_VALID WorldInteractionNode **ppnodeTargetOut,
												SA_PARAM_OP_VALID bool *pbShouldSetHardTarget,
												SA_PARAM_OP_VALID PowerActivation *pact,
												S32 bQuiet,
												S32 bUseReplacementPower,
												ActivationFailureReason *peFailOut);

// Cancels and removes all mods on a character.  Optionally allowed mods marked to survive
//  death to linger.  Will also trigger expiration processing if the expiration reason is
//  something other than Unset.
void character_RemoveAllMods(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, S32 bAllowSurvival, S32 bUnownedModsOnly, ModExpirationReason eReason, GameAccountDataExtract *pExtract);

// Cancels all mods from the given power. erSource cannot be 0.  Optionally disables instead of cancels.
void character_CancelModsFromPowerID(Character *pchar,
									 U32 uiPowerID,
									 EntityRef erSource,
									 U32 uiActivationID,
									 S32 bDisable);

// Cancels all mods from the given def.  Optionally disables instead of cancels.
//  If erSource is non-zero, only cancels the mods from that source.
void character_CancelModsFromDef(SA_PARAM_NN_VALID Character *pchar,
								 SA_PARAM_NN_VALID PowerDef *pdef,
								 EntityRef erSource,
								 U32 uiActivationID,
								 S32 bDisable);

// An entity that this character created was destroyed, so handle cleanup of that
void character_CreatedEntityDestroyed(SA_PARAM_NN_VALID Character *pchar,
									  SA_PARAM_NN_VALID Entity *pentCreated);

// Returns a pointer to the AttribMod on the Character that created the Power, if one exists
SA_RET_OP_VALID AttribMod *character_GetPowerCreatorMod(SA_PARAM_NN_VALID Character *pchar,
													 SA_PARAM_NN_VALID Power *ppow);

// Returns whether or not the given character was created by another entity and if it expires
bool character_DoesCharacterExpire(SA_PARAM_NN_VALID Character *pchar);

// Called when characters are loaded to/from the database

// Cleans up transacted part of character after loading from the db
void character_LoadTransact(SA_PARAM_NN_VALID NOCONST(Entity) *e);

// Cleans up a character after loading from the db, this can only modify non-transacted data
//  The bOffline flag indicates if the Character is "offline" and just being processed to get
//  an idea of the state of the Character.
void character_LoadNonTransact(int iPartitionIdx, SA_PARAM_NN_VALID Entity *e, S32 bOffline);

// Fills in a CharacterPreSaveInfo struct with all the current information on the character
void character_FillInPreSaveInfo(Character *pChar, CharacterPreSaveInfo *pPreSaveInfo);

// Clears out any information alloced in the PreSaveInfo struct
void CharacterPreSaveInfo_Destroy(CharacterPreSaveInfo *pPreSaveInfo);

// Cleans up a character before saving to the db
void character_PreSave(SA_PARAM_NN_VALID Entity *e);

// Sends the data necessary to have the client properly initialize its Character when it loads
void character_SendClientInitData(SA_PARAM_NN_VALID Character *pchar);



// CombatTracker functions

// Mark the Character's CombatTrackerList, so that it is considered changed on the client
void character_CombatTrackerListTouch(SA_PARAM_NN_VALID Character *pchar);

// Adds a CombatTracker event to the Character
CombatTrackerNet* character_CombatTrackerAdd(	SA_PARAM_NN_VALID Character *pchar,
												SA_PARAM_NN_VALID PowerDef *pdef,
												EntityRef erOwner,
												EntityRef erSource,
												SA_PARAM_OP_VALID PowerDef *pSecondaryDef,
												AttribType eType,
												F32 fMagnitude,
												F32 fMagnitudeBase,
												CombatTrackerFlag eFlags,
												F32 fDelay,
												S32 bAINotifyMiss);

void character_CreateImmunityCombatTracker(	SA_PARAM_NN_VALID Character *pchar, 
											SA_PARAM_OP_VALID Entity *pSource,
											SA_PARAM_NN_VALID AttribMod *pMod, 
											SA_PARAM_NN_VALID AttribModDef *pModDef);

#ifdef GAMECLIENT

// Copies all CombatTrackers in the regular event list into the client buffer list
void character_CombatTrackerBuffer(SA_PARAM_NN_VALID Character *pchar);

// Forces dismounting of the character from the client, using some costume change prediction
// combat config needs to be setup to use this.
void gclCharacter_ForceDismount(Character* pChar);

// combatConfig, iMountPowerCategory must be turned on- returns true if the character has a mounted costume 
bool gclCharacter_HasMountedCostume(Character *pChar);


#endif

// Checks all the timers on the buffered event list and transfers them to the regular event list when the delay reaches 0
void character_CombatTrackerBufferTick(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fTick);


// Misc utility

// Fills in the PowerRechargeState for the Character
void character_RechargeStateBuild(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerRechargeState *pState);

// Applies the PowerRechargeState to the Character
void character_RechargeStateApply(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerRechargeState *pState);

// Returns the value in the Character's *current* Class's table at the Character's combat level, otherwise 0
F32 character_PowerTableLookupOffset(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_STR const char *pchTable, int iOffset);

// Returns the value in the Character's *current* Class's table at the Character's combat level, otherwise 0.
__forceinline static F32 character_PowerTableLookup(SA_PARAM_NN_VALID Character *pchar,  SA_PARAM_NN_STR const char *pchTable) { return character_PowerTableLookupOffset(pchar, pchTable, 0); }

// Returns the value in the Character's *true* Class's table at the specified index, otherwise 0.  
F32 entity_PowerTableLookupAtHelper(SA_PARAM_OP_VALID NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchTable, int idx);


// General Character access functions

// Returns the matching PTNode if the characters owns this PTNodeDef.  Optionally returns the PowerTree.
PTNode *character_FindPowerTreeNode(Character *p, PTNodeDef *pDef, PowerTree **ppOutTree);


// Returns true if the Character switch in or out of BattleForm (based on their current BattleForm state)
S32 character_CanToggleBattleForm(SA_PARAM_NN_VALID Character *pchar);

// Sets the Character in or out of BattleForm
void character_SetBattleForm(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, bool bEnable, bool bForce, bool bTimer, GameAccountDataExtract *pExtract);



bool character_CanBuyPowerTreeNodeNextRank( int iPartitionIdx, Character *pChar, PTGroupDef *pGroup, PTNodeDef *pNodeDef);

bool character_CanBuyPowerTree(int iPartitionIdx, Character *pChar, PowerTreeDef *pTree);

bool character_CanBuyPowerTreeNodeIgnorePointsRank( Character *pChar, PowerTree *pTree, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank);
bool character_CanBuyPowerTreeNode(int iPartitionIdx, Character *pChar, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank);
bool character_CanBuyPowerTreeGroup(int iPartitionIdx, Character *pChar, PTGroupDef *pGroup);

S32 entity_TreePointsToSpend(SA_PARAM_OP_VALID Entity *pEnt);
S32 character_GetNextRank(Character *pChar, PTNodeDef *pDef);

S32 character_FindTrainingLevel(SA_PARAM_NN_VALID Character *pChar);
S32 character_Find_TableTrainingLevel(SA_PARAM_NN_VALID Character *pChar, const char *pchTableName);

void character_CacheAttribMods(Character *pchar, AttribMod ***pppOldCostumeChanges, AttribMod ***pppOldCostumeModifies, AttribMod ***pppOldRewardModifies);
void character_RegenCostumeIfChanged(Character *pchar, AttribMod ***pppOldCostumeChanges, AttribMod ***pppOldCostumeModifies);
void character_RegenRewardsIfChanged(Character *pchar, AttribMod ***pppRewardModifies);

bool character_IsLegalTarget(Character *pchar, EntityRef ref);
F32 character_GetRelativeDangerValue(Character *pchar, EntityRef ref);

const char *character_GetDefaultPlayingStyle(Character *pChar);

void character_trh_GetPowerTreeVersion(ATH_ARG NOCONST(Character)* pChar, U32* puVersion, U32* puFullRespecVersion);
#define character_GetPowerTreeVersion(pChar, puVersion, puFullRespecVersion) character_trh_GetPowerTreeVersion(CONTAINER_NOCONST(Character, pChar), puVersion, puFullRespecVersion)

bool character_CanUseWarpPower(Character *pchar, PowerDef *ppowDef);

S32 character_IgnoresExternalAnimBits(SA_PARAM_NN_VALID Character *pchar, EntityRef erSource);
LATELINK;
void character_ProjSpecificDeathLogString(Entity *pEnt, bool beforePenalty, char **outString);
LATELINK;
void character_ProjSpecificCombatLevelChange(Entity* pEnt, bool bLevelControlRemoved, char** pestrMsg); 
#endif

void character_UpdateDeathCapsuleLinger(Entity * pEnt);

void character_updateTacticalRequirements(Character *pchar);

int character_CheckSourceActivateRules(SA_PARAM_OP_VALID Character *pChar, PowerActivateRules eActivateRules);
