/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "Color.h"
#include "GlobalTypeEnum.h"
#include "rand.h"
#include "CostumeCommon.h"
#include "ReferenceSystem.h"
#include "UIColor.h"
#include "wlCostume.h"

typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct NOCONST(PCPart) NOCONST(PCPart);
typedef struct NOCONST(PCScaleValue) NOCONST(PCScaleValue);
typedef struct Entity Entity;
typedef struct SpeciesDef SpeciesDef;
typedef struct CostumePreset CostumePreset;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct PlayerCostumeRef PlayerCostumeRef;
typedef struct Guild Guild;
typedef struct GameAccountDataExtract GameAccountDataExtract;

// -- Dictionaries and management functions --

#define EDIT_FLAG_GEOMETRY  0x0001
#define EDIT_FLAG_MATERIAL  0x0002
#define EDIT_FLAG_TEXTURE1  0x0004
#define EDIT_FLAG_TEXTURE2  0x0008
#define EDIT_FLAG_TEXTURE3  0x0010
#define EDIT_FLAG_TEXTURE4  0x0020
#define EDIT_FLAG_TEXTURE5  0x0040
#define EDIT_FLAG_COLOR1    0x0040
#define EDIT_FLAG_COLOR2    0x0080
#define EDIT_FLAG_COLOR3    0x0100
#define EDIT_FLAG_COLOR4    0x0200

// costumeTailor_GetValidBones/Regions/Categories & costumeTailor_CategoryHasValidGeos flags
// TODO(jm): make the other functions use these flags instead of passing in a ton of bool's. though this mainly applied to costumeGetValidBones seeing a bunch of meaningless false's makes it harder to add new flags
#define CGVF_MIRROR_MODE				0x0001
#define CGVF_BONE_GROUP_MODE			0x0002
#define CGVF_OMIT_EMPTY					0x0004
#define CGVF_OMIT_ONLY_ONE				0x0008
#define CGVF_COUNT_NONE					0x0010
#define CGVF_REQUIRE_POWERFX			0x0020
#define CGVF_SORT_DISPLAY				0x0040
#define CGVF_UNLOCK_ALL					0x0080
#define CGVF_EXCLUDE_GUILD_EMBLEM		0x0100

// -- Costume validation functions --

//Returns true if this uniform has unlockable stuff
bool costumeTailor_DoesCostumeHaveUnlockables(PlayerCostume *pBaseCostume);

//Extracts an overlay from a costume using the specified bone group
PlayerCostume *costumeTailor_MakeCostumeOverlayEx(PlayerCostume *pBaseCostume, PCSkeletonDef *pSkel, const char *pchBoneGroup, bool bIncludeChildBones, bool bCloneCostume);
#define costumeTailor_MakeCostumeOverlay(pBaseCostume, pchBoneGroup, bIncludeChildBones, bCloneCostume) costumeTailor_MakeCostumeOverlayEx(pBaseCostume, NULL, pchBoneGroup, bIncludeChildBones, bCloneCostume)
PlayerCostume *costumeTailor_MakeCostumeOverlayBG(PlayerCostume *pBaseCostume, PCSkeletonDef *pSkel, PCBoneGroup *bg, bool bIncludeChildBones, bool bCloneCostume);


//Generates a costume from the entity costume and passed in CostumeDisplayData
NOCONST(PlayerCostume) *costumeTailor_ApplyOverrideSet(PlayerCostume *pBaseCostume, PCSlotType *pSlotType, CostumeDisplayData **eaData, SpeciesDef *pSpecies);

//Generates a mount costume from the passed in CostumeDisplayData
NOCONST(PlayerCostume) *costumeTailor_ApplyMount(CostumeDisplayData **eaData, F32 *fOutMountScaleOverride);

