/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef COMBATDEBUG_H__
#define COMBATDEBUG_H__
GCC_SYSTEM

// Describes structure for debugging combat information of an entity

typedef struct AttribMod			AttribMod;
typedef struct CharacterAttribs		CharacterAttribs;
typedef struct Entity				Entity;
typedef struct Player				Player;
typedef struct Power				Power;
typedef struct PowerActivation		PowerActivation;
typedef struct AttribModDef			AttribModDef;

// Copied from GlobalTypes.h
typedef U32 EntityRef;

// If the server should be writing its own CombatLog from CombatTrackerNets
extern S32 g_bCombatLogServer;

AUTO_STRUCT;
typedef struct CombatDebug
{
	bool bValid;
		// True if this struct is full of combat debug goodness

	EntityRef erDebugEnt;
		// The ent we're debugging

	F32 fTimerSleep;
	F32 fTimeSlept;
		// Sleeping data

	Power **ppPowers;
		// EArray of powers

	AttribMod **ppMods;
		// EArray of mods

	AttribModDef **ppInnateMods;
		// EArray of Innate attrib mods

	CharacterAttribs *pattrBasic;
		// Basic attribs

	CharacterAttribs *pattrStr;
		// Generic strengths

	CharacterAttribs *pattrRes;
		// Generic resists

	PowerActivation *pactivation;
		// Current power activation

} CombatDebug;

// Returns a new struct full of combat debugging info about the given ent
CombatDebug *combatdebug_GetData(EntityRef erDebugEnt);

// Destroys an existing CombatDebug structure
void combatdebug_Destroy(CombatDebug *pDbg);


// Structure to track time spent on a particular combat event
AUTO_STRUCT;
typedef struct CombatDebugPerfEvent
{
	char *pchEvent;
		// Name of event

	U32 uiCount;
		// Count of events

	U64 ulTime;
		// Sum CPU ticks spent on event

	U64 ulTimeSub;
		// Sum CPU ticks spent on event (extra timer)

	U64 ulTimePerEvent;		AST(SERVER_ONLY)
		// Average time per event, derived on the client

	F32 fPercentUsage;		AST(SERVER_ONLY)
		// Percent of total time this event accounts for across all events

} CombatDebugPerfEvent;

// Number of Players with perf info enabled
extern U32 g_uiCombatDebugPerf;
extern EntityRef g_erCombatDebugEntRef;

// Enable/disable perf tracking to the Player
void combatdebug_PerfEnable(SA_PARAM_NN_VALID Entity *pEnt, bool bEnable);

// Tracks performance data
void combatdebug_PerfTrack(SA_PARAM_NN_STR char *pchEvent, U32 uiCount, U64 ulTime, U64 ulTimeSub);

// Resets performance data
void combatdebug_PerfReset(void);

// Updates the Player's combat perf data
void combatdebug_PerfPlayerUpdate(SA_PARAM_NN_VALID Entity *pEnt);

void combatdebug_SetDebugFlagByName(const char* flagname);

#endif
