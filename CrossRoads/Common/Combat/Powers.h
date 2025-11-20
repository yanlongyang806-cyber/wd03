/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWERS_H__
#define POWERS_H__
GCC_SYSTEM

#include "referencesystem.h"
#include "structDefines.h"	// For StaticDefineInt
#include "Message.h" // For DisplayMessage

#include "AttribMod.h" // For PowerTagsStruct
#include "CharacterAttribsMinimal.h" // For enums
#include "PowersEnums.h" // For enums
#include "WorldLibEnums.h" // For enums


#define MAX_POWER_NAME_LEN		60
#define MAX_POWER_NAMEFULL_LEN	(CRYPTIC_MAX_PATH + MAX_POWER_NAME_LEN + 1)

#define ATTRIB_KEY_BLOCK_BITS 6
#define ATTRIB_KEY_BLOCK_SIZE (1 << ATTRIB_KEY_BLOCK_BITS)

#define POWERS_FOREVER FLT_MAX

// POWERID Chunks: Top 2 bits for type, middle 8 bits for ent id, low 22 bits for base id
#define POWERID_TYPE_BITS 2
#define POWERID_ENT_BITS 8
#define POWERID_BASE_BITS 22

// Macro for putting together a POWERID
#define POWERID_CREATE(uiIDBase,uiIDEnt,uiIDType) ((uiIDBase) | ((uiIDEnt) << POWERID_BASE_BITS) | ((uiIDType) << (POWERID_ENT_BITS+POWERID_BASE_BITS)))

// Macros for getting chunks from a POWERID
#define POWERID_GET_ENT(uiPowerID) (((uiPowerID) >> POWERID_BASE_BITS) & ((1<<POWERID_ENT_BITS)-1))
#define POWERID_GET_BASE(uiPowerID) ((uiPowerID) & ((1<<POWERID_BASE_BITS)-1))

// Three types, hopefully don't ever have to add more
#define POWERID_TYPE_MAIN 0			// A main (player) Entity acting as itself
#define POWERID_TYPE_SAVEDPET 1		// A SavedPet Entity, or a main Entity using the Puppet system to act as a SavedPet
#define POWERID_TYPE_TEMP 2			// A temporary source (AttribMods, Refer-A-Friend, etc)

#define POWERID_SAVEDPET_TEMPPUPPET (1 << 7)



#define POWERS_CHANCE_ERROR(chance) ((chance)<=0.0f || (chance)>1.0f)
#define POWERS_CHANCE_WARNING(chance) ((chance)<0.01f || ((chance)>0.99f && (chance)<1.f))

extern DictionaryHandle g_hPowerTargetDict;
extern DictionaryHandle g_hPowerDefDict;
extern DictionaryHandle g_hPowerEmitDict;

extern bool g_bPowersDebug;
extern bool g_bPowersErrors;
extern bool g_bPowersSelectDebug;
extern int g_bNewAttributeSystem;

// Global list of names of Powers that are disabled
extern const char **g_ppchPowersDisabled;

void combatdebug_PowersDebugPrint(Entity *e, S32 detailFlag, const char *format, ...);


#define PowersDebugPrint(flag,format,...)				if(g_bPowersDebug) combatdebug_PowersDebugPrint(NULL,flag,format,##__VA_ARGS__)
#define PowersDebugPrintEnt(flag,ent,format, ...)	if(g_bPowersDebug) combatdebug_PowersDebugPrint(ent,flag,format,##__VA_ARGS__)
#define PowersError(format, ...) if(g_bPowersErrors) Errorf(format,##__VA_ARGS__)

#define PowersSelectDebug(format, ...) if(g_bPowersSelectDebug) printf(format,##__VA_ARGS__)

#define POWERNAME(ppow) ((ppow) && (GET_REF((ppow)->hDef)) ? GET_REF((ppow)->hDef)->pchName : "NULL")
#define POWERLEVEL(ppow,iDefault) (((ppow) && (ppow)->pParentPower && (ppow)->pParentPower->iLevel > 0) ? (ppow)->pParentPower->iLevel+(ppow)->pParentPower->iLevelAdjustment : ((ppow) && (ppow)->iLevel > 0) ? (ppow)->iLevel+(ppow)->iLevelAdjustment : (iDefault))
#define POWERDEF_ATTRIBCOST(pdef) (((pdef) && ATTRIB_NOT_DEFAULT((pdef)->eAttribCost)) ? (pdef)->eAttribCost : kAttribType_Power)


// Forward declarations

typedef struct	AIPowerConfigDef	AIPowerConfigDef;
typedef struct	AttribModDef	AttribModDef;
typedef struct	Character	Character;
typedef struct	NOCONST(Character)	NOCONST(Character);
typedef struct	Character		Character;
typedef struct  CostumeDisplayData CostumeDisplayData;
typedef struct  Entity			Entity;
typedef struct	Expression		Expression;
typedef struct	InheritanceData	InheritanceData;
typedef struct	PTNodeDef		PTNodeDef;
typedef struct	PowerActivation	PowerActivation;
typedef struct	PowerAnimFX		PowerAnimFX; 
typedef struct  SensitivityStruct SensitivityStruct;
typedef struct	AlwaysPropSlotDef AlwaysPropSlotDef;
typedef struct	PetRelationship PetRelationship;
typedef struct  Item			Item; 
typedef struct  ItemDef			ItemDef; 
typedef struct	PTNode			PTNode;
extern StaticDefineInt CombatEventEnum[];
extern StaticDefineInt PowerCategoriesEnum[];
extern StaticDefineInt PowerTagsEnum[];
extern StaticDefineInt PowerAITagsEnum[];
extern StaticDefineInt CharClassTypesEnum[];
extern StaticDefineInt PowerTypeEnum[];


/***** POWER GROUPING *****/

typedef struct PEPowerDefGroup PEPowerDefGroup;
typedef struct PEInhNode PEInhNode;
typedef struct PowerDef PowerDef;

AUTO_STRUCT;
typedef struct PEPowerDefGroup
{
	char *pchName;
	PEPowerDefGroup **ppGroups;
	PowerDef **ppPowers;
} PEPowerDefGroup;

AUTO_STRUCT;
typedef struct PEInhNode
{
	char *pchName;
	PEInhNode **ppChildren;
	PowerDef *pPowerDef;
}PEInhNode;
extern PEPowerDefGroup s_PEPowerDefTopGroup;

extern ParseTable parse_PowerDef_ForExpr[];
#define TYPE_parse_PowerDef_ForExpr PowerDef_ForExpr


/***** ENUMS *****/

// PowerType defines the archetype of the power, which primarily controls how it is activated
AUTO_ENUM;
typedef enum PowerType
{
	kPowerType_Combo,
		// A shell that contains component powers that are activated instead

	kPowerType_Click,
		// Activates when the user clicks it, remains current while activating
	
	kPowerType_Instant,
		// Activates instantly, regardless of what else is currently happening to the player. Does not queue up, charge, or maintain any state.

	kPowerType_Maintained,
		// Repeatedly activates until the user turns it off, remains current while activating

	kPowerType_Toggle,
		// Repeatedly activates until the user turns it off, does not remain current after being turned on

	kPowerType_Passive,
		// Repeatedly activates without any input from the user, is never current

	kPowerType_Innate,
		// Does not activate, simply exists and applies its effects to the character

	kPowerType_Enhancement,
		// Does not activate, applies its effects to adjust or extend other powers

	kPowerType_Count, EIGNORE

	kPowerType_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} PowerType;

#define PowerType_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kPowerType_MAX <= (1 << (PowerType_NUMBITS - 1)));

#define POWERTYPE_TARGETED(eType) ((eType)<kPowerType_Innate)
#define POWERTYPE_ACTIVATABLE(eType) ((eType)<kPowerType_Passive)
#define POWERTYPE_PERIODIC(eType) ((eType)>=kPowerType_Maintained && (eType)<=kPowerType_Passive)


// EffectArea defines the area over which a power acts
AUTO_ENUM;
typedef enum EffectArea
{
	kEffectArea_Character,
		// A single character

	kEffectArea_Location,
		// A single spot on the ground

	kEffectArea_Cylinder,
		// A cylinder in a direction

	kEffectArea_Cone,
		// A cone in a direction

	kEffectArea_Sphere,
		// A sphere around the target

	kEffectArea_Team,
		// Members of the entity's team on the map, optionally within a sphere around the target

	kEffectArea_Volume,
		// The volume of the caster

	kEffectArea_Map,
		// The entire map

	kEffectArea_Count, EIGNORE

	kEffectArea_MAX, EIGNORE // Leave this last, not a valid fTag, just for the compile time assert below
} EffectArea;

#define EffectArea_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kEffectArea_MAX <= (1 << (EffectArea_NUMBITS - 1)));

// Defines the order in which AoE targets are sorted
AUTO_ENUM;
typedef enum EffectAreaSort
{
	kEffectAreaSort_Primary_Dist,
		// Primary target first, then by distance from area source

	kEffectAreaSort_Dist,
		// By distance from area source

	kEffectAreaSort_Primary_Random,
		// Primary target first, then random

	kEffectAreaSort_Random,
		// Random

	kEffectAreaSort_HardTarget_Dist,
		// Hard target first, then by distance from the area source

	kEffectAreaSort_HardTarget_Random,
		// Hard target first, then random

	kEffectAreaSort_Count, EIGNORE

} EffectAreaSort;

#define EffectAreaSort_NUMBITS 5 // 5 bits can hold up to an enum value of 2^4-1, because of the sign bit
STATIC_ASSERT(kEffectAreaSort_Count <= (1 << (EffectAreaSort_NUMBITS - 1)));

// Defines combat state handling for powers
AUTO_ENUM;
typedef enum PowerEnterCombatType
{
	kPowerEnterCombatType_Default,
		// Default logic: Enter combat only when targeting a foe, or a non-foe that is in combat
	
	kPowerEnterCombatType_Always,
		// Always set the source target in combat

	kPowerEnterCombatType_Never,
		// Never set the source target in combat

	kPowerEnterCombatType_Count, EIGNORE

} PowerEnterCombatType;