// Overlays the second costume onto the first to the extent that is legal
// Returns true if the base costume is modified in any way
bool costumeTailor_ApplyCostumeOverlay(PlayerCostume *pBaseCostume, CostumeDisplayData* pData, PlayerCostume *pCostumeToOverlay, PlayerCostume **eaUnlockedCostumes, const char *pchBoneGroup, PCSlotType *pSlotType, bool bForceCategory, bool bIgnoreSpeciesMatching, bool bIgnoreSkelMatching, bool bUseClosestColors);
bool costumeTailor_ApplyCostumeOverlayBG(PlayerCostume *pBaseCostume, CostumeDisplayData* pData, PlayerCostume *pCostumeToOverlay, PlayerCostume **eaUnlockedCostumes, PCBoneGroup *bg, PCSlotType *pSlotType, bool bForceCategory, bool bIgnoreSpeciesMatching, bool bIgnoreSkelMatching, bool bUseClosestColors, bool bOverlayCategoryReplace);

// Find the closest color in this set
void costumeTailor_FindClosestColor(const U8 colorSrc[4], const UIColorSet *colorSet, U8 colorDst[4]);

//Find the correct color set for this part
UIColorSet *costumeTailor_GetColorSetForPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int colorNum);
UIColorSet *costumeTailor_GetColorSetForPartConsideringSkin(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int colorNum);

PCColorQuadSet *costumeTailor_GetColorQuadSetForPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, NOCONST(PCPart) *pPart);

int costumeTailor_IsColorValidForPart(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, const U8 color0[4], const U8 color1[4], const U8 color2[4], const U8 color3[4], bool bCheckColor3);

// Validate a player created costume
typedef struct ValidatePlayerCostumeItems ValidatePlayerCostumeItems;
bool costumeValidate_ValidatePlayerCreated(PlayerCostume *pPCCostume, SpeciesDef *pSpecies, PCSlotType *pSlotType, Entity *pPlayerEnt, Entity *pEnt, char **ppEStringError, ValidatePlayerCostumeItems *pTestItems, PlayerCostume **eaOverrideUnlockedCostumes, bool bInEditor);

// Returns true if the given restriction passes for the costume
bool costumeTailor_TestRestriction(PCRestriction eRestriction, bool bUnlocked, PlayerCostume *pPCCostume);

bool costumeTailor_CategoryIsExcluded(NOCONST(PlayerCostume) *pPCCostume, PCCategory *pCategory);
bool costumeTailor_CategoryHasValidGeos(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, U32 uCGVFlags);

void costumeTailor_GetTextureValueMinMax(PCPart *pPCPart, PCTextureDef *pTexture, SpeciesDef *pSpecies, float *pfMin, float *pfMax);
void costumeTailor_GetTextureMovableValues(PCPart *pPCPart, PCTextureDef *pTexture, SpeciesDef *pSpecies,
									float *pfMovableMinX, float *pfMovableMaxX, float *pfMovableMinY, float *pfMovableMaxY,
									float *pfMovableMinScaleX, float *pfMovableMaxScaleX, float *pfMovableMinScaleY, float *pfMovableMaxScaleY,
									bool *pbMovableCanEditPosition, bool *pbMovableCanEditRotation, bool *pbMovableCanEditScale);


// -- Costume editing functions --

