/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef DAMAGETRACKER_H
#define DAMAGETRACKER_H
#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "CharacterAttribsMinimal.h"
#include "PowersEnums.h"

// Forward declarations
typedef struct Character	Character;
typedef struct Entity		Entity;
typedef struct Message		Message;
typedef struct PowerDef		PowerDef;


typedef struct DamageTracker
{
	F32 fDamage;
		// Actual damage dealt

	F32 fDamageNoResist;
		// Damage dealt before factoring in resistance

	U32 eDamageType;
		// Type of damage, currently used before accumulation so we can handle immunity

	U32 uiApplyID;
		// ApplyID of the AttribMod causing the damage, currently used before 
		// accumulation to handle special fragile AttribMods

	REF_TO(PowerDef) hPower;
		// The source PowerDef, currently used to trigger death bits/fx and find the AttribModDef

	U32 uiDefIdx;
		// The index of the source AttribModDef

	EntityRef erOwner;
		// The owner of the damage

	EntityRef erSource;
		// The source of the damage (usually the same as the owner)

	EntityRef erTarget;
		// The target entity

	U32 uiTimestamp;
		// timeSecondsSince2000 at which the damage was dealt
		//  Accumulated: last uiTimestamp seen
	
	S32 iLevel;
		// Level at which the damage was dealt
		//  Accumulated: highest iLevel seen

	U32 teamup_id;			// Not dealing with teams at all right now

	CombatTrackerFlag eFlags;
		// Extra data about the damage

	char *pchDisplayNameKey;
		// Key to use for a display name message, instead of PowerDef's display name

	U32 *puiFragileTypes; //To damage only a specific type of fragile attrib mod

	U8 bDecay : 1;
		// Handy flag for decay system, set temporarily

} DamageTracker;

// Struct for sending combat-related events
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct CombatTrackerNet
{
	EntityRef erOwner;
		// The owner entity

	EntityRef erSource;
		// The source entity, if different from the owner

	F32 fMagnitude;
		// The magnitude of the event.  0 and negative values are allowed and may have special meaning

	F32 fMagnitudeBase;
		// The base magnitude of the event, if different from the actual magnitude
		//  Typically this is the pre-mitigation magnitude (such as pre-resist damage vs actual damage)

	AttribType eType;
		// The Attrib involved in the event (when relevant)

	REF_TO(Message) hDisplayName;
		// Ref to display name of the Power (or perhaps the environmental source, for those cases)

	REF_TO(Message) hSecondaryDisplayName;
		// Ref to display name of a secondary power, for example shields absorbing part of the damage.

	CombatTrackerFlag eFlags;	AST(FLAGS)
		// Extra data about this event

	F32 fDelay;					AST(SERVER_ONLY)
		// The Delay before sending this to the client

	PowerDef *pPowerDef;		AST(UNOWNED)
		// Pointer to PowerDef, only used on the server to fully inform CombatEvent tracking

	U32 powID;
		// the power ID on the owner, if applicable

	U8 bAINotifyMiss : 1;		AST(SERVER_ONLY)
		// Flag to indicate that when this is actually added to the live list it should trigger
		//  an AINotify indicating a miss.

} CombatTrackerNet;

	//Very simple for now, just attribs that we don't want to show to the client.
AUTO_STRUCT;
typedef struct CombatLogClientFilter
{
	AttribType eAttrib;
} CombatLogClientFilter;

AUTO_STRUCT;
typedef struct CombatLogClientFilters
{
	CombatLogClientFilter **ppFilters;	AST(NAME(CombatLogClientFilter))
} CombatLogClientFilters;

// Destroys an existing tracker
void damageTrackerDestroy(SA_PRE_NN_VALID SA_POST_FREE DamageTracker *pTracker);

// Adds immediate damage when we know the attacker and damage type
SA_ORET_NN_VALID DamageTracker *damageTracker_AddTick(int iPartitionIdx,
												 SA_PARAM_NN_VALID Character *pchar,
												 EntityRef erOwner,
												 EntityRef erSource,
												 EntityRef erTarget,
												 F32 fDamage,
												 F32 fDamageNoResist,
												 U32 eDamageType,
												 U32 uiApplyID,
												 SA_PARAM_OP_VALID PowerDef *ppow,
												 U32 uiDefIdx,
												 SA_PARAM_OP_VALID U32 *puiFragileTypes,
												 CombatTrackerFlag eFlags);

// This is called to remove damage of the type from the incoming tick list
void damageTracker_Immunity(SA_PARAM_NN_VALID Character *pchar, U32 eDamageType);

// Free and clear the damage trackers
void damageTracker_ClearAll(SA_PARAM_NN_VALID Character *pchar);

// Accumulate a single DamageTracker from a tick into the Character's longterm DamageTracker data
void character_DamageTrackerAccum(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID DamageTracker *pTracker, F32 fCredit);

// Accumulate the basic data for a damage event into the Character's longterm DamageTracker data
void character_DamageTrackerAccumEvent(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, EntityRef erOwner, F32 fDamage);

// Decay the Character's longterm damage trackers
void character_DamageTrackerDecay(SA_PARAM_NN_VALID Character *pchar, F32 fDecay);

// Marks the highest incoming damage tracker as with the Kill CombatTracker flag.
//  This is NOT the same as the other code which finds the Entity that landed the
//  killing blow, because apparently that does special stuff.
DamageTracker *damagetracker_MarkKillingBlow(SA_PARAM_NN_VALID Character *pchar);


DamageTracker *damageTracker_GetHighestDamager( Character * victim );
Entity        *damageTracker_GetHighestDamagerEntity( int iPartitionIdx, Character * victim );
	// Later we will want functions to look at damage trackers and decide more things probably

int damageTracker_FillAttackerArray(int iPartitionIdx, Character *pchar, Entity*** ents);
	// Later we will want to hand adding groups, not just ents that caused damage

bool damageTracker_HasBeenDamagedInPvP(int iPartitionIdx, Character *pchar);




// CombatTrackerNet code

// Constructs a CombatTrackerNet from the given DamageTracker
SA_RET_NN_VALID CombatTrackerNet *damageTracker_BuildCombatTrackerNet(SA_PARAM_NN_VALID DamageTracker *pTracker, Entity* pOwner);



#endif
