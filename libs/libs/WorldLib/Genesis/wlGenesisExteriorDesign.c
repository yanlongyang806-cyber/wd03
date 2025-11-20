#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include "wlTerrainSource.h"
#include "wlTerrainBrush.h"
#include "wlGenesis.h"
#include "wlGenesisExterior.h"
#include "wlGenesisExteriorDesign.h"

#include "WorldGrid.h"
#include "error.h"
#include "ObjectLibrary.h"
#include "fileutil.h"
#include "ResourceInfo.h"
#include "FolderCache.h"
#include "ZoneMap.h"
#include "StringCache.h"
#include "GroupProperties.h"

static DictionaryHandle ecotype_room_dict = NULL;
static bool ecotype_room_dict_loaded = false;

#define GEN_WATER_PLANE_COUNT 10
#define GEN_WATER_PLANE_SIZE 1000.0f
#define GEN_WATER_PLANE_CNTR 1024.0f
#define GEN_WATER_PLANE_OFFSET (((GEN_WATER_PLANE_SIZE*GEN_WATER_PLANE_COUNT)/2.0f)-GEN_WATER_PLANE_CNTR);
#define GEN_WALL_COLLISION_DIST 150.0f

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

void genesisReloadEcosystem(const char* relpath, int when)
{
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(GENESIS_ECOTYPE_DICTIONARY) );
}

// Fills in the filename_no_path of the POIFile during load
AUTO_FIXUPFUNC;
TextParserResult genesisFixupEcotype(GenesisEcosystem *ecosystem, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];
			if (ecosystem->name)
				StructFreeString(ecosystem->name);
			getFileNameNoExt(name, ecosystem->filename);
			ecosystem->name = StructAllocString(name);
		}
	}
	return 1;
}

bool genesisEcotypeValidate(GenesisEcosystem *ecosystem)
{
	int i;
	bool ret = true;
	GroupDef *def = NULL;

	if(ecosystem->water_name)
	{
		def = objectLibraryGetGroupDefByName(ecosystem->water_name, false);
		if(!def)
		{
			ErrorFilenamef(ecosystem->filename, "Ecosystem refrences non-exsistant object library piece for water. Name: (%s)", ecosystem->water_name);
			ret = false;
		}
	}

	for ( i=0; i < eaSize(&ecosystem->placed_objects); i++ )
	{
		def = objectLibraryGetGroupDefFromRef(ecosystem->placed_objects[i], false);
		if(!def)
		{
			ErrorFilenamef(ecosystem->filename, "Ecosystem refrences non-exsistant object library piece for a placed object. Name: (%s)", ecosystem->placed_objects[i]->name_str);
			ret = false;
		}
	}

	if(ecosystem->path_geometry)
	{
		def = objectLibraryGetGroupDefByName(ecosystem->path_geometry, false);
		if(!def)
		{
			ErrorFilenamef(ecosystem->filename, "Ecosystem refrences non-exsistant object library piece for path geometry. Name: (%s)", ecosystem->path_geometry);
			ret = false;
		}
	}

	return ret;
}

