#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "MultiVal.h"
#include "referencesystem.h"
#include "PowersEnums.h"

extern DictionaryHandle g_hPowerAnimFXDict;

// Forward declarations
typedef struct AttribModDef			AttribModDef;
typedef struct Character			Character;
typedef struct DynParamBlock		DynParamBlock;
typedef struct Expression			Expression;
typedef struct Power				Power;
typedef struct PowerActivation		PowerActivation;
typedef struct PowerApplication		PowerApplication;
typedef struct PowerDef				PowerDef;
typedef struct PowerRef				PowerRef;
typedef struct PowerEmit			PowerEmit;
typedef struct DynAnimGraph			DynAnimGraph;

// Copied from elsewhere
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

/***** DEFINES ******/

// How many PowerAnimFX 'Frames' there are per second
#define PAFX_FPS 30.0f

/***** END DEFINES ******/

/***** ENUMS *****/

AUTO_ENUM;
typedef enum PowerFXHitType
{
	kPowerFXHitType_Unset = 0,
		// Do the hit test when the activate FX is turned on
	kPowerFXHitType_UnsetEvalHitChanceWithoutPower,
		// Same as Unset, but evaluate the the hit chance even if there is no power instance
	kPowerFXHitType_Hit,
		// The hit test was already done, it returned true
	kPowerFXHitType_Miss,
		// The hit test was already done, it returned false
} PowerFXHitType;

AUTO_ENUM;
typedef enum PowerFXParamType
{
	kPowerFXParamType_FLT = MULTI_FLOAT,
		// Float

	kPowerFXParamType_STR = MULTI_STRING_F,
		// String

	kPowerFXParamType_VEC = MULTIOP_LOC_MAT4_F,
		// Mat4

	kPowerFXParamType_VC4 = MULTI_VEC4_F,
		// Vec4

} PowerFXParamType;

AUTO_ENUM;
typedef enum PowerAnimFXType
{
	// Special 'none' type
	kPowerAnimFXType_None = 1,

	// General anim/fx
	kPowerAnimFXType_StanceSticky,
	kPowerAnimFXType_PersistStanceSticky,
	kPowerAnimFXType_StanceFlash,
	kPowerAnimFXType_PersistStanceFlash,
	kPowerAnimFXType_StanceEmit,
	kPowerAnimFXType_ChargeSticky,
	kPowerAnimFXType_ChargeFlash,
	kPowerAnimFXType_LungeSticky,
	kPowerAnimFXType_LungeFlash,
	kPowerAnimFXType_MoveLungeSticky,
	kPowerAnimFXType_MoveLurchSticky,
	kPowerAnimFXType_ActivateSticky,
	kPowerAnimFXType_ActivateFlash,
	kPowerAnimFXType_Deactivate,
	kPowerAnimFXType_PreactivateSticky,
	kPowerAnimFXType_PreactivateFlash,
	kPowerAnimFXType_ActivationImmunity,

	// Special case anim/fx
	kPowerAnimFXType_Targeted,
	kPowerAnimFXType_HitSticky,
	kPowerAnimFXType_HitFlash,
	kPowerAnimFXType_HitFlag,
	kPowerAnimFXType_Block,
	kPowerAnimFXType_Death,

	// AttribMod anim/fx
	kPowerAnimFXType_Continuing,
	kPowerAnimFXType_Conditional,
	kPowerAnimFXType_ModUse,

	// Really special anim/fx
	kPowerAnimFXType_Carry,			// Carrying objects
	kPowerAnimFXType_ItemArt,		// Weapons/shields (and other kinds of Item-based Powers-relevant anim/fx)
	kPowerAnimFXType_FromPet,		// FX caused by having a pet active
	kPowerAnimFXType_NearDeath,		// NearDeath state related
	kPowerAnimFXType_PVP,
	kPowerAnimFXType_ReactivePowerPre,
	kPowerAnimFXType_ReactivePower,
	kPowerAnimFXType_CombatPowerStateSwitching,

	// External FX
	kPowerAnimFXType_Expr,			// Bits/FX from expressions
	
	kPowerAnimFXType_STOScanForClickies,	// For STO-specific scan for nearby clickies
	kPowerAnimFXType_Combat, // Special type for setting a combat anim bit
	kPowerAnimFXType_WeaponSwap,
} PowerAnimFXType;

// Typedef to track things that can effect movement
//  Currently in order of importance
//  0 is used to cancel all types
typedef enum PowerMoveType
{
	// General movement
	kPowerMoveType_Face = 1,
	kPowerMoveType_Lurch,
	kPowerMoveType_Lunge,

} PowerMoveType;

AUTO_ENUM;
typedef enum PowerLungeDirection
{
	kPowerLungeDirection_Target = 0,
		// Lunge at the target

	kPowerLungeDirection_TargetChase,
		// Lunge at the target and chase them
	
	kPowerLungeDirection_Down,
		// Lunge straight down

	kPowerLungeDirection_Away,
		// Lunge away from the target

} PowerLungeDirection;

// Defines the different ways a hit react can be triggered, which can improve
//  the syncing between the attack striking the target and the hit react occuring
AUTO_ENUM;
typedef enum AttackReactTrigger
{
	kAttackReactTrigger_Time = 0,
		// Hit/Block reacts are not triggered, they fire at a precalculated time

	kAttackReactTrigger_Bits,
		// Hit/Block reacts are trigged by the animation system (indirectly from the ActivateBits)

	kAttackReactTrigger_FX,
		// Hit/Block reacts are trigged by the fx system (directly from the ActivateFX)

	kAttackReactTrigger_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} AttackReactTrigger;

#define AttackReactTrigger_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kAttackReactTrigger_MAX <= (1 << (AttackReactTrigger_NUMBITS - 1)));

AUTO_ENUM;
typedef enum PowerAnimNodeSelectionType
{
	kPowerAnimNodeSelectionType_Default = 0, ENAMES(Default)
		// Use whatever is specified in the combat config, or random if nothing is specified

	kPowerAnimNodeSelectionType_RandomInRangeAndArc, ENAMES(RandomInRangeAndArc InRangeAndArc)
		// Choose a random node in range and arc of the power

	kPowerAnimNodeSelectionType_Random, ENAMES(Random)
		// The default selection type for FX if nothing is specified in the combat config

	kPowerAnimNodeSelectionType_ClosestInRangeAndArc, ENAMES(ClosestInRangeAndArc)
		// Always choose the closest node in range and arc of the power

	kPowerAnimNodeSelectionType_MAX, EIGNORE 
} PowerAnimNodeSelectionType;

#define PowerAnimNodeSelectionType_NUMBITS 3 // 8 bits can hold up to an enum value of 2^2-1, because of the sign bit
STATIC_ASSERT(kPowerAnimNodeSelectionType_MAX <= (1 << (PowerAnimNodeSelectionType_NUMBITS - 1)));

AUTO_ENUM;
typedef enum PowerAnimDirection
{
	PADID_Default,       //  0  0  0 (doesn't apply a direction, takes default path through anim. graph)
	PADID_Front_to_Back, //  0  0  1
	PADID_Back_to_Front, //  0  0 -1
	PADID_Right_to_Left, // -1  0  0
	PADID_Left_to_Right, //  1  0  0
	PADID_Up_to_Down,	 //  0 -1  0
	PADID_Down_to_Up,	 //  0  1  0
} PowerAnimDirection;