#define PowerEnterCombatType_NUMBITS 3 // 3 bits can hold up to an enum value of 2^2-1, because of the sign bit
STATIC_ASSERT(kPowerEnterCombatType_Count <= (1 << (PowerEnterCombatType_NUMBITS - 1)));

// RequireValidTarget override type for powers
AUTO_ENUM;
typedef enum PowerRequireValidTarget
{
	kPowerRequireValidTarget_Default,
		// Default logic: Use the value on the CombatConfig or ControlScheme
	
	kPowerRequireValidTarget_Never,
		// Never require a valid target

	kPowerRequireValidTarget_Always,
		// Always require a valid target

	kPowerRequireValidTarget_Count, EIGNORE

} PowerRequireValidTarget;

#define PowerRequireValidTarget_NUMBITS 3 // 3 bits can hold up to an enum value of 2^2-1, because of the sign bit
STATIC_ASSERT(kPowerRequireValidTarget_Count <= (1 << (PowerRequireValidTarget_NUMBITS - 1)));

// TargetVisibility defines line of sight requirements for targeting and affecting
AUTO_ENUM;
typedef enum TargetVisibility
{
	kTargetVisibility_LineOfSight,
		// The source must have a direct line of sight to the target

	kTargetVisibility_None,
		// No visibility requirements

	kTargetVisibility_Count, EIGNORE

	kTargetVisibility_MAX, EIGNORE // Leave this last, not a valid fTag, just for the compile time assert below
} TargetVisibility;

#define TargetVisibility_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kTargetVisibility_MAX <= (1 << (TargetVisibility_NUMBITS - 1)));

// TargetTracking defines how the Character should track the target of the Power
AUTO_ENUM;
typedef enum TargetTracking
{
	kTargetTracking_Full,
		// The target is constantly tracked

	kTargetTracking_UntilCurrent,
		// The target is tracked until the activation becomes current

	kTargetTracking_UntilFirstApply,
		// The target is tracked until the first application of the power

	kTargetTracking_Count, EIGNORE

	kTargetTracking_MAX, EIGNORE // Leave this last, not a valid fTag, just for the compile time assert below
} TargetTracking;

#define TargetTracking_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(kTargetTracking_MAX <= (1 << (TargetTracking_NUMBITS - 1)));

// TargetType defines relationships between the caster and a potential target
// This enum is extended by each game, in character_target.h
AUTO_ENUM;
typedef enum TargetType
{
	kTargetType_Alive		= 1 << 0,
	// alive

	kTargetType_Self		= 1 << 1,
	// yourself

	kTargetType_Player		= 1 << 2,
	// a player

	kTargetType_Critter		= 1 << 3,
	// a critter

	kTargetType_Friend		= 1 << 4,
	// a friend

	kTargetType_Foe			= 1 << 5,
	// a foe

	kTargetType_Neutral		= 1 << 6,
	// neither friend nor foe, and thus not a legal combat target (included for utility)

	kTargetType_Teammate	= 1 << 7,
	// an entity on source's "team" (source's pet or owner, or source's (owner's) Team member, or Team member's pet)

	kTargetType_PrimaryPet	= 1 << 8,
	// source's pet

	kTargetType_Owner		= 1 << 9,
	// source's owner

	kTargetType_Creator		= 1 << 10,
	// source's creator

	kTargetType_Owned		= 1 << 11,
	// a entity that considers the source its Owner

	kTargetType_Created		= 1 << 12,
	// a entity that considers the source its Creator

	kTargetType_Destructible = 1 << 13,
	// a destructible object

	kTargetType_PseudoPlayer = 1 << 14,
	// a critter that is flagged as a pseudo-Player (e.g. Bridge Officer, Henchman, etc)

	kTargetType_Untargetable = 1 << 15,
	// Not targetable

	kTargetType_NearDeath = 1 << 16,
	// near death

	kTargetType_Ridable = 1 << 17,
	// Set up to be ridden

	kTargetType_Critterpet = 1 << 18,
	// near death

	kTargetType_MaxGeneric = 1 << 19, EIGNORE
	// Start product-specific flags with this one
} TargetType;

AUTO_ENUM;
typedef enum EnhancementAttachUnownedType
{
	kEnhancementAttachUnownedType_Never = 0,
	// never attach to unowned powers. Should be the default

	kEnhancementAttachUnownedType_AlwaysIfAttached,
	// always attach to unowned powers 

	kEnhancementAttachUnownedType_CheckAttachExpr,
	// run the attach expression on the unowned powers 

	kEnhancementAttachUnownedType_MAX, EIGNORE

} EnhancementAttachUnownedType;

#define EnhancementAttachUnownedType_NUMBITS 3 
STATIC_ASSERT(kEnhancementAttachUnownedType_MAX <= (1 << (EnhancementAttachUnownedType_NUMBITS - 1)));


AUTO_STRUCT;
typedef struct PowerPurposeNames
{
	const char **ppchNames; AST(NAME(PowerPurposeName))

} PowerPurposeNames;

// Struct for loading the names of ModStackGroups
AUTO_STRUCT;
typedef struct ModStackGroupNames
{
	const char **ppchNames;	AST(NAME(AttribModStackGroup))
} ModStackGroupNames;

extern DefineContext *g_pDefinePowerAITags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefinePowerAITags);
typedef enum PowerAITags {
	kPowerAITag_Attack			= 1 << 1,
	kPowerAITag_Heal			= 1 << 2,
	kPowerAITag_Shield_Heal		= 1 << 3,
	kPowerAITag_Buff			= 1 << 4,
	kPowerAITag_Cure			= 1 << 5,
	kPowerAITag_Control			= 1 << 6,
	kPowerAITag_Lunge			= 1 << 7,
	kPowerAITag_Flight			= 1 << 8,
	kPowerAITag_Resurrect		= 1 << 9,
	kPowerAITag_AreaEffect		= 1 << 10,
	kPowerAITag_OutOfCombat		= 1 << 11,
	kPowerAITag_UseTargetPos	= 1 << 12,

	// this will end up overriding the AIPowerConfigDef's maxDist field to what the AI's prefMaxRange if it is less than
	// the initialyl set range
	// Note: will NOT work with useDynamicPrefRange
	kPowerAITag_UseWithinPreferredMax	= 1 << 13,
	

	// DONT ADD VALUES AFTER THIS
	kPowerAITag_CODEMAX			= kPowerAITag_UseWithinPreferredMax, EIGNORE
	kPowerAITag_AllCode			= (kPowerAITag_CODEMAX<<1)-1, EIGNORE
} PowerAITags;

AUTO_ENUM;
typedef enum PowerTacticalMovementMode
{
	kPowerTacticalMovementMode_Aim		= 1 << 0,
	kPowerTacticalMovementMode_Roll		= 1 << 1,
} PowerTacticalMovementMode;

AUTO_ENUM;
typedef enum PowerActivateRules
{
	kPowerActivateRules_None = 0,
		// This power cannot be activated

	kPowerActivateRules_SourceDead = (1 << 0),
		// The character can use this power while dead

	kPowerActivateRules_SourceAlive = (1 << 1),
		// The character can use this power while alive
		// WARNING: if you change the value of this, change the DEFAULT eActivateRules for PowerDefs
	
	kPowerActivateRules_Count, EIGNORE
} PowerActivateRules;

#define PowerActivateRules_NUMBITS 3 // 3 bits can hold up to an enum value of 2^2-1, because of the sign bit
STATIC_ASSERT(kPowerActivateRules_Count <= (1 << (PowerActivateRules_NUMBITS - 1)));

/***** END ENUMS *****/

// Defines a PowerCategory and its related data.  Referenced by indexing into the
//  global PowerCategories structure.
AUTO_STRUCT;
typedef struct PowerCategory
{
	const char *pchName;	AST(STRUCTPARAM, POOL_STRING)
		// The name of the category

	S32 iPreferredTray;
		// Preferred tray for a Power with this PowerCategory.  0 means no preference.

	F32 fTimeCooldown;
		// A global cool down timer for all powers that use this category. On a per entity basis.

	F32 fTimeCooldownOutOfCombat;	AST(DEFAULT(-1))
		// A global cool down timer for all powers that use this category. On a per entity basis.
		// Used only when the critter is out of combat

	int *piRequiredItemCategories;			AST(SUBTABLE(ItemCategoryEnum))
		// If DDWeaponBased is set, select a weapon that matches all of these item categories

	S32 iSortGroup;
		// What "type" of power this is. Used to collate powers for logging purposes. 0 means don't collate

	PowerTacticalMovementMode	eMatchTacticalMode; AST(FLAGS, SUBTABLE(PowerTacticalMovementModeEnum))
		// list of tactical modes, must match any of the given 

	U32 bSlottingRequired : 1;
		// If set, any Powers with this PowerCategory must be slotted to be executed

	U32 bToggleExclusive : 1;
		// If set, any Powers with this PowerCategory that are toggles are mutually exclusive

	U32 bAutoAttackServer : 1;
		// If set, any Powers with this PowerCategory that are Clicks are special "AutoAttack" Powers that
		//  behave a bit like toggles and are run by the server.  Cached on the PowerDef.

	U32 bAutoAttackEnabler : 1;
		// If set, any Powers with this PowerCategory automatically enable AutoAttack.  Cached on the PowerDef.

	U32 bAutoAttackDisabler : 1;
		// If set, any Powers with this PowerCategory automatically disable AutoAttack  Cached on the PowerDef.

	U32 bWeaponBased : 1;
		// If set, any Powers with this PowerCategory have a weapon-categorized Item attached to their Applications

	U32 bHasAutoSlotPriority : 1;
		// If set, this power will auto slot before any power whose category does not have this flag set

	U32 bIgnoreTargetPitch : 1;
		// If set, this power will not take in to account the players camera or facing pitch when targeting
		// Only relevant when bUseFacingPitch is on in the control scheme

	U32 bDisplayAttribModsAsBaseStat : 1;
		// If set, things that show the break down of an entity's stats will treat this as part of the base
		// stat instead of as a bonus received from a power.
} PowerCategory;

