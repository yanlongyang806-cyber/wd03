#ifndef NO_EDITORS

#include "wlTerrainSource.h"
#include "wlTerrainBrush.h"
#include "wlTerrainQueue.h"
#include "wlEditorIncludes.h"

#include "error.h"
#include "hoglib.h"
#include "ScratchStack.h"
#include "ThreadManager.h"

#include "Quat.h"

extern ParseTable parse_TerrainObjectEntry[];
#define TYPE_parse_TerrainObjectEntry TerrainObjectEntry

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//// Source functions

TerrainEditorSource *terrainSourceInitialize()
{
	ZoneMap *zmap = worldGetActiveMap();
    TerrainEditorSource *ret = (TerrainEditorSource*)calloc(1, sizeof(TerrainEditorSource));
    ret->editing_lod = -1;
    ret->visible_lod = 0;
    ret->heightmaps = stashTableCreateInt(128);

    return ret;
}

void terrainSourceLayerDestroy(TerrainEditorSourceLayer *layer)
{
    terrainSourceUnloadLayerData(layer);
}

void terrainSourceDestroy(TerrainEditorSource *source)
{
	RefDictIterator it;
	TerrainDefaultBrush *brush;
	ASSERT_CALLED_IN_SINGLE_THREAD;

	if (!source)
		return;

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		terrainFreeBrushImage(brush->default_values.image_ref);
		brush->default_values.image_ref = NULL;
	}

	source->heightmaps = NULL;
	eaDestroy(&source->brush_images);
    eaDestroyEx(&source->layers, terrainSourceUnloadLayerData);
    stashTableDestroyEx(source->heightmaps, NULL, heightMapDestroy);

	//Delete Random Terrain Gen Data
    eaDestroy(&source->loaded_objects);

    SAFE_FREE(source);
}

TerrainEditorSourceLayer *terrainSourceAddLayer(TerrainEditorSource *source, ZoneMapLayer *layer)
{
    TerrainEditorSourceLayer *ret = (TerrainEditorSourceLayer*)calloc(1, sizeof(TerrainEditorSourceLayer));
    ret->source = source;
    ret->layer = layer;
	ret->effective_mode = layer->layer_mode;
    ret->loaded_lod = -1;
    ret->saved = 1;
    
    ret->dummy_layer = (ZoneMapLayer*)calloc(1, sizeof(ZoneMapLayer));
    ret->dummy_layer->layer_mode = LAYER_MODE_GROUPTREE;
	ret->dummy_layer->zmap_parent = layer->zmap_parent;
	ret->dummy_layer->dummy_layer = layer;

	layer->terrain.source_data = ret;        
    eaPush(&source->layers, ret);

	return ret;
}

int terrainSourceGetObjectIndex( TerrainEditorSource *source, int uid, int seed, bool create)
{
	int idx;
	TerrainObjectEntry *new_entry;

	for (idx = 0; idx < eaSize(&source->object_table); idx++)
	{
		if (uid == source->object_table[idx]->objInfo.name_uid)
			return idx;
	}

	if(!create)
		return UNDEFINED_OBJ;

	new_entry = StructAlloc(parse_TerrainObjectEntry);
	new_entry->objInfo.name_uid = uid;
	new_entry->seed = (seed != -1) ? seed : rand();
	terrainLock();
	eaPush(&source->object_table, new_entry);
	terrainUnlock();
	return eaSize(&source->object_table)-1;
}

int terrainSourceGetMaterialIndex( TerrainEditorSource *source, const char *material, bool create)
{
	int idx;
	char *new_entry;

	if (!material || !source) 
		return UNDEFINED_MAT;

	for (idx = 0; idx < eaSize(&source->material_table); idx++)
	{
		if (stricmp(source->material_table[idx], material) == 0)
			return idx;
	}

	if(!create)
		return UNDEFINED_MAT;

	new_entry = StructAllocString(material);
	terrainLock();
	eaPush(&source->material_table, new_entry);
	terrainUnlock();
	return eaSize(&source->material_table)-1;
}

void terrainSourceRefreshHeightmaps(TerrainEditorSource *source, bool texture_only)
{
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator(source->heightmaps, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
		if (texture_only)
			heightMapUpdateTextures(height_map);
		else
			heightMapUpdate(height_map);
    }
}

void terrainSourceModifyHeightmaps(TerrainEditorSource *source)
{
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator(source->heightmaps, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
        heightMapModify(height_map);
    }
}

//// Source Layer functions

void terrainSourceLayerSetUnsaved(TerrainEditorSourceLayer *layer)
{
	layer->saved = false;
	layer->layer->terrain.unsaved_changes = true;
}

int terrainSourceLoadLayerData(TerrainEditorSourceLayer *layer, bool clear_map_cache, bool open_trackers, progress_callback callback, U32 progress_id)
{
    int i, block, lod;

	if (layer->loading || layer->loaded)
	{
		return layer->loaded_lod;
	}
	layer->loading = true;
	layer->playable = layerGetPlayable(layer->layer);
    layer->exclusion_version = layerGetExclusionVersion(layer->layer);
	layer->color_shift = layerGetColorShift(layer->layer);
    
    eaiClear(&layer->object_lookup);

    for (i = 0; i < eaSize(&layer->layer->terrain.object_table); i++)
        eaiPush(&layer->object_lookup,
                terrainSourceGetObjectIndex(layer->source,
                                            layer->layer->terrain.object_table[i]->objInfo.name_uid,
                                            layer->layer->terrain.object_table[i]->seed, true));

    eaiClear(&layer->material_lookup);

    for (i = 0; i < eaSize(&layer->layer->terrain.material_table); i++)
        eaiPush(&layer->material_lookup,
        		terrainSourceGetMaterialIndex(layer->source, layer->layer->terrain.material_table[i], true));

	if(eaSize(&layer->layer->terrain.material_table) == 0)
	{
		char *new_entry = StructAllocString("TerrainDefault");
		eaiPush(&layer->material_lookup,
        		terrainSourceGetMaterialIndex(layer->source, new_entry, true));
	}

    lod = -1;
	for (block = 0; block < eaSize(&layer->layer->terrain.blocks); block++)
	{
		TerrainBlockRange *range = layer->layer->terrain.blocks[block];
        TerrainBlockRange *new_block = calloc(1, sizeof(TerrainBlockRange));
		layerTerrainUpdateFilenames(layer->layer, range);
		terrainBlockLoadSource(layer->layer, range, false);
		for (i = 0; i < eaSize(&range->map_cache); i++)
		{
			HeightMap *height_map = range->map_cache[i];
            IVec2 map_pos;
            heightMapGetMapLocalPos(height_map, map_pos);
			stashIntAddPointer(layer->source->heightmaps, getTerrainGridPosKey(map_pos), height_map, false);
			if (open_trackers)
			{
				HeightMapTracker *tracker = heightMapTrackerOpen(height_map);
		        eaPush(&layer->heightmap_trackers, tracker);
			}
			if (lod > 0 && (int)heightMapGetLoadedLOD(height_map) != lod)
				ErrorFilenamef(layer->layer->filename, "Layer has blocks at different LOD's!");
            if (lod < 0 || (int)heightMapGetLoadedLOD(height_map) < lod)
                lod = (int)heightMapGetLoadedLOD(height_map);
		}
		if (clear_map_cache)
			eaDestroy(&range->map_cache);
        
        new_block->range_name = strdup(range->range_name);
        copyVec3(range->range.min_block, new_block->range.min_block);
        copyVec3(range->range.max_block, new_block->range.max_block);
        eaPush(&layer->blocks, new_block);

        if (range->interm_file)
        {
            //printf("Closing read-only intermediate %s %p\n", hogFileGetArchiveFileName(block->interm_file), block);
            hogFileDestroy(range->interm_file, true);
            range->interm_file = NULL;
        }
		if (callback)
			callback(progress_id, 1);
	}
    if (lod != -1)
	{
		terrainSourceLayerResample(layer, lod);
	}
	else
	{
        lod = HEIGHTMAP_NO_LOD;
	}

    layer->loaded_lod = layer->layer->terrain.loaded_lod = lod;
    
    terrainSourceUpdateNormals(layer, true);
    terrainSourceUpdateColors(layer, true);

	layer->loading = false;
    layer->loaded = true;

    return lod;
}

void terrainSourceFinishLoadLayerData(int iPartitionIdx, TerrainEditorSourceLayer *layer)
{
	int i;
 	terrainSourceUpdateAllObjects(layer, true, true);
	for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
		heightMapTrackerOpenEntries(iPartitionIdx, layer->heightmap_trackers[i]);
}

void terrainSourceUnloadLayerData(TerrainEditorSourceLayer *layer)
{
    int i;
    HeightMap **height_maps = NULL;

    layer->loaded = false;
    
	if (layer->heightmap_trackers)
	{
		for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
		{
			IVec2 map_pos;
			HeightMap *height_map = layer->heightmap_trackers[i]->height_map;
			heightMapGetMapLocalPos(height_map, map_pos);
			stashIntRemovePointer(layer->source->heightmaps, getTerrainGridPosKey(map_pos), NULL);
			eaPush(&height_maps, height_map);
		}
	}
	else
	{
		StashTableIterator iter;
		StashElement elem;
		stashGetIterator(layer->source->heightmaps, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
			if (heightMapGetLayer(height_map) == layer->layer)
			{
				IVec2 map_pos;
				heightMapGetMapLocalPos(height_map, map_pos);
				stashIntRemovePointer(layer->source->heightmaps, getTerrainGridPosKey(map_pos), NULL);
				eaPush(&height_maps, height_map);
			}
		}
	}

    //eaDestroyStruct(&layer->rivers, parse_River);

    eaDestroyEx(&layer->heightmap_trackers, heightMapTrackerClose);
    layer->heightmap_trackers = NULL;

	eaDestroyEx(&layer->dummy_layer->cell_entries, worldCellEntryFree);
	eaDestroyEx(&layer->dummy_layer->terrain.object_defs, layerFreeObjectWrapperEntries);

    eaDestroyEx(&height_maps, heightMapDestroy);
    
    eaDestroyStruct(&layer->blocks, parse_TerrainBlockRange);

	layer->saved = 1;

	layer->layer->terrain.source_data = NULL;
	eaFindAndRemove(&layer->source->layers, layer);
	SAFE_FREE(layer);
}

bool terrainSourceCreateBlock(int iPartitionIdx, TerrainEditorSourceLayer *layer, IVec2 min_block, IVec2 max_block, bool do_checkout, bool create_trackers) 
{
    IVec2 local_pos;
    int i;
    int lod = layer->loaded_lod;
    TerrainBlockRange **new_blocks = NULL;
	TerrainBlockRange *new_block;

    for (i = 0; i < eaSize(&layer->blocks); i++)
    {
        new_block = StructCreate(parse_TerrainBlockRange);
        copyVec3(layer->blocks[i]->range.min_block, new_block->range.min_block);
        copyVec3(layer->blocks[i]->range.max_block, new_block->range.max_block);
        eaPush(&new_blocks, new_block);
    }
    new_block = StructCreate(parse_TerrainBlockRange);
	setVec3(new_block->range.min_block, min_block[0], 0, min_block[1]);
	setVec3(new_block->range.max_block, max_block[0], 0, max_block[1]);
    eaPush(&new_blocks, new_block);

    if (!terrainSourceOptimizeLayer(layer, new_blocks, do_checkout))
    {
        eaDestroyStruct(&new_blocks, parse_TerrainBlockRange);
        return false;
    }

    eaDestroyStruct(&new_blocks, parse_TerrainBlockRange);

    if (lod == HEIGHTMAP_NO_LOD)
        layer->loaded_lod = lod = HEIGHTMAP_DEFAULT_LOD;

    // Fill with heightmaps
    for (local_pos[1] = min_block[1]; local_pos[1] <= max_block[1]; ++local_pos[1])
    {
        for (local_pos[0] = min_block[0]; local_pos[0] <= max_block[0]; ++local_pos[0])
        {
            HeightMapTracker *tracker;
            HeightMap *height_map = heightMapCreateDefault(layer->layer, lod, local_pos[0], local_pos[1]);
            heightMapModify(height_map);
			heightMapUpdate(height_map);
            stashIntAddPointer(layer->source->heightmaps, getTerrainGridPosKey(local_pos), height_map, false);
			if (create_trackers)
			{
	            tracker = heightMapTrackerOpen(height_map);
		        eaPush(&layer->heightmap_trackers, tracker);
			    heightMapTrackerOpenEntries(iPartitionIdx, tracker);
			}
        }
    }

	//assert(layer->effective_mode == LAYER_MODE_EDITABLE);
    terrainSourceLayerSetUnsaved(layer);

    terrainSourceUpdateNormals(layer, true);
    terrainSourceUpdateColors(layer, true);
    terrainSourceUpdateAllObjects(layer, true, true);

    return true;
}

void terrainSourceUpdateNormals(TerrainEditorSourceLayer *layer, bool force)
{
	int i;
	HeightMap **map_cache = NULL;
	layer->disable_normals = false;
	if (eaSize(&layer->heightmap_trackers))
	{
		for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
			eaPush(&map_cache, layer->heightmap_trackers[i]->height_map);
	}
	else
	{
		FOR_EACH_IN_STASHTABLE(layer->source->heightmaps, HeightMap, it)
		{
			if (heightMapGetLayer(it) == layer->layer)
				eaPush(&map_cache, it);
		} FOR_EACH_END;
	}
	for (i = 0; i < eaSize(&map_cache); i++)
	{
		HeightMap *height_maps[3][3];
		if (!map_cache[i] || 
            (!force && !heightMapWasTouched(map_cache[i], TERRAIN_BUFFER_HEIGHT)))
            continue;
		height_maps[1][1] = map_cache[i];
		heightMapGetNeighbors(height_maps, map_cache);
		heightMapUpdateNormals(height_maps);
		heightMapUpdateGeometry(map_cache[i]);
    }
	eaDestroy(&map_cache);
}

void terrainSourceUpdateColors(TerrainEditorSourceLayer *layer, bool force)
{
	int i, lod;
	HeightMap **map_cache = NULL;
	for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
		eaPush(&map_cache, layer->heightmap_trackers[i]->height_map);
	for (lod=GET_COLOR_LOD(layer->loaded_lod) ; lod < GET_COLOR_LOD(MAX_TERRAIN_LODS); lod++)
	{
        for (i = 0; i < eaSize(&map_cache); i++)
        {
            HeightMap *height_maps[3][3];
			if (!map_cache[i] ||
				(!force && !heightMapWasTouched(map_cache[i], TERRAIN_BUFFER_COLOR)))
                continue;
			height_maps[1][1] = map_cache[i];
			heightMapGetNeighbors(height_maps, map_cache);
            heightMapUpdateColors(height_maps, lod);
        }
	}
}

#define CONVERT_U8_VEC_VAL_TO_VEC3_VAL(u8VecVal) ((u8VecVal-128.0f)/127.0f)
#define CONVERT_U8_VEC_TO_VEC3(retVec3, u8Vec) \
	retVec3[2] = CONVERT_U8_VEC_VAL_TO_VEC3_VAL(u8Vec[0]); \
	retVec3[1] = CONVERT_U8_VEC_VAL_TO_VEC3_VAL(u8Vec[1]); \
	retVec3[0] = -CONVERT_U8_VEC_VAL_TO_VEC3_VAL(u8Vec[2]);

