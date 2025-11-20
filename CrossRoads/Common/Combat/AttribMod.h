/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef ATTRIBMOD_H_
#define ATTRIBMOD_H_
GCC_SYSTEM

#include "referencesystem.h"	// For REF_TO, etc
#include "structDefines.h"	// For StaticDefineInt
#include "Message.h" // For DisplayMessage

#include "CharacterAttribsMinimal.h" // For enums
#include "PowersEnums.h" // For enums


// Forward declarations
typedef struct	Character			Character;
typedef struct	CharacterAttribs	CharacterAttribs;
typedef struct	CharacterClass		CharacterClass;
typedef struct	CommandQueue		CommandQueue;
typedef struct	DefineContext		DefineContext;
typedef struct	Entity				Entity;
typedef struct	ExprContext			ExprContext;
typedef struct	Expression			Expression;
typedef struct	FragileScaleSet		FragileScaleSet;
typedef struct	ExprLocalData		ExprLocalData;
typedef struct  GameAccountDataExtract GameAccountDataExtract;
typedef struct	ModArray			ModArray;
typedef struct	ModDefFragility		ModDefFragility;
typedef struct	ModFragility		ModFragility;
typedef struct	PACritical			PACritical;
typedef struct	Power				Power;
typedef struct	PowerActivation		PowerActivation;
typedef struct	PowerApplication	PowerApplication;
typedef struct	PowerDef			PowerDef;
typedef struct	PowerFXParam		PowerFXParam;
typedef struct	PowerSubtargetChoice PowerSubtargetChoice;
typedef struct	SensitivityMod		SensitivityMod;
typedef struct	WorldInteractionNode	WorldInteractionNode;
extern StaticDefineInt AttribTypeEnum[];
extern StaticDefineInt AttribAspectEnum[];
extern StaticDefineInt CombatEventEnum[];
extern StaticDefineInt SensitivityTypeEnum[];
extern StaticDefineInt PowerErrorEnum[];

// Redefined from elsewhere
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

// Utility macros
#define SIZE_OF_NORMAL_ATTRIB 4
#define SIZE_OF_ALL_NORMAL_ATTRIBS (sizeof(CharacterAttribs)-sizeof(DirtyBit))
#define NUM_NORMAL_ATTRIBS (SIZE_OF_ALL_NORMAL_ATTRIBS/SIZE_OF_NORMAL_ATTRIB)
#define IS_NORMAL_ATTRIB(x) ((x)<sizeof(CharacterAttribs))
#define IS_SPECIAL_ATTRIB(x) ((x)>=sizeof(CharacterAttribs))
#define IS_SET_ATTRIB(x) ((x)>kAttribType_LAST)
#define ATTRIB_INDEX(x) (IS_NORMAL_ATTRIB(x) ? (x)/SIZE_OF_NORMAL_ATTRIB : ((x)-sizeof(CharacterAttribs))+NUM_NORMAL_ATTRIBS)
#define F32PTR_OF_ATTRIB(ptrOfCharacterAttribs, offAttrib) ((F32*)((char*)ptrOfCharacterAttribs + offAttrib))
#define MODTIME(pmod) pmTimestampFrom((pmod)->uiTimestamp,(pmod)->fAnimFXDelay - (pmod)->fTimer)

#define mod_Expire(pmod) ((pmod)->fDuration = kModExpirationReason_Unset)
#define mod_ExpireIsValid(pmod) ((pmod)->fDuration == floorf((pmod)->fDuration))

// Parse tables configured for use in expressions
extern ParseTable parse_AttribModDef_ForExpr[];
#define TYPE_parse_AttribModDef_ForExpr AttribModDef_ForExpr
extern ParseTable parse_AttribMod_ForExpr[];
#define TYPE_parse_AttribMod_ForExpr AttribMod_ForExpr

extern DictionaryHandle g_hFragileScaleSetDict;


/***** ENUMS *****/

// ModTarget defines which entity the AttribMod should effect
AUTO_ENUM;
typedef enum ModTarget
{
	kModTarget_Self		= 1 << 0,
		// The AttribMod affects yourself, for every target
	
	kModTarget_Target	= 1 << 1,
		// The AttribMod affects your target

	kModTarget_SelfOnce	= 1 << 2,
		// The AttribMod affects yourself, independent of the number of targets hit

	kModTarget_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} ModTarget;

#define ModTarget_NUMBITS 8
STATIC_ASSERT(kModTarget_MAX <= (1 << (ModTarget_NUMBITS - 2)) + 1);

// ModType defines which of the attribmod's expressions are variable (with respect to strength and resistances)
// You must only use values up to 1 << 15
AUTO_ENUM;
typedef enum ModType
{
	kModType_None = 0,
		// None of the expressions are scaled or resisted

	kModType_Magnitude = 1 << 0,
		// The magnitude expression is scaled and resisted

	kModType_Duration = 1 << 1,
		// The duration expression is scaled and resisted

	kModType_Both = kModType_Duration | kModType_Magnitude,
		// Both duration and magnitude expressions are scaled and resisted

} ModType;

#define ModType_NUMBITS 8

// StackEntity defines how AttribMods can be considered copies based on the creating Entity
AUTO_ENUM;
typedef enum StackEntity
{
	kStackEntity_Source,
		// Stacking is based on the AttribMod's source

	kStackEntity_Owner,
		// Stacking is based on the AttribMod's owner

	kStackEntity_None,
		// Stacking does not consider the AttribMod's source or owner

} StackEntity;

#define StackEntity_NUMBITS 3 // Extra bit for the sign bit, silly enums

// Defines the behavior between AttribMods on the same Entity that are considered copies
AUTO_ENUM;
typedef enum StackType
{
	kStackType_Stack,
		// Allow multiple copies

	kStackType_Extend,
		// Update the parameters and extend the existing copy (additive)

	kStackType_Replace,
		// Replace the existing copy

	kStackType_Discard,
		// New copies are discarded

	kStackType_KeepBest,
		// Replace the existing copy if the new copy has a greater magnitude, otherwise discard the new copy

	kStackType_Count, EIGNORE

	kStackType_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} StackType;

#define StackType_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kStackType_MAX <= (1 << (StackType_NUMBITS - 1)));

// ModHitTest defines when the AttribMod is applied based on hitting or missing the target
AUTO_ENUM;
typedef enum ModHitTest
{
	kModHitTest_Hit = 0,
		// Mod is applied when the target is Hit, default

	kModHitTest_Miss,
		// Mod is applied when the target is Missed

	kModHitTest_HitOrMiss,
		// Mod is applied regardless of the target being Hit or Missed
} ModHitTest;

#define ModHitTest_NUMBITS 3 // Extra bit for the sign bit, silly enums

// CombatEventResponse defines how the AttribMod should respond to the CombatEvents it watches
AUTO_ENUM;
typedef enum CombatEventResponse
{
	kCombatEventResponse_None = 0,
		// Default behavior, AttribMod has no response

	kCombatEventResponse_CancelIfNew,
		// AttribMod is canceled, if it is a new AttribMod

	kCombatEventResponse_IgnoreIfNew,
		// AttribMod is ignored until the event is gone, if it is a new AttribMod

	kCombatEventResponse_CheckExisting,	EIGNORE
		// Utility enum, all values above this must be checked on existing AttribMods

	kCombatEventResponse_Cancel,
		// AttribMod is canceled

	kCombatEventResponse_Ignore,
		// AttribMod is ignored until the event is gone

	kCombatEventResponse_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} CombatEventResponse;