// Set the default skin color
void costumeTailor_SetDefaultSkinColor(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Fill all the regions with legal categories
void costumeTailor_FillAllRegions(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const char **eaPowerFXBones, PCSlotType *pSlotType);

// Fill a player costume with all legal bones but with no geometry unless one is required
// Useful for tools
void costumeTailor_FillAllBones(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const char **eaPowerFXBones, PCSlotType *pSlotType, bool bApplyRequired, bool bAddArtistData, bool bSortDisplay);

// Strip out all unnecessary data from a costume
// Good to do this before saving
void costumeTailor_StripUnnecessary(NOCONST(PlayerCostume) *pPCCostume);

// Change the skeleton on the costume
void costumeTailor_ChangeSkeleton(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCSlotType *pSlotType, PCSkeletonDef *pSkel);


// -- Costume color editing functions --

// Sets the color on a part, honoring color linking and skin color rules
// "colorNum" is 0 through 4.  "glowScale" is 0 through 10.
bool costumeTailor_SetPartColor(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, int colorNum, U8 color[4], U8 glowScale);
// Returns true if a color was copied into the color out param
bool costumeTailor_GetPartColor(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, int colorNum, U8 color[4]);

// This is a variant of setting color that supports mirror bones and getting
// a "real part".  It also modifies skin color instead of channel 3 when appropriate.
bool costumeTailor_SetRealPartColor(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, int iColorNum, U8 color[4],
									PCSlotType *pSlotType, bool bMirrorSelectMode);
// Returns true if a color was copied into the color out param
bool costumeTailor_GetRealPartColor(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, int iColorNum, U8 color[4]);

// Sets all four colors and glow scaling on a part, honoring color linking a skin color rules
void costumeTailor_SetPartColors(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, NOCONST(PCPart) *pPart, U8 color0[4], U8 color1[4], U8 color2[4], U8 color3[4], U8 glowScale[4]);

// Set skin color and apply to appropriate parts
bool costumeTailor_SetSkinColor(NOCONST(PlayerCostume) *pPCCostume, U8 skinColor[4]);
// Returns true if a color was copied into the color out param
bool costumeTailor_GetSkinColor(NOCONST(PlayerCostume) *pPCCostume, U8 skinColor[4]);

// Set color linking applying rules
void costumeTailor_SetPartColorLinking(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCColorLink eColorLink, SpeciesDef *pSpecies, const PCSlotType *pSlotType);
void costumeTailor_SetPartMaterialLinking(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCColorLink eMaterialLink, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);

// Get the scale factor for converting a mat constants to 
// a 100 scale "costumeValue = (materialValue * scale)"
F32 costumeTailor_GetMatConstantScale(const char *pcName);

// Get the number of values present in a material constant, determined by its name.  
S32 costumeTailor_GetMatConstantNumValues(const char* pcName);

// Get the name of a particular material constant value, for use in sub-labels.
const char* costumeTailor_GetMatConstantValueName(const char* pcName, int valIdx);

bool costumeTailor_IsSliderConstValid(const char *pcName, PCMaterialDef *pMat, int index);


// -- Costume valid part functions --

// Fills the earray with the legal set of costumes from costume set
// Applies restrictions based on the species
void costumeTailor_GetValidCostumesFromSet(PCCostumeSet *pCostumeSet, SpeciesDef *pSpecies, CostumeRefForSet ***peaCostumes, bool bSortDisplay, bool bLooseRestrict);

// Fills the earray with the legal set of skeletons for the costume
// Applies restrictions based on the costume
// If bSameSpecies is set, then it will match against all species with the same pcSpeciesName as the provided species
void costumeTailor_GetValidSkeletons(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCSkeletonDef ***peaSkels, bool bSameSpecies, bool bSortDisplay);

// Fills the earray with the legal set of species for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidSpecies(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef ***peaSpecies, bool bSortDisplay, bool bAllowCustom);
void costumeTailor_SortSpecies(SpeciesDef **eaSpecies, bool bSortDisplay);

// Fills the earray with the legal set of presets for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidPresets(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, CostumePreset ***peaPresets, bool bSortDisplay, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);
void costumeTailor_SortPresets(CostumePreset **eaPresets, bool bSortDisplay);

// Fills the earray with the legal set of stances for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidStances(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PCStanceInfo ***peaStances, bool bSortDisplay, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);


// Fills the earray with the legal set of moods
void costumeTailor_GetValidMoods(PCMood ***peaMoods, bool bSortDisplay);

// Fills the earray with the legal set of voices
void costumeTailor_GetValidVoices(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCVoice ***peaVoices, bool bSortDisplay, GameAccountDataExtract *pExtract);

// Fills the earray with the legal set of body scales for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidBodyScales(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCBodyScaleInfo ***peaBodyScales, bool bSortDisplay);

// Fills the earray with the legal set of regions for the costume
// Applies restrictions based on the costume
void costumeTailor_GetValidRegions(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, PCRegion ***peaRegions, U32 uCGVFlags);
void costumeTailor_SortRegionsOnDependency(PCRegion **eaRegions);

// Fills the earray with the legal set of categories for the costume and provided region
// Applies restrictions based on the costume
void costumeTailor_GetValidCategories(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, PCCategory ***peaCategories, U32 uCGVFlags);

// Fills the earray with the legal set of bones for the skeleton defined in the costume
// Applies restrictions based on the costume
// Region and category are optional and further restrict things.
void costumeTailor_GetValidBones(NOCONST(PlayerCostume) *pPCCostume, PCSkeletonDef *pSkeleton, PCRegion *pRegion, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCBoneDef ***peaBones, U32 uCGVFlags);

bool costumeTailor_IsBoneRequired(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone, SpeciesDef *pSpecies);

// Fills the earray with the legal set of geometries for the given bone
// Applies restrictions based on the costume
// Category is optional and further restricts things.
void costumeTailor_GetValidGeos(NOCONST(PlayerCostume) *pPCCostume, PCSkeletonDef *pSkeleton, PCBoneDef *pBoneDef, PCCategory *pCategory, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCGeometryDef ***peaGeos, bool bMirrorMode, bool bBoneGroupMode, bool bSortDisplay, bool bUnlockAll);

// Fills the earray with the legal set of geometry layers for the costume and provided geometry
// Applies restrictions based on the costume
void costumeTailor_GetValidLayers(NOCONST(PlayerCostume) *pPCCostume, PCGeometryDef *pGeoDef, PCGeometryDef *pChildGeoDef, PCLayer ***peaLayers, bool bSortDisplay);

// Fills the earray with the legal set of geometries for the child bone provided
// Applies restrictions based on the costume
void costumeTailor_GetValidChildGeos(NOCONST(PlayerCostume) *pPCCostume, PCCategory *pCategory, PCGeometryDef *pGeoDef, PCBoneDef *pChildBone, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCGeometryDef ***peaGeos, bool bSortDisplay, bool bUnlockAll);

// Fills the earray with the legal set of materials for the given geometry
// Applies restrictions based on the costume
void costumeTailor_GetValidMaterials(NOCONST(PlayerCostume) *pPCCostume, PCGeometryDef *pGeoDef, SpeciesDef *pSpecies, PCGeometryDef *pMirrorGeo, PCBoneGroup *pBoneGroup, PlayerCostume **eaUnlockedCostumes, PCMaterialDef ***peaMats, bool bMirrorMode, bool bSortDisplay, bool bUnlockAll);

bool costumeTailor_DoesMaterialRequireType(PCGeometryDef *pGeoDef, PCMaterialDef *pMatDef, SpeciesDef *pSpecies, PCTextureType eType);

// Fills the earray with the legal set of textures for the given material and texture type
// Applies restrictions based on the costume
void costumeTailor_GetValidTextures(NOCONST(PlayerCostume) *pPCCostume, PCMaterialDef *pMatDef, SpeciesDef *pSpecies, PCMaterialDef *pMirrorMat, PCBoneGroup *pBoneGroup, PCGeometryDef *pGeoDef, PCGeometryDef *pMirrorGeo, PlayerCostume **eaUnlockedCostumes, PCTextureType eType, PCTextureDef ***peaTexs, bool bMirrorMode, bool bSortDisplay, bool bUnlockAll);

// Get the list of all valid styles
// If pGeoDef is given, then it will only return styles for that geo
// If pBone is given, then it will only return styles for geos on that bone
// If pRegion is given, then it will only return styles for geos available on all the bones in the region
void costumeTailor_GetValidStyles(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory *pCategory, PCBoneDef *pBone, PCGeometryDef *pGeoDef, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCStyle ***peaStyles, U32 uCGVFlags);

// Get the list of all valid PCSlotTypes from pSlotDef.
// It is possible for a NULL slot type to be added to the returned list, if one of the optional slot types is unrestricted.
void costumeTailor_GetValidSlotTypes(NOCONST(PlayerCostume) *pPCCostume, PCSlotDef *pSlotDef, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType ***peaSlotTypes, U32 uCGVFlags);

// Fills the earray with the set of PresetScaleGroups for the given skeleton and group name
void costumeTailor_GetCostumePresetScales(NOCONST(PlayerCostume) *pPCCostume, PCPresetScaleValueGroup ***peaPresets, const char* pchGroupName);

// Get the texwords keys
void costumeTailor_GetValidTexWordsKeys(NOCONST(PlayerCostume) *pPCCostume, const char ***peaKeys);

// Pick a valid stance for the costume
// Leaves the costume alone if the current stance is legal
void costumeTailor_PickValidStance(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);


// Return the PCStanceInfo object corresponding to the costume's stance.
PCStanceInfo *costumeTailor_GetStance(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Pick a valid voice for the costume
// Leaves the costume alone if the current voice is legal
void costumeTailor_PickValidVoice(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, GameAccountDataExtract *pExtract);

// Pick a category for the region
// Leaves the category alone if the current category is legal
void costumeTailor_PickValidCategoryForRegion(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory **eaCategories, bool bIgnoreRegCatNull);

// Pick a valid category for the part
// Leaves the part alone if the current category is legal
void costumeTailor_PickValidCategory(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCCategory **eaCategories);

// Pick a valid geometry for the part
// Leaves the part alone if the current geometry is legal
void costumeTailor_PickValidGeometry(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCGeometryDef **eaGeos, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);

// Pick a valid child geometry for the part
// Leaves the part alone if the current geometry is legal
void costumeTailor_PickValidChildGeometry(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCBoneDef *pChildBone, SpeciesDef *pSpecies, PCGeometryDef **eaGeos, PlayerCostume **eaUnlockedCostumes, bool bUnlockAll);

// Pick a valid layer for the part.  Deletes invalid layer.
// Leaves the part alone if the current layer is legal
PCLayer *costumeTailor_PickValidLayer(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCLayer *pLayer, PCLayer **eaLayers);

// Pick a valid material for the part
// Leaves the part alone if the current material is legal
void costumeTailor_PickValidMaterial(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, NOCONST(PCPart) *pPart, PlayerCostume **eaUnlockedCostumes, PCMaterialDef **eaMats, bool bUnlockAll, bool bApplyMaterialRules);

// Pick a valid material for the part
// Leaves the part alone if the current material is legal
void costumeTailor_PickValidTexture(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCTextureType eType, PCTextureDef **eaTexs);

bool costumeTailor_PartHasGuildEmblem(NOCONST(PCPart) *pPart, Guild *pGuild);
bool costumeTailor_PartHasBadGuildEmblem(NOCONST(PCPart) *pPart, Guild *pGuild);

// Pick valid details for all aspects of the part
void costumeTailor_PickValidPartValues(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const PCSlotType *pSlotType, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bFixColors, bool bInEditor, Guild *pGuild);

void costumeTailor_MakeCostumeValid(NOCONST(PlayerCostume) *pFixUpCostume, SpeciesDef *pSpecies, PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, bool bSortDisplay, bool bUnlockAll, bool bInEditor, Guild *pGuild,
	bool bIgnoreRegCatNull, GameAccountDataExtract *pExtract, bool bUGC, const char** eapchPowerFXBones);

// Gets the category for a given region
PCCategory *costumeTailor_GetCategoryForRegion(PlayerCostume *pPCCostume, PCRegion *pRegion);

// Set the category for a given region
void costumeTailor_SetRegionCategory(NOCONST(PlayerCostume) *pPCCostume, PCRegion *pRegion, PCCategory *pCategory);

// Evaluate current parts and pick a category that works best
PCCategory *costumeTailor_PickBestCategory(PlayerCostume *pPCCostume, PCRegion *pRegion, SpeciesDef *pSpecies, PCSlotType *pSlotType, bool bIssueWarnings);

// Get the part (if any) that is the mirror of the provided part
NOCONST(PCPart) *costumeTailor_GetMirrorPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart);

// Sets the mirror part to match the current part
void costumeTailor_CopyToMirrorPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, NOCONST(PCPart) *pMirrorPart, SpeciesDef *pSpecies, int eModeFlags, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bSoftCopy);
bool costumeTailor_CopyToGroupPart(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, NOCONST(PCPart) *pGroupPart, SpeciesDef *pSpecies, int eModeFlags, PlayerCostume **eaUnlockedCostumes, bool bSortDisplay, bool bUnlockAll, bool bCopyUnmatched, bool bSoftCopy);

