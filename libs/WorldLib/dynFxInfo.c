
#include "rand.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "quat.h"
#include "tokenstore.h"
#include "timing.h"
#include "StringCache.h"
#include "StructPack.h"
#include "UnitSpec.h"

#include "wlPhysicalProperties.h"
#include "WorldCellEntry.h"
#include "wlState.h"

#include "dynFx.h"
#include "dynFxInfo.h"
#include "dynFxParticle.h"
#include "dynFxPhysics.h"
#include "dynFxFastParticle.h"
#include "dynFxDamage.h"

#include "dynFxEnums_h_ast.h"

#include "dynFxInfo_h_ast.h"
#include "dynFxInfo_c_ast.h"

#include "WorldLibEnums_h_ast.h"
#include "dynFxPhysics_h_ast.h"
#include "dynFxManager_h_ast.h"
#include "cmdparse.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

static int iNumFxInfoUnpacked = 0;

#define UNINIT_VALUE -1023

// 06/19/2009
#define DONTAUTOKILL_DEPRECATION_DATE 1245369600 
// 09/29/2009
#define SORTASBLOCK_DEPRECATION_DATE 1254182400
// 11/01/2009
#define GROUPTEXTURES_DEPRECATION_DATE 1257033600

#define VALIDATE_PARSEDYNOBJECT_TOKEN_INDEX(a) assert(a < ARRAY_SIZE(ParseDynObject))

#if 0
// JE: Added these macros to track down something going to non-finite
#define CHECK_FINITE(f) assert(FINITE(f))
#else
#define CHECK_FINITE(f)
#endif

int bDynListAllUnusedTexturesAndGeometry;
StashTable stUsedTextures;
StashTable stUsedGeometry;

AUTO_CMD_INT(bDynListAllUnusedTexturesAndGeometry, dfxListUnused) ACMD_CMDLINE;

extern bool gbIgnoreTextureErrors;

bool g_dynFxExtraAssertsEnabled = false;
#define dynFxExtraAssert(exp, msg) (g_dynFxExtraAssertsEnabled ? assertmsg(exp, msg) : 0)

AUTO_CMD_INT(g_dynFxExtraAssertsEnabled, dfxEnableExtraAsserts) ACMD_CMDLINE;


static void dynFxMarkGeometryAsUsed(const char* pcGeometry);
static void dynFxPrintUnusedTexturesAndGeometry(void);

static U32 dynTokenIndexFromString( SA_PARAM_NN_STR const char* pcString);
static U32 dynTokenIndexFromStringGeneral(const char* pcString, const ParseTable* pParseTable);
const char*		MultiValTypeToString(MultiValType t);			// Returns 4 byte (3 char + NULL) representation of type

static U32 dynFxFlagsFromInfo(const DynFxInfo* pInfo);

const char* pcKillMessage;
const char* pcStartMessage;
const char* pcMovedTooFarMessage;

AUTO_RUN;
void dynFxInfo_InitStrings(void)
{
	pcKillMessage = allocAddStaticString("Kill");
	pcStartMessage = allocAddStaticString("Start");
	pcMovedTooFarMessage = allocAddStaticString("MovedTooFar");
}

static dynFxSoundVerifyFunc sndVerifyFunc = NULL;
static dynFxDSPVerifyFunc sndVerifyDSPFunc = NULL;
static dynFxSoundInvalidateFunc sndInvalidateFunc = NULL;

DictionaryHandle hDynFxInfoDict;

StashTable stBadFxFiles; // for logging bad files to blame people later when they break

StaticDefineInt ParseDynKeyFrameType[] =
{
	DEFINE_INT
	{ "Create",				eDynKeyFrameType_Create },
	{ "Destroy",			eDynKeyFrameType_Destroy },
	{ "Recreate",			eDynKeyFrameType_Recreate },
	{ "Update",				eDynKeyFrameType_Update },
	DEFINE_END
};


StaticDefineInt ParseMessageTo[] =
{
	DEFINE_INT
	{ "Self",				emtSelf },
	{ "Children",			emtChildren },
	{ "Parent",				emtParent },
	{ "Near",				emtNear },
	{ "Entity",				emtEntity },
	{ "Siblings",			emtSiblings },
	DEFINE_END
};

typedef struct DynOperator
{
	DynOpType eOpType;
	char* pcTokenString;
	Vec3 vValue;
	int iParseColumn;        // ''
	int iLineNum;
	U8 uiNumValuesSpecd;
} DynOperator;

typedef struct DynInterp
{
	eDynInterpType eInterpType;
	char* pcTokenString;
	int iLineNum;
} DynInterp;



StaticDefineInt ParseDynOpType[] =
{
	DEFINE_INT
	{ "Jitter",				DynOpType_Jitter },
	{ "JitterSphere",		DynOpType_SphereJitter },
	{ "JitterSphereShell",	DynOpType_SphereShellJitter },
	{ "Inherit",			DynOpType_Inherit },
	{ "Add",				DynOpType_Add },
	{ "Multiply",			DynOpType_Multiply },
	{ "Min",				DynOpType_Min },
	{ "Max",				DynOpType_Max },
	DEFINE_END
};

StaticDefineInt ParseDynParamOp[] =
{
	DEFINE_INT
	{ "Copy",				edpoCopy },
	{ "Add",				edpoAdd },
	{ "Multiply",			edpoMultiply },
	DEFINE_END
};

StaticDefineInt ParseDynFxDictType[] =
{
	DEFINE_INT
	{ "Material",	eDynFxDictType_Material },
	{ "Geometry",	eDynFxDictType_Geometry },
	{ "Texture",	eDynFxDictType_Texture  },
	{ "ClothInfo",	eDynFxDictType_ClothInfo },
	{ "ClothCollisionInfo", eDynFxDictType_ClothCollisionInfo },
	{ "None",		eDynFxDictType_None },
	DEFINE_END
};

AUTO_ENUM;
typedef enum eDynFxDepFlag
{
	eDynFxDepFlag_UsedOnSplat = 1<<0,
	eDynFxDepFlag_MaterialNotUsed = 1<<1, // For models, is the default material overridden?
	eDynFxDepFlag_UsedOnFlare = 1<<2,
} eDynFxDepFlag;

AUTO_STRUCT;
typedef struct DynFxDeps
{
	const char **texture_deps; AST( POOL_STRING )
	eDynFxDepFlag *texture_dep_flags; // sparse/short earray
	const char **material_deps; AST( POOL_STRING )
	eDynFxDepFlag *material_dep_flags;
	const char **geometry_deps; AST( POOL_STRING )
	eDynFxDepFlag *geometry_dep_flags;
} DynFxDeps;


void dynFxInfoLogBadFile(const char* pcFileName)
{
	if (!stBadFxFiles)
		stBadFxFiles = stashTableCreateWithStringKeys(16, StashDefault);
	if (strEndsWith(pcFileName, ".dfx"))
	{
		char cName[256];
		getFileNameNoExt(cName, pcFileName);
		stashAddPointer(stBadFxFiles, allocAddString(cName), allocAddString(pcFileName), true);
	}
}

void dynFxInfoReportFileError(const char* pcInfoName)
{
	const char* pcFileName;
	
	devassert(pcInfoName && pcInfoName[0]);

	if (stBadFxFiles && stashFindPointerConst(stBadFxFiles, pcInfoName, &pcFileName))
	{
		ErrorFilenameGroupf(pcFileName, "FX", 3, "Tried to use invalid FX %s, please check it for errors!", pcInfoName);
		return;
	}
	// If we got here, we must be unable to find any errors for this fx
	Errorf("Unable to find any mention of FX %s. Are you sure such a file exists?", pcInfoName);
}


static eDynParamType* edptMap;
static void initParamTokenTypeMap()
{
	U32 uiTokenIndex = uiFirstDynObjectStaticToken;
	edptMap = calloc(sizeof(eDynParamType), uiDynObjectTokenTerminator);
	while (uiTokenIndex != uiDynObjectTokenTerminator)
	{
		switch (ParseDynObject[uiTokenIndex].type & TOK_TYPE_MASK)
		{
			xcase TOK_STRING_X:
			{
				if(ParseDynObject[uiTokenIndex].type & TOK_EARRAY) {
					edptMap[uiTokenIndex] = edptStringArray;
				} else {
					edptMap[uiTokenIndex] = edptString;
				}
			}
			xcase TOK_F32_X:
			{
				if ( ParseDynObject[uiTokenIndex].param == 3 )
					edptMap[uiTokenIndex] = edptVector;
				else if ( ParseDynObject[uiTokenIndex].param == 4 )
					edptMap[uiTokenIndex] = edptVector4;
				else if ( ParseDynObject[uiTokenIndex].param == 2 )
					edptMap[uiTokenIndex] = edptVector2;
				else
					edptMap[uiTokenIndex] = edptNumber;
			}
			xcase TOK_INT_X:
			{
				edptMap[uiTokenIndex] = edptInteger;
			}
			xcase TOK_BOOL_X:
			case TOK_BOOLFLAG_X:
			{
				edptMap[uiTokenIndex] = edptBool;
			}
		}
		++uiTokenIndex;
	}
}


eDynParamType dynFxParamTypeFromToken(U32 uiTokenIndex)
{
	return edptMap[uiTokenIndex];
}

eDynParamType dynFxParamTypeFromTokenGeneral(U32 uiTokenIndex, const ParseTable* pParseTable)
{
	switch (pParseTable[uiTokenIndex].type & TOK_TYPE_MASK)
	{
		xcase TOK_STRING_X:
		{
			// Can't handle edptStringArrays here.
			return edptString;
		}
		xcase TOK_F32_X:
		{
			if ( pParseTable[uiTokenIndex].param == 3 )
				return edptVector;
			else if ( pParseTable[uiTokenIndex].param == 4 )
				return edptVector4;
			else if ( pParseTable[uiTokenIndex].param == 2 )
				return edptVector2;
			else
				return edptNumber;
		}
		xcase TOK_INT_X:
		{
			return edptInteger;
		}
		xcase TOK_BOOL_X:
		case TOK_BOOLFLAG_X:
		{
			return edptBool;
		}
		xcase TOK_QUATPYR_X:
		{
			return edptQuat;
		}
	}
	return edptNone;
}

StaticDefineInt ParseDynFlareType[] =
{
	DEFINE_INT
	{ "None",				eDynFlareType_None },
	{ "Sun",				eDynFlareType_Sun },
	DEFINE_END
};

StaticDefineInt ParseDynInterpType[] =
{
	DEFINE_INT
	{ "Linear",				ediLinear },
	{ "Step",				ediStep },
	{ "EaseIn",				ediEaseIn },
	{ "EaseOut",			ediEaseOut },
	{ "EaseInAndOut",		ediEaseInAndOut },
	DEFINE_END
};

StaticDefineInt ParseDynBlendMode[] =
{
	DEFINE_INT
	{ "Normal",				DynBlendMode_Normal },
	{ "Additive",			DynBlendMode_Additive },
	{ "Subtractive",		DynBlendMode_Subtractive },
	DEFINE_END
};

// For now only on or off, but this provides for a future with different types
StaticDefineInt ParseDynMeshTrail[] =
{
	DEFINE_INT
	{ "Off",				DynMeshTrail_None },
	{ "Oriented",			DynMeshTrail_Normal },
	{ "CamOriented",		DynMeshTrail_CamOriented },
	{ "Cylinder",			DynMeshTrail_Cylinder },
	DEFINE_END
};

StaticDefineInt ParseDynStreakMode[] =
{
	DEFINE_INT
	{ "None",				DynStreakMode_None },
	{ "Velocity",			DynStreakMode_Velocity },
	{ "Parent",				DynStreakMode_Parent },
	{ "ScaleTo",			DynStreakMode_ScaleToTarget },
	{ "Chain",				DynStreakMode_Chain },
	DEFINE_END
};

StaticDefineInt ParseDynLightType[] =
{
	DEFINE_INT
	{ "None",				WL_LIGHT_NONE },
	{ "Point",				WL_LIGHT_POINT },
	{ "Spot",				WL_LIGHT_SPOT },
	{ "Projector",			WL_LIGHT_PROJECTOR },
	DEFINE_END
};

StaticDefineInt ParseDynEntityLightMode[] =
{
	DEFINE_INT
	{ "None",				edelmNone },
	{ "Add",				edelmAdd },
	{ "Multiply",			edelmMultiply },
	DEFINE_END
};

StaticDefineInt ParseDynEntityTintMode[] =
{
	DEFINE_INT
	{ "None",				edetmNone },
	{ "Multiply",			edetmMultiply },
	{ "Add",				edetmAdd },
	{ "Alpha",				edetmAlpha },
	{ "Set",				edetmSet },
	DEFINE_END
};

StaticDefineInt ParseDynEntityScaleMode[] =
{
	DEFINE_INT
	{ "None",				edesmNone },
	{ "Multiply",			edesmMultiply },
	DEFINE_END
};

StaticDefineInt ParseDynEntityMaterialMode[] =
{
	DEFINE_INT
	{ "None",				edemmNone },
	{ "Swap",				edemmSwap },
	{ "Add",				edemmAdd },
	{ "TextureSwap",		edemmTextureSwap },
	{ "SwapWithConstants",	edemmSwapWithConstants },
	{ "AddWithConstants",	edemmAddWithConstants },
	{ "Dissolve",			edemmDissolve },
	DEFINE_END
};


StaticDefineInt ParseDynNodeXFormFlags[] = 
{
	DEFINE_INT
	{ "Position",			ednTrans },
	{ "Rotation",			ednRot },
	{ "Scale",				ednScale },
	{ "None",				ednNone },
	{ "All",				ednAll },
	DEFINE_END
};

StaticDefineInt ParseDynParentFlags[] = 
{
	DEFINE_INT
	{ "None",				edpfNone },
	{ "ScaleToOnce",		edpfScaleToOnce },
	{ "OrientToOnce",		edpfOrientToOnce },
	{ "LocalPosition",		edpfLocalPosition },
	{ "AttachAfterOrient",	edpfAttachAfterOrient },
	{ "OrientToLockToPlane", edpfOrientToLockToPlane },
	DEFINE_END
};


StaticDefineInt ParseDynOrientMode[] =
{
	DEFINE_INT
	{ "Camera",			DynOrientMode_Camera },
	{ "Local",			DynOrientMode_Local },
	{ "ZAxis",			DynOrientMode_ZAxis },
	{ "LockToX",		DynOrientMode_LockToX },
	{ "LockToY",		DynOrientMode_LockToY },
	DEFINE_END
};
/*
ParseTable ParseDynJListEntry[] = 
{
{ "",		TOK_LINENUM(DynJListEntry,iLineNum)				},
{ "",		TOK_STRUCTPARAM|TOK_INT(DynOperator,eOpType, DynOpType_None), ParseDynOpType	},
{ "",		TOK_STRUCTPARAM|TOK_STRING(DynOperator,pcTokenString,0) },
{ "",		TOK_STRUCTPARAM|TOK_F32(DynOperator,vValue[0], UNINIT_VALUE)},
{ "",		TOK_STRUCTPARAM|TOK_F32(DynOperator,vValue[1], UNINIT_VALUE)},
{ "",		TOK_STRUCTPARAM|TOK_F32(DynOperator,vValue[2], UNINIT_VALUE)},
{ "\n",					TOK_END,			0										},
{ "", 0, 0 },
};

ParseTable ParseDynJList[] =
{
{ "Name",			TOK_POOL_STRING|TOK_STRUCTPARAM|TOK_STRING(DynJList, pcJListTag, 0), },
{ "", 0, 0 },
};
*/

ParseTable ParseDynOperator[] = 
{
{ "",		TOK_LINENUM(DynOperator,iLineNum)				},
{ "",		TOK_STRUCTPARAM|TOK_INT(DynOperator,eOpType, DynOpType_None), ParseDynOpType	},
{ "",		TOK_POOL_STRING|TOK_STRUCTPARAM|TOK_STRING(DynOperator,pcTokenString,0) },
{ "",		TOK_STRUCTPARAM|TOK_F32(DynOperator,vValue[0], UNINIT_VALUE)},
{ "",		TOK_STRUCTPARAM|TOK_F32(DynOperator,vValue[1], UNINIT_VALUE)},
{ "",		TOK_STRUCTPARAM|TOK_F32(DynOperator,vValue[2], UNINIT_VALUE)},
{ "\n",					TOK_END,			0										},
{ "", 0, 0 },
};


ParseTable ParseDynInterp[] = 
{
{ "",		TOK_LINENUM(DynInterp,iLineNum)				},
{ "",		TOK_STRUCTPARAM|TOK_INT(DynInterp,eInterpType, ediLinear), ParseDynInterpType	},
{ "",		TOK_POOL_STRING|TOK_STRUCTPARAM|TOK_STRING(DynInterp,pcTokenString,0) },
{ "\n",					TOK_END,			0										},
{ "", 0, 0 },
};

ParseTable ParseDynChildCall[] = 
{
{ "",		TOK_LINENUM(DynChildCall,iLineNum)				},
{ "",		TOK_STRUCTPARAM | TOK_REQUIRED | TOK_REFERENCE(DynChildCall, hChildFx, 0, "DynFxInfo") },
{ "",		TOK_STRUCTPARAM|TOK_INT(DynChildCall,iTimesToCall, 1) },
{ "\n",					TOK_END,			0										},
{ "", 0, 0 },
};

ParseTable ParseSimpleDynFxMessage[] =
{
	{ "DynFxMessage", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(DynFxMessage), 0, NULL, 0 },
	{ "MessageType",	TOK_STRUCTPARAM | TOK_POOL_STRING | TOK_STRING(DynFxMessage, pcMessageType, 0), NULL },
	{ "SendTo",			TOK_IGNORE|TOK_AUTOINT(DynFxMessage, eSendTo, emtSelf), ParseMessageTo },
	{ "\n",				TOK_END, 0 },
	{ "", 0, 0 }
};