// Used for getting the texture from a costume
AUTO_ENUM;
typedef enum PowerAnimCostumeTexture
{
	kPowerAnimCostumeTexture_None = 0,
	kPowerAnimCostumeTexture_DetailTexture,
	kPowerAnimCostumeTexture_PatternTexture,
	kPowerAnimCostumeTexture_SpecularTexture,
	kPowerAnimCostumeTexture_DiffuseTexture,
}PowerAnimCostumeTexture;

/***** END ENUMS *****/

// Wrapper for a param and an expression that defines it
AUTO_STRUCT;
typedef struct PowerFXParam
{
	const char *cpchParam;		AST(NAME(Param), POOL_STRING)
		// Name of the param

	PowerFXParamType eType;		AST(NAME(Type))
		// Expected type of the param

	Expression* expr;			AST(NAME(ExprBlock), REDUNDANT_STRUCT(Expr, parse_Expression_StructParam), LATEBIND)
		// Expression that defines the param
} PowerFXParam;

// Completely StructParam'd version of the parse table for PowerFXParam, used like parse_Expression_StructParam
extern ParseTable parse_PowerFXParam_StructParam[];
#define TYPE_parse_PowerFXParam_StructParam PowerFXParam_StructParam


AUTO_STRUCT;
typedef struct PowerChargedActivate
{
	float fTimeMin;								AST(STRUCTPARAM)
		// Minimum charge time for this custom activation

	float fTimeMax;								AST(STRUCTPARAM DEFAULT(0.0))	// Default is defined so it isn't written if it matches
		// Maximum charge time for this custom activation

	const char **ppchActivateBits;				AST(NAME(ActivateBits), POOL_STRING)
		// Bits when the power finishes charging and is applied

	const char **ppchActivateFX;				AST(NAME(ActivateFX), POOL_STRING)
		// FX when the power finishes charging and is applied

	const char **ppchActivateMissFX;			AST(NAME(ActivateMissFX), POOL_STRING)
		// FX when the power finishes charging and is applied, but is going to miss the primary target
		//  If not specified, uses regular ActivateFX instead

	PowerFXParam **ppActivateFXParams;			AST(NAME(ActivateFXParamBlock), REDUNDANT_STRUCT(ActivateFXParam, parse_PowerFXParam_StructParam))
		// Params for ActivateFX
} PowerChargedActivate;

// TODO(JW): Hack: Temporary struct for handling activate fx.  Should be removed when fx are triggered by messages.
AUTO_STRUCT;
typedef struct PowerActivateFX
{
	const char *pchActivateFX;					AST(STRUCTPARAM, POOL_STRING)
		// Name of the FX

	int iActivateFrameOffset;					AST(STRUCTPARAM DEFAULT(0))
		// Frame offset from global FramesBeforeActivateFX
} PowerActivateFX;

// Tracks all the data relevant to lunging
AUTO_STRUCT;
typedef struct PowerLunge
{
	F32 fSpeed;
		// Normal speed of the lunge

	F32 fRangeMin;
		// Minimum range to the target for the lunge to occur

	F32 fRange;
		// Range of the lunge

	F32 fStopDistance;							AST(DEFAULT(-1))
		// the distance from the target we should stop at. 
		// if this field is negative, the default is used- configurable based on the combatConfig field LungeDefaultStopDistance

	S32 iFrameStart;
		// Starting frame of lunging movement (from when the power finishes charge)

	S32 iFramesOfActivate;
		// Frames of lunging movement during which the power should be activating

	PowerLungeDirection eDirection;
		// Direction to lunge

	S32 iStrictFrameDuration; 
		// if set will ignore lunge speed, speed will be derived from the distance and duration of the lunge

	U32 bUseRootDistance : 1;
		// if set, will use strict point to point distance instead of capsule distances

} PowerLunge;

AUTO_STRUCT;
typedef struct PowerLurch
{
	// todo(RP): it would be nice to fix up the powerArt data so this struct doesn't need to be EMBEDDED_FLAT
	//		and then we can remove the deprecated ADDNAMES

	int iSlideFrameStart;						AST(ADDNAMES(MovementFrameStart))
		// Starting frame of movement

	int iSlideFrameCount;						AST(ADDNAMES(MovementFrameCount))

	F32 fSlideDistance;							AST(ADDNAMES(MovementDistance))
		// Distance of sliding movement. Does not use any actual units

	F32 fMovementSpeed;
		// feet/sec Speed of movement when sliding

	F32	fLurchCapsuleBufferRadius;
		// an override to the CombatConfig's lurch.fAddedCapsuleRadius, 
		// which is the added distance for detecting an entity collision during a lurch

	REF_TO(DynAnimGraph) hMovementGraph;		AST(NAME(MovementGraph))		
		// The animation graph which contains the power movement data. 
		// If this is set, the slide values are not used

	U32 bLurchIgnoreCollision : 1;
		// if set, the lurch will ignore all collisions

	U32 bLurchSlideInMovementDirection : 1;
		// if set, the lurch will slide in the direction of the movement keys. 
		// this currently only works if the power is setup to always fire in the direction of the camera
		// but this feature doesn't make too much sense with proximity target assist

} PowerLurch;

// Tracks all the data relevant to grabbing
AUTO_STRUCT;
typedef struct PowerGrab
{
	F32 fTimeChase;
		// Max time to chase

	F32 fDistStart;
		// Distance to the target for the grab to start

	F32 fDistHold;
		// Distance to hold the target at

	S32 iFramesHold;
		// Frames to hold the target (in frames so it can more easily match animation data?)

	const char **ppchSourceBits;	AST(NAME(SourceBits), POOL_STRING)
		// Bits to play on the source during the grab

	const char **ppchTargetBits;	AST(NAME(TargetBits), POOL_STRING)
		// Bits to play on the target during the grab

} PowerGrab;

AUTO_STRUCT;
typedef struct PowerAnimFXCostumePartOverride
{
	const char *cpchFxGeo;	AST(NAME(FxGeo), POOL_STRING)
		// Override calls to GetCostumePartFxGeo() made by related powers

	Vec4 vecColor;			AST(NAME(Color))
	Vec4 vecColor1;			AST(NAME(Color1))
	Vec4 vecColor2;			AST(NAME(Color2))
	Vec4 vecColor3;			AST(NAME(Color3))
		// Override calls to GetCostumePartColor() index 0, 1, 2, 3

} PowerAnimFXCostumePartOverride;

AUTO_STRUCT;
typedef struct DragonPowerAnimFX
{
	// rotates the dragon's body to face in the direction of the head
	U32		bRotateBodyToHeadOrientation : 1;

	// rotates the dragon's head to face in the direction of the body
	U32		bRotateHeadToBodyOrientation : 1;
	
} DragonPowerAnimFX;

