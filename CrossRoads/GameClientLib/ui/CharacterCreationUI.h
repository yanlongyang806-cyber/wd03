#pragma once
GCC_SYSTEM
#ifndef CHARACTER_CREATION_UI_H
#define CHARACTER_CREATION_UI_H

#include "NotifyCommon.h"

typedef struct PossibleCharacterChoice PossibleCharacterChoice;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct CmdList CmdList;
typedef struct SpeciesDef SpeciesDef;
typedef struct CharacterPath CharacterPath;
typedef struct AllegianceDef AllegianceDef;

extern CmdList g_CharacterCreationCmds;

extern NOCONST(Entity) *g_pFakePlayer;

AUTO_STRUCT;
typedef struct CharacterPathNode 
{
	CharacterPath *pPath; AST(UNOWNED)
	const char *pchProductName; AST(POOL_STRING)
	char *estrError; AST(ESTRING)
	bool bCanPurchase;
	bool bCanUse;

} CharacterPathNode;

AUTO_ENUM;
typedef enum FillSpeciesListFlag
{
	kFillSpeciesListFlag_NoDuplicates = 1 << 0,
	kFillSpeciesListFlag_Space = 1 << 2,
	kFillSpeciesListFlag_UGC = 1 << 3,
	kFillSpeciesListFlag_MicroTransacted = 1 << 4,
	kFillSpeciesListFlag_OnlyIncludeGenderFemale = 1 << 5,
	kFillSpeciesListFlag_OnlyIncludeGenderMale = 1 << 6,
	kFillSpeciesListFlag_HideBetaSpecies = 1 << 7,
} FillSpeciesListFlag;

AUTO_STRUCT;
typedef struct CharacterCreationSpeciesUI
{
	// Internal name of species group
	const char *pchSpeciesName; AST(POOL_STRING)

	// Internal name of current species
	const char *pchCurrentSpecies; AST(POOL_STRING)

	// The display name of the current species
	DisplayMessage DisplayNameMsg; AST(STRUCT(parse_DisplayMessage))

	// The texture name of the current species
	const char *pchTextureName; AST(POOL_STRING)

	// It's possible to create a new character with this species
	bool bCreatable;

	// This species is currently selected
	bool bSelected;

	// The number of species in the group
	S32 iSpeciesCount;

	// The earray of species that are in the species group
	const char **eapchRelatedSpecies; AST(POOL_STRING)

	// The seed for random costumes
	U32 uCostumeSeed;

	// Pointer to the current SpeciesDef
	SpeciesDef *pSpeciesDef; AST(UNOWNED)
} CharacterCreationSpeciesUI;

AUTO_STRUCT;
typedef struct CharacterCreationAllegianceUI
{
	// Internal name of the allegiance
	const char *pchAllegiance; AST(POOL_STRING)

	// The display name of the allegiance
	DisplayMessage DisplayNameMsg; AST(STRUCT(parse_DisplayMessage))

	// The texture name of the allegiance
	const char *pchTextureName; AST(POOL_STRING)

	// It's possible to create a new character in this allegiance
	bool bCreatable;

	// This allegiance is currently selected
	bool bSelected;

	// The current/selected species
	const char *pchCurrentSpecies; AST(POOL_STRING)

	// The earray of valid species
	CharacterCreationSpeciesUI **eaSpecies;

	// Pointer to the current AllegianceDef
	AllegianceDef *pAllegianceDef; AST(UNOWNED)
} CharacterCreationAllegianceUI;

AUTO_STRUCT;
typedef struct SpeciesGroupUI
{
	const char *pcSpeciesGroup;				AST( POOL_STRING NAME("SpeciesGroup") )
	EARRAY_OF(SpeciesDef) eaSpeciesList;	AST( NAME("SpeciesList") NO_INDEX )
} SpeciesGroupUI;

bool CharacterCreation_NotifyNameError(NotifyType eType, const char *pcName, int iMinLength, int iMaxLength, bool bCharacterName);
#define CharacterCreation_IsNameValidWithErrorMessage(pchName, eType) (!CharacterCreation_NotifyNameError((eType), (pchName), -1, -1, true))

bool CharacterCreation_NotifyDescriptionError(NotifyType eType, const char *pcDescription);
#define CharacterCreation_IsDescriptionValidWithErrorMessage(pchDescription) (!CharacterCreation_NotifyDescriptionError(kNotifyType_DescriptionInvalid, (pchDescription)))

Entity* CharacterCreation_InitFakePlayerWithPowers(Entity *pOwner, Entity *pEnt);
void CharacterCreation_FillSpeciesListEx(SpeciesDef ***peaSpeciesList, const char *pcAllegianceName, U32 uFlags);
#define CharacterCreation_FillSpeciesList(peaSpeciesList, pcAllegianceName, bSpace, bUGC) CharacterCreation_FillSpeciesListEx(peaSpeciesList, pcAllegianceName, kFillSpeciesListFlag_NoDuplicates | ((bSpace) ? kFillSpeciesListFlag_Space : 0) | ((bUGC) ? kFillSpeciesListFlag_UGC : 0));
void CharacterCreation_FillGenderListEx(SpeciesDef ***peaGenderList, SpeciesDef *pSpecies, U32 uFlags);
#define CharacterCreation_FillGenderList(peaGenderList, pSpecies, bUGC) CharacterCreation_FillGenderListEx(peaGenderList, pSpecies, ((bUGC) ? kFillSpeciesListFlag_UGC : 0))
S32 CharacterCreation_DD_GetAbilityMod(const char *pszAttribName);
PlayerCostume* CharacterCreation_GetDefaultCostumeForSpecies(SA_PARAM_NN_VALID SpeciesDef* pSpecies, bool bIsPet);
void CharacterCreation_ManageFreeCharacterPathList(void);

#endif