int terrainSourceObjectConstraintsChildren(void *user_data, TrackerTreeTraverserDrawParams *draw)
{
	//if (!wleTrackerIsEditable(trackerHandleFromTracker(draw->tracker->parent), false, true, true))
	{
		//return 0;
	}
	if (draw->tracker->def)
	{
		if (draw->tracker->def->property_structs.terrain_properties.bSnapToTerrainHeight)
		{
			Mat4 modified;
			copyMat4(draw->world_mat, modified);
			if (terrainSourceGetInterpolatedHeight((TerrainEditorSource*)user_data, modified[3][0], modified[3][2], &modified[3][1], NULL))
			{
				Mat4 inverse, parent, parentInverse, result;

				if (draw->tracker->def->property_structs.terrain_properties.bSnapToTerrainNormal)
				{
					Quat qRot;
					F32 angle, dot;
					Vec3 upVec, oldUp, axis;
					U8 upVec8[3];
					Mat3 rotated;
					terrainSourceGetNormal((TerrainEditorSource*)user_data, modified[3][0], modified[3][2], upVec8, NULL);
					CONVERT_U8_VEC_TO_VEC3(upVec, upVec8);
					normalVec3(upVec);
					copyVec3(modified[1], oldUp);
					normalVec3(oldUp);
					dot = dotVec3(upVec, oldUp);
					if (dot < -1)
					{
						dot = -1;
					}
					else if (dot > 1)
					{
						dot = 1;
					}
					angle = acos(dot);

					if (angle)
					{
						// create a rotation axis by crossing the old and new y-axes
						crossVec3(upVec, modified[1], axis);
						normalVec3(axis);

						// rotate around the rotation axes to align the two axes
						if (axisAngleToQuat(axis, angle, qRot))
						{
							quatToMat(qRot, rotated);
							mulMat3(rotated, modified, result);
							copyMat3(result, modified);
						}
					}
				}

				invertMat4(draw->tracker->parent->def->children[draw->tracker->idx_in_parent]->mat, inverse);
				mulMat4(draw->world_mat, inverse, parent);
				invertMat4(parent, parentInverse);
				mulMat4(parentInverse, modified, result);
				copyMat4(result, draw->tracker->parent->def->children[draw->tracker->idx_in_parent]->mat);
				copyMat4(modified, draw->world_mat);
				groupDefModify(draw->tracker->parent->def, draw->tracker->idx_in_parent, true);
				return 1;
			}
			else
			{
				return 0;
			}
		}
		return 1;
	}
	return 0;
}

void terrainSourceUpdateObjectConstraints(TerrainEditorSourceLayer *layer, GroupDef *def)
{
	if (layer->effective_mode == LAYER_MODE_EDITABLE &&
		layer->layer->grouptree.root_def)
	{
		trackerTreeTraverse(layer->layer, layerGetTracker(layer->layer), terrainSourceObjectConstraintsChildren, layer->source);
		layerTrackerUpdate(layer->layer, false, false);
	}
}

void terrainSourceUpdateAllObjects(TerrainEditorSourceLayer *layer, bool force, bool create_entries)
{
	int i;

    if (!layer->source->objects_hidden && !force)
    {
        bool needs_update = false;
        for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
        {
            HeightMap *height_map = layer->heightmap_trackers[i]->height_map;
            if (heightMapWasTouched(height_map, TERRAIN_BUFFER_HEIGHT) ||
                heightMapWasTouched(height_map, TERRAIN_BUFFER_COLOR) ||
                heightMapWasTouched(height_map, TERRAIN_BUFFER_OBJECTS))
            {
                needs_update = true;
                break;
            }
        }
        if (!needs_update)
            return;
    }

	terrainSourceUpdateObjectConstraints(layer, NULL);

	// I'm going to keep my old dummy layers if I can, so that I only need to repopulate the ones that are in map regions that I touched.
	if (force || layer->dummy_layer == NULL || eaSize(&layer->dummy_layer->terrain.object_defs) != eaiSize(&layer->object_lookup))
	{
		force = 1;
		// Sets up some dummy layers.  These layers will be full of group defs that will be created from the terrain source data.
		// (Destroys layer->dummy_layer->terrain.object_defs, and recreates it - one entry per entry in object_lookup)
		layerInitObjectWrappers(layer->dummy_layer, eaiSize(&layer->object_lookup));
	}
	// I believe this code is virtuous, unfortunately, I can't figure out if there's any information at all in the layer wrapper to correlate 
	// it to the data, whether it's safe to add fields to the struct, whether anything depends on the order it was created in, or which
	// of the fake data created (such as the filename) matters at all.
/*	else
	{
		TerrainObjectWrapper ** pWrappers = layer->dummy_layer->terrain.object_defs;
		int iNewNumWrappers = eaiSize(&layer->object_lookup);
		int j;
		layer->dummy_layer->terrain.object_defs = NULL;
		for (i=0;i<iNewNumWrappers;i++)
		{
			// See if we have an existing wrapper for this (I imagine they will always be in order at the beginning of the array anyway)
			for (j=0;j<eaSize(&pWrappers);j++)
			{
				if (????????????)
				{
					char layer_name[64];
					sprintf(layer_name, "TERRAIN_OBJECT_DUMMY_%d", i);
					free((void *)pWrappers[j]->layer->filename);
					pWrappers[j]->layer->filename = strdup(layer_name); //strdupping into a pooled string....
					eaPush(&layer->dummy_layer->terrain.object_defs,pWrappers[j]);
					eaRemove(&pWrappers,j);
					break;
				}
			}

			if (j==eaSize(&pWrappers))
			{
				// make a new one
				// I don't know why we have dummy layers that have dummy layers, and why we have to use them [RMARR - 7/1/13]
				eaPush(&layer->dummy_layer->terrain.object_defs, layerCreateObjectWrapper(layer->dummy_layer->dummy_layer ? layer->dummy_layer->dummy_layer : layer->dummy_layer, i));
			}
		}

		eaDestroyEx(&pWrappers, layerFreeObjectWrapper);
	}*/
    
    if (!layer->source->objects_hidden)
    {
		int block;
		for (block = 0; block < eaSize(&layer->blocks); block++)
		{
			HeightMap **maps = NULL;
			TerrainBlockRange *range = layer->blocks[block];
			if (layer->heightmap_trackers)
			{
				for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
				{
					IVec2 local_pos;
					heightMapGetMapLocalPos(layer->heightmap_trackers[i]->height_map, local_pos);
					if (local_pos[0] >= range->range.min_block[0] &&
						local_pos[0] <= range->range.max_block[0] &&
						local_pos[1] >= range->range.min_block[2] &&
						local_pos[1] <= range->range.max_block[2])
						eaPush(&maps, layer->heightmap_trackers[i]->height_map);
				}
			}
			else
			{
				StashTableIterator iter;
				StashElement elem;
				stashGetIterator(layer->source->heightmaps, &iter);
				while (stashGetNextElement(&iter, &elem))
				{
					HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
					if (heightMapGetLayer(height_map) == layer->layer)
					{
						IVec2 local_pos;
						heightMapGetMapLocalPos(height_map, local_pos);
						if (local_pos[0] >= range->range.min_block[0] &&
								local_pos[0] <= range->range.max_block[0] &&
								local_pos[1] >= range->range.min_block[2] &&
								local_pos[1] <= range->range.max_block[2])
							eaPush(&maps, height_map);
					}
				}
			}
			terrainCreateObjectGroupsWithLookup(maps, range, layer->layer, layer->dummy_layer->terrain.object_defs,
											layer->object_lookup,
											layer->source->object_table,
											layer->exclusion_version, layer->color_shift, layer->playable, force);
			eaDestroy(&maps);
		}
    }

	// Recreate the actually world cells based on what is now in our dummy layer
	if (create_entries)
		for (i = 0; i < eaSize(&layer->dummy_layer->terrain.object_defs); i++)
		{
			// This function calls a function that destroys all the things
			// "object_defs" is wrappers.
			layerUpdateObjectWrapperEntries(layer->dummy_layer->terrain.object_defs[i]);
		}
}

void terrainSourceUpdateObjectsByDef(TerrainEditorSourceLayer *layer, GroupDef *def)
{
	// TomY TODO someday implement this nicely
	int i;

	if (!layer) return;

	terrainSourceUpdateObjectConstraints(layer, def);

	layerInitObjectWrappers(layer->dummy_layer, eaiSize(&layer->object_lookup));

    if (!layer->source->objects_hidden)
    {
		int block;
		for (block = 0; block < eaSize(&layer->blocks); block++)
		{
			HeightMap **maps = NULL;
			TerrainBlockRange *range = layer->blocks[block];
			for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
			{
				IVec2 local_pos;
				heightMapGetMapLocalPos(layer->heightmap_trackers[i]->height_map, local_pos);
				if (local_pos[0] >= range->range.min_block[0] &&
						local_pos[0] <= range->range.max_block[0] &&
						local_pos[1] >= range->range.min_block[2] &&
						local_pos[1] <= range->range.max_block[2])
					eaPush(&maps, layer->heightmap_trackers[i]->height_map);
			}
			terrainCreateObjectGroupsWithLookup(maps, range, layer->layer, layer->dummy_layer->terrain.object_defs,
					layer->object_lookup,
					layer->source->object_table,
					layer->exclusion_version, layer->color_shift, layer->playable, true);
			eaDestroy(&maps);
		}
    }

	for (i = 0; i < eaSize(&layer->dummy_layer->terrain.object_defs); i++)
		layerUpdateObjectWrapperEntries(layer->dummy_layer->terrain.object_defs[i]);
}

void terrainSourceClearTouchedBits(TerrainEditorSourceLayer *layer)
{
    int i;
    for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
    {
        HeightMap *height_map = layer->heightmap_trackers[i]->height_map;
        heightMapClearTouchedBits(height_map);
    }
}

void terrainSourceSetLOD(TerrainEditorSourceLayer *layer, U32 lod)
{
	int i;
    
    if (layer->effective_mode < LAYER_MODE_TERRAIN ||
		!layer->loaded)
        return;

    lod = MAX((S32)lod, layer->loaded_lod);
	assert(((S32)lod) >= 0);
    
	for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
	{
        HeightMap *height_map = layer->heightmap_trackers[i]->height_map;
        assert(height_map);
        heightMapSetLevelOfDetail(height_map, lod);
    }
}

void terrainSourceLayerResample(TerrainEditorSourceLayer *layer, U32 lod)
{
	int i;
	IVec2 local_pos;

	for (i = 0; i < eaSize(&layer->blocks); ++i)
	{
		TerrainBlockRange *block = layer->blocks[i];
		for (local_pos[1] = block->range.min_block[2]; local_pos[1] <= block->range.max_block[2]; ++local_pos[1])
		{
			for (local_pos[0] = block->range.min_block[0]; local_pos[0] <= block->range.max_block[0]; ++local_pos[0])
			{
                HeightMap *height_map = terrainSourceGetHeightMap(layer->source, local_pos);
				if (height_map)
				{
					terrainLock();
					heightMapResample(height_map, lod);
					terrainUnlock();
				}
			}
		}
	}
	layer->loaded_lod = lod;

    //terrainSourceUpdateColors(layer, true);
    //terrainSourceUpdateNormals(layer, true);
}

#define TERRAIN_BLOCK_IDEAL_SIZE 6
#define TERRAIN_BLOCK_IDEAL_AREA (TERRAIN_BLOCK_IDEAL_SIZE*TERRAIN_BLOCK_IDEAL_SIZE) 

bool block_is_contained(TerrainBlockRange *range, IVec2 min, IVec2 max)
{
    if (range->range.min_block[0] >= min[0] &&
        range->range.max_block[0] <= max[0] &&
        range->range.min_block[2] >= min[1] &&
        range->range.max_block[2] <= max[1])
        return true;
    return false;
}

void compute_labeling_recursive(TerrainBlockRange **blocks, int num_blocks, int label, int *area_array,
                               int *label_array, int *best_solution, int *solution_array);

void try_create_block(TerrainBlockRange **blocks, int num_blocks, int *area_array, int *label_array,
                     int *best_solution, int *solution_array, int i, int j, int label)
{
    int k, ret = num_blocks;
    IVec2 min, max;
    int x, z;
    bool found = false;
    static U8 block_test_array[TERRAIN_BLOCK_IDEAL_AREA][TERRAIN_BLOCK_IDEAL_AREA];

    min[0] = MIN(blocks[i]->range.min_block[0], blocks[j]->range.min_block[0]);
    min[1] = MIN(blocks[i]->range.min_block[2], blocks[j]->range.min_block[2]);
    max[0] = MAX(blocks[i]->range.max_block[0], blocks[j]->range.max_block[0]);
    max[1] = MAX(blocks[i]->range.max_block[2], blocks[j]->range.max_block[2]);
    memset(block_test_array, 0, TERRAIN_BLOCK_IDEAL_AREA*TERRAIN_BLOCK_IDEAL_AREA);
    for (k = 0; k < num_blocks; k++)
    {
        if (k == i || k == j ||
            (label_array[k] == 0 && block_is_contained(blocks[k], min, max)))
        {
            int z0 = blocks[k]->range.min_block[2]-min[1];
            int z1 = blocks[k]->range.max_block[2]-min[1];
            int x0 = blocks[k]->range.min_block[0]-min[0];
            int x1 = blocks[k]->range.max_block[0]-min[0];
            assert(x0 >= 0 && z0 >= 0 &&
                   x1 < TERRAIN_BLOCK_IDEAL_AREA &&
                   z1 < TERRAIN_BLOCK_IDEAL_AREA);
            for (z = z0; z <= z1; ++z)
	            for (x = x0; x <= x1; ++x)
                    block_test_array[x][z] = 1;
            label_array[k] = label;
        }
    }
    for (z = 0; z < max[1]+1-min[1]; z++)
	    for (x = 0; x < max[0]+1-min[0]; x++)
            if (block_test_array[x][z] == 0)
                found = true;

    if (!found)
		compute_labeling_recursive(blocks, num_blocks, label+1, area_array, label_array, best_solution, solution_array);
        
    // Unlabel
    for (k = 0; k < num_blocks; k++)
        if (k != i && label_array[k] == label)
            label_array[k] = 0;
}

void compute_labeling_recursive(TerrainBlockRange **blocks, int num_blocks, int label, int *area_array,
                               int *label_array, int *best_solution, int *solution_array)
{
    int i, j;

    for (i = 0; i < num_blocks; i++)
    {
        if (label_array[i] == 0)
        {
            label_array[i] = label;

            // Try just the single block
            compute_labeling_recursive(blocks, num_blocks, label+1, area_array, label_array, best_solution, solution_array);
            
            if (area_array[i] < TERRAIN_BLOCK_IDEAL_AREA) 
            {
                // Try grouping blocks
                IVec3 new_min, new_max;
                copyVec3(blocks[i]->range.min_block, new_min);
                copyVec3(blocks[i]->range.max_block, new_max);
                for (j = i+1; j < num_blocks; j++)
                    if (label_array[j] == 0)
                    {
                        S32 new_area = (MAX(blocks[j]->range.max_block[0], new_max[0]) + 1 - MIN(blocks[j]->range.min_block[0], new_min[0])) *
                            (MAX(blocks[j]->range.max_block[2], new_max[2]) + 1 - MIN(blocks[j]->range.min_block[2], new_min[2]));
                        if (new_area < TERRAIN_BLOCK_IDEAL_AREA)
                        {
                            try_create_block(blocks, num_blocks, area_array, label_array, best_solution, solution_array, i, j, label);
                        }
                    }
            }
            label_array[i] = 0;
            return;
        }
    }
    if (label-1 <= *best_solution)
    {
        *best_solution = label-1;
        memcpy(solution_array, label_array, num_blocks*sizeof(int));
    }
}