// Basic definition of a power fx
AUTO_STRUCT AST_IGNORE(RootOptionalHack) AST_IGNORE(StanceSTO) AST_IGNORE(PersistedWeaponStanceSTO) AST_IGNORE(RootDuringCharge) AST_IGNORE(RootDuringActivate);
typedef struct PowerAnimFX
{
	const char *cpchName;						AST(NAME(Name), STRUCTPARAM, KEY, POOL_STRING)
		// Internal name, generated at text load from cpchFile

	const char *cpchScope;						AST(NAME(Scope), POOL_STRING)
		// Internal scope, generated at text load from cpchFile

	const char *cpchFile;						AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)

	U32 uiStanceID;								AST(NO_TEXT_SAVE)
		// Stance ID based on stance bits and fx, generated at load time

	U32 uiPersistStanceID;						AST(NO_TEXT_SAVE)
		// Stance ID based on the persistent stance bits and fx, generated at load time

	F32 fStanceTransitionTime;					AST(NAME(StanceTransitionTime))
		// The amount of time that the activation should be delayed after entering the stance

	const char* pchAnimKeyword;					AST(NAME(AnimKeyword), POOL_STRING)
		// The one animation graph used by this poweranimfx

	const char **ppchStickyStanceWords;			AST(NAME(StickyStanceWords), POOL_STRING)
		// Bits for the duration of the power activation (including toggle/passive)

	const char* pchHitAnimKeyword;				AST(NAME(HitAnimKeyword), POOL_STRING)
		// The one animation graph used by this poweranimfx for hits

	const char* pchBlockAnimKeyword;			AST(NAME(BlockAnimKeyword), ADDNAMES(MissAnimKeyword), POOL_STRING)
		// The one animation graph used by this poweranimfx for misses

	//
	// STANCE / PERSIST STANCE

	const char **ppchStanceStickyBits;			AST(NAME(StanceStickyBits), POOL_STRING)
		// Bits for the duration of the power activation (including toggle/passive)

	const char **ppchPersistStanceStickyBits;	AST(NAME(PersistStanceStickyBits), POOL_STRING)
		// Bits that remain on until specifically told turn off

	const char **ppchStanceStickyFX;			AST(NAME(StanceStickyFX), POOL_STRING)
		// Array of FX for the duration of the power activation (including toggle/passive), defines the 'stance'

	const char **ppchPersistStanceStickyFX;		AST(NAME(PersistStanceStickyFX), POOL_STRING)
		// Array of FX that remains on until specifically told to turn off

	PowerFXParam **ppStanceStickyFXParams;		AST(NAME(StanceStickyFXParamBlock), REDUNDANT_STRUCT(StanceStickyFXParam, parse_PowerFXParam_StructParam))
		// Params for StanceStickyFX

	PowerFXParam **ppPersistStanceStickyFXParams; AST(NAME(PersistStanceStickyFXParamBlock), REDUNDANT_STRUCT(PersistStanceStickyFXParam, parse_PowerFXParam_StructParam))
		// Params for PersistStanceStickyFX

	PowerFXParam **ppPersistStanceInactiveStickyFXParams; AST(NAME(PersistStanceInactiveStickyFXParamBlock), REDUNDANT_STRUCT(PersistStanceInactiveStickyFXParam, parse_PowerFXParam_StructParam))
		// Params for inactive PersistStanceStickyFX

	const char **ppchStanceFlashBits;			AST(NAME(StanceFlashBits), POOL_STRING)
		// Bits when the power starts the activation process, defines the 'stance'

	const char **ppchPersistStanceFlashBits;	AST(NAME(PersistStanceFlashBits), POOL_STRING)
		// Bits when the power starts the activation process, defines the 'persistent stance'

	const char **ppchStanceFlashFX;				AST(NAME(StanceFlashFX), POOL_STRING)
		// FX when the power starts the activation process
	
	const char **ppchPersistStanceFlashFX;	AST(NAME(PersistStanceFlashFX), POOL_STRING)
		// FX when the power starts the activation process

	PowerFXParam **ppStanceFlashFXParams;		AST(NAME(StanceFlashFXParamBlock), REDUNDANT_STRUCT(StanceFlashFXParam, parse_PowerFXParam_StructParam))
		// Params for StanceFlashFX

	PowerFXParam **ppPersistStanceFlashFXParams; AST(NAME(PersistStanceFlashFXParamBlock), REDUNDANT_STRUCT(PersistStanceFlashFXParam, parse_PowerFXParam_StructParam))
		// Params for PersistStanceFlashFX

	//
	// CHARGE

	const char **ppchChargeStickyBits;			AST(NAME(ChargeStickyBits),ADDNAMES(ChargeBits), POOL_STRING)
		// Sticky bits while the power is charging

	const char **ppchChargeStickyFX;			AST(NAME(ChargeStickyFX),ADDNAMES(ChargeFX), POOL_STRING)
		// Maintained FX while the power is charging

	PowerFXParam **ppChargeStickyFXParams;		AST(NAME(ChargeStickyFXParamBlock), REDUNDANT_STRUCT(ChargeStickyFXParam, parse_PowerFXParam_StructParam), REDUNDANT_STRUCT(ChargeFXParam, parse_PowerFXParam_StructParam))
		// Params for ChargeStickyFX

	const char **ppchChargeFlashBits;			AST(NAME(ChargeFlashBits), POOL_STRING)
		// Flash bits started when the power starts charging

	const char **ppchChargeFlashFX;				AST(NAME(ChargeFlashFX), POOL_STRING)
		// Flash FX started when the power starts charging

	PowerFXParam **ppChargeFlashFXParams;		AST(NAME(ChargeFlashFXParamBlock), REDUNDANT_STRUCT(ChargeFlashFXParam, parse_PowerFXParam_StructParam))
		// Params for ChargeFlashFX
			
	//
	// LUNGE

	const char **ppchLungeStickyBits;			AST(NAME(LungeStickyBits), POOL_STRING)
		// Sticky bits while the power is lunging

	const char **ppchLungeStickyFX;				AST(NAME(LungeStickyFX), POOL_STRING)
		// Maintained FX while the power is lunging

	PowerFXParam **ppLungeStickyFXParams;		AST(NAME(LungeStickyFXParamBlock), REDUNDANT_STRUCT(LungeStickyFXParam, parse_PowerFXParam_StructParam))
		// Params for LungeStickyFX

	const char **ppchLungeFlashBits;			AST(NAME(LungeFlashBits), POOL_STRING)
		// Flash bits started when the power starts lunging

	const char **ppchLungeFlashFX;				AST(NAME(LungeFlashFX), POOL_STRING)
		// Flash FX started when the power starts lunging

	PowerFXParam **ppLungeFlashFXParams;		AST(NAME(LungeFlashFXParamBlock), REDUNDANT_STRUCT(LungeFlashFXParam, parse_PowerFXParam_StructParam))
		// Params for LungeFlashFX

	//
	// PRE-ACTIVATE
	
	PowerActivateFX **ppPreactivateFX;			AST(NAME(PreactivateFX))
	// FX when the power finishes charging

	PowerFXParam **ppPreactivateFXParams;		AST(NAME(PreactivateFXParamBlock), REDUNDANT_STRUCT(PreactivateFXParam, parse_PowerFXParam_StructParam))
	// Params for PreactivateFX

	const char **ppchPreactivateStickyBits;		AST(NAME(PreactivateStickyBits), POOL_STRING)
	// Sticky bits while the power is actually preactivating

	const char **ppchPreactivateStickyFX;		AST(NAME(PreactivateStickyFX), POOL_STRING)
	// Maintained FX while the power is activating

	PowerFXParam **ppPreactivateStickyFXParams;	AST(NAME(PreactivateStickyFXParamBlock), REDUNDANT_STRUCT(PreactivateStickyFXParam, parse_PowerFXParam_StructParam))
	// Params for ActivateStickyFX

	const char **ppchPreactivateBits;			AST(NAME(PreactivateBits), POOL_STRING)
	// Bits when the power is applied

	const char* pchPreactivateAnimFlag;			AST(NAME(PreactivateAnimFlag), POOL_STRING)
	// A flag to send to the anim graph (in the new anim system) upon activation


	//
	// ACTIVATE

	const char **ppchActivateStickyBits;		AST(NAME(ActivateStickyBits), POOL_STRING)
		// Sticky bits while the power is actually activating

	const char **ppchActivateStickyFX;			AST(NAME(ActivateStickyFX), POOL_STRING)
		// Maintained FX while the power is activating

	PowerFXParam **ppActivateStickyFXParams;	AST(NAME(ActivateStickyFXParamBlock), REDUNDANT_STRUCT(ActivateStickyFXParam, parse_PowerFXParam_StructParam))
		// Params for ActivateStickyFX

	const char **ppchActivateBits;				AST(NAME(ActivateBits), POOL_STRING)
		// Bits when the power finishes charging and is applied

	const char* pchActivateAnimFlag;			AST(NAME(ActivateAnimFlag), POOL_STRING)
		// A flag to send to the anim graph (in the new anim system) upon activation
		
	PowerActivateFX **ppActivateFX;				AST(NAME(ActivateFX))
		// FX when the power starts activating and is applied

	PowerActivateFX **ppActivateNearFX;				AST(NAME(ActivateNearFX))
		// FX when the source is close to the target, defined by fNearActivateFXDistance

	PowerActivateFX **ppActivateMissFX;			AST(NAME(ActivateMissFX))
		// FX when the power finishes charging and is applied, but is going to miss the primary target
		//  If not specified, uses regular ActivateFX instead

	PowerFXParam **ppActivateFXParams;			AST(NAME(ActivateFXParamBlock), REDUNDANT_STRUCT(ActivateFXParam, parse_PowerFXParam_StructParam))
		// Params for ActivateFX

	// 
	// PERIODIC

	const char **ppchPeriodicPreactivateBits;	AST(NAME(PeriodicPreactivateBits), POOL_STRING)
		// Flash bits when the power activates periodically.

	const char **ppchPeriodicActivateBits;		AST(NAME(PeriodicActivateBits), POOL_STRING)
		// Flash bits when the power activates periodically.
		//  If not specified, uses regular ActivateBits instead. (is this true?)

	PowerActivateFX **ppPeriodicActivateFX;		AST(NAME(PeriodicActivateFX))
		// Flash FX when the power applies periodically
		//  If not specified, uses regular ActivateFX instead.

	PowerFXParam **ppPeriodicActivateFXParams;	AST(NAME(PeriodicActivateFXParamBlock), REDUNDANT_STRUCT(PeriodicActivateFXParam, parse_PowerFXParam_StructParam))
		// Params for PeriodicActivateFX
	
	// 
	// DEACTIVATE

	const char **ppchDeactivateBits;			AST(NAME(DeactivateBits), POOL_STRING)
		// Bits when the power is turned off (for powers that get turned off)

	const char **ppchDeactivateFX;				AST(NAME(DeactivateFX), POOL_STRING)
		// FX when the power is turned off (for powers that get turned off)

	PowerFXParam **ppDeactivateFXParams;		AST(NAME(DeactivateFXParamBlock), REDUNDANT_STRUCT(DeactivateFXParam, parse_PowerFXParam_StructParam))
		// Params for DeactivateFX

	const char *pchDeactivateAnimFlag;			AST(NAME(DeactivateAnimFlag), POOL_STRING)
		// A flag to send to the anim graph (in the new anim system) upon deactivation (for powers that get turned off)
	
		
	PowerChargedActivate **ppChargedActivate;	AST(NAME(ChargedActivate))
		// Earray of custom overrides for activations on charged powers
		
	//
	// TARGETED

	const char **ppchTargetedBits;				AST(NAME(TargetedBits), POOL_STRING)
		// Flash bits on target when it is targeted by this power
	
	const char **ppchTargetedFX;				AST(NAME(TargetedFX), POOL_STRING)
		// FX flashed on target when it is targeted by this power

	PowerFXParam **ppTargetedFXParams;			AST(NAME(TargetedFXParamBlock), REDUNDANT_STRUCT(TargetedFXParam, parse_PowerFXParam_StructParam))
		// Params for TargetedFX

	//
	// HIT

	const char **ppchHitBits;					AST(NAME(HitBits), POOL_STRING)
		// Flash bits on target when it is hit by this power

	const char **ppchHitFX;						AST(NAME(HitFX), POOL_STRING)
		// FX flashed on target when it is hit by this power

	PowerFXParam **ppHitFXParams;				AST(NAME(HitFXParamBlock), REDUNDANT_STRUCT(HitFXParam, parse_PowerFXParam_StructParam))
		// Params for HitFX

	const char **ppchHitStickyFX;				AST(NAME(HitStickyFX), POOL_STRING)
		// Array of one-shot FX on target when it is hit by this power.  They are all given the same
		//  params

	PowerFXParam **ppHitStickyFXParams;			AST(NAME(HitStickyFXParamBlock), REDUNDANT_STRUCT(HitStickyFXParam, parse_PowerFXParam_StructParam))
		// Params for HitStickyFX

	//
	// BLOCK
	
	const char **ppchBlockBits;					AST(NAME(BlockBits), ADDNAMES(MissBits), POOL_STRING)
		// Flash bits on target when it blocks this power

	const char **ppchBlockFX;					AST(NAME(BlockFX), ADDNAMES(MissFX), POOL_STRING)
		// One-shot FX on target when it blocks this power

	PowerFXParam **ppBlockFXParams;				AST(NAME(BlockFXParamBlock), ADDNAMES(MissFXParamBlock), REDUNDANT_STRUCT(BlockFXParam, parse_PowerFXParam_StructParam))
		// Params for BlockFX

	// 
	// DEATH

	const char **ppchDeathBits;					AST(NAME(DeathBits), POOL_STRING)
		// Flash bits on target when it dies in a tick it took damage from this power

	const char **ppchDeathFX;					AST(NAME(DeathFX), POOL_STRING)
		// One-shot FX on target when it dies in a tick it took damage from this power

	PowerFXParam **ppDeathFXParams;				AST(NAME(DeathFXParamBlock), REDUNDANT_STRUCT(DeathFXParam, parse_PowerFXParam_StructParam))
		// Params for DeathFX

	const char **ppchDeathAnimStanceWords;			AST(NAME(DeathAnimStanceWords), POOL_STRING)
		// Stances to set (new animation system) on death while something else plays the Death keyword
	
	PowerAnimDirection eDeathDirection;	AST(NAME(DeathDirection))
		// The direction which the source is hitting the target from to cause its death (in the src's frame of reference)

	PowerAnimDirection eHitDirection; AST(NAME(HitDirection))
		// The direction which the source is hitting the target from (in the src's frame of reference), shouldn't be used if a reaction trigger is also set (fails validation)

	//
	// FRAMESBEFORE TIMING
	
	F32 fNearActivateFXDistance;
		// If the source is within this distance from the target, play ppActivateNearFX instead of ppActivateFX

	int iFramesBeforeActivateBits;
		// Frames to delay Activate bits
		
	int iFramesBeforeActivateFX;
		// Frames to delay Activate fx

	int iFramesBeforeTargeted;
		// Frames to delay TargetedBits/FX
	
	int *piFramesBeforeHit;
		// Array of frames to play HitBits/FX

	int iFramesBeforeBlock;
		// Frames to delay BlockBits/FX

	
	// Duplicate FramesBefore fields for periodic parts of attacks

	int iFramesBeforePeriodicActivateBits;
		// Frames to delay periodic Activate bits

	int iFramesBeforePeriodicActivateFX;
		// Frames to delay periodic Activate fx

	int iFramesBeforePeriodicTargeted;
		// Frames to delay periodic TargetedBits/FX

	int *piFramesBeforePeriodicHit;
		// Array of frames to play periodic HitBits/FX

	int iFramesBeforePeriodicBlock;
		// Frames to delay periodic BlockBits/FX

	F32 fDefaultHue;
		// Default Hue used for all the FX in this file

	REF_TO(PowerEmit) hDefaultEmit;			AST(NAME(DefaultEmit))
		// Default Emit to be used with this PowerAnimFX

	// Movement

	F32 fProjectileSpeed;
		// Speed of the projectile, if there is one

	F32 fMeleeSwingStartAngle;
		// DelayedHit must be set. In degrees, where the melee swing starts in relation to the direction of the atta. A positive direction should be to the right

	F32 fMeleeSwingAnglePerSecond;
		// DelayedHit must be set. In degrees per second, the rate to calculate the hit time 

	F32 fMeleeSwingHitPauseTime;
		// if the attack has a hit-pause, the amount of time to add of the target is on the opposite side of the facing from the starting angle.

	
	F32 fSpeedPenaltyDuringCharge;
		// Penalty applied to all movement speeds while Charging

	F32 fSpeedPenaltyDuringPreactivate;
		// Penalty applied to all movement speeds while Preactivating

	F32 fSpeedPenaltyDuringActivate;
		// Penalty applied to all movement speeds while Activating
	
	PowerLunge *pLunge;							AST(NAME(Lunge))
		// Data for an optional lunge movement

	PowerGrab *pGrab;							AST(NAME(Grab))
		// Data for an optional grab movement

	PowerLurch lurch;							AST(EMBEDDED_FLAT)
		// todo(rp): it would be nice to fix up the data so we don't need to EMBEDDED_FLAT this, and then this struct could be optional (a pointer to PowerLurch)
			
	F32 fFinalFloaterPercent;
		// How much of the damage to weight onto the last floater of a multi-floater hit
		
	// Misc other stuff

	PowerAnimFXCostumePartOverride *pCostumePartOverride;
		// Optional override to any calls to GetCostumePartX() made by related powers
	
	const char *cpchPCBoneName;						AST(POOL_STRING, NO_TEXT_SAVE)
		// Derived pooled string of any player costume bone lookup any fx param performs

	const char *pchLocTargetingReticleFxName;		AST(NAME("LocTargetingReticleFxName") POOL_STRING)
		// if the power uses location targeting, this is an override to the reticle FX

	DragonPowerAnimFX	*pDragon;

	U32 bfUsedFields[6];							AST(USEDFIELD)
		// TextParser UsedField array

	// Misc flags

	U32 bPlayChargeDuringActivate : 1;
		// Starts and immediately stops the Charge stage at the same time as the Activate stage.
		//  With this it's assumed iFramesBeforeActivateBits/FX is set so that the Charge stage
		//  plays for some specific number of frames.

	U32 bKeepStance : 1;
		// If true, this PowerAnimFX doesn't cause the existing PowerAnimFX stance to be replaced

	U32 bEmitCustomizable : 1;
		// If true, a Power with this PowerAnimFX can have its PowerEmit customized

	U32 bActivateFXOffsetOnMiss : 1;
		// If true the Activate FX will be offset from the target when the application misses.

	U32 bDelayedHit : 1;
		// If true the hit/block is delayed based on the distance between the source and target

	U32 bDelayedHitShared : 1;
		// If true and bDelayedHit is true, the delayed is shared among all targets, based on the
		//  delay to the first target (generally primary target at the center of a sphere)

	U32 bLocationHit : 1;
		// If true the target/hit/block fx are played at the target location if no actual target
		//  exists

	U32 bNoPeriodicHitBits : 1;				AST(ADDNAMES(NoPeriodicHitAnimKeyword))
		// If true the hit bits are not played on periodic powers except for the initial application

	U32 bMainTargetOnly : 1;
		// True if the bits and fx only apply to the main target, rather than all affected targets

	U32 bGlobalFX : 1;
		// True if the fx should be visible globally, and are thus sent through the transport entity.

	U32 bCapsuleHit : 1;
		// If true, the fx should hit the side of the capsule, rather than a bone

	U32 bDisableFaceActivate : 1;			AST(ADDNAMES(DisableTurnToFaceTarget))
		// If the character should not perform activation facing for this power

	U32 bLockFacingDuringActivate : 1;		AST(ADDNAMES(FaceActivateAllowForward))
		// If the Character is allowed to use itself as the activation face target for this
		//  Power, which effectively means they face the direction they were already facing.

	U32 bLockFacingDuringPreactivate : 1;
		// If the Character is allowed to use itself as the activation face target for this
		//  Power, which effectively means they face the direction they were already facing.

	U32 bLocationActivate : 1;
		// This flag is used usually with location targeting where the player fires a power at an arbitrary location in the world.
		// In those cases, the main target is actually self and the FX would play on the character. If this is set to true, the 
		// target entity is ignored and target becomes the position rather than the target entity.

	U32 bFacingOnlyDuringBaseActivateTime : 1;
		// if set, facing will only be the duration of the power's TimeActivate and not include activate periods and post-maintained time.

	U32 bFlashTriggersStanceSwitch : 1; AST(DEFAULT(1))
		// If false, flash bits and FX will not automatically trigger a stance switch

	U32 bHasSticky : 1;	AST(NO_TEXT_SAVE)
		// Derived.  Set if this has any sticky bits or fx/params on it.

	U32 bAlwaysChooseSameNode : 1;
		// When using a jitter list, always select the same node based on the activation ID

	U32 bDerivePersistStanceFromItem : 1;
		// HACK: flag to derive the persist stance from another power on the item

	AttackReactTrigger eReactTrigger : AttackReactTrigger_NUMBITS; AST(SUBTABLE(AttackReactTriggerEnum))
		// What triggers the Hit or Block react.  If not set, reacts trigger at a specific time.
	
	PowerAnimNodeSelectionType eNodeSelection : PowerAnimNodeSelectionType_NUMBITS; AST(NAME(NodeSelection, ChooseNodesInRangeAndArc) SUBTABLE(PowerAnimNodeSelectionTypeEnum))
		// What type of node selection to do

	// FX Flags

	U32 bChargeFXNeedsPowerScales : 1;
		// For charge FX, tells the powers that it needs to supply the area of effect scales to the FX
	
	U32 bActivateFXNeedsPowerScales : 1;
		// For the Activate FX, tells the powers that it needs to supply the area of effect scales to the FX
	
	
} PowerAnimFX;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PowerAnimFXRef
{
	REF_TO(PowerAnimFX) hFX;
		// Reference to the PowerAnimFX
	U32 uiSrcEquipSlot;
		//which equipslot this fx came from
	F32 fHue;
		// The hue to be used with the FX in that PowerAnimFX
} PowerAnimFXRef;

