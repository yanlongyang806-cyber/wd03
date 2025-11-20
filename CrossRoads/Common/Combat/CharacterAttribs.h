#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "textparser.h" // For StaticDefineInt

#include "AttribMod.h" // For AttribModDefParams struct
#include "CharacterAttribsMinimal.h"
#include "CombatPool.h"		// For embedded CombatPool structure
#include "entEnums.h"
#include "ItemEnums.h"
#include "ItemEnums_h_ast.h"
#include "CostumeCommonEnums.h"
#include "PowersEnums.h"
#include "PvPGameCommon.h"

// Forward declarations
typedef struct	AttribAccrualSet		AttribAccrualSet;
typedef struct	AssignedStats			AssignedStats;
typedef struct	Character				Character;
typedef struct	NOCONST(Character)		NOCONST(Character);
typedef struct	CharacterAttribs		CharacterAttribs;
typedef struct	CharacterClass			CharacterClass;
typedef struct  CritterDef				CritterDef;
typedef struct	CritterFaction			CritterFaction;
typedef struct	CritterGroup			CritterGroup;
typedef struct	DisplayMessage			DisplayMessage;
typedef struct	Entity					Entity;
typedef struct	Expression				Expression;
typedef struct	ExprContext				ExprContext;
typedef struct	FSM						FSM;
typedef struct  GameAccountDataExtract	GameAccountDataExtract;
typedef struct	InventorySlotIDDef		InventorySlotIDDef;
typedef struct	ItemDef					ItemDef;
typedef struct	PowerDef				PowerDef;
typedef struct	PowerSubtargetCategory	PowerSubtargetCategory;
typedef struct	ProjectileEntityDef		ProjectileEntityDef;
typedef struct	PlayerCostume			PlayerCostume;
typedef struct	PCSkeletonDef			PCSkeletonDef;
typedef struct	RewardTable				RewardTable;
typedef struct	SavedAttribStats_AutoGen_NoConst SavedAttribStats_AutoGen_NoConst;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct	WorldInteractionNode	WorldInteractionNode;
typedef struct DynFxInfo DynFxInfo;

// Utility defines

// (not to be confused with IS_NORMAL_ATTRIB)
#define FIRST_NORMAL_ATTRIBUTE kAttribType_StatDamage

// Attribs that are damage types
#define ATTRIB_DAMAGE(attrib) ((attrib)<offsetof(CharacterAttribs,fHitPointsMax))

// Attribs that are data-defined
#define ATTRIB_DATADEFINED(attrib) ((attrib)>=offsetof(CharacterAttribs,fDataDefined01) && (attrib)<kAttribType_Null)

// Attribs that have a base value of 1
#define ATTRIB_SCALAR(attrib) ((attrib)==kAttribType_SpeedRecharge \
							    || (attrib)==kAttribType_SpeedCooldown \
								|| (!g_CombatConfig.bDiscountCostIsAbsolute && (attrib)==kAttribType_DiscountCost) \
								|| (attrib)==kAttribType_StealthSight \
								|| (attrib)==kAttribType_Gravity \
								|| (attrib)==kAttribType_GravityJumpingUp \
								|| (attrib)==kAttribType_GravityJumpingDown \
								|| (attrib)==kAttribType_AIThreatScale \
								|| (attrib)==kAttribType_SpeedCharge \
								|| (attrib)==kAttribType_SpeedPeriod)

// Attribs that take effect when they are positive
#define ATTRIB_BOOLEAN(attrib) ((attrib)==kAttribType_Hold \
								|| (attrib)==kAttribType_Root \
								|| (attrib)==kAttribType_Disable \
								|| (attrib)==kAttribType_Flight \
								|| (attrib)==kAttribType_OnlyAffectSelf \
								|| (attrib)==kAttribType_NoCollision \
								|| (attrib)==kAttribType_Swinging)

// Attribs that affect other Attribs, and should have an affects expression
#define ATTRIB_AFFECTOR(attrib) ((attrib)==kAttribType_AttribModDamage \
									|| (attrib)==kAttribType_AttribModExpire \
									|| (attrib)==kAttribType_AttribModHeal)

// Attribs that are known to affect Powers on a per-Power basis, are thus calculated
//  with character_PowerBasicAttrib(), and thus may have an affects expression.
// I'm also including the user defined attributes.  They are liable to be calculated with
// character_PowerBasicAttrib, as in DDEvalHitRoll.  [RMARR - 8/9/11]
#define POWER_AFFECTOR(attrib) ((attrib)==kAttribType_Avoidance \
									|| (attrib)==kAttribType_CritChance \
									|| (attrib)==kAttribType_CritSeverity \
									|| (attrib)==kAttribType_Disable \
									|| (attrib)==kAttribType_DiscountCost \
									|| (attrib)==kAttribType_Dodge \
									|| (attrib)==kAttribType_SpeedRecharge \
									|| (attrib)==kAttribType_SpeedCharge \
									|| (attrib)==kAttribType_SpeedPeriod \
									|| (attrib)==kAttribType_SubtargetAccuracy \
									|| ((attrib)>=kAttribType_FirstUserDefined && (attrib)<kAttribType_Null))

#define ATTRIB_IGNORES_MAGNITUDE(attrib) ((attrib)==kAttribType_Placate \
											|| (attrib)==kAttribType_Kill)

// Solves case where the default value is -1 (so it doesn't default to the first damage type),
//  but editor doesn't allow setting back to -1, so we also consider Null to be "default"
#define ATTRIB_NOT_DEFAULT(attrib) ((attrib)!=-1 && (attrib)!=kAttribType_Null)

#define IS_BASIC_ASPECT(aspect) ((aspect)>=kAttribAspect_BasicAbs && (aspect)<=kAttribAspect_BasicFactNeg)
#define IS_STRENGTH_ASPECT(aspect) ((aspect)>=kAttribAspect_StrBase && (aspect)<=kAttribAspect_StrAdd)
#define IS_RESIST_ASPECT(aspect) ((aspect)>=kAttribAspect_ResTrue && (aspect)<=kAttribAspect_ResFactBonus)
#define IS_IMMUNITY_ASPECT(aspect) ((aspect)==kAttribAspect_Immunity)
#define IS_RESIST_OR_IMMUNITY_ASPECT(aspect) ((aspect)>=kAttribAspect_ResTrue && (aspect)<=kAttribAspect_Immunity)

#define IS_DAMAGE_ATTRIBASPECT(attrib, aspect) (ATTRIB_DAMAGE(attrib) && ((aspect)==kAttribAspect_BasicAbs || (aspect)==kAttribAspect_BasicFactNeg))
#define IS_HEALING_ATTRIBASPECT(attrib, aspect) (((aspect)==kAttribAspect_BasicAbs || (aspect)==kAttribAspect_BasicFactPos) && (attrib)==kAttribType_HitPoints)
#define IS_POOL_ATTRIBASPECT(attrib, aspect) ((ATTRIB_DAMAGE(attrib) || (attrib)==kAttribType_HitPoints || (attrib)==kAttribType_Power || (attrib)==kAttribType_Air || attrib_isAttribPoolCur(attrib)) && IS_BASIC_ASPECT(aspect))

// used to determine if the aspect is a type that will be considered to interrupt an activation
#define IS_INTERRUPT_ASPECT(aspect)	((aspect)==kAttribType_Interrupt \
										|| (aspect)==kAttribType_KnockTo \
										|| (aspect)==kAttribType_KnockUp \
										|| (aspect)==kAttribType_KnockBack \
										|| (aspect)==kAttribType_Hold \
										|| (aspect)==kAttribType_Disable)

extern StaticDefineInt AttribTypeEnum[];


// Defines the names of damage
AUTO_STRUCT;
typedef struct DamageTypeNames
{
	char **ppchNames; AST(NAME(DamageName))
} DamageTypeNames;

// Globally accessible damage name count and data
extern int g_iDamageTypeCount;
extern DamageTypeNames g_DamageTypeNames;

// Defines the names (and eventually other data) for arbitrary attributes
AUTO_STRUCT;
typedef struct DataDefinedAttributes
{
	char **ppchAttributes; AST(NAME(Attribute))
} DataDefinedAttributes;

// Globally accessible data defined attribute count and data
extern int g_iDataDefinedAttributesCount;
extern DataDefinedAttributes g_DataDefinedAttributes;

// Globally accessible useful attribute count and size
extern int g_iCharacterAttribCount;
extern int g_iCharacterAttribSizeUsed;

AUTO_ENUM;
typedef enum AttribPoolTargetClamping
{
	kAttribPoolTargetClamp_None = 0,
		//No Target clamping

	kAttribPoolTargetClamp_Max,
		//The Current value cannot exceed that of the target value	

	kAttribPoolTargetClamp_Min,
		// The Current value cannot be below the target value

}AttribPoolTargetClamping;