// Array of the PowerCategories, loaded and indexed directly
AUTO_STRUCT;
typedef struct PowerCategories
{
	PowerCategory **ppCategories;	AST(NAME(PowerCategory))
} PowerCategories;

// additional power info
AUTO_STRUCT;
typedef struct PowerConfig
{
	// this will lock powers if the map is not a powerhouse map, used for CO
	bool bLockPowersIfNotinPowerhouse;

}PowerConfig;

// Globally accessibly PowerCategories structure
extern PowerCategories g_PowerCategories;

// global config for powers
extern PowerConfig gPowerConfig;


// Defines all the names of power tags
AUTO_STRUCT;
typedef struct PowerTagNames
{
	char **ppchNames; AST(NAME(PowerTag))
}PowerTagNames;

// Defines all the names of power AI tags
AUTO_STRUCT;
typedef struct PowerAITagNames
{
	char **ppchNames; AST(NAME(PowerAITag))
}PowerAITagNames;

AUTO_STRUCT;
typedef struct TargetTypePair
{
	TargetType eRequire;				AST(FLAGS, SUBTABLE(TargetTypeEnum))
		// Bitfield describing relations that must be true

	TargetType eExclude;				AST(FLAGS, SUBTABLE(TargetTypeEnum))
		// Bitfield describing relations that must not be true

} TargetTypePair;

// Defines the relations allowed between a power's source and target
AUTO_STRUCT;
typedef struct PowerTarget
{
	char *pchName;						AST(STRUCTPARAM KEY POOL_STRING)
		// Unique internal name of the target type

	TargetType eRequire;				AST(FLAGS, SUBTABLE(TargetTypeEnum))
		// Bitfield describing relations that must be true

	TargetType eExclude;				AST(FLAGS, SUBTABLE(TargetTypeEnum))
		// Bitfield describing relations that must not be true

	TargetTypePair **ppOrPairs;			AST(NAME(Or))
		// Additional eRequire & eExclude pairs, which this PowerTarget can also match

	REF_TO(Message) hMsgDescription;	AST(NAME(msgDescription))
		// Description of the target type

	const char *cpchFile;				AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)

	U32 bDoNotTargetUnlessRequired : 1;	AST(NAME(DoNotTargetUnlessRequired, NeverTargetIfNotRequired))
		// Do not select a target during activation, unless RequireValidTarget is set

	U32 bUseLocationHitIfNoTarget : 1;	AST(NAME(UseLocationHitIfNoTarget))
		// Use a location for the hit/targeted FX if no targets are found for the power

	U32 bFaceActivateSticky : 1;		AST(ADDNAMES(FaceDuringPowerActivation))
		// Override for CombatConfig flag that enables facing for the entire power activation

	// Derived utility flags for checking certain common rules (rather than testing the all the bitfields)

	U32 bRequireSelf : 1;
		// If this PowerTarget can target only yourself

	U32 bAllowSelf : 1;
		// If this PowerTarget can target yourself

	U32 bAllowFoe : 1;
		// If this PowerTarget can target foes

	U32 bAllowFriend : 1;
		// If this PowerTarget can target friends

	U32 bAllowNearDeath : 1;
		// If this PowerTarget can target entities that are in the NearDeath state

	U32 bSafeForSelfOnly : 1;
		// If this PowerTarget is safe for use in the SelfOnly state

} PowerTarget;


// Description of a combo component and its requirements
AUTO_STRUCT;
typedef struct PowerCombo
{
	int iKey;						AST(KEY)
		//The Key for the power combo, used with inheritance

	REF_TO(PowerDef) hPower;		AST(NAME(Power) STRUCTPARAM)
		// Reference to the sub-power
	
	F32 fOrder;						AST(NAME(ComboOrder))
		//The order in which the power is checked. If 0, will be given a value

	Expression *pExprRequires;		AST(NAME(ExprBlockRequires, RequiresBlock), REDUNDANT_STRUCT(Requires,parse_Expression_StructParam),LATEBIND)
		// Expression to determine if this power executed in a combo

	int *piModeRequire;				AST(NAME(ModeRequire) SUBTABLE(PowerModeEnum))
		// The entity activating this power must have these modes currently active

	int *piModeExclude;				AST(NAME(ModeExclude) SUBTABLE(PowerModeEnum))
		// If the entity activating this power has one of these modes active, it will not execute this power

	F32 fPercentChargeRequired;		AST(ADDNAMES(TimeChargeRequired))
		// Percent. 1 means it requires a full charge to successful activate.

	Expression *pExprTargetClient;	AST(NAME(ExprBlockTargetClient), REDUNDANT_STRUCT(TargetClient,parse_Expression_StructParam),LATEBIND)
		// Expression to validate a target on the client.  Only used in special cases where the combo
		//  has very complex targeting rules.  The server does not evaluate this field for success or failure.

} PowerCombo;

// Description of an emit point for a Power
AUTO_STRUCT;
typedef struct PowerEmit
{
	const char *cpchName;			AST(NAME(Name), STRUCTPARAM, KEY, POOL_STRING)
		// Unique internal name of the emit point

	const char **ppchBits;			AST(NAME(Bits), POOL_STRING)
		// Animation bits that are appended when the Power starts some of its animation stages

	DisplayMessage msgDisplayName;	AST(STRUCT(parse_DisplayMessage))
		// Message to display for the name

} PowerEmit;


AUTO_STRUCT;
typedef struct PowerPropDef
{	
	bool bPropPower;
		//Turn on power propagation for this power

	int *eCharacterTypes;				AST(NAME(CharacterType), SUBTABLE(CharClassTypesEnum))
		//The Character type that this power can propagate to
}PowerPropDef;