AUTO_STRUCT;
typedef struct PowerAnimFXJitterListFilterData
{
	F32 fYawOffset;
	F32 fFilterNodesInArc;
	F32 fFilterNodesInRange;
} PowerAnimFXJitterListFilterData;

// Wrappers for powerdef_AnimFXID
U32 power_AnimFXID(SA_PARAM_NN_VALID Power *ppow);
U32 powerref_AnimFXID(SA_PARAM_NN_VALID PowerRef *ppowRef);

// Returns a consistent ID number for powers that don't always have
//  usable activation ID numbers
U32 powerdef_AnimFXID(SA_PARAM_OP_VALID PowerDef *pdef, U32 uiPowerID);

// Finds all the PCBones used by the Entity's Powers and pushes them into the earray
void entity_FindPowerFXBones(SA_PARAM_NN_VALID Entity *pent, SA_PARAM_NN_VALID const char ***pppchBones);


// Sets the character's root status due to power activation
void character_PowerActivationRootStart(SA_PARAM_NN_VALID Character *pchar, 
										U8 uchID, PowerAnimFXType eType, U32 uiTimestamp,
										SA_PARAM_OP_VALID char *pchCause);

void character_PowerActivationRootStop(SA_PARAM_NN_VALID Character *pchar,
									   U8 uchID, PowerAnimFXType eType, U32 uiTimestamp);