// AttribPool defines a pool constructed from Attributes.  It's built on top of
//  the generic CombatPool system.
AUTO_STRUCT;
typedef struct AttribPool
{
	char* pchName;					AST(STRUCTPARAM KEY POOL_STRING)		

	CombatPoolDef combatPool;		AST(EMBEDDED_FLAT)
		// The base rules of the pool

	AttribType eAttribCur;			AST(SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the current value of the pool

	AttribType eAttribMin;			AST(SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the minimum value of the pool

	AttribType eAttribMax;			AST(SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the maximum value of the pool

	AttribType eAttribTarget;		AST(SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the target value of the pool

	AttribType eAttribRegenRate;	AST(ADDNAMES(AttribRegen) SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the regen rate scale of the pool

	AttribType eAttribRegenMag;		AST(SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the regen magnitude scale of the pool

	AttribType eAttribDecayRate;	AST(ADDNAMES(AttribDecay) SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the decay rate scale of the pool

	AttribType eAttribDecayMag;		AST(SUBTABLE(AttribTypeEnum))
		// The Attribute that defines the decay magnitude scale of the pool

	AttribType *peAttribDamage;		AST(NAME(AttribDamage), INT, SUBTABLE(AttribTypeEnum))
		// The Attributes that damage the current value of the pool

	AttribType *peAttribHeal;		AST(NAME(AttribHeal), INT, SUBTABLE(AttribTypeEnum))
		// The Attributes that heal the current value of the pool

	AttribPoolTargetClamping eTargetClamp; AST(SUBTABLE(AttribPoolTargetClampingEnum), INT, DEFAULT(kAttribPoolTargetClamp_None))
		// Target clamp rules. This will clamp the current value by the target, using the target as either a max or a min.

	U32 bPersist : 1;
		// Set to true if this AttribPool should be persisted.  This will cause the
		//  AttribCur attribute to be saved and loaded from the db.  Other attributes
		//  may need to be persisted as well to recover properly, depending on the
		//  combatPool's Bound setting.  When enabled, the combatPool's Init field
		//  is only used when no persisted value is found upon loading.

	U32 bTargetNotCalculated : 1;
		// Set to true if this AttribPools Target should not be calculated on a per
		// combat tick basis. This will make it work similar to that of the Current
		// value attrib. If this value is turned on, as well as the persist value, then
		// the Target value is also persisted

	U32 bAutoFill : 1;
		// Set to true if this AttribPools Target should be set to the max upon character creation. 
		// Applies to critters and saved pets as well. Should only be turned on if TargetNotCalculated is turned 
		// on as well.

	U32 bDoNotEmpty : 1;
		// Set to true if you do not want the attrib pool to empty.

	U32 bTickWhileDead : 1;
		// If true, evaluate this pool even when dead

	U32 bTickDisabledInCombat : 1;
		// If true, the pool doesn't tick (approach the target value) while in combat, and the timer is reset

	U32 bAbsorbsBasicDamage : 1;
		// If true, the pool absorbs basic damage (from the built-in set of 30), preventing the damage from
		//  reaching the built-in HitPoints pool.  This is after and in addition to any damage the pool suffers
		//  from its own specific damage Attributes.

	U32 bFillOnRespawn : 1;
		// Set to true if this AttribPool should fill to its target value on a players respawn. 

} AttribPool;

// EArray of AttribPools
AUTO_STRUCT;
typedef struct AttribPools
{
	AttribPool **ppPools;	AST(NAME(Pool))
} AttribPools;

// Globally available array, array size and flag if array has a Power pool
extern AttribPools g_AttribPools;
extern int g_iAttribPoolCount;
extern int g_bAttribPoolPower;


// Simple structure to save the value of an Attribute to the db
AUTO_STRUCT AST_CONTAINER;
typedef struct SavedAttribute
{
	CONST_STRING_MODIFIABLE pchAttrib;		AST(PERSIST SUBSCRIBE)
		// The Attribute that has been saved and should be loaded.  Saved as a char* so the db doesn't
		//  choke on it if the Attribute ever goes away.

	const F32 fValue;						AST(PERSIST SUBSCRIBE)
		// The value of the Attribute

} SavedAttribute;

// Simple structure to save the value of an Attribute to the db
// Non-persisted version of SavedAttribute so it can be used to pass though a transaction
AUTO_STRUCT;
typedef struct TempAttribute
{
	const char *pchAttrib;
		// The Attribute that has been saved and should be loaded.  Saved as a char* so the db doesn't
		//  choke on it if the Attribute ever goes away.

	F32 fValue;
		// The value of the Attribute
} TempAttribute;

AUTO_STRUCT;
typedef struct TempAttributes
{
	TempAttribute **ppAttributes;
}TempAttributes;

AUTO_STRUCT;
typedef struct AttribStatsPresetDef
{
	const char* pchName;				AST(STRUCTPARAM POOL_STRING KEY)

	DisplayMessage *pDisplayMessage;	AST(NAME(DisplayName))
	
	AssignedStats** ppAttribStats;		AST(NAME(AttribStats))
} AttribStatsPresetDef;

// Custom parameters for special Attribs

AUTO_ENUM;
typedef enum AIAvoidVolumeType
{
	AIAvoidVolumeType_AVOID,
	AIAvoidVolumeType_ENEMY_AVOID,
	AIAvoidVolumeType_ATTRACT
} AIAvoidVolumeType;

// Defines the radius of an AIAvoid AttribMod
AUTO_STRUCT;
typedef struct AIAvoidParams
{
	AttribModDefParams params;		AST(POLYCHILDTYPE(kAttribType_AIAvoid))
		// Must be first.  Parent parameter struct.

	F32 fRadius;					AST(NAME(Radius))
		// Radius of the AttribMod

	AIAvoidVolumeType eVolumeType;	AST(SUBTABLE(AIAvoidVolumeTypeEnum))
		// The type of volume this is, defaults to AVOID
} AIAvoidParams;

// Defines the expression the AI should run for an AICommand AttribMod
AUTO_STRUCT;
typedef struct AICommandParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AICommand))
		// Must be first. Parent parameter struct.

	Expression *pExpr;			AST(NAME(Expression) ADDNAMES(exprblock) LATEBIND)
		// Command expression 
} AICommandParams;

// Defines the radius of an AISoftAvoid AttribMod
AUTO_STRUCT;
typedef struct AISoftAvoidParams
{
	AttribModDefParams params;		AST(POLYCHILDTYPE(kAttribType_AISoftAvoid))
		// Must be first.  Parent parameter struct.

	F32 fRadius;					AST(NAME(Radius))
		// Radius of the AttribMod

} AISoftAvoidParams;

AUTO_STRUCT;
typedef struct PVPSpecialActionParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_PVPSpecialAction))

	PVPSpecialActions eAction;
}PVPSpecialActionParams;

// Defines the potential source and target entities for ApplyPower AttribMods
AUTO_ENUM;
typedef enum ApplyPowerEntity
{
	kApplyPowerEntity_ModOwner,				ENAMES(ModOwner Owner)
		// The ultimate parent of the apply power chain

	kApplyPowerEntity_ModSource,			ENAMES(ModSource Source)
		// The source of the ApplyPower AttribMod

	kApplyPowerEntity_ModSourceCreator,		ENAMES(ModSourceCreator)
		// The creator of the source of the ApplyPower AttribMod

	kApplyPowerEntity_ModSourceTargetDual,	ENAMES(ModSourceTargetDual)
		// The dual target of the source (or the source) of the ApplyPower AttribMod

	kApplyPowerEntity_ModTarget,			ENAMES(ModTarget Target)
		// The target of the ApplyPower AttribMod

	kApplyPowerEntity_Random,				ENAMES(Random)
		// A random valid entity (includes the source as a possible target)

	kApplyPowerEntity_RandomNotSource,		ENAMES(RandomNotSource)
		// A random valid entity other than the source of the AttribMod

	kApplyPowerEntity_ApplicationTarget,	ENAMES(ApplicationTarget)
		// The effective target of the Application that created the ApplyPower AttribMod

	kApplyPowerEntity_RandomNotApplicationTarget,	ENAMES(RandomNotApplicationTarget)
		// A random valid entity other than the effective target of the Application that created the ApplyPower AttribMod

	kApplyPowerEntity_ClosestNotSource,		ENAMES(ClosestNotSource)
		// The closest valid entity other than the source of the AttribMod

	kApplyPowerEntity_ClosestNotSourceOrTarget,	ENAMES(ClosestNotSourceOrTarget)
		// The closest valid entity other than the source or target of the AttribMod

	kApplyPowerEntity_ClosestNotTarget,		ENAMES(ClosestNotTarget)
		// The closest valid entity other than the target of the AttribMod

	kApplyPowerEntity_HeldObject,			ENAMES(HeldObject)
		// The object held by the source of the ApplyPower AttribMod

	kApplyPowerEntity_Count, EIGNORE
} ApplyPowerEntity;

// Defines the power, source and target of an ApplyPower AttribMod
AUTO_STRUCT;
typedef struct ApplyPowerParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_ApplyPower))
		// Must be first.  Parent parameter struct.

	REF_TO(PowerDef) hDef;		AST(NAME(Def), REFDICT(PowerDef))
		// Handle to power def to be applied

	ApplyPowerEntity eSource;
		// The 'source' of the applied power

	ApplyPowerEntity eTarget;
		// The 'target' of the applied power

	U32 bUseOwnerTargetType : 1;
		// Set to true to use the AttribMod's Owner for TargetType testing. This
		//  essentially enables the CombatConfig bModAppliedPowersUseOwnerTargetType
		//  flag just for this ApplyPower mod

	U32 bCanMiss : 1;
		// If set, then do hit eval for the power that is applied
} ApplyPowerParams;



// Defines the attrib mods to be damaged in an AttribModDamage AttribMod
AUTO_STRUCT;
typedef struct AttribModDamageParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AttribModDamage))
		// Must be first.  Parent parameter struct.

	AttribType offattribDamageType;		AST(NAME(DamageType),SUBTABLE(AttribTypeEnum))
		// The type of damage this mod will deal to fragile attribs

	U32 *puiAffectsAttribTypes;		AST(NAME(Affects), SUBTABLE(AttribTypeEnum))
		// The fragile attribute types this damage will affect
} AttribModDamageParams;



