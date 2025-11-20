/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#pragma once
GCC_SYSTEM

#include "Capsule.h"
#include "Message.h"
#include "CostumeCommonEnums.h"
#include "GlobalTypeEnum.h"

typedef struct AllegianceDef AllegianceDef;
typedef struct Expression Expression;
typedef struct PlayerCostume PlayerCostume;

// ---- Enumerations --------------------------------------------------

AUTO_ENUM;
typedef enum PCTextureType {
	kPCTextureType_Pattern  = 1 << 0,
	kPCTextureType_Detail   = 1 << 1,
	kPCTextureType_Specular = 1 << 2,
	kPCTextureType_Diffuse  = 1 << 3,
	kPCTextureType_Movable  = 1 << 4,
	kPCTextureType_Other    = 1 << 5,
} PCTextureType;
extern StaticDefineInt PCTextureTypeEnum[];

// Colors on costume parts can be linked so that changes to one should affect others
AUTO_ENUM;
typedef enum PCColorLink {
	kPCColorLink_None,     // Not linked to any other part
	kPCColorLink_All,      // Linked to all other parts with ALL linkage
	kPCColorLink_Mirror,   // Linked to the other left/right part
	kPCColorLink_Group,	   // Linked to the other parts of the same bone group
	kPCColorLink_MirrorGroup,   // Linked to the other left/right part and to the other parts of the same bone group
	kPCColorLink_Different // This is a state used only in the tailor and should not be preserved 
} PCColorLink;
extern StaticDefineInt PCColorLinkEnum[];

AUTO_ENUM;
typedef enum PCColorFlags {
	kPCColorFlags_Color0 = 1 << 0,
	kPCColorFlags_Color1 = 1 << 1,
	kPCColorFlags_Color2 = 1 << 2,
	kPCColorFlags_Color3 = 1 << 3,
} PCColorFlags;
extern StaticDefineInt PCColorFlagsEnum[];

// WARNING: THIS ENUM IS BOTH PERSISTED FOR PLAYERS AND
// IS EXPORTED INTO FILES ON PLAYER PCs.  ALL CHANGES TO THIS MUST BE
// BACKWARD COMPATIBLE OR MIGRATION CODE NEEDS TO BE BUILT IN ON LOAD
// IN SEVERAL PLACES.
AUTO_ENUM;
typedef enum PCCostumeType {
	kPCCostumeType_NPC,
	kPCCostumeType_NPCObject,
	kPCCostumeType_Player,   ENAMES(Player Hero Villain)
	kPCCostumeType_Item,
	kPCCostumeType_Overlay,
	kPCCostumeType_Unrestricted,
	kPCCostumeType_UGC,
} PCCostumeType;
extern StaticDefineInt PCCostumeTypeEnum[];

// Note that this is stored in a U8 so max of 8 bits before structure changes are required
AUTO_ENUM;
typedef enum PCRestriction {
	kPCRestriction_NPC             = 1 << 0,
	kPCRestriction_NPCObject       = 1 << 1,
	kPCRestriction_Player          = 1 << 2,
	kPCRestriction_Player_Initial  = 1 << 3,
	kPCRestriction_UGC             = 1 << 4,
	kPCRestriction_UGC_Initial     = 1 << 5,
} PCRestriction;
extern StaticDefineInt PCRestrictionEnum[];

AUTO_ENUM;
typedef enum PCLayerArea {
	kPCLayerArea_Main,
	kPCLayerArea_Child
} PCLayerArea;

AUTO_ENUM;
typedef enum PCLayerType {
	kPCLayerType_All,
	kPCLayerType_Front,
	kPCLayerType_Back,
	kPCLayerType_Left,
	kPCLayerType_Right
} PCLayerType;

AUTO_ENUM;
typedef enum PCEditMode {
	kPCEditMode_Both,
	kPCEditMode_Left,
	kPCEditMode_Right,
	kPCEditMode_Front,
	kPCEditMode_Back
} PCEditMode;

AUTO_ENUM;
typedef enum PCRegionType {
	kPCRegionType_Ground,
	kPCRegionType_Space
} PCRegionType;

AUTO_ENUM;
typedef enum PCEditColor
{
	kPCEditColor_Color0,
	kPCEditColor_Color1,
	kPCEditColor_Color2,
	kPCEditColor_Color3,
	kPCEditColor_Skin = 100,
	kPCEditColor_SharedColor0 = 110,
	kPCEditColor_SharedColor1 = 111,
	kPCEditColor_SharedColor2 = 112,
	kPCEditColor_SharedColor3 = 113,
	kPCEditColor_PerPartColor0 = 120,
	kPCEditColor_PerPartColor1 = 121,
	kPCEditColor_PerPartColor2 = 122,
	kPCEditColor_PerPartColor3 = 123,
} PCEditColor;

// WARNING: THIS ENUM IS BOTH PERSISTED FOR PLAYERS AND
// IS EXPORTED INTO FILES ON PLAYER PCs.  ALL CHANGES TO THIS MUST BE
// BACKWARD COMPATIBLE OR MIGRATION CODE NEEDS TO BE BUILT IN ON LOAD
// IN SEVERAL PLACES.
AUTO_ENUM;
typedef enum PCControlledRandomLock {
	kPCControlledRandomLock_Geometry     = 1 << 0,     ENAMES(Geometry)
	kPCControlledRandomLock_Material     = 1 << 1,     ENAMES(Material)
	kPCControlledRandomLock_Pattern      = 1 << 2,     ENAMES(Pattern)
	kPCControlledRandomLock_Detail       = 1 << 3,     ENAMES(Detail)
	kPCControlledRandomLock_Specular     = 1 << 4,     ENAMES(Specular)
	kPCControlledRandomLock_Diffuse      = 1 << 5,     ENAMES(Diffuse)
	kPCControlledRandomLock_Movable      = 1 << 6,    ENAMES(Movable)
	kPCControlledRandomLock_Color0       = 1 << 7,     ENAMES(Color0)
	kPCControlledRandomLock_Color1       = 1 << 8,     ENAMES(Color1)
	kPCControlledRandomLock_Color2       = 1 << 9,     ENAMES(Color2)
	kPCControlledRandomLock_Color3       = 1 << 10,     ENAMES(Color3)
	kPCControlledRandomLock_Colors       = 0x780,      ENAMES(Colors) // All colors locked
	kPCControlledRandomLock_AllColor0    = 1 << 11,    ENAMES(SharedColor0)
	kPCControlledRandomLock_AllColor1    = 1 << 12,    ENAMES(SharedColor1)
	kPCControlledRandomLock_AllColor2    = 1 << 13,    ENAMES(SharedColor2)
	kPCControlledRandomLock_AllColor3    = 1 << 14,    ENAMES(SharedColor3)
	kPCControlledRandomLock_AllColors    = 0x7800,     ENAMES(SharedColors) // All shared colors locked
	kPCControlledRandomLock_SkinColor    = 1 << 15,    ENAMES(SkinColor)
	kPCControlledRandomLock_AllStyle     = 0x7FF,       ENAMES(AllStyle All) // All styling information locked
} PCControlledRandomLock;

AUTO_ENUM;
typedef enum PCPaymentMethod
{
	kPCPay_Default,			// Pay the default way
	kPCPay_Resources,		// Pay with resources
	kPCPay_FreeToken,		// The Item numeric token change
	kPCPay_FreeFlexToken,	// The Item "flexible" numeric token change
	kPCPay_GADToken,		// Account-wide Game account data token
} PCPaymentMethod;

// ---- Definitions --------------------------------------------------

typedef struct UIColor UIColor;
typedef struct UIColorSet UIColorSet;

AUTO_STRUCT;
typedef struct MessageRef
{
	const char *pcName;						AST( POOL_STRING NAME("Name") )
	REF_TO(Message) hMessage;				AST( NAME("Message") )
} MessageRef;

AUTO_STRUCT;
typedef struct MessageRefList
{
	MessageRef **eaMessages;
} MessageRefList;

AUTO_STRUCT;
typedef struct PCColorQuad {
	U8 color0[4];							AST( NAME("Color0") )
	U8 color1[4];							AST( NAME("Color1") )
	U8 color2[4];							AST( NAME("Color2") )
	U8 color3[4];							AST( NAME("Color3") )
	F32 fRandomWeight;                      AST( NAME("Weight") )
} PCColorQuad;

AUTO_STRUCT;
typedef struct PCColorQuadSet {
	const char *pcName;						AST( NAME("Name") STRUCTPARAM KEY POOL_STRING )
	const char *pcFilename;					AST( CURRENTFILE )

	PCColorQuad **eaColorQuads;             AST( NAME("ColorQuad") )
} PCColorQuadSet;

AUTO_STRUCT;
typedef struct PCMood {
	const char *pcName;						AST( KEY POOL_STRING )
	const char *pcFilename;					AST( CURRENTFILE )

	DisplayMessage displayNameMsg;          AST( STRUCT(parse_DisplayMessage) )
	const char *pcBits;
	F32 fOrder;
} PCMood;

AUTO_STRUCT;
typedef struct PCVoice {
	const char *pcName;						AST( KEY POOL_STRING )
	const char *pcFilename;					AST( CURRENTFILE )
	DisplayMessage displayNameMsg;          AST( STRUCT(parse_DisplayMessage) )

	const char *pcVoice;					AST( NAME(Voice) )

	PCRestriction eRestriction;				AST(NAME("RestrictedTo"), FLAGS)
	Gender eGender;							AST( NAME("Gender") SUBTABLE(GenderEnum) )
	F32 fOrder;

	//Unlock Rule
	const char *pcUnlockCode;
} PCVoice;

typedef struct SpeciesDef SpeciesDef;
typedef struct PCCategory PCCategory;
typedef struct PCRegion PCRegion;
typedef struct PCGeometryDef PCGeometryDef;
typedef struct PCBoneDef PCBoneDef;
typedef struct PCFX PCFX;
typedef struct PCFXSwap PCFXSwap;
typedef struct PCScaleValue PCScaleValue;
typedef struct DynFxDamageInfo DynFxDamageInfo;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCCategoryRef {
	REF_TO(PCCategory) hCategory;           AST( STRUCTPARAM REFDICT(CostumeCategory) )
} PCCategoryRef;
extern ParseTable parse_PCCategoryRef[];
#define TYPE_parse_PCCategoryRef PCCategoryRef

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCRegionRef {
	REF_TO(PCRegion) hRegion;               AST( STRUCTPARAM REFDICT(CostumeRegion) NAME("Region"))
} PCRegionRef;

AUTO_STRUCT;
typedef struct PCGeometryRef {
	REF_TO(PCGeometryDef) hGeo;            AST( STRUCTPARAM REFDICT(CostumeGeometry) )
} PCGeometryRef;
extern ParseTable parse_PCGeometryRef[];
#define TYPE_parse_PCGeometryRef PCGeometryRef

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCBoneRef {
	REF_TO(PCBoneDef) hBone;                AST( STRUCTPARAM REFDICT(CostumeBone) )
} PCBoneRef;