void character_PowerActivationRootCancel(SA_PARAM_NN_VALID Character *pchar,
										 U8 uchID, PowerAnimFXType eType);

// Sets the character's root status due to generic combat
void character_GenericRoot(Character *p, bool bRooted, U32 uiTimestamp);

// Sets the character's hold status due to generic combat
void character_GenericHold(Character *p, bool bHeld, U32 uiTimestamp);


// Resets the character's movement state and errorfs with useful data
void character_MovementReset(SA_PARAM_NN_VALID Character *pchar);




// Flashes the given bits on the character
void character_FlashBitsOn(SA_PARAM_NN_VALID Character *pchar,
						   U32 uiID,
						   U32 uiSubID,
						   PowerAnimFXType eType,
						   EntityRef erSource,
						   SA_PRE_NN_NN_STR const char **ppchBits,
						   U32 uiTime,
						   S32 bTrigger,
						   S32 bTriggerIsEntityID,
						   S32 bTriggerMultiHit,
						   S32 bNeverCancel);

// Turns on the given bits on the character
void character_StickyBitsOn(SA_PARAM_NN_VALID Character *pchar, 
							U32 uiID,
							U32 uiSubID,
							PowerAnimFXType eType,
							EntityRef erSource,
							SA_PRE_NN_NN_STR const char **ppchBits, 
							U32 uiTime);

