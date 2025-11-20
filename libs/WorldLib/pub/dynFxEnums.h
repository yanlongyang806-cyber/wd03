
#pragma once
GCC_SYSTEM

#include "dynBitField.h"

AUTO_ENUM;
typedef enum eDynPriority
{
	edpOverride, ENAMES(Override)
	edpCritical, ENAMES(Critical)
	edpDefault, ENAMES(Default)
	edpDetail, ENAMES(Detail)
	edpNotSet, ENAMES(NotSet)
} eDynPriority;

AUTO_ENUM;
typedef enum eDynFlareType
{
	eDynFlareType_None, ENAMES(None)
	eDynFlareType_Sun,  ENAMES(Sun)
} eDynFlareType;

AUTO_ENUM;
typedef enum eDynSplatType
{
	eDynSplatType_None, ENAMES(None)
	eDynSplatType_Cylinder, ENAMES(Cylinder)
	eDynSplatType_Cone, ENAMES(Cone)
} eDynSplatType;

AUTO_ENUM;
typedef enum eDynRaycastFilter
{
	eDynRaycastFilter_World	= 1<<0, ENAMES(World)
	eDynRaycastFilter_Hull	= 1<<1, ENAMES(Hull)
	eDynRaycastFilter_Shield = 1<<2, ENAMES(Shield)
	eDynRaycastFilter_WaterVolume = 1<<3, ENAMES(Water)
} eDynRaycastFilter;

// For debugging
AUTO_ENUM;
typedef enum eDynFxType
{
	eDynFxType_Sprite, ENAMES(Sprite)
	eDynFxType_FastParticleSet, ENAMES(FastParticleSet)
	eDynFxType_MeshTrail, ENAMES(MeshTrail)
	eDynFxType_Geometry, ENAMES(Geometry)
	eDynFxType_SkinnedGeometry, ENAMES(SkinnedGeometry)
} eDynFxType;

AUTO_ENUM;
typedef enum eDynFxSource
{
	eDynFxSource_Power, ENAMES(Power)
	eDynFxSource_Costume, ENAMES(Costume)
	eDynFxSource_Environment, ENAMES(Environment)
	eDynFxSource_Volume, ENAMES(Volume)
	eDynFxSource_Animation, ENAMES(Animation)
	eDynFxSource_Test, ENAMES(Test)
	eDynFxSource_UI, ENAMES(UI)
	eDynFxSource_Expression, ENAMES(Expression)
	eDynFxSource_HardCoded, ENAMES(HardCoded)
	eDynFxSource_Damage, ENAMES(Damage)
	eDynFxSource_Cutscene, ENAMES(Cutscene)
} eDynFxSource;

AUTO_ENUM;
typedef enum DynParticleEmitFlag
{
	DynParticleEmitFlag_Ignore, ENAMES(Ignore)
	DynParticleEmitFlag_Inherit, ENAMES(Inherit)
	DynParticleEmitFlag_Update, ENAMES(Update)
} DynParticleEmitFlag;

typedef enum eDynKeyFrameType
{
	eDynKeyFrameType_Create,
	eDynKeyFrameType_Destroy,
	eDynKeyFrameType_Recreate,
	eDynKeyFrameType_Update,
} eDynKeyFrameType;

typedef enum eDynParamType
{
	edptNone,
	edptVector2,
	edptVector,
	edptVector4,
	edptString,
	edptNumber,
	edptInteger,
	edptBool,
	edptQuat,
	edptStringArray,
} eDynParamType;

AUTO_ENUM;
typedef enum eDynParamConditionalType
{
	edpctGreaterThan,ENAMES(GreaterThan)
	edpctLessThan,ENAMES(LessThan)
	edpctEquals,ENAMES(Equals)
} eDynParamConditionalType;

typedef enum eDynParamOperator
{
	edpoNone,
	edpoCopy,
	edpoAdd,
	edpoMultiply,
} eDynParamOperator;

typedef enum eDynInterpType
{
	ediLinear,
	ediStep,
	ediEaseIn,
	ediEaseOut,
	ediEaseInAndOut,
} eDynInterpType;


typedef enum eDynEntityLightMode
{
	edelmNone,
	edelmAdd,
	edelmMultiply,
} eDynEntityLightMode;

typedef enum eDynEntityMaterialMode
{
	edemmNone,
	edemmSwap,
	edemmAdd,
	edemmSwapWithConstants,
	edemmAddWithConstants,
	edemmDissolve,
	edemmTextureSwap,
} eDynEntityMaterialMode;

typedef enum eDynEntityTintMode
{
	edetmNone,
	edetmMultiply,
	edetmAdd,
	edetmAlpha,
	edetmSet,
} eDynEntityTintMode;

typedef enum eDynParentFlags
{
	edpfNone = 0,
	edpfScaleToOnce = ( 1 << 2 ),
	edpfOrientToOnce = ( 1 << 3 ),
	edpfGoToOnce = ( 1 << 4 ),
	edpfLocalPosition = ( 1 << 5 ),
	edpfAttachAfterOrient = ( 1 << 6 ),
	edpfOrientToLockToPlane = ( 1 << 7 ),
} eDynParentFlags;

typedef enum eDebugSortMode
{
	eDebugSortMode_Num,
	eDebugSortMode_NumReverse,
	eDebugSortMode_Name,
	eDebugSortMode_NameReverse,
	eDebugSortMode_Peak,
	eDebugSortMode_PeakReverse,
	eDebugSortMode_Mem,
	eDebugSortMode_MemReverse,
	eDebugSortMode_Level,
	eDebugSortMode_LevelReverse,
	eDebugSortMode_PhysicsObjects,
	eDebugSortMode_PhysicsObjectsReverse,
	eDebugSortMode_None,
} eDebugSortMode;

typedef enum eFxManagerType
{
	eFxManagerType_None,
	eFxManagerType_Entity,
	eFxManagerType_Player,
	eFxManagerType_World,
	eFxManagerType_Global,
	eFxManagerType_UI,
	eFxManagerType_Debris,
	eFxManagerType_Headshot,
	eFxManagerType_Tailor,
} eFxManagerType;

typedef enum eDynEntityScaleMode
{
	edesmNone,
	edesmMultiply,
} eDynEntityScaleMode;

typedef enum eDynFxDictType
{
	eDynFxDictType_Material,
	eDynFxDictType_Geometry,
	eDynFxDictType_Texture,
	eDynFxDictType_ClothInfo,
	eDynFxDictType_ClothCollisionInfo,
	eDynFxDictType_None,
} eDynFxDictType;
