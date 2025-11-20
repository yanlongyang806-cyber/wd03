#include "dynFxParticle.h"

#include "tokenstore.h"
#include "rand.h"
#include "Memorypool.h"
#include "StringCache.h"

#include "wlLight.h"
#include "wlCostume.h"

#include "dynFxDebug.h"
#include "dynFxInfo.h"
#include "dynFx.h"
#include "dynFxPhysics.h"
#include "dynDraw.h"
#include "dynNodeInline.h"
#include "dynFxFastParticle.h"

#include "WorldLibEnums_h_ast.h"
#include "dynFxEnums_h_ast.h"
#include "dynFxInfo_h_ast.h"
#include "dynFxManager_h_ast.h"
#include "DynFxInterface.h"

#include "dynCostume.h"

#include "WorldLib_autogen_QueuedFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

#if 0
// JE: Added these macros to track down something going to non-finite
#define CHECK_FINITE(f) assert(FINITE(f))
#define CHECK_FINITEVEC3(f) assert(FINITEVEC3(f))
#else
#define CHECK_FINITE(f)
#define CHECK_FINITEVEC3(f)
#endif

static void dynEventUpdaterReset(DynEventUpdater* pUpdater, DynFx* pFx);
static void dynDrawParticleInit( SA_PARAM_NN_VALID DynDrawParticle* pDraw, SA_PARAM_NN_VALID const DynEvent* pEvent);

#pragma pop_macro("vPos_DNODE")
#pragma pop_macro("qRot_DNODE")

ParseTable ParseDynDrawParticle[] =
{
	{ "Start",		TOK_START,		0										},
	{ "Position",	TOK_VEC3(DynDrawParticle, node.vPos_DNODE) },
	{ "DrawOffset",	 TOK_VEC3(DynDrawParticle, vDrawOffset) },
	{ "Velocity",	TOK_VEC3(DynDrawParticle, vVelocity) },
	{ "Scale",		TOK_VEC3(DynDrawParticle, vScale) },
	{ "Orientation",TOK_QUATPYR(DynDrawParticle, node.qRot_DNODE) }, 
	{ "Spin",		TOK_VEC3(DynDrawParticle, vSpin) },
	{ "SpriteOrientation",TOK_F32(DynDrawParticle, fSpriteOrientation, 0) }, 
	{ "SpriteSpin",		TOK_F32(DynDrawParticle, fSpriteSpin, 0) },
	{ "GoToSpeed",		TOK_F32(DynDrawParticle,fGoToSpeed, 0.0f) }, 
	{ "GoToGravity",		TOK_F32(DynDrawParticle,fGoToGravity, 0.0f) }, 
	{ "GoToGravityFalloff",	TOK_F32(DynDrawParticle,fGoToGravityFalloff, 0.0f) }, 
	{ "GoToApproachSpeed",	TOK_F32(DynDrawParticle,fGoToApproachSpeed, 0.0f) }, 
	{ "GoToSpringEquilibrium",	TOK_F32(DynDrawParticle,fGoToSpringEquilibrium, 0.0f) }, 
	{ "GoToSpringConstant",		TOK_F32(DynDrawParticle,fGoToSpringConstant, 0.0f) }, 
	{ "ParentVelocityOffset",		TOK_F32(DynDrawParticle,fParentVelocityOffset, 0.0f) }, 
	{ "Color",		TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynDrawParticle, vColor), 3 },
	{ "HSVShift",	TOK_VEC3(DynDrawParticle, vHSVShift) },
	{ "Alpha",		TOK_F32(DynDrawParticle, vColor[3], 0), NULL },
	{ "Drag",		TOK_F32(DynDrawParticle, fDrag, 0), NULL },
	{ "TightenUp",		TOK_F32(DynDrawParticle, fTightenUp, 0), NULL },
	{ "Gravity",		TOK_F32(DynDrawParticle, fGravity, 0), NULL },
	{ "BlendMode",	TOK_AUTOINT(DynDrawParticle, iBlendMode, 0), NULL },
	{ "EntLightMode",TOK_AUTOINT(DynDrawParticle, iEntLightMode, 0), NULL },
	{ "EntTintMode",TOK_AUTOINT(DynDrawParticle, iEntTintMode, 0), NULL },
	{ "EntScaleMode",TOK_AUTOINT(DynDrawParticle, iEntScaleMode, 0), NULL },
	{ "StreakMode",	TOK_AUTOINT(DynDrawParticle, iStreakMode, 0), NULL },
	{ "StreakScale",	TOK_F32(DynDrawParticle, fStreakScale, 1), NULL },
	{ "TexOffset",		TOK_VEC2(DynDrawParticle, vTexOffset) },
	{ "TexScale",		TOK_VEC2(DynDrawParticle, vTexScale) },
	{ "Texture",	TOK_STRING(DynDrawParticle, pcTextureName, 0), NULL },
	{ "Texture2",	TOK_STRING(DynDrawParticle, pcTextureName2, 0), NULL },
	{ "Material",	TOK_STRING(DynDrawParticle, pcMaterialName, 0), NULL },
	{ "GeoDissolveMaterial",	TOK_STRING(DynDrawParticle, pcGeoDissolveMaterialName, 0), NULL },
	{ "GeoAddMaterials",		TOK_STRINGARRAY(DynDrawParticle, ppcGeoAddMaterialNames), NULL },
	{ "Material2", TOK_STRING(DynDrawParticle, pcMaterial2Name, 0), NULL },
	{ "Geometry",	TOK_STRING(DynDrawParticle, pcModelName, 0), NULL },
	{ "Cloth",		TOK_STRING(DynDrawParticle, pcClothName, 0), NULL },
	{ "ClothInfo",	TOK_STRING(DynDrawParticle, pcClothInfo, 0), NULL },
	{ "ClothCollisionInfo", TOK_STRING(DynDrawParticle, pcClothCollisionInfo, 0), NULL },
	{ "ClothCollide", TOK_BOOL(DynDrawParticle, bClothCollide, 0), NULL },
	{ "ClothCollideSelfOnly", TOK_BOOL(DynDrawParticle, bClothCollideSelfOnly, 0), NULL },
	{ "ClothWindOverride", TOK_VEC3(DynDrawParticle, vClothWindOverride) },
	{ "LightModulationAmount", TOK_F32(DynDrawParticle, fLightModulation, 0) },
	{ "BoneName",	TOK_STRING(DynDrawParticle, node.pcTag, 0), NULL },
	{ "Skeleton",	TOK_STRING(DynDrawParticle, pcBaseSkeleton, 0), NULL },
	{ "HInvert",	TOK_BOOL(DynDrawParticle, bHInvert, 0), NULL },
	{ "VInvert",	TOK_BOOL(DynDrawParticle, bVInvert, 0), NULL },
	{ "FixedAspectRatio", TOK_BOOL(DynDrawParticle, bFixedAspectRatio, 0), NULL },
	{ "StreakTile",	TOK_BOOL(DynDrawParticle, bStreakTile, 0), NULL },
	{ "CastShadows",	TOK_BOOL(DynDrawParticle, bCastShadows, 0), NULL },
	{ "EntMaterial",TOK_AUTOINT(DynDrawParticle, iEntMaterial, 0), NULL },
	{ "Oriented",	TOK_AUTOINT(DynDrawParticle, eOriented, 0), NULL },
	{ "LocalVelocity",TOK_BOOL(DynDrawParticle, bLocalOrientation, 0), NULL },
	{ "VelocityDriveOrientation",TOK_BOOL(DynDrawParticle, bVelocityDriveOrientation, 0), NULL },
	{ "Color1",	TOK_VEC4(DynDrawParticle, vColor1) },
	{ "Color2",	TOK_VEC4(DynDrawParticle, vColor2) },
	{ "Color3",	TOK_VEC4(DynDrawParticle, vColor3) },
	{ "TexWords",	TOK_STRING(DynDrawParticle, pcTexWords, 0), NULL },
	{ "GetModelFromCostumeBone", TOK_STRING(DynDrawParticle,pcBoneForCostumeModelGrab, 0), NULL },
	{ "UseClothWindOverride", TOK_BOOL(DynDrawParticle, bUseClothWindOverride, 0), NULL },
	{ "Instanceable", TOK_BOOL(DynDrawParticle, bExplicitlyInstanceable, 0), NULL },
	{ "End",		TOK_END,		0										},
	{ "", 0, 0 }
};

#pragma push_macro("vPos_DNODE")
#define vPos_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS
#pragma push_macro("qRot_DNODE")
#define qRot_DNODE PLEASE_USE_ACCESSORS_OR_MUTATORS


ParseTable ParseDynFlare[] =
{
	{ "FlareType",		TOK_INT(DynFlare,eType,eDynFlareType_None), eDynFlareTypeEnum },
	{ "FlareSize",		TOK_F32ARRAY(DynFlare,size) },
	{ "FlareColor",		TOK_VEC3(DynFlare,hsv_color) },
	{ "FlarePosition",	TOK_F32ARRAY(DynFlare,position) },
	{ "FlareMaterial",	TOK_POOL_STRING|TOK_STRINGARRAY(DynFlare,ppcMaterials), NULL },
	{ "End",			TOK_END,		0										},
	{ "", 0, 0 }
};

ParseTable ParseDynLight[] =
{
	{ "Start",				TOK_START,		0										},
	{ "LightType",			TOK_INT(DynLight,eLightType, WL_LIGHT_NONE), ParseDynLightType },
	{ "LightShadows",		TOK_BOOL(DynLight,bCastShadows, false) },
	{ "LightDiffuseHSV",	TOK_VEC3(DynLight,vDiffuseHSV) },
	{ "LightSpecularHSV",	TOK_VEC3(DynLight,vSpecularHSV) },
	{ "LightRadius",		TOK_F32(DynLight,fRadius, 0.0f) },
	{ "LightInnerRadiusPercentage",	TOK_F32(DynLight,fInnerRadiusPercentage, 0.1f) },
	{ "LightInnerCone",		TOK_F32(DynLight,fInnerConeAngle, 0.0f) },
	{ "LightOuterCone",		TOK_F32(DynLight,fOuterConeAngle, 0.0f) },
	{ "End",				TOK_END,		0										},
	{ "", 0, 0 }
};


ParseTable ParseDynCameraInfo[] =
{
	{ "Start",				TOK_START,		0										},
	{ "ShakePower",			TOK_F32(DynCameraInfo,fShakePower, 0.0f) },
	{ "ShakeRadius",		TOK_F32(DynCameraInfo,fShakeRadius, 0.0f) },
	{ "ShakeVertical",		TOK_F32(DynCameraInfo,fShakeVertical, 0.0f) },
	{ "ShakeSpeed",			TOK_F32(DynCameraInfo,fShakeSpeed, 1.0f) },
	{ "ShakePan",			TOK_F32(DynCameraInfo,fShakePan, 0.0f) },
	{ "AttachCamera",		TOK_BOOL(DynCameraInfo,bAttachCamera, false) },
	{ "CameraControlInfluence", TOK_F32(DynCameraInfo,fCameraInfluence, 1.0f) },
	{ "FOV",				TOK_F32(DynCameraInfo,fCameraFOV, 0.0f) },
	{ "CameraDelaySpeed",	TOK_F32(DynCameraInfo,fCameraDelaySpeed, 0.0f) },
	{ "CameraDelayDistanceBasis",	TOK_F32(DynCameraInfo,fCameraDelayDistanceBasis, 0.0f) },
	{ "CameraLookAt",	TOK_POOL_STRING|TOK_STRING(DynCameraInfo,pcCameraLookAtNode,0) },
	{ "CameraLookAtSpeed",	TOK_F32(DynCameraInfo,fCameraLookAtSpeed, 0.0f) },
	{ "End",				TOK_END,		0										},
	{ "", 0, 0 }
};

ParseTable ParseDynSplat[] =
{
	{ "Start",				TOK_START,		0										},
	{ "SplatType",			TOK_INT(DynSplat,eType,eDynSplatType_None), eDynSplatTypeEnum },
	{ "SplatRadius",		TOK_F32(DynSplat,fSplatRadius, 0.0f) },
	{ "SplatInnerRadius",	TOK_F32(DynSplat,fSplatInnerRadius, 0.0f) },
	{ "SplatLength",		TOK_F32(DynSplat,fSplatLength, 0.0f) },
	{ "SplatForceDown",		TOK_BOOLFLAG(DynSplat,bForceDown, false) },
	{ "SplatUpdateScale",	TOK_BOOLFLAG(DynSplat,bUpdateScale, false) },
	{ "SplatCenter",		TOK_BOOLFLAG(DynSplat,bCenterLength,false) },
	{ "SplatFadePlane",		TOK_F32(DynSplat,fSplatFadePlanePt,0.0f) },
	{ "SplatBoneNameForProjection", TOK_POOL_STRING|TOK_STRING(DynSplat,pcSplatProjectionBone, 0) },
	{ "SplatDisableCulling",TOK_BOOLFLAG(DynSplat,bDisableCulling, false) },
	{ "End",				TOK_END,		0										},
	{ "", 0, 0 }
};


ParseTable ParseDynSkyVolume[] =
{
	{ "Start",				TOK_START,		0										},
	{ "SkyName",			TOK_POOL_STRING | TOK_STRINGARRAY(DynSkyVolume, ppcSkyName), NULL },
	{ "SkyRadius",			TOK_F32(DynSkyVolume,fSkyRadius, 0.0f) },
	{ "SkyLength",			TOK_F32(DynSkyVolume,fSkyLength, 0.0f) },
	{ "SkyWeight",			TOK_F32(DynSkyVolume,fSkyWeight, 0.0f) },
	{ "SkyFalloff",			TOK_INT(DynSkyVolume,eSkyFalloff, eSkyFalloffType_Linear), eSkyFalloffTypeEnum },
	{ "End",				TOK_END,		0										},
	{ "", 0, 0 }
};