// Defines the order in which AttribMods are processed for an AttribModExpire AttribMod
AUTO_ENUM;
typedef enum AttribModExpireOrder
{
	kAttribModExpireOrder_Unset = 0,
		// Unset, AttribMods are processed in the order they are found on the Character

	kAttribModExpireOrder_DurationLeast,
		// Least duration expires first

	kAttribModExpireOrder_DurationMost,
		// Most duration expires first

	kAttribModExpireOrder_DurationUsedLeast,
		// Least used duration expires first

	kAttribModExpireOrder_DurationUsedMost,
		// Most used duration expires first

} AttribModExpireOrder;

// Defines various controls for an AttribModExpire AttribMod
AUTO_STRUCT;
typedef struct AttribModExpireParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AttribModExpire))
		// Must be first.  Parent parameter struct.


	AttribModExpireOrder eOrder;
		// Order in which AttribMods are processed for expiration

	U32 bGroupByApplication : 1;
		// Indicates that AttribMods from the same Application should
		//  be counted as a single unit and expired together.

} AttribModExpireParams;



// Defines the scales used by an AttribModFragilityScale AttribMod
AUTO_STRUCT;
typedef struct AttribModFragilityScaleParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AttribModFragilityScale))
		// Must be first.  Parent parameter struct.

	REF_TO(FragileScaleSet) hScaleIn;	AST(NAME(ScaleIn))
		// Additional scale applied to the AttribMod's magnitude for incoming damage

	REF_TO(FragileScaleSet) hScaleOut;	AST(NAME(ScaleOut))
		// Additional scale applied to the AttribMod's magnitude for outgoing damage

} AttribModFragilityScaleParams;



// Defines the various aspects of an AttribMod that can be healed via
//  an AttribModHeal AttribMod
AUTO_ENUM;
typedef enum AttribModHealAspect
{
	kAttribModHealAspect_Magnitude,
	// Heal the magnitude

	kAttribModHealAspect_Duration,
	// Heal the duration

	kAttribModHealAspect_Health,
	// Heal the health

} AttribModHealAspect;

// Defines the aspect of an AttribModHeal AttribMod
AUTO_STRUCT;
typedef struct AttribModHealParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AttribModHeal))
		// Must be first.  Parent parameter struct.

	AttribModHealAspect eAspect;
		// Aspect to heal

	bool bCountsAsHeal;
		// creates a healing event and will add credit for healing
} AttribModHealParams;

// Defines the aspect of an AttribModKnockback AttribMod
AUTO_STRUCT;
typedef struct AttribModKnockbackParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_KnockBack))
		// Must be first.  Parent parameter struct.

	bool bInstantFacePlant;
		//Whether or not the target should instantly go into a faceplant animation (skip the rising & falling parts)

	bool bOmitProne;
		//Whether or not the target should be proned at the end of the movement.

	F32 fTimer;
		//time for the knockback

	bool bIgnoreTravelTime;
		//when true the timer only counts prone time
		//when false the timer starts when the keyword is played

} AttribModKnockbackParams;

// Defines the aspect of an AttribModKnockup AttribMod
AUTO_STRUCT;
typedef struct AttribModKnockupParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_KnockUp))
		// Must be first.  Parent parameter struct.

	bool bInstantFacePlant;
		//Whether or not the target should instantly go into a faceplant animation (skip the rising & falling parts)

	bool bOmitProne;
		//Whether or not the target should be proned at the end of the movement.

	F32 fTimer;
		//time for the knockback

	bool bIgnoreTravelTime;
		//when true the timer only counts prone time
		//when false the timer starts when the keyword is played

} AttribModKnockupParams;

// Defines the various aspects of an AttribMod that can be shared via
//  an AttribModShare AttribMod
AUTO_ENUM;
typedef enum AttribModShareAspect
{
	kAttribModShareAspect_Magnitude,
		// Share the magnitude

	kAttribModShareAspect_Duration,
		// Share the duration

	kAttribModShareAspect_Health,
		// Share the health

} AttribModShareAspect;

// Defines the aspect and share expression of an AttribModShare AttribMod
AUTO_STRUCT;
typedef struct AttribModShareParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AttribModShare))
		// Must be first.  Parent parameter struct.

	AttribModShareAspect eAspect;
		// Aspect to share

	Expression *pExprShare;		AST(NAME(ExprBlockShare), LATEBIND)
		// Feet. Distance to the front of the target to create the critter.

	Expression *pExprContribution;		AST(NAME(ExprBlockContribution), LATEBIND)
		// Feet. Distance to the front of the target to create the critter.

} AttribModShareParams;



AUTO_STRUCT;
typedef struct AttribOverrideParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_AttribOverride))
		// Must be first. Parent parameter struct.

	AttribType offAttrib;		AST(NAME(Attrib), SUBTABLE(AttribTypeEnum), DEFAULT(-1))
		// The attrib to be set
} AttribOverrideParams;



AUTO_STRUCT;
typedef struct BecomeCritterParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_BecomeCritter))
		// Must be first.  Parent parameter struct.

	REF_TO(CritterDef) hCritter;	AST(NAME(Critter), RESOURCEDICT(CritterDef))
		// Name of the critter to become

	REF_TO(CharacterClass) hClass;	AST(NAME(Class), RESOURCEDICT(CharacterClass))
		// Optional Class to switch to while this is active

} BecomeCritterParams;



// Defines the target entity
AUTO_ENUM;
typedef enum DamageTriggerEntity
{
	kDamageTriggerEntity_Self,
		// The entity that the trigger is on

	kDamageTriggerEntity_SelfOwner,
		// The owner of the entity that the trigger is on

	kDamageTriggerEntity_DamageOwner,
		// The ultimate parent of the damage mod that triggered the response

	kDamageTriggerEntity_DamageSource,
		// The source of the damage mod that triggered the response

	kDamageTriggerEntity_DamageTarget,
		// The target of the damage mod that triggered the response.  Identical to Self for incoming triggers.

	kDamageTriggerEntity_TriggerOwner,
		// The ultimate parent of the trigger mod itself

	kDamageTriggerEntity_TriggerSource,
		// The source of the trigger mod itself

	kDamageTriggerEntity_TriggerPet,	EIGNORE
		// TODO(JW): Reimplement

} DamageTriggerEntity;

// Defines the damage type, and response power of a DamageTrigger AttribMod
AUTO_STRUCT;
typedef struct DamageTriggerParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_DamageTrigger))
		// Must be first.  Parent parameter struct.

	AttribType offAttrib;		AST(NAME(Attrib), SUBTABLE(AttribTypeEnum), DEFAULT(-1))
		// Type of damage that triggers the response power

	REF_TO(PowerDef) hDef;		AST(NAME(Def), REFDICT(PowerDef))
		// Handle to power def to be applied

	DamageTriggerEntity eTarget;
		// The target of the response power

	Expression *pExprChance;		AST(NAME(ExprChanceBlock), REDUNDANT_STRUCT(Chance, parse_Expression_StructParam), LATEBIND)
		// Chance for the trigger to respond to any single event

	U32 bHeal : 1;
		// True if this actually triggers from heals, instead of damage

	U32 bOutgoing : 1;
		// True if this actually triggers from outgoing effects, instead of incoming

	U32 bMagnitudeIsCharges : 1;
		// If this trigger limits the number of times it can respond before expiring using its magnitude

	U32 bOwnedByDamager : 1;
		// True if the response power should be owned by the damage owner - does not change
		//  the fact that it is sourced from the Character taking damage.  If we decide to allow
		//  changing the source, this should probably be removed.

} DamageTriggerParams;


// Provides a way for the powers system to disable specific tactical movement types
AUTO_STRUCT;
typedef struct DisableTacticalMovementParams
{
	AttribModDefParams params;		AST(POLYCHILDTYPE(kAttribType_DisableTacticalMovement))
		// Must be first.  Parent parameter struct.

	TacticalDisableFlags eFlags;	AST(NAME(Flags) FLAGS SUBTABLE(TacticalDisableFlagsEnum))
		// Which tactical movement types to disable

} DisableTacticalMovementParams;

// Defines costume and attach bone for fake entity to be created
AUTO_STRUCT;
typedef struct EntAttachParams
{
	AttribModDefParams params;		AST(POLYCHILDTYPE(kAttribType_EntAttach))
		// Must be first.  Parent parameter struct.

	char *pchCostumeName;		AST(NAME(CostumeName) RESOURCEDICT(PlayerCostumetity))
		// String name of costume

	char *pchAttachBone;		AST(NAME(AttachBone))
		// Bone to attach to

	char *pchExtraBit;			AST(NAME(ExtraBit))
		// Anim bit to add
} EntAttachParams;



// Defines the teams available to a pet
AUTO_ENUM;
typedef enum EntCreateTeam
{
	kEntCreateTeam_Owner,
		// The ultimate parent of the pet

	kEntCreateTeam_Source,
		// The source of the AttribMod that created the pet

	kEntCreateTeam_Target,
		// The target of the AttribMod that created the pet

	kEntCreateTeam_None,
		// The pet is not on anyone's team
} EntCreateTeam;

// Defines the manner in which the pet reflects the strength of the source
AUTO_ENUM;
typedef enum EntCreateStrength
{
	kEntCreateStrength_Locked,
		// All the pet's powers are locked to a specific strength, like an ApplyPower

	kEntCreateStrength_Independent,
		// The pet's strength is completely independent of its owner

} EntCreateStrength;

