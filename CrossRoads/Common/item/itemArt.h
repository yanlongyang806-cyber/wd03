#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"
#include "itemEnums.h"
#include "ReferenceSystem.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

typedef struct Entity Entity;
typedef struct PowerCategory PowerCategory;
typedef struct PCFXTemp PCFXTemp;
extern DictionaryHandle g_hItemArtDict;

// Forward declarations

AUTO_STRUCT;
typedef struct ItemArtGeoData
{
	S32 eCat;		AST(NAME(Category), STRUCTPARAM, SUBTABLE(ItemPowerArtCategoryEnum) REQUIRED)
	const char *pchGeo;			AST(NAME(Name), POOL_STRING, STRUCTPARAM REQUIRED)
	const char *pchFXAdd;		AST(NAME(FXAdd), POOL_STRING, STRUCTPARAM REQUIRED)
} ItemArtGeoData;

// Basic definition of art related to an item
AUTO_STRUCT;
typedef struct ItemArt
{
	const char *cpchName;					AST(NAME(Name), STRUCTPARAM, KEY, POOL_STRING)
		// Internal name, generated at text load from cpchFile


	// Geometry data

	//TODO(CM): These should be unified.
	const char *pchGeo;						AST(NAME(Geo), POOL_STRING)
	const char *pchGeoSecondary;			AST(NAME(GeoSecondary), POOL_STRING)
	ItemArtGeoData** eaGeoList;				AST(NAME(GeoList), POOL_STRING)
	// Internal name of object library geo.

	Vec3 vColor;
	Vec4 vColor2;
	Vec4 vColor3;
	Vec4 vColor4;
	const char *pchMaterialReplace;				AST(NAME(Material), POOL_STRING)
		// Geo color params


	// Animation bit data

	const char **ppchPrimaryBits;			AST(NAME(Bits), ADDNAMES(Bit,PrimaryBit), POOL_STRING)
	const char **ppchSecondaryBits;			AST(NAME(SecondaryBits), ADDNAMES(SecondaryBit), POOL_STRING)
		// Bit added by the item to indicate it is active.
	const char **ppchSecondaryStanceWords;	AST(NAME(SecondaryStanceWords), POOL_STRING)
	const char **ppchPrimaryStanceWords;	AST(NAME(StanceWords), ADDNAMES(PrimaryStanceWords), POOL_STRING)
		// Bit added by the item to indicate it is active.


	// FX Data

	const char *pchFXAdd;					AST(NAME(FXAdd), POOL_STRING)
	const char *pchFXPrimary;				AST(NAME(FXPrimary), POOL_STRING)
	const char *pchFXSecondary;				AST(NAME(FXSecondary), POOL_STRING)

	//these override the bag settings if they exist.
	const char *pchFXItemArtActive;					AST(NAME(FXItemArtActive), POOL_STRING)
	const char *pchFXItemArtInactive;				AST(NAME(FXItemArtInactive), POOL_STRING)
	const char *pchSecondaryFXItemArtActive;		AST(NAME(SecondaryFXItemArtActive), POOL_STRING)
	const char *pchSecondaryFXItemArtInactive;		AST(NAME(SecondaryFXItemArtInactive), POOL_STRING)

	F32 fHue;
		// Optional FX hue shift
	const char *pchActivePrimaryBone;		AST(NAME(ActivePrimaryBone), POOL_STRING)
	const char *pchActiveSecondaryBone;		AST(NAME(ActiveSecondaryBone), POOL_STRING)
	Vec3 rotActive;							AST(NAME(ActiveRotation))
	Vec3 posActive;							AST(NAME(ActivePosition))

	const char *pchHolsterPrimaryBone;		AST(NAME(HolsterPrimaryBone), POOL_STRING)
	const char *pchHolsterSecondaryBone;	AST(NAME(HolsterSecondaryBone), POOL_STRING)
	Vec3 rotHolster;						AST(NAME(HolsterRotation))
	Vec3 posHolster;						AST(NAME(HolsterPosition))
		//these allow an itemart file to specify where on the character the geo will be anchored.

	bool bAlwaysShowOnBothBones;			AST(NAME(AlwaysShowOnBothBones))
	F32 fOptionalParam;

	ItemCategory *peAdditionalCategories;	AST(NAME(AdditionalCategories) SUBTABLE(ItemCategoryEnum))
		// These are the categories that would normally not go into the Item definition.
		// They are used in cases such as a shield being drawn when a melee weapon power is used.
		// In this case you would add melee category into the additional categories even 
		// though the shield is not a melee weapon.


	const char *cpchScope;					AST(NAME(Scope), POOL_STRING)
		// Internal scope, generated at text load from cpchFile

	const char *cpchFile;					AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)

} ItemArt;

// Represents the ItemArt state of a given bag.
AUTO_STRUCT;
typedef struct ItemArtBagState
{
	InvBagIDs eBagID;
		// ID of the bag

	int iSlot;
		// Slot of item in the bag, for secondary items

	REF_TO(ItemArt) hItemArt;
		// Ref to the ItemArt

	const char **ppchBits;		AST(POOL_STRING, UNOWNED)
		// Pointer copy of the EArray of names of the bits we want to add

	const char **ppchFXNames;		AST(POOL_STRING, UNOWNED)
		// Names of the FX we want to use to display the ItemArt

	const char* pchBone;		AST(POOL_STRING)

	const char* pchGeoOverride;	AST(POOL_STRING)

	Vec3 pos;
	Vec3 rot;
	//positional information and bone to attach to.

} ItemArtBagState;

// Represents the general state of an Entity's ItemArt
AUTO_STRUCT;
typedef struct EquippedArt
{
	S32 *piInvBagsReady;			AST(NO_NETSEND)
		// EArray of ready inventory bags (by ID) - bags that would be active
		//  if you were in an active state (such as in Combat mode)

	ItemArtBagState **ppState;		AST(SELF_ONLY)
		// Current state for visible bags

	U8 bActive : 1;					AST(NO_NETSEND)
		// Whether or not the state represents the "active" style, saved so
		//  that it doesn't change while you're dead

	U8 bCanUpdate : 1;				AST(NO_NETSEND)
		// Whether or not the state is allowed to change, used to prevent multiple changes per frame

	DirtyBit dirtyBit;				AST(NO_NETSEND)

} EquippedArt;


// Clears the existing ready bags, finds Items for the PowerCategory and readies those
//  bags.  Updates the ItemArt state if the list ends up changing.
void entity_ReadyItemsForPowerCat(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerCategory *pCat);

// Updates the Entity's ItemArt state based on the ready bags and active state.
//  If there are no ready bags, generates the default ready bags list.
void entity_UpdateItemArtAnimFX(SA_PARAM_NN_VALID Entity *pEnt);

bool gclEntity_UpdateItemArtAnimFX(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CharacterClass *pClass, PCFXTemp ***peaFX, bool bForceUpdate);
PCFXTemp* gclItemArt_GetItemPreviewFX( SA_PARAM_NN_VALID ItemArt* pArt);