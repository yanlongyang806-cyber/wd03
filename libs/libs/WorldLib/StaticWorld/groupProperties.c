/***************************************************************************



***************************************************************************/


#include "net/net.h"

#include "WorldGridLoad.h"
#include "groupProperties.h"
#include "objPath.h"
#include "tokenstore.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););


typedef enum GroupDefPropConvertTypes
{
	GDPTT_Flag = 0,
	GDPTT_Float,
	GDPTT_Int,
	GDPTT_String,
	GDPTT_Pooled,
	GDPTT_Vec3,
	GDPTT_OccFace,
	GDPTT_CarryEnum,
	GDPTT_LAEnum,
	GDPTT_LTEnum,
	GDPTT_RoomEnum,
	GDPTT_BoxVol,
	GDPTT_SphrVol,
	GDPTT_VolType,
	GDPTT_ChildSelect,
	GDPTT_Deprecated,
} GroupDefPropConvertTypes;

typedef struct GroupDefPropConvertEntry {
	const char *pcPropName;
	GroupDefPropConvertTypes eType;
	const char *pcPropPath;
	ParseTable *parentPti;
	bool bIsLight;
} GroupDefPropConvertEntry;

#define GroupDefPropConvertEntryCnt 166
GroupDefPropConvertEntry g_GroupDefPropConvertTable[GroupDefPropConvertEntryCnt] = 
{
	{"Alpha",							GDPTT_Float,		".Physical.Alpha",							NULL, 0},
	{"LodScale",						GDPTT_Float,		".Physical.LodScale",						NULL, 0},
	{"AlwaysCollide",					GDPTT_Flag,			".Physical.AlwaysCollide",					NULL, 0},
	{"AxisCameraFacing",				GDPTT_Flag,			".Physical.AxisCameraFacing",				NULL, 0},
	{"CameraFacing",					GDPTT_Flag,			".Physical.CameraFacing",					NULL, 0},
	{"CastCloseShadowsOnly",			GDPTT_Deprecated,	NULL,										NULL, 0},
	{"Debris",							GDPTT_Flag,			".Physical.IsDebris",						NULL, 0},
	{"DontCastShadows",					GDPTT_Flag,			".Physical.DontCastShadows",					NULL, 0},
	{"DontReceiveShadows",				GDPTT_Flag,			".Physical.DontReceiveShadows",				NULL, 0},
	{"DoubleSidedOccluder",				GDPTT_Flag,			".Physical.DoubleSidedOccluder",				NULL, 0},
	{"EditorVisibleOnly",				GDPTT_Flag,			".Physical.EditorVisibleOnly",				NULL, 0},
	{"HideOnPlace",						GDPTT_Flag,			".Physical.HideOnPlace",						NULL, 0},
	{"HighDetail",						GDPTT_Flag,			".Physical.HighDetail",						NULL, 0},
	{"HighFillDetail",					GDPTT_Flag,			".Physical.HighFillDetail",					NULL, 0},
	{"IgnoreLODOverride",				GDPTT_Flag,			".Physical.IgnoreLODOverride",				NULL, 0},
	{"HandPivot",						GDPTT_Flag,			".Physical.HandPivot",						NULL, 0},
	{"MassPivot",						GDPTT_Flag,			".Physical.MassPivot",						NULL, 0},
	{"CarryAnimationBit",				GDPTT_CarryEnum,	".Physical.CarryAnimationBit",				NULL, 0},
	{"InstanceOnPlace",					GDPTT_Flag,			".Physical.InstanceOnPlace",					NULL, 0},
	{"IsAutoPlacer",					GDPTT_Deprecated,	NULL,										NULL, 0},
	{"IsAutoPlacerOverride",			GDPTT_Deprecated,	NULL,										NULL, 0},
	{"LowDetail",						GDPTT_Flag,			".Physical.LowDetail",						NULL, 0},
	{"MapSnapHidden",					GDPTT_Flag,			".Physical.MapSnapHidden",					NULL, 0},	
	{"NamedPoint",						GDPTT_Flag,			".Physical.NamedPoint",						NULL, 0},	
	{"RoomExclude",						GDPTT_Flag,			".Physical.RoomExcluded",					NULL, 0},	
	{"NoCollision",						GDPTT_Flag,			".Physical.NoCollision",						NULL, 0},	
	{"FullCollision",					GDPTT_Flag,			".Physical.FullCollision",					NULL, 0},	
	{"CameraCollision",					GDPTT_Flag,			".Physical.CameraCollision",					NULL, 0},	
	{"NoCollisionWithCamera",			GDPTT_Flag,			".Physical.TranslucentWhenCameraCollides",	NULL, 0},	
	{"NoOcclusion",						GDPTT_Flag,			".Physical.NoOcclusion",						NULL, 0},	
	{"NoVertexLighting",				GDPTT_Flag,			".Physical.NoVertexLighting",				NULL, 0},	
	{"UseCharacterLighting",			GDPTT_Flag,			".Physical.UseCharacterLighting",				NULL, 0},	
	{"OccluderFaces",					GDPTT_OccFace,		".Physical.OccluderFaces",					NULL, 0},	
	{"OcclusionOnly",					GDPTT_Flag,			".Physical.OcclusionOnly",					NULL, 0},	
	{"OnlyAVolume",						GDPTT_Flag,			".Physical.OnlyAVolume",						NULL, 0},	
	{"Permeable",						GDPTT_Flag,			".Physical.Permeable",						NULL, 0},	
	{"RandomSelect",					GDPTT_Flag,			".Physical.RandomSelect",					NULL, 0},	
	{"SubObjectEditOnPlace",			GDPTT_Flag,			".Physical.SubObjectEditOnPlace",			NULL, 0},	
	{"TagID",							GDPTT_Int,			".Physical.TagID",							NULL, 0},
	{"WeldInstances",					GDPTT_Int,			".Physical.WeldInstances",					NULL, 0},
	{"ChildSelect",						GDPTT_ChildSelect,	".Physical.ChildSelect",						NULL, 0},
	{"MinLevel",						GDPTT_Deprecated,	NULL,										NULL, 0},
	{"MaxLevel",						GDPTT_Deprecated,	NULL,										NULL, 0},
	{"InteractionClass",				GDPTT_Deprecated,	NULL,										NULL, 0},
	{"InteractionType",					GDPTT_Deprecated,	NULL,										NULL, 0},
	{"EncounterDef",					GDPTT_Deprecated,	NULL,										NULL, 0},
	{"AmbientJobLocation",				GDPTT_Deprecated,	NULL,										NULL, 0},
	{"CivilianGenerator",				GDPTT_Flag,			".Physical.CivilianGenerator",				NULL, 0},
	{"CombatJobLocation",				GDPTT_Deprecated,	NULL,										NULL, 0},
	{"FadeNode",						GDPTT_Flag,			".Physical.FadeNode",						NULL, 0},
	{"ForceTrunkWind",					GDPTT_Flag,			".Physical.ForceTrunkWind",					NULL, 0},
	{"EncounterActor",					GDPTT_Deprecated,	NULL,										NULL, 0},
	{"IsDebrisFieldCont",				GDPTT_Flag,			".Physical.IsDebrisFieldCont",				NULL, 0},	
	{"NoChildOcclusion",				GDPTT_Flag,			".Physical.NoChildOcclusion",				NULL, 0},	
	{"DummyGroup",						GDPTT_Flag,			".Physical.DummyGroup",						NULL, 0},	
	{"ForbiddenPosition",				GDPTT_Flag,			".Physical.ForbiddenPosition",				NULL, 0},	
	
	{"BuildingGenBottomOffset",			GDPTT_Float,		".Physical.BuildingGenBottomOffset",			NULL, 0},
	{"BuildingGenTopOffset",			GDPTT_Float,		".Physical.BuildingGenTopOffset",			NULL, 0},

	{"CurveLength",						GDPTT_Float,		".Physical.CurveLength",						NULL, 0},

	{"ExcludeOtherBegins",				GDPTT_Deprecated,	NULL,										NULL, 0},
	{"ExcludeOthersBegin",				GDPTT_Int,			".Terrain.ExcludeOthersBegin",				NULL, 0},
	{"ExcludeOthersEnd",				GDPTT_Int,			".Terrain.ExcludeOthersEnd",					NULL, 0},
	{"ExcludePriority",					GDPTT_Float,		".Terrain.ExcludePriority",					NULL, 0},
	{"ExcludePriorityScale",			GDPTT_Float,		".Terrain.ExcludePriorityScale",				NULL, 0},
	{"ExcludeSame",						GDPTT_Float,		".Terrain.ExcludeSame",						NULL, 0},
	{"MultiExclusionVolumesDensity",	GDPTT_Float,		".Terrain.MultiExclusionVolumesDensity",		NULL, 0},
	{"MultiExclusionVolumesRequired",	GDPTT_Float,		".Terrain.MultiExclusionVolumesRequired",	NULL, 0},
	{"MultiExclusionVolumesRotation",	GDPTT_Int,			".Terrain.MultiExclusionVolumesRotation",	NULL, 0},
	{"ScaleMin",						GDPTT_Float,		".Terrain.ScaleMin",							NULL, 0},
	{"ScaleMax",						GDPTT_Float,		".Terrain.ScaleMax",							NULL, 0},
	{"SnapToTerrainNormal",				GDPTT_Float,		".Terrain.SnapToTerrainNormal",				NULL, 0},
	{"VaccuFormBrush",					GDPTT_String,		".Terrain.VaccuFormBrush",					NULL, 0},
	{"VaccuFormFalloff",				GDPTT_Float,		".Terrain.VaccuFormFalloff",					NULL, 0},
	{"IntensityVariation",				GDPTT_Float,		".Terrain.IntensityVariation",				NULL, 0},
	{"TerrainObject",					GDPTT_Flag,			".Terrain.TerrainObject",					NULL, 0},
	{"ExclusionAllowOnPath",			GDPTT_Deprecated,	NULL,										NULL, 0},
	{"SnapToTerrainHeight",				GDPTT_Flag,			".Terrain.SnapToTerrainHeight",				NULL, 0},
	{"SnapToTerrainNormal",				GDPTT_Flag,			".Terrain.SnapToNormal",					NULL, 0},
	{"VaccuFormMe",						GDPTT_Flag,			".Terrain.VaccuFormMe",						NULL, 0},
	{"GetTerrainColor",					GDPTT_Flag,			".Terrain.GetTerrainColor",					NULL, 0},
	{"VolumeName",						GDPTT_String,		".Terrain.VolumeName",						NULL, 0},

	{"FX",								GDPTT_Pooled,		".FXProperties.Name",						parse_WorldFXProperties, 0},
	{"FX_condition",					GDPTT_String,		".FXProperties.Condition",					parse_WorldFXProperties, 0},
	{"FX_params",						GDPTT_String,		".FXProperties.Params",						parse_WorldFXProperties, 0},
	{"FX_faction",						GDPTT_String,		".FXProperties.Faction",						parse_WorldFXProperties, 0},
	{"FX_Hue",							GDPTT_Float,		".FXProperties.Hue",							parse_WorldFXProperties, 0},
	{"FX_Has_Target",					GDPTT_Flag,			".FXProperties.HasTarget",					parse_WorldFXProperties, 0},
	{"FX_Target_No_Anim",				GDPTT_Flag,			".FXProperties.TargetNoAnim",				parse_WorldFXProperties, 0},
	{"FX_Target_Pos",					GDPTT_Vec3,			".FXProperties.TargetPos",					parse_WorldFXProperties, 0},
	{"FX_Target_Pyr",					GDPTT_Vec3,			".FXProperties.TargetPyr",					parse_WorldFXProperties, 0},

	{"genesisCompleteName",				GDPTT_String,		".GenesisProperties.GenesisCompleteName",				parse_WorldGenesisProperties, 0},
	{"GenesisNode",						GDPTT_Int,			".GenesisProperties.NodeType",							parse_WorldGenesisProperties, 0},
	{"GENESIS_DETAIL",					GDPTT_Flag,			".GenesisProperties.IsDetail",							parse_WorldGenesisProperties, 0},
	{"GENESIS_NODES",					GDPTT_Flag,			".GenesisProperties.IsNode",							parse_WorldGenesisProperties, 0},
	{"GENESIS_DESIGN",					GDPTT_Deprecated,	NULL,										NULL, 0},
	{"GenesisChallengeType",			GDPTT_Deprecated,	NULL,										NULL, 0},

	{"LightAffects",					GDPTT_LAEnum,		".LightProperties.LightAffects",						parse_WorldLightProperties, 1},
	{"LightType",						GDPTT_LTEnum,		".LightProperties.LightType",						parse_WorldLightProperties, 1},
	{"LightProjectedTexture",			GDPTT_String,		".LightProperties.LightProjectedTexture",			parse_WorldLightProperties, 1},

	{"LightCloudTexture",				GDPTT_Pooled,		".LightProperties.LightCloudTexture",			parse_WorldLightProperties, 1},
	{"LightCloudMultiplier1",			GDPTT_Float,		".LightProperties.LightCloudMultiplier1",					parse_WorldLightProperties, 1},
	{"LightCloudScale1",				GDPTT_Float,		".LightProperties.LightCloudScale1",					parse_WorldLightProperties, 1},
	{"LightCloudScrollX1",				GDPTT_Float,		".LightProperties.LightCloudScrollX1",					parse_WorldLightProperties, 1},
	{"LightCloudScrollY1",				GDPTT_Float,		".LightProperties.LightCloudScrollY1",					parse_WorldLightProperties, 1},
	{"LightCloudMultiplier2",			GDPTT_Float,		".LightProperties.LightCloudMultiplier2",					parse_WorldLightProperties, 1},
	{"LightCloudScale2",				GDPTT_Float,		".LightProperties.LightCloudScale2",					parse_WorldLightProperties, 1},
	{"LightCloudScrollX2",				GDPTT_Float,		".LightProperties.LightCloudScrollX2",					parse_WorldLightProperties, 1},
	{"LightCloudScrollY2",				GDPTT_Float,		".LightProperties.LightCloudScrollY2",					parse_WorldLightProperties, 1},
	{"LightAmbientHSV",					GDPTT_Vec3,			".LightProperties.LightAmbientHSV",					parse_WorldLightProperties, 1},
	{"LightAmbientMultiplier",			GDPTT_Vec3,			".LightProperties.LightAmbientMultiplier",			parse_WorldLightProperties, 1},
	{"LightAmbientOffset",				GDPTT_Vec3,			".LightProperties.LightAmbientOffset",				parse_WorldLightProperties, 1},
	{"LightDiffuseHSV",					GDPTT_Vec3,			".LightProperties.LightDiffuseHSV",					parse_WorldLightProperties, 1},
	{"LightDiffuseMultiplier",			GDPTT_Vec3,			".LightProperties.LightDiffuseMultiplier",			parse_WorldLightProperties, 1},
	{"LightDiffuseOffset",				GDPTT_Vec3,			".LightProperties.LightDiffuseOffset",				parse_WorldLightProperties, 1},
	{"LightSecondaryDiffuseHSV",		GDPTT_Vec3,			".LightProperties.LightSecondaryDiffuseHSV",			parse_WorldLightProperties, 1},
	{"LightSecondaryDiffuseMultiplier",	GDPTT_Vec3,			".LightProperties.LightSecondaryDiffuseMultiplier",	parse_WorldLightProperties, 1},
	{"LightSecondaryDiffuseOffset",		GDPTT_Vec3,			".LightProperties.LightSecondaryDiffuseOffset",		parse_WorldLightProperties, 1},
	{"LightSpecularHSV",				GDPTT_Vec3,			".LightProperties.LightSpecularHSV",					parse_WorldLightProperties, 1},
	{"LightSpecularMultiplier",			GDPTT_Vec3,			".LightProperties.LightSpecularMultiplier",			parse_WorldLightProperties, 1},
	{"LightSpecularOffset",				GDPTT_Vec3,			".LightProperties.LightSpecularOffset",				parse_WorldLightProperties, 1},
	{"LightShadowColorHSV",				GDPTT_Vec3,			".LightProperties.LightShadowColorHSV",					parse_WorldLightProperties, 1},
	{"LightShadowColorMultiplier",		GDPTT_Vec3,			".LightProperties.LightShadowColorMultiplier",			parse_WorldLightProperties, 1},
	{"LightShadowColorOffset",			GDPTT_Vec3,			".LightProperties.LightShadowColorOffset",				parse_WorldLightProperties, 1},
	{"LightConeInner",					GDPTT_Float,		".LightProperties.LightConeInner",					parse_WorldLightProperties, 1},
	{"LightConeOuter",					GDPTT_Float,		".LightProperties.LightConeOuter",					parse_WorldLightProperties, 1},
	{"LightCone2Inner",					GDPTT_Float,		".LightProperties.LightCone2Inner",					parse_WorldLightProperties, 1},
	{"LightCone2Outer",					GDPTT_Float,		".LightProperties.LightCone2Outer",					parse_WorldLightProperties, 1},
	{"LightRadius",						GDPTT_Float,		".LightProperties.LightRadius",						parse_WorldLightProperties, 1},
	{"LightRadiusInner",				GDPTT_Float,		".LightProperties.LightRadiusInner",					parse_WorldLightProperties, 1},
	{"LightShadowNearDist",				GDPTT_Float,		".LightProperties.LightShadowNearDist",				parse_WorldLightProperties, 1},
	{"LightVisualLODScale",				GDPTT_Float,		".LightProperties.LightVisualLODScale",				parse_WorldLightProperties, 1},
	{"LightCastsShadows",				GDPTT_Flag,			".LightProperties.LightCastsShadows",				parse_WorldLightProperties, 1},
	{"LightInfiniteShadows",			GDPTT_Flag,			".LightProperties.LightInfiniteShadows",				parse_WorldLightProperties, 1},
	{"LightIsKey",						GDPTT_Flag,			".LightProperties.LightIsKey",						parse_WorldLightProperties, 1},
	{"LightIsSun",						GDPTT_Flag,			".LightProperties.LightIsSun",						parse_WorldLightProperties, 1},

	{"RoomType",						GDPTT_RoomEnum,		".RoomProperties.Type",							parse_WorldRoomProperties, 0},
	{"RoomLimitLights",					GDPTT_Flag,			".RoomProperties.LimitLights",					parse_WorldRoomProperties, 0},
	{"RoomOccluder",					GDPTT_Flag,			".RoomProperties.Occluder",						parse_WorldRoomProperties, 0},
	{"RoomUseModels",					GDPTT_Flag,			".RoomProperties.UseModels",						parse_WorldRoomProperties, 0},

	{"SoundSphere",						GDPTT_Pooled,		".SoundSphere.EventName",						parse_WorldSoundSphereProperties, 0},
	{"SoundDSP",						GDPTT_Pooled,		".SoundSphere.DSPName",							parse_WorldSoundSphereProperties, 0},
	{"SoundMultiplier",					GDPTT_Float,		".SoundSphere.Multiplier",						parse_WorldSoundSphereProperties, 0},
	{"SoundPriority",					GDPTT_Float,		".SoundSphere.Priority",							parse_WorldSoundSphereProperties, 0},
	{"SoundGroup",						GDPTT_String,		".SoundSphere.SoundGroup",						parse_WorldSoundSphereProperties, 0},
	{"SoundGroupOrd",					GDPTT_Int,			".SoundSphere.SoundGroupOrd",					parse_WorldSoundSphereProperties, 0},
	{"SoundExclude",					GDPTT_Flag,			".SoundSphere.Exclude",							parse_WorldSoundSphereProperties, 0},
	{"max_range1",						GDPTT_Deprecated,	NULL,											NULL, 0},
	{"max_range2",						GDPTT_Deprecated,	NULL,											NULL, 0},
	{"min_range1",						GDPTT_Deprecated,	NULL,											NULL, 0},
	{"min_range2",						GDPTT_Deprecated,	NULL,											NULL, 0},

	{"VolumeSize",						GDPTT_BoxVol,		".Volume.BoxSize",								parse_GroupVolumeProperties, 0},
	{"VolumeRadius",					GDPTT_SphrVol,		".Volume.SphereRadius",							parse_GroupVolumeProperties, 0},
	{"VolumeType",						GDPTT_VolType,		".HullProperties.HullType",						parse_GroupHullProperties, 0},
	{"SubVolume",						GDPTT_Flag,			".Volume.SubVolume",							parse_GroupVolumeProperties, 0},

	{"SkyFade2",						GDPTT_Deprecated,	NULL,											NULL, 0},
	{"WaterWet",						GDPTT_Deprecated,	NULL,											NULL, 0},
	{"SoundMaxRadius",					GDPTT_Deprecated,	NULL,											NULL, 0},

};