// Defines the manner in which the pet reflects the strength of the source
AUTO_ENUM;
typedef enum EntCreateType
{
	kEntCreateType_Critter,
		// Spawns the defined critter
		
	kEntCreateType_CritterOfGroup,
		// Uses the critter group to spawn a specific critty

	kEntCreateType_Nemesis,
		// Uses the critter group to spawn a critter from your nemesis

} EntCreateType;

// Defines the facing type of the created entity
AUTO_ENUM;
typedef enum EntCreateFaceType
{
	kEntCreateFaceType_RelativeToCreator,
		// Face relative to the facing direction of the creator

	kEntCreateFaceType_Absolute,
		// Absolute facing, use world coordinates

	kEntCreateFaceType_RelativeToTarget,
		// Face relative to the facing direction of the target

	kEntCreateFaceType_FaceCreator,
		// Face the creator

	kEntCreateFaceType_FaceTarget,
		// Face the target

} EntCreateFaceType;

// Defines the critter, team and other required info for an EntCreate AttribMod
AUTO_STRUCT;
typedef struct EntCreateParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_EntCreate))
		// Must be first.  Parent parameter struct.

	EntCreateType eCreateType;				AST(DEFAULT(kEntCreateType_Critter))
		// Determines how the ent create finds/validates the entity created

	REF_TO(CritterDef) hCritter;		AST(NAME(Critter), RESOURCEDICT(CritterDef))
		// Name of the critter to be created

	REF_TO(CritterGroup) hCritterGroup;	AST(NAME(CritterGroup) RESOURCEDICT(CritterGroup))
		// The critter group to spawn a random critter from

	EntCreateTeam eTeam;				AST(DEFAULT(kEntCreateTeam_Owner))
		// Team to place the pet on

	EntCreateStrength eStrength;		AST(DEFAULT(kEntCreateStrength_Locked))
		// Strength of the pet's powers

	const char *pcRank;					AST(POOL_STRING)
		// If the create strength of the critter is independent, it uses this to set the critter's rank
		//  NOTE:  This field does nothing when EntCreateType is Critter!  Only useful in critter group or nemesis minion spawns.

	const char *pcSubRank;				AST(POOL_STRING)
		// If the create strength of the critter is independent, it uses this to set the critter's subrank

	Expression *pExprDistanceFront;		AST(NAME(ExprBlockDistanceFront), LATEBIND)
		// Feet. Distance to the front of the target to create the critter.

	Expression *pExprDistanceRight;		AST(NAME(ExprBlockDistanceRight), LATEBIND)
		// Feet. Distance to the right of the target to create the critter.

	Expression *pExprDistanceAbove;		AST(NAME(ExprBlockDistanceAbove), LATEBIND)
		// Feet. Distance above the target to create the critter.

	F32 fFacing;						AST(NAME(Facing))
		// Degrees, positive means to spin the character to the right, negative to the left

	char* pchFSM;						AST(NAME(FSM) RESOURCEDICT(FSM))
		// Referenced FSM to be applied to the character

	S32 iCostumeDefault;
		// If not 0, defines a 1-based index into the costume list of the Critter to default
		//  to when created (instead of random).  May be overridden if bCanCustomizeCostume
		//  is true and the Player changes the relevant Power's iEntCreateCostume.

	EntCreateFaceType eFaceType;		AST(NAME(FaceType, FaceAbsolute))
		// Determines the facing direction of the created entity

	U32 bModsOwnedByOwner : 1;
		// If any AttribMods created by the Critter's Powers are owned by the Critter's erOwner
		//  instead of the Pet.

	U32 bTryTeleportFirst : 1;			AST(NAME(Teleport))
		// Will try to 'teleport' the critter before doing the capsule cast

	U32 bOffsetUsesPitchAndRoll : 1; AST(NAME(OffsetUsesPitchAndRoll, AdjustOffsetByPitch))
		// If set, the offset vector by pitch and roll angles. By default it just uses the yaw angle.

	U32 bUseFacingPitch : 1;		AST(NAME(UseFacingPitch, UseCreatorPitch))
		// If true, the initial pitch of the created Critter will use the facing type's pitch

	U32 bUseFacingRoll : 1;			AST(NAME(UseFacingRoll))
		// If true, the initial roll of the created Critter will be use that of facing type's roll

	U32 bUseMainTarget : 1;				AST(NAME(UseMainTarget))
		// Used for AI, to figure out who it should use as its default target

	U32 bSurviveCharacterDeathExpiration : 1;	AST(NAME(SurviveCharacterDeathExpiration))
		// If true, when the AttribMod is being cleaned up, if it expired due to CharacterDeath
		//  the created Critter is not killed.

	U32 bDieOnExpire : 1;				AST(NAME(DieOnExpire))
		// The critter will die when the duration expires instead of getting cleaned up immediately

	U32 bCanCustomizeCostume : 1;
		// The Player can choose to control which costume the EntCreate uses (doesn't actually mean they
		//  can make a custom costume).

	U32 bUseTargetPositionWhenNoTarget : 1;		AST(NAME(UseTargetPositionWhenNoTarget))
		// Use the effective target location of the application if there is no target entity

	U32 bUseCreatorsDisplayName : 1;

	U32 bUseCreatorsPuppetDisplayName : 1;

	U32 bPersistent : 1;				AST(NAME(Persistent))
		// This causes the EntCreate attrib to persist through zoning

	U32 bCreateAtTargetedEntityPos : 1;
		// This causes the entity to be created not at the mod's target, but at the erParam position

	U32 bUseTargetPositionAsAIVarsTargetPos : 1;
		// this makes the vecTarget from the client get used as a position set on the AIVars. 
		// to access this position, use GetPowerEntCreateTargetPoint
	
	U32 bClampToGround : 1;
		// this casts the position down to the ground and uses that position instead.


	const char *pcBoneGroup;			AST(NAME(BoneGroup))
		// Make the costume from the owner player from the BoneGroup on the owner player's current skeleton

	REF_TO(PCSkeletonDef) hSkeleton;	AST(NAME(Skeleton), REFDICT(CostumeSkeleton))
		// If this is set change the costume skeleton to it (Only if the costume was made from a bone group)
} EntCreateParams;


// Defines the critter for an EntCreateVanity AttribMod
AUTO_STRUCT;
typedef struct EntCreateVanityParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_EntCreateVanity))
		// Must be first.  Parent parameter struct.

	REF_TO(CritterDef) hCritter;		AST(NAME(Critter), RESOURCEDICT(CritterDef))
		// Name of the critter to be created

	Expression *pExprDistanceFront;		AST(NAME(ExprBlockDistanceFront), LATEBIND)
		// Feet. Distance to the front of the target to create the critter.

	Expression *pExprDistanceRight;		AST(NAME(ExprBlockDistanceRight), LATEBIND)
		// Feet. Distance to the right of the target to create the critter.

	Expression *pExprDistanceAbove;		AST(NAME(ExprBlockDistanceAbove), LATEBIND)
		// Feet. Distance above the target to create the critter.

} EntCreateVanityParams;



// Defines the Faction for a Faction AttribMod
AUTO_STRUCT;
typedef struct FactionParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_Faction))
		// Must be first.  Parent parameter struct.

	REF_TO(CritterFaction) hFaction;	AST(NAME(Faction) REFDICT(CritterFaction))
		// Faction to be set
} FactionParams;



// Defines the flags that can be enabled with a Flag AttribMod
AUTO_ENUM;
typedef enum FlagAttributeFlags
{
	kFlagAttributeFlags_Untargetable = 1 << 0,	ENAMES(Untargetable)
		// Set the ENTITYFLAG_UNTARGETABLE flag
	kFlagAttributeFlags_Unkillable = 1 << 1,	ENAMES(Unkillable)
		// Set the character's unkillable flag
	kFlagAttributeFlags_Unselectable = 1 << 2,	ENAMES(Unselectable)
		// Set the ENTITYFLAG_UNSELECTABLE flag

} FlagAttributeFlags;

// Defines the set of flags that are enabled with a Flag AttribMod
AUTO_STRUCT;
typedef struct FlagParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_Flag))
		// Must be first.  Parent parameter struct.

	FlagAttributeFlags eFlags;			AST(NAME(Flags), FLAGS, SUBTABLE(FlagAttributeFlagsEnum))
		// Flags to be set
} FlagParams;

AUTO_STRUCT;
typedef struct FlightParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_Flight))
		// Must be first.  Parent parameter struct.

	U32 bDisableFakeRoll : 1;			AST(NAME(DisableFakeRoll))
		// Disable fake roll

	U32 bIgnorePitch : 1;				AST(NAME(IgnorePitch))
		// Ignore pitch for rotations

	U32 bUseJumpBit : 1;				AST(NAME(UseJumpBit))
		// Use a jump bit

	U32 bConstantForward : 1;			AST(NAME(ConstantFoward))
		// The player should always be moving in the forward direction.
} FlightParams;

// Defines the Power to be granted by a GrantPower AttribMod
AUTO_STRUCT;
typedef struct GrantPowerParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_GrantPower))
		// Must be first.  Parent parameter struct.

	REF_TO(PowerDef) hDef;		AST(REFDICT(PowerDef))
		// Handle to power def
} GrantPowerParams;