AUTO_STRUCT;
typedef struct PCStyle {
	const char *pcName;                     AST( STRUCTPARAM KEY POOL_STRING)
	DisplayMessage displayNameMsg;          AST( STRUCT(parse_DisplayMessage) )
	const char *pcFileName;                 AST( CURRENTFILE )
	F32 fOrder;
} PCStyle;

AUTO_STRUCT;
typedef struct PCCostumeGroupInfo {
	const char *pcName;                     AST( STRUCTPARAM KEY POOL_STRING)
} PCCostumeGroupInfo;

AUTO_STRUCT;
typedef struct PCLayer {
	const char *pcName;                     AST( STRUCTPARAM KEY POOL_STRING)
	DisplayMessage displayNameMsg;          AST( STRUCT(parse_DisplayMessage) )
	const char *pcFileName;                 AST( CURRENTFILE )
	PCLayerArea eLayerArea;
	PCLayerType eLayerType;
} PCLayer;

AUTO_STRUCT;
typedef struct PCCategory {
	const char *pcName;                     AST( STRUCTPARAM KEY POOL_STRING NAME("Name"))
	DisplayMessage displayNameMsg;          AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg"))
	const char *pcFileName;                 AST( CURRENTFILE )

	F32 fOrder;
	F32 fRandomWeight;  // Zero treated same as 1.0, -1 if no chance
	bool bHidden;

	PCBoneRef **eaRequiredBones;            AST( NAME("RequiredBone") )
	PCBoneRef **eaExcludedBones;            AST( NAME("ExcludedBone") )
	PCCategoryRef **eaExcludedCategories;   AST( NAME("ExcludedCategory") )
} PCCategory;
extern ParseTable parse_PCCategory[];
#define TYPE_parse_PCCategory PCCategory

AUTO_STRUCT;
typedef struct PCRegion {
	const char *pcName;                     AST( STRUCTPARAM KEY POOL_STRING NAME("Name"))
	DisplayMessage displayNameMsg;          AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg"))
	const char *pcFileName;                 AST( CURRENTFILE )

	F32 fOrder;
	REF_TO(PCCategory) hDefaultCategory;    AST( NAME("DefaultCategory") REFDICT(CostumeCategory) )
	PCCategoryRef **eaCategories;           AST( NAME("Category") )
} PCRegion;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCExtraTexture {
	const char *pcOrigTexture;        AST( POOL_STRING )
	const char *pcNewTexture;         AST( POOL_STRING )
	const char *pcTexWordsKey;        AST( POOL_STRING )
	bool bTexWordsCaps;
	U8 eTypeFlags;                    AST( FLAGS SUBTABLE(PCTextureTypeEnum) )
} PCExtraTexture;
extern ParseTable parse_PCExtraTexture[];
#define TYPE_parse_PCExtraTexture PCExtraTexture

AUTO_STRUCT;
typedef struct PCTextureValueOptions {
	const char *pcValueConstant;		   AST( POOL_STRING NAME("ValueConstant") )
	S32 iValConstIndex;                    AST( NAME("ConstValueIndex") )
	F32 fValueDefault;                     AST( NAME("ConstValueDefault") )
	F32 fValueMin;                         AST( NAME("ConstValueMin") )
	F32 fValueMax;                         AST( NAME("ConstValueMax") )
} PCTextureValueOptions;
extern ParseTable parse_PCTextureValueOptions[];
#define TYPE_parse_PCTextureValueOptions PCTextureValueOptions

AUTO_STRUCT;
typedef struct PCTextureMovableOptions {
	F32 fMovableMinX;                      AST(NAME("MovableMinX"))
	F32 fMovableMaxX;                      AST(NAME("MovableMaxX"))
	F32 fMovableDefaultX;                  AST(NAME("MovableDefaultX"))
	F32 fMovableMinY;                      AST(NAME("MovableMinY"))
	F32 fMovableMaxY;                      AST(NAME("MovableMaxY"))
	F32 fMovableDefaultY;                  AST(NAME("MovableDefaultY"))
	F32 fMovableMinScaleX;                 AST(NAME("MovableMinScaleX") DEF(1.0f))
	F32 fMovableMaxScaleX;                 AST(NAME("MovableMaxScaleX") DEF(1.0f))
	F32 fMovableDefaultScaleX;             AST(NAME("MovableDefaultScaleX") DEF(1.0f))
	F32 fMovableMinScaleY;                 AST(NAME("MovableMinScaleY") DEF(1.0f))
	F32 fMovableMaxScaleY;                 AST(NAME("MovableMaxScaleY") DEF(1.0f))
	F32 fMovableDefaultScaleY;             AST(NAME("MovableDefaultScaleY") DEF(1.0f))
	F32 fMovableDefaultRotation;           AST(NAME("MovableDefaultRotation"))
	bool bMovableCanEditPosition;          AST(NAME("MovableCanEditPosition"))
	bool bMovableCanEditRotation;          AST(NAME("MovableCanEditRotation"))
	bool bMovableCanEditScale;             AST(NAME("MovableCanEditScale"))
} PCTextureMovableOptions;
extern ParseTable parse_PCTextureMovableOptions[];
#define TYPE_parse_PCTextureMovableOptions PCTextureMovableOptions

AUTO_STRUCT;
typedef struct PCTextureColorOptions {
	bool bHasDefaultColor0;
	bool bHasDefaultColor1;
	bool bHasDefaultColor2;
	bool bHasDefaultColor3;

	//Default colors
	U8 uDefaultColor0[4];			AST(NAME("DefaultColor_0"))
	U8 uDefaultColor1[4];			AST(NAME("DefaultColor_1"))
	U8 uDefaultColor2[4];			AST(NAME("DefaultColor_2"))
	U8 uDefaultColor3[4];			AST(NAME("DefaultColor_3"))
} PCTextureColorOptions;
extern ParseTable parse_PCTextureColorOptions[];
#define TYPE_parse_PCTextureColorOptions PCTextureColorOptions

// This structure defines a named texture swap for use in costumes
// Please pay careful attention to field ordering in this structure.
// There are many of them loaded, and we need to keep it small.
AUTO_STRUCT AST_IGNORE(Group) AST_IGNORE(Style) AST_SINGLETHREADED_MEMPOOL;
typedef struct PCTextureDef {
	const char *pcName;                       AST( STRUCTPARAM KEY POOL_STRING NAME("Name") )
	const char *pcScope;                      AST( POOL_STRING NAME("Scope") )
	const char *pcFileName;                   AST( CURRENTFILE )
	DisplayMessage displayNameMsg;            AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg"))

	// Used in Tailor UI only.
	// The default here is intentionally 0, unlike other color choice flags.
	U8 eColorChoices;                         AST( NAME("ColorChoices") SUBTABLE(PCColorFlagsEnum) FLAGS )

	// Restrictions on use
	U8 eRestriction;                          AST( NAME("RestrictedTo") FLAGS SUBTABLE(PCRestrictionEnum) )

	U8 eTypeFlags;                            AST( FLAGS SUBTABLE(PCTextureTypeEnum) )

	// Whether color[3] is skin or not
	bool bHasSkin : 1;

	// TexWords options
	bool bTexWordsCaps : 1;
	const char *pcTexWordsKey;                AST( POOL_STRING )

	// The texture swap definition
	const char *pcOrigTexture;                AST( POOL_STRING )
	const char *pcNewTexture;                 AST( POOL_STRING )

	PCExtraTexture **eaExtraSwaps;            AST( NAME("ExtraTexture") )

	F32 fOrder;
	F32 fRandomWeight;  // Zero treated same as 1.0, -1 if no chance

	U8 uColorSwap0;							  AST( NAME("ColorSwap0") DEF(0) )
	U8 uColorSwap1;							  AST( NAME("ColorSwap1") DEF(1) )
	U8 uColorSwap2;							  AST( NAME("ColorSwap2") DEF(2) )
	U8 uColorSwap3;							  AST( NAME("ColorSwap3") DEF(3) )

	PCTextureColorOptions *pColorOptions;    AST( NAME("ColorOptions") )

	// Texture Driven Material Constants
	PCTextureValueOptions *pValueOptions;     AST( NAME("ValueOptions") )
	PCTextureValueOptions deprecated_ValueOptions; AST(EMBEDDED_FLAT) // TODO(jfw): Remove after migration.

	// Movable Texture Constants
	PCTextureMovableOptions *pMovableOptions; AST( NAME("MovableOptions") )
	PCTextureMovableOptions deprecated_MovableOptions; AST(EMBEDDED_FLAT) // TODO(jfw): Remove after migration.
	
	// temporary field for sorting costumes
	const char **eaCostumeGroups;				AST( NAME("CostumeGroups") POOL_STRING RESOURCEDICT(CostumeGroupDict) )
	
} PCTextureDef;
extern ParseTable parse_PCTextureDef[];
#define TYPE_parse_PCTextureDef PCTextureDef

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCMaterialDefConstant {
	STRING_POOLED pcName;       AST( STRUCTPARAM POOL_STRING )
	Vec4 values;                AST( STRUCTPARAM ) // Scale is 0 to 100
} PCMaterialDefConstant;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCMaterialDefColor {
	STRING_POOLED pcName;       AST( STRUCTPARAM POOL_STRING )
	Vec4 color;                 AST( STRUCTPARAM ) // Scale is where 255 255 255 255 is white
} PCMaterialDefColor;

// If you add new fields to this structure, update the checks
// in costumeDefEdit_UIDefMatSave.
AUTO_STRUCT;
typedef struct PCMaterialColorOptions
{
	bool bAllowGlow[4];			// Whether color[n] allows colors above 255
	bool bAllowReflection[4];	// Whether color[n] has reflection control
	bool bAllowSpecularity[4];	// Whether color[n] has specularity control
	bool bSuppressMuscle[4];	// Whether color[n] suppresses muscle value

	bool bCustomReflection;
	bool bCustomSpecularity;

	U8 defaultReflection[4];    // 0 to 100 by color channel
	U8 defaultSpecularity[4];   // 0 to 100 by color channel

} PCMaterialColorOptions;
extern ParseTable parse_PCMaterialColorOptions[];
#define TYPE_parse_PCMaterialColorOptions PCMaterialColorOptions

// If you add new fields to this structure, update the checks
// in costumeDefEdit_UIDefMatSave.
AUTO_STRUCT;
typedef struct PCMaterialOptions
{
	// Overrides used by artists and the runtime
	PCMaterialDefColor **eaExtraColors;       AST( NAME("ExtraColor") )
	PCMaterialDefConstant **eaExtraConstants; AST( NAME("ExtraValue") )
	EARRAY_OF(PCFXSwap) eaFXSwap;			  AST( NAME("FXSwap") )

} PCMaterialOptions;
extern ParseTable parse_PCMaterialOptions[];
#define TYPE_parse_PCMaterialOptions PCMaterialOptions