ParseTable ParseDynMeshTrailInfo[] =
{
	{ "Start",			TOK_START,		0										},
	{ "TrailTexDensity",TOK_F32(DynMeshTrailInfo,fTexDensity,0.0f) },
	{ "TrailEmitRate",	TOK_F32(DynMeshTrailInfo,fEmitRate,0.0f) },
	{ "TrailEmitDistance",TOK_F32(DynMeshTrailInfo,fEmitDistance,0.0f) },
	{ "TrailMinSpeed",	TOK_F32(DynMeshTrailInfo,fMinForwardSpeed,0.0f) },
	{ "TrailKey1Time",	TOK_F32(DynMeshTrailInfo,keyFrames[0].fTime,0.0f) },
	{ "TrailKey2Time",	TOK_F32(DynMeshTrailInfo,keyFrames[1].fTime,0.0f) },
	{ "TrailKey3Time",	TOK_F32(DynMeshTrailInfo,keyFrames[2].fTime,0.0f) },
	{ "TrailKey4Time",	TOK_F32(DynMeshTrailInfo,keyFrames[3].fTime,0.0f) },
	{ "TrailKey1Width",	TOK_F32(DynMeshTrailInfo,keyFrames[0].fWidth,0.0f) },
	{ "TrailKey2Width",	TOK_F32(DynMeshTrailInfo,keyFrames[1].fWidth,0.0f) },
	{ "TrailKey3Width",	TOK_F32(DynMeshTrailInfo,keyFrames[2].fWidth,0.0f) },
	{ "TrailKey4Width",	TOK_F32(DynMeshTrailInfo,keyFrames[3].fWidth,0.0f) },
	{ "TrailKey1Alpha",	TOK_F32(DynMeshTrailInfo,keyFrames[0].vColor[3],0.0f) },
	{ "TrailKey2Alpha",	TOK_F32(DynMeshTrailInfo,keyFrames[1].vColor[3],0.0f) },
	{ "TrailKey3Alpha",	TOK_F32(DynMeshTrailInfo,keyFrames[2].vColor[3],0.0f) },
	{ "TrailKey4Alpha",	TOK_F32(DynMeshTrailInfo,keyFrames[3].vColor[3],0.0f) },
	{ "TrailKey1Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynMeshTrailInfo, keyFrames[0].vColor), 3 },
	{ "TrailKey2Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynMeshTrailInfo, keyFrames[1].vColor), 3 },
	{ "TrailKey3Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynMeshTrailInfo, keyFrames[2].vColor), 3 },
	{ "TrailKey4Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynMeshTrailInfo, keyFrames[3].vColor), 3 },
	{ "TrailFadeIn",	TOK_F32(DynMeshTrailInfo,fFadeInTime,0.0f) },
	{ "TrailCurveDir",	TOK_VEC3(DynMeshTrailInfo,vCurveDir) },
	{ "TrailFadeOut",	TOK_F32(DynMeshTrailInfo,fFadeOutTime,0.0f) },
	{ "TrailSpeedThreshold",	TOK_F32(DynMeshTrailInfo,fEmitSpeedThreshold,0.0f) },
	{ "TrailShiftSpeed",	TOK_F32(DynMeshTrailInfo,fShiftSpeed,0.0f) },
	{ "TrailCurve",	TOK_BOOLFLAG(DynMeshTrailInfo,bSubFrameCurve,false) },
	{ "End",			TOK_END,		0										},
	{ "", 0, 0 }
};

ParseTable ParseDynControlInfo[] =
{
	{ "Start",			TOK_START,		0 },
	{ "TimeScale",		TOK_F32(DynFxControlInfo,fTimeScale,1.0f) },
	{ "TimeScaleChildren",TOK_BOOLFLAG(DynFxControlInfo,bTimeScaleChildren,false) },
	{ "End",			TOK_END,		0 },
	{ "",				0,				0 }
};

typedef struct DynLoopUpdater
{
	DynLoop* pLoopInfo;
	F32 fTimeAccum;
	F32 fDistAccum;
	F32 fLODNearSquared;
	F32 fLODFarSquared;
	REF_TO(DynFx) hLastChildCreated;
} DynLoopUpdater;

typedef struct DynRaycastUpdater
{
	DynRaycast* pRaycastInfo;
	F32 fTimeAccum;
	DynNode* pRaycastNode;
	bool bFired;
	bool bHitOnce;
	DynLoopUpdater** eaLoopUpdater;
	DynFxFastParticleSet** eaParticleSets;
} DynRaycastUpdater;


typedef struct DynEventUpdater
{
	const DynEvent* pEvent;
	DynFxTime uiTimeSinceStart;
	bool bNeverUpdated;
	int iKeyBeforeIndex;
	DynFxPathSet* pPathSet;
	DynLoopUpdater** eaLoopUpdater;
	DynRaycastUpdater** eaRaycastUpdater;
	const DynForce** eaForces;
	DynSoundUpdater soundUpdater;
} DynEventUpdater;

MP_DEFINE(DynLoopUpdater);
MP_DEFINE(DynRaycastUpdater);

static const char *s_pcOther;
static const char *s_pcNotSet;
AUTO_RUN;
void dynFxParticle_InitStrings(void)
{
	s_pcOther  = allocAddStaticString("Other");
	s_pcNotSet = allocAddStaticString("NotSet");
}

static U32* puiDynObjectToDynDrawParticleTokenMap = NULL;
static U32* puiDynObjectToDynFlareTokenMap = NULL;
static U32* puiDynObjectToDynLightTokenMap = NULL;
static U32* puiDynObjectToDynCameraInfoTokenMap = NULL;
static U32* puiDynObjectToDynSplatTokenMap = NULL;
static U32* puiDynObjectToDynSkyVolumeTokenMap = NULL;
static U32* puiDynObjectToDynFxControlInfoTokenMap = NULL;
static U32* puiDynObjectToDynMeshTrailInfoTokenMap = NULL;

static dynFxSoundGetGuidFunc sndGuidFunc = NULL;		// Gets a new guid
static dynFxSoundCreateFunc sndStartFunc = NULL;		// Used to start an event by guid
static dynFxSoundDestroyFunc sndStopFunc = NULL;		// Stops an event by guid
static dynFxSoundCleanFunc sndCleanFunc = NULL;			// Cleans an event by guid (no more position updates)
static dynFxSoundMoveFunc sndMoveFunc = NULL;			// Moves an event by guid

static dynFxDSPCreateFunc sndStartDSPFunc = NULL;
static dynFxDSPDestroyFunc sndStopDSPFunc = NULL;
static dynFxDSPCleanFunc sndCleanDSPFunc = NULL;

void initDynObjectToDynDrawParticleTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynDrawParticleTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynDrawParticle[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynDrawParticle[uiDrawToken].name)==0)
			{
				puiDynObjectToDynDrawParticleTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

void initDynObjectToDynFlareTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynFlareTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynFlare[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynFlare[uiDrawToken].name)==0)
			{
				puiDynObjectToDynFlareTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

void initDynObjectToDynLightTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynLightTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynLight[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynLight[uiDrawToken].name)==0)
			{
				puiDynObjectToDynLightTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}


void initDynObjectToDynCameraInfoTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynCameraInfoTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynCameraInfo[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynCameraInfo[uiDrawToken].name)==0)
			{
				puiDynObjectToDynCameraInfoTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

void initDynObjectToDynFxControlInfoTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynFxControlInfoTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynControlInfo[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynControlInfo[uiDrawToken].name)==0)
			{
				puiDynObjectToDynFxControlInfoTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

void initDynObjectToDynSplatTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynSplatTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynSplat[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynSplat[uiDrawToken].name)==0)
			{
				puiDynObjectToDynSplatTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

void initDynObjectToDynSkyVolumeTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynSkyVolumeTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynSkyVolume[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynSkyVolume[uiDrawToken].name)==0)
			{
				puiDynObjectToDynSkyVolumeTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

void initDynObjectToDynMeshTrailInfoTokenMap(void)
{
	U32 uiTokenIndex = 0;
	puiDynObjectToDynMeshTrailInfoTokenMap = calloc(sizeof(U32), uiDynObjectTokenTerminator);
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiDrawToken = 0;
		while (ParseDynMeshTrailInfo[uiDrawToken].type != TOK_END)
		{
			if ( stricmp(ParseDynObject[uiTokenIndex].name, ParseDynMeshTrailInfo[uiDrawToken].name)==0)
			{
				puiDynObjectToDynMeshTrailInfoTokenMap[uiTokenIndex] = uiDrawToken;
			}
			++uiDrawToken;
		}
		++uiTokenIndex;
	}
}

static void dynFxCallLoopEnds(SA_PARAM_NN_VALID DynFx* pFx, SA_PARAM_NN_VALID DynLoopRef*** peaLoops);
static void dynFxCallLoopStarts(SA_PARAM_NN_VALID DynEventUpdater* pUpdater, SA_PARAM_NN_VALID DynLoopRef*** peaLoops, SA_PARAM_NN_VALID DynFx* pFx);
static void dynFxCallForceEnds(SA_PARAM_NN_VALID DynEventUpdater* pUpdater, SA_PARAM_NN_VALID DynForceRef*** peaForces);
static void dynFxCallForceStarts(SA_PARAM_NN_VALID DynEventUpdater* pUpdater, SA_PARAM_NN_VALID DynForceRef*** peaForces);
static void dynFxClearForceOnces(SA_PARAM_NN_VALID DynEventUpdater* pUpdater);
static void dynFxCallRaycastStarts(SA_PARAM_NN_VALID DynEventUpdater* pUpdater, SA_PARAM_NN_VALID DynRaycastRef*** peaRaycastStarts, SA_PARAM_NN_VALID DynFx* pFx);
static void dynFxCallRaycastStops(SA_PARAM_NN_VALID DynEventUpdater* pUpdater, SA_PARAM_NN_VALID DynRaycastRef*** peaRaycastStops, SA_PARAM_NN_VALID DynFx* pFx);
static void dynRaycastUpdate(int iPartitionIdx, SA_PARAM_NN_VALID DynRaycastUpdater* pUpdater, DynFx* pFx, DynEventUpdater* pEventUpdater);
static void dynFxCallSoundStartsAndEnds(SA_PARAM_NN_VALID DynEventUpdater* pUpdater, SA_PARAM_NN_VALID DynFx *pFx, SA_PARAM_NN_VALID DynKeyFrame* pKeyFrame);
static void dynFxUpdatePathSetCurrentKeyFrames( SA_PARAM_NN_VALID DynFxPathSet* pPathSet, SA_PARAM_NN_VALID DynParticle* pParticle, U32 uiBeforeKeyFrame, SA_PARAM_NN_VALID DynFx* pFx);
static void dynParticleUpdate( SA_PARAM_NN_VALID DynParticle* pParticle, SA_PARAM_NN_VALID DynFx* pFx, SA_PARAM_NN_VALID DynFxPathSet* pPathSet, DynFxTime uiDeltaTime, bool bPassedKeyFrame);
static void dynFxSetUpdateMask( SA_PARAM_NN_VALID DynFxDynamicPath* pPath ); 


void dynFxCreatePathSetFromEvent(DynFxPathSet* pPathSet, const DynEvent* pEvent, const DynParamBlock* pParamBlock, const DynFx* pParentFx)
{
	U32 uiPathIndex = 0;
	const DynParticle* pParentParticle = dynFxGetParticleConst(pParentFx);

	memcpy(pPathSet, pEvent->pPathSet, pEvent->pPathSet->uiTotalSize);
	// Fix up pointers
	pPathSet->pStaticPaths = (DynFxStaticPath*)((size_t)pPathSet + (size_t)pPathSet->pStaticPaths);
	pPathSet->pDynamicPaths = (DynFxDynamicPath*)((size_t)pPathSet + (size_t)pPathSet->pDynamicPaths);

	for (uiPathIndex=0; uiPathIndex<pPathSet->uiNumStaticPaths; ++uiPathIndex)
	{
		DynFxStaticPath* pPath = &pPathSet->pStaticPaths[uiPathIndex];
		U32 uiPointIndex;
		// Fix up pointers
		pPath->pPathPoints = (DynFxStaticPathPoint*)((size_t)pPath->pPathPoints + (size_t)pPathSet);
		for (uiPointIndex=0;uiPointIndex<pPath->uiNumPathPoints;++uiPointIndex)
		{
			DynFxStaticPathPoint* pPoint = &pPath->pPathPoints[uiPointIndex];
			if ( ParseDynObject[pPath->uiTokenIndex].type & TOK_EARRAY )
			{
				void* pDataPtr;
				if (ParseDynObject[pPath->uiTokenIndex].type & TOK_F32_X)
					pDataPtr = TokenStoreGetEArrayF32(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue], NULL);
				else
					pDataPtr = TokenStoreGetEArray(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue], NULL);
				memcpy(&pPoint->data, pDataPtr, pPath->uiDataSize);
			}
			else
			{
				void* pDataPtr = TokenStoreGetPointer(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue], 0, NULL);

				if ( ( ParseDynObject[pPath->uiTokenIndex].type & TOK_TYPE_MASK ) == TOK_STRING_X  )
				{
					memcpy(&pPoint->data, &pDataPtr, pPath->uiDataSize);
				}
				else
					memcpy(&pPoint->data, pDataPtr, pPath->uiDataSize);
			}

			if ( pParamBlock )
			{
				DynApplyParam** eaParams = pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue].eaParams;
				const U32 uiNumParams = eaSize(&eaParams);
				if ( uiNumParams )
					dynFxApplyCopyParamsGeneral(uiNumParams, eaParams, pPath->uiTokenIndex, pParamBlock, &pPoint->data, ParseDynObject);
			}
			{
				DynJitterList** eaJLists = pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue].eaJLists;
				const U32 uiNumJLists = eaSize(&eaJLists);
				if ( uiNumJLists )
					dynFxApplyJitterLists(uiNumJLists, eaJLists, pPath->uiTokenIndex, &pPoint->data, ParseDynObject, NULL, NULL);
			}
			dynFxApplyStaticDynOps(&pPoint->data, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue], pPath->uiTokenIndex, pParentParticle);
		}
	}

	for (uiPathIndex=0; uiPathIndex<pPathSet->uiNumDynamicPaths; ++uiPathIndex)
	{
		DynFxDynamicPath* pPath = &pPathSet->pDynamicPaths[uiPathIndex];
		// Fix up pointers
		pPath->pPathPoints = (DynFxDynamicPathPoint*)((size_t)pPath->pPathPoints + (size_t)pPathSet);
		if ( ( ParseDynObject[pPath->uiTokenIndex].type & TOK_TYPE_MASK ) != TOK_QUATPYR_X )
		{
			U32 uiPointIndex;
			for (uiPointIndex=0; uiPointIndex < pPath->uiNumPathPoints; ++uiPointIndex)
			{
				DynFxDynamicPathPoint* pPoint = &pPath->pPathPoints[uiPointIndex];
				DynFxDynamicPathPoint* pNextPoint = (uiPointIndex+pPath->uiFloatsPer < pPath->uiNumPathPoints)?&pPath->pPathPoints[uiPointIndex+pPath->uiFloatsPer]:NULL;

				if ( uiPointIndex < pPath->uiFloatsPer )
				{
					pPoint->fStartV = TokenStoreGetF32(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pPoint->uiWhichFloat, NULL);
					CHECK_FINITE(pPoint->fStartV);
					if ( pParamBlock )
					{
						DynApplyParam** eaParams = pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO].eaParams;
						const U32 uiNumParams = eaSize(&eaParams);
						if ( uiNumParams )
							dynFxApplyF32Params(uiNumParams, eaParams, pPath->uiTokenIndex, pParamBlock, &pPoint->fStartV, pPoint->uiWhichFloat);
					}
					{
						DynJitterList** eaJLists = pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue].eaJLists;
						const U32 uiNumJLists = eaSize(&eaJLists);
						if ( uiNumJLists )
							dynFxApplyF32JitterList(uiNumJLists, eaJLists, pPath->uiTokenIndex, &pPoint->fStartV, ParseDynObject, pPoint->uiWhichFloat);
					}
					dynFxApplyF32DynOps(&pPoint->fStartV, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pPath->uiTokenIndex, pPoint->uiWhichFloat, pParentParticle);
					CHECK_FINITE(pPoint->fStartV);
				}


				if ( pNextPoint )
				{
					if ( dynObjectInfoSpecifiesToken(&pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pPath->uiTokenIndex ) )
						pNextPoint->fStartV = TokenStoreGetF32(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pNextPoint->uiWhichFloat, NULL);
					else
						pNextPoint->fStartV = pPoint->fStartV;
					if ( pParamBlock )
					{
						DynApplyParam** eaParams = pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO].eaParams;
						const U32 uiNumParams = eaSize(&eaParams);
						if ( uiNumParams )
							dynFxApplyF32Params(uiNumParams, eaParams, pPath->uiTokenIndex, pParamBlock, &pNextPoint->fStartV, pPoint->uiWhichFloat);
					}
					{
						DynJitterList** eaJLists = pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[edoValue].eaJLists;
						const U32 uiNumJLists = eaSize(&eaJLists);
						if ( uiNumJLists )
							dynFxApplyF32JitterList(uiNumJLists, eaJLists, pPath->uiTokenIndex, &pNextPoint->fStartV, ParseDynObject, pNextPoint->uiWhichFloat);
					}
					dynFxApplyF32DynOps(&pNextPoint->fStartV, &pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pPath->uiTokenIndex, pNextPoint->uiWhichFloat, pParentParticle);
					if ( pPoint->uiInterpType == ediLinear || pPoint->uiInterpType == ediEaseIn || pPoint->uiInterpType == ediEaseInAndOut || pPoint->uiInterpType == ediEaseOut )// !pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO].puiInterpTypes || pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO].puiInterpTypes[pPath->uiTokenIndex] == ediLinear )
					{
						F32 fTimeFromAToB = FLOATTIME(pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->uiTimeStamp) - FLOATTIME(pEvent->keyFrames[pPoint->uiKeyFrameIndex]->uiTimeStamp);
						pPoint->fDiffV = (pNextPoint->fStartV - pPoint->fStartV) / fTimeFromAToB;
					}
					else
					{
						pPoint->fDiffV = (pNextPoint->fStartV - pPoint->fStartV);
					}
				}
			}
		}
		else // Handle quats
		{
			U32 uiPointIndex;
			Quat qValueA;
			for (uiPointIndex=0; uiPointIndex < pPath->uiNumPathPoints; uiPointIndex += pPath->uiFloatsPer )
			{
				DynFxDynamicPathPoint* pPoint = &pPath->pPathPoints[uiPointIndex];
				DynFxDynamicPathPoint* pNextPoint = (uiPointIndex+pPath->uiFloatsPer < pPath->uiNumPathPoints)?&pPath->pPathPoints[uiPointIndex+pPath->uiFloatsPer]:NULL;
				Quat qValueB;
				int i;

				if ( uiPointIndex < pPath->uiFloatsPer )
				{
					copyQuat((F32*)TokenStoreGetPointer(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], 0, NULL), qValueA);
					if ( pParamBlock )
					{
						DynApplyParam** eaParams = pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[edoValue].eaParams;
						const U32 uiNumParams = eaSize(&eaParams);
						if ( uiNumParams )
							dynFxApplyQuatParams(uiNumParams, eaParams, pPath->uiTokenIndex, pParamBlock, qValueA);
					}
					dynFxApplyQuatDynOps(qValueA, &pEvent->keyFrames[pPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pPath->uiTokenIndex );
				}

				for (i=0; i<4; ++i)
				{
					pPath->pPathPoints[uiPointIndex+i].fStartV = qValueA[i];
				}

				if ( pNextPoint )
				{
					Quat qInverseA, qInterpQuat, qDest;
					F32 fTimeFromAToB = FLOATTIME(pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->uiTimeStamp) - FLOATTIME(pEvent->keyFrames[pPoint->uiKeyFrameIndex]->uiTimeStamp);
					copyQuat((F32*)TokenStoreGetPointer(ParseDynObject, pPath->uiTokenIndex, &pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], 0, NULL), qValueB);
					if ( pParamBlock )
					{
						DynApplyParam** eaParams = pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[edoValue].eaParams;
						const U32 uiNumParams = eaSize(&eaParams);
						if ( uiNumParams )
							dynFxApplyQuatParams(uiNumParams, eaParams, pPath->uiTokenIndex, pParamBlock, qValueB);
					}
					dynFxApplyQuatDynOps(qValueB, &pEvent->keyFrames[pNextPoint->uiKeyFrameIndex]->objInfo[pPath->uiEDO], pPath->uiTokenIndex );

					quatInverse(qValueA, qInverseA);
					quatMultiply(qValueB, qInverseA, qInterpQuat);
					quatToAxisAngle(qInterpQuat, qDest, &qDest[3]);
					qDest[3] /= fTimeFromAToB;

					for (i=0; i<4; ++i)
					{
						pPath->pPathPoints[uiPointIndex+i].fDiffV = qDest[i];
					}

					copyQuat(qValueB, qValueA);
				}
				else
				{
					pPoint->fDiffV = 0.0f;
				}
			}
		}
		dynFxSetUpdateMask(pPath);
	}
}