// Defines the grant reward params
AUTO_STRUCT AST_IGNORE(Level);
typedef struct GrantRewardParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_GrantReward))
		// Must be first. Parent parameter struct.

	REF_TO(RewardTable) hRewardTable;	AST(NAME(RewardTable))
		// Reference to the reward table
} GrantRewardParams;

// Defines the Power to be included by an IncludeEnhancement AttribMod
AUTO_STRUCT;
typedef struct IncludeEnhancementParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_IncludeEnhancement))
		// Must be first.  Parent parameter struct.

	REF_TO(PowerDef) hDef;		AST(REFDICT(PowerDef))
		// Handle to PowerDef
} IncludeEnhancementParams;

// Defines how an Interrupt AttribMod affects the recharge of an interrupted Power
AUTO_STRUCT;
typedef struct InterruptParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_Interrupt))
		// Must be first. Parent parameter struct.

	U32 bRechargePercent : 1;
		// If true, the magnitude is treated as a percentage of the Power's default instead of an absolute number of seconds

} InterruptParams;

AUTO_ENUM;
typedef enum KillType
{
	kKillType_Silent,				ENAMES(Silent)
		// Silently kills (despawns) an entity.  Cannot be used on a player.

	kKillType_Irresponsible,		ENAMES(Irresponsible)
		// Kills an entity but does not hold the source responsible for the kill.

	kKillType_Responsible,			ENAMES(Responsible)
		// Kills an entity and holds the source responsible for the remaining life of the target
} KillType;

AUTO_STRUCT;
typedef struct KillParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_Kill))
		// Must be first. Parent parameter struct.

	KillType	eKillType;		AST(DEFAULT(kKillType_Silent))
		// Tells the combat system how the entity is to be killed.
} KillParams;

// Defines the target entity for KillTriggers
AUTO_ENUM;
typedef enum KillTriggerEntity
{
	kKillTriggerEntity_Self,
		// The entity that the trigger is on, that got the kill

	kKillTriggerEntity_Victim,
		// The entity that was killed

} KillTriggerEntity;

// Defines the response power of a KillTrigger AttribMod
AUTO_STRUCT;
typedef struct KillTriggerParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_KillTrigger))
		// Must be first.  Parent parameter struct.

	REF_TO(PowerDef) hDef;		AST(NAME(Def), REFDICT(PowerDef))
		// Handle to power def to be applied

	KillTriggerEntity eTarget;
		// The target of the response power

	F32 fChance;				AST(DEFAULT(1))
		// Chance for the trigger to respond to any single event

	U32 bMagnitudeIsCharges : 1;
		// If this trigger limits the number of times it can respond before expiring using its magnitude

} KillTriggerParams;



// Defines the parameters for a KnockTo AttribMod
AUTO_STRUCT;
typedef struct KnockToParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_KnockTo))
		// Must be first.  Parent parameter struct.

	F32 fDistanceFront;
		// Feet. Distance to the front of the source to knock to.

	F32 fDistanceRight;
		// Feet. Distance to the right of the source to knock to.

	F32 fDistanceAbove;
		// Feet. Distance above the source to knock to.

	bool bInstantFacePlant;
	//Whether or not the target should instantly go into a faceplant animation (skip the rising & falling parts)

	bool bOmitProne;
	//Whether or not the target should be proned at the end of the movement.

	F32 fTimer;
	//Time for the knockback

	bool bIgnoreTravelTime;
	//When true the timer specifies prone time
	//When false the timer specifies flight + prone time

} KnockToParams;



// Defines the event string for a MissionEvent AttribMod
AUTO_STRUCT;
typedef struct MissionEventParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_MissionEvent))
		// Must be first.  Parent parameter struct.

	char *pchEventName;			AST(NAME(EventName))
		// Name of the event
} MissionEventParams;



// Defines the costume used in a SetCostume AttribMod
AUTO_STRUCT;
typedef struct ModifyCostumeParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_ModifyCostume))
		// Must be first.  Parent parameter struct.

	kCostumeValueArea eArea;            AST(NAME(Area))
	kCostumeValueMode eMode;            AST(NAME(Mode))
	F32 fValue;							AST(NAME(Value))
	F32 fMinValue;						AST(NAME(MinValue))
	F32 fMaxValue;						AST(NAME(MaxValue))
	int iPriority;                      AST(NAME(Priority))
} ModifyCostumeParams;


AUTO_STRUCT;
typedef struct NotifyParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_Notify))
		// Must be first. Parent parameter struct.

	const char* pchMessageKey;	AST(POOL_STRING)
	// If true, the attrib mod affects the global cool down for all powers instead of recharge times for individual powers

} NotifyParams;


// Defines the fields for placteing options
AUTO_STRUCT;
typedef struct PlacateParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_Placate))
		// Must be first. Parent parameter struct.

	bool bStealthPlacater;		AST(NAME(SteatlhPlacater) DEFAULT(true))
		// This option should be true if the caster becomes invisible to the target
} PlacateParams;



AUTO_STRUCT;
typedef struct PowerModeParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_PowerMode))
		// Must be first. Parent parameter struct.

	int iPowerMode;				AST(NAME(PowerMode),SUBTABLE(PowerModeEnum))
		// The power mode this will be turning on

	U32 bMissionEvent : 1;
		// If true, causes the AttribMod to generate a MissionEvent named for the mode when the
		//  mode is first turned on

	U32 bCombatEvent : 1;
		// If true, causes the AttribMod to generate a single PowerMode CombatEvent each tick

} PowerModeParams;


// Defines how and when the recharge is applied
AUTO_ENUM;
typedef enum PowerRechargeApply
{
	kPowerRechargeApply_SetAlways,
		// The target Power has its recharge time set, regardless of the existing recharge time

	kPowerRechargeApply_SetIfLarger,
		// The target Power has its recharge time set if the new value is larger than the existing recharge time

	kPowerRechargeApply_SetIfSmaller,
		// The target Power has its recharge time set if the new value is smaller than the existing recharge time

	kPowerRechargeApply_Add,
		// The target Power has its recharge time increased by the new value (or decreased if the new value is negative)
} PowerRechargeApply;

// Defines the rules for interpreting and applying a PowerRecharge AttribMod
AUTO_STRUCT;
typedef struct PowerRechargeParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_PowerRecharge))
		// Must be first. Parent parameter struct.

	PowerRechargeApply eApply;
		// The method used to apply the recharge

	U32 bPercent : 1;
		// If true, the magnitude is treated as a percentage of the Power's default instead of an absolute number of seconds

	U32 bAffectsGlobalCooldown : 1;
		// If true, the attrib mod affects the global cool down for all powers instead of recharge times for individual powers

} PowerRechargeParams;



AUTO_STRUCT;
typedef struct PowerShieldParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_PowerShield))
		// Must be first. Parent parameter struct.

	F32 fRatio;
		// The ratio of damage dealt % to power damage dealt %
} PowerShieldParams;

AUTO_STRUCT;
typedef struct PVPFlagParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_PVPFlag))
		// Must be first. Parent parameter struct.

	const char* pchGroupName;			AST(POOL_STRING)
		// Specifies that this is part of a predetermined group

	const char* pchSubGroupName;		AST(POOL_STRING)
		// Specifies that this is part of a predetermined subgroup - NULL implies new subgroup for each participant

	F32 fRadius;
		// Determines distance from source where PVP Flag mod is auto-removed, unless bGlobal is true
	
	U32 bAllowHeal			: 1;
		// Specifies whether external entities can heal internal entities, mostly useful for special duels or Infection
	U32 bTeamHeal			: 1;
		// Not yet implemented
	U32 bAllowExternCombat	: 1;
		// Specifies whether internal entities can attack external entities as normal, mostly for Infection
	U32 bGlobal				: 1;
		// Tells the system not to expire the PVPFlag when the source dies (for Entity sources) or the 
			// radius is exceeded (for other sources)
} PVPFlagParams;


AUTO_STRUCT;
typedef struct ProjectileCreateParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_ProjectileCreate))

	REF_TO(ProjectileEntityDef)	hProjectileDef;		AST(NAME(ProjectileDef), RESOURCEDICT(ProjectileEntityDef))

	// relative creation location 

	// forward/backward offset in the facing direction in feet
	F32		fCreateDistanceForward;

	// right/left offset in the facing direction in feet
	F32		fCreateDistanceRight;

	// up/down offset 
	F32		fCreateDistanceUp;
	
	// the movement direction yaw offset
	F32		fDirectionYawOffset;

	// the pitch offset 
	F32		fDirectionPitchOffset;

	// if set, will attempt to snap the projectile and start on the ground on create
	U32		bSnapToGroundOnCreate : 1;

	// if set, uses the client's trajectory aiming position
	U32		bUseAimingTrajectory : 1;
		
	
} ProjectileCreateParams;

// Defines the Power to be removed by a RemovePower AttribMod
AUTO_STRUCT;
typedef struct RemovePowerParams
{
	AttribModDefParams params; AST(POLYCHILDTYPE(kAttribType_RemovePower))
		// Must be first.  Parent parameter struct.

	REF_TO(PowerDef) hDef;	AST(REFDICT(PowerDef))
		// Handle to power def
} RemovePowerParams;

// Defines how the rewards system is to be modified for this entity
AUTO_STRUCT;
typedef struct RewardModifierParams
{
	AttribModDefParams params; AST(POLYCHILDTYPE(kAttribType_RewardModifier))
		// Must be first.  Parent parameter struct.

	REF_TO(ItemDef) hNumeric;
		//The numeric this mod affects
} RewardModifierParams;