static int genesisEcotypeValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisEcosystem *pEcosystem, U32 userID)
{
	switch(eType)
	{
	case RESVALIDATE_CHECK_REFERENCES:
		genesisEcotypeValidate(pEcosystem);		
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void genesisInitEcotypeLibrary(void)
{
	ecotype_room_dict = RefSystem_RegisterSelfDefiningDictionary(GENESIS_ECOTYPE_DICTIONARY, false, parse_GenesisEcosystem, true, false, NULL);
	resDictMaintainInfoIndex(ecotype_room_dict, ".Name", NULL, ".Tags", NULL, NULL);
	resDictManageValidation(ecotype_room_dict, genesisEcotypeValidateCB);
}

void genesisLoadEcotypeLibrary()
{
	if (!areEditorsPossible() || ecotype_room_dict_loaded)
		return;
	resLoadResourcesFromDisk(ecotype_room_dict, "genesis/ecosystems", ".ecosystem", "GenesisEcosystems.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/ecosystems/*.ecosystem", genesisReloadEcosystem);
	ecotype_room_dict_loaded = true;
}

U8 terGenGetAttributeIdx(GenesisDesignData *data, GenesisTerrainAttribute *in_attr)
{
	int i;
	GenesisTerrainAttribute *attr;

	if(!in_attr->name || in_attr->name[0] == '\0')
		return 0;

	if(in_attr->group_num != 0)
	{
		for( i=0; i < eaSize(&data->attribute_list) ; i++ )
		{
			attr = data->attribute_list[i];
			if(attr->group_num == in_attr->group_num && strcmp(attr->name, in_attr->name)==0)
				return i;
		}
	}

	i = eaSize(&data->attribute_list);
	attr = StructAlloc(parse_GenesisTerrainAttribute);
	StructCopyAll(parse_GenesisTerrainAttribute, in_attr, attr);
	eaPush(&data->attribute_list, attr);
	return i;
}

void terEdApplyBrushToTerrain(TerrainTaskQueue *queue, TerrainEditorSource *source, const char *name, bool optimized, bool locked_edges, int flags)
{
    TerrainCompiledMultiBrush *compiled_multibrush = calloc(1, sizeof(TerrainCompiledMultiBrush));
    TerrainMultiBrush *multi_brush = terrainGetMultiBrushByName(source, name);
    TerrainCommonBrushParams *params = calloc(1, sizeof(TerrainCommonBrushParams));
	if (!multi_brush)
	{
		Errorf("Cannot find brush: %s!", name);
		return;
	}
    terEdCompileMultiBrush(compiled_multibrush, multi_brush, true);
    params->brush_strength = 1.f; 
	params->lock_edges = locked_edges;
    terrainQueueFillWithMultiBrush(queue, compiled_multibrush, 
		params, optimized, source->visible_lod, 
		true, flags);
	terrainDestroyMultiBrush(multi_brush);
}

static bool terEdEcotypePaintsOnVista(GenesisEcotype *ecotype)
{
	int i;
	for( i=0; i < eaSize(&ecotype->attribute_types); i++ )
	{
		if(stricmp(ecotype->attribute_types[i], "Vista") == 0)
			return true;
	}
	return false;
}

static void terEdApplyEcosystemBrushesToTerrain(TerrainTaskQueue *queue, TerrainEditorSource *source, GenesisEcosystem *ecosystem, bool optimized, int flags)
{
	int i;
    TerrainCompiledMultiBrush *compiled_multibrush = calloc(1, sizeof(TerrainCompiledMultiBrush));
    TerrainCommonBrushParams *params = calloc(1, sizeof(TerrainCommonBrushParams));
	bool non_playable = false;
	bool has_vista_ecotype = false;

	for( i=0; i < eaSize(&source->layers); i++ )
	{
		if(!layerGetPlayable(source->layers[i]->layer))
		{
			non_playable = true;
			break;
		}
	}
	for( i=0; i < eaSize(&ecosystem->ecotypes) && !has_vista_ecotype ; i++ )
	{
		if(terEdEcotypePaintsOnVista(ecosystem->ecotypes[i]))
		{
			has_vista_ecotype = true;
			break;
		}
	}

	for( i=0; i < eaSize(&ecosystem->ecotypes) ; i++ )
	{
		TerrainMultiBrush *multi_brush;
		if(has_vista_ecotype && non_playable && !terEdEcotypePaintsOnVista(ecosystem->ecotypes[i]))
			continue;
		else if (has_vista_ecotype && !non_playable && terEdEcotypePaintsOnVista(ecosystem->ecotypes[i]))
			continue;
		multi_brush = terrainGetMultiBrushByName(source, ecosystem->ecotypes[i]->brush_name);
		if (!multi_brush)
		{
			Errorf("Cannot find brush: %s!", ecosystem->ecotypes[i]->brush_name);
			return;
		}
		terEdCompileMultiBrush(compiled_multibrush, multi_brush, false);
		terrainDestroyMultiBrush(multi_brush);
	}
    params->brush_strength = 1.f; 
    terrainQueueFillWithMultiBrush(queue, compiled_multibrush, 
		params, optimized, source->visible_lod, 
		true, flags);
}

typedef struct TerrainClearing
{
	F32 height;
	const char *name;
} TerrainClearing;

static bool terEdClearingTraverseCallback(TerrainClearing ***clearings, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	int i;
	if (groupIsVolumeType(def, "TerrainFilter"))
	{
		TerrainClearing *new_obj;
		const char *name = allocAddString(def->property_structs.terrain_properties.pcVolumeName);
		for (i = 0; i < eaSize(clearings); i++)
			if ((*clearings)[i]->name == name)
				return true;

		new_obj = calloc(1, sizeof(TerrainClearing));
		new_obj->height = info->world_matrix[3][1];
		new_obj->name = name;
		eaPush(clearings, new_obj);
	}
	return true;
}

static void terEdApplyClearingMultibrush(TerrainTaskQueue *queue, TerrainEditorSource *source, GenesisEcosystem *ecosystem, int flags)
{
	int i;
	TerrainClearing **clearings = NULL;
    TerrainCompiledMultiBrush *compiled_multibrush = calloc(1, sizeof(TerrainCompiledMultiBrush));
    TerrainCommonBrushParams *params = calloc(1, sizeof(TerrainCommonBrushParams));
    
    terEdClearCompiledMultiBrush(compiled_multibrush);

	for (i = zmapGetLayerCount(NULL)-1; i >= 0; --i)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		layerGroupTreeTraverse(layer, terEdClearingTraverseCallback, &clearings, false, false);
	}

	{
		TerrainCompiledBrush *new_brush;
		TerrainCompiledBrushOp *new_brush_op;
		new_brush = terrainAlloc(1, sizeof(TerrainCompiledBrush));
		if (!new_brush)
			return;
		new_brush->uses_color = false;
		new_brush->falloff_values.diameter_multi = 1;
		new_brush->falloff_values.hardness_multi = 1;
		new_brush->falloff_values.strength_multi = 1;
		new_brush->falloff_values.invert_filters = false;

		// Flatten brush
		new_brush_op = terrainAlloc(1, sizeof(TerrainCompiledBrushOp));
		new_brush_op->draw_func = terEdGetFunctionFromName(TFN_HeightFlattenBlob);
		new_brush_op->channel = TBC_Height;
		new_brush_op->values_copy = StructCreate(parse_TerrainBrushValues);
		new_brush_op->values_copy->float_1 = 0; // Begin distance
		new_brush_op->values_copy->float_2 = (ecosystem->clearing_size > 0) ? ecosystem->clearing_size : 0.2f; // End distance
		new_brush_op->values_copy->float_3 = (ecosystem->clearing_falloff > 0) ? ecosystem->clearing_falloff : 0.5f; // Falloff
		eaPush(&new_brush->bucket[TBK_OptimizedBrush].brush_ops, new_brush_op);

		eaPush(&compiled_multibrush->brushes, new_brush);
	}

	eaDestroyEx(&clearings, NULL);

    params->brush_strength = 1.f; 
    terrainQueueFillWithMultiBrush(queue, compiled_multibrush, 
		params, true, source->visible_lod, 
		true, flags);
}

static void genesisPlaceWaterPlane(GenesisToPlaceObject ***object_list, GenesisToPlaceObject *parent, const char *water_name)
{

	int i, j;
	int uid = objectLibraryUIDFromObjName(water_name);

	if (uid == 0)
		return;

	for( j=0; j < GEN_WATER_PLANE_COUNT; j++ )
	{
		for( i=0; i < GEN_WATER_PLANE_COUNT; i++ )
		{
			GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
			copyMat4(unitmat, to_place_object->mat);
			to_place_object->mat[3][0] = i*GEN_WATER_PLANE_SIZE - GEN_WATER_PLANE_OFFSET;
			to_place_object->mat[3][2] = j*GEN_WATER_PLANE_SIZE - GEN_WATER_PLANE_OFFSET;
			to_place_object->uid = uid;
			to_place_object->parent = parent;
			eaPush(object_list, to_place_object);
		}
	}
}

static GenesisToPlaceObject* genesisMakeExteriorVolume(GenesisToPlaceObject *parent_object, const char *name, Vec3 min, Vec3 max)
{
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	to_place_object->object_name = allocAddString(name);
	copyMat4(unitmat, to_place_object->mat);
	to_place_object->params = StructCreate(parse_GenesisProceduralObjectParams);
	to_place_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
	to_place_object->params->volume_properties->eShape = GVS_Box;
	copyVec3(min, to_place_object->params->volume_properties->vBoxMin);
	copyVec3(max, to_place_object->params->volume_properties->vBoxMax);
	to_place_object->uid = 0;
	to_place_object->parent = parent_object;
	return to_place_object;
}

static void genesisMakeExteriorBoundingSkyVolume(GenesisToPlaceObject ***object_list, GenesisToPlaceObject *parent_object, const char *name, Vec2 min, Vec2 max, WorldSkyVolumeProperties *sky_props)
{
	GenesisToPlaceObject *to_place_object;
	to_place_object = genesisMakeExteriorVolume(parent_object, name, min, max); 
	to_place_object->params->sky_volume_properties = StructClone(parse_WorldSkyVolumeProperties, sky_props);
	genesisProceduralObjectEnsureType(to_place_object->params);
	eaPush(object_list, to_place_object);
}

static void genesisMakeExteriorBoundingSkyVolumes(GenesisToPlaceObject ***object_list, GenesisToPlaceObject *parent_object, Vec3 min, Vec3 max)
{
	#define GEN_BOUND_VOL_SIZE 100
	Vec3 vol_min, vol_max;
	GenesisConfig *config = genesisConfig();
	WorldSkyVolumeProperties *sky_props;

	if(!config)
		return;
	sky_props = config->boundary;
	if(!sky_props)
		return;

	// Upper
	{
		vol_min[0] =  min[0];
		vol_min[1] =  max[1] - GEN_BOUND_VOL_SIZE;
		vol_min[2] =  min[2];
		vol_max[0] =  max[0];
		vol_max[1] =  max[1];
		vol_max[2] =  max[2];
		genesisMakeExteriorBoundingSkyVolume(object_list, parent_object, "Exterior_Autogen_SkyFade_Upper", vol_min, vol_max, sky_props);
	}

	// Lower
	{
		vol_min[0] =  min[0];
		vol_min[1] =  min[1];
		vol_min[2] =  min[2];
		vol_max[0] =  max[0];
		vol_max[1] =  min[1] + GEN_BOUND_VOL_SIZE;
		vol_max[2] =  max[2];
		genesisMakeExteriorBoundingSkyVolume(object_list, parent_object, "Exterior_Autogen_SkyFade_Lower", vol_min, vol_max, sky_props);
	}

	// North
	{
		vol_min[0] =  min[0];
		vol_min[1] =  min[1];
		vol_min[2] =  max[2] - GEN_BOUND_VOL_SIZE;
		vol_max[0] =  max[0];
		vol_max[1] =  max[1];
		vol_max[2] =  max[2];
		genesisMakeExteriorBoundingSkyVolume(object_list, parent_object, "Exterior_Autogen_SkyFade_North", vol_min, vol_max, sky_props);
	}

	// South
	{
		vol_min[0] =  min[0];
		vol_min[1] =  min[1];
		vol_min[2] =  min[2];
		vol_max[0] =  max[0];
		vol_max[1] =  max[1];
		vol_max[2] =  min[2] + GEN_BOUND_VOL_SIZE;
		genesisMakeExteriorBoundingSkyVolume(object_list, parent_object, "Exterior_Autogen_SkyFade_South", vol_min, vol_max, sky_props);
	}

	// East
	{
		vol_min[0] =  max[0] - GEN_BOUND_VOL_SIZE;
		vol_min[1] =  min[1];
		vol_min[2] =  min[2];
		vol_max[0] =  max[0];
		vol_max[1] =  max[1];
		vol_max[2] =  max[2];
		genesisMakeExteriorBoundingSkyVolume(object_list, parent_object, "Exterior_Autogen_SkyFade_East", vol_min, vol_max, sky_props);
	}

	// West
	{
		vol_min[0] =  min[0];
		vol_min[1] =  min[1];
		vol_min[2] =  min[2];
		vol_max[0] =  min[0] + GEN_BOUND_VOL_SIZE;
		vol_max[1] =  max[1];
		vol_max[2] =  max[2];
		genesisMakeExteriorBoundingSkyVolume(object_list, parent_object, "Exterior_Autogen_SkyFade_West", vol_min, vol_max, sky_props);
	}
}

void genesisMakeDetailObjects(GenesisToPlaceState *to_place, GenesisEcosystem *ecosystem, GenesisZoneNodeLayout *layout, bool force_no_water, bool make_sky_volumes)
{
	int i;
	Vec3 min, max;
	F32 cx[] = { 0.f, 1.f, 0.5f, 0.5f };
	F32 cz[] = { 0.5f, 0.5f, 1.f, 0.f };
	GenesisGeotype *geotype;
	F32 play_buffer;
	bool has_playable_volume = false;

	GenesisToPlaceObject *detail_objects = calloc(1, sizeof(GenesisToPlaceObject));
	detail_objects->object_name = allocAddString("GenesisDetailObjects");
	detail_objects->uid = 0;
	detail_objects->parent = NULL;
	identityMat4(detail_objects->mat);
	eaPush(&to_place->objects, detail_objects);

	// Add Water Plane
	if(layout)
		geotype = GET_REF(layout->geotype);
	if(!force_no_water && (!layout || !geotype || !geotype->node_data.no_water))
		genesisPlaceWaterPlane(&to_place->objects, detail_objects, ecosystem->water_name ? ecosystem->water_name : "Water_Tropical_1000ft_01");

	for ( i=0; i < eaSize(&ecosystem->placed_objects); i++ )
	{
		GroupDefRef *def_ref = ecosystem->placed_objects[i];
		int uid = def_ref->name_uid;
		if(!uid)
			uid = objectLibraryUIDFromObjName(def_ref->name_str);
		if(!uid)
		{
			genesisRaiseErrorInternal(GENESIS_ERROR, "Ecosystem", ecosystem->name,
									  "References a just placed object %s that does not exist.",
									  def_ref->name_uid);
		}
		else 
		{
			GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
			copyMat4(unitmat, to_place_object->mat);
			to_place_object->uid = uid;
			eaPush(&to_place->objects, to_place_object);	
		}
	}

	if (layout)
	{
		min[0] = layout->play_min[0];
		min[2] = layout->play_min[1];
		max[0] = layout->play_max[0];
		max[2] = layout->play_max[1];
		play_buffer = layout->play_buffer;
	}
	else
	{
		min[0] = 0;
		min[2] = 0;
		max[0] = 2048.f;
		max[2] = 2048.f;
		play_buffer = EXTERIOR_MIN_PLAYFIELD_BUFFER;
	}

	if (max[0] > min[0] && max[2] > min[2])
	{
		min[1] = max[1] = 0;
		addVec3same(min, play_buffer, min);
		subVec3same(max, play_buffer, max);

		if (layout->play_heights[1] > layout->play_heights[0])
		{
			min[1] = layout->play_heights[0];
			max[1] = layout->play_heights[1];
		}
		else
		{
			min[1] = -1500;
			max[1] = 5000;
		}

		has_playable_volume = true;
	}

	if (make_sky_volumes)
	{
		// Add Bounding Sky Fade Volumes
		genesisMakeExteriorBoundingSkyVolumes(&to_place->objects, detail_objects, min, max);
	}

	// Add Room Volume[s]
	if (eaSize(&layout->room_partitions) > 0)
	{
		FOR_EACH_IN_EARRAY(layout->room_partitions, ZoneMapEncounterRoomInfo, partition)
		{
			GenesisToPlaceObject *to_place_object = genesisMakeExteriorVolume(detail_objects, "Exterior_Autogen_Volume", partition->min, partition->max);
			to_place_object->params->room_properties = StructCreate(parse_WorldRoomProperties);
			to_place_object->params->room_properties->eRoomType = WorldRoomType_Room;
			eaPush(&to_place->objects, to_place_object);
		}
		FOR_EACH_END;
	}
	else if (has_playable_volume)
	{
		GenesisToPlaceObject *to_place_object = genesisMakeExteriorVolume(detail_objects, "Exterior_Autogen_Volume", min, max);
		to_place_object->params->room_properties = StructCreate(parse_WorldRoomProperties);
		to_place_object->params->room_properties->eRoomType = WorldRoomType_Room;
		eaPush(&to_place->objects, to_place_object);
	}
	
	if (has_playable_volume)
	{
		GenesisToPlaceObject *to_place_object = genesisMakeExteriorVolume(detail_objects, "Exterior_Autogen_Playable_Volume", min, max);
		genesisProceduralObjectAddVolumeType(to_place_object->params, "Playable");
		eaPush(&to_place->objects, to_place_object);
	}
	
	if(layout && layout->backdrop)
	{
		// FX Volume
		if(layout->backdrop->fx_volume)
		{
			GenesisToPlaceObject *to_place_object = genesisMakeExteriorVolume(detail_objects, "Exterior_Autogen_FX_Volume", min, max);
			to_place_object->params->fx_volume = StructClone(parse_WorldFXVolumeProperties, layout->backdrop->fx_volume);
			genesisProceduralObjectEnsureType(to_place_object->params);
			eaPush(&to_place->objects, to_place_object);	
		}
		// Power Volume
		if(layout->backdrop->power_volume)
		{
			GenesisToPlaceObject *to_place_object = genesisMakeExteriorVolume(detail_objects, "Exterior_Autogen_Power_Volume", min, max);
			to_place_object->params->power_volume = StructClone(parse_WorldPowerVolumeProperties, layout->backdrop->power_volume);
			genesisProceduralObjectEnsureType(to_place_object->params);
			eaPush(&to_place->objects, to_place_object);	
		}
	}
}

bool genesisPathTraverseCallback(Spline ***curve_array, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if (def->property_structs.curve && def->property_structs.curve->genesis_path && def->property_structs.curve->spline.spline_widths)
	{
		Spline *new_spline = StructClone(parse_Spline, &def->property_structs.curve->spline);
		splineTransformMatrix(new_spline, info->curve_matrix);
		eaPush(curve_array, new_spline);
	}
	return true;
}

void genesisMoveDesignToDetail(TerrainTaskQueue *queue, TerrainEditorSource *source,
							   GenesisEcosystem *ecosystem, int flags,
							   terrainTaskCustomFinishCallback callback, void *userdata)
{
	int i;
	int lod = 2;
	GroupDef *path_def;
	ModelLOD *path_model;
	F32 path_model_length;
	VaccuformObject **objects = NULL;
	Spline **path_curves = NULL;

	if(eaSize(&ecosystem->ecotypes) <= 0)
		return;

	for (i = 0; i < eaSize(&source->layers); i++)
	{
		if ((zmapInfoHasGenesisData(NULL) || source->layers[i]->effective_mode == LAYER_MODE_EDITABLE) &&
			source->layers[i]->exclusion_version != ecosystem->exclusion_version)
		{
			source->layers[i]->exclusion_version = ecosystem->exclusion_version;
			//genesisRaiseError(GENESIS_WARNING, "Setting exclusion version on layer prior to painting!");
		}
	}

	// Paint clearings in
	terEdApplyClearingMultibrush(queue, source, ecosystem, flags);

	// Paint paths in
	path_def = objectLibraryGetGroupDefByName(ecosystem->path_geometry ? ecosystem->path_geometry : "Pathplane_Berms_01", true);
	if (path_def && path_def->model)
	{
		path_model = modelLODLoadAndMaybeWait(path_def->model, 0, true);
		path_model_length = path_def->bounds.max[2]-path_def->bounds.min[2];
		eaPush(&source->loaded_objects, path_def);

		if (path_model)
		{
			for (i = zmapGetLayerCount(NULL)-1; i >= 0; --i)
			{
				ZoneMapLayer *layer = zmapGetLayer(NULL, i);
				layerGroupTreeTraverse(layer, genesisPathTraverseCallback, &path_curves, false, false);
			}
			for( i=0; i < eaSize(&path_curves); i++ )
			{
				int j;
				Spline *curve = path_curves[i];
				Mat4 parent_mat;
				Vec3 center = { 0, 0, 0 };
				Spline norm_spline = { 0 };
				F32 length = path_model_length, scale = curve->spline_widths[0]/10.f;
				WorldChildCurve child_props = { 0 };

				identityMat4(parent_mat);

				child_props.normalize = true;
				child_props.repeat_length = length*scale;
				child_props.max_cps = 1000;
				child_props.child_type = CURVE_CHILD_RIGID;
				curveCalculateChild(curve, &norm_spline, &child_props, 0, path_def, parent_mat);

				for (j = 0; j < eafSize(&norm_spline.spline_points)-3; j += 3)
				{
					char *prop_val;
					VaccuformObject *obj = calloc(1, sizeof(VaccuformObject));
					F32 falloff = 20;
					splineGetMatrices(parent_mat, center, 1.f, &norm_spline, j, obj->spline_matrices, false);
					obj->spline_matrices[1][2][0] = length;
					obj->spline_matrices[1][2][1] = scale;

					obj->is_spline = true;
					falloff = path_def->property_structs.terrain_properties.fVaccuFormFalloff;
					if ((prop_val = path_def->property_structs.terrain_properties.pcVaccuFormBrush) != NULL)
						strcpy(obj->brush_name, prop_val);

					//printf("Vaccuforming %s - %f (%s)\n", def->name_str, falloff, obj->brush_name);
					obj->model = path_model;
					identityMat4(obj->mat);
					obj->falloff = falloff;
					terrainQueueVaccuformObject(queue, source, obj, flags, true);
				}
			}
			eaDestroyEx(&path_curves, splineDestroy);
		}
	}
	else
	{
		genesisRaiseErrorInternal(GENESIS_ERROR, "Ecosystem", ecosystem->name, "Ecosystem references invalid path geometry \"%s\"", ecosystem->path_geometry ? ecosystem->path_geometry : "Pathplane_Berms_01");
	}

	// Vaccuform other objects
	for (i = 0; i < eaSize(&source->layers); i++)
		terrainQueueFindObjectsToVaccuform(source->layers[i], &objects, false);

	for (i = 0; i < eaSize(&objects); i++)
		terrainQueueVaccuformObject(queue, source, objects[i], flags, false);
	eaDestroyEx(&objects, NULL);

    terrainQueueUpdateNormals(queue, flags);
	terrainQueueFinishTask(queue, NULL, NULL);

	terEdApplyEcosystemBrushesToTerrain(queue, source, ecosystem, true, flags);

    terrainQueueFinishTask(queue, callback, userdata);
}

#include "wlGenesisExteriorDesign_h_ast.c"

#endif