// Basic definition of a power
AUTO_STRUCT 
AST_IGNORE(msgActivateSource) 
AST_IGNORE(msgActivateBroadcast) 
AST_IGNORE(msgHitSource) 
AST_IGNORE(msgHitTarget) 
AST_IGNORE(ModSubtargetAccuracy) 
AST_IGNORE(bUnrestrictedTargeting_GetTarget)
AST_IGNORE(DeactivateMaintainOnFailedApplication);
typedef struct PowerDef
{
	InheritanceData *pInheritance;				AST(SERVER_ONLY)
		// Optional data to make this def inherit from another def

	char *pchName;								AST( STRUCTPARAM KEY POOL_STRING )
		// Internal name of the power

	PowerTagsStruct tags;						AST(NAME(Tags), NO_TEXT_SAVE)
		//The tags the Power has, built from its AttribMods or Combos

	S32 *piCategories;							AST(NAME(Categories), SUBTABLE(PowerCategoriesEnum))
		// The categories the power has

	PowerPropDef powerProp;						AST(NAME(PowerProp))
		// Rules for power propagation

	
	// Cost fields
		
	AttribType eAttribCost;						AST(DEFAULT(-1) SUBTABLE(AttribTypeEnum))
		// Optional override to the Attribute used to pay the Cost

	Expression *pExprCost;						AST(NAME(ExprBlockCost, ExprCostBlock), REDUNDANT_STRUCT(ExprCost, parse_Expression_StructParam), LATEBIND)
		// Expression defining the cost to activate this Power

	Expression *pExprCostPeriodic;				AST(NAME(ExprBlockCostPeriodic), REDUNDANT_STRUCT(ExprCostPeriodic, parse_Expression_StructParam), LATEBIND)
		// Expression defining the cost to activate this power periodically.  If unset, periodic cost is the same as regular cost.

	Expression *pExprCostSecondary;				AST(NAME(ExprBlockCostSecondary), LATEBIND)
		// Expression defining the secondary cost to activate this Power, ignores eAttribCost (and is thus always Power)

	Expression *pExprCostPeriodicSecondary;		AST(NAME(ExprBlockCostPeriodicSecondary), LATEBIND)
		// Expression defining the secondary cost to activate this power periodically, ignores eAttribCost (and is thus always Power).  If unset, periodic cost is the same as regular cost.

	int iCostPowerMode;							AST(SUBTABLE(PowerModeEnum))
		// Optional PowerMode included as part of the cost to activate this Power
		//  When paid, this will expire all PowerMode AttribMods on the Character with this PowerMode
		//  This cost is not included as part of the periodic cost when specified on a periodic Power with no pExprCostPeriodic

	REF_TO(ItemDef) hCostRecipe;				AST(NAME(CostRecipe))
		// Recipe describing items required to activate this power.

	
	// Targeting fields
		
	REF_TO(PowerTarget) hTargetMain;			AST(NAME(TargetMain), REFDICT(PowerTarget))
		// The type of target that the power uses for targeting

	REF_TO(PowerTarget) hTargetAffected;		AST(NAME(TargetAffected), REFDICT(PowerTarget))
		// The type of target that the power actually affects

	F32 fTargetArc;
		// Degrees.  Arc of fire allowed for this PowerDef if the character can't face automatically.


	// Area of effect fields

	S32 iMaxTargetsHit;
		// Limits the number of targets this power is allowed to affect

	Expression *pExprRadius;					AST(NAME(ExprBlockRadius, ExprRadiusBlock), REDUNDANT_STRUCT(ExprRadius, parse_Expression_StructParam), LATEBIND)
		// Feet.  Radius of effect. Expression will return a number that will be the Radius
		// bEnhancePowerFields Enhanceable - Use power_GetRadius to access this field if you want to get a Power's true radius

	Expression *pExprInnerRadius;					AST(NAME(ExprBlockInnerRadius, ExprInnerRadiusBlock), REDUNDANT_STRUCT(ExprInnerRadius, parse_Expression_StructParam), LATEBIND)
		// Feet. Min radius of effect. Currently only applies to spheres.

	Expression *pExprArc;						AST(NAME(ExprBlockArc, ArcBlock, ExprArcBlock), REDUNDANT_STRUCT(ExprArc, parse_Expression_StructParam), REDUNDANT_STRUCT(Arc, parse_Expression_StructParam), LATEBIND)
		// Degrees.  Spherical degrees of the cone.

	F32 fYaw;
		// Degrees.  Yaw to direct this Power, relative to the default orientation.  
		// Only valid for Cone and Cylinder effect areas.

	F32 fPitch;
		// Degrees. Pitch to offset the power volume, relative to the default orientation. 
		// 0 is parallel with the XZ plane. 90 degrees is straight up.
		// Only valid for Cone and Cylinder effect areas.

	F32 fFrontOffset;
		// Feet. Distance to the front of the target to offset the power effect area, relative to the default orientation. 
		// Only valid for Cone and Cylinder effect areas.

	F32 fRightOffset;
		// Feet. Distance to the right of the target to offset the power effect area, relative to the default orientation. 

		// Only valid for Cone and Cylinder effect areas.
			
	F32 fUpOffset;
		// Feet. Distance above the target to offset the power effect area, relative to the default orientation. 
		// Only valid for Cone and Cylinder effect areas.
	
	F32 fRange;
		// Feet.  Max distance to target. 
		// bEnhancePowerFields Enhanceable - Use power_GetRange to access this field if you want to get a Power's true range

	F32 fRangeMin;
		// Feet.  Min distance to target.

	F32 fRangeSecondary;
		// Feet.  Max distance to secondary target if there is one.

	F32 fStartingRadius;
		// Feet. Only valid for Cone area of effect types

	// Timing fields

	F32 fTimeCharge;
		// Seconds.  Max time that this power can be charged.

	F32 fTimeActivate;
		// Seconds.  Time it takes after charge for this power to complete.

	F32 fTimeActivatePeriod;
		// Seconds.  Time it takes for this power to periodically activate.  Used for toggle,
		//  maintained and passive powers.

	U32 uiPeriodsMax;					AST(NAME(PeriodsMax))
		// Maximum number of periodic activations the power is allowed to make before it shuts
		//  off.  Used for toggle and maintained powers.  0 means no limit.

	F32 fTimePreactivate;
		// Seconds.  Time after charge but before activate.  Purely cosmetic except for motion

	F32 fTimeMaintain;					AST(NO_TEXT_SAVE)
		// Seconds.  Derived.  Max time that this power can be maintained.

	F32 fTimeRecharge;
		// Seconds.  Time after this power is used before it can be used again.

	F32 fTimeOverride;
		// Seconds.  Time at the end of activation where this power can be interrupted
		//  by an overriding power.

	F32 fTimeOverrideReactivePower;
		// Seconds.  Time at the end of activation where this power can be interrupted
		//  by a reactive power. If this is not set, fTimeOverride will be used. 

	F32 fTimeAllowQueue;				AST(DEFAULT(-1))
		// Seconds.  Time at the end of activation where this power will allow other
		//  powers to queue.

	F32 fTotalCastPeriod;				AST(NO_TEXT_SAVE)
		//Defined as preactive + activate + recharge time. Dervied.

	F32 fTimePostMaintain;
		// seconds. the time after a maintain is stopped that the character will be in the post maintain stage of the activation
		// this phase means the player isn't moving onto the next activation and will extend any activation movement penalties 

	// Charging and activation fields

	Expression *pExprRequiresQueue;			AST(NAME(ExprBlockRequiresQueue), LATEBIND)
		// Expression that must evaluate to true for the power to be queued

	Expression *pExprRequiresCharge;	AST(NAME(ExprBlockRequiresCharge, ExprRequiresChargeBlock), REDUNDANT_STRUCT(ExprRequiresCharge, parse_Expression_StructParam), LATEBIND)
		// Expression that must evaluate to true for the power to continue charging

	F32 fChargeRequire;
		//Percent. If this is a charged power, this represents how long the power must remain charging in order to execute

	int *piPowerModesRequired;			AST(SUBTABLE(PowerModeEnum))
		//The power modes required for this power to be available

	int *piPowerModesDisallowed;		AST(SUBTABLE(PowerModeEnum))
		//The power modes not allowed to be on for this power to be available

	int *piCombatEvents;				AST(SUBTABLE(CombatEventEnum))
		// List of CombatEvents to listen for, which will cancel this power if it's a Toggle or Maintained

	F32 fCombatEventTime;
		// Seconds.  How far back in time to look for a CombatEvent.  If 0, will
		//  only look for events within the last combat tick.

	Expression *pExprRequiresApply;			AST(NAME(ExprBlockRequiresApply), LATEBIND)
		// An expression that must evaluate to true in order for a maintained power to be applied.
		// If the application fails the maintained power is deactivated.

	// Limited lifetime fields

	int iCharges;
		// If the Power has a limited number of charges, this is the limit.  0 is unlimited.

	F32 fChargeRefillInterval;
		// The time it takes to regain a single charge. Power refills the charges only upto the number of charges defined.

	F32 fLifetimeReal;
		// Seconds. Real-world time the Power is allowed before expiring.  0 is unlimited.

	F32 fLifetimeGame;
		// Seconds. Logged-in game time the Power is allowed before expiring.  0 is unlimited.

	F32 fLifetimeUsage;
		// Seconds. Active time the Power is allowed before expiring.  0 is unlimited.


	// AttribMods & Combos

	int iAttribKeyBlock;
		// If non-0, the block of keys assigned to this power def.  Powers don't usually
		//  get blocks until they actually need one.  Blocks are distributed in chunks of
		//  64.

	AttribModDef **ppMods;					AST(NAME(AttribMod), SERVER_ONLY)
		// INDEXED EArray of AttribMod defs the power applies when activated.
		//  This list is sorted by the AttribMods' key.  It does not include
		//  ANY internally derived AttribMods.  This list is primarily used
		//  for editing and inheritance.
		// SERVER_ONLY since it's only used during load/generate and editing

	AttribModDef **ppOrderedMods;			AST(NAME(""), NO_INDEX, SERVER_ONLY)
		// NONINDEXED EArray of AttribMod defs the power applies when activated.
		//  This list is sorted by the AttribMods' ApplyPriority, and also includes
		//  AttribMods that are internally derived.  This list is primarily used
		//  for applying the power and for mapping the AttribMods of a character
		//  after the character is loaded from the database.

	AttribModDef **ppOrderedModsClient;		AST(NO_INDEX, NO_TEXT_SAVE)
		// NONINDEXED copy of ppOrderedMods, without any internally derived AttribMods.
		//  This is sent to the client, which then reconstitutes ppOrderedMods from it.
		//  We do this because the server does all the load/generate processing

	AttribModDef **ppSpecialModsClient;		AST(NO_INDEX, NO_TEXT_SAVE)
		// NONINDEX copy of special attribModDefs that the client needs to know about
		// in order to do powers procesing on the client

	PowerCombo **ppCombos;					AST(NAME(Combo))
		// List of sub powers and their requirements, only valid if this is a combo power

	PowerCombo **ppOrderedCombos;			AST(NAME("") NO_INDEX)
		//NONINDEXED EArray of combo powers. This list is sorted by the PowerCombos fOrder


	// Misc fields

	S32 *piAttribIgnore;					AST(NAME(AttribIgnore), SUBTABLE(AttribTypeEnum))
		// Array of attribs that this power "ignores", where the definition of "ignore"
		//  depends on the attrib in question

	S32 *piAttribDepend;					AST(SERVER_ONLY, NO_TEXT_SAVE)
		// Derived.  Array of attributes that this Power's effect may be dependent up. Used
		//  for Powers that can automatically reapply if the attributes in this list change.

	Expression *pExprEnhanceAttach;			AST(NAME(ExprBlockEnhanceAttach, ExprEnhanceBlock), REDUNDANT_STRUCT(ExprEnhance, parse_Expression_StructParam), LATEBIND)
		// An expression that defines what other powers this power can enhance.  Used to control
		//  the ability for enhancements to attach to other powers.

	Expression *pExprEnhanceApply;			AST(NAME(ExprBlockEnhanceApply, ExprEnhanceApplyBlock), LATEBIND)
		// An expression that must evaluate to true in order for the enhancement to be applied 
		// to the power.

	Expression *pExprEnhanceEntCreate;		AST(NAME(ExprEnhanceEntCreate, ExprEnhanceEntCreateBlock), LATEBIND)
		// An expression that must evaluate to true in order for the enhancement to be added to an entCreated critter

	AttribType eAttribHit;					AST(DEFAULT(-1) SUBTABLE(AttribTypeEnum))
		// The attribute that defines the ability to cause the Power to hit
		//  Per-Power version of eAttribHit in HitChanceConfig, used for DD ruleset

	AttribType eAttribMiss;					AST(DEFAULT(-1) SUBTABLE(AttribTypeEnum))
		// The attribute that defines the ability to cause the Power to miss
		//  Per-Power version of eAttribMiss in HitChanceConfig, used for DD ruleset

	REF_TO(PowerDef) hPreActivatePowerDef;	AST(NAME(PreActivatePowerDef), REFDICT(PowerDef))
		// A reference to another power def that is meant to be applied when this power becomes current

	// Art/UI fields
	
	REF_TO(PowerAnimFX) hFX;			AST(NAME(AnimFX), REFDICT(PowerAnimFX))
		// Handle to the PowerAnimFX

	F32 fHueOverride;
		// Override of the PowerAnimFX fDefaultHue

	REF_TO(PowerEmit) hEmitOverride;	AST(NAME(EmitOverride))
		// Override of the PowerAnimFX hDefaultEmit

	const char *pchIconName;			AST(POOL_STRING)
		// Name of the power's icon (for UIs)

	PowerPurpose ePurpose;				AST(NAME(Purpose))
        // The purpose of a power determines where it will appear in the 
		// UI only. It has no other effect on the way the power behaves. 

	F32 fCursorLocationTargetRadius;
		// For cursor location targeting, if this is valid, this is the range scale that will be applied to the effect

	// Message fields

	DisplayMessage msgDisplayName;			AST(STRUCT(parse_DisplayMessage))
		// Message to display to the name of the power

	DisplayMessage msgDescription;			AST(STRUCT(parse_DisplayMessage))
		// Short description for players, approximately 50 characters long

	DisplayMessage msgDescriptionLong;		AST(STRUCT(parse_DisplayMessage))
		// Long description for players, approximately 200 characters long

	DisplayMessage msgDescriptionFlavor;	AST(STRUCT(parse_DisplayMessage))
		// Flavor description for players, approximately 100 characters long

	DisplayMessage msgAttribOverride;		AST(STRUCT(parse_DisplayMessage))
		// Message which overrides the autogenerated "These attributes affect me" block in the tooltip.

	DisplayMessage msgAutoDesc;				AST(STRUCT(parse_DisplayMessage))
		// Message to use for AutoDesc of the Power's effects instead of the standard
		//  AttribMod line-item method

	DisplayMessage msgRankChange;			AST(STRUCT(parse_DisplayMessage))
		// Message to use to describe what changes from rank to rank in this power.
		// Usually, this will be something like "+10% Damage", but if Rank 3 adds a Snare,
		// Rank 3 should be a new PowerDef, with "+10% Damage & Snare".

	const char* pchRequiresQueueFailMsgKey;	AST(POOL_STRING)
		// Message to display when the RequiresQueue expression fails


	REF_TO(PowerDef) hTooltipDamagePowerDef; AST(NAME(TooltipDamagePowerDef), REFDICT(PowerDef))
		// This power is used only in tooltips to calculate the damage of the power.

	// AI utility fields

	int eAITags;							AST(NAME(AITags), FLAGS, SUBTABLE(PowerAITagsEnum), SERVER_ONLY)
		//The AI tags the Power has

	int iAIMinRange;						AST(SERVER_ONLY)
		// Minimum range the AI should use this power

	int iAIMaxRange;						AST(SERVER_ONLY)
		// Maximum range the AI should use this power

	const char *pchAIPowerConfigDef;		AST(NAME(AIPowerConfigDef), RESOURCEDICT(AIPowerConfigDef), SERVER_ONLY, POOL_STRING)
		// Tells the AI how, when, why to use this power
		// Not a REF_TO because the editor apparently can't handle it

	AIPowerConfigDef *pAIPowerConfigDefInst;	AST(NAME(AIPowerConfigDefInst), SERVER_ONLY, LATEBIND)
		// Instanced data on this structure - unset fields inherited from above reference

	Expression *pExprAICommand;				AST(NAME(ExprBlockAICommand), REDUNDANT_STRUCT(ExprAICommand, parse_Expression_StructParam), LATEBIND, SERVER_ONLY)
		// Command run by the AI when this power becomes current

	// Editing fields
		
	char *pchGroup;							AST(SERVER_ONLY, POOL_STRING)
		// Internal category of the power (used for editing, errors, etc)

	char *pchIndexTags;						AST(SERVER_ONLY, POOL_STRING)
		// Internal value used by global dictionary index

	char *pchNotes;							AST(SERVER_ONLY, CASE_SENSITIVE)
		// Developer notes (should not be binned)

	const char *pchFile;					AST(CURRENTFILE, SERVER_ONLY)
		// Current file (required for reloading)

	U32 uiVersion;							AST(SERVER_ONLY, NO_TEXT_SAVE, ADDNAMES(ForceBinningAgain))
		// The 'version' of the PowerDef.  0 is invalid.  Used to assure that structures with
		//  version-sensitive references to PowerDefs are still valid after loading from the db.
		//  Currently implemented as the max timestamp of the file or any of its inheritance parents' files.

	AttribType eCriticalChanceAttrib;		AST(DEFAULT(-1) NO_TEXT_SAVE, SERVER_ONLY)
		// derived by the first damage type attrib on this power. The attrib that is used to calculate critChance for this power
		// if -1 uses the default kAttribType_CritChance

	AttribType eCriticalSeverityAttrib;		AST(DEFAULT(-1) NO_TEXT_SAVE, SERVER_ONLY)
		// derived by the first damage type attrib on this power. The attrib that is used to calculate critSeverity for this power
		// if -1 uses the default kAttribType_CritSeverity
				
	PowerInterruption eInterrupts : PowerInterruption_NUMBITS;		AST(FLAGS, DEFAULT(21), SUBTABLE(PowerInterruptionEnum))
		// Bitfield specifying the ways in which this Power's charge or active Toggle state can be interrupted

	PowerError eError : PowerError_NUMBITS;	AST(NAME(Error), SUBTABLE(PowerErrorEnum), NO_TEXT_SAVE)
		// Error state

	bool bAffectedRequiresPerceivance;			AST(NAME(AffectedRequiresPerceivance))
		// If the affected targets are required to pass a perceivance check in order to affect the target.

	bool bTrackTarget;							AST(DEFAULT(1))
		// True if the power/caster should track the target during activation

	PowerType eType : PowerType_NUMBITS;		AST(DEFAULT(5), SUBTABLE(PowerTypeEnum))
		// Type of the power

	TargetVisibility eTargetVisibilityMain : TargetVisibility_NUMBITS;		AST(SUBTABLE(TargetVisibilityEnum))
		// Line of sight requirements for hitting the primary target

	TargetVisibility eTargetVisibilityAffected : TargetVisibility_NUMBITS;	AST(SUBTABLE(TargetVisibilityEnum))
		// Line of sight requirements for targets that can be affected by the power

	TargetTracking eTracking : TargetTracking_NUMBITS;			AST(SUBTABLE(TargetTrackingEnum))
		// The rule for tracking the main target

	EffectArea eEffectArea : EffectArea_NUMBITS;				AST(SUBTABLE(EffectAreaEnum))
		// Effect area of the power

	EffectAreaSort eEffectAreaSort : EffectAreaSort_NUMBITS;	AST(SUBTABLE(EffectAreaSortEnum))
		// Sort method for targets in the effect area

	PowerEnterCombatType eSourceEnterCombat : PowerEnterCombatType_NUMBITS; AST(SUBTABLE(PowerEnterCombatTypeEnum) ADDNAMES(SetsCombat))
		// Whether this power should always set the source in combat, never set in combat, or use default logic

	PowerRequireValidTarget eRequireValidTarget : PowerRequireValidTarget_NUMBITS; AST(SUBTABLE(PowerRequireValidTargetEnum) ADDNAMES(UnrestrictedTargeting))
		// Whether this power should use the default value (derived from CombatConfig or ControlScheme), 
		// always require a valid target, or never require a valid target

	PowerActivateRules eActivateRules : PowerActivateRules_NUMBITS; AST(NAME(ActivateRules, ActivateWhileDead) FLAGS SUBTABLE(PowerActivateRulesEnum) DEFAULT(2))
		// Determines when this power is allowed to be activated

	EnhancementAttachUnownedType eEnhancementAttachUnowned : EnhancementAttachUnownedType_NUMBITS; AST(SUBTABLE(EnhancementAttachUnownedTypeEnum))
		// If an enhancement, how the attachment works for unowned powers

	// Bitfield
		
	U32 bUnpredicted : 1;
		// True if the Power is not predicted by the client.  This has all sorts of crazy
		//  side-effects, so is not generally useful.

	U32 bActivateWhileMundane : 1;
		// True if the Power can be activated even if the Character could be in BattleForm but is not
		// TODO: This should be combined with PowerActivateRules

	U32 bOverrides : 1;
		// True if the power can override the activation of the character's current power

	U32 bInstantDeactivation : 1;
		// True if the power deactivates instantly upon request. Valid only on maintained powers
		
	U32 bRechargeDisabled : 1;
		// True if the Power does not naturally recharge after use (aka recharge takes "forever")

	U32 bRechargeRequiresHit : 1;
		// True if the Power does not trigger its recharge if it does not hit any targets

	U32 bRechargeRequiresCombat : 1;
		// True if the Power does not trigger its recharge if the owner isn't in combat

	U32 bForceRechargeOnInterrupt : 1;
		// If true, the power will be forced into recharge if it was interrupted.
	
	U32 bCooldownGlobalNotChecked : 1;
		// True if the Power can become current even while the Character has a non-zero Global Cooldown

	U32 bCooldownGlobalNotApplied : 1;
		// True if the Power doesn't apply the Global Cooldown when it becomes current

	U32 bChargesSetCooldownWhenEmpty : 1;
		//	When set, the power will set its cooldown and refill its charges when they are all expended.

	U32 bAutoReapply : 1;
		// True if the Power is a Passive or Toggle that should automatically reapply itself when
		//  a dependency (such as piAttribDepend) changes.

	U32 bDeactivationLeavesMods : 1;		AST(SERVER_ONLY)
		// True if deactivating the power allows the mods it has created to remain.  Valid only on periodic powers.
		//  Mutually exclusive with bDeactivationDisablesMods, bModsExpireWithoutPower.

	U32 bDeactivationDisablesMods : 1;		AST(SERVER_ONLY)
		// True if deactivating the Power allows the mods it has created to remain, but in a permanently disabled state.
		//  Valid only on periodic powers.  Mutually exclusive with bDeactivationLeavesMods, bModsExpireWithoutPower.

	U32 bModsExpireWithoutPower : 1;		AST(SERVER_ONLY)
		// True if the AttribMods created by the Power automatically expire if the Power no longer exists in the map,
		//  which can happen if the source leaves, or the source loses access to the Power somehow.  Mutually
		//  exclusive with bDeactivationLeavesMods, bDeactivationDisablesMods.

	U32 bAlwaysQueue : 1;
		// If the Power attempts to cancel all existing Activations before queuing itself

	U32 bDisableTargetEnterCombat : 1;
		// This power will not trigger the combat state for the target character

	U32 bHitChanceIgnore : 1;			AST(ADDNAMES(IgnoreAccuracy))
		// If the PowerDef ignores the HitChance system

	U32 bHitChanceOneTime : 1;
		// If the PowerDef is periodic and Character-effect area, the success of the HitChance roll
		//  can be locked to the first result, so the remaining periods will continue to either hit or miss.

	U32 bDoNotAutoSlot : 1;
		// Do not auto-slot this power in the tray - primarily for propagated powers or item-based powers
		// as there is already a flag on PTNodes that handles this for powers coming from your power trees

	U32 bHideInUI : 1;
		// Hide this power in the tray and powers list

	U32 bSimpleProjectileMotion : 1;	AST(ADDNAMES(AlwaysHitGround))
		// If ray casting to the target position is beyond the range of the power, 
		// then find a point in the character's horizontal facing direction

	U32 bEnhanceCopyLevel : 1;
		// Makes an Enhancement operate at the level of the Power it is attached to, rather than its own level

	U32 bEnhancePowerFields : 1;
		// for enhancement attaching to powers, use this enhancement's fields to modify the power's fields
		// right now there aren't many fields to be modified (Range, Radius), but as we increase the fields that can be enhanced
		// this may need to change to a struct of enhancement modifier info.

	U32 bAreaEffectOffsetsRootRelative : 1;
		// The area of effect offsets are relative to the root rotation of the entity. Only should be used for very special cases
		// where the root rotation is guaranteed to be facing relative to the character orientation, like on the dragon requester
		// this will also override any power activation direction

	U32 bSlottingRequired : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef must be slotted to be used

	U32 bToggleExclusive : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef is a toggle in a mutual exclusion PowerCategory

	U32 bAutoAttackServer : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef is a Click in a AutoAttackServer PowerCategory

	U32 bAutoAttackEnabler : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef is in a AutoAttackEnabler PowerCategory

	U32 bAutoAttackDisabler : 1;		AST(NO_TEXT_SAVE)
		// If the PowerDef is in a AutoAttackDisabler PowerCategory

	U32 bWeaponBased : 1;				AST(NO_TEXT_SAVE)
		// If the PowerDef is in a PowerCategory with a weapon-categorized Item attached to their Applications

	U32 bSelfOnce : 1;					AST(NO_TEXT_SAVE)
		// If the PowerDef has SelfOnce mods on it

	U32 bMissMods : 1;					AST(NO_TEXT_SAVE)
		// If the PowerDef has Miss or HitOrMiss AttribMods on it

	U32 bComboToggle : 1;				AST(NO_TEXT_SAVE)
		// If the PowerDef is a Combo and at least one of its children is a Toggle

	U32 bComboTargetRules : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef is a Combo where at least one PowerCombo has a Target expression

	U32 bApplyObjectDeath : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef has specific mods on it that need special handling if no
		//  Character is hit by an Application. Also allows the Power to be used
		//  with no target.

	U32 bLimitedUse : 1;				AST(NO_TEXT_SAVE)
		// Set to true if the PowerDef has limited Charges or Lifetimes

	U32 bEnhancementExtension : 1;		AST(NO_TEXT_SAVE ADDNAMES(EnhanceExtend))
		// True if the PowerDef has EnhancementExtension mods on it

	U32 bRequiresCooldown : 1;			AST(NO_TEXT_SAVE)
		// True if the power has a category which requires has a cool down

	U32 bSafeForSelfOnly : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef has can be used while the character can only affect self

	U32 bMultiAttribPower : 1;			AST(NO_TEXT_SAVE)
		// If the PowerDef has more than one side-effect producing AttribMod, used for AutoDesc

	U32 bModsIgnoreStrength : 1;		AST(NO_TEXT_SAVE)
		// If all the AttribMods have 0 Strength sensitivity

	U32 bHasEffectAreaOffsets : 1;		AST(NO_TEXT_SAVE)
		// if any of the yaw, pitch or left/right/up offsets are set

	U32 bHasEffectAreaPositionOffsets : 1;		AST(NO_TEXT_SAVE)
		// if any of the left/right/up offsets are set

	U32 bHasProjectileCreateAttrib : 1;	AST(NO_TEXT_SAVE)
		// if this power has any projectile create attribmods

	U32 bHasTeleportAttrib : 1;			AST(NO_TEXT_SAVE)
		// if this power has a teleport create attribmod

	U32 bHasWarpAttrib : 1;				AST(NO_TEXT_SAVE)
		// If this power has a Warp attribmod

	U32 bHasPredictedMods : 1;			AST(NO_TEXT_SAVE)
		// If this power has a Warp attribmod

	U32 bHasAttribApplyUnownedPowers : 1; AST(NO_TEXT_SAVE)
		// derived. If this power has an attrib that applies a power (expiration def, apply power attrib, etc.)

	U32 bDelayTargetingOnQueuedActivation : 1;
		// If set, before the power goes current, the client will update the targetVec 
	
	U32 bUseCameraTargetingVecTargetAssist : 1;
		// Only valid for player powers that will use camera targeting, 
		// will choose a vec target in the direction of a nearby target based on the combatconfigs

	U32 bDoNotAllowCancelBeforeHitFrame : 1;
		// if true, will not allow non-forced cancels of this power, like from the flag bAlwaysQueue
		// and will not allow the tactical manager to cancel the power before the hitframe due to crouch/aim, roll, etc

	U32 bUpdateChargeTargetOnDeactivate : 1;
		// if set, will update the target & target vec when the charging of a power deactivates

	U32 bChargeAllowIndefiniteCharging : 1;
		// if set, a charge will not activate if the max charge time has been reached. 
		// The charge time will be clamped to the power's max charge, fTimeCharge

	U32 bDisableConfuseTargeting : 1;
		// If set, this power will not do confuse targeting if the entity is confused

	U32 bActivationImmunity : 1;
		// If set, this power will grant immunity based on the combatConfig's PowerActivationImmunities, 
		// which must be defined otherwise setting this will throw an error.

	U32 bForceHideDamageFloats : 1;
		// If set, this power will never create a damage floater.

	U32 bIgnoreShieldCheck : 1;
		// if set, this will allow the power to affect shields as if it were a normal attribute

	U32 bRechargeWhileOffline : 1;
		// if set, on map load, this power's recharge time will be reduced by the length of time a character was logged out.

	U32 bCancelPreActivatePower : 1;
		// If set, the pre-activate apply power will be canceled when this power ends

	U32 bEffectAreaCentered : 1;
		// If set, Area of Effect powers will be centered on the entity's position rather than in front of their capsule

	U32 bDisallowWhileRooted : 1;
		// if set, the power activation will be disallowed while rooted

	U32 bCheckComboBeforeToggle : 1;
		// if set, active toggles don't automatically override the combo behavior for ComboToggles

	U32 bGenerateUniqueApplyID : 1;
		// used when applying the power as an unowned power, 
		// if set, a new applyID will be generated instead of using one passed in (usually from an attribMod)

	U32 bNeverAttachEnhancements : 1;
		// if set, enhancements will not be allowed to attach to the given power

} PowerDef;