#define CombatEventResponse_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kCombatEventResponse_MAX <= (1 << (CombatEventResponse_NUMBITS - 1)));

// ModExpirationReason defines how an AttribMod expired.  It's set on an AttribMod by setting the duration
//  of the AttribMod to one of these negative values.
AUTO_ENUM;
typedef enum ModExpirationReason
{
	kModExpirationReason_Unset = -1,
		// Expired for no particular reason

	kModExpirationReason_AttribModExpire = -2,
		// An AttribModExpire AttribMod told it to

	kModExpirationReason_Chance = -3,
		// Failed a chance check and cancels as a result

	kModExpirationReason_CombatEventCancel = -4,
		// A CombatEvent happened to which its response is to cancel

	kModExpirationReason_Duration = -5,
		// Ran out of duration (typical)

	kModExpirationReason_FragileDeath = -6,
		// It was fragile and ran out of health

	kModExpirationReason_Immunity = -7,
		// Target was immune to it

	kModExpirationReason_CharacterDeath = -8,
		// Target died

	kModExpirationReason_Charges = -9,
		// Limited number of charges and it ran out

	kModExpirationReason_AttribLinkExpire = -10,
		// An AttribModExpire AttribMod told it to

} ModExpirationReason;

// ModExpirationEntity defines the target of the event that occurs when an AttribMod expires
AUTO_ENUM;
typedef enum ModExpirationEntity
{
	kModExpirationEntity_Unset = 0,					EIGNORE
		// Unset, generates an error during validation

	kModExpirationEntity_ModOwner,					ENAMES(ModOwner)
		// The ultimate owner of the expiring AttribMod

	kModExpirationEntity_ModSource,					ENAMES(ModSource)
		// The source of the expiring AttribMod

	kModExpirationEntity_ModSourceTargetDual,		ENAMES(ModSourceTargetDual)
		// The dual target of the source of the expiring AttribMod, or the source

	kModExpirationEntity_ModTarget,					ENAMES(ModTarget)
		// The target of the expiring AttribMod

	kModExpirationEntity_RandomNotSource,			ENAMES(RandomNotSource)
		// A random valid target other than the source of the expiring AttribMod

} ModExpirationEntity;

/***** END ENUMS *****/

// Definition of a named set of attribs, which is unrolled at load time
AUTO_STRUCT;
typedef struct AttribSet
{
	char *pchName;					AST(STRUCTPARAM)
		// Name of the set

	AttribType *poffAttribs;		AST(INT, NAME(Attribs), SUBTABLE(AttribTypeEnum))
		// Earray of attribs that this set unrolls to
} AttribSet;

// Structure used to load attrib sets, add them to a define table and look them up
AUTO_STRUCT;
typedef struct AttribSets
{
	AttribSet **ppSets;						AST(NAME(Set))
		// Earray of all sets

	DefineContext *pDefineAttribSets;		NO_AST
		// Context for AttribTypeEnum to include these

	int iSetOffsetStart;					NO_AST
		// Offset used during a reverse lookup (offset -> set)

	int iSetOffsetEnd;						NO_AST
		// Offset used during a reverse lookup (offset -> set)
} AttribSets;

// Globally available group set
extern AttribSets g_AttribSets;


AUTO_STRUCT;
typedef struct PowerTagsStruct
{
	S32 *piTags;	AST(STRUCTPARAM, SUBTABLE(PowerTagsEnum))
		// The EArray of Tags

} PowerTagsStruct;

// ModExpiration defines the response when an AttribMod expires
AUTO_STRUCT;
typedef struct ModExpiration
{
	REF_TO(PowerDef) hDef;				AST(NAME(Def))
		// The PowerDef applied when the AttribMod expires

	ModExpirationEntity eTarget;
		// The target entity of the response PowerDef

	Expression *pExprRequiresExpire;	AST(NAME(ExprBlockRequiresExpire), LATEBIND)
		// Expression that must evaluate to true for the Expiration to be applied.
		//  Currently evaluated under the Affects context.

	U8 bPeriodic : 1;
		// Sleazy re-use feature: When this is set on a periodic AttribMod,
		//  the expiration is run each period, rather than on expiration.
} ModExpiration;



// Parent definition of AttribModDef parameters
AUTO_STRUCT;
typedef struct AttribModDefParams
{
	AttribType eType;				AST(SUBTABLE(AttribTypeEnum), POLYPARENTTYPE)
		// The type of the attrib, which maps directly to the param structure used.
		//  The child structures are defined by the attribs, so they are found in
		//  CharacterAttribs.c/h
} AttribModDefParams;

AUTO_STRUCT;
typedef struct AttribModAnimFX
{
	// Animation and FX fields

	const char **ppchContinuingBits;		AST(NAME(ContinuingBits), POOL_STRING, SERVER_ONLY)
		// Bits while the AttribMod is active

	const char **ppchContinuingFX;			AST(NAME(ContinuingFX), POOL_STRING, SERVER_ONLY)
		// FX while the AttribMod is active

	const char **ppchStanceWord_unused;				AST(NAME(StanceWord), POOL_STRING, SERVER_ONLY)
		// New anim system stance during attrib mod.

	const char *pchAnimKeyword_unsued;				AST(NAME(AnimKeyword), POOL_STRING, SERVER_ONLY)
		// New anim system anim keyword started when attrib mod starts

	PowerFXParam **ppContinuingFXParams;	AST(NAME(ContinuingFXParamBlock), REDUNDANT_STRUCT(ContinuingFxParam, parse_PowerFXParam_StructParam), SERVER_ONLY)
		// Params for ContinuingFX

	const char **ppchConditionalBits;		AST(NAME(ConditionalBits), POOL_STRING, SERVER_ONLY)
		// Bits while the AttribMod is active, if the target's current value for the attrib is > 0

	const char **ppchConditionalFX;			AST(NAME(ConditionalFX), POOL_STRING, SERVER_ONLY)
		// FX while the AttribMod is active, if the target's current value for the attrib is > 0

	PowerFXParam **ppConditionalFXParams;	AST(NAME(ConditionalFXParamBlock), REDUNDANT_STRUCT(ConditionalFxParam, parse_PowerFXParam_StructParam), SERVER_ONLY)
		// Params for ConditionalFX

} AttribModAnimFX;

