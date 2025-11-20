/***************************************************************************



***************************************************************************/

#include "rand.h"
#include "utilitiesLib.h"
#include "timing.h"
#include "fileutil.h"
#include "rgb_hsv.h"
#include "hoglib.h"
#include "qsortG.h"
#include "logging.h"
#include "stringcache.h"
#include "ContinuousBuilderSupport.h"


#include "wlState.h"

#include "WorldGridPrivate.h"
#include "WorldGridLoad.h"
#include "WorldGridLoadPrivate.h"
#include "WorldCellStreaming.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldVariable.h"
#include "ObjectLibrary.h"
#include "wlBeacon.h"
#include "wlAutoLOD.h"
#include "wlVolumes.h"
#include "wlTerrainSource.h"
#include "wlUGC.h"

#include "wininclude.h"

#include "WorldLibEnums_h_ast.h"
#include "WorldGridLoadPrivate_h_ast.c"
#include "ThreadManager.h"
#include "net/net.h"
#include "globalComm.h"
#include "sysUtil.h"
#include "utils.h"
#include "memreport.h"
#include "ScratchStack.h"
#include "utf8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:MultiplexedMakeBins_MasterThread", BUDGET_EngineMisc););

#define MAX_OBJLIB_FILE_DEPTH 15

static int tint0_column, tint0_offset_column, scale_column, wind_column, usedfield_column, version_column;

extern bool world_created_bins;

void BeginMultiplexedMakeBins_Master(ZoneMapInfo **ppZoneMapInfos);
void BeginMultiplexedMakeBins_Slave(void);


//for unknown reasons, children sometimes sit around forever after getting the message saying they're done...
//presumably due to extreme system load during log flushing or something. We don't need any logging or anything after that, so we just
//do a very certain exit
void ReallyExitRightNow(void);

//if true, then print out mmpl after every map binned
static bool sbDoMMPLDuringBinning = false;
AUTO_CMD_INT(sbDoMMPLDuringBinning, DoMMPLDuringBinning) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//if non-zero, specifies the "index" of this slave (used for various reporting purposes)
static int siDoMultiplexedMakeBinsAsSlave = 0;
AUTO_CMD_INT(siDoMultiplexedMakeBinsAsSlave, DoMultiplexedMakeBinsAsSlave) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//if non-zero, specifies number of children to launch
static int siDoMultiplexedMakeBinsAsMaster = 0;
AUTO_CMD_INT(siDoMultiplexedMakeBinsAsMaster, DoMultiplexedMakeBinsAsMaster) ACMD_COMMANDLINE;

static int siMultiplexedMakeBinsMasterProcessPID = 0;
AUTO_CMD_INT(siMultiplexedMakeBinsMasterProcessPID, MultiplexedMakeBinsMasterProcessPID) ACMD_COMMANDLINE; 
//if set, then any map whose filename includes any of these as a substring will not be makebinned

static char *spFolderForMultiplexedSlaveLogs = NULL;
AUTO_CMD_ESTRING(spFolderForMultiplexedSlaveLogs, FolderForMultiplexedSlaveLogs) ACMD_COMMANDLINE;

static char **sppMakeBinsExclusionStrings = NULL;

AUTO_COMMAND ACMD_COMMANDLINE;
void MakeBinsExclusionString(char *pString)
{
	eaPush(&sppMakeBinsExclusionStrings, strdup(pString));
}

AUTO_RUN;
int initColumnVars(void)
{
	assert(ParserFindColumn(parse_GroupDef, "Version", &version_column));
	assert(ParserFindColumn(parse_GroupDef, "TintColorHSV0", &tint0_column));
	assert(ParserFindColumn(parse_GroupDef, "TintColorOffsetHSV0", &tint0_offset_column));
	assert(ParserFindColumn(parse_GroupDef, "ModelScale", &scale_column));
	assert(ParserFindColumn(parse_GroupDef, "WindParams", &wind_column));
	assert(ParserFindColumn(parse_GroupDef, "bfParamsSpecified", &usedfield_column));
	return 1;
}

char *worldMakeBinName(const char *fname)
{
	static char buf[MAX_PATH];
	char ns[MAX_PATH], filename[MAX_PATH];
	char *cs;

	if(!fileGetNameSpacePath(fname, ns, filename))
	{
		ns[0] = '\0';
		strcpy(filename, fname);
	}
	forwardSlashes(filename);

	buf[0] = 0;

	if (strstri(filename, "BACKUPS/"))
		return NULL; // *Never* load .bin files when loading a backup!
	cs = strstri(filename, "object_library/");
	if (!cs)
		cs = strstri(filename, "maps/");
	if (!cs)
		return NULL;
	if(ns[0])
		sprintf(buf,"%s:/geobin/%s.bin", ns, cs);
	else
		sprintf(buf,"geobin/%s.bin", cs);
	return buf;
}

char *worldMakeBinDir(const char *fname)
{
	static char buf[MAX_PATH];
	char filename[MAX_PATH];
	char *cs = NULL;
	char *end = NULL;

	strcpy(filename, fname);
	forwardSlashes(filename);

	buf[0] = 0;

	if ( (end = strrchr(filename,'/')) )
	{
		end[0] = 0;
	}
	else
	{
		return NULL; // invalid name
	}

	if (strstri(filename, "BACKUPS/"))
		return NULL; // *Never* load .bin files when loading a backup!
	cs = strstri(filename, "object_library/");
	if (!cs)
		cs = strstri(filename, "maps/");
	if (!cs)
		return NULL;
	sprintf(buf,"geobin/%s/",cs);
	return buf;
}

LibFileLoad *loadLayerFromDisk(const char *filename)
{
	LibFileLoad *layerload;

	if (strlen(filename) > 7 &&
		!stricmp(&filename[strlen(filename)-7], ".clayer"))
		return NULL;

	if (strlen(filename) > 7 &&
		!stricmp(&filename[strlen(filename)-7], ".rlayer"))
		return NULL;

	// in production mode the source file will not exist
	if (isDevelopmentMode() && !fileExists(filename))
		return NULL;

	layerload = StructCreate(parse_LibFileLoad);
	if (!ParserLoadFiles(NULL, filename, NULL, PARSER_BINS_ARE_SHARED, parse_LibFileLoad, layerload))
	{
		StructDestroy(parse_LibFileLoad, layerload);
		return NULL;
	}

	return layerload;
}

LibFileLoad *loadLibFromDisk(const char *filename)
{
	LibFileLoad *libload;

	if (!fileExists(filename))
		return NULL;

	libload = StructCreate(parse_LibFileLoad);
	if (!ParserLoadFiles(NULL, filename, NULL, PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, parse_LibFileLoad, libload))
	{
		StructDestroy(parse_LibFileLoad, libload);
		return NULL;
	}

	return libload;
}

//////////////////////////////////////////////////////////////////////////

//Convert to the new collision properties
static void groupFixup_Ver0_To_Ver1(GroupDef *def)
{
	WorldPhysicalProperties *pProps = &def->property_structs.physical_properties;

	if(def->version >= 1)
		return;
	
	pProps->bVisible = !pProps->bEditorVisibleOnly_Deprecated;
	pProps->bPhysicalCollision = false;
	pProps->eCameraCollType = WLCCT_NoCamCollision;
	pProps->eGameCollType = WLGCT_FullyPermeable;

	if(pProps->bEditorVisibleOnly_Deprecated) {
		if(pProps->bFullCollision_Deprecated) {
			pProps->bPhysicalCollision = true;
			if(pProps->bTranslucentWhenCameraCollides_Deprecated) {
				pProps->eCameraCollType = WLCCT_ObjectFades;
				pProps->eGameCollType = WLGCT_TargetableOnly;
			} else if(pProps->bPermeable_Deprecated) {
				pProps->eCameraCollType = WLCCT_FullCamCollision;
			} else {
				pProps->eCameraCollType = WLCCT_FullCamCollision;
				pProps->eGameCollType = WLGCT_NotPermeable;
			}
		} else if(pProps->bAlwaysCollide_Deprecated) {
			pProps->bPhysicalCollision = true;
			pProps->eGameCollType = WLGCT_NotPermeable;
		} else if (pProps->bCameraCollision_Deprecated) {
			if(pProps->bTranslucentWhenCameraCollides_Deprecated) {
				pProps->eCameraCollType = WLCCT_ObjectFades;
			} else {
				pProps->eCameraCollType = WLCCT_FullCamCollision;
			}
		} else if (pProps->bPermeable_Deprecated) {
			pProps->bPhysicalCollision = true;
		}
	} else {
		if(pProps->bNoCollision_Deprecated) {
			if(pProps->bAlwaysCollide_Deprecated) {
				pProps->bPhysicalCollision = true;
				pProps->eGameCollType = WLGCT_NotPermeable;
			} else if (pProps->bCameraCollision_Deprecated) {
				if(pProps->bTranslucentWhenCameraCollides_Deprecated) {
					pProps->eCameraCollType = WLCCT_ObjectFades;
				} else {
					pProps->eCameraCollType = WLCCT_FullCamCollision;
				}
			}
		} else if (pProps->bTranslucentWhenCameraCollides_Deprecated) {
			pProps->bPhysicalCollision = true;
			pProps->eCameraCollType = WLCCT_ObjectFades;
			pProps->eGameCollType = WLGCT_TargetableOnly;				
		} else if (pProps->bPermeable_Deprecated) {
			pProps->bPhysicalCollision = true;
			pProps->eCameraCollType = WLCCT_FullCamCollision;
		} else {
			pProps->bPhysicalCollision = true;
			pProps->eCameraCollType = WLCCT_FullCamCollision;
			pProps->eGameCollType = WLGCT_NotPermeable;
		}
	}

	pProps->bSplatsCollision = pProps->bPhysicalCollision;

	pProps->bAlwaysCollide_Deprecated = 0;
	pProps->bNoCollision_Deprecated = 0;
	pProps->bFullCollision_Deprecated = 0;
	pProps->bCameraCollision_Deprecated = 0;
	pProps->bTranslucentWhenCameraCollides_Deprecated = 0;
	pProps->bPermeable_Deprecated = 0;
}

static void groupFixupMoveToCurrentVersion(GroupDef *def)
{
	assert(def->version <= GROUP_DEF_VERSION);//Never decrement the version number
	if(def->version == GROUP_DEF_VERSION)
		return;
	groupFixup_Ver0_To_Ver1(def);
	def->version = GROUP_DEF_VERSION;
}

static void addProperties(GroupDef *def)
{
	int			i;
	int iSize = eaSize(&def->prop_load_deprecated);

	PERFINFO_AUTO_START_FUNC();

	for(i=0;i<iSize;i++)
	{
		PropertyLoad *prop = def->prop_load_deprecated[i];
		if (!prop->name)
		{
			ErrorFilenamef(def->filename,	"null property name (%s,%s) in group \"%s\"",
				prop->name,
				prop->value,
				def->name_str);
			continue;
		}
		if (!prop->value)
			prop->value = StructAllocString("");

		removeLeadingAndFollowingSpaces(prop->name);
		removeLeadingAndFollowingSpaces(prop->value);

		// convert from old style sky fade properties to new style
		if (stricmp(prop->name, "SkyFade1")==0 && prop->value)
		{
			SkyInfoOverride *sky_override;
			char sky_name[256];

			if (!def->property_structs.client_volume.sky_volume_properties)
				def->property_structs.client_volume.sky_volume_properties = StructCreate(parse_WorldSkyVolumeProperties);
			def->property_structs.client_volume.sky_volume_properties->weight = 1;
			def->property_structs.client_volume.sky_volume_properties->fade_in_rate = 0.5;
			def->property_structs.client_volume.sky_volume_properties->fade_out_rate = 0.5;

			sky_override = StructCreate(parse_SkyInfoOverride);
			getFileNameNoExt(sky_name, prop->value);
			SET_HANDLE_FROM_STRING("SkyInfo", sky_name, sky_override->sky);
			eaPush(&def->property_structs.client_volume.sky_volume_properties->sky_group.override_list, sky_override);
		}
		else if (stricmp(prop->name, "SkyFadeWeight")==0 && prop->value)
		{
			if (def->property_structs.client_volume.sky_volume_properties)
				def->property_structs.client_volume.sky_volume_properties->weight = 1 - atof(prop->value);
		}
		else if (stricmp(prop->name, "WaterDef")==0 && prop->value)
		{
			if (!def->property_structs.client_volume.water_volume_properties)
				def->property_structs.client_volume.water_volume_properties = StructCreate(parse_WorldWaterVolumeProperties);
			StructFreeString(def->property_structs.client_volume.water_volume_properties->water_def);
			def->property_structs.client_volume.water_volume_properties->water_def = StructAllocString(prop->value);
		}
		else if (stricmp(prop->name, "IndoorAmbientHSV")==0 && prop->value)
		{
			if (!def->property_structs.client_volume.indoor_volume_properties)
				def->property_structs.client_volume.indoor_volume_properties = StructCreate(parse_WorldIndoorVolumeProperties);
			fillVec3sFromStr(prop->value, &def->property_structs.client_volume.indoor_volume_properties->ambient_hsv, 1);
		}
		else if (stricmp(prop->name, "IndoorLightRange")==0 && prop->value)
		{
			if (!def->property_structs.client_volume.indoor_volume_properties)
				def->property_structs.client_volume.indoor_volume_properties = StructCreate(parse_WorldIndoorVolumeProperties);
			def->property_structs.client_volume.indoor_volume_properties->light_range = fillF32FromStr(prop->value, 1);
		}
		else
		{
			groupDefAddPropertyEx(def, prop->name, prop->value, true);
		}
	}

	if(objectLibraryInited()) {
		if(def->property_structs.building_properties) {
			WorldBuildingProperties *pBuilding = def->property_structs.building_properties;
			if(pBuilding->lod_model_deprecated.name_uid || pBuilding->lod_model_deprecated.name_str) {
				if(pBuilding->lod_model_deprecated.name_uid || pBuilding->lod_model_deprecated.name_str) {
					GroupDef *refrenced_def = objectLibraryGetGroupDefFromRef(&pBuilding->lod_model_deprecated, false);
					if(refrenced_def) {
						SET_HANDLE_FROM_REFERENT(OBJECT_LIBRARY_DICT, refrenced_def, pBuilding->lod_group_ref);
					}
					pBuilding->lod_model_deprecated.name_uid = 0;
					StructFreeStringSafe(&pBuilding->lod_model_deprecated.name_str);
				}
			}

			for ( i=0; i < eaSize(&pBuilding->layers); i++ ) {
				WorldBuildingLayerProperties *pLayer = pBuilding->layers[i];
				if(pLayer->group_deprecated.name_uid || pLayer->group_deprecated.name_str) {
					GroupDef *refrenced_def = objectLibraryGetGroupDefFromRef(&pLayer->group_deprecated, false);
					if(refrenced_def) {
						SET_HANDLE_FROM_REFERENT(OBJECT_LIBRARY_DICT, refrenced_def, pLayer->group_ref);
					}
					pLayer->group_deprecated.name_uid = 0;
					StructFreeStringSafe(&pLayer->group_deprecated.name_str);
				}
			}
		}

		if(def->property_structs.debris_field_properties) {
			WorldDebrisFieldProperties *pDebris = def->property_structs.debris_field_properties;
			if(pDebris->group_deprecated.name_uid || pDebris->group_deprecated.name_str) {
				GroupDef *refrenced_def = objectLibraryGetGroupDefFromRef(&pDebris->group_deprecated, false);
				if(refrenced_def) {
					SET_HANDLE_FROM_REFERENT(OBJECT_LIBRARY_DICT, refrenced_def, pDebris->group_ref);
				}
				pDebris->group_deprecated.name_uid = 0;
				StructFreeStringSafe(&pDebris->group_deprecated.name_str);
			}
		}
	}

	if(def->property_structs.volume && eaSize(&def->property_structs.volume->ppcVolumeTypesDeprecated) > 0) {
		assert(!def->property_structs.hull);
		def->property_structs.hull = StructCreate(parse_GroupHullProperties);
		eaCopy(&def->property_structs.hull->ppcTypes, &def->property_structs.volume->ppcVolumeTypesDeprecated);
		eaDestroy(&def->property_structs.volume->ppcVolumeTypesDeprecated);
	}

	if(def->property_structs.physical_properties.fCurveLengthDeprecated) {
		if(!def->property_structs.child_curve)
			def->property_structs.child_curve = StructCreate(parse_WorldChildCurve);
		def->property_structs.child_curve->geo_length = def->property_structs.physical_properties.fCurveLengthDeprecated;
		def->property_structs.physical_properties.fCurveLengthDeprecated = 0;
	}

	if(!vec4IsZero(def->wind_params_deprecated)) {
		def->property_structs.wind_properties.fEffectAmount = def->wind_params_deprecated[0];
		def->property_structs.wind_properties.fBendiness = def->wind_params_deprecated[1];
		def->property_structs.wind_properties.fPivotOffset = def->wind_params_deprecated[2];
		def->property_structs.wind_properties.fRustling = def->wind_params_deprecated[3];
		setVec4same(def->wind_params_deprecated, 0);
	}

	// convert sound volumes
	if (groupIsVolumeType(def, "SoundVolume"))
	{
		def->property_structs.room_properties = StructCreate(parse_WorldRoomProperties);
		def->property_structs.room_properties->eRoomType = WorldRoomType_Room;
		if (!def->property_structs.client_volume.sound_volume_properties)
			def->property_structs.client_volume.sound_volume_properties = StructCreate(parse_WorldSoundVolumeProperties);
		def->property_structs.client_volume.sound_volume_properties->event_name = StructAllocString(groupDefFindProperty(def, "SoundSphere"));
		def->property_structs.client_volume.sound_volume_properties->dsp_name = StructAllocString(groupDefFindProperty(def, "SoundDSP"));
		if (def->property_structs.sound_sphere_properties)
			def->property_structs.client_volume.sound_volume_properties->priority = def->property_structs.sound_sphere_properties->fPriority;
		if (def->property_structs.sound_sphere_properties)
			def->property_structs.client_volume.sound_volume_properties->multiplier = def->property_structs.sound_sphere_properties->fMultiplier;
		else
			def->property_structs.client_volume.sound_volume_properties->multiplier = 1;

		StructDestroySafe(parse_WorldSoundSphereProperties, &def->property_structs.sound_sphere_properties);
		groupDefRemoveVolumeType(def, "SoundVolume");
	}
	if (groupIsVolumeType(def, "SoundConn"))
	{
		StructDestroySafe(parse_WorldSoundSphereProperties, &def->property_structs.sound_sphere_properties);
		groupDefRemoveVolumeType(def, "SoundConn");
	}

	eaDestroyStruct(&def->prop_load_deprecated, parse_PropertyLoad);

	PERFINFO_AUTO_STOP_FUNC();
}