// Put the object descriptor stuff here, it's used in two parse tables
ParseTable ParseDynObject[] = 
{
{ "ParamBitfield",	TOK_USEDFIELD | TOK_FIXED_ARRAY | TOK_AUTOINTARRAY(DynObjectInfo,bfParamsSpecified)		},
{ "Apply",			TOK_STRUCT(DynObjectInfo,eaDynOps,ParseDynOperator) },
{ "Param",			TOK_STRUCT(DynObjectInfo,eaParams,parse_DynApplyParam) },
{ "Interp",			TOK_STRUCT(DynObjectInfo,eaInterps,ParseDynInterp) },
{ "JitterList",		TOK_STRUCT(DynObjectInfo,eaJLists,parse_DynJitterList) },
{ "StartOfValueToks", TOK_IGNORE, 0 },
{ "Texture",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcTextureName,0) },
{ "Texture2",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcTextureName2,0) },
{ "Geometry",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcModelName,0) },
{ "Cloth",			TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcClothName,0) },
{ "ClothInfo",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcClothInfo,0) },
{ "ClothCollide",	TOK_BOOL(DynObjectInfo,obj.draw.bClothCollide, false) },
{ "ClothCollideSelfOnly", TOK_BOOL(DynObjectInfo,obj.draw.bClothCollideSelfOnly, false) },
{ "ClothCollisionInfo", TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcClothCollisionInfo,0) },
{ "ClothBackMaterial", TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcMaterial2Name,0) },
{ "Material2",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcMaterial2Name,0) },
{ "Material",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcMaterialName,0) },
{ "GeoDissolveMaterial", TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcGeoDissolveMaterialName,0) },
{ "GeoAddMaterials", TOK_POOL_STRING|TOK_STRINGARRAY(DynObjectInfo,obj.draw.ppcGeoAddMaterialNames), NULL },
{ "BoneName",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcBoneName,0) },
{ "Skeleton",		TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcBaseSkeleton,0) },
{ "SkyName",		TOK_POOL_STRING|TOK_STRINGARRAY(DynObjectInfo,obj.skyInfo.ppcSkyName), NULL },
{ "SkyFalloff",		TOK_INT(DynObjectInfo,obj.skyInfo.eSkyFalloff, eSkyFalloffType_Linear ), eSkyFalloffTypeEnum}, 
{ "BlendMode",		TOK_INT(DynObjectInfo,obj.draw.blend, DynBlendMode_Normal), ParseDynBlendMode  }, 
{ "Trail",			TOK_INT(DynObjectInfo,obj.meshTrail.mode, DynMeshTrail_None), ParseDynMeshTrail  }, 
{ "StreakMode",		TOK_INT(DynObjectInfo,obj.draw.streakMode, DynStreakMode_None), ParseDynStreakMode  }, 
{ "LightType",		TOK_INT(DynObjectInfo,obj.light.eLightType, WL_LIGHT_NONE), ParseDynLightType },
{ "LightShadows",	TOK_BOOL(DynObjectInfo,obj.light.bCastShadows, false) },
{ "EntMaterial",	TOK_INT(DynObjectInfo,obj.draw.eEntityMaterialMode, edemmNone), ParseDynEntityMaterialMode },
{ "HInvert",		TOK_BOOL(DynObjectInfo,obj.draw.bHInvert, false) },
{ "VInvert",		TOK_BOOL(DynObjectInfo,obj.draw.bVInvert, false) },
{ "FixedAspectRatio", TOK_BOOL(DynObjectInfo,obj.draw.bFixedAspectRatio, false) },
{ "StreakTile",		TOK_BOOL(DynObjectInfo,obj.draw.bStreakTile, false) },
{ "CastShadows",	TOK_BOOL(DynObjectInfo,obj.draw.bCastShadows, false) },
{ "Oriented",		TOK_INT(DynObjectInfo,obj.draw.eOriented, DynOrientMode_Camera), ParseDynOrientMode },
{ "LocalVelocity",	TOK_BOOL(DynObjectInfo,obj.draw.bLocalOrientation, false) },
{ "LocalOrientation",	TOK_REDUNDANTNAME|TOK_BOOL(DynObjectInfo,obj.draw.bLocalOrientation, false) },
{ "VelocityDriveOrientation", TOK_BOOL(DynObjectInfo,obj.draw.bVelocityDriveOrientation, false) },
{ "EntLightMode",	TOK_INT(DynObjectInfo,obj.draw.eEntityLightMode, edelmNone), ParseDynEntityLightMode },
{ "EntTintMode",	TOK_INT(DynObjectInfo,obj.draw.eEntityTintMode, edetmNone), ParseDynEntityTintMode },
{ "EntScaleMode",	TOK_INT(DynObjectInfo,obj.draw.eEntityScaleMode, edesmNone), ParseDynEntityScaleMode },
{ "SplatType",	TOK_INT(DynObjectInfo,obj.splatInfo.eType,eDynSplatType_None), eDynSplatTypeEnum },
{ "FlareType",		TOK_INT(DynObjectInfo,obj.flare.eType,eDynFlareType_None), eDynFlareTypeEnum },
{ "FlareSize",		TOK_F32_X | TOK_EARRAY, offsetof(DynObjectInfo,obj.flare.size), 0 },
{ "FlarePosition",	TOK_F32_X | TOK_EARRAY, offsetof(DynObjectInfo,obj.flare.position), 0 },
{ "FlareMaterial",	TOK_POOL_STRING|TOK_STRINGARRAY(DynObjectInfo,obj.flare.ppcMaterials), NULL },
{ "SplatForceDown",	TOK_BOOLFLAG(DynObjectInfo,obj.splatInfo.bForceDown,false) },
{ "SplatUpdateScale",	TOK_BOOLFLAG(DynObjectInfo,obj.splatInfo.bUpdateScale,false) },
{ "SplatCenter",	TOK_BOOLFLAG(DynObjectInfo,obj.splatInfo.bCenterLength,false) },
{ "TrailCurve",TOK_BOOLFLAG(DynObjectInfo,obj.meshTrail.bSubFrameCurve,false) },
{ "TrailStop",	TOK_BOOLFLAG(DynObjectInfo,obj.meshTrail.bStopEmitting,false) },
{ "TexWords",  TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcTexWords,0) },
{ "UseClothWindOverride", TOK_BOOL(DynObjectInfo,obj.draw.bUseClothWindOverride, false) },
{ "TimeScaleChildren",	TOK_BOOLFLAG(DynObjectInfo,obj.controls.bTimeScaleChildren, false) },
{ "AttachCamera", TOK_BOOL(DynObjectInfo,obj.cameraInfo.bAttachCamera, false) },
{ "GetModelFromCostumeBone", TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.draw.pcBoneForCostumeModelGrab,0) },
{ "Instanceable", TOK_BOOLFLAG(DynObjectInfo,obj.draw.bExplicitlyInstanceable, false) },
{ "CameraDelayDistanceBasis",	TOK_F32(DynObjectInfo,obj.cameraInfo.fCameraDelayDistanceBasis,0.0f) },
{ "CameraLookAt",    TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.cameraInfo.pcCameraLookAtNode,0)},
{ "CameraLookAtSpeed", TOK_F32(DynObjectInfo,obj.cameraInfo.fCameraLookAtSpeed,0.0f) },
{ "SplatBoneNameForProjection", TOK_POOL_STRING|TOK_STRING(DynObjectInfo,obj.splatInfo.pcSplatProjectionBone,0) },
{ "SplatDisableCulling", TOK_BOOLFLAG(DynObjectInfo,obj.splatInfo.bDisableCulling,false) },
// Place all interpolatable data after this entry (StartOfDynamicToks)
{ "StartOfDynamicToks", TOK_IGNORE, 0 },
{ "Position",		TOK_VEC3(DynObjectInfo,obj.draw.vPos) },
{ "DrawOffset",		TOK_VEC3(DynObjectInfo,obj.draw.vDrawOffset) },
{ "Velocity",		TOK_VEC3(DynObjectInfo,obj.draw.vVel) }, 
{ "Scale",			TOK_VEC3(DynObjectInfo,obj.draw.vScale) },
{ "HSVShift",		TOK_VEC3(DynObjectInfo,obj.draw.vHSVShift) },
{ "TexOffset",			TOK_VEC2(DynObjectInfo,obj.draw.vTexOffset) },
{ "TexScale",			TOK_VEC2(DynObjectInfo,obj.draw.vTexScale) },
{ "Color",			TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynObjectInfo, obj.draw.color), 3 },
{ "Alpha",			TOK_F32(DynObjectInfo,obj.draw.color[3], 0.0f) },
{ "Drag",			TOK_F32(DynObjectInfo,obj.draw.fDrag, 0.0f) },
{ "TightenUp",		TOK_F32(DynObjectInfo,obj.draw.fTightenUp, 0.0f) },
{ "Gravity",			TOK_F32(DynObjectInfo,obj.draw.fGravity, 0.0f) },
{ "Orientation",	TOK_QUATPYR(DynObjectInfo,obj.draw.qRot) }, 
{ "Spin",			TOK_VEC3(DynObjectInfo,obj.draw.vSpin) }, 
{ "SpriteSpin",		TOK_F32(DynObjectInfo,obj.draw.fSpriteSpin, 0.0f) }, 
{ "SpriteOrientation",	TOK_F32(DynObjectInfo,obj.draw.fSpriteOrientation, 0.0f) }, 
{ "StreakScale",	TOK_F32(DynObjectInfo,obj.draw.fStreakScale, 1) },
{ "GoToSpeed",		TOK_F32(DynObjectInfo,obj.draw.fGoToSpeed, 0.0f) }, 
{ "GoToGravity",		TOK_F32(DynObjectInfo,obj.draw.fGoToGravity, 0.0f) }, 
{ "GoToGravityFalloff",	TOK_F32(DynObjectInfo,obj.draw.fGoToGravityFalloff, 0.0f) }, 
{ "GoToApproachSpeed",	TOK_F32(DynObjectInfo,obj.draw.fGoToApproachSpeed, 0.0f) }, 
{ "GoToSpringEquilibrium",	TOK_F32(DynObjectInfo,obj.draw.fGoToSpringEquilibrium, 0.0f) }, 
{ "GoToSpringConstant",		TOK_F32(DynObjectInfo,obj.draw.fGoToSpringConstant, 0.0f) }, 
{ "ParentVelocityOffset",		TOK_F32(DynObjectInfo,obj.draw.fParentVelocityOffset, 0.0f) }, 
{ "Color1",	TOK_VEC4(DynObjectInfo, obj.draw.vColor1) },
{ "Color2",	TOK_VEC4(DynObjectInfo, obj.draw.vColor2) },
{ "Color3",	TOK_VEC4(DynObjectInfo, obj.draw.vColor3) },
{ "LightDiffuseHSV",	TOK_VEC3(DynObjectInfo, obj.light.vDiffuseHSV) },
{ "LightSpecularHSV",	TOK_VEC3(DynObjectInfo, obj.light.vSpecularHSV) },
{ "LightRadius",	TOK_F32(DynObjectInfo,obj.light.fRadius, 0.0f) },
{ "LightInnerRadiusPercentage",	TOK_F32(DynObjectInfo,obj.light.fInnerRadiusPercentage, 0.1f) },
{ "LightInnerCone",	TOK_F32(DynObjectInfo,obj.light.fInnerConeAngle, 0.0f) },
{ "LightOuterCone",	TOK_F32(DynObjectInfo,obj.light.fOuterConeAngle, 0.0f) },
{ "TrailKey1Time",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[0].fTime,0.0f) },
{ "TrailKey2Time",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[1].fTime,0.0f) },
{ "TrailKey3Time",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[2].fTime,0.0f) },
{ "TrailKey4Time",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[3].fTime,0.0f) },
{ "TrailKey1Width",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[0].fWidth,0.0f) },
{ "TrailKey2Width",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[1].fWidth,0.0f) },
{ "TrailKey3Width",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[2].fWidth,0.0f) },
{ "TrailKey4Width",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[3].fWidth,0.0f) },
{ "TrailKey1Alpha",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[0].vColor[3],0.0f) },
{ "TrailKey2Alpha",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[1].vColor[3],0.0f) },
{ "TrailKey3Alpha",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[2].vColor[3],0.0f) },
{ "TrailKey4Alpha",	TOK_F32(DynObjectInfo,obj.meshTrail.keyFrames[3].vColor[3],0.0f) },
{ "TrailKey1Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynObjectInfo, obj.meshTrail.keyFrames[0].vColor), 3 },
{ "TrailKey2Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynObjectInfo, obj.meshTrail.keyFrames[1].vColor), 3 },
{ "TrailKey3Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynObjectInfo, obj.meshTrail.keyFrames[2].vColor), 3 },
{ "TrailKey4Color",	TOK_F32_X | TOK_FIXED_ARRAY, offsetof(DynObjectInfo, obj.meshTrail.keyFrames[3].vColor), 3 },
{ "TrailFadeIn",	TOK_F32(DynObjectInfo,obj.meshTrail.fFadeInTime,0.0f) },
{ "TrailFadeOut",	TOK_F32(DynObjectInfo,obj.meshTrail.fFadeOutTime,0.0f) },
{ "TrailTexDensity",TOK_F32(DynObjectInfo,obj.meshTrail.fTexDensity,0.0f) },
{ "TrailEmitRate",TOK_F32(DynObjectInfo,obj.meshTrail.fEmitRate,0.0f) },
{ "TrailEmitDistance",TOK_F32(DynObjectInfo,obj.meshTrail.fEmitDistance,0.0f) },
{ "TrailMinSpeed",TOK_F32(DynObjectInfo,obj.meshTrail.fMinForwardSpeed,0.0f) },
{ "TrailCurveDir",	TOK_VEC3(DynObjectInfo,obj.meshTrail.vCurveDir) },
{ "TrailSpeedThreshold", TOK_F32(DynObjectInfo,obj.meshTrail.fEmitSpeedThreshold,0.0f) },
{ "TrailShiftSpeed", TOK_F32(DynObjectInfo,obj.meshTrail.fShiftSpeed,0.0f) },
{ "ShakePower",	TOK_F32(DynObjectInfo,obj.cameraInfo.fShakePower,0.0f) },
{ "ShakeRadius",	TOK_F32(DynObjectInfo,obj.cameraInfo.fShakeRadius,0.0f) },
{ "ShakeVertical",	TOK_F32(DynObjectInfo,obj.cameraInfo.fShakeVertical,0.0f) },
{ "ShakePan",	TOK_F32(DynObjectInfo,obj.cameraInfo.fShakePan,0.0f) },
{ "ShakeSpeed",	TOK_F32(DynObjectInfo,obj.cameraInfo.fShakeSpeed,1.0f) },
{ "FOV",		TOK_F32(DynObjectInfo,obj.cameraInfo.fCameraFOV,0.0f) },
{ "CameraControlInfluence", TOK_F32(DynObjectInfo,obj.cameraInfo.fCameraInfluence,1.0f) },
{ "CameraDelaySpeed",	TOK_F32(DynObjectInfo,obj.cameraInfo.fCameraDelaySpeed,0.0f) },
{ "SplatRadius",	TOK_F32(DynObjectInfo,obj.splatInfo.fSplatRadius,0.0f) },
{ "SplatInnerRadius",TOK_F32(DynObjectInfo,obj.splatInfo.fSplatInnerRadius,0.0f) },
{ "SplatLength",	TOK_F32(DynObjectInfo,obj.splatInfo.fSplatLength,0.0f) },
{ "SplatFadePlane",	TOK_F32(DynObjectInfo,obj.splatInfo.fSplatFadePlanePt,0.0f) },
{ "SkyRadius",	TOK_F32(DynObjectInfo,obj.skyInfo.fSkyRadius,0.0f) },
{ "SkyLength",	TOK_F32(DynObjectInfo,obj.skyInfo.fSkyLength,0.0f) },
{ "SkyWeight",	TOK_F32(DynObjectInfo,obj.skyInfo.fSkyWeight,0.0f) },
{ "FlareColor",		TOK_VEC3(DynObjectInfo,obj.flare.hsv_color) },
{ "ClothWindOverride", TOK_VEC3(DynObjectInfo,obj.draw.vClothWindOverride) },
{ "LightModulationAmount", TOK_F32(DynObjectInfo,obj.draw.fLightModulation, 0.0f) },
{ "TimeScale",	TOK_F32(DynObjectInfo,obj.controls.fTimeScale, 1.0f) },
{ "End",			TOK_END,		0										},
{ "", 0, 0 }
};




ParseTable ParseDynKeyFrame[] = 
{
// Control parameters
{ "Time",			TOK_F32(DynKeyFrame,fParseTimeStamp,	0),	},
{ "Type",			TOK_INT(DynKeyFrame,eType,	eDynKeyFrameType_Update), ParseDynKeyFrameType },
{ "Count",			TOK_INT(DynKeyFrame,uiCount, 0) },
{ "Value",			TOK_EMBEDDEDSTRUCT(DynKeyFrame,objInfo[edoValue], ParseDynObject) },
{ "Rate",			TOK_EMBEDDEDSTRUCT(DynKeyFrame,objInfo[edoRate], ParseDynObject) },
{ "Amp",			TOK_EMBEDDEDSTRUCT(DynKeyFrame,objInfo[edoAmp], ParseDynObject) },
{ "Freq",			TOK_EMBEDDEDSTRUCT(DynKeyFrame,objInfo[edoFreq], ParseDynObject) },
{ "CycleOffset",	TOK_EMBEDDEDSTRUCT(DynKeyFrame,objInfo[edoCycleOffset], ParseDynObject) },
{ "childCallCollection", 	TOK_IGNORE | TOK_FLATEMBED },
{ "CallParam",				TOK_STRUCT(DynKeyFrame, childCallCollection.eaChildCall, parse_DynChildCall) },
{ "Call",					TOK_REDUNDANTNAME | TOK_STRUCT(DynKeyFrame, childCallCollection.eaChildCall, ParseDynChildCall) },
{ "CallList",				TOK_STRUCT(DynKeyFrame, childCallCollection.eaChildCallList, parse_DynChildCallList) },
{ "LoopStart",		TOK_STRUCT(DynKeyFrame,eaLoopStart, parse_DynLoopRef) },
{ "LoopStop",		TOK_STRUCT(DynKeyFrame,eaLoopStop, parse_DynLoopRef) },
{ "LoopEnd",		TOK_REDUNDANTNAME|TOK_STRUCT(DynKeyFrame,eaLoopStop, parse_DynLoopRef) },
{ "EmitterStart",	TOK_STRUCT(DynKeyFrame,eaEmitterStart, parse_DynParticleEmitterRef) },
{ "EmitterStop",	TOK_STRUCT(DynKeyFrame,eaEmitterStop, parse_DynParticleEmitterRef) },
{ "RaycastStart",	TOK_STRUCT(DynKeyFrame,eaRaycastStart, parse_DynRaycastRef) },
{ "RaycastStop",	TOK_STRUCT(DynKeyFrame,eaRaycastStop, parse_DynRaycastRef) },
{ "ForceStart",		TOK_STRUCT(DynKeyFrame,eaForceStart, parse_DynForceRef) },
{ "ForceStop",		TOK_STRUCT(DynKeyFrame,eaForceStop, parse_DynForceRef) },
{ "Costume",		TOK_STRUCT(DynKeyFrame,eaCostume, parse_DynFxCostume) },
{ "SendMessage",	TOK_STRUCT(DynKeyFrame,eaMessage, parse_DynFxMessage) },
{ "Message",		TOK_REDUNDANTNAME|TOK_STRUCT(DynKeyFrame,eaMessage, ParseSimpleDynFxMessage) },
{ "SelfMessage",	TOK_REDUNDANTNAME|TOK_STRUCT(DynKeyFrame,eaMessage, ParseSimpleDynFxMessage) },
{ "Parent",			TOK_OPTIONALSTRUCT(DynKeyFrame,pParentBhvr, parse_DynParentBhvr) },
{ "Physics",			TOK_OPTIONALSTRUCT(DynKeyFrame,pPhysicsInfo, parse_DynFxPhysicsInfo) },
{ "SkinToChildren", TOK_BOOLFLAG(DynKeyFrame,bSkinToChildren, false) },
{ "InheritVelocity", TOK_F32(DynKeyFrame,fInheritParentVelocity, false) },
{ "SoundStart",		TOK_STRINGARRAY(DynKeyFrame,ppcSoundStarts) },
{ "SoundEnd",		TOK_STRINGARRAY(DynKeyFrame,ppcSoundEnds) },
{ "SoundDSPStart",		TOK_STRINGARRAY(DynKeyFrame,ppcSoundDSPStarts) },
{ "SoundDSPEnd",		TOK_STRINGARRAY(DynKeyFrame,ppcSoundDSPEnds) },
{ "FadeOutTime",	TOK_F32(DynKeyFrame,fFadeOutTime,	0),	},
{ "FadeInTime",		TOK_F32(DynKeyFrame,fFadeInTime,	0),	},
{ "SystemFadeOut",	TOK_BOOLFLAG(DynKeyFrame,bSystemFade,	0),	},
{ "SystemFade",		TOK_BOOLFLAG(DynKeyFrame,bSystemFade,	0),	},
{ "EntCostumeParts",	TOK_POOL_STRING | TOK_STRINGARRAY(DynKeyFrame, eaEntCostumeParts), NULL },
{ "EntMaterialExcludeOptionalParts", TOK_BOOL(DynKeyFrame,bEntMaterialExcludeOptionalParts, 0), },
{ "SeverBones",		TOK_POOL_STRING | TOK_STRINGARRAY(DynKeyFrame, eaSeverBones), NULL },
{ "RestoreSeveredBones",	TOK_POOL_STRING | TOK_STRINGARRAY(DynKeyFrame, eaRestoreSeveredBones), NULL },
{ "Hide",			TOK_BOOLFLAG(DynKeyFrame,bHide, false) },
{ "Unhide",			TOK_BOOLFLAG(DynKeyFrame,bUnhide, false) },
{ "End",			TOK_END,		0										},
{ "", 0, 0 }
};

ParseTable ParseDynEvent[] =
{
{ "Name",		TOK_POOL_STRING|TOK_STRUCTPARAM|TOK_STRING(DynEvent,pcMessageType, 0)	},
{ "KeyFrame",		TOK_STRUCT(DynEvent,keyFrames,	ParseDynKeyFrame) },
{ "MatConstRename",	TOK_STRUCT(DynEvent,eaMatConstRename,	parse_DynMNCRename) },
{ "ContinuingFX",	TOK_BOOLFLAG(DynEvent,bKeepAlive, 0)			},
{ "Loop",			TOK_BOOLFLAG(DynEvent,bLoop, 0)			},
{ "TriggerOnce",	TOK_BOOLFLAG(DynEvent,bTriggerOnce, 0)			},
{ "AutoTriggerTime",	TOK_F32(DynEvent,fAutoCallTime,	0),	},
{ "End",			TOK_END,						0										},
{ "", 0, 0 }
};

#define TYPE_parse_DynFxInfo DynFxInfo
ParseTable parse_DynFxInfo[] =
{
{ "Name",		TOK_STRUCTPARAM|TOK_IGNORE, 0 },
{ "InternalName",	TOK_KEY|TOK_IGNORE|TOK_POOL_STRING|TOK_STRING(DynFxInfo, pcDynName, 0) },
{ "FileName",		TOK_POOL_STRING | TOK_CURRENTFILE(DynFxInfo,pcFileName)				},
{ "TS",				TOK_TIMESTAMP(DynFxInfo,fileAge)},
{ "Loop",			TOK_STRUCT(DynFxInfo,eaLoops,	parse_DynLoop) },
{ "Emitter",		TOK_STRUCT(DynFxInfo,eaEmitters,	parse_DynParticleEmitter) },
{ "Raycast",		TOK_STRUCT(DynFxInfo,eaRaycasts,	parse_DynRaycast) },
{ "ContactEvent",	TOK_STRUCT(DynFxInfo,eaContactEvents,	parse_DynContactEvent) },
{ "Force",			TOK_STRUCT(DynFxInfo,eaForces,	parse_DynForce) },
{ "List",			TOK_STRUCT(DynFxInfo,eaLists,	parse_DynList) },
{ "Priority",		TOK_INT(DynFxInfo,iPriorityLevel, edpDefault), eDynPriorityEnum },
{ "DropPriority",	TOK_INT(DynFxInfo,iDropPriorityLevel, edpNotSet), eDynPriorityEnum },
{ "MinDrawDistance",TOK_F32(DynFxInfo,fMinDrawDistance, 0.0f) },
{ "DrawDistance",	TOK_F32(DynFxInfo,fDrawDistance, 0.0f) },
{ "Radius",			TOK_F32(DynFxInfo,fRadius, 0.0f) },
{ "PlaybackJitter",	TOK_F32(DynFxInfo,fPlaybackJitter, 0.0f) },
{ "InheritPlaybackSpeed",	TOK_BOOLFLAG(DynFxInfo,bInheritPlaybackSpeed, false) },
{ "SortAsBlock",	TOK_BOOLFLAG(DynFxInfo,bSortAsBlock_Deprecated, 0)			},
{ "ForwardMessages",TOK_BOOLFLAG(DynFxInfo,bForwardMessages, 0)			},
{ "DontDraw",		TOK_BOOLFLAG(DynFxInfo,bForceDontDraw, false) },
{ "EntNeedsAuxPass",TOK_BOOLFLAG(DynFxInfo,bEntNeedsAuxPass, false) },
{ "DontHueShift",	TOK_BOOLFLAG(DynFxInfo,bDontHueShift, false) },
{ "DontHueShiftChildren",	TOK_BOOLFLAG(DynFxInfo,bDontHueShiftChildren, false) },
{ "DefaultHue",		TOK_F32(DynFxInfo,fDefaultHue, 0) },
{ "EntityFadeSpeedOverride", TOK_F32(DynFxInfo,fEntityFadeSpeedOverride, 0) },
{ "MovedTooFarMessageDistance", TOK_F32(DynFxInfo,fMovedTooFarMessageDistance, 0) },
{ "KillIfOrphaned",	TOK_BOOLFLAG(DynFxInfo,bKillIfOrphaned, false) },
{ "SuppressionTag",	TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcSuppressionTag,0) },
{ "ExclusionTag",	TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcExclusionTag,0) },
{ "IKTargetTag",	TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcIKTargetTag,0) },
{ "IKTargetBone",	TOK_POOL_STRING|TOK_STRINGARRAY(DynFxInfo, eaIKTargetBone), NULL },
{ "NoAlphaInherit", TOK_BOOLFLAG(DynFxInfo,bNoAlphaInherit, false) },
{ "UseSkeletonGeometryAlpha", TOK_BOOLFLAG(DynFxInfo,bUseSkeletonGeometryAlpha, false) },
{ "Unique",			TOK_BOOLFLAG(DynFxInfo,bUnique, false) },
{ "Debug",			TOK_BOOLFLAG(DynFxInfo,bDebugFx, false) },
{ "Force2D",		TOK_BOOLFLAG(DynFxInfo,bForce2D, false) },
{ "OverrideForce2DForNonLocal", TOK_BOOLFLAG(DynFxInfo,bOverrideForce2DNonLocal, false) },
{ "LocalPlayerOnly",TOK_BOOLFLAG(DynFxInfo,bLocalPlayerOnly, false) },
{ "LowRes",			TOK_BOOLFLAG(DynFxInfo,bLowRes, false) },
{ "DontLeakTest",	TOK_BOOLFLAG(DynFxInfo,bDontLeakTest, false) },
{ "PlayOnCostumeTag",TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcPlayOnCostumeTag,0) },
{ "KillOnEmpty",	TOK_BOOLFLAG(DynFxInfo,bKillOnEmpty_Deprecated, false) },
{ "DontAutoKill",	TOK_BOOLFLAG(DynFxInfo,bDontAutoKill_Deprecated, false)			},
{ "Hibernate",		TOK_BOOLFLAG(DynFxInfo,bHibernate, false)			},
{ "RequiresNode",	TOK_BOOLFLAG(DynFxInfo,bRequiresNode, false)			},
{ "UseMountNodeAliases",TOK_BOOLFLAG(DynFxInfo,bUseMountNodeAliases, false)	},
{ "Event",			TOK_STRUCT(DynFxInfo,events,ParseDynEvent) },
{ "DefaultParam",	TOK_STRUCT(DynFxInfo,paramBlock.eaDefineParams,parse_DynDefineParam) },
{ "EditorParam",	TOK_STRUCT(DynFxInfo,eaEditorParams, parse_DynEditorParam) },
{ "AlienColorPriority", TOK_F32(DynFxInfo,fAlienColorPriority, 0) },
// Redirects to other FX
{ "NonTargetVersion", TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcNonTargetVersion,0) },
{ "SourcePlayerVersion", TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcSourcePlayerVersion,0) },
{ "TargetPlayerVersion", TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcTargetPlayerVersion,0) },
{ "EnemyVersion",	TOK_POOL_STRING|TOK_STRING(DynFxInfo,pcEnemyVersion,0) },
// World model interaction
{ "GetModelFromWorld", TOK_BOOLFLAG(DynFxInfo,bGetModelFromWorld,false) },
{ "GetClothModelFromWorld", TOK_BOOLFLAG(DynFxInfo,bGetClothModelFromWorld,false) },
{ "HideWorldModel", TOK_BOOLFLAG(DynFxInfo,bHideWorldModel,false) },
{ "UseWorldModelScale", TOK_BOOLFLAG(DynFxInfo,bUseWorldModelScale,false) },
{ "AllowWorldModelSwitch", TOK_BOOLFLAG(DynFxInfo,bAllowWorldModelSwitch,false) },
//{ "Inherit",		TOK_FLAGS(DynFxInfo,uiInheritFlags,0),ParseDynTransformFlag	},
//{ "Parent",			TOK_FLAGS(DynFxInfo,uiParentFlags,0), ParseDynTransformFlag	},
{ "PropagateZeroAlpha", TOK_BOOLFLAG(DynFxInfo,bPropagateZeroAlpha,false) },
{ "LateUpdateFastParticles", TOK_BOOLFLAG(DynFxInfo,bLateUpdateFastParticles,false) },
{ "End",			TOK_END,						0										},
{ "", 0, 0 }
};



void dynFxObjectClearToken(ParseTable tpi[], int column, DynObject* pObject)
{
	static DynObject clearObject;
	static bool bInit = false;
	if (!bInit)
	{
		ZeroStruct(&clearObject);
		bInit = true;
	}
	TokenCopy(tpi, column, pObject, &clearObject, 0);
}

bool dynObjectInfoHasDynOpForToken(const DynObjectInfo* pDynObjectInfo, int iTokenIndex )
{
	const U32 uiNumDynOps = eaSize(&pDynObjectInfo->eaDynOps);
	U32 uiDynOpIndex;
	for (uiDynOpIndex=0; uiDynOpIndex<uiNumDynOps; ++uiDynOpIndex)
	{
		DynOperator* pDynOp = pDynObjectInfo->eaDynOps[uiDynOpIndex];
		if ( pDynOp->iParseColumn == iTokenIndex )
			return true;
	}
	return false;
}

static bool dynObjectInfoHasJListForToken(const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex )
{
	const U32 uiNumJLists = eaSize(&pDynObjectInfo->eaJLists);
	U32 uiJList;
	for (uiJList=0; uiJList<uiNumJLists; ++uiJList)
	{
		DynJitterList* pJList = pDynObjectInfo->eaJLists[uiJList];
		if ( pJList->uiTokenIndex == uiTokenIndex )
			return true;
	}
	return false;
}

bool dynObjectInfoHasParamForToken(const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex )
{
	const U32 uiNumParams = eaSize(&pDynObjectInfo->eaParams);
	U32 uiParamIndex;
	for (uiParamIndex=0; uiParamIndex<uiNumParams; ++uiParamIndex)
	{
		DynApplyParam* pParam = pDynObjectInfo->eaParams[uiParamIndex];
		if ( pParam->uiTokenIndex == uiTokenIndex )
			return true;
	}
	return false;
}

static Vec3 vSphereResult = {0};
static int uiLastIndex=-1;
static void dynFxApplyF32DynOp(F32* fValue, DynOperator* pDynOp, U32 uiValueIndex, const DynParticle* pParentParticle)
{
	switch (pDynOp->eOpType)
	{
		xcase DynOpType_Jitter:
		{
			if ( pDynOp->uiNumValuesSpecd == 1 )
			{
				static F32 fRandomResult;
				if (uiValueIndex == 0)
				{
					fRandomResult = randomF32Seeded(NULL, RandType_BLORN);
				}
				*fValue += fRandomResult * pDynOp->vValue[0];
			}
			else
				*fValue += pDynOp->vValue[uiValueIndex] * randomF32Seeded(NULL, RandType_BLORN);
		}
		xcase DynOpType_SphereJitter:
		{
			// Oh god. This relies on calling dynFxApplyF32DynOp in the right order... this will break eventually
			if ( uiValueIndex == 0 )
				randomSphereSliceSeeded(NULL, RandType_BLORN, RAD(pDynOp->vValue[0]), CLAMP(RAD(pDynOp->vValue[1]), -PI, PI), pDynOp->vValue[2], vSphereResult);
			else
				assert(uiValueIndex == uiLastIndex+1);
			CHECK_FINITE(*fValue);
			CHECK_FINITE(vSphereResult[uiValueIndex]);
			*fValue += vSphereResult[uiValueIndex];
			CHECK_FINITE(*fValue);
			uiLastIndex = uiValueIndex;
		}
		xcase DynOpType_SphereShellJitter:
		{
			// Oh god. This relies on calling dynFxApplyF32DynOp in the right order... this will break eventually
			if ( uiValueIndex == 0 )
				randomSphereShellSliceSeeded(NULL, RandType_BLORN, RAD(pDynOp->vValue[0]), pDynOp->vValue[1]/180.0f, pDynOp->vValue[2], vSphereResult);
			else
				assert(uiValueIndex == uiLastIndex+1);
			*fValue += vSphereResult[uiValueIndex];
			uiLastIndex = uiValueIndex;
		}
		xcase DynOpType_Inherit:
		{
			U32 uiEntryIndex;
			if ( pParentParticle )
			{
				for (uiEntryIndex=0; uiEntryIndex<pParentParticle->uiNumEntries; ++uiEntryIndex)
				{
					if ( pParentParticle->pEntries[uiEntryIndex].uiTokenIndex == (U32)pDynOp->iParseColumn )
					{
						*fValue = *((F32*)(pParentParticle->pData + pParentParticle->pEntries[uiEntryIndex].uiOffset + uiValueIndex*sizeof(F32)));
					}
				}
			}
	//		if (srcStruct) TokenStoreSetF32(tpi, column, dstStruct, index, TokenStoreGetF32(tpi, column, srcStruct, index));
		}
		xcase DynOpType_Add:
		{
			if ( pDynOp->uiNumValuesSpecd == 1 )
				*fValue += pDynOp->vValue[0];
			else
				*fValue += pDynOp->vValue[uiValueIndex];
		}
		xcase DynOpType_Multiply:
		{

			if ( pDynOp->uiNumValuesSpecd == 1 )
				*fValue *= pDynOp->vValue[0];
			else
				*fValue *= pDynOp->vValue[uiValueIndex];
		}
		xcase DynOpType_Min:
		{
			if ( pDynOp->uiNumValuesSpecd == 1 )
				*fValue = MIN(pDynOp->vValue[0], *fValue);
			else
				*fValue = MIN(pDynOp->vValue[uiValueIndex], *fValue);
		}
		xcase DynOpType_Max:
		{
			if ( pDynOp->uiNumValuesSpecd == 1 )
				*fValue = MAX(pDynOp->vValue[0], *fValue);
			else
				*fValue = MAX(pDynOp->vValue[uiValueIndex], *fValue);
		}
	}
}


