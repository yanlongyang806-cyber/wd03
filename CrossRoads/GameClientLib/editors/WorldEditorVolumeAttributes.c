#ifndef NO_EDITORS

#include "WorldEditorVolumeAttributes.h"

#include "WorldGrid.h"
#include "EditorObject.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "WorldEditorUtil.h"
#include "WorldEditorUI.h"
#include "groupdbmodify.h"
#include "gclCivilian.h"
#include "wlVolumes.h"
#include "wlEncounter.h"
#include "RoomConn.h"
#include "Expression.h"
#include "StringCache.h"
#include "ChoiceTable_common.h"
#include "wlEditorIncludes.h"
#include "WorldEditorOptions.h"
#include "contact_common.h"
#include "mission_common.h"
#include "UIFXButton.h"
#include "WorldCellClustering.h"
#include "GenericMesh.h"

#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorOperations.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
#define MAX_OPTIONAL_ACTIONS 9
#define MAX_VARIABLES 9
#define MAX_CRITTER_SETS 8

#define WLE_AE_VOLUME_ALIGN_WIDTH 75
#define WLE_AE_VOLUME_ALIGN_WIDE 100
#define WLE_AE_VOLUME_TEXT_ENTRY_WIDTH 130
#define WLE_AE_VOLUME_NUM_ENTRY_WIDTH 100
#define WLE_AE_VOLUME_STRENGTH_ENTRY_WIDTH 120
#define WLE_AE_VOLUME_INDENT 20

#define wleAEVolumeSetupParam(param, propertyName)\
	param.left_pad = 20;\
	param.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;\
	param.property_name = propertyName

#define wleAEVolumeSetupParamSimple(param)\
	param.left_pad = 20;\
	param.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;

#define wleAEVolumeSetupStructParam(param, defStructMember, pti, fieldName)\
	param.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;\
	param.left_pad = WLE_AE_VOLUME_INDENT;\
	param.struct_offset = offsetof(GroupDef, property_structs.defStructMember);\
	param.struct_pti = pti;\
	param.struct_fieldname = fieldName

#define wleAEVolumeUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	tracker = (obj->type->objType == EDTYPE_TRACKER) ? trackerFromTrackerHandle(obj->obj) : NULL;\
	def = tracker ? tracker->def : NULL

#define wleAEVolumeApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(obj->obj);\
	if (!tracker)\
		return;\
	def = tracker ? tracker->def : NULL;\
	if (!def)\
	{\
		wleOpPropsEnd();\
		return;\
	}\

#define wleAEVolumeApplyInitAt(i)\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(objs[i]->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(objs[i]->obj);\
	if (!tracker)\
		continue;\
	def = tracker ? tracker->def : NULL;\
	if (!def)\
	{\
		wleOpPropsEndNoUIUpdate();\
		continue;\
	}\

#define wleAEVolumeUISkinAxes(param) \
	if (wleAEGlobalVolumeUI.autoWidget->root->children)\
	{\
		for (i = 0; i < 3; i++)\
		{\
			UISkin *skin = NULL;\
			switch (i)\
			{\
				xcase 0:\
					if (!param.diff[2])\
						skin = editorUIState->skinBlue;\
				xcase 1:\
					if (!param.diff[1])\
						skin = editorUIState->skinGreen;\
				xcase 2:\
					if (!param.diff[0])\
						skin = editorUIState->skinRed;\
			}\
			if (skin)\
				ui_WidgetSkin(wleAEGlobalVolumeUI.autoWidget->root->children[eaSize(&wleAEGlobalVolumeUI.autoWidget->root->children) - 1 - i]->widget1, skin);\
		}\
	}

typedef struct WleAEVolumeWarpVarPropUI
{
	WleAEParamWorldVariableDef var;
} WleAEVolumeWarpVarPropUI;

typedef struct WleAEVolumeCivilianCritter
{
	WleAEParamText	critterName;
	WleAEParamFloat critterChance;
	WleAEParamBool	isCar;
	WleAEParamBool	restrictedToVolume;
} WleAEVolumeCivilianCritter;


typedef struct WleAEVolumeUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;

	struct
	{
		// global
		WleAEParamCombo volumeStyle;
		WleAEParamCombo volumeShape;
		WleAEParamFloat volumeRadius;
		WleAEParamVec3 volumeMin;
		WleAEParamVec3 volumeMax;

		// rooms
		WleAEParamCombo roomType;
		WleAEParamBool roomOccluder;
		WleAEParamBool roomUseModels;
		WleAEParamBool roomDisablePhoto;
		WleAEParamBool roomOverridePhoto;
		WleAEParamTexture roomOverridePhotoTexture;
		WleAEParamBool roomLimitLights;

		// sky fade
		WleAEParamBool isSkyFade;
		WleAEParamBool skyFadeIsPositional;
		WleAEParamCombo **skyNames;
		WleAEParamFloat skyPercent;
		WleAEParamFloat skyFadeInRate;
		WleAEParamFloat skyFadeOutRate;

		// occlusion
		WleAEParamBool isOccluder;
		WleAEParamBool occluderPosX;
		WleAEParamBool occluderNegX;
		WleAEParamBool occluderPosY;
		WleAEParamBool occluderNegY;
		WleAEParamBool occluderPosZ;
		WleAEParamBool occluderNegZ;

		// action
		WleAEParamBool isAction;
		WleAEParamExpression enteredCondition;
		WleAEParamExpression enteredAction;
		WleAEParamExpression exitedCondition;
		WleAEParamExpression exitedAction;

		// powers
		WleAEParamBool isPower;
		WleAEParamDictionary powerDef;
		WleAEParamCombo powerStrength;
		WleAEParamInt powerLevel;
		WleAEParamFloat powerTime;
		WleAEParamExpression powerCond;

		// warp
		WleAEParamBool isWarp;
		WleAEParamWorldVariableDef warpDest;
		WleAEParamExpression warpCond;
		WleAEParamBool warpSequenceOverride;
		WleAEParamDictionary warpTransition;
		WleAEParamBool warpHasVariables;
		WleAEVolumeWarpVarPropUI **warpVariables;

		// landmark
		WleAEParamBool isLandmark;
		WleAEParamTexture landmarkIcon;
		WleAEParamMessage landmarkDispName;
		WleAEParamBool landmarkHideUnlessRevealed;

		// neighborhood
		WleAEParamBool isNeighborhood;
		WleAEParamMessage neighborhoodDispName;
		WleAEParamText neighborhoodSound;

		//Map Level Override
		WleAEParamBool isLevelOverride;
		WleAEParamInt levelOverride;

		// AI
		WleAEParamBool aiAvoid;

		// Event
		WleAEParamBool isEvent;
		WleAEParamGameAction eventFirstEnteredAction;
		WleAEParamExpression eventEnteredCondition;
		WleAEParamExpression eventExitedCondition;

		// water
		WleAEParamBool isWater;
		WleAEParamCombo waterDef;
		WleAEParamCombo waterCond;

		// playable
		WleAEParamBool playable;
		
		// nodynconn
		WleAEParamBool nodynconn;

		// Dueling
		WleAEParamBool duelDisable;
		WleAEParamBool duelEnable;

		WleAEParamBool petsDisabled;

		// sounds
		WleAEParamBool ignoreSound;

		// indoor
		WleAEParamBool isIndoor;
		WleAEParamHSV indoorAmbient;
		WleAEParamFloat indoorLightRange;
		WleAEParamBool indoorSeeOutdoors;

		// FX
		WleAEParamBool isFX;
		WleAEParamCombo fxFilter;
		UIFXButton *pFXButtonEntrance;
		WleAEParamDictionary fxEntrance;
		WleAEParamHue fxEntranceHue;
		UIFXButton *pFXButtonExit;
		WleAEParamDictionary fxExit;
		WleAEParamHue fxExitHue;
		UIFXButton *pFXButtonMaintained;
		WleAEParamDictionary fxMaintained;
		WleAEParamHue fxMaintainedHue;

		// Civilian
		WleAEParamBool isCivilian;
		WleAEParamBool civDisablesSidewalk;
		WleAEParamBool civDisablesRoad;

		WleAEParamBool civForcedRoad;
		WleAEParamBool civForcedRoadHasMedian;
		WleAEParamBool civForcedIntersection;
		WleAEParamBool civForcedSidewalk;
		WleAEParamBool civForcedCrosswalk;
		WleAEParamBool civForcedAsIs;
		WleAEParamBool civPedestrianWanderArea;

		WleAEParamCombo	civLegDef;
		
		
		WleAEVolumeCivilianCritter	**civCritters;

		// Debris Field exclusion
		WleAEParamBool isDebrisFieldExcluder;

		// Terrain exclusion
		WleAEParamBool isExcluder;
		WleAEParamCombo exclusionType;
		WleAEParamCombo exclusionCollisionType;
		WleAEParamCombo exclusionPlatformType;
		WleAEParamBool exclusionChallengesOnly;

		// UGC interior footprint
		WleAEParamBool isUGCRoomFootprint;

		// Mastermind properties
		WleAEParamBool hasMastermindProperties;
		WleAEParamBool mastermindRoomIsSafe;

		// Simplygon Clustering
		WleAEParamBool isCluster;
		WleAEParamCombo targetLOD;
		WleAEParamCombo minLevel;
		WleAEParamCombo maxLODLevel;
		WleAEParamCombo textureHeight;
		WleAEParamCombo textureWidth;
		WleAEParamCombo textureSupersample;
		WleAEParamCombo geometryResolution;
		WleAEParamBool includeNormal;
		WleAEParamBool includeSpecular;
		EditorObject *previouslyUpdated;
		
	} data;

	//for Water Volumes
	char **triggerConditionNames;
} WleAEVolumeUI;

StaticDefineInt WorldTerrainCollisionTypeUIEnum[] =
{
	DEFINE_INT
	{"Full Collision",	WorldTerrainCollisionType_Collide_All},
	{"Allow on Paths",	WorldTerrainCollisionType_Collide_All_Except_Paths},
	{"No Collision",	WorldTerrainCollisionType_Collide_None},
	{"Lights",			WorldTerrainCollisionType_Collide_Lights},
	{"Encounters",		WorldTerrainCollisionType_Collide_Encounters},
	{"Detail_1",		WorldTerrainCollisionType_Collide_Detail_1},
	{"Detail_2",		WorldTerrainCollisionType_Collide_Detail_2},
	{"Detail_3",		WorldTerrainCollisionType_Collide_Detail_3},
	DEFINE_END
};

/********************
* GLOBALS
********************/
static WleAEVolumeUI wleAEGlobalVolumeUI;

/********************
* UTIL
********************/
static int wleAEVolumeCompareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


/********************
* PARAMETER CALLBACKS
********************/
static void wleAEVolumeVolumeStyleUpdate(void *param_unused, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	wleAEGlobalVolumeUI.data.volumeShape.stringvalue = "Box";

	if (!def)
	{
		wleAEGlobalVolumeUI.data.volumeStyle.stringvalue = "No";
	}
	else if (def->property_structs.volume && !def->property_structs.volume->bSubVolume)
	{
		wleAEGlobalVolumeUI.data.volumeStyle.stringvalue = "Yes";
		if (def->property_structs.volume->eShape == GVS_Sphere)
			wleAEGlobalVolumeUI.data.volumeShape.stringvalue = "Sphere";
	}
	else if (def->property_structs.volume && def->property_structs.volume->bSubVolume)
	{
		wleAEGlobalVolumeUI.data.volumeStyle.stringvalue = "SubVolume";
		if (def->property_structs.volume->eShape == GVS_Sphere)
			wleAEGlobalVolumeUI.data.volumeShape.stringvalue = "Sphere";
	}
	else
	{
		wleAEGlobalVolumeUI.data.volumeStyle.stringvalue = "No";
	}
}

static void wleAEInitializeVolume(GroupDef *def)
{
	if(!def->property_structs.volume)
		def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);

	if (stricmp(wleAEGlobalVolumeUI.data.volumeShape.stringvalue, "Box")==0)
	{
		// add volume size (defaulted to bounds)
		def->property_structs.volume->eShape = GVS_Box;
		copyVec3(def->bounds.min, def->property_structs.volume->vBoxMin);
		copyVec3(def->bounds.max, def->property_structs.volume->vBoxMax);
	}
	else
	{
		def->property_structs.volume->eShape = GVS_Sphere;
		def->property_structs.volume->fSphereRadius = MAX(def->bounds.radius, 0.001f);
	}

	if (stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "SubVolume") == 0)
		groupDefAddProperty(def, "SubVolume", "1");
	else
		groupDefRemoveProperty(def, "SubVolume");
	groupDefRemoveProperty(def, "RoomUseModels");
}

static void wleAEVolumeVolumeStyleApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "Yes") != 0)
		{
			// remove all volume-related properties
			if(def->property_structs.hull)
			{
				eaDestroy(&def->property_structs.hull->ppcTypes);
			}
			groupDefRemoveProperty(def, "OccluderFaces");

			StructDestroySafe(parse_WorldSoundConnProperties, &def->property_structs.sound_conn_properties);
			StructDestroySafe(parse_WorldAIVolumeProperties, &def->property_structs.server_volume.ai_volume_properties);
			StructDestroySafe(parse_WorldBeaconVolumeProperties, &def->property_structs.server_volume.beacon_volume_properties);
			StructDestroySafe(parse_WorldLandmarkVolumeProperties, &def->property_structs.server_volume.landmark_volume_properties);
			StructDestroySafe(parse_WorldNeighborhoodVolumeProperties, &def->property_structs.server_volume.neighborhood_volume_properties);
			StructDestroySafe(parse_WorldOptionalActionVolumeProperties, &def->property_structs.server_volume.obsolete_optionalaction_properties);
			StructDestroySafe(parse_WorldWarpVolumeProperties, &def->property_structs.server_volume.warp_volume_properties);
			StructDestroySafe(parse_WorldCivilianVolumeProperties, &def->property_structs.server_volume.civilian_volume_properties);

			// convert interaction properties to normal, non-volume interaction properties
			if (def->property_structs.server_volume.interaction_volume_properties)
			{
				if (!def->property_structs.interaction_properties)
				{
					def->property_structs.interaction_properties = def->property_structs.server_volume.interaction_volume_properties;
					def->property_structs.server_volume.interaction_volume_properties = NULL;
				}
				else
				{
					StructDestroySafe(parse_WorldInteractionProperties, &def->property_structs.server_volume.interaction_volume_properties);
				}
			}

			if (stricmp(wleAEGlobalVolumeUI.data.roomType.stringvalue, "None") == 0)
			{
				StructDestroySafe(parse_WorldRoomProperties, &def->property_structs.room_properties);
			}

			if (stricmp(wleAEGlobalVolumeUI.data.roomType.stringvalue, "Room") != 0)
			{
				if (def->property_structs.client_volume.sky_volume_properties)
				{
					gfxSkyNotifySkyGroupFreed(&def->property_structs.client_volume.sky_volume_properties->sky_group);
					StructDestroySafe(parse_WorldSkyVolumeProperties, &def->property_structs.client_volume.sky_volume_properties);
				}

				StructDestroySafe(parse_WorldActionVolumeProperties, &def->property_structs.server_volume.action_volume_properties);
				StructDestroySafe(parse_WorldEventVolumeProperties, &def->property_structs.server_volume.event_volume_properties);
				StructDestroySafe(parse_WorldPowerVolumeProperties, &def->property_structs.server_volume.power_volume_properties);

				StructDestroySafe(parse_WorldWaterVolumeProperties, &def->property_structs.client_volume.water_volume_properties);
				StructDestroySafe(parse_WorldIndoorVolumeProperties, &def->property_structs.client_volume.indoor_volume_properties);
			}
		}
		else
		{
			// convert existing interaction properties to volume interaction properties
			def->property_structs.server_volume.interaction_volume_properties = def->property_structs.interaction_properties;
			def->property_structs.interaction_properties = NULL;
			if (def->property_structs.server_volume.interaction_volume_properties)
			{
				groupDefAddVolumeType(def, "Interaction");
			}
		}

		if (stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "No") != 0)
		{
			wleAEInitializeVolume(def);
		}
		else
		{
			StructDestroySafe(parse_GroupVolumeProperties, &def->property_structs.volume);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeRadiusUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def)
		param->floatvalue = 0;
	else if (def->property_structs.volume)
		param->floatvalue = def->property_structs.volume->fSphereRadius;
	else
		param->floatvalue = def->bounds.radius;
}

static void wleAEVolumeRadiusApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);
		if(!def->property_structs.volume)
		{
			def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
		}
		def->property_structs.volume->eShape = GVS_Sphere;
		def->property_structs.volume->fSphereRadius = MAX(param->floatvalue, 0.001f);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeBoundsUpdate(WleAEParamVec3 *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def) {
		copyVec3(zerovec3, param->vecvalue);
	} else if (def->property_structs.volume) {
		if(param == &wleAEGlobalVolumeUI.data.volumeMin)
			copyVec3(def->property_structs.volume->vBoxMin, param->vecvalue);
		else
			copyVec3(def->property_structs.volume->vBoxMax, param->vecvalue);
	} else {
		copyVec3(param == &wleAEGlobalVolumeUI.data.volumeMin ? def->bounds.min : def->bounds.max, param->vecvalue);
	}
}

static void wleAEVolumeBoundsApply(WleAEParamVec3 *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		Vec3 min = {0,0,0};
		Vec3 max = {0,0,0};
		GroupVolumeProperties *props;
		wleAEVolumeApplyInitAt(i);

		props = def->property_structs.volume;
		if(props)
		{
			copyVec3(props->vBoxMin, min);
			copyVec3(props->vBoxMax, max);
		}
		if (param == &wleAEGlobalVolumeUI.data.volumeMin)
		{
			copyVec3(param->vecvalue, min);
		}
		else
		{
			copyVec3(param->vecvalue, max);
		}
		if(!def->property_structs.volume)
		{
			def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
		}
		def->property_structs.volume->eShape = GVS_Box;
		copyVec3(min, def->property_structs.volume->vBoxMin);
		copyVec3(max, def->property_structs.volume->vBoxMax);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsTypeUpdate(WleAEParamBool *param, const char *typeName, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def)
		param->boolvalue = false;
	else
		param->boolvalue = groupIsVolumeType(def, typeName);
}

static void wleAEVolumeIsSkyFadeApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (param->boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "SkyFade");

			if (def->property_structs.client_volume.sky_volume_properties)
			{
				gfxSkyNotifySkyGroupFreed(&def->property_structs.client_volume.sky_volume_properties->sky_group);
				StructDestroySafe(parse_WorldSkyVolumeProperties, &def->property_structs.client_volume.sky_volume_properties);
				is_new = false;
			}

			def->property_structs.client_volume.sky_volume_properties = StructCreate(parse_WorldSkyVolumeProperties);
			if (is_new)
			{
				def->property_structs.client_volume.sky_volume_properties->weight = 1.f;
				def->property_structs.client_volume.sky_volume_properties->fade_in_rate = 0.25f;
				def->property_structs.client_volume.sky_volume_properties->fade_out_rate = 0.25f;
				def->property_structs.client_volume.sky_volume_properties->positional_fade = false;
			}
			else
			{
				def->property_structs.client_volume.sky_volume_properties->weight = saturate(wleAEGlobalVolumeUI.data.skyPercent.floatvalue / 100.f);
				def->property_structs.client_volume.sky_volume_properties->fade_in_rate = MAX(wleAEGlobalVolumeUI.data.skyFadeInRate.floatvalue / 100.f, 0);
				def->property_structs.client_volume.sky_volume_properties->fade_out_rate = MAX(wleAEGlobalVolumeUI.data.skyFadeOutRate.floatvalue / 100.f, 0);
				def->property_structs.client_volume.sky_volume_properties->positional_fade = wleAEGlobalVolumeUI.data.skyFadeIsPositional.boolvalue;
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "SkyFade");

			if (def->property_structs.client_volume.sky_volume_properties)
			{
				gfxSkyNotifySkyGroupFreed(&def->property_structs.client_volume.sky_volume_properties->sky_group);
				StructDestroySafe(parse_WorldSkyVolumeProperties, &def->property_structs.client_volume.sky_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeSkyFadeIsPositionalUpdate(WleAEParamBool *param, const char *typeName, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->boolvalue = false;
	
	if (def->property_structs.client_volume.sky_volume_properties)
	{
		param->boolvalue = def->property_structs.client_volume.sky_volume_properties->positional_fade;
	}
}

static void wleAEVolumeSkyFadeIsPositionalApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);
		def->property_structs.client_volume.sky_volume_properties->positional_fade = param->boolvalue;
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeSkyNameUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->is_specified = false;
	param->stringvalue = NULL;

	if (def && def->property_structs.client_volume.sky_volume_properties)
	{
		const char *sky_name = NULL;

		if (eaSize(&def->property_structs.client_volume.sky_volume_properties->sky_group.override_list) > param->index)
		{
			sky_name = REF_STRING_FROM_HANDLE(def->property_structs.client_volume.sky_volume_properties->sky_group.override_list[param->index]->sky);
			param->is_specified = true;
		}

		if (sky_name)
		{
			int i;
			for (i = 0; i < eaSize(&param->available_values); ++i)
			{
				if (stricmp(sky_name, param->available_values[i]) == 0)
				{
					param->stringvalue = param->available_values[i];
					break;
				}
			}
		}
	}
}

static void wleAEVolumeSkyNameApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.client_volume.sky_volume_properties && param->index >= 0)
		{
			if (param->is_specified)
			{
				SkyInfoOverride *sky_override = eaSize(&def->property_structs.client_volume.sky_volume_properties->sky_group.override_list) > param->index ? def->property_structs.client_volume.sky_volume_properties->sky_group.override_list[param->index] : NULL;
				char sky_name[256];

				if (!sky_override)
				{
					do
					{
						sky_override = StructCreate(parse_SkyInfoOverride);
						eaPush(&def->property_structs.client_volume.sky_volume_properties->sky_group.override_list, sky_override);
					} while (eaSize(&def->property_structs.client_volume.sky_volume_properties->sky_group.override_list) < param->index + 1);
				}

				getFileNameNoExt(sky_name, param->stringvalue ? param->stringvalue : "");
				SET_HANDLE_FROM_STRING("SkyInfo", sky_name, sky_override->sky);
			}
			else
			{
				StructDestroy(parse_SkyInfoOverride, eaRemove(&def->property_structs.client_volume.sky_volume_properties->sky_group.override_list, param->index));
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeSkyPercentUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->floatvalue = 0;

	if (def && def->property_structs.client_volume.sky_volume_properties)
		param->floatvalue = def->property_structs.client_volume.sky_volume_properties->weight * 100;
}

static void wleAEVolumeSkyPercentApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.client_volume.sky_volume_properties)
		{
			def->property_structs.client_volume.sky_volume_properties->weight = saturate(param->floatvalue / 100.f);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeSkyFadeInRateUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->floatvalue = 0;

	if (def && def->property_structs.client_volume.sky_volume_properties)
		param->floatvalue = def->property_structs.client_volume.sky_volume_properties->fade_in_rate * 100;
}

static void wleAEVolumeSkyFadeInRateApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.client_volume.sky_volume_properties)
		{
			def->property_structs.client_volume.sky_volume_properties->fade_in_rate = MAX(param->floatvalue / 100.f, 0);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeSkyFadeOutRateUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->floatvalue = 0;

	if (def && def->property_structs.client_volume.sky_volume_properties)
		param->floatvalue = def->property_structs.client_volume.sky_volume_properties->fade_out_rate * 100;
}

static void wleAEVolumeSkyFadeOutRateApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.client_volume.sky_volume_properties)
		{
			def->property_structs.client_volume.sky_volume_properties->fade_out_rate = MAX(param->floatvalue / 100.f, 0);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsOccluderApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (param->boolvalue)
		{
			groupDefAddVolumeType(def, "Occluder");
		}
		else
		{
			groupDefRemoveVolumeType(def, "Occluder");
			groupDefRemoveProperty(def, "OccluderFaces");
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeOccluderFaceUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	const char *propertyVal;
	wleAEVolumeUpdateInit();
	
	if (!def)
		param->boolvalue = false;
	else
	{
		propertyVal = groupDefFindProperty(def, "OccluderFaces");
		if (!propertyVal)
			param->boolvalue = false;
		else if (param == &wleAEGlobalVolumeUI.data.occluderNegX)
			param->boolvalue = !!strstri(propertyVal, "negx");
		else if (param == &wleAEGlobalVolumeUI.data.occluderPosX)
			param->boolvalue = !!strstri(propertyVal, "posx");
		else if (param == &wleAEGlobalVolumeUI.data.occluderNegY)
			param->boolvalue = !!strstri(propertyVal, "negy");
		else if (param == &wleAEGlobalVolumeUI.data.occluderPosY)
			param->boolvalue = !!strstri(propertyVal, "posy");
		else if (param == &wleAEGlobalVolumeUI.data.occluderNegZ)
			param->boolvalue = !!strstri(propertyVal, "negz");
		else if (param == &wleAEGlobalVolumeUI.data.occluderPosZ)
			param->boolvalue = !!strstri(propertyVal, "posz");
		else
			param->boolvalue = false;
	}
}

static void wleAEVolumeOccluderFaceApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		const char *propertyVal;
		char *srchStr;
		char newVal[32];
		wleAEVolumeApplyInitAt(i);

		if (param == &wleAEGlobalVolumeUI.data.occluderNegX)
		{
			srchStr = "negx";
		}
		else if (param == &wleAEGlobalVolumeUI.data.occluderPosX)
		{
			srchStr = "posx";
		}
		else if (param == &wleAEGlobalVolumeUI.data.occluderNegY)
		{
			srchStr = "negy";
		}
		else if (param == &wleAEGlobalVolumeUI.data.occluderPosY)
		{
			srchStr = "posy";
		}
		else if (param == &wleAEGlobalVolumeUI.data.occluderNegZ)
		{
			srchStr = "negz";
		}
		else if (param == &wleAEGlobalVolumeUI.data.occluderPosZ)
		{
			srchStr = "posz";
		}
		else
		{
			wleOpPropsEndNoUIUpdate();
			continue;
		}

		propertyVal = groupDefFindProperty(def, "OccluderFaces");
		if (!propertyVal)
		{
			newVal[0] = 0;
		}
		else
		{
			strcpy(newVal, propertyVal);
		}
		if (param->boolvalue && !strstri(newVal, srchStr))
		{
			strcatf(newVal, " %s", srchStr);
		}
		else if (!param->boolvalue)
		{
			strstriReplace(newVal, srchStr, "");
			strstriReplace(newVal, "  ", " ");
		}
		removeLeadingAndFollowingSpaces(newVal);
		groupDefAddProperty(def, "OccluderFaces", newVal);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsActionApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isAction.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Action");

			if (def->property_structs.server_volume.action_volume_properties)
			{
				StructDestroySafe(parse_WorldActionVolumeProperties, &def->property_structs.server_volume.action_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.action_volume_properties = StructCreate(parse_WorldActionVolumeProperties);
			if (is_new)
			{
				def->property_structs.server_volume.action_volume_properties->entered_action_cond = NULL;
				def->property_structs.server_volume.action_volume_properties->entered_action = NULL;
				def->property_structs.server_volume.action_volume_properties->exited_action_cond = NULL;
				def->property_structs.server_volume.action_volume_properties->exited_action = NULL;
			}
			else
			{
				def->property_structs.server_volume.action_volume_properties->entered_action_cond = exprClone(wleAEGlobalVolumeUI.data.enteredCondition.exprvalue);
				def->property_structs.server_volume.action_volume_properties->entered_action = exprClone(wleAEGlobalVolumeUI.data.enteredAction.exprvalue);
				def->property_structs.server_volume.action_volume_properties->exited_action_cond = exprClone(wleAEGlobalVolumeUI.data.exitedCondition.exprvalue);
				def->property_structs.server_volume.action_volume_properties->exited_action = exprClone(wleAEGlobalVolumeUI.data.exitedAction.exprvalue);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Action");
			if (def->property_structs.server_volume.action_volume_properties)
			{
				StructDestroySafe(parse_WorldActionVolumeProperties, &def->property_structs.server_volume.action_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsPowerApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isPower.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Power");

			if (def->property_structs.server_volume.power_volume_properties)
			{
				StructDestroySafe(parse_WorldPowerVolumeProperties, &def->property_structs.server_volume.power_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.power_volume_properties = StructCreate(parse_WorldPowerVolumeProperties);
			if (is_new)
			{
				//def->property_structs.server_volume.power_volume_properties->power leave handle NULL;
				def->property_structs.server_volume.power_volume_properties->strength = WorldPowerVolumeStrength_Default;
				def->property_structs.server_volume.power_volume_properties->level = 0;
				def->property_structs.server_volume.power_volume_properties->repeat_time = 1.0;
				def->property_structs.server_volume.power_volume_properties->trigger_cond = NULL;
			}
			else
			{
				SET_HANDLE_FROM_STRING("PowerDef", wleAEGlobalVolumeUI.data.powerDef.refvalue, def->property_structs.server_volume.power_volume_properties->power);
				def->property_structs.server_volume.power_volume_properties->strength = StaticDefineIntGetInt(WorldPowerVolumeStrengthEnum, wleAEGlobalVolumeUI.data.powerStrength.stringvalue);
				def->property_structs.server_volume.power_volume_properties->level = wleAEGlobalVolumeUI.data.powerLevel.intvalue;
				def->property_structs.server_volume.power_volume_properties->repeat_time = wleAEGlobalVolumeUI.data.powerTime.floatvalue;
				def->property_structs.server_volume.power_volume_properties->trigger_cond = exprClone(wleAEGlobalVolumeUI.data.powerCond.exprvalue);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Power");

			if (def->property_structs.server_volume.power_volume_properties)
			{
				StructDestroySafe(parse_WorldPowerVolumeProperties, &def->property_structs.server_volume.power_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumePowerStrengthUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->stringvalue = NULL;

	if (def && def->property_structs.server_volume.power_volume_properties)
		param->stringvalue = StaticDefineIntRevLookup(WorldPowerVolumeStrengthEnum, def->property_structs.server_volume.power_volume_properties->strength);
}

static void wleAEVolumePowerStrengthApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.server_volume.power_volume_properties)
		{
			def->property_structs.server_volume.power_volume_properties->strength = StaticDefineIntGetInt(WorldPowerVolumeStrengthEnum, param->stringvalue);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumePropWarpDepartOverrideUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();
	
	if (	param->boolvalue == false && def 
		&&	def->property_structs.server_volume.warp_volume_properties 
		&&	GET_REF(def->property_structs.server_volume.warp_volume_properties->hTransSequence) )
	{
		param->boolvalue = true;
	}
}

static void wleAEVolumePropWarpDepartOverrideApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if ( def->property_structs.server_volume.warp_volume_properties )
		{
			if ( !param->boolvalue )
			{
				REMOVE_HANDLE( def->property_structs.server_volume.warp_volume_properties->hTransSequence );
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumePropWarpActionTransOverrideUpdate(WleAEParamDictionary *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if ( def && def->property_structs.server_volume.warp_volume_properties )
	{
		param->refvalue = StructAllocString(REF_STRING_FROM_HANDLE(def->property_structs.server_volume.warp_volume_properties->hTransSequence));
	}
	else
	{
		param->refvalue = NULL;
	}
}

static void wleAEVolumePropWarpActionTransOverrideApply(WleAEParamDictionary *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if ( def && def->property_structs.server_volume.warp_volume_properties )
		{
			SET_HANDLE_FROM_STRING("DoorTransitionSequenceDef", param->refvalue, def->property_structs.server_volume.warp_volume_properties->hTransSequence);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumePropWarpHasVariablesUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEVolumeUpdateInit();

		if (def && def->property_structs.server_volume.warp_volume_properties
			&& !param->disabled)
		{
			param->boolvalue = (eaSize(&def->property_structs.server_volume.warp_volume_properties->variableDefs) > 0);
			return;
		}
	}
	param->boolvalue = 0;
}

static void wleAEVolumePropWarpHasVariablesApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.warp_volume_properties)
		{
			if (!param->boolvalue)
			{
				int j;
				for(j = eaSize(&def->property_structs.server_volume.warp_volume_properties->variableDefs) - 1; j >= 0; --j)
				{
					StructDestroy(parse_WorldVariableDef, def->property_structs.server_volume.warp_volume_properties->variableDefs[j]);
				}
				eaDestroy(&def->property_structs.server_volume.warp_volume_properties->variableDefs);
			}
			else
			{
				if (!eaSize(&def->property_structs.server_volume.warp_volume_properties->variableDefs))
				{
					WorldVariableDef *var = StructCreate(parse_WorldVariableDef);
					eaPush(&def->property_structs.server_volume.warp_volume_properties->variableDefs, var);
				}
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumePropWarpVarUpdate(WleAEParamWorldVariableDef *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEVolumeUpdateInit();
		if (def && def->property_structs.server_volume.warp_volume_properties && (eaSize(&def->property_structs.server_volume.warp_volume_properties->variableDefs) > param->index) && def->property_structs.server_volume.warp_volume_properties->variableDefs[param->index])
		{
			StructCopyAll(parse_WorldVariableDef, def->property_structs.server_volume.warp_volume_properties->variableDefs[param->index],
						  &param->var_def);
			param->is_specified = true;
			return;
		}
	}

	StructReset( parse_WorldVariableDef, &param->var_def );
	param->is_specified = false;
}

static void wleAEVolumePropWarpVarApply(WleAEParamWorldVariableDef *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.warp_volume_properties)
		{
			if (param->is_specified)
			{
				while (eaSize(&def->property_structs.server_volume.warp_volume_properties->variableDefs) <= param->index) 
				{
					WorldVariableDef *varDef = StructCreate(parse_WorldVariableDef);
					eaPush(&def->property_structs.server_volume.warp_volume_properties->variableDefs, varDef);
				}

				assert(def->property_structs.server_volume.warp_volume_properties->variableDefs);
				StructDestroySafe( parse_WorldVariableDef, &def->property_structs.server_volume.warp_volume_properties->variableDefs[param->index]);
				def->property_structs.server_volume.warp_volume_properties->variableDefs[param->index]
				= StructClone( parse_WorldVariableDef, &param->var_def );
				worldVariableDefCleanup(def->property_structs.server_volume.warp_volume_properties->variableDefs[param->index]);
			}
			else if (eaSize(&def->property_structs.server_volume.warp_volume_properties->variableDefs) > param->index)
			{
				assert(def->property_structs.server_volume.warp_volume_properties->variableDefs);
				StructDestroy(parse_WorldVariableDef, def->property_structs.server_volume.warp_volume_properties->variableDefs[param->index]);
				eaRemove(&def->property_structs.server_volume.warp_volume_properties->variableDefs, param->index);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeFXFilterUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->stringvalue = NULL;

	if (def && def->property_structs.client_volume.fx_volume_properties)
		param->stringvalue = StaticDefineIntRevLookup(WorldFXVolumeFilterEnum, def->property_structs.client_volume.fx_volume_properties->fx_filter);
}

static void wleAEVolumeFXFilterApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.client_volume.fx_volume_properties)
		{
			def->property_structs.client_volume.fx_volume_properties->fx_filter = StaticDefineIntGetInt(WorldFXVolumeFilterEnum, param->stringvalue);;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsWarpApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isWarp.boolvalue)
		{
			bool is_new = true;

			groupDefAddVolumeType(def, "Warp");

			if (def->property_structs.server_volume.warp_volume_properties)
			{
				StructDestroySafe(parse_WorldWarpVolumeProperties, &def->property_structs.server_volume.warp_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.warp_volume_properties = StructCreate(parse_WorldWarpVolumeProperties);
			if (!is_new)
			{
				StructCopyAll(parse_WorldVariableDef, &wleAEGlobalVolumeUI.data.warpDest.var_def, &def->property_structs.server_volume.warp_volume_properties->warpDest);
				def->property_structs.server_volume.warp_volume_properties->warp_cond = exprClone(wleAEGlobalVolumeUI.data.warpCond.exprvalue);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Warp");

			if (def->property_structs.server_volume.warp_volume_properties)
			{
				StructDestroySafe(parse_WorldWarpVolumeProperties, &def->property_structs.server_volume.warp_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeWarpDestUpdate(WleAEParamWorldVariableDef *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();
		
	if (def && def->property_structs.server_volume.warp_volume_properties)
	{
		WorldWarpVolumeProperties* props = def->property_structs.server_volume.warp_volume_properties;
		
		param->is_specified = true;
			
		StructCopyAll(parse_WorldVariableDef, &props->warpDest, &param->var_def);
		param->var_def.eType = WVAR_MAP_POINT;
		if(!param->var_def.pSpecificValue) {
			param->var_def.pSpecificValue = StructCreate(parse_WorldVariable);
			param->var_def.pSpecificValue->eType = WVAR_MAP_POINT;
		}
		
		return;
	}

	StructReset(parse_WorldVariableDef, &param->var_def);
	param->is_specified = false;
}

static void wleAEVolumeWarpDestApply(WleAEParamWorldVariableDef *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.warp_volume_properties)
		{
			WorldWarpVolumeProperties* props = def->property_structs.server_volume.warp_volume_properties;
		
			StructCopyAll(parse_WorldVariableDef, &param->var_def, &props->warpDest);
			props->warpDest.eType = WVAR_MAP_POINT;
			if (props->warpDest.pSpecificValue)
			{
				props->warpDest.pSpecificValue->eType = WVAR_MAP_POINT;
			}
			if( wleAEWorldVariableDefDoorHasVarsDisabled( param ))
			{
				eaClearStruct(&props->variableDefs, parse_WorldVariableDef);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsLandmarkApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isLandmark.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Landmark");

			if (def->property_structs.server_volume.landmark_volume_properties)
			{
				StructDestroySafe(parse_WorldLandmarkVolumeProperties, &def->property_structs.server_volume.landmark_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.landmark_volume_properties = StructCreate(parse_WorldLandmarkVolumeProperties);
			if (!is_new)
			{
				def->property_structs.server_volume.landmark_volume_properties->icon_name = StructAllocString(wleAEGlobalVolumeUI.data.landmarkIcon.texturename);
				langMakeEditorCopy(parse_WorldLandmarkVolumeProperties, def->property_structs.server_volume.landmark_volume_properties, true);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Landmark");

			if (def->property_structs.server_volume.landmark_volume_properties)
			{
				StructDestroySafe(parse_WorldLandmarkVolumeProperties, &def->property_structs.server_volume.landmark_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsNeighborhoodApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isNeighborhood.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Neighborhood");

			if (def->property_structs.server_volume.neighborhood_volume_properties)
			{
				StructDestroySafe(parse_WorldNeighborhoodVolumeProperties, &def->property_structs.server_volume.neighborhood_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.neighborhood_volume_properties = StructCreate(parse_WorldNeighborhoodVolumeProperties);
			if (!is_new)
			{
				langMakeEditorCopy(parse_WorldNeighborhoodVolumeProperties, def->property_structs.server_volume.neighborhood_volume_properties, true);
				def->property_structs.server_volume.neighborhood_volume_properties->sound_effect = StructAllocString(wleAEGlobalVolumeUI.data.neighborhoodSound.stringvalue);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Neighborhood");

			if (def->property_structs.server_volume.neighborhood_volume_properties)
			{
				StructDestroySafe(parse_WorldNeighborhoodVolumeProperties, &def->property_structs.server_volume.neighborhood_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsLevelOverrideApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isLevelOverride.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "LevelOverride");

			if (def->property_structs.server_volume.map_level_volume_properties)
			{
				StructDestroySafe(parse_WorldMapLevelOverrideVolumeProperties, &def->property_structs.server_volume.map_level_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.map_level_volume_properties = StructCreate(parse_WorldMapLevelOverrideVolumeProperties);
			if (!is_new)
			{
				langMakeEditorCopy(parse_WorldMapLevelOverrideVolumeProperties, def->property_structs.server_volume.neighborhood_volume_properties, true);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "LevelOverride");

			if (def->property_structs.server_volume.map_level_volume_properties)
			{
				StructDestroySafe(parse_WorldMapLevelOverrideVolumeProperties, &def->property_structs.server_volume.map_level_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeAIAvoidUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (def) 
		param->boolvalue = (def->property_structs.server_volume.ai_volume_properties ? def->property_structs.server_volume.ai_volume_properties->avoid : false);
}

static void wleAEVolumeAIAvoidApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.aiAvoid.boolvalue)
		{
			if (!def->property_structs.server_volume.ai_volume_properties)
			{
				def->property_structs.server_volume.ai_volume_properties = StructCreate(parse_WorldAIVolumeProperties);
			}
			def->property_structs.server_volume.ai_volume_properties->avoid = true;
			groupDefAddVolumeType(def, "AI");
		}
		else
		{
			if (def->property_structs.server_volume.ai_volume_properties)
			{
				StructDestroySafe(parse_WorldAIVolumeProperties, &def->property_structs.server_volume.ai_volume_properties);
			}
			groupDefRemoveVolumeType(def, "AI");
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeNoDynConnUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (def) 
		param->boolvalue = (def->property_structs.server_volume.beacon_volume_properties ? def->property_structs.server_volume.beacon_volume_properties->nodynconn : false);
}

static void wleAEVolumeNoDynConnApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.nodynconn.boolvalue)
		{
			if (!def->property_structs.server_volume.beacon_volume_properties)
			{
				def->property_structs.server_volume.beacon_volume_properties = StructCreate(parse_WorldBeaconVolumeProperties);
			}
			def->property_structs.server_volume.beacon_volume_properties->nodynconn = true;
			groupDefAddVolumeType(def, "Beacon");
		}
		else
		{
			if (def->property_structs.server_volume.beacon_volume_properties)
			{
				StructDestroySafe(parse_WorldBeaconVolumeProperties, &def->property_structs.server_volume.beacon_volume_properties);
			}
			groupDefRemoveVolumeType(def, "Beacon");
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsEventApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isEvent.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Event");

			if (def->property_structs.server_volume.event_volume_properties)
			{
				StructDestroySafe(parse_WorldEventVolumeProperties, &def->property_structs.server_volume.event_volume_properties);
				is_new = false;
			}

			def->property_structs.server_volume.event_volume_properties = StructCreate(parse_WorldEventVolumeProperties);
			if (is_new)
			{
				def->property_structs.server_volume.event_volume_properties->entered_cond = NULL;
				def->property_structs.server_volume.event_volume_properties->exited_cond = NULL;
			}
			else
			{
				def->property_structs.server_volume.event_volume_properties->entered_cond = exprClone(wleAEGlobalVolumeUI.data.eventEnteredCondition.exprvalue);
				def->property_structs.server_volume.event_volume_properties->exited_cond = exprClone(wleAEGlobalVolumeUI.data.eventExitedCondition.exprvalue);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Event");

			if (def->property_structs.server_volume.event_volume_properties)
			{
				StructDestroySafe(parse_WorldEventVolumeProperties, &def->property_structs.server_volume.event_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}



static void wleAEVolumeEventFirstEnterGameActionUpdate(WleAEParamGameAction *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();
	if (groupDefIsVolumeType(def, "event") && wleAEGlobalVolumeUI.data.isEvent.boolvalue)
	{
		WorldGameActionBlock* pEditorCopy = wleAEGlobalVolumeUI.data.eventFirstEnteredAction.action_block;
		const WorldGameActionBlock* pDataCopy = def->property_structs.server_volume.event_volume_properties->first_entered_action;
		if (pEditorCopy)
		{
			StructDestroySafe(parse_WorldGameActionBlock, &pEditorCopy);
		}
		if(pDataCopy)
		{
			pEditorCopy = StructClone(parse_WorldGameActionBlock, pDataCopy);
			wleAEFixupGameActionMessageKey(pEditorCopy, def, "VolumeFirstEnterAction");
		}
		
		wleAEGlobalVolumeUI.data.eventFirstEnteredAction.action_block = pEditorCopy;
	}
}


static void wleAEVolumeEventFirstEnterGameActionApply(WleAEParamGameAction *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);
		if (groupDefIsVolumeType(def, "event") && wleAEGlobalVolumeUI.data.isEvent.boolvalue)
		{
			const WorldGameActionBlock* pEditorCopy = wleAEGlobalVolumeUI.data.eventFirstEnteredAction.action_block;
			WorldGameActionBlock* pDataCopy = def->property_structs.server_volume.event_volume_properties->first_entered_action;
			if(pDataCopy)
			{
				StructDestroySafe(parse_WorldGameActionBlock, &pDataCopy);
			}
			if (pEditorCopy)
			{
				pDataCopy = StructClone(parse_WorldGameActionBlock, pEditorCopy);
			}
			if(pDataCopy)
			{
			wleAEFixupGameActionMessageKey(pDataCopy, def, "VolumeFirstEnterAction");
			}
			def->property_structs.server_volume.event_volume_properties->first_entered_action = pDataCopy;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeRoomTypeUpdate(WleAEParamCombo *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def || !def->property_structs.room_properties)
		param->stringvalue = "None";
	else if (def->property_structs.room_properties->eRoomType == WorldRoomType_Room)
		param->stringvalue = "Room";
	else if (def->property_structs.room_properties->eRoomType == WorldRoomType_Partition)
		param->stringvalue = "Room Partition";
	else if (def->property_structs.room_properties->eRoomType == WorldRoomType_Portal)
		param->stringvalue = "Portal";
}

static void wleAEVolumeRoomTypeApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (strcmpi(param->stringvalue, "None") != 0)
		{
			StructDestroySafe(parse_WorldSoundSphereProperties, &def->property_structs.sound_sphere_properties);
		}
		if (strcmpi(param->stringvalue, "Room") != 0)
		{
			groupDefRemoveProperty(def, "RoomOccluder");
			groupDefRemoveProperty(def, "RoomUseModels");
			groupDefRemoveProperty(def, "RoomLimitLights");
			if (def->property_structs.client_volume.sound_volume_properties)
			{
				StructDestroySafe(parse_WorldSoundVolumeProperties, &def->property_structs.client_volume.sound_volume_properties);
			}
		}
		if (strcmpi(param->stringvalue, "Portal") != 0)
		{
			if (def->property_structs.sound_conn_properties)
			{
				StructDestroySafe(parse_WorldSoundConnProperties, &def->property_structs.sound_conn_properties);
			}
		}

		if (strcmpi(param->stringvalue, "None") == 0)
		{
			StructDestroySafe(parse_WorldRoomProperties, &def->property_structs.room_properties);
		}
		else if (strcmpi(param->stringvalue, "Room") == 0)
		{
			if(!def->property_structs.room_properties)
			{
				def->property_structs.room_properties = StructCreate(parse_WorldRoomProperties);
			}
			def->property_structs.room_properties->eRoomType = WorldRoomType_Room;
			if (!def->property_structs.client_volume.sound_volume_properties)
			{
				def->property_structs.client_volume.sound_volume_properties = StructCreate(parse_WorldSoundVolumeProperties);
				def->property_structs.client_volume.sound_volume_properties->multiplier = 1;
			}
		}
		else if (strcmpi(param->stringvalue, "Portal") == 0)
		{
			if(!def->property_structs.room_properties)
			{
				def->property_structs.room_properties = StructCreate(parse_WorldRoomProperties);
			}
			def->property_structs.room_properties->eRoomType = WorldRoomType_Portal;
			if (!def->property_structs.sound_conn_properties)
			{
				def->property_structs.sound_conn_properties = StructCreate(parse_WorldSoundConnProperties);
				def->property_structs.sound_conn_properties->min_range = 50;
				def->property_structs.sound_conn_properties->max_range = 100;
			}
		}
		else if (strcmpi(param->stringvalue, "Room Partition") == 0)
		{
			if(!def->property_structs.room_properties)
			{
				def->property_structs.room_properties = StructCreate(parse_WorldRoomProperties);
			}
			def->property_structs.room_properties->eRoomType = WorldRoomType_Partition;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeRoomDisablePhotoUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def || !def->property_structs.room_properties || !def->property_structs.room_properties->room_instance_data)
		param->boolvalue = false;
	else
		param->boolvalue = def->property_structs.room_properties->room_instance_data->no_photo;
}

static void wleAEVolumeRoomOverridePhotoUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def || !def->property_structs.room_properties || !def->property_structs.room_properties->room_instance_data)
		param->boolvalue = false;
	else
		param->boolvalue = def->property_structs.room_properties->room_instance_data->texture_override;
}

static void wleAEVolumeRoomOverridePhotoTextureUpdate(WleAEParamTexture *param, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def || !def->property_structs.room_properties || !def->property_structs.room_properties->room_instance_data || !def->property_structs.room_properties->room_instance_data->texture_name)
		StructFreeStringSafe(&param->texturename);
	else
		param->texturename = StructAllocString(def->property_structs.room_properties->room_instance_data->texture_name);
}

static void wleAEVolumeRoomDisablePhotoApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.room_properties)
		{
			if(!def->property_structs.room_properties->room_instance_data)
			{
				def->property_structs.room_properties->room_instance_data = StructCreate(parse_RoomInstanceData);
			}
			def->property_structs.room_properties->room_instance_data->no_photo = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeRoomOverridePhotoApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.room_properties)
		{
			if(!def->property_structs.room_properties->room_instance_data)
			{
				def->property_structs.room_properties->room_instance_data = StructCreate(parse_RoomInstanceData);
			}
			def->property_structs.room_properties->room_instance_data->texture_override = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeRoomOverridePhotoTextureApply(WleAEParamTexture *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.room_properties)
		{
			if(!def->property_structs.room_properties->room_instance_data)
			{
				def->property_structs.room_properties->room_instance_data = StructCreate(parse_RoomInstanceData);
			}
			def->property_structs.room_properties->room_instance_data->texture_name = allocAddString(param->texturename);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeTypeApply(WleAEParamBool *param, const char *type, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (param->boolvalue)
		{
			groupDefAddVolumeType(def, type);
		}
		else
		{
			groupDefRemoveVolumeType(def, type);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsWaterApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isWater.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Water");

			if (def->property_structs.client_volume.water_volume_properties)
			{
				StructDestroySafe(parse_WorldWaterVolumeProperties, &def->property_structs.client_volume.water_volume_properties);
				is_new = false;
			}

			def->property_structs.client_volume.water_volume_properties = StructCreate(parse_WorldWaterVolumeProperties);
			if (is_new)
			{
				def->property_structs.client_volume.water_volume_properties->water_def = StructAllocString("Air");
				def->property_structs.client_volume.water_volume_properties->water_cond = NULL;
			}
			else
			{
				def->property_structs.client_volume.water_volume_properties->water_def = StructAllocString(wleAEGlobalVolumeUI.data.waterDef.stringvalue?wleAEGlobalVolumeUI.data.waterDef.stringvalue:"Air");
				def->property_structs.client_volume.water_volume_properties->water_cond = allocAddString(wleAEGlobalVolumeUI.data.waterCond.stringvalue);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Water");
			StructDestroySafe(parse_WorldWaterVolumeProperties, &def->property_structs.client_volume.water_volume_properties);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeClusterApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isCluster.boolvalue)
		{
			groupDefAddVolumeType(def, "Cluster");

			if (def->property_structs.client_volume.cluster_volume_properties)
			{
				StructDestroySafe(parse_WorldClusterVolumeProperties, &def->property_structs.client_volume.cluster_volume_properties);
			}

			def->property_structs.client_volume.cluster_volume_properties = StructCreate(parse_WorldClusterVolumeProperties);

			if (wleAEGlobalVolumeUI.data.targetLOD.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->targetLOD = StaticDefineIntGetInt(ClusterTargetLODEnum, wleAEGlobalVolumeUI.data.targetLOD.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.targetLOD.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTargetLODEnum, def->property_structs.client_volume.cluster_volume_properties->targetLOD);
			}

			if (wleAEGlobalVolumeUI.data.minLevel.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->minLevel = StaticDefineIntGetInt(ClusterMinLevelEnum, wleAEGlobalVolumeUI.data.minLevel.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.minLevel.stringvalue = StaticDefineIntRevLookupNonNull(ClusterMinLevelEnum, def->property_structs.client_volume.cluster_volume_properties->minLevel);
			}

			if (wleAEGlobalVolumeUI.data.maxLODLevel.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->maxLODLevel = StaticDefineIntGetInt(ClusterMaxLODLevelEnum, wleAEGlobalVolumeUI.data.maxLODLevel.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.maxLODLevel.stringvalue = StaticDefineIntRevLookupNonNull(ClusterMaxLODLevelEnum, def->property_structs.client_volume.cluster_volume_properties->maxLODLevel);
			}

			if (wleAEGlobalVolumeUI.data.textureWidth.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->textureWidth = StaticDefineIntGetInt(ClusterTextureResolutionEnum, wleAEGlobalVolumeUI.data.textureWidth.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.textureWidth.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTextureResolutionEnum, def->property_structs.client_volume.cluster_volume_properties->textureWidth);
			}

			if (wleAEGlobalVolumeUI.data.textureHeight.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->textureHeight = StaticDefineIntGetInt(ClusterTextureResolutionEnum, wleAEGlobalVolumeUI.data.textureHeight.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.textureHeight.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTextureResolutionEnum, def->property_structs.client_volume.cluster_volume_properties->textureHeight);
			}

			if (wleAEGlobalVolumeUI.data.textureSupersample.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->textureSupersample = StaticDefineIntGetInt(ClusterTextureSupersampleEnum, wleAEGlobalVolumeUI.data.textureSupersample.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.textureSupersample.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTextureSupersampleEnum, def->property_structs.client_volume.cluster_volume_properties->textureSupersample);
			}

			if (wleAEGlobalVolumeUI.data.geometryResolution.stringvalue)
			{
				def->property_structs.client_volume.cluster_volume_properties->geometryResolution = StaticDefineIntGetInt(ClusterGeometryResolutionEnum, wleAEGlobalVolumeUI.data.geometryResolution.stringvalue);
			}
			else
			{
				wleAEGlobalVolumeUI.data.geometryResolution.stringvalue = StaticDefineIntRevLookupNonNull(ClusterGeometryResolutionEnum, def->property_structs.client_volume.cluster_volume_properties->geometryResolution);
			}

			def->property_structs.client_volume.cluster_volume_properties->includeNormal	=	wleAEGlobalVolumeUI.data.includeNormal.boolvalue;

			def->property_structs.client_volume.cluster_volume_properties->includeSpecular	=	wleAEGlobalVolumeUI.data.includeSpecular.boolvalue;
		}
		else
		{
			groupDefRemoveVolumeType(def, "Cluster");
			StructDestroySafe(parse_WorldClusterVolumeProperties, &def->property_structs.client_volume.cluster_volume_properties);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeClusterUpdate(void *param_unused, void *unused, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (wleAEGlobalVolumeUI.data.isCluster.boolvalue)
	{
		if ((wleAEGlobalVolumeUI.data.targetLOD.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->targetLOD = StaticDefineIntGetInt(ClusterTargetLODEnum, wleAEGlobalVolumeUI.data.targetLOD.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.targetLOD.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTargetLODEnum, def->property_structs.client_volume.cluster_volume_properties->targetLOD);
		}

		if ((wleAEGlobalVolumeUI.data.minLevel.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->minLevel = StaticDefineIntGetInt(ClusterMinLevelEnum, wleAEGlobalVolumeUI.data.minLevel.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.minLevel.stringvalue = StaticDefineIntRevLookupNonNull(ClusterMinLevelEnum, def->property_structs.client_volume.cluster_volume_properties->minLevel);
		}

		if ((wleAEGlobalVolumeUI.data.maxLODLevel.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->maxLODLevel = StaticDefineIntGetInt(ClusterMaxLODLevelEnum, wleAEGlobalVolumeUI.data.maxLODLevel.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.maxLODLevel.stringvalue = StaticDefineIntRevLookupNonNull(ClusterMaxLODLevelEnum, def->property_structs.client_volume.cluster_volume_properties->maxLODLevel);
		}

		if ((wleAEGlobalVolumeUI.data.textureHeight.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->textureHeight = StaticDefineIntGetInt(ClusterTextureResolutionEnum, wleAEGlobalVolumeUI.data.textureHeight.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.textureHeight.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTextureResolutionEnum, def->property_structs.client_volume.cluster_volume_properties->textureHeight);
		}

		if ((wleAEGlobalVolumeUI.data.textureWidth.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->textureWidth = StaticDefineIntGetInt(ClusterTextureResolutionEnum, wleAEGlobalVolumeUI.data.textureWidth.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.textureWidth.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTextureResolutionEnum, def->property_structs.client_volume.cluster_volume_properties->textureWidth);
		}

		if ((wleAEGlobalVolumeUI.data.textureSupersample.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->textureSupersample = StaticDefineIntGetInt(ClusterTextureSupersampleEnum, wleAEGlobalVolumeUI.data.textureSupersample.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.textureSupersample.stringvalue = StaticDefineIntRevLookupNonNull(ClusterTextureSupersampleEnum, def->property_structs.client_volume.cluster_volume_properties->textureSupersample);
		}

		if ((wleAEGlobalVolumeUI.data.geometryResolution.stringvalue) && (wleAEGlobalVolumeUI.data.previouslyUpdated == obj))
		{
			def->property_structs.client_volume.cluster_volume_properties->geometryResolution = StaticDefineIntGetInt(ClusterGeometryResolutionEnum, wleAEGlobalVolumeUI.data.geometryResolution.stringvalue);
		}
		else
		{
			wleAEGlobalVolumeUI.data.geometryResolution.stringvalue = StaticDefineIntRevLookupNonNull(ClusterGeometryResolutionEnum, def->property_structs.client_volume.cluster_volume_properties->geometryResolution);
		}

		if  (wleAEGlobalVolumeUI.data.previouslyUpdated == obj)
		{
			def->property_structs.client_volume.cluster_volume_properties->includeNormal	=	wleAEGlobalVolumeUI.data.includeNormal.boolvalue;
		}

		if  (wleAEGlobalVolumeUI.data.previouslyUpdated == obj)
		{
			def->property_structs.client_volume.cluster_volume_properties->includeSpecular	=	wleAEGlobalVolumeUI.data.includeSpecular.boolvalue;
		}
	}
	else
	{
		StructDestroySafe(parse_WorldClusterVolumeProperties, &def->property_structs.client_volume.cluster_volume_properties);
	}
	wleAEGlobalVolumeUI.data.previouslyUpdated = obj;
}

static void wleAEVolumeIsIndoorApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isIndoor.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "Indoor");

			if (def->property_structs.client_volume.indoor_volume_properties)
			{
				StructDestroySafe(parse_WorldIndoorVolumeProperties, &def->property_structs.client_volume.indoor_volume_properties);
				is_new = false;
			}

			def->property_structs.client_volume.indoor_volume_properties = StructCreate(parse_WorldIndoorVolumeProperties);
			if (is_new)
			{
				setVec3(def->property_structs.client_volume.indoor_volume_properties->ambient_hsv, 0, 0, 1);
				def->property_structs.client_volume.indoor_volume_properties->light_range = 0;
				def->property_structs.client_volume.indoor_volume_properties->can_see_outdoors = false;
			}
			else
			{
				copyVec3(wleAEGlobalVolumeUI.data.indoorAmbient.hsvvalue, def->property_structs.client_volume.indoor_volume_properties->ambient_hsv);
				def->property_structs.client_volume.indoor_volume_properties->light_range = wleAEGlobalVolumeUI.data.indoorLightRange.floatvalue;
				def->property_structs.client_volume.indoor_volume_properties->can_see_outdoors = wleAEGlobalVolumeUI.data.indoorSeeOutdoors.boolvalue;
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "Indoor");
			StructDestroySafe(parse_WorldIndoorVolumeProperties, &def->property_structs.client_volume.indoor_volume_properties);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsFXApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isFX.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "FX");

			if (def->property_structs.client_volume.fx_volume_properties)
			{
				StructDestroySafe(parse_WorldFXVolumeProperties, &def->property_structs.client_volume.fx_volume_properties);
				is_new = false;
			}
			def->property_structs.client_volume.fx_volume_properties = StructCreate(parse_WorldFXVolumeProperties);
		}
		else
		{
			groupDefRemoveVolumeType(def, "FX");

			if (def->property_structs.client_volume.fx_volume_properties)
			{
				StructDestroySafe(parse_WorldFXVolumeProperties, &def->property_structs.client_volume.fx_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}


static void wleAEVolumeIsCivilianApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isCivilian.boolvalue)
		{
			groupDefAddVolumeType(def, "Civilian");

			if (def->property_structs.server_volume.civilian_volume_properties)
			{
				StructDestroySafe(parse_WorldCivilianVolumeProperties, &def->property_structs.server_volume.civilian_volume_properties);
			}
			def->property_structs.server_volume.civilian_volume_properties = StructCreate(parse_WorldCivilianVolumeProperties);
		}
		else
		{
			groupDefRemoveVolumeType(def, "Civilian");

			if (def->property_structs.server_volume.civilian_volume_properties)
			{
				StructDestroySafe(parse_WorldCivilianVolumeProperties, &def->property_structs.server_volume.civilian_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}


static void wleAEVolumeCivilianCritterNameUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEVolumeUpdateInit();
		if (def && def->property_structs.server_volume.civilian_volume_properties && 
				(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) && 
				(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]) )
		{
			param->stringvalue = StructAllocString(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->critter_name);
			param->is_specified = true;
			return;
		}
	}
	param->stringvalue = NULL;
	param->is_specified = false;
}

static void wleAEVolumeCivilianCritterNameApply(WleAEParamText *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.civilian_volume_properties)
		{
			if (param->is_specified)
			{
				while (eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) <= param->index)
				{
					CivilianCritterSpawn *critter = StructCreate(parse_CivilianCritterSpawn);
					eaPush(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns, critter);
				}
				assert(def->property_structs.server_volume.civilian_volume_properties->critter_spawns);
				def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->critter_name = allocAddString(param->stringvalue);
			}
			else if (eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index)
			{
				assert(def->property_structs.server_volume.civilian_volume_properties->critter_spawns);
				StructDestroy(parse_CivilianCritterSpawn, def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]);
				eaRemove(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns, param->index);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

// rrp- having to write these functions per variable is ugly. This and most of the other things here 
// can totally be abstracted... 
// SIP- heartily seconded

static void wleAEVolumeCivilianCritterChanceApply(WleAEParamFloat *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.civilian_volume_properties && 
			(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) && 
			(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]))
		{
			def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->spawn_weight = param->floatvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeCivilianCritterChanceUpdate(WleAEParamFloat *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEVolumeUpdateInit();
		if (def && def->property_structs.server_volume.civilian_volume_properties && 
				(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) && 
				(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]) )
		{
			param->floatvalue = def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->spawn_weight;
			return;
		}
	}
	param->floatvalue = 0;
}

static void wleAEVolumeCivilianCritterIsCarUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEVolumeUpdateInit();
		if (def && def->property_structs.server_volume.civilian_volume_properties && 
				(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) &&
				(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]) )
		{
			param->boolvalue = def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->is_car;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEVolumeCivilianCritterIsCarApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.civilian_volume_properties && 
				(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) && 
				(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]) )
		{
			def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->is_car = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}


static void wleAEVolumeCivilianCritterRestrictedUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		wleAEVolumeUpdateInit();
		if (def && def->property_structs.server_volume.civilian_volume_properties && 
				(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) &&
				(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]) )
		{
			param->boolvalue = def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->restricted_to_volume;
			return;
		}
	}
	param->boolvalue = false;
}

static void wleAEVolumeCivilianCritterRestrictedApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def && def->property_structs.server_volume.civilian_volume_properties && 
				(eaSize(&def->property_structs.server_volume.civilian_volume_properties->critter_spawns) > param->index) && 
				(def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]) )
		{
			def->property_structs.server_volume.civilian_volume_properties->critter_spawns[param->index]->restricted_to_volume = param->boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}



static void wleAEVolumeIsDebrisFieldExcluderApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isDebrisFieldExcluder.boolvalue)
		{
			bool is_new = true;
			groupDefAddVolumeType(def, "DebrisFieldExclusion");
		}
		else
		{
			groupDefRemoveVolumeType(def, "DebrisFieldExclusion");
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeIsExcluderApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.isExcluder.boolvalue)
		{
			bool is_new = true;

			groupDefAddVolumeType(def, "TerrainExclusion");
			if (!def->property_structs.terrain_exclusion_properties)
			{
				def->property_structs.terrain_exclusion_properties = StructCreate(parse_WorldTerrainExclusionProperties);
			}
		}
		else
		{
			groupDefRemoveVolumeType(def, "TerrainExclusion");
			if (def->property_structs.terrain_exclusion_properties)
			{
				StructDestroy(parse_WorldTerrainExclusionProperties, def->property_structs.terrain_exclusion_properties);
			}
			def->property_structs.terrain_exclusion_properties = NULL;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeExclusionTypeUpdate(WleAEParamCombo *param, const char *typeName, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def)
		param->stringvalue = "Anywhere";
	else
		param->stringvalue = (def && def->property_structs.terrain_exclusion_properties) ? param->available_values[def->property_structs.terrain_exclusion_properties->exclusion_type] : "Anywhere";
}

static void wleAEVolumeExclusionTypeApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int j;
		wleAEVolumeApplyInitAt(i);

		if (!def->property_structs.terrain_exclusion_properties)
		{
			def->property_structs.terrain_exclusion_properties = StructCreate(parse_WorldTerrainExclusionProperties);
		}
		for (j = 0; j < 3; j++)
		{
			if (!stricmp(wleAEGlobalVolumeUI.data.exclusionType.stringvalue, wleAEGlobalVolumeUI.data.exclusionType.available_values[j]))
			{
				def->property_structs.terrain_exclusion_properties->exclusion_type = j;
				break;
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeCollisionTypeUpdate(WleAEParamCombo *param, const char *typeName, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->stringvalue = NULL;
	if (def && def->property_structs.terrain_exclusion_properties)
		param->stringvalue = StaticDefineIntRevLookup(WorldTerrainCollisionTypeUIEnum, def->property_structs.terrain_exclusion_properties->collision_type);
	if(!param->stringvalue)
		param->stringvalue = StaticDefineIntRevLookup(WorldTerrainCollisionTypeUIEnum, 0);
}

static void wleAEVolumeCollisionTypeApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int val;
		wleAEVolumeApplyInitAt(i);

		if (!def->property_structs.terrain_exclusion_properties)
		{
			def->property_structs.terrain_exclusion_properties = StructCreate(parse_WorldTerrainExclusionProperties);
		}
		val = StaticDefineIntGetInt(WorldTerrainCollisionTypeUIEnum, wleAEGlobalVolumeUI.data.exclusionCollisionType.stringvalue);
		val = MAX(0, val);
		def->property_structs.terrain_exclusion_properties->collision_type = val;
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumePlatformTypeUpdate(WleAEParamCombo *param, const char *typeName, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	param->stringvalue = NULL;
	if (def && def->property_structs.terrain_exclusion_properties)
		param->stringvalue = StaticDefineIntRevLookup(WorldPlatformTypeEnum, def->property_structs.terrain_exclusion_properties->platform_type);
	if(!param->stringvalue)
		param->stringvalue = StaticDefineIntRevLookup(WorldPlatformTypeEnum, 0);
}

static void wleAEVolumePlatformTypeApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		int val;
		wleAEVolumeApplyInitAt(i);

		if (!def->property_structs.terrain_exclusion_properties)
		{
			def->property_structs.terrain_exclusion_properties = StructCreate(parse_WorldTerrainExclusionProperties);
		}
		val = StaticDefineIntGetInt(WorldPlatformTypeEnum, wleAEGlobalVolumeUI.data.exclusionPlatformType.stringvalue);
		val = MAX(0, val);
		def->property_structs.terrain_exclusion_properties->platform_type = val;
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEVolumeChallengesOnlyUpdate(WleAEParamBool *param, const char *typeName, EditorObject *obj)
{
	wleAEVolumeUpdateInit();

	if (!def || !def->property_structs.terrain_exclusion_properties)
		param->boolvalue = false;
	else
		param->boolvalue = def->property_structs.terrain_exclusion_properties->challenges_only;
}

static void wleAEVolumeChallengesOnlyApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (def->property_structs.terrain_exclusion_properties)
		{
			def->property_structs.terrain_exclusion_properties->challenges_only = wleAEGlobalVolumeUI.data.exclusionChallengesOnly.boolvalue;
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static bool wleAEVolumeCritCheck(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, void *data)
{
	wleAEVolumeUpdateInit();
	return groupDefIsVolumeType(def, val);
}

static void wleAEVolumeIsMastermindApply(void *param_unused, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEVolumeApplyInitAt(i);

		if (wleAEGlobalVolumeUI.data.hasMastermindProperties.boolvalue)
		{
			groupDefAddVolumeType(def, "Mastermind");

			if (def->property_structs.server_volume.mastermind_volume_properties)
			{
				StructDestroySafe(parse_WorldMastermindVolumeProperties, &def->property_structs.server_volume.mastermind_volume_properties);
			}
			def->property_structs.server_volume.mastermind_volume_properties = StructCreate(parse_WorldMastermindVolumeProperties);
		}
		else
		{
			groupDefRemoveVolumeType(def, "Mastermind");

			if (def->property_structs.server_volume.mastermind_volume_properties)
			{
				StructDestroySafe(parse_WorldMastermindVolumeProperties, &def->property_structs.server_volume.mastermind_volume_properties);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

/********************
* MAIN
********************/

static bool wleAEVolumeHasProps(GroupDef *def)
{
	return !!(def->property_structs.volume || def->property_structs.room_properties);
}

static void wleAEVolumeAddCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pTracker->def) {
		if(!wleAEVolumeHasProps(pData->pTracker->def)) {
			wleAEInitializeVolume(pData->pTracker->def);
		}
	}
}

void wleAEVolumeAdd(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAEVolumeAddCB, NULL, NULL);
}

static void wleAEVolumeRemoveCB(EditorObject *pObject, WleAESelectionCBData *pData, UserData *pUnused, UserData *pUnused2)
{
	if(pData->pTracker->def) {
		StructDestroySafe(parse_GroupVolumeProperties, &pData->pTracker->def->property_structs.volume);
		StructDestroySafe(parse_WorldRoomProperties, &pData->pTracker->def->property_structs.room_properties);
		StructDestroySafe(parse_WorldSoundVolumeProperties, &pData->pTracker->def->property_structs.client_volume.sound_volume_properties);
		StructDestroySafe(parse_WorldSoundConnProperties, &pData->pTracker->def->property_structs.sound_conn_properties);
	}
}

void wleAEVolumeRemove(UserData *pUnused, UserData *pUnused2)
{
	wleAEApplyToSelection(wleAEVolumeRemoveCB, NULL, NULL);
}

int wleAEVolumeReload(EMPanel *panel, EditorObject *edObj)
{
	EditorObject **objects = NULL;
	Vec3 min = {-1000000, -1000000, -1000000}, max = {1000000, 1000000, 1000000}, step = {1, 1, 1};
	bool panelActive = true;
	bool canBeRoom = true;
	bool canBePartition = true;
	bool hide = false;
	int i;
	bool common_scope = false;
	WorldScope *closest_scope = NULL;
	bool isVolume, isRoom;
	bool hasProps = false;
	int var_index, civ_index;
	int numSkies = 0;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *tracker;

		assert(objects[i]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(objects[i]->obj);
		if (!tracker || !tracker->def || wleNeedsEncounterPanels(tracker->def))
		{
			hide = true;
			break;
		}

		if (!hasProps)
			hasProps = wleAEVolumeHasProps(tracker->def);
		if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
			panelActive = false;

		if (tracker->parent && tracker->parent->room)
			canBeRoom = false;
		if (!tracker->parent || !tracker->parent->room || tracker->parent->room->box_room || tracker->parent->room_partition)
			canBePartition = false;		

		if (tracker->def->property_structs.client_volume.sky_volume_properties)
			numSkies = MAX(numSkies, eaSize(&tracker->def->property_structs.client_volume.sky_volume_properties->sky_group.override_list));

		// Hide some panels if volumes are not in the same scope
		if (!closest_scope)
		{
			common_scope = true;
			closest_scope = tracker->closest_scope;
		}
		else if (closest_scope != tracker->closest_scope)
		{
			common_scope = false;
		}
	}
	eaDestroy(&objects);

	if (hide)
		return WLE_UI_PANEL_INVALID;

	// need a blank sky combo for adding skies
	numSkies++;

	// update combo box values
	wlVolumeWaterDefKeys(&wleAEGlobalVolumeUI.data.waterDef.available_values);
	eaClear(&wleAEGlobalVolumeUI.data.roomType.available_values);
	eaPush(&wleAEGlobalVolumeUI.data.roomType.available_values, "None");
	if (canBeRoom)
		eaPush(&wleAEGlobalVolumeUI.data.roomType.available_values, "Room");
	else if (canBePartition)
		eaPush(&wleAEGlobalVolumeUI.data.roomType.available_values, "Room Partition");

	// update combo contents for water trigger condition
	eaDestroy(&wleAEGlobalVolumeUI.triggerConditionNames);
	if(common_scope && closest_scope && closest_scope->name_to_obj)
		worldGetObjectNames(WL_ENC_TRIGGER_CONDITION, &wleAEGlobalVolumeUI.triggerConditionNames, closest_scope);
	wleAEGlobalVolumeUI.data.waterCond.available_values = wleAEGlobalVolumeUI.triggerConditionNames;

	// update array parameters
	eaDestroyStruct(&wleAEGlobalVolumeUI.data.skyNames, parse_WleAEParamCombo);
	for (i = 0; i < numSkies; i++)
	{
		WleAEParamCombo *combo = StructCreate(parse_WleAEParamCombo);
		wleAEVolumeSetupParam((*combo), NULL);
		combo->available_values = gfxGetAllSkyNames(false);
		combo->update_func = wleAEVolumeSkyNameUpdate;
		combo->apply_func = wleAEVolumeSkyNameApply;
		combo->entry_width = 1.0;
		combo->left_pad += 20;
		combo->index = i;
		combo->can_unspecify = true;
		combo->is_filtered = true;
		eaPush(&wleAEGlobalVolumeUI.data.skyNames, combo);
	}

	wleAEGlobalVolumeUI.data.civLegDef.available_values = (char**)gclCivilian_GetLegDefNames();

	// fill data
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.volumeStyle);
	if (stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "Yes")==0)
		eaPush(&wleAEGlobalVolumeUI.data.roomType.available_values, "Portal");
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.volumeShape);
	wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.volumeRadius);
	wleAEVec3Update(&wleAEGlobalVolumeUI.data.volumeMin);
	wleAEVec3Update(&wleAEGlobalVolumeUI.data.volumeMax);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isSkyFade);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.skyFadeIsPositional);
	for (i = 0; i < numSkies; i++)
		wleAEComboUpdate(wleAEGlobalVolumeUI.data.skyNames[i]);
	wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.skyPercent);
	wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.skyFadeInRate);
	wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.skyFadeOutRate);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isOccluder);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.occluderPosX);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.occluderNegX);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.occluderPosY);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.occluderNegY);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.occluderPosZ);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.occluderNegZ);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isAction);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.enteredCondition);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.enteredAction);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.exitedCondition);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.exitedAction);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isPower);
	wleAEDictionaryUpdate(&wleAEGlobalVolumeUI.data.powerDef);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.powerStrength);
	wleAEIntUpdate(&wleAEGlobalVolumeUI.data.powerLevel);
	wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.powerTime);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.powerCond);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isCluster);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.targetLOD);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.minLevel);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.maxLODLevel);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.textureHeight);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.textureWidth);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.textureSupersample);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.includeNormal);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.includeSpecular);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isWarp);
	wleAEWorldVariableDefUpdate(&wleAEGlobalVolumeUI.data.warpDest);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.warpSequenceOverride);
	wleAEDictionaryUpdate(&wleAEGlobalVolumeUI.data.warpTransition);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.warpCond);

	wleAEGlobalVolumeUI.data.warpHasVariables.disabled = wleAEWorldVariableDefDoorHasVarsDisabled( &wleAEGlobalVolumeUI.data.warpDest );
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.warpHasVariables);
	for(var_index=0; var_index<MAX_VARIABLES; ++var_index)
	{
		WorldVariable* mapDest = wleAEWorldVariableCalcVariableNonRandom( &wleAEGlobalVolumeUI.data.warpDest.var_def );
		wleAEGlobalVolumeUI.data.warpVariables[var_index]->var.dest_map_name = SAFE_MEMBER(mapDest, pcZoneMap);
		wleAEWorldVariableDefUpdate(&wleAEGlobalVolumeUI.data.warpVariables[var_index]->var);
	}

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isLandmark);
	wleAETextureUpdate(&wleAEGlobalVolumeUI.data.landmarkIcon);
	wleAEMessageUpdate(&wleAEGlobalVolumeUI.data.landmarkDispName);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.landmarkHideUnlessRevealed);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isNeighborhood);
	wleAEMessageUpdate(&wleAEGlobalVolumeUI.data.neighborhoodDispName);
	wleAETextUpdate(&wleAEGlobalVolumeUI.data.neighborhoodSound);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isLevelOverride);
	wleAEIntUpdate(&wleAEGlobalVolumeUI.data.levelOverride);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.aiAvoid);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isEvent);
	wleAEGameActionUpdate(&wleAEGlobalVolumeUI.data.eventFirstEnteredAction);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.eventEnteredCondition);
	wleAEExpressionUpdate(&wleAEGlobalVolumeUI.data.eventExitedCondition);

	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.roomType);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.roomOccluder);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.roomUseModels);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.roomDisablePhoto);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.roomOverridePhoto);
	wleAETextureUpdate(&wleAEGlobalVolumeUI.data.roomOverridePhotoTexture);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.roomLimitLights);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isWater);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.waterDef);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.waterCond);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.playable);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.nodynconn);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.duelDisable);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.duelEnable);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.ignoreSound);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isIndoor);
	wleAEHSVUpdate(&wleAEGlobalVolumeUI.data.indoorAmbient);
	wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.indoorLightRange);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.indoorSeeOutdoors);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.petsDisabled);

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isFX);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.fxFilter);
	wleAEDictionaryUpdate(&wleAEGlobalVolumeUI.data.fxEntrance);
	wleAEHueUpdate(&wleAEGlobalVolumeUI.data.fxEntranceHue);
	wleAEDictionaryUpdate(&wleAEGlobalVolumeUI.data.fxExit);
	wleAEHueUpdate(&wleAEGlobalVolumeUI.data.fxExitHue);
	wleAEDictionaryUpdate(&wleAEGlobalVolumeUI.data.fxMaintained);
	wleAEHueUpdate(&wleAEGlobalVolumeUI.data.fxMaintainedHue);

	// civilian
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isCivilian);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civDisablesSidewalk);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civDisablesRoad);

	
	for (civ_index=0; civ_index<MAX_CRITTER_SETS; ++civ_index)
	{
		wleAETextUpdate(&wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterName);
		wleAEFloatUpdate(&wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterChance);
		wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civCritters[civ_index]->isCar);
		wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civCritters[civ_index]->restrictedToVolume);
	}

	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civForcedRoad);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civForcedRoadHasMedian);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civForcedIntersection);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civForcedSidewalk);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civForcedCrosswalk);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civForcedAsIs);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.civPedestrianWanderArea);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.civLegDef);
	
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isDebrisFieldExcluder);

	// Terrain exclusion
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isExcluder);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.exclusionType);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.exclusionCollisionType);
	wleAEComboUpdate(&wleAEGlobalVolumeUI.data.exclusionPlatformType);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.exclusionChallengesOnly);

	// UGC room footprint
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.isUGCRoomFootprint);

	// mastermind 
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.hasMastermindProperties);
	wleAEBoolUpdate(&wleAEGlobalVolumeUI.data.mastermindRoomIsSafe);

	// rebuild UI
	ui_RebuildableTreeInit(wleAEGlobalVolumeUI.autoWidget, &wleAEGlobalVolumeUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);

	isVolume = wleAEGlobalVolumeUI.data.volumeStyle.stringvalue && stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "No") != 0;
	isRoom = eaSize(&wleAEGlobalVolumeUI.data.roomType.available_values) > 1 && wleAEGlobalVolumeUI.data.roomType.stringvalue && stricmp(wleAEGlobalVolumeUI.data.roomType.stringvalue, "Room") == 0;

	wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Volume", "Marks the def as a volume or subvolume.", "volumeStyle", &wleAEGlobalVolumeUI.data.volumeStyle);

	// room type
	if (eaSize(&wleAEGlobalVolumeUI.data.roomType.available_values) > 1)
		wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Room Type", "The type of room volume.", "room_type", &wleAEGlobalVolumeUI.data.roomType);

	if (isVolume || isRoom)
	{
		if (isVolume)
		{
			wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Shape", "Sets the shape of the volume.", "volumeShape", &wleAEGlobalVolumeUI.data.volumeShape);
			if (wleAEGlobalVolumeUI.data.volumeShape.stringvalue && stricmp(wleAEGlobalVolumeUI.data.volumeShape.stringvalue, "Box") == 0)
			{
				wleAEVec3AddWidget(wleAEGlobalVolumeUI.autoWidget, "Min Bounds", "The minimum of the volume's bounds.", "volume_min", &wleAEGlobalVolumeUI.data.volumeMin, min, wleAEGlobalVolumeUI.data.volumeMax.vecvalue, step);
				wleAEVolumeUISkinAxes(wleAEGlobalVolumeUI.data.volumeMin)
				wleAEVec3AddWidget(wleAEGlobalVolumeUI.autoWidget, "Max Bounds", "The maximum of the volume's bounds.", "volume_max", &wleAEGlobalVolumeUI.data.volumeMax, wleAEGlobalVolumeUI.data.volumeMin.vecvalue, max, step);
				wleAEVolumeUISkinAxes(wleAEGlobalVolumeUI.data.volumeMax)
			}
			else
			{
				wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Radius", "The radius of the volume's sphere.", "volumeRadius", &wleAEGlobalVolumeUI.data.volumeRadius, 0.001f, 100000, 1);
			}
		}

		if (isRoom || stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "Yes")==0)
		{
			if (!isRoom && wleAEGlobalVolumeUI.data.volumeShape.stringvalue && stricmp(wleAEGlobalVolumeUI.data.volumeShape.stringvalue, "Box") == 0)
			{
				// occluder
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Occluder", "Marks the def as an occluder volume.", "is_occluder", &wleAEGlobalVolumeUI.data.isOccluder);
				if (wleAEGlobalVolumeUI.data.isOccluder.boolvalue)
				{
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Min X", "Marks the negative X face of the occluder volume as occluding.", "occluder_negx", &wleAEGlobalVolumeUI.data.occluderNegX);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Max X", "Marks the positive X face of the occluder volume as occluding.", "occluder_posx", &wleAEGlobalVolumeUI.data.occluderPosX);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Min Y", "Marks the negative Y face of the occluder volume as occluding.", "occluder_negy", &wleAEGlobalVolumeUI.data.occluderNegY);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Max Y", "Marks the positive Y face of the occluder volume as occluding.", "occluder_posy", &wleAEGlobalVolumeUI.data.occluderPosY);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Min Z", "Marks the negative Z face of the occluder volume as occluding.", "occluder_negz", &wleAEGlobalVolumeUI.data.occluderNegZ);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Max Z", "Marks the positive Z face of the occluder volume as occluding.", "occluder_posz", &wleAEGlobalVolumeUI.data.occluderPosZ);
				}
			}
			else if (isRoom)
			{
				if (wleAEGlobalVolumeUI.data.volumeStyle.stringvalue && stricmp(wleAEGlobalVolumeUI.data.volumeStyle.stringvalue, "No") == 0)
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Use Model Verts", "Use model vertices to calculate a tighter hull around the room instead of using model bounding boxes.", "roomUseModels", &wleAEGlobalVolumeUI.data.roomUseModels);
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Disable map photo", "Used to tell the map photo system to skip this room when taking photos.", "roomDisablePhoto", &wleAEGlobalVolumeUI.data.roomDisablePhoto);
				if(!wleAEGlobalVolumeUI.data.roomDisablePhoto.boolvalue)
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Override map photo", "Uses the specified texture rather than take photos", "roomOverridePhoto", &wleAEGlobalVolumeUI.data.roomOverridePhoto);
				if(!wleAEGlobalVolumeUI.data.roomDisablePhoto.boolvalue && wleAEGlobalVolumeUI.data.roomOverridePhoto.boolvalue)
					wleAETextureAddWidget(wleAEGlobalVolumeUI.autoWidget, "Override Image", "Texture to use instead of take picutres", "roomOverridePhotoTexture", &wleAEGlobalVolumeUI.data.roomOverridePhotoTexture);
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Create Occlusion", "Automatically create occlusion geometry around the room.", "roomOccluder", &wleAEGlobalVolumeUI.data.roomOccluder);
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Limit Lights", "Only allow lights in the room to affect object inside the room.", "roomLimitLights", &wleAEGlobalVolumeUI.data.roomLimitLights);
			}
	
			// playable volume
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Playable", "Marks the def as a playable volume.", "playable", &wleAEGlobalVolumeUI.data.playable);

			// no dynamic conns
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "No Dyn Conns", "Disables dynamic connections in the volume", "nodynconn", &wleAEGlobalVolumeUI.data.nodynconn);

			// duel disable override volume
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Duel Disable", "Marks the def as a duel disabled volume.", "duelEnable", &wleAEGlobalVolumeUI.data.duelDisable);

			// Duel enable override volume
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Duel Enable", "Marks the def as a duel enabled volume.", "duelDisable", &wleAEGlobalVolumeUI.data.duelEnable);

			// Ignore Sound Occlusion
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Ignore Sound Occlusion", "Prevents the sound system from using the volume to occlude sounds.", "ignoreSound", &wleAEGlobalVolumeUI.data.ignoreSound);

			// sky fade
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "SkyFade", "Marks the def as a sky fade volume.", "isSkyFade", &wleAEGlobalVolumeUI.data.isSkyFade);
			if (wleAEGlobalVolumeUI.data.isSkyFade.boolvalue)
			{
				for (i = 0; i < numSkies; i++)
				{
					char paramDisplayName[32];
					char paramInternalName[32];
					sprintf(paramDisplayName, "Sky Name %i", i);
					sprintf(paramInternalName, "skyName%i", i);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, paramDisplayName, "Specify which sky file should be set when in this volume.", paramInternalName, wleAEGlobalVolumeUI.data.skyNames[i]);
				}
				wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Percent", "How much this sky should be faded in to (0% to 100%)", "skyPercent", &wleAEGlobalVolumeUI.data.skyPercent, 0, 100, 1);

				if (!wleAEGlobalVolumeUI.data.skyFadeIsPositional.boolvalue)
				{
					wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Fade In Rate", "How fast this sky should be faded in (in percent per second)", "skyFadeInRate", &wleAEGlobalVolumeUI.data.skyFadeInRate, 0, 10000, 1);
				}
				
				wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Fade Out Rate", "How fast this sky should be faded out (in percent per second)", "skyFadeOutRate", &wleAEGlobalVolumeUI.data.skyFadeOutRate, 0, 10000, 1);
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Is Positional", "A special mode where the sky fade % is based on the characters distance in Z", "skyFadeIsPositional", &wleAEGlobalVolumeUI.data.skyFadeIsPositional);
			}

			if (!isRoom)
			{
				// warp
				if (common_scope)
				{
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Warp", "Adds a warp to the volume.", "isWarp", &wleAEGlobalVolumeUI.data.isWarp);
					
					if (wleAEGlobalVolumeUI.data.isWarp.boolvalue)
					{
						wleAEWorldVariableDefAddWidget(wleAEGlobalVolumeUI.autoWidget, "Destination", "Determines how the destination will be chosen.", "warpDest", &wleAEGlobalVolumeUI.data.warpDest);

						wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Door Variables", "Whether or not the door passes variables to the map.", "HasVariables", &wleAEGlobalVolumeUI.data.warpHasVariables);
						if (wleAEGlobalVolumeUI.data.warpHasVariables.boolvalue)
						{
							for(var_index=0; var_index < MAX_VARIABLES; ++var_index) {
								WleAEVolumeWarpVarPropUI* volumeVar = wleAEGlobalVolumeUI.data.warpVariables[var_index];
								char buf[128];
								sprintf(buf, "Var #%d", var_index+1);

								wleAEWorldVariableDefAddWidget(wleAEGlobalVolumeUI.autoWidget, buf, "The name of the map variable.", "doorVar", &volumeVar->var);

								if ( !volumeVar->var.is_specified && !volumeVar->var.var_name_diff && !volumeVar->var.var_init_from_diff && !volumeVar->var.var_value_diff && !volumeVar->var.spec_diff)
									break;
							}
						}

						wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Override Depart Sequence", "Override the default departure sequence defined by region rules", "warpSequenceOverride", &wleAEGlobalVolumeUI.data.warpSequenceOverride);
						if ( wleAEGlobalVolumeUI.data.warpSequenceOverride.boolvalue )
						{
							wleAEDictionaryAddWidget(wleAEGlobalVolumeUI.autoWidget, "Transition", "Transition sequence to play before warping", "TransitionOverride", &wleAEGlobalVolumeUI.data.warpTransition);
						}
						wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Warp Condition", "Warp to the destination only if this condition is true.", "warpCond", &wleAEGlobalVolumeUI.data.warpCond);
					}
				}

				// landmark
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Landmark", "Adds landmark status to the volume.", "isLandmark", &wleAEGlobalVolumeUI.data.isLandmark);
				if (wleAEGlobalVolumeUI.data.isLandmark.boolvalue)
				{
					wleAETextureAddWidget(wleAEGlobalVolumeUI.autoWidget, "Icon", "The icon to show for the landmark.", "landmarkIcon", &wleAEGlobalVolumeUI.data.landmarkIcon);
					wleAEMessageAddWidget(wleAEGlobalVolumeUI.autoWidget, "Display Name", "The display name of the landmark.", "landmark_display_name_msg", &wleAEGlobalVolumeUI.data.landmarkDispName);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Hide Unless Revealed", "Hide the landmark icon from the maps until the player has revealed it.", "landmark_hide_unless_revealed", &wleAEGlobalVolumeUI.data.landmarkHideUnlessRevealed);
				}
			}

			// neighborbood
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Neighborhood", "Adds neighborhood status to the volume.", "isNeighborhood", &wleAEGlobalVolumeUI.data.isNeighborhood);
			if (wleAEGlobalVolumeUI.data.isNeighborhood.boolvalue)
			{
				wleAEMessageAddWidget(wleAEGlobalVolumeUI.autoWidget, "Display Name", "The display name of the neighborhood.", "neighborhood_display_name_msg", &wleAEGlobalVolumeUI.data.neighborhoodDispName);
				wleAETextAddWidget(wleAEGlobalVolumeUI.autoWidget, "Sound", "The sound effect to play", "soundEffect", &wleAEGlobalVolumeUI.data.neighborhoodSound);
			}

			if (isVolume)
			{
				// map level override
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Level Override", "Overrides the map level for encounter spawns within the volume.", "isLevelOverride", &wleAEGlobalVolumeUI.data.isLevelOverride);
				if (wleAEGlobalVolumeUI.data.isLevelOverride.boolvalue)
				{
					wleAEIntAddWidget(wleAEGlobalVolumeUI.autoWidget, "Level", "The level at which to spawn encounters", "levelOverride", &wleAEGlobalVolumeUI.data.levelOverride, 0, MAX_LEVELS, 1);
				}
			}
			else
			{
				wleAEGlobalVolumeUI.data.isLevelOverride.boolvalue = false;
				wleAEGlobalVolumeUI.data.levelOverride.intvalue = 0;
			}

			if (isVolume)
			{
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Pets Disabled", "Pets will despawn when players enter this volume, and respawn when players leave it.", "petsDisabled", &wleAEGlobalVolumeUI.data.petsDisabled);
			}

			if(isVolume)
			{
				// AI
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "AI Avoid", "AI will avoid this volume.", "aiAvoid", &wleAEGlobalVolumeUI.data.aiAvoid);
			}

			// action
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Action", "Adds an action to the volume.", "isAction", &wleAEGlobalVolumeUI.data.isAction);
			if (wleAEGlobalVolumeUI.data.isAction.boolvalue)
			{
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Enter Condition", "Condition under which the enter action is performed.", "enterCondition", &wleAEGlobalVolumeUI.data.enteredCondition);
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Enter Action", "The action performed on entry if enter condition is true.", "enterAction", &wleAEGlobalVolumeUI.data.enteredAction);
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Exit Condition", "Condition under which the exit condition is performed.", "exitCondition", &wleAEGlobalVolumeUI.data.exitedCondition);
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Exit Action", "The action performed on exit if exit condition is true.", "exitAction", &wleAEGlobalVolumeUI.data.exitedAction);
			}

			// powers
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Powers", "Adds a power to the volume.", "isPower", &wleAEGlobalVolumeUI.data.isPower);
			if (wleAEGlobalVolumeUI.data.isPower.boolvalue)
			{
				wleAEDictionaryAddWidget(wleAEGlobalVolumeUI.autoWidget, "Power", "The power to invoke.", "powerDef", &wleAEGlobalVolumeUI.data.powerDef);
				wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Power Strength", "The strength of the power.", "powerStrength", &wleAEGlobalVolumeUI.data.powerStrength);
				wleAEIntAddWidget(wleAEGlobalVolumeUI.autoWidget, "Power Level", "The level the power.  If zero, the level is determined automatically.", "powerLevel", &wleAEGlobalVolumeUI.data.powerLevel,0,60,1);
				wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Repeat Time", "The repeat time for the power.  If zero, it happens once on entry.", "powerTime", &wleAEGlobalVolumeUI.data.powerTime,0,10000,0.5);
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Active Condition", "This expression must be true for the power to execute.", "powerCond", &wleAEGlobalVolumeUI.data.powerCond);
			}

			// fx
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "FX", "Marks this as an fx volume.", "isFX", &wleAEGlobalVolumeUI.data.isFX);
			if (wleAEGlobalVolumeUI.data.isFX.boolvalue)
			{
				GroupTracker *tracker;
				GroupDef *def;
				assert(edObj->type->objType == EDTYPE_TRACKER);
				tracker = trackerFromTrackerHandle(edObj->obj);
				def = tracker ? tracker->def : NULL;

				wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Filter", "What objects do these FX operate on.", "fxFilter", &wleAEGlobalVolumeUI.data.fxFilter);
				wleAEDictionaryAddWidget(wleAEGlobalVolumeUI.autoWidget, "Entrance", "FX that plays on an entity as it enters the volume.", "fxEntrance", &wleAEGlobalVolumeUI.data.fxEntrance);
				if (wleAEGlobalVolumeUI.data.fxEntrance.refvalue && wleAEGlobalVolumeUI.data.fxEntrance.refvalue[0])
				{
					// FX Parameters.
					REF_TO(DynFxInfo) hInfo;
					// Get the info for this FX.
					SET_HANDLE_FROM_STRING(hDynFxInfoDict, wleAEGlobalVolumeUI.data.fxEntrance.refvalue, hInfo);
					if (GET_REF(hInfo))
					{
						if ((wleAEGlobalVolumeUI.data.pFXButtonEntrance) &&
							(ui_WidgetGroupRemove(&wleAEGlobalVolumeUI.autoWidget->old_root->groupWidget->children, UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonEntrance))))	//This line prevent the button from being destroyed if it's going to be reused. Without this, refreshing the UI destroys the window, so it never opens
						{
							ui_FXButtonUpdate(wleAEGlobalVolumeUI.data.pFXButtonEntrance, GET_REF(hInfo), &wleAEGlobalVolumeUI.data.fxEntranceHue.huevalue, &def->property_structs.client_volume.fx_volume_properties->pcEntranceParams);
						}
						else
						{
							wleAEGlobalVolumeUI.data.pFXButtonEntrance = ui_FXButtonCreate(0, 0, GET_REF(hInfo), &wleAEGlobalVolumeUI.data.fxEntranceHue.huevalue, &def->property_structs.client_volume.fx_volume_properties->pcEntranceParams);
							ui_WidgetSetPositionEx(UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonEntrance), 15, 0, 0, 0, UITopLeft);
							ui_FXButtonSetChangedCallback(wleAEGlobalVolumeUI.data.pFXButtonEntrance, trackerNotifyOnFXChanged, trackerFromTrackerHandle(edObj->obj));
							ui_FXButtonSetChangedCallbackShort(wleAEGlobalVolumeUI.data.pFXButtonEntrance, wleAEHueChanged, &wleAEGlobalVolumeUI.data.fxEntranceHue);
							ui_FXButtonSetStopCallback(wleAEGlobalVolumeUI.data.pFXButtonEntrance, trackerStopOnFXChanged, trackerFromTrackerHandle(edObj->obj));
						}
						ui_RebuildableTreeAddWidget(wleAEGlobalVolumeUI.autoWidget->root, UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonEntrance), NULL, true, "FX Button Entrance", NULL);
					}
					REMOVE_HANDLE(hInfo);
				}
				else if (wleAEGlobalVolumeUI.data.pFXButtonEntrance)
				{
					//It's in the tree... The system will delete it
					wleAEGlobalVolumeUI.data.pFXButtonEntrance = NULL;
				}
				wleAEDictionaryAddWidget(wleAEGlobalVolumeUI.autoWidget, "Exit", "FX that plays on an entity as it exits the volume.", "fxExit", &wleAEGlobalVolumeUI.data.fxExit);
				if (wleAEGlobalVolumeUI.data.fxExit.refvalue && wleAEGlobalVolumeUI.data.fxExit.refvalue[0])
				{
					// FX Parameters.
					REF_TO(DynFxInfo) hInfo;
					// Get the info for this FX.
					SET_HANDLE_FROM_STRING(hDynFxInfoDict, wleAEGlobalVolumeUI.data.fxExit.refvalue, hInfo);
					if (GET_REF(hInfo))
					{
						if ((wleAEGlobalVolumeUI.data.pFXButtonExit) &&
							(ui_WidgetGroupRemove(&wleAEGlobalVolumeUI.autoWidget->old_root->groupWidget->children, UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonExit))))	//This line prevent the button from being destroyed if it's going to be reused. Without this, refreshing the UI destroys the window, so it never opens
						{
							ui_FXButtonUpdate(wleAEGlobalVolumeUI.data.pFXButtonExit, GET_REF(hInfo), &wleAEGlobalVolumeUI.data.fxExitHue.huevalue, &def->property_structs.client_volume.fx_volume_properties->pcExitParams);
						}
						else
						{
							wleAEGlobalVolumeUI.data.pFXButtonExit = ui_FXButtonCreate(0, 0, GET_REF(hInfo), &wleAEGlobalVolumeUI.data.fxExitHue.huevalue, &def->property_structs.client_volume.fx_volume_properties->pcExitParams);
							ui_WidgetSetPositionEx(UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonExit), 15, 0, 0, 0, UITopLeft);
							ui_FXButtonSetChangedCallback(wleAEGlobalVolumeUI.data.pFXButtonExit, trackerNotifyOnFXChanged, trackerFromTrackerHandle(edObj->obj));
							ui_FXButtonSetChangedCallbackShort(wleAEGlobalVolumeUI.data.pFXButtonExit, wleAEHueChanged, &wleAEGlobalVolumeUI.data.fxExitHue);
							ui_FXButtonSetStopCallback(wleAEGlobalVolumeUI.data.pFXButtonExit, trackerStopOnFXChanged, trackerFromTrackerHandle(edObj->obj));
						}
						ui_RebuildableTreeAddWidget(wleAEGlobalVolumeUI.autoWidget->root, UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonExit), NULL, true, "FX Button Exit", NULL);
					}
					REMOVE_HANDLE(hInfo);
				}
				else if (wleAEGlobalVolumeUI.data.pFXButtonExit)
				{
					//It's in the tree... The system will delete it
					wleAEGlobalVolumeUI.data.pFXButtonExit = NULL;
				}
				wleAEDictionaryAddWidget(wleAEGlobalVolumeUI.autoWidget, "Maintained", "FX that is maintained on any entity while it is inside this volume.", "fxMaintained", &wleAEGlobalVolumeUI.data.fxMaintained);
				if (wleAEGlobalVolumeUI.data.fxMaintained.refvalue && wleAEGlobalVolumeUI.data.fxMaintained.refvalue[0])
				{
					// FX Parameters.
					REF_TO(DynFxInfo) hInfo;
					// Get the info for this FX.
					SET_HANDLE_FROM_STRING(hDynFxInfoDict, wleAEGlobalVolumeUI.data.fxMaintained.refvalue, hInfo);
					if (GET_REF(hInfo))
					{
						if ((wleAEGlobalVolumeUI.data.pFXButtonMaintained) &&
							(ui_WidgetGroupRemove(&wleAEGlobalVolumeUI.autoWidget->old_root->groupWidget->children, UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonMaintained))))	//This line prevent the button from being destroyed if it's going to be reused. Without this, refreshing the UI destroys the window, so it never opens
						{
							ui_FXButtonUpdate(wleAEGlobalVolumeUI.data.pFXButtonMaintained, GET_REF(hInfo), &wleAEGlobalVolumeUI.data.fxMaintainedHue.huevalue, &def->property_structs.client_volume.fx_volume_properties->pcMaintainedParams);
						}
						else
						{
							wleAEGlobalVolumeUI.data.pFXButtonMaintained = ui_FXButtonCreate(0, 0, GET_REF(hInfo), &wleAEGlobalVolumeUI.data.fxMaintainedHue.huevalue, &def->property_structs.client_volume.fx_volume_properties->pcMaintainedParams);
							ui_WidgetSetPositionEx(UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonMaintained), 15, 0, 0, 0, UITopLeft);
							ui_FXButtonSetChangedCallback(wleAEGlobalVolumeUI.data.pFXButtonMaintained, trackerNotifyOnFXChanged, trackerFromTrackerHandle(edObj->obj));
							ui_FXButtonSetChangedCallbackShort(wleAEGlobalVolumeUI.data.pFXButtonMaintained, wleAEHueChanged, &wleAEGlobalVolumeUI.data.fxMaintainedHue);
							ui_FXButtonSetStopCallback(wleAEGlobalVolumeUI.data.pFXButtonMaintained, trackerStopOnFXChanged, trackerFromTrackerHandle(edObj->obj));
						}
						ui_RebuildableTreeAddWidget(wleAEGlobalVolumeUI.autoWidget->root, UI_WIDGET(wleAEGlobalVolumeUI.data.pFXButtonMaintained), NULL, true, "FX Button Maintained", NULL);
					}
					REMOVE_HANDLE(hInfo);
				}
				else if (wleAEGlobalVolumeUI.data.pFXButtonMaintained)
				{
					//It's in the tree... The system will delete it
					wleAEGlobalVolumeUI.data.pFXButtonMaintained = NULL;
				}
			}

			// Event
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Send Events", "If true, events are sent for the volume.", "isEvent", &wleAEGlobalVolumeUI.data.isEvent);
			if (wleAEGlobalVolumeUI.data.isEvent.boolvalue)
			{
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Enter Condition", "Condition under which an entry event is sent when an entity enters the volume", "eventEnterCondition", &wleAEGlobalVolumeUI.data.eventEnteredCondition);
				wleAEExpressionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Exit Condition", "Condition under which an exit event is sent when an entity exits the volume.", "eventExitCondition", &wleAEGlobalVolumeUI.data.eventExitedCondition);
				wleAEGameActionAddWidget(wleAEGlobalVolumeUI.autoWidget, "Player First Enter Action", "Game Action to do when a player enters the region for the first time.", "FirstEnteredAction", &wleAEGlobalVolumeUI.data.eventFirstEnteredAction);
			}

			// water
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Water", "Marks this as a water volume.", "isWater", &wleAEGlobalVolumeUI.data.isWater);
			if (wleAEGlobalVolumeUI.data.isWater.boolvalue)
			{
				wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Type", "Water definition to use.", "waterDef", &wleAEGlobalVolumeUI.data.waterDef);
				wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Condition", "Water trigger condition to use, if any.", "waterCond", &wleAEGlobalVolumeUI.data.waterCond);
			}

			// Simplygon Clustering
			if (simplygonGetEnabled())
			{
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Simplygon Cluster Sector", "If true, this sets settings for Simplygon clustering.", "isCluster", &wleAEGlobalVolumeUI.data.isCluster);
				if (wleAEGlobalVolumeUI.data.isCluster.boolvalue)
				{
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Target cluster LOD", "This indicates the resolution options should only affect a particular level of the cluster. It will default to unspecified. Use a targeted setting to change the, say, texture resolution, for a particular LOD of a cell.", "ClusterTargetLOD", &wleAEGlobalVolumeUI.data.targetLOD);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Cluster min level", "We will not make LODs for clusters until this world cell LOD level. The size of a world cell block, 256 feet, will be the default. If the min level is set to 512 feet (2x2x2 blocks), then we will drop visibility distance level zero clusters, and show clusters for only 512 feet. This will prevent wasted data for non-playable areas where the camera can’t get close.", "minLevel", &wleAEGlobalVolumeUI.data.minLevel);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Cluster max LOD level", "We will make this many (hierarchical) levels of clusters. A value of one means just make the clusters at the min level.", "maxLODLevel", &wleAEGlobalVolumeUI.data.maxLODLevel);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Texture Height", "The resolution of the remeshed cluster’s textures. 512 will be the default height.", "textureHeight", &wleAEGlobalVolumeUI.data.textureHeight);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Texture Width", "The resolution of the remeshed cluster’s textures. 512 will be the default width.", "textureWidth", &wleAEGlobalVolumeUI.data.textureWidth);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Texture Supersample", "The amount of supersampling we use to make the remesh textures. 4x should be the default (zero will be interpreted as the default).", "textureSupersample", &wleAEGlobalVolumeUI.data.textureSupersample);
					wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Geometry resolution", "The precision of the remeshed cluster’s geometry. 256 will be the default.", "geometryResolution", &wleAEGlobalVolumeUI.data.geometryResolution);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Include Normal Map Texture", "Include a normal map for the cluster. Default is off.", "includeNormal", &wleAEGlobalVolumeUI.data.includeNormal);
					wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Include Specular Map Texture", "Include a specular map for the cluster. Default is off.", "includeSpecular", &wleAEGlobalVolumeUI.data.includeSpecular);
				}
			}

			// indoor
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Indoor", "Marks this as an indoor volume.", "isIndoor", &wleAEGlobalVolumeUI.data.isIndoor);
			if (wleAEGlobalVolumeUI.data.isIndoor.boolvalue)
			{
				wleAEHSVAddWidget(wleAEGlobalVolumeUI.autoWidget, "Ambient", "Ambient color to use in this indoor volume.", "indoorAmbient", &wleAEGlobalVolumeUI.data.indoorAmbient);
				wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Light Range", "Light range to use in this indoor volume.", "indoorLightRange", &wleAEGlobalVolumeUI.data.indoorLightRange, 0, 10, 0.2f);
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "See Outdoors", "Check this if this indoor volume or room is connected to the outside.", "indoorSeeOutdoors", &wleAEGlobalVolumeUI.data.indoorSeeOutdoors);
			}

			// civilian
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Civilian", "Enables the civilian volume options.", "isCivilian", &wleAEGlobalVolumeUI.data.isCivilian);
			if (wleAEGlobalVolumeUI.data.isCivilian.boolvalue)
			{
				// todo: some of these options are exclusive to each other and should be disabled appropriately 
				//		this should just for designer clarity
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Disable Sidewalks", 
											"Disables sidewalks in the volume. Used for sidewalk preprocessing only.", 
											"disableSidewalks", &wleAEGlobalVolumeUI.data.civDisablesSidewalk);
				
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Disable Roads", 
											"Disables roads in the volume. Used for road preprocessing only.", 
											"disablesCivilian", &wleAEGlobalVolumeUI.data.civDisablesRoad);

				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Forced Sidewalk", 
											"Forces this volume to be a sidewalk. Used for sidewalk preprocessing only.", 
											"forceSidewalk", &wleAEGlobalVolumeUI.data.civForcedSidewalk);
				
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Forced Crosswalk", 
											"Forces this volume to be a crosswalk."
											" Used for sidewalk preprocessing only.", 
											"forceCrosswalk", &wleAEGlobalVolumeUI.data.civForcedCrosswalk);

				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Forced Road", 
											"Forces this volume to be a road. Used for sidewalk preprocessing only.", 
											"forceRoad", &wleAEGlobalVolumeUI.data.civForcedRoad);
				
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Road Has Median", 
											"The forced road has a median. Automatically detects median width.", 
											"forceRoadMedian", &wleAEGlobalVolumeUI.data.civForcedRoadHasMedian);
				
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Forced Intersection", 
											"Disables roads from entering this area, but will automatically"
											" connect any roads that lead to this area.", 
											"forcedIntersection", &wleAEGlobalVolumeUI.data.civForcedIntersection);
				
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Forced As-Is", 
											"If true, this volume will not be clipped by intersecting objects."
											"Note: Only applies to sidewalks, currently.", 
											"forcedAsIs", &wleAEGlobalVolumeUI.data.civForcedAsIs);
				
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Pedestrian Wander Area", 
											"If set, pedestrians will use this area to randomly wander in.",
											"PedestrianWanderArea", 
											&wleAEGlobalVolumeUI.data.civPedestrianWanderArea);
								
				wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Leg Def", 
											"The leg definition that is to be matched to the LegDef"
											" in the civilian map def file.",  
											"LegDefinition", &wleAEGlobalVolumeUI.data.civLegDef );

				for(civ_index=0; civ_index<MAX_CRITTER_SETS; ++civ_index)
				{
					char buf[256];
										
					sprintf(buf, "Civ Critter Def #%d", civ_index+1);
					wleAETextAddWidget(wleAEGlobalVolumeUI.autoWidget, buf, "The critter def name for the civilian.", "civCritterDef", &wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterName);

					if (wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterName.is_specified)
					{
						wleAEFloatAddWidget(wleAEGlobalVolumeUI.autoWidget, "Spawn Weight", "The weighting that is given to this def to spawn. The percentage chance is relative to all other weights.", "civSpawnChance", &wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterChance, 1, 100, 5);
						wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Restricted To Volume", "If checked, the civilian will attempt to stay within this volume.", "restrictedToVolume", &wleAEGlobalVolumeUI.data.civCritters[civ_index]->restrictedToVolume);
						wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Is Car", "If this civilian critter def is a car.", "civIsCar", &wleAEGlobalVolumeUI.data.civCritters[civ_index]->isCar);
					}
					else if (!wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterName.diff && 
								!wleAEGlobalVolumeUI.data.civCritters[civ_index]->critterName.spec_diff)
					{
						// Stop showing controls once we run into one that is blank and not different between selections
						break;
					}
				}
			}

			// Debris Field exclusion
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Debris Field Excluder", "Excludes objects placed in debris fields.  The debris field and the volume must both be a child of the same parent marked as a debris field container", "isDebrisFieldExcluder", &wleAEGlobalVolumeUI.data.isDebrisFieldExcluder);

			// Terrain exclusion
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Terrain Excluder", "Excludes other volumes when painted on terrain.", "isExcluder", &wleAEGlobalVolumeUI.data.isExcluder);
			wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Relation to Terrain", "Keep this volume always above or below terrain.", "exclusionType", &wleAEGlobalVolumeUI.data.exclusionType);
			wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Collision Type", "Keep this volume from colliding with no other volumes, with only non-path volumes, or with all volumes.", "exclusionCollisionType", &wleAEGlobalVolumeUI.data.exclusionCollisionType);
			wleAEComboAddWidget(wleAEGlobalVolumeUI.autoWidget, "Platform", "Place other objects on top of or in this volume.", "exclusionPlatformType", &wleAEGlobalVolumeUI.data.exclusionPlatformType);
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Challenge Only", "If this is a platform, only place challenges on it.", "exclusionChallengesOnly", &wleAEGlobalVolumeUI.data.exclusionChallengesOnly);

			// UGC Room Footprint
			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "UGC Room Footprint", "The UGC interior editor uses footprint volumes to display the room footprint in the editor.", "isUGCRoomFootprint", &wleAEGlobalVolumeUI.data.isUGCRoomFootprint);

			wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Mastermind", 
								"Gives the volume Mastermind properties that are used for the Mastermind system",
								"hasMastermindProperties", &wleAEGlobalVolumeUI.data.hasMastermindProperties);
			if (wleAEGlobalVolumeUI.data.hasMastermindProperties.boolvalue)
			{
				wleAEBoolAddWidget(wleAEGlobalVolumeUI.autoWidget, "Safe Room", 
								"Marks the room as a Mastermind Safe Room upon entry, until the room is first exited.",
								"mastermindRoomIsSafe", &wleAEGlobalVolumeUI.data.mastermindRoomIsSafe);
			}
		}
	}

	ui_RebuildableTreeDoneBuilding(wleAEGlobalVolumeUI.autoWidget);
	emPanelSetHeight(wleAEGlobalVolumeUI.panel, elUIGetEndY(wleAEGlobalVolumeUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalVolumeUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalVolumeUI.scrollArea->widget.children[0]->children) + 5;
	emPanelSetActive(wleAEGlobalVolumeUI.panel, panelActive);

	return (hasProps ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);
}

