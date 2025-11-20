#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "Message.h"
#include "CostumeCommon.h"
#include "NameGen.h"

typedef struct PTNodeDefRef PTNodeDefRef;
typedef struct CritterDef CritterDef;
typedef struct GameAccountData GameAccountData;
typedef struct Entity Entity;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct BodyScaleLimit
{
	CONST_STRING_POOLED pcName;				AST( PERSIST SUBSCRIBE NAME("Name") STRUCTPARAM POOL_STRING )
	const F32 fMin;							AST( PERSIST SUBSCRIBE NAME("Min") )
	const F32 fMax;							AST( PERSIST SUBSCRIBE NAME("Max") )
} BodyScaleLimit;

AUTO_STRUCT;
typedef struct BodyScaleSP
{
	const char *pcName;						AST( STRUCTPARAM POOL_STRING )
} BodyScaleSP;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct BoneScaleLimit
{
	CONST_STRING_POOLED pcName;				AST( PERSIST SUBSCRIBE NAME("Name") STRUCTPARAM POOL_STRING )
	const F32 fMin;							AST( PERSIST SUBSCRIBE NAME("Min") )
	const F32 fMax;							AST( PERSIST SUBSCRIBE NAME("Max") )
} BoneScaleLimit;

AUTO_STRUCT;
typedef struct BoneScaleSP
{
	const char *pcName;						AST( STRUCTPARAM POOL_STRING )
} BoneScaleSP;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct TextureLimits
{
	REF_TO(PCTextureDef) hTexture;				AST( PERSIST SUBSCRIBE STRUCTPARAM REFDICT(CostumeTexture) NAME("Texture") )

	const bool bOverrideConstValues;			AST( PERSIST SUBSCRIBE NAME("OverrideConstValues") )
	const F32 fValueMin;						AST( PERSIST SUBSCRIBE NAME("ConstValueMin") )
	const F32 fValueMax;						AST( PERSIST SUBSCRIBE NAME("ConstValueMax") )

	const bool bOverrideMovableValues;			AST(PERSIST SUBSCRIBE NAME("OverrideMovableValues"))
	const F32 fMovableMinX;						AST(PERSIST SUBSCRIBE NAME("MovableMinX"))
	const F32 fMovableMaxX;						AST(PERSIST SUBSCRIBE NAME("MovableMaxX"))
	const F32 fMovableMinY;						AST(PERSIST SUBSCRIBE NAME("MovableMinY"))
	const F32 fMovableMaxY;						AST(PERSIST SUBSCRIBE NAME("MovableMaxY"))
	const F32 fMovableMinScaleX;				AST(PERSIST SUBSCRIBE NAME("MovableMinScaleX") DEF(1.0f))
	const F32 fMovableMaxScaleX;				AST(PERSIST SUBSCRIBE NAME("MovableMaxScaleX") DEF(1.0f))
	const F32 fMovableMinScaleY;				AST(PERSIST SUBSCRIBE NAME("MovableMinScaleY") DEF(1.0f))
	const F32 fMovableMaxScaleY;				AST(PERSIST SUBSCRIBE NAME("MovableMaxScaleY") DEF(1.0f))
	const bool bMovableCanEditPosition;			AST(PERSIST SUBSCRIBE NAME("MovableCanEditPosition"))
	const bool bMovableCanEditRotation;			AST(PERSIST SUBSCRIBE NAME("MovableCanEditRotation"))
	const bool bMovableCanEditScale;			AST(PERSIST SUBSCRIBE NAME("MovableCanEditScale"))
} TextureLimits;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct MaterialLimits
{
	REF_TO(PCMaterialDef) hMaterial;		AST( PERSIST SUBSCRIBE NAME("Material") REFDICT(CostumeMaterial) )
	const bool bAllowAllTex;                AST( PERSIST SUBSCRIBE NAME("AllowAllTextures") DEFAULT(1) )
	const bool bRequiresPattern;			AST( PERSIST SUBSCRIBE NAME("RequiresPattern") )
	const bool bRequiresDetail;				AST( PERSIST SUBSCRIBE NAME("RequiresDetail") )
	const bool bRequiresSpecular;			AST( PERSIST SUBSCRIBE NAME("RequiresSpecular") )
	const bool bRequiresDiffuse;			AST( PERSIST SUBSCRIBE NAME("RequiresDiffuse") )
	const bool bRequiresMovable;			AST( PERSIST SUBSCRIBE NAME("RequiresMovable") )
	CONST_EARRAY_OF(TextureLimits) eaTextures;	AST( PERSIST SUBSCRIBE NAME("AllowedTexture") )
} MaterialLimits;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct GeometryLimits
{
	REF_TO(PCGeometryDef) hGeo;				   AST( PERSIST SUBSCRIBE NAME("Geometry") STRUCTPARAM REFDICT(CostumeGeometry) )
	const bool bAllowAllMat;                   AST( PERSIST SUBSCRIBE NAME("AllowAllMaterials") DEFAULT(1) )
	CONST_EARRAY_OF(MaterialLimits) eaMaterials;	AST( PERSIST SUBSCRIBE NAME("AllowedMaterial") )

	REF_TO(UIColorSet) hBodyColorSet0;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet1") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet2;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet2") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet3;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet3") REFDICT(CostumeColors) )
	REF_TO(PCColorQuadSet) hColorQuadSet;	   AST( PERSIST SUBSCRIBE NAME("ColorQuadSet") REFDICT(CostumeColorQuads) )
} GeometryLimits;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct CategoryLimits
{
	REF_TO(PCCategory) hCategory;			AST( PERSIST SUBSCRIBE STRUCTPARAM NAME("Category") REFDICT(CostumeCategory) )
} CategoryLimits;