// Turns off the given bits on the character
void character_StickyBitsOff(SA_PARAM_NN_VALID Character *pchar, 
							 U32 uiID,
							 U32 uiSubID,
							 PowerAnimFXType eType,
							 EntityRef erSource,
							 SA_PRE_NN_NN_STR const char **ppchBits, 
							 //bool bKeep, 
							 U32 uiTime);

void character_SendAnimKeywordOrFlag(Character *pchar,
						 U32 uiID,
						 U32 uiSubID,
						 PowerAnimFXType eType,
						 EntityRef erSource,
						 const char* pchKeyword,
						 PowerActivation* pact,
						 U32 uiTime,
						 S32 bTrigger,
						 S32 bTriggerIsEntityID,
						 S32 bTriggerMultiHit,
						 S32 bIsKeyword,
						 S32 bAssumeControl,
						 S32 bForceDetailFlag);

void character_StanceWordOn(SA_PARAM_NN_VALID Character *pchar, 
							U32 uiID,
							U32 uiSubID,
							PowerAnimFXType eType,
							EntityRef erSource,
							SA_PRE_NN_NN_STR const char **ppchStanceWord, 
							U32 uiTime);

void character_StanceWordOff(SA_PARAM_NN_VALID Character *pchar, 
							U32 uiID,
							U32 uiSubID,
							PowerAnimFXType eType,
							EntityRef erSource,
							SA_PRE_NN_NN_STR const char **ppchStanceWord, 
							U32 uiTime);

// Cancels the given bits on the character
void character_BitsCancel(Character *pchar, 
						  U32 uiID,
						  U32 uiSubID,
						  PowerAnimFXType eType,
						  EntityRef erSource);


// Flashes the given fx from the source character to the target character/location
void character_FlashFX(int iPartitionIdx,
					   SA_PARAM_NN_VALID Character *pcharSource,
					   U32 uiID,
					   U32 uiSubID,
					   PowerAnimFXType eType,
					   SA_PARAM_OP_VALID Character *pcharTarget,
					   SA_PRE_OP_RELEMS(3) const Vec3 vecTarget,
					   PowerApplication *papp,
					   PowerActivation *pact,
					   Power *ppow,
					   const char **ppchFXNames,
					   PowerFXParam **ppParams,
					   F32 fHue,
					   U32 uiTime,
					   EPowerAnimFXFlag ePowerAnimFlags,
					   PowerAnimNodeSelectionType eNodeSelectType);

// Cancels the given fx flash from the source character to the target character/location
void character_FlashFXCancel(SA_PARAM_NN_VALID Character *pcharSource,
							 U32 uiID,
							 U32 uiSubID,
							 PowerAnimFXType eType,
							 SA_PARAM_OP_VALID Character *pcharTarget,
							 const char **ppchFXNames,
							 int bGlobal);

// Flashes the given fx from the source character to the target character/location
// TODO(JW): Hack: Temp ActivateFX version
void character_FlashActivateFX(int iPartitionIdx,
							   SA_PARAM_NN_VALID Character *pcharSource,
							   U32 uiID,
							   U32 uiSubID,
							   PowerAnimFXType eType,
							   SA_PARAM_OP_VALID Character *pcharTarget,
							   SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
							   PowerApplication *papp,
							   PowerActivation *pact,
							   Power *ppow,
							   PowerActivateFX **ppFX,
							   PowerFXParam **ppParams,
							   F32 fHue,
							   U32 uiTime,
							   EPowerAnimFXFlag ePowerAnimFlags,
							   PowerAnimNodeSelectionType eNodeSelectType);


// Cancels the given fx flash from the source character to the target character/location
// TODO(JW): Hack: Temp ActivateFX version
void character_FlashActivateFXCancel(SA_PARAM_NN_VALID Character *pcharSource,
									 U32 uiID,
									 U32 uiSubID,
									 PowerAnimFXType eType,
									 SA_PARAM_OP_VALID Character *pcharTarget,
									 PowerActivateFX **ppFX,
									 int bGlobal);