bool terrainSourceOptimizeLayer(TerrainEditorSourceLayer *layer, TerrainBlockRange **new_blocks, bool do_checkout)
{
    int num_blocks = eaSize(&new_blocks);
    int *area_array = calloc(1, num_blocks*sizeof(int));
    int *label_array = calloc(1, num_blocks*sizeof(int));
    int *solution_array = calloc(1, num_blocks*sizeof(int));
    int i, best_solution = num_blocks;
    bool failure = false;

	assertHeapValidateAll();

    for (i = 0; i < num_blocks; i++)
    {
        area_array[i] = (new_blocks[i]->range.max_block[0] + 1 - new_blocks[i]->range.min_block[0]) *
            (new_blocks[i]->range.max_block[2] + 1 - new_blocks[i]->range.min_block[2]);
    }
    
    compute_labeling_recursive(new_blocks, num_blocks, 1, area_array, label_array, &best_solution, solution_array);

	if (best_solution <= eaSize(&layer->blocks))
	{
		// Reduce total number of blocks by deletion
        while (eaSize(&layer->blocks) > best_solution)
		{
            StructDestroy(parse_TerrainBlockRange, layer->blocks[best_solution]);
            eaRemove(&layer->blocks, best_solution);
		}
	}
	else
	{
		// Add new blocks
        int old_size = eaSize(&layer->blocks);
		for (i = old_size; i < best_solution; i++)
		{
            char filename[256], ext[256];
            TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
			strcpy(filename, strrchr(layer->layer->filename, '/')+1);
			*(strrchr(filename, '.')) = 0;
			sprintf(ext, "_B%d", i);
			strcat(filename, ext);
            //sprintf(filename, "%s_B%d", layer->layer->grouptree.root_def->name_str, i);
            new_block->range_name = StructAllocString(filename);
            layerTerrainUpdateFilenames(layer->layer, new_block);
            if (do_checkout && !terrainCheckoutBlock(new_block))
            {
                StructDestroy(parse_TerrainBlockRange, new_block);
                while (eaSize(&layer->blocks) > old_size)
                {
                    StructDestroy(parse_TerrainBlockRange, layer->blocks[old_size]);
                    eaRemove(&layer->blocks, old_size);
                }
                failure = true;
                break;
            }
            eaPush(&layer->blocks, new_block);
        }
	}

    if (!failure)
    {
        for (i = 0; i < best_solution; i++)
        {
            int k;
            bool found = false;
            for (k = 0; k < num_blocks; k++)
                if (solution_array[k] == (i+1))
                {
                    if (!found)
                    {
                        copyVec3(new_blocks[k]->range.min_block, layer->blocks[i]->range.min_block);
                        copyVec3(new_blocks[k]->range.max_block, layer->blocks[i]->range.max_block);
                        found = true;
                    }
                    else
                    {
                        if (new_blocks[k]->range.min_block[0] < layer->blocks[i]->range.min_block[0])
                            layer->blocks[i]->range.min_block[0] = new_blocks[k]->range.min_block[0];
                        if (new_blocks[k]->range.min_block[2] < layer->blocks[i]->range.min_block[2])
                            layer->blocks[i]->range.min_block[2] = new_blocks[k]->range.min_block[2];
                        if (new_blocks[k]->range.max_block[0] > layer->blocks[i]->range.max_block[0])
                            layer->blocks[i]->range.max_block[0] = new_blocks[k]->range.max_block[0];
                        if (new_blocks[k]->range.max_block[2] > layer->blocks[i]->range.max_block[2])
                            layer->blocks[i]->range.max_block[2] = new_blocks[k]->range.max_block[2];
                    }
                }
			layer->blocks[i]->block_idx = i;
        }
    }

    SAFE_FREE(area_array);
    SAFE_FREE(label_array);
    SAFE_FREE(solution_array);

    assertHeapValidateAll();

    return !failure;
}

bool terrainSourceModifyLayers(int iPartitionIdx, TerrainEditorSourceLayer **in_layers, TerrainEditorSourceLayer *dest_layer,
                               IVec2 min, IVec2 max)
{
    int i, j;
    TerrainBlockRange **blocks = NULL;
    int **orig_mat_list;
    int **orig_obj_list;
    TerrainEditorSourceLayer **layers = NULL;
    TerrainEditorSource *source = in_layers[0]->source;
    bool found = false;
    HeightMap **height_maps = NULL;
    int num_layers;

    eaPushEArray(&layers, &in_layers);

    if (dest_layer)
    {
        for (i = 0; i < eaSize(&layers); i++)
            if (dest_layer == layers[i])
                found = true;
        if (!found)
            eaPush(&layers, dest_layer);

        for (i = 0; i < eaSize(&dest_layer->blocks); i++)
        {
            TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
            copyVec3(dest_layer->blocks[i]->range.min_block, new_block->range.min_block);
            copyVec3(dest_layer->blocks[i]->range.max_block, new_block->range.max_block);
            eaPush(&blocks, new_block);
        }
	}

    num_layers = eaSize(&layers);

    // Calculate destination blocks
    for (i = 0; i < num_layers; i++)
    {
        if (dest_layer != layers[i])
        {
            TerrainBlockRange **blocks_to_keep = NULL;
            
            // Split blocks
            for (j = 0; j < eaSize(&layers[i]->blocks); j++)
            {
                TerrainBlockRange *block = layers[i]->blocks[j];
                if (block->range.max_block[0] < min[0] || block->range.max_block[2] < min[1] ||
                    block->range.min_block[0] > max[0] || block->range.min_block[2] > max[1])
                {
                    // Completely out of range, keep
                    TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                    copyVec3(block->range.min_block, new_block->range.min_block);
                    copyVec3(block->range.max_block, new_block->range.max_block);
                    eaPush(&blocks_to_keep, new_block);
                }
                else if (block->range.max_block[0] <= max[0] && block->range.max_block[2] <= max[1] &&
                         block->range.min_block[0] >= min[0] && block->range.min_block[2] >= min[1])
                {
                    // Completely in range, split
                    TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                    copyVec3(block->range.min_block, new_block->range.min_block);
                    copyVec3(block->range.max_block, new_block->range.max_block);
                    eaPush(&blocks, new_block);
                }
                else
                {
                    S32 bottom = block->range.min_block[2];
                    S32 left = block->range.min_block[0];
                    S32 top = block->range.max_block[2];
                    S32 right = block->range.max_block[0];
                
                    if (min[1] > block->range.min_block[2])
                    {
                        // Bottom block
                        TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                        setVec3(new_block->range.min_block, block->range.min_block[0], 0, block->range.min_block[2]);
                        setVec3(new_block->range.max_block, block->range.max_block[0], 0, min[1]-1);
                        eaPush(&blocks_to_keep, new_block);
                        bottom = min[1];
                    }
                    if (min[0] > block->range.min_block[0])
                    {
                        // Left block
                        TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                        setVec3(new_block->range.min_block, block->range.min_block[0], 0, bottom);
                        setVec3(new_block->range.max_block, min[0]-1, 0, block->range.max_block[2]);
                        eaPush(&blocks_to_keep, new_block);
                        left = min[0];                    
                    }
                    if (max[1] < block->range.max_block[2])
                    {
                        // Top block
                        TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                        setVec3(new_block->range.min_block, left, 0, max[1]+1);
                        setVec3(new_block->range.max_block, block->range.max_block[0], 0, block->range.max_block[2]);
                        eaPush(&blocks_to_keep, new_block);
                        top = max[1];
                    }
                    if (max[0] < block->range.max_block[0])
                    {
                        // Right block
                        TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                        setVec3(new_block->range.min_block, max[0]+1, 0, bottom);
                        setVec3(new_block->range.max_block, block->range.max_block[0], 0, top);
                        eaPush(&blocks_to_keep, new_block);
                        right = max[0];
                    }

                   	{
                        TerrainBlockRange *new_block = StructCreate(parse_TerrainBlockRange);
                        setVec3(new_block->range.min_block, left, 0, bottom);
                        setVec3(new_block->range.max_block, right, 0, top);
                        eaPush(&blocks, new_block);
                    }
                }
            }

            if (!terrainSourceOptimizeLayer(layers[i], blocks_to_keep, true))
            {
                // Bailing!
                eaDestroyStruct(&blocks_to_keep, parse_TerrainBlockRange);
                eaDestroyStruct(&blocks, parse_TerrainBlockRange);
                eaDestroy(&layers);
                return false;
            }
            eaDestroyStruct(&blocks_to_keep, parse_TerrainBlockRange);
        }
    }

    if (dest_layer)
    {
        if (!terrainSourceOptimizeLayer(dest_layer, blocks, true))
        {
            // Bailing!
            eaDestroyStruct(&blocks, parse_TerrainBlockRange);
            eaDestroy(&layers);
            return false;
        }
    }

    eaDestroyStruct(&blocks, parse_TerrainBlockRange);

    // After this point, the operation cannot bail.
    
    orig_mat_list = calloc(1, num_layers*sizeof(void*));
    orig_obj_list = calloc(1, num_layers*sizeof(void*));
    for (i = 0; i < num_layers; i++)
    {
        assert(layers[i]);
		orig_mat_list[i] = layers[i]->material_lookup;
        layers[i]->material_lookup = NULL;

        orig_obj_list[i] = layers[i]->object_lookup;
        layers[i]->object_lookup = NULL;
    }

    // Update blocks
    for (i = 0; i < num_layers; i++)
    {
        // Clear heightmaps out
        for (j = 0; j < eaSize(&layers[i]->heightmap_trackers); j++)
        {
            HeightMap *height_map = layers[i]->heightmap_trackers[j]->height_map;
            eaPush(&height_maps, height_map);
        }
        eaDestroyEx(&layers[i]->heightmap_trackers, heightMapTrackerClose);
    }

    // Split heightmaps
    for (i = 0; i < eaSize(&height_maps); i++)
    {
        int k, l, num_materials;
        U8 material_ids[NUM_MATERIALS];
        TerrainBuffer *buffer;
        HeightMapTracker *tracker;
        int layer_idx = -1;
		HeightMap *height_map = height_maps[i];
        TerrainEditorSourceLayer *source_layer = terrainSourceGetHeightMapLayer(source, height_map);
        TerrainEditorSourceLayer *target_layer = NULL;
        IVec2 local_pos;
        
        heightMapGetMapLocalPos(height_map, local_pos);

        for (k = 0; k < num_layers; k++)
        {
            for (l = 0; l < eaSize(&layers[k]->blocks); l++)
            {
                TerrainBlockRange *block = layers[k]->blocks[l];
                if (local_pos[0] >= block->range.min_block[0] &&
                    local_pos[0] <= block->range.max_block[0] &&
                    local_pos[1] >= block->range.min_block[2] &&
                    local_pos[1] <= block->range.max_block[2])
                {
                    target_layer = layers[k];
                    break;
                }
            }
            if (source_layer == layers[k])
                layer_idx = k;
        }
        assert(layer_idx >= 0);
        if (dest_layer)
        {
        	assert(target_layer);
        }

         if (target_layer)
         {
            // Fixup material indices
            heightMapGetMaterialIDs(height_map, material_ids, &num_materials);
            for(k = 0; k < num_materials; k++)
            {
                char idx = material_ids[k];
                if (idx > -1)
                {
                    bool mat_found = false;
                    int mat;
                    assert(idx < eaiSize(&orig_mat_list[layer_idx]));
                    mat = orig_mat_list[layer_idx][idx];
                    for (l = 0; l < eaiSize(&target_layer->material_lookup); l++)
                        if (mat == target_layer->material_lookup[l])
                        {
                            material_ids[k] = l;
                            mat_found = true;
                            break;
                        }
                    if (!mat_found)
                    {
                        material_ids[k] = l;
                        eaiPush(&target_layer->material_lookup, mat);
                    }
                }
            }
            heightMapSetMaterialIDs(height_map, material_ids, num_materials);

            // Fixup object indices
            buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, heightMapGetLoadedLOD(height_map));
            if (buffer)
            {
                for (k = 0; k < eaSize(&buffer->data_objects); k++)
                {
                    int idx = buffer->data_objects[k]->object_type;
                    bool obj_found = false;
                    int obj;
                    assert(idx < eaiSize(&orig_obj_list[layer_idx]));
                    obj = orig_obj_list[layer_idx][idx];
                    for (l = 0; l < eaiSize(&target_layer->object_lookup); l++)
                        if (obj == target_layer->object_lookup[l])
                        {
                            buffer->data_objects[k]->object_type = l;
                            obj_found = true;
                            break;
                        }
                    if (!obj_found)
                    {
                        buffer->data_objects[k]->object_type = l;
                        eaiPush(&target_layer->object_lookup, obj);                            
                    }
                }
            }

            // Add heightmap to destination layer
            heightMapSetLayer(height_map, target_layer->layer);
            tracker = heightMapTrackerOpen(height_map);
            eaPush(&target_layer->heightmap_trackers, tracker);
            heightMapTrackerOpenEntries(iPartitionIdx, tracker);
    	}
        else
        {
            assert(source_layer);
            printf("DELETING HEIGHTMAP AT %d,%d\n", local_pos[0], local_pos[1]);
            stashIntRemovePointer(source->heightmaps, getTerrainGridPosKey(local_pos), NULL);
            heightMapDestroy(height_map);
        }
    }
    eaDestroy(&height_maps);

    for (i = 0; i < num_layers; i++)
    {
		assert(layers[i]->effective_mode == LAYER_MODE_EDITABLE);
	    terrainSourceLayerSetUnsaved(layers[i]);

        if (eaSize(&layers[i]->heightmap_trackers) > 0)
        	layers[i]->loaded_lod = source->editing_lod;
        else
            layers[i]->loaded_lod = HEIGHTMAP_NO_LOD;

        eaiDestroy(&orig_mat_list[i]);
        eaiDestroy(&orig_obj_list[i]);
    }
    SAFE_FREE(orig_mat_list);
    SAFE_FREE(orig_obj_list);
    eaDestroy(&layers);

    terrainSourceRefreshHeightmaps(source, false);

    assertHeapValidateAll();
   
    return true;
}

//// Utility functions

TerrainEditorSourceLayer *terrainSourceGetHeightMapLayer(TerrainEditorSource *source, HeightMap *height_map)
{
   int i;
    ZoneMapLayer *height_map_layer = heightMapGetLayer(height_map);
    for (i = 0; i < eaSize(&source->layers); i++)
        if (source->layers[i]->layer == height_map_layer)
            return source->layers[i];
    return NULL;
}

bool terrainSourceIsLayerEditable(TerrainEditorSourceLayer *source_layer)
{
	if (!source_layer)
		return false;
	return (source_layer->effective_mode == LAYER_MODE_EDITABLE || source_layer->layer->zmap_parent->map_info.genesis_data);
}

bool terrainSourceIsHeightMapEditable(HeightMap *height_map)
{
	return terrainSourceIsLayerEditable(heightMapGetLayer(height_map)->terrain.source_data);
}

HeightMap* terrainSourceGetHeightMap(TerrainEditorSource *source, IVec2 local_pos)
{
	HeightMap* height_map;
	if (!stashIntFindPointer(source->heightmaps, getTerrainGridPosKey(local_pos), &height_map))
	{
		return NULL;
	}
	return height_map;
}


TerrainBlockRange *terrainSourceGetBlockAt(TerrainEditorSource *source, F32 x, F32 z)
{
    int num_maps = 0;
	HeightMap* map;
	int mapx;
	int mapy;
	int idx;
    IVec2 local_pos;
	TerrainBlockRange test_range;
    TerrainEditorSourceLayer *layer;
    
    terrainSourceGetTouchingHeightMaps(source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, NULL, false );
    if (num_maps == 0)
        return NULL;

    layer = terrainSourceGetHeightMapLayer(source, map);
    if (!layer)
        return NULL;

    heightMapGetMapLocalPos(map, local_pos);
	setVec3(test_range.range.min_block, local_pos[0], 0, local_pos[1]);
	setVec3(test_range.range.max_block, local_pos[0], 0, local_pos[1]);
    
    for (idx = 0; idx < eaSize(&layer->blocks); idx++)
        if (trangesOverlap(&test_range, layer->blocks[idx]))
            return layer->blocks[idx];

    return NULL;
}

