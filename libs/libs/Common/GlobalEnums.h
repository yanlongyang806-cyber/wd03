#pragma once
GCC_SYSTEM

typedef struct StaticDefineInt StaticDefineInt;

AUTO_ENUM;
typedef enum ZoneMapType
{
	ZMTYPE_UNSPECIFIED,				ENAMES(Unspecified)

	ZMTYPE_STATIC,					ENAMES(Static)		// Maps shared by many players
	ZMTYPE_MISSION,					ENAMES(Mission)		// Maps private to a player or a team
	ZMTYPE_OWNED,					ENAMES(Owned)		// A Mission map with an owning player used for Nemesis
	ZMTYPE_SHARED,					ENAMES(Shared)		// A Mission map that can be shared by many players
	ZMTYPE_PVP,						ENAMES(PvP)			// Maps managed by the PvP Queue
	ZMTYPE_QUEUED_PVE,				ENAMES(QPvE)		// Maps managed by the PvE Queue

	ZMTYPE_COUNT,					EIGNORE

	ZMTYPE_MAX,						EIGNORE // Leave this last, not a valid flag, just for the compile time assert below
} ZoneMapType;
extern StaticDefineInt ZoneMapTypeEnum[];

#define ZoneMapType_NUMBITS 8 // 8 bits can hold up to an enum value of 2^7-1, because of the sign bit
STATIC_ASSERT(ZMTYPE_MAX <= (1 << (ZoneMapType_NUMBITS - 1)));


AUTO_ENUM;
typedef enum NemesisState
{
	NemesisState_None,		// Undefined
	NemesisState_Primary,	// Primary Nemesis
	NemesisState_AtLarge,	// Nemesis is out in the world and may be encountered
	NemesisState_InJail,	// Nemesis is currently locked away, but could be released in the future
	NemesisState_Dead,		// Nemesis is dead and gone for good (or is s/he?  Dun dun DUN!!!)
	NemesisState_Reformed,	// Nemesis has turned good, and might act as an ally
	NemesisState_Max,		EIGNORE

	// --- The following are not real states, they are just used for Events and logging purposes ---
	NemesisState_Created,
	NemesisState_Deleted,
} NemesisState;


//flags that are passed back and forth when commands are called
AUTO_ENUM;
typedef enum CommandServingFlags
{
	CMDSRV_JSONRPC = 1 << 0, //this is a JSON RPC command
	CMDSRV_NON_CACHED_RETURN = 1 << 1, //normally when a command is executed and the result makes it back up to the serving layer,
		//the result is stuck in a cache and we wait for someone to access /waitforcommand to get their result. In this mode, we 
		//shortcut that
	CMDSRV_NORETURN = 1 << 2, // no return value expected

	CMDSRV_ALWAYS_ALLOW_JSONRPC = 1 << 3, //allow all commands to be executed through JSONRPC, even
		//ones without CMDF_ALLOW_JSONRPC

} CommandServingFlags;


extern StaticDefineInt NemesisStateEnum[];
