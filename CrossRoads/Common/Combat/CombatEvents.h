#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// CombatEvents are simple code-defined named integers that are tracked
//  on Characters.  Each event tracks the last time it occurred, as well
//  as the number of instances of that event in the combat tick.  Powers,
//  AttribMods and other systems may query the events for a variety of
//  purposes.

// Forward declarations
typedef struct AttribModDef AttribModDef;
typedef struct Character Character;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PowerDef PowerDef;

/***** ENUMS *****/

// All CombatEvents are currently defined by code
AUTO_ENUM;
typedef enum CombatEvent
{
	kCombatEvent_ActivateSelf,
		// Character activated a power on self

	kCombatEvent_ActivateInOther,
		// Character was targeted by an activated power used by another character

	kCombatEvent_ActivateOutOther,
		// Character targeted another character with an activated power

	kCombatEvent_AttribDamageIn,
		// Character's attribute was damaged

	kCombatEvent_AttribDamageOut,
		// Character damaged a target's attribute	

	kCombatEvent_AttribHealIn,
		// Character's attribute was healed

	kCombatEvent_AttribHealOut,
		// Character healed a target's attribute

	kCombatEvent_AttribPowerEmptied,
		// Character's Power attribute reached 0

	kCombatEvent_BlockInTimed,
		// Character blocked an incoming Power (kCombatTrackerFlag_Block), at hit time

	kCombatEvent_BloodiedStart,
		// Character became Bloodied (as specified by fBloodiedThreshold)

	kCombatEvent_CombatModeActIn,
		// Character was acted upon in a way that triggers combat mode

	kCombatEvent_CombatModeActOut,
		// Character performed an action that triggers combat mode

	kCombatEvent_CombatModeStart,
		// Character entered combat mode

	kCombatEvent_CombatModeStop,
		// Character left combat mode

	kCombatEvent_CriticalIn,
		// Character was critically hit by an incoming Power (kCombatTrackerFlag_Critical)

	kCombatEvent_CriticalInTimed,
		// Character was critically hit by an incoming Power (kCombatTrackerFlag_Critical), at hit time

	kCombatEvent_CriticalOut,
		// Character had an outgoing Power critically hit (kCombatTrackerFlag_Critical)

	kCombatEvent_CriticalOutTimed,
		// Character had an outgoing Power critically hit (kCombatTrackerFlag_Critical), at hit time

	kCombatEvent_DamageIn,	ENAMES(DamageIn Damaged)
		// Character was damaged

	kCombatEvent_DamageOut,
		// Character damaged a target

	kCombatEvent_DisabledStart,
		// Character became disabled

	kCombatEvent_DisabledStop,
		// Character stopped being disabled

	kCombatEvent_DodgeIn,
		// Character dodged an incoming Power (kCombatTrackerFlag_Dodge)

	kCombatEvent_DodgeInTimed,
		// Character dodged an incoming Power (kCombatTrackerFlag_Dodge), at hit time 

	kCombatEvent_DodgeOut,
		// Character had an outgoing Power dodged (kCombatTrackerFlag_Dodge)

	kCombatEvent_DodgeOutTimed,
		// Character had an outgoing Power dodged (kCombatTrackerFlag_Dodge), at hit time

	kCombatEvent_HealIn,
		// Character was healed

	kCombatEvent_HealOut,
		// Character healed a target

	kCombatEvent_HeldStart,
		// Character became held

	kCombatEvent_HeldStop,
		// Character stopped being held

	kCombatEvent_InteractStart,
		// Character begins an interact

	kCombatEvent_KillIn,
		// Character was killed 

	kCombatEvent_KillOut,
		// Character killed a target

	kCombatEvent_KnockIn,
		// Character was knocked (Includes KnockUp, KnockBack, KnockTo and Repel)

	kCombatEvent_MissIn,
		// Character was missed by an incoming Power (kCombatTrackerFlag_Miss)

	kCombatEvent_MissInTimed,
		// Character was missed by an incoming Power (kCombatTrackerFlag_Miss), at hit time

	kCombatEvent_MissOut,
		// Character had an outgoing Power miss (kCombatTrackerFlag_Miss)

	kCombatEvent_MissOutTimed,
		// Character had an outgoing Power miss (kCombatTrackerFlag_Miss), at hit time

	kCombatEvent_PlacateIn,
		// Character became placated by a source they were not previously placated by

	kCombatEvent_PlacateOut,
		// Character placated a target they were not previously placating

	kCombatEvent_PowerMode,
		// Character has an active PowerMode AttribMod which has the CombatEvent flag enabled

	kCombatEvent_PowerRecharged,
		// A Power that was recharging has finished

	kCombatEvent_AttemptRepelOut,
		// A Power is trying to repel a target

	kCombatEvent_AttemptRepelIn,
		// A Power is trying to repel the source

	kCombatEvent_RootedStart,
		// Character became rooted

	kCombatEvent_RootedStop,
		// Character stopped being rooted

	kCombatEvent_PowerChargeGained,
		// The power has gained a single charge

	kCombatEvent_NearDeathDead,
		// only when nearDeath is a possibility for the character. When the character dies for reals, this event is triggered

	kCombatEvent_Count,	EIGNORE
} CombatEvent;

