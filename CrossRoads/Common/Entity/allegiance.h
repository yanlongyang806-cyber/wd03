/***************************************************************************
*     Copyright (c) 2003-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef allegiance_H
#define allegiance_H
GCC_SYSTEM

#include "Message.h"

typedef const void* DictionaryHandle;
typedef struct CritterFaction CritterFaction;
typedef struct SpeciesDefRef SpeciesDefRef;
typedef struct SpeciesDef SpeciesDef;
typedef struct CharacterClassRef CharacterClassRef;
typedef struct Entity Entity;
typedef struct GameAccountData GameAccountData;
typedef struct MissionDef MissionDef;

AUTO_STRUCT;
typedef struct AllegianceWarpRestrict
{
	S32 iRequiredLevel;
	const char* pchRequiredMission; AST(NAME(RequiredMission) POOL_STRING)
} AllegianceWarpRestrict;

AUTO_STRUCT;
typedef struct AllegianceDefaultMap
{
	const char* pchMapName; AST(POOL_STRING KEY STRUCTPARAM)
	const char* pchSpawn;	AST(POOL_STRING)
} AllegianceDefaultMap;

AUTO_STRUCT;
typedef struct AllegianceNamePrefix
{
	const char* pchPrefix;					AST(STRUCTPARAM)
	CharacterClassRef **eaClassesAllowed;	AST(NAME("AllowedClass"))
	SpeciesDefRef **eaSpeciesAllowed;		AST(NAME("AllowedSpecies"))
	const char* pchGameAccountDataKey;		AST(NAME("GameAccountUnlockKey"))
} AllegianceNamePrefix;

AUTO_STRUCT;
typedef struct AllegianceSpeciesChange {
	REF_TO(SpeciesDef) hFromSpecies;		AST(NAME("FromSpecies"))
	REF_TO(SpeciesDef) hToSpecies;			AST(NAME("ToSpecies"))
} AllegianceSpeciesChange;

AUTO_STRUCT;
typedef struct AllegianceDef
{
	char *pcName;							AST(NAME(Name) STRUCTPARAM KEY)
	DisplayMessage displayNameMsg;			AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	char *pcLogName;
	char *pchIcon;							AST(NAME(Icon))
	const char *pcFileName;					AST(CURRENTFILE)

	REF_TO(CritterFaction) hFaction;		AST(RESOURCEDICT(CritterFaction) NAME(Faction))
	REF_TO(CritterFaction) hFactionBattleForm;	AST(RESOURCEDICT(CritterFaction) NAME(FactionBattleForm))
		// Faction to switch to when in BattleForm

	DisplayMessage descriptionMsg;			AST(NAME(Description) STRUCT(parse_DisplayMessage))
	DisplayMessage descriptionLongMsg;		AST(NAME(DescriptionLong) STRUCT(parse_DisplayMessage))

	bool bNeedsUnlock;

	bool bDefaultPlayerAllegiance;
	bool bCanBeSubAllegiance;
	bool bDeferOfficerToSubAllegiance;

	REF_TO(MissionDef) hRequiredMissionForAllegianceChange;	AST(NAME("RequiredMissionForAllegianceChange"))
	const char **ppchAllowedAllegianceChanges;	AST(NAME("AllowedAllegianceChange") POOL_STRING)

	SpeciesDefRef **eaStartSpecies;	AST(NAME("StartingSpecies"))
	SpeciesDefRef **eaPetSpecies;	AST(NAME("PetGrantSpecies"))

	AllegianceSpeciesChange **eaSpeciesChange;	AST(NAME("SpeciesChange"))

	CharacterClassRef **eaClassesAllowed;	AST(NAME("AllowedClass"))

	AllegianceWarpRestrict* pWarpRestrict;	AST(NAME("WarpRestrictions"))

	AllegianceDefaultMap** eaDefaultMaps;	AST(NAME("DefaultMap"))

	AllegianceNamePrefix** eaNamePrefixes;	AST(NAME("NamePrefix"))
	AllegianceNamePrefix** eaSubNamePrefixes;	AST(NAME("SubNamePrefix"))

	char **ppNewCharacterMaps;				AST(NAME(NewCharacterMap)) // What maps new characters are allowed to start on - If Not NULL this overrides MapManagerConfig
	char **ppSkipTutorialMaps;				AST(NAME(SkipTutorialMap)) // What maps new characters are allowed to start on when the skip the tutorial - If Not NULL this overrides MapManagerConfig
	char *pFallbackStaticMap;				AST(NAME(FallbackStaticMap)) // This is the map a player will be sent to if they try to log in and don't have a valid map in their map history - If Not NULL this overrides MapManagerConfig
} AllegianceDef;

typedef struct AllegianceDefaults
{
	REF_TO(AllegianceDef) hDefaultPlayerAllegiance;
} AllegianceDefaults;

AUTO_STRUCT;
typedef struct AllegianceRef {
	REF_TO(AllegianceDef) hDef;
} AllegianceRef;
extern ParseTable parse_AllegianceRef[];
#define TYPE_parse_AllegianceRef AllegianceRef

AUTO_STRUCT;
typedef struct AllegianceList {
	AllegianceRef **refArray;
} AllegianceList;

AllegianceDefaults* gAllegianceDefaults;

#define allegiance_GetOfficerPreference(pAllegiance, pSubAllegiance)  (pAllegiance ? (((pAllegiance)->bDeferOfficerToSubAllegiance && pSubAllegiance) ? pSubAllegiance : pAllegiance) : NULL)
#define allegiance_GetOfficerNonPreference(pAllegiance, pSubAllegiance)  (pAllegiance ? (((pAllegiance)->bDeferOfficerToSubAllegiance && pSubAllegiance) ? pAllegiance : NULL) : NULL)

void critterAllegiance_Load(void);
AllegianceDef* allegiance_FindByName( const char* pchAllegiance );

bool allegiance_CanPlayerUseWarp(Entity* pEnt);
bool allegiance_CanUseNamePrefix(AllegianceNamePrefix* pPrefix, Entity* pEnt, GameAccountData *pGameAccount);
const char *allegiance_GetNamePrefix(AllegianceDef* pAllegiance, AllegianceDef* pSubAllegiance, Entity* pNameEnt, GameAccountData *pGameAccount, const char *pchName, const char **ppchExpectedPrefix);
const char *allegiance_GetSubNamePrefix(AllegianceDef* pAllegiance, AllegianceDef* pSubAllegiance, Entity* pNameEnt, GameAccountData *pGameAccount, const char *pchName, const char **ppchExpectedPrefix);

extern DictionaryHandle g_hAllegianceDict;

extern ParseTable parse_AllegianceDef[];
#define TYPE_parse_AllegianceDef AllegianceDef

#endif