bool dynFxApplyF32DynOps(F32* fValue, const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex, U32 uiValueIndex, const DynParticle* pParentParticle)
{
	const U32 uiNumDynOps = eaSize(&pDynObjectInfo->eaDynOps);
	U32 uiDynOpIndex;
	bool bApplied = false;
	for (uiDynOpIndex=0; uiDynOpIndex<uiNumDynOps; ++uiDynOpIndex)
	{
		DynOperator* pDynOp = pDynObjectInfo->eaDynOps[uiDynOpIndex];
		if ( (int)uiTokenIndex == pDynOp->iParseColumn )
		{
			CHECK_FINITE(*fValue);
			dynFxApplyF32DynOp(fValue, pDynOp, uiValueIndex, pParentParticle);
			CHECK_FINITE(*fValue);
			bApplied = true;
		}
	}
	return bApplied;
}

static void dynFxApplyStaticDynOp(void* pValue, DynOperator* pDynOp, const DynParticle* pParentParticle)
{
	switch (pDynOp->eOpType)
	{
		xcase DynOpType_Jitter:
		{
			switch ( ParseDynObject[pDynOp->iParseColumn].type & TOK_TYPE_MASK )
			{
				xcase TOK_BOOLFLAG_X:
				case TOK_BOOL_X:
				{
					bool bResult = randomBoolSeeded(NULL, RandType_BLORN);
					memcpy(pValue, &bResult, sizeof(bResult));
				}
			}
		}
		xcase DynOpType_SphereJitter:
		case DynOpType_SphereShellJitter:
		case DynOpType_Add:
		case DynOpType_Multiply:
		case DynOpType_Min:
		case DynOpType_Max:
		{
			Errorf("DynOp Type %s not supported with token %s!", ParseDynOpType[pDynOp->eOpType].key, ParseDynObject[pDynOp->iParseColumn].name);

		}
		xcase DynOpType_Inherit:
		{
			U32 uiEntryIndex;
			if ( pParentParticle )
			{
				for (uiEntryIndex=0; uiEntryIndex<pParentParticle->uiNumEntries; ++uiEntryIndex)
				{
					if ( pParentParticle->pEntries[uiEntryIndex].uiTokenIndex == (U32)pDynOp->iParseColumn )
					{
						memcpy(pValue, pParentParticle->pData + pParentParticle->pEntries[uiEntryIndex].uiOffset, pParentParticle->pEntries[uiEntryIndex].uiSize);
					}
				}
			}
		}
	}
}

bool dynFxApplyStaticDynOps(void* pValue, const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex, const DynParticle* pParentParticle)
{
	const U32 uiNumDynOps = eaSize(&pDynObjectInfo->eaDynOps);
	U32 uiDynOpIndex;
	bool bApplied = false;
	for (uiDynOpIndex=0; uiDynOpIndex<uiNumDynOps; ++uiDynOpIndex)
	{
		DynOperator* pDynOp = pDynObjectInfo->eaDynOps[uiDynOpIndex];
		if ( (int)uiTokenIndex == pDynOp->iParseColumn )
		{
			dynFxApplyStaticDynOp(pValue, pDynOp, pParentParticle);
			bApplied = true;
		}
	}
	return bApplied;
}

static void dynFxApplyQuatDynOp(Quat qValue, DynOperator* pDynOp )
{
	switch (pDynOp->eOpType)
	{
		xcase DynOpType_Jitter:
		{
			Vec3 vRandom;
			Quat qTemp, qTemp2;
			randomVec3Seeded(NULL, RandType_BLORN, vRandom);
			mulVecVec3(vRandom, pDynOp->vValue, vRandom);
			RADVEC3(vRandom);
			PYRToQuat(vRandom, qTemp);
			quatMultiply(qValue, qTemp, qTemp2);
			copyQuat(qTemp2, qValue);
		}
		xcase DynOpType_Inherit:
		{
		}
		xcase DynOpType_Add:
		{
			Errorf("You can't add rotations, maybe you mean multiply?");
		}
		xcase DynOpType_Multiply:
		{
			Vec3 vTemp;
			Quat qTemp, qTemp2;
			copyVec3(pDynOp->vValue, vTemp);
			RADVEC3(vTemp);
			PYRToQuat(vTemp, qTemp);
			quatMultiply(qValue, qTemp, qTemp2);
			copyQuat(qTemp2, qValue);
		}
		xcase DynOpType_Min:
		{
			Errorf("You can't get the min of a rotation, maybe you mean multiply?");
		}
		xcase DynOpType_Max:
		{
			Errorf("You can't get the max of a rotation, maybe you mean multiply?");
		}
	}
}

bool dynFxApplyQuatDynOps(Quat qValue, const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex)
{
	const U32 uiNumDynOps = eaSize(&pDynObjectInfo->eaDynOps);
	U32 uiDynOpIndex;
	bool bApplied = false;
	for (uiDynOpIndex=0; uiDynOpIndex<uiNumDynOps; ++uiDynOpIndex)
	{
		DynOperator* pDynOp = pDynObjectInfo->eaDynOps[uiDynOpIndex];
		if ( (int)uiTokenIndex == pDynOp->iParseColumn )
		{
			dynFxApplyQuatDynOp(qValue, pDynOp);
			bApplied = true;
		}
	}
	return bApplied;
}


static bool dynOpValueSet(F32 fValue)
{
	return ((int)fValue) != UNINIT_VALUE;
}

void dynFxApplyMultiVal( eDynParamType edpt, MultiVal* pMv, void* pTargetPtr ) 
{
	switch (edpt)
	{
		bool bSuccess;
		xcase edptString:
		{
			char* pcResult;
			const char *pcCached;
			pcResult = (char*)MultiValGetAscii(pMv, &bSuccess);			
			if ( bSuccess ) {
				pcCached = allocAddString(pcResult);
				memcpy(pTargetPtr, &pcCached, sizeof(char*));
			}
		}
		xcase edptInteger:
		{
			int iResult = MultiValGetInt(pMv, &bSuccess);
			if ( bSuccess )
			{
				memcpy(pTargetPtr, &iResult, sizeof(int));
			}
		}
		xcase edptVector2:
		{
			// Pull out a Vec3, but apply as a Vec2.
			Vec3* pvResult = MultiValGetVec3(pMv, &bSuccess);
			if ( bSuccess && pvResult)
			{
				memcpy(pTargetPtr, pvResult, sizeof(Vec2));
			}
		}
		xcase edptVector:
		{
			Vec3* pvResult = MultiValGetVec3(pMv, &bSuccess);
			if ( bSuccess && pvResult)
			{
				memcpy(pTargetPtr, pvResult, sizeof(Vec3));
			}
		}
		xcase edptVector4:
		{
			Vec4* pvResult = MultiValGetVec4(pMv, &bSuccess);
			if ( bSuccess && pvResult )
			{
				memcpy(pTargetPtr, pvResult, sizeof(Vec4));
			}
		}
		xcase edptNumber:
		{
			F32 fResult = MultiValGetFloat(pMv, &bSuccess);
			if ( bSuccess )
			{
				memcpy(pTargetPtr, &fResult, sizeof(fResult));
			}
		}
		xcase edptBool:
		{
			bool bResult = MultiValGetInt(pMv, &bSuccess);
			if ( bSuccess )
			{
				memcpy(pTargetPtr, &bResult, sizeof(bResult));
			}
		}
		xcase edptQuat:
		{
			Vec3* pvResult = MultiValGetVec3(pMv, &bSuccess);
			if ( bSuccess && pvResult )
			{
				Vec3 vResult;
				copyVec3(*pvResult, vResult);
				RADVEC3(vResult);
				PYRToQuat(vResult, *((Quat*)pTargetPtr));
			}
		}
	}
}


U32 dynFxApplyCopyParamsGeneral( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, void* pTargetPtr, ParseTable* pParseTable)
{
	U32 uiApplyParamIndex;
	const U32 uiNumPassParams = eaUSize(&pParamBlock->eaDefineParams);
	U32 bRet = 0;
	for (uiApplyParamIndex=0; uiApplyParamIndex<uiNumParams; ++uiApplyParamIndex)
	{
		DynApplyParam* pApplyParam = eaParams[uiApplyParamIndex];
		// Only support copying
		if (pApplyParam->eParamOp == edpoCopy && pApplyParam->uiTokenIndex == uiTokenIndex)
		{
			U32 uiPassParamIndex;
			for (uiPassParamIndex=0; uiPassParamIndex<uiNumPassParams; ++uiPassParamIndex)
			{
				DynDefineParam* pDefineParam = pParamBlock->eaDefineParams[uiPassParamIndex];
				MultiVal* pMv = &pDefineParam->mvVal;
				eDynParamType edpt = pApplyParam->edpt;
				if ( pDefineParam->pcParamName != pApplyParam->pcParamName )
					continue;
				dynFxApplyMultiVal(edpt, pMv, pTargetPtr);
				bRet = 1;
			}
		}
	}
	return bRet;
}

U32 dynFxSelectRandomDynListNode(DynListNode** eaNodes, bool bEqualChance)
{
	const U32 uiNumOptions = eaUSize(&eaNodes);
	U32 uiResult;

	if ( bEqualChance )
	{
		// Pick one at random, copy it to target ptr
		uiResult = randomU32() % uiNumOptions;
	}
	else
	{
		// Pick a random jitter list node based on node weights
		F32 fValue = randomPositiveF32();
		F32 fTotalChance = eaNodes[0]->fChance;
		uiResult = 0;
		while (fValue > fTotalChance)
		{
			++uiResult;
			if ( uiResult >= uiNumOptions )
			{
				uiResult = uiNumOptions-1;
				break;
			}
			fTotalChance += eaNodes[uiResult]->fChance;
		}
	}
	return uiResult;
}

static void dynFxApplyJitterList( SA_PARAM_NN_VALID DynJitterList* pJList, void* pTargetPtr, dynJitterListSelectionCallback cbFunc, void* cbData ) 
{
	const U32 uiNumOptions = pJList->pList?eaUSize(&pJList->pList->eaNodes):0;
	U32 uiResult;
	if (!uiNumOptions)
		return;
	
	if ( cbFunc )
	{
		// If a callback function is specified, use that
		uiResult = cbFunc(pJList, cbData);
	}
	else
	{
		uiResult = dynFxSelectRandomDynListNode(pJList->pList->eaNodes, pJList->pList->bEqualChance);
	}
	dynFxApplyMultiVal(pJList->edpt, &pJList->pList->eaNodes[uiResult]->mvVal, pTargetPtr);
}

void dynFxApplyJitterLists( const U32 uiNumJLists, DynJitterList** eaJLists, U32 uiTokenIndex, void* pTargetPtr, ParseTable* pParseTable, dynJitterListSelectionCallback cbFunc, void* cbData )
{
	U32 uiJListIndex;
	for ( uiJListIndex=0; uiJListIndex<uiNumJLists; ++uiJListIndex )
	{
		DynJitterList* pJList = eaJLists[uiJListIndex];
		if ( pJList->uiTokenIndex == uiTokenIndex )
		{
			dynFxApplyJitterList(pJList, pTargetPtr, cbFunc, cbData);
		}
	}
}

void dynFxApplyF32JitterList( const U32 uiNumJLists, DynJitterList** eaJLists, U32 uiTokenIndex, F32* pFloat, ParseTable* pParseTable, U8 uiWhichFloat)
{
	U32 uiJListIndex;
	for ( uiJListIndex=0; uiJListIndex<uiNumJLists; ++uiJListIndex )
	{
		DynJitterList* pJList = eaJLists[uiJListIndex];
		if ( pJList->uiTokenIndex == uiTokenIndex )
		{
			switch ( pJList->edpt )
			{
				xcase edptNumber:
				{
					dynFxApplyJitterList(pJList, pFloat, NULL, NULL);
					CHECK_FINITE(*pFloat);
				}
				xcase edptVector:
				{
					static Vec3 vResult;
					if ( uiWhichFloat == 0 )
						dynFxApplyJitterList(pJList, vResult, NULL, NULL);
					*pFloat = vResult[uiWhichFloat];
					CHECK_FINITE(*pFloat);
				}
				xcase edptVector2:
				{
					static Vec2 vResult;
					if ( uiWhichFloat == 0 )
						dynFxApplyJitterList(pJList, vResult, NULL, NULL);
					*pFloat = vResult[uiWhichFloat];
					CHECK_FINITE(*pFloat);
				}
				xcase edptVector4:
				{
					static Vec4 vResult;
					if ( uiWhichFloat == 0 )
						dynFxApplyJitterList(pJList, vResult, NULL, NULL);
					*pFloat = vResult[uiWhichFloat];
					CHECK_FINITE(*pFloat);
				}
				xcase edptQuat:
				{
					static Quat qResult;
					if ( uiWhichFloat == 0 )
						dynFxApplyJitterList(pJList, qResult, NULL, NULL);
					*pFloat = qResult[uiWhichFloat];
					CHECK_FINITE(*pFloat);
				}
				xcase edptString:
				{
					Errorf("Can not apply dynFxApplyF32JitterList to string token %s", pJList->pcTokenString);
					return;
				}
			}
		}
	}
}

void dynFxApplyF32Params( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, F32* pFloat, U8 uiWhichFloat)
{
	U32 uiApplyParamIndex;
	const U32 uiNumPassParams = eaUSize(&pParamBlock->eaDefineParams);
	for (uiApplyParamIndex=0; uiApplyParamIndex<uiNumParams; ++uiApplyParamIndex)
	{
		DynApplyParam* pApplyParam = eaParams[uiApplyParamIndex];
		if (pApplyParam->uiTokenIndex == uiTokenIndex)
		{
			eDynParamType edpt = dynFxParamTypeFromToken(pApplyParam->uiTokenIndex);
			U32 uiPassParamIndex;
			for (uiPassParamIndex=0; uiPassParamIndex<uiNumPassParams; ++uiPassParamIndex)
			{
				DynDefineParam* pDefineParam = pParamBlock->eaDefineParams[uiPassParamIndex];
				F32 fValue = 0.0;
				bool bSuccess = false;
				if ( pDefineParam->pcParamName != pApplyParam->pcParamName )
					continue;

				// Found matching define and apply params
				switch (pDefineParam->mvVal.type)
				{
					xcase MULTI_FLOAT:
					{
						fValue = pDefineParam->mvVal.floatval;
						bSuccess = true;
					}
					xcase MULTI_VEC3:
					case MULTI_VEC3_F:
					{
						Vec3* pvResult = MultiValGetVec3(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvResult )
							fValue = (*pvResult)[uiWhichFloat];
					}
					xcase MULTI_VEC4:
					case MULTI_VEC4_F:
					{
						Vec4* pvResult = MultiValGetVec4(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvResult )
							fValue = (*pvResult)[uiWhichFloat];
					}
				}
				CHECK_FINITE(fValue);

				if ( bSuccess )
				{
					switch (pApplyParam->eParamOp)
					{
						xcase edpoCopy:
						{
							*pFloat = fValue;
						}
						xcase edpoAdd:
						{
							*pFloat += fValue;
						}
						xcase edpoMultiply:
						{
							*pFloat *= fValue;
						}
					}
					CHECK_FINITE(*pFloat);
				}
			}
		}
	}
}

void dynFxApplyQuatParams( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, Quat q)
{
	U32 uiApplyParamIndex;
	const U32 uiNumPassParams = eaUSize(&pParamBlock->eaDefineParams);
	for (uiApplyParamIndex=0; uiApplyParamIndex<uiNumParams; ++uiApplyParamIndex)
	{
		DynApplyParam* pApplyParam = eaParams[uiApplyParamIndex];
		if (pApplyParam->uiTokenIndex == uiTokenIndex)
		{
			eDynParamType edpt = dynFxParamTypeFromToken(pApplyParam->uiTokenIndex);
			U32 uiPassParamIndex;
			for (uiPassParamIndex=0; uiPassParamIndex<uiNumPassParams; ++uiPassParamIndex)
			{
				DynDefineParam* pDefineParam = pParamBlock->eaDefineParams[uiPassParamIndex];
				Quat qValue;
				bool bSuccess = false;
				if ( pDefineParam->pcParamName != pApplyParam->pcParamName )
					continue;

				// Found matching define and apply params
				if ( ( pDefineParam->mvVal.type == MULTI_VEC3 || pDefineParam->mvVal.type == MULTI_VEC3_F) && pApplyParam->edpt == edptQuat )
				{
					Vec3* pvResult = MultiValGetVec3(&pDefineParam->mvVal, &bSuccess);
					if ( bSuccess && pvResult )
					{
						Vec3 vResult;
						copyVec3(*pvResult, vResult);
						RADVEC3(vResult);
						PYRToQuat(vResult, qValue);
						copyQuat(qValue, q);
					}
				}
				else
				{
					Errorf("Can't apply Param %s to Token %s", pApplyParam->pcParamName, pApplyParam->pcTokenString);
				}
			}
		}
	}
}

/*
void dynFxPushParamDynOp( DynApplyParam* pParam, Vec3 vValue, U8 uiNumValuesSpeced, DynObjectInfo* pObjInfo ) 
{
	DynOperator* pNewOp = StructAlloc(sizeof(DynOperator));
	pNewOp->iLineNum = pParam->iLineNum;
	pNewOp->iParseColumn = pParam->uiTokenIndex;
	if ( pParam->eParamOp == edpoAdd )
		pNewOp->eOpType = DynOpType_Add;
	else
		pNewOp->eOpType = DynOpType_Multiply;
	pNewOp->uiNumValuesSpecd = uiNumValuesSpeced;
	copyVec3(vValue, pNewOp->vValue);
	pNewOp->pcTokenString = pParam->pcTokenString;
	eaPush(&pObjInfo->eaDynOps, pNewOp);
}
*/

static void dynFxApplyParamToObjInfo( DynApplyParam* pParam, DynObjectInfo* pObjInfo, const char* pcFileName, DynFxInfo* pInfo ) 
{
	// First, find corresponding DefaultParams
	DynDefineParam* pDefineParam = NULL;
	{
		U32 uiIndex;
		for (uiIndex=0; !pDefineParam && uiIndex< eaUSize(&pInfo->paramBlock.eaDefineParams); ++uiIndex)
		{
			DynDefineParam* pTempDefineParam = pInfo->paramBlock.eaDefineParams[uiIndex];
			if ( pTempDefineParam->pcParamName == pParam->pcParamName )
				pDefineParam = pTempDefineParam;
		}
	}
	if ( pDefineParam )
	{
		if (pObjInfo->bfParamsSpecified)
			SETB(pObjInfo->bfParamsSpecified, pParam->uiTokenIndex);
		else
			FxFileError(pcFileName, "Unhandled lack of specified params bitfield!");

		switch (pParam->eParamOp)
		{
			xcase edpoCopy:
			{
				switch (pParam->edpt)
				{
					bool bSuccess;
					xcase edptString:
					{
						char* pcResult;
						pcResult = (char*)MultiValGetAscii(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess )
							TokenStoreSetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, pcResult, NULL);
					}
					xcase edptVector:
					{
						Vec3* pvResult = MultiValGetVec3(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvResult)
						{
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							memcpy(pValue, (*pvResult), sizeof(Vec3));
						}
					}
					xcase edptVector2:
					{
						// Pulls a Vec3 out of the multival. Applies as Vec2.
						Vec3* pvResult = MultiValGetVec3(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvResult)
						{
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							memcpy(pValue, (*pvResult), sizeof(Vec2));
						}
					}
					xcase edptVector4:
					{
						Vec4* pvResult = MultiValGetVec4(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvResult)
						{
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							memcpy(pValue, (*pvResult), sizeof(Vec4));
						}
					}
					xcase edptNumber:
					{
						F32 fResult = MultiValGetFloat(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess )
						{
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							memcpy(pValue, &fResult, sizeof(fResult));
						}
					}
					xcase edptInteger:
					{
						int iResult = MultiValGetInt(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess )
						{
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							memcpy(pValue, &iResult, sizeof(iResult));
						}
					}
					xcase edptBool:
					{
						bool bResult = MultiValGetFloat(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess )
						{
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							memcpy(pValue, &bResult, sizeof(bResult));
						}
					}
					xcase edptQuat:
					{
						Vec3* pvResult = MultiValGetVec3(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvResult)
						{
							Vec3 vResult;
							void* pValue = TokenStoreGetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, NULL);
							copyVec3(*pvResult, vResult);
							RADVEC3(vResult);
							PYRToQuat(vResult, *((Quat*)pValue));
						}
					}
				}
			}

			xcase edpoAdd:
			{
				if(dynFxParamTypeFromToken(pParam->uiTokenIndex) == edptStringArray) {
					char ***pValue = (char***)TokenStoreGetEArray(ParseDynObject, pParam->uiTokenIndex, pObjInfo, NULL);
					char *pcResult;
					bool bSuccess;
					pcResult = (char*)MultiValGetAscii(&pDefineParam->mvVal, &bSuccess);
					if(bSuccess) {
						printf("HIT CODE FOR STRINGARRAY ADD %s\n", pcResult);
						eaPush(pValue, (char*)allocAddString(pcResult));
						// TokenStoreSetPointer(ParseDynObject, pParam->uiTokenIndex, pObjInfo, 0, pValue, NULL);
					}
				}
			}
			xcase edpoMultiply:
			{
			}
				
			/*
			{
				bool bSuccess;
				switch ( pDefineParam->mvVal.type )
				{
					xcase MULTI_FLOAT:
					{
						Vec3 vValue;
						vValue[0] = MultiValGetFloat(&pDefineParam->mvVal, &bSuccess);
						vValue[1] = UNINIT_VALUE;
						vValue[2] = UNINIT_VALUE;
						if ( bSuccess )
						{
							// Push a dyn op
							dynFxPushParamDynOp(pParam, vValue, 1, pObjInfo);
						}
					}
					xcase MULTI_VEC3:
					{
						Vec3* pvValue = MultiValGetVec3(&pDefineParam->mvVal, &bSuccess);
						if ( bSuccess && pvValue )
						{
							// Push a dyn op
							dynFxPushParamDynOp(pParam, *pvValue, 3, pObjInfo);
						}
					}
					xdefault:
					{
					}
				}
			}
			*/
		}
	}
}

void dynFxInfoGetAllNames(const char*** pppcDynFxNames)
{
	ResourceDictionaryInfo *resDictInfo = resDictGetInfo("DynFxInfo");
	int i;
	for (i = 0; i < eaSize(&resDictInfo->ppInfos); i++)
	{
		ResourceInfo *resInfo = resDictInfo->ppInfos[i];
		if (resInfo->resourceName)
			eaPush(pppcDynFxNames, allocAddString(resInfo->resourceName));
	}
}

static bool dynFxInfoExistsServerSide(const char* pcDynFxName);

bool dynFxInfoExists(const char* pcDynFxName)
{
	if (IsServer())
		return dynFxInfoExistsServerSide(pcDynFxName);
	if (!dynDebugState.bNoNewFx && !resGetLocationID(hDynFxInfoDict, (char*)pcDynFxName))
	{
		return !!(RefSystem_ReferentFromString(hDynFxInfoDict, pcDynFxName));
	}
	return true;
}


//
// These next few functions are for server side verification of the existence of FX
//

StashTable stServerSideFxInfoTable = NULL;

static FileScanAction addFxNameToTable(char* dir, struct _finddata32_t* data, void *pUserData)
{
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;

	{
		char cName[256];
		getFileNameNoExt(cName, data->name);
		stashAddInt(stServerSideFxInfoTable, allocAddString(cName), 1, false);
	}
	return FSA_EXPLORE_DIRECTORY;
}

static void dynFxFileNameOnlyReloadCallback(const char *relpath, int when)
{
	dynFxLoadFileNamesOnly();
}