void terrainSourceGetTouchingHeightMaps(TerrainEditorSource *source, F32 x, F32 z, int max_out_maps, int* num_out_maps, HeightMap* out_maps[], int out_map_x[], int out_map_y[], bool color_lod, HeightMapCache *cache, bool editable_only)
{
	// find grid position
	int pos[2];
	HeightMap* height_map;
	int mapx, mapy;
	int lod;
	bool y_edge, x_edge;

	*num_out_maps = 0;

	pos[0] = floorf( x / (GRID_BLOCK_SIZE) );
	pos[1] = floorf( z / (GRID_BLOCK_SIZE) );

	mapx = (int)( x - (pos[0] * GRID_BLOCK_SIZE) + 0.5f );
	mapy = (int)( z - (pos[1] * GRID_BLOCK_SIZE) + 0.5f );

	height_map = terrainFindHeightMap(source->heightmaps, pos, cache);
	if (height_map && (!editable_only || terrainSourceIsHeightMapEditable(height_map)))
	{
        lod = heightMapGetLoadedLOD(height_map);
        if (color_lod)
            lod = GET_COLOR_LOD(lod);

		out_maps[*num_out_maps] = height_map;
		out_map_x[*num_out_maps] = mapx >> lod;
		out_map_y[*num_out_maps] = mapy >> lod;
		(*num_out_maps)++;
		if (*num_out_maps >= max_out_maps)
			return;
	}

	x_edge = (mapx == 0 || mapx == GRID_BLOCK_SIZE);
	y_edge = (mapy == 0 || mapy == GRID_BLOCK_SIZE);

	// border cases
	if (x_edge || y_edge)
	{
		int which_y = mapy / GRID_BLOCK_SIZE;
		int which_x = mapx / GRID_BLOCK_SIZE;
		int off_pos[2];

		if (y_edge)
		{
			// top / bottom edge case
			off_pos[0] = pos[0];
			off_pos[1] = pos[1] + which_y*2 - 1;

			height_map = terrainFindHeightMap(source->heightmaps, off_pos, cache);
			if (height_map && terrainSourceIsHeightMapEditable(height_map))
			{
                lod = heightMapGetLoadedLOD(height_map);
                if (color_lod)
                    lod = GET_COLOR_LOD(lod);
				out_maps[*num_out_maps] = height_map;
				out_map_x[*num_out_maps] = mapx >> lod;
				out_map_y[*num_out_maps] = (int)((1 - which_y)*GRID_BLOCK_SIZE) >> lod;
				(*num_out_maps)++;
				if (*num_out_maps >= max_out_maps)
					return;
			}

			// corner case
			if (x_edge)
			{
				off_pos[0] = pos[0] + which_x*2 - 1;

				height_map = terrainFindHeightMap(source->heightmaps, off_pos, cache);
				if (height_map && terrainSourceIsHeightMapEditable(height_map))
				{
                    lod = heightMapGetLoadedLOD(height_map);
                    if (color_lod)
                        lod = GET_COLOR_LOD(lod);
					out_maps[*num_out_maps] = height_map;
					out_map_x[*num_out_maps] = (int)((1 - which_x)*GRID_BLOCK_SIZE) >> lod;
					out_map_y[*num_out_maps] = (int)((1 - which_y)*GRID_BLOCK_SIZE) >> lod;
					(*num_out_maps)++;
					if (*num_out_maps >= max_out_maps)
						return;
				}
			}
		}

		if (x_edge)
		{
			// left / right edge case
			off_pos[0] = pos[0] + which_x*2 - 1;
			off_pos[1] = pos[1];

			height_map = terrainFindHeightMap(source->heightmaps, off_pos, cache);
			if (height_map && terrainSourceIsHeightMapEditable(height_map))
			{
                lod = heightMapGetLoadedLOD(height_map);
                if (color_lod)
                    lod = GET_COLOR_LOD(lod);
				out_maps[*num_out_maps] = height_map;
				out_map_x[*num_out_maps] = (int)((1 - which_x)*GRID_BLOCK_SIZE) >> lod;
				out_map_y[*num_out_maps] = mapy >> lod;
				(*num_out_maps)++;
				if (*num_out_maps >= max_out_maps)
					return;
			}
		}
	}
}

void terrainSourceGetNeighborLayers(TerrainEditorSource *source, HeightMap *height_map, ZoneMapLayer *out_layers[4], int block_idxs[4], bool include_self)
{
    TerrainEditorSourceLayer *heightmap_layer; 
	int layernum, n_block;
	IVec2 local_pos;
	int layer_count = zmapGetLayerCount(NULL);
    heightMapGetMapLocalPos(height_map, local_pos);

    heightmap_layer = terrainSourceGetHeightMapLayer(source, height_map);

	out_layers[0] = out_layers[1] = out_layers[2] = out_layers[3] = NULL;

    for (layernum = 0; layernum < layer_count; layernum++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, layernum);
		if (layer && (include_self || layer != heightmap_layer->layer))
		{
			TerrainEditorSourceLayer *src_layer = layer->terrain.source_data;
            int num_blocks = (src_layer && src_layer->loaded) ? eaSize(&src_layer->blocks) : layerGetTerrainBlockCount(layer);
			for (n_block = 0; n_block < num_blocks; n_block++)
			{
				IVec2 n_min, n_max;
                if (src_layer && src_layer->loaded)
                	terrainBlockGetExtents(src_layer->blocks[n_block], n_min, n_max);
                else
                    layerGetTerrainBlockExtents(layer, n_block, n_min, n_max);
                
                if (((local_pos[0] >= n_min[0] && n_max[0] >= local_pos[0]) &&
                     local_pos[1] == n_min[1]-1))
                {
                    out_layers[0] = layer;
                    block_idxs[0] = n_block;
                }

                if (((local_pos[0] >= n_min[0] && n_max[0] >= local_pos[0]) &&
                     n_max[1] == local_pos[1]-1))
                {
                    out_layers[1] = layer;
                    block_idxs[1] = n_block;
                }

                if (((local_pos[1] >= n_min[1] && n_max[1] >= local_pos[1]) &&
                     local_pos[0] == n_min[0]-1))
                {
                    out_layers[2] = layer;
                    block_idxs[2] = n_block;
                }

                if (((local_pos[1] >= n_min[1] && n_max[1] >= local_pos[1]) &&
                     n_max[0] == local_pos[0]-1))
                {
                    out_layers[3] = layer;
                    block_idxs[3] = n_block;
                }

				if(	include_self && 
					local_pos[0] >= n_min[0] && n_max[0] >= local_pos[0] &&
					local_pos[1] >= n_min[1] && n_max[1] >= local_pos[1] )
				{
					if(local_pos[1] != n_max[1])
					{
						out_layers[0] = layer;
						block_idxs[0] = n_block;
					}
					if(local_pos[1] != n_min[1])
					{
						out_layers[1] = layer;
						block_idxs[1] = n_block;
					}
					if(local_pos[0] != n_max[0])
					{
						out_layers[2] = layer;
						block_idxs[2] = n_block;
					}
					if(local_pos[0] != n_min[0])
					{
						out_layers[3] = layer;
						block_idxs[3] = n_block;
					}
				}
			}
		}
	}
}

void terrainSetLockedEdges(TerrainEditorSource *source, bool lock_edges)
{

	StashTableIterator iter;
	StashElement elem;
	stashGetIterator(source->heightmaps, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		int i, j;
		HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
		ZoneMapLayer *layer = heightMapGetLayer(height_map);
		bool found = false;
		if(lock_edges)
		{
			for (i = 0; i < eaSize(&source->layers); i++)
			{
				TerrainEditorSourceLayer *source_layer = source->layers[i]; 
				if (layer == source_layer->layer && source_layer->loaded && terrainSourceIsLayerEditable(source_layer))
				{
					bool locked[4];
					ZoneMapLayer *neighbor_layers[] = { NULL, NULL, NULL, NULL };
					int neighbor_idxs[4];
					terrainSourceGetNeighborLayers(source, height_map, neighbor_layers, neighbor_idxs, true);
					for (j = 0; j < 4; j++)
					{
						if (neighbor_layers[j] && (neighbor_layers[j]->layer_mode == LAYER_MODE_EDITABLE || neighbor_layers[j]->zmap_parent->map_info.genesis_data))
							locked[j] = false;
						else
							locked[j] = true;
					}
					heightMapSetLockedEdges(height_map, locked[1], locked[3], locked[0], locked[2]);
					found = true;
					break;
				}
			}
		}
		if(!found)
			heightMapSetLockedEdges(height_map, false, false, false, false);
	}
}

//// Raycast for editor UI

bool terrainRayCastCheckQuad(TerrainBuffer *buffer, S32 sx, S32 sz, Vec3 start, Vec3 end, Vec3 out_pos)
{
	Vec3 min, max;
	Vec3 p1, p2, p3, p4;
	min[0] = sx*(1<<buffer->lod);
	max[0] = min[0]+(1<<buffer->lod);
	min[2] = sz*(1<<buffer->lod);
	max[2] = min[2]+(1<<buffer->lod);
	setVec3(p1, min[0], buffer->data_f32[sx+sz*buffer->size], min[2]);
	setVec3(p2, max[0], buffer->data_f32[sx+1+sz*buffer->size], min[2]);
	setVec3(p3, min[0], buffer->data_f32[sx+(sz+1)*buffer->size], max[2]);
	setVec3(p4, max[0], buffer->data_f32[sx+1+(sz+1)*buffer->size], max[2]);
	min[1] = MIN(p1[1], MIN(p2[1], MIN(p3[1], p4[1])));
	max[1] = MAX(p1[1], MAX(p2[1], MAX(p3[1], p4[1])));
	if (lineBoxCollision( start, end, min, max, out_pos ))
	{
		Vec3 tri[3];
		copyVec3(p1, tri[0]);
		copyVec3(p2, tri[1]);
		copyVec3(p4, tri[2]);
		if (triangleLineIntersect2(start, end, tri, out_pos))
		{
			return true;
		}
		copyVec3(p1, tri[0]);
		copyVec3(p4, tri[1]);
		copyVec3(p3, tri[2]);
		if (triangleLineIntersect2(start, end, tri, out_pos))
		{
			return true;
		}
	}
	return false;
}

bool terrainRayCastCheckHeightmap(HeightMap *height_map, Vec3 start, Vec3 end, Vec3 out_pos)
{
	int i;
	TerrainBuffer *height_buffer = heightMapGetOldestBuffer(height_map, TERRAIN_BUFFER_HEIGHT, -1);
	S32 width = 1<<height_buffer->lod;
	F32 px, pz;
	S32 sx = ((S32)(start[0]/width));
	S32 sz = ((S32)(start[2]/width));
	S32 dx = ((S32)(end[0]/width));
	S32 dz = ((S32)(end[2]/width));
	F32 delta[] = { (end[0]-start[0]), (end[2]-start[2]) };
	sx = CLAMP(sx, 0, height_buffer->size-2);
	sz = CLAMP(sz, 0, height_buffer->size-2);
	dx = CLAMP(dx, 0, height_buffer->size-2);
	dz = CLAMP(dz, 0, height_buffer->size-2);
	px = start[0] - sx*width;
	pz = start[2] - sz*width;
	for (i = 0; i < (S32)GRID_BLOCK_SIZE*2; i++)
	{
		int move_x = -1, move_z = -1;
		F32 test_x = -px, test_z = -pz;
		// Collided?
		if (terrainRayCastCheckQuad(height_buffer, sx, sz, start, end, out_pos))
		{
			return true;
		}

		// Done?
		if (sx == dx && sz == dz)
			return false;

		// Which way to go?
		if (delta[0] > 0)
		{
			test_x = width-px;
			move_x = 1;
		}
		if (delta[1] > 0)
		{
			test_z = width-pz;
			move_z = 1;
		}
		if ((test_x/delta[0]) < (test_z/delta[1]))
		{
			// Move in x
			sx += move_x;
			px -= move_x*width;
		}
		else
		{
			sz += move_z;
			pz -= move_z*width;
		}
		if (sx < 0 || sz < 0 || sx >= height_buffer->size-1 || sz >= height_buffer->size-1)
			return false;
	}
	return false;
}

bool terrainSourceDoRayCast(TerrainEditorSource *source, Vec3 start, Vec3 end, Vec3 out_pos, HeightMap **out_heightmap)
{
	StashTableIterator iter;
	StashElement elem;
	F32 distance, min_distance = 1e8;
	*out_heightmap = NULL;

	stashGetIterator(source->heightmaps, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		F32 padding;
		Vec3 new_start, new_end, min, max;
		HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
		heightMapGetBounds(height_map, min, max);
		padding = (1<<heightMapGetLoadedLOD(height_map)) * 0.5f;
		min[1] -= padding;
		max[1] += padding; // Padding helps with thin bounding boxes
		if (lineBoxCollision( start, end, min, max, new_start ))
		{
			distance = distance3(new_start, start);
			if (distance < min_distance)
			{
				Vec3 heightmap_pos;
				lineBoxCollision( end, start, min, max, new_end );
				new_start[0] -= min[0];
				new_start[2] -= min[2];
				new_end[0] -= min[0];
				new_end[2] -= min[2];
				if (terrainRayCastCheckHeightmap(height_map, new_start, new_end, heightmap_pos))
				{
					out_pos[0] = heightmap_pos[0] + min[0];
					out_pos[1] = heightmap_pos[1];
					out_pos[2] = heightmap_pos[2] + min[2];
					min_distance = distance;
					*out_heightmap = height_map;
				}
			}
		}
	}
	if (min_distance < 1e8)
	{
		return true;
	}
	return false;
}

//// Interpolated getters

bool terrainSourceGetInterpolatedHeight(TerrainEditorSource *source, F32 x, F32 z, F32 *height, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	int m;
	F32 interpX;
	F32 interpY;
	int startX, startY;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, false );

	if (num_maps <= 0)
	{
		return false;
	}

	// Find the appropriate height map and starting position
	for (m = 0; m < num_maps; m++)
	{
		if(!terrainGetInterpolatedValues(x, z, map[m], mapx[m], mapy[m], &startX, &startY, &interpX, &interpY))
			continue;

		*height = heightMapGetInterpolatedHeight(map[m], startX, startY, interpX, interpY);
		return true;	
	}
	// Use a single point, because we must be very close to an intersection
	*height = heightMapGetHeight( map[0], mapx[0], mapy[0]);
	return true;
}

bool terrainSourceGetInterpolatedNormal(TerrainEditorSource *source, F32 x, F32 z, U32 lod, U8Vec3 normal, HeightMapCache *cache)
{
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	int m;
	F32 interpX;
	F32 interpY;
	int startX, startY;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, false );

	if (num_maps <= 0)
	{
		return false;
	}

	// Find the appropriate normal map and starting position
	for (m = 0; m < num_maps; m++)
	{
		if(!terrainGetInterpolatedValues(x, z, map[m], mapx[m], mapy[m], &startX, &startY, &interpX, &interpY))
			continue;
		heightMapGetInterpolatedNormal(map[m], startX, startY, interpX, interpY, lod, normal);
		return true;	
	}
	// Use a single point, because we must be very close to an intersection
	heightMapGetNormal( map[0], mapx[0], mapy[0], lod, normal);
	return true;
}

bool terrainSourceGetInterpolatedSoilDepth(TerrainEditorSource *source, F32 x, F32 z, F32 *depth, HeightMapCache *cache)
{
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	int m;
	F32 interpX;
	F32 interpY;
	int startX, startY;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, false );

	if (num_maps <= 0)
	{
		return false;
	}

	// Find the appropriate normal map and starting position
	for (m = 0; m < num_maps; m++)
	{
		if(!terrainGetInterpolatedValues(x, z, map[m], mapx[m], mapy[m], &startX, &startY, &interpX, &interpY))
			continue;
		heightMapGetInterpolatedSoilDepth(map[m], startX, startY, interpX, interpY, depth);
		return true;
	}
	// Use a single point, because we must be very close to an intersection
	(*depth) = heightMapGetSoilDepth(map[0], mapx[0], mapy[0]);
	return true;
}

//// Regular getters

bool terrainSourceGetHeight(TerrainEditorSource *source, F32 x, F32 z, F32 *height, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, cache, false );
	if (num_maps > 0)
	{
		*height = heightMapGetHeight( map, mapx, mapy );
		return true;
    }
	return false;
}

bool terrainSourceGetNormal(TerrainEditorSource *source, F32 x, F32 z, U8 normal[3], HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, cache, false );
	if (num_maps > 0)
	{
		heightMapGetNormal( map, mapx, mapy, heightMapGetLoadedLOD(map), normal);
		return true;
	}
	return false;
}

bool terrainSourceGetSelection(TerrainEditorSource *source, F32 x, F32 z, F32 *sel, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, cache, false );
	if (num_maps > 0)
	{
		*sel = heightMapGetSelection( map, mapx, mapy );
		return true;
    }
	return false;
}

bool terrainSourceGetAttribute(TerrainEditorSource *source, F32 x, F32 z, U8 *attr, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, cache, false );
	if (num_maps > 0)
	{
		*attr = heightMapGetAttribute( map, mapx, mapy );
		return true;
	}
	return false;
}

bool terrainSourceGetShadow(TerrainEditorSource *source, F32 x, F32 z, F32 *sel, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map[4];
    int m;
	int mapx[4];
	int mapy[4];
	F32 interpX;
	F32 interpY;
	int startX, startY;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, false );
	for (m = 0; m < num_maps; m++)
	{
		if(!terrainGetInterpolatedValues(x, z, map[m], mapx[m], mapy[m], &startX, &startY, &interpX, &interpY))
			continue;
		*sel = heightMapGetInterpolatedShadow( map[m], startX, startY, interpX, interpY);
		return true;
    }
	return false;
}