// Defines the costume used in a SetCostume AttribMod
AUTO_STRUCT;
typedef struct SetCostumeParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_SetCostume))
		// Must be first.  Parent parameter struct.

	kCostumeDisplayMode eMode;          AST(NAME(Mode))
	int iPriority;                      AST(NAME(Priority))
	REF_TO(PlayerCostume) hCostume;		AST(NAME(CostumeName), REFDICT(PlayerCostume))
		// Costume for the target to change into

	const char *pcBoneGroup;			AST(NAME(BoneGroup))
		// Make the costume from the BoneGroup on the player's current skeleton
	REF_TO(PCSkeletonDef) hSkeleton;	AST(NAME(Skeleton), REFDICT(CostumeSkeleton))
	// If this is set change the costume skeleton to it

	F32 fTurnRateScale;					
		// scaling the turn rate of the entity

	bool bCopyCostumeFromSourceEnt;
		//If set, the ent will copy the costume from the attribmod's erSource.

	bool bMount;						AST(NAME(AsMount))
		//If set, this costume is assumed to be a mount

	F32 fMountScaleOverride;			AST(NAME(MountScaleOverride))
		//If set, the mount will auto-rescale itself based on the override instead of blending with the rider's size

} SetCostumeParams;



// Defines the damage type a shield AttribMod protects against
AUTO_STRUCT;
typedef struct ShieldParams
{
	AttribModDefParams params;					AST(POLYCHILDTYPE(kAttribType_Shield))
		// Must be first.  Parent parameter struct.

	AttribType offAttrib;						AST(NAME(Attrib), SUBTABLE(AttribTypeEnum), DEFAULT(-1))
		// Attribute the shield absorbs

	Expression *pExprMaxDamageAbsorbed;			AST(NAME(ExprBlockMaxDamageAbsorbed), LATEBIND)
		// Maximum damage absorbed per hit expression

	F32 fPercentIgnored;						AST(NAME(PercentIgnored) ADDNAMES(PercentDamageIgnored))
		// Percentage of incoming damage that the shield will not try to absorb

	U32 uiCharges;								AST(NAME(Charges))
		// Maximum number of times the Shield will absorb damage, 0 is unlimited

	S32 iPriority;
		// Optional priority, Shields with higher priority always absorb first

	const char *pchHitFX;						AST(NAME(HitFX) POOL_STRING)
		// FX to flash when the shield absorbs damage

	U32 bScaleMaxAbsorbedByProportion : 1;
		// Scales the max damaged absorbed by health percent remaining * proportion

	U32 bDamageCredit : 1;
		// Damage done to the Shield is included in damage tracking.
		// Healing done to the Shield DOES NOT perform damage decay.  This is because Shields
		//  do not have a natural regen system yet.

	U32 bDamageTriggersTrackers : 1;
		// if set, will count the damage shielded as have taken the damage, 
		// so triggers and damage floats will get the pre-shielded magnitude for damage.
		// note: if there are multiple shields in effect, the last shield to have acted on damage
		// will have its bCountAsTakingDamage used.
		

} ShieldParams;

// Defines the data used by a SpeedCooldown AttribMod
AUTO_STRUCT;
typedef struct SpeedCooldownParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_SpeedCooldownCategory))
		// Must be first.  Parent parameter struct.

	S32 ePowerCategory; AST(NAME(PowerCategory) SUBTABLE(PowerCategoriesEnum))
		// The power category to affect
} SpeedCooldownParams;

// Defines the Subtarget data used in a SubtargetSet AttribMod
AUTO_STRUCT;
typedef struct SubtargetSetParams
{
	AttribModDefParams params;					AST(POLYCHILDTYPE(kAttribType_SubtargetSet))
		// Must be first.  Parent parameter struct.

	REF_TO(PowerSubtargetCategory) hCategory;	AST(NAME(Category) REFDICT(PowerSubtargetCategory))
		// Category to be subtargeted

} SubtargetSetParams;

AUTO_ENUM;
typedef enum AttibModTeleportTarget
{
	// Uses the casting entity as the base position for teleport
	kAttibModTeleportTarget_Self,
	
	// uses the target entity as the base position for the teleport
	kAttibModTeleportTarget_Target,

	// uses an owned projectile as the base position
	kAttibModTeleportTarget_OwnedProjectile,

	// uses the given expression to find a target. Only valid for non bClientViewTeleport teleports
	kAttibModTeleportTarget_Expression,
	
} AttibModTeleportTarget;

AUTO_ENUM;
typedef enum TeleportFaceType
{
	kTeleportFaceType_Current,
		// current facing as the base

	kTeleportFaceType_FaceTarget,
		// will face the eTeleportTarget target

	kTeleportFaceType_MatchTargetOrientation,
		// matches the eTeleportTarget target's orientation

	kTeleportFaceType_Absolute,
		// uses the FacingYawOffset

} TeleportFaceType;

AUTO_ENUM;
typedef enum TeleportOffsetOrientation
{
	kTeleportOffsetOrientation_TeleportTargetFacing,
		// the offset will be oriented off the teleport target's facing

	kTeleportOffsetOrientation_CurrentFacing,
		// the offset will be based on your current facing

	kTeleportOffsetOrientation_RelativeFromTeleportTarget,
		// the offset will be based on the direction from the teleport target

	kTeleportOffsetOrientation_TeleportTargetMovementRotation,
		// the offset will be based current rotation
		
} TeleportOffsetOrientation;

AUTO_STRUCT;
typedef struct TeleportParams
{
	AttribModDefParams params;			AST(POLYCHILDTYPE(kAttribType_Teleport))

	// what entity we should be using as the base position for teleporting
	AttibModTeleportTarget	eTeleportTarget;

	Expression *pTeleportTargetExpr;	AST(NAME(ExprBlockTeleportTarget) ADDNAMES(exprblock) LATEBIND)

	// how the teleporting entity's facing is calculated
	TeleportFaceType eFacingType;

	// if no tags are specified, or any tag that matches- teleports to the first attrib that is found
	S32 *piProjectileTags;				AST(STRUCTPARAM, SUBTABLE(PowerTagsEnum))

	// the relative orientation to base the teleport offset from
	TeleportOffsetOrientation eTeleportOffsetOrientation;
	
	// forward/backward offset in the facing direction in feet
	F32 fOffsetForward;

	// right/left offset in the facing direction in feet
	F32	fOffsetRight;

	// up/down offset 
	F32 fOffsetUp;
	
	// forward/backward offset in the facing direction in feet. Takes priority over fOffsetForward 
	Expression *pExprDistanceFront;		AST(NAME(ExprBlockDistanceFront), LATEBIND)

	// right/left offset in the facing direction in feet. Takes priority over fOffsetRight 
	Expression *pExprDistanceRight;		AST(NAME(ExprBlockDistanceRight), LATEBIND)

	// up/down offset. Takes priority over fOffsetUp  
	Expression *pExprDistanceAbove;		AST(NAME(ExprBlockDistanceAbove), LATEBIND)

	// the yaw offset from whatever the facing would end up being
	F32 fFacingYawOffset;
	
	// If set, the offset direction uses pitch. By default it justs uses yaw.
	U32 bOffsetUsesPitch : 1;

	// If set, the offset direction uses roll. By default it justs uses yaw.
	U32 bOffsetUsesRoll : 1;

	// if set, clients that activate this power get the relative position locally
	U32 bClientViewTeleport : 1;

	// when adjusting the facing of the character, modify the pitch as well
	U32 bFacingUsePitch : 1;

	// normally, the entity will attempt to snap to the ground if it is close to them (see ModTeleportGetValidOffsetLocation)
	// this switch disables that behavior & is useful for things like flying critters
	U32 bDontAttemptGroundSnap : 1;
		
} TeleportParams;


// Defines the power used in a TeleThrow AttribMod
AUTO_STRUCT;
typedef struct TeleThrowParams
{
	AttribModDefParams params;		AST(POLYCHILDTYPE(kAttribType_TeleThrow))
		// Must be first.  Parent parameter struct.

	F32 fRadius;
		// How far to search around the source for something to throw

	REF_TO(PowerDef) hDef;			AST(NAME(Def), REFDICT(PowerDef))
		// Handle to PowerDef to be applied by the thrown entity to the target

	REF_TO(PowerDef) hDefFallback;	AST(NAME(DefFallback), REFDICT(PowerDef))
		// Handle to PowerDef to be applied by the source to the target if there
		//  is nothing to throw

} TeleThrowParams;



// Defines the potential source and target entities for TriggerComplex AttribMods
AUTO_ENUM;
typedef enum TriggerComplexEntity
{
	kTriggerComplexEntity_ModTarget,	ENAMES(ModTarget)
		// The target of the AttribMod

	kTriggerComplexEntity_ModSource,	ENAMES(ModSource)
		// The immediate source of the AttribMod

	kTriggerComplexEntity_ModOwner,		ENAMES(ModOwner)
		// The ultimate owner of the AttribMod

	kTriggerComplexEntity_EventOther,	ENAMES(EventOther)
		// The other party to the event (the source or the target of the event opposite the ModTarget)

} TriggerComplexEntity;

// data required for swinging power, the dynfixinfo is a ref to make the editor go into pick validated mode.
AUTO_STRUCT;
typedef struct SwingingParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_Swinging))
		// Must be first.  Parent parameter struct.

	REF_TO(DynFxInfo) hSwingingFx;	AST(NAME(SwingingFx), REFDICT(DynFxInfo))
}SwingingParams;