DynFx* dynFxFindChildrenWithBoneNameEx(DynFx* pFx, const char* pcBoneName, const char* pcAlias) 
{
	const char* pcFindNode = FIRST_IF_SET(pcAlias, pcBoneName);
	U32 uiChildIndex;
	const U32 uiNumChildren = eaUSize(&pFx->eaChildFx);
	
	if (!pcAlias && SAFE_MEMBER2(pFx->pManager->pDrawSkel,pSkeleton,pRoot))
	{
		DynFxInfo* pFxInfo = GET_REF(pFx->hInfo);
		pcFindNode = pcAlias = dynSkeletonGetNodeAlias(pFx->pManager->pDrawSkel->pSkeleton, pcBoneName, SAFE_MEMBER(pFxInfo,bUseMountNodeAliases));
	}

	for (uiChildIndex=0; uiChildIndex < uiNumChildren; ++uiChildIndex)
	{
		DynFx* pResult;
		DynFx* pChildFx = pFx->eaChildFx[uiChildIndex];
		if ( pChildFx->pParticle && pChildFx->pParticle->pDraw->node.pcTag 
			&& stricmp(pChildFx->pParticle->pDraw->node.pcTag, pcFindNode)==0 )
		{
			pResult = pChildFx;
		}
		else
		{
			pResult = dynFxFindChildrenWithBoneNameEx(pChildFx, pcBoneName, pcAlias);
		}

		if ( pResult )
			return pResult;
	}
	return NULL;
}

static void dynFxSkinToChildren( SA_PARAM_NN_VALID DynDrawParticle* pDraw, SA_PARAM_NN_VALID DynFx* pFx ) 
{
	// Now we have a model, skin it!
	if ( pDraw->pModel && pDraw->pModel->header && eaSize(&pDraw->pModel->header->bone_names) > 0 )
	{
		if ( !pDraw->pcBaseSkeleton )
		{
			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			ErrorFilenamef(pInfo->pcFileName, "Can not skin without a skeleton in %s", pInfo->pcDynName );
			pFx->bWaitingToSkin = false;
			return;
		}
		
		if ( pDraw->pcBaseSkeleton && !pDraw->pBaseSkeleton )
			pDraw->pBaseSkeleton = dynBaseSkeletonFind(pDraw->pcBaseSkeleton);

		if ( !pDraw->pBaseSkeleton )
		{
			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			ErrorFilenamef(pInfo->pcFileName, "Unable to find skeleton %s. Can not skin without a skeleton in %s", pDraw->pcBaseSkeleton, pInfo->pcDynName );
			pFx->bWaitingToSkin = false;
			return;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pDraw->pModel->header->bone_names, const char, pcBoneName)
		{
			// Try to find a node with this bone name
			DynFx* pBoneFx = dynFxFindChildrenWithBoneName(pFx, pcBoneName);
			DynNode* pDynNode = pBoneFx?dynFxGetNode(pBoneFx):NULL;

			if ( pDynNode )
				eaPush(&pDraw->eaSkinChildren, pDynNode);
			else
			{
				DynFxInfo *pInfo = GET_REF(pFx->hInfo);
				ErrorFilenamef(pInfo->pcFileName, "Must provide every bone when SkinToChildren is active. Missing bone %s", pcBoneName);
				pFx->bWaitingToSkin = false;
				eaDestroy(&pDraw->eaSkinChildren);
				pDraw->pBaseSkeleton = NULL;
				return;
			}
		}
		FOR_EACH_END;



		pFx->bWaitingToSkin = false;
	}
}

static void dynFxCostumeKeyframe( DynFx* pFx, DynKeyFrame* pKeyFrame)
{
	FOR_EACH_IN_EARRAY(pKeyFrame->eaCostume, DynFxCostume, pCostumeInfo)
	{
		DynFxInfo* pFxInfo = GET_REF(pFx->hInfo);
		const char* pcCostume = pCostumeInfo->pcCostume;
		U32 uiNumParams = eaSize(&pCostumeInfo->eaParams);
		DynParamBlock* pParamBlock = GET_REF(pFx->hParamBlock);
		DynParamBlock *pFallbackParamBlock = pFxInfo ? &pFxInfo->paramBlock : NULL;

		if (pParamBlock && uiNumParams) {
			dynFxApplyCopyParamsGeneral(uiNumParams, pCostumeInfo->eaParams, PARSE_DYNFXCOSTUME_COSTUMENAME_INDEX, pParamBlock, (void*)&pcCostume, parse_DynFxCostume);
		}

		if (pcCostume ||
			pCostumeInfo->bCloneSourceCostume)
		{
			const WLCostume* pCostume;
			const SkelInfo* pSkelInfo;
			const char *pcCostumeFxTag;

			if (pFx->pParticle->pDraw->pDrawSkeleton) {
				dynDrawSkeletonFree(pFx->pParticle->pDraw->pDrawSkeleton);
			}

			if (pFx->pParticle->pDraw->pSkeleton) {
				dynSkeletonFree(pFx->pParticle->pDraw->pSkeleton);
			}

			if (pCostumeInfo->bCloneSourceCostume)
			{
				//assuming it's safe to use the WLCostume from the FX manager since it should already be loaded,
				//normally we use a DynCostume instead to avoid problems with loading/caching WLCostumes over the network
				DynDrawSkeleton *pDrawSkeleton = dynFxManagerGetDrawSkeleton(pFx->pManager);
				if (SAFE_MEMBER(pDrawSkeleton,pSkeleton) && REF_HANDLE_IS_ACTIVE(pDrawSkeleton->pSkeleton->hCostume))  {
					pCostume = GET_REF(pDrawSkeleton->pSkeleton->hCostume);
				} else {
					pCostume = NULL;
				}
			}
			else
			{
				pCostume = dynCostumeFetchOrCreateFromFxCostume(pcCostume, pCostumeInfo, pParamBlock, pFallbackParamBlock);	
			}	

			if (!pCostume)
			{
				FxFileError(GET_REF(pFx->hInfo) ? GET_REF(pFx->hInfo)->pcFileName : "Unknown File",
							"Failed to find %s Costume %s, can't create it.",
							pCostumeInfo->bCloneSourceCostume ? "Cloned" : "",
							FIRST_IF_SET(pCostumeInfo->pcCostume,"un-named"));
				continue;
			}

			pSkelInfo = pCostume ? GET_REF(pCostume->hSkelInfo) : NULL;
			if (!pSkelInfo)
			{
				FxFileError(GET_REF(pFx->hInfo) ? GET_REF(pFx->hInfo)->pcFileName : "Unknown File",
							"Failed to find SkelInfo %s in %s Costume %s, can't create costume.",
							REF_STRING_FROM_HANDLE(pCostume->hSkelInfo),
							pCostumeInfo->bCloneSourceCostume ? "Cloned" : "",
							FIRST_IF_SET(pCostumeInfo->pcCostume,"un-named"));
				continue;
			}

			pcCostumeFxTag = FIRST_IF_SET(pCostumeInfo->pcCostumeTag, s_pcNotSet);

			// Actually create skeleton
			pFx->pParticle->pDraw->pSkeleton = dynSkeletonCreate(pCostume, false, false, false, false, false, pcCostumeFxTag);
			if (pFx->pParticle->pDraw->pSkeleton)
			{
				pFx->pParticle->pDraw->pDrawSkeleton = dynDrawSkeletonCreate( pFx->pParticle->pDraw->pSkeleton, pCostume, NULL, false, false, true);
				if (pFx->pParticle->pDraw->pDrawSkeleton)
				{
					DynDrawSkeleton* pDrawSkeleton;
					Quat qParent;
						
					//there's a bunch of preexisting code elsewhere that zeros out quaternions that really should probably be setting their value to {0,0,0,-1}
					//rather than reworking all of that code, this block will just catch any such "errors" and correct for them when setting up a dyncostume skeleton
					//if we don't do this, the zeroed out quaternions will crush the dyncostume skeleton down to a single point during animation
					dynNodeGetWorldSpaceRot(&pFx->pParticle->pDraw->node, qParent);
					if (fabsf(qParent[0]) < .1f &&
						fabsf(qParent[1]) < .1f &&
						fabsf(qParent[2]) < .1f &&
						fabsf(qParent[3]) < .1f)
					{
						dynNodeSetRot(&pFx->pParticle->pDraw->node, unitquat);
					}

					dynNodeParent(pFx->pParticle->pDraw->pSkeleton->pRoot, &pFx->pParticle->pDraw->node);
					pDrawSkeleton = dynFxManagerGetDrawSkeleton(pFx->pManager);
					if (pDrawSkeleton && !pCostumeInfo->bNotDependentSkeleton)
					{
						dynSkeletonPushDependentSkeleton(pDrawSkeleton->pSkeleton, pFx->pParticle->pDraw->pSkeleton, pCostumeInfo->bInheritBits, pCostumeInfo->bSubCostume);
					}
					pFx->pParticle->pDraw->pDrawSkeleton->bFXCostume = true;
				}

				if (gConf.bNewAnimationSystem)
				{
					pFx->pParticle->pDraw->pAnimWordFeed = calloc(sizeof(DynAnimWordFeed), 1);
					if (pFx->pParticle->pDraw->pAnimWordFeed) {
						dynSkeletonPushAnimWordFeed(pFx->pParticle->pDraw->pSkeleton, pFx->pParticle->pDraw->pAnimWordFeed);
					}
				}
				else
				{
					pFx->pParticle->pDraw->pBitFeed = calloc(sizeof(DynBitFieldGroup), 1);
					if(pFx->pParticle->pDraw->pBitFeed) {
						dynSeqPushBitFieldFeed(pFx->pParticle->pDraw->pSkeleton, pFx->pParticle->pDraw->pBitFeed);
					}
				}
			}
		}

		if (pFx->pParticle->pDraw->pSkeleton)
		{
			if (pCostumeInfo->bSnapshotOfCallersPose)
			{
				DynDrawSkeleton* pCallerDrawSkeleton = dynFxManagerGetDrawSkeleton(pFx->pManager);
				if (SAFE_MEMBER(pCallerDrawSkeleton,pSkeleton))
				{
					dynSkeletonSetSnapshot(	pCallerDrawSkeleton->pSkeleton,
											pFx->pParticle->pDraw->pSkeleton);
				}
			}

			if (pCostumeInfo->bReleaseSnapshot)
			{
				dynSkeletonReleaseSnapshot(pFx->pParticle->pDraw->pSkeleton);
			}

			if (gConf.bNewAnimationSystem)
			{
				DynAnimWordFeed *pAnimWordFeed = pFx->pParticle->pDraw->pAnimWordFeed;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcAnimKeyword, const char, pcBit) {
					dynSkeletonPlayKeywordInAnimWordFeed(pAnimWordFeed, allocFindString(pcBit));
				} FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcAnimFlag, const char, pcBit) {
					dynSkeletonPlayFlagInAnimWordFeed(pAnimWordFeed, allocFindString(pcBit));
				} FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcSetStance, const char, pcBit) {
					dynSkeletonSetStanceInAnimWordFeed(pAnimWordFeed, allocFindString(pcBit));
				} FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcClearStance, const char, pcBit) {
					dynSkeletonClearStanceInAnimWordFeed(pAnimWordFeed, allocFindString(pcBit));
				} FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcToggleStance, const char, pcBit) {
					dynSkeletonToggleStanceInAnimWordFeed(pAnimWordFeed, allocFindString(pcBit));
				} FOR_EACH_END;
			}
			else
			{
				DynBitFieldGroup* pBitField = pFx->pParticle->pDraw->pBitFeed;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcSetBits, const char, pcBit)
					dynBitFieldGroupSetBit(pBitField, pcBit);
				FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcClearBits, const char, pcBit)
					dynBitFieldGroupClearBit(pBitField, pcBit);
				FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcToggleBits, const char, pcBit)
					dynBitFieldGroupToggleBit(pBitField, pcBit);
				FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumeInfo->eapcFlashBits, const char, pcBit)
					dynBitFieldGroupFlashBit(pBitField, pcBit);
				FOR_EACH_END;
			}
		}
	}
	FOR_EACH_END
}

static void dynFxProcessPassedKeyFrame(SA_PARAM_NN_VALID DynFx* pFx, SA_PARAM_NN_VALID DynEventUpdater* pUpdater, U32 uiKeyFrameIndex)
{
	// Check for any calls
	DynKeyFrame* pKeyFrame = pUpdater->pEvent->keyFrames[uiKeyFrameIndex];
	
	pUpdater->iKeyBeforeIndex = (int)uiKeyFrameIndex;

	// check for creation
	switch ( pKeyFrame->eType )
	{
		xcase eDynKeyFrameType_Create:
		{
			if (!pFx->pParticle)
			{
				DynFxInfo *pInfo = GET_REF(pFx->hInfo);
				pFx->pParticle = dynParticleCreate(
					pUpdater, SAFE_MEMBER(pInfo,bDontHueShift)?0.0f:pFx->fHue,
					pFx->fSaturationShift, pFx->fValueShift,
					pFx->pSortBucket, SAFE_MEMBER(pInfo,bDontDraw),
					SAFE_MEMBER(pInfo,bLowRes),
					SAFE_MEMBER(pInfo,events));
			}
			pFx->bHasCreatedParticle = 1;
		}
		xcase eDynKeyFrameType_Destroy:
		{
			if ( pFx->pParticle )
			{
				dynParticleFree(pFx->pParticle, dynFxManGetDynFxRegion(pFx->pManager));
				pFx->pParticle = NULL;
			}
		}
		xcase eDynKeyFrameType_Recreate:
		{
			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			if ( pFx->pParticle )
			{
				dynParticleFree(pFx->pParticle, dynFxManGetDynFxRegion(pFx->pManager));
				pFx->pParticle = NULL;
			}
			pFx->pParticle = dynParticleCreate(
				pUpdater, SAFE_MEMBER(pInfo,bDontHueShift)?0.0f:pFx->fHue,
				pFx->fSaturationShift, pFx->fValueShift,
				pFx->pSortBucket, SAFE_MEMBER(pInfo,bDontDraw),
				SAFE_MEMBER(pInfo,bLowRes),
				SAFE_MEMBER(pInfo,events));
			pFx->bHasCreatedParticle = 1;
		}
		xcase eDynKeyFrameType_Update:
		{
		}
	}

	dynFxUpdatePathSetCurrentKeyFrames(pUpdater->pPathSet, pFx->pParticle, pUpdater->iKeyBeforeIndex, pFx);

	dynFxUpdateParentBhvrKeyframe(pFx, pKeyFrame);

	if ( pKeyFrame->fFadeOutTime > 0.0f )
	{
		if (pFx->fFadeOutTime < 0.0f)
			pFx->fFadeOutTime = pKeyFrame->fFadeOutTime;
		else
			pFx->fFadeOutTime = MIN(pKeyFrame->fFadeOutTime, pFx->fFadeOutTime);
	}

	if ( pKeyFrame->fFadeInTime > 0.0f )
	{
		if (pFx->fFadeInTime < 0.0f)
			pFx->fFadeInTime = pKeyFrame->fFadeInTime;
		else
			pFx->fFadeInTime = MIN(pKeyFrame->fFadeInTime, pFx->fFadeInTime);
	}

	if ( pKeyFrame->fFadeOutTime > 0.0f || pKeyFrame->fFadeInTime > 0.0f )
	{
		if (pKeyFrame->bSystemFade)
			pFx->bSystemFade = true;
	}

	if(pFx->pManager->pDrawSkel)
		dynFxGrabCostumeModel(pFx);

	if (pFx->pParticle)
	{
		if (pFx->pParticle->pDraw->pcModelName) {
			dynFxAddNodesFromGeometry(pFx, pFx->pParticle->pDraw->pcModelName);
		}
		dynFxCallRaycastStarts(pUpdater, &pKeyFrame->eaRaycastStart, pFx);
		dynFxCallRaycastStops(pUpdater, &pKeyFrame->eaRaycastStop, pFx);
	}

	dynFxSendMessages(pFx, &pKeyFrame->eaMessage);

	if (pFx->pParticle)
	{ 
		if (!uiKeyFrameIndex &&
			(	eaSize(&pKeyFrame->childCallCollection.eaChildCall) > 0 ||
				eaSize(&pKeyFrame->childCallCollection.eaChildCallList) > 0))
		{
			dynParticleUpdate(pFx->pParticle, pFx, pUpdater->pPathSet, 1, true);
		}

		if (eaSize(&pKeyFrame->eaCostume) > 0) {
			dynFxCostumeKeyframe(pFx, pKeyFrame);
		}
	}

	dynFxCallChildDyns(pFx, &pKeyFrame->childCallCollection, NULL);

	dynFxCallLoopStarts(pUpdater, &pKeyFrame->eaLoopStart, pFx);
	dynFxCallLoopEnds(pFx, &pKeyFrame->eaLoopStop);

	if (pFx->pParticle)
	{
		DynFxInfo *pInfo = GET_REF(pFx->hInfo);
		dynFxCallEmitterStarts(pFx->pParticle, &pKeyFrame->eaEmitterStart, pFx->fHue, pFx->fSaturationShift, pFx->fValueShift, &pFx->pParticle->pDraw->node, pFx->iPriorityLevel, pFx);
		dynFxCallEmitterStops(pFx->pParticle, &pKeyFrame->eaEmitterStop, pFx);

		if (pInfo->pcIKTargetTag && pFx->pManager)
		{
			const DynNode* pIKNode = NULL;
			if (eaSize(&pInfo->eaIKTargetBone))
			{
				EARRAY_CONST_FOREACH_BEGIN(pInfo->eaIKTargetBone, curTargetBone, numTargetBones);
					pIKNode = dynFxNodeByName(pInfo->eaIKTargetBone[curTargetBone], pFx);
					dynFxManAddIKTarget(pFx->pManager, pInfo->pcIKTargetTag, pIKNode, pFx);
				EARRAY_FOREACH_END;
			}
			else
			{
				pIKNode = &pFx->pParticle->pDraw->node;
				dynFxManAddIKTarget(pFx->pManager, pInfo->pcIKTargetTag, pIKNode, pFx);
			}				
		}
	}

	dynFxClearForceOnces(pUpdater);
	dynFxCallForceStarts(pUpdater, &pKeyFrame->eaForceStart);
	dynFxCallForceEnds(pUpdater, &pKeyFrame->eaForceStop);

	dynFxCallSoundStartsAndEnds(pUpdater, pFx, pKeyFrame);


	// Be sure that any costume changes get applied
	if (
		pKeyFrame->objInfo[edoValue].obj.draw.eEntityMaterialMode != edemmNone
		|| pKeyFrame->objInfo[edoValue].obj.draw.eEntityTintMode != edetmNone
		|| pKeyFrame->objInfo[edoValue].obj.draw.eEntityLightMode != edelmNone
		|| pKeyFrame->objInfo[edoValue].obj.draw.eEntityScaleMode != edesmNone
		)
	{
		dynFxApplyToCostume(pFx);
		if (pKeyFrame->eaEntCostumeParts)
			pFx->eaEntCostumeParts = pKeyFrame->eaEntCostumeParts;

		pFx->bEntMaterialExcludeOptionalParts = pKeyFrame->bEntMaterialExcludeOptionalParts;
	}

	if(pFx->pManager->pDrawSkel)
	{
		if (eaSize(&pKeyFrame->eaSeverBones) > 0) {
			dynDrawSkeletonSeverBones(pFx->pManager->pDrawSkel, pKeyFrame->eaSeverBones, eaSize(&pKeyFrame->eaSeverBones));
		}

		if(eaSize(&pKeyFrame->eaRestoreSeveredBones) > 0) {
			dynDrawSkeletonRestoreSeveredBones(pFx->pManager->pDrawSkel, pKeyFrame->eaRestoreSeveredBones, eaSize(&pKeyFrame->eaRestoreSeveredBones));
		}
	}

	if (pKeyFrame->pPhysicsInfo)
	{
		if(!pFx->bPhysicsEnabled) {
			DynFxInfo *pInfo = GET_REF(pFx->hInfo);
			DynFxTracker* pFxTracker = pInfo ? dynFxGetTracker(pInfo->pcDynName) : NULL;
			if(pFxTracker) {
				pFxTracker->uiNumPhysicsObjects++;
				dynDebugState.uiNumPhysicsObjects++;
				if (pFxTracker->uiNumPhysicsObjects == 100)
				{
					dynDebugState.uiNumExcessivePhysicsObjectsFX++;
				}
			}
		}

		pFx->bPhysicsEnabled = true;

		if(pFx->pParticle && pFx->pPhysicsInfo && pKeyFrame->pPhysicsInfo->bNoCollide != pFx->pPhysicsInfo->bNoCollide) {
			// Collision property changed.
			QueuedCommand_dpoSetCollidable(pFx->pParticle->pDraw->pDPO, !pKeyFrame->pPhysicsInfo->bNoCollide);
		}

		pFx->pPhysicsInfo = pKeyFrame->pPhysicsInfo;
	}

	if (pFx->pParticle)
	{
		if (pFx->pParticle->pDraw->pMeshTrail &&
			pKeyFrame->objInfo[edoValue].obj.meshTrail.bStopEmitting)
		{
			pFx->pParticle->pDraw->pMeshTrail->bStop = true;
		}

		if (pKeyFrame->bSkinToChildren &&
			pFx->pParticle->pDraw->pcModelName )
		{
			DynDrawParticle* pDraw = pFx->pParticle->pDraw;
			// Skin any geometry to children bones
			// Loop through children, if they match a bone name in our geometry, record the pointer
			if (!pDraw->pModel)
			{
				pDraw->pModel = modelFind(pDraw->pcModelName, true, WL_FOR_FX);
				if (!pDraw->pModel)
				{
					Errorf("Unable to find geometry %s for particle system", pDraw->pcModelName);
				}
			}
			if ( pDraw->pModel )
				pFx->bWaitingToSkin = true;
		}
	}

	if (pKeyFrame->fInheritParentVelocity)
	{
		DynFx* pParentFx = GET_REF(pFx->hParentFx);
		if (pParentFx && pParentFx->pParticle)
		{
			CHECK_FINITEVEC3(pParentFx->pParticle->vWorldSpaceVelocity);
			CHECK_FINITE(pKeyFrame->fInheritParentVelocity);
			scaleVec3(pParentFx->pParticle->vWorldSpaceVelocity, pKeyFrame->fInheritParentVelocity, pFx->pParticle->pDraw->vVelocity);
		}
	}

	if (pKeyFrame->bHide)
		pFx->bHidden = true;
	if (pKeyFrame->bUnhide)
		pFx->bHidden = false;
}