static U32 fixupGetOccluderFaces(const char *prop)
{
	U32 retVal;
	char *s;
	if (prop)
	{
		retVal = 0;

		while (prop[0] == ' ')
			++prop;

		while (prop[0])
		{
			s = strchr(prop, ' ');
			if (s)
				*s = 0;
			if (stricmp(prop, "posx")==0)
				retVal |= VOLFACE_POSX;
			else if (stricmp(prop, "negx")==0)
				retVal |= VOLFACE_NEGX;
			else if (stricmp(prop, "posy")==0)
				retVal |= VOLFACE_POSY;
			else if (stricmp(prop, "negy")==0)
				retVal |= VOLFACE_NEGY;
			else if (stricmp(prop, "posz")==0)
				retVal |= VOLFACE_POSZ;
			else if (stricmp(prop, "negz")==0)
				retVal |= VOLFACE_NEGZ;
			if (s)
				*s = ' ';
			else
				break;
			prop = s;
			while (prop[0] == ' ')
				++prop;
		}
	}
	else
	{
		retVal = VOLFACE_ALL;
	}
	return retVal;
}

AUTO_FIXUPFUNC;
TextParserResult fixupWorldLightProperties(WorldLightProperties* props, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_TEMPLATE_CONSTRUCTOR: {
			setVec3same(props->vAmbientMultiplier, 1);
			setVec3same(props->vDiffuseMultiplier, 1);
			setVec3same(props->vSecondaryDiffuseMultiplier, 1);
			setVec3same(props->vShadowColorMultiplier, 1);
			setVec3same(props->vSpecularMultiplier, 1);
			setVec3(props->vDiffuseHSV, 0, 0, 1);
			setVec3(props->vSpecularHSV, 0, 0, 1);
		}
	}

	return 1;
}