// Basic definition of an AttribMod 
AUTO_STRUCT AST_IGNORE(Resist) AST_IGNORE(Strength) AST_IGNORE_STRUCTPARAM(Name);
typedef struct AttribModDef
{
	AttribType offAttrib;			AST(NAME(Attrib), SUBTABLE(AttribTypeEnum), DEFAULT(-1))
		// Offset to the attribute in CharacterAttribs, typed to AttribType so it shows in debugger
		//  Because 0 is valid and typically defined by data, the default is -1 to force writing 0
		//  as full text

	AttribAspect offAspect;			AST(NAME(Aspect), SUBTABLE(AttribAspectEnum), DEFAULT(-1))
		// Offset to the CharacterAttribs structure in Character
		//  Because 0 is valid but defined by the game, the default is -1 to force writing 0
		//  as full text

	AttribModDefParams *pParams;
		// Special-case parameters for the def.  This is a polymorphic type based on the attrib.

	PowerTagsStruct tags;			AST(NAME(Tags))
		// The tags the AttribMod has

	PowerDef *pPowerDef;			NO_AST
		// Pointer to the PowerDef this AttribModDef belongs to.  Always valid
		//  since AttribModDefs can't exist without a parent PowerDef

	U32 uiDefIdx;					AST(NAME(""))
		// Index in the power's AttribModDef array.  Maybe we could make due with a U8 or U16.
		//  Copied into AttribMods when they are created, but needs to be binned, so it's name
		//  is empty

	F32 fApplyPriority;				AST(DEFAULT(0))
		// Controls the order in which AttribMods are applied.  Lower numbers are applied
		//  first.  Must be >0.  If it's default (0), it's assigned an priority based on the
		//  preexisting order.

	int iKey;						AST(KEY, SERVER_ONLY)
		// Internal key of the AttribMod.  Created by the power that owns the original
		//  version of this AttribMod.  0 is not valid.

	Expression *pExprDuration;		AST(NAME(ExprBlockDuration,ExprDurationBlock), REDUNDANT_STRUCT(ExprDuration, parse_Expression_StructParam), LATEBIND)
		// Expression that defines the duration.  If NULL it's assumed to be 0.

	Expression *pExprMagnitude;		AST(NAME(ExprBlockMagnitude,ExprMagnitudeBlock), REDUNDANT_STRUCT(ExprMagnitude, parse_Expression_StructParam), LATEBIND)
		// Expression that defines the magnitude

	const char *pchTableDefault;	AST(POOL_STRING)
		// If not null, this provides a default table lookup that is multiplied as a final step
		//  for calculating duration or magnitude

	F32 fVariance;
		// Defines how the AttribMod's value randomly varies.  Valid values are [0..1].  0 means no
		//  variance at all.  1 means the value can vary +/- 100%.



	ModDefFragility *pFragility;	AST(NAME(Fragility), SERVER_ONLY)
		// Fragility structure for tracking all fragility-related data, not currently sent to client


	// Sensitivity

	S32 *piSensitivities;					AST(NAME(Sensitivity), SUBTABLE(SensitivityModsEnum) SERVER_ONLY)
		// The sensitivity mods for the AttribMod.  Not sent to the client as it's generally not relevant.

	F32 fSensitivityStrength;				AST(NO_TEXT_SAVE DEFAULT(1.0))
		// A copy of the sensitivity the attrib has to strength.  Defaulted to 1.0 to save on bandwidth.

	F32 fSensitivityResistance;				AST(NO_TEXT_SAVE DEFAULT(1.0))
		// A copy of the sensitivity the attrib has to resistance.  Defaulted to 1.0 to save on bandwidth.

	F32 fSensitivityImmune;					AST(NO_TEXT_SAVE DEFAULT(1.0))
		// A copy of the sensitivity the attrib has to immunities.  Defaulted to 1.0 to save on bandwidth.

	// Timing fields

	F32 fDelay;
		// Seconds.  How long to wait before applying the AttribMod for the first time

	F32 fPeriod;
		// Seconds.  How long is the period of application of the AttribMod


	// Application limits fields
	
	Expression *pExprRequires;		AST(NAME(ExprBlockRequires,ExprRequiresBlock), REDUNDANT_STRUCT(ExprRequires, parse_Expression_StructParam), LATEBIND)
		// Expression that must evaluate to true when the power is activated for this AttribMod to
		//  be applied to the target

	Expression *pExprAffects;		AST(NAME(ExprBlockAffects,ExprAffectsBlock), REDUNDANT_STRUCT(ExprAffects, parse_Expression_StructParam), LATEBIND)
		// Expression that limits what this AttribMod can affect while it's active.  Can be checked
		//  against both a target AttribModDef and/or a target PowerDef.

	F32 fArcAffects;
		// Degrees.  If this AttribMod affects incoming AttribMods, a non-0 value in this field indicates that
		//  it only affects AttribMods from a direction that falls within the arc.

	F32 fYaw;
		// Degrees.  Specifies the direction this AttribMod points.  Generally only useful in conjunction with
		//  fArcAffects or other directional-sensitive systems.

	Expression *pExprChance;		AST(NAME(ExprChanceBlock), REDUNDANT_STRUCT(Chance, parse_Expression_StructParam), LATEBIND)
		// The chance that this AttribMod is applied to the target. 1 means always.
	
	// Event fields
	
	int *piCombatEvents;					AST(SUBTABLE(CombatEventEnum))
		// List of CombatEvents to listen for

	F32 fCombatEventTime;
		// Seconds.  How far back in time to look for a CombatEvent.  If 0, will
		//  only look for events within the last combat tick.

	ModExpiration *pExpiration;
		// The response when the AttribMod expires

	U32 uiStackLimit;						AST(NAME(StackLimit))
		// Specifies that a specific number of copies are allowed.  Only valid with Discard and Replace
		//  stack types, in which case the value must be either 0, or >= 2, since 1 is redundant.

	AttribModAnimFX *pAnimFX;			AST(SERVER_ONLY)
		// Animation and FX definition

	// Animation and FX fields (6 fields related to animation and FX will be moved into the pAnimFX after the data migration)

	const char **ppchContinuingBits;		AST(NAME(ContinuingBits), POOL_STRING, SERVER_ONLY)
		// Bits while the AttribMod is active

	const char **ppchAttribModDefStanceWordText;AST(NAME(StanceWord), POOL_STRING, SERVER_ONLY)
		// New anim system stance during attrib mod.

	const char *pchAttribModDefAnimKeywordText;	AST(NAME(AnimKeyword), POOL_STRING, SERVER_ONLY)
		// New anim system anim keyword started when attrib mod starts

	const char **ppchContinuingFX;			AST(NAME(ContinuingFX), POOL_STRING, SERVER_ONLY)
		// FX while the AttribMod is active

	PowerFXParam **ppContinuingFXParams;	AST(NAME(ContinuingFXParamBlock), REDUNDANT_STRUCT(ContinuingFxParam, parse_PowerFXParam_StructParam), SERVER_ONLY)
		// Params for ContinuingFX


	const char **ppchConditionalBits;		AST(NAME(ConditionalBits), POOL_STRING, SERVER_ONLY)
		// Bits while the AttribMod is active, if the target's current value for the attrib is > 0

	const char **ppchConditionalFX;			AST(NAME(ConditionalFX), POOL_STRING, SERVER_ONLY)
		// FX while the AttribMod is active, if the target's current value for the attrib is > 0

	PowerFXParam **ppConditionalFXParams;	AST(NAME(ConditionalFXParamBlock), REDUNDANT_STRUCT(ConditionalFxParam, parse_PowerFXParam_StructParam), SERVER_ONLY)
		// Params for ConditionalFX

	DisplayMessage msgAutoDesc;				AST(STRUCT(parse_DisplayMessage))
		// Message to use for AutoDesc instead of the standard message

	U32 uiAutoDescKey : 6;					AST(NAME(AutoDescKey))
		// Key used by the Power's custom AutoDesc message.  If 0, the AttribMod is not accessible.

	ModTarget eTarget : ModTarget_NUMBITS;	AST(DEFAULT(2), SUBTABLE(ModTargetEnum))
		// Who is the target of this AttribMod

	ModType eType : ModType_NUMBITS;		AST(SUBTABLE(ModTypeEnum))
		// Defines how the attribute is scaled with respect to strength, resistance and variance

	ModHitTest eHitTest : ModHitTest_NUMBITS;	AST(SUBTABLE(ModHitTestEnum))
		// Defines when the AttribMod is applied based on hitting or missing the target

	CombatTrackerFlag eFlags : CombatTrackerFlag_NUMBITS;	AST(FLAGS, SUBTABLE(CombatTrackerFlagEnum))
		// Special case CombatTrackerFlags for this AttribModDef
		//  Currently only used to mark flags picked up during strength (crit, exploit) and resistance (block, dodge)

	PowerError eError : PowerError_NUMBITS;		AST(NAME(Error), SUBTABLE(PowerErrorEnum), NO_TEXT_SAVE)
		// Error state

	CombatEventResponse eCombatEventResponse : CombatEventResponse_NUMBITS;
		// Response to a detected CombatEvent

	StackEntity eStackEntity : StackEntity_NUMBITS;
		// Defines how AttribMods can be considered copies based on the creating Entity

	StackType eStack : StackType_NUMBITS;
		// Defines the behavior between AttribMods on the same Entity that are considered copies

	ModStackGroup eStackGroup : ModStackGroup_NUMBITS;
		// Determines if multiple AttribMods with the same Attribute and Aspect are considered a group
		//  for which only the single "best" AttribMod is allowed to be active.

	ModStackGroup eStackGroupPending : ModStackGroup_NUMBITS;
		// Determines if multiple AttribMods with the same Attribute and Aspect are considered the same
		//  during the actual stacking check where AttribMods move from pending to active.

	// Bitfield

	U32		bContinuingFXAsLocation : 1;	AST(NAME(ContinuingFXAsLocation))
		// 
		
	U32 bPersonal : 1;
		// True if this AttribMod only works when the two parties involved are the source and target of
		//  the Application that created it.

	U32 bEnhancementExtension : 1;			AST(ADDNAMES(EnhancementExtendDef))
		// If this Power is an Enhancement, this AttribMod is appended to the attached non-Enhancement Power
		// If this Power is not an Enhancement, this AttribMod acts as an Enhancement

	U32 bIgnoreFirstTick : 1;
		// If the AttribMod should not operate on its first tick.  Valid only on periodic AttribMods.

	U32 bReplaceKeepsTimer : 1;
		// If when Replacing,the AttribMod should keep the replaced AttribMod's fTimer.
	    //  Valid only on periodic AttribMods that Replace.

	U32 bChanceNormalized : 1;
		// True if the chance field should be normalized to a 1 second total activation time.  Total
		//  activation time is time charged + power activation time, or for periodic applications,
		//  the power's periodic activation time.  Non-activated applications have 0 total activation time.
		//  This field is not allowed to be true on periodic AttribMods.

	U32 bCancelOnChance : 1;
		// True if this AttribMod should be canceled when a chance check is failed

	U32 bSurviveTargetDeath : 1;
		// True if this AttribMod should not be canceled when the target dies

	U32 bKeepWhenImmune : 1;
		// True if this AttribMod should be kept even when the target has immunity

	U32 bIgnoreAttribModExpire : 1;
		// True if this AttribMod can not be affected by AttribModExpire.  Safety feature for things
		//  that absolutely must be kept around under all circumstances.

	U32 bAutoDescDisabled : 1;
		// True if this AttribMod should NOT be included in any normal-detail AutoDesc display

	U32 bDerivedInternally : 1;
		// Set to true if this AttribModDef was derived from the actual data during load.
		//  No AttribModDefs with this flag should be saved or shown in the editor.
	
	U32 bIgnoredDuringPVP : 1;
		// Set to true for this AttribModDef to be ignored during pvp matches

	U32 bPowerInstanceStacking : 1;
		// Apply stacking rules to each power instance

	U32 bProcessOfflineTimeOnLogin : 1;
		// If true, on login this mod's duration will be reduced by the time the player was logged out.

	U32 bUIShowSpecial : 1;					AST(NAME(UIShowSpecial))
		// flags the mod as kCombatTrackerFlag_ShowSpecial for when sent to the client via a CombatTrackerNet
		
	U32 bNotifyGameEventOnApplication : 1;	
		// if set, when this attrib is first applied to the target, it will notify the game event system 
	U32 bIncludeInEstimatedDamage : 1;		AST(DEFAULT(1))
		//Include this mod in the "estimated damage" calculations that NW uses.
	U32 bForever : 1;						AST(NO_TEXT_SAVE)
		// Derived.  Set to true if the AttribMod is intended to last forever.

	U32 bHasAnimFX : 1;						AST(NO_TEXT_SAVE, SERVER_ONLY)
		// Derived.  Set to true if the AttribMod has Continuing or Conditional Bits or FX

	U32 bSaveApplyStrengths : 1;			AST(NO_TEXT_SAVE, SERVER_ONLY)
		// Derived.  Set to true if the AttribMod can cause the application of additional Powers
		//  and we need to fix the strength of the AttribMods of those Powers ahead of time.

	U32 bSaveSourceDetails : 1;				AST(NO_TEXT_SAVE, SERVER_ONLY)
		// Derived.  Set to true if a large set of details about the source of the AttribMod
		//  should be saved at apply time.

	U32 bSaveHue : 1;						AST(NO_TEXT_SAVE, SERVER_ONLY)
		// Derived.  Set to true if the hue should be saved at apply time.

	U32 bPowerIconCFX : 1;					AST(NO_TEXT_SAVE, SERVER_ONLY)
		// Derived.  Set to true if any of the AttribMod's Continuing or Conditional FX are
		//  named foo_PowerIcon and the Power has an Icon.  Causes a "PowerIcon" FX Param to
		//  be generated automatically.

	U32 bAttribLinkToSource : 1;
		// valid only if the power has a AttribLink type attribMod. 
		// This signifies that this attrib to be kept track of on the power's AttribLink mod. 

	U32 bAffectsOnlyOnFirstModTick : 1;
		// used for resist, immune and ShieldParams mods. If set, it will only affect the mods
		// that haven't yet been applied. 
		
} AttribModDef;