static bool defTokenSpecified(GroupDef *defload, int column)
{
	return TokenIsSpecified(parse_GroupDef, column, defload, usedfield_column);
}

void groupEnsureValidVersion(GroupDef *defload)
{
	if(!defTokenSpecified(defload, version_column))
		defload->version = 0;
}

static void addTintColor(GroupDef *defload)
{
	Vec3 color;

	if (defTokenSpecified(defload, tint0_column) || defload->hasTint0)
	{
		hsvToRgb(defload->tintColorHSV0, color);
		groupSetTintColor(defload, color);
	}
	
	if (defTokenSpecified(defload, tint0_offset_column) || defload->hasTintOffset0)
		groupSetTintOffset(defload, defload->tintColorOffsetHSV0);
}

static void addModelParams(GroupDef *defload)
{
	if (!defTokenSpecified(defload, scale_column))
		setVec3same(defload->model_scale, 1);
}

// Returns false if already set and not forced (i.e. if there was nothing done)
static bool groupSetModel(GroupDef *defload, bool force)
{
	if(defload->model_valid && !force)
		return false;

	defload->model = NULL;
	defload->model_null = 1;

	if(defload->model_name)
	{
		defload->model = groupModelFind(defload->model_name, 0);
		if (!defload->model)
		{
			int new_def_name_uid = objectLibraryUIDFromObjName(defload->model_name);
			if (new_def_name_uid && new_def_name_uid != defload->name_uid)
			{
				// replace model with a child reference to the object library group with the same name
				int i;
				GroupChild *child = StructCreate(parse_GroupChild);
				child->name = allocAddString(defload->model_name);
				child->name_uid = new_def_name_uid;
				child->uid_in_parent = 1;
				for (i = 0; i < eaSize(&defload->children); i++)
					child->uid_in_parent = MAX(child->uid_in_parent, defload->children[i]->uid_in_parent+1);
				identityMat4(child->mat);
				eaPush(&defload->children, child);

				StructFreeString(defload->model_name);
				defload->model_name = NULL;
				defload->def_lib->was_fixed_up = 1;
				defload->was_fixed_up = 1;
			}
			else
			{
				ErrorFilenamef(defload->filename, "Can't find model %s", defload->model_name);
			}
		}
		else
			defload->model_null = 0;

		if (fabs(defload->model_scale[0]) < 0.01f ||
			fabs(defload->model_scale[1]) < 0.01f ||
			fabs(defload->model_scale[2]) < 0.01f)
		{
			ErrorFilenamef(defload->filename, "Invalid model scale in def %s %X (%f %f %f)", defload->name_str, defload->name_uid, defload->model_scale[0], defload->model_scale[1], defload->model_scale[2]);
		}
	}

	defload->model_valid = 1;

	return true;
}

void groupSetModelRecursive(GroupDef *def, bool force)
{
	int i;
	GroupChild **children = groupGetChildren(def);
	if(groupSetModel(def, force)) // early out at the root if no change is needed
	{
		for (i = 0; i < eaSize(&children); i++)
		{
			GroupDef *child_def = groupChildGetDef(def, children[i], false);
			if(child_def)
				groupSetModelRecursive(child_def, force);
		}
	}
}

void validateGroupPropertyStructs(GroupDef *def, const char *filename, const char *name_str, int name_uid, bool check_references)
{
#define VALIDATE(func, prop) if (def->property_structs.prop) func(def->property_structs.prop, filename, name_str, name_uid, check_references)

	VALIDATE(validateSkyVolumeProperties, client_volume.sky_volume_properties);
	VALIDATE(validateSoundVolumeProperties, client_volume.sound_volume_properties);
	VALIDATE(validateWaterVolumeProperties, client_volume.water_volume_properties);
	VALIDATE(validateIndoorVolumeProperties, client_volume.indoor_volume_properties);
	VALIDATE(validateFXVolumeProperties, client_volume.fx_volume_properties);

	VALIDATE(validateActionVolumeProperties, server_volume.action_volume_properties);
	VALIDATE(validatePowerVolumeProperties, server_volume.power_volume_properties);
	VALIDATE(validateWarpVolumeProperties, server_volume.warp_volume_properties);
	VALIDATE(validateLandmarkVolumeProperties, server_volume.landmark_volume_properties);
	VALIDATE(validateNeighborhoodVolumeProperties, server_volume.neighborhood_volume_properties);
	VALIDATE(validateInteractionVolumeProperties, server_volume.interaction_volume_properties);
	VALIDATE(validateAIVolumeProperties, server_volume.ai_volume_properties);
	VALIDATE(validateBeaconVolumeProperties, server_volume.beacon_volume_properties);
	VALIDATE(validateEventVolumeProperties, server_volume.event_volume_properties);
	VALIDATE(validateCivilianVolumeProperties, server_volume.civilian_volume_properties);
	VALIDATE(validateMastermindVolumeProperties, server_volume.mastermind_volume_properties);

	VALIDATE(validatePlanetProperties, planet_properties);
	VALIDATE(validateSoundConnProperties, sound_conn_properties);
	VALIDATE(validateAnimationProperties, animation_properties);
	VALIDATE(validateWindSourceProperties, wind_source_properties);

	VALIDATE(validateCurve, curve);
	VALIDATE(validateCurveGaps, curve_gaps);
	VALIDATE(validateChildCurve, child_curve);
	VALIDATE(validateLODOverride, lod_override);

	VALIDATE(validateEncounterLayerProperties, encounter_layer_properties);
	VALIDATE(validateEncounterHackProperties, encounter_hack_properties);
	VALIDATE(validateEncounterProperties, encounter_properties);
	VALIDATE(validateInteractionProperties, interaction_properties);
	VALIDATE(validateSpawnProperties, spawn_properties);
	VALIDATE(validatePatrolProperties, patrol_properties);
	VALIDATE(validateAutoPlacementProperties, auto_placement_properties);
	VALIDATE(validateTriggerConditionProperties, trigger_condition_properties);
	VALIDATE(validateLayerFSMProperties, layer_fsm_properties);
	VALIDATE(validateGenesisChallengeProperties, genesis_challenge_properties);
	VALIDATE(validateTerrainExclusionProperties, terrain_exclusion_properties);

	// GroupProps : validate struct here

#undef VALIDATE
}

void validateEncounterLayerProperties(WorldEncounterLayerProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	if (group_uid != 1)
		ErrorFilenamef(filename, "Encounter layer properties found on non-layer GroupDef (\"%s\", %d).", group_name, group_uid);
}

void validateInteractionPropertyEntry(const char *filename, const char *group_name, WorldInteractionPropertyEntry *pEntry)
{
	int i, j;

	if (!GET_REF(pEntry->hInteractionDef) && REF_STRING_FROM_HANDLE(pEntry->hInteractionDef)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent InteractionDef '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->hInteractionDef));
	}

	if (pEntry->pActionProperties) {
		for(i=eaSize(&pEntry->pActionProperties->successActions.eaActions)-1; i>=0; --i) {
			WorldGameActionProperties *pAction = pEntry->pActionProperties->successActions.eaActions[i];

			if (pAction->pGrantMissionProperties) {
				if (!GET_REF(pAction->pGrantMissionProperties->hMissionDef) && REF_STRING_FROM_HANDLE(pAction->pGrantMissionProperties->hMissionDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent Mission '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pGrantMissionProperties->hMissionDef));
				}
			}
			if (pAction->pMissionOfferProperties) {
				if (!GET_REF(pAction->pMissionOfferProperties->hMissionDef) && REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->hMissionDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent Mission '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->hMissionDef));
				}
				if (pAction->pMissionOfferProperties->pHeadshotProps) {
					if (!GET_REF(pAction->pMissionOfferProperties->pHeadshotProps->hCostume) && REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->pHeadshotProps->hCostume)) {
						ErrorFilenamef(filename, "Group '%s' refers to non-existent Costume '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->pHeadshotProps->hCostume));
					}
					if (!GET_REF(pAction->pMissionOfferProperties->pHeadshotProps->hPetContactList) && REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->pHeadshotProps->hPetContactList)) {
						ErrorFilenamef(filename, "Group '%s' refers to non-existent PetContactList '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->pHeadshotProps->hPetContactList));
					}
					if (!GET_REF(pAction->pMissionOfferProperties->pHeadshotProps->hCostumeCritterGroup) && REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->pHeadshotProps->hCostumeCritterGroup)) {
						ErrorFilenamef(filename, "Group '%s' refers to non-existent CritterGroup '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pMissionOfferProperties->pHeadshotProps->hCostumeCritterGroup));
					}
				}
			}
			if (pAction->pGiveItemProperties) {
				if (!GET_REF(pAction->pGiveItemProperties->hItemDef) && REF_STRING_FROM_HANDLE(pAction->pGiveItemProperties->hItemDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent ItemDef '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pGiveItemProperties->hItemDef));
				}
			}
			if (pAction->pTakeItemProperties) {
				if (!GET_REF(pAction->pTakeItemProperties->hItemDef) && REF_STRING_FROM_HANDLE(pAction->pTakeItemProperties->hItemDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent ItemDef '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pTakeItemProperties->hItemDef));
				}
			}
			if (pAction->pGiveDoorKeyItemProperties) {
				if (!GET_REF(pAction->pGiveDoorKeyItemProperties->hItemDef) && REF_STRING_FROM_HANDLE(pAction->pGiveDoorKeyItemProperties->hItemDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent ItemDef '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pGiveDoorKeyItemProperties->hItemDef));
				}
				worldVariableValidateDef(pAction->pGiveDoorKeyItemProperties->pDestinationMap, pAction->pGiveDoorKeyItemProperties->pDestinationMap, group_name, filename);
				for(j=eaSize(&pAction->pGiveDoorKeyItemProperties->eaVariableDefs)-1; j>=0; --j) {
					worldVariableValidateDef(pAction->pGiveDoorKeyItemProperties->eaVariableDefs[j], pAction->pGiveDoorKeyItemProperties->eaVariableDefs[j], group_name, filename);
				}
			}
			if (pAction->pNPCSendEmailProperties) {
				if (!GET_REF(pAction->pNPCSendEmailProperties->hItemDef) && REF_STRING_FROM_HANDLE(pAction->pNPCSendEmailProperties->hItemDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent ItemDef '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pNPCSendEmailProperties->hItemDef));
				}
			}
			if (pAction->pShardVariableProperties) {
				worldVariableValidateValue(pAction->pShardVariableProperties->pVarValue, group_name, filename, true);
			}
			if (pAction->pActivityLogProperties) {
				if (!GET_REF(pAction->pActivityLogProperties->dArgString.hMessage) && REF_STRING_FROM_HANDLE(pAction->pActivityLogProperties->dArgString.hMessage)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pActivityLogProperties->dArgString.hMessage));
				}
			}
			if (pAction->pSendFloaterProperties) {
				if (!GET_REF(pAction->pSendFloaterProperties->floaterMsg.hMessage) && REF_STRING_FROM_HANDLE(pAction->pSendFloaterProperties->floaterMsg.hMessage)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pSendFloaterProperties->floaterMsg.hMessage));
				}
			}
			if (pAction->pSendNotificationProperties) {
				if (!GET_REF(pAction->pSendNotificationProperties->notifyMsg.msg.hMessage) && REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->notifyMsg.msg.hMessage)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->notifyMsg.msg.hMessage));
				}
				if (pAction->pSendNotificationProperties->pHeadshotProperties) {
					if (!GET_REF(pAction->pSendNotificationProperties->pHeadshotProperties->hCostume) && REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->pHeadshotProperties->hCostume)) {
						ErrorFilenamef(filename, "Group '%s' refers to non-existent Costume '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->pHeadshotProperties->hCostume));
					}
					if (!GET_REF(pAction->pSendNotificationProperties->pHeadshotProperties->hPetContactList) && REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->pHeadshotProperties->hPetContactList)) {
						ErrorFilenamef(filename, "Group '%s' refers to non-existent PetContactList '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->pHeadshotProperties->hPetContactList));
					}
					if (!GET_REF(pAction->pSendNotificationProperties->pHeadshotProperties->hCostumeCritterGroup) && REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->pHeadshotProperties->hCostumeCritterGroup)) {
						ErrorFilenamef(filename, "Group '%s' refers to non-existent CritterGroup '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pSendNotificationProperties->pHeadshotProperties->hCostumeCritterGroup));
					}
				}
			}
			if (pAction->pContactProperties) {
				if (!GET_REF(pAction->pContactProperties->hContactDef) && REF_STRING_FROM_HANDLE(pAction->pContactProperties->hContactDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent Contact '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pContactProperties->hContactDef));
				}
			}
			if (pAction->pWarpProperties) {
				if (!GET_REF(pAction->pWarpProperties->hTransSequence) && REF_STRING_FROM_HANDLE(pAction->pWarpProperties->hTransSequence)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent DoorTransitionSequenceDef '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pWarpProperties->hTransSequence));
				}
				worldVariableValidateDef(&pAction->pWarpProperties->warpDest, &pAction->pWarpProperties->warpDest, group_name, filename);

				for(j=eaSize(&pAction->pWarpProperties->eaVariableDefs)-1; j>=0; --j) {
					worldVariableValidateDef(pAction->pWarpProperties->eaVariableDefs[j], pAction->pWarpProperties->eaVariableDefs[j], group_name, filename);
				}
				for(j=eaSize(&pAction->pWarpProperties->eaOldVariables)-1; j>=0; --j) {
					worldVariableValidateValue(pAction->pWarpProperties->eaOldVariables[j], group_name, filename, true);
				}
			}
			if (pAction->pItemAssignmentProperties) {
				if (!GET_REF(pAction->pItemAssignmentProperties->hAssignmentDef) && REF_STRING_FROM_HANDLE(pAction->pItemAssignmentProperties->hAssignmentDef)) {
					ErrorFilenamef(filename, "Group '%s' refers to non-existent ItemAssignmentDef '%s'", group_name, REF_STRING_FROM_HANDLE(pAction->pItemAssignmentProperties->hAssignmentDef));
				}
			}
		}
	}

	if (pEntry->pAnimationProperties) {
		if (!GET_REF(pEntry->pAnimationProperties->hInteractAnim) && REF_STRING_FROM_HANDLE(pEntry->pAnimationProperties->hInteractAnim)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent AIAnimList '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pAnimationProperties->hInteractAnim));
		}
	}

	if (pEntry->pTextProperties) {
		if (!GET_REF(pEntry->pTextProperties->usabilityOptionText.hMessage) && REF_STRING_FROM_HANDLE(pEntry->pTextProperties->usabilityOptionText.hMessage)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pTextProperties->usabilityOptionText.hMessage));
		}
		if (!GET_REF(pEntry->pTextProperties->interactOptionText.hMessage) && REF_STRING_FROM_HANDLE(pEntry->pTextProperties->interactOptionText.hMessage)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pTextProperties->interactOptionText.hMessage));
		}
		if (!GET_REF(pEntry->pTextProperties->interactDetailText.hMessage) && REF_STRING_FROM_HANDLE(pEntry->pTextProperties->interactDetailText.hMessage)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pTextProperties->interactDetailText.hMessage));
		}
		if (!GET_REF(pEntry->pTextProperties->successConsoleText.hMessage) && REF_STRING_FROM_HANDLE(pEntry->pTextProperties->successConsoleText.hMessage)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pTextProperties->successConsoleText.hMessage));
		}
		if (!GET_REF(pEntry->pTextProperties->failureConsoleText.hMessage) && REF_STRING_FROM_HANDLE(pEntry->pTextProperties->failureConsoleText.hMessage)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pTextProperties->failureConsoleText.hMessage));
		}
	}

	if (pEntry->pRewardProperties) {
		if (!GET_REF(pEntry->pRewardProperties->hRewardTable) && REF_STRING_FROM_HANDLE(pEntry->pRewardProperties->hRewardTable)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent RewardTable '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pRewardProperties->hRewardTable));
		}
	}

	if (pEntry->pContactProperties) {
		if (!GET_REF(pEntry->pContactProperties->hContactDef) && REF_STRING_FROM_HANDLE(pEntry->pContactProperties->hContactDef)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Contact '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pContactProperties->hContactDef));
		}
	}

	if (pEntry->pCraftingProperties) {
		if (!GET_REF(pEntry->pCraftingProperties->hCraftRewardTable) && REF_STRING_FROM_HANDLE(pEntry->pCraftingProperties->hCraftRewardTable)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent RewardTable '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pCraftingProperties->hCraftRewardTable));
		}
		if (!GET_REF(pEntry->pCraftingProperties->hDeconstructRewardTable) && REF_STRING_FROM_HANDLE(pEntry->pCraftingProperties->hDeconstructRewardTable)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent RewardTable '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pCraftingProperties->hDeconstructRewardTable));
		}
		if (!GET_REF(pEntry->pCraftingProperties->hExperimentRewardTable) && REF_STRING_FROM_HANDLE(pEntry->pCraftingProperties->hExperimentRewardTable)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent RewardTable '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pCraftingProperties->hExperimentRewardTable));
		}
	}

	if (pEntry->pDoorProperties) {
		if (!GET_REF(pEntry->pDoorProperties->hQueueDef) && REF_STRING_FROM_HANDLE(pEntry->pDoorProperties->hQueueDef)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent QueueDef '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pDoorProperties->hQueueDef));
		}
		if (!GET_REF(pEntry->pDoorProperties->hOldChoiceTable) && REF_STRING_FROM_HANDLE(pEntry->pDoorProperties->hOldChoiceTable)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent ChoiceTable '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pDoorProperties->hOldChoiceTable));
		}
		if (!GET_REF(pEntry->pDoorProperties->hTransSequence) && REF_STRING_FROM_HANDLE(pEntry->pDoorProperties->hTransSequence)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent DoorTransitionSequenceDef '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pDoorProperties->hTransSequence));
		}

		worldVariableValidateDef(&pEntry->pDoorProperties->doorDest, &pEntry->pDoorProperties->doorDest, group_name, filename);

		for(i=eaSize(&pEntry->pDoorProperties->eaVariableDefs)-1; i>=0; --i) {
			worldVariableValidateDef(pEntry->pDoorProperties->eaVariableDefs[i], pEntry->pDoorProperties->eaVariableDefs[i], group_name, filename);
		}
		for(i=eaSize(&pEntry->pDoorProperties->eaOldVariables)-1; i>=0; --i) {
			worldVariableValidateValue(pEntry->pDoorProperties->eaOldVariables[i], group_name, filename, true);
		}
	}

	if (pEntry->pDestructibleProperties) {
		if (!GET_REF(pEntry->pDestructibleProperties->hCritterDef) && REF_STRING_FROM_HANDLE(pEntry->pDestructibleProperties->hCritterDef)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent CritterDef '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pDestructibleProperties->hCritterDef));
		}
		if (!GET_REF(pEntry->pDestructibleProperties->hCritterOverrideDef) && REF_STRING_FROM_HANDLE(pEntry->pDestructibleProperties->hCritterOverrideDef)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent CritterOverrideDef '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pDestructibleProperties->hCritterOverrideDef));
		}
		if (!GET_REF(pEntry->pDestructibleProperties->hOnDeathPowerDef) && REF_STRING_FROM_HANDLE(pEntry->pDestructibleProperties->hOnDeathPowerDef)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent PowerDef '%s'", group_name, REF_STRING_FROM_HANDLE(pEntry->pDestructibleProperties->hOnDeathPowerDef));
		}
	}
}