AUTO_STRUCT;
typedef struct ValidatePlayerCostumeItems
{
	bool bTestSkinColor;					AST( DEF(true) )
		bool bTestHeight;						AST( DEF(true) )
		bool bTestMuscle;						AST( DEF(true) )
		bool bTestStance;						AST( DEF(true) )
		bool bTestBones;						AST( DEF(false) )
		bool bTestBodyScales;					AST( DEF(false) )
		bool bTestBoneScales;					AST( DEF(false) )
		PCBoneRef **eaBonesUsed;			    AST( NAME("TestBone") )
		BodyScaleSP **eaBodyScalesUsed;			AST( NAME("TestBodyScale") )
		BoneScaleSP **eaBoneScalesUsed;			AST( NAME("TestBoneScale") )
} ValidatePlayerCostumeItems;

AUTO_STRUCT;
typedef struct CostumePresetCategory
{
	const char *pcName;						AST( STRUCTPARAM KEY POOL_STRING NAME("Name") )
		const char *pcScope;					AST( POOL_STRING )
		const char *pcFileName;					AST( CURRENTFILE )

		DisplayMessage displayNameMsg;			AST( STRUCT(parse_DisplayMessage) )

		const char *pcGroup;					AST( POOL_STRING )
		const char *pcSlotType;					AST( NAME("SlotType") )
		bool bExcludeGroup;						AST( DEF(false) )
		bool bExcludeSlotType;					AST( DEF(false) )

		ValidatePlayerCostumeItems validatePlayerValues;	AST( NAME("CostumeTestParts") )
} CostumePresetCategory;

AUTO_STRUCT;
typedef struct CostumePreset
{
	const char *pcName;						AST( STRUCTPARAM KEY POOL_STRING)
	DisplayMessage displayNameMsg;			AST( STRUCT(parse_DisplayMessage) )
	REF_TO(PlayerCostume) hCostume;			AST( NAME("Costume") )
	const char *pcGroup;					AST( POOL_STRING )
	const char *pcSlotType;					AST( NAME("SlotType") )
	bool bOverrideExcludeValues;				AST( DEF(false) )
	bool bExcludeGroup;						AST( DEF(false) )
	bool bExcludeSlotType;					AST( DEF(false) )
	F32 fOrder;
	bool bOverrideValidateValues;			AST( DEF(false) )
	ValidatePlayerCostumeItems validatePlayerValues;	AST( NAME("CostumeTestParts") )

	REF_TO(CostumePresetCategory) hPresetCategory;	AST( NAME("PresetCategory") )
} CostumePreset;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct SpeciesBoneData
{
	REF_TO(PCBoneDef) hBone;				   AST( PERSIST SUBSCRIBE NAME("Bone") STRUCTPARAM REFDICT(CostumeBone) )
	const bool bRequires;					   AST( PERSIST SUBSCRIBE NAME("Requires") )

	REF_TO(UIColorSet) hBodyColorSet0;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet1") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet2;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet2") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet3;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet3") REFDICT(CostumeColors) )
	REF_TO(PCColorQuadSet) hColorQuadSet;	   AST( PERSIST SUBSCRIBE NAME("ColorQuadSet") REFDICT(CostumeColorQuads) )
} SpeciesBoneData;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct VoiceRef
{
	REF_TO(PCVoice) hVoice;			AST( PERSIST SUBSCRIBE NAME("Voice") STRUCTPARAM REFDICT(CostumeVoice) )
} VoiceRef;