// Optional system to track the of each AttribMod during an unowned Power application.  Saved
//  on AttribMods that need the data, like ApplyPower, DamageTrigger, etc.
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct PowerApplyStrength
{

	REF_TO(PowerDef) hdef;			AST(REFDICT(PowerDef), PERSIST, NO_TRANSACT)
		// Reference to the PowerDef this Strength data is for

	U32 uiVersion;					AST(PERSIST, NO_TRANSACT)
		// Version of the PowerDef

	F32 *pfModStrengths;			AST(PERSIST, NO_TRANSACT)
		// Strength of each mod, in order matching the ppOrderedMods array.
		//  Will not exist if all values are 1.

	F32 *pfModStrAdd;				AST(PERSIST, NO_TRANSACT)
		// StrAdd for each mod. Will not exist if all values are 0.

	S32 *piModFlags;				AST(PERSIST, NO_TRANSACT)
		// Additional flags for each mod. Will not exist if all values are 0.
		//  Should be CombatTrackerFlags, but we can't write arrays of those,
		//  so we'll just write the S32 representation.

} PowerApplyStrength;

// Element of the AttribModSourceDetails structure, used to remember what Powers that
//  were used when the AttribMod was created, if the AttribMod needs to use them for
//  some reason or recreate them elsewhere.
AUTO_STRUCT AST_CONTAINER;
typedef struct PowerClone
{

	REF_TO(PowerDef) hdef;			AST(PERSIST, NO_TRANSACT)
		// PowerDef of the Power that was cloned

	int iLevel;						AST(PERSIST, NO_TRANSACT)
		// Level of the clone for table lookups

	F32 fTableScale;				AST(PERSIST, NO_TRANSACT)
		// TableScale of the clone for table lookups

} PowerClone;