// Turns on a sticky FX from source vector to target character/location
void location_StickyFXOn(Vec3 vecSource,
						 int iPartitionIdx,
						 U32 uiID,
						 U32 uiSubID,
						 PowerAnimFXType eType,
						 Character *pcharTarget,
						 Vec3 vecTarget,
						 PowerActivation *pact,
						 Power *ppow,
						 const char **ppchFXNames,
						 PowerFXParam **ppParams,
						 F32 fHue,
						 U32 uiTime,
						 EPowerAnimFXFlag ePowerAnimFlags);

void location_StickyFXOff(int iPartitionIdx,
						  U32 uiID,
						  U32 uiSubID,
						  PowerAnimFXType eType,
						  Character *pcharTarget,
						  U32 uiTime);

// Turns on the given fx on the character
void character_StickyFXOn(int iPartitionIdx,
						  SA_PARAM_NN_VALID Character *pchar,
						  U32 uiID,
						  U32 uiSubID,
						  PowerAnimFXType eType,
						  SA_PARAM_OP_VALID Character *pcharTarget,
						  SA_PRE_OP_RELEMS(3) const Vec3 vecTarget,
						  SA_PARAM_OP_VALID PowerApplication *papp,
						  SA_PARAM_OP_VALID PowerActivation *pact, 
						  SA_PARAM_OP_VALID Power *ppow,
						  SA_PARAM_OP_VALID const char **ppchFXName, 
						  SA_PARAM_OP_VALID PowerFXParam **ppParams, 
						  F32 fHue, 
						  U32 uiTime,
						  EPowerAnimFXFlag ePowerAnimFlags,
						  PowerAnimNodeSelectionType eNodeSelectType,
						  U32 uiEquipSlot);


// Turns off the given fx on the character
void character_StickyFXOff(SA_PARAM_NN_VALID Character *pchar,
						   U32 uiID,
						   U32 uiSubID,
						   PowerAnimFXType eType,
						   SA_PARAM_OP_VALID Character *pcharTarget,
						   SA_PARAM_OP_VALID const char **ppchFXName, 
						   U32 uiTime,
						   int bGlobal);

// Turns off the given fx on the character
void character_StickyFXCancel(SA_PARAM_NN_VALID Character *pchar,
							  SA_PARAM_OP_VALID Character *pcharTarget, 
							  U32 uiID,
							  U32 uiSubID,
							  PowerAnimFXType eType,
							  SA_PARAM_OP_VALID const char **ppchFXName,
							  int bGlobal);


// Movement

// Starts a turn-to-face movement
void character_MoveFaceStart(int iPartitionIdx,
							 SA_PARAM_NN_VALID Character *pchar,
							 SA_PARAM_NN_VALID PowerActivation *pact,
							 PowerAnimFXType eType);

// Stops a face movement (by sending a Face with the same ID with the given stop time)
void character_MoveFaceStop(SA_PARAM_NN_VALID Character *pchar,
							SA_PARAM_NN_VALID PowerActivation *pact,
							U32 uiTimeStop);

// Starts a lunging movement, based on the data in the power activation
void character_MoveLungeStart(SA_PARAM_NN_VALID Character *pchar,
							  SA_PARAM_NN_VALID PowerActivation *pact);

// Starts a sliding movement
void character_MoveLurchStart(int iPartitionIdx,
							  SA_PARAM_NN_VALID Character *pchar,
							  U8 uchID,
							  SA_PARAM_OP_VALID PowerActivation *pact,
							  SA_PARAM_NN_VALID PowerAnimFX *pafx,
							  EntityRef erTarget,
							  SA_PRE_OP_RELEMS(3) const Vec3 vecTarget,
							  U32 uiTime);

// Cancels all unstarted movements with the given id/type (0 is wildcard)
void character_MoveCancel(SA_PARAM_NN_VALID Character *pchar, U32 uiID, PowerMoveType eType);



// Charging

// Starts the charge bits and fx
void character_AnimFXChargeOn(int iPartitionIdx,
							  SA_PARAM_NN_VALID Character *pchar,
							  SA_PARAM_NN_VALID PowerActivation *pact,
							  SA_PARAM_OP_VALID Power *ppow,
							  SA_PRE_OP_RELEMS(3) const Vec3 vecTarget);

// Stops the charge bits and fx.  If the server and client will both do this, but at different times,
//  set bSynced to false.
void character_AnimFXChargeOff(int iPartitionIdx,
							   SA_PARAM_NN_VALID Character *pchar,
							   SA_PARAM_NN_VALID PowerActivation *pact,
							   U32 uiTime,
							   bool bSynced);

// Cancels the charge bits/fx/moves
void character_AnimFXChargeCancel(int iPartitionIdx,
								  SA_PARAM_NN_VALID Character *pchar,
								  SA_PARAM_NN_VALID PowerActivation *pact,
								  bool bInterruped);

// Starts the charge bits and fx from a location
void location_AnimFXChargeOn(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecSource,
							 int iPartitionIdx,
							 SA_PARAM_NN_VALID PowerAnimFX *pafx,
							 SA_PARAM_OP_VALID Character *pcharTarget,
							 SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
							 U32 uiTime,
							 U32 uiID);


// Lunging

// Starts and stops the lunge bits and fx based on the times in the activation
void character_AnimFXLunge(int iPartitionIdx,
						   SA_PARAM_NN_VALID Character *pchar,
						   SA_PARAM_NN_VALID PowerActivation *pact);


// Grabbing

// Starts the grab state
S32 character_AnimFXGrab(int iPartitionIdx,
						 SA_PARAM_NN_VALID Character *pchar,
						 SA_PARAM_NN_VALID PowerActivation *pact);

// Returns the effective grab state
S32 character_GetAnimFXGrabState(SA_PARAM_NN_VALID Character *pchar);


// Activate and Deactivate
void character_AnimFxPowerActivationImmunity(	int iPartitionIdx, 
												SA_PARAM_NN_VALID Character *pchar,
												SA_PARAM_NN_VALID PowerActivation *pact);

void character_AnimFxPowerActivationImmunityCancel(	int iPartitionIdx, 
													Character *pchar,
													PowerActivation *pact);

// Starts the proper activate bits and fx from a character.  An Activation is
//  preferred, but if none is available a PowerAnimFX and hue will do.
void character_AnimFXActivateOn(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_OP_VALID PowerApplication *papp,
								SA_PARAM_OP_VALID PowerActivation *pact,
								SA_PARAM_OP_VALID Power *ppow,
								SA_PARAM_OP_VALID Character *pcharTarget,
								SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
								U32 uiTime,
								U32 uiActID,
								U32 uiActSubID,
								PowerFXHitType eHitType);

// Starts the proper activate bits and fx from a location
void location_AnimFXActivateOn(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecSource,
							   int iPartitionIdx,
							   SA_PARAM_NN_VALID PowerAnimFX *pafx,
							   SA_PARAM_OP_VALID Character *pcharTarget,
							   SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
							   SA_PRE_OP_ELEMS(4) SA_POST_OP_VALID Quat quatTarget,
							   U32 uiTime,
							   U32 uiID);