void validateInteractionProperties(WorldInteractionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	int i;

	// Most validation is done in "gslInteractable.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(properties->displayNameMsg.hMessage)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(properties->displayNameMsg.hMessage));
	}

	for(i=eaSize(&properties->peaInteractLocations)-1; i>=0; --i) {
		WorldInteractLocationProperties *pLocation = properties->peaInteractLocations[i];

		if (!GET_REF(pLocation->hFsm) && REF_STRING_FROM_HANDLE(pLocation->hFsm)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent FSM '%s'", group_name, REF_STRING_FROM_HANDLE(pLocation->hFsm));
		}
		if (!GET_REF(pLocation->hSecondaryFsm) && REF_STRING_FROM_HANDLE(pLocation->hSecondaryFsm)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent FSM '%s'", group_name, REF_STRING_FROM_HANDLE(pLocation->hSecondaryFsm));
		}
	}

	for(i=eaSize(&properties->eaEntries)-1; i>=0; --i) {
		WorldInteractionPropertyEntry *pEntry = properties->eaEntries[i];

		validateInteractionPropertyEntry(filename, group_name, pEntry);
	}
}

void validateSkyVolumeProperties(WorldSkyVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	if (GetAppGlobalType() == GLOBALTYPE_CLIENT)
	{
		int i;
		for (i = 0; i < eaSize(&properties->sky_group.override_list); ++i)
		{
			SkyInfoOverride *sky_override = properties->sky_group.override_list[i];
			if (REF_STRING_FROM_HANDLE(sky_override->sky) && !GET_REF(sky_override->sky))
				ErrorFilenamef(filename, "Group %s (%d) has sky volume properties with an invalid Sky reference (%s).", group_name, group_uid, REF_STRING_FROM_HANDLE(sky_override->sky));
		}
	}

	properties->weight = CLAMP(properties->weight, 0, 1);
	properties->fade_in_rate = MAX(properties->fade_in_rate, 0);
	properties->fade_out_rate = MAX(properties->fade_out_rate, 0);
}

void validatePlanetProperties(WorldPlanetProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	MAX1(properties->collision_radius, properties->geometry_radius);

	MAX1(properties->atmosphere.planet_radius, 0.1f);
	MAX1(properties->atmosphere.atmosphere_thickness, 0);
	if (!properties->atmosphere.atmosphere_thickness)
		properties->has_atmosphere = false;
}

static void findBestLODInfo(GroupDef *def, ModelLODInfo **best_lod_info)
{
	GroupChild **def_children;
	int i;

	if (!def)
		return;

	if (def->model && def->model->lod_info)
	{
		if (!*best_lod_info || eaSize(&(*best_lod_info)->lods) < eaSize(&def->model->lod_info->lods))
			*best_lod_info = def->model->lod_info;
	}

	def_children = groupGetChildren(def);
	for (i = 0; i < eaSize(&def_children); ++i)
	{
		GroupDef *child_def = groupChildGetDef(def, def_children[i], false);
		if (child_def)
			findBestLODInfo(child_def, best_lod_info);
	}
}

GroupChild **generateBuildingChildren(GroupDef *def, const Mat4 world_mat, U32 parent_seed)
{
	int i, j;
	GroupChild **ret = NULL;
	F32 current_height = 0;
	int layer_groups = 0, cur_child;
	Vec3 lod_min = {8e16, 8e16, 8e16}, lod_max = {-8e16, -8e16, -8e16};
	F32 lod_near_distance, lod_far_distance;
	WorldBuildingProperties *properties = def->property_structs.building_properties;
	ModelLODInfo *best_lod_info = NULL;
	GroupDef *building_def = NULL, *lod_def = NULL;
	GroupChild *new_child;
	Model *lod_model = NULL;
	char group_name[256];

	for (i = 0; i < eaSize(&properties->layers); ++i)
	{
		WorldBuildingLayerProperties *layer = properties->layers[i];
		GroupDef *layer_def;
		MAX1(layer->height, 1);
		if(layer_def = GET_REF(layer->group_ref)) {
			layer->group_def_cached = groupChildGetDefEx(def, layer_def->name_uid, layer_def->name_str, false, false);
			if (layer->group_def_cached)
			{
				if (!layer->group_def_cached->def_lib->zmap_layer)
					layer->group_def_cached = objectLibraryGetEditingCopy(layer->group_def_cached, true, false);
				layer_groups++;
			}
		}
	}

	// make a group for the layers
	groupLibMakeGroupName(def->def_lib, "Layers", SAFESTR(group_name), 0);
	building_def = groupLibNewGroupDef(def->def_lib, def->filename, 0, group_name, 0, false, true);
	if (!def->def_lib->zmap_layer)
		building_def = objectLibraryGetEditingCopy(building_def, true, false);
	building_def->is_dynamic = true;
	assert(building_def);

	// Add building def as a child
	new_child = StructCreate(parse_GroupChild);
	new_child->name_uid = building_def->name_uid;
	new_child->name = building_def->name_str;
	//new_child->def = building_def;
	if (world_mat)
		copyMat4(world_mat, new_child->mat);
	else
		identityMat4(new_child->mat);
	eaPush(&ret, new_child);

	cur_child = 0;
	for (i = 0; i < eaSize(&properties->layers); ++i)
	{
		WorldBuildingLayerProperties *layer = properties->layers[i];

		if (!layer->seed)
			layer->seed = parent_seed + i*17;

		if (layer->group_def_cached)
		{
			F32 bottom_offset, top_offset;
			GroupDef *layer_def;
			Vec3 occ_min = {8e16, 8e16, 8e16}, occ_max = {-8e16, -8e16, -8e16};

			bottom_offset = layer->group_def_cached->property_structs.physical_properties.fBuildingGenBottomOffset;
			top_offset = layer->group_def_cached->property_structs.physical_properties.fBuildingGenTopOffset;

			groupLibMakeGroupName(def->def_lib, "Layers", SAFESTR(group_name), 0);
			layer_def = groupLibNewGroupDef(def->def_lib, def->filename, 0, group_name, 0, false, true);
			if (!def->def_lib->zmap_layer)
				layer_def = objectLibraryGetEditingCopy(layer_def, true, false);
			layer_def->is_dynamic = true;
			assert(layer_def);

			groupDefSetChildCount(layer_def, NULL, layer->height);

			// use an occlusion volume, do not use the building geometry as occluders
			layer_def->property_structs.physical_properties.bNoChildOcclusion = 1;

			groupChildInitialize(building_def, cur_child++, layer_def, NULL, 0, 0, 0);

			for (j = 0; j < layer->height; ++j)
			{
				Vec3 bounds_min, bounds_max;
				GroupChild *entry = groupChildInitialize(layer_def, j, layer->group_def_cached, NULL, 0, layer->seed + j * layer->seed_delta, 0);
				entry->mat[3][1] = current_height - bottom_offset;

				copyVec3(layer->group_def_cached->bounds.min, bounds_min);
				copyVec3(layer->group_def_cached->bounds.max, bounds_max);
				bounds_min[1] += entry->mat[3][1];
				bounds_max[1] += entry->mat[3][1] - top_offset;
				vec3RunningMin(bounds_min, lod_min);
				vec3RunningMax(bounds_max, lod_max);

				if (groupIsVolumeType(layer->group_def_cached, "Occluder"))
				{
					GroupVolumeProperties *vol_props = layer->group_def_cached->property_structs.volume;
					if(vol_props) {
						copyVec3(vol_props->vBoxMin, bounds_min);
						copyVec3(vol_props->vBoxMax, bounds_max);
					} else {
						setVec3same(bounds_min, 0);
						setVec3same(bounds_max, 0);
					}
					bounds_min[1] += entry->mat[3][1];
					bounds_max[1] += entry->mat[3][1];
				}
				vec3RunningMin(bounds_min, occ_min);
				vec3RunningMax(bounds_max, occ_max);

				current_height += layer->group_def_cached->bounds.max[1] - bottom_offset - top_offset;
			}

			if (!groupIsVolumeType(def, "Occluder") && !properties->no_occlusion)
			{
				if(!def->property_structs.volume)
					def->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
				def->property_structs.volume->eShape = GVS_Box;
				copyVec3(occ_min, def->property_structs.volume->vBoxMin);
				copyVec3(occ_max, def->property_structs.volume->vBoxMax);
				groupDefAddVolumeType(layer_def, "Occluder");
			}
		}
	}

	// Calculate child bounds & set radius
	groupSetBounds(building_def, false);

	findBestLODInfo(building_def, &best_lod_info);
	if (best_lod_info)
	{
		ModelLODTemplate *lod_template = lodinfoGetTemplateForRadius(best_lod_info, building_def->bounds.radius);

		building_def->property_structs.lod_override = StructCreate(parse_WorldLODOverride);
		eaCopyStructs(&lod_template->lods, &building_def->property_structs.lod_override->lod_distances, parse_AutoLODTemplate);
		building_def->bounds_valid = 0;
		building_def->property_structs.lod_override->lod_distances[eaSize(&building_def->property_structs.lod_override->lod_distances)-1]->no_fade = true;
		
		lod_near_distance = loddistFromLODInfo(NULL, best_lod_info, lod_template->lods, 1, NULL, false, NULL, NULL);
	}
	else
	{
		lod_near_distance = 500;
	}

	lod_far_distance = 10 * lod_near_distance;

	if (groupIsVolumeType(def, "Occluder"))
	{
		GroupVolumeProperties *vol_props = def->property_structs.volume;
		if(vol_props) {
			copyVec3(vol_props->vBoxMin, lod_min);
			copyVec3(vol_props->vBoxMax, lod_max);
		} else {
			setVec3same(lod_min, 0);
			setVec3same(lod_max, 0);
		}
	}

	// lookup LOD model and put it in a containing group marked as nocoll
	if (!properties->no_lod) {
		GroupDef *lod_model_def = GET_REF(properties->lod_group_ref);
		if(lod_model_def) {
			lod_model = groupModelFind(lod_model_def->name_str, WL_FOR_WORLD);
		}
	}
	if (lod_model)
	{
		groupLibMakeGroupName(def->def_lib, "LOD", SAFESTR(group_name), 0);
		lod_def = groupLibNewGroupDef(def->def_lib, def->filename, 0, group_name, 0, false, true);
		if (!def->def_lib->zmap_layer)
			lod_def = objectLibraryGetEditingCopy(lod_def, true, false);
		lod_def->is_dynamic = true;
		assert(lod_def);

		lod_def->property_structs.physical_properties.bNoOcclusion = 1;
		lod_def->property_structs.physical_properties.bSplatsCollision = false;
		lod_def->property_structs.physical_properties.bPhysicalCollision = false;
		lod_def->property_structs.physical_properties.eCameraCollType = WLCCT_NoCamCollision;
		lod_def->property_structs.physical_properties.eGameCollType = WLGCT_FullyPermeable;

		lod_def->property_structs.lod_override = StructCreate(parse_WorldLODOverride);
		eaPush(&lod_def->property_structs.lod_override->lod_distances, StructCreate(parse_AutoLODTemplate));
		lod_def->property_structs.lod_override->lod_distances[0]->lod_near = lod_near_distance;
		lod_def->property_structs.lod_override->lod_distances[0]->lod_far = lod_far_distance;
		lod_def->property_structs.lod_override->lod_distances[0]->no_fade = true;
		lod_def->bounds_valid = 0;

		subVec3(lod_max, lod_min, lod_def->model_scale);
		lod_def->model_scale[0] /= lod_model->max[0] - lod_model->min[0];
		lod_def->model_scale[1] /= lod_model->max[1] - lod_model->min[1];
		lod_def->model_scale[2] /= lod_model->max[2] - lod_model->min[2];

		lod_def->model = lod_model;
	}

	// Add LOD def as a child
	if (lod_def)
	{
		Vec3 new_min;
		Mat4 local_mat;

		// offset def so scaled LOD model is positioned correctly
		mulVecVec3(lod_def->model_scale, lod_model->min, new_min);
		identityMat3(local_mat);
		subVec3(lod_min, new_min, local_mat[3]);

		new_child = StructCreate(parse_GroupChild);
		//new_child->def = lod_def;
		new_child->name_uid = lod_def->name_uid;
		new_child->name = lod_def->name_str;
		if (world_mat)
			mulMat4(world_mat, local_mat, new_child->mat);
		else
			copyMat4(local_mat, new_child->mat);
		eaPush(&ret, new_child);

		groupSetBounds(lod_def, false);
	}

	groupInvalidateBounds(building_def);
	groupInvalidateBounds(lod_def);

	return ret;
}