// Optional structure of detailed information about the source of an AttribMod,
//  used for complex special Attribs like ApplyPower, EntCreate, etc that can cause
//  new Powers to be applied (for which we need the additional details)
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct AttribModSourceDetails
{

	REF_TO(CharacterClass) hClass;			AST(PERSIST, NO_TRANSACT)
		// CharacterClass used to construct this AttribMod

	int iLevel;								AST(PERSIST, NO_TRANSACT)
		// Combat level used to construct this AttribMod

	int iIdxMulti;							AST(PERSIST, NO_TRANSACT)
		// MultiTable index used to construct this AttribMod

	F32 fTableScale;						AST(PERSIST, NO_TRANSACT)
		// Table scale used to construct this AttribMod

	PowerClone **ppEnhancements;			AST(PERSIST, NO_TRANSACT)
		// The enhancements available to the AttribMod

	PACritical *pCritical;					AST(PERSIST, NO_TRANSACT)
		// Data about the critical state of the Application of the AttribMod

	EntityRef erTargetApplication;			AST(PERSIST, NO_TRANSACT)
		// Effective target EntityRef of the Application of the AttribMod

	Vec3 vecTargetApplication;				AST(PERSIST, NO_TRANSACT)
		// Effective target Vec3 of the Application of the AttribMod

	S64 iItemID;							AST(PERSIST, NO_TRANSACT)
		// The source item for the power that created this Application

	U8 bLevelAdjusting : 1;					AST(PERSIST, NO_TRANSACT)
		// This AttribMod was created in a level-adjusting situation

} AttribModSourceDetails;

// Structure that tracks custom parameters for AttribMods.  Generic, as it is used by several attributes.
//  Not yet containerified
AUTO_STRUCT AST_CONTAINER;
typedef struct AttribModParams
{
	Vec3 vecParam;								AST(PERSIST NO_TRANSACT)
		// EntCreate: Feet.  Evaluated distance to the front, right and above of the target to create the critter.
		// Shield: [evaluated max damage absorbed per hit; the current percent ignored (which may be modified); number of hits taken]
		// Teleport: Target location
		// ProjectileCreate: center position of the projectile

	Vec3 vecTarget;
		// If this vector is set, use this as the target position instead of the entity position

	Vec3 vecOffset;								NO_AST
		// Teleport: the offset vector of the teleport

	EntityRef erParam;
		// EntCreate: The entity that was targeted when the power was applied

	S32 iParam;									AST(PERSIST NO_TRANSACT)
		// EntCreate: The costume index to pick for the Critter

	REF_TO(WorldInteractionNode) hNodeParam;
		// ApplyObjectDeath: Node key of the object being destroyed (might change this to the string)

	CommandQueue *pCommandQueue;				NO_AST
		// AICommand: Used to clean up side effects from expressions

	ExprLocalData **localData;					NO_AST
		// AICommand: Used to store side effects for cleanup

	AttribType eDynamicCachedType;
} AttribModParams;


// UI list struct
AUTO_STRUCT;
typedef struct AttribStat
{
	const char *pchKeyName;
		// Internal name of the attrib
	char *estrNameMessage;
		// Message name for this attrib's name
	char *estrDescMessage;
		// Message name for this attrib's description
	float fCurrent;
		// The current value of the attrib
	float fBase;
		// The base value of the attrib
} AttribStat;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct AttribModLink
{
	U32 uiLinkedApplyID;
	U32 uiLinkedActIDServer;
	U32 uiLinkedDefIdx;
	EntityRef erTarget;
} AttribModLink;