// Used to reference which list in PowerAttribEnhancements is to be used. 
typedef enum EEnhancedAttribList
{
	EEnhancedAttribList_DEFAULT,
		// used for puiEnhancementIDs

	EEnhancedAttribList_EXPIRATION,
		// puiExpirationEnhancementIDs

} EEnhancedAttribList;


AUTO_STRUCT;
typedef struct PowerAttribEnhancements
{
	S32 iAttribIdx;
		// the index of the attribModDef list on the powerDef this pertains to

		// if we ever need more than maybe this should just have been an array. with EEnhancedAttribList indexing it

	U32 *puiEnhancementIDs;
		// a list of enhancements that apply to a particular generic powerDef on the attrib

	U32 *puiExpirationEnhancementIDs;			
		// a list of enhancements that apply to the ModExpiration on the AttribMod

} PowerAttribEnhancements;


// Instance of a power
AUTO_STRUCT AST_CONTAINER;
typedef struct Power
{

	// Core properties
	//  Change rarely to never

	CONST_REF_TO(PowerDef) hDef;	AST(PERSIST SUBSCRIBE REFDICT(PowerDef))
		// Handle to power def

	const U32 uiID;					AST(PERSIST SUBSCRIBE, KEY)
		// Unique ID of this power, with respect to all the powers of whatever owns it

	const U32 uiTimeCreated;		AST(PERSIST SUBSCRIBE)
		// Time in SecondsSince2000 that this was created

	const int iLevel;				AST(PERSIST SUBSCRIBE)
		// If positive, the level the power activates or applies at.  Otherwise the power
		//  activates or applies at the level of the character that owns it.


	// Customization
	//  Really should be pushed into a sub-structure allocated as needed, rather than flat

	const F32 fHue;					AST(PERSIST SUBSCRIBE)
		// Sets the hue of the Power's FX.  0 defers to the Power's PowerAnimFX's DefaultHue.

	CONST_REF_TO(PowerEmit) hEmit;	AST(PERSIST SUBSCRIBE REFDICT(PowerEmit))
		// Handle to the emit type of the Power

	const S32 iEntCreateCostume;	AST(PERSIST SUBSCRIBE)
		// Sets the costume index for any EntCreate AttribMods made by this Power.
		//  The default of 0 means no customization has taken place, and the default behavior
		//  should be used (which may be a random choice, or may be a specific index/behavior).

	// Usage tracking
	//  Change never unless the Power actually has usage limitations, in which case they change frequently
	//  Really should be pushed into a sub-structure allocated as needed, rather than flat

	int iChargesUsed;				AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// Charges used. If the PowerDef specifies non-zero charges and the Power has used
		//  that many, the Power can not be used (and potentially should be cleaned up by its owner).

	const int iChargesUsedTransact;	AST(PERSIST SUBSCRIBE)
		// Charges used. Used for powers that require transacting this field. 
		// Currently only used on items.


	F32 fLifetimeGameUsed;			AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Game lifetime used.  Only tracked if the PowerDef actually cares.

	F32 fLifetimeUsageUsed;			AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Active lifetime used.  Only tracked if the PowerDef actually cares.



	// Persisted data that shouldn't be transacted, but is marked to transact so it gets into transactions

	F32 fTimeRecharge;				AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Time until the power has recharged
		//  Changes with extreme frequency, so client keeps its own copy up to date, and changing it on the server
		//  doesn't trigger setting a DirtyBit.
		// The server sends this data down in a large set during login, as well as on-demand

	F32 fTimeChargeRefill;			AST(PERSIST SOMETIMES_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Time until the power gains another charge
		//  Changes with extreme frequency, so client keeps its own copy up to date, and changing it on the server
		//  doesn't trigger setting a DirtyBit.
		// The server sends this data down in a large set during login, as well as on-demand

	F32 fTimeRechargeBase;			NO_AST
		// the recharge time that was last used so we can correctly get powerslot sweeps

	// Non-persisted data, must be able to be rebuilt from scratch frequently
	
	Power *pParentPower;			NO_AST // Struct sending can't handle backpointers
		// Pointer to parent combo power, if this is a sub power
	
	Power **ppSubPowers;			AST(NO_INDEX)
		// List of sub powers, if this is a combo power
		//  Changes never, though substructure data may change
				
	Power *pCombatPowerStateParent;	NO_AST
		// the parent if the power is related to a combat power state 
		
	Power **ppSubCombatStatePowers;	
		// List of sub combat state powers
		//  Changes never, though substructure data may change
		
	const char *pchCombatPowersState;		NO_AST
		// pooled string. If a combat power state derived power, the state this power is valid in

	int iLevelAdjustment;			AST(NO_NETSEND)
		// When the iLevel is being adjusted for some reason, this is the total adjustment.  Only applied in
		//  cases where the Power has a non-zero iLevel.
		//  Changes rarely - set during character_ResetPowersArray() on client and server
		
	F32 fYaw;						AST(NO_NETSEND)
		// Yaw of the Power, relative to the current yaw of the source
		//  Changes never - set during character_ResetPowersArray() on client and server

	U32 uiReplacementID;			AST(NO_NETSEND)
		// ID of power that should be executed instead of this power
		//  Changes rarely - set during character_ResetPowersArray() on client and server
	U32 uiSrcEquipSlot;				AST(NO_NETSEND)
		//the slot of the equip bag that this power came from. Needed for dual-wielding enhancements.
		//  Changes rarely - set during character_ResetPowersArray() on client and server
	U32 uiPowerSlotReplacementID;	AST(NO_NETSEND)
		// ID of Power that we check PowerSlotting for, instead of this Power
		//  Changes never - set during character_ResetPowersArray() on client and server
	
	F32 fTableScale;				AST(DEFAULT(1))
		// Scalar applied to TableDefault lookups (for duration and magnitude)
		//  Changes never

	S32 iIdxMultiTable;				NO_AST
		// Multi-table index used by this power, set as needed by whatever creates/owns the power

	PowerSource eSource;			NO_AST
		// System that owns the Power

	Item *pSourceItem;				NO_AST
		// If owned by an item, this is the cached pointer

		// note: this should probably be NO_NETSEND 
	U32 *puiEnhancementIDs;
		// If this is an Enhancement, this is the list of Powers it is enhancing.  If
		//  it's not an Enhancement, this is the list of Powers enhancing it.
		// Currently calculated only on the server, though I'm not sure why.
		// Changes rarely - generally only during a character_ResetPowersArray()
	
	PowerAttribEnhancements **ppAttribEnhancements;	AST(SERVER_ONLY)
		// cache of which enhancements attach to attribute power triggers (unowned power applications) will have enhancements attached to them
	
	F32 fCachedAreaOfEffectExprValue;	NO_AST
		// currently used for FX. this is for the area of effect parameters that are expressions
		// cached so we don't have to recall the expression. 
		// the only two we care bout are arc & radius, and they aren't used at the same time

	// Powers enhancements
	//  The following are Enhanced fields from attached enhancements and are precalculated 
	//- they are set during character_ResetPowersArray()

	F32	fEnhancedRange;					AST(NO_NETSEND)
		// enhanced by a power to increase the range of this power
	
	F32 fEnhancedRadius;
		// enhanced by a power to increase the radius of this power

	
	// Bitfield

	const bool bHideInUI : 1;			AST(PERSIST SUBSCRIBE)
		// Should we show this Power in the UI?
		//  Currently only set on Powers granted by Items through an ItemPowerDef
		//  which has an active handle to a PowerReplaceDef...

	bool bActive : 1;					AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// If the Power is currently active or not
		//  Changes with extreme frequency, client keeps its own copy

	bool bIsReplacing : 1;				NO_AST
		// If the Power is replacing another power

	U32 bNeedsResetCache : 1;			NO_AST
		// Set to true when the Power is added to a Character, set to false when
		//  a PowerResetCache is found or created

	U32 bTempSourceIsPowerTree : 1;		NO_AST
		// Set to true when the Power is specifically from Temporary PowerTrees, as
		//  opposed to some other temp source.  If any more temp sources are added
		//  we should make this an enum.

	U32 bExpirationPending : 1;			NO_AST
		// This is set when the power has already called character_PowerExpire and
		//  is waiting on a transaction to complete
		
} Power;


// Structure for referring to a Power in as stable and persistable way as possible
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct PowerRef
{
	U32 uiID;				AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// ID of the Power

	S32 iIdxSub;			AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// The index into ppSubPowers if this is tracking a child Power

	U16 uiSrcEquipSlot;
		//which equip slot this power came from

	S16 iLinkedSub;
		// the index into ppSubCombatStatePowers if this is tracking a power from a combatStatePower

	REF_TO(PowerDef) hdef;	AST(PERSIST NO_TRANSACT SUBSCRIBE)
		// Handle to the def of the Power

} PowerRef;

AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct CooldownTimer
{
	S32 iPowerCategory;			AST(KEY PERSIST NO_TRANSACT)
		//Relates to the power category enum

	F32 fCooldown;				AST(PERSIST, NO_TRANSACT)
		// Cooldown time remaining

	char *pchCategory;			AST(PERSIST, NO_TRANSACT)
		// String for the enum used for reloading
}CooldownTimer;

// Creates and returns a PowerRef for the Power
SA_RET_NN_VALID PowerRef *power_CreateRef(SA_PARAM_OP_VALID Power *ppow);

// Frees the PowerRef
void powerref_Destroy(SA_PARAM_OP_VALID PowerRef *ppowref);

// Frees the PowerTracker and sets its pointer to NULL
void powerref_DestroySafe(SA_PARAM_NN_OP_VALID PowerRef **pppowref);

// Sets the PowerRef to refer to the Power
void powerref_Set(SA_PARAM_NN_VALID PowerRef *ppowref, SA_PARAM_OP_VALID Power *ppow);

// Searches an earray of PowerRefs for the Power, returns the index or -1
int power_FindInRefs(SA_PARAM_NN_VALID Power *ppow, SA_PARAM_NN_VALID PowerRef ***pppPowerRefs);


// Returns the translated display name of the PowerDef, if it exists, otherwise it returns the internal name
SA_RET_NN_STR const char *powerdef_GetLocalName(SA_PARAM_NN_VALID PowerDef *pdef);

// Returns the translated display name of the Power's PowerDef, if it exists, otherwise it returns the internal name
SA_RET_OP_STR const char *power_GetLocalName(SA_PARAM_NN_VALID Power *ppow);


void powers_Load();

// Validates the input PowerDef.  Also optionally includes warnings.
S32 powerdef_Validate(SA_PARAM_NN_VALID PowerDef *pdef, S32 bWarnings);

// Validates the references of the input PowerDef.  This must be called after all data has been loaded, so all
//  references should be filled in.  Used to check references that would be circular during load-time, such as
//  CritterDefs.
S32 powerdef_ValidateReferences(int iPartitionIdx, SA_PARAM_NN_VALID PowerDef *pdef);

// Sorts various arrays on the PowerDef, done on load and pre-save to keep the data clean.
void powerdef_SortArrays(SA_PARAM_NN_VALID PowerDef *pdef);



F32 powerdef_GetLastApplyPriority(PowerDef *pdef);

// Returns the next valid key for an attrib mod of the power def.  Optionally pass in a
//  key this function just returned to speed up the search process.
int powerdef_GetNextAttribKey(PowerDef *pdef, int iPreviousKey);

// Goes through all the attrib mod defs of the power and gives ones without a key
//  a valid key
void powerdef_FillMissingAttribKeys(PowerDef *pdef);

int powerdef_GetNextComboKey(PowerDef *pdef, int iPreviousKey);

void powerdef_FillMissingComboKeys(PowerDef *pdef);




// Cooldown

CooldownTimer *cooldowntimer_Create();

void cooldowntimer_Free(CooldownTimer *pTimer);

// Utility function for setting a specific cooldown time of a specific category,
// with proper notification to the client.
void character_CategorySetCooldown(int iPartitionIdx, Character *pchar, S32 iPowerCategory, F32 fTime);

void power_SetCooldownDefault(int iPartitionIdx, Character *pchar, Power *ppow);

F32 power_GetCooldown(Power *ppow);

F32 powerdef_GetCooldown(PowerDef *pDef);

F32 character_GetCooldownFromPower(Character *pChar, Power *ppow);

F32 character_GetCooldownFromPowerDef(Character *pChar, PowerDef *pDef);

// Saves the cooldown data to a special persistent field
void character_SaveCooldowns(SA_PARAM_NN_VALID Character *pchar);

CooldownTimer *character_GetCooldownTimerForCategory(Character *pChar, S32 iPowerCategory);

// Loads all of the cooldown timers from the saved information. Should only be called 
// after a load
void character_LoadFixCooldowns(int iPartitionIdx, Character *pchar);

// Recharge

// Utility function for setting a specific recharge on a specific Power ID (0 means all Powers), with
//  proper notification to the client.
void character_PowerSetRecharge(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, U32 uiID, F32 fTime);

// Sets the Power to recharge in its default recharge time.  Also takes a flag to indicate if the
//  Power happened to Miss all of its targets, which may trigger slightly different behavior thanks
//  to the bRechargeRequiresHit flag.
void power_SetRechargeDefault(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow, S32 bMissedAllTargets);

// Sets the Power to recharge in the given time
void power_SetRecharge(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow, F32 fTime);

// Gets the PowerDef's default recharge time.  If the PowerDef is a Combo it
//  returns the largest default recharge time of its children.  If recharge is
//  disabled it returns POWERS_FOREVER.
F32 powerdef_GetRechargeDefault(SA_PARAM_NN_VALID PowerDef *pdef);

// Gets the Power's current recharge time remaining
F32 power_GetRecharge(SA_PARAM_NN_VALID Power *ppow);

// returns the effective basic aspect of kAttribType_SpeedRecharge for the given power.
// this will check if the powerDef wants to ignore kAttribType_SpeedRecharge 
F32 power_GetSpeedRecharge(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, 
							SA_PARAM_NN_VALID Power *ppow, SA_PARAM_OP_VALID PowerDef *pDef);

// Gets the Power's current recharge time remaining, given the Character's recharge speed with respect to that Power
F32 character_GetPowerRechargeEffective(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow);

F32 character_GetPowerRechargeBaseEffective(int iPartitionIdx, Character *pchar, Power *ppow);

// Charge

// Gets the time remaining until the power gains another charge
F32 power_GetChargeRefillTime(Power *ppow);

// Returns the number of charges used for the given Power
int power_GetChargesUsed(Power *ppow);

// Returns the number of charges left for the given Power, or -1 if unlimited
int power_GetChargesLeft(SA_PARAM_NN_VALID Power *ppow);

void power_SetChargesUsed(	SA_PARAM_NN_VALID Character *pchar, 
							SA_PARAM_NN_VALID Power *ppow, 
							SA_PARAM_OP_VALID PowerDef *pDef, 
							int iChargesUsed);


#ifdef GAMESERVER

// Restarts the timers for any power which needs to refill its charges
void character_LoadTimeChargeRefillFixup(int iPartitionIdx, Character *pchar);

#endif

// Active

// Set the Power's active status.  Also sets the active status of the parent.
void power_SetActive(SA_PARAM_NN_VALID Power *ppow, int bActive);



PowerDef *powerdef_Find(SA_PARAM_NN_STR const char *pchName);



// Allocates a Power, sets the def reference from the name
SA_RET_NN_VALID Power *power_Create(const char *pchName);

// Destroys the Power.  Optionally takes the Character it's on to make sure it's not left behind.
void power_Destroy(SA_PARAM_OP_VALID Power *ppow, SA_PARAM_OP_VALID Character *pchar);

// Copies out the Power's ID and sub-Power idx, if it's a sub-Power (-1 if it's not)
//  Set to 0,-1 if the Power is NULL
void power_GetIDAndSubIdx(SA_PARAM_OP_VALID Power *ppow, SA_PARAM_NN_VALID U32 *puiIDOut, SA_PARAM_NN_VALID int *piSubIdxOut, SA_PARAM_NN_VALID S16 *piLinkedSubIdxOut);


// Set the Power to be replaced by uiReplacementID when executed.  Set to 0 to turn off replacement.
//  Fails and returns false in the case that a non-0 replacement is set on a Power that already has a 
//  replacement.
int power_SetPowerReplacementID(SA_PARAM_NN_VALID Power *ppow, U32 uiReplacementID);

void power_ResetCachedEnhancementFields(SA_PARAM_NN_VALID Power *ppow);


// Lifetime limitations

// Returns the number of seconds of real-world time left for the given Power, or -1 if unlimited
F32 power_GetLifetimeRealLeft(SA_PARAM_NN_VALID Power *ppow);

// Returns the number of seconds of logged-in game time left for the given Power, or -1 if unlimited
F32 power_GetLifetimeGameLeft(SA_PARAM_NN_VALID Power *ppow);

// Returns the number of seconds of active time left for the given Power, or -1 if unlimited
F32 power_GetLifetimeUsageLeft(SA_PARAM_NN_VALID Power *ppow);

// Returns true if the given Power should be expired due to usage limitations
//  or lifetimes. The check will only include charges if bIncludeCharges is set to true
int power_IsExpired(SA_PARAM_NN_VALID Power *ppow, bool bIncludeCharges);


// Misc utility

Power* power_GetBasePower(SA_PARAM_NN_VALID Power *pPower);

// gets a power's range, use this to get the true range of a power 
// as it could be enhanced via an attached enhancement
F32 power_GetRange(SA_PARAM_OP_VALID Power *pPower, SA_PARAM_NN_VALID PowerDef *pDef);

// gets a power's radius, evaluating the PowerDef's pExprRadius expression
// use this to get the true radius of a power as it could be enhanced via an attached enhancement. 
F32 power_GetRadius(int iPartitionIdx, SA_PARAM_OP_VALID Character *pChar, SA_PARAM_OP_VALID Power *pPower, SA_PARAM_NN_VALID PowerDef *pDef, 
					SA_PARAM_OP_VALID Character *pcharTarget, SA_PARAM_OP_VALID PowerApplication *papp);

// Debug version of POWERLEVEL macro, makes sure the return value is >=1 and generates a detailed
//  error with callstack if it wouldn't have been.
S32 power_PowerLevelDebug(SA_PARAM_OP_VALID Power *ppow, S32 iDefault);

// Returns the default hue of the PowerDef, including its PowerAnimFX if necessary
F32 powerdef_GetHue(SA_PARAM_NN_VALID PowerDef *pdef);

// Returns the PowerEmit of the Power, using its PowerDef and PowerAnimFX if necessary
SA_RET_OP_VALID PowerEmit *power_GetEmit(SA_PARAM_NN_VALID Power *ppow, SA_PARAM_OP_VALID Character *pchar);

// Returns if the PowerDef can have its PowerEmit customized by the player
S32 powerdef_EmitCustomizable(SA_PARAM_NN_VALID PowerDef *pdef);

// Returns the first AttribModDef for the PowerDef (or its children) that can
//  have its EntCreateCostume customized by the player
SA_RET_OP_VALID AttribModDef* powerdef_EntCreateCostumeCustomizable(SA_PARAM_NN_VALID PowerDef *pdef);

// Gets the teleport attribMod for a power if it has one
SA_RET_OP_VALID AttribModDef* powerdef_GetTeleportAttribMod(SA_PARAM_NN_VALID PowerDef *pdef);

// Gets the warp AttribMod for a power if it has one
SA_RET_OP_VALID AttribModDef* powerdef_GetWarpAttribMod(SA_PARAM_NN_VALID PowerDef *pdef, bool bUseSpecialModArray, AttribModDef *pLastAttrib);

// If this power is enabled due to power modes, return the time remaining
// else return 0
// If bSelfOnly is true, only check attribs that target 'Self'
F32 character_GetModeEnabledTime(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow, bool bSelfOnly);



// Get costume impact from powers
void powers_GetPowerCostumeDataToShow(Entity *pEnt, CostumeDisplayData ***peaData, CostumeDisplayData ***peaMountData);


// Checks the validity of a character's powers, and fixes them if requested
bool character_CheckPowerValidity(Character *p, bool bFix);

// Returns Group.Name
char *powerdef_NameFull(PowerDef *pdef);

// Fills in the subpowers of a power.  Won't recreate them if the power already has
//  the correct ones.  Will still fix them up.
void power_CreateSubPowers(SA_PARAM_NN_VALID Power *ppow);

// Simple function to fix backpointers to parent power and PowerSource
void power_FixSubPowers(SA_PARAM_NN_VALID Power *ppow);

// Helper function to destroy the sub powers array on a power
void power_DestroySubPowers(Power *ppow);

PowerDef *power_doesActivate(char *pchPowerName);
bool power_DefDoesActivate(const PowerDef *pDef);

//For building groups
void PEBuildGroupedPowerDefs();

// Returns true if the PowerDef ignores the given attrib
S32 powerdef_IgnoresAttrib(SA_PARAM_NN_VALID PowerDef *pdef, AttribType eType);

// Returns the lowest preferred tray for the PowerDef, which is based on its categories.  If there
//  is no preference, returns 0.
S32 powerdef_GetPreferredTray(SA_PARAM_NN_VALID PowerDef *pdef);

// Returns the category sort ID for this PowerDef. It'll use the highest category ID in case of conflicts
S32 powerdef_GetCategorySortID(SA_PARAM_NN_VALID PowerDef *pdef);

// Figures out which WorldRegionType the PowerDef will likely activate in
WorldRegionType powerdef_GetBestRegionType(SA_PARAM_NN_VALID PowerDef *pDef);

void character_FindAllPowersInItems(Entity *pEntity, Power ***pppPowerOut);
void character_FindAllPowersInPowerTrees(Character *pChar, Power ***pppPowerOut);
void character_FindAllNodesInPowerTrees(Character *pChar, PTNode ***pppNodeOut, bool bUseEscrow);

// How many powers (in power tree) does this ent have with this purpose and category?
S32 ent_GetNumPowersWithCatAndPurpose(Entity *pEnt, S32 category, PowerPurpose purpose);

// enty with look-ups from strings to check for power cat and purpose
S32 PowersUI_GetNumPowersWithCatAndPurposeInternal(Entity *pEntity, char *pcCategory, char *pcPurpose);

// Compares two powerdefs by their power purpose
S32 ComparePowerDefsByPurpose(const PowerDef** a, const PowerDef** b);

bool ent_canTakePower(Entity *pEntity, PowerDef *pDef);

// Returns highest sort ID for categories.
S32 GetHighestCategorySortID(void);

bool powerdef_ignorePitch(PowerDef *pdef);

// returns true if the powerdef has one of the given power categories
bool powerdef_hasCategory(SA_PARAM_NN_VALID PowerDef *pdef, S32 *piPowerCategories);

bool character_HasRequiredAnyTacticalModes(Character *pchar, PowerDef *pdef);

int powerddef_ShouldDelayTargeting(PowerDef *pDef);

bool character_IsQueuingDisabledForPower(Character *pchar, PowerDef *pdef);

bool character_PowerRequiresValidTarget(Character *pchar, PowerDef *pdef);

void power_RefreshCachedAreaOfEffectExprValue(int iPartitionIdx, Character *pChar, Power *pPower, PowerDef *pPowerDef);

#endif
