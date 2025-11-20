#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "MicroTransactionUI.h"

typedef struct CostumeEditLine CostumeEditLine;
typedef struct CostumeUIScaleGroup CostumeUIScaleGroup;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PCBodyScaleInfo PCBodyScaleInfo;
typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct UnlockMetaData UnlockMetaData;
typedef enum CostumeEditLineType CostumeEditLineType;

AUTO_ENUM;
typedef enum CostumeEditLineType {
	kCostumeEditLineType_Invalid		= 0,		EIGNORE
	kCostumeEditLineType_Divider		= 1 << 0,
	kCostumeEditLineType_Region			= 1 << 1,
	kCostumeEditLineType_Category		= 1 << 2,
	kCostumeEditLineType_Bone			= 1 << 3,
	kCostumeEditLineType_Geometry		= 1 << 4,
	kCostumeEditLineType_Material		= 1 << 5,
	kCostumeEditLineType_Texture0		= 1 << 6,
	kCostumeEditLineType_Texture1		= 1 << 7,
	kCostumeEditLineType_Texture2		= 1 << 8,
	kCostumeEditLineType_Texture3		= 1 << 9,
	kCostumeEditLineType_Texture4		= 1 << 10,
	kCostumeEditLineType_TextureScale	= 1 << 11,
	kCostumeEditLineType_Scale			= 1 << 12,
	kCostumeEditLineType_BodyScale		= 1 << 13,
	kCostumeEditLineType_Overlay		= 1 << 14,
	kCostumeEditLineType_GuildOverlay	= 1 << 15,
} CostumeEditLineType;

AUTO_ENUM;
typedef enum CostumeUIBodyScaleRule {
	kCostumeUIBodyScaleRule_Disabled = 0,
	kCostumeUIBodyScaleRule_AfterOverlays = 1,
	kCostumeUIBodyScaleRule_AfterLastRegionHeader = 2,
	kCostumeUIBodyScaleRule_AfterRegions = 3,
	kCostumeUIBodyScaleRule_AfterScaleInfoGroups = 4,
} CostumeUIBodyScaleRule;

AUTO_STRUCT;
typedef struct CostumeUIScaleGroup {
	const char* pcName;				AST( POOL_STRING )
} CostumeUIScaleGroup;

AUTO_STRUCT;
typedef struct CostumeEditLine
{
	const char *pcName;               AST( KEY POOL_STRING )
	const char *pcName2;		      AST( POOL_STRING )
	DisplayMessage displayNameMsg;    AST(STRUCT(parse_DisplayMessage))
	DisplayMessage displayNameMsg2;   AST(STRUCT(parse_DisplayMessage))

	F32 fScaleValue1;
	F32 fScaleValue2;
	F32 fScaleMin1;
	F32 fScaleMin2;
	F32 fScaleMax1;
	F32 fScaleMax2;

	CostumeEditLineType iType;
	CostumeEditLineType iMergeType;

	PCTextureDef *pCurrentTex;			AST(UNOWNED)
	PCMaterialDef *pCurrentMat;			AST(UNOWNED)
	PCGeometryDef *pCurrentGeo;			AST(UNOWNED)
	PCBoneDef *pCurrentBone;			AST(UNOWNED)
	PCCategory *pCurrentCat;			AST(UNOWNED)
	PCRegion *pCurrentRegion;			AST(UNOWNED)
	PCBodyScaleValue *pCurrentValue;	AST(UNOWNED)
	CostumeRefForSet *pCurrentOverlay;	AST(UNOWNED)
	PlayerCostumeHolder *pCurrentGuildOverlay;	AST(UNOWNED)

	REF_TO(PCMaterialDef) hOwnerMat;
	REF_TO(PCGeometryDef) hOwnerGeo;
	REF_TO(PCBoneDef) hOwnerBone;
	REF_TO(PCCategory) hOwnerCat;
	REF_TO(PCRegion) hOwnerRegion;

	PCTextureDef **eaTex;				AST(UNOWNED)
	PCMaterialDef **eaMat;				AST(UNOWNED)
	PCGeometryDef **eaGeo;				AST(UNOWNED)
	PCBoneDef **eaBone;					AST(UNOWNED)
	PCCategory **eaCat;					AST(UNOWNED)
	PCRegion **eaRegion;				AST(UNOWNED)
	PCBodyScaleValue **eaValues;		AST(UNOWNED)
	CostumeRefForSet **eaOverlays;		AST(UNOWNED)
	PlayerCostumeHolder **eaGuildOverlays;	AST(UNOWNED)

	U8 bColor0Allowed : 1;
	U8 bColor1Allowed : 1;
	U8 bColor2Allowed : 1;
	U8 bColor3Allowed : 1;
	U8 bHasSlider : 1; //If Scale type then this says if we have second slider

	// Used as scratch space by editors
	const char *pcSysName;				AST(NAME("SysName") POOL_STRING) 
	F32 fTempValue1;					AST(NAME("Value1"))
	F32 fTempValue2;					AST(NAME("Value2"))

	UnlockMetaData *pUnlockInfo;		AST(UNOWNED)
} CostumeEditLine;

