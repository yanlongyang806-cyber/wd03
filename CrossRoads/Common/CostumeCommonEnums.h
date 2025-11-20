/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

// Separated from PlayerCostume.h so we can avoid incomplete type errors

#pragma once

// ---- Enumerations --------------------------------------------------

AUTO_ENUM;
typedef enum kCostumeDisplayMode {
	kCostumeDisplayMode_Overlay = 0,
	kCostumeDisplayMode_Overlay_Always, //Ignore skeleton matching
	kCostumeDisplayMode_Replace_Match,
	kCostumeDisplayMode_Replace_Always,
	kCostumeDisplayMode_Unlock
} kCostumeDisplayMode;
extern StaticDefineInt kCostumeDisplayModeEnum[];

AUTO_ENUM;
typedef enum kCostumeDisplayType {
	kCostumeDisplayType_Costume = 0,
	kCostumeDisplayType_Replace_Attached,
	kCostumeDisplayType_Value_Change
} kCostumeDisplayType;
extern StaticDefineInt kCostumeDisplayTypeEnum[];

AUTO_ENUM;
typedef enum kCostumeValueArea {
	kCostumeValueArea_Height = 0,
	kCostumeValueArea_Mass,
	kCostumeValueArea_Transparency
} kCostumeValueArea;
extern StaticDefineInt kCostumeValueAreaEnum[];

AUTO_ENUM;
typedef enum kCostumeValueMode {
	kCostumeValueMode_Increment_Value = 0,
	kCostumeValueMode_Scale_Value,
	kCostumeValueMode_Set_Value
} kCostumeValueMode;
extern StaticDefineInt kCostumeValueModeEnum[];

AUTO_ENUM;
typedef enum PCCostumeStorageType {
	// Primary costumes are what players use
	kPCCostumeStorageType_Primary,
	// Secondary costumes are not currently used by anything
	kPCCostumeStorageType_Secondary,
	// Pet type is used to alter a pet
	kPCCostumeStorageType_Pet,
	// Space pet is a type used to alter a space puppet while on the ground
	kPCCostumeStorageType_SpacePet,
	// Used by the Tailor to pick a Nemesis
	kPCCostumeStorageType_Nemesis,

	kPCCostumeStorageType_Count, EIGNORE
} PCCostumeStorageType;
extern StaticDefineInt PCCostumeStorageTypeEnum[];