bool terrainSourceGetSoilDepth(TerrainEditorSource *source, F32 x, F32 z, F32 *depth, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, cache, false );
	if (num_maps > 0)
	{
		*depth = heightMapGetSoilDepth( map, mapx, mapy );
		return true;
    }
	return false;
}

bool terrainSourceGetColor(TerrainEditorSource *source, F32 x, F32 z, Color *color, HeightMapCache *cache )
{
	int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, true, cache, false );
	if (num_maps > 0)
	{
		*color = heightMapGetVertexColor( map, mapx, mapy, GET_COLOR_LOD(heightMapGetLoadedLOD(map)) );
		return true;
    }
	return false;
}

TerrainMaterialWeight *terrainSourceGetMaterialWeights(TerrainEditorSource *source, F32 x, F32 z, const char *material_names[NUM_MATERIALS], int *material_count)
{
	int i; 
	int num_maps = 0;
	HeightMap* map;
	int mapx;
	int mapy;
    int mat_count;
    const char *wrong_material_names[NUM_MATERIALS];

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, NULL, false );
	if(num_maps)
	{
		TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map);
        U8 material_ids[NUM_MATERIALS];
        assert(layer);
        heightMapGetMaterialIDs(map, material_ids, &mat_count);
		if(material_count)
            *material_count = mat_count;
        for (i = 0; i < NUM_MATERIALS; i++)
            if (material_ids[i] == 0xFF)
                material_names[i] = NULL;
        	else
			{
				if (material_ids[i] < 0 || material_ids[i] >= eaiSize(&layer->material_lookup))
				{
					material_names[i] = NULL;
				}
				else
				{
					int id = layer->material_lookup[material_ids[i]];
					if (!source->material_table || id < 0 || id > eaSize(&source->material_table))
					{
						material_names[i] = NULL;
					}
					else
					{
		                material_names[i] = source->material_table[id];
					}
				}
			}
		return heightMapGetMaterialWeights( map, mapx, mapy, wrong_material_names );
	}

	for(i=0; i < NUM_MATERIALS; i++)
	{
		material_names[i] = NULL;
	}

	if(material_count)
	    (*material_count) = 0;
    return NULL;
}

F32 terrainSourceGetMaterialWeight(TerrainEditorSource *source, F32 x, F32 z, U8 global_mat)
{
	int j; 
	int num_maps = 0;
	HeightMap* map;
	int mapx;
	int mapy;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapy, false, NULL, false );
	if(num_maps)
	{
		TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map);
		U8 local_mat = UNDEFINED_MAT;
		assert(layer);

		for( j=0; j < eaiSize(&layer->material_lookup); j++ )
		{
			if(layer->material_lookup[j] == global_mat)
			{
				local_mat = j;
				break;
			}
		}
		if(local_mat == UNDEFINED_MAT)
			return 0.0f;

		return heightMapGetMaterialWeight(map, mapx, mapy, local_mat);
	}
	return 0.0f;
}

void terrainSourceGetObjectDensities(TerrainEditorSource *source, F32 x, F32 z,
                                               TerrainObjectEntry ***entries, U32 **densities)
{
	int k, num_maps = 0;
	HeightMap* map;
	int mapx;
	int mapz;

    eaClear(entries);
    eaiClear(densities);

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapz, false, NULL, false );
	if(num_maps)
	{
        TerrainBuffer *buffer;
		TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map);
        assert(layer);
		buffer = heightMapGetBuffer(map, TERRAIN_BUFFER_OBJECTS, heightMapGetLoadedLOD(map));

        if (buffer)
		{
            for(k=0; k < eaSize(&buffer->data_objects); k++)
            {
                int idx;
                U32 density;
                TerrainObjectEntry *object_info;
                assert(buffer->data_objects[k]->object_type >= 0 &&
                       buffer->data_objects[k]->object_type < eaiSize(&layer->object_lookup));
                idx = layer->object_lookup[buffer->data_objects[k]->object_type];
                assert(idx >= 0 && idx < eaSize(&source->object_table));
                object_info = source->object_table[idx];
                density = buffer->data_objects[k]->density[mapx + mapz*buffer->size];
                
                eaPush(entries, object_info);
                eaiPush(densities, density);
            }
		}
    }
	
	return;
}

F32 terrainSourceGetObjectDensity(TerrainEditorSource *source, F32 x, F32 z, U8 obj)
{
	int k, num_maps = 0;
	HeightMap* map;
	int mapx;
	int mapz;

	terrainSourceGetTouchingHeightMaps( source, x, z, 1, &num_maps, &map, &mapx, &mapz, false, NULL, false );
	if(num_maps)
	{
		TerrainBuffer *buffer;
		TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map);
		assert(layer);
		buffer = heightMapGetBuffer(map, TERRAIN_BUFFER_OBJECTS, heightMapGetLoadedLOD(map));

		if (buffer)
		{
			for(k=0; k < eaSize(&buffer->data_objects); k++)
			{
				int idx;
				assert(buffer->data_objects[k]->object_type >= 0 &&
					buffer->data_objects[k]->object_type < eaiSize(&layer->object_lookup));
				idx = layer->object_lookup[buffer->data_objects[k]->object_type];
				if(idx == obj)
					return buffer->data_objects[k]->density[mapx + mapz*buffer->size];
			}
		}
	}
	return 0.0f;
}


//// Drawing functions

void terrainSourceDrawSoilDepth(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, F32 delta_soil_depth, HeightMapCache *cache)
{
	int m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	F32 old_soil_depth;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, true );
	if (num_maps > 0)
	{
		old_soil_depth = heightMapGetSoilDepth( map[0], mapx[0], mapy[0] );
		for (m = 0; m < num_maps; m++)
		{
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			heightMapMakeBackup( map[m], mapx[m], mapy[m], draw_lod, TERRAIN_BUFFER_SOIL_DEPTH, -1);
			heightMapSetSoilDepth( map[m], mapx[m], mapy[m], CLAMP(old_soil_depth+delta_soil_depth, 0, MAX_SOIL_DEPTH));
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
            terrainSourceLayerSetUnsaved(layer);
		}
    }
}

void terrainSourceDrawHeight(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, F32 delta_height, HeightMapCache *cache)
{
	int m, i, j;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	F32 old_height;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, true );
	for (m = 0; m < num_maps; m++)
	{
		if (heightMapEdgeIsLocked(map[m], mapx[m], mapy[m])) 
			return;
	}

	if (num_maps > 0)
	{
		old_height = heightMapGetHeight( map[0], mapx[0], mapy[0] );
		for (m = 0; m < num_maps; m++)
		{
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			int size = heightMapGetSize(map[m]);
			IVec2 local_pos;
			heightMapGetMapLocalPos(map[m], local_pos);
			heightMapMakeBackup( map[m], mapx[m], mapy[m], draw_lod, TERRAIN_BUFFER_HEIGHT, -1);
			layer->disable_normals = true;
			heightMapSetHeight( map[m], mapx[m], mapy[m], old_height+delta_height);

			for (j = 0; j < 3; ++j)
			{
				for (i = 0; i < 3; ++i)
				{
					if (i == 1 && j == 1)
						continue;
					if ((i == 0 && mapx[m] == 0 ||
						i == 2 && mapx[m] == size-1 ||
						i == 1) &&
						(j == 0 && mapy[m] == 0 || 
						j == 2 && mapy[m] == size-1 ||
						j == 1))
					{
						HeightMap *height_map;
						IVec2 pos;
						setVec2(pos, local_pos[0] + i - 1, local_pos[1] + j - 1);
						if ( (height_map = terrainFindHeightMap(source->heightmaps, pos, cache)) )
							heightMapUpdateGeometry(height_map);
					}
				}
			}

			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
            terrainSourceLayerSetUnsaved(layer);
		}
    }
}

void terrainSourceDrawSelection(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, F32 delta_sel, HeightMapCache *cache)
{
	int m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	F32 old_sel;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, true );
	if (num_maps > 0)
	{
		old_sel = heightMapGetSelection( map[0], mapx[0], mapy[0] );
		for (m = 0; m < num_maps; m++)
		{
            int lod = heightMapGetLoadedLOD(map[m]);
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
            if (!heightMapGetBuffer(map[m], TERRAIN_BUFFER_SELECTION, lod))
                heightMapCreateBuffer(map[m], TERRAIN_BUFFER_SELECTION, lod);
			heightMapMakeBackup( map[m], mapx[m], mapy[m], draw_lod, TERRAIN_BUFFER_SELECTION, -1);
			heightMapSetSelection( map[m], mapx[m], mapy[m], CLAMP(old_sel+delta_sel, 0.f, 1.f));
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
            terrainSourceLayerSetUnsaved(layer);
		}
    }
}

void terrainSourceDrawColor(TerrainEditorSource *source, F32 x, F32 z, U32 draw_lod, Color color, F32 strength, HeightMapCache *cache)
{
	int m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	U8Vec3 old_color, new_color;

	strength = CLAMP(strength, 0, 1);

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, true, cache, true );
	if (num_maps > 0)
	{
		int color_draw_lod = GET_COLOR_LOD(draw_lod);
		int shift = (draw_lod - heightMapGetLoadedLOD(map[0]));

		heightMapGetVertexColorVec3( map[0], mapx[0]>>shift, mapy[0]>>shift, color_draw_lod, &old_color);

		new_color[0] = old_color[0] * (1-strength) + (float)color.r * strength;
		new_color[1] = old_color[1] * (1-strength) + (float)color.g * strength;
		new_color[2] = old_color[2] * (1-strength) + (float)color.b * strength;

		for (m = 0; m < num_maps; m++)
		{
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			heightMapMakeBackup(map[m], mapx[m]>>COLOR_LOD_DIFF, mapy[m]>>COLOR_LOD_DIFF, draw_lod, TERRAIN_BUFFER_COLOR, -1);
            shift = (draw_lod - heightMapGetLoadedLOD(map[m]));
			heightMapSetVertexColorVec3( map[m], mapx[m]>>shift, mapy[m]>>shift, color_draw_lod, new_color);
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
            terrainSourceLayerSetUnsaved(layer);
		}
    }
}

void terrainSourceDrawTerrainType(TerrainEditorSource *source, F32 x, F32 z, U32 draw_lod, U8 type, F32 strength, HeightMapCache *cache)
{
	int granularity = GET_COLOR_LOD(draw_lod);
	int step = (1<<granularity);

	int m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	TerrainBuffer *buffer;

	if(strength < 1.0f)
		return;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, true );
	if (num_maps > 0)
	{
		for (m = 0; m < num_maps; m++)
		{
			TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			U32 map_lod = heightMapGetLoadedLOD(map[m]);
			buffer = heightMapGetBuffer(map[m], TERRAIN_BUFFER_ATTR, map_lod);
			if (!buffer)
				buffer = heightMapCreateBuffer(map[m], TERRAIN_BUFFER_ATTR, map_lod);
			heightMapMakeBackup( map[m], mapx[m], mapy[m], draw_lod, TERRAIN_BUFFER_ATTR, -1);
			heightMapSetAttribute( map[m], mapx[m], mapy[m], type);
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
			terrainSourceLayerSetUnsaved(layer);
		}
	}
}

void terrainSourceDrawAlpha(TerrainEditorSource *source, F32 x, F32 z, U32 lod, U8 alpha)
{
	int m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, NULL, true );
	if (num_maps > 0)
	{
		int shift = (lod - heightMapGetLoadedLOD(map[0]));
		for (m = 0; m < num_maps; m++)
		{
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			heightMapMakeBackup(map[m], mapx[m], mapy[m], lod, TERRAIN_BUFFER_ALPHA, -1);
			heightMapSetAlpha( map[m], mapx[m]>>shift, mapy[m]>>shift, lod, alpha);
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
            terrainSourceLayerSetUnsaved(layer);
		}
    }
}

void terrainSourceDrawOcclusion(TerrainEditorSource *source, F32 x, F32 z, bool reverse, S32 *last_draw_pos)
{
    HeightMap *height_map;
    TerrainEditorSourceLayer *layer;
    TerrainBuffer *buffer;
    IVec2 pos;
    S32 p1x = (S32)x, p1z = (S32)z;

	pos[0] = (p1x / GRID_BLOCK_SIZE) + ((p1x < 0) ? -1 : 0);
	pos[1] = (p1z / GRID_BLOCK_SIZE) + ((p1z < 0) ? -1 : 0);

	if (pos[0] < 0)
		p1x += -pos[0] * GRID_BLOCK_SIZE;
	if (pos[1] < 0)
		p1z += -pos[1] * GRID_BLOCK_SIZE;

	p1x -= (p1x & 0x3f);
	p1z -= (p1z & 0x3f);

    p1x = (p1x & 0xff) / 64;
    p1z = (p1z & 0xff) / 64;

	if ((pos[0]<<2)+p1x == last_draw_pos[0] &&
		(pos[1]<<2)+p1z == last_draw_pos[1])
		return;

	last_draw_pos[0] = (pos[0]<<2)+p1x;
	last_draw_pos[1] = (pos[1]<<2)+p1z;
   
    height_map = terrainSourceGetHeightMap(source, pos);

    if (!height_map)
        return;

    layer = terrainSourceGetHeightMapLayer(source, height_map);

    buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OCCLUSION, 5);
    if (!buffer)
        buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OCCLUSION, 5);

    heightMapMakeBackup(height_map, 0, 0, heightMapGetLoadedLOD(height_map), TERRAIN_BUFFER_OCCLUSION, -1);
	if (reverse)
	{
		// 1 -> 0, 0 -> 2, 2 -> 2
		U8 val = buffer->data_byte[p1x+p1z*4];
		buffer->data_byte[p1x+p1z*4] = (val == 1) ? 0 : 2;
	}
	else
	{
		// 0 -> 1, 2 -> 0, 1 -> 1
		U8 val = buffer->data_byte[p1x+p1z*4];
		buffer->data_byte[p1x+p1z*4] = (val == 2) ? 0 : 1;
	}
    heightMapModify(height_map);
	heightMapUpdateGeometry(height_map);
    
	assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
    terrainSourceLayerSetUnsaved(layer);
}


typedef enum
{
	MAP_FULL = -1,
	NEEDS_MATERIAL = -2,
} MapMaterialSearch;

