#pragma once
GCC_SYSTEM

#include "dynBitField.h"
#include "GlobalTypes.h"
#include "UIGen.h"
#include "gclUIGen.h"
#include "dynFxInfo.h"

typedef struct BasicTexture BasicTexture;
typedef struct Entity Entity;
typedef struct HeadshotStyleDef HeadshotStyleDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct WLCostume WLCostume;
typedef struct ExprContext ExprContext;
typedef struct Item Item;
typedef struct ItemArt ItemArt;
typedef struct CostumeDisplayData CostumeDisplayData;
typedef struct PaperdollHeadshotData PaperdollHeadshotData;
typedef struct AIAnimList AIAnimList;

AUTO_ENUM;
typedef enum UIGenHeadshotMode
{
	kUIGenHeadshotMode_Stretched,
	kUIGenHeadshotMode_Scaled,
	kUIGenHeadshotMode_Filled,
} UIGenHeadshotMode;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenPaperdoll
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypePaperdoll))
	Expression* pHeadshotExpr;  AST(NAME(Headshot) REDUNDANT_STRUCT(HeadshotExpr, parse_Expression_StructParam) LATEBIND)
	const char* pchBackgroundTexture; AST(NAME(BackgroundTexture) POOL_STRING)
	const char* pchMaskTexture;	AST(NAME(MaskTexture) POOL_STRING)
	const char* pchAnimBits; AST(NAME(animBits))
	const char* pcAnimKeyword; AST(NAME(AnimKeyword) POOL_STRING)
	const char* pchHeadshotStyle; AST(NAME(HeadshotStyle) POOL_STRING)
	const char* pchSky; AST(NAME(Sky) POOL_STRING)
	U32 uBackgroundColor; AST(NAME(BackgroundColor) SUBTABLE(ColorEnum) FORMAT_COLOR)
	U16 uRenderWidth; AST(NAME(RenderWidth) DEFAULT(128))
	U16 uRenderHeight; AST(NAME(RenderHeight) DEFAULT(128))
	F32 fRotation; AST(NAME(Rotation))
	F32 fFOVy; AST(NAME(FieldOfView, FOV) DEFAULT(-1))
	UIGenHeadshotMode eMode; AST(NAME(Mode))
	U32 bAnimated : 1; AST(NAME(Animated))
	U32 bUpdateCamera : 1;	AST(NAME(UpdateCamera))
	const char* pchFrame; AST(NAME(Frame) POOL_STRING)

	// With UpdateCamera, will use the bounding box to compute the center instead of centering on the origin.
	U32 bAutoCenter : 1; AST(NAME(AutoCenter))
	U32 bUpdateExtentsOnce : 1; AST(NAME(UpdateExtentsOnce))
	U32 bHeadshotFocus : 1; AST(NAME(HeadshotFocus))
	U32 bLowerRenderPriority : 1; AST(NAME(LowerRenderPriority))
	U32 bRenderStaleHeadshots : 1; AST(NAME(RenderStaleHeadshots))
} UIGenPaperdoll;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenPaperdollState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypePaperdoll))
	BasicTexture* pTexture; AST(UNOWNED SUBTABLE(parse_BasicTexture) ADDNAMES(HeadshotTexture))
	BasicTexture* pStaleTexture; AST(UNOWNED SUBTABLE(parse_BasicTexture) ADDNAMES(StaleHeadshotTexture))
	AtlasTex* pMask; NO_AST
	BasicTexture* pBackground; AST(UNOWNED SUBTABLE(parse_BasicTexture) ADDNAMES(BackgroundTexture))
	PlayerCostume* pCostume; NO_AST
	REF_TO(WLCostume) hCostume;
	REF_TO(HeadshotStyleDef) hHeadshotStyle;
	REF_TO(Entity) hEntity;
	ContainerRef EntityContainerRef; NO_AST
	DynBitFieldGroup BitFieldGroup; NO_AST
	const char *pcAnimKeyword;
	char *pchLastBits;
	char *pchLastBackground;
	U32 uWLCostumeId;
	const char *pchWLCostumeName; AST(POOL_STRING)
	U32 uLastUpdateTime;
	U32 uDisplayDataTime;
	CostumeDisplayData *pDisplayData; NO_AST

	U32 bRedraw : 1;
	U32 bUseSkeletonRadius : 1;
	U32 bAnimating : 1;
	U32 bWasAnimating : 1;
	U32 bCreated : 1;
	U32 bExtentsInitialized : 1;
	U32 bUpdateCamera : 1;

	// Camera properties
	F32 fCachedEntityHeight;
	F32 fRadius;
	F32 fZoom;
	F32 fZoomHeight;
	F32 fPitch;
	F32 fYaw;

	PaperdollHeadshotData **eaHeadshotData;
	S32 iUsedHeadshotData;

	PCFXTemp **eaExtraFX;
	ItemArt* pItemArt; AST(UNOWNED)
	const char** ppchAddedFX; AST(POOL_STRING)
	Vec3 v3ExtentsMin;
	Vec3 v3ExtentsMax;

	Vec3 v3LastCamPos;
	Vec3 v3LastCamDir;
} UIGenPaperdollState;

AUTO_STRUCT;
typedef struct PaperdollHeadshotData
{
	const char* pchHeadshotStyle; AST(UNOWNED)
	WLCostume* pWLCostume; AST(UNOWNED)
	PlayerCostume* pPlayerCostume; AST(UNOWNED)
	Entity* pEntity; AST(UNOWNED)
	ItemArt* pItemArt; AST(UNOWNED)
	const char** ppchAddedFX; AST(POOL_STRING)
	AIAnimList *pAnimList; AST(UNOWNED)
} PaperdollHeadshotData;

extern PaperdollHeadshotData* gclPaperdoll_CreateHeadshotData(ExprContext* pContext, WLCostume* pWLCostume, PlayerCostume* pPlayerCostume, Entity* pEntity, ItemArt* pArt, const char* pchHeadshotStyle, const char*** ppchAddedFX);