static GroupDefPropConvertEntry* groupDefFindConvertEntry(const char *propName)
{
	int i;
	static StashTable pStash = NULL;
	StashElement pElement = NULL;

	if(!pStash) {
		pStash = stashTableCreate(GroupDefPropConvertEntryCnt, StashDefault, StashKeyTypeStrings, sizeof(void*));
		for ( i=0; i < GroupDefPropConvertEntryCnt; i++ ) {
			if(!g_GroupDefPropConvertTable[i].pcPropName)
				break;
			stashAddPointer(pStash, g_GroupDefPropConvertTable[i].pcPropName, &g_GroupDefPropConvertTable[i], false);
		}
	}

	assertmsgf(stashFindElement(pStash, propName, &pElement), "%s is not in the group def property conversion table", propName);
	return stashElementGetPointer(pElement);
}


static const char *groupDefFindPropertyEx(GroupProperties *property_structs, const char *propName)
{
	int i;
	GroupDefPropConvertEntry *pEntry;
	int intVal = 0;
	F32 floatVal = 0;
	Vec3 vecVal;
	const char *stringVal;
	char returnString[512];
	char newPath[1024];

	if (!property_structs || !propName)
		return NULL;

	returnString[0] = 0;

	propName = allocAddString(propName);

	pEntry = groupDefFindConvertEntry(propName);
	if(!pEntry)
		return NULL;

	if(pEntry->bIsLight) {
		if(property_structs->light_properties) {
			if(eaFind(&property_structs->light_properties->ppcSetFields, propName) == -1)
				return NULL;
		} else {
			return NULL;
		}
	}

	switch(pEntry->eType) 
	{
	case GDPTT_Flag:
		if(!objPathGetBit(pEntry->pcPropPath, parse_GroupProperties, property_structs, &intVal))
			return NULL;
		sprintf(returnString, "%d", intVal);
		break;
	case GDPTT_Int:		
		if(!objPathGetInt(pEntry->pcPropPath, parse_GroupProperties, property_structs, &intVal))
			return NULL;
		sprintf(returnString, "%d", intVal);
		break;
	case GDPTT_Float:
		if(!objPathGetF32(pEntry->pcPropPath, parse_GroupProperties, property_structs, &floatVal))
			return NULL;
		sprintf(returnString, "%f", floatVal);
		break;
	case GDPTT_String:
	case GDPTT_Pooled:
		if(!objPathGetString(pEntry->pcPropPath, parse_GroupProperties, property_structs, returnString, 512))
			return NULL;
		if(!returnString || !returnString[0])
			return NULL;
		break;
	case GDPTT_Vec3:
		sprintf(newPath, "%s[0]", pEntry->pcPropPath);
		if(!objPathGetF32(newPath, parse_GroupProperties, property_structs, &floatVal))
			return NULL;
		vecVal[0] = floatVal;
		sprintf(newPath, "%s[1]", pEntry->pcPropPath);
		assert(objPathGetF32(newPath, parse_GroupProperties, property_structs, &floatVal));
		vecVal[1] = floatVal;
		sprintf(newPath, "%s[2]", pEntry->pcPropPath);
		assert(objPathGetF32(newPath, parse_GroupProperties, property_structs, &floatVal));
		vecVal[2] = floatVal;
		sprintf(returnString, "%f %f %f", vecVal[0], vecVal[1], vecVal[2]);
		break;
	case GDPTT_OccFace:
		if(property_structs->physical_properties.iOccluderFaces == VOLFACE_ALL)
			return NULL;
		if(property_structs->physical_properties.iOccluderFaces & VOLFACE_POSX)
			strcat(returnString, (returnString[0] ? " posx" : "posx"));
		if(property_structs->physical_properties.iOccluderFaces & VOLFACE_NEGX)
			strcat(returnString, (returnString[0] ? " negx" : "negx"));
		if(property_structs->physical_properties.iOccluderFaces & VOLFACE_POSY)
			strcat(returnString, (returnString[0] ? " posy" : "posy"));
		if(property_structs->physical_properties.iOccluderFaces & VOLFACE_NEGY)
			strcat(returnString, (returnString[0] ? " negy" : "negy"));
		if(property_structs->physical_properties.iOccluderFaces & VOLFACE_POSZ)
			strcat(returnString, (returnString[0] ? " posz" : "posz"));
		if(property_structs->physical_properties.iOccluderFaces & VOLFACE_NEGZ)
			strcat(returnString, (returnString[0] ? " negz" : "negz"));
		break;
	case GDPTT_CarryEnum:
		if(property_structs->physical_properties.eCarryAnimationBit == WorldCarryAnimationMode_BoxCarryMode)
			sprintf(returnString, "BOXCARRYMODE");
		else if(property_structs->physical_properties.eCarryAnimationBit == WorldCarryAnimationMode_OverheadMode)
			sprintf(returnString, "OVERHEADMODE");
		else
			return NULL;
		break;
	case GDPTT_LAEnum:
		if(!property_structs->light_properties)
			return NULL;
		switch (property_structs->light_properties->eAffectType) {
		case WL_LIGHTAFFECT_ALL:
			stringVal = "All Objects";
			break;
		case WL_LIGHTAFFECT_STATIC:
			stringVal = "Static Objects";
			break;
		case WL_LIGHTAFFECT_DYNAMIC:
			stringVal = "Dynamic Objects";
			break;
		default:
			assertmsg(false, "Trying to return unknown light affect type in groupDefFindPropertyEx");
		}
		if(!stringVal)
			return NULL;
		strcpy(returnString, stringVal);
		break;
	case GDPTT_LTEnum:
		if(!property_structs->light_properties)
			return NULL;
		stringVal = StaticDefineIntRevLookup(WleAELightTypeEnum, (int)property_structs->light_properties->eLightType);
		if(!stringVal)
			return NULL;
		strcpy(returnString, stringVal);
		break;
	case GDPTT_RoomEnum:
		if(!property_structs->room_properties)
			return NULL;
		stringVal = StaticDefineIntRevLookup(WorldRoomTypeEnum, (int)property_structs->room_properties->eRoomType);
		if(!stringVal)
			return NULL;
		strcpy(returnString, stringVal);
		break;
	case GDPTT_ChildSelect:
		if(!property_structs->physical_properties.bIsChildSelect)
			return NULL;
		sprintf(returnString, "%d", property_structs->physical_properties.iChildSelectIdx);
		break;
	case GDPTT_BoxVol:
		if(!property_structs->volume || property_structs->volume->eShape != GVS_Box)
			return NULL;
		sprintf(returnString, "%f %f %f %f %f %f", 
			property_structs->volume->vBoxMin[0],
			property_structs->volume->vBoxMin[1],
			property_structs->volume->vBoxMin[2],
			property_structs->volume->vBoxMax[0],
			property_structs->volume->vBoxMax[1],
			property_structs->volume->vBoxMax[2]);
		break;
	case GDPTT_SphrVol:
		if(!property_structs->volume || property_structs->volume->eShape != GVS_Sphere)
			return NULL;
		sprintf(returnString, "%f", property_structs->volume->fSphereRadius);
		break;
	case GDPTT_VolType:
		if(!property_structs->hull || eaSize(&property_structs->hull->ppcTypes)==0)
			return NULL;
		for ( i=0; i < eaSize(&property_structs->hull->ppcTypes); i++ ) {
			if(i!=0)
				strcat(returnString, " ");
			strcat(returnString, property_structs->hull->ppcTypes[i]);
		}
		break;
	case GDPTT_Deprecated:
		assertmsg(false, "Asking for a depricated value in groupDefFindProperty");
		break;
	default:
		assertmsg(false, "Unknown type in groupDefFindProperty");
	}
	
	return allocAddString(returnString);
}