void wleAEVolumeCreate(EMPanel *panel)
{
	WleCriterion *volumeTypeCrit = NULL;
	int i;
	int var_index, civ_index;

	if (wleAEGlobalVolumeUI.autoWidget)
		return;

	resSetDictionaryEditMode(g_ContactDictionary, true);
	resSetDictionaryEditMode(g_MissionDictionary, true);
	resSetDictionaryEditMode(g_hItemDict, true);

	wleAEGlobalVolumeUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalVolumeUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalVolumeUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	wleAEGlobalVolumeUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalVolumeUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalVolumeUI.scrollArea->widget.sb->alwaysScrollX = false;
	emPanelAddChild(panel, wleAEGlobalVolumeUI.scrollArea, false);

	// set parameter settings
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.volumeStyle, NULL);
	wleAEGlobalVolumeUI.data.volumeStyle.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.volumeStyle.update_func = wleAEVolumeVolumeStyleUpdate;
	wleAEGlobalVolumeUI.data.volumeStyle.apply_func = wleAEVolumeVolumeStyleApply;
	eaPush(&wleAEGlobalVolumeUI.data.volumeStyle.available_values, "No");
	eaPush(&wleAEGlobalVolumeUI.data.volumeStyle.available_values, "Yes");
	eaPush(&wleAEGlobalVolumeUI.data.volumeStyle.available_values, "SubVolume");
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.volumeShape, NULL);
	wleAEGlobalVolumeUI.data.volumeShape.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.volumeShape.update_func = wleAEVolumeVolumeStyleUpdate;
	wleAEGlobalVolumeUI.data.volumeShape.apply_func = wleAEVolumeVolumeStyleApply;
	eaPush(&wleAEGlobalVolumeUI.data.volumeShape.available_values, "Box");
	eaPush(&wleAEGlobalVolumeUI.data.volumeShape.available_values, "Sphere");
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.volumeRadius, NULL);
	wleAEGlobalVolumeUI.data.volumeRadius.precision = 4;
	wleAEGlobalVolumeUI.data.volumeRadius.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.volumeRadius.update_func = wleAEVolumeRadiusUpdate;
	wleAEGlobalVolumeUI.data.volumeRadius.apply_func = wleAEVolumeRadiusApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.volumeMin, NULL);
	wleAEGlobalVolumeUI.data.volumeMin.precision = 2;
	wleAEGlobalVolumeUI.data.volumeMin.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.volumeMin.update_func = wleAEVolumeBoundsUpdate;
	wleAEGlobalVolumeUI.data.volumeMin.apply_func = wleAEVolumeBoundsApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.volumeMax, NULL);
	wleAEGlobalVolumeUI.data.volumeMax.precision = 2;
	wleAEGlobalVolumeUI.data.volumeMax.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.volumeMax.update_func = wleAEVolumeBoundsUpdate;
	wleAEGlobalVolumeUI.data.volumeMax.apply_func = wleAEVolumeBoundsApply;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isSkyFade, NULL);
	wleAEGlobalVolumeUI.data.isSkyFade.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isSkyFade.update_data = "SkyFade";
	wleAEGlobalVolumeUI.data.isSkyFade.apply_func = wleAEVolumeIsSkyFadeApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.skyFadeIsPositional, NULL);
	wleAEGlobalVolumeUI.data.skyFadeIsPositional.update_func = wleAEVolumeSkyFadeIsPositionalUpdate;
	wleAEGlobalVolumeUI.data.skyFadeIsPositional.apply_func = wleAEVolumeSkyFadeIsPositionalApply;
	wleAEGlobalVolumeUI.data.skyFadeIsPositional.left_pad += 20;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.skyPercent, NULL);
	wleAEGlobalVolumeUI.data.skyPercent.update_func = wleAEVolumeSkyPercentUpdate;
	wleAEGlobalVolumeUI.data.skyPercent.apply_func = wleAEVolumeSkyPercentApply;
	wleAEGlobalVolumeUI.data.skyPercent.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.skyPercent.precision = 2;
	wleAEGlobalVolumeUI.data.skyPercent.left_pad += 20;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.skyFadeInRate, NULL);
	wleAEGlobalVolumeUI.data.skyFadeInRate.update_func = wleAEVolumeSkyFadeInRateUpdate;
	wleAEGlobalVolumeUI.data.skyFadeInRate.apply_func = wleAEVolumeSkyFadeInRateApply;
	wleAEGlobalVolumeUI.data.skyFadeInRate.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.skyFadeInRate.precision = 2;
	wleAEGlobalVolumeUI.data.skyFadeInRate.left_pad += 20;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.skyFadeOutRate, NULL);
	wleAEGlobalVolumeUI.data.skyFadeOutRate.update_func = wleAEVolumeSkyFadeOutRateUpdate;
	wleAEGlobalVolumeUI.data.skyFadeOutRate.apply_func = wleAEVolumeSkyFadeOutRateApply;
	wleAEGlobalVolumeUI.data.skyFadeOutRate.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.skyFadeOutRate.precision = 2;
	wleAEGlobalVolumeUI.data.skyFadeOutRate.left_pad += 20;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isOccluder, NULL);
	wleAEGlobalVolumeUI.data.isOccluder.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isOccluder.update_data = "Occluder";
	wleAEGlobalVolumeUI.data.isOccluder.apply_func = wleAEVolumeIsOccluderApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.occluderNegX, NULL);
	wleAEGlobalVolumeUI.data.occluderNegX.left_pad += 20;
	wleAEGlobalVolumeUI.data.occluderNegX.update_func = wleAEVolumeOccluderFaceUpdate;
	wleAEGlobalVolumeUI.data.occluderNegX.apply_func = wleAEVolumeOccluderFaceApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.occluderPosX, NULL);
	wleAEGlobalVolumeUI.data.occluderPosX.left_pad += 20;
	wleAEGlobalVolumeUI.data.occluderPosX.update_func = wleAEVolumeOccluderFaceUpdate;
	wleAEGlobalVolumeUI.data.occluderPosX.apply_func = wleAEVolumeOccluderFaceApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.occluderNegY, NULL);
	wleAEGlobalVolumeUI.data.occluderNegY.left_pad += 20;
	wleAEGlobalVolumeUI.data.occluderNegY.update_func = wleAEVolumeOccluderFaceUpdate;
	wleAEGlobalVolumeUI.data.occluderNegY.apply_func = wleAEVolumeOccluderFaceApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.occluderPosY, NULL);
	wleAEGlobalVolumeUI.data.occluderPosY.left_pad += 20;
	wleAEGlobalVolumeUI.data.occluderPosY.update_func = wleAEVolumeOccluderFaceUpdate;
	wleAEGlobalVolumeUI.data.occluderPosY.apply_func = wleAEVolumeOccluderFaceApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.occluderNegZ, NULL);
	wleAEGlobalVolumeUI.data.occluderNegZ.left_pad += 20;
	wleAEGlobalVolumeUI.data.occluderNegZ.update_func = wleAEVolumeOccluderFaceUpdate;
	wleAEGlobalVolumeUI.data.occluderNegZ.apply_func = wleAEVolumeOccluderFaceApply;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.occluderPosZ, NULL);
	wleAEGlobalVolumeUI.data.occluderPosZ.left_pad += 20;
	wleAEGlobalVolumeUI.data.occluderPosZ.update_func = wleAEVolumeOccluderFaceUpdate;
	wleAEGlobalVolumeUI.data.occluderPosZ.apply_func = wleAEVolumeOccluderFaceApply;
	
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.playable, NULL);
	wleAEGlobalVolumeUI.data.playable.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.playable.update_data = "Playable";
	wleAEGlobalVolumeUI.data.playable.apply_func = wleAEVolumeTypeApply;
	wleAEGlobalVolumeUI.data.playable.apply_data = "Playable";

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.duelDisable, NULL);
	wleAEGlobalVolumeUI.data.duelDisable.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.duelDisable.update_data = "DuelDisable";
	wleAEGlobalVolumeUI.data.duelDisable.apply_func = wleAEVolumeTypeApply;
	wleAEGlobalVolumeUI.data.duelDisable.apply_data = "DuelDisable";

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.duelEnable, NULL);
	wleAEGlobalVolumeUI.data.duelEnable.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.duelEnable.update_data = "DuelEnable";
	wleAEGlobalVolumeUI.data.duelEnable.apply_func = wleAEVolumeTypeApply;
	wleAEGlobalVolumeUI.data.duelEnable.apply_data = "DuelEnable";

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.ignoreSound, NULL);
	wleAEGlobalVolumeUI.data.ignoreSound.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.ignoreSound.update_data = "IgnoreSound";
	wleAEGlobalVolumeUI.data.ignoreSound.apply_func = wleAEVolumeTypeApply;
	wleAEGlobalVolumeUI.data.ignoreSound.apply_data = "IgnoreSound";

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isAction, NULL);
	wleAEGlobalVolumeUI.data.isAction.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isAction.update_data = "Action";
	wleAEGlobalVolumeUI.data.isAction.apply_func = wleAEVolumeIsActionApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.enteredCondition, server_volume.action_volume_properties, parse_WorldActionVolumeProperties, "EnteredActionCondBlock");
	wleAEGlobalVolumeUI.data.enteredCondition.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.enteredCondition.left_pad += 20;
	wleAEGlobalVolumeUI.data.enteredCondition.context = exprContextCreate();
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.enteredAction, server_volume.action_volume_properties, parse_WorldActionVolumeProperties, "EnteredActionBlock");
	wleAEGlobalVolumeUI.data.enteredAction.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.enteredAction.left_pad += 20;
	wleAEGlobalVolumeUI.data.enteredAction.context = exprContextCreate();
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.exitedCondition, server_volume.action_volume_properties, parse_WorldActionVolumeProperties, "ExitedActionCondBlock");
	wleAEGlobalVolumeUI.data.exitedCondition.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.exitedCondition.left_pad += 20;
	wleAEGlobalVolumeUI.data.exitedCondition.context = exprContextCreate();
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.exitedAction, server_volume.action_volume_properties, parse_WorldActionVolumeProperties, "ExitedActionBlock");
	wleAEGlobalVolumeUI.data.exitedAction.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.exitedAction.left_pad += 20;
	wleAEGlobalVolumeUI.data.exitedAction.context = exprContextCreate();

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isPower, NULL);
	wleAEGlobalVolumeUI.data.isPower.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isPower.update_data = "Power";
	wleAEGlobalVolumeUI.data.isPower.apply_func = wleAEVolumeIsPowerApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.powerDef, server_volume.power_volume_properties, parse_WorldPowerVolumeProperties, "Power");
	wleAEGlobalVolumeUI.data.powerDef.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.powerDef.left_pad += 20;
	wleAEGlobalVolumeUI.data.powerDef.dictionary = "PowerDef";
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.powerStrength, NULL);
	wleAEGlobalVolumeUI.data.powerStrength.update_func = wleAEVolumePowerStrengthUpdate;
	wleAEGlobalVolumeUI.data.powerStrength.apply_func = wleAEVolumePowerStrengthApply;
	wleAEGlobalVolumeUI.data.powerStrength.entry_width = WLE_AE_VOLUME_STRENGTH_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.powerStrength.left_pad += 20;
	for (i = WorldPowerVolumeStrength_Harmless; i < WorldPowerVolumeStrength_Count; i++)
		eaPush(&wleAEGlobalVolumeUI.data.powerStrength.available_values, (char*) StaticDefineIntRevLookup(WorldPowerVolumeStrengthEnum, i));
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.powerLevel, server_volume.power_volume_properties, parse_WorldPowerVolumeProperties, "Level");
	wleAEGlobalVolumeUI.data.powerLevel.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.powerLevel.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.powerTime, server_volume.power_volume_properties, parse_WorldPowerVolumeProperties, "RepeatTime");
	wleAEGlobalVolumeUI.data.powerTime.entry_width = WLE_AE_VOLUME_NUM_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.powerTime.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.powerCond, server_volume.power_volume_properties, parse_WorldPowerVolumeProperties, "TriggerCondition");
	wleAEGlobalVolumeUI.data.powerCond.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.powerCond.left_pad += 20;
	wleAEGlobalVolumeUI.data.powerCond.context = exprContextCreate();

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isWarp, NULL);
	wleAEGlobalVolumeUI.data.isWarp.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isWarp.update_data = "Warp";
	wleAEGlobalVolumeUI.data.isWarp.apply_func = wleAEVolumeIsWarpApply;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.petsDisabled, NULL);
	wleAEGlobalVolumeUI.data.petsDisabled.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.petsDisabled.update_data = "PetsDisabled";
	wleAEGlobalVolumeUI.data.petsDisabled.apply_func = wleAEVolumeTypeApply;
	wleAEGlobalVolumeUI.data.petsDisabled.apply_data = "PetsDisabled";

	wleAEGlobalVolumeUI.data.warpDest.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.warpDest.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.warpDest.left_pad = WLE_AE_VOLUME_INDENT + 20;
	wleAEGlobalVolumeUI.data.warpDest.update_func = wleAEVolumeWarpDestUpdate;
	wleAEGlobalVolumeUI.data.warpDest.apply_func = wleAEVolumeWarpDestApply;
	wleAEGlobalVolumeUI.data.warpDest.source_map_name = SAFE_MEMBER(zmapGetInfo(NULL), map_name);
	wleAEGlobalVolumeUI.data.warpDest.no_name = true;

	wleAEGlobalVolumeUI.data.warpSequenceOverride.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.warpSequenceOverride.left_pad = WLE_AE_VOLUME_INDENT + 20;
	wleAEGlobalVolumeUI.data.warpSequenceOverride.update_func = wleAEVolumePropWarpDepartOverrideUpdate;
	wleAEGlobalVolumeUI.data.warpSequenceOverride.apply_func = wleAEVolumePropWarpDepartOverrideApply;

	wleAEGlobalVolumeUI.data.warpTransition.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.warpTransition.left_pad = WLE_AE_VOLUME_INDENT + 40;
	wleAEGlobalVolumeUI.data.warpTransition.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.warpTransition.update_func = wleAEVolumePropWarpActionTransOverrideUpdate;
	wleAEGlobalVolumeUI.data.warpTransition.apply_func = wleAEVolumePropWarpActionTransOverrideApply;
	wleAEGlobalVolumeUI.data.warpTransition.dictionary = "DoorTransitionSequenceDef";

	wleAEGlobalVolumeUI.data.warpHasVariables.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.warpHasVariables.left_pad = WLE_AE_VOLUME_INDENT+20;
	wleAEGlobalVolumeUI.data.warpHasVariables.update_func = wleAEVolumePropWarpHasVariablesUpdate;
	wleAEGlobalVolumeUI.data.warpHasVariables.apply_func = wleAEVolumePropWarpHasVariablesApply;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.warpCond, server_volume.warp_volume_properties, parse_WorldWarpVolumeProperties, "WarpCondition");
	wleAEGlobalVolumeUI.data.warpCond.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.warpCond.left_pad += 20;
	wleAEGlobalVolumeUI.data.warpCond.context = exprContextCreate();

	for(var_index=0; var_index<MAX_VARIABLES; ++var_index)
	{
		WleAEVolumeWarpVarPropUI *new_var_entry = calloc(1, sizeof(WleAEVolumeWarpVarPropUI));
		eaPush(&wleAEGlobalVolumeUI.data.warpVariables, new_var_entry);

		new_var_entry->var.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
		new_var_entry->var.left_pad = WLE_AE_VOLUME_INDENT+20;
		new_var_entry->var.entry_width = 1.0;
		new_var_entry->var.update_func = wleAEVolumePropWarpVarUpdate;
		new_var_entry->var.apply_func = wleAEVolumePropWarpVarApply;
		new_var_entry->var.index = var_index;
		new_var_entry->var.can_unspecify = true;
		new_var_entry->var.source_map_name = SAFE_MEMBER(zmapGetInfo(NULL), map_name);
		new_var_entry->var.dest_map_name = NULL;
	}

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isLandmark, NULL);
	wleAEGlobalVolumeUI.data.isLandmark.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isLandmark.update_data = "Landmark";
	wleAEGlobalVolumeUI.data.isLandmark.apply_func = wleAEVolumeIsLandmarkApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.landmarkIcon, server_volume.landmark_volume_properties, parse_WorldLandmarkVolumeProperties, "Icon");
	wleAEGlobalVolumeUI.data.landmarkIcon.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.landmarkDispName, server_volume.landmark_volume_properties, parse_WorldLandmarkVolumeProperties, "DisplayName");
	wleAEGlobalVolumeUI.data.landmarkDispName.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.landmarkDispName.left_pad += 20;
	wleAEGlobalVolumeUI.data.landmarkDispName.source_key = "landmarkDispName";
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.landmarkHideUnlessRevealed, server_volume.landmark_volume_properties, parse_WorldLandmarkVolumeProperties, "HideUnlessRevealed");
	wleAEGlobalVolumeUI.data.landmarkHideUnlessRevealed.left_pad += 20;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isNeighborhood, NULL);
	wleAEGlobalVolumeUI.data.isNeighborhood.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isNeighborhood.update_data = "Neighborhood";
	wleAEGlobalVolumeUI.data.isNeighborhood.apply_func = wleAEVolumeIsNeighborhoodApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.neighborhoodDispName, server_volume.neighborhood_volume_properties, parse_WorldNeighborhoodVolumeProperties, "DisplayName");
	wleAEGlobalVolumeUI.data.neighborhoodDispName.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.neighborhoodDispName.left_pad += 20;
	wleAEGlobalVolumeUI.data.neighborhoodDispName.source_key = "neighborhoodDispName";
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.neighborhoodSound, server_volume.neighborhood_volume_properties, parse_WorldNeighborhoodVolumeProperties, "SoundEffect");
	wleAEGlobalVolumeUI.data.neighborhoodSound.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.neighborhoodSound.left_pad += 20;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isLevelOverride, NULL);
	wleAEGlobalVolumeUI.data.isLevelOverride.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isLevelOverride.update_data = "LevelOverride";
	wleAEGlobalVolumeUI.data.isLevelOverride.apply_func = wleAEVolumeIsLevelOverrideApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.levelOverride, server_volume.map_level_volume_properties, parse_WorldMapLevelOverrideVolumeProperties, "Level");
	wleAEGlobalVolumeUI.data.levelOverride.left_pad += 20;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.aiAvoid, NULL);
	wleAEGlobalVolumeUI.data.aiAvoid.update_func = wleAEVolumeAIAvoidUpdate;
	wleAEGlobalVolumeUI.data.aiAvoid.apply_func = wleAEVolumeAIAvoidApply;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.nodynconn, NULL);
	wleAEGlobalVolumeUI.data.nodynconn.update_func = wleAEVolumeNoDynConnUpdate;
	wleAEGlobalVolumeUI.data.nodynconn.apply_func = wleAEVolumeNoDynConnApply;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isEvent, NULL);
	wleAEGlobalVolumeUI.data.isEvent.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isEvent.update_data = "Event";
	wleAEGlobalVolumeUI.data.isEvent.apply_func = wleAEVolumeIsEventApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.eventFirstEnteredAction, server_volume.event_volume_properties, parse_WorldGameActionBlock, "FirstEnteredAction");
	wleAEGlobalVolumeUI.data.eventFirstEnteredAction.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.eventFirstEnteredAction.left_pad += 20;
	wleAEGlobalVolumeUI.data.eventFirstEnteredAction.action_block = StructCreate(parse_WorldGameActionBlock);
	wleAEGlobalVolumeUI.data.eventFirstEnteredAction.update_func = wleAEVolumeEventFirstEnterGameActionUpdate;
	wleAEGlobalVolumeUI.data.eventFirstEnteredAction.apply_func = wleAEVolumeEventFirstEnterGameActionApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.eventEnteredCondition, server_volume.event_volume_properties, parse_WorldEventVolumeProperties, "EnterEventCondition");
	wleAEGlobalVolumeUI.data.eventEnteredCondition.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.eventEnteredCondition.left_pad += 20;
	wleAEGlobalVolumeUI.data.eventEnteredCondition.context = exprContextCreate();
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.eventExitedCondition, server_volume.event_volume_properties, parse_WorldEventVolumeProperties, "ExitEventCondition");
	wleAEGlobalVolumeUI.data.eventExitedCondition.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.eventExitedCondition.left_pad += 20;
	wleAEGlobalVolumeUI.data.eventExitedCondition.context = exprContextCreate();

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isWater, NULL);
	wleAEGlobalVolumeUI.data.isWater.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isWater.update_data = "Water";
	wleAEGlobalVolumeUI.data.isWater.apply_func = wleAEVolumeIsWaterApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.waterDef, client_volume.water_volume_properties, parse_WorldWaterVolumeProperties, "WaterDef");
	wleAEGlobalVolumeUI.data.waterDef.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.waterDef.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.waterCond, client_volume.water_volume_properties, parse_WorldWaterVolumeProperties, "Condition");
	wleAEGlobalVolumeUI.data.waterCond.can_unspecify = true;
	wleAEGlobalVolumeUI.data.waterCond.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.waterCond.left_pad += 20;

	//Cluster (for Simplygon)

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isCluster, NULL);
	wleAEGlobalVolumeUI.data.isCluster.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isCluster.update_data = "Cluster";
	wleAEGlobalVolumeUI.data.isCluster.apply_func = wleAEVolumeClusterApply;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.targetLOD, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "TargetLOD");
	wleAEGlobalVolumeUI.data.targetLOD.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.targetLOD.update_data = "TargetLOD";
	wleAEGlobalVolumeUI.data.targetLOD.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.targetLOD.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.targetLOD.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.targetLOD.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD0));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD1));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD2));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD3));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD4));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD5));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD6));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD7));
	eaPush(&wleAEGlobalVolumeUI.data.targetLOD.available_values, (char*)StaticDefineIntRevLookup(ClusterTargetLODEnum, ClusterTargetLOD8));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.minLevel, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "MinLevel");
	wleAEGlobalVolumeUI.data.minLevel.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.minLevel.update_data = "MinLevel";
	wleAEGlobalVolumeUI.data.minLevel.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.minLevel.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.minLevel.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.minLevel.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.minLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMinLevelEnum, ClusterMinLevel256ft));
	eaPush(&wleAEGlobalVolumeUI.data.minLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMinLevelEnum, ClusterMinLevel512ft));
	eaPush(&wleAEGlobalVolumeUI.data.minLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMinLevelEnum, ClusterMinLevel1024ft));
	eaPush(&wleAEGlobalVolumeUI.data.minLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMinLevelEnum, ClusterMinLevel2048ft));
	eaPush(&wleAEGlobalVolumeUI.data.minLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMinLevelEnum, ClusterMinLevel4096ft));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.maxLODLevel, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "MaxLODLevel");
	wleAEGlobalVolumeUI.data.maxLODLevel.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.maxLODLevel.update_data = "MaxLODLevel";
	wleAEGlobalVolumeUI.data.maxLODLevel.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.maxLODLevel.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.maxLODLevel.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.maxLODLevel.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_Default));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_1));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_2));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_3));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_4));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_5));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_6));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_7));
	eaPush(&wleAEGlobalVolumeUI.data.maxLODLevel.available_values, (char*)StaticDefineIntRevLookup(ClusterMaxLODLevelEnum, ClusterMaxLODLevel_8));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.textureHeight, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "TextureHeight");
	wleAEGlobalVolumeUI.data.textureHeight.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.textureHeight.update_data = "TextureHeight";
	wleAEGlobalVolumeUI.data.textureHeight.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.textureHeight.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.textureHeight.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.textureHeight.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolutionDefault));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution64));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution128));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution256));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution512));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution1024));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution2048));
	eaPush(&wleAEGlobalVolumeUI.data.textureHeight.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution4096));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.textureWidth, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "TextureWidth");
	wleAEGlobalVolumeUI.data.textureWidth.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.textureWidth.update_data = "TextureWidth";
	wleAEGlobalVolumeUI.data.textureWidth.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.textureWidth.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.textureWidth.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.textureWidth.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolutionDefault));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution64));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution128));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution256));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution512));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution1024));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution2048));
	eaPush(&wleAEGlobalVolumeUI.data.textureWidth.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureResolutionEnum, ClusterTextureResolution4096));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.textureSupersample, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "TextureSupersample");
	wleAEGlobalVolumeUI.data.textureSupersample.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.textureSupersample.update_data = "TextureSupersample";
	wleAEGlobalVolumeUI.data.textureSupersample.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.textureSupersample.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.textureSupersample.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.textureSupersample.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.textureSupersample.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureSupersampleEnum, ClusterTextureSupersampleDefault));
	eaPush(&wleAEGlobalVolumeUI.data.textureSupersample.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureSupersampleEnum, ClusterTextureSupersample1));
	eaPush(&wleAEGlobalVolumeUI.data.textureSupersample.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureSupersampleEnum, ClusterTextureSupersample4x));
	eaPush(&wleAEGlobalVolumeUI.data.textureSupersample.available_values, (char*)StaticDefineIntRevLookup(ClusterTextureSupersampleEnum, ClusterTextureSupersample16x));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.geometryResolution, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "GeometryResolution");
	wleAEGlobalVolumeUI.data.geometryResolution.update_func = wleAEVolumeClusterUpdate;
	wleAEGlobalVolumeUI.data.geometryResolution.update_data = "GeometryResolution";
	wleAEGlobalVolumeUI.data.geometryResolution.apply_func = wleAEVolumeClusterApply;
	wleAEGlobalVolumeUI.data.geometryResolution.struct_fieldname = NULL;
	wleAEGlobalVolumeUI.data.geometryResolution.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.geometryResolution.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.geometryResolution.available_values, (char*)StaticDefineIntRevLookup(ClusterGeometryResolutionEnum, ClusterGeometryResolutionDefault));
	eaPush(&wleAEGlobalVolumeUI.data.geometryResolution.available_values, (char*)StaticDefineIntRevLookup(ClusterGeometryResolutionEnum, ClusterGeometryResolution64));
	eaPush(&wleAEGlobalVolumeUI.data.geometryResolution.available_values, (char*)StaticDefineIntRevLookup(ClusterGeometryResolutionEnum, ClusterGeometryResolution128));
	eaPush(&wleAEGlobalVolumeUI.data.geometryResolution.available_values, (char*)StaticDefineIntRevLookup(ClusterGeometryResolutionEnum, ClusterGeometryResolution256));
	eaPush(&wleAEGlobalVolumeUI.data.geometryResolution.available_values, (char*)StaticDefineIntRevLookup(ClusterGeometryResolutionEnum, ClusterGeometryResolution512));
	eaPush(&wleAEGlobalVolumeUI.data.geometryResolution.available_values, (char*)StaticDefineIntRevLookup(ClusterGeometryResolutionEnum, ClusterGeometryResolution1024));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.includeNormal, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "IncludeNormal");
	wleAEGlobalVolumeUI.data.includeNormal.left_pad += 20;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.includeSpecular, client_volume.cluster_volume_properties, parse_WorldClusterVolumeProperties, "IncludeSpecular");
	wleAEGlobalVolumeUI.data.includeSpecular.left_pad += 20;

	//End Cluster

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isIndoor, NULL);
	wleAEGlobalVolumeUI.data.isIndoor.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isIndoor.update_data = "Indoor";
	wleAEGlobalVolumeUI.data.isIndoor.apply_func = wleAEVolumeIsIndoorApply;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.indoorAmbient, client_volume.indoor_volume_properties, parse_WorldIndoorVolumeProperties, "AmbientHSV");
	wleAEGlobalVolumeUI.data.indoorAmbient.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.indoorLightRange, client_volume.indoor_volume_properties, parse_WorldIndoorVolumeProperties, "LightRange");
	wleAEGlobalVolumeUI.data.indoorLightRange.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.indoorSeeOutdoors, client_volume.indoor_volume_properties, parse_WorldIndoorVolumeProperties, "CanSeeOutdoors");
	wleAEGlobalVolumeUI.data.indoorSeeOutdoors.left_pad += 20;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.roomType, NULL);
	wleAEGlobalVolumeUI.data.roomType.update_func = wleAEVolumeRoomTypeUpdate;
	wleAEGlobalVolumeUI.data.roomType.apply_func = wleAEVolumeRoomTypeApply;
	wleAEGlobalVolumeUI.data.roomType.entry_width = WLE_AE_VOLUME_TEXT_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.roomDisablePhoto.left_pad = 20;
	wleAEGlobalVolumeUI.data.roomDisablePhoto.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.roomDisablePhoto.update_func = wleAEVolumeRoomDisablePhotoUpdate;
	wleAEGlobalVolumeUI.data.roomDisablePhoto.apply_func = wleAEVolumeRoomDisablePhotoApply;
	wleAEGlobalVolumeUI.data.roomOverridePhoto.left_pad = 20;
	wleAEGlobalVolumeUI.data.roomOverridePhoto.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.roomOverridePhoto.update_func = wleAEVolumeRoomOverridePhotoUpdate;
	wleAEGlobalVolumeUI.data.roomOverridePhoto.apply_func = wleAEVolumeRoomOverridePhotoApply;
	wleAEGlobalVolumeUI.data.roomOverridePhotoTexture.left_pad = 20;
	wleAEGlobalVolumeUI.data.roomOverridePhotoTexture.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.roomOverridePhotoTexture.update_func = wleAEVolumeRoomOverridePhotoTextureUpdate;
	wleAEGlobalVolumeUI.data.roomOverridePhotoTexture.apply_func = wleAEVolumeRoomOverridePhotoTextureApply;

	wleAEGlobalVolumeUI.data.roomType.disabled = !UserIsInGroup("World") && !UserIsInGroup("Software");

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.roomOccluder, "RoomOccluder");
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.roomUseModels, "RoomUseModels");
	wleAEGlobalVolumeUI.data.roomUseModels.left_pad += 20;
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.roomLimitLights, "RoomLimitLights");

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isFX, NULL);
	wleAEGlobalVolumeUI.data.isFX.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isFX.update_data = "FX";
	wleAEGlobalVolumeUI.data.isFX.apply_func = wleAEVolumeIsFXApply;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.fxFilter, NULL);
	wleAEGlobalVolumeUI.data.fxFilter.update_func = wleAEVolumeFXFilterUpdate;
	wleAEGlobalVolumeUI.data.fxFilter.apply_func = wleAEVolumeFXFilterApply;
	wleAEGlobalVolumeUI.data.fxFilter.entry_width = WLE_AE_VOLUME_STRENGTH_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.fxFilter.left_pad += 20;
	for (i = WorldFXVolumeFilter_AllEntities; i < WorldFXVolumeFilter_Count; i++)
		eaPush(&wleAEGlobalVolumeUI.data.fxFilter.available_values, (char*) StaticDefineIntRevLookup(WorldFXVolumeFilterEnum, i));

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.fxEntrance, client_volume.fx_volume_properties, parse_WorldFXVolumeProperties, "Entrance");
	wleAEGlobalVolumeUI.data.fxEntrance.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.fxEntrance.left_pad += 20;
	wleAEGlobalVolumeUI.data.fxEntrance.dictionary = "DynFxInfo";

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.fxEntranceHue, client_volume.fx_volume_properties, parse_WorldFXVolumeProperties, "EntranceHue");
	wleAEGlobalVolumeUI.data.fxEntranceHue.entry_width = 70;
	wleAEGlobalVolumeUI.data.fxEntranceHue.slider_width = 100;
	wleAEGlobalVolumeUI.data.fxEntranceHue.left_pad += 20;
	wleAEGlobalVolumeUI.data.fxEntranceHue.precision = 1;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.fxExit, client_volume.fx_volume_properties, parse_WorldFXVolumeProperties, "Exit");
	wleAEGlobalVolumeUI.data.fxExit.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.fxExit.left_pad += 20;
	wleAEGlobalVolumeUI.data.fxExit.dictionary = "DynFxInfo";

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.fxExitHue, client_volume.fx_volume_properties, parse_WorldFXVolumeProperties, "ExitHue");
	wleAEGlobalVolumeUI.data.fxExitHue.entry_width = 70;
	wleAEGlobalVolumeUI.data.fxExitHue.slider_width = 100;
	wleAEGlobalVolumeUI.data.fxExitHue.left_pad += 20;
	wleAEGlobalVolumeUI.data.fxExitHue.precision = 1;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.fxMaintained, client_volume.fx_volume_properties, parse_WorldFXVolumeProperties, "Maintained");
	wleAEGlobalVolumeUI.data.fxMaintained.entry_width = 1.0;
	wleAEGlobalVolumeUI.data.fxMaintained.left_pad += 20;
	wleAEGlobalVolumeUI.data.fxMaintained.dictionary = "DynFxInfo";

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.fxMaintainedHue, client_volume.fx_volume_properties, parse_WorldFXVolumeProperties, "MaintainedHue");
	wleAEGlobalVolumeUI.data.fxMaintainedHue.entry_width = 70;
	wleAEGlobalVolumeUI.data.fxMaintainedHue.slider_width = 100;
	wleAEGlobalVolumeUI.data.fxMaintainedHue.left_pad += 20;
	wleAEGlobalVolumeUI.data.fxMaintainedHue.precision = 1;

	// civilian
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isCivilian, NULL);
	wleAEGlobalVolumeUI.data.isCivilian.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isCivilian.update_data = "Civilian";
	wleAEGlobalVolumeUI.data.isCivilian.apply_func = wleAEVolumeIsCivilianApply;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civDisablesSidewalk, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "DisableSidewalks");
	wleAEGlobalVolumeUI.data.civDisablesSidewalk.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civDisablesSidewalk.left_pad += 20;
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civDisablesRoad, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "DisableRoads");
	wleAEGlobalVolumeUI.data.civDisablesRoad.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civDisablesRoad.left_pad += 20;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civForcedSidewalk, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "ForcedSidewalk");
	wleAEGlobalVolumeUI.data.civForcedSidewalk.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civForcedSidewalk.left_pad += 20;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civForcedCrosswalk, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "ForcedCrosswalk");
	wleAEGlobalVolumeUI.data.civForcedCrosswalk.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civForcedCrosswalk.left_pad += 20;

	
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civForcedRoad, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "ForcedRoad");
	wleAEGlobalVolumeUI.data.civForcedRoad.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civForcedRoad.left_pad += 20;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civForcedRoadHasMedian, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "ForcedRoadHasMedian");
	wleAEGlobalVolumeUI.data.civForcedRoadHasMedian.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civForcedRoadHasMedian.left_pad += 20;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civForcedIntersection, server_volume.civilian_volume_properties, parse_WorldCivilianVolumeProperties, "ForcedIntersection");
	wleAEGlobalVolumeUI.data.civForcedIntersection.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civForcedIntersection.left_pad += 20;
		
	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civForcedAsIs, 
									server_volume.civilian_volume_properties, 
									parse_WorldCivilianVolumeProperties, "ForcedAsIs");
	wleAEGlobalVolumeUI.data.civForcedAsIs.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civForcedAsIs.left_pad += 20;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civPedestrianWanderArea, 
									server_volume.civilian_volume_properties, 
									parse_WorldCivilianVolumeProperties, "PedestrianWanderArea");
	wleAEGlobalVolumeUI.data.civPedestrianWanderArea.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civPedestrianWanderArea.left_pad += 20;
	

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.civLegDef, 
								server_volume.civilian_volume_properties, 
								parse_WorldCivilianVolumeProperties, "LegDefinition");

	wleAEGlobalVolumeUI.data.civLegDef.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.civLegDef.left_pad += 20;
	wleAEGlobalVolumeUI.data.civLegDef.entry_width = WLE_AE_VOLUME_TEXT_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.civLegDef.can_unspecify = true;
	wleAEGlobalVolumeUI.data.civLegDef.available_values = (char**)gclCivilian_GetLegDefNames();
		
	
	for (civ_index = 0; civ_index < MAX_CRITTER_SETS; civ_index++)
	{	
		WleAEVolumeCivilianCritter *new_entry = calloc(1, sizeof(WleAEVolumeCivilianCritter));
		
		eaPush(&wleAEGlobalVolumeUI.data.civCritters, new_entry);

		new_entry->critterName.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
		new_entry->critterName.entry_width = 1.0f;
		new_entry->critterName.left_pad = WLE_AE_VOLUME_INDENT+20;
		new_entry->critterName.update_func = wleAEVolumeCivilianCritterNameUpdate;
		new_entry->critterName.apply_func = wleAEVolumeCivilianCritterNameApply;
		new_entry->critterName.index = civ_index;
		new_entry->critterName.can_unspecify = true;

		new_entry->critterChance.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
		new_entry->critterChance.entry_width = WLE_AE_VOLUME_TEXT_ENTRY_WIDTH;
		new_entry->critterChance.left_pad = WLE_AE_VOLUME_INDENT+40;
		new_entry->critterChance.update_func = wleAEVolumeCivilianCritterChanceUpdate;
		new_entry->critterChance.apply_func = wleAEVolumeCivilianCritterChanceApply;
		new_entry->critterChance.index = civ_index;
		
		new_entry->isCar.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
		new_entry->isCar.left_pad = WLE_AE_VOLUME_INDENT+40;
		new_entry->isCar.update_func = wleAEVolumeCivilianCritterIsCarUpdate;
		new_entry->isCar.apply_func = wleAEVolumeCivilianCritterIsCarApply;
		new_entry->isCar.index = civ_index;
		
		new_entry->restrictedToVolume.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
		new_entry->restrictedToVolume.left_pad = WLE_AE_VOLUME_INDENT+40;
		new_entry->restrictedToVolume.update_func = wleAEVolumeCivilianCritterRestrictedUpdate;
		new_entry->restrictedToVolume.apply_func = wleAEVolumeCivilianCritterRestrictedApply;
		new_entry->restrictedToVolume.index = civ_index;
	}


	// Debris Field exclusion
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isDebrisFieldExcluder, NULL);
	wleAEGlobalVolumeUI.data.isDebrisFieldExcluder.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isDebrisFieldExcluder.update_data = "DebrisFieldExclusion";
	wleAEGlobalVolumeUI.data.isDebrisFieldExcluder.apply_func = wleAEVolumeIsDebrisFieldExcluderApply;

	// Terrain exclusion
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isExcluder, NULL);
	wleAEGlobalVolumeUI.data.isExcluder.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isExcluder.update_data = "TerrainExclusion";
	wleAEGlobalVolumeUI.data.isExcluder.apply_func = wleAEVolumeIsExcluderApply;

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.exclusionType, NULL);
	wleAEGlobalVolumeUI.data.exclusionType.update_func = wleAEVolumeExclusionTypeUpdate;
	wleAEGlobalVolumeUI.data.exclusionType.apply_func = wleAEVolumeExclusionTypeApply;
	wleAEGlobalVolumeUI.data.exclusionType.entry_width = WLE_AE_VOLUME_TEXT_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.exclusionType.left_pad += 20;
	eaPush(&wleAEGlobalVolumeUI.data.exclusionType.available_values, "Anywhere");
	eaPush(&wleAEGlobalVolumeUI.data.exclusionType.available_values, "Above Terrain");
	eaPush(&wleAEGlobalVolumeUI.data.exclusionType.available_values, "Below Terrain");

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.exclusionCollisionType, NULL);
	wleAEGlobalVolumeUI.data.exclusionCollisionType.update_func = wleAEVolumeCollisionTypeUpdate;
	wleAEGlobalVolumeUI.data.exclusionCollisionType.apply_func = wleAEVolumeCollisionTypeApply;
	wleAEGlobalVolumeUI.data.exclusionCollisionType.entry_width = WLE_AE_VOLUME_TEXT_ENTRY_WIDTH;
	wleAEGlobalVolumeUI.data.exclusionCollisionType.left_pad += 20;
	DefineFillAllKeysAndValues(WorldTerrainCollisionTypeUIEnum, &wleAEGlobalVolumeUI.data.exclusionCollisionType.available_values, NULL);

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.exclusionPlatformType, NULL);
	wleAEGlobalVolumeUI.data.exclusionPlatformType.left_pad += 20;
	wleAEGlobalVolumeUI.data.exclusionPlatformType.update_func = wleAEVolumePlatformTypeUpdate;
	wleAEGlobalVolumeUI.data.exclusionPlatformType.apply_func = wleAEVolumePlatformTypeApply;
	wleAEGlobalVolumeUI.data.exclusionPlatformType.entry_width = WLE_AE_VOLUME_TEXT_ENTRY_WIDTH;
	DefineFillAllKeysAndValues(WorldPlatformTypeEnum, &wleAEGlobalVolumeUI.data.exclusionPlatformType.available_values, NULL);

	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.exclusionChallengesOnly, NULL);
	wleAEGlobalVolumeUI.data.exclusionChallengesOnly.left_pad += 20;
	wleAEGlobalVolumeUI.data.exclusionChallengesOnly.update_func = wleAEVolumeChallengesOnlyUpdate;
	wleAEGlobalVolumeUI.data.exclusionChallengesOnly.apply_func = wleAEVolumeChallengesOnlyApply;

	// UGC room footprint volume
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.isUGCRoomFootprint, NULL);
	wleAEGlobalVolumeUI.data.isUGCRoomFootprint.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.isUGCRoomFootprint.update_data = "UGCRoomFootprint";
	wleAEGlobalVolumeUI.data.isUGCRoomFootprint.apply_func = wleAEVolumeTypeApply;
	wleAEGlobalVolumeUI.data.isUGCRoomFootprint.apply_data = "UGCRoomFootprint";

	// mastermind params
	wleAEVolumeSetupParam(wleAEGlobalVolumeUI.data.hasMastermindProperties, NULL);
	wleAEGlobalVolumeUI.data.hasMastermindProperties.update_func = wleAEVolumeIsTypeUpdate;
	wleAEGlobalVolumeUI.data.hasMastermindProperties.update_data = "Mastermind";
	wleAEGlobalVolumeUI.data.hasMastermindProperties.apply_func = wleAEVolumeIsMastermindApply;

	wleAEVolumeSetupStructParam(wleAEGlobalVolumeUI.data.mastermindRoomIsSafe, 
								server_volume.mastermind_volume_properties, 
								parse_WorldMastermindVolumeProperties, "SafeRoomUntilExit");
	wleAEGlobalVolumeUI.data.mastermindRoomIsSafe.entry_align = WLE_AE_VOLUME_ALIGN_WIDTH;
	wleAEGlobalVolumeUI.data.mastermindRoomIsSafe.left_pad += 20;

	// volume type filter criterion
	volumeTypeCrit = StructCreate(parse_WleCriterion);
	volumeTypeCrit->propertyName = "Volume Type";
	eaiPush(&volumeTypeCrit->allConds, WLE_CRIT_CONTAINS);
	volumeTypeCrit->checkCallback = wleAEVolumeCritCheck;
	wleCriterionRegister(volumeTypeCrit);
}

#endif