// Determine if assets count as mirrored
bool costumeTailor_IsMirrorGeometry(PCGeometryDef *pGeo1, PCGeometryDef *pGeo2);
bool costumeTailor_IsMirrorMaterial(PCMaterialDef *pMat1, PCMaterialDef *pMat2);

// Returns true if costume restrictions are met, false otherwise
bool costumeValidate_AreRestrictionsValid(NOCONST(PlayerCostume) *pPCCostume, PlayerCostume **ppCostumes, bool bAlwaysUnlock, char ** ppEStringError, PCBoneRef ***peaBonesUsed);

// Returns true if category choices are valid, false otherwise.
// Assigns a category if the costume currently has none for a region.
bool costumeValidate_AreCategoriesValid(NOCONST(PlayerCostume) *pPCCostume, SpeciesDef *pSpecies, PCSlotType *pSlotType, bool bCreateIfNeeded);

// Gets the flags for a texture type
PCTextureType costumeTailor_GetTextureFlags(PCTextureDef *pTexDef);

// Get the child part for a part
NOCONST(PCPart) *costumeTailor_GetPartByBone(NOCONST(PlayerCostume) *pPCCostume, PCBoneDef *pBone, PCLayer *pLayer);

// Get the child part for a part
void costumeTailor_GetChildParts(NOCONST(PlayerCostume) *pPCCostume, NOCONST(PCPart) *pPart, PCLayer *pLayer, NOCONST(PCPart) ***peaParts);