// An actual instance of an AttribMod, hangs on a character and eventually expires
//  NOTE: If you change the layout of this structure, make sure parse_AttribMod_ForExpr
//  is properly updated to match the new autogenerated parse_AttribMod (specifically the
//  bits).
// NOTE: You are NOT ALLOWED to AST(DEFAULT()) anything in this structure!
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL AST_IGNORE(eModPowerSource) AST_IGNORE(fCheckTimer);
typedef struct AttribMod
{
	EntityRef erOwner;							NO_AST
		// Owner of the AttribMod.  Generally the same as erSource, but in the case
		//  of AttribMods creating AttribMods, the owner is the ultimate source.
	
	EntityRef erSource;							NO_AST
		// Source of the AttribMod
		
	Vec3 vecSource;								NO_AST
		// Position of the source of the AttribMod


	AttribModDef *pDef;							NO_AST // I REALLY don't want a dictionary of these.
		// Pointer/Reference to the def, call mod_GetDef() to get this safely

	REF_TO(PowerDef) hPowerDef;					AST(PERSIST NO_TRANSACT SUBSCRIBE REFDICT(PowerDef))
		// Reference to the power this mod came from, version-sensitive

	U32 uiVersion;								AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Version of the PowerDef

	U32 uiDefIdx;								AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// Index in the power's AttribModDef array.  Maybe we could make due with a U8 or U16.


	U32 uiApplyID;								AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// Tracks the map-wide PowerApplication ID.  All AttribMods that share an ID came from the same
		//  PowerApplication.  Persisted values are regenerated into a new space after loading from
		//  the db.  0 is an invalid/unknown value.  Not sent to client.

	U32 uiActIDServer;							NO_AST
		// Tracks the map-wide server-side PowerActivation ID.  This is the server-side U32 ID, NOT
		//  the U8 client-owned ID - specifically to prevent cheating and false-positive matches. 0
		//  is an invalid/unknown value.  Not currently persisted, since it's not currently used for
		//  persistence-relevant features.

	U32 uiPowerID;								AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
	S32	iPowerIDSub;							AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
	S16 iPowerIDLinkedSub;						AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Tracks the ID of the power that created this attrib mod. Not sent to client
		
	EntityRef erPersonal;
		// Tracks the relevant Entity for an AttribMod that is Personal, which means this AttribMod
		//  only works when the other Entity involved is this Entity
		// Jered told me I could send this to the client -BZ

	U32 uiRandomSeedActivation;					NO_AST
		// Random seed from Activation/Application, for any per-Activation randomness

	
	AttribModParams *pParams;					AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// Special parameters for the mod.
		
	
	U32 uiAnimFXID;								NO_AST
		// Tracks the anim/fx id, if one is needed.

	F32 fHue;									AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// Tracks the hue to use if needed (left 0 if the def doesn't have any fx)

	U32 uiTimestamp;							NO_AST
		// Tracks the mm timestamp of the creation of this AttribMod.  Used for turning
		//  on and off the bits and fx, and whatever else.

	F32 fAnimFXDelay;							NO_AST
		// The delay relative to creation that this AttribMod experiences before its bits and fx should be turned on

	
	U32 bActive : 1;							AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Set to true when the AttribMod actually becomes fully active, applying itself to the character

	U32 bIgnored : 1;							NO_AST
		// Set to true if the AttribMod is being ignored due to CombatEvents or being fully disabled

	U32 bDisabled : 1;							AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// Set to true if the AttribMod ever becomes fully disabled.  This allows it to hang around
		//  for stacking purposes (primarily bReplaceKeepsHealth), but prevents it from operating
		//  (by forcing bIgnored true) or loading from the db.  This occurs when a Power with
		//  bDeactivationDisablesMods shuts off.

	U32 bContinuingAnimFXOn : 1;				NO_AST
		// True if the continuing bits and fx have been started

	U32 bConditionalAnimFXOn : 1;				NO_AST
		// True if the conditional bits and fx have been started

	U32 bNotifiedAI : 1;						NO_AST
		// Set to true when the AI has been notified about this mod

	U32 bNotifiedPVP : 1;						NO_AST
		// Set to true when PVP system has been notified about this mod (for PVP infection)

	U32 bNotifiedGameEvent : 1;					NO_AST
		// Set to true when the gameEvent system has been notified that this mod was first applied

	U32 bCheckSource : 1;						NO_AST
		// A flag to see if we need to check that source entity still exists and is alive during the
		//  the pending process

	U32 bCheckLunge : 1;						NO_AST
		// Check to see if the Lunge activation that started this was successful in reaching its target
	
	U32 bNew : 1;								NO_AST
		// The mod is new to the pending array and shouldn't be ticked

	U32 bResistPositive : 1;					NO_AST
	U32 bResistNegative : 1;					NO_AST
		// Whether or not the attrib mod is resisted above or below the default value of 1.0
		
	U32 bProcessOfflineTimeOnLogin : 1;			AST(PERSIST NO_TRANSACT SERVER_ONLY)
		// If true, this attribmod will be processed on login for the full duration that the
		// character was offline.

	U32 bDeathPeekCompleted : 1;
		// set when the CombatDeathPrediction is enabled and this attrib has been checked for death prediction qualifications

	U32 bPostFirstTickApply : 1;
		// set to true after the first time this attrib has been applied, used for resist/immune/shield attribs that only apply to mods that haven't applied yet
		// Note: that this won't always mean that it really has been applied, 
		//		Through unowned applyPowers this could be set on application since it can be considered as applied as part of a chain

	U32 bProcessedDisplayNameTraker : 1;
		// set if the modDef had the combatTrackerFlag ShowPowerDisplayName and it has already been processed so we don't send
		// multiple per attrib

	F32 fDuration;								AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Seconds.  Remaining duration of the AttribMod

	F32 fDurationOriginal;						AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Seconds.  Original duration of the AttribMod

	F32 fMagnitude;								AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// Actual magnitude of the AttribMod

	F32 fMagnitudeOriginal;						AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// Original magnitude of the AttribMod

	ModFragility *pFragility;					AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Fragility data of the AttribMod, NULL if the mod is not fragile


	F32 fAvoidance;								AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Amount avoided by a dodge


	F32 fTimer;									AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Timer used for various purposes during AttribMod processing
		//  bActive false: The timer is used to count down until the AttribMod becomes active
		//  bActive true: The timer is used to count between periods (if there is no period fixed at 0)
		// Changes always, not sent to clients

	U32 uiPeriod;								NO_AST
		// The current period of the attrib mod. 1-based.
		// This is currently not persisted, as the period can be closely approximated on load.

	F32 fCheckTimer;							NO_AST
		// The check timer while pending. AttribMods are created ahead of time, and this timer is
		//  what tracks the timer until the source actually strikes the target.

	F32 fPredictionOffset;						NO_AST
		// The time in advance that this AttribMod needs to be handled so that information about it
		//  can be communicated to the source player


	EntityRef erCreated;
		// Tracks the entity created by this AttribMod, if any

	Power **ppPowersCreated;					AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY NO_INDEX)
		// Powers created by this AttribMod, if any


	PowerSubtargetChoice *pSubtarget;			AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY, FORCE_CONTAINER)
		// Optional subtarget data

	PowerApplyStrength **ppApplyStrengths;		AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Strengths to use for unowned Power applications caused by this AttribMod

	AttribModSourceDetails *pSourceDetails;		AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Optional source details

	union {
		AttribModLink **eaAttribModLinks;		NO_AST
		AttribModLink *pAttribModLink;			NO_AST
	};

	CombatTrackerFlag eFlags;					AST(PERSIST NO_TRANSACT SUBSCRIBE FLAGS)
		// Flags used to denote certain help describe what is happening to the client

} AttribMod;


// Custom structure for communicating AttribMod data to clients.  It is heavily managed in order to
//  minimize networking costs.  Please do not add anything without discussing with Jered.
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct AttribModNet
{
	DirtyBit dirtyBit;		AST(SERVER_ONLY)
		// Dirty bit, set whenever anything other than uiDuration changes

	REF_TO(PowerDef) hPowerDef;
		// Reference to the PowerDef this AttribMod came from

	U32 uiDefIdx;
		// Index into PowerDef's AttribModDef array
		//  This can probably be packed into 8 bits, but it's closer to 2

	U32 uiDuration;			NO_AST
		// Duration remaining, after (U32)(ceil(x))
		// NOTE: If the AttribMod being represented is bForever, this is just 1
		//  This can probably be packed into 16 bits, but it's closer to 4
		//  Changes to this field do not dirty the entire AttribModNet.  Instead
		//   the uiDuration values are sent in the entity bucket periodically.

	U32 uiDurationOriginal;
		// Original duration, after (U32)(ceil(x)).
		//  This can probably be packed into 16 bits, but it's closer to 4
		//  NOTE: If the AttribMod being represented is bForever, this is just 1
		//  NOTE: If this is 0, this is not a valid instance (see ATTRIBMODNET_VALID())

	S32 iMagnitude;
		// Magnitude after (S32)(100.f*x)  (see ATTRIBMODNET_MAGSCALE)

	S32 iMagnitudeOriginal;
		// Original magnitude after (S32)(100.f*x)  (see ATTRIBMODNET_MAGSCALE)
		//  If this is 0, it's the same as Magnitude

	S32 iHealth;
		// Health after (S32)(x)

	S32 iHealthMax;
		// Max health after (S32)(x)
		//  If this is 0, it's the same as Health

	void *pvAttribMod;		NO_AST
		// Pointer to the actual AttribMod.  Never deference, just use for pointer compares.

	U32 bResistPositive : 1;
		// Character's resistance is above 1.0

	U32 bResistNegative : 1;
		// Character's resistance is below 1.0

} AttribModNet;

#define ATTRIBMODNET_MAGSCALE 100.f