void terrainSourceDrawMaterial(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, U8 global_mat, F32 strength, HeightMapCache *cache)
{
	int num_maps, m;
	HeightMap *map[4];
	int mapx[4], mapz[4];
	U8 local_mat[] = { 0xFF, 0xFF, 0xFF, 0xFF };

	assert(source->material_table);

	if (strength == 0.f) 
		return;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapz, false, cache, true );
	if (num_maps > 0)
	{
		int i, j;
		MapMaterialSearch map_mat_index[4] = {MAP_FULL, MAP_FULL, MAP_FULL, MAP_FULL};
		TerrainMaterialWeight *old_weights = heightMapGetMaterialWeights( map[0], mapx[0], mapz[0], NULL);
		TerrainMaterialWeight new_weights;
		F32 min_weight = 256, total = 0;

		// Check maps for needed materials
		for (m = 0; m < num_maps; m++)
		{
			U8 material_ids[NUM_MATERIALS];
			int material_count;
			TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);

			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
			terrainSourceLayerSetUnsaved(layer);

			// Look up material on local layer
			for (i = 0; i < eaiSize(&layer->material_lookup); i++)
				if (layer->material_lookup[i] == global_mat)
				{
					local_mat[m] = i;
					break;
				}
			if (local_mat[m] == 0xFF)
			{
				eaiPush(&layer->material_lookup, global_mat);
				local_mat[m] = eaiSize(&layer->material_lookup)-1;
			}

			// Find a matching material location or an empty location on main map.
			heightMapGetMaterialIDs(map[m], material_ids, &material_count);
			for(i=0; i < NUM_MATERIALS; i++)
			{
				// If we found a match, use this index and stop looking.
				if( material_ids[i] != UNDEFINED_MAT && 
						local_mat[m] == material_ids[i])
				{
					map_mat_index[m] = i;
					break;
				}
				else if(i >= material_count)
				{
					map_mat_index[m] = NEEDS_MATERIAL;
					break;
				}
			}

			// If no matching materials and no empty spots for a new one, do not paint.
			if(map_mat_index[m] == MAP_FULL)
				return;
		}

		// Do allocation in a second loop so that we don't unnessesarily add materials
		for (m = 0; m < num_maps; m++)
		{
			if(map_mat_index[m] == NEEDS_MATERIAL)
			{
				// Add the new mat to the material list.
				U8 material_ids[NUM_MATERIALS];
				int material_count;
				TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
				TerrainMaterialChange *mat_change = calloc(1, sizeof(TerrainMaterialChange));
				mat_change->height_map = map[m];

				heightMapGetMaterialIDs(map[m], material_ids, &material_count);

				// Save old values
				memcpy(mat_change->old_material_ids, material_ids, sizeof(U8)*NUM_MATERIALS);
				mat_change->old_material_count = material_count;

				map_mat_index[m] = heightMapAddMaterial(map[m], local_mat[m]);
				material_ids[map_mat_index[m]] = local_mat[m];

				// Save new values
				memcpy(mat_change->new_material_ids, material_ids, sizeof(U8)*NUM_MATERIALS);
				mat_change->new_material_count = material_count+1;

				// Push onto stack for undo purposes later
				if(layer->added_mat_list == NULL)
					layer->added_mat_list = calloc(1, sizeof(TerrainMaterialChangeList));
				eaPush(&layer->added_mat_list->list, mat_change);
			}
		}

		// Set the new weight for our material
		assert(map_mat_index[0] < NUM_MATERIALS && map_mat_index[0] >= 0);
		new_weights.weights[map_mat_index[0]] = MIN(255.f, (strength*256.f) + (F32)(old_weights->weights[map_mat_index[0]]));

		// Find weights sum minus the one we are changing
		for (i=0; i < NUM_MATERIALS; i++)
		{
			if (i != map_mat_index[0])
				total += old_weights->weights[i];
		}

		// Set the new weights for all other materials.
		for (i = 0; i < NUM_MATERIALS; i++)
		{
			if (i != map_mat_index[0]) 
			{
				if (total > 0)
				{
					new_weights.weights[i] = old_weights->weights[i] * (255.f-(F32)new_weights.weights[map_mat_index[0]])/total;
					if (new_weights.weights[i] < 0.001f) 
						new_weights.weights[i] = 0.f;
				}
				else
				{
					new_weights.weights[i] = 0; 
				}
			}
		}

		// Apply new weights
		for (m = 0; m < num_maps; m++)
		{
			heightMapMakeBackup(map[m], mapx[m], mapz[m], draw_lod, TERRAIN_BUFFER_MATERIAL, -1);
		}
		{
			TerrainEditorSourceLayer *layer_0 = terrainSourceGetHeightMapLayer(source, map[0]);
			U8 material_ids_0[NUM_MATERIALS];
			int material_count_0;

			heightMapGetMaterialIDs(map[0], material_ids_0, &material_count_0);
			heightMapSetMaterialWeights( map[0], mapx[0], mapz[0], new_weights);
			assert(layer_0->effective_mode == LAYER_MODE_EDITABLE || layer_0->layer->zmap_parent->map_info.genesis_data);
			terrainSourceLayerSetUnsaved(layer_0);

			for (m=1; m < num_maps; m++)
			{
				//For all other maps, re-arrange weights to match the order of the material names in that map.
				TerrainMaterialWeight re_arranged_weights = {0};
				TerrainEditorSourceLayer *layer_m = terrainSourceGetHeightMapLayer(source, map[m]);
				U8 material_ids_m[NUM_MATERIALS];
				int material_count_m;
				heightMapGetMaterialIDs(map[m], material_ids_m, &material_count_m);

				for(i=0; i < material_count_0; i++)
				{
					for(j=0; j < NUM_MATERIALS; j++)
					{
						//If we found a match, copy new weights in
						if(material_ids_m[j] != UNDEFINED_MAT &&
								layer_0->material_lookup[material_ids_0[i]] ==
								layer_m->material_lookup[material_ids_m[j]])
						{	
							re_arranged_weights.weights[j] = new_weights.weights[i];
							break;
						}
					}
					//assert(j < NUM_MATERIALS || new_weights.weights[i] == 0.f);  //Generally this is bad, but no need to assert
				}
				heightMapSetMaterialWeights( map[m], mapx[m], mapz[m], re_arranged_weights);
			}
		}
	}
}

void terrainSourceReplaceMaterial(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, U8 global_mat_old, U8 global_mat_new, F32 strength, HeightMapCache *cache)
{
	int num_maps, m;
	HeightMap *map[4];
	int mapx[4], mapz[4];
	U8 local_mat_new[] = { 0xFF, 0xFF, 0xFF, 0xFF };
	U8 local_mat_old[] = { 0xFF, 0xFF, 0xFF, 0xFF };

	assert(source->material_table);

	if (strength == 0.f) 
		return;

	if (global_mat_old == global_mat_new)
		return;

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapz, false, cache, true );
	if (num_maps > 0)
	{
		int i, j;
		MapMaterialSearch map_mat_index_new[4] = {MAP_FULL, MAP_FULL, MAP_FULL, MAP_FULL};
		MapMaterialSearch map_mat_index_old[4] = {MAP_FULL, MAP_FULL, MAP_FULL, MAP_FULL};
		TerrainMaterialWeight *old_weights = heightMapGetMaterialWeights( map[0], mapx[0], mapz[0], NULL);
		TerrainMaterialWeight new_weights;

		// Check maps for needed materials
		for (m = 0; m < num_maps; m++)
		{
			U8 material_ids[NUM_MATERIALS];
			int material_count;
			TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);

			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
			terrainSourceLayerSetUnsaved(layer);

			// Look up material on local layer
			for (i = 0; i < eaiSize(&layer->material_lookup); i++)
				if (layer->material_lookup[i] == global_mat_old)
				{
					local_mat_old[m] = i;
					break;
				}

			if (local_mat_old[m] != 0xFF)
			{
				// Layer contains old material
				for (i = 0; i < eaiSize(&layer->material_lookup); i++)
					if (layer->material_lookup[i] == global_mat_new)
					{
						local_mat_new[m] = i;
						break;
					}
				if (local_mat_new[m] == 0xFF)
				{
					eaiPush(&layer->material_lookup, global_mat_new);
					local_mat_new[m] = eaiSize(&layer->material_lookup)-1;
				}

				// Find a matching material location or an empty location on main map.
				heightMapGetMaterialIDs(map[m], material_ids, &material_count);
				for (i = 0; i < NUM_MATERIALS; i++)
				{
					// If we found a match, use this index and stop looking.
					if( material_ids[i] != UNDEFINED_MAT && 
						local_mat_old[m] == material_ids[i])
					{
						map_mat_index_old[m] = i;
						break;
					}
				}
				if (map_mat_index_old[m] != MAP_FULL)
				{
					// Heightmap contains old material
					for (i = 0; i < NUM_MATERIALS; i++)
					{
						// If we found a match, use this index and stop looking.
						if( material_ids[i] != UNDEFINED_MAT && 
								local_mat_new[m] == material_ids[i])
						{
							map_mat_index_new[m] = i;
							break;
						}
						else if(i >= material_count)
						{
							map_mat_index_new[m] = NEEDS_MATERIAL;
							break;
						}
					}

					// If no matching materials and no empty spots for a new one, do not paint.
					if (map_mat_index_new[m] == MAP_FULL)
						return;
				}
			}
		}

		// Do allocation in a second loop so that we don't unnessesarily add materials
		for (m = 0; m < num_maps; m++)
		{
			if (map_mat_index_new[m] == NEEDS_MATERIAL)
			{
				// Add the new mat to the material list.
				U8 material_ids[NUM_MATERIALS];
				int material_count;
				TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
				TerrainMaterialChange *mat_change = calloc(1, sizeof(TerrainMaterialChange));
				mat_change->height_map = map[m];

				heightMapGetMaterialIDs(map[m], material_ids, &material_count);

				// Save old values
				memcpy(mat_change->old_material_ids, material_ids, sizeof(U8)*NUM_MATERIALS);
				mat_change->old_material_count = material_count;

				map_mat_index_new[m] = heightMapAddMaterial(map[m], local_mat_new[m]);
				material_ids[map_mat_index_new[m]] = local_mat_new[m];

				// Save new values
				memcpy(mat_change->new_material_ids, material_ids, sizeof(U8)*NUM_MATERIALS);
				mat_change->new_material_count = material_count+1;

				// Push onto stack for undo purposes later
				if(layer->added_mat_list == NULL)
					layer->added_mat_list = calloc(1, sizeof(TerrainMaterialChangeList));
				eaPush(&layer->added_mat_list->list, mat_change);
			}
		}

		if (map_mat_index_old[0] >= 0 && map_mat_index_old[0] < NUM_MATERIALS)
		{
			F32 amount_to_take;
			// Set the new weight for our material
			assert(map_mat_index_new[0] < NUM_MATERIALS && map_mat_index_new[0] >= 0);
			amount_to_take = MIN(old_weights->weights[map_mat_index_old[0]], strength*256.f);
			new_weights.weights[map_mat_index_new[0]] = MIN(255.f, old_weights->weights[map_mat_index_new[0]]+amount_to_take);
			new_weights.weights[map_mat_index_old[0]] = MAX(0, old_weights->weights[map_mat_index_old[0]]-amount_to_take);
		}

		// Leave weights the same for all other materials.
		for (i = 0; i < NUM_MATERIALS; i++)
		{
			if (i != map_mat_index_new[0] && i != map_mat_index_old[0]) 
			{
				new_weights.weights[i] = old_weights->weights[i];
			}
		}

		// Apply new weights
		for (m = 0; m < num_maps; m++)
		{
			heightMapMakeBackup(map[m], mapx[m], mapz[m], draw_lod, TERRAIN_BUFFER_MATERIAL, -1);
		}
		{
			TerrainEditorSourceLayer *layer_0 = terrainSourceGetHeightMapLayer(source, map[0]);
			U8 material_ids_0[NUM_MATERIALS];
			int material_count_0;

			heightMapGetMaterialIDs(map[0], material_ids_0, &material_count_0);
			heightMapSetMaterialWeights( map[0], mapx[0], mapz[0], new_weights);
			assert(layer_0->effective_mode == LAYER_MODE_EDITABLE || layer_0->layer->zmap_parent->map_info.genesis_data);
			terrainSourceLayerSetUnsaved(layer_0);

			for (m=1; m < num_maps; m++)
			{
				//For all other maps, re-arrange weights to match the order of the material names in that map.
				TerrainMaterialWeight re_arranged_weights = {0};
				TerrainEditorSourceLayer *layer_m = terrainSourceGetHeightMapLayer(source, map[m]);
				U8 material_ids_m[NUM_MATERIALS];
				int material_count_m;
				heightMapGetMaterialIDs(map[m], material_ids_m, &material_count_m);

				for(i=0; i < material_count_0; i++)
				{
					for(j=0; j < NUM_MATERIALS; j++)
					{
						//If we found a match, copy new weights in
						if(material_ids_m[j] != UNDEFINED_MAT &&
								layer_0->material_lookup[material_ids_0[i]] ==
								layer_m->material_lookup[material_ids_m[j]])
						{	
							re_arranged_weights.weights[j] = new_weights.weights[i];
							break;
						}
					}
					//assert(j < NUM_MATERIALS || new_weights.weights[i] == 0.f);  //Generally this is bad, but no need to assert
				}
				heightMapSetMaterialWeights( map[m], mapx[m], mapz[m], re_arranged_weights);
			}
		}
	}
}

void terrainSourceDrawAllExistingObjects(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, TerrainObjectDensity density, HeightMapCache *cache)
{
	int m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, true );
	if (num_maps > 0)
	{
		for (m = 0; m < num_maps; m++)
		{
			TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			heightMapSetAllObjectsAndBackup( map[m], mapx[m], mapy[m], draw_lod, density);
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
			terrainSourceLayerSetUnsaved(layer);
		}
	}
}

void terrainSourceDrawObjects(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, U8 global_object, TerrainObjectDensity density, F32 strength, HeightMapCache *cache)
{
	int i, m;
	int num_maps = 0;
	HeightMap* map[4];
	int mapx[4];
	int mapy[4];
	F32 delta_density; 
	TerrainObjectDensity old_density;
	F32 delta_fraction;
    U8 local_object[] = { 0xFF, 0xFF, 0xFF, 0xFF };

    assert(source->object_table);

	terrainSourceGetTouchingHeightMaps( source, x, z, 4, &num_maps, map, mapx, mapy, false, cache, true );
	if (num_maps > 0)
	{
        for (m = 0; m < num_maps; m++)
        {
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
            // Look up object on local layer
            for (i = 0; i < eaiSize(&layer->object_lookup); i++)
                if (layer->object_lookup[i] == global_object)
                {
                    local_object[m] = i;
                    break;
                }
            if (local_object[m] == 0xFF)
            {
                eaiPush(&layer->object_lookup, global_object);
                local_object[m] = eaiSize(&layer->object_lookup)-1;
            }
        }
            
		old_density = heightMapGetObjectDensity(map[0], mapx[0], mapy[0], local_object[0]);

		delta_density = (density - old_density)*strength;
		delta_fraction = delta_density - (U32)delta_density;
		//So that drawing 0.1 will still do something
		if(delta_density > 0)
			delta_density += ((rand()%1000) < (delta_fraction*1000)) ? 1.f : 0.f;
		else
			delta_density -= ((rand()%1000) < (-delta_fraction*1000)) ? 1.f : 0.f;

		//Clamp
		if(old_density + delta_density < 0)
			delta_density = -old_density;
		else if(old_density + delta_density > MAX_OBJECT_DENSITY)
			delta_density = MAX_OBJECT_DENSITY-old_density;

		//No need to draw if 0
		if (delta_density == 0)  
			return;

		for (m = 0; m < num_maps; m++)
		{
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, map[m]);
			heightMapMakeBackup(map[m], mapx[m], mapy[m], draw_lod, TERRAIN_BUFFER_OBJECTS, local_object[m]); 
			heightMapSetObjectDensity( map[m], mapx[m], mapy[m], old_density+delta_density, local_object[m]);
			assert(layer->effective_mode == LAYER_MODE_EDITABLE || layer->layer->zmap_parent->map_info.genesis_data);
            terrainSourceLayerSetUnsaved(layer);
		}
    }
}

typedef struct ScanLine
{
	F32 min_x;
	F32 min_y;
	F32 max_x;
	F32 max_y;
} ScanLine;

static void terrain_draw_line(Vec3 v1, Vec3 v2, ScanLine *buffer, F32 z_begin, U32 buffer_height)
{
	int dominant_axis;
	Vec3 delta, pos, dest;
	U32 pz;
	F32 min_y = MIN(v1[1], v2[1]);
	F32 max_y = MAX(v1[1], v2[1]);

	// Find the dominant axis of movement
	subVec3(v2, v1, delta);
	if (fabs(delta[0]) > fabs(delta[2]))
	{
		dominant_axis = 0;
	}
	else
	{
		dominant_axis = 2;
	}
	// Sort endpoints
	if (delta[dominant_axis] > 0)
	{
		copyVec3(v1, pos);
		copyVec3(v2, dest);
	}
	else
	{
		copyVec3(v2, pos);
		copyVec3(v1, dest);
	}
	// Normalize dominant axis movement to 1'
	if (fabs(delta[dominant_axis]) > 1.f)
	{
		F32 scale = 1.f / delta[dominant_axis];
		scaleVec3(delta, scale, delta);
	}
	else
		delta[dominant_axis] = 100.f; // Makes sure the following loop ends after one iteration

	// Walk along dominant axis
	do {
		pz = (U32)(pos[2]-z_begin);
		assert(pz < buffer_height);
		if (pos[0] < buffer[pz].min_x)
		{
			buffer[pz].min_x = pos[0];
			buffer[pz].min_y = pos[1];
		}
		if (pos[0] > buffer[pz].max_x)
		{
			buffer[pz].max_x = pos[0];
			buffer[pz].max_y = pos[1];
		}
		addVec3(pos, delta, pos);
		pos[1] = CLAMP(pos[1], min_y, max_y);
	} while ((int)pos[dominant_axis] <= (int)dest[dominant_axis]);
}

