#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "PowersEnums.h" // For enums

// The structure and utility code for applying a PowerDef

// Forward declarations
typedef struct AttribMod			AttribMod;
typedef struct Character			Character;
typedef struct CharacterAttribs		CharacterAttribs;
typedef struct CharacterClass		CharacterClass;
typedef struct CombatEventTracker	CombatEventTracker;
typedef struct Entity				Entity;
typedef struct Item					Item;
typedef struct Power				Power;
typedef struct PowerActivation		PowerActivation;
typedef struct PowerAnimFX			PowerAnimFX;
typedef struct PowerDef				PowerDef;
typedef struct PowerApplyStrength	PowerApplyStrength;
typedef struct PowerSubtargetChoice	PowerSubtargetChoice;
typedef struct WorldVolume			WorldVolume;

// Copied from elsewhere
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere


// PATrigger is a substructure of PowerApplication which tracks information about
//  the event that triggered the application.  A few fields are exposed for XPath
//  for backwards compatibility - accessing the fields should generally be done
//  with CombatEval functions.
AUTO_STRUCT;
typedef struct PATrigger
{
	F32 fMag;							AST(NAME(Mag))
		// The magnitude of the event (damage dealt, healed, etc)

	F32 fMagScale;						AST(NAME(MagScale))
		// The magnitude of the event divided by the relevant value of the target (damage / target's max health, etc)

	F32 fMagPreResist;					AST(NAME(MagPreResist))
		// The magnitude of the event before various kinds of resistance

	CombatEventTracker *pCombatEventTracker;	AST(UNOWNED)
		// The tracker for the CombatEvent, if any

} PATrigger;

// Substructure of PowerApplication which tracks information about the critical state.
//  Marked as a persist
AUTO_STRUCT AST_CONTAINER;
typedef struct PACritical
{
	int bSuccess : 1;					AST(NAME(Success,Hit),PERSIST,NO_TRANSACT)
		// True if the Application is critical

	F32 fRoll;							AST(NAME(Roll),PERSIST,NO_TRANSACT)
		// The [0..1) roll

	F32 fThreshold;						AST(NAME(Threshold),PERSIST,NO_TRANSACT)
		// The roll cutoff point (if Roll was less than this value, the Application is critical)

	F32 fSeverity;						AST(NAME(Severity),PERSIST,NO_TRANSACT)
		// How severe the critical is (derived from the source's attributes)
} PACritical;

// Substructure of PowerApplication which tracks information about the avoidance state.
AUTO_STRUCT;
typedef struct PAAvoidance
{
	int bSuccess : 1;					AST(NAME(Success))
		// True if the Application is avoided

	F32 fRoll;							AST(NAME(Roll))
		// The [0..1) roll

	F32 fThreshold;						AST(NAME(Threshold))
		// The roll cutoff point (if Roll was less than this value, the Application is avoided)

	F32 fSeverity;						AST(NAME(Severity))
		// How severe the avoidance is (derived from the target's attributes)

} PAAvoidance;

// Structure to keep direct pointers and hue information for PowerAnimFX of
//  a PowerApplication.  Basically just a deref'ed version of PowerAnimFXRef.
typedef struct PAPowerAnimFX
{
	PowerAnimFX *pafx;
		// Direct pointer to PowerAnimFX

	F32 fHue;
		// Hue override for the PowerAnimFX
} PAPowerAnimFX;