void dynFxLoadFileNamesOnly(void)
{
	if (isProductionMode())
		return;
	loadstart_printf("Loading DynFx names...");
	if (!stServerSideFxInfoTable)
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/fx/*.dfx", dynFxFileNameOnlyReloadCallback);
	}
	else
		stashTableDestroy(stServerSideFxInfoTable);
	stServerSideFxInfoTable = stashTableCreateWithStringKeys(8192, StashDefault);
	fileScanAllDataDirs("dyn/fx", addFxNameToTable, NULL);
	loadend_printf(" done.");
}


static bool dynFxInfoExistsServerSide(const char* pcDynFxName)
{
	if (isProductionMode())
		return true;
	if (!stServerSideFxInfoTable)
	{
		Errorf("Trying to call dynFxInfoExistsServerSide() before dynFxLoadFileNamesOnly() was called. Check the startup dependencies and that the WL startup flag WL_LOAD_DYNFX_NAMES_FOR_VERIFICATION is set.");
		return true;
	}
	return stashFindInt(stServerSideFxInfoTable, pcDynFxName, NULL);
}




int dynFxInfoGetPriorityLevel(const char* pcDynFxName)
{
	int iResult;
	REF_TO(DynFxInfo) hTemp;
	DynFxInfo *pTemp;
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pcDynFxName, hTemp);
	pTemp = GET_REF(hTemp);
	iResult = pTemp?pTemp->iPriorityLevel:-1;
	REMOVE_HANDLE(hTemp);
	return iResult;
}

int dynFxInfoGetDropPriorityLevel(const char* pcDynFxName)
{
	int iResult;
	REF_TO(DynFxInfo) hTemp;
	DynFxInfo *pTemp;
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pcDynFxName, hTemp);
	pTemp = GET_REF(hTemp);
	iResult = pTemp?pTemp->iDropPriorityLevel:-1;
	REMOVE_HANDLE(hTemp);
	return iResult;
}

F32 dynFxInfoGetDrawDistance(const char* pcDynFxName)
{
	F32 fResult;
	REF_TO(DynFxInfo) hTemp;
	DynFxInfo *pTemp;
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pcDynFxName, hTemp);
	pTemp = GET_REF(hTemp);
	fResult = pTemp?pTemp->fDrawDistance:0.0f;
	REMOVE_HANDLE(hTemp);
	return fResult;
}

const char* dynFxInfoGetFileName(const char* pcDynFxName)
{
	const char* pcResult;
	REF_TO(DynFxInfo) hTemp;
	DynFxInfo *pTemp;
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pcDynFxName, hTemp);
	pTemp = GET_REF(hTemp);
	pcResult = pTemp?allocAddString(pTemp->pcFileName):NULL;
	REMOVE_HANDLE(hTemp);
	return pcResult;
}

void dynFxInfoToggleDebug(const char* pcDynFxName)
{
	REF_TO(DynFxInfo) hTemp;
	DynFxInfo *pTemp;
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pcDynFxName, hTemp);
	pTemp = GET_REF(hTemp);
	if (pTemp)
		pTemp->bDebugFx = !(pTemp->bDebugFx);
	REMOVE_HANDLE(hTemp);
}


int dynEventIndexFind(const DynFxInfo* pFxInfo, const char* pcMessageName)
{
	// is unlikely to have more than 1-4 events
	int iNumEvents = eaSize(&pFxInfo->events);
	int iIndex;
	for (iIndex=0; iIndex<iNumEvents; ++iIndex)
	{
		const DynEvent* pEvent = pFxInfo->events[iIndex];
		if ( pEvent->pcMessageType == pcMessageName )
			return iIndex;
	}

	if (pcMessageName == pcKillMessage)
		return DYNFX_KILL_MESSAGE_INDEX;

	return -1;  // could not find message
}

static bool dynListVerify(DynList* pList, const char* pcFileName)
{
	const U32 uiNumListNodes = eaUSize(&pList->eaNodes);
	U32 uiNodeIndex;
	F32 fTotalChance;
	F32 fEqualChance;

	pList->bEqualChance = true;

	if ( !uiNumListNodes )
	{
		FxFileError(pcFileName, "Zero-item list not allowed, please check List %s", pList->pcListName);
		return false;
	}

	pList->mvType = pList->eaNodes[0]->mvVal.type;
	fEqualChance = fTotalChance = pList->eaNodes[0]->fChance;

	for (uiNodeIndex=1; uiNodeIndex<uiNumListNodes; ++uiNodeIndex)
	{
		if ( pList->eaNodes[uiNodeIndex]->mvVal.type != pList->mvType )
		{
			FxFileError(pcFileName, "All item types in list %s must be the same!", pList->pcListName);
			return false;
		}
		if ( fEqualChance != pList->eaNodes[uiNodeIndex]->fChance )
			pList->bEqualChance = false;
		fTotalChance += pList->eaNodes[uiNodeIndex]->fChance;
	}

	// Normalize the chances in all of the nodes
	if ( fTotalChance <= 0.0f )
	{
			FxFileError(pcFileName, "Chance totals must exceed zero in list %s!", pList->pcListName);
			return false;
	}

	fTotalChance = 1.0f / fTotalChance;

	for (uiNodeIndex=0; uiNodeIndex<uiNumListNodes; ++uiNodeIndex)
	{
		pList->eaNodes[uiNodeIndex]->fChance *= fTotalChance;
	}

	return true;
}

static bool dynMNCRenameVerify(DynMNCRename* pMNCRename, const char* pcFileName)
{
	if (
		pMNCRename->pcBefore != allocAddString("Color1")
		&& pMNCRename->pcBefore != allocAddString("Color2")
		&& pMNCRename->pcBefore != allocAddString("Color3")
		)
	{
		FxFileError(pcFileName, "MatConstRename must rename Color1, Color2 or Color3... %s is invalid.", pMNCRename->pcBefore);
		return false;
	}
	return true;
}

static bool dynLoopVerify(DynLoop* pLoop, const char* pcFileName)
{
	// Verify fx name, somehow, eventually

	return true;
}

static bool dynRaycastVerify(DynRaycast* pRaycast, DynFxInfo* pInfo, const char* pcFileName)
{
	if (strnicmp(pRaycast->pcTag, "Ray", 3) != 0)
	{
		FxFileError(pcFileName, "Raycast %s must start with the word 'Ray'!", pRaycast->pcTag);
		return false;
	}
	pRaycast->bCheckPhysProps = false;

	if (eaSize(&pRaycast->eaHitEvent) > 32)
	{
		FxFileError(pcFileName, "Raycast %s exceeds hit event limit of 32! Wow!", pRaycast->pcTag);
		return false;
	}


	FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pHitEvent)
		// Verify loop stops and starts
		FOR_EACH_IN_EARRAY(pHitEvent->eaLoopStart, DynLoopRef, pLoopStart)
			FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
				if (stricmp(pLoopStart->pcTag, pLoop->pcLoopTag)==0)
				{
					pLoopStart->pLoop = pLoop;
					break;
				}
			FOR_EACH_END
			if (!pLoopStart->pLoop)
			{
				FxFileError(pcFileName, "Can't find Loop %s", pLoopStart->pcTag);
				return false;
			}
		FOR_EACH_END

		// Verify emitter stops and starts
		FOR_EACH_IN_EARRAY(pHitEvent->eaEmitterStart, DynParticleEmitterRef, pEmitterStart)
			FOR_EACH_IN_EARRAY(pInfo->eaEmitters, DynParticleEmitter, pEmitter)
				if (stricmp(pEmitterStart->pcTag, pEmitter->pcTag)==0)
				{
					//pInfo->bDontDraw = false;
					pEmitterStart->pEmitter = pEmitter;
					break;
				}
			FOR_EACH_END
			if (!pEmitterStart->pEmitter)
			{
				FxFileError(pcFileName, "Can't find Emitter %s", pEmitterStart->pcTag);
				return false;
			}
		FOR_EACH_END
		if (eaSize(&pHitEvent->eaHitTypes) > 0)
			pRaycast->bCheckPhysProps = true;
	FOR_EACH_END
	return true;
}

static bool dynContactEventVerify(DynContactEvent* pContactEvent, DynFxInfo* pInfo, const char* pcFileName)
{
	/*
		// Verify loop stops and starts
		FOR_EACH_IN_EARRAY(pHitEvent->eaLoopStart, DynLoopRef, pLoopStart)
			FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
				if (stricmp(pLoopStart->pcTag, pLoop->pcLoopTag)==0)
				{
					pLoopStart->pLoop = pLoop;
					break;
				}
			FOR_EACH_END
			if (!pLoopStart->pLoop)
			{
				FxFileError(pcFileName, "Can't find Loop %s", pLoopStart->pcTag);
				return false;
			}
		FOR_EACH_END
		*/

	// Verify emitter stops and starts
	FOR_EACH_IN_EARRAY(pContactEvent->eaEmitterStart, DynParticleEmitterRef, pEmitterStart)
	{
		FOR_EACH_IN_EARRAY(pInfo->eaEmitters, DynParticleEmitter, pEmitter)
		{

			if (stricmp(pEmitterStart->pcTag, pEmitter->pcTag)==0)
			{
				//pInfo->bDontDraw = false;
				pEmitterStart->pEmitter = pEmitter;
				break;
			}
		}
		FOR_EACH_END;
		if (!pEmitterStart->pEmitter)
		{
			FxFileError(pcFileName, "Can't find Emitter %s", pEmitterStart->pcTag);
			return false;
		}
	}
	FOR_EACH_END;
		/*
		if (eaSize(&pHitEvent->eaHitTypes) > 0)
			pRaycast->bCheckPhysProps = true;
			*/

	return true;
}

static bool dynJitterListVerify(DynJitterList* pJList, const char* pcFileName, DynFxInfo* pInfo, const ParseTable* pParseTable, DynKeyFrame* pKeyFrame, DynFxDeps* pDeps)
{
	// Get Token type
	pJList->uiTokenIndex = dynTokenIndexFromStringGeneral(pJList->pcTokenString, pParseTable);
	if (pJList->uiTokenIndex == 0xFFFFFFFF)
	{
		FxFileError(pcFileName, "Error Line %d: Invalid Token %s in JitterList line", pJList->iLineNum, pJList->pcTokenString);
		return false;
	}

	// Find the corresponding DynList
	{
		U32 uiListIndex;
		const U32 uiNumLists = eaUSize(&pInfo->eaLists);
		bool bFound=false;
		for (uiListIndex=0; !bFound && uiListIndex<uiNumLists; ++uiListIndex)
		{
			if ( pInfo->eaLists[uiListIndex]->pcListName == pJList->pcJListName )
			{
				pJList->pList = pInfo->eaLists[uiListIndex];
				bFound = true;
			}
		}
		if (!bFound)
		{
			FxFileError(pcFileName, "Error Line %d: Failed to find JitterList %s", pJList->iLineNum, pJList->pcJListName);
			return false;
		}
	}

	pJList->edpt = dynFxParamTypeFromTokenGeneral(pJList->uiTokenIndex, pParseTable);

	switch( pJList->edpt )
	{
		xcase edptString:
		{
			if ( pJList->pList->mvType != MULTI_STRING )
			{
				FxFileError(pcFileName, "Line %d: Token type %s requires a List with type STR members", pJList->iLineNum, pJList->pcTokenString);
				return false;
			}
		}
		xcase edptNumber:
		{
			if ( pJList->pList->mvType != MULTI_FLOAT )
			{
				FxFileError(pcFileName, "Line %d: Token type %s requires a List with type FLT members", pJList->iLineNum, pJList->pcTokenString);
				return false;
			}
		}
		xcase edptVector:
		{
			if ( pJList->pList->mvType != MULTI_VEC3 )
			{
				FxFileError(pcFileName, "Line %d: Token type %s requires a List with type VEC members", pJList->iLineNum, pJList->pcTokenString);
				return false;
			}
		}
		xcase edptVector2:
		{
			if ( pJList->pList->mvType != MULTI_VEC3 )
			{
				FxFileError(pcFileName, "Line %d: Token type %s requires a List with type VEC members", pJList->iLineNum, pJList->pcTokenString);
				return false;
			}
		}
		xcase edptVector4:
		{
			if ( pJList->pList->mvType != MULTI_VEC4 )
			{
				FxFileError(pcFileName, "Line %d: Token type %s requires a List with type VC4 members", pJList->iLineNum, pJList->pcTokenString);
				return false;
			}
		}
		xcase edptInteger:
		{
			if ( pJList->pList->mvType != MULTI_INT )
			{
				FxFileError(pcFileName, "Line %d: Token type %s requires a List with type INT members", pJList->iLineNum, pJList->pcTokenString);
				return false;
			}
		}
	}

	// note that if the parse table is not the ParseDynObject table, it will get confused about these token indices.
	// I'm dealing with this by only passing in the pDeps pointer if the parse table is correct
	if (pDeps)
	{
		assert(pParseTable == ParseDynObject);
		if (pJList->uiTokenIndex == uiGeometryTokenIndex)
		{
			FOR_EACH_IN_EARRAY(pJList->pList->eaNodes, DynListNode, pNode)
			{
				char* pcResult;
				bool bSuccess = false;
				pcResult = (char*)MultiValGetAscii(&pNode->mvVal, &bSuccess);
				if (stricmp(pcResult, "Footl")==0)
				{
					int stupid = 1;
				}
				if (bSuccess)
				{
					int index = eaPush(&pDeps->geometry_deps, pcResult);
					if (pKeyFrame->objInfo[edoValue].obj.draw.pcMaterialName)
						eaiSet(&pDeps->geometry_dep_flags, eaiGet(&pDeps->geometry_dep_flags, index)|eDynFxDepFlag_MaterialNotUsed, index);
				}
			}
			FOR_EACH_END;
		}
		else if (pJList->uiTokenIndex == uiTextureTokenIndex || pJList->uiTokenIndex == uiTexture2TokenIndex)
		{
			FOR_EACH_IN_EARRAY(pJList->pList->eaNodes, DynListNode, pNode)
			{
				char* pcResult;
				bool bSuccess = false;
				pcResult = (char*)MultiValGetAscii(&pNode->mvVal, &bSuccess);
				if (stricmp(pcResult, "Footl")==0)
				{
					int stupid = 1;
				}
				if (bSuccess)
				{
					int index = eaPush(&pDeps->texture_deps, pcResult);
					if (pJList->uiTokenIndex == uiTextureTokenIndex && pKeyFrame->objInfo[edoValue].obj.splatInfo.eType != eDynSplatType_None)
						eaiSet(&pDeps->texture_dep_flags, eaiGet(&pDeps->texture_dep_flags, index)|eDynFxDepFlag_UsedOnSplat, index);
				}
			}
			FOR_EACH_END;
		}
		else if (pJList->uiTokenIndex == uiMaterialTokenIndex)
		{
			FOR_EACH_IN_EARRAY(pJList->pList->eaNodes, DynListNode, pNode)
			{
				char* pcResult;
				bool bSuccess = false;
				pcResult = (char*)MultiValGetAscii(&pNode->mvVal, &bSuccess);
				if (stricmp(pcResult, "Footl")==0)
				{
					int stupid = 1;
				}
				if (bSuccess)
				{
					int index = eaPush(&pDeps->material_deps, pcResult);
					if (pKeyFrame->objInfo[edoValue].obj.splatInfo.eType != eDynSplatType_None)
						eaiSet(&pDeps->material_dep_flags, eaiGet(&pDeps->material_dep_flags, index)|eDynFxDepFlag_UsedOnSplat, index);
				}
			}
			FOR_EACH_END;
		}
	}


	return true;
}

static bool dynOperatorVerify(DynOperator* pDynOp, const char* pcFileName, eDynObjectInfo edo)
{
	// verify dynop
	// we know a dynop operates on keyframes only, so lookup the parsetable within that table
	const ParseTable* pParseTable = ParseDynObject;
	int iParseColumn = 0;
	bool bFoundParseTable = false;
	if ( pDynOp->pcTokenString )
	{
		while (pParseTable[iParseColumn].type != TOK_END && !bFoundParseTable)
		{
			if ( pParseTable->type == TOK_IGNORE )
			{
				++pParseTable;
				continue;
			}
			if ( stricmp(pParseTable[iParseColumn].name,pDynOp->pcTokenString) == 0 )
			{
				pDynOp->iParseColumn = iParseColumn;
				bFoundParseTable = true;
			}
			++iParseColumn;
		}
	}

	if (!bFoundParseTable)
	{
		FxFileError(pcFileName, "Error on line %d: Unrecognized token %s", pDynOp->iLineNum, pDynOp->pcTokenString);
		return false;
	}

	if ( edo == edoRate)
	{
		if ( stricmp(ParseDynObject[pDynOp->iParseColumn].name, "Position") == 0 || stricmp(ParseDynObject[pDynOp->iParseColumn].name, "Orientation") == 0 )
		{
			FxFileError(pcFileName, "Error on line %d: No rate support for Position or Orientation, use Velocity or Spin", pDynOp->iLineNum);
		}
	}

	if (dynOpValueSet(pDynOp->vValue[2]))
		pDynOp->uiNumValuesSpecd = 3;
	else if (dynOpValueSet(pDynOp->vValue[1]))
	{
		pDynOp->uiNumValuesSpecd = 2;
		//pDynOp->vValue[2] = pDynOp->vValue[0];
	}
	else if (dynOpValueSet(pDynOp->vValue[0]))
	{
		pDynOp->uiNumValuesSpecd = 1;
		/*
		pDynOp->vValue[2] = pDynOp->vValue[0];
		pDynOp->vValue[1] = pDynOp->vValue[0];
		*/
	}
	else 
	{
		pDynOp->uiNumValuesSpecd = 0;
		/*
		pDynOp->vValue[2] = 0.0f;
		pDynOp->vValue[1] = 0.0f;
		pDynOp->vValue[0] = 0.0f;
		*/
	}

	// Now make sure the number of filled out values matches the token in pParseTable
	if ( pDynOp->eOpType == DynOpType_Inherit )
	{
		// inherit doesn't take values
		if ( pDynOp->uiNumValuesSpecd > 0 )
			FxFileError(pcFileName, "Error on line %d: Inherit operator doesn't use values", pDynOp->iLineNum);
	}
	else
	{
		if (ParseDynObject[pDynOp->iParseColumn].type & TOK_FIXED_ARRAY && ParseDynObject[pDynOp->iParseColumn].param == 3 || ParseDynObject[pDynOp->iParseColumn].param == 4) // RGB, VEC3, etc
		{
			if ( pDynOp->uiNumValuesSpecd == 0 || pDynOp->uiNumValuesSpecd == 2 )
			{
				FxFileError(pcFileName, "Error on line %d: Token type %s requires one or three values", pDynOp->iLineNum, pDynOp->pcTokenString);
				return false;
			}
		}
		else if (ParseDynObject[pDynOp->iParseColumn].type & TOK_FIXED_ARRAY && ParseDynObject[pDynOp->iParseColumn].param == 2) // RG, VEC2
		{
			if ( pDynOp->uiNumValuesSpecd == 0 || pDynOp->uiNumValuesSpecd == 3 )
			{
				FxFileError(pcFileName, "Error on line %d: Token type %s requires one or two values", pDynOp->iLineNum, pDynOp->pcTokenString);
				return false;
			}
		}
		else if (!(ParseDynObject[pDynOp->iParseColumn].type & TOK_FIXED_ARRAY))
		{
			if ( pDynOp->uiNumValuesSpecd != 1 )
			{
				FxFileError(pcFileName, "Error on line %d: Token type %s requires one value", pDynOp->iLineNum, pDynOp->pcTokenString);
				return false;
			}
		}
		else
		{
			FxFileError(pcFileName, "Error on line %d: No support for Apply operation on token %s", pDynOp->iLineNum, pDynOp->pcTokenString);
			return false;
		}
	}

	{
		switch(pDynOp->eOpType)
		{
			xcase DynOpType_SphereShellJitter:
			case DynOpType_SphereJitter:
			{
				if (pDynOp->uiNumValuesSpecd != 3)
				{
					FxFileError(pcFileName, "Error on line %d: Must specify 3 values for sphere jitter", pDynOp->iLineNum);
					return false;
				}
				if (fabsf(pDynOp->vValue[1]) > 180.0f)
				{
					FxFileError(pcFileName, "Error on line %d: Second value %f must be between -180 and 180", pDynOp->iLineNum, pDynOp->vValue[1]);
					return false;
				}
			}
			xdefault:
			{
			}
		}
	}

	return true;
}