#include "tga.h"
void terrain_draw_object_debug(F32 *buffer, U32 width, U32 height, int object_no, int frame_no)
{
	U32 i;
	U8 *char_buf = calloc(1, 4*width*height);
	F32 min = 5000, max = -5000;
	F32 range;
	char filename[256];
	for (i = 0; i < width*height; i++)
	{
		if (buffer[i] > max) max = buffer[i];
		if (buffer[i] < min &&
			buffer[i] > -5000)
			min = buffer[i];
	}
	min--;
	range = MAX(1,max-min);
	for (i = 0; i < width*height; i++)
	{
		U8 val = CLAMP((buffer[i]-min)*255.f/range, 0, 255);
		char_buf[i*4+0] = val;
		char_buf[i*4+1] = val;
		char_buf[i*4+2] = val;
		char_buf[i*4+3] = 255;
	}

	sprintf(filename, "C:\\vaccusuck\\terrain_objectA_%02d_%03d.tga", object_no, frame_no);
	tgaSave(filename, char_buf, width, height, 4);

	for (i = 0; i < width*height; i++)
	{
		U8 val = buffer[i+width*height]*255;
		char_buf[i*4+0] = val;
		char_buf[i*4+1] = val;
		char_buf[i*4+2] = val;
		char_buf[i*4+3] = 255;
	}

	sprintf(filename, "C:\\vaccusuck\\terrain_objectB_%02d_%03d.tga", object_no, frame_no);
	tgaSave(filename, char_buf, width, height, 4);

	SAFE_FREE(char_buf);
}

void terrainSourceDoVaccuform(TerrainEditorSource *source, VaccuformObjectInternal *object)
{
	int i, j, x, z;
	static const F32 falloff_constant = -4.159; // = ln (1/64), 1/64 is the value of alpha after (falloff) iterations
	static const int dx[] = { -1, 0, 1, 1, 1, 0, -1, -1 };
	static const int dz[] = { -1, -1, -1, 0, 1, 1, 1, 0 };

	F32 *height_buffer[2], *alpha_buffer[2];
	F32 x_begin, z_begin;
	int buffer_width, buffer_height;
	ScanLine *scanline_buffer;
	F32 falloffA, falloffB, falloffC;
	bool changed;
	F32 delta_factor = 1.f;

	if (object->vert_count < 3 || object->tri_count < 1)
		return;

	if (object->max[0] <= object->min[0] || object->max[2] <= object->min[2])
		return;

	falloffA = exp(falloff_constant/object->falloff);
	falloffB = exp(falloff_constant*1.4142/object->falloff);
	falloffC = exp(falloff_constant*0.2/object->falloff);

	object->min[0] -= object->falloff;
	object->min[2] -= object->falloff;
	object->max[0] += object->falloff;
	object->max[2] += object->falloff;

	// Create buffers
	object->buffer_width = buffer_width = (U32)(object->max[0]-object->min[0]) + 3;
	object->buffer_height = buffer_height = (U32)(object->max[2]-object->min[2]) + 3;
	x_begin = floor(object->min[0]) - 1.f;
	z_begin = floor(object->min[2]) - 1.f;

	if (buffer_width > 2048 || buffer_height > 2048)
	{
		Alertf("Attempting to draw objects bigger than 8192 ft. into terrain! Skipping object.");
		return;
	}

	scanline_buffer = calloc(buffer_height, sizeof(ScanLine));
	for (i = 0; i < buffer_height; i++)
	{
		scanline_buffer[i].min_x = object->max[0]+1;
		scanline_buffer[i].min_y = -1e8;
		scanline_buffer[i].max_x = object->min[0]-1;
		scanline_buffer[i].max_y = -1e8;
	}

	height_buffer[0] = calloc(buffer_width*buffer_height*2, sizeof(F32));
	alpha_buffer[0] = &height_buffer[0][buffer_width*buffer_height];
	for (i = 0; i < buffer_width*buffer_height; i++)
	{
		height_buffer[0][i] = -1e8;
		alpha_buffer[0][i] = 0;
	}

	height_buffer[1] = calloc(buffer_width*buffer_height*2, sizeof(F32));
	alpha_buffer[1] = &height_buffer[1][buffer_width*buffer_height];

	// Draw triangles
	for (i = 0; i < object->tri_count; i++)
	{
		assert(object->inds[i*3+0] < (U32)object->vert_count);
		assert(object->inds[i*3+1] < (U32)object->vert_count);
		assert(object->inds[i*3+2] < (U32)object->vert_count);
		terrain_draw_line(&object->verts[object->inds[i*3+0]*3],
			&object->verts[object->inds[i*3+1]*3], scanline_buffer, z_begin, buffer_height);
		terrain_draw_line(&object->verts[object->inds[i*3+1]*3],
			&object->verts[object->inds[i*3+2]*3], scanline_buffer, z_begin, buffer_height);
		terrain_draw_line(&object->verts[object->inds[i*3+2]*3],
			&object->verts[object->inds[i*3+0]*3], scanline_buffer, z_begin, buffer_height);

		for (z = 0; z < buffer_height; z++)
		{
			U32 start_x = (U32)(scanline_buffer[z].min_x-x_begin);
			U32 end_x = (U32)(scanline_buffer[z].max_x-x_begin);
			if (scanline_buffer[z].min_x > scanline_buffer[z].max_x)
				continue;
			if (start_x == end_x)
			{
				if (scanline_buffer[z].min_y > height_buffer[0][start_x + z*buffer_width])
				{
					height_buffer[0][start_x + z*buffer_width] = scanline_buffer[z].min_y;
				}
				alpha_buffer[0][start_x + z*buffer_width] = 1.f;
			}
			else
			{
				F32 y = scanline_buffer[z].min_y;
				F32 min_y = MIN(scanline_buffer[z].max_y, scanline_buffer[z].min_y);
				F32 max_y = MAX(scanline_buffer[z].max_y, scanline_buffer[z].min_y);
				F32 delta_y = (scanline_buffer[z].max_y-scanline_buffer[z].min_y) /
					(scanline_buffer[z].max_x-scanline_buffer[z].min_x);
				for (x = start_x; x <= (int)end_x; x++)
				{
					if (y > height_buffer[0][x + z*buffer_width])
					{
						height_buffer[0][x + z*buffer_width] = y;
					}
					alpha_buffer[0][x + z*buffer_width] = 1.f;
					y = CLAMP(y + delta_y, min_y, max_y);
				}
			}
			scanline_buffer[z].min_x = object->max[0]+1;
			scanline_buffer[z].min_y = -1e8;
			scanline_buffer[z].max_x = object->min[0]-1;
			scanline_buffer[z].max_y = -1e8;
		}
	}

	for (z = 0; z < buffer_height; z++)
		for (x = 0; x < buffer_width; x++)
			if (height_buffer[0][x+z*buffer_width] < object->min[1]-1)
				height_buffer[0][x+z*buffer_width] = 0.5f * (object->min[1]+object->max[1]);

	for (i = 0; i < object->falloff+1; i++)
	{
		F32 falloff[] = { falloffB, falloffA, falloffB, falloffA, falloffB, falloffA, falloffB, falloffA };
		for (z = 0; z < buffer_height; z++)
			for (x = 0; x < buffer_width; x++)
			{
				F32 alpha = alpha_buffer[i%2][x+z*buffer_width];
				F32 max_alpha = 1.f;

				if (alpha < 1.f)
				{
					max_alpha = 0.f;
					for (j = 0; j < 8; j++)
					{
						int px = x+dx[j];
						int pz = z+dz[j];
						if (px >= 0 && pz >= 0 &&
							px < buffer_width && pz < buffer_height)
						{
							if (alpha_buffer[i%2][px+pz*buffer_width] * falloff[j] > max_alpha)
							{
								max_alpha = alpha_buffer[i%2][px+pz*buffer_width] * falloff[j];
							}
						}
					}
				}
				alpha_buffer[(i+1)%2][x+z*buffer_width] = MAX(max_alpha, alpha);
			}
	}
	if (i%2 == 1)
	{
		memcpy(alpha_buffer[0], alpha_buffer[1], buffer_width*buffer_height*sizeof(F32));
	}

	//terrain_draw_object_debug(height_buffer[0], buffer_width, buffer_height, 0, 0); // Debug only

	i = 0;
	do
	{
		F32 delta_scale[] = { 0.7071f, 1.f, 0.7071f, 1.f, 0.7071f, 1.f, 0.7071f, 1.f };
		changed = false;
		for (z = 0; z < buffer_height; z++)
			for (x = 0; x < buffer_width; x++)
			{
				F32 alpha = alpha_buffer[i%2][x+z*buffer_width];
				F32 height = height_buffer[i%2][x+z*buffer_width];
				F32 target_height = height;
				F32 dest_height;

				height_buffer[(i+1)%2][x+z*buffer_width] = height;

				if (alpha < 1.f)
				{
					for (j = 0; j < 8; j++)
					{
						int px = x+dx[j];
						int pz = z+dz[j];
						if (px >= 0 && pz >= 0 &&
							px < buffer_width && pz < buffer_height)
						{
							if (alpha_buffer[i%2][px+pz*buffer_width] > alpha)
							{
								F32 delta = (height_buffer[i%2][px+pz*buffer_width] - target_height) * delta_scale[j];
								target_height += delta;
							}
						}
					}

					dest_height = lerp(height, target_height, delta_factor);
					if (fabs(dest_height - height) > 0.1f)
					{
						height_buffer[(i+1)%2][x+z*buffer_width] = dest_height;
						changed = true;
					}
				}
			}

		delta_factor *= falloffC;
		++i;
		//terrain_draw_object_debug(height_buffer[i%2], buffer_width, buffer_height, 0, i); // Debug only
	}
	while (changed);

	object->buffers = height_buffer[i%2];
	SAFE_FREE(height_buffer[1-(i%2)]);

	for (i = 0; i < buffer_width*buffer_height; i++)
	{
		F32 alpha = CLAMP(object->buffers[buffer_width*buffer_height+i]*1.2, 0, 1);
		alpha = (1-alpha)*(1-alpha);
		alpha *= alpha;
		object->buffers[buffer_width*buffer_height+i] = 1-alpha;
	}

	//terrain_draw_object_debug(object->buffers, buffer_width, buffer_height, 0, 0); // Debug only

	SAFE_FREE(scanline_buffer);
	SAFE_FREE(object->verts);
	SAFE_FREE(object->inds);
}

bool terrain_sample_object_buffers(VaccuformObjectInternal *object, F32 x, F32 z, F32 *height, F32 *alpha)
{
	if (x >= object->min[0] && x <= object->max[0] &&
		z >= object->min[2] && z <= object->max[2])
	{
		int px = x-object->min[0];
		int pz = z-object->min[2];
		*height = object->buffers[px+pz*object->buffer_width];
		*alpha = object->buffers[px+pz*object->buffer_width + object->buffer_height*object->buffer_width];
		return true;
	}
	return false;
}

void terrainSourceFinishVaccuform(TerrainEditorSource *source, VaccuformObjectInternal **objects)
{
	int t;
	F32 x, z, px, pz, step;
	int begin_x, begin_z;
	TerrainBrushState state = { 0 };
	Vec3 all_max = { -1e8,-1e8,-1e8 }, all_min = { 1e8,1e8,1e8 };
    HeightMapCache cache = { 0 };
    F32 filter_value;
    TerrainBrushFilterBuffer filter;
	F32 *alpha_list;

	for (t = 0; t < eaSize(&objects); t++)
	{
		vec3RunningMinMax(objects[t]->min, all_min, all_max);
		vec3RunningMinMax(objects[t]->max, all_min, all_max);
	}

	begin_x = (U32)all_min[0];
	begin_x -= (begin_x % 4);
	begin_z = (U32)all_min[2];
	begin_z -= (begin_z % 4);

	step = (F32)(1 << source->visible_lod) / 4;

	for (z = begin_z; z <= all_max[2]; z += step)
		for (x = begin_x; x <= all_max[0]; x += step)
		{
			F32 orig_height, out_height = 0.f, out_alpha = 1.f, alpha_total = 0.f;
			if (terrainSourceGetHeight(source, x*4, z*4, &orig_height, NULL))
			{
				for (t = 0; t < eaSize(&objects); t++)
				{
					F32 height, alpha;
					if (terrain_sample_object_buffers(objects[t], x, z, &height, &alpha))
					{
						out_height += height*alpha;
						out_alpha *= (1-alpha);
						alpha_total += alpha;
					}
				}
				if (alpha_total > 0.004f)
				{
					out_height = (out_height/alpha_total);
					terrainSourceDrawHeight(source, x*4, z*4, source->visible_lod, (out_height-orig_height)*(1-out_alpha), NULL);
				}
			}
		}

	state.visible_lod = source->visible_lod;

    filter.buffer = &filter_value;
	filter.height = 1;
	filter.width = 1;
	filter.x_offset = 0;
	filter.y_offset = 0;
	filter.rel_x_cntr = 0;
	filter.rel_y_cntr = 0;
	filter.lod = GET_COLOR_LOD(source->visible_lod);     

	alpha_list = ScratchAlloc(eaSize(&objects) * sizeof(F32));

	for (z = begin_z; z <= all_max[2]; z += step)
		for (x = begin_x; x <= all_max[0]; x += step)
		{
			F32 height, alpha_total = 0.f, out_alpha = 1.f;
			for (t = 0; t < eaSize(&objects); t++)
			{
				alpha_list[t] = 0.f;
				terrain_sample_object_buffers(objects[t], x, z, &height, &alpha_list[t]);
				alpha_total += alpha_list[t];
				out_alpha *= (1-alpha_list[t]);
			}
			for (t = 0; t < eaSize(&objects); t++)
			{
				F32 alpha;
				if (alpha_list[t] > 0.f)
				{
					alpha = (alpha_list[t] / alpha_total) * (1-out_alpha);
					for (pz = z*4; pz < (z+step)*4; pz += step)
						for (px = x*4; px < (x+step)*4; px += step)
						{
							F32 sel;
							if (terrainSourceGetSelection(source, px, pz, &sel, &cache))
								terrainSourceDrawSelection(source, px, pz, source->visible_lod, alpha-sel, &cache);
							if (objects[t]->multibrush)
								terrainBrushApplyOptimized(source, &state, px, pz, step, &filter, &cache,
																objects[t]->multibrush, 1.f, false, false);
						}
				}
			}
		}

	for (t = 0; t < eaSize(&objects); t++)
	{
		SAFE_FREE(objects[t]->buffers);
		if (objects[t]->multibrush)
			terEdDestroyCompiledMultiBrush(objects[t]->multibrush);
	}
	ScratchFree(alpha_list);
}

