#pragma once
GCC_SYSTEM

#include "dynFxInfo.h"
#include "wlCostume.h"

typedef struct DynCostumeMaterialParameters {
	const char *pcMaterial;
	const char *pcTexture1New;
	const char *pcTexture1Old;
	const char *pcTexture2New;
	const char *pcTexture2Old;
	const char *pcTexture3New;
	const char *pcTexture3Old;
	const char *pcTexture4New;
	const char *pcTexture4Old;
	Vec4 vColor0;
	Vec4 vColor1;
	Vec4 vColor2;
	Vec4 vColor3;

	U32 bSetMaterial	: 1;
	U32 bSetTexture1New	: 1;
	U32 bSetTexture1Old	: 1;
	U32 bSetTexture2New	: 1;
	U32 bSetTexture2Old	: 1;
	U32 bSetTexture3New	: 1;
	U32 bSetTexture3Old : 1;
	U32 bSetTexture4New	: 1;
	U32 bSetTexture4Old	: 1;
	U32 bSetColor0		: 1;
	U32 bSetColor1		: 1;
	U32 bSetColor2		: 1;
	U32 bSetColor3		: 1;
} DynCostumeMaterialParameters;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynCostumePartTextureSwap {
	const char *pcOldTexture;	AST(POOL_STRING)
	const char *pcNewTexture;	AST(POOL_STRING)
} DynCostumePartTextureSwap;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynCostumePartColorSwap {
	const char *pcName;		AST(POOL_STRING)
	const U32 uiValue[4];	AST(NAME("Value"))
} DynCostumePartColorSwap;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynCostumeScaleSetting {
	const char *pcName;	AST(POOL_STRING)
	const Vec3 vValue;	AST(NAME("Value"))
} DynCostumeScaleSetting;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynCostumePart
{
	const char* pchBoneName;							AST(STRUCTPARAM POOL_STRING)
		// The name of the bone this costume part is attached to
	const char* pchGeometry;							AST(POOL_STRING)
		// The name of the geometry this costume part uses, if null it's the default of the costume
	const char* pcModel;								AST(POOL_STRING)
		// Override the default piece of geometry attached to the bone with this model in the geometry
	const char* pchMaterial;							AST(POOL_STRING)

	DynCostumePartTextureSwap	**eaTextureSwaps;	AST(NAME(TextureSwap))
	DynCostumePartColorSwap		**eaColorSwaps;		AST(NAME(UseColor))

	bool bUseAltParams; AST(BOOLFLAG)
}
DynCostumePart;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynSubCostumeInfo
{
	const char *pcSkelInfo;			AST(POOL_STRING REQUIRED)
	const char *pcAttachmentBone;	AST(POOL_STRING REQUIRED)
	DynCostumePart			**eaCostumeParts;	AST(NAME(CostumePart))
	DynCostumeScaleSetting	**eaScaleSettings;	AST(NAME(ScaleSetting))
}
DynSubCostumeInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynCostumeInfo
{
	const char* pcInfoName;	AST(KEY POOL_STRING)
	const char* pcFileName;	AST(CURRENTFILE POOL_STRING)
	const char* pcSkelInfo;	AST(POOL_STRING REQUIRED)
	DynCostumePart			**eaCostumeParts;	AST(NAME(CostumePart))
	DynCostumeScaleSetting	**eaScaleSettings;	AST(NAME(ScaleSetting))
	DynSubCostumeInfo		**eaSubCostumes;	AST(NAME(SubCostume))
}
DynCostumeInfo;

void dynCostumeInfoLoadAll(void);
WLCostume *dynCostumeFetchOrCreateFromFxCostume(const char *, DynFxCostume *, DynParamBlock *, DynParamBlock *);