static bool dynApplyParamVerify(DynApplyParam* pApplyParam, const char* pcFileName, DynFxInfo* pInfo, const ParseTable* pParseTable)
{
	U32 uiTokenIndex = dynTokenIndexFromStringGeneral(pApplyParam->pcTokenString, pParseTable);
	if (uiTokenIndex == 0xFFFFFFFF)
	{
		FxFileError(pcFileName, "Error Line %d: Invalid Token %s in Param line", pApplyParam->iLineNum, pApplyParam->pcTokenString);
		return false;
	}
	pApplyParam->uiTokenIndex = uiTokenIndex;
	pApplyParam->edpt = dynFxParamTypeFromTokenGeneral(uiTokenIndex, pParseTable);
	if ( pApplyParam->edpt == edptNone )
	{
		FxFileError(pcFileName, "Error Line %d: Unhandled token type %s in Param Line", pApplyParam->iLineNum, pApplyParam->pcTokenString);
		return false;
	}

	// Also, look up whether or not this parameter is declared in the dynFxInfo Param block and makes sense
	{
		U32 uiIndex;
		bool bFoundOne = false;
		for (uiIndex=0; uiIndex<eaUSize(&pInfo->paramBlock.eaDefineParams); ++uiIndex)
		{
			DynDefineParam* pDefineParam = pInfo->paramBlock.eaDefineParams[uiIndex];
			if ( pDefineParam->pcParamName == pApplyParam->pcParamName )
			{
				bFoundOne = true;
				switch (pApplyParam->edpt)
				{
					xcase edptVector:
					{
						if ( pDefineParam->mvVal.type != MULTI_VEC3 )
						{
							if ( pDefineParam->mvVal.type == MULTI_STRING )
							{
								FxFileError(pcFileName, "Error Line %d: Can't use String Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
							}

							if ( pDefineParam->mvVal.type == MULTI_FLOAT && pApplyParam->eParamOp != edpoMultiply )
							{
								FxFileError(pcFileName, "Error Line %d: Can't use Float Param %s on Token %s Unless it's a multiply.", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
							}

							if ( pDefineParam->mvVal.type == MULTI_VEC4 )
							{
								FxFileError(pcFileName, "Error Line %d: Can't use VC4 Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
							}
						}
					}
					xcase edptVector4:
					{
						if ( pDefineParam->mvVal.type != MULTI_VEC4 )
						{
							if ( pDefineParam->mvVal.type == MULTI_STRING )
							{
								FxFileError(pcFileName, "Error Line %d: Can't use String Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
							}

							if ( pDefineParam->mvVal.type == MULTI_FLOAT && pApplyParam->eParamOp != edpoMultiply )
							{
								FxFileError(pcFileName, "Error Line %d: Can't use Float Param %s on Token %s Unless it's a multiply.", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
							}

							if ( pDefineParam->mvVal.type == MULTI_VEC3 )
							{
								FxFileError(pcFileName, "Error Line %d: Can't use VEC Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
							}
						}
					}
					xcase edptNumber:
					{
						if ( pDefineParam->mvVal.type != MULTI_FLOAT )
						{
								FxFileError(pcFileName, "Error Line %d: Can't use non-Float Param %s on Token %s.", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
								return false;
						}
					}
					xcase edptBool:
					{
						if ( pDefineParam->mvVal.type != MULTI_FLOAT )
						{
							FxFileError(pcFileName, "Error Line %d: Can't use non-FLT Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
							return false;
						}
						if ( pApplyParam->eParamOp != edpoCopy )
						{
							FxFileError(pcFileName, "Error Line %d: Only Param operation Copy is supported with bool Params", pApplyParam->iLineNum);
							return false;
						}
					}
					xcase edptString:
					{
						if ( pDefineParam->mvVal.type != MULTI_STRING )
						{
							FxFileError(pcFileName, "Error Line %d: Can't use non-string Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
							return false;
						}
						if ( pApplyParam->eParamOp != edpoCopy && pApplyParam->eParamOp != edpoAdd )
						{
							FxFileError(pcFileName, "Error Line %d: Only Param operations Copy and Add are supported with string Params", pApplyParam->iLineNum);
							return false;
						}
					}
					xcase edptQuat:
					{
						if ( pDefineParam->mvVal.type != MULTI_VEC3 )
						{
							FxFileError(pcFileName, "Error Line %d: Can't use non-VEC Param %s on Token %s", pApplyParam->iLineNum, pApplyParam->pcParamName, pApplyParam->pcTokenString);
							return false;
						}
						if ( pApplyParam->eParamOp != edpoCopy )
						{
							FxFileError(pcFileName, "Error Line %d: Only Param operation Copy is supported with token %s", pApplyParam->iLineNum, pApplyParam->pcTokenString);
							return false;
						}
					}
}
			}
		}

		if ( !bFoundOne )
		{
			FxFileError(pcFileName, "Error Line %d: No DefaultParam found for Param %s", pApplyParam->iLineNum, pApplyParam->pcParamName);
			return false;
		}
	}
	return true;
}

static const DynLoop* dynLoopByNameFind(const DynLoop*** peaLoops, char* pcLoopName)
{
	const U32 uiNumLoops = eaSize(peaLoops);
	U32 uiLoopIndex;
	for (uiLoopIndex=0; uiLoopIndex< uiNumLoops; ++uiLoopIndex)
	{
		if (stricmp((*peaLoops)[uiLoopIndex]->pcLoopTag, pcLoopName) == 0)
			return (*peaLoops)[uiLoopIndex];
	}

	return NULL;
}

static bool dynLookupLoops(char*** peaLoopNames, const DynLoop*** peaLoops, const DynLoop*** cpeaEventLoops, const char* pcFileName)
{
	const U32 uiNumLoops = eaSize(peaLoopNames);
	U32 uiLoopIndex;
	for (uiLoopIndex=0; uiLoopIndex<uiNumLoops; ++uiLoopIndex)
	{
		const DynLoop* pLoop = dynLoopByNameFind(cpeaEventLoops, (*peaLoopNames)[uiLoopIndex]);
		if (!pLoop)
		{
			FxFileError(pcFileName, "Could not find Loop %s\n", (*peaLoopNames)[uiLoopIndex]);
			return false;
		}
		eaPush(peaLoops, pLoop);
	}
	return true;
}

bool dynObjectInfoSpecifiesToken(const DynObjectInfo* pObjectInfo, int iTokenIndex)
{
	return TokenIsSpecified(ParseDynObject, iTokenIndex, (void*)pObjectInfo, 0);
}

static bool dynObjectInfoReferencesToken(const DynObjectInfo* pObjectInfo, int iTokenIndex)
{
	return TokenIsSpecified(ParseDynObject, iTokenIndex, (void*)pObjectInfo, 0)
		|| dynObjectInfoHasDynOpForToken(pObjectInfo, iTokenIndex)
		|| dynObjectInfoHasJListForToken(pObjectInfo, iTokenIndex);
}

static U32 dynTokenIndexFromString(const char* pcString)
{
	U32 uiTokenIndex = uiFirstDynObjectStaticToken;
	VALIDATE_PARSEDYNOBJECT_TOKEN_INDEX(uiFirstDynObjectStaticToken);
	while (uiTokenIndex != uiDynObjectTokenTerminator)
	{
		if ( stricmp(pcString, ParseDynObject[uiTokenIndex].name) == 0 )
			return uiTokenIndex;
		++uiTokenIndex;
	}
	return 0;
}

static U32 dynTokenIndexFromStringGeneral(const char* pcString, const ParseTable* pParseTable)
{
	U32 uiTokenIndex = 0;
	while (pParseTable[uiTokenIndex].type != TOK_END)
	{
		if ( stricmp(pcString, pParseTable[uiTokenIndex].name) == 0 )
			return uiTokenIndex;
		++uiTokenIndex;
	}
	return 0xFFFFFFFF;
}

//#define TRACK_BONE_USAGE 1

#if TRACK_BONE_USAGE 
static StashTable stBoneUsage = NULL;
static void trackBoneUsage(const char* pcBoneName)
{
	if (!pcBoneName)
		return;
	if (!stBoneUsage)
		stBoneUsage = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	if (!stashAddInt(stBoneUsage, pcBoneName, 1, false))
	{
		StashElement elem;
		if (stashFindElement(stBoneUsage, pcBoneName, &elem))
		{
			stashElementSetInt(elem, stashElementGetInt(elem) + 1);
		}
	}
}

//AUTO_COMMAND;
void printBoneUsage(void)
{
	StashTableIterator iter;
	StashElement elem;
	const DynBaseSkeleton* pBaseSkeleton = dynBaseSkeletonFind("CoreDefault/Skel_Core_Default");
	stashGetIterator(stBoneUsage, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		const char* pcBoneName = (const char*)stashElementGetKey(elem);
		if (pBaseSkeleton && dynBaseSkeletonFindNode(pBaseSkeleton, pcBoneName))
			printf("%s - %d\n", pcBoneName, stashElementGetInt(elem));
	}
}
#endif

static bool dynKeyFrameVerify(DynKeyFrame** pKeyFrames, U32 uiKeyFrameIndex, DynEvent* pEvent, const char* pcFileName, DynFxInfo* pInfo, DynFxDeps *pDeps)
{
	DynKeyFrame* pKeyFrame = pKeyFrames[uiKeyFrameIndex];
	// verify keyframe parts
	if ( pKeyFrame->fParseTimeStamp < 0.0f )
	{
		FxFileError(pcFileName, "Negative time stamps are not allowed!");
		return false;
	}

	// Check for keyframes in the wrong order. (If we don't like this error, then we should sort keyframes by time
	// instead!)
	if(uiKeyFrameIndex > 0) {
		if(pKeyFrame->fParseTimeStamp < pKeyFrames[uiKeyFrameIndex - 1]->fParseTimeStamp) {
			FxFileError(pcFileName, "Keyframes have timestamps out of order.");
			return false;
		}
	}

	pKeyFrame->uiTimeStamp = DYNFXTIME(pKeyFrame->fParseTimeStamp);

	if (pKeyFrame->fParseTimeStamp > 300)
	{
		FxFileError(pcFileName, "Please don't make fx last longer than 300 seconds. Use ContinuingFX if you want it to last a long time.");
		return false;
	}

	// lookup loop starts and loop ends
	FOR_EACH_IN_EARRAY(pKeyFrame->eaLoopStart, DynLoopRef, pLoopStart)
		FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
			if (stricmp(pLoopStart->pcTag, pLoop->pcLoopTag)==0)
			{
				pLoopStart->pLoop = pLoop;
				break;
			}
		FOR_EACH_END
		if (!pLoopStart->pLoop)
		{
			FxFileError(pcFileName, "Can't find Loop %s", pLoopStart->pcTag);
			return false;
		}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pKeyFrame->eaLoopStop, DynLoopRef, pLoopStop)
		FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
			if (stricmp(pLoopStop->pcTag, pLoop->pcLoopTag)==0)
			{
				pLoopStop->pLoop = pLoop;
				break;
			}
		FOR_EACH_END
		if (!pLoopStop->pLoop)
		{
			FxFileError(pcFileName, "Can't find Loop %s", pLoopStop->pcTag);
			return false;
		}
	FOR_EACH_END

	// Verify emitter stops and starts
	FOR_EACH_IN_EARRAY(pKeyFrame->eaEmitterStart, DynParticleEmitterRef, pEmitterStart)
		FOR_EACH_IN_EARRAY(pInfo->eaEmitters, DynParticleEmitter, pEmitter)
			//pInfo->bDontDraw = false;
			if (stricmp(pEmitterStart->pcTag, pEmitter->pcTag)==0)
			{
				pEmitterStart->pEmitter = pEmitter;
				if (pEmitter->position == DynParticleEmitFlag_Inherit && pEmitter->rotation == DynParticleEmitFlag_Update)
				{
					FxFileError(pcFileName, "Can't set Rotation to Update without also setting Position to Update in Emitter Block %s! This would cause it to rotate about the world origin, which is likely not what you wanted.", pEmitter->pcTag);
					return false;
				}
				break;
			}
		FOR_EACH_END
		if (!pEmitterStart->pEmitter)
		{
			FxFileError(pcFileName, "Can't find Emitter %s", pEmitterStart->pcTag);
			return false;
		}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pKeyFrame->eaEmitterStop, DynParticleEmitterRef, pEmitterStop)
		FOR_EACH_IN_EARRAY(pInfo->eaEmitters, DynParticleEmitter, pEmitter)
			if (stricmp(pEmitterStop->pcTag, pEmitter->pcTag)==0)
			{
				pEmitterStop->pEmitter = pEmitter;
				break;
			}
		FOR_EACH_END
		if (!pEmitterStop->pEmitter)
		{
			FxFileError(pcFileName, "Can't find Emitter %s", pEmitterStop->pcTag);
			return false;
		}
	FOR_EACH_END

	// Verify raycast stops and starts
	FOR_EACH_IN_EARRAY(pKeyFrame->eaRaycastStart, DynRaycastRef, pRaycastStart)
		FOR_EACH_IN_EARRAY(pInfo->eaRaycasts, DynRaycast, pRaycast)
			if (stricmp(pRaycastStart->pcTag, pRaycast->pcTag)==0)
			{
				pRaycastStart->pRaycast = pRaycast;
				break;
			}
		FOR_EACH_END
		if (!pRaycastStart->pRaycast)
		{
			FxFileError(pcFileName, "Can't find Raycast %s", pRaycastStart->pcTag);
			return false;
		}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pKeyFrame->eaRaycastStop, DynRaycastRef, pRaycastStop)
		FOR_EACH_IN_EARRAY(pInfo->eaRaycasts, DynRaycast, pRaycast)
			if (stricmp(pRaycastStop->pcTag, pRaycast->pcTag)==0)
			{
				pRaycastStop->pRaycast = pRaycast;
				break;
			}
		FOR_EACH_END
		if (!pRaycastStop->pRaycast)
		{
			FxFileError(pcFileName, "Can't find Raycast %s", pRaycastStop->pcTag);
			return false;
		}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pKeyFrame->eaForceStart, DynForceRef, pForceStart)
		FOR_EACH_IN_EARRAY(pInfo->eaForces, DynForce, pForce)
			if (stricmp(pForceStart->pcTag, pForce->pcTag)==0)
			{
				pForceStart->pForce = pForce;
				break;
			}
		FOR_EACH_END
		if (!pForceStart->pForce)
		{
			FxFileError(pcFileName, "Can't find Force %s", pForceStart->pcTag);
			return false;
		}
	FOR_EACH_END


	FOR_EACH_IN_EARRAY(pKeyFrame->eaForceStop, DynForceRef, pForceStop)
		FOR_EACH_IN_EARRAY(pInfo->eaForces, DynForce, pForce)
			if (stricmp(pForceStop->pcTag, pForce->pcTag)==0)
			{
				pForceStop->pForce = pForce;
				break;
			}
		FOR_EACH_END
		if (!pForceStop->pForce)
		{
			FxFileError(pcFileName, "Can't find Force %s", pForceStop->pcTag);
			return false;
		}
	FOR_EACH_END

	// Verify DynParentBhvr
	if ( pKeyFrame->pParentBhvr )
	{
		bool bGoToSpec = false;
		bool bAtSpec = false;
		bool bOrientToSpec = false;
		const U32 uiNumParams = eaSize(&pKeyFrame->pParentBhvr->eaParams);
		U32 uiDynParamIndex;
		for (uiDynParamIndex=0; uiDynParamIndex<uiNumParams; ++uiDynParamIndex)
		{
			DynApplyParam* pParam = pKeyFrame->pParentBhvr->eaParams[uiDynParamIndex];
			if (!dynApplyParamVerify(pParam, pcFileName, pInfo, parse_DynParentBhvr))
				return false;
			if (pParam->uiTokenIndex == PARSE_DYNPARENTBHVR_AT_INDEX)
				bAtSpec = true;
			if (pParam->uiTokenIndex == PARSE_DYNPARENTBHVR_GOTO_INDEX)
				bGoToSpec = true;
			if (pParam->uiTokenIndex == PARSE_DYNPARENTBHVR_ORIENTTO_INDEX)
				bOrientToSpec = true;
		}

		{
			const U32 uiNumJLists = eaSize(&pKeyFrame->pParentBhvr->eaJLists);
			U32 uiJListIndex;
			for (uiJListIndex=0; uiJListIndex<uiNumJLists; ++uiJListIndex)
			{
				DynJitterList* pJList = pKeyFrame->pParentBhvr->eaJLists[uiJListIndex];
				if (!dynJitterListVerify(pJList, pcFileName, pInfo, parse_DynParentBhvr, pKeyFrame, NULL))
					return false;
			}
		}

		for (uiDynParamIndex=0; uiDynParamIndex<uiNumParams; ++uiDynParamIndex)
		{
			DynApplyParam* pParam = pKeyFrame->pParentBhvr->eaParams[uiDynParamIndex];
			char* pcResult;
			// Apply the dynparam to the parentbhvr
			dynFxApplyCopyParamsGeneral(uiNumParams, pKeyFrame->pParentBhvr->eaParams, pParam->uiTokenIndex, &pInfo->paramBlock, &pcResult, parse_DynParentBhvr);
			TokenStoreSetPointer( parse_DynParentBhvr, pParam->uiTokenIndex, pKeyFrame->pParentBhvr, 0, pcResult, NULL);
		}


		// Deal with transform flags
		if (pKeyFrame->pParentBhvr->pcAtNode)
			bAtSpec = true;
		if (pKeyFrame->pParentBhvr->pcGoToNode)
			bGoToSpec = true;
		if (pKeyFrame->pParentBhvr->pcOrientToNode)
			bOrientToSpec = true;

#if TRACK_BONE_USAGE 
		trackBoneUsage(pKeyFrame->pParentBhvr->pcAtNode);
		trackBoneUsage(pKeyFrame->pParentBhvr->pcGoToNode);
		trackBoneUsage(pKeyFrame->pParentBhvr->pcOrientToNode);
#endif

		if (pKeyFrame->pParentBhvr->uiDynFxParentFlags & edpfAttachAfterOrient)
			pKeyFrame->pParentBhvr->uiDynFxParentFlags |= edpfOrientToOnce;

		if (pKeyFrame->pParentBhvr->uiDynFxInheritFlags == ednNone && pKeyFrame->pParentBhvr->uiDynFxUpdateFlags == ednNone)
		{
			// Can't have both none, so default is update all!
			pKeyFrame->pParentBhvr->uiDynFxUpdateFlags = ednAll;
			pKeyFrame->pParentBhvr->uiDynFxInheritFlags = ednAll;
		}
		if ( bOrientToSpec )
		{
			pKeyFrame->pParentBhvr->uiDynFxInheritFlags &= ~ednRot;
			pKeyFrame->pParentBhvr->uiDynFxUpdateFlags &= ~ednRot;
		}
		if ( bGoToSpec )
		{
			pKeyFrame->pParentBhvr->uiDynFxUpdateFlags &= ~ednTrans;
			pKeyFrame->pParentBhvr->uiDynFxUpdateFlags &= ~ednRot;
		}
	}

	// Make sure we haven't repeated any time stamps thus far
	{
		U32 uiIndex;
		for (uiIndex=0; uiIndex<uiKeyFrameIndex; ++uiIndex )
		{
			if ( pKeyFrame->uiTimeStamp == pKeyFrames[uiIndex]->uiTimeStamp )
			{
				FxFileError(pcFileName, "Two keyframes with the same timestamp: %.2f. Maybe too close together...", pKeyFrame->fParseTimeStamp);
				return false;
			}
		}
	}

	// verify all of the params
	{
		eDynObjectInfo edo = 0;
		for (edo = 0; edo < edoTotal; ++edo)
		{
			DynObjectInfo* pObjInfo = &pKeyFrame->objInfo[edo];
			const U32 uiNumParams = eaSize(&pKeyFrame->objInfo[edo].eaParams);
			U32 uiDynParamIndex;
			for (uiDynParamIndex=0; uiDynParamIndex<uiNumParams; ++uiDynParamIndex)
			{
				DynApplyParam* pParam = pObjInfo->eaParams[uiDynParamIndex];
				if (!dynApplyParamVerify(pParam, pcFileName, pInfo, ParseDynObject))
					return false;
				// Apply the dynparam to the keyframe
				dynFxApplyParamToObjInfo(pParam, pObjInfo, pcFileName, pInfo);
			}
		}
	}
	// verify all of the JLists
	{
		eDynObjectInfo edo = 0;
		for (edo = 0; edo < edoTotal; ++edo)
		{
			DynObjectInfo* pObjInfo = &pKeyFrame->objInfo[edo];
			const U32 uiNumJLists = eaSize(&pObjInfo->eaJLists);
			U32 uiJListIndex;
			for (uiJListIndex=0; uiJListIndex<uiNumJLists; ++uiJListIndex)
			{
				DynJitterList* pJList = pObjInfo->eaJLists[uiJListIndex];
				if (!dynJitterListVerify(pJList, pcFileName, pInfo, ParseDynObject, pKeyFrame, pDeps))
					return false;
			}
		}
	}
	// now verify every tokop
	{
		eDynObjectInfo edo = 0;
		for (edo = 0; edo < edoTotal; ++edo)
		{
			const U32 uiNumDynOps = eaSize(&pKeyFrame->objInfo[edo].eaDynOps);
			const U32 uiNumInterps = eaSize(&pKeyFrame->objInfo[edo].eaInterps);
			U32 uiDynOpIndex, uiInterpIndex;
			if ( uiNumInterps )
				pKeyFrame->objInfo[edo].puiInterpTypes = calloc(sizeof(U8) , uiDynObjectTokenTerminator);
			for (uiDynOpIndex=0; uiDynOpIndex<uiNumDynOps; ++uiDynOpIndex )
			{
				if (!dynOperatorVerify(pKeyFrame->objInfo[edo].eaDynOps[uiDynOpIndex], pcFileName, edo))
					return false;
			}
			for (uiInterpIndex=0; uiInterpIndex<uiNumInterps; ++uiInterpIndex)
			{
				// Find the token index from the string
				DynInterp* pInterp = pKeyFrame->objInfo[edo].eaInterps[uiInterpIndex];
				U32 uiTokenIndex = dynTokenIndexFromString(pInterp->pcTokenString);
				if (!uiTokenIndex)
				{
					FxFileError(pcFileName, "Error Line %d: Invalid Token %s in Interp line", pInterp->iLineNum, pInterp->pcTokenString);
					return false;
				}
				pKeyFrame->objInfo[edo].puiInterpTypes[uiTokenIndex] = pInterp->eInterpType;
			}
		}
	}


	// Verify costume stuff
	FOR_EACH_IN_EARRAY(pKeyFrame->eaCostume, DynFxCostume, pCostume)
	{
		if (pCostume->bSnapshotOfCallersPose &&
			pCostume->bReleaseSnapshot)
		{
			FxFileError(pcFileName, "Unable to take and release a DynCost snapshot on the same frame");
		}

		FOR_EACH_IN_EARRAY(pCostume->eaParams, DynApplyParam, pParam)
		{
			if (!dynApplyParamVerify(pParam, pcFileName, pInfo, parse_DynFxCostume))
				return false;
			FOR_EACH_IN_EARRAY(pCostume->eapcSetBits, const char, pcBit)
				if (!dynBitIsValidName(pcBit))
				{
					FxFileError(pcFileName, "Unknown Costume Bit %s", pcBit);
					return false;
				}
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY(pCostume->eapcClearBits, const char, pcBit)
				if (!dynBitIsValidName(pcBit))
				{
					FxFileError(pcFileName, "Unknown Costume Bit %s", pcBit);
					return false;
				}
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY(pCostume->eapcToggleBits, const char, pcBit)
				if (!dynBitIsValidName(pcBit))
				{
					FxFileError(pcFileName, "Unknown Costume Bit %s", pcBit);
					return false;
				}
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY(pCostume->eapcFlashBits, const char, pcBit)
				if (!dynBitIsValidName(pcBit))
				{
					FxFileError(pcFileName, "Unknown Costume Bit %s", pcBit);
					return false;
				}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;


	// Verify physics info
	if (pKeyFrame->pPhysicsInfo)
	{
		if (pKeyFrame->pPhysicsInfo->pcPhysicalProperty)
		{
			REF_TO(PhysicalProperties) physRef = {0};
			if (!physicalPropertiesFindByName(pKeyFrame->pPhysicsInfo->pcPhysicalProperty, &physRef.__handle_INTERNAL) || GET_REF(physRef) == NULL)
			{
				FxFileError(pcFileName, "Can't find physical property %s", pKeyFrame->pPhysicsInfo->pcPhysicalProperty);
				REMOVE_HANDLE(physRef);
				return false;
			}
			REMOVE_HANDLE(physRef);
		}
	}

	// Verify sound info
	if(pKeyFrame->ppcSoundStarts || pKeyFrame->ppcSoundEnds)
	{
		int i, max;
		U32 passed = 1;

		max = eaSize(&pKeyFrame->ppcSoundStarts);

		for(i=0; i<max; i++)
		{
			char *event_name = pKeyFrame->ppcSoundStarts[i];
			if(sndVerifyFunc)
			{				
				if(!sndVerifyFunc(event_name))
				{
					SndFxFileError(pcFileName, "Invalid Sound Event Start: %s", event_name);
					passed = 0;
				}
			}
		}
		
		if(max>0)
		{
			// Make sure a create event existed first
			U32 uiIndex;
			U32 createFound = 0;
			for (uiIndex=0; uiIndex<=uiKeyFrameIndex; ++uiIndex )
			{
				if ( pKeyFrames[uiIndex]->eType == eDynKeyFrameType_Create )
				{
					createFound = 1;
				}
			}

			if(!createFound) // Next, try the 'start' event, if one exists
			{
				FOR_EACH_IN_EARRAY(pInfo->events, DynEvent, pOtherEvent)
					if (pOtherEvent != pEvent)
					{
						FOR_EACH_IN_EARRAY(pOtherEvent->keyFrames, DynKeyFrame, pOtherKeyFrame)
							if ( pOtherKeyFrame->eType == eDynKeyFrameType_Create )
							{
								createFound = 1;
								break;
							}
						FOR_EACH_END;
					}
					if (createFound)
						break;
				FOR_EACH_END;
			}
			if(!createFound)
			{
				if (sndInvalidateFunc) sndInvalidateFunc(pcFileName);
				SndFxFileError(pcFileName, "Create key frame not found before SoundStart: %s", pKeyFrame->ppcSoundStarts[0]);
				return false;
			}
		}

		max = eaSize(&pKeyFrame->ppcSoundEnds);

		for(i=0; i<max; i++)
		{
			char *event_name = pKeyFrame->ppcSoundEnds[i];
			int j, jmax, found = 0;
			if(sndVerifyFunc)
			{	
				if(!sndVerifyFunc(event_name))
				{
					SndFxFileError(pcFileName, "Invalid Sound Event End: %s", event_name);
					passed = 0;
				}
			}

			jmax = eaSize(&pKeyFrame->ppcSoundStarts);
			for(j=0; j<jmax; j++)
			{
				char *start = pKeyFrame->ppcSoundEnds[j];

				if(!stricmp(event_name, start))
				{
					found = 1;
					break;
				}
			}

			if(!found)
			{
				/*
				TODO(AM): Does this matter?  Can't conclusively say it wasn't started?
				SndFxFileError(pcFileName, "Trying to End Event without a Start: %s", event_name);
				passed = 0;
				*/
			}
		}

		if(!passed)
		{
			return false;
		}
	}

	if(pKeyFrame->ppcSoundDSPStarts || pKeyFrame->ppcSoundDSPEnds)
	{
		int passed = true;
		static char** endDup = NULL;
		static char** startDup = NULL;

		eaClear(&endDup);
		eaPushEArray(&endDup, &pKeyFrame->ppcSoundDSPEnds);

		FOR_EACH_IN_EARRAY(pKeyFrame->ppcSoundDSPStarts, char, dsp)
		{
			int found = false;
			if(sndVerifyDSPFunc && !sndVerifyDSPFunc(dsp))
			{
				SndFxFileError(pcFileName, "Invalid Sound DSP Start - DSP Not Found: %s", dsp);
				passed = false;
			}

			/*
			if(!pEvent->bLoop && !pEvent->bKeepAlive)
			{
				FOR_EACH_IN_EARRAY(endDup, char, end)
				{
					if(!stricmp(dsp, end))
					{
						eaRemoveFast(&endDup, FOR_EACH_IDX(-, end));
						found = true;
						break;
					}
				}
				FOR_EACH_END;

				if(!found)
				{
					SndFxFileError(pcFileName, "Invalid Sound DSP Start - End Not Found: %s", dsp);
					passed = false;
				}
			}
			*/
		}
		FOR_EACH_END;

		/*
		FOR_EACH_IN_EARRAY(endDup, char, endtest)
		{
			SndFxFileError(pcFileName, "Invalid Sound DSP End - No Start: %s", endtest);
			passed = false;
		}
		FOR_EACH_END;
		*/

		if(!passed)
			return false;
	}


	// Do all the verification of DynObjectInfo stuff
	// Only edoValue needs defaults for now
	{
		U32 uiParseTableIndex = uiFirstDynObjectStaticToken;
		const ParseTable* pParseTable = ParseDynObject;

		FORALL_PARSETABLE(pParseTable, uiParseTableIndex)
		{
			if (!dynObjectInfoSpecifiesToken(&pKeyFrame->objInfo[edoValue], uiParseTableIndex))
			{
				if ( stricmp(pParseTable[uiParseTableIndex].name, "Color") == 0)
				{
#ifdef DYN_FX_NORMALIZED_COLORS
					pKeyFrame->objInfo[edoValue].obj.draw.color[0] = 1.0f;
					pKeyFrame->objInfo[edoValue].obj.draw.color[1] = 1.0f;
					pKeyFrame->objInfo[edoValue].obj.draw.color[2] = 1.0f;
#else
					pKeyFrame->objInfo[edoValue].obj.draw.color[0] = 255.0f;
					pKeyFrame->objInfo[edoValue].obj.draw.color[1] = 255.0f;
					pKeyFrame->objInfo[edoValue].obj.draw.color[2] = 255.0f;
#endif
				}
				else if ( stricmp(pParseTable[uiParseTableIndex].name, "Alpha") == 0)
				{
#ifdef DYN_FX_NORMALIZED_COLORS
					pKeyFrame->objInfo[edoValue].obj.draw.color[3] = 1.0f;
#else
					pKeyFrame->objInfo[edoValue].obj.draw.color[3] = 255.0f;
#endif
				}
				else if ( stricmp(pParseTable[uiParseTableIndex].name, "Scale") == 0)
				{
					copyVec3(onevec3, pKeyFrame->objInfo[edoValue].obj.draw.vScale);
				}
			}

			if ( ( stricmp(pParseTable[uiParseTableIndex].name, "Position") == 0 || stricmp(pParseTable[uiParseTableIndex].name, "Orientation") == 0 ) && dynObjectInfoSpecifiesToken(&pKeyFrame->objInfo[edoRate], uiParseTableIndex))
			{
				FxFileError(pcFileName, "Can't specify Rate for Position or Orientation, use Velocity or Spin respectively");
			}
		}

		// Check any texture refs
		{
			// If our EntMaterial mode is TextureSwap, make sure we have two textures set
			bool bRequiresBothTextures = (pKeyFrame->objInfo[edoValue].obj.draw.eEntityMaterialMode == edemmTextureSwap);
			const char *pcTextureName = pKeyFrame->objInfo[edoValue].obj.draw.pcTextureName;
			if ( pcTextureName )
			{
				int index = eaPush(&pDeps->texture_deps, pcTextureName);
				if (pKeyFrame->objInfo[edoValue].obj.splatInfo.eType != eDynSplatType_None)
					eaiSet(&pDeps->texture_dep_flags, eaiGet(&pDeps->texture_dep_flags, index)|eDynFxDepFlag_UsedOnSplat, index);
			}
			else if (bRequiresBothTextures)
			{
				FxFileError(pcFileName, "Both Texture and Texture2 must be set for EntMaterial TextureSwap");
				return false;
			}
			pcTextureName = pKeyFrame->objInfo[edoValue].obj.draw.pcTextureName2;
			if ( pcTextureName )
			{
				eaPush(&pDeps->texture_deps, pcTextureName);
				// Don't care about Texture2 on Splats
			}
			else if (bRequiresBothTextures)
			{
				FxFileError(pcFileName, "Both Texture and Texture2 must be set for EntMaterial TextureSwap");
				return false;
			}
		}

		// Check material refs
		{
			int material, count = eaSize(&pKeyFrame->objInfo[edoValue].obj.flare.ppcMaterials);
			const char* pcMaterialName = pKeyFrame->objInfo[edoValue].obj.draw.pcMaterialName;
			
			const char* pcDissolveMaterialName = pKeyFrame->objInfo[edoValue].obj.draw.pcGeoDissolveMaterialName;
			int iGeoMaterialAddCount = eaSize(&pKeyFrame->objInfo[edoValue].obj.draw.ppcGeoAddMaterialNames);

			if ( pcMaterialName )
			{
				int index = eaPush(&pDeps->material_deps, pcMaterialName);
				if (pKeyFrame->objInfo[edoValue].obj.splatInfo.eType != eDynSplatType_None)
					eaiSet(&pDeps->material_dep_flags, eaiGet(&pDeps->material_dep_flags, index)|eDynFxDepFlag_UsedOnSplat, index);
			}

			if(pcDissolveMaterialName) {
				eaPush(&pDeps->material_deps, pcDissolveMaterialName);
			}

			while(iGeoMaterialAddCount) {
				iGeoMaterialAddCount--;
				eaPush(&pDeps->material_deps, pKeyFrame->objInfo[edoValue].obj.draw.ppcGeoAddMaterialNames[iGeoMaterialAddCount]);
			}

			for (material = 0; material < count; ++material)
			{
				const char* pcFlareMaterialName = pKeyFrame->objInfo[edoValue].obj.flare.ppcMaterials[material];
				int index = eaPush(&pDeps->material_deps, pcFlareMaterialName);
				if (pKeyFrame->objInfo[edoValue].obj.flare.eType != eDynFlareType_None)
					eaiSet(&pDeps->material_dep_flags, eaiGet(&pDeps->material_dep_flags, index)|eDynFxDepFlag_UsedOnFlare, index);
			}

			switch (pKeyFrame->objInfo[edoValue].obj.draw.eEntityMaterialMode)
			{
				case edemmSwap:
				case edemmAdd:
				case edemmSwapWithConstants:
				case edemmAddWithConstants:
				case edemmDissolve:
					if (!pcMaterialName)
					{
						FxFileError(pcFileName, "Must set a material if using EntMaterial");
						return false;
					}
				xdefault:
				case edemmTextureSwap:
				case edemmNone:
					// Do Nothing
					{

					}

			}
		}

		// Check any geometry refs
		{
			const char* pcModelName = pKeyFrame->objInfo[edoValue].obj.draw.pcModelName;
			if ( pcModelName )
			{
				int index = eaPush(&pDeps->geometry_deps, pcModelName);
				if (pKeyFrame->objInfo[edoValue].obj.draw.pcMaterialName)
					eaiSet(&pDeps->geometry_dep_flags, eaiGet(&pDeps->geometry_dep_flags, index)|eDynFxDepFlag_MaterialNotUsed, index);
			}
		}

		// Make sure that the quat is not empty
		if ( quatIsZero(pKeyFrame->objInfo[edoValue].obj.draw.qRot))
			pKeyFrame->objInfo[edoValue].obj.draw.qRot[3] = -1.0f;

	}







	{
		U32 uiTokenIndex = uiFirstDynObjectStaticToken;
		eDynObjectInfo edo;
		for (edo=0; edo<edoTotal; ++edo)
		{
			ZeroStruct(&pKeyFrame->bfChanges[edo]);
		}

		ZeroStruct(&pKeyFrame->bfAnyChanges);

		while ( uiTokenIndex != uiDynObjectTokenTerminator )
		{
			U32 uiBitFieldIndex = uiTokenIndex - uiFirstDynObjectStaticToken;
			for (edo=0; edo<edoTotal; ++edo)
			{
				if ( dynObjectInfoReferencesToken(&pKeyFrame->objInfo[edo], uiTokenIndex) )
				{
					SETB(pKeyFrame->bfChanges[edo].bf, uiBitFieldIndex);
					SETB(pKeyFrame->bfAnyChanges.bf, uiBitFieldIndex);
				}
			}
			++uiTokenIndex;
		}
	}


	return true;
}



/*
static bool dynFxHeaderPushEntry(DynFxHeader* pFxHeader, U32 uiMaxDataSize, U32 uiTokenIndex, eDynObjectInfo edoType, void* pData, U8 uiDataSize)
{
	DynFxHeaderEntry* pEntry = &pFxHeader->pEntries[pFxHeader->uiNumEntries];
	DynFxHeaderEntry* pPrevEntry = (pFxHeader->uiNumEntries == 0)
		?NULL
		:&pFxHeader->pEntries[pFxHeader->uiNumEntries - 1];


	if ( pPrevEntry )
	{
		pEntry->uiOffset = pPrevEntry->uiOffset + pPrevEntry->uiDataSize;
	}
	else
		pEntry->uiOffset = 0;

	pEntry->uiTokenIndex = (U8)uiTokenIndex;
	pEntry->uiType = (U8)edoType;
	pEntry->uiDataSize = uiDataSize;

	if ( (U32)pEntry->uiOffset + (U32)pEntry->uiDataSize > uiMaxDataSize )
	{
		FatalErrorf("Exceeded max temp data size for fx header");
		return false;
	}
	memcpy((char*)pFxHeader->pData + pEntry->uiOffset, pData, pEntry->uiDataSize);

	++pFxHeader->uiNumEntries;
	return true;
}

U8 dynFxTokenSize(U32 uiTokenIndex, void* pData)
{
	size_t sDataSize = 0;
	switch (ParseDynObject[uiTokenIndex].type & TOK_TYPE_MASK)
	{
		xcase TOK_F32_X:
			sDataSize = sizeof(F32) * ParseDynObject[uiTokenIndex].param;
		xcase TOK_INT_X:
			sDataSize = sizeof(int) * ParseDynObject[uiTokenIndex].param;
		xcase TOK_BOOL_X:
			sDataSize = sizeof(bool) * ParseDynObject[uiTokenIndex].param;
		xcase TOK_QUATPYR_X:
			sDataSize = sizeof(F32) * ParseDynObject[uiTokenIndex].param;
		xcase TOK_STRING_X:
			sDataSize = strlen((const char*)pData) + 1;
		xdefault:
			FatalErrorf("Unhandled token in dynFxTokenSize()");
	}

	if ( sDataSize >= 256 )
	{
		FatalErrorf("Unreasonable data size in dynFxTokenSize: %d", sDataSize);
		return 0;
	}
	
	return (U8)sDataSize;
}

void dynFxHeaderEntryPrint(DynFxHeaderEntry* pEntry, void* pDataStart)
{
	int i;
	void* pData = (char*)pDataStart + pEntry->uiOffset;
	printf("%-20s / ", ParseDynObject[pEntry->uiTokenIndex].name );
	switch (pEntry->uiType)
	{
		xcase edoValue:
			printf("Value:    ");
		xcase edoRate:
			printf("Rate:     ");
		xdefault:		
			printf("Unknown:  ");
	}

	switch (ParseDynObject[pEntry->uiTokenIndex].type & TOK_TYPE_MASK)
	{
		xcase TOK_QUATPYR_X:
		xcase TOK_F32_X:
			for (i=0; i<ParseDynObject[pEntry->uiTokenIndex].param; ++i)
			{
				printf("%.4f  ", ((F32*)(pData))[i]);
			}
		xcase TOK_INT_X:
			for (i=0; i<ParseDynObject[pEntry->uiTokenIndex].param; ++i)
			{
				printf("%d  ", ((int*)(pData))[i]);
			}
		xcase TOK_BOOL_X:
			for (i=0; i<ParseDynObject[pEntry->uiTokenIndex].param; ++i)
			{
				if ( ((bool*)(pData))[i] )
					printf("true  ");
				else
					printf("false  ");
			}
		xcase TOK_STRING_X:
			printf("%s", (const char*)pData);
		xdefault:
			printf("Unhandled token in dynFxHeaderEntryPrint()");
	}
	printf("\n");

}

void dynFxHeaderPrint(DynFxHeader* pHeader)
{
	U32 uiEntryIndex;
	printf("\nHeader entries: %d\n", pHeader->uiNumEntries);

	for (uiEntryIndex=0; uiEntryIndex<pHeader->uiNumEntries; ++uiEntryIndex)
	{
		dynFxHeaderEntryPrint(&pHeader->pEntries[uiEntryIndex], pHeader->pData);
	}
	printf("Done\n\n");

}


static bool	dynCompileEvent(DynEvent* pEvent, const char* pcFileName)
{
	U32 uiTokenIndex = uiFirstDynObjectStaticToken;
	const U32 uiMaxEntriesPerHeader = 256;
	const U32 uiMaxDataSize = 256;
	U32 uiNumKeyFrames = eaSize(&pEvent->keyFrames);
	DynFxHeader* pKeyFrameHeaders = _alloca(sizeof(DynFxHeader) * uiNumKeyFrames );
	U32 uiKeyFrameIndex;

	if (uiNumKeyFrames == 0)
	{
		FxFileError(pcFileName, "Need at least one keyframe!");
		return false;
	}

	// Prepare the temp keyframe blocks, which we will copy over to appropriately sized ones once they are prepared
	for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
	{
		pKeyFrameHeaders[uiKeyFrameIndex].uiNumEntries = 0;
		pKeyFrameHeaders[uiKeyFrameIndex].pEntries = _alloca(sizeof(DynFxHeaderEntry) * uiMaxEntriesPerHeader);
		pKeyFrameHeaders[uiKeyFrameIndex].pData = _alloca(uiMaxDataSize);
	}

	// Process on a per-token basis
	while ( uiTokenIndex != uiDynObjectTokenTerminator )
	{
		if ( ParseDynObject[uiTokenIndex].type == TOK_IGNORE )
		{
			++uiTokenIndex;
			continue;
		}
		else
		{
			// Look from the higher orders down
			bool bValueOnlyToken = (uiTokenIndex < uiFirstDynObjectDynamicToken);
			bool bSpecifiedFirstFrame = false;


			if ( bValueOnlyToken )
			{
				for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
				{
					if (dynObjectInfoSpecifiesToken(&pEvent->keyFrames[uiKeyFrameIndex]->objInfo[edoValue], uiTokenIndex))
					{
						void* pData = TokenStoreGetPointer(ParseDynObject, uiTokenIndex, &pEvent->keyFrames[uiKeyFrameIndex]->objInfo[edoValue], 0);
						U8 uiDataSize = dynFxTokenSize(uiTokenIndex, pData);
						dynFxHeaderPushEntry(&pKeyFrameHeaders[0], uiMaxDataSize, uiTokenIndex, edoValue, pData, uiDataSize);
					}
				}
			}
			else
			{
				// First, do value changes, then add those to the rate for every keyframe
				for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
				{
				}


			}





		}
		++uiTokenIndex;
	}

	dynFxHeaderPrint(pKeyFrameHeaders);
	return true;
}
*/

U8 dynFxSizeOfToken(U32 uiTokenIndex, ParseTable* pParseTable)
{
	int iNumObjects = (pParseTable[uiTokenIndex].param)?pParseTable[uiTokenIndex].param:1;
	switch (pParseTable[uiTokenIndex].type & TOK_TYPE_MASK)
	{
		xcase TOK_F32_X:
		case TOK_QUATPYR_X:
		{
			return (U8)(sizeof(F32) * iNumObjects);
		}
		xcase TOK_INT_X:
		{
			return (U8)(sizeof(int) * iNumObjects);
		}
		xcase TOK_BOOL_X:
		case TOK_BOOLFLAG_X:
		{
			return (U8)(sizeof(bool) * iNumObjects);
		}
		xcase TOK_U8_X:
		{
			return (U8)(sizeof(U8) * iNumObjects);
		}
		xcase TOK_STRING_X:
		{
			return (U8)(sizeof(char*));
		}
		xdefault:
		{
			FatalErrorf("Failed to handle token type %d in dynFxSizeOfToken()!", uiTokenIndex);
		}
	}
	return 0;
}

#define ALLOCATE_AND_TRACK_SIZE_OF(some_size) _alloca(some_size); uiTotalSize += some_size
#define PACK_FX_BLOCK(srcAddress, some_size) memcpy(pCursor, srcAddress, some_size); pCursor += some_size


static DynFxPathSet* dynFxInfoCreateSamplePathSetFromEvent(DynEvent* pEvent, const char* pcFileName)
{
	// Do it per token...
	const U32 uiNumKeyFrames = eaSize(&pEvent->keyFrames);
	U32 uiTokenIndex = uiFirstDynObjectStaticToken;
	U32 uiDynPathIndex = 0;
	U32 uiStatPathIndex = 0;
	DynFxPathSet pathSet;
	U32 uiTotalSize = sizeof(pathSet);

	// Init
	pathSet.uiNumStaticPaths = pEvent->uiNumStaticPaths;
	pathSet.pStaticPaths = ALLOCATE_AND_TRACK_SIZE_OF(sizeof(DynFxStaticPath) * pathSet.uiNumStaticPaths);
	pathSet.uiNumDynamicPaths = pEvent->uiNumDynamicPaths;
	pathSet.pDynamicPaths = ALLOCATE_AND_TRACK_SIZE_OF(sizeof(DynFxDynamicPath) * pathSet.uiNumDynamicPaths);


	// Loop over tokens
	while ( uiTokenIndex <= pEvent->uiLargestTokenThatChanges )
	{
		U32 uiBitFieldIndex = uiTokenIndex - uiFirstDynObjectStaticToken;
		bool bStaticToken = uiTokenIndex < uiFirstDynObjectDynamicToken;
		if ( bStaticToken )
		{
			if ( TSTB(pEvent->bfAnyChanges.bf, uiBitFieldIndex) )
			{
				DynFxStaticPath* pPath = &pathSet.pStaticPaths[uiStatPathIndex++];
				U32 uiKeyFrameIndex;
				pPath->uiNumPathPoints = 1; // always have the first one
				pPath->uiTokenIndex = (U8)uiTokenIndex;
				pPath->uiDataSize = dynFxSizeOfToken(uiTokenIndex, ParseDynObject);
				pPath->uiCurrentPathPointIndex = 0;
				for (uiKeyFrameIndex=1; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
				{
					DynKeyFrame* pKeyFrame = pEvent->keyFrames[uiKeyFrameIndex];
					// See if this keyframe is relevant to this token
					if ( TSTB(pKeyFrame->bfChanges[edoValue].bf, uiBitFieldIndex) )
						++pPath->uiNumPathPoints;
				}
				pPath->pPathPoints = ALLOCATE_AND_TRACK_SIZE_OF(sizeof(DynFxStaticPathPoint) * pPath->uiNumPathPoints);
				{
					U32 uiPathPoint = 0;
					for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
					{
						DynKeyFrame* pKeyFrame = pEvent->keyFrames[uiKeyFrameIndex];
						// See if this keyframe is relevant to this token
						if ( uiKeyFrameIndex==0 || TSTB(pKeyFrame->bfChanges[edoValue].bf, uiBitFieldIndex) )
						{
							DynFxStaticPathPoint* pPoint = &pPath->pPathPoints[uiPathPoint++];
							pPoint->uiKeyFrameIndex = uiKeyFrameIndex;
							pPoint->data = 0; // will be set at creation time
						}
					}
				}
			}
		}
		else
		{
			// Check to see if it ever changes
			if ( TSTB(pEvent->bfAnyChanges.bf, uiBitFieldIndex) )
			{
				eDynObjectInfo edo = 0;
				if (uiTokenIndex == uiColorTokenIndex)
					pEvent->bColorChanges = true;
				else if (uiTokenIndex == uiAlphaTokenIndex)
					pEvent->bAlphaChanges = true;
				else if (uiTokenIndex == uiScaleTokenIndex)
					pEvent->bScaleChanges = true;
				for (edo = 0; edo < edoTotal; ++edo)
				{
					if ( TSTB(pEvent->bfChanges[edo].bf, uiBitFieldIndex) )
					{
						DynFxDynamicPath* pPath = &pathSet.pDynamicPaths[uiDynPathIndex++];
						U32 uiKeyFrameIndex;
						pPath->uiTokenIndex = (U8)uiTokenIndex;
						VALIDATE_PARSEDYNOBJECT_TOKEN_INDEX(uiTokenIndex);
						pPath->uiFloatsPer = MAX(ParseDynObject[uiTokenIndex].param, 1);
						pPath->uiNumPathPoints = pPath->uiFloatsPer;
						pPath->uiEDO = edo;
						pPath->uiCurrentPathPointIndex = 0;
						pPath->uiHasValuePath = TSTB(pEvent->bfChanges[edoValue].bf, uiBitFieldIndex);
						pPath->uiKeyTime = 0;

						for (uiKeyFrameIndex=1; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
						{
							DynKeyFrame* pKeyFrame = pEvent->keyFrames[uiKeyFrameIndex];
							// See if this keyframe is relevant to this token
							if ( TSTB(pKeyFrame->bfChanges[edo].bf, uiBitFieldIndex) )
								pPath->uiNumPathPoints += pPath->uiFloatsPer;
						}
						pPath->pPathPoints = ALLOCATE_AND_TRACK_SIZE_OF(sizeof(DynFxDynamicPathPoint) * pPath->uiNumPathPoints);

						// Mark path points with keyframe indices
						{
							U32 uiPathPoint = 0;
							for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex)
							{
								DynKeyFrame* pKeyFrame = pEvent->keyFrames[uiKeyFrameIndex];
								DynKeyFrame* pNextKeyFrame = (uiKeyFrameIndex+1 < uiNumKeyFrames)?pEvent->keyFrames[uiKeyFrameIndex+1]:NULL;
								// See if this keyframe is relevant to this token
								if ( uiKeyFrameIndex==0 || TSTB(pKeyFrame->bfChanges[edo].bf, uiBitFieldIndex) )
								{
									int i;
									for (i=0; i<pPath->uiFloatsPer; ++i)
									{
										pPath->pPathPoints[uiPathPoint].uiWhichFloat = i;
										pPath->pPathPoints[uiPathPoint].fDiffV = pPath->pPathPoints[uiPathPoint].fStartV = 0.0f;
										pPath->pPathPoints[uiPathPoint].uiInterpType = ediLinear;
										if ( pNextKeyFrame && pNextKeyFrame->objInfo[pPath->uiEDO].puiInterpTypes) {
											pPath->pPathPoints[uiPathPoint].uiInterpType = pNextKeyFrame->objInfo[pPath->uiEDO].puiInterpTypes[pPath->uiTokenIndex];
										}
										pPath->pPathPoints[uiPathPoint++].uiKeyFrameIndex = uiKeyFrameIndex;
									}
								}
							}
						}
					}
				}
			}
		}
		++uiTokenIndex;
	}


	// Ok, we've locally allocated a block, and set everything... Now, allocate the real block, copy it over and fix the pointers
	{
		DynFxPathSet* pNewPathSet = calloc(uiTotalSize, 1);
		char* pCursor = (char*)pNewPathSet;
		U32 uiStaticPathIndex, uiDynamicPathIndex;
		PACK_FX_BLOCK(&pathSet, sizeof(DynFxPathSet));
		pNewPathSet->uiTotalSize = uiTotalSize;
		// Pack Paths
		pNewPathSet->pStaticPaths = (DynFxStaticPath*)pCursor;
		for (uiStaticPathIndex=0; uiStaticPathIndex < pNewPathSet->uiNumStaticPaths; ++uiStaticPathIndex)
		{
			PACK_FX_BLOCK(&pathSet.pStaticPaths[uiStaticPathIndex], sizeof(DynFxStaticPath));
		}
		pNewPathSet->pDynamicPaths = (DynFxDynamicPath*)pCursor;
		for (uiDynamicPathIndex=0; uiDynamicPathIndex < pNewPathSet->uiNumDynamicPaths; ++uiDynamicPathIndex)
		{
			PACK_FX_BLOCK(&pathSet.pDynamicPaths[uiDynamicPathIndex], sizeof(DynFxDynamicPath));
		}

//#pragma warning( push )
//#pragma warning( disable : 6385 ) // /analyze thinks pNewPathSet->pStaticPaths is a pointer to a single element, not an array 
		// Pack path points
		for (uiStaticPathIndex=0; uiStaticPathIndex < pNewPathSet->uiNumStaticPaths; ++uiStaticPathIndex)
		{
			U32 uiPointIndex;
			pNewPathSet->pStaticPaths[uiStaticPathIndex].pPathPoints = (DynFxStaticPathPoint*)pCursor;
			for (uiPointIndex=0; uiPointIndex< pathSet.pStaticPaths[uiStaticPathIndex].uiNumPathPoints; ++uiPointIndex)
			{
				PACK_FX_BLOCK(&pathSet.pStaticPaths[uiStaticPathIndex].pPathPoints[uiPointIndex], sizeof(DynFxStaticPathPoint));
			}
			// Fix up pointer (change to offset)
			pNewPathSet->pStaticPaths[uiStaticPathIndex].pPathPoints = (DynFxStaticPathPoint*)((char*)pNewPathSet->pStaticPaths[uiStaticPathIndex].pPathPoints - (char*)pNewPathSet);

		}
		for (uiDynamicPathIndex=0; uiDynamicPathIndex < pNewPathSet->uiNumDynamicPaths; ++uiDynamicPathIndex)
		{
			U32 uiPointIndex;
			pNewPathSet->pDynamicPaths[uiDynamicPathIndex].pPathPoints = (DynFxDynamicPathPoint*)pCursor;
			for (uiPointIndex=0; uiPointIndex< pathSet.pDynamicPaths[uiDynamicPathIndex].uiNumPathPoints; ++uiPointIndex)
			{
				CHECK_FINITE(pathSet.pDynamicPaths[uiDynamicPathIndex].pPathPoints[uiPointIndex].fStartV);
				PACK_FX_BLOCK(&pathSet.pDynamicPaths[uiDynamicPathIndex].pPathPoints[uiPointIndex], sizeof(DynFxDynamicPathPoint));
			}
			// Fix up pointer (change to offset)
			pNewPathSet->pDynamicPaths[uiDynamicPathIndex].pPathPoints = (DynFxDynamicPathPoint*)((char*)pNewPathSet->pDynamicPaths[uiDynamicPathIndex].pPathPoints - (char*)pNewPathSet);
		}
//#pragma warning( pop ) 

		// Fix up pointers (change to offsets)
		pNewPathSet->pStaticPaths = (DynFxStaticPath*)((char*)pNewPathSet->pStaticPaths - (char*)pNewPathSet);
		pNewPathSet->pDynamicPaths = (DynFxDynamicPath*)((char*)pNewPathSet->pDynamicPaths - (char*)pNewPathSet);

		if ( (U64)(pCursor - (char*)pNewPathSet) != uiTotalSize)
		{
			FatalErrorFilenamef(pcFileName, "Failure to properly pack fx header block, mismatch in size!");
		}



		return pNewPathSet;
	}
}


static bool dynEventVerify(DynEvent* pEvent, const char* pcFileName, DynFxInfo* pInfo, DynFxDeps *pDeps)
{
	const U32 uiNumKeyFrames = eaSize(&pEvent->keyFrames);
	U32 uiKeyFrameIndex;


	FOR_EACH_IN_EARRAY(pEvent->eaMatConstRename, DynMNCRename, pMNCRename)
		if (!dynMNCRenameVerify(pMNCRename, pcFileName))
			return false;
	FOR_EACH_END


	// now verify every keyframe
	for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex )
	{
		if (!dynKeyFrameVerify(pEvent->keyFrames, uiKeyFrameIndex, pEvent, pcFileName, pInfo, pDeps))
			return false;
		if (pEvent->keyFrames[uiKeyFrameIndex]->pPhysicsInfo && pEvent->keyFrames[uiKeyFrameIndex]->pPhysicsInfo->bDebris)
			pEvent->bDebris = true;
	}

	// Now that every keyframe has been verified, we need to post-process the keyframes 
	// so that interpolating is a simple, fast affair
	{
		// First, Make sure they are sorted.
		DynFxTime uiFrameTimeStamp = 0;
		for (uiKeyFrameIndex=0; uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex )
		{
			if ( pEvent->keyFrames[uiKeyFrameIndex]->uiTimeStamp < uiFrameTimeStamp )
			{
				FxFileError(pcFileName, "Error in event %s. Keyframes are not sorted by timestamp.", pEvent->pcMessageType);
				return false;
			}
		}
	}

	// Create bitfield for whether this event touches a given token
	{
		U32 uiTokenIndex = uiFirstDynObjectStaticToken;
		eDynObjectInfo edo;
		for (edo=0; edo<edoTotal; ++edo)
		{
			ZeroStruct(&pEvent->bfChanges[edo]);
		}

		ZeroStruct(&pEvent->bfAnyChanges);
		pEvent->uiNumStaticPaths = pEvent->uiNumDynamicPaths = 0;

		while ( uiTokenIndex != uiDynObjectTokenTerminator )
		{
			U32 uiBitFieldIndex = uiTokenIndex - uiFirstDynObjectStaticToken;
			for (edo=0; edo<edoTotal; ++edo)
			{
				bool bFoundKey = false;
				for (uiKeyFrameIndex=0; !bFoundKey && uiKeyFrameIndex<uiNumKeyFrames; ++uiKeyFrameIndex )
				{
					DynKeyFrame* pKey = pEvent->keyFrames[uiKeyFrameIndex];
					if ( TSTB(pKey->bfChanges[edo].bf, uiBitFieldIndex) )
					{
						SETB(pEvent->bfChanges[edo].bf, uiBitFieldIndex);
						SETB(pEvent->bfAnyChanges.bf, uiBitFieldIndex);
						if ( uiTokenIndex >= uiFirstDynObjectDynamicToken )
							++pEvent->uiNumDynamicPaths;
						else
							++pEvent->uiNumStaticPaths;
						bFoundKey = true;
						pEvent->uiLargestTokenThatChanges = uiTokenIndex;
					}
				}
			}
			if (
				   (  TSTB(pEvent->bfChanges[edoAmp].bf, uiBitFieldIndex) && !TSTB(pEvent->bfChanges[edoFreq].bf, uiBitFieldIndex) )
				|| ( !TSTB(pEvent->bfChanges[edoAmp].bf, uiBitFieldIndex) &&  TSTB(pEvent->bfChanges[edoFreq].bf, uiBitFieldIndex) )
				)
			{
				VALIDATE_PARSEDYNOBJECT_TOKEN_INDEX(uiTokenIndex);
				FxFileError(pcFileName, "Must specify both Freq and Amp for Token %s to have a cycle!", ParseDynObject[uiTokenIndex].name);
				return false;
			}
			++uiTokenIndex;
		}
	}

	// If no token or geometry is ever specified, don't draw this token!
	if ( TSTB(pEvent->bfAnyChanges.bf, uiTextureTokenIndex - uiFirstDynObjectStaticToken)
		|| TSTB(pEvent->bfAnyChanges.bf, uiGeometryTokenIndex - uiFirstDynObjectStaticToken) 
		|| TSTB(pEvent->bfAnyChanges.bf, uiGetModelFromCostumeBoneIndex - uiFirstDynObjectStaticToken) 
		|| TSTB(pEvent->bfAnyChanges.bf, uiClothTokenIndex - uiFirstDynObjectStaticToken)
		|| TSTB(pEvent->bfAnyChanges.bf, uiMaterialTokenIndex - uiFirstDynObjectStaticToken) 
		|| TSTB(pEvent->bfAnyChanges.bf, uiSplatTypeTokenIndex - uiFirstDynObjectStaticToken) 
		)
	{
		pInfo->bDontDraw = false; 
	}
	else
	{
		if(pInfo->bGetClothModelFromWorld || pInfo->bGetModelFromWorld) {
			pInfo->bDontDraw = false;
		}

		FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
			FOR_EACH_IN_EARRAY(pKeyFrame->eaCostume, DynFxCostume, pCostume)
				if (pCostume->pcCostume ||
					pCostume->bCloneSourceCostume)
				{
					pInfo->bDontDraw = false;
				}
				else
				{
					FOR_EACH_IN_EARRAY(pCostume->eaParams, DynApplyParam, pParam)
						if (stricmp(pParam->pcTokenString, "CostumeName")==0)
							pInfo->bDontDraw = false;
					FOR_EACH_END;
				}
			FOR_EACH_END;
		FOR_EACH_END;
	}

	if ( TSTB(pEvent->bfAnyChanges.bf, uiColor1TokenIndex - uiFirstDynObjectStaticToken)
		|| TSTB(pEvent->bfAnyChanges.bf, uiColor2TokenIndex - uiFirstDynObjectStaticToken) 
		|| TSTB(pEvent->bfAnyChanges.bf, uiColor3TokenIndex - uiFirstDynObjectStaticToken) 
		)
	{
		pEvent->bMultiColor = true;
	}

	if (pInfo->bForceDontDraw)
		pInfo->bDontDraw = true;

	FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
		if (pKeyFrame->objInfo[edoValue].obj.light.eLightType == WL_LIGHT_PROJECTOR)
			pInfo->bDontDraw = true;
		if (pKeyFrame->objInfo[edoValue].obj.draw.eEntityMaterialMode == edemmTextureSwap)
			pInfo->bDontDraw = true;
	FOR_EACH_END;


	pEvent->bCreatesParticle = 0;
	FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
		if (pKeyFrame->eType == eDynKeyFrameType_Create)
		{
			pEvent->bCreatesParticle = 1;
			break;
		}
	FOR_EACH_END;

	// Now, compile the keyframes into compressed, header/dataset versions
	/*
	if (!dynCompileEvent(pEvent, pcFileName))
		return false;
		*/	
	pEvent->pPathSet = dynFxInfoCreateSamplePathSetFromEvent(pEvent, pcFileName);
	if (!pEvent->pPathSet)
	{
		FxFileError(pcFileName, "Failed to successfully create a pathset for FX %s!", pInfo->pcDynName);
		return false;
	}

	if (pEvent->pcMessageType == pcKillMessage)
	{
		pEvent->bKillEvent = 1;
		pEvent->bTriggerOnce = 1; // Should never need more than one kill event triggered
	}

	return true;
}

static bool dynParticleEmitterVerify( DynParticleEmitter* pEmitter, DynFxInfo* pDynFxInfo ) 
{
	U32 uiWeightTotal = eafSize(&pEmitter->eaWeights);
	if (pEmitter->fDrawDistance <= 0.0f)
		pEmitter->fDrawDistance = pDynFxInfo->fDrawDistance;
	if (pEmitter->fMinDrawDistance <= 0.0f)
		pEmitter->fMinDrawDistance = pDynFxInfo->fMinDrawDistance;
	if (eaSize(&pEmitter->eaAtNodes) > 16)
	{
		FxFileError(pDynFxInfo->pcFileName, "Emitter %s has more than 16 AtNodes!", pEmitter->pcTag);
		return false;
	}
	if (eaSize(&pEmitter->eaEmitTargetNodes) > 0 && eaSize(&pEmitter->eaEmitTargetNodes) != eaSize(&pEmitter->eaAtNodes))
	{
		FxFileError(pDynFxInfo->pcFileName, "Emitter %s has more EmitTargetNodes (%d) than AtNodes (%d)! Please make sure the numbers match.", pEmitter->pcTag, eaSize(&pEmitter->eaEmitTargetNodes), eaSize(&pEmitter->eaAtNodes));
		return false;
	}
	if (uiWeightTotal > 0 && uiWeightTotal != eaSize(&pEmitter->eaAtNodes))
	{
		FxFileError(pDynFxInfo->pcFileName, "Emitter %s has more Weights (%d) than AtNodes (%d)! Please make sure the numbers match.", pEmitter->pcTag, uiWeightTotal, eaSize(&pEmitter->eaAtNodes));
		return false;
	}
	// Normalize weights
	if (uiWeightTotal > 0)
	{
		F32 fTotal = 0.0f;
		U32 uiIndex;
		F32 fTotalInv;
		for (uiIndex = 0; uiIndex < uiWeightTotal; ++uiIndex)
			fTotal += pEmitter->eaWeights[uiIndex];
		if (fTotal <= 0.0f)
		{
			FxFileError(pDynFxInfo->pcFileName, "Weights in Emitter %s must add up to more than zero. Current total is %.2f", pEmitter->pcTag, fTotal);
			return false;
		}
		fTotalInv = 1.0f / fTotal;
		// Do actual normalization
		for (uiIndex = 0; uiIndex < uiWeightTotal; ++uiIndex)
			pEmitter->eaWeights[uiIndex] *= fTotalInv;
	}
	{
		DynFxFastParticleInfo* pInfo = GET_REF(pEmitter->hParticle);
		if (pInfo)
		{
			if (pInfo->bUniformLine && !pEmitter->bApplyCountEvenly)
			{
				FxFileError(pDynFxInfo->pcFileName, "Emitter %s: Must have flag 'ApplyCountEvenly' in emitter in order to use UniformLine in particle.", pEmitter->pcTag);
				return false;
			}
			if (pInfo->bCountByDistance && eafSize(&pEmitter->eaWeights))
			{
				FxFileError(pDynFxInfo->pcFileName, "Emitter %s: CountByDistance in fast particle set %s does not work with Weights!", pEmitter->pcTag, REF_STRING_FROM_HANDLE(pEmitter->hParticle));
				return false;
			}
			if (pInfo->bCountByDistance && (eaSize(&pEmitter->eaEmitTargetNodes) == 0) && !pEmitter->pcEmitTarget && !pEmitter->bPatternModel)
			{
				FxFileError(pDynFxInfo->pcFileName, "Emitter %s: CountByDistance in fast particle set %s requires an EmitTarget or a pattern model!", pEmitter->pcTag, REF_STRING_FROM_HANDLE(pEmitter->hParticle));
				return false;
			}
		}
	}

	return true;
}

static StashTable gtFxNames = NULL;

static void buildFxNameValidationStash(DynFxInfo*** peaInfoList)
{
	if (!gtFxNames)
	{
		gtFxNames = stashTableCreateWithStringKeys(eaSize(peaInfoList), StashDefault);
	}
	FOR_EACH_IN_EARRAY((*peaInfoList), DynFxInfo, pInfo)
		stashAddPointer(gtFxNames, pInfo->pcDynName, pInfo, false);
	FOR_EACH_END;
}

static bool checkForFxExistence(const char* pcName)
{
	devassert(gtFxNames);
	return stashFindPointer(gtFxNames, pcName, NULL) || dynFxInfoExists(pcName);
}

static bool dynFxChildCallVerify(DynChildCall* pChildCall, DynFxInfo*** peaInfoList, const char* pcInfoName, const char* pcFileName)
{
	const char* pcChildFxName = REF_STRING_FROM_HANDLE(pChildCall->hChildFx);
	if (stricmp(pcChildFxName, pcInfoName)==0)
	{
		FxFileError(pcFileName, "FX %s can not call itself, or an infinite loop is possible!", pcInfoName);
		return false;
	}
	if (!checkForFxExistence(pcChildFxName))
	{
		FxFileError(pcFileName, "FX %s trying to call invalid or non-existent FX %s. Invalidating.", pcInfoName, pcChildFxName);
		return false;
	}
	return true;
}

static bool dynFxChildCallCollectionVerify(DynChildCallCollection* pCollection, DynFxInfo*** peaInfoList, const char* pcInfoName, const char* pcFileName)
{
	FOR_EACH_IN_EARRAY(pCollection->eaChildCall, DynChildCall, pChildCall)
		if (!dynFxChildCallVerify(pChildCall, peaInfoList, pcInfoName, pcFileName))
			return false;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pCollection->eaChildCallList, DynChildCallList, pChildCallList)
		FOR_EACH_IN_EARRAY(pChildCallList->eaChildCall, DynChildCall, pChildCall)
			if (!dynFxChildCallVerify(pChildCall, peaInfoList, pcInfoName, pcFileName))
				return false;
		FOR_EACH_END;
	FOR_EACH_END;

	return true;
}

// currently just used to catch infinite loops
static bool dynFxInfoVerifyChildrenFX(DynFxInfo* pInfo, DynFxInfo*** peaInfoList)
{
	FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
		if (!dynFxChildCallCollectionVerify(&pLoop->childCallCollection, peaInfoList, pInfo->pcDynName, pInfo->pcFileName))
			return false;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->eaRaycasts, DynRaycast, pRaycast)
		FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pEvent)
			if (!dynFxChildCallCollectionVerify(&pEvent->childCallCollection, peaInfoList, pInfo->pcDynName, pInfo->pcFileName))
				return false;
		FOR_EACH_END;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->eaContactEvents, DynContactEvent, pContactEvent)
		if (!dynFxChildCallCollectionVerify(&pContactEvent->childCallCollection, peaInfoList, pInfo->pcDynName, pInfo->pcFileName))
			return false;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->events, DynEvent, pEvent)
		FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
			if (!dynFxChildCallCollectionVerify(&pKeyFrame->childCallCollection, peaInfoList, pInfo->pcDynName, pInfo->pcFileName))
				return false;
			if (pKeyFrame->pParentBhvr)
			{
				FOR_EACH_IN_EARRAY(pKeyFrame->pParentBhvr->eaNearEvents, DynParentNearEvent, pNearEvent)
					if (!dynFxChildCallCollectionVerify(&pNearEvent->childCallCollection, peaInfoList, pInfo->pcDynName, pInfo->pcFileName))
						return false;
				FOR_EACH_END;
			}
		FOR_EACH_END;
	FOR_EACH_END;

	return true;
}

static bool dynChildCallListSetup(DynChildCallList* pList)
{
	// Normalize probablities
	F32 fProbabilityTotal = 0.0f;
	F32 fFirst;
	bool bAllSame = true;
	if (eaSize(&pList->eaChildCall) == 0)
		return true;
	fFirst = pList->eaChildCall[0]->fChance;
	if (fFirst <= 0.0f)
		fFirst = 1.0f;
	FOR_EACH_IN_EARRAY_FORWARDS(pList->eaChildCall, DynChildCall, pChildCall)
		if (pChildCall->fChance <= 0.0f)
			pChildCall->fChance = 1.0f;
		fProbabilityTotal += pChildCall->fChance;
		if (pChildCall->fChance != fFirst)
			bAllSame = false;
	FOR_EACH_END;

	if (bAllSame)
	{
		pList->bEqualChance = true;
		return true;
	}

	// Otherwise, we need to normalize the probabilities so they add up to 1.0
	if ( fabsf(fProbabilityTotal - 1.0f ) > 0.0001f )
	{
		F32 fNormalizationFactor = 1.0f / fProbabilityTotal;
		FOR_EACH_IN_EARRAY_FORWARDS(pList->eaChildCall, DynChildCall, pChildCall)
		{
			pChildCall->fChance *= fNormalizationFactor;
		}
		FOR_EACH_END;
	}

	return true;
}

static bool dynChildCallCollectionSetup(DynChildCallCollection* pCollection, DynFxInfo* pInfo)
{
	FOR_EACH_IN_EARRAY(pCollection->eaChildCallList, DynChildCallList, pList)
		dynChildCallListSetup(pList);
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY(pCollection->eaChildCall, DynChildCall, pChild)
	{
		if (pChild->bSortAsBlock_Deprecated && pInfo->fileAge > SORTASBLOCK_DEPRECATION_DATE)
		{
			FxFileError(pInfo->pcFileName, "SortAsBlock is deprecated, and now does nothing. Please remove it.");
			return false;
		}

		if (pChild->bGroupTextures_Deprecated && pInfo->fileAge > GROUPTEXTURES_DEPRECATION_DATE)
		{
			FxFileError(pInfo->pcFileName, "GroupTextures is deprecated, and now does nothing. Please remove it.");
			return false;
		}

		if (pChild->fChance <= 0.0f)
			pChild->fChance = 1.0f;
	}
	FOR_EACH_END;

	return true;
}

static bool dynFxInfoSetupAllChildCallCollections(DynFxInfo* pInfo)
{
	FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
		dynChildCallCollectionSetup(&pLoop->childCallCollection, pInfo);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->eaRaycasts, DynRaycast, pRaycast)
		FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pEvent)
			dynChildCallCollectionSetup(&pEvent->childCallCollection, pInfo);
		FOR_EACH_END;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->eaContactEvents, DynContactEvent, pContactEvent)
		dynChildCallCollectionSetup(&pContactEvent->childCallCollection, pInfo);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->events, DynEvent, pEvent)
		FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
			dynChildCallCollectionSetup(&pKeyFrame->childCallCollection, pInfo);
			if (pKeyFrame->pParentBhvr)
			{
				FOR_EACH_IN_EARRAY(pKeyFrame->pParentBhvr->eaNearEvents, DynParentNearEvent, pNearEvent)
					dynChildCallCollectionSetup(&pNearEvent->childCallCollection, pInfo);
				FOR_EACH_END;
			}
		FOR_EACH_END;
	FOR_EACH_END;

	return true;
}