void validateDebrisFieldProperties(WorldDebrisFieldProperties *properties, GroupDef *def, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateSoundVolumeProperties(WorldSoundVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	if(properties->multiplier==0)
	{
		ErrorFilenameGroupf(filename, "Audio", 3, "Sound volume %s has 0 multiplier.", group_name);
	}
}

void validateSoundConnProperties(WorldSoundConnProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	if(properties->max_range==0)
	{
		ErrorFilenameGroupf(filename, "Audio", 3, "Sound Connector: %s has 0 max range", group_name);
	}
}

void validateAnimationProperties(WorldAnimationProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	properties->sway_angle[0] = CLAMP(properties->sway_angle[0], 0, PI);
	properties->sway_angle[1] = CLAMP(properties->sway_angle[1], 0, PI);
	properties->sway_angle[2] = CLAMP(properties->sway_angle[2], 0, PI);

	properties->scale_amount[0] = CLAMP(properties->scale_amount[0], 0.01, 2);
	properties->scale_amount[1] = CLAMP(properties->scale_amount[1], 0.01, 2);
	properties->scale_amount[2] = CLAMP(properties->scale_amount[2], 0.01, 2);

	properties->translation_amount[0] = CLAMP(properties->translation_amount[0], -10000, 10000);
	properties->translation_amount[1] = CLAMP(properties->translation_amount[1], -10000, 10000);
	properties->translation_amount[2] = CLAMP(properties->translation_amount[2], -10000, 10000);
}

void validateCurve(WorldCurve *curve, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateCurveGaps(WorldCurveGaps *curve_gaps, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateChildCurve(WorldChildCurve *curve, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateLODOverride(WorldLODOverride *lod_override, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	int i;
	for (i = 0; i < eaSize(&lod_override->lod_distances); ++i)
	{
		AutoLODTemplate *lod = lod_override->lod_distances[i];
		AutoLODTemplate *prev_lod = i>0?lod_override->lod_distances[i-1]:NULL;
		if (lod->lod_near < 0)
		{
			ErrorFilenamef(filename, "Group %s (%d) has an LOD override with a negative near distance.", group_name, group_uid);
			lod->lod_near = 0;
		}
		if (prev_lod && lod->lod_near < prev_lod->lod_far)
		{
			ErrorFilenamef(filename, "Group %s (%d) has an LOD override with out of order distances.", group_name, group_uid);
			lod->lod_near = prev_lod->lod_far;
		}
		if (lod->lod_far < lod->lod_near + 10)
		{
			ErrorFilenamef(filename, "Group %s (%d) has an LOD override with the far distance less than or too close to the near distance.", group_name, group_uid);
			lod->lod_far = lod->lod_near + 10;
		}
	}
}

void validateEncounterHackProperties(WorldEncounterHackProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Most validation is done in "gslEncounter.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->base_def) && REF_STRING_FROM_HANDLE(properties->base_def)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent EncounterDef '%s'", group_name, REF_STRING_FROM_HANDLE(properties->base_def));
	}
}

void validateEncounterProperties(WorldEncounterProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	int i, j;

	// Most validation is done in "gslEncounter.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->hTemplate) && REF_STRING_FROM_HANDLE(properties->hTemplate)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent EncounterTemplate '%s'", group_name, REF_STRING_FROM_HANDLE(properties->hTemplate));
	}

	for(i=eaSize(&properties->eaActors)-1; i>=0; --i) {
		WorldActorProperties *pActor = properties->eaActors[i];

		if (!GET_REF(pActor->hFSMOverride) && REF_STRING_FROM_HANDLE(pActor->hFSMOverride)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent FSM '%s'", group_name, REF_STRING_FROM_HANDLE(pActor->hFSMOverride));
		}
		if (!GET_REF(pActor->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pActor->displayNameMsg.hMessage)) {
			ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(pActor->displayNameMsg.hMessage));
		}
		for(j=eaSize(&pActor->eaFSMVariableDefs)-1; j>=0; --j) {
			worldVariableValidateDef(pActor->eaFSMVariableDefs[j], pActor->eaFSMVariableDefs[j], group_name, filename);
		}

		if (pActor->pInteractionProperties) {
			validateInteractionProperties(pActor->pInteractionProperties, filename, group_name, group_uid, true);
		}
		if (pActor->pCostumeProperties) {
			if (!GET_REF(pActor->pCostumeProperties->hCostume) && REF_STRING_FROM_HANDLE(pActor->pCostumeProperties->hCostume)) {
				ErrorFilenamef(filename, "Group '%s' refers to non-existent PlayerCostume '%s'", group_name, REF_STRING_FROM_HANDLE(pActor->pCostumeProperties->hCostume));
			}
		}
		if (pActor->pCritterProperties) {
			if (!GET_REF(pActor->pCritterProperties->hCritterDef) && REF_STRING_FROM_HANDLE(pActor->pCritterProperties->hCritterDef)) {
				ErrorFilenamef(filename, "Group '%s' refers to non-existent CritterDef '%s'", group_name, REF_STRING_FROM_HANDLE(pActor->pCritterProperties->hCritterDef));
			}
		}
		if (pActor->pFactionProperties) {
			if (!GET_REF(pActor->pFactionProperties->hCritterFaction) && REF_STRING_FROM_HANDLE(pActor->pFactionProperties->hCritterFaction)) {
				ErrorFilenamef(filename, "Group '%s' refers to non-existent CritterFaction '%s'", group_name, REF_STRING_FROM_HANDLE(pActor->pFactionProperties->hCritterFaction));
			}
		}
	}
}

void validateActionVolumeProperties(WorldActionVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Nothing to do
}

void validatePowerVolumeProperties(WorldPowerVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Most validation is done in "gslVolume.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->power) && REF_STRING_FROM_HANDLE(properties->power)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent PowerDef '%s'", group_name, REF_STRING_FROM_HANDLE(properties->power));
	}
}

void validateWarpVolumeProperties(WorldWarpVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	int i;

	// Most validation is done in "gslVolume.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->hTransSequence) && REF_STRING_FROM_HANDLE(properties->hTransSequence)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent DoorTransitionSequenceDef '%s'", group_name, REF_STRING_FROM_HANDLE(properties->hTransSequence));
	}
	if (!GET_REF(properties->old_choice_table) && REF_STRING_FROM_HANDLE(properties->old_choice_table)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent ChoiceTable '%s'", group_name, REF_STRING_FROM_HANDLE(properties->old_choice_table));
	}

	worldVariableValidateDef(&properties->warpDest, &properties->warpDest, group_name, filename);
	for(i=eaSize(&properties->oldVariables)-1; i>=0; --i) {
		worldVariableValidateValue(properties->oldVariables[i], group_name, filename, true);
	}
	for(i=eaSize(&properties->variableDefs)-1; i>=0; --i) {
		worldVariableValidateDef(properties->variableDefs[i], properties->variableDefs[i], group_name, filename);
	}
}

void validateLandmarkVolumeProperties(WorldLandmarkVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Most validation is done in "gslVolume.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->display_name_msg.hMessage) && REF_STRING_FROM_HANDLE(properties->display_name_msg.hMessage)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(properties->display_name_msg.hMessage));
	}
}

void validateNeighborhoodVolumeProperties(WorldNeighborhoodVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Most validation is done in "gslVolume.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->display_name_msg.hMessage) && REF_STRING_FROM_HANDLE(properties->display_name_msg.hMessage)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent Message '%s'", group_name, REF_STRING_FROM_HANDLE(properties->display_name_msg.hMessage));
	}
}

void validateInteractionVolumeProperties(WorldInteractionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Most validation is done in "gslVolume.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	validateInteractionProperties(properties, filename, group_name, group_uid, true);
}

void validateAIVolumeProperties(WorldAIVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Nothing to do
}

void validateBeaconVolumeProperties(WorldBeaconVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Nothing to do
}

void validateEventVolumeProperties(WorldEventVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Nothing to do
}

void validateSpawnProperties(WorldSpawnProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	int i;
	eaQSort(properties->source_volume_names, strCmp);
	for (i = eaSize(&properties->source_volume_names) - 1; i > 0; i--)
	{
		if (strcmpi(properties->source_volume_names[i], properties->source_volume_names[i - 1]) == 0)
			StructFreeString(eaRemove(&properties->source_volume_names, i));
	}

	// Most validation is done in "gslSpawnPoint.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->hTransSequence) && REF_STRING_FROM_HANDLE(properties->hTransSequence)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent DoorTransitionSequenceDef '%s'", group_name, REF_STRING_FROM_HANDLE(properties->hTransSequence));
	}

}

void validatePatrolProperties(WorldPatrolProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Nothing to do
}

void validateWaterVolumeProperties(WorldWaterVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	if (stricmp(properties->water_def, "Air")!=0 && !wlVolumeWaterFromKey(properties->water_def))
	{
		ErrorFilenamef(filename, "Group %s (%d) has water volume properties with an invalid water def name (%s).", group_name, group_uid, properties->water_def);
	}
}