#define ATTRIBMODNET_VALID(pNet) (pNet && pNet->uiDurationOriginal)

// Attempts to return the AttribModDef of the AttribMod.  May return NULL.
SA_RET_OP_VALID AttribModDef *mod_GetDef(SA_PARAM_OP_VALID AttribMod *pmod);

// Attempts to return the AttribModDef of the AttribModNet.  May return NULL.
SA_RET_OP_VALID AttribModDef *modnet_GetDef(SA_PARAM_OP_VALID AttribModNet *pmodnet);


// Returns the angle to the source of an AttribMod, give the current position and orientation of
//  the Entity.  Return value is in radians.
F32 mod_AngleToSource(SA_PARAM_NN_VALID AttribMod *pmod,
					  SA_PARAM_NN_VALID Entity *pent);
// the method the mod_AngleToSource uses to get th angle to the source
F32 mod_AngleToSourcePosUtil(SA_PARAM_NN_VALID Entity *pent, const Vec3 vSourcePos);

S32 moddef_AffectsModFromAngleToSource(SA_PARAM_NN_VALID AttribModDef *pmoddefAffects,
										SA_PARAM_NN_VALID Character *pchar,
										F32 fAngleToSource);

// Returns true if the AttribModDef is allowed to affect the target AttribMod, based on the yaw/arc of the
//  main AttribModDef and the incoming direction of the target AttribMod.  This does not also perform the
//  more general moddef_AffectsModOrPower() check.
S32 moddef_AffectsModFromDirection(SA_PARAM_NN_VALID AttribModDef *pmoddefAffects,
								    SA_PARAM_NN_VALID Character *pchar,
								    SA_PARAM_OP_VALID AttribMod *pmodTarget);

// a macro to avoid calling the above function if not necessary
#define moddef_AffectsModFromDirectionChk(pmoddefAffects,pchar, pmodTarget)	((pmoddefAffects->fArcAffects < 0)||moddef_AffectsModFromDirection(pmoddefAffects,pchar, pmodTarget))

// Returns true if the AttribModDef in question is allowed to affect the target mod and/or power.  Because of
//  the different situations in which this may be called, all parameters except the primary are optional.
int moddef_AffectsModOrPower(int iPartitionIdx,
							 SA_PARAM_NN_VALID AttribModDef *pmoddefAffects,
							 SA_PARAM_OP_VALID Character *pchar,
							 SA_PARAM_OP_VALID AttribMod *pmodAffects,
							 SA_PARAM_OP_VALID AttribModDef *pmoddefTarget,
							 SA_PARAM_OP_VALID AttribMod *pmodTarget,
							 SA_PARAM_OP_VALID PowerDef *ppowdefTarget);

// a macro to avoid calling the above function if not necessary
#define moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefAffects,pchar,pmodAffects,pmoddefTarget,pmodTarget,ppowdefTarget)	\
	(!pmoddefAffects->pExprAffects || moddef_AffectsModOrPower(iPartitionIdx,pmoddefAffects,pchar,pmodAffects,pmoddefTarget,pmodTarget,ppowdefTarget))

// Returns true if the given pExprAffects returns true that it affects the target mod and/or power.  Because of
//  the different situations in which this may be called, all parameters except the primary are optional.
int mod_AffectsModOrPower(	int iPartitionIdx,
							SA_PARAM_NN_VALID Expression *pExprAffects,
							SA_PARAM_OP_VALID Character *pchar,
							SA_PARAM_OP_VALID AttribModDef *pmoddefTarget,
							SA_PARAM_OP_VALID AttribMod *pmodTarget,
							SA_PARAM_OP_VALID PowerDef *ppowdefTarget);

// Gets the actual effective magnitude of an AttribModDef, based
//  on a specific class and level
F32 moddef_GetMagnitude(int iPartitionIdx,
								   SA_PARAM_NN_VALID AttribModDef *pdef,
								   SA_PARAM_OP_VALID CharacterClass *pClass,
								   int iLevel,
								   F32 fTableScale,
								   int bApply);

// Returns true if the PowerTagsStruct includes the specified tag
S32 powertags_Check(SA_PARAM_NN_VALID PowerTagsStruct *pTags, S32 iTag);




// Functions to operate on ModArrays

// Adds an AttribMod to the array
void modarray_Add(ModArray *plist, AttribMod *pmod);

// Removes the AttribMod at the given index, destroys it
void modarray_Remove(ModArray *plist, int idx);

// Removes the given AttribMod, destroys it
void modarray_RemoveMod(ModArray *plist, AttribMod *pmod);

// Removes and frees all AttribMods in the array, and destroys the array
void modarray_RemoveAll(Character *pchar, ModArray *plist, S32 bAllowSurvival, S32 bUnownedOnly);

// Searches a mod array to find a mod that matches the given def from the same source
AttribMod *modarray_Find(ModArray *plist, AttribModDef *pdef, EntityRef erOwner, EntityRef erSource);

// Pushes the AttribMods that needs to be saved into the ppModsSaved list
void character_SaveAttribMods(SA_PARAM_NN_VALID Character *pchar);

// Cleans up the character's array of AttribMods after a load from db
void character_LoadAttribMods(SA_PARAM_NN_VALID Character *pchar, S32 bOffline, S32 bLevelDecreased);