const char *groupDefFindProperty(GroupDef *def, const char *propName)
{
	if(!def)
		return NULL;
	return groupDefFindPropertyEx(&def->property_structs, propName);
}

int groupHasPropertySet(GroupDef *def, const char *propName)
{
	const char *s = groupDefFindProperty(def, propName);
	return s && stricmp(s, "1")==0;
}

void groupDefAddPropertyEx(GroupDef* def, const char* propName, const char* propValue, bool ignore_deprecated)
{
	GroupProperties *property_structs;
	GroupDefPropConvertEntry *pEntry;
	char *oldValue = NULL;
	char structPath[512];
	char *dotptr;
	int intVal;
	F32 floatVal;
	Vec3 vecVals[2];
	char newPath[1024];
	char tempStr[2048];
	char *strPtr;
	char *prevStr = NULL;

	if (!def || !propName)
		return;

	property_structs = &def->property_structs;

	pEntry = groupDefFindConvertEntry(propName);
	if(!pEntry)
		return;

	propName = allocAddString(propName);

	if(pEntry->parentPti)
	{
		int column;
		void *ptrVal = NULL;
		strcpy(structPath, pEntry->pcPropPath+1);
		dotptr = strchr(structPath, '.');
		assert(dotptr);
		dotptr[0] = '\0';
		assert(ParserFindColumn(parse_GroupProperties, structPath, &column));
		ptrVal = TokenStoreGetPointer(parse_GroupProperties, column, &def->property_structs, 0, NULL);
		if(!ptrVal) {
			ptrVal = StructCreateVoid(pEntry->parentPti);
			TokenStoreSetPointer(parse_GroupProperties, column, &def->property_structs, 0, ptrVal, NULL);
		}
	}

	switch(pEntry->eType) 
	{
	case GDPTT_Flag:
		intVal = atoi(propValue);
		assert(objPathSetBit(pEntry->pcPropPath, parse_GroupProperties, property_structs, !!intVal, true));
		break;
	case GDPTT_Int:
		intVal = atoi(propValue);
		assert(objPathSetInt(pEntry->pcPropPath, parse_GroupProperties, property_structs, intVal, true));
		break;
	case GDPTT_Float:
		floatVal = atof(propValue);
		assert(objPathSetF32(pEntry->pcPropPath, parse_GroupProperties, property_structs, floatVal, true));
		break;
	case GDPTT_String:
	case GDPTT_Pooled:
		assert(objPathSetString(pEntry->pcPropPath, parse_GroupProperties, property_structs, propValue));
		break;
	case GDPTT_Vec3:
		fillVec3sFromStr(propValue, vecVals, 1);
		sprintf(newPath, "%s[0]", pEntry->pcPropPath);
		assert(objPathSetF32(newPath, parse_GroupProperties, property_structs, vecVals[0][0], true));
		sprintf(newPath, "%s[1]", pEntry->pcPropPath);
		assert(objPathSetF32(newPath, parse_GroupProperties, property_structs, vecVals[0][1], true));
		sprintf(newPath, "%s[2]", pEntry->pcPropPath);
		assert(objPathSetF32(newPath, parse_GroupProperties, property_structs, vecVals[0][2], true));
		break;
	case GDPTT_OccFace:
		property_structs->physical_properties.iOccluderFaces = fixupGetOccluderFaces(propValue);
		break;
	case GDPTT_CarryEnum:
		property_structs->physical_properties.eCarryAnimationBit = StaticDefineIntGetInt(WorldCarryAnimationModeEnum, propValue);
		break;
	case GDPTT_LAEnum:
		if(stricmp(propValue, "All Objects")==0) {
			property_structs->light_properties->eAffectType = WL_LIGHTAFFECT_ALL;
		} else if (stricmp(propValue, "Static Objects")==0) {
			property_structs->light_properties->eAffectType = WL_LIGHTAFFECT_STATIC;
		} else if (stricmp(propValue, "Dynamic Objects")==0) {
			property_structs->light_properties->eAffectType = WL_LIGHTAFFECT_DYNAMIC;
		} else {
			assertmsg(false, "Trying to add unknown light affect type in groupDefAddPropertyEx");
		}
		break;
	case GDPTT_LTEnum:
		property_structs->light_properties->eLightType = StaticDefineIntGetInt(WleAELightTypeEnum, propValue);
		break;
	case GDPTT_ChildSelect:
		intVal = atoi(propValue);
		property_structs->physical_properties.bIsChildSelect = 1;
		property_structs->physical_properties.iChildSelectIdx = intVal;
		break;
	case GDPTT_RoomEnum:
		property_structs->room_properties->eRoomType = StaticDefineIntGetInt(WorldRoomTypeEnum, propValue);
		break;
	case GDPTT_BoxVol:
		property_structs->volume->eShape = GVS_Box;
		fillVec3sFromStr(propValue, vecVals, 2);
		copyVec3(vecVals[0], property_structs->volume->vBoxMin);
		copyVec3(vecVals[1], property_structs->volume->vBoxMax);
		break;
	case GDPTT_SphrVol:
		floatVal = atof(propValue);
		property_structs->volume->eShape = GVS_Sphere;
		property_structs->volume->fSphereRadius = floatVal;
		break;
	case GDPTT_VolType:
		eaClear(&property_structs->hull->ppcTypes);
		strcpy(tempStr, propValue);
		strPtr = strtok_s(tempStr, " ", &prevStr);
		while(strPtr && strPtr[0]) {
			groupDefAddVolumeType(def, strPtr);
			strPtr = strtok_s(NULL, " ", &prevStr);
		}
		break;
	case GDPTT_Deprecated:
		assertmsg(ignore_deprecated, "Asking for a depricated value in groupDefAddProperty");
		break;
	default:
		assertmsg(false, "Unknown type in groupDefAddProperty");
	}

	if(pEntry->bIsLight) {
		assert(property_structs->light_properties);
		eaPushUnique(&property_structs->light_properties->ppcSetFields, propName);
	}

	groupInvalidateBounds(def);
}

