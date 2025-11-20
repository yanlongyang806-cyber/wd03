/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWERSENUMS_H__
#define POWERSENUMS_H__

#include "structDefines.h"

// PowerError defines a simple error state for a Powers-system object
AUTO_ENUM;
typedef enum PowerError
{
	kPowerError_Valid = 0,
	// Perfectly valid

	kPowerError_Warning = 1 << 0,
	// Minor issues/warnings

	kPowerError_Error = 1 << 1,
	// Serious errors

	kPowerError_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} PowerError;

#define PowerError_NUMBITS 16
STATIC_ASSERT(kPowerError_MAX <= (1 << (PowerError_NUMBITS - 2)) + 1);

// PowerSource defines the system that owns the Power of a Character
AUTO_ENUM;
typedef enum PowerSource
{
	kPowerSource_Unset = 0,
		// The Character doesn't know how it owns the Power 

	kPowerSource_Personal,
		// The Character owns the Power directly

	kPowerSource_Class,
		// The Character owns the Power because of its Class

	kPowerSource_PowerTree,
		// The Character received the Power from a PowerTree

	kPowerSource_Item,
		// The Character received the Power from an Item

	kPowerSource_AttribMod,
		// The Character received the Power from an AttribMod (which ultimately came
		//  from some other source)
	
	kPowerSource_Propagation,
		// The Character received the power from another character owned by the master entity

	kPowerSource_Species,
		// The Character owns the Power because of its Species

	kPowerSource_Temporary,
		// Added from the temporary power list
} PowerSource;

// PowerInterruption defines ways in which a charging Power or active Toggle Power can be interrupted
AUTO_ENUM;
typedef enum PowerInterruption
{

	kPowerInterruption_Requested = 1 << 0,
		// Interruption is allowed by request

	kPowerInterruption_Movement = 1 << 1,
		// Interruption by any kind of movement

	kPowerInterruption_Knock = 1 << 2,
		// Interruption by knocked movement

	kPowerInterruption_Damage = 1 << 3,
		// Interruption by damage

	kPowerInterruption_InterruptAttrib = 1 << 4,
		// Interruption by an AttribMod of the Interrupt Attrib

	kPowerInterruption_Interact = 1 << 5,
		// Interruption by ANY interaction -- always comes with one of the next two

	kPowerInterruption_ContactInteract = 1 << 6,
		// Interruption by an interaction with a contact

	kPowerInterruption_NonContactInteract = 1 << 7,
		// Interruption by an interaction with something other than a contact

	kPowerInterruption_Count, EIGNORE

	kPowerInterruption_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below
} PowerInterruption;

#define PowerInterruption_NUMBITS 16
STATIC_ASSERT(kPowerInterruption_MAX <= (1 << (PowerInterruption_NUMBITS - 2)) + 1);

// Defines various flags on various kinds of events that the combat system tells users about
AUTO_ENUM;
typedef enum CombatTrackerFlag
{
	kCombatTrackerFlag_Immune = 1 << 0,
		// The event involved immunity

	kCombatTrackerFlag_Critical = 1 << 1,
		// The event involved a critical hit

	kCombatTrackerFlag_Dodge = 1 << 2,
		// The event involved a dodge

	kCombatTrackerFlag_Block = 1 << 3,
		// The event involved a block

	kCombatTrackerFlag_Flank = 1 << 4,
		// The event involved a flank

	kCombatTrackerFlag_Exploit = 1 << 5,
		// The event involved an exploit

	kCombatTrackerFlag_Miss = 1 << 6,
		// The event involved a miss

	kCombatTrackerFlag_ShowPowerDisplayName = 1 << 7,
		// the event came from an attrib that wants a floater to display the name of the power it came from

	kCombatTrackerFlag_LASTFLOATER = kCombatTrackerFlag_ShowPowerDisplayName,		EIGNORE
		// Last flag that is converted to a floater in the UI

	kCombatTrackerFlag_Kill = 1 << 8,
		// The event involved a kill shot

	kCombatTrackerFlag_Pseudo = 1 << 9,
		// The event was false in some way (typically damage that wasn't actually dealt)

	kCombatTrackerFlag_ShowSpecial = 1 << 10,
		// The event was flagged as some special damage type the client might care extra about because it's special 
	
	kCombatTrackerFlag_NoFloater = 1 << 11,
		//This event should show up in the combat log but NOT display a floater.
	
	kCombatTrackerFlag_SpecialMiss = 1 << 12,
		// This event was flagged as being a miss that would not have happened without a certain effect

	kCombatTrackerFlag_ReactiveDodge = 1 << 13,
		// Tagged on attribModDefs in the editor, flag for the client to know that a reactive dodge was apart of the event

	kCombatTrackerFlag_ReactiveBlock = 1 << 14,
		// Tagged on attribModDefs in the editor, flag for the client to know that a reactive block was apart of the event
			
	kCombatTrackerFlag_LAST = kCombatTrackerFlag_SpecialMiss,		EIGNORE
		// Last valid flag

	kCombatTrackerFlag_MAX, EIGNORE // Leave this last, not a valid flag, just for the compile time assert below

} CombatTrackerFlag;

#define CombatTrackerFlag_NUMBITS 16
STATIC_ASSERT(kCombatTrackerFlag_MAX <= (1 << (CombatTrackerFlag_NUMBITS - 2)) + 1);

// Detail levels.  0 should be default, with everything else relative to that.
// Here instead of PowersAutoDesc.h so that other projects can see it easily.
AUTO_ENUM;
typedef enum AutoDescDetail
{
	kAutoDescDetail_Minimum = -1,
	kAutoDescDetail_Normal = 0,
	kAutoDescDetail_Maximum = 1,
} AutoDescDetail;


