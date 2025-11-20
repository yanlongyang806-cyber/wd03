#pragma once
GCC_SYSTEM

#include "UICore.h"
#include "Color.h"
#include "CBox.h"

typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;
typedef struct StaticDefineInt StaticDefineInt;
typedef struct NinePatch NinePatch;

typedef struct UITextureAssembly UITextureAssembly;
typedef struct UITextureInstancePosition UITextureInstancePosition;
typedef struct UITextureInstance UITextureInstance;

extern StaticDefineInt UISizeEnum[];

AUTO_STRUCT;
typedef struct UITextureInstancePosition
{
	const char *pchRelative; AST(POOL_STRING STRUCTPARAM)
	U16 eDirection; AST(STRUCTPARAM SUBTABLE(UIDirectionEnum))
	F32 fOffset; AST(STRUCTPARAM)
	UITextureInstance *pRelative; NO_AST
} UITextureInstancePosition;
extern ParseTable parse_UITextureInstancePosition[];
#define TYPE_parse_UITextureInstancePosition UITextureInstancePosition

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UITextureInstanceFallback
{
	UIDirection eDirection; AST(STRUCTPARAM)
	F32 fOffset; AST(STRUCTPARAM)
	const char **eachFallback; AST(POOL_STRING RESOURCEDICT(Texture) NAME(Fallback))
	void **eaTextures; NO_AST
} UITextureInstanceFallback;
extern ParseTable parse_UITextureInstanceFallback[];
#define TYPE_parse_UITextureInstanceFallback UITextureInstanceFallback

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UITextureInstance
{
	const char *pchKey; AST(STRUCTPARAM POOL_STRING KEY) // RESOURCEDICT(Texture) JE: Removed this because it implies some name fixup which is undesirable
	const char *pchTexture; AST(STRUCTPARAM POOL_STRING RESOURCEDICT(Texture))

	REF_TO(UITextureAssembly) hSubAssembly; AST(NAME(SubAssembly))

	UITextureInstancePosition LeftFrom;
	UITextureInstancePosition RightFrom;
	UITextureInstancePosition TopFrom;
	UITextureInstancePosition BottomFrom;
	UITextureInstancePosition HorizontalCenterFrom;
	UITextureInstancePosition VerticalCenterFrom;

	UITextureInstanceFallback *pFallback;

	U32 uiTopLeftColor; AST(NAME(TopLeftColor, Color, TopColor, LeftColor) SUBTABLE(ColorEnum))
	U32 uiTopRightColor; AST(NAME(TopRightColor, RightColor) SUBTABLE(ColorEnum))
	U32 uiBottomLeftColor; AST(NAME(BottomLeftColor, BottomColor) SUBTABLE(ColorEnum))
	U32 uiBottomRightColor; AST(NAME(BottomRightColor) SUBTABLE(ColorEnum))

	F32 fScaleHorizontal; AST(DEFAULT(1) NAME(ScaleHorizontal, HorizontalScale))
	F32 fScaleVertical; AST(DEFAULT(1) NAME(ScaleVertical, VerticalScale))

	UIAngle Rotation;

	U8 iZ;
	UITextureMode eModeVertical : UITextureMode_NUMBITS; AST(NAME(VerticalMode, ModeVertical) DEFAULT(1))
	UITextureMode eModeHorizontal : UITextureMode_NUMBITS; AST(NAME(HorizontalMode, ModeHorizontal) DEFAULT(1))
	UIDirection eAlignment : UIDirection_NUMBITS;

	U8 iMouseRegion; 

	bool bResetScale : 1; AST(NAME(ScaleReset, ResetScale))
	bool bResetColor: 1; AST(NAME(ColorReset, ResetColor))
	bool bIsSubAssembly : 1;
	bool bAdditive : 1;
	bool bResetAdditive : 1;  AST(NAME(AdditiveReset, ResetAdditive))
	bool bIsAtlas : 1; NO_AST
	bool bFlipX : 1;
	bool bFlipY : 1;
	bool bClip : 1;

	union
	{
		BasicTexture *pTexture; NO_AST
		AtlasTex *pAtlasTexture; NO_AST
	};

	const NinePatch *pNinePatch; NO_AST

	CBox Box; NO_AST

} UITextureInstance;
extern ParseTable parse_UITextureInstance[];
#define TYPE_parse_UITextureInstance UITextureInstance

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UITextureAssembly
{
	const char *pchName; AST(STRUCTPARAM KEY REQUIRED)
	UITextureInstance **eaTextures; AST(NAME(Texture, Textures) NO_INDEX)

	UITextureInstance **eaSortedByDependency; AST(UNOWNED NO_WRITE NO_INDEX)
		// Sorted by layout dependency order.
	UITextureInstance **eaSortedByDrawZ; AST(UNOWNED NO_WRITE NO_INDEX)
		// Sorted by draw order.

	S8 iPaddingTop; AST(NAME(PaddingTop, TopPadding) SUBTABLE(UISizeEnum))
	S8 iPaddingBottom; AST(NAME(PaddingBottom, BottomPadding) SUBTABLE(UISizeEnum))
	S8 iPaddingLeft; AST(NAME(PaddingLeft, LeftPadding) SUBTABLE(UISizeEnum))
	S8 iPaddingRight; AST(NAME(PaddingRight, RightPadding) SUBTABLE(UISizeEnum))

	S8 iClipMarginTop; AST(NAME(MarginClipTop, ClipMarginTop, TopClipMargin) SUBTABLE(UISizeEnum))
	S8 iClipMarginBottom; AST(NAME(MarginClipBottom, ClipMarginBottom, BottomClipMargin) SUBTABLE(UISizeEnum))
	S8 iClipMarginLeft; AST(NAME(MarginClipLeft, ClipMarginLeft, LeftClipMargin) SUBTABLE(UISizeEnum))
	S8 iClipMarginRight; AST(NAME(MarginClipRight, ClipMarginRight, RightClipMargin) SUBTABLE(UISizeEnum))

	S8 v2chHorizontalMultiple[2]; AST(NAME(HorizontalMultiple))
	S8 v2chVerticalMultiple[2]; AST(NAME(VerticalMultiple))

	U8 iMaxZ;

	// Multiplicative color fade across the entire assembly.
	Color4 Colors; AST(STRUCT(parse_Color4) ADDNAMES(Color, Color4, Tint))

	UIStyleBorder *pBorder;

	const char *pchFilename; AST(CURRENTFILE)
} UITextureAssembly;
extern ParseTable parse_UITextureAssembly[];
#define TYPE_parse_UITextureAssembly UITextureAssembly