// Defines the response and events responded to by an TriggerComplex AttribMod
AUTO_STRUCT;
typedef struct TriggerComplexParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_TriggerComplex))
		// Must be first.  Parent parameter struct.

	int *piCombatEvents;		AST(NAME(Events), SUBTABLE(CombatEventEnum))
		// List of CombatEvents to trigger from

	REF_TO(PowerDef) hDef;		AST(NAME(Def), REFDICT(PowerDef))
		// Handle to power def to be applied

	TriggerComplexEntity eSource;
		// The 'source' of the applied power

	TriggerComplexEntity eTarget;
		// The 'target' of the applied power

	Expression *pExprChance;		AST(NAME(ExprChanceBlock), REDUNDANT_STRUCT(Chance, parse_Expression_StructParam), LATEBIND)
		// Chance for the trigger to respond to any single event
			

	U32 bMagnitudeIsCharges : 1;
		// If this trigger limits the number of times it can respond before expiring using its magnitude

} TriggerComplexParams;


// Defines the potential source and target entities for TriggerSimple AttribMods
AUTO_ENUM;
typedef enum TriggerSimpleEntity
{
	kTriggerSimpleEntity_ModTarget,	ENAMES(ModTarget)
		// The target of the AttribMod

	kTriggerSimpleEntity_ModSource,	ENAMES(ModSource)
		// The immediate source of the AttribMod

	kTriggerSimpleEntity_ModOwner,	ENAMES(ModOwner)
		// The ultimate owner of the AttribMod
} TriggerSimpleEntity;

// Defines the response and events responded to by an TriggerSimple AttribMod
AUTO_STRUCT;
typedef struct TriggerSimpleParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_TriggerSimple))
		// Must be first.  Parent parameter struct.

	int *piCombatEvents;		AST(NAME(Events), SUBTABLE(CombatEventEnum))
		// List of CombatEvents to trigger from

	REF_TO(PowerDef) hDef;		AST(NAME(Def), REFDICT(PowerDef))
		// Handle to power def to be applied

	TriggerSimpleEntity eSource;
		// The 'source' of the applied power

	TriggerSimpleEntity eTarget;
		// The 'target' of the applied power

	Expression *pExprChance;		AST(NAME(ExprChanceBlock), REDUNDANT_STRUCT(Chance, parse_Expression_StructParam), LATEBIND)
		// Chance for the trigger to respond to any single event
		
	U32 bMagnitudeIsCharges : 1;
		// If this trigger limits the number of times it can respond before expiring using its magnitude

	U32 bRespondOncePerTick : 1;
		// If this trigger should only respond once per combat tick, regardless of the number of events

} TriggerSimpleParams;


// Defines the data used by a WarpTo AttribMod
AUTO_STRUCT;
typedef struct WarpToParams
{
	AttribModDefParams params;	AST(POLYCHILDTYPE(kAttribType_WarpTo))
		// Must be first.  Parent parameter struct.

	const char *cpchMap;		AST(NAME(Map), POOL_STRING)
		// Name of the map, for inter-map warping

	const char *cpchSpawn;		AST(NAME(Spawn), POOL_STRING)
		// Name of the spawn point, for inter- or intra-map warping

	REF_TO(DoorTransitionSequenceDef) hTransOverride; AST(NAME(TransitionOverride) REFDICT(DoorTransitionSequenceDef))
		// Optional transition sequence override for a map transfer

	U32 bDisallowSameMapTransfer : 1; AST(NAME(DisallowSameMapTransfer))
		// Disallow the character from warping if already on the warp map

	U32 bAllowedInQueueMap : 1;
		// Allows this to be used in a queue map, by default WarpTo's cannot be used in queue maps

} WarpToParams;

AUTO_STRUCT;
typedef struct ItemDurabilityParams
{
	AttribModDefParams params;				AST(POLYCHILDTYPE(kAttribType_ItemDurability))
	InvBagIDs eBagID;						AST(SUBTABLE(InvBagIDsEnum))
	int iSlotNum;							AST(DEFAULT(-1))
	REF_TO(InventorySlotIDDef) hSlotDef;	AST(REFDICT(InventorySlotIDDef))
} ItemDurabilityParams;

AUTO_ENUM;
typedef enum AIAggroTotalScaleApplyType
{
	kAIAggroTotalScaleApplyType_Self,
		// The attrib mod affects self

	kAIAggroTotalScaleApplyType_Owner,
		// The attrib mod affects owner

} AIAggroTotalScaleApplyType;

// Defines the parameters for an AIAggroTotalScale AttribMod
AUTO_STRUCT;
typedef struct AIAggroTotalScaleParams
{
	AttribModDefParams params;				AST(POLYCHILDTYPE(kAttribType_AIAggroTotalScale))
		// Must be first.  Parent parameter struct.

	AIAggroTotalScaleApplyType eApplyType;	AST(DEFAULT(kAIAggroTotalScaleApplyType_Self))
		// Determines whose aggro is affected by the attrib mod
} AIAggroTotalScaleParams;


AUTO_ENUM;
typedef enum CombatAdvantageType
{
	kCombatAdvantageApplyType_Advantage,
		// grants advantage
	
	kCombatAdvantageApplyType_Disadvantage,
		// grants advantage against the target

} CombatAdvantageType;


// Defines the parameters for an AIAggroTotalScale AttribMod
AUTO_STRUCT;
typedef struct CombatAdvantageParams
{
	AttribModDefParams params;				AST(POLYCHILDTYPE(kAttribType_CombatAdvantage))
	
	CombatAdvantageType	eAdvantageType;
		
} CombatAdvantageParams;

AUTO_STRUCT;
typedef struct ConstantForceParams
{
	AttribModDefParams params;				AST(POLYCHILDTYPE(kAttribType_ConstantForce))

	Expression *pExprYawOffset;				AST(NAME(ExprBlockYawOffset), LATEBIND)
				
	U32 bModOwnerRelative : 1;

} ConstantForceParams;

// Defines the parameters for an DynamicAttrib AttribMod
AUTO_STRUCT;
typedef struct DynamicAttribParams
{
	AttribModDefParams params;				AST(POLYCHILDTYPE(kAttribType_DynamicAttrib))

	Expression *pExprAttrib;				AST(NAME(ExprAttribType) LATEBIND)

	const char *cpchAttribMessageKey;		AST(NAME(AttribMessageKey))

	bool bDoNoCache;
}DynamicAttribParams;

// Returns the ParseTable for the Attrib
SA_RET_OP_VALID ParseTable *characterattribs_GetSpecialParseTable(S32 iAttrib);

// Returns if the Attrib is the current value of an attrib pool
bool attrib_isAttribPoolCur(AttribType eAttrib);

// Returns the first attrib pool that is found that matches eAttribCur
AttribPool* attrib_getAttribPoolByCur(AttribType eAttrib);

// sets the given attribPool (by name) timer to the given time once. 
// the tick time will then reset to the normal time once it pops
// note: pchName is assumed to be a POOL_STRING 
void attribPool_SetOverrideTickTimer(Character *pChar, const char *pchName, F32 fTimer);

// Performs load-time generation of custom params
S32 moddef_Load_Generate(SA_PARAM_NN_VALID AttribModDef *pdef);

// Performs the custom parameter setup during mod fill if the mod needs it
void mod_Fill_Params(int iPartitionIdx,
					 SA_PARAM_NN_VALID AttribMod *pmod,
					 SA_PARAM_NN_VALID AttribModDef *pdef,
					 SA_PARAM_NN_VALID PowerApplication *pApplication,
					 EntityRef erEffectedTarget);

// Dirties the various caches of Innate data when PowerDefs reload
void powerdefs_Reload_DirtyInnateCaches(void);


// Saves the Attributes on a Character that need to be persisted, but aren't hardcoded as persist
//void character_SaveAttributes(SA_PARAM_NN_VALID Character *pchar);
void character_SaveTempAttributes(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID TempAttributes *pTempAttributes);

// Loads the Attributes on from a Character's saved data back onto the Character
void character_LoadSavedAttributes(SA_PARAM_NN_VALID Character *pchar);

// Regenerates the InnateAccrual of all Characters that have been noted as dirty
void combat_RegenerateDirtyInnates(void);

// Notes that the character's innate effects from equipment have changed
void character_DirtyInnateEquip(Character *p);

// Notes that the character's innate powers have changed
void character_DirtyInnatePowers(Character *p);

// Notes that the characer's purchased stats have changed
void character_DirtyPowerStats(Character *p);

// Notes that the Character's overall innate accrual has changed
void character_DirtyInnateAccrual(SA_PARAM_NN_VALID Character *pchar);

// Fetches the base accrual set for this character, which is based on: 
//  class, level and innate powers
//  stats
// Optionally sets a dirty bit to true if it had to do any recalculation
SA_ORET_OP_VALID AttribAccrualSet *character_GetInnateAccrual(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID S32 *pbDirtyOut);

// sets to default values
// if bStrMultOnly is true, the struct will not be zeroed out
// StrMult attribs are set to 1.0
void AttribAccrualSet_SetDefaultValues(AttribAccrualSet *pSet, bool bStrMultOnly);

// Returns the effective magnitude of an AttribMod, given the Character it's on
F32 mod_GetEffectiveMagnitude(int iPartitionIdx,
							  SA_PARAM_NN_VALID AttribMod *pmod,
							  SA_PARAM_NN_VALID AttribModDef *pmoddef,
							  SA_PARAM_NN_VALID Character *pchar);

