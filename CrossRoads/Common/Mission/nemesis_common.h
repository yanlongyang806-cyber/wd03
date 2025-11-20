/***************************************************************************
*     Copyright (c) 2008-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NEMESIS_COMMON_H
#define NEMESIS_COMMON_H
GCC_SYSTEM

#include "entEnums.h"
#include "Message.h"
#include "referencesystem.h"
#include "GlobalEnums.h"

typedef struct Entity Entity;
typedef struct ParseTable ParseTable;
typedef struct Player Player;
typedef struct CritterGroup CritterGroup;
typedef struct NOCONST(Nemesis) NOCONST(Nemesis);
typedef U32 ContainerID;
typedef struct PlayerCostume PlayerCostume;
typedef struct CritterDef CritterDef;
typedef struct CritterGroup CritterGroup;

#define MAX_NEMESIS_COUNT 18
#define MAX_NEMESIS_TEAM_INDEX 100	// maximum team index, much larger than current team size to allow for expansion

// This is the Nemesis data stored on the Nemesis entity itself
AUTO_STRUCT AST_CONTAINER;
typedef struct Nemesis
{
	const NemesisMotivation motivation;				AST(PERSIST SUBSCRIBE)
	const NemesisPersonality personality;			AST(PERSIST SUBSCRIBE)
	
	// TODO - Maybe these should be references?
	CONST_STRING_POOLED pchPowerSet;				AST(PERSIST POOL_STRING SUBSCRIBE)
	CONST_STRING_POOLED pchMinionPowerSet;			AST(PERSIST POOL_STRING SUBSCRIBE)
	CONST_STRING_POOLED pchMinionCostumeSet;		AST(PERSIST POOL_STRING SUBSCRIBE)

	const F32 fPowerHue;							AST(PERSIST SUBSCRIBE)
	const F32 fMinionPowerHue;						AST(PERSIST SUBSCRIBE)

} Nemesis;

// This is the Nemesis state data stored on the player
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerNemesisState
{
	// The Container ID for the nemesis (type is always GLOBALTYPE_ENTITYSAVEDPET)
	const ContainerID iNemesisID;					AST(PERSIST KEY SUBSCRIBE FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "EntitySavedPet"))
	const NemesisState eState;						AST(PERSIST SUBSCRIBE)
	const U32 uTimesDefeated;						AST(PERSIST)
} PlayerNemesisState;

// Info for a nemesis power set
AUTO_STRUCT;
typedef struct NemesisPowerSet
{
	CONST_STRING_POOLED pcName;			AST(POOL_STRING STRUCTPARAM NAME("Name"))
	DisplayMessage msgDisplayName;		AST(STRUCT(parse_DisplayMessage) NAME("DisplayMessage"))
	DisplayMessage msgDescription;		AST(STRUCT(parse_DisplayMessage) NAME("Description"))
	CONST_STRING_POOLED pcImage;		AST(NAME("Image"))
	CONST_STRING_POOLED pcIcon;			AST(NAME("Icon"))
	CONST_STRING_POOLED pcCritter;		AST(POOL_STRING NAME("CritterDef"))
} NemesisPowerSet;

// Info for a nemesis minion power set
AUTO_STRUCT;
typedef struct NemesisMinionPowerSet
{
	CONST_STRING_POOLED pcName;			AST(POOL_STRING STRUCTPARAM NAME("Name"))
	DisplayMessage msgDisplayName;		AST(STRUCT(parse_DisplayMessage) NAME("DisplayMessage"))
} NemesisMinionPowerSet;

// costumes by class pair
AUTO_STRUCT;
typedef struct NemesisMinionCostume
{
	// class name that uses this costume
	CONST_STRING_POOLED pcClassName;			AST(POOL_STRING STRUCTPARAM NAME("ClassName"))
	// The costume to use for this class
	REF_TO(PlayerCostume) hCostume;				AST(NAME(CostumeName), REFDICT(PlayerCostume))
}NemesisMinionCostume;

// Info for a nemesis minion costume set
AUTO_STRUCT;
typedef struct NemesisMinionCostumeSet
{
	CONST_STRING_POOLED pcName;					AST(POOL_STRING STRUCTPARAM NAME("Name"))
	DisplayMessage msgDisplayName;				AST(STRUCT(parse_DisplayMessage) NAME("DisplayMessage"))
	EARRAY_OF(NemesisMinionCostume) eaCostumes;	AST(NAME("Costumes"))
} NemesisMinionCostumeSet;

// Structures for holding an EArray of each of the above nemesis structures
AUTO_STRUCT;
typedef struct NemesisPowerSetList
{
	NemesisPowerSet **sets;				AST(NAME("NemesisPowerSet"))
} NemesisPowerSetList;

AUTO_STRUCT;
typedef struct NemesisMinionPowerSetList
{
	NemesisMinionPowerSet **sets;		AST(NAME("NemesisMinionPowerSet"))
} NemesisMinionPowerSetList;

AUTO_STRUCT;
typedef struct NemesisMinionCostumeSetList
{
	NemesisMinionCostumeSet **sets;		AST(NAME("NemesisMinionCostumeSet"))
} NemesisMinionCostumeSetList;

AUTO_STRUCT;
typedef struct NemesisPrices
{
	U32 personalityCost;
	U32 nemesisPowerSetCost;
	U32 nemesisPowerHueCost;      // (maybe free if you also change the Power Set)
	U32 minionPowerSetCost;
	U32 minionPowerHueCost;       // (maybe free if you also change the Power set)
	U32 minionCostumeSetCost;

	F32 *eafLevelMultipliers;		AST(NAME("LevelMultipliers"))
} NemesisPrices;

AUTO_STRUCT;
typedef struct NemesisDefaultCritter
{
	// critter def to use if character missing (nemesis)
	REF_TO(CritterDef) hCritter;				AST(NAME(CritterDef), RESOURCEDICT(CritterDef))

}NemesisDefaultCritter;

AUTO_STRUCT;
typedef struct NemesisDefaultCritterGroup
{
	// crittergroup (minions) for missing character
	REF_TO(CritterGroup) hCritterGroup;			AST(NAME(CritterGroup) RESOURCEDICT(CritterGroup))

}NemesisDefaultCritterGroup;

AUTO_STRUCT;
typedef struct NemesisDefaultName
{
	const char* pchNemesisName;					AST(POOL_STRING NAME("NemesisName"))

}NemesisDefaultName;

AUTO_STRUCT;
typedef struct NemesisDefaultCostume
{
	// costume of the nemesis
	REF_TO(PlayerCostume) hNemesisCostume;		AST(NAME(CostumeName), REFDICT(PlayerCostume))

}NemesisDefaultCostume;

AUTO_STRUCT;
typedef struct NemesisDefaultMinionCostume
{
	// the name of the nemesis minion costume set
	const char* pchNemesisCostumeSet;			AST(POOL_STRING, NAME("NemesisMinionCostume"))

}NemesisDefaultMinionCostume;


AUTO_STRUCT;
typedef struct NemesisConfig
{
	// The default nemesis info, used when a player needs a nemesis and doesn't have one
	EARRAY_OF(NemesisDefaultCritter) eaNemesisCritters;
	EARRAY_OF(NemesisDefaultCritterGroup) eaNemesisCritterGroups;
	EARRAY_OF(NemesisDefaultCostume) eaNemesisCostumes;
	EARRAY_OF(NemesisDefaultName) eaNemesisDefaultNames;
	EARRAY_OF(NemesisDefaultMinionCostume) eaNemesisDefaultMinionCostumes;

}NemesisConfig;

extern NemesisPowerSetList g_NemesisPowerSetList;
extern NemesisMinionPowerSetList g_NemesisMinionPowerSetList;
extern NemesisMinionCostumeSetList g_NemesisMinionCostumeSetList;
extern NemesisPrices g_NemesisPrices;
extern NemesisConfig g_NemesisConfig;

extern const char **g_eaNemesisCostumeNames;

// ----------------------------------------------------------------------------
// Function Declarations
// ----------------------------------------------------------------------------

// Get the player's current primary nemesis
Entity* player_GetPrimaryNemesis(const Entity* pEnt);

// Gets a Nemesis by the ID
Entity* player_GetNemesisByID(const Entity* pEnt, ContainerID iNemesisID);

// Gets a player's Nemesis State by ID
PlayerNemesisState* player_GetNemesisStateByID(const Entity* pEnt, ContainerID iNemesisID);

// Gets the ID of the player's current primary nemesis
ContainerID player_GetPrimaryNemesisID(const Entity* pEnt);

// Finds a Nemesis ID from a display name.  Don't use this for anything important as 
// nemesis names are not 100% guaranteed to be unique.
ContainerID nemesis_FindIDFromName(Entity *pEnt, const char *pchNemesisName);

// Gets the cost in Resources to change the Nemesis
S32 nemesis_trh_GetCostToChange(ATH_ARG const NOCONST(Nemesis) *pNemesis, int iPlayerLevel, NemesisPersonality personality, const char *pchNemesisPowerSet, const char *pchMinionPowerSet, const char *pchMinionCostumeSet, F32 fPowerHue, F32 fMinionPowerHue);

#endif