static void dynFxDoFadeOut( SA_PARAM_NN_VALID DynFx* pFx, DynFxTime uiDeltaTime) 
{
	if (pFx->fFadeOutTime > 0.0f)
	{
		F32 fDeltaTime = FLOATTIME(uiDeltaTime);
		F32 fTimeLeft = pFx->fFadeOut * pFx->fFadeOutTime;
		pFx->fFadeOut = (fTimeLeft - fDeltaTime) / pFx->fFadeOutTime;
		if (pFx->fFadeOut <= 0.0f)
		{
			pFx->fFadeOut = 0.0f;
			pFx->fFadeOutTime = -2.0f;
		}
	}

	if (pFx->fFadeInTime > 0.0f)
	{
		F32 fDeltaTime = FLOATTIME(uiDeltaTime);
		F32 fTimeLeft = (1.0f - pFx->fFadeIn) * pFx->fFadeInTime;
		pFx->fFadeIn = 1.0f - (fTimeLeft - fDeltaTime) / pFx->fFadeInTime;
		if (pFx->fFadeIn >= 1.0f)
		{
			pFx->fFadeIn = 1.0f;
			pFx->fFadeInTime = -2.0f;
		}
	
	} else {
		pFx->fFadeIn = 1.0f;
	}

}



bool dynEventUpdaterUpdate( int iPartitionIdx, DynFx* pFx, DynEventUpdater* pUpdater, DynFxTime uiDeltaTime )
{
	DynKeyFrame** eaKeyFrames = pUpdater->pEvent->keyFrames;
	const U32 uiNumKeys = eaSize(&eaKeyFrames);
	DynFxTime uiStartTime;
	U32 uiAfterKeyIndex;

	// Update time
	if ( pUpdater->bNeverUpdated )
	{
		pUpdater->bNeverUpdated = false;
		uiStartTime = 0;
		if ( uiNumKeys && eaKeyFrames[0]->uiTimeStamp == 0 )
			dynFxProcessPassedKeyFrame(pFx, pUpdater, 0);
		else
			pUpdater->iKeyBeforeIndex = -1;
		pUpdater->uiTimeSinceStart += uiDeltaTime;
	}
	else
	{
		uiStartTime = pUpdater->uiTimeSinceStart;
		pUpdater->uiTimeSinceStart += uiDeltaTime;
	}

	uiAfterKeyIndex = pUpdater->iKeyBeforeIndex+1;

	if ( pFx->bWaitingToSkin && pFx->pParticle && pFx->pParticle->pDraw->pModel && modelLODIsLoaded(modelLoadLOD(pFx->pParticle->pDraw->pModel, 0)))
		dynFxSkinToChildren(pFx->pParticle->pDraw, pFx);

	// This loop is set up to handle the case where we pass multiple keyframes in one update
	// If we don't handle each in one update, we'll get different results at different framerates, which is bad
	// Unfortunately, this means things get slower as the frame rate drops, which is also bad...
	{
		U32 uiKeyWePassed;
		DynFxTime uiNewDeltaTime = uiDeltaTime;
		for(uiKeyWePassed = pUpdater->iKeyBeforeIndex+1; uiKeyWePassed<uiNumKeys && pUpdater->uiTimeSinceStart > eaKeyFrames[uiKeyWePassed]->uiTimeStamp; ++uiKeyWePassed)
		{
			DynFxTime uiPerKeyDeltaTime = eaKeyFrames[uiKeyWePassed]->uiTimeStamp - uiStartTime;
			++uiAfterKeyIndex;
			if ( pFx->pParticle )
				dynParticleUpdate(pFx->pParticle, pFx, pUpdater->pPathSet, uiPerKeyDeltaTime, true);
			dynFxDoFadeOut(pFx, uiPerKeyDeltaTime);
			uiNewDeltaTime -= uiPerKeyDeltaTime;
			// Do fade out
			dynFxProcessPassedKeyFrame(pFx, pUpdater, uiKeyWePassed);
			uiStartTime = eaKeyFrames[uiKeyWePassed]->uiTimeStamp; // update time for next go around
		}
		if ( pFx->pParticle && uiNewDeltaTime > 0 )
		{
			dynParticleUpdate(pFx->pParticle, pFx, pUpdater->pPathSet, uiNewDeltaTime, false);
		}
		if (uiNewDeltaTime > 0)
			dynFxDoFadeOut(pFx, uiNewDeltaTime);
	}

	//Process the keyframes we passed

	dynFxUpdateParentBhvr(pFx, uiDeltaTime);

	// Process physics changes
	if (pFx->bPhysicsEnabled && pFx->pParticle && !pFx->pParticle->pDraw->pDPO)
		dynFxCreateDPO(pFx);

	//Process any loops
	{
		const U32 uiNumLoops = eaSize(&pUpdater->eaLoopUpdater);
		U32 uiLoopIndex;
		F32 fDeltaTimeToUse = MIN(FLOATTIME(uiDeltaTime), 0.03333f); // We don't want a runaway feedback loop for calling, so lock this time at 1/30
		F32 fSquaredDist = -1.0f;
		for (uiLoopIndex=0; uiLoopIndex<uiNumLoops; ++uiLoopIndex)
		{
			DynLoopUpdater* pLoop = pUpdater->eaLoopUpdater[uiLoopIndex];
			if ( pLoop->fLODFarSquared > 0.0f || pLoop->fLODNearSquared > 0.0f  )
			{
				if (fSquaredDist < 0.0f)
				{
					Vec3 vWorldSpaceDiff;
					Vec3 vCamNode;
					dynNodeGetWorldSpacePos(dynCameraNodeGet(), vCamNode);
					if (pFx->pParticle)
					{
						Vec3 vPartNode;
						dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, vPartNode);
						subVec3(vPartNode, vCamNode, vWorldSpaceDiff);
					}
					else
					{
						Vec3 vManNode;
						dynNodeGetWorldSpacePos(dynFxManGetDynNode(pFx->pManager), vManNode);
						subVec3(vManNode, vCamNode, vWorldSpaceDiff);
					}
					fSquaredDist = lengthVec3Squared(vWorldSpaceDiff);
				}
				// Calc distance, and use this to figure out whether or not to play
				if (
					( pLoop->fLODFarSquared > 0.0f && fSquaredDist > pLoop->fLODFarSquared )
					|| ( pLoop->fLODNearSquared > 0.0f && fSquaredDist <= pLoop->fLODNearSquared )
					)
				{
					continue; // do not process this loop
				}
					
			}
			if ( pLoop->pLoopInfo->fCyclePeriod > 0.0f )
			{
				U32 uiCount=0;
				pLoop->fTimeAccum -= fDeltaTimeToUse;
				while (pLoop->fTimeAccum <= 0.0f )
				{
					DynFx* pLastChild = dynFxCallChildDyns(pFx, &pLoop->pLoopInfo->childCallCollection, GET_REF(pLoop->hLastChildCreated));
					REMOVE_HANDLE(pLoop->hLastChildCreated);
					if (pLastChild)
						ADD_SIMPLE_POINTER_REFERENCE_DYN(pLoop->hLastChildCreated, pLastChild);

					dynFxSendMessages(pFx, &pLoop->pLoopInfo->eaMessage);

					pLoop->fTimeAccum += pLoop->pLoopInfo->fCyclePeriod + pLoop->pLoopInfo->fCyclePeriodJitter * randomF32();

					uiCount++;
					if (uiCount == 100)
						pLoop->fDistAccum = 0;
				}
			}
			if (pFx->pParticle && pLoop->pLoopInfo->fDistance > 0.0f)
			{
				U32 uiCount=0;
				pLoop->fDistAccum -= pFx->pParticle->fDistTraveled;
				while (pLoop->fDistAccum <= 0.0f)
				{
					DynFx* pLastChild = dynFxCallChildDyns(pFx, &pLoop->pLoopInfo->childCallCollection, GET_REF(pLoop->hLastChildCreated));
					REMOVE_HANDLE(pLoop->hLastChildCreated);
					if (pLastChild)
						ADD_SIMPLE_POINTER_REFERENCE_DYN(pLoop->hLastChildCreated, pLastChild);

					dynFxSendMessages(pFx, &pLoop->pLoopInfo->eaMessage);

					pLoop->fDistAccum += pLoop->pLoopInfo->fDistance + pLoop->pLoopInfo->fDistanceJitter * randomF32();

					uiCount++;
					if (uiCount == 100)
						pLoop->fDistAccum = 0;
				}
			}
		}
	}

	// Process all ray casts
	if (pFx->pParticle)
	{
		FOR_EACH_IN_EARRAY(pUpdater->eaRaycastUpdater, DynRaycastUpdater, pRayUpdater)
			dynRaycastUpdate(iPartitionIdx, pRayUpdater, pFx, pUpdater);
		FOR_EACH_END

		// Process all forces
		FOR_EACH_IN_EARRAY(pUpdater->eaForces, const DynForce, pForce)
			dynFxForceUpdate(dynFxGetNode(pFx), dynFxManGetDynFxRegion(pFx->pManager), pForce, uiDeltaTime);
		FOR_EACH_END

	}

	dynSoundMove(&pUpdater->soundUpdater, pFx);

	// Calc world space xform
	if ( pFx->pParticle )
	{
		Vec3 vNewNodePos;
		dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, vNewNodePos);
		if (uiDeltaTime == 0)
		{
			zeroVec3(pFx->pParticle->vWorldSpaceVelocity);
			pFx->pParticle->fDistTraveled = 0.0f;
		}
		else
		{
			Vec3 vWorldSpaceVelocity;
			if (pFx->pParticle->bVelCalculated) {
				subVec3(vNewNodePos, pFx->pParticle->vOldPos, vWorldSpaceVelocity);
			} else {
				zeroVec3(vWorldSpaceVelocity);
			}
			pFx->pParticle->fDistTraveled = lengthVec3(vWorldSpaceVelocity);
			copyVec3(vNewNodePos, pFx->pParticle->vOldPos);
			pFx->pParticle->bVelCalculated = true;
			scaleVec3(vWorldSpaceVelocity, (1.0f / FLOATTIME(uiDeltaTime)), pFx->pParticle->vWorldSpaceVelocity);
		}
		switch ( pFx->pParticle->pDraw->iStreakMode )
		{
			xcase DynStreakMode_Velocity:
			{
				if ( uiDeltaTime > 1 )
				{
					scaleVec3(pFx->pParticle->vWorldSpaceVelocity, pFx->pParticle->pDraw->fStreakScale, pFx->pParticle->pDraw->vStreakDir);
				}
				else
				{
					zeroVec3(pFx->pParticle->pDraw->vStreakDir);
				}
			}
			xcase DynStreakMode_Parent:
			{
				DynNode* pParentNode = dynNodeGetParent(&pFx->pParticle->pDraw->node);
				if ( pParentNode )
				{
					Vec3 vNode;
					dynNodeGetWorldSpacePos(pParentNode, vNode);
					subVec3( vNode, vNewNodePos, pFx->pParticle->pDraw->vStreakDir);
					scaleVec3(pFx->pParticle->pDraw->vStreakDir, pFx->pParticle->pDraw->fStreakScale, pFx->pParticle->pDraw->vStreakDir);
				}
			}
			xcase DynStreakMode_ScaleToTarget:
			{
				DynNode* pScaleToNode = GET_REF(pFx->hScaleToNode);
				if ( pScaleToNode )
				{
					Vec3 vNode;
					dynNodeGetWorldSpacePos(pScaleToNode, vNode);
					subVec3( vNode, vNewNodePos, pFx->pParticle->pDraw->vStreakDir);
					scaleVec3(pFx->pParticle->pDraw->vStreakDir, pFx->pParticle->pDraw->fStreakScale, pFx->pParticle->pDraw->vStreakDir);
				}
			}
			xcase DynStreakMode_Chain:
			{
				DynFx* pSiblingFx = GET_REF(pFx->hSiblingFx);
				if ( pSiblingFx && pSiblingFx->pParticle )
				{
					DynNode* pSiblingNode = &pSiblingFx->pParticle->pDraw->node;
					Vec3 vNode;
					dynNodeGetWorldSpacePos(pSiblingNode, vNode);
					subVec3( vNode, vNewNodePos, pFx->pParticle->pDraw->vStreakDir);
					scaleVec3(pFx->pParticle->pDraw->vStreakDir, pFx->pParticle->pDraw->fStreakScale, pFx->pParticle->pDraw->vStreakDir);
				}
				else
				{
					zeroVec3(pFx->pParticle->pDraw->vStreakDir);
				}
			}
		}
	}


	if ( uiAfterKeyIndex == uiNumKeys )
	{
		if ( stricmp(pUpdater->pEvent->pcMessageType, "Kill") == 0 )
			pFx->bKill = true;

		if (pUpdater->pEvent->bDebris)
		{
			pFx->bDebris = true;
		}
		if (!pUpdater->pEvent->bKeepAlive )
		{
			// we've exhausted this, get out of here

			return false;
		}

		if (pUpdater->pEvent->bLoop)
		{
			dynEventUpdaterReset(pUpdater, pFx);
			return dynEventUpdaterUpdate(iPartitionIdx, pFx, pUpdater, 1);
		}
	}
	/*
	else if ( pFx->obj[edoValue].node )
	{
		// If we are here, we have a valid after index, look forward and backward to determine where we are in the stream.
		dynFxIntegrateObject(pFx, pUpdater->fTimeSinceStart - fStartTime, fStartTime);
	}
	*/

	return true;
}