// This structure defines a named material for use in costumes
// Please pay careful attention to field ordering in this structure.
// There are many of them loaded, and we need to keep it small.
AUTO_STRUCT AST_IGNORE(Style) AST_SINGLETHREADED_MEMPOOL;
typedef struct PCMaterialDef {
	const char *pcName;                       AST( STRUCTPARAM KEY POOL_STRING NAME("Name"))
	const char *pcScope;                      AST( POOL_STRING NAME("Scope") )
	const char *pcFileName;                   AST( CURRENTFILE )
	DisplayMessage displayNameMsg;            AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg"))

	// The material definition
	const char *pcMaterial;                   AST( POOL_STRING )

	// Default patterns
	REF_TO(PCTextureDef) hDefaultPattern;     AST( NAME("DefaultPattern") REFDICT(CostumeTexture) )
	REF_TO(PCTextureDef) hDefaultDetail;      AST( NAME("DefaultDetail") REFDICT(CostumeTexture) )
	REF_TO(PCTextureDef) hDefaultSpecular;    AST( NAME("DefaultSpecular") REFDICT(CostumeTexture) )
	REF_TO(PCTextureDef) hDefaultDiffuse;     AST( NAME("DefaultDiffuse") REFDICT(CostumeTexture) )
	REF_TO(PCTextureDef) hDefaultMovable;     AST( NAME("DefaultMovable") REFDICT(CostumeTexture) )

	// Restrictions on use
	U8 eRestriction;                          AST( NAME("RestrictedTo") FLAGS SUBTABLE(PCRestrictionEnum) )

	// Used in Tailor UI only. Default to all colors.
	U8 eColorChoices;                         AST( NAME("ColorChoices") FLAGS DEF(15) SUBTABLE(PCColorFlagsEnum) ) 

	// Whether color[3] is skin or not
	bool bHasSkin : 1;

	bool bRequiresPattern : 1;
	bool bRequiresDetail : 1;
	bool bRequiresSpecular : 1;
	bool bRequiresDiffuse : 1;
	bool bRequiresMovable : 1;

	// Allowed textures for this material
	const char **eaAllowedTextureDefs;		  AST( NAME("Texture") POOL_STRING RESOURCEDICT(CostumeTexture) )

	F32 fOrder;
	F32 fRandomWeight;  // Zero treated same as 1.0, -1 if no chance

	PCMaterialColorOptions *pColorOptions;    AST( NAME("ColorOptions") )
	PCMaterialColorOptions deprecated_ColorOptions; AST(EMBEDDED_FLAT) // TODO(jfw): Remove after migration.

	PCMaterialOptions *pOptions;              AST( NAME("Options") )
	PCMaterialOptions deprecated_Options; AST(EMBEDDED_FLAT) // TODO(jfw): Remove after migration.

	// temporary field for sorting costumes
	const char **eaCostumeGroups;				AST( NAME("CostumeGroups") POOL_STRING RESOURCEDICT(CostumeGroupDict) )

} PCMaterialDef;
extern ParseTable parse_PCMaterialDef[];
#define TYPE_parse_PCMaterialDef PCMaterialDef

// This structure defines additional allowed textures for a material
// It is used in dev environments by the artists
AUTO_STRUCT;
typedef struct PCMaterialAdd {
	const char *pcName;                        AST( STRUCTPARAM KEY POOL_STRING NAME("Name", "Ver2") )
	const char *pcFileName;                    AST( CURRENTFILE )

	// Allowed textures for this material
	const char *pcMatName;                     AST( NAME("Material") POOL_STRING RESOURCEDICT(CostumeMaterial) )
	const char **eaAllowedTextureDefs;		   AST( NAME("Texture") POOL_STRING RESOURCEDICT(CostumeTexture) )
} PCMaterialAdd;
extern ParseTable parse_PCMaterialAdd[];
#define TYPE_parse_PCMaterialAdd PCMaterialAdd

AUTO_STRUCT;
typedef struct PCGeometryChildDef {
	REF_TO(PCBoneDef) hChildBone;				AST( NAME("ChildBone") REFDICT(CostumeBone) )
	REF_TO(PCGeometryDef) hDefaultChildGeo;     AST( NAME("DefaultChildGeometry") REFDICT(CostumeGeometry) )
	PCGeometryRef **eaChildGeometries;          AST( NAME("ChildGeometry") )
	bool bRequiresChild;                        AST( NAME("RequiresChildGeometry") )
} PCGeometryChildDef;
extern ParseTable parse_PCGeometryChildDef[];
#define TYPE_parse_PCGeometryChildDef PCGeometryChildDef

AUTO_ENUM;
typedef enum CostumeLODLevel {
	kCostumeLODLevel_SuperDetail = 0,			ENAMES(SuperDetail)
	kCostumeLODLevel_Detail = 1,				ENAMES(Detail)
	kCostumeLODLevel_Near = 2,					ENAMES(Near)
	kCostumeLODLevel_Far = 3,					ENAMES(Far)
	kCostumeLODLevel_Default = 10,			    ENAMES(Default)
	kCostumeLODLevel_Required = 10,			    ENAMES(Required)
} CostumeLODLevel;
extern StaticDefineInt CostumeLODLevelEnum[];

AUTO_STRUCT;
typedef struct PCGeometryClothData {
	bool bIsCloth;                              AST( NAME("IsCloth") )
	bool bHasClothBack;                         AST( NAME("HasClothBack") )
	const char *pcClothInfo;                    AST( NAME("ClothInfo") POOL_STRING RESOURCEDICT(DynClothInfo) )
	const char *pcClothColInfo;                 AST( NAME("ClothCollision") POOL_STRING RESOURCEDICT(DynClothCollision) )
} PCGeometryClothData;
extern ParseTable parse_PCGeometryClothData[];
#define TYPE_parse_PCGeometryClothData PCGeometryClothData