void validateIndoorVolumeProperties(WorldIndoorVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateFXVolumeProperties(WorldFXVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateTriggerConditionProperties(WorldTriggerConditionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	// Nothing to do
}

void validateLayerFSMProperties(WorldLayerFSMProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
	int i;

	// Most validation is done in "gslLayerFSM.c" for layers
	if (!check_references) {
		return;
	}

	// basic reference checks are also done here for library pieces

	if (!GET_REF(properties->hFSM) && REF_STRING_FROM_HANDLE(properties->hFSM)) {
		ErrorFilenamef(filename, "Group '%s' refers to non-existent FSM '%s'", group_name, REF_STRING_FROM_HANDLE(properties->hFSM));
	}
	for(i=eaSize(&properties->fsmVars)-1; i>=0; --i) {
		worldVariableValidateValue(properties->fsmVars[i], group_name, filename, true);
	}
}

void validateAutoPlacementProperties(WorldAutoPlacementProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateCivilianVolumeProperties(WorldCivilianVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateMastermindVolumeProperties(WorldMastermindVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateGenesisChallengeProperties(WorldGenesisChallengeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

void validateTerrainExclusionProperties(WorldTerrainExclusionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}


void validateWindSourceProperties(WorldWindSourceProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references)
{
}

// GroupProps : add validate function

void groupInvalidateBounds(GroupDef *def)
{
	if (!def)
		return;
	def->bounds_valid = 0;
}

GroupDef *groupChildGetDefEx(GroupDef *parent, int def_name_uid, const char *def_name, bool silent, bool skip_parent_check)
{
	GroupDef *child_def = NULL;
	bool child_in_objlib = (!parent->def_lib->zmap_layer && !parent->def_lib->editing_lib && !parent->def_lib->dummy);

	if (!def_name_uid && (!def_name || !def_name[0]))
	{
		if (!silent)
			ErrorFilenamef(parent->filename, "Group %s references invalid child group with UID %d and name \"%s\".", parent->name_str, def_name_uid, def_name ? def_name : "(null)");
		child_def = objectLibraryGetDummyGroupDef();
		assert(child_def != parent);
		return child_def;
	}

	if (def_name)
		assert(!strchr(def_name, '/') && !strchr(def_name, '\\'));

	if (groupIsObjLibUID(def_name_uid))
	{
		if (!child_in_objlib)
		{
			// Check editing lib
			if (!child_def)
				child_def = objectLibraryGetEditingGroupDef(def_name_uid, true);
		}

		// Check object lib
		if (!child_def)
			child_def = objectLibraryGetGroupDef(def_name_uid, false);
		if (!child_def && def_name)
			child_def = objectLibraryGetGroupDefByName(def_name, false);

		if (child_def && !child_in_objlib && child_def!=objectLibraryGetDummyGroupDef())
		{
			child_def = objectLibraryGetEditingCopy(child_def, true, false);
		}

		if (child_def && child_def->name_uid != def_name_uid)
		{
			parent->def_lib->was_fixed_up = 1;
			parent->was_fixed_up = 1;
		}
	}
	else
	{
 		child_def = groupLibFindGroupDef(parent->def_lib, def_name_uid, true);

		if (!child_def && def_name)
		{
			child_def = groupLibFindGroupDefByName(parent->def_lib, def_name, true);
			if (!child_def || child_def->name_uid == 1)
				child_def = NULL; // not allowed to reference a layer root group as a child
 			else
			{
 				parent->def_lib->was_fixed_up = 1;
				parent->was_fixed_up = 1;
			}
		}
	}

	if (!child_def || child_def == objectLibraryGetDummyGroupDef())
	{
		if (!silent)
			ErrorFilenamef(parent->filename, "Group %s references unknown group %s (%d)", parent->name_str, def_name, def_name_uid);
		child_def = objectLibraryGetDummyGroupDef();
		if (groupIsObjLibUID(def_name_uid))
			groupLibAddTemporaryDef(objectLibraryGetEditingDefLib(), child_def, def_name_uid);
		else
			groupLibAddTemporaryDef(parent->def_lib, child_def, def_name_uid);
	}

	if(!skip_parent_check)
		assert(child_def != parent);
	return child_def;
}



static void worldFixupInteractionsPostRead(GroupDef *pDef)
{
	WorldInteractionProperties *pVolumeProps = pDef->property_structs.server_volume.interaction_volume_properties;
	if(pVolumeProps) {
		int i;
		for ( i=0; i < eaSize(&pVolumeProps->eaEntries); i++ ) {
			WorldInteractionPropertyEntry *pEntry = pVolumeProps->eaEntries[i];
			if(pEntry->pVisibleExpr){
				char *pcVisibleExpr = NULL;
				estrCopy2(&pcVisibleExpr, exprGetCompleteString(pEntry->pVisibleExpr));
				if(pcVisibleExpr && pcVisibleExpr[0]) {
					char *pcInteractExpr = NULL;
					if(pEntry->pInteractCond)
						estrCopy2(&pcInteractExpr, exprGetCompleteString(pEntry->pInteractCond));
					if(pcInteractExpr && pcInteractExpr[0]) {
						char *pcNewCond = NULL;
						estrPrintf(&pcNewCond, "(%s) and (%s)", pcInteractExpr, pcVisibleExpr);
						exprSetOrigStrNoFilename(pEntry->pInteractCond, pcNewCond);
						estrDestroy(&pcNewCond);
					} else {
						if(!pEntry->pInteractCond)
							pEntry->pInteractCond = StructCreate(parse_Expression);
						exprSetOrigStrNoFilename(pEntry->pInteractCond, pcVisibleExpr);
					}
					estrDestroy(&pcInteractExpr);
				}
				estrDestroy(&pcVisibleExpr);
				StructDestroySafe(parse_Expression, &pEntry->pVisibleExpr);
			}
		}	
	}
}

void groupFixupAfterRead(GroupDef *defload, bool useBitfield)
{
	int i, j;

	stashTableClear(defload->name_to_path);
	stashTableClear(defload->path_to_name);

	// scope data
	for (i = 0; i < eaSize(&defload->scope_entries_load); i++)
	{
		if (!resIsValidName(defload->scope_entries_load[i]->name))
			ErrorFilenamef(defload->filename, "Scope name '%s' contains invalid characters", defload->scope_entries_load[i]->name);
		groupDefScopeSetPathName(defload, defload->scope_entries_load[i]->path, defload->scope_entries_load[i]->name, false);
	}
	if (defload->name_uid == 1)
	{
		for (i = 0; i < eaSize(&defload->instance_data_load); i++)
			groupDefLayerScopeAddInstanceData(defload, defload->instance_data_load[i]->name, StructCloneFields(parse_InstanceData, &defload->instance_data_load[i]->data));
	}

	defload->group_flags = 0;

	if (defload->replace_material_name)
	{
		defload->group_flags |= GRP_HAS_MATERIAL_REPLACE;
	}
	if (defload->material_properties)
	{
		defload->group_flags |= GRP_HAS_MATERIAL_PROPERTIES;
	}

	eaDestroyStruct(&defload->material_swaps, parse_MaterialSwap);
	eaDestroyStruct(&defload->texture_swaps, parse_TextureSwap);
	for (i=0;i<eaSize(&defload->tex_swap_load);i++)
	{
		bool found_dup = false;

		if (!defload->tex_swap_load[i]->orig_swap_name || !defload->tex_swap_load[i]->rep_swap_name)
			continue;

		// Delete duplicates (TomY TODO remove this later when duplicate texswap issue is fixed)
		for (j=0; j < i; j++)
		{
			if (StructCompare(parse_TexSwapLoad, defload->tex_swap_load[i], defload->tex_swap_load[j], 0, 0, 0) == 0)
			{
				found_dup = true;
				break;
			}
		}
		if (found_dup)
			continue;

		if (defload->tex_swap_load[i]->is_material)
		{
			eaPush(&defload->material_swaps, createMaterialSwap(defload->tex_swap_load[i]->orig_swap_name, defload->tex_swap_load[i]->rep_swap_name));
			defload->group_flags |= GRP_HAS_MATERIAL_SWAPS;
		}
		else
		{
			eaPush(&defload->texture_swaps, createTextureSwap(defload->filename, defload->tex_swap_load[i]->orig_swap_name, defload->tex_swap_load[i]->rep_swap_name));
			defload->group_flags |= GRP_HAS_TEXTURE_SWAPS;
		}
	}

	addProperties(defload);

	addTintColor(defload);
	if (useBitfield && (defload->model_name || defload->property_structs.planet_properties))
		addModelParams(defload);

	groupFixupMoveToCurrentVersion(defload);
	groupFixupChildren(defload);

	validateGroupPropertyStructs(defload, defload->filename, defload->name_str, defload->name_uid, false);

	worldFixupInteractionsPostRead(defload);
	worldFixupVolumeInteractions(defload, NULL, &defload->property_structs.server_volume);

	groupInvalidateBounds(defload);
}

static GroupDef *groupDefFromLoaded(GroupDef *defload, GroupDefLib *def_lib, ZoneMapLayer *layer, const char *filename)
{
	GroupDef *dup;

 	if (objectLibraryInited() && !isProductionMode())
		filelog_printf("objectLog", "groupDefFromLoaded: %d %s", defload->name_uid, defload->name_str);
	if (filename)
	{
		defload->filename = filename;

		// Check if this name or uid already exist in the object library
		if (dup = objectLibraryGetGroupDef(defload->name_uid, false))
		{
			ErrorFilenamef(filename, "Group %s (%d) conflicts with %s (%d) in the Object Library, ignoring.", defload->name_str, defload->name_uid, dup->name_str, dup->name_uid);
		}
	}
	if (!groupLibAddGroupDef(def_lib, defload, &dup))
	{
		if (filename)
			ErrorFilenamef(filename, "Group %s (%d) exists twice in layer; ignoring.", defload->name_str, defload->name_uid);
		else
		{
			char def_filename[MAX_PATH];
			if (strEndsWith(defload->filename, ROOTMODS_EXTENSION))
				changeFileExt(defload->filename, MODELNAMES_EXTENSION, def_filename);
			else
				strcpy(def_filename, defload->filename);
			if (dup)
			{
				if (defload->name_uid == dup->name_uid)
				{
					// Duplicate named objects, find the appropriate files to blame
					char dup_filename[MAX_PATH];
					if (strEndsWith(dup->filename, ROOTMODS_EXTENSION))
						changeFileExt(dup->filename, MODELNAMES_EXTENSION, dup_filename);
					else
						strcpy(dup_filename, dup->filename);
					ErrorFilenameDup(def_filename, dup_filename, defload->name_str, "Group");
				} else // Something else going wrong?
					ErrorFilenamef(def_filename, "Group %s (%d) conflicts with %s (%d) in the Object Library, ignoring.", defload->name_str, defload->name_uid, dup->name_str, dup->name_uid); // TomY TODO figure out why this is going off on Object Library save
			} else
				ErrorFilenamef(def_filename, "Group %s (%d) cannot be added to Object Library.", defload->name_str, defload->name_uid);
		}
		StructDestroy(parse_GroupDef, defload);
		return NULL;
	}

	assert(defload->def_lib);

	return defload;
}

void groupPostLoad(GroupDef *def)
{
	groupSetModelRecursive(def, true);
	groupSetBounds(def, true);
}

void worldFixupVolumeInteractions(GroupDef *def, WorldVolumeEntry *entry, GroupVolumePropertiesServer *volume_properties)
{
	if (volume_properties && volume_properties->obsolete_optionalaction_properties)
	{
		int i;
		for (i = 0; i < eaSize(&volume_properties->obsolete_optionalaction_properties->entries); i++)
		{
			WorldOptionalActionVolumeEntry *optionalaction_entry = volume_properties->obsolete_optionalaction_properties->entries[i];
			WorldInteractionPropertyEntry *interaction_entry = StructCreate(parse_WorldInteractionPropertyEntry);

			interaction_entry->pcInteractionClass = allocAddString("Clickable");
			interaction_entry->pVisibleExpr = exprClone(optionalaction_entry->visible_cond);
			interaction_entry->pInteractCond = exprClone(optionalaction_entry->enabled_cond);
			interaction_entry->pcCategoryName = optionalaction_entry->category_name;
			interaction_entry->iPriority = optionalaction_entry->priority;
			interaction_entry->bAutoExecute = optionalaction_entry->auto_execute;
			if (IS_HANDLE_ACTIVE(optionalaction_entry->display_name_msg.hMessage))
			{
				interaction_entry->pTextProperties = StructCreate(parse_WorldTextInteractionProperties);
				StructCopyFields(parse_DisplayMessage, &optionalaction_entry->display_name_msg, &interaction_entry->pTextProperties->interactOptionText, 0, 0);
			}
			if (eaSize(&optionalaction_entry->actions.eaActions) > 0)
			{
				interaction_entry->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);
				StructCopyFields(parse_WorldGameActionBlock, &optionalaction_entry->actions, &interaction_entry->pActionProperties->successActions, 0, 0);
			}

			if (!volume_properties->interaction_volume_properties)
				volume_properties->interaction_volume_properties = StructCreate(parse_WorldInteractionProperties);
			eaPush(&volume_properties->interaction_volume_properties->eaEntries, interaction_entry);
		}
		StructDestroySafe(parse_WorldOptionalActionVolumeProperties, &volume_properties->obsolete_optionalaction_properties);
	}

	// If the volume is of type "OptionalAction", then convert it to "Interaction"
	if (groupDefIsVolumeType(def, "OptionalAction"))
	{
		groupDefRemoveVolumeType(def, "OptionalAction");
		groupDefAddVolumeType(def, "Interaction");
	}
	if (entry && (entry->volume_type_bits & wlVolumeTypeNameToBitMask("OptionalAction")))
	{
		entry->volume_type_bits &= (~wlVolumeTypeNameToBitMask("OptionalAction"));
		entry->volume_type_bits |= wlVolumeTypeNameToBitMask("Interaction");
	}


	// This block here undoes a previous bug where "Interaction" was added to all
	// volumes even if they had no interaction properties.  This block looks for volumes
	// with "Interaction" type bit set that also have no interaction properties, 
	// and clears it from the bits and types.
	if (!volume_properties || !volume_properties->interaction_volume_properties)
	{
		groupDefRemoveVolumeType(def, "Interaction");
		if (entry && (entry->volume_type_bits & wlVolumeTypeNameToBitMask("Interaction")))
		{
			entry->volume_type_bits &= (~wlVolumeTypeNameToBitMask("Interaction"));
		}
	}
}

// Sort rootmods *before* modelnames
static int sortDefLoads(const GroupDef **pDef1, const GroupDef **pDef2)
{
	int ret = (*pDef1)->name_uid - (*pDef2)->name_uid;
	if (ret != 0)
		return ret;
	return stricmp((*pDef2)->filename, (*pDef1)->filename);
}

void groupLibFromLoaded(GroupDef ***defs, int file_version, GroupDefLib *def_lib, ZoneMapLayer *layer, const char *fname, bool object_library)
{
	int i;
	StashTable rootmods_table = NULL;

	if (!defs)
		return;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Find Duplicates", 1);
	if (!layer)
	{
		rootmods_table = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);

		for (i = 0; i < eaSize(defs); i++)
			if ((*defs)[i] && !!strEndsWith((*defs)[i]->filename, ROOTMODS_EXTENSION))
			{
				GroupDef *dup;
				char filename_trunc[MAX_PATH];
				strcpy(filename_trunc, (*defs)[i]->filename);
				*strrchr(filename_trunc, '.') = '\0';
				strcatf(filename_trunc, "#%X", (*defs)[i]->name_uid);
				if (!stashFindPointer(rootmods_table, filename_trunc, &dup))
				{
					stashAddPointer(rootmods_table, filename_trunc, (*defs)[i], true);
				}
				else
				{
					ErrorFilenamef((*defs)[i]->filename, "GroupDef exists twice in rootmods file: %s and %s (%d)", (*defs)[i]->name_str, dup->name_str, dup->name_uid);
				}
				eaRemove(defs, i);
				--i;
			}
	}

	PERFINFO_AUTO_STOP_START("Create GroupDefs", 1);

	// first create all groupdefs
	for (i = 0; i < eaSize(defs); i++)
		if ((*defs)[i])
		{
			GroupDef *def = (*defs)[i];
			if (!groupDefFromLoaded(def, def_lib, layer, fname))
			{
				eaRemove(defs, i);
				--i;
			}
		}

	if (!layer)
	{
		FOR_EACH_IN_STASHTABLE(rootmods_table, GroupDef, def)
			StructDestroy(parse_GroupDef, def);
		FOR_EACH_END
		stashTableDestroy(rootmods_table);
	}

	PERFINFO_AUTO_STOP_START("PostLoad", 1);

	for (i = 0; i < eaSize(defs); i++)
		groupPostLoad((*defs)[i]);

	// TODO(CD) look for circular references

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
}

LibFileLoad *worldLoadLayerGroupFile(ZoneMapLayer *layer, const char *filename)
{
	LibFileLoad *layerondisk = NULL;
	GroupDef *def = NULL;

	world_grid.loading++;

	{
		LibFileLoad *cache_layer = ugcLayerCacheGetLayer(filename);
		if (cache_layer)
		{
			layerondisk = StructCreate(parse_LibFileLoad);
			layerondisk->filename = filename;
			FOR_EACH_IN_EARRAY(cache_layer->defs, GroupDef, cache_def)
			{
				GroupDef *new_def = StructCreate(parse_GroupDef);
				StructCopyFields(parse_GroupDef, cache_def, new_def, 0, 0);
				groupFixupAfterRead(new_def, false);
				eaPush(&layerondisk->defs, new_def);
			}
			FOR_EACH_END;
		}
	}

	if (!layerondisk)
		layerondisk = loadLayerFromDisk(filename);

	if (!layerondisk)
	{
		world_grid.loading--;
		return NULL;
	}

	// load defs
	assert(layer->grouptree.def_lib);
	groupLibFromLoaded(&layerondisk->defs, layerondisk->version, layer->grouptree.def_lib, layer, filename, false);
	eaDestroy(&layerondisk->defs);

	// find layer def
	def = groupLibFindGroupDef(layer->grouptree.def_lib, 1, false);
	if (def)
	{
		def->is_layer = 1;
		groupInvalidateBounds(def);
	}

	world_grid.loading--;

	return layerondisk;
}

void reloadFileLayer(const char *relpath)
{
	int i, j;
	bool found = false, editable = false;

	for (i = worldGetLoadedZoneMapCount()-1; i >= 0; --i)
	{
		ZoneMap *zmap = worldGetLoadedZoneMapByIndex(i);
		if (zmap)
		{
			for (j = zmapGetLayerCount(zmap)-1; j >= 0; --j)
			{
				ZoneMapLayer *layer = zmapGetLayer(zmap, j);
				if (layer && (!relpath || stricmp(relpath, layerGetFilename(layer))==0))
				{
					if (!relpath || layer->last_change_time != fileLastChanged(relpath))
					{
						if (layer->layer_mode == LAYER_MODE_EDITABLE)
						{
							Alertf("Warning! Layer contents changing on disk while editing! This could lead to data loss.");
						}
						else
						{
							printf("Reloading modified layer %s...\n", layerGetFilename(layer));
							// TODO: this doesn't work because server prevents saving a file if the file was not reloaded properly;
							// we'll look into this later
							layerSetMode(layer, LAYER_MODE_TERRAIN, true, false, true);
							found = true;
						}
					}
				}
			}
		}
	}

	if (found)
	{
		worldUpdateBounds(editable, editable);
		worldCheckForNeedToOpenCells();
	}
}

void worldLoadBeacons(void)
{
	if(wlIsServer() && !wlDontLoadBeacons())
	{
		beaconReload();

#if !PLATFORM_CONSOLE
		beaconFileGatherMetaData(worldGetAnyCollPartitionIdx(), 0, 0);
		beaconClearCRCData();
#endif
	}
}

//////////////////////////////////////////////////////////////////////////

static StashTable htProcessed;
static int file_count;

static char **sppOnlyMapsToBin = NULL;
AUTO_COMMAND ACMD_COMMANDLINE;
void OnlyMapToBin(char *pMaps)
{
	eaDestroyEx(&sppOnlyMapsToBin, NULL);
	DivideString(pMaps, ",", &sppOnlyMapsToBin, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);
}

static char **sppMapsNotToBin = NULL;
AUTO_COMMAND ACMD_COMMANDLINE;
void MapsNotToBin(char *pMaps)
{
	eaDestroyEx(&sppMapsNotToBin, NULL);
	DivideString(pMaps, ",", &sppMapsNotToBin, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);
}

static char *spFilenameForOnlyMapToBinErrors = NULL;
AUTO_CMD_ESTRING(spFilenameForOnlyMapToBinErrors, FilenameForOnlyMapToBinErrors) ACMD_COMMANDLINE;

static int cmpZoneMapInfoFilename(const ZoneMapInfo **ppinfo1, const ZoneMapInfo **ppinfo2)
{
	const char *filename1 = zmapInfoGetFilename(*ppinfo1);
	const char *filename2 = zmapInfoGetFilename(*ppinfo2);
	assert(filename1 && filename2);
	return stricmp(filename1, filename2);
}

static bool stringStartsWithItemInArray(const char*** eaArray, const char* string)
{
	if(eaArray && string)
	{
		int i, n = eaSize(eaArray);

		for(i=0; i<n; i++)
		{
			if(strStartsWith(string, (*eaArray)[i]))
				return true;
		}
	}
	return false;
}


bool g_EnableFastBinCheck = false;
AUTO_CMD_INT(g_EnableFastBinCheck, EnableFastBinCheck) ACMD_CMDLINE;

bool g_ExcludeBinningNamespaceMaps = false;
AUTO_CMD_INT(g_ExcludeBinningNamespaceMaps, ExcludeBinningNamespaceMaps) ACMD_CMDLINE;

static void binAllMapsInList(bool do_memdump, bool do_verify)
{
#if !PLATFORM_CONSOLE
	ZoneMapInfo *zminfo, **zminfos = NULL, **verify_zminfos = NULL;
	RefDictIterator zmap_iter;
	char consoleTitle[256];
	int i;
	bool *pbFound = NULL;

	PERFINFO_AUTO_START_FUNC();

	loadstart_printf("Calculating map dependencies...");

	if (do_memdump)
	{
		makeDirectories("c:\\memlogs");
		// TODO delete old memlogs
	}

	if (eaSize(&sppOnlyMapsToBin))
	{
		pbFound = calloc(eaSize(&sppOnlyMapsToBin) * sizeof(bool), 1);
	}

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter)) // worldGetNextZoneMap does the private map filtering
	{
		const char *public_name = zmapInfoGetPublicName(zminfo);

		if (!public_name)
			continue;




		if (eaSize(&sppOnlyMapsToBin))
		{
			int iIndex;
			assert(pbFound);
			if ((iIndex = eaFindString(&sppOnlyMapsToBin, public_name)) == -1)
			{
				printf("NOT binning %s\n", public_name);
				continue;
			}
			else
			{
				pbFound[iIndex] = true;
			}
		}

		if (eaSize(&sppMakeBinsExclusionStrings))
		{
			const char *file_name = zmapInfoGetFilename(zminfo);
			bool bFound = false;

			for (i=0; i < eaSize(&sppMakeBinsExclusionStrings); i++)
			{
				if (strstri(file_name, sppMakeBinsExclusionStrings[i]))
				{
					printf("NOT binning %s... its filename (%s) matchines exclusion string %s\n",
						public_name, file_name, sppMakeBinsExclusionStrings[i]);
					bFound = true; 
					break;
				}
			}

			if (bFound)
			{
				continue;
			}
		}

		if(eaSize(&sppMapsNotToBin))
		{		
			int iIndex;
			const char *file_name = zmapInfoGetFilename(zminfo);
			if ((iIndex = eaFindString(&sppMapsNotToBin, public_name)) >= 0)
			{
				printf("NOT binning %s\n", public_name);
				continue;
			}
			else if(stringStartsWithItemInArray(&sppMapsNotToBin, file_name))
			{
				printf("NOT binning %s\n", public_name);
				continue;
			}
		}

		if (gpcMakeBinsAndExitNamespace)
		{
			char nameSpace[MAX_PATH], baseName[MAX_PATH];
			if(!resExtractNameSpace(public_name, nameSpace, baseName) || stricmp(nameSpace, gpcMakeBinsAndExitNamespace))
			{
				printf("NOT binning %s\n", public_name);
				continue;
			}
		}

		if ( g_ExcludeBinningNamespaceMaps && worldIsZoneMapInNamespace(zminfo) )
		{
			continue;
		}

		if (g_EnableFastBinCheck)
		{
			loadstart_printf("Checking if map \"%s\" needs binning...", public_name);
			if (!worldCellCheckNeedsBins(zminfo))
			{
				loadend_printf("(NO: not binning)");
				eaPush(&verify_zminfos, zminfo);
				continue;
			}
			loadend_printf("(YES: needs bins)");
		}
	
		eaPush(&zminfos, zminfo);
	}

	if (eaSize(&sppOnlyMapsToBin))
	{
		char *pErrorString = NULL;
		assert(pbFound);
		for (i=0; i < eaSize(&sppOnlyMapsToBin); i++)
		{
			if (!pbFound[i])
			{
				estrConcatf(&pErrorString, "%s%s", estrLength(&pErrorString) ? ", ":"", sppOnlyMapsToBin[i]);
			}
		}

		if (estrLength(&pErrorString))
		{
			if (spFilenameForOnlyMapToBinErrors)
			{
				FILE *pOutFile;
				mkdirtree_const(spFilenameForOnlyMapToBinErrors);
				pOutFile = fopen(spFilenameForOnlyMapToBinErrors, "wt");
				assertmsgf(pOutFile, "Couldn't open %s to writeout OnlyMapToBin errors... unknown maps: %s",
					spFilenameForOnlyMapToBinErrors, pErrorString);
				fprintf(pOutFile, "One or more maps specified in OnlyMapToBin were unknown: %s", pErrorString);
				fclose(pOutFile);
			}
			else
			{
				assertmsgf(0, "One or more maps specified in OnlyMapToBin were unknown: %s", pErrorString);
			}
		}

		estrDestroy(&pErrorString);
		free(pbFound);
	}

	eaQSortG(zminfos, cmpZoneMapInfoFilename);

	loadend_printf("Done. (Binning %d maps)", eaSize(&zminfos));


	if (do_verify && eaSize(&verify_zminfos) > 0)
	{
		int fail_count = 0;
		loadstart_printf("Validating %d up-to-date maps", eaSize(&verify_zminfos));

		for (i = 0; i < eaSize(&verify_zminfos); ++i)
		{
			const char *public_name;

			assertHeapValidateAll();

			zminfo = verify_zminfos[i];
			public_name = zmapInfoGetPublicName(zminfo);

			loadstart_printf("Verifying %s (%d/%d)", public_name, i+1, eaSize(&verify_zminfos));
			sprintf(consoleTitle, "Verifying map (%d/%d) (%s)", i+1, eaSize(&verify_zminfos), public_name);
			setConsoleTitle(consoleTitle);

			worldLoadZoneMapSyncWithPatching(zminfo, false, false, false);

			if (world_created_bins)
			{
				Errorf("Tom's fast dependency checker was wrong! Bins created for map %s.", public_name);
				filelog_printf("DepChecker.log", "Dependency check failed for map %s.", public_name);
				fail_count++;
			}

			utilitiesLibOncePerFrame(0, 1);

		

			loadend_printf("done");
		}

		if (fail_count)
			loadend_printf("done (%d FAILED VALIDATION!)", fail_count);
		else
			loadend_printf("done.");
	}
	

	loadstart_printf("Binning all maps");

	if (siDoMultiplexedMakeBinsAsMaster || siDoMultiplexedMakeBinsAsSlave)
	{
		if (siDoMultiplexedMakeBinsAsMaster)
		{
			SendStringToCB(CBSTRING_SUBSTATE, "Multiplexed master %s: Binning %d maps.", wlIsServer() ? "Server" : "Client", eaSize(&zminfos));

			BeginMultiplexedMakeBins_Master(zminfos);
		

			//every master is also its own slave, because why not, it might as well do work
			siDoMultiplexedMakeBinsAsSlave = 1;
			BeginMultiplexedMakeBins_Slave();

		
		}
		else
		{
			BeginMultiplexedMakeBins_Slave();
		}

		//for actual slaves, we never get here, as they exit when they are done. For the master, the master thread quits and the 
		//main thread continues, so we get here

		loadend_printf("done");


		eaDestroy(&zminfos);
		eaDestroy(&verify_zminfos);

		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	SendStringToCB(CBSTRING_SUBSTATE, "%s: Binning %d maps.", wlIsServer() ? "Server" : "Client", eaSize(&zminfos));

	if (sbDoMMPLDuringBinning)
	{
		mmpl();
	}

	for (i = 0; i < eaSize(&zminfos); ++i)
	{
		const char *public_name;
		
		//assertHeapValidateAll();

		zminfo = zminfos[i];
		public_name = zmapInfoGetPublicName(zminfo);

		loadstart_printf("Binning %s (%d/%d)", public_name, i+1, eaSize(&zminfos));
		SendStringToCB(CBSTRING_SUBSUBSTATE, "%s: Loading map %s (%d/%d)", wlIsServer() ? "Server" : "Client", public_name, i+1, eaSize(&zminfos));

		sprintf(consoleTitle, "Binning map (%d/%d) (%s)", i+1, eaSize(&zminfos), public_name);
		setConsoleTitle(consoleTitle);

		if (do_memdump)
		{
			char memlog_file[MAX_PATH];
			worldResetWorldGrid();
			memCheckDumpAllocsFileOnly("c:\\memlog.txt");
			sprintf(memlog_file, "c:\\memlogs\\%03d_mem.txt", i);
			fileCopy("c:\\memlog.txt", memlog_file);
		}

		worldLoadZoneMapSyncWithPatching(zminfo, false, false, false);

		utilitiesLibOncePerFrame(0, 1);

	
		loadend_printf("done");


	}

	if (sbDoMMPLDuringBinning)
	{
		mmpl();
	}

	loadend_printf("done");


	eaDestroy(&zminfos);
	eaDestroy(&verify_zminfos);

	PERFINFO_AUTO_STOP_FUNC();
#endif
}


static int delete_count;
static int delete_total_count;

static FileScanAction removeBin(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char		buf[1000];
	char		fname[1000];

	PERFINFO_AUTO_START_FUNC();

	if (strEndsWith(data->name, ".bin") || strEndsWith(data->name, ".deps")
		|| strEndsWith(data->name, ".info") || strEndsWith(data->name, ".region_bounds")
		|| strEndsWith(data->name, ".tlayer.bin") || strEndsWith(data->name, ".hogg"))
	{
		sprintf(fname,"%s/%s",dir,data->name);
		verbose_printf("removing bin %s\n", fname);
		if (delete_count > 100)
		{
			printf(".");
			delete_count = 0;
		}
		fileLocateWrite(fname, buf);
		fileForceRemove(buf);
		++delete_count;
		++delete_total_count;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return FSA_EXPLORE_DIRECTORY;
}

void worldGridDeleteAllBins(void)
{
	// TomY TODO - Namespace bins
	delete_total_count = 0;
	loadstart_printf("Removing old bins..");
	fileScanAllDataDirs("bin/geobin", removeBin, NULL);
	if (wlIsServer())
		fileScanAllDataDirs("server/bin/geobin", removeBin, NULL);
	loadend_printf(" done (%d deleted).", delete_total_count);
}

static FileScanAction removeOutdatedBin(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char		buf[1000];
	char		fname[1000];

	PERFINFO_AUTO_START_FUNC();

	if (data->attrib & _A_SUBDIR) {
		PERFINFO_AUTO_STOP_FUNC();
		return FSA_EXPLORE_DIRECTORY;
	}

	if (strEndsWith(data->name, ".tcl") || strEndsWith(data->name, ".thm")
		|| strEndsWith(data->name, ".tnm")
		|| strEndsWith(data->name, ".trn") || strEndsWith(data->name, ".eci")
		|| strEndsWith(data->name, ".atl") || strEndsWith(data->name, ".tmp")
		|| strEndsWith(data->name, ".bnd") || strEndsWith(data->name, ".occ")
		|| strEndsWith(data->name, ".bounds")
		|| strStartsWith(data->name, "client_atmospherics.bin")
		|| strStartsWith(data->name, "client_info.") || strStartsWith(data->name, "server_info.")
		|| strStartsWith(data->name, "client_strings.bin") || strStartsWith(data->name, "server_strings.bin")
		|| (strstri(dir, "/region") && strstri(dir, "/level")/* && !(data->attrib & _A_SUBDIR)*/))
	{
		sprintf(fname,"%s/%s",dir,data->name);
		verbose_printf("removing outdated bin %s\n", fname);
		if (delete_count > 100)
		{
			printf(".");
			delete_count = 0;
		}
		fileLocateWrite(fname, buf);
		//assert(!dirExists(buf));
		fileForceRemove(buf);
		fileUpdateHoggAfterWrite(fname, NULL, 0);
		++delete_count;
		++delete_total_count;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return FSA_EXPLORE_DIRECTORY;
}

void worldGridDeleteAllOutdatedBins(void)
{
#if !PLATFORM_CONSOLE
	HogFile *hog_file;
	char fullpath[MAX_PATH];
	delete_total_count = 0;
	fileLocateWrite("piggs/bin.hogg", fullpath);
	hog_file = hogFileRead(fullpath, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	hogFileLock(hog_file);
	loadstart_printf("Removing outdated bins..");
	// TomY TODO - Namespace bins
	fileScanAllDataDirs("bin/geobin", removeOutdatedBin, NULL);
	if (wlIsServer())
		fileScanAllDataDirs("server/bin/geobin", removeOutdatedBin, NULL);
	loadend_printf(" done (%d deleted).", delete_total_count);
	hogFileUnlock(hog_file);
	hogFileDestroy(hog_file, true);
#endif
}

bool worldGridShouldDeleteUntouchedFiles(void)
{
	return (eaSize(&sppOnlyMapsToBin) == 0 && eaSize(&sppMapsNotToBin) == 0 && eaSize(&sppMakeBinsExclusionStrings) == 0 && !gpcMakeBinsAndExitNamespace);
}


void worldGridMakeAllBins(bool remove, bool do_memdump, bool do_validate)
{
	PERFINFO_AUTO_START_FUNC();

	if (remove)
		worldGridDeleteAllBins();

	// TomY TODO rebuild object library bin here?

	loadstart_printf("Binning maps..\n");
		worldResetWorldGrid();
		binAllMapsInList(do_memdump, do_validate);
	loadend_printf("");

	PERFINFO_AUTO_STOP_FUNC();
}


void worldGridDeleteBinsForZoneMap(const char *mapName)
{
	char binPath[MAX_PATH];
	delete_total_count = 0;
	loadstart_printf("Removing bins for %s..", mapName);
	// TomY TODO namespaces
	sprintf(binPath, "bin/%s",worldMakeBinDir(mapName));
	fileScanAllDataDirs(binPath, removeBin, NULL);
	sprintf(binPath, "tempbin/%s",worldMakeBinDir(mapName)+7);
	fileScanAllDataDirs(binPath, removeBin, NULL);
	loadend_printf(" done (%d deleted).", delete_total_count);
}

static bool g_fixup_map_dry_run = false;

static void fixupMap(void)
{
	int j, k;
	GroupDefLib *obj_lib = objectLibraryGetDefLib();

	// TomY TODO TEST
	for (j = 0; j < eaSize(&world_grid.maps); ++j)
	{
		ZoneMap *zmap = world_grid.maps[j];
		if (!zmap)
			continue;
		for (k = 0; k < eaSize(&zmap->layers); ++k)
		{
			char lockee[256];
			ZoneMapLayer *layer = zmap->layers[k];
			GroupDefLib *def_lib = layerGetGroupDefLib(layer);
			if (!def_lib || !def_lib->was_fixed_up)
				continue;
			filelog_printf("fixupMaps.log", "%s: Fixing layer %s...\n", zmapInfoGetPublicName(NULL), layerGetName(layer));
			printf(" Fixing up layer %s...", layerGetFilename(layer));
			if (!layerAttemptLock(layer, SAFESTR(lockee), g_fixup_map_dry_run))
			{
				filelog_printf("fixupMaps.log", "%s: FAILED to lock layer %s (locked by %s).\n", zmapInfoGetPublicName(NULL), layerGetName(layer), lockee);
				continue;
			}
			if (!g_fixup_map_dry_run)
			{
				layer->layer_mode = LAYER_MODE_EDITABLE;
				if (!layerSave(layer, true, false))
				{
					filelog_printf("fixupMaps.log", "%s: FAILED to save layer %s.\n", zmapInfoGetPublicName(NULL), layerGetName(layer));
				}
				else
				{
					filelog_printf("fixupMaps.log", "%s: SUCCESS saving layer %s.\n", zmapInfoGetPublicName(NULL), layerGetName(layer));
				}
			}
		}
	}

	if (obj_lib->was_fixed_up)
	{
		GroupDef **lib_defs = groupLibGetDefEArray(obj_lib);
		for (j = 0; j < eaSize(&lib_defs); j++)
		{
			if (lib_defs[j]->was_fixed_up)
			{
				GroupDef *editing_copy = objectLibraryGetEditingCopy(lib_defs[j], true, false);
				printf(" Fixing up object library def %s...", editing_copy->name_str);
				objectLibraryGroupSetEditable(editing_copy);
				if (groupIsEditable(editing_copy))
					groupDefModify(editing_copy, 0, true);
			}
		}
	}

	objectLibrarySave(NULL);
}

void worldGridLoadAllMaps(voidVoidFunc callback)
{
	int i = 0;
	int iCount = worldGetZoneMapCount();
	RefDictIterator zmap_iter;
	ZoneMapInfo *zminfo;
	char consoleTitle[256];

	PERFINFO_AUTO_START_FUNC();

	worldResetWorldGrid();

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter))
	{
		const char *public_name = zmapInfoGetPublicName(zminfo);

		if (!public_name)
			continue;

		sprintf(consoleTitle, "Loading map (%d/%d) (%s)", ++i, iCount, public_name);

		setConsoleTitle(consoleTitle);

		worldLoadZoneMapSyncWithPatching(zminfo, false, true, true);

		callback();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void worldForEachMap(mapFunc callback)
{
	RefDictIterator zmap_iter;
	ZoneMapInfo *zminfo;

	PERFINFO_AUTO_START_FUNC();

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter))
	{
		callback(zminfo);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void worldGridFixupAllMaps(bool dry_run)
{
	g_fixup_map_dry_run = dry_run;

	if (eaSize(&sppOnlyMapsToBin))
	{
		int i;

		loadstart_printf("Fixing up %d maps because of onlyMapToBin being set", eaSize(&sppOnlyMapsToBin));

		for (i=0; i < eaSize(&sppOnlyMapsToBin); i++)
		{
			if (worldLoadZoneMapByNameSyncWithPatching(sppOnlyMapsToBin[i]))
			{
				filelog_printf("fixupMaps.log", "*** Fixing up single map%s: %s ***\n", dry_run ? " (dry run)" : "", sppOnlyMapsToBin[i]);
				fixupMap();
				filelog_printf("fixupMaps.log", "*** Done. ***\n");
			}
		}
		loadend_printf("done");
		return;
	}

	if (eaSize(&sppMapsNotToBin) || eaSize(&sppMakeBinsExclusionStrings) || gpcMakeBinsAndExitNamespace)
	{
		RefDictIterator zmap_iter;
		ZoneMapInfo *zminfo;

		loadstart_printf("Fixing up unknown # of maps because of sppMapsNotToBin or sppMakeBinsExclusionStrings or gpcMakeBinsAndExitNamespace being set");

		worldGetZoneMapIterator(&zmap_iter);
		while (zminfo = worldGetNextZoneMap(&zmap_iter))
		{
			int iIndex;
			const char *file_name = zmapInfoGetFilename(zminfo);
			const char *public_name = zmapInfoGetPublicName(zminfo);

			if (!public_name)
				continue;

			if ((iIndex = eaFindString(&sppMapsNotToBin, public_name)) >= 0)
			{
				continue;
			}
			else if(stringStartsWithItemInArray(&sppMapsNotToBin, file_name))
			{
				continue;
			}
			else if (gpcMakeBinsAndExitNamespace)
			{
				char nameSpace[MAX_PATH], baseName[MAX_PATH];
				if(!resExtractNameSpace(public_name, nameSpace, baseName) || stricmp(nameSpace, gpcMakeBinsAndExitNamespace))
				{
					continue;
				}
			}
			else
			{
				bool bFound = false;
				int i;

				for (i=0; i < eaSize(&sppMakeBinsExclusionStrings); i++)
				{
					if (strstri(file_name, sppMakeBinsExclusionStrings[i]))
					{
						printf("NOT binning %s... its filename (%s) matches exclusion string %s\n",
							public_name, file_name, sppMakeBinsExclusionStrings[i]);
						bFound = true; 
						break;
					}
				}
				if (bFound)
				{
					continue;
				}

				if (worldLoadZoneMapByNameSyncWithPatching(public_name))
				{
					filelog_printf("fixupMaps.log", "*** Fixing up single map%s: %s ***\n", dry_run ? " (dry run)" : "", public_name);
					fixupMap();
					filelog_printf("fixupMaps.log", "*** Done. ***\n");
				}
			}
		}

		loadend_printf("done");
		return;
	}

	loadstart_printf("Fixing up maps..\n");
	filelog_printf("fixupMaps.log", "*** Fixing up all maps%s. ***\n", dry_run ? " (dry run)" : "");
	worldGridLoadAllMaps(fixupMap);
	filelog_printf("fixupMaps.log", "*** Done. ***\n");
	loadend_printf("");
}

#ifndef NO_EDITORS

static void getMapLayerSizesInList(void)
{
	int i = 0, j, k, l, iCount = worldGetZoneMapCount();
	RefDictIterator zmap_iter;
	ZoneMapInfo *zminfo;
	char consoleTitle[256];
	FILE *fOut = fopen("C:\\MapLayerSizes.csv", "w");

	PERFINFO_AUTO_START_FUNC();

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter))
	{
		const char *public_name = zmapInfoGetPublicName(zminfo);
		const char *file_name = zmapInfoGetFilename(zminfo);
		U32 before_map_size, after_map_size;
		TerrainEditorSource *source;

		if (!public_name)
			continue;

		if (eaSize(&sppOnlyMapsToBin) && eaFindString(&sppOnlyMapsToBin, public_name) == -1)
		{
			printf("Skipping calc sizes of map %s\n", public_name);
			continue;
		}

		if(eaSize(&sppMapsNotToBin))
		{		
			int iIndex;
			if ((iIndex = eaFindString(&sppMapsNotToBin, public_name)) >= 0)
			{
				printf("Skipping calc sizes of map %s\n", public_name);
				continue;
			}
			else if(stringStartsWithItemInArray(&sppMapsNotToBin, file_name))
			{
				printf("Skipping calc sizes of map %s\n", public_name);
				continue;
			}
		}

		if (eaSize(&sppMakeBinsExclusionStrings))
		{
			bool bFound = false;

			for (j=0; j < eaSize(&sppMakeBinsExclusionStrings); j++)
			{
				if (strstri(file_name, sppMakeBinsExclusionStrings[j]))
				{
					printf("Skipping calc sizes of map %s\n", public_name);
					bFound = true; 
					break;
				}
			}
			if (bFound)
			{
				continue;
			}
		}

		if (gpcMakeBinsAndExitNamespace)
		{
			char nameSpace[MAX_PATH], baseName[MAX_PATH];
			if(!resExtractNameSpace(public_name, nameSpace, baseName) || stricmp(nameSpace, gpcMakeBinsAndExitNamespace))
			{
				printf("Skipping calc sizes of map %s\n", public_name);
				continue;
			}
		}

		source = terrainSourceInitialize();

		sprintf(consoleTitle, "Calculating sizes for map (%d/%d) (%s)", ++i, iCount, public_name);

		setConsoleTitle(consoleTitle);

		worldLoadZoneMapSyncWithPatching(zminfo, false, true, true);

		before_map_size = terrainGetProcessMemory();

		for (j = 0; j < eaSize(&world_grid.maps); ++j)
		{
			ZoneMap *zmap = world_grid.maps[j];
			if (!zmap)
				continue;
			for (k = 0; k < eaSize(&zmap->layers); ++k)
			{
				ZoneMapLayer *layer = zmap->layers[k];
				TerrainEditorSourceLayer *source_layer;
				int total_block_area = 0;
				U32 before_size, after_size;

				layerSetMode(layer, LAYER_MODE_GROUPTREE, false, false, true);
				before_size = terrainGetProcessMemory();

				source_layer = terrainSourceAddLayer(source, layer);
				terrainSourceLoadLayerData(source_layer, false, false, NULL, 0);

				for (l = 0; l < eaSize(&source_layer->blocks); l++)
				{
					TerrainBlockRange *range = source_layer->blocks[l];
					int area = (range->range.max_block[0]+1-range->range.min_block[0]) *
						(range->range.max_block[2]+1-range->range.min_block[2]);
					total_block_area += area;
				}

				after_size = terrainGetProcessMemory();

				printf("Layer %s size %d area %d\n", layer->name, after_size-before_size, total_block_area);
				fprintf(fOut, "%s,%s,%d,%d\n", public_name, layer->name, total_block_area, after_size-before_size);
				
				terrainSourceUnloadLayerData(source_layer);

				layerUnload(layer);
			}
		}

		terrainSourceDestroy(source);

		after_map_size = terrainGetProcessMemory();
		if (after_map_size > before_map_size)
			printf("Leaked %d bytes of memory.\n", after_map_size - before_map_size);
	}

	fclose(fOut);

	PERFINFO_AUTO_STOP_FUNC();

}

void worldGridCalculateMapLayerSizes(void)
{
	loadstart_printf("Calculating layer sizes for maps..\n");
	worldResetWorldGrid();
	getMapLayerSizesInList();
	loadend_printf("");
}

void worldGridCalculateDependencies(char *filename)
{
#if !PLATFORM_CONSOLE
	RefDictIterator zmap_iter;
	char search_file[CRYPTIC_MAX_PATH];
	ZoneMapInfo *zminfo;
	char consoleTitle[256];
	int matches = 0;
	char temp_file[CRYPTIC_MAX_PATH];
	FILE *fOut;

	sprintf(temp_file, "%s/deps.txt", fileTempDir());
	fOut = fopen(temp_file, "w");

	fileLocateWrite(filename, search_file);
	forwardSlashes(search_file);

	fprintf(fOut, "SEARCHING FOR %s...\n", search_file);

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter))
	{
		bool found = false;
		BinFileList *file_list = StructAlloc(parse_BinFileList);
		const char *public_name = zmapInfoGetPublicName(zminfo);
		char deps_filename[CRYPTIC_MAX_PATH];
		char dep_absolute[CRYPTIC_MAX_PATH];
		char base_dir[MAX_PATH];

		sprintf(consoleTitle, "Loading map %s", public_name);
		setConsoleTitle(consoleTitle);

		worldGetTempBaseDir(zmapInfoGetFilename(zminfo), SAFESTR(base_dir));
		sprintf(deps_filename, "%s/client_world_cells_deps.bin", base_dir);
		if (ParserOpenReadBinaryFile(NULL, deps_filename, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
		{
			FOR_EACH_IN_EARRAY(file_list->source_files, BinFileEntry, entry)
			{
				fileLocateWrite(entry->filename, dep_absolute);
				if (!stricmp(dep_absolute, search_file))
				{
					fprintf(fOut, "Matches client: %s (%s)\n", public_name, zmapInfoGetFilename(zminfo));
					matches++;
					found = true;
					break;
				}
			}
			FOR_EACH_END;
		}
		else
		{
			fprintf(fOut, "Could not open deps file: %s\n", deps_filename);
		}

		if (!found)
		{
			worldGetTempBaseDir(zmapInfoGetFilename(zminfo), SAFESTR(base_dir));
			sprintf(deps_filename, "%s/server_world_cells_deps.bin", base_dir);
			if (ParserOpenReadBinaryFile(NULL, deps_filename, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
			{
				FOR_EACH_IN_EARRAY(file_list->source_files, BinFileEntry, entry)
				{
					fileLocateWrite(entry->filename, dep_absolute);
					if (!stricmp(dep_absolute, search_file))
					{
						fprintf(fOut, "Matches server: %s (%s)\n", public_name, zmapInfoGetFilename(zminfo));
						matches++;
						found = true;
						break;
					}
				}
				FOR_EACH_END;
			}
			else
			{
				fprintf(fOut, "Could not open deps file: %s\n", deps_filename);
			}
		}

		StructDestroy(parse_BinFileList, file_list);
	}
	fprintf(fOut, "DONE. (%d matches)\n", matches);
	fclose(fOut);
	{
		char cmd[MAX_PATH*2];
		sprintf(cmd, "notepad.exe \"%s\"", temp_file);
		system_detach(cmd, 0, 0);
	}
#endif
}

#endif

static ZoneMapInfo **sppZoneMapInfosForMultiplexedMakeBins = NULL;

static ZoneMapInfo **sppZoneMapsInProgress = NULL;

static int siStartingNumMaps = 0;

typedef struct MultiplexedMakeBinsUserData
{
	int iSlaveIndex;
	int iSlavePID;
} MultiplexedMakeBinsUserData;

static int siNextSlaveIndex = 1;

//commands which, if passed in to the master, should NOT be passed to the slave
static char *cmdsToRemove[] = 
{
	"DoMultiplexedMakeBinsAsMaster",
	"XMLTiming",
	"XMLTimingFiltered",
	"FolderForMultiplexedSlaveLogs",
};


static void ApplyMyCommandLineToSlave(char **ppSlaveCmdLine)
{
	const char *pCmdLine = GetCommandLineWithoutExecutable();
	char **ppCommands = NULL;
	int i, j;

	DivideString(pCmdLine, "-", &ppCommands, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	if (!eaSize(&ppCommands))
	{
		return;
	}

	for (i = eaSize(&ppCommands) - 1; i >= 0; i--)
	{
		bool bRemove = false;
		for (j = 0; j < ARRAY_SIZE(cmdsToRemove); j++)
		{
			if (strStartsWith(ppCommands[i], cmdsToRemove[j]))
			{
				bRemove = true;
				break;
			}
		}

		if (bRemove)
		{
			free(ppCommands[i]);
			eaRemove(&ppCommands, i);
		}
	}


	for (i = 0; i < eaSize(&ppCommands); i++)
	{
		estrConcatf(ppSlaveCmdLine, " -%s ", ppCommands[i]);
	}

}

static void LaunchMultiplexedMakeBinsSlave(void)
{
	char *pSlaveCmdLine = NULL;

	siNextSlaveIndex++;

	estrPrintf(&pSlaveCmdLine, "%s", getExecutableName());
	backSlashes(pSlaveCmdLine);
	estrConcatf(&pSlaveCmdLine,  " %s -DoMultiplexedMakeBinsAsSlave %d -NoSharedMemory -MultiplexedMakeBinsMasterProcessPID %d -ForceReadBinFilesForMultiplexedMakebins -ProductionModeBins ", CBSupport_GetSpawningCommandLine(), siNextSlaveIndex, getpid());

	ApplyMyCommandLineToSlave(&pSlaveCmdLine);

	if (spFolderForMultiplexedSlaveLogs)
	{
		char snippet[CRYPTIC_MAX_PATH * 2];
		sprintf(snippet, "-ForkPrintfsToFile %s\\%s_Makebins_Slave_%d.txt", spFolderForMultiplexedSlaveLogs,
			GlobalTypeToName(GetAppGlobalType()), siNextSlaveIndex);
		backSlashes(snippet);
		estrConcatf(&pSlaveCmdLine, " %s ", snippet);
	}


	system_detach(pSlaveCmdLine, false, false);

	estrDestroy(&pSlaveCmdLine);
}

static void KillSlaveCB(NetLink* link, MultiplexedMakeBinsUserData *pUserData)
{

	Packet *pOutPack = pktCreate(link, MULTIPLEXED_MAKEBINS_MASTER_TO_SLAVE_ALL_DONE);
	pktSend(&pOutPack);
	linkFlush(link);
	
}

static void MakebinsMasterDisconnectCB(NetLink *link, MultiplexedMakeBinsUserData *pUserData)
{
	assertmsgf(0, "Lost connection to slave %d (pid %d), binning has failed", pUserData->iSlaveIndex, pUserData->iSlavePID);
}

static void MasterUpdateConsoleTitle(void)
{
	char consoleTitle[512];
	sprintf(consoleTitle, "MASTER - %d workers (counting me), %d/%d maps remain (%d in progress)",
		siNextSlaveIndex, eaSize(&sppZoneMapInfosForMultiplexedMakeBins), siStartingNumMaps, eaSize(&sppZoneMapsInProgress));
	SetConsoleTitle_UTF8(consoleTitle);
}

static RelationshipGrouper *spMasterMultiplexerGrouper;


static bool CanBeBinnedNow(ZoneMapInfo *pMap)
{
	int i;

	for (i = 0; i < eaSize(&sppZoneMapsInProgress); i++)
	{
		if (RelationshipGrouper_AreTwoItemsRelated(spMasterMultiplexerGrouper, 
			pMap->map_name, sppZoneMapsInProgress[i]->map_name))
		{
			printf("Can't bin %s right now... %s is already being binned\n",
				pMap->map_name, sppZoneMapsInProgress[i]->map_name);
			return false;
		}
	}

	return true;
}


static ZoneMapInfo *GetNextZoneMapToSend(void)
{
	int iCount;

	if (!(iCount = eaSize(&sppZoneMapInfosForMultiplexedMakeBins)))
	{
		return NULL;
	}

	while (iCount)
	{
		ZoneMapInfo *pMaybe = eaRemove(&sppZoneMapInfosForMultiplexedMakeBins, 0);
		if (CanBeBinnedNow(pMaybe))
		{
			return pMaybe;
		}

		eaPush(&sppZoneMapInfosForMultiplexedMakeBins, pMaybe);
		iCount--;
	}

	return NULL;
}
	



static void MakebinsMasterHandleMsg(Packet *pPack, int iCmd, NetLink *link, MultiplexedMakeBinsUserData *pUserData)
{
	int iPID;
	int iSlaveID;

	switch (iCmd)
	{
	xcase MULTIPLEXED_MAKEBINS_SLAVE_TO_MASTER_REQUESTING_ZMAP:
		iSlaveID = pktGetBits(pPack, 32);
		iPID = pktGetBits(pPack, 32);

		if (!pUserData->iSlavePID)
		{
			pUserData->iSlaveIndex = iSlaveID;
			
			SendStringToCB(CBSTRING_COMMENT, "Got first contact from slave %d. Its PID is %d. %s.",
				pUserData->iSlaveIndex, iPID, iPID == getpid() ? "It's actually me!" : "It's not me.");
					
			pUserData->iSlavePID = iPID;
		}

		if (eaSize(&sppZoneMapInfosForMultiplexedMakeBins))
		{
			ZoneMapInfo *pZoneMapToSend = GetNextZoneMapToSend();
			if (pZoneMapToSend)
			{
				Packet *pOutPack;
				SendStringToCB(CBSTRING_COMMENT, "Sending %s to slave %d", pZoneMapToSend->map_name, pUserData->iSlaveIndex);
				pOutPack = pktCreate(link, MULTIPLEXED_MAKEBINS_MASTER_TO_SLAVE_HERE_IS_ZMAP_TO_BIN);
				pktSendString(pOutPack, pZoneMapToSend->map_name);
				pktSend(&pOutPack);
				eaPush(&sppZoneMapsInProgress, pZoneMapToSend);
				MasterUpdateConsoleTitle();
			}
			else
			{
				Packet *pOutPack;
				SendStringToCB(CBSTRING_COMMENT, "No binnable maps right now... telling slave %d to wait",
					pUserData->iSlaveIndex);
				pOutPack = pktCreate(link, MULTIPLEXED_MAKEBINS_MASTER_TO_SLAVE_HERE_IS_ZMAP_TO_BIN);
				pktSendString(pOutPack, "");
				pktSend(&pOutPack);
			}
		}

	xcase MULTIPLEXED_MAKEBINS_SLAVE_TO_MASTER_REPORTING_COMPLETION:
		{
			char *pMapName = pktGetStringTemp(pPack);
			int i;

			SendStringToCB(CBSTRING_COMMENT, "Slave %d reports completion of map %s", pUserData->iSlaveIndex, pMapName);

			for (i = 0; i < eaSize(&sppZoneMapsInProgress); i++)
			{
				if (stricmp(sppZoneMapsInProgress[i]->map_name, pMapName) == 0)
				{
					eaRemoveFast(&sppZoneMapsInProgress, i);
					MasterUpdateConsoleTitle();
					break;
				}
			}
		}

	xcase MULTIPLEXED_MAKEBINS_SLAVE_TO_MASTER_TOUCHED_BIN_FILE:
		{
			char *pFileName = pktGetStringTemp(pPack);
			binNotifyTouchedOutputFile_Inner(pFileName);
		}
	}
}
	

static void MultiplexedMakeBins_MasterThread_inner(void)
{
	NetComm *pMultiplexedMakeBinsComm = commCreate(0, 0);
	NetListen *pListen = commListen(pMultiplexedMakeBinsComm,LINKTYPE_SHARD_CRITICAL_1MEG, LINK_FORCE_FLUSH,DEFAULT_MULTIPLEXED_MAKEBINS_PORT,
		MakebinsMasterHandleMsg,NULL,MakebinsMasterDisconnectCB,sizeof(MultiplexedMakeBinsUserData));
	int i;
	RelationshipGroup **ppGroups = NULL;



	printf("In master thread... have %d maps to bin, going to launch %d children\n", eaSize(&sppZoneMapInfosForMultiplexedMakeBins), siDoMultiplexedMakeBinsAsMaster);

	siStartingNumMaps = eaSize(&sppZoneMapInfosForMultiplexedMakeBins);

	printf("Calculating relationships... for %d maps\n", siStartingNumMaps);
	spMasterMultiplexerGrouper = RelationshipGrouper_Create();

	FOR_EACH_IN_EARRAY(sppZoneMapInfosForMultiplexedMakeBins, ZoneMapInfo, pZoneMap)
	{
		FOR_EACH_IN_EARRAY(pZoneMap->secondary_maps, SecondaryZoneMap, pSecondaryZoneMap)
		{
			RelationshipGrouper_AddRelationship(spMasterMultiplexerGrouper, pZoneMap->map_name, pSecondaryZoneMap->map_name);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	RelationshipGrouper_GenerateGroups(spMasterMultiplexerGrouper, &ppGroups);
	printf("Found %d groups\n", eaSize(&ppGroups));
	FOR_EACH_IN_EARRAY(ppGroups, RelationshipGroup, pGroup)
	{
		printf("Group ID %d: ", pGroup->iIndex);
		for (i = 0; i < eaSize(&pGroup->ppNames); i++)
		{
			printf("%s%s", i == 0 ? "" : ", ", pGroup->ppNames[i]);
		}
		printf("\n\n");
	}
	FOR_EACH_END;


	SendStringToCB(CBSTRING_COMMENT, "Multiplexed makebins master thread is active... going to create %d child processes (plus this process will also act as a slave)",
		siDoMultiplexedMakeBinsAsMaster);

	for (i = 0; i < siDoMultiplexedMakeBinsAsMaster; i++)
	{
		LaunchMultiplexedMakeBinsSlave();
	}

	while (1)
	{

		if (eaSize(&sppZoneMapInfosForMultiplexedMakeBins) == 0 && eaSize(&sppZoneMapsInProgress) == 0)
		{
			SendStringToCB(CBSTRING_COMMENT, "Multiplexed makebins master thread complete!");
			
			linkIterate(pListen, KillSlaveCB);
			
			if (sbDoMMPLDuringBinning)
			{
				mmpl();
			}

			return;
		}

		commMonitor(pMultiplexedMakeBinsComm);
		Sleep(1);
	}
}




static DWORD WINAPI MultiplexedMakeBins_MasterThread(LPVOID lpParam)
{
	EXCEPTION_HANDLER_BEGIN

	if (sbDoMMPLDuringBinning)
	{
		mmpl();
	}

	MultiplexedMakeBins_MasterThread_inner();

	if (sbDoMMPLDuringBinning)
	{
		mmpl();
	}

	EXCEPTION_HANDLER_END;
	return 0;
}


void BeginMultiplexedMakeBins_Master(ZoneMapInfo **ppZoneMapInfos)
{
	sppZoneMapInfosForMultiplexedMakeBins = ppZoneMapInfos;
	assert(tmCreateThread(MultiplexedMakeBins_MasterThread, NULL));


}




static char sMapNameToBin[1024] = "";
static bool sbRequestedAMap = false;
static bool sbIAmTheMaster = false;
static bool sbSlaveIsDone = false;
static U32 siNextTimeToAskForMapToBin = 0;

static void MakebinsSlaveDisconnectCB(NetLink *link, MultiplexedMakeBinsUserData *pUserData)
{
	if (!sbIAmTheMaster)
	{
		printf("Lost connection to master... exiting\n");
		exit(-1);
	}
}


static void MakebinsSlaveHandleMsg(Packet *pPack, int iCmd, NetLink *link, MultiplexedMakeBinsUserData *pUserData)
{
	switch (iCmd)
	{
	xcase MULTIPLEXED_MAKEBINS_MASTER_TO_SLAVE_HERE_IS_ZMAP_TO_BIN:
		sbRequestedAMap = false;
		pktGetString(pPack, SAFESTR(sMapNameToBin));
		if (sMapNameToBin[0])
		{
			printf("Got packet, told to bin %s\n", sMapNameToBin);
		}
		else
		{
			printf("Got packet, told to wait and ask again\n");
			siNextTimeToAskForMapToBin = timeSecondsSince2000() + 5;
		}
	xcase MULTIPLEXED_MAKEBINS_MASTER_TO_SLAVE_ALL_DONE:
		printf("Master says we're all done... hurray!\n");
		if (sbIAmTheMaster)
		{
			sbSlaveIsDone = true;
		}
		else
		{
			ReallyExitRightNow();
		}
	}
}

static NetLink *spLinkToMaster = NULL;

void BeginMultiplexedMakeBins_Slave(void)
{
	
	//in slave mode, we are launched with gbProductionModeBins true so that we'd load "normal" bins as fast as possible, since we
	//know they were all just made by the master binner. But once we get here, turn that off so that the map binning isn't affected
	gbProductionModeBins = false;
	gbForceReadBinFilesForMultiplexedMakebins = false;

	sbIAmTheMaster = (siMultiplexedMakeBinsMasterProcessPID == 0);

	printf("In MultiplexedMakeBins_Slave code... about to try to connect to master\n");
	
	//have to disable logging (awkward though that is) or else we'll end up fighting over the same log file. Fortuantely
	//console output will generally be getting saved, which is how CBs do their logging in general anyhow
	logDisableLogging(true);

	spLinkToMaster = commConnectWait(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, "localhost", DEFAULT_MULTIPLEXED_MAKEBINS_PORT,
		MakebinsSlaveHandleMsg, NULL, MakebinsSlaveDisconnectCB, 0, 10.0f);

	if (!spLinkToMaster)
	{
		printf("Connection to master failed... exiting in disgrace\n");
		exit(-1);
	}
	else
	{
		printf("Connection to master succeeded... waiting for maps to process\n");
	}

	if (siMultiplexedMakeBinsMasterProcessPID)
	{
		char *pExeName = getExecutableName();
		char *pShortExeName = NULL;
		estrGetDirAndFileName(pExeName, NULL, &pShortExeName);
		UtilitiesLib_KillMeWhenAnotherProcessDies(pShortExeName, siMultiplexedMakeBinsMasterProcessPID);
		estrDestroy(&pShortExeName);
	}

	if (!sbIAmTheMaster)
	{
		setConsoleTitle("Slave multiplexed map binner");
	}

	while (!sbSlaveIsDone)
	{
		Packet *pOutPack;

		Sleep(1);
		commMonitor(commDefault());
		utilitiesLibOncePerFrame(REAL_TIME);

		if (!sbRequestedAMap && !sMapNameToBin[0] && 
			(!siNextTimeToAskForMapToBin || siNextTimeToAskForMapToBin <= timeSecondsSince2000()))
		{
			pOutPack = pktCreate(spLinkToMaster, MULTIPLEXED_MAKEBINS_SLAVE_TO_MASTER_REQUESTING_ZMAP);
			pktSendBits(pOutPack, 32, siDoMultiplexedMakeBinsAsSlave);
			pktSendBits(pOutPack, 32, getpid());
			pktSend(&pOutPack);
			sbRequestedAMap = true;
			printf("Requesting a map to bin...\n");
		}

		if (sMapNameToBin[0])
		{
			ZoneMapInfo *pMap = zmapInfoGetByPublicName(sMapNameToBin);
			if (!pMap)
			{
				printf("We were told to bin map %s... but it doesn't seem to exist\n", sMapNameToBin);
				sMapNameToBin[0] = 0;
			}
			else
			{
				if (!sbIAmTheMaster)
				{
					char consoleTitle[512];
					sprintf(consoleTitle, "Slave %d binning %s", siDoMultiplexedMakeBinsAsSlave, sMapNameToBin);
					setConsoleTitle(consoleTitle);
				}
				printf("Told to bin map %s... going to do it\n", sMapNameToBin);
				worldLoadZoneMapSyncWithPatching(pMap, false, false, false);

				//among other things, this deletes any .bak files created during the previous function, and these .bak files
				//confuse the makebins "do I know about all files that were created" code.
				if (!sbIAmTheMaster)
				{
					worldResetWorldGrid();
				}

				printf("Done\n");

				pOutPack = pktCreate(spLinkToMaster, MULTIPLEXED_MAKEBINS_SLAVE_TO_MASTER_REPORTING_COMPLETION);
				pktSendString(pOutPack, sMapNameToBin);
				pktSend(&pOutPack);
				sMapNameToBin[0] = 0;


			}
		}
	}

	linkFlushAndClose(&spLinkToMaster, "Done with multiplexed makebins");
}

bool WorldGrid_DoingMultiplexedMakeBinsAsSlave(void)
{
	return !!siDoMultiplexedMakeBinsAsSlave;
}


bool OVERRIDE_LATELINK_binNotifyTouchedOutputFile_Multiplexed(const char *filename)
{
	if (spLinkToMaster)
	{
		Packet *pPak = pktCreate(spLinkToMaster, MULTIPLEXED_MAKEBINS_SLAVE_TO_MASTER_TOUCHED_BIN_FILE);
		pktSendString(pPak, filename);
		pktSend(&pPak);

		return true;
	}
	return false;
}

#undef exit
void ReallyExitRightNow(void)
{
	exit(0);
}