extern ParseTable parse_CostumePreset[];
#define TYPE_parse_CostumePreset CostumePreset
extern StaticDefineInt CharClassTypesEnum[];

AUTO_ENUM;
typedef enum NameOrder
{
	kNameOrder_FML,
	kNameOrder_LFM
} NameOrder;

AUTO_STRUCT;
typedef struct SpeciesPowerSuggestion
{
	const char* pchClass; AST(STRUCTPARAM KEY REQUIRED)
	PTNodeDefRef **eaNodes; AST(NAME("Node"))
} SpeciesPowerSuggestion;

AUTO_STRUCT;
typedef struct SpeciesPowerTableBonus
{
	const char* pchTableName; AST(STRUCTPARAM POOL_STRING KEY)
	F32 *pfValues;					AST(NAME(Values))
} SpeciesPowerTableBonus;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT
AST_IGNORE(IsCustomSpecies);		// This was designed for user-defined shareable species defs which never got implemented. Got tangled up with Aliens.
typedef struct SpeciesDef
{
	CONST_STRING_POOLED pcName;				AST( PERSIST SUBSCRIBE STRUCTPARAM KEY POOL_STRING NAME("Name") )
	const char *pcScope;					AST( POOL_STRING NAME("Scope") )
	const char *pcFileName;					AST( CURRENTFILE NAME("FileName") )

	const U32	eType;						AST( PERSIST SUBSCRIBE SUBTABLE(CharClassTypesEnum) NAME("Type") )

	// Values used internally to identify species
	CONST_STRING_MODIFIABLE pcSpeciesName;	AST( PERSIST SUBSCRIBE NAME("SpeciesName") )
	const char *pcTextureName;				AST( POOL_STRING NAME("TextureName") )
	const Gender eGender;					AST( PERSIST SUBSCRIBE NAME("Gender") )

	// Species "group", which is what you actually think when you think Species.
	//  All Species that are the "same" from a gameplay mechanics perspective
	//  should be in the same group.
	const char *pcSpeciesGroup;				AST( POOL_STRING NAME("SpeciesGroup") )

	// Display information
	DisplayMessage displayNameMsg;			AST( STRUCT(parse_DisplayMessage) NAME("displayNameMsg") )
	DisplayMessage genderNameMsg;			AST( STRUCT(parse_DisplayMessage) NAME("genderNameMsg") )
	DisplayMessage descriptionMsg;			AST( STRUCT(parse_DisplayMessage) NAME("descriptionMsg") )

	//Animation Bits
	CONST_STRING_MODIFIABLE pcCostumeBits;	AST( PERSIST SUBSCRIBE NAME("CostumeBits") )

	// Costume configuration data
	REF_TO(PCSkeletonDef) hSkeleton;		AST( PERSIST SUBSCRIBE NAME("Skeleton") )
	EARRAY_OF(CostumePreset) eaPresets;		AST( NAME("CostumePreset") )

	// The Stances to make available; If none are listed than all listed in Skeleton are available
	CONST_STRING_POOLED pcDefaultStance;    AST( PERSIST SUBSCRIBE POOL_STRING NAME("DefaultStance") )
	CONST_EARRAY_OF(PCStanceInfo) eaStanceInfo;			AST( PERSIST SUBSCRIBE NAME("Stance") )

	CONST_EARRAY_OF(SpeciesBoneData) eaBoneData;		AST( PERSIST SUBSCRIBE NAME("BoneData") )

	CONST_EARRAY_OF(CategoryLimits) eaCategories;       AST( PERSIST SUBSCRIBE NAME("AllowedCategory") )
	CONST_EARRAY_OF(GeometryLimits) eaGeometries;       AST( PERSIST SUBSCRIBE NAME("AllowedGeometry") )

	CONST_EARRAY_OF(BodyScaleLimit) eaBodyScaleLimits;	AST( PERSIST SUBSCRIBE NAME("BodyScaleLimits") )
	CONST_EARRAY_OF(BoneScaleLimit) eaBoneScaleLimits;	AST( PERSIST SUBSCRIBE NAME("BoneScaleLimits") )

	const bool bNoHeightChange;						AST( PERSIST SUBSCRIBE NAME("NoHeightChange") )
	const F32 fMinHeight;							AST( PERSIST SUBSCRIBE NAME("MinHeight") )
	const F32 fMaxHeight;							AST( PERSIST SUBSCRIBE NAME("MaxHeight") )

	const bool bNoMuscle;							AST( PERSIST SUBSCRIBE NAME("NoMuscle") )
	const F32 fMinMuscle;							AST( PERSIST SUBSCRIBE NAME("MinMuscle") )
	const F32 fMaxMuscle;							AST( PERSIST SUBSCRIBE NAME("MaxMuscle") )

	REF_TO(UIColorSet) hSkinColorSet;         AST( PERSIST SUBSCRIBE NAME("SkinColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet;         AST( PERSIST SUBSCRIBE NAME("BodyColorSet0","BodyColorSet") REFDICT(CostumeColors) )
	REF_TO(UIColorSet) hBodyColorSet1;        AST( PERSIST SUBSCRIBE NAME("BodyColorSet1") REFDICT(CostumeColors) ) //inherits from BodyColorSet0 if not present
	REF_TO(UIColorSet) hBodyColorSet2;        AST( PERSIST SUBSCRIBE NAME("BodyColorSet2") REFDICT(CostumeColors) ) //inherits from BodyColorSet0 if not present
	REF_TO(UIColorSet) hBodyColorSet3;        AST( PERSIST SUBSCRIBE NAME("BodyColorSet3") REFDICT(CostumeColors) ) //inherits from BodyColorSet0 if not present
	REF_TO(PCColorQuadSet) hColorQuadSet;     AST( PERSIST SUBSCRIBE NAME("ColorQuadSet") REFDICT(CostumeColorQuads) )

	const bool bAllowAllVoices;					AST( PERSIST SUBSCRIBE NAME("AllowAllVoices") DEF(true))
	CONST_EARRAY_OF(VoiceRef) eaAllowedVoices;	AST( PERSIST SUBSCRIBE NAME("AllowedVoice"))

	//as far as the UI is concerned, this is the power tree you'll get for picking this species.
	const char* pchUIPowerTree;		AST( POOL_STRING NAME(UIPowerTree) )

	bool bIsGenSpecies;		AST( NAME("IsGenSpecies") )
	bool bHideInBeta;       AST( NAME("HideInBeta") )

	const NameOrder eNameOrder;						AST( PERSIST SUBSCRIBE NAME("NameOrder") )
	CONST_STRING_MODIFIABLE pcDefaultNamePrefix;	AST( PERSIST SUBSCRIBE NAME("DefaultNamePrefix") )
	CONST_STRING_MODIFIABLE pcDefaultSubNamePrefix;	AST( PERSIST SUBSCRIBE NAME("DefaultSubNamePrefix") )
	CONST_EARRAY_OF(NameTemplateListRef) eaNameTemplateLists;	AST( PERSIST SUBSCRIBE NAME("RandomNameRule","FirstNameRule","NickNameRule","LastNameRule"))
	CONST_EARRAY_OF(PCFXSwap) eaFXSwap;           AST( PERSIST NAME("FXSwap") )

	F32 fOrder;								AST( NAME("Order") )
	PCRestriction eRestriction;				AST( NAME("RestrictedTo") FLAGS )

	//Unlock Rule
	const char *pcUnlockCode;				AST( NAME("UnlockCode") )

	// Unlock Timestamp (in timeServerSecondsSince2000() )
	U32 uUnlockTimestamp;					AST( NAME("UnlockTimestamp") )

	SpeciesPowerTableBonus **eaBonusTablePoints; AST( NAME("BonusTablePoints"))
	SpeciesPowerSuggestion **eaPowerSuggestions; AST( NAME("PowerSuggestions") NAME("PowerSuggestion"))
	CONST_STRING_MODIFIABLE pcDefaultClassPathName;		AST(NAME("DefaultClassPath"))
} SpeciesDef;

AUTO_STRUCT;
typedef struct SpeciesDefRef
{
	REF_TO(SpeciesDef) hSpecies;			AST(STRUCTPARAM REFDICT(Species))
} SpeciesDefRef;

AUTO_ENUM;
typedef enum SpeciesDefiningType {
	kSpeciesDefiningType_Invalid =		0,
	kSpeciesDefiningType_Default =		1, //All items in the feature set are the default starting species items - Get non-geo/mat/tex defaults from SpeciesDefault

	//The order of these 8 enums matters
	kSpeciesDefiningType_Geometry =		2,
	kSpeciesDefiningType_Grouped =		3, //All items in this feature set must be include together - Normally only one thing in the feature set is chosen
	kSpeciesDefiningType_Material =		4,
	kSpeciesDefiningType_Pattern =		5,
	kSpeciesDefiningType_Detail =		6,
	kSpeciesDefiningType_Specular =		7,
	kSpeciesDefiningType_Diffuse =		8,
	kSpeciesDefiningType_Movable =		9,

	kSpeciesDefiningType_BodyScale =	10,
	kSpeciesDefiningType_BoneScale =	11,
	kSpeciesDefiningType_Height =		12,
	kSpeciesDefiningType_Muscle =		13,
} SpeciesDefiningType;
extern StaticDefineInt SpeciesDefiningTypeEnum[];

AUTO_STRUCT;
typedef struct SpeciesDefiningFeature
{
	const char *pcName;						AST( STRUCTPARAM KEY POOL_STRING NAME("Name") )
	const char *pcScope;					AST( POOL_STRING )
	const char *pcFileName;					AST( CURRENTFILE )

	const char *pcMatchName;				AST( POOL_STRING NAME("MatchName") )

	SpeciesDefiningType	eType;				AST( NAME("Type") )
	REF_TO(PCBoneDef) hExcludeBone;			AST( NAME("ExcludeOption") REFDICT(CostumeBone) )

	REF_TO(PCSkeletonDef) hSkeleton;		AST( NAME("Skeleton") REFDICT(CostumeSkeleton) )
	REF_TO(SpeciesDef) hSpeciesDefault;		AST( NAME("SpeciesDefault") REFDICT(Species) )

	CategoryLimits **eaCategories;          AST( NAME("AllowedCategory") ) //Only Type Default supports Categories
	GeometryLimits **eaGeometries;          AST( NAME("AllowedGeometry") )

	BodyScaleLimit **eaBodyScaleLimits;		AST( NAME("BodyScaleLimits") )
	BoneScaleLimit **eaBoneScaleLimits;		AST( NAME("BoneScaleLimits") )

	F32 fMinHeight;
	F32 fMaxHeight;

	F32 fMinMuscle;
	F32 fMaxMuscle;
}SpeciesDefiningFeature;

AUTO_STRUCT;
typedef struct SpeciesDefiningFeatureRef
{
	REF_TO(SpeciesDefiningFeature) hSpeciesDefiningFeatureRef;	AST( STRUCTPARAM REFDICT(SpeciesFeature) )
}SpeciesDefiningFeatureRef;

AUTO_STRUCT;
typedef struct CritterDefGenRef
{
	REF_TO(CritterDef) hCritterDef;			AST(STRUCTPARAM REFDICT(CritterDef))
}CritterDefGenRef;

AUTO_STRUCT;
typedef struct CritterGroupGen
{
	CritterDefGenRef **eaCritterDef;		AST(NAME(CritterDef))
	F32	fWeight;							AST(NAME("Weight") DEF(1.0f))
}CritterGroupGen;

AUTO_STRUCT;
typedef struct UniformDefGenRef
{
	REF_TO(PlayerCostume) hPlayerCostume;	AST(STRUCTPARAM REFDICT(PlayerCostume))
}UniformDefGenRef;

AUTO_STRUCT;
typedef struct UniformGroupGen
{
	UniformDefGenRef **eaUniforms;			AST(NAME(Uniform))
	F32	fWeight;							AST(NAME("Weight") DEF(1.0f))
}UniformGroupGen;

AUTO_STRUCT;
typedef struct SpeciesGenData
{
	const char *pcName;						AST( STRUCTPARAM KEY POOL_STRING NAME("Name") )
	const char *pcScope;					AST( POOL_STRING )
	const char *pcFileName;					AST( CURRENTFILE )

	int iNumToGenerate;

	REF_TO(PCSkeletonDef) hMaleSkeleton;	AST( NAME("MaleSkeleton") REFDICT(CostumeSkeleton) )
	REF_TO(PCSkeletonDef) hFemaleSkeleton;	AST( NAME("FemaleSkeleton") REFDICT(CostumeSkeleton) )

	REF_TO(SpeciesDefiningFeature) hDefaultMale;	AST( NAME("DefaultMaleFeatures") REFDICT(SpeciesFeature) )
	REF_TO(SpeciesDefiningFeature) hDefaultFemale;	AST( NAME("DefaultFemaleFeatures") REFDICT(SpeciesFeature) )
	SpeciesDefiningFeatureRef **eaFeaturesToUse;	AST( NAME("FeaturesToUse") )

	UniformGroupGen **eaUniformGroups;		AST(NAME(UniformGroup))
	CritterGroupGen	**eaCritterGroupGen;	AST(NAME(GroupedCritterDef))
	VoiceRef **eaAllowedMaleVoices;				AST(NAME("AllowedMaleVoice"))
	VoiceRef **eaAllowedFemaleVoices;			AST(NAME("AllowedFemaleVoice"))
}SpeciesGenData;

#define CUSTOM_SPECIES_VERSION 0

AUTO_STRUCT AST_CONTAINER;
typedef struct CustomSpecies
{
	const int									 iVersion;			AST(PERSIST SUBSCRIBE)
	const int									 iChecksum;			AST(PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(SpeciesDef)			 pMaleSpecies;		AST(PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(SpeciesDef)			 pFemaleSpecies;	AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(NameTemplatePhonemeSetNames) eaMaleNameGen;		AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(NameTemplatePhonemeSetNames) eaFemaleNameGen;	AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(PhonemeSet)					 eaPhonemeSets;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_EARRAY							 pcRequiredTraits;	AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_STRING_EARRAY							 pcOptionalTraits;	AST(PERSIST SUBSCRIBE POOL_STRING)
}CustomSpecies;

extern ParseTable parse_SpeciesDef[];
#define TYPE_parse_SpeciesDef SpeciesDef
extern DictionaryHandle g_hSpeciesDict;

extern ParseTable parse_SpeciesDefiningFeature[];
#define TYPE_parse_SpeciesDefiningFeature SpeciesDefiningFeature
extern DictionaryHandle g_hSpeciesDefiningDict;

extern ParseTable parse_SpeciesGenData[];
#define TYPE_parse_SpeciesGenData SpeciesGenData
extern DictionaryHandle g_hSpeciesGenDataDict;

extern ParseTable parse_CostumePresetCategory[];
#define TYPE_parse_CostumePresetCategory CostumePresetCategory
extern DictionaryHandle g_hCostumePresetCatDict;

void species_GetAvailableSpecies(GameAccountData *pGameAccountData, SpeciesDef ***peaSpecies);
void species_EntGetAvailableSpecies(Entity *pEnt, SpeciesDef ***peaSpecies);
void species_GetSpeciesList(Entity *pEnt, const char *pcSpeciesName, PCSkeletonDef *pSkeleton, SpeciesDef ***peaSpecies);