// Returns the resistance to an AttribMod, given the Character resisting it
F32 mod_GetResist(int iPartitionIdx,
				  SA_PARAM_NN_VALID AttribMod *pmod,
				  SA_PARAM_NN_VALID AttribModDef *pmoddef,
				  SA_PARAM_NN_VALID Character *pchar,
				  SA_PARAM_OP_VALID F32 *pfResTrueOut,
				  SA_PARAM_OP_VALID F32 *pfImmuneOut,
				  SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsOut);

// Returns the generic resistance to an Attrib, given the Character resisting it.
F32 character_GetResistGeneric(int iPartitionIdx,
							   SA_PARAM_NN_VALID Character *pchar,
							   AttribType offAttrib,
							   SA_PARAM_OP_VALID F32 *pfResTrueOut,
							   SA_PARAM_OP_VALID F32 *pfImmuneOut);

// Returns the strength for an AttribModDef, given the Character and optional power/enhancements/target involved in
//  creating it.  If passed a CharacterAttribs array, the function will do a lookup into that, instead
//  of basing strength off the Character and power/enhancements/target.
F32 moddef_GetStrength(int iPartitionIdx,
					   SA_PARAM_NN_VALID AttribModDef *pdef,
					   SA_PARAM_OP_VALID Character *pchar,
					   SA_PARAM_OP_VALID PowerDef *ppowdefActing,
					   S32 iLevelMain,
					   S32 iLevelInline,
					   F32 fTableScaleInline,
					   SA_PARAM_OP_VALID Power **ppEnhancements,
					   EntityRef erTarget,
					   SA_PARAM_OP_VALID CharacterAttribs *pattrStrOverride,
					   S32 bExcludeRequires,
					   SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsOut, 
					   SA_PARAM_OP_VALID F32 *pfStrAddOut);

// Returns the generic strength of an Attrib, given the Character creating it.
F32 character_GetStrengthGeneric(int iPartitionIdx,
								 SA_PARAM_NN_VALID Character *pchar,
								 AttribType offAttrib,
								 SA_PARAM_OP_VALID F32 *pfStrAddOut);


// Returns the basic value of an attribute with respect to the Power and Character.
//  Automatically includes relevant Enhancements.  Optionally with respect to a specific target.
F32 character_PowerBasicAttrib(int iPartitionIdx,
							   SA_PARAM_NN_VALID Character *pchar,
							   SA_PARAM_OP_VALID Power *ppow,
							   AttribType offAttrib,
							   EntityRef erTarget);

// Custom call for character_PowerBasicAttrib() on kAttribType_Disable, which has an early exit
//  if there are no Basic Disable mods.
F32 character_PowerBasicDisable(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_NN_VALID Power *ppow);

// Returns the basic value of an attribute with respect to the Power and Character.
//  Uses specific list of Enhancements.    Optionally with respect to a specific target.
//  If Power is set use that, otherwise use ppowDef and don't check inline enhancements
F32 character_PowerBasicAttribEx(int iPartitionIdx,
								 SA_PARAM_NN_VALID Character *pchar,
								 SA_PARAM_OP_VALID Power *ppow,
								 SA_PARAM_OP_VALID PowerDef *ppowDef, 
								 AttribType offAttrib,
								 SA_PARAM_OP_VALID Power **ppEnhancements,
								 EntityRef erTarget);


// Resets a Character's AttribPools to their initial values
void character_AttribPoolsReset(SA_PARAM_NN_VALID Character *pchar, bool bResetNonPersistedOnly);

// Clamps a Character's AttribPools to the current minimum and maximum
void character_AttribPoolsClamp(SA_PARAM_NN_VALID Character *pchar);

// Sets a Character's AttribPools to their minimum values
void character_AttribPoolsEmpty(SA_PARAM_NN_VALID Character *pchar);

void character_AttribPoolRespawn(SA_PARAM_NN_VALID Character *pChar);

//Auto fills attrib pools in a transaction. Called during creation of persisted entities
void character_FillSavedAttributesFromClass(NOCONST(Character) *pChar, int iLevel);



// Case-by-case code for handling special attrib mods, if they're not on a Character
void mod_ProcessSpecialUnowned(SA_PARAM_NN_VALID AttribMod *pmod,
							   F32 fRate,
							   int iPartitionIdx,
							   GameAccountDataExtract *pExtract);


// Temporary resting place for mod processing functions

// Calculates the magnitude of an AttribModDef, assuming it's from an Innate PowerDef
F32 mod_GetInnateMagnitude(int iPartitionIdx,
						   SA_PARAM_NN_VALID AttribModDef *pdef,
						   SA_PARAM_OP_VALID Character *pchar,
						   SA_PARAM_OP_VALID CharacterClass *pClass,
						   int iLevel,
						   F32 fTableScale);

// Slimmed down and tweaked version of mod_Process, used to accumulate mods from innate powers
//  Returns true if the accrual changed.
int mod_ProcessInnate(int iPartitionIdx,
					  SA_PARAM_NN_VALID AttribModDef *pdef, 
					  SA_PARAM_OP_VALID Character *pchar, 
					  SA_PARAM_OP_VALID CharacterClass *pClass,
					  int iLevel,
					  F32 fTableScale,
					  SA_PARAM_NN_VALID AttribAccrualSet *pattrSet);

// Processes an AttribMod on a character; returns true if the mod has expired
void mod_Process(int iPartitionIdx,
				 SA_PARAM_NN_VALID AttribMod *pmod,
				 SA_PARAM_NN_VALID Character *pchar,
				 F32 fRate,
				 AttribAccrualSet *pattrSet,
				 SA_PARAM_OP_VALID int *piModes,
				 U32 uiTimeLoggedOut,
				 GameAccountDataExtract *pExtract);



// Functions related to netsending of Attributes (defined in CombatConfig)

// Returns true if the Character needs to send Attributes
S32 character_AttribsNetCheckUpdate(SA_PARAM_NN_VALID Character *pchar);

// Sends the Character's Attributes
void character_AttribsNetSend(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);

// Receives the Character's Attributes.  Safely consumes all the Attributes if there isn't a Character.
void character_AttribsNetReceive(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);

// Returns true if the Character needs to send innate Attributes
S32 character_AttribsInnateNetCheckUpdate(SA_PARAM_NN_VALID Character *pchar);

// Sends the Character's innate Attributes
void character_AttribsInnateNetSend(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);

// Receives the Character's innate Attributes.  Safely consumes all the Attributes if there isn't a Character.
void character_AttribsInnateNetReceive(SA_PARAM_OP_VALID Character *pchar, SA_PARAM_NN_VALID Packet *pak);



// Misc utility functions related to specific attributes

// Strikes a target EntityRef with the source Character's held object, and makes the source Character drop said object
void character_DropHeldObjectOnTarget(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, EntityRef erTarget, GameAccountDataExtract *pExtract);

// Returns true if all the Character's Shield attribmods are essentially full (or if they don't have any)
S32 character_ModsShieldsFull(SA_PARAM_NN_VALID Character *pchar);


bool character_IsRooted(Character *pChar);
bool character_IsHeld(Character *pChar);
bool character_IsDisabled(Character *pChar);

//Returns whether the character is under the affects of a mod compared to thier class' attrib.
bool character_AffectedBy(Character *pChar, AttribType eAttrib);

AttribStatsPresetDef* attribstatspreset_GetDefByName( const char* pchName );
NOCONST(SavedAttribStats)* attribstatspreset_CreateStatsFromDefName( const char* pchName );
TempAttribute *character_NewTempAttribute(SA_PARAM_NN_VALID Character *pChar, AttribType eAttrib);
void entity_ModifySavedAttributes(Entity *pEnt, ItemDef *pItemDef, TempAttributes *pTempAttributes, Entity *pItemOwner, U64 uiItemID );

Entity* ModTeleportGetTeleportMainEntity(	Character *pChar, 
											Entity *pTarget,
											AttribModDef *pTeleportAttribModDef);

S32 ModTeleportGetTeleportTargetTranslations(	Character *pChar, 
												Entity *eTarget, 
												AttribModDef *pTeleportAttribModDef,
												Vec3 vTeleportBasePosOut,
												Vec3 vTeleportPYROut);

F32 ModCalculateEffectiveMagnitude(F32 fMag, F32 fResistTrue, F32 fResist, F32 fAvoid, F32 fImmune, F32 fAdd);

void character_FakeEntInitAccrual(Character* pchar);

F32 character_ProcessShields(int iPartitionIdx, Character *pchar, AttribType offAttrib, 
								F32 fMag, F32 fMagNoResist, AttribMod *pmodDamage, S32 *pbCountAsDamaged, 
								bool bPeekOnly, CombatTrackerFlag *pFlagsAddedOut);

void character_ModGetMitigators(int iPartitionIdx, 
								SA_PARAM_NN_VALID AttribMod *pmod, 
								SA_PARAM_NN_VALID AttribModDef *pmoddef, 
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_OP_VALID F32 *pfResTrueOut,
								SA_PARAM_NN_VALID F32 *pfResistOut, 
								SA_PARAM_OP_VALID F32 *pfImmuneOut, 
								SA_PARAM_NN_VALID F32 *pfAvoidOut, 
								SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsOut);

F32 character_GetSpeedCharge(int iPartitionIdx, Character *pchar, Power *ppow);
F32 character_GetSpeedPeriod(int iPartitionIdx, Character *pchar, Power *ppow);

bool character_TriggerAttribModCheckChance(int iPartitionIdx, Character *pchar, EntityRef erModOwner, Expression *pExprChance);