// DefineContext for data-defined ModStackGroups
extern DefineContext *g_pModStackGroups;

// A ModStackGroup groups AttribMods of the same Attrib and Aspect into sets in which only the "best"
//  is active at any given moment. The names and number of ModStackGroups is data-defined.
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pModStackGroups);
typedef enum ModStackGroup
{
	kModStackGroup_None,
		// Not in a StackGroup.

	kModStackGroup_Generic,
		// A default StackGroup provided for convenience.

	kModStackGroup_CODEMAX = kModStackGroup_Generic,
		// Last code-defined ModStackGroup

} ModStackGroup;

#define ModStackGroup_NUMBITS 8


// PowerPurpose only affects the UI. It broadly categorizes
//  Powers or PowerTreeNodes into groups that make sense to
//  the end user, such as "Offense", "Defense" or "Travel".
// Loading is handled in standard Powers loading code.
extern DefineContext *g_pPowerPurposes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pPowerPurposes);
typedef enum PowerPurpose
{
	kPowerPurpose_None = -1, ENAMES(None)
	kPowerPurpose_Uncategorized = 0, ENAMES(Uncategorized)
		// The current power is uncategorized and will appear in an 
		// "uncategorized" bucket in the UI
} PowerPurpose;


// Various ways in which an AttribMod can control how responsive
//  it is to various game mechanics.  Sits here instead of
//  CombatSensitivity.h for convenience.
AUTO_ENUM;
typedef enum SensitivityType
{
	kSensitivityType_Strength,
		// Adjust the sensitivity to Strength aspect effects

	kSensitivityType_Resistance,
		// Adjust the sensitivity to Resistance aspect effects

	kSensitivityType_Immune,
		// Adjust the sensitivity to Immune aspect effects. This is all or nothing

	kSensitivityType_AttribCurve,
		// Adjust the sensitivity to the AttribCurve system

	kSensitivityType_Avoidance,
		// Adjust the sensitivity to the avoidance system

	kSensitivityType_CombatMod,
		// Adjust the sensitivity to level-based combat effects

	kSensitivityType_Shield,
		// Adjust the sensitivity to the Shield Attribute

} SensitivityType;


// these are shared flags 
typedef enum EPowerFXFlags
{
	EPowerFXFlags_MISS = (1<<0),
	EPowerFXFlags_TRIGGER = (1<<1),
	EPowerFXFlags_TRIGGER_IS_ENTITY_ID = (1<<2),
	EPowerFXFlags_TRIGGER_MULTI_HIT = (1<<3),
	EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE = (1<<4),
	EPowerFXFlags_FROM_SOURCE_VEC = (1<<5),
	EPowerFXFlags_DO_NOT_TRACK_FLASHED = (1<<6),

	PowerFXFlags_NUM_BITS = 7,
} PowerFXFlags;

// inherits EPowerFXFlags
typedef enum EPMFXStartFlags
{
	EPMFXStartFlags_FLASH				= (1<<(PowerFXFlags_NUM_BITS+0)),
	EPMFXStartFlags_USE_TARGET_NODE		= (1<<(PowerFXFlags_NUM_BITS+1)),
} EPMFXStartFlags;

// inherits EPowerFXFlags
// these are a super-set of the powersMovement FXStart flags
typedef enum EPowerAnimFXFlag
{
	EPowerAnimFXFlag_ADD_POWER_AREA_EFFECT_PARAMS	= (1<<(PowerFXFlags_NUM_BITS+0)),
	EPowerAnimFXFlag_GLOBAL							= (1<<(PowerFXFlags_NUM_BITS+1)),

} EPowerAnimFXFlag;

#define EPowerAnimFXFlag_To_EPMFXStartFlags(x)	((x)&((1<<PowerFXFlags_NUM_BITS)-1))

// Disable certain tactical movement types
AUTO_ENUM;
typedef enum TacticalDisableFlags {
	TDF_NONE				= 0,
	TDF_ROLL				= BIT(0),
	TDF_AIM					= BIT(1),
	TDF_SPRINT				= BIT(2),
	TDF_CROUCH				= BIT(3),
	TDF_QUEUE				= BIT(4),	EIGNORE
	TDF_ALL					= BIT(5)-1, EIGNORE
} TacticalDisableFlags;

AUTO_ENUM;
typedef enum EPowerDebugFlags
{
	EPowerDebugFlags_ACTIVATE	= 1 << 0,		ENAMES(activate)
	EPowerDebugFlags_APPLY		= 1 << 1,		ENAMES(apply)
	EPowerDebugFlags_POWERS		= 1 << 2,		ENAMES(powers)
	EPowerDebugFlags_UTILITY	= 1 << 3,		ENAMES(utility)
	EPowerDebugFlags_REACTIVE	= 1 << 4,		ENAMES(reactive)
	EPowerDebugFlags_ANIMFX		= 1 << 5,		ENAMES(animfx)
	EPowerDebugFlags_RECHARGE	= 1 << 6,		ENAMES(recharge)
	EPowerDebugFlags_DEATHPREDICT	= 1 << 7,	ENAMES(deathpredict)
	EPowerDebugFlags_ENHANCEMENT	= 1 << 8,	ENAMES(enhancement)
	EPowerDebugFlags_ROOT			= 1 << 9,	ENAMES(root)
	EPowerDebugFlags_PROJECTILE		= 1 << 10,	ENAMES(projectile)
		
	EPowerDebugFlags_ALL		= 0x7FFFFFFF,	ENAMES(all)
} EPowerDebugFlags;

#endif