// Check if a given geometry has been unlocked
bool costumeTailor_GeometryIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCGeometryDef *pGeoDef);

// Check if a given material has been unlocked for this geometry
bool costumeTailor_MaterialIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCMaterialDef *pMatDef, PCGeometryDef *pGeoDef);

// Check if a given texture has been unlocked for this geometry and material
bool costumeTailor_TextureIsUnlocked(PlayerCostume **eaUnlockedCostumes, PCTextureDef *pTexDef, PCMaterialDef *pMatDef, PCGeometryDef *pGeoDef);

//
int costumeTailor_GetMatchingCategoryIndex(PCCategory *pCategory, PCCategory **eaCategory);
int costumeTailor_GetMatchingBoneIndex(PCBoneDef *pBone, PCBoneDef **eaBones);
int costumeTailor_GetMatchingGeoIndex(PCGeometryDef *pGeo, PCGeometryDef **eaGeos);
int costumeTailor_GetMatchingMatIndex(PCMaterialDef *pMat, PCMaterialDef **eaMats);
int costumeTailor_GetMatchingTexIndex(PCTextureDef *pTex, PCTextureDef **eaTexs);
int costumeTailor_GetMatchingBodyScaleIndex(NOCONST(PlayerCostume) *pPCCostume, PCBodyScaleInfo *pScale);