void groupDefAddProperty(GroupDef* def, const char* propName, const char* propValue)
{
	groupDefAddPropertyEx(def, propName, propValue, false);
}

void groupDefRemoveProperty(GroupDef *def, const char *propName)
{
	char *oldValue = NULL;
	GroupDefPropConvertEntry *pEntry;
	static GroupProperties *g_DefaultProps = NULL;
	char structPath[512];

	if (!def || !propName)
		return;

	pEntry = groupDefFindConvertEntry(propName);
	if(!pEntry)
		return;

	propName = allocAddString(propName);

	if(pEntry->eType == GDPTT_ChildSelect) {
		def->property_structs.physical_properties.bIsChildSelect = 0;
		def->property_structs.physical_properties.iChildSelectIdx = 0;
	} else if(pEntry->parentPti) {
		int column;
		void *ptrVal = NULL;
		void *tempStruct;
		char *dotptr;
		strcpy(structPath, pEntry->pcPropPath+1);
		dotptr = strchr(structPath, '.');
		assert(dotptr);
		dotptr[0] = '\0';
		assert(ParserFindColumn(parse_GroupProperties, structPath, &column));
		ptrVal = TokenStoreGetPointer(parse_GroupProperties, column, &def->property_structs, 0, NULL);
		if(!ptrVal)
			return;
		tempStruct = StructCreateVoid(pEntry->parentPti);
		dotptr[0] = '.';
		StructCopyFieldVoid(pEntry->parentPti, tempStruct, ptrVal, dotptr, 0, 0, 0);
		StructDestroyVoid(pEntry->parentPti, tempStruct);
	} else {
		if(!g_DefaultProps)
			g_DefaultProps = StructCreate(parse_GroupProperties);
		groupDefAddProperty(def, propName, groupDefFindPropertyEx(g_DefaultProps, propName));
	}

	if(pEntry->bIsLight) {
		assert(def->property_structs.light_properties);
		eaFindAndRemove(&def->property_structs.light_properties->ppcSetFields, propName);
	}

	groupInvalidateBounds(def);
}

void groupDefAddVolumeType(GroupDef *def, const char *volType)
{
	if(!def)
		return;
	if(!def->property_structs.hull)
		def->property_structs.hull = StructCreate(parse_GroupHullProperties);
	eaPushUnique(&def->property_structs.hull->ppcTypes, allocAddString(volType));
}

void groupDefRemoveVolumeType(GroupDef *def, const char *volType)
{
	if(!def || !def->property_structs.hull)
		return;
	eaFindAndRemove(&def->property_structs.hull->ppcTypes, allocAddString(volType));
}

bool groupDefIsVolumeType(GroupDef *def, const char *volType)
{
	if(!def || !def->property_structs.hull)
		return false;
	return (eaFind(&def->property_structs.hull->ppcTypes, allocAddString(volType)) != -1);
}