// Walks the Character's AttribMod Powers and adds them to the general Powers list
void character_AddPowersFromAttribMods(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Utility compare functions
int mod_CmpDuration(const void *a, const void *b);
int mod_CmpDurationDefIdx(const void *a, const void *b);
int modnet_CmpDurationDefIdx(const void *a, const void *b);

//Helper function to replace one mod with another.
// Also used when an extend or discard mod arrives in the same combat tick as the previous mod was expiring.
void mod_Replace(int iPartitionIdx, Character *pchar, AttribMod *pmod, AttribMod *pmodPrior, int imodIdx, GameAccountDataExtract *pExtract);

// Destroys and frees the memory of an AttribMod.  If outside the mod processing loop, you
//  should be calling one of the modarray_Remove functions.
void mod_Destroy(SA_PARAM_NN_VALID AttribMod *pmod);

// The proper way to expire an attribMod. If the mod has an expiration def then it will make sure the character will be awake to process it
void character_ModExpireReason(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID AttribMod *pmod, ModExpirationReason eReason);

// Applies the appropriate AttribMods from a PowerDef to a target character.
void character_ApplyModsFromPowerDef(int iPartitionIdx,
									 SA_PARAM_NN_VALID Character *pcharTarget,
									 SA_PARAM_NN_VALID PowerApplication *papp,
									 F32 fHitDelay,
									 F32 fCheckDelay,
									 ModTarget eTargetsAllowed,
									 S32 bMiss,
									 SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsAppliedOut,
									 SA_PARAM_OP_VALID ExprContext **ppOutContext,
									 SA_PARAM_OP_VALID S64 *plTimerExcludeAccum);

// Fetches an AttribModDef's sensitivity to a specific type
F32 moddef_GetSensitivity(SA_PARAM_NN_VALID AttribModDef *pmoddef, SensitivityType eType);

// Determines the effectiveness of a modifier on an attrib, based on the attrib's sensitivity
//  Used for both strength and resistance modifications
//  Assumes fSensitivity > 0.0f, fMag > 0.0f, and fMag < 1 is a penalty
F32 mod_SensitivityAdjustment(F32 fMag, F32 fSensitivity);

// Determines how much to scale an attrib, based on the attrib's variance.
//  Used for both strength and resistance modifications if the mod is of
//  that type.  Assumes fVariance is (0.0f..1.0f]
F32 mod_VarianceAdjustment(F32 fVariance);

// Fills the earray with a list of the PowerDefs the AttribModDef may cause to be applied
void moddef_GetAppliedPowerDefs(SA_PARAM_NN_VALID AttribModDef *pdef,
								SA_PARAM_NN_VALID PowerDef ***pppPowerDefs);


// Utility function to check pending mods, make them active if needed, and merge them into
//  the active list.  Also adds fragile delayed mods to the fragile list if appropriate.
//  Should be called before processing the active list.
void character_ModsProcessPending(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract);

// Utility function to mark mods as either canceled or ignored based on CombatEvents or being disabled.
//  Should be called before processing the active list.
void character_ModsSuppress(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar);

// Applies any expiration effects of the AttribMod
void character_ApplyModExpiration(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID AttribMod *pmod, GameAccountDataExtract *pExtract);

// Utility function to check all the currently active mods, and see if they have expired. if
// so removed them from the active mods list. Should be called before processing the active list.
void character_ModsRemoveExpired(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract);

// Compares two attrib mods to determine which one has greater magnitude.
// Remarks: Shields use the health instead of magnitude for magnitude comparison
S32 mods_CompareMagnitude(SA_PARAM_NN_VALID AttribMod *pMod1, SA_PARAM_NN_VALID AttribMod *pMod2);

// Searches the list of AttribMods for the "best" one and returns its index.  If erPersonal is 0,
//  will not include bPersonal AttribMods.  If erPersonal is non-zero, bPersonal AttribMods that
//  match will be included.  Assumes all AttribMods are the "same" (Attribute, Aspect and ModStackGroup).
S32 mods_FindBest(SA_PARAM_OP_VALID AttribMod **ppMods, EntityRef erPersonal);

// Filters the list of AttribMods, removing anything that isn't the "best" in its ModStackGroup.
//  Assumes all AttribMods are the same Attribute, but not Aspect or ModStackGroup.
void mods_StackGroupFilter(AttribMod **ppMods);


// Utility function update the Character's AttribModNet data
void character_ModsUpdateNet(SA_PARAM_NN_VALID Character *pchar);

// Checks whether AttribModNet entity bucket fields need to be sent
bool character_ModsNetCheckUpdate(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID Character *pcharOld);

// Sends the Character's AttribModNets
void character_ModsNetSend(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Packet* pak);

// Receives the Character's AttribModNets.  Safely consumes all the AttribModNets if there isn't a Character.
void character_ModsNetReceive(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);

// Updates the AttribModNets for diffing next frame
void character_ModsNetUpdate(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID Character *pcharOld);

// Attempts to create fake AttribMods using the Character's AttribModNets
S32 character_ModsNetCreateFakeMods(SA_PARAM_NN_VALID Character *pchar);

// Gets the predicted duration of a mod net
U32 character_ModNetGetPredictedDuration(Character *pchar, AttribModNet *pmodNet);

// Gets the magnitude of all the AttribModNet's on the character that match the (optional) given tags
// optionally will fill all the matching AttribModNet in peaModNetsOut
F32 character_ModsNetGetTotalMagnitudeByTag(SA_PARAM_OP_VALID  Character *pchar, 
											AttribType eType, 
											SA_PARAM_OP_VALID S32 *piTags,
											SA_PARAM_OP_VALID AttribModNet ***peaModNetsOut);

AttribModNet* character_ModsNetGetByIndexAndTag(SA_PARAM_OP_VALID  Character *pchar, 
												AttribType eType, 
												SA_PARAM_OP_VALID S32 *piTags,
												S32 index);

// When most AttribMods need to apply a Power, they call this function to find the Character that
//  should be used for the TargetType tests.  Basically just a wrapper for a CombatConfig check
//  and erOwner lookup for now.  If the value returned is invalid, true is assigned to pbFailOut,
//  and the apply should probably be terminated.
SA_RET_OP_VALID Character* mod_GetApplyTargetTypeCharacter(int iPartitionIdx, SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID S32 *pbFailOut);


// Returns the list of attribs that the given attrib unrolls to, or NULL if the attrib does
//  not unroll.
AttribType *attrib_Unroll(AttribType eAttrib);

// Determines if the Attrib A can be found in the Attrib B.  This is trivially true
//  if they're the same Attrib.  If the Attrib B unrolls, this function will return true
//  if the Attrib A is in the unrolled list.
bool attrib_Matches(AttribType eAttribA, AttribType eAttribB);

// Cleans up side effects of a mod when it is done
void mod_Cancel(int iPartitionIdx, SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID Character *pchar, S32 bCancelAnimFX, SA_PARAM_OP_VALID AttribMod *pmodReplace, GameAccountDataExtract *pExtract);

// Loads and sets up the attrib groups
void attribsets_Load(void);

// Sorting function for setting higher priority attrib mods first
int attrib_sortfunc(const AttribMod **pmodA, const AttribMod **pmodB);



// Functions to handle animation and fx of AttribMods

// Fills in an appropriate id and subid for bits and FX for this AttribMod
void mod_AnimFXID(SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID U32 *uiID, SA_PARAM_NN_VALID U32 *uiSubID);

// Starts the continuing animation bits and FX the may turned on.  Conditional
//  bits and fx are handled elsewhere
void mod_AnimFXOn(int iPartitionIdx, SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID Character *pchar);

// Terminates animation bits and FX the AttribMod may have turned on
void mod_AnimFXOff(SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID Character *pchar);

// Looks through the mods and updates the conditional animation bits and fx
void character_UpdateConditionalModAnimFX(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar);

// Creates a CSV description of the attrib mod and concatenates it to the estring
void moddef_CSV(AttribModDef *pdef, char **estr, const char *pchPrefix);

// Counts similar AttribMods, stores them in the given StashTable* (which it creates
//  if it doesn't exist)
void moddef_RecordStaticPerf(SA_PARAM_NN_VALID AttribModDef *pdef, SA_PARAM_NN_VALID StashTable *pstStaticPerfs);

void moddef_PostTextReadFixup(AttribModDef *pdef, PowerDef *ppowDef);

S32 moddef_IsPredictedAttrib(SA_PARAM_NN_VALID AttribModDef *pdef);

// returns true if the moddef has a powerDef is can apply somehow (expiration def, apply powers, triggers, etc)
S32 moddef_HasUnownedPowerApplication(SA_PARAM_NN_VALID AttribModDef *pdef);


void AttribModLink_Free(SA_PARAM_OP_VALID AttribModLink *pLink);

AttribMod* AttribModLink_FindMod(SA_PARAM_NN_OP_VALID AttribMod **eaMods, SA_PARAM_NN_VALID AttribModLink *pLink, SA_PARAM_OP_VALID S32 *piIndexOut);

void AttribModLink_ModProcess(int iPartition, AttribMod *pMod, Character *pchar);

#endif