void dynFxSetSoundStartFunc(dynFxSoundCreateFunc func)
{
	sndStartFunc = func;
}

void dynFxSetSoundStopFunc(dynFxSoundDestroyFunc func)
{
	sndStopFunc = func;
}

void dynFxSetSoundCleanFunc(dynFxSoundCleanFunc func)
{
	sndCleanFunc = func;
}

void dynFxSetSoundMoveFunc(dynFxSoundMoveFunc func)
{
	sndMoveFunc = func;
}

void dynFxSetSoundGuidFunc(dynFxSoundGetGuidFunc func)
{
	sndGuidFunc = func;
}

void dynFxSetDSPStartFunc(dynFxDSPCreateFunc func)
{
	sndStartDSPFunc = func;
}

void dynFxSetDSPStopFunc(dynFxDSPDestroyFunc func)
{
	sndStopDSPFunc = func;
}

void dynFxSetDSPCleanFunc(dynFxDSPCleanFunc func)
{
	sndCleanDSPFunc = func;
}

void dynFxKillAllSounds(DynSoundUpdater* pUpdater)
{
	if(sndCleanFunc)
	{
		int i;
		//printf("Cleaning %p\n", pUpdater);
		for(i=0; i<eaSize(&pUpdater->eaSoundEvents); i++)
		{
			DynSoundEvent *pEvent = pUpdater->eaSoundEvents[i];

			sndCleanFunc(pEvent->guid);

			free(pEvent);
		}
		eaDestroy(&pUpdater->eaSoundEvents);

		for(i=0; i<eaSize(&pUpdater->eaSoundDSPs); i++)
		{
			DynSoundDSP *pDSP = pUpdater->eaSoundDSPs[i];

			sndCleanDSPFunc(pDSP->guid);

			free(pDSP);
		}
		eaDestroy(&pUpdater->eaSoundDSPs);
	}
}

void dynSoundStart( DynFx* pFx, DynSoundUpdater* pUpdater, const char* pcSoundStart ) 
{
	// Headshots should not play sound in the background
	if (pFx->pManager->bNoSound) {
		return;
	}
	
	if(sndStartFunc && sndGuidFunc)
	{
		DynSoundEvent *pEvent;
		Vec3 pos;
		DynFxInfo *pInfo = GET_REF(pFx->hInfo);
		DynNode *pTargetNode = GET_REF(pFx->hTargetRoot);
		pEvent = callocStruct(DynSoundEvent);
		pUpdater->playedSound = 1;
		pEvent->guid = sndGuidFunc(pFx);
		pEvent->pcEventName = pcSoundStart;
		if (pFx->pParticle)
			dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, pos);
		else if (pTargetNode)
			dynNodeGetWorldSpacePos(pTargetNode, pos);
		else
			zeroVec3(pos);
		++dynDebugState.uiNumFxSoundStarts;
		sndStartFunc(pEvent->guid, pos, (char*)pcSoundStart, pInfo->pcFileName);
		eaPush(&pUpdater->eaSoundEvents, pEvent);
	}
}

void dynSoundStop( DynSoundUpdater* pSoundUpdater, const char* pcSoundEnd ) 
{
	if(sndStopFunc)
	{
		U32 uiSoundEventIndex;
		U32 uiNumSoundEvents = eaSize(&pSoundUpdater->eaSoundEvents);
		for(uiSoundEventIndex=0; uiSoundEventIndex < uiNumSoundEvents; uiSoundEventIndex++)
		{
			DynSoundEvent *pEvent = pSoundUpdater->eaSoundEvents[uiSoundEventIndex];

			if(!stricmp(pcSoundEnd, pEvent->pcEventName))
			{
				sndStopFunc(pEvent->guid);
				break;
			}
		}
	}
}

static void dynSoundDSPStart(DynFx *pFx, SA_PARAM_NN_VALID DynSoundUpdater *pUpdater, const char* pcSoundDSPStart)
{
	if(pFx->pManager->bNoSound)
		return;

	if(sndStartDSPFunc && sndGuidFunc)
	{
		DynSoundDSP *pDSP;
		DynFxInfo *pInfo = GET_REF(pFx->hInfo);

		pDSP = callocStruct(DynSoundDSP);
		pDSP->guid = sndGuidFunc(pFx);
		pDSP->pcDSPName = pcSoundDSPStart;

		sndStartDSPFunc(pDSP->guid, pDSP->pcDSPName, pInfo->pcFileName);
		eaPush(&pUpdater->eaSoundDSPs, pDSP);
	}
}

void dynSoundDSPStop(DynSoundUpdater *pSoundUpdater, const char* pcSoundDSPEnd)
{
	if(sndStopDSPFunc)
	{
		U32 uiDSPIndex;
		U32 uiNumSoundDSPs = eaSize(&pSoundUpdater->eaSoundDSPs);
		for(uiDSPIndex=0; uiDSPIndex < uiNumSoundDSPs; uiDSPIndex++)
		{
			DynSoundDSP *pDSP = pSoundUpdater->eaSoundDSPs[uiDSPIndex];

			if (!stricmp(pcSoundDSPEnd, pDSP->pcDSPName) &&
				sndStopDSPFunc(pDSP->guid)) // handle duplicates correctly
			{
					// we might want to also remove it from the list here instead of waiting for it to get cleaned
					break;
			}
		}
	}
}

void dynSoundMove(DynSoundUpdater* pSoundUpdater, DynFx* pFx) 
{
	if(pFx->pParticle && sndMoveFunc)
	{
		int i;
		Quat rot;
		Vec3 pos = {0,0,0}, vel = {0,0,0}, dir = {0,0,0}, unitZ = {0,0,-1};

		dynNodeGetWorldSpacePos(&pFx->pParticle->pDraw->node, pos);
		dynNodeGetWorldSpaceRot(&pFx->pParticle->pDraw->node, rot);
		copyVec3(pFx->pParticle->vWorldSpaceVelocity, vel);
		quatRotateVec3(rot, unitZ, dir);

		for(i=0; i<eaSize(&pSoundUpdater->eaSoundEvents); i++)
		{
			DynSoundEvent *pEvent = pSoundUpdater->eaSoundEvents[i];

			++dynDebugState.uiNumFxSoundMoves;
			sndMoveFunc(pEvent->guid, pos, vel, dir);
		}
	}
}

static bool dynFxManagerDSPAllowed(DynFxManager *pManager)
{
	return dynFxManIsLocalPlayer(pManager) || pManager->eType==eFxManagerType_UI;
}

static void dynFxCallSoundStartsAndEnds(DynEventUpdater* pUpdater, DynFx *pFx, DynKeyFrame* pKeyFrame)
{

	{
		U32 uiNumSounds = eaSize(&pKeyFrame->ppcSoundStarts);
		U32 uiSoundIndex;
		for (uiSoundIndex=0; uiSoundIndex < uiNumSounds; ++uiSoundIndex)
		{
			char* pcSoundStart = pKeyFrame->ppcSoundStarts[uiSoundIndex];
			dynSoundStart(pFx, &pUpdater->soundUpdater, pcSoundStart);
		}
	}

	{
		U32 uiNumSounds = eaSize(&pKeyFrame->ppcSoundEnds);
		U32 uiSoundIndex;
		for (uiSoundIndex=0; uiSoundIndex < uiNumSounds; ++uiSoundIndex)
		{
			char* pcSoundEnd = pKeyFrame->ppcSoundEnds[uiSoundIndex];
			DynSoundUpdater* pSoundUpdater = &pUpdater->soundUpdater;
			dynSoundStop(pSoundUpdater, pcSoundEnd);
		}
	}

	// DSPs are only ever local player
	if(dynFxManagerDSPAllowed(pFx->pManager))
	{
		U32 uiNumDSPStarts = eaSize(&pKeyFrame->ppcSoundDSPStarts);
		U32 uiNumDSPEnds = eaSize(&pKeyFrame->ppcSoundDSPEnds);
		U32 uiDSPIndex;

		for(uiDSPIndex=0; uiDSPIndex < uiNumDSPStarts; uiDSPIndex++)
		{
			char* pcDSPStart = pKeyFrame->ppcSoundDSPStarts[uiDSPIndex];
			dynSoundDSPStart(pFx, &pUpdater->soundUpdater, pcDSPStart);
		}

		for(uiDSPIndex=0; uiDSPIndex < uiNumDSPEnds; uiDSPIndex++)
		{
			char* pcDSPEnd = pKeyFrame->ppcSoundDSPEnds[uiDSPIndex];
			dynSoundDSPStop(&pUpdater->soundUpdater, pcDSPEnd);
		}
	}
}

static void dynFxCallLoopStarts(DynEventUpdater* pUpdater, DynLoopRef*** peaLoops, DynFx* pFx)
{
	const U32 uiNumLoops = eaSize(peaLoops);
	U32 uiLoopIndex;
	for (uiLoopIndex=0; uiLoopIndex < uiNumLoops; ++uiLoopIndex)
	{
		bool bShouldAdd = true;
		if (pUpdater->eaLoopUpdater)
		{
			const U32 uiNumUpdaters = eaSize(&pUpdater->eaLoopUpdater);
			U32 uiUpdaterIndex;
			for (uiUpdaterIndex=0; uiUpdaterIndex<uiNumUpdaters; ++uiUpdaterIndex)
			{
				DynLoopUpdater* pLoopUpdater = pUpdater->eaLoopUpdater[uiUpdaterIndex];
				if ( pLoopUpdater->pLoopInfo == (*peaLoops)[uiLoopIndex]->pLoop)
				{
					bShouldAdd = false;
					break;
				}
			}
		}

		if (bShouldAdd)
		{
			DynLoopUpdater* pLoopUpdater;
			MP_CREATE(DynLoopUpdater, 1024);
			pLoopUpdater = MP_ALLOC(DynLoopUpdater);
			pLoopUpdater->pLoopInfo = (*peaLoops)[uiLoopIndex]->pLoop;
			pLoopUpdater->fTimeAccum = 0.0f;
			pLoopUpdater->fDistAccum = 0.0f;
			pLoopUpdater->fLODFarSquared = SQR(pLoopUpdater->pLoopInfo->fLODFar);
			pLoopUpdater->fLODNearSquared = SQR(pLoopUpdater->pLoopInfo->fLODNear);
			eaPush(&pUpdater->eaLoopUpdater, pLoopUpdater);
			dynFxLog(pFx, "Starting Loop %s", pLoopUpdater->pLoopInfo->pcLoopTag);
		}
	}
}

static void dynFxCallLoopEnds(DynFx* pFx, DynLoopRef*** peaLoops)
{
	FOR_EACH_IN_EARRAY(pFx->eaDynEventUpdaters, DynEventUpdater, pUpdater)
		const U32 uiNumLoops = eaSize(peaLoops);
		U32 uiLoopIndex;
		for (uiLoopIndex=0; uiLoopIndex < uiNumLoops; ++uiLoopIndex)
		{
			U32 uiNumUpdaters = eaSize(&pUpdater->eaLoopUpdater);
			U32 uiUpdaterIndex;
			for (uiUpdaterIndex=0; uiUpdaterIndex<uiNumUpdaters; ++uiUpdaterIndex)
			{
				DynLoopUpdater* pLoopUpdater = pUpdater->eaLoopUpdater[uiUpdaterIndex];
				if ( pLoopUpdater->pLoopInfo == (*peaLoops)[uiLoopIndex]->pLoop)
				{
					// should remove
					dynFxLog(pFx, "Stopping Loop %s", pLoopUpdater->pLoopInfo->pcLoopTag);
					eaRemoveFast(&pUpdater->eaLoopUpdater, uiUpdaterIndex);
					REMOVE_HANDLE(pLoopUpdater->hLastChildCreated);
					MP_FREE(DynLoopUpdater, pLoopUpdater);
					--uiUpdaterIndex;
					--uiNumUpdaters;
				}
			}
		}
	FOR_EACH_END
}

static void dynFxCallForceStarts(DynEventUpdater* pUpdater, DynForceRef*** peaForces)
{
	const U32 uiNumForces = eaSize(peaForces);
	U32 uiForceIndex;
	for (uiForceIndex=0; uiForceIndex < uiNumForces; ++uiForceIndex)
	{
		bool bShouldAdd = true;
		const U32 uiNumUpdaters = eaSize(&pUpdater->eaForces);
		U32 uiUpdaterIndex;
		for (uiUpdaterIndex=0; uiUpdaterIndex<uiNumUpdaters; ++uiUpdaterIndex)
		{
			const DynForce* pForce = pUpdater->eaForces[uiUpdaterIndex];
			if ( pForce == (*peaForces)[uiForceIndex]->pForce)
			{
				bShouldAdd = false;
				break;
			}
		}

		if (bShouldAdd)
		{
			eaPush(&pUpdater->eaForces, (*peaForces)[uiForceIndex]->pForce);
		}
	}
}

static void dynFxCallForceEnds(DynEventUpdater* pUpdater, DynForceRef*** peaForces)
{
	const U32 uiNumForces = eaSize(peaForces);
	U32 uiForceIndex;
	for (uiForceIndex=0; uiForceIndex < uiNumForces; ++uiForceIndex)
	{
		U32 uiNumUpdaters = eaSize(&pUpdater->eaForces);
		U32 uiUpdaterIndex;
		for (uiUpdaterIndex=0; uiUpdaterIndex<uiNumUpdaters; ++uiUpdaterIndex)
		{
			const DynForce* pForceUpdater = pUpdater->eaForces[uiUpdaterIndex];
			if ( pForceUpdater == (*peaForces)[uiForceIndex]->pForce)
			{
				// should remove
				eaRemoveFast(&pUpdater->eaForces, uiUpdaterIndex);
				--uiUpdaterIndex;
				--uiNumUpdaters;
			}
		}
	}
}

static void dynFxClearForceOnces(DynEventUpdater* pUpdater)
{
	FOR_EACH_IN_EARRAY(pUpdater->eaForces, const DynForce, pForce)
		if (pForce->bImpulse)
		{
			eaRemoveFast(&pUpdater->eaForces, ipForceIndex);
			--ipForceIndex;
		}
	FOR_EACH_END
}