// Sort lists
void costumeTailor_SortCategories(PCCategory **eaCategories, bool bSortDisplay);
void costumeTailor_SortCategoryRefs(PCCategoryRef **eaCategories, bool bSortDisplay);
void costumeTailor_SortBones(PCBoneDef **eaBones, bool bSortDisplay);
void costumeTailor_SortGeos(PCGeometryDef **eaGeos, bool bSortDisplay);
void costumeTailor_SortMaterials(PCMaterialDef **eaMats, bool bSortDisplay);
void costumeTailor_SortTextures(PCTextureDef **eaTexs, bool bSortDisplay);

//
// Functions to set values obeying rules
//

bool costumeTailor_SetCategory(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, PCRegion *pRegion, const char *pchCategory,
								PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType,
								Guild *pGuild, bool bUnlockAll);

bool costumeTailor_SetAllMaterials(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, const char *pcMatName,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll);

bool costumeTailor_SetPartGeometry(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, PCBoneDef *pBone, const char *pchGeo,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, 
								   Guild *pGuild, bool bUnlockAll, bool bMirrorMode, bool bGroupMode);

bool costumeTailor_SetPartMaterial(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCBoneDef **eaFindBones, const char *pcMatName,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode);

bool costumeTailor_SetPartTexturePattern(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcPattern,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode);

bool costumeTailor_SetPartTextureDetail(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcDetail,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode);