static bool dynFxInfoVerify(DynFxInfo* pDynFxInfo, DynFxDeps *pDeps)
{
	// Verify dyninfo param block
	pDynFxInfo->bDontDraw = true;

	pDynFxInfo->bSelfTerminates = true;

	{
		char cName[256];
		getFileNameNoExt(cName, pDynFxInfo->pcFileName);
		pDynFxInfo->pcDynName = allocAddString(cName);
	}


	if (pDynFxInfo->bForce2D && !pDynFxInfo->bOverrideForce2DNonLocal)
		pDynFxInfo->bLocalPlayerOnly = true;

	if (pDynFxInfo->fPlaybackJitter < 0.0f || pDynFxInfo->fPlaybackJitter >= 1.0f)
	{
		FxFileError(pDynFxInfo->pcFileName, "PlaybackJitter must be between 0.0 and 1.0");
		return false;
	}

	{
		U32 uiIndex;
		for (uiIndex=0; uiIndex< eaUSize(&pDynFxInfo->paramBlock.eaDefineParams); ++uiIndex)
		{
			DynDefineParam* pParam = pDynFxInfo->paramBlock.eaDefineParams[uiIndex];
			// Verify each parameter
			switch (pParam->mvVal.type)
			{
				xcase MULTI_FLOAT:
				{
				}
				xcase MULTI_VEC3:
				{
				}
				xcase MULTI_VEC4:
				{
				}
				xcase MULTI_STRING:
				{
				}
				xcase MULTI_INT:
				{
				}
				xdefault:
				{
					FxFileError(pDynFxInfo->pcFileName, "Invalid DefineParam type %s, Line %d", MultiValTypeToString(pParam->mvVal.type), pParam->iLineNum);
				}
			}
		}
	}

	// Verify all emitters
	FOR_EACH_IN_EARRAY(pDynFxInfo->eaEmitters, DynParticleEmitter, pEmitter)
	{
		if (!dynParticleEmitterVerify(pEmitter, pDynFxInfo))
		{
			return false;
		}
	}
	FOR_EACH_END;

	// Verify all lists
	FOR_EACH_IN_EARRAY(pDynFxInfo->eaLists, DynList, pList)
	{
		if (!dynListVerify(pList, pDynFxInfo->pcFileName))
		{
			return false;
		}
	}
	FOR_EACH_END;
	// Verify all loops
	FOR_EACH_IN_EARRAY(pDynFxInfo->eaLoops, DynLoop, pLoop)
	{
		if (pLoop->bSortAsBlock_Deprecated && pDynFxInfo->fileAge > SORTASBLOCK_DEPRECATION_DATE)
		{
			FxFileError(pDynFxInfo->pcFileName, "SortAsBlock is deprecated, and now does nothing. Please remove it.");
			return false;
		}

		if (pLoop->bGroupTextures_Deprecated && pDynFxInfo->fileAge > GROUPTEXTURES_DEPRECATION_DATE)
		{
			FxFileError(pDynFxInfo->pcFileName, "GroupTextures is deprecated, and now does nothing. Please remove it.");
			return false;
		}

		if (!dynLoopVerify(pLoop, pDynFxInfo->pcFileName))
		{
			return false;
		}
	}
	FOR_EACH_END;
	// Verify all raycasts
	FOR_EACH_IN_EARRAY(pDynFxInfo->eaRaycasts, DynRaycast, pRaycast)
	{
		if (!dynRaycastVerify(pRaycast, pDynFxInfo, pDynFxInfo->pcFileName))
		{
			return false;
		}
	}
	FOR_EACH_END;

	if (eaSize(&pDynFxInfo->eaContactEvents) > 32)
	{
		FxFileError(pDynFxInfo->pcFileName, "Contact Events in DynFxInfo %s exceeds limit of 32! Wow!", pDynFxInfo->pcDynName);
		return false;
	}
	FOR_EACH_IN_EARRAY(pDynFxInfo->eaContactEvents, DynContactEvent, pContactEvent)
	{
		if (!dynContactEventVerify(pContactEvent, pDynFxInfo, pDynFxInfo->pcFileName))
		{
			return false;
		}
	}
	FOR_EACH_END;


	dynFxInfoSetupAllChildCallCollections(pDynFxInfo);

	// now verify every event
	{
		const int iNumEvents = eaSize(&pDynFxInfo->events);
		int iEventIndex;
		if (iNumEvents > DYNFX_MAX_EVENTS)
		{
			FxFileError(pDynFxInfo->pcFileName, "Can not have more than 8 events per fx");
			return false;
		}
		for (iEventIndex=0; iEventIndex<iNumEvents; ++iEventIndex )
		{
			DynEvent* pEvent = pDynFxInfo->events[iEventIndex];
			if (!dynEventVerify(pEvent, pDynFxInfo->pcFileName, pDynFxInfo, pDeps))
			{
				return false;
			}
			pEvent->uiEventIndex = (U8)iEventIndex;
			if (pEvent->fAutoCallTime)
			{
				pDynFxInfo->bHasAutoEvents = true;
			}
		}
	}

	FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pEvent)
		if (pEvent->bLoop)
			pEvent->bKeepAlive = true;
		if (pEvent->bKeepAlive)
		{
			pDynFxInfo->bKillIfOrphaned = true;
			pDynFxInfo->bSelfTerminates = false;
		}
		if (pDynFxInfo->bHibernate && pEvent->bKeepAlive)
		{
			FxFileError(pDynFxInfo->pcFileName, "Can't use Hibernate with ContinuingFX. Hibernate keeps the FX alive after the event, so ContinuingFX should not be necessary");
			return false;
		}
	FOR_EACH_END;

	if (pDynFxInfo->fDrawDistance > 0.0f)
	{
		pDynFxInfo->fFadeDistance = pDynFxInfo->fDrawDistance * 0.9f;
	}

	pDynFxInfo->fMinFadeDistance = pDynFxInfo->fMinDrawDistance * 1.1f;

	if (pDynFxInfo->iPriorityLevel < 0 || pDynFxInfo->iPriorityLevel > 3)
	{
		FxFileError(pDynFxInfo->pcFileName, "Invalid Priority level %d! Must be between 0 and 3!", pDynFxInfo->iPriorityLevel);
		return false;
	}

	// Figure out fx radius
	/*
	pDynFxInfo->eRadiusType = edfrtStatic; // default to "static", meaning that it doesn't change over time.. usually radius 0.0
	FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pEvent)
		FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
			if (pKeyFrame->pParentBhvr && pKeyFrame->pParentBhvr->pcScaleToNode)
			{
				pDynFxInfo->eRadiusType = edfrtScaleTo;
			}
		FOR_EACH_END;
	FOR_EACH_END;
	*/
	FOR_EACH_IN_EARRAY(pDynFxInfo->eaEmitters, DynParticleEmitter, pEmitter)
		DynFxFastParticleInfo* pInfo = GET_REF(pEmitter->hParticle);
		if (pInfo)
		{
			pDynFxInfo->fRadius = MAX( pDynFxInfo->fRadius, pInfo->fRadius );
		}
	FOR_EACH_END;


	// Report errors on deprecated fields
	if (pDynFxInfo->bDontAutoKill_Deprecated && pDynFxInfo->fileAge > DONTAUTOKILL_DEPRECATION_DATE)
	{
			FxFileError(pDynFxInfo->pcFileName, "DontAutoKill is deprecated, and now does nothing. Please remove it.");
			return false;
	}
	// Report errors on deprecated fields
	if (pDynFxInfo->bKillOnEmpty_Deprecated && pDynFxInfo->fileAge > DONTAUTOKILL_DEPRECATION_DATE)
	{
			FxFileError(pDynFxInfo->pcFileName, "KillOnEmpty is deprecated, and now does nothing. Please remove it.");
			return false;
	}
	if (pDynFxInfo->bSortAsBlock_Deprecated && pDynFxInfo->fileAge > SORTASBLOCK_DEPRECATION_DATE)
	{
			FxFileError(pDynFxInfo->pcFileName, "SortAsBlock is deprecated, and now does nothing. Please remove it.");
			return false;
	}

	return true;
}