void dynFxCallEmitterStarts(DynParticle* pParticle, DynParticleEmitterRef*** peaEmitterStarts, F32 fHueShift, F32 fSaturationShift, F32 fValueShift, DynNode* pLocation, int iPriorityLevel, DynFx* pFx)
{
	FOR_EACH_IN_EARRAY((*peaEmitterStarts), DynParticleEmitterRef, pStart)
	{
		bool bAlreadyCreated = false;
		int iPriorityLevelToUse = (pStart->pEmitter->iPriorityLevel==edpNotSet)?iPriorityLevel:pStart->pEmitter->iPriorityLevel;

		if (dynFxPriorityBelowDetailSetting(iPriorityLevelToUse))
			continue;

		if (pStart->pEmitter->bLocalPlayerOnly && pFx && pFx->pManager) {
			if(!dynFxManIsLocalPlayer(pFx->pManager)) {
				continue;
			}
		}

		// Check if it already exists
		FOR_EACH_IN_EARRAY(pParticle->eaParticleSets, DynFxFastParticleSet, pCurrentSet)
			if ( pCurrentSet->pcEmitterName == pStart->pcTag )
			{
				bAlreadyCreated = true;
				//Make sure it's still emitting in case it was soft killed
				if (pCurrentSet->bStopEmitting)
				{
					pCurrentSet->bStopEmitting = false;
					dynFxFastParticleSetReset(pCurrentSet);
				}
			}
		FOR_EACH_END

		// If not, go for it
		if (!bAlreadyCreated)
		{
			DynFxFastParticleSet* pSet;
			const DynNode* pMagnet = pStart->pEmitter->pcMagnet?dynFxNodeByName(pStart->pEmitter->pcMagnet, pFx):NULL;
			const DynNode* pEmitTarget = pStart->pEmitter->pcEmitTarget?dynFxNodeByName(pStart->pEmitter->pcEmitTarget, pFx):NULL;
			const DynNode* pTransformTarget = pStart->pEmitter->pcTransformTarget?dynFxNodeByName(pStart->pEmitter->pcTransformTarget, pFx):NULL;
			DynFPSetParams params = {0};
			params.pInfo = GET_REF(pStart->pEmitter->hParticle);
			params.pLocation = pLocation;
			params.pMagnet = pMagnet;
			params.pEmitTarget = pEmitTarget;
			params.pTransformTarget = pTransformTarget;
			params.pParentFX = pFx;
			params.ePosFlag = pStart->pEmitter->position;
			params.eRotFlag = pStart->pEmitter->rotation;
			params.eScaleFlag = pStart->pEmitter->scale;
			params.fScalePosition = pStart->pEmitter->fScalePosition;
			params.fScaleSprite   = pStart->pEmitter->fScaleSprite;
			params.iPriorityLevel = iPriorityLevelToUse;
			params.fDrawDistance = pStart->pEmitter->fDrawDistance;
			params.fMinDrawDistance = pStart->pEmitter->fMinDrawDistance;
			params.pcEmitterName = pStart->pEmitter->pcTag;
			params.peaAtNodes = eaSize(&pStart->pEmitter->eaAtNodes)>0?&pStart->pEmitter->eaAtNodes:NULL;
			params.peaEmitTargetNodes = eaSize(&pStart->pEmitter->eaEmitTargetNodes)>0?&pStart->pEmitter->eaEmitTargetNodes:NULL;
			params.peaWeights = eafSize(&pStart->pEmitter->eaWeights)>0?&pStart->pEmitter->eaWeights:NULL;
			params.bSoftKill = pStart->pEmitter->bSoftKill;
			params.bApplyCountEvenly = pStart->pEmitter->bApplyCountEvenly;

			params.fHueShift        = (pStart->pEmitter->bDontHueShift?0.0f:fHueShift) * 0.00277777777f; // 1 / 360
			params.fSaturationShift = (pStart->pEmitter->bDontHueShift?0.0f:fSaturationShift);
			params.fValueShift =      (pStart->pEmitter->bDontHueShift?0.0f:fValueShift);

			params.fCutoutDepthScale = pStart->pEmitter->fCutoutDepthScale;

			params.fParticleMass = pStart->pEmitter->fParticleMass;
			params.fSystemAlphaFromFx = pStart->pEmitter->fSystemAlphaFromFx;

			params.b2D = pFx->b2D;
			params.bJumpStart = pStart->pEmitter->bJumpStart;
			params.bEnvironmentFX = !!pFx->pManager->pCellEntry;

			if(pStart->pEmitter->bPatternModel && pParticle->pDraw->pcModelName) {
				params.pcPatternModelName = pParticle->pDraw->pcModelName;
			}
			params.bPatternModelUseTriangles = pStart->pEmitter->bPatternModelUseTriangles;
			params.bUseModel = pStart->pEmitter->bPatternModel;

			params.bOverrideSpecialParam = pStart->pEmitter->bOverrideSpecialParam;
			params.bLightModulation = pStart->pEmitter->bLightModulation;
			params.bColorModulation = pStart->pEmitter->bColorModulation;

			params.bNormalizeTransformTarget = pStart->pEmitter->bNormalizeTransformTarget;
			params.bNormalizeTransformTargetOtherAxes = pStart->pEmitter->bNormalizeTransformTargetOtherAxes;

			pSet = dynFxFastParticleSetCreate(&params);
			if (pSet) 
				eaPush(&pParticle->eaParticleSets, pSet);
			dynFxLog(pFx, "Starting Fast Particle Emitter %s", params.pInfo->pcName);
		}
	}
	FOR_EACH_END
}

void dynFxCallEmitterStops(DynParticle* pParticle, DynParticleEmitterRef*** peaEmitterStops, DynFx* pFx)
{
	FOR_EACH_IN_EARRAY((*peaEmitterStops), DynParticleEmitterRef, pStop)
		int iNumSets = eaSize(&pParticle->eaParticleSets);
		int iIndex;
		for (iIndex=0; iIndex<iNumSets; ++iIndex)
		{
			DynFxFastParticleSet* pSet = pParticle->eaParticleSets[iIndex];
			if (pStop->pcTag == pSet->pcEmitterName)
			{
				DynFxFastParticleInfo *pInfo = GET_REF(pSet->hInfo);
				if (pStop->pEmitter->bSoftKill)
				{
					dynFxLog(pFx, "Soft Kill on Emitter %s", SAFE_MEMBER(pInfo,pcName));
					pSet->bStopEmitting = true;
				}
				else
				{
					dynFxLog(pFx, "Hard Kill on Emitter %s", SAFE_MEMBER(pInfo,pcName));
					dynFxFastParticleSetDestroy(pSet);
					eaRemove(&pParticle->eaParticleSets, iIndex);
					--iNumSets;
					--iIndex;
				}
			}
		}
	FOR_EACH_END
}

static void dynFxCallRaycastStarts(DynEventUpdater* pUpdater, DynRaycastRef*** peaRaycastStarts, DynFx* pFx)
{
	FOR_EACH_IN_EARRAY((*peaRaycastStarts), DynRaycastRef, pStart)
	{
		DynRaycastUpdater* pRayUpdater;
		const DynParamBlock* pParamBlock;
		bool bAbort = false;

		if ( (pParamBlock = GET_REF(pFx->hParamBlock)) || (pParamBlock = &GET_REF(pFx->hInfo)->paramBlock))
		{
			FOR_EACH_IN_EARRAY(pParamBlock->eaDefineParams, DynDefineParam, pParam)
			{
				if (strnicmp(pParam->pcParamName,"BlockStart", 10)==0 && stricmp(MultiValGetString(&pParam->mvVal,NULL), pStart->pcTag)==0)
				{
					dynFxLog(pFx, "Raycast %s blocked by parameter %s", pStart->pcTag, pParam->pcParamName);
					bAbort = true;
					break;
				}
			}
			FOR_EACH_END;
		}

		if (bAbort)
			continue;

		MP_CREATE(DynRaycastUpdater, 512);
		pRayUpdater = MP_ALLOC(DynRaycastUpdater);
		pRayUpdater->pRaycastInfo = pStart->pRaycast;
		pRayUpdater->fTimeAccum = 0.0f;
		pRayUpdater->pRaycastNode = dynNodeAlloc();
		pRayUpdater->pRaycastNode->pcTag = pRayUpdater->pRaycastInfo->pcBoneName?pRayUpdater->pRaycastInfo->pcBoneName:pRayUpdater->pRaycastInfo->pcTag;
		eaPush(&pFx->eaRayCastNodes, pRayUpdater->pRaycastNode);
		eaPush(&pUpdater->eaRaycastUpdater, pRayUpdater);
		dynFxLog(pFx, "Starting RayCast %s", pStart->pcTag);
	}
	FOR_EACH_END
}

static void dynFxCallRaycastStops(DynEventUpdater* pUpdater, DynRaycastRef*** peaRaycastStops, DynFx* pFx)
{
	FOR_EACH_IN_EARRAY((*peaRaycastStops), DynRaycastRef, pStop)
		int iNumUpdaters = eaSize(&pUpdater->eaRaycastUpdater);
		int iIndex;
		for (iIndex=iNumUpdaters-1; iIndex >= 0; --iIndex)
		{
			DynRaycastUpdater* pRaycastUpdater = pUpdater->eaRaycastUpdater[iIndex];
			dynFxLog(pFx, "Stopping RayCast %s", pRaycastUpdater->pRaycastInfo->pcTag);
			eaRemove(&pUpdater->eaRaycastUpdater, iIndex);
			eaFindAndRemove(&pFx->eaRayCastNodes, pRaycastUpdater->pRaycastNode);
			RefSystem_RemoveReferent(pRaycastUpdater->pRaycastNode, true);
			dynNodeFree(pRaycastUpdater->pRaycastNode);
			MP_FREE(DynRaycastUpdater, pRaycastUpdater);
		}
	FOR_EACH_END
}

static void dynRaycastUpdate(int iPartitionIdx, DynRaycastUpdater* pUpdater, DynFx* pFx, DynEventUpdater* pEventUpdater)
{
	// Check if we need to update
	if (pUpdater->pRaycastInfo->bUpdate || !pUpdater->bFired)
	{
		// Fire a ray
		bool bHit;
		F32 fDistance = pFx->pParticle->pDraw->vScale[2] * pUpdater->pRaycastInfo->fRange * pFx->pParticle->fZScaleTo; 
		const char* pcPhysProp = NULL;
		U32 uiHit = 0; // we use this to log which hit events hit and which didn't, for later processing
		bool bFireOther = true;
		int iOtherIndex = -1;

		// Cast the actual ray
		bHit = dynFxRaycast(iPartitionIdx, pFx, &pFx->pParticle->pDraw->node, fDistance, pUpdater->pRaycastNode, pUpdater->pRaycastInfo->bOrientToNormal, pUpdater->pRaycastInfo->bUseParentRotation, pUpdater->pRaycastInfo->bCheckPhysProps?&pcPhysProp:NULL, pUpdater->pRaycastInfo->bForceDown, pUpdater->pRaycastInfo->bCopyScale, pUpdater->pRaycastInfo->eFilter);

		if (!bHit)
		{
			dynFxLog(pFx, "Raycast %s Did not Hit", pUpdater->pRaycastInfo->pcTag);
		}
		else
		{
			dynFxLog(pFx, "Raycast %s Hit", pUpdater->pRaycastInfo->pcTag);
		}

		// First loop through and set whether it hit or didn't
		FOR_EACH_IN_EARRAY(pUpdater->pRaycastInfo->eaHitEvent, DynRaycastHitEvent, pHitEvent)
		{
			if ((!!bHit) == (!pHitEvent->bMissEvent)) // counts as a hit (bFireOnMiss flips it)
			{
				// Always play if no hittype listed
				if (!pHitEvent->bFireOnce || !pUpdater->bHitOnce)
				{
					if (eaSize(&pHitEvent->eaHitTypes) == 0)
						SETB(&uiHit, ipHitEventIndex);
					else 
					{
						FOR_EACH_IN_EARRAY(pHitEvent->eaHitTypes, const char, pcHitType)
							if (pcPhysProp && pcHitType == pcPhysProp)
							{
								SETB(&uiHit, ipHitEventIndex);
								bFireOther = false; // we fired a match, so don't fire other unless otherwise triggered
							}
							else if (pcHitType == s_pcOther)
								iOtherIndex = ipHitEventIndex;
						FOR_EACH_END
					}
				}
			}
		}
		FOR_EACH_END;

		if (bFireOther && iOtherIndex >= 0) // if there is an other, and we need to fire it, make sure the bit is set
		{
			SETB(&uiHit, iOtherIndex);
		}


		// Loop through hit events, first firing stop events, then loop again and fire start events
		FOR_EACH_IN_EARRAY(pUpdater->pRaycastInfo->eaHitEvent, DynRaycastHitEvent, pHitEvent)
		{
			if ( !TSTB(&uiHit, ipHitEventIndex) )
			{
				dynFxCallEmitterStops(pFx->pParticle, &pHitEvent->eaEmitterStart, pFx);
				dynFxCallLoopEnds(pFx, &pHitEvent->eaLoopStart);
				if(sndStopFunc)
				{
					FOR_EACH_IN_EARRAY(pHitEvent->eaSoundStart, const char, pcSound)
					{
						dynSoundStop(&pEventUpdater->soundUpdater, pcSound);
					}
					FOR_EACH_END;
				}
			}
		}
		FOR_EACH_END;

		// Now start events
		FOR_EACH_IN_EARRAY(pUpdater->pRaycastInfo->eaHitEvent, DynRaycastHitEvent, pHitEvent)
		{
			if ( TSTB(&uiHit, ipHitEventIndex) )
			{
				DynFxInfo *pInfo = GET_REF(pFx->hInfo);

				dynFxCallChildDyns(pFx, &pHitEvent->childCallCollection, NULL);
				dynFxCallEmitterStarts(pFx->pParticle, &pHitEvent->eaEmitterStart, pFx->fHue, pFx->fSaturationShift, pFx->fValueShift, pUpdater->pRaycastNode, pFx->iPriorityLevel, pFx);
				dynFxCallLoopStarts(pEventUpdater, &pHitEvent->eaLoopStart, pFx);
				if(sndStartFunc && sndGuidFunc)
				{
					FOR_EACH_IN_EARRAY(pHitEvent->eaSoundStart, const char, pcSound)
					{
						dynSoundStart(pFx, &pEventUpdater->soundUpdater, pcSound);
					}
					FOR_EACH_END
				}
				dynFxSendMessages(pFx, &pHitEvent->eaMessage);
			}
		}
		FOR_EACH_END;

		if (bHit)
			pUpdater->bHitOnce = true;
		pUpdater->bFired = true;
	}
}

void dynEventUpdaterClear(DynEventUpdater* pUpdater, DynFx* pFx)
{
	dynFxLog(pFx, "Deleting Event %s", pUpdater->pEvent->pcMessageType);
	dynFxKillAllSounds(&pUpdater->soundUpdater);

	FOR_EACH_IN_EARRAY(pUpdater->eaLoopUpdater, DynLoopUpdater, pLoopUpdater)
		REMOVE_HANDLE(pLoopUpdater->hLastChildCreated);
		MP_FREE(DynLoopUpdater, pLoopUpdater);
	FOR_EACH_END
	eaDestroy(&pUpdater->eaLoopUpdater);

	FOR_EACH_IN_EARRAY(pUpdater->eaRaycastUpdater, DynRaycastUpdater, pRaycastUpdater)
		eaFindAndRemove(&pFx->eaRayCastNodes, pRaycastUpdater->pRaycastNode);
		RefSystem_RemoveReferent(pRaycastUpdater->pRaycastNode, true);
		dynNodeFree(pRaycastUpdater->pRaycastNode);
		MP_FREE(DynRaycastUpdater, pRaycastUpdater);
	FOR_EACH_END
	eaDestroy(&pUpdater->eaRaycastUpdater);

	eaDestroy(&pUpdater->eaForces);
}

U32 dynEventUpdaterHasSounds(DynEventUpdater* pUpdater)
{
	FOR_EACH_IN_EARRAY(pUpdater->pEvent->keyFrames, DynKeyFrame, pKeyFrame)
		if(eaSize(&pKeyFrame->ppcSoundStarts))
		{
			return 1;
		}
		FOR_EACH_IN_EARRAY(pKeyFrame->eaRaycastStart, DynRaycastRef, pRaycastRef)
			FOR_EACH_IN_EARRAY(pRaycastRef->pRaycast->eaHitEvent, DynRaycastHitEvent, pHitEvent)
				if(eaSize(&pHitEvent->eaSoundStart))
				{
					return 1;
				}
			FOR_EACH_END
		FOR_EACH_END
	FOR_EACH_END

	return 0;
}

static void dynEventUpdaterReset(DynEventUpdater* pUpdater, DynFx* pFx)
{
	dynFxCreatePathSetFromEvent(pUpdater->pPathSet, pUpdater->pEvent, GET_REF(pFx->hParamBlock), dynFxGetParentFxConst(pFx));
	pUpdater->bNeverUpdated = true;
	pUpdater->uiTimeSinceStart = 0;
	if (pFx->pParticle)
	{
		dynNodeClear(&pFx->pParticle->pDraw->node);
		memset(pFx->pParticle->pDraw, 0, sizeof(DynDrawParticle));
		dynDrawParticleInit(pFx->pParticle->pDraw, pUpdater->pEvent);
		dynNodeReset(&pFx->pParticle->pDraw->node);
	}
}

DynEventUpdater* dynFxCreateEventUpdater( const DynEvent* pEvent, DynFx* pFx ) 
{
	DynEventUpdater* pNewUpdater;
	const DynParamBlock* pParamBlock;

	if(!pEvent->pPathSet) {
		// Something went very very wrong.
		DynFxInfo *pInfo = GET_REF(pFx->hInfo);
		Errorf(
			"Internal FX system error in %s when creating event updater on FX %s.",
			__FUNCTION__,
			pInfo ? pInfo->pcDynName : "unknown");
		return NULL;
	}

	if (pEvent->bTriggerOnce)
	{
		U8 uiMask = 0x1 << pEvent->uiEventIndex;
		if (pFx->uiEventTriggered & uiMask)
			return NULL;
		else
			pFx->uiEventTriggered |= uiMask;
	}
	if (pFx->bHasCreatedParticle && pEvent->bCreatesParticle && !pEvent->bKillEvent)
	{
		return NULL;
	}
	pNewUpdater = calloc(sizeof(DynEventUpdater) + pEvent->pPathSet->uiTotalSize, 1);
	pParamBlock = GET_REF(pFx->hParamBlock);
	pNewUpdater->uiTimeSinceStart = 0;
	pNewUpdater->bNeverUpdated = true;
	pNewUpdater->pEvent = pEvent;
	pNewUpdater->pPathSet = (DynFxPathSet*)((char*)pNewUpdater + sizeof(DynEventUpdater));
	/*
	if (!pParamBlock)
		pParamBlock = &(pFx->pInfo->paramBlock);
		*/
	dynFxCreatePathSetFromEvent(pNewUpdater->pPathSet, pEvent, pParamBlock, dynFxGetParentFxConst(pFx));
	if (!GET_REF(pFx->hInfo)  || !GET_REF(pFx->hInfo)->bHibernate)
		pFx->bHasHadEvent = 1;
	dynFxLog(pFx, "Creating Event %s", pEvent->pcMessageType);
	return pNewUpdater;
}