bool costumeTailor_SetPartTextureSpecular(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcSpecular,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode);

bool costumeTailor_SetPartTextureDiffuse(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcDiffuse,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode);

bool costumeTailor_SetPartTextureMovable(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, const char *pcMovable,
								   PlayerCostume **eaUnlockedCostumes, PCSlotType *pSlotType, Guild *pGuild, bool bUnlockAll, bool bMirrorMode);

void costumeTailor_SetPartDefaultTextureValues(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, SpeciesDef *pSpecies, PCTextureDef *pTex, PCTextureType eTexType);

void costumeTailor_SetPartDefaultTextureColors(NOCONST(PlayerCostume) *pCostume, NOCONST(PCPart) *pPart, PCTextureDef *pTex);

bool costumeTailor_SetBodyScale(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, int iIndex, F32 fBodyScale, PCSlotType *pSlotType);
bool costumeTailor_SetBodyScaleByName(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, const char *pcScaleName, F32 fBodyScale, PCSlotType *pSlotType);

//
// *** Override function for species and slot
//

// get the min and max of this body scale from slot or species (slot is higher priority). return false if neither have this value.
bool costumeTailor_GetOverrideBodyScale(const PCSkeletonDef *pSkeleton, const char *pcName, const SpeciesDef *pSpecies, const PCSlotType *pSlotType, F32 *fMinOut, F32 *fMaxOut);

// get the min and max of this bone scale from slot or species (slot is higher priority). return false if neither have this value.
// sclae index can be < 0 in which case it will look up the skeleton index to this scale
bool costumeTailor_GetOverrideBoneScale(const PCSkeletonDef *pSkeleton, const PCScaleInfo *pScale, const char *pcName, const SpeciesDef *pSpecies, const PCSlotType *pSlotType, F32 *fMinOut, F32 *fMaxOut);

// Get the override skin color, note that slot is higher priority
UIColorSet *costumeTailor_GetOverrideSkinColorSet(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// get the min height 0.0f == not used
F32 costumeTailor_GetOverrideHeightMin(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// get the max height 0.0f == not used
F32 costumeTailor_GetOverrideHeightMax(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Get no change (used when randomly building costumes)
bool costumeTailor_GetOverrideHeightNoChange(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// get the min muscle 0.0f == not used
F32 costumeTailor_GetOverrideMuscleMin(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// get the max muscle 0.0f == not used
F32 costumeTailor_GetOverrideMuscleMax(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Get no change muscle(used when randomly building costumes)
bool costumeTailor_GetOverrideMuscleNoChange(const SpeciesDef *pSpecies, const PCSlotType *pSlotType);

// Get the gender from the species or costume
Gender costumeTailor_GetGenderFromSpeciesOrCostume(const SpeciesDef *pSpecies, const PlayerCostume *pCostume);

// Get the stance information for this slot based on gender. Returns NULL if no information on stances
const PCSlotStanceStruct *costumeTailor_GetStanceFromSlot(const PCSlotType *pSlotType, Gender eGender);

//
// *** end Override function for species and slot
//

void costumeTailor_ApplyPresetScaleValueGroup(NOCONST(PlayerCostume) *pCostume, PCPresetScaleValueGroup *pPreset);


// ---- Misc. Accessors ----

Gender costumeTailor_GetGender(PlayerCostume *pCostume);

// Get the index for a body scale value.  Returns -1 if this body scale is not used
int costumeTailor_FindBodyScaleInfoIndexByName( PlayerCostume* costume, const char* name );

// Get the PCScaleValue for a specific scale value.
const PCScaleValue* costumeTailor_FindScaleValueByName( PlayerCostume* costume, const char* name );
NOCONST(PCScaleValue)* costumeTailor_FindScaleValueByNameNoConst( NOCONST(PlayerCostume)* costume, const char* name );

// Get the PCScaleInfoGroup by its name
const PCScaleInfoGroup* costumeTailor_FindScaleGroupByName( const PCSkeletonDef* pSkel, const char* name );

// Get the PCScaleInfo by its name
const PCScaleInfo* costumeTailor_FindScaleInfoByName( const PCSkeletonDef* pSkel, const char* name );