void ui_TextureAssemblyDrawEx(UITextureAssembly *pTexAs, const CBox *pBox, CBox *pOutBox, F32 fScale, F32 fMinZ, F32 fMaxZ, char chAlpha, Color4 *pTint, F32 screenDist, bool is_3D);
#define ui_TextureAssemblyDraw(pTexAs, pBox, pOutBox, fScale, fMinZ, fMaxZ, chAlpha, pTint) ui_TextureAssemblyDrawEx(pTexAs, pBox, pOutBox, fScale, fMinZ, fMaxZ, chAlpha, pTint, 0.0, false)
void ui_TextureAssemblyDrawRot(UITextureAssembly *pTexAs, const CBox *pBox, float centerX, float centerY, float rot, CBox *pOutBox, F32 fScale, F32 fMinZ, F32 fMaxZ, char chAlpha, Color4 *pTint);

UITextureInstance *ui_TextureAssemblyGetMouseRegion(UITextureAssembly *pTexAs);

#define ui_TextureAssemblyTopSize(pTexAs) ((pTexAs) ? (pTexAs)->iPaddingTop : 0)
#define ui_TextureAssemblyBottomSize(pTexAs) ((pTexAs) ? (pTexAs)->iPaddingBottom : 0)
#define ui_TextureAssemblyLeftSize(pTexAs) ((pTexAs) ? (pTexAs)->iPaddingLeft : 0)
#define ui_TextureAssemblyRightSize(pTexAs) ((pTexAs) ? (pTexAs)->iPaddingRight : 0)
#define ui_TextureAssemblyWidth(pTexAs) ((pTexAs) ? ((pTexAs)->iPaddingLeft + (pTexAs)->iPaddingRight) : 0)
#define ui_TextureAssemblyHeight(pTexAs) ((pTexAs) ? ((pTexAs)->iPaddingTop + (pTexAs)->iPaddingBottom) : 0)

#define ui_TextureInstanceAtlasTex(pTexInt) ((pTexInt)->bIsAtlas ? (pTexInt)->pAtlasTexture : NULL)
#define ui_TextureInstanceBasicTex(pTexInt) ((pTexInt)->bIsAtlas ? NULL : (pTexInt)->pTexture)