AUTO_STRUCT;
typedef struct PCGeometryOptions {
	// Child Definition
	PCGeometryChildDef **eaChildGeos;           AST( NAME("ChildGeometryDef") )
	bool bIsChild;                              AST( NAME("IsChild") )

	// Sub skeletons for animated costume parts
	const char *pcSubSkeleton;					AST( NAME("SubSkeleton") POOL_STRING )
	const char *pcSubBone;						AST( NAME("SubBone") POOL_STRING )

	// Overrides used by artists and the runtime
	EARRAY_OF(PCFXSwap) eaFXSwap;				AST( NAME("FXSwap") )

	// Attach FX
	EARRAY_OF(PCFX) eaFX;                       AST( NAME("FX") )

	// Determines the color palettes for the bone; overrides skeleton and bone colors if present
	REF_TO(UIColorSet) hBodyColorSet0;         AST( NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;         AST( NAME("BodyColorSet1") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet2;         AST( NAME("BodyColorSet2") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet3;         AST( NAME("BodyColorSet3") REFDICT(CostumeColors) )
	REF_TO(PCColorQuadSet) hColorQuadSet;     AST( NAME("ColorQuadSet") REFDICT(CostumeColorQuads) )

	REF_TO(DynFxDamageInfo) hDamageFxInfo;		AST( NAME ("DamageFxInfo") REFDICT(DynFxDamageInfo))
} PCGeometryOptions;
extern ParseTable parse_PCGeometryOptions[];
#define TYPE_parse_PCGeometryOptions PCGeometryOptions

// This structure defines a named geometry for use in costumes
// Please pay careful attention to field ordering in this structure.
// There are many of them loaded, and we need to keep it small.
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCGeometryDef {
	const char *pcName;                         AST( STRUCTPARAM KEY POOL_STRING NAME("Name") )
	const char *pcScope;                        AST( POOL_STRING NAME("Scope") )
	const char *pcFileName;                     AST( CURRENTFILE )
	DisplayMessage displayNameMsg;              AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg"))

	REF_TO(PCBoneDef) hBone;                    AST( NAME("Bone") REFDICT(CostumeBone) )

	// The geometry definition
	const char *pcGeometry;                     AST( POOL_STRING )
	const char *pcModel;                        AST( POOL_STRING )

	// The paired geometry
	const char *pcMirrorGeometry;               AST( POOL_STRING RESOURCEDICT(CostumeGeometry) )

	// The material that should be used by default in the editor
	REF_TO(PCMaterialDef) hDefaultMaterial;     AST( NAME("DefaultMaterial") REFDICT(CostumeMaterial) )

	// The allowed materials for this geometry
	const char **eaAllowedMaterialDefs;         AST( NAME("Material") POOL_STRING RESOURCEDICT(CostumeMaterial) )

	// How high the LOD must be to draw this object.
	U8 eLOD;                                    AST( NAME("LOD") DEFAULT(kCostumeLODLevel_Default) SUBTABLE(CostumeLODLevelEnum) )

	// Restrictions on use
	U8 eRestriction;                            AST( NAME("RestrictedTo") FLAGS SUBTABLE(PCRestrictionEnum) )

	// Used in Tailor UI only. Default to all colors.
	U8 eColorChoices;                           AST( NAME("ColorChoices") FLAGS DEF(15) SUBTABLE(PCColorFlagsEnum) )

	bool bHasAlpha;								AST( NAME("HasAlpha") )
	
	F32 fOrder;
	F32 fRandomWeight;  // Zero treated same as 1.0, -1 if no chance

	// Categories and styles for this geometry
	PCCategoryRef **eaCategories;               AST( NAME("Category") )
	const char **eaStyles;						AST( NAME("Style") POOL_STRING RESOURCEDICT(CostumeStyle) )

	PCGeometryClothData *pClothData;            AST( NAME("ClothData") )
	PCGeometryClothData deprecated_ClothData;   AST(EMBEDDED_FLAT) // TODO(jfw): Remove after migration.

	PCGeometryOptions *pOptions;                AST( NAME("Options") )
	PCGeometryOptions deprecated_Options;       AST(EMBEDDED_FLAT) // TODO(jfw): Remove after migration.

	// temporary field for sorting costumes
	const char **eaCostumeGroups;				AST( NAME("CostumeGroups") POOL_STRING RESOURCEDICT(CostumeGroupDict) )

} PCGeometryDef;
extern ParseTable parse_PCGeometryDef[];
#define TYPE_parse_PCGeometryDef PCGeometryDef

// This structure defines additional allowed materials for a geometry
// It is used in dev environments by the artists
AUTO_STRUCT;
typedef struct PCGeometryAdd {
	const char *pcName;                         AST( STRUCTPARAM KEY POOL_STRING NAME("Name", "Ver2") )
	const char *pcFileName;                     AST( CURRENTFILE )

	// The allowed materials for this geometry
	const char *pcGeoName;                      AST( NAME("Geometry") POOL_STRING RESOURCEDICT(CostumeGeometry) )
	const char **eaAllowedMaterialDefs;			AST( NAME("Material") POOL_STRING RESOURCEDICT(CostumeMaterial) )
} PCGeometryAdd;
extern ParseTable parse_PCGeometryAdd[];
#define TYPE_parse_PCGeometryAdd PCGeometryAdd

AUTO_STRUCT;
typedef struct PCChildBone {
	REF_TO(PCBoneDef) hChildBone;     AST( NAME("ChildBone") REFDICT(CostumeBone) )
	REF_TO(PCLayer) hChildLayerFront; AST( NAME("ChildLayer") REFDICT(CostumeLayer) )
	REF_TO(PCLayer) hChildLayerBack;  AST( NAME("ChildLayerBack") REFDICT(CostumeLayer) )
} PCChildBone;

AUTO_ENUM;
typedef enum PCBoneGroupFlags {
	kPCBoneGroupFlags_None = 0,
	kPCBoneGroupFlags_MatchGeos = 1 << 0,
	kPCBoneGroupFlags_LinkMaterials = 1 << 1,
} PCBoneGroupFlags;

AUTO_STRUCT;
typedef struct PCBoneGroup {
	const char *pcName;               AST( STRUCTPARAM KEY POOL_STRING)
	DisplayMessage displayNameMsg;    AST( STRUCT(parse_DisplayMessage) )
	const char *pcFileName;           AST( CURRENTFILE )

	PCBoneGroupFlags eBoneGroupFlags;	AST( NAME("Flags") FLAGS DEF(0) )

	PCBoneRef **eaBoneInGroup;          AST( NAME("BoneInGroup") )
} PCBoneGroup;

AUTO_STRUCT;
typedef struct PCBoneDef {
	const char *pcName;               AST( STRUCTPARAM KEY POOL_STRING NAME("Name"))
	DisplayMessage displayNameMsg;    AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg"))
	const char *pcFileName;           AST( CURRENTFILE )

	DisplayMessage geometryFieldDispName;		AST( STRUCT(parse_DisplayMessage) )
	DisplayMessage materialFieldDispName;		AST( STRUCT(parse_DisplayMessage) )
	DisplayMessage patternFieldDispName;		AST( STRUCT(parse_DisplayMessage) )
	DisplayMessage detailFieldDisplayName;		AST( STRUCT(parse_DisplayMessage) )
	DisplayMessage specularFieldDisplayName;	AST( STRUCT(parse_DisplayMessage) )
	DisplayMessage diffuseFieldDisplayName;		AST( STRUCT(parse_DisplayMessage) )
	DisplayMessage movableFieldDisplayName;		AST( STRUCT(parse_DisplayMessage) )

	// The region this bone is available in
	REF_TO(PCRegion) hRegion;         AST( NAME("Region") REFDICT(CostumeRegion) )

	const char *pcBoneName;           AST( POOL_STRING ) // The world layer skeleton bone to use
	const char *pcClickBoneName;      AST( POOL_STRING ) // The world layer bone for clicking

	// The default geometry to use in the editor if bone is required
	REF_TO(PCGeometryDef) hDefaultGeo;  AST( NAME("DefaultGeo") REFDICT(CostumeGeometry) ) 

	// Optional mirror bone def, layer for this bone, and merged name
	REF_TO(PCBoneDef) hMirrorBone;    AST( NAME("MirrorBone") REFDICT(CostumeBone) )
	REF_TO(PCLayer) hSelfLayer;       AST( NAME("SelfLayer") REFDICT(CostumeLayer) )
	REF_TO(PCLayer) hMergeLayer;      AST( NAME("MergeLayer") REFDICT(CostumeLayer) )
	DisplayMessage mergeNameMsg;      AST( STRUCT(parse_DisplayMessage) )

	// Optional child bone def
	PCChildBone **eaChildBones;       AST( NAME("ChildBoneEntry") )

	// Set this if this bone is a child bone
	bool bIsChildBone;

	// Set this if this bone is for only displaying guild emblems
	bool bIsGuildEmblemBone;

	// Layers to use if this is a cloth piece
	REF_TO(PCLayer) hMainLayerFront;  AST( NAME("MainLayer") REFDICT(CostumeLayer) )
	REF_TO(PCLayer) hMainLayerBack;   AST( NAME("MainLayerBack") REFDICT(CostumeLayer) )
	REF_TO(PCLayer) hMainLayerBoth;   AST( NAME("MainLayerBoth") REFDICT(CostumeLayer) )

	// Determines the color palettes for the bone; overrides skeleton colors if present
	REF_TO(UIColorSet) hBodyColorSet0;         AST( NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;         AST( NAME("BodyColorSet1") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet2;         AST( NAME("BodyColorSet2") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet3;         AST( NAME("BodyColorSet3") REFDICT(CostumeColors) )

	// Used in Tailor UI only. Default to all colors.
	PCColorFlags eColorChoices;					AST( NAME("ColorChoices") FLAGS DEF(15))

	// Determines the quads of colors used by the randomizer.  Colors are
	// picked in groups of four as chosen by the artists instead of being
	// randomly picked independently.
	PCColorQuad *pDefaultBoneColorQuad;       AST( NAME("DefaultBoneColorQuad") )
	REF_TO(PCColorQuadSet) hColorQuadSet;     AST( NAME("ColorQuadSet") REFDICT(CostumeColorQuads) )

	F32 fOrder;
	F32 fRandomChance;   // If not required, chance of bone getting a value (0 treated as default, -1 if none)

	// How high the LOD must be to draw this object.
	CostumeLODLevel eLOD;					AST( NAME("LOD") DEFAULT(kCostumeLODLevel_Default) )

	// Restrictions on use
	PCRestriction eRestriction;         AST(NAME("RestrictedTo"), FLAGS)
	bool bPowerFX;						AST( NAME("PowerFX") )
	bool bRaycastable;					AST( NAME("Raycastable") )
} PCBoneDef;
extern ParseTable parse_PCBoneDef[];
#define TYPE_parse_PCBoneDef PCBoneDef

AUTO_STRUCT;
typedef struct PCBodyScaleValue {
	const char* pcName;					AST( NAME("Name") STRUCTPARAM POOL_STRING )
	DisplayMessage displayNameMsg;		AST( STRUCT(parse_DisplayMessage) )
	F32 fValue;							AST( NAME("Value") STRUCTPARAM )
} PCBodyScaleValue;

AUTO_STRUCT;
typedef struct PCAnimBitRange {
	const char *pcBit;
	F32 fMin;
	F32 fMax;
} PCAnimBitRange;

AUTO_STRUCT;
typedef struct PCBodyScaleInfo {
	const char *pcName;                 AST( STRUCTPARAM POOL_STRING )
	DisplayMessage displayNameMsg;      AST( STRUCT(parse_DisplayMessage) )
	F32 fOrder;
	PCRestriction eRestriction;         AST(NAME("RestrictedTo"), FLAGS)
	PCBodyScaleValue** eaValues;		AST( NAME("ScaleChoice") )
	PCAnimBitRange** eaAnimBitRange;	AST( NAME("AnimBitRange") ) // Min/Max range from 0-100
} PCBodyScaleInfo;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCScaleEntry {
	const char *pcName;                 AST( STRUCTPARAM POOL_STRING )
	int iIndex;                         AST( STRUCTPARAM )
} PCScaleEntry;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCScaleInfo {
	const char *pcName;					AST( POOL_STRING )
	const char *pcDisplayName;			AST( POOL_STRING )
	DisplayMessage displayNameMsg;      AST( STRUCT(parse_DisplayMessage) )

	// Animated sub-skeleton to which this scale info applies
	const char* pcSubSkeleton;			AST( POOL_STRING )

	// Player range restrictions
	F32 fPlayerMin;
	F32 fPlayerMax;

	// Scale entries are the underlying skeleton name/index pairs to use
	PCScaleEntry **eaScaleEntries;      AST( NAME("Affects") )

	PCRestriction eRestriction;         AST( NAME("RestrictedTo"), FLAGS )
} PCScaleInfo;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PCScaleInfoGroup {
	const char* pcName;					AST( POOL_STRING )
	const char* pcDisplayName;			AST( POOL_STRING )
	DisplayMessage displayNameMsg;      AST( STRUCT(parse_DisplayMessage) )
	
	// Child scale info structures belonging to this group
	PCScaleInfo** eaScaleInfo;			AST( NAME("Scale") )
} PCScaleInfoGroup;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct PCStanceInfo {
	CONST_STRING_POOLED pcName;			AST( PERSIST SUBSCRIBE NAME("Name") POOL_STRING )
	DisplayMessage displayNameMsg;      AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg") )
	const char *pcBits;					AST( NAME("Bits") )

	F32 fOrder;							AST( NAME("Order") )
	F32 fRandomWeight;					AST( NAME("RandomWeight") )

	PCRestriction eRestriction;         AST( NAME("RestrictedTo"), FLAGS )
} PCStanceInfo;

AUTO_STRUCT;
typedef struct PCPresetScaleValueGroup
{
	const char* pcName;				  AST( KEY POOL_STRING NAME("Name"))
	// This is just an arbitrary tag that can be placed on the 
	// scale for UI display purposes. e.g. "Head" or "Body"
	const char* pcTag;				  AST( POOL_STRING )
	const char* pcMood;				  AST( POOL_STRING )
	const char* pcStance;			  AST( POOL_STRING )
	PCScaleValue **eaScaleValues;
} PCPresetScaleValueGroup;


AUTO_STRUCT;
typedef struct PCStump
{
	REF_TO(PCGeometryDef)	hGeoDef;	AST( STRUCTPARAM )
} PCStump;

// This structure defines a named skeleton
AUTO_STRUCT AST_IGNORE(NoHeightScaling);
typedef struct PCSkeletonDef {
	const char *pcName;               AST( STRUCTPARAM KEY POOL_STRING )
	DisplayMessage displayNameMsg;    AST(STRUCT(parse_DisplayMessage))
	const char *pcFileName;           AST( CURRENTFILE)

	// The underlying skeleton to use
	const char *pcSkeleton;           AST( POOL_STRING )

	// The regions bones can exist in on this skeleton
	PCRegionRef **eaRegions;                  AST(NAME("Region"))

	// The set of bones on this skeleton to make available
	PCBoneRef **eaRequiredBoneDefs;           AST( NAME("RequiredBone") )
	PCBoneRef **eaOptionalBoneDefs;           AST( NAME("OptionalBone") )
	PCBoneGroup **eaBoneGroups;				  AST( NAME("BoneGroup") )

	// The stances to make available
	const char *pcDefaultStance;              AST( POOL_STRING )
	PCStanceInfo **eaStanceInfo;              AST( NAME("Stance") )

	// Time required to falldown & standup after a knockdown
	F32 fImpactTime_Push;
	F32 fImpactTime_Knock;
	F32 fGetupTime;

	// Used to determine a mounts global scale at runtime based on both costumes, 1.0 = 100% mount, 0.0 = 100% rider, -1.0 = OFF (uses a hard coded value when mounting happens)
	// when set this will override the similarly named value from CostumeConfig
	F32 fMountRiderScaleBlend; AST(DEFAULT(-1.f))

	// Provides a description of collidable objects on the costume's skeleton
	// to apply to the cloth of any attached riders
	const char *pcMountClothCollisionInfo; AST(POOL_STRING)

	// Terrain tilting controls
	bool bTerrainTiltApply;			AST(BOOLFLAG)
	bool bTerrainTiltModifyRoot;	AST(BOOLFLAG)
	F32 fTerrainTiltBaseLength;
	F32 fTerrainTiltStrength;		AST(DEFAULT(1.f))
	F32 fTerrainTiltMaxBlendAngle;

	// Flourish controls (supported on mounts)
	F32 fFlourishTimer;				AST(DEFAULT(3.f))
	
	// Height scaling (in feet)
	// These are related to the values in the skelinfo and are used for
	// mapping internal skeleton height to  to feet.  The underlying system
	// uses a range from -1.0 to 1.0, but we want costumes to actually be related
	// in feet.  This is the icky glue that maps between the two sets of values.
	// Do not mess with these unless the skelinfo is modified.
	F32 fHeightMin;  // The value in feet that maps to -1.0 on the underlying float
	F32 fHeightBase; // The value in feet that maps to 0.0 on the underlying float
	F32 fHeightMax;  // The value in feet that maps to 1.0 on the underlying float
	PCAnimBitRange** eaAnimBitRange;	AST( NAME("HeightAnimBitRange", "AnimBitRange") ) // Min/Max are in feet

	// Editor defaults and ranges
	// These are used by the tailor and by the runtime to keep players from making
	// costumes that are completely wild.
	F32 fDefaultHeight;    // The default height for both players and artists
	F32 fPlayerMinHeight;  // The min height a player can set in feet (artists can go to fHeightMin)
	F32 fPlayerMaxHeight;  // The max height a player can set in feet (artists can go to fHeightMax)

	//Min and Max Camera Distance in the Tailor
	F32 fMinTailorCamDist;
	F32 fMaxTailorCamDist;
	bool bAutoAdjustTailorCamDistance; //Adjusts distance based on the entity

	// Editor defaults and ranges
	// These are used by the tailor to keep players from making costumes that
	// don't look right as far as the artists are concerned.
	bool bNoMuscle;
	F32 fDefaultMuscle;
	F32 fPlayerMinMuscle;
	F32 fPlayerMaxMuscle;

	// Body scales are defined using the body animation system.  A skeleton
	// can have zero or more of these and they are defined in order.
	// These values define the ranges within -1.0 to 1.0 that the player can
	// use.  The float arrays must appear in the same order as the scale infos
	PCBodyScaleInfo **eaBodyScaleInfo;        AST( NAME("BodyScale") )
	FLOAT_EARRAY eafDefaultBodyScales;         AST(NAME("DefaultBodyScale"))
	FLOAT_EARRAY eafPlayerMinBodyScales;       AST(NAME("PlayerMinBodyScale"))
	FLOAT_EARRAY eafPlayerMaxBodyScales;       AST(NAME("PlayerMaxBodyScale"))

	// Bone scales are used to distort body areas (like making arms longer).
	// These can be put into named groups for UI convenience or simply listed.
	PCScaleInfoGroup **eaScaleInfoGroups;     AST( NAME("ScaleGroup") )
	PCScaleInfo **eaScaleInfo;                AST( NAME("Scale") )

	PCPresetScaleValueGroup **eaPresets;	  AST( NAME("ScalePreset", "Preset") )

	// Determines the color palettes for the skeleton
	Vec4 defaultSkinColor;                    AST( NAME("DefaultSkinColor") )
	REF_TO(UIColorSet) hSkinColorSet;         AST( NAME("SkinColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet;          AST( NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;         AST( NAME("BodyColorSet1") REFDICT(CostumeColors) ) //inherits from BodyColorSet0 if not present
	REF_TO(UIColorSet) hBodyColorSet2;         AST( NAME("BodyColorSet2") REFDICT(CostumeColors) ) //inherits from BodyColorSet0 if not present
	REF_TO(UIColorSet) hBodyColorSet3;         AST( NAME("BodyColorSet3") REFDICT(CostumeColors) ) //inherits from BodyColorSet0 if not present

	// Determines the quads of colors used by the randomizer.  Colors are
	// picked in groups of four as chosen by the artists instead of being
	// randomly picked independently.
	PCColorQuad *pDefaultBodyColorQuad;       AST( NAME("DefaultBodyColorQuad") )
	REF_TO(PCColorQuadSet) hColorQuadSet;     AST( NAME("ColorQuadSet") REFDICT(CostumeColorQuads) )

	// Overrides used by artists and the runtime
	EARRAY_OF(PCFX) eaFX;			  AST( NAME("FX") )


	PCStump** eaStumps;		 AST( NAME("Stump") )

	
	// Gender defined for critters using this costume skeleton
	Gender eGender;					  AST( NAME("Gender") SUBTABLE(GenderEnum) )

	// Restriction on use
	PCRestriction eRestriction;       AST( NAME("RestrictedTo"), FLAGS )

	// Used by tailor UI for sorting
	F32 fOrder;

	// Capsule override
	Capsule **ppCollCapsules;		  AST( NAME("Capsule") )
	F32 fCollRadius;				  AST( NAME("CollRadius") )

	REF_TO(DynFxDamageInfo) hDamageFxInfo; AST( NAME ("DamageFxInfo") REFDICT(DynFxDamageInfo))

	// Perform starship glue-up adjustment (for STO)
	U32 bAutoGlueUp:1;                AST( NAME("AutoGlueUp") )

	// Shield geometry, scale, and attachment bone (for FX)
	const char* pcShieldGeometry; AST(POOL_STRING)
	Vec3 vShieldScale; AST( NAME("ShieldScale") )
	const char* pcShieldAttachBone; AST(POOL_STRING)


	// Perform copies of values into the costume at runtime
	U32 bCopyName:1;                  AST( NAME("CopyName") )
	U32 bCopySubName:1;               AST( NAME("CopySubName") )

	U32 bTorsoPointing:1;			  AST( NAME("TorsoPointing") )

	U32 bUseCapsuleBoundsForTargeting; AST( NAME("UseCapsuleBoundsForTargeting" ) )
} PCSkeletonDef;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct PlayerCostumeHolder {
	PlayerCostume *pCostume;					AST( NAME("PlayerCostume") )
} PlayerCostumeHolder;


AUTO_ENUM;
typedef enum PCCostumeSetFlags {
	kPCCostumeSetFlags_None = 0,
	kPCCostumeSetFlags_DontPreloadOnClient = 1 << 0,
	kPCCostumeSetFlags_Unlockable = 1 << 1,
	kPCCostumeSetFlags_TailorPresets = 1 << 2,
} PCCostumeSetFlags;

AUTO_STRUCT;
typedef struct CostumeRefForSet {
	const char *pcName;							 AST( STRUCTPARAM KEY POOL_STRING )
	DisplayMessage displayNameMsg;				 AST(STRUCT(parse_DisplayMessage))
	DisplayMessage descriptionMsg;				 AST(STRUCT(parse_DisplayMessage))
	const char *pcImage;						 AST( POOL_STRING )

	REF_TO(PlayerCostume) hPlayerCostume;       AST( REFDICT(PlayerCostume) NAME("CostumeName"))

	// Used by tailor UI for sorting
	F32 fOrder;
} CostumeRefForSet;

AUTO_STRUCT;
typedef struct PCCostumeSet {
	const char *pcName;							 AST( STRUCTPARAM KEY POOL_STRING )
	DisplayMessage displayNameMsg;				 AST(STRUCT(parse_DisplayMessage))
	const char *pcFileName;						 AST( CURRENTFILE )

	PCCostumeType eCostumeType;					 AST( NAME("CostumeType") DEF(kPCCostumeType_Overlay) )

	PCCostumeSetFlags eCostumeSetFlags;			 AST( NAME("Flags") FLAGS DEF(0) )
	Expression *pExprUnlock;					 AST(NAME(ExprBlockUnlock,ExprUnlockBlock), REDUNDANT_STRUCT(ExprUnlock, parse_Expression_StructParam), LATEBIND)

	CostumeRefForSet **eaPlayerCostumes;         AST( NAME("PlayerCostume") )
} PCCostumeSet;

AUTO_STRUCT;
typedef struct PCSlotSpecies
{
	// Override species for costume validation
	REF_TO(SpeciesDef) hSpecies;			      AST( NAME("Species") REFDICT(Species) )

}PCSlotSpecies;

AUTO_STRUCT;
typedef struct SlotBodyScaleLimit
{
	const char *pcName;						AST( STRUCTPARAM POOL_STRING )
	F32 fMin;
	F32 fMax;
} SlotBodyScaleLimit;

AUTO_STRUCT;
typedef struct SlotBoneScaleLimit
{
	const char *pcName;						AST( STRUCTPARAM POOL_STRING )
	F32 fMin;
	F32 fMax;
} SlotBoneScaleLimit;

AUTO_STRUCT;
typedef struct PCSlotStanceStruct
{
	Gender eGender;
	const char *pcDefaultStance;            AST( POOL_STRING )
	PCStanceInfo **eaStanceInfo;            AST( NAME("Stance") )
	
}PCSlotStanceStruct;


AUTO_STRUCT;
typedef struct PCSlotType {
	const char *pcName;							AST( STRUCTPARAM KEY POOL_STRING )
	const char *pcFileName;						AST( CURRENTFILE )

	// Description messages for the UI to use
	DisplayMessage displayNameMsg;				AST( NAME("DisplayName") STRUCT(parse_DisplayMessage) )

	PCCategoryRef **eaCategories;				AST( NAME("AllowedCategory") )

	//
	// all of the following will require bUseCostumeSlotOverride to be true to use them
	//

	bool bUseCostumeSlotOverride;

	SlotBodyScaleLimit **eaBodyScaleLimits;		AST( NAME("BodyScaleLimits") )
	SlotBoneScaleLimit **eaBoneScaleLimits;		AST( NAME("BoneScaleLimits") )

	bool bNoHeightChange;
	F32 fMinHeight;
	F32 fMaxHeight;

	bool bNoMuscle;
	F32 fMinMuscle;
	F32 fMaxMuscle;
	
	REF_TO(UIColorSet) hSkinColorSet;			AST( NAME("SkinColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet;			AST( NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;			AST( NAME("BodyColorSet1") REFDICT(CostumeColors) ) 
	REF_TO(UIColorSet) hBodyColorSet2;			AST( NAME("BodyColorSet2") REFDICT(CostumeColors) ) 
	REF_TO(UIColorSet) hBodyColorSet3;			AST( NAME("BodyColorSet3") REFDICT(CostumeColors) ) 
	
	EARRAY_OF(PCSlotStanceStruct) eaSlotStances;	AST( NAME("SlotStances") )
	
} PCSlotType;

AUTO_STRUCT;
typedef struct PCSlotTypes {
	PCSlotType **eaSlotTypes;					AST(NAME("CostumeSlotType"))
} PCSlotTypes;

AUTO_ENUM;
typedef enum PCCharacterCreateSlot {
	kPCCharacterCreateSlot_Default,
	kPCCharacterCreateSlot_Required, // this slot is required to be filled at character creation
} PCCharacterCreateSlot;

AUTO_STRUCT;
typedef struct PCSlotDef {
	// ID used for uniquely matching slots on player
	int iSlotID;								AST( NAME("ID") )

	// Description messages for the UI to use
	DisplayMessage descriptionMsg;				AST( NAME("DescriptionMessage") STRUCT(parse_DisplayMessage) )
	DisplayMessage lockedDescriptionMsg;		AST( NAME("LockedDescriptionMessage") STRUCT(parse_DisplayMessage) )

	// Expressions for when the slot becomes visible and usable
	Expression *pExprUnlock;					AST( NAME("UnlockExpressionBlock"), REDUNDANT_STRUCT("UnlockExpression", parse_Expression_StructParam), LATEBIND)
	Expression *pExprUnhide;					AST( NAME("UnhideExpressionBlock"), REDUNDANT_STRUCT("UnhideExpression", parse_Expression_StructParam), LATEBIND)

	// The slot type information
	// If no slot type, then everything is legal
	const char *pcSlotType;						AST( NAME("SlotType") )
	PCSlotType *pSlotType;						NO_AST

	// Optional slot types which may or may not be possible
	const char **eaOptionalSlotTypes;			AST( NAME("OptionalSlotType") POOL_STRING )

	// The behavior of this slot at character creation
	PCCharacterCreateSlot eCreateCharacter;		AST( NAME("CreateCharacter") )
} PCSlotDef;

AUTO_STRUCT;
typedef struct PCSlotSet {
	const char *pcName;							AST( STRUCTPARAM KEY POOL_STRING )
	const char *pcFileName;						AST( CURRENTFILE )

	// Change this version to force fixup transaction on characters
	U8 iSetVersion;								AST( NAME("Version") )

	// The type of entity to match this slot set to
	// If marked as default, this applies if no other slot set applies
	// If marked as artist slot, then it is used by "ChangeCostume"
	GlobalType eEntityType;						AST( NAME("EntityType") )
	PCRegionType eRegionType;					AST( NAME("RegionType") )
	REF_TO(AllegianceDef) hAllegiance;			AST( NAME("Allegiance") )
	bool bIsDefault;							AST( NAME("IsDefault") )

	// The base costume slot entries
	PCSlotDef **eaSlotDefs;						AST( NAME("CostumeSlot") )

	// The def to use for any extra costumes earned
	PCSlotDef *pExtraSlotDef;					AST( NAME("ExtraCostumeSlot") )
} PCSlotSet;

AUTO_STRUCT;
typedef struct PCSlotSets {
	PCSlotSet **eaSlotSets;						AST(NAME("CostumeSlotSet"))
} PCSlotSets;

#define DEFAULT_ITEM_REPLACE_PRIORITY   10
#define DEFAULT_ITEM_OVERLAY_PRIORITY   30
#define DEFAULT_POWER_REPLACE_PRIORITY  20
#define DEFAULT_POWER_OVERLAY_PRIORITY  40
#define DEFAULT_POWER_MODIFY_PRIORITY   50
#define DEFAULT_MOUNT_PRIORITY          60

AUTO_ENUM;
typedef enum PCPartType {
	kPCPartType_Primary = 0,
	kPCPartType_Child,
	kPCPartType_Cloth
} PCPartType;

typedef struct CostumeDisplayData {
	int iPriority;
	kCostumeDisplayType eType;

	kCostumeDisplayMode eMode;
	PlayerCostume **eaCostumes;
	PlayerCostume **eaCostumesOwned;
	PCFX** eaAddedFX;

	kCostumeValueArea eValueArea;
	kCostumeValueMode eValueMode;
	F32 fValue;
	F32 fMinValue; // Clamp on ranges of value modify
	F32 fMaxValue;

	Vec3 vDyeColors[4];
	REF_TO(PCMaterialDef) hDyeMat;
	const char** eaStances;

	F32 fMountScaleOverride;
} CostumeDisplayData;


// ---- Player costume instance structures (persisted) ----

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCBitName {
	CONST_STRING_POOLED pcName;       AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING NAME("Name"))
} PCBitName;

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCScaleValue {
	CONST_STRING_POOLED pcScaleName;  AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING )
	const F32 fValue;                 AST( PERSIST SUBSCRIBE STRUCTPARAM )  // Scale is -100 to 100
} PCScaleValue;

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCMaterialConstant {
	CONST_STRING_POOLED pcName;       AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING )
	const Vec4 values;                AST( PERSIST SUBSCRIBE STRUCTPARAM ) // Scale is 0 to 100
} PCMaterialConstant;
extern ParseTable parse_PCMaterialConstant[];
#define TYPE_parse_PCMaterialConstant PCMaterialConstant

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCTexWords {
	CONST_STRING_POOLED pcKey;        AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING NAME("Key") )
	CONST_STRING_MODIFIABLE pcText;   AST( PERSIST SUBSCRIBE STRUCTPARAM NAME("Text") )
} PCTexWords;

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCFX {
	CONST_STRING_POOLED pcName;       AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING )
	const F32 fHue;                   AST( PERSIST SUBSCRIBE STRUCTPARAM )
	char *pcParams;                   AST( NAME("Params") )
} PCFX;

AUTO_STRUCT;
typedef struct PCFXNoPersist {
	const char* pcName;				  AST( POOL_STRING )
	F32 fHue;
} PCFXNoPersist;

extern ParseTable parse_PCFXNoPersist[];
#define TYPE_parse_PCFXNoPersist PCFXNoPersist

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCFXSwap {
	CONST_STRING_POOLED pcOldName;    AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING )
	CONST_STRING_POOLED pcNewName;    AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING )
} PCFXSwap;
extern ParseTable parse_PCFXSwap[];
#define TYPE_parse_PCFXSwap PCFXSwap

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCRegionCategory {
	REF_TO(PCRegion) hRegion;         AST( PERSIST SUBSCRIBE NAME("hRegion","PCRegion") STRUCTPARAM REFDICT(CostumeRegion) )
	REF_TO(PCCategory) hCategory;     AST( PERSIST SUBSCRIBE NAME("hCategory","PCCategory") STRUCTPARAM REFDICT(CostumeCategory) )
} PCRegionCategory;

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
// WARNING: This structure is shared between V0 and V5 costume structures.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCTextureRef {
	REF_TO(PCTextureDef) hTexture;    AST( PERSIST SUBSCRIBE STRUCTPARAM NAME("Name") REFDICT(CostumeTexture))
} PCTextureRef;
extern ParseTable parse_PCTextureRef[];
#define TYPE_parse_PCTextureRef PCTextureRef

// ---- Version 5 Costume Structures ---------------------------------------------

// WARNING: This structure is NOT USED any more in live game code
// except to migrate old characters forward.  Do not modify this structure!
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCMaterialColorV0 {
	CONST_STRING_POOLED pcName;       AST( PERSIST STRUCTPARAM POOL_STRING )
	const Vec4 color;                 AST( PERSIST STRUCTPARAM ) // Scale is where 255 255 255 255 is white
} PCMaterialColorV0;
extern ParseTable parse_PCMaterialColorV0[];
#define TYPE_parse_PCMaterialColorV0 PCMaterialColorV0

typedef struct PCPartV0 PCPartV0;

// WARNING: This structure is NOT USED any more in live game code
// except to migrate old characters forward.  Do not modify this structure!
AUTO_STRUCT AST_CONTAINER;
typedef struct PCPartV0 {
	// Choices made for the part
	REF_TO(PCBoneDef) hBoneDef;             AST( PERSIST NAME("Bone") REFDICT(CostumeBone))
	REF_TO(PCGeometryDef) hGeoDef;          AST( PERSIST NAME("Geometry") REFDICT(CostumeGeometry))
	REF_TO(PCMaterialDef) hMatDef;          AST( PERSIST NAME("Material") REFDICT(CostumeMaterial))
	CONST_STRING_POOLED hTextureGroup;      AST( PERSIST NAME("TextureGroup") POOL_STRING )
	REF_TO(PCTextureDef) hPatternTexture;   AST( PERSIST NAME("PatternTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hDetailTexture;    AST( PERSIST NAME("DetailTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hSpecularTexture;  AST( PERSIST NAME("SpecularTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hDiffuseTexture;   AST( PERSIST NAME("DiffuseTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hMovableTexture;   AST( PERSIST NAME("MovableTexture") REFDICT(CostumeTexture))
	int iBoneGroupIndex;					AST( CLIENT_ONLY DEFAULT(-1)) //Index into PCSkeletonDef->eaBoneGroups; -1 = no bone group

	// Colors for the part.  Scale is where 255/255/255/255 is white.
	const Vec4 color0;                      AST( PERSIST NAME("Color0") )
	const Vec4 color1;                      AST( PERSIST NAME("Color1") )
	const Vec4 color2;                      AST( PERSIST NAME("Color2") )
	const Vec4 color3;                      AST( PERSIST NAME("Color3") )
	const U8 glowScale[4];                  AST( PERSIST NAME("glowScale") ) // Specifies glow scale per color[n]. 0 or 1=none, 10 = Amazing glow
	const bool bCustomReflection;           AST( PERSIST NAME("CustomReflection") )
	const bool bCustomSpecularity;          AST( PERSIST NAME("CustomSpecularity") )
	const U8 reflection[4];                 AST( PERSIST NAME("Reflection") ) // Specifies reflectivity on the part (only used if "customReflection" true)
	const U8 specularity[4];                AST( PERSIST NAME("specularity") ) // Specifies specularity on the part (only used if "customSpecularity" true)
	const PCColorLink eColorLink;           AST( PERSIST NAME("ColorLink") )
	const PCColorLink eMaterialLink;        AST( PERSIST NAME("MaterialLink") )
	const bool bGeoLink;                    AST( PERSIST NAME("GeoLink") ) // If true and there is a mirror geometry, consider them linked
	const bool bNoShadow;                   AST( PERSIST NAME("NoShadow") )
	const PCEditMode eEditMode;				NO_AST

	// Texture Driven Material Constants
	const F32 fPatternValue;                AST( PERSIST ) // -100 to 100
	const F32 fDetailValue;                 AST( PERSIST ) // -100 to 100
	const F32 fSpecularValue;               AST( PERSIST ) // -100 to 100
	const F32 fDiffuseValue;                AST( PERSIST ) // -100 to 100
	const F32 fMovableValue;                AST( PERSIST ) // -100 to 100

	// Movable Texture Constants
	const F32 fMovableX;					AST( NAME("MovableX") PERSIST ) // -100 to 100
	const F32 fMovableY;					AST( NAME("MovableY") PERSIST ) // -100 to 100
	const F32 fMovableScaleX;				AST( NAME("MovableScaleX","MovScaleX") DEF(1.0f) PERSIST ) // 0 to 100
	const F32 fMovableScaleY;				AST( NAME("MovableScaleY","MovScaleY") DEF(1.0f) PERSIST ) // 0 to 100
	const F32 fMovableRotation;				AST( NAME("MovableRotation","MovRotation") PERSIST ) // [-PI, PI)

	// Overrides used by artists and the runtime
	CONST_EARRAY_OF(PCMaterialColorV0) eaExtraColors;      AST( PERSIST NAME("ExtraColor") )
	CONST_EARRAY_OF(PCMaterialConstant) eaExtraConstants;  AST( PERSIST NAME("ExtraValue") )
	CONST_EARRAY_OF(PCTextureRef) eaExtraTextures;         AST( PERSIST NAME("ExtraTexture") )

	// Layer used for cloth
	// The layer  does not use: hBoneDef, hGeoDef, bGeoLink, pClothLayer
	OPTIONAL_STRUCT(PCPartV0) pClothLayer;    AST( PERSIST NAME("Cloth") )

	// Locks on the randomized fields used by the controlled
	// randomizer to tell it what shouldn't be changed.
	const PCControlledRandomLock eControlledRandomLocks; AST( PERSIST NAME("ControlledRandomLocks") )
} PCPartV0;


// WARNING: This structure is NOT USED any more in live game code
// except to migrate old characters forward.  Do not modify this structure!
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerCostumeV0 {
	CONST_STRING_POOLED pcName;                   AST( PERSIST POOL_STRING STRUCTPARAM KEY NAME("Name") )
	STRING_POOLED pcFileName;		              AST( CURRENTFILE NAME("FileName") )
	CONST_STRING_POOLED pcScope;			      AST( PERSIST POOL_STRING NAME("Scope") )
	const PCCostumeType eCostumeType;             AST( PERSIST NAME("CostumeType") )
	const U32 bAccountWideUnlock;                 AST( NAME("AccountUnlock") )

	// Body type information
	REF_TO(PCSkeletonDef) hSkeleton;              AST( PERSIST NAME("Skeleton") REFDICT(CostumeSkeleton) )
	REF_TO(SpeciesDef) hSpecies;			      AST( NAME("Species") REFDICT(Species) )
	const Gender eGender;						  AST( PERSIST NAME("Gender") SUBTABLE(GenderEnum))

	CONST_EARRAY_OF(PCRegionCategory) eaRegionCategories;  AST( PERSIST NAME("RegionCategory") )

	// Global settings for the costume
	const Vec4 skinColor;                         AST( PERSIST NAME("SkinColor") )
	const bool eDefaultColorLinkAll;			  AST( PERSIST NAME("DefaultColorLinkAll") DEF(true) )
	const bool eDefaultMaterialLinkAll;			  AST( PERSIST NAME("DefaultMaterialLinkAll") )
	CONST_FLOAT_EARRAY eafBodyScales;             AST( PERSIST NAME("BodyScale") )    // Scale is 0 to 100
	const F32 fMuscle;                            AST( PERSIST NAME("Muscle") )       // Scale is 0 to 100
	const F32 fHeight;                            AST( PERSIST NAME("Height") )       // In feet
	const F32 fTransparency;                      AST( PERSIST NAME("Transparency") ) // Scale is 0 to 100
	CONST_EARRAY_OF(PCScaleValue) eaScaleValues;  AST( PERSIST NAME("ScaleValues") )
	CONST_STRING_POOLED pcStance;                 AST( PERSIST POOL_STRING NAME("Stance") )
	CONST_EARRAY_OF(PCTexWords) eaTexWords;       AST( PERSIST NAME("TexWords") )

	// Overrides used by artists and the runtime
	const bool bNoCollision;                      AST( PERSIST NAME("NoCollision") )
	const bool bNoBodySock;						  AST( PERSIST NAME("NoBodySock") )
	const bool bNoRagdoll;						  AST( PERSIST NAME("NoRagdoll") )
	const bool bShellColl;						  AST( PERSIST NAME("ShellColl") )
	CONST_STRING_POOLED pcCollisionGeo;           AST( PERSIST POOL_STRING NAME("CollisionGeo") )
	CONST_EARRAY_OF(PCFX) eaFX;                   AST( PERSIST NAME("FX") )
	CONST_EARRAY_OF(PCFXSwap) eaFXSwap;           AST( PERSIST NAME("FXSwap") )
	CONST_EARRAY_OF(PCBitName) eaConstantBits;    AST( PERSIST NAME("ConstantBits") )

	// Costume parts last to make file look nicer
	CONST_EARRAY_OF(PCPartV0) eaParts;		      AST( PERSIST NAME("Part") )

	// Capsule override
	CONST_EARRAY_OF(SavedCapsule) ppCollCapsules;	  AST( PERSIST NAME("Capsule") FORCE_CONTAINER)
	const F32 fCollRadius;				              AST( PERSIST NAME("CollRadius") )
} PlayerCostumeV0;

extern ParseTable parse_PlayerCostumeV0[];
#define TYPE_parse_PlayerCostumeV0 PlayerCostumeV0

// ---- Version 2 Costume Structures ---------------------------------------------

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
AUTO_STRUCT AST_CONTAINER AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct PCMaterialColor {
	CONST_STRING_POOLED pcName;       AST( PERSIST SUBSCRIBE STRUCTPARAM POOL_STRING )
	const U8 color[4];                AST( PERSIST SUBSCRIBE STRUCTPARAM ) // Scale is where 255 255 255 255 is white
} PCMaterialColor;
extern ParseTable parse_PCMaterialColor[];
#define TYPE_parse_PCMaterialColor PCMaterialColor

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCMovableTextureInfo {
	REF_TO(PCTextureDef) hMovableTexture;   AST( PERSIST SUBSCRIBE NAME("MovableTexture") REFDICT(CostumeTexture))

	const F32 fMovableX;					AST( PERSIST SUBSCRIBE NAME("MovableX") ) // -100 to 100
	const F32 fMovableY;					AST( PERSIST SUBSCRIBE NAME("MovableY") ) // -100 to 100
	const F32 fMovableScaleX;				AST( PERSIST SUBSCRIBE NAME("MovableScaleX","MovScaleX") DEF(1.0f) ) // 0 to 100
	const F32 fMovableScaleY;				AST( PERSIST SUBSCRIBE NAME("MovableScaleY","MovScaleY") DEF(1.0f) ) // 0 to 100
	const F32 fMovableRotation;				AST( PERSIST SUBSCRIBE NAME("MovableRotation","MovRotation") ) // [-PI, PI)
	const F32 fMovableValue;                AST( PERSIST SUBSCRIBE NAME("MovableValue") ) // -100 to 100
} PCMovableTextureInfo;

extern ParseTable parse_PCMovableTextureInfo[];
#define TYPE_parse_PCMovableTextureInfo PCMovableTextureInfo

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCCustomColorInfo {
	const U8 glowScale[4];                  AST( PERSIST SUBSCRIBE NAME("glowScale") ) // Specifies glow scale per color[n]. 0 or 1=none, 10 = Amazing glow
	const U8 reflection[4];                 AST( PERSIST SUBSCRIBE NAME("Reflection") ) // Specifies reflectivity on the part (only used if "customReflection" true)
	const U8 specularity[4];                AST( PERSIST SUBSCRIBE NAME("specularity") ) // Specifies specularity on the part (only used if "customSpecularity" true)

	// Flags grouped together for memory savings
	const bool bCustomReflection;           AST( PERSIST SUBSCRIBE NAME("CustomReflection") )
	const bool bCustomSpecularity;          AST( PERSIST SUBSCRIBE NAME("CustomSpecularity") )
} PCCustomColorInfo;

extern ParseTable parse_PCCustomColorInfo[];
#define TYPE_parse_PCCustomColorInfo PCCustomColorInfo

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCTextureValueInfo {
	// Texture Driven Material Constants
	const F32 fPatternValue;                AST( PERSIST SUBSCRIBE NAME("PatternValue") ) // -100 to 100
	const F32 fDetailValue;                 AST( PERSIST SUBSCRIBE NAME("DetailValue") ) // -100 to 100
	const F32 fSpecularValue;               AST( PERSIST SUBSCRIBE NAME("SpecularValue") ) // -100 to 100
	const F32 fDiffuseValue;                AST( PERSIST SUBSCRIBE NAME("DiffuseValue") ) // -100 to 100
} PCTextureValueInfo;

extern ParseTable parse_PCTextureValueInfo[];
#define TYPE_parse_PCTextureValueInfo PCTextureValueInfo

// WARNING: This data structure is both persisted for players and
// is exported into files on player PCs.  All changes to this must be
// backward compatible or migration code needs to be built in on load
// in several places.
AUTO_STRUCT AST_CONTAINER;
typedef struct PCArtistPartData {
	// Overrides used by artists and the runtime
	CONST_EARRAY_OF(PCMaterialColor) eaExtraColors;        AST( PERSIST SUBSCRIBE NAME("ExtraColor") )
	CONST_EARRAY_OF(PCMaterialConstant) eaExtraConstants;  AST( PERSIST SUBSCRIBE NAME("ExtraValue") )
	CONST_EARRAY_OF(PCTextureRef) eaExtraTextures;         AST( PERSIST SUBSCRIBE NAME("ExtraTexture") )

	// Flags grouped together for memory savings
	const bool bNoShadow;                                  AST( PERSIST SUBSCRIBE NAME("NoShadow") )
} PCArtistPartData;

extern ParseTable parse_PCArtistPartData[];
#define TYPE_parse_PCArtistPartData PCArtistPartData

typedef struct PCPart PCPart;

// This structure defines the details of an instance of a costume part
// NOTE: Do not give DEFAULT values to any fields in here!
//
// WARNING: THIS DATA STRUCTURE IS BOTH PERSISTED FOR PLAYERS AND
// IS EXPORTED INTO FILES ON PLAYER PCs.  ALL CHANGES TO THIS MUST BE
// BACKWARD COMPATIBLE OR MIGRATION CODE NEEDS TO BE BUILT IN ON LOAD
// IN SEVERAL PLACES.
AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct PCPart {
	// Choices made for the part
	REF_TO(PCBoneDef) hBoneDef;             AST( PERSIST SUBSCRIBE NAME("Bone") REFDICT(CostumeBone))
	REF_TO(PCGeometryDef) hGeoDef;          AST( PERSIST SUBSCRIBE NAME("Geometry") REFDICT(CostumeGeometry))
	REF_TO(PCMaterialDef) hMatDef;          AST( PERSIST SUBSCRIBE NAME("Material") REFDICT(CostumeMaterial))
	REF_TO(PCTextureDef) hPatternTexture;   AST( PERSIST SUBSCRIBE NAME("PatternTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hDetailTexture;    AST( PERSIST SUBSCRIBE NAME("DetailTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hSpecularTexture;  AST( PERSIST SUBSCRIBE NAME("SpecularTexture") REFDICT(CostumeTexture))
	REF_TO(PCTextureDef) hDiffuseTexture;   AST( PERSIST SUBSCRIBE NAME("DiffuseTexture") REFDICT(CostumeTexture))
	CONST_OPTIONAL_STRUCT(PCMovableTextureInfo) pMovableTexture; AST( PERSIST SUBSCRIBE NAME("Movable") )

	// Colors for the part.  Scale is where 255/255/255/255 is white.
	const U8 color0[4];                     AST( PERSIST SUBSCRIBE NAME("Color_0") )
	const U8 color1[4];                     AST( PERSIST SUBSCRIBE NAME("Color_1") )
	const U8 color2[4];                     AST( PERSIST SUBSCRIBE NAME("Color_2") )
	const U8 color3[4];                     AST( PERSIST SUBSCRIBE NAME("Color_3") )

	// Less frequently used part customization
	CONST_OPTIONAL_STRUCT(PCCustomColorInfo) pCustomColors; AST( PERSIST SUBSCRIBE NAME("CustomColors") )
	CONST_OPTIONAL_STRUCT(PCTextureValueInfo) pTextureValues; AST( PERSIST SUBSCRIBE NAME("TextureValues") )

	// Artist data
	CONST_OPTIONAL_STRUCT(PCArtistPartData) pArtistData; AST( PERSIST SUBSCRIBE NAME("ArtistData") )

	// Color linking and other editor stuff that we persist so you can re-edit
	const U8 eColorLink;                    AST( PERSIST SUBSCRIBE NAME("ColorLink") SUBTABLE(PCColorLinkEnum) )
	const U8 eMaterialLink;                 AST( PERSIST SUBSCRIBE NAME("MaterialLink") SUBTABLE(PCColorLinkEnum) )
	const U8 eEditMode;				        NO_AST // Uses PCEditMode enum but marked as U8 for memory reasons
	const PCControlledRandomLock eControlledRandomLocks; AST( PERSIST SUBSCRIBE NAME("ControlledRandomLocks") )

	// Non-persisted fields for editor stuff
	const int iBoneGroupIndex;				AST( CLIENT_ONLY NAME("BoneGroupIndex", "iBoneGroupIndex") DEFAULT(-1) ) //Index into PCSkeletonDef->eaBoneGroups; -1 = no bone group

	// Layer used for cloth
	// The layer  does not use: hBoneDef, hGeoDef, pClothLayer
	CONST_OPTIONAL_STRUCT(PCPart) pClothLayer;    AST( PERSIST SUBSCRIBE NAME("Cloth") )
	CONST_STRING_POOLED pchOrigBone;				AST( PERSIST SUBSCRIBE POOL_STRING NO_TEXT_SAVE)
} PCPart;

extern ParseTable parse_PCPart[];
#define TYPE_parse_PCPart PCPart

AUTO_STRUCT AST_CONTAINER;
typedef struct PCArtistCostumeData {
	// Collision override controls
	CONST_STRING_POOLED pcCollisionGeo;           AST( PERSIST SUBSCRIBE POOL_STRING NAME("CollisionGeo") )
	CONST_EARRAY_OF(SavedCapsule) ppCollCapsules; AST( PERSIST SUBSCRIBE NAME("Capsule") FORCE_CONTAINER)
	const F32 fCollRadius;						  AST( PERSIST SUBSCRIBE NAME("CollRadius") )

	// FX override controls
	CONST_EARRAY_OF(PCFX) eaFX;                   AST( PERSIST SUBSCRIBE NAME("FX") )
	CONST_EARRAY_OF(PCFXSwap) eaFXSwap;           AST( PERSIST SUBSCRIBE NAME("FXSwap") )

	// Other override controls
	CONST_EARRAY_OF(PCBitName) eaConstantBits;    AST( PERSIST SUBSCRIBE NAME("ConstantBits") )
	const F32 fTransparency;                      AST( PERSIST SUBSCRIBE NAME("Transparency") ) // Scale is 0 to 100

	CONST_OPTIONAL_STRUCT(PCFX) pDismountFX;	  AST( PERSIST SUBSCRIBE NAME("DismountFX") )

	// Other overrides that are grouped to save memory
	const bool bNoCollision;                      AST( PERSIST SUBSCRIBE NAME("NoCollision") )
	const bool bNoBodySock;					      AST( PERSIST SUBSCRIBE NAME("NoBodySock") )
	const bool bNoRagdoll;					      AST( PERSIST SUBSCRIBE NAME("NoRagdoll") )
	const bool bShellColl;					      AST( PERSIST SUBSCRIBE NAME("ShellColl") )
	const bool bHasOneWayCollision;			      AST( PERSIST SUBSCRIBE NAME("hasOneWayCollision") )

	// This flag is set if you want to unlock all CMats and CTexs without checking agaist the geometry 
	const bool bUnlockAll;                        AST( NAME("UnlockAll") ) // NOT PERSISTED

	// This flag is set if you want to unlock all CMats without checking agaist the geometry 
	const bool bUnlockAllCMat;                    AST( NAME("UnlockAllCmat") ) // NOT PERSISTED

	// This flag is set if you want to unlock all CTexs without checking agaist the geometry 
	const bool bUnlockAllCTex;                    AST( NAME("UnlockAllCTex") ) // NOT PERSISTED

} PCArtistCostumeData;

extern ParseTable parse_PCArtistCostumeData[];
#define TYPE_parse_PCArtistCostumeData PCArtistCostumeData

AUTO_STRUCT AST_CONTAINER;
typedef struct PCBodySockInfo
{
	CONST_STRING_POOLED pcBodySockGeo;	AST(PERSIST SUBSCRIBE POOL_STRING NAME("BodySockGeo"))
		CONST_STRING_POOLED pcBodySockPose;	AST(PERSIST SUBSCRIBE POOL_STRING NAME("BodySockPose"))
		const Vec3 vBodySockMin;			AST(PERSIST SUBSCRIBE NAME("BodySockMin"))
		const Vec3 vBodySockMax;			AST(PERSIST SUBSCRIBE NAME("BodySockMax"))
} PCBodySockInfo;

extern ParseTable parse_PCBodySockInfo[];
#define TYPE_parse_PCBodySockInfo PCBodySockInfo

// This structure defines the details of an instance of a player costume
// NOTE: Do not give DEFAULT values to any fields in here!
//
// WARNING: THIS DATA STRUCTURE IS BOTH PERSISTED FOR PLAYERS AND
// IS EXPORTED INTO FILES ON PLAYER PCs.  ALL CHANGES TO THIS MUST BE
// BACKWARD COMPATIBLE OR MIGRATION CODE NEEDS TO BE BUILT IN ON LOAD
// IN SEVERAL PLACES.
AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT
AST_IGNORE(bCapsuleCollisionsUseMovementOrientation) AST_IGNORE(Tags) AST_IGNORE_STRUCT(UGCProperties);
typedef struct PlayerCostume {
	CONST_STRING_POOLED pcName;                   AST( PERSIST SUBSCRIBE POOL_STRING STRUCTPARAM KEY NAME("Name") )
	STRING_POOLED pcFileName;		              AST( CURRENTFILE NAME("FileName") ) // NOT PERSISTED
	CONST_STRING_POOLED pcScope;			      AST( POOL_STRING NAME("Scope") )    // NOT PERSISTED

	// Body type information
	REF_TO(PCSkeletonDef) hSkeleton;              AST( PERSIST SUBSCRIBE NAME("Skeleton") REFDICT(CostumeSkeleton) )
	REF_TO(SpeciesDef) hSpecies;			      AST( NAME("Species") REFDICT(Species) ) // NOT PERSISTED
	CONST_OPTIONAL_STRUCT(PCBodySockInfo) pBodySockInfo;	AST( PERSIST SUBSCRIBE NAME(BodySockInfo))

	// Small values grouped together for size savings
	const U8 eCostumeType;	                      AST( PERSIST SUBSCRIBE NAME("CostumeType") SUBTABLE(PCCostumeTypeEnum) )
	const U8 eGender;						      AST( PERSIST SUBSCRIBE NAME("Gender") SUBTABLE(GenderEnum) )
	const bool eDefaultColorLinkAll;			  AST( PERSIST SUBSCRIBE NAME("DefaultColorLinkAll") DEF(true) )
	const bool eDefaultMaterialLinkAll;			  AST( PERSIST SUBSCRIBE NAME("DefaultMaterialLinkAll") )

	REF_TO(PCVoice) hVoice;						  AST( PERSIST SUBSCRIBE NAME("Voice") )

	// Values for costume-level scales
	CONST_FLOAT_EARRAY eafBodyScales;             AST( PERSIST SUBSCRIBE NAME("BodyScale") )    // Scale is 0 to 100
	CONST_EARRAY_OF(PCScaleValue) eaScaleValues;  AST( PERSIST SUBSCRIBE NAME("ScaleValues") )
	const F32 fMuscle;                            AST( PERSIST SUBSCRIBE NAME("Muscle") )       // Scale is 0 to 100
	const F32 fHeight;                            AST( PERSIST SUBSCRIBE NAME("Height") )       // In feet

	// Special data that can be set for the costume
	CONST_STRING_POOLED pcStance;                 AST( PERSIST SUBSCRIBE POOL_STRING NAME("Stance") )
	CONST_EARRAY_OF(PCTexWords) eaTexWords;       AST( PERSIST SUBSCRIBE NAME("TexWords") )
	CONST_OPTIONAL_STRUCT(PCArtistCostumeData) pArtistData; AST( PERSIST SUBSCRIBE NAME("ArtistData") )

	// Top level value for skin color
	const U8 skinColor[4];                        AST( PERSIST SUBSCRIBE NAME("ColorSkin") )

	// Region/Category data for editor
	CONST_EARRAY_OF(PCRegionCategory) eaRegionCategories;  AST( PERSIST SUBSCRIBE NAME("RegionCategory") )

	// Costume parts last to make file look nicer
	CONST_EARRAY_OF(PCPart) eaParts;		      AST( PERSIST SUBSCRIBE NAME("Part") )

	// This flag is set for costumes that the player is not allowed to change
	const bool bPlayerCantChange;				  AST( PERSIST SUBSCRIBE NAME("PlayerCantChange") )

	// This flag is set on artist made costumes that should unlock account wide
	const bool bAccountWideUnlock;                AST( NAME("AccountUnlock") ) // NOT PERSISTED

	// This flag is set on artist made costumes that should pre-load on the client
	const bool bLoadedOnClient;                   AST( NAME("LoadedOnClient") ) // NOT PERSISTED

	// When changing between costumes that use the same skeletons, always save the sequencers onto the new skeleton 
	const bool bCostumeChangeAlwaysSaveSequencersIfSkelsMatch; AST( NAME("CostumeChangeAlwaysSaveSequencersIfSkelsMatch") ) // NOT PERSISTED
	
} PlayerCostume;

extern ParseTable parse_PlayerCostume[];
#define TYPE_parse_PlayerCostume PlayerCostume

#define IS_PLAYER_COSTUME(pCostume) ((pCostume->eCostumeType == kPCCostumeType_Player) || (pCostume->eCostumeType == kPCCostumeType_UGC))
#define GET_COSTUME_TRANSPARENCY(pCostume) ((pCostume)->pArtistData ? (pCostume)->pArtistData->fTransparency : 0)
#define GET_PART_GLOWSCALE(pPart, index) ((pPart)->pCustomColors ? (pPart)->pCustomColors->glowScale[(index)] : 0)

#define COPY_COSTUME_COLOR(src,dest)     ((dest)[0] = (src)[0], (dest)[1] = (src)[1], (dest)[2] = (src)[2], (dest)[3] = (src)[3])
#define IS_SAME_COSTUME_COLOR(c1,c2)     ((c1[0] == c2[0]) && (c1[1] == c2[1]) && (c1[2] == c2[2]) && (c1[3] == c2[3]))
#define VEC4_TO_COSTUME_COLOR(v4,color)  ((color)[0] = (v4)[0]+.5, (color)[1] = (v4)[1]+.5, (color)[2] = (v4)[2]+.5, (color)[3] = (v4)[3]+.5)
#define VEC3_TO_COSTUME_COLOR(v3,color)  ((color)[0] = (v3)[0]+.5, (color)[1] = (v3)[1]+.5, (color)[2] = (v3)[2]+.5, (color)[3] = 255)