// Starts the proper anims and fx from a character.
void character_AnimFXPreactivateOn(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_OP_VALID PowerApplication *papp,
								SA_PARAM_OP_VALID PowerActivation *pact,
								SA_PARAM_OP_VALID Power *ppow,
								SA_PARAM_OP_VALID Character *pcharTarget,
								SA_PRE_OP_ELEMS(3) Vec3 vecTarget,
								U32 uiTime,
								U32 uiActID,
								U32 uiActSubID);

// Stops anims and fx on a character.
void character_AnimFXPreactivateOff(int iPartitionIdx,
								 Character *pchar,
								 PowerActivation *pact,
								 U32 uiTime, 
								 bool bCancel);

void character_AnimFXPreactivateCancel(	int iPartitionIdx,
										Character *pchar,
										PowerActivation *pact);

// Stops the sticky hit fx on the PowerActivation, with optional override of the targets, so that
//  they can be shut off on specific targets in the middle of an activation.
void character_AnimFXHitStickyFXOff(int iPartitionIdx,
									SA_PARAM_NN_VALID Character *pchar,
									SA_PARAM_NN_VALID PowerActivation *pact,
									U32 uiTime,
									SA_PARAM_OP_VALID EntityRef *perTargets);

// Stops the sticky activate bits and fx
void character_AnimFXActivateOffEx(	int iPartitionIdx,
									SA_PARAM_NN_VALID Character *pchar,
									SA_PARAM_NN_VALID PowerActivation *pact,
									U32 uiTime,
									bool bKeepAnimsAndPenalties);
#define character_AnimFXActivateOff(i,c,a,t) character_AnimFXActivateOffEx((i),(c),(a),(t),false)

// Cancels the all activate bits and fx
void character_AnimFXActivateCancel(int iPartitionIdx,
									SA_PARAM_NN_VALID Character *pchar,
									SA_PARAM_NN_VALID PowerActivation *pact,
									bool bInterrupted,
									bool bDoNotCancelFlashFX);

// Cancels the most recent periodic activate bits and fx
void character_AnimFXActivatePeriodicCancel(SA_PARAM_NN_VALID Character *pchar,
											SA_PARAM_NN_VALID PowerActivation *pact,
											SA_PARAM_OP_VALID Character *pcharTarget);

// Starts the deactivate bits and fx
void character_AnimFXDeactivate(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_NN_VALID PowerActivation *pact,
								U32 uiTime);


// Apply

// Starts the targeted bits and fx
void character_AnimFXTargeted(int iPartitionIdx,
							  SA_PARAM_NN_VALID Character *pcharTarget,
							  SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetHit,
							  SA_PARAM_NN_VALID PowerApplication *papp);

// Starts the hit bits and fx
void character_AnimFXHit(int iPartitionIdx,
						 SA_PARAM_NN_VALID Character *pcharTarget,
						 SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetHit,
						 SA_PARAM_NN_VALID PowerApplication *papp,
						 F32 fDelayProjectile,
						 U32 bBits,
						 U32 *bAppliedHitPause);

// Starts the block bits and fx
void character_AnimFXBlock(int iPartitionIdx,
						   SA_PARAM_NN_VALID Character *pcharTarget,
						   SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTargetHit,
						   SA_PARAM_NN_VALID PowerApplication *papp,
						   F32 fDelayProjectile);

// Starts the targeted and hit fx at the given location
void location_AnimFXHit(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
						SA_PARAM_NN_VALID PowerApplication *papp,
						F32 fDelayHit);



// Starts the death bits and fx
void character_AnimFXDeath(int iPartitionIdx,
						   SA_PARAM_NN_VALID Character *pcharTarget,
						   SA_PARAM_OP_VALID Character *pcharSource,
						   SA_PARAM_NN_VALID PowerAnimFX *pafx,
						   U32 uiTime);


// Special

// Starts the custom carry bits/fx, based on the data in the AttribModDef
void character_AnimFXCarryOn(int iPartitionIdx,
							 SA_PARAM_NN_VALID Character *pchar,
							 SA_PARAM_NN_VALID AttribModDef *pdef,
							 U32 uiID);

// Stops the custom carry bits/fx (actually a cancel for ease of use)
void character_AnimFXCarryOff(SA_PARAM_NN_VALID Character *pchar);

// 
void poweranimfx_DragonStartPowerActivation(SA_PARAM_NN_VALID Character *pChar, 
											SA_PARAM_NN_VALID PowerAnimFX *pAfx,
											SA_PARAM_NN_VALID PowerActivation *pAct,
											SA_PARAM_OP_VALID PowerDef *pPowerDef);


// Stances

// Sets the Character's default stance to the given PowerDef.  If null, the
//  Character's default stance is cleared.
void character_SetDefaultStance(int iPartitionIdx,
							    SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_OP_VALID PowerDef *pdef);

// Puts the character into the stance of the given power.  If bReplace is true
//  the power will be considered the base stance of the character.  Returns
//  true if the power is placing the character into a new stance
int character_EnterStance(int iPartitionIdx,
						  SA_PARAM_NN_VALID Character *pchar,
						  SA_PARAM_OP_VALID Power *ppow,
						  SA_PARAM_OP_VALID PowerActivation *pact,
						  int bReplace,
						  U32 uiTime);

// Puts the character into the persist stance of the given power. Returns
//  true if the power is placing the character into a new stance
int character_EnterPersistStance(int iPartitionIdx,
								 Character *pchar,
								 Power *ppow,
								 PowerDef *pdef,
								 PowerActivation *pact,
								 U32 uiTime,
								 U32 uiRequestID,
								 bool bInactiveStance);

// Takes the character out of the stance in the pafx.  Does NOT update
//  the base stance of the character
void character_ExitStance(SA_PARAM_NN_VALID Character *pchar,
						  SA_PARAM_NN_VALID PowerAnimFX *pafx,
						  U32 uiID,
						  U32 uiTime);

// Takes the character out of the persist stance in the pafx.
void character_ExitPersistStance(Character *pchar,
								 PowerAnimFX *pafx,
								 U32 uiID,
								 U32 uiTime);

// Returns true if the two powers use the same 'stance'
int poweranimfx_StanceMatch(SA_PARAM_OP_VALID PowerDef *pdefA,
							SA_PARAM_OP_VALID PowerDef *pdefB);

// Returns true if the two powers use the same 'persistent stance'
int poweranimfx_PersistStanceMatch(PowerDef *pdefA,
								   PowerDef *pdefB);

// Returns true if the given stance ID matches the empty stance ID
int poweranimfx_IsEmptyStance(U32 uiStanceID);

// Returns true if the given persist stance ID matches the empty persist stance ID
int poweranimfx_IsEmptyPersistStance(U32 uiStanceID);


// Other

// Generates the FX Params for the AttribModDef's FX
void moddef_GenerateFXParams(SA_PARAM_NN_VALID AttribModDef *pmoddef);

// Copies the AttribModDef's PowerDef's Icon data into a spot where param generation
//  can get at it, and returns the static PowerFXParam to use to refer to it.
SA_RET_NN_VALID PowerFXParam* moddef_SetPowerIconParam(SA_PARAM_NN_VALID AttribModDef *pmoddef);