// PowerApplication tracks all relevant information about the application
//  of a PowerDef, including the PowerDef, source and targets, and
//  other tasty bits.  AUTO_STRUCTed so that its fields are accessible
//  in expressions.
AUTO_STRUCT;
typedef struct PowerApplication
{
	
	U32 uiApplyID;						NO_AST
		// ID of the Application

	PowerDef *pdef;						AST(NAME(PowerDef))
		// The PowerDef being applied

	Power *ppow;						NO_AST
		// The Power instance being applied, may be NULL

	Power **pppowEnhancements;			NO_AST
		// EArray of Enhancement Power instances to include

	AttribMod *pmodEvent;				NO_AST
		// The attrib mod event that started this application (trigger, expiration etc)

	
	// Source

	Character *pcharSource;				NO_AST
		// The source Character, may be NULL, usually sets the following variables

	int iPartitionIdx;					NO_AST
		// The partition

	Vec3 vecSourcePos;					NO_AST
		// The source point

	Vec3 vecSourceDir;					NO_AST
		// The source direction

	CharacterClass *pclass;				NO_AST
		// The source class, may be NULL

	int iLevel;							AST(NAME(Level))
		// The source level

	S32 iLevelMod;						NO_AST
		// The level of the AttribMod currently under consideration by the Application.
		//  This is commonly the source level (iLevel), but can be the level of the Power
		//  or even the level of the target in the case of bLevelAdjusting. 

	Item **ppEquippedItems;				NO_AST
		// The equipped items which are relevant to the power application

	S64 iSrcItemID;						NO_AST
		// The source item ID that was originally used to create this Application
		// This is currently only used for activate FX
		// TODO(MK): Set this only when necessary as it eventually gets persisted

	PowerApplyStrength **ppStrengths;	NO_AST
		// If the Application is using pre-computed strengths, this is where they are kept

	int iIdxMulti;						AST(NAME(IdxMulti))
		// The multi-table index

	F32 fTableScale;					AST(NAME(TableScale))
		// Scalar applied to TableDefault lookups (for duration and magnitude)

	EntityRef erModSource;				NO_AST
		// EntityRef that is the source of all AttribMods created by this Application

	EntityRef erModOwner;				NO_AST
		// EntityRef that owns all AttribMods created by this Application

	Character *pcharSourceTargetType;	NO_AST
		// The Character used during target type checks, usually the same as pcharSource


	// Intended Target

	EntityRef erTarget;					NO_AST
		// Ref to target entity, may be 0 or deref to NULL

	WorldVolume*** pppvolTarget;		NO_AST
		// EArray handle of target volumes, in the case of a volume-targeting PowerDef

	Vec3 vecTarget;						NO_AST
		// The target point, used when there is no entity or object, or the entity/object 
		//  uses special combat positions

	Vec3 vecTargetSecondary;			NO_AST
		// The target point for the powers with a secondary target

	PowerSubtargetChoice *pSubtarget;	AST(NAME(Subtarget))
		// The subtarget, if there is one

	EntityRef erProximityAssistTarget;	NO_AST
		// optional, only for self-targeted AE type powers

	// Effective Target

	Entity *pentTargetEff;				NO_AST
		// The actual target entity for the effect area

	Vec3 vecTargetEff;					NO_AST
		// The actual target point for the effect area

	S32 iNumTargets;					AST(NAME(NumTargets))
		// Number of targeted Characters for this application overall

	S32 iNumTargetsHit;					NO_AST
		// Number of targeted Characters that have been hit.
		// Currently incremented as each target is Hit or Missed, so it's
		//  not set to the final value until all possible targets have been
		//  processed.

	S32 iNumTargetsMissed;				NO_AST
		// Number of targeted Characters that have been missed.  See note
		//  on iNumTargetsHit about how it's set.

	// Misc

	PowerActivation *pact;				AST(NAME(Activation))
		// Activation causing this Application, may be NULL

	PACritical critical;				AST(NAME(Critical))
		// Data about the critical state of the Application

	PAAvoidance avoidance;				AST(NAME(Avoidance))
		// Data about the avoided state of the Application

	PATrigger trigger;					AST(NAME(DamageTrigger))
		// Data about the trigger event may have triggered the Application

	F32 fRandom;						AST(NAME(Random))
		// Random [0..1) value assigned to the Application, can be used in expressions

	U32 uiRandomSeedActivation;			NO_AST
		// Random seed from Activation, passed to AttribMods, for any per-Activation randomness

	U32 uiSeedSBLORN;					NO_AST
		// Static BLORN seed for per-Application randomness.  Do NOT actually modify this value
		//  when generating random numbers.

	PowerAnimFX *pafx;					NO_AST
		// Primary PowerAnimFX of the Application

	F32 fHue;							NO_AST
		// Primary hue override for FX and AttribMods created by this Application

	PAPowerAnimFX **ppafxEnhancements;	NO_AST
		// Extended PowerAnimFX and hue overrides from Enhancements

	U32 uiTimestampAnim;				NO_AST
		// Base timestamp to use for animation and FX

	U32 uiTimestampClientView;			NO_AST
		// for AE related powers, the view time at the time the client activates the power

	U32 uiPeriod;						AST(NAME(Period))
		// Period of the Activation (0 is the base activation stage)

	U32 uiActID;						NO_AST
		// ID of the Activation

	U32 uiActSubID;						NO_AST
		// Sub-ID of the Activation

	S32 iFramesBeforeHitAdjust;			NO_AST
		// How many frames to adjust the frames per hit, usually 0

	F32 fAngleToTarget;					AST(NAME(AngleToTarget))
		// Degrees.  Horizontal angle from the source to the current target, relative to source's
		//  current orientation.  (-180 .. 180].

	F32 fAngleToSource;					AST(NAME(AngleToSource))
		// Degrees.  Horizontal angle from the current target to the source, relative to target's
		//  current orientation.  (-180 .. 180].

	F32 fAngleToTargetVertical;			AST(NAME(AngleToTargetVertical))
		//Degrees. Vertical... see fAngleToTarget

	F32 fAngleToSourceVertical;			AST(NAME(AngleToSourceVertical))
		//Degrees. Vertical... see fAngleToSource

	U32 bPrimaryTarget : 1;				AST(NAME(PrimaryTarget))
		// Set to true if the current target matches the actual target

	U32 bGlancing : 1;					AST(NAME(Glancing))
		// True if this is a glancing blow

	U32 bAnimActivate : 1;				NO_AST
		// True if we want to play the Activate bits/fx

	U32 bPredict : 1;					NO_AST
		// True if we want to try to predict AttribMods created by this Application

	U32 bSelfOnce : 1;					NO_AST
		// True if we want to run the SelfOnce AttribMod pass in this Application

	U32 bMissMods : 1;
		// True if we want to apply AttribMods on misses for this Application

	U32 bLevelAdjusting : 1;			NO_AST
		// True if all AttribMods from this Application should adjust their level
		//  to the level of the target Character

	U32 bOffsetEffectArea : 1;			NO_AST
		// true if the effect area has an offset	

	U32 bCountModsAsPostApplied : 1;
		// if set, then the mods will be counted as already applied for the resist/immunity/shield only affecting first tick 
		// Note: This will only be applied to mods on a character that is the target of the application.

	U32 bHitSelf : 1;					NO_AST
		// set if the application hit targets and one of them included the source


} PowerApplication;

// Returns the next Application ID
U32 powerapp_NextID(void);

// Returns the current Application ID
U32 powerapp_GetCurrentID();


// Returns the hue that should be used, given the Character, Power
//  PowerActivation, and/or PowerDef/PowerDef's AnimFX (in order of preference)
F32 powerapp_GetHue(SA_PARAM_OP_VALID Character *pchar,
					SA_PARAM_OP_VALID Power *ppow,
					SA_PARAM_OP_VALID PowerActivation *pact,
					SA_PARAM_OP_VALID PowerDef *pdef);

F32 powerapp_GetTotalTime(PowerApplication * papp);

