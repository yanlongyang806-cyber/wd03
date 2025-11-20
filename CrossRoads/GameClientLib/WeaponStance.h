#pragma once

#include "stdtypes.h"
#include "CostumeCommon.h"
#include "PowerAnimFX.h"

#define WEAPONSTANCE_BASE_DIR "defs/weaponstances"
#define WEAPONSTANCE_EXTENSION ".weaponstance"

typedef struct PCBoneDef PCBoneDef;

AUTO_STRUCT;
typedef struct TailorWeaponStance
{
	REF_TO(PCBoneDef) hBoneDef;				AST(NAME(Bone) KEY)
	const char **ppchStanceStickyBits;		AST(NAME(StanceStickyBits), POOL_STRING)
	const char **pchStanceStickyFX;			AST(NAME(StanceStickyFX), POOL_STRING)

	// Defaults
	// This is when "None" is selected
	const char *pchDefaultGeo;				AST(NAME(DefaultGeo), POOL_STRING)
	DisplayMessage *pDefaultName;			AST(NAME(DefaultName))

	// Colors to display when the geo colors are linked
	Vec3 vColor0;							AST(NAME(Color0))
	Vec4 vColor1;							AST(NAME(Color1))
	Vec4 vColor2;							AST(NAME(Color2))
	Vec4 vColor3;							AST(NAME(Color3))

	PowerFXParam **eaParams;				AST(NAME(StanceStickyFXParamBlock) REDUNDANT_STRUCT(StanceStickyFXParam, parse_PowerFXParam_StructParam))

	const char *pchFilename;				AST(CURRENTFILE)
} TailorWeaponStance;

TailorWeaponStance* WeaponStace_GetStanceForBone(PCBoneDef* pBoneDef);

void TailorWeaponStance_load(void);