bool terrainSourceStitchNeighbors(TerrainEditorSource *source)
{
	bool ret = false;
	int layer1, layer2;
    int num_layers = zmapGetLayerCount(NULL);

    for (layer2 = 0; layer2 < num_layers; layer2++)
    {
		ZoneMapLayer *src_layer = zmapGetLayer(NULL, layer2);
		assert(src_layer);
        if (src_layer->layer_mode != LAYER_MODE_EDITABLE)
        {
            int n_block, n_num_blocks = eaSize(&src_layer->terrain.blocks);
            
            for (n_block = 0; n_block < n_num_blocks; n_block++)
            {
                IVec2 n_min, n_max;
                if (layerGetTerrainBlockExtents(src_layer, n_block, n_min, n_max))
                {
                    TerrainBlockRange temp_range = { 0 };
                
                    for (layer1 = 0; layer1 < eaSize(&source->layers); layer1++)
                    {
                        if (source->layers[layer1]->effective_mode == LAYER_MODE_EDITABLE)
                        {
                            TerrainEditorSourceLayer *edit_layer = source->layers[layer1];
                            U32 lod = edit_layer->loaded_lod;
                            int block, num_blocks = eaSize(&edit_layer->blocks);
                            int map;

                            bool found = false;
                            int block_flags = 0;

                            for (block = 0; block < num_blocks; block++)
                            {
                                TerrainBlockRange *range = edit_layer->blocks[block];
                                
                                if (((range->range.max_block[0] >= n_min[0] && n_max[0] >= range->range.min_block[0]) &&
                                     range->range.max_block[2] == n_min[1]-1))
                                {
                                    block_flags |= 1;
                                }

                                if (((range->range.max_block[0] >= n_min[0] && n_max[0] >= range->range.min_block[0]) &&
                                     n_max[1] == range->range.min_block[2]-1))
                                {
                                    block_flags |= 2;
                                }

                                if (((range->range.max_block[2] >= n_min[1] && n_max[1] >= range->range.min_block[2]) &&
                                     range->range.max_block[0] == n_min[0]-1))
                                {
                                    block_flags |= 4;
                                }

                                if (((range->range.max_block[2] >= n_min[1] && n_max[1] >= range->range.min_block[2]) &&
                                     n_max[0] == range->range.min_block[0]-1))
                                {
                                    block_flags |= 8;
                                }
                                
                                if (block_flags > 0)
                                {
                                    if (eaSize(&temp_range.map_cache) == 0)
                                    {
                                        TerrainBlockRange *orig_range = layerGetTerrainBlock(src_layer, n_block);
                                        temp_range.block_idx = orig_range->block_idx;
                                        copyVec3(orig_range->range.max_block, temp_range.range.max_block);
                                        copyVec3(orig_range->range.min_block, temp_range.range.min_block);
                                        temp_range.range_name = strdup(orig_range->range_name);
						
                                        layerTerrainUpdateFilenames(src_layer, &temp_range);
                                        terrainBlockLoadSource(src_layer, &temp_range, false);
                                        SAFE_FREE(temp_range.range_name);
                                    }

                                    for (map = 0; map < eaSize(&temp_range.map_cache); map++)
                                    {
                                        HeightMap *height_map = temp_range.map_cache[map];
                                        IVec2 local_pos;
                                        heightMapGetMapLocalPos(height_map, local_pos);
                                        if (heightMapGetLoadedLOD(height_map) >= lod)
                                        {
                                            U32 i;
                                            F32 old_height;
                                            TerrainBuffer *height_buf;
                                            if (block_flags > 0)
                                            {
                                                heightMapResample(height_map, lod);
                                                height_buf = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
                                            }
                                            for (i = 0; i < GRID_LOD(lod); i++)
                                            {
                                                if (block_flags & 1 &&
                                                    terrainSourceGetHeight( source, local_pos[0]*GRID_BLOCK_SIZE + (i<<lod), local_pos[1]*GRID_BLOCK_SIZE, &old_height, NULL))
                                                {
                                                    F32 new_height = height_buf->data_f32[i];
                                                    if (fabs(new_height-old_height) > 0.0001f)
                                                    {
                                                        terrainSourceDrawHeight( source, local_pos[0]*GRID_BLOCK_SIZE + (i<<lod), local_pos[1]*GRID_BLOCK_SIZE,
                                                                                 lod, new_height - old_height, NULL );
                                                        ret = true;
                                                    }
                                                }
                                                if (block_flags & 2 &&
                                                    terrainSourceGetHeight( source, local_pos[0]*GRID_BLOCK_SIZE + (i<<lod), (local_pos[1]+1)*GRID_BLOCK_SIZE, &old_height, NULL))
                                                {
                                                    F32 new_height = height_buf->data_f32[(GRID_LOD(lod)-1)*GRID_LOD(lod) + i];
                                                    if (fabs(new_height-old_height) > 0.0001f)
                                                    {
                                                        terrainSourceDrawHeight( source, local_pos[0]*GRID_BLOCK_SIZE + (i<<lod), (local_pos[1]+1)*GRID_BLOCK_SIZE,
                                                                                 lod, new_height - old_height, NULL );
                                                        ret = true;
                                                    }
                                                }
                                                if (block_flags & 4 &&
                                                    terrainSourceGetHeight( source, local_pos[0]*GRID_BLOCK_SIZE, local_pos[1]*GRID_BLOCK_SIZE + (i<<lod), &old_height, NULL))
                                                {
                                                    F32 new_height = height_buf->data_f32[i*GRID_LOD(lod)];
                                                    if (fabs(new_height-old_height) > 0.0001f)
                                                    {
                                                        terrainSourceDrawHeight( source, local_pos[0]*GRID_BLOCK_SIZE, local_pos[1]*GRID_BLOCK_SIZE + (i<<lod),
                                                                                 lod, new_height - old_height, NULL );
                                                        ret = true;
                                                    }
                                                }
                                                if (block_flags & 8 &&
                                                    terrainSourceGetHeight( source, (local_pos[0]+1)*GRID_BLOCK_SIZE, local_pos[1]*GRID_BLOCK_SIZE + (i<<lod), &old_height, NULL))
                                                {
                                                    F32 new_height = height_buf->data_f32[GRID_LOD(lod)-1 + i*GRID_LOD(lod)];
                                                    if (fabs(new_height-old_height) > 0.0001f)
                                                    {
                                                        terrainSourceDrawHeight( source, (local_pos[0]+1)*GRID_BLOCK_SIZE, local_pos[1]*GRID_BLOCK_SIZE + (i<<lod),
                                                                                 lod, new_height - old_height, NULL );
                                                        ret = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                       
                    eaDestroyEx(&temp_range.map_cache, heightMapDestroy);
                }
            }
        }
    }
	return ret;
}

//// Updates

void terrainSourceUpdateTrackers(TerrainEditorSource *source, bool force, bool update_collision)
{
    int i, l;
    for (l = 0; l < eaSize(&source->layers); l++)
        for (i = 0; i < eaSize(&source->layers[l]->heightmap_trackers); i++)
            heightMapTrackerUpdate(source->layers[l]->heightmap_trackers[i], force, update_collision, false);
} 

void terrainSourceBrushMouseUp(TerrainEditorSource *source, TerrainChangeList *change_list)
{
    int i;
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator(source->heightmaps, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		HeightMap *height_map = (HeightMap *)stashElementGetPointer(elem);
        heightMapDoUpsamples(height_map, change_list->draw_lod, change_list->keep_high_res);
        heightMapFindUnusedObjects(height_map);
        heightMapFindUnusedMaterials(height_map, &change_list->mat_change_list);
        { // TomY Debug only!
            TerrainEditorSourceLayer *layer = terrainSourceGetHeightMapLayer(source, height_map);
	        heightMapValidateMaterials(height_map, layer->material_lookup, source->material_table);
        }
        heightMapSaveBackupBuffers(height_map, &change_list->map_backup_list);
    }
    for (i = 0; i < eaSize(&source->layers); i++)
	{
    	if (source->layers[i]->added_mat_list)
        {
            eaPushEArray(&change_list->added_mat_list.list, &source->layers[i]->added_mat_list->list);
            source->layers[i]->added_mat_list = NULL;
        }
	}
}

//// Undo Functions

void terrainUndoMaterialChanges(ZoneMapLayer *layer, TerrainMaterialChangeList *mat_change_list, bool is_undo)
{
	int i, dir;
    if (!mat_change_list || !mat_change_list->list)
        return;
    if (is_undo)
    {
        i = eaSize(&mat_change_list->list)-1;
        dir = -1;
    }
    else
    {
        i = 0;
        dir = 1;
    }
	for (; i >= 0 && i < eaSize(&mat_change_list->list); i += dir)
	{
		TerrainMaterialChange *mat_change = mat_change_list->list[i];
		HeightMap *heightMap = mat_change->height_map;
		TerrainBuffer *material_buffer = heightMapGetBuffer(heightMap, TERRAIN_BUFFER_MATERIAL, heightMapGetLoadedLOD(heightMap));

		if(material_buffer && heightMap)
		{
			//printf("Undoing material change (%d,%d)\n", heightMap->map_local_pos[0], heightMap->map_local_pos[1]);

            if (is_undo)
				heightMapRearrangeMaterialWeights(heightMap, material_buffer,
                                              	mat_change->new_material_ids, mat_change->new_material_count,
                                              	mat_change->old_material_ids, mat_change->old_material_count);
            else
				heightMapRearrangeMaterialWeights(heightMap, material_buffer,
                                                  mat_change->old_material_ids, mat_change->old_material_count,
                                                  mat_change->new_material_ids, mat_change->new_material_count);
		}
		else
		{
			printf("ERROR: Undoing material change FAILED!\n");
		}
    }
}

void terrainSourceUndoBackupBuffer(TerrainEditorSource *source, HeightMapBackupList *map_backup_list)
{
	int i,j;

    if (map_backup_list)
    {
        for(i=0; i < eaSize(&map_backup_list->list); i++)
        {
            HeightMapBackup *backup = map_backup_list->list[i];
            HeightMap *height_map = backup->height_map;

            for(j=0; j < eaSize(&backup->backup_buffers); j++)
            {
                heightMapApplyBackupBuffer(height_map, backup->backup_buffers[j]);
            }
        }
    }
}


//// Saving

bool terrainSourceSaveLayer(TerrainEditorSourceLayer *layer, bool force)
{
	int blocknum, i;
	IVec2 local_pos;
	U32 loaded_lod = layer->loaded_lod;
	U32 color_loaded_lod = GET_COLOR_LOD(loaded_lod);
	bool passed_validation;

	for (blocknum = 0; blocknum < eaSize(&layer->blocks); blocknum++)
	{
		TerrainBlockRange *block = layer->blocks[blocknum];
		if (block->interm_file)
		{
			//printf("Closing read-only intermediate %s %p\n", hogFileGetArchiveFileName(block->interm_file), block);
			hogFileDestroy(block->interm_file, true);
			block->interm_file = NULL;
		}
    	layerTerrainUpdateFilenames(layer->layer, block);
        if (!terrainStartSavingBlock(block))
        {
			layer->layer->zmap_parent->failed_validation = true;
			layer->layer->saving = false;
            return false;
        }
	}

    // Delete old blocks
    for (blocknum = 0; blocknum < eaSize(&layer->layer->terrain.blocks); blocknum++)
    {
        bool found = false;
        for (i = 0; i < eaSize(&layer->blocks); i++)
            if (!strcmp(layer->blocks[i]->range_name, layer->layer->terrain.blocks[blocknum]->range_name))
            {
                found = true;
                break;
            }
        if (!found)
        {
	        terrainDeleteBlock(layer->layer->terrain.blocks[blocknum]);
            StructDestroy(parse_TerrainBlockRange, layer->layer->terrain.blocks[blocknum]);
            eaRemove(&layer->layer->terrain.blocks, blocknum);
            --blocknum;
        }
    }

	printf("Saving %s...\n", layer->layer->filename);

	for (blocknum = 0; blocknum < eaSize(&layer->blocks); blocknum++)
	{
		TerrainBlockRange *block = layer->blocks[blocknum];
		bool needs_save = force;

        if (!needs_save)
        {
            for (local_pos[1] = block->range.min_block[2]; local_pos[1] <= block->range.max_block[2]; ++local_pos[1])
            {
                for (local_pos[0] = block->range.min_block[0]; local_pos[0] <= block->range.max_block[0]; ++local_pos[0])
                {
                    HeightMap *height_map = terrainSourceGetHeightMap(layer->source, local_pos);
                    if (height_map && heightMapIsUnsaved(height_map))
                    {
                        needs_save = true;
                        break;
                    }
                }
            }
        }

		if (needs_save)
		{
			if (!terrainBlockSaveSource(block, blocknum, loaded_lod, layer->source->heightmaps))
			{
				layer->layer->zmap_parent->failed_validation = true;
				layer->layer->saving = false;
                return false;
			}
		}
	}
	// Validate the saved files
	passed_validation = true;
	printf("Validating...");
	for (blocknum = 0; blocknum < eaSize(&layer->blocks); blocknum++)
	{
		TerrainBlockRange *block = layer->blocks[blocknum];
		if (!terrainBlockValidateSource(layer->layer, block))
		{
			Errorf("Terrain layer failed validation in block %s! Your layer has not been saved. Please find a programmer immediately.", block->range_name);
			passed_validation = false;
		}
	}
	if (passed_validation)
	{
		printf("Passed.\n");

		for (blocknum = 0; blocknum < eaSize(&layer->blocks); blocknum++)
		{
			TerrainBlockRange *block = layer->blocks[blocknum];
			if (!terrainBlockCommitSource(layer->layer, block))
			{
				Errorf("Terrain layer failed writing files in block %s! Your layer has not been saved. Please find a programmer immediately.", block->range_name);
				layer->layer->zmap_parent->failed_validation = true;
				layer->layer->saving = false;
				return false;
			}
		}
		layer->layer->terrain.unsaved_changes = false;
	}
	else
	{
		layer->layer->zmap_parent->failed_validation = true;
		layer->layer->saving = false;
		printf("Failed.\n");
		return false;
	}

	return true;
}

void terrainSourceBeginSaveLayer(TerrainEditorSourceLayer *layer)
{
    int i;
	// Copy over relevant info

	if (layer->blocks == layer->layer->terrain.blocks &&
		layer->blocks == 0)
		return;

	layer->layer->terrain.non_playable = !layer->playable;
	layer->layer->terrain.exclusion_version = layer->exclusion_version;
	layer->layer->terrain.color_shift = layer->color_shift;

	eaDestroyEx(&layer->layer->terrain.material_table, StructFreeString);
	eaDestroyStruct(&layer->layer->terrain.object_table, parse_TerrainObjectEntry);
	for (i = 0; i < eaSize(&layer->layer->terrain.blocks); i++)
		deinitTerrainBlock(layer->layer->terrain.blocks[i]);
	eaDestroyStruct(&layer->layer->terrain.blocks, parse_TerrainBlockRange);

    for (i = 0; i < eaiSize(&layer->material_lookup); i++)
        eaPush(&layer->layer->terrain.material_table, StructAllocString(layer->source->material_table[layer->material_lookup[i]]));

    for (i = 0; i < eaiSize(&layer->object_lookup); i++)
        eaPush(&layer->layer->terrain.object_table, StructClone(parse_TerrainObjectEntry, layer->source->object_table[layer->object_lookup[i]]));

	for (i = 0; i < eaSize(&layer->blocks); i++)
        eaPush(&layer->layer->terrain.blocks, StructClone(parse_TerrainBlockRange, layer->blocks[i]));
}

void terrainSourceFinishSaveLayer(TerrainEditorSourceLayer *layer)
{
    int blocknum;

	layer->layer->saving = false;
	layer->saved = 1;

	for (blocknum = 0; blocknum < eaSize(&layer->blocks); blocknum++)
	{
		TerrainBlockRange *block = layer->blocks[blocknum];
		if (block->interm_file)
		{
			//printf("Closing read-only intermediate %s %p\n", hogFileGetArchiveFileName(block->interm_file), block);
			hogFileDestroy(block->interm_file, true);
			block->interm_file = NULL;
		}
        //eaPush(&tlayer.blocks, block);
        terrainDoneSavingBlock(layer->blocks[blocknum]);
    }
}

void terrainSourceSetVisibleLOD(TerrainEditorSource *source, int lod)
{
    int i;
    for (i = 0; i < eaSize(&source->layers); i++)
    {
        terrainSourceSetLOD(source->layers[i], lod);
    }
    source->visible_lod = lod;
}

void terrainSourceChangeListDestroy(TerrainChangeList *change_list)
{
	if (change_list->mat_change_list)
	{
		eaDestroyEx(&change_list->mat_change_list->list, NULL);
		SAFE_FREE(change_list->mat_change_list);
	}
	eaDestroyEx(&change_list->added_mat_list.list, NULL);
	if (change_list->map_backup_list)
	{
		heightMapFreeBackupBuffers(change_list->map_backup_list);
	}
	SAFE_FREE(change_list);
}

int terrainChangeListGetMemorySize(TerrainChangeList *change_list)
{
	int i, j;
	int size = sizeof(TerrainChangeList);
    if (change_list->mat_change_list)
        size += eaSize(&change_list->mat_change_list->list) * sizeof(TerrainMaterialChange);
    size += eaSize(&change_list->added_mat_list.list) * sizeof(TerrainMaterialChange);
    if (change_list->map_backup_list)
    {
        for (i = 0; i < eaSize(&change_list->map_backup_list->list); i++)
        {
            size += sizeof(HeightMapBackup) +
                eaSize(&change_list->map_backup_list->list[i]->backup_buffers)*sizeof(TerrainBuffer);
            for (j = 0; j < eaSize(&change_list->map_backup_list->list[i]->backup_buffers); j++)
            	size += GetTerrainBufferSize(change_list->map_backup_list->list[i]->backup_buffers[j]);
        }
    }
	return size;
}

#endif