extern U8 g_ubCombatEventComplexWhitelist[];

/***** END ENUMS *****/

// Tracks a bunch of extra data about an otherwise generic CombatEvent
AUTO_STRUCT;
typedef struct CombatEventTracker
{
	CombatEvent eEvent;
		// The actual event

	EntityRef erOther;
		// The other party to the event

	PowerDef *pPowerDef;			AST(UNOWNED)
		// The PowerDef of the event

	AttribModDef *pAttribModDef;	AST(UNOWNED)
		// The AttribModDef of the event

	F32 fMag;
		// The magnitude of the event (e.g. amount of damage)

	F32 fMagPreResist;
		// The pre-resistance magnitude of the event (e.g. amount of damage prior to target's resistance)

	F32 fAngleToSource;
		// for ArcAffects 

} CombatEventTracker;

// Tracks a Character's CombatEvent state, not allocated on the
//  client since it's only relevant on the server.
AUTO_STRUCT;
typedef struct CombatEventState
{

	U32 auiCombatEventTimestamp[kCombatEvent_Count];		NO_AST
		// Times of CombatEvents

	U16 auiCombatEventCount[kCombatEvent_Count];			NO_AST
		// Count of CombatEvents

	U16 auiCombatEventCountCurrent[kCombatEvent_Count];		NO_AST
		// Count of CombatEvents that have occurred during the current tick

	U8 abCombatEventTriggerComplex[kCombatEvent_Count];		NO_AST
		// Flags to indicate if a particular event has a TriggerComplex watching for it

	U8 abCombatEventTriggerSimple[kCombatEvent_Count];		NO_AST
		// Flags to indicate if a particular event has a TriggerSimple watching for it

	CombatEventTracker **ppEvents;
		// EArray of complex event data

	U32 *puiCombatTrackerFlagCriticalIDs;
		// EArray of IDs associated with CombatTrackerFlag_Critical.
		//  This CombatTrackerFlag triggers a CriticalInTimed and CriticalOutTimed CombatEvent.
		//  Critical may be detected more than once per real incoming critical
		//  on a Character, so the ID for the event is saved, and checked against
		//  this list to see if it's already been seen before being counted as real.

	U32 *puiCombatTrackerFlagDodgeIDs;
		// EArray of IDs associated with CombatTrackerFlag_Dodge.  Same deal as Critical.

	U32 *puiCombatTrackerFlagBlockIDs;
		// EArray of IDs associated with CombatTrackerFlag_Block.  Same deal as Critical.

} CombatEventState;

// Creates a CombatEventState structure
SA_RET_NN_VALID CombatEventState* combatEventState_Create(void);

// Utility function for the very common case of tracking an event between the target (pcharIn) and another Entity (pentOut).
//  Because this can call the complex tracking, it takes all the same arguments, which may end up making
//  it too heavy to use.
void character_CombatEventTrackInOut(SA_PARAM_NN_VALID Character *pcharIn,
									 CombatEvent eEventIn,
									 CombatEvent eEventOut,
									 SA_PARAM_OP_VALID Entity *pentOut,
									 SA_PARAM_OP_VALID PowerDef *pPowerDef,
									 SA_PARAM_OP_VALID AttribModDef *pAttribModDef,
									 F32 fMag,
									 F32 fMagPreResist,
									 SA_PARAM_OP_VALID const Vec3 vSourcePosIn,
									 SA_PARAM_OP_VALID const Vec3 vSourcePosOut);

// Adds full tracking for an event to the Character's CombatEvent trackers.  Should
//  only be called if there is a TriggerComplex AttribMod watching for the event.
void character_CombatEventTrackComplex(SA_PARAM_NN_VALID Character *pchar,
									   CombatEvent eEvent,
									   SA_PARAM_OP_VALID Entity *pentOther,
									   SA_PARAM_OP_VALID PowerDef *pPowerDef,
									   SA_PARAM_OP_VALID AttribModDef *pAttribModDef,
									   F32 fMag,
									   F32 fMagPreResist,
									   SA_PARAM_OP_VALID const Vec3 vSourcePos);

// Takes the current CombatEvents array and copies it to the regular array, and updates
//  the timestamp array as appropriate.  Also handles triggering any TriggerComplex mods.
void character_FinalizeCombatEvents(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, U32 uiTimestamp, GameAccountDataExtract *pExtract);

// Takes a list of CombatEvents and a time in seconds, which is how far back in time to 
//  look for the CombatEvents.  If 0 or less, will only look for CombatEvents within the
//  last combat tick.
S32 character_CheckCombatEvents(SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_NN_VALID int *piEvents,
								F32 fTime);

// Takes a list of CombatEvents, and finds the number of said events that occurred during
//  the last combat tick.  If the max events is non-zero, will break after finding that
//  many events.
S32 character_CountCombatEvents(SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_NN_VALID int *piEvents,
								S32 iMaxEvents);