static dynFxInfoReloadCallback* eaFxReloadCallback;

void dynFxInfoAddReloadCallback(dynFxInfoReloadCallback pCallback)
{
	eaPush((cEArrayHandle*)&eaFxReloadCallback, pCallback);
}

void dynFxInfoReloadWorldCellEntries(const char* relpath, int when)
{
	char cName[256];
	getFileNameNoExt(cName, relpath);
	worldFxReloadEntriesMatchingFxName(cName);
}

StashTable stFxInfoFlags;

void dynFxInfoReloadInfoFlags(const char* relpath, int when)
{
	if (stFxInfoFlags)
	{
		char cName[256];
		getFileNameNoExt(cName, relpath);
		{
			REF_TO(DynFxInfo) hTemp;
			DynFxInfo *pTemp;
			SET_HANDLE_FROM_STRING(hDynFxInfoDict, cName, hTemp);
			pTemp = GET_REF(hTemp);
			if (pTemp)
			{
				stashAddInt(stFxInfoFlags, pTemp->pcDynName, dynFxFlagsFromInfo(pTemp), true);
			}
			REMOVE_HANDLE(hTemp);
		}
	}
}

AUTO_STRUCT;
typedef struct DynFxDataInfoSerialize
{
	const char *pcDynName; AST( POOL_STRING )
	U32 data_offset;
	const char *pcFileName; AST( POOL_STRING CURRENTFILE)
	DynFxDeps deps;
	U32 uiFxFlags;
} DynFxDataInfoSerialize;


AUTO_STRUCT AST_FIXUPFUNC(fixupDynFxInitialLoad);
typedef struct DynFxInitialLoad
{
	// Parsed text file info - at bin write time, this will be NULL, as it will have been packed into packedFxStream, and thence into packed_data_serialize.
	// In fact, the only time this array EVER exists is immediately after parsing from text files
	DynFxInfo** eaFxInfo; AST(NAME("DynFx") STRUCT(parse_DynFxInfo))

	// Written to .bin and read in, converted, freed
	SerializablePackedStructStream *packed_data_serialize; AST( NO_TEXT_SAVE )
	DynFxDataInfoSerialize **data_infos_serialize; AST( NO_TEXT_SAVE )

	// Run-time referenced
	PackedStructStream packedFxStream; NO_AST

} DynFxInitialLoad;

static DynFxInitialLoad fxLoadData={0};


void dynFxInfoGetFlagsIterator(StashTableIterator* pIter)
{
	stashGetIterator(stFxInfoFlags, pIter);
}

bool dynFxInfoSelfTerminates(const char* pcName)
{
	U32 uiFlags;
	if (pcName && stashFindInt(stFxInfoFlags, pcName, &uiFlags))
		return (uiFlags & FxInfoSelfTerminates);
	return true;
}

void AutoUnpackFxInfo(DictionaryHandleOrName dictHandle, int command, ConstReferenceData pRefData, Referent pReferent, const char* reason)
{
	errorIsDuringDataLoadingInc("bin/DynFx.bin");
	resUnpackHandleRequest(dictHandle, command, pRefData, pReferent, &fxLoadData.packedFxStream);
	errorIsDuringDataLoadingDec();
	++iNumFxInfoUnpacked;
}

static void dynFxReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading Dynfx...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	dynFxKillAllFx();
	dynFxClearMissingFiles();

	if(!ParserReloadFileToDictionary(relpath,hDynFxInfoDict))
	{
		// super redundant with all the errors that will get emitted
		//FxFileError(relpath, "Error reloading fx file: %s", relpath);
	}

	// Change the number to keep to 0 (meaning infinity) so that we don't ever lose our reloaded fx.
	resDictSetMaxUnreferencedResources(hDynFxInfoDict, RES_DICT_KEEP_ALL);

	//Reload entities automatically to ensure all fx we killed will get re-applied.
	globCmdParse("forceFullEntityUpdate");

	{
		int i;
		for (i=0; i<eaSizeUnsafe(&eaFxReloadCallback); i++) 
			eaFxReloadCallback[i](relpath, when);
	}

	loadend_printf("done");
}