static void dynDrawParticleInit(DynDrawParticle* pDraw, const DynEvent* pEvent)
{
	if (!pEvent->bScaleChanges)
		copyVec3(onevec3, pDraw->vScale);
	if (!pEvent->bColorChanges)
	{
#ifdef DYN_FX_NORMALIZED_COLORS
		setVec3same(pDraw->vColor, 1.0f);
#else
		setVec3same(pDraw->vColor, 255.0f);
#endif
	}
	if (!pEvent->bAlphaChanges)
	{
#ifdef DYN_FX_NORMALIZED_COLORS
		pDraw->vColor[3] = 1.0f;
#else
		pDraw->vColor[3] = 255.0f;
#endif
	}

	pDraw->modelTracker.fade_out_lod = pDraw->modelTracker.fade_in_lod = -1;
}

DynParticle* dynParticleCreate(const DynEventUpdater* pUpdater, F32 fHueShift, F32 fSaturationShift, F32 fValueShift, DynFxSortBucket* pSortBucket, bool bDontDraw, bool bLowRes, const DynEvent **eaAllEvents)
{
	DynParticle* pNewParticle = NULL;
	DynParticleHeader tempHeader[64] = {0};
	U32 uiTokenIndex = uiFirstDynObjectStaticToken;
	U32 uiNumEntries = 0;
	U32 uiDataSize = sizeof(DynDrawParticle);
	const DynEvent* pCurEvent = NULL;
	DynKeyFrame* pFirstKeyFrame = NULL;
	int i;

	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		U32 uiBitFieldIndex = uiTokenIndex - uiFirstDynObjectStaticToken;
		bool bFound = false;

		for(i = 0; i < eaSize(&eaAllEvents); i++) {
			pCurEvent = eaAllEvents[i];
			if(pCurEvent && pCurEvent->keyFrames) {
				pFirstKeyFrame = pCurEvent->keyFrames[0];

				if(TSTB(pCurEvent->bfAnyChanges.bf, uiBitFieldIndex)
					|| TSTB(pFirstKeyFrame->bfAnyChanges.bf, uiBitFieldIndex)
					) {

					bFound = true;
					break;
				}
			}
		}

		if(bFound) {

			// Needs an entry
			DynParticleHeader* pEntry = &tempHeader[uiNumEntries++];
			U32 uiDynDrawParticleTokenIndex = puiDynObjectToDynDrawParticleTokenMap[uiTokenIndex];
			if ( uiDynDrawParticleTokenIndex )
			{
				assert(uiDynDrawParticleTokenIndex < ARRAY_SIZE(ParseDynDrawParticle));
				pEntry->uiOffset = (U16)ParseDynDrawParticle[uiDynDrawParticleTokenIndex].storeoffset;
				pEntry->uiTokenIndex = uiTokenIndex;
				pEntry->uiSize = dynFxSizeOfToken(uiTokenIndex, ParseDynObject);
			}
			else
			{
				pEntry->uiOffset = uiDataSize;
				pEntry->uiTokenIndex = uiTokenIndex;
				pEntry->uiSize = dynFxSizeOfToken(uiTokenIndex, ParseDynObject);
				uiDataSize += pEntry->uiSize;
#if PLATFORM_CONSOLE
				// Add 4 byte alignment for whiny xbox processor
				if (pEntry->uiSize & 0x3)
				{
					uiDataSize += ( 4 - pEntry->uiSize & 0x3 );
				}
#endif
			}
		}
		++uiTokenIndex;
	}

	pFirstKeyFrame = pUpdater->pEvent->keyFrames[0];

	// Now we have our info, create the real deal, copy the temp stuff over.
	// Note that we are allocating it all at once so it's one nice big block of cheese
	pNewParticle = calloc(sizeof(DynParticle), 1);
	pNewParticle->pData = aligned_calloc(sizeof(DynParticleHeader)*uiNumEntries + uiDataSize, 1, 16);
	pNewParticle->uiNumEntries = uiNumEntries;
	pNewParticle->uiDataSize = uiDataSize;
	pNewParticle->fZScaleTo = 1.0f;
	pNewParticle->pEntries = (DynParticleHeader*)(pNewParticle->pData + uiDataSize);
	pNewParticle->fFadeOut = 1.0f;
	pNewParticle->bMultiColor = pUpdater->pEvent->bMultiColor;
	//pNewParticle->pSortBucket = dynSortBucketIncRefCount(pSortBucket);
	dynDrawParticleInit(pNewParticle->pDraw, pUpdater->pEvent);
	dynNodeReset(&pNewParticle->pDraw->node);
	if (eaSize(&pUpdater->pEvent->eaMatConstRename) > 0)
		pNewParticle->peaMNCRename = (DynMNCRename***)(&pUpdater->pEvent->eaMatConstRename);

	memcpy(pNewParticle->pEntries, tempHeader, sizeof(DynParticleHeader) * uiNumEntries);

	// Now, loop through the header entries and init the data to the first frame info
	{
		U32 uiHeaderIndex;
		for (uiHeaderIndex=0; uiHeaderIndex<pNewParticle->uiNumEntries; ++uiHeaderIndex)
		{
			DynParticleHeader* pEntry = &pNewParticle->pEntries[uiHeaderIndex];
			void* pSrc;
			StructTypeField entry_type = ParseDynObject[pEntry->uiTokenIndex].type;
			if (pEntry->uiTokenIndex >= uiFirstDynObjectDynamicToken)
				break;
			if ((entry_type & (TOK_F32_X | TOK_EARRAY)) != (TOK_F32_X | TOK_EARRAY))
				pSrc = TokenStoreGetPointer(ParseDynObject, pEntry->uiTokenIndex, &pFirstKeyFrame->objInfo[edoValue].obj, 0, NULL);
			else
				pSrc = TokenStoreGetEArrayF32(ParseDynObject, pEntry->uiTokenIndex, &pFirstKeyFrame->objInfo[edoValue].obj, NULL);

			if ( ( entry_type & TOK_STRING_X ) == TOK_STRING_X )
				memcpy(pNewParticle->pData + pEntry->uiOffset, &pSrc, pEntry->uiSize);
			else
				memcpy(pNewParticle->pData + pEntry->uiOffset, pSrc, pEntry->uiSize);
		};
	}

	pNewParticle->pDraw->fSaturationShift = fSaturationShift;
	pNewParticle->pDraw->fValueShift = fValueShift;
	pNewParticle->pDraw->fHueShift = fHueShift;
	pNewParticle->bLowRes = bLowRes;
	return pNewParticle;
}

const char* dynParticleGetTextureName( DynParticle* pParticle )
{
	return pParticle->pDraw->pcTextureName;
}

const char* dynParticleGetMaterialName( DynParticle* pParticle )
{
	return pParticle->pDraw->pcMaterialName;
}

static void dynFxSetUpdateMask(DynFxDynamicPath* pPath) 
{
	{
		DynFxDynamicPathPoint* pCurrentPathPoint;
		U8 uiWhichFloat;

		switch (pPath->uiEDO)
		{
			xcase edoValue:
		{
			pPath->uiUpdateMask = 0;
			for (uiWhichFloat = 0; uiWhichFloat < pPath->uiFloatsPer; ++uiWhichFloat )
			{
				pCurrentPathPoint = &pPath->pPathPoints[pPath->uiCurrentPathPointIndex + uiWhichFloat];
				if ( pCurrentPathPoint->uiKeyFrameIndex == 0 || pCurrentPathPoint->fDiffV != 0.0f )
					pPath->uiUpdateMask |= (1 << uiWhichFloat);
			}
		}
		xcase edoRate:
		case edoAmp:
			{

				pPath->uiUpdateMask = 0;
				for (uiWhichFloat = 0; uiWhichFloat < pPath->uiFloatsPer; ++uiWhichFloat )
				{
					pCurrentPathPoint = &pPath->pPathPoints[pPath->uiCurrentPathPointIndex + uiWhichFloat];
					if ( pCurrentPathPoint->fStartV != 0.0f || pCurrentPathPoint->fDiffV != 0.0f )
						pPath->uiUpdateMask |= (1 << uiWhichFloat);
				}
			}
		};
	}
}


static void dynFxUpdatePathSetCurrentKeyFrames( DynFxPathSet* pPathSet, DynParticle* pParticle, U32 uiBeforeKeyFrame, DynFx* pFx)
{
	// Walk through path set
	U32 uiStaticIndex, uiDynamicIndex;
	U32 uiCurrentEntryIndex = 0;
	for (uiStaticIndex=0; uiStaticIndex<pPathSet->uiNumStaticPaths; ++uiStaticIndex)
	{
		// Here's where we actually set the current static infos
		DynFxStaticPath* pPath = &pPathSet->pStaticPaths[uiStaticIndex];
		U32 uiPathPointIndex = pPath->uiCurrentPathPointIndex;
		U32 uiTokenIndex = pPath->uiTokenIndex;
		while ( pPath->uiCurrentPathPointIndex < pPath->uiNumPathPoints && pPath->pPathPoints[pPath->uiCurrentPathPointIndex].uiKeyFrameIndex < uiBeforeKeyFrame)
			++pPath->uiCurrentPathPointIndex;
		if ( pPath->uiCurrentPathPointIndex == pPath->uiNumPathPoints || pPath->pPathPoints[pPath->uiCurrentPathPointIndex].uiKeyFrameIndex > uiBeforeKeyFrame )
			continue;
		// We're on the keyframe, copy the result over
		if ( pParticle )
		{
			while ( pParticle->pEntries[uiCurrentEntryIndex].uiTokenIndex < uiTokenIndex )
			{
				if ( ++uiCurrentEntryIndex >= pParticle->uiNumEntries )
				{
					return;
				}
			}
			{
				DynParticleHeader* pEntry = &pParticle->pEntries[uiCurrentEntryIndex];
				if ( pEntry->uiTokenIndex == uiTokenIndex )
				{
					void* pDst = (void*)(pParticle->pData + pEntry->uiOffset);
					memcpy(pDst, &(pPath->pPathPoints[pPath->uiCurrentPathPointIndex].data), pPath->uiDataSize);
					if ( uiTokenIndex == uiTextureTokenIndex )
						pParticle->pDraw->pTexture = NULL;
					else if ( uiTokenIndex == uiTexture2TokenIndex )
						pParticle->pDraw->pTexture2 = NULL;
					else if ( uiTokenIndex == uiMaterialTokenIndex )
						pParticle->pDraw->pMaterial = pParticle->pDraw->pcMaterialName?materialFindNoDefault(pParticle->pDraw->pcMaterialName, WL_FOR_FX):NULL;
					else if ( uiTokenIndex == uiGeoDissolveMaterialTokenIndex )
						pParticle->pDraw->pGeoDissolveMaterial = pParticle->pDraw->pcGeoDissolveMaterialName?materialFindNoDefault(pParticle->pDraw->pcGeoDissolveMaterialName, WL_FOR_FX):NULL;
					else if ( uiTokenIndex == uiGeoAddMaterialsTokenIndex ) {

						int i;
						for(i = 0; i < eaSize(&pParticle->pDraw->ppcGeoAddMaterialNames); i++) {
							eaPush(&pParticle->pDraw->eaGeoAddMaterials, materialFindNoDefault(pParticle->pDraw->ppcGeoAddMaterialNames[i], WL_FOR_FX));
						}

					} else if ( uiTokenIndex == uiMaterial2TokenIndex )
						pParticle->pDraw->pMaterial2 = pParticle->pDraw->pcMaterial2Name?materialFindNoDefault(pParticle->pDraw->pcMaterial2Name, WL_FOR_FX):NULL;
					else if ( uiTokenIndex == uiGeometryTokenIndex )
						pParticle->pDraw->pModel = NULL;
					else if ( uiTokenIndex == uiFlareTypeTokenIndex )
					{
						DynFlare * pFlare = pParticle->pDraw->pDynFlare;
						if (!pFlare)
						{
							pFlare = calloc(sizeof(DynFlare), 1);
							pParticle->pDraw->pDynFlare = pFlare;
						}
					}
					else if ( uiTokenIndex == uiLightTypeTokenIndex )
					{
						LightType eLightType = *((LightType*)pDst);
						if ( pParticle->pDraw->pDynLight )
						{
							pParticle->pDraw->pDynLight->eLightType = eLightType;
						}
						else if (eLightType != WL_LIGHT_NONE )
						{
							pParticle->pDraw->pDynLight = calloc(sizeof(DynLight), 1);
							pParticle->pDraw->pDynLight->eLightType = eLightType;
							pParticle->pDraw->pDynLight->fInnerRadiusPercentage = 0.1f;
						}
					}
					else if ( uiTokenIndex == uiMeshTrailTypeTokenIndex )
					{
						DynMeshTrailMode eMode = *((DynMeshTrailMode*)pDst);
						if ( pParticle->pDraw->pMeshTrail )
							pParticle->pDraw->pMeshTrail->meshTrailInfo.mode = eMode;
						else if (eMode != DynMeshTrail_None )
						{
							pParticle->pDraw->pMeshTrail = calloc(sizeof(DynMeshTrail), 1);
							pParticle->pDraw->pMeshTrail->meshTrailInfo.mode = eMode;
						}
					}
					else if ( uiTokenIndex == uiSplatTypeTokenIndex )
					{
						if (!pParticle->pDraw->pSplat)
							pParticle->pDraw->pSplat = calloc(sizeof(DynSplat), 1);
					}
					else if ( uiTokenIndex == uiSkyNameTokenIndex )
					{
						if (!pParticle->pDraw->pSkyVolume)
							pParticle->pDraw->pSkyVolume = calloc(sizeof(DynSkyVolume), 1);
					}
					else if ( uiTokenIndex == uiAttachCameraTokenIndex || 
							  uiTokenIndex == uiCameraLookAtNodeTokenIndex)
					{
						if (!pParticle->pDraw->pCameraInfo) {
							pParticle->pDraw->pCameraInfo = calloc(sizeof(DynCameraInfo), 1);
							pParticle->pDraw->pCameraInfo->fShakeSpeed = 1;
							pParticle->pDraw->pCameraInfo->fCameraInfluence = 1;
							pParticle->pDraw->pCameraInfo->fCameraDelaySpeed = -1.f;
						}
					}
				}
			}
		}
		++pPath->uiCurrentPathPointIndex;
	}
	for (uiDynamicIndex=0; uiDynamicIndex<pPathSet->uiNumDynamicPaths; ++uiDynamicIndex)
	{
		DynFxDynamicPath* pPath = &pPathSet->pDynamicPaths[uiDynamicIndex];
		U32 uiPathPointIndex = pPath->uiCurrentPathPointIndex;
		while (pPath->uiCurrentPathPointIndex+pPath->uiFloatsPer < pPath->uiNumPathPoints && pPath->pPathPoints[pPath->uiCurrentPathPointIndex+pPath->uiFloatsPer].uiKeyFrameIndex <= uiBeforeKeyFrame )
		{
			pPath->uiCurrentPathPointIndex += pPath->uiFloatsPer;
			pPath->uiKeyTime = 0;
		}
		if(pParticle) {
			if( pPath->uiTokenIndex == uiShakePowerTokenIndex ||
				pPath->uiTokenIndex == uiShakeSpeedTokenIndex ||
				pPath->uiTokenIndex == uiCameraFOVTokenIndex ||
				pPath->uiTokenIndex == uiCameraInfluenceTokenIndex ||
				pPath->uiTokenIndex == uiCameraDelaySpeedTokenIndex) {

				if (!pParticle->pDraw->pCameraInfo) {
					pParticle->pDraw->pCameraInfo = calloc(sizeof(DynCameraInfo), 1);
					pParticle->pDraw->pCameraInfo->fShakeSpeed = 1;
					pParticle->pDraw->pCameraInfo->fCameraInfluence = 1;
					pParticle->pDraw->pCameraInfo->fCameraDelaySpeed = -1.f;
				}

			} else if (
				pPath->uiTokenIndex == uiTimeScaleTokenIndex ||
				pPath->uiTokenIndex == uiTimeScaleChildrenTokenIndex ) {

				if (!pParticle->pDraw->pControlInfo) {
					pParticle->pDraw->pControlInfo = calloc(sizeof(DynFxControlInfo), 1);
					pParticle->pDraw->pControlInfo->fTimeScale = 1;
				}
			}
		}


		dynFxSetUpdateMask(pPath);


	}
}