AUTO_STRUCT;
typedef struct CostumeSubListRow
{
	const char *pcName;					AST(UNOWNED)
	const char *pcDisplayName;			AST(UNOWNED)
	CostumeEditLineType iType;
	CostumeEditLine *pLine;				AST(NAME(line) UNOWNED)
	bool bPlayerInitial;

	PCTextureDef *pTex;					AST(UNOWNED)
	PCMaterialDef *pMat;				AST(UNOWNED)
	PCGeometryDef *pGeo;				AST(UNOWNED)
	PCBoneDef *pBone;					AST(UNOWNED)
	PCCategory *pCat;					AST(UNOWNED)
	PCBodyScaleValue *pValue;			AST(UNOWNED)
	CostumeRefForSet *pOverlay;			AST(UNOWNED)
	PlayerCostumeHolder *pGuildOverlay;	AST(UNOWNED)

	UnlockMetaData *pUnlockInfo;		AST(UNOWNED)

	// This contains the MicroTransaction product details
	// specifically for this row, but only if UnlockInfo
	// defines FullProductList.
	MicroTransactionUIProduct *pProduct;	AST(UNOWNED)
} CostumeSubListRow;


void costumeLineUI_UpdateLines(NOCONST(PlayerCostume) *pCostume, CostumeEditLine ***peaCostumeEditLines,
						       SpeciesDef *pSpecies, PCSkeletonDef *pSkeleton,
						       CostumeEditLineType eFindTypes, int iBodyScalesRule, PCRegionRef ***peaFindRegions, CostumeUIScaleGroup **eaFindScaleGroup,
   						       PCBodyScaleInfo **eaBodyScalesInclude, PCBodyScaleInfo **eaBodyScalesExclude, const char **eaIncludeBones, const char **eaExcludeBones,
						       PCSlotType *pSlotType, const char *pcCostumeSet, bool bLineListHideMirrorBones, bool bUnlockAll, bool bMirrorSelectMode, bool bGroupSelectMode, bool bCountNone, bool bOmitHasOnlyOne, bool bCombineLines, bool bTextureLinesForCurrentPartValuesOnly,
						       PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones);
int costumeLineUI_GetCostumeEditSubLineListSizeInternal(SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType);
bool costumeLineUI_SetLineItemInternal(NOCONST(PlayerCostume) *pCostume, SpeciesDef *pSpecies, 
										const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType,
										PlayerCostume **eaUnlockedCostumes, const char **eaPowerFXBones, PCSlotType *pSlotType, 
										Guild *pGuild, GameAccountDataExtract *pExtract, bool bUnlockAll, bool bMirrorMode, bool bGroupMode);
bool costumeLineUI_SetHoverLineMergeItemInternal(const char *pchSysName, SA_PARAM_OP_VALID CostumeEditLine *el, CostumeEditLineType iType);
void costumeLineUI_SetColorAtLine(S32 iColor, SA_PARAM_OP_VALID CostumeEditLine *el, F32 fR, F32 fG, F32 fB, F32 fA);
bool costumeLineUI_SetLineScale(SA_PARAM_OP_VALID CostumeEditLine *el, int scaleNum, F32 fValue);
void costumeLineUI_SetAllowedLineTypes(int types);
void costumeLineUI_SetBodyScalesRule(int rule);
bool costumeLineUI_SetLineScaleInternal(NOCONST(PlayerCostume) *pCostume, SA_PARAM_OP_VALID CostumeEditLine *el, int scaleNum, F32 fValue);
void costumeLineUI_FillUnlockInfo(CostumeEditLine **eaCostumeEditLines, StashTable stashGeoUnlockMeta, StashTable stashMatUnlockMeta, StashTable stashTexUnlockMeta);

void costumeLineUI_DestroyLine(CostumeEditLine *pLine);
void costumeLineUI_DestroyLines(CostumeEditLine ***peaLines);