static bool dynFxVerifyDeps(const char *pcFileName, DynFxDeps *deps)
{
	bool bRet=true;

	if (wl_state.tex_find_func)
	{
		FOR_EACH_IN_EARRAY(deps->texture_deps, const char, pcTextureName)
		{
			BasicTexture *tex;
			if (!(tex=wl_state.tex_find_func(pcTextureName, false, WL_FOR_FX)))
			{
				if(!gbIgnoreTextureErrors)
					FxFileError(pcFileName, "Can't find specified texture: %s", pcTextureName);
				bRet = false;
			} else {
				if (eaiGet(&deps->texture_dep_flags, ipcTextureNameIndex) & eDynFxDepFlag_UsedOnSplat)
				{
					if (!wl_state.tex_is_alpha_bordered_func(tex))
					{
						FxFileError(pcFileName, "Splat FX references non-alpha-bordered texture: %s\nPlease enable the alpha-border texture processing flag and reprocess with GetTex.", pcTextureName);
						bRet = false;
					}
				}
			}
			// Used to find unreferenced textures in the fx system, for purging
			if (bDynListAllUnusedTexturesAndGeometry)
				dynFxMarkTextureAsUsed(pcTextureName);
		}
		FOR_EACH_END;
	}
	FOR_EACH_IN_EARRAY(deps->material_deps, const char, pcMaterialName)
	{
		if(pcMaterialName && pcMaterialName[0]) {
			Material *material = materialFindNoDefault(pcMaterialName, 0);
			if (!material)
			{
				FxFileError(pcFileName, "Can't find specified material: %s", pcMaterialName);
				bRet = false;
			} else {
				// Verify referencing only FX-allowed templates
				const char *pcTemplateName=NULL;
				if (wl_state.material_validate_for_fx)
				{
					if (!wl_state.material_validate_for_fx(material, &pcTemplateName, !!(eaiGet(&deps->material_dep_flags, ipcMaterialNameIndex) & eDynFxDepFlag_UsedOnSplat)))
					{
						FxFileError(pcFileName, "FX references material \"%s\" referencing non-preloaded/non-FX template \"%s\"", pcMaterialName,
							pcTemplateName);
						if (!strstri(pcFileName, "/Test_Fx/")) // Allow/don't invalidate for test FX (but still Errorf)
						{
							bRet = false;
						}
					}
				}
			}
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(deps->geometry_deps, const char, pcModelName)
	{
		if (!modelExists(pcModelName))
		{
			FxFileError(pcFileName, "Can't find specified geometry: %s", pcModelName);
			bRet = false;
		} else {
			if (!(eaiGet(&deps->geometry_dep_flags, ipcModelNameIndex) & eDynFxDepFlag_MaterialNotUsed))
			{
				if (!materialVerifyObjectMaterialDepsForFx(pcFileName, pcModelName))
				{
					dynFxInfoLogBadFile(pcFileName);
					bRet = false;
				}
			}
		}
		dynFxMarkGeometryAsUsed(pcModelName);
	}
	FOR_EACH_END;
	return bRet;
}

TextParserResult fixupDynFxInfo(DynFxInfo* pDynFxInfo, enumTextParserFixupType eType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			char cName[256];
			getFileNameNoExt(cName, pDynFxInfo->pcFileName);
			pDynFxInfo->pcDynName = allocAddString(cName);
		}

// This verification is done on the whole list of DynFxInfos now, to keep track of dependencies
// 		xcase FIXUPTYPE_POST_TEXT_READ:
// 		//case FIXUPTYPE_POST_BIN_READ: // post bin read no longer has any DynFxInfos
// 			if (!dynFxInfoVerify(pDynFxInfo))
// 			{
// 				dynFxInfoLogBadFile(pDynFxInfo->pcFileName);
// 				return PARSERESULT_INVALID; // remove this from the list
// 			}

		xcase FIXUPTYPE_POST_RELOAD:
		{
			DynFxDeps deps = {0};
			if (!dynFxInfoVerify(pDynFxInfo, &deps) || !dynFxVerifyDeps(pDynFxInfo->pcFileName, &deps))
			{
				pDynFxInfo->bVerifyFailed = true;
				dynFxInfoLogBadFile(pDynFxInfo->pcFileName);
				ret = PARSERESULT_INVALID; // remove this from the list
			}
			else
			{
				ResourceInfo *pOldInfo = resGetInfo(hDynFxInfoDict, pDynFxInfo->pcDynName);
				if (pOldInfo && stricmp(pOldInfo->resourceLocation, pDynFxInfo->pcFileName) != 0 )
				{
					// Generate duplicate file errorf
					FxFileError(pDynFxInfo->pcFileName, "Duplicate FX found: %s", pOldInfo->resourceLocation);
					ret = PARSERESULT_INVALID;
				}
				else
					resUpdateInfo(hDynFxInfoDict, pDynFxInfo->pcDynName, parse_DynFxInfo, pDynFxInfo, NULL, NULL, NULL, NULL, NULL, false, false);
			}
			StructDeInit(parse_DynFxDeps, &deps);
		}
	}

	return ret;
}

void dynFxInfoChildCallFreeExtraData(DynChildCallCollection* pCollection)
{
	FOR_EACH_IN_EARRAY(pCollection->eaChildCall, DynChildCall, pChildCall)
		RefSystem_RemoveReferent(&pChildCall->paramBlock, false);
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY(pCollection->eaChildCallList, DynChildCallList, pChildCallList)
		FOR_EACH_IN_EARRAY(pChildCallList->eaChildCall, DynChildCall, pChildCall)
			RefSystem_RemoveReferent(&pChildCall->paramBlock, false);
		FOR_EACH_END;
	FOR_EACH_END;
}

void dynFxInfoFreeExtraData(DynFxInfo* pInfo)
{
	RefSystem_RemoveReferent(&pInfo->paramBlock, false);

	FOR_EACH_IN_EARRAY(pInfo->eaLoops, DynLoop, pLoop)
		dynFxInfoChildCallFreeExtraData(&pLoop->childCallCollection);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->eaRaycasts, DynRaycast, pRaycast)
		FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pEvent)
			dynFxInfoChildCallFreeExtraData(&pEvent->childCallCollection);
		FOR_EACH_END;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->eaContactEvents, DynContactEvent, pContactEvent)
		dynFxInfoChildCallFreeExtraData(&pContactEvent->childCallCollection);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pInfo->events, DynEvent, pEvent)
		FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
			eDynKeyFrameType edo;
			for (edo=0; edo<edoTotal; ++edo)
				SAFE_FREE(pKeyFrame->objInfo[edo].puiInterpTypes);
			dynFxInfoChildCallFreeExtraData(&pKeyFrame->childCallCollection);
			if (pKeyFrame->pParentBhvr)
			{
				FOR_EACH_IN_EARRAY(pKeyFrame->pParentBhvr->eaNearEvents, DynParentNearEvent, pNearEvent)
					dynFxInfoChildCallFreeExtraData(&pNearEvent->childCallCollection);
				FOR_EACH_END;
			}
		FOR_EACH_END;
		SAFE_FREE(pEvent->pPathSet);
	FOR_EACH_END;
}



static void dynFxInfoRefDictCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		DynFxInfo* pInfo = (DynFxInfo*)pReferent;

		// The only thing this does is stop double errors on reload.  If verify is succeeding, it still gets run twice.  This would be easy to fix, if we decide
		// we care.  The mechanism existed before, but didn't work. [RMARR - 3/30/12]
		if (!pInfo->bVerifyFailed)
		{
			DynFxDeps deps = {0};
			if (!dynFxInfoVerify(pInfo, &deps))
				pInfo->bVerifyFailed = true;
			if (!dynFxVerifyDeps(pInfo->pcFileName, &deps))
				pInfo->bVerifyFailed = true;
			StructDeInit(parse_DynFxDeps, &deps);
		}
	}
	else if (eType == RESEVENT_RESOURCE_REMOVED)
	{
		DynFxInfo* pInfo = (DynFxInfo*)pReferent;
		dynFxInfoFreeExtraData(pInfo);
	}
}


AUTO_RUN;
void registerDynFxInfoDictionary(void)
{
	ParserSetTableInfo(parse_DynFxInfo, sizeof(DynFxInfo), "DynFxInfo", fixupDynFxInfo, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfoRecurse(parse_DynFxInfo, sizeof(DynFxInfo), "DynFxInfo", NULL, __FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
	hDynFxInfoDict = RefSystem_RegisterSelfDefiningDictionary("DynFxInfo", false, parse_DynFxInfo, true, true, "DynFx");

	resDictRequestMissingResources(hDynFxInfoDict, RES_DICT_KEEP_ALL, false, AutoUnpackFxInfo);
	resDictRegisterEventCallback(hDynFxInfoDict, dynFxInfoRefDictCallback, NULL);

	if (isDevelopmentMode())
	{
		resDictMaintainInfoIndex(hDynFxInfoDict, NULL, NULL, NULL, NULL, NULL);
	}
}

// Print out information about the current state of the dynfxinfo dictionary
AUTO_COMMAND  ACMD_CATEGORY(dynFx);
void dfxPrintUnpackedFXList(void)
{
	RefDictIterator iter;
	int i = 0;
	const char* pcRef;
	RefSystem_InitRefDictIterator(hDynFxInfoDict, &iter);
	while (pcRef = RefSystem_GetNextReferentFromIterator(&iter))
	{
		++i;
	}
	printf("Counted %d Referents\n", i);

	RefSystem_InitRefDictIterator(hDynFxInfoDict, &iter);
}


static U32 dynFxFlagsFromInfo(const DynFxInfo* pInfo)
{
	return (pInfo->bSelfTerminates?FxInfoSelfTerminates:0);
}

TextParserResult fxLoadDataPreProcessor(DynFxInitialLoad *pFxLoadInfo)
{
	// Pack data, serialize, free parsed
	TextParserResult ret = PARSERESULT_SUCCESS;
	U32 packed_size;

	loadend_printf("done (%d DynFx Loaded)", eaSize(&pFxLoadInfo->eaFxInfo));

	loadstart_printf("Packing DynFx... ");

	buildFxNameValidationStash(&pFxLoadInfo->eaFxInfo);

	// Verify and pack
	PackedStructStreamInit(&pFxLoadInfo->packedFxStream, STRUCT_PACK_BITPACK);
	FOR_EACH_IN_EARRAY(pFxLoadInfo->eaFxInfo, DynFxInfo, pInfo)
	{
		DynFxDeps deps = {0};
		if (!dynFxInfoVerify(pInfo, &deps) || !dynFxInfoVerifyChildrenFX(pInfo, &pFxLoadInfo->eaFxInfo))
		{
			StructDeInit(parse_DynFxDeps, &deps);
			dynFxInfoLogBadFile(pInfo->pcFileName);
			ret = PARSERESULT_INVALID; // .bin file will not be written
		} else {
			DynFxDataInfoSerialize *info = StructAlloc(parse_DynFxDataInfoSerialize);
			info->data_offset = StructPack(parse_DynFxInfo, pInfo, &pFxLoadInfo->packedFxStream);
			info->pcDynName = pInfo->pcDynName;
			info->pcFileName = pInfo->pcFileName;
			info->uiFxFlags = dynFxFlagsFromInfo(pInfo);
			info->deps = deps;
			dynFxInfoFreeExtraData(pInfo); // we're going to free this pInfo set in a bit
			eaPush(&pFxLoadInfo->data_infos_serialize, info);
		}
	}
	FOR_EACH_END;
	PackedStructStreamFinalize(&pFxLoadInfo->packedFxStream);
	packed_size = PackedStructStreamGetSize(&pFxLoadInfo->packedFxStream);
	eaDestroyStruct(&pFxLoadInfo->eaFxInfo, parse_DynFxInfo);

	// Serialize
	pFxLoadInfo->packed_data_serialize = PackedStructStreamSerialize(&pFxLoadInfo->packedFxStream);
	PackedStructStreamDeinit(&pFxLoadInfo->packedFxStream);

	loadend_printf("done (%s)", friendlyBytes(packed_size));
	loadstart_printf("Writing .bin... ");

	return ret;
}

TextParserResult fxLoadDataPostProcessor(DynFxInitialLoad *pFxLoadInfo)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	// Deserialize
	loadend_printf("done.");
	loadstart_printf("Setting up DynFx run-time data... ");

	allocAddStringMapRecentMemory("SerializablePackedStructStream", __FILE__, __LINE__);
	PackedStructStreamDeserialize(&pFxLoadInfo->packedFxStream, pFxLoadInfo->packed_data_serialize);
	StructDestroySafe(parse_SerializablePackedStructStream, &pFxLoadInfo->packed_data_serialize);

	FOR_EACH_IN_EARRAY(pFxLoadInfo->data_infos_serialize, DynFxDataInfoSerialize, pInfo)
	{
		if (dynFxVerifyDeps(pInfo->pcFileName, &pInfo->deps))
		{
			bool bSucceeded = true;
			if (isDevelopmentMode())
			{
				ResourceInfo *pOldInfo = resGetInfo(hDynFxInfoDict, pInfo->pcDynName);
				if (pOldInfo && stricmp(pOldInfo->resourceLocation, pInfo->pcFileName) != 0 )
				{
					// Generate duplicate file errorf
					FxFileError(pInfo->pcFileName, "Duplicate FX found: %s", pOldInfo->resourceLocation);
					ret = PARSERESULT_INVALID;
					bSucceeded = false;
				}
				else
					resUpdateInfo(hDynFxInfoDict, pInfo->pcDynName, parse_DynFxDataInfoSerialize, pInfo, NULL, NULL, NULL, NULL, NULL, false, false);
			}
			if (bSucceeded)
			{
				resSetLocationID(hDynFxInfoDict, pInfo->pcDynName, pInfo->data_offset + 1); // Add 1 so 0 can be invalid
				stashAddInt(stFxInfoFlags, pInfo->pcDynName, pInfo->uiFxFlags, false);
			}
		}
		// otherwise, not added to the dictionary
	}
	FOR_EACH_END;

	eaDestroyStruct(&pFxLoadInfo->data_infos_serialize, parse_DynFxDataInfoSerialize);

	return ret;
}


TextParserResult fixupDynFxInitialLoad(DynFxInitialLoad *pFxLoadInfo, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
		// Verify internal consistency
		// Pack data, serialize, free parsed
		return fxLoadDataPreProcessor(pFxLoadInfo);

	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
		// Deserialize
		return fxLoadDataPostProcessor(pFxLoadInfo);

	}

	return PARSERESULT_SUCCESS;
}


void dynFxInfoLoadAll(void)
{
	// SharedMemory thoughts: could store and use the de-serialized packed struct
	//   stream in shared memory, and 

	loadstart_printf("DynFx Startup... ");

	//ErrorFilenamefSetCallback(dynFxInfoLogBadFile);
	// Disable errorFilenameF callback for now

	stFxInfoFlags = stashTableCreateWithStringKeys(4096, StashDefault);

	loadstart_printf("Loading DynFx... ");
	ParserLoadFiles("dyn/fx", ".dfx", "DynFx.bin", PARSER_OPTIONALFLAG|PARSER_BINS_ARE_SHARED|PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING, parse_DynFxInitialLoad, &fxLoadData);
	assert(!eaSize(&fxLoadData.data_infos_serialize));
	assert(!eaSize(&fxLoadData.eaFxInfo));
	assert(!fxLoadData.packed_data_serialize);
	loadend_printf("done.");

	loadstart_printf("Unpacking referenced DynFX... ");
	iNumFxInfoUnpacked = 0;
	if(gbMakeBinsAndExit) {
		resRequestAllResourcesInDictionary(hDynFxInfoDict);
	} else {
		resReRequestMissingResources(hDynFxInfoDict);
	}
	loadend_printf("done (%d Unpacked)", iNumFxInfoUnpacked);

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/fx/*.dfx", dynFxReloadCallback);
		dynFxInfoAddReloadCallback(dynFxInfoReloadWorldCellEntries);
		dynFxInfoAddReloadCallback(dynFxInfoReloadInfoFlags);
	}

	//ErrorFilenamefSetCallback(NULL);
	//loadend_printf("done (%d DynFx), %s", resGetNumberOfInfosEvenPacked(hDynFxInfoDict), friendlyBytes(PackedStructStreamGetSize(&fxLoadData.packedFxStream)));

	if (bDynListAllUnusedTexturesAndGeometry)
		dynFxPrintUnusedTexturesAndGeometry();

	loadend_printf("done (%d DynFx Loaded)", resGetNumberOfInfosEvenPacked(hDynFxInfoDict));
}


// Put the object descriptor stuff here, it's used in two parse tables
U32 uiFirstDynObjectStaticToken=0;
U32 uiFirstDynObjectDynamicToken=0;
U32 uiDynObjectTokenTerminator=0;
U32 uiTextureTokenIndex;
U32 uiTexture2TokenIndex;
U32 uiMaterialTokenIndex;
U32 uiGeoDissolveMaterialTokenIndex;
U32 uiGeoAddMaterialsTokenIndex;
U32 uiMaterial2TokenIndex;
U32 uiGeometryTokenIndex;
U32 uiGetModelFromCostumeBoneIndex;
U32 uiClothTokenIndex;
U32 uiFlareTypeTokenIndex;
U32 uiLightTypeTokenIndex;
U32 uiMeshTrailTypeTokenIndex;
U32 uiShakePowerTokenIndex;
U32 uiShakeSpeedTokenIndex;
U32 uiCameraFOVTokenIndex;
U32 uiCameraInfluenceTokenIndex;
U32 uiAttachCameraTokenIndex;
U32 uiCameraDelaySpeedTokenIndex;
U32 uiCameraLookAtNodeTokenIndex;
U32 uiSplatTypeTokenIndex;
U32 uiSkyNameTokenIndex;
U32 uiColorTokenIndex;
U32 uiColor1TokenIndex;
U32 uiColor2TokenIndex;
U32 uiColor3TokenIndex;
U32 uiAlphaTokenIndex;
U32 uiScaleTokenIndex;
U32 uiScale2TokenIndex;
U32 uiTimeScaleTokenIndex;
U32 uiTimeScaleChildrenTokenIndex;

void dynFxSysInit(void)
{
	// Count some fields in DynParseObject
	U32 uiParseTableIndex = 0;
	const ParseTable* pParseTable = &ParseDynObject[uiParseTableIndex];

	loadstart_printf("DynFx System Init...");

	dynDebugState.fDynFxRate = 1.0f;

	// This is the max number of fx drawn every frame
	dynDebugState.uiMaxDrawn = 150; 
	dynDebugState.uiMaxPriorityDrawn = 3;

	while (pParseTable && pParseTable->type != TOK_END)
	{
		if ( stricmp(pParseTable->name, "StartOfValueToks")==0)
			uiFirstDynObjectStaticToken = uiParseTableIndex+1;
		else if ( stricmp(pParseTable->name, "StartOfDynamicToks")==0)
			uiFirstDynObjectDynamicToken = uiParseTableIndex+1;
		else if (stricmp(pParseTable->name, "Texture")==0)
			uiTextureTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Texture2")==0)
			uiTexture2TokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Material")==0)
			uiMaterialTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "GeoDissolveMaterial")==0)
			uiGeoDissolveMaterialTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "GeoAddMaterials")==0)
			uiGeoAddMaterialsTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "ClothBackMaterial")==0 || stricmp(pParseTable->name, "Material2")==0)
			uiMaterial2TokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Geometry")==0)
			uiGeometryTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "GetModelFromCostumeBone")==0)
			uiGetModelFromCostumeBoneIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Cloth")==0)
			uiClothTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "FlareType")==0)
			uiFlareTypeTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "LightType")==0)
			uiLightTypeTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Trail")==0)
			uiMeshTrailTypeTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "ShakePower")==0)
			uiShakePowerTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "ShakeSpeed")==0)
			uiShakeSpeedTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "FOV")==0)
			uiCameraFOVTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "AttachCamera")==0)
			uiAttachCameraTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "CameraControlInfluence")==0)
			uiCameraInfluenceTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "CameraDelaySpeed")==0)
			uiCameraDelaySpeedTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "CameraLookAt")==0)
			uiCameraLookAtNodeTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "SplatType")==0)
			uiSplatTypeTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "SkyName")==0)
			uiSkyNameTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Color")==0)
			uiColorTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Color1")==0)
			uiColor1TokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Color2")==0)
			uiColor2TokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Color3")==0)
			uiColor3TokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Scale")==0)
			uiScaleTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Scale2")==0)
			uiScale2TokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "Alpha")==0)
			uiAlphaTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "TimeScale")==0)
			uiTimeScaleTokenIndex = uiParseTableIndex;
		else if (stricmp(pParseTable->name, "TimeScaleChildren")==0)
			uiTimeScaleChildrenTokenIndex = uiParseTableIndex;

		++pParseTable;
		++uiParseTableIndex;
	}
	uiDynObjectTokenTerminator = uiParseTableIndex;

	if ( uiDynObjectTokenTerminator - uiFirstDynObjectStaticToken > MAX_DYN_FX_TOKENS )
		FatalErrorf("Too many dyn object tokens, you are goingt to have to change some bitfields in DynFx struct!");

	initDynObjectToDynDrawParticleTokenMap();
	initDynObjectToDynFlareTokenMap();
	initDynObjectToDynLightTokenMap();
	initDynObjectToDynCameraInfoTokenMap();
	initDynObjectToDynSplatTokenMap();
	initDynObjectToDynSkyVolumeTokenMap();
	initDynObjectToDynMeshTrailInfoTokenMap();
	initDynObjectToDynFxControlInfoTokenMap();
	initParamTokenTypeMap();
	initDynFxPhysics();

	if ( !materialExists("Particle_NoCutout"))
	{
		FatalErrorf("FX System requires material Particle_NoCutout, the default sprite material, in order to draw particles!");
	}

	loadend_printf(" done.");

	dynFxFastParticleLoadAll();
	dynFxInfoLoadAll();
	dynFxDamageInfoLoadAll();
	dynFxManagerInit();
	fxLog.eaLines = NULL;
	CHECK_FX_COUNT;
}

void dynFxSetSoundVerifyFunc(dynFxSoundVerifyFunc func)
{
	sndVerifyFunc = func;
}

void dynFxSetDSPVerifyFunc(dynFxDSPVerifyFunc func)
{
	sndVerifyDSPFunc = func;
}

void dynFxSetSoundInvalidateFunc(dynFxSoundInvalidateFunc func)
{
	sndInvalidateFunc = func;
}


void dynFxMarkTextureAsUsed(const char* pcTextureName)
{
	if (!stUsedTextures)
		stUsedTextures = stashTableCreateWithStringKeys(1024, StashDefault);
	stashAddInt(stUsedTextures, allocAddString(pcTextureName), 1, false);
}

static void dynFxMarkGeometryAsUsed(const char* pcGeometry)
{
	if (!stUsedGeometry)
		stUsedGeometry = stashTableCreateWithStringKeys(1024, StashDefault);
	stashAddInt(stUsedGeometry, allocAddString(pcGeometry), 1, false);
}

int iTotalFoundUnused;

static FileScanAction checkTextureUsage(char* dir, struct _finddata32_t* data, void *pUserData)
{
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;

	{
		char cName[256];
		getFileNameNoExt(cName, data->name);
		if (!stashFindInt(stUsedTextures, cName, NULL))
		{
			printf("%s/%s\n", dir, data->name);
			++iTotalFoundUnused;
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}


static void dynFxPrintUnusedTexturesAndGeometry(void)
{
	iTotalFoundUnused = 0;
	printf("Unused Textures:\n--------------------------\n");
	fileScanAllDataDirs("texture_library/fx", checkTextureUsage, NULL);
	printf("-------------------\n%d Found.", iTotalFoundUnused);



	iTotalFoundUnused = 0;
	printf("Unused Geometry:\n--------------------------\n");
	{
		RefDictIterator iter;
		ModelHeader* pModelHeader;
		RefSystem_InitRefDictIterator("ModelHeader", &iter);
		while(pModelHeader = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (strstri(pModelHeader->filename, "object_library/fx"))
			{
				if (!stashFindInt(stUsedGeometry, pModelHeader->modelname, NULL))
				{
					printf("%s\n", pModelHeader->filename);
					++iTotalFoundUnused;
				}
			}
		}
	}
	printf("-------------------\n%d Found.", iTotalFoundUnused);
}

MultiVal *dynFxInfoGetParamValueFromBlock(const char *paramName, DynParamBlock *block)
{
	MultiVal *ret = NULL;
	int i;

	for(i = 0; i < eaSize(&block->eaDefineParams); i++) {
		if(stricmp(block->eaDefineParams[i]->pcParamName, paramName) == 0) {
			ret = MultiValCreate();
			MultiValCopy(ret, &(block->eaDefineParams[i]->mvVal));
			break;
		}
	}

	return ret;
}

MultiVal *dynFxInfoGetDefaultParamValueFromBlock(const char *paramName, DynParamBlock *block)
{
	MultiVal *ret = NULL;
	int i;
	
	DynDefineParam *pDefineParam;
	for (i = 0; (i < eaSize(&(block->eaDefineParams))); i++)
	{
		pDefineParam = block->eaDefineParams[i];
		if (pDefineParam->pcParamName == paramName)
		{
			ret = MultiValCreate();
			MultiValCopy(ret, &(pDefineParam->mvVal));
			break;
		}
	}

	return ret;
}

MultiVal *dynFxInfoGetParamValue(const char *fxName, const char *paramName)
{
	REF_TO(DynFxInfo) hInfo;
	DynFxInfo *pFxInfo;
	MultiVal *retVal = NULL;

	// Get the param block from the FX info.
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, fxName, hInfo);
	pFxInfo = GET_REF(hInfo);

	if(pFxInfo) {
		retVal = dynFxInfoGetParamValueFromBlock(paramName, &pFxInfo->paramBlock);
	}

	REMOVE_HANDLE(hInfo);

	return retVal;
}

MultiVal *dynFxInfoGetDefaultParamValue(const char *fxName, const char *paramName)
{
	REF_TO(DynFxInfo) hInfo;
	DynFxInfo *pFxInfo;
	MultiVal *retVal = NULL;

	// Get the param block from the FX info.
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, fxName, hInfo);
	pFxInfo = GET_REF(hInfo);

	if(pFxInfo)
	{
		retVal = dynFxInfoGetDefaultParamValueFromBlock(paramName, &pFxInfo->paramBlock);
	}

	REMOVE_HANDLE(hInfo);

	return retVal;
}

static bool dynFxInfo_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

void dynFxInfo_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	DynFxInfo *pDynFxInfo;
	ResourceIterator rI;

	resRequestAllResourcesInDictionary(hDynFxInfoDict);

	*ppcType = strdup("DynFx");

	resInitIterator(hDynFxInfoDict, &rI);
	while (resIteratorGetNext(&rI, NULL, &pDynFxInfo))
	{
		bool bResourceHasAudio = false;

		FOR_EACH_IN_EARRAY(pDynFxInfo->eaRaycasts, DynRaycast, pRaycast) {
			FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pRaycastHitEvent) {
				FOR_EACH_IN_EARRAY(pRaycastHitEvent->eaSoundStart, char, pcSound)
				{
					bResourceHasAudio |= dynFxInfo_GetAudioAssets_HandleString(pcSound, peaStrings);
				}
				FOR_EACH_END;
			} FOR_EACH_END;
		} FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pDynFxInfo->eaContactEvents, DynContactEvent, pContactEvent) {
			FOR_EACH_IN_EARRAY(pContactEvent->eaSoundStart, char, pcSound)
			{
				bResourceHasAudio |= dynFxInfo_GetAudioAssets_HandleString(pcSound, peaStrings);
			}
			FOR_EACH_END;
		} FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pEvent) {
			FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyframe) {	
				FOR_EACH_IN_EARRAY(pKeyframe->ppcSoundStarts, char, pcSound)
				{
					bResourceHasAudio |= dynFxInfo_GetAudioAssets_HandleString(pcSound, peaStrings);

				}
				FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pKeyframe->ppcSoundEnds, char, pcSound)
				{
					bResourceHasAudio |= dynFxInfo_GetAudioAssets_HandleString(pcSound, peaStrings);
				}
				FOR_EACH_END;
			} FOR_EACH_END;
		} FOR_EACH_END;

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "dynFxInfo_c_ast.c"