static bool dynParticleIntegrate(SA_PARAM_NN_VALID DynParticle* pParticle, DynFxTime uiDeltaTime, F32 fFrac)
{
	F32 fDeltaTime;
	Vec3 vOldPos;
	pParticle->pDraw->node.uiDirtyBits = 1;
	dynNodeGetWorldSpacePos(&pParticle->pDraw->node, vOldPos);
	// Integrate rotations and velocity
	fDeltaTime = FLOATTIME(uiDeltaTime);

	if ( pParticle->pDraw->pDPO )
	{
		if(!dynFxPhysicsUpdate(pParticle, fDeltaTime)) {
			return false;
		}
	}
	else
	{
		Vec3 vAxis;
		F32 fAngle;
		Quat qDelta, qTemp;
		Vec3 vDelta;
		CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
		if ( !vec3IsZero(pParticle->pDraw->vVelocity ) )
		{
			Vec3 vPos;
			const F32 *vLocalPos;
			if ( pParticle->pDraw->fDrag != 0.0f )
			{
				F32 fDragAmount = CLAMP(1.0f - (pParticle->pDraw->fDrag * fDeltaTime), 0.0f, 1.0f);
				CHECK_FINITE(fDragAmount);
				scaleVec3(pParticle->pDraw->vVelocity, fDragAmount, pParticle->pDraw->vVelocity);
			}
			scaleVec3(pParticle->pDraw->vVelocity, fDeltaTime, vDelta);
			CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
			if (pParticle->pDraw->bLocalOrientation && !(pParticle->pDraw->node.uiTransformFlags & ednLocalRot))
			{
				Vec3 vTemp;
				Quat qPartRot;
				dynNodeGetWorldSpaceRot(&pParticle->pDraw->node, qPartRot);
				quatRotateVec3(qPartRot, vDelta, vTemp);
				copyVec3(vTemp, vDelta);
			}
			vLocalPos = dynNodeGetLocalPosRefInline(&pParticle->pDraw->node);
			addVec3( vLocalPos, vDelta, vPos );
			pParticle->fGravityVel -= pParticle->pDraw->fGravity * fDeltaTime;
			vPos[1] += pParticle->fGravityVel * fDeltaTime;

			if(CHECK_DYNPOS_NONFATAL(vPos)) {
				dynNodeSetPos(&pParticle->pDraw->node, vPos);
			} else {
				return false;
			}
		}

		if ( !vec3IsZero(pParticle->pDraw->vSpin ) )
		{
			const F32 *qLocalRot;
			scaleVec3(pParticle->pDraw->vSpin,0.01745329f, vAxis); // don't just copy, we need it in RAD not DEG
			fAngle = normalVec3(vAxis) * fFrac;
			axisAngleToQuat(vAxis, fAngle * fDeltaTime, qDelta);
			qLocalRot = dynNodeGetLocalRotRefInline(&pParticle->pDraw->node);
			quatMultiply(qLocalRot, qDelta, qTemp);
			dynNodeSetRot(&pParticle->pDraw->node, qTemp);
		}
		if ( pParticle->pDraw->fSpriteSpin )
			pParticle->pDraw->fSpriteOrientation += pParticle->pDraw->fSpriteSpin * fDeltaTime;
	}

	if(pParticle->pDraw->bVelocityDriveOrientation && !vec3IsZero(pParticle->vWorldSpaceVelocity)) {
		Vec3 vStart = { 0, 0, 0 };
		Quat qRot;
		quatLookAt(vStart, pParticle->vWorldSpaceVelocity, qRot);
		dynNodeSetRot(&pParticle->pDraw->node, qRot);
	}

	// Do scale
	{
		Vec3 vScale;
		Vec3 vCurrentScale;

		copyVec3(pParticle->pDraw->vScale, vScale);
		vScale[2] *= pParticle->fZScaleTo;

		dynNodeGetLocalScaleInline(&pParticle->pDraw->node, vCurrentScale);
		dynNodeSetScaleInline(&pParticle->pDraw->node, vScale);

		if(!sameVec3(vScale, vCurrentScale)) {
			dynNodeSetDirtyInline(&pParticle->pDraw->node);
		}
	}

	return true;
}

// Sigmoid function, but moved into a 0-1 domain and range.
static float sigmoid(float x) {
	return 1.0 - 1.0/(1.0+pow(2.71828, (x*2 - 1) * 6));
}

// Inverse of the same sigmoid.
static float unsigmoid(float x) {
	
	float r = 0;
	
	// Clamp all values to something sane.
	if(x <= 0) {
		r = 0;
	} else if(x >= 1) {
		r = 1;
	} else {
		r = (log(-x / (x-1)) + 6.0) / 12.0;
	}

	// Clamp the output, too.
	if(r < 0) {
		r = 0;
	} else if(r >= 1) {
		r = 1;
	}

	return r;
}

static float firsthalfSigmoid(float x) {
	return sigmoid(x * 0.5) * 2.0;
}

static float unfirsthalfSigmoid(float x) {
	return unsigmoid(x * 0.5) * 2.0;
}

static float secondhalfSigmoid(float x) {
	return (sigmoid(x * 0.5 + 0.5) - 0.5) * 2.0;
}

static float unsecondhalfSigmoid(float x) {
	return (unsigmoid(x * 0.5 + 0.5) - 0.5) * 2.0;
}

static void dynParticleUpdate(DynParticle* pParticle, DynFx* pFx, DynFxPathSet* pPathSet, DynFxTime uiDeltaTime, bool bPassedKeyFrame)
{
	// Walk through path set, transfer info to particle
	U32 uiDynamicIndex;
	U32 uiTokenIndex = 0;
	U32 uiCurrentEntryIndex = 0;

	CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);

	for (uiDynamicIndex=0; uiDynamicIndex<pPathSet->uiNumDynamicPaths; ++uiDynamicIndex)
	{
		DynFxDynamicPath* pPath = &pPathSet->pDynamicPaths[uiDynamicIndex];
		if ( !pPath->uiUpdateMask )
			continue;
		CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
		uiTokenIndex = pPath->uiTokenIndex;
		while ( pParticle->pEntries[uiCurrentEntryIndex].uiTokenIndex < uiTokenIndex )
		{
			if ( ++uiCurrentEntryIndex >= pParticle->uiNumEntries )
			{
				return;
			}
		}
		CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
		if ( pParticle->pEntries[uiCurrentEntryIndex].uiTokenIndex == uiTokenIndex )
		{
			F32 fStartTime;
			F32 fDeltaTime;
			U32 uiWhichFloat;
			DynParticleHeader* pEntry = &pParticle->pEntries[uiCurrentEntryIndex];

			fStartTime = FLOATTIME(pPath->uiKeyTime);
			pPath->uiKeyTime += uiDeltaTime;
			fDeltaTime = FLOATTIME(uiDeltaTime);

			CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
			if ( (ParseDynObject[uiTokenIndex].type & TOK_TYPE_MASK) != TOK_QUATPYR_X )
			{
				for (uiWhichFloat = 0; uiWhichFloat<pPath->uiFloatsPer; ++uiWhichFloat)
				{
					U32 uiPointIndex = pPath->uiCurrentPathPointIndex + uiWhichFloat;
					DynFxDynamicPathPoint* pPoint = &pPath->pPathPoints[uiPointIndex];
					DynFxDynamicPathPoint* pNextPoint = (uiPointIndex+pPath->uiFloatsPer < pPath->uiNumPathPoints)?&pPath->pPathPoints[uiPointIndex+pPath->uiFloatsPer]:NULL;
					
					F32* pfResult;
					if (!( pPath->uiUpdateMask & (1 << uiWhichFloat) ))
						continue;

					CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
					pfResult = (F32*)(pParticle->pData + pEntry->uiOffset + sizeof(F32) * uiWhichFloat);

					/*
					if ( !pPath->uiHasValuePath )
						*pfResult = 0.0f;
					*/
					switch (pPath->uiEDO)
					{
						xcase edoValue:
						{
							
							if ( fStartTime == 0.0f && pPoint->uiKeyFrameIndex == 0 )
								*pfResult += pPoint->fStartV;
							
							if ( pPoint->uiInterpType == ediLinear ) {
								*pfResult += pPoint->fDiffV * fDeltaTime;
							} else if ( pPoint->uiInterpType == ediEaseInAndOut || pPoint->uiInterpType == ediEaseOut || pPoint->uiInterpType == ediEaseIn) {

								if(pNextPoint) {

									float diff = (pNextPoint->fStartV - pPoint->fStartV);

									if(diff) {

										// Get a value from 0 to 1 indicating the point between the
										// keyframes we're interpolating between. (y)
										float y = (*pfResult - pPoint->fStartV) / diff;

										float (*func)(float) = NULL;
										float (*inverseFunc)(float) = NULL;
										float x;
										float y2;

										// Pick a function based on the interpolation type. (Or more
										// precisely: Pick a slice of the function to use.)
										switch(pPoint->uiInterpType) {

											case ediEaseOut:
												func = firsthalfSigmoid;
												inverseFunc = unfirsthalfSigmoid;
												break;

											case ediEaseIn:
												func = secondhalfSigmoid;
												inverseFunc = unsecondhalfSigmoid;
												break;

											case ediEaseInAndOut:
												func = sigmoid;
												inverseFunc = unsigmoid;
												break;
										}

										// Get the "real" x value from the point the function is already at.
										x = inverseFunc(y);

										// Add the delta, converted into something appropriate for the
										// 0-1 range of the chosen function.
										x += fDeltaTime / (fabs(diff) / fabs(pPoint->fDiffV));

										// Go back to the function value for the given point.
										y2 = func(x);

										// Plug y2 back into *pfResult, the same way we got y out of it.
										*pfResult = y2 * diff + pPoint->fStartV;

									} else {
										*pfResult += pPoint->fDiffV;
									}
								}
							} else if (bPassedKeyFrame) {
								*pfResult += pPoint->fDiffV;
							}
						}
						xcase edoRate:
						{
							*pfResult += pPoint->fStartV * fDeltaTime + pPoint->fDiffV * fDeltaTime * fDeltaTime * 0.5f;
						}
						xcase edoAmp:
						{
							// We know at this point that the next path point is freq
							DynFxDynamicPath* pFreqPath = &pPathSet->pDynamicPaths[uiDynamicIndex+1];
							DynFxDynamicPathPoint* pFreqPoint = &pFreqPath->pPathPoints[pFreqPath->uiCurrentPathPointIndex + uiWhichFloat];
							F32 fCycleOffset = 0.0f;
							if ((uiDynamicIndex+2) < pPathSet->uiNumDynamicPaths
								&& pPathSet->pDynamicPaths[uiDynamicIndex+2].uiEDO == edoCycleOffset && pPathSet->pDynamicPaths[uiDynamicIndex+2].uiTokenIndex == pPathSet->pDynamicPaths[uiDynamicIndex].uiTokenIndex)
							{
								DynFxDynamicPath* pCycleOffsetPath = &pPathSet->pDynamicPaths[uiDynamicIndex+2];
								DynFxDynamicPathPoint* pCycleOffsetPoint = &pCycleOffsetPath->pPathPoints[pCycleOffsetPath->uiCurrentPathPointIndex + uiWhichFloat];
								fCycleOffset = pCycleOffsetPoint->fStartV;
							}
							if ( pFreqPoint->fStartV != 0 )
							{
								F32 fEndTime = fStartTime + fDeltaTime;
								F32 fSinDiff = (pPoint->fDiffV * fEndTime + pPoint->fStartV) * sinf(TWOPI*(0.5f * pFreqPoint->fDiffV * fEndTime * fEndTime + pFreqPoint->fStartV * fEndTime + fCycleOffset))
									- (pPoint->fDiffV * fStartTime + pPoint->fStartV) * sinf(TWOPI*(0.5f * pFreqPoint->fDiffV * fStartTime * fStartTime + pFreqPoint->fStartV * fStartTime + fCycleOffset	));
								*pfResult += fSinDiff;
							}
						}
						xdefault:
						{
						}
					}
					CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
				}
			}
			else
			{
				DynFxDynamicPathPoint* pPoint = &pPath->pPathPoints[pPath->uiCurrentPathPointIndex];
				F32* pfResult = (F32*)(pParticle->pData + pEntry->uiOffset);
				Quat qStartV, qDiffV, qIntegral;
				CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
				for (uiWhichFloat = 0; uiWhichFloat<pPath->uiFloatsPer; ++uiWhichFloat)
				{
					qStartV[uiWhichFloat] = pPoint[uiWhichFloat].fStartV;
					qDiffV[uiWhichFloat] = pPoint[uiWhichFloat].fDiffV;
				}
				if ( pPath->uiEDO == edoValue && fStartTime == 0.0f && pPoint->uiKeyFrameIndex == 0 )
				{
					Quat qTemp;
					quatMultiply( qStartV, pfResult, qTemp);
					copyQuat(qTemp, pfResult);
				}


				CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
				if (qDiffV[3] > 0.0 && axisAngleToQuat(qDiffV, qDiffV[3] * fDeltaTime, qIntegral) )
				{
					Quat qTemp;
					// axisAngleToQuat returns true only if it's a valid axis/angle, otherwise just copy
					quatMultiply(qIntegral, pfResult, qTemp);
					copyQuat(qTemp, pfResult);
				}
				CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);
			}
		}
	}

	CHECK_FINITEVEC3(pParticle->pDraw->vVelocity);

	if(!dynParticleIntegrate(
		   pParticle, uiDeltaTime,
		   eaSize(&pFx->eaDynEventUpdaters) ? (1.0 / (F32)eaSize(&pFx->eaDynEventUpdaters)) : 1)) {

		DynFxInfo *pInfo = GET_REF(pFx->hInfo);
		const char *fxName = "<unknown>";
		if(pInfo) {
			fxName = pInfo->pcDynName;
		}

		Errorf("dynParticleIntegrate failed for FX %s. Check acceleration and velocity for this FX and any parent FX.", fxName);
		pFx->bKill = true;
	}

	if (pParticle->pDraw->fParentVelocityOffset)
	{
		DynFx* pParentFX = GET_REF(pFx->hParentFx);
		if (pParentFX)
		{
			DynParticle* pParentParticle = dynFxGetParticle(pParentFX);
			if (pParentParticle)
			{
				Vec3 vNewPos;
				scaleVec3(pParentParticle->vWorldSpaceVelocity, pParticle->pDraw->fParentVelocityOffset, vNewPos);

				// If the length of this is over ten thousand, I'm just going to assume that the FX was attached to the
				// camera and it had a moment of non-contiguous movement. That, or some artist set a
				// ParentVelocityOffset that was truly ridiculous.
				if(lengthVec3(vNewPos) < 10000.0) {
					dynNodeSetPos(&pParticle->pDraw->node, vNewPos);
				}
			}
		}
	}

	if(eaSize(&pFx->eaAltPivs)) {
		dynNodeSetDirtyInline(&pParticle->pDraw->node);
	}

}

void dynParticleCopyToDynFlare( DynParticle* pParticle, DynFlare* pFlare)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynFlareTokenIndex = puiDynObjectToDynFlareTokenMap[pEntry->uiTokenIndex];
		if (uiDynFlareTokenIndex)
		{
			assert(uiDynFlareTokenIndex < ARRAY_SIZE(ParseDynFlare));
			memcpy((char*)pFlare + ParseDynFlare[uiDynFlareTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}
}

void dynParticleCopyToDynLight( DynParticle* pParticle, DynLight* pLight)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynLightTokenIndex = puiDynObjectToDynLightTokenMap[pEntry->uiTokenIndex];
		if (uiDynLightTokenIndex)
		{
			assert(uiDynLightTokenIndex < ARRAY_SIZE(ParseDynLight));
			memcpy((char*)pLight + ParseDynLight[uiDynLightTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}
}

void dynParticleCopyToDynCameraInfo( DynParticle* pParticle, DynCameraInfo* pCameraInfo)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynCameraInfoTokenIndex = puiDynObjectToDynCameraInfoTokenMap[pEntry->uiTokenIndex];
		if (uiDynCameraInfoTokenIndex)
		{
			assert(uiDynCameraInfoTokenIndex < ARRAY_SIZE(ParseDynCameraInfo));
			memcpy((char*)pCameraInfo + ParseDynCameraInfo[uiDynCameraInfoTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}
}

void dynParticleCopyToDynFxControlInfo( DynParticle* pParticle, DynFxControlInfo* pControlInfo)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynControlInfoTokenIndex = puiDynObjectToDynFxControlInfoTokenMap[pEntry->uiTokenIndex];
		if (uiDynControlInfoTokenIndex)
		{
			assert(uiDynControlInfoTokenIndex < ARRAY_SIZE(ParseDynCameraInfo));
			memcpy((char*)pControlInfo + ParseDynCameraInfo[uiDynControlInfoTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}
}

void dynParticleCopyToDynSplat( DynParticle* pParticle, DynSplat* pSplat)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynSplatTokenIndex = puiDynObjectToDynSplatTokenMap[pEntry->uiTokenIndex];
		if (uiDynSplatTokenIndex)
		{
			assert(uiDynSplatTokenIndex < ARRAY_SIZE(ParseDynSplat));
			memcpy((char*)pSplat + ParseDynSplat[uiDynSplatTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}
}

void dynParticleCopyToDynSkyVolume( DynParticle* pParticle, DynSkyVolume* pSkyVolume)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynSkyVolumeTokenIndex = puiDynObjectToDynSkyVolumeTokenMap[pEntry->uiTokenIndex];
		if (uiDynSkyVolumeTokenIndex)
		{
			assert(uiDynSkyVolumeTokenIndex < ARRAY_SIZE(ParseDynSkyVolume));
			memcpy((char*)pSkyVolume + ParseDynSkyVolume[uiDynSkyVolumeTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}
}

void dynParticleCopyToDynMeshTrail( DynParticle* pParticle, DynMeshTrail* pMeshTrail)
{
	U32 uiEntryIndex;
	for (uiEntryIndex=0; uiEntryIndex<pParticle->uiNumEntries; ++uiEntryIndex)
	{
		DynParticleHeader* pEntry = &pParticle->pEntries[uiEntryIndex];
		U32 uiDynMeshTrailTokenIndex = puiDynObjectToDynMeshTrailInfoTokenMap[pEntry->uiTokenIndex];
		if (uiDynMeshTrailTokenIndex)
		{
			assert(uiDynMeshTrailTokenIndex < ARRAY_SIZE(ParseDynMeshTrailInfo));
			memcpy((char*)(&pMeshTrail->meshTrailInfo) + ParseDynMeshTrailInfo[uiDynMeshTrailTokenIndex].storeoffset, pParticle->pData + pEntry->uiOffset, pEntry->uiSize);
		}
	}

	if (pMeshTrail->meshTrailInfo.fEmitDistance <= 0.0f && pMeshTrail->meshTrailInfo.fEmitRate <= 0.0f)
		pMeshTrail->meshTrailInfo.fEmitRate = 60.0f;
}

#include "dynFxParticle_h_ast.